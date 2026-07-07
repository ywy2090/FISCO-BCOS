#include "bcos-evm/eth-eest-test/EestForkInference.h"

#include "bcos-evm/eth-eest-test/EestStateFullManifest.h"

#include <algorithm>

namespace bcos::evm::reference_tests
{
namespace
{

bool profileMatchesFork(ForkProfile const& profile, std::string_view fork)
{
    if (profile.upstreamForkName == fork)
    {
        return true;
    }
    return std::any_of(profile.aliases.begin(), profile.aliases.end(),
        [&](std::string const& alias) { return alias == fork; });
}

std::optional<std::filesystem::path> relativeCaseDir(
    std::filesystem::path const& file, std::filesystem::path const& stateTestsRoot)
{
    std::error_code ec;
    auto const rel = std::filesystem::relative(file.parent_path(), stateTestsRoot, ec);
    if (ec || rel.empty() || rel.string().starts_with(".."))
    {
        return std::nullopt;
    }
    return rel;
}

std::optional<std::string_view> profileIdFromPathSegment(std::string_view segment)
{
    return ForkProfileRegistry::instance().profileIdForDirSegment(segment);
}

/// WP-HIST posts: Homestead/Berlin were previously path-gated in Phase 1; Phase 2 runs
/// them wherever the selected fork profile matches the post fork label.
bool shouldRunWpHistPost(
    std::string_view /*postForkKey*/, std::filesystem::path const& /*sourceFile*/)
{
    return true;
}

}  // namespace

std::optional<std::string_view> inferUpstreamForkFromPath(
    std::filesystem::path const& file, std::filesystem::path const& stateTestsRoot)
{
    auto const caseDir = relativeCaseDir(file, stateTestsRoot);
    if (!caseDir.has_value() || caseDir->empty())
    {
        return std::nullopt;
    }
    auto const segment = caseDir->begin()->string();
    return ForkProfileRegistry::instance().upstreamForkForDirSegment(segment);
}

std::optional<std::string_view> manifestProfileIdForPath(
    std::filesystem::path const& file, std::filesystem::path const& stateTestsRoot)
{
    auto const caseDir = relativeCaseDir(file, stateTestsRoot);
    if (!caseDir.has_value())
    {
        return std::nullopt;
    }

    auto const rel = caseDir->generic_string();
    auto const& index = StateFullManifestIndex::instance();
    if (auto const manifestId = index.profileIdForRelativeDir(rel))
    {
        return manifestId;
    }

    auto const segment = caseDir->begin()->string();
    return profileIdFromPathSegment(segment);
}

std::vector<ResolvedSubtestRun> resolveRunsForCase(StateTestCase const& test,
    std::filesystem::path const& sourceFile, std::filesystem::path const& stateTestsRoot,
    std::vector<ForkProfile> const& profileFilter)
{
    auto const& registry = ForkProfileRegistry::instance();
    auto const& manifestIndex = StateFullManifestIndex::instance();
    auto const manifestId = manifestProfileIdForPath(sourceFile, stateTestsRoot);
    std::vector<ResolvedSubtestRun> runs;

    auto const caseRelDir = [&]() -> std::optional<std::string> {
        auto const caseDir = relativeCaseDir(sourceFile, stateTestsRoot);
        if (!caseDir.has_value())
        {
            return std::nullopt;
        }
        return caseDir->generic_string();
    }();

    auto filterCanRunPostFork = [&](std::string_view postForkKey) {
        if (!shouldRunWpHistPost(postForkKey, sourceFile))
        {
            return false;
        }
        if (profileFilter.empty())
        {
            return true;
        }
        for (auto const& profile : profileFilter)
        {
            if (profileMatchesFork(profile, postForkKey))
            {
                return true;
            }
        }
        return false;
    };

    auto isDistinctManifestProfile = [&]() {
        if (!manifestId.has_value() || !caseRelDir.has_value())
        {
            return false;
        }
        return manifestIndex.isDistinctManifestProfile(*caseRelDir, *manifestId);
    };

    auto determineBaseProfile = [&](std::string_view postForkKey) -> std::optional<ForkProfile> {
        std::vector<ForkProfile const*> matching;
        for (auto const& profile : profileFilter)
        {
            if (profileMatchesFork(profile, postForkKey))
            {
                matching.push_back(&profile);
            }
        }
        if (matching.size() == 1)
        {
            return *matching.front();
        }

        if (manifestId.has_value())
        {
            if (auto const profile = registry.findByProfileId(*manifestId))
            {
                if (isDistinctManifestProfile() || profileMatchesFork(*profile, postForkKey))
                {
                    return *profile;
                }
            }
        }

        if (auto const pathHint = inferUpstreamForkFromPath(sourceFile, stateTestsRoot))
        {
            if (*pathHint == postForkKey)
            {
                for (auto const& profile : profileFilter)
                {
                    if (profileMatchesFork(profile, *pathHint))
                    {
                        return profile;
                    }
                }
            }
        }

        if (profileFilter.size() == 1)
        {
            return profileFilter.front();
        }
        return std::nullopt;
    };

    for (auto const& [postForkKey, posts] : test.postByFork)
    {
        (void)posts;
        if (!filterCanRunPostFork(postForkKey))
        {
            continue;
        }

        auto const baseProfile = determineBaseProfile(postForkKey);
        if (!baseProfile.has_value())
        {
            continue;
        }

        ResolvedSubtestRun run;
        run.postForkKey = postForkKey;
        run.executionProfile =
            registry.resolveExecutionProfile(*baseProfile, std::string(postForkKey));
        runs.push_back(std::move(run));
    }

    return runs;
}

}  // namespace bcos::evm::reference_tests
