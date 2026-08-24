// ReplayGate.h — shared OP block-level differential replay gate.
//
// Extracted from OpT8nReplayTest.cpp (schema v3-block vectors) so the corpus
// test gate and the op-mainnet-replay CLI run the same per-block comparison
// machinery. Boost-free: the failure sink is ReplayReport (+ FailStream lazy
// streaming). When the consuming TU defines OP_REPLAY_BOOST before including,
// ReplayReport::fail() also routes every message through BOOST_ERROR.
//
// Generalization (live-chain path): the replay functions no longer hold an
// evmone::test::TestState directly. They read via StateBackend (a
// evmone::state::StateView whose reads must be satisfied by the backend),
// apply StateDiff via StateBackend::applyDiff, build the post-execution root
// via StateBackend::visitAccounts (the stateRootOf<Ledger> contract), and load
// vector `pre` states via StateBackend::loadPre. TestStateBackend wraps an
// in-memory evmone::test::TestState with behavior identical to the pre-split
// code (write-back = applyStateDiffStrict); the RocksDB backend
// (StateBackendRocksDB.h) serves the live chain path.
//
// ReplayOptions (per-run knobs, default = corpus semantics):
//   - requirePostState: live chain vectors carry no postState (full-state
//     equality is proven by the stateRoot field instead); false skips the
//     whole postState section AND the setcode delegation anchor in
//     loadBlockContext (both jAt(postState)-backed).
//   - chainId: default chain id used when a block carries no per-tx chainId
//     (deposit-only blocks); corpus constant is 8453, live Sepolia is
//     11155420.
//   - blockHashes: pre-filled number->hash table for historical BLOCKHASH
//     reads ([H0-256, H0-1]); when empty, ParentOnlyBlockHashes keeps its
//     number-1-only corpus behavior.
#ifndef OP_REPLAY_GATE_H
#define OP_REPLAY_GATE_H

#include "../StateDiffWriteback.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-evm/adapter/StateRootCompute.h>
#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/opstack/OpPredeploys.h>
#include <bcos-evm/opstack/OpTransition.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <cxxabi.h>
#include <evmone/evmone.h>
#include <fmt/format.h>
#include <json/json.h>
#include <opstack-executor/OpBlockExecute.h>
#include <opstack-executor/OpSchedulerSeam.h>
#include <algorithm>
#include <bcos-evm/eth/state/hash_utils.hpp>
#include <cstdint>
#include <evmc/evmc.hpp>
#include <evmc/hex.hpp>
#include <evmone_precompiles/secp256k1.hpp>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <intx/intx.hpp>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <test/utils/rlp.hpp>
#include <test/utils/test_state.hpp>
#include <vector>

#ifdef OP_REPLAY_BOOST
#include <boost/test/unit_test.hpp>
#endif

namespace op_replay
{
using namespace evmone;
using namespace bcos::evm::opstack;
// Non-conflicting alias for jsoncpp's value type (a bare `using Json = Json::Value`
// would shadow the `Json` namespace and break Json::Reader/Json::Value).
using JsonValue = Json::Value;

// ── jsoncpp .at() equivalent ────────────────────────────────────────────────
// nlohmann's .at(key) throws on a missing member; jsoncpp's operator[] silently
// fabricates a null. Every required-field access in this replayer goes through
// jAt so a missing field is a named failure, never a silent null read.
//
// key is `const char*` (not `const std::string&`) deliberately: a string literal
// argument would materialize a temporary std::string, and GCC-14's -Wdangling-reference
// flags any reference-returning call that receives a class-type temporary — even though
// the returned reference aliases `v`, never `key` (false positive). A `const char*`
// argument creates no temporary, so the warning cannot fire. Callers with a std::string
// key pass .c_str() (jAt(post, authAddr)).
inline const Json::Value& jAt(const Json::Value& v, const char* key)
{
    if (!v.isMember(key))
        throw std::invalid_argument(std::string("missing required field: ") + key);
    return v[key];
}

// jsoncpp Reader-based parse (nlohmann Json::parse equivalent; string and stream).
inline Json::Value jParse(const std::string& input)
{
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(input, root))
        throw std::runtime_error("JSON parse failed: " + reader.getFormattedErrorMessages());
    return root;
}

inline Json::Value jParse(std::istream& input)
{
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(input, root))
        throw std::runtime_error("JSON parse failed: " + reader.getFormattedErrorMessages());
    return root;
}

// ── Failure report + lazy failure stream ───────────────────────────────────
// OP_REPLAY_FAIL(msg) must be invoked WITHOUT wrapping parentheses around msg:
// `FailStream() << (a << b)` would evaluate `a << b` first (a std::string
// left-shifted by const char* — a compile error). The left-associative chain
// `FailStream() << a << b` keeps the stream temporaries alive to full-expression
// end, where the destructor reports the accumulated message.

struct ReplayReport
{
    int failures = 0;
    std::vector<std::string> details;

    void reset()
    {
        failures = 0;
        details.clear();
    }
    void fail(const std::string& msg)
    {
        ++failures;
        details.push_back(msg);
#ifdef OP_REPLAY_BOOST
        BOOST_ERROR(msg);
#endif
    }
};
inline ReplayReport& replayReport()
{
    static ReplayReport r;
    return r;
}

class FailStream
{
public:
    ~FailStream() { replayReport().fail(m_os.str()); }
    template <typename T>
    FailStream& operator<<(const T& v)
    {
        m_os << v;
        return *this;
    }

private:
    std::ostringstream m_os;
};

#define OP_REPLAY_FAIL(msg) ::op_replay::FailStream() << msg

// ── Corpus chain id ──────────────────────────────────────────────────────────
// The generator buildChainConfig (generator/main.go) pins all cases to 8453
// (0x2105). Vectors with normal txs use tx.chainId (asserted consistent across
// the block); deposit-only vectors carry no chainId, so this corpus constant is
// used — a mirror of the generator's constant, not a fallback default. The live
// path overrides via ReplayOptions.chainId (11155420 on Sepolia).

constexpr uint64_t kCorpusChainId = 0x2105;

// ── Per-run knobs (see file header) ─────────────────────────────────────────
struct ReplayOptions
{
    bool requirePostState = true;  // false: live chain vectors carry no postState
    bool comparePostState =
        true;  // (both set by --skip-poststate; section is gated on requirePostState)
    uint64_t chainId = kCorpusChainId;             // corpus 8453; live Sepolia 11155420
    std::map<int64_t, evmc::bytes32> blockHashes;  // pre-filled [N-256, N-1] number->hash table
};

}  // namespace op_replay

// ── Local subset re-implementation of evmone test::from_json ─────────────────
// (at global scope — nested inside op_replay this block would create a separate
// op_replay::evmone::test namespace and break the qualified names used above)
// The vcpkg evmone package does not ship test/utils/statetest.hpp (which declares
// evmone's test::from_json<T>); the t8n gate needs a few of its conversions.
// Declare the primary template and define only the used specializations, with
// semantics identical to evmone statetest_loader.cpp (ints accept number or "0x"
// hex strings; bytes/address/hash256 via evmc::from_hex; TestState parses
// account objects and drops zero-valued storage slots).
namespace evmone::test
{
using ::op_replay::jAt;  // from_json<TestState> reads required fields through the gate's jAt

template <typename T>
T from_json(const Json::Value& j) = delete;

template <>
inline int64_t from_json<int64_t>(const Json::Value& j)
{
    if (j.isIntegral())
    {
        if (j.isInt64())
            return j.asInt64();
        throw std::invalid_argument("from_json<int64_t>: integer out of range");
    }
    if (!j.isString())
        throw std::invalid_argument("from_json<int64_t>: must be integer or string of integer");
    const auto s = j.asString();
    size_t num_processed = 0;
    const auto v = static_cast<int64_t>(std::stoull(s, &num_processed, 0));
    if (num_processed == 0 || num_processed != s.size())
        throw std::invalid_argument("from_json<int64_t>: must be integer or string of integer");
    return v;
}

template <>
inline uint64_t from_json<uint64_t>(const Json::Value& j)
{
    if (j.isIntegral())
    {
        if (j.isUInt64())
            return j.asUInt64();
        throw std::invalid_argument("from_json<uint64_t>: integer out of range");
    }
    if (!j.isString())
        throw std::invalid_argument("from_json<uint64_t>: must be integer or string of integer");
    const auto s = j.asString();
    size_t num_processed = 0;
    const auto v = static_cast<uint64_t>(std::stoull(s, &num_processed, 0));
    if (num_processed == 0 || num_processed != s.size())
        throw std::invalid_argument("from_json<uint64_t>: must be integer or string of integer");
    return v;
}

template <>
inline intx::uint256 from_json<intx::uint256>(const Json::Value& j)
{
    return intx::from_string<intx::uint256>(j.asString());
}

template <>
inline evmone::bytes from_json<evmone::bytes>(const Json::Value& j)
{
    return evmc::from_hex(j.asString()).value();
}

template <>
inline evmc::address from_json<evmc::address>(const Json::Value& j)
{
    const auto v = evmc::from_hex<evmc::address>(j.asString());
    if (!v.has_value())
        throw std::invalid_argument("from_json<address>: must be hexadecimal string");
    return *v;
}

// Note: evmone::hash256 is a using-alias of evmc::bytes32, so this one specialization serves both
// from_json<hash256> (header hashes) and from_json<bytes32> (storage keys/values).
template <>
inline evmc::bytes32 from_json<evmc::bytes32>(const Json::Value& j)
{
    const auto s = j.asString();
    if (s == "0" || s == "0x0")  // Special case to handle "0". Required by exec-spec-tests.
        return evmc::bytes32{};
    const auto v = evmc::from_hex<evmc::bytes32>(s);
    if (!v.has_value())
        throw std::invalid_argument("from_json<bytes32>: must be hexadecimal string");
    return *v;
}

template <>
inline evmone::test::TestState from_json<evmone::test::TestState>(const Json::Value& j)
{
    evmone::test::TestState o;
    assert(j.isObject());
    for (const auto& j_addr : j.getMemberNames())
    {
        const auto& j_acc = j[j_addr];
        auto& acc = o[from_json<evmc::address>(Json::Value(j_addr))] = {
            .nonce = from_json<uint64_t>(jAt(j_acc, "nonce")),
            .balance = from_json<intx::uint256>(jAt(j_acc, "balance")),
            .storage = {},
            .code = from_json<evmone::bytes>(jAt(j_acc, "code"))};
        if (j_acc.isMember("storage"))
        {
            const auto& storage = j_acc["storage"];
            for (const auto& j_key : storage.getMemberNames())
            {
                const auto& j_value = storage[j_key];
                if (const auto value = from_json<evmc::bytes32>(j_value); !evmc::is_zero(value))
                    acc.storage[from_json<evmc::bytes32>(Json::Value(j_key))] = value;
            }
        }
    }
    return o;
}
}  // namespace evmone::test

namespace op_replay
{
// ── Canonical printing (the only want/got form written by DIVERGE/ALLOWLIST) ─
// Numeric: "0x"-prefixed lowercase minimal hex (same shape as generator
// hexutil.EncodeUint64/EncodeBig). Hash/address: "0x" fixed-length lowercase.
// The absent side is always "<absent>".
constexpr const char* kAbsent = "<absent>";

inline std::string hexU64(uint64_t v)
{
    std::ostringstream out;
    out << "0x" << std::hex << v;
    return out.str();
}

inline std::string hexU256(const intx::uint256& v)
{
    return "0x" + intx::to_string(v, 16);
}

inline std::string hexHash(const hash256& h)
{
    return "0x" + evmc::hex(evmc::bytes_view{h.bytes, sizeof(h.bytes)});
}

inline std::string hexAddr(const evmc::address& a)
{
    return "0x" + evmc::hex(evmc::bytes_view{a.bytes, sizeof(a.bytes)});
}

inline std::string hexBytes(evmc::bytes_view b)
{
    return "0x" + evmc::hex(b);
}

// bytes32 slot keys/values are written as minimal-numeric hex (trie semantics: 0 == absent,
// compared after normalization).
inline std::string hexSlot(const evmc::bytes32& b)
{
    return hexU256(intx::be::load<intx::uint256>(b));
}

inline intx::uint256 parseU256(const JsonValue& j)
{
    return intx::from_string<intx::uint256>(j.asString());
}

// ── DivergenceLedger (brief block E) ────────────────────────────────────────

struct AllowEntry
{
    std::string vectorId, field, entryId, attribution, status, want, got;
    bool exempt = false;
    int hits = 0;
};

class DivergenceLedger
{
public:
    ReplayOptions opts;  // per-run knobs (chainId / blockHashes / postState gate)

    // Missing ledger file = FAILURE (the ledger is a gate deliverable; a missing file must never
    // imply all-exempt/all-empty).
    static DivergenceLedger load(const std::filesystem::path& path)
    {
        DivergenceLedger ledger;
        std::ifstream input(path);
        if (!input.is_open())
        {
            OP_REPLAY_FAIL("DIVERGENCES.md missing: " << path);
            return ledger;
        }
        // Mirrors the DIVERGENCES.md "machine format" section verbatim.
        static const std::regex linePattern(
            R"(<!--\s*ALLOWLIST\s+vectorId=(\S+)\s+field=(\S+)\s+entry=(\S+)\s+attribution=(\S+)\s+status=(\S+)\s+want=(\S+)\s+got=(\S+)\s*-->)");
        static const std::regex headingPattern(R"(^##\s+(\S+))");
        std::string line;
        std::set<std::string> headings;
        while (std::getline(input, line))
        {
            std::smatch m;
            if (std::regex_search(line, m, headingPattern))
                headings.insert(m[1].str());
            if (std::regex_search(line, m, linePattern))
            {
                AllowEntry e{m[1].str(), m[2].str(), m[3].str(), m[4].str(), m[5].str(), m[6].str(),
                    m[7].str()};
                e.exempt = (e.attribution == "a" && e.status == "PENDING-FIX") ||
                           (e.attribution == "c" && e.status == "SIGNED-OFF");
                // FINDING-dual-* entries belong to OpDualPathEquivalenceTest's own ledger
                // instance (A-vs-B harness); this suite's finish() stale-check must not see them.
                if (e.entryId.rfind("FINDING-dual-", 0) != 0)
                    ledger.m_entries.push_back(std::move(e));
            }
        }
        // Dangling entry= (no matching "## <ENTRY-ID>" heading) = FAILURE: an
        // ALLOWLIST row must hang under a real FINDING/entry section; a lone row has no evidence.
        for (const auto& e : ledger.m_entries)
        {
            if (!headings.contains(e.entryId))
                OP_REPLAY_FAIL("DIVERGENCES.md ALLOWLIST entry="
                               << e.entryId << " (vectorId=" << e.vectorId << " field=" << e.field
                               << ") has no matching '## " << e.entryId << "' heading");
        }
        return ledger;
    }

    // Single divergence-reporting entry point: full 4-tuple match with exempt
    // status -> KNOWN-DIVERGE (stdout + count); otherwise ADD_FAILURE. The
    // 4-tuple match prevents new regressions on the same field riding old exemptions.
    void diverge(const std::string& vectorId, const std::string& field, const std::string& want,
        const std::string& got)
    {
        for (auto& e : m_entries)
        {
            if (e.exempt && e.vectorId == vectorId && e.field == field && e.want == want &&
                e.got == got)
            {
                ++e.hits;
                ++m_knownCount;
                std::cout << "KNOWN-DIVERGE " << vectorId << " " << e.entryId << " field=" << field
                          << " want=" << want << " got=" << got << "\n";
                return;
            }
        }
        OP_REPLAY_FAIL(
            "DIVERGE " << vectorId << " " << field << " want=" << want << " got=" << got);
    }

    // An exemption never hit this run = FAILURE (stale exemption turns red; must be cleared after
    // fix/vector regen).
    void finish() const
    {
        for (const auto& e : m_entries)
        {
            if (e.exempt && e.hits == 0)
                OP_REPLAY_FAIL("stale ALLOWLIST exemption never hit this run: entry="
                               << e.entryId << " vectorId=" << e.vectorId << " field=" << e.field
                               << " want=" << e.want << " got=" << e.got);
        }
    }

private:
    std::vector<AllowEntry> m_entries;
    int m_knownCount = 0;
};

// ── Per-vector comparison context (comparison count + field prefix) ─────────

struct VectorContext
{
    DivergenceLedger& ledger;
    std::string id;
    int comparisons = 0;

    void checkField(const std::string& field, const std::string& want, const std::string& got)
    {
        ++comparisons;
        if (want != got)
            ledger.diverge(id, field, want, got);
    }

    // checkOptional semantics (brief block D): want present + got absent ->
    // got=<absent>; want absent + got present -> want=<absent>; both absent pass.
    // Never gate on has_value() before comparing.
    void checkOptional(const std::string& field, const std::optional<std::string>& want,
        const std::optional<std::string>& got)
    {
        ++comparisons;
        if (!want.has_value() && !got.has_value())
            return;
        const auto w = want.value_or(kAbsent);
        const auto g = got.value_or(kAbsent);
        if (w != g)
            ledger.diverge(id, field, w, g);
    }
};

// ── Generalized state backend (live-chain path) ─────────────────────────────
// Replay reads the block pre/post state through this interface instead of an
// evmone::test::TestState directly. A backend is a StateView (the processOpBlock
// read side), plus applyDiff (write-back), visitAccounts (the
// stateRootOf<Ledger> + postState-reverse enumeration surface) and loadPre
// (vector `pre` states).
struct AccountView
{
    const evmc::address& addr;
    uint64_t nonce;
    const intx::uint256& balance;
    evmc::bytes32 codeHash;
    const std::map<evmc::bytes32, evmc::bytes32>& storage;
};

class StateBackend : public evmone::state::StateView
{
public:
    // StateDiff write-back (deleted_accounts must be deleted; modified_storage
    // value 0 erases the slot; code overwritten only when has_value()).
    virtual void applyDiff(const evmone::state::StateDiff& d) = 0;
    // Full account enumeration (stateRootOf + postState reverse-existence). The
    // visitor must not retain references past the call.
    virtual bool visitAccounts(const std::function<bool(const AccountView&)>& visitor) const = 0;
    // Full storage map of one account (message-passer seal snapshot, postState
    // slot-union enumeration).
    virtual std::map<evmc::bytes32, evmc::bytes32> getAccountStorage(
        const evmc::address& addr) const = 0;
    // Load a vector `pre` state (replaces the whole backend state).
    virtual void loadPre(const JsonValue& pre) = 0;
};

// In-memory backend over evmone::test::TestState — byte-for-byte the pre-split
// behavior (write-back = applyStateDiffStrict; keccak codeHash; map reads).
class TestStateBackend final : public StateBackend
{
public:
    evmone::test::TestState& state;
    explicit TestStateBackend(evmone::test::TestState& s) : state(s) {}

    // Note: must be StateView::Account (the nested 4-field view contract), not the
    // namespace-scope evmone::state::Account (account.hpp's full journaled account).
    std::optional<evmone::state::StateView::Account> get_account(
        const evmc::address& addr) const noexcept override
    {
        const auto it = state.find(addr);
        if (it == state.end())
            return std::nullopt;
        return evmone::state::StateView::Account{it->second.nonce, it->second.balance,
            evmone::keccak256(it->second.code), !it->second.storage.empty()};
    }
    evmone::bytes get_account_code(const evmc::address& addr) const noexcept override
    {
        const auto it = state.find(addr);
        return it == state.end() ? evmone::bytes{} : it->second.code;
    }
    evmc::bytes32 get_storage(
        const evmc::address& addr, const evmc::bytes32& key) const noexcept override
    {
        const auto it = state.find(addr);
        if (it == state.end())
            return evmc::bytes32{};
        const auto sit = it->second.storage.find(key);
        return sit == it->second.storage.end() ? evmc::bytes32{} : sit->second;
    }
    void applyDiff(const evmone::state::StateDiff& d) override
    {
        bcos::evm::applyStateDiffStrict(state, d);
    }
    bool visitAccounts(const std::function<bool(const AccountView&)>& visitor) const override
    {
        for (const auto& [addr, account] : state)
        {
            const AccountView view{.addr = addr,
                .nonce = account.nonce,
                .balance = account.balance,
                .codeHash = evmone::keccak256(account.code),
                .storage = account.storage};
            if (!visitor(view))
                return false;
        }
        return true;
    }
    std::map<evmc::bytes32, evmc::bytes32> getAccountStorage(
        const evmc::address& addr) const override
    {
        const auto it = state.find(addr);
        return it == state.end() ? std::map<evmc::bytes32, evmc::bytes32>{} : it->second.storage;
    }
    void loadPre(const JsonValue& pre) override
    {
        state = evmone::test::from_json<evmone::test::TestState>(pre);
    }
};

// ── BlockHashes: return env.parentHash only for number-1 ────────────────────
// (EIP-2935 system call in block 1 stores the genesis hash = env.parentHash;
// other heights are never queried by this corpus, so returning zero exposes any
// out-of-range query instead of silently fabricating a hash chain.) The live
// path pre-fills [N-256, N-1] number->hash (ReplayOptions.blockHashes) so real
// chain contracts reading historical block hashes get the true values.
struct ParentOnlyBlockHashes final : state::BlockHashes
{
    int64_t blockNumber = 0;
    hash256 parentHash{};
    const std::map<int64_t, evmc::bytes32>* prefilled = nullptr;

    evmc::bytes32 get_block_hash(int64_t block_number) const noexcept override
    {
        if (prefilled != nullptr)
        {
            if (const auto it = prefilled->find(block_number); it != prefilled->end())
                return it->second;
        }
        return block_number == blockNumber - 1 ? parentHash : evmc::bytes32{};
    }
};

// ── EIP-7702 authority recovery ─────────────────────────────────────────────
// The in-module recoverAuthority (OpTransition.cpp:34-45) lives in an anonymous
// namespace and is not exported; minimally re-implement it here (same formula
// keccak256(0x05 || rlp([chain_id,address,nonce])) + evmmax secp256k1 ecrecover).
// After building, assert signer.has_value() — evmone/module transition silently
// skips tuples whose signer was not recovered, so corpus signatures must be recoverable.

// ── structurallyUnrecoverable: secp256k1 structural-validity predicate ──────
// (EIP-2/EIP-7702 malleability boundary; no ecrecover, only whether r/s/v fall
// outside the recoverable domain.)

inline const intx::uint256 kSecpN = intx::from_string<intx::uint256>(
    "0xfffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141");
inline const intx::uint256 kSecpHalfN = kSecpN >> 1;

inline bool structurallyUnrecoverable(const evmone::state::Authorization& a)
{
    return a.v > 1 || a.s > kSecpHalfN || a.r == 0 || a.r >= kSecpN || a.s == 0 || a.s >= kSecpN;
}

inline std::optional<evmc::address> replayRecoverAuthority(const state::Authorization& auth)
{
    const auto msg = bytes{0x05} + rlp::encode_tuple(auth.chain_id, auth.addr, auth.nonce);
    const auto h = keccak256(msg);
    const auto r = intx::be::store<evmc::bytes32>(auth.r);
    const auto s = intx::be::store<evmc::bytes32>(auth.s);
    return evmmax::secp256k1::ecrecover(std::span<const uint8_t, 32>{h.bytes, 32},
        std::span<const uint8_t, 32>{r.bytes, 32}, std::span<const uint8_t, 32>{s.bytes, 32},
        auth.v != 0);
}

// ── manifest.txt: one required vector filename per line ('#' comments and blank lines ignored) ─

inline std::set<std::string> loadManifest(const std::filesystem::path& path)
{
    std::set<std::string> names;
    std::ifstream input(path);
    if (!input.is_open())
    {
        OP_REPLAY_FAIL("manifest.txt missing: " << path);
        return names;
    }
    std::string line;
    while (std::getline(input, line))
    {
        const auto b = line.find_first_not_of(" \t\r");
        if (b == std::string::npos)
            continue;
        const auto e = line.find_last_not_of(" \t\r");
        line = line.substr(b, e - b + 1);
        if (line.empty() || line[0] == '#')
            continue;
        names.insert(line);
    }
    return names;
}

// ── Current API adaptation helpers ───────────────────────────────────────────
// After plan A phase 2 (cf8d1af70): processOpBlock takes 9 params
// (receiptFactory), receipts are bcos::protocol::TransactionReceipt::Ptr, OP
// fields come via opStackMeta() (bcos::u256 / uint64).
// bcos::u256 -> "0x" + lowercase no-leading-zero hex (same shape as setOpStackMeta's u256ToHex).
inline std::string hexU256Bcos(const bcos::u256& v)
{
    return "0x" + v.str(0, std::ios_base::hex);
}

/// Same receiptFactory construction as the W6 harness (OpNewPayloadRpcE2eTest.cpp:93-95).
inline bcos::protocol::TransactionReceiptFactory::Ptr makeTestReceiptFactory()
{
    return std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(
        std::make_shared<bcos::crypto::CryptoSuite>(
            std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr));
}

// ── Single-block load context (shared by replaySingleBlockInto / assertRejectThrow) ──
// Pure move of the load section from the original replayVector path (previously
// :456-643), no assertion-logic change. On failure (invalid hardfork /
// inconsistent intra-block chainId / unknown _op_type) it OP_REPLAY_FAILs and
// returns false; the caller returns directly.

struct BlockContext
{
    const OpForkConfig* cfg = nullptr;
    bool isJovian = false;
    state::BlockInfo blk;
    ParentOnlyBlockHashes hashes;
    std::vector<OpBlockTx> txs;
    uint64_t chainId = kCorpusChainId;
    // decode-class reject (blob): processOpBlock never reaches raw-tx decode
    // (txs are already OpBlockTx), so the load section reproduces the real
    // rejection via the type-byte classification (execute hook / runOpBlockInjection)
    // and records the message here for assertRejectThrow to assert directly.
    std::optional<std::string> decodeRejectMessage;
};

inline bool loadBlockContext(const std::string& id, const JsonValue& blk, BlockContext& out,
    uint64_t defaultChainId = kCorpusChainId)
{
    // _info.hardfork must be exactly ecotone|fjord|granite|holocene|isthmus|jovian,
    // anything else = FAILURE. No default fork (the default-Isthmus precedent is a
    // known hole, not ported). isJovian drives the blobGasUsed header gate and the
    // _op_da_footprint expectation — ecotone/fjord/granite/holocene are all false,
    // matching isthmus semantics (has_da_footprint true only on Jovian).
    const auto hardfork = jAt(jAt(blk, "_info"), "hardfork").asString();
    if (hardfork == "isthmus")
        out.cfg = &isthmusConfig();
    else if (hardfork == "jovian")
    {
        out.cfg = &jovianConfig();
        out.isJovian = true;
    }
    else if (hardfork == "ecotone")
        out.cfg = &ecotoneConfig();
    else if (hardfork == "fjord")
        out.cfg = &fjordConfig();
    else if (hardfork == "granite")
        out.cfg = &graniteConfig();
    else if (hardfork == "holocene")
        out.cfg = &holoceneConfig();
    else
    {
        OP_REPLAY_FAIL(id << ": _info.hardfork must be exactly "
                             "ecotone|fjord|granite|holocene|isthmus|jovian, got '"
                          << hardfork << "' (no default fork)");
        return false;
    }

    // env (all 8 fields required) -> hand-built BlockInfo.
    const auto& env = jAt(blk, "env");
    auto& bi = out.blk;
    bi.number = test::from_json<int64_t>(jAt(env, "currentNumber"));
    bi.timestamp = test::from_json<int64_t>(jAt(env, "currentTimestamp"));
    bi.gas_limit = test::from_json<int64_t>(jAt(env, "currentGasLimit"));
    bi.base_fee = test::from_json<uint64_t>(jAt(env, "currentBaseFee"));
    bi.coinbase = test::from_json<evmc::address>(jAt(env, "currentCoinbase"));
    bi.prev_randao = test::from_json<hash256>(jAt(env, "currentRandom"));
    bi.parent_beacon_block_root = test::from_json<hash256>(jAt(env, "parentBeaconBlockRoot"));

    auto& hs = out.hashes;
    hs.blockNumber = bi.number;
    hs.parentHash = test::from_json<hash256>(jAt(env, "parentHash"));

    // Three transaction arms (deposit / eip1559 / setcode). Unknown _op_type = FAILURE.
    auto& txs = out.txs;
    std::optional<uint64_t> vectorChainId;
    for (const auto& t : jAt(jAt(blk, "block"), "transactions"))
    {
        const auto opType = jAt(t, "_op_type").asString();
        if (opType == "deposit")
        {
            const auto& d = jAt(t, "_op_deposit");
            DepositTx dep;
            dep.source_hash = test::from_json<hash256>(jAt(d, "source_hash"));
            dep.from = test::from_json<evmc::address>(jAt(d, "from"));
            dep.to = jAt(d, "to").isNull() ?
                         std::nullopt :
                         std::optional{test::from_json<evmc::address>(jAt(d, "to"))};
            dep.mint = d.isMember("mint") ? std::optional{parseU256(jAt(d, "mint"))} : std::nullopt;
            dep.value = d.isMember("value") ? parseU256(jAt(d, "value")) : intx::uint256{0};
            dep.gas_limit = test::from_json<int64_t>(jAt(d, "gas"));
            dep.is_system_tx = jAt(d, "is_system_tx").asBool();
            dep.data = test::from_json<bytes>(jAt(t, "data"));
            txs.push_back({.tx = std::move(dep), .signedEnvelope = {}});
        }
        else if (opType == "eip1559" || opType == "setcode")
        {
            state::Transaction tx;
            tx.type = opType == "setcode" ? state::Transaction::Type::set_code :
                                            state::Transaction::Type::eip1559;
            tx.sender = test::from_json<evmc::address>(jAt(t, "sender"));
            tx.to = jAt(t, "to").isNull() ?
                        std::nullopt :
                        std::optional{test::from_json<evmc::address>(jAt(t, "to"))};
            tx.nonce = test::from_json<uint64_t>(jAt(t, "nonce"));
            tx.gas_limit = test::from_json<int64_t>(jAt(t, "gas"));
            tx.max_gas_price = parseU256(jAt(t, "maxFeePerGas"));
            tx.max_priority_gas_price = parseU256(jAt(t, "maxPriorityFeePerGas"));
            tx.value = parseU256(jAt(t, "value"));
            tx.data = test::from_json<bytes>(jAt(t, "data"));
            // EIP-2930 access list (optional; no hits in the legacy 25 vectors, dormant path).
            if (t.isMember("accessList"))
            {
                for (const auto& e : jAt(t, "accessList"))
                {
                    std::vector<evmc::bytes32> keys;
                    for (const auto& k : jAt(e, "storageKeys"))
                        keys.push_back(test::from_json<hash256>(k));
                    tx.access_list.emplace_back(
                        test::from_json<evmc::address>(jAt(e, "address")), std::move(keys));
                }
            }
            tx.chain_id = test::from_json<uint64_t>(jAt(t, "chainId"));
            if (vectorChainId.has_value() && *vectorChainId != tx.chain_id)
            {
                OP_REPLAY_FAIL(id << ": inconsistent chainId across txs: " << hexU64(*vectorChainId)
                                  << " vs " << hexU64(tx.chain_id));
                return false;
            }
            vectorChainId = tx.chain_id;
            if (opType == "setcode")
            {
                // hasMarked/hasUnmarked/anchorOk: mix-check marked tuples
                // (structurally unrecoverable, _op_signer_unrecoverable=true) and
                // unmarked tuples (existing recovery path); when marked tuples exist
                // there must be >=1 unmarked tuple anchoring the delegation in postState.
                bool hasMarked = false;
                bool hasUnmarked = false;
                bool anchorOk = false;
                for (const auto& a : jAt(t, "_op_authorization_list"))
                {
                    state::Authorization auth;
                    auth.chain_id = parseU256(jAt(a, "chainId"));
                    auth.addr = test::from_json<evmc::address>(jAt(a, "address"));
                    auth.nonce = test::from_json<uint64_t>(jAt(a, "nonce"));
                    auth.r = parseU256(jAt(a, "r"));
                    auth.s = parseU256(jAt(a, "s"));
                    auth.v = parseU256(jAt(a, "yParity"));

                    const bool marked = a.isMember("_op_signer_unrecoverable");
                    if (marked && (!jAt(a, "_op_signer_unrecoverable").isBool() ||
                                      !jAt(a, "_op_signer_unrecoverable").asBool()))
                    {
                        OP_REPLAY_FAIL(id << ": _op_signer_unrecoverable must be literal true");
                        continue;
                    }
                    if (marked)
                    {
                        // Reverse-verify with the structural predicate (no bare
                        // ecrecover); signer left empty and tuple passed through as-is —
                        // production OpTransition.cpp:46-135 does real ecrecover and
                        // skips per its predicate (the real differential path).
                        if (!structurallyUnrecoverable(auth))
                            OP_REPLAY_FAIL(
                                id << ": marked unrecoverable but structurally recoverable");
                        hasMarked = true;
                    }
                    else
                    {
                        // Fill recovered signer + assert per tuple (evmone silently skips tuples
                        // without a signer).
                        auth.signer = replayRecoverAuthority(auth);
                        if (!auth.signer.has_value())
                            OP_REPLAY_FAIL(id << ": authorization signer recovery failed (unmarked "
                                                 "tuple)");
                        else
                        {
                            hasUnmarked = true;
                            // Non-empty delegation anchor existence: the authority must
                            // carry 0xef0100||tuple.addr delegation code in the vector
                            // postState (required only when this tx has marked tuples).
                            // Live chain vectors carry no postState — the anchor is not
                            // checkable there (full-state equality rides the stateRoot
                            // compare instead).
                            const auto authAddr = hexAddr(*auth.signer);
                            if (blk.isMember("postState"))
                            {
                                const auto& post = jAt(blk, "postState");
                                if (post.isMember(authAddr))
                                {
                                    const std::string wantCode =
                                        "0xef0100" + hexAddr(auth.addr).substr(2);
                                    if (jAt(post, authAddr.c_str())
                                            .get("code", Json::Value(""))
                                            .asString() == wantCode)
                                        anchorOk = true;
                                }
                            }
                        }
                    }
                    tx.authorization_list.push_back(std::move(auth));
                }
                if (hasMarked && !hasUnmarked)
                    OP_REPLAY_FAIL(id
                                   << ": setcode tx with marked tuples must contain >=1 unmarked "
                                      "tuple");
                if (hasMarked && hasUnmarked && !anchorOk)
                    OP_REPLAY_FAIL(id << ": marked-tuple tx has no applied delegation anchor in "
                                         "postState");
            }
            auto envelope = test::from_json<bytes>(jAt(t, "_op_raw"));
            txs.push_back({.tx = std::move(tx), .signedEnvelope = std::move(envelope)});
        }
        else if (opType == "legacy")
        {
            // Task 3 F1 legacy arm: type-0 EIP-155 protected tx. Single gasPrice (no
            // maxFeePerGas/maxPriorityFeePerGas); evmone legacy has priority==max==gasPrice.
            state::Transaction tx;
            tx.type = state::Transaction::Type::legacy;
            tx.sender = test::from_json<evmc::address>(jAt(t, "sender"));
            tx.to = jAt(t, "to").isNull() ?
                        std::nullopt :
                        std::optional{test::from_json<evmc::address>(jAt(t, "to"))};
            tx.nonce = test::from_json<uint64_t>(jAt(t, "nonce"));
            tx.gas_limit = test::from_json<int64_t>(jAt(t, "gas"));
            const auto gasPrice = parseU256(jAt(t, "gasPrice"));
            tx.max_gas_price = gasPrice;
            tx.max_priority_gas_price = gasPrice;
            tx.value = parseU256(jAt(t, "value"));
            tx.data = test::from_json<bytes>(jAt(t, "data"));
            tx.chain_id = test::from_json<uint64_t>(jAt(t, "chainId"));
            if (vectorChainId.has_value() && *vectorChainId != tx.chain_id)
            {
                OP_REPLAY_FAIL(id << ": inconsistent chainId across txs: " << hexU64(*vectorChainId)
                                  << " vs " << hexU64(tx.chain_id));
                return false;
            }
            vectorChainId = tx.chain_id;
            auto envelope = test::from_json<bytes>(jAt(t, "_op_raw"));
            txs.push_back({.tx = std::move(tx), .signedEnvelope = std::move(envelope)});
        }
        else if (opType == "blob")
        {
            // Task 4 blob arm: type-0x3 blob txs are decode-class rejected on OP chains.
            // processOpBlock never reaches raw-tx decode (txs are already OpBlockTx), so
            // reproduce the real rejection via the type-byte classification the execute hook /
            // runOpBlockInjection apply (rawTxBytes[i][0] == 0x03 → OpConsensusError), record the
            // message, and let assertRejectThrow assert it directly (consumer-first; review R16
            // consumer:both).
            const auto raw = test::from_json<bytes>(jAt(t, "_op_raw"));
            const bcos::bytes rawVec(raw.begin(), raw.end());
            try
            {
                // Same type-byte classification as OpScheduler::execute / runOpBlockInjection:
                // blob (0x03) is not in {0x01, 0x02, 0x04} and not a legacy RLP list (>= 0xc0).
                if (rawVec.empty())
                    throw bcos::evm::engine::OpConsensusError("op block: empty envelope");
                constexpr uint8_t kRlpListBase = 0xc0;
                const auto typeByte = rawVec[0];
                if (typeByte < kRlpListBase && typeByte != 0x01 && typeByte != 0x02 &&
                    typeByte != 0x04)
                    throw bcos::evm::engine::OpConsensusError(
                        fmt::format("op block: unsupported tx type byte 0x{:02x}",
                            static_cast<unsigned>(typeByte)));
                OP_REPLAY_FAIL(
                    id << ": blob raw envelope must be rejected by type-byte classification");
                return false;
            }
            catch (const std::runtime_error& e)
            {
                // Note: must not use catch(std::exception) — libevmone(-fno-rtti)
                // brings in a hidden non-unique typeinfo for std::exception, so typed
                // catch does not reliably bind the runtime_error subtree
                // (see OpSchedulerSeam.h:1083-1104); the runtime_error branch is
                // verified to bind (assertRejectThrow). OpConsensusError is a FISCO-side
                // runtime_error subclass with libc++ unique typeinfo — it binds.
                out.decodeRejectMessage = std::string(e.what());
            }
            // Placeholder tx: keeps the post-deposit non-deposit structure (the
            // decodeRejectMessage branch never runs processOpBlock; the placeholder
            // is only for structural completeness).
            state::Transaction placeholder;
            placeholder.type = state::Transaction::Type::blob;
            placeholder.sender = test::from_json<evmc::address>(jAt(t, "sender"));
            placeholder.to = jAt(t, "to").isNull() ?
                                 std::nullopt :
                                 std::optional{test::from_json<evmc::address>(jAt(t, "to"))};
            placeholder.gas_limit = test::from_json<int64_t>(jAt(t, "gas"));
            placeholder.value = parseU256(jAt(t, "value"));
            placeholder.data = test::from_json<bytes>(jAt(t, "data"));
            placeholder.max_gas_price =
                t.isMember("maxFeePerGas") ? parseU256(jAt(t, "maxFeePerGas")) : intx::uint256{};
            placeholder.max_priority_gas_price = t.isMember("maxPriorityFeePerGas") ?
                                                     parseU256(jAt(t, "maxPriorityFeePerGas")) :
                                                     intx::uint256{};
            placeholder.chain_id = vectorChainId.value_or(kCorpusChainId);
            txs.push_back({.tx = std::move(placeholder), .signedEnvelope = std::move(raw)});
        }
        else
        {
            OP_REPLAY_FAIL(id << ": unknown _op_type '" << opType << "'");
            return false;
        }
    }
    out.chainId = vectorChainId.value_or(defaultChainId);
    return true;
}

/// Executes one block: blk env/hardfork/transactions + backend (pre ready or inherited).
/// A nullptr pre skips pre parsing (chain block i>0 pre:null inherits the backend
/// state after the previous block's applyDiff writes). touchedAddrs/touchedSlots
/// are per-block (block N's .uncovered must not see addresses touched in blocks 0..N-1).
inline void replaySingleBlockInto(const std::string& id, const JsonValue& blk,
    StateBackend& backend, const JsonValue* pre, DivergenceLedger& ledger, evmc::VM& vm,
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory)
{
    VectorContext ctx{ledger, id};
    BlockContext bc;
    if (!loadBlockContext(id, blk, bc, ledger.opts.chainId))
        return;
    const auto& cfg = *bc.cfg;
    const bool isJovian = bc.isJovian;

    // pre -> backend state (evmone golden loader; balance/nonce/code required, zero-valued
    // storage slots dropped per trie semantics). A nullptr pre inherits the caller's state.
    if (pre != nullptr)
        backend.loadPre(*pre);

    // Live path: pre-fill the historical BLOCKHASH table ([H0-256, H0-1] number->hash).
    if (!ledger.opts.blockHashes.empty())
        bc.hashes.prefilled = &ledger.opts.blockHashes;

    // Execute: the applyDiff callback both writes back into the backend and accumulates the write
    // set (used by the coverage assertion).
    std::set<evmc::address> touchedAddrs;
    std::map<evmc::address, std::set<evmc::bytes32>> touchedSlots;
    const auto apply = [&](const state::StateDiff& d) {
        for (const auto& m : d.modified_accounts)
        {
            touchedAddrs.insert(m.addr);
            for (const auto& [k, val] : m.modified_storage)
                touchedSlots[m.addr].insert(k);
        }
        for (const auto& a : d.deleted_accounts)
            touchedAddrs.insert(a);
        backend.applyDiff(d);
    };

    OpBlockResult result;
    try
    {
        result = processOpBlock(
            backend, bc.blk, bc.hashes, bc.txs, cfg, vm, bc.chainId, receiptFactory, apply);
    }
    catch (const std::exception& e)
    {
        OP_REPLAY_FAIL(id << ": processOpBlock threw block-level error: " << e.what());
        return;
    }
    catch (...)
    {
        // typed-catch RTTI fallback (see docs/audits/2026-07-12-typed-catch-rtti-investigation.md):
        // libevmone.a (-fno-rtti) ships a hidden non-unique typeinfo copy for
        // std::exception, so after linking all catch(std::exception&) here compare
        // unequal to the libc++ throwing side's base typeinfo and miss (arm64
        // non-unique RTTI bit-mix rule). Diagnosis is not swallowed: name the dynamic
        // type, then finish with the same semantics as the typed branch.
        const auto* excType = abi::__cxa_current_exception_type();
        OP_REPLAY_FAIL(
            id << ": processOpBlock threw block-level error (typed catch "
               << "bypassed, exception type: " << (excType ? excType->name() : "<unknown>") << ")");
        return;
    }

    // seal: message passer storage = end-of-block (post-finalize) snapshot (OpBlockExecute.h
    // contract).
    const auto mpStorage = backend.getAccountStorage(OP_L2_TO_L1_MESSAGE_PASSER);
    const auto seal = sealOpBlock(result, cfg, mpStorage);

    // ── header six fields ────────────────────────────────────────────────────
    const auto& h = jAt(jAt(blk, "_op_expected"), "header");
    ctx.checkField("gasUsed", hexU256(parseU256(jAt(h, "gasUsed"))),
        hexU64(static_cast<uint64_t>(result.gasUsed)));
    ctx.checkField("receiptsRoot", hexHash(test::from_json<hash256>(jAt(h, "receiptsRoot"))),
        hexHash(seal.receiptsRoot));
    // bloom is always compared as 512 hex chars (a zero bloom is an all-zero string, not absent).
    {
        auto wantBloom = jAt(h, "logsBloom").asString();
        std::ranges::transform(wantBloom, wantBloom.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (wantBloom.size() != 2 + 512 || !wantBloom.starts_with("0x"))
        {
            OP_REPLAY_FAIL(id << ": header.logsBloom must be 0x + 512 hex chars, got "
                              << wantBloom.size() << " chars");
            return;
        }
        ctx.checkField("logsBloom", wantBloom, hexBytes(evmc::bytes_view(seal.logsBloom)));
    }
    ctx.checkField("withdrawalsRoot", hexHash(test::from_json<hash256>(jAt(h, "withdrawalsRoot"))),
        hexHash(seal.withdrawalsRoot));
    // ── header.stateRoot (single leg: execution+engine vs op-geth consensus root) ─
    // Timing: the seal-stage backend is already the full post-finalize world state (same
    // anchor as the messagePasserStorage snapshot); later postState comparisons only
    // read the backend, never write — safe to build the root here. Engine correctness
    // (evmone mpt_hash) is anchored upstream; not re-proven here. Red failures
    // should be attributed to execution/accounting or pre-alloc completeness first.
    ctx.checkField("stateRoot", hexHash(test::from_json<hash256>(jAt(h, "stateRoot"))),
        hexHash(bcos::evm::stateRootOf(backend)));
    // requestsHash: pre-Prague (ecotone/fjord/..., incl. ecotone_upgrade_fjord_activation)
    // vectors do not emit this key (op-geth t8n omitempty; only Prague has EIP-7685
    // requests), so the want side goes through checkOptional by presence.
    ctx.checkOptional("requestsHash",
        h.isMember("requestsHash") ?
            std::optional{hexHash(test::from_json<hash256>(jAt(h, "requestsHash")))} :
            std::nullopt,
        seal.requestsHash.has_value() ? std::optional{hexHash(*seal.requestsHash)} : std::nullopt);
    // blobGasUsed: all six forks emit it in vectors (op-geth headers really carry 0x0,
    // a 4844 leftover field from Ecotone+). Jovian -> value compare ("0x0" is an
    // in-place zero, e.g. jovian_first_block); the other five forks assert the C++
    // side is absent (seal.blobGasUsed semantics = Jovian DA-footprint header field;
    // pre-Isthmus has no such reuse bit, and the vector's 0x0 is informational only).
    {
        const auto wantBlobGas =
            parseU256(jAt(h, "blobGasUsed"));  // required (always emitted on Ecotone+)
        const auto gotBlobGas =
            seal.blobGasUsed.has_value() ? std::optional{hexU64(*seal.blobGasUsed)} : std::nullopt;
        if (isJovian)
            ctx.checkOptional("blobGasUsed", std::optional{hexU256(wantBlobGas)}, gotBlobGas);
        else
            ctx.checkOptional("blobGasUsed", std::nullopt, gotBlobGas);
    }

    // ── receipts ────────────────────────────────────────────────────────────
    const auto& expReceipts = jAt(jAt(blk, "_op_expected"), "receipts");
    if (expReceipts.size() != result.receipts.size())
    {
        OP_REPLAY_FAIL(id << ": receipts count mismatch: expected " << expReceipts.size() << " got "
                          << result.receipts.size() << " (no zip-min)");
        return;
    }
    for (size_t i = 0; i < expReceipts.size(); ++i)
    {
        const auto& er = expReceipts[static_cast<Json::ArrayIndex>(i)];
        const std::string p = "receipts[" + std::to_string(i) + "]";

        // Got-side unified view (plan A phase 2 API): FISCO TransactionReceipt::Ptr +
        // parallel txTypes byte (EIP-2718 type). OP fields via opStackMeta(); deposit vs
        // normal tx discriminated by kDepositTxType (equivalent to the old
        // OpDepositReceipt/OpTxReceipt variant discrimination).
        const auto& receipt = result.receipts[i];
        const bool isDeposit = (result.txTypes[i] == static_cast<uint8_t>(kDepositTxType));
        const auto& meta = receipt->opStackMeta();
        std::optional<std::string> gotDepNonce, gotDepVersion, gotL1Fee, gotOperatorFee,
            gotDaFootprint, gotL1GasPrice, gotL1BlobBaseFee, gotL1GasUsed, gotL1BaseFeeScalar,
            gotL1BlobBaseFeeScalar, gotOpFeeScalar, gotOpFeeConstant, gotDaFootprintGasScalar;
        if (isDeposit)
        {
            if (meta && meta->deposit_nonce.has_value())
                gotDepNonce = hexU64(*meta->deposit_nonce);
            if (meta && meta->deposit_receipt_version.has_value())
                gotDepVersion = hexU64(*meta->deposit_receipt_version);
        }
        else
        {
            if (meta && meta->l1_fee.has_value())
                gotL1Fee = hexU256Bcos(*meta->l1_fee);
            // _op_operator_fee presence mirrors op-geth deriveOPStackFields (slot 8
            // scalar/constant not emitted when all-zero); on this side the same rule
            // rides meta.operator_fee_scalar/constant (deriveOpReceiptMeta only fills
            // when non-zero). meta.operator_fee itself is a FISCO extension always
            // filled on Isthmus+ (incl. 0); comparing it directly would report a
            // representation difference as a divergence.
            if (meta &&
                (meta->operator_fee_scalar.has_value() || meta->operator_fee_constant.has_value()))
                gotOperatorFee = hexU256Bcos(meta->operator_fee.value_or(bcos::u256{0}));
            if (meta && meta->da_footprint.has_value())
                gotDaFootprint = hexU64(*meta->da_footprint);
            // Full-field compare across the ecotone/fjord/granite/holocene/isthmus/jovian
            // fieldmap: u256 -> hexU256Bcos, uint64 -> hexU64. got-reads live inside the
            // non-deposit branch (mirrors generator !IsDepositTx emission: deposit receipts
            // carry no fee fields on either side; ungated reads would false-diverge).
            // operator-fee absent pre-Isthmus: ecotone..holocene has_operator_fee=false ->
            // deriveOpReceiptMeta fills no operator_fee* -> all got nullopt; generator does
            // not emit _op_operator_fee pre-Isthmus -> optWant also nullopt -> both-absent
            // pass (no false divergence). l1_gas_used kept unconditionally — even when the
            // vector lacks the key (optWant nullopt), assert FISCO-side presence
            // (Task 4 recomputation; Fjord+ always emits, Ecotone uses bedrockCalldataGasUsed).
            if (meta && meta->l1_gas_price.has_value())
                gotL1GasPrice = hexU256Bcos(*meta->l1_gas_price);
            if (meta && meta->l1_blob_base_fee.has_value())
                gotL1BlobBaseFee = hexU256Bcos(*meta->l1_blob_base_fee);
            if (meta && meta->l1_gas_used.has_value())
                gotL1GasUsed = hexU64(*meta->l1_gas_used);
            if (meta && meta->l1_base_fee_scalar.has_value())
                gotL1BaseFeeScalar = hexU64(*meta->l1_base_fee_scalar);
            if (meta && meta->l1_blob_base_fee_scalar.has_value())
                gotL1BlobBaseFeeScalar = hexU64(*meta->l1_blob_base_fee_scalar);
            if (meta && meta->operator_fee_scalar.has_value())
                gotOpFeeScalar = hexU64(*meta->operator_fee_scalar);
            if (meta && meta->operator_fee_constant.has_value())
                gotOpFeeConstant = hexU64(*meta->operator_fee_constant);
            if (meta && meta->da_footprint_gas_scalar.has_value())
                gotDaFootprintGasScalar = hexU64(*meta->da_footprint_gas_scalar);
        }

        ctx.checkField(p + ".type", hexU256(parseU256(jAt(er, "type"))),
            hexU64(static_cast<uint64_t>(result.txTypes[i])));
        ctx.checkField(p + ".status", hexU256(parseU256(jAt(er, "status"))),
            receipt->status() == 0 ? "0x1" : "0x0");
        ctx.checkField(p + ".gasUsed", hexU256(parseU256(jAt(er, "gasUsed"))),
            hexU64(static_cast<uint64_t>(receipt->gasUsed())));
        // Tier-2 Phase B: the stored field is DECIMAL (tars convention, RPC lexical_cast
        // semantics); the generator's golden is a hex quantity — compare by VALUE.
        ctx.checkField(p + ".cumulativeGasUsed", hexU256(parseU256(jAt(er, "cumulativeGasUsed"))),
            hexU256(parseU256(std::string{receipt->cumulativeGasUsed()})));
        ctx.checkField(p + ".logsCount", std::to_string(jAt(er, "logsCount").asInt64()),
            std::to_string(receipt->logEntries().size()));
        // Receipt output (tx return data): the generator always emits it (empty = "0x");
        // FISCO output() returns raw bytes, normalized to "0x"+lowercase hex by hexBytes.
        // Both-absent/both-present byte-exact compare — wrapper returndata truncation and
        // p256 32-byte-1 are both pinned by it.
        ctx.checkOptional(p + ".output",
            er.isMember("output") ? std::optional{jAt(er, "output").asString()} : std::nullopt,
            std::optional{
                hexBytes(evmc::bytes_view{receipt->output().data(), receipt->output().size()})});

        const auto optWant = [&](const char* key) -> std::optional<std::string> {
            return er.isMember(key) ? std::optional{hexU256(parseU256(jAt(er, key)))} :
                                      std::nullopt;
        };
        ctx.checkOptional(p + "._op_deposit_nonce", optWant("_op_deposit_nonce"), gotDepNonce);
        ctx.checkOptional(p + "._op_deposit_receipt_version",
            optWant("_op_deposit_receipt_version"), gotDepVersion);
        ctx.checkOptional(p + "._op_l1_fee", optWant("_op_l1_fee"), gotL1Fee);
        ctx.checkOptional(p + "._op_operator_fee", optWant("_op_operator_fee"), gotOperatorFee);
        ctx.checkOptional(p + "._op_da_footprint", optWant("_op_da_footprint"), gotDaFootprint);
        ctx.checkOptional(p + "._op_l1_gas_price", optWant("_op_l1_gas_price"), gotL1GasPrice);
        ctx.checkOptional(
            p + "._op_l1_blob_base_fee", optWant("_op_l1_blob_base_fee"), gotL1BlobBaseFee);
        ctx.checkOptional(p + "._op_l1_gas_used", optWant("_op_l1_gas_used"), gotL1GasUsed);
        ctx.checkOptional(
            p + "._op_l1_base_fee_scalar", optWant("_op_l1_base_fee_scalar"), gotL1BaseFeeScalar);
        ctx.checkOptional(p + "._op_l1_blob_base_fee_scalar",
            optWant("_op_l1_blob_base_fee_scalar"), gotL1BlobBaseFeeScalar);
        ctx.checkOptional(
            p + "._op_operator_fee_scalar", optWant("_op_operator_fee_scalar"), gotOpFeeScalar);
        ctx.checkOptional(p + "._op_operator_fee_constant", optWant("_op_operator_fee_constant"),
            gotOpFeeConstant);
        ctx.checkOptional(p + "._op_da_footprint_gas_scalar",
            optWant("_op_da_footprint_gas_scalar"), gotDaFootprintGasScalar);
    }

    // ── postState bidirectional (decision record 8) ───────────────────────────
    // Skipped wholesale when ReplayOptions.requirePostState=false (live chain
    // vectors carry no postState; full-state equality is proven by the stateRoot
    // field compare above).
    if (ledger.opts.requirePostState)
    {
        // Forward: vector per-account per-slot vs replay final state. Zero slots/accounts
        // reduced per trie semantics (0 == absent): the vector emits "candidate accounts
        // absent after the block" as {"balance":"0x0"} — the compare is "all four fields
        // zero + all slots zero", and a missing got-side account is treated as a zero
        // account, so identity => pass. A present-but-empty got account also passes
        // (EIP-161 empty account == absent from trie).
        const auto& post = jAt(blk, "postState");
        std::set<evmc::address> postAddrs;
        for (const auto& addrStr : post.getMemberNames())
        {
            const auto& acc = post[addrStr];
            const auto addr = test::from_json<evmc::address>(Json::Value(addrStr));
            postAddrs.insert(addr);
            const auto ap = "postState." + hexAddr(addr);

            const auto gotAcc = backend.get_account(addr);
            ctx.checkField(ap + ".balance", hexU256(parseU256(jAt(acc, "balance"))),
                gotAcc.has_value() ? hexU256(gotAcc->balance) : "0x0");
            ctx.checkField(ap + ".nonce",
                hexU64(acc.isMember("nonce") ? test::from_json<uint64_t>(jAt(acc, "nonce")) : 0),
                hexU64(gotAcc.has_value() ? gotAcc->nonce : 0));
            ctx.checkField(ap + ".code",
                acc.isMember("code") ? hexBytes(test::from_json<bytes>(jAt(acc, "code"))) : "0x",
                hexBytes(backend.get_account_code(addr)));

            // Slot union = vector-declared slots ∪ got non-zero slots ∪ replay write-set
            // touched slots (slot dimension of coverage assertion (ii): a touched slot is
            // always in the union and explicitly compared — final non-zero while unlisted
            // by the vector => want=0x0 turns red; final zero while unlisted => 0==absent
            // both-zero pass, i.e. "covered").
            std::map<evmc::bytes32, intx::uint256> wantStorage;
            if (acc.isMember("storage"))
            {
                const auto& storage = acc["storage"];
                for (const auto& slotStr : storage.getMemberNames())
                    wantStorage[test::from_json<hash256>(Json::Value(slotStr))] =
                        parseU256(storage[slotStr]);
            }
            std::set<evmc::bytes32> slots;
            for (const auto& [k, val] : wantStorage)
                slots.insert(k);
            const auto gotStorage = backend.getAccountStorage(addr);
            for (const auto& [k, val] : gotStorage)
                slots.insert(k);
            if (const auto tIt = touchedSlots.find(addr); tIt != touchedSlots.end())
                slots.insert(tIt->second.begin(), tIt->second.end());
            for (const auto& slot : slots)
            {
                const auto wIt = wantStorage.find(slot);
                const auto want = wIt != wantStorage.end() ? wIt->second : intx::uint256{0};
                const auto gIt = gotStorage.find(slot);
                const auto gotVal = gIt != gotStorage.end() ?
                                        intx::be::load<intx::uint256>(gIt->second) :
                                        intx::uint256{0};
                ctx.checkField(ap + ".storage." + hexSlot(slot), hexU256(want), hexU256(gotVal));
            }
        }
        // Reverse existence: a non-empty account in the replay final state not listed by
        // the vector = DIVERGE (the vector candidate set claims coverage of all written
        // accounts; empty accounts == absent from trie, reduced to pass).
        backend.visitAccounts([&](const AccountView& account) {
            if (postAddrs.contains(account.addr))
                return true;
            const bool storageAllZero = std::ranges::all_of(
                account.storage, [](const auto& kv) { return evmc::is_zero(kv.second); });
            if (account.nonce != 0 || account.balance != 0 ||
                !backend.get_account_code(account.addr).empty() || !storageAllZero)
                ledger.diverge(
                    id, "postState." + hexAddr(account.addr) + ".exists", kAbsent, "<present>");
            return true;
        });
        // Coverage assertion (ii) address dimension: an address touched by replay applyDiff
        // but not listed in the vector postState = DIVERGE .uncovered (an account touched
        // then deleted is emitted by the generator as {"balance":"0x0"} and stays in
        // postAddrs — absent means the corpus candidate set missed an account).
        for (const auto& addr : touchedAddrs)
        {
            if (!postAddrs.contains(addr))
                ledger.diverge(
                    id, "postState." + hexAddr(addr) + ".uncovered", "<covered>", "<uncovered>");
        }
    }

    // Per-vector comparison count: 0 = FAILURE (prevents a vacuous green).
    if (ctx.comparisons == 0)
        OP_REPLAY_FAIL(id << ": zero comparisons executed");
}

// ── Chain replay ─────────────────────────────────────────────────────────────
// blocks[0] seeds pre; blocks[i>0] with pre:null uses the previous block's
// post-state (backend state after applyDiff write-back). The backend is passed
// by reference across blocks (blocks[i>0] skip pre parsing — pre:null would trip
// from_json's is_object assert, hence the nullptr pre pointer). ParentOnlyBlockHashes
// only answers number-1 (see above), so corpus chain vectors must not contain txs
// that read historical blockhashes (transfer-safe; review R13) — the live path
// pre-fills ReplayOptions.blockHashes instead.

inline void replayChainVector(const std::string& id, const JsonValue& v, DivergenceLedger& ledger,
    evmc::VM& vm, const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory,
    StateBackend& backend)
{
    const auto& blocks = jAt(v, "blocks");
    for (std::size_t i = 0; i < blocks.size(); ++i)
    {
        const auto& blk = blocks[static_cast<Json::ArrayIndex>(i)];
        const JsonValue* pre = nullptr;
        if (blk.isMember("pre") && !blk["pre"].isNull())
        {
            backend.loadPre(blk["pre"]);
            pre = &blk["pre"];
        }
        replaySingleBlockInto(
            id + "[" + std::to_string(i) + "]", blk, backend, pre, ledger, vm, receiptFactory);
    }
}

}  // namespace op_replay
#endif  // OP_REPLAY_GATE_H
