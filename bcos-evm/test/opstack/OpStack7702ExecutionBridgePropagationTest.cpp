#define BOOST_TEST_MODULE OpStack7702ExecuteViaHostPropagationTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/eth/Eip7702.h"
#include "bcos-evm/eth/ExecuteMessage.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/gas/EthTxGasSettlement.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/OpStackExecutionBridge.h"
#include "bcos-evm/opstack/OpStackVmHostPolicy.h"
#include "bcos-evm/opstack/fee/OpStackGasSettlement.h"
#include "state/InMemoryEvmStateReader.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
class FakeHash final : public crypto::Hash
{
public:
    crypto::HashType hash(bytesConstRef /*unused*/) const override { return crypto::HashType{}; }
    bcos::crypto::hasher::AnyHasher hasher() const override { return {}; }
};

evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
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

void setOpFeeParams(state::test::InMemoryEvmStateReader& stateView)
{
    state::Account l1BlockAccount;
    l1BlockAccount.storage[state::toEvmC(L1_BASE_FEE_SLOT)] = state::toEvmC(u256(31'250));
    l1BlockAccount.storage[state::toEvmC(L1_BLOB_BASE_FEE_SLOT)] = state::toEvmC(u256(0));
    l1BlockAccount.storage[state::toEvmC(L1_FEE_SCALARS_SLOT)] = packFeeScalars(1, 0);
    l1BlockAccount.storage[state::toEvmC(OPERATOR_FEE_PARAMS_SLOT)] =
        packOperatorFeeParams(1'000'000, 5);
    stateView.insert_account(OP_L1_BLOCK_PREDEPLOY, std::move(l1BlockAccount));
}

OpStackExecutionRequest make7702Input(evmc_address sender, evmc_address recipient,
    evmc_address delegationTarget, uint64_t gasLimit, uint64_t authTupleCount)
{
    OpStackExecutionRequest input;
    input.message.kind = EVMC_CALL;
    input.message.gas = static_cast<int64_t>(gasLimit);
    input.message.sender = sender;
    input.message.recipient = recipient;
    input.message.code_address = recipient;
    input.gasTipCap = 1;
    input.gasFeeCap = 2;
    input.blockInfo.number = 1;
    input.blockInfo.timestamp = 12345;
    input.blockInfo.gasLimit = 30'000'000;
    input.blockInfo.baseFee = 1;
    input.blockInfo.chainId = 1;
    input.blockInfo.coinbase = addressFromLastByte(0x99);
    input.revisionConfig = bcos::evm_standard::makeIsthmusRevisionConfig();
    input.txProps.warmDestination = true;
    input.rollupCostData = RollupCostData{.ones = 2, .fastLzSize = 3};
    input.opTxExecutor.m_l1FeeRecipient = OP_L1_FEE_RECIPIENT;
    input.skipTransactionChecks = true;
    input.skipNonceChecks = true;
    input.authorizationListPresent = true;
    for (uint64_t i = 0; i < authTupleCount; ++i)
    {
        input.authorizations.push_back(
            {.chainId = u256(1), .authority = sender, .address = delegationTarget, .nonce = i + 1});
    }
    return input;
}
}  // namespace

BOOST_AUTO_TEST_CASE(opStackExecute_propagates_authorizations_to_executeMessage)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x31);
    auto const recipient = addressFromLastByte(0x32);
    auto const delegationTarget = addressFromLastByte(0x42);
    setOpFeeParams(stateView);

    state::Account senderAccount;
    senderAccount.nonce = 0;
    senderAccount.balance = 1'000'000;
    stateView.insert_account(sender, senderAccount);
    stateView.insert_account(recipient, state::Account{});

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 200'000;
    message.sender = sender;
    message.recipient = recipient;
    message.code_address = recipient;

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;

    OpStackExecutionRequest input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hash;
    input.message = message;
    input.gasTipCap = 1;
    input.gasFeeCap = 2;
    input.blockInfo.number = 1;
    input.blockInfo.timestamp = 12345;
    input.blockInfo.gasLimit = 30'000'000;
    input.blockInfo.baseFee = 1;
    input.blockInfo.chainId = 1;
    input.blockInfo.coinbase = addressFromLastByte(0x99);
    input.revisionConfig = bcos::evm_standard::makeIsthmusRevisionConfig();
    input.txProps.warmDestination = true;
    input.rollupCostData = RollupCostData{.ones = 2, .fastLzSize = 3};
    input.opTxExecutor.m_l1FeeRecipient = OP_L1_FEE_RECIPIENT;
    input.skipTransactionChecks = true;
    input.skipNonceChecks = true;
    input.authorizationListPresent = true;
    input.authorizations.push_back(
        {.chainId = u256(1), .authority = sender, .address = delegationTarget, .nonce = 1});

    auto output = task::syncWait(opStackExecute(std::move(input)));
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);

    auto const it = output.stateDiff.accounts.find(sender);
    BOOST_REQUIRE(it != output.stateDiff.accounts.end());
    auto const& installedCode = it->second.code;
    BOOST_CHECK_EQUAL(installedCode.size(), size_t(23));
    BOOST_CHECK_EQUAL(installedCode[0], 0xEF);
    BOOST_CHECK_EQUAL(installedCode[1], 0x01);
    BOOST_CHECK_EQUAL(installedCode[2], 0x00);
    BOOST_CHECK_EQUAL(it->second.nonce, uint64_t(2));
}

BOOST_AUTO_TEST_CASE(opStackExecute_rejects_7702_intrinsic_below_25000_per_tuple)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x33);
    auto const recipient = addressFromLastByte(0x34);
    auto const delegationTarget = addressFromLastByte(0x43);
    setOpFeeParams(stateView);

    state::Account senderAccount;
    senderAccount.nonce = 0;
    senderAccount.balance = 1'000'000;
    stateView.insert_account(sender, senderAccount);
    stateView.insert_account(recipient, state::Account{});

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;

    auto const intrinsicGas =
        static_cast<uint64_t>(gas::TX_BASE_GAS + gas::calcAuthTupleIntrinsicGas(1));
    BOOST_CHECK_EQUAL(intrinsicGas, gas::TX_BASE_GAS + PER_EMPTY_ACCOUNT_COST);

    auto input = make7702Input(sender, recipient, delegationTarget, intrinsicGas - 1, 1);
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hash;

    auto output = task::syncWait(opStackExecute(std::move(input)));
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_OUT_OF_GAS);
}

BOOST_AUTO_TEST_CASE(opStackExecute_charges_7702_intrinsic_25000_per_tuple)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x35);
    auto const recipient = addressFromLastByte(0x36);
    auto const delegationTarget = addressFromLastByte(0x44);
    setOpFeeParams(stateView);

    state::Account senderAccount;
    senderAccount.nonce = 0;
    senderAccount.balance = 1'000'000;
    stateView.insert_account(sender, senderAccount);
    stateView.insert_account(recipient, state::Account{});

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;

    auto const authTupleCount = uint64_t(2);
    auto const intrinsicGas =
        static_cast<uint64_t>(gas::TX_BASE_GAS + gas::calcAuthTupleIntrinsicGas(authTupleCount));
    BOOST_CHECK_EQUAL(intrinsicGas, gas::TX_BASE_GAS + authTupleCount * PER_EMPTY_ACCOUNT_COST);

    auto input =
        make7702Input(sender, recipient, delegationTarget, intrinsicGas - 1, authTupleCount);
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hash;

    auto output = task::syncWait(opStackExecute(std::move(input)));
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_OUT_OF_GAS);
}

BOOST_AUTO_TEST_CASE(opStackExecute_refunds_existence_cost_when_authority_already_exists)
{
    constexpr uint64_t kExistenceRefund = PER_EMPTY_ACCOUNT_COST - PER_AUTH_BASE_COST;
    BOOST_CHECK_EQUAL(kExistenceRefund, 12'500u);

    auto const authority = addressFromLastByte(0x53);
    auto const delegationTarget = addressFromLastByte(0x55);
    std::vector<SetCodeAuthorization> const auths = {
        {.chainId = u256(1), .authority = authority, .address = delegationTarget, .nonce = 0}};

    {
        state::test::InMemoryEvmStateReader seededView;
        state::Account authorityAccount;
        authorityAccount.nonce = 0;
        seededView.insert_account(authority, authorityAccount);
        state::State seededState(seededView);
        applyAuthorizations(seededState, auths, u256(1));
        BOOST_CHECK_EQUAL(seededState.get_refund(), kExistenceRefund);
    }
    {
        state::test::InMemoryEvmStateReader freshView;
        state::State freshState(freshView);
        applyAuthorizations(freshState, auths, u256(1));
        BOOST_CHECK_EQUAL(freshState.get_refund(), 0u);
    }

    static bcos::bytes const kReturn42Code = {
        0x60, 0x2a, 0x60, 0x00, 0x52, 0x60, 0x20, 0x60, 0x00, 0xf3};

    auto const runOpStackCase = [&](bool preSeedAuthority) {
        state::test::InMemoryEvmStateReader stateView;
        auto const sender = addressFromLastByte(0x51);
        auto const recipient = addressFromLastByte(0x52);
        auto const authorityAddr = addressFromLastByte(0x53);
        auto const delegationTargetAddr = addressFromLastByte(0x55);
        setOpFeeParams(stateView);

        state::Account senderAccount;
        senderAccount.nonce = 0;
        senderAccount.balance = 10'000'000;
        stateView.insert_account(sender, senderAccount);

        state::Account recipientAccount;
        recipientAccount.code = kReturn42Code;
        stateView.insert_account(recipient, std::move(recipientAccount));

        if (preSeedAuthority)
        {
            state::Account authorityAccount;
            authorityAccount.nonce = 0;
            stateView.insert_account(authorityAddr, authorityAccount);
        }

        evmc::VM vm{evmc_create_evmone()};
        FakeHash hash;

        auto input = make7702Input(sender, recipient, delegationTargetAddr, 200'000, 1);
        input.authorizations.clear();
        input.authorizations.push_back({.chainId = u256(1),
            .authority = authorityAddr,
            .address = delegationTargetAddr,
            .nonce = 0});
        input.stateView = &stateView;
        input.vm = &vm;
        input.hashImpl = &hash;

        auto output = task::syncWait(opStackExecute(std::move(input)));
        BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
        auto const authIt = output.stateDiff.accounts.find(authorityAddr);
        BOOST_REQUIRE(authIt != output.stateDiff.accounts.end());
        BOOST_CHECK_EQUAL(authIt->second.code.size(), size_t(23));
    };

    auto const runExecuteMessageRefund = [&](bool preSeedAuthority) {
        state::test::InMemoryEvmStateReader stateView;
        auto const sender = addressFromLastByte(0x61);
        auto const recipient = addressFromLastByte(0x62);
        auto const authorityAddr = addressFromLastByte(0x63);
        auto const delegationTargetAddr = addressFromLastByte(0x65);

        stateView.insert_account(sender, state::Account{.balance = 1'000'000});

        state::Account recipientAccount;
        recipientAccount.code = kReturn42Code;
        stateView.insert_account(recipient, std::move(recipientAccount));

        if (preSeedAuthority)
        {
            state::Account authorityAccount;
            authorityAccount.nonce = 0;
            stateView.insert_account(authorityAddr, authorityAccount);
        }

        evmc_message message{};
        message.kind = EVMC_CALL;
        auto const gasLimit = int64_t(300'000);
        message.gas = gasLimit;
        message.sender = sender;
        message.recipient = recipient;
        message.code_address = recipient;

        evmc::VM vm{evmc_create_evmone()};
        state::State state(stateView);
        OpVmHostPolicy extension(&state);

        ExecuteMessageInput input;
        input.stateView = &state;
        input.vm = &vm;
        input.message = message;
        input.blockInfo.number = 1;
        input.blockInfo.chainId = 1;
        input.blockInfo.gasLimit = 30'000'000;
        input.revisionConfig = bcos::evm_standard::makeIsthmusRevisionConfig();
        input.authorizationListPresent = true;
        input.authorizations.push_back({.chainId = u256(1),
            .authority = authorityAddr,
            .address = delegationTargetAddr,
            .nonce = 0});
        input.extension = &extension;

        auto const intrinsicGas = static_cast<int64_t>(
            gas::TX_BASE_GAS + gas::calcAuthTupleIntrinsicGas(input.authorizations.size()));
        input.message.gas -= intrinsicGas;

        auto output = executeMessage(std::move(input));
        BOOST_CHECK_EQUAL(output.result.status_code, EVMC_SUCCESS);
        BOOST_CHECK_EQUAL(state.get_refund(), preSeedAuthority ? kExistenceRefund : 0u);
    };

    runOpStackCase(true);
    runOpStackCase(false);
    runExecuteMessageRefund(true);
    runExecuteMessageRefund(false);

    // postExecuteGasSettlement with peakGasUsed / 5 >= 12500: existence credit lowers gasUsed by
    // 12500.
    constexpr uint64_t kSettlementGasLimit = 80'000;
    constexpr uint64_t kSettlementGasLeft = 10'000;
    auto const cappedExisting =
        postExecuteGasSettlement(kSettlementGasLimit, kSettlementGasLeft, kExistenceRefund, 0);
    auto const cappedFresh =
        postExecuteGasSettlement(kSettlementGasLimit, kSettlementGasLeft, 0, 0);
    BOOST_CHECK_EQUAL(cappedFresh.gasUsed - cappedExisting.gasUsed, kExistenceRefund);
}

}  // namespace bcos::evm::test
