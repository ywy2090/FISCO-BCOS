# ADR-005: Orchestration Domain Boundaries

**Status:** Accepted  
**Date:** 2026-06-20  
**Related:** ADR-001, ADR-003, `bcos-evm/capability-matrix.md`

---

## Context

Chain-specific behavior spans nonce management, auth checks, value transfer, blob gas, receipts, deposits, and L1 fees. Without boundaries, logic leaks into `bcos-evm/eth` or matrix rows omit whole domains.

---

## Decision

### 1. Domain → layer mapping

| Domain | Primary layer | BCOS location | OPStack location | Kernel (`eth`) |
| --- | --- | --- | --- | --- |
| **Nonce** (tx + CREATE) | orchestration + host extension | `TransactionExecutorImpl`, `FiscoHostExtension::bumpContractCreateNonce` | `OpStackPreCheck`, deposit rules | no tx nonce in kernel |
| **Auth check** | orchestration | `ExecuteViaHost` + `AuthCheck` before `executeMessage` | N/A (OP auth model differs) | never |
| **Value transfer** | orchestration + host extension | `ExecuteViaHost::maybeTransferValue`, `skipHostValueTransfer` | deposit mint + fee routing | `Transfer.h` helpers only |
| **Blob gas (EIP-4844)** | revision profile + orchestration | `feature-gated` until Web3 blob tx on BCOS | `OpStackPreCheck` + `eip4844` | no blob tx in kernel |
| **Receipt metadata** | orchestration | FISCO receipt fields via executor | `OpStackReceiptMeta` | logs in `ExecuteMessageOutput` only |
| **Deposit / L1 fee** | orchestration | unsupported | `OpStackExecuteViaHost`, fee modules | never |
| **Gas settlement / refund** | orchestration | `ExecuteViaHost` + TE settlement | `postExecuteGasSettlement`, floor gas | shared helpers in `eth/gas/` |

### 2. Rules

1. **Never** add BCOS/OPStack includes to `bcos-evm/eth` for these domains.  
2. Each domain gets **at least one matrix row** when active on a chain (see capability matrix orchestration section).  
3. **`deviation`** requires a positive test on that chain (ADR-002).  
4. Shared math/helpers live in `eth/gas/` or neutral headers; **policy** stays in orchestrators.

### 3. HostExtension vs orchestrator

| Concern | HostExtension hook | Orchestrator |
| --- | --- | --- |
| Skip value transfer inside CALL | `skipHostValueTransfer` | pre-tx value move |
| CREATE nonce bump side effect | `bumpContractCreateNonce` | tx nonce validation |
| Chain precompile | `tryChainPrecompile` | address routing policy |
| Auth table / caller rewrite | `prepareMessage`, `setCallerAddress` | `authChecker` callback |

Orchestrator runs **before** `executeMessage`; HostExtension runs **inside** kernel call tree.

---

## Consequences

- Phase 1 matrix adds rows: BCOS auth, BCOS value transfer, nonce, OPStack receipt, blob gas (already partially present).
- EIP onboarding checklist must tag each domain per §4 class.

---

## Compliance checklist

- [ ] New chain behavior classified into a domain above.
- [ ] Matrix row added with correct layer.
- [ ] No new BCOS/OP includes under `bcos-evm/eth`.
