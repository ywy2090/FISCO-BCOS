#pragma once

#include "bcos-evm/eth-eest-test/ForkProfileRegistry.h"
#include "bcos-evm/eth-eest-test/GeneralStateTestLoader.h"
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::evm::reference_tests
{

struct ResolvedSubtestRun
{
    ForkProfile executionProfile;  // after resolveExecutionProfile
    std::string postForkKey;       // key in postByFork / tryListSubtests arg
};

/// Optional path segment hint: cancun → Cancun (never sole source of truth).
std::optional<std::string_view> inferUpstreamForkFromPath(
    std::filesystem::path const& file, std::filesystem::path const& stateTestsRoot);

/// Lookup forkProfileId for manifest-16 dirs (prague/eip7623 → eth-osaka).
std::optional<std::string_view> manifestProfileIdForPath(
    std::filesystem::path const& file, std::filesystem::path const& stateTestsRoot);

/// Expand one StateTestCase into executable (profile, postFork) pairs.
std::vector<ResolvedSubtestRun> resolveRunsForCase(StateTestCase const& test,
    std::filesystem::path const& sourceFile, std::filesystem::path const& stateTestsRoot,
    std::vector<ForkProfile> const& profileFilter);

}  // namespace bcos::evm::reference_tests
