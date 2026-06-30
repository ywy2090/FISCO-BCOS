# ADR-018: Revision Gating Single Source of Truth

**Status:** Accepted  
**Date:** 2026-06-23  
**Related:** ADR-004, ADR-006, `bcos-evm/eth/RevisionConfig.h`, `docs/superpowers/specs/2026-06-23-revision-single-source-design.md`

---

## Context

EIP-to-revision derivation was duplicated across `EthChainPolicy`, `FiscoPolicy`, `ForkProfileRegistry`, and `makeIsthmusRevisionConfig`, and re-derived at read sites (`PrecompileActive.h`, `EthHost::selfdestruct`). Drift had already produced a sparse Isthmus profile inconsistent with canonical Prague gates.

---

## Decision

1. **`revisionConfigFromRevision(evmc_revision)`** in `eth/RevisionConfig.h` is the single canonical function for "given a revision, which EIP flags are on" (canonical maximal config).

2. **FISCO feature-gating** is a mask on top via `applyFiscoFeatureGates` + `FISCO_GATED_FLAG_MAP` X-macro in `bcos/FiscoPolicy.h`. A compile-time `static_assert` verifies the map covers every `REVISION_CONFIG_GATED_FIELDS` entry.

3. **Consumers read `RevisionConfig` bools**, not `revision >= EVMC_xxx`, for gated EIPs:
   - `PrecompileActive.h`: `cfg.eip2537`, `cfg.eip7212`
   - `EthHost::selfdestruct`: `cfg.eip6780`

4. **CI grep gate** (`tools/ci/check-revision-single-source.sh`) forbids deriving A-class fields from `revision >=` outside `RevisionConfig.h`. Consumer-side correctness is guarded by equivalence tests, not a structural grep rule for bare `revision >=`.

5. **Boundary:** blockNum/features → revision translation (`evmcRevisionFromBlockNumber`, `toFiscoRevision`) stays in policies and is out of scope for `derive`.

---

## Consequences

- EthChainPolicy / FiscoPolicy / Isthmus / ForkProfileRegistry snapshots densify (`eip1559`/`eip3651`, full Prague set for Isthmus); changes are profile-only / runtime-inert per ADR-004.
- New A-class fields require updating `REVISION_CONFIG_GATED_FIELDS`, `FISCO_GATED_FLAG_MAP`, and the CI grep field list together.

---

## Compliance checklist

- [ ] New EIP gate rule added only in `revisionConfigFromRevision`.
- [ ] FISCO A-class mask updated if a new gated field is added.
- [ ] Consumer reads `cfg` bool, not inline `revision >=`.
- [ ] `RevisionConfigProfileTest` snapshots updated.
- [ ] `capability-matrix.md` updated when profile semantics change.
