# ADR-026: TxFeeSettlement Deepening (EIP-1559 Cross-Path Projection)

**Status:** Accepted  
**Date:** 2026-06-26  
**Related:** ADR-005, ADR-016, ADR-018, ADR-019, ADR-021, ADR-025, `eth/gas/Eip1559.h`, `eth/gas/Eip1559Access.h`, `eth/reference/EthTxFeeLedger.h`, `opstack/OpStackTxFeeLedger.*`, `test/eth-eest-test/src/EthReferenceBridgeAdapter.cpp`

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
| `OpStackTxFeeLedger` | pre + post | sync `ctx.state`; route `baseFeeAmount` → `m_baseFeeRecipient`; sidecar adds L1/operator/blob to pre-debit |

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

## References

- ADR-016 (formulas and equivalence)
- ADR-005 § gas settlement domain
- geth `core/state_transition.go` — `buyGas`, `refundGas`, `EffectiveGasTip`
- Architecture review 2026-06-26 — candidate #6 TxFeeSettlement
