# OpStack Isthmus Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement Isthmus-era OP-Stack execution in `bcos-evm-op` with op-geth-aligned fee engine (Fjord L1 + Isthmus operator) and OP semantics (deposit tx, L1Block, preCheck, settlement).

**Architecture:** Single-tx API `opStackExecuteViaHost` orchestrates preCheck → buyGas/deposit → executeEntryChecks → `executeMessage` → `postExecuteGasSettlement` → `refundGas`. Shared eth layer gains refund counter, empty-code `HostExtension` hook, EIP-7702, `CanTransfer`. OpStack modules own fee formulas and L1Block native dispatch.

**Tech Stack:** C++20, CMake 3.28+, evmone (EVMC_PRAGUE target), Boost.Test, `bcos-evm-eth` / `bcos-evm-op`, op-geth reference (`rollup_cost.go`, `state_transition.go`).

**Spec:** `docs/superpowers/specs/2026-06-18-opstack-isthmus-design.md` (grilling Q2–Q10 closed).

## Global Constraints

- Isthmus-only: Fjord L1 + Isthmus operator fee; no Bedrock/Regolith/Ecotone/Jovian branching.
- EVM revision: `EVMC_PRAGUE` when available; `makeIsthmusRevisionConfig()` sets `eip7623=true`, `eip7702=true`, `prague_post_execution=false`.
- Q5-B: explicit `calcRefund` (EIP-3529 quotient 5) before `returnGas`; `State::get_refund()` drives settlement.
- Q10-A: `gas_left` is pre-refund; ignore `evmc_result.gas_refund` in settlement; Day-0 spike may add host shim, never change formula.
- Q2: `resolveEffectiveGasPrice = min(tipCap+baseFee, feeCap)`; balanceCheck uses `gasFeeCap`; remove input `gasPrice`.
- Hard EVM failure: always run §7.3 settlement + §7.4 fee routing (remove `OpStackTxExecutor.h:142-146` early return).
- Regression: pre-existing 15 `bcos-evm/test` CTest targets must keep passing; compat filtered suite must pass.
- Out of scope: `bcos-executor`, txpool, scheduler, receipt RLP encoding, L1 derivation.
- Test constants (op-geth): `fjordFee=3203000`, `ithmusOperatorFee=1256417826611659930` (gas=1618), `fastLz=31` for empty tx.

---

## File map (create / modify)

| File | Responsibility |
|------|----------------|
| `bcos-evm/opstack/OpStackConstants.h` | Predeploy addresses, L1Block slots, Fjord constants |
| `bcos-evm/opstack/RollupCost.h/.cpp` | `RollupCostData`, `FlzCompressLen`, `newRollupCostData` |
| `bcos-evm/opstack/OpStackFee.h/.cpp` | Fjord L1, Isthmus operator, `loadOpStackFeeParams` |
| `bcos-evm/opstack/OpStackFloorGas.h/.cpp` | `floorDataGas(calldata)` EIP-7623 |
| `bcos-evm/opstack/OpStackGasSettlement.h` | `postExecuteGasSettlement` |
| `bcos-evm/opstack/OpStackTxExecutor.cpp` | `buyGas`, `refundGas`, `refundIsthmusOperatorCost` (move from header) |
| `bcos-evm/opstack/OpStackPreCheck.h/.cpp` | Deposit / nonce / EIP-1559 / blob / EIP-7702 shape |
| `bcos-evm/opstack/OpStackDepositTx.h` | Type `0x7E` metadata |
| `bcos-evm/opstack/L1BlockStorage.h/.cpp` | Isthmus 176B payload parse |
| `bcos-evm/opstack/L1BlockPredeploy.h/.cpp` | Getter/setter dispatch `0x4200…0015` |
| `bcos-evm/opstack/OpStackExecuteViaHost.cpp` | Full orchestration (deposit + non-deposit) |
| `bcos-evm/opstack/OpStackReceiptMeta.h` | Metadata interface |
| `bcos-evm/opstack/OpHostExtension.h` | State-injected `tryChainPrecompile` |
| `bcos-evm/eth/state/State.hpp/.cpp` | `m_gasRefund`, `add_refund/get_refund/clear_refund` |
| `bcos-evm/eth/state/EthHost.cpp` | SSTORE refund accumulation; `canTransfer` on CALL |
| `bcos-evm/eth/Transfer.h` | `canTransfer`, `transfer` |
| `bcos-evm/eth/Eip7702.h/.cpp` | Delegation codec, `applyAuthorization` |
| `bcos-evm/eth/RevisionConfig.h` | `prague_post_execution`, `makeIsthmusRevisionConfig()` |
| `bcos-evm/eth/executeMessage.cpp` | Empty-code hook, `clear_refund`, EIP-7702 apply |
| `bcos-framework/.../OpStackTxType.h` | `DEPOSIT_TX_TYPE = 0x7E` |
| `bcos-evm/test/opstack/*.cpp` | Unit + integration tests |
| `bcos-evm/test/fixtures/opstack/` | Binary/json fixtures |
| `bcos-evm/test/helpers/ApplyStateDiffToView.h` | Block-ordering test helper (Stage B) |
| `bcos-evm/CMakeLists.txt` | Expand `BCOS_EVM_OP_SOURCES`, link eth sources |
| `bcos-evm/test/CMakeLists.txt` | Register new test targets |

**Build / test commands** (from repo root, adjust `BUILD` if needed):

```bash
cmake --preset <your-preset> -B BUILD && cmake --build BUILD --target RollupCostTest -j
ctest --test-dir BUILD/bcos-evm/test -R 'RollupCost' --output-on-failure
ctest --test-dir BUILD/bcos-evm/test --output-on-failure   # full suite
```

---

## Stage A — Fee engine

### Task 0: Day-0 spike (Q10 + Prague)

**Files:**
- Create: `bcos-evm/test/opstack/EvmoneRefundSpikeTest.cpp`
- Modify: `bcos-evm/test/CMakeLists.txt`

**Interfaces:**
- Produces: documented spike outcome (`DEFERRED_REFUND` vs `PRE_APPLIED_REFUND`) in test comment or `docs/superpowers/plans/2026-06-18-opstack-isthmus-spike.md`

- [ ] **Step 1: Add spike test** — SSTORE-clear contract; record `gas_left`, `gas_refund`, balance delta after tx.

```cpp
// EvmoneRefundSpikeTest.cpp — BOOST_AUTO_TEST_CASE(SstoreClear_recordsGasLeftAndRefund)
// Deploy: PUSH1 0 PUSH1 0 SSTORE STOP with slot pre-filled non-zero
// Assert: document whether gas_left excludes refund credit (Branch A) or includes it
```

- [ ] **Step 2: Run spike**

Run: `cmake --build BUILD --target EvmoneRefundSpikeTest && ./BUILD/bcos-evm/test/EvmoneRefundSpikeTest`
Expected: PASS with logged semantics; if pre-applied, note shim requirement for Task 6.

- [ ] **Step 3: Confirm EVMC_PRAGUE** — build with `revision = EVMC_PRAGUE` in spike; fallback note if unavailable.

- [ ] **Step 4: Commit**

```bash
git add bcos-evm/test/opstack/EvmoneRefundSpikeTest.cpp bcos-evm/test/CMakeLists.txt
git commit -m "test(opstack): Day-0 evmone refund + Prague spike"
```

---

### Task 1: OpStackConstants + RollupCost (FastLz)

**Files:**
- Create: `bcos-evm/opstack/OpStackConstants.h`
- Create: `bcos-evm/opstack/RollupCost.h`, `bcos-evm/opstack/RollupCost.cpp`
- Create: `bcos-evm/test/opstack/RollupCostTest.cpp`
- Modify: `bcos-evm/CMakeLists.txt` (`BCOS_EVM_OP_SOURCES`)

**Interfaces:**
- Produces: `RollupCostData`, `newRollupCostData(bytesConstRef)`, `flzCompressLen(bytesConstRef) -> uint64_t`

- [ ] **Step 1: Write failing FlzCompressLen test**

```cpp
BOOST_AUTO_TEST_CASE(FlzCompressLen_matchesOpGethVectors)
{
    // Port 5 vectors from op-geth TestFlzCompressLen
    BOOST_CHECK_EQUAL(flzCompressLen(ref), expected);
}
```

- [ ] **Step 2: Run test** — Expected: FAIL (link error / undefined)

- [ ] **Step 3: Implement `RollupCost.cpp`** — port `FlzCompressLen` verbatim from `op-geth/core/types/rollup_cost.go:667+`; `newRollupCostData` counts zeroes/ones + fastLz.

- [ ] **Step 4: Run test** — Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add bcos-evm/opstack/OpStackConstants.h bcos-evm/opstack/RollupCost.* bcos-evm/test/opstack/RollupCostTest.cpp bcos-evm/CMakeLists.txt bcos-evm/test/CMakeLists.txt
git commit -m "feat(opstack): add RollupCost and FastLz port"
```

---

### Task 2: OpStackFee (Fjord L1 + Isthmus operator)

**Files:**
- Create: `bcos-evm/opstack/OpStackFee.h`, `bcos-evm/opstack/OpStackFee.cpp`
- Create: `bcos-evm/test/fixtures/opstack/empty_tx.bin` (generate from op-geth `emptyTx`)
- Modify: `bcos-evm/test/opstack/OpStackFeeTest.cpp`

**Interfaces:**
- Consumes: `RollupCostData`, `OpStackFeeParams`, slot constants from `OpStackConstants.h`
- Produces: `l1CostFjord(...) -> u256`, `operatorCostIsthmus(gas, params) -> u256`, `loadOpStackFeeParams(StateView const&)`

- [ ] **Step 1: Write failing Fjord + operator tests**

```cpp
BOOST_AUTO_TEST_CASE(FjordL1_emptyTx_matches3203000)
{
    auto data = newRollupCostData(loadFixture("empty_tx.bin"));
    BOOST_CHECK_EQUAL(data.fastLzSize, 31u);
    auto cost = l1CostFjord(data, params);
    BOOST_CHECK_EQUAL(cost, u256(3'203'000));
}
BOOST_AUTO_TEST_CASE(IsthmusOperator_gas1618_matchesFixture)
{
    BOOST_CHECK_EQUAL(operatorCostIsthmus(1618, params), u256("1256417826611659930"));
}
```

- [ ] **Step 2: Run tests** — Expected: FAIL

- [ ] **Step 3: Implement `OpStackFee.cpp`** per spec §6.1–6.2 formulas; slot 3/8 unpack.

- [ ] **Step 4: Run tests** — Expected: PASS

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(opstack): Fjord L1 and Isthmus operator fee formulas"
```

---

### Task 3: OpStackFloorGas (EIP-7623)

**Files:**
- Create: `bcos-evm/opstack/OpStackFloorGas.h`, `bcos-evm/opstack/OpStackFloorGas.cpp`
- Create: `bcos-evm/test/opstack/FloorDataGasTest.cpp`

**Interfaces:**
- Produces: `floorDataGas(bytesConstRef data) -> uint64_t` (TxGas=21000, token=4/10)

- [ ] **Step 1: Write failing floor formula test**

```cpp
BOOST_AUTO_TEST_CASE(FloorDataGas_emptyCalldata_is21000)
{
    BOOST_CHECK_EQUAL(floorDataGas({}), 21'000u);
}
```

- [ ] **Step 2: Implement `floorDataGas`** mirroring `state_transition.go:120-133` overflow check.

- [ ] **Step 3: Add execute-entry reject test** (wired in Task 8; stub `executeEntryChecks` here).

- [ ] **Step 4: Commit**

```bash
git commit -m "feat(opstack): EIP-7623 FloorDataGas"
```

---

### Task 4: State refund counter

**Files:**
- Modify: `bcos-evm/eth/state/State.hpp`, `bcos-evm/eth/state/State.cpp`
- Create: `bcos-evm/test/state/StateRefundTest.cpp`

**Interfaces:**
- Produces: `void add_refund(uint64_t)`, `uint64_t get_refund() const`, `void clear_refund()`, reset on `checkpoint()`/`revert()`

- [ ] **Step 1: Write failing refund journal test**

```cpp
BOOST_AUTO_TEST_CASE(Refund_clearedOnRevert)
{
    State s(view);
    s.checkpoint();
    s.add_refund(4800);
    s.revert();
    BOOST_CHECK_EQUAL(s.get_refund(), 0u);
}
```

- [ ] **Step 2: Implement `m_gasRefund` in State journal**

- [ ] **Step 3: Run `StateRefundTest` + full `StateJournalRevert` regression**

- [ ] **Step 4: Commit**

```bash
git commit -m "feat(eth): State gas refund counter with journal reset"
```

---

### Task 5: EthHost SSTORE refund accumulation

**Files:**
- Modify: `bcos-evm/eth/state/EthHost.cpp`
- Modify: `bcos-evm/eth/executeMessage.cpp` (call `state.clear_refund()` at tx entry)
- Extend: `bcos-evm/test/opstack/CalcRefundTest.cpp` (or `state/SstoreRefundTest.cpp`)

**Interfaces:**
- Consumes: `State::add_refund`, `classifyStorageStatus` → EIP-3529 refund amounts per geth `operations_acl.go`

- [ ] **Step 1: Write failing SSTORE-clear refund test**

```cpp
BOOST_AUTO_TEST_CASE(EthHost_sstoreClear_accumulates4800)
{
    // non-zero slot → zero; after executeMessage
    BOOST_CHECK_EQUAL(state.get_refund(), 4800u);
}
```

- [ ] **Step 2: Map `evmc_storage_status` → `add_refund` in `EthHost::set_storage`**

- [ ] **Step 3: Add `clear_refund()` at `executeMessage` entry (sole call site)**

- [ ] **Step 4: Run compat tests** — `ctest -R 'CompatExecuteViaHost|SstoreStatus'`

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(eth): EthHost accumulates SSTORE refunds into State"
```

---

### Task 6: OpStackGasSettlement + OpStackTxExecutor rewrite

**Files:**
- Create: `bcos-evm/opstack/OpStackGasSettlement.h`
- Create: `bcos-evm/opstack/OpStackTxExecutor.cpp`
- Modify: `bcos-evm/opstack/OpStackTxExecutor.h` (slim interface; remove early return L142-146)
- Create: `bcos-evm/test/opstack/CalcRefundTest.cpp`, `RefundIsthmusTest.cpp`

**Interfaces:**
- Produces: `GasSettlement postExecuteGasSettlement(gasLimit, gasLeft, stateRefund, floorDataGas)`
- Produces: `resolveEffectiveGasPrice(gasTipCap, gasFeeCap, baseFee) -> u256`
- Produces: `refundIsthmusOperatorCost`, `refundGas` (4-way routing to 0x19/0x1A/0x1B + coinbase)

- [ ] **Step 1: Pure formula tests**

```cpp
BOOST_AUTO_TEST_CASE(Settlement_capBinds)
{
    auto s = postExecuteGasSettlement(100'000, 80'000, 50'000, 0);
    BOOST_CHECK_EQUAL(s.gasUsed, 76'000u);  // refund min(50000, 4000)=4000
}
```

- [ ] **Step 2: Implement settlement + remove hard-failure early return**

- [ ] **Step 3: Implement dual-track `buyGas`** (effectiveGasPrice vs gasFeeCap balanceCheck per §7.2)

- [ ] **Step 4: `RefundIsthmusTest`** — limit 1618, used 500 delta.

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(opstack): gas settlement, calcRefund, buyGas dual-track"
```

---

### Task 7: OpStackExecuteViaHost wiring (non-deposit Stage A)

**Files:**
- Modify: `bcos-evm/opstack/OpStackExecuteViaHost.cpp`, `OpStackExecuteViaHost.h`
- Modify: `bcos-evm/test/opstack/OpStackExecuteViaHostSmokeTest.cpp`
- Create: `bcos-evm/test/opstack/OpStackSettlementTest.cpp`

**Interfaces:**
- Consumes: all Task 1–6 outputs
- Produces: `opStackExecuteViaHost(input) -> OpStackExecuteViaHostOutput` non-deposit path complete

- [ ] **Step 1: Replace `gasPrice` input with `gasFeeCap`/`gasTipCap` in `OpStackExecuteViaHostInput`**

- [ ] **Step 2: Wire flow** §9 steps 5a–5g: `loadOpStackFeeParams` → `buyGas` → `executeEntryChecks` → `executeMessage` → `postExecuteGasSettlement` → `refundGas`

- [ ] **Step 3: Remove `m_l1CostFunc` mock** from smoke; use real `l1CostFjord`

- [ ] **Step 4: `OpStackSettlementTest`** — 4-way routing balances; hard failure still refunds.

- [ ] **Step 5: `EvmoneParity_noDoubleCount`** in `CalcRefundTest` (Q10-A gate)

- [ ] **Step 6: Stage A merge gate**

Run: `ctest --test-dir BUILD/bcos-evm/test -R 'RollupCost|OpStackFee|RefundIsthmus|CalcRefund|FloorDataGas|OpStackSettlement|OpStackExecuteViaHost'`
Plus: pre-existing 15 tests PASS.

- [ ] **Step 7: Commit + freeze input API v1**

```bash
git commit -m "feat(opstack): wire non-deposit execute path; Stage A fee engine"
```

---

## Stage B — OP semantics

### Task 8: OpStackPreCheck + DepositTx types

**Files:**
- Create: `bcos-evm/opstack/OpStackDepositTx.h`, `OpStackPreCheck.h/.cpp`
- Create: `bcos-framework/bcos-framework/executor/OpStackTxType.h`
- Create: `bcos-evm/test/opstack/DepositTxPreCheckTest.cpp`

**Interfaces:**
- Produces: `isDepositTx(input) -> bool` derived from type `0x7E`
- Produces: `opStackPreCheck(input, state) -> expected<unit, Error>`

- [ ] **Step 1: System deposit rejected test**

- [ ] **Step 2: Implement deposit path preCheck** (gas pool hook, no balance/nonce)

- [ ] **Step 3: Non-deposit nonce + EIP-1559 + blob checks**

- [ ] **Step 4: Commit**

```bash
git commit -m "feat(opstack): OpStackPreCheck and deposit tx type"
```

---

### Task 9: Deposit execution path

**Files:**
- Modify: `bcos-evm/opstack/OpStackExecuteViaHost.cpp`
- Create: `bcos-evm/test/opstack/DepositMintTest.cpp`, `DepositNoFeeRoutingTest.cpp`

**Interfaces:**
- Implements §7.6: mint before checkpoint, checkpoint, executeMessage, failure revert+nonce++, success settlement without §7.4

- [ ] **Step 1: `DepositMintTest`** — mint credited before EVM

- [ ] **Step 2: Deposit branch in `opStackExecuteViaHost`** §9 step 4

- [ ] **Step 3: `DepositNoFeeRoutingTest`** — no credits to 0x19/0x1A/0x1B/coinbase

- [ ] **Step 4: Commit**

```bash
git commit -m "feat(opstack): deposit execution path"
```

---

### Task 10: L1BlockPredeploy + storage

**Files:**
- Create: `bcos-evm/opstack/L1BlockStorage.h/.cpp`, `L1BlockPredeploy.h/.cpp`
- Create: `bcos-evm/test/fixtures/opstack/isthmus_l1_attributes.bin` (176 bytes)
- Modify: `bcos-evm/opstack/OpHostExtension.h` (inject `State*`, dispatch)

**Interfaces:**
- Produces: `L1BlockPredeploy::dispatch(State&, evmc_message const&) -> optional<evmc_result>`
- Produces: `applySetterIsthmus(calldata)` writes slots 1/3/7/8

- [ ] **Step 1: Unit test setter unpack** — 176-byte fixture → expected slot values

- [ ] **Step 2: Implement getter selectors + setter `0x098999be`**

- [ ] **Step 3: Depositor-only guard on setter**

- [ ] **Step 4: Commit**

```bash
git commit -m "feat(opstack): L1BlockPredeploy native dispatch"
```

---

### Task 11: executeMessage empty-code hook (§8.4)

**Files:**
- Modify: `bcos-evm/eth/executeMessage.cpp`
- Create: `bcos-evm/test/opstack/EmptyCodeHookTest.cpp`

**Interfaces:**
- Consumes: `HostExtension::tryChainPrecompile`
- Behavior: `checkpoint → tryChainPrecompile | SUCCESS → commit/revert` (never unconditional commit with outer checkpoint)

- [x] **Step 1: `EmptyCodeHookTest`** — top-level CALL `0x4200…0015` + `0x098999be` hits predeploy, not empty SUCCESS

- [x] **Step 2: Replace empty-code shortcut** per spec §8.4 pseudocode

- [x] **Step 3: `L1AttributesDepositTest` skeleton** (full in Task 14)

- [ ] **Step 4: Commit**

```bash
git commit -m "feat(eth): empty-code HostExtension hook for L1Block"
```

---

### Task 12: EIP-7702

**Files:**
- Create: `bcos-evm/eth/Eip7702.h/.cpp`
- Modify: `bcos-evm/eth/executeMessage.cpp`, `OpStackPreCheck.cpp`
- Create: `bcos-evm/test/opstack/Eip7702PreCheckTest.cpp`, `Eip7702ApplyAuthorizationTest.cpp`

- [ ] **Step 1: Delegation codec** — `parseDelegationTarget`, `addressToDelegation`

- [ ] **Step 2: `applyAuthorization`** — ignore invalid auths; existence refund 12500

- [ ] **Step 3: Wire before CALL in `executeMessage` non-create path**

- [ ] **Step 4: PreCheck auth list shape tests**

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(eth): EIP-7702 authorization application"
```

---

### Task 13: CanTransfer + RevisionConfig preset

**Files:**
- Create: `bcos-evm/eth/Transfer.h`
- Modify: `bcos-evm/eth/RevisionConfig.h`, `EthHost.cpp`, `OpStackExecuteViaHost.cpp` (`executeEntryChecks`)
- Create: `bcos-evm/test/opstack/CanTransferTest.cpp`, `IsthmusPostExecutionPolicyTest.cpp`

- [ ] **Step 1: `canTransfer` + execute-entry steps 6–7**

- [ ] **Step 2: `EthHost::call` value guard**

- [ ] **Step 3: `makeIsthmusRevisionConfig()`** with `prague_post_execution=false`

- [ ] **Step 4: Tests** — insufficient value; predeploy transfer allowed

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(eth): CanTransfer and Isthmus RevisionConfig preset"
```

---

### Task 14: Stage B integration tests + fixtures

**Files:**
- Create: `bcos-evm/test/helpers/ApplyStateDiffToView.h`
- Create: remaining §11.2 tests + fixtures dir
- Modify: `bcos-evm/test/CMakeLists.txt`

**Tests to implement (15):**
`L1AttributesDepositTest`, `L1AttributesDepositFailureTest`, `GasFeeCapBalanceTest`, `BlobGasBalanceTest`, `L1BlockGetterTest`, (+ all from Tasks 8–13)

- [ ] **Step 1: `applyStateDiffToView` helper**

- [ ] **Step 2: Block-ordering tests** — L1 attrs → user tx L1 cost

- [ ] **Step 3: L1 attrs failure reverts slots**

- [ ] **Step 4: Register all test targets in CMake**

- [ ] **Step 5: Stage B merge gate** — all 15 + Stage A tests + 15 baseline

- [ ] **Step 6: Commit**

```bash
git commit -m "test(opstack): Stage B integration tests and fixtures"
```

---

### Task 15: Receipt metadata + final polish

**Files:**
- Create: `bcos-evm/opstack/OpStackReceiptMeta.h`
- Modify: `OpStackExecuteViaHostOutput`, smoke tests

- [ ] **Step 1: Populate `receiptMeta.l1Fee`, `operatorFee`, `depositNonce`**

- [ ] **Step 2: Extend smoke** — coinbase tip, baseFee 0x19, operator refund, hard-failure gas

- [ ] **Step 3: Update spec status to Implemented**

- [ ] **Step 4: Full regression**

Run: `ctest --test-dir BUILD/bcos-evm/test --output-on-failure`

- [ ] **Step 5: Commit**

```bash
git commit -m "feat(opstack): receipt metadata and smoke test completion"
```

---

## Spec coverage checklist

| Spec section | Task |
|--------------|------|
| §5.1 RollupCostData | Task 1 |
| §6.1–6.2 Fee formulas | Task 2 |
| §6.4 FloorDataGas | Task 3 |
| §5.9 calcRefund / State counter | Tasks 4–6 |
| §5.8 effectiveGasPrice | Task 6–7 |
| §7.3–7.4 settlement / refundGas | Tasks 6–7 |
| §7.6 deposit | Tasks 8–9 |
| §8 L1Block | Tasks 10–11 |
| §5.10 EIP-7702 | Task 12 |
| §5.12 CanTransfer | Task 13 |
| §5.11 postExecution policy | Task 13 |
| §11 all tests | Tasks 1–15 |
| §5.7 receipt meta | Task 15 |

## Self-review notes

- Placeholder scan: all tasks have concrete files and test entry points.
- Q10 shim: if Task 0 finds pre-applied refunds, add sub-step in Task 5 to disable duplicate `add_refund` for settlement (formula unchanged).
- Duplicate success criterion #9 in spec (two item 9s) — implement once in Task 8/9; no plan duplication.
- `bcos-evm-op` must link `bcos-evm-eth` after eth changes — update `bcos-evm/CMakeLists.txt` `target_link_libraries(bcos-evm-op PUBLIC bcos-evm-eth)` when wiring Task 7.

---

**Plan complete and saved to `docs/superpowers/plans/2026-06-18-opstack-isthmus.md`. Two execution options:**

1. **Subagent-Driven (recommended)** — dispatch a fresh subagent per task, review between tasks, fast iteration
2. **Inline Execution** — execute tasks in this session using executing-plans, batch execution with checkpoints

**Which approach?**
