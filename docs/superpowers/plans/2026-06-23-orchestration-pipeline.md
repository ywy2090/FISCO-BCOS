# Orchestration Pipeline — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Converge `executeViaEth`, `executeViaHost`, and `opStackExecuteViaHost` onto a single synchronous `runOrchestration` deep module, while preserving chain-specific error mapping, OpStack entry ordering, and wrapper-owned async fee/state-machine behavior.

**Architecture:** `eth/orchestration/` owns portable execution orchestration: `OrchestrationContext`, sync `OrchestrationHooks`, structured intrinsic debit, one `ExecuteMessageInput` builder, and one `adoptEvmcResult`. `OrchestrationContext` is construction-valid and owns the only mutable `evmc_message`. OpStack `preDebitEntry` preserves balance/floor-before-debit ordering, and OpStack `buyGas`/`refundGas`/deposit state machine remain in wrapper code.

**Tech Stack:** C++20, evmc, evmone, Boost.Test, CMake, CTest, `bcos-task`.

**Spec:** `docs/superpowers/specs/2026-06-23-orchestration-pipeline-design.md`

## Global Constraints

- `eth/` must never `#include` `bcos/` or `opstack/` headers. OpStack floor checks remain in `opstack/` and enter the pipeline only through OpStack hooks.
- `runOrchestration` is synchronous `void`; `executeVia*` wrappers remain coroutine functions where they already are.
- `OrchestrationContext` is not default constructible. Construct it with `StateView`, initial `evmc_message`, and `RevisionConfig`.
- `debitIntrinsicGas` returns structured outcome only. It must not construct final chain `EVMCResult`.
- `mapIntrinsicFailure` and `mapException(std::exception_ptr)` are chain hooks. Fisco catches `NotFoundCodeError` inside `bcos/ExecuteViaHost.cpp`.
- Pipeline `try/catch` covers steps ②–⑪. Step ① validation remains outside catch and throws `std::invalid_argument`.
- Early exits set `OrchestrationExitKind`; pipeline does not automatically execute post-settlement for early exits.
- `OpStackIntrinsicGasSyncTest` uses test-only `executeMessage` spy seam. Its target compiles OpStack sources with `BCOS_EVM_TESTING` and does not link ordinary `bcos-evm-op`.
- `OrchestrationContext` is the sole owner of `State`; it is explicitly `= delete` copy/move. `gasPrice` is a constructor parameter. OpStack `txData.m_state = &ctx.state` (borrow); there is no second debitable `message`.
- There is **no** `buildExtension` hook. Wrappers construct `HostExtension` (bound to `&ctx.state`) before `runOrchestration` and store it as a borrow pointer in `ctx.extension`; `buildExecuteMessageInput` reads `ctx.gasPrice` and `ctx.extension`.
- `captureSettlementSnapshot` fills `ctx.snapshot` **only** when `intrinsicPolicy.mode == Eip7623`. OpStack settlement math stays in `opstack/` and runs via `postSettle` (not in kernel step ⑨).
- Final `stateDiff`/`logs` mapping is wrapper-owned; the kernel does not produce final values. OpStack must use `ctx.state.build_diff()` **after** all wrapper-side state changes (`buyGas`/`refundGas`/mint/nonce), never `kernelOutput.stateDiff`.
- On the exception path, `state.revert()` + checkpoint semantics belong to `mapException`/wrapper. The kernel pipeline never reverts state itself. `preKernel` may mutate `ctx.state` (Fisco transfer) and signals failure by `throw`.
- After buyGas succeeds, **every** `exitKind` must run settlement + `refundGas` + `build_diff` (OpStack normal). entryChecks split must preserve intrinsic→canTransfer→floor failure precedence.

### Build & Test Conventions

- Run commands from repo root.
- Build dir: `build/`
- Build one target: `cmake --build build --target <Name>Test 2>&1 | rtk err`
- Run one ctest: `ctest --test-dir build -R "^<Name>$" --output-on-failure 2>&1 | rtk err`
- Target names end with `Test`; ctest names drop `Test`.

### File Map

| Path | Responsibility |
| --- | --- |
| `bcos-evm/eth/orchestration/adoptEvmcResult.h` | Single `evmc::Result` → `EVMCResult` adoption |
| `bcos-evm/eth/orchestration/debitIntrinsicGas.h` | `IntrinsicDebitMode`, structured intrinsic/auth debit outcome |
| `bcos-evm/eth/orchestration/OrchestrationContext.h` | Construction-valid execution frame; sole `State` owner; `= delete` copy/move; owns `gasPrice` + `extension` borrow |
| `bcos-evm/eth/orchestration/OrchestrationHooks.h` | Sync hooks (no `buildExtension`), `mapIntrinsicFailure`, `mapException(std::exception_ptr)` |
| `bcos-evm/eth/orchestration/buildExecuteMessageInput.h` | Build `ExecuteMessageInput` from context (reads `ctx.gasPrice` + `ctx.extension`) |
| `bcos-evm/eth/orchestration/captureSettlementSnapshot.h` | Fill shared EIP-7623 settlement snapshot (only `Eip7623` mode) |
| `bcos-evm/eth/orchestration/normalizeIncludedTxVmerr.h` | Eth ADR-015 normalize helper |
| `bcos-evm/eth/orchestration/OrchestrationPipeline.h/.cpp` | Sync 12-step `runOrchestration` |
| `bcos-evm/opstack/OpStackPreDebitEntry.h/.cpp` | OpStack ③½ balance/floor check |
| `bcos-evm/opstack/OpStackExecuteMessageTestHook.h` | Test-only `executeMessage` spy seam |

---

### Task 1A: Pure orchestration helpers

**Files:**

- Create: `bcos-evm/eth/orchestration/adoptEvmcResult.h`
- Create: `bcos-evm/eth/orchestration/debitIntrinsicGas.h`
- Create: `bcos-evm/test/eth/DebitIntrinsicGasTest.cpp`
- Modify: `bcos-evm/test/CMakeLists.txt`

**Interfaces:**

- Produces:
  - `EVMCResult adoptEvmcResult(evmc::Result&& result, bcos::crypto::Hash const& hashImpl)`
  - `enum class IntrinsicDebitMode { None, AuthOnly, Eip7623, OpStackEntry }`
  - `enum class IntrinsicDebitFailure { None, GasLimitMinimum, CalldataOutOfGas, AuthTupleOutOfGas, OpStackIntrinsicOutOfGas }`
  - `struct IntrinsicGasPolicy { IntrinsicDebitMode mode; bool authorizationListPresent; uint64_t authTupleCount; Eip2930AccessList const* accessList; uint8_t web3TypedTxKind; }`
  - `struct DebitIntrinsicGasOutcome { bool ok; IntrinsicDebitFailure failure; int64_t gasLeftOnFailure; int64_t debitAmount; }`
  - `DebitIntrinsicGasOutcome debitIntrinsicGas(evmc_message& message, IntrinsicGasPolicy const& policy)`

- [ ] **Step 1: Write failing helper tests**

Create `bcos-evm/test/eth/DebitIntrinsicGasTest.cpp`:

```cpp
#define BOOST_TEST_MODULE DebitIntrinsicGasTest

#include "bcos-evm/eth/orchestration/debitIntrinsicGas.h"
#include "bcos-evm/eth/gas/EthTxGasSettlement.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
BOOST_AUTO_TEST_CASE(none_mode_does_not_debit)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 100'000;

    auto const out = debitIntrinsicGas(message, {.mode = IntrinsicDebitMode::None});
    BOOST_REQUIRE(out.ok);
    BOOST_CHECK_EQUAL(out.debitAmount, 0);
    BOOST_CHECK_EQUAL(message.gas, 100'000);
}

BOOST_AUTO_TEST_CASE(auth_only_mode_debits_auth_cost)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 100'000;

    auto const out = debitIntrinsicGas(message,
        {.mode = IntrinsicDebitMode::AuthOnly,
            .authorizationListPresent = true,
            .authTupleCount = 1});
    BOOST_REQUIRE(out.ok);
    BOOST_CHECK_EQUAL(out.debitAmount, gas::calcAuthTupleIntrinsicGas(1));
    BOOST_CHECK_EQUAL(message.gas, 100'000 - gas::calcAuthTupleIntrinsicGas(1));
}

BOOST_AUTO_TEST_CASE(eip7623_mode_reports_structured_calldata_failure)
{
    bcos::bytes calldata{0x01};
    auto const calldataGas = gas::calcEip7623CalldataGas(bcos::bytesConstRef(calldata));

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = calldataGas - 1;
    message.input_data = calldata.data();
    message.input_size = calldata.size();

    auto const out = debitIntrinsicGas(message, {.mode = IntrinsicDebitMode::Eip7623});
    BOOST_REQUIRE(!out.ok);
    BOOST_CHECK_EQUAL(static_cast<int>(out.failure),
        static_cast<int>(IntrinsicDebitFailure::CalldataOutOfGas));
    BOOST_CHECK_EQUAL(message.gas, calldataGas - 1);
}

BOOST_AUTO_TEST_CASE(opstack_entry_mode_debits_intrinsic_without_floor_mapping)
{
    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 100'000;

    auto const out = debitIntrinsicGas(message, {.mode = IntrinsicDebitMode::OpStackEntry});
    BOOST_REQUIRE(out.ok);
    BOOST_CHECK_EQUAL(out.debitAmount, gas::TX_BASE_GAS);
    BOOST_CHECK_EQUAL(message.gas, 100'000 - gas::TX_BASE_GAS);
}
}  // namespace bcos::evm::test
```

- [ ] **Step 2: Run failing build**

Run: `cmake --build build --target DebitIntrinsicGasTest 2>&1 | rtk err`  
Expected: FAIL because headers do not exist.

- [ ] **Step 3: Implement `adoptEvmcResult.h`**

```cpp
#pragma once

#include "bcos-evm/eth/EVMCResult.h"
#include <evmc/evmc.hpp>

namespace bcos::crypto
{
class Hash;
}

namespace bcos::evm
{
inline EVMCResult adoptEvmcResult(evmc::Result&& result, bcos::crypto::Hash const& hashImpl)
{
    auto raw = result.release_raw();
    auto [status, ignored] = evmcStatusToErrorMessage(hashImpl, raw.status_code);
    (void)ignored;
    return EVMCResult(raw, status);
}
}  // namespace bcos::evm
```

- [ ] **Step 4: Implement `debitIntrinsicGas.h`**

Implement the interfaces exactly as listed. Behavior:

- `None`: return ok, no mutation.
- `AuthOnly`: require auth gas if auth list is present, subtract only auth cost.
- `Eip7623`: perform existing Eth/Fisco 7623 minimum + calldata checks, subtract `preExecutionDebit + authCost`.
- `OpStackEntry`: compute `preExecutionDebit + authCost`; if insufficient, return `OpStackIntrinsicOutOfGas`; otherwise subtract. Do not call `executeEntryFloorDataGasCheck` here.

- [ ] **Step 5: Register and run tests**

Add to `bcos-evm/test/CMakeLists.txt`:

```cmake
add_executable(DebitIntrinsicGasTest eth/DebitIntrinsicGasTest.cpp)
target_include_directories(DebitIntrinsicGasTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(DebitIntrinsicGasTest PRIVATE bcos-evm-eth)
add_test(NAME DebitIntrinsicGas COMMAND DebitIntrinsicGasTest)
```

Run:

```bash
cmake --build build --target DebitIntrinsicGasTest 2>&1 | rtk err
ctest --test-dir build -R "^DebitIntrinsicGas$" --output-on-failure 2>&1 | rtk err
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/eth/orchestration/adoptEvmcResult.h bcos-evm/eth/orchestration/debitIntrinsicGas.h bcos-evm/test/eth/DebitIntrinsicGasTest.cpp bcos-evm/test/CMakeLists.txt
rtk git commit -m "feat(evm): add orchestration gas and result helpers"
```

---

### Task 1B: OpStack pre-debit split + message sync bug fix

**Files:**

- Create: `bcos-evm/opstack/OpStackPreDebitEntry.h`
- Create: `bcos-evm/opstack/OpStackPreDebitEntry.cpp`
- Create: `bcos-evm/opstack/OpStackExecuteMessageTestHook.h`
- Create: `bcos-evm/test/opstack/OpStackIntrinsicGasSyncTest.cpp`
- Create: `bcos-evm/test/opstack/OpStackPreDebitOrderTest.cpp`
- Modify: `bcos-evm/opstack/OpStackExecuteViaHost.cpp`
- Modify: `bcos-evm/CMakeLists.txt`
- Modify: `bcos-evm/test/CMakeLists.txt`

**Interfaces:**

- Produces:
  - `struct OpStackPreDebitEntryInput { evmc_message const& message; state::State& state; uint64_t gasLimit; bool skipTransactionChecks; bcos::bytesConstRef inputData; uint64_t& floorDataGasOut; }`
  - `std::optional<EVMCResult> opStackPreDebitEntry(OpStackPreDebitEntryInput const& input)`
  - Test-only `bcos::evm::opstack::test::setExecuteMessageSpy`, `clearExecuteMessageSpy`, `maybeCallExecuteMessageSpy`

- [ ] **Step 1: Extract OpStack ③½ logic**

Move only balance and floor checks from `executeEntryChecks` into `OpStackPreDebitEntry.cpp`. This function must not subtract gas.

- [ ] **Step 2: Add test-only executeMessage hook**

Create `OpStackExecuteMessageTestHook.h` guarded by `#ifdef BCOS_EVM_TESTING`. In `OpStackExecuteViaHost.cpp`, route calls through:

```cpp
ExecuteMessageOutput callExecuteMessage(ExecuteMessageInput input)
{
#ifdef BCOS_EVM_TESTING
    if (auto output = bcos::evm::opstack::test::maybeCallExecuteMessageSpy(input);
        output.has_value())
    {
        return std::move(*output);
    }
#endif
    return executeMessage(std::move(input));
}
```

- [ ] **Step 3: Fix OpStack message ownership**

In both normal and deposit paths:

- create local `evmc_message message = input.message`;
- capture `originalGasLimit = input.message.gas`;
- run `opStackPreDebitEntry` before `debitIntrinsicGas`;
- call `debitIntrinsicGas(message, {.mode = IntrinsicDebitMode::OpStackEntry, ...})`;
- pass `message` to `executeMessage`;
- keep OpStack settlement using original gas limit and `floorDataGas`.

- [ ] **Step 4: Write `OpStackIntrinsicGasSyncTest` with spy**

The spy captures `ExecuteMessageInput.message.gas` and returns a simple successful `ExecuteMessageOutput`. Assert captured gas equals `originalGasLimit - intrinsicDebit`.

- [ ] **Step 5: Write `OpStackPreDebitOrderTest`**

Directly test `opStackPreDebitEntry`:

- insufficient balance returns `EVMC_INSUFFICIENT_BALANCE`;
- floor failure returns `EVMC_OUT_OF_GAS`;
- `floorDataGasOut` is written before debit phase and no gas subtraction happens in this helper.

- [ ] **Step 6: CMake for test-only source build**

Add `OpStackPreDebitEntry.cpp` to `BCOS_EVM_OP_SOURCES`. For `OpStackIntrinsicGasSyncTest`, do not link `bcos-evm-op`; compile OpStack sources into the test with `BCOS_EVM_TESTING`:

```cmake
add_executable(OpStackIntrinsicGasSyncTest
    opstack/OpStackIntrinsicGasSyncTest.cpp
    ../opstack/OpStackExecuteViaHost.cpp
    ../opstack/OpStackPreCheck.cpp
    ../opstack/OpStackTxExecutor.cpp
    ../opstack/RollupCost.cpp
    ../opstack/OpStackFee.cpp
    ../opstack/OpStackFloorGas.cpp
    ../opstack/L1BlockStorage.cpp
    ../opstack/L1BlockPredeploy.cpp
    ../opstack/OpStackPreDebitEntry.cpp)
target_compile_definitions(OpStackIntrinsicGasSyncTest PRIVATE BCOS_EVM_TESTING)
target_include_directories(OpStackIntrinsicGasSyncTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(OpStackIntrinsicGasSyncTest PRIVATE
    bcos-evm-eth bcos-task evmone::evmone bcos-framework ledger
    bcos-protocol bcos-utilities bcos-crypto codec)
add_test(NAME OpStackIntrinsicGasSync COMMAND OpStackIntrinsicGasSyncTest)
```

`OpStackPreDebitOrderTest` may link ordinary `bcos-evm-op`.

- [ ] **Step 7: Run OpStack tests**

```bash
cmake --build build --target OpStackIntrinsicGasSyncTest OpStackPreDebitOrderTest OpStackExecuteViaHostSmokeTest 2>&1 | rtk err
ctest --test-dir build -R "^(OpStackIntrinsicGasSync|OpStackPreDebitOrder|OpStackExecuteViaHost)$" --output-on-failure 2>&1 | rtk err
```

Expected: PASS.

- [ ] **Step 8: Commit**

```bash
rtk git add bcos-evm/opstack/OpStackPreDebitEntry.* bcos-evm/opstack/OpStackExecuteMessageTestHook.h bcos-evm/opstack/OpStackExecuteViaHost.cpp bcos-evm/CMakeLists.txt bcos-evm/test/
rtk git commit -m "fix(evm): pass debited OpStack message into executeMessage"
```

---

### Task 2: `OrchestrationContext` + sync pipeline + Eth migration

**Files:**

- Create: `bcos-evm/eth/orchestration/OrchestrationContext.h`
- Create: `bcos-evm/eth/orchestration/OrchestrationHooks.h`
- Create: `bcos-evm/eth/orchestration/buildExecuteMessageInput.h`
- Create: `bcos-evm/eth/orchestration/captureSettlementSnapshot.h`
- Create: `bcos-evm/eth/orchestration/normalizeIncludedTxVmerr.h`
- Create: `bcos-evm/eth/orchestration/OrchestrationPipeline.h`
- Create: `bcos-evm/eth/orchestration/OrchestrationPipeline.cpp`
- Create: `bcos-evm/test/eth/OrchestrationPipelineTest.cpp`
- Modify: `bcos-evm/eth/ExecuteViaEth.cpp`
- Modify: `bcos-evm/CMakeLists.txt`
- Modify: `bcos-evm/test/CMakeLists.txt`

**Interfaces:**

- Produces:
  - `enum class OrchestrationExitKind { None, PreExecuteRejected, PreDebitRejected, IntrinsicRejected, KernelCompleted, ExceptionMapped }`
  - `class OrchestrationContext` with constructor `OrchestrationContext(state::StateView const&, evmc_message, RevisionConfig, intx::uint256 gasPrice)`; `= delete` copy/move; `extension` set by wrapper after construction
  - `struct OrchestrationHooks` with sync hooks (no `buildExtension`), `mapIntrinsicFailure`, and `mapException(std::exception_ptr)`
  - `void runOrchestration(OrchestrationContext& ctx, OrchestrationHooks const& hooks)`

- [ ] **Step 1: Write failing pipeline tests**

`OrchestrationPipelineTest` must verify:

- context is constructed from `StateView` and cannot be default-constructed (`static_assert(!std::is_default_constructible_v<OrchestrationContext>)`), nor copied/moved (`static_assert(!std::is_copy_constructible_v<OrchestrationContext> && !std::is_move_constructible_v<OrchestrationContext>)`);
- early exit sets `exitKind` and does not call build/execute hooks;
- intrinsic failure maps via `mapIntrinsicFailure`;
- exception thrown from `preKernel` maps via `mapException(std::exception_ptr)`, and the kernel does not revert state itself;
- `captureSettlementSnapshot` leaves `ctx.snapshot` untouched for non-`Eip7623` modes.

- [ ] **Step 2: Implement context and hooks**

`OrchestrationContext` constructor:

```cpp
OrchestrationContext(state::StateView const& stateView, evmc_message inputMessage,
    bcos::evm_standard::RevisionConfig inputRevisionConfig, intx::uint256 inputGasPrice)
  : message(inputMessage),
    originalGasLimit(inputMessage.gas),
    state(stateView),
    gasPrice(inputGasPrice),
    revisionConfig(inputRevisionConfig)
{
    execution::setWarmDestinationFromKind(txProps, message.kind);
}

OrchestrationContext(OrchestrationContext const&) = delete;
OrchestrationContext& operator=(OrchestrationContext const&) = delete;
OrchestrationContext(OrchestrationContext&&) = delete;
OrchestrationContext& operator=(OrchestrationContext&&) = delete;

// HostExtension* extension; set by wrapper to &localExtension (borrow) before runOrchestration.
```

- [ ] **Step 3: Implement `runOrchestration`**

Pipeline:

- validate `ctx.inputs.vm` and `ctx.inputs.hashImpl` outside catch;
- wrap steps ②–⑪ in `try/catch`;
- on ③/③½/④ early-exit, set `earlyExit` and specific `exitKind`, then return without post-settle;
- step ⑥ reads `ctx.extension` (no `buildExtension` hook) and `ctx.gasPrice`;
- step ⑨ `captureSettlementSnapshot` only fills snapshot when `intrinsicPolicy.mode == Eip7623`;
- on exception, set `exitKind = ExceptionMapped` and call `mapException(ctx, std::current_exception())`; the kernel does **not** revert state — chain-specific `state.revert()`/checkpoint is performed inside the hook.

- [ ] **Step 4: Move Eth normalize helpers**

Move `isTopLevelIncludedTxVmError` / normalize helper from `ExecuteViaEth.cpp` to `normalizeIncludedTxVmerr.h`.

- [ ] **Step 5: Migrate `executeViaEth`**

Use `OrchestrationContext ctx{*input.stateView, input.message, input.revisionConfig, input.gasPrice}`. Construct `EthHostExtension` on the wrapper stack and set `ctx.extension = &ethExtension` before calling `runOrchestration`. Configure hooks:

- `preExecute`: existing Eth precheck + 1559 gas price normalization (may overwrite `ctx.gasPrice`);
- `intrinsicPolicy.mode`: `Eip7623`, `AuthOnly`, or `None`;
- `mapIntrinsicFailure`: Eth `OutOfGasLimit`;
- `preKernel`: `canTransfer` (read-only);
- `postAdopt`: included-tx normalize.

Wrapper output mapping after pipeline: `logs = convertLogs(ctx.kernelOutput.logs)`, `stateDiff = std::move(ctx.kernelOutput.stateDiff)`.

- [ ] **Step 6: Run Eth regression**

```bash
cmake --build build --target OrchestrationPipelineTest ExecuteViaEthFixtureTest Eip7623PrecheckTest EthExecuteViaEth1559GasPriceTest EthIncludedTxVmerrTest 2>&1 | rtk err
ctest --test-dir build -R "^(OrchestrationPipeline|ExecuteViaEthFixture|Eip7623Precheck|EthExecuteViaEth1559GasPrice|EthIncludedTxVmerr)$" --output-on-failure 2>&1 | rtk err
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
rtk git add bcos-evm/eth/orchestration/ bcos-evm/eth/ExecuteViaEth.cpp bcos-evm/CMakeLists.txt bcos-evm/test/
rtk git commit -m "feat(evm): add sync runOrchestration pipeline and migrate executeViaEth"
```

---

### Task 3: Migrate Fisco and OpStack wrappers

**Files:**

- Modify: `bcos-evm/bcos/ExecuteViaHost.cpp`
- Modify: `bcos-evm/opstack/OpStackExecuteViaHost.cpp`
- Modify: `bcos-evm/test/eth/OrchestrationPipelineTest.cpp`

**Interfaces:**

- Consumes: Task 2 pipeline and Task 1B OpStack pre-debit helper.

- [ ] **Step 1: Migrate Fisco wrapper**

Configure hooks in `bcos/ExecuteViaHost.cpp`:

- `prepareMessage`: `deriveMessage`;
- `preExecute`: auth check;
- `intrinsicPolicy.mode`: `Eip7623` only for `web3Tx && input.revisionConfig.eth().eip7623`, otherwise `None`;
- `mapIntrinsicFailure`: use existing `makeErrorEVMCResult` with `TransactionStatus::OutOfGas`, correct reason string, and `fixErrorHandling`;
- before pipeline: construct `FiscoHostExtension` (with deps) on the wrapper stack and set `ctx.extension`;
- `preKernel`: `maybeTransferValue`, `BALANCE_TRANSFER_GAS`, empty-code check — **state-mutating**, signals failure by `throw` (no early-exit flag);
- `mapException`: `std::rethrow_exception` and catch `OutOfGas`, `NotEnoughCashError`, `NotFoundCodeError`, `std::exception` exactly as current code, **and** `if (ctx.state.has_checkpoint()) ctx.state.revert();` (checkpoint is wrapper/`prepareMessage`-owned, never the kernel);
- wrapper output mapping: `logs = convertLogs(ctx.kernelOutput.logs)` (clear on failure per `fix_revert_logs`); `stateDiff = std::move(ctx.kernelOutput.stateDiff)` **only on success**;
- wrapper after pipeline: preserve `gas_left < 0` check.

- [ ] **Step 2: Migrate OpStack normal wrapper**

Keep these outside pipeline (wrapper order):

- `gasPoolSubGasHook` (before buyGas);
- `GasPoolReturnGuard` (armed once `buyGas` succeeds);
- `co_await buyGas` → then set `ctx.gasPrice = txData.m_effectiveGasPrice`;
- construct `OpHostExtension(&ctx.state)` and set `ctx.extension`;
- `co_await refundGas`;
- L1/operator receipt meta.

`txData.m_state = &ctx.state` (borrow); do not keep a second debitable `txData.m_message`.

Hooks:

- `preExecute`: `opStackPreCheck`;
- `preDebitEntry`: `opStackPreDebitEntry` — keep intrinsic→canTransfer→floor failure precedence so included-tx `status_code` does not drift;
- `intrinsicPolicy.mode`: `OpStackEntry`;
- `mapIntrinsicFailure`: `OutOfGasLimit`;
- `postSettle`: only shared gas math into `txData` (`postExecuteGasSettlement` stays in `opstack/`).

**Settlement/refund matrix (hard contract):** after `buyGas` succeeds (`guard.armed`), **every** `exitKind` (`PreDebitRejected`, `IntrinsicRejected`, `canTransfer` reject, `KernelCompleted`) must run `postExecuteGasSettlement` + `co_await refundGas` + `ctx.state.build_diff()`. Failures **before** buyGas (`opStackPreCheck`, `gasPoolSubGasHook`, buyGas itself) return directly with no settle/refund.

Wrapper output mapping (tail, after all state changes): `stateDiff = ctx.state.build_diff()` — never `ctx.kernelOutput.stateDiff`, or fee/refund balance deltas are lost.

- [ ] **Step 3: Migrate OpStack deposit wrapper**

Keep outside pipeline:

- mint + checkpoint before pipeline;
- success commit + nonce bump after kernel completion;
- failure revert + nonce bump + `gasUsed = gasLimit` + `returnDepositPoolGas` for early-exit/failure;
- `stateDiff = ctx.state.build_diff()` as the tail step on every branch (after commit/revert + nonce).

- [ ] **Step 4: Delete duplicate code**

Remove local `adoptResult` copies, duplicated `ExecuteMessageInput` assembly, and old `executeEntryChecks` bundle.

- [ ] **Step 5: Full regression**

```bash
ctest --test-dir build -R "^(ExecuteViaHostSmoke|ExecuteViaHostImportedFixture|Bcos7623Precheck|BcosAuthOrchestratorHook|Bcos6780Selfdestruct|OpStackExecuteViaHost|OpStackIntrinsicGasSync|OpStackPreDebitOrder|DepositMint|DepositNoFeeRouting|OpStackSettlement)$" --output-on-failure 2>&1 | rtk err
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/bcos/ExecuteViaHost.cpp bcos-evm/opstack/OpStackExecuteViaHost.cpp bcos-evm/test/
rtk git commit -m "feat(evm): migrate Fisco and OpStack wrappers to runOrchestration"
```

---

### Task 4: ADR-019 + capability matrix

**Files:**

- Create: `bcos-evm/docs/adr/019-orchestration-pipeline.md`
- Modify: `bcos-evm/capability-matrix.md`
- Create or modify: `bcos-evm/docs/adr/005-*.md` if ADR-005 exists in this worktree

- [ ] **Step 1: Write ADR-019**

Include: Context, Decision, `IntrinsicDebitMode`, `OrchestrationExitKind`, wrapper-out async fee/state machine, test-only spy seam, consequences.

- [ ] **Step 2: Update capability matrix**

Mark TE baseline paths as \"via `runOrchestration`\".

- [ ] **Step 3: Update ADR-005 if present**

Run: `rtk find "*005*" bcos-evm/docs 2>/dev/null`  
If present, add `eth/orchestration/` boundary and wrapper-out fee/state-machine note.

- [ ] **Step 4: Verify seam**

```bash
rtk grep -n '#include.*bcos-evm/bcos\\|#include.*bcos-evm/opstack' bcos-evm/eth/orchestration/ bcos-evm/eth/ExecuteViaEth.cpp 2>&1 | rtk err
```

Expected: no matches.

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/docs/adr/019-orchestration-pipeline.md bcos-evm/capability-matrix.md bcos-evm/docs/adr/
rtk git commit -m "docs(evm): add ADR-019 orchestration pipeline"
```

---

## Self-Review

| Grilling conclusion | Covered by |
| --- | --- |
| `IntrinsicDebitMode` | Task 1A |
| Context construction-valid | Task 2 |
| Structured intrinsic failure + `mapIntrinsicFailure` | Task 1A, Task 2, Task 3 |
| `mapException(std::exception_ptr)` | Task 2, Task 3 |
| catch covers ②–⑪ | Task 2 |
| 5 task split, no interim frame | This plan |
| test-only OpStack spy seam | Task 1B |
| `runOrchestration` sync `void` | Task 2 |
| `OrchestrationExitKind` | Task 2, Task 3 |
| State/message single ownership; `txData.m_state = &ctx.state` (Q14) | Task 2, Task 3 |
| No `buildExtension`; wrapper-built `ctx.extension`; `ctx` non-copyable/movable (Q15) | Task 2, Task 3 |
| `gasPrice` constructor param + `preExecute`/buyGas overwrite (Q16) | Task 2, Task 3 |
| `captureSettlementSnapshot` `Eip7623`-only; OpStack settlement in `opstack/` (Q17) | Task 2, Task 3 |
| `stateDiff`/`logs` wrapper-owned; OpStack `build_diff()` after wrapper state changes (Q18) | Task 2, Task 3 |
| OpStack settlement/refund matrix + intrinsic→canTransfer→floor precedence (Q19) | Task 3 |
| `preKernel` state-mutating throw; `mapException` owns `state.revert()`/checkpoint (Q20) | Task 2, Task 3 |

---

## Execution Handoff

**Plan complete and saved to `docs/superpowers/plans/2026-06-23-orchestration-pipeline.md`. Two execution options:**

**1. Subagent-Driven (recommended)** — dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** — execute tasks in this session using executing-plans, batch execution with checkpoints

**Which approach?**
