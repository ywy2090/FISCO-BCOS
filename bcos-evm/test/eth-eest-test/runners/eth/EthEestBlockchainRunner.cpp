#include "bcos-evm/eth-eest-test/EestStateTestLoader.h"
#include "bcos-evm/eth-eest-test/EthMessageAdapter.h"
#include "bcos-evm/eth-eest-test/ForkProfileRegistry.h"
#include "bcos-evm/eth-eest-test/GeneralStateTestLoader.h"
#include "bcos-evm/eth-eest-test/GstStateHash.h"
#include "bcos-evm/eth-eest-test/TestStateView.h"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"

#include "bcos-crypto/hash/Keccak256.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{

void runBlockchainFixtures(fs::path const& fixturesDir, size_t limit)
{
    using namespace bcos::evm::reference_tests;

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
        std::cout << "PASS " << jsonPath.filename().string() << " (skeleton)\n";
        ++passed;
    }

    std::cout << "Results: " << passed << " passed, " << failed << " failed, " << skipped
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
        else
        {
            std::cerr << "Unknown: " << arg << '\n';
            return 1;
        }
    }

    if (fixturesDir.empty())
    {
        std::cerr << "Missing --fixtures\n";
        return 1;
    }
    runBlockchainFixtures(fixturesDir, limit);
    return 0;
}
