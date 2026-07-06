#pragma once

#include "bcos-evm/eth-eest-test/StateTestTypes.h"
#include "bcos-evm/eth/state/Account.hpp"
#include "bcos-evm/eth/state/BlockInfo.hpp"
#include "bcos-evm/eth/state/Transaction.hpp"
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/property_tree/ptree_fwd.hpp>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace bcos::evm::reference_tests
{

struct GstAuthorizationEntry
{
    bcos::u256 chainId;
    evmc_address address{};
    evmc_address authority{};
    uint64_t nonce{0};
    std::optional<uint64_t> yParity;
    bcos::bytes signatureR;
    bcos::bytes signatureS;
};

struct GstAccessListEntry
{
    evmc_address address{};
    std::vector<evmc_bytes32> storageKeys;
};

struct GstTransactionTemplate
{
    std::optional<evmc_address> sender;
    std::optional<std::string> to;
    std::vector<uint64_t> gasLimit;
    std::vector<bcos::bytes> data;
    std::vector<bcos::u256> value;
    bcos::u256 gasPrice{0};
    bcos::u256 maxFeePerGas{0};
    bcos::u256 maxPriorityFeePerGas{0};
    bcos::u256 maxFeePerBlobGas{0};
    std::vector<h256> blobVersionedHashes;
    uint64_t nonce{0};
    std::vector<GstAuthorizationEntry> authorizationList;
    bool authorizationListKeyPresent{false};
    std::vector<std::vector<GstAccessListEntry>> accessLists;
};

struct StateTestCase
{
    std::string name;
    std::string variantKey;
    std::filesystem::path sourcePath;
    state::BlockInfo env;
    state::Transaction tx;
    GstTransactionTemplate transaction;
    std::vector<std::pair<evmc_address, state::Account>> preState;
    std::map<std::string, std::vector<ExpectedPostState>> postByFork;
};

std::filesystem::path resolveEthereumTestsRoot();
void ensureGeneralStateTestsExtracted(std::filesystem::path const& ethereumTestsRoot);
std::string generalStateTestCaseId(
    std::filesystem::path const& ethereumTestsRoot, std::filesystem::path const& jsonPath);

std::vector<std::string> listGeneralStateTestVariantKeys(std::filesystem::path const& jsonPath);
std::vector<StateTestCase> loadGeneralStateTestFile(std::filesystem::path const& jsonPath);
StateTestCase loadGeneralStateTest(std::filesystem::path const& jsonPath,
    std::optional<std::string_view> variantKey = std::nullopt);
std::vector<StateSubtest> listSubtests(StateTestCase const& test, std::string_view fork);
std::vector<StateSubtest> tryListSubtests(StateTestCase const& test, std::string_view fork);
ExpectedPostState selectExpected(StateTestCase const& test, StateSubtest const& subtest);

/// Parse EEST/GST transaction JSON (scalar blockchain fields or GST variant arrays).
GstTransactionTemplate parseTransactionTemplate(boost::property_tree::ptree const& txTree);

/// Materialize index-0 GST variant fields into a state::Transaction.
state::Transaction materializeTransaction(GstTransactionTemplate const& transaction);

/// Build a single-variant GstTransactionTemplate from a plain transaction (block tests).
GstTransactionTemplate gstTransactionTemplateFromSimple(state::Transaction const& tx);

}  // namespace bcos::evm::reference_tests
