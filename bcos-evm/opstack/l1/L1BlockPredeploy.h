#pragma once

// Native dispatch for the OP Stack L1Block predeploy (0x4200…0015).
// OpStackChainCallTargetAdapter intercepts CALLs to this address (no EVM bytecode).

#include "bcos-evm/eth/state/State.hpp"
#include <evmc/evmc.h>
#include <optional>

namespace bcos::evm
{
class L1BlockPredeploy
{
public:
    static std::optional<evmc_result> dispatch(state::State& state, evmc_message const& msg);
    // Read-only getters; used by L1Block and proxied from GasPriceOracle.
    static std::optional<evmc_result> dispatchGetter(
        state::State& state, uint32_t selector, int64_t gas);
};
}  // namespace bcos::evm
