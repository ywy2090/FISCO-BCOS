# Task 6 — Prague 簇审计笔记（EIP-7702）

**日期：** 2026-06-20  
**范围：** inventory #13–#15；ETH reference `executeViaEth` 路径  
**参考：** geth v1.17.3 `core/state_transition.go`；Besu `PragueGasCalculator`

---

## Step 1 — revision enable 链（inventory #15）

### 调用链

```
EthChainPolicy::computeRevisionConfig(header)
  → RevisionConfig.eip7702 (default false; EthChainPolicy 未赋值)
executeViaEth(input)
  → executeMessage(..., revisionConfig, authorizationListPresent, authorizations)
ExecuteMessage.cpp:173
  → if (revisionConfig.eip7702 && authorizationListPresent && !authorizations.empty())
       applyAuthorizations(...); warmDelegationTarget(...)
```

### 对照

| 来源 | PRAGUE+ `eip7702` |
|------|-------------------|
| `EthChainPolicy.h:27-41` | **未赋值** → false |
| `FiscoPolicy.h:66` | `feature_evm_prague` → true |
| `makeIsthmusRevisionConfig()` | true |
| `RevisionConfigProfileTest` ETH 行 | PRAGUE/OSAKA 期望 false |
| `capability-matrix.md:53` | 声称 `EthChainPolicy at PRAGUE+` inherited |

**Task 1 交叉引用：** `_work/task1-revision-profile.md` Step 1 — matrix 与 EthChainPolicy 不符；7702 kernel apply 在 reference baseline 不可达。

**判定：🔴** — profile 未启用；`ExecuteViaEth.cpp` / `TxFeaturePrepare.h` 无额外赋值。

---

## Step 2 — tx field propagation（inventory #14）

### FB 实现

| 层 | 文件 | 行为 |
|----|------|------|
| 解码 | `Web3Eip7702Decoder.h:74-157` | type `0x04` RLP → `SetCodeAuthorization`；ecrecover authority；无效 sig 跳过 |
| 映射 | `EthTxInputBuilder.h:34-44` | `fillWeb3Fields` → `authorizationListPresent` + `authorizations` |
| 传递 | `ExecuteViaEth.cpp:112-113` | 原样传入 `executeMessage` |

### geth 对照

- type-4 tx → `msg.SetCodeAuthorizations`（`state_transition.go:222,622-627`）
- FB 字段：`chainId`, `address`（delegate target）, `nonce`, `authority`（恢复地址）

### 测试

```bash
cd build && ./bcos-evm/test/EthTxInputBuilderTest --log_level=test_suite
# PASS — fillWeb3Fields_maps_eip7702_authorizations
```

**缺口：** 无 `fillWeb3Fields` → `executeViaEth` → post-state 的 ETH reference E2E；传播在 input 层 ✅，runtime 被 #15 profile 阻断。

**判定：✅**（tx input 映射）/ **🟡**（reference E2E 不可达，见 #15）

---

## Step 3 — kernel authorization apply（inventory #13）

### `Eip7702.cpp` vs geth

| 规则 | FB `applyAuthorizations` | geth `validateAuthorization` + `applyAuthorization` |
|------|--------------------------|------------------------------------------------------|
| zero authority | skip | N/A（从 sig 恢复） |
| chainId | skip if set且≠0且≠txChainId | 同 |
| nonce | skip on mismatch | 同（invalid → skip in loop） |
| existing code | skip if non-empty且非 delegation | 同 |
| warm authority | `warm_up_address(authority)` | `AddAddressToAccessList(authority)` |
| refund | `CALL_NEW_ACCOUNT_GAS - TX_AUTH_TUPLE_GAS` if account exists | 同（`params.CallNewAccountGas - params.TxAuthTupleGas`） |
| apply | nonce+1；zero addr → clear code；else `0xEF0100‖addr` | 同 |
| post warm target | `warmDelegationTarget(codeAddress)` if `warm_access` | `ParseDelegation(GetCode(*msg.To))` warm（`state_transition.go:635-636`） |

delegation 前缀：`0xEF 0x01 0x00` + 20-byte address（23 bytes），与 geth `AddressToDelegation` 一致。

### `EthHost.cpp` 执行语义

- `resolveExecutionCode`（`EthHost.cpp:472-484`）返回账户原始 code，**无** Host 层 delegation 解析。
- delegation 设计符执行由 **evmone + `EVMC_PRAGUE` revision** 委托（与 MCOPY/6780 同类 evmone-delegated）。
- `routeCall` / `call` 不特殊处理 7702；7702 在 tx 级 apply + EVM 加载时生效。

### 门控

`ExecuteMessage.cpp:173-181` 需 **`eip7702 && authorizationListPresent && !authorizations.empty()`** 三者同时成立。

### 测试覆盖

| 测试 | 路径 | 断言 |
|------|------|------|
| `Eip7702ApplyAuthorizationTest` | `opstack/` — 直接 `executeMessage`，**手动** `eip7702=true` | delegation code 23B + nonce bump |
| `Bcos7702ExecuteViaHostPropagationTest` | FISCO `executeViaHost` + manual flag | stateDiff delegation |
| `EthTxInputBuilderTest` | 仅 builder 解码 | 无 apply |
| `ExecuteViaEthFixtureTest` | `executeViaEth` + `makePragueRevisionConfig()`（**无 eip7702**） | 无 auth apply |

**ETH reference baseline：** kernel 代码存在，**不可达**（EthChainPolicy `eip7702=false`）；无 `executeViaEth` + auth list 集成测试。

**判定：🟡** — kernel 实现与 geth 对齐（opstack 单测验证）；reference 路径不可达 🔴（profile）；无 delegation **执行** E2E fixture。

---

## Step 4 — fixture 断言审计

### `stEIP7702_delegation.json`

```json
"source": "hand-crafted/simplified from GeneralStateTests/stEIP7702/delegation (implementation code on delegatee)"
"revision": "prague"
pre[0xbb].code = "0x602a60005260206000f3"   // 普通 RETURN 42 字节码，非 0xEF0100 前缀
tx: plain CALL → 0xbb, data=0x, 无 authorization list
```

### `EthFixtureAdapter.h`

- `makePragueRevisionConfig()` 设 `eip2537/eip7623/...` 但 **不设 `eip7702=true`**。
- `buildExecuteViaEthInput` 不填 `authorizationListPresent` / `authorizations`。

### 运行

```bash
cd build && ./bcos-evm/test/ExecuteViaEthFixtureTest --log_level=test_suite
# PASS（含 stEIP7702_delegation）
```

**结论：** **smoke，非 7702 E2E**。仅验证 Prague revision 下对含普通合约 code 的账户 plain CALL 返回 42。与 `architecture-known-gaps.md` gap #38 行一致。

`FixtureAssert.h` 只断言 status/output/logs/gas，不验证 delegation code 安装或 authority nonce。

---

## Step 5 — 判定汇总

| inventory | 能力 | 状态 | 要点 |
|-----------|------|------|------|
| #13 | authorization apply | 🟡 | 内核与 geth 对齐；EthChainPolicy 阻断 reference 路径 |
| #14 | tx field propagation | ✅ | `EthTxInputBuilder` + `ExecuteViaEth` 传递正确 |
| #15 | revision enable | 🔴 | EthChainPolicy 未设 `eip7702`；matrix 声明不符（Task 1） |

**Task 状态：** **DONE_WITH_CONCERNS**

**建议：**

1. `EthChainPolicy::computeRevisionConfig` 在 `revision >= EVMC_PRAGUE` 设 `eip7702=true`（或更新 matrix 为 feature-gated/unreachable）。
2. `makePragueRevisionConfig()` / fixture 路径同步 `eip7702=true` 以便 reference 测试可达。
3. 替换 `stEIP7702_delegation.json` 为真 7702 向量（auth list + delegation code + 经 delegatee 执行）或新增 `ExecuteViaEth` auth apply 集成测试。
4. 7702 intrinsic gas / precheck 在 reference 路径为 `unsupported`（matrix 行）；OPStack 另有 `OpStackPreCheck`。
