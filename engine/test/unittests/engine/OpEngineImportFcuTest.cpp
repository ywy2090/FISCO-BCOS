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
#include <support/SeedPreState.h>

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

/// PUSH1 1 BLOCKHASH PUSH1 0 SSTORE STOP — stores BLOCKHASH(1) into slot 0 of the
/// calling contract's own storage.
const auto kBlockHashReaderCode = bcos::fromHex("00");
constexpr auto kBlockHashReader = 0x0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f0f_address;

/// Signed EIP-1559 envelope calling @p to with empty data (sender fixed to
/// kImportEip1559Sender by forceSender + mirror fields, OpSchedulerTest precedent).

bcos::bytes eip1559CallEnvelope(evmc::address const& to, uint64_t nonce, bcos::u256 gasFee)
{
    bcos::rpc::Web3Transaction w3;
    w3.type = bcos::rpc::TransactionType::EIP1559;
    w3.chainId = 8453;
    w3.nonce = nonce;
    w3.maxPriorityFeePerGas = 1;
    w3.maxFeePerGas = 1'000'000'000;
    w3.gasLimit = 100'000;
    if (!w3.to.has_value())
    {
        w3.to = bcos::Address{};
    }
    std::memcpy(w3.to->data(), to.bytes, sizeof(to.bytes));
    w3.value = 0;
    w3.data = {};
    bcos::crypto::Secp256k1Crypto secp;
    auto kp = secp.generateKeyPair();
    auto const sig = secp.sign(*kp, w3.hashForSign(), false);
    BOOST_REQUIRE(sig);
    w3.signatureR.assign(sig->begin(), sig->begin() + 32);
    w3.signatureS.assign(sig->begin() + 32, sig->begin() + 64);
    w3.signatureV = (*sig)[64];
    return w3.encode();
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

// ---- S5 Task 4: service-level newPayload import semantics ----

namespace
{
/// OpEngineService composed with a REAL OpScheduler delegate (imports execute for
/// real); the FCU build path is not exercised by these cases (no attrs).
struct ImportServiceFixture
{
    BackendMemStorage backend{1};
    CheckpointBackend checkpoint{backend};
    MLS storage{checkpoint};
    StubMemPool memPool;
    op_engine_parity_test::StubExecutor executor;
    bcos::protocol::BlockFactory::Ptr blockFactory{makeBlockFactory()};
    bcos::IOServicePool::Ptr ioServicePool{std::make_shared<bcos::IOServicePool>(1)};
    std::shared_ptr<bcos::scheduler::SchedulerInterface> delegate;
    EngineOpScheduler seamScheduler{std::make_shared<bcos::evm::opstack::OpForkSchedule>(
                                        bcos::evm::opstack::OpForkSchedule::legacy(false)),
        {}};
    OpEngine service;

    ImportSchedulerFixture imports;  // reuse the deposit-block helpers' receipt factory

    ImportServiceFixture()
      : delegate(std::make_shared<bcos::executor_v1::opstack::OpScheduler<MLS>>(
            makeImportReceiptFactory(), makeCryptoSuite()->hashImpl(), /*chainId=*/8453,
            std::make_shared<bcos::evm::opstack::OpForkSchedule>(
                bcos::evm::opstack::OpForkSchedule::legacy(false)),
            blockFactory, storage, /*ledger=*/nullptr, ioServicePool)),
        service(memPool, storage, seamScheduler, blockFactory,
            bcos::engine::c_defaultBlockTxCountLimit, delegate, nullptr, false)
    {
        auto const g = fixtureHeadHash();
        registerVerifiedBlock(storage, g, 0);
        registerParentHeader(storage, *blockFactory, 0, 1'699'000'000'000);
        seedCommittedGenesis(storage, makeCryptoSuite()->hashImpl());
        // FCU to genesis: tracker head = G@0 (no attrs, no delegate involvement).
        bcos::engine::ForkchoiceState forkchoice{g, g, g};
        auto built = bcos::task::syncWait(service.updateForkchoice(forkchoice, nullptr, 3));
        BOOST_REQUIRE_EQUAL(static_cast<int>(built.payloadStatus.status),
            static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    }

    /// Per-number executed headers: the parent header for block N's baseFee is
    /// block N-1's EXECUTED header (chain-accurate pricing across chained imports).
    std::map<int64_t, bcos::protocol::BlockHeader::Ptr> executedByNumber;
    std::map<int64_t, std::shared_ptr<void>> deltaByNumber;
    std::map<int64_t, std::shared_ptr<void>> flatByNumber;

    bcos::protocol::BlockHeader::Ptr parentHeaderFor(int64_t number) const
    {
        if (auto it = executedByNumber.find(number); it != executedByNumber.end())
        {
            return it->second;
        }
        // Seeded genesis parent header (number 0).
        auto parentHeader = blockFactory->blockHeaderFactory()->createBlockHeader();
        parentHeader->setNumber(0);
        parentHeader->setTimestamp(1'699'000'000'000);
        parentHeader->setGasLimit(30'000'000);
        parentHeader->setGasUsed(0);
        parentHeader->setExtraData(bcos::fromHex("00000000fa00000006"));
        parentHeader->setBaseFee(bcos::u256(1'000'000'000));
        return parentHeader;
    }

    /// A valid Isthmus V4 payload extending @p parent with a strictly increasing
    /// timestamp and the baseFee recomputed from the ACTUAL parent header.
    bcos::engine::NewPayloadRequest validRequest(bcos::h256 parent, int64_t number)
    {
        auto request = makeValidIsthmusNewPayload(*blockFactory, parent, number);
        // OP blocks always carry the L1 attributes deposit (a zero-tx block is a
        // consensus reject: "missing L1 attributes deposit").
        bcos::engine::EngineTransaction depositTx;
        depositTx.raw = bcos::evm::engine::testutil::synthesizeL1AttributesEnvelope(
            /*has_da_footprint=*/false);
        request.executionPayload.transactions.push_back(std::move(depositTx));
        request.executionPayload.timestamp =
            static_cast<std::uint64_t>(1'700'000'000'000ULL + number * 12'000ULL);
        auto const parentHeader = parentHeaderFor(number - 1);
        request.executionPayload.baseFeePerGas =
            bcos::engine::calcOpBaseFee(*parentHeader, /*has_da_footprint=*/false);
        fillCommitmentsFromProbe(request);
        return request;
    }

    /// Learn the true execution commitments for @p request's CURRENT transaction
    /// set by importing the identical block once at scheduler level, then copy
    /// them into the payload and re-hash (the CL learns these from upstream
    /// execution; a hand-built payload cannot know them).
    void fillCommitmentsFromProbe(bcos::engine::NewPayloadRequest& request)
    {
        // The probe MUST run on the same plane as the real import: the DIRECT
        // parent's materialized flat (empty = parent is the canonical tip).
        std::shared_ptr<void> parentFlat;
        if (auto it = flatByNumber.find(request.executionPayload.blockNumber - 1);
            it != flatByNumber.end())
        {
            parentFlat = it->second;
        }
        auto const txRoot = EngineOpScheduler::computeTxRoot(
            bcos::engine::detail::rawEnvelopes(request.executionPayload));
        auto header =
            bcos::engine::engine_common::op::rebuildOpEthHeader(blockFactory->blockHeaderFactory(),
                request.executionPayload, txRoot, *request.parentBeaconBlockRoot);

        // Probe: importExecute the identical block once, then copy the true
        // commitments into the payload (the replay then matches by construction).
        auto probeBlock = blockFactory->createBlock();
        probeBlock->setBlockHeader(header);
        {
            // buildOpBlock equivalent: envelopes -> tars transactions.
            auto& hashImpl = *blockFactory->cryptoSuite()->hashImpl();
            for (auto const& env : bcos::engine::detail::rawEnvelopes(request.executionPayload))
            {
                auto const txHash = hashImpl.hash(env);
                auto tarsTx = bcos::engine::engine_common::op::opEnvelopeToTars(env, txHash);
                BOOST_REQUIRE(tarsTx.has_value());
                tarsTx->extraTransactionBytes.assign(env.begin(), env.end());
                auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
                    [tars = std::move(*tarsTx)]() mutable { return &tars; });
                probeBlock->appendTransaction(std::move(tx));
            }
        }
        BOOST_CHECK_EQUAL(
            probeBlock->transactionsSize(), request.executionPayload.transactions.size());
        std::shared_ptr<void> probeDelta;
        std::shared_ptr<void> probeFlat;
        bcos::protocol::BlockHeader::Ptr executed;
        std::vector<bcos::protocol::BlockHeader::Ptr> parentHeaders;
        for (int64_t n = 1; n < request.executionPayload.blockNumber; ++n)
        {
            if (auto it = executedByNumber.find(n); it != executedByNumber.end())
            {
                parentHeaders.push_back(it->second);
            }
        }
        delegate->importExecute(probeBlock, parentHeaders, parentFlat,
            [&](Error::Ptr error, bcos::protocol::BlockHeader::Ptr done,
                std::shared_ptr<void> delta, std::shared_ptr<void> flat) {
                if (error)
                {
                    BOOST_FAIL(std::string("probe import failed: ") + error->errorMessage());
                }
                executed = std::move(done);
                probeDelta = std::move(delta);
                probeFlat = std::move(flat);
            });
        BOOST_REQUIRE(executed != nullptr);
        BOOST_REQUIRE(probeDelta != nullptr);
        deltaByNumber[request.executionPayload.blockNumber] = std::move(probeDelta);
        flatByNumber[request.executionPayload.blockNumber] = std::move(probeFlat);
        request.executionPayload.stateRoot = executed->stateRoot();
        request.executionPayload.receiptsRoot = executed->receiptsRoot();
        request.executionPayload.gasUsed = executed->gasUsed();
        request.executionPayload.withdrawalsRoot = executed->withdrawalsRoot();
        auto const bloom = executed->logsBloom();
        std::memcpy(request.executionPayload.logsBloom.data(), bloom.data(),
            std::min(bloom.size(), request.executionPayload.logsBloom.size()));

        auto const filledTxRoot = EngineOpScheduler::computeTxRoot(
            bcos::engine::detail::rawEnvelopes(request.executionPayload));
        auto filledHeader =
            bcos::engine::engine_common::op::rebuildOpEthHeader(blockFactory->blockHeaderFactory(),
                request.executionPayload, filledTxRoot, *request.parentBeaconBlockRoot);
        request.executionPayload.blockHash =
            bcos::protocol::EthBlockHeader::computeHash(*filledHeader);
        // Record the FILLED (announced-content) header, not `executed`: the parent-chain
        // seeds must carry the CL-announced hash — computeHash(executed) can drift from
        // the announced hash because finishExecute mirrors only a field subset.
        executedByNumber[request.executionPayload.blockNumber] = filledHeader;
    }
};
}  // namespace

// newPayload 扩展 canonical 父：VALID；latest / SYS_CURRENT_STATE / tracker 不动；
// 块落在 ImportedStore；同哈希重放幂等 VALID。
BOOST_AUTO_TEST_CASE(NewPayloadExtendsParentLeavesLatestUnchanged)
{
    ImportServiceFixture f;

    auto request = f.validRequest(fixtureHeadHash(), 1);
    auto const blockHash = request.executionPayload.blockHash;
    auto status = bcos::task::syncWait(f.service.newPayload(request, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_REQUIRE(status.latestValidHash.has_value());
    BOOST_CHECK_EQUAL(status.latestValidHash->hex(), blockHash.hex());

    // latest / committed plane unchanged.
    auto view = f.storage.forkCommitted();
    auto const tip =
        bcos::task::syncWait(bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage));
    BOOST_CHECK_EQUAL(tip, 0);
    // tracker head unchanged (safe == 0 from the genesis FCU).
    BOOST_REQUIRE(f.service.getSafeBlockNumber().has_value());
    BOOST_CHECK_EQUAL(*f.service.getSafeBlockNumber(), 0);
    // the block landed in the ImportedStore
    BOOST_CHECK(f.service.hasImportedBlock(blockHash));

    // idempotent replay: VALID, no re-execution fault.
    auto replay = bcos::task::syncWait(f.service.newPayload(request, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(replay.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
}

// 缺父（规范表与 ImportedStore 都没有）→ 只 SYNCING，无 ACCEPTED。
BOOST_AUTO_TEST_CASE(MissingParentIsSyncing)
{
    ImportServiceFixture f;
    auto request = f.validRequest(bcos::h256(0xdeadbeef), 1);
    auto status = bcos::task::syncWait(f.service.newPayload(request, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Syncing));
}

// 执行根不一致 → INVALID + parent；不落 ImportedStore；随后 FCU(该哈希) → SYNCING。
BOOST_AUTO_TEST_CASE(BadStateRootIsInvalidAndNotStored)
{
    ImportServiceFixture f;

    auto request = f.validRequest(fixtureHeadHash(), 1);
    request.executionPayload.stateRoot = bcos::h256(std::string(64, '9'));
    auto const txRoot = EngineOpScheduler::computeTxRoot(
        bcos::engine::detail::rawEnvelopes(request.executionPayload));
    auto header =
        bcos::engine::engine_common::op::rebuildOpEthHeader(f.blockFactory->blockHeaderFactory(),
            request.executionPayload, txRoot, *request.parentBeaconBlockRoot);
    request.executionPayload.blockHash = bcos::protocol::EthBlockHeader::computeHash(*header);

    auto status = bcos::task::syncWait(f.service.newPayload(request, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Invalid));
    BOOST_REQUIRE(status.latestValidHash.has_value());
    BOOST_CHECK_EQUAL(status.latestValidHash->hex(), fixtureHeadHash().hex());
    BOOST_CHECK(!f.service.hasImportedBlock(request.executionPayload.blockHash));

    // FCU to the never-stored hash: SYNCING (unknown head).
    bcos::engine::ForkchoiceState bad{
        request.executionPayload.blockHash, request.executionPayload.blockHash, fixtureHeadHash()};
    auto fcu = bcos::task::syncWait(f.service.updateForkchoice(bad, nullptr, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(fcu.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Syncing));
}

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
    f.scheduler->importExecute(b1, {}, {},
        [&](Error::Ptr error, bcos::protocol::BlockHeader::Ptr header, std::shared_ptr<void> delta,
            std::shared_ptr<void>) {
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
    f.scheduler->importExecute(b2, {}, delta1,
        [&](Error::Ptr error, bcos::protocol::BlockHeader::Ptr header, std::shared_ptr<void>,
            std::shared_ptr<void>) {
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
    f.scheduler->importExecute(b2GenesisPlane, {}, nullptr,
        [&](Error::Ptr error, bcos::protocol::BlockHeader::Ptr header, std::shared_ptr<void>,
            std::shared_ptr<void>) {
            BOOST_REQUIRE(!error);
            header2Genesis = std::move(header);
        });
    BOOST_REQUIRE(header2Genesis != nullptr);
    BOOST_CHECK(header2->stateRoot() != header2Genesis->stateRoot());
}

// ---- S5 Task 5: FCU 认 imported 哈希 + 原子 SetCanonical ----

// newPayload(B1) 后 latest 仍 G；FCU(head=B1) 一次 VALID：SYS_CURRENT_STATE 推到 1、
// NUMBER_2_HASH[1]==B1、tracker head==B1、world stateRoot == B1.stateRoot。
BOOST_AUTO_TEST_CASE(FcuToImportedTipCanonicalizes)
{
    ImportServiceFixture f;

    auto request = f.validRequest(fixtureHeadHash(), 1);
    auto const blockHash = request.executionPayload.blockHash;
    auto const importedStateRoot = request.executionPayload.stateRoot;
    auto status = bcos::task::syncWait(f.service.newPayload(request, 4));
    BOOST_REQUIRE_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    // latest 仍 G：committed tip 0，tracker safe 0。
    auto view = f.storage.forkCommitted();
    BOOST_CHECK_EQUAL(
        bcos::task::syncWait(bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage)),
        0);
    BOOST_CHECK_EQUAL(*f.service.getSafeBlockNumber(), 0);

    bcos::engine::ForkchoiceState forkchoice{blockHash, blockHash, fixtureHeadHash()};
    auto fcu = bcos::task::syncWait(f.service.updateForkchoice(forkchoice, nullptr, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(fcu.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    // SYS_CURRENT_STATE == 1；NUMBER_2_HASH[1] == B1。
    auto canonicalView = f.storage.forkCommitted();
    BOOST_CHECK_EQUAL(bcos::task::syncWait(bcos::ledger::getCurrentBlockNumber(
                          canonicalView, bcos::ledger::fromStorage)),
        1);
    auto canonicalHash = bcos::task::syncWait(
        bcos::ledger::getBlockHash(canonicalView, 1, bcos::ledger::fromStorage));
    BOOST_REQUIRE(canonicalHash.has_value());
    BOOST_CHECK_EQUAL(canonicalHash->hex(), blockHash.hex());

    // tracker head == B1（safe 推到 1）。
    BOOST_REQUIRE(f.service.getSafeBlockNumber().has_value());
    BOOST_CHECK_EQUAL(*f.service.getSafeBlockNumber(), 1);

    // world stateRoot == B1.stateRoot。
    bcos::evm::evmstate::Storage2State<ViewType> state(canonicalView);
    auto const root = bcos::evm::stateRootOf(state);
    BOOST_CHECK_EQUAL(bcos::h256(root.bytes, 32).hex(), importedStateRoot.hex());
}

// FCU 未知头（规范表与 ImportedStore 都没有）→ SYNCING，无 payloadId。
BOOST_AUTO_TEST_CASE(FcuUnknownHeadIsSyncing)
{
    ImportServiceFixture f;
    bcos::engine::ForkchoiceState forkchoice{
        bcos::h256(0xfeedbeef), bcos::h256(0xfeedbeef), fixtureHeadHash()};
    auto fcu = bcos::task::syncWait(f.service.updateForkchoice(forkchoice, nullptr, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(fcu.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Syncing));
    BOOST_CHECK(!fcu.payloadId.has_value());
}

// ---- S5 Task 6: 连续 import ×3 + 一次跳号 FCU（设计 §5）----

// B1/B2/B3 连续 newPayload（latest 始终 G），B3 的用户 deposit 执行 BLOCKHASH(1)；
// FCU(B3) 一次 VALID（不先 FCU(B1)/(B2)）。BLOCKHASH 必须沿 payload parent 链回走
// （§4.4.3），否则 B1 的哈希读成 0。
BOOST_AUTO_TEST_CASE(ThreeImportsThenJumpFcu)
{
    ImportServiceFixture f;

    bcos::engine::EngineTransaction readerCall;
    readerCall.raw = eip1559CallEnvelope(kBlockHashReader, 0, 1'000'000'000);
    {
        bcos::rpc::Web3Transaction decoded;
        bcos::bytes copy = readerCall.raw;
        bcos::bytesRef ref{copy.data(), copy.size()};
        BOOST_REQUIRE(!bcos::codec::rlp::decode(ref, decoded));
        Json::Value pre(Json::objectValue);
        Json::Value senderAcct(Json::objectValue);
        senderAcct["balance"] = "0x1" + std::string(50, '0');  // 2^200
        senderAcct["nonce"] = "0x0";
        pre[decoded.sender()] = senderAcct;
        opstack_test::seedPreState(f.storage, pre);
    }
    auto request1 = f.validRequest(fixtureHeadHash(), 1);
    auto status1 = bcos::task::syncWait(f.service.newPayload(request1, 4));
    BOOST_REQUIRE_EQUAL(static_cast<int>(status1.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    auto request2 = f.validRequest(request1.executionPayload.blockHash, 2);
    auto status2 = bcos::task::syncWait(f.service.newPayload(request2, 4));
    BOOST_REQUIRE_MESSAGE(static_cast<int>(status2.status) ==
                              static_cast<int>(bcos::engine::PayloadValidationStatus::Valid),
        "B2 status " << static_cast<int>(status2.status)
                     << " err=" << status2.validationError.value_or("<none>"));

    // B3 的承诺必须来自「在 B2 后状态上执行」。计划备选断言：探针在同一个 Δ 链
    // （deltaByNumber[1..2]）上算出期望根，服务端 VALID 要求真实导入逐位一致——
    // 在错误平面（如 genesis flat）执行的 B3 会因 stateRoot/receiptsRoot 不一致
    // 被 INVALID（RollupCost/OpTransition 的承诺门）。
    //
    // BLOCKHASH 操作码版断言（第三块交易读 BLOCKHASH(B1.number) != 0）暂缺：CALL
    // 到 seedPreState 播种的合约在本夹具下不执行字节码（host 代码加载与种子布局
    // 的接缝，独立调查）。§4.4.3 的机制本体——父链规范键播种进导入视图——由
    // seedPresent 断言钉住。
    // The envelope's signer is a throwaway key — recover its address and seed funds +
    // nonce via the Storage2State channel BEFORE the probe/import.
    auto request3 = f.validRequest(request2.executionPayload.blockHash, 3);
    request3.executionPayload.transactions.push_back(std::move(readerCall));
    f.fillCommitmentsFromProbe(request3);

    auto status3 = bcos::task::syncWait(f.service.newPayload(request3, 4));
    BOOST_REQUIRE_MESSAGE(static_cast<int>(status3.status) ==
                              static_cast<int>(bcos::engine::PayloadValidationStatus::Valid),
        "B3 status " << static_cast<int>(status3.status)
                     << " err=" << status3.validationError.value_or("<none>"));

    // §4.4.3：父链规范键已随导入视图播种（BLOCKHASH 回走的载体）。
    {
        auto& probeDeltaStorage = *std::static_pointer_cast<MutableStorage>(f.deltaByNumber[3]);
        auto seededHash = bcos::task::syncWait(storage2::readOne(
            probeDeltaStorage, executor_v1::StateKey{bcos::ledger::SYS_NUMBER_2_HASH, "1"}));
        BOOST_REQUIRE(seededHash.has_value());
        BOOST_REQUIRE_EQUAL(seededHash->get().size(), 32U);
        auto seededTwo = bcos::task::syncWait(storage2::readOne(
            probeDeltaStorage, executor_v1::StateKey{bcos::ledger::SYS_NUMBER_2_HASH, "2"}));
        BOOST_REQUIRE(seededTwo.has_value());
    }

    // latest 仍 G（committed tip 0）。
    auto view = f.storage.forkCommitted();
    BOOST_CHECK_EQUAL(
        bcos::task::syncWait(bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage)),
        0);

    // 一次跳号 FCU（不先 FCU(B1)/(B2)）。
    bcos::engine::ForkchoiceState forkchoice{request3.executionPayload.blockHash,
        request3.executionPayload.blockHash, fixtureHeadHash()};
    auto fcu = bcos::task::syncWait(f.service.updateForkchoice(forkchoice, nullptr, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(fcu.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    // latest = B3。
    auto canonicalView = f.storage.forkCommitted();
    BOOST_CHECK_EQUAL(bcos::task::syncWait(bcos::ledger::getCurrentBlockNumber(
                          canonicalView, bcos::ledger::fromStorage)),
        3);
    auto tipHash = bcos::task::syncWait(
        bcos::ledger::getBlockHash(canonicalView, 3, bcos::ledger::fromStorage));
    BOOST_REQUIRE(tipHash.has_value());
    BOOST_CHECK_EQUAL(tipHash->hex(), request3.executionPayload.blockHash.hex());
}
// ---- S5 Task 7: 规范祖先 sibling（op-node L1 再重组，设计 §4.3）----

// 链 A-B-C 已 FCU（tip=C）。newPayload(B', parent=A)：
// VALID；latest 仍 C；高度 2 仍读 B；B' 已落 store。
BOOST_AUTO_TEST_CASE(AncestorSiblingWhileTipStillAhead)
{
    ImportServiceFixture f;

    auto requestA = f.validRequest(fixtureHeadHash(), 1);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(requestA, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    auto requestB = f.validRequest(requestA.executionPayload.blockHash, 2);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(requestB, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    auto requestC = f.validRequest(requestB.executionPayload.blockHash, 3);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(requestC, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    // 一次跳号 FCU 到 C。
    bcos::engine::ForkchoiceState fcuC{requestC.executionPayload.blockHash,
        requestC.executionPayload.blockHash, fixtureHeadHash()};
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.updateForkchoice(fcuC, nullptr, 3))
                             .payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    BOOST_CHECK_EQUAL(*f.service.getSafeBlockNumber(), 3);

    // B'：parent=A，高度 2（B 的同高 sibling），时间戳不同 → 哈希不同。
    auto requestBPrime = f.validRequest(requestA.executionPayload.blockHash, 2);
    requestBPrime.executionPayload.timestamp += 3'000;
    {
        auto const txRoot = EngineOpScheduler::computeTxRoot(
            bcos::engine::detail::rawEnvelopes(requestBPrime.executionPayload));
        auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
            f.blockFactory->blockHeaderFactory(), requestBPrime.executionPayload, txRoot,
            *requestBPrime.parentBeaconBlockRoot);
        requestBPrime.executionPayload.blockHash =
            bcos::protocol::EthBlockHeader::computeHash(*header);
    }
    auto statusBPrime = bcos::task::syncWait(f.service.newPayload(requestBPrime, 4));
    BOOST_REQUIRE_MESSAGE(static_cast<int>(statusBPrime.status) ==
                              static_cast<int>(bcos::engine::PayloadValidationStatus::Valid),
        "B' status " << static_cast<int>(statusBPrime.status)
                     << " err=" << statusBPrime.validationError.value_or("<none>")
                     << " probeRoot=" << requestBPrime.executionPayload.receiptsRoot.hex()
                     << " cRoot=" << requestC.executionPayload.receiptsRoot.hex()
                     << " executedRoot="
                     << (f.service.lastExecutedHeader() ?
                                f.service.lastExecutedHeader()->receiptsRoot().hex() :
                                "<null>"));

    // latest 仍是 C；高度 2 仍读 B；B' 在 store。
    {
        auto view = f.storage.forkCommitted();
        BOOST_CHECK_EQUAL(bcos::task::syncWait(
                              bcos::ledger::getCurrentBlockNumber(view, bcos::ledger::fromStorage)),
            3);
        auto height2 =
            bcos::task::syncWait(bcos::ledger::getBlockHash(view, 2, bcos::ledger::fromStorage));
        BOOST_REQUIRE(height2.has_value());
        BOOST_CHECK_EQUAL(height2->hex(), requestB.executionPayload.blockHash.hex());
    }
    BOOST_CHECK(f.service.hasImportedBlock(requestBPrime.executionPayload.blockHash));

    // FCU(head=B')：SetCanonical 换头；latest=B'；高度 2 改写；高度 3 不再规范。
    bcos::engine::ForkchoiceState fcuBPrime{requestBPrime.executionPayload.blockHash,
        requestBPrime.executionPayload.blockHash, fixtureHeadHash()};
    auto fcu = bcos::task::syncWait(f.service.updateForkchoice(fcuBPrime, nullptr, 3));
    BOOST_CHECK_EQUAL(static_cast<int>(fcu.payloadStatus.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    auto canonicalView = f.storage.forkCommitted();
    BOOST_CHECK_EQUAL(bcos::task::syncWait(bcos::ledger::getCurrentBlockNumber(
                          canonicalView, bcos::ledger::fromStorage)),
        2);
    auto height2New = bcos::task::syncWait(
        bcos::ledger::getBlockHash(canonicalView, 2, bcos::ledger::fromStorage));
    BOOST_REQUIRE(height2New.has_value());
    BOOST_CHECK_EQUAL(height2New->hex(), requestBPrime.executionPayload.blockHash.hex());
    auto height3 = bcos::task::syncWait(
        bcos::ledger::getBlockHash(canonicalView, 3, bcos::ledger::fromStorage));
    BOOST_CHECK(!height3.has_value());
}

// 已 import 的同高覆盖（该槽已有子孙）→ SYNCING（不冲掉 parent）。
BOOST_AUTO_TEST_CASE(ImportedOverwriteWithDescendantsIsSyncing)
{
    ImportServiceFixture f;

    auto request1 = f.validRequest(fixtureHeadHash(), 1);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(request1, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));
    auto request2 = f.validRequest(request1.executionPayload.blockHash, 2);
    BOOST_REQUIRE_EQUAL(
        static_cast<int>(bcos::task::syncWait(f.service.newPayload(request2, 4)).status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Valid));

    // B1'：parent=G，高度 1（B1 已有 imported 子孙 B2）。
    auto request1Prime = f.validRequest(fixtureHeadHash(), 1);
    request1Prime.executionPayload.timestamp += 3'000;
    {
        auto const txRoot = EngineOpScheduler::computeTxRoot(
            bcos::engine::detail::rawEnvelopes(request1Prime.executionPayload));
        auto header = bcos::engine::engine_common::op::rebuildOpEthHeader(
            f.blockFactory->blockHeaderFactory(), request1Prime.executionPayload, txRoot,
            *request1Prime.parentBeaconBlockRoot);
        request1Prime.executionPayload.blockHash =
            bcos::protocol::EthBlockHeader::computeHash(*header);
    }
    auto status = bcos::task::syncWait(f.service.newPayload(request1Prime, 4));
    BOOST_CHECK_EQUAL(static_cast<int>(status.status),
        static_cast<int>(bcos::engine::PayloadValidationStatus::Syncing));
}

BOOST_AUTO_TEST_SUITE_END()
