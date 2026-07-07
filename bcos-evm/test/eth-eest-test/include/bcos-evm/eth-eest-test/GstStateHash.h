#pragma once

#include "bcos-evm/eth/state/Account.hpp"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/StateDiff.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include <evmc/evmc.h>
#include <utility>
#include <vector>

namespace bcos::evm::reference_tests
{

struct GstPostStateView
{
    std::vector<std::pair<evmc_address, state::Account>> accounts;
    /// When true (post-EIP-158), empty accounts are omitted from the state trie root.
    bool eip158ClearEmpty = true;
};

GstPostStateView buildPostStateView(
    std::vector<std::pair<evmc_address, state::Account>> const& preState,
    state::StateDiff const& stateDiff, bool applyDiff, evmc_address const& coinbase, bool eip158);

evmc_bytes32 computeStateRoot(GstPostStateView const& postState);
evmc_bytes32 computeLogsHash(std::vector<state::LogEntry> const& logs);

}  // namespace bcos::evm::reference_tests
