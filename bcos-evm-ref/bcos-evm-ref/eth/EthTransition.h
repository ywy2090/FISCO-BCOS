#pragma once

#include <evmc/evmc.hpp>
#include <optional>
#include <span>
#include <system_error>
#include <test/state/state.hpp>
#include <variant>

namespace bcos::evmref::eth
{
using Result = std::variant<evmone::state::TransactionReceipt, std::error_code>;

/// validate -> transition（spec §4.2）。零 fee 逻辑：全部复用 evmone。
/// 不写回状态；调用方用 applyStateDiff(receipt.state_diff) 落账。
/// blobGasLeft 与 BlockInfo.blob_base_fee 由调用方按 BlobParams 预计算（spec §2）。
[[nodiscard]] Result runTransaction(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    const evmone::state::Transaction& tx, evmc_revision rev, evmc::VM& vm, int64_t blockGasLeft,
    int64_t blobGasLeft);

/// 块级收尾（withdrawals / ommer 奖励）。返回待写回的 diff。
[[nodiscard]] evmone::state::StateDiff runBlockFinalize(const evmone::state::StateView& view,
    evmc_revision rev, const evmc::address& coinbase, std::optional<uint64_t> blockReward,
    std::span<const evmone::state::Ommer> ommers,
    std::span<const evmone::state::Withdrawal> withdrawals);
}  // namespace bcos::evmref::eth
