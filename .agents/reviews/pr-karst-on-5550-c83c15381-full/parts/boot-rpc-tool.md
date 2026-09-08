# boot-rpc-tool — FULL @ c83c15381

## Summary
OP boot, `[op_fork_schedule]` loader, Engine JSON endpoint, genesis template, Karst release gate. Live path: `OpForkSchedule::parse(canonical)` (no `TestBypass`) → `buildOp`. Prior template F2 is fixed. **Zero new findings.**

## Findings
None. Did not re-file F1–F14 or carry-forward LOWs (release gate tracked-only; stored-vs-genesis ignore).

## Claim-audit
Disclosed Karst landing matches this slice: timestamp schedule from genesis `canonical=`, getPayload V4→V5 on the wire. No extra boot/RPC behaviour beyond known K1 leftover (`opJovianActive` when section+metadata absent). `generateGenesisData` still omits `m_opstackForkSchedule` (carry-forward, not re-filed).

## Conventions
Touched headers (`Initializer.h`, `NodeConfig.h`, `EngineEndpoint.h`) carry full Apache + SPDX + `@file`. No new `k*` constants in these files. Loader throws `InvalidConfig` via `BOOST_THROW_EXCEPTION`, not raw `std::invalid_argument`.

## Checked, and not a problem
1. **TestBypass stays out of boot.** Grep of `libinitializer/` is empty. Production ctor is parse-only:
```
m_opForkSchedule = std::make_shared<bcos::evm::opstack::OpForkSchedule>(
    bcos::evm::opstack::OpForkSchedule::parse(canonical));
```
(`Initializer.cpp:540-541`)

2. **Template names the real section.** Operator instruction is `[op_fork_schedule] canonical=` with `…:karst` examples (`chain-config.template.yaml:16-19`). NodeConfig reads that key (`NodeConfig.cpp:1270-1284`).

3. **getPayloadV5 is callable and version-stamped.** Endpoint forwards V5 into the service:
```
co_await handleGetPayload(engine::ApiVersion::V5, request, response);
```
(`EngineEndpoint.cpp:226-228`) then `getPayload(payloadId, (uint32_t)version)` (`:250-251`). Consult: OP caps include `"engine_getPayloadV5"` (`OpEngineService.cpp:94`). FCU V4 is unimplemented (`:145-149`) and is **not** in that advertise list.

4. **Production fork token is `karst`.** Loader test pins `0:jovian,1:karst` and rejects `0:isthmus,1:karst` (`NodeConfigOpForkScheduleTest.cpp:32-47`).

5. **`maxEngineVersion=V4` does not cap V5.** Passed at `Initializer.cpp:568`; consult `EngineServiceInitializer.h:122` `(void)maxEngineVersion`. Profile mismatch is service `-38005`, not this cap.

## Verification boundary
Assigned files only (Initializer.cpp grepped then read `:360-576`; EngineEndpoint.cpp once; NodeConfig `loadOpForkSchedule` + `generateGenesisData`; tests; template; gate). Consult greps: `OpEngineService.cpp` caps, `EngineServiceInitializer.h` discard, `OpForkSchedule.cpp` parse throws. `EngineHelper` not in tree; V5 JSON shape pinned by `EngineRpcTest.cpp:519-555` through the endpoint. Did not compile or run tests. Gate CI wiring is the known tracked-only LOW.

Reviewed with fisco-review v1.20.1
