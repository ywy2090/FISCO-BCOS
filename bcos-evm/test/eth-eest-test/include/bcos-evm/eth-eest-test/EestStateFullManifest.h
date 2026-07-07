#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::evm::reference_tests
{

/// Canonical eth-eest-state-full.json path (SPECS_TESTS_MANIFEST_DIR when defined).
std::filesystem::path stateFullManifestPath();

/// Index of casePath → forkProfileId from eth-eest-state-full.json (single source for granular).
class StateFullManifestIndex
{
public:
    static StateFullManifestIndex const& instance();

    /// Unique forkProfileIds in manifest entry order (default --fork-profiles when omitted).
    std::vector<std::string_view> defaultGranularProfileIds() const;

    /// Exact or prefix match on directory relative to fixtures/state_tests/.
    std::optional<std::string_view> profileIdForRelativeDir(std::string_view relDir) const;

    /// True when manifest profile differs from first-segment dir heuristic (e.g. prague/7623 →
    /// osaka).
    bool isDistinctManifestProfile(
        std::string_view relDir, std::string_view manifestProfileId) const;

private:
    StateFullManifestIndex();

    std::vector<std::string> m_defaultProfileIdStorage;
    std::vector<std::string_view> m_defaultProfileIds;
    std::vector<std::pair<std::string, std::string>> m_dirProfiles;
};

}  // namespace bcos::evm::reference_tests
