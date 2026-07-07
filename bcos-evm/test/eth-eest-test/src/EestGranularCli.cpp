#include "bcos-evm/eth-eest-test/EestGranularCli.h"

#include "bcos-evm/eth-eest-test/EestStateFullManifest.h"

#include <algorithm>
#include <array>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace bcos::evm::reference_tests
{
namespace
{

void splitCommaSeparated(std::string_view list, std::vector<std::string>& out)
{
    while (!list.empty())
    {
        auto const comma = list.find(',');
        auto const token = list.substr(0, comma);
        if (!token.empty())
            out.emplace_back(token);
        if (comma == std::string_view::npos)
            break;
        list.remove_prefix(comma + 1);
    }
}

[[noreturn]] void throwUsage(char const* prog, char const* reason)
{
    throw std::invalid_argument(std::string(reason) + "\nUsage: " + prog +
                                " <path> [<path>...] [-k SUBSTR] [--fork-profiles IDS]\n"
                                "       [--gtest_filter=...]   # standard GTest flags");
}

std::string joinProfileIds(std::vector<std::string_view> const& ids)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < ids.size(); ++i)
    {
        if (i != 0)
        {
            out << ", ";
        }
        out << ids[i];
    }
    return out.str();
}

[[noreturn]] void throwUnknownProfileIds(std::vector<std::string> const& unknown)
{
    auto const& registry = ForkProfileRegistry::instance();
    std::ostringstream msg;
    msg << "unknown fork profile id(s): ";
    for (std::size_t i = 0; i < unknown.size(); ++i)
    {
        if (i != 0)
        {
            msg << ", ";
        }
        msg << unknown[i];
    }
    msg << "; known: " << joinProfileIds(registry.allProfileIds());
    throw std::invalid_argument(msg.str());
}

/// WP-HIST profiles appended to manifest defaults for granular full-tree runs.
static constexpr std::array<char const*, 2> kGranularHistoricalProfileIds = {
    "eth-homestead",
    "eth-berlin",
};

void appendProfileIdIfKnown(
    std::vector<ForkProfile>& profiles, ForkProfileRegistry const& registry, std::string_view id)
{
    if (auto const profile = registry.findByProfileId(id))
    {
        if (std::ranges::none_of(profiles, [&](ForkProfile const& existing) {
                return existing.profileId == profile->profileId;
            }))
        {
            profiles.push_back(*profile);
        }
    }
}

}  // namespace

EestGranularCliOptions parseEestGranularCliRemaining(int argc, char** argv)
{
    EestGranularCliOptions opts;
    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg(argv[i]);
        if (arg == "-k")
        {
            if (i + 1 >= argc)
                throwUsage(argv[0], "missing argument for -k");
            opts.nameFilter = argv[++i];
        }
        else if (arg == "--fork-profiles")
        {
            if (i + 1 >= argc)
                throwUsage(argv[0], "missing argument for --fork-profiles");
            splitCommaSeparated(argv[++i], opts.profileIds);
        }
        else if (arg.starts_with('-'))
        {
            throwUsage(argv[0], ("unknown flag: " + std::string(arg)).c_str());
        }
        else
        {
            opts.paths.emplace_back(arg);
        }
    }
    return opts;
}

std::vector<ForkProfile> buildRunnerConfig(std::vector<std::string> const& profileIds)
{
    std::vector<ForkProfile> profiles;
    auto const& registry = ForkProfileRegistry::instance();

    if (profileIds.empty())
    {
        for (auto const id : StateFullManifestIndex::instance().defaultGranularProfileIds())
        {
            appendProfileIdIfKnown(profiles, registry, id);
        }
        for (auto const id : kGranularHistoricalProfileIds)
        {
            appendProfileIdIfKnown(profiles, registry, id);
        }
        return profiles;
    }

    std::vector<std::string> unknown;
    for (auto const& id : profileIds)
    {
        if (auto const profile = registry.findByProfileId(id))
        {
            if (std::ranges::none_of(profiles, [&](ForkProfile const& existing) {
                    return existing.profileId == profile->profileId;
                }))
            {
                profiles.push_back(*profile);
            }
        }
        else
        {
            unknown.push_back(id);
        }
    }

    if (!unknown.empty())
    {
        throwUnknownProfileIds(unknown);
    }
    return profiles;
}

}  // namespace bcos::evm::reference_tests
