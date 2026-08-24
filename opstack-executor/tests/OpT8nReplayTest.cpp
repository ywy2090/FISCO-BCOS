// OpT8nReplayTest.cpp — OP block-level differential replay gate (corpus side).
//
// Replays test/opstack/t8n/vectors/*.json (schema v3-block, op-geth
// GenerateChain+InsertChain golden, generator in t8n/generator/) block-by-block
// through processOpBlock -> sealOpBlock, comparing header fields, per-receipt
// fields, and postState (bidirectional + applyDiff write-set coverage) against
// _op_expected.
//
// The replay machinery itself lives in support/ReplayGate.h (namespace op_replay,
// boost-free, generalized over a StateBackend); this TU defines OP_REPLAY_BOOST
// so failures route through BOOST_ERROR, keeps the corpus-specific drivers
// (replayVector / reject branch / the test cases) and adds `using namespace
// op_replay` for the moved helpers.
//
// Hard assertion discipline: A) dir *.json set == manifest.txt set; parse
// failure / missing required field = named ADD_FAILURE; per-vector comparison
// count recorded, 0 = FAILURE. B) required fields via jAt(); hardfork must be
// exactly ecotone|fjord|granite|holocene|isthmus|jovian (no default fork);
// unknown _op_type / receipt count mismatch = FAILURE (no zip-min).
// D) comparisons routed through checkField/checkOptional into DivergenceLedger;
// checkOptional never gated on has_value() (one-sided absence = DIVERGE
// <absent>); bloom always 512 hex; postState bidirectional with zero-slot trie
// reduction (0 == absent) + write-set coverage. E) exemptions only from
// DIVERGENCES.md ALLOWLIST tuples (a:PENDING-FIX / c:SIGNED-OFF); dangling
// entry= or never-hit exemptions = FAILURE.

#define OP_REPLAY_BOOST
#include "support/ReplayGate.h"

#include "StateDiffWriteback.h"
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
#include <opstack-executor/OpBlockExecute.h>  // seal (merged into the block-execution module)
#include <opstack-executor/OpSchedulerSeam.h>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <bcos-evm/eth/state/hash_utils.hpp>
#include <evmone_precompiles/secp256k1.hpp>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <test/utils/rlp.hpp>
#include <test/utils/test_state.hpp>
#include <vector>

namespace fs = std::filesystem;
using namespace bcos::evm::opstack;
using namespace evmone;
using namespace op_replay;

// ── Single-vector replay (flat vector: pre at top level; chain vectors call replayChainVector per
// block) ─

void replayVector(const std::string& id, const JsonValue& v, DivergenceLedger& ledger, evmc::VM& vm,
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory)
{
    evmone::test::TestState ts;
    TestStateBackend backend{ts};
    replaySingleBlockInto(id, v, backend, &jAt(v, "pre"), ledger, vm, receiptFactory);
}

// ── reject branch ────────────────────────────────────────────────────────────
// Top-level _op_expected.reject present -> invalid vector. The T8n side only
// consumes executor/both (engine direct-connect field-corruption classes are
// consumed by OpNewPayloadRpcE2eTest, Task 2).

bool hasReject(const JsonValue& v)
{
    return v.isMember("_op_expected") && jAt(v, "_op_expected").isMember("reject");
}

std::string rejectConsumer(const JsonValue& v)
{
    // consumer defaults to executor (T8n is an execution-layer replayer; engine-class vectors
    // explicitly write engine).
    return jAt(jAt(jAt(v, "_op_expected"), "reject"), "fisco")
        .get("consumer", Json::Value("executor"))
        .asString();
}

/// reject(executor/both) assertion: processOpBlock must throw std::runtime_error whose
/// what() contains the expected substring. Reuses loadBlockContext loading
/// (env->BlockInfo / ParentOnlyBlockHashes / transactions->OpBlockTx) but skips the
/// success-execution assertion path (no header/receipts/postState compares).
/// The throw side is verified catchable by typed catch: OpSchedulerSeamSmokeTest.cpp:161
/// catches processOpBlock's empty-block rejection via BOOST_CHECK_THROW(..., std::runtime_error)
/// and is green (FISCO-side throws use libc++ unique typeinfo, not the libevmone
/// -fno-rtti hidden copy).
void assertRejectThrow(const std::string& id, const JsonValue& v, evmc::VM& vm,
    const bcos::protocol::TransactionReceiptFactory::Ptr& receiptFactory)
{
    BlockContext bc;
    if (!loadBlockContext(id, v, bc))
        return;
    // decode-class reject (blob): the load section already reproduced the type-byte-classification
    // rejection and recorded the message. processOpBlock never reaches that decode (txs
    // are already OpBlockTx); assert the recorded message directly (same string as the engine
    // side).
    if (bc.decodeRejectMessage.has_value())
    {
        const auto expected =
            jAt(jAt(jAt(jAt(v, "_op_expected"), "reject"), "fisco"), "validation_error_contains")
                .asString();
        BOOST_CHECK_MESSAGE(bc.decodeRejectMessage->find(expected) != std::string::npos,
            id << ": decode reject message missing '" << expected
               << "', got: " << *bc.decodeRejectMessage);
        return;
    }
    evmone::test::TestState ts = test::from_json<test::TestState>(jAt(v, "pre"));
    // applyDiff callback matches the replaySingleBlockInto execute section (incl. ts write-back);
    // ts is discarded after reject.
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
        bcos::evm::applyStateDiffStrict(ts, d);
    };
    try
    {
        // Warning: only an invalid tx inserted after the first deposit throws "invalid
        //    non-deposit tx"; a deposit-only block does not throw (legal execution) —
        //    the vector must contain an invalid transaction.
        processOpBlock(
            ts, bc.blk, bc.hashes, bc.txs, *bc.cfg, vm, bc.chainId, receiptFactory, apply);
    }
    catch (const std::runtime_error& e)
    {
        const auto expected =
            jAt(jAt(jAt(jAt(v, "_op_expected"), "reject"), "fisco"), "validation_error_contains")
                .asString();
        BOOST_CHECK_MESSAGE(std::string(e.what()).find(expected) != std::string::npos,
            id << ": throw message missing '" << expected << "', got: " << e.what());
        return;
    }
    catch (...)
    {
        // typed-catch RTTI fallback (mechanism in replaySingleBlockInto's typed-catch
        // comment) — name the dynamic type, then finish with typed-branch semantics.
        const auto* excType = abi::__cxa_current_exception_type();
        BOOST_ERROR(id << ": threw non-runtime_error (typed catch bypassed, exception type: "
                       << (excType ? excType->name() : "<unknown>") << ")");
        return;
    }
    BOOST_ERROR(id << ": expected processOpBlock to reject, but it executed");
}

// ── structurallyUnrecoverable predicate boundary unit test (prevents the predicate degenerating to
// always-true/always-false and letting marked tuples escape) ──
BOOST_AUTO_TEST_SUITE(OpT8nReplay)

BOOST_AUTO_TEST_CASE(StructurallyUnrecoverablePredicateBoundaries)
{
    auto mk = [](uint64_t v, const intx::uint256& r, const intx::uint256& s) {
        evmone::state::Authorization a{};
        a.v = v;
        a.r = r;
        a.s = s;
        return a;
    };
    const intx::uint256 one{1};
    BOOST_CHECK(!structurallyUnrecoverable(mk(0, one, one)));
    BOOST_CHECK(!structurallyUnrecoverable(mk(1, one, kSecpHalfN)));       // s == N/2 is legal
    BOOST_CHECK(structurallyUnrecoverable(mk(2, one, one)));               // v > 1
    BOOST_CHECK(structurallyUnrecoverable(mk(0, one, kSecpHalfN + 1)));    // s > N/2
    BOOST_CHECK(structurallyUnrecoverable(mk(0, intx::uint256{0}, one)));  // r == 0
    BOOST_CHECK(!structurallyUnrecoverable(mk(0, kSecpN - 1, one)));
    BOOST_CHECK(structurallyUnrecoverable(mk(0, kSecpN, one)));            // r >= N
    BOOST_CHECK(structurallyUnrecoverable(mk(0, one, intx::uint256{0})));  // s == 0
}

BOOST_AUTO_TEST_CASE(Vectors)
{
    const fs::path vectorsDir = OP_T8N_VECTORS_DIR;
    BOOST_REQUIRE_MESSAGE(fs::is_directory(vectorsDir), vectorsDir);

    // A) Set equality: dir *.json filename set == manifest.txt list (missing or extra = FAILURE).
    const auto manifest = loadManifest(vectorsDir / "manifest.txt");
    BOOST_REQUIRE_MESSAGE(!manifest.empty(), "empty manifest");
    std::set<std::string> present;
    for (const auto& entry : fs::directory_iterator(vectorsDir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
            present.insert(entry.path().filename().string());
    }
    for (const auto& name : manifest)
    {
        if (!present.contains(name))
            BOOST_ERROR("manifest lists " << name << " but file is missing");
    }
    // Exclusions (forced): expectedBlobVersionedHashes / executionRequests cannot
    // be expressed through the GoldenSample loader, so the generator still emits files
    // but keeps them out of the manifest. Set equality exempts these two known
    // unregistered static-face files (suffix match, base-independent).
    const auto isUnregisteredStatic = [](std::string const& n) {
        // "_static_3.json" = 14 chars, "_static_12.json" = 15 chars (suffix match,
        // base-independent)
        return (n.size() >= 14 && n.rfind("_static_3.json") == n.size() - 14) ||
               (n.size() >= 15 && n.rfind("_static_12.json") == n.size() - 15);
    };
    for (const auto& name : present)
    {
        if (!manifest.contains(name) && !isUnregisteredStatic(name))
            BOOST_ERROR("unmanifested vector file present: " << name);
    }

    auto ledger = DivergenceLedger::load(vectorsDir / "DIVERGENCES.md");
    auto vm = evmc::VM{evmc_create_evmone()};
    auto receiptFactory = makeTestReceiptFactory();

    // Replay only the set intersection (missing/extra already FAILURE'd; don't let set errors
    // cascade into parse crashes).
    for (const auto& name : manifest)
    {
        if (!present.contains(name))
            continue;
        const auto file = vectorsDir / name;
        // parse failure / missing required field -> named ADD_FAILURE, then next file; never
        // silent.
        try
        {
            std::ifstream input(file);
            const auto doc = jParse(input);
            std::string id;
            const JsonValue* vec = nullptr;
            for (const auto& key : doc.getMemberNames())
            {
                if (key == "_op_test_vectors")
                    continue;
                if (vec != nullptr)
                    throw std::runtime_error("more than one vector object in file");
                id = key;
                vec = &doc[key];
            }
            if (vec == nullptr)
                throw std::runtime_error("no vector object in file");
            const auto stem = file.stem().string();
            if (id != stem)
                throw std::runtime_error("vector id '" + id + "' != filename stem '" + stem + "'");
            // Warning — order: chain vectors have only blocks at top level, no
            // _op_expected, so the reject check (jAt(v, "_op_expected")) would throw
            // invalid_argument; must test blocks first.
            if (vec->isMember("blocks"))
            {
                evmone::test::TestState chainState;
                TestStateBackend backend{chainState};
                replayChainVector(id, *vec, ledger, vm, receiptFactory, backend);
                continue;
            }
            if (hasReject(*vec))
            {
                const auto consumer = rejectConsumer(*vec);
                if (consumer == "engine")
                    continue;  // field-corruption class: OpNewPayloadRpcE2eTest only
                assertRejectThrow(id, *vec, vm, receiptFactory);  // executor/both
                continue;
            }
            replayVector(id, *vec, ledger, vm, receiptFactory);
        }
        catch (const std::exception& e)
        {
            BOOST_ERROR(name << ": " << e.what());
        }
        catch (...)
        {
            // typed-catch RTTI fallback (same mechanism as above) — name the type,
            // same semantics as the typed branch (record FAILURE then next vector file).
            const auto* excType = abi::__cxa_current_exception_type();
            BOOST_ERROR(name << ": exception escaped typed catch (exception type: "
                             << (excType ? excType->name() : "<unknown>") << ")");
        }
    }

    // E) End-of-run ledger: stale exemptions turn red + KNOWN-DIVERGE total into RecordProperty.
    ledger.finish();
}

// ── reject branch: processOpBlock must throw std::runtime_error
//    ("op block: invalid non-deposit tx: ...", OpBlockExecute.cpp:190) for an invalid
//    non-deposit tx; assert throw + what() substring. Inline vector
//    (not a corpus file, embedded directly here) — fields must satisfy loader
//    requirements: deposit data at the tx top level (jAt(t, "data")); from=OP_DEPOSITOR
//    to=OP_L1_BLOCK (OpPredeploys.h / OpBlockExecute.cpp); mint/value/gas as hex strings.
//    The second eip1559 tx must carry a real signed EIP-2718 _op_raw envelope
//    (opValidate forces non-empty signedTxEnvelope, OpTransition.cpp:362); gas=0 ->
//    validate_transaction throws INTRINSIC_GAS_TOO_LOW (eth/state/errors.hpp:51) ->
//    processOpBlock throws "op block: invalid non-deposit tx: intrinsic gas too low".
BOOST_AUTO_TEST_CASE(RejectExecutorSurface)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    auto receiptFactory = makeTestReceiptFactory();
    // Warning: DivergenceLedger is default-constructed; load("") would BOOST_ERROR
    // ("DIVERGENCES.md missing"). assertRejectThrow does not consume the ledger
    // (reject vectors have no postState/exemption compares); kept for brief conformance.
    DivergenceLedger ledger;
    // Hand-built vector uses the jsoncpp Reader on a raw string (a Json::Value
    // initializer-list tree would fight jsoncpp's aggregate Value constructors).
    JsonValue v = jParse(R"({
        "_info": {"hardfork": "isthmus"},
        "env": {
            "currentNumber": 1, "currentTimestamp": "0x64",
            "currentGasLimit": "0x989680", "currentBaseFee": "0x3b9aca00",
            "currentCoinbase": "0x0000000000000000000000000000000000000000",
            "currentRandom": "0x0000000000000000000000000000000000000000000000000000000000000000",
            "parentBeaconBlockRoot": "0x0000000000000000000000000000000000000000000000000000000000000000",
            "parentHash": "0x0000000000000000000000000000000000000000000000000000000000000000"
        },
        "pre": {
            "0x0000000000000000000000000000000000000001": {
                "balance": "0xde0b6b3a7640000", "nonce": "0x0", "code": "0x"
            }
        },
        "block": {
            "transactions": [
                {
                    "_op_type": "deposit",
                    "_op_deposit": {
                        "source_hash": "0x0000000000000000000000000000000000000000000000000000000000000000",
                        "from": "0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001",
                        "to": "0x4200000000000000000000000000000000000015",
                        "mint": "0x0", "value": "0x0", "gas": "0x186a0",
                        "is_system_tx": false
                    },
                    "data": "0x"
                },
                {
                    "_op_type": "eip1559",
                    "_op_raw": "0x02f874822105808405f5e100847735940082520894b0b0000000000000000000000000000000000001880de0b6b3a764000080c001a0e37533ddb9f696c0b21788f1b00c78adc4a81b1d811d84e70fad672096fc924ea00ae693f4d68955a4c01ee8bab26f5be740ee416dd2556822f68b747d5aab7714",
                    "chainId": "0x2105", "nonce": "0x0",
                    "to": "0xb0b0000000000000000000000000000000000001",
                    "gas": "0x0", "maxFeePerGas": "0x77359400", "maxPriorityFeePerGas": "0x5f5e100",
                    "value": "0x0", "data": "0x",
                    "sender": "0x7e5f4552091a69125d5dfcb7b8c2659029395bdf"
                }
            ]
        },
        "_op_expected": {
            "reject": {
                "op_geth": "intrinsic gas too low",
                "fisco": {
                    "consumer": "executor", "classification": "INVALID",
                    "latest_valid_hash": "parent",
                    "validation_error_contains": "invalid non-deposit tx"
                }
            }
        }
    })");
    assertRejectThrow("reject_executor_intrinsic", v, vm, receiptFactory);
}

// ── blob decode-class reject (consumer:both) ─────────────────────────
// The blob arm reproduces the real rejection via the type-byte classification
// (processOpBlock never reaches raw-tx decode); the message is
// "op block: unsupported tx type byte 0x03" (runOpBlockInjection / OpScheduler execute hook).
// _op_raw only needs the type-0x03 first byte to hit the classification branch (it checks the
// type byte before parsing any field).
BOOST_AUTO_TEST_CASE(RejectBlobDecode)
{
    auto vm = evmc::VM{evmc_create_evmone()};
    auto receiptFactory = makeTestReceiptFactory();
    DivergenceLedger ledger;
    JsonValue v = jParse(R"({
        "_info": {"hardfork": "isthmus"},
        "env": {
            "currentNumber": 1, "currentTimestamp": "0x64",
            "currentGasLimit": "0x989680", "currentBaseFee": "0x3b9aca00",
            "currentCoinbase": "0x0000000000000000000000000000000000000000",
            "currentRandom": "0x0000000000000000000000000000000000000000000000000000000000000000",
            "parentBeaconBlockRoot": "0x0000000000000000000000000000000000000000000000000000000000000000",
            "parentHash": "0x0000000000000000000000000000000000000000000000000000000000000000"
        },
        "pre": {
            "0x0000000000000000000000000000000000000001": {
                "balance": "0xde0b6b3a7640000", "nonce": "0x0", "code": "0x"
            }
        },
        "block": {
            "transactions": [
                {
                    "_op_type": "deposit",
                    "_op_deposit": {
                        "source_hash": "0x0000000000000000000000000000000000000000000000000000000000000000",
                        "from": "0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001",
                        "to": "0x4200000000000000000000000000000000000015",
                        "mint": "0x0", "value": "0x0", "gas": "0x186a0",
                        "is_system_tx": false
                    },
                    "data": "0x"
                },
                {
                    "_op_type": "blob",
                    "_op_raw": "0x03",
                    "chainId": "0x2105", "nonce": "0x0",
                    "to": "0xb0b0000000000000000000000000000000000001",
                    "gas": "0x186a0", "maxFeePerGas": "0x77359400", "maxPriorityFeePerGas": "0x5f5e100",
                    "value": "0x0", "data": "0x",
                    "sender": "0x7e5f4552091a69125d5dfcb7b8c2659029395bdf"
                }
            ]
        },
        "_op_expected": {
            "reject": {
                "op_geth": "data blobs present in block body",
                "fisco": {
                    "consumer": "both", "classification": "INVALID",
                    "latest_valid_hash": "parent",
                    "validation_error_contains": "unsupported tx type byte 0x03"
                }
            }
        }
    })");
    assertRejectThrow("reject_blob_decode", v, vm, receiptFactory);
}

// ── legacy arm loading ───────────────────────────────────────────
// Verifies _op_type "legacy" loads a type=legacy tx and fills gasPrice into both
// max/priority (evmone legacy single-price semantics). _op_raw is structural
// placeholder only (load section does not decode); the full golden path is covered
// by the corpus isthmus/jovian_legacy_transfer vectors (replayed in full after regen).
BOOST_AUTO_TEST_CASE(LegacyArmBuildsLegacyTx)
{
    JsonValue v = jParse(R"({
        "_info": {"hardfork": "isthmus"},
        "env": {
            "currentNumber": 1, "currentTimestamp": "0x64",
            "currentGasLimit": "0x989680", "currentBaseFee": "0x3b9aca00",
            "currentCoinbase": "0x0000000000000000000000000000000000000000",
            "currentRandom": "0x0000000000000000000000000000000000000000000000000000000000000000",
            "parentBeaconBlockRoot": "0x0000000000000000000000000000000000000000000000000000000000000000",
            "parentHash": "0x0000000000000000000000000000000000000000000000000000000000000000"
        },
        "pre": {
            "0x0000000000000000000000000000000000000001": {
                "balance": "0xde0b6b3a7640000", "nonce": "0x0", "code": "0x"
            }
        },
        "block": {
            "transactions": [
                {
                    "_op_type": "deposit",
                    "_op_deposit": {
                        "source_hash": "0x0000000000000000000000000000000000000000000000000000000000000000",
                        "from": "0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001",
                        "to": "0x4200000000000000000000000000000000000015",
                        "mint": "0x0", "value": "0x0", "gas": "0x186a0",
                        "is_system_tx": false
                    },
                    "data": "0x"
                },
                {
                    "_op_type": "legacy",
                    "_op_raw": "0x01",
                    "chainId": "0x2105", "nonce": "0x0",
                    "to": "0xb0b0000000000000000000000000000000000001",
                    "gas": "0x5208", "gasPrice": "0x4a817c800",
                    "value": "0xde0b6b3a7640000", "data": "0x",
                    "sender": "0x7e5f4552091a69125d5dfcb7b8c2659029395bdf"
                }
            ]
        }
    })");
    BlockContext bc;
    BOOST_REQUIRE_MESSAGE(loadBlockContext("legacy_arm_load", v, bc), "legacy vector must load");
    BOOST_REQUIRE_EQUAL(bc.txs.size(), 2u);
    const auto* tx = std::get_if<state::Transaction>(&bc.txs[1].tx);
    BOOST_REQUIRE_MESSAGE(tx != nullptr, "tx[1] must be a normal tx");
    BOOST_CHECK(tx->type == state::Transaction::Type::legacy);
    BOOST_CHECK_EQUAL(tx->chain_id, uint64_t{0x2105});
    // legacy single-price: max == priority == gasPrice
    BOOST_CHECK(tx->max_gas_price == tx->max_priority_gas_price);
    BOOST_CHECK(tx->max_gas_price == parseU256(jParse("\"0x4a817c800\"")));
    BOOST_CHECK(bc.txs[1].signedEnvelope.size() > 0);
}

// Ported from op-alignment (audit O3): the persisted golden transactionsRoot must equal
// computeOpTxRoot over the golden's rawTransactions (raw EIP-2718 envelopes incl. the 0x7E
// deposit). Without this cross-check the golden txsRoot is never compared against FISCO's own
// derivation. Pre-Isthmus goldens (ecotone/fjord/granite) and Isthmus+ are both covered.
BOOST_AUTO_TEST_CASE(GoldenTransactionsRootMatches)
{
    const fs::path goldenDir = OP_T8N_GOLDEN_ENGINE_DIR;
    BOOST_REQUIRE_MESSAGE(fs::is_directory(goldenDir), goldenDir);
    int checked = 0;
    for (const auto& entry : fs::directory_iterator(goldenDir))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
            continue;
        Json::Value j;
        {
            std::ifstream in(entry.path());
            in >> j;
        }
        if (!j.isMember("rawTransactions") || !j.isMember("transactionsRoot"))
            continue;
        std::vector<bcos::bytes> raws;
        for (auto const& rt : j["rawTransactions"])
            raws.push_back(bcos::fromHexWithPrefix(rt.asString()));
        auto root = bcos::evm::engine::computeOpTxRoot(raws);
        auto golden = bcos::h256(j["transactionsRoot"].asString());
        BOOST_CHECK_MESSAGE(root == golden,
            entry.path().filename() << ": txsRoot mismatch computed=" << root.hexPrefixed()
                                    << " golden=" << j["transactionsRoot"].asString());
        ++checked;
    }
    BOOST_CHECK_MESSAGE(checked >= 16, "expected >=16 golden txsRoot checks, got " << checked);
}

BOOST_AUTO_TEST_SUITE_END()
