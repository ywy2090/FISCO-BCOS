# OpStack Isthmus Design

**Status:** Implemented (Stage A + Stage B) → implementation plan `docs/superpowers/plans/2026-06-18-opstack-isthmus.md`  
**Date:** 2026-06-18  
**Review:** Round 1 + Round 2 multi-agent review; grilling closed 2026-06-18 (Q2–Q10)  
**Scope:** `bcos-evm/opstack` + minimal `bcos-protocol` / `bcos-framework` extensions  
**Reference:** `op-geth` (`core/types/rollup_cost.go`, `core/types/deposit_tx.go`, `core/state_transition.go`)  
**Prerequisite:** Layer Refactor Step 3–4 complete (`OpStackExecuteViaHost`, `bcos-evm-op` library)  
**Out of scope:** `bcos-executor` / txpool / scheduler wiring (Integration phase), L1 derivation / batch submission, Jovian+ forks

---

## 1. Goal

Implement Isthmus-era OP-Stack execution inside `bcos-evm-op` so that transaction semantics match op-geth at Isthmus, assuming the chain is **always post-Isthmus / post-Regolith** (no Bedrock/Regolith/Ecotone fork branching).

This spec covers two implementation stages delivered as one unit:

| Stage | Focus |
|-------|-------|
| **A — Fee engine** | Fjord L1 cost, Isthmus operator fee, settlement routing |
| **B — OP semantics** | Deposit tx, L1Block storage, preCheck, L1Block getter dispatch |

### Success criteria

**Fee engine (Stage A)**

1. `L1CostFuncFjord` and `OperatorCostFuncIsthmus` match op-geth `rollup_cost_test.go` canonical empty-tx fixture.
2. `refundIsthmusOperatorCost` refunds `(operatorCost(gasLimit) - operatorCost(gasUsed))` to sender.
3. Fee routing credits `0x4200…0019` (base fee), `0x4200…001A` (L1), `0x4200…001B` (operator), and coinbase (tip).
4. Unused gas returned to sender on SUCCESS, REVERT, and hard EVM failure.
5. `calcRefund` (EIP-3529, quotient 5) applied before `returnGas`; `gasUsed` for fee routing reflects post-refund settlement (op-geth `state_transition.go:644–664`).
6. EIP-7623 `FloorDataGas`: execute-entry `gasLimit` check + post-refund floor bump matches op-geth (`state_transition.go:546–554, 650–661`).
7. evmone refund settlement uses **Branch A** (§5.9): `calcRefund` from `State` counter; `gas_left` is pre-refund; Day-0 spike must pass `EvmoneParity_noDoubleCount`.

**OP semantics (Stage B)**

8. `DepositTx` (type `0x7E`) recognized at execution boundary; `isDepositTx` derived from tx type.
9. Deposit preCheck: no fee/nonce check; gas pool hook exposed; system tx rejected.
9. Deposit execution: Mint credited, EVM runs, post-execution skips all fee routing.
10. L1 attributes deposit writes L1Block slots; subsequent user txs read correct fee params.
11. `OpHostExtension` dispatches L1Block read-only getter calls.
12. Non-deposit txs: GasFeeCap balance check and blob gas check in buyGas.
13. EIP-7702: auth list preCheck, delegation EOA sender allowed, `applyAuthorization` before CALL, existence refund (`state_transition.go:451–459, 604–609, 778–802`).
14. EIP-6110/7002/7251 block postExecution **not invoked** at Isthmus (`state_processor.go:141`).
15. `CanTransfer`: sender balance covers `tx.value` at execute entry; nested CALL value transfers use same rule (`core/evm.go:142–146`).

**Regression**

16. `bcos-evm/test` 15/15, filtered compat set pass, OpStack smoke tests pass.

---

## 2. Isthmus-only assumption

| Component | Isthmus behavior |
|-----------|------------------|
| L1 data fee | **Fjord** (`NewL1CostFuncFjord`) — FastLz linear regression |
| Operator fee | **Isthmus** (`newOperatorCostFuncIsthmus`) — `gas × scalar / 1e6 + constant` |
| EVM revision | **Prague** (bound to Isthmus) |
| Deposit tx | post-Regolith: gas free, no fee routing, system tx rejected |
| L1 attributes | Isthmus selector `0x098999be`, **176-byte** payload (`IsthmusL1AttributesLen`; Jovian=178, out of scope) |
| Prague postExecution | EIP-6110/7002/7251 **disabled** when Isthmus active (`state_processor.go:141`) |
| EIP-7623 floor gas | **Enabled** (`PragueTime = IsthmusTime`; `RevisionConfig.eip7623 = true`) |
| EIP-7702 delegations | **Enabled** at Prague (`RevisionConfig.eip7702 = true`) |

**Excluded:**

- Bedrock / Regolith / Ecotone L1 cost functions and fork selectors
- Jovian `newOperatorCostFuncOperatorFeeFix`
- First-Ecotone-block Bedrock fallback
- pre-Regolith deposit behavior
- `bcos-executor` integration (separate Integration spec)
- Receipt full encoding (interface only; encoding in Integration phase)

### 2.1 Resolved design decisions (grilling)

| # | Topic | Decision |
|---|-------|----------|
| Q2 | `effectiveGasPrice` | op-geth dual-track: `resolveEffectiveGasPrice = min(tipCap+baseFee, feeCap)`; balanceCheck uses `gasFeeCap`; remove input `gasPrice` → §5.8 |
| Q3 | L1 attrs failure rollback | Mint before checkpoint; `state.checkpoint()` → `executeMessage`; failure `revert()` + nonce++; L1 slots written inside EVM → §7.6 |
| Q4 | L1Block top-level CALL | **D**: `executeMessage` empty-code hook → `extension->tryChainPrecompile` → §8.4 |
| Q5 | `gasUsed` / `calcRefund` | **B**: explicit `calcRefund` port; State refund counter + post-execute settlement before `returnGas` → §5.9, §7.3 |
| Q6 | EIP-7623 floor gas | **A**: align op-geth — `FloorDataGas` at execute entry + post-`calcRefund` floor bump → §6.4, §7.1.1, §7.3 |
| Q7 | EIP-7702 authorizations | **A**: align op-geth — preCheck auth list + `applyAuthorization` before CALL + existence refund → §5.10, §7.1, §7.8 |
| Q8 | EIP-6110/7002/7251 postExecution | **A**: align op-geth — disabled at Isthmus (`IsPrague && !IsIsthmus`); `praguePostExecution=false` in preset → §5.11 |
| Q9 | `CanTransfer` / tx value | **A**: align op-geth — `GetBalance(from) >= value` at execute entry + EVM CALL path → §5.12, §7.1.1 |
| Q10 | evmone refund semantics | **A**: **Branch A normative** — `gas_left` pre-refund; `State` counter drives `calcRefund`; Day-0 spike validates + host shim if double-count → §5.9 |

**Grilling closed:** Q2–Q10 all resolved. Spec ready for user approval → implementation plan.

## 3. Architecture

```
OpStackExecuteViaHostInput
  │
  ├─ opStackPreCheck()                 → deposit / system / nonce / eip1559 / blob
  │
  ├─ [deposit path]
  │     ├─ applyMint (if present)          ← before checkpoint (op-geth:474–480)
  │     ├─ state.checkpoint()              ← op-geth: snap := Snapshot()
  │     ├─ executeEntryChecks()            ← intrinsic + FloorDataGas (§7.1.1)
  │     └─ executeMessage (no buyGas)      ← L1 attrs written HERE via L1Block CALL
  │           └─ on failure: state.revert() + nonce++ + gasPool return
  │
  └─ [non-deposit path]
        ├─ newRollupCostData(txBytes)
        ├─ loadOpStackFeeParams(stateView)   ← reads L1Block slots
        ├─ OpStackTxExecutor::buyGas()
        ├─ executeEntryChecks()                ← intrinsic + FloorDataGas (§7.1.1)
        ├─ executeMessage()                  ← Prague, OpHostExtension
        ├─ postExecuteGasSettlement()        ← calcRefund + EIP-7623 floor (§7.3)
        └─ OpStackTxExecutor::refundGas()
              ├─ return unused gas to sender (uses post-refund gasRemaining)
              ├─ coinbase += tip
              ├─ baseFeeRecipient += baseFee
              ├─ l1FeeRecipient += l1Cost
              ├─ refundIsthmusOperatorCost()
              └─ operatorFeeRecipient += operatorCost(gasUsed)
```

### Module layout

```
bcos-evm/opstack/
├── OpStackConstants.h         # predeploy addresses, L1Block slots, fee constants
├── RollupCost.h/.cpp          # RollupCostData, FastLz, newRollupCostData
├── OpStackFee.h/.cpp          # Fjord L1 + Isthmus operator + loadFeeParams
├── OpStackTxExecutor.h        # buyGas / refundGas / refundIsthmusOperatorCost
├── OpStackDepositTx.h         # DepositTx type 0x7E metadata
├── OpStackPreCheck.h/.cpp     # deposit / nonce / eip1559 / blob preCheck
├── OpStackFloorGas.h/.cpp     # FloorDataGas (EIP-7623), execute-entry check
├── L1BlockStorage.h/.cpp      # payload parse helpers (shared by setter)
├── L1BlockPredeploy.h/.cpp    # getter + setter dispatch for 0x4200…0015
├── OpHostExtension.h          # State-injected; tryChainPrecompile → L1BlockPredeploy
├── OpStackTxExecutor.cpp      # buyGas/refundGas impl (move out of header)
├── OpStackExecuteViaHost.h/.cpp
└── OpStackReceiptMeta.h       # receipt metadata interface (encoding deferred)

bcos-framework/ (minimal)
└── executor/OpStackTxType.h   # DEPOSIT_TX_TYPE = 0x7E
```

### CMake

Add to `BCOS_EVM_OP_SOURCES`: `RollupCost.cpp`, `OpStackFee.cpp`, `OpStackPreCheck.cpp`, `OpStackFloorGas.cpp`, `L1BlockStorage.cpp`, `L1BlockPredeploy.cpp`.

---

## 4. L1Block storage and block ordering

### 4.1 Consensus-critical ordering

op-geth defers reading L1Block fee parameters until transaction execution time so that the **first deposit tx in each block** (L1 attributes) can write slots before user txs are charged.

```
Block execution order:
  1. L1 attributes deposit  → L1Block setter via EVM/native dispatch (§8.4)
  2. User deposit txs
  3. User L2 txs            → loadOpStackFeeParams() reads updated slots
```

### 4.2 Test strategy for storage initialization

| Test layer | What it proves | How |
|------------|----------------|-----|
| **Unit** (Stage A) | Fee formulas correct | Inject slots directly into `InMemoryStateView`; compare against op-geth constants |
| **Integration** (Stage B) | Block ordering correct | `L1AttributesDepositTest`: apply attributes → execute user tx → verify L1 cost matches |
| **Standalone** | End-to-end without executor | `OpStackExecuteViaHost` accepts ordered tx sequence in one test harness |

Stage A unit tests use manual slot injection — this is intentional: they validate pure fee math. Block-ordering correctness is validated in Stage B integration tests, not deferred to Integration phase.

### 4.3 Caller responsibility (single-tx API boundary)

`opStackExecuteViaHost` is a **single-transaction API**. It does NOT reorder block txs. The Integration-phase caller (executor/sealer) MUST:

1. Execute L1 attributes deposit as the **first tx** in each block.
2. Persist `stateDiff` from tx N into `StateView` before executing tx N+1.

Stage B tests use `applyStateDiffToView(stateDiff, stateView)` helper (test infra) to simulate block ordering without executor wiring.

---

## 5. Data structures

### 5.1 RollupCostData

```cpp
struct RollupCostData {
    uint64_t zeroes{0};
    uint64_t ones{0};
    uint64_t fastLzSize{0};
    bool isEmpty() const noexcept;
};
RollupCostData newRollupCostData(bcos::bytesConstRef serializedTx);
```

### 5.2 OpStackConstants

```cpp
// Predeploy fee recipients (params/protocol_params.go)
OP_BASE_FEE_RECIPIENT     = 0x4200000000000000000000000000000000000019
OP_L1_FEE_RECIPIENT       = 0x420000000000000000000000000000000000001A
OP_OPERATOR_FEE_RECIPIENT = 0x420000000000000000000000000000000000001B
OP_L1_BLOCK_PREDEPLOY     = 0x4200000000000000000000000000000000000015

// L1Block storage slots
L1_BASE_FEE_SLOT          = u256(1)
L1_FEE_SCALARS_SLOT       = u256(3)
L1_BLOB_BASE_FEE_SLOT     = u256(7)
OPERATOR_FEE_PARAMS_SLOT  = u256(8)

// Fjord constants
L1_COST_INTERCEPT         = -42'585'600
L1_COST_FASTLZ_COEF       = 836'500
MIN_TX_SIZE_SCALED        = 100'000'000
FJORD_DIVISOR             = 1'000'000'000'000
```

### 5.3 OpStackFeeParams

```cpp
struct OpStackFeeParams {
    u256 l1BaseFee;
    u256 l1BlobBaseFee;
    u256 l1BaseFeeScalar;      // slot 3, bytes [16:20)
    u256 l1BlobBaseFeeScalar;  // slot 3, bytes [20:24)
    u256 operatorFeeScalar;    // slot 8, bytes [20:24)
    u256 operatorFeeConstant;  // slot 8, bytes [24:32)
};
OpStackFeeParams loadOpStackFeeParams(state::StateView const& state);
```

### 5.4 OpStackDepositTx

```cpp
inline constexpr uint8_t DEPOSIT_TX_TYPE = 0x7E;

struct OpStackDepositTx {
    bcos::h256 sourceHash;
    evmc_address from;
    std::optional<evmc_address> to;
    std::optional<bcos::u256> mint;
    bcos::u256 value;
    uint64_t gas{0};
    bool isSystemTransaction{false};
    bcos::bytes data;
};
```

`isDepositTx` is **derived**: `web3TypedTxKind == DEPOSIT_TX_TYPE` or `depositTx.has_value()`. The manual `bool isDepositTx` input field is removed.

### 5.5 OpStackTxExecutionData

```cpp
u256 m_l1CostCharged{0};
u256 m_operatorCostLimit{0};
u256 m_effectiveGasPrice{0};
u256 m_baseFee{0};
uint64_t m_floorDataGas{0};   // cached at execute entry (§7.1.1)
evmc_address m_coinbase{};
```

### 5.6 OpStackExecuteViaHostInput

```cpp
struct OpStackExecuteViaHostInput {
    // ...
    bcos::u256 gasFeeCap{0};      // tx intrinsic: maxFeePerGas (EIP-1559)
    bcos::u256 gasTipCap{0};      // tx intrinsic: maxPriorityFeePerGas
    // gasPrice is NOT caller-supplied for real txs — resolved internally (§5.8)
    bool noBaseFee{false};        // eth_call sim: mirrors evm.Config.NoBaseFee
    bool call{false};
    bool skipNonceChecks{false};
    bool skipTransactionChecks{false};
    std::vector<SetCodeAuthorization> authorizations;  // EIP-7702 (§5.10); empty = none
    // ...
};
```

Remove standalone `gasPrice` from input. Callers pass `gasFeeCap` + `gasTipCap` (and legacy txs set both equal to legacy gasPrice). Integration adapter maps protocol tx fields; tests may set caps directly.

### 5.10 EIP-7702 SetCodeAuthorization (Q7: A)

Reference: `types.SetCodeAuthorization` (op-geth), `state_transition.go:747–802`.

```cpp
struct SetCodeAuthorization {
    std::optional<bcos::u256> chainId;  // zero/null = any chain
    evmc_address address{};             // delegation target; zero = clear delegation
    uint64_t nonce{0};
    // yParity, r, s signature fields — recover authority via ECDSA
};

bool isDelegationCode(bcos::bytesConstRef code);           // types.ParseDelegation
bcos::bytes addressToDelegation(evmc_address const& addr); // types.AddressToDelegation
std::optional<evmc_address> parseDelegationTarget(bcos::bytesConstRef code);
```

**preCheck** (non-deposit, `state_transition.go:451–459`):

| Rule | Error |
|------|-------|
| If `authorizations` non-empty → `to` must be set (no create) | `ErrSetCodeTxCreate` |
| If `authorizations` present → list must be non-empty | `ErrEmptyAuthList` |

Deposit txs do not carry authorization lists (type `0x7E`).

**EOA sender check** (`state_transition.go:384–389`): sender may have code **only** if it is an existing EIP-7702 delegation (`parseDelegationTarget` succeeds).

**Intrinsic gas:** each auth tuple adds `TxAuthTupleGas` (12500) to intrinsic gas (`IntrinsicGas` L72–115).

**applyAuthorization** (eth layer, before top-level CALL in `executeMessage`):

| Step | Action |
|------|--------|
| 1 | `validateAuthorization`: chainId, nonce overflow, recover authority, access-list warm authority |
| 2 | Authority must have empty code **or** existing delegation |
| 3 | Authority nonce must match `auth.nonce` |
| 4 | On success: if authority exists → `state.add_refund(CallNewAccountGas - TxAuthTupleGas)` (25000 − 12500) |
| 5 | `SetNonce(authority, nonce+1)`; set delegation code or clear if `address == 0` |

**Invalid auths:** errors **ignored** per tuple (op-geth L607–608); continue to next auth.

**Delegation target warming** (`state_transition.go:617–618`): after applying auths, if `to` has delegation code, warm the delegation target in access list before CALL.

**Implementation location:** `bcos-evm/eth/Eip7702.h/.cpp`; invoked from `executeMessage` non-create path after sender nonce increment, before `vm->execute`.

### 5.11 Prague postExecution policy (Q8: A)

op-geth block processor calls `postExecution` only when **`IsPrague && !IsIsthmus`** (`state_processor.go:141`):

```go
if config.IsPrague(block.Number(), block.Time()) && !config.IsIsthmus(block.Time()) {
    // EIP-6110 ParseDepositLogs
    // EIP-7002 ProcessWithdrawalQueue
    // EIP-7251 ProcessConsolidationQueue
}
```

At Isthmus, `postExecution` returns **empty requests** — no deposit log parsing, no withdrawal/consolidation queue system calls.

**`RevisionConfig` extension** (`bcos-evm/eth/RevisionConfig.h`):

```cpp
bool prague_post_execution : 1 = false;  // EIP-6110/7002/7251 block-level requests
```

**`makeIsthmusRevisionConfig()`** sets `prague_post_execution = false` explicitly (Isthmus always).

**Single-tx API boundary:** `opStackExecuteViaHost` does **not** invoke block-level `postExecution` (per-tx only). This spec enforces the policy via preset + documentation; Integration-phase block builder MUST skip `postExecution` when chain is Isthmus.

**Intentional diff registry:** If a future non-Isthmus Prague testnet is added, only then set `prague_post_execution = true`.

### 5.12 `CanTransfer` and value transfer (Q9: A)

Reference: `core/evm.go:142–146`, `state_transition.go:573–580`, `vm/evm.go:271/368/523`.

op-geth `CanTransfer` is a **balance sufficiency check** for the transfer amount — not a predeploy deny-list:

```go
func CanTransfer(db vm.StateDB, addr common.Address, amount *uint256.Int) bool {
    return db.GetBalance(addr).Cmp(amount) >= 0
}
```

**No OP predeploy special case:** sending ETH to `0x4200…0015/0019/001A/001B` is allowed when the sender has sufficient balance (same as any address). Fee routing credits those addresses via `AddBalance`, independent of user `tx.value`.

**bcos port** (`bcos-evm/eth/Transfer.h`):

```cpp
bool canTransfer(state::StateView const& state, evmc_address const& from, bcos::u256 const& value);
void transfer(state::State& state, evmc_address const& from, evmc_address const& to, bcos::u256 const& value);
```

| Location | When | Reference |
|----------|------|-----------|
| Execute entry (§7.1.1) | `value != 0` → `canTransfer(from, value)` else `ErrInsufficientFundsForTransfer` | `innerExecute` clause 6 |
| Value overflow | `value` must fit `u256` | `state_transition.go:574–576` |
| `EthHost::call` / nested CALL | Non-zero `msg.value` → `canTransfer(caller, value)` before debit | `vm/evm.go:271` |
| CREATE | `canTransfer(caller, value)` before value transfer | `vm/evm.go:368` |

**Relation to buyGas:** `buyGas` `balanceCheck` includes `txValue` using `gasFeeCap` track (§7.2 step 8). `CanTransfer` is a **separate** execute-entry check on `value` alone — mirror op-geth dual validation.

**Deposits:** no `buyGas`, but deposit with non-zero `value` still runs `CanTransfer` at execute entry (mint does not waive value transfer requirement from sender balance).

**eth_call:** skip when `skipTransactionChecks` (mirror op-geth `NoBaseFee` eth_call paths that bypass consensus checks).

### 5.7 OpStackExecuteViaHostOutput

```cpp
struct OpStackReceiptMetadata {
    std::optional<bcos::u256> l1Fee;
    std::optional<bcos::u256> operatorFee;
    std::optional<uint64_t> depositNonce;
};

struct OpStackExecuteViaHostOutput {
    EVMCResult evmcResult{};
    state::StateDiff stateDiff;
    std::vector<LogEntry> logs;
    int64_t gasUsed{0};           // post-settlement; see §7.3/§7.6
    std::optional<uint64_t> maxUsedGas;  // peakGasUsed pre-refund (op-geth ExecutionResult.MaxUsedGas)
    OpStackReceiptMetadata receiptMeta;
};
```

Deposit `gasUsed`: success = post-§7.3 `gasUsed`; failure (post-Regolith) = `gasLimit`.

### 5.8 Effective gas price (op-geth-aligned)

Mirror `TransactionToMessage` (`state_transition.go:204–209`):

```cpp
// Called at start of opStackExecuteViaHost, before preCheck/buyGas.
u256 resolveEffectiveGasPrice(u256 gasTipCap, u256 gasFeeCap, u256 baseFee)
{
    auto effective = gasTipCap + baseFee;
    if (gasFeeCap != 0 && effective > gasFeeCap)
        effective = gasFeeCap;
    return effective;
}
```

| Field | Source | Used for |
|-------|--------|----------|
| `gasFeeCap` | tx input | **balanceCheck** only (`gasLimit × gasFeeCap + rollup + blob + value`) |
| `gasTipCap` | tx input | feeds `resolveEffectiveGasPrice` |
| `effectiveGasPrice` | resolved | **mgval** (`gasLimit × effective`), **returnGas**, **coinbase tip** (`gasUsed × (effective - baseFee)`) |

**Legacy tx:** adapter sets `gasFeeCap = gasTipCap = legacyGasPrice`; `effectiveGasPrice = legacyGasPrice`.

**eth_call** (`call && skipTransactionChecks`): when `noBaseFee && gasFeeCap==0 && gasTipCap==0`:
- skip buyGas (no deduction)
- skip OP fee routing (`state_transition.go:697–701`)
- `returnGas` is no-op (effectiveGasPrice = 0)

This matches op-geth dual-track: balance check uses cap, settlement uses effective price.

### 5.9 Gas refund counter and `calcRefund` (Q5: B)

Isthmus binds to Prague → London active → **EIP-3529** refund quotient **5** (`RefundQuotientEIP3529`). Mirror op-geth `calcRefund` (`state_transition.go:806–822`) and post-execute ordering (`state_transition.go:644–664`).

**State refund counter** (`bcos-evm/eth/state/State.hpp` — shared eth layer, not op-only):

```cpp
void add_refund(uint64_t amount);
[[nodiscard]] uint64_t get_refund() const noexcept;
void clear_refund() noexcept;  // called on checkpoint/revert boundaries
```

- `checkpoint()` / `revert()` reset `m_gasRefund` with journal (same scope as warm-access journal).
- **`clear_refund()` sole call site:** `executeMessage` tx entry (after warm-up, before EVM execution).

**EthHost accumulation** (`EthHost.cpp`): when `set_storage` returns `evmc_storage_status`, add refund amounts per geth `operations_acl.go` / `gas_table.go` (SSTORE clear/reset schedules, selfdestruct). EIP-7702 existence refund via `applyAuthorization` (§5.10) calls `state.add_refund(CallNewAccountGas - TxAuthTupleGas)`.

**Settlement helper** (in `OpStackTxExecutor.cpp` or `OpStackGasSettlement.h`):

```cpp
struct GasSettlement {
    uint64_t gasRemaining{};  // after calcRefund — used by returnGas
    uint64_t gasUsed{};       // gasLimit - gasRemaining — used by fee routing
    uint64_t peakGasUsed{};   // pre-refund gasUsed (= gasLimit - gasLeftAfterEvm); op-geth peakGasUsed
};

GasSettlement postExecuteGasSettlement(
    uint64_t gasLimit, uint64_t gasLeftAfterEvm, uint64_t stateRefund, uint64_t floorDataGas)
{
    auto const peakGasUsed = gasLimit - gasLeftAfterEvm;
    auto refundCap = peakGasUsed / 5;  // EIP-3529 (Isthmus-only; no pre-London branch)
    auto refund = std::min(stateRefund, refundCap);
    GasSettlement s;
    s.gasRemaining = gasLeftAfterEvm + refund;
    s.gasUsed = gasLimit - s.gasRemaining;
    s.peakGasUsed = peakGasUsed;
    // EIP-7623 (Q6-A): post-refund floor bump — mirrors state_transition.go:650–661
    if (floorDataGas > 0 && s.gasUsed < floorDataGas)
    {
        s.gasRemaining = gasLimit - floorDataGas;
        s.gasUsed = floorDataGas;
    }
    if (floorDataGas > 0 && s.peakGasUsed < floorDataGas)
    {
        s.peakGasUsed = floorDataGas;
    }
    return s;
}
```

**Semantics (op-geth-aligned):**

| Field | Meaning |
|-------|---------|
| `result.gas_left` | `gasRemaining` **before** `calcRefund` |
| `state.get_refund()` | accumulated SSTORE/SELFDESTRUCT refunds during EVM |
| `gasRemaining` (settlement) | `gas_left + calcRefund` → `returnGas` multiplier |
| `gasUsed` (settlement) | `gasLimit - gasRemaining` → coinbase tip, base/L1/operator fees |

**evmone parity (Q10-A — normative Branch A):**

Settlement **always** uses §7.3 formula with `state.get_refund()` — mirrors op-geth deferred refund model. `evmc_result.gas_left` is **pre-`calcRefund` remaining gas**. **Ignore** `evmc_result.gas_refund` in settlement (never merge with `State::get_refund()`).

**Day-0 spike (Stage A validation gate, not branch selection):**

| Spike outcome | Required action |
|---------------|-----------------|
| evmone defers refunds → `gas_left` pre-refund | No shim; proceed as spec |
| evmone pre-applies refunds to `gas_left` | **Host shim:** stop accumulating `State` refunds from `set_storage` status for settlement (or subtract pre-applied amount); `postExecuteGasSettlement` still uses op-geth formula with corrected inputs |
| Either path | `CalcRefundTest::EvmoneParity_noDoubleCount` must match op-geth fixture |

**Branch B** (trust `gas_left` only, `stateRefund=0`) is **rejected** by Q10-A — do not implement unless spec is reopened.

**Isthmus `RevisionConfig` preset:**

```cpp
RevisionConfig makeIsthmusRevisionConfig()
{
    RevisionConfig c;
    c.revision = EVMC_PRAGUE;  // or EVMC_CANCUN until Day-0 spike
    c.eip7623 = true;
    c.eip7702 = true;
    c.prague_post_execution = false;  // Q8-A: Isthmus skips 6110/7002/7251
    c.calldata_floor_per_token = 10;  // params.TxCostFloorPerToken
    return c;
}
```

---

## 6. Fee formulas

### 6.1 L1 cost — Fjord

Reference: `NewL1CostFuncFjord` (rollup_cost.go:607–627)

```
scaledL1BaseFee     = l1BaseFeeScalar × l1BaseFee
calldataCostPerByte = scaledL1BaseFee × 16
blobCostPerByte     = l1BlobBaseFeeScalar × l1BlobBaseFee
l1FeeScaled         = calldataCostPerByte + blobCostPerByte
estimatedSizeScaled = max(MIN_TX_SIZE_SCALED, INTERCEPT + FASTLZ_COEF × fastLzSize)
l1Cost              = estimatedSizeScaled × l1FeeScaled / FJORD_DIVISOR
```

Empty `RollupCostData` (deposit, eth_call): return `0`.

### 6.2 Operator cost — Isthmus

Reference: `newOperatorCostFuncIsthmus` (rollup_cost.go:253–268)

```
operatorCost(gas) = gas × operatorFeeScalar / 1_000_000 + operatorFeeConstant
```

If `operatorFeeParams` slot (32 bytes) is all-zero (`common.Hash{}`), return `0`.

### 6.3 FastLz

Port `FlzCompressLen` verbatim from op-geth `rollup_cost.go:667+`. Consensus-critical.

### 6.4 Floor data gas — EIP-7623 (Q6: A)

Reference: `FloorDataGas` (`state_transition.go:120–133`), constants `params/protocol_params.go`.

```
zeroes = count(data, 0x00)
nonZeroes = len(data) - zeroes
tokens = nonZeroes × TxTokenPerNonZeroByte + zeroes   // TxTokenPerNonZeroByte = 4
floorDataGas = TxGas + tokens × TxCostFloorPerToken   // TxGas = 21000, TxCostFloorPerToken = 10
```

Overflow check mirrors op-geth (`ErrGasUintOverflow`).

**When applied (op-geth `innerExecute`, not `preCheck`):**

| Phase | Check | Reference |
|-------|-------|-----------|
| Execute entry | `gasLimit >= floorDataGas` else `ErrFloorDataGas` | `state_transition.go:546–554` |
| Post-`calcRefund` settlement | If `gasUsed < floorDataGas`: bump `gasUsed` to floor, reduce `gasRemaining` | `state_transition.go:650–655` |
| `maxUsedGas` | `peakGasUsed = max(peakGasUsed, floorDataGas)` | `state_transition.go:659–660` |

Applies to **all txs** including deposits (L1 attributes 176-byte calldata has non-trivial floor). `RevisionConfig.eip7623` must be `true` for Isthmus path.

---

## 7. Transaction lifecycle

### 7.1 preCheck (all txs)

Called before buyGas or deposit execution.

**Deposit path** (state_transition.go:346–361):

| Step | Action |
|------|--------|
| 1 | Set `initialGas = gasLimit` |
| 2 | If `isSystemTransaction` → `ErrSystemTxNotSupported` |
| 3 | `gasPoolSubGas(gasLimit)` if hook provided |
| 4 | Return success (no balance/nonce/fee checks) |

**Non-deposit path:**

| Check | Reference |
|-------|-----------|
| Nonce validation | state_transition.go:364–376 |
| Sender is EOA or EIP-7702 delegation | state_transition.go:384–389; `parseDelegationTarget(senderCode)` |
| `gasFeeCap >= baseFee`, `gasFeeCap >= gasTipCap` | state_transition.go:400–413 |
| Blob hash validity (if present) | state_transition.go:416–434 |
| `blobGasFeeCap >= blockBlobBaseFee` | state_transition.go:436–449 (preCheck, not buyGas) |
| EIP-7702 auth list shape | `authorizations` non-empty → `to` set, list non-empty → §5.10 |

Skipped when `skipNonceChecks` / `skipTransactionChecks` (eth_call).

### 7.1.1 Execute entry checks (all txs, mirrors `innerExecute`)

Reference: `state_transition.go:538–563` — runs after preCheck/buyGas, before EVM.

| Step | Action |
|------|--------|
| 1 | Compute intrinsic gas from calldata + access list + authorizations + create flag |
| 2 | If `gasRemaining < intrinsicGas` → `ErrIntrinsicGas` |
| 3 | `floorDataGas = FloorDataGas(calldata)` when `eip7623` active (Isthmus: always) |
| 4 | If `gasLimit < floorDataGas` → `ErrFloorDataGas` |
| 5 | Subtract intrinsic gas from `gasRemaining` (deposits included; tracer display differs in op-geth only) |
| 6 | If `value` overflows `u256` → `ErrInsufficientFundsForTransfer` |
| 7 | If `value != 0` and `!canTransfer(from, value)` → `ErrInsufficientFundsForTransfer` (§5.12) |

Skipped steps 6–7 when `skipTransactionChecks` (eth_call).

`floorDataGas` cached on `OpStackTxExecutionData` for §7.3 settlement.

### 7.2 buyGas (non-deposit only)

Reference: state_transition.go:282–343

| Step | Action |
|------|--------|
| 1 | If `call && skipTransactionChecks && noBaseFee && gasFeeCap==0 && gasTipCap==0` → skip buyGas balance deduction (op-geth still SubGas; see P2 note §5.8) |
| 2 | `gasPoolSubGas(gasLimit)` if hook provided (mirrors buyGas L331) |
| 3 | If `gasLimit <= 0` → return true |
| 4 | `effectiveGasPrice = resolveEffectiveGasPrice(gasTipCap, gasFeeCap, baseFee)` |
| 5 | `mgval = gasLimit × effectiveGasPrice` |
| 6 | If `!skipNonceChecks && !skipTransactionChecks`: `mgval += l1Cost + operatorCost(gasLimit)` |
| 7 | If blob tx: `mgval += blobGasUsed × blockBlobBaseFee` |
| 8 | If typed tx has fee cap field (`gasFeeCap` set / EIP-1559): `balanceCheck = gasLimit×gasFeeCap + l1 + operator + blobGasUsed×blobGasFeeCap + txValue` |
| 9 | Else (legacy): `balanceCheck = mgval + txValue` |
| 10 | If insufficient → `EVMC_INSUFFICIENT_BALANCE` |
| 11 | `sender -= mgval`; cache `m_l1CostCharged`, `m_operatorCostLimit`, `m_effectiveGasPrice` |

Note: mgval uses **effectiveGasPrice**; balanceCheck uses **gasFeeCap** when EIP-1559 — dual-track per op-geth.

### 7.3 Post-execute gas settlement (all txs)

Reference: `state_transition.go:644–661` (pre-`returnGas`)

After `executeMessage` returns (any status: SUCCESS, REVERT, hard failure):

| Step | Action |
|------|--------|
| 1 | `gasLeft = evmcResult.gas_left` (pre-refund remaining) |
| 2 | `peakGasUsed = gasLimit - gasLeft` |
| 3 | `refund = min(state.get_refund(), peakGasUsed / 5)` — EIP-3529 |
| 4 | `gasRemaining = gasLeft + refund` |
| 5 | `gasUsed = gasLimit - gasRemaining` |
| 6 | **EIP-7623 floor bump (Q6-A):** if `gasUsed < floorDataGas`: `gasRemaining = gasLimit - floorDataGas`; `gasUsed = floorDataGas` |
| 7 | `peakGasUsed = max(peakGasUsed, floorDataGas)` |
| 8 | `output.gasUsed = static_cast<int64_t>(gasUsed)`; `output.maxUsedGas = peakGasUsed` |
| 9 | `gasPoolReturnGas(gasRemaining, gasUsed)` if hook provided (mirrors `gp.ReturnGas`; post-refund + floor values) |

**Deposit txs:** still run steps 1–7 for gas metering and gas pool (op-geth `state_transition.go:644–672`); **skip** §7.4 OP fee routing only (`gasPrice=0` makes `returnGas` balance-neutral).

**Hard failure / REVERT:** settlement always runs before §7.4 (do not skip on non-SUCCESS status).

### 7.4 refundGas (non-deposit only)

Reference: `state_transition.go:664–733`

Uses `gasRemaining` / `gasUsed` from §7.3.

| Step | Action |
|------|--------|
| 1 | If `call && skipTransactionChecks && noBaseFee && gasFeeCap==0 && gasTipCap==0` → skip all settlement |
| 2 | **Always** `returnGas`: `sender += gasRemaining × effectiveGasPrice` (incl. hard failure) |
| 3 | `effectiveTip = max(effectiveGasPrice - baseFee, 0)` |
| 4 | `coinbase += gasUsed × effectiveTip` |
| 5 | `baseFeeRecipient += gasUsed × baseFee` |
| 6 | `l1FeeRecipient += L1CostFunc(rollupCostData)` |
| 7 | `refundIsthmusOperatorCost()` |
| 8 | `operatorFeeRecipient += operatorCostIsthmus(gasUsed)` |

Hard failure: EVM journal reverts via `executeMessage`, but buyGas deduction is outside journal. **Always** run §7.3 then steps 2–8 (incl. OOG/INVALID); step 2 uses post-`calcRefund` `gasRemaining`.

**Remove current stub behavior:** `OpStackTxExecutor` must not `co_return` before settlement on hard failure (`OpStackTxExecutor.h:142–146`).

### 7.5 refundIsthmusOperatorCost

```cpp
auto limitCost = operatorCostIsthmus(gasLimit, feeParams);
auto usedCost  = operatorCostIsthmus(gasUsed, feeParams);
assert(usedCost <= limitCost);
sender += (limitCost - usedCost);
```

### 7.6 Deposit execution (op-geth `execute()` mirror)

Reference: `state_transition.go:473–513`

| Step | Action |
|------|--------|
| 1 | If `mint` present → `AddBalance(depositTx.from, mint)` **before checkpoint** |
| 2 | `state.checkpoint()` — op-geth `snap := Snapshot()` |
| 3 | Build `evmc_message`, run `executeMessage` (no buyGas); non-create path increments nonce inside EVM (`state_transition.go:602`) |
| 4 | L1 attributes: written **inside** step 3 via L1Block predeploy setter dispatch (`0x098999be`), NOT a pre-EVM direct write |
| 5 | Deposit **does** subtract intrinsic gas like any tx (`state_transition.go:538–563`); tracer display differs, gas meter does not |
| 6 | On success: run §7.3 settlement + gasPool return; skip §7.4 OP fee routing (`gasPrice=0`) |
| 7 | On failure (`err != nil`, post-Regolith): `state.revert()` to checkpoint → **L1Block slot writes rolled back**; `SetNonce(from, nonce+1)`; `gasPool.returnGas(0, gasLimit)`; `output.gasUsed = gasLimit` |
| 8 | `receiptMeta.depositNonce` = `GetNonce(from)` captured **before** step 3 |

Deposit invariants: `gasPrice = 0`, `nonce = 0` (unchecked), empty `RollupCostData`.

**L1 attributes failure semantics (op-geth-aligned):** slot writes occur inside `executeMessage` and are reverted on deposit failure. A failed L1 attributes deposit leaves previous block's fee params intact.

### 7.7 OpHostExtension State injection

Mirror `FiscoHostExtension`: `OpHostExtension` holds `state::State*` injected at construction (mutable — setter writes storage). `tryChainPrecompile` calls `L1BlockPredeploy::dispatch(*m_state, msg)`.

**L1Block test paths:** With §8.4 hook, top-level CALL (L1 attributes deposit) and nested CALL (getter tests) both route through `tryChainPrecompile`.

### 7.8 EIP-7702 authorization application (Q7: A)

Reference: `state_transition.go:600–622` — non-create path only.

In `executeMessage`, after execute-entry checks and **before** `vm->execute` / chain precompile dispatch:

| Step | Action |
|------|--------|
| 1 | If create tx → skip (authorizations incompatible; preCheck rejects non-empty list without `to`) |
| 2 | `SetNonce(sender, nonce + 1)` |
| 3 | For each `SetCodeAuthorization` in input: `applyAuthorization(state, auth)` — **ignore errors** per tuple |
| 4 | If `to` has delegation code → warm delegation target address (EIP-2929) |
| 5 | Proceed to EVM CALL / §8.4 chain precompile |

Authorizations apply inside `executeMessage` journal: reverted on REVERT/hard failure within EVM checkpoint; buyGas remains outside journal.

**eth layer files:** `bcos-evm/eth/Eip7702.h/.cpp` (delegation codec + validate/apply); wired from `executeMessage.cpp` and `OpStackPreCheck` (auth list shape).

---

## 8. L1Block predeploy (`0x4200…0015`)

### 8.1 Storage layout

| Slot | Content | Writer |
|------|---------|--------|
| 1 | `l1BaseFee` | L1 attributes deposit |
| 3 | `l1FeeScalars` (packed) | L1 attributes deposit |
| 7 | `l1BlobBaseFee` | L1 attributes deposit |
| 8 | `operatorFeeParams` (packed) | L1 attributes deposit |

### 8.2 Isthmus L1 attributes (via L1Block EVM setter)

L1 attributes are **not** written by a standalone pre-EVM function. They are written when the L1 attributes deposit executes `CALL` to `0x4200…0015` with selector `0x098999be` — handled by `L1BlockPredeploy::applySetter` inside `executeMessage`.

**Detection** (for logging/tests):

- `depositTx.from == DEPOSITOR_ACCOUNT` (`0xDeaDDEaDDeAdDeAdDEAdDEaddeAddEAdDEAd0001`)
- `depositTx.to == OP_L1_BLOCK_PREDEPLOY` (`0x4200…0015`)
- `data[0:4] == 0x098999be`
- `len(data) >= 176`

**Payload layout** (offsets from `rollup_cost.go:514–527`):

```
offset 0:   selector (4)
offset 4:   baseFeeScalar (uint32)
offset 8:   blobBaseFeeScalar (uint32)
offset 12:  sequenceNumber (uint64)
offset 20:  timestamp (uint64)
offset 28:  l1BlockNumber (uint64)
offset 36:  l1BaseFee (uint256)
offset 68:  l1BlobBaseFee (uint256)
offset 100: hash (bytes32)
offset 132: batcherHash (bytes32)
offset 164: operatorFeeScalar (uint32)
offset 168: operatorFeeConstant (uint64)
Total: 176 bytes
```

Writes all L1Block storage fields per `setL1BlockValuesIsthmus` (number, timestamp, hash, batcherHash, sequence, fee slots 1/3/7/8).

### 8.3 L1Block getter/setter dispatch

```cpp
class L1BlockPredeploy {
public:
    static std::optional<evmc_result> dispatch(
        state::State& state, evmc_message const& msg);
    // getter selectors → read storage
    // setter 0x098999be → applySetter(state, calldata)
};
```

Read-only getters: `l1BaseFee`, `baseFeeScalar`, `blobBaseFeeScalar`, `l1BlobBaseFee`, `operatorFeeScalar`, `operatorFeeConstant`.

`setL1BlockValues` only invocable via deposit CALL (Depositor account); direct user CALL rejected (mirror L1Block.sol `onlyDepositor`).

`OpHostExtension::tryChainPrecompile` routes `0x4200…0015` to `L1BlockPredeploy::dispatch`.

### 8.4 L1Block top-level CALL dispatch (recommended: D)

**Problem:** L1 attributes deposit is a **top-level CALL** to `0x4200…0015`. Current `executeMessage` returns SUCCESS for empty-code accounts without invoking `EthHost::call()` → `OpHostExtension` (executeMessage.cpp:169–175). op-geth avoids this by predeploying **L1Block contract bytecode** at genesis.

**Recommendation: D** — minimal `executeMessage` extension hook (OP track only via `HostExtension`).

**Critical:** empty-code path must use the same `checkpoint → execute → commit/revert` pattern as the VM path. **Never** call unconditional `state.commit()` when an outer caller checkpoint exists (deposit §7.6) — that would pop the deposit snapshot and break L1 attrs failure rollback.

```cpp
// executeMessage.cpp — replace empty-code SUCCESS shortcut (line ~169)
if (code.empty() && !isCreateKind(input.message.kind))
{
    state.checkpoint();
    evmc_result result{};
    if (input.extension != nullptr)
    {
        if (auto chain = input.extension->tryChainPrecompile(
                input.revisionConfig.revision, input.message))
        {
            result = *chain;
        }
        else
        {
            result = makeSuccessResult(input.message.gas);
        }
    }
    else
    {
        result = makeSuccessResult(input.message.gas);
    }
    output.result = result;
    output.logs = host.take_logs();
    if (result.status_code == EVMC_SUCCESS)
    {
        state.commit();
        output.stateDiff = state.build_diff();
    }
    else
    {
        state.revert();
    }
    return output;
}
```

`L1BlockPredeploy::dispatch(State& state, evmc_message const& msg)` contract: reads/writes via `state.set_storage`; returns `evmc_result` with updated `gas_left`; enforces Depositor-only on setter `0x098999be`.

| Option | Verdict | Reason |
|--------|---------|--------|
| **A** Genesis L1Block bytecode | 长期 Integration 可选 | 与 op-geth 完全一致，但需维护 bytecode 产物、genesis 配置，Stage B +2–3 天 |
| **B/D** `extension` empty-code hook | **推荐，本 spec 采用** | 语义等价（同 calldata → 同 storage）；FISCO/eth 轨 `extension=nullptr` 行为不变；L1 attrs deposit + getter 统一走 `L1BlockPredeploy` |
| **C** OpStack 绕过 executeMessage | 拒绝 | 非 op-geth 执行路径，破坏 deposit checkpoint/revert 封装 |

**Consensus equivalence criterion:** `L1BlockPredeploy::applySetter(calldata)` storage diff == op-geth L1Block bytecode execution diff. Verified by `L1AttributesDepositTest` against op-geth fixture.

**Not in scope for A:** Full genesis bytecode embedding deferred to Integration phase unless native dispatch tests fail parity check.

---

## 9. OpStackExecuteViaHost flow

```
1. Validate stateView, vm, hashImpl
2. state::State state(*stateView)
3. effectiveGasPrice = resolveEffectiveGasPrice(gasTipCap, gasFeeCap, blockInfo.baseFee)
4. opStackPreCheck(input) → early exit on failure
4. If deposit:
     a. capture depositNonce = GetNonce(from)
     b. applyMint (if present)
     c. state.checkpoint()
     d. executeEntryChecks (§7.1.1) → early exit on intrinsic/floor failure
     e. executeMessage (no buyGas) — L1 attrs via L1Block setter inside EVM
     f. postExecuteGasSettlement (§7.3) — gas metering + EIP-7623 floor
     g. on failure: state.revert(); nonce++; gasPool return(0, gasLimit)
     h. return (skip §7.4 OP fee routing)
5. Non-deposit:
     a. rollupCostData = input or newRollupCostData(serializedTx)
     b. feeParams = loadOpStackFeeParams(state)
     c. buyGas → early exit on failure
     d. executeEntryChecks (§7.1.1) → early exit on intrinsic/floor failure
     e. executeMessage (OpHostExtension, EVMC_PRAGUE)
     f. postExecuteGasSettlement (§7.3) — always, any EVM status
     g. refundGas — always, any EVM status
6. Populate receiptMeta (l1Fee, operatorFee)
7. Return output
```

---

## 10. Error handling

| Scenario | Behavior |
|----------|----------|
| L1Block slots uninitialized | L1 cost = 0; operator = 0 |
| Insufficient balance | buyGas fails; no EVM execution |
| `gasLimit < FloorDataGas` | execute entry rejects (`ErrFloorDataGas`); no EVM execution |
| `value > sender balance` | execute entry `CanTransfer` fails (`ErrInsufficientFundsForTransfer`) |
| System deposit tx | preCheck rejects |
| EVM REVERT | Refund unused gas; L1/operator fees retained |
| EVM hard failure | Refund unused gas; L1 fee routed; operator partial refund |
| Empty rollupCostData (non-deposit) | L1 cost = 0 |
| Operator cost overflow | assert (matches op-geth panic) |

---

## 11. Testing

### 11.0 Test-first gates

- **Stage A merge gate:** `RollupCostTest` + `OpStackFeeTest` + `RefundIsthmusTest` + `CalcRefundTest` + `FloorDataGasTest` + `OpStackSettlementTest` green; smoke uses real Fjord/Isthmus formulas (no CI mock injection); **pre-existing** 15 CTest targets still pass; compat filtered suite unchanged or extended (not frozen at incorrect count).
- **Stage A blocker:** Q10 Day-0 spike must pass `EvmoneParity_noDoubleCount` (Branch A normative; shim allowed, formula unchanged).
- **Stage B merge gate:** all §11.2 cases (15) + fixtures + full regression.

### 11.1 Unit tests — fee engine (Stage A)

| Test file | Cases | op-geth reference |
|-----------|-------|-------------------|
| `RollupCostTest.cpp` | byte counting, FastLz | `rollup_cost_test.go` |
| `OpStackFeeTest.cpp` | Fjord L1 → `fjordFee` (3_203_000) | `TestFjordL1CostFuncMinimumBounds` |
| `OpStackFeeTest.cpp` | Isthmus operator gas=1618 → `ithmusOperatorFee` | `TestNewOperatorCostFunc` |
| `RefundIsthmusTest.cpp` | limit=1618, used=500 refund delta | derived from test constants |
| `RollupCostTest.cpp` | `FlzCompressLen` 5-vector table | `TestFlzCompressLen` |
| `RollupCostTest.cpp` | `newRollupCostData(empty_tx.bin)` → fastLz=31 | `emptyTx` |
| `OpStackFeeTest.cpp` | Fjord min-boundary fastLz {100,150,170,171,175,200} | `TestFjordL1CostFuncMinimumBounds` |
| `OpStackFeeTest.cpp` | `loadOpStackFeeParams` slot 3/8 unpack | `testStateGetter` |
| `OpStackSettlementTest.cpp` | 4-way routing: coinbase, baseFee 0x19, L1 0x1A, operator 0x1B | state_transition.go:702–732 |
| `OpStackSettlementTest.cpp` | REVERT/OOG: unused gas, L1 retained, operator partial refund | §1 #4 |
| `CalcRefundTest.cpp` | `Settlement_capBinds` / `Settlement_capDoesNotBind` pure formula | `state_transition.go:806–822` |
| `CalcRefundTest.cpp` | `EthHost_sstoreClear_accumulatesRefund` + `Refund_clearedOnRevert` | `operations_acl.go` |
| `CalcRefundTest.cpp` | `returnGas` uses `gasRemaining` not `gas_left` | `returnGas` state_transition.go:824–834 |
| `CalcRefundTest.cpp` | `EvmoneParity_noDoubleCount` (Q10 branch A or B) | §5.9 decision tree |
| `FloorDataGasTest.cpp` | `FloorDataGas` formula + overflow vectors | `state_transition.go:120–133` |
| `FloorDataGasTest.cpp` | Execute entry: `gasLimit < floor` → reject | `ErrFloorDataGas` |
| `FloorDataGasTest.cpp` | Post-refund bump: low-execution tx pays floor `gasUsed` | `state_transition.go:650–661` |
| `OpStackSettlementTest.cpp` | `HardFailure_stillRefundsUnusedGas` + L1/operator partial refund | §7.4 |

### 11.2 Unit / integration tests — OP semantics (Stage B)

| Test | Description |
|------|-------------|
| `DepositTxPreCheckTest` | System tx rejected; deposit passes without balance |
| `DepositMintTest` | Mint credited before execution |
| `DepositNoFeeRoutingTest` | No fee routing after deposit |
| `L1AttributesDepositTest` | Success: deposit CALL writes slots → user tx gets correct L1 cost |
| `L1AttributesDepositFailureTest` | Failed L1 attrs deposit reverts slots; user tx reads **previous** block params |
| `GasFeeCapBalanceTest` | gasFeeCap balance check |
| `BlobGasBalanceTest` | Blob tx blobGasFeeCap check |
| `L1BlockGetterTest` | CALL `l1BaseFee()` via OpHostExtension |
| `Eip7702PreCheckTest` | Non-empty auth list without `to` rejected; empty list with field present rejected |
| `Eip7702DelegationSenderTest` | Sender with delegation code passes EOA check |
| `Eip7702ApplyAuthorizationTest` | Valid auth installs delegation; invalid auth skipped; existence refund 12500 |
| `Eip7702ClearDelegationTest` | Auth with zero address clears delegation code |
| `IsthmusPostExecutionPolicyTest` | `makeIsthmusRevisionConfig().prague_post_execution == false`; documents Integration skip |
| `CanTransferTest` | `value > balance` rejected at execute entry |
| `CanTransferPredeployTest` | Transfer to `0x4200…0015` succeeds when sender funded (no deny-list) |

### 11.3 Integration smoke tests

Extend `OpStackExecuteViaHostSmokeTest.cpp`: baseFee recipient (`0x19`), operator refund, coinbase tip, hard-failure unused gas refund. **Stage A exit: remove `m_l1CostFunc` mock**; use canonical `fjordFee` constants.

### 11.4 Fixtures

```
test/fixtures/opstack/
├── empty_tx.bin                        # emptyTx.MarshalBinary(), fastLz=31
├── isthmus_l1_attributes.bin           # 176-byte payload (with operator)
├── isthmus_l1_attributes_no_operator.bin
├── contract_call_tx.bin                # fastLz=202
├── deposit_tx_call.json
└── deposit_tx_mint.json
```

Sources: `rollup_cost_test.go`, `receipt_opstack_test.go:268/290`.

### 11.5 Test constants (op-geth `rollup_cost_test.go`)

```
baseFee=1000×1e6  blobBaseFee=10×1e6  baseFeeScalar=2  blobBaseFeeScalar=3
operatorFeeScalar=1439103868  operatorFeeConstant=1256417826609331460
fjordFee=3203000  minimumFjordGas=1600  ithmusOperatorFee=1256417826611659930 (gas=1618)
```

### 11.6 Regression gates

| Stage | Suite | Baseline |
|-------|-------|----------|
| A | pre-existing `bcos-evm/test` targets | 15/15 pass (add new targets separately) |
| A | compat filtered (`CompatExecuteViaHost\|…\|OpStackExecuteViaHost`) | all pass; count may grow with eth-layer changes |
| B | above + §11.2 all 15 cases | +15 integration |

Register new test targets in `bcos-evm/test/CMakeLists.txt` (link `bcos-evm-op`).

---

## 12. Implementation schedule (~5.5–6 weeks)

Realistic estimate post 2nd review: Stage A **11–13** days, Stage B **14–17** days (Q5-B eth-layer + §8.4 hook). Original 5-week plan is lower bound only if Q10 spike passes first try.

### Stage A — Fee engine (Week 1–2.5)

| Day | Task |
|-----|------|
| 0 | **Day-0 spike:** evmone Prague + Q10 refund semantics (merge blocker) |
| 1 | `OpStackConstants.h`, `RollupCost.h/.cpp` |
| 2 | `OpStackFee.h/.cpp`, `OpStackFloorGas.h/.cpp` |
| 3 | `RollupCostTest.cpp`, `OpStackFeeTest.cpp`, fixtures |
| 4–5 | `State` refund counter + `EthHost` SSTORE refund accumulation + compat regression |
| 6 | `OpStackTxExecutor` rewrite + `postExecuteGasSettlement` / `calcRefund` |
| 7 | `OpStackExecuteViaHost` wiring (non-deposit path) |
| 8 | `RefundIsthmusTest.cpp`, `CalcRefundTest.cpp`, extend smoke tests |
| 9–13 | Regression + buffer |

### Stage B — OP semantics (Week 3–6)

| Day | Task |
|-----|------|
| B1–B2 | `OpStackDepositTx`, `OpStackPreCheck` |
| B3 | Deposit execution path + §7.3 gas metering |
| B4–B5 | `L1BlockStorage` + `L1BlockPredeploy` setter/getter |
| B6 | `executeMessage` empty-code extension hook (§8.4) + checkpoint fix |
| B7 | `Eip7702.h/.cpp` + `executeMessage` auth application (§7.8) |
| B8 | `Transfer.h` + execute-entry `CanTransfer` (§5.12) + `EthHost::call` guard |
| B9 | `OpHostExtension` wiring + deposit path |
| B10 | GasFeeCap + blob gas in buyGas |
| B11–B12 | Stage B tests + fixtures + `applyStateDiffToView` helper |
| B13 | `OpStackReceiptMeta.h`, output wiring |
| B14–B17 | Full regression + buffer |

### EVM revision note

Isthmus binds to Prague. Caller sets `revisionConfig.revision = EVMC_PRAGUE` when available in evmone; until then use `EVMC_CANCUN` with documented delta. Day-0 spike: confirm evmone Prague support in build environment.

---

## 13. Dependencies

### Depends on (existing)

- `bcos-evm-eth`: `executeMessage`, `state::State`, `state::StateView`, `HostExtension`
- `bcos-utilities`: `u256`, `bytes`
- `BlockInfo`: `baseFee`, `coinbase`, `blobBaseFee`

### Provides to (Integration phase — out of scope)

- `OpStackExecuteViaHostInput` as executor adapter contract
- `gasPoolSubGas` hook → scheduler gas pool
- `OpStackReceiptMetadata` → receipt factory
- `newRollupCostData` → txpool fee estimation

### Does not modify (default)

- `bcos-evm-bcos` (`ExecuteViaHost`, `FiscoHostExtension`)
- `bcos-executor`, `bcos-txpool`, `bcos-scheduler`

### Minimal eth change (§8.4 D + §5.9 Q5)

- `bcos-evm/eth/state/State.hpp/.cpp`: `m_gasRefund` counter; `add_refund` / `get_refund` / `clear_refund`; reset on `checkpoint`/`revert`
- `bcos-evm/eth/state/EthHost.cpp`: accumulate refunds from `set_storage` status + selfdestruct (mirror geth `operations_acl.go`)
- `bcos-evm/eth/Transfer.h`: `canTransfer` / `transfer` mirroring `core/evm.go` (§5.12); used in execute entry + `EthHost::call`
- `bcos-evm/eth/RevisionConfig.h`: add `prague_post_execution` flag; `makeIsthmusRevisionConfig()` sets `false` (§5.11)
- `bcos-evm/eth/Eip7702.h/.cpp`: delegation codec, `validateAuthorization`, `applyAuthorization` (§5.10, §7.8)
- `bcos-evm/eth/executeMessage.cpp`: empty-code path `checkpoint/commit/revert` + `extension->tryChainPrecompile`; **`clear_refund()` at tx entry** (sole call site); EIP-7702 auth apply before CALL (§7.8)
- Guard: only when `input.extension != nullptr`; FISCO/eth callers unaffected for §8.4; refund counter benefits all eth-track tests
- `bcos-evm-eth` `EthHost::call()` nested path unchanged (refunds accumulate via same `set_storage` hook)

---

## 14. Risks

| Risk | Mitigation |
|------|------------|
| FastLz diverges from op-geth | Byte-for-byte test on known inputs |
| u256 overflow in operator cost | Same operation order as op-geth; assert |
| Mint credit target wrong | Cross-test against op-geth deposit fixture during Stage B |
| L1 attributes byte layout mismatch | Use `IsthmusL1AttributesLen=176` test vectors |
| evmone `gas_left` vs `calcRefund` double-count | Q10-A Branch A normative; Day-0 spike + shim if needed; `EvmoneParity_noDoubleCount` merge gate |
| `executeMessage` empty-code unconditional `commit()` | §8.4 checkpoint fix; deposit revert regression test |
| Eth layer scope underestimated | §2.2 Q6–Q10; schedule 5.5–6 weeks |
| L1Block top-level CALL blocked | §8.4 empty-code extension hook in executeMessage |
| Block ordering not tested until Stage B | `L1AttributesDepositTest` explicitly validates ordering |
| Breaking existing smoke tests | `feeParamsOverride` test hook; migrate smoke off mock by Stage A exit |
| Single-tx API vs block ordering | §4.3 caller contract; Integration spec enforces deposit-first |
| Prague revision unavailable | Day-0 evmone spike; Cancun fallback documented |
| `OpStackExecuteViaHostInput` API churn | Freeze input contract v1 at end of Stage A |
| InMemoryStateView no diff apply | `applyStateDiffToView` test helper in Stage B Day 17 |

---

## 15. Integration phase preview (out of scope)

For planning reference only — not part of this spec:

| Item | Module |
|------|--------|
| Executor三轨路由 | `bcos-executor` |
| TxPool `TotalRollupCostFunc` | `bcos-txpool` |
| Receipt encoding | `bcos-protocol` |
| Block builder (deposit ordering) | `bcos-sealer` — **caller MUST put L1 attributes deposit first** |
| Block `postExecution` skip | `bcos-executor` / sealer — **MUST NOT** run EIP-6110/7002/7251 at Isthmus (§5.11) |
| RPC deposit tx codec | `bcos-rpc` |
| L1 derivation | separate L2 infra |

---

## Appendix A: op-geth reference map

| bcos-evm | op-geth |
|----------|---------|
| `newRollupCostData` | `NewRollupCostData` rollup_cost.go:137 |
| `flzCompressLen` | `FlzCompressLen` rollup_cost.go:667 |
| `l1CostFjord` | `NewL1CostFuncFjord` rollup_cost.go:607 |
| `operatorCostIsthmus` | `newOperatorCostFuncIsthmus` rollup_cost.go:253 |
| `loadOpStackFeeParams` | rollup_cost.go:165–167, 228 |
| `resolveEffectiveGasPrice` | `TransactionToMessage` state_transition.go:204–209 |
| `buyGas` dual-track | state_transition.go:282–343 |
| `postExecuteGasSettlement` / `calcRefund` | state_transition.go:644–649, 806–822 |
| `floorDataGas` / EIP-7623 bump | `FloorDataGas` state_transition.go:120–133; bump 650–661 |
| `applyAuthorization` / EIP-7702 | state_transition.go:747–802; apply loop 604–609 |
| `praguePostExecution` disabled | `postExecution` state_processor.go:141 (`!IsIsthmus`) |
| `canTransfer` | `core/evm.go:142–146`; clause 6 state_transition.go:573–580 |
| `refundGas` | returnGas + L713–733 state_transition.go |
| `State::add_refund` | geth `StateDB.AddRefund` + `operations_acl.go` |
| `refundIsthmusOperatorCost` | state_transition.go:836–846 |
| `OpStackDepositTx` | `DepositTx` deposit_tx.go:29 |
| `opStackPreCheckDeposit` | state_transition.go:347–361 |
| `applyIsthmusL1Attributes` / `L1BlockPredeploy::applySetter` | L1Block.sol `setL1BlockValuesIsthmus` via deposit CALL |
| Deposit checkpoint/revert | `execute()` state_transition.go:481–491 |
| `L1BlockPredeploy::dispatch` | L1Block.sol getters |
| Deposit post-execution | state_transition.go:678–688 |
