/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief Shared fixtures for evmone Phase 2 forward-compatibility (Compat) tests.
 *  @file CompatTestFixture.h
 */
#pragma once

#include "bcos-framework/ledger/Features.h"
#include "bcos-utilities/Common.h"
#include "vm/EvmPrecompiledAddress.h"
#include <string_view>

namespace bcos::test::compat
{

/// Governance profiles for T32 forward-compatibility matrix (see
/// docs/superpowers/plans/2026-05-19-evmone-phase2-ut-compat-plan.md).
struct CompatFeatureProfile
{
    static ledger::Features legacyLondon()
    {
        ledger::Features f;
        return f;
    }

    static ledger::Features cancunOnly()
    {
        ledger::Features f;
        f.set(ledger::Features::Flag::feature_evm_cancun);
        return f;
    }

    static ledger::Features cancunEip2929()
    {
        ledger::Features f;
        f.set(ledger::Features::Flag::feature_evm_cancun);
        f.set(ledger::Features::Flag::feature_evm_eip2929);
        return f;
    }

    static ledger::Features shanghaiEip2929()
    {
        ledger::Features f;
        f.set(ledger::Features::Flag::feature_evm_eip2929);
        return f;
    }

    static ledger::Features pragueEnabled()
    {
        ledger::Features f;
        f.set(ledger::Features::Flag::feature_evm_cancun);
        f.set(ledger::Features::Flag::feature_evm_prague);
        f.set(ledger::Features::Flag::feature_evm_eip2929);
        return f;
    }

    static ledger::Features osakaEnabled()
    {
        auto f = pragueEnabled();
        f.set(ledger::Features::Flag::feature_evm_osaka);
        return f;
    }
};

// Address constants: vm/EvmPrecompiledAddress.h

namespace compat_addr
{
using bcos::executor::BLS_G1ADD_PRECOMPILE_ADDRESS;
using bcos::executor::BLS_G1MSM_PRECOMPILE_ADDRESS;
using bcos::executor::BLS_G2ADD_PRECOMPILE_ADDRESS;
using bcos::executor::BLS_G2MSM_PRECOMPILE_ADDRESS;
using bcos::executor::BLS_MAP_FP2_TO_G2_PRECOMPILE_ADDRESS;
using bcos::executor::BLS_MAP_FP_TO_G1_PRECOMPILE_ADDRESS;
using bcos::executor::BLS_PAIRING_CHECK_PRECOMPILE_ADDRESS;
using bcos::executor::MODEXP_PRECOMPILE_ADDRESS;
using bcos::executor::P256VERIFY_PRECOMPILE_ADDRESS;
using bcos::executor::POINT_EVALUATION_PRECOMPILE_ADDRESS;

inline constexpr std::string_view BLS_G1ADD = BLS_G1ADD_PRECOMPILE_ADDRESS;
inline constexpr std::string_view BLS_G1MSM = BLS_G1MSM_PRECOMPILE_ADDRESS;
inline constexpr std::string_view BLS_G2ADD = BLS_G2ADD_PRECOMPILE_ADDRESS;
inline constexpr std::string_view BLS_G2MSM = BLS_G2MSM_PRECOMPILE_ADDRESS;
inline constexpr std::string_view BLS_PAIRING_CHECK = BLS_PAIRING_CHECK_PRECOMPILE_ADDRESS;
inline constexpr std::string_view BLS_MAP_FP_TO_G1 = BLS_MAP_FP_TO_G1_PRECOMPILE_ADDRESS;
inline constexpr std::string_view BLS_MAP_FP2_TO_G2 = BLS_MAP_FP2_TO_G2_PRECOMPILE_ADDRESS;
inline constexpr std::string_view POINT_EVALUATION = POINT_EVALUATION_PRECOMPILE_ADDRESS;
inline constexpr std::string_view P256VERIFY = P256VERIFY_PRECOMPILE_ADDRESS;
inline constexpr std::string_view MODEXP = MODEXP_PRECOMPILE_ADDRESS;
}  // namespace compat_addr

/// EIP-198 modexp input (shared by FC-M / FC-P; unity build merges compat/*.cpp).
inline bytes compatMakeModexpInput(bytes base, bytes exp, bytes mod)
{
    bytes input;
    input.resize(input.size() + 32, 0);
    input.back() = static_cast<uint8_t>(base.size());
    input.resize(input.size() + 32, 0);
    input.back() = static_cast<uint8_t>(exp.size());
    input.resize(input.size() + 32, 0);
    input.back() = static_cast<uint8_t>(mod.size());
    input.insert(input.end(), base.begin(), base.end());
    input.insert(input.end(), exp.begin(), exp.end());
    input.insert(input.end(), mod.begin(), mod.end());
    return input;
}

}  // namespace bcos::test::compat
