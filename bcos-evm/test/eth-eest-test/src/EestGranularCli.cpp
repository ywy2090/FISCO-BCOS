#include "bcos-evm/eth-eest-test/EestGranularCli.h"

#include <algorithm>
#include <array>
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
    static constexpr std::array<char const*, 4> kManifest16Profiles = {
        "eth-shanghai", "eth-cancun", "eth-prague", "eth-osaka"};

    std::vector<ForkProfile> profiles;
    auto const& registry = ForkProfileRegistry::instance();

    if (profileIds.empty())
    {
        for (auto const* id : kManifest16Profiles)
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
    }
    else
    {
        for (auto const& id : profileIds)
        {
            if (auto const profile = registry.findByProfileId(id))
            {
                profiles.push_back(*profile);
            }
        }
    }
    return profiles;
}

}  // namespace bcos::evm::reference_tests
