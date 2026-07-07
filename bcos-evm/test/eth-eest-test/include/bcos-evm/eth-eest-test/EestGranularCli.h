#pragma once

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

}  // namespace bcos::evm::reference_tests
