#pragma once
#include <evmc/evmc.h>
#include <cstdint>

namespace bcos::evm_standard
{

struct RevisionConfig
{
    evmc_revision revision = EVMC_CANCUN;

    // A. Feature-gated EIPs (require explicit flag ON in FISCO)
    bool warm_access : 1 = false;
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
    bool prague_post_execution : 1 = false;

    // C. Fork-dependent parameters
    uint8_t calldata_floor_per_token = 10;
};

inline RevisionConfig makeIsthmusRevisionConfig()
{
    RevisionConfig config;
    config.revision = EVMC_PRAGUE;
    config.eip7623 = true;
    config.eip7702 = true;
    config.prague_post_execution = false;
    config.calldata_floor_per_token = 10;
    return config;
}

}  // namespace bcos::evm_standard
