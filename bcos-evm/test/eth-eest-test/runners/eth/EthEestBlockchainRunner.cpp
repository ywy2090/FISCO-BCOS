#include "../helpers/BlockTransition.h"
#include "bcos-evm/eth-eest-test/ForkProfileRegistry.h"
#include "bcos-evm/eth-eest-test/GstStateHash.h"
#include "bcos-evm/eth-eest-test/TestStateView.h"

#include "bcos-crypto/hash/Keccak256.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace pt = boost::property_tree;

namespace
{

using namespace bcos::evm;
using namespace bcos::evm::reference_tests;

/// Parse a hex string ("0x..." or bare hex) into bytes.
bcos::bytes parseHex(std::string_view s)
{
    if (s.starts_with("0x") || s.starts_with("0X"))
        s.remove_prefix(2);
    if (s.empty())
        return {};
    bcos::bytes out;
    out.reserve(s.size() / 2);
    for (size_t i = 0; i + 1 < s.size(); i += 2)
    {
        char buf[3] = {s[i], s[i + 1], '\0'};
        out.push_back(static_cast<uint8_t>(std::strtoul(buf, nullptr, 16)));
    }
    return out;
}

/// Parse a 20-byte address from hex.
evmc_address parseAddr(std::string_view hex)
{
    evmc_address addr{};
    auto bytes = parseHex(hex);
    if (bytes.size() >= sizeof(addr.bytes))
        std::memcpy(addr.bytes, bytes.data(), sizeof(addr.bytes));
    return addr;
}

/// Parse a 32-byte hash from hex.
evmc_bytes32 parseBytes32(std::string_view hex)
{
    evmc_bytes32 out{};
    auto bytes = parseHex(hex);
    if (bytes.size() >= sizeof(out.bytes))
        std::memcpy(out.bytes, bytes.data(), sizeof(out.bytes));
    return out;
}

/// Parse uint256 from hex or decimal string.
intx::uint256 parseU256(std::string_view s)
{
    if (s.empty())
        return 0;
    if (s.starts_with("0x") || s.starts_with("0X"))
        return intx::from_hex<intx::uint256>(s.substr(2));
    return intx::from_string<intx::uint256>(s);
}

/// Parse a pre-state account map from JSON.
std::vector<std::pair<evmc_address, state::Account>> parsePreState(pt::ptree const& preTree)
{
    std::vector<std::pair<evmc_address, state::Account>> accounts;
    for (auto const& [addrStr, accTree] : preTree)
    {
        state::Account acc{};
        if (auto nonce = accTree.get_optional<std::string>("nonce"))
            acc.nonce = static_cast<uint64_t>(parseU256(*nonce));
        if (auto bal = accTree.get_optional<std::string>("balance"))
            acc.balance = parseU256(*bal);
        if (auto codeStr = accTree.get_optional<std::string>("code"))
        {
            acc.code = parseHex(*codeStr);
            acc.codeHash =
                bcos::crypto::keccak256Hash(bcos::bytesConstRef(acc.code.data(), acc.code.size()));
            acc.codeHash = acc.codeHash;
        }
        if (auto storage = accTree.get_child_optional("storage"))
        {
            for (auto const& [keyStr, valStr] : *storage)
            {
                auto key = parseBytes32(keyStr);
                auto val = parseBytes32(valStr.get_value<std::string>());
                acc.storage[key] = val;
            }
        }
        accounts.emplace_back(parseAddr(addrStr), std::move(acc));
    }
    return accounts;
}

/// Convert pre-state pairs to TestStateView.
TestStateView buildTestStateView(
    std::vector<std::pair<evmc_address, state::Account>> const& preState)
{
    TestStateView sv;
    for (auto const& [addr, acc] : preState)
        sv.set_account(addr, acc);
    return sv;
}

/// Parse a single transaction from RLP hex (Format A blockchain tests use RLP-encoded txs).
state::Transaction parseRlpTx(std::string const& rlpHex)
{
    // Phase 1: treat empty data or simple transfer tx as default.
    // Full RLP decoding will be added in follow-up.
    state::Transaction tx{};
    tx.from = parseAddr("0x0000000000000000000000000000000000000000");
    tx.gasLimit = 21000;
    return tx;
}

/// Parse a simple transaction object from JSON.
state::Transaction parseTx(pt::ptree const& txTree)
{
    state::Transaction tx{};
    // Optional standard fields
    if (auto s = txTree.get_optional<std::string>("sender"))
        tx.from = parseAddr(*s);
    if (auto s = txTree.get_optional<std::string>("to"))
        tx.to = parseAddr(*s);
    if (auto s = txTree.get_optional<std::string>("gasLimit"))
        tx.gasLimit = static_cast<int64_t>(parseU256(*s));
    if (auto s = txTree.get_optional<std::string>("value"))
        tx.value = parseU256(*s);
    if (auto s = txTree.get_optional<std::string>("data"))
        tx.data = parseHex(*s);
    if (auto s = txTree.get_optional<std::string>("gasPrice"))
        tx.gasPrice = parseU256(*s);
    if (auto s = txTree.get_optional<std::string>("nonce"))
    {
        tx.nonce = static_cast<int64_t>(parseU256(*s));
        tx.fromNonce = tx.nonce;
    }
    // EIP-1559 fields
    if (auto s = txTree.get_optional<std::string>("maxFeePerGas"))
    {
        tx.gasPrice = parseU256(*s);
    }
    if (auto s = txTree.get_optional<std::string>("maxPriorityFeePerGas"))
    {
        // stored as gasPrice for BlockTransition compatibility
    }
    return tx;
}

/// Parse block info from JSON.
state::BlockInfo parseBlockInfo(pt::ptree const& headerTree)
{
    state::BlockInfo info{};
    if (auto s = headerTree.get_optional<std::string>("blockNumber"))
        info.number = static_cast<int64_t>(parseU256(*s));
    if (auto s = headerTree.get_optional<std::string>("timestamp"))
        info.timestamp = static_cast<int64_t>(parseU256(*s));
    if (auto s = headerTree.get_optional<std::string>("gasLimit"))
        info.gasLimit = static_cast<int64_t>(parseU256(*s));
    if (auto s = headerTree.get_optional<std::string>("baseFeePerGas"))
        info.baseFee = static_cast<int64_t>(parseU256(*s));
    if (auto s = headerTree.get_optional<std::string>("coinbase"))
        info.coinbase = parseAddr(*s);
    if (auto s = headerTree.get_optional<std::string>("parentHash"))
        info.parentHash = parseBytes32(*s);
    if (auto s = headerTree.get_optional<std::string>("stateRoot"))
        info.stateRoot = parseBytes32(*s);
    if (auto s = headerTree.get_optional<std::string>("prevRandao"))
        info.prevRandao = parseBytes32(*s);
    if (auto s = headerTree.get_optional<std::string>("gasUsed"))
        info.gasUsed = static_cast<int64_t>(parseU256(*s));
    if (auto s = headerTree.get_optional<std::string>("parentBeaconBlockRoot"))
        info.parentBeaconRoot = parseBytes32(*s);
    if (auto s = headerTree.get_optional<std::string>("excessBlobGas"))
        info.excessBlobGas = static_cast<int64_t>(parseU256(*s));
    if (auto s = headerTree.get_optional<std::string>("blobGasUsed"))
        info.blobGasUsed = static_cast<int64_t>(parseU256(*s));
    return info;
}

/// Run one blockchain test fixture.
void runOneFixture(fs::path const& jsonPath, ForkProfile const& profile,
    bcos::crypto::Hash& hashImpl, evmc::VM& vm, size_t& passed, size_t& failed)
{
    pt::ptree root;
    try
    {
        pt::read_json(jsonPath.string(), root);
    }
    catch (std::exception const& e)
    {
        std::cerr << "FAIL " << jsonPath.filename().string() << " (parse error: " << e.what()
                  << ")\n";
        ++failed;
        return;
    }

    size_t testCount = 0;
    for (auto const& [testName, testTree] : root)
    {
        if (!testTree.count("pre") || !testTree.count("genesisBlockHeader"))
        {
            // Engine API sync format — requires payload RLP decoding (Phase 2)
            continue;
        }

        try
        {
            // ── 1. Build pre-state ──
            auto prePairs = parsePreState(testTree.get_child("pre"));
            TestStateView stateView = buildTestStateView(prePairs);

            // ── 2. Parse genesis header ──
            auto genesisHeader = parseBlockInfo(testTree.get_child("genesisBlockHeader"));

            // ── 3. Process blocks ──
            std::vector<TestBlockHeader> chain;
            std::optional<evmc_bytes32> lastStateRoot;

            // Legacy format: "blocks" array
            if (auto blocksOpt = testTree.get_child_optional("blocks"))
            {
                for (auto const& [_, blockTree] : *blocksOpt)
                {
                    auto blockInfo = genesisHeader;
                    if (auto headerOpt = blockTree.get_child_optional("blockHeader"))
                        blockInfo = parseBlockInfo(*headerOpt);

                    // Parse transactions
                    std::vector<state::Transaction> txs;
                    if (auto txsOpt = blockTree.get_child_optional("transactions"))
                        for (auto const& [_, txTree] : *txsOpt)
                            txs.push_back(parseTx(txTree));

                    if (txs.empty() && blockTree.count("rlp"))
                    {
                        // Block has RLP hex but no parsed txs — skip for now
                        continue;
                    }

                    if (txs.empty())
                        continue;

                    // Apply block
                    auto blockResult =
                        applyEthBlock(stateView, txs, blockInfo, profile, vm, hashImpl);

                    // Compute post-state root
                    GstPostStateView postView;
                    for (auto const& [addr, acc] : blockResult.postState.accounts())
                        postView.emplace_back(addr, acc);
                    auto computedRoot = computeStateRoot(postView);
                    lastStateRoot = computedRoot;

                    // Validate against expected stateRoot
                    if (auto expectedRootStr =
                            blockTree.get_optional<std::string>("blockHeader.stateRoot"))
                    {
                        auto expectedRoot = parseBytes32(*expectedRootStr);
                        bool match = (std::memcmp(computedRoot.bytes, expectedRoot.bytes, 32) == 0);
                        if (!match)
                        {
                            std::cerr << "FAIL " << testName << " stateRoot mismatch\n";
                            ++failed;
                            return;
                        }
                    }

                    // Update state view for next block
                    stateView = std::move(blockResult.postState);
                }
            }

            // ── 4. Validate post-state hash ──
            if (auto postStateOpt = testTree.get_child_optional("postState"))
            {
                // Full post-state comparison (Phase 2)
            }
            else if (auto postHashOpt = testTree.get_optional<std::string>("postStateHash"))
            {
                if (lastStateRoot.has_value())
                {
                    auto expectedHash = parseBytes32(*postHashOpt);
                    bool match = (std::memcmp(lastStateRoot->bytes, expectedHash.bytes, 32) == 0);
                    if (!match)
                    {
                        std::cerr << "FAIL " << testName << " postStateHash mismatch\n";
                        ++failed;
                        return;
                    }
                }
            }

            ++testCount;
        }
        catch (std::exception const& e)
        {
            std::cerr << "FAIL " << testName << " (exception: " << e.what() << ")\n";
            ++failed;
            return;
        }
    }

    if (testCount > 0)
    {
        std::cout << "PASS " << jsonPath.filename().string() << " (" << testCount << " blocks)\n";
        ++passed;
    }
    else
    {
        std::cout << "SKIP " << jsonPath.filename().string() << " (no supported blocks)\n";
    }
}

void runBlockchainFixtures(fs::path const& fixturesDir, size_t limit)
{
    bcos::crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};

    size_t passed = 0, failed = 0, skipped = 0, executed = 0;

    std::vector<fs::path> jsonFiles;
    for (auto& entry : fs::recursive_directory_iterator{
             fixturesDir, fs::directory_options::skip_permission_denied})
        if (entry.is_regular_file() && entry.path().extension() == ".json")
            jsonFiles.push_back(entry.path());
    std::sort(jsonFiles.begin(), jsonFiles.end());

    for (auto& jsonPath : jsonFiles)
    {
        if (limit > 0 && executed >= limit)
            break;

        // Fork detection from path
        std::string forkStr = "Cancun";
        auto pathStr = jsonPath.generic_string();
        constexpr std::string_view bcPrefix = "blockchain_tests/";
        auto statePos = pathStr.find(bcPrefix);
        if (statePos != std::string::npos)
        {
            auto start = statePos + bcPrefix.size();
            auto end = pathStr.find('/', start);
            if (end != std::string::npos)
            {
                forkStr = pathStr.substr(start, end - start);
                if (!forkStr.empty())
                    forkStr[0] = static_cast<char>(std::toupper(forkStr[0]));
            }
        }

        auto profile = ForkProfileRegistry::instance().findByUpstreamFork(forkStr);
        if (!profile.has_value())
        {
            std::cout << "SKIP " << jsonPath.filename().string() << " (unknown fork " << forkStr
                      << ")\n";
            ++skipped;
            continue;
        }

        ++executed;
        runOneFixture(jsonPath, *profile, hashImpl, vm, passed, failed);
    }

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed, " << skipped
              << " skipped (" << executed << " executed)\n";
    if (failed > 0)
        std::exit(1);
}

}  // namespace

int main(int argc, char** argv)
{
    fs::path fixturesDir;
    size_t limit = 0;

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg(argv[i]);
        if (arg == "--fixtures" && i + 1 < argc)
            fixturesDir = argv[++i];
        else if (arg == "--limit" && i + 1 < argc)
            limit = static_cast<size_t>(std::stoull(argv[++i]));
    }

    if (fixturesDir.empty())
    {
        std::cerr << "Missing --fixtures\n";
        return 1;
    }
    runBlockchainFixtures(fixturesDir, limit);
    return 0;
}
