# Task 5 Report: EthHost SSTORE refund accumulation

**Status:** DONE  
**Commit:** `b0141647743de564091d5c9ac74db5153ca4ceda`  
**Message:** `feat(eth): EthHost accumulates SSTORE refunds into State`

## Summary

Wired EIP-3529 SSTORE gas refunds from `EthHost::set_storage` into `State::add_refund`, and reset the counter at transaction entry in `executeMessage`.

## Implementation

### `EthHost::set_storage`

After `classifyStorageStatus`, when status is `EVMC_STORAGE_DELETED` (non-zero slot cleared to zero), call:

```cpp
m_state.add_refund(4800);  // params.SstoreClearsScheduleRefundEIP3529
```

Matches geth `core/vm/operations_acl.go` `gasSStoreEIP3529` — only `StorageDeleted` schedules refund; other statuses (`ASSIGNED`, `ADDED`, `MODIFIED`) do not.

### `executeMessage`

`state.clear_refund()` at tx entry (sole call site), before `warmTransactionEntry`, so each transaction starts with a zero refund counter.

## TDD

1. Added failing `SstoreRefundTest::EthHost_sstoreClear_accumulates4800` — non-zero slot → zero via `executeMessage`, expect `state.get_refund() == 4800`.
2. Implemented refund mapping in `EthHost::set_storage` and `clear_refund()` in `executeMessage`.
3. All tests green.

## Test results

```text
cd build/bcos-evm/test && ctest -R 'SstoreRefund|SstoreStatus|StateRefund' --output-on-failure
3/3 tests passed

cd build/bcos-evm/test && ctest --output-on-failure
21/21 tests passed (full bcos-evm regression)
```

## Files changed

| File | Action |
|------|--------|
| `bcos-evm/eth/state/EthHost.cpp` | `add_refund(4800)` on `EVMC_STORAGE_DELETED` |
| `bcos-evm/eth/executeMessage.cpp` | `clear_refund()` at tx entry |
| `bcos-evm/test/state/SstoreRefundTest.cpp` | created |
| `bcos-evm/test/CMakeLists.txt` | add `SstoreRefund` test target |

## Next

Task 6: OpStack post-execute gas settlement using `state.get_refund()` with EIP-3529 cap (`min(refund, peakGasUsed/5)`).
