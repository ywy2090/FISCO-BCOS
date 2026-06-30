# ADR-016: ETH Reference / TE — EIP-1559 Effective Gas and Tip Settlement

**Status:** Accepted  
**Date:** 2026-06-22  
**Related:** ADR-005 (orchestration boundaries), ADR-015 (7623 / included-tx vmerr), `capability-matrix.md` EIP-1559 row, spec v1.2 (`docs/superpowers/specs/2026-06-21-eth-eip1559-settlement-design.md`)

---

## Context

EIP-1559 splits execution gas payment into a **base fee** (burned) and a **priority fee (tip)** routed to the block coinbase. Before this ADR, the ETH TE path used `protocol::effectiveGasPrice()` (which returns `maxFeePerGas` for type-2 txs, not the geth effective formula), and `ExecuteViaEth` passed legacy `gasPrice` / `maxFeePerGas` into `executeMessage`, breaking the `GASPRICE` opcode on reference fixtures that read it.

EEST `stExample/eip1559` exercises `GASPRICE` and `BASEFEE` under a non-zero `currentBaseFee`; GST adapter settlement already used the correct effective formula but duplicated it locally.

---

## Decision

### Shared formulas (`eth/eip/Eip1559.h`)

**1559 tx recognition** — sole criterion `isEip1559GasCapsTx(web3TypedTxKind, hasExplicitFeeCaps)`:

- Type-2 (0x02) and type-4 (0x04): always 1559 caps.
- Legacy (0x00) and type-1 (0x01): never 1559, even if numeric fields are pre-filled.

**Effective gas price** (geth `EffectiveGasPrice` / `state_transition.go`):

```
effective = min(gasTipCap + baseFee, gasFeeCap)
```

Implemented as `resolveEffectiveGasPrice(gasTipCap, gasFeeCap, baseFee)`.

**Tip per gas** (coinbase credit):

```
tipPerGas = effective > baseFee ? effective - baseFee : 0
```

Implemented as `tipPerGas(effectiveGasPrice, baseFee)`.

**Balance precheck (1559):** `maxBalanceGasDebit = gasLimit × gasFeeCap` (fee cap, not effective).

### Base fee burn (not credited)

Mainnet semantics: the sender pays `finalGasUsed × effectiveGasPrice`; the coinbase receives only `finalGasUsed × tipPerGas`. The base-fee portion (`finalGasUsed × baseFee`) is **destroyed** — no account is credited.

Both settlement models honor this:

| Model | Mechanism |
| --- | --- |
| TE (`EthTxExecutor::refundGas`) | Pre-debit `gasLimit × effective`; refund unused at effective; credit coinbase `finalGasUsed × tipPerGas`; never mint base fee |
| GST adapter (`applyGstTransactionSettlement`) | Post-hoc debit sender `finalGasUsed × effective`; credit coinbase `finalGasUsed × tipPerGas` |

### TE buyGas/refund vs GST post-hoc equivalence

| | TE (geth production) | GST adapter (EEST) |
| --- | --- | --- |
| Timing | `buyGas` pre-debit → execute → `refundGas` | execute → one-shot post-hoc settlement |
| Sender net | `finalGasUsed × effective` | same |
| Coinbase | `finalGasUsed × tipPerGas` | same |
| Base fee | burned | same |

Equivalence holds when **`finalGasUsed` is identical** on both paths (see below).

### `finalGasUsed` and ADR-015 / 7623 cross

`finalGasUsed` is the gas unit count used for fee settlement (sender debit, coinbase tip, unused refund).

| Scenario | TE: `settleGasUsedFromEvmResult` | GST adapter |
| --- | --- | --- |
| Success + `eip7623` | `finalizeEthereumGasUsed(snapshot, floorToken)` | same |
| Included top-level vmerr + `eip7623` | `settleIncludedTopLevelTransactionGas(...)` (ADR-015) | same |
| Success, no 7623 | `gasLimit - evmGasLeft` | `TX_BASE_GAS + result.gasUsed` (GST legacy only) |

1559 tip credit always uses **`finalGasUsed`**, not raw EVM gas_left delta. Included-tx vmerr (ADR-015) remains **included** — settlement and coinbase tip still apply when `stateDiff` is non-empty.

### ADR-005 boundary — `ExecuteViaEth` no balance settlement

Per ADR-005 gas-settlement domain:

| Responsibility | Layer |
| --- | --- |
| Formulas, `isEip1559GasCapsTx` | `eth/eip/Eip1559.h` |
| Sender pre-debit / refund, coinbase tip | orchestration (`EthTxExecutor`, GST adapter) |
| `GASPRICE` / `BASEFEE` EVM context | `ExecuteViaEth` → `executeMessage` |
| Fee-cap precheck | `ExecuteViaEthPreCheck` (existing W4) |

`ExecuteViaEth` **normalizes** `input.gasPrice` to effective for 1559 txs (including `eth_call`) but **does not** mutate `State` balances. Reference parity balances come from the adapter's post-hoc `applyGstTransactionSettlement` or from TE `buyGas`/`refundGas`.

### Known debt — `protocol::effectiveGasPrice` unchanged

This ADR does **not** change `bcos-framework::protocol::effectiveGasPrice()`. ETH TE paths explicitly call `gas::resolveEffectiveGasPrice`. Non-ETH paths (`TransactionExecutorImpl`, `OpStackTxExecutor`, `TxValidator`) retain the legacy helper per spec §2.3; grep audit in `docs/audits/_work/eip1559-effectiveGasPrice-audit.md`.

---

## Consequences

- Matrix row **EIP-1559 effective gas + tip settlement (ETH TE)** marked `explicit` on ETH reference orchestration.
- EEST probe `eth-eest-1559-gasprice-probe.json` anchors `GASPRICE`/`BASEFEE` storage parity on `stExample/eip1559` (Cancun variant); smoke delta may be zero per spec §1.1 because adapter settlement was already correct — probe still gates regression.
- BCOS `executeViaHost` 1559 settlement remains out of scope; future work may reuse `Eip1559.h`.
- OpStack `resolveEffectiveGasPrice` dedup deferred to a follow-up PR.

---

## References

- geth `core/state_transition.go` — `buyGas`, `refundGas`, `EffectiveGasTip`
- `bcos-evm/eth/eip/Eip1559.h`, `EthTxExecutor.h`, `ExecuteViaEth.cpp`, `ExecuteViaEthAdapter.cpp`
- ADR-005, ADR-015
- Spec v1.2: `docs/superpowers/specs/2026-06-21-eth-eip1559-settlement-design.md`
- EEST probe: `manifests/eth-eest-1559-gasprice-probe.json`
