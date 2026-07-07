#include "bcos-evm/eth-eest-test/EthMessageAdapter.h"

#include "bcos-evm/eth-eest-test/GstStateHash.h"
#include "bcos-evm/eth-eest-test/TestStateView.h"
#include "bcos-evm/eth/apply/ApplyEthMessage.h"
#include "bcos-evm/eth/eip/Eip2718TypedTx.h"
#include "bcos-evm/eth/eip/Eip2930AccessList.h"
#include "bcos-evm/eth/gas/TopLevelGasSettlement.h"
#include "bcos-evm/eth/state/HashUtils.hpp"
#include "bcos-evm/eth/state/WarmAccessProbe.h"
#include "bcos-evm/eth/trace/EvmOpcodeProbe.h"
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
        authorization.yParity = entry.yParity;
        authorization.signatureR = entry.signatureR;
        authorization.signatureS = entry.signatureS;
        authorizations.push_back(std::move(authorization));
    }
    return authorizations;
}

}  // namespace

EthMessageAdapter::EthMessageAdapter(
    ForkProfile profile, bcos::crypto::Hash& hashImpl, evmc::VM& vm) noexcept
  : m_profile(std::move(profile)), m_hashImpl(&hashImpl), m_vm(&vm)
{}

bool EthMessageAdapter::supports(ForkProfile const& profile, std::string_view capabilityRowId) const
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

task::Task<ExecutionResult> EthMessageAdapter::execute(
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

    EthMessageRequest input;
    input.stateView = &view;
    input.vm = m_vm;
    input.hashImpl = m_hashImpl;
    input.blockInfo = testCase.env;
    if (m_blockHashes)
        input.blockHashes = m_blockHashes;
    else
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
    input.blobGasFeeCap = tmpl.maxFeePerBlobGas;
    input.blobVersionedHashes = tmpl.blobVersionedHashes;
    input.web3TypedTxKind =
        inferWeb3TypedTxKindFromFields(testCase.transaction.authorizationListKeyPresent,
            !testCase.transaction.authorizationList.empty(),
            !testCase.transaction.blobVersionedHashes.empty(),
            testCase.transaction.maxFeePerBlobGasKeyPresent,
            tmpl.maxFeePerGas != 0 || tmpl.maxPriorityFeePerGas != 0,
            !testCase.transaction.accessLists.empty());
    input.hasExplicitFeeCaps = tmpl.maxFeePerGas != 0 || tmpl.maxPriorityFeePerGas != 0;
    input.accessList =
        accessList.empty() ? nullptr : std::make_shared<Eip2930AccessList const>(accessList);
    input.authorizationListPresent = testCase.transaction.authorizationListKeyPresent;
    input.authorizations = authorizations;
    input.txNonce = tx.nonce;
    input.txValue = tx.value;

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

    auto output = co_await applyEthMessage(std::move(input));

    ExecutionResult result;
    result.status = output.evmcResult.status_code;
    result.receiptStatus = output.evmcResult.status;
    result.topLevelIncludedTxVmError = output.topLevelIncludedTxVmError;
    result.exitKind = output.exitKind;
    result.authorizationListPresent =
        input.authorizationListPresent || !input.authorizations.empty();
    result.gasUsed = output.gasUsed;
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

    auto const applyDiff = result.status == EVMC_SUCCESS || !result.stateDiff.empty();
    auto const eip158 = m_profile.revision.revision >= EVMC_SPURIOUS_DRAGON;
    auto const postState = buildPostStateView(
        testCase.preState, result.stateDiff, applyDiff, testCase.env.coinbase, eip158);
    result.stateRoot = computeStateRoot(postState);
    result.logsHash = computeLogsHash(result.logs);

    if (std::getenv("EEST_PROBE") != nullptr)
    {
        auto const& snap = output.gasSettlementSnapshot;
        int64_t const floorDataGas =
            gas::calcFloorDataGas(m_profile.revision.calldata_floor_per_token, snap.calldata);
        std::cerr << "=== EEST_PROBE ===\n"
                  << "status=" << static_cast<int>(result.status) << " gasUsed=" << result.gasUsed
                  << " gasPrice=" << tx.gasPrice.str(0, std::ios::hex) << " gasCost="
                  << (tx.gasPrice * static_cast<bcos::u256>(result.gasUsed)).str(0, std::ios::hex)
                  << " hostRefund=" << snap.evmGasRefund
                  << " evmoneRefund=" << output.evmcResult.gas_refund
                  << " evmGasLeft=" << output.evmcResult.gas_left
                  << " floorDataGas=" << floorDataGas << "\nstateRoot=0x"
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
        for (auto const& [address, account] : postState.accounts)
        {
            std::cerr << "account 0x"
                      << bcos::toHex(
                             bcos::bytes(address.bytes, address.bytes + sizeof(address.bytes)))
                      << " nonce=" << account.nonce << " balance=0x"
                      << account.balance.str(0, std::ios::hex) << " code=0x"
                      << bcos::toHex(account.code) << "\n";
            for (auto const& [slot, value] : account.storage)
            {
                std::cerr << "  storage[0x" << bcos::toHex(bcos::bytes(slot.bytes, slot.bytes + 32))
                          << "]=0x" << bcos::toHex(bcos::bytes(value.bytes, value.bytes + 32))
                          << "\n";
            }
        }
    }

    if (state::WarmAccessProbe::enabled())
    {
        state::WarmAccessProbe::instance().print(std::cerr);
    }

    if (trace::EvmOpcodeProbe::enabled())
    {
        trace::EvmOpcodeProbe::print(std::cerr);
    }

    co_return result;
}

}  // namespace bcos::evm::reference_tests
