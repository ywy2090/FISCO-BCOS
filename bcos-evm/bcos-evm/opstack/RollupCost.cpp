#include <bcos-evm/opstack/RollupCost.h>

#include <bcos-evm/opstack/OpFeeParams.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <array>
#include <cstdint>

namespace bcos::evm::opstack
{
namespace
{
constexpr int64_t kL1CostIntercept = -42585600;
constexpr int64_t kL1CostFastlzCoef = 836500;
constexpr int64_t kMinTxSizeScaled = 100000000;
constexpr int64_t kFjordDivisor = 1000000000000;
constexpr int64_t kNonzeroByteCost = 16;
constexpr int64_t kZeroByteCost = 4;
constexpr int64_t kOperatorFeeScalarDivisor = 1000000;
// The 1e6 scaling factor for estimatedDaSizeScaled (semantically unrelated to the operator
// scalar's 1e6; do not merge them).
constexpr int64_t kDaSizeScaleDivisor = 1'000'000;
// Jovian operator fee: gas x scalar x 100 + constant (op-geth Jovian spec coefficient).
constexpr int64_t kJovianOperatorFeeMultiplier = 100;

// Port of op-geth FlzCompressLen: length of output if serializedTx were FastLZ-compressed.
uint32_t flzCompressLenImpl(evmc::bytes_view ib) noexcept
{
    uint32_t n = 0;
    std::array<uint32_t, 8192> ht{};

    auto const* const bytes = ib.data();
    auto const len = static_cast<uint32_t>(ib.size());

    auto u24 = [&](uint32_t i) -> uint32_t {
        return static_cast<uint32_t>(bytes[i]) | (static_cast<uint32_t>(bytes[i + 1]) << 8) |
               (static_cast<uint32_t>(bytes[i + 2]) << 16);
    };
    auto cmp = [&](uint32_t p, uint32_t q, uint32_t e) -> uint32_t {
        uint32_t l = 0;
        for (e -= q; l < e; ++l)
        {
            if (bytes[p + l] != bytes[q + l])
            {
                e = 0;
            }
        }
        return l;
    };
    auto literals = [&](uint32_t r) {
        n += 0x21 * (r / 0x20);
        r %= 0x20;
        if (r != 0)
        {
            n += r + 1;
        }
    };
    auto match = [&](uint32_t l) {
        --l;
        n += 3 * (l / 262);
        if (l % 262 >= 6)
        {
            n += 3;
        }
        else
        {
            n += 2;
        }
    };
    auto hash = [](uint32_t v) -> uint32_t { return ((2654435769U * v) >> 19) & 0x1fff; };
    auto setNextHash = [&](uint32_t ip) -> uint32_t {
        ht[hash(u24(ip))] = ip;
        return ip + 1;
    };

    uint32_t a = 0;
    uint32_t ipLimit = len - 13;
    if (len < 13)
    {
        ipLimit = 0;
    }

    for (uint32_t ip = a + 2; ip < ipLimit;)
    {
        uint32_t r = 0;  // read after the loop by cmp(); must outlive it
        for (;;)
        {
            auto const s = u24(ip);
            auto const h = hash(s);
            r = ht[h];
            ht[h] = ip;
            const uint32_t d = ip - r;
            if (ip >= ipLimit)
            {
                break;
            }
            ++ip;
            if (d <= 0x1fff && s == u24(r))
            {
                break;
            }
        }
        if (ip >= ipLimit)
        {
            break;
        }
        --ip;
        if (ip > a)
        {
            literals(ip - a);
        }
        auto const l = cmp(r + 3, ip + 3, ipLimit + 9);
        match(l);
        ip = setNextHash(setNextHash(ip + l));
        a = ip;
    }
    literals(len - a);
    return n;
}
}  // namespace

uint32_t flzCompressLen(evmc::bytes_view data) noexcept
{
    return flzCompressLenImpl(data);
}

intx::uint256 estimatedDaSizeScaled(uint32_t fastlzSize) noexcept
{
    const int64_t scaled = kL1CostIntercept + kL1CostFastlzCoef * static_cast<int64_t>(fastlzSize);
    const int64_t clamped = scaled < kMinTxSizeScaled ? kMinTxSizeScaled : scaled;
    return intx::uint256{static_cast<uint64_t>(clamped)};
}

uint64_t estimatedDaSizeFromFlz(uint32_t flzLen) noexcept
{
    if (flzLen == 0)
        return 0;
    return static_cast<uint64_t>(
        estimatedDaSizeScaled(flzLen) / intx::uint256{kDaSizeScaleDivisor});
}

uint64_t estimatedDaSize(evmc::bytes_view signedTxEnvelope) noexcept
{
    if (signedTxEnvelope.empty())
        return 0;
    return estimatedDaSizeFromFlz(flzCompressLen(signedTxEnvelope));
}

uint64_t bedrockCalldataGasUsed(evmc::bytes_view env) noexcept
{
    uint64_t zeroes = 0;
    uint64_t nonZeroes = 0;
    for (const auto b : env)
        (b == 0 ? zeroes : nonZeroes)++;
    return zeroes * static_cast<uint64_t>(kZeroByteCost) +
           nonZeroes * static_cast<uint64_t>(kNonzeroByteCost);
}

intx::uint256 computeL1CostFromFlz(
    const OpFeeParams& params, uint32_t flzLen, const OpForkConfig& cfg) noexcept
{
    (void)cfg;
    if (flzLen == 0)
        return intx::uint256{0};
    // 512-bit like the opValidate balance cap: the two whole-slot fee reads let these products
    // cross 2^256, where op-geth's big.Int evaluation does not wrap. Saturate on return — a fee
    // >= 2^256 exceeds any representable balance, so the cap rejects it either way.
    const auto calldataPerByte = intx::umul(params.l1_base_fee,
        intx::uint256{params.base_fee_scalar} * intx::uint256{kNonzeroByteCost});
    const auto blobPerByte =
        intx::umul(params.blob_base_fee, intx::uint256{params.blob_base_fee_scalar});
    // op-geth Fjord+:
    // estimatedDaSizeScaled(flz)*(l1BaseFee*16*baseScalar+blobBaseFee*blobScalar)/1e12
    const auto scaled = estimatedDaSizeScaled(flzLen);
    const auto fee =
        (calldataPerByte + blobPerByte) * intx::uint512{scaled} / intx::uint512{kFjordDivisor};
    if (fee > intx::uint512{~intx::uint256{0}})
        return ~intx::uint256{0};
    return static_cast<intx::uint256>(fee);
}

/// fee = (value * a * b) / divisor, saturating to uint256 max.
///
/// 512-bit intermediates, with a guard before each multiply. The unguarded form wrapped mod
/// 2^512 and charged a too-small fee: with (overhead, l1BaseFee, scalar) = (0, 2^255, 2^255)
/// and calldataGas 480 the true fee is ~2^498, yet 480 * 2^255 * 2^255 == 120 * 2^512 == 0
/// (mod 2^512), so the Bedrock arm charged zero. A zero factor keeps the mathematical result
/// of zero, matching op-geth's big.Int evaluation rather than saturating.
[[nodiscard]] intx::uint256 saturatingL1Fee(
    intx::uint512 value, intx::uint256 a, intx::uint256 b, intx::uint256 divisor) noexcept
{
    constexpr intx::uint256 c_maxU256 = ~intx::uint256{0};
    const intx::uint512 max512 = ~intx::uint512{0};
    if (a == 0 || b == 0 || value == 0)
    {
        return intx::uint256{0};
    }
    if (intx::uint512{a} > max512 / value)
    {
        return c_maxU256;  // value*a >= 2^512, so the fee is far above uint256 max
    }
    const intx::uint512 first = value * intx::uint512{a};
    if (intx::uint512{b} > max512 / first)
    {
        return c_maxU256;
    }
    const intx::uint512 fee = first * intx::uint512{b} / intx::uint512{divisor};
    if (fee > intx::uint512{c_maxU256})
    {
        return c_maxU256;
    }
    return static_cast<intx::uint256>(fee);
}

intx::uint256 computeL1Cost(
    const OpFeeParams& params, evmc::bytes_view signedTxEnvelope, const OpForkConfig& cfg) noexcept
{
    if (signedTxEnvelope.empty())
        return intx::uint256{0};

    // Ecotone-timestamped blocks keep the Pre-Ecotone formula until the new-formula slots go
    // live: the Ecotone activation block still runs setL1BlockValues (specs.optimism.io/
    // protocol/ecotone/l1-attributes.html), so slot3 scalars and slot7 are still zero and the
    // formula selection falls back on the same zero-probe op-geth uses.
    const bool bedrock = cfg.l1_fee_model == L1FeeModel::Bedrock ||
                         (cfg.l1_fee_model == L1FeeModel::Ecotone && !ecotoneL1SlotsLive(params));
    if (bedrock)
    {
        // op-geth newL1CostFuncBedrockHelper / l1CostHelper (exec-engine Pre-Ecotone):
        //   (rollupDataGas + overhead) * l1BaseFee * scalar / 1e6, evaluated in that order.
        // The sum is widened before the multiply: `overhead` is a whole-slot read, so
        // (calldataGas + overhead) can itself cross 2^256.
        const intx::uint512 gasPlusOverhead =
            intx::uint512{bedrockCalldataGasUsed(signedTxEnvelope)} +
            intx::uint512{params.overhead};
        return saturatingL1Fee(
            gasPlusOverhead, params.l1_base_fee, params.bedrock_scalar, intx::uint256{1'000'000});
    }

    if (cfg.l1_fee_model == L1FeeModel::Ecotone || cfg.has_ecotone_l1_formula)
    {
        // op-geth newL1CostFuncEcotone:
        //   calldataGas*(l1BaseFee*16*baseScalar + blobBaseFee*blobScalar)/16e6
        const auto calldataPerByte = intx::umul(params.l1_base_fee,
            intx::uint256{params.base_fee_scalar} * intx::uint256{kNonzeroByteCost});
        const auto blobPerByte =
            intx::umul(params.blob_base_fee, intx::uint256{params.blob_base_fee_scalar});
        const auto calldataGas = intx::uint256{bedrockCalldataGasUsed(signedTxEnvelope)};
        const auto fee = (calldataPerByte + blobPerByte) * intx::uint512{calldataGas} /
                         intx::uint512{16'000'000};
        if (fee > intx::uint512{~intx::uint256{0}})
            return ~intx::uint256{0};
        return static_cast<intx::uint256>(fee);
    }

    // Fjord+ (current implementation):
    //   estimatedDaSizeScaled(flz)*(l1BaseFee*16*baseScalar + blobBaseFee*blobScalar)/1e12
    return computeL1CostFromFlz(params, flzCompressLen(signedTxEnvelope), cfg);
}

intx::uint256 computeOperatorCost(
    const OpFeeParams& params, uint64_t gas, bool jovianFormula) noexcept
{
    if (jovianFormula)
    {
        return intx::uint256{gas} * intx::uint256{params.operator_fee_scalar} *
                   intx::uint256{kJovianOperatorFeeMultiplier} +
               intx::uint256{params.operator_fee_constant};
    }
    return intx::uint256{gas} * intx::uint256{params.operator_fee_scalar} /
               intx::uint256{kOperatorFeeScalarDivisor} +
           intx::uint256{params.operator_fee_constant};
}

intx::uint256 computeOperatorCost(
    const OpFeeParams& params, uint64_t gas, const OpForkConfig& cfg) noexcept
{
    return computeOperatorCost(params, gas, cfg.has_jovian_operator_formula);
}
}  // namespace bcos::evm::opstack
