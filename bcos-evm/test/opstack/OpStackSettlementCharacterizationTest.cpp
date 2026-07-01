#define BOOST_TEST_MODULE OpStackSettlementCharacterizationTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/gas/TxIntrinsicGas.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/opstack/apply/ApplyOpStackMessage.h"
#include "bcos-evm/opstack/policy/OpStackConstants.h"
#include "bcos-evm/opstack/policy/OpStackIsthmusRevision.h"
#include "helpers/InMemoryStateView.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
// Task 6 oracle (pre-refactor baseline, normal L2 user tx):
// - IntrinsicRejected (gas limit below intrinsic): output.gasUsed == 0, status EVMC_OUT_OF_GAS
// - Completed empty-account CALL (50_000 gas limit, Isthmus): output.gasUsed == 21000, status
// EVMC_SUCCESS

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
    l1BlockAccount.storage[state::toEvmC(OPERATOR_FEE_PARAMS_SLOT)] =
        packOperatorFeeParams(1'000'000, 5);
    stateView.insert_account(OP_L1_BLOCK_PREDEPLOY, std::move(l1BlockAccount));
}

OpStackMessageRequest makeNormalL2Input(state::test::InMemoryStateView& stateView, evmc::VM& vm,
    const crypto::Hash& hash, const evmc_address& sender, const evmc_address& recipient,
    int64_t gasLimit)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = gasLimit;
    message.sender = sender;
    message.recipient = recipient;
    message.code_address = recipient;

    OpStackMessageRequest input;
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
    input.revisionConfig = bcos::evm::makeIsthmusRevisionConfig();
    input.txProps.warmDestination = true;
    input.rollupCostData = RollupCostData{.ones = 2, .fastLzSize = 3};
    input.opTxExecutor.m_l1FeeRecipient = OP_L1_FEE_RECIPIENT;
    return input;
}
}  // namespace

BOOST_AUTO_TEST_CASE(characterize_intrinsic_reject_gas_used_is_zero)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x01);
    auto const recipient = addressFromLastByte(0x02);
    setOpFeeParams(stateView);

    state::Account senderAccount;
    senderAccount.balance = 300'000;
    senderAccount.nonce = 0;
    stateView.insert_account(sender, senderAccount);
    stateView.insert_account(recipient, state::Account{});

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;

    evmc_message probe{};
    probe.kind = EVMC_CALL;
    auto const intrinsic = gas::computeTxIntrinsicGas(probe, {}, 0u);
    auto const gasBelowIntrinsic =
        static_cast<int64_t>(intrinsic.preExecutionDebit() + gas::calcAuthTupleIntrinsicGas(0)) - 1;

    auto input = makeNormalL2Input(stateView, vm, hash, sender, recipient, gasBelowIntrinsic);
    auto output = task::syncWait(applyOpStackMessage(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_OUT_OF_GAS);
    BOOST_CHECK_EQUAL(output.gasUsed, int64_t{0});
    BOOST_CHECK(!output.receiptMeta.l1Fee.has_value());
}

BOOST_AUTO_TEST_CASE(characterize_completed_call_gas_used_positive)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x11);
    auto const recipient = addressFromLastByte(0x12);
    setOpFeeParams(stateView);

    state::Account senderAccount;
    senderAccount.balance = 300'000;
    senderAccount.nonce = 0;
    stateView.insert_account(sender, senderAccount);
    stateView.insert_account(recipient, state::Account{});

    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;

    auto input = makeNormalL2Input(stateView, vm, hash, sender, recipient, 50'000);
    auto output = task::syncWait(applyOpStackMessage(std::move(input)));

    BOOST_CHECK_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_GT(output.gasUsed, int64_t{0});
}
}  // namespace bcos::evm::test
