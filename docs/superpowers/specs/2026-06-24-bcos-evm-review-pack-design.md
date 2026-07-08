# bcos-evm Review Pack — Design Spec

**Date:** 2026-06-24  
**Status:** Approved (brainstorming)  
**Deliverable:** `bcos-evm/docs/review-pack.md`

---

## Goal

Provide a self-contained onboarding document for external/cross-team reviewers. Readers should understand architecture, execution flow, extension mechanisms, governance, and review checklist in 30–60 minutes without reading all 19 ADRs.

## Non-goals

- Replace `architecture-overview.md` (internal deep reference; not updated by this work)
- Duplicate full capability matrix or ADR bodies
- Source-level walkthrough of every `eth/` subdirectory (link to `eth-layer-design-review.md`)

## Audience

- Familiar with EVM / Ethereum execution layer
- May not know FISCO-BCOS or OP Stack specifics
- Needs to review PRs or audit the inheritance contract

## Freshness policy

Review Pack content is validated against **source on `feat-evm-refactor` branch as of 2026-06-24**:

| Verified artifact | Purpose |
| --- | --- |
| `bcos-evm/eth/orchestration/OrchestrationPipeline.cpp` | 12-step pipeline step order |
| `bcos-evm/eth/ExecuteViaEth.cpp`, `bcos/ExecuteViaHost.cpp`, `opstack/OpStackExecuteViaHost.cpp` | All three call `runOrchestration` |
| `bcos-evm/capability-matrix.md` | Column semantics, Isthmus dense profile (ADR-018) |
| `bcos-evm/docs/adr/018`, `019` | Revision single source + orchestration pipeline |
| `.github/workflows/capability-gate.yml` | CI enforcement list |
| `bcos-evm/eth/RevisionConfig.h` | 13 bool fields, 6 gated fields |

**Stale companion docs** (Review Pack calls these out; do not treat as authoritative):

~~`architecture-overview.md` §3 — missing `runOrchestration` layer~~ **Fixed 2026-06-24**

~~`architecture-overview.md` §7.2 — Isthmus described as sparse~~ **Fixed 2026-06-24**

~~`eth-layer-design-review.md` §3.6 — claims pipeline is Eth-only~~ **Fixed 2026-06-24**

## Document structure

1. Executive Summary (~60 lines)
2. Execution flow with ADR-019 pipeline (~80 lines)
3. Extension mechanisms: HostExtension / Port / OrchestrationHooks (~70 lines)
4. Revision gating: ADR-018 single source (~50 lines)
5. Governance: matrix + ADR index + CI (~40 lines)
6. Review Checklist (~60 lines)
7. Known issues and next leverage (~40 lines)
8. File index + related docs (~30 lines)

## Review Checklist categories

- G1–G5: universal gates (include boundary, matrix sync, pipeline invariant)
- A–E: domain-specific (kernel, orchestration, revision, port, chain-specific)
- Red flags: dual-track message, eth/orchestration chain includes, inherited without tests

## Outputs

| File | Role |
| --- | --- |
| `bcos-evm/docs/review-pack.md` | External reviewer entry point |
| This spec | Brainstorming record |

## Maintenance

When ADR-019+ orchestration, ADR-018 revision, or capability-matrix column semantics change, update Review Pack §2, §4, §5 in the same PR.
