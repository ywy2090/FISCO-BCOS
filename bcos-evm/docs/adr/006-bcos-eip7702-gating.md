# ADR-006: BCOS EIP-7702 Gating Model

**Status:** Accepted  
**Date:** 2026-06-20  
**Related:** Open Decision #2 (design doc §11), `bcos-evm/capability-matrix.md`

---

## Context

BCOS TE baseline lacked EIP-7702 tx fields and profile enablement. Phase 2 required a gating decision before wiring.

---

## Decision

BCOS adopts **feature-gated EIP-7702** aligned with existing Prague Web3 rollout:

1. **Transaction format:** Web3 typed tx kind `0x04` (same decoder path as OPStack/Eth via `Web3AccessListResolver` / future TE-local decoder).  
2. **Revision enable:** `FiscoPolicy::computeRevisionConfig` sets `ethCfg.eip7702 = true` when `feature_evm_prague` is on **and** `revision >= EVMC_PRAGUE`.  
3. **Tx fields:** `ExecuteViaHostInput` carries `authorizationListPresent` + `authorizations`; wired from TE Web3 builder when kind `0x04`.  
4. **Orchestration:** Phase 2 wires fields only; sender-code precheck and auth intrinsic gas follow OPStack patterns in a **later** PR unless required for safety (matrix rows stay `unsupported` until implemented).  
5. **Legacy `bcos-executor`:** remains **unsupported** for 7702 inheritance contract.  
6. **Auth check interaction:** delegated execution runs after `authChecker` passes; document in matrix as orchestration row when implemented.

Permanent opt-out remains valid via matrix `unsupported` if product decides not to ship 7702 on BCOS.

---

## Consequences

- Phase 2 implements items 2–3; matrix BCOS 7702 tx propagation is **`feature-gated`** (fields wired; baseline-reachable when `feature_evm_prague` + Web3 `0x04`).  
- Precheck/intrinsic rows stay `unsupported` until explicit orchestration lands.
