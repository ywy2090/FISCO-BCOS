/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief Native dispatch for the OP Stack L1Block predeploy (0x4200…0015).
 * @file L1BlockPredeploy.h
 *
 * L1Block holds L1-derived attributes (base fee, blob fee, scalars, batcher hash, …)
 * that the sequencer injects via the first deposit tx of each L2 block. On-chain there
 * is no bytecode at this address; OpStackChainCallTargetAdapter intercepts CALLs and
 * routes them here. Storage layout and ABI match op-geth contracts-bedrock L1Block.sol.
 */

#pragma once

#include <evmc/evmc.h>
#include <optional>

namespace bcos::evm::state
{
class State;
}

namespace bcos::evm
{
/// Native handler for OP_L1_BLOCK_PREDEPLOY (0x4200…0015).
class L1BlockPredeploy
{
public:
    /// Route a CALL by 4-byte selector: getters, setters, or isFeatureEnabled.
    /// Returns std::nullopt only from dispatchGetter for unknown selectors; dispatch
    /// itself always returns a result (revert on unknown / malformed input).
    static std::optional<evmc_result> dispatch(state::State& state, evmc_message const& msg);

    /// Read-only getters shared with GasPriceOracle proxy paths.
    static std::optional<evmc_result> dispatchGetter(
        state::State& state, uint32_t selector, int64_t gas);
};
}  // namespace bcos::evm
