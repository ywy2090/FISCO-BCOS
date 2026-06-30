# ETH Reference CANCUN+ 复审计报告

**日期：** 2026-06-20  
**类型：** P0 修复 + P1/P2 补测复审计  
**分支/commit：** `worktree-feat-evm-refactor` @ `f989f073f` + **未提交变更**（P0 后内核/测试：7702 delegation、1153 purge、7623/1153/7702/2929 测试）  
**前置审计：** `2026-06-20-eth-reference-cancun-plus-audit.md` @ `e16b623e7`（❌ 7×🔴）  
**修复摘要：** `2026-06-20-eth-reference-cancun-plus-audit-fix-summary.md`  
**geth / Besu：** v1.17.3 @ `117e067f0` / 26.6.0 @ `de8d3f0e20`（与初审计相同）

---

## Part 0 — 执行摘要

**结论：** P0 **7×🔴 全部关闭**；P1（7702 E2E / 7623 / 1153）与 P2（2929 opcode gas）补测已通过。ETH reference `executeViaEth` 路径 **⚠️ 有条件通过**（0×🔴，2×🟡 非阻断）。

| 指标 | 初审计 | P0 复审计 | P1 | **当前** |
|------|--------|-----------|-----|----------|
| Part 1 审计行 | 29 | 29 | 29 | 29 |
| ✅ 一致 | 14 | 22 | 25 | **26** |
| 🟡 警告 | 4 | 7 | 4 | **2** |
| 🔴 阻断 | 7 | 0 | 0 | **0** |
| 📋 设计选择 | 4 | 4 | 4 | 4 |
| **合并判定** | **❌** | **⚠️** | **⚠️** | **⚠️ 有条件通过** |

> 仍 ⚠️ 的原因：`eip2929` 等 ADR-004 profile-only 文档化、fixture `gas_used` 断言强化——均非 CANCUN+ 内核阻断。

### 原 🔴 → 状态（P0）

| # | 能力 | 初 | 现 | 验证依据 |
|---|------|----|----|----------|
| 1 | EIP-7702 revision enable | 🔴 | ✅ | `EthChainPolicy.h:38`；`RevisionConfigProfileTest` |
| 2 | EIP-6780 SELFDESTRUCT | 🔴 | ✅ | `EthHost.cpp`；`stSelfDestruct_basic.json`；`Bcos6780SelfdestructTest` |
| 3 | EIP-2537 MSM gas | 🔴 | ✅ | `BlsGas.h`；k=2 → **22776** |
| 4 | EIP-7212 (0x0100) | 🔴 | ✅ | `Eip7212KernelTest`；`Bcos7212ExecuteViaHostTest` |
| 5 | EIP-7823 modexp bounds | 🔴 | ✅ | `Eip7823ModexpRejectTest` |
| 6 | RevisionConfig `eip7212` | 🔴 | ✅ | profile + TE dispatch |
| 7 | RevisionConfig `eip7823` | 🔴 | ✅ | profile + TE reject wired |

### P1 关闭项（🟡 → ✅）

| 能力 | 实现 / 测试 |
|------|-------------|
| EIP-7702 delegation E2E | `stEIP7702_delegation.json`；`resolveExecutableCode`（`ExecuteMessage.cpp` / `EthHost.cpp`） |
| EIP-7623 entry precheck (ETH) | `Eip7623PrecheckTest` + `Bcos7623PrecheckTest` |
| EIP-1153 transient | `State::build_diff()` purge；`Eip1153TransientStorageTest`（3 cases） |

### P2 关闭项（🟡 → ✅）

| 能力 | 测试 |
|------|------|
| EIP-2929 opcode gas | `Eip2929OpcodeGasTest`（5 cases）：`BALANCE` cold **2600** / warm **100**；`SLOAD` cold **2100** / warm **100**；`eip2929=false` 双 cold **5206** |

与 `Eip2929AccessHostTest`（Host COLD/WARM 回调）互补，覆盖 evmone 经 Host 的端到端 gas literal。

### 仍存 🟡（非阻断）

| 能力 | 说明 | 建议 |
|------|------|------|
| `eip2929` profile | ADR-004 profile-only；runtime 用 revision | 文档 |
| Part 3 fixture 断言 | 部分 `gas_used=0` 跳过、2930 名实不符 | P2 余项 |

### 📋 设计选择（非缺口）

| 项 | 说明 |
|----|------|
| BCOS `executeViaHost` SELFDESTRUCT | `allowSelfdestruct=false` 有意行为；ETH reference ✅ |
| EIP-4844 blob / profile-only flags | 见初审计 Part 1 📋 行 |

### 回归测试（22/22 PASS）

**P0（14）：** `RevisionConfigProfileTest`、`Eip7702ApplyAuthorizationEthTest`、`EipPrecompileRevisionGateTest`、`Eip2537KernelTest`、`Eip7823ModexpRejectTest`、`Eip7212KernelTest`、`ExecuteViaEthFixtureTest`、`Eip2929AccessHostTest`、`Bcos7702ExecuteViaHostPropagationTest`、`BcosPrecompileRevisionGateTest`、`Bcos2537MsmGasTest`、`Bcos7823ModexpRejectTest`、`Bcos7212ExecuteViaHostTest`、`Bcos6780SelfdestructTest`

**P1（3）：** `Eip7623PrecheckTest`、`Eip1153TransientStorageTest`、`ExecuteViaEthFixtureTest`（含 `stEIP7702_delegation`）

**P2（1）：** `Eip2929OpcodeGasTest`

**关联仍绿：** `WarmTransactionEntryTest`、`StateJournalRevertTest`（2929 journal）

运行路径：`build/bcos-evm/test/<binary>`

### 内核增量（P1，BCOS 经 `executeMessage` 继承）

| 变更 | 文件 |
|------|------|
| 7702 delegation → delegatee 字节码 | `ExecuteMessage.cpp`、`EthHost.cpp` |
| transient 不进入 `StateDiff` | `State.cpp` `build_diff()` |

---

## Part 1 — 状态变更摘要（相对初审计）

| EIP | 层级 | 初 | **现** | FB 测试 |
|-----|------|----|----|---------|
| EIP-6780 SELFDESTRUCT | kernel | 🔴 | ✅ | fixture + `Bcos6780*` |
| EIP-7702 revision / auth / delegation | profile+kernel | 🔴/🟡 | ✅ | `RevisionConfigProfileTest` + `stEIP7702_delegation.json` |
| EIP-1153 transient | kernel | 🟡 test | ✅ | `Eip1153TransientStorageTest` |
| EIP-7623 entry precheck | orchestration | 🟡 | ✅ | `Eip7623PrecheckTest` + `Bcos7623*` |
| **EIP-2929 runtime warm + gas** | kernel | ✅/🟡 test | **✅** | `Eip2929AccessHostTest` + **`Eip2929OpcodeGasTest`** |
| EIP-2537 / 7212 / 7823 / 门控 | kernel/profile | 🔴/🟡 | ✅ | P0 测试 |

未变 ✅/📋：2929 tx-entry warm、7623 settlement、4844 profile、evmone-delegated opcode 等。

---

## Part 2 — 已关闭偏离项

### P0（7 项）— 见 fix-summary

### P1（3 项）

8. 7702 delegation 未解析 → `resolveExecutableCode`  
9. 7623 ETH 无专项测 → `Eip7623PrecheckTest`  
10. 1153 transient 跨 tx 泄漏 → `build_diff()` purge + runtime 测试

### P2（1 项）

11. **2929 缺 opcode gas 断言** → `Eip2929OpcodeGasTest`（geth/evmone literal：2600/100/2100/5206）

---

## Part 3 — 断言审计（增量）

| 项 | 初 | **现** |
|----|-----|--------|
| `stSelfDestruct_basic.json` | 🟡 | ✅ post-state |
| `stEIP7702_delegation.json` | 🟡 smoke | ✅ E2E |
| `Eip7623PrecheckTest` | 无 | ✅ |
| `Eip1153TransientStorageTest` | 无 | ✅ |
| **`Eip2929OpcodeGasTest`** | 无 | **✅** |
| `g1msm_k2_gas_matches_geth` | 无 | ✅ 22776 |
| fixture `gas_used=0` 跳过 | 🟡 多项 | 🟡 剩 2 项 |

**Fixture 断言批次（M3 T3-1~T3-7）：**
- `stExample_gasPrice0.json` 已补 `gas_used=18`。
- 删除重复 smoke：`prague_call_return_word.json`、`prague_call_revert.json`、`prague_call_empty_account.json`。
- `prague_create_empty_initcode.json` 复核 empty initcode CREATE 期望状态为 `EVMC_SUCCESS`（实测 gas=0，当前机制仍跳过 gas）。
- 历史批次一并纳入：`stEIP7702_delegation` gas+delegation、`stExample_return42_warmProps` 重命名、`prague_selfdestruct` 删除、预编译 gas 落盘、`stPrecompile_ecrecover` 输出修正。

---

## Part 4 — 后续动作

### 已完成

| 阶段 | 项 |
|------|-----|
| P0 | P0-1～P0-6 |
| P1 | 7702 E2E + delegation 解析 + 7623 + 1153 |
| P2 | **2929 opcode gas**（`Eip2929OpcodeGasTest`） |

### 建议余项（非阻断）

1. `FixtureAssert` 减少 `gas_used==0` 跳过  
2. ADR-004 profile-only 字段消费文档化  
3. `capability-matrix.md` 同步 P1/P2 行  

### 合并判定

| 规则 | 结果 |
|------|------|
| 任一 🔴 | 无 |
| 无 🔴 有 🟡 | **⚠️ 有条件通过**（2×🟡 均为文档/断言深度） |
| 仅 ✅/📋 | 未达 |

**复审计合并判定：⚠️ 有条件通过**

---

## Spec 自检

- [x] 7 项原 🔴 逐项对照  
- [x] P0+P1+P2 回归 22/22 PASS  
- [x] 2929 Host + opcode gas 双覆盖  
- [x] BCOS 继承边界已注明  
- [ ] capability-matrix 待同步  
- [ ] 内核/测试变更待 git commit
