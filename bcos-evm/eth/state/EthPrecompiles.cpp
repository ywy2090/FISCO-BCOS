/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file EthPrecompiles.cpp
 */

#include "bcos-evm/eth/state/EthPrecompiles.hpp"
#include "bcos-evm/eth/precompiled/ModexpGas.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <evmone_precompiles/blake2b.hpp>
#include <evmone_precompiles/bls.hpp>
#include <evmone_precompiles/bn254.hpp>
#include <evmone_precompiles/kzg.hpp>
#include <evmone_precompiles/modexp.hpp>
#include <evmone_precompiles/ripemd160.hpp>
#include <evmone_precompiles/secp256k1.hpp>
#include <evmone_precompiles/sha256.hpp>
#include <intx/intx.hpp>
#include <limits>
#include <span>
#include <vector>

namespace bcos::evm::state
{
namespace
{
bool isHigh18BytesZero(const evmc_address& address) noexcept
{
    for (size_t i = 0; i < 18; ++i)
    {
        if (address.bytes[i] != 0)
        {
            return false;
        }
    }
    return true;
}

std::optional<uint16_t> toSuffix(const evmc_address& address) noexcept
{
    if (!isHigh18BytesZero(address))
    {
        return std::nullopt;
    }
    auto const suffix = static_cast<uint16_t>((address.bytes[18] << 8) | address.bytes[19]);
    if (suffix < 0x0001 || suffix > 0x0011)
    {
        return std::nullopt;
    }
    return suffix;
}

int64_t safeBigintToI64(const bcos::bigint& value) noexcept
{
    if (value < 0)
    {
        return 0;
    }
    if (value > std::numeric_limits<int64_t>::max())
    {
        return std::numeric_limits<int64_t>::max();
    }
    return value.convert_to<int64_t>();
}

int64_t wordsCost(size_t inputSize, int64_t base, int64_t perWord) noexcept
{
    auto const words = static_cast<int64_t>((inputSize + 31) / 32);
    return base + words * perWord;
}

std::pair<bool, bcos::bytes> executeEcrecover(bcos::bytesConstRef input)
{
    std::array<uint8_t, 128> buffer{};
    std::copy_n(input.data(), std::min(input.size(), buffer.size()), buffer.data());

    auto const hash = std::span<uint8_t const, 32>{buffer.data(), 32};
    auto const vBytes = std::span<uint8_t const, 32>{buffer.data() + 32, 32};
    auto const sigBytes = std::span<uint8_t const, 64>{buffer.data() + 64, 64};

    auto const v = intx::be::unsafe::load<intx::uint256>(vBytes.data());
    if (v != 27 && v != 28)
    {
        return {true, {}};
    }
    auto const parity = v == 28;

    auto const recovered = evmmax::secp256k1::ecrecover(
        hash, sigBytes.subspan<0, 32>(), sigBytes.subspan<32, 32>(), parity);
    if (!recovered.has_value())
    {
        return {true, {}};
    }

    bcos::bytes output(32, 0);
    std::copy_n(recovered->bytes, sizeof(*recovered), output.data() + 12);
    return {true, std::move(output)};
}

std::pair<bool, bcos::bytes> executeModexp(bcos::bytesConstRef input)
{
    auto const lens = bcos::evm::parseModexpLengths(input);
    auto const baseLen = lens.baseLen;
    auto const expLen = lens.expLen;
    auto const modLen = lens.modLen;

    if (modLen == 0)
    {
        return {true, {}};
    }

    auto padded = [&](size_t offset, size_t len) -> bcos::bytes {
        bcos::bytes buffer(len, 0);
        auto const available = input.size() > offset ? input.size() - offset : 0;
        if (available > 0)
        {
            std::memcpy(buffer.data(), input.data() + offset, std::min(len, available));
        }
        return buffer;
    };

    auto const base = padded(96, baseLen);
    auto const exp = padded(96 + baseLen, expLen);
    auto const mod = padded(96 + baseLen + expLen, modLen);

    auto const modZero =
        std::all_of(mod.begin(), mod.end(), [](uint8_t byte) { return byte == 0; });
    if (modZero)
    {
        return {true, bcos::bytes(modLen, 0)};
    }

    bcos::bytes output(modLen, 0);
    evmone::crypto::modexp(std::span<const uint8_t>{base}, std::span<const uint8_t>{exp},
        std::span<const uint8_t>{mod}, output.data());
    return {true, std::move(output)};
}

std::pair<bool, bcos::bytes> executeBnAdd(bcos::bytesConstRef input)
{
    using namespace evmmax::bn254;
    uint8_t data[128]{};
    std::memcpy(data, input.data(), std::min(input.size(), sizeof(data)));
    auto const p = AffinePoint::from_bytes(std::span<const uint8_t, 64>{data, 64});
    auto const q = AffinePoint::from_bytes(std::span<const uint8_t, 64>{data + 64, 64});
    if (!p.has_value() || !q.has_value() || !validate(*p) || !validate(*q))
    {
        return {false, bcos::bytes(64, 0)};
    }
    bcos::bytes output(64, 0);
    evmmax::ecc::add_affine(*p, *q).to_bytes(std::span<uint8_t, 64>{output.data(), 64});
    return {true, std::move(output)};
}

std::pair<bool, bcos::bytes> executeBnMul(bcos::bytesConstRef input)
{
    using namespace evmmax::bn254;
    uint8_t data[96]{};
    std::memcpy(data, input.data(), std::min(input.size(), sizeof(data)));
    auto const p = AffinePoint::from_bytes(std::span<const uint8_t, 64>{data, 64});
    if (!p.has_value() || !validate(*p))
    {
        return {false, bcos::bytes(64, 0)};
    }
    auto const scalar = intx::be::unsafe::load<intx::uint256>(data + 64);
    bcos::bytes output(64, 0);
    mul(*p, scalar).to_bytes(std::span<uint8_t, 64>{output.data(), 64});
    return {true, std::move(output)};
}

std::pair<bool, bcos::bytes> executeBnPairing(bcos::bytesConstRef input)
{
    constexpr size_t PAIR_SIZE = 192;
    if (input.size() % PAIR_SIZE != 0)
    {
        return {false, bcos::bytes(32, 0)};
    }

    using namespace evmmax::bn254;
    using intx::be::unsafe::load;
    std::vector<std::pair<Point, ExtPoint>> pairs;
    pairs.reserve(input.size() / PAIR_SIZE);
    for (auto* ptr = input.data(); ptr != input.data() + input.size(); ptr += PAIR_SIZE)
    {
        auto const g1 = AffinePoint::from_bytes(std::span<const uint8_t, 64>{ptr, 64});
        if (!g1.has_value() || !validate(*g1))
        {
            return {false, bcos::bytes(32, 0)};
        }
        const ExtPoint g2{{load<intx::uint256>(ptr + 96), load<intx::uint256>(ptr + 64)},
            {load<intx::uint256>(ptr + 160), load<intx::uint256>(ptr + 128)}};
        pairs.emplace_back(Point{g1->x.value(), g1->y.value()}, g2);
    }

    bcos::bytes output(32, 0);
    auto const result = pairing_check(pairs);
    if (!result.has_value())
    {
        return {false, bcos::bytes(32, 0)};
    }
    if (*result)
    {
        output[31] = 1;
    }
    return {true, std::move(output)};
}

std::pair<bool, bcos::bytes> executeBlake2f(bcos::bytesConstRef input)
{
    constexpr size_t INPUT_SIZE = 213;
    if (input.size() != INPUT_SIZE)
    {
        return {false, {}};
    }
    auto const rounds = fromBigEndian<uint32_t>(input.getCroppedData(0, 4));
    uint64_t h[8]{};
    uint64_t m[16]{};
    uint64_t t[2]{};

    for (size_t i = 0; i < 8; ++i)
    {
        std::memcpy(&h[i], input.data() + 4 + i * 8, 8);
    }
    for (size_t i = 0; i < 16; ++i)
    {
        std::memcpy(&m[i], input.data() + 68 + i * 8, 8);
    }
    std::memcpy(&t[0], input.data() + 196, 8);
    std::memcpy(&t[1], input.data() + 204, 8);
    auto const finalFlag = input[212];
    if (finalFlag != 0 && finalFlag != 1)
    {
        return {false, {}};
    }
    evmone::crypto::blake2b_compress(rounds, h, m, t, finalFlag != 0);

    bcos::bytes output(64, 0);
    for (size_t i = 0; i < 8; ++i)
    {
        std::memcpy(output.data() + i * 8, &h[i], 8);
    }
    return {true, std::move(output)};
}

std::pair<bool, bcos::bytes> executePointEvaluation(bcos::bytesConstRef input)
{
    constexpr size_t INPUT_SIZE = 192;
    constexpr size_t VERSIONED_HASH_SIZE = 32;
    constexpr size_t Y_END = 96;
    constexpr size_t COMMITMENT_END = 144;
    if (input.size() != INPUT_SIZE)
    {
        return {false, {}};
    }

    std::array<std::byte, evmone::crypto::SHA256_HASH_SIZE> expected{};
    evmone::crypto::sha256(expected.data(),
        reinterpret_cast<const std::byte*>(input.data() + Y_END), COMMITMENT_END - Y_END);
    expected[0] = evmone::crypto::VERSIONED_HASH_VERSION_KZG;
    if (!std::equal(
            expected.begin(), expected.end(), reinterpret_cast<const std::byte*>(input.data())))
    {
        return {false, {}};
    }

    auto const ok =
        evmone::crypto::kzg_verify_proof(reinterpret_cast<const std::byte*>(input.data()),
            reinterpret_cast<const std::byte*>(input.data() + VERSIONED_HASH_SIZE),
            reinterpret_cast<const std::byte*>(input.data() + 64),
            reinterpret_cast<const std::byte*>(input.data() + Y_END),
            reinterpret_cast<const std::byte*>(input.data() + COMMITMENT_END));
    if (!ok)
    {
        return {false, {}};
    }
    return {
        true, bcos::fromHex("000000000000000000000000000000000000000000000000000000000000100073eda"
                            "753299d7d483339d80809a1d80553bda402fffe5bfeffffffff00000001")};
}

std::pair<bool, bcos::bytes> executeBlsG1Add(bcos::bytesConstRef input)
{
    constexpr size_t INPUT_SIZE = 256;
    if (input.size() != INPUT_SIZE)
    {
        return {false, {}};
    }
    std::array<uint8_t, INPUT_SIZE> in{};
    std::copy_n(input.data(), INPUT_SIZE, in.data());
    std::array<uint8_t, 128> out{};
    auto const ok = evmone::crypto::bls::g1_add(
        out.data(), out.data() + 64, in.data(), in.data() + 64, in.data() + 128, in.data() + 192);
    if (!ok)
    {
        return {false, {}};
    }
    return {true, bcos::bytes(out.begin(), out.end())};
}

std::pair<bool, bcos::bytes> executeBlsG1Msm(bcos::bytesConstRef input)
{
    constexpr size_t PAIR_SIZE = 160;
    if (input.empty() || input.size() % PAIR_SIZE != 0)
    {
        return {false, {}};
    }
    std::array<uint8_t, 128> out{};
    auto const ok =
        evmone::crypto::bls::g1_msm(out.data(), out.data() + 64, input.data(), input.size());
    if (!ok)
    {
        return {false, {}};
    }
    return {true, bcos::bytes(out.begin(), out.end())};
}

std::pair<bool, bcos::bytes> executeBlsG2Add(bcos::bytesConstRef input)
{
    constexpr size_t INPUT_SIZE = 512;
    if (input.size() != INPUT_SIZE)
    {
        return {false, {}};
    }
    std::array<uint8_t, INPUT_SIZE> in{};
    std::copy_n(input.data(), INPUT_SIZE, in.data());
    std::array<uint8_t, 256> out{};
    auto const ok = evmone::crypto::bls::g2_add(
        out.data(), out.data() + 128, in.data(), in.data() + 128, in.data() + 256, in.data() + 384);
    if (!ok)
    {
        return {false, {}};
    }
    return {true, bcos::bytes(out.begin(), out.end())};
}

std::pair<bool, bcos::bytes> executeBlsG2Msm(bcos::bytesConstRef input)
{
    constexpr size_t PAIR_SIZE = 288;
    if (input.empty() || input.size() % PAIR_SIZE != 0)
    {
        return {false, {}};
    }
    std::array<uint8_t, 256> out{};
    auto const ok =
        evmone::crypto::bls::g2_msm(out.data(), out.data() + 128, input.data(), input.size());
    if (!ok)
    {
        return {false, {}};
    }
    return {true, bcos::bytes(out.begin(), out.end())};
}

std::pair<bool, bcos::bytes> executeBlsPairingCheck(bcos::bytesConstRef input)
{
    constexpr size_t PAIR_SIZE = 384;
    if (input.empty() || input.size() % PAIR_SIZE != 0)
    {
        return {false, {}};
    }
    std::array<uint8_t, 32> out{};
    auto const ok = evmone::crypto::bls::pairing_check(out.data(), input.data(), input.size());
    if (!ok)
    {
        return {false, {}};
    }
    return {true, bcos::bytes(out.begin(), out.end())};
}

std::pair<bool, bcos::bytes> executeBlsMapFpToG1(bcos::bytesConstRef input)
{
    constexpr size_t INPUT_SIZE = 64;
    if (input.size() != INPUT_SIZE)
    {
        return {false, {}};
    }
    std::array<uint8_t, INPUT_SIZE> in{};
    std::copy_n(input.data(), INPUT_SIZE, in.data());
    std::array<uint8_t, 128> out{};
    auto const ok = evmone::crypto::bls::map_fp_to_g1(out.data(), out.data() + 64, in.data());
    if (!ok)
    {
        return {false, {}};
    }
    return {true, bcos::bytes(out.begin(), out.end())};
}

std::pair<bool, bcos::bytes> executeBlsMapFp2ToG2(bcos::bytesConstRef input)
{
    constexpr size_t INPUT_SIZE = 128;
    if (input.size() != INPUT_SIZE)
    {
        return {false, {}};
    }
    std::array<uint8_t, INPUT_SIZE> in{};
    std::copy_n(input.data(), INPUT_SIZE, in.data());
    std::array<uint8_t, 256> out{};
    auto const ok = evmone::crypto::bls::map_fp2_to_g2(out.data(), out.data() + 128, in.data());
    if (!ok)
    {
        return {false, {}};
    }
    return {true, bcos::bytes(out.begin(), out.end())};
}

int64_t precompileGasCost(uint16_t suffix, bcos::bytesConstRef input, evmc_revision revision)
{
    switch (suffix)
    {
    case 0x0001:
        return 3000;
    case 0x0002:
        return wordsCost(input.size(), 60, 12);
    case 0x0003:
        return wordsCost(input.size(), 600, 120);
    case 0x0004:
        return wordsCost(input.size(), 15, 3);
    case 0x0005:
        return safeBigintToI64(bcos::evm::calcModexpGas(input, revision));
    case 0x0006:
        return revision >= EVMC_ISTANBUL ? 150 : 500;
    case 0x0007:
        return revision >= EVMC_ISTANBUL ? 6000 : 40000;
    case 0x0008:
        return (revision >= EVMC_ISTANBUL ? 45000 : 100000) +
               static_cast<int64_t>(input.size() / 192) *
                   (revision >= EVMC_ISTANBUL ? 34000 : 80000);
    case 0x0009:
        return input.size() < 4 ?
                   0 :
                   static_cast<int64_t>(fromBigEndian<uint32_t>(input.getCroppedData(0, 4)));
    case 0x000a:
        return 50000;
    case 0x000b:
        return 375;
    case 0x000c:
        return 12000 * static_cast<int64_t>(input.size() / 160);
    case 0x000d:
        return 600;
    case 0x000e:
        return 22500 * static_cast<int64_t>(input.size() / 288);
    case 0x000f:
        return 37700 + 32600 * static_cast<int64_t>(input.size() / 384);
    case 0x0010:
        return 5500;
    case 0x0011:
        return 23800;
    default:
        return 0;
    }
}
}  // namespace

bool EthPrecompiles::isAddressInRange(const evmc_address& address) noexcept
{
    return toSuffix(address).has_value();
}

std::optional<EthPrecompileResult> EthPrecompiles::dispatch(
    const evmc_address& address, bcos::bytesConstRef input, int64_t msgGas, evmc_revision revision)
{
    auto const suffix = toSuffix(address);
    if (!suffix.has_value())
    {
        return std::nullopt;
    }

    EthPrecompileResult result;
    result.gasCost = precompileGasCost(*suffix, input, revision);
    if (msgGas < result.gasCost)
    {
        result.status = EVMC_OUT_OF_GAS;
        return result;
    }

    std::pair<bool, bcos::bytes> executed{false, {}};
    switch (*suffix)
    {
    case 0x0001:
        executed = executeEcrecover(input);
        break;
    case 0x0002:
    {
        bcos::bytes output(evmone::crypto::SHA256_HASH_SIZE, 0);
        evmone::crypto::sha256(reinterpret_cast<std::byte*>(output.data()),
            reinterpret_cast<const std::byte*>(input.data()), input.size());
        executed = {true, std::move(output)};
        break;
    }
    case 0x0003:
    {
        bcos::bytes output(32, 0);
        evmone::crypto::ripemd160(reinterpret_cast<std::byte*>(output.data() + 12),
            reinterpret_cast<const std::byte*>(input.data()), input.size());
        executed = {true, std::move(output)};
        break;
    }
    case 0x0004:
        executed = {true, input.toBytes()};
        break;
    case 0x0005:
        executed = executeModexp(input);
        break;
    case 0x0006:
        executed = executeBnAdd(input);
        break;
    case 0x0007:
        executed = executeBnMul(input);
        break;
    case 0x0008:
        executed = executeBnPairing(input);
        break;
    case 0x0009:
        executed = executeBlake2f(input);
        break;
    case 0x000a:
        executed = executePointEvaluation(input);
        break;
    case 0x000b:
        executed = executeBlsG1Add(input);
        break;
    case 0x000c:
        executed = executeBlsG1Msm(input);
        break;
    case 0x000d:
        executed = executeBlsG2Add(input);
        break;
    case 0x000e:
        executed = executeBlsG2Msm(input);
        break;
    case 0x000f:
        executed = executeBlsPairingCheck(input);
        break;
    case 0x0010:
        executed = executeBlsMapFpToG1(input);
        break;
    case 0x0011:
        executed = executeBlsMapFp2ToG2(input);
        break;
    default:
        return std::nullopt;
    }

    result.status = executed.first ? EVMC_SUCCESS : EVMC_PRECOMPILE_FAILURE;
    result.output = std::move(executed.second);
    return result;
}
}  // namespace bcos::evm::state
