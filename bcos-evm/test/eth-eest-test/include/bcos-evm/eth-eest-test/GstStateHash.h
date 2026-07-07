#pragma once

#include "bcos-evm/eth-eest-test/ReceiptForRoot.h"
#include "bcos-evm/eth/state/Account.hpp"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/StateDiff.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace bcos::evm::reference_tests
{

struct Withdrawal;

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
evmc_bytes32 computeTxRoot(std::span<const bcos::bytes> signedTxRlps);
evmc_bytes32 computeReceiptsRoot(std::span<const ReceiptForRoot> receipts);
evmc_bytes32 computeWithdrawalRoot(std::span<const Withdrawal> withdrawals);

bcos::bytes rlpEncodeRaw(bcos::bytes const& input);
bcos::bytes rlpEncodeUint64(uint64_t value);
bcos::bytes rlpEncodeU256(bcos::u256 value);
bcos::bytes rlpEncodeList(std::vector<bcos::bytes> const& items);

}  // namespace bcos::evm::reference_tests
