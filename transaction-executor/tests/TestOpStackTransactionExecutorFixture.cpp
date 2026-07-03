/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief OpStackTransactionExecutorImpl executor-level smoke tests (T-18 Phase 1+2).
 *  @file TestOpStackTransactionExecutorFixture.cpp
 */

#include "../bcos-transaction-executor/OpStackTransactionExecutorImpl.h"
#include "../bcos-transaction-executor/OpStackTxInputBuilder.h"
#include "TestMemoryStorage.h"
#include "bcos-codec/rlp/RLPEncode.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/opstack/fee/OpStackFeeParams.h"
#include "bcos-evm/opstack/fee/RollupCost.h"
#include "bcos-evm/opstack/types/OpStackBlockHeaderExtension.h"
#include "bcos-framework/executor/OpStackTxType.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-rpc/web3jsonrpc/model/Web3Transaction.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-task/Wait.h>
#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <array>
#include <fstream>
#include <string>

namespace bcos::evm::test
{
namespace
{
// Builds 0x000…00{value}. For executeTransaction CALL targets use value >= 0x12:
// 0x01..0x11 map to active Ethereum precompiles (EthPrecompiles 0x01..0x11).
evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

std::string addressToTableHex(evmc_address const& address)
{
    std::array<char, sizeof(address.bytes) * 2> hex{};
    boost::algorithm::hex_lower(address.bytes, address.bytes + sizeof(address.bytes), hex.data());
    return {hex.data(), hex.size()};
}

evmc_bytes32 packFeeScalars(uint32_t baseFeeScalar, uint32_t blobBaseFeeScalar)
{
    constexpr size_t scalarSectionStart = 32 - 12 - 4;
    evmc_bytes32 out{};
    out.bytes[scalarSectionStart] = static_cast<uint8_t>((baseFeeScalar >> 24) & 0xff);
    out.bytes[scalarSectionStart + 1] = static_cast<uint8_t>((baseFeeScalar >> 16) & 0xff);
    out.bytes[scalarSectionStart + 2] = static_cast<uint8_t>((baseFeeScalar >> 8) & 0xff);
    out.bytes[scalarSectionStart + 3] = static_cast<uint8_t>(baseFeeScalar & 0xff);
    out.bytes[scalarSectionStart + 4] = static_cast<uint8_t>((blobBaseFeeScalar >> 24) & 0xff);
    out.bytes[scalarSectionStart + 5] = static_cast<uint8_t>((blobBaseFeeScalar >> 16) & 0xff);
    out.bytes[scalarSectionStart + 6] = static_cast<uint8_t>((blobBaseFeeScalar >> 8) & 0xff);
    out.bytes[scalarSectionStart + 7] = static_cast<uint8_t>(blobBaseFeeScalar & 0xff);
    return out;
}

evmc_bytes32 packOperatorFeeParams(uint32_t operatorFeeScalar, uint64_t operatorFeeConstant)
{
    evmc_bytes32 out{};
    out.bytes[20] = static_cast<uint8_t>((operatorFeeScalar >> 24) & 0xff);
    out.bytes[21] = static_cast<uint8_t>((operatorFeeScalar >> 16) & 0xff);
    out.bytes[22] = static_cast<uint8_t>((operatorFeeScalar >> 8) & 0xff);
    out.bytes[23] = static_cast<uint8_t>(operatorFeeScalar & 0xff);
    out.bytes[24] = static_cast<uint8_t>((operatorFeeConstant >> 56) & 0xff);
    out.bytes[25] = static_cast<uint8_t>((operatorFeeConstant >> 48) & 0xff);
    out.bytes[26] = static_cast<uint8_t>((operatorFeeConstant >> 40) & 0xff);
    out.bytes[27] = static_cast<uint8_t>((operatorFeeConstant >> 32) & 0xff);
    out.bytes[28] = static_cast<uint8_t>((operatorFeeConstant >> 24) & 0xff);
    out.bytes[29] = static_cast<uint8_t>((operatorFeeConstant >> 16) & 0xff);
    out.bytes[30] = static_cast<uint8_t>((operatorFeeConstant >> 8) & 0xff);
    out.bytes[31] = static_cast<uint8_t>(operatorFeeConstant & 0xff);
    return out;
}

task::Task<void> seedOpFeeParams(executor_v1::MutableStorage& storage)
{
    ledger::account::EVMAccount l1Block(storage, OP_L1_BLOCK_PREDEPLOY, false);
    co_await l1Block.create();
    co_await l1Block.setStorage(state::toEvmC(L1_BASE_FEE_SLOT), state::toEvmC(u256(31'250)));
    co_await l1Block.setStorage(state::toEvmC(L1_BLOB_BASE_FEE_SLOT), state::toEvmC(u256(0)));
    co_await l1Block.setStorage(state::toEvmC(L1_FEE_SCALARS_SLOT), packFeeScalars(1, 0));
    co_await l1Block.setStorage(
        state::toEvmC(OPERATOR_FEE_PARAMS_SLOT), packOperatorFeeParams(1'000'000, 5));
}

OpStackFeeParams seededFeeParams()
{
    return OpStackFeeParams{
        .l1BaseFee = u256(31'250),
        .l1BlobBaseFee = u256(0),
        .l1BaseFeeScalar = 1,
        .l1BlobBaseFeeScalar = 0,
        .operatorFeeScalar = 1'000'000,
        .operatorFeeConstant = u256(5),
    };
}

u256 parseHexU256(std::string const& hex)
{
    auto value = hex;
    if (value.starts_with("0x") || value.starts_with("0X"))
    {
        value = value.substr(2);
    }
    if (value.empty())
    {
        return 0;
    }
    return u256("0x" + value);
}

void attachSignedWeb3RollupPayload(
    bcostars::protocol::TransactionImpl& tx, bcos::rpc::Web3Transaction const& w3)
{
    auto const signBytes = w3.encodeForSign();
    auto& inner = tx.mutableInner();
    inner.extraTransactionBytes.assign(signBytes.begin(), signBytes.end());
    inner.signature.assign(w3.signatureR.begin(), w3.signatureR.end());
    inner.signature.insert(inner.signature.end(), w3.signatureS.begin(), w3.signatureS.end());
    inner.signature.push_back(static_cast<tars::Char>(w3.signatureV));
}

bytes compactU256(u256 value)
{
    auto encoded = toBigEndian(value);
    auto it = std::find_if(encoded.begin(), encoded.end(), [](auto c) { return c != 0; });
    if (it == encoded.end())
    {
        return {};
    }
    return bytes(it, encoded.end());
}

bytes loadOpStackFixture(std::string_view name)
{
    auto const path = std::string(OPSTACK_FIXTURES_DIR) + "/" + std::string(name);
    std::ifstream input(path, std::ios::binary);
    BOOST_REQUIRE_MESSAGE(input.is_open(), "missing fixture: " << path);
    return {std::istreambuf_iterator<char>(input), {}};
}

bytes buildDepositExtra(evmc_address const& from, evmc_address const& to, u256 mint,
    bytes const& calldata = {}, uint64_t gas = 50'000)
{
    bytes payload;
    h256 sourceHash{0x1234};
    bytes sourceHashRaw(sourceHash.begin(), sourceHash.end());
    bytes fromRaw(from.bytes, from.bytes + sizeof(from.bytes));
    bytes toRaw(to.bytes, to.bytes + sizeof(to.bytes));
    bytes mintData = compactU256(mint);
    bytes valueData = compactU256(0);
    codec::rlp::encode(
        payload, sourceHashRaw, fromRaw, toRaw, mintData, valueData, gas, bytes{0x00}, calldata);

    bytes extra;
    extra.push_back(bcos::executor::DEPOSIT_TX_TYPE);
    extra.insert(extra.end(), payload.begin(), payload.end());
    return extra;
}

std::shared_ptr<bcostars::protocol::TransactionImpl> makeEip1559Tx(
    bcostars::protocol::TransactionFactoryImpl& factory, evmc_address const& sender,
    evmc_address const& recipient, int64_t gasLimit, std::string const& nonce = "0",
    bcos::bytes const& input = {0x01, 0x02, 0x03})
{
    auto tx = factory.createTransaction(1, addressToTableHex(recipient), input, nonce, 0, "", "", 0,
        std::string{}, "0x0", "0x0", gasLimit, "0x2", "0x1");
    tx->forceSender(bytes(sender.bytes, sender.bytes + sizeof(sender.bytes)));
    auto impl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
    BOOST_REQUIRE(impl);
    impl->mutableInner().type =
        static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
    impl->mutableInner().web3TypedTxKind = 2;
    return impl;
}

bcos::rpc::Web3Transaction makeSignedEip1559Web3(
    evmc_address target, bcos::bytes const& data = {0xde, 0xad, 0xbe, 0xef})
{
    bcos::rpc::Web3Transaction w3;
    w3.type = bcos::rpc::TransactionType::EIP1559;
    w3.chainId = 1;
    w3.nonce = 0;
    w3.maxPriorityFeePerGas = 1;
    w3.maxFeePerGas = 2;
    w3.gasLimit = 50'000;
    w3.to = bcos::Address(addressToTableHex(target));
    w3.value = 0;
    w3.data = data;
    w3.signatureR = bcos::bytes(32, 0x11);
    w3.signatureS = bcos::bytes(32, 0x22);
    w3.signatureV = 1;
    return w3;
}

std::shared_ptr<bcostars::protocol::TransactionImpl> makeAttachedSignedEip1559Tx(
    bcostars::protocol::TransactionFactoryImpl& factory, evmc_address sender, evmc_address target,
    bcos::rpc::Web3Transaction const& w3)
{
    auto tx = makeEip1559Tx(factory, sender, target, 50'000, "0", w3.data);
    attachSignedWeb3RollupPayload(*tx, w3);
    return tx;
}

std::shared_ptr<bcostars::protocol::TransactionImpl> makeDepositTx(
    bcostars::protocol::TransactionFactoryImpl& factory, evmc_address const& from,
    evmc_address const& to, u256 mint, bytes const& calldata = {}, int64_t gasLimit = 50'000)
{
    auto extra = buildDepositExtra(from, to, mint, calldata, static_cast<uint64_t>(gasLimit));
    auto tx = factory.createTransaction(1, addressToTableHex(to), calldata, "0", 0, "", "", 0,
        std::string{}, "0x0", "0x0", gasLimit, "0x2", "0x1");
    tx->forceSender(bytes(from.bytes, from.bytes + sizeof(from.bytes)));
    auto impl = std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx);
    BOOST_REQUIRE(impl);
    impl->mutableInner().type =
        static_cast<tars::Char>(bcos::protocol::TransactionType::Web3Transaction);
    impl->mutableInner().web3TypedTxKind = bcos::executor::DEPOSIT_TX_TYPE;
    impl->mutableInner().extraTransactionBytes.assign(extra.begin(), extra.end());
    return impl;
}
}  // namespace

class OpStackExecutorFixtureHarness
{
public:
    executor_v1::MutableStorage storage;
    ledger::LedgerConfig ledgerConfig;
    std::shared_ptr<crypto::CryptoSuite> cryptoSuite = std::make_shared<crypto::CryptoSuite>(
        std::make_shared<crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionFactoryImpl transactionFactory{cryptoSuite};
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    executor_v1::OpStackTransactionExecutorImpl executor{receiptFactory, cryptoSuite->hashImpl()};
    int contextId = 0;

    OpStackExecutorFixtureHarness()
    {
        executor::GlobalHashImpl::g_hashImpl = cryptoSuite->hashImpl();
        ledger::Features features;
        features.setGenesisFeatures(protocol::BlockVersion::MAX_VERSION);
        features.set(ledger::Features::Flag::feature_evm_cancun);
        features.set(ledger::Features::Flag::feature_balance);
        features.set(ledger::Features::Flag::feature_balance_policy1);
        ledgerConfig.setFeatures(features);
        ledgerConfig.setGasLimit({30'000'000, 0});
        ledgerConfig.setGasPrice({"0x1", 0});
    }

    bcostars::protocol::BlockHeaderImpl makeBlockHeader(bcos::u256 baseFee = 1)
    {
        bcostars::protocol::BlockHeaderImpl header;
        header.setVersion(static_cast<uint32_t>(protocol::BlockVersion::MAX_VERSION));
        header.setNumber(1);
        header.setTimestamp(12'345);
        evmc_address coinbase{};
        header.setExtraData(encodeOpStackHeaderExtra(coinbase, baseFee, 0));
        header.calculateHash(*cryptoSuite->hashImpl());
        return header;
    }

    task::Task<void> seedSender(evmc_address const& sender, u256 balance, std::string nonce = "0")
    {
        ledger::account::EVMAccount account(storage, sender, false);
        co_await account.create();
        co_await account.setBalance(balance);
        co_await account.setNonce(std::move(nonce));
    }

    task::Task<void> seedRevertTarget(evmc_address const& target)
    {
        ledger::account::EVMAccount account(storage, target, false);
        co_await account.create();
        bytes code{0x60, 0x00, 0x60, 0x00, 0xfd};
        co_await account.setCode(code, "", crypto::HashType{});
    }

    task::Task<void> seedInvalidTarget(evmc_address const& target)
    {
        ledger::account::EVMAccount account(storage, target, false);
        co_await account.create();
        bytes code{0xfe};
        co_await account.setCode(code, "", crypto::HashType{});
    }
};

BOOST_FIXTURE_TEST_SUITE(OpStackTransactionExecutorFixture, OpStackExecutorFixtureHarness)

// Unsigned tx (buildRollupCostData falls back to tx.input(), not signed RLP). Keeps
// fee-routing smoke coverage; FIX-05 / R4 / D2-2 signed-RLP TE E2E closure is in
// FIX05_signed_rlp_rollup_execute_e2e below.
BOOST_AUTO_TEST_CASE(l1_fee_recipient_gets_fee_on_success)
{
    task::syncWait([this]() -> task::Task<void> {
        auto const sender = addressFromLastByte(0x01);
        auto const target = addressFromLastByte(0x02);
        co_await seedOpFeeParams(storage);
        co_await seedSender(sender, 300'000, "0");

        auto tx = makeEip1559Tx(transactionFactory, sender, target, 50'000);
        auto header = makeBlockHeader();
        auto receipt = co_await executor.executeTransaction(
            storage, header, *tx, contextId++, ledgerConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        BOOST_REQUIRE(receipt->l1Fee().has_value());
        auto const rollup = opstack_tx::buildRollupCostData(*tx);
        BOOST_REQUIRE(rollup.has_value());
        auto const expectedL1 = l1CostFjord(*rollup, seededFeeParams());
        BOOST_CHECK_EQUAL(parseHexU256(receipt->l1Fee().value()), expectedL1);

        ledger::account::EVMAccount l1Recipient(storage, OP_L1_FEE_RECIPIENT, false);
        BOOST_CHECK_EQUAL(co_await l1Recipient.balance(), expectedL1);
    }());
}

BOOST_AUTO_TEST_CASE(signed_rlp_rollup_matches_l1_cost_formula)
{
    auto const sender = addressFromLastByte(0x05);
    auto const target = addressFromLastByte(0x20);
    auto const w3 = makeSignedEip1559Web3(target);
    auto tx = makeAttachedSignedEip1559Tx(transactionFactory, sender, target, w3);

    auto const unsignedRollup = newRollupCostData(bcos::ref(w3.encodeForSign()));
    auto const rollup = opstack_tx::buildRollupCostData(*tx);
    BOOST_REQUIRE(rollup.has_value());
    BOOST_CHECK_NE(unsignedRollup.fastLzSize, rollup->fastLzSize);

    auto const signedL1 = l1CostFjord(*rollup, seededFeeParams());
    BOOST_CHECK_GT(signedL1, u256(0));
}

// FIX-05 / Task 5: TE E2E — signed RLP → buildRollupCostData → l1CostFjord
// → receipt.l1Fee literal + OP_L1_FEE_RECIPIENT balance delta (R4 / D2-2 closure).
BOOST_AUTO_TEST_CASE(FIX05_signed_rlp_rollup_execute_e2e)
{
    task::syncWait([this]() -> task::Task<void> {
        auto const sender = addressFromLastByte(0x05);
        auto const target = addressFromLastByte(0x20);
        co_await seedOpFeeParams(storage);
        co_await seedSender(sender, 500'000'000, "0");

        auto const w3 = makeSignedEip1559Web3(target);
        auto tx = makeAttachedSignedEip1559Tx(transactionFactory, sender, target, w3);

        auto const unsignedRollup = newRollupCostData(bcos::ref(w3.encodeForSign()));
        auto const rollup = opstack_tx::buildRollupCostData(*tx);
        BOOST_REQUIRE(rollup.has_value());
        BOOST_CHECK_GT(rollup->fastLzSize, 0U);
        BOOST_CHECK_NE(unsignedRollup.fastLzSize, rollup->fastLzSize);

        auto const feeParams = seededFeeParams();
        auto const expectedL1 = l1CostFjord(*rollup, feeParams);
        BOOST_CHECK_GT(expectedL1, u256(0));

        ledger::account::EVMAccount l1Recipient(storage, OP_L1_FEE_RECIPIENT, false);
        auto const l1BalBefore = co_await l1Recipient.balance();

        auto header = makeBlockHeader();
        auto receipt = co_await executor.executeTransaction(
            storage, header, *tx, contextId++, ledgerConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        BOOST_REQUIRE(receipt->l1Fee().has_value());
        BOOST_CHECK_EQUAL(parseHexU256(receipt->l1Fee().value()), expectedL1);
        BOOST_CHECK_EQUAL(co_await l1Recipient.balance(), l1BalBefore + expectedL1);
    }());
}

BOOST_AUTO_TEST_CASE(operator_fee_recipient_gets_fee_on_success)
{
    task::syncWait([this]() -> task::Task<void> {
        auto const sender = addressFromLastByte(0x03);
        auto const target = addressFromLastByte(0x04);
        co_await seedOpFeeParams(storage);
        co_await seedSender(sender, 300'000, "0");

        auto tx = makeEip1559Tx(transactionFactory, sender, target, 50'000);
        auto header = makeBlockHeader();
        auto receipt = co_await executor.executeTransaction(
            storage, header, *tx, contextId++, ledgerConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        BOOST_REQUIRE(receipt->operatorFee().has_value());
        BOOST_CHECK_NE(receipt->operatorFee().value(), "0x0");

        auto const feeParams = seededFeeParams();
        BOOST_REQUIRE(receipt->operatorFeeScalar().has_value());
        BOOST_CHECK_EQUAL(
            parseHexU256(receipt->operatorFeeScalar().value()), feeParams.operatorFeeScalar);
        BOOST_REQUIRE(receipt->operatorFeeConstant().has_value());
        BOOST_CHECK_EQUAL(
            parseHexU256(receipt->operatorFeeConstant().value()), feeParams.operatorFeeConstant);

        ledger::account::EVMAccount operatorRecipient(storage, OP_OPERATOR_FEE_RECIPIENT, false);
        BOOST_CHECK_GT(co_await operatorRecipient.balance(), u256(0));
    }());
}

BOOST_AUTO_TEST_CASE(insufficient_balance_fails_before_execution)
{
    task::syncWait([this]() -> task::Task<void> {
        auto const sender = addressFromLastByte(0x11);
        auto const target = addressFromLastByte(0x12);
        co_await seedOpFeeParams(storage);
        co_await seedSender(sender, 100, "0");

        auto tx = makeEip1559Tx(transactionFactory, sender, target, 50'000);
        auto header = makeBlockHeader();
        auto receipt = co_await executor.executeTransaction(
            storage, header, *tx, contextId++, ledgerConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(
            receipt->status(), static_cast<int32_t>(protocol::TransactionStatus::NotEnoughCash));

        ledger::account::EVMAccount l1Recipient(storage, OP_L1_FEE_RECIPIENT, false);
        BOOST_CHECK_EQUAL(co_await l1Recipient.balance(), u256(0));
    }());
}

BOOST_AUTO_TEST_CASE(revert_keeps_l1_fee_and_operator_fee)
{
    task::syncWait([this]() -> task::Task<void> {
        auto const sender = addressFromLastByte(0x21);
        auto const target = addressFromLastByte(0x22);
        co_await seedOpFeeParams(storage);
        co_await seedSender(sender, 300'000, "0");
        co_await seedRevertTarget(target);

        auto tx = makeEip1559Tx(transactionFactory, sender, target, 50'000);
        auto header = makeBlockHeader();
        auto receipt = co_await executor.executeTransaction(
            storage, header, *tx, contextId++, ledgerConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_NE(receipt->status(), 0);
        auto const rollup = opstack_tx::buildRollupCostData(*tx);
        BOOST_REQUIRE(rollup.has_value());
        auto const expectedL1 = l1CostFjord(*rollup, seededFeeParams());
        BOOST_REQUIRE(receipt->l1Fee().has_value());
        BOOST_CHECK_EQUAL(parseHexU256(receipt->l1Fee().value()), expectedL1);

        auto const feeParams = seededFeeParams();
        auto const expectedOperator =
            operatorCostIsthmus(static_cast<uint64_t>(receipt->gasUsed()), feeParams);
        BOOST_REQUIRE(receipt->operatorFee().has_value());
        BOOST_CHECK_EQUAL(parseHexU256(receipt->operatorFee().value()), expectedOperator);
        BOOST_REQUIRE(receipt->operatorFeeScalar().has_value());
        BOOST_CHECK_EQUAL(
            parseHexU256(receipt->operatorFeeScalar().value()), feeParams.operatorFeeScalar);
        BOOST_REQUIRE(receipt->operatorFeeConstant().has_value());
        BOOST_CHECK_EQUAL(
            parseHexU256(receipt->operatorFeeConstant().value()), feeParams.operatorFeeConstant);

        ledger::account::EVMAccount operatorRecipient(storage, OP_OPERATOR_FEE_RECIPIENT, false);
        BOOST_CHECK_EQUAL(co_await operatorRecipient.balance(), expectedOperator);

        ledger::account::EVMAccount senderAccount(storage, sender, false);
        BOOST_CHECK_GT(co_await senderAccount.balance(), u256(0));
        BOOST_CHECK_LT(co_await senderAccount.balance(), u256(300'000));
    }());
}

// FIX-06 / D5-4: TE E2E — isthmus L1 attributes system deposit through
// OpStackTransactionExecutorImpl::executeTransaction (not fiscoExecute-only).
BOOST_AUTO_TEST_CASE(l1_attributes_deposit_via_te)
{
    task::syncWait([this]() -> task::Task<void> {
        co_await seedOpFeeParams(storage);
        co_await seedSender(OP_DEPOSITOR_ACCOUNT, 0, "0");

        auto const calldata = loadOpStackFixture("isthmus_l1_attributes.bin");
        BOOST_REQUIRE_EQUAL(calldata.size(), ISTHMUS_L1_ATTRIBUTES_LEN);
        auto tx = makeDepositTx(transactionFactory, OP_DEPOSITOR_ACCOUNT, OP_L1_BLOCK_PREDEPLOY,
            u256(0), calldata, 500'000);

        auto header = makeBlockHeader();
        auto receipt = co_await executor.executeTransaction(
            storage, header, *tx, contextId++, ledgerConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        BOOST_REQUIRE(receipt->depositNonce().has_value());
        BOOST_CHECK_EQUAL(parseHexU256(receipt->depositNonce().value()), u256(0));
        BOOST_REQUIRE(receipt->depositReceiptVersion().has_value());
        BOOST_CHECK_EQUAL(parseHexU256(receipt->depositReceiptVersion().value()), u256(1));
        BOOST_CHECK(!receipt->daFootprintGasScalar().has_value());
        BOOST_CHECK(!receipt->blobGasUsed().has_value());

        ledger::account::EVMAccount depositor(storage, OP_DEPOSITOR_ACCOUNT, false);
        BOOST_CHECK_EQUAL((co_await depositor.nonce()).value(), "1");
    }());
}

BOOST_AUTO_TEST_CASE(deposit_mint_applied_without_fee_routing)
{
    task::syncWait([this]() -> task::Task<void> {
        auto const sender = addressFromLastByte(0x31);
        auto const target = addressFromLastByte(0x32);
        co_await seedOpFeeParams(storage);
        co_await seedSender(sender, 0, "3");

        auto tx = makeDepositTx(transactionFactory, sender, target, u256(100));
        auto header = makeBlockHeader();
        auto receipt = co_await executor.executeTransaction(
            storage, header, *tx, contextId++, ledgerConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(receipt->status(), 0);
        BOOST_REQUIRE(receipt->depositNonce().has_value());
        BOOST_CHECK_EQUAL(parseHexU256(receipt->depositNonce().value()), u256(3));
        BOOST_REQUIRE(receipt->depositReceiptVersion().has_value());
        BOOST_CHECK_EQUAL(parseHexU256(receipt->depositReceiptVersion().value()), u256(1));

        ledger::account::EVMAccount senderAccount(storage, sender, false);
        BOOST_CHECK_EQUAL(co_await senderAccount.balance(), u256(100));
        BOOST_CHECK_EQUAL((co_await senderAccount.nonce()).value(), "4");

        ledger::account::EVMAccount l1Recipient(storage, OP_L1_FEE_RECIPIENT, false);
        BOOST_CHECK_EQUAL(co_await l1Recipient.balance(), u256(0));
    }());
}

BOOST_AUTO_TEST_CASE(deposit_skips_fee_routing_recipients)
{
    task::syncWait([this]() -> task::Task<void> {
        auto const sender = addressFromLastByte(0x41);
        auto const target = addressFromLastByte(0x42);
        co_await seedOpFeeParams(storage);
        co_await seedSender(sender, 0, "0");

        auto tx = makeDepositTx(transactionFactory, sender, target, u256(50));
        auto header = makeBlockHeader();
        auto receipt = co_await executor.executeTransaction(
            storage, header, *tx, contextId++, ledgerConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_EQUAL(receipt->status(), 0);

        ledger::account::EVMAccount baseRecipient(storage, OP_BASE_FEE_RECIPIENT, false);
        ledger::account::EVMAccount l1Recipient(storage, OP_L1_FEE_RECIPIENT, false);
        ledger::account::EVMAccount operatorRecipient(storage, OP_OPERATOR_FEE_RECIPIENT, false);
        BOOST_CHECK_EQUAL(co_await baseRecipient.balance(), u256(0));
        BOOST_CHECK_EQUAL(co_await l1Recipient.balance(), u256(0));
        BOOST_CHECK_EQUAL(co_await operatorRecipient.balance(), u256(0));
    }());
}

BOOST_AUTO_TEST_CASE(deposit_failure_reverts_but_keeps_mint)
{
    task::syncWait([this]() -> task::Task<void> {
        auto const sender = addressFromLastByte(0x51);
        auto const target = addressFromLastByte(0x52);
        co_await seedOpFeeParams(storage);
        co_await seedSender(sender, 0, "7");
        co_await seedRevertTarget(target);

        auto tx = makeDepositTx(transactionFactory, sender, target, u256(100));
        auto header = makeBlockHeader();
        auto receipt = co_await executor.executeTransaction(
            storage, header, *tx, contextId++, ledgerConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_NE(receipt->status(), 0);
        BOOST_CHECK_EQUAL(receipt->gasUsed(), 21'006);
        BOOST_CHECK_LT(receipt->gasUsed(), 50'000);
        BOOST_REQUIRE(receipt->depositNonce().has_value());
        BOOST_CHECK_EQUAL(parseHexU256(receipt->depositNonce().value()), u256(7));
        BOOST_REQUIRE(receipt->depositReceiptVersion().has_value());
        BOOST_CHECK_EQUAL(parseHexU256(receipt->depositReceiptVersion().value()), u256(1));

        ledger::account::EVMAccount senderAccount(storage, sender, false);
        BOOST_CHECK_EQUAL(co_await senderAccount.balance(), u256(100));
        BOOST_CHECK_EQUAL((co_await senderAccount.nonce()).value(), "8");

        ledger::account::EVMAccount l1Recipient(storage, OP_L1_FEE_RECIPIENT, false);
        BOOST_CHECK_EQUAL(co_await l1Recipient.balance(), u256(0));
    }());
}

BOOST_AUTO_TEST_CASE(hard_failure_status_propagates_without_state_commit)
{
    task::syncWait([this]() -> task::Task<void> {
        BOOST_TEST_MESSAGE(
            "Executor commits stateDiff only on SUCCESS/REVERT; INVALID hard failures keep "
            "storage unchanged while still reporting gas/L1 meta on the receipt.");

        auto const sender = addressFromLastByte(0x61);
        auto const target = addressFromLastByte(0x62);
        co_await seedOpFeeParams(storage);
        co_await seedSender(sender, 300'000, "0");
        co_await seedInvalidTarget(target);

        auto tx = makeEip1559Tx(transactionFactory, sender, target, 50'000);
        auto header = makeBlockHeader();
        auto receipt = co_await executor.executeTransaction(
            storage, header, *tx, contextId++, ledgerConfig, false);

        BOOST_REQUIRE(receipt);
        BOOST_CHECK_NE(receipt->status(), 0);
        BOOST_CHECK_GT(receipt->gasUsed(), 0);
        auto const rollup = opstack_tx::buildRollupCostData(*tx);
        BOOST_REQUIRE(rollup.has_value());
        auto const expectedL1 = l1CostFjord(*rollup, seededFeeParams());
        BOOST_REQUIRE(receipt->l1Fee().has_value());
        BOOST_CHECK_EQUAL(parseHexU256(receipt->l1Fee().value()), expectedL1);

        auto const expectedOperator =
            operatorCostIsthmus(static_cast<uint64_t>(receipt->gasUsed()), seededFeeParams());
        BOOST_REQUIRE(receipt->operatorFee().has_value());
        BOOST_CHECK_EQUAL(parseHexU256(receipt->operatorFee().value()), expectedOperator);

        ledger::account::EVMAccount senderAccount(storage, sender, false);
        BOOST_CHECK_EQUAL(co_await senderAccount.balance(), u256(300'000));

        ledger::account::EVMAccount l1Recipient(storage, OP_L1_FEE_RECIPIENT, false);
        BOOST_CHECK_EQUAL(co_await l1Recipient.balance(), u256(0));
    }());
}

BOOST_AUTO_TEST_CASE(second_transaction_rejected_when_block_gas_exhausted)
{
    task::syncWait([this]() -> task::Task<void> {
        auto const sender1 = addressFromLastByte(0x91);
        auto const sender2 = addressFromLastByte(0x92);
        auto const target1 = addressFromLastByte(0xa1);
        auto const target2 = addressFromLastByte(0xa2);
        co_await seedOpFeeParams(storage);
        co_await seedSender(sender1, 1'000'000, "0");
        co_await seedSender(sender2, 1'000'000, "0");

        // 220k block pool: tx1 reserves 200k, returns unused gas, leaving ~199k — below tx2's 200k
        // cap.
        executor.beginBlock(220'000);
        auto header = makeBlockHeader();

        auto tx1 = makeEip1559Tx(transactionFactory, sender1, target1, 200'000);
        auto receipt1 = co_await executor.executeTransaction(
            storage, header, *tx1, contextId++, ledgerConfig, false);
        BOOST_REQUIRE(receipt1);
        BOOST_CHECK_EQUAL(receipt1->status(), 0);

        auto tx2 = makeEip1559Tx(transactionFactory, sender2, target2, 200'000);
        auto receipt2 = co_await executor.executeTransaction(
            storage, header, *tx2, contextId++, ledgerConfig, false);
        BOOST_REQUIRE(receipt2);
        BOOST_CHECK_EQUAL(
            receipt2->status(), static_cast<int32_t>(protocol::TransactionStatus::OutOfGasLimit));

        executor.endBlock();
    }());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::test
