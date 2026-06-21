# Task 4 — OPStack Deposit Tx 审计笔记（含失败路径）

**日期：** 2026-06-21（复审计 @ `54e17a62c`）  
**初审计：** 2026-06-20 @ `f989f073f` — 🔴 成功 nonce / REVERT gas / entry 失败  
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

### 实现链（`OpStackExecuteViaHost.cpp:124–178`）

```
depositNonce = GetNonce(from)          // :126，mint 前 ✅
mint > 0 → AddBalance(from)            // :127-132 ✅
checkpoint()                           // :134
executeEntryChecks()                   // :135
executeMessage(gasPrice=0, ext=OpHost) // :147-160
SUCCESS → postExecuteGasSettlement     // :167-174 ✅ 实际 gas
       → set_nonce(sender, nonce+1)    // :175-176 ✅ OP-03
       → commit()                      // :177
       → 无 fee routing（deposit 分支不走 buyGas/refundGas）✅
```

### 对照 op-geth 成功路径

| 项 | FB | op-geth | 54e17a62c |
|----|----|---------|-----------|
| mint 在 checkpoint 前 | ✅ | ✅ | ✅ |
| gasPrice=0 | ✅ | ✅ | ✅ |
| 跳过 buyGas / refundGas / L1&operator fee | ✅ | ✅ | ✅ |
| 成功 gasUsed = 实际 | ✅ `postExecuteGasSettlement` | ✅ Regolith+ | ✅ |
| **sender nonce +1** | ~~❌~~ | ✅ Call 前 bump | ✅ **OP-03** `:175-176` |
| depositNonce 捕获时机 | ✅ mint 前 | ✅ EVM 前 | ✅ |

### 测试（成功路径）

| 测试 | 断言 | 判定 |
|------|------|------|
| `DepositMintTest::deposit_mint_is_applied_before_execution` | SUCCESS；balance=100；**nonce 3→4**；depositNonce=3 | ✅ **OP-03 扩展** |
| `DepositNoFeeRoutingTest::deposit_skips_fee_routing_recipients` | fee recipient balance=0 | ✅ 无 fee 路由 |
| `TestOpStackTransactionExecutorFixture::deposit_mint_applied_without_fee_routing` | TE E2E mint + 无 L1 fee | ✅ |
| TE fixture | **未断言** 成功 nonce / depositNonce | 🟡 |

---

## Step 3: FB deposit 失败路径（深审 — 决策 A）

### FB 实现（`OpStackExecuteViaHost.cpp:135-190`）

**Entry 失败（`:135-144`）— OP-05：**
```cpp
state.revert();
state.set_nonce(sender, nonce + 1);
txData.m_gasUsed = gasLimit;
```

**EVM 非 SUCCESS（`:179-190`）— OP-04：**
```cpp
postExecuteGasSettlement(...);  // actual gas
state.revert();
state.set_nonce(sender, nonce + 1);
txData.m_gasUsed = settlement.gasUsed;  // actual, not gasLimit
```

### op-geth 双轨失败语义 vs FB（54e17a62c）

| 场景 | FB | op-geth (Isthmus) | 54e17a62c |
|------|----|--------------------|-----------|
| EVM REVERT | revert + nonce+1 + **actual gas** | nonce+1 + actual gas | ✅ **OP-04** |
| executeEntryChecks 失败 | revert + nonce+1 + **gasLimit** | mint 保留 + nonce+1 + gasLimit | ✅ **OP-05** |
| mint 保留 | ✅ | ✅ | ✅ |
| 失败仍 inclusion | ✅ | ✅ | ✅ |

**结论：** 三条失败/成功 nonce+gas 路径与 op-geth Regolith+/Isthmus 语义对齐 ✅。

---

## Step 4: Precheck（`OpStackPreCheck.cpp`）

### FB deposit 分支（`:40–50`）

| 检查 | FB | op-geth `preCheck():347–360` |
|------|----|------------------------------|
| system tx | 恒 `Malformed` | Regolith+ `ErrSystemTxNotSupported` |
| nonce / fee / EOA / blob / 7702 | 跳过 | 跳过 |
| gas pool | `gasPoolSubGasHook(gasLimit)` | `gp.SubGas(GasLimit)` |

**Isthmus 判定：** system tx 拒绝 ✅。gas pool hook ✅。无 🔴。

**🟡：** FB 不区分 pre-Regolith system tx 历史行为（Isthmus 范围可忽略）。

---

## Part 1 — 合规矩阵行

| 能力 | 层级 | 清单来源 | Matrix 状态 | 审计深度 | 状态 | Spec 依据 | FB 实现 | op-geth 对照 | FB 测试 | 缺口 |
|------|------|----------|-------------|----------|------|-----------|---------|--------------|---------|------|
| OPStack deposit tx | orchestration | matrix:64 | explicit | 深审 | ✅ | `deposits.md` §Execution, §Nonce, §Deposit Receipt | `OpStackExecuteViaHost.cpp:124-193`；`OpStackPreCheck.cpp:40-50` | mint/fee 豁免/pool/nonce/gas 全路径对齐 | `DepositTxPreCheckTest`, `DepositMintTest`, `DepositNoFeeRoutingTest`, TE deposit E2E | 🟡 TE E2E 缺 gasUsed/depositNonce 精确断言 |

---

## Part 2 — 偏离项详情

### ✅ D4-1 成功 deposit 递增 sender nonce（OP-03 闭合）

- **54e17a62c：** `OpStackExecuteViaHost.cpp:175-176` 在 `commit()` 前 `set_nonce(sender, nonce+1)`。
- **测试：** `DepositMintTest` 断言 nonce 3→4 + `depositNonce=3` ✅。

### ✅ D4-2 Isthmus/Regolith+ EVM REVERT → actual gasUsed（OP-04 闭合）

- **54e17a62c：** 失败分支 `:181-189` 经 `postExecuteGasSettlement` 取 actual gas，不再恒 `gasLimit`。
- **测试：** `DepositNoFeeRoutingTest::deposit_failure_reverts_execution_but_keeps_mint_and_bumps_nonce` 断言 `gasUsed=21'000 < gasLimit` ✅。

### ✅ D4-3 executeEntryChecks 失败 bump nonce + gasLimit（OP-05 闭合）

- **54e17a62c：** `:135-144` entry 失败：revert → nonce+1 → `gasUsed=gasLimit`。
- **测试：** `DepositNoFeeRoutingTest::deposit_entry_failure_bumps_nonce_and_uses_gas_limit` ✅。

### ✅ D4-4 失败路径测试已与 op-geth 对齐

- **54e17a62c：** REVERT 用例从编码 gasLimit 行为修正为 actual gas；新增 entry-failure 用例；断言 `depositNonce` ✅。

### D4-5 🟡 TE E2E 测试深度仍不足

| 缺口 | 说明 |
|------|------|
| TE `deposit_failure_reverts_but_keeps_mint` | 断言 nonce + mint ✅；**未断言** `gasUsed` actual vs gasLimit |
| TE 成功 deposit | 未断言 nonce / depositNonce |
| TE entry 失败 | 无独立用例（编排层 `DepositNoFeeRoutingTest` 已覆盖） |

---

## Part 3 — 测试断言审计（deposit 失败覆盖）

| 测试文件 | 用例 | 断言状态 | 金标准来源 | 备注 |
|----------|------|----------|------------|------|
| `DepositTxPreCheckTest.cpp` | system / pool / 非 deposit 对照 | ✅ 有效 | `preCheck():347-360` | 不覆盖 execute 失败 |
| `DepositMintTest.cpp` | `deposit_mint_is_applied_before_execution` | ✅ 有效 | mint + nonce + depositNonce | **OP-03** |
| `DepositNoFeeRoutingTest.cpp` | `deposit_skips_fee_routing_recipients` | ✅ 有效 | 无 fee 路由 | 成功路径 |
| `DepositNoFeeRoutingTest.cpp` | `deposit_failure_reverts_execution_but_keeps_mint_and_bumps_nonce` | ✅ 有效 | Regolith+ actual gas | gasUsed=21000；nonce 7→8；depositNonce=7 **OP-04** |
| `DepositNoFeeRoutingTest.cpp` | `deposit_entry_failure_bumps_nonce_and_uses_gas_limit` | ✅ 有效 | 非 EVM entry 失败 | gasUsed=gasLimit；nonce 5→6 **OP-05** |
| `TestOpStackTransactionExecutorFixture.cpp` | `deposit_failure_reverts_but_keeps_mint` | 🟡 部分 | TE E2E | nonce + mint ✅；无 gasUsed |
| `L1AttributesDepositFailureTest.cpp` | slot revert | ✅ 有效（Task 5 域） | 未断言 nonce/gasUsed |

**失败路径覆盖结论：**

| 断言项 | 有独立测试 | 与 op-geth 一致 |
|--------|------------|-----------------|
| mint 失败保留 | ✅ | ✅ |
| nonce 失败 +1 | ✅ | ✅ |
| gasUsed REVERT actual | ✅ | ✅ |
| entry 失败 nonce/gas | ✅ | ✅ |
| 成功 nonce +1 | ✅ `DepositMintTest` | ✅ |
| depositNonce | ✅ 编排层三用例 | ✅ |
| TE E2E gasUsed/depositNonce | 🟡 弱 | — |

### 测试执行（54e17a62c）

```bash
ctest --test-dir build/bcos-evm/test -R 'DepositMint|DepositNoFee' --output-on-failure
# 2/2 passed (2026-06-21)
```

---

## 汇总

| 子路径 | vs op-geth (Isthmus) | 初审计 | 54e17a62c |
|--------|----------------------|--------|-----------|
| mint / fee 豁免 / gas pool precheck | 一致 | ✅ | ✅ |
| 成功 gas settlement | 一致 | ✅ | ✅ |
| **成功 sender nonce** | FB 未 bump | 🔴 | ✅ |
| **EVM REVERT 失败：nonce** | 一致 | ✅ | ✅ |
| **EVM REVERT 失败：gasUsed** | FB gasLimit | 🔴 | ✅ |
| **entry/pre-EVM 失败** | FB 无 nonce、gas=0 | 🔴 | ✅ |
| 失败测试 | 编码偏离 | 🟡 | ✅ 编排层 |
| TE E2E 深度 | — | 🟡 | 🟡 |

**Task 4 状态：** **PASS** — OP-03/04/05 闭合；deposit 语义与 op-geth Isthmus 对齐。

**剩余 🟡：** TE fixture 未断言 deposit 失败 actual gasUsed / 成功 depositNonce；entry 失败无 TE 独立用例（编排层已覆盖）。

**可裁决计数：** ✅ 6 · 🟡 1 · 🔴 0

---

## Wave 3 复审计附录（@ `52dda0921`）

| 场景 | op-geth | FB | Wave 3 |
|------|---------|-----|--------|
| CALL deposit 成功 nonce | Call 前 bump `:601-602` | 执行后 bump `:175-176` | ✅ `DepositMintTest` 3→4 |
| CREATE deposit 成功 nonce | `evm.Create` 内 +1 `:530` | 执行后无条件 +1 `:175-176` | 🟡 **R3-DEP-1** — 缺 CREATE 用例 |
| REVERT / entry 失败 | Regolith+ | 编排层 | ✅ 无回归 |
| system tx 错误码 | `ErrSystemTxNotSupported` | `Malformed` | 🟡 语义等价 |

**Wave 3 Task 4 判定：** ✅ CALL 路径 PASS；🟡 CREATE nonce 待测。
