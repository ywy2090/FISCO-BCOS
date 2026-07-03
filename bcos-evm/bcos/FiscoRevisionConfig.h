#pragma once

#include "bcos-evm/eth/RevisionConfig.h"

namespace bcos::chain_policy
{

struct FiscoRevisionConfig
{
    bcos::evm::RevisionConfig ethConfig{};

    // FISCO-specific bugfix toggles.
    bool fix_storage_status : 1 = false;
    bool fix_error_handling : 1 = false;
    bool fix_delegatecall_transfer : 1 = false;
    bool fix_auth_check : 1 = false;
    bool fix_nonce_init : 1 = false;
    bool fix_revert_logs : 1 = false;
    bool fix_gas_precheck : 1 = false;
    bool fix_precompiled_feature_gate : 1 = false;

    // FISCO chain identity / behavior overlays.
    bool use_raw_address : 1 = false;
    bool use_web3_timestamp : 1 = false;
    bool enable_balance_transfer : 1 = false;
    bool enable_auth_check : 1 = false;
    bool feature_evm_address : 1 = false;

    bcos::evm::RevisionConfig& eth() noexcept { return ethConfig; }
    const bcos::evm::RevisionConfig& eth() const noexcept { return ethConfig; }
};

}  // namespace bcos::chain_policy
