#define BOOST_TEST_MODULE IsthmusPostExecutionPolicyTest

#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/opstack/OpStackIsthmusRevision.h"
#include <boost/test/included/unit_test.hpp>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::evm::test
{
namespace
{
namespace fs = std::filesystem;

std::vector<std::string> collectSourceFiles(fs::path const& root)
{
    std::vector<std::string> files;
    if (!fs::exists(root))
    {
        return files;
    }
    for (auto const& entry : fs::recursive_directory_iterator(root))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        auto const ext = entry.path().extension().string();
        if (ext == ".cpp" || ext == ".h" || ext == ".hpp")
        {
            files.push_back(entry.path().string());
        }
    }
    return files;
}

bool fileContains(std::string const& path, std::string_view needle)
{
    std::ifstream in(path);
    if (!in.is_open())
    {
        return false;
    }
    std::string line;
    while (std::getline(in, line))
    {
        if (line.find(needle) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

fs::path opstackSourceRoot()
{
#ifdef BCOS_EVM_SOURCE_DIR
    return fs::path(BCOS_EVM_SOURCE_DIR) / "opstack";
#else
    return fs::path(__FILE__).parent_path().parent_path().parent_path() / "opstack";
#endif
}
}  // namespace

BOOST_AUTO_TEST_SUITE(IsthmusPostExecutionPolicyTest)

// op-geth disables block postExecution at Isthmus (state_processor.go:141).
// bcos-evm OpStack executes at tx granularity; there is no block postExecution layer.
BOOST_AUTO_TEST_CASE(isthmus_revision_config_is_prague_tx_level)
{
    auto const config = bcos::evm::makeIsthmusRevisionConfig();
    BOOST_CHECK_EQUAL(config.revision, EVMC_PRAGUE);
    BOOST_CHECK(config.eip7702);
    BOOST_CHECK(config.eip7623);
    BOOST_CHECK(config.warm_access);
}

// prague_post_execution was removed from RevisionConfig (dead profile-only flag).
// Criteria 14 is enforced by architectural scope + source scan below + CI gate.
BOOST_AUTO_TEST_CASE(revision_config_has_no_prague_post_execution_field)
{
    BOOST_CHECK_EQUAL(bcos::evm_standard::revisionConfigBoolFieldCount(), 12U);
}

BOOST_AUTO_TEST_CASE(opstack_sources_have_no_prague_block_post_execution_hooks)
{
    static constexpr std::string_view kForbidden[] = {
        "ParseDepositLogs",
        "ProcessWithdrawalQueue",
        "ProcessConsolidationQueue",
        "prague_post_execution",
        "praguePostExecution",
    };

    auto const root = opstackSourceRoot();
    BOOST_REQUIRE_MESSAGE(fs::exists(root), "opstack source root not found: " + root.string());

    for (auto const& file : collectSourceFiles(root))
    {
        for (auto const& needle : kForbidden)
        {
            BOOST_CHECK_MESSAGE(!fileContains(file, needle),
                "forbidden Prague postExecution hook '" << needle << "' in " << file);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::test
