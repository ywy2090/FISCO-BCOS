# bcos-evm 方案 C Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 自研 `bcos-evm/eth/state`（语义参考 evmone v0.21.0 `test/state/`，**不引入 test 源码**），以 `EthHost` 替换 `HostContext`：FISCO 生产走 `executeViaHost`（3.18.0 对齐），`eth/` 向量走 `transition()`（geth 对齐，**不比 state/receipt root**，Q20）。

**Architecture:** 自研 `State` journal + `EthHost`（8 扩展点 / `HostExtension`+`FiscoHostExtension`）；`FiscoStateView` 每次 `syncWait` 穿透 storage；仅 `EVMC_SUCCESS` 时 `co_await applyStateDiff`（D-A/W-C）；ETH 预编译 crypto 复用 `evmone::precompiles` 生产库；C4 删 HostContext/eip2929/BuiltinPrecompiles。范围不含 `bcos-executor` 旧 Host（Scope-A）；写集/DAG 不在本 spec（§15.2）。

**Tech Stack:** C++20, evmone 0.21.0（fork `ywy2090/evmone`，保留 `hash_fn`/`isSMCrypto`），evmc, intx, bcos-task, bcos-framework (EVMAccount, RollbackableStorage)

**Spec:** `docs/superpowers/specs/2026-06-18-bcos-evm-plan-c.md`（§14 Grill + §15–§23 补遗）

**审查注记（2026-06-18）：** 本计划经三路 sub-agent 审查后修订。构建目录以本地 out-of-tree 为准（如 `build/`）；**测试统一放在 `bcos-evm/test/`**（决策 A：`bcos-evm` 可独立 `ctest`）；`bcos/*.cpp` 新文件需显式加入 `bcos-evm/CMakeLists.txt`；compat 回归需区分 `bcos-executor/.../compat` 与 `transaction-executor/tests`。

## 实施分解（spec §23.2）

```text
Plan 1 (C0)  自研 eth/state + EthHost 骨架
    ↓
Plan 2 (C1)  FiscoStateView + 去 executor 依赖    ∥    Plan 3 (C2)  eth 向量门禁
    ↓
Plan 4 (C3 → C4 → C5)  生产路径回归 → 删旧 Host → TransactionExecutorImpl 接线
```

---

## Global Constraints

- `bcos-evm/eth/**` 不得 `#include "bcos-executor`（C1：gas 相关清零；**C4 后全量零**）
- **禁止** vendor / 拷贝 evmone `test/state/*` 或 `test/utils/*` 进生产构建（§18）
- FISCO 生产**不得**调用 `transition()` 余额段（Grill #1）
- FISCO 差异仅经 `HostExtension`/`FiscoHostExtension` + `EthHost` 8 扩展点（§7.1）；Hook#7 动态预编译为 **EthHost 内建**
- 继续用 **fork 版** evmone/evmc（`evmc_host_context.hash_fn` / `isSMCrypto`，§16）
- `applyStateDiff` 仅 `EVMC_SUCCESS`（D-A），且在 `refundGas`/`consumeBalance` 之前（W-C）
- `EthHost::set_storage` 复刻 FISCO `sstoreStatus` 4态/2态，**禁用** evmone 9 态 EIP-2200（§20.2）
- revert logs：**先** `take_logs()` 转存 `FiscoExecutionContext`，**再** 丢 State；顶层 `fix_revert_logs` 门控保持 `makeReceipt`（§19）
- eth 向量：比对 status/gas/logs/output，**不比** state/receipt root（Q20）
- 3.18.0 验收：同 tx + 同 feature flag → 同 receipt/status/gas（含 L-A 双 gas 路径 + 国密链）

---

# Plan 1 — C0：自研 eth/state + EthHost 骨架

### Task C0-1: HostExtension / EthHostExtension 接口

**Files:**
- Create: `bcos-evm/eth/policy/HostExtension.h`
- Create: `bcos-evm/eth/policy/EthHostExtension.h`（eth 向量默认，全默认 true/nullopt）
- Modify: `bcos-evm/CMakeLists.txt`（若需安装头路径）

**Interfaces（spec §5.3）：**
- `HostExtension`：4 虚方法（selfdestruct / delegatecall-precompile / skipValueTransfer / callFiscoPrecompile）
- `EthHostExtension`：eth 向量用，无 FISCO override
- **`FiscoHostExtension` 不在此 task**（放 C3-1 `bcos/`，避免双定义）

- [x] **Step 1:** 实现 `HostExtension.h` + `EthHostExtension.h`
- [x] **Step 2:** 编译 `bcos-evm` PASS（`cmake --build <build-dir> --target bcos-evm`）

---

### Task C0-2a: `State` journal + transient + EIP-2929 access tracking

**Files:** `bcos-evm/eth/state/State.hpp/.cpp`, `StateView.hpp`, `StateDiff.hpp`, `Account.hpp`

- [ ] **Step 1:** `State` journal + revert 语义（T-A transient 内置）
- [ ] **Step 2:** EIP-2929 access/warm 状态机（替代将删的 `eip2929/*`，§15.5 #14）
- [ ] **Step 3:** 单测 — journal revert 丢弃写入

### Task C0-2b: `EthHost` evmc 回调 + 8 扩展点

**Files:** `bcos-evm/eth/state/EthHost.hpp/.cpp`, `hash_utils.hpp`

- [ ] **Step 1:** 实现 evmc Host 回调 + §7.1 扩展点 1–8（Hook#7 内建；Hook#8 调 `FiscoHostExtension`，C3 注入）
- [ ] **Step 2:** 单测 — selfdestruct/DELEGATECALL-precompile/skipValueTransfer 钩子

### Task C0-2c: `EthPrecompiles` + `transition()` 骨架

**Files:** `EthPrecompiles.hpp/.cpp`, `transition.hpp/.cpp`, `Transaction.hpp`, `BlockInfo.hpp`, `bloom_filter.*`, `errors.hpp`

- [ ] **Step 1:** `EthPrecompiles` 0x01–0x11（调用 `evmone_precompiles/*.hpp` 头 API，非虚构 namespace）
- [ ] **Step 2:** `transition()` 骨架（eth 向量，标准 SSTORE 语义；FISCO 4态在 C1-2）
- [ ] **Step 3:** 全部 `.cpp` 加入 `bcos-evm/CMakeLists.txt`；`cmake --build <build-dir> --target bcos-evm` PASS

### Task C0-2（原 mega-task，已拆为 2a/2b/2c）

**参考蓝本（只读对照，不纳入构建）：** evmone v0.21.0 @ `ywy2090/evmone` `test/state/*`

---

### Task C0-3: portfile + fork evmc 验收

**Files:**
- Modify: `ports/evmone/portfile.cmake`（增装 `keccak.hpp`, `secp256k1.hpp`，§18.3）

- [ ] **Step 1:** portfile 增装两个头并验证 vcpkg 安装
- [ ] **Step 2:** 确认 `evmc_host_context` 仍含 `hash_fn`/`isSMCrypto`（§16）
- [ ] **Step 3:** 文档注释：`eth/state` 语义基准 = evmone tag，定期 diff

---

### Task C0-4: 测试脚手架 + InMemoryStateView smoke

**Files:**
- Create: `bcos-evm/test/CMakeLists.txt` + `bcos-evm/test/state/InMemoryStateView.h`
- Create: `bcos-evm/test/state/StateHostSmokeTest.cpp`
- Modify: `bcos-evm/CMakeLists.txt`（`if(TESTS) add_subdirectory(test)`）

- [x] **Step 1:** 建立 gtest target `test-bcos-evm-state`
- [x] **Step 2:** 失败测试 — 空账户 CALL → `EVMC_SUCCESS`（`transition()` + `InMemoryStateView`）
- [x] **Step 3:** 实现 `InMemoryStateView`；`ctest -R StateHostSmokeTest` PASS

**C0 验收：** `bcos-evm` 独立编译；§7.1 八扩展点逐条 checklist；无 `test/` 源码进生产构建。

---

# Plan 2 — C1：FiscoStateView + 去 executor 依赖

> 可与 Plan 3 (C2) **并行**（均仅依赖 C0）。**建议 C1-0（T2 gas 迁移）尽早完成**（spec §17.4）。

### Task C1-0: T2 — gas 符号迁出 `bcos-executor`（前置）

**Files:**
- Create: `bcos-evm/eth/gas/Eip7623.h`, `bcos-evm/eth/AccessList.h`
- Modify: `bcos-evm/eth/gas/EthTxGasSettlement.h`, `bcos-executor/src/Common.h`, `bcos-executor/src/CallParameters.h`（`using` 回兼）
- Modify: `bcos-evm/eth/precompiled/ModexpGas.cpp`（删冗余 include；必要时 `#include "bcos-utilities/DataConvertUtility.h"`）

- [x] **Step 1:** 抽取 `Eip7623Components`/`calcEip7623Components`/`Eip2930AccessList` 到 `bcos-evm`
- [x] **Step 2:** executor 侧 `using` 别名，零调用点改动
- [x] **Step 3:** `rg '#include "bcos-executor' bcos-evm/eth` → 仅剩 `HostContext`/`eip2929`（待 C4）

### Task C1-1: FiscoStateView + StateDiffApplier + FiscoBlockInfo

**Files:**
- Create: `bcos-evm/bcos/FiscoStateView.h/.cpp`, `StateDiffApplier.h/.cpp`, `FiscoBlockInfo.h`
- Modify: `bcos-evm/CMakeLists.txt`（**新增 bcos/*.cpp 到 bcos-evm target**）
- Test: `transaction-executor/tests/FiscoStateViewTest.cpp`

**要点：**
- 每次 `get_*` → `task::syncWait` 穿透 `RollbackableStorage`
- **SM3 codeHash** + **ABI 存储**（§16、§15.5 行6）
- **`FiscoBlockInfo`**：coinbase/timestamp/chainId/gasPrice/blockHash、`convertTimestamp`（§6、§15.5 #18）
- `applyStateDiff`：仅 SUCCESS 写回（D-A）

- [ ] **Step 1:** 失败测试 — 读 nonce/balance/code/codeHash(SM3)/ABI
- [ ] **Step 2:** 实现 `FiscoStateView` + `StateDiffApplier` + `FiscoBlockInfo`
- [ ] **Step 3:** `StateDiffApplier` 单测 — balance/storage 写回
- [ ] **Step 4:** 测试 PASS

### Task C1-2: `EthHost::set_storage` — FISCO sstoreStatus

**Files:**
- Modify: `bcos-evm/eth/state/EthHost.hpp/.cpp`
- Test: `bcos-evm/test/state/SstoreStatusTest.cpp`

- [ ] **Step 1:** 复刻 `HostContext::sstoreStatus`（`fix_storage_status` ON→4态，OFF→2态）
- [ ] **Step 2:** DIRECT 读（bypass journal，§20.2）
- [ ] **Step 3:** 单测 — 4×2 矩阵 PASS

### Task C1-3: （已合并至 C1-0）

**C1 验收：** FISCO storage 读写；gas 相关 `eth/**` 零 `bcos-executor` include。

---

# Plan 3 — C2：eth 向量门禁

> 可与 Plan 2 (C1) **并行**。

### Task C2-1: `warmTransactionEntry` + BlockInfo 构建

**Files:**
- Create: `bcos-evm/eth/execution/warmTransactionEntry.h`
- Create: `bcos-evm/eth/execution/BlockInfoBuilder.h`

- [ ] **Step 1:** 实现 `warmTransactionEntry`（sender/to/access_list/coinbase，E-C）
- [ ] **Step 2:** 单测 — access list 预热与 gas 一致

---

### Task C2-2: Prague/Osaka 向量（不比 root，Q20）

**Files:**
- Create: `bcos-evm/test/state/PragueStateTest.cpp`
- Create: `bcos-evm/test/fixtures/state/`（JSON 向量，来源 geth/evmone general state tests）
- 按需: `system_contracts` / `requests` / `ethash_difficulty` 最小实现（§18.1）

**验收标准：** status / gas_used / logs / output vs geth；**不比** root。

- [ ] **Step 0:** 选定首批 5 个 Prague case 名称 + 夹具目录结构
- [ ] **Step 1:** Prague general state tests（CALL/CREATE/SELFDESTRUCT）PASS
- [ ] **Step 2:** Osaka 向量 wave 2（或记录 defer 理由，spec §11 允许分期）
- [ ] **Step 3:** `ctest -R 'PragueStateTest|StateHostSmokeTest'` PASS

**C2 验收：** eth/ 门禁绿灯。

---

# Plan 4 — C3 → C4 → C5

### Task C3-1: FiscoHostExtension + FiscoExecutionContext

**Files:**
- Create: `bcos-evm/bcos/FiscoHostExtension.h/.cpp`（**仅此一处**，含 `onCreateFrameEntry`）
- Create: `bcos-evm/bcos/FiscoExecutionContext.h`
- Modify: `bcos-evm/CMakeLists.txt`

**依赖说明：** `callFiscoPrecompile` 接 **transaction-executor** 侧 `PrecompiledManager`（`precompiled/PrecompiledManager.cpp`）；`createAuthTable` 经 `AuthCheck.h`/`ExecutiveWrapper`（`bcos/` 层仍绑 executor，Scope-A 内可接受）。

- [ ] **Step 1:** `FiscoHostExtension` 四钩子 + `onCreateFrameEntry`（nonce + `createAuthTable`，FIB-82）
- [ ] **Step 2:** 单测 — CREATE revert 后权限表回滚（§21.4 journal）；确认写通道进 `State` 回滚域
- [ ] **Step 3:** `FiscoExecutionContext`：message、revisionConfig、logs、gasSettlementSnapshot

---

### Task C3-2: `executeViaHost` 编排层

**Files:**
- Create: `bcos-evm/bcos/ExecuteViaHost.h/.cpp`
- Create: `bcos-evm/bcos/FiscoTxAdapter.h`（`deriveMessage`，CR-A）
- Create: `bcos-evm/bcos/FiscoTransactionPrepare.h`（Prep-A）

**编排顺序（spec §8，目标架构）：**
```cpp
deriveMessage(msg);                              // CR-A
if (auto r = checkAuth(...)) return r;         // Auth-A
debitEip7623Calldata(msg, rev);                // Pre-A
co_await transferBalanceIfEnabled(...);          // Pre-A
consumeTransferGas(msg);
warmTransactionEntry(host, ...);               // E-C
// fork evmc: 填充 hash_fn / isSMCrypto（§16）
auto result = task::syncWait([&]{ State s(view); EthHost host(s, ext); return host.call(msg); });
ctx.logs = convert(host.take_logs());          // §19：先于 State 丢弃
return {result, state.build_diff(rev)};
```

- [ ] **Step 1:** 实现 `executeViaHost`（含 try/catch → `EVMCResult`，`fix_error_handling` 门控，§20.1）
- [ ] **Step 2:** 实现 `FiscoTransactionPrepare` + **Prep-A 单测**（Prepare 触达 ReadWriteSetStorage 读集）
- [ ] **Step 3:** 单测 — 空账户 CALL、auth 失败、CREATE + createAuthTable、嵌套 CREATE Hook#2、Hook#7 动态预编译
- [ ] **Step 4:** 国密链 smoke（SM3 CREATE/codeHash/SHA3 opcode）

**注意：** 此阶段 **不** 切换 `TransactionExecutorImpl`（留给 C5）；通过单测 / harness 验证 `executeViaHost`。

---

### Task C3-3: 3.18.0 回归（executeViaHost harness）

**Files:**
- Create: `transaction-executor/tests/ExecuteViaHostCompatTest.cpp`（从 `transaction-executor/tests/CompatHostContextTest.cpp` 迁移）
- 参考: `bcos-executor/test/unittest/evmone/compat/CompatHostContextHarness.h`（executor 侧 compat **不在本期 Scope**，仅借 harness 模式）

- [ ] **Step 1:** feature flag 矩阵 + `fix_error_handling` 逐 FIB 用例（§20.1）
- [ ] **Step 2:** revert logs — flag OFF/ON（§19.4）
- [ ] **Step 3:** L-A 双路径（precheck ON: buyGas/refundGas；OFF: consumeBalance）
- [ ] **Step 4:** 与 `release-3.18.0` 快照 / 现有 TE 测试对比 PASS

**C3 验收：** `executeViaHost` 路径与 3.18.0 一致（含国密 + L-A）。

---

### Task C4-1: 删除遗留 HostContext 路径

**Files:**
- Delete: `bcos-evm/eth/vm/HostContext.h`, `eth/HostContextPolicy.h`, `eth/eip2929/*`, `PrecompiledRegistrar.*`, `BuiltinPrecompiles.*`
- Delete: `transaction-executor/bcos-transaction-executor/vm/HostContext.h`（转发头）
- Modify: `transaction-executor/**` 去除 HostContext 引用
- **不删:** `bcos-executor/src/vm/HostContext.h`（Scope-A / C7+）

- [ ] **Step 1:** `rg 'HostContext|PrecompiledRegistrar|eip2929|BuiltinPrecompiles' bcos-evm transaction-executor` → 无命中
- [ ] **Step 2:** `rg '#include "bcos-executor' bcos-evm/eth` → **0**
- [ ] **Step 3:** 全量编译 + `ctest` PASS

**C4 验收：** FISCO 自维护 Host 代码净减；`eth/**` 零 `bcos-executor` include。

---

### Task C5-1: TransactionExecutorImpl 接线 + scheduler CI

**Files:**
- Modify: `transaction-executor/bcos-transaction-executor/TransactionExecutorImpl.h`
- Modify: `bcos-evm/bcos/FiscoTxExecutor.h`

- [ ] **Step 1:** `Data::m_hostContext` → `FiscoExecutionContext`；删 `m_transientStorage`（T-A）
- [ ] **Step 2:** **Prepare** 阶段接 `FiscoTransactionPrepare`（Prep-A，触达 ReadWriteSetStorage）
- [ ] **Step 3:** Execute 接线 `executeViaHost` + `applyStateDiff` + L-A 双路径；**保留** `updateNonce()` 在 Execute 前（现状顺序）

```cpp
// precheck ON:  buyGas → executeViaHost → (SUCCESS) applyStateDiff → refundGas
// precheck OFF: executeViaHost → (SUCCESS) applyStateDiff → consumeBalance
```

- [ ] **Step 4:** Finalize：`makeReceipt` 保持 `fix_revert_logs` 门控（§19）
- [ ] **Step 5:** C3 回归复跑 PASS
- [ ] **Step 6:** `transaction-scheduler` Prepare 读集测试 PASS：

```bash
ctest -R 'FIB101_102_103_104_SchedulerTest' -V
# 或 transaction-scheduler/tests 下等价 target
```

**C5 验收：** `TransactionExecutorImpl` 全切换；scheduler Prepare 绿灯。**不含** DAG 写集传播（§15.2）。

---

## Self-Review（spec §14 Grill + 补遗覆盖）

| Grill # | 决策 | Task |
|---------|------|------|
| 1 | FISCO 不用 transition 余额 | C3-2 |
| 2 | StateView 穿透 storage | C1-1 |
| 3–4 | W-C + D-A applyStateDiff | C1-1, C5-1 |
| 5 | T-A transient | C0-2a, C5-1 |
| 6 | L-A consumeBalance | C5-1 |
| 7 | E-C warmTransactionEntry | C2-1, C3-2 |
| 8 | P-A 自研分发 + evmone::precompiles | C0-2c |
| 9 | CR-A deriveMessage + Hook#2 | C3-2 |
| 10 | Prep-A | C3-2, C5-1 |
| 11 | Auth-A checkAuth + CREATE 建表 | C3-1, C3-2 |
| 12 | Pre-A | C3-2 |
| 13 | Log-B + §19 转存顺序 | C3-1, C3-2, C5-1 |
| 14 | Scope-A | （不建 bcos-executor Host task） |
| 15 | EthGate-A | C2-2 |
| 16–17 | SD-A, DC-A | C0-2b |
| 18 | Exec-A | C3-2, C5-1 |
| 19 | 自研 EthHost（非 vendor patch） | C0-2a/b/c |
| 20 | 不比 root | C2-2 |
| §16 | SM3 三路 | C0-3, C1-1, C3-2 |
| §20.2 | FISCO sstoreStatus | C1-2 |
| §21 | createAuthTable Hook#8 | C3-1 |
| §17 T2 | gas 去 executor | C1-0 |
| FiscoBlockInfo | tx_context / E-C | C1-1 |

---

## 执行选项

计划已保存至 `docs/superpowers/plans/2026-06-18-bcos-evm-plan-c.md`。

**1. Subagent-Driven（推荐）** — 每个 Task 派生子 agent，任务间 review  
**2. Inline Execution** — 本会话按 Task 顺序直接实施，checkpoint 汇报

默认从 **Plan 1 / Task C0-1** 开始。

---

## Plan 审查汇总（2026-06-18，三路 sub-agent）

| 维度 | 结论 | 已修订项 |
|------|------|----------|
| Plan vs Spec | **有条件 PASS** | +FiscoBlockInfo、C5 Prepare、Q20、FiscoHostExtension 单路径 |
| 任务可执行性 | **C0/C1 可启动** | C0-2 拆 2a/2b/2c；补测试脚手架；C1-0 前置 T2 |
| 代码库可行性 | **需留意 CMake/测试** | bcos/*.cpp 接线；compat 分层；transaction-scheduler 测试路径 |

**仍须在执行中关注：** `bcos/` 层对 `bcos-executor` 的 auth/precompile 桥接（Scope-A 内保留）；C2 向量夹具需 Step 0 选型；Osaka 可 wave 2。
