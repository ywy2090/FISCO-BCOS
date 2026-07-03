# bcos-evm/eth vs go-ethereum 标准 EVM Parity 报告

**状态：** Phase 1–15 初稿 + Round 1/2 复核 + **2026-07-01 修复同步**（[6.5] DELEGATECALL value ✅、[2.1] nonce ✅）+ **ADR-028 reject 簇已标注暂缓（Gap 39）**  
**提示词：** [2026-07-01-eth-vs-geth-diff-prompt.md](../../superpowers/plans/2026-07-01-eth-vs-geth-diff-prompt.md)  
**日期：** 2026-07-01  
**范围：** 可移植 ETH 内核（`bcos-evm/eth/`）+ TE 编排（`EthTransactionExecutorImpl`），不含 `opstack/` / `bcos/` 链扩展

---

## 元信息

| 项 | 值 |
|----|-----|
| go-ethereum | **v1.17.3** @ `117e067f0f0bae1a17082321f224dedb6765b10f` |
| go-ethereum 路径 | `/Users/octopus/octo/code/blockchain-impl/go-ethereum` |
| bcos worktree | `feat-evm-refactor` |
| bcos eth 内核 | `bcos-evm/eth/` |
| 审计 revision 基线 | Cancun / Prague / Osaka（主网路径） |
| 方法 | 静态代码对照 + 触发场景推导 |

---

## 执行摘要

bcos `eth/` 内核在 **Prague+ 且经完整 TE 路径**（`buyGas` → `stateTransitionExecute` → `refundGas`）时，intrinsic 公式、EIP-7623 floor、1559 扣款公式、预热集合与 geth **大体对齐**。

初稿标记 **12 项 🔴**；Round 1 去重后 **8 项唯一主题**（见文末复核节）。Prague+ 产品基线下原 **2 项 state root 阻断**；**[6.5] DELEGATECALL value** 与 **[2.1] 顶层 nonce 时机** 均已于 2026-07-01 修复，**Prague+ 主路径 state root 阻断已清零**。

主要差异主题：

1. **Cancun（`eip7623=false`）内核不扣 intrinsic gas** — gasUsed 系统性偏低（Prague+ 产品基线下为有意门控，非主路径 bug）  
2. ~~**Nonce 仅在 SUCCESS 路径递增**~~ — ✅ **已修复 2026-07-01**（`bumpTopLevelSenderNonce`：included tx 无论成败均递增，含 CREATE/precompile；reject 早退不受影响）  
3. **多处 geth reject → bcos included 失败** — intrinsic 不足、buyGas 不足、transfer 不足（**⏸️ ADR-028 已标注，待 TE + 共识层批次落地**；见 Gap 39 / ADR-028 §Implementation tracking）  
4. **非 eip7623 路径跳过 EIP-3529 refund settlement** — London–Cancun 链少退 gas（Prague+ 走 `settleTopLevelTransactionGas`，已对齐）  
5. ~~**DELEGATECALL/CALLCODE 未过滤 value transfer**~~ — ✅ **已修复 2026-07-01**（`isValueTransferSkippedKind` + 回归测试）  
6. **PrecompileActive 将 0x01–0x09 统一门控到 Berlin+** — 与 geth fork 表矛盾（有意设计，见 `PrecompileActiveGateMatrixTest`；Prague+ 不受影响）

**合并判定（复核后）：** ⚠️ **条件 parity** — Prague+ Web3 全链路 intrinsic/7623/refund/预热对齐；**delegate value transfer 与顶层 nonce 时机均已闭合**；Prague+ 主路径无剩余 state root 阻断。剩余 **inclusion/receipt 层**（ADR-028 reject 语义、ADR-015 receiptsRoot）与 **全 revision parity**（Cancun intrinsic、London refund、历史 fork 预编译）为独立课题。

| 等级 | 初稿 | 复核后（去重/分级修正） |
|------|------|------------------------|
| 🔴 | 12（含重复） | **8 主题**；Prague+ **state root 阻断 0**（[6.5]、[2.1] 均已修复）；剩余 ADR-028 / ADR-015 为 inclusion/receipt 层 |
| 🟡 | 18 | 18（ plausible，计数规则不透明） |
| 🟢 | 18 | 18（合并粒度偏粗） |

---

## Top 10 风险差异（按影响排序）

| # | ID | 主题 | 影响 |
|---|-----|------|------|
| 1 | 3.1 | Cancun 无 intrinsic debit | gasUsed 偏低 ≥21000+calldata |
| 2 | 2.1 / P7-05 | Nonce 仅 SUCCESS 递增 | 失败 tx nonce 不变 | ✅ 已修复 |
| 3 | 9.3 / 10.5 | 非 eip7623 跳过 refund settlement | sender 少退 gas、tip 错误 |
| 4 | 6.5 / P8-05 | DELEGATECALL value transfer 未跳过 | 嵌套调用余额错误 | ✅ 已修复 |
| 5 | 1.4 / 3.5 / 4.4 | reject vs included 语义分裂 | receipt/gasUsed/inclusion | ⏸️ ADR-028（TE+共识） |
| 6 | 11.1 | PrecompileActive Berlin 门控过严 | Frontier–Istanbul 预编译不可达 |
| 7 | 4.1 | applyEthMessage 无 buyGas | 单独调用余额不对 |
| 8 | 2.6 | CanTransfer → included INSUFFICIENT_BALANCE | geth reject vs bcos 入块失败 |
| 9 | 2.4 / 4.5 | Blob 4844 precheck + buyGas 缺失 | type-3 tx 路径偏差 |
| 10 | 13.1 | ADR-015 vmerr 归一化 vs geth Failed receipt | 收据 status 映射不同 |

---

## Phase 1 — 顶层流程与入口

### [1.1] 架构拆分：内核 vs TE 编排

**风险等级**: 🔴

**geth 行为**:
`ApplyMessage` (`state_transition.go:308-315`) → `preCheck`（含 `buyGas`）→ `execute` 单体。

**bcos-evm 行为**:
`applyEthMessage` (`ApplyEthMessage.cpp`) → `stateTransitionExecute`；`buyGas`/`refundGas` 在 `EthTxFeeSettlement` + `EthTransactionExecutorImpl.h:171-199`，**不在** `applyEthMessage` 内。

**差异分析**: 编排分层设计；单独调 `applyEthMessage`（EEST adapter、单测）不预扣 gas。

**影响面**: balance: 是；gasUsed: 间接

**触发场景**: 直接 `applyEthMessage` 而不走 TE `buyGas`/`refundGas`

---

### [1.2] 阶段顺序 vs geth execute

**风险等级**: 🟢

bcos `StateTransitionExecute.cpp:59-131` 顺序与 geth `execute` 实质等价（Prague+）：precheck → intrinsic → CanTransfer → innerExecute（含 warm）。

---

### [1.3] GasPool

**风险等级**: 🟢

geth 块级 `GasPool`；bcos ETH 内核无 GasPool，块 gas 在 TE/块执行层。单笔 message 语义无直接影响。

---

### [1.4] Intrinsic / precheck 失败：reject vs included — ⏸️ ADR-028（待 TE + 共识层）

**风险等级**: 🔴（inclusion/receipt 层；**非 state root 阻断**）

**处置**: **已标注、暂缓实现。** 不在 `eth/` 内核单独修；与 [4.4]、N2、reject 簇一并按 [ADR-028](../adr/028-consensus-reject-entry-failure-inclusion.md) Phase C–D（TE Finalize `nullptr` + scheduler 块级错误）批次交付。台账：`architecture-known-gaps.md` Gap 39。

**geth 行为**: `preCheck` / intrinsic / floor 失败 → `execute` 返回 error，**不入块**。

**bcos-evm 行为**: `EthStateTransitionErrorPolicy.h:13-17` → `OutOfGasLimit` + `EVMC_OUT_OF_GAS`；仍产出 `evmcResult`，TE 可出 receipt。

**影响面**: gasUsed: 是（TE 投影 `gasLimit`）；status/receipt: 是

**触发场景**: `gasLimit=20000`，Prague，`eip7623=true`，legacy call，sender 有余额

---

### [1.5] Checkpoint / revert 边界

**风险等级**: 🟡

异常路径 revert 对齐；EVM 失败时 nonce 行为见 [2.1]。

---

## Phase 2 — Precheck

### [2.1] Nonce：无状态校验 + 递增时机 — ✅ 递增时机已修复

**风险等级**: 🔴 → ✅ RESOLVED（递增时机部分）

**geth 行为**: `preCheck` 比较 `msg.Nonce` vs state；非 CREATE 在 **Call 前**递增，**无论成败** (`state_transition.go:619-620`)；CREATE 在 `evm.create` 的 revert snapshot 之前递增 (`evm.go:499`)，地址派生用递增前 nonce (`evm.go:625`)。

**bcos-evm 原行为**: 递增仅在 `TxExecutionRunner.cpp` **`EVMC_SUCCESS` 路径**；失败（REVERT/OOG）不递增；顶层 precompile 直调路径完全不递增。

**影响面**: status: 是；state root: 是（nonce）

**触发场景**: CALL `value=0` execution REVERT/OOG；顶层 CREATE 失败；顶层 tx 目标为 precompile。

**修复 (2026-07-01)**:
- 提取 `bumpTopLevelSenderNonce(state, input)`（`TxExecutionRunner.cpp` 匿名命名空间），在 `finalizeAfterFrame` **成功与失败两分支** + `finalizePrecompileHit` 统一调用；保留 `depth==0`、非零 sender、`skipTopLevelSenderNonceBump`（OpStack deposit）、EIP-7702 auth 预 bump 互斥条件。
- reject 类交易（preCheck/gasAfford/intrinsic/CanTransfer）在 `stateTransitionExecute` 于 `onInvokeInnerExecute` **之前**早退，永不进入内核，故不会误 bump —— 与 geth 对齐。
- CREATE 地址派生仍在帧内用递增前 nonce（bump 在 finalize，位于帧之后），CREATE 失败也按 geth 递增一次。
- 回归：`TxExecutionRunnerTest`（REVERT/INVALID/CREATE 失败/precompile 各 bump 一次；skip flag / nested / 7702 预 bump 不受影响）、`InnerExecuteSmokeTest` 更新为 bump-on-revert；OpStack deposit 4 项（skip flag + 结算层 bump）不受影响，全绿。

**残留（非本次范围）**: 无状态 nonce 校验（`msg.Nonce` vs state nonce 比较）由生产 TE 层 `updateNonce` 承担，Eth 参考路径 `EthTxPrecheck.cpp` 仅拒 `UINT64_MAX`——属 TE 层职责，与本 state-root 阻断分离。

---

### [2.2] EIP-1559 fee cap 校验

**风险等级**: 🟢（公式）/ 🟡（错误码）

公式对齐；bcos 合并为 `Malformed`，geth 区分 `ErrTipAboveFeeCap` / `ErrFeeCapTooLow`。

---

### [2.3] EIP-7702 sender / auth 校验

**风险等级**: 🟢

`EthTxPrecheck.cpp:30-59` 与 geth 对齐。

---

### [2.4] EIP-4844 blob 校验缺失

**风险等级**: 🟡

**geth 行为**: blob hash 前缀、空 hashes、blob fee cap、禁止 CREATE。

**bcos-evm 行为**: `ethTxPrecheck` **无** blob 字段校验；仅有 revision gate。

**触发场景**: Cancun+ type-3 tx，`blobGasFeeCap < blobBaseFee`

---

### [2.5] Typed tx revision gate

**风险等级**: 🟢

`Web3TypedTxKind.h:71-88` 与 geth 门控一致。

---

### [2.6] Balance / CanTransfer 时机与语义 — ⏸️ ADR-028（直调内核路径；TE 主路径假阳性）

**风险等级**: 🔴 → 🟡（TE 主路径）/ 🔴（绕过 TE 直调 `applyEthMessage`）

**处置**: TE 生产路径由 `buyGas` 先验，CanTransfer 不可达 — **Round 2 假阳性**。直调内核时仍 included — 归入 ADR-028 reject 簇，**待 TE + 共识层批次**，不在内核单独改。

**geth 行为**: `buyGas` 先验 `gasFeeCap*gasLimit + value + blob`；`CanTransfer` 仅 value → **`ErrInsufficientFundsForTransfer` reject**。

**bcos-evm 行为**: `EthStateTransitionHooks.cpp:65-73`：`balance < value` → **`INSUFFICIENT_BALANCE` included vmerr**。

**触发场景**: sender balance=50，`value=100`，gas 充足；geth reject，bcos included failed

---

### [2.7] Max initcode / Osaka gas limit

**风险等级**: 🟡

geth `CheckMaxInitCodeSize` / `MaxTxGas`；bcos eth 内核 **无** 等效 precheck。

---

## Phase 3 — Intrinsic Gas + EIP-7623

### [3.1] Pre-Prague：内核不扣 intrinsic gas

**风险等级**: 🔴

**geth 行为**: 始终 `IntrinsicGas` + `gasRemaining.Charge`。

**bcos-evm 行为**: `EthStateTransitionHooks.cpp:33-41`：`eip7623=false` 且无 auth → `IntrinsicDebitMode::None`；`DeductIntrinsicGas.h` **debit=0**。

**触发场景**: Cancun profile，`eip7623=false`，legacy CALL，`gasLimit=50000`

---

### [3.2] 常量对齐（Prague+ Eip7623 模式）

**风险等级**: 🟢

21000 / 4/16 / 32000+2×words / 2400/1900 / 25000×auth / 7623 floor tokens 均对齐。

---

### [3.3] 扣减时序（Prague+）

**风险等级**: 🟢

`deductIntrinsicGas` 在 `onPreCheckCanTransfer` 之前 debit，与 geth 等价。

---

### [3.4] Floor vs intrinsic 比较

**风险等级**: 🟢（公式）/ 🔴（失败语义）

公式对齐；失败路径见 [1.4]。

---

### [3.5] Intrinsic 不足时 gasUsed

**风险等级**: 🔴

geth 拒绝无 receipt；bcos TE 投影 `gasUsed = gasLimit`。

---

## Phase 4 — buyGas

### [4.1] buyGas 不在 applyEthMessage 内核

**风险等级**: 🔴

`StateTransitionHooks.cpp` `onPreCheckGasAffordable` 空实现；`EthTransactionExecutorImpl.h:171` 在 Execute 阶段调用。

---

### [4.2] 余额检查公式（EIP-1559）

**风险等级**: 🟢

检查 `gasFeeCap * gasLimit + value`；扣款 `gasLimit * effectiveGasPrice`，与 geth 一致。

---

### [4.3] effectiveGasPrice

**风险等级**: 🟢

`resolveEffectiveGasPrice` = `min(tipCap+baseFee, feeCap)`。

---

### [4.4] 余额不足：reject vs included + 部分罚没 — ⏸️ ADR-028（待 TE + 共识层）

**风险等级**: 🔴（inclusion/receipt 层）

**处置**: 与 [1.4]、N2 同簇；**ADR-028 Phase C–D** 批次（TE `buyGas` fail → `consensusRejected` → 无 receipt）。内核 ErrorPolicy 映射可保留供 trace。

geth `ErrInsufficientFunds` → reject；bcos `NotEnoughCash`，扣 `min(balance, 21000*effectiveGasPrice)`。

---

### [4.5] Blob gas 余额

**风险等级**: 🟡

`EthTxFeeSettlement::buyGas` **无** blob 分支。

---

## Phase 5 — state.Prepare / Warm

### [5.1] 预热集合

**风险等级**: 🟢

sender / recipient / precompiles / access list / coinbase（EIP-3651）均对齐 `PrepareState.h`。

---

### [5.2]–[5.5] CREATE warm / 7702 warm / transient 清零

**风险等级**: 🟢

与 geth `Prepare` 语义等价。

---

## Phase 6 — Host / State / Value Transfer

### [6.5] DELEGATECALL/CALLCODE value transfer 未跳过 — ✅ 已修复

**风险等级**: 🔴 → ✅ RESOLVED

**geth 行为**: `DelegateCall` / `CallCode` **无** `Transfer` (`evm.go:339-424`)。

**bcos-evm 原行为**: `FrameValueTransfer.h` nested 路径对所有 kind 在 `msg.value≠0` 时转账；`PrecompileRouter::tryEnvelopeValueTransfer` 同样。

**影响面**: balance: 是

**触发场景**: 父 CALL 带 value，子 **DELEGATECALL** 继承非零 value

**修复 (2026-07-01)**:
- 新增 `execution::isValueTransferSkippedKind(kind)`（`bcos-evm/eth/kernel/CallKind.h`）：`DELEGATECALL || CALLCODE` → 跳过资产转移。
- `FrameValueTransfer.h::transferFrameValue` 在 `value≠0` 后新增 `isValueTransferSkippedKind` 门控。
- `PrecompileRouter.cpp::tryEnvelopeValueTransfer` 同步门控（覆盖 DELEGATECALL→precompile 路径）。
- 回归测试 `EthDelegateCallValueTransferCharacterizationTest.cpp`（5 用例，含单元/端到端/CALLCODE/对照），端到端断言 EOA=9/Proxy=1，与 geth 一致。

---

### 其他 P6 项

| ID | 等级 | 摘要 |
|----|------|------|
| P6-01~04,07~08 | 🟢 | journal/revert、EIP-6780、空账户早退、顶层转 code_address 对齐 |
| P6-06 | 🟡 | STATICCALL 无 touch empty account |

---

## Phase 7 — CREATE / CREATE2

| ID | 等级 | 摘要 |
|----|------|------|
| P7-01~04,07~08 | 🟢 | 地址派生、code deposit、嵌套 CREATE nonce bump、endowment 顺序 |
| P7-05 | ✅ | 顶层 CREATE 失败 nonce bump（同 [2.1]，已修复：`bumpTopLevelSenderNonce`） |
| P7-06 | 🟡 | 无显式 contract address collision 检查 |

---

## Phase 8 — 嵌套调用

| ID | 等级 | 摘要 |
|----|------|------|
| P8-01~04,06~07 | 🟢 | depth 1024、revert、7702 路由、precompile envelope |
| P8-05 | ✅ | 同 [6.5] delegate value transfer — **已修复 2026-07-01** |

---

## Phase 9 — Gas Settlement

### [9.3] 非 eip7623 跳过 EIP-3529 refund settlement

**风险等级**: 🔴

**geth 行为**: `calcRefund()` 始终应用 (`state_transition.go:647-648`)。

**bcos-evm 行为**: `EthTransactionExecutorImpl.h:217-225` — 仅 `eip7623` 时调用 `settleTopLevelTransactionGas`；否则 `gasUsed = gasLimit - gasLeft`（**忽略 refund counter**）。

**影响面**: gasUsed: 是；balance: 是（经 refundGas）

**触发场景**: London–Cancun revision，SSTORE 产生 refund 的 tx

---

### [9.4] Refund snapshot 仅 Eip7623 模式

**风险等级**: 🔴

`StateTransitionExecute.cpp` snapshot 仅在 `IntrinsicDebitMode::Eip7623` 时捕获 refund。

---

### 其他 P9 项

| ID | 等级 | 摘要 |
|----|------|------|
| P9-02 | 🟢 | Prague+ 公式对齐 |
| P9-05 | 🟡 | 固定 `/5`，无 pre-London `/2` |
| P9-06 | 🟡 | ADR-015 vmerr 归一化 |
| P9-07 | 🟢 | 每 tx `clear_refund()` |

---

## Phase 10 — Fee Routing

| ID | 等级 | 摘要 |
|----|------|------|
| P10-02,06 | 🟢 | sender refund、coinbase tip、baseFee 销毁 |
| P10-03 | 🟢 | 非 SUCCESS/REVERT 保留预扣 gas |
| P10-04 | 🔴 | buyGas 不足 included + penalty（同 [4.4]） |
| P10-05 | 🔴 | 依赖 [9.3]，refundGas 用错误 gasUsed |

---

## Phase 11 — 预编译

### [11.1] PrecompileActive Berlin 门控过严

**风险等级**: 🔴

**geth 行为**: 0x01–0x04 Frontier；0x05–0x08 Byzantium；0x09 Istanbul。

**bcos-evm 行为**: `PrecompileActive.h:46-48` — 0x01–0x09 统一要求 `revision >= EVMC_BERLIN`。

**触发场景**: Frontier/Byzantium/Istanbul 历史 fork 测试

---

### 其他 P11 项

| ID | 等级 | 摘要 |
|----|------|------|
| P11-02 | 🟡 | `PrecompileTraits.h` 分叉表正确但与 `PrecompileActive` 不一致 |
| P11-03~06 | 🟡/🟢 | ecrecover padding、BLS 底层库差异；modexp gas 对齐 |
| P11-07~08 | 🟢 | 失败耗光 gas、envelope value transfer |

---

## Phase 12 — EIP 覆盖矩阵

| EIP | 状态 | 备注 |
|-----|------|------|
| 1559 / 2929 / 2930 / 3529 / 3651 | ✅ | |
| 4844 / 5656 / 6780 / 1153 | ✅ | blob precheck 在 eth 层缺失见 [2.4] |
| 2537 / 7623 / 7702 / 7212 / 7823 / 7883 | ✅ | |
| 7825 MaxTxGas | ❌ | geth Osaka+ |
| 4762 Verkle / UBT | ❌ | |
| 7976 / 7928 / 7708 Amsterdam | ❌ | |

---

## Phase 13 — 错误映射

| 场景 | geth | bcos | 一致？ | 跟踪 |
|------|------|------|--------|------|
| 顶层 OOG/FAILURE | Failed receipt | ADR-015 → SUCCESS + flag | ❌ 收据语义 | ADR-015 产品决策 |
| 顶层 REVERT | Failed | REVERT | ✅ | |
| intrinsic 不足 | reject | included OutOfGasLimit | ❌ | ⏸️ ADR-028 |
| buyGas 不足 | reject | included NotEnoughCash | ❌ | ⏸️ ADR-028 |
| transfer 不足 | reject | included INSUFFICIENT_BALANCE | ❌ | ⏸️ ADR-028（直调内核） |
| 预编译 failure | 耗光 gas | 同 | ✅ | |

---

## Phase 14 — State 数据层

| 维度 | 状态 | 备注 |
|------|------|------|
| Journal checkpoint/revert | ✅ | `State.hpp` overlay |
| EIP-2929 warm / transient | ✅ | |
| `build_diff()` 输出 | 🟢 信息 | 集成层写库，非 trie commit |
| `BlockInfo` → `evmc_tx_context` | ✅ | |
| Verkle / Amsterdam BAL | ❌ | 未覆盖 |

---

## Phase 15 — 常数对齐

| 常量 | 状态 |
|------|------|
| TX_BASE 21000 / CREATE 32000 / initcode word 2 | ✅ |
| Calldata 4/16 / access list 2400/1900 | ✅ |
| CREATE_DATA 200/byte / MAX_CODE 0x6000 | ✅ |
| 7702 auth 25000 / refund 12500 | ✅ |
| EIP-7623 floor token 10 / refund /5 | ✅ |
| MaxTxGas 16M / MaxCode Amsterdam 32768 | ❌ |

---

## 建议测试向量（每个 🔴）

| ID | 测试草图 | 文件建议 |
|----|---------|---------|
| R1 | Cancun `eip7623=false`，断言 EVM 前 `message.gas` 是否扣 intrinsic | `test/eth/EthIntrinsicGasSyncTest.cpp` |
| R2 | CALL REVERT，断言 sender nonce geth+1 vs bcos | 新 characterization test |
| R3 | `gasLimit=20000` Prague，reject vs included gasUsed | 已有部分覆盖 |
| R4 | `applyEthMessage` 无 TE，sender balance 不变 | EEST adapter 测试 |
| R5 | balance < gasFeeCap×gasLimit，reject vs penalty | `EthTxFeeLedger1559Test` 扩展 |
| R6 | London SSTORE refund tx，非 eip7623 gasUsed 对比 | 新 test |
| R7 | DELEGATECALL 带 value 嵌套，balance 对比 | ✅ `EthDelegateCallValueTransferCharacterizationTest.cpp` |
| R8 | Istanbul CALL 0x01 ecrecover 可达性 | `PrecompileRouter` test |

---

## 优先修复建议（复核后排序）

1. ~~**6.5 / P8-05**~~ — ✅ 已修复：`isValueTransferSkippedKind` 门控 `FrameValueTransfer` + `PrecompileRouter`；`EthDelegateCallValueTransferCharacterizationTest`
2. ~~**2.1 / P7-05**~~ — ✅ 已修复：`bumpTopLevelSenderNonce` 在 finalize 成功/失败 + precompile 统一递增；`TxExecutionRunnerTest` + `InnerExecuteSmokeTest`
3. **ADR-028** — ⏸️ **已标注、暂缓** — 关闭 preCheck reject vs included 簇（intrinsic/buyGas/transfer）；**与 TE Finalize + 共识/scheduler 同批实现**（Gap 39；ADR-028 §Implementation tracking）
4. 若需全 revision：**3.1** 非 `eip7623` 也应 debit intrinsic；**9.3** London+ 始终 refund settlement；**11.1** `PrecompileActive` 对齐 `PrecompileTraits`
5. Prague+ 产品基线：文档化 `eip7623` 门控即 intrinsic+settlement 开关（`RevisionConfig.h:93`）

---

## Round 1 独立复核（2026-07-01）

四路 sub-agent 对初稿逐项复验源码（geth v1.17.3 + bcos feat-evm-refactor）。

### 裁决汇总

| 范围 | 核查项 | VERIFIED | PARTIAL | REFUTED |
|------|--------|----------|---------|---------|
| Phase 1–5 六项 🔴 | 3.1, 2.1, 1.4, 4.4, 2.6, 4.1 | 5 | 1（2.6） | 0 |
| Phase 6–10 五项 🔴 | 6.5, 9.3, 9.4, P7-05, P7-06 | 5 | 0 | 0 |
| Phase 11–15 | 11.1, EIP 矩阵, 错误映射, 常数 | 多数 | 4844/REVERT/refund | 0 |
| 交叉一致性 | 去重、假阳性、设计选择 | — | — | — |

**无一初稿 🔴 被 REFUTED**；[2.6] CanTransfer 降为 PARTIAL（TE 主路径 `buyGas` 已含 value 校验，逻辑冗余）。

### 🔴 去重表（8 项唯一主题）

| 主题 | 初稿重复项 | Prague+ 主路径？ |
|------|-----------|------------------|
| Nonce SUCCESS-only bump | 2.1, P7-05 | ~~是 — 阻断~~ → **✅ 已修复 2026-07-01** |
| DELEGATECALL value transfer | 6.5, P8-05, P8-05b/c | ~~是 — 阻断~~ → **✅ 已修复 2026-07-01** |
| reject vs included 簇 | 1.4, 3.4, 3.5, 4.4, P10-04, 2.6 | 是（ADR-028 设计差距） | ⏸️ 待 TE+共识批次 |
| Cancun 无 intrinsic | 3.1 | 否（`eip7623` Prague+ 恒 true） |
| 非 eip7623 refund skip | 9.3, 9.4, P10-05 | 否（Prague+ 已走 settlement） |
| buyGas 不在内核 | 1.1, 4.1 | TE 路径为设计；直调内核为缺口 |
| PrecompileActive Berlin | 11.1 | 否（Prague+ ≥ Berlin） |

### 复核新发现（初稿未强调）

| ID | 发现 | 等级 |
|----|------|------|
| N1 | 7702 授权 tx 在 Call 前预增 nonce（`TxExecutionRunner.cpp:76-78`），部分缓解 7702 路径 | 🟡 |
| N2 | intrinsic 失败后 buyGas 已扣款、bcos 仍出 receipt（geth reject 不入块） | ⏸️ ADR-028（TE+共识） |
| N3 | CALLCODE 与 DELEGATECALL→precompile 同族 value transfer 缺陷 | ✅ 已修复（同 [6.5]） |
| N4 | `isWeb3==false` 时 Prague+ 也跳过 refund settlement | 🔴（窄场景） |
| N5 | `PrecompileActiveGateMatrixTest` 固化 Berlin 门控为**有意行为** | 信息 |
| N6 | REVERT 收据：geth `Failed(0)` vs bcos 保留 `EVMC_REVERT`（语义同失败、编码不同） | 🟡 |

### 假阳性（Prague+ 产品基线）

- **3.1** — `RevisionConfig.h:93`：`eip7623 = revision >= EVMC_PRAGUE`
- **9.3 / 9.4** — `EthTransactionExecutorImpl.h:217-221` 在 Prague+ 调用 `settleTopLevelTransactionGas`
- **11.1** — Prague+ revision 已 ≥ Berlin
- **1.1 / 4.1**（TE 主路径）— 编排分层，文末已标「架构说明（非缺陷）」

### 设计选择（非 bug）

- **ADR-015** — 顶层 vmerr 归一化为 SUCCESS + flag（`IncludedTxVmerrNormalize.h`）
- **ADR-028**（Proposed，**tracked / deferred 2026-07-01**）— preCheck 失败 inclusion 与 geth reject 差距；**TE + 共识层批次**，见 Gap 39
- **buyGas/refund 在 TE** — `eth/README.md` 分层架构

### 修正后的 Prague+ 阻断清单（Round 1 快照；Round 2 终裁 + 修复记录见下节）

1. ~~**Nonce**~~ — ✅ **已修复 2026-07-01**（`TxExecutionRunner.cpp::bumpTopLevelSenderNonce`）
2. ~~**DELEGATECALL/CALLCODE value**~~ — ✅ **已修复 2026-07-01**（`CallKind.h` / `FrameValueTransfer.h` / `PrecompileRouter.cpp`）

---

## Round 2 交叉验证（2026-07-01，终裁式复核）

针对前两轮**所有**异常项（含争议、降级、新增），五路 sub-agent 追完整调用链并对 evmone/geth 语义作定论。**修正了 Round 1 的两处误判**；DELEGATECALL value 与 nonce 时机 **均已修复（2026-07-01）**，Prague+ 主路径 state root 阻断已清零。

### 关键修正 1 — DELEGATECALL value transfer 是真 bug（非无害）

Round 1 曾疑「evmone DELEGATECALL 传 value=0 故无害」。**REFUTED。** evmone 明确把**继承的 parent apparent value**写入 `evmc_message.value`（`instructions_calls.cpp:117-118`：`msg.value = state.msg->value`），非零时非零。

- `FrameValueTransfer.h:85-91` nested 路径不区分 `msg.kind` → DELEGATECALL 继承非零 value 时**真的**二次 `set_balance`
- geth `DelegateCall`/`CallCode`（`evm.go:388-424`）**无** `Transfer`
- `PrecompileRouter::tryEnvelopeValueTransfer`（`PrecompileRouter.cpp:48-62`）对 DELEGATECALL→precompile 同样转账
- **可复现**：EOA(10 ETH) → Proxy，`value=1 wei`，Proxy 内 `DELEGATECALL(Impl)`；geth 后 EOA=9 ETH / Proxy=1 wei，bcos 后 EOA=8 ETH / Proxy=2 wei（顶层转 1 + delegate 再转 1）
- CALLCODE 分路径：→EVM 合约 `sender==recipient` 自转净额为 0（PARTIAL）；→precompile 带 value 为真 bug
- 现有 `EthDelegateCallPrecompileTest` **未设 `message.value`**，测不到此 bug

**终裁：Prague+ 阻断级 parity 缺陷（真 state root 分歧）。** → **✅ 已修复 2026-07-01**（见 [6.5] 修复节与 `EthDelegateCallValueTransferCharacterizationTest`）。

### 关键修正 2 — reject-vs-included 的 TE 流程定论

Round 1 两 agent 在「buyGas/intrinsic 失败是否入块」上矛盾。**终裁：当前 TE 必定入块并收费**（非 reject）。

- `executeTransaction`（`EthTransactionExecutorImpl.h:281-289`）**无条件**执行 `executeStep<2>()`（Finalize→makeReceipt）；Execute 阶段 `co_return {}` 仅丢弃返回值，不阻断 Finalize
- **N2 成立**：intrinsic 失败前 buyGas 已扣 `gasLimit×effectiveGasPrice`，`refundGas` rollback EVM 但保留预扣 → sender 净损失全额 gas；geth `preCheck` 失败 `return nil,err` → `state_processor.go:176-178` 无 receipt、块 apply 失败、**完全不收费**
- **[2.6] CanTransfer 在 TE 主路径为假阳性**：buyGas 已校验 `maxBalanceDebit + value`，通过后 CanTransfer 不可失败；仅绕过 TE 直调 `applyEthMessage` 时可达
- 均为 **ADR-028（Proposed）** 已文档化的目标态差距（目标：`consensusRejected` → 无 receipt、余额不变）

### 关键修正 3 — receipt status 影响 receiptsRoot（不影响 state root）

新发现：ADR-015 归一化不仅是内部 flag，**会翻转链上 receipt 成功/失败位**。

- OOG/INVALID/7702+REVERT → `evmcResult.status = TransactionStatus::None(0)` → `createReceipt2` 写入 → eth RPC `ReceiptResponse.cpp:16` 反转为 `status=1`（成功）；geth 同场景 `status=0`（失败）
- 顶层 REVERT（无 auth list）**不**归一化，保留 `RevertInstruction=16`，RPC 报失败，与 geth 语义一致（原报告 ✅ 仅在 RPC 语义层成立，链上整型编码仍不同）
- **state root 不受影响**；但 **receiptsRoot 受影响**（receipt hash 含 status）→ 与 geth 对拍 block header 时 receipt root 分歧
- ADR-015 明确仅管 reference/GST 路径，产品 TE receipt 对齐是独立课题——属**有意设计**，非 bug，但若目标为 geth header parity 需单独映射

### 关键修正 4 — Prague+ 生产 Eth 假阳性确认

追 tx type 溯源确认：`eth_sendRawTransaction` → `Web3Transaction.cpp:147` 硬写 `type=Web3Transaction`，`EthChainPolicy` 无 feature mask → 生产 Eth 主路径 **`isWeb3` 与 `eip7623` 恒真**。

- **3.1 / 9.3 / 9.4 / 11.1 / N4**：Prague+ 生产 Eth（block ≥ 22M、RPC 入块）**均为假阳性**
- 仅以下场景真实：**London–Cancun 历史回放**（block 15.5M–22M）、直调 `applyEthMessage` 绕过 TE、`BCOSTransaction` 误入 Eth executor、FISCO `feature_evm_prague` 关闭时

### Round 2 终裁总表

| 项 | Round 1 判定 | Round 2 终裁 | Prague+ 主路径 |
|----|-------------|-------------|----------------|
| Nonce 仅 SUCCESS bump | 🔴 阻断 → ✅ 已修复 | VERIFIED 真阻断（含顶层 CREATE 失败 + precompile）；**2026-07-01 修复**：`bumpTopLevelSenderNonce` 成功/失败/precompile 统一递增 | 已闭合 |
| DELEGATECALL/CALLCODE value | 🔴 阻断 | **VERIFIED 真 bug** → **✅ 已修复 2026-07-01** | ~~受影响~~ 已闭合 |
| N2 intrinsic 失败仍收费 | 🔴 | **VERIFIED**；ADR-028 目标态未实现 | ⏸️ 待 TE+共识（ADR-028） |
| reject vs included 簇 | 🔴 | **VERIFIED 设计差距**（TE 入块+收费） | ⏸️ ADR-028 + Gap 39 |
| 2.6 CanTransfer | PARTIAL | **假阳性**（TE 主路径不可达） | 不受影响 |
| 3.1 Cancun intrinsic | 🔴 | **假阳性**（Prague+）；London–Cancun 真实 | 不受影响 |
| 9.3/9.4 refund skip | 🔴 | **假阳性**（Prague+）；London–Cancun 真实 | 不受影响 |
| N4 isWeb3 gate | 🔴 窄 | **假阳性**（生产 Eth 恒 Web3Tx） | 不受影响 |
| 11.1 PrecompileActive | 🔴 | **假阳性**（Eth 最低 revision=London>Berlin） | 不受影响 |
| ADR-015 receipt 归一化 | ✅ 已修复（Gap 40） | receipt `status` 对齐 geth 失败位；settlement 仍用 `status_code==SUCCESS` | 已闭合 |

### 修正后的最终阻断清单（Prague+ 生产 Eth 主路径）

| # | 阻断 | 影响 | 关键证据 |
|---|------|------|----------|
| 1 | ~~**DELEGATECALL/CALLCODE(+precompile) value transfer**~~ ✅ 已修复 | 余额 / state root | `CallKind.h::isValueTransferSkippedKind` 门控 `FrameValueTransfer.h` + `PrecompileRouter.cpp`；回归 `EthDelegateCallValueTransferCharacterizationTest` |
| 2 | ~~**顶层 CALL/CREATE 失败 nonce 不 bump**~~ ✅ 已修复 | nonce / state root | `TxExecutionRunner.cpp::bumpTopLevelSenderNonce`（成功/失败/precompile 统一）；回归 `TxExecutionRunnerTest` + `InnerExecuteSmokeTest` |
| 3 | **preCheck/intrinsic/buyGas 失败 inclusion + 收费**（N2 簇） | inclusion / 余额 / receiptsRoot | `EthTransactionExecutorImpl.h:281-289`；**⏸️ ADR-028 待 TE+共识批次**（Gap 39） |
| 4 | ~~**ADR-015 vmerr receipt 成功化**~~ ✅ 已修复 | receiptsRoot / RPC status | `IncludedTxVmerrNormalize.h`（Gap 40，2026-07-02） |

**修复优先级**：~~1~~（✅ 已修复）→ ~~2~~（✅ 已修复）→ **3（⏸️ ADR-028，reject 语义 — 待 TE + 共识层同批）** → **4（🎯 ADR-015 receipt status — Gap 40，eth+TE 小步）** → 5（全 revision 3.1 / 9.3 / 11.1）。Prague+ 主路径 state root 阻断已清零；**完全 geth 对齐**还需 3 + 4 + 5。

---

## 修复记录（Remediation log）

| 日期 | ID | 变更 | 验证 |
|------|-----|------|------|
| 2026-07-01 | [6.5] / P8-05 / N3 | `CallKind.h::isValueTransferSkippedKind`；`FrameValueTransfer.h` + `PrecompileRouter.cpp` 门控 DELEGATECALL/CALLCODE 转账 | `EthDelegateCallValueTransferCharacterizationTest`（5 用例） |
| 2026-07-01 | [2.1] / P7-05 | `TxExecutionRunner.cpp::bumpTopLevelSenderNonce` | `TxExecutionRunnerTest`、`InnerExecuteSmokeTest` |
| 2026-07-02 | Gap 40 / ADR-015 receipt | `IncludedTxVmerrNormalize.h`：included vmerr 仅归一化 `status_code`；receipt 保留失败 `TransactionStatus` | `EthIncludedTxVmerrTest`、`EthStateTransitionErrorPolicyTest`、`StateTransitionExecuteTest` |

**下一项建议**：#3 ADR-028 + Gap 39（TE+共识批次）；#5 全 revision 3.1 / 9.3 / 11.1。

---

## 完全 geth 对齐路线图（2026-07-01 产品目标）

| 层 | 主题 | 状态 | 交付 |
|----|------|------|------|
| **State root** | nonce bump、DELEGATECALL value | ✅ 已修复 | `TxExecutionRunner` + 回归测试 |
| **Inclusion** | 执行前 reject（intrinsic/buyGas） | ⏸️ ADR-028 Gap 39 | TE Finalize `nullptr` + scheduler |
| **Receipt / header** | included vmerr 失败位 | ✅ Gap 40（2026-07-02） | `normalizeIncludedTxVmerr` 只改 `status_code` |
| **Receipt / header** | 7702 REVERT receipt | ✅ 同 Gap 40 | `normalizeSetCodeTransactionVmerr` 保留 `RevertInstruction` |
| **Historical fork** | 3.1 / 9.3 / 11.1 | 低优先级 | London–Cancun 回放专项 |

**原则：** settlement 与 inclusion 解耦 — `topLevelIncludedTxVmError` + `status_code==SUCCESS` 驱动 state/gas；`TransactionStatus` 驱动 receipt/RPC，与 geth 一致。

---

## 待办追踪（Deferred backlog）

| ID | 主题 | 层级 | 状态 | 落地方式 |
|----|------|------|------|----------|
| ADR-028 / N2 / [1.4][4.4] | reject vs included（执行前失败仍入块+出 receipt；N2 可能预扣 gas） | inclusion / receiptsRoot / 余额 | ⏸️ **已标注，暂缓** | **TE**（`EthTransactionExecutorImpl` / OpStack TE Finalize `nullptr`）+ **共识/scheduler**（null receipt → 块错误）；`eth/` 仅 Phase A–B `TxConsensusOutcome` 桥接。详见 [ADR-028 §Implementation tracking](../adr/028-consensus-reject-entry-failure-inclusion.md#implementation-tracking-2026-07-01)、`architecture-known-gaps.md` Gap 39 |
| ADR-015 | vmerr receipt 成功化 | receiptsRoot / RPC | ✅ **已修复 2026-07-02**（Gap 40） | `IncludedTxVmerrNormalize.h`：`status_code` 归一化 + receipt `status` 保留 |
| 3.1 / 9.3 / 11.1 | London–Cancun 历史 fork | gas / refund / 预编译 | 低优先级 | 全 revision parity 专项 |

**明确不做（本阶段）**：在 `eth/` 内核单独改 ErrorPolicy 或去掉 entry-failure 的 `evmcResult`，而不接 TE Finalize 与 scheduler —— 会造成 trace 与 inclusion 语义更分裂。

---

## 架构说明（非缺陷）

- **eth 内核**负责 EVM 执行与 state overlay；geth `execute()` 尾部的 refund/floor/fee 在 bcos 拆到 executor `settleGasUsedFromEvmResult` + `EthTxFeeSettlement::refundGas`。
- **`onFinalizeGasUsed`** 故意只做 ADR-015 included-vmerr 规范化，不做 gas 算术。
- **`StateDiff`** 是执行边界持久化 delta，与 geth trie commit 集成点不同但语义清晰。
