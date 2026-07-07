#include "bcos-evm/eth-eest-test/BlockchainTestLoader.h"
#include "bcos-evm/eth-eest-test/ForkProfileRegistry.h"
#include "helpers/BlockchainRunCore.h"

#include "bcos-crypto/hash/Keccak256.h"
#include <evmone/evmone.h>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
namespace pt = boost::property_tree;

namespace
{

using namespace bcos::evm::reference_tests;

void runOneFixture(fs::path const& jsonPath, std::string_view forkFilter,
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

    auto tests = loadBlockchainTests(root);
    if (tests.empty())
    {
        std::cout << "SKIP " << jsonPath.filename().string() << " (no supported tests)\n";
        return;
    }

    size_t filePassed = 0;
    size_t fileFailed = 0;
    for (auto const& test : tests)
    {
        if (!forkFilter.empty() && test.network != forkFilter)
            continue;

        auto failures = runBlockchainTest(test, vm, hashImpl);
        if (!failures.empty())
        {
            std::cerr << "FAIL " << test.name << " " << failures.front() << '\n';
            ++fileFailed;
        }
        else
        {
            ++filePassed;
        }
    }

    passed += filePassed;
    failed += fileFailed;

    if (filePassed > 0 && fileFailed == 0)
        std::cout << "PASS " << jsonPath.filename().string() << " (" << filePassed << " tests)\n";
    else if (fileFailed > 0)
        std::cout << "FAIL " << jsonPath.filename().string() << " (" << filePassed << " passed, "
                  << fileFailed << " failed)\n";
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
        runOneFixture(jsonPath, forkStr, hashImpl, vm, passed, failed);
    }

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed, " << skipped
              << " skipped (" << executed << " files)\n";
    if (failed > 0)
        std::exit(1);
}

}  // namespace

int main(int argc, char** argv)
{
    fs::path fixturesDir;
    fs::path manifestPath;
    fs::path eestRoot;
    size_t limit = 0;

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg(argv[i]);
        if (arg == "--fixtures" && i + 1 < argc)
            fixturesDir = argv[++i];
        else if (arg == "--limit" && i + 1 < argc)
            limit = static_cast<size_t>(std::stoull(argv[++i]));
        else if (arg == "--manifest" && i + 1 < argc)
            manifestPath = argv[++i];
        else if (arg == "--eest-root" && i + 1 < argc)
            eestRoot = argv[++i];
    }

    if (!manifestPath.empty())
    {
        std::cerr << "manifest mode lands in Phase 4 (--manifest " << manifestPath.string();
        if (!eestRoot.empty())
            std::cerr << " --eest-root " << eestRoot.string();
        std::cerr << ")\n";
        return 1;
    }

    if (fixturesDir.empty())
    {
        std::cerr << "Missing --fixtures\n";
        return 1;
    }
    runBlockchainFixtures(fixturesDir, limit);
    return 0;
}
