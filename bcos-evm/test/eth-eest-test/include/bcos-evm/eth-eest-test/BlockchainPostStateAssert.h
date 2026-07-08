#pragma once

#include "bcos-evm/eth-eest-test/TestStateView.h"
#include "bcos-evm/eth/state/Account.hpp"
#include <bcos-utilities/Common.h>
#include <evmc/evmc.h>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bcos::evm::reference_tests
{

/// One account's expected post-state. Presence flags mirror EEST partial semantics:
/// only fields whose JSON key was authored are asserted.
struct ExpectedPostAccount
{
    enum class Kind
    {
        Present,  // account must exist; check flagged fields
        Absent    // account must NOT exist in actual
    };

    Kind kind{Kind::Present};
    bool hasNonce{false};
    bool hasBalance{false};
    bool hasCode{false};
    bool hasStorage{false};  // `storage` key present (even if empty object)
    uint64_t nonce{0};
    bcos::u256 balance{0};
    bcos::bytes code;
    /// Listed slots in fixture order. Empty + hasStorage ⇒ "all actual slots must be zero".
    std::vector<std::pair<evmc_bytes32, evmc_bytes32>> storage;
};

/// Blockchain fixture post expectation: hash path OR partial account map.
/// Invariant: if `accounts` is non-empty it wins and `hash` is ignored (loader precedence).
struct PostStateExpectation
{
    std::optional<evmc_bytes32> hash;  // postStateHash
    std::vector<std::pair<evmc_address, ExpectedPostAccount>> accounts;
};

struct PostStateFieldDiff
{
    evmc_address address{};
    std::string field;  // "nonce"|"balance"|"code"|"storage"|"existence"|"postStateHash"
    std::optional<evmc_bytes32> slot;
    std::string got;
    std::string want;
    std::string message;  // human-readable one-liner
};

struct PostStateAssertReport
{
    bool passed{true};
    std::vector<PostStateFieldDiff> diffs;
    std::string summary;  // first diff message; empty on pass
};

struct AssertOptions
{
    /// Derived by caller from the canonical-tip revision `>= EVMC_SPURIOUS_DRAGON`;
    /// affects the hash path only.
    bool eip158ClearEmpty{true};
};

/// Assert `actual` canonical-tip state against `expected` per spec §4.4.
PostStateAssertReport assertPostState(TestStateView const& actual,
    PostStateExpectation const& expected, AssertOptions const& opts = {});

}  // namespace bcos::evm::reference_tests
