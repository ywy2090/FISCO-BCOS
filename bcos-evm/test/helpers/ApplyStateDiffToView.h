#pragma once

#include "bcos-evm/eth/state/StateDiff.hpp"
#include "state/InMemoryStateView.h"

namespace bcos::evm::test
{
inline void applyStateDiffToView(
    state::StateDiff const& stateDiff, state::test::InMemoryStateView& stateView)
{
    for (auto const& [address, account] : stateDiff.accounts)
    {
        stateView.insert_account(address, account);
    }
}
}  // namespace bcos::evm::test
