#pragma once
#include <evmc/evmc.h>
#include <cstddef>
#include <cstdint>

namespace bcos::evm
{

// Single source of truth for the EIP flag set: RevisionConfig's bool fields are
// generated from this list, so the struct and the X-macro cannot drift.
#define REVISION_CONFIG_BOOL_FIELDS(X) \
    X(eip2929)                         \
    X(eip2537)                         \
    X(eip7212)                         \
    X(eip7623)                         \
    X(eip7823)                         \
    X(eip7825)                         \
    X(eip1153)                         \
    X(eip4844)                         \
    X(eip5656)                         \
    X(eip6780)                         \
    X(eip1559)                         \
    X(eip3651)                         \
    X(eip7702)                         \
    X(eip3860)

struct RevisionConfig
{
    evmc_revision revision = EVMC_CANCUN;

#define REVISION_CONFIG_DECLARE_FLAG(name) bool name : 1 = false;
    REVISION_CONFIG_BOOL_FIELDS(REVISION_CONFIG_DECLARE_FLAG)
#undef REVISION_CONFIG_DECLARE_FLAG

    // Fork-dependent parameters
    uint8_t calldata_floor_per_token = 10;
};

// A-class feature-gated fields (FISCO requires an explicit flag ON for each).
#define REVISION_CONFIG_GATED_FIELDS(X) \
    X(eip2929)                          \
    X(eip2537)                          \
    X(eip7623)                          \
    X(eip7702)                          \
    X(eip7212)                          \
    X(eip7823)                          \
    X(eip7825)

#define REVISION_CONFIG_PLUS_ONE(name) +1
inline constexpr std::size_t revisionConfigBoolFieldCount() noexcept
{
    return 0 REVISION_CONFIG_BOOL_FIELDS(REVISION_CONFIG_PLUS_ONE);
}

inline constexpr std::size_t revisionConfigGatedFieldCount() noexcept
{
    return 0 REVISION_CONFIG_GATED_FIELDS(REVISION_CONFIG_PLUS_ONE);
}
#undef REVISION_CONFIG_PLUS_ONE

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
    cfg.eip3860 = revision >= EVMC_SHANGHAI;
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
