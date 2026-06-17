#pragma once
#include "RevisionConfig.h"
#include <bcos-task/Task.h>
#include <evmc/evmc.h>
#include <concepts>
#include <cstdint>
#include <optional>

namespace bcos::evm_standard
{

// Forward declares: actual types resolved at instantiation site
template <typename P>
concept HostContextPolicy = requires(P& policy, const struct bcos::protocol::BlockHeader& header,
    bool web3Tx, const evmc_message& msg, int64_t blockNum, int64_t ctxId, int64_t seq,
    const u256& nonce, const struct bcos::crypto::Hash& hashImpl, int64_t blockTimestampMs,
    const evmc_address& addr, const evmc_address& beneficiary) {
    // 1. Revision configuration — non-static, Features captured at policy construction
    {
        policy.computeRevisionConfig(header)
    } -> std::same_as<RevisionConfig>;
    // 2. Address/message derivation (resolves CREATE address)
    {
        P::deriveMessage(web3Tx, msg, blockNum, ctxId, seq, nonce, hashImpl)
    } -> std::same_as<evmc_message>;
    // 3. Timestamp conversion (ms to EVM seconds)
    {
        policy.convertTimestamp(blockTimestampMs)
    } -> std::same_as<int64_t>;
    // 4. Selfdestruct behavior
    {
        policy.selfdestruct(addr, beneficiary)
    } -> std::same_as<bool>;
    // 5. DELEGATECALL to precompile
    {
        policy.allowDelegateCallToPrecompile()
    } -> std::same_as<bool>;
};

// Optional trait: auth check
template <typename P, typename Storage, typename... Args>
concept HasAuthCheck = requires(P& p, Storage& s, Args... args) {
    {
        p.checkAuth(s, args...)
    } -> std::same_as<task::Task<std::optional<struct bcos::evm::EVMCResult>>>;
};

}  // namespace bcos::evm_standard
