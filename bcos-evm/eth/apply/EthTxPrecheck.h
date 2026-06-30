#pragma once

#include "bcos-evm/eth/EVMCResult.h"
#include <optional>

namespace bcos::evm
{
struct EthMessageRequest;
namespace state
{
class State;
}

std::optional<EVMCResult> ethTxPrecheck(EthMessageRequest const& input, state::State& state);
}  // namespace bcos::evm
