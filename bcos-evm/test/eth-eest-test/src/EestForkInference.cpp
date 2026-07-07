#include "bcos-evm/eth-eest-test/EestForkInference.h"

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

std::optional<std::string_view> forkNameFromDirSegment(std::string_view segment)
{
    if (segment == "berlin")
    {
        return "Berlin";
    }
    if (segment == "london")
    {
        return "London";
    }
    if (segment == "paris" || segment == "merge")
    {
        return "Paris";
    }
    if (segment == "shanghai")
    {
        return "Shanghai";
    }
    if (segment == "cancun")
    {
        return "Cancun";
    }
    if (segment == "prague")
    {
        return "Prague";
    }
    if (segment == "osaka")
    {
        return "Osaka";
    }
    return std::nullopt;
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

struct ManifestPathOverride
{
    std::string_view pathSuffix;
    std::string_view profileId;
};

constexpr ManifestPathOverride kManifestPathOverrides[] = {
    {"prague/eip7623_increase_calldata_cost", "eth-osaka"},
    {"prague/eip7702_set_code_tx", "eth-osaka"},
};

}  // namespace

std::optional<std::string_view> inferUpstreamForkFromPath(
    std::filesystem::path const& file, std::filesystem::path const& stateTestsRoot)
{
    auto const caseDir = relativeCaseDir(file, stateTestsRoot);
    if (!caseDir.has_value() || caseDir->empty())
    {
        return std::nullopt;
    }
    return forkNameFromDirSegment(caseDir->begin()->string());
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
    for (auto const& entry : kManifestPathOverrides)
    {
        if (rel == entry.pathSuffix || rel.starts_with(std::string(entry.pathSuffix) + "/"))
        {
            return entry.profileId;
        }
    }

    auto const segment = caseDir->begin()->string();
    if (segment == "shanghai")
    {
        return std::string_view("eth-shanghai");
    }
    if (segment == "cancun")
    {
        return std::string_view("eth-cancun");
    }
    if (segment == "prague")
    {
        return std::string_view("eth-prague");
    }
    if (segment == "osaka")
    {
        return std::string_view("eth-osaka");
    }
    return std::nullopt;
}

std::vector<ResolvedSubtestRun> resolveRunsForCase(StateTestCase const& test,
    std::filesystem::path const& sourceFile, std::filesystem::path const& stateTestsRoot,
    std::vector<ForkProfile> const& profileFilter)
{
    auto const& registry = ForkProfileRegistry::instance();
    auto const manifestId = manifestProfileIdForPath(sourceFile, stateTestsRoot);
    std::vector<ResolvedSubtestRun> runs;

    auto filterCanRunPostFork = [&](std::string_view postForkKey) {
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

    auto isManifestPathOverride = [&]() {
        if (!manifestId.has_value())
        {
            return false;
        }
        auto const caseDir = relativeCaseDir(sourceFile, stateTestsRoot);
        if (!caseDir.has_value())
        {
            return false;
        }
        auto const rel = caseDir->generic_string();
        for (auto const& entry : kManifestPathOverrides)
        {
            if (rel == entry.pathSuffix || rel.starts_with(std::string(entry.pathSuffix) + "/"))
            {
                return true;
            }
        }
        return false;
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
                if (isManifestPathOverride() || profileMatchesFork(*profile, postForkKey))
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
