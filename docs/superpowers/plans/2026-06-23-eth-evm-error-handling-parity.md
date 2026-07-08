# ETH EVM Error Handling Parity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `bcos-evm/eth` kernel and ETH reference orchestration match geth/EVMC error-state semantics for value transfer, nested call/create failures, included top-level vmerr settlement, CREATE error families, and status taxonomy.

**Architecture:** Keep path-specific behavior in orchestration (`ExecuteViaEth`, ETH TE, BCOS/OPStack wrappers) and put geth-compatible frame semantics in the shared kernel (`executeMessage`, `EthHost`, `PrecompileRouter`, `CreateExecution`). Add focused tests before every behavior change. Extract shared top-level gas settlement so the GST adapter and ETH TE cannot drift.

**Tech Stack:** C++20, EVMC/evmone, Boost.Test, CMake/CTest, `bcos-evm` shared kernel, `transaction-executor` ETH TE, geth source at `/Users/octopus/octo/code/blockchain-impl/go-ethereum`.

## Global Constraints

- Scope decision: shared kernel and ETH reference orchestration must be equivalent to geth/EVMC; BCOS/OPStack orchestration deviations remain isolated in extension layers.
- Do not modify legacy `bcos-executor` / DAG / `HostContext`.
- Do not change BCOS `BALANCE_TRANSFER_GAS` 21000, FISCO auth, OPStack deposit/L1Block/operator-fee product semantics.
- Do not guess depth-exceeded EVMC status; characterize evmone's current status and require `gas_left` to be preserved.
- Reference path should default to corrected semantics; production BCOS/OPStack consensus-sensitive fixes may be feature-gated in implementation if the executor path requires it.
- Use `rtk` prefix for shell commands in this repo.
- Commit after each task; do not combine behavior changes from separate tasks.

---

## File Structure

### Kernel Envelope

- Modify `bcos-evm/eth/precompiled/PrecompileRouter.cpp`
  - Responsibility: shared precompile dispatch envelope for depth 0 empty-code precompile calls and nested `EthHost::call`.
  - Change: check balance before checkpoint, then checkpoint, then transfer, then precompile dispatch, then commit/revert.
- Modify `bcos-evm/test/eth/PrecompileRouterEnvelopeTest.cpp`
  - Responsibility: top-level and nested precompile envelope equivalence.
  - Change: update insufficient balance gas expectations and add value-paying failure rollback coverage.
- Modify `bcos-evm/test/CMakeLists.txt`
  - Responsibility: build and register new/expanded ETH parity tests if missing.

### Nested Call/Create Gas Semantics

- Modify `bcos-evm/eth/state/EthHost.cpp`
  - Responsibility: EVMC host `call` frame semantics.
  - Change: preserve gas on the `transferValue` insufficient-balance path. NOTE: for non-CREATE calls this path is only reached when `dispatchPrecompile` returns `NotApplicable`; the CALL/CALLCODE value-transfer insufficient-balance case is intercepted earlier by the router (Task 1) and returns before `transferValue`. So this EthHost edit primarily covers the **nested CREATE/CREATE2 endowment** insufficient-balance case.
- Modify `bcos-evm/eth/executeMessage.cpp`
  - Responsibility: top-level (`depth == 0`) reference execution wrapper.
  - Change: preserve gas in helper result. NOTE: both call sites are top-level value-transfer / CREATE-endowment paths (geth handles these at preCheck as consensus rejection; Task 5 classifies `EVMC_INSUFFICIENT_BALANCE` as `ConsensusRejected`, so `gas_left` here does not affect included-tx settlement). The change is for taxonomy/EVMC-spec consistency, not nested-frame parity.
- Create `bcos-evm/test/eth/InsufficientBalanceGasLeftTest.cpp`
  - Responsibility: characterize gas preservation for CALL/CALLCODE/CREATE/CREATE2 and depth.

### Shared ETH Settlement

- Modify `bcos-evm/eth/eip/EthTxGasSettlement.h`
  - Responsibility: pure gas settlement helpers.
  - Change: add `finalizeEthTxGasUsed(...)`.
- Modify `bcos-evm/specs-tests/src/ExecuteViaEthAdapter.cpp`
  - Responsibility: GST/EEST adapter settlement.
  - Change: replace local settlement branch with shared helper.
- Modify `transaction-executor/bcos-transaction-executor/EthTransactionExecutorImpl.h`
  - Responsibility: ETH TE transaction settlement.
  - Change: consume `m_topLevelIncludedTxVmError` via shared helper.
- Modify `bcos-evm/test/eth/EthIncludedTxVmerrTest.cpp`
  - Responsibility: pure helper coverage for included vmerr.
  - Change: add direct helper tests for top-level vmerr and regular success path.
- Modify `transaction-executor/tests/EthTxGasSettlementTest.cpp`
  - Responsibility: TE-facing helper characterization.
  - Change: add shared helper parity tests.

### CREATE Error Parity

- Modify `bcos-evm/eth/state/CreateExecution.h`
  - Responsibility: CREATE target binding and code-deposit checks.
  - Change: add missing geth-aligned checks only after characterization proves gaps.
- Modify `bcos-evm/eth/executeMessage.cpp`
  - Responsibility: top-level CREATE pre-execution.
  - Change: add max initcode / collision prechecks if characterization confirms gaps.
- Modify `bcos-evm/eth/state/EthHost.cpp`
  - Responsibility: nested CREATE path.
  - Change: add collision / max initcode / depth behavior if characterization confirms gaps.
- Create `bcos-evm/test/eth/CreateErrorParityTest.cpp`
  - Responsibility: geth-aligned CREATE error-family characterization and regression.

### Taxonomy, Status, Docs

- Create `bcos-evm/eth/EthTxOutcome.h`
  - Responsibility: small, dependency-light outcome enum and helper names used by orchestration/tests.
- Modify `bcos-evm/eth/EVMCResult.cpp`
  - Responsibility: EVMC status to transaction status/error metadata.
  - Change: no reachable `UnknownEVMCStatus` for known EVMC statuses.
- Modify `bcos-evm/eth/ExecuteViaEth.cpp`
  - Responsibility: top-level outcome classification and exception handling.
  - Change: use taxonomy helper names and narrow unknown exception handling behavior.
- Create `bcos-evm/docs/adr/017-eth-evm-error-taxonomy.md`
  - Responsibility: record the outcome taxonomy and geth/EVMC mapping.
- Modify `bcos-evm/capability-matrix.md`
  - Responsibility: normative capability tracking.
  - Change: add rows from spec §6.3.

---

### Task 1: PrecompileRouter Envelope Parity

**Files:**
- Modify: `bcos-evm/eth/precompiled/PrecompileRouter.cpp`
- Modify: `bcos-evm/test/eth/PrecompileRouterEnvelopeTest.cpp`
- Modify: `bcos-evm/test/CMakeLists.txt`

**Interfaces:**
- Consumes: `PrecompileRouterOutput dispatchPrecompile(PrecompileRouterInput const& input)`
- Produces: `dispatchPrecompile(...)` with geth-compatible envelope: balance check before checkpoint, transfer inside checkpoint, failure reverts value transfer, `EVMC_INSUFFICIENT_BALANCE` preserves `input.message.gas`.

- [ ] **Step 1: Write the failing insufficient-balance expectation**

Replace the two `gasLeft == 0` checks in `bcos-evm/test/eth/PrecompileRouterEnvelopeTest.cpp`:

```cpp
    BOOST_REQUIRE_EQUAL(depth0.status, EVMC_INSUFFICIENT_BALANCE);
    BOOST_REQUIRE_EQUAL(depth0.gasLeft, message.gas);
    BOOST_REQUIRE_EQUAL(depth1.status, EVMC_INSUFFICIENT_BALANCE);
    BOOST_REQUIRE_EQUAL(depth1.gasLeft, message.gas);
```

- [ ] **Step 2: Add value-paying precompile failure rollback test**

Append this test before the closing namespace in `bcos-evm/test/eth/PrecompileRouterEnvelopeTest.cpp`:

```cpp
BOOST_AUTO_TEST_CASE(value_transfer_rolls_back_when_precompile_runs_out_of_gas)
{
    auto const sender = addressFromLastByte(0x01);
    auto const modexp = precompileAddress(0x05);
    // valueTransferMessage takes std::array<uint8_t, 4> (fixed). modexp with a tiny input still
    // dispatches and runs OOG at gas=1 (min cost >= 200), which is all this rollback test needs.
    std::array<uint8_t, 4> inputBytes{0x00, 0x00, 0x00, 0x01};
    auto message = valueTransferMessage(sender, modexp, weiValue(100), inputBytes);
    message.gas = 1;

    state::test::InMemoryStateView view0;
    state::State state0(view0);
    state0.set_balance(sender, 1'000'000);
    auto depth0 = runDepth0(state0, message);

    state::test::InMemoryStateView view1;
    state::State state1(view1);
    state1.set_balance(sender, 1'000'000);
    auto depth1 = runDepth1(state1, message);

    BOOST_REQUIRE_NE(depth0.status, EVMC_SUCCESS);
    BOOST_REQUIRE_NE(depth1.status, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(depth0.senderBalance, 1'000'000);
    BOOST_CHECK_EQUAL(depth1.senderBalance, 1'000'000);
    BOOST_CHECK_EQUAL(depth0.recipientBalance, 0);
    BOOST_CHECK_EQUAL(depth1.recipientBalance, 0);
}
```

- [ ] **Step 3: Ensure the test target is registered**

If `PrecompileRouterEnvelopeTest` is not already present in `bcos-evm/test/CMakeLists.txt`, add this block near the other `eth/` tests:

```cmake
add_executable(PrecompileRouterEnvelopeTest eth/PrecompileRouterEnvelopeTest.cpp)
target_include_directories(PrecompileRouterEnvelopeTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)
target_link_libraries(PrecompileRouterEnvelopeTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME PrecompileRouterEnvelope COMMAND PrecompileRouterEnvelopeTest)
```

- [ ] **Step 4: Run the test to verify it fails**

Run:

```bash
rtk cmake --build build --target PrecompileRouterEnvelopeTest
rtk ctest --test-dir build -R PrecompileRouterEnvelope --output-on-failure
```

Expected: failure showing `gasLeft` is `0` instead of `message.gas`, or sender/recipient balances show value transfer was not rolled back.

- [ ] **Step 5: Implement geth-compatible router envelope**

Replace the private helper and beginning of `dispatchPrecompile` in `bcos-evm/eth/precompiled/PrecompileRouter.cpp` with:

```cpp
evmc::Result makeInsufficientBalanceResult(int64_t gasLeft) noexcept
{
    evmc_result result{};
    result.status_code = EVMC_INSUFFICIENT_BALANCE;
    result.gas_left = gasLeft;
    return evmc::Result(result);
}

void finalizeEnvelope(state::State& state, PrecompileRouterOutput& output)
{
    if (output.result.status_code == EVMC_SUCCESS)
    {
        output.gasRefund = static_cast<int64_t>(state.get_refund());
        state.commit();
    }
    else
    {
        state.revert();
    }
}
```

Then replace lines from the first value-transfer block through `input.state.checkpoint();` with:

```cpp
    bool const hasValue = !state::isZeroBytes32(input.message.value) && !input.skipValueTransfer;
    if (hasValue)
    {
        auto const value = state::fromEvmC(input.message.value);
        if (!canTransfer(input.state, input.message.sender, value))
        {
            output.outcome = PrecompileDispatchOutcome::Dispatched;
            output.result = makeInsufficientBalanceResult(input.message.gas);
            return output;
        }
    }

    input.state.checkpoint();
    if (hasValue)
    {
        transfer(input.state, input.message.sender, input.target, state::fromEvmC(input.message.value));
    }
```

- [ ] **Step 6: Run the test to verify it passes**

Run:

```bash
rtk cmake --build build --target PrecompileRouterEnvelopeTest
rtk ctest --test-dir build -R PrecompileRouterEnvelope --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
rtk git add bcos-evm/eth/precompiled/PrecompileRouter.cpp bcos-evm/test/eth/PrecompileRouterEnvelopeTest.cpp bcos-evm/test/CMakeLists.txt
rtk git commit -m "$(cat <<'EOF'
fix(eth): align precompile value-transfer envelope with geth

EOF
)"
```

---

### Task 2: Nested CALL/CREATE Gas Preservation

**Files:**
- Modify: `bcos-evm/eth/state/EthHost.cpp`
- Modify: `bcos-evm/eth/executeMessage.cpp`
- Create: `bcos-evm/test/eth/InsufficientBalanceGasLeftTest.cpp`
- Modify: `bcos-evm/test/CMakeLists.txt`

**Interfaces:**
- Consumes: `EthHost::call(const evmc_message& msg) noexcept`, `executeMessage(ExecuteMessageInput input)`.
- Produces: nested insufficient-balance results preserve the frame gas, matching geth `ErrInsufficientBalance`. Path split (verified against `EthHost::call`): CALL/CALLCODE value-transfer insufficient-balance is handled by the **router** (Task 1, `dispatchPrecompile` returns before `transferValue`); nested CREATE/CREATE2 endowment insufficient-balance is handled by this task's `EthHost::transferValue` edit.

- [ ] **Step 1: Create the failing test file**

Create `bcos-evm/test/eth/InsufficientBalanceGasLeftTest.cpp`:

```cpp
#define BOOST_TEST_MODULE InsufficientBalanceGasLeftTest

#include "bcos-evm/eth/state/EthHost.hpp"
#include "state/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
namespace
{
evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

evmc_uint256be weiValue(uint8_t value)
{
    evmc_uint256be out{};
    out.bytes[31] = value;
    return out;
}

state::BlockHashes emptyBlockHashes()
{
    return [](int64_t) { return evmc_bytes32{}; };
}

evmc::Result runHostCall(state::State& state, evmc_message message)
{
    evmc::VM vm{evmc_create_evmone()};
    evmc_tx_context txContext{};
    txContext.block_gas_limit = 30'000'000;
    bcos::evm_standard::RevisionConfig cfg{};
    cfg.revision = EVMC_PRAGUE;
    cfg.warm_access = true;
    state::EthHost host(state, txContext, cfg, vm, emptyBlockHashes(), nullptr, false);
    message.depth = 1;
    return host.call(message);
}
}  // namespace

// NOTE: this is a router-path cross-check (Task 1). A plain CALL to an empty/precompile address
// goes through dispatchPrecompile, which handles the value-transfer insufficient-balance case and
// returns BEFORE EthHost::transferValue. We deliberately pick a non-precompile address (>= 0x0a) so
// dispatchPrecompile treats it as an empty account; the router still owns the insufficient-balance
// envelope, so this asserts the Task 1 fix is observable through the nested EthHost::call entry.
BOOST_AUTO_TEST_CASE(call_insufficient_balance_preserves_frame_gas_via_router)
{
    auto const sender = addressFromLastByte(0x01);
    auto const target = addressFromLastByte(0x0a);  // not a precompile (0x01..0x09)
    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(sender, 99);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.gas = 123'456;
    message.sender = sender;
    message.recipient = target;
    message.code_address = target;
    message.value = weiValue(100);

    auto result = runHostCall(state, message);

    BOOST_CHECK_EQUAL(result.status_code, EVMC_INSUFFICIENT_BALANCE);
    BOOST_CHECK_EQUAL(result.gas_left, message.gas);
}

BOOST_AUTO_TEST_CASE(create_insufficient_endowment_preserves_frame_gas)
{
    auto const sender = addressFromLastByte(0x01);
    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(sender, 99);

    evmc_message message{};
    message.kind = EVMC_CREATE;
    message.gas = 222'222;
    message.sender = sender;
    message.value = weiValue(100);

    auto result = runHostCall(state, message);

    BOOST_CHECK_EQUAL(result.status_code, EVMC_INSUFFICIENT_BALANCE);
    BOOST_CHECK_EQUAL(result.gas_left, message.gas);
}

BOOST_AUTO_TEST_CASE(call_depth_characterization_preserves_frame_gas)
{
    auto const sender = addressFromLastByte(0x01);
    auto const target = addressFromLastByte(0x02);
    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(sender, 1'000'000);

    evmc_message message{};
    message.kind = EVMC_CALL;
    message.depth = 1025;
    message.gas = 333'333;
    message.sender = sender;
    message.recipient = target;
    message.code_address = target;

    auto result = runHostCall(state, message);

    BOOST_CHECK_NE(result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(result.gas_left, message.gas);
}

}  // namespace bcos::evm::test
```

- [ ] **Step 2: Register the test target**

Add to `bcos-evm/test/CMakeLists.txt`:

```cmake
add_executable(InsufficientBalanceGasLeftTest eth/InsufficientBalanceGasLeftTest.cpp)
target_include_directories(InsufficientBalanceGasLeftTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)
target_link_libraries(InsufficientBalanceGasLeftTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME InsufficientBalanceGasLeft COMMAND InsufficientBalanceGasLeftTest)
```

- [ ] **Step 3: Run the test to verify it fails**

Run:

```bash
rtk cmake --build build --target InsufficientBalanceGasLeftTest
rtk ctest --test-dir build -R InsufficientBalanceGasLeft --output-on-failure
```

Expected: CALL/CREATE insufficient balance fail because `gas_left` is `0`; depth characterization may fail if the current host does not check depth before evmone.

- [ ] **Step 4: Preserve gas in `EthHost::call` insufficient-balance path**

In `bcos-evm/eth/state/EthHost.cpp`, replace:

```cpp
        return makeResult(EVMC_INSUFFICIENT_BALANCE, 0);
```

with:

```cpp
        return makeResult(EVMC_INSUFFICIENT_BALANCE, callMessage.gas);
```

- [ ] **Step 5: Preserve gas in `executeMessage` helper**

In `bcos-evm/eth/executeMessage.cpp`, replace `makeInsufficientBalanceResult()` with:

```cpp
evmc::Result makeInsufficientBalanceResult(int64_t gasLeft) noexcept
{
    evmc_result result{};
    result.status_code = EVMC_INSUFFICIENT_BALANCE;
    result.gas_left = gasLeft;
    return evmc::Result(result);
}
```

Then replace both call sites:

```cpp
        output.result = makeInsufficientBalanceResult(input.message.gas);
```

- [ ] **Step 6: Add explicit depth preservation if characterization fails for the wrong reason**

If `call_depth_characterization_preserves_frame_gas` returns `EVMC_SUCCESS`, add this check near the start of `EthHost::call` after `callMessage` is resolved:

```cpp
    constexpr int32_t maxCallDepth = 1024;
    if (callMessage.depth > maxCallDepth)
    {
        return makeResult(EVMC_FAILURE, callMessage.gas);
    }
```

If the test fails with a non-success status but `gas_left` is already preserved, do not add this code; keep the characterization result documented in the test name and comments.

- [ ] **Step 7: Run tests**

Run:

```bash
rtk cmake --build build --target InsufficientBalanceGasLeftTest PrecompileRouterEnvelopeTest
rtk ctest --test-dir build -R "InsufficientBalanceGasLeft|PrecompileRouterEnvelope" --output-on-failure
```

Expected: PASS.

- [ ] **Step 8: Commit**

```bash
rtk git add bcos-evm/eth/state/EthHost.cpp bcos-evm/eth/executeMessage.cpp bcos-evm/test/eth/InsufficientBalanceGasLeftTest.cpp bcos-evm/test/CMakeLists.txt
rtk git commit -m "$(cat <<'EOF'
fix(eth): preserve gas for nested insufficient balance

EOF
)"
```

---

### Task 3: Shared ETH Top-Level Gas Settlement

**Files:**
- Modify: `bcos-evm/eth/eip/EthTxGasSettlement.h`
- Modify: `bcos-evm/specs-tests/src/ExecuteViaEthAdapter.cpp`
- Modify: `transaction-executor/bcos-transaction-executor/EthTransactionExecutorImpl.h`
- Modify: `bcos-evm/test/eth/EthIncludedTxVmerrTest.cpp`
- Modify: `transaction-executor/tests/EthTxGasSettlementTest.cpp`

**Interfaces:**
- Produces:
  - `int64_t finalizeEthTxGasUsed(int64_t gasLimit, int64_t legacyGasLeft, int64_t rawGasUsed, bool isWeb3, bool eip7623, bool topLevelIncludedTxVmError, TxGasSettlementSnapshot const& snapshot, uint8_t calldataFloorPerToken) noexcept`
- Consumes: existing `settleTopLevelTransactionGas(...)` and `settleIncludedTopLevelTransactionGas(...)`.

- [ ] **Step 1: Add failing helper tests**

Append to `bcos-evm/test/eth/EthIncludedTxVmerrTest.cpp` inside `BOOST_AUTO_TEST_SUITE(EthIncludedTxVmerrTest)`:

```cpp
BOOST_AUTO_TEST_CASE(finalizeEthTxGasUsed_routes_included_vmerr_to_peak_settlement)
{
    gas::TxGasSettlementSnapshot snapshot;
    snapshot.gasLimit = 10'000'000;
    snapshot.calldata = calcEip7623Components({});
    snapshot.evmGasRefund = 0;

    auto const gasUsed = gas::finalizeEthTxGasUsed(
        10'000'000, 12'500, 10'000'000 - 12'500, true, true, true, snapshot, 10);

    BOOST_CHECK_EQUAL(gasUsed, 9'987'500);
}

BOOST_AUTO_TEST_CASE(finalizeEthTxGasUsed_routes_regular_eip7623_to_top_level_settlement)
{
    gas::TxGasSettlementSnapshot snapshot;
    snapshot.gasLimit = 100'000;
    snapshot.calldata = calcEip7623Components({});
    snapshot.evmGasRefund = 0;

    auto const gasUsed = gas::finalizeEthTxGasUsed(
        100'000, 99'500, 500, true, true, false, snapshot, 10);

    BOOST_CHECK_EQUAL(gasUsed, 21'000);
}
```

- [ ] **Step 2: Add the shared helper**

Append this function to `bcos-evm/eth/eip/EthTxGasSettlement.h` after `settleIncludedTopLevelTransactionGas(...)`:

```cpp
inline int64_t finalizeEthTxGasUsed(int64_t gasLimit, int64_t legacyGasLeft, int64_t rawGasUsed,
    bool isWeb3, bool eip7623, bool topLevelIncludedTxVmError,
    TxGasSettlementSnapshot const& snapshot, uint8_t calldataFloorPerToken) noexcept
{
    if (topLevelIncludedTxVmError && eip7623 && snapshot.gasLimit > 0)
    {
        return settleIncludedTopLevelTransactionGas(gasLimit, legacyGasLeft, snapshot.evmGasRefund,
            calldataFloorPerToken, snapshot.calldata);
    }
    if (snapshot.gasLimit > 0 && isWeb3 && eip7623)
    {
        return settleTopLevelTransactionGas(gasLimit, legacyGasLeft, snapshot.evmGasRefund,
            calldataFloorPerToken, snapshot.calldata);
    }
    return rawGasUsed;
}
```

- [ ] **Step 3: Replace adapter settlement (peak branch only; keep base-gas branches verbatim)**

CRITICAL: the existing adapter has two `TX_BASE_GAS + result.gasUsed` base-gas branches (the
`snap.gasLimit == 0 && SUCCESS` case and the non-EIP-7623 `SUCCESS` case). `finalizeEthTxGasUsed`
short-circuits to `rawGasUsed` (no `+21000`) whenever `snapshot.gasLimit <= 0`, so replacing the
WHOLE block with the helper silently drops the base-gas uplift and regresses GST/EEST. Only the
inner `snap.gasLimit > 0` peak branch may be routed through the helper.

In `bcos-evm/specs-tests/src/ExecuteViaEthAdapter.cpp`, change only the `snap.gasLimit > 0` branch,
leaving both `TX_BASE_GAS + result.gasUsed` branches intact:

```cpp
    int64_t finalGasUsed = result.gasUsed;
    if (m_profile.revision.eip7623)
    {
        auto const& snap = output.executionContext.gasSettlementSnapshot;
        if (snap.gasLimit > 0)
        {
            finalGasUsed = gas::finalizeEthTxGasUsed(gasBefore, output.evmcResult.gas_left,
                result.gasUsed, true, m_profile.revision.eip7623,
                output.topLevelIncludedTxVmError, snap,
                m_profile.revision.calldata_floor_per_token);
        }
        else if (result.status == EVMC_SUCCESS)
        {
            finalGasUsed = gas::TX_BASE_GAS + result.gasUsed;
        }
    }
    else if (result.status == EVMC_SUCCESS)
    {
        finalGasUsed = gas::TX_BASE_GAS + result.gasUsed;
    }
    result.gasUsed = finalGasUsed;
```

The helper exists only to keep the peak-settlement formula identical between adapter and TE; it must
NOT become the adapter's sole settlement entry point, because the GST-only base-gas convention lives
in the adapter.

- [ ] **Step 4: Replace TE settlement**

NOTE on the actual gap: the current TE `settleGasUsedFromEvmResult` already routes `isWeb3 && eip7623 && gasLimit > 0` through `settleTopLevelTransactionGas` (the peak model), which is exactly what included-vmerr needs — and `settleIncludedTopLevelTransactionGas` is presently a verbatim forwarder to `settleTopLevelTransactionGas`. So for **web3** included-vmerr txs the gas is already correct; consuming `m_topLevelIncludedTxVmError` only changes behavior for the **non-web3 + eip7623 + gasLimit > 0** included-vmerr case (peak model instead of `gasLimit - gas_left` raw). Before claiming this closes an ADR-015 gap, confirm such txs actually reach this TE path; if they cannot, this step is a safe refactor (no behavior change) and should be described as such. Do not assert a TE gasUsed delta in tests unless a non-web3 included-vmerr fixture exists.

In `transaction-executor/bcos-transaction-executor/EthTransactionExecutorImpl.h`, replace `settleGasUsedFromEvmResult()` body with:

```cpp
        void settleGasUsedFromEvmResult()
        {
            auto& evmcResult = *m_data->m_evmcResult;
            auto const& snapshot = m_data->m_executionContext.gasSettlementSnapshot;
            auto const isWeb3 =
                m_data->m_transaction.get().type() == protocol::TransactionType::Web3Transaction;
            auto const eip7623 = m_data->m_executionContext.revisionConfig.eip7623;
            auto const rawGasUsed = m_data->m_gasLimit - evmcResult.gas_left;

            m_data->m_gasUsed = gas::finalizeEthTxGasUsed(m_data->m_gasLimit,
                evmcResult.gas_left, rawGasUsed, isWeb3, eip7623,
                m_data->m_topLevelIncludedTxVmError, snapshot,
                m_data->m_executionContext.revisionConfig.calldata_floor_per_token);
        }
```

- [ ] **Step 5: Add TE-facing helper test**

Append to `transaction-executor/tests/EthTxGasSettlementTest.cpp`:

```cpp
BOOST_AUTO_TEST_CASE(FinalizeEthTxGasUsed_included_vmerr_matches_geth_peak)
{
    TxGasSettlementSnapshot snapshot;
    snapshot.gasLimit = 10'000'000;
    snapshot.calldata = calcEip7623Components(ref(bytes{}));
    snapshot.evmGasRefund = 0;

    BOOST_CHECK_EQUAL(finalizeEthTxGasUsed(10'000'000, 12'500, 9'987'500, true, true, true,
                          snapshot, 10),
        9'987'500);
}
```

- [ ] **Step 6: Run tests**

Run:

```bash
rtk cmake --build build --target EthIncludedTxVmerrTest
rtk ctest --test-dir build -R EthIncludedTxVmerr --output-on-failure
rtk cmake --build build --target test_BCOS-transaction-executor
rtk ctest --test-dir build -R EthTxGasSettlement --output-on-failure
```

Expected: helper tests pass; if target names differ, use `rtk ctest --test-dir build -N | rtk grep EthTxGasSettlement` to find the exact test name.

- [ ] **Step 7: Commit**

```bash
rtk git add bcos-evm/eth/eip/EthTxGasSettlement.h bcos-evm/specs-tests/src/ExecuteViaEthAdapter.cpp transaction-executor/bcos-transaction-executor/EthTransactionExecutorImpl.h bcos-evm/test/eth/EthIncludedTxVmerrTest.cpp transaction-executor/tests/EthTxGasSettlementTest.cpp
rtk git commit -m "$(cat <<'EOF'
fix(eth): share top-level gas settlement

EOF
)"
```

---

### Task 4: CREATE Error Parity Characterization

**Files:**
- Create: `bcos-evm/test/eth/CreateErrorParityTest.cpp`
- Modify: `bcos-evm/test/CMakeLists.txt`
- Modify after tests fail: `bcos-evm/eth/state/CreateExecution.h`
- Modify after tests fail: `bcos-evm/eth/executeMessage.cpp`
- Modify after tests fail: `bcos-evm/eth/state/EthHost.cpp`

**Interfaces:**
- Consumes: `executeMessage(...)`, `EthHost::call(...)`, `state::applyCreateCodeDepositGas(...)`.
- Produces: tests that lock geth-compatible behavior for CREATE collision, max initcode, code-store OOG, invalid code prefix, max runtime code size, and nonce/depth characterization.

- [ ] **Step 1: Create characterization test skeleton**

Create `bcos-evm/test/eth/CreateErrorParityTest.cpp`:

```cpp
#define BOOST_TEST_MODULE CreateErrorParityTest

#include "bcos-evm/eth/executeMessage.h"
#include "bcos-evm/eth/state/EthHost.hpp"
#include "state/InMemoryStateView.h"
#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

// bcos::bytes / bcos::fromHex come from bcos-utilities; verify the exact header during
// implementation (DataConvertUtility provides fromHex in this tree) and adjust if the symbol
// resolves elsewhere.

namespace bcos::evm::test
{
namespace
{
evmc_address addressFromLastByte(uint8_t value)
{
    evmc_address address{};
    address.bytes[19] = value;
    return address;
}

state::BlockHashes emptyBlockHashes()
{
    return [](int64_t) { return evmc_bytes32{}; };
}

ExecuteMessageOutput runCreate(state::State& state, bcos::bytes const& initcode, int64_t gas)
{
    static evmc::VM vm{evmc_create_evmone()};
    evmc_message message{};
    message.kind = EVMC_CREATE;
    message.gas = gas;
    message.sender = addressFromLastByte(0x01);
    message.input_data = initcode.data();
    message.input_size = initcode.size();

    ExecuteMessageInput input;
    input.stateView = &state;
    input.vm = &vm;
    input.message = message;
    input.blockInfo.number = 1;
    input.blockInfo.gasLimit = 30'000'000;
    input.revisionConfig.revision = EVMC_PRAGUE;
    input.revisionConfig.warm_access = true;
    return executeMessage(input);
}
}  // namespace

BOOST_AUTO_TEST_CASE(create_runtime_code_starting_with_ef_fails_and_reverts)
{
    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(addressFromLastByte(0x01), 1'000'000'000);
    bcos::bytes initcode = bcos::fromHex("60ef6000526001601ff3");

    auto output = runCreate(state, initcode, 500'000);

    BOOST_CHECK_EQUAL(output.result.status_code, EVMC_CONTRACT_VALIDATION_FAILURE);
    BOOST_CHECK_EQUAL(output.result.gas_left, 0);
    // NOTE: `accounts.empty()` is a fragile expectation — top-level CREATE runs warmTransactionEntry,
    // initializeCreateTargetAccount, and sender-nonce handling before revert, so build_diff() after
    // revert may still surface warmed/nonce-touched accounts. Run first; if the diff is non-empty,
    // relax to assert the created contract account is absent (no deployed code) rather than a fully
    // empty diff, and record the observed diff shape in a comment.
    BOOST_CHECK(output.stateDiff.accounts.empty());
}

// initcode OOG during execution (gas exhausted before RETURN). This is plain execution OOG, NOT
// code-deposit (code-store) OOG — exercising the deposit path needs enough gas to finish initcode
// but not enough for the 200/byte deposit charge. Kept as a baseline OOG characterization; add a
// separate, higher-gas case if/when code-store-OOG parity needs locking.
BOOST_AUTO_TEST_CASE(create_initcode_oog_consumes_remaining_gas)
{
    state::test::InMemoryStateView view;
    state::State state(view);
    state.set_balance(addressFromLastByte(0x01), 1'000'000'000);
    bcos::bytes initcode = bcos::fromHex("600160005260206000f3");

    auto output = runCreate(state, initcode, 10);

    BOOST_CHECK_NE(output.result.status_code, EVMC_SUCCESS);
    BOOST_CHECK_EQUAL(output.result.gas_left, 0);
}

}  // namespace bcos::evm::test
```

- [ ] **Step 2: Register test target**

Add to `bcos-evm/test/CMakeLists.txt`:

```cmake
add_executable(CreateErrorParityTest eth/CreateErrorParityTest.cpp)
target_include_directories(CreateErrorParityTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)
target_link_libraries(CreateErrorParityTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME CreateErrorParity COMMAND CreateErrorParityTest)
```

- [ ] **Step 3: Run characterization tests**

Run:

```bash
rtk cmake --build build --target CreateErrorParityTest
rtk ctest --test-dir build -R CreateErrorParity --output-on-failure
```

Expected: `create_runtime_code_starting_with_ef_fails_and_reverts` should pass if current `CreateExecution.h` behavior is correct; `create_code_store_oog_consumes_remaining_gas` may fail depending on current pre-debit behavior.

- [ ] **Step 4: Add missing max initcode check if characterization shows it is absent**

If a test with >49152-byte initcode succeeds or enters evmone, add this helper in `bcos-evm/eth/state/CreateExecution.h`:

```cpp
constexpr size_t MAX_INITCODE_SIZE = 0xc000;

inline bool exceedsMaxInitCodeSize(evmc_revision revision, size_t initCodeSize) noexcept
{
    return revision >= EVMC_SHANGHAI && initCodeSize > MAX_INITCODE_SIZE;
}
```

Then in `executeMessage.cpp`, before `state.checkpoint();`, add:

```cpp
    if (isCreateKind(input.message.kind) &&
        state::exceedsMaxInitCodeSize(input.revisionConfig.revision, input.message.input_size))
    {
        evmc_result result{};
        result.status_code = EVMC_FAILURE;
        result.gas_left = 0;
        output.result = evmc::Result(result);
        output.logs = host.take_logs();
        return output;
    }
```

- [ ] **Step 5: Add collision check if characterization shows it is absent**

If CREATE can overwrite an existing account with nonce/code, add a helper to `CreateExecution.h`:

```cpp
inline bool hasCreateCollision(State& state, evmc_address const& createAddr) noexcept
{
    if (isZeroAddress(createAddr))
    {
        return false;
    }
    auto account = state.find(createAddr);
    if (!account.has_value())
    {
        return false;
    }
    return account->nonce != 0 || !account->code.empty() || !account->storage.empty();
}
```

Use it immediately after `bindCreateMessageForInit(...)` and before `initializeCreateTargetAccount(...)` in both `executeMessage.cpp` and `EthHost.cpp`:

```cpp
        if (state::hasCreateCollision(state, input.message.recipient))
        {
            evmc_result result{};
            result.status_code = EVMC_FAILURE;
            result.gas_left = 0;
            output.result = evmc::Result(result);
            output.logs = host.take_logs();
            return output;
        }
```

- [ ] **Step 6: Re-run tests**

Run:

```bash
rtk cmake --build build --target CreateErrorParityTest
rtk ctest --test-dir build -R CreateErrorParity --output-on-failure
```

Expected: PASS. If a characterization test reveals a different evmone status but state/gas semantics match geth, update the assertion to lock the observed status and add a comment naming the geth error it corresponds to.

- [ ] **Step 7: Commit**

```bash
rtk git add bcos-evm/test/eth/CreateErrorParityTest.cpp bcos-evm/test/CMakeLists.txt bcos-evm/eth/state/CreateExecution.h bcos-evm/eth/executeMessage.cpp bcos-evm/eth/state/EthHost.cpp
rtk git commit -m "$(cat <<'EOF'
test(eth): characterize create error parity

EOF
)"
```

---

### Task 5: Taxonomy, Status Mapping, and Exception Boundaries

**Files:**
- Create: `bcos-evm/eth/EthTxOutcome.h`
- Modify: `bcos-evm/eth/EVMCResult.cpp`
- Modify: `bcos-evm/eth/ExecuteViaEth.cpp`
- Create: `bcos-evm/test/eth/EthTxOutcomeClassificationTest.cpp`
- Create: `bcos-evm/docs/adr/017-eth-evm-error-taxonomy.md`
- Modify: `bcos-evm/test/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `enum class EthTxOutcome`
  - `bool isIncludedTopLevelVmError(evmc_status_code status, int32_t depth) noexcept`
  - no known EVMC status throws `UnknownEVMCStatus`.

- [ ] **Step 1: Add taxonomy header**

Create `bcos-evm/eth/EthTxOutcome.h`:

```cpp
#pragma once

#include <evmc/evmc.h>
#include <cstdint>

namespace bcos::evm
{
enum class EthTxOutcome
{
    ConsensusRejected,
    IncludedVmError,
    NestedCallError,
    InfrastructureFault,
};

inline bool isIncludedTopLevelVmError(evmc_status_code status, int32_t depth) noexcept
{
    if (depth != 0)
    {
        return false;
    }
    switch (status)
    {
    case EVMC_SUCCESS:
    case EVMC_INSUFFICIENT_BALANCE:
    case EVMC_INTERNAL_ERROR:
        return false;
    default:
        return true;
    }
}
}  // namespace bcos::evm
```

- [ ] **Step 2: Add classification tests**

Create `bcos-evm/test/eth/EthTxOutcomeClassificationTest.cpp`:

```cpp
#define BOOST_TEST_MODULE EthTxOutcomeClassificationTest

#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/eth/EVMCResult.h"
#include "bcos-evm/eth/EthTxOutcome.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{

BOOST_AUTO_TEST_CASE(included_top_level_vmerr_excludes_success_balance_and_internal)
{
    BOOST_CHECK(!isIncludedTopLevelVmError(EVMC_SUCCESS, 0));
    BOOST_CHECK(!isIncludedTopLevelVmError(EVMC_INSUFFICIENT_BALANCE, 0));
    BOOST_CHECK(!isIncludedTopLevelVmError(EVMC_INTERNAL_ERROR, 0));
    BOOST_CHECK(isIncludedTopLevelVmError(EVMC_REVERT, 0));
    BOOST_CHECK(isIncludedTopLevelVmError(EVMC_OUT_OF_GAS, 0));
    BOOST_CHECK(!isIncludedTopLevelVmError(EVMC_REVERT, 1));
}

BOOST_AUTO_TEST_CASE(known_evmc_statuses_do_not_throw)
{
    crypto::Keccak256 hashImpl;
    std::array<evmc_status_code, 13> statuses{EVMC_SUCCESS, EVMC_FAILURE, EVMC_REVERT,
        EVMC_OUT_OF_GAS, EVMC_INVALID_INSTRUCTION, EVMC_UNDEFINED_INSTRUCTION,
        EVMC_STACK_OVERFLOW, EVMC_STACK_UNDERFLOW, EVMC_BAD_JUMP_DESTINATION,
        EVMC_INVALID_MEMORY_ACCESS, EVMC_STATIC_MODE_VIOLATION,
        EVMC_INSUFFICIENT_BALANCE, EVMC_CONTRACT_VALIDATION_FAILURE};

    for (auto status : statuses)
    {
        BOOST_CHECK_NO_THROW((void)evmcStatusToErrorMessage(hashImpl, status));
        BOOST_CHECK_NO_THROW((void)evmcStatusToTransactionStatus(status));
    }
}

}  // namespace bcos::evm::test
```

- [ ] **Step 3: Register test target**

Add to `bcos-evm/test/CMakeLists.txt`:

```cmake
add_executable(EthTxOutcomeClassificationTest eth/EthTxOutcomeClassificationTest.cpp)
target_include_directories(EthTxOutcomeClassificationTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)
target_link_libraries(EthTxOutcomeClassificationTest PRIVATE bcos-evm-eth bcos-crypto)
add_test(NAME EthTxOutcomeClassification COMMAND EthTxOutcomeClassificationTest)
```

- [ ] **Step 4: Use taxonomy in ExecuteViaEth**

In `bcos-evm/eth/ExecuteViaEth.cpp`, include:

```cpp
#include "bcos-evm/eth/EthTxOutcome.h"
```

Delete the local `isTopLevelIncludedTxVmError(...)` function and replace its call sites:

```cpp
    if (!isIncludedTopLevelVmError(result.status_code, depth))
```

and:

```cpp
        output.topLevelIncludedTxVmError =
            isIncludedTopLevelVmError(output.evmcResult.status_code, input.message.depth);
```

- [ ] **Step 5: Complete status mapping**

In `bcos-evm/eth/EVMCResult.cpp`, replace `evmcStatusToTransactionStatus(...)` with:

```cpp
bcos::protocol::TransactionStatus bcos::evm::evmcStatusToTransactionStatus(evmc_status_code status)
{
    switch (status)
    {
    case EVMC_SUCCESS:
        return protocol::TransactionStatus::None;
    case EVMC_REVERT:
        return protocol::TransactionStatus::RevertInstruction;
    case EVMC_OUT_OF_GAS:
        return protocol::TransactionStatus::OutOfGas;
    case EVMC_INSUFFICIENT_BALANCE:
        return protocol::TransactionStatus::NotEnoughCash;
    case EVMC_STACK_OVERFLOW:
        return protocol::TransactionStatus::OutOfStack;
    case EVMC_STACK_UNDERFLOW:
        return protocol::TransactionStatus::StackUnderflow;
    case EVMC_INVALID_INSTRUCTION:
    case EVMC_UNDEFINED_INSTRUCTION:
        return protocol::TransactionStatus::BadInstruction;
    case EVMC_BAD_JUMP_DESTINATION:
        return protocol::TransactionStatus::BadJumpDestination;
    case EVMC_INVALID_MEMORY_ACCESS:
    case EVMC_STATIC_MODE_VIOLATION:
    case EVMC_CONTRACT_VALIDATION_FAILURE:
    case EVMC_FAILURE:
    case EVMC_INTERNAL_ERROR:
    case EVMC_REJECTED:
    case EVMC_OUT_OF_MEMORY:
    case EVMC_PRECOMPILE_FAILURE:
        return protocol::TransactionStatus::Unknown;
    default:
        return protocol::TransactionStatus::Unknown;
    }
}
```

- [ ] **Step 6: Add ADR-017**

Create `bcos-evm/docs/adr/017-eth-evm-error-taxonomy.md`:

```markdown
# ADR-017: ETH EVM Error Taxonomy

**Status:** Accepted
**Date:** 2026-06-23
**Related:** ADR-005, ADR-015, ADR-016, `docs/superpowers/specs/2026-06-23-eth-evm-error-handling-parity-design.md`

## Context

geth separates consensus rejection, included top-level VM errors, and nested frame errors. `bcos-evm/eth` must preserve that separation so state roots, gas used, and receipts match Ethereum reference behavior.

## Decision

Use four outcome categories:

- `ConsensusRejected`: geth `ApplyMessage` returns `error`; transaction is not included.
- `IncludedVmError`: geth `ExecutionResult.Err != nil` and `ApplyMessage` error is nil; transaction is included and receipt is failed.
- `NestedCallError`: child frame failure; child state reverts and status/gas propagate to the parent frame.
- `InfrastructureFault`: non-EVM infrastructure failure; never used to silently reclassify a valid EVM vmerr.

Nested `ErrInsufficientBalance` and `ErrDepth` preserve frame gas. Nested non-REVERT fatal VM errors consume frame gas. Top-level vmerr normalization remains in orchestration, not in the shared kernel.

## Consequences

`ExecuteViaEth` uses `isIncludedTopLevelVmError(...)` for ADR-015 normalization. `EVMCResult` maps all known EVMC statuses to existing `TransactionStatus` values without throwing.
```

- [ ] **Step 7: Run tests**

Run:

```bash
rtk cmake --build build --target EthTxOutcomeClassificationTest EthIncludedTxVmerrTest
rtk ctest --test-dir build -R "EthTxOutcomeClassification|EthIncludedTxVmerr" --output-on-failure
```

Expected: PASS.

- [ ] **Step 8: Commit**

```bash
rtk git add bcos-evm/eth/EthTxOutcome.h bcos-evm/eth/EVMCResult.cpp bcos-evm/eth/ExecuteViaEth.cpp bcos-evm/test/eth/EthTxOutcomeClassificationTest.cpp bcos-evm/docs/adr/017-eth-evm-error-taxonomy.md bcos-evm/test/CMakeLists.txt
rtk git commit -m "$(cat <<'EOF'
docs(eth): define evm error taxonomy

EOF
)"
```

---

### Task 6: Capability Matrix, Probe Manifests, and Final Validation

**Files:**
- Modify: `bcos-evm/capability-matrix.md`
- Create: `bcos-evm/specs-tests/manifests/eth-eest-probe-insufficient-balance.json`
- Create: `bcos-evm/specs-tests/manifests/eth-eest-probe-depth.json`
- Create: `bcos-evm/specs-tests/manifests/eth-eest-probe-create-errors.json`
- Create: `bcos-evm/specs-tests/assets/geth-diff-allowlist.json`

**Interfaces:**
- Consumes: Tasks 1-5 behavior and test names.
- Produces: governance and reference-test tracking for error-handling parity.

- [ ] **Step 1: Update capability matrix rows**

Add these rows under the matrix near other orchestration/kernel rows in `bcos-evm/capability-matrix.md`:

```markdown
| nested INSUFFICIENT_BALANCE gas_left | kernel | inherited | inherited | inherited | `InsufficientBalanceGasLeftTest` |
| PrecompileRouter value-transfer envelope | kernel | inherited | inherited | inherited | `PrecompileRouterEnvelopeTest` |
| CREATE error parity | kernel + orchestration | explicit/inherited (by error layer) | inherited (kernel subset) | inherited (kernel subset) | `CreateErrorParityTest` |
| included-tx vmerr TE settlement | orchestration | explicit (`finalizeEthTxGasUsed` consumes `topLevelIncludedTxVmError`) | unsupported | unsupported | `EthIncludedTxVmerrTest`, `EthTxGasSettlementTest` |
| EthTxOutcome taxonomy | orchestration | explicit (`EthTxOutcome.h`, ADR-017) | inherited (kernel taxonomy) | inherited (kernel taxonomy) | `EthTxOutcomeClassificationTest` |
```

- [ ] **Step 2: Add allowlist JSON**

Create `bcos-evm/specs-tests/assets/geth-diff-allowlist.json`:

```json
{
  "version": 1,
  "entries": []
}
```

- [ ] **Step 3: Add probe manifests using the real manifest schema**

CRITICAL: existing probe manifests in `bcos-evm/specs-tests/manifests/` use
`{"manifestVersion": 1, "entries": [ { "evidenceId", "sourceSuite", "casePath", "variantKey",
"forkProfileId", "path", "evidenceKind", "capabilityRowIds", "assertLevels" } ]}` — NOT
`{"name","description","tests":[]}`. Match that schema or the manifest loader will reject them.
Inspect `eth-eest-probe-invalid.json` and the loader before creating these. Empty `entries` is
acceptable as a tracked placeholder only if the loader tolerates it; otherwise populate with at
least one real pinned EEST case id from the already-fetched assets.

Create `bcos-evm/specs-tests/manifests/eth-eest-probe-insufficient-balance.json`:

```json
{
  "manifestVersion": 1,
  "entries": []
}
```

Create `bcos-evm/specs-tests/manifests/eth-eest-probe-depth.json`:

```json
{
  "manifestVersion": 1,
  "entries": []
}
```

Create `bcos-evm/specs-tests/manifests/eth-eest-probe-create-errors.json`:

```json
{
  "manifestVersion": 1,
  "entries": []
}
```

- [ ] **Step 4: Run focused validation**

Run:

```bash
rtk cmake --build build --target PrecompileRouterEnvelopeTest InsufficientBalanceGasLeftTest EthIncludedTxVmerrTest EthTxOutcomeClassificationTest CreateErrorParityTest
rtk ctest --test-dir build -R "PrecompileRouterEnvelope|InsufficientBalanceGasLeft|EthIncludedTxVmerr|EthTxOutcomeClassification|CreateErrorParity" --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Run broader reference validation**

Run:

There is no single `bcos-specs-tests` target. The reference suite is a set of executables/tests
(`EthGSTSmoke`, `EthGSTFull`, `EthExecutionSpecStateTests`, `EthExecutionSpecTransactionTests`,
`ExecuteViaEthAdapterTest`, ...) backed by libs `bcos-evm-specs-tests-core` / `bcos-evm-specs-tests-eth`.
Discover the exact names first, then build/run them:

```bash
rtk ctest --test-dir build -N | rtk grep -E "EthGST|EthExecutionSpec|ExecuteViaEthAdapter"
rtk cmake --build build --target EthGSTSmoke EthExecutionSpecStateTests ExecuteViaEthAdapterTest
rtk ctest --test-dir build -R "EthGST|EthExecutionSpec|ExecuteViaEthAdapter" --output-on-failure
```

Expected: PASS or only known unrelated failures already present before this branch. Record any unrelated failures in the final PR notes; do not add them to the new allowlist unless they are geth-diff cases from this error-handling scope.

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/capability-matrix.md bcos-evm/specs-tests/assets/geth-diff-allowlist.json bcos-evm/specs-tests/manifests/eth-eest-probe-insufficient-balance.json bcos-evm/specs-tests/manifests/eth-eest-probe-depth.json bcos-evm/specs-tests/manifests/eth-eest-probe-create-errors.json
rtk git commit -m "$(cat <<'EOF'
docs(eth): track error-handling parity coverage

EOF
)"
```

---

## Self-Review

**Spec coverage:**  
Task 1 covers §4.1 PrecompileRouter envelope. Task 2 covers §4.2 nested CALL/CREATE insufficient-balance and depth characterization. Task 3 covers §4.3 shared gas settlement and ADR-015 TE closure. Task 4 covers §4.4 CREATE error parity. Task 5 covers §4.5/§4.6 taxonomy, status mapping, and exception boundaries. Task 6 covers §6 capability matrix, probe manifests, allowlist, and final validation.

**Placeholder scan:**  
No plan steps contain missing implementation instructions. The probe manifests use the real
`{"manifestVersion":1,"entries":[]}` schema (matching existing `eth-eest-probe-*.json`); empty
`entries` is a tracked placeholder pending fixture population from the already-pinned upstream
assets, and must be validated against the manifest loader (Task 6 Step 3) before commit.

**Type consistency:**  
`finalizeEthTxGasUsed(...)` is defined once in Task 3 and reused by both adapter and TE in the same task. `isIncludedTopLevelVmError(...)` is defined in Task 5 and used in `ExecuteViaEth.cpp`. Test target names match the file names used in CMake blocks.

**Execution order:**  
Task 1 must precede Task 2 because router envelope changes affect gas-left and rollback assertions. Task 3 can follow Task 1-2 and is independent of CREATE parity. Task 5 can run after Task 3, but ADR/capability docs in Task 6 should be last so they reflect final test names and behavior.
