# VM Domain EVMC-Only Semantics Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make ETH/OpStack reference paths speak EVMC inside the VM execution domain, project `TransactionStatus` once at `adoptEvmcResult`, unify the dual EVMC mapping tables, and (P3+) gate block inclusion on `TxConsensusOutcome` per ADR-028.

**Architecture:** Introduce `eth/EvmcStatusMap.*` as the single authoritative EVMC → `TransactionStatus` table (never throws). Refactor `EVMCResult.cpp` to delegate all status projection to that table. Add CI guardrails so `eth/execution/`, `EthHost.cpp`, and `VMInstance.cpp` never include `TransactionStatus.h`. P3 adds `TxConsensusOutcome` at bridge/lifecycle boundaries so TE skips receipts on geth-equivalent preCheck rejects without changing VM frame semantics.

**Tech Stack:** C++17+, CMake 3.28, Boost.Test, evmone, FISCO-BCOS `bcos-evm` (eth/bcos/opstack static libs), `transaction-executor` adapters.

**Spec / ADR:** `docs/superpowers/specs/2026-06-26-vm-domain-evmc-only-design.md` · ADR-015 · ADR-028 (draft) · GAP-005/009/010/011

## Global Constraints

- **Scope:** ETH reference + OpStack reference orchestration only; **FISCO `bcos/` production baseline untouched**.
- **Out of scope:** `bcos-protocol/TransactionStatus` enum changes; receipt wire format changes; exposing raw EVMC on receipt wire.
- **VM domain modules:** `eth/execution/*`, `eth/state/EthHost.cpp`, `eth/vm/VMInstance.cpp`, `eth/precompiled/PrecompileRouter.cpp` — **EVMC only**; no `#include "bcos-protocol/TransactionStatus.h"`.
- **First TS projection:** `adoptEvmcResult` in `eth/pipeline/adoptEvmcResult.h`; bridge/lifecycle may override (ADR-015 normalize, ADR-028 reject).
- **Mapping contract:** `mapEvmcToTransactionStatus` **never throws**; unlisted codes → `TransactionStatus::Unknown`; remove `UnknownEVMCStatus` from hot path (GAP-005/011).
- **ADR-015:** `normalizeIncludedTxVmerr` stays in orchestration (`normalizeIncludedTxVmerr.h`), never inside `ExecutionFrame`.
- **YAGNI:** No new `VmFrameResult` type in P0; no FISCO path migration.
- New `eth/*.cpp` picked up automatically by `file(GLOB_RECURSE eth/*.cpp)` in `bcos-evm/CMakeLists.txt`.
- **Build dir (local):** `build-bcos-evm-check` from repo root with `-DBUILD_TESTS=ON`.

## File Structure Map

| File | Responsibility |
| --- | --- |
| `bcos-evm/eth/EvmcStatusMap.h` | Declares `mapEvmcToTransactionStatus` |
| `bcos-evm/eth/EvmcStatusMap.cpp` | Single authoritative EVMC → TS switch (spec §4.1 table) |
| `bcos-evm/eth/EVMCResult.cpp` | Delegates to map; `evmcStatusToErrorMessage` uses map for status half |
| `bcos-evm/eth/pipeline/adoptEvmcResult.h` | Unchanged signature; benefits from unified map via `evmcStatusToErrorMessage` |
| `bcos-evm/eth/vm/VMInstance.cpp` | Single-arg `EVMCResult` ctor — stops throwing after P0 |
| `bcos-evm/test/eth/EvmcStatusMappingTest.cpp` | Oracle for GAP-005/010/011; flip throw expectations in P0 |
| `bcos-evm/tools/ci/check-vm-domain-transaction-status.sh` | P1: forbid `TransactionStatus.h` in VM domain paths |
| `.github/workflows/capability-gate.yml` | Wire P1 script into `matrix-lint` job |
| `bcos-evm/eth/TxConsensusOutcome.h` | P3: `TxConsensusOutcome` + `isConsensusRejected` |
| `bcos-evm/eth/reference/EthReferenceBridge.h/.cpp` | P3: propagate `consensusOutcome` |
| `bcos-evm/opstack/OpStackExecutionBridge.h` / `OpStackTxLifecycle.cpp` | P3: propagate `consensusOutcome` |
| `transaction-executor/.../EthTransactionExecutorImpl.h` | P3: skip receipt when `Rejected` |
| `transaction-executor/.../OpStackTransactionExecutorImpl.h` | P3: skip receipt when `Rejected` (normal L2 only) |
| `bcos-evm/docs/adr/029-vm-domain-evmc-only.md` | P4: normative ADR |
| `bcos-evm/capability-matrix.md` | P4: row for EVMC-only VM domain + unified mapping |

---

## P0 — Unified Mapping Table (GAP-005/010/011)

### Task 1: `EvmcStatusMap` module + failing test flip

**Files:**
- Create: `bcos-evm/eth/EvmcStatusMap.h`
- Create: `bcos-evm/eth/EvmcStatusMap.cpp`
- Modify: `bcos-evm/test/eth/EvmcStatusMappingTest.cpp`

**Interfaces:**
- Produces: `bcos::evm::mapEvmcToTransactionStatus(evmc_status_code) noexcept → protocol::TransactionStatus`

- [ ] **Step 1: Add include and update mapping table expectations (TDD — test first)**

At top of `EvmcStatusMappingTest.cpp`, add:

```cpp
#include "bcos-evm/eth/EvmcStatusMap.h"
```

In `mappingTable()`, change throwing rows to mapped expectations:

```cpp
        {EVMC_BAD_JUMP_DESTINATION, "BAD_JUMP_DESTINATION", TransactionStatus::BadJumpDestination,
            false, TransactionStatus::BadJumpDestination, true, "errors.go:35 ErrInvalidJump"},
        {EVMC_INVALID_MEMORY_ACCESS, "INVALID_MEMORY_ACCESS", TransactionStatus::StackUnderflow,
            false, TransactionStatus::StackUnderflow, true,
            "errors.go:47+ ErrStackUnderflow (no memory OOB vm.Error)"},
        {EVMC_STATIC_MODE_VIOLATION, "STATIC_MODE_VIOLATION", TransactionStatus::Unknown, false,
            TransactionStatus::Unknown, true, "errors.go:36 ErrWriteProtection"},
        {EVMC_INTERNAL_ERROR, "INTERNAL_ERROR", TransactionStatus::Unknown, false,
            TransactionStatus::Unknown, false, "errors.go:205 VMErrorCodeUnknown fallback"},
        {static_cast<evmc_status_code>(999), "UNKNOWN_999", TransactionStatus::Unknown, false,
            TransactionStatus::Unknown, false, "errors.go:205 VMErrorCodeUnknown"},
```

Replace the two focused throw tests at bottom with non-throw assertions:

```cpp
BOOST_AUTO_TEST_CASE(invalid_memory_access_maps_to_stack_underflow_unified)
{
    bcos::crypto::Keccak256 hashImpl;
    BOOST_CHECK_EQUAL(mapEvmcToTransactionStatus(EVMC_INVALID_MEMORY_ACCESS),
        TransactionStatus::StackUnderflow);
    BOOST_CHECK_EQUAL(evmcStatusToTransactionStatus(EVMC_INVALID_MEMORY_ACCESS),
        TransactionStatus::StackUnderflow);
    auto const [status, output] = evmcStatusToErrorMessage(hashImpl, EVMC_INVALID_MEMORY_ACCESS);
    BOOST_CHECK_EQUAL(status, TransactionStatus::StackUnderflow);
    BOOST_CHECK(!output.empty());
}

BOOST_AUTO_TEST_CASE(static_mode_violation_maps_to_unknown_unified)
{
    bcos::crypto::Keccak256 hashImpl;
    BOOST_CHECK_EQUAL(mapEvmcToTransactionStatus(EVMC_STATIC_MODE_VIOLATION),
        TransactionStatus::Unknown);
    BOOST_CHECK_EQUAL(evmcStatusToTransactionStatus(EVMC_STATIC_MODE_VIOLATION),
        TransactionStatus::Unknown);
    auto const [status, output] = evmcStatusToErrorMessage(hashImpl, EVMC_STATIC_MODE_VIOLATION);
    BOOST_CHECK_EQUAL(status, TransactionStatus::Unknown);
    BOOST_CHECK(!output.empty());
}
```

Replace `vm_instance_single_arg_constructor_throws_while_adopt_does_not` with:

```cpp
BOOST_AUTO_TEST_CASE(vm_instance_and_adopt_use_same_mapping)
{
    bcos::crypto::Keccak256 hashImpl;
    evmc_result raw{};
    raw.status_code = EVMC_STATIC_MODE_VIOLATION;
    raw.gas_left = 42'000;

    EVMCResult constructed{raw};
    BOOST_CHECK_EQUAL(constructed.status, TransactionStatus::Unknown);

    auto adopted = adoptEvmcResult(evmc::Result(raw), hashImpl);
    BOOST_CHECK_EQUAL(adopted.status_code, EVMC_STATIC_MODE_VIOLATION);
    BOOST_CHECK_EQUAL(adopted.status, constructed.status);
}
```

In `evmc_status_mapping_completeness_table`, remove the `transactionStatusThrows` branch entirely; always assert:

```cpp
            BOOST_REQUIRE(row.transactionStatus.has_value());
            BOOST_CHECK_EQUAL(mapEvmcToTransactionStatus(row.evmcStatus), *row.transactionStatus);
            BOOST_CHECK_EQUAL(evmcStatusToTransactionStatus(row.evmcStatus), *row.transactionStatus);
            // ... error message + adopt checks unchanged ...
            EVMCResult constructed{rawStatus(row.evmcStatus)};
            BOOST_CHECK_EQUAL(constructed.status, row.errorMessageStatus);
            BOOST_CHECK_EQUAL(constructed.status, adopted.status);
```

Keep `insufficient_balance_precheck_vs_execution_status_codes` unchanged (GAP-009 documents intentional two-arg override until P3).

- [ ] **Step 2: Run test to verify it fails**

Run:

```bash
cd build-bcos-evm-check
cmake --build . --target EvmcStatusMappingTest -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
rtk ctest -R '^EvmcStatusMapping$' -V
```

Expected: **FAIL** — linker error `mapEvmcToTransactionStatus` undefined, or compile error if only test changed.

- [ ] **Step 3: Create `EvmcStatusMap.h`**

```cpp
#pragma once

#include "bcos-protocol/TransactionStatus.h"
#include <evmc/evmc.h>

namespace bcos::evm
{

protocol::TransactionStatus mapEvmcToTransactionStatus(evmc_status_code status) noexcept;

}  // namespace bcos::evm
```

- [ ] **Step 4: Create `EvmcStatusMap.cpp`**

```cpp
#include "EvmcStatusMap.h"

bcos::protocol::TransactionStatus bcos::evm::mapEvmcToTransactionStatus(
    evmc_status_code status) noexcept
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
        // evmone compat; documented deviation — maps to StackUnderflow (spec §4.1)
        return protocol::TransactionStatus::StackUnderflow;
    case EVMC_STATIC_MODE_VIOLATION:
        return protocol::TransactionStatus::Unknown;
    case EVMC_INTERNAL_ERROR:
    case EVMC_FAILURE:
    case EVMC_REJECTED:
    case EVMC_OUT_OF_MEMORY:
    case EVMC_CONTRACT_VALIDATION_FAILURE:
    case EVMC_PRECOMPILE_FAILURE:
        return protocol::TransactionStatus::Unknown;
    default:
        return protocol::TransactionStatus::Unknown;
    }
}
```

- [ ] **Step 5: Run test — still fails until Task 2**

Expected: **FAIL** — `evmcStatusToTransactionStatus` still throws on `EVMC_BAD_JUMP_DESTINATION`.

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/eth/EvmcStatusMap.h bcos-evm/eth/EvmcStatusMap.cpp \
  bcos-evm/test/eth/EvmcStatusMappingTest.cpp
rtk git commit -m "$(cat <<'EOF'
feat(bcos-evm): add EvmcStatusMap single authoritative EVMC→TS table

EOF
)"
```

---

### Task 2: Unify `EVMCResult.cpp` — delegate + remove throw

**Files:**
- Modify: `bcos-evm/eth/EVMCResult.cpp`
- Modify: `bcos-evm/eth/EVMCResult.h` (optional `@deprecated` note on alias)

**Interfaces:**
- Consumes: `mapEvmcToTransactionStatus` from Task 1
- Produces: `evmcStatusToTransactionStatus` as thin alias; single-arg ctor never throws

- [ ] **Step 1: Refactor `EVMCResult.cpp`**

Add include:

```cpp
#include "EvmcStatusMap.h"
```

Remove:

```cpp
DERIVE_BCOS_EXCEPTION(UnknownEVMCStatus);
```

Replace `evmcStatusToTransactionStatus` body with:

```cpp
bcos::protocol::TransactionStatus bcos::evm::evmcStatusToTransactionStatus(
    evmc_status_code status)
{
    return mapEvmcToTransactionStatus(status);
}
```

At top of `evmcStatusToErrorMessage`, after `using namespace std::string_literals;`, add status from map for every case — refactor each `return {TransactionStatus::*, ...}` to use a shared prefix:

```cpp
    auto mapped = mapEvmcToTransactionStatus(status);
    switch (status)
    {
    case EVMC_SUCCESS:
        return {mapped, {}};
    case EVMC_REVERT:
        return {mapped, {}};
    case EVMC_OUT_OF_GAS:
        return {mapped,
            bcos::evm::writeErrInfoToOutput(hashImpl, "Execution out of gas."s)};
    // ... keep existing message strings; use `mapped` as first tuple element for all cases ...
    case EVMC_BAD_JUMP_DESTINATION:
        return {mapped,
            bcos::evm::writeErrInfoToOutput(
                hashImpl, "Execution has violated the jump destination restrictions."s)};
    case EVMC_INVALID_MEMORY_ACCESS:
        return {mapped,
            bcos::evm::writeErrInfoToOutput(
                hashImpl, "Execution tried to read outside memory bounds."s)};
    case EVMC_STATIC_MODE_VIOLATION:
        return {mapped,
            bcos::evm::writeErrInfoToOutput(hashImpl,
                "Execution tried to execute an operation which is restricted in static mode."s)};
    case EVMC_INTERNAL_ERROR:
    default:
        return {mapped, {}};
    }
```

Remove `#include "bcos-utilities/Exceptions.h"` and `#include <boost/throw_exception.hpp>` if no longer used.

- [ ] **Step 2: Run mapping test**

Run:

```bash
cd build-bcos-evm-check
cmake --build . --target EvmcStatusMappingTest -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
rtk ctest -R '^EvmcStatusMapping$' -V
```

Expected: **PASS**

- [ ] **Step 3: Run parity regression slice**

Run:

```bash
rtk ctest -R 'InsufficientBalanceGasLeft|PrecompileRouterEnvelope|ExecutionFrame$|EthIncludedTxVmerr'
```

Expected: **PASS** (behavior unchanged except no throw on unmapped EVMC)

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-evm/eth/EVMCResult.cpp bcos-evm/eth/EVMCResult.h
rtk git commit -m "$(cat <<'EOF'
refactor(bcos-evm): unify EVMC status projection via EvmcStatusMap

Removes UnknownEVMCStatus throw path; closes GAP-005/010/011 on reference paths.
EOF
)"
```

---

## P1 — CI Guard: VM Domain Must Not Include TransactionStatus

### Task 3: `check-vm-domain-transaction-status.sh` + workflow wire

**Files:**
- Create: `bcos-evm/tools/ci/check-vm-domain-transaction-status.sh`
- Modify: `.github/workflows/capability-gate.yml`

**Interfaces:**
- Produces: exit 0 when VM domain paths are clean; exit 1 with file list otherwise

- [ ] **Step 1: Create CI script**

```bash
#!/usr/bin/env bash
# Spec §3.1 / P1: VM execution domain must not include TransactionStatus.h
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"

status=0
paths=(
  "eth/execution"
  "eth/state/EthHost.cpp"
  "eth/vm/VMInstance.cpp"
  "eth/precompiled/PrecompileRouter.cpp"
)

for path in "${paths[@]}"; do
  if [[ ! -e "$path" ]]; then
    echo "WARN: path missing: $path" >&2
    continue
  fi
  if matches=$(grep -l 'bcos-protocol/TransactionStatus\.h' "$path" \
      --include='*.cpp' --include='*.h' --include='*.hpp' -r 2>/dev/null || true); then
    if [[ -n "$matches" ]]; then
      echo "ERROR: VM domain must not include TransactionStatus.h — $path:" >&2
      echo "$matches" >&2
      status=1
    fi
  fi
done

if [[ $status -eq 0 ]]; then
  echo "vm-domain-transaction-status gate: OK"
fi
exit $status
```

```bash
chmod +x bcos-evm/tools/ci/check-vm-domain-transaction-status.sh
```

- [ ] **Step 2: Add to `capability-gate.yml` after eth-layer-boundary step**

```yaml
      - name: VM domain TransactionStatus gate
        run: bash bcos-evm/tools/ci/check-vm-domain-transaction-status.sh
```

- [ ] **Step 3: Run locally**

Run:

```bash
bash bcos-evm/tools/ci/check-vm-domain-transaction-status.sh
```

Expected: `vm-domain-transaction-status gate: OK`

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-evm/tools/ci/check-vm-domain-transaction-status.sh \
  .github/workflows/capability-gate.yml
rtk git commit -m "$(cat <<'EOF'
ci(bcos-evm): gate VM domain against TransactionStatus includes

EOF
)"
```

---

## P2 — Execution Domain Type Purity (Documentation + Verification)

### Task 4: Document EVMC-only boundary on execution headers

**Files:**
- Modify: `bcos-evm/eth/execution/InnerExecute.h` (comment on `ExecuteMessageOutput`)
- Modify: `bcos-evm/eth/execution/EvmCallFrame.h` (module boundary comment)
- Test: existing suites (no new test file required — domain already clean)

**Interfaces:**
- Produces: documented contract that `ExecuteMessageOutput::result` is raw EVMC until pipeline adopt

- [ ] **Step 1: Add boundary comment to `ExecuteMessage.h`**

Above `ExecuteMessageOutput`:

```cpp
/// VM execution domain output — raw evmc::Result only.
/// TransactionStatus projection happens at pipeline adoptEvmcResult (spec §3.1).
struct ExecuteMessageOutput
```

- [ ] **Step 2: Add boundary comment to `ExecutionFrame.h`**

After `#pragma once` block / before namespace:

```cpp
// VM execution domain (spec §3.1): evmc_status_code + gas_left only.
// Do not include bcos-protocol/TransactionStatus.h in this directory.
```

- [ ] **Step 3: Verify grep clean**

Run:

```bash
rtk grep 'TransactionStatus' bcos-evm/eth/execution bcos-evm/eth/state/EthHost.cpp \
  bcos-evm/eth/vm/VMInstance.cpp bcos-evm/eth/precompiled/PrecompileRouter.cpp
```

Expected: **no matches**

- [ ] **Step 4: Run execution regression**

Run:

```bash
rtk ctest -R 'ExecutionFrame$|InsufficientBalanceGasLeft|ExecuteMessageSmoke'
```

Expected: **PASS**

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/eth/execution/InnerExecute.h bcos-evm/eth/execution/EvmCallFrame.h
rtk git commit -m "$(cat <<'EOF'
docs(bcos-evm): document EVMC-only VM execution domain boundary

EOF
)"
```

---

## P3 — ADR-028 Consensus Reject (GAP-009/001/002)

> **Note:** P3 is independently shippable after P0 but changes TE inclusion semantics. Run full characterization suite before merge.

### Task 5: `TxConsensusOutcome` taxonomy + unit test

**Files:**
- Create: `bcos-evm/eth/TxConsensusOutcome.h`
- Create: `bcos-evm/test/eth/TxConsensusOutcomeTest.cpp`
- Modify: `bcos-evm/test/cmake/EthTests.cmake`

**Interfaces:**
- Produces: `TxConsensusOutcome`, `isConsensusRejected(TxPipelineExitKind) noexcept → bool`

- [ ] **Step 1: Write failing test**

Create `bcos-evm/test/eth/TxConsensusOutcomeTest.cpp`:

```cpp
#define BOOST_TEST_MODULE TxConsensusOutcomeTest

#include "bcos-evm/eth/TxConsensusOutcome.h"
#include "bcos-evm/eth/pipeline/TxPipelineExitKind.h"
#include <boost/test/included/unit_test.hpp>

BOOST_AUTO_TEST_CASE(rejected_exit_kinds)
{
    using bcos::evm::TxPipelineExitKind;
    BOOST_CHECK(bcos::evm::isConsensusRejected(TxPipelineExitKind::RulesRejected));
    BOOST_CHECK(bcos::evm::isConsensusRejected(TxPipelineExitKind::GasAffordRejected));
    BOOST_CHECK(bcos::evm::isConsensusRejected(TxPipelineExitKind::IntrinsicRejected));
    BOOST_CHECK(!bcos::evm::isConsensusRejected(TxPipelineExitKind::Completed));
    BOOST_CHECK(!bcos::evm::isConsensusRejected(TxPipelineExitKind::ExceptionHandled));
}
```

Add to `EthTests.cmake`:

```cmake
add_executable(TxConsensusOutcomeTest eth/TxConsensusOutcomeTest.cpp)
target_include_directories(TxConsensusOutcomeTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(TxConsensusOutcomeTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME TxConsensusOutcome COMMAND TxConsensusOutcomeTest)
```

- [ ] **Step 2: Run test — expect compile fail**

Run: `rtk ctest -R '^TxConsensusOutcome$' -V`  
Expected: **FAIL** — header not found

- [ ] **Step 3: Implement `TxConsensusOutcome.h`** (copy from ADR-028 §1)

```cpp
#pragma once

#include "bcos-evm/eth/pipeline/TxPipelineExitKind.h"

namespace bcos::evm
{

enum class TxConsensusOutcome {
    Executed,
    Rejected,
};

inline bool isConsensusRejected(TxPipelineExitKind exitKind) noexcept
{
    switch (exitKind)
    {
    case TxPipelineExitKind::RulesRejected:
    case TxPipelineExitKind::GasAffordRejected:
    case TxPipelineExitKind::IntrinsicRejected:
        return true;
    default:
        return false;
    }
}

}  // namespace bcos::evm
```

- [ ] **Step 4: Run test — expect PASS**

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/eth/TxConsensusOutcome.h bcos-evm/test/eth/TxConsensusOutcomeTest.cpp \
  bcos-evm/test/cmake/EthTests.cmake
rtk git commit -m "$(cat <<'EOF'
feat(bcos-evm): add TxConsensusOutcome for ADR-028 inclusion gating

EOF
)"
```

---

### Task 6: Bridge/lifecycle propagate `consensusOutcome`

**Files:**
- Modify: `bcos-evm/eth/reference/EthReferenceBridge.h`
- Modify: `bcos-evm/eth/reference/EthReferenceBridge.cpp`
- Modify: `bcos-evm/opstack/OpStackExecutionBridge.h`
- Modify: `bcos-evm/opstack/OpStackTxLifecycle.cpp`
- Test: `bcos-evm/test/eth/EthIntrinsicGasFailureCharacterizationTest.cpp`
- Test: `bcos-evm/test/opstack/OpStackTxLifecycleCharacterizationTest.cpp`

**Interfaces:**
- Consumes: `isConsensusRejected`, `TxPipelineContext::exitKind`
- Produces: `EthReferenceResult::consensusOutcome`, `OpStackMessageResult::consensusOutcome`

- [ ] **Step 1: Extend result structs**

In `EthReferenceBridge.h`:

```cpp
#include "bcos-evm/eth/TxConsensusOutcome.h"

struct EthReferenceResult
{
    EVMCResult evmcResult{evmc_result{}};
    state::StateDiff stateDiff;
    std::vector<state::LogEntry> logs;
    EthExecutionContext executionContext;
    bool topLevelIncludedTxVmError{false};
    TxConsensusOutcome consensusOutcome{TxConsensusOutcome::Executed};
};
```

Mirror field on `OpStackMessageResult` in `OpStackExecutionBridge.h`.

- [ ] **Step 2: Set `Rejected` after pipeline early exit**

In `EthReferenceBridge.cpp`, after `runTxPipeline` returns, before building result:

```cpp
    if (isConsensusRejected(ctx.exitKind))
    {
        result.consensusOutcome = TxConsensusOutcome::Rejected;
    }
```

In `OpStackTxLifecycle.cpp`, on normal-L2 pre-execution reject paths (ADR-025 `isNormalPreExecutionReject`, `buyGas` false, gas pool acquire fail):

```cpp
        output.consensusOutcome = TxConsensusOutcome::Rejected;
```

**Do not** set `Rejected` for deposit entry failures (ADR-021/023).

- [ ] **Step 3: Update characterization tests**

In intrinsic-gas / afford-fail tests, add:

```cpp
BOOST_CHECK_EQUAL(output.consensusOutcome, bcos::evm::TxConsensusOutcome::Rejected);
```

Keep existing `OutOfGasLimit` / `InsufficientFunds` trace assertions on `evmcResult.status` (ADR-028 §4: mapping unchanged for trace).

- [ ] **Step 4: Run tests**

Run:

```bash
rtk ctest -R 'EthIntrinsicGasFailure|OpStackTxLifecycleCharacterization|TxConsensusOutcome'
```

Expected: **PASS**

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/eth/reference/EthReferenceBridge.h bcos-evm/eth/reference/EthReferenceBridge.cpp \
  bcos-evm/opstack/OpStackExecutionBridge.h bcos-evm/opstack/OpStackTxLifecycle.cpp \
  bcos-evm/test/eth/EthIntrinsicGasFailureCharacterizationTest.cpp \
  bcos-evm/test/opstack/OpStackTxLifecycleCharacterizationTest.cpp
rtk git commit -m "$(cat <<'EOF'
feat(bcos-evm): propagate TxConsensusOutcome on entry failure paths

ADR-028 Phase B: orchestration marks Rejected; trace evmcResult mapping unchanged.
EOF
)"
```

---

### Task 7: TE skip receipt when `consensusOutcome == Rejected`

**Files:**
- Modify: `transaction-executor/bcos-transaction-executor/EthTransactionExecutorImpl.h`
- Modify: `transaction-executor/bcos-transaction-executor/OpStackTransactionExecutorImpl.h`
- Test: extend characterization tests or add TE-boundary assertion in existing ETH/OP tests

**Interfaces:**
- Consumes: `EthReferenceResult::consensusOutcome` / `OpStackMessageResult::consensusOutcome`
- Produces: `Finalize` phase returns `nullptr` receipt when rejected

- [ ] **Step 1: Store consensus outcome on TE data**

In `EthTransactionExecutorImpl.h` `Execute` phase after `ethReferenceExecuteTx()`:

```cpp
                m_data->m_consensusRejected =
                    (output.consensusOutcome == bcos::evm::TxConsensusOutcome::Rejected);
```

Add `bool m_consensusRejected{false};` to executor data struct.

- [ ] **Step 2: Skip receipt in Finalize**

```cpp
            else if constexpr (phase == static_cast<int>(EthExecutePhase::Finalize))
            {
                if (m_data->m_consensusRejected)
                {
                    co_return {};
                }
                co_return co_await m_data->m_executor.get().m_txExecutor.makeReceipt(*m_data);
            }
```

Mirror in `OpStackTransactionExecutorImpl.h` `Finalize` / `makeReceipt()`.

Also set `m_consensusRejected = true` when `buyGas` returns false (ADR-028 §1 TE-layer reject).

- [ ] **Step 3: Run full parity characterization suite**

Run:

```bash
rtk ctest -R 'EthIntrinsicGasFailure|InsufficientBalanceGasLeft|TopLevelInsufficient|OpStackTxLifecycleCharacterization|OrchestrationErrorPolicy|EvmcStatusMapping'
```

Expected: **PASS** (update any test that still expects failed receipt on entry reject to expect null receipt at TE layer)

- [ ] **Step 4: Commit**

```bash
rtk git add transaction-executor/bcos-transaction-executor/EthTransactionExecutorImpl.h \
  transaction-executor/bcos-transaction-executor/OpStackTransactionExecutorImpl.h
rtk git commit -m "$(cat <<'EOF'
fix(transaction-executor): skip receipt on ADR-028 consensus reject

Closes GAP-001/002 at TE boundary for ETH/OpStack reference paths.
EOF
)"
```

---

## P4 — Documentation (ADR-029 + Capability Matrix)

### Task 8: ADR-029 + capability-matrix row

**Files:**
- Create: `bcos-evm/docs/adr/029-vm-domain-evmc-only.md`
- Modify: `bcos-evm/capability-matrix.md`
- Modify: `bcos-evm/docs/architecture-known-gaps.md` (close GAP-005/010/011; note GAP-009 partial until P3)

**Interfaces:**
- Produces: normative ADR linking spec §3–§4, P0–P4 delivery record

- [ ] **Step 1: Write ADR-029**

Sections: Context (dual mapping problem), Decision (Approach B + `EvmcStatusMap`), Domain boundary (§3.1 forbidden includes), Consequences, Test refs (`EvmcStatusMappingTest`, CI gate).

- [ ] **Step 2: Add capability-matrix row**

```markdown
| EVMC-only VM domain + unified status map | kernel | inherited (`EvmcStatusMap`; adoptEvmcResult projection) | inherited | inherited | `EvmcStatusMappingTest`, `check-vm-domain-transaction-status.sh` |
```

- [ ] **Step 3: Update known-gaps**

Mark GAP-005, GAP-010, GAP-011 **Closed** with ADR-029 ref. GAP-009 **Partial** until P3 + ADR-028.

- [ ] **Step 4: Run CI gates locally**

Run:

```bash
bash bcos-evm/tools/ci/check-capability-matrix.sh
bash bcos-evm/tools/ci/check-vm-domain-transaction-status.sh
```

Expected: **PASS**

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/docs/adr/029-vm-domain-evmc-only.md bcos-evm/capability-matrix.md \
  bcos-evm/docs/architecture-known-gaps.md
rtk git commit -m "$(cat <<'EOF'
docs(bcos-evm): ADR-029 VM domain EVMC-only semantics + matrix row

EOF
)"
```

---

## Self-Review (plan vs spec)

| Spec requirement | Task |
| --- | --- |
| §4.1 `EvmcStatusMap` single table | Task 1–2 |
| §4.2 refactor APIs / no throw | Task 2 |
| §5 GAP-009 precheck vs execution | Task 1 keeps doc test; Task 6–7 (P3) |
| §7 P0 flip `EvmcStatusMappingTest` | Task 1–2 |
| §7 P1 CI forbidden includes | Task 3 |
| §7 P2 execution purity | Task 4 |
| §7 P3 ADR-028 | Task 5–7 |
| §7 P4 ADR-029 | Task 8 |
| §8 testing matrix | Tasks 1–2, 4, 6–7 |
| §9 success criteria 1–5 | P0–P4 tasks |
| YAGNI: FISCO untouched | Global Constraints |
| ADR-015 normalize outside VM | Global Constraints; no ExecutionFrame changes |

**Placeholder scan:** None — all steps include concrete code/commands.

**Type consistency:** `mapEvmcToTransactionStatus` used consistently; `consensusOutcome` field name matches ADR-028.

---

## Full Regression Command (pre-merge)

```bash
cd build-bcos-evm-check
cmake --build . -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
rtk ctest -R 'EvmcStatusMapping|TxConsensusOutcome|EthIntrinsicGasFailure|InsufficientBalanceGasLeft|PrecompileRouterEnvelope|ExecutionFrame$|TopLevelInsufficient|EthIncludedTxVmerr|OpStackTxLifecycleCharacterization|OrchestrationErrorPolicy'
bash bcos-evm/tools/ci/check-vm-domain-transaction-status.sh
bash bcos-evm/tools/ci/check-eth-layer-boundary.sh
```
