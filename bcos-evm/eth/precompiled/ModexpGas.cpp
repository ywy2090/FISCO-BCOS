/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Modexp (0x05) precompile gas: EIP-198 / EIP-2565 / EIP-7883 by revision.
 *  @file ModexpGas.cpp
 *
 *  Pricing formulas mirror geth `modexpMultComplexity` / `modexpIterationCount` and the
 *  legacy EIP-198 `multComplexity` piecewise curve. Public entry: `calcModexpGas`.
 */

#include "ModexpGas.h"
#include "bcos-evm/eth/precompiled/PrecompiledAddress.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <evmc/evmc.h>
#include <algorithm>
#include <limits>
#include <utility>

using namespace std;
using namespace bcos;

namespace bcos::evm
{
namespace
{
/// Read @p count bytes at @p begin, zero-padded on the right (modexp exp head semantics).
bigint parseBigEndianRightPadded(bytesConstRef in, bigint const& begin, bigint const& count)
{
    if (begin > in.count())
    {
        return 0;
    }
    assert(count <= numeric_limits<size_t>::max() / 8);

    size_t const beginOffset{begin};
    size_t const countBytes{count};

    bytesConstRef const cropped =
        in.getCroppedData(beginOffset, min(countBytes, in.count() - beginOffset));

    bigint ret = fromBigEndian<bigint>(cropped);
    assert(countBytes - cropped.count() <= numeric_limits<size_t>::max() / 8);
    ret <<= 8 * (countBytes - cropped.count());
    return ret;
}

/// One 32-byte big-endian length word from the modexp header (offset 0 / 32 / 64).
bigint parseHeaderLen(bytesConstRef in, size_t offset)
{
    return parseBigEndianRightPadded(in, offset, MODEXP_LEN_FIELD_SIZE);
}

/// Effective exponent bit-length for gas (EIP-2565 / shared by 198 path).
/// expOffset = MODEXP_HEADER_SIZE + baseLen points at the start of the exponent field.
bigint expLengthAdjustEip2565(bigint const& expOffset, bigint const& expLength, bytesConstRef in)
{
    if (expLength <= MODEXP_LEN_FIELD_SIZE)
    {
        bigint const exp(parseBigEndianRightPadded(in, expOffset, expLength));
        return exp ? msb(exp) : 0;
    }
    bigint const expFirstWord(parseBigEndianRightPadded(in, expOffset, MODEXP_LEN_FIELD_SIZE));
    size_t const highestBit(expFirstWord ? msb(expFirstWord) : 0);
    return 8 * (expLength - MODEXP_LEN_FIELD_SIZE) + highestBit;
}

/// EIP-198 multiplication complexity (piecewise on max(baseLen, modLen)).
bigint multComplexityEip198(bigint const& x)
{
    if (x <= MODEXP_EIP198_COMPLEXITY_SMALL_MAX)
    {
        return x * x;
    }
    if (x <= MODEXP_EIP198_COMPLEXITY_MID_MAX)
    {
        return (x * x) / 4 + MODEXP_HEADER_SIZE * x - 3072;
    }
    return (x * x) / 16 + 480 * x - 199680;
}

/// EIP-2565: complexity = ceil(maxLen/8)^2 (word-based).
bigint modexpMultComplexityEip2565(bigint const& maxLenBytes)
{
    bigint const words = (maxLenBytes + 7) / 8;
    return words * words;
}

/// EIP-7883: complexity = 16 when max(base,mod) ≤ 32, else 2 * ceil(maxLen/8)^2.
bigint modexpMultComplexityEip7883(size_t baseLen, size_t modLen)
{
    size_t const maxLen = std::max(baseLen, modLen);
    if (maxLen <= MODEXP_EIP7883_SMALL_INPUT_MAX)
    {
        return MODEXP_EIP7883_SMALL_COMPLEXITY;
    }
    bigint const words = (bigint(maxLen) + 7) / 8;
    return 2 * words * words;
}

/// EIP-7883 iteration count: uses low 256 bits of exponent head when expLen > 32.
bigint modexpIterationCountEip7883(size_t expLen, bytesConstRef in, size_t baseLen)
{
    size_t const expOffset = MODEXP_HEADER_SIZE + baseLen;
    if (expLen <= MODEXP_LEN_FIELD_SIZE)
    {
        bigint const exp = expLen == 0 ? 0 : parseBigEndianRightPadded(in, expOffset, expLen);
        if (expLen <= MODEXP_LEN_FIELD_SIZE && exp == 0)
        {
            return 0;
        }
        return exp ? msb(exp) : 0;
    }
    bigint const expHead(parseBigEndianRightPadded(in, expOffset, MODEXP_LEN_FIELD_SIZE));
    static bigint const kMask256 = (bigint(1) << MODEXP_EIP7883_EXP_HEAD_BITS) - 1;
    bigint const exp256 = expHead & kMask256;
    size_t const highestBit = exp256 ? msb(exp256) : 0;
    return MODEXP_EIP7883_EXP_BIT_MULTIPLIER * (expLen - MODEXP_LEN_FIELD_SIZE) + highestBit;
}

/// gas = multComplexity(max(base,mod)) * max(adjustedExpBits, 1) / 20
bigint calcModexpGasEip198(bytesConstRef in)
{
    bigint const baseLength(parseHeaderLen(in, MODEXP_BASE_LEN_OFFSET));
    bigint const expLength(parseHeaderLen(in, MODEXP_EXP_LEN_OFFSET));
    bigint const modLength(parseHeaderLen(in, MODEXP_MOD_LEN_OFFSET));

    bigint const maxLength(max(modLength, baseLength));
    bigint const adjustedExpLength(
        expLengthAdjustEip2565(baseLength + MODEXP_HEADER_SIZE, expLength, in));

    return multComplexityEip198(maxLength) * max<bigint>(adjustedExpLength, 1) /
           MODEXP_GAS_DIVISOR_EIP198;
}

/// gas = complexity * max(iterationCount, 1) / 3, floor 200 (EIP-2565).
bigint calcModexpGasEip2565(bytesConstRef in)
{
    bigint const baseLength(parseHeaderLen(in, MODEXP_BASE_LEN_OFFSET));
    bigint const expLength(parseHeaderLen(in, MODEXP_EXP_LEN_OFFSET));
    bigint const modLength(parseHeaderLen(in, MODEXP_MOD_LEN_OFFSET));

    bigint const maxLength(max(modLength, baseLength));
    bigint const iterationCount(
        expLengthAdjustEip2565(baseLength + MODEXP_HEADER_SIZE, expLength, in));
    bigint const complexity = modexpMultComplexityEip2565(maxLength);
    bigint const gas = complexity * max<bigint>(iterationCount, 1) / MODEXP_GAS_DIVISOR_EIP2565;
    static bigint const kMinGas{MODEXP_MIN_GAS_EIP2565};
    return gas < kMinGas ? kMinGas : gas;
}

/// gas = complexity * max(iterationCount, 1), floor 500 (EIP-7883 / Osaka+).
bigint calcModexpGasEip7883(bytesConstRef in)
{
    auto const lens = parseModexpLengths(in);
    bigint const mc = modexpMultComplexityEip7883(lens.baseLen, lens.modLen);
    bigint const iter = modexpIterationCountEip7883(lens.expLen, in, lens.baseLen);
    bigint const gas = mc * max<bigint>(iter, 1);
    static bigint const kMinGas{MODEXP_MIN_GAS_EIP7883};
    return gas < kMinGas ? kMinGas : gas;
}
}  // namespace

ModexpLengths parseModexpLengths(bytesConstRef input)
{
    ModexpLengths out;
    static bigint const kUint64Max = (bigint(1) << 64) - 1;

    auto const base = parseHeaderLen(input, MODEXP_BASE_LEN_OFFSET);
    auto const exp = parseHeaderLen(input, MODEXP_EXP_LEN_OFFSET);
    auto const mod = parseHeaderLen(input, MODEXP_MOD_LEN_OFFSET);

    // Lengths used for gas and 7823 validation; oversize headers → overflow (reject).
    auto assign = [&](bigint const& v, size_t& dst) {
        if (v > kUint64Max)
        {
            out.overflow = true;
            dst = 0;
            return;
        }
        dst = static_cast<size_t>(v.convert_to<uint64_t>());
    };

    assign(base, out.baseLen);
    assign(exp, out.expLen);
    assign(mod, out.modLen);
    return out;
}

bool validateModexpEip7823(bytesConstRef input, evmc_revision revision)
{
    // Pre-Osaka: no field bound (7823 is an Osaka EIP; revision gate matches geth).
    if (revision < EVMC_OSAKA)
    {
        return true;
    }
    auto const lens = parseModexpLengths(input);
    if (lens.overflow)
    {
        return false;
    }
    if (lens.baseLen > MODEXP_MAX_FIELD_LEN_EIP7823)
    {
        return false;
    }
    if (lens.expLen > MODEXP_MAX_FIELD_LEN_EIP7823)
    {
        return false;
    }
    if (lens.modLen > MODEXP_MAX_FIELD_LEN_EIP7823)
    {
        return false;
    }
    return true;
}

bool shouldRejectModexpEip7823(evmc_address const& addr, bytesConstRef input,
    const bcos::evm::RevisionConfig& rev, evmc_revision revision) noexcept
{
    if (!isModexpPrecompileEvmcAddress(addr))
    {
        return false;
    }
    if (!modexpEip7823Enabled(rev))
    {
        return false;
    }
    return !validateModexpEip7823(input, revision);
}

bool shouldRejectModexpEip7823(std::string_view addr, bytesConstRef input,
    const bcos::evm::RevisionConfig& rev, evmc_revision revision) noexcept
{
    if (!isModexpPrecompileAddress(addr))
    {
        return false;
    }
    if (!modexpEip7823Enabled(rev))
    {
        return false;
    }
    return !validateModexpEip7823(input, revision);
}

/// Fork dispatch for `EthPrecompiles::gasModexp` (Osaka → 7883, Berlin → 2565, else 198).
bigint calcModexpGas(bytesConstRef input, evmc_revision revision)
{
    if (revision >= EVMC_OSAKA)
    {
        return calcModexpGasEip7883(input);
    }
    if (revision >= EVMC_BERLIN)
    {
        return calcModexpGasEip2565(input);
    }
    return calcModexpGasEip198(input);
}

bigint calcModexpGasEip198Public(bytesConstRef input)
{
    return calcModexpGasEip198(input);
}

}  // namespace bcos::evm
