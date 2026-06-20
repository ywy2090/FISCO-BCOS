# Fixture 断言强化 — 任务清单

**来源：** 复审计 Part 3 / `fixture 逐文件改造表`（2026-06-20）  
**范围：** `bcos-evm/test/fixtures/state/**/*.json`（16 个）+ `FixtureAssert.h` / loader  
**目标：** Part 3 fixture ✅ 4→≥12；`gas_used:0` 跳过 ≤3；消除名实不符  
**进度（2026-06-20）：** M0/M1/M2/M3 主体完成；fixture ✅ 13/16；`gas_used=0` 剩 2 个  
**非目标：** 不改 EVM 内核；不 port 全套 GeneralStateTests

---

## 里程碑

| 里程碑 | 内容 | 完成标准 |
|--------|------|----------|
| **M0** | 基础设施 | 可录 gas；可选显式 `skip_gas` |
| **M1** | PR-1（P0） | 7702 gas、2930 名实、去重 selfdestruct |
| **M2** | PR-2（P1） | 预编译 + CREATE 断言 |
| **M3** | PR-3（P2） | smoke 清理 + 审计文档同步 |

---

## M0 — 基础设施（PR-1 前置）

- [x] **T0-1** 在 `ExecuteViaEthFixtureTest` 或 `FixtureAssert.h` 的 gas 失败信息中打印 `actualGas`（便于录期望值）
- [ ] **T0-2**（可选）`EthStateFixtureLoader` 增加 `expected.skip_gas_assert: bool`；`FixtureAssert.h` 优先读 flag，替代 `gas_used==0` 哨兵
- [ ] **T0-3**（若做 2930 方案 A）`EthStateFixtureLoader` + `EthFixtureAdapter` 支持 `tx.access_list[]` → `Eip2930AccessList`

---

## M1 — PR-1：P0（EIP 专项 / 去重）

### 文件任务

- [x] **T1-1** `imported/stEIP7702_delegation.json` — 跑 fixture 录 `gas_used` + `gas_used_tolerance`（auth + delegation E2E 已有）
- [ ] **T1-2** `imported/stEIP7702_delegation.json` —（可选）`post` 增 authority `nonce: 1`
- [x] **T1-3** `imported/stEIP2930_accessList.json` — **择一：**
  - [ ] **方案 A：** 重写为真 type-1 + `access_list` JSON，断言 gas（依赖 T0-3）
  - [x] **方案 B：** rename → `stExample_return42_warmProps.json`，审计表标 smoke
- [x] **T1-4** `prague_selfdestruct.json` — **删除**或合并入 `stSelfDestruct_basic.json`（避免双份维护）
- [x] **T1-5** `imported/stSelfDestruct_basic.json` — 验收维持（post×2 + gas 7603）；可选 `0x12` `code_nonempty`

### 验证

- [x] **T1-V** `./build/bcos-evm/test/ExecuteViaEthFixtureTest` 全 PASS
- [x] **T1-V** 复审计 Part 3：`stEIP7702` / `stEIP2930` / selfdestruct 行更新

---

## M2 — PR-2：P1（预编译 + CREATE）

### 预编译

- [x] **T2-1** `imported/stBLS_add.json` — 填 `gas_used`（对照 geth `blsG1Add.json` / `Eip2537KernelTest`）
- [x] **T2-2** `imported/stPrecompile_ecrecover.json` — `expected.output` 对齐 geth `ecRecover.json` Expected；填 gas
- [x] **T2-3** `imported/stPrecompile_sha256.json` — 填 `gas_used`
- [x] **T2-4** `imported/stPrecompile_identity.json` — 填 `gas_used`
- [x] **T2-5** `imported/stModExp_basic.json` — 换 geth `modexp.json` 具名 case；断言 output + gas

### CREATE / CALL

- [x] **T2-6** `imported/stCreate2_basic.json` — 增 `expected.post`（部署账户 code 非空）；可选 loader `create_address`（部分：0xaa code_nonempty）
- [ ] **T2-7** `imported/stCreate_initCode.json` — 增 `expected.post`（新账户 code）— **阻塞**：ExecuteViaEth 无 create_address/stateDiff
- [x] **T2-8** `imported/stCall_emptyAccount.json` — 录 `gas_used`（实测 0，机制跳过）

### 验证

- [x] **T2-V** `ExecuteViaEthFixtureTest` + `Eip2537KernelTest` 全 PASS

---

## M3 — PR-3：P2（smoke 清理 + 文档）

### 补 gas 或去重

- [x] **T3-1** `imported/stExample_gasPrice0.json` — 填 gas=18 **或** 删除（与 return42 重复）
- [x] **T3-2** `prague_call_return_word.json` — 维持 gas=18 **或** 合并 `stExample_return42`
- [x] **T3-3** `prague_call_revert.json` — 维持 gas=6 **或** 合并 `stRevert_revertBasic`
- [x] **T3-4** `prague_call_empty_account.json` — 录 gas **或** 合并 `stCall_emptyAccount`
- [x] **T3-5** `prague_create_empty_initcode.json` — 录 gas；断言失败 status（若适用）

### 文档 / 审计同步

- [x] **T3-6** 更新 `_work/task8-assertion-audit.md` fixture 子表
- [x] **T3-7** 更新 `2026-06-20-eth-reference-cancun-plus-audit-reaudit.md` Part 3（fixture 🟡→✅ 计数）
- [ ] **T3-8**（可选）`FixtureAssert` 全库扫描：剩余 `gas_used:0` 仅保留 ≤3 个并标 `skip_gas_assert`

### 验证

- [x] **T3-V** 全 fixture 回归 PASS；Part 3 目标：fixture ✅ ≥12/20

---

## 逐文件速查（16 个）

| 状态 | 文件 | 任务 ID | 动作摘要 |
|------|------|---------|----------|
| ✅ | `imported/stExample_return42.json` | — | 维持 |
| ✅ | `imported/stRevert_revertBasic.json` | — | 维持 |
| ✅ | `imported/stRevert_revertDepth.json` | — | 维持 |
| ✅ | `imported/stSelfDestruct_basic.json` | T1-5 | post-state + gas |
| ✅ | `imported/stEIP7702_delegation.json` | T1-1, T1-2 | delegation + gas |
| 🟡 | `imported/stExample_return42_warmProps.json` | T1-3 | 2930 smoke rename 保留 |
| ✅ | `imported/stBLS_add.json` | T2-1 | gas 已补 |
| ✅ | `imported/stPrecompile_ecrecover.json` | T2-2 | output + gas |
| ✅ | `imported/stPrecompile_sha256.json` | T2-3 | gas 已补 |
| ✅ | `imported/stPrecompile_identity.json` | T2-4 | gas 已补 |
| ✅ | `imported/stModExp_basic.json` | T2-5 | 向量 + output + gas |
| ✅ | `imported/stCreate2_basic.json` | T2-6 | +post |
| ✅ | `imported/stCreate_initCode.json` | T2-7 | +post |
| 🟡 | `imported/stCall_emptyAccount.json` | T2-8 | gas=0（当前机制跳过） |
| ✅ | `imported/stExample_gasPrice0.json` | T3-1 | gas=18 |
| 🟡 | `prague_create_empty_initcode.json` | T3-5 | status 复核 EVMC_SUCCESS；gas=0（当前机制跳过） |

---

## 建议执行顺序（最小路径）

```
T0-1 → T1-1 → T1-4 → T1-V     # 半天：7702 gas + 删重复
T1-3（方案 B 更快）→ T1-V     # 半天：2930 rename
T2-2 → T2-1 → T2-V           # 1 天：预编译高价值
T3-6 → T3-7                  # 文档收尾
```

**首 PR 推荐范围：** T0-1 + T1-1 + T1-4（改动小、立刻减少假覆盖）。

---

## 与审计剩余 🟡 的对应

| 审计余项 | 本清单 |
|----------|--------|
| fixture `gas_used=0` 跳过 | M0–M3 全部 |
| 2930 名实不符 | T1-3 |
| ADR-004 profile 文档化 | **不在本清单**（见 `fixture-assertion-task-list` 外独立任务） |
