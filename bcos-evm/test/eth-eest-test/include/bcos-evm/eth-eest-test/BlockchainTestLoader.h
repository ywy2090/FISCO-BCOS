#pragma once

#include "bcos-evm/eth-eest-test/BlockchainTestTypes.h"
#include <boost/property_tree/ptree.hpp>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::evm::reference_tests
{
/// Legacy GST fixtures under blockchain_tests/static/ carry fork in JSON `network`.
inline bool isStaticBlockchainFixture(std::filesystem::path const& file)
{
    return file.generic_string().find("blockchain_tests/static/") != std::string::npos;
}

/// Native fork dirs use the first path segment after blockchain_tests/; static returns empty.
inline std::string inferBlockchainForkFromPath(std::filesystem::path const& file)
{
    if (isStaticBlockchainFixture(file))
        return {};

    std::string forkStr = "Cancun";
    auto pathStr = file.generic_string();
    constexpr std::string_view bcPrefix = "blockchain_tests/";
    auto statePos = pathStr.find(bcPrefix);
    if (statePos != std::string::npos)
    {
        auto start = statePos + bcPrefix.size();
        auto end = pathStr.find('/', start);
        if (end != std::string::npos)
        {
            forkStr = pathStr.substr(start, end - start);
            if (!forkStr.empty())
                forkStr[0] =
                    static_cast<char>(std::toupper(static_cast<unsigned char>(forkStr[0])));
        }
    }
    return forkStr;
}

/// Parse every test object in an EEST blockchain fixture ptree.
/// Skips engine-only objects (no `pre` + `genesisBlockHeader`).
std::vector<BlockchainTest> loadBlockchainTests(boost::property_tree::ptree const& root);

TestBlockHeader parseBlockHeader(boost::property_tree::ptree const& j);
TestBlock parseTestBlock(boost::property_tree::ptree const& j, std::string_view network);
}  // namespace bcos::evm::reference_tests
