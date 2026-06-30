/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief FISCO EthHost extension with Hook#8 CREATE-frame side effects.
 * @file FiscoVmHostPolicy.h
 */

#pragma once

#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-evm/bcos/FiscoConstants.h"
#include "bcos-evm/bcos/ports/AuthPort.h"
#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/state/EvmHostHooks.h"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include <evmc/evmc.h>
#include <functional>
#include <string>

namespace bcos::evm
{
class FiscoVmHostPolicy final : public state::EvmHostHooks
{
public:
    struct RevisionFlags
    {
        bool fix_auth_check{false};
        bool use_raw_address{false};
        bool fix_storage_status{false};
        bool fix_nonce_init{false};
        bool web3Tx{false};
        int64_t createLevel{0};
    };

    using RecipientPathResolver = std::function<std::string(const evmc_message&)>;

    struct FiscoVmHostPolicyDeps
    {
        void* storageRef{nullptr};
        protocol::BlockHeader const* blockHeader{nullptr};
        ledger::LedgerConfig const* ledgerConfig{nullptr};
        int64_t blockNumber{0};
        int64_t contextID{0};
        int64_t* seq{nullptr};
        bcos::crypto::Hash const* hashImpl{nullptr};
        std::function<void(const evmc_address&, uint64_t)> persistContractCreateNonce;
        evmc_address origin{};
        RevisionFlags revisionFlags{};
        state::State* state{nullptr};
        RecipientPathResolver recipientPathResolver{};
        AuthPort const* authPort{nullptr};
    };

    explicit FiscoVmHostPolicy(bool skipEvmNativeValueTransfer, FiscoVmHostPolicyDeps deps);

    bool allowSelfdestruct(const state::Account& /*unused*/) override { return false; }
    bool allowDelegateCallToPrecompile() override { return false; }
    bool skipHostValueTransfer() override { return m_skipEvmNativeValueTransfer; }

    void prepareMessage(evmc_revision rev, evmc_message& msg) override;
    void setCallerAddress(const evmc_address& caller) override;
    void bumpContractCreateNonce(const evmc_address& contractAddress) override;

    void applySstoreRefund(state::State& state, evmc_bytes32 const& current,
        evmc_bytes32 const& original, evmc_bytes32 const& newValue) const noexcept override;

    evmc_storage_status classifyStorageStatus(evmc_bytes32 const& original,
        evmc_bytes32 const& current, evmc_bytes32 const& newValue) const noexcept override;

    void applyLegacySstoreDeletedRefund(
        state::State& state, evmc_storage_status status) const noexcept override;

    void finalizeTopLevelCreateNonce(
        state::State& state, evmc_address const& createAddr) noexcept override;

private:
    static bool isZeroAddress(const evmc_address& address) noexcept;
    static evmc_address createTarget(const evmc_message& message) noexcept;
    static std::string hexAddress(const evmc_address& address);
    void deriveNestedCreateAddress(evmc_message& message);
    void applyCreateNonceSemantics(const evmc_message& message);
    std::string resolveAuthTablePath(const evmc_message& message) const;

private:
    bool m_skipEvmNativeValueTransfer{false};
    void* m_storageRef{nullptr};
    protocol::BlockHeader const* m_blockHeader{nullptr};
    ledger::LedgerConfig const* m_ledgerConfig{nullptr};
    int64_t m_blockNumber{0};
    int64_t m_contextID{0};
    int64_t* m_seq{nullptr};
    bcos::crypto::Hash const* m_hashImpl{nullptr};
    std::function<void(const evmc_address&, uint64_t)> m_persistContractCreateNonce;
    evmc_address m_callerAddress{};
    evmc_address m_origin{};
    RevisionFlags m_revisionFlags{};
    state::State* m_state{nullptr};
    RecipientPathResolver m_recipientPathResolver;
    AuthPort const* m_authPort{nullptr};
};

}  // namespace bcos::evm
