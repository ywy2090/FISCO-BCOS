#include "bcos-evm/specs-tests/EestTransactionTestLoader.h"

#include "bcos-evm/eth/state/hash_utils.hpp"
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace bcos::evm::reference_tests
{
namespace
{
namespace pt = boost::property_tree;

[[noreturn]] void throwLoaderError(std::string const& message)
{
    throw std::runtime_error(message);
}

bcos::bytes parseHexBytes(std::string const& value)
{
    if (value.empty() || value == "0x")
    {
        return {};
    }
    return bcos::fromHex(value);
}

TransactionForkResult parseForkResult(pt::ptree const& node)
{
    TransactionForkResult result;
    if (auto const exception = node.get_optional<std::string>("exception"))
    {
        result.exception = *exception;
    }
    if (auto const hash = node.get_optional<std::string>("hash"))
    {
        result.hash = *hash;
    }
    if (auto const gas = node.get_optional<std::string>("intrinsicGas"))
    {
        result.intrinsicGas = *gas;
    }
    return result;
}

TransactionTestCase parseTransactionTestBody(
    pt::ptree const& tree, std::string const& name, std::filesystem::path const& jsonPath)
{
    TransactionTestCase testCase;
    testCase.name = name;
    testCase.sourcePath = jsonPath;
    testCase.txbytes = parseHexBytes(tree.get<std::string>("txbytes"));

    if (auto const resultNode = tree.get_child_optional("result"))
    {
        for (auto const& [fork, forkNode] : *resultNode)
        {
            testCase.resultByFork.emplace(fork, parseForkResult(forkNode));
        }
    }
    return testCase;
}

pt::ptree readJsonTree(std::filesystem::path const& jsonPath)
{
    std::ifstream input(jsonPath);
    if (!input.good())
    {
        throwLoaderError("Failed to open transaction test: " + jsonPath.string());
    }
    pt::ptree tree;
    pt::read_json(input, tree);
    return tree;
}

}  // namespace

std::vector<TransactionTestCase> loadTransactionTestFile(std::filesystem::path const& jsonPath)
{
    auto const tree = readJsonTree(jsonPath);
    std::vector<TransactionTestCase> cases;
    for (auto const& [name, body] : tree)
    {
        if (name == "_info")
        {
            continue;
        }
        cases.push_back(parseTransactionTestBody(body, name, jsonPath));
    }
    if (cases.empty())
    {
        throwLoaderError("No transaction tests in " + jsonPath.string());
    }
    return cases;
}

TransactionTestCase loadTransactionTest(
    std::filesystem::path const& jsonPath, std::optional<std::string_view> testName)
{
    auto const cases = loadTransactionTestFile(jsonPath);
    if (!testName.has_value())
    {
        if (cases.size() == 1)
        {
            return cases.front();
        }
        throwLoaderError("Multiple transaction tests; specify testName: " + jsonPath.string());
    }
    for (auto const& testCase : cases)
    {
        if (testCase.name == *testName)
        {
            return testCase;
        }
    }
    throwLoaderError(
        "Unknown transaction test '" + std::string(*testName) + "' in " + jsonPath.string());
}

std::vector<std::filesystem::path> listEestTransactionTestFiles(
    std::filesystem::path const& eestRoot, std::string_view pathFilter)
{
    auto const root = eestRoot / "fixtures" / "transaction_tests";
    if (!std::filesystem::exists(root))
    {
        throw std::runtime_error("EEST transaction_tests directory not found: " + root.string());
    }

    std::vector<std::filesystem::path> files;
    for (auto const& entry : std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
        {
            continue;
        }
        if (!pathFilter.empty())
        {
            auto const pathStr = entry.path().generic_string();
            if (pathStr.find(pathFilter) == std::string::npos)
            {
                continue;
            }
        }
        files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

}  // namespace bcos::evm::reference_tests
