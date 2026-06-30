#define BOOST_TEST_MODULE BlobGasBalanceTest

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/pipeline/TxPipelineContext.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/opstack/OpStackChainPolicy.h"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/OpStackExecute.h"
#include "bcos-evm/opstack/OpStackFeeSettlement.h"
#include "bcos-evm/opstack/OpStackSettlementFacade.h"
#include "helpers/InMemoryStateView.h"
#include "helpers/OpStackEntryPrecheck.h"
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

void setOpFeeParams(state::test::InMemoryStateView& stateView)
{
    state::Account l1BlockAccount;
    l1BlockAccount.storage[state::toEvmC(L1_BASE_FEE_SLOT)] = state::toEvmC(u256(31'250));
    l1BlockAccount.storage[state::toEvmC(L1_BLOB_BASE_FEE_SLOT)] = state::toEvmC(u256(0));
    l1BlockAccount.storage[state::toEvmC(L1_FEE_SCALARS_SLOT)] = packFeeScalars(1, 0);
    stateView.insert_account(OP_L1_BLOCK_PREDEPLOY, std::move(l1BlockAccount));
}

u256 balanceFromDiff(
    state::StateDiff const& diff, evmc_address const& address, u256 fallbackBalance)
{
    auto const it = diff.accounts.find(address);
    if (it == diff.accounts.end())
    {
        return fallbackBalance;
    }
    return it->second.balance;
}

h256 makeVersionedHash()
{
    h256 hash{};
    hash[0] = 0x01;
    hash[31] = 0x42;
    return hash;
}

OpStackExecutionRequest makeBlobPreCheckInput(evmc_address sender)
{
    OpStackExecutionRequest input;
    input.message.kind = EVMC_CALL;
    input.message.gas = 100'000;
    input.message.sender = sender;
    input.message.recipient = addressFromLastByte(0x92);
    input.message.code_address = input.message.recipient;
    input.nonce = 0;
    input.gasTipCap = 1;
    input.gasFeeCap = 2;
    input.revisionConfig.eip4844 = true;
    input.blockInfo.baseFee = 1;
    input.blockInfo.blobBaseFee = 1;
    return input;
}
}  // namespace

BOOST_AUTO_TEST_CASE(blob_hashes_without_blob_gas_fee_cap_is_rejected)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x90);
    stateView.insert_account(sender, state::Account{.balance = u256(1'000'000), .nonce = 0});
    state::State state(stateView);

    auto input = makeBlobPreCheckInput(sender);
    input.blobVersionedHashes.push_back(makeVersionedHash());
    // blobGasFeeCap left at default 0 — orchestration treats as under blobBaseFee (op-geth
    // ErrInsufficientFunds).

    auto error = runOpStackEntryPrecheck(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::InsufficientFunds);
}

BOOST_AUTO_TEST_CASE(blob_hashes_rejected_when_eip4844_disabled)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x8f);
    stateView.insert_account(sender, state::Account{.balance = u256(1'000'000), .nonce = 0});
    state::State state(stateView);

    auto input = makeBlobPreCheckInput(sender);
    input.revisionConfig.eip4844 = false;
    input.blobVersionedHashes.push_back(makeVersionedHash());
    input.blobGasFeeCap = 200;

    auto error = runOpStackEntryPrecheck(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::Malformed);
}

BOOST_AUTO_TEST_CASE(blob_gas_fee_cap_under_blob_base_fee_is_rejected)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x91);
    stateView.insert_account(sender, state::Account{.balance = u256(1'000'000), .nonce = 0});
    state::State state(stateView);

    auto input = makeBlobPreCheckInput(sender);
    input.blobVersionedHashes.push_back(makeVersionedHash());
    input.blobGasFeeCap = 0;

    auto error = runOpStackEntryPrecheck(input, stateView);
    BOOST_REQUIRE(error.has_value());
    // op-geth preCheck: maxFeePerBlobGas < blobBaseFee → ErrInsufficientFunds
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::InsufficientFunds);
}

BOOST_AUTO_TEST_CASE(buy_gas_deducts_blob_base_fee_times_blob_gas)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x93);
    auto const initialBalance = u256(3'000'000);
    stateView.insert_account(sender, state::Account{.balance = initialBalance, .nonce = 0});

    evmc_message msg{};
    msg.sender = sender;
    msg.gas = 1'000;
    auto revision = bcos::evm::makeIsthmusRevisionConfig();
    TxPipelineContext ctx{stateView, msg, revision, bcos::u256(0)};

    OpStackFeeSettlement executor;
    OpStackExecutionRequest input;
    input.gasTipCap = 1;
    input.gasFeeCap = 2;
    input.blockInfo.baseFee = 1;
    input.blockInfo.blobBaseFee = 10;
    input.blobGasFeeCap = 20;
    input.blobVersionedHashes.push_back(makeVersionedHash());

    OpStackFeeSidecar sidecar;
    OpStackSettlementFacade view{ctx, input, sidecar};

    auto const executionGasCost = u256(1'000) * u256(2);
    auto const blobGasCost = u256(OP_BLOB_GAS_PER_BLOB) * u256(10);
    auto const expectedDeduction = executionGasCost + blobGasCost;

    BOOST_REQUIRE(task::syncWait(executor.buyGas(view)));
    BOOST_CHECK_EQUAL(ctx.state.get_balance(sender), initialBalance - expectedDeduction);
}

BOOST_AUTO_TEST_CASE(buy_gas_rejects_insufficient_balance_for_blob_cost)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x94);
    stateView.insert_account(sender, state::Account{.balance = u256(1'500'000), .nonce = 0});

    evmc_message msg{};
    msg.sender = sender;
    msg.gas = 1'000;
    auto revision = bcos::evm::makeIsthmusRevisionConfig();
    TxPipelineContext ctx{stateView, msg, revision, bcos::u256(0)};

    OpStackFeeSettlement executor;
    OpStackExecutionRequest input;
    input.gasTipCap = 1;
    input.gasFeeCap = 2;
    input.blockInfo.baseFee = 1;
    input.blockInfo.blobBaseFee = 10;
    input.blobGasFeeCap = 20;
    input.blobVersionedHashes.push_back(makeVersionedHash());

    OpStackFeeSidecar sidecar;
    OpStackSettlementFacade view{ctx, input, sidecar};

    auto const balanceBefore = ctx.state.get_balance(sender);
    BOOST_REQUIRE(!task::syncWait(executor.buyGas(view)));
    BOOST_CHECK_EQUAL(ctx.evmcResult.status_code, EVMC_INSUFFICIENT_BALANCE);
    BOOST_CHECK_EQUAL(ctx.state.get_balance(sender), balanceBefore);
}

BOOST_AUTO_TEST_CASE(l1_blob_base_fee_slot_does_not_set_execution_blob_base_fee)
{
    state::test::InMemoryStateView stateView;
    auto const sender = addressFromLastByte(0x94);
    stateView.insert_account(sender, state::Account{.balance = u256(1'000'000), .nonce = 0});
    state::Account l1BlockAccount;
    l1BlockAccount.storage[state::toEvmC(L1_BLOB_BASE_FEE_SLOT)] = state::toEvmC(u256(999));
    stateView.insert_account(OP_L1_BLOCK_PREDEPLOY, std::move(l1BlockAccount));
    state::State state(stateView);

    auto input = makeBlobPreCheckInput(sender);
    input.blobVersionedHashes.push_back(makeVersionedHash());
    input.blobGasFeeCap = 0;

    auto error = runOpStackEntryPrecheck(input, stateView);
    BOOST_REQUIRE(error.has_value());
    BOOST_CHECK_EQUAL(error->status, protocol::TransactionStatus::InsufficientFunds);
}

BOOST_AUTO_TEST_CASE(opStackExecute_deducts_blob_fee_on_success)
{
    auto const initialBalance = u256(50'000'000'000);
    auto runCase = [&](bool withBlobVersionedHashes) -> u256 {
        state::test::InMemoryStateView stateView;
        auto const sender = addressFromLastByte(0x95);
        auto const target = addressFromLastByte(0x96);
        setOpFeeParams(stateView);
        stateView.insert_account(sender, state::Account{.balance = initialBalance, .nonce = 0});
        stateView.insert_account(target, state::Account{});

        evmc::VM vm{evmc_create_evmone()};
        FakeHash hash;

        OpStackExecutionRequest input;
        input.stateView = &stateView;
        input.vm = &vm;
        input.hashImpl = &hash;
        input.message.kind = EVMC_CALL;
        input.message.gas = 100'000;
        input.message.sender = sender;
        input.message.recipient = target;
        input.message.code_address = target;
        input.nonce = 0;
        input.gasTipCap = 1;
        input.gasFeeCap = 2;
        input.revisionConfig = bcos::evm::makeIsthmusRevisionConfig();
        input.blockInfo.baseFee = 1;
        input.blockInfo.blobBaseFee = 10;
        input.rollupCostData = RollupCostData{.ones = 2, .fastLzSize = 3};
        input.txProps.warmDestination = true;
        if (withBlobVersionedHashes)
        {
            input.blobGasFeeCap = 20;
            input.blobVersionedHashes.push_back(makeVersionedHash());
        }

        auto const output = task::syncWait(applyOpStackMessage(input));
        BOOST_REQUIRE_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
        return balanceFromDiff(output.stateDiff, sender, initialBalance);
    };

    auto const withoutBlobBalance = runCase(false);
    auto const withBlobBalance = runCase(true);
    auto const blobCost = u256(OP_BLOB_GAS_PER_BLOB) * u256(10);
    BOOST_CHECK_EQUAL(withoutBlobBalance - withBlobBalance, blobCost);
}
}  // namespace bcos::evm::test
