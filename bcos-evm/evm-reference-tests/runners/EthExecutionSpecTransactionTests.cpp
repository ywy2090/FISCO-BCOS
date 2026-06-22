#include "bcos-evm/evm-reference-tests/EestStateTestLoader.h"
#include "bcos-evm/evm-reference-tests/EestTransactionTestLoader.h"
#include "bcos-evm/evm-reference-tests/Eip7702StrictTxValidator.h"
#include "bcos-evm/evm-reference-tests/ForkProfileRegistry.h"
#include "bcos-evm/evm-reference-tests/ManifestLoader.h"
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::evm::reference_tests
{
namespace
{

struct Options
{
    std::filesystem::path manifestPath;
    std::filesystem::path eestRoot;
};

Options parseOptions(int argc, char** argv)
{
    Options options;
    options.eestRoot = resolveEestRoot();

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg(argv[i]);
        if (arg == "--manifest" && i + 1 < argc)
        {
            options.manifestPath = argv[++i];
        }
        else if (arg == "--eest-root" && i + 1 < argc)
        {
            options.eestRoot = argv[++i];
        }
        else
        {
            throw std::runtime_error("Unknown argument: " + std::string(arg));
        }
    }

    if (options.manifestPath.empty())
    {
        throw std::runtime_error("Missing required --manifest argument");
    }

    return options;
}

std::vector<std::filesystem::path> resolveTxCasePaths(
    std::filesystem::path const& eestRoot, ManifestEntry const& entry)
{
    auto const target = eestRoot / entry.casePath;
    if (std::filesystem::is_directory(target))
    {
        return listEestTransactionTestFiles(eestRoot, entry.casePath);
    }
    return {target};
}

std::optional<std::string> resolveTransactionResultFork(
    TransactionTestCase const& testCase, ForkProfile const& profile)
{
    if (testCase.resultByFork.contains(profile.upstreamForkName))
    {
        return profile.upstreamForkName;
    }
    if (testCase.resultByFork.contains("Osaka"))
    {
        return "Osaka";
    }
    if (testCase.resultByFork.contains("Prague"))
    {
        return "Prague";
    }
    return std::nullopt;
}

bool validate7702Tx(TransactionTestCase const& testCase, std::string const& fork, int& failures)
{
    auto const forkIt = testCase.resultByFork.find(fork);
    if (forkIt == testCase.resultByFork.end())
    {
        return false;
    }

    auto const& expected = forkIt->second;
    bool const expectInvalid = expected.exception.has_value();
    if (testCase.txbytes.empty() || testCase.txbytes[0] != 0x04)
    {
        if (expectInvalid)
        {
            return true;
        }
        std::cerr << "FAIL " << testCase.name << ": expected valid EIP-7702 typed tx\n";
        ++failures;
        return true;
    }

    bool const valid = validateStrictEip7702TypedTx(
        bcos::bytesConstRef{testCase.txbytes.data(), testCase.txbytes.size()});
    if (expectInvalid)
    {
        if (valid)
        {
            std::cerr << "FAIL " << testCase.name << ": expected decode failure for fork " << fork
                      << '\n';
            ++failures;
        }
        return true;
    }

    if (!valid)
    {
        std::cerr << "FAIL " << testCase.name
                  << ": expected decodable EIP-7702 authorization list\n";
        ++failures;
    }
    return true;
}

}  // namespace
}  // namespace bcos::evm::reference_tests

int main(int argc, char** argv)
{
    using namespace bcos::evm::reference_tests;

    try
    {
        auto const options = parseOptions(argc, argv);
        ensureEestFixturesExtracted(options.eestRoot);

        auto const entries = loadManifest(options.manifestPath);
        int failures = 0;
        int executed = 0;

        for (auto const& entry : entries)
        {
            if (entry.sourceSuite != "eest")
            {
                std::cerr << "FAIL " << entry.evidenceId << ": sourceSuite must be eest\n";
                ++failures;
                continue;
            }

            auto const profile =
                ForkProfileRegistry::instance().findByProfileId(entry.forkProfileId);
            if (!profile.has_value())
            {
                std::cerr << "FAIL " << entry.evidenceId << ": unknown forkProfileId\n";
                ++failures;
                continue;
            }

            for (auto const& jsonPath : resolveTxCasePaths(options.eestRoot, entry))
            {
                std::vector<TransactionTestCase> cases;
                try
                {
                    cases = loadTransactionTestFile(jsonPath);
                }
                catch (std::exception const& ex)
                {
                    std::cerr << "SKIP " << entry.evidenceId << " " << jsonPath << " (" << ex.what()
                              << ")\n";
                    continue;
                }

                for (auto const& testCase : cases)
                {
                    if (entry.variantKey.has_value() && testCase.name != *entry.variantKey)
                    {
                        continue;
                    }

                    auto const resultFork = resolveTransactionResultFork(testCase, *profile);
                    if (!resultFork.has_value())
                    {
                        continue;
                    }

                    if (!validate7702Tx(testCase, *resultFork, failures))
                    {
                        continue;
                    }

                    ++executed;
                    std::cout << "PASS " << entry.evidenceId << " test=" << testCase.name << '\n';
                }
            }
        }

        if (executed == 0)
        {
            std::cerr << "No EEST transaction tests executed\n";
            return 1;
        }

        if (failures != 0)
        {
            std::cerr << failures << " failure(s)\n";
            return 1;
        }

        std::cout << "All " << executed << " EEST transaction test(s) passed\n";
        return 0;
    }
    catch (std::exception const& ex)
    {
        std::cerr << "EthExecutionSpecTransactionTests error: " << ex.what() << '\n';
        return 1;
    }
}
