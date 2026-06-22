#pragma once

#include <bcos-utilities/Common.h>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace bcos::evm::reference_tests
{

struct TransactionForkResult
{
    std::optional<std::string> exception;
    std::optional<std::string> hash;
    std::optional<std::string> intrinsicGas;
};

struct TransactionTestCase
{
    std::string name;
    std::filesystem::path sourcePath;
    bcos::bytes txbytes;
    std::map<std::string, TransactionForkResult> resultByFork;
};

std::vector<TransactionTestCase> loadTransactionTestFile(std::filesystem::path const& jsonPath);
TransactionTestCase loadTransactionTest(
    std::filesystem::path const& jsonPath, std::optional<std::string_view> testName = std::nullopt);

std::vector<std::filesystem::path> listEestTransactionTestFiles(
    std::filesystem::path const& eestRoot, std::string_view pathFilter = {});

}  // namespace bcos::evm::reference_tests
