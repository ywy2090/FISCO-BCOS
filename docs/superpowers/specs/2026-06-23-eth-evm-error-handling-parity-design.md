# ETH EVM 异常/错误处理 — geth 执行状态一致性 — 设计规格

**日期：** 2026-06-23  
**版本：** v1.1（严格对照 geth / bcos-evm 源码审查修订）  
**状态：** 待评审  
**范围决策（方案 C）：** 共享 kernel 与 geth/EVMC 完全等价；ETH 参考 orchestration 全面对齐；BCOS/OPStack orchestration deviation 隔离在 extension 层且不得污染标准 EVM 状态语义  
**前置：** ADR-005（orchestration 边界）、ADR-015（included-tx vmerr）、ADR-016（EIP-1559 settlement）、`capability-matrix.md`、`2026-06-21-bcos-evm-test-system-design.md`、geth `core/state_transition.go` / `core/vm/`

**交付物（本 spec 批准后）：**

| 文档 | 路径 |
|------|------|
| 本设计规格 | `docs/superpowers/specs/2026-06-23-eth-evm-error-handling-parity-design.md` |
| ADR-017 错误分类契约 | `bcos-evm/docs/adr/017-eth-evm-error-taxonomy.md` |
| capability-matrix 更新 | `bcos-evm/capability-matrix.md`（error-handling 相关行） |

**与既有 spec 的关系：**

- **延续** ADR-015/016 已闭合的 included-tx vmerr、7623 settlement、storage no-op。
- **修订** `2026-06-23-precompile-router-design.md` grilling 方案 A 中「`INSUFFICIENT_BALANCE` 时 `gas_left=0`」——该决策与 geth `evm.Call` 及 EVMC 规范冲突，本 spec 以 geth 为准。
- **修订** `PrecompileRouter` envelope 顺序：当前 router 在 checkpoint 前做 value transfer；geth 是 `Snapshot → Transfer → RunPrecompiledContract → err 时 RevertToSnapshot`，本 spec 以 geth 为准。
- **不修改** BCOS `BALANCE_TRANSFER_GAS` 21000、FISCO auth、OPStack deposit/L1Block 等已登记 orchestration deviation 的产品语义。

---

## 1. 背景与动机

### 1.1 问题陈述

FISCO-BCOS `bcos-evm/eth` 通过 evmone 执行 opcode，与 geth 在指令级行为上大体一致；**执行状态差异**主要来源于：

1. **Host 回调语义**（`EthHost::call`、value transfer、gas_left 返回）
2. **Orchestration 对错误的解释**（拒绝入块 vs included vmerr vs 嵌套传播）
3. **Gas 结算路径分裂**（GST adapter 用 peakGasUsed 模型，TE 生产路径仍用 `gasLimit - gas_left`）

审计发现以下 **state-affecting** 缺口（详见 §4）：

| 缺口 | 影响面 |
|------|--------|
| `PrecompileRouter` checkpoint 晚于 value transfer | precompile 失败时错误保留转账，直接影响 stateRoot |
| 嵌套 `INSUFFICIENT_BALANCE` 时 `gas_left=0` | gasUsed、sender/coinbase 余额、可能 stateRoot |
| TE 未消费 `topLevelIncludedTxVmError`（**范围有限**：web3 路径已走 peak 模型、已正确；仅非 web3 + eip7623 受影响，见 §4.3） | 非 web3 included-vmerr 的 gasUsed / 1559 tip |
| CREATE / CREATE2 pre-execution 与 code-deposit 错误族未完整纳入 | contract collision、max initcode、code store OOG 等 state/gas 偏差 |
| `catch (std::exception&)` 宽泛兜底 | 可执行 tx 被误判为 `INTERNAL_ERROR` 拒绝 |
| `evmcStatusToTransactionStatus` 不完整 | 部分 EVMC status 抛 `UnknownEVMCStatus` |

### 1.2 geth 三层错误模型（金标准）

geth `state_transition.go` 将错误分为三层，**对最终状态的影响完全不同**：

| 层级 | 条件 | `ApplyMessage` 返回 | 交易入块 | 状态提交 |
|------|------|---------------------|----------|----------|
| **共识拒绝** | preCheck、buyGas、intrinsic/floor gas、顶层 transfer 余额不足 | `error != nil` | 否 | 否 |
| **Included vmerr** | 顶层 EVM 执行失败（INVALID、REVERT、OOG 等） | `err == nil`，`ExecutionResult.Err = vmerr` | **是** | **是** |
| **嵌套 call 错误** | 子帧失败 | 向父帧传播；子帧 snapshot revert | — | 子帧回滚 |

收据：`vmerr != nil` → `ReceiptStatusFailed`，但 `stateRoot` 仍反映已提交变更（含 EIP-7702 auth 等）。

### 1.3 方案 C 范围（brainstorming 确认）

不考虑工作量前提下，为达到「**执行状态与以太坊标准一致**」：

```
必须对齐：
  ├─ 共享 kernel（executeMessage, EthHost, State, PrecompileRouter, CreateExecution）
  ├─ ETH 参考 orchestration（ExecuteViaEth, EthTxGasSettlement, EthTransactionExecutorImpl）
  └─ 测试/CI（GST/EEST merge gate + geth nightly 差分）

隔离保留（documented deviation）：
  ├─ BCOS ExecuteViaHost（21000 gas、auth、chain precompile 优先级）
  └─ OPStack（deposit、L1Block、operator fee、postExecuteGasSettlement）

原则：deviation 不得改变「标准 Web3 tx + 无 extension hook」下的 EVM 状态语义。
```

---

## 2. 目标与非目标

### 2.1 目标

1. **验收标准可执行：** 在 ETH 参考 profile（Prague/Osaka+）下，GST/EEST fixture 的 `stateRoot`、`gasUsed`、`logsHash` 与 geth 一致（无 `expectException` 的用例）。
2. **引入 `EthTxOutcome` 错误分类契约**（ADR-017），贯穿 kernel 与 orchestration 出口。
3. **修复 §4 全部 P0 语义缺口**，kernel 修复自动惠及三路径。
4. **闭合 TE 与 GST adapter 的 settlement 分裂**（ADR-015 生产路径）。
5. **扩展 capability-matrix** 与探针 manifest，防止回归。

### 2.2 非目标

- 不修改 legacy `bcos-executor` / `HostContext`。
- 不强制 BCOS/OPStack orchestration 与 geth 逐字段相同（仅要求不污染标准 EVM 子集）。
- 不审计 evmone 内部 opcode 实现（信任 EVMC 接口；仅审 Host 边界）。
- 不在本 spec 内实现 receipt/status 字段的链上产品语义（ETH TE vs BCOS receipt 格式为独立 concern）。
- 不修改 FISCO chain precompile 对 non-empty `[PRECOMPILED]` code 的顶层/嵌套不对称（PrecompileRouter Phase 1 刻意保留，见 precompile-router spec §2.2）。

---

## 3. 错误分类契约（ADR-017 摘要）

### 3.1 `EthTxOutcome` 枚举

```cpp
namespace bcos::evm {

enum class EthTxOutcome {
    ConsensusRejected,    // geth ApplyMessage error
    IncludedVmError,      // geth ExecutionResult.Err != nil, err == nil
    NestedCallError,      // depth > 0，原始 evmc status 传播
    InfrastructureFault,  // 未预期基础设施故障
};

}  // namespace bcos::evm
```

### 3.2 分类规则

| 场景 | `EthTxOutcome` | `evmc_status_code`（对外） | 状态 |
|------|----------------|---------------------------|------|
| preCheck 失败（EOA、fee cap、malformed auth） | `ConsensusRejected` | `EVMC_FAILURE` + `Malformed` | 无 diff |
| intrinsic / floor gas 不足 | `ConsensusRejected` | `EVMC_OUT_OF_GAS` + `OutOfGasLimit` | 无 diff |
| 顶层 transfer 余额不足（clause 6） | `ConsensusRejected` | `EVMC_INSUFFICIENT_BALANCE` | 无 diff |
| 顶层 INVALID / OOG / REVERT / … | `IncludedVmError` | 归一化 → `EVMC_SUCCESS`（ADR-015） | **提交** |
| 嵌套 REVERT | `NestedCallError` | `EVMC_REVERT`，**gas_left 保留** | 子帧 revert |
| 嵌套非 REVERT vmerr | `NestedCallError` | 对应 status，**gas_left = 0** | 子帧 revert |
| 嵌套 transfer 余额不足 | `NestedCallError` | `EVMC_INSUFFICIENT_BALANCE`，**gas_left 保留** | 子帧 revert |
| 嵌套 CREATE / CREATE2 endowment 余额不足 | `NestedCallError` | `EVMC_INSUFFICIENT_BALANCE`，**gas_left 保留** | 子帧 revert |
| 嵌套 depth 超限 | `NestedCallError` | **由 characterization 锁定 evmone 实际 status**，语义要求 `gas_left` 保留 | 子帧 revert |
| CREATE address collision / max initcode / code store OOG / invalid code | `NestedCallError` 或顶层 `IncludedVmError`（按 depth） | 按 geth 错误族映射，非 REVERT fatal 错误 `gas_left=0` | 对应帧 revert |
| 未预期 `std::exception` | `InfrastructureFault` | `EVMC_INTERNAL_ERROR` | revert（若有 checkpoint） |

### 3.3 层级职责（ADR-005 延伸）

| 层 | 职责 |
|----|------|
| **Orchestration**（`ExecuteViaEth`、TE precheck/settlement） | preCheck、intrinsic debit、顶层 normalization、`topLevelIncludedTxVmError`、final `gasUsed` |
| **Kernel**（`executeMessage`、`EthHost`） | checkpoint/revert、host call gas_left、嵌套错误传播、**不做**顶层 normalization |
| **evmone** | opcode 执行、REVERT/OOG 等 status |

### 3.4 geth `vm.Err` ↔ EVMC status 映射表（规范引用）

完整表写入 ADR-017；核心行：

| geth `vm.Err` | EVMC | 嵌套 gas_left |
|---------------|------|---------------|
| `ErrExecutionReverted` | `EVMC_REVERT` | 保留 |
| `ErrOutOfGas` | `EVMC_OUT_OF_GAS` | 0 |
| `ErrInsufficientBalance` | `EVMC_INSUFFICIENT_BALANCE` | **保留** |
| `ErrDepth` | characterization 锁定 evmone 实际 status；不在 spec 猜测 | **保留** |
| `ErrInvalidJump` | `EVMC_BAD_JUMP_DESTINATION` | 0 |
| `ErrWriteProtection` | `EVMC_STATIC_MODE_VIOLATION` | 0 |
| `ErrCodeStoreOutOfGas` | `EVMC_OUT_OF_GAS` / `EVMC_FAILURE`（Homestead 例外见 CreateExecution） | 0（Homestead 前 code store OOG 特殊） |
| `ErrMaxCodeSizeExceeded` | `EVMC_FAILURE` | 0 |
| `ErrInvalidCode` (0xEF) | `EVMC_CONTRACT_VALIDATION_FAILURE` | 0 |
| `ErrContractAddressCollision` | characterization 锁定；语义为 fatal create error | 0 |
| `ErrMaxInitCodeSizeExceeded` | 顶层 consensus reject；嵌套 create fatal error | 0 |

---

## 4. 必须修复的语义点

### 4.1 P0 — `PrecompileRouter` envelope checkpoint 顺序（Kernel）

**现状：** `PrecompileRouter` 在 checkpoint 前执行 `transfer(sender → target)`；如果 builtin/chain precompile 后续失败，`finalizeEnvelope(...).revert()` 只回滚 checkpoint 之后的变更，无法回滚已发生的 value transfer。

**geth 行为：** `evm.Call` 先 `Snapshot()`，再做 value transfer，再进入 `RunPrecompiledContract`；precompile OOG 或 `Run` 返回错误时 `RevertToSnapshot(snapshot)`，因此 value transfer 被一并回滚。

**目标顺序：**

```cpp
if (value != 0 && !canTransfer(...)) {
    return makeInsufficientBalanceResult(input.message.gas);  // 不建 checkpoint
}

state.checkpoint();
if (value != 0 && !skipValueTransfer) {
    transfer(state, sender, target, value);
}

// chain/builtin precompile dispatch
// success: commit()
// failure: revert()  // 回滚 transfer + precompile state
```

**修改文件：**

- `bcos-evm/eth/precompiled/PrecompileRouter.cpp`
- `bcos-evm/test/eth/PrecompileRouterEnvelopeTest.cpp`

**测试：**

- 新增/扩展 value-paying precompile failure 用例：余额足够、precompile OOG 或 failure，断言 sender/target balance 与 storage/logs 全部回滚。
- 保留空账户成功路径：empty account success 仍 commit value transfer。

### 4.2 P0 — 嵌套 CALL/CREATE `INSUFFICIENT_BALANCE` 的 `gas_left`（Kernel）

**现状：**

```cpp
// EthHost.cpp, PrecompileRouter.cpp, executeMessage.cpp
makeResult(EVMC_INSUFFICIENT_BALANCE, 0);
```

**geth 行为**（`evm.Call` / `evm.Create`，transfer 前检查）：

```go
return nil, gas, ErrInsufficientBalance  // gas 不变
```

**EVMC 规范：** `EVMC_INSUFFICIENT_BALANCE` — "The remaining gas is left."

**目标：**

```cpp
makeResult(EVMC_INSUFFICIENT_BALANCE, callMessage.gas);  // CALL/CALLCODE/CREATE/CREATE2 均保留 gas
```

**路径拆分（对照 `EthHost::call` 实测）：**

- **CALL / CALLCODE / STATICCALL 的 value-transfer 余额不足**：由 `PrecompileRouter::dispatchPrecompile` 在 `transferValue` 之前拦截并返回（`EthHost::call` 对所有非 CREATE 调用先进 router，`outcome != NotApplicable` 即提前 return）。因此该族由 §4.1 router 修复负责保留 gas，**不经过** `EthHost::transferValue`。
- **嵌套 CREATE / CREATE2 endowment 余额不足**：CREATE 跳过 router，走 `EthHost::transferValue` → 失败路径，由本节 EthHost 修复负责。
- **`executeMessage` 的两个 `makeInsufficientBalanceResult` 调用点均为 depth==0 顶层**（`applyTopLevelValueTransfer` 仅在 depth==0 生效，顶层 CREATE endowment 同理）。geth 对顶层余额不足在 preCheck 阶段 consensus-reject；§3.2 已将 `EVMC_INSUFFICIENT_BALANCE` 归为 `ConsensusRejected`，故此处 `gas_left` 不影响 included-tx 结算。改动目的是 EVMC 规范/分类一致性，而非嵌套帧 parity。

**修改文件：**

- `bcos-evm/eth/precompiled/PrecompileRouter.cpp` — `makeInsufficientBalanceResult`（CALL 族真正生效点）
- `bcos-evm/eth/state/EthHost.cpp` — `transferValue` 失败路径（嵌套 CREATE 生效点）
- `bcos-evm/eth/executeMessage.cpp` — `makeInsufficientBalanceResult`（顶层路径，分类一致性）

**测试：**

- 更新 `PrecompileRouterEnvelopeTest`（C5）：`gasLeft == message.gas`
- 新增 `InsufficientBalanceGasLeftTest`：CALL 族经 router 路径、CREATE/CREATE2 经 EthHost 路径分别验证父帧 gas 与 geth 一致（测试需用非预编译地址 ≥0x0a，避免误落入 builtin precompile dispatch）
- GST 子集 / geth `inner_instafail` tracer fixture 差分

### 4.3 P0 — TE / adapter 共享 gas settlement，闭合 ADR-015（Orchestration）

**现状：** `EthTransactionExecutorImpl::settleGasUsedFromEvmResult` 仅：

```cpp
m_data->m_gasUsed = m_data->m_gasLimit - evmcResult.gas_left;
```

`m_topLevelIncludedTxVmError` 已由 `executeViaEthTx` 写入但未消费。

**gap 实际范围（对照现有代码）：** 现有 `settleGasUsedFromEvmResult` 已对 `isWeb3 && eip7623 && gasLimit>0` 走 `settleTopLevelTransactionGas`（peak 模型），且 `settleIncludedTopLevelTransactionGas` 当前是 `settleTopLevelTransactionGas` 的逐字转发。因此对 **web3** included-vmerr 交易，gasUsed 已正确；消费 `m_topLevelIncludedTxVmError` 只改变 **非 web3 + eip7623 + gasLimit>0** 的 included-vmerr 情形（peak 模型替代 `gasLimit - gas_left` raw）。实施前须确认此类交易是否真实到达 TE 路径——若不可达，本项为安全重构（无行为变化），不应宣称闭合 gasUsed 差异，也不要在测试中断言 TE gasUsed delta（除非存在非 web3 included-vmerr fixture）。

**目标：** 抽取共享 `finalizeEthTxGasUsed(...)`，`EthTransactionExecutorImpl` 与 `ExecuteViaEthAdapter` 均调用同一实现，避免再次漂移。注意 adapter 侧仅替换 `snapshot.gasLimit>0` 的 peak 分支，保留两处 `TX_BASE_GAS + gasUsed` base-gas 分支（helper 在 `gasLimit<=0` 时短路返回 raw，不含 +21000，不能作为 adapter 唯一结算入口）：

```cpp
if (topLevelIncludedTxVmError && eip7623) {
    return gas::settleIncludedTopLevelTransactionGas(
        snapshot.gasLimit, evmcResult.gas_left, snapshot.evmGasRefund,
        revisionConfig.calldata_floor_per_token, snapshot.calldata);
} else if (snapshot.gasLimit > 0 && isWeb3 && eip7623) {
    return gas::settleTopLevelTransactionGas(...);
} else {
    return gasLimit - evmcResult.gas_left;
}
```

**影响：** 顶层 INVALID/REVERT/OOG 的 `gasUsed`、EIP-1559 effective tip、sender/coinbase 最终余额。

**测试：**

- `EthIncludedTxVmerrTest` 已有 settlement 单元覆盖
- 新增 TE integration：`included_vmerr_still_routes_tip`（ADR-016 §5.6 待补项）
- adapter 与 TE 共享函数的同输入同输出单测

### 4.4 P1 — CREATE / CREATE2 错误族 parity（Kernel + Orchestration）

**现状：** 当前 `bcos-evm/eth` 已处理部分 CREATE code-deposit 错误（runtime code size、0xEF、deposit gas），但没有在本 spec 中覆盖 geth 的完整 CREATE pre-execution 与 code-deposit 错误族；源码搜索未显示 contract address collision / max initcode size 等显式对齐点。

**geth 金标准：**

- 顶层 `CheckMaxInitCodeSize` 在 `state_transition.execute()` 中作为 consensus reject。
- `evm.create` 中 depth、endowment balance、nonce overflow、contract address collision 在 snapshot 前处理。
- initcode 执行后 code size、0xEF、code store OOG 决定是否 revert snapshot 与是否耗尽 gas。

**目标：**

- 明确 `CreateExecution.h` / `executeMessage.cpp` / `EthHost.cpp` 中每个 CREATE error 的 geth 对照。
- 对缺失项补 characterization test；确认为缺口时补实现。
- 不在 ADR-017 中猜测 evmone status；先以测试锁定实际 status，再建立映射。

**测试：**

- contract address collision
- max initcode size
- code store OOG
- max runtime code size
- invalid code prefix `0xEF`
- nonce overflow / depth 超限 characterization

### 4.5 P1 — 收窄 `catch (std::exception&)`（Orchestration）

**现状：** `ExecuteViaEth.cpp` 将所有 `std::exception` 映射为 `EVMC_INTERNAL_ERROR`。

**目标：**

- 保留 `catch (protocol::OutOfGas&)` 专用路径
- `catch (std::exception&)` 改为：记录日志 + `InfrastructureFault`；**禁止**在 reference path 上将可执行 EVM tx 静默转为拒绝，除非明确为基础设施错误
- 长期：kernel 路径消除可抛异常（`invalid_argument` 仅用于 programmer error，不进共识）

**测试：** 注入已知 `protocol::OutOfGas` 与未知异常，断言 outcome 分类。

### 4.6 P1 — `evmcStatusToTransactionStatus` 补全（元数据层）

**现状：** `default` 分支 `BOOST_THROW_EXCEPTION(UnknownEVMCStatus)`。

**目标（两级）：**

**最小修复：** 所有当前 EVMC status 都映射到现有 `TransactionStatus`，不再抛 `UnknownEVMCStatus`。

**增强修复：** 若需要新增 `TransactionStatus`（例如独立 memory access status），必须作为协议层兼容任务单独评估。

最小修复至少覆盖：

- `EVMC_BAD_JUMP_DESTINATION` → `BadJumpDestination`
- `EVMC_INVALID_MEMORY_ACCESS` → 现有最接近 status（**不再**误映射为 `StackUnderflow`）
- `EVMC_STATIC_MODE_VIOLATION` → 合适 `TransactionStatus`
- `EVMC_CONTRACT_VALIDATION_FAILURE` → 合适 `TransactionStatus`
- `EVMC_FAILURE` → `Unknown` 或专用枚举

**注意：** `adoptResult` 已走 `evmcStatusToErrorMessage`；本项防止 BCOS 路径调用 `evmcStatusToTransactionStatus` 时崩溃。一般不改变 stateRoot。

### 4.7 已对齐项（回归防护，不修改语义）

| 项 | 文档 | 回归测试 |
|----|------|----------|
| 顶层 included vmerr normalization | ADR-015 | `EthIncludedTxVmerrTest`、EEST smoke |
| EIP-7623 peakGasUsed + floor | ADR-015/016 | `EthIncludedTxVmerrTest`、adapter |
| Storage no-op | ADR-015 | GST state trie |
| REVERT 保留 gas | evmone | GST `stRevert*` |
| 非 REVERT exhaust gas | evmone | GST `stBadOpcode*` |

---

## 5. 架构与数据流

### 5.1 错误流（顶层交易）

```
Transaction
    │
    ▼
ExecuteViaEth::ethExecuteViaEthPreCheck
    │ ConsensusRejected? ──yes──► return (no state diff)
    ▼
intrinsic / floor gas / auth debit
    │ ConsensusRejected? ──yes──► return
    ▼
top-level value transfer check (clause 6)
    │ ConsensusRejected? ──yes──► return INSUFFICIENT_BALANCE
    ▼
executeMessage (depth=0)
    │
    ├─ SUCCESS ──► IncludedVmError? no ──► finalize settlement
    │
    └─ vmerr ──► IncludedVmError? yes ──► normalize SUCCESS
                              │              + topLevelIncludedTxVmError
                              │              + settleIncludedTopLevelTransactionGas
                              ▼
                         state diff committed (7702 auth 等保留)
```

### 5.2 错误流（嵌套 call — Kernel）

```
EthHost::call (depth > 0)
    │
    ├─ DELEGATECALL + precompile blocked ──► PRECOMPILE_FAILURE, gas 保留
    │
    ├─ [非 CREATE] precompile dispatch（CALL/CALLCODE/STATICCALL 一律先经此）
    │       ├─ balance insufficient ──► INSUFFICIENT_BALANCE, gas_left = msg.gas, no checkpoint  [§4.1 router 修复]
    │       └─ checkpoint → transfer → precompile → commit/revert
    │       └─ NotApplicable（非空 code 合约）──► 继续往下
    │
    ├─ [CREATE/CREATE2 或 router NotApplicable] transferValue fail
    │       └─► INSUFFICIENT_BALANCE, gas_left = msg.gas  [§4.2 EthHost 修复，主要覆盖嵌套 CREATE]
    │
    ├─ vm.execute ──► evmone status
    │       ├─ SUCCESS ──► commit
    │       └─ failure ──► revert (REVERT 保留 gas_left)
    │
    └─ return raw status to evmone（无 normalization）
```

### 5.3 三路径与 kernel 边界

```
┌─────────────────────────────────────────────────────────┐
│ Orchestration (path-specific)                           │
│  executeViaEth │ executeViaHost │ opStackExecuteViaHost │
│  normalization │ BCOS 21000     │ deposit / L1Block      │
│  7623 settle   │ auth           │ operator fee           │
└────────────────────────┬────────────────────────────────┘
                         │ ExecuteMessageInput
                         ▼
┌─────────────────────────────────────────────────────────┐
│ Shared Kernel (MUST match geth for standard EVM)        │
│  executeMessage → EthHost → State → PrecompileRouter  │
└────────────────────────┬────────────────────────────────┘
                         │ evmc_host_interface
                         ▼
                      evmone
```

**BCOS/OPStack deviation 隔离规则：**

1. Deviation 仅通过 `HostExtension` 钩子（`skipHostValueTransfer`、`tryChainPrecompile` 等）实现。
2. capability-matrix 每行 deviation 标注 **影响 stateRoot（标准 EVM 子集）?** — 若为 Y 且无 positive test，视为 bug。
3. Kernel 修复不删除 deviation，但确保 deviation 代码路径不与标准路径共享错误语义分支。

---

## 6. 测试与 CI

### 6.1 测试金字塔

| 层级 | 内容 | Gate |
|------|------|------|
| **L1 单元** | `EthIncludedTxVmerrTest`、`InsufficientBalanceGasLeftTest`、`EthTxOutcomeClassificationTest`、`CreateErrorParityTest`、更新 `PrecompileRouterEnvelopeTest` | PR required |
| **L2 参考向量** | GST + EEST（Cancun/Prague/Osaka） | PR required（manifest 子集）；nightly 全量 |
| **L3 探针** | `eth-eest-probe-{return,invalid,revert,oog}.json` + 新增 `insufficient_balance`、`depth`、`create_errors` | PR required |
| **L4 geth 差分** | 同一 fixture → FB adapter vs geth `t8ntool`；diff stateRoot/gasUsed/logsHash | Nightly |
| **L5 TE 集成** | `included_vmerr_still_routes_tip`、1559 + vmerr 交叉 | PR required |

### 6.2 探针 manifest（新增）

| Manifest | 覆盖 |
|----------|------|
| `eth-eest-probe-insufficient-balance.json` | 嵌套 CALL value > balance |
| `eth-eest-probe-depth.json` | 超深度 CALL |
| `eth-eest-probe-create-errors.json` | CREATE collision / code-deposit / invalid-code |

### 6.3 capability-matrix 新增/更新行

| Capability | Layer | ETH | BCOS | OPStack | 修后 token |
|------------|-------|-----|------|---------|------------|
| nested INSUFFICIENT_BALANCE gas_left | kernel | `inherited` | `inherited` | `inherited` | `InsufficientBalanceGasLeftTest` |
| PrecompileRouter value-transfer envelope | kernel | `inherited` | `inherited` | `inherited` | `PrecompileRouterEnvelopeTest` |
| CREATE error parity | kernel + orchestration | `explicit`/`inherited`（按错误层级） | `inherited` for kernel | `inherited` for kernel | `CreateErrorParityTest` |
| included-tx vmerr TE settlement | orchestration | `explicit`（闭合 TE 分支） | N/A | N/A | `included_vmerr_still_routes_tip` |
| EthTxOutcome taxonomy | orchestration | `explicit`（ADR-017） | `inherited`（kernel 分类） | `inherited` | `EthTxOutcomeClassificationTest` |

### 6.4 验收标准（Definition of Done）

1. L1 + L3 + L5 全绿。
2. GST/EEST PR manifest 零失败；nightly 全量零失败（或白名单仅含已登记 deviation）。
3. geth nightly 差分：标准 EVM fixture 零 diff。
4. ADR-017 与 capability-matrix 已合并。
5. 无新增 `UnknownEVMCStatus` 未覆盖路径。

---

## 7. 实施阶段

| Phase | 内容 | 产出 | 验收 |
|-------|------|------|------|
| **1** | P0 kernel：`PrecompileRouter` checkpoint/value-transfer envelope + insufficient-balance gas_left | PR #1 | value-paying precompile failure 回滚；C5 绿 |
| **2** | P0 nested CALL/CREATE gas_left parity：CALL/CALLCODE/CREATE/CREATE2 insufficient balance + depth characterization | PR #2 | `InsufficientBalanceGasLeftTest` 绿 |
| **3** | P0 TE/adapter shared settlement：抽 `finalizeEthTxGasUsed()` + ADR-015 TE 分支 | PR #3 | L5 绿 |
| **4** | P1 CREATE error parity：collision、max initcode、code store OOG、invalid code、max code size | PR #4 | `CreateErrorParityTest` 绿 |
| **5** | P1 taxonomy/status/exception：ADR-017、EVMC status 全覆盖、exception 收窄 | PR #5 | L1 classification 绿 |
| **6** | capability-matrix + 探针扩展 + GST/EEST 子集 + geth nightly 差分 | PR #6 | L3/L4 绿 |

Phase 1 必须先做，因为它修正会直接影响后续 precompile 与 gas_left 测试的基线。Phase 3 可在 Phase 1–2 之后并行准备，但最终需使用共享 settlement 函数替换 adapter 与 TE 两处逻辑。

---

## 8. 风险与缓解

| 风险 | 缓解 |
|------|------|
| `gas_left` 修复改变历史链上 gasUsed（若已上线） | 以 `Features::Flag` 门控（类似 `bugfix_v1_error_handling`）；reference path 默认新语义 |
| `PrecompileRouter` envelope 修复改变失败 precompile 的历史状态 | 与 `gas_left` 修复同一 bugfix flag；标准 ETH reference path 默认启用 |
| precompile-router C5 测试期望反转 | 本 spec 明确修订 grilling 方案 A；测试随 Phase 1 更新 |
| TE 与 adapter settlement 仍有细微差 | Phase 3 抽取共享 `finalizeEthTxGasUsed()` 函数，adapter 与 TE 共用 |
| BCOS 21000 gas 干扰 GST | GST 仅跑 `executeViaEth` adapter，不经过 BCOS orchestration |

---

## 9. 开放问题（实施前关闭）

| # | 问题 | 建议默认 |
|---|------|----------|
| Q1 | `gas_left` 修复是否需 `Features::Flag` 共识门控？ | 是（新链默认 ON；存量链 configurable） |
| Q2 | `PrecompileRouter` envelope 修复是否与 `gas_left` 使用同一 flag？ | 是，作为同一类 kernel error-handling bugfix |
| Q3 | `EVMC_INVALID_MEMORY_ACCESS` 映射到哪个 `TransactionStatus`？ | 最小修复先映射到现有最接近 status；新增 enum 另立协议兼容任务 |
| Q4 | depth 超限实际 EVMC status 是什么？ | 不猜测；Phase 2 characterization 锁定 |
| Q5 | geth nightly 差分白名单存放位置？ | `specs-tests/assets/geth-diff-allowlist.json` |

---

## 10. 参考文献

- geth `core/state_transition.go` — `execute()`, `preCheck()`
- geth `core/vm/evm.go` — `Call`, `Create`, gas exhaust on error
- geth `core/vm/errors.go` — `VMError` / `VMErrorCode`
- EVMC `evmc.h` — `evmc_status_code` 语义
- `bcos-evm/docs/adr/015-eth-reference-7702-gas-and-included-tx-vmerr.md`
- `bcos-evm/docs/adr/016-eth-eip1559-settlement.md`
- `docs/superpowers/specs/2026-06-23-precompile-router-design.md`（§修订 INSUFFICIENT_BALANCE gas_left）

---

## Spec 自检记录（v1.1）

| 检查项 | 结果 |
|--------|------|
| Placeholder / TBD | §9 开放问题已列出建议默认；depth status 明确要求 characterization，不作猜测 |
| 内部一致性 | 方案 C 范围与 §4 修复项、§5 架构、§6 测试一致；已纳入 PrecompileRouter envelope 与 CREATE 错误族 |
| 范围 | 单次 implementation plan 可覆盖 Phase 1–6；Phase 4 CREATE parity 可独立 PR 实施 |
| 歧义 | 「标准 EVM 子集」= Web3 tx + 无 BCOS/OPStack extension hook + ETH reference profile |
| 与 ADR-015 关系 | 延续并闭合 TE 路径；不重复已闭合项 |
