/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file OpEngineImportFcuTest.cpp
 * @brief S5+S6 handshake matrix: newPayload = import-without-setHead, FCU = pin
 * three-branch + atomic SetCanonical. Drives the REAL OpScheduler through the
 * SchedulerInterface seam against an in-memory MLS (no ledger: execute-only —
 * imports never touch prewriteBlockToBuffer).
 */

#include "support/OpEngineKarstTestHarness.h"

#include <opstack-executor/OpDepositEncode.h>  // encodeDepositEnvelope
#include <opstack-executor/OpScheduler.h>

#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-ledger/LedgerMethods.h>
#include <bcos-ledger/mpt/Constants.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::engine;
using namespace op_engine_parity_test;
using bcos::evm::opstack::encodeDepositEnvelope;
using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
using namespace evmc::literals;
using bcos::executor_v1::opstack::decodeDepositEnvelope;

namespace
{
constexpr auto kDepositFrom = 0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001_address;
constexpr auto kL1Block = 0x4200000000000000000000000000000000000015_address;
const bcos::Address kImportEip1559Sender{"0x1000000000000000000000000000000000000001"};

bcos::protocol::TransactionReceiptFactory::Ptr makeImportReceiptFactory()
{
    return std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(makeCryptoSuite());
}

bcos::evm::opstack::DepositTx makeImportDeposit(std::string_view label)
{
    bcos::crypto::Keccak256 hasher;
    bcos::evm::opstack::DepositTx dep;
    auto digest = hasher.hash(
        bcos::bytesConstRef{reinterpret_cast<const bcos::byte*>(label.data()), label.size()});
    std::memcpy(dep.source_hash.bytes, digest.data(), sizeof(dep.source_hash.bytes));
    dep.from = kDepositFrom;
    dep.to = kL1Block;
    dep.mint = std::nullopt;
    dep.value = intx::uint256{0};
    dep.gas_limit = 0xf4240;
    dep.is_system_tx = false;
    dep.data = {};
    return dep;
}

/// extraTransactionBytes = full envelope (the only bytes depositFromTransaction
/// reads); sender forced to the deposit's from (OpSchedulerTest precedent).
bcos::protocol::Transaction::Ptr buildImportDepositTx(
    bcos::bytes const& env, bcos::crypto::Hash::Ptr const& hashImpl)
{
    auto const txHash = hashImpl->hash(env);
    bcostars::Transaction tars;
    tars.type = static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
    tars.extraTransactionHash.assign(txHash.begin(), txHash.end());
    tars.extraTransactionBytes.assign(env.begin(), env.end());
    tars.web3TypedTxKind = static_cast<tars::Char>(0x7e);
    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
        [tars = std::move(tars)]() mutable { return &tars; });
    auto const dep = decodeDepositEnvelope(bcos::bytesConstRef{env.data(), env.size()});
    tx->forceSender(bcos::bytes(dep.from.bytes, dep.from.bytes + sizeof(dep.from.bytes)));
    return tx;
}

std::shared_ptr<bcostars::protocol::BlockHeaderImpl> makeImportHeader(
    bcos::protocol::BlockNumber number, bcos::h256 parentHash, uint64_t timestampSeconds)
{
    auto h = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
    h->setNumber(number);
    h->setTimestamp(static_cast<int64_t>(timestampSeconds * 1000));  // internal ms
    h->setParentInfo(
        bcos::protocol::ParentInfo{.blockNumber = number - 1, .blockHash = parentHash});
    h->setCoinbase(bcos::Address{"0x4200000000000000000000000000000000000011"});
    h->setStateRoot(bcos::h256{});
    h->setTxsRoot(bcos::h256{});
    h->setReceiptsRoot(bcos::h256{});
    h->setGasLimit(bcos::u256(30'000'000));
    h->setGasUsed(bcos::u256(0));
    h->setExtraData(bcos::bytes{});
    h->setPrevRandao(bcos::h256{});
    h->setBaseFee(bcos::u256(1'000'000'000));
    h->setWithdrawalsRoot(bcos::h256{});
    h->setBlobGasUsed(bcos::u256(0));
    h->setExcessBlobGas(bcos::u256(0));
    h->setParentBeaconBlockRoot(bcos::h256{});
    h->setRequestsHash(bcos::h256{});
    return h;
}

/// Seed the committed plane as an existing genesis: current number 0 plus a funded
/// EOA so execution has a world to run against. This is the `latest` the imports
/// must NOT move.
void seedCommittedGenesis(MLS& mls, bcos::crypto::Hash::Ptr const& hashImpl)
{
    auto view = mls.fork();
    view.newMutable();
    bcos::ledger::account::EVMAccount account(view, kImportEip1559Sender, /*rawAddress=*/false);
    bcos::task::syncWait(account.create());
    bcos::task::syncWait(account.setCode({}, {}, hashImpl->emptyHash()));
    bcos::task::syncWait(account.setNonce("0"));
    bcos::task::syncWait(account.setBalance(bcos::u256(1) << 200));
    bcos::storage::Entry entry;
    entry.set("0");
    bcos::task::syncWait(bcos::storage2::writeOne(view,
        StateKey{bcos::ledger::SYS_CURRENT_STATE, bcos::ledger::SYS_KEY_CURRENT_NUMBER},
        std::move(entry)));
    bcos::task::syncWait(mls.mergeView(std::move(view)));
}

int64_t committedTipNumber(MLS& mls)
{
    auto view = mls.forkCommitted();
    return bcos::task::syncWait(
        bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage));
}

std::optional<bcos::h256> committedHashAt(MLS& mls, int64_t number)
{
    auto view = mls.forkCommitted();
    return bcos::task::syncWait(
        bcos::ledger::getBlockHash(view, number, bcos::ledger::fromStorage));
}

struct ImportSchedulerFixture
{
    BackendMemStorage backend{1};
    CheckpointBackend checkpoint{backend};
    MLS storage{checkpoint};
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    bcos::crypto::Hash::Ptr hashImpl{makeCryptoSuite()->hashImpl()};
    bcos::IOServicePool::Ptr ioServicePool{std::make_shared<bcos::IOServicePool>(1)};
    std::shared_ptr<bcos::scheduler::SchedulerInterface> scheduler;

    ImportSchedulerFixture()
    {
        // Execute-only: ledger = nullptr (importExecute must never reach
        // prewriteBlockToBuffer, which is the only ledger-dependent path).
        scheduler = std::make_shared<bcos::executor_v1::opstack::OpScheduler<MLS>>(
            makeImportReceiptFactory(), hashImpl, /*chainId=*/8453,
            std::make_shared<bcos::evm::opstack::OpForkSchedule>(
                bcos::evm::opstack::OpForkSchedule::legacy(false)),
            blockFactory, storage, /*ledger=*/nullptr, ioServicePool);
        seedCommittedGenesis(storage, hashImpl);
    }

    bcos::protocol::Block::Ptr depositBlock(bcos::protocol::BlockNumber number,
        bcos::h256 const& parentHash, uint64_t timestampSeconds, std::string_view label)
    {
        auto env = encodeDepositEnvelope(makeImportDeposit(label));
        auto tx = buildImportDepositTx(env, hashImpl);
        auto block = blockFactory->createBlock();
        block->setBlockHeader(makeImportHeader(number, parentHash, timestampSeconds));
        block->appendTransaction(std::move(tx));
        return block;
    }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(OpEngineImportFcuTest)

// S5 Task 3: importExecute executes on the PARENT's post-state (delta chain stacked
// over the committed flat) WITHOUT any canonical-table write, and consecutive imports
// never trip the linear pending-slot gates (no RefuseOtherHeight — the pending slot
// is not importExecute's business).
BOOST_AUTO_TEST_CASE(ImportExecuteStacksParentDeltasWithoutCanonicalWrites)
{
    ImportSchedulerFixture f;

    // B1: parent = genesis (empty delta chain).
    auto b1 = f.depositBlock(1, bcos::h256{}, 1'000'000, "import-b1");
    std::shared_ptr<void> delta1;
    bcos::protocol::BlockHeader::Ptr header1;
    f.scheduler->importExecute(b1, {},
        [&](Error::Ptr error, bcos::protocol::BlockHeader::Ptr header,
            std::shared_ptr<void> delta) {
            BOOST_REQUIRE(!error);
            header1 = std::move(header);
            delta1 = std::move(delta);
        });
    BOOST_REQUIRE(header1 != nullptr);
    BOOST_REQUIRE(delta1 != nullptr);
    auto const b1Hash = bcos::protocol::EthBlockHeader::computeHash(*header1);
    // Receipts ride on the block for the later canonical prewrite (Task 5).
    BOOST_CHECK_EQUAL(b1->receiptsSize(), b1->transactionsSize());

    // The canonical plane is untouched after B1.
    BOOST_CHECK_EQUAL(committedTipNumber(f.storage), 0);
    BOOST_CHECK(!committedHashAt(f.storage, 1).has_value());

    // B2: parent = B1, delta chain {delta1}. Must NOT be RefuseOtherHeight.
    auto b2 = f.depositBlock(2, b1Hash, 1'000'012, "import-b2");
    bcos::protocol::BlockHeader::Ptr header2;
    f.scheduler->importExecute(b2, {delta1},
        [&](Error::Ptr error, bcos::protocol::BlockHeader::Ptr header, std::shared_ptr<void>) {
            BOOST_REQUIRE(!error);
            header2 = std::move(header);
        });
    BOOST_REQUIRE(header2 != nullptr);
    BOOST_CHECK_EQUAL(committedTipNumber(f.storage), 0);
    BOOST_CHECK(!committedHashAt(f.storage, 2).has_value());

    // Control: the SAME B2 executed on the genesis plane (no parent delta) must
    // produce a DIFFERENT stateRoot — proving the stacked run really executed on
    // B1's post-state instead of the committed flat.
    auto b2GenesisPlane = f.depositBlock(2, b1Hash, 1'000'012, "import-b2");
    bcos::protocol::BlockHeader::Ptr header2Genesis;
    f.scheduler->importExecute(b2GenesisPlane, {},
        [&](Error::Ptr error, bcos::protocol::BlockHeader::Ptr header, std::shared_ptr<void>) {
            BOOST_REQUIRE(!error);
            header2Genesis = std::move(header);
        });
    BOOST_REQUIRE(header2Genesis != nullptr);
    BOOST_CHECK(header2->stateRoot() != header2Genesis->stateRoot());
}

BOOST_AUTO_TEST_SUITE_END()
