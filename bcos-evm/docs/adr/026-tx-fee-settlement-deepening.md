# ADR-026: TxFeeSettlement Deepening (EIP-1559 Cross-Path Projection)

**Status:** Accepted  
**Date:** 2026-06-26  
**Related:** ADR-005, ADR-016, ADR-018, ADR-019, ADR-021, ADR-025, `eth/eip/Eip1559.h`, `eth/eip/Eip1559Gate.h`, `eth/settlement/EthFeeSettlement.*`, `opstack/OpStackFeeSettlement.*`, `opstack/fee/OpStackPreDebitPlan.h`, `opstack/fee/OpStackPostSettlementPlan.h`, `docs/superpowers/specs/2026-06-26-opstack-fee-projection-design.md`, `docs/superpowers/specs/2026-07-03-eth-fee-settlement-remove-ledger-dependency-design.md`, `test/eth-eest-test/src/EthReferenceExecuteAdapter.cpp`

---

## Context

EIP-1559 formulas live in `eth/eip/Eip1559.h` (`normalizeGasCaps`, `resolveEffectiveGasPrice`, `tipPerGas`, `maxBalanceGasDebit`). ADR-016 documents TE pre-debit vs GST post-hoc **equivalence** when `finalGasUsed` matches.

**Observed friction:**

1. **Math is shared; composition is not.** `EthTxFeeSettlement`, `OpStackFeeSettlement`, and `applyGstTransactionSettlement` each re-compose caps, effective price, refund, and tip at independent call sites. OpStack `refundGas` hand-rolls `effectiveTip` instead of `tipPerGas()`.

2. **No single test oracle.** `Eip1559GateTest` gates fork flags; unit tests on `Eip1559.h` cover formulas. No cross-path characterization asserts that TE buyGas/refund, GST post-hoc, and OpStack 1559 routing produce identical **plan amounts** for the same inputs.

3. **Call-site bugs hide behind pure functions.** ADR-025 phantom fee (pre-execution abort × `refundGas`) is a lifecycle composition error, not an `Eip1559.h` math error. Tighter projection locality reduces drift class.

4. **OpStack cap normalization diverges.** `OpStackFeeSettlement::buyGas` applies a legacy fallback (`!m_hasGasFeeCap → gasPrice`) without calling `normalizeGasCaps` — ADR-016 deferred debt.

**Non-goals (v1):**

- FISCO `FiscoTxFeeSettlement` migration (legacy `protocol::effectiveGasPrice`; matrix deviation).
- OpStack L1 / operator / blob fee math (remain sidecar in `opstack/` adapter).
- TE `buyGas` insufficient-balance **penalty** (TE production policy, not EIP-1559).
- Replacing `Eip1559.h` primitives or `Eip1559Gate.h` fork gates.
- Async `EVMAccount` mutation abstraction inside kernel.

---

## Decision

Grilling outcomes (2026-06-26):

| # | Question | Choice |
| --- | --- | --- |
| D1 | Scope | **B.** Eth reference + Eth TE + OpStack **1559 core**; OpStack L1/operator/blob as adapter sidecar. FISCO out of v1. |
| D2 | Interface shape | **A.** Projection-only sync core; no State mutation. |
| D3 | `FeeInputs` | **B+C.** Primitive struct; `normalizeGasCaps` inside core; optional `eth/kernel/state-transition/FeeInputsMapping.h` mapper. |
| D4 | Base fee | **A.** Plan outputs `baseFeeAmount`; adapter decides burn (Eth/GST) vs route (OpStack). |
| D5 | buyGas penalty | **A.** Stays in `EthTxFeeSettlement` adapter; may read `plan.effectiveGasPrice`. |
| D6 | PR order | **A.** Core-first + characterization, then adapters PR2–PR5. |

### 1. New deep module: `eth/gas/TxFeeSettlement.h`

Sync, State-free. Calls `Eip1559.h` internally; does not duplicate formulas.

```cpp
namespace bcos::evm::gas {

struct FeeInputs {
    bcos::evm_standard::RevisionConfig const& revision;
    bcos::u256 baseFee;
    int64_t gasLimit;
    bcos::u256 gasPrice;           // legacy
    bcos::u256 gasTipCap;
    bcos::u256 gasFeeCap;
    uint8_t web3TypedTxKind;
    bool hasExplicitFeeCaps;
};

struct FeeSettlementPlan {
    bcos::u256 effectiveGasPrice;
    bcos::u256 maxBalanceDebit;    // pre: gasLimit × feeCap (1559) or gasLimit × gasPrice (legacy)
    bcos::u256 preDebitAmount;     // pre: gasLimit × effective
    // post fields meaningful after planPostExecution:
    bcos::u256 unusedRefund;       // gasRemaining × effective
    bcos::u256 coinbaseTip;        // gasUsed × tipPerGas()
    bcos::u256 baseFeeAmount;      // gasUsed × baseFee
    bcos::u256 senderNetDebit;      // gasUsed × effective
};

FeeSettlementPlan planPreExecution(FeeInputs const& inputs);

FeeSettlementPlan planPostExecution(
    FeeInputs const& inputs, int64_t gasUsed, int64_t gasRemaining);

}  // namespace bcos::evm::gas
```

**`planPreExecution`** — populates `effectiveGasPrice`, `maxBalanceDebit`, `preDebitAmount`; post fields zero.

**`planPostExecution`** — starts from `planPreExecution`, then fills refund/tip/base/senderNet from `gasUsed` / `gasRemaining`.

Invariant (1559 active): `senderNetDebit == coinbaseTip + baseFeeAmount` (base destroyed on Eth/GST; routed on OpStack).

### 2. Adapter responsibilities (ADR-005 unchanged)

| Path | Reads plan | Applies (timing + backend) |
| --- | --- | --- |
| `EthFeeSettlement` | pre + post | sync `ctx.state`; burn base; penalty on buyGas fail |
| `applyGstTransactionSettlement` | post | `StateDiff` post-hoc; burn base |
| `OpStackFeeSettlement` | pre + post | sync `ctx.state`; route `baseFeeAmount` → `m_baseFeeRecipient`; pre-debit via **Appendix B** `planOpStackPreDebit`; post-debit via **Appendix C** `planOpStackPostSettlement` |

Core **never** credits accounts. Routing policy stays at orchestration seam.

### 3. Optional pipeline mapper (`eth/kernel/state-transition/FeeInputsMapping.h`)

Convenience only; core does not include `TxPipelineContext.h`.

```cpp
FeeInputs toFeeInputs(
    bcos::evm_standard::RevisionConfig const& revision,
    state::BlockInfo const& blockInfo,
    FeeCapsView const& caps,   // gasPrice, tipCap, feeCap, kind, hasExplicitFeeCaps
    int64_t gasLimit);
```

Adapters without pipeline context (EEST GST) construct `FeeInputs` manually.

### 4. Relationship to existing modules

| Module | Role after ADR-026 |
| --- | --- |
| `Eip1559.h` | Primitive formulas; called by `TxFeeSettlement` |
| `Eip1559Gate.h` | Fork gates; unchanged; CI `check-eip1559-access.sh` |
| `EthPrecheckPolicy` | PR5: may set `ctx.gasPrice` from `planPreExecution(...).effectiveGasPrice` |
| `OpStackSettlement` | Gas unit math (`finalizeNormal`); fee **amounts** from plan + sidecar |

### 5. PR slice (core-first)

| PR | Deliverable | Gate |
| --- | --- | --- |
| **PR1** | `TxFeeSettlement.h` + `TxFeeSettlementTest` + `FeeSettlementCharacterizationTest` (oracle vs current ledger math) | characterization green |
| **PR2** | `applyGstTransactionSettlement` → `planPostExecution` | EEST smoke |
| **PR3** | `EthTxFeeSettlement` buyGas/refund → plan; penalty stays in adapter | existing Eth TE tests |
| **PR4** | `OpStackFeeSettlement` 1559 → plan; delete hand-rolled `effectiveTip`; sidecar unchanged | `OpStackSettlementTest` |
| **PR5** | `FeeInputsMapping.h` + `EthPrecheckPolicy` effective from plan | `Eip1559GateTest` extended |

---

## Consequences

- ADR-016 TE/GST equivalence becomes **test-enforced** via shared `FeeSettlementPlan` oracle.
- ADR-016 consequence "OpStack `resolveEffectiveGasPrice` dedup deferred" **closed** in PR4.
- FISCO remains on `protocol::effectiveGasPrice` until a separate ADR; matrix deviation unchanged.
- New CI optional: characterization test in `cross/` or `eth/` ctest target.

---

## Compliance checklist

- [x] `TxFeeSettlement` has no `#include` of `State`, `TxPipelineContext`, `bcos/`, or `opstack/`.
- [x] Adapters do not call `normalizeGasCaps` / `resolveEffectiveGasPrice` directly for settlement (route through plan).
- [x] `baseFeeAmount` routed only in OpStack adapter; Eth/GST burn.
- [x] buyGas penalty logic not added to core interface.
- [x] `FeeSettlementCharacterizationTest` covers legacy + type-2 + OpStack base-fee route amounts.

---

## Appendix B — OpStack Pre-Debit Composition (post PR4)

**Status:** Accepted (grilling 2026-06-26)  
**Extends:** ADR-026 §1 non-goals — L1 / operator / blob math stays in `opstack/`; this appendix deepens **composition** above `TxFeeSettlement`, not kernel math.  
**Builds on:** ADR-021 Appendix A (`OpStackSettlementProjection`, `OpStackFeeSidecar`, `OpStackNormalTxFeeCoordinator` — Done per `2026-06-26-opstack-fee-projection-design.md`).

### B.1 Context

ADR-026 PR4 routes OpStack **1559 core** through `TxFeeSettlement::planPreExecution` / `planPostExecution`. Remaining friction: `OpStackFeeSettlement::buyGas` still hand-composes L1 cost, operator limit, blob debit, and `hasGasFeeCap` balance-check bifurcation (~45–80 lines) at the adapter call site.

`FeeSettlementCharacterizationTest` covers 1559 amounts only. No single oracle asserts full OpStack **pre-debit** totals (`totalDebit`, `balanceCheck`, sidecar snapshot fields).

### B.2 Grilling outcomes

| # | Question | Choice |
| --- | --- | --- |
| B-D1 | v1 scope | **Pre-debit only.** Post stays on `planPostExecution` + `OpStackFeeSettlement::refundGas` routing. |
| B-D2 | Interface shape | **Primitive struct + mapper** (mirror `FeeInputs` + `FeeInputsMapping.h`). |
| B-D3 | Sidecar writes | **Embedded `OpStackFeeSidecarWrite` snapshot** in plan output. |
| B-D4 | Migration | **Single PR** — plan module + characterization + `buyGas` wiring. |
| B-D5 | Documentation | **This appendix** (not a separate ADR). |
| B-D6 | Skip guards | **`call` / `deposit` / `gasLimit ≤ 0` stay in `OpStackFeeSettlement::buyGas`**; plan not invoked. |

### B.3 New deep module: `opstack/fee/OpStackPreDebitPlan.h`

Sync, State-free. Calls `TxFeeSettlement` internally; does not duplicate 1559 formulas or L1/operator/blob primitives.

```cpp
namespace bcos::evm {

struct OpStackFeeHooks {
    std::function<u256(RollupCostData const&, uint64_t)> const* l1CostFunc{nullptr};
    std::function<u256(uint64_t gasLimit, uint64_t blockTime)> const* operatorCostFunc{nullptr};
};

struct OpStackFeeSidecarWrite {
    bcos::u256 effectiveGasPrice{0};
    bcos::u256 baseFee{0};
    bcos::u256 l1CostCharged{0};
    bcos::u256 operatorCostLimit{0};
};

struct OpStackPreDebitPlan {
    gas::FeeSettlementPlan core1559;
    OpStackFeeSidecarWrite sidecar;
    bcos::u256 blobDebit{0};           // included in totalDebit
    bcos::u256 blobBalanceCheck{0};    // fee-cap balance path
    bcos::u256 totalDebit{0};          // actual pre-debit from sender
    bcos::u256 balanceCheck{0};        // threshold incl. txValue
};

OpStackPreDebitPlan planOpStackPreDebit(
    OpStackPreDebitInputs const& inputs, OpStackFeeHooks const& hooks);

}  // namespace bcos::evm
```

**`planOpStackPreDebit`** — populates `core1559` via `gas::planPreExecution`, applies L1/operator hooks, blob math, `totalDebit` / `balanceCheck`, and `sidecar` snapshot. Does **not** mutate `State` or `OpStackFeeSidecar`.

**`floorDataGas`** is out of scope — written by precheck, not `buyGas`.

### B.4 Mapper: `opstack/fee/OpStackPreDebitInputs.h`

Convenience only; plan does not include `OpStackSettlementProjection.h` if inputs are constructed manually in tests.

```cpp
OpStackPreDebitInputs toOpStackPreDebitInputs(OpStackSettlementProjection const& view);
```

`OpStackPreDebitInputs` bundles `gas::FeeInputs` fields plus `txValue`, blob hashes/feeCap, `rollupCostData`, `blockTime`, and `hasGasFeeCap` (from view accessors).

### B.5 Adapter contract (`OpStackFeeSettlement::buyGas`)

```text
if isCall || isDeposit || originalGasLimit <= 0:
    return true                                    // B-D6 — adapter gate, no plan

inputs = toOpStackPreDebitInputs(view)
hooks  = { &m_l1CostFunc, &m_operatorCostFunc }
plan   = planOpStackPreDebit(inputs, hooks)

sidecar.effectiveGasPrice = plan.sidecar.effectiveGasPrice
sidecar.baseFee           = plan.sidecar.baseFee
sidecar.l1CostCharged     = plan.sidecar.l1CostCharged
sidecar.operatorCostLimit = plan.sidecar.operatorCostLimit

if senderBalance < plan.balanceCheck:
    fail EVMC_INSUFFICIENT_BALANCE; return false

state.debit(sender, plan.totalDebit)
return true
```

**Unchanged modules:**

| Module | Role |
| --- | --- |
| `OpStackNormalTxFeeCoordinator` | Lifecycle + ADR-025 abort tree; `projectNormalReceiptMeta` reads plan from `refundGas` |
| `OpStackFeeSettlement::refundGas` | Post: delegate to **Appendix C**; apply credits; return plan |
| `TxFeeSettlement` | 1559 core only (`eth/gas/`) |

### B.6 Non-goals (Appendix B)

- Post-debit composition module — see **Appendix C** (`planOpStackPostSettlement`).
- Deposit path symmetry.
- FISCO fee migration.
- Moving L1/operator/blob **primitives** into `eth/` kernel.
- Changing `OpStackNormalTxFeeCoordinator` or ADR-025 abort semantics.

### B.7 Delivery (single PR)

| Action | File |
| --- | --- |
| Add | `opstack/fee/OpStackPreDebitPlan.h` (+ `.cpp` if needed) |
| Add | `opstack/fee/OpStackPreDebitInputs.h` |
| Modify | `opstack/OpStackFeeSettlement.cpp` — delegate to plan |
| Add | `test/opstack/OpStackPreDebitCharacterizationTest.cpp` |
| Update | `docs/superpowers/specs/2026-06-26-opstack-fee-projection-design.md` §8 → pointer here |

**Gate:** full OpStack ctest green; `OpStackNormalTxFeeCoordinatorTest` (ADR-025) green; characterization matrix covers legacy gasPrice, type-2 1559, L1 hook, operator hook, blob tx, null hooks.

### B.8 Appendix B compliance checklist

- [x] `planOpStackPreDebit` has no `#include` of `State`, `TxPipelineContext`, or `eth/` orchestration headers beyond `TxFeeSettlement` / `FeeInputsProjection`.
- [x] `buyGas` skip guards (`call` / `deposit` / `gasLimit ≤ 0`) remain in adapter only.
- [x] Sidecar pre-buy fields written only from `plan.sidecar` snapshot.
- [x] `OpStackPreDebitCharacterizationTest` oracle matches pre-refactor `buyGas` inline math.
- [x] `refundGas` unchanged in Appendix B v1 (Appendix C deepens post path).

---

## Appendix C — OpStack Post-Settlement Composition (post Appendix B)

**Status:** Accepted (grilling 2026-06-26)  
**Extends:** ADR-026 Appendix B — mirrors pre-debit deepening for the post-execute fee seam.  
**Builds on:** ADR-021 Appendix A (`OpStackSettlementProjection`, `OpStackFeeSidecar`, `OpStackNormalTxFeeCoordinator`), Appendix B (`OpStackPreDebitPlan`, sidecar snapshots).

### C.1 Context

Appendix B deepened **pre-debit** (`planOpStackPreDebit`). Remaining friction: `OpStackFeeSettlement::refundGas` hand-composes 1559 post amounts, recipient routing, Isthmus operator refund, and operator fee charge at the adapter call site (~40 lines). `projectNormalReceiptMeta` in `OpStackNormalTxFeeCoordinator.cpp` **re-invokes** `m_operatorCostFunc(gasUsed)` for receipt metadata — a locality leak (same hook, two call sites).

`FeeSettlementCharacterizationTest` covers 1559 post core via `planPostExecution` only. No single oracle asserts full OpStack **post-settlement** amounts (`senderOperatorRefund`, `l1FeeRouted`, `operatorFeeCharged`) or the operator refund formula.

### C.2 Grilling outcomes

| # | Question | Choice |
| --- | --- | --- |
| C-D1 | Plan output boundary | **A.** Pure amount oracle; receipt reads `l1FeeRouted` / `operatorFeeCharged` from plan; `operatorFeeScalar` / `operatorFeeConstant` still from `OpStackFeeParams`. |
| C-D2 | Input shape | **A.** `OpStackPostSettlementInputs` + `toOpStackPostSettlementInputs(view, settled)` mapper (mirror Appendix B). Plan does not include `OpStackSettlementProjection.h`. |
| C-D3 | Isthmus fork gating | **A.** Plan does not read fork schedule. State routing unchanged. `isOpStackIsthmus` gates receipt `operatorFee` write only (orchestration policy). |
| C-D4 | `refundIsthmusOperatorCost` | **A.** Delete public method; Isthmus refund covered by `plan.senderOperatorRefund` + characterization (migrate `RefundIsthmusTest`). |
| C-D5 | Delivery | **A.** Single PR + this appendix. |
| C-D6 | Plan computation site | **A.** `refundGas` computes plan once, applies credits, **returns** `OpStackPostSettlementPlan` for `projectNormalReceiptMeta`. |
| C-D7 | Skip guards | **A.** `deposit` / zero-fee `call` early-return **default empty plan**; no state mutation. |
| C-D8 | `l1FeeRouted` | **A.** Passthrough `inputs.l1CostCharged` (sidecar snapshot from `buyGas`); post does **not** re-invoke `l1CostFunc`. |

### C.3 New deep module: `opstack/fee/OpStackPostSettlementPlan.h`

Sync, State-free. Calls `TxFeeSettlement::planPostExecution` internally; does not duplicate 1559 formulas. L1 amount is snapshot passthrough; operator **used** cost invokes `operatorCostFunc` once.

```cpp
namespace bcos::evm {

struct OpStackPostSettlementInputs {
    gas::FeeInputs fee;
    int64_t gasUsed{0};
    int64_t gasRemaining{0};
    uint64_t blockTime{0};
    bcos::u256 l1CostCharged{0};      // sidecar snapshot (buyGas)
    bcos::u256 operatorCostLimit{0};  // sidecar snapshot (buyGas)
};

struct OpStackPostSettlementPlan {
    gas::FeeSettlementPlan core1559;
    bcos::u256 l1FeeRouted{0};         // == inputs.l1CostCharged
    bcos::u256 operatorFeeCharged{0}; // hook(gasUsed, blockTime); 0 if no hook
    bcos::u256 senderOperatorRefund{0}; // max(0, operatorCostLimit - operatorFeeCharged)
    // sender unused refund, coinbase tip, base fee in core1559.{unusedRefund,coinbaseTip,baseFeeAmount}
};

OpStackPostSettlementPlan planOpStackPostSettlement(
    OpStackPostSettlementInputs const& inputs,
    OpStackFeeHooks const& hooks) noexcept;

}  // namespace bcos::evm
```

**`planOpStackPostSettlement`** — populates `core1559` via `gas::planPostExecution(inputs.fee, gasUsed, gasRemaining)`. Sets `l1FeeRouted = inputs.l1CostCharged` (no L1 hook on post). If `hooks.operatorCostFunc` is set: computes `operatorFeeCharged` from **used** gas; `senderOperatorRefund = operatorCostLimit - operatorFeeCharged` when positive. Does **not** mutate `State`, read fork schedule, or write receipt metadata.

**Post hooks usage:** only `operatorCostFunc` (for used cost). `l1CostFunc` is **not** passed on the post path.

### C.4 Mapper: `opstack/fee/OpStackPostSettlementInputs.h`

Convenience only; plan does not include `OpStackSettlementProjection.h` if inputs are constructed manually in tests.

```cpp
OpStackPostSettlementInputs toOpStackPostSettlementInputs(
    OpStackSettlementProjection const& view,
    OpStackTxFinalizeResult const& settled) noexcept;
```

Mapper projects `gas::FeeInputs` from view/ctx (same caps path as pre-debit), `gasUsed` / `gasRemaining` from `settled`, and sidecar snapshots `l1CostCharged` / `operatorCostLimit` from `view.feeSidecar()`.

**FeeInputs vs sidecar invariant:** Post mapper uses `toFeeInputs(ctx.revisionConfig, view.blockInfo(), FeeCapsView{...}, ctx.originalGasLimit)` — the same projection as `toOpStackPreDebitInputs`. It does **not** feed `sidecar.effectiveGasPrice` into `planPostExecution` (1559 math is recomputed from caps). After successful `buyGas`, characterization must assert `plan.core1559.effectiveGasPrice == sidecar.effectiveGasPrice` for the same view inputs.

### C.5 Adapter contract (`OpStackFeeSettlement::refundGas`)

Signature change: `task::Task<OpStackPostSettlementPlan> refundGas(...)`.

```text
if isDeposit:
    return {}                                         // C-D7 — empty plan, no apply

if isCall && skipTransactionChecks && noBaseFee &&
   gasFeeCap == 0 && gasTipCap == 0:
    return {}                                         // C-D7

inputs = toOpStackPostSettlementInputs(view, settled)
hooks  = { nullptr, &m_operatorCostFunc }             // L1 hook omitted on post (C-D8)
plan   = planOpStackPostSettlement(inputs, hooks)

apply credits:
    sender        += plan.core1559.unusedRefund + plan.senderOperatorRefund
    coinbase      += plan.core1559.coinbaseTip
    baseFeeRecip  += plan.core1559.baseFeeAmount
    l1FeeRecip    += plan.l1FeeRouted
    operatorRecip += plan.operatorFeeCharged

return plan
```

Delete `refundIsthmusOperatorCost` (public method removed; logic absorbed into plan).

### C.6 Receipt projection (`OpStackNormalTxFeeCoordinator`)

#### C.6.1 `settleNormal` return type

Introduce an anonymous-namespace aggregate in `OpStackNormalTxFeeCoordinator.cpp` (not a public header — internal seam only):

```cpp
struct NormalSettleOutcome {
    OpStackTxFinalizeResult settled;
    OpStackPostSettlementPlan feePlan;
};
```

`settleNormal` signature becomes:

```cpp
task::Task<NormalSettleOutcome> settleNormal(
    OpStackSettlementProjection view,
    TxPipelineExitKind exitKind,
    OpStackFeeSettlement& ledger,
    GasPoolHooks const& gasPool);
```

`completeAfterPipeline` unpacks:

```text
auto outcome = co_await settleNormal(view, ctx.exitKind, ledger, gasPool)
output.gasUsed = outcome.settled.gasUsed
projectNormalReceiptMeta(output, view, feeParams, outcome.settled, outcome.feePlan)
output.stateDiff = ctx.state.build_diff()
```

#### C.6.2 Flow

`settleNormal` captures plan from `refundGas` and passes it to `projectNormalReceiptMeta`:

```text
settled = finalizeNormal(ctx, sidecar, exitKind)
feePlan = await ledger.refundGas(view, settled)
gasPool.returnGas(...)
return { settled, feePlan }
```

**`projectNormalReceiptMeta`** signature adds `OpStackPostSettlementPlan const& feePlan` (thinned body):

```text
output.receiptMeta.l1Fee = feePlan.l1FeeRouted
if isOpStackIsthmus(forkSchedule, blockTime) && m_operatorCostFunc:
    output.receiptMeta.operatorFee = feePlan.operatorFeeCharged   // no second hook call
    if feeParams.operatorFeeScalar != 0 || feeParams.operatorFeeConstant != 0:
        copy scalar/constant from feeParams
```

Skip paths (empty `feePlan`): `l1Fee` and `operatorFee` remain zero; Isthmus gate still applies before writing `operatorFee`.

#### C.6.3 ADR-025 abort boundary (negative)

`completeAfterPipeline` **does not** call `settleNormal` / `refundGas` when `isNormalPreExecutionReject(ctx.exitKind)` — only `abortNormalAfterBuyGas`. No `NormalSettleOutcome` is produced on that path. Regression guard: existing `OpStackNormalTxFeeCoordinatorTest` ADR-025 matrix (phantom fee).

### C.7 Unchanged modules

| Module | Role |
| --- | --- |
| `OpStackNormalTxFeeCoordinator` | ADR-025 abort tree unchanged; only receipt reads plan |
| `planOpStackPreDebit` / `buyGas` | Appendix B unchanged |
| `finalizeNormal` / `postExecuteGasSettlement` | Gas **units** only; amounts from plan |
| `TxFeeSettlement` | 1559 core only (`eth/gas/`) |
| ADR-025 abort semantics | Unchanged |

### C.8 Non-goals (Appendix C)

- Deposit path post-settlement symmetry.
- FISCO fee migration.
- Moving L1/operator **primitives** into `eth/` kernel.
- Fork policy inside plan (Isthmus gating stays in orchestration).
- Receipt scalar fields in plan output (`OpStackFeeParams` remains source).
- Changing `OpStackNormalTxFeeCoordinator` ADR-025 decision tree.

### C.9 Delivery (single PR)

| Action | File |
| --- | --- |
| Add | `opstack/fee/OpStackPostSettlementPlan.h` (+ `.cpp` if needed) |
| Add | `opstack/fee/OpStackPostSettlementInputs.h` |
| Modify | `opstack/OpStackFeeSettlement.h` — `refundGas` returns plan; delete `refundIsthmusOperatorCost` |
| Modify | `opstack/OpStackFeeSettlement.cpp` — delegate to plan |
| Modify | `opstack/OpStackNormalTxFeeCoordinator.cpp` — `NormalSettleOutcome`, `settleNormal`, `projectNormalReceiptMeta` |
| Add | `test/opstack/OpStackPostSettlementCharacterizationTest.cpp` |
| Delete | `test/opstack/RefundIsthmusTest.cpp` — cases migrated to characterization (see below) |
| Modify | `test/cmake/OpStackTests.cmake` — drop `RefundIsthmusTest` target |
| Update | `docs/superpowers/specs/2026-06-26-opstack-fee-projection-design.md` §8 → pointer here |

**`RefundIsthmusTest` migration (mandatory):** Delete the file. Port `RefundIsthmus_refundsLimitMinusUsedCost` into `OpStackPostSettlementCharacterizationTest` as a **plan-field** assertion (no `State`, no ledger sub-method):

```text
operatorCostLimit = 2618, gasUsed = 500, hook(g) = g + 1000
→ operatorFeeCharged = 1500
→ senderOperatorRefund = 1118
```

Integration routing for operator recipient remains in `OpStackFeeSettlementCtxTest`.

**Gate:** full OpStack ctest green; `OpStackNormalTxFeeCoordinatorTest` (ADR-025) green; `OpStackFeeSettlementCtxTest` routing green; characterization matrix covers:

| Case | Oracle |
| --- | --- |
| Legacy gasPrice post | Pre-refactor `refundGas` 1559 portion |
| Type-2 1559 | `planPostExecution` + `FeeSettlementCharacterizationTest` |
| Post effectiveGasPrice vs sidecar | After buyGas snapshot: `plan.core1559.effectiveGasPrice == sidecar.effectiveGasPrice` |
| L1 hook non-zero | `l1FeeRouted == inputs.l1CostCharged` |
| Operator hook | `operatorFeeCharged` + `senderOperatorRefund` |
| Null hooks | OP fields zero |
| Isthmus refund | `limit - used` formula (ex-`RefundIsthmusTest`; plan fields only) |
| Skip deposit / zero-fee call | Empty plan, no state change |
| **ADR-025 pre-exec abort** | `refundGas` not invoked; `OpStackNormalTxFeeCoordinatorTest` phantom-fee matrix |

### C.10 Appendix C compliance checklist

- [x] `planOpStackPostSettlement` has no `#include` of `State`, `TxPipelineContext`, or `eth/` orchestration headers beyond `TxFeeSettlement` / `FeeInputsProjection`.
- [x] Post path does not invoke `l1CostFunc` (`l1FeeRouted` is snapshot passthrough).
- [x] `operatorCostFunc` invoked at most once per tx (in plan); `projectNormalReceiptMeta` does not call hook.
- [x] Skip guards (`deposit` / zero-fee `call`) remain in adapter only; return default plan.
- [x] `refundIsthmusOperatorCost` public method removed.
- [x] `OpStackPostSettlementCharacterizationTest` oracle matches pre-refactor `refundGas` inline math.
- [x] `settleNormal` returns `NormalSettleOutcome`; `completeAfterPipeline` passes `feePlan` to `projectNormalReceiptMeta`.
- [x] Post `effectiveGasPrice` characterization matches sidecar after `buyGas`.
- [x] `RefundIsthmusTest.cpp` deleted; Isthmus refund covered in `OpStackPostSettlementCharacterizationTest`.
- [x] ADR-025 abort path does not call `refundGas` (`OpStackNormalTxFeeCoordinatorTest` green).

---

## References

- ADR-016 (formulas and equivalence)
- ADR-005 § gas settlement domain
- ADR-021 + `docs/superpowers/specs/2026-06-26-opstack-fee-projection-design.md` (view / sidecar / normal settlement)
- geth `core/state_transition.go` — `buyGas`, `refundGas`, `EffectiveGasTip`
- Architecture review 2026-06-26 — candidates #1 OpStack post-settlement, #2 OpStack fee composition, #6 TxFeeSettlement
