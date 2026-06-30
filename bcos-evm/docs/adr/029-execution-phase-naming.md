# ADR-029: Execution Phase Naming Convention

**Status:** Accepted  
**Date:** 2026-06-29  
**Related:** ADR-019, ADR-023, ADR-027, ADR-030, `architecture-overview.md`

---

## Context

Single-tx execution spans bridge → orchestration pipeline → EVM kernel → call frames → nested host callbacks. Reusing verbs (`execute`, `run`, `message`) across layers made call stacks hard to read.

---

## Decision

### 1. Layer prefixes (mandatory for new code)

| Layer | ID | Prefix / name | When |
| --- | --- | --- | --- |
| TE shell | L0 | (outside `bcos-evm`) | receipt, executor |
| Chain bridge | L1 | `*Execute` | `fiscoExecute`, `ethReferenceExecute`, `opStackExecute` |
| OP lifecycle | L1½ | `lifecycle*` | before/after `runTxPipeline` (gas pool, buyGas, settle) |
| Tx orchestration | L2 | `pipeline*` | steps inside `runTxPipeline` |
| EVM kernel (tx-level) | L3 | `runEvmKernel*` | warm, 7702, top-level frame, finalize |
| Call frame | L4 | `runCallFrame` | one `evmc_message` frame (top or nested) |
| Host nested | L5 | `EthHost::call` | evmone callback into L4 |

`runTxPipeline` stays — it is the L2 deep module name (ADR-019).

### 2. `ChainPrecheckPolicy` virtual API (L2)

| Old | New |
| --- | --- |
| `setupMessage` | `pipelineSetupMessage` |
| `checkTransactionRules` | `pipelineCheckRules` |
| `checkGasAffordable` | `pipelineCheckGasAffordable` |
| `checkBalanceAndValue` | `pipelineCheckBalance` |
| `tuneExecutionInput` | `pipelineTuneKernelInput` |
| `runEvmExecution` | `pipelineInvokeEvmKernel` |

### 3. Kernel entry (L3)

| Symbol | Role |
| --- | --- |
| `executeMessage` | **Stable TE ABI** — forwards to L3 |
| `TxExecutionRunner::runEvmKernelTopLevel` | L3 implementation |

### 4. Frame entry (L4)

| Old | New |
| --- | --- |
| `runExecutionFrame` | `runCallFrame` |

### 5. OpStack-only (L1½)

| Old | New |
| --- | --- |
| `checkEntryRules` | `lifecycleCheckEntryRules` |

### 6. Logging

`TxPipeline` trace keys use step names aligned with §2 (`pipelineSetupMessage`, …, `pipelineInvokeEvmKernel`).

---

## Consequences

- Call stacks encode phase: `fiscoExecute → runTxPipeline → pipelineInvokeEvmKernel → runEvmKernelTopLevel → runCallFrame`.
- `executeMessage` name retained for external linkage; documented as L3 alias.
- ADR-029 / ADR-023 prose may still mention old names in historical context; code uses §2–5.
- **geth vocabulary:** see ADR-030 for `ApplyMessage` / `stateTransition.execute` equivalents (`preCheck`, `innerExecute`, `evm.Call`).
