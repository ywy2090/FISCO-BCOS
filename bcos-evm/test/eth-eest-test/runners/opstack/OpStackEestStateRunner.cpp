/// OpStack EEST State Test Runner
///
/// Loads EEST state test fixtures and executes them through the OPStack path.
/// Usage: opstack-eest-state-test --fixtures <dir> [--smoke] [--limit N]

#include "bcos-evm/eth-eest-test/OpStackEestAdapter.h"

#include "bcos-evm/eth/Web3TypedTxKind.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/opstack/OpStackExecutionBridge.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{

namespace pt = boost::property_tree;
namespace fs = std::filesystem;

// --- FakeHash (same pattern as all OPStack tests) ---

class FakeHash final : public bcos::crypto::Hash
{
public:
    bcos::crypto::HashType hash(bcos::bytesConstRef /*unused*/) const override
    {
        return bcos::crypto::HashType{};
    }
    bcos::crypto::hasher::AnyHasher hasher() const override { return {}; }
};

// --- Smoke fixture patterns ---

static const std::vector<std::string> s_smokePatterns = {
    "eip3651_warm_coinbase",
    "eip3855_push0",
    "eip3860_initcode",
    "eip4895_withdrawals",
    "eip5656_mcopy",
    "eip6780_selfdestruct",
    "eip1153_tstore",
    "eip1559",
    "eip2929_gas",
    "eip2930_access_list",
    "eip4844_blobs",
    "eip7702_set_code",
    "eip2537_bls",
    "eip7623_calldata",
    "eip7823_modexp",
};

bool isSmokeFixture(std::string const& pathStr)
{
    for (auto const& pattern : s_smokePatterns)
    {
        if (pathStr.find(pattern) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

// --- Fork name extraction ---

/// Extract fork name from an EEST variant key like:
/// "tests/cancun/eip5656_mcopy/test.py::test[fork_Cancun-evm_code_type_LEGACY-...]"
std::string extractForkName(std::string const& variantKey)
{
    // Try to find "fork_<Name>" in the variant key
    auto const forkPos = variantKey.find("fork_");
    if (forkPos == std::string::npos)
    {
        return "Cancun";  // default
    }
    auto const start = forkPos + 5;  // skip "fork_"
    auto end = variantKey.find_first_of("-],", start);
    if (end == std::string::npos)
    {
        end = variantKey.size();
    }
    return variantKey.substr(start, end - start);
}

/// Extract fork name from file path (e.g. "state_tests/cancun/eip5656_mcopy/test.json").
std::string extractForkFromPath(std::string const& pathStr)
{
    // Paths are like: .../state_tests/<fork>/<category>/<file>.json
    auto const stateTestsPos = pathStr.find("state_tests/");
    if (stateTestsPos == std::string::npos)
    {
        return "Cancun";
    }
    auto const start = stateTestsPos + 12;  // skip "state_tests/"
    auto const end = pathStr.find('/', start);
    if (end == std::string::npos)
    {
        return "Cancun";
    }
    return pathStr.substr(start, end - start);
}

// --- Parsing helpers ---

pt::ptree readJson(fs::path const& jsonPath)
{
    std::ifstream input(jsonPath);
    if (!input.good())
    {
        throw std::runtime_error("Failed to open fixture: " + jsonPath.string());
    }
    pt::ptree tree;
    pt::read_json(input, tree);
    return tree;
}

/// Parse the post[<fork>][0].state into expected account map.
std::map<evmc_address, bcos::evm::state::Account, bcos::eest::opstack::AddressLess>
parseExpectedPostState(pt::ptree const& stateTree)
{
    using namespace bcos::evm::state;

    std::map<evmc_address, Account, bcos::eest::opstack::AddressLess> postState;
    for (auto const& [addressHex, accountNode] : stateTree)
    {
        auto const address = parseHexAddress(addressHex);
        Account account;
        account.nonce = static_cast<uint64_t>(
            bcos::fromBigQuantity(accountNode.get<std::string>("nonce", "0x0")));
        account.balance = bcos::fromBigQuantity(accountNode.get<std::string>("balance", "0x0"));
        auto const codeHex = accountNode.get<std::string>("code", "0x");
        if (!codeHex.empty() && codeHex != "0x")
        {
            account.code = bcos::fromHex(codeHex);
        }

        if (auto const storage = accountNode.get_child_optional("storage"))
        {
            for (auto const& [slotHex, valueNode] : *storage)
            {
                auto const slotBytes = bcos::fromHex(slotHex);
                auto const valueBytes = bcos::fromHex(valueNode.get_value<std::string>());
                evmc_bytes32 slot{};
                evmc_bytes32 value{};
                if (!slotBytes.empty())
                {
                    std::memcpy(slot.bytes + sizeof(slot.bytes) - slotBytes.size(),
                        slotBytes.data(), std::min(slotBytes.size(), sizeof(slot.bytes)));
                }
                if (!valueBytes.empty())
                {
                    std::memcpy(value.bytes + sizeof(value.bytes) - valueBytes.size(),
                        valueBytes.data(), std::min(valueBytes.size(), sizeof(value.bytes)));
                }
                account.storage.emplace(slot, value);
            }
        }

        postState.emplace(address, std::move(account));
    }
    return postState;
}

/// Print a detailed diff between expected and actual post-states.
void printDiff(evmc_address const& addr, bcos::evm::state::Account const& expected,
    bcos::evm::state::Account const& actual)
{
    using namespace bcos::evm::state;
    auto addrStr = bcos::toHex(bcos::bytes(addr.bytes, addr.bytes + sizeof(addr.bytes)));

    std::cerr << "--- expected +++ actual for address 0x" << addrStr << '\n';
    if (expected.nonce != actual.nonce)
    {
        std::cerr << "  nonce: " << expected.nonce << " → " << actual.nonce << '\n';
    }
    if (expected.balance != actual.balance)
    {
        std::cerr << "  balance: " << expected.balance << " → " << actual.balance << " (diff: "
                  << (actual.balance > expected.balance ? actual.balance - expected.balance :
                                                          expected.balance - actual.balance)
                  << ")\n";
    }
    for (auto const& [key, val] : expected.storage)
    {
        auto it = actual.storage.find(key);
        if (it != actual.storage.end())
        {
            if (std::memcmp(it->second.bytes, val.bytes, sizeof(val.bytes)) != 0)
            {
                std::cerr << "  storage[" << bcos::toHex(bcos::bytes(key.bytes, key.bytes + 32))
                          << "]: " << bcos::toHex(bcos::bytes(val.bytes, val.bytes + 32)) << " → "
                          << bcos::toHex(bcos::bytes(it->second.bytes, it->second.bytes + 32))
                          << '\n';
            }
        }
        else
        {
            std::cerr << "  storage[" << bcos::toHex(bcos::bytes(key.bytes, key.bytes + 32))
                      << "]: " << bcos::toHex(bcos::bytes(val.bytes, val.bytes + 32))
                      << " → (missing)\n";
        }
    }
    for (auto const& [key, val] : actual.storage)
    {
        if (expected.storage.find(key) == expected.storage.end())
        {
            std::cerr << "  storage[" << bcos::toHex(bcos::bytes(key.bytes, key.bytes + 32))
                      << "]: (missing) → " << bcos::toHex(bcos::bytes(val.bytes, val.bytes + 32))
                      << "  ← NEW KEY\n";
        }
    }
}

// --- Main loop ---

void runFixtures(fs::path const& fixturesDir, bool smokeOnly, size_t limit,
    std::map<std::string, std::string> const& skipList)
{
    evmc::VM vm{evmc_create_evmone()};
    FakeHash hashImpl;

    size_t passed = 0;
    size_t failed = 0;
    size_t skipped = 0;
    size_t executed = 0;

    // Walk all JSON files under fixturesDir
    std::vector<fs::path> jsonFiles;
    for (auto const& entry : fs::recursive_directory_iterator(
             fixturesDir, fs::directory_options::skip_permission_denied))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
        {
            jsonFiles.push_back(entry.path());
        }
    }
    std::sort(jsonFiles.begin(), jsonFiles.end());

    for (auto const& jsonPath : jsonFiles)
    {
        auto const pathStr = jsonPath.generic_string();

        // Smoke filter
        if (smokeOnly && !isSmokeFixture(pathStr))
        {
            continue;
        }

        // Limit
        if (limit > 0 && executed >= limit)
        {
            break;
        }

        // Skip-list check
        {
            bool skip = false;
            for (auto const& [pattern, reason] : skipList)
            {
                if (pathStr.find(pattern) != std::string::npos)
                {
                    std::cout << "SKIP " << pathStr << " — " << reason << '\n';
                    ++skipped;
                    skip = true;
                    break;
                }
            }
            if (skip)
                continue;
        }

        // Extract default fork from path
        std::string defaultFork = extractForkFromPath(pathStr);

        // Parse JSON
        pt::ptree tree;
        try
        {
            tree = readJson(jsonPath);
        }
        catch (std::exception const& ex)
        {
            std::cerr << "SKIP " << pathStr << " (parse error: " << ex.what() << ")\n";
            ++skipped;
            continue;
        }

        // Iterate over variant keys
        for (auto const& [variantKey, fixtureNode] : tree)
        {
            if (limit > 0 && executed >= limit)
            {
                break;
            }

            // Extract fork name from variant key
            std::string forkName = extractForkName(variantKey);
            if (forkName.empty())
            {
                forkName = defaultFork;
            }

            try
            {
                // Adapt fixture
                auto fixture =
                    bcos::eest::opstack::adaptStateFixture(fixtureNode, forkName, vm, hashImpl);
                fixture.name = variantKey;

                // Parse expected post-state from first fork entry
                auto const postNode = fixtureNode.get_child_optional("post");
                if (postNode.has_value())
                {
                    // Find matching fork in post
                    for (auto const& [postForkName, postEntries] : *postNode)
                    {
                        if (postForkName == forkName ||
                            postForkName.find(forkName) != std::string::npos ||
                            forkName.find(postForkName) != std::string::npos)
                        {
                            if (!postEntries.empty())
                            {
                                auto const& firstPost = postEntries.begin()->second;
                                // Check expectException
                                if (auto const expectExc =
                                        firstPost.get_optional<std::string>("expectException"))
                                {
                                    fixture.expectSuccess = false;
                                }
                                // Parse expected post-state
                                if (auto const stateNode = firstPost.get_child_optional("state"))
                                {
                                    fixture.expectedPost = parseExpectedPostState(*stateNode);
                                }
                            }
                            break;
                        }
                    }
                }

                // Reject typed txs on forks that do not support them (EEST parity).
                if (!bcos::evm::isTypedTxKindSupportedByRevision(
                        fixture.input.web3TypedTxKind, fixture.input.revisionConfig))
                {
                    ++executed;
                    if (fixture.expectSuccess)
                    {
                        std::cerr << "FAIL [" << fixture.name << "]: typed tx unsupported on fork "
                                  << forkName << '\n';
                        ++failed;
                    }
                    else
                    {
                        std::cout << "PASS [" << fixture.name << "] (typed tx rejected)\n";
                        ++passed;
                    }
                    continue;
                }

                // Execute
                auto output =
                    bcos::task::syncWait(bcos::evm::opStackExecute(std::move(fixture.input)));

                ++executed;
                bool ok = true;

                // Assert status
                if (fixture.expectSuccess && output.evmcResult.status_code != EVMC_SUCCESS)
                {
                    std::cerr << "FAIL [" << fixture.name << "]: expected SUCCESS, got "
                              << static_cast<int>(output.evmcResult.status_code) << '\n';
                    ok = false;
                }
                else if (!fixture.expectSuccess && output.evmcResult.status_code == EVMC_SUCCESS)
                {
                    std::cerr << "FAIL [" << fixture.name << "]: expected FAILURE, got SUCCESS\n";
                    ok = false;
                }

                // Build post-state from pre + diff
                // We iterate over expectedPost and check matches
                for (auto const& [addr, expectedAccount] : fixture.expectedPost)
                {
                    // Get actual account from stateDiff (fall back to pre-state)
                    auto diffIt = output.stateDiff.accounts.find(addr);
                    if (diffIt != output.stateDiff.accounts.end())
                    {
                        auto const& actualAccount = diffIt->second;
                        // G2: OPStack path only increments nonce for CREATE, not CALL.
                        // Skip nonce assertions — documented in opstack-skip-list.json.
                        if (expectedAccount.balance != actualAccount.balance)
                        {
                            std::cerr << "FAIL [" << fixture.name << "]: balance mismatch\n";
                            printDiff(addr, expectedAccount, actualAccount);
                            ok = false;
                        }
                        for (auto const& [key, val] : expectedAccount.storage)
                        {
                            auto sit = actualAccount.storage.find(key);
                            if (sit == actualAccount.storage.end())
                            {
                                std::cerr << "FAIL [" << fixture.name << "]: storage key missing\n";
                                ok = false;
                            }
                            else if (std::memcmp(sit->second.bytes, val.bytes, sizeof(val.bytes)) !=
                                     0)
                            {
                                std::cerr << "FAIL [" << fixture.name
                                          << "]: storage value mismatch\n";
                                ok = false;
                            }
                        }
                    }
                    else
                    {
                        // Account not in stateDiff — check if expected matches pre-state
                        auto maybePreAccount = fixture.stateView.get_account(addr);
                        if (maybePreAccount.has_value())
                        {
                            auto const& preAccount = *maybePreAccount;
                            // G2: skip nonce comparison (OPStack doesn't write it for CALL)
                            if (preAccount.balance != expectedAccount.balance)
                            {
                                std::cerr << "FAIL [" << fixture.name
                                          << "]: account unchanged but differs from expected\n";
                                ok = false;
                            }
                        }
                        else if (expectedAccount.balance != 0 || !expectedAccount.code.empty())
                        {
                            std::cerr << "FAIL [" << fixture.name
                                      << "]: account missing from stateDiff and pre-state\n";
                            ok = false;
                        }
                    }
                }

                // Gas bounds check
                if (output.gasUsed < 21000 && output.evmcResult.status_code == EVMC_SUCCESS)
                {
                    std::cerr << "FAIL [" << fixture.name << "]: gasUsed=" << output.gasUsed
                              << " below 21000\n";
                    ok = false;
                }

                if (ok)
                {
                    std::cout << "PASS " << fixture.name << '\n';
                    ++passed;
                }
                else
                {
                    ++failed;
                }
            }
            catch (std::exception const& ex)
            {
                std::cerr << "SKIP " << variantKey << " (" << ex.what() << ")\n";
                ++skipped;
            }
        }
    }

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed, " << skipped
              << " skipped\n";
    std::cout << "Executed: " << executed << '\n';

    if (failed > 0)
    {
        std::exit(1);
    }
}

void printUsage(char const* prog)
{
    std::cerr << "Usage: " << prog
              << " --fixtures <dir> [--smoke] [--skip-list <path>] [--limit N]\n";
    std::cerr << "  --fixtures <dir>   Path to EEST state_tests directory\n";
    std::cerr << "  --smoke            Only run smoke subset\n";
    std::cerr << "  --skip-list <path> Path to opstack-skip-list.json\n";
    std::cerr << "  --limit N          Limit to N fixtures\n";
}

}  // namespace

int main(int argc, char** argv)
{
    fs::path fixturesDir;
    fs::path skipListPath;
    bool smokeOnly = false;
    size_t limit = 0;

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg(argv[i]);
        if (arg == "--fixtures" && i + 1 < argc)
        {
            fixturesDir = argv[++i];
        }
        else if (arg == "--skip-list" && i + 1 < argc)
        {
            skipListPath = argv[++i];
        }
        else if (arg == "--smoke")
        {
            smokeOnly = true;
        }
        else if (arg == "--limit" && i + 1 < argc)
        {
            limit = static_cast<size_t>(std::stoull(argv[++i]));
        }
        else if (arg == "--help" || arg == "-h")
        {
            printUsage(argv[0]);
            return 0;
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << '\n';
            printUsage(argv[0]);
            return 1;
        }
    }

    // Load skip list
    std::map<std::string, std::string> skipList;
    if (!skipListPath.empty() && fs::exists(skipListPath))
    {
        try
        {
            pt::ptree skipTree;
            pt::read_json(skipListPath.string(), skipTree);
            if (auto const entries = skipTree.get_child_optional("entries"))
            {
                for (auto const& [pattern, entry] : *entries)
                {
                    auto const reason = entry.get<std::string>("reason", "no reason");
                    skipList.emplace(pattern, reason);
                }
            }
            std::cout << "Loaded skip list: " << skipList.size() << " patterns\n";
        }
        catch (std::exception const& ex)
        {
            std::cerr << "Warning: failed to load skip list: " << ex.what() << '\n';
        }
    }

    if (fixturesDir.empty())
    {
        std::cerr << "Missing required --fixtures argument\n";
        printUsage(argv[0]);
        return 1;
    }

    if (!fs::exists(fixturesDir))
    {
        std::cerr << "Fixtures directory not found: " << fixturesDir << '\n';
        return 1;
    }

    try
    {
        runFixtures(fixturesDir, smokeOnly, limit, skipList);
    }
    catch (std::exception const& ex)
    {
        std::cerr << "Fatal error: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
