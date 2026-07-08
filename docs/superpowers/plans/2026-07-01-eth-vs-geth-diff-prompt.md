# bcos-evm/eth/ vs go-ethereum 标准 EVM 执行语义对比 — 统一提示词

> 一次性喂给 AI，产出按差异点组织的逐项对比分析，重点标注会影响 **状态根 / gasUsed / 余额 / receipt** 的差异。

---

## 审计目标

对比 **bcos-evm 可移植 ETH 内核**（`bcos-evm/eth/`，编译为 `bcos-evm-eth`）与 **go-ethereum 标准 EVM**（`blockchain-impl/go-ethereum`）在单笔交易执行全链路上的语义差异。

**只审计标准以太坊路径**，不包含 OP Stack / FISCO 链特有编排（`opstack/`、`bcos/`），但允许通过 `applyEthMessage` / `EthStateTransitionHooks` 观察 ETH 参考链如何调用内核。

**核心问题**：在相同 pre-state、区块环境、交易输入下，两边是否产生相同的：
- `gasUsed` / `gasRefund` / `effectiveGasPrice`
- 账户 `balance` / `nonce` / `code` / `storage`
- `evmc_status_code` / receipt status
- logs / state root（若可推导）

---

## 代码路径（必须阅读）

### bcos-evm（被审计方）

| 层级 | 路径 | 职责 |
|------|------|------|
| 链入口 | `eth/apply/ApplyEthMessage.*` | geth `ApplyMessage` 适配 |
| 状态转换驱动 | `eth/kernel/state-transition/StateTransitionExecute.*` | geth `stateTransition.execute` |
| 上下文 | `eth/kernel/state-transition/StateTransitionContext.h` | msg / gas / state / revision |
| 预检 | `eth/apply/EthTxPrecheck.*`, `EthStateTransitionHooks.*` | preCheck 规则切片 |
| Intrinsic gas | `eth/kernel/state-transition/DeductIntrinsicGas.h`, `eth/gas/TxIntrinsicGas.h` | intrinsic debit + post-settlement |
| 费用结算 | `eth/apply/EthTxFeeSettlement.h`, `eth/gas/TxFeeSettlement.h` | buyGas / refundGas |
| 执行入口 | `eth/kernel/execution/InnerExecute.*` | geth innerExecute |
| 帧执行 | `eth/kernel/execution/EvmCallFrame.*` | geth `evm.Call` / `Create` |
| 合约创建 | `eth/kernel/execution/CreateContract.h` | CREATE/CREATE2 setup + code deposit |
| 地址解析 | `eth/kernel/execution/ExecutionAddressResolver.*` | 7702 delegation / CREATE 地址 |
| 预热 | `eth/kernel/execution/WarmTransactionEntry.h` | geth `state.Prepare` 等价 |
| 状态层 | `eth/state/State.hpp`, `State.cpp` | journal / checkpoint / warm / refund |
| Host | `eth/host/EthHost.*` | evmone host：balance/transfer/storage/log |
| 状态输出 | `eth/state/StateDiff.hpp` | `build_diff()` 持久化 delta |
| 区块/交易上下文 | `eth/state/BlockInfo.hpp`, `Transaction.hpp` | `evmc_tx_context` + warm 输入 |
| EIP 实现 | `eth/eip/Eip*.h`, `eth/eip/Eip7702.cpp` | 单 EIP 逻辑 |
| Gas 常量 | `eth/gas/ProtocolGas.h`, `eth/eip/Eip2929StorageGas.h` | opcode / storage gas |
| 预编译 | `eth/precompiled/PrecompileRouter.*`, `EthPrecompiles.*` | geth `core/vm/contracts.go` |
| VM | `eth/vm/VMInstance.*` | evmone 封装 |
| 类型交易 | `eth/Web3TypedTxKind.h` | EIP-2718 type byte |
| Revision | `eth/RevisionConfig.h`, `eth/policy/EthChainPolicy.h` | fork 门控 |
| 错误映射 | `eth/kernel/state-transition/IncludedTxVmerrNormalize.h`, `EthStateTransitionErrorPolicy.h` | reject vs included vmerr |

**已知近期重构（会话 f633c84a 上下文，审计时以当前代码为准）：**
- `Transition.hpp` / `BloomFilter` 已移至 `test/state/`（仅测试，非生产路径）
- `CreateExecution.h` → `eth/kernel/execution/CreateContract.h`（namespace `bcos::evm::execution`）
- `EthTxGasSettlement.h` → `eth/gas/TxIntrinsicGas.h`
- `Transaction.hpp` 仅保留 `Transaction` / `TransactionProperties` / `LogEntry`（无 `TransactionReceipt`）

### go-ethereum（参照方）

| 路径 | 职责 |
|------|------|
| `core/state_transition.go` | `ApplyMessage`, `stateTransition.execute`, preCheck, buyGas, settlement |
| `core/state_processor.go` | `ApplyTransaction`, receipt |
| `core/evm.go` | `NewEVM`, block context |
| `core/vm/evm.go` | `Call`, `Create`, `Create2`, nested calls |
| `core/vm/gas_table.go`, `gas.go` | opcode gas |
| `core/vm/contracts.go` | 预编译 |
| `core/vm/eips.go` | EIP 开关与 gas 修订 |
| `core/state/statedb.go` | journal, snapshot, Prepare, refund |
| `core/types/transaction.go` | `IntrinsicGas`, typed tx |
| `params/protocol_params.go` | gas 常量 |
| `params/config.go` | fork 配置 |

**参照版本**：优先 pin `go-ethereum` @ **v1.17.3**（与 ADR-030 一致）；若本地 HEAD 不同，在报告元信息中注明 commit。

**本地路径**：`/Users/octopus/octo/code/blockchain-impl/go-ethereum`

**bcos worktree**：`/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor`

---

## 审计方法

对每个阶段：
1. 阅读 go-ethereum 对应源文件（给出函数名 + 行号）
2. 阅读 bcos-evm 对应文件（给出路径 + 行号）
3. 列出所有差异，标注风险等级：
   - 🔴 **高**：直接导致 gasUsed / balance / status / state root 不一致
   - 🟡 **中**：边界条件或 fork 门控下可能不一致
   - 🟢 **低**：实现细节不同但语义等价
4. 对每个 🔴/🟡 差异给出**可复现的触发交易**（具体 calldata、gasLimit、pre-state balance/nonce、revision）

**禁止**：只列文件名不读代码；把 evmone 行为当黑盒不对照 geth；混淆 `reject`（不入块）与 `included vmerr`（入块失败）。

---

## 阶段 1：顶层流程与入口

- **geth**: `ApplyMessage` → `stateTransition.execute`
- **bcos**: `applyEthMessage` → `stateTransitionExecute` → hooks → `innerExecute`

对比要点：
- 阶段划分与顺序是否与 ADR-030 §3 step map 一致
- `GasPool` 扣减时机（geth 在 `ApplyMessage` 外层；bcos ETH 参考路径在哪里？）
- checkpoint / revert 边界：失败时 state journal 是否等价于 geth snapshot revert
- `StateDiff`（`build_diff()`）导出字段是否与 geth state commit 范围一致（是否包含 transient storage、warm set 泄漏）

---

## 阶段 2：Precheck — 交易入口校验

- **geth**: `preCheck()`, `Transaction.Validate()`
- **bcos**: `EthTxPrecheck`, `EthStateTransitionHooks::onPreCheck*`

逐项核查：

1. **Nonce**：检查时机、错误类型（reject vs included）、成功后递增时机
2. **EIP-1559**：`gasFeeCap < gasTipCap`、`gasFeeCap < baseFee` 的错误码
3. **EIP-7702**：sender code 为非法 delegation 时的处理；`Web3TypedTxKind::EIP7702` 与 revision 门控
4. **EIP-4844**：blob tx 字段校验、versioned hash prefix、`blobGasFeeCap < blobBaseFee` 错误码
5. **Typed tx revision gate**：`isTypedTxKindSupportedByRevision` vs geth `ValidateTransaction`
6. **Balance 预检**：`onPreCheckCanTransfer` / `canTransfer` 是否在正确时机检查 `value`（geth `ErrInsufficientFundsForTransfer`）
7. **geth 有而 bcos 缺失的检查**，或反之

---

## 阶段 3：Intrinsic Gas 与 Floor Gas（EIP-7623）

- **geth**: `IntrinsicGas()`, `FloorDataGas`, `state_transition.go` intrinsic charge
- **bcos**: `computeTxIntrinsicGas`, `deductIntrinsicGas`, `TxIntrinsicGas::gasLimitMinimum`

逐项核查：

1. **TX_BASE_GAS** = 21000
2. **Calldata**：零字节 4 / 非零 16；EIP-7623 token 计数与 floor reserve
3. **CREATE intrinsic**：32000 + 2×ceil(initcode/32)
4. **Access list**：2400/地址 + 1900/key
5. **EIP-7702 auth tuple**：25000×count；是否与 intrinsic minimum 合并（`gasLimitMinimumWithAuth`）
6. 🔴 **扣减时序**：geth `gasRemaining.Charge(intrinsic)` vs bcos `deductIntrinsicGas` 从 `message.gas` 扣除的时机
7. 🔴 **floor vs intrinsic 比较**：`max(21000+floor, intrinsic)` 是否在 pre-execution 与 post-execution 两侧都对齐
8. intrinsic 不足时：gasUsed=0 还是 gasLimit？status 映射

---

## 阶段 4：buyGas — 余额预扣

- **geth**: `buyGas()` in `preCheck`
- **bcos**: `EthTxFeeSettlement`, fee ledger / orchestration

逐项核查：

1. 🔴 **余额检查公式**：
   - Legacy: `gasPrice * gasLimit + value`
   - EIP-1559: `gasFeeCap * gasLimit + value`（保守）vs `effectiveGasPrice * gasLimit + value`
2. **effectiveGasPrice** = `min(tipCap+baseFee, feeCap)` 与 geth `EffectiveGasTip` 一致吗
3. **扣款时机**：从 sender 扣除 `gasLimit * effectiveGasPrice` 还是 `gasFeeCap * gasLimit`
4. **Blob gas**：blob balance 检查与扣除
5. 余额不足：reject 还是 included `INSUFFICIENT_BALANCE`

---

## 阶段 5：state.Prepare — 地址预热（EIP-2929 / 3651 / 2930）

- **geth**: `state.Prepare(rules, sender, coinbase, dest, precompiles, txAccesses)`
- **bcos**: `warmTransactionEntry`, `BlockInfo` + `TransactionProperties`

逐项核查：

1. 预热集合：sender、recipient、access list 地址+key、precompile 地址
2. **EIP-3651 warm coinbase**：`txProps.warmCoinbase && revision >= Shanghai`
3. **CREATE warm destination**：`setWarmDestinationFromKind`
4. **EIP-7702 delegation 地址**是否在 prepare 阶段预热
5. transient storage 清零时机（`State` journal vs geth `StateDB.Prepare`）
6. 🔴 预热遗漏是否导致 `BALANCE`/`SLOAD`/`CALL` gas 差异

---

## 阶段 6：EVM 执行 — Host 与 State

- **geth**: `evm.Call`/`Create`, `StateDB` 接口
- **bcos**: `runCallFrame`/`EvmCallFrame`, `EthHost`, `state::State`

### 6a. 余额与转账

| 操作 | geth | bcos |
|------|------|------|
| `CanTransfer` | 顶层 value 检查 | `FrameValueTransfer` |
| `Transfer` | sender→recipient | `EthHost` / `State::transfer` |
| CREATE 时 value 转移 | create 前扣款 | `prepareCreateTargetBeforeInit` 前后 |
| SELFDESTRUCT 余额去向 | EIP-6780 规则 | `State` 实现 |
| 7702 delegation 账户余额 | 不变 | 对照 `Eip7702` |

🔴 重点：嵌套 CALL 的 value 转移、revert 后 balance 恢复、touch empty account 行为。

### 6b. Storage / Transient / Refund

- SLOAD/SSTORE gas（EIP-2200/2929/3529）：`Eip2929StorageGas.h` vs `gas_table.go`
- `State::get_refund()` vs geth `StateDB.GetRefund()` — 谁为 settlement 的权威来源
- transient storage：`State` 是否在 `build_diff()` 前清除

### 6c. Logs 与 Output

- `EthHost::emit_log` → `LogEntry` vs geth `AddLog`
- REVERT 时 output 保留规则
- 测试专用 `BloomFilter` 已移出生产路径 — 确认生产 receipt logs bloom 由哪层生成

---

## 阶段 7：CREATE / CREATE2 语义

- **geth**: `evm.Create`, `create` in `state_transition.go`
- **bcos**: `CreateContract.h`, `EvmCallFrame`, `installCreatedContractCode`

逐项核查：

1. 地址派生：legacy CREATE nonce 公式、CREATE2 keccak
2. Spurious Dragon 后 nonce=1 初始化时机（checkpoint 内）
3. Code deposit gas：200/byte；Frontier 不足 gas 时的特殊行为
4. EIP-3541：`0xEF` 前缀拒绝（`EVMC_CONTRACT_VALIDATION_FAILURE`）
5. Max code size 0x6000
6. EIP-6780：create 后 selfdestruct 限制
7. initcode 执行失败 vs deploy 失败（gas 消耗差异）

---

## 阶段 8：嵌套调用与帧语义

- **geth**: `evm.Call`, `DelegateCall`, `StaticCall`, depth limit
- **bcos**: `EvmCallFrame`, `FrameScope`, `CallTargetResolver`, `EvmHostHooks`

对比要点：
1. 1024 depth limit
2. Static call 下 state 变更禁止
3. Delegate call 的 `msg.sender`/`value` 继承
4. Precompile 调用 gas 退还是否一致
5. `ChainExtendedPrecompileDispatch` 是否改变标准预编译行为（ETH 参考路径应为 no-op 扩展）
6. 7702：`ExecutionAddressResolver` 替换 code 的时机 vs geth `resolveCode`

---

## 阶段 9：Gas Settlement — 执行后结算

- **geth**: `execute()` 尾部 refund + 7623 uplift
- **bcos**: `settleTopLevelTransactionGas`, `onFinalizeGasUsed`, `TxGasSettlementSnapshot`

逐项核查：

1. **EIP-3529 refund cap**：`min(stateRefund, gasUsedBeforeRefund / 5)` 整数除法方向
2. `gasRemaining = min(gasLimit, gasLeft + cappedRefund)`
3. `gasUsed = gasLimit - gasRemaining`
4. **EIP-7623 floor uplift**：`gasUsed = max(gasUsed, floorDataGas)` 且 `<= gasLimit`
5. `floorDataGas > gasLimit` 时 gasUsed 取何值
6. 🔴 `evmc_result.gas_refund` vs `State::get_refund()` 哪个参与 settlement
7. 不同失败路径的 gasUsed：
   - intrinsic 拒绝
   - buyGas 失败
   - EVM REVERT / OOG / INVALID
   - 预编译失败

---

## 阶段 10：Fee Routing — 费用分配

- **geth**: `refundGas`, coinbase tip, baseFee burn
- **bcos**: `EthTxFeeSettlement`, `TxFeeSettlement.h`

逐项核查：

1. Sender refund：`gasRemaining * effectiveGasPrice`
2. Coinbase tip：`gasUsed * max(0, effectiveGasPrice - baseFee)`
3. Base fee：销毁（EIP-1559）vs 转账
4. Legacy tx：`gasPrice` 与 `effectiveGasPrice` 退化行为
5. 🔴 结算后 sender balance 是否与 geth 一致（含 refund 与 tip 分配）

---

## 阶段 11：预编译合约

- **geth**: `core/vm/contracts.go`
- **bcos**: `PrecompileRouter`, `EthPrecompiles`, `ModexpGas`, `Eip2537Gas`

按地址逐项对比（至少覆盖 Cancun+ / Prague 基线）：

| 地址 | 预编译 | 重点 |
|------|--------|------|
| 0x01 | ecrecover | 输入 padding、无效签名 |
| 0x02 | SHA256 | |
| 0x03 | RIPEMD160 | |
| 0x04 | identity | |
| 0x05 | modexp | EIP-2565/7823 拒绝规则 |
| 0x06-09 | bn254 | |
| 0x0a | blake2f | |
| 0x0b-11 | EIP-2537 BLS12-381 | gas 表 |
| 0x0f | EIP-7212 secp256r1 | |
| 0x100 | EIP-7702 相关（若启用） | |

每个预编译标注：gas 公式、错误时 gas 消耗、输出格式。

---

## 阶段 12：EIP 覆盖矩阵

以 `RevisionConfig`（bcos）vs `params.Rules`（geth）为轴，逐项确认门控与实现：

| EIP | bcos 文件 | geth 锚点 | 影响面 |
|-----|-----------|-----------|--------|
| 1559 | `Eip1559.h`, `Eip1559Gate.h` | `state_transition.go` | gas price, base fee |
| 2929 | `Eip2929Gate.h`, `State` warm | `statedb.go Prepare` | access gas |
| 2930 | `Eip2930AccessList.h` | `transaction.go` | intrinsic + warm |
| 3529 | `TxIntrinsicGas` refund cap | `state_transition.go` | refund |
| 3651 | `WarmTransactionEntry` | `Prepare` | coinbase warm |
| 4844 | `Eip4844.h` | blob tx paths | blob gas |
| 6780 | CREATE/selfdestruct | `evm.go` | create semantics |
| 7623 | `Eip7623.h`, `TxIntrinsicGas` | `FloorDataGas` | floor gas |
| 7702 | `Eip7702.*` | `state_transition.go` | delegation, auth |
| 7610 | — | `eip7610.go` | 创建合约规则 |

---

## 阶段 13：错误映射与 included-tx 语义

- **geth**: `error`（reject）vs `vmerr`（入块失败）
- **bcos**: `IncludedTxVmerrNormalize`, `EthStateTransitionErrorPolicy`, `EVMCResult`

填表：

| 场景 | geth 结果 | bcos status | bcos TransactionStatus | 一致？ |
|------|-----------|-------------|------------------------|--------|
| Nonce too high/low | | | | |
| Insufficient funds (buyGas) | | | | |
| Insufficient funds (transfer) | | | | |
| Intrinsic gas too low | | | | |
| Gas limit reached | | | | |
| REVERT | | | | |
| OOG in execution | | | | |
| INVALID opcode | | | | |
| Precompile failure | | | | |
| 7702 malformed auth | | | | |

---

## 阶段 14：State 数据层专项（会话上下文）

基于 `eth/state/` 生产文件逐项对照 geth `StateDB`：

| bcos 文件 | geth 对应 | 审计重点 |
|-----------|-----------|----------|
| `State.hpp/cpp` | `statedb.go` | journal, snapshot, revert, refund counter |
| `Account.hpp` | `stateObject` | dirty flags, empty vs deleted |
| `StateDiff.hpp` | commit delta | `build_diff()` 是否遗漏字段 |
| `BlockInfo.hpp` | `BlockContext` | `evmc_tx_context` 各字段 |
| `Transaction.hpp` | `Message` 子集 | 仅执行语义字段 |
| `EvmStateReader.hpp` | `StateDB` 读接口 | balance/code/storage 只读视图 |
| `HashUtils.hpp` | crypto/address utils | CREATE 地址、keccak |

**明确跳过**（非生产路径）：`test/state/Transition.hpp`, `BloomFilter.*`

---

## 阶段 15：常数对齐

| 常量 | bcos 位置 | geth 位置 | 一致？ |
|------|-----------|-----------|--------|
| TX_BASE_GAS | `TxIntrinsicGas.h` | `params` | |
| CREATE_BASE_GAS | | | |
| INITCODE_WORD_GAS | | | |
| CREATE_DATA_GAS_PER_BYTE | `CreateContract.h` | | |
| MAX_CODE_SIZE | `CreateContract.h` | | |
| ACCESS_LIST_ADDRESS_COST | | | |
| PER_EMPTY_ACCOUNT_COST | | | |
| CALL_STIPEND | `ProtocolGas.h` | | |
| SSTORE 系列 | `Eip2929StorageGas.h` | `gas_table.go` | |

---

## 输出格式要求

每个差异点：

```markdown
### [阶段 X] — [差异主题]

**风险等级**: 🔴/🟡/🟢

**geth 行为**:
<函数名、文件:行号、逻辑描述>

**bcos-evm 行为**:
<函数名、文件:行号、逻辑描述>

**差异分析**:
<根因 — 公式/时序/门控/类型>

**影响面**:
- gasUsed: 是/否，如何变
- balance: 是/否，哪些账户
- status/receipt: 是/否

**触发场景**:
<具体交易参数 + pre-state，可写成 table 或伪代码>

**修复建议（可选）**:
<对齐 geth 的修改方向>
```

---

## 最终交付物

1. **执行摘要**：🔴/🟡/🟢 数量；是否达到 Cancun+/Prague 参考链 parity
2. **Top 10 风险差异**（按影响排序）
3. **阶段 1–15 完整差异清单**
4. **EIP 覆盖矩阵**（✅/⚠️/❌）
5. **建议测试向量**：每个 🔴 差异对应一个可写入 `bcos-evm/test/eth/` 的 characterization test 草图
6. **元信息表**

---

## 使用建议

1. **分阶段投喂**：一次一个阶段，避免上下文溢出；每阶段要求 AI 必须引用具体行号。
2. **先跑 ETH 参考路径**：`applyEthMessage` + `EthStateTransitionHooks`，不要混入 `opstack/` / `bcos/`。
3. **与现有审计衔接**：OP Stack 差异用 `docs/superpowers/plans/2026-06-25-opstack-op-geth-diff-prompt.md`；本提示词专注 **可移植内核 parity**。
4. **验证闭环**：每个 🔴 差异应能落到 `bcos-evm/test/eth/` 或 EEST fixture 的一条断言。
