#include "bcos-evm/evm-reference-tests/GeneralStateTestLoader.h"

#include "bcos-evm/eth/AccessList.h"
#include "bcos-evm/eth/Eip7702.h"
#include "bcos-evm/eth/gas/Eip4844.h"
#include "bcos-evm/eth/state/Account.hpp"
#include "bcos-evm/eth/state/hash_utils.hpp"
#include "bcos-utilities/DataConvertUtility.h"
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace bcos::evm::reference_tests
{
namespace
{
namespace pt = boost::property_tree;

[[noreturn]] void throwLoaderError(std::string const& message)
{
    throw std::runtime_error(message);
}

bcos::u256 parseQuantity(std::string const& value)
{
    if (value.empty())
    {
        return 0;
    }
    return bcos::fromBigQuantity(value);
}

uint64_t parseUint64(std::string const& value)
{
    if (value.starts_with("0x") || value.starts_with("0X"))
    {
        return static_cast<uint64_t>(parseQuantity(value));
    }
    return std::stoull(value);
}

bcos::bytes parseHexBytes(std::string const& value)
{
    if (value.empty() || value == "0x")
    {
        return {};
    }
    return bcos::fromHex(value);
}

evmc_bytes32 parseBytes32(std::string const& value)
{
    auto const bytes = parseHexBytes(value);
    evmc_bytes32 out{};
    if (bytes.size() > sizeof(out.bytes))
    {
        throwLoaderError("Invalid bytes32 literal: " + value);
    }
    if (!bytes.empty())
    {
        std::memcpy(out.bytes + sizeof(out.bytes) - bytes.size(), bytes.data(), bytes.size());
    }
    return out;
}

std::vector<std::string> parseStringArray(pt::ptree const& node, char const* field)
{
    std::vector<std::string> values;
    for (auto const& [key, child] : node.get_child(field))
    {
        static_cast<void>(key);
        values.push_back(child.get_value<std::string>());
    }
    return values;
}

std::vector<uint64_t> parseUint64Array(pt::ptree const& node, char const* field)
{
    std::vector<uint64_t> values;
    for (auto const& [key, child] : node.get_child(field))
    {
        static_cast<void>(key);
        values.push_back(parseUint64(child.get_value<std::string>()));
    }
    return values;
}

state::BlockInfo parseEnv(pt::ptree const& envTree)
{
    state::BlockInfo env;
    env.number = static_cast<int64_t>(parseUint64(envTree.get<std::string>("currentNumber")));
    env.timestamp = static_cast<int64_t>(parseUint64(envTree.get<std::string>("currentTimestamp")));
    env.gasLimit = static_cast<int64_t>(parseUint64(envTree.get<std::string>("currentGasLimit")));
    env.coinbase = state::parseHexAddress(envTree.get<std::string>("currentCoinbase"));

    if (auto const random = envTree.get_optional<std::string>("currentRandom"))
    {
        env.prevRandao = parseBytes32(*random);
    }
    else if (auto const difficulty = envTree.get_optional<std::string>("currentDifficulty"))
    {
        env.prevRandao = parseBytes32(*difficulty);
    }

    if (auto const baseFee = envTree.get_optional<std::string>("currentBaseFee"))
    {
        env.baseFee = parseQuantity(*baseFee);
    }

    if (auto const excessBlobGas = envTree.get_optional<std::string>("currentExcessBlobGas"))
    {
        env.blobBaseFee = gas::calcBlobBaseFee(parseUint64(*excessBlobGas));
    }

    return env;
}

std::vector<std::pair<evmc_address, state::Account>> parsePreState(pt::ptree const& preTree)
{
    std::vector<std::pair<evmc_address, state::Account>> preState;
    for (auto const& [addressHex, accountNode] : preTree)
    {
        auto const address = state::parseHexAddress(addressHex);
        state::Account account;
        account.balance = parseQuantity(accountNode.get<std::string>("balance", "0x0"));
        account.nonce = parseUint64(accountNode.get<std::string>("nonce", "0x0"));
        account.code = parseHexBytes(accountNode.get<std::string>("code", "0x"));

        if (auto const storage = accountNode.get_child_optional("storage"))
        {
            for (auto const& [slotHex, valueNode] : *storage)
            {
                account.storage.emplace(
                    parseBytes32(slotHex), parseBytes32(valueNode.get_value<std::string>()));
            }
        }

        preState.emplace_back(address, std::move(account));
    }
    return preState;
}

std::vector<GstAuthorizationEntry> parseAuthorizationList(pt::ptree const& txTree)
{
    std::vector<GstAuthorizationEntry> authorizations;
    auto const authNode = txTree.get_child_optional("authorizationList");
    if (!authNode.has_value())
    {
        return authorizations;
    }

    for (auto const& [key, entryNode] : *authNode)
    {
        static_cast<void>(key);
        GstAuthorizationEntry entry;
        entry.chainId = parseQuantity(entryNode.get<std::string>("chainId", "0x0"));
        entry.address = state::parseHexAddress(entryNode.get<std::string>("address"));
        entry.nonce = parseUint64(entryNode.get<std::string>("nonce", "0x0"));
        if (auto const yParity = entryNode.get_optional<std::string>("yParity"))
        {
            entry.yParity = parseUint64(*yParity);
        }
        else if (auto const v = entryNode.get_optional<std::string>("v"))
        {
            entry.yParity = parseUint64(*v);
        }
        if (auto const r = entryNode.get_optional<std::string>("r"))
        {
            entry.signatureR = parseHexBytes(*r);
        }
        if (auto const s = entryNode.get_optional<std::string>("s"))
        {
            entry.signatureS = parseHexBytes(*s);
        }
        if (auto const signer = entryNode.get_optional<std::string>("signer"))
        {
            entry.authority = state::parseHexAddress(*signer);
        }
        authorizations.push_back(std::move(entry));
    }
    return authorizations;
}

std::vector<std::vector<GstAccessListEntry>> parseAccessLists(pt::ptree const& txTree)
{
    std::vector<std::vector<GstAccessListEntry>> accessLists;
    auto const listsNode = txTree.get_child_optional("accessLists");
    if (!listsNode.has_value())
    {
        return accessLists;
    }

    for (auto const& [listKey, listNode] : *listsNode)
    {
        static_cast<void>(listKey);
        std::vector<GstAccessListEntry> entries;
        for (auto const& [entryKey, entryNode] : listNode)
        {
            static_cast<void>(entryKey);
            GstAccessListEntry entry;
            entry.address = state::parseHexAddress(entryNode.get<std::string>("address"));
            if (auto const storage = entryNode.get_child_optional("storageKeys"))
            {
                for (auto const& [slotKey, slotNode] : *storage)
                {
                    static_cast<void>(slotKey);
                    entry.storageKeys.push_back(parseBytes32(slotNode.get_value<std::string>()));
                }
            }
            entries.push_back(std::move(entry));
        }
        accessLists.push_back(std::move(entries));
    }
    return accessLists;
}

GstTransactionTemplate parseTransactionTemplate(pt::ptree const& txTree)
{
    GstTransactionTemplate transaction;
    transaction.nonce = parseUint64(txTree.get<std::string>("nonce", "0x0"));

    if (auto const gasPrice = txTree.get_optional<std::string>("gasPrice"))
    {
        transaction.gasPrice = parseQuantity(*gasPrice);
    }
    if (auto const maxFee = txTree.get_optional<std::string>("maxFeePerGas"))
    {
        transaction.maxFeePerGas = parseQuantity(*maxFee);
    }
    if (auto const maxPriority = txTree.get_optional<std::string>("maxPriorityFeePerGas"))
    {
        transaction.maxPriorityFeePerGas = parseQuantity(*maxPriority);
    }
    if (auto const to = txTree.get_optional<std::string>("to"))
    {
        transaction.to = *to;
    }

    transaction.gasLimit = parseUint64Array(txTree, "gasLimit");
    auto const dataStrings = parseStringArray(txTree, "data");
    transaction.data.reserve(dataStrings.size());
    for (auto const& item : dataStrings)
    {
        transaction.data.push_back(parseHexBytes(item));
    }

    for (auto const& [key, child] : txTree.get_child("value"))
    {
        static_cast<void>(key);
        transaction.value.push_back(parseQuantity(child.get_value<std::string>()));
    }

    if (auto const sender = txTree.get_optional<std::string>("sender"))
    {
        transaction.sender = state::parseHexAddress(*sender);
    }

    transaction.authorizationListKeyPresent =
        txTree.get_child_optional("authorizationList").has_value();
    transaction.authorizationList = parseAuthorizationList(txTree);
    transaction.accessLists = parseAccessLists(txTree);

    if (auto const maxBlobFee = txTree.get_optional<std::string>("maxFeePerBlobGas"))
    {
        transaction.maxFeePerBlobGas = parseQuantity(*maxBlobFee);
    }
    if (auto const blobHashes = txTree.get_child_optional("blobVersionedHashes"))
    {
        for (auto const& [key, hashNode] : *blobHashes)
        {
            static_cast<void>(key);
            transaction.blobVersionedHashes.emplace_back(
                state::fromEvmC(parseBytes32(hashNode.get_value<std::string>())));
        }
    }

    return transaction;
}

state::Transaction materializeTransaction(GstTransactionTemplate const& transaction)
{
    state::Transaction tx;
    if (transaction.sender.has_value())
    {
        tx.from = *transaction.sender;
    }
    if (transaction.to.has_value() && !transaction.to->empty())
    {
        tx.to = state::parseHexAddress(*transaction.to);
    }
    tx.nonce = transaction.nonce;
    tx.gasPrice = transaction.gasPrice != 0 ? transaction.gasPrice : transaction.maxFeePerGas;
    if (!transaction.gasLimit.empty())
    {
        tx.gasLimit = static_cast<int64_t>(transaction.gasLimit.front());
    }
    if (!transaction.data.empty())
    {
        tx.data = transaction.data.front();
    }
    if (!transaction.value.empty())
    {
        tx.value = transaction.value.front();
    }
    return tx;
}

ExpectedPostState parsePostState(pt::ptree const& postTree)
{
    ExpectedPostState expected;
    if (auto const exception = postTree.get_optional<std::string>("expectException"))
    {
        expected.expectException = *exception;
    }
    if (auto const hash = postTree.get_optional<std::string>("hash"))
    {
        expected.stateRoot = parseBytes32(*hash);
    }
    if (auto const logs = postTree.get_optional<std::string>("logs"))
    {
        expected.logsHash = parseBytes32(*logs);
    }

    if (auto const indexes = postTree.get_child_optional("indexes"))
    {
        expected.dataIndex = indexes->get<int>("data", 0);
        expected.gasIndex = indexes->get<int>("gas", 0);
        expected.valueIndex = indexes->get<int>("value", 0);
    }
    return expected;
}

std::map<std::string, std::vector<ExpectedPostState>> parsePostByFork(pt::ptree const& postTree)
{
    std::map<std::string, std::vector<ExpectedPostState>> postByFork;
    for (auto const& [forkName, forkPosts] : postTree)
    {
        std::vector<ExpectedPostState> posts;
        for (auto const& [key, postNode] : forkPosts)
        {
            static_cast<void>(key);
            posts.push_back(parsePostState(postNode));
        }
        postByFork.emplace(forkName, std::move(posts));
    }
    return postByFork;
}

StateTestCase parseStateTestBody(
    pt::ptree const& body, std::string variantKey, std::filesystem::path const& sourcePath)
{
    StateTestCase testCase;
    testCase.variantKey = variantKey;
    testCase.name = variantKey;
    testCase.sourcePath = sourcePath;
    testCase.env = parseEnv(body.get_child("env"));
    if (auto const config = body.get_child_optional("config"))
    {
        if (auto const chainId = config->get_optional<std::string>("chainid"))
        {
            testCase.env.chainId = parseQuantity(*chainId);
        }
    }
    testCase.preState = parsePreState(body.get_child("pre"));
    testCase.transaction = parseTransactionTemplate(body.get_child("transaction"));
    testCase.tx = materializeTransaction(testCase.transaction);
    testCase.postByFork = parsePostByFork(body.get_child("post"));
    if (testCase.postByFork.empty())
    {
        throwLoaderError("GST case has empty post section: " + sourcePath.string());
    }
    return testCase;
}

pt::ptree readJsonTree(std::filesystem::path const& jsonPath)
{
    std::ifstream input(jsonPath);
    if (!input.good())
    {
        throwLoaderError("Failed to open GST JSON: " + jsonPath.string());
    }

    pt::ptree tree;
    pt::read_json(input, tree);
    return tree;
}

bool isLegacyStateTestRoot(pt::ptree const& tree)
{
    return tree.get_child_optional("env").has_value() &&
           tree.get_child_optional("pre").has_value() &&
           tree.get_child_optional("transaction").has_value() &&
           tree.get_child_optional("post").has_value();
}

pt::ptree const* findDirectChild(pt::ptree const& tree, std::string const& key)
{
    for (auto const& [childKey, child] : tree)
    {
        if (childKey == key)
        {
            return &child;
        }
    }
    return nullptr;
}

pt::ptree const& requireDirectChild(pt::ptree const& tree, std::string const& key)
{
    if (auto const* child = findDirectChild(tree, key))
    {
        return *child;
    }
    throwLoaderError("Unknown GST variantKey '" + key + "'");
}

StateTestCase loadFromLegacyRoot(pt::ptree const& tree, std::filesystem::path const& jsonPath)
{
    auto const variantKey = jsonPath.stem().string();
    return parseStateTestBody(tree, variantKey, jsonPath);
}

std::vector<std::string> sortedVariantKeys(pt::ptree const& tree)
{
    std::vector<std::string> keys;
    keys.reserve(tree.size());
    for (auto const& [key, child] : tree)
    {
        static_cast<void>(child);
        keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

std::vector<StateTestCase> loadAllFromNamedMap(
    pt::ptree const& tree, std::filesystem::path const& jsonPath)
{
    if (tree.empty())
    {
        throwLoaderError("GST JSON map is empty: " + jsonPath.string());
    }

    std::vector<StateTestCase> cases;
    for (auto const& key : sortedVariantKeys(tree))
    {
        cases.push_back(parseStateTestBody(requireDirectChild(tree, key), key, jsonPath));
    }
    return cases;
}

StateTestCase loadOneFromNamedMap(pt::ptree const& tree, std::filesystem::path const& jsonPath,
    std::optional<std::string_view> variantKey)
{
    auto const keys = sortedVariantKeys(tree);
    if (keys.empty())
    {
        throwLoaderError("GST JSON map is empty: " + jsonPath.string());
    }

    if (!variantKey.has_value())
    {
        if (keys.size() == 1)
        {
            return parseStateTestBody(
                requireDirectChild(tree, keys.front()), keys.front(), jsonPath);
        }
        throwLoaderError(
            "GST JSON contains " + std::to_string(keys.size()) +
            " variants; pass variantKey or use loadGeneralStateTestFile: " + jsonPath.string());
    }

    auto const key = std::string(*variantKey);
    return parseStateTestBody(requireDirectChild(tree, key), key, jsonPath);
}

bool forkMatches(std::string_view candidate, std::string_view requested)
{
    if (candidate == requested)
    {
        return true;
    }
    if (candidate.starts_with(requested) &&
        (candidate.size() == requested.size() || candidate[requested.size()] == '+' ||
            candidate[requested.size()] == '-'))
    {
        return true;
    }
    return candidate.find(requested) != std::string_view::npos;
}

std::filesystem::path moduleAssetsRoot()
{
#ifdef EVM_REFERENCE_TESTS_SOURCE_DIR
    return std::filesystem::path(EVM_REFERENCE_TESTS_SOURCE_DIR) / "assets" / "ethereum-tests";
#else
    return std::filesystem::path("bcos-evm/evm-reference-tests/assets/ethereum-tests");
#endif
}

}  // namespace

std::filesystem::path resolveEthereumTestsRoot()
{
    if (char const* overrideRoot = std::getenv("ETHEREUM_TESTS_ROOT"))
    {
        return std::filesystem::path(overrideRoot);
    }
    return moduleAssetsRoot();
}

void ensureGeneralStateTestsExtracted(std::filesystem::path const& ethereumTestsRoot)
{
    auto const gstDir = ethereumTestsRoot / "GeneralStateTests";
    if (std::filesystem::exists(gstDir))
    {
        return;
    }

    auto const tarball = ethereumTestsRoot / "fixtures_general_state_tests.tgz";
    if (!std::filesystem::exists(tarball))
    {
        throwLoaderError(
            "GeneralStateTests directory missing and fixtures tarball not found under " +
            ethereumTestsRoot.string());
    }

    auto const command = "tar -xzf \"" + tarball.string() + "\" -C \"" +
                         ethereumTestsRoot.string() + "\" GeneralStateTests";
    if (std::system(command.c_str()) != 0)
    {
        throwLoaderError("Failed to extract fixtures_general_state_tests.tgz");
    }
}

std::vector<std::string> listGeneralStateTestVariantKeys(std::filesystem::path const& jsonPath)
{
    auto const tree = readJsonTree(jsonPath);
    if (isLegacyStateTestRoot(tree))
    {
        return {jsonPath.stem().string()};
    }
    return sortedVariantKeys(tree);
}

std::vector<StateTestCase> loadGeneralStateTestFile(std::filesystem::path const& jsonPath)
{
    auto const tree = readJsonTree(jsonPath);
    if (isLegacyStateTestRoot(tree))
    {
        return {loadFromLegacyRoot(tree, jsonPath)};
    }
    return loadAllFromNamedMap(tree, jsonPath);
}

StateTestCase loadGeneralStateTest(
    std::filesystem::path const& jsonPath, std::optional<std::string_view> variantKey)
{
    auto const tree = readJsonTree(jsonPath);
    if (isLegacyStateTestRoot(tree))
    {
        if (variantKey.has_value() && *variantKey != jsonPath.stem().string())
        {
            throwLoaderError("Legacy GST JSON has a single implicit variantKey '" +
                             jsonPath.stem().string() + "': " + jsonPath.string());
        }
        return loadFromLegacyRoot(tree, jsonPath);
    }
    return loadOneFromNamedMap(tree, jsonPath, variantKey);
}

std::vector<StateSubtest> listSubtests(StateTestCase const& test, std::string_view fork)
{
    auto subtests = tryListSubtests(test, fork);
    if (subtests.empty())
    {
        throwLoaderError(
            "No GST subtests for fork '" + std::string(fork) + "' in " + test.sourcePath.string());
    }
    return subtests;
}

std::vector<StateSubtest> tryListSubtests(StateTestCase const& test, std::string_view fork)
{
    std::vector<StateSubtest> subtests;
    for (auto const& [forkName, posts] : test.postByFork)
    {
        if (!forkMatches(forkName, fork))
        {
            continue;
        }
        for (auto const& post : posts)
        {
            StateSubtest subtest;
            subtest.fork = forkName;
            subtest.dataIndex = post.dataIndex;
            subtest.gasIndex = post.gasIndex;
            subtest.valueIndex = post.valueIndex;
            subtests.push_back(subtest);
        }
    }
    return subtests;
}

ExpectedPostState selectExpected(StateTestCase const& test, StateSubtest const& subtest)
{
    auto const forkIt = test.postByFork.find(subtest.fork);
    if (forkIt == test.postByFork.end())
    {
        throwLoaderError("Unknown fork in subtest: " + subtest.fork);
    }

    for (auto const& post : forkIt->second)
    {
        if (post.dataIndex == subtest.dataIndex && post.gasIndex == subtest.gasIndex &&
            post.valueIndex == subtest.valueIndex)
        {
            return post;
        }
    }

    throwLoaderError("No expected post state for subtest indexes d" +
                     std::to_string(subtest.dataIndex) + "g" + std::to_string(subtest.gasIndex) +
                     "v" + std::to_string(subtest.valueIndex));
}

}  // namespace bcos::evm::reference_tests
