#pragma once

#include "bcos-evm/eth/EVMCResult.h"
#include <optional>

namespace bcos::evm
{
struct OpStackExecutionRequest;
namespace state
{
class State;
}

bool isDepositTx(OpStackExecutionRequest const& input) noexcept;

std::optional<EVMCResult> opStackTxPrecheck(
    OpStackExecutionRequest const& input, state::State& state);
}  // namespace bcos::evm
