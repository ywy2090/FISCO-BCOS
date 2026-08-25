#pragma once
// Task 9 Karst NUT fixture helpers — test-only orchestration (op-node/kona source-hash parity).
#include <bcos-evm/opstack/OpTransition.h>
#include <opstack-executor/OpBlockExecute.h>
#include <cstdint>
#include <evmc/evmc.hpp>
#include <span>
#include <string>
#include <string_view>

namespace opstack_test
{
// Pinned bundle: op-core/nuts/bundles/karst_nut_bundle.json (31 txs).
inline constexpr uint64_t KarstPinnedUpgradeGas = 55'370'657;

enum class KarstActivationSegment
{
    L1Attributes,
    UserDeposit,
    NutUpgrade,
};

[[nodiscard]] std::string karstNutQualifiedIntent(size_t index, std::string_view intent);

[[nodiscard]] evmc::bytes32 upgradeDepositSourceHash(std::string_view qualifiedIntent);

[[nodiscard]] bool isUpgradeDeposit(
    bcos::evm::opstack::DepositTx const& dep, std::string_view qualifiedIntent) noexcept;

[[nodiscard]] KarstActivationSegment classifyKarstActivationDeposit(
    bcos::evm::opstack::DepositTx const& dep,
    std::span<std::string_view const> nutQualifiedIntents);

void validateKarstActivationOrder(std::span<bcos::evm::opstack::OpBlockTx const> txs,
    std::span<std::string_view const> nutQualifiedIntents);
}  // namespace opstack_test
