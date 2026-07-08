# FISCO Per-Frame Semantics Regressions (EVM Kernel Refactor)

**Date:** 2026-07-08
**Scope:** `bcos-evm/bcos/` (FISCO layer) vs legacy `bcos-executor/src/executive/TransactionExecutive.cpp`
**Status:** 3 CONFIRMED consensus/security regressions — **require a design decision, not a surgical fix**
**Verification:** Findings confirmed by direct source comparison (legacy vs current tree). No fix applied.

---

## TL;DR

The EVM-kernel refactor moved from a model where **every call frame** ran through a fresh
`TransactionExecutive` (which applied FISCO-specific value transfer, balance policies, and auth
checks per frame) to a model where a **single evmone execution** handles nested calls internally
via `EthHost::call → runCallFrame`. FISCO's per-frame logic now lives **only** in the top-level
`StateTransitionHooks` and never fires for nested `CALL`/`CREATE` frames.

Three per-frame behaviors were lost:

| # | Behavior | Legacy (per-frame) | Current (top-level only) | Impact |
|---|----------|--------------------|--------------------------|--------|
| #3 | Native value transfer | `TransactionExecutive.cpp:306-341` (incl. `seq != 0` nested) | `maybeTransferValue` in `onPreCheckCanTransfer` (top-level) | Funds accounting divergence |
| #4 | `feature_balance_policy2` whitelist | `TransactionExecutive.cpp:329-336` | **Absent entirely** in `bcos-evm` | Access-control bypass |
| #5 | Auth checks (frozen/abolished/ACL) | `TransactionExecutive.cpp:709, 827` (per executive) | `FiscoStateTransitionHooks.cpp:74` (top-level only) | Freeze/ACL bypass via any intermediary |

Additionally, **`feature_balance_policy1` (disable-transfer) and the `balance_transfer` system
config check are also dropped** (legacy `TransactionExecutive.cpp:292-304`), a superset of #4.

---

## Root cause (shared)

**Legacy model:** each nested call created a new `TransactionExecutive`; its `execute()` ran value
transfer + balance policies, and `callExec`/`createExec` ran `checkAuth` — so FISCO could intercept
**every** call depth.

**Current model:** one evmone execution; nested `CALL`/`CREATE` recurse through
`EthHost::call → runCallFrame` (`bcos-evm/eth/host/EthHost.cpp:300`). FISCO-specific per-frame
logic was migrated to the **top-level** `FiscoStateTransitionHooks` pipeline hooks
(`onPreCheckCanTransfer`, `onPreCheckRules`), which run **once** for the outermost message. Nested
frames execute entirely inside the kernel and never re-enter the FISCO hook surface.

The kernel deliberately exposes `EvmHostHooks::skipHostValueTransfer()` so FISCO can "settle value
elsewhere" (`bcos-evm/eth/kernel/execution/EvmCallFrame.cpp:226`). That skip is applied for **all**
frames, but the replacement (`maybeTransferValue`) only runs at the top level — the migration is
incomplete for value, and was never done at all for policy2/policy1/auth.

---

## Finding #3 — Nested value transfer dropped

**Legacy** (`bcos-executor/src/executive/TransactionExecutive.cpp:306-341`):
```cpp
if (m_blockContext.features().get(Flag::feature_balance) && callParameters->value > 0)
{
    ...
    bool transferFromEVM = callParameters->seq != 0;   // <-- nested call, seq != 0
    int64_t requiredGas = transferFromEVM ? 0 : BALANCE_TRANSFER_GAS;
    ...
    callParameters = transferBalance(std::move(callParameters), requiredGas, currentContextAddress);
}
```
`execute()` runs per executive (per frame), and explicitly handles nested (`seq != 0`) transfers.

**Current:**
- Kernel `transferOrFail` (`EvmCallFrame.cpp:231-247`) short-circuits when
  `extension->skipHostValueTransfer()` is true — which FISCO returns whenever
  `enable_balance_transfer` is on (`FiscoEvmHostHooks::skipHostValueTransfer`,
  wired from `FiscoExecutionBundle.h:17`).
- The only FISCO value transfer is `maybeTransferValue` (`FiscoPipelineInternals.h:56`) called from
  `FiscoStateTransitionHooks::onPreCheckCanTransfer` (`FiscoStateTransitionHooks.cpp:84-88`) — a
  **top-level** hook.

**Failure scenario:** On a balance-enabled FISCO chain, contract A executes payable
`B.deposit{value: X}()`. The nested `CALL` succeeds and B's code runs with `msg.value == X`, but
**no balance is debited from A or credited to B**. Bookkeeping computed inside B diverges from
actual account balances; replay across executor versions produces different state roots.

---

## Finding #4 — `feature_balance_policy2` whitelist bypass

**Legacy** (`TransactionExecutive.cpp:329-336`):
```cpp
if (m_blockContext.features().get(Flag::feature_balance_policy2) &&
    !checkTransferPermission(storage, senderAddress) &&
    !checkTransferPermission(storage, receiveAddress))
{
    callParameters->status = PermissionDenied;   // reject transfer
}
else { transferBalance(...); }
```

**Current:** `grep -r "balance_policy2|checkTransferPermission" bcos-evm/` returns **nothing**.
`FiscoRevisionConfig` (`bcos-evm/bcos/FiscoRevisionConfig.h`) carries only
`enable_balance_transfer` — no `policy1`/`policy2` flags. `maybeTransferValue` performs the transfer
with no whitelist gate.

**Failure scenario:** On a chain with `feature_balance` + `feature_balance_policy2`, a
non-whitelisted account sends a value transfer. Legacy rejects with `PermissionDenied` (no balance
moves); the new executor executes the transfer — an **access-control bypass** letting unauthorized
parties move funds, plus divergence between old- and new-executor nodes.

**Also dropped (superset):** `feature_balance_policy1` disable-transfer and the
`SystemConfig::balance_transfer == "0"` check (`TransactionExecutive.cpp:292-304`), which zeroed
`value` before transfer.

---

## Finding #5 — Auth checks run only at top level

**Legacy** (`TransactionExecutive.cpp:709` in `callExec`, `:827` in `createExec`):
```cpp
if (m_blockContext.isAuthCheck() && !checkAuth(callParameters)) { revert(); return ...; }
```
`checkAuth` (`:1912+`) enforces account frozen/abolished status, contract frozen status, and method
ACL. Because a new executive is created per call, this runs at **every** call depth.

**Current:** `AuthPort::checkAuth` is invoked only in
`FiscoStateTransitionHooks::onPreCheckRules` (`FiscoStateTransitionHooks.cpp:74`) — the top-level
message. Nested `CALL`/`CREATE` frames run via `EthHost::call → runCallFrame` and never consult
`AuthPort`.

**Failure scenario:** On an auth-enabled FISCO chain, governance freezes contract B (or revokes a
caller's method ACL on B). A tx calls proxy contract A, which internally `CALL`s B. Legacy reverts
with `ContractFrozen`/`PermissionDenied`; the new executor **executes B and commits its state
changes** — the freeze/ACL is bypassed via any intermediary contract, and mixed-version validator
sets fork on the block. **Security-relevant.**

---

## Recommended remediation (design decision required)

This is an **architectural migration gap**, not a local bug. Options:

1. **Re-establish per-frame FISCO semantics in the host-callback path.** Give
   `FiscoEvmHostHooks` (or `FiscoChainCallTargetAdapter`) a per-nested-call hook that runs, for each
   `CALL`/`CREATE` frame entered via `EthHost::call`:
   - value transfer (respecting `enable_balance_transfer`, policy1 disable, `balance_transfer`
     config),
   - policy2 whitelist (`checkTransferPermission`),
   - auth checks (`AuthPort::checkAuth` for frozen/abolished/ACL).
   Requires: adding `policy1`/`policy2` flags to `FiscoRevisionConfig`, wiring them through
   `FiscoPolicy`, and porting `checkTransferPermission` into the FISCO layer.

2. **Confirm the top-level-only migration is intentional** for a defined contract set, document the
   semantic change, and gate it behind a compatibility feature flag so old chains keep per-frame
   behavior on replay.

Either path needs FISCO/EEST characterization tests covering nested `CALL`/`CREATE` with value,
policy2 whitelist, and frozen/ACL intermediaries.

**Constraints noted at review time:**
- All three live in the FISCO layer currently under active development by a parallel work stream.
- The fix touches consensus-critical value/auth semantics — must be validated against legacy replay.

---

## Evidence index (file:line)

- Legacy per-frame value + policy1/policy2: `bcos-executor/src/executive/TransactionExecutive.cpp:292-341`
- Legacy per-frame auth: `bcos-executor/src/executive/TransactionExecutive.cpp:709, 827, 1912+`
- Kernel value-transfer skip: `bcos-evm/eth/kernel/execution/EvmCallFrame.cpp:231-247`
- FISCO skip flag wiring: `bcos-evm/bcos/FiscoExecutionBundle.h:17`
- FISCO top-level value transfer: `bcos-evm/bcos/FiscoPipelineInternals.h:56`, `bcos-evm/bcos/FiscoStateTransitionHooks.cpp:84-88`
- FISCO top-level auth: `bcos-evm/bcos/FiscoStateTransitionHooks.cpp:74`
- Nested-call re-entry: `bcos-evm/eth/host/EthHost.cpp:300`
- `FiscoRevisionConfig` flag set: `bcos-evm/bcos/FiscoRevisionConfig.h` (only `enable_balance_transfer`)
