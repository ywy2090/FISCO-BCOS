/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @brief M-T op-geth differential gate: replays opt8n-generated t8n vectors through
 *        applyOpStackMessage and diffs postState/receipts against the vector's expected values.
 * @file T8nVectorReplayTest.cpp
 *
 * Vector format: `bcos-evm/docs/superpowers/plans/2026-07-09-mt-t8n-gate-opstack.md` (v2,
 * op-test-vectors-compatible profile). Vectors are opt8n output (`t8n/generator/main.go`),
 * committed under `t8n/vectors` as `.json` files; `.in.json` files are generator inputs, not
 * vectors, and are skipped.
 *
 * Divergence discipline (predeclared in the plan before any vector was generated): a mismatch is
 * never silently skipped or expectation-adjusted. Every check funnels through checkXxx() below,
 * which emits `DIVERGE <vector_id> <field> want=<..> got=<..>` on failure via
 * BOOST_CHECK_MESSAGE, so ctest output is grep-able for `DIVERGE` regardless of which field
 * diverged.
 */

#define BOOST_TEST_MODULE T8nVectorReplayTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/eth/core/RevisionConfig.h"
#include "bcos-evm/eth/eip/Eip2718TypedTx.h"
#include "bcos-evm/eth/state/Account.hpp"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/StateDiff.hpp"
#include "bcos-evm/opstack/apply/ApplyOpStackMessage.h"
#include "bcos-evm/opstack/fee/RollupCost.h"
#include "bcos-evm/opstack/policy/OpStackForkSchedule.h"
#include "bcos-evm/opstack/policy/OpStackIsthmusRevision.h"
#include "bcos-evm/opstack/types/OpStackDepositTx.h"
#include "helpers/InMemoryStateView.h"
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <evmc/evmc.h>
#include <evmone/evmone.h>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/test/included/unit_test.hpp>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifndef T8N_VECTORS_DIR
#define T8N_VECTORS_DIR "bcos-evm/test/opstack/t8n/vectors"
#endif

namespace bcos::evm::test
{
namespace
{
namespace pt = boost::property_tree;

class FakeHash final : public crypto::Hash
{
public:
    crypto::HashType hash(bytesConstRef /*unused*/) const override { return crypto::HashType{}; }
    bcos::crypto::hasher::AnyHasher hasher() const override { return {}; }
};

// ── JSON scalar parsing (op-test-vectors quantities are always "0x..." hex) ────────────────────
// Mirrors bcos-evm/test/eth-eest-test/src/OpStackEestAdapter.cpp's parse* helpers; kept local
// (no shared header exists for this exact combination) rather than factored out, matching the
// project convention of per-test anonymous-namespace helpers (see DepositMintTest.cpp).

bcos::u256 parseQuantity(std::string const& value)
{
    if (value.empty())
    {
        return 0;
    }
    return bcos::fromBigQuantity(value);
}

uint64_t parseUint64(std::string const& value)
{
    if (value.empty())
    {
        return 0;
    }
    return static_cast<uint64_t>(parseQuantity(value));
}

bcos::bytes parseHexBytes(std::string const& value)
{
    if (value.empty() || value == "0x")
    {
        return {};
    }
    return bcos::fromHex(value);
}

evmc_bytes32 parseBytes32Str(std::string const& value)
{
    auto const raw = parseHexBytes(value);
    evmc_bytes32 out{};
    BOOST_REQUIRE_MESSAGE(raw.size() <= sizeof(out.bytes), "bytes32 literal too long: " << value);
    if (!raw.empty())
    {
        std::memcpy(out.bytes + sizeof(out.bytes) - raw.size(), raw.data(), raw.size());
    }
    return out;
}

bcos::h256 parseH256(std::string const& value)
{
    auto raw = parseHexBytes(value);
    BOOST_REQUIRE_MESSAGE(raw.size() == bcos::h256::SIZE, "expected 32-byte hash: " << value);
    return bcos::h256(bcos::ref(raw));
}

bool parseBool(pt::ptree const& node, std::string const& key, bool defaultValue)
{
    auto const value = node.get_optional<std::string>(key);
    if (!value.has_value())
    {
        return defaultValue;
    }
    return *value == "true" || *value == "1";
}

std::optional<evmc_address> parseOptionalAddress(pt::ptree const& node, std::string const& key)
{
    auto const value = node.get_optional<std::string>(key);
    if (!value.has_value() || value->empty() || *value == "null")
    {
        return std::nullopt;
    }
    return state::parseHexAddress(*value);
}

std::optional<bcos::u256> parseOptionalQuantity(pt::ptree const& node, std::string const& key)
{
    auto const value = node.get_optional<std::string>(key);
    if (!value.has_value())
    {
        return std::nullopt;
    }
    return parseQuantity(*value);
}

evmc_address addressFromFixed(bcos::Address const& address)
{
    evmc_address out{};
    auto const view = address.ref();
    std::memcpy(out.bytes, view.data(), sizeof(out.bytes));
    return out;
}

// ── Divergence ledger: DIVERGENCES.md is the *only* exemption source ───────────────────────────
// Predeclared gate discipline (plan §"预注册的 gate 纪律" + this task's brief): a replay
// mismatch is always a finding, filed in DIVERGENCES.md under one of three attributions
// (a: bcos-evm/opstack defect pending its own fix plan; b: generator defect, already fixed and
// the whole vector batch regenerated, so it should never reach this code; c: accepted difference,
// but ONLY once a human has signed off). This ledger's parser knows nothing about *what* value is
// expected anywhere -- it only reads which (vectorId, field) pairs DIVERGENCES.md has already
// filed, under which attribution/status, from the ledger file itself. No expected/actual value is
// ever hardcoded here; that would be exactly the "手改期望值" the plan's rule 2 forbids, just
// relocated into C++ instead of JSON.
struct DivergenceEntry
{
    std::string entryId;
    std::string attribution;  // "a" | "b" | "c"
    std::string status;       // "PENDING-FIX" | "SIGNED-OFF" | ...
};

class DivergenceLedger
{
public:
    // Missing file => empty ledger => every mismatch is unexempted (fails), same as before this
    // mechanism existed. This is intentional: an absent ledger must never silently mean "anything
    // goes".
    static DivergenceLedger loadFromFile(std::filesystem::path const& path)
    {
        DivergenceLedger ledger;
        std::ifstream input(path);
        if (!input.is_open())
        {
            return ledger;
        }
        // Machine-parseable line, invisible in rendered Markdown (HTML comment). Order of
        // key=value pairs is fixed; see DIVERGENCES.md's own header for the authoring contract
        // this regex mirrors. Example:
        //   <!-- ALLOWLIST vectorId=isthmus_transfer_basic field=receipts[0].gasUsed
        //        entry=FINDING-1 attribution=a status=PENDING-FIX -->
        static std::regex const linePattern(
            R"(<!--\s*ALLOWLIST\s+vectorId=(\S+)\s+field=(\S+)\s+entry=(\S+)\s+attribution=(\S+)\s+status=(\S+)\s*-->)");
        std::string line;
        while (std::getline(input, line))
        {
            std::smatch match;
            if (std::regex_search(line, match, linePattern))
            {
                ledger.m_entries[{match[1].str(), match[2].str()}] =
                    DivergenceEntry{match[3].str(), match[4].str(), match[5].str()};
            }
        }
        return ledger;
    }

    // Returns the covering entry only when the ledger records it under a status that exempts it
    // from failing the build right now: attribution "a" while its own fix is still pending, or
    // attribution "c" once a human has signed off. An unlisted (vectorId, field), or one listed
    // under any other status (e.g. attribution "c" awaiting signoff), returns nullopt -- the
    // caller must still fail, per rule 1 ("分歧即 finding，不许静默 skip").
    [[nodiscard]] std::optional<DivergenceEntry> lookupExempt(
        std::string const& vectorId, std::string const& field) const
    {
        auto const it = m_entries.find({vectorId, field});
        if (it == m_entries.end())
        {
            return std::nullopt;
        }
        auto const& entry = it->second;
        bool const exempt = (entry.attribution == "a" && entry.status == "PENDING-FIX") ||
                            (entry.attribution == "c" && entry.status == "SIGNED-OFF");
        if (!exempt)
        {
            return std::nullopt;
        }
        return entry;
    }

private:
    std::map<std::pair<std::string, std::string>, DivergenceEntry> m_entries;
};

// ── Diagnostic hex formatting for DIVERGE messages ─────────────────────────────────────────────

std::string hexU256(bcos::u256 const& value)
{
    std::ostringstream out;
    out << "0x" << std::hex << value;
    return out.str();
}

std::string hexU64(uint64_t value)
{
    std::ostringstream out;
    out << "0x" << std::hex << value;
    return out.str();
}

std::string hexBytes(bcos::bytes const& value)
{
    return bcos::toHexStringWithPrefix(value);
}

std::string hexBytes32(evmc_bytes32 const& value)
{
    return bcos::toHexStringWithPrefix(bcos::bytesConstRef(value.bytes, sizeof(value.bytes)));
}

std::string hexAddress(evmc_address const& value)
{
    return bcos::toHexStringWithPrefix(bcos::bytesConstRef(value.bytes, sizeof(value.bytes)));
}

// ── Divergence-reporting comparators ────────────────────────────────────────────────────────────
// On mismatch: an unlisted (or not-yet-exempt) (vectorId, field) still fails the build via
// BOOST_CHECK_MESSAGE ("DIVERGE ..."), same as before this mechanism existed. A mismatch the
// ledger already accounts for under an exempting status instead prints
// "KNOWN-DIVERGE <vector> <entry-id>" via BOOST_WARN_MESSAGE (visible in ctest output, never
// counted as a failed assertion) -- this is the mechanism that turns the gate from "permanently
// red" into "red only for un-filed divergences" per this task's brief.

void reportKnownDivergence(std::string const& vectorId, std::string const& field,
    DivergenceEntry const& entry, std::string const& want, std::string const& got)
{
    BOOST_WARN_MESSAGE(false, "KNOWN-DIVERGE " << vectorId << " " << entry.entryId << " field="
                                               << field << " want=" << want << " got=" << got);
}

void checkU256(DivergenceLedger const& ledger, std::string const& vectorId,
    std::string const& field, bcos::u256 const& want, bcos::u256 const& got)
{
    if (want == got)
    {
        return;
    }
    if (auto const entry = ledger.lookupExempt(vectorId, field))
    {
        reportKnownDivergence(vectorId, field, *entry, hexU256(want), hexU256(got));
        return;
    }
    BOOST_CHECK_MESSAGE(false, "DIVERGE " << vectorId << " " << field << " want=" << hexU256(want)
                                          << " got=" << hexU256(got));
}

void checkU64(DivergenceLedger const& ledger, std::string const& vectorId, std::string const& field,
    uint64_t want, uint64_t got)
{
    if (want == got)
    {
        return;
    }
    if (auto const entry = ledger.lookupExempt(vectorId, field))
    {
        reportKnownDivergence(vectorId, field, *entry, hexU64(want), hexU64(got));
        return;
    }
    BOOST_CHECK_MESSAGE(false, "DIVERGE " << vectorId << " " << field << " want=" << hexU64(want)
                                          << " got=" << hexU64(got));
}

void checkBytes(DivergenceLedger const& ledger, std::string const& vectorId,
    std::string const& field, bcos::bytes const& want, bcos::bytes const& got)
{
    if (want == got)
    {
        return;
    }
    if (auto const entry = ledger.lookupExempt(vectorId, field))
    {
        reportKnownDivergence(vectorId, field, *entry, hexBytes(want), hexBytes(got));
        return;
    }
    BOOST_CHECK_MESSAGE(false, "DIVERGE " << vectorId << " " << field << " want=" << hexBytes(want)
                                          << " got=" << hexBytes(got));
}

void checkBytes32(DivergenceLedger const& ledger, std::string const& vectorId,
    std::string const& field, evmc_bytes32 const& want, evmc_bytes32 const& got)
{
    bool const equal = std::memcmp(want.bytes, got.bytes, sizeof(want.bytes)) == 0;
    if (equal)
    {
        return;
    }
    if (auto const entry = ledger.lookupExempt(vectorId, field))
    {
        reportKnownDivergence(vectorId, field, *entry, hexBytes32(want), hexBytes32(got));
        return;
    }
    BOOST_CHECK_MESSAGE(false, "DIVERGE " << vectorId << " " << field << " want="
                                          << hexBytes32(want) << " got=" << hexBytes32(got));
}

void checkAddress(DivergenceLedger const& ledger, std::string const& vectorId,
    std::string const& field, evmc_address const& want, evmc_address const& got)
{
    bool const equal = std::memcmp(want.bytes, got.bytes, sizeof(want.bytes)) == 0;
    if (equal)
    {
        return;
    }
    if (auto const entry = ledger.lookupExempt(vectorId, field))
    {
        reportKnownDivergence(vectorId, field, *entry, hexAddress(want), hexAddress(got));
        return;
    }
    BOOST_CHECK_MESSAGE(false, "DIVERGE " << vectorId << " " << field << " want="
                                          << hexAddress(want) << " got=" << hexAddress(got));
}

// ── Minimal block gas pool ──────────────────────────────────────────────────────────────────────
// Reimplements the SubGas/AddGas semantics of opstack_tx::BlockGasPool
// (transaction-executor/bcos-transaction-executor/OpStackTxInputBuilder.h) without atomics: the
// replayer executes transactions sequentially, so lock-free concurrency support would be dead
// weight. Pulling the full production header here would also drag in ledger/executor/tars
// dependencies for a lightweight, self-contained test binary.
class SimpleBlockGasPool
{
public:
    explicit SimpleBlockGasPool(int64_t gasLimit) : m_remaining(std::max<int64_t>(0, gasLimit)) {}

    bool subGas(uint64_t gas)
    {
        auto const requested = static_cast<int64_t>(
            std::min<uint64_t>(gas, static_cast<uint64_t>(std::numeric_limits<int64_t>::max())));
        if (m_remaining < requested)
        {
            return false;
        }
        m_remaining -= requested;
        return true;
    }

    void returnGas(uint64_t gasRemaining, uint64_t /*gasUsed*/)
    {
        auto const returned = static_cast<int64_t>(std::min<uint64_t>(
            gasRemaining, static_cast<uint64_t>(std::numeric_limits<int64_t>::max())));
        m_remaining += returned;
    }

private:
    int64_t m_remaining;
};

// ── Fork name → OP Stack fork schedule ──────────────────────────────────────────────────────────
// RevisionConfig stays makeIsthmusRevisionConfig() (EVM revision = Prague) for every OP fork
// covered so far -- matches the existing precedent in L1AttributesDepositTest.cpp, where the
// Jovian test case sets only forkSchedule = makeJovianPlusForkSchedule() and leaves
// revisionConfig as makeIsthmusRevisionConfig(). Only forkSchedule varies by fork name.
OpStackForkSchedule forkScheduleForName(std::string const& fork)
{
    if (fork == "jovian")
    {
        return makeJovianPlusForkSchedule();
    }
    return makeIsthmusPlusForkSchedule();
}

// ── Cross-tx state propagation: merge, not replace ──────────────────────────────────────────────
// State::build_diff() (bcos-evm/eth/state/State.cpp) emits a *sparse* per-account patch: scalar
// fields (balance/nonce/code) are populated only when their dirty flag is set or the value
// differs from tx-start, and `storage` holds only the slots whose value changed this tx (see the
// file's `touched || !patch.storage.empty()` gate). helpers/ApplyStateDiffToView.h's
// applyStateDiffToView() does a whole-account `insert_account` replace, which is only safe when
// every prior field of the account is itself already covered by the incoming patch (true for
// every other opstack test's L1Block account, which starts *absent* from `pre` so the first
// deposit's native L1Block setter dispatch writes -- and thus patches -- every slot it touches).
// This vector instead pre-seeds L1Block's fee slots directly (per its `_info.comment`: no L1Block
// bytecode is deployed) while its deposit tx's calldata additionally writes the block-number/
// timestamp slot (slot 0, previously absent) via the same native dispatch. That makes slot 0
// "touched" while slots 1/3/7/8 keep their pre-seeded values (unchanged this tx, so excluded from
// the sparse patch) -- a whole-account replace would then wipe the untouched-but-still-current
// fee slots the very next tx reads for L1/operator fee. Mirror the real ledger apply path instead
// (bcos-evm/storage/StateDiffApplier.h): gate scalar fields on their dirty flag, merge storage
// per-slot. Like StateDiffApplier.h, this does not special-case Account::storageReset (CREATE/
// SELFDESTRUCT-recreate storage-namespace resets) -- out of scope for this vector (no CREATE/
// SELFDESTRUCT in it); StateDiffApplier.h's own doc comment records the same accepted gap.
void applyStateDiffMerged(state::StateDiff const& diff, state::test::InMemoryStateView& stateView)
{
    for (auto const& address : diff.deletedAccounts)
    {
        stateView.erase_account(address);
    }
    for (auto const& [address, patch] : diff.accounts)
    {
        auto account = stateView.get_account(address).value_or(state::Account{});
        if (patch.balanceDirty)
        {
            account.balance = patch.balance;
        }
        if (patch.nonceDirty)
        {
            account.nonce = patch.nonce;
        }
        if (patch.codeDirty)
        {
            account.code = patch.code;
            account.codeHash = patch.codeHash;
        }
        for (auto const& [slot, value] : patch.storage)
        {
            account.storage[slot] = value;
        }
        stateView.insert_account(address, std::move(account));
    }
}

// ── pre / env / postState / receipts parsing ────────────────────────────────────────────────────

void seedPreState(pt::ptree const& preNode, state::test::InMemoryStateView& stateView)
{
    for (auto const& [addressHex, accountNode] : preNode)
    {
        auto const address = state::parseHexAddress(addressHex);
        state::Account account;
        account.balance = parseQuantity(accountNode.get<std::string>("balance", "0x0"));
        account.nonce = parseUint64(accountNode.get<std::string>("nonce", "0x0"));
        account.code = parseHexBytes(accountNode.get<std::string>("code", "0x"));
        if (auto const storageNode = accountNode.get_child_optional("storage"))
        {
            for (auto const& [slotHex, valueNode] : *storageNode)
            {
                account.storage[parseBytes32Str(slotHex)] =
                    parseBytes32Str(valueNode.get_value<std::string>());
            }
        }
        stateView.insert_account(address, std::move(account));
    }
}

state::BlockInfo parseEnv(pt::ptree const& vectorNode)
{
    state::BlockInfo blockInfo;
    auto const& env = vectorNode.get_child("env");
    blockInfo.coinbase = state::parseHexAddress(env.get<std::string>("currentCoinbase"));
    blockInfo.number = static_cast<int64_t>(parseUint64(env.get<std::string>("currentNumber")));
    blockInfo.timestamp =
        static_cast<int64_t>(parseUint64(env.get<std::string>("currentTimestamp")));
    blockInfo.gasLimit = static_cast<int64_t>(parseUint64(env.get<std::string>("currentGasLimit")));
    blockInfo.baseFee = parseQuantity(env.get<std::string>("currentBaseFee", "0x0"));
    if (auto const random = env.get_optional<std::string>("currentRandom"))
    {
        blockInfo.prevRandao = parseBytes32Str(*random);
    }
    if (auto const beaconRoot = env.get_optional<std::string>("parentBeaconBlockRoot"))
    {
        blockInfo.parentBeaconBlockRoot = parseBytes32Str(*beaconRoot);
    }
    return blockInfo;
}

void checkAccount(DivergenceLedger const& ledger, std::string const& vectorId,
    std::string const& addressHex, pt::ptree const& acctNode,
    state::test::InMemoryStateView const& stateView)
{
    auto const address = state::parseHexAddress(addressHex);
    auto const account = stateView.get_account(address).value_or(state::Account{});
    std::string const field = "postState." + addressHex + ".";

    if (auto const balance = acctNode.get_optional<std::string>("balance"))
    {
        checkU256(ledger, vectorId, field + "balance", parseQuantity(*balance), account.balance);
    }
    if (auto const nonce = acctNode.get_optional<std::string>("nonce"))
    {
        checkU64(ledger, vectorId, field + "nonce", parseUint64(*nonce), account.nonce);
    }
    if (auto const code = acctNode.get_optional<std::string>("code"))
    {
        checkBytes(ledger, vectorId, field + "code", parseHexBytes(*code), account.code);
    }
    if (auto const storageNode = acctNode.get_child_optional("storage"))
    {
        for (auto const& [slotHex, valueNode] : *storageNode)
        {
            auto const slot = parseBytes32Str(slotHex);
            auto const want = parseBytes32Str(valueNode.get_value<std::string>());
            auto const it = account.storage.find(slot);
            evmc_bytes32 const got = (it != account.storage.end()) ? it->second : evmc_bytes32{};
            checkBytes32(ledger, vectorId, field + "storage[" + slotHex + "]", want, got);
        }
    }
}

void checkReceipt(DivergenceLedger const& ledger, std::string const& vectorId, size_t txIndex,
    pt::ptree const& expected, OpStackMessageResult const& output, uint8_t txKind)
{
    std::string const field = "receipts[" + std::to_string(txIndex) + "].";

    if (auto const wantType = expected.get_optional<std::string>("type"))
    {
        checkU64(ledger, vectorId, field + "type", parseUint64(*wantType), txKind);
    }

    auto const wantStatus = parseUint64(expected.get<std::string>("status"));
    auto const gotStatus =
        (output.evmcResult.status_code == EVMC_SUCCESS) ? uint64_t{1} : uint64_t{0};
    checkU64(ledger, vectorId, field + "status", wantStatus, gotStatus);

    auto const wantGasUsed = parseUint64(expected.get<std::string>("gasUsed"));
    auto const gotGasUsed = static_cast<uint64_t>(std::max<int64_t>(0, output.gasUsed));
    checkU64(ledger, vectorId, field + "gasUsed", wantGasUsed, gotGasUsed);

    auto const wantLogsCount = static_cast<uint64_t>(expected.get<int>("logsCount", 0));
    checkU64(ledger, vectorId, field + "logsCount", wantLogsCount, output.logs.size());

    if (auto const depositNonce = expected.get_optional<std::string>("_op_deposit_nonce"))
    {
        BOOST_REQUIRE_MESSAGE(output.receiptMeta.depositNonce.has_value(),
            "DIVERGE " << vectorId << " " << field << "_op_deposit_nonce want=" << *depositNonce
                       << " got=<absent>");
        checkU64(ledger, vectorId, field + "_op_deposit_nonce", parseUint64(*depositNonce),
            *output.receiptMeta.depositNonce);
    }
    if (auto const depositVersion =
            expected.get_optional<std::string>("_op_deposit_receipt_version"))
    {
        BOOST_REQUIRE_MESSAGE(output.receiptMeta.depositReceiptVersion.has_value(),
            "DIVERGE " << vectorId << " " << field << "_op_deposit_receipt_version want="
                       << *depositVersion << " got=<absent>");
        checkU64(ledger, vectorId, field + "_op_deposit_receipt_version",
            parseUint64(*depositVersion), *output.receiptMeta.depositReceiptVersion);
    }
    if (auto const l1Fee = expected.get_optional<std::string>("_op_l1_fee"))
    {
        BOOST_REQUIRE_MESSAGE(output.receiptMeta.l1Fee.has_value(),
            "DIVERGE " << vectorId << " " << field << "_op_l1_fee want=" << *l1Fee
                       << " got=<absent>");
        checkU256(ledger, vectorId, field + "_op_l1_fee", parseQuantity(*l1Fee),
            *output.receiptMeta.l1Fee);
    }
}

// ── Per-tx-kind input construction + execution ──────────────────────────────────────────────────

OpStackMessageResult applyDepositTx(pt::ptree const& txNode,
    state::test::InMemoryStateView& stateView, evmc::VM& vm, crypto::Hash const& hashImpl,
    state::BlockInfo const& blockInfo, OpStackForkSchedule const& forkSchedule,
    SimpleBlockGasPool& gasPool)
{
    auto const& depositNode = txNode.get_child("_op_deposit");

    OpStackDepositTx deposit;
    deposit.sourceHash = parseH256(depositNode.get<std::string>("source_hash"));
    deposit.from = state::parseHexAddress(depositNode.get<std::string>("from"));
    deposit.to = parseOptionalAddress(depositNode, "to");
    deposit.mint = parseOptionalQuantity(depositNode, "mint");
    // `value` lives inside `_op_deposit` (parallel to `mint`), matching op-geth's
    // types.DepositTx (Mint/Value are sibling fields of the same struct) and the generator's
    // `inputDeposit`/`outputDepositTx` Go structs (main.go). Found during Task 3 vector
    // authoring: this function previously read `value` from the outer tx object, which the
    // generator never populates for deposits (buildTx's "deposit" branch only reads
    // `in.OpDeposit.Value`) -- so every deposit vector with a non-zero intended `value` was
    // silently generated with value=0 by opt8n while the replayer would have read a different
    // (also-zero, coincidentally never populated) location. Not a real bcos-evm/opstack
    // divergence; a replayer/vector-schema bug fixed before any vector relying on deposit
    // `value` was committed.
    deposit.value = parseOptionalQuantity(depositNode, "value").value_or(0);
    deposit.gas = parseUint64(txNode.get<std::string>("gasLimit"));
    deposit.isSystemTransaction = parseBool(depositNode, "is_system_tx", false);
    deposit.data = parseHexBytes(txNode.get<std::string>("data", "0x"));

    evmc_message message{};
    message.kind = deposit.to.has_value() ? EVMC_CALL : EVMC_CREATE;
    message.sender = deposit.from;
    if (deposit.to.has_value())
    {
        message.recipient = *deposit.to;
        message.code_address = *deposit.to;
    }
    message.gas = static_cast<int64_t>(deposit.gas);
    message.input_data = deposit.data.data();
    message.input_size = deposit.data.size();
    message.value = state::toEvmC(deposit.value);

    OpStackMessageRequest input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hashImpl;
    input.message = message;
    input.blockInfo = blockInfo;
    input.revisionConfig = makeIsthmusRevisionConfig();
    input.forkSchedule = forkSchedule;
    input.web3TypedTxKind = toWeb3TypedTxKindValue(Web3TypedTxKind::OpStackDeposit);
    input.depositTx = deposit;
    input.gasPoolSubGasHook = [&gasPool](uint64_t gas) { return gasPool.subGas(gas); };
    input.gasPoolReturnGasHook = [&gasPool](uint64_t remaining, uint64_t used) {
        gasPool.returnGas(remaining, used);
    };

    return task::syncWait(applyOpStackMessage(std::move(input)));
}

// Decodes `_op_raw` via the same RLP path production Web3 transactions use
// (bcos::codec::rlp::decode -> bcos::rpc::Web3Transaction, bcos-rpc/web3jsonrpc/model), then
// cross-checks the decoded envelope against the vector's field-form tx (nonce/fee caps/gasLimit/
// to/value/chainId/data) per the plan's "_op_raw is authoritative" rule. Signature bytes (r/s/v)
// are intentionally not re-derived here: RLP-minimal-encodes them (no leading zero bytes) while
// the JSON field form is left-padded to 32 bytes, so a byte-for-byte compare would need
// normalization for no additional coverage -- sender recovery from the signature is already
// exercised end-to-end via decoded.sender() below (feeds directly into message.sender, and thus
// into every balance/nonce postState check for the signing account).
OpStackMessageResult applyEip1559Tx(DivergenceLedger const& ledger, std::string const& vectorId,
    size_t txIndex, pt::ptree const& txNode, state::test::InMemoryStateView& stateView,
    evmc::VM& vm, crypto::Hash const& hashImpl, state::BlockInfo const& blockInfo,
    OpStackForkSchedule const& forkSchedule, SimpleBlockGasPool& gasPool)
{
    auto const rawBytes = parseHexBytes(txNode.get<std::string>("_op_raw"));

    bcos::rpc::Web3Transaction decoded;
    {
        auto copy = rawBytes;
        bcos::bytesRef in(copy.data(), copy.size());
        auto error = bcos::codec::rlp::decode(in, decoded);
        BOOST_REQUIRE_MESSAGE(error == nullptr,
            "DIVERGE " << vectorId << " tx[" << txIndex
                       << "]._op_raw decode failed: " << (error ? error->errorMessage() : ""));
    }

    std::string const field = "tx[" + std::to_string(txIndex) + "].rawConsistency.";
    checkU64(ledger, vectorId, field + "nonce", parseUint64(txNode.get<std::string>("nonce")),
        decoded.nonce);
    checkU256(ledger, vectorId, field + "maxFeePerGas",
        parseQuantity(txNode.get<std::string>("maxFeePerGas")), decoded.maxFeePerGas);
    checkU256(ledger, vectorId, field + "maxPriorityFeePerGas",
        parseQuantity(txNode.get<std::string>("maxPriorityFeePerGas")),
        decoded.maxPriorityFeePerGas);
    checkU64(ledger, vectorId, field + "gasLimit", parseUint64(txNode.get<std::string>("gasLimit")),
        decoded.gasLimit);
    checkU256(ledger, vectorId, field + "value", parseQuantity(txNode.get<std::string>("value")),
        decoded.value);
    checkU64(ledger, vectorId, field + "chainId", parseUint64(txNode.get<std::string>("chainId")),
        decoded.chainId.value_or(0));
    checkBytes(ledger, vectorId, field + "data",
        parseHexBytes(txNode.get<std::string>("data", "0x")), decoded.data);
    if (auto const toStr = txNode.get_optional<std::string>("to"))
    {
        BOOST_REQUIRE_MESSAGE(decoded.to.has_value(),
            "DIVERGE " << vectorId << " " << field << "to want=" << *toStr << " got=<absent>");
        checkAddress(ledger, vectorId, field + "to", state::parseHexAddress(*toStr),
            addressFromFixed(*decoded.to));
    }

    auto const sender = state::parseHexAddress(decoded.sender());

    evmc_message message{};
    message.kind = decoded.to.has_value() ? EVMC_CALL : EVMC_CREATE;
    message.sender = sender;
    if (decoded.to.has_value())
    {
        auto const recipient = addressFromFixed(*decoded.to);
        message.recipient = recipient;
        message.code_address = recipient;
    }
    message.gas = static_cast<int64_t>(decoded.gasLimit);
    message.input_data = decoded.data.data();
    message.input_size = decoded.data.size();
    message.value = state::toEvmC(decoded.value);

    OpStackMessageRequest input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hashImpl;
    input.message = message;
    input.nonce = decoded.nonce;
    input.gasTipCap = decoded.maxPriorityFeePerGas;
    input.gasFeeCap = decoded.maxFeePerGas;
    input.blockInfo = blockInfo;
    input.revisionConfig = makeIsthmusRevisionConfig();
    input.forkSchedule = forkSchedule;
    input.web3TypedTxKind = toWeb3TypedTxKindValue(Web3TypedTxKind::EIP1559);
    input.rollupCostData = newRollupCostData(bcos::ref(rawBytes));
    input.gasPoolSubGasHook = [&gasPool](uint64_t gas) { return gasPool.subGas(gas); };
    input.gasPoolReturnGasHook = [&gasPool](uint64_t remaining, uint64_t used) {
        gasPool.returnGas(remaining, used);
    };

    return task::syncWait(applyOpStackMessage(std::move(input)));
}

// ── Vector-level replay ──────────────────────────────────────────────────────────────────────────

void replayVector(DivergenceLedger const& ledger, std::string const& vectorId,
    pt::ptree const& vectorNode, std::string const& forkName)
{
    state::test::InMemoryStateView stateView;
    seedPreState(vectorNode.get_child("pre"), stateView);

    auto const envBlockInfo = parseEnv(vectorNode);
    auto const forkSchedule = forkScheduleForName(forkName);

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hashImpl;

    auto const& expectedReceipts = vectorNode.get_child("_op_expected.receipts");
    auto receiptIt = expectedReceipts.begin();
    size_t txIndex = 0;
    uint64_t blockGasUsed = 0;

    for (auto const& [blockKey, blockNode] : vectorNode.get_child("blocks"))
    {
        (void)blockKey;
        // blockHeader mirrors env.number/timestamp/gasLimit for single-block vectors (schema
        // redundancy); block header is authoritative here since multiple blocks (not exercised
        // by this first vector) would each carry their own header.
        auto const& header = blockNode.get_child("blockHeader");
        state::BlockInfo blockInfo = envBlockInfo;
        blockInfo.number = static_cast<int64_t>(parseUint64(header.get<std::string>("number")));
        blockInfo.timestamp =
            static_cast<int64_t>(parseUint64(header.get<std::string>("timestamp")));
        blockInfo.gasLimit = static_cast<int64_t>(parseUint64(header.get<std::string>("gasLimit")));

        SimpleBlockGasPool gasPool(blockInfo.gasLimit);

        for (auto const& [txKey, txNode] : blockNode.get_child("transactions"))
        {
            (void)txKey;
            auto const opType = txNode.get<std::string>("_op_type", "");

            OpStackMessageResult output;
            uint8_t txKind = 0;
            if (opType == "deposit")
            {
                txKind = toWeb3TypedTxKindValue(Web3TypedTxKind::OpStackDeposit);
                output = applyDepositTx(
                    txNode, stateView, vm, hashImpl, blockInfo, forkSchedule, gasPool);
            }
            else if (opType == "eip1559")
            {
                txKind = toWeb3TypedTxKindValue(Web3TypedTxKind::EIP1559);
                output = applyEip1559Tx(ledger, vectorId, txIndex, txNode, stateView, vm, hashImpl,
                    blockInfo, forkSchedule, gasPool);
            }
            else
            {
                BOOST_ERROR("DIVERGE " << vectorId << " tx[" << txIndex << "] unsupported _op_type="
                                       << opType << " (replayer scope so far: deposit, eip1559)");
                ++txIndex;
                if (receiptIt != expectedReceipts.end())
                {
                    ++receiptIt;
                }
                continue;
            }

            BOOST_REQUIRE_MESSAGE(receiptIt != expectedReceipts.end(),
                "DIVERGE " << vectorId << " tx[" << txIndex << "] no matching expected receipt");
            checkReceipt(ledger, vectorId, txIndex, receiptIt->second, output, txKind);

            blockGasUsed += static_cast<uint64_t>(std::max<int64_t>(0, output.gasUsed));
            applyStateDiffMerged(output.stateDiff, stateView);

            ++txIndex;
            ++receiptIt;
        }
    }

    auto const wantBlockGasUsed =
        parseUint64(vectorNode.get<std::string>("_op_expected.blockGasUsed"));
    checkU64(ledger, vectorId, "blockGasUsed", wantBlockGasUsed, blockGasUsed);

    for (auto const& [addressHex, acctNode] : vectorNode.get_child("postState"))
    {
        checkAccount(ledger, vectorId, addressHex, acctNode, stateView);
    }
}

}  // namespace

BOOST_AUTO_TEST_CASE(replay_t8n_vectors)
{
    std::filesystem::path const vectorsDir{T8N_VECTORS_DIR};
    BOOST_REQUIRE_MESSAGE(
        std::filesystem::exists(vectorsDir), "t8n vectors directory missing: " << vectorsDir);

    std::vector<std::filesystem::path> files;
    for (auto const& entry : std::filesystem::directory_iterator(vectorsDir))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        auto const& path = entry.path();
        if (path.extension() != ".json")
        {
            continue;
        }
        // Generator inputs (transfer_basic.in.json) are not vectors; skip them.
        if (path.filename().string().ends_with(".in.json"))
        {
            continue;
        }
        files.push_back(path);
    }
    std::sort(files.begin(), files.end());
    BOOST_REQUIRE_MESSAGE(!files.empty(), "no t8n vector files found in " << vectorsDir);

    // DIVERGENCES.md is the sole exemption source (see DivergenceLedger's doc comment); loaded
    // once here and threaded down through every comparator so a KNOWN-DIVERGE finding is reported
    // instead of failing the build, while anything not yet filed there still turns the build red.
    auto const ledger = DivergenceLedger::loadFromFile(vectorsDir / "DIVERGENCES.md");

    for (auto const& path : files)
    {
        pt::ptree root;
        std::ifstream input(path);
        BOOST_REQUIRE_MESSAGE(input.is_open(), "cannot open vector file: " << path);
        pt::read_json(input, root);

        std::string forkName = "isthmus";
        if (auto const meta = root.get_child_optional("_op_test_vectors"))
        {
            forkName = meta->get<std::string>("fork", "isthmus");
        }

        for (auto const& [vectorId, vectorNode] : root)
        {
            if (vectorId == "_op_test_vectors")
            {
                continue;
            }
            BOOST_TEST_CONTEXT("vector=" << vectorId << " file=" << path.filename().string())
            {
                replayVector(ledger, vectorId, vectorNode, forkName);
            }
        }
    }
}

}  // namespace bcos::evm::test
