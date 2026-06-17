#pragma once
#include "bcos-executor/src/Common.h"
#include "bcos-executor/src/vm/VMInstance.h"  // toRevision
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "transaction-executor/bcos-transaction-executor/vm/RevisionConfig.h"
#include <evmc/evmc.h>

namespace bcos::chain_policy
{

class FiscoPolicy
{
public:
    explicit FiscoPolicy(
        const ledger::Features& features, bool balanceTransfer, bool authCheckEnabled)
      : m_features(features),
        m_balanceTransfer(balanceTransfer),
        m_authCheckEnabled(authCheckEnabled)
    {}

    RevisionConfig computeRevisionConfig(const protocol::BlockHeader& header) const
    {
        using Flag = ledger::Features::Flag;
        RevisionConfig cfg;

        cfg.revision =
            std::max(bcos::executor::toRevision(m_features, header.version()), EVMC_CANCUN);

        cfg.eip2929 = cfg.revision >= EVMC_BERLIN && m_features.get(Flag::feature_evm_eip2929);
        cfg.eip1153 = cfg.revision >= EVMC_CANCUN;
        cfg.eip4844 = cfg.revision >= EVMC_CANCUN;
        cfg.eip5656 = cfg.revision >= EVMC_CANCUN;
        cfg.eip6780 = cfg.revision >= EVMC_CANCUN;
        cfg.eip1559 = cfg.revision >= EVMC_LONDON;
        cfg.eip2537 = cfg.revision >= EVMC_PRAGUE && m_features.get(Flag::feature_evm_prague);
        cfg.eip7623 = cfg.revision >= EVMC_PRAGUE && m_features.get(Flag::feature_evm_prague);
        cfg.eip7212 = cfg.revision >= EVMC_OSAKA && m_features.get(Flag::feature_evm_osaka);
        cfg.eip7823 = cfg.revision >= EVMC_OSAKA && m_features.get(Flag::feature_evm_osaka);
        cfg.calldata_floor_per_token = cfg.eip7623 ? 10 : 0;

        cfg.fix_storage_status = m_features.get(Flag::bugfix_evm_storage_status);
        cfg.fix_error_handling = m_features.get(Flag::bugfix_v1_error_handling);
        cfg.fix_delegatecall_transfer = m_features.get(Flag::bugfix_delegatecall_transfer);
        cfg.fix_auth_check = m_features.get(Flag::bugfix_auth_check);
        cfg.fix_nonce_init = m_features.get(Flag::bugfix_nonce_initialize);
        cfg.fix_revert_logs = m_features.get(Flag::bugfix_revert_logs);
        cfg.fix_gas_precheck = m_features.get(Flag::bugfix_gas_payment_balance_precheck);
        cfg.fix_precompiled_feature_gate = m_features.get(Flag::bugfix_precompiled_feature_gate);

        cfg.use_raw_address = m_features.get(Flag::feature_raw_address);
        cfg.use_web3_timestamp = m_features.get(Flag::bugfix_v1_timestamp) &&
                                 m_features.get(Flag::feature_evm_timestamp);
        cfg.enable_balance_transfer = m_balanceTransfer;
        cfg.enable_auth_check = m_authCheckEnabled;

        return cfg;
    }

    static evmc_message deriveMessage(bool web3Tx, const evmc_message& msg,
        protocol::BlockNumber blockNum, int64_t ctxId, int64_t seq, const u256& nonce,
        const crypto::Hash& hashImpl)
    {
        return deriveMessageImpl(web3Tx, msg, blockNum, ctxId, seq, nonce, hashImpl);
    }

    int64_t convertTimestamp(int64_t blockTimestampMs) const
    {
        return m_features.get(ledger::Features::Flag::bugfix_v1_timestamp) &&
                       m_features.get(ledger::Features::Flag::feature_evm_timestamp) ?
                   blockTimestampMs / 1000 :
                   blockTimestampMs;
    }

    bool selfdestruct(const evmc_address&, const evmc_address&) const
    {
        return false;  // FISCO: no beneficiary, no refund
    }

    bool allowDelegateCallToPrecompile() const { return false; }

    template <class Storage>
    task::Task<std::optional<struct bcos::executor_v1::EVMCResult>> checkAuth(Storage& storage,
        const protocol::BlockHeader& blockHeader, const evmc_message& msg,
        const evmc_address& origin, auto&& externalCaller,
        const struct bcos::executor_v1::PrecompiledManager& precompiledMgr, int64_t contextID,
        int64_t& seq, const crypto::Hash& hashImpl) const
    {
        co_return std::nullopt;  // Stub — full implementation wired in Task 2-preamble
    }

private:
    static evmc_message deriveMessageImpl(bool web3Tx, const evmc_message& msg,
        protocol::BlockNumber blockNum, int64_t ctxId, int64_t seq, const u256& nonce,
        const crypto::Hash& hashImpl)
    {
        // TODO: Move getMessage() body from HostContext.h here in Task 2-preamble
        return msg;
    }

    const ledger::Features& m_features;
    bool m_balanceTransfer;
    bool m_authCheckEnabled;
};

}  // namespace bcos::chain_policy
