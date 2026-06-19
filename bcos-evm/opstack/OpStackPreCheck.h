#pragma once

#include "bcos-evm/eth/EVMCResult.h"
#include <optional>

namespace bcos::evm
{
struct OpStackExecuteViaHostInput;
namespace state
{
class State;
}

bool isDepositTx(OpStackExecuteViaHostInput const& input) noexcept;

std::optional<EVMCResult> opStackPreCheck(
    OpStackExecuteViaHostInput const& input, state::State& state);
}  // namespace bcos::evm
