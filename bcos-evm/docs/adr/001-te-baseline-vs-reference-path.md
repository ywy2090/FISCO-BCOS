# ADR-001: TE Baseline Path vs `executeViaEth` Reference Path

**Status:** Accepted  
**Date:** 2026-06-20  
**Deciders:** bcos-evm architecture (inheritance contract)  
**Related:** `docs/superpowers/specs/2026-06-19-eth-kernel-capability-inheritance-design.md`, `bcos-evm/capability-matrix.md`, ADR-002

---

## Context

`bcos-evm` exposes three sibling transaction orchestrators that all call the shared kernel `executeMessage()`:

| Orchestrator | Module | Typical caller |
| --- | --- | --- |
| `executeViaEth` | `bcos-evm/eth` | `EthTransactionExecutorImpl` (TE) |
| `executeViaHost` | `bcos-evm/bcos` | `TransactionExecutorImpl` (TE) |
| `opStackExecuteViaHost` | `bcos-evm/opstack` | `OpStackTransactionExecutorImpl` (TE) |

The inheritance design goal states that BCOS and OPStack should automatically receive kernel-level EIP behavior when revision and tx inputs are wired correctly. Review found repeated ambiguity:

1. Documentation used **baseline path** and **ETH (baseline)** interchangeably, even though `executeViaEth` is not BCOS or OPStack production traffic.
2. Acceptance criteria required tx-input wiring on all three orchestrators, which read as if `executeViaEth` were a production inheritance path for every chain.
3. Tests on the ETH reference path alone were sometimes treated as proof that BCOS/OPStack inherited an EIP.
4. Legacy `bcos-executor` / DAG / `HostContext` remains in the repository but was occasionally conflated with TE baseline scope.

Without a frozen path taxonomy, the capability matrix and Phase 2 wiring work cannot be reviewed consistently.

---

## Decision

### 1. Inheritance contract scope

**Only TE baseline paths are in scope for “BCOS inherits” / “OPStack inherits”:**

| Chain | TE executor | Orchestrator | Kernel entry |
| --- | --- | --- | --- |
| BCOS | `TransactionExecutorImpl` | `executeViaHost` | `executeMessage()` |
| OPStack | `OpStackTransactionExecutorImpl` | `opStackExecuteViaHost` | `executeMessage()` |

**Out of scope** unless a future ADR explicitly expands scope:

- Legacy `bcos-executor` / DAG / `HostContext`
- Any path that does not reach `executeMessage()` through the TE executors above

### 2. Reference path role

`executeViaEth` (via `EthTransactionExecutorImpl` or direct unit tests) is the **ETH reference path**:

- Used to audit **kernel-input contract** wiring (`ExecuteMessageInput` fields).
- Used for ETH-vector fixtures and direct orchestrator tests.
- **Does not** satisfy BCOS or OPStack baseline-reachable inheritance proof.
- Changes to `executeViaEth` do **not** automatically change BCOS or OPStack production behavior.

### 3. Capability matrix columns

`bcos-evm/capability-matrix.md` uses exactly these column names:

| Column | Meaning |
| --- | --- |
| **ETH (reference)** | `executeViaEth` / `EthTransactionExecutorImpl` — wiring audit only |
| **BCOS (TE baseline)** | `TransactionExecutorImpl` → `executeViaHost` |
| **OPStack (TE baseline)** | `OpStackTransactionExecutorImpl` → `opStackExecuteViaHost` |

Reviewers must not rename ETH (reference) to “ETH baseline” in matrix or test names without reopening this ADR.

### 4. Kernel inheritance boundary

Automatic kernel reuse is defined at **`executeMessage()` and below** (see design doc §3.1). Orchestrators are siblings above that boundary; they are not inheritance boundaries themselves.

### 5. Prepare-phase warm on BCOS (non-normative)

`TransactionExecutorImpl` may call `prepareTransaction` on a **local** `State` during Prepare. That warm set does not persist into Execute. For inheritance audits and tests, only the **Execute-phase** path through `executeViaHost` → `executeMessage()` is normative.

### 6. Documented TE dependency outside kernel

`transaction-executor` input builders currently depend on `bcos-executor` (e.g. `Web3AccessListResolver`). This affects tx-input propagation on baseline paths but is not part of the kernel boundary. Phase 2 may migrate decoders; until then, matrix rows for tx input must account for this coupling.

---

## Consequences

### Positive

- Matrix columns, test naming, and review checklists align on what “inherited” proves for each chain.
- ETH reference tests can continue without being mistaken for BCOS/OP production coverage.
- Phase 2 BCOS wiring targets are unambiguous (`ExecuteViaHostInput`, `TransactionExecutorImpl`).

### Negative / trade-offs

- Teams must maintain **separate** baseline-path tests for BCOS and OPStack even when kernel logic is shared.
- ETH reference column remains useful but adds a third column to every matrix row.
- `EthTransactionExecutorImpl` production role (if any) must be decided separately; this ADR only defines its **reference** role today.

### Required follow-ups

| Item | Owner phase |
| --- | --- |
| Baseline-path CTest naming convention (`*ExecuteViaHost*`, `*OpStackExecuteViaHost*`) | Phase 2 (see design doc Open Decision #4) |
| Migrate Web3 decoders off `bcos-executor` or document permanent coupling | Phase 2 |
| Legacy executor path: remain out of scope or new ADR | Deferred |

---

## Compliance checklist (for PR reviewers)

- [ ] Matrix updates use **ETH (reference)** vs **TE baseline** columns correctly.
- [ ] BCOS/OP “inherited” claims cite a **TE baseline** test, not only `executeViaEth` or kernel-direct tests.
- [ ] No requirement that BCOS/OPStack reuse `executeViaEth` as their transaction pipeline.
- [ ] Legacy `bcos-executor` behavior is not implied by inheritance matrix rows.
