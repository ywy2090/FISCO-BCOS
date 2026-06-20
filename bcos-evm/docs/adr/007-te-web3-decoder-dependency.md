# ADR-007: TE Web3 Decoder Dependency on bcos-executor

**Status:** Accepted  
**Date:** 2026-06-20  
**Related:** Design §2.1, §6.2; Open Decision (Web3 decoder); `bcos-evm/docs/architecture-known-gaps.md`

---

## Context

TE baseline input builders (`EthTxInputBuilder`, `FiscoTxInputBuilder`, `OpStackTxInputBuilder`) decode Web3 typed transactions and access lists via `bcos-executor` (`Web3AccessListResolver`, typed-tx kind). This coupling sits outside the `executeMessage()` kernel boundary but affects tx-input propagation on the inheritance contract path.

Design §6.2 listed migration vs permanent dependency as open work.

---

## Decision

TE baseline builders **continue** to depend on `bcos-executor` Web3 decode for the inheritance-contract scope. Migration to a TE-local decoder is **deferred**.

This does not block kernel inheritance proofs on `executeViaHost` / `opStackExecuteViaHost` when tests supply decoded fields directly or via builders on the TE path.

---

## Consequences

- Matrix tracks tx-input **fields after decode**, not decoder module location.
- Future migration requires an ADR amendment and matrix note; not a silent refactor.
- Inheritance acceptance for tx-input EIPs remains **Partial** until decoder is TE-local or TE executor E2E fixtures cover full decode→orchestrator paths.
