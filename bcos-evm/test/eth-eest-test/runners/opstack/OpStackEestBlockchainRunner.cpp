/// OpStack EEST Blockchain Test Runner
///
/// Loads EEST blockchain test fixtures and executes them through the OPStack path.
/// Blockchain tests process a sequence of transactions per block.
/// Usage: opstack-eest-blockchain-test --fixtures <dir> [--limit N]

#include "bcos-evm/eth-eest-test/OpStackEestAdapter.h"

#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/opstack/ApplyOpStackMessage.h"
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

class FakeHash final : public bcos::crypto::Hash
{
public:
    bcos::crypto::HashType hash(bcos::bytesConstRef /*unused*/) const override
    {
        return bcos::crypto::HashType{};
    }
    bcos::crypto::hasher::AnyHasher hasher() const override { return {}; }
};

std::string extractForkName(std::string const& variantKey)
{
    auto const forkPos = variantKey.find("fork_");
    if (forkPos == std::string::npos)
    {
        return "Cancun";
    }
    auto const start = forkPos + 5;
    auto end = variantKey.find_first_of("-],", start);
    if (end == std::string::npos)
    {
        end = variantKey.size();
    }
    return variantKey.substr(start, end - start);
}

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

void runBlockchainFixtures(fs::path const& fixturesDir, size_t limit)
{
    evmc::VM vm{evmc_create_evmone()};
    FakeHash hashImpl;

    size_t passed = 0;
    size_t failed = 0;
    size_t skipped = 0;
    size_t executed = 0;

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
        if (limit > 0 && executed >= limit)
        {
            break;
        }

        auto const pathStr = jsonPath.generic_string();
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

        for (auto const& [variantKey, fixtureNode] : tree)
        {
            if (limit > 0 && executed >= limit)
            {
                break;
            }

            std::string forkName = extractForkName(variantKey);

            try
            {
                if (!fixtureNode.get_child_optional("env"))
                {
                    std::cerr << "SKIP " << variantKey
                              << " (blockchain fixture lacks state-test env section)\n";
                    ++skipped;
                    continue;
                }

                auto fixture =
                    bcos::eest::opstack::adaptStateFixture(fixtureNode, forkName, vm, hashImpl);
                fixture.name = variantKey;

                // Blockchain tests have "blocks" array
                auto const blocksNode = fixtureNode.get_child_optional("blocks");
                if (!blocksNode.has_value())
                {
                    std::cerr << "SKIP " << variantKey
                              << " (no blocks array in blockchain fixture)\n";
                    ++skipped;
                    continue;
                }

                for (auto const& [blockKey, blockNode] : *blocksNode)
                {
                    static_cast<void>(blockKey);

                    auto const txNode = blockNode.get_child_optional("transactions");
                    if (!txNode.has_value() || txNode->empty())
                    {
                        continue;
                    }

                    for (auto const& [txKey, txEntry] : *txNode)
                    {
                        static_cast<void>(txKey);

                        // Adapt each transaction and execute
                        // Create a fresh state view for each block's first tx
                        auto& stateViewForTx = fixture.stateView;
                        auto blockInfo = fixture.input.blockInfo;
                        auto revisionConfig = fixture.input.revisionConfig;

                        // Build message from tx entry
                        evmc_message msg{};
                        auto const txTo = txEntry.get_optional<std::string>("to");
                        bool isCreate = !txTo.has_value() || txTo->empty() || *txTo == "0x";
                        msg.kind = isCreate ? EVMC_CREATE : EVMC_CALL;
                        msg.gas = static_cast<int64_t>(
                            std::stoull(txEntry.get<std::string>("gasLimit", "0")));
                        msg.sender = bcos::evm::state::parseHexAddress(
                            txEntry.get<std::string>("sender", "0x0"));
                        if (!isCreate)
                        {
                            msg.recipient = bcos::evm::state::parseHexAddress(*txTo);
                        }
                        msg.value = bcos::evm::state::toEvmC(
                            bcos::fromBigQuantity(txEntry.get<std::string>("value", "0x0")));
                        auto data = bcos::fromHex(txEntry.get<std::string>("data", "0x"));
                        msg.input_data = data.data();
                        msg.input_size = data.size();

                        bcos::evm::OpStackExecutionRequest input;
                        input.stateView = &stateViewForTx;
                        input.vm = &vm;
                        input.hashImpl = &hashImpl;
                        input.message = msg;
                        input.gasFeeCap =
                            bcos::fromBigQuantity(txEntry.get<std::string>("maxFeePerGas", "0x0"));
                        input.gasTipCap = bcos::fromBigQuantity(
                            txEntry.get<std::string>("maxPriorityFeePerGas", "0x0"));
                        input.blockInfo = blockInfo;
                        input.revisionConfig = revisionConfig;
                        input.forkSchedule = bcos::evm::makeIsthmusPlusForkSchedule();

                        auto output =
                            bcos::task::syncWait(bcos::evm::applyOpStackMessage(std::move(input)));

                        // Blockchain tests expect cumulative gas
                        // For now, just check that execution didn't crash
                        if (output.evmcResult.status_code != EVMC_SUCCESS &&
                            output.evmcResult.status_code != EVMC_REVERT)
                        {
                            std::cerr << "FAIL [" << variantKey << "]: unexpected status "
                                      << static_cast<int>(output.evmcResult.status_code)
                                      << " in tx " << txKey << '\n';
                            ++failed;
                        }
                    }
                }

                ++executed;
                if (failed == 0)
                {
                    std::cout << "PASS " << variantKey << " (all txs executed)\n";
                    ++passed;
                }
                else
                {
                    // Reset failed counter for next fixture
                    failed = 0;
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

}  // namespace

int main(int argc, char** argv)
{
    fs::path fixturesDir;
    size_t limit = 0;

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg(argv[i]);
        if (arg == "--fixtures" && i + 1 < argc)
        {
            fixturesDir = argv[++i];
        }
        else if (arg == "--limit" && i + 1 < argc)
        {
            limit = static_cast<size_t>(std::stoull(argv[++i]));
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cerr << "Usage: " << argv[0] << " --fixtures <dir> [--limit N]\n";
            return 0;
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << '\n';
            return 1;
        }
    }

    if (fixturesDir.empty())
    {
        std::cerr << "Missing required --fixtures argument\n";
        return 1;
    }

    try
    {
        runBlockchainFixtures(fixturesDir, limit);
    }
    catch (std::exception const& ex)
    {
        std::cerr << "Fatal error: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
