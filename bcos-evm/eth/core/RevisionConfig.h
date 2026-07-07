#pragma once
#include <evmc/evmc.h>
#include <cstddef>
#include <cstdint>

namespace bcos::evm
{

struct RevisionConfig
{
    evmc_revision revision = EVMC_CANCUN;

    bool eip2929 : 1 = false;
    bool eip2537 : 1 = false;
    bool eip7212 : 1 = false;
    bool eip7623 : 1 = false;
    bool eip7823 : 1 = false;
    bool eip7825 : 1 = false;
    bool eip1153 : 1 = false;
    bool eip4844 : 1 = false;
    bool eip5656 : 1 = false;
    bool eip6780 : 1 = false;
    bool eip1559 : 1 = false;
    bool eip3651 : 1 = false;
    bool eip7702 : 1 = false;

    // Fork-dependent parameters
    uint8_t calldata_floor_per_token = 10;
};

// Single source of truth: EIP gating for a given revision. Canonical (maximal) config.
// Chain policy builders translate blockNum/features -> revision, then optionally mask A-class
// fields. Never read `revision >= EVMC_xxx` for a gated EIP outside here.
inline RevisionConfig revisionConfigFromRevision(evmc_revision revision)
{
    RevisionConfig cfg;
    cfg.revision = revision;
    cfg.eip2929 = revision >= EVMC_BERLIN;
    cfg.eip1559 = revision >= EVMC_LONDON;
    cfg.eip3651 = revision >= EVMC_SHANGHAI;
    cfg.eip1153 = revision >= EVMC_CANCUN;
    cfg.eip4844 = revision >= EVMC_CANCUN;
    cfg.eip5656 = revision >= EVMC_CANCUN;
    cfg.eip6780 = revision >= EVMC_CANCUN;
    cfg.eip2537 = revision >= EVMC_PRAGUE;
    cfg.eip7623 = revision >= EVMC_PRAGUE;
    cfg.eip7702 = revision >= EVMC_PRAGUE;
    cfg.eip7212 = revision >= EVMC_OSAKA;
    cfg.eip7823 = revision >= EVMC_OSAKA;
    cfg.eip7825 = revision >= EVMC_OSAKA;
    cfg.calldata_floor_per_token = cfg.eip7623 ? 10 : 0;
    return cfg;
}

}  // namespace bcos::evm
