#include "bcos-evm/eth-eest-test/OpStackManifestAdapter.h"

#include "bcos-evm/eth-eest-test/GstStateHash.h"
#include "bcos-evm/eth-eest-test/TestStateView.h"
#include "bcos-evm/eth/Web3TypedTxKind.h"
#include "bcos-evm/eth/eip/Eip2930AccessList.h"
#include "bcos-evm/eth/eip/Eip4844.h"
#include "bcos-evm/eth/eip/Eip7623.h"
#include "bcos-evm/eth/eip/Eip7702.h"
#include "bcos-evm/eth/gas/TxIntrinsicGas.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/opstack/apply/ApplyOpStackMessage.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <bcos-task/Wait.h>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

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
        authorization.yParity = entry.yParity;
        authorization.signatureR = entry.signatureR;
        authorization.signatureS = entry.signatureS;
        authorizations.push_back(std::move(authorization));
    }
    return authorizations;
}

}  // namespace

OpStackManifestAdapter::OpStackManifestAdapter(
    ForkProfile profile, bcos::crypto::Hash& hashImpl, evmc::VM& vm) noexcept
  : m_profile(std::move(profile)), m_hashImpl(&hashImpl), m_vm(&vm)
{}

bool OpStackManifestAdapter::supports(
    ForkProfile const& profile, std::string_view capabilityRowId) const
{
    static_cast<void>(capabilityRowId);
    for (auto const& pathProfile : profile.pathProfiles)
    {
        if (pathProfile.path == ExecutionPath::OpStackBaseline)
        {
            return true;
        }
    }
    return false;
}

task::Task<ExecutionResult> OpStackManifestAdapter::execute(
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

    OpStackMessageRequest input;
    input.stateView = &view;
    input.vm = m_vm;
    input.hashImpl = m_hashImpl;
    input.blockInfo = testCase.env;
    input.blockHashes = [](int64_t) { return evmc_bytes32{}; };
    input.revisionConfig = m_profile.revision;

    auto const& tmpl = testCase.transaction;
    input.nonce = tx.nonce;
    input.gasTipCap = tmpl.maxPriorityFeePerGas != 0 ? tmpl.maxPriorityFeePerGas : tx.gasPrice;
    input.gasFeeCap = tmpl.maxFeePerGas != 0 ? tmpl.maxFeePerGas : tx.gasPrice;
    input.web3TypedTxKind =
        inferWeb3TypedTxKindFromFields(testCase.transaction.authorizationListKeyPresent,
            !testCase.transaction.authorizationList.empty(),
            !testCase.transaction.blobVersionedHashes.empty(),
            testCase.transaction.maxFeePerBlobGasKeyPresent,
            tmpl.maxFeePerGas != 0 || tmpl.maxPriorityFeePerGas != 0,
            !testCase.transaction.accessLists.empty());
    input.accessList = accessList.empty() ? nullptr : &accessList;
    input.authorizationListPresent = testCase.transaction.authorizationListKeyPresent;
    input.authorizations = authorizations;
    input.skipNonceChecks = false;
    input.skipTransactionChecks = false;
    input.noBaseFee = false;
    input.call = true;

    if (m_profile.revision.eip7623)
    {
        auto const eip7623Components = gas::calcEip7623Components(bcos::ref(tx.data));
        input.floorDataGas =
            gas::calcFloorDataGas(m_profile.revision.calldata_floor_per_token, eip7623Components);
    }

    if (!testCase.transaction.blobVersionedHashes.empty())
    {
        input.blobVersionedHashes = testCase.transaction.blobVersionedHashes;
    }

    // No-op gas pool hooks for manifest-driven testing (no block gas limit).
    input.gasPoolSubGasHook = [](uint64_t) { return true; };
    input.gasPoolReturnGasHook = [](uint64_t, uint64_t) {};

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
    auto output = co_await applyOpStackMessage(std::move(input));

    ExecutionResult result;
    result.status = output.evmcResult.status_code;
    result.authorizationListPresent =
        input.authorizationListPresent || !input.authorizations.empty();
    result.gasUsed = gasBefore - output.evmcResult.gas_left;
    if (output.evmcResult.output_data != nullptr && output.evmcResult.output_size > 0)
    {
        result.output.assign(output.evmcResult.output_data,
            output.evmcResult.output_data + output.evmcResult.output_size);
    }
    result.stateDiff = std::move(output.stateDiff);
    result.logs = std::move(output.logs);

    // OPStack fee settlement is intentionally NOT applied here.
    // baseFee → OP_BASE_FEE_RECIPIENT (not burned) and operator fee (not EIP-1559 tip)
    // are documented divergences in opstack-skip-list.json (G1, G5).
    // StateRoot assertions for OPStack manifests should use transitional-only assertLevels.

    auto const applyDiff = result.status == EVMC_SUCCESS || !result.stateDiff.accounts.empty();
    auto const postState = buildPostStateView(
        testCase.preState, result.stateDiff, applyDiff, testCase.env.coinbase, false);
    result.stateRoot = computeStateRoot(postState);
    result.logsHash = computeLogsHash(result.logs);

    co_return result;
}

}  // namespace bcos::evm::reference_tests
