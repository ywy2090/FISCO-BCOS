#include "bcos-evm/evm-reference-tests/ExecuteViaEthAdapter.h"

#include "bcos-evm/eth/AccessList.h"
#include "bcos-evm/eth/ExecuteViaEth.h"
#include "bcos-evm/eth/gas/EthTxGasSettlement.h"
#include "bcos-evm/eth/state/hash_utils.hpp"
#include "bcos-evm/evm-reference-tests/GstStateHash.h"
#include "bcos-evm/evm-reference-tests/TestStateView.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <bcos-task/Wait.h>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace bcos::evm::reference_tests
{
namespace
{

state::Transaction materializeIndexedTransaction(
    StateTestCase const& testCase, StateSubtest const& subtest)
{
    auto const& tmpl = testCase.transaction;
    state::Transaction tx;
    if (tmpl.sender.has_value())
    {
        tx.from = *tmpl.sender;
    }
    if (tmpl.to.has_value() && !tmpl.to->empty())
    {
        tx.to = state::parseHexAddress(*tmpl.to);
    }
    tx.nonce = tmpl.nonce;
    tx.gasPrice = tmpl.gasPrice != 0 ? tmpl.gasPrice : tmpl.maxFeePerGas;

    auto pick = [&](auto const& values, int index) {
        if (values.empty())
        {
            throw std::runtime_error("GST transaction variant array is empty");
        }
        if (index < 0 || static_cast<size_t>(index) >= values.size())
        {
            throw std::runtime_error("GST transaction index out of range");
        }
        return values[static_cast<size_t>(index)];
    };

    tx.gasLimit = static_cast<int64_t>(pick(tmpl.gasLimit, subtest.gasIndex));
    tx.data = pick(tmpl.data, subtest.dataIndex);
    tx.value = pick(tmpl.value, subtest.valueIndex);
    return tx;
}

Eip2930AccessList materializeAccessList(
    GstTransactionTemplate const& transaction, StateSubtest const& subtest)
{
    Eip2930AccessList accessList;
    if (transaction.accessLists.empty())
    {
        return accessList;
    }
    auto const index = static_cast<size_t>(subtest.dataIndex);
    if (index >= transaction.accessLists.size())
    {
        return accessList;
    }
    for (auto const& entry : transaction.accessLists[index])
    {
        h160 account;
        std::memcpy(account.data(), entry.address.bytes, sizeof(entry.address.bytes));
        std::vector<h256> keys;
        keys.reserve(entry.storageKeys.size());
        for (auto const& slot : entry.storageKeys)
        {
            keys.emplace_back(state::fromEvmC(slot));
        }
        accessList.emplace_back(account, std::move(keys));
    }
    return accessList;
}

std::vector<SetCodeAuthorization> materializeAuthorizations(
    GstTransactionTemplate const& transaction)
{
    std::vector<SetCodeAuthorization> authorizations;
    authorizations.reserve(transaction.authorizationList.size());
    for (auto const& entry : transaction.authorizationList)
    {
        SetCodeAuthorization authorization;
        authorization.chainId = entry.chainId;
        authorization.address = entry.address;
        authorization.authority = entry.authority;
        authorization.nonce = entry.nonce;
        authorizations.push_back(std::move(authorization));
    }
    return authorizations;
}

uint8_t resolveWeb3TypedTxKind(GstTransactionTemplate const& transaction)
{
    if (transaction.authorizationListKeyPresent || !transaction.authorizationList.empty())
    {
        return 0x04;
    }
    if (transaction.maxFeePerGas != 0 || transaction.maxPriorityFeePerGas != 0)
    {
        return 0x02;
    }
    return 0;
}

bcos::u256 effectiveGasPriceForSettlement(bcos::u256 gasPrice, bcos::u256 gasTipCap,
    bcos::u256 gasFeeCap, bcos::u256 baseFee, bool eip1559Tx) noexcept
{
    if (!eip1559Tx)
    {
        return gasPrice;
    }
    auto const tipPlusBase = baseFee + gasTipCap;
    return gasFeeCap < tipPlusBase ? gasFeeCap : tipPlusBase;
}

void applyGstTransactionSettlement(state::StateDiff& stateDiff,
    std::vector<std::pair<evmc_address, state::Account>> const& preState, evmc_message const& msg,
    evmc_address const& coinbase, bcos::u256 effectiveGasPrice, bcos::u256 baseFee, int64_t gasUsed)
{
    auto resolveAccount = [&](evmc_address const& address) -> state::Account& {
        if (auto it = stateDiff.accounts.find(address); it != stateDiff.accounts.end())
        {
            return it->second;
        }
        for (auto const& [preAddress, preAccount] : preState)
        {
            if (state::AddressEqual{}(preAddress, address))
            {
                auto [inserted, _] = stateDiff.accounts.emplace(address, preAccount);
                return inserted->second;
            }
        }
        auto [inserted, _] = stateDiff.accounts.emplace(address, state::Account{});
        return inserted->second;
    };

    auto& sender = resolveAccount(msg.sender);
    uint64_t preNonce = 0;
    for (auto const& [preAddress, preAccount] : preState)
    {
        if (state::AddressEqual{}(preAddress, msg.sender))
        {
            preNonce = preAccount.nonce;
            break;
        }
    }
    if (sender.nonce <= preNonce)
    {
        sender.nonce = preNonce + 1;
    }

    bcos::u256 const gasCost = effectiveGasPrice * static_cast<bcos::u256>(gasUsed);
    if (gasCost != 0)
    {
        sender.balance = sender.balance > gasCost ? sender.balance - gasCost : 0;
    }

    bcos::u256 const tipPerGas =
        effectiveGasPrice > baseFee ? effectiveGasPrice - baseFee : bcos::u256{0};
    bcos::u256 const coinbaseCredit = tipPerGas * static_cast<bcos::u256>(gasUsed);
    if (coinbaseCredit != 0)
    {
        auto& miner = resolveAccount(coinbase);
        miner.balance += coinbaseCredit;
    }
}

}  // namespace

ExecuteViaEthAdapter::ExecuteViaEthAdapter(
    ForkProfile profile, bcos::crypto::Hash& hashImpl, evmc::VM& vm) noexcept
  : m_profile(std::move(profile)), m_hashImpl(&hashImpl), m_vm(&vm)
{}

bool ExecuteViaEthAdapter::supports(
    ForkProfile const& profile, std::string_view capabilityRowId) const
{
    static_cast<void>(capabilityRowId);
    for (auto const& pathProfile : profile.pathProfiles)
    {
        if (pathProfile.path == ExecutionPath::Reference &&
            pathProfile.evidenceKind == EvidenceKind::ReferenceParity)
        {
            return true;
        }
    }
    return false;
}

task::Task<ExecutionResult> ExecuteViaEthAdapter::execute(
    StateTestCase const& testCase, StateSubtest const& subtest)
{
    TestStateView view;
    for (auto const& [address, account] : testCase.preState)
    {
        view.insertAccount(address, account);
    }

    auto const tx = materializeIndexedTransaction(testCase, subtest);
    auto const accessList = materializeAccessList(testCase.transaction, subtest);
    auto const authorizations = materializeAuthorizations(testCase.transaction);

    ExecuteViaEthInput input;
    input.stateView = &view;
    input.vm = m_vm;
    input.hashImpl = m_hashImpl;
    input.blockInfo = testCase.env;
    input.blockHashes = [](int64_t) { return evmc_bytes32{}; };
    input.revisionConfig = m_profile.revision;
    input.gasPrice = tx.gasPrice;
    auto const& tmpl = testCase.transaction;
    if (tmpl.maxFeePerGas != 0 || tmpl.maxPriorityFeePerGas != 0)
    {
        input.gasTipCap = tmpl.maxPriorityFeePerGas;
        input.gasFeeCap = tmpl.maxFeePerGas;
    }
    else
    {
        input.gasTipCap = tx.gasPrice;
        input.gasFeeCap = tx.gasPrice;
    }
    input.web3TypedTxKind = resolveWeb3TypedTxKind(testCase.transaction);
    input.accessList = accessList.empty() ? nullptr : &accessList;
    input.authorizationListPresent = testCase.transaction.authorizationListKeyPresent;
    input.authorizations = authorizations;

    evmc_message msg{};
    msg.kind = tx.to.has_value() ? EVMC_CALL : EVMC_CREATE;
    msg.depth = 0;
    msg.gas = tx.gasLimit;
    msg.sender = tx.from;
    msg.recipient = tx.to.value_or(evmc_address{});
    msg.code_address = msg.recipient;
    msg.input_data = tx.data.data();
    msg.input_size = tx.data.size();
    msg.value = state::toEvmC(tx.value);
    input.message = msg;

    int64_t const gasBefore = msg.gas;
    auto output = co_await executeViaEth(std::move(input));

    ExecutionResult result;
    result.status = output.evmcResult.status_code;
    result.gasUsed = gasBefore - output.evmcResult.gas_left;
    if (output.evmcResult.output_data != nullptr && output.evmcResult.output_size > 0)
    {
        result.output.assign(output.evmcResult.output_data,
            output.evmcResult.output_data + output.evmcResult.output_size);
    }
    result.stateDiff = std::move(output.stateDiff);
    result.logs = std::move(output.logs);

    int64_t finalGasUsed = result.gasUsed;
    if (output.topLevelIncludedTxVmError && m_profile.revision.eip7623)
    {
        auto const& snap = output.executionContext.gasSettlementSnapshot;
        finalGasUsed =
            gas::settleIncludedTopLevelTransactionGas(gasBefore, output.evmcResult.gas_left,
                snap.evmGasRefund, m_profile.revision.calldata_floor_per_token, snap.calldata);
    }
    else if (result.status == EVMC_SUCCESS && m_profile.revision.eip7623)
    {
        auto const& snap = output.executionContext.gasSettlementSnapshot;
        gas::TxGasSettlementContext ctx;
        ctx.gasLimit = gasBefore;
        ctx.gasBeforeEvm = snap.gasBeforeEvm;
        ctx.calldata = snap.calldata;
        ctx.fixedIntrinsic = snap.fixedIntrinsic;
        ctx.authIntrinsic = snap.authIntrinsic;
        ctx.createTerm = snap.createTerm;
        ctx.evmGasLeft = output.evmcResult.gas_left;
        ctx.evmGasRefund = snap.evmGasRefund;
        finalGasUsed =
            gas::finalizeEthereumGasUsed(ctx, m_profile.revision.calldata_floor_per_token);
    }
    else if (result.status == EVMC_SUCCESS)
    {
        finalGasUsed = gas::TX_BASE_GAS + result.gasUsed;
    }
    result.gasUsed = finalGasUsed;

    if (result.status != EVMC_SUCCESS)
    {
        result.rejectionReason = std::to_string(static_cast<int>(result.status));
    }

    if (result.status == EVMC_SUCCESS || !result.stateDiff.accounts.empty())
    {
        auto const eip1559Tx = tmpl.maxFeePerGas != 0 || tmpl.maxPriorityFeePerGas != 0;
        auto const effectiveGasPrice = effectiveGasPriceForSettlement(
            tx.gasPrice, input.gasTipCap, input.gasFeeCap, testCase.env.baseFee, eip1559Tx);
        applyGstTransactionSettlement(result.stateDiff, testCase.preState, msg,
            testCase.env.coinbase, effectiveGasPrice, testCase.env.baseFee, finalGasUsed);
    }

    auto const applyDiff = result.status == EVMC_SUCCESS || !result.stateDiff.accounts.empty();
    auto const postState = buildPostStateView(
        testCase.preState, result.stateDiff, applyDiff, testCase.env.coinbase, true);
    result.stateRoot = computeStateRoot(postState);
    result.logsHash = computeLogsHash(result.logs);

    if (std::getenv("EEST_PROBE") != nullptr)
    {
        std::cerr << "=== EEST_PROBE ===\n"
                  << "status=" << static_cast<int>(result.status) << " gasUsed=" << result.gasUsed
                  << " gasPrice=" << tx.gasPrice.str(0, std::ios::hex) << " gasCost="
                  << (tx.gasPrice * static_cast<bcos::u256>(result.gasUsed)).str(0, std::ios::hex)
                  << " evmGasRefund=" << output.executionContext.gasSettlementSnapshot.evmGasRefund
                  << " authIntrinsic="
                  << output.executionContext.gasSettlementSnapshot.authIntrinsic << "\nstateRoot=0x"
                  << bcos::toHex(bcos::bytes(result.stateRoot->bytes,
                         result.stateRoot->bytes + sizeof(result.stateRoot->bytes)))
                  << "\n";
        for (auto const& [address, account] : postState.accounts)
        {
            if (state::AddressEqual{}(address, tx.from))
            {
                std::cerr << "sender nonce=" << account.nonce << " balance=0x"
                          << account.balance.str(0, std::ios::hex) << " code=0x"
                          << bcos::toHex(account.code) << "\n";
                for (auto const& [slot, value] : account.storage)
                {
                    std::cerr << "  storage[0x"
                              << bcos::toHex(bcos::bytes(slot.bytes, slot.bytes + 32)) << "]=0x"
                              << bcos::toHex(bcos::bytes(value.bytes, value.bytes + 32)) << "\n";
                }
            }
            if (state::AddressEqual{}(address, testCase.env.coinbase))
            {
                std::cerr << "coinbase nonce=" << account.nonce << " balance=0x"
                          << account.balance.str(0, std::ios::hex) << "\n";
            }
        }
        std::cerr << "stateDiff accounts=" << result.stateDiff.accounts.size() << "\n";
    }

    co_return result;
}

}  // namespace bcos::evm::reference_tests
