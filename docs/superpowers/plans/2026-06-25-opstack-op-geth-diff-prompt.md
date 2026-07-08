# bcos-evm/opstack/ vs op-geth 交易执行流程对比 — 统一提示词

> 一次性喂给 AI，产出按差异点组织的逐项对比分析，标注风险等级和导致执行结果不一致的场景。

---

对比 **bcos-evm/opstack/**（`/bcos-evm/opstack/`）与 **op-geth**（`op-geth/core/`、`op-geth/core/types/`、`op-geth/params/`）的交易执行全流程。从交易入口开始，按执行阶段逐段展开。每个阶段：
1. 阅读 op-geth 对应源文件
2. 阅读 bcos-evm 对应文件（已在各阶段标注）
3. 列出所有差异点，标注风险等级：🔴 高（直接导致执行结果不一致）/ 🟡 中（边界条件可能不一致）/ 🟢 低（实现细节差异但语义等价）
4. 对每个差异点给出导致结果不一致的具体输入场景

---

## 阶段 1：交易入口与顶层流程

- **op-geth**: `core/state_processor.go` → `ApplyTransaction`, `core/state_transition.go` → `TransitionDb()`
- **bcos-evm**: `OpStackTxLifecycle.cpp` → `runOpStackTxLifecycle`, `OpStackExecutionBridge.cpp` → `opStackExecute`

对比要点：
- 整个生命周期的阶段划分和顺序
- deposit tx / normal tx 两条分支的判断条件
- 是否有任何阶段在一边存在而另一边缺失
- gasPool subGas/returnGas 的调用点

---

## 阶段 2：Precheck — 入口校验

- **op-geth**: `core/state_transition.go` → `preCheck()`, `core/types/transaction.go` → `Validate()`
- **bcos-evm**: `OpStackPrecheckPolicy.cpp` → `checkEntryRules`, `OpStackPrecheckPolicy.cpp` → `checkGasAffordable`

逐项核查：

1. **Nonce 检查**：bcos-evm `checkEntryRules` 检查 `stateNonce != expectedNonce`，deposit tx 跳过 nonce。op-geth 的 nonce 检查何时发生？错误返回码是否一致？

2. **EIP-1559 gas caps**：`gasFeeCap < gasTipCap` 或 `gasFeeCap < baseFee` → reject。两边返回值/错误码是否相同？

3. **EIP-7702 sender code 检查**：bcos-evm 检查 sender 有 code 但不是合法 delegation → `Malformed`。op-geth 对应检查在哪里？

4. **Blob tx 校验**（`OpStackBlobTxIntent.h` + `checkEntryRules`）：
   - 非 EIP-4844 fork → reject
   - CREATE + blob → reject
   - `blobVersionedHashes` 为空 → reject
   - versioned hash prefix != 0x01 → reject
   - `blobGasFeeCap < blobBaseFee` → `InsufficientFunds`（注意：bcos-evm 此处返回 InsufficientFunds 而非 Malformed）
   - op-geth 对应每次检查的错误码是否一致？

5. **Authorization 校验**：EIP-7702 auth + CREATE → `Malformed`；`authorizationListPresent==true` 但 authorizations 为空 → `Malformed`。op-geth 对应逻辑？

6. **Floor gas 检查**（`OpStackFloorGasPrecheck.cpp`）：bcos-evm 在 `checkGasAffordable` 中调用 `opStackFloorGasPrecheck`，同时检查 `canTransfer(…value)`。这与 op-geth 的 floor gas / balance check 的位置和时机是否一致？

7. **op-geth 特有而 bcos-evm 缺失的检查项**？反之亦然？

---

## 阶段 3：Intrinsic Gas 计算与扣减

- **op-geth**: `core/types/transaction.go` → `IntrinsicGas()`, `core/state_transition.go` 中 intrinsic gas debit
- **bcos-evm**: `TxIntrinsicGas.h` → `computeTxIntrinsicGas`, `DebitIntrinsicGas.h` → `debitIntrinsicGas`（`OpStackEntry` 模式）

逐项核查：

1. **基础 gas**：`TX_BASE_GAS`（21000）

2. **Calldata 字节计费**：零字节 4 gas / 非零字节 16 gas。bcos-evm 的 `calcEip7623Components` 是否与 op-geth 的 `IntrinsicGas` 对齐？

3. **Contract creation intrinsic**：`CREATE_BASE_GAS` + `INITCODE_WORD_GAS * ceil(len/32)`，word 边界取整方向（向上取整）是否一致？

4. **Access list 成本**：地址 gas（2400）+ 每个 key gas（1900），值与 op-geth `params/protocol_params.go` 是否对齐？

5. **EIP-7702 auth tuple**：`PER_EMPTY_ACCOUNT_COST`（25000），是否与 op-geth 对齐？

6. **🔴 floor data gas 扣减时序（关键差异）**：
   - op-geth：floor data gas 在 intrinsic 计算阶段统一处理
   - bcos-evm `OpStackEntry` 模式：仅 debit `preExecutionDebit() + authCost`，floor gas 在后续 `opStackFloorGasPrecheck` 中单独检查
   - 这种时序分离是否会导致：某笔交易的 `gasLimit` 满足 intrinsic 检查但被后续 floor check 拒绝？反之是否有可能通过 intrinsic 但 floor check 公式不同导致多余 gas 被错误扣除？

7. **`OpStackEntry` 模式 vs op-geth intrinsic check**：bcos-evm 的 `OpStackEntry` 模式只检查 `message.gas >= totalDebit`，不做 EIP-7623 的 `max(intrinsic, 21000 + floor)` 比较。op-geth 做了吗？如果做了，差异在什么交易上会触发？

---

## 阶段 4：buyGas — 预扣费用与余额检查

- **op-geth**: `core/state_transition.go` → `buyGas()`
- **bcos-evm**: `OpStackTxFeeLedger.cpp` → `OpStackTxFeeLedger::buyGas`

逐项核查：

1. **🔴 余额检查公式（关键差异）**：
   - op-geth 使用保守公式：`gasFeeCap * gasLimit + value + l1Cost + blobFee`
   - bcos-evm 主检查使用：`effectiveGasPrice * gasLimit + l1Cost + operatorCost + blobCost + value`
   - 当 `effectiveGasPrice < gasFeeCap`（即 `tip + base < feeCap`）时，bcos-evm 可能低估所需余额，导致某笔交易在 bcos-evm 通过余额检查但在 op-geth 被拒绝。反之，bcos-evm 还有一条 `hasGasFeeCap` 分支使用 `gasFeeCap` 保守检查 —— 这个分支什么条件下触发？两条路径是否覆盖全部场景？

2. **L1 Cost 计算**（`OpStackFee.h` + `RollupCost.h`）：
   - Fjord 公式：`L1_COST_INTERCEPT`(-42585600) / `FASTLZ_COEF`(836500) / `FJORD_DIVISOR`(1000000000000) / `MIN_TX_SIZE_SCALED`(100000000)
   - FastLZ 压缩长度 `flzCompressLen` 算法与 op-geth `core/types/rollup_cost.go` 是否逐位对齐？
   - zero/one 字节计数：`newRollupCostData` 的统计方式与 op-geth 是否一致？
   - scalar 参数从 L1Block 合约 `L1_FEE_SCALARS_SLOT`（slot 3）读取，解析方式是否与 op-geth 对齐？

3. **Blob gas 扣除**：`OP_BLOB_GAS_PER_BLOB` = 131072，blob balance check 使用 `blobGasUsed * blobGasFeeCap` —— 与 op-geth 一致吗？

4. **Operator cost（Isthmus）**：`operatorCostIsthmus` 公式与 op-geth Isthmus fork 的 operator fee 计算是否对齐？

5. **effectiveGasPrice 计算**：bcos-evm `resolveEffectiveGasPrice` = `min(tipCap + baseFee, feeCap)`。与 op-geth 的 `effectiveGasTip()` / `effectiveGasPrice` 公式一致吗？Legacy tx（无 feeCap）的兼容处理？

---

## 阶段 5：EVM 执行层

- **op-geth**: `core/vm/evm.go`, `core/state_transition.go` 中 EVM 调用
- **bcos-evm**: `ExecuteMessage.cpp` → `executeMessage` → `TxExecutionAdapter::run`, `TxPipeline.cpp` → `runTxPipeline`

对比要点：

1. **EIP-2929 地址预热**：bcos-evm 在 `TxPipelineContext` 构造中调用 `setWarmDestinationFromKind` 预热 sender/recipient。op-geth 预热了哪些地址（sender、recipient、coinbase、access list）？差异是否影响 `BALANCE`/`EXTCODEHASH`/`CALL` 的 gas 成本？

2. **EIP-3651 warm coinbase**：op-geth 预热 coinbase，bcos-evm 预热了 coinbase 吗？

3. **Access list 预热**：bcos-evm 的 `IntrinsicGasPolicy.accessList` 传入 pipeline，但 access list 中的地址和 key 在执行中是否被实际预热？

4. **OpStack chain call-target adapter**：bcos-evm 在 `OpStackTxLifecycle.cpp:97` 设置 `OpStack chain call-target adapter(&ctx.state, blockInfo.baseFee)`。当 EVM 访问指定预编译地址（如 L1Block → `OP_L1_BLOCK_PREDEPLOY`）时，EvmHostHooks 做了什么？与 op-geth 的 `OVM` 预编译/状态访问行为是否一致？

5. **EIP-7702 delegation 生效**：bcos-evm 在 `checkEntryRules` 中用 `parseDelegationTarget` 检查 sender code，但 delegation 实际替换 sender code 的时机在哪里？op-geth 在 EVM 执行前做 delegation code 替换 —— bcos-evm 是在何时完成的？

---

## 阶段 6：Gas Settlement — 执行后结算

- **op-geth**: `core/state_transition.go` 中 `TransitionDb()` 的 gas 结算部分
- **bcos-evm**: `OpStackGasSettlement.h` → `postExecuteGasSettlement`, `TxIntrinsicGas.h` → `settleTopLevelTransactionGas`, `OpStackSettlement.cpp` → `finalizeNormal` / `finalizeDeposit`

逐项核查：

1. **EIP-3529 refund cap**：`effectiveRefund = min(stateRefund, gasUsedBeforeRefund / 5)`。整数除法取整方向（向零取整、向下取整）是否一致？`stateRefund` 来源：op-geth 用 `state.GetRefund()`，bcos-evm 用 `ctx.evmcResult.gas_refund` —— 这两个值是否等价？

2. **`gasRemaining = min(gasLimit, gasLeft + cappedRefund)`**：gasLeft 是否有 clamp？`max(0, gasLeft)` 的处理两边一致吗？

3. **EIP-7623 floor uplift**：`gasUsed = max(gasUsed, floorDataGas)`，但不超过 gasLimit。当 `floorDataGas > gasLimit` 时，gasUsed 是 `gasLimit` 还是 `floorDataGas`？

4. **Deposit tx 结算特殊逻辑**：
   - bcos-evm `applyDepositPostExecuteSettlement` 传 `floorDataGas=0`
   - op-geth deposit tx 不使用 EIP-1559 的 refund 机制吗？
   - finalizeDeposit 按 evmStatus 分三种情况（SUCCESS / 非 SUCCESS 完成 / 非完成），op-geth 的 deposit 结算是否有同样的三条路径？

5. **不同 exitKind 的处理**：
   - `IntrinsicRejected` / `GasAffordRejected`：gasUsed=0, gasRemaining=gasLimit
   - `Completed` / `RulesRejected` / `ExceptionHandled`：apply 正常 post-execute settlement
   - op-geth 对不同失败路径的 gas 处理是否一致？

6. **`captureSettlementSnapshot`**：bcos-evm 在 pipeline 中捕获 `TxGasSettlementSnapshot`，这些 snapshot 数据后续在哪里被消费？op-geth 有对应机制吗？

---

## 阶段 7：Fee Routing — 费用分配（仅 normal tx）

- **op-geth**: `core/state_transition.go` → `refundGas()` 或 `TransitionDb()` 中的费用分配
- **bcos-evm**: `OpStackTxFeeLedger.cpp` → `OpStackTxFeeLedger::refundGas`

逐项核查：

1. **Sender refund**：`gasRemaining * effectiveGasPrice`，两边公式一致吗？

2. **Coinbase tip**：`gasUsed * max(0, effectiveGasPrice - baseFee)`。当 `effectiveGasPrice < baseFee` 时 tip=0，op-geth 相同？

3. **🔴 baseFee 收款方（OP Stack 特殊设计）**：
   - op-geth OP Stack：baseFee 转到 `0x4200…19` 还是烧毁？
   - bcos-evm：转到 `OP_BASE_FEE_RECIPIENT` (0x4200...19)
   - 确认 op-geth OP Stack 的 baseFee 接受地址是否与 bcos-evm `OP_BASE_FEE_RECIPIENT` 一致

4. **L1 Fee 收款方**：`OP_L1_FEE_RECIPIENT` (0x4200...1a)

5. **Operator Fee 收款方**（Isthmus）：`OP_OPERATOR_FEE_RECIPIENT` (0x4200...1b)。refund 时未使用的 operator cost 是否退回 sender？`refundIsthmusOperatorCost` 的逻辑与 op-geth 是否对齐？

6. **🔴 Deposit tx 费用处理**：`refundGas` 对 deposit tx 直接 `co_return`（不退费也不收任何费）。op-geth deposit tx 执行后的费用处理是怎样的？deposit tx 消耗的 gas 不产生费用，两边语义是否一致？

---

## 阶段 8：Deposit Tx 全流程语义

- **op-geth**: `core/state_transition.go` 中 `isDepositTx` 分支
- **bcos-evm**: `OpStackTxLifecycle.cpp` deposit 路径 + `OpStackSettlement.cpp` → `finalizeDeposit` + `settleDeposit`

逐项核查：

1. **Mint 时机**：bcos-evm 在 buyGas 前 mint（`OpStackTxLifecycle.cpp:134-138`），op-geth 在何时 mint？在 EVM 执行之前还是之后？

2. **Mint 金额为 0**：bcos-evm 当 `mint` 为 0 或缺失时跳过 mint 但不跳过执行。op-geth 处理方式一致？

3. **Checkpoint/revert 机制**：bcos-evm 对 deposit 做 checkpoint 然后 execute，失败 revert。op-geth 使用 journal/snapshot 机制吗？revert 语义是否等价？

4. **🔴 Deposit nonce（关键差异）**：bcos-evm 的 `finalizeDeposit` 在 **所有路径** 都 `nonce+1`（SUCCESS、非 SUCCESS 完成、gasUsed==gasLimit 的失败路径）。op-geth deposit tx 的 nonce 递增是否在所有路径都发生？特别关注：
   - EVM 执行 REVERT 时 nonce 是否递增？
   - intrinsic gas 不足（gasUsed == gasLimit）时 nonce 是否递增？
   - gas pool 不足导致提前退出时 nonce 是否递增？

5. **System tx 拒绝**：bcos-evm `checkEntryRules` 拒绝 `isSystemTransaction==true` 的 deposit tx。op-geth 如何处理 system deposit？是否也直接拒绝？

6. **Deposit gas pool**：deposit tx 也消耗 block gas pool，bcos-evm `acquireGasPool` 在 mint 之前调用。op-geth 的 deposit tx 是否也消耗 block gas pool？时机是否一致？

---

## 阶段 9：Error Handling — 错误映射

- **op-geth**: `core/state_transition.go` 中各错误路径的返回值
- **bcos-evm**: `OpStackPipelineInternals.h`, `OpStackOrchestrationErrorPolicy.h`, `OpStackPrecheckPolicy.cpp`

逐项核对每种错误场景的 (evmc_status_code, TransactionStatus) 映射是否一致：

| 错误场景 | bcos-evm | op-geth | 是否一致？ |
|----------|----------|---------|-----------|
| Intrinsic gas 不足 | `EVMC_OUT_OF_GAS` + `OutOfGasLimit` | | |
| buyGas 余额不足 | `EVMC_INSUFFICIENT_BALANCE` + `NotEnoughCash` | | |
| Gas pool 不足 | `EVMC_OUT_OF_GAS` + `OutOfGasLimit` | | |
| Nonce 不匹配 | `EVMC_FAILURE` + `NonceCheckFail` | | |
| EIP-1559 cap 违反 | `EVMC_FAILURE` + `Malformed` | | |
| Blob tx 约束违反 | `EVMC_FAILURE` + `Malformed` | | |
| BlobGasFeeCap < BlobBaseFee | `EVMC_FAILURE` + `InsufficientFunds` | | |
| EIP-7702 sender code | `EVMC_FAILURE` + `Malformed` | | |
| Pipeline 异常 | `EVMC_INTERNAL_ERROR` + `Unknown` | | |

---

## 阶段 10：Fork 激活与时间戳

- **op-geth**: `core/types/rollup_config.go` → `IsFjord` / `IsIsthmus`
- **bcos-evm**: `OpStackForkSchedule.h` → `isOpStackFjord` / `isOpStackIsthmus`

对比要点：
1. Fork 激活条件：bcos-evm 使用 `forkTime.has_value() && *forkTime <= blockTime`。op-geth 的 fork 激活判断是否有额外条件（如 chain config 中的 `!= nil` + 时间比较）？
2. bcos-evm 默认 `makeIsthmusPlusForkSchedule()` 直接激活 fjord + isthmus。生产环境 fork 时间戳从哪里读入？是否可能因为 fork 时间不一致导致同一笔交易走不同代码路径？
3. Isthmus fork 对执行流程的修改点：operator fee、L1Block 属性长度，两边是否对齐？

---

## 阶段 11：常数对齐

- **bcos-evm**: `OpStackConstants.h`
- **op-geth**: `params/protocol_params.go`, `core/types/rollup_cost.go`

逐项对比以下常量值：

| 常量 | bcos-evm 值 | op-geth 对应 | 是否一致？ |
|------|------------|-------------|-----------|
| TX_BASE_GAS | ? | 21000 | |
| ACCESS_LIST_ADDRESS_COST | ? | 2400 | |
| ACCESS_LIST_STORAGE_KEY_COST | ? | 1900 | |
| PER_EMPTY_ACCOUNT_COST | ? | 25000 | |
| BLOB_GAS_PER_BLOB | 131072 | | |
| OP_BASE_FEE_RECIPIENT | 0x4200...19 | | |
| OP_L1_FEE_RECIPIENT | 0x4200...1a | | |
| OP_OPERATOR_FEE_RECIPIENT | 0x4200...1b | | |
| L1_COST_INTERCEPT | -42'585'600 | | |
| L1_COST_FASTLZ_COEF | 836'500 | | |
| MIN_TX_SIZE_SCALED | 100'000'000 | | |
| FJORD_DIVISOR | 1'000'000'000'000 | | |
| ISTHMUS_L1_ATTRIBUTES_LEN | 176 | | |
| L1_BASE_FEE_SLOT | 1 | | |
| L1_FEE_SCALARS_SLOT | 3 | | |
| OPERATOR_FEE_PARAMS_SLOT | 8 | | |

---

## 阶段 12：收据元数据（ReceiptMeta）

- **bcos-evm**: `OpStackReceiptMeta.h`, `OpStackTxLifecycle.cpp` → `projectNormalReceiptMeta`
- **op-geth**: receipt 结构中的对应字段

对比要点：
1. `l1Fee`：bcos-evm 使用 `feeCtx.m_l1CostCharged`，即 buyGas 中计算并扣除的 L1 cost。op-geth receipt 中的 L1 fee 来源是否相同？
2. `operatorFee`（Isthmus）：bcos-evm 仅在 `isOpStackIsthmus && operatorCostFunc != null` 时记录。op-geth 是否也有 operator fee 的 receipt 字段？
3. `depositNonce`（deposit tx）：bcos-evm 在 deposit 路径记录 depositNonce。op-geth receipt 是否有对应字段？
4. op-geth receipt 有无 bcos-evm 缺失的字段（如 DepositReceiptVersion、DepositNonce 的各种变体）？

---

## 输出格式要求

对上述 12 个阶段中的每一个差异点，按以下格式输出：

```markdown
### [阶段 X] — [差异主题]

**风险等级**: 🔴/🟡/🟢

**op-geth 行为**:
<具体代码/逻辑描述>

**bcos-evm 行为**:
<具体代码/逻辑描述，标注文件路径和行号>

**差异分析**:
<为什么会有差异，根源是什么>

**触发场景**:
<哪些具体的交易输入会导致两边执行结果不一致？给出一组具体的交易参数>

**修复建议（可选）**:
<如果差异需要修复，建议的修改方向>
```
