#pragma once

#include "bcos-evm/eth/state/State.hpp"
#include <evmc/evmc.h>
#include <optional>

namespace bcos::evm
{
class L1BlockPredeploy
{
public:
    static std::optional<evmc_result> dispatch(state::State& state, evmc_message const& msg);
    static std::optional<evmc_result> dispatchGetter(
        state::State& state, uint32_t selector, int64_t gas);
};
}  // namespace bcos::evm
