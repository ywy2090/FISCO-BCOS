# ETH Kernel Capability Matrix

**Status:** Normative (Phase 1 audit complete — 2026-06-20; Phase 2–3 partial on `feat-evm-refactor`)  
**ADRs:** ADR-001–019 under `bcos-evm/docs/adr/`

Row granularity rules: see ADR-003 (one row = one independently testable sub-capability on one layer; no rollup rows).

This file is the **single authoritative capability matrix** for the `bcos-evm` inheritance contract. Update this file in the same PR as any change to `RevisionConfig.h`, `executeMessage.*`, `HostExtension.h`, or EIP behavior on a TE baseline path.

---

## Column semantics

| Column | Execution path | Role |
| --- | --- | --- |
| ETH (reference) | `executeViaEth` → **`runOrchestration`** → `executeMessage` | Wiring audit and kernel-input contract tests; **not** BCOS/OPStack production inheritance proof |
| BCOS (TE baseline) | `TransactionExecutorImpl` → `executeViaHost` → **`runOrchestration`** → `executeMessage` | FISCO production inheritance contract |
| OPStack (TE baseline) | `OpStackTransactionExecutorImpl` → `opStackExecuteViaHost` → **`runOrchestration`** → `executeMessage` | OPStack production inheritance contract |

**Orchestration pipeline (ADR-019):** All three `executeVia*` entry points converge on sync `runOrchestration` in `eth/orchestration/`. Chain-specific precheck, intrinsic policy, fee routing (`buyGas`/`refundGas`), deposit state machine, and final `stateDiff`/`logs` mapping remain in thin wrappers per ADR-005.

Legacy `bcos-executor` / DAG / `HostContext` is **out of scope** unless a future ADR brings it in.

---

## Status tokens

Each cell uses exactly one token. Non-`inherited` cells must include a short reason in parentheses.

| Token | Meaning |
| --- | --- |
| `inherited` | Kernel-capable (kernel rows) or baseline-reachable (other rows) on that path without chain-specific reimplementation beyond documented hooks |
| `explicit` | Chain-specific orchestration (precheck, intrinsic gas, settlement) implemented in the extension orchestrator |
| `feature-gated` | Blocked by unset `RevisionConfig` field and/or FISCO `Features::Flag` until profile work closes the gap |
| `unsupported` | Intentionally not wired; default-off or not applicable on this path |
| `deviation` | Intentionally different from Ethereum reference; requires a positive deviation test |

**Footnotes:** Cells may note **implicit-default** behavior (e.g. `TransactionProperties::warmCoinbase{true}`) when the orchestrator does not assign the field but struct defaults make it effective. Cite the default source.

**Kernel vs end-to-end:** A kernel row marked `inherited` means **kernel-capable** only. End-to-end EIP support requires matching tx input, revision profile, and orchestration rows.

**Profile-only fields:** When ADR-004 marks a field as profile-only (no TE consumer), use `feature-gated (profile-only; …)` even if a policy builder assigns the flag.

---

## Matrix

| Capability | Layer | ETH (reference) | BCOS (TE baseline) | OPStack (TE baseline) | Test ref |
| --- | --- | --- | --- | --- | --- |
| EIP-2929 runtime warm | kernel | inherited | inherited | inherited | `Eip2929AccessHostTest`, `WarmTransactionEntryTest` |
| EIP-2929 tx-entry destination warm | tx input | inherited (explicit `setWarmDestinationFromKind` in `ExecuteViaEth.cpp`) | inherited (explicit in `ExecuteViaHost.cpp:213`) | inherited (explicit `applyDefaultTxProps` in TE) | `WarmTransactionEntryTest`, `OpStackTxPropsTest` |
| EIP-2929 tx-entry coinbase warm | tx input | inherited (implicit-default `warmCoinbase{true}` when `rev>=SHANGHAI`) | inherited (same default) | inherited (same default) | `WarmTransactionEntryTest` |
| EIP-7702 authorization apply | kernel | inherited (`EthPolicy` sets `eip7702` at PRAGUE+; `applyAuthorizations` reachable) | inherited (kernel-capable; baseline-reachable when profile + tx rows satisfied) | inherited (baseline-reachable on Isthmus profile) | `Eip7702ApplyAuthorizationEthTest`, `Eip7702ApplyAuthorizationTest`, EEST `eth-eest-7702-core-smoke.json` |
| EIP-7702 tx field propagation | tx input | inherited (`EthTxInputBuilder`) | feature-gated (fields via `FiscoTxInputBuilder`; requires `feature_evm_prague` + Web3 `0x04`, ADR-006) | inherited (`OpStackTxInputBuilder`) | `EthTxInputBuilderTest`, `FiscoTxInputBuilderTest`, `Bcos7702ExecuteViaHostPropagationTest`, `OpStack7702ExecuteViaHostPropagationTest`, `OpStackTxInputBuilderTest`, EEST `eth-eest-tx-smoke.json` |
| EIP-7702 revision enable | revision profile | inherited (`EthPolicy` at PRAGUE+) | feature-gated (`FiscoPolicy` when `feature_evm_prague` + PRAGUE, ADR-006) | inherited (`makeIsthmusRevisionConfig`) | `RevisionConfigProfileTest` |
| EIP-1559 effective gas + tip settlement (ETH TE) | orchestration | explicit (`EthTxExecutor` + `Eip1559.h`; ADR-016) | unsupported | unsupported (OpStack uses own resolver; dedup deferred) | `EthEip1559GasTest`, `EthExecuteViaEth1559GasPriceTest`, EEST `eth-eest-1559-gasprice-probe.json` |
| EIP-7702 precheck + intrinsic gas | orchestration | explicit (`ExecuteViaEth.cpp`: auth intrinsic debit + snapshot; ADR-015) | unsupported | explicit (`OpStackPreCheck` + auth intrinsic) | `Eip7702PreCheckTest`, `OpStack7702ExecuteViaHostPropagationTest`, EEST `self_sponsored_set_code` smoke |
| EIP-7623 entry precheck | orchestration | explicit (`ExecuteViaEth.cpp` when `eip7623`) | feature-gated (`web3Tx` + `eip7623` + `feature_evm_prague`) | explicit (OPStack precheck path) | `Bcos7623PrecheckTest` |
| EIP-7623 settlement / floor gas | orchestration | explicit (`finalizeEthereumGasUsed`; included-tx vmerr via `settleIncludedTopLevelTransactionGas`, ADR-015) | feature-gated (same gates as entry) | deviation (`OpStackFloorGas` + `postExecuteGasSettlement`) | `OpStackFloorGasTest`, `OpStackSettlementTest`, EEST `self_sponsored_set_code` smoke |
| BCOS fixed 21000 gas debit | orchestration | unsupported (no equivalent on reference path) | deviation (`BALANCE_TRANSFER_GAS` in `ExecuteViaHost.cpp:259-263`) | unsupported (OP path does not debit) | `Bcos21000GasDeviationTest` |
| EIP-2537 precompiles (0x0b–0x11) | kernel | inherited (via `isActivePrecompile` + `cfg.eip2537`; `BlsGas.h` MSM discount table) | feature-gated (kernel via `cfg.eip2537`; FISCO manager also needs `feature_evm_prague`) | inherited (via `cfg.eip2537`) | `Eip2537KernelTest`, `Bcos2537MsmGasTest` |
| EIP-7212 precompile (0x0100) | kernel | inherited (TE dispatch via `EthPrecompiles`; gated by `cfg.eip7212`) | feature-gated (kernel via `cfg.eip7212` at OSAKA+; FISCO manager needs `feature_evm_osaka`) | unsupported (Isthmus profile is PRAGUE; 0x0100 inactive) | `Eip7212KernelTest`, `Bcos7212ExecuteViaHostTest` |
| EIP-4844 revision profile | revision profile | inherited (`EthPolicy` at CANCUN+) | inherited (`FiscoPolicy` at CANCUN+) | inherited (`makeIsthmusRevisionConfig`) | `RevisionConfigProfileTest` |
| EIP-4844 blob orchestration | orchestration | unsupported (no blob precheck on reference path) | unsupported (no blob tx on BCOS TE) | explicit (`OpStackPreCheck` blob fields) | `BlobGasBalanceTest`, `OpStackPreCheck4844Test`, `BlockGasPoolTest`, `TestOpStackTransactionExecutorFixture::second_transaction_rejected_when_block_gas_exhausted` |
| builtin precompiles (0x01–0x11) | kernel | inherited (`PrecompileActive.h`: 0x01–0x0a always; 0x0b–0x11 when `cfg.eip2537`) | inherited (same gate via shared kernel) | inherited (same gate via shared kernel) | `stPrecompile_*` fixtures, `ExecuteViaHostImportedFixtureTest`, `EipPrecompileRevisionGateTest`, `BcosPrecompileRevisionGateTest`, `PrecompileRouterEquivalenceTest` |
| chain precompile routing | host extension | inherited (`tryChainPrecompile` default nullopt) | deviation (FISCO precedence; empty-code CALL semantics differ) | deviation (`OpHostExtension` L1Block predeploy: full `IL1Block` getter/setter Isthmus surface; no GPO `0x4200…000F`, `setFeature`, `proxyAdmin*`) | `FiscoHostExtensionTest`, `L1BlockPredeployTest`, `L1BlockGetterTest`, `PrecompileRouterPrecedenceTest` |
| FISCO chain precompile dispatch via Port | orchestration | unsupported | explicit (`ExecuteViaHost` injects `ChainPrecompilePort` / `AuthPort` and dispatches in TE adapters) | unsupported | `CompatExecuteViaHost*` |
| OPStack deposit tx | orchestration | unsupported | unsupported | explicit (OPStack-only orchestration) | `DepositTxPreCheckTest`, `DepositMintTest`, `DepositCreateNonceTest` |
| L1 attributes system deposit | orchestration | unsupported | unsupported | explicit (Isthmus deposit → `setL1BlockValuesIsthmus` on L1Block predeploy) | `L1AttributesDepositTest`, `L1AttributesDepositFailureTest` |
| OPStack operator fee (Isthmus) | orchestration | unsupported | unsupported | explicit (`operatorCostIsthmus` + `refundIsthmusOperatorCost`; `OpStackForkSchedule` + `wireOperatorCostFuncWithState`) | `RefundIsthmusTest`, `OpStackExecuteViaHostSmokeTest` |
| Isthmus executor integration | orchestration | unsupported | unsupported | explicit (`isIsthmusOrchestrationProfile` → `OpStackTransactionExecutorImpl` / `opStackExecuteViaHost`) | `OpStackTxPropsTest`, `OpStackExecuteViaHostSmokeTest`, `OpStackBlockHeaderExtensionTest`, TE fixture |
| Rollup L1 cost tx bytes | tx input | unsupported | unsupported | explicit (`buildRollupCostData` signed RLP) | `OpStackTxInputBuilderTest`, FIX-05 |
| Rollup L1 cost fork selection | orchestration | unsupported | unsupported | explicit (`wireL1CostFuncWithState` / `selectL1CostFunc`) | `OpStackForkScheduleTest`, `OpStackFeeTest` FIX-04 |
| L1 pre-Fjord unsupported | orchestration | unsupported | unsupported | deviation (throw) | `OpStackExecuteViaHostSmokeTest` pre_fjord |
| OPStack operator fee fork gate | orchestration | unsupported | unsupported | explicit (`wireOperatorCostFuncWithState` / `selectOperatorCostFunc`) | `OpStackFeeTest`, `RefundIsthmusTest` |
| RevisionConfig `warm_access` | revision profile | feature-gated (profile-only; runtime uses `rev>=BERLIN`, ADR-004) | feature-gated (via `revisionConfigFromRevision` + `applyFiscoFeatureGates`, ADR-018) | feature-gated (via `revisionConfigFromRevision`, ADR-018) | `RevisionConfigProfileTest` |
| RevisionConfig `eip1153` | revision profile | inherited (via `revisionConfigFromRevision` at CANCUN+, ADR-018) | inherited (via `revisionConfigFromRevision` at CANCUN+, ADR-018) | inherited (via `makeIsthmusRevisionConfig` = `derive(PRAGUE)`, ADR-018) | `RevisionConfigProfileTest` |
| RevisionConfig `eip5656` | revision profile | inherited (via `revisionConfigFromRevision`, ADR-018) | inherited (via `revisionConfigFromRevision`, ADR-018) | inherited (via `derive(PRAGUE)`, ADR-018) | `RevisionConfigProfileTest` |
| RevisionConfig `eip6780` | revision profile | inherited (via `revisionConfigFromRevision` at CANCUN+; kernel reads `cfg.eip6780` in `EthHost::selfdestruct`, ADR-018) | inherited (via `revisionConfigFromRevision` at CANCUN+) | inherited (via `derive(PRAGUE)`, ADR-018) | `RevisionConfigProfileTest` |
| EIP-6780 SELFDESTRUCT (kernel) | kernel | inherited (`EthHost::selfdestruct` reads `cfg.eip6780` + same-tx CREATE tracking) | inherited (via shared `executeMessage` kernel) | inherited (via shared kernel) | `ExecuteViaEthFixtureTest` (`stSelfDestruct_basic`), `Bcos6780SelfdestructTest`, EEST `eth-eest-6780-smoke.json` |
| RevisionConfig `eip1559` | revision profile | feature-gated (profile-only; assigned by `revisionConfigFromRevision` at LONDON+, ADR-004/018) | feature-gated (profile-only; via `derive` at CANCUN floor+, ADR-004/018) | feature-gated (profile-only; via `derive(PRAGUE)`, ADR-004/018) | `RevisionConfigProfileTest` |
| RevisionConfig `eip3651` | revision profile | inherited (assigned by `derive` at SHANGHAI+; kernel reads `cfg.eip3651` in `warmTransactionEntry`, ADR-004/018) | inherited (via `derive` at CANCUN floor+; coinbase warm reads `cfg.eip3651`) | inherited (via `derive(PRAGUE)`; coinbase warm reads `cfg.eip3651`) | `RevisionConfigProfileTest`, `WarmTransactionEntryTest` |
| RevisionConfig `prague_post_execution` | revision profile | unsupported (deprecated/reserved; struct default false, no TE consumer, ADR-004) | unsupported (deprecated/reserved) | unsupported (deprecated/reserved; no overlay on Isthmus helper) | `RevisionConfigProfileTest` |
| RevisionConfig `eip7823` | revision profile | inherited (via `EthPolicy` at OSAKA+; TE consumer in `EthPrecompiles` modexp dispatch) | feature-gated (`feature_evm_osaka`; TE consumer active when flag ON) | feature-gated (not set on Isthmus helper) | `RevisionConfigProfileTest`, `Eip7823ModexpRejectTest`, `Bcos7823ModexpRejectTest` |
| BCOS auth check | orchestration | unsupported | explicit (`ExecuteViaHost` + `authChecker` callback) | unsupported | `BcosAuthOrchestratorHookTest` (hook-only; not AuthCheck integration) |
| BCOS value transfer | orchestration | unsupported | explicit (`maybeTransferValue` when `enable_balance_transfer`) | unsupported | — |
| BCOS CREATE nonce persist | orchestration + host extension | unsupported | explicit (`persistContractCreateNonce` + `bumpContractCreateNonce`) | unsupported | — |
| OPStack receipt metadata | orchestration | unsupported | unsupported | explicit (`OpStackReceiptMeta` in executor) | `OpStackSettlementTest` |

---

## Phase 1 audit notes

RevisionConfig consumption rules: ADR-004. Orchestration domains: ADR-005. Shared orchestration pipeline: ADR-019 (`runOrchestration`). ETH reference 7702 gas + included-tx vmerr: ADR-015. ETH TE EIP-1559 settlement: ADR-016. Add new Isthmus-only EIPs as separate rows when implemented (ADR-003).

### Wave 2 — Isthmus profile footnote (FIX-12, updated ADR-018)

`makeIsthmusRevisionConfig()` now returns `revisionConfigFromRevision(EVMC_PRAGUE)` — a **dense** canonical Prague profile (same gate set as Eth/Fisco at Prague). `prague_post_execution` stays `false` via struct default (no overlay). Runtime behavior on Isthmus is unchanged: profile-only densification; kernel rows (`EIP-6780 SELFDESTRUCT`, builtin precompiles) read `cfg` bools set by `derive`. Smoke: `OpStack67802537KernelSmokeTest`, `RevisionConfigProfileTest::isthmus_helper_dense_profile_all_fields`.

## Change rules

1. The engineer changing EIP behavior **owns** the matrix update in the same PR.
2. Reviewers reject EIP PRs that omit matrix updates when capability surfaces change.
3. Rows marked baseline-reachable `inherited` require a TE baseline-path test (not ETH reference alone).
4. Rows marked `deviation` require a positive deviation test reference in the **Test ref** column.
5. Valid status tokens only: `inherited`, `explicit`, `feature-gated`, `unsupported`, `deviation`.
6. `feature-gated` / profile-only rows do **not** require baseline-path tests; only baseline-reachable `inherited` and `deviation` rows do.

---

## Enforcement

| Trigger | Required response |
| --- | --- |
| Change `bcos-evm/eth/RevisionConfig.h` | Update this matrix + extend `RevisionConfigProfileTest` (required by CI when `REVISION_CONFIG_BOOL_FIELDS` changes) |
| Change `executeMessage.*` / EIP implementation | Update matrix row + kernel contract test |
| Change `HostExtension.h` | Update matrix if hook semantics change + extension tests |
| Change TE orchestrators / `*Policy*` / `*TxInputBuilder*` | Update matrix when capability behavior changes |
| Mark capability baseline-reachable | Add propagation or baseline-path CTest; name in **Test ref** |

CI: `.github/workflows/capability-gate.yml` + `bcos-evm/tools/ci/check-capability-matrix.sh`.
