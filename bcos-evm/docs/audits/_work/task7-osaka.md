# Task 7 — Osaka 簇审计笔记（EIP-7212 / EIP-7823）

**日期：** 2026-06-20  
**范围：** inventory #16–#17；ETH reference `executeViaEth` 路径  
**参考：** geth v1.17.3 `core/vm/contracts.go`；Besu 26.6.0 `P256VerifyPrecompiledContract` / `BigIntegerModularExponentiationPrecompiledContract`

---

## Step 1 — EIP-7212 / 0x0100 p256verify（inventory #16）

### grep 结果（`bcos-evm/eth/`）

| 符号 / 路径 | 内容 |
|-------------|------|
| `EthPolicy.h:38` | `cfg.eip7212 = cfg.revision >= EVMC_OSAKA` |
| `RevisionConfig.h:16` | `bool eip7212` profile 字段 |
| `PrecompileTraits.h:37` | `{0x0100, EVMC_OSAKA, …, 6900, 0}` — FISCO `findPrecompile` 表 |
| `EthBuiltinRegistry.cpp:491-518` | `p256verify` 完整实现（160B 输入、6900 gas、`evmmax::secp256r1::verify`） |
| `EthPrecompiles.cpp:55-66` | `toSuffix` **仅** `0x0001–0x0011`；**排除** `0x0100` |
| `EthPrecompiles.cpp:421-465` | `precompileGasCost` / `dispatch` switch 无 `0x0100` case |
| `EthHost.cpp:378-382` | `isBuiltinPrecompileAddress` **包含** `high==0x01 && low==0x00` |
| `EthHost.cpp:189-196` | 嵌套 CALL：`hasPrecompileTarget` → `tryDispatchInCall` |
| `ExecuteMessage.cpp:183-194` | 顶层直调预编译同上 |

**matrix（`capability-matrix.md:59`）：** `unsupported (TE path: not in EthPrecompiles; legacy registry only)` — **与 TE 实现一致**。

### TE 路径行为（OSAKA revision）

1. CALL `0x000…0100` → `isBuiltinPrecompileAddress` = true → `routeCall` 设 `hasPrecompileTarget`。
2. `EthPrecompiles::tryDispatchInCall` → `toSuffix` 返回 `nullopt` → **不 dispatch**。
3. 嵌套 CALL：落回 EVM 空 code 路径；顶层直调：`ExecuteMessage.cpp:197-223` → `makeSuccessResult`（空账户成功）。
4. **与 geth 分叉：** geth Osaka `activePrecompiledContracts` 含 `p256Verify`（6900 gas + 160B 语义）。

### geth / Besu 对照

| 项 | geth | Besu |
|----|------|------|
| 地址 | `0x000…0100` (`contracts.go:171`) | `P256VerifyPrecompiledContract` |
| Gas | `params.P256VerifyGas = 6900` | `OsakaGasCalculator` |
| 输入 | 160 bytes；错误长度 / 无效 sig → 空输出 success | 同 EIP-7212 |
| 有效 sig | 32B 输出末字节 `0x01` | 同 |

`EthBuiltinRegistry.cpp` 语义与 geth `p256Verify.Run` 对齐；**未接入** `EthPrecompiles` TE 路径。

### 测试

```bash
rtk grep -rn "7212\|p256\|0x0100" bcos-evm/test/
# 无匹配 — 零 TE 覆盖
```

`RevisionConfigProfileTest` 仅断言 OSAKA `eip7212=true`（profile 赋值）。

### 判定

| 层 | 状态 | 理由 |
|----|------|------|
| profile `eip7212` | 🔴 | EthPolicy OSAKA+ 设 true，暗示已启用；ADR-004 称 partial；无 TE consumer |
| kernel 0x0100 | 🔴 | matrix unsupported 正确，但 Host 误认 builtin → **静默错误**（非 PRECOMPILE_FAILURE） |
| matrix | ✅ 文档一致 | 建议 P2 更新为强调 Host/Policy 分裂风险 |

**Task 状态：** **DONE_WITH_BLOCKERS**

---

## Step 2 — EIP-7823 modexp 长度上限（inventory #17）

### ModexpGas 实现

| 符号 | 位置 | 行为 |
|------|------|------|
| `MODEXP_MAX_FIELD_LEN_EIP7823` | `ModexpGas.h:26` | `1024`（与 EIP MUST 一致） |
| `validateModexpEip7823` | `ModexpGas.cpp:170-194` | OSAKA+ revision 且任一 field len > 1024 → false |
| `modexpEip7823Enabled` | `ModexpGas.h:30-32` | 读 `rev.eip7823` |
| `shouldRejectModexpEip7823` | `ModexpGas.cpp:196-222` | 组合 modexp 地址 + flag + validate |

### ADR-004 profile-only vs 实际 consumer

| 路径 | 7823 门控 |
|------|-----------|
| FISCO `callBuiltinPrecompiled` | ✅ `PrecompiledImpl.h:78-82` 调用 `shouldRejectModexpEip7823` |
| TE `EthPrecompiles::executeModexp` | ❌ **无** 调用；`EthPrecompiles.cpp:116-152` 直接 `evmone::crypto::modexp` |
| TE gas | `calcModexpGas` 无 7823 拒绝（仅 EIP-7883/2565/198 定价） |

`EthPolicy.h:39` OSAKA+ 设 `eip7823=true`；profile 与 FISCO 路径就绪，**TE reference 未 wired**。

### geth / Besu 对照

| 客户端 | 行为 |
|--------|------|
| geth `bigModExp.Run` (`contracts.go:631-632`) | `eip7823 && max(base,exp,mod) > 1024` → error |
| Besu `BigIntegerModularExponentiationPrecompiledContract` (`:106-118`) | 各 length > `upperBound`(1024) → `PRECOMPILE_ERROR` |

FB `validateModexpEip7823` 逻辑等价；**TE 0x05 路径未调用**。

### 测试

- `RevisionConfigProfileTest`：OSAKA `eip7823=true` ✅（profile only）
- `stModExp_basic.json`：小输入 smoke；**无** >1024 拒绝向量
- **无** `shouldRejectModexpEip7823` / TE modexp 7823 单元测试

### 判定

| 层 | 状态 | 理由 |
|----|------|------|
| profile `eip7823` | 🔴 | Policy OSAKA+ true；ADR-004 profile-only；helper 已实现但未 consumed |
| kernel modexp @ OSAKA | 🔴 | TE 仍接受 >1024 输入并计算；与 geth/Besu MUST 不符 |
| matrix | ✅ | `feature-gated (profile-only; no TE consumer)` 准确 |

**Task 状态：** **DONE_WITH_BLOCKERS**

---

## Step 3 — 判定汇总

| inventory | 能力 | 状态 | 要点 |
|-----------|------|------|------|
| #16 | EIP-7212 precompile 0x0100 | 🔴 | Registry 有实现；TE `EthPrecompiles` 无；Host 误路由 |
| #17 | RevisionConfig `eip7823` | 🔴 | `ModexpGas` 验证存在；TE `executeModexp` 未 wired |

**共同模式：** Osaka profile flags 在 `EthPolicy` 已开启，但 TE baseline（`EthPrecompiles` / `executeMessage`）未接入；FISCO `PrecompiledImpl` 路径已部分 wired（7823）或仅有 Registry（7212）。

**建议（P0）：**

1. **7212：** 在 `EthPrecompiles` 增加 `0x0100` dispatch（可委托 `builtinExecutorBySuffix(0x0100)` 或内联 `evmmax::secp256r1`）；`toSuffix` 扩展；或 OSAKA 前从 `isBuiltinPrecompileAddress` 移除 `0x0100` 直至 wired。
2. **7823：** 在 `EthPrecompiles::executeModexp` 或 `tryDispatchInCall` 入口调用 `shouldRejectModexpEip7823`（传入 `RevisionConfig` + revision）。
3. **测试：** OSAKA fixture — p256verify 有效/无效 sig；modexp baseLen=1025 → PRECOMPILE_FAILURE。
4. **matrix：** 7212 保持 unsupported 直至 TE wired；7823 consumer wired 后改 ADR-004 为 consumed。
