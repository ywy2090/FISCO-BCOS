#include "bcos-evm/evm-reference-tests/EestStateTestLoader.h"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace bcos::evm::reference_tests
{
namespace
{

std::filesystem::path moduleAssetsRoot()
{
#ifdef EVM_REFERENCE_TESTS_SOURCE_DIR
    return std::filesystem::path(EVM_REFERENCE_TESTS_SOURCE_DIR) / "assets" / "eest";
#else
    return std::filesystem::path("bcos-evm/evm-reference-tests/assets/eest");
#endif
}

}  // namespace

std::filesystem::path resolveEestRoot()
{
    if (char const* overrideRoot = std::getenv("EEST_ROOT"))
    {
        return std::filesystem::path(overrideRoot);
    }
    return moduleAssetsRoot();
}

void ensureEestFixturesExtracted(std::filesystem::path const& eestRoot)
{
    auto const stateTests = eestRoot / "fixtures" / "state_tests";
    if (std::filesystem::exists(stateTests))
    {
        return;
    }

    auto const tarball = eestRoot / "fixtures_develop.tar.gz";
    if (!std::filesystem::exists(tarball))
    {
        throw std::runtime_error(
            "EEST fixtures missing; download fixtures_develop.tar.gz to " + eestRoot.string());
    }

    auto const command = "tar -xzf \"" + tarball.string() + "\" -C \"" + eestRoot.string() + "\"";
    if (std::system(command.c_str()) != 0)
    {
        throw std::runtime_error("Failed to extract EEST fixtures_develop.tar.gz");
    }
}

std::vector<StateTestCase> loadEestStateTestFile(std::filesystem::path const& jsonPath)
{
    return loadGeneralStateTestFile(jsonPath);
}

StateTestCase loadEestStateTest(
    std::filesystem::path const& jsonPath, std::optional<std::string_view> variantKey)
{
    return loadGeneralStateTest(jsonPath, variantKey);
}

std::vector<std::filesystem::path> listEestStateTestFiles(
    std::filesystem::path const& eestRoot, std::string_view forkFilter)
{
    auto const root = eestRoot / "fixtures" / "state_tests";
    if (!std::filesystem::exists(root))
    {
        throw std::runtime_error("EEST state_tests directory not found: " + root.string());
    }

    std::vector<std::filesystem::path> files;
    for (auto const& entry : std::filesystem::recursive_directory_iterator(
             root, std::filesystem::directory_options::skip_permission_denied))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
        {
            continue;
        }
        if (!forkFilter.empty())
        {
            auto const pathStr = entry.path().generic_string();
            if (pathStr.find(forkFilter) == std::string::npos)
            {
                continue;
            }
        }
        files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

}  // namespace bcos::evm::reference_tests
