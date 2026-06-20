# Task 4 — OPStack Deposit Tx 审计笔记（含失败路径）

**日期：** 2026-06-20  
**范围：** matrix 行 `OPStack deposit tx`；`opStackExecuteViaHost` deposit 分支、precheck、失败 nonce/gas  
**参考：** op-geth `e8800cffe` — `core/types/deposit_tx.go`, `core/state_transition.go:346–361,474–511,601–688`  
**Specs：** ethereum-optimism-specs `689a96f` — `specs/protocol/deposits.md`（Execution / Nonce Handling / Deposit Receipt）

---

## Step 1: Spec + op-geth MUST（deposit 语义基线）

| 规则 | optimism specs | op-geth |
|------|----------------|---------|
| 类型 `0x7E`；字段 sourceHash/from/to/mint/value/gas/isSystemTx/data | `deposits.md` §The Deposited Transaction Type | `deposit_tx.go:27–46` |
| 无 nonce / 无 fee 字段；gasPrice=0 | 同 § | `gasFeeCap/gasTipCap/gasPrice` 恒零（`:75–77,84–86`） |
| 执行前先 mint（无条件，失败也不回滚 mint） | §Execution 第 1 步 | `execute():474–479` mint 在 snapshot 前 |
| 跳过 nonce / fee / EOA 校验 | §Execution bullet list | `preCheck():347–360` |
| 非 system deposit 占用 block gas pool | §Execution 末段 | `gp.SubGas(GasLimit)`（`:360`） |
| Regolith+ system tx 拒绝 | § + `isSystemTx` 脚注 | `ErrSystemTxNotSupported`（`:354–356`） |
| 无 L1 fee / base fee / coinbase tip / operator fee | §Execution | `!st.msg.IsDepositTx` 分支才路由 fee（`:713+`） |
| Regolith+ 成功 deposit：receipt 用**实际** gasUsed | §Deposit Receipt | `innerExecute()` Regolith 分支（`:681–688`） |
| **非 EVM** 状态转换错误：mint 后 rollback → nonce+1 → gasUsed=gasLimit | §Execution 177–184 | `execute()` 失败包装（`:486–510`） |
| Regolith+ EVM REVERT：nonce 仍递增（CALL 前 bump），gasUsed=**实际** | §Nonce Handling + Regolith receipt 变更 | `SetNonce` 在 Call 前（`:601–602`）；Regolith deposit 返回 `st.gasUsed()`（`:681–688`） |
| `depositNonce` = EVM 处理**前** sender nonce | §Deposit Receipt（Canyon+） | receipt 扩展；FB 在 mint 前捕获 |

**Isthmus 审计锚点：** Isthmus 继承 Regolith deposit 语义（actual gas on success/revert-EVM；非 EVM 错误仍 gasLimit）。op-geth 无 Isthmus 专用 deposit 分支。

---

## Step 2: FB deposit 成功路径

### 实现链（`OpStackExecuteViaHost.cpp:122–170`）

```
depositNonce = GetNonce(from)          // :124，mint 前 ✅
mint > 0 → AddBalance(from)            // :125–130 ✅
checkpoint()                           // :132
executeEntryChecks()                   // :133
executeMessage(gasPrice=0, ext=OpHost) // :141–154
SUCCESS → postExecuteGasSettlement     // :161–168 ✅ 实际 gas
       → commit()                      // :169
       → 无 fee routing（deposit 分支不走 buyGas/refundGas）✅
```

### 结构体（`OpStackDepositTx.h`）

字段与 `deposit_tx.go:DepositTx` 一一对应（`sourceHash/from/to/mint/value/gas/isSystemTransaction/data`）。解码/传播在 executor 层（本 Task 范围外）。

### 对照 op-geth 成功路径

| 项 | FB | op-geth | 一致 |
|----|----|---------|------|
| mint 在 checkpoint 前 | ✅ | ✅ | ✅ |
| gasPrice=0 | ✅ `:144` | ✅ | ✅ |
| 跳过 buyGas / refundGas / L1&operator fee | ✅（`:26–28,112–114` in `OpStackTxExecutor.cpp`） | ✅ | ✅ |
| 成功 gasUsed = 实际 | ✅ `postExecuteGasSettlement` | ✅ Regolith+ `:681–688` | ✅ |
| **sender nonce +1** | ❌ 成功路径无 bump | ✅ `innerExecute():601–602` Call 前 | 🔴 |
| depositNonce 捕获时机 | ✅ mint 前 | ✅ EVM 前 | ✅ |

### 测试（成功路径）

| 测试 | 断言 | 判定 |
|------|------|------|
| `DepositMintTest::deposit_mint_is_applied_before_execution` | SUCCESS；stateDiff balance=100 | ✅ mint |
| `DepositNoFeeRoutingTest::deposit_skips_fee_routing_recipients` | fee recipient balance=0 | ✅ 无 fee 路由 |
| 上述两例 | **未断言** nonce / depositNonce / gasUsed 数值 | 🟡 成功 nonce 缺测 |

---

## Step 3: FB deposit 失败路径（深审 — 决策 A）

### FB 实现（`OpStackExecuteViaHost.cpp:171–178`）

```cpp
state.revert();                              // 回滚 checkpoint 后执行变更；mint 保留 ✅
state.set_nonce(sender, nonce + 1);          // 显式 bump ✅
txData.m_gasUsed = gasLimit;                 // 恒 gasLimit ⚠️
```

触发条件：`output.evmcResult.status_code != EVMC_SUCCESS`（含 `EVMC_REVERT`、`EVMC_OUT_OF_GAS` 等所有非 SUCCESS）。

### op-geth 双轨失败语义

| 失败类型 | op-geth 路径 | nonce | gasUsed（Regolith+/Isthmus） |
|----------|--------------|-------|------------------------------|
| **非 EVM** 错误（如 value 转账不足、intrinsic/floor gas 不足） | `innerExecute` 返回 `err` → `execute():486–510` | Revert 后 `SetNonce+1` | **gasLimit** |
| **EVM REVERT**（合约 REVERT/INVALID 等） | `innerExecute` 正常返回，`vmerr!=nil` | Call 前已 `SetNonce+1`（`:602`，REVERT 不 rollback） | **实际 gas**（`:681–688`） |
| preCheck 失败（gas pool / system tx） | 不进 `execute()` | 不适用 | 不适用 |

### FB vs op-geth 失败路径对照

| 场景 | FB | op-geth (Isthmus) | 严重度 |
|------|----|--------------------|--------|
| EVM REVERT（如目标合约 `REVERT`） | revert + nonce+1 + **gasLimit** | nonce+1 + **actual gas** | 🔴 gas 记账 |
| executeEntryChecks 失败（OOG intrinsic / floor / insufficient value） | revert；**无 nonce bump**；`gasUsed=0`；提前 return（`:133–138`） | mint 保留 + nonce+1 + gasLimit | 🔴 |
| mint 保留 | ✅ | ✅ | ✅ |
| 失败仍 inclusion（返回 stateDiff 而非 throw） | ✅ | ✅ | ✅ |

**结论（失败路径 vs op-geth）：**

- **nonce（EVM REVERT 路径）：** ✅ 与 op-geth 最终状态一致（FB 显式 bump；geth Call 前 bump）。
- **gasUsed（EVM REVERT @ Isthmus）：** 🔴 FB 恒 `gasLimit`，op-geth 报实际 gas — 违反 specs Regolith+ receipt/cumulativeGasUsed 语义。
- **executeEntryChecks 失败：** 🔴 两处偏离（无 nonce bump、gasUsed=0 而非 gasLimit）。
- **成功路径 nonce：** 🔴 见 Step 2。

---

## Step 4: Precheck（`OpStackPreCheck.cpp`）

### FB deposit 分支（`:40–50`）

| 检查 | FB | op-geth `preCheck():347–360` |
|------|----|------------------------------|
| system tx | 恒 `Malformed` | Regolith+ `ErrSystemTxNotSupported`；pre-Regolith 允许 |
| nonce / fee / EOA / blob / 7702 | 跳过 | 跳过 |
| gas pool | `gasPoolSubGasHook(gasLimit)` | `gp.SubGas(GasLimit)` |
| 返回值 | 通过 → nullopt | 设置 `initialGas/gasRemaining` |

**Isthmus 判定：** system tx 拒绝 ✅（Isthmus ≫ Regolith）。gas pool hook ✅。无 🔴。

**🟡：** FB 不区分 pre-Regolith system tx 历史行为（Isthmus 范围可忽略）。

### 测试（`DepositTxPreCheckTest.cpp`）

| 用例 | 覆盖 |
|------|------|
| `system_deposit_is_rejected` | system tx → Malformed ✅ |
| `deposit_skips_nonce_and_fee_checks_but_still_subtracts_gas_pool` | 错误 nonce/fee 仍通过 + hook 调用 ✅ |
| 非 deposit 对照用例 ×4 | 边界隔离 ✅ |

---

## Part 1 — 合规矩阵行

| 能力 | 层级 | 清单来源 | Matrix 状态 | 审计深度 | 状态 | Spec 依据 | FB 实现 | op-geth 对照 | FB 测试 | 缺口 |
|------|------|----------|-------------|----------|------|-----------|---------|--------------|---------|------|
| OPStack deposit tx | orchestration | matrix:64 | explicit | 深审 | 🟡 **DONE_WITH_CONCERNS** | `deposits.md` §Execution, §Nonce, §Deposit Receipt | `OpStackExecuteViaHost.cpp:122–181`；`OpStackPreCheck.cpp:40–50`；`OpStackDepositTx.h` | mint/fee 豁免/pool/regolith gas(success) 对齐；**失败 REVERT gasLimit 🔴**；**成功/entry 失败 nonce 🔴** | `DepositTxPreCheckTest`, `DepositMintTest`, `DepositNoFeeRoutingTest` | 成功 nonce；entry 失败；Isthmus REVERT 实际 gas；depositNonce on failure |

**Matrix Test ref 建议增补：** `DepositNoFeeRoutingTest`（含 `deposit_failure_*`）。

---

## Part 2 — 偏离项详情

### D4-1 🔴 成功 deposit 未递增 sender nonce

- **现象：** 成功路径仅 `state.commit()`（`:169`），无 `set_nonce+1`。`executeMessage` 顶层 CALL 不 bump nonce（与 geth `innerExecute:601–602` 不同）。
- **规范：** `deposits.md` §Nonce Handling — 每次 deposit 执行应递增 `from` nonce。
- **金标准：** `state_transition.go:601–602`（Call 前 `SetNonce`）。
- **修复建议：** 在 deposit 成功 `commit()` 前 `set_nonce(sender, nonce+1)`；或于 `executeMessage` deposit 专用路径对齐 geth 时机（须与 failure bump 去重）。
- **补测：** 成功 deposit nonce N→N+1；连续 deposit 的 `depositNonce` 递增。

### D4-2 🔴 Isthmus/Regolith+ EVM REVERT 仍记 gasUsed=gasLimit

- **现象：** `:176–177` 对所有 `status != SUCCESS` 设 `gasUsed = gasLimit`。op-geth Regolith+ 对 EVM revert 返回 `st.gasUsed()`（`:681–688`）。
- **规范：** `deposits.md` §Deposit Receipt — Regolith 起 cumulativeGasUsed 反映 EVM 实际 metered gas。
- **金标准：** `state_transition.go:681–688`（`vmerr` 仍用 actual gas）；对比非 EVM 包装 `:498`（gasLimit）。
- **修复建议：** 失败分支区分 EVM 错误 vs 非 EVM/entry 错误；EVM revert 走 `postExecuteGasSettlement` 或等价 `gasLimit - gas_left`。
- **补测：** REVERT fixture 断言 `gasUsed < gasLimit` 且与 op-geth 同值；非 EVM 错误仍 `gasUsed=gasLimit`。

### D4-3 🔴 executeEntryChecks 失败未 bump nonce 且 gasUsed=0

- **现象：** `:133–138` entry 失败：`revert()` 后直接 return；无 nonce++；`output.gasUsed` 默认 0。
- **规范：** specs §Execution 177–184（非 EVM 错误等价 native failure：rollback + nonce+1）。
- **金标准：** `state_transition.go:486–510`（非 EVM deposit 失败包装）。
- **修复建议：** entry 失败复用 `:171–178` 逻辑（revert → nonce+1 → gasLimit），或统一 fall-through 到失败 settlement。
- **补测：** intrinsic OOG / insufficient value（有 mint、value>balance）deposit 用例。

### D4-4 🟡 失败路径测试断言与 Isthmus 金标准不一致

- **现象：** `DepositNoFeeRoutingTest::deposit_failure_reverts_execution_but_keeps_mint_and_bumps_nonce` 断言 `gasUsed == 50'000`（gasLimit），编码了 D4-2 行为而非 op-geth Regolith+。
- **修复建议：** 修正实现后更新断言为 actual gas；补充 `depositNonce` 失败路径断言。

### D4-5 🟡 成功路径 / depositNonce 测试缺口

- **现象：** 三份指定测试均未断言成功 nonce 或 `receiptMeta.depositNonce`。
- **修复建议：** 扩展 `DepositMintTest` 或新增 executor E2E。

---

## Part 3 — 测试断言审计（deposit 失败覆盖）

| 测试文件 | 用例 | 断言状态 | 金标准来源 | 备注 |
|----------|------|----------|------------|------|
| `DepositTxPreCheckTest.cpp` | system / pool / 非 deposit 对照 | ✅ 有效 | `preCheck():347–360` | 不覆盖 execute 失败 |
| `DepositMintTest.cpp` | `deposit_mint_is_applied_before_execution` | 🟡 部分 | mint 必须保留 | 无 nonce/gas/depositNonce |
| `DepositNoFeeRoutingTest.cpp` | `deposit_skips_fee_routing_recipients` | ✅ 有效 | 无 fee 路由 | 成功路径 |
| `DepositNoFeeRoutingTest.cpp` | **`deposit_failure_reverts_execution_but_keeps_mint_and_bumps_nonce`** | 🟡 **假覆盖（gas）** | Regolith+ 应 actual gas；nonce/mint 断言 ✅ | REVERT bytecode `PUSH1 0 PUSH1 0 REVERT`；`gasUsed=gasLimit` 与 op-geth 🔴；未测 depositNonce |
| `L1AttributesDepositFailureTest.cpp` | `failed_l1_attributes_deposit_does_not_commit_slot_changes` | ✅ 有效（Task 5 域） | slot revert | 未断言 nonce/gasUsed；EVM REVERT 同类 |

**目录扫描（deposit 相关，Task 4 范围）：** 5 文件 — 上述 4 + `L1AttributesDepositTest.cpp`（成功域）。

**失败路径覆盖结论：**

| 断言项 | 有独立测试 | 与 op-geth 一致 |
|--------|------------|-----------------|
| mint 失败保留 | ✅ `deposit_failure_*` | ✅ |
| nonce 失败 +1 | ✅ `deposit_failure_*` | ✅（EVM REVERT） |
| gasUsed 失败 | ✅ 有断言 | 🔴 断言 gasLimit（Isthmus 应为 actual） |
| entry 失败 nonce/gas | ❌ 无 | 🔴 |
| 成功 nonce +1 | ❌ 无 | 🔴 |
| depositNonce | ❌ 无 | — |

---

## 汇总

| 子路径 | vs op-geth (Isthmus) | 严重度 |
|--------|----------------------|--------|
| mint / fee 豁免 / gas pool precheck | 一致 | ✅ |
| 成功 gas settlement | 一致 | ✅ |
| **成功 sender nonce** | FB 未 bump | 🔴 |
| **EVM REVERT 失败：nonce** | 一致（最终 +1） | ✅ |
| **EVM REVERT 失败：gasUsed** | FB gasLimit；geth actual | 🔴 |
| **entry/pre-EVM 失败** | FB 无 nonce、gas=0 | 🔴 |
| 失败测试 | 存在但 gas 断言编码偏离 | 🟡 |

**Task 4 状态：** **DONE_WITH_CONCERNS**（2×🔴 失败 gas 语义 + 1×🔴 成功 nonce + 1×🔴 entry 失败；测试 🟡）

**P0 修复顺序：** D4-1（成功 nonce）→ D4-2（区分 EVM vs 非 EVM 失败 gas）→ D4-3（entry 失败包装）→ D4-4（更新测试断言）。
