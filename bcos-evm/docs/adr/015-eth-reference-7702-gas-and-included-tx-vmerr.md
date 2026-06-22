# ADR-015: ETH Reference — EIP-7702 Auth Intrinsic and Included-Tx vmerr Settlement

**Status:** Accepted  
**Date:** 2026-06-21  
**Related:** ADR-005 (orchestration boundaries), ADR-006 (7702 gating), `capability-matrix.md` rows 7702 precheck / 7623 settlement

---

## Context

EEST `test_self_sponsored_set_code` (Osaka) exposed three reference-path gaps vs geth `state_transition.go`:

1. **7702 auth intrinsic (25_000 / tuple)** was checked but not debited or included in EIP-7623 settlement → wrong `gasUsed`, sender balance, and `stateRoot` on success paths.
2. **Zero-value SSTORE to an empty slot** was persisted in the in-memory account map → extra storage trie entries vs geth (`SetState` no-op when `prev == value`).
3. **Top-level EVM vmerr** (`INVALID`, `REVERT`, `OUT_OF_GAS`, …) was surfaced as transaction rejection (`EVMC_UNDEFINED_INSTRUCTION`, etc.) even though geth returns `ExecutionResult{Err: vmerr}` with `TransitionDb err == nil` — 7702 authorizations applied before `Call` remain committed.

GST reference tests without `expectException` assert `EVMC_SUCCESS` at the adapter boundary (included transaction semantics).

---

## Decision

### 7702 auth intrinsic (orchestration)

On the ETH reference path (`ExecuteViaEth.cpp`, when `eip7623`):

- Debit `calcAuthTupleIntrinsicGas(n)` from `message.gas` before `executeMessage` (same gross charge as geth `IntrinsicGas` + `CallNewAccountGas` per tuple).
- Record `authIntrinsic` on `gasSettlementSnapshot` and add it to `finalizeEthereumGasUsed`.
- Apply existence refund (`PER_EMPTY_ACCOUNT_COST - PER_AUTH_BASE_COST`) via normal `state.get_refund()` → `evmGasRefund` in settlement.

### Storage no-op (kernel)

`State::set_storage` returns early when `get_storage(key) == value` (geth `SetState` parity). GST `hashStorageTrie` / `buildPostStateView` omit zero-valued slots from the post-state trie.

### Included top-level vmerr (orchestration)

After preCheck succeeds and `executeMessage` runs at **depth 0**:

- **Do not** treat vm execution errors as transaction rejection for reference/GST parity.
- Normalize `evmc_result.status_code` to `EVMC_SUCCESS` for included-tx vmerr (exclude `INSUFFICIENT_BALANCE` and `INTERNAL_ERROR`).
- Set `ExecuteViaEthOutput::topLevelIncludedTxVmError` when normalization occurs.

**Gas settlement split (7623):**

| Path | Function | When |
| --- | --- | --- |
| Successful EVM | `finalizeEthereumGasUsed` | `status == SUCCESS` and not `topLevelIncludedTxVmError` |
| Included-tx vmerr | `settleIncludedTopLevelTransactionGas` | `topLevelIncludedTxVmError` — geth `peakGasUsed` + EIP-3529 refund cap model |

`executeMessage` captures `gasRefund` on the failure/revert path after checkpoint revert so auth existence refund is visible to settlement.

Nested calls (`depth > 0`) keep raw `evmc_status_code`.

---

## Consequences

- Matrix row **EIP-7702 precheck + intrinsic gas** on ETH (reference) moves from `unsupported` to `explicit`.
- EEST smoke covers all ten `self_sponsored_set_code` Osaka variants (114 subtests including warming + invalid auth signature expansion).
- TE baseline (BCOS/OPStack) unchanged by this ADR; OPStack already uses `postExecuteGasSettlement` for vmerr paths.
- Receipt-level failure signaling on production TE executors remains a separate product concern; this ADR governs **reference path + GST adapter** only.

---

## References

- geth `core/state_transition.go` — `IntrinsicGas`, `applyAuthorization`, `TransitionDb` vmerr handling
- `ExecuteViaEth.cpp`, `EthTxGasSettlement.h`, `ExecuteViaEthAdapter.cpp`
- EEST probes: `manifests/eth-eest-probe-{return,invalid,revert,oog}.json`
