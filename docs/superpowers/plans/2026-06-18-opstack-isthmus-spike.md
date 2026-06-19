# OpStack Isthmus — Day-0 Spike: evmone Refund + EVMC_PRAGUE

**Date:** 2026-06-19  
**Task:** 0 (Q10-A validation)  
**Test:** `bcos-evm/test/opstack/EvmoneRefundSpikeTest.cpp`

## Question

Does evmone report `evmc_result.gas_left` as **pre-refund** (Branch A / `DEFERRED_REFUND`) or **post-refund** (Branch B / `PRE_APPLIED_REFUND`)?

The Isthmus spec (Q10-A) mandates Branch A: settlement uses explicit `calcRefund` from `State::get_refund()`, ignoring `evmc_result.gas_refund` for the refund counter source but treating `gas_left` as pre-refund remaining.

## Setup

| Parameter | Value |
|-----------|-------|
| EVM revision | `EVMC_PRAGUE` (available in current vcpkg evmone) |
| Bytecode | `600060005500` — `PUSH1 0 PUSH1 0 SSTORE STOP` |
| Pre-state (clear case) | Storage slot 0 = non-zero (`0x01`) |
| Baseline | Same bytecode, slot 0 = zero (no refund) |
| Gas limit | 1,000,000 |
| Host | `EthHost` + `InMemoryStateView`, direct `vm.execute` |
| `fix_storage_status` | `true` (4-state SSTORE status) |

## Measured Values (clear case)

| Field | Value |
|-------|-------|
| `gas_left` (clear) | 994,994 |
| `gas_left` (baseline) | 997,794 |
| `gas_refund` | 4,800 (EIP-3529 SSTORE clear refund) |
| `peak_gas_used` | 5,006 (`gas_limit - gas_left`) |
| `capped_refund` | 1,001 (`min(4800, peak/5)` per EIP-3529) |
| `gas_remaining` (deferred model) | 995,995 (`gas_left + capped_refund`) |
| `gas_used` (deferred model) | 4,005 |

## Classification

**Outcome: `DEFERRED_REFUND` (Branch A)**

Discriminator: compare clear vs baseline `gas_left`. Under PRE_APPLIED, the clear case would show `gas_left` inflated by `capped_refund` relative to execution-cost delta. Observed delta (2,800 gas) matches raw execution cost difference only — refund appears solely in `gas_refund`, not in `gas_left`.

## Implications for Implementation

| Branch | Task 6 action |
|--------|---------------|
| **DEFERRED_REFUND** (observed) | No evmone shim needed. `postExecuteGasSettlement` uses `gas_left` as pre-refund, applies `min(State::get_refund(), peakGasUsed/5)`. |
| PRE_APPLIED_REFUND (not observed) | Would require host shim subtracting `cappedRefund` from `gas_left` before settlement. |

## EVMC_PRAGUE Availability

`EVMC_PRAGUE` is available and used successfully. No fallback revision required.

## Balance Delta

Not applicable for this spike — direct `vm.execute` path does not route fee settlement through `OpStackTxExecutor`. Gas semantics only.

## Commands

```bash
cmake -B build -S .
cmake --build build --target EvmoneRefundSpikeTest -j
./build/bcos-evm/test/EvmoneRefundSpikeTest
ctest --test-dir build/bcos-evm/test --output-on-failure
```
