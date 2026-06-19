#define BOOST_TEST_MODULE OpStackExecuteViaHostSmokeTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/eth/state/hash_utils.hpp"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/OpStackExecuteViaHost.h"
#include "state/InMemoryStateView.h"
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

void setOpFeeParams(state::test::InMemoryStateView& stateView)
{
    state::Account l1BlockAccount;
    l1BlockAccount.storage[state::toEvmC(L1_BASE_FEE_SLOT)] = state::toEvmC(u256(31'250));
    l1BlockAccount.storage[state::toEvmC(L1_BLOB_BASE_FEE_SLOT)] = state::toEvmC(u256(0));
    l1BlockAccount.storage[state::toEvmC(L1_FEE_SCALARS_SLOT)] = packFeeScalars(1, 0);
    l1BlockAccount.storage[state::toEvmC(OPERATOR_FEE_PARAMS_SLOT)] = packOperatorFeeParams(0, 0);
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

OpStackExecuteViaHostInput makeBaseInput(state::test::InMemoryStateView& stateView, evmc::VM& vm,
    const crypto::Hash& hash, const evmc_address& sender, const evmc_address& recipient)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 50'000;
    message.sender = sender;
    message.recipient = recipient;
    message.code_address = recipient;

    OpStackExecuteViaHostInput input;
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
    input.revisionConfig.revision = EVMC_CANCUN;
    input.txProps.warmDestination = true;
    input.rollupCostData = RollupCostData{.ones = 2, .fastLzSize = 3};
    input.opTxExecutor.m_isIsthmus = true;
    input.opTxExecutor.m_l1FeeRecipient = OP_L1_FEE_RECIPIENT;
    return input;
}
}  // namespace

BOOST_AUTO_TEST_CASE(l1_fee_recipient_gets_fee_on_success)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x01);
    auto const target = addressFromLastByte(0x02);
    auto const l1FeeRecipient = OP_L1_FEE_RECIPIENT;
    setOpFeeParams(stateView);

    state::Account senderAccount;
    senderAccount.balance = 300'000;
    stateView.insert_account(sender, senderAccount);

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    auto input = makeBaseInput(stateView, vm, hash, sender, target);
    auto output = task::syncWait(opStackExecuteViaHost(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(balanceFromDiff(output.stateDiff, l1FeeRecipient), u256(50));
    auto const expectedSenderBalance = u256(300'000) - u256(50) - u256(output.gasUsed) * u256(2);
    BOOST_CHECK_EQUAL(balanceFromDiff(output.stateDiff, sender), expectedSenderBalance);
}

BOOST_AUTO_TEST_CASE(insufficient_balance_fails_before_execution)
{
    state::test::InMemoryStateView stateView;
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
    auto output = task::syncWait(opStackExecuteViaHost(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_INSUFFICIENT_BALANCE);
    BOOST_CHECK_EQUAL(balanceFromDiff(output.stateDiff, l1FeeRecipient), u256(0));
}

BOOST_AUTO_TEST_CASE(revert_refunds_unused_gas_and_keeps_l1_fee)
{
    state::test::InMemoryStateView stateView;
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
    auto output = task::syncWait(opStackExecuteViaHost(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_REVERT);
    BOOST_CHECK_GT(output.gasUsed, 0);
    BOOST_CHECK_EQUAL(balanceFromDiff(output.stateDiff, l1FeeRecipient), u256(50));

    auto const expectedSenderBalance = u256(300'000) - u256(50) - u256(output.gasUsed) * u256(2);
    BOOST_CHECK_EQUAL(balanceFromDiff(output.stateDiff, sender), expectedSenderBalance);
}
}  // namespace bcos::evm::test
