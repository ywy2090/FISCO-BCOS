#define BOOST_TEST_MODULE OpStack67802537KernelSmokeTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/opstack/OpStackChainPolicy.h"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/OpStackExecute.h"
#include "helpers/ApplyStateDiffToView.h"
#include "helpers/InMemoryStateView.h"
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
    l1BlockAccount.storage[state::toEvmC(OPERATOR_FEE_PARAMS_SLOT)] =
        packOperatorFeeParams(1'000'000, 5);
    stateView.insert_account(OP_L1_BLOCK_PREDEPLOY, std::move(l1BlockAccount));
}

OpStackExecutionRequest makeIsthmusBaseInput(
    state::test::InMemoryStateView& stateView, evmc::VM& vm, const crypto::Hash& hash)
{
    auto const sender = addressFromLastByte(0x01);
    state::Account senderAccount;
    senderAccount.balance = 2'000'000;
    stateView.insert_account(sender, senderAccount);
    setOpFeeParams(stateView);

    OpStackExecutionRequest input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hash;
    input.message.sender = sender;
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
    input.skipTransactionChecks = true;
    input.skipNonceChecks = true;
    return input;
}
}  // namespace

BOOST_AUTO_TEST_CASE(opStackExecute_g1msm_k2_gas_matches_geth_isthmus)
{
    state::test::InMemoryStateView stateView;
    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    auto input = makeIsthmusBaseInput(stateView, vm, hash);

    auto const g1MsmAddr = addressFromLastByte(0x0c);
    bcos::bytes msmInput(320, 0);
    int64_t const txGas = 500'000;

    input.message.kind = EVMC_CALL;
    input.message.gas = txGas;
    input.message.recipient = g1MsmAddr;
    input.message.code_address = g1MsmAddr;
    input.message.input_data = msmInput.data();
    input.message.input_size = msmInput.size();

    auto output = task::syncWait(applyOpStackMessage(std::move(input)));
    BOOST_REQUIRE_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(txGas - output.evmcResult.gas_left, 45056);
}

BOOST_AUTO_TEST_CASE(opStackExecute_created_in_tx_selfdestruct_clears_code_isthmus)
{
    state::test::InMemoryStateView stateView;
    evmc::VM vm{evmc_create_evmone()};
    FakeHash hash;
    auto input = makeIsthmusBaseInput(stateView, vm, hash);

    auto const sender = input.message.sender;
    auto const beneficiary = addressFromLastByte(0xbb);
    stateView.insert_account(beneficiary, state::Account{});

    auto const initCode = bcos::fromHex("730000000000000000000000000000000000bbff");
    input.message.kind = EVMC_CREATE;
    input.message.gas = 500'000;
    input.message.recipient = {};
    input.message.code_address = {};
    input.message.input_data = initCode.data();
    input.message.input_size = initCode.size();
    input.message.value = {};

    auto const predictedAddr = state::predictLegacyCreateAddress(sender, 0);
    auto output = task::syncWait(applyOpStackMessage(std::move(input)));
    BOOST_REQUIRE_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);

    applyStateDiffToView(output.stateDiff, stateView);
    auto const createAddr = state::isZeroAddress(output.evmcResult.create_address) ?
                                predictedAddr :
                                output.evmcResult.create_address;
    BOOST_CHECK(stateView.get_code(createAddr).empty());
    BOOST_CHECK_EQUAL(stateView.get_balance(createAddr), u256(0));
}

}  // namespace bcos::evm::test
