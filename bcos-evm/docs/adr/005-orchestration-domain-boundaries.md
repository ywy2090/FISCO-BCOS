# ADR-005: Orchestration Domain Boundaries

**Status:** Accepted  
**Date:** 2026-06-20  
**Related:** ADR-001, ADR-003, ADR-019, `bcos-evm/capability-matrix.md`

---

## Context

Chain-specific behavior spans nonce management, auth checks, value transfer, blob gas, receipts, deposits, and L1 fees. Without boundaries, logic leaks into `bcos-evm/eth` or matrix rows omit whole domains.

---

## Decision

### 1. Domain → layer mapping

| Domain | Primary layer | BCOS location | OPStack location | Kernel (`eth`) |
| --- | --- | --- | --- | --- |
| **Nonce** (tx + CREATE) | orchestration + VmHostPolicy | `TransactionExecutorImpl`, `FiscoVmHostPolicy::bumpContractCreateNonce` | `OpStackPrecheckPolicy::checkEntryRules`, deposit rules | no tx nonce in kernel |
| **Auth check** | orchestration | `FiscoExecute` + `AuthCheck` before `executeMessage` | N/A (OP auth model differs) | never |
| **Value transfer** | orchestration + VmHostPolicy | `FiscoExecute::maybeTransferValue`, `skipHostValueTransfer` | deposit mint + fee routing | `Transfer.h` helpers only |
| **Blob gas (EIP-4844)** | revision profile + orchestration | `feature-gated` until Web3 blob tx on BCOS | `OpStackPrecheckPolicy::checkEntryRules` + `eip4844` | no blob tx in kernel |
| **Receipt metadata** | orchestration | FISCO receipt fields via executor | `OpStackReceiptMeta` | logs in `ExecuteMessageOutput` only |
| **Deposit / L1 fee** | orchestration | unsupported | `OpStackExecute`, fee modules | never |
| **Gas settlement / refund** | orchestration | `FiscoExecute` + TE settlement | `postExecuteGasSettlement`, floor gas | shared helpers in `eth/gas/` |

### 2. Rules

1. **Never** add BCOS/OPStack includes to `bcos-evm/eth` for these domains.  
2. Each domain gets **at least one matrix row** when active on a chain (see capability matrix orchestration section).  
3. **`deviation`** requires a positive test on that chain (ADR-002).  
4. Shared math/helpers live in `eth/gas/` or neutral headers; **policy** stays in orchestrators.

### 3. VmHostPolicy vs orchestrator

| Concern | VmHostPolicy hook | Orchestrator |
| --- | --- | --- |
| Skip value transfer inside CALL | `skipHostValueTransfer` | pre-tx value move |
| CREATE nonce bump side effect | `bumpContractCreateNonce` | tx nonce validation |
| Chain precompile | `ChainCallTargetDispatcher`（ADR-024） | address routing policy |
| Auth table / caller rewrite | `prepareMessage`, `setCallerAddress` | `authChecker` callback |

Orchestrator runs **before** `executeMessage`; VmHostPolicy runs **inside** kernel call tree.

### 4. Shared orchestration pipeline (`eth/pipeline/`, ADR-019)

Since ADR-019, portable orchestration steps (validate, intrinsic debit, `ExecuteMessageInput` build, `adoptEvmcResult`, EIP-7623 settlement snapshot) live in `eth/pipeline/` as sync `runTxPipeline`. Three execution-bridge wrappers (`ethReferenceExecute`, `fiscoExecute`, `opStackExecute`) supply chain hooks only.

**Still wrapper-out (not in fixed pipeline steps):**

| Domain | Examples | Why outside kernel |
| --- | --- | --- |
| Async fee | OpStack `buyGas` / `refundGas` | coroutine; ADR-019 Q7 |
| Fee routing | L1 fee, operator fee, gas pool | chain policy |
| State machine | deposit mint, `checkpoint`/`commit`/`revert`, `GasPoolReturnGuard` | RAII + async boundaries |
| Final output | OpStack `ctx.state.build_diff()` after fee/refund | wrapper-side balance deltas |

Rule unchanged: **`eth/pipeline/` must not `#include` `bcos/` or `opstack/`**. OpStack floor/balance checks enter via `preDebitEntry` hook calling `opstack/` code from the wrapper translation unit, not from portable headers.

---

## Consequences

- Phase 1 matrix adds rows: BCOS auth, BCOS value transfer, nonce, OPStack receipt, blob gas (already partially present).
- ADR-019 consolidates shared orchestration into `eth/pipeline/`; domain policy remains in wrappers/hooks per §4.
- EIP onboarding checklist must tag each domain per §4 class.

---

## Compliance checklist

- [ ] New chain behavior classified into a domain above.
- [ ] Matrix row added with correct layer.
- [ ] No new BCOS/OP includes under `bcos-evm/eth`.
