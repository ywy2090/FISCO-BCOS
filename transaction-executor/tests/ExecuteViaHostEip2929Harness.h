/*
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  @brief fiscoExecute harness for EIP-2929 compat tests (replaces ExecuteFrame).
 */

#pragma once

#include "../../bcos-evm/test/state/InMemoryEvmStateReader.h"
#include "Eip2929TestHelpers.h"
#include "bcos-evm/bcos/FiscoExecutionBridge.h"
#include "bcos-evm/bcos/FiscoPolicy.h"
#include "bcos-evm/bcos/FiscoTransactionPrepare.h"
#include "bcos-evm/bcos/FiscoTxAdapter.h"
#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/state/EthHost.hpp"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-executor/src/Common.h"
#include "bcos-executor/src/vm/VMInstance.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <cstring>
#include <memory>
#include <optional>

namespace bcos::test
{

class Eip2929ExecuteViaHostFixture
{
public:
    using InMemoryEvmStateReader = bcos::evm::state::test::InMemoryEvmStateReader;

    std::shared_ptr<bcos::crypto::Keccak256> hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    InMemoryEvmStateReader stateView;
    bcostars::protocol::BlockHeaderImpl blockHeader{
        [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); }};
    bcos::ledger::LedgerConfig ledgerConfig;
    int64_t seq = 0;

    Eip2929ExecuteViaHostFixture()
    {
        executor::GlobalHashImpl::g_hashImpl = hashImpl;
        blockHeader.setVersion(static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION));
        blockHeader.calculateHash(*hashImpl);
    }

    bcos::chain_policy::FiscoRevisionConfig revisionFromFeatures(
        bcos::ledger::Features const& features) const
    {
        bcos::chain_policy::FiscoPolicy policy(
            features, ledgerConfig.balanceTransfer(), ledgerConfig.authCheckStatus() != 0);
        return policy.computeRevisionConfig(blockHeader);
    }

    void insertCode(evmc_address const& address, bcos::bytes const& code,
        bcos::u256 balance = bcos::u256(1) << 96)
    {
        bcos::evm::state::Account account;
        account.balance = balance;
        account.code = code;
        stateView.insert_account(address, account);
    }

    void fund(evmc_address const& address, bcos::u256 balance = bcos::u256(1) << 96)
    {
        bcos::evm::state::Account account;
        account.balance = balance;
        stateView.insert_account(address, account);
    }

    bcos::ledger::Features pragueEip2929Features() const
    {
        return warmset::makeFeaturesPragueEip2929();
    }
    bcos::ledger::Features cancunEip2929Features() const
    {
        return warmset::makeFeaturesCancunEip2929();
    }
    bcos::ledger::Features shanghaiEip2929Features() const
    {
        return warmset::makeFeaturesShanghaiEip2929();
    }
    bcos::ledger::Features osakaEip2929Features() const
    {
        return warmset::makeFeaturesOsakaEip2929();
    }

    int64_t measureBytecodeGas(bcos::chain_policy::FiscoRevisionConfig const& revisionConfig,
        evmc_address const& runner, bcos::bytes const& code, int64_t startGas = 2'000'000)
    {
        bcos::evm::state::Account runnerAccount;
        runnerAccount.balance = 1'000'000'000;
        runnerAccount.code = code;
        stateView.insert_account(runner, runnerAccount);

        evmc_message message{};
        message.kind = EVMC_CALL;
        message.gas = startGas;
        message.sender = runner;
        message.recipient = runner;
        message.code_address = runner;

        bcos::evm::state::BlockInfo blockInfo;
        blockInfo.number = blockHeader.number();
        blockInfo.gasLimit = 30'000'000;

        bcos::evm::FiscoExecutionRequest input;
        input.stateView = &stateView;
        input.vm = &vm();
        input.hashImpl = hashImpl.get();
        input.message = message;
        input.blockInfo = blockInfo;
        input.revisionConfig = revisionConfig;

        auto output = bcos::task::syncWait(bcos::evm::fiscoExecute(std::move(input)));
        if (output.evmcResult.status_code != EVMC_SUCCESS)
        {
            return -1;
        }
        return startGas - output.evmcResult.gas_left;
    }

    int64_t measureDoubleExtCodeSizeGas(bcos::ledger::Features const& features,
        evmc_address const& target, int64_t startGas = 2'000'000, uint8_t runnerTag = 0x71)
    {
        evmc_address runner{};
        runner.bytes[19] = runnerTag;
        auto const code = warmset::doubleExtCodeSizeBytecode(target);
        return measureBytecodeGas(revisionFromFeatures(features), runner, code, startGas);
    }

    int64_t measureTwoAccountsExtCodeSizeGas(bcos::ledger::Features const& features,
        evmc_address const& target1, evmc_address const& target2, int64_t startGas = 2'000'000,
        uint8_t runnerTag = 0x72)
    {
        evmc_address runner{};
        runner.bytes[19] = runnerTag;
        auto code = warmset::warmTwoAccountsExtCodeSizeBytecode(target1, target2);
        code.push_back(0x00);
        return measureBytecodeGas(revisionFromFeatures(features), runner, code, startGas);
    }

    /// Shim replacing ExecuteFrame for migrated CompatHostContext tests.
    class CompatHostShim
    {
    public:
        CompatHostShim(Eip2929ExecuteViaHostFixture& fixture,
            bcos::chain_policy::FiscoRevisionConfig revisionConfig, evmc_address originIn,
            evmc_address recipientIn, evmc_call_kind kindIn,
            std::shared_ptr<const bcos::executor::Eip2930AccessList> accessList = {},
            uint8_t web3TypedTxKind = 0, int64_t gas = 1'000'000)
          : m_fixture(fixture),
            m_revisionConfig(revisionConfig),
            m_accessList(std::move(accessList)),
            m_web3Kind(web3TypedTxKind)
        {
            m_message.kind = kindIn;
            m_message.gas = gas;
            m_message.sender = originIn;
            m_message.recipient = recipientIn;
            m_message.code_address = recipientIn;
            if (kindIn == EVMC_CREATE || kindIn == EVMC_CREATE2)
            {
                m_message.recipient = {};
            }
        }

        evmc_message const& message() const { return m_message; }
        evmc_message& mutableMessage() { return m_message; }

        bcos::task::Task<void> prepare()
        {
            m_state = std::make_unique<bcos::evm::state::State>(m_fixture.stateView);
            m_message =
                bcos::evm::deriveMessage(bcos::evm::FiscoTxAdapterInput{.message = m_message,
                    .blockNumber = m_fixture.blockHeader.number(),
                    .contextID = 0,
                    .seq = m_fixture.seq,
                    .nonce = 0,
                    .hashImpl = m_fixture.hashImpl.get()});

            bcos::evm::state::Transaction tx;
            tx.from = m_message.sender;
            if (m_message.kind != EVMC_CREATE && m_message.kind != EVMC_CREATE2)
            {
                tx.to = m_message.recipient;
            }

            bcos::evm::state::TransactionProperties props;
            props.warmDestination = m_message.kind != EVMC_CREATE && m_message.kind != EVMC_CREATE2;
            std::optional<evmc_address> createCodeAddress;
            if (m_message.kind == EVMC_CREATE || m_message.kind == EVMC_CREATE2)
            {
                createCodeAddress = m_message.code_address;
            }

            bcos::evm::Eip2930AccessList const* listPtr = nullptr;
            if (m_accessList != nullptr)
            {
                listPtr = m_accessList.get();
            }

            bcos::evm::prepareTransaction(*m_state, tx, blockInfo(),
                bcos::evm::FiscoTransactionPrepareInput{.revisionConfig = m_revisionConfig.eth(),
                    .properties = props,
                    .accessList = listPtr,
                    .web3TypedTxKind = m_web3Kind,
                    .createCodeAddress = createCodeAddress});
            co_return;
        }

        bcos::task::Task<bcos::evm::EVMCResult> execute() { co_return callThroughHost(m_message); }

        bcos::task::Task<bcos::evm::EVMCResult> externalCall(evmc_message nested)
        {
            nested = bcos::evm::deriveMessage(bcos::evm::FiscoTxAdapterInput{.message = nested,
                .blockNumber = m_fixture.blockHeader.number(),
                .contextID = 0,
                .seq = m_fixture.seq + 1,
                .nonce = 0,
                .hashImpl = m_fixture.hashImpl.get()});
            co_return callThroughHost(nested);
        }

        evmc_access_status accessAccount(evmc_address const& addr)
        {
            return host().access_account(addr);
        }

        evmc_access_status accessStorage(evmc_address const& addr, evmc_bytes32 const& key)
        {
            return host().access_storage(addr, key);
        }

        bcos::ledger::LedgerConfig const& ledgerConfig() const { return m_fixture.ledgerConfig; }

        uint32_t blockVersion() const { return m_fixture.blockHeader.version(); }

    private:
        bcos::evm::state::BlockInfo blockInfo() const
        {
            bcos::evm::state::BlockInfo info;
            info.number = m_fixture.blockHeader.number();
            info.gasLimit = 30'000'000;
            return info;
        }

        bcos::evm::state::EthHost& host()
        {
            if (!m_host)
            {
                if (!m_state)
                {
                    m_state = std::make_unique<bcos::evm::state::State>(m_fixture.stateView);
                }
                evmc_tx_context ctx{};
                ctx.tx_origin = m_message.sender;
                bool const fixStorageStatus = m_revisionConfig.fix_storage_status;
                bool const warmAccess = m_revisionConfig.eth().warm_access;
                static_cast<void>(warmAccess);
                m_host.emplace(*m_state, ctx, m_revisionConfig.eth(), m_fixture.evm(),
                    bcos::evm::state::BlockHashes{}, nullptr, fixStorageStatus);
            }
            return *m_host;
        }

        bcos::evm::EVMCResult callThroughHost(evmc_message const& msg)
        {
            if (!m_state)
            {
                m_state = std::make_unique<bcos::evm::state::State>(m_fixture.stateView);
            }
            auto raw = host().call(msg);
            return bcos::evm::EVMCResult(
                raw.release_raw(), bcos::protocol::TransactionStatus::None);
        }

        evmc_tx_context buildTxContext(evmc_message const& msg) const
        {
            evmc_tx_context ctx{};
            ctx.tx_origin = msg.sender;
            return ctx;
        }

        Eip2929ExecuteViaHostFixture& m_fixture;
        bcos::chain_policy::FiscoRevisionConfig m_revisionConfig;
        std::shared_ptr<const bcos::executor::Eip2930AccessList> m_accessList;
        uint8_t m_web3Kind{0};
        evmc_message m_message{};
        std::unique_ptr<bcos::evm::state::State> m_state;
        std::optional<bcos::evm::state::EthHost> m_host;
    };

    CompatHostShim makeHost(bcos::ledger::Features const& features,
        uint32_t blockHeaderVersion = static_cast<uint32_t>(
            bcos::protocol::BlockVersion::MAX_VERSION),
        evmc_address originIn = {}, evmc_address recipientIn = {},
        evmc_call_kind kindIn = EVMC_CALL,
        std::shared_ptr<const bcos::executor::Eip2930AccessList> eip2930AccessList = {},
        uint8_t web3TypedTxKindForAccessList = 0, int64_t gas = 1'000'000,
        std::shared_ptr<bcos::executor::Eip2929AccessState> /*warmsetAccess*/ = nullptr)
    {
        ledgerConfig.setFeatures(features);
        blockHeader.setVersion(blockHeaderVersion);
        blockHeader.calculateHash(*hashImpl);
        auto rev = revisionFromFeatures(features);
        return CompatHostShim(*this, rev, originIn, recipientIn, kindIn,
            std::move(eip2930AccessList), web3TypedTxKindForAccessList, gas);
    }

    evmc::VM& evm() { return vm(); }

private:
    evmc::VM& vm()
    {
        static evmc::VM instance{evmc_create_evmone()};
        return instance;
    }
};

}  // namespace bcos::test
