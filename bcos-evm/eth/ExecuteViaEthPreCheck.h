#pragma once

#include "bcos-evm/eth/EVMCResult.h"
#include <optional>

namespace bcos::evm
{
struct ExecuteViaEthInput;
namespace state
{
class State;
}

std::optional<EVMCResult> ethExecuteViaEthPreCheck(
    ExecuteViaEthInput const& input, state::State& state);
}  // namespace bcos::evm
