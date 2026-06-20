# ETH Reference CANCUN+ Audit Inventory

**Source:** `bcos-evm/capability-matrix.md` ETH (reference) column  
**Scope:** `executeViaEth` path, `EVMC_CANCUN`–`EVMC_OSAKA` (`EthPolicy.h`)  
**Generated:** 2026-06-20 (Task 0)

Skipped rows: ETH `unsupported` and not CANCUN+ boundary (BCOS auth, BCOS value transfer, BCOS CREATE nonce, OPStack deposit/receipt, BCOS 21000 gas debit, EIP-7702 precheck on reference path).

| # | Capability | Layer | Matrix ETH status | Audit? |
|---|------------|-------|-------------------|--------|
| 1 | EIP-2929 runtime warm | kernel | inherited | YES (prereq) |
| 2 | EIP-2929 tx-entry destination warm | tx input | inherited | YES |
| 3 | EIP-2929 tx-entry coinbase warm | tx input | inherited | YES |
| 4 | builtin precompiles (0x01–0x11) | kernel | inherited | YES (prereq) |
| 5 | RevisionConfig `eip1153` | revision profile | inherited | YES |
| 6 | EIP-4844 revision profile | revision profile | inherited | YES |
| 7 | EIP-4844 blob orchestration | orchestration | unsupported | YES (📋 boundary) |
| 8 | RevisionConfig `eip5656` | revision profile | inherited | YES (evmone-delegated) |
| 9 | RevisionConfig `eip6780` | revision profile | inherited | YES |
| 10 | EIP-2537 precompiles (0x0b–0x11) | kernel | inherited | YES |
| 11 | EIP-7623 entry precheck | orchestration | explicit | YES |
| 12 | EIP-7623 settlement / floor gas | orchestration | explicit | YES |
| 13 | EIP-7702 authorization apply | kernel | inherited | YES |
| 14 | EIP-7702 tx field propagation | tx input | inherited | YES |
| 15 | EIP-7702 revision enable | revision profile | inherited | YES |
| 16 | EIP-7212 precompile (0x0100) | kernel | unsupported | YES (📋/gap) |
| 17 | RevisionConfig `eip7823` | revision profile | feature-gated (profile-only) | YES |
| 18 | chain precompile routing | host extension | inherited | YES (smoke) |

**Profile-only rows (ADR-004) — audit profile assignment, not TE consumer:**

| # | Capability | Layer | Matrix ETH status | Audit? |
|---|------------|-------|-------------------|--------|
| 19 | RevisionConfig `warm_access` | revision profile | feature-gated (profile-only) | YES (profile) |
| 20 | RevisionConfig `eip1559` | revision profile | feature-gated (profile-only) | YES (profile) |
| 21 | RevisionConfig `eip3651` | revision profile | feature-gated (profile-only) | YES (profile) |
| 22 | RevisionConfig `prague_post_execution` | revision profile | feature-gated (profile-only) | YES (profile) |

**Test refs (from matrix):**

- 2929: `Eip2929AccessHostTest`, `WarmTransactionEntryTest`
- precompiles: `stPrecompile_*`, `ExecuteViaHostImportedFixtureTest`
- profile: `RevisionConfigProfileTest`
- 2537: `Eip2537KernelTest`
- 7623: `ExecuteViaEth.cpp` orchestration
- 7702: `EthTxInputBuilderTest`, `Eip7702ApplyAuthorizationTest`
- end-to-end: `ExecuteViaEthFixtureTest`
