// opstack-mainnet-replay — chain-vector replay CLI (shares ReplayGate.h with the
// corpus gate OpT8nReplayTest.cpp).
//
// Usage:
//   opstack-mainnet-replay --chain <chain.json> [--sidecar <state.sidecar>]
//       [--id <vector-id>] [--report <out.json>] [--allowlist <allowlist.json>]
//       [--skip-poststate] [--rocks <dir>] [--chain-id <id>] [--check-import-root <root>]
//
// Exit code: 0 = all green (incl. exemptions); 1 = divergence/import-root mismatch;
// 2 = argument error (explicit, never thrown — the run_fisco lesson).
//
// State backend selection: with --sidecar the RocksDB backend is bootstrapped from the
// dump sidecar (live-chain path, per-block `pre` is null); without it an in-memory
// TestStateBackend replays the corpus chain vectors (block-0 `pre` seeds the state).
// With --check-import-root <root> the imported state's rebuilt MPT root must equal
// <root> (sidecar corruption tripwire) — abort otherwise.
#include "tests/support/ReplayGate.h"
#include "tests/support/StateBackendRocksDB.h"
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <evmone/evmone.h>
#include <json/json.h>
#include <fstream>
#include <iostream>
#include <string>

using namespace op_replay;

namespace
{
bcos::protocol::TransactionReceiptFactory::Ptr makeReceiptFactory()
{
    return std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(
        std::make_shared<bcos::crypto::CryptoSuite>(
            std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr));
}

// Top-level "_op_block_hashes" (written by the --live generator): number -> hash
// pre-fill table for historical BLOCKHASH reads ([H0-256, H0-1]).
std::map<int64_t, evmc::bytes32> loadBlockHashes(const Json::Value& doc)
{
    std::map<int64_t, evmc::bytes32> out;
    if (!doc.isMember("_op_block_hashes"))
        return out;
    const auto& table = doc["_op_block_hashes"];
    for (const auto& numStr : table.getMemberNames())
    {
        const auto hash = evmc::from_hex<evmc::bytes32>(table[numStr].asString());
        if (hash.has_value())
            out[std::stoll(numStr)] = *hash;
    }
    return out;
}

// --allowlist JSON -> four-tuple exemption rows. Format (Task 7):
//   {"<vectorId>.<field>": [["<want>","<got>","STATUS"], ...]}
// Each row becomes a DivergenceLedger::addAllowEntry (PENDING-FIX / SIGNED-OFF
// exempt) injected into the matching vector's ledger before it replays.
struct AllowRow
{
    std::string vectorId, field, want, got, status;
};

std::vector<AllowRow> loadAllowlist(const std::string& path)
{
    std::vector<AllowRow> rows;
    std::ifstream input(path);
    if (!input.is_open())
        throw std::runtime_error("cannot open allowlist: " + path);
    Json::Value doc;
    Json::Reader reader;
    if (!reader.parse(input, doc))
        throw std::runtime_error("allowlist parse failed: " + reader.getFormattedErrorMessages());
    for (const auto& key : doc.getMemberNames())
    {
        const auto dot = key.find('.');
        if (dot == std::string::npos)
            throw std::runtime_error("allowlist key '" + key + "' must be '<vectorId>.<field>'");
        const std::string vid = key.substr(0, dot);
        const std::string field = key.substr(dot + 1);
        for (const auto& row : doc[key])
        {
            if (row.size() != 3)
                throw std::runtime_error(
                    "allowlist row for '" + key + "' must be [want, got, STATUS]");
            const auto status = row[2].asString();
            if (status != "PENDING-FIX" && status != "SIGNED-OFF")
                std::cerr << "allowlist row for '" << key << "': status '" << status
                          << "' is not PENDING-FIX|SIGNED-OFF — recorded without exempting\n";
            rows.push_back({vid, field, row[0].asString(), row[1].asString(), status});
        }
    }
    return rows;
}

// Parse --chain-id (base 0: decimal or 0x-hex). Reject empty/partial/negative:
// std::stoull("0x") silently yields 0 and "-1" wraps to UINT64_MAX.
bool parseChainId(const std::string& s, uint64_t& out)
{
    if (s.empty() || s[0] == '-')
        return false;
    std::size_t pos = 0;
    try
    {
        out = std::stoull(s, &pos, 0);
    }
    catch (...)
    {
        return false;
    }
    return pos > 0 && pos == s.size();
}
}  // namespace

int main(int argc, char** argv)
{
    std::string chainPath, sidecarPath, idFilter, reportPath, allowlistPath, rocksPath, expectRoot;
    bool skipPostState = false;
    uint64_t chainId = 11155420;  // D11: default Sepolia; --chain-id overrides
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--chain" && i + 1 < argc)
            chainPath = argv[++i];
        else if (a == "--sidecar" && i + 1 < argc)
            sidecarPath = argv[++i];
        else if (a == "--id" && i + 1 < argc)
            idFilter = argv[++i];
        else if (a == "--report" && i + 1 < argc)
            reportPath = argv[++i];
        else if (a == "--allowlist" && i + 1 < argc)
            allowlistPath = argv[++i];
        else if (a == "--rocks" && i + 1 < argc)
            rocksPath = argv[++i];
        else if (a == "--chain-id" && i + 1 < argc)
        {
            const std::string v = argv[++i];
            if (!parseChainId(v, chainId))
            {
                std::cerr << "bad --chain-id '" << v << "'\n";
                return 2;
            }
        }
        else if (a == "--check-import-root" && i + 1 < argc)
            expectRoot = argv[++i];
        else if (a == "--skip-poststate")
            skipPostState = true;
        else
        {
            std::cerr << "usage: opstack-mainnet-replay --chain <chain.json> "
                         "[--sidecar <state.sidecar>] [--id <vector-id>] [--report <out.json>] "
                         "[--allowlist <allowlist.json>] [--skip-poststate] [--rocks <dir>] "
                         "[--chain-id <id>] [--check-import-root <root>]\n";
            return 2;
        }
    }
    if (chainPath.empty())
    {
        std::cerr << "missing --chain\n";
        return 2;
    }
    if (!sidecarPath.empty() && rocksPath.empty())
    {
        std::cerr << "--sidecar requires --rocks <dir> (RocksDB state backend path)\n";
        return 2;
    }
    std::vector<AllowRow> allowRows;
    if (!allowlistPath.empty())
    {
        try
        {
            allowRows = loadAllowlist(allowlistPath);
        }
        catch (const std::exception& e)
        {
            std::cerr << "error: " << e.what() << "\n";
            return 2;
        }
    }

    try
    {
        replayReport().reset();
        std::ifstream input(chainPath);
        if (!input.is_open())
            throw std::runtime_error("cannot open chain file: " + chainPath);
        const auto doc = jParse(input);
        auto vm = evmc::VM{evmc_create_evmone()};
        auto receiptFactory = makeReceiptFactory();
        const auto blockHashes = loadBlockHashes(doc);

        // State backend: RocksDB (live, sidecar-bootstrapped) or in-memory (corpus).
        std::unique_ptr<StateBackendRocksDB> rocksBackend;
        std::unique_ptr<evmone::test::TestState> memState;
        std::unique_ptr<TestStateBackend> memBackend;
        StateBackend* backend = nullptr;
        if (!sidecarPath.empty())
        {
            rocksBackend = std::make_unique<StateBackendRocksDB>(rocksPath);
            rocksBackend->loadDumpSidecar(sidecarPath);
            // Automatic import tripwire: the rebuilt MPT root must equal the
            // sidecar's ROOT line (the generator wrote the real h0-1 root there).
            // --check-import-root is an explicit override for odd flows.
            const auto rebuilt = bcos::evm::stateRootOf(*rocksBackend);
            const auto wantRoot = expectRoot.empty() ?
                                      std::optional<evmc::bytes32>{rocksBackend->importRoot()} :
                                      evmc::from_hex<evmc::bytes32>(expectRoot);
            if (!wantRoot.has_value() || rebuilt != *wantRoot)
            {
                std::cerr << "import root mismatch: rebuilt=" << hexHash(rebuilt) << " expected="
                          << (expectRoot.empty() ? hexHash(rocksBackend->importRoot()) : expectRoot)
                          << "\n";
                return 1;
            }
            std::cout << "import root == ROOT (" << hexHash(rebuilt) << ")\n";
            backend = rocksBackend.get();
        }
        else
        {
            memState = std::make_unique<evmone::test::TestState>();
            memBackend = std::make_unique<TestStateBackend>(*memState);
            backend = memBackend.get();
        }

        bool any = false;
        for (const auto& key : doc.getMemberNames())
        {
            if (key == "_op_test_vectors" || key == "_op_block_hashes")
                continue;
            if (!idFilter.empty() && key != idFilter)
                continue;
            const auto& vec = doc[key];
            if (!vec.isMember("blocks"))
            {
                std::cerr << "vector '" << key << "' is not a chain vector\n";
                return 2;
            }
            DivergenceLedger ledger;
            ledger.opts.requirePostState = !skipPostState;
            ledger.opts.comparePostState = !skipPostState;
            ledger.opts.chainId = chainId;
            ledger.opts.blockHashes = blockHashes;
            for (const auto& row : allowRows)
                // Allowlist vectorIds carry the [i] block suffix ("vec[0].field");
                // inject only into the matching vector's ledger (the four-tuple
                // match in diverge() does the exact exemption).
                if (row.vectorId.rfind(key, 0) == 0)
                    ledger.addAllowEntry(row.vectorId, row.field, row.want, row.got, row.status);
            replayChainVector(key, vec, ledger, vm, receiptFactory, *backend);
            ledger.finish();
            any = true;
        }
        if (!any)
        {
            std::cerr << "no vector replayed\n";
            return 2;
        }

        if (!reportPath.empty())
        {
            Json::Value out;
            out["pass"] = (replayReport().failures == 0);
            out["failures"] = replayReport().failures;
            out["details"] = Json::arrayValue;
            for (const auto& d : replayReport().details)
                out["details"].append(d);
            std::ofstream os(reportPath);
            os << out.toStyledString();
        }
        // Divergence details are captured in ReplayReport (no OP_REPLAY_BOOST here); surface
        // them on stderr so a red run is diagnosable without opening the report file.
        for (const auto& d : replayReport().details)
            std::cerr << d << "\n";
        return replayReport().failures == 0 ? 0 : 1;
    }
    catch (const std::exception& e)
    {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    catch (...)
    {
        std::cerr << "error: unexpected\n";
        return 1;
    }
}
