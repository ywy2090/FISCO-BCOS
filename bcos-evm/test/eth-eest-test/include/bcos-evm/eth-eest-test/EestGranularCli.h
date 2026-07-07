#pragma once

#include "bcos-evm/eth-eest-test/ForkProfileRegistry.h"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace bcos::evm::reference_tests
{

struct EestGranularCliOptions
{
    std::vector<std::filesystem::path> paths;
    std::optional<std::string> nameFilter;  // -k
    std::vector<std::string> profileIds;    // empty => runner defaults
};

/// Parse argv AFTER testing::InitGoogleTest (gtest flags removed).
EestGranularCliOptions parseEestGranularCliRemaining(int argc, char** argv);

/// Resolve fork profiles; empty profileIds => defaults from eth-eest-state-full.json.
std::vector<ForkProfile> buildRunnerConfig(std::vector<std::string> const& profileIds);

}  // namespace bcos::evm::reference_tests
