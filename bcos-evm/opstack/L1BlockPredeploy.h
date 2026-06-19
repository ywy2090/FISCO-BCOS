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
};
}  // namespace bcos::evm
