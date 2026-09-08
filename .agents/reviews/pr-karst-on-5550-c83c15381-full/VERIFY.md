# Verification of Round-4 findings at c83c15381

Runtime + static re-check. Pin test (uncommitted):
`engine/test/unittests/engine/OpEngineKarstProfileTest.cpp`
`HashlessActivationRejectFlattensToInternalError` — **PASS** (documents current FCU flatten).

## F17 HIGH — CONFIRMED (FCU-only; newPayload is INVALID)

### Live wiring
`Initializer.cpp:555-567` constructs `OpScheduler` as `opDelegate` and passes it to `EngineServiceInitializer::buildOp`.

### Chain (FCU)

1. RPC `engine_forkchoiceUpdatedV3` → `EngineEndpoint::handleForkchoiceUpdated` (`EngineEndpoint.cpp:169-170`).
2. `updateForkchoice` → `buildOpPayload` (`OpEngineService.inl:278-279`).
3. Seal unless `noTxPool == true`:
   `if (!payloadAttributes.noTxPool.value_or(false)) m_memPool.seal(...)` (`inl:340-345`).
   Omitted `noTxPool` therefore seals. `StubMemPool::seal` copies `pool` (`OpEngineKarstTestHarness.h:122-128`).
4. Sealed envelopes appended after forced (`inl:494-505`). No `isNoUserTxActivationBlock` consult.
5. `m_delegate->executeBlock` (`inl:537`). Production delegate is `OpScheduler`.
6. `OpScheduler::execute` → `preBlockOpSteps(..., m_schedule.get(), parentTsSec)` (`OpScheduler.h:1044-1045`).
7. Q5 (`OpBlockExecute.h:381-386`):
   `isNoUserTxActivationBlock && hasNonDepositEnvelope` →
   `throw OpConsensusError("…fork activation block")` — one-arg ctor, `txHash` empty (`OpCommon.h:45`).
8. `attachOpRejectInfo` only tags `OpCulpritTxHash` when `txHash.has_value()` (`OpScheduler.h:1335-1338`).
9. FCU retry (`inl:557-570`): `if (culprit.has_value() && …) continue;` else
   `throw OpExecutionInternalError{"OP payload build execution failed: …"}`.
10. Endpoint (`EngineEndpoint.cpp:194-196`) → `rethrowAsEngineInternalError` →
    `JsonRpcException(InternalError, …)` and `InternalError = -32603` (`bcos-rpc/jsonrpc/Common.h:70`).

### Contrast: newPayload does NOT flatten
`mapDelegateError` (`OpEngineService.h:203-210`): `OpConsensusRejected` → `PayloadStatus::Invalid`.
Q5 on **newPayload** is consensus INVALID. F17 is **FCU builder liveness**, not accept-too-loose execute.

### Runtime
```
./build/engine/test/test-bcos-engine \
  --run_test=OpEngineKarstProfileSuite/HashlessActivationRejectFlattensToInternalError
# PASS: executeCalls==1, no pool evict, throws OpExecutionInternalError

./build/opstack-executor/tests/opstack-executor-block-tests \
  --run_test=OpKarstActivationSuite
# 8/8 PASS including KarstActivationBlockRejectsUserTx
```

The pin stub returns the Q5 `what()` **without** `OpCulpritTxHash` — the same shape live `attachOpRejectInfo` produces. It does not run `OpScheduler`; step 7 is independently pinned by `OpKarstActivationSuite`.

### Reachability
| Inputs | Result |
|---|---|
| `noTxPool=false` or omitted + mempool user tx + attrs ts in Jovian/Karst activation window | FCU `-32603`, no `payloadId` |
| `attrs.transactions` already has a non-deposit (even `noTxPool=true`) + real `OpScheduler` | same flatten (`"no deposit"` / Q5, both hash-less) |
| Honest op-node: deposits-only attrs + `noTxPool=true` | F17 does **not** fire |
| Karst Engine tests (`makeOpPayloadAttributes` sets `noTxPool=true`, stub execute always succeeds) | cannot see F17; `buildPayloadAt` even FCU-builds a Karst-ts payload whose only tx is a user Web3 tx |

`OpEngineServiceParityTest::driveBuildWithCulprit` (`noTxPool=false`) is a first-class seal path: with a culprit hash the loop finishes VALID. Hash-less Q5 cannot use that loop.

## F16 LOW — CONFIRMED
`op_getpayload_v5_response_json_shape` (`OpEngineServiceParityTest.cpp:1062-1099`):
`getPayload(5)` + `combineGetPayloadResponse` + `parseNewPayloadRequest(..., V4)`.
No `pair.service.newPayload`. `OpEngineKarstProfileTest` has zero `newPayload` sites.

## F15 LOW — CONFIRMED
`test_OpForkScheduleMetadata.cpp:1-4` SPDX-only. Sibling `test_GenesisEthHeader.cpp` has full Apache + `@file`.

## F14 LOW — CONFIRMED
`git show --stat 30296bb3b`: 4 files under `.agents/reviews/pr-karst-on-5550-46f9bc9a9/`, +426.

## F18 LOW — CONFIRMED
`EngineServiceCommon.cpp:80` “Eth and Op advertise the same list.”
`OpEngineService.cpp:92-94` OP list is getPayload V4+V5 + newPayload V4 only.
