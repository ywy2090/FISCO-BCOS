#pragma once

#include "bcos-evm/eth/EVMCResult.h"
#include <optional>

namespace bcos::evm
{
struct EthReferenceRequest;
namespace state
{
class State;
}

std::optional<EVMCResult> ethTxPrecheck(EthReferenceRequest const& input, state::State& state);
}  // namespace bcos::evm
