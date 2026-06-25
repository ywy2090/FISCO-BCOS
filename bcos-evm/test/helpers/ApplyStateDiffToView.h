#pragma once

#include "bcos-evm/eth/state/StateDiff.hpp"
#include "helpers/InMemoryEvmStateReader.h"

namespace bcos::evm::test
{
inline void applyStateDiffToView(
    state::StateDiff const& stateDiff, state::test::InMemoryEvmStateReader& stateView)
{
    for (auto const& [address, account] : stateDiff.accounts)
    {
        stateView.insert_account(address, account);
    }
}
}  // namespace bcos::evm::test
