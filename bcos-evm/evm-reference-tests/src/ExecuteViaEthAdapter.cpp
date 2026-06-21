#include "bcos-evm/evm-reference-tests/ExecuteViaEthAdapter.h"

#include "bcos-evm/eth/ExecuteViaEth.h"
#include "bcos-evm/eth/state/hash_utils.hpp"
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
    if (result.status != EVMC_SUCCESS)
    {
        result.rejectionReason = std::to_string(static_cast<int>(result.status));
    }

    co_return result;
}

}  // namespace bcos::evm::reference_tests
