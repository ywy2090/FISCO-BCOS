# Inheritance contract — PR review checklist

Use with `bcos-evm/capability-matrix.md` and ADR-001–006.

## Scope

- [ ] Changes touch TE baseline path (`executeViaHost` / `opStackExecuteViaHost`) or kernel (`executeMessage` and below)?
- [ ] Legacy `bcos-executor` path not implied as inherited without ADR.

## Capability matrix (ADR-002, ADR-003)

- [ ] `bcos-evm/capability-matrix.md` updated in same PR when EIP/kernel/HostExtension/profile behavior changes.
- [ ] Each cell uses one of: `inherited`, `explicit`, `feature-gated`, `unsupported`, `deviation`.
- [ ] Multi-layer EIPs split into kernel / tx input / profile / orchestration rows (ADR-003).
- [ ] Kernel `inherited` footnotes **kernel-capable / not baseline-reachable** when profile or tx rows block TE path.
- [ ] No rollup-only summary rows.

## Paths (ADR-001)

- [ ] BCOS/OP inheritance claims cite TE baseline tests, not ETH reference alone.
- [ ] ETH reference column used only for wiring audit.

## RevisionConfig (ADR-004)

- [ ] New fields classified consumed vs profile-only.
- [ ] Profile builders assign explicitly or matrix documents opt-out.
- [ ] `RevisionConfigProfileTest` updated when profile assignments change.

## Orchestration domains (ADR-005)

- [ ] Auth, value transfer, nonce, receipt, blob, deposit classified and matrix row updated if behavior changes.
- [ ] No BCOS/OP includes added under `bcos-evm/eth`.

## Tests

- [ ] Kernel contract test for kernel rows.
- [ ] Baseline-path or propagation test for baseline-reachable `inherited` non-kernel rows.
- [ ] Positive test for each new `deviation` row.

## BCOS EIP-7702 (ADR-006)

- [ ] Web3 kind `0x04` + `eip7702` profile + `ExecuteViaHostInput` fields when enabling 7702 on BCOS.
