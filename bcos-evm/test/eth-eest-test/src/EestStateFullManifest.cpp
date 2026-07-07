#include "bcos-evm/eth-eest-test/EestStateFullManifest.h"

#include "bcos-evm/eth-eest-test/ForkProfileRegistry.h"
#include "bcos-evm/eth-eest-test/ManifestLoader.h"

#include <algorithm>
#include <unordered_set>

namespace bcos::evm::reference_tests
{
namespace
{

constexpr char kStateTestsPrefix[] = "fixtures/state_tests/";

std::string normalizeCaseRelativePath(std::string const& casePath)
{
    if (casePath.starts_with(kStateTestsPrefix))
    {
        return casePath.substr(sizeof(kStateTestsPrefix) - 1);
    }
    return casePath;
}

bool dirMatchesRelative(std::string_view relDir, std::string const& manifestDir)
{
    if (relDir == manifestDir)
    {
        return true;
    }
    if (relDir.size() <= manifestDir.size())
    {
        return false;
    }
    return relDir.starts_with(manifestDir) && relDir[manifestDir.size()] == '/';
}

}  // namespace

std::filesystem::path stateFullManifestPath()
{
#ifdef SPECS_TESTS_MANIFEST_DIR
    return std::filesystem::path(SPECS_TESTS_MANIFEST_DIR) / "eth/eth-eest-state-full.json";
#elif defined(SPECS_TESTS_SOURCE_DIR)
    return std::filesystem::path(SPECS_TESTS_SOURCE_DIR) / "manifests/eth/eth-eest-state-full.json";
#else
    return std::filesystem::path(
        "bcos-evm/test/eth-eest-test/manifests/eth/eth-eest-state-full.json");
#endif
}

StateFullManifestIndex::StateFullManifestIndex()
{
    auto const entries = loadManifest(stateFullManifestPath());
    std::unordered_set<std::string> seenDefaultIds;

    m_dirProfiles.reserve(entries.size());
    m_defaultProfileIdStorage.reserve(8);

    for (auto const& entry : entries)
    {
        auto const relDir = normalizeCaseRelativePath(entry.casePath);
        m_dirProfiles.emplace_back(relDir, entry.forkProfileId);

        if (seenDefaultIds.insert(entry.forkProfileId).second)
        {
            m_defaultProfileIdStorage.push_back(entry.forkProfileId);
        }
    }

    for (auto const& id : m_defaultProfileIdStorage)
    {
        m_defaultProfileIds.emplace_back(id);
    }

    std::ranges::sort(m_dirProfiles,
        [](auto const& a, auto const& b) { return a.first.size() > b.first.size(); });
}

StateFullManifestIndex const& StateFullManifestIndex::instance()
{
    static StateFullManifestIndex const index;
    return index;
}

std::vector<std::string_view> StateFullManifestIndex::defaultGranularProfileIds() const
{
    return m_defaultProfileIds;
}

std::optional<std::string_view> StateFullManifestIndex::profileIdForRelativeDir(
    std::string_view relDir) const
{
    for (auto const& [dir, profileId] : m_dirProfiles)
    {
        if (dirMatchesRelative(relDir, dir))
        {
            return profileId;
        }
    }
    return std::nullopt;
}

bool StateFullManifestIndex::isDistinctManifestProfile(
    std::string_view relDir, std::string_view manifestProfileId) const
{
    if (!profileIdForRelativeDir(relDir).has_value())
    {
        return false;
    }

    auto const slash = relDir.find('/');
    auto const segment = slash == std::string_view::npos ? relDir : relDir.substr(0, slash);
    auto const segmentProfile = ForkProfileRegistry::instance().profileIdForDirSegment(segment);
    return segmentProfile.has_value() && *segmentProfile != manifestProfileId;
}

}  // namespace bcos::evm::reference_tests
