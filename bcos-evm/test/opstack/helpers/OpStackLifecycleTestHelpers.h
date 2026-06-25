#pragma once

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/eth/RevisionConfig.h"
#include "bcos-evm/eth/gas/TxIntrinsicGas.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "bcos-evm/opstack/OpStackDepositTx.h"
#include "bcos-evm/opstack/OpStackExecutionBridge.h"
#include "bcos-evm/opstack/OpStackSettlement.h"
#include "bcos-framework/executor/OpStackTxType.h"
#include "helpers/InMemoryEvmStateReader.h"
#include <evmc/evmc.h>
#include <functional>

namespace bcos::evm::test
{

class LifecycleFakeHash final : public crypto::Hash
{
public:
    crypto::HashType hash(bytesConstRef /*unused*/) const override { return crypto::HashType{}; }
    bcos::crypto::hasher::AnyHasher hasher() const override { return {}; }
};

inline evmc_address lifecycleAddressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

struct LifecycleGasPoolSpy
{
    int subGasCallCount{0};
    int returnGasCallCount{0};
    uint64_t lastSubGas{0};
    uint64_t lastReturnRemaining{0};
    uint64_t lastReturnUsed{0};
    bool subGasResult{true};

    GasPoolHooks hooks()
    {
        GasPoolHooks out{};
        out.subGas = [this](uint64_t gasLimit) {
            ++subGasCallCount;
            lastSubGas = gasLimit;
            return subGasResult;
        };
        out.returnGas = [this](uint64_t gasRemaining, uint64_t gasUsed) {
            ++returnGasCallCount;
            lastReturnRemaining = gasRemaining;
            lastReturnUsed = gasUsed;
        };
        return out;
    }
};

inline evmc_bytes32 packLifecycleFeeScalars(uint32_t baseFeeScalar, uint32_t blobBaseFeeScalar)
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

inline evmc_bytes32 packLifecycleOperatorFeeParams(
    uint32_t operatorFeeScalar, uint64_t operatorFeeConstant)
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

inline void setLifecycleOpFeeParams(state::test::InMemoryEvmStateReader& stateView)
{
    state::Account l1BlockAccount;
    l1BlockAccount.storage[state::toEvmC(L1_BASE_FEE_SLOT)] = state::toEvmC(u256(31'250));
    l1BlockAccount.storage[state::toEvmC(L1_BLOB_BASE_FEE_SLOT)] = state::toEvmC(u256(0));
    l1BlockAccount.storage[state::toEvmC(L1_FEE_SCALARS_SLOT)] = packLifecycleFeeScalars(1, 0);
    l1BlockAccount.storage[state::toEvmC(OPERATOR_FEE_PARAMS_SLOT)] =
        packLifecycleOperatorFeeParams(0, 0);
    stateView.insert_account(OP_L1_BLOCK_PREDEPLOY, std::move(l1BlockAccount));
}

inline u256 lifecycleBalanceFromDiff(const state::StateDiff& diff, const evmc_address& address)
{
    auto const it = diff.accounts.find(address);
    if (it == diff.accounts.end())
    {
        return 0;
    }
    return it->second.balance;
}

inline uint64_t lifecycleNonceFromDiff(
    const state::StateDiff& diff, const evmc_address& address, uint64_t fallbackNonce)
{
    auto const it = diff.accounts.find(address);
    if (it == diff.accounts.end())
    {
        return fallbackNonce;
    }
    return it->second.nonce;
}

inline void installAlwaysRevertCode(
    state::test::InMemoryEvmStateReader& stateView, const evmc_address& recipient)
{
    // PUSH1 0 PUSH1 0 REVERT
    bcos::bytes revertCode{0x60, 0x00, 0x60, 0x00, 0xfd};
    stateView.insert_account(recipient, state::Account{.code = std::move(revertCode)});
}

inline OpStackExecutionRequest makeLifecycleNormalInput(
    state::test::InMemoryEvmStateReader& stateView, evmc::VM& vm, const crypto::Hash& hash,
    const evmc_address& sender, const evmc_address& recipient, int64_t gasLimit,
    LifecycleGasPoolSpy& gasPoolSpy)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = gasLimit;
    message.sender = sender;
    message.recipient = recipient;
    message.code_address = recipient;

    OpStackExecutionRequest input;
    input.stateView = &stateView;
    input.vm = &vm;
    input.hashImpl = &hash;
    input.message = message;
    input.nonce = 0;
    input.gasTipCap = 5;
    input.gasFeeCap = 10;
    input.blockInfo.number = 1;
    input.blockInfo.timestamp = 12345;
    input.blockInfo.gasLimit = 30'000'000;
    input.blockInfo.baseFee = 2;
    input.blockInfo.coinbase = lifecycleAddressFromLastByte(0x99);
    input.revisionConfig = bcos::evm_standard::makeIsthmusRevisionConfig();
    input.txProps.warmDestination = true;
    input.rollupCostData = RollupCostData{.ones = 1, .fastLzSize = 1};
    auto const poolHooks = gasPoolSpy.hooks();
    input.gasPoolSubGasHook = poolHooks.subGas;
    input.gasPoolReturnGasHook = poolHooks.returnGas;
    return input;
}

inline OpStackExecutionRequest makeLifecycleDepositInput(
    state::test::InMemoryEvmStateReader& stateView, evmc::VM& vm, const crypto::Hash& hash,
    const evmc_address& sender, const evmc_address& recipient, int64_t gasLimit,
    LifecycleGasPoolSpy& gasPoolSpy, std::optional<u256> mint = std::nullopt)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = gasLimit;
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
    input.revisionConfig = bcos::evm_standard::makeIsthmusRevisionConfig();
    input.txProps.warmDestination = true;
    input.skipTransactionChecks = true;
    input.web3TypedTxKind = bcos::executor::DEPOSIT_TX_TYPE;
    input.depositTx = OpStackDepositTx{
        .from = sender, .to = recipient, .value = 0, .gas = static_cast<uint64_t>(message.gas)};
    if (mint.has_value())
    {
        input.depositTx->mint = *mint;
    }
    auto const poolHooks = gasPoolSpy.hooks();
    input.gasPoolSubGasHook = poolHooks.subGas;
    input.gasPoolReturnGasHook = poolHooks.returnGas;
    return input;
}

inline int64_t lifecycleGasBelowIntrinsic(evmc_message const& message)
{
    auto const intrinsic = gas::computeTxIntrinsicGas(message, {}, 0u);
    return static_cast<int64_t>(intrinsic.preExecutionDebit() + gas::calcAuthTupleIntrinsicGas(0)) -
           1;
}

}  // namespace bcos::evm::test
