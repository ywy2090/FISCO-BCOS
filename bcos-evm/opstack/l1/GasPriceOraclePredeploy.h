/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Native dispatch for the OP Stack GasPriceOracle predeploy (0x4200…000f).
 * @file GasPriceOraclePredeploy.h
 *
 * RPC-facing fee oracle: L2 base fee (injected by adapter), Fjord L1 cost estimation,
 * Isthmus/Jovian operator fee, fork flags, and a subset of L1Block getters proxied
 * in-process (no nested CALL). Matches op-geth GasPriceOracle.sol surface.
 */

#pragma once

#include "bcos-evm/opstack/policy/OpStackForkSchedule.h"
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <optional>

namespace bcos::evm::state
{
class State;
}

namespace bcos::evm
{
/// Native handler for OP_GAS_PRICE_ORACLE_PREDEPLOY (0x4200…000f).
class GasPriceOraclePredeploy
{
public:
    /// @param l2BaseFee  Current block L2 base fee (wei), from execution context.
    /// @param forkSchedule  Used for isJovian and operator-fee formula selection.
    /// @param blockTime  L2 block timestamp for time-gated fork checks.
    static std::optional<evmc_result> dispatch(state::State& state, evmc_message const& msg,
        bcos::u256 l2BaseFee,
        OpStackForkSchedule const& forkSchedule = makeIsthmusPlusForkSchedule(),
        uint64_t blockTime = 0);
};
}  // namespace bcos::evm
