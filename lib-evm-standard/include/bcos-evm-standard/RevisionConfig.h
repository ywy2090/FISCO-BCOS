#pragma once
#include <evmc/evmc.h>
#include <cstdint>

namespace bcos::evm_standard
{

struct RevisionConfig
{
    evmc_revision revision = EVMC_CANCUN;

    // A. Feature-gated EIPs (require explicit flag ON in FISCO)
    bool eip2929 : 1 = false;
    bool eip2537 : 1 = false;
    bool eip7212 : 1 = false;
    bool eip7623 : 1 = false;
    bool eip7823 : 1 = false;

    // B. Revision-only EIPs (derived from evmc_revision)
    bool eip1153 : 1 = false;
    bool eip4844 : 1 = false;
    bool eip5656 : 1 = false;
    bool eip6780 : 1 = false;
    bool eip1559 : 1 = false;
    bool eip3651 : 1 = false;
    bool eip7702 : 1 = false;

    // C. Bugfix toggles (FISCO-specific; false for standard Ethereum)
    bool fix_storage_status : 1 = false;
    bool fix_error_handling : 1 = false;
    bool fix_delegatecall_transfer : 1 = false;
    bool fix_auth_check : 1 = false;
    bool fix_nonce_init : 1 = false;
    bool fix_revert_logs : 1 = false;
    bool fix_gas_precheck : 1 = false;
    bool fix_precompiled_feature_gate : 1 = false;

    // D. Chain identity
    bool use_raw_address : 1 = false;
    bool use_web3_timestamp : 1 = false;
    bool enable_balance_transfer : 1 = false;
    bool enable_auth_check : 1 = false;

    // E. Fork-dependent parameters
    uint8_t calldata_floor_per_token = 10;
};

}  // namespace bcos::evm_standard
