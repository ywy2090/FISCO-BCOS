# engine @ c83c15381 (fisco-review v1.20.1)

Live path matches the disclosed triple: FCU V3, newPayload V4, getPayload V5 only at Karst. Profile keys on payload/attrs timestamp (ms→unix s), never head. Caps advertise getPayload V4+V5 and newPayload V4 only; FCU V4/newPayload V5 are not listed. No undisclosed live-path behaviour in these files. No new self-oracle (corpus `blockHash`; `s6_request_rebuild_*` is the documented discriminator).

## Findings

### [E1] Karst V5 tests never call newPayload
**Severity:** LOW
**Origin:** INTRODUCED
**Scope:** tests
**Location:** engine/test/unittests/engine/OpEngineServiceParityTest.cpp:1062
**Evidence:**
```
/// The full getPayload-response JSON shape as the CL sees it (Karst pairing
/// FCU V3 -> getPayload V5 -> newPayload V4): combineGetPayloadResponse must
...
auto parsed = bcos::rpc::parseNewPayloadRequest(params, bcos::engine::ApiVersion::V4);
checkSameExecutionPayload(payload->executionPayload, parsed.executionPayload);
```
**Problem:** Comment claims the Karst pairing includes `newPayload V4`. The test only JSON-parses as V4; it never calls `OpEngineService::newPayload`. `OpEngineKarstProfileTest.cpp` has zero `newPayload` sites. Caps tests pin `engine_getPayloadV5` present and dead V1–V3 getPayload/newPayload, but not `engine_newPayloadV5` absent.
**Why it matters:** `handleOpNewPayload` requires `version == ctx.api.newPayload` at the payload timestamp. If Karst `engineApiFor.newPayload` were bumped to V5 while the V4-only static gate stayed, every Karst `newPayload V4` would -38005 and every engine Karst test would still pass.
**Recommended fix:** After the V5 JSON pin, `newPayload(parsed, 4)` and expect VALID; assert caps contain neither `engine_newPayloadV5` nor `engine_forkchoiceUpdatedV4`.

## Checked, not a problem

1. getPayload keys on payload timestamp, not head (`OpEngineService.inl:117-120`):
```
uint64_t const tsSec = unixSecondsFromInternalMillis(built->executionPayload.timestamp);
auto const ctx = requireOpEngineForkAt(tsSec);
if (version != static_cast<std::uint32_t>(ctx.api.getPayload))
```
2. FCU build keys on attrs timestamp; Karst does not bump FCU (`OpEngineService.inl:154-158`):
```
// Isthmus/Jovian/Karst all advertise FCU V3, so Karst does not bump this.
uint64_t const tsSec = unixSecondsFromInternalMillis(payloadAttributes->timestamp);
if (version != static_cast<std::uint32_t>(ctx.api.forkchoiceUpdated))
```
3. newPayload stays V4 (`OpEngineService.inl:667-679` + `OpEngineService.h:228-231`): static gate is V4-only, then `version == ctx.api.newPayload`.
4. Caps vs live (`OpEngineService.cpp:92-94`): `forkchoiceUpdatedV1-V3`, `getPayloadV4`, `getPayloadV5`, `newPayloadV4`. Heartbeat V1/V2 skip the profile check; attrs require V3 (`inl:147-152`).
5. `requireGetPayloadShape` V3 field checks use `>= V3` / `>= V4` (no upper bound), so V5 still requires withdrawalsRoot and V3 blob/beacon fields.

## Conventions
Full Apache grant on all nine files. Production constants use `c_`. Throws are `BOOST_THROW_EXCEPTION`. Tracker lock released before `co_await` in getPayload.

Reviewed with fisco-review v1.20.1
