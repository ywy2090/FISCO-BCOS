#pragma once

#include "bcos-evm/eth/state/State.hpp"
#include "bcos-evm/opstack/OpStackForkSchedule.h"
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
