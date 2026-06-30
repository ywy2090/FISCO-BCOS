/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Ethereum builtin precompile registration (EIP-198..7212).
 */

#include "EthBuiltinRegistry.h"
#include "BlsGas.h"
#include "ModexpGas.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "wedpr-crypto/WedprCrypto.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <evmone_precompiles/blake2b.hpp>
#include <evmone_precompiles/bls.hpp>
#include <evmone_precompiles/bn254.hpp>
#include <evmone_precompiles/kzg.hpp>
#include <evmone_precompiles/modexp.hpp>
#include <evmone_precompiles/ripemd160.hpp>
#include <evmone_precompiles/secp256r1.hpp>
#include <evmone_precompiles/sha256.hpp>
#include <intx/intx.hpp>
#include <span>
#include <stdexcept>
#include <string>

using namespace bcos;
using namespace bcos::crypto;
using bcos::evm::parseModexpLengths;

#define ETH_REGISTER_PRECOMPILED(Name)                                                        \
    static std::pair<bool, bytes> __eth_registerPrecompiledFunction##Name(bytesConstRef _in); \
    static std::pair<bool, bytes> __eth_registerPrecompiledFunction##Name

#define ETH_REGISTER_PRECOMPILED_PRICER(Name)                        \
    static bigint __eth_registerPricerFunction##Name(bytesConstRef); \
    static bigint __eth_registerPricerFunction##Name

namespace
{
std::pair<bool, bytes> ecrecoverImpl(bytesConstRef _in)
{
    constexpr int RSV_LENGTH = 65;
    constexpr int PUBLIC_KEY_LENGTH = 64;
    if (_in.size() <= 128 - 32)
    {
        return {true, {}};
    }

    byte rawRSV[RSV_LENGTH] = {0};
    memcpy(rawRSV, _in.data() + 64, std::min(_in.size() - 64, (size_t)(RSV_LENGTH - 1)));
    rawRSV[RSV_LENGTH - 1] = (byte)((int)_in[63] - 27);
    crypto::HashType mHash;
    memcpy(mHash.data(), _in.data(), crypto::HashType::SIZE);

    crypto::PublicPtr pk;
    try
    {
        pk = crypto::secp256k1Recover(mHash, bytesConstRef(rawRSV, RSV_LENGTH));
    }
    catch (...)
    {
        return {true, {}};
    }

    std::pair<bool, bytes> ret{true, bytes(crypto::HashType::SIZE, 0)};
    if (pk == nullptr)
    {
        return {true, {}};
    }
    CInputBuffer pubkeyBuffer{pk->constData(), PUBLIC_KEY_LENGTH};
    COutputBuffer pubkeyHash{(char*)ret.second.data(), crypto::HashType::SIZE};
    if (wedpr_keccak256_hash(&pubkeyBuffer, &pubkeyHash) != 0)
    {
        return {true, {}};
    }
    memset(ret.second.data(), 0, 12);
    return ret;
}

ETH_REGISTER_PRECOMPILED(ecrecover)(bytesConstRef _in)
{
    return ecrecoverImpl(_in);
}

ETH_REGISTER_PRECOMPILED(sha256)(bytesConstRef _in)
{
    bytes output(evmone::crypto::SHA256_HASH_SIZE, 0);
    evmone::crypto::sha256(reinterpret_cast<std::byte*>(output.data()),
        reinterpret_cast<const std::byte*>(_in.data()), _in.size());
    return {true, std::move(output)};
}

ETH_REGISTER_PRECOMPILED(ripemd160)(bytesConstRef _in)
{
    bytes output(32, 0);
    evmone::crypto::ripemd160(reinterpret_cast<std::byte*>(output.data() + 12),
        reinterpret_cast<const std::byte*>(_in.data()), _in.size());
    return {true, std::move(output)};
}

ETH_REGISTER_PRECOMPILED(identity)(bytesConstRef _in)
{
    return {true, _in.toBytes()};
}

ETH_REGISTER_PRECOMPILED(modexp)(bytesConstRef _in)
{
    // EIP-198: big-number modular exponentiation (base^exp) % mod.
    // https://github.com/ethereum/EIPs/blob/master/EIPS/eip-198.md
    auto const lens = parseModexpLengths(_in);
    size_t const baseLen = lens.baseLen;
    size_t const expLen = lens.expLen;
    size_t const modLen = lens.modLen;

    if (modLen == 0)
    {
        // EIP-198: output length is modLen; skip padded() allocations when mod is absent.
        return {true, {}};
    }

    // Zero-pad inputs to declared lengths (EIP-198: missing bytes are right-padded with zeros)
    auto padded = [&](size_t offset, size_t len) -> bytes {
        bytes buf(len, 0);
        size_t const avail = _in.size() > offset ? _in.size() - offset : 0;
        if (avail > 0)
            std::memcpy(buf.data(), _in.data() + offset, std::min(len, avail));
        return buf;
    };
    bytes const baseBuf = padded(96, baseLen);
    bytes const expBuf = padded(96 + baseLen, expLen);
    bytes const modBuf = padded(96 + baseLen + expLen, modLen);

    // EIP-198: if mod is zero, return all-zero output
    bool const modZero =
        std::all_of(modBuf.begin(), modBuf.end(), [](uint8_t b) { return b == 0; });
    if (modZero)
        return {true, bytes(modLen, 0)};

    bytes output(modLen, 0);
    evmone::crypto::modexp(std::span<const uint8_t>{baseBuf}, std::span<const uint8_t>{expBuf},
        std::span<const uint8_t>{modBuf}, output.data());
    return {true, std::move(output)};
}

// Legacy EIP-198 pricer for EvmPrecompiledTest::modexp_pricer_* and direct registrar use.
// Production modexp gas uses PrecompiledContract::modexp() -> calcModexpGas(revision).
ETH_REGISTER_PRECOMPILED_PRICER(modexp)(bytesConstRef _in)
{
    return bcos::evm::calcModexpGasEip198Public(_in);
}

ETH_REGISTER_PRECOMPILED(alt_bn128_G1_add)(bytesConstRef _in)
{
    using namespace evmmax::bn254;

    uint8_t buf[128]{};
    std::memcpy(buf, _in.data(), std::min(_in.size(), sizeof(buf)));

    const auto p = AffinePoint::from_bytes(std::span<const uint8_t, 64>{buf, 64});
    const auto q = AffinePoint::from_bytes(std::span<const uint8_t, 64>{buf + 64, 64});
    if (!p.has_value() || !q.has_value() || !validate(*p) || !validate(*q))
        return {false, bytes(64, 0)};

    bytes output(64, 0);
    evmmax::ecc::add_affine(*p, *q).to_bytes(std::span<uint8_t, 64>{output.data(), 64});
    return {true, std::move(output)};
}

ETH_REGISTER_PRECOMPILED(alt_bn128_G1_mul)(bytesConstRef _in)
{
    using namespace evmmax::bn254;

    uint8_t buf[96]{};
    std::memcpy(buf, _in.data(), std::min(_in.size(), sizeof(buf)));

    const auto p = AffinePoint::from_bytes(std::span<const uint8_t, 64>{buf, 64});
    if (!p.has_value() || !validate(*p))
        return {false, bytes(64, 0)};

    const auto c = intx::be::unsafe::load<intx::uint256>(buf + 64);
    bytes output(64, 0);
    mul(*p, c).to_bytes(std::span<uint8_t, 64>{output.data(), 64});
    return {true, std::move(output)};
}

ETH_REGISTER_PRECOMPILED(alt_bn128_pairing_product)(bytesConstRef _in)
{
    static constexpr size_t PAIR_SIZE = 192;
    if (_in.size() % PAIR_SIZE != 0)
        return {false, bytes(32, 0)};

    using namespace evmmax::bn254;
    using intx::be::unsafe::load;

    std::vector<std::pair<Point, ExtPoint>> pairs;
    pairs.reserve(_in.size() / PAIR_SIZE);
    for (const uint8_t* ptr = _in.data(); ptr != _in.data() + _in.size(); ptr += PAIR_SIZE)
    {
        const auto g1 = AffinePoint::from_bytes(std::span<const uint8_t, 64>{ptr, 64});
        if (!g1.has_value() || !validate(*g1))
            return {false, bytes(32, 0)};

        const ExtPoint g2{{load<intx::uint256>(ptr + 96), load<intx::uint256>(ptr + 64)},
            {load<intx::uint256>(ptr + 160), load<intx::uint256>(ptr + 128)}};
        pairs.emplace_back(Point{g1->x.value(), g1->y.value()}, g2);
    }

    bytes output(32, 0);
    auto const result = pairing_check(pairs);
    if (!result.has_value())
        return {false, bytes(32, 0)};
    if (*result)
        output[31] = 1;
    return {true, std::move(output)};
}

ETH_REGISTER_PRECOMPILED_PRICER(alt_bn128_pairing_product)
(bytesConstRef _in)
{
    auto const k = _in.size() / 192;
    return 45000 + k * 34000;
}

ETH_REGISTER_PRECOMPILED(blake2_compression)(bytesConstRef _in)
{
    static constexpr size_t totalInputSize = 213;
    if (_in.size() != totalInputSize)
        return {false, {}};

    // EIP-152 §spec:
    //   rounds — 32-bit unsigned big-endian word
    //   h      — 8  unsigned 64-bit little-endian words (64 bytes)
    //   m      — 16 unsigned 64-bit little-endian words (128 bytes)
    //   t0, t1 — 2  unsigned 64-bit little-endian words (8 bytes each)
    //   f      — final block indicator flag (1 byte)
    //   Output: return the updated state vector h with unchanged encoding (little-endian)
    auto const rounds = fromBigEndian<uint32_t>(_in.getCroppedData(0, 4));
    uint64_t h[8]{};
    uint64_t m[16]{};
    uint64_t t[2]{};

    // Use std::memcpy to load little-endian words.  On all supported platforms
    // (x86-64, AArch64) native byte order is little-endian, so a direct memory
    // copy produces the correct integer value without any byte swapping.
    for (size_t i = 0; i < 8; ++i)
        std::memcpy(&h[i], _in.data() + 4 + i * 8, 8);
    for (size_t i = 0; i < 16; ++i)
        std::memcpy(&m[i], _in.data() + 68 + i * 8, 8);
    std::memcpy(&t[0], _in.data() + 196, 8);
    std::memcpy(&t[1], _in.data() + 204, 8);

    auto const finalBlockIndicator = _in[212];
    if (finalBlockIndicator != 0 && finalBlockIndicator != 1)
        return {false, {}};
    auto const last = finalBlockIndicator != 0;

    evmone::crypto::blake2b_compress(rounds, h, m, t, last);

    // Output h[] back as little-endian bytes (unchanged encoding per EIP-152).
    bytes output(64, 0);
    for (size_t i = 0; i < 8; ++i)
        std::memcpy(output.data() + i * 8, &h[i], 8);
    return {true, std::move(output)};
}

ETH_REGISTER_PRECOMPILED_PRICER(blake2_compression)
(bytesConstRef _in)
{
    auto const rounds = fromBigEndian<uint32_t>(_in.getCroppedData(0, 4));
    return rounds;
}

// The precompiled contract for point evaluation, EIP-4844:
// https://eips.ethereum.org/EIPS/eip-4844#point-evaluation-precompile
ETH_REGISTER_PRECOMPILED(point_evaluation)(bytesConstRef _in)
{
    static constexpr size_t versioned_hash_size = 32;
    static constexpr size_t z_end_bound = 64;
    static constexpr size_t y_end_bound = 96;
    static constexpr size_t commitment_end_bound = 144;
    static constexpr size_t proof_end_bound = 192;

    if (_in.size() != 192)
        return {false, {}};

    std::array<std::byte, evmone::crypto::SHA256_HASH_SIZE> expectedVersionedHash{};
    evmone::crypto::sha256(expectedVersionedHash.data(),
        reinterpret_cast<const std::byte*>(_in.data() + y_end_bound),
        commitment_end_bound - y_end_bound);
    expectedVersionedHash[0] = evmone::crypto::VERSIONED_HASH_VERSION_KZG;
    if (!std::equal(expectedVersionedHash.begin(), expectedVersionedHash.end(),
            reinterpret_cast<const std::byte*>(_in.data())))
        return {false, {}};

    bool ok = evmone::crypto::kzg_verify_proof(reinterpret_cast<const std::byte*>(_in.data()),
        reinterpret_cast<const std::byte*>(_in.data() + versioned_hash_size),
        reinterpret_cast<const std::byte*>(_in.data() + z_end_bound),
        reinterpret_cast<const std::byte*>(_in.data() + y_end_bound),
        reinterpret_cast<const std::byte*>(_in.data() + commitment_end_bound));
    if (!ok)
        return {false, {}};

    // Return FIELD_ELEMENTS_PER_BLOB and BLS_MODULUS as padded 32 byte big endian values
    // return turn and Bytes(U256(FIELD_ELEMENTS_PER_BLOB).to_be_bytes32() +
    // U256(BLS_MODULUS).to_be_bytes32()) refer to
    // https://github.com/erigontech/silkworm/blob/85ba5171e88855a6702602d38f102aae9b896f9c/silkworm/core/execution/precompile.cpp#L502-L524
    return {
        true, bcos::fromHex("000000000000000000000000000000000000000000000000000000000000100073eda"
                            "753299d7d483339d80809a1d80553bda402fffe5bfeffffffff00000001")};
}

ETH_REGISTER_PRECOMPILED_PRICER(point_evaluation)(bytesConstRef _in)
{
    return 50000;
}

// EIP-2537 BLS12-381 precompiles (Prague-gated).

ETH_REGISTER_PRECOMPILED(bls12_g1add)(bytesConstRef _in)
{
    constexpr size_t INPUT_SIZE = 256;
    if (_in.size() != INPUT_SIZE)
        return {false, {}};
    std::array<uint8_t, INPUT_SIZE> in{};
    std::copy_n(_in.data(), INPUT_SIZE, in.data());
    std::array<uint8_t, 128> out{};
    bool const ok = evmone::crypto::bls::g1_add(
        out.data(), out.data() + 64, in.data(), in.data() + 64, in.data() + 128, in.data() + 192);
    if (!ok)
        return {false, {}};
    return {true, bytes(out.begin(), out.end())};
}
ETH_REGISTER_PRECOMPILED_PRICER(bls12_g1add)(bytesConstRef)
{
    return u256(375);
}

ETH_REGISTER_PRECOMPILED(bls12_g1msm)(bytesConstRef _in)
{
    constexpr size_t PAIR_SIZE = 160;
    if (_in.empty() || _in.size() % PAIR_SIZE != 0)
        return {false, {}};
    std::array<uint8_t, 128> out{};
    bool const ok =
        evmone::crypto::bls::g1_msm(out.data(), out.data() + 64, _in.data(), _in.size());
    if (!ok)
        return {false, {}};
    return {true, bytes(out.begin(), out.end())};
}
ETH_REGISTER_PRECOMPILED_PRICER(bls12_g1msm)(bytesConstRef _in)
{
    constexpr size_t PAIR_SIZE = 160;
    // EIP-2537: k = floor(len(input) / PAIR_SIZE). Only k == 0 (empty or too short for one pair)
    // returns zero gas. For k >= 1 the formula charges gas even if the input length is not
    // divisible — the precompile execution will reject malformed input, but gas is already
    // charged (matching go-ethereum behaviour).
    auto const k = _in.size() / PAIR_SIZE;
    return u256(bcos::evm::precompiled::blsG1MsmGas(k));
}

ETH_REGISTER_PRECOMPILED(bls12_g2add)(bytesConstRef _in)
{
    constexpr size_t INPUT_SIZE = 512;
    if (_in.size() != INPUT_SIZE)
        return {false, {}};
    std::array<uint8_t, INPUT_SIZE> in{};
    std::copy_n(_in.data(), INPUT_SIZE, in.data());
    std::array<uint8_t, 256> out{};
    bool const ok = evmone::crypto::bls::g2_add(
        out.data(), out.data() + 128, in.data(), in.data() + 128, in.data() + 256, in.data() + 384);
    if (!ok)
        return {false, {}};
    return {true, bytes(out.begin(), out.end())};
}
ETH_REGISTER_PRECOMPILED_PRICER(bls12_g2add)(bytesConstRef)
{
    return u256(600);
}

ETH_REGISTER_PRECOMPILED(bls12_g2msm)(bytesConstRef _in)
{
    constexpr size_t PAIR_SIZE = 288;
    if (_in.empty() || _in.size() % PAIR_SIZE != 0)
        return {false, {}};
    std::array<uint8_t, 256> out{};
    bool const ok =
        evmone::crypto::bls::g2_msm(out.data(), out.data() + 128, _in.data(), _in.size());
    if (!ok)
        return {false, {}};
    return {true, bytes(out.begin(), out.end())};
}
ETH_REGISTER_PRECOMPILED_PRICER(bls12_g2msm)(bytesConstRef _in)
{
    constexpr size_t PAIR_SIZE = 288;
    // EIP-2537: k = floor(len(input) / PAIR_SIZE). Only k == 0 (empty or too short for one pair)
    // returns zero gas. For k >= 1 the formula charges gas even if the input length is not
    // divisible — the precompile execution will reject malformed input, but gas is already
    // charged (matching go-ethereum behaviour).
    auto const k = _in.size() / PAIR_SIZE;
    return u256(bcos::evm::precompiled::blsG2MsmGas(k));
}

ETH_REGISTER_PRECOMPILED(bls12_pairing_check)(bytesConstRef _in)
{
    constexpr size_t PAIR_SIZE = 384;
    if (_in.empty() || _in.size() % PAIR_SIZE != 0)
        return {false, {}};
    std::array<uint8_t, 32> out{};
    bool const ok = evmone::crypto::bls::pairing_check(out.data(), _in.data(), _in.size());
    if (!ok)
        return {false, {}};
    return {true, bytes(out.begin(), out.end())};
}
ETH_REGISTER_PRECOMPILED_PRICER(bls12_pairing_check)(bytesConstRef _in)
{
    constexpr size_t PAIR_SIZE = 384;
    // EIP-2537: k = floor(len(input) / PAIR_SIZE). Only k == 0 (empty or too short for one pair)
    // returns zero gas. For k >= 1 the formula charges gas even if the input length is not
    // divisible — the precompile execution will reject malformed input, but gas is already
    // charged (matching go-ethereum behaviour).
    auto const k = static_cast<int64_t>(_in.size() / PAIR_SIZE);
    if (k == 0)
        return u256(0);
    return u256(37700 + 32600 * k);
}

ETH_REGISTER_PRECOMPILED(bls12_map_fp_to_g1)(bytesConstRef _in)
{
    constexpr size_t INPUT_SIZE = 64;
    if (_in.size() != INPUT_SIZE)
        return {false, {}};
    std::array<uint8_t, INPUT_SIZE> in{};
    std::copy_n(_in.data(), INPUT_SIZE, in.data());
    std::array<uint8_t, 128> out{};
    bool const ok = evmone::crypto::bls::map_fp_to_g1(out.data(), out.data() + 64, in.data());
    if (!ok)
        return {false, {}};
    return {true, bytes(out.begin(), out.end())};
}
ETH_REGISTER_PRECOMPILED_PRICER(bls12_map_fp_to_g1)(bytesConstRef)
{
    return u256(5500);
}

ETH_REGISTER_PRECOMPILED(bls12_map_fp2_to_g2)(bytesConstRef _in)
{
    constexpr size_t INPUT_SIZE = 128;
    if (_in.size() != INPUT_SIZE)
        return {false, {}};
    std::array<uint8_t, INPUT_SIZE> in{};
    std::copy_n(_in.data(), INPUT_SIZE, in.data());
    std::array<uint8_t, 256> out{};
    bool const ok = evmone::crypto::bls::map_fp2_to_g2(out.data(), out.data() + 128, in.data());
    if (!ok)
        return {false, {}};
    return {true, bytes(out.begin(), out.end())};
}
ETH_REGISTER_PRECOMPILED_PRICER(bls12_map_fp2_to_g2)(bytesConstRef)
{
    return u256(23800);
}

// EIP-7212 / RIP-7212: secp256r1 (P-256) signature verification
// Input: 160 bytes = msg_hash(32) ++ r(32) ++ s(32) ++ x(32) ++ y(32)
// Output: 32 bytes with last byte 0x01 on success, empty on wrong-size or failed verify
// Address: 0x0100 (Osaka-gated via callBuiltInPrecompiled guard)
ETH_REGISTER_PRECOMPILED(p256verify)(bytesConstRef _in)
{
    static constexpr size_t INPUT_SIZE = 160;
    if (_in.size() != INPUT_SIZE)
        return {true, {}};  // EIP-7212: wrong size → success with empty output
    const auto* d = _in.data();
    ethash::hash256 h{};
    std::memcpy(h.bytes, d, 32);
    const auto r = intx::be::unsafe::load<intx::uint256>(d + 32);
    const auto s = intx::be::unsafe::load<intx::uint256>(d + 64);
    const auto qx = intx::be::unsafe::load<intx::uint256>(d + 96);
    const auto qy = intx::be::unsafe::load<intx::uint256>(d + 128);
    bool ok = evmmax::secp256r1::verify(h, r, s, qx, qy);
    if (!ok)
        return {true, {}};  // EIP-7212: invalid signature → success with empty output
    bytes output(32, 0);
    output[31] = 1;
    return {true, std::move(output)};
}

ETH_REGISTER_PRECOMPILED_PRICER(p256verify)(bytesConstRef)
{
    return u256(6900);  // EIP-7212 / evmone 0.21 gas cost
}

}  // namespace

namespace bcos::evm
{
namespace
{
[[noreturn]] void throwUnknownBuiltin(uint16_t suffix)
{
    throw std::out_of_range("Unknown builtin precompile suffix: " + std::to_string(suffix));
}
}  // namespace

PrecompiledExecutor const& builtinExecutorBySuffix(uint16_t suffix)
{
    static const PrecompiledExecutor ecrecover = __eth_registerPrecompiledFunctionecrecover;
    static const PrecompiledExecutor sha256 = __eth_registerPrecompiledFunctionsha256;
    static const PrecompiledExecutor ripemd160 = __eth_registerPrecompiledFunctionripemd160;
    static const PrecompiledExecutor identity = __eth_registerPrecompiledFunctionidentity;
    static const PrecompiledExecutor modexp = __eth_registerPrecompiledFunctionmodexp;
    static const PrecompiledExecutor bnAdd = __eth_registerPrecompiledFunctionalt_bn128_G1_add;
    static const PrecompiledExecutor bnMul = __eth_registerPrecompiledFunctionalt_bn128_G1_mul;
    static const PrecompiledExecutor bnPair =
        __eth_registerPrecompiledFunctionalt_bn128_pairing_product;
    static const PrecompiledExecutor blake2f = __eth_registerPrecompiledFunctionblake2_compression;
    static const PrecompiledExecutor pointEval = __eth_registerPrecompiledFunctionpoint_evaluation;
    static const PrecompiledExecutor blsG1Add = __eth_registerPrecompiledFunctionbls12_g1add;
    static const PrecompiledExecutor blsG1Msm = __eth_registerPrecompiledFunctionbls12_g1msm;
    static const PrecompiledExecutor blsG2Add = __eth_registerPrecompiledFunctionbls12_g2add;
    static const PrecompiledExecutor blsG2Msm = __eth_registerPrecompiledFunctionbls12_g2msm;
    static const PrecompiledExecutor blsPair = __eth_registerPrecompiledFunctionbls12_pairing_check;
    static const PrecompiledExecutor blsMapFp = __eth_registerPrecompiledFunctionbls12_map_fp_to_g1;
    static const PrecompiledExecutor blsMapFp2 =
        __eth_registerPrecompiledFunctionbls12_map_fp2_to_g2;
    static const PrecompiledExecutor p256verify = __eth_registerPrecompiledFunctionp256verify;

    switch (suffix)
    {
    case 0x0001:
        return ecrecover;
    case 0x0002:
        return sha256;
    case 0x0003:
        return ripemd160;
    case 0x0004:
        return identity;
    case 0x0005:
        return modexp;
    case 0x0006:
        return bnAdd;
    case 0x0007:
        return bnMul;
    case 0x0008:
        return bnPair;
    case 0x0009:
        return blake2f;
    case 0x000a:
        return pointEval;
    case 0x000b:
        return blsG1Add;
    case 0x000c:
        return blsG1Msm;
    case 0x000d:
        return blsG2Add;
    case 0x000e:
        return blsG2Msm;
    case 0x000f:
        return blsPair;
    case 0x0010:
        return blsMapFp;
    case 0x0011:
        return blsMapFp2;
    case 0x0100:
        return p256verify;
    default:
        throwUnknownBuiltin(suffix);
    }
}

PrecompiledPricer const& builtinPricerBySuffix(uint16_t suffix)
{
    static const PrecompiledPricer modexp = __eth_registerPricerFunctionmodexp;
    static const PrecompiledPricer bnPair = __eth_registerPricerFunctionalt_bn128_pairing_product;
    static const PrecompiledPricer blake2f = __eth_registerPricerFunctionblake2_compression;
    static const PrecompiledPricer pointEval = __eth_registerPricerFunctionpoint_evaluation;
    static const PrecompiledPricer blsG1Add = __eth_registerPricerFunctionbls12_g1add;
    static const PrecompiledPricer blsG1Msm = __eth_registerPricerFunctionbls12_g1msm;
    static const PrecompiledPricer blsG2Add = __eth_registerPricerFunctionbls12_g2add;
    static const PrecompiledPricer blsG2Msm = __eth_registerPricerFunctionbls12_g2msm;
    static const PrecompiledPricer blsPair = __eth_registerPricerFunctionbls12_pairing_check;
    static const PrecompiledPricer blsMapFp = __eth_registerPricerFunctionbls12_map_fp_to_g1;
    static const PrecompiledPricer blsMapFp2 = __eth_registerPricerFunctionbls12_map_fp2_to_g2;
    static const PrecompiledPricer p256verify = __eth_registerPricerFunctionp256verify;

    switch (suffix)
    {
    case 0x0005:
        return modexp;
    case 0x0008:
        return bnPair;
    case 0x0009:
        return blake2f;
    case 0x000a:
        return pointEval;
    case 0x000b:
        return blsG1Add;
    case 0x000c:
        return blsG1Msm;
    case 0x000d:
        return blsG2Add;
    case 0x000e:
        return blsG2Msm;
    case 0x000f:
        return blsPair;
    case 0x0010:
        return blsMapFp;
    case 0x0011:
        return blsMapFp2;
    case 0x0100:
        return p256verify;
    default:
        throwUnknownBuiltin(suffix);
    }
}

bool hasBuiltinPricerBySuffix(uint16_t suffix) noexcept
{
    switch (suffix)
    {
    case 0x0005:
    case 0x0008:
    case 0x0009:
    case 0x000a:
    case 0x000b:
    case 0x000c:
    case 0x000d:
    case 0x000e:
    case 0x000f:
    case 0x0010:
    case 0x0011:
    case 0x0100:
        return true;
    default:
        return false;
    }
}

}  // namespace bcos::evm
