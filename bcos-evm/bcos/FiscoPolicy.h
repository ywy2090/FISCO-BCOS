#pragma once
#include "FiscoConstants.h"
#include "FiscoRevisionConfig.h"
#include "bcos-crypto/ChecksumAddress.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-utilities/DataConvertUtility.h"
#include "transaction-executor/bcos-transaction-executor/adapters/AuthCheck.h"
#include "transaction-executor/bcos-transaction-executor/adapters/PrecompiledManager.h"
#include <evmc/evmc.h>
#include <evmc/helpers.h>
#include <fmt/compile.h>
#include <fmt/format.h>
#include <cstring>

namespace bcos::chain_policy
{
namespace
{
inline evmc_revision toFiscoRevision(const ledger::Features& features, uint32_t blockVersion)
{
    using Flag = ledger::Features::Flag;
    if (features.get(Flag::feature_evm_osaka))
    {
        return EVMC_OSAKA;
    }
    if (features.get(Flag::feature_evm_prague))
    {
        return EVMC_PRAGUE;
    }
    if (blockVersion >= static_cast<uint32_t>(protocol::BlockVersion::V3_2_VERSION))
    {
        return features.get(Flag::feature_evm_cancun) ? EVMC_CANCUN : EVMC_PARIS;
    }
    return EVMC_LONDON;
}
}  // namespace

// FISCO field -> feature flag map. X-macro keeps mask code and the completeness
// assert in one place. Flag identity stays in the FISCO layer (never in eth/).
#define FISCO_GATED_FLAG_MAP(X)         \
    X(warm_access, feature_evm_eip2929) \
    X(eip2537, feature_evm_prague)      \
    X(eip7623, feature_evm_prague)      \
    X(eip7702, feature_evm_prague)      \
    X(eip7212, feature_evm_osaka)       \
    X(eip7823, feature_evm_osaka)

inline constexpr std::size_t fiscoGatedFlagMapCount() noexcept
{
    std::size_t n = 0;
#define FISCO_GATE_COUNT(field, flag) ++n;
    FISCO_GATED_FLAG_MAP(FISCO_GATE_COUNT)
#undef FISCO_GATE_COUNT
    return n;
}

// Completeness: every A-class kernel field has exactly one FISCO flag mapping.
static_assert(fiscoGatedFlagMapCount() == bcos::evm_standard::revisionConfigGatedFieldCount(),
    "FISCO_GATED_FLAG_MAP must cover every REVISION_CONFIG_GATED_FIELDS entry");

inline void applyFiscoFeatureGates(
    bcos::evm_standard::RevisionConfig& cfg, const ledger::Features& features)
{
    using Flag = ledger::Features::Flag;
#define FISCO_APPLY_GATE(field, flag) cfg.field = cfg.field && features.get(Flag::flag);
    FISCO_GATED_FLAG_MAP(FISCO_APPLY_GATE)
#undef FISCO_APPLY_GATE
}

class FiscoPolicy
{
public:
    explicit FiscoPolicy(
        const ledger::Features& features, bool balanceTransfer, bool authCheckEnabled)
      : m_features(features),
        m_balanceTransfer(balanceTransfer),
        m_authCheckEnabled(authCheckEnabled)
    {}

    FiscoRevisionConfig computeRevisionConfig(const protocol::BlockHeader& header) const
    {
        using Flag = ledger::Features::Flag;
        FiscoRevisionConfig cfg;
        auto& ethCfg = cfg.eth();

        ethCfg.revision = std::max(toFiscoRevision(m_features, header.version()), EVMC_CANCUN);

        ethCfg.warm_access =
            ethCfg.revision >= EVMC_BERLIN && m_features.get(Flag::feature_evm_eip2929);
        ethCfg.eip1153 = ethCfg.revision >= EVMC_CANCUN;
        ethCfg.eip4844 = ethCfg.revision >= EVMC_CANCUN;
        ethCfg.eip5656 = ethCfg.revision >= EVMC_CANCUN;
        ethCfg.eip6780 = ethCfg.revision >= EVMC_CANCUN;
        ethCfg.eip1559 = ethCfg.revision >= EVMC_LONDON;
        ethCfg.eip2537 = ethCfg.revision >= EVMC_PRAGUE && m_features.get(Flag::feature_evm_prague);
        ethCfg.eip7623 = ethCfg.revision >= EVMC_PRAGUE && m_features.get(Flag::feature_evm_prague);
        ethCfg.eip7702 = ethCfg.revision >= EVMC_PRAGUE && m_features.get(Flag::feature_evm_prague);
        ethCfg.eip7212 = ethCfg.revision >= EVMC_OSAKA && m_features.get(Flag::feature_evm_osaka);
        ethCfg.eip7823 = ethCfg.revision >= EVMC_OSAKA && m_features.get(Flag::feature_evm_osaka);
        ethCfg.calldata_floor_per_token = ethCfg.eip7623 ? 10 : 0;

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

    const ledger::Features& features() const { return m_features; }

    template <class Storage>
    task::Task<std::optional<class bcos::evm::EVMCResult>> checkAuth(Storage& storage,
        const protocol::BlockHeader& blockHeader, const evmc_message& msg,
        const evmc_address& origin, const bcos::evm::PrecompiledManager& precompiledMgr,
        int64_t contextID, int64_t& seq, const crypto::Hash& hashImpl) const
    {
        if (!m_authCheckEnabled)
        {
            co_return std::nullopt;
        }
        auto result = bcos::evm::checkAuth(storage, blockHeader, msg, origin, precompiledMgr,
            contextID, seq, hashImpl, m_features.get(ledger::Features::Flag::bugfix_auth_check));
        co_return result;
    }

private:
    static evmc_message deriveMessageImpl(bool web3Tx, const evmc_message& msg,
        protocol::BlockNumber blockNum, int64_t ctxId, int64_t seq, const u256& nonce,
        const crypto::Hash& hashImpl)
    {
        evmc_message message = msg;
        switch (message.kind)
        {
        case EVMC_CREATE:
        {
            if (std::memcmp(message.code_address.bytes, bcos::evm::EMPTY_EVM_ADDRESS.bytes,
                    sizeof(bcos::evm::EMPTY_EVM_ADDRESS.bytes)) == 0)
            {
                if (!web3Tx)
                {
                    auto address = fmt::format(FMT_COMPILE("{}_{}_{}"), blockNum, ctxId, seq);
                    auto hash = hashImpl.hash(address);
                    std::copy_n(hash.data(), sizeof(message.code_address.bytes),
                        message.code_address.bytes);
                }
                else
                {
                    auto legacyAddr =
                        newLegacyEVMAddress(bytesConstRef(message.sender.bytes), nonce);
                    std::copy(legacyAddr.begin(), legacyAddr.end(), message.code_address.bytes);
                }
            }
            message.recipient = message.code_address;
            break;
        }
        case EVMC_CREATE2:
        {
            std::array<bcos::byte, 1 + sizeof(message.sender.bytes) + sizeof(message.create2_salt) +
                                       crypto::HashType::SIZE>
                buffer;
            uint8_t* ptr = buffer.data();
            *ptr++ = 0xff;
            ptr =
                std::uninitialized_copy_n(message.sender.bytes, sizeof(message.sender.bytes), ptr);
            auto salt = toBigEndian(fromEvmC(message.create2_salt));
            ptr = std::uninitialized_copy(salt.begin(), salt.end(), ptr);
            auto inputHash = hashImpl.hash(bytesConstRef(message.input_data, message.input_size));
            ptr = std::uninitialized_copy(inputHash.begin(), inputHash.end(), ptr);
            auto addressHash = hashImpl.hash(bytesConstRef(buffer.data(), buffer.size()));

            std::copy_n(addressHash.begin() + 12, sizeof(message.code_address.bytes),
                message.code_address.bytes);
            message.recipient = message.code_address;
            break;
        }
        default:
            break;
        }
        return message;
    }

    const ledger::Features& m_features;
    bool m_balanceTransfer;
    bool m_authCheckEnabled;
};

}  // namespace bcos::chain_policy
