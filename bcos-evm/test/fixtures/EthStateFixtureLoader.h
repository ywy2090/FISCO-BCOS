/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief Shared JSON fixture loader for eth state / ethReferenceExecute tests.
 * @file EthStateFixtureLoader.h
 */

#pragma once

#include "bcos-evm/eth/Eip7702.h"
#include "bcos-evm/eth/execution/BlockInfoBuilder.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/Transition.hpp"
#include "bcos-utilities/DataConvertUtility.h"
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bcos::evm::test::fixtures
{
namespace pt = boost::property_tree;

struct ExpectedPostAccount
{
    evmc_address address{};
    std::optional<bcos::u256> balance;
    std::optional<bool> codeEmpty;
    std::optional<bool> codeNonempty;
};

struct ExpectedResult
{
    evmc_status_code status = EVMC_SUCCESS;
    int64_t gasUsed = 0;
    int64_t gasUsedTolerance = 0;
    int64_t gasUsedExecutor = 0;
    int64_t gasUsedExecutorTolerance = 0;
    int64_t settledGasUsed = 0;
    bcos::bytes output;
    size_t logs = 0;
    std::vector<ExpectedPostAccount> post;
};

struct FixtureCase
{
    std::string name;
    std::string source;
    std::string revision;
    state::Transaction tx;
    state::BlockInfo block;
    state::TransactionProperties txProps;
    bool authorizationListPresent{false};
    std::vector<SetCodeAuthorization> authorizations;
    std::vector<std::pair<evmc_address, state::Account>> preState;
    ExpectedResult expected;
};

inline evmc_status_code parseStatus(std::string_view status)
{
    if (status == "EVMC_SUCCESS" || status == "SUCCESS")
    {
        return EVMC_SUCCESS;
    }
    if (status == "EVMC_REVERT" || status == "REVERT")
    {
        return EVMC_REVERT;
    }
    if (status == "EVMC_OUT_OF_GAS" || status == "OUT_OF_GAS")
    {
        return EVMC_OUT_OF_GAS;
    }
    BOOST_FAIL("Unsupported status in fixture: " << status);
    return EVMC_INTERNAL_ERROR;
}

inline evmc_address parseAddress(const std::string& value)
{
    auto const address = state::parseHexAddress(value);
    BOOST_REQUIRE_MESSAGE(
        !(state::isZeroAddress(address) && value != "0x0000000000000000000000000000000000000000"),
        "Invalid address literal: " << value);
    return address;
}

inline bcos::u256 parseU256(std::string_view value)
{
    if (value.empty())
    {
        return 0;
    }
    return bcos::fromBigQuantity(value);
}

inline bcos::bytes parseBytes(const std::string& value)
{
    return bcos::fromHex(value);
}

inline state::BlockInfo parseBlock(const pt::ptree& tree)
{
    auto builder = bcos::evm::execution::BlockInfoBuilder()
                       .number(tree.get<int64_t>("number", 0))
                       .timestamp(tree.get<int64_t>("timestamp", 0))
                       .gasLimit(tree.get<int64_t>("gas_limit", 0))
                       .coinbase(parseAddress(tree.get<std::string>(
                           "coinbase", "0x0000000000000000000000000000000000000000")))
                       .baseFee(parseU256(tree.get<std::string>("base_fee", "0x0")))
                       .chainId(parseU256(tree.get<std::string>("chain_id", "0x1")))
                       .blobBaseFee(parseU256(tree.get<std::string>("blob_base_fee", "0x0")));
    return builder.build();
}

inline FixtureCase loadFixture(std::filesystem::path const& path)
{
    std::ifstream file(path);
    BOOST_REQUIRE_MESSAGE(file.good(), "Failed to open fixture: " << path.string());

    pt::ptree tree;
    pt::read_json(file, tree);

    FixtureCase fixture;
    fixture.name = tree.get<std::string>("name");
    fixture.source = tree.get<std::string>("source", "");
    fixture.revision = tree.get<std::string>("revision", "prague");

    auto const& txTree = tree.get_child("tx");
    fixture.tx.from = parseAddress(txTree.get<std::string>("from"));
    if (auto to = txTree.get_optional<std::string>("to"); to.has_value())
    {
        fixture.tx.to = parseAddress(*to);
    }
    fixture.tx.gasLimit = txTree.get<int64_t>("gas_limit");
    fixture.tx.gasPrice = parseU256(txTree.get<std::string>("gas_price", "0x0"));
    fixture.tx.value = parseU256(txTree.get<std::string>("value", "0x0"));
    fixture.tx.nonce = txTree.get<uint64_t>("nonce", 0);
    fixture.tx.data = parseBytes(txTree.get<std::string>("data", "0x"));
    fixture.authorizationListPresent = txTree.get<bool>("authorization_list_present", false);
    if (auto authList = txTree.get_child_optional("authorizations"); authList.has_value())
    {
        for (auto const& authNode : *authList)
        {
            auto const& authTree = authNode.second;
            SetCodeAuthorization authorization;
            if (auto chainId = authTree.get_optional<std::string>("chain_id"); chainId.has_value())
            {
                authorization.chainId = parseU256(*chainId);
            }
            authorization.authority = parseAddress(authTree.get<std::string>("authority"));
            authorization.address = parseAddress(authTree.get<std::string>("address"));
            authorization.nonce = authTree.get<uint64_t>("nonce", 0);
            fixture.authorizations.push_back(std::move(authorization));
        }
    }

    fixture.txProps.warmCoinbase = tree.get<bool>("tx_props.warm_coinbase", true);
    fixture.txProps.warmDestination = tree.get<bool>("tx_props.warm_destination", true);
    fixture.txProps.isStatic = tree.get<bool>("tx_props.is_static", false);

    fixture.block = parseBlock(tree.get_child("block"));

    if (auto pre = tree.get_child_optional("pre"); pre.has_value())
    {
        for (auto const& accountNode : *pre)
        {
            auto const& accountTree = accountNode.second;
            auto const address = parseAddress(accountTree.get<std::string>("address"));
            state::Account account;
            account.balance = parseU256(accountTree.get<std::string>("balance", "0x0"));
            account.nonce = accountTree.get<uint64_t>("nonce", 0);
            account.code = parseBytes(accountTree.get<std::string>("code", "0x"));
            fixture.preState.emplace_back(address, std::move(account));
        }
    }

    auto const& expectedTree = tree.get_child("expected");
    fixture.expected.status = parseStatus(expectedTree.get<std::string>("status", "EVMC_SUCCESS"));
    fixture.expected.gasUsed = expectedTree.get<int64_t>("gas_used", 0);
    fixture.expected.gasUsedTolerance = expectedTree.get<int64_t>("gas_used_tolerance", 0);
    fixture.expected.gasUsedExecutor = expectedTree.get<int64_t>("gas_used_executor", 0);
    fixture.expected.gasUsedExecutorTolerance =
        expectedTree.get<int64_t>("gas_used_executor_tolerance", 0);
    fixture.expected.settledGasUsed = expectedTree.get<int64_t>("settled_gas_used", 0);
    fixture.expected.output = parseBytes(expectedTree.get<std::string>("output", "0x"));
    fixture.expected.logs = expectedTree.get<size_t>("logs", 0);

    if (auto post = expectedTree.get_child_optional("post"); post.has_value())
    {
        for (auto const& postNode : *post)
        {
            auto const& postTree = postNode.second;
            ExpectedPostAccount expectedPost;
            expectedPost.address = parseAddress(postTree.get<std::string>("address"));
            if (auto balance = postTree.get_optional<std::string>("balance"); balance.has_value())
            {
                expectedPost.balance = parseU256(*balance);
            }
            if (postTree.get_optional<bool>("code_empty").value_or(false))
            {
                expectedPost.codeEmpty = true;
            }
            if (postTree.get_optional<bool>("code_nonempty").value_or(false))
            {
                expectedPost.codeNonempty = true;
            }
            fixture.expected.post.push_back(std::move(expectedPost));
        }
    }

    return fixture;
}

inline bool sameBytes(bcos::bytes const& lhs, bcos::bytes const& rhs)
{
    return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

inline std::vector<std::filesystem::path> listRootFixtureFiles(std::filesystem::path const& rootDir)
{
    return {
        rootDir / "prague_call_empty_account.json",
        rootDir / "prague_call_return_word.json",
        rootDir / "prague_call_revert.json",
        rootDir / "prague_create_empty_initcode.json",
        rootDir / "prague_selfdestruct.json",
    };
}

inline std::vector<std::filesystem::path> listAllFixtureFiles(std::filesystem::path const& rootDir)
{
    std::vector<std::filesystem::path> paths;

    if (std::filesystem::exists(rootDir))
    {
        for (auto const& entry : std::filesystem::directory_iterator(rootDir))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".json")
            {
                paths.push_back(entry.path());
            }
        }
    }

    auto const importedDir = rootDir / "imported";
    if (std::filesystem::exists(importedDir))
    {
        for (auto const& entry : std::filesystem::directory_iterator(importedDir))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".json")
            {
                paths.push_back(entry.path());
            }
        }
    }

    std::sort(paths.begin(), paths.end());
    return paths;
}

inline std::vector<std::filesystem::path> listCreateSettlementFixtureFiles(
    std::filesystem::path const& rootDir)
{
    std::vector<std::filesystem::path> paths;
    auto const dir = rootDir / "create_settlement";
    if (!std::filesystem::exists(dir))
    {
        return paths;
    }
    for (auto const& entry : std::filesystem::directory_iterator(dir))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
        {
            paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

}  // namespace bcos::evm::test::fixtures
