# ADR-026: TxFeeSettlement Deepening (EIP-1559 Cross-Path Projection)

**Status:** Accepted  
**Date:** 2026-06-26  
**Related:** ADR-005, ADR-016, ADR-018, ADR-019, ADR-021, ADR-025, `eth/gas/Eip1559.h`, `eth/gas/Eip1559Access.h`, `eth/reference/EthTxFeeLedger.h`, `opstack/OpStackTxFeeLedger.*`, `opstack/fee/OpStackPreDebitPlan.h`, `docs/superpowers/specs/2026-06-26-opstack-fee-projection-design.md`, `test/eth-eest-test/src/EthReferenceBridgeAdapter.cpp`

---

## Context

EIP-1559 formulas live in `eth/gas/Eip1559.h` (`normalizeGasCaps`, `resolveEffectiveGasPrice`, `tipPerGas`, `maxBalanceGasDebit`). ADR-016 documents TE pre-debit vs GST post-hoc **equivalence** when `finalGasUsed` matches.

**Observed friction:**

1. **Math is shared; composition is not.** `EthTxFeeLedger`, `OpStackTxFeeLedger`, and `applyGstTransactionSettlement` each re-compose caps, effective price, refund, and tip at independent call sites. OpStack `refundGas` hand-rolls `effectiveTip` instead of `tipPerGas()`.

2. **No single test oracle.** `Eip1559AccessTest` gates fork flags; unit tests on `Eip1559.h` cover formulas. No cross-path characterization asserts that TE buyGas/refund, GST post-hoc, and OpStack 1559 routing produce identical **plan amounts** for the same inputs.

3. **Call-site bugs hide behind pure functions.** ADR-025 phantom fee (pre-execution abort × `refundGas`) is a lifecycle composition error, not an `Eip1559.h` math error. Tighter projection locality reduces drift class.

4. **OpStack cap normalization diverges.** `OpStackTxFeeLedger::buyGas` applies a legacy fallback (`!m_hasGasFeeCap → gasPrice`) without calling `normalizeGasCaps` — ADR-016 deferred debt.

**Non-goals (v1):**

- FISCO `FiscoTxFeeLedger` migration (legacy `protocol::effectiveGasPrice`; matrix deviation).
- OpStack L1 / operator / blob fee math (remain sidecar in `opstack/` adapter).
- TE `buyGas` insufficient-balance **penalty** (TE production policy, not EIP-1559).
- Replacing `Eip1559.h` primitives or `Eip1559Access.h` fork gates.
- Async `EVMAccount` mutation abstraction inside kernel.

---

## Decision

Grilling outcomes (2026-06-26):

| # | Question | Choice |
| --- | --- | --- |
| D1 | Scope | **B.** Eth reference + Eth TE + OpStack **1559 core**; OpStack L1/operator/blob as adapter sidecar. FISCO out of v1. |
| D2 | Interface shape | **A.** Projection-only sync core; no State mutation. |
| D3 | `FeeInputs` | **B+C.** Primitive struct; `normalizeGasCaps` inside core; optional `eth/pipeline/FeeInputsProjection.h` mapper. |
| D4 | Base fee | **A.** Plan outputs `baseFeeAmount`; adapter decides burn (Eth/GST) vs route (OpStack). |
| D5 | buyGas penalty | **A.** Stays in `EthTxFeeLedger` adapter; may read `plan.effectiveGasPrice`. |
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
| `EthTxFeeLedger` | pre + post | async `EVMAccount`; burn `baseFeeAmount`; penalty on buyGas fail (adapter-only) |
| `applyGstTransactionSettlement` | post | `StateDiff` post-hoc; burn base |
| `OpStackTxFeeLedger` | pre + post | sync `ctx.state`; route `baseFeeAmount` → `m_baseFeeRecipient`; pre-debit composition via **Appendix B** `planOpStackPreDebit` |

Core **never** credits accounts. Routing policy stays at orchestration seam.

### 3. Optional pipeline mapper (`eth/pipeline/FeeInputsProjection.h`)

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
| `Eip1559Access.h` | Fork gates; unchanged; CI `check-eip1559-access.sh` |
| `EthPrecheckPolicy` | PR5: may set `ctx.gasPrice` from `planPreExecution(...).effectiveGasPrice` |
| `OpStackSettlement` | Gas unit math (`finalizeNormal`); fee **amounts** from plan + sidecar |

### 5. PR slice (core-first)

| PR | Deliverable | Gate |
| --- | --- | --- |
| **PR1** | `TxFeeSettlement.h` + `TxFeeSettlementTest` + `FeeSettlementCharacterizationTest` (oracle vs current ledger math) | characterization green |
| **PR2** | `applyGstTransactionSettlement` → `planPostExecution` | EEST smoke |
| **PR3** | `EthTxFeeLedger` buyGas/refund → plan; penalty stays in adapter | existing Eth TE tests |
| **PR4** | `OpStackTxFeeLedger` 1559 → plan; delete hand-rolled `effectiveTip`; sidecar unchanged | `OpStackSettlementTest` |
| **PR5** | `FeeInputsProjection.h` + `EthPrecheckPolicy` effective from plan | `Eip1559AccessTest` extended |

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
**Builds on:** ADR-021 Appendix A (`OpStackSettlementView`, `OpStackFeeSidecar`, `OpStackNormalFeeSettlement` — Done per `2026-06-26-opstack-fee-projection-design.md`).

### B.1 Context

ADR-026 PR4 routes OpStack **1559 core** through `TxFeeSettlement::planPreExecution` / `planPostExecution`. Remaining friction: `OpStackTxFeeLedger::buyGas` still hand-composes L1 cost, operator limit, blob debit, and `hasGasFeeCap` balance-check bifurcation (~45–80 lines) at the adapter call site.

`FeeSettlementCharacterizationTest` covers 1559 amounts only. No single oracle asserts full OpStack **pre-debit** totals (`totalDebit`, `balanceCheck`, sidecar snapshot fields).

### B.2 Grilling outcomes

| # | Question | Choice |
| --- | --- | --- |
| B-D1 | v1 scope | **Pre-debit only.** Post stays on `planPostExecution` + `OpStackTxFeeLedger::refundGas` routing. |
| B-D2 | Interface shape | **Primitive struct + mapper** (mirror `FeeInputs` + `FeeInputsProjection.h`). |
| B-D3 | Sidecar writes | **Embedded `OpStackFeeSidecarWrite` snapshot** in plan output. |
| B-D4 | Migration | **Single PR** — plan module + characterization + `buyGas` wiring. |
| B-D5 | Documentation | **This appendix** (not a separate ADR). |
| B-D6 | Skip guards | **`call` / `deposit` / `gasLimit ≤ 0` stay in `OpStackTxFeeLedger::buyGas`**; plan not invoked. |

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

Convenience only; plan does not include `OpStackSettlementView.h` if inputs are constructed manually in tests.

```cpp
OpStackPreDebitInputs toOpStackPreDebitInputs(OpStackSettlementView const& view);
```

`OpStackPreDebitInputs` bundles `gas::FeeInputs` fields plus `txValue`, blob hashes/feeCap, `rollupCostData`, `blockTime`, and `hasGasFeeCap` (from view accessors).

### B.5 Adapter contract (`OpStackTxFeeLedger::buyGas`)

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
| `OpStackNormalFeeSettlement` | Lifecycle + ADR-025 abort tree |
| `OpStackTxFeeLedger::refundGas` | Post: `planPostExecution` + recipient routing + Isthmus operator refund |
| `TxFeeSettlement` | 1559 core only (`eth/gas/`) |

### B.6 Non-goals (Appendix B)

- Post-debit composition module (`planOpStackPostSettlement`) — defer until post routing drifts across call sites.
- Deposit path symmetry.
- FISCO fee migration.
- Moving L1/operator/blob **primitives** into `eth/` kernel.
- Changing `OpStackNormalFeeSettlement` or ADR-025 abort semantics.

### B.7 Delivery (single PR)

| Action | File |
| --- | --- |
| Add | `opstack/fee/OpStackPreDebitPlan.h` (+ `.cpp` if needed) |
| Add | `opstack/fee/OpStackPreDebitInputs.h` |
| Modify | `opstack/OpStackTxFeeLedger.cpp` — delegate to plan |
| Add | `test/opstack/OpStackPreDebitCharacterizationTest.cpp` |
| Update | `docs/superpowers/specs/2026-06-26-opstack-fee-projection-design.md` §8 → pointer here |

**Gate:** full OpStack ctest green; `OpStackNormalFeeSettlementTest` (ADR-025) green; characterization matrix covers legacy gasPrice, type-2 1559, L1 hook, operator hook, blob tx, null hooks.

### B.8 Appendix B compliance checklist

- [x] `planOpStackPreDebit` has no `#include` of `State`, `TxPipelineContext`, or `eth/` orchestration headers beyond `TxFeeSettlement` / `FeeInputsProjection`.
- [x] `buyGas` skip guards (`call` / `deposit` / `gasLimit ≤ 0`) remain in adapter only.
- [x] Sidecar pre-buy fields written only from `plan.sidecar` snapshot.
- [x] `OpStackPreDebitCharacterizationTest` oracle matches pre-refactor `buyGas` inline math.
- [x] `refundGas` unchanged in v1.

---

## References

- ADR-016 (formulas and equivalence)
- ADR-005 § gas settlement domain
- ADR-021 + `docs/superpowers/specs/2026-06-26-opstack-fee-projection-design.md` (view / sidecar / normal settlement)
- geth `core/state_transition.go` — `buyGas`, `refundGas`, `EffectiveGasTip`
- Architecture review 2026-06-26 — candidates #2 OpStack fee composition, #6 TxFeeSettlement
