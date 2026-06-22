#include "bcos-evm/evm-reference-tests/ExecuteViaEthAdapter.h"

#include "bcos-evm/eth/ExecuteViaEth.h"
#include "bcos-evm/eth/gas/EthTxGasSettlement.h"
#include "bcos-evm/eth/state/hash_utils.hpp"
#include "bcos-evm/evm-reference-tests/GstStateHash.h"
#include "bcos-evm/evm-reference-tests/TestStateView.h"
#include <bcos-task/Wait.h>

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

void applyGstTransactionSettlement(state::StateDiff& stateDiff,
    std::vector<std::pair<evmc_address, state::Account>> const& preState, evmc_message const& msg,
    bcos::u256 gasPrice, int64_t gasUsed)
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
    sender.nonce = preNonce + 1;

    bcos::u256 const gasCost = gasPrice * static_cast<bcos::u256>(gasUsed);
    if (gasCost != 0)
    {
        sender.balance = sender.balance > gasCost ? sender.balance - gasCost : 0;
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

    ExecuteViaEthInput input;
    input.stateView = &view;
    input.vm = m_vm;
    input.hashImpl = m_hashImpl;
    input.blockInfo = testCase.env;
    input.blockHashes = [](int64_t) { return evmc_bytes32{}; };
    input.revisionConfig = m_profile.revision;
    input.gasPrice = tx.gasPrice;

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
    if (result.status != EVMC_SUCCESS)
    {
        result.rejectionReason = std::to_string(static_cast<int>(result.status));
    }
    else
    {
        int64_t finalGasUsed = result.gasUsed;
        if (m_profile.revision.eip7623)
        {
            auto const& snap = output.executionContext.gasSettlementSnapshot;
            gas::TxGasSettlementContext ctx;
            ctx.gasLimit = gasBefore;
            ctx.gasBeforeEvm = snap.gasBeforeEvm;
            ctx.calldata = snap.calldata;
            ctx.fixedIntrinsic = snap.fixedIntrinsic;
            ctx.createTerm = snap.createTerm;
            ctx.evmGasLeft = output.evmcResult.gas_left;
            ctx.evmGasRefund = snap.evmGasRefund;
            finalGasUsed =
                gas::finalizeEthereumGasUsed(ctx, m_profile.revision.calldata_floor_per_token);
        }
        else
        {
            finalGasUsed = gas::TX_BASE_GAS + result.gasUsed;
        }
        result.gasUsed = finalGasUsed;
        applyGstTransactionSettlement(
            result.stateDiff, testCase.preState, msg, tx.gasPrice, finalGasUsed);
    }

    auto const applyDiff = result.status == EVMC_SUCCESS;
    auto const postState = buildPostStateView(
        testCase.preState, result.stateDiff, applyDiff, testCase.env.coinbase, true);
    result.stateRoot = computeStateRoot(postState);
    result.logsHash = computeLogsHash(result.logs);

    co_return result;
}

}  // namespace bcos::evm::reference_tests
