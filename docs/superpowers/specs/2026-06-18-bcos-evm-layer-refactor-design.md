# bcos-evm 分层架构重构设计（Step 1–4）

**Status:** Approved（2026-06-18）  
**Date:** 2026-06-18  
**Builds on:** [2026-06-18-bcos-evm-plan-c.md](./2026-06-18-bcos-evm-plan-c.md)（Plan C 已完成 C0–C5、C4-2）  
**Scope:** `bcos-evm/`（eth / bcos / opstack）+ `transaction-executor/` 接线；**不含** `bcos-executor` DAG/旧 executive 路径

---

## 1. 背景与动机

Plan C 已将 TE 生产路径切到 `executeViaHost`，EIP-2929 warm set 迁入 `bcos-evm/eth`。当前距「**TE + bcos-evm/eth 路径**与以太坊对齐、bcos/opstack 扩展零入侵」仍有差距（DAG/旧 executive 路径不在范围，语义不保证一致）：

| 缺口 | 影响 |
|------|------|
| `EthHost::call()` 无递归 `vm.execute()` | 嵌套 CALL 语义错误（stub 返回 SUCCESS）；`transition()` 无法覆盖嵌套场景 |
| eth 层残留 FISCO 概念（`callFiscoPrecompile`、`[PRECOMPILED]` 路由） | eth 无法作为独立纯 eth 库 |
| `RevisionConfig` 混入 FISCO `fix_*` flag | 策略边界模糊 |
| `transition()` 与 `executeViaHost()` 逻辑重复 | 双路径维护成本 |
| OpStack 仅费用层草稿 | 未接入 executeViaHost 管线 |
| 单一 CMake 目标 | 无编译期模块隔离 |

**参考架构：** evmone-exec 式分层 — 共享 State/Host 内核，Eth/OP/Fisco Executor 在链层扩展。

---

## 2. 范围与非目标

### 2.1 In Scope

- Step 1：`EthHost::call()` 完整递归语义 + `get_block_hash` 接线（**策略 A**，见 §13）
- Step 2：`RevisionConfig` 纯化 + `FiscoRevisionConfig` 下沉 + `eth::executeMessage()` 抽出 + `ExecuteViaHost` 接线
- Step 3：三轨编排收敛（eth / bcos / opstack）+ TE 接线
- Step 4：CMake 模块拆分 + 公共 API 头

### 2.2 Out of Scope（明确排除）

- **`bcos-executor` DAG / 旧 `TransactionExecutive` / `HostContext` 路径的任何改动**
- 不删除、不迁移、不对齐 `bcos-executor/src/vm/Eip2929AccessState.h` 等遗留实现
- 不保证 DAG 路径与 `executeViaHost` 语义一致（双路径长期并存）
- WASM 执行路径重构
- 全量 General State Tests 向量导入
- OpStack L1Block 预编译完整实现（Step 3 仅接口 + 费用层接线）
- **禁止** 帧级嵌套 CALL 重入 `executeViaHost`（旧 `externalCaller` / `ExecutiveWrapper` 模式，见 §13）

> **用户确认（2026-06-18）：** DAG/旧 executive 遗留不作处理。

---

## 3. 推进策略

采用 **纵向切片**，分 Phase α 交付 Step 1–4。不采用大爆炸或先拆 CMake 再填语义的横向切层。

| Phase | 内容 | 交付物 |
|-------|------|--------|
| **Phase α** | Step 1 → 2 → 3 → 4 | TE+bcos-evm/eth 路径语义完整；bcos/op 扩展零入侵 |
| **（取消）Phase β** | executor EIP-2929 清理 | 用户明确排除，不实施 |

---

## 4. Step 1：补全 eth 核心 Host

### 4.1 目标

`EthHost::call()` 实现与 evmone-exec `CommonHost::call()` 等价的递归语义（**策略 A**）。`transition()` 与后续 `executeViaHost` 共用同一 `EthHost` 递归点。

**Step 1 边界：** 仅改动 `eth/` 层 + eth 测试。**不改动** `ExecuteViaHost.cpp`（推迟至 Step 2）。

### 4.2 两层职责（不可混淆）

| 层 | 职责 | 频率 |
|----|------|------|
| Transaction 编排（`executeViaHost` / `transition`） | warm entry、auth、deriveMessage、buyGas、**顶层** `vm.execute` 一次 | 每 tx 一次 |
| Host（`EthHost::call`） | precompile、value transfer、checkpoint、**递归** `vm.execute`、commit/rollback | 每 CALL/CREATE 一次 |

### 4.3 目标 call() 流程

见 §14 钩子顺序表。核心路径：

```
call(msg)
  → routeCall(msg)                    // 仅地址路由 + CREATE pin，不调 prepareMessage
  → tryChainPrecompile()              // 链扩展（FISCO/OP）；eth 默认 nullopt
  → EthPrecompiles::tryDispatchInCall() // 内置 0x01–0x11
  → DELEGATECALL 预编译门控
  → prepareMessage()                  // CREATE 帧副作用（auth 表、nonce）
  → transferValue()
  → state.checkpoint()
  → vm.execute(host, rev, msg, code)  // 唯一递归点
  → SUCCESS ? commit() : revert()      // REVERT 保留父帧 warm set（EIP-2929）
  → return Result
```

### 4.4 文件改动

| 文件 | 操作 | 要点 |
|------|------|------|
| `bcos-evm/eth/state/EthHost.hpp` | 修改 | `evmc::VM* m_vm`、`BlockHashes m_blockHashes`（或引用）；构造接收 VM + block_hashes |
| `bcos-evm/eth/state/EthHost.cpp` | 重写 | `call()` 递归；`get_block_hash` 接线；`routeCall` 去掉 `onCreateFrameEntry`；移除 `parseDynamicPrecompileTarget` |
| `bcos-evm/eth/policy/HostExtension.h` | 修改 | `callFiscoPrecompile` → `tryChainPrecompile`；`onCreateFrameEntry` → `prepareMessage` |
| `bcos-evm/bcos/FiscoHostExtension.h/.cpp` | 修改 | 覆写新方法；`[PRECOMPILED]` 路由迁入 `tryChainPrecompile` |
| `bcos-evm/eth/state/EthPrecompiles.hpp/.cpp` | 修改 | 新增 `tryDispatchInCall()` |
| `bcos-evm/eth/state/transition.cpp` | 修改 | `EthHost` 传入 `&vm` + `block_hashes`；顶层预编译 dispatch 保留或收敛至 `call()` 内（二选一，实现时统一） |
| `bcos-evm/test/state/NestedCallHostTest.cpp` | 新增 | **真实 `EthHost::call()` 递归** — 合约 A 调 B |
| `bcos-evm/test/state/PrecompileInCallTest.cpp` | 新增 | CALL 指令调内置预编译 |
| `bcos-evm/test/state/BlockHashHostTest.cpp` | 新增 | BLOCKHASH 操作码 |
| `bcos-evm/test/state/NestedRevertWarmTest.cpp` | 新增 | 子帧 REVERT 后父帧 warm set 保持 |
| `bcos-evm/test/CMakeLists.txt` | 修改 | 注册新测试 target |

**Step 1 不改动：** `bcos-evm/bcos/ExecuteViaHost.cpp`、`transaction-executor/`（Step 2 统一接线）。

### 4.5 验收

```bash
ctest --test-dir build/bcos-evm/test                              # 全部 PASS（含新增 ≥4）
ctest -R 'NestedCallHost|PrecompileInCall|BlockHashHost|NestedRevertWarm'
# 新增测试必须走 EthHost::call() 递归，不得用 CompatHostShim::runEvm 旁路
```

> **注意：** Step 1 完成后 `CompatExecuteViaHost` 可能需调整（当前 shim 绕过 `EthHost::call()`）。调整归入 Step 2 一并验收。

---

## 5. Step 2：策略分层纯化

### 5.1 目标

- `eth/RevisionConfig` 仅含 EIP + revision 参数
- FISCO bugfix / 链身份 flag 下沉到 `bcos/FiscoRevisionConfig`
- 抽出 `eth::executeMessage()` 作为纯 eth 编排核心
- `bcos-evm/**` 零 `bcos-executor` include（不仅 eth/）
- `ExecuteViaHost` 接线：`EthHost` 传入 `input.vm`；**移除** TE `externalCaller` 重入模式

### 5.2 类型拆分

```
eth/RevisionConfig           — A/B/E 段（EIP、revision 派生、calldata floor）
bcos/FiscoRevisionConfig     — 包装 eth + fix_* + enable_auth_check 等
eth/executeMessage()         — warm → EthHost(vm) → 顶层 vm.execute → commit/revert
bcos/executeViaHost()        — FISCO 外壳：deriveMessage、auth、FiscoHostExtension 注入
```

`fix_storage_status` 等 FISCO 行为通过 `FiscoRevisionConfig` → `EthHost(fixStorageStatus)` 构造参数传入，**eth 核心不读 FISCO flag 表**。

### 5.3 文件改动

| 文件 | 操作 | 要点 |
|------|------|------|
| `bcos-evm/eth/RevisionConfig.h` | 修改 | 移除 C 段（fix_*）、D 段（enable_*） |
| `bcos-evm/bcos/FiscoRevisionConfig.h` | 新增 | FISCO 策略 overlay；`eth()` 便捷访问 |
| `bcos-evm/bcos/FiscoPolicy.h` | 修改 | 返回 `FiscoRevisionConfig` |
| `bcos-evm/eth/vm/EthPolicy.h` | 修改 | 仅填充纯 `RevisionConfig` |
| `bcos-evm/eth/executeMessage.h/.cpp` | 新增 | 纯 eth 执行管线 |
| `bcos-evm/bcos/ExecuteViaHost.h/.cpp` | 重构 | 调用 `executeMessage()` + FISCO 适配；`EthHost` 传 `input.vm` |
| `bcos-evm/bcos/FiscoConstants.h` | 新增 | 替代 `bcos-executor/Common.h` 常量 |
| `bcos-evm/bcos/FiscoHostExtension.h/.cpp` | 修改 | 去除 `bcos-executor` include；`precompileCaller` 在 `tryChainPrecompile` 内 dispatch |
| `bcos-evm/bcos/ExecutiveWrapper.h` | 移出 | 上移到 `transaction-executor/`（DAG 遗留，新路径不依赖） |
| `transaction-executor/.../TransactionExecutorImpl.h` | 修改 | 删除 stub `externalCaller`；嵌套 CALL 由 `EthHost::call()` 处理 |
| `transaction-executor/tests/ExecuteViaHostEip2929Harness.h` | 修改 | `CompatHostShim` 改为走真实 `EthHost::call()` 递归 |

### 5.4 验收

```bash
# CI：整个 bcos-evm 目录不得 include bcos-executor
! grep -r 'bcos-executor' bcos-evm/
! grep -rE 'bcos/Fisco' bcos-evm/eth/

ctest --test-dir build/bcos-evm/test
ctest -R 'CompatExecuteViaHost'              # 50/50（shim 已对齐真实 call 路径）
ctest -R 'ExecuteViaHostCompat'
ctest -R 'TestFiscoPolicy|TestStandardEthPolicy'
```

---

## 6. Step 3：编排层三轨收敛

### 6.1 目标

```
eth::executeMessage()        ← transition() 与 TE 共享内核
bcos::executeViaHost()       ← Fisco 扩展注入
op::executeViaHost()         ← OpHostExtension + OpStackTxExecutor 费用（编译 + 烟雾；TE 默认仍走 bcos）
```

消除 `transition()` 与 `executeViaHost()` 的重复 warm/execute/commit 逻辑。

### 6.2 文件改动

| 文件 | 操作 | 要点 |
|------|------|------|
| `bcos-evm/eth/executeMessage.h/.cpp` | 完善 | 统一管线 |
| `bcos-evm/eth/state/transition.cpp` | 简化 | 薄包装 → `executeMessage()` + receipt 转换 |
| `bcos-evm/bcos/ExecuteViaHost.cpp` | 简化 | 仅 FISCO 差异层 |
| `bcos-evm/opstack/OpHostExtension.h` | 新增 | HostExtension 子类（deposit/L1 钩子 stub） |
| `bcos-evm/opstack/OpStackExecuteViaHost.h` | 新增 | op 路径入口（无 TE 消费者，烟雾验证） |
| `bcos-evm/opstack/OpStackTxExecutor.h` | 修改 | 接 `ExecuteViaHostOutput`；去除 `m_hostContext` |
| `bcos-evm/eth/EthTxExecutor.h` | 修改 | 接入 `executeMessage()` |
| `transaction-executor/.../TransactionExecutorImpl.h` | 保持 | 仍调用 `bcos::executeViaHost` |

**不改动（遗留并存）：** `bcos-executor` HostContext / Eip2929* / DAG 路径。

### 6.3 新增测试

| 文件 | 内容 |
|------|------|
| `bcos-evm/test/opstack/OpStackExecuteViaHostSmokeTest.cpp` | buyGas L1 fee 扣款烟雾（≥3 cases） |

### 6.4 验收

```bash
ctest -R 'CompatExecuteViaHost'              # 50/50
ctest -R 'ExecuteViaHostCompat'              # PASS
ctest -R 'FIB101_102_103_104_SchedulerTest'  # 10/10
ctest --test-dir build/bcos-evm/test         # PASS
ctest -R 'OpStackExecuteViaHost'             # ≥3 PASS
```

---

## 7. Step 4：构建隔离 + 公共 API

**硬依赖 Step 2 完成**（`RevisionConfig` 纯化、`EthPolicy` 去 `ledger::Features`）。

### 7.1 CMake 目标

```cmake
add_library(bcos-evm-eth STATIC ...)       # 仅 eth/；链接 evmone + bcos-utilities
add_library(bcos-evm-bcos STATIC ...)      # PUBLIC bcos-evm-eth
add_library(bcos-evm-op STATIC ...)        # PUBLIC bcos-evm-eth
add_library(bcos-evm ALIAS bcos-evm-bcos)  # FISCO 默认
```

### 7.2 公共头

```
include/bcos-evm/
  executor.hpp           # executeMessage 接口（facade，不搬迁现有路径）
  eth_executor.hpp       # transition + EthTxExecutor
  fisco_executor.hpp     # executeViaHost + FiscoTxExecutor
  op_executor.hpp        # OpStackExecuteViaHost
```

### 7.3 编译边界 CI

- `bcos-evm-eth` 不得 include `bcos/`、`opstack/`、`bcos-executor/`
- `bcos-evm-bcos` 不得 include `opstack/`
- `bcos-evm-op` 不得 include `bcos/`
- `bcos-evm/**` 不得 include `bcos-executor/`

### 7.4 验收

```bash
cmake --build build --target bcos-evm-eth bcos-evm-bcos bcos-evm-op
ctest --test-dir build/bcos-evm/test
ctest -R 'CompatExecuteViaHost|ExecuteViaHostCompat|FIB101'
# transaction-executor 构建 + 全量测试
```

---

## 8. 依赖顺序

```
Step 1 (EthHost 递归 call，仅 eth/)
  → Step 2 (RevisionConfig + executeMessage + ExecuteViaHost 接线 + TE 去 externalCaller)
    → Step 3 (三轨收敛 + OpStack stub)
      → Step 4 (CMake 拆分，硬依赖 Step 2)
```

**工期估算（Phase α）：** 11–16 天（含 Step 1 嵌套 CALL 测试对齐 + review）

---

## 9. 风险与缓解

| 风险 | 缓解 |
|------|------|
| Step 1 递归 call 改变 TE 嵌套语义（当前 stub） | 策略 A 为有意修正；Step 2 移除 externalCaller；补 NestedCallHost + TE compat 回归 |
| `call()` 递归与 FISCO precompile 冲突 | `[PRECOMPILED]` 仅在 `FiscoHostExtension::tryChainPrecompile`；通过 `precompileCaller` 回调 dispatch |
| `routeCall` 与 `prepareMessage` 重复 | §14 单一真相表；`routeCall` 仅路由，CREATE 副作用只在 `prepareMessage` |
| `RevisionConfig` 拆分爆炸半径 | `FiscoRevisionConfig::eth()` + 过渡期 type alias；批量替换 |
| CompatHostShim 绕过 `EthHost::call()` | Step 2 改造 shim；长期不以旁路测试为唯一基线 |
| OpStack stub 不足 | Step 3 仅烟雾；L1Block 完整实现列 C6 |
| DAG 路径语义漂移 | §2.2 明确排除；措辞限定「TE+bcos-evm/eth 路径」 |

---

## 10. 回归基线（全程不得回退）

| 测试集 | 基线 | 备注 |
|--------|------|------|
| `CompatExecuteViaHost` | 50/50 PASS | Step 2 后须走真实 `EthHost::call()` |
| `build/bcos-evm/test` | 9/9 → Step 1 后 ≥13 | 含 NestedCall/RevertWarm 等 |
| `ExecuteViaHostCompat` | PASS | |
| `FIB101_102_103_104_SchedulerTest` | 10/10 PASS | |
| `test-transaction-executor` 构建 | OK | |

---

## 11. Spec Self-Review（2026-06-18，Grill 后修订）

| 检查项 | 结果 |
|--------|------|
| TBD/TODO 占位 | 无 |
| 内部一致性 | 策略 A 与 Step 1/2 边界、§14 钩子表一致；禁止帧级重入 executeViaHost |
| 范围聚焦 | DAG/executor 排除；措辞限定 TE+bcos-evm/eth |
| 歧义 | Step 1 不改 ExecuteViaHost；Step 4 硬依赖 Step 2 |

---

## 12. 审批记录

| 项 | 状态 |
|----|------|
| 纵向切片推进 | **已确认（2026-06-18）** |
| DAG/旧 executive 不处理 | **已确认（2026-06-18）** |
| 嵌套 CALL 策略 **A**（`EthHost::call()` 内递归 `vm.execute`） | **已确认（2026-06-18）** |
| 禁止策略 C（帧级重入 `executeViaHost` / `externalCaller`） | **已确认（2026-06-18）** |
| Step 1–4 文件范围（Grill 修订版） | **已确认（2026-06-18）** |
| 全文 | **已批准（2026-06-18）** |

---

## 13. 嵌套 CALL 架构决策（策略 A）

**已确认：A — `EthHost::call()` 内统一递归 `vm.execute()`。**

### 13.1 选定方案

```
executeViaHost (tx 级，每 tx 一次)
  warmTransactionEntry / auth / deriveMessage
  EthHost(host, vm, extension)
  vm.execute(host, topMsg, code)        ← 顶层入口
      └─ EthHost::call(nestedMsg)       ← 唯一递归点
            tryChainPrecompile()         ← FISCO/OP 链扩展
            EthPrecompiles::dispatch()   ← eth 内置
            prepareMessage()
            checkpoint → vm.execute → commit/rollback
```

### 13.2 明确拒绝的方案

| 方案 | 拒绝原因 |
|------|----------|
| **B** transition 递归、TE 保持 stub | 双语义 Host，eth 库不可独立验证 |
| **C** `externalCaller` 重入 `executeViaHost` | 层级倒挂：帧级重入 tx 编排（warm/auth/deriveMessage 语义错误）；旧 ExecutiveWrapper 反模式 |

### 13.3 FISCO 预编译在策略 A 下的位置

- 系统预编译 / `[PRECOMPILED]` 路由：`FiscoHostExtension::tryChainPrecompile()`
- 执行：注入的 `precompileCaller` 回调直接 dispatch，返回 `evmc_result`
- **不** 启动新 `TransactionExecutive`；**不** 重入 `executeViaHost`

### 13.4 TE 变更（Step 2）

- 删除 `TransactionExecutorImpl` 中 stub `externalCaller`（`"external call not available"`）
- 嵌套 CALL 全部由 `EthHost::call()` 递归处理
- `authChecker` 保持 **tx 级**（仅在 `executeViaHost` 入口调用一次）

---

## 14. call() 钩子调用顺序（单一真相表）

| 阶段 | `routeCall()` | `call()` 入口 | `prepareMessage()` | `pin_warm_create` |
|------|---------------|---------------|--------------------|--------------------|
| 顶层 CALL | 地址路由、动态 code 解析 | ✅ 全流程 | 仅 CREATE/CREATE2 | CREATE 时在 `prepareMessage` |
| 嵌套 CALL | 地址路由 | ✅ 全流程 | 仅 CREATE/CREATE2 | 同左 |
| DELEGATECALL | 预编译 target 标记 | ✅ + delegate 门控 | ❌ | ❌ |
| CREATE/CREATE2 | `recipient`/`code_address` 对齐 | ✅ 全流程 | ✅ auth 表、nonce | ✅ `State::pin_warm_create_address` |

**规则：**

1. `routeCall()` **不再**调用 `onCreateFrameEntry` / `prepareMessage`（避免 double-prepare）
2. `prepareMessage()` **仅在** `call()` 内、precompile dispatch 之后、`transferValue` 之前调用
3. tx 入口 warm set（origin、callee、precompile、access list）**仅在** `warmTransactionEntry`（tx 级），不在 `call()` 内重复
4. 子帧 `revert()` 回滚 warm journal，但 pinned CREATE 地址保持 warm（与现有 `State` 语义一致）

---

## 15. 目标架构图（终态）

```
          ┌─────────────────────────────────┐
          │   Transaction 编排层             │
          │ executeViaHost / transition     │
          │ (每 tx 一次)                     │
          └──────────────┬──────────────────┘
                         │ 顶层 vm.execute
          ┌──────────────▼──────────────────┐
          │      EthHost::call()             │
          │   (每 CALL/CREATE 递归)          │
          │ ┌ tryChainPrecompile (bcos/op) │
          │ ├ EthPrecompiles (eth)          │
          │ ├ prepareMessage (bcos/op)      │
          │ ├ checkpoint → vm.execute       │
          │ └ access_account/storage        │
          └──────────────┬──────────────────┘
                         │
          ┌──────────────▼──────────────────┐
          │   State (eth)                    │
          │ account / storage / warm / journal│
          └──────────────────────────────────┘
```
