# ETH Kernel Capability Matrix

**Status:** Normative (Phase 1 audit complete — 2026-06-20; post ADR-032/033 naming on `feat/adr-030-geth-naming`)  
**ADRs:** ADR-001–019, ADR-030–033 under `bcos-evm/docs/adr/`

Row granularity rules: see ADR-003 (one row = one independently testable sub-capability on one layer; no rollup rows).

This file is the **single authoritative capability matrix** for the `bcos-evm` inheritance contract. Update this file in the same PR as any change to `RevisionConfig.h`, `innerExecute` / `StateTransitionExecute`, `EvmHostHooks`, or EIP behavior on a TE baseline path.

---

## Column semantics

| Column | Execution path | Role |
| --- | --- | --- |
| ETH (reference) | `applyEthMessage` → **`stateTransitionExecute`** → `innerExecute` | Wiring audit and kernel-input contract tests; **not** BCOS/OPStack production inheritance proof |
| BCOS (TE baseline) | `TransactionExecutorImpl` → `applyFiscoMessage` → **`stateTransitionExecute`** → `innerExecute` | FISCO production inheritance contract |
| OPStack (TE baseline) | `OpStackTransactionExecutorImpl` → `applyOpStackMessage` → **`stateTransitionExecute`** → `innerExecute` | OPStack production inheritance contract |

**Orchestration pipeline (ADR-019):** All three chain L1 adapters converge on sync `stateTransitionExecute` in `eth/kernel/state-transition/StateTransitionExecute.*`. Chain-specific precheck, intrinsic policy, fee routing (`buyGas`/`refundGas`), deposit state machine, and final `stateDiff`/`logs` mapping remain in thin wrappers per ADR-005.

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
| EIP-2929 runtime warm | kernel | inherited | deviation (`eip2929` masked by `feature_evm_eip2929`; Scheme A ADR-004) | inherited | `Eip2929GateHost`, `Eip2929OpcodeGas`, `WarmTransactionEntry` |
| EIP-2929 tx-entry destination warm | tx input | inherited (explicit `setWarmDestinationFromKind` in `StateTransitionContext`) | inherited (explicit in `ApplyFiscoMessage` / `FiscoPrecheckPolicy`) | inherited (explicit `applyDefaultTxProps` in TE) | `WarmTransactionEntry`, `OpStackTxProps` |
| EIP-2929 tx-entry coinbase warm | tx input | inherited (implicit-default `warmCoinbase{true}` when `rev>=SHANGHAI`) | inherited (same default) | inherited (same default) | `WarmTransactionEntry` |
| EIP-7702 authorization apply | kernel | inherited (`EthChainPolicy` sets `eip7702` at PRAGUE+; `applyAuthorizations` reachable) | inherited (kernel-capable; baseline-reachable when profile + tx rows satisfied) | inherited (baseline-reachable on Isthmus profile) | `Eip7702ApplyAuthorizationEth`, `Eip7702ApplyAuthorization`, EEST `eth-eest-7702-core-smoke.json` |
| EIP-7702 tx field propagation | tx input | inherited (`EthTxInputBuilder`) | feature-gated (fields via `FiscoTxInputBuilder`; requires `feature_evm_prague` + Web3 `0x04`, ADR-006) | inherited (`OpStackTxInputBuilder`) | `EthTxInputBuilderTest`, `FiscoTxInputBuilderTest`, `Bcos7702FiscoExecutePropagation`, `OpStack7702ExecutePropagation`, `OpStackTxInputBuilderTest`, EEST `eth-eest-tx-smoke.json` |
| EIP-7702 revision enable | revision profile | inherited (`EthChainPolicy` at PRAGUE+) | feature-gated (`FiscoPolicy` when `feature_evm_prague` + PRAGUE, ADR-006) | inherited (`OpStackIsthmusRevision::makeIsthmusRevisionConfig`) | `RevisionConfigProfile` |
| EIP-1559 effective gas + tip settlement (ETH TE) | orchestration | explicit (`EthTxFeeSettlement` + `Eip1559.h`; ADR-016) | unsupported | unsupported (OpStack uses own resolver; dedup deferred) | `EthEip1559Gas`, `EthReferenceExecute1559GasPrice`, EEST `eth-eest-1559-gasprice-probe.json` |
| EIP-7702 precheck + intrinsic gas | orchestration | explicit (`EthPrecheckPolicy` + auth intrinsic debit) | unsupported | explicit (`OpStackPrecheckPolicy` + auth intrinsic) | `Eip7702PreCheck`, `OpStack7702ExecutePropagation`, EEST `self_sponsored_set_code` smoke |
| EIP-7623 entry precheck | orchestration | explicit (`EthPrecheckPolicy` when `eip7623`) | feature-gated (`web3Tx` + `eip7623` + `feature_evm_prague`) | explicit (OPStack precheck path) | `Bcos7623Precheck`, `Eip7623Precheck` |
| EIP-7623 settlement / floor gas | orchestration | explicit (`onFinalizeGasUsed` / `IncludedTxVmerrNormalize`; ADR-015) | feature-gated (same gates as entry) | deviation (`OpStackFloorGas` + `OpStackPostSettlementCharacterization`) | `OpStackFloorGas`, `OpStackPostSettlementCharacterization`, EEST `self_sponsored_set_code` smoke |
| BCOS fixed 21000 gas debit | orchestration | unsupported (no equivalent on reference path) | deviation (`BALANCE_TRANSFER_GAS` in `FiscoPrecheckPolicy.cpp`) | unsupported (OP path does not debit) | `Bcos21000GasDeviation` |
| EIP-2537 precompiles (0x0b–0x11) | kernel | inherited (via `isActivePrecompile` + `cfg.eip2537`; `BlsGas.h` MSM discount table) | feature-gated (kernel via `cfg.eip2537`; FISCO manager also needs `feature_evm_prague`) | inherited (via `cfg.eip2537`) | `Eip2537Kernel`, `Bcos2537MsmGas` |
| EIP-7212 precompile (0x0100) | kernel | inherited (TE dispatch via `EthPrecompiles`; gated by `cfg.eip7212`) | feature-gated (kernel via `cfg.eip7212` at OSAKA+; FISCO manager needs `feature_evm_osaka`) | unsupported (Isthmus profile is PRAGUE; 0x0100 inactive) | `Eip7212Kernel`, `Bcos7212FiscoExecute` |
| EIP-4844 revision profile | revision profile | inherited (`EthChainPolicy` at CANCUN+) | inherited (`FiscoPolicy` at CANCUN+) | inherited (`makeIsthmusRevisionConfig`) | `RevisionConfigProfile` |
| EIP-4844 blob orchestration | orchestration | unsupported (no blob precheck on reference path) | unsupported (no blob tx on BCOS TE) | explicit (`OpStackPrecheckPolicy` blob fields) | `BlobGasBalance`, `OpStackPreCheck4844`, `BlockGasPool`, `OpStackTransactionExecutorFixture` |
| builtin precompiles (0x01–0x11) | kernel | inherited (`PrecompileActive.h`: 0x01–0x0a always; 0x0b–0x11 when `cfg.eip2537`) | inherited (same gate via shared kernel) | inherited (same gate via shared kernel) | `stPrecompile_*` fixtures, `FiscoExecuteImportedFixture`, `EipPrecompileRevisionGate`, `BcosPrecompileRevisionGate`, `PrecompileRouterEquivalence` |
| chain precompile routing | host extension | inherited (`tryChainPrecompile` default nullopt) | deviation (FISCO precedence; empty-code CALL semantics differ) | deviation (`OpStackChainCallTargetAdapter` L1Block predeploy: full `IL1Block` getter/setter Isthmus surface; no GPO `0x4200…000F`, `setFeature`, `proxyAdmin*`) | `FiscoHostExtension`, `L1BlockPredeploy`, `L1BlockGetter`, `PrecompileRouterEquivalence` |
| FISCO chain precompile dispatch via Port | orchestration | unsupported | explicit (`applyFiscoMessage` injects `ChainExtendedPrecompileDispatch` / `AuthPort` and dispatches in TE adapters) | unsupported | `ExecuteViaHostCompat` |
| OPStack deposit tx | orchestration | unsupported | unsupported | explicit (OPStack-only orchestration) | `DepositTxPreCheck`, `DepositMint`, `DepositCreateNonce` |
| L1 attributes system deposit | orchestration | unsupported | unsupported | explicit (Isthmus deposit → `setL1BlockValuesIsthmus` on L1Block predeploy) | `L1AttributesDeposit`, `L1AttributesDepositFailure` |
| OPStack operator fee (Isthmus) | orchestration | unsupported | unsupported | explicit (`operatorCostIsthmus` + `refundIsthmusOperatorCost`; `OpStackForkSchedule` + `wireOperatorCostFuncWithState`) | `OpStackFee`, `OpStackExecuteSmoke` |
| Isthmus executor integration | orchestration | unsupported | unsupported | explicit (`isIsthmusOrchestrationProfile` → `OpStackTransactionExecutorImpl` / `applyOpStackMessage`) | `OpStackTxProps`, `OpStackExecuteSmoke`, `OpStackBlockHeaderExtension`, `OpStackTransactionExecutorFixture` |
| Rollup L1 cost tx bytes | tx input | unsupported | unsupported | explicit (`buildRollupCostData` signed RLP) | `OpStackTxInputBuilderTest`, FIX-05 |
| Rollup L1 cost fork selection | orchestration | unsupported | unsupported | explicit (`wireL1CostFuncWithState` / `selectL1CostFunc`) | `OpStackForkSchedule`, `OpStackFee` FIX-04 |
| L1 pre-Fjord unsupported | orchestration | unsupported | unsupported | deviation (throw) | `OpStackExecuteSmoke` pre_fjord |
| OPStack operator fee fork gate | orchestration | unsupported | unsupported | explicit (`wireOperatorCostFuncWithState` / `selectOperatorCostFunc`) | `OpStackFee` |
| RevisionConfig `eip2929` | revision profile | inherited (TE consumer via `Eip2929Gate.h`; ADR-004 Scheme A) | feature-gated (`revisionConfigFromRevision` + FISCO `feature_evm_eip2929` mask; OFF = deviation) | inherited (via `revisionConfigFromRevision`, ADR-018) | `RevisionConfigProfile`, `Eip2929GateHost`, `Eip2929OpcodeGas` |
| RevisionConfig `eip1153` | revision profile | inherited (via `revisionConfigFromRevision` at CANCUN+, ADR-018) | inherited (via `revisionConfigFromRevision` at CANCUN+, ADR-018) | inherited (via `makeIsthmusRevisionConfig` = `derive(PRAGUE)`, ADR-018) | `RevisionConfigProfile` |
| RevisionConfig `eip5656` | revision profile | inherited (via `revisionConfigFromRevision`, ADR-018) | inherited (via `revisionConfigFromRevision`, ADR-018) | inherited (via `derive(PRAGUE)`, ADR-018) | `RevisionConfigProfile` |
| RevisionConfig `eip6780` | revision profile | inherited (via `revisionConfigFromRevision` at CANCUN+; kernel reads `cfg.eip6780` in `EthHost::selfdestruct`, ADR-018) | inherited (via `revisionConfigFromRevision` at CANCUN+) | inherited (via `derive(PRAGUE)`, ADR-018) | `RevisionConfigProfile` |
| EIP-6780 SELFDESTRUCT (kernel) | kernel | inherited (`EthHost::selfdestruct` reads `cfg.eip6780` + same-tx CREATE tracking) | inherited (via shared `innerExecute` kernel) | inherited (via shared kernel) | `EthReferenceExecuteFixture`, `Bcos6780Selfdestruct`, EEST `eth-eest-6780-smoke.json` |
| RevisionConfig `eip1559` | revision profile | inherited (TE consumer via `Eip1559Gate.h`; typed-tx gate, fee-cap precheck, gas refund) | inherited (via `revisionConfigFromRevision` at CANCUN floor+, ADR-004/018) | inherited (via `derive(PRAGUE)`; `Eip1559Gate.h`) | `RevisionConfigProfile`, `Eip1559Gate`, `EthEip1559Gas` |
| RevisionConfig `eip3651` | revision profile | inherited (assigned by `derive` at SHANGHAI+; kernel reads `cfg.eip3651` in `warmTransactionEntry`, ADR-004/018) | inherited (via `derive` at CANCUN floor+; coinbase warm reads `cfg.eip3651`) | inherited (via `derive(PRAGUE)`; coinbase warm reads `cfg.eip3651`) | `RevisionConfigProfile`, `WarmTransactionEntry` |
| RevisionConfig `eip7823` | revision profile | inherited (via `EthChainPolicy` at OSAKA+; TE consumer in `EthPrecompiles` modexp dispatch) | feature-gated (`feature_evm_osaka`; TE consumer active when flag ON) | feature-gated (not set on Isthmus helper) | `RevisionConfigProfile`, `Eip7823ModexpReject`, `Bcos7823ModexpReject` |
| BCOS auth check | orchestration | unsupported | explicit (`applyFiscoMessage` + `authChecker` callback) | unsupported | `BcosAuthOrchestratorHook` (hook-only; not AuthCheck integration) |
| BCOS value transfer | orchestration | unsupported | explicit (`maybeTransferValue` when `enable_balance_transfer`) | unsupported | — |
| BCOS CREATE nonce persist | orchestration + host extension | unsupported | explicit (`persistContractCreateNonce` + `bumpContractCreateNonce`) | unsupported | — |
| OPStack receipt metadata | orchestration | unsupported | unsupported | explicit (`OpStackReceiptMeta` in executor) | `OpStackSettlement` |

---

## Phase 1 audit notes

RevisionConfig consumption rules: ADR-004. Orchestration domains: ADR-005. Shared orchestration pipeline: ADR-019 (`stateTransitionExecute`). ETH reference 7702 gas + included-tx vmerr: ADR-015. ETH TE EIP-1559 settlement: ADR-016. Add new Isthmus-only EIPs as separate rows when implemented (ADR-003).

### Wave 2 — Isthmus profile footnote (FIX-12, updated ADR-018)

`makeIsthmusRevisionConfig()` in `opstack/OpStackIsthmusRevision.h` returns `revisionConfigFromRevision(EVMC_PRAGUE)` — a **dense** canonical Prague profile (same gate set as Eth/Fisco at Prague). Runtime behavior on Isthmus is unchanged: profile-only densification; kernel rows (`EIP-6780 SELFDESTRUCT`, builtin precompiles) read `cfg` bools set by `derive`. Smoke: `OpStack67802537KernelSmoke`, `RevisionConfigProfile::isthmus_helper_dense_profile_all_fields`.

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
| Change `innerExecute` / `StateTransitionExecute` / EIP implementation | Update matrix row + kernel contract test |
| Change `EvmHostHooks` | Update matrix if hook semantics change + extension tests |
| Change TE orchestrators / `*Policy*` / `*TxInputBuilder*` | Update matrix when capability behavior changes |
| Mark capability baseline-reachable | Add propagation or baseline-path CTest; name in **Test ref** |

CI: `.github/workflows/capability-gate.yml` + `bcos-evm/tools/ci/check-capability-matrix.sh`.
