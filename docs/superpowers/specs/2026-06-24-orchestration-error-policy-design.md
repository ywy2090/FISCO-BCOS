# OrchestrationErrorPolicy — typed error mapper seam

**Status:** Accepted (post-brainstorming)
**Date:** 2026-06-24
**Related:** ADR-005, ADR-015, ADR-019, `2026-06-24-orchestration-profile-design.md`, `architecture-review-post-orchestration-2026-06-23.md` candidate 4

---

## 1. Problem

ADR-019 moved the shared transaction orchestration steps into `runOrchestration()`, and
`OrchestrationProfile` moved chain strategy hooks out of the three wrappers. The remaining
wrapper-local hook bodies are the error mappers:

- `mapIntrinsicFailure`
- `mapException`

These mappers encode chain-specific error taxonomy: Eth maps intrinsic failures to
`OutOfGasLimit`; Fisco has the `fixErrorHandling` matrix and `NotFoundCodeError` special cases;
OpStack maps both deposit and normal paths through the same out-of-gas/internal-error helpers.

Because these rules remain inline in `ExecuteViaEth.cpp`, `ExecuteViaHost.cpp`, and
`OpStackExecuteViaHost.cpp`, understanding how one exception becomes `EVMCResult`,
`TransactionStatus`, `gas_left`, and state revert behavior still requires reading three wrapper
implementations.

---

## 2. Decision

Introduce named per-chain error policy modules:

```text
bcos-evm/eth/
  EthOrchestrationErrorPolicy.h/.cpp

bcos-evm/bcos/
  FiscoOrchestrationErrorPolicy.h/.cpp

bcos-evm/opstack/
  OpStackOrchestrationErrorPolicy.h/.cpp
```

Each policy exposes `Session` plus `attach(OrchestrationHooks&, Session const&)`.
The policy attaches the existing `mapIntrinsicFailure` and `mapException` lambdas to
`OrchestrationHooks`.

This spec intentionally keeps `runOrchestration()` and `OrchestrationHooks` ABI stable. It does
not remove the error hook fields from `OrchestrationHooks`; it only gives them a named,
testable source.

---

## 3. Public Interface

### 3.1 Common Shape

```cpp
namespace bcos::evm {

struct XxxOrchestrationErrorPolicy
{
    struct Session
    {
        // Chain-specific dependencies needed only for error mapping.
    };

    static void attach(OrchestrationHooks& hooks, Session const& session);
};

}  // namespace bcos::evm
```

### 3.2 Eth Session

```cpp
struct EthOrchestrationErrorPolicy
{
    struct Session
    {};

    static void attach(OrchestrationHooks& hooks, Session const& session);
};
```

Eth currently needs no external dependencies for error mapping.

### 3.3 Fisco Session

```cpp
struct FiscoOrchestrationErrorPolicy
{
    struct Session
    {
        bcos::crypto::Hash const& hashImpl;
        bool fixErrorHandling{false};
    };

    static void attach(OrchestrationHooks& hooks, Session const& session);
};
```

Fisco error mapping depends on `makeErrorEVMCResult()`, the configured hash implementation, and
the `fixErrorHandling` compatibility switch.

### 3.4 OpStack Session

```cpp
struct OpStackOrchestrationErrorPolicy
{
    struct Session
    {};

    static void attach(OrchestrationHooks& hooks, Session const& session);
};
```

OpStack currently maps through the existing `makeOutOfGasLimitResult()` and
`makeInternalErrorResult()` helpers.

### 3.5 Wrapper Shape

```cpp
auto hooks = XxxOrchestrationProfile::buildHooks(profileSession);
XxxOrchestrationErrorPolicy::attach(hooks, errorSession);
runOrchestration(ctx, hooks);
```

Wrappers remain responsible for validation, extension lifetime, `OrchestrationContext`
construction, async fee/state-machine boundaries, and final output mapping.

---

## 4. Scope

### 4.1 In Scope

| Chain | Migrated behavior |
| --- | --- |
| Eth | `mapIntrinsicFailure`; `mapException`; checkpoint revert after exception |
| Fisco | `mapIntrinsicFailure`; `mapException`; `fixErrorHandling` gas/status matrix; `NotEnoughCashError`; `NotFoundCodeError` |
| OpStack | `mapIntrinsicFailure`; `mapException`; shared attach path for deposit and normal execution |

### 4.2 Out of Scope

- Eth `postAdopt` included-transaction vmerr normalization.
- OpStack `postSettle` and gas settlement.
- OpStack `buyGas`, `refundGas`, deposit mint/nonce, gas pool hooks, and state-machine branches.
- `runOrchestration()` signature changes.
- Removing `mapIntrinsicFailure` or `mapException` fields from `OrchestrationHooks`.
- New public executor facade, new `ExecuteVia*Input`/`Output` types, or mapper structs.
- Applying ADR-015 Eth vmerr normalization to Fisco or OpStack.

---

## 5. Boundary Rules

- `eth/` must not include `bcos/` or `opstack/` headers.
- `eth/orchestration/` remains portable and must not depend on chain-specific policies.
- Error policies may include their own chain-local helpers.
- Error policies are not profiles: profiles own chain strategy hooks; error policies own only
  failure/exception mapping hooks.
- Compatibility behavior is preserved exactly. In particular, Fisco `fixErrorHandling=false`
  remains a first-class path.

---

## 6. Migration Plan

### P1: Eth Error Policy

Create `EthOrchestrationErrorPolicy` and migrate the Eth wrapper's two error hooks.

Required tests:

- Intrinsic failure maps to `EVMC_OUT_OF_GAS` and `OutOfGasLimit`.
- `protocol::OutOfGas` maps to `EVMC_OUT_OF_GAS` and `OutOfGasLimit`.
- Generic `std::exception` maps to `EVMC_INTERNAL_ERROR` and `Unknown`.
- Exception mapping reverts an active checkpoint.

### P2: OpStack Error Policy

Create `OpStackOrchestrationErrorPolicy` and use it from both deposit and normal paths.

Required tests:

- Intrinsic failure maps through `makeOutOfGasLimitResult()`.
- Exception maps through `makeInternalErrorResult()`.
- Deposit and normal wrapper paths both call the same policy.

### P3: Fisco Error Policy

Create `FiscoOrchestrationErrorPolicy` and migrate the Fisco compatibility matrix.

Required tests:

- Intrinsic failure reasons distinguish gas limit minimum, calldata OOG, and auth tuple OOG.
- `protocol::OutOfGas` maps to `OutOfGas`.
- `protocol::NotEnoughCashError` maps to `NotEnoughCash` and `EVMC_INSUFFICIENT_BALANCE`.
- `NotFoundCodeError` in static/delegate context maps to success with unchanged gas.
- `NotFoundCodeError` in normal call maps to `EVMC_REVERT`.
- Generic exception preserves existing `fixErrorHandling=false/true` status and gas-left
  behavior.
- Exception mapping reverts an active checkpoint.

### P4: Cleanup Gate

Remove the existing candidate-4 marker comments from wrappers once the inline error hook bodies
are gone.

---

## 7. Testing and Regression Gate

Unit tests:

```text
bcos-evm/test/eth/EthOrchestrationErrorPolicyTest.cpp
bcos-evm/test/opstack/OpStackOrchestrationErrorPolicyTest.cpp
bcos-evm/test/bcos/FiscoOrchestrationErrorPolicyTest.cpp
```

Regression tests:

- Existing `EthOrchestrationProfileTest`
- Existing `OpStackOrchestrationProfileTest`
- Existing `FiscoOrchestrationProfileTest`
- `EthIncludedTxVmerr`, to confirm ADR-015 behavior remains outside ErrorPolicy
- Existing Fisco and OpStack smoke gates used by the profile phases

Success requires zero behavior changes in wrapper outputs, `EVMCResult`, transaction status,
state checkpoint behavior, and gas-left semantics.

---

## 8. Rejected Alternatives

### 8.1 Pass policy directly to `runOrchestration()`

This gives the cleanest type boundary but changes the pipeline signature and all call sites. It is
unnecessary for the current follow-up because `OrchestrationHooks` already contains the needed
error hook fields.

### 8.2 Merge error hooks into `XxxOrchestrationProfile`

This reduces file count but blurs the boundary established by `OrchestrationProfile`: profiles
own chain strategy hooks, while error policies own error mapping. Keeping them separate gives a
smaller test surface and prevents profile modules from becoming wrapper-shaped again.

### 8.3 Move Eth included-tx vmerr `postAdopt` into ErrorPolicy

Included-transaction vmerr normalization is ADR-015 Eth-specific post-adoption behavior, not a
generic exception/intrinsic-failure mapper. Moving it into ErrorPolicy would make the policy
responsible for successful kernel-result normalization and invite accidental reuse by Fisco or
OpStack.

---

## 9. Success Criteria

1. The three wrappers no longer inline `mapIntrinsicFailure` or `mapException` hook bodies.
2. `runOrchestration()` signature remains unchanged.
3. `OrchestrationHooks` field layout remains unchanged.
4. Each chain has a named ErrorPolicy module and focused taxonomy tests.
5. Eth included-tx vmerr normalization remains in Eth Profile.
6. OpStack settlement remains in OpStack Profile / future `OpStackSettlementContext` work.
7. ADR-005 remains true: `eth/` has no `bcos/` or `opstack/` include.
8. Existing behavior is preserved for status, gas-left, state revert, logs, and output fields.
