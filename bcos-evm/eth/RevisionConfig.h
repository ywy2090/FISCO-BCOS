#pragma once
#include <evmc/evmc.h>
#include <cstddef>
#include <cstdint>

namespace bcos::evm_standard
{

struct RevisionConfig
{
    evmc_revision revision = EVMC_CANCUN;

    // A. Feature-gated EIPs (chain policy may mask; see revisionConfigFromRevision)
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

    // C. Fork-dependent parameters
    uint8_t calldata_floor_per_token = 10;
};

// Enumerate bool EIP/profile flags for profile tests and drift detection (design §5.6).
#define REVISION_CONFIG_BOOL_FIELDS(X) \
    X(warm_access)                     \
    X(eip2537)                         \
    X(eip7212)                         \
    X(eip7623)                         \
    X(eip7823)                         \
    X(eip1153)                         \
    X(eip4844)                         \
    X(eip5656)                         \
    X(eip6780)                         \
    X(eip1559)                         \
    X(eip3651)                         \
    X(eip7702)

inline constexpr std::size_t revisionConfigBoolFieldCount() noexcept
{
    std::size_t n = 0;
#define REVISION_CONFIG_COUNT_FIELD(name) ++n;
    REVISION_CONFIG_BOOL_FIELDS(REVISION_CONFIG_COUNT_FIELD)
#undef REVISION_CONFIG_COUNT_FIELD
    return n;
}

static_assert(revisionConfigBoolFieldCount() == 12,
    "Keep REVISION_CONFIG_BOOL_FIELDS in sync with RevisionConfig bool bitfields");

// A-class feature-gated fields (may be masked off per chain policy).
#define REVISION_CONFIG_GATED_FIELDS(X) \
    X(warm_access)                      \
    X(eip2537)                          \
    X(eip7212)                          \
    X(eip7623)                          \
    X(eip7823)                          \
    X(eip7702)

inline constexpr std::size_t revisionConfigGatedFieldCount() noexcept
{
    std::size_t n = 0;
#define REVISION_CONFIG_GATED_COUNT(name) ++n;
    REVISION_CONFIG_GATED_FIELDS(REVISION_CONFIG_GATED_COUNT)
#undef REVISION_CONFIG_GATED_COUNT
    return n;
}

static_assert(revisionConfigGatedFieldCount() == 6,
    "Keep REVISION_CONFIG_GATED_FIELDS in sync with the A-class field set");

// Single source of truth: EIP gating for a given revision. Canonical (maximal) config.
// Chain policy builders translate blockNum/features -> revision, then optionally mask A-class
// fields. Never read `revision >= EVMC_xxx` for a gated EIP outside here.
inline RevisionConfig revisionConfigFromRevision(evmc_revision revision)
{
    RevisionConfig cfg;
    cfg.revision = revision;
    cfg.warm_access = revision >= EVMC_BERLIN;
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
    cfg.calldata_floor_per_token = cfg.eip7623 ? 10 : 0;
    return cfg;
}

}  // namespace bcos::evm_standard
