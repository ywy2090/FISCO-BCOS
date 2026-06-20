# ETH Kernel Capability Inheritance Design

**Status:** Accepted — architecture contract frozen; Phase 1 doc/ADR complete; Phase 2–3 partial implementation on `feat-evm-refactor` (see `bcos-evm/docs/inheritance-work-tracker.md`)  
**Date:** 2026-06-19  
**Scope:** `bcos-evm/eth`, `bcos-evm/bcos`, `bcos-evm/opstack`, `transaction-executor/bcos-transaction-executor` baseline path  
**Non-goals:** This document does not require BCOS or OPStack to reuse `executeViaEth()` as their transaction pipeline. It also does not guarantee inheritance for the legacy `bcos-executor` / DAG / `HostContext` path.  
**Related specs:** `docs/superpowers/plans/2026-06-18-bcos-evm-layer-refactor.md` (layer refactor), `docs/superpowers/plans/2026-06-18-opstack-isthmus.md` (OPStack Isthmus), `docs/superpowers/reviews/eth-opstack-geth-parity-report-2026-06-19.md` (parity review). This document governs **inheritance status semantics** and matrix ownership; implementation details remain in sibling plans. Where parity review and the capability matrix disagree (e.g. tx-entry warm auditing method), **`bcos-evm/capability-matrix.md` wins** after code re-audit.

**Operational docs:** `bcos-evm/docs/inheritance-work-tracker.md`, `bcos-evm/docs/inheritance-pr-review-checklist.md`, `bcos-evm/docs/architecture-known-gaps.md`

---

## 1. Goal

The target architecture is:

> Any EIP that is implemented in the shared ETH execution kernel, and whose required revision, transaction fields, and orchestration prerequisites are correctly satisfied on the TE baseline path, is **baseline-reachable** on BCOS and OPStack without duplicating kernel logic. Any EIP that changes chain-level transaction orchestration must be explicitly integrated by each extension layer.

**Important:** Kernel rows marked `inherited` mean **kernel-capable** only. End-to-end “automatic inheritance” requires every mandatory matrix row for that EIP (kernel, tx input, revision profile, orchestration) to be baseline-reachable on that chain — not merely implemented under `bcos-evm/eth`.

In this document, "BCOS inherits" / "OPStack inherits" always means the **TE baseline path** inherits the behavior — i.e. `TransactionExecutorImpl` → `executeViaHost` (BCOS) or `OpStackTransactionExecutorImpl` → `opStackExecuteViaHost` (OPStack) → `executeMessage()`. The legacy `bcos-executor` / DAG / `HostContext` path may remain present in the repository, but it is outside this inheritance contract unless a later design explicitly brings it into scope.

The word **inherited** has two levels that must not be conflated:

| Level | Meaning |
| --- | --- |
| **kernel-capable** | Logic exists in `executeMessage` and below; can be exercised by direct kernel tests with manually supplied `RevisionConfig` and tx inputs |
| **baseline-reachable** | On the TE baseline path, with that chain's default profile builder and normal tx decoding, the capability is **observably triggered** without chain-specific reimplementation |

A matrix **kernel** row may be `inherited` (kernel-capable). End-to-end EIP support additionally requires matching **tx input**, **revision profile**, and **orchestration** rows. A kernel row marked `inherited` does **not** imply the EIP is user-visible on that chain.

### Terminology

| Term | Definition |
| --- | --- |
| kernel | `executeMessage()` and everything it owns: `EthHost`, `State`, builtin precompiles, `execution/*`, `Eip7702.*`, and the evmone revision behavior selected by `RevisionConfig::revision` |
| orchestrator | One of the three sibling transaction entry points: `executeViaEth`, `executeViaHost`, `opStackExecuteViaHost` |
| TE baseline path | BCOS: `TransactionExecutorImpl` → `executeViaHost` → `executeMessage`. OPStack: `OpStackTransactionExecutorImpl` → `opStackExecuteViaHost` → `executeMessage`. Excludes legacy `bcos-executor` |
| reference path | `executeViaEth` (and optionally `EthTransactionExecutorImpl` in TE). ETH-vector reference; **not** BCOS/OPStack production inheritance proof |
| implicit-default tx input | A tx field not explicitly assigned by an orchestrator but still effective because struct defaults apply (e.g. `TransactionProperties::warmDestination{true}`) |
| inherited | On the relevant path, the capability is **baseline-reachable** (or, for kernel-only rows, **kernel-capable**) with no chain-specific reimplementation beyond documented hooks |
| explicit | Chain-specific orchestration (precheck, intrinsic gas, settlement, receipt) in the extension orchestrator |
| feature-gated | Blocked by unset `RevisionConfig` field and/or FISCO `Features::Flag`, or profile-only until a consumer is wired |
| unsupported | Intentionally not wired; default-off behavior is expected |
| deviation | Intentionally different from Ethereum reference behavior; requires a positive deviation test |

This deliberately separates two kinds of reuse:

| Layer | Reuse Expectation | Examples |
| --- | --- | --- |
| ETH kernel | Automatically shared when inputs are wired | opcode behavior, `EthHost`, `State`, warm access, builtin precompiles, EIP-7702 authorization application |
| Chain orchestration | Explicit per chain | gas buying/refund, nonce, auth, deposit, L1 fee, receipt metadata, FISCO precompile routing |

The design does not promise that changing `eth/ExecuteViaEth.cpp` automatically changes BCOS or OPStack. `executeViaEth`, `executeViaHost`, and `opStackExecuteViaHost` are sibling transaction orchestrators that share the lower `executeMessage` kernel.

---

## 2. Current Architecture Assessment

The current module split already points in the right direction:

```text
bcos-evm-eth
  executeMessage()
  EthHost
  State
  RevisionConfig
  builtin precompiles

bcos-evm-bcos
  executeViaHost()
  FiscoPolicy
  FiscoRevisionConfig
  FiscoHostExtension
  FISCO auth/precompile/value-transfer behavior

bcos-evm-op
  opStackExecuteViaHost()
  OpHostExtension
  OpStackPreCheck
  OpStackTxExecutor
  OP gas/L1 fee/deposit behavior
```

`bcos-evm-bcos` and `bcos-evm-op` both link `bcos-evm-eth`, and both call `executeMessage()`. This is the correct foundation for kernel-level inheritance.

The main remaining gaps (see §8 and `inheritance-work-tracker.md`): Web3 decoder coupling to `bcos-executor`; BCOS 7702 precheck/intrinsic orchestration (ADR-006 deferred); OPStack 7702 orchestrator E2E; full `RevisionConfig` fork snapshots beyond MVP.

### 2.1 TE executors, reference path, and build dependencies

Three transaction-executor entry points exist; only two are TE baseline paths for BCOS/OPStack inheritance:

| Executor | Orchestrator | Role in this contract |
| --- | --- | --- |
| `TransactionExecutorImpl` | `executeViaHost` | BCOS TE baseline |
| `OpStackTransactionExecutorImpl` | `opStackExecuteViaHost` | OPStack TE baseline |
| `EthTransactionExecutorImpl` | `executeViaEth` | ETH reference path (TE module; not BCOS/OP production) |

CMake layering (`bcos-evm/CMakeLists.txt`): `bcos-evm-bcos` and `bcos-evm-op` both `PUBLIC` link `bcos-evm-eth`. `transaction-executor` additionally links `executor` (`transaction-executor/CMakeLists.txt`) because TE input builders still include `bcos-executor/src/Web3AccessListResolver.h` (`EthTxInputBuilder.h`, `OpStackTxInputBuilder.h`). That coupling is outside the kernel boundary but affects baseline tx-input propagation; Phase 2 should track migration of decoders out of `bcos-executor` or document it as a documented dependency.

OPStack tx builders live under `transaction-executor/bcos-transaction-executor/OpStackTxInputBuilder.h`, not under `bcos-evm/opstack/`.

BCOS `TransactionExecutorImpl` also runs `prepareTransaction` on a **local** `State` during Prepare (`TransactionExecutorImpl.h`); that warm set does not persist into Execute. Only the Execute-phase `executeViaHost` warm path is normative for inheritance.

---

## 3. Design Principles

### 3.1 `executeMessage()` is the inheritance boundary

An EIP is considered automatically inherited only if its behavior is implemented at or below:

- `bcos-evm/eth/executeMessage.*`
- `bcos-evm/eth/state/EthHost.*`
- `bcos-evm/eth/state/State.*`
- `bcos-evm/eth/state/EthPrecompiles.*`
- `bcos-evm/eth/execution/*` (including `warmTransactionEntry.h`)
- `bcos-evm/eth/Eip7702.*`
- evmone revision behavior selected by `RevisionConfig::revision`

BCOS and OPStack should not duplicate logic that belongs in this layer. "Inherited" here always refers to the TE baseline path (see Terminology), not the legacy `bcos-executor` path.

### 3.2 Tx inputs are shared data, not chain policy

Access list, typed transaction kind, authorization list, blob hashes, and similar EIP inputs should have a single semantic meaning before they enter `executeMessage()`.

Upper layers may parse these fields differently from their own transaction formats, but once parsed they should be passed to the kernel using the same names and semantics.

### 3.3 Chain policy remains explicit

BCOS and OPStack may intentionally differ from Ethereum at the transaction orchestration layer. Those differences must be visible in the extension layer, not hidden inside ETH kernel code.

Examples:

- BCOS auth checks
- BCOS native precompiled dispatch
- BCOS value transfer behavior
- OPStack deposit transactions
- OPStack L1/operator fees
- OPStack floor gas settlement

### 3.4 Revision selection is chain-specific, but `RevisionConfig` semantics are kernel-specific

Each chain may decide how to produce a `RevisionConfig`, but once produced, each field must mean the same thing to the ETH kernel.

`FiscoRevisionConfig` should wrap `RevisionConfig` and add FISCO-only bugfix or chain behavior flags. It should not redefine ETH EIP semantics.

---

## 4. Capability Classification

Every new EIP must be classified before implementation.

| Class | Definition | Expected Work |
| --- | --- | --- |
| Kernel capability | Behavior lives entirely in `executeMessage`, `EthHost`, `State`, builtin precompiles, or evmone revision | Implement once in `eth`; ensure BCOS/OPStack pass the correct `RevisionConfig` |
| Tx input capability | Kernel can implement the behavior, but needs new tx fields | Add fields to the shared input path; wire all orchestrators that should support it |
| Tx orchestration capability | Behavior affects precheck, intrinsic gas, gas settlement, nonce, balance, or receipt | Implement explicit BCOS/OPStack integration where applicable |
| Host extension capability | Behavior depends on chain hooks such as precompile routing or value transfer | Extend `HostExtension` contract or each chain extension explicitly |
| Chain deviation | Chain intentionally differs from Ethereum or OP | Document the deviation and test it |

### Class → layer → status mapping (normative)

When adding matrix rows, use this mapping. A single EIP often needs **multiple rows** (kernel / tx input / revision profile / orchestration).

| §4 Class | Matrix `Layer` column | Typical status tokens |
| --- | --- | --- |
| Kernel capability | `kernel` | `inherited` when kernel-capable on TE path; else `unsupported` |
| Tx input capability | `tx input` | `inherited`, footnoted **implicit-default** (not a sixth token), or `unsupported` |
| Tx orchestration capability | `orchestration` | `explicit`, `feature-gated`, `unsupported`, or `deviation` |
| Host extension capability | `host extension` | `inherited` (no chain hook), `deviation`, or `explicit` |
| Chain deviation | `orchestration` or `host extension` | `deviation` (must include positive test reference) |

**Token rules:**

- Only the five status tokens above plus documented footnotes; **`n/a` is not allowed** in matrix cells.
- `feature-gated`: blocked by `RevisionConfig` default `false` and/or a FISCO `Features::Flag` until profile work or an explicit opt-out is recorded.
- `deviation`: intentionally different from Ethereum reference behavior on that path.
- `implicit-default`: orchestrator does not assign the field, but struct defaults make the behavior effective (must cite default source, e.g. `TransactionProperties`).

### Examples

A capability is rarely a single class. The table below splits each EIP into its runtime (kernel) part and its non-kernel dependencies, because "implemented in the kernel" does not by itself mean "inherited on the baseline path".

| EIP / Feature | Kernel part | Non-kernel dependencies | Current Inheritance Risk |
| --- | --- | --- | --- |
| EIP-2929 warm access | Runtime cold/warm via `EthHost::access_account/_storage` → `State::warm_up_*` | Tx-entry warm via `warmTransactionEntry`: explicit `setWarmDestinationFromKind` on ETH/BCOS/OPStack TE (`TxFeaturePrepare.h`); `warmCoinbase{true}` default when `rev >= SHANGHAI` | Low for runtime; Low for tx-entry on TE paths (OPStack uses explicit `applyDefaultTxProps`) |
| EIP-7702 authorization application | `applyAuthorizations` in `executeMessage` (gated by `eip7702 && authorizationListPresent`) | (a) tx field propagation; (b) `RevisionConfig::eip7702`; (c) sender-code precheck + auth intrinsic gas | Medium — kernel-capable everywhere; BCOS baseline-reachable when `feature_evm_prague` + Web3 `0x04` (ADR-006); OPStack Isthmus baseline-reachable; orchestration precheck still OP-only |
| EIP-7623 floor calldata gas | Shared intrinsic helpers | Settlement orchestration-specific; BCOS also debits fixed `BALANCE_TRANSFER_GAS` (21000) before EVM | High — three divergent settlement paths plus BCOS gas deviation |
| EIP-2537 precompiles (0x0b–0x11) | `EthPrecompiles` dispatch by address, gated by `evmc_revision` | BCOS `feature_evm_prague` gates FISCO manager path; kernel dispatch ignores `RevisionConfig::eip2537` flag | Medium — revision vs feature-flag dual track |
| EIP-7212 precompile (0x0100) | Implemented in legacy `EthBuiltinRegistry`, **not** in TE `EthPrecompiles` | Not on TE baseline until `EthPrecompiles` extended | High — silent gap if matrix says "builtin inherited" |
| OPStack deposit | none | Entirely OPStack orchestration | Not inherited; OPStack-only |

---

## 5. Target Architecture

### 5.1 Introduce an EIP capability matrix

Add a maintained design table that maps each EIP to:

- capability class
- owning module
- required `RevisionConfig` fields
- required tx input fields
- BCOS support status
- OPStack support status
- known deviations
- required tests

Single source of truth:

- The **`bcos-evm/capability-matrix.md`** file is the **normative** capability matrix. There must be exactly one authoritative matrix.
- This design document references that file and must not duplicate editable rows.
- Any `bcos-evm/README` or `bcos-evm/docs` checklist is a non-normative summary only.

**Related ADRs:**

| ADR | Topic | Path |
| --- | --- | --- |
| ADR-001 | TE baseline path vs `executeViaEth` reference path scope | `bcos-evm/docs/adr/001-te-baseline-vs-reference-path.md` |
| ADR-002 | Matrix status tokens — definitions and Class→Layer mapping | `bcos-evm/docs/adr/002-matrix-status-tokens.md` |
| ADR-003 | Sub-capability row granularity for multi-layer EIPs | `bcos-evm/docs/adr/003-sub-capability-row-granularity.md` |
| ADR-004 | `RevisionConfig` field consumption vs profile-only flags | `bcos-evm/docs/adr/004-revision-config-field-consumption.md` |
| ADR-005 | Orchestration domain boundaries (nonce, auth, blob gas, receipt) | `bcos-evm/docs/adr/005-orchestration-domain-boundaries.md` |
| ADR-006 | BCOS EIP-7702 gating model | `bcos-evm/docs/adr/006-bcos-eip7702-gating.md` |

This matrix is the entry gate for future EIP work.

### 5.2 Standardize the kernel input contract

`ExecuteMessageInput` should remain the canonical contract for kernel-visible EIP data.

The existing fields are already close to the desired shape:

- `revisionConfig`
- `txProps`
- `accessList`
- `authorizationListPresent`
- `authorizations`
- `web3TypedTxKind`
- `extension`
- `fixStorageStatus`
- `fixNonceInit` (FISCO overlay mapped from `ExecuteViaHostInput` revision flags)

The design requirement is that every orchestrator that claims **baseline-reachable** support for a kernel EIP must have an explicit or documented implicit-default field path into `ExecuteMessageInput`. The reference path (`executeViaEth`) is tracked in the matrix **ETH (reference)** column for wiring audits but does not satisfy BCOS/OPStack inheritance tests.

Required alignment:

| Orchestrator | Path role | Status (2026-06-20) |
| --- | --- | --- |
| `executeViaEth` | ETH reference | Passes tx fields via `EthTxInputBuilder`; used for kernel-input contract tests |
| `executeViaHost` | BCOS TE baseline | EIP-7702 fields wired (`FiscoTxInputBuilder` → `ExecuteViaHostInput`); uses `TxFeaturePrepare` for warm destination |
| `opStackExecuteViaHost` | OPStack TE baseline | Authorization/access-list wired; explicit `applyDefaultTxProps` for `txProps` |

This does not require sharing the same transaction decoder. It only requires sharing the post-decoding kernel input.

### 5.3 Define chain revision profiles

Keep `RevisionConfig` as the ETH kernel capability object. Add or document chain-specific profile builders:

```text
EthPolicy::computeRevisionConfig(header) -> RevisionConfig
FiscoPolicy::computeRevisionConfig(header) -> FiscoRevisionConfig { ethConfig: RevisionConfig, fisco flags... }
makeIsthmusRevisionConfig() -> RevisionConfig
```

The arrows above describe the profile model. **Live assignments** are verified by `RevisionConfigProfileTest` and `bcos-evm/capability-matrix.md` (not this static list).

**Closed (2026-06-20):**

- `eip7702` / `eip7623`: set in `EthPolicy` at PRAGUE+, `FiscoPolicy` when `feature_evm_prague`, and `makeIsthmusRevisionConfig()`.
- `eip4844`: set in `EthPolicy`/`FiscoPolicy` at CANCUN+ and in `makeIsthmusRevisionConfig()`.

**Still open (profile-only or unwired consumers — see ADR-004):**

- `eip3651`, `prague_post_execution`: not assigned in `EthPolicy`; coinbase warm uses `txProps` + revision, not `eip3651`.
- `warm_access`, `eip1559`, `eip7823`: assigned in some builders but **no TE runtime consumer** — matrix rows use `feature-gated (profile-only)`.
- Full-field X-Macro snapshot test (`REVISION_CONFIG_BOOL_FIELDS`) — **implemented** (field enumeration + `static_assert`); per-fork expected-value tables remain **open**.

#### RevisionConfig field consumption (kernel runtime)

Profile builders must set each field explicitly, but **only consumed fields affect baseline-reachable kernel behavior**. Phase 3 audit and `RevisionConfigProfileTest` should distinguish assignment from consumption.

| Field | Read by kernel at runtime? | Actual gate |
| --- | --- | --- |
| `revision` | yes | evmone + address-based precompile tables |
| `warm_access` | **no** | `warmTransactionEntry` uses `rev >= EVMC_BERLIN`; `EthHost` always journals warm access |
| `eip1153`, `eip4844`, `eip5656`, `eip6780` | via evmone revision | revision threshold in policy builders |
| `eip2537`, `eip7212` | **partial** | `EthPrecompiles` uses `evmc_revision`; flags affect FISCO `PrecompiledManager`, not TE dispatch |
| `eip7623`, `eip7702` | yes | orchestration + `executeMessage` / settlement helpers |
| `eip1559`, `eip3651`, `prague_post_execution` | **no** in current bcos-evm TE path | profile/documentation placeholders until wired |
| `calldata_floor_per_token` | yes (when `eip7623`) | floor gas helpers |

Design constraints:

- `RevisionConfig` fields must be named after ETH kernel behavior, not chain products.
- FISCO bugfix flags stay in `FiscoRevisionConfig`.
- OPStack fork profiles should make clear which ETH EIPs are active and which OP-specific rules are handled outside the kernel.
- Adding a new `RevisionConfig` field requires updating all profile builders or explicitly documenting why a profile leaves it disabled.

### 5.4 Keep `HostExtension` as the only chain hook below the kernel boundary

`HostExtension` is the right mechanism for chain-specific behavior inside `EthHost`.

The hook contract should be documented as:

The current `HostExtension` contract (`bcos-evm/eth/policy/HostExtension.h`) defines seven hooks, all as virtual methods with a default implementation:

| Hook | Default | Ownership |
| --- | --- | --- |
| `allowSelfdestruct(const Account&)` | `true` | Chain policy |
| `allowDelegateCallToPrecompile()` | `true` | Chain policy |
| `skipHostValueTransfer()` | `false` | Chain policy |
| `tryChainPrecompile(rev, msg)` | `nullopt` | Chain extension |
| `prepareMessage(rev, msg&)` | no-op | Chain extension for CREATE/CALL side effects |
| `setCallerAddress(const evmc_address&)` | no-op | Chain extension for caller rewriting |
| `bumpContractCreateNonce(const evmc_address&)` | no-op | Chain extension for CREATE nonce semantics |

All hooks have safe defaults so that a `nullptr` extension (or `EthHostExtension`) reproduces standard ETH behavior. New hooks must follow the same pattern: a default that preserves ETH semantics, plus explicit overrides in `FiscoHostExtension` / `OpHostExtension` where the chain differs.

No FISCO or OPStack-specific include should be introduced into `eth` for new EIPs. If a new EIP requires chain behavior below `executeMessage`, it should be represented as a neutral hook or handled above the kernel.

### 5.5 Extract common tx feature preparation helpers

The three orchestrators should remain separate, but common EIP preparation should be factored into neutral helpers when duplication creates risk.

Candidates:

- build intrinsic gas components
- build EIP-7623 settlement snapshot
- normalize access list input
- normalize EIP-7702 authorization metadata
- derive `TransactionProperties`

This should be done incrementally. Do not introduce a large abstract executor unless repeated bugs prove the need.

### 5.6 Add enforcement gates

The architecture should not rely only on reviewer memory. Changes to EIP capability surfaces must force each chain path to either support the capability or explicitly opt out.

Required gates:

The enforcement must match what C++ and the current code structure can actually express. Pure compile-time guarantees are mostly not available here: `RevisionConfig` uses designated-initializer bitfields, and a missing field is silently `false` rather than a compile error. `HostExtension` hooks intentionally have defaults, so making them pure virtual would break the `nullptr` / `EthHostExtension` "standard ETH" path and contradict §3.3. The realistic enforcement is therefore a combination of a central definition point, an enumerated unit test, and a path-triggered CI check.

| Trigger | Required Response | Realistic Enforcement |
| --- | --- | --- |
| Add a `RevisionConfig` field | Set it (or document opt-out) in `EthPolicy`, `FiscoPolicy`, and the OPStack profile; update the matrix | `REVISION_CONFIG_BOOL_FIELDS(X)` macro + `revisionConfigBoolFieldCount()`; extend `RevisionConfigProfileTest`; CI requires profile test diff when `RevisionConfig.h` changes |
| Add an `ExecuteMessageInput` field | Wire it in each supporting orchestrator or mark it unsupported in the matrix | A `toExecuteMessageInput(...)` mapping plus propagation tests that assert the field reaches `executeMessage`; reuse the `Eip7702ApplyAuthorizationTest` direct-kernel pattern per orchestrator |
| Add a `HostExtension` hook | Add it as a defaulted virtual that preserves ETH behavior; override in `FiscoHostExtension`/`OpHostExtension` where the chain differs; extend the hook tests | Keep defaulted virtual (not pure virtual); extend `FiscoHostExtensionTest` / `EthHostExtensionHooksTest`; path CI requiring `*HostExtension*` changes when `HostExtension.h` changes |
| Add or change an EIP implementation | Update the capability matrix in the same change | Path CI: changes to `RevisionConfig.h` / `executeMessage.h` / `HostExtension.h` or any `EIP-NNNN` reference require **`bcos-evm/capability-matrix.md`** to change in the same PR |
| Mark a capability inherited | Add a kernel contract test and a baseline-path test | Named CTest targets/fixtures per capability (see §6.5 boundary rules) |

Why pure compile-time is not enough:

- C++ cannot statically assert that every profile builder assigned a given bitfield; an omitted designated initializer just defaults to `false`.
- `sizeof`/layout assertions catch new fields but not unassigned ones.

So the central definition point (X-Macro list / shared mapping helper) plus an enumerated profile/propagation test is the mechanism that actually fails when a chain is forgotten.

CI note: `docs/**` is under `paths-ignore`; **`bcos-evm/capability-matrix.md`** is outside that ignore and is the CI gate target. **Implemented:** `.github/workflows/capability-gate.yml` + `check-capability-matrix.sh` (token lint; surface→matrix diff; `RevisionConfig.h`→`RevisionConfigProfileTest.cpp`). **Still open:** per-fork profile expected-value tables; OPStack 7702 orchestrator E2E.

Silent defaults are not enough. A default `false` flag is acceptable only when the capability matrix says the chain is unsupported, feature-gated, or intentionally deviates. **`TransactionProperties` defaults (`warmDestination{true}`, `warmCoinbase{true}`) are implicit-default tx inputs**, not unsupported behavior.

---

## 6. Required Changes

### 6.1 Capability matrix (authoritative copy)

The normative capability matrix lives at **`bcos-evm/capability-matrix.md`**. That file is the single editable source; this section does not duplicate rows.

The matrix seed (audited 2026-06-20) covers EIP-2929, EIP-7702, EIP-7623, EIP-2537/7212, EIP-4844 (split profile/orchestration rows), builtin/chain precompiles, BCOS gas deviation, RevisionConfig profile fields, and OPStack deposit. See the matrix file for the authoritative table and change rules.

**Column semantics** (summary):

| Column | Path |
| --- | --- |
| ETH (reference) | `executeViaEth` / `EthTransactionExecutorImpl` — wiring audit only |
| BCOS (TE baseline) | `TransactionExecutorImpl` → `executeViaHost` |
| OPStack (TE baseline) | `OpStackTransactionExecutorImpl` → `opStackExecuteViaHost` |

Status tokens: `inherited`, `explicit`, `feature-gated`, `unsupported`, `deviation`. See the matrix file for definitions, the full table, test references, and change rules.

### 6.2 BCOS / OPStack tx-input wiring (as-built)

**Done (TE baseline):**

| Item | Evidence |
| --- | --- |
| BCOS EIP-7702 fields | `ExecuteViaHostInput`, `FiscoTxInputBuilder`, `TransactionExecutorImpl`, `Bcos7702ExecuteViaHostPropagationTest` |
| OPStack authorization + access list | `OpStackTxInputBuilder`, `OpStackTransactionExecutorImpl` |
| OPStack explicit `txProps` | `applyDefaultTxProps` + `OpStackTxPropsTest` |
| Shared warm destination helper | `TxFeaturePrepare::setWarmDestinationFromKind` on all three orchestrators |
| Builder decode tests | `EthTxInputBuilderTest`, `FiscoTxInputBuilderTest`, `OpStackTxInputBuilderTest` |

**Open:**

| Item | Notes |
| --- | --- |
| Web3 decoder in `bcos-executor` | Migrate to TE-local decoder or document permanent dependency (§2.1) |
| BCOS 7702 precheck / auth intrinsic gas | ADR-006 deferred; matrix orchestration row `unsupported` |
| OPStack 7702 orchestrator E2E | Kernel + builder covered; `opStackExecuteViaHost` delegation E2E optional follow-up |
| Legacy `bcos-executor` 7702 | Remains **unsupported** for inheritance contract |

EIP-7702 on BCOS is **feature-gated** (ADR-006): Web3 `0x04`, `feature_evm_prague`, profile `eip7702`. Precheck/auth interaction documented in matrix orchestration rows when implemented.

### 6.3 Revision profile alignment (as-built)

Profile builders: `EthPolicy`, `FiscoPolicy`, `makeIsthmusRevisionConfig()` (see §5.3 Closed/Still open).

**Enforcement:** `REVISION_CONFIG_BOOL_FIELDS` in `RevisionConfig.h`; `RevisionConfigProfileTest` spot snapshots; CI gate when `RevisionConfig.h` changes.

**Remaining:** per-fork expected-value tables for every bool field; wire or permanently document profile-only fields (`eip3651`, `prague_post_execution`, etc.).

### 6.4 Host extension boundary cleanup

Document and enforce that:

- `bcos-evm/eth` does not include BCOS or OPStack headers
- chain precompiles enter via `HostExtension::tryChainPrecompile`
- chain-specific value transfer behavior enters via `HostExtension::skipHostValueTransfer`
- CREATE/CALL side effects enter via `HostExtension::prepareMessage`

This preserves the shared kernel boundary.

### 6.5 Cross-path tests

Add a small capability-oriented test matrix.

These tests must distinguish the kernel contract from public transaction pipeline behavior. The public ETH, BCOS, and OPStack orchestrators are allowed to differ in gas accounting, prechecks, value transfer, receipt metadata, and chain precompile routing.

Required categories:

| Test Category | Purpose |
| --- | --- |
| Kernel contract test | Directly exercise `executeMessage` with controlled state, `RevisionConfig`, and tx inputs; expected behavior must be chain-neutral |
| Supporting path inheritance test | Exercise `executeViaHost` or `opStackExecuteViaHost` only for the specific inherited effect, not for full receipt/gas equivalence |
| Tx input propagation test | Prove access list, authorization list, and typed tx metadata reach `executeMessage` for each supporting orchestrator |
| Explicit orchestration test | Prove EIP-7623, OP floor gas, or similar chain-specific behavior is intentionally handled outside the kernel |
| Host extension test | Prove FISCO and OPStack chain precompiles enter through `HostExtension` and do not pollute the ETH kernel |

Test boundary rules:

- Kernel tests may compare ETH, BCOS, and OPStack only after normalizing chain-specific extension hooks.
- Public orchestrator tests should assert inherited kernel effects and documented deviations separately.
- A capability cannot be marked baseline-reachable `inherited` if only the ETH reference path has a test.
- A chain-level deviation must have a positive test for the deviation, not just an omitted inheritance test.

Example anchor cases:

- EIP-2929 warm account/storage behavior
- EIP-7702 delegation authorization application
- EIP-7623 gas used / floor data gas settlement
- OPStack L1Block predeploy dispatch
- FISCO precompile dispatch through `FiscoHostExtension`

### 6.6 Capability matrix ownership

The capability matrix (`bcos-evm/capability-matrix.md`) is part of the architecture contract, not optional documentation.

Ownership rules:

- The engineer adding or modifying an EIP owns the matrix update in the same change.
- Reviewers should reject an EIP change when the matrix does not mention the EIP or changed capability.
- A matrix row must use one of these support states for each chain: `inherited`, `explicit`, `unsupported`, `feature-gated`, or `deviation`.
- `unsupported`, `feature-gated`, and `deviation` rows must include a short reason and a test or review reference.
- If a row says baseline-reachable `inherited`, tests must prove the behavior through the relevant TE baseline path (not ETH reference alone).
- Footnotes may document **implicit-default** struct behavior; the default source must be cited.

---

## 7. EIP Onboarding Workflow

Future EIP work should follow this checklist:

1. Classify the EIP using the capability classes in this document.
2. Identify required `RevisionConfig` fields.
3. Identify required tx input fields.
4. Decide whether each chain should support, reject, or intentionally deviate.
5. Implement kernel logic only in `eth` if the capability is kernel-owned.
6. Wire tx fields through each supporting orchestrator.
7. Add explicit chain orchestration code only where the EIP requires it.
8. Add cross-path tests for inherited behavior and chain-specific tests for deviations.
9. Update the capability matrix.
10. Check `bcos-evm/docs/inheritance-work-tracker.md`, `architecture-known-gaps.md`, and the PR checklist before merge.

No EIP should be considered "shared" merely because it compiles inside `bcos-evm/eth`. It is shared only when all required revision and tx fields reach `executeMessage()` and the chain has not opted out.

---

## 8. Phased Rollout (as-built + remaining)

### Phase 1: Make the contract visible — **Done**

- **`bcos-evm/capability-matrix.md`** extended beyond seed; ADR-001–006 accepted.
- Inheritance boundary documented; PR checklist + known-gaps doc added.
- CI: `capability-gate` workflow + matrix token lint.

### Phase 2: Close tx-input wiring gaps — **Partial**

| Item | Status |
| --- | --- |
| BCOS EIP-7702 tx fields (`FiscoTxInputBuilder`, ADR-006) | **Done** |
| OPStack explicit `txProps` (`applyDefaultTxProps`) | **Done** |
| Shared `TxFeaturePrepare` helper | **Done** |
| Builder-level propagation tests | **Done** (`*TxInputBuilderTest`) |
| Orchestrator→`executeMessage` propagation E2E (7702 on BCOS baseline) | **Done** (`Bcos7702ExecuteViaHostPropagationTest`) |
| Orchestrator→`executeMessage` propagation E2E (7702 on OP baseline) | **Done** (`OpStack7702ExecuteViaHostPropagationTest`) |
| BCOS 7702 precheck/intrinsic orchestration | **Open** (ADR-006 deferred) |
| BCOS 7623 entry precheck test | **Done** (`Bcos7623PrecheckTest`) |
| Imported fixture → `executeViaHost` pipeline | **Done** (`ExecuteViaHostImportedFixtureTest`) |
| EIP-2537 kernel test ref | **Done** (`Eip2537KernelTest`) |
| BCOS auth orchestrator hook test | **Done** (`BcosAuthOrchestratorHookTest`; hook-only) |

### Phase 3: Align revision profiles — **Partial**

| Item | Status |
| --- | --- |
| `eip7702` / `eip4844` in Eth/Fisco/Isthmus builders | **Done** |
| `RevisionConfigProfileTest` MVP | **Done** |
| Profile-only fields documented (ADR-004) | **Done** |
| `REVISION_CONFIG_BOOL_FIELDS` X-Macro + full fork snapshots | **Done** (`eth_policy_full_fork_snapshots`, `fisco_policy_feature_gate_snapshots`, Isthmus sparse assert) |

### Phase 4: Reduce orchestration duplication — **Started**

- `TxFeaturePrepare` extracted; further intrinsic/7623 helpers remain optional follow-ups.

Deliverable tracking: `bcos-evm/docs/inheritance-work-tracker.md`.

---

## 9. Acceptance Criteria

Fulfillment as of 2026-06-20 (detail in §8 and `inheritance-work-tracker.md`):

| Criterion | Status |
| --- | --- |
| `bcos-evm/eth` free of BCOS/OP includes | **Met** (audited; see `architecture-known-gaps.md`) |
| `executeMessage()` documented as inheritance boundary | **Met** |
| Inheritance limited to TE baseline path | **Met** |
| Every kernel EIP has `RevisionConfig` or revision dependency in matrix | **Met** (ongoing rows for new EIPs) |
| Tx-input EIPs have visible TE field path or documented opt-out | **Partial** (7702 BCOS wired; decoder coupling deferred per ADR-007) |
| New surfaces require opt-in/out per chain | **Partial** (CI matrix gate; not compile-time) |
| BCOS/OP tests prove TE baseline kernel inheritance | **Partial** (7702/7623/fixture smoke + kernel tests; value transfer / CREATE nonce / full auth still open) |
| Kernel vs orchestrator tests separated | **Met** (policy in §6.5) |
| Chain EIPs have extension tests | **Partial** (L1Block done; auth hook-only; value transfer / nonce open) |
| Matrix updated with EIP changes | **Met** (process + CI) |

Target architecture is **partially achieved**; remaining work tracked in §8 Open rows and the tracker.

---

## 10. Risks and Mitigations

| Risk | Impact | Mitigation |
| --- | --- | --- |
| Treating `executeViaEth` as the common layer | BCOS/OPStack chain semantics become hard to preserve | Keep `executeMessage` as the inheritance boundary |
| Adding tx fields only to ETH path | Kernel EIP appears implemented but BCOS/OPStack silently miss it | Require tx-input propagation tests |
| Silent `RevisionConfig` default false | New EIP is accidentally disabled in one chain | Require revision profile audit for every new field |
| Treating BCOS as all execution paths | Legacy `bcos-executor` behavior is accidentally implied | Scope inheritance to `transaction-executor` baseline path |
| Over-abstracting transaction orchestration | BCOS/OPStack special rules become unclear | Extract small helpers only; keep orchestrators explicit |
| Matrix in `docs/**` with CI paths-ignore | Normative gate is advisory-only | Matrix at `bcos-evm/capability-matrix.md` + `capability-gate` workflow (extend coverage as surfaces grow) |
| Kernel row `inherited` read as end-to-end EIP support | Teams ship unreachable capabilities | Distinguish kernel-capable vs baseline-reachable; split matrix rows |
| Chain precompile logic leaking into `eth` | ETH kernel stops being reusable | Use `HostExtension` only |

---

## 11. Open Decisions

Decisions marked **[blocking]** must be resolved before the matching phase can start; the rest can be deferred.

1. Whether the capability matrix should live only in docs or also be mirrored in code comments near `RevisionConfig`. (non-blocking; affects §6.6 enforcement only)
2. **Resolved (ADR-006):** BCOS EIP-7702 uses **feature-gated** rollout: Web3 `0x04` tx fields via `FiscoTxInputBuilder`, `eip7702` enabled in `FiscoPolicy` when `feature_evm_prague` and `revision >= PRAGUE`. Precheck/intrinsic orchestration remains unsupported until explicitly implemented.
3. Whether OPStack fork profiles should remain helper functions such as `makeIsthmusRevisionConfig()` or move behind a named policy class. (non-blocking; refactor preference)
4. **Resolved (ADR-002 §7):** Baseline-path tests default to **orchestrator unit tests** (`executeViaHost` / `opStackExecuteViaHost`) first; TE executor fixtures optional follow-up. Builder decode tests alone do not satisfy baseline-reachable `inherited` rows.
5. **Resolved (ADR-007):** TE baseline input builders **continue** to depend on `bcos-executor` Web3 decode for the inheritance-contract scope; migration to a TE-local decoder is deferred.

---

## 12. Summary

The desired goal is achievable if the architecture draws the inheritance boundary at the ETH message execution kernel, not at the ETH transaction executor.

On the `transaction-executor` baseline path, BCOS and OPStack can automatically inherit EIPs whose kernel logic, revision profile, tx inputs, and orchestration prerequisites are all satisfied—or are explicitly documented as gated, unsupported, or deviating. They should not automatically inherit chain-level transaction lifecycle rules. Those must remain explicit in each extension layer. The legacy `bcos-executor` path is out of scope. The ETH reference path (`executeViaEth`) shares the kernel but is not BCOS/OPStack production inheritance proof.
