# opstack-executor

An OP Stack (Optimism L2) transaction executor implementing the
`bcos::executor_v1::TransactionExecutor` concept. It is the OP-specific analogue of
`ethereum-executor` (PR #5366): instead of evmone's stock `validate_transaction` / `transition`,
it drives the OP transition pipeline from `bcos-evm/opstack` — `opValidateFromState` followed by
`opTransition` — so base / L1-data / operator fees are routed to the four OP fee vaults with
op-geth-faithful pricing.

## Scope (v0)

- **Normal transactions only.** Legacy / EIP-2930 / EIP-1559 (and, once
  `Transaction::authorizationList()` is available, EIP-7702 set-code) are executed.
- **Deposits (0x7E) are out of scope.** `protocol::Transaction` carries no
  `source_hash` / `mint` / `is_system_tx` fields, and deposits are a block-level concern. See
  the block-level follow-up in the project notes.
- **Receipt** is the base (evmone) receipt. The OP receipt meta (`OpReceiptMeta`: `l1_fee`,
  `operator_fee`, DA footprint, …) has no field on `protocol::TransactionReceipt` yet — surfacing
  it is a receipt-extension follow-up.
- **Fork selection** is explicit at construction (default: `jovianConfig()`), because
  `LedgerConfig` does not yet expose an OP fork schedule. Timestamp-driven selection is a
  follow-up tied to the Karst adaptation.

## Build dependency: PR #5366 (`ethereum-executor`)

This module **depends on the `ethereum-executor` module** (PR #5366), which is not yet merged.
It reuses, verbatim, `ethereum-executor`'s:

- `BCOS2Evmone.h` — `blockHeaderToBlockInfo`, `bcosTransactionToEvmone`, `applyStateDiff`,
  `evmoneReceiptToBcos`, `ZeroBlockHashes`;
- `StorageStateView.h` — the storage-backed read-only `evmone::state::StateView`;
- `LedgerConfig::evmcRevision()` and the `Transaction` accessors #5366 adds
  (`authorizationList()` for EIP-7702; our base already provides `extraTransactionBytes()` — the
  OP L1-cost envelope — and `web3AccessList()`).

`#5366`'s branch has a stale merge-base relative to `feat-evm-opstack-port`, so it cannot be
cleanly merged in locally today (a full merge drags in ~230 unrelated files and conflicts on the
tars `TransactionImpl`). The intended sequencing is therefore:

1. `#5366` merges to `release-3.18.0`.
2. This branch (or its PR) **rebases** on the updated `release-3.18.0`; the `ethereum-executor`
   target and the framework additions then exist, and this module builds and its tests run
   unchanged.

Until then the repo-root `add_subdirectory(opstack-executor)` is **guarded on the
`ethereum-executor` target existing**, so the tree stays buildable before #5366 lands.

## Files

- `OpstackExecutor.h` — the executor (header-only template over the storage type).
- `CMakeLists.txt` — `INTERFACE` library `opstack-executor`.
- `tests/OpstackExecutorTest.cpp` — GTest unit tests (construction, fork/revision guard,
  end-to-end normal transfer over BCOS storage). Runnable after the #5366 rebase.
