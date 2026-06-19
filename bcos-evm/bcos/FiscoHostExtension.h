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
 * @file FiscoHostExtension.h
 */

#pragma once

#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/policy/HostExtension.h"
#include "bcos-evm/eth/state/State.hpp"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include <evmc/evmc.h>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace bcos::evm
{
class PrecompiledManager;

class FiscoHostExtension final : public state::HostExtension
{
public:
    struct RevisionFlags
    {
        bool fix_auth_check{false};
        bool use_raw_address{false};
        bool fix_nonce_init{false};
        bool web3Tx{false};
        int64_t createLevel{0};
    };

    using ExternalCaller = std::function<EVMCResult(const evmc_message&)>;
    using FiscoPrecompileCaller =
        std::function<std::optional<evmc_result>(evmc_revision, const evmc_message&)>;
    using RecipientPathResolver = std::function<std::string(const evmc_message&)>;
    using CreateAuthTableInvoker = std::function<void(const evmc_message&, std::string_view)>;

    struct FiscoHostExtensionDeps
    {
        void* storageRef{nullptr};
        protocol::BlockHeader const* blockHeader{nullptr};
        ledger::LedgerConfig const* ledgerConfig{nullptr};
        PrecompiledManager const* precompiledManager{nullptr};
        int64_t blockNumber{0};
        int64_t contextID{0};
        int64_t* seq{nullptr};
        ExternalCaller externalCaller{};
        evmc_address origin{};
        RevisionFlags revisionFlags{};
        state::State* state{nullptr};
        RecipientPathResolver recipientPathResolver{};
        CreateAuthTableInvoker createAuthTableInvoker{};
    };

    explicit FiscoHostExtension(
        bool skipEvmNativeValueTransfer, FiscoPrecompileCaller precompileCaller = {});
    FiscoHostExtension(bool skipEvmNativeValueTransfer, FiscoHostExtensionDeps deps,
        FiscoPrecompileCaller precompileCaller = {});

    bool allowSelfdestruct(const state::Account& /*unused*/) override { return false; }
    bool allowDelegateCallToPrecompile() override { return false; }
    bool skipHostValueTransfer() override { return m_skipEvmNativeValueTransfer; }

    std::optional<evmc_result> tryChainPrecompile(
        evmc_revision rev, const evmc_message& msg) override;
    void prepareMessage(evmc_revision rev, const evmc_message& msg) override;

private:
    static bool isFiscoPrecompileAddress(const evmc_address& address) noexcept;
    static bool isZeroAddress(const evmc_address& address) noexcept;
    static evmc_address createTarget(const evmc_message& message) noexcept;
    static std::string hexAddress(const evmc_address& address);
    void applyCreateNonceSemantics(const evmc_message& message);
    std::string resolveAuthTablePath(const evmc_message& message) const;

private:
    bool m_skipEvmNativeValueTransfer{false};
    FiscoPrecompileCaller m_precompileCaller;
    void* m_storageRef{nullptr};
    protocol::BlockHeader const* m_blockHeader{nullptr};
    ledger::LedgerConfig const* m_ledgerConfig{nullptr};
    PrecompiledManager const* m_precompiledManager{nullptr};
    int64_t m_blockNumber{0};
    int64_t m_contextID{0};
    int64_t* m_seq{nullptr};
    evmc_address m_origin{};
    RevisionFlags m_revisionFlags{};
    state::State* m_state{nullptr};
    ExternalCaller m_externalCaller;
    RecipientPathResolver m_recipientPathResolver;
    CreateAuthTableInvoker m_createAuthTableInvoker;
};

}  // namespace bcos::evm
