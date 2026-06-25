#pragma once

#include "bcos-evm/eth-eest-test/GeneralStateTestLoader.h"
#include <filesystem>

namespace bcos::evm::reference_tests
{

std::filesystem::path resolveEestRoot();
void ensureEestFixturesExtracted(std::filesystem::path const& eestRoot);

/// EEST state test fixtures reuse the official GST JSON schema under fixtures/state_tests/.
std::vector<StateTestCase> loadEestStateTestFile(std::filesystem::path const& jsonPath);
StateTestCase loadEestStateTest(std::filesystem::path const& jsonPath,
    std::optional<std::string_view> variantKey = std::nullopt);

std::vector<std::filesystem::path> listEestStateTestFiles(
    std::filesystem::path const& eestRoot, std::string_view forkFilter = {});

}  // namespace bcos::evm::reference_tests
