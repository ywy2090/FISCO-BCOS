# ADR-014: OpStack Fork Schedule vs Historical L1 Cost

**Status:** Accepted  
**Date:** 2026-06-21  
**Related:** R3-ORCH-3, ADR-004, ADR-005, ADR-012, `OpStackForkSchedule.h`, `OpStackFee.*`, `bcos-evm/capability-matrix.md`, `docs/superpowers/specs/2026-06-21-opstack-r3-orch3-l1-cost-fork-selector-design.md`

**Audit closure:** **R3-ORCH-3 CLOSED** — fork scaffold + intentional historical-fork scope documented; Isthmus+ product baseline unchanged.

---

## Context

Wave 3 Isthmus re-audit (R3-ORCH-3) flagged that OPStack orchestration hard-coded `l1CostFjord` and used `m_isIsthmus` as a profile heuristic for operator fee, while op-geth selects L1 cost and operator fee via `ChainConfig.Optimism.*Time` and per-block cache (`NewL1CostFunc` / `NewOperatorCostFunc` in `rollup_cost.go`).

ORCH-1/2 (header `baseFee` / `blobBaseFee` from OPF1) are out of scope here. ORCH-3 covers **rollup data fee formula selection** and **operator fee fork gating** only.

Phase 1 product baseline is **Isthmus+** via `makeIsthmusPlusForkSchedule()` (`fjordTime=0`, `isthmusTime=0`). Bedrock and Ecotone L1 formulas, first-Ecotone Bedrock fallback, and OP mainnet historical replay are **intentionally unsupported**. Genesis / ledger fork injection (ORCH-3-INT-1) is Phase 2.

`RevisionConfig` gates EVM kernel semantics (`executeMessage`, evmone, precompiles). `OpStackForkSchedule` gates L1 rollup cost and operator fee only. The two structs are **strictly orthogonal** — TE injects both in parallel; neither may derive the other (design Q5=A).

---

## Decision

### 1. OpStackForkSchedule + per-block cache factories

Introduce `OpStackForkSchedule` with `std::optional<uint64_t> fjordTime` and `isthmusTime`. Fork active when `forkTime.has_value() && *forkTime <= blockTime`.

Shared helpers `makeCachedL1CostFunc` / `makeCachedOperatorCostFunc` implement per-block cache; on cache miss they re-evaluate fork gates and params:

| Factory (test) | Factory (production) |
| --- | --- |
| `selectL1CostFunc(schedule, params)` | `wireL1CostFuncWithState(schedule, state)` |
| `selectOperatorCostFunc(schedule, params)` | `wireOperatorCostFuncWithState(schedule, state)` |

- `select*` value-captures an `OpStackFeeParams` snapshot (unit tests).
- `wire*` captures `StateView const&`; cache miss calls `loadOpStackFeeParams(state)`.
- Closures are created per `opStackExecuteViaHost` invocation; **must not** be reused across transactions.

Remove `OpStackTxExecutor::m_isIsthmus` and all profile-heuristic fee gates. Receipt operator metadata is gated by `isOpStackIsthmus(forkSchedule, blockTime)` instead.

### 2. Consumption table (normative)

| 字段 / 条款 | Category | Consumer / 内容 |
| --- | --- | --- |
| `fjordTime` | consumed | `makeCachedL1CostFunc` miss → Fjord / throw |
| `isthmusTime` | consumed | `makeCachedOperatorCostFunc` miss → Isthmus / zero |
| L1 pre-Fjord throw | deviation | op-geth → Bedrock; FB throw; positive test |
| Operator pre-Isthmus zero | explicit | aligns op-geth |
| Empty rollup → 0 vs nil | deviation | profile-equivalent |
| Cache miss 读 state | consumed (wire path) | `loadOpStackFeeParams(state)` on miss |
| vs RevisionConfig | policy | strictly orthogonal (Q5=A) |

**Deviation detail — L1 pre-Fjord:** op-geth falls back to Bedrock L1 cost. FB has no Bedrock/Ecotone implementation; pre-Fjord user-tx invoke throws `std::invalid_argument("OpStack: pre-Fjord L1 cost unsupported")`. Deposit txs bypass `buyGas` / L1 closure and do not throw.

**Deviation detail — empty rollup:** If `RollupCostData::isEmpty()`, FB returns `0` on invoke. op-geth outer closure returns nil for empty data. Semantically profile-equivalent on Isthmus+ (deposits skip L1; user txs with rollup bytes never empty in practice).

**Explicit — operator pre-Isthmus:** Returns zero without throw, matching op-geth pre-Isthmus behavior.

### 3. Phase 1 scope boundaries

| In scope | Out of scope |
| --- | --- |
| `makeIsthmusPlusForkSchedule()` preset | Bedrock / Ecotone L1 formulas |
| Fjord L1 via `l1CostFjord` when Fjord active | First-Ecotone Bedrock fallback |
| Isthmus operator via `operatorCostIsthmus` when Isthmus active | Jovian+ formulas (extension point only) |
| pre-Fjord L1 throw (documented deviation) | `LedgerConfig` / genesis fork fields (ORCH-3-INT-1) |
| pre-Isthmus operator zero | RevisionConfig fields for fork times |

### 4. Future fork contract (§7 of design spec)

Adding a fork requires, in one PR: new `optional` schedule field + `isOpStack*()` helper; new formula in `OpStackFee.cpp`; shared cache-miss branch; capability-matrix row; op-geth parity test; update to this consumption table.

---

## Consequences

- R3-ORCH-3 closes with extensible scaffold and documented intentional gaps for historical forks.
- Isthmus+ TE behavior is unchanged: Fjord L1 + Isthmus operator fee via `makeIsthmusPlusForkSchedule()`.
- Matrix gains separate rows for tx bytes, fork selection, pre-Fjord deviation, and operator fork gate (ADR-003 granularity).
- Integration teams must not infer fork schedule from `RevisionConfig`; ORCH-3-INT-1 will wire ledger genesis separately.
