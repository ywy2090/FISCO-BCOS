#include "bcos-evm/eth-eest-test/EestGranularCli.h"

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

}  // namespace bcos::evm::reference_tests
