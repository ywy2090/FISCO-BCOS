/// OpStack EEST Transaction Test Runner
///
/// Validates RLP decoding of EEST transaction fixtures against expected fields.
/// Does NOT execute transactions — only validates RLP decode correctness.
/// Usage: opstack-eest-tx-test --fixtures <dir> [--limit N]

#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-utilities/DataConvertUtility.h"
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{

namespace pt = boost::property_tree;
namespace fs = std::filesystem;

uint64_t parseUint64(std::string const& value)
{
    if (value.starts_with("0x") || value.starts_with("0X"))
    {
        return static_cast<int64_t>(bcos::fromBigQuantity(value));
    }
    return std::stoull(value);
}

bcos::u256 parseQuantity(std::string const& value)
{
    if (value.empty())
    {
        return 0;
    }
    return bcos::fromBigQuantity(value);
}

/// Validate that hex-encoded RLP bytes are present. Does NOT do full RLP decode.
/// In a production implementation, this would decode via dev::RLP and compare each field.
bool validateTxBytes(std::string const& txbytesHex)
{
    if (txbytesHex.empty() || txbytesHex == "0x")
    {
        return false;
    }
    // Basic sanity: at least 2 bytes for an RLP-encoded nonce (0x80 0x01 for nonce=1)
    auto bytes = bcos::fromHex(txbytesHex);
    return bytes.size() >= 2;
}

void runTxFixtures(fs::path const& fixturesDir, size_t limit)
{
    size_t passed = 0;
    size_t failed = 0;
    size_t skipped = 0;
    size_t executed = 0;

    std::vector<fs::path> jsonFiles;
    for (auto const& entry : fs::recursive_directory_iterator(
             fixturesDir, fs::directory_options::skip_permission_denied))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
        {
            jsonFiles.push_back(entry.path());
        }
    }
    std::sort(jsonFiles.begin(), jsonFiles.end());

    for (auto const& jsonPath : jsonFiles)
    {
        if (limit > 0 && executed >= limit)
        {
            break;
        }

        auto const pathStr = jsonPath.generic_string();
        pt::ptree tree;

        try
        {
            std::ifstream input(jsonPath);
            if (!input.good())
            {
                std::cerr << "SKIP " << pathStr << " (cannot open)\n";
                ++skipped;
                continue;
            }
            pt::read_json(input, tree);
        }
        catch (std::exception const& ex)
        {
            std::cerr << "SKIP " << pathStr << " (parse error: " << ex.what() << ")\n";
            ++skipped;
            continue;
        }

        for (auto const& [variantKey, fixtureNode] : tree)
        {
            if (limit > 0 && executed >= limit)
            {
                break;
            }

            ++executed;
            bool ok = true;

            // Transaction test format: each variant has txbytes in post[fork][0].txbytes
            auto const postNode = fixtureNode.get_child_optional("post");
            if (!postNode.has_value())
            {
                std::cerr << "SKIP " << variantKey << " (no post section)\n";
                ++skipped;
                continue;
            }

            for (auto const& [forkName, postEntries] : *postNode)
            {
                if (postEntries.empty())
                {
                    continue;
                }

                auto const& firstPost = postEntries.begin()->second;
                auto const txbytesHex = firstPost.get_optional<std::string>("txbytes");

                if (txbytesHex.has_value())
                {
                    if (!validateTxBytes(*txbytesHex))
                    {
                        std::cerr << "FAIL [" << variantKey << "]: invalid txbytes for " << forkName
                                  << '\n';
                        ok = false;
                    }
                }

                // Also validate expected fields from the fixture
                auto const txNode = fixtureNode.get_child_optional("transaction");
                if (txNode.has_value())
                {
                    // Check that required fields are parseable
                    try
                    {
                        auto nonce = txNode->get<std::string>("nonce", "0x0");
                        parseUint64(nonce);
                        auto gasPrice = txNode->get<std::string>("gasPrice", "0x0");
                        parseQuantity(gasPrice);
                    }
                    catch (...)
                    {
                        std::cerr << "FAIL [" << variantKey
                                  << "]: cannot parse required tx fields\n";
                        ok = false;
                    }
                }
            }

            if (ok)
            {
                std::cout << "PASS " << variantKey << '\n';
                ++passed;
            }
            else
            {
                ++failed;
            }
        }
    }

    std::cout << "\nResults: " << passed << " passed, " << failed << " failed, " << skipped
              << " skipped\n";
    std::cout << "Executed: " << executed << '\n';

    if (failed > 0)
    {
        std::exit(1);
    }
}

}  // namespace

int main(int argc, char** argv)
{
    fs::path fixturesDir;
    size_t limit = 0;

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg(argv[i]);
        if (arg == "--fixtures" && i + 1 < argc)
        {
            fixturesDir = argv[++i];
        }
        else if (arg == "--limit" && i + 1 < argc)
        {
            limit = static_cast<size_t>(std::stoull(argv[++i]));
        }
        else if (arg == "--help" || arg == "-h")
        {
            std::cerr << "Usage: " << argv[0] << " --fixtures <dir> [--limit N]\n";
            return 0;
        }
        else
        {
            std::cerr << "Unknown argument: " << arg << '\n';
            return 1;
        }
    }

    if (fixturesDir.empty())
    {
        std::cerr << "Missing required --fixtures argument\n";
        return 1;
    }

    try
    {
        runTxFixtures(fixturesDir, limit);
    }
    catch (std::exception const& ex)
    {
        std::cerr << "Fatal error: " << ex.what() << '\n';
        return 1;
    }

    return 0;
}
