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
 * @brief End-to-end EthTransactionExecutorImpl fixture validation (Phase 1).
 * @file TestEthTransactionExecutorFixture.cpp
 */
#include "../bcos-transaction-executor/EthTransactionExecutorImpl.h"
#include "TestMemoryStorage.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "fixtures/EthFixtureStorageSeeder.h"
#include "fixtures/EthFixtureTransactionBuilder.h"
#include "fixtures/EthStateFixtureLoader.h"
#include "fixtures/ExecutorFixtureAssert.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>
#include <array>
#include <intx/intx.hpp>

namespace bcos::evm::test
{
using namespace fixtures;

class EthExecutorFixtureHarness
{
public:
    executor_v1::MutableStorage storage;
    std::shared_ptr<crypto::CryptoSuite> cryptoSuite = std::make_shared<crypto::CryptoSuite>(
        std::make_shared<crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionFactoryImpl transactionFactory{cryptoSuite};
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    executor_v1::EthTransactionExecutorImpl executor{receiptFactory, cryptoSuite->hashImpl()};
    int contextId = 0;

    EthExecutorFixtureHarness() { executor::GlobalHashImpl::g_hashImpl = cryptoSuite->hashImpl(); }

    ledger::LedgerConfig makeLedgerConfig(FixtureCase const& fixture)
    {
        ledger::LedgerConfig cfg;
        ledger::Features features;
        features.setGenesisFeatures(protocol::BlockVersion::MAX_VERSION);
        features.set(ledger::Features::Flag::feature_evm_cancun);
        features.set(ledger::Features::Flag::feature_evm_prague);
        features.set(ledger::Features::Flag::feature_evm_eip2929);
        features.set(ledger::Features::Flag::feature_balance);
        features.set(ledger::Features::Flag::feature_balance_policy1);
        cfg.setFeatures(features);
        cfg.setGasLimit({fixture.block.gasLimit, 0});
        cfg.setGasPrice({"0", 0});
        evmc_uint256be chainId{};
        intx::be::store(
            chainId.bytes, intx::uint256{fixture.block.chainId.template convert_to<uint64_t>()});
        cfg.setChainId(chainId);
        return cfg;
    }

    bcostars::protocol::BlockHeaderImpl makeBlockHeader(FixtureCase const& fixture)
    {
        bcostars::protocol::BlockHeaderImpl header;
        header.setVersion(static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION));
        int64_t blockNumber = fixture.block.number;
        if (fixture.revision == "prague")
        {
            bool const needsGatedEips =
                fixture.authorizationListPresent ||
                (fixture.tx.to.has_value() && fixture.tx.to->bytes[19] == 0x0b);
            if (needsGatedEips)
            {
                blockNumber = 22'000'000;
            }
        }
        else if (fixture.revision == "cancun")
        {
            blockNumber = std::max(blockNumber, int64_t{19'426'587});
        }
        header.setNumber(blockNumber);
        header.setTimestamp(fixture.block.timestamp);
        header.calculateHash(*cryptoSuite->hashImpl());
        return header;
    }
};

BOOST_FIXTURE_TEST_SUITE(EthTransactionExecutorFixture, EthExecutorFixtureHarness)

BOOST_AUTO_TEST_CASE(all_fixtures_phase1)
{
    auto const files = listAllFixtureFiles(
#ifdef ETH_STATE_FIXTURES_DIR
        std::filesystem::path(ETH_STATE_FIXTURES_DIR)
#else
        std::filesystem::path("fixtures/state")
#endif
    );
    BOOST_REQUIRE_EQUAL(files.size(), 20u);
    for (auto const& path : files)
    {
        auto fixture = loadFixture(path);
        BOOST_TEST_CONTEXT("fixture=" << fixture.name << " path=" << path.string())
        {
            task::syncWait([&, fixture]() -> task::Task<void> {
                executor_v1::MutableStorage localStorage;
                co_await seedPreState(localStorage, fixture, cryptoSuite->hashImpl());
                auto tx = buildFixtureTransaction(fixture, transactionFactory);
                auto header = makeBlockHeader(fixture);
                auto ledgerConfig = makeLedgerConfig(fixture);
                auto receipt = co_await executor.executeTransaction(
                    localStorage, header, *tx, contextId++, ledgerConfig, false);
                BOOST_REQUIRE(receipt);
                assertExecutorFixtureResult(fixture, *receipt, AssertPhase::Phase1);
            }());
        }
    }
}

BOOST_AUTO_TEST_CASE(gas_executor_phase2_subset)
{
    static std::array<std::string_view, 6> const kGasFixtures = {
        "prague_call_return_word.json",
        "imported/stExample_return42.json",
        "imported/stRevert_revertBasic.json",
        "imported/stRevert_revertDepth.json",
        "imported/stCreate_initCode.json",
        "imported/stCreate2_basic.json",
    };

    auto const root =
#ifdef ETH_STATE_FIXTURES_DIR
        std::filesystem::path(ETH_STATE_FIXTURES_DIR)
#else
        std::filesystem::path("fixtures/state")
#endif
        ;

    for (auto const& relativePath : kGasFixtures)
    {
        auto const path = root / relativePath;
        auto fixture = loadFixture(path);
        BOOST_TEST_CONTEXT("fixture=" << fixture.name << " path=" << path.string())
        {
            task::syncWait([&, fixture]() -> task::Task<void> {
                executor_v1::MutableStorage localStorage;
                co_await seedPreState(localStorage, fixture, cryptoSuite->hashImpl());
                auto tx = buildFixtureTransaction(fixture, transactionFactory);
                auto header = makeBlockHeader(fixture);
                auto ledgerConfig = makeLedgerConfig(fixture);
                auto receipt = co_await executor.executeTransaction(
                    localStorage, header, *tx, contextId++, ledgerConfig, false);
                BOOST_REQUIRE(receipt);
                assertExecutorFixtureResult(fixture, *receipt, AssertPhase::Phase2);
            }());
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::test
