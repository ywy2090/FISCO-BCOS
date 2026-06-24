#pragma once

#include "bcos-evm/eth/EthReferenceBridge.h"
#include "bcos-evm/eth/EthTxPrecheck.h"

namespace bcos::evm
{
[[deprecated("use ethTxPrecheck")]] inline std::optional<EVMCResult> ethTxPrecheck(
    EthReferenceRequest const& input, state::State& state)
{
    return ethTxPrecheck(input, state);
}
}  // namespace bcos::evm
