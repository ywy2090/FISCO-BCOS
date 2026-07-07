/*
 *  Copyright (C) 2026 FISCO BCOS.
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
 *
 * Native implementations delegate to evmone_precompiles where possible.
 * Table-driven dispatch: address suffix → {execute, gas, optional reject}.
 */

#include "bcos-evm/eth/precompiled/EthPrecompiles.h"
#include "bcos-evm/eth/precompiled/Eip2537Gas.h"
#include "bcos-evm/eth/precompiled/ModexpGas.h"
#include "bcos-evm/eth/precompiled/PrecompileGas.h"
#include "bcos-evm/eth/precompiled/PrecompiledAddress.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/DataConvertUtility.h"
#include "eth/core/RevisionConfig.h"
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
#include <evmone_precompiles/secp256r1.hpp>
#include <evmone_precompiles/sha256.hpp>
#include <intx/intx.hpp>
#include <limits>
#include <range/v3/utility/get.hpp>
#include <span>
#include <utility>
#include <vector>

namespace bcos::evm::precompiled
{
namespace
{
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
        return {false, {}};
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
        return {false, {}};
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
        return {false, {}};
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
            return {false, {}};
        }
        const ExtPoint g2{{load<intx::uint256>(ptr + 96), load<intx::uint256>(ptr + 64)},
            {load<intx::uint256>(ptr + 160), load<intx::uint256>(ptr + 128)}};
        pairs.emplace_back(Point{g1->x.value(), g1->y.value()}, g2);
    }

    bcos::bytes output(32, 0);
    auto const result = pairing_check(pairs);
    if (!result.has_value())
    {
        return {false, {}};
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

std::pair<bool, bcos::bytes> executeP256Verify(bcos::bytesConstRef input)
{
    static constexpr size_t INPUT_SIZE = 160;
    if (input.size() != INPUT_SIZE)
    {
        return {true, {}};
    }
    auto const* d = input.data();
    ethash::hash256 h{};
    std::memcpy(h.bytes, d, 32);
    auto const r = intx::be::unsafe::load<intx::uint256>(d + 32);
    auto const s = intx::be::unsafe::load<intx::uint256>(d + 64);
    auto const qx = intx::be::unsafe::load<intx::uint256>(d + 96);
    auto const qy = intx::be::unsafe::load<intx::uint256>(d + 128);
    if (!evmmax::secp256r1::verify(h, r, s, qx, qy))
    {
        return {true, {}};
    }
    bcos::bytes output(32, 0);
    output[31] = 1;
    return {true, std::move(output)};
}

std::pair<bool, bcos::bytes> executeSha256(bcos::bytesConstRef input)
{
    bcos::bytes output(evmone::crypto::SHA256_HASH_SIZE, 0);
    evmone::crypto::sha256(reinterpret_cast<std::byte*>(output.data()),
        reinterpret_cast<const std::byte*>(input.data()), input.size());
    return {true, std::move(output)};
}

std::pair<bool, bcos::bytes> executeRipemd160(bcos::bytesConstRef input)
{
    bcos::bytes output(32, 0);
    evmone::crypto::ripemd160(reinterpret_cast<std::byte*>(output.data() + 12),
        reinterpret_cast<const std::byte*>(input.data()), input.size());
    return {true, std::move(output)};
}

std::pair<bool, bcos::bytes> executeIdentity(bcos::bytesConstRef input)
{
    return {true, input.toBytes()};
}

int64_t gasEcrecover(bcos::bytesConstRef /*input*/, evmc_revision /*revision*/) noexcept
{
    return gas::ECRECOVER;
}

int64_t gasSha256(bcos::bytesConstRef input, evmc_revision /*revision*/) noexcept
{
    return wordsCost(input.size(), gas::SHA256_WORD, gas::SHA256_BASE);
}

int64_t gasRipemd160(bcos::bytesConstRef input, evmc_revision /*revision*/) noexcept
{
    return wordsCost(input.size(), gas::RIPEMD160_WORD, gas::RIPEMD160_BASE);
}

int64_t gasIdentity(bcos::bytesConstRef input, evmc_revision /*revision*/) noexcept
{
    return wordsCost(input.size(), gas::IDENTITY_WORD, gas::IDENTITY_BASE);
}

int64_t gasModexp(bcos::bytesConstRef input, evmc_revision revision) noexcept
{
    return safeBigintToI64(bcos::evm::calcModexpGas(input, revision));
}

int64_t gasBnAdd(bcos::bytesConstRef /*input*/, evmc_revision revision) noexcept
{
    return revision >= EVMC_ISTANBUL ? gas::BN_ADD_ISTANBUL : gas::BN_ADD_LEGACY;
}

int64_t gasBnMul(bcos::bytesConstRef /*input*/, evmc_revision revision) noexcept
{
    return revision >= EVMC_ISTANBUL ? gas::BN_MUL_ISTANBUL : gas::BN_MUL_LEGACY;
}

int64_t gasBnPairing(bcos::bytesConstRef input, evmc_revision revision) noexcept
{
    return (revision >= EVMC_ISTANBUL ? gas::BN_PAIRING_BASE_ISTANBUL :
                                        gas::BN_PAIRING_BASE_LEGACY) +
           static_cast<int64_t>(input.size() / gas::BN_PAIRING_INPUT_BYTES) *
               (revision >= EVMC_ISTANBUL ? gas::BN_PAIRING_PAIR_ISTANBUL :
                                            gas::BN_PAIRING_PAIR_LEGACY);
}

int64_t gasBlake2f(bcos::bytesConstRef input, evmc_revision /*revision*/) noexcept
{
    return input.size() < 4 ?
               0 :
               static_cast<int64_t>(fromBigEndian<uint32_t>(input.getCroppedData(0, 4)));
}

int64_t gasPointEvaluation(bcos::bytesConstRef /*input*/, evmc_revision /*revision*/) noexcept
{
    return gas::POINT_EVALUATION;
}

int64_t gasBlsG1Add(bcos::bytesConstRef /*input*/, evmc_revision /*revision*/) noexcept
{
    return BLS_G1_ADD_GAS;
}

int64_t gasBlsG1Msm(bcos::bytesConstRef input, evmc_revision /*revision*/) noexcept
{
    return precompiled::blsG1MsmGas(input.size() / BLS_G1_INPUT_BYTES);
}

int64_t gasBlsG2Add(bcos::bytesConstRef /*input*/, evmc_revision /*revision*/) noexcept
{
    return BLS_G2_ADD_GAS;
}

int64_t gasBlsG2Msm(bcos::bytesConstRef input, evmc_revision /*revision*/) noexcept
{
    return precompiled::blsG2MsmGas(input.size() / BLS_G2_INPUT_BYTES);
}

int64_t gasBlsPairingCheck(bcos::bytesConstRef input, evmc_revision /*revision*/) noexcept
{
    return BLS_PAIRING_CHECK_BASE_GAS +
           BLS_PAIRING_CHECK_PAIR_GAS *
               static_cast<int64_t>(input.size() / BLS_PAIRING_INPUT_BYTES);
}

int64_t gasBlsMapFpToG1(bcos::bytesConstRef /*input*/, evmc_revision /*revision*/) noexcept
{
    return BLS_MAP_FP_TO_G1_GAS;
}

int64_t gasBlsMapFp2ToG2(bcos::bytesConstRef /*input*/, evmc_revision /*revision*/) noexcept
{
    return BLS_MAP_FP2_TO_G2_GAS;
}

int64_t gasP256Verify(bcos::bytesConstRef /*input*/, evmc_revision /*revision*/) noexcept
{
    return gas::P256_VERIFY;
}

bool rejectModexp7823(evmc_address const& address, bcos::bytesConstRef input,
    bcos::evm::RevisionConfig const& cfg, evmc_revision revision) noexcept
{
    return bcos::evm::shouldRejectModexpEip7823(address, input, cfg, revision);
}

using ExecuteFn = std::pair<bool, bcos::bytes> (*)(bcos::bytesConstRef);
using GasFn = int64_t (*)(bcos::bytesConstRef, evmc_revision);
/// Optional policy gate (e.g. modexp EIP-7823 field limits); failure → PRECOMPILE_FAILURE.
using RejectFn = bool (*)(
    evmc_address const&, bcos::bytesConstRef, bcos::evm::RevisionConfig const&, evmc_revision);

/// One row per Ethereum builtin precompile; keyed by address suffix (bytes[18:19]).
struct PrecompileEntry
{
    uint16_t suffix;
    ExecuteFn execute;
    GasFn gas;
    RejectFn reject;
};

/// Canonical builtin set: 0x0001–0x0011 (classic + Cancun + Prague) and 0x0100 (Osaka p256).
constexpr PrecompileEntry kPrecompileTable[] = {
    {0x0001, executeEcrecover, gasEcrecover, nullptr},
    {0x0002, executeSha256, gasSha256, nullptr},
    {0x0003, executeRipemd160, gasRipemd160, nullptr},
    {0x0004, executeIdentity, gasIdentity, nullptr},
    {0x0005, executeModexp, gasModexp, rejectModexp7823},
    {0x0006, executeBnAdd, gasBnAdd, nullptr},
    {0x0007, executeBnMul, gasBnMul, nullptr},
    {0x0008, executeBnPairing, gasBnPairing, nullptr},
    {0x0009, executeBlake2f, gasBlake2f, nullptr},
    {0x000a, executePointEvaluation, gasPointEvaluation, nullptr},
    {0x000b, executeBlsG1Add, gasBlsG1Add, nullptr},
    {0x000c, executeBlsG1Msm, gasBlsG1Msm, nullptr},
    {0x000d, executeBlsG2Add, gasBlsG2Add, nullptr},
    {0x000e, executeBlsG2Msm, gasBlsG2Msm, nullptr},
    {0x000f, executeBlsPairingCheck, gasBlsPairingCheck, nullptr},
    {0x0010, executeBlsMapFpToG1, gasBlsMapFpToG1, nullptr},
    {0x0011, executeBlsMapFp2ToG2, gasBlsMapFp2ToG2, nullptr},
    {static_cast<uint16_t>(P256VERIFY_PRECOMPILE_INDEX), executeP256Verify, gasP256Verify, nullptr},
};

PrecompileEntry const* lookupPrecompileEntry(uint16_t suffix) noexcept
{
    for (auto const& entry : kPrecompileTable)
    {
        if (entry.suffix == suffix)
        {
            return &entry;
        }
    }
    return nullptr;
}
}  // namespace

bool EthPrecompiles::isAddressInRange(const evmc_address& address) noexcept
{
    return bcos::evm::precompileSuffix(address).has_value();
}

// Core dispatch: suffix lookup → gas check → optional reject → execute.
std::optional<EthPrecompileResult> EthPrecompiles::dispatch(const evmc_address& address,
    bcos::bytesConstRef input, int64_t msgGas, evmc_revision revision,
    bcos::evm::RevisionConfig const& cfg)
{
    auto const suffixKey = bcos::evm::precompileSuffix(address);
    if (!suffixKey.has_value())
    {
        return std::nullopt;
    }

    auto const* entry = lookupPrecompileEntry(*suffixKey);
    if (entry == nullptr)
    {
        return std::nullopt;
    }

    EthPrecompileResult result;
    result.gasCost = entry->gas(input, revision);
    if (msgGas < result.gasCost)
    {
        result.status = EVMC_OUT_OF_GAS;
        return result;
    }

    std::pair<bool, bcos::bytes> executed{false, {}};
    if (entry->reject != nullptr && entry->reject(address, input, cfg, revision))
    {
        executed = {false, {}};
    }
    else
    {
        executed = entry->execute(input);
    }

    result.status = executed.first ? EVMC_SUCCESS : EVMC_PRECOMPILE_FAILURE;
    result.output = std::move(executed.second);
    return result;
}

// Adapter for PrecompileRouter: dispatch + map to evmc::Result (gas_left, owned output).
std::optional<evmc::Result> EthPrecompiles::tryDispatchInCall(const evmc_address& address,
    const evmc_message& msg, evmc_revision revision, bcos::evm::RevisionConfig const& cfg)
{
    bcos::bytesConstRef input(msg.input_data, msg.input_size);
    auto dispatched = dispatch(address, input, msg.gas, revision, cfg);
    if (!dispatched.has_value())
    {
        return std::nullopt;
    }

    evmc_result result{};
    result.status_code = dispatched->status;
    result.gas_refund = 0;
    result.create_address = {};

    // evm.Call exhausts gas on non-revert failure; evmone call_precompile sets gas_left = 0.
    if (dispatched->status == EVMC_SUCCESS)
    {
        result.gas_left = std::max<int64_t>(0, msg.gas - dispatched->gasCost);
    }
    else
    {
        result.gas_left = 0;
    }

    // geth: failed precompile runs return no output; evmone still memcpy's host output on
    // CALL failure when output_size > 0 (instructions_calls.cpp), so omit output unless SUCCESS.
    if (dispatched->status == EVMC_SUCCESS && !dispatched->output.empty())
    {
        auto* data = new uint8_t[dispatched->output.size()];
        std::copy(dispatched->output.begin(), dispatched->output.end(), data);
        result.output_data = data;
        result.output_size = dispatched->output.size();
        result.release = [](const evmc_result* value) { delete[] value->output_data; };
    }
    return evmc::Result(result);
}
}  // namespace bcos::evm::precompiled
