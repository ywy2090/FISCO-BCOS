#pragma once

// Native dispatch for the OP Stack GasPriceOracle predeploy (0x4200…000f).
// Exposes L2 base fee, L1 fee estimates, and proxied L1Block getters.

#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/opstack/policy/OpStackForkSchedule.h"
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <optional>

namespace bcos::evm
{
class GasPriceOraclePredeploy
{
public:
    static std::optional<evmc_result> dispatch(state::State& state, evmc_message const& msg,
        bcos::u256 l2BaseFee,
        OpStackForkSchedule const& forkSchedule = makeIsthmusPlusForkSchedule(),
        uint64_t blockTime = 0);
};
}  // namespace bcos::evm
