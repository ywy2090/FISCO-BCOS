#define BOOST_TEST_MODULE OpStackExecuteViaHostSmokeTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/OpStackExecutionBridge.h"
#include "bcos-evm/opstack/OpStackForkSchedule.h"
#include "bcos-framework/executor/OpStackTxType.h"
#include "helpers/InMemoryEvmStateReader.h"
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

u256 balanceFromDiff(const state::StateDiff& diff, const evmc_address& address)
{
    auto const it = diff.accounts.find(address);
    if (it == diff.accounts.end())
    {
        return 0;
    }
    return it->second.balance;
}

OpStackExecutionRequest makeBaseInput(state::test::InMemoryEvmStateReader& stateView, evmc::VM& vm,
    const crypto::Hash& hash, const evmc_address& sender, const evmc_address& recipient)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 50'000;
    message.sender = sender;
    message.recipient = recipient;
    message.code_address = recipient;

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
    input.blockInfo.coinbase = addressFromLastByte(0x99);
    input.revisionConfig = bcos::evm_standard::makeIsthmusRevisionConfig();
    input.txProps.warmDestination = true;
    input.rollupCostData = RollupCostData{.ones = 2, .fastLzSize = 3};
    input.opTxExecutor.m_l1FeeRecipient = OP_L1_FEE_RECIPIENT;
    return input;
}
}  // namespace

BOOST_AUTO_TEST_CASE(l1_fee_recipient_gets_fee_on_success)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x01);
    auto const target = addressFromLastByte(0x02);
    auto const l1FeeRecipient = OP_L1_FEE_RECIPIENT;
    auto const baseFeeRecipient = OP_BASE_FEE_RECIPIENT;
    auto const operatorFeeRecipient = OP_OPERATOR_FEE_RECIPIENT;
    auto const coinbase = addressFromLastByte(0x99);
    setOpFeeParams(stateView);

    state::Account senderAccount;
    senderAccount.balance = 300'000;
    stateView.insert_account(sender, senderAccount);

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    auto input = makeBaseInput(stateView, vm, hash, sender, target);
    auto output = task::syncWait(opStackExecute(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(balanceFromDiff(output.stateDiff, l1FeeRecipient), u256(50));
    BOOST_CHECK_GT(balanceFromDiff(output.stateDiff, baseFeeRecipient), u256(0));
    BOOST_CHECK_GT(balanceFromDiff(output.stateDiff, coinbase), u256(0));
    BOOST_CHECK_GT(balanceFromDiff(output.stateDiff, operatorFeeRecipient), u256(0));
    BOOST_REQUIRE(output.receiptMeta.l1Fee.has_value());
    BOOST_CHECK_EQUAL(*output.receiptMeta.l1Fee, u256(50));
    BOOST_REQUIRE(output.receiptMeta.operatorFee.has_value());
    BOOST_CHECK_GT(*output.receiptMeta.operatorFee, u256(0));
    auto const senderBalance = balanceFromDiff(output.stateDiff, sender);
    BOOST_CHECK_GT(senderBalance, u256(0));
    BOOST_CHECK_LT(senderBalance, u256(300'000));
}

BOOST_AUTO_TEST_CASE(insufficient_balance_fails_before_execution)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x11);
    auto const target = addressFromLastByte(0x12);
    auto const l1FeeRecipient = OP_L1_FEE_RECIPIENT;
    setOpFeeParams(stateView);

    state::Account senderAccount;
    senderAccount.balance = 100;
    stateView.insert_account(sender, senderAccount);

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    auto input = makeBaseInput(stateView, vm, hash, sender, target);
    auto output = task::syncWait(opStackExecute(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_INSUFFICIENT_BALANCE);
    BOOST_CHECK_EQUAL(balanceFromDiff(output.stateDiff, l1FeeRecipient), u256(0));
}

BOOST_AUTO_TEST_CASE(revert_refunds_unused_gas_and_keeps_l1_fee)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x21);
    auto const target = addressFromLastByte(0x22);
    auto const l1FeeRecipient = OP_L1_FEE_RECIPIENT;
    setOpFeeParams(stateView);

    state::Account senderAccount;
    senderAccount.balance = 300'000;
    stateView.insert_account(sender, senderAccount);

    state::Account targetAccount;
    targetAccount.code = {0x60, 0x00, 0x60, 0x00, 0xfd};  // PUSH1 0 PUSH1 0 REVERT
    stateView.insert_account(target, targetAccount);

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    auto input = makeBaseInput(stateView, vm, hash, sender, target);
    auto output = task::syncWait(opStackExecute(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_REVERT);
    BOOST_CHECK_GT(output.gasUsed, 0);
    BOOST_CHECK_EQUAL(balanceFromDiff(output.stateDiff, l1FeeRecipient), u256(50));

    auto const senderBalance = balanceFromDiff(output.stateDiff, sender);
    BOOST_CHECK_GT(senderBalance, u256(0));
    BOOST_CHECK_LT(senderBalance, u256(300'000));
}

BOOST_AUTO_TEST_CASE(hard_failure_still_refunds_unused_gas_and_routes_fees)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x31);
    auto const target = addressFromLastByte(0x32);
    auto const l1FeeRecipient = OP_L1_FEE_RECIPIENT;
    setOpFeeParams(stateView);

    state::Account senderAccount;
    senderAccount.balance = 1'000'000;
    stateView.insert_account(sender, senderAccount);

    state::Account targetAccount;
    targetAccount.code = {0xfe};  // INVALID
    stateView.insert_account(target, targetAccount);

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    auto input = makeBaseInput(stateView, vm, hash, sender, target);
    auto output = task::syncWait(opStackExecute(std::move(input)));

    BOOST_CHECK(output.evmcResult.status_code != EVMC_SUCCESS);
    BOOST_CHECK_GT(balanceFromDiff(output.stateDiff, sender), u256(0));
    BOOST_CHECK_GT(balanceFromDiff(output.stateDiff, l1FeeRecipient), u256(0));
}

BOOST_AUTO_TEST_CASE(pre_fjord_schedule_throws_on_user_tx)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x41);
    auto const target = addressFromLastByte(0x42);
    setOpFeeParams(stateView);

    state::Account senderAccount;
    senderAccount.balance = 300'000;
    stateView.insert_account(sender, senderAccount);

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    auto input = makeBaseInput(stateView, vm, hash, sender, target);
    input.forkSchedule = OpStackForkSchedule{.fjordTime = 100, .isthmusTime = 0};
    input.blockInfo.timestamp = 50;

    BOOST_CHECK_THROW(task::syncWait(opStackExecute(std::move(input))), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(deposit_pre_fjord_schedule_no_throw)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x51);
    auto const target = addressFromLastByte(0x52);

    state::Account senderAccount;
    senderAccount.balance = 0;
    stateView.insert_account(sender, senderAccount);

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    auto input = makeBaseInput(stateView, vm, hash, sender, target);
    input.revisionConfig.revision = EVMC_CANCUN;
    input.web3TypedTxKind = bcos::executor::DEPOSIT_TX_TYPE;
    input.depositTx = OpStackDepositTx{
        .from = sender, .to = target, .mint = u256(100), .value = 0, .gas = 50'000};
    input.forkSchedule = OpStackForkSchedule{.fjordTime = 100, .isthmusTime = 0};
    input.blockInfo.timestamp = 50;

    auto output = task::syncWait(opStackExecute(std::move(input)));
    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
}

BOOST_AUTO_TEST_CASE(host_pre_isthmus_operator_recipient_zero)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x61);
    auto const target = addressFromLastByte(0x62);
    auto const operatorFeeRecipient = OP_OPERATOR_FEE_RECIPIENT;
    setOpFeeParams(stateView);

    state::Account senderAccount;
    senderAccount.balance = 300'000;
    stateView.insert_account(sender, senderAccount);

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    auto input = makeBaseInput(stateView, vm, hash, sender, target);
    input.forkSchedule = OpStackForkSchedule{.fjordTime = 0, .isthmusTime = 100};
    input.blockInfo.timestamp = 50;

    auto output = task::syncWait(opStackExecute(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(balanceFromDiff(output.stateDiff, operatorFeeRecipient), u256(0));
    BOOST_CHECK(
        !output.receiptMeta.operatorFee.has_value() || *output.receiptMeta.operatorFee == u256(0));
}

BOOST_AUTO_TEST_CASE(orthogonality_non_isthmus_revision_with_isthmus_fork_schedule)
{
    state::test::InMemoryEvmStateReader stateView;
    auto const sender = addressFromLastByte(0x71);
    auto const target = addressFromLastByte(0x72);
    auto const operatorFeeRecipient = OP_OPERATOR_FEE_RECIPIENT;
    setOpFeeParams(stateView);

    state::Account senderAccount;
    senderAccount.balance = 300'000;
    stateView.insert_account(sender, senderAccount);

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    auto input = makeBaseInput(stateView, vm, hash, sender, target);
    input.revisionConfig.revision = EVMC_CANCUN;
    input.forkSchedule = makeIsthmusPlusForkSchedule();

    auto output = task::syncWait(opStackExecute(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_GT(balanceFromDiff(output.stateDiff, operatorFeeRecipient), u256(0));
}
}  // namespace bcos::evm::test
