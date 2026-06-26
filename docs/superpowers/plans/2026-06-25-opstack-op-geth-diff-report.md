# bcos-evm/opstack/ vs op-geth 交易执行流程对比报告

> 对比日期: 2026-06-25
> bcos-evm: `feat-evm-refactor` worktree, `bcos-evm/opstack/` + `bcos-evm/eth/`
> op-geth: `/Users/octopus/octo/code/blockchain-impl/op-geth`, develop branch

---

## 总体流程对齐情况

### 阶段对比总览

| 步骤 | op-geth (`core/state_transition.go`) | bcos-evm (`OpStackTxLifecycle.cpp`) | 对齐？ |
|------|--------------------------------------|-------------------------------------|--------|
| 1. Entry rules (nonce, EIP-1559, 7702, blob) | `preCheck()` | `checkEntryRules()` | ✅ |
| 2. buyGas / gasPool | `buyGas()` at end of `preCheck()` | separate `acquireGasPool` + `buyGas` | ✅ |
| 3. Intrinsic gas debit | `innerExecute` lines 539-563 | `debitIntrinsicGas` inside `runTxPipeline` | ✅ |
| 4. Floor data gas check | `innerExecute` lines 547-555（intrinsic 扣减**前**，用原始 `msg.GasLimit`） | `checkGasAffordable` → `opStackFloorGasPrecheck`（intrinsic 扣减**前**，用 `ctx.originalGasLimit`） | ✅ |
| 5. Balance+Value check | `innerExecute` lines 574-580 | `checkBalanceAndValue` inside `runTxPipeline` | ✅ |
| 6. EIP-7702 apply | `innerExecute` lines 605-610 | `apply7702TxAuthorizationsIfNeeded` in `TxExecutionAdapter` | ✅ |
| 7. EVM execution | `evm.Call` / `evm.Create` | `executeMessage` → `TxExecutionAdapter::run` | ✅ |
| 8. Gas settlement (refund cap + floor uplift) | `innerExecute` lines 646-662 | `settleTopLevelTransactionGas` + `postExecuteGasSettlement` | ✅ |
| 9. Fee refund (sender/coinbase/L1/operator) | `innerExecute` lines 691-735 | `refundGas` (`OpStackTxFeeLedger.cpp:121`) | ✅ |
| 10. Receipt projection | implicit in result | `projectNormalReceiptMeta` | 🟡 |

---

## 🔴 高优先级差异

### 🔴 差异 1: Deposit tx 非 Call 路径的 Nonce 递增

**风险等级**: 🔴 高

**op-geth 行为**:
在 `innerExecute()` line 602 处，nonce 递增仅发生在 Call 路径（非 contract creation）:
```go
if contractCreation {
    ret, _, st.gasRemaining, vmerr = st.evm.Create(...)
} else {
    st.state.SetNonce(msg.From, st.state.GetNonce(msg.From)+1, ...)
    // apply authorizations...
    ret, st.gasRemaining, vmerr = st.evm.Call(...)
}
```
对于 deposit tx 且 `msg.To == nil`（contract creation 路径），nonce 不会在 innerExecute 中递增。仅在 `execute()` 的 failed-deposit 恢复分支 (line 493) 中递增。

**bcos-evm 行为**:
`finalizeDeposit` (`OpStackSettlement.cpp:57-88`) 中，**所有三个路径** 都执行 `ctx.state.set_nonce(sender, get_nonce(sender) + 1)`:
- SUCCESS 路径: commit → nonce+1
- 非 SUCCESS 完成路径: revert → nonce+1
- 非完成路径 (gasUsed == gasLimit): revert → nonce+1

**差异分析**:
op-geth 对 deposit + Create 成功路径不递增 nonce；bcos-evm 对所有 deposit 路径都递增 nonce。虽然 deposit tx 通常一定是 call（有 `To` 地址），但如果有 deposit + Create 的边缘情况，两边 nonce 不一致。

**触发场景**:
一个 deposit tx 的 `To` 地址为 nil 且 `input` 是 contract creation data 时（即 L1→L2 deposit 创建合约），成功执行后：
- op-geth: nonce 不递增
- bcos-evm: nonce 递增 +1

**修复建议**:
OP Stack 规范中 deposit tx 是否允许 Create？如果允许，需确认规范行为。如果 OP Stack 不允许 deposit Create，bcos-evm 的当前行为反而是安全的（多递增 nonce 不影响正确性，因为 deposit nonce 不会被再次检查）。

---

### 🔴 差异 2: Legacy tx 的 effectiveGasPrice 计算（边界条件）

**风险等级**: 🔴 高（如果 OP Stack 支持 legacy tx）

**op-geth 行为**:
`TransactionToMessage()` line 204-209:
```go
if baseFee != nil {
    msg.GasPrice = msg.GasPrice.Add(msg.GasTipCap, baseFee)
    if msg.GasPrice.Cmp(msg.GasFeeCap) > 0 {
        msg.GasPrice = msg.GasFeeCap
    }
}
```
如果 baseFee 为 nil（pre-London），GasPrice 保持原始的 tx.GasPrice() 值，不做 effectiveGasPrice 转换。

**bcos-evm 行为**:
`populateFeeContext` (`OpStackTxLifecycle.cpp:46`):
```cpp
feeCtx.m_effectiveGasPrice = resolveEffectiveGasPrice(input.gasTipCap, input.gasFeeCap, input.blockInfo.baseFee);
```
`resolveEffectiveGasPrice` (`OpStackTxFeeLedger.cpp:22`):
```cpp
return std::min(gasTipCap + baseFee, gasFeeCap);
```
对 legacy tx（gasTipCap=0, gasFeeCap=0），`effectiveGasPrice = min(0 + baseFee, 0) = 0`。

**差异分析**:
如果 OP Stack 上存在 legacy tx（gasPrice 非零，但无 gasTipCap/gasFeeCap），bcos-evm 会将其 effectiveGasPrice 计算为 0，导致不收费。op-geth 则使用原始 gasPrice。

OP Stack post-Bedrock 要求所有 user tx 使用 EIP-1559 动态费用格式。如果上游（TE 层）保证将 legacy tx 的 gasPrice 正确填充到 gasFeeCap，则此差异不触发。

**触发场景**:
一笔 legacy tx（type=0），gasPrice=20 gwei，baseFee=10 gwei，但 gasTipCap/gasFeeCap 在 bcos-evm 输入中都为 0：
- op-geth: effectiveGasPrice = 20 gwei（原始 gasPrice）
- bcos-evm: effectiveGasPrice = 0

**修复建议**（二选一）:
1. **上游约定（推荐）**：TE 层在解码 legacy tx 时将 `gasPrice` 映射到 `gasFeeCap`/`gasTipCap`（EEST adapter 已采用此模式：`maxFeePerGas != 0 ? maxFeePerGas : tx.gasPrice`）。若 TE 已保证映射，**无需改 bcos-evm 代码**。
2. **执行层回退**：在 `populateFeeContext` / `buyGas` 中，当 `gasFeeCap == 0 && gasTipCap == 0` 时使用 legacy `gasPrice` 计算 effectiveGasPrice。注意：`OpStackExecutionRequest` **当前没有 `gasPrice` 字段**，且 `populateFeeContext` 固定 `m_hasGasFeeCap = true`，导致 `buyGas` 中的 legacy 回退路径（`!m_hasGasFeeCap`）在 OpStack 主路径上不可达；若走方案 2，需先在 request 中新增 `gasPrice` 或在 populate 阶段正确设置 `m_hasGasFeeCap`。

---

### 🔴 差异 3: Pre-Regolith System Deposit Tx 处理

**风险等级**: 🔴 高（仅影响 pre-Regolith 历史区块）

**op-geth 行为**:
`preCheck()` lines 346-361:
```go
if st.msg.IsDepositTx {
    st.initialGas = st.msg.GasLimit
    st.gasRemaining = st.msg.GasLimit
    if st.msg.IsSystemTx {
        if st.evm.ChainConfig().IsOptimismRegolith(st.evm.Context.Time) {
            return fmt.Errorf("%w: ...", ErrSystemTxNotSupported, ...)
        }
        return nil  // pre-Regolith: accept system deposit
    }
    return st.gp.SubGas(st.msg.GasLimit)
}
```
Pre-Regolith 环境下，system deposit tx 被接受且不消耗 gas pool。

**bcos-evm 行为**:
`checkEntryRules` (`OpStackPrecheckPolicy.cpp:64-68`):
```cpp
if (deposit) {
    if (m_input.depositTx.has_value() && m_input.depositTx->isSystemTransaction) {
        ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::Malformed);
        ctx.earlyExit = true;
    }
    return;
}
```
**无条件** 拒绝 system deposit tx。

**差异分析**:
对于 pre-Regolith 历史区块的重放，system deposit tx 在 op-geth 会成功但 bcos-evm 会失败。由于 bcos-evm 默认使用 `makeIsthmusPlusForkSchedule()`（假设 Isthmus 已激活），且所有当前 OP Stack 链都已远超 Regolith，此差异在实际使用中不会触发。

**触发场景**:
Pre-Regolith 区块中有一个 isSystemTransaction=true 的 deposit tx。

**修复建议**:
如果不需要支持 pre-Regolith 历史区块，当前行为可接受。否则需添加 Regolith fork 判断。

---

## 🟡 中优先级差异

### 🟡 差异 5: EIP-7702 authorization 应用的 nonce 副作用

**风险等级**: 🟡 中

**op-geth 行为**:
`innerExecute()` line 602: sender nonce 递增使用 `tracing.NonceChangeEoACall`。
Lines 605-610: 对每个 auth tuple 调用 `applyAuthorization`，其中包括 nonce 递增（`tracing.NonceChangeAuthorization`）。

**bcos-evm 行为**:
`apply7702TxAuthorizationsIfNeeded` (TxExecutionAdapter.cpp:61-85):
```cpp
state.checkpoint();
auto const senderNonce = state.get_nonce(input.message.sender);
state.set_nonce(input.message.sender, senderNonce + 1);
applyAuthorizations(state, input.authorizations, input.blockInfo.chainId);
// warmDelegationTarget...
state.commit();
```
Sender nonce 在 checkpoint 内递增，然后 apply authorizations（每个 authority nonce+1），然后 commit。如果在 applyAuthorizations 中发生异常，checkpoint/revert 会回滚 sender nonce 递增。

op-geth 则不同：sender nonce 在 line 602 递增（在 state snapshot 之外，不可回滚），authorizations 在 605-610 应用，如果 authorization 处理中出错？注意 op-geth 代码注释说 "Note errors are ignored, we simply skip invalid authorizations here."

**差异分析**:
op-geth 的 sender nonce 递增在 authorization apply 之前且不可回滚；bcos-evm 在 checkpoint 内，如果 authorization apply 有意外失败会回滚 sender nonce。但由于 applyAuthorization 中错误被忽略，此差异通常不触发。

**触发场景**:
当 authorization apply 导致严重错误需要回滚整个交易时，bcos-evm 会回滚 sender nonce 递增，op-geth 不会。

---

### 🟡 差异 6: EIP-1559 cap 检查覆盖范围

**风险等级**: 🟡 中

**op-geth 行为**:
`preCheck()` lines 392-414:
- 检查 `gasFeeCap` 和 `gasTipCap` 的 bit length 不超过 256
- 检查 `gasFeeCap >= gasTipCap`
- 检查 `gasFeeCap >= baseFee`
- 支持 `NoBaseFee` skip 逻辑（用于 eth_call）

**bcos-evm 行为**:
`checkEntryRules` (`OpStackPrecheckPolicy.cpp:95-99`):
```cpp
if (m_input.gasFeeCap < m_input.gasTipCap || m_input.gasFeeCap < m_input.blockInfo.baseFee) {
    ctx.evmcResult = makePreCheckError(protocol::TransactionStatus::Malformed);
    // ...
}
```
- 不检查 bit length 溢出
- 不支持 NoBaseFee skip
- 使用 `||`（或），即任一条件失败就 reject

**差异分析**:
1. bit length 检查缺失：如果 gasFeeCap 极端大（>2^256），op-geth 会 reject，bcos-evm 不会（但 bcos-evm 使用 u256，自动防止溢出）
2. NoBaseFee skip：eth_call 场景下 op-geth 可跳过 baseFee 检查，bcos-evm 不能

**触发场景**:
eth_call 模拟交易时 NoBaseFee=true 且 gasFeeCap=0, baseFee > 0:
- op-geth: 跳过检查，交易通过
- bcos-evm: reject

---

## 🟢 低优先级 / 已验证对齐

### ✅ 已验证对齐项

| 检查项 | op-geth 值 | bcos-evm 值 | 结果 |
|--------|-----------|-------------|------|
| TX_BASE_GAS | 21000 | `TX_BASE_GAS`=21000 | ✅ |
| CREATE intrinsic total | TxGasContractCreation=53000 | `TX_BASE_GAS`(21000) + `CREATE_BASE_GAS`(32000) = 53000 | ✅ 见差异 7 |
| INITCODE_WORD_GAS | 2 | 2 | ✅ |
| ACCESS_LIST_ADDRESS_COST | 2400 | 2400 | ✅ |
| ACCESS_LIST_STORAGE_KEY_COST | 1900 | 1900 | ✅ |
| PER_EMPTY_ACCOUNT_COST | CallNewAccountGas=25000 | `PER_EMPTY_ACCOUNT_COST`=25000 | ✅ |
| 7702 auth intrinsic | CallNewAccountGas=25000 × n | `PER_EMPTY_ACCOUNT_COST`=25000 × n | ✅ |
| PER_AUTH_BASE_COST (refund) | TxAuthTupleGas=12500 | `PER_AUTH_BASE_COST`=12500 | ✅ 仅用于 existence refund，非 intrinsic |
| BLOB_GAS_PER_BLOB | 131072 | `BLOB_GAS_PER_BLOB`=131072 | ✅ |
| TOKENS_PER_NONZERO_BYTE | 4 | `TOKENS_PER_NONZERO_BYTE`=4 | ✅ |
| TOTAL_COST_FLOOR_PER_TOKEN | 10 | `TOTAL_COST_FLOOR_PER_TOKEN`=10 | ✅ |
| TX_DATA_ZERO_GAS | 4 | `ZERO_BYTE_INTRINSIC_COST`=4 | ✅ |
| TX_DATA_NON_ZERO_GAS (EIP-2028) | 16 | `NONZERO_BYTE_INTRINSIC_COST`=16 | ✅ |
| L1_COST_INTERCEPT | -42_585_600 | `L1_COST_INTERCEPT`=-42'585'600 | ✅ |
| L1_COST_FASTLZ_COEF | 836_500 | `L1_COST_FASTLZ_COEF`=836'500 | ✅ |
| MIN_TX_SIZE_SCALED | 100_000_000 | `MIN_TX_SIZE_SCALED`=100'000'000 | ✅ |
| FJORD_DIVISOR | 1_000_000_000_000 | `FJORD_DIVISOR`=1'000'000'000'000 | ✅ |
| ISTHMUS_L1_ATTRIBUTES_LEN | 176 | `ISTHMUS_L1_ATTRIBUTES_LEN`=176 | ✅ |
| OP_BASE_FEE_RECIPIENT | 0x4200...19 | `OP_BASE_FEE_RECIPIENT`=0x4200...19 | ✅ |
| OP_L1_FEE_RECIPIENT | 0x4200...1A | `OP_L1_FEE_RECIPIENT`=0x4200...1a | ✅ |
| OP_OPERATOR_FEE_RECIPIENT | 0x4200...1B | `OP_OPERATOR_FEE_RECIPIENT`=0x4200...1b | ✅ |
| L1_BASE_FEE_SLOT | 1 | `L1_BASE_FEE_SLOT`=1 | ✅ |
| L1_FEE_SCALARS_SLOT | 3 | `L1_FEE_SCALARS_SLOT`=3 | ✅ |
| OPERATOR_FEE_PARAMS_SLOT | 8 | `OPERATOR_FEE_PARAMS_SLOT`=8 | ✅ |

---

### ✅ 差异 7: CREATE_BASE_GAS 常量命名（已对齐）

**风险等级**: 🟢 无（数值已对齐，仅命名/拆分方式不同）

**op-geth**:
```go
TxGasContractCreation uint64 = 53000  // protocol_params.go:50
```
IntrinsicGas 中: `gas = params.TxGasContractCreation` (53000)

**bcos-evm**:
```cpp
inline constexpr int64_t CREATE_BASE_GAS = 32'000;  // ProtocolGas.h:16
```
但 `computeTxIntrinsicGas` 中使用 `TX_BASE_GAS`=(21000) + `createIntrinsic` (32000 + 2*ceil(len/32)) = 53000 + 2*ceil(len/32)。

op-geth `IntrinsicGas` 中:
```go
if isContractCreation && isHomestead {
    gas = params.TxGasContractCreation  // 53000
}
// ... later, if isContractCreation && isEIP3860:
gas += lenWords * params.InitCodeWordGas
```

所以 op-geth: contract creation intrinsic = 53000 + 2*ceil(len/32)

bcos-evm:
```cpp
intrinsic.txBase = TX_BASE_GAS;  // 21000
intrinsic.createIntrinsic = calcCreateIntrinsic(message);  // 32000 + 2*ceil(len/32)
// preExecutionDebit = txBase + normalCalldata + accessListCost + createIntrinsic
// = 21000 + 0 + 0 + 32000 + 2*ceil(len/32)
// = 53000 + 2*ceil(len/32)
```

✅ **实际上对齐！** bcos-evm 通过 `txBase(21000) + createIntrinsic(32000 + words*2)` 得到与 op-geth `53000 + words*2` 相同的结果。`CREATE_BASE_GAS`=32000 不是直接作为 contract creation 的 base，而是与 `TX_BASE_GAS` 组合后等于 53000。

---

### ✅ 已验证对齐的逻辑

| 模块 | 对齐状态 | 备注 |
|------|---------|------|
| IntrinsicGas 公式 | ✅ | TX_BASE_GAS(21000) + calldata(4/16) + accessList(2400/1900) + create(32000 + 2*words) + auth(25000*n) |
| FloorDataGas 公式 | ✅ | tokens = 4*nz + z; floor = 21000 + tokens*10 |
| settleTopLevelTransactionGas | ✅ | refund cap = min(stateRefund, gasUsed/5); floor uplift = max(gasUsed, floorDataGas) |
| L1CostFjord 公式 | ✅ | estimatedSize = max(MIN, INTERCEPT + COEF*fastLz); l1Cost = estimatedSize * l1FeeScaled / DIVISOR |
| OperatorCostIsthmus 公式 | ✅ | fee = gas * scalar / 1e6 + constant |
| OperatorCostJovian 公式 | ✅ | fee = gas * scalar * 100 + constant（Jovian fork 门控） |
| GPO `isJovian` / `getOperatorFee` | ✅ | 按 `OpStackForkSchedule.jovianTime` + blockTime 分支 |
| Jovian L1 attributes setter | ✅ | selector `0x3db6be2b`，178B，`daFootprintGasScalar` 写入 slot 8 |
| effectiveGasPrice | ✅ | min(tipCap + baseFee, feeCap)；**前提**：gasTipCap/gasFeeCap 已正确填充（legacy 零 cap 见差异 2） |
| Floor data gas 检查时机 | ✅ | 均在 intrinsic 扣减前，用原始 gasLimit 与 floor 比较；失败时 error code 路径略有不同（`ErrFloorDataGas` vs `GasAffordRejected`） |
| FastLZ 压缩算法 | ✅ | `flzCompressLenImpl` 逐行对齐 op-geth `FlzCompressLen` |
| RollupCostData 零一字节统计 | ✅ | `newRollupCostData` 与 op-geth `NewRollupCostData` 完全对齐 |
| Fee scalar 解析 | ✅ | `readBigEndianU32` 从 L1_FEE_SCALARS_SLOT bytes[16:24) 读取，对齐 op-geth `ExtractEcotoneFeeParams` |
| OperatorFeeParams 解析 | ✅ | scalar 从 bytes[20:24) 读取，constant 从 bytes[24:32) 读取，对齐 op-geth `ExtractOperatorFeeParams` |
| EIP-3529 refund cap | ✅ | refund = min(stateRefund, peakGasUsed / 5) |
| Blob balance check | ✅ | blobGasUsed * blobGasFeeCap（保守检查），实际扣除 blobGasUsed * blobBaseFee |

---

## 🟢 差异 8: System tx gas reporting（pre-Regolith）

**风险等级**: 🟢 低（仅影响 pre-Regolith）

**op-geth 行为**:
pre-Regolith deposit system tx: `gasUsed = 0`（无论实际消耗多少 gas）

**bcos-evm 行为**:
system deposit 直接被 `checkEntryRules` 拒绝。

**差异分析**:
由于 bcos-evm 根本不支持 pre-Regolith system deposit，不存在 gas reporting 差异。所有当前 OP Stack 链均为 post-Regolith，system deposit 应当被拒绝。

---

### ✅ 已对齐：差异 9 — Operator cost Jovian fork 支持

**原风险等级**: 🟢 低（Jovian 尚未激活）→ **已实现**（2026-06-26）

**op-geth 行为**:
Jovian fork 使用 `newOperatorCostFuncOperatorFeeFix`:
```go
fee = gas * scalar * 100 + constant  // (no division by 1e6)
```

**bcos-evm 行为**（`OpStackFee.cpp`）:
```cpp
fee = gas * scalar * 100 + constant  // operatorCostJovian, gated by isOpStackJovian
```

**对齐状态**: ✅ `operatorCostJovian` + `makeCachedOperatorCostFunc` fork 分支；GPO `isJovian`/`getOperatorFee`；Jovian L1 attributes setter `0x3db6be2b`。见 `docs/superpowers/plans/2026-06-26-opstack-jovian.md`。

---

## 汇总

### 🔴 需要关注的高优差异

| # | 差异 | 影响 | 触发条件 |
|---|------|------|---------|
| 1 | Deposit Create nonce | nonce 不一致 | deposit tx + To=nil（罕见） |
| 2 | Legacy tx effectiveGasPrice=0 | 费用计算完全错误 | legacy tx 传入且 gasFeeCap/gasTipCap 未设置 |
| 3 | Pre-Regolith system deposit | 历史区块重放失败 | pre-Regolith 区块（几乎不会遇到） |
| 6 | NoBaseFee skip 缺失 | eth_call 可能误 reject | NoBaseFee=true + 低 gasFeeCap |

### 🟡 后续需要关注

| # | 差异 | 影响 |
|---|------|------|
| 5 | 7702 nonce 回滚语义 | 异常路径下行为差异 |

### ✅ 已确认对齐（无需代码变更）

| # | 项 | 说明 |
|---|-----|------|
| 4 | Floor data gas 检查 | 均在 intrinsic 扣减前用原始 gasLimit 比较，行为等价 |
| 7 | CREATE intrinsic gas | `21000 + 32000 = 53000`，与 op-geth `TxGasContractCreation` 一致 |
| 9 | Jovian operator fee | `gas * scalar * 100 + constant`；GPO + L1 attributes 178B setter |

### 整体评估

bcos-evm/opstack 的实现与 op-geth **高度对齐**。核心执行逻辑（intrinsic gas、floor data gas、L1 cost、gas settlement、fee routing）均已验证数值一致。主要风险集中在：
1. **Legacy tx 兼容**：需确认 TE 层是否保证 gasFeeCap/gasTipCap 填充（见差异 2 修复建议）
2. **Deposit 边缘路径**：deposit Create 的 nonce 行为

> **修订记录**（2026-06-26）：修正差异 4 floor gas 时序描述（op-geth 同样在 intrinsic 扣减前检查）；澄清 effectiveGasPrice / PER_AUTH 常量适用条件；补充差异 2 修复前提（`OpStackExecutionRequest` 无 `gasPrice` 字段）；差异 7 移入已对齐项；**差异 9 Jovian operator fee 已实现**（见 `2026-06-26-opstack-jovian.md`）。

---

## 附录：多子代理核查结果（2026-06-26）

以下由 5 个子代理并行核查原文 9 项差异及所有常量/公式对齐声明。每个核查结论均附源文件行号证据。

---

### 核查 A：差异 1 — Deposit tx nonce 递增

**原文声明**：op-geth 对 deposit+Create 成功路径不递增 nonce；bcos-evm 对所有 deposit 路径都递增 nonce。

**核查结论**：🔴 **部分证伪** — 原文关于 bcos-evm 的描述正确，但关于 op-geth 的描述遗漏了关键细节。

**op-geth 证据**：
- `core/state_transition.go:598-602`：`innerExecute()` 中 nonce 递增在 `else` 分支（非 contract creation），deposit+Create 不经过此处
- `core/vm/evm.go:530`：`evm.create()` 内部**确实递增** nonce — `evm.StateDB.SetNonce(caller, nonce+1, ...)`
- `core/state_transition.go:486-493`：`execute()` 中**失败** deposit 的恢复路径**也递增** nonce

因此 op-geth 在所有 deposit 路径（包括 Create 成功路径）实际都会递增 nonce，只是通过 `evm.create()` 内部而非显式的 `st.state.SetNonce(…)` 调用。

**bcos-evm 证据**：
- `opstack/OpStackSettlement.cpp:57-88`：`finalizeDeposit` 在所有三个路径均执行 `set_nonce(sender, get_nonce(sender) + 1)`
- `opstack/OpStackPrecheckPolicy.cpp:173-175`：deposit tx 通过 `skipTopLevelSenderNonceBump = true` 抑制 EVM 层的 nonce 递增

**实际语义**：两者最终都会递增 deposit Create 的 nonce。差异是架构性的：op-geth 在 EVM 层递增（`NonceChangeContractCreator`），bcos-evm 抑制 EVM 层而集中在 `finalizeDeposit` 中统一处理。

---

### 核查 B：差异 2 — Legacy tx effectiveGasPrice=0

**原文声明**：legacy tx 传入时 `gasTipCap=0, gasFeeCap=0`，bcos-evm 会算出 `effectiveGasPrice=0`。

**核查结论**：🟡 **部分证伪 — 生产路径安全，但不经过 fillGasCaps 的直接调用有风险**

**关键发现**：生产入口 `OpStackTransactionExecutorImpl.h:212` 在调用 `opStackExecute()` 之前会调用 `fillGasCaps()`：

- `OpStackTxInputBuilder.h:92-104`：`fillGasCaps` 正常化逻辑：
  ```cpp
  if (input.gasFeeCap == 0) { input.gasFeeCap = protocol::effectiveGasPrice(tx); }
  if (input.gasTipCap == 0) { input.gasTipCap = input.gasFeeCap; }
  ```
- `bcos-framework/bcos-framework/protocol/Transaction.h:207-226`：`protocol::effectiveGasPrice(tx)` 对 legacy tx 读取 `tx.gasPrice()` → 正确填充

**函数级漏洞**：`resolveEffectiveGasPrice(0, 0, baseFee)` = `min(0+baseFee, 0)` = **0**。对任何绕过 `fillGasCaps` 构造 `OpStackExecutionRequest` 的调用方，此漏洞确实存在。op-geth 通过 tx 类型自身保证 `gasFeeCap()`/`gasTipCap()` 对 legacy tx 返回正确的 gasPrice，而 bcos-evm 依赖外部预处理。

---

### 核查 C：差异 3 — Pre-Regolith System Deposit

**原文声明**：bcos-evm 无条件拒绝 system deposit；op-geth 仅在 post-Regolith 拒绝。

**核查结论**：✅ **完全证实**

**op-geth 证据**：
- `core/state_transition.go:347-361`：
  ```go
  if st.msg.IsSystemTx {
      if st.evm.ChainConfig().IsOptimismRegolith(st.evm.Context.Time) {
          return fmt.Errorf("%w: ...", ErrSystemTxNotSupported, ...)  // 拒绝
      }
      return nil  // pre-Regolith: 接受
  }
  ```

**bcos-evm 证据**：
- `opstack/OpStackPrecheckPolicy.cpp:62-70`：
  ```cpp
  if (deposit) {
      if (m_input.depositTx.has_value() && m_input.depositTx->isSystemTransaction) {
          ctx.evmcResult = makePreCheckError(...Malformed);  // 无条件拒绝
          ctx.earlyExit = true;
      }
  }
  ```
  无 fork 判断 — 完全不区分 Regolith 前后。

---

### 核查 D：差异 4 — Floor data gas 检查时序

**原文声明**：op-geth 在 intrinsic 扣减**之后**检查；bcos-evm 在**之前**检查；但均使用原始 gasLimit 比较，结果相同。

**核查结论**：🟡 **原文的"之后"描述有误 — 两者均在 intrinsic 扣减之前检查**

**op-geth 证据**：
- `core/state_transition.go:548-563`：
  ```go
  // line 548-555: floorDataGas 检查 — 用 msg.GasLimit（原始 gasLimit）
  floorDataGas, err = FloorDataGas(msg.Data)
  if msg.GasLimit < floorDataGas { ... }
  // line 563: intrinsic 扣减发生在此之后
  st.gasRemaining -= gas
  ```

**bcos-evm 证据**：
- `opstack/OpStackPrecheckPolicy.cpp:155-158`：用 `ctx.originalGasLimit` 调用 `opStackFloorGasPrecheck`
- `opstack/fee/OpStackFloorGas.cpp:62`：`if (gasLimit < floorGasValue)` — 同样检查原始 gasLimit

**结论**：两者在 intrinsic 扣减前用原始 gasLimit 比较 floorDataGas，行为完全等价。原文"时序差异"不成立。已在报告阶段对比总览中修正。

---

### 核查 E：差异 5 — EIP-7702 authorization 的 nonce 回滚语义

**原文声明**：bcos-evm 将 sender nonce 递增包裹在 checkpoint 内（可回滚），op-geth 则不然。

**核查结论**：✅ **结构上证实，但实际行为等价**

**op-geth 证据**：
- `core/state_transition.go:602`：`st.state.SetNonce(msg.From, st.state.GetNonce(msg.From)+1, ...)` — 直接在 state 上递增，无 snapshot/journal 包裹
- `core/state_transition.go:605-610`：authorization 应用循环，注释明确说 "Note errors are ignored"

**bcos-evm 证据**：
- `eth/execution/TxExecutionAdapter.cpp:61-84`：
  ```cpp
  state.checkpoint();
  state.set_nonce(input.message.sender, senderNonce + 1);  // checkpoint 内
  applyAuthorizations(state, input.authorizations, ...);     // checkpoint 内
  state.commit();                                            // commit
  ```
- `eth/Eip7702.cpp:156-211`：`applyAuthorizations` 内部以 `continue` 静默跳过无效 authorization，永不抛异常

**结论**：结构差异确实存在，但由于两个代码库都对 authorization 错误进行静默忽略，结构差异不产生实际的 nonce 分歧。

---

### 核查 F：差异 6 — EIP-1559 cap 检查覆盖范围

**原文声明**：bcos-evm 缺少 bit-length 检查和 NoBaseFee skip。

**核查结论**：✅ **完全证实**

**op-geth 证据**：
- `core/state_transition.go:394`：`skipCheck := st.evm.Config.NoBaseFee && msg.GasFeeCap.BitLen() == 0 && msg.GasTipCap.BitLen() == 0` — 完全跳过所有 EIP-1559 校验
- `core/state_transition.go:396-403`：显式的 `BitLen() > 256` 检查

**bcos-evm 证据**：
- `opstack/OpStackPrecheckPolicy.cpp:95`：`if (m_input.gasFeeCap < m_input.gasTipCap || m_input.gasFeeCap < m_input.blockInfo.baseFee)` — 无 NoBaseFee 短路，无 bit-length 检查
- `opstack/OpStackTxLifecycle.cpp:50`：`m_noBaseFee` 仅用于 `refundGas`，永远不进 `checkEntryRules`

**缓解因素**：`bcos::u256` 是固定 256 位类型，结构上无法溢出 >256 位，bit-length 检查在 C++ 中非必要。但 NoBaseFee skip 缺失在 `eth_call` 场景中确实会导致误拒（`gasFeeCap=0, baseFee>0` 时）。

---

### 核查 G：差异 7 — CREATE_BASE_GAS 常量等价性 + 全部常量对齐

**原文声明**：`CREATE_BASE_GAS=32000` + `TX_BASE_GAS=21000` = `53000` = op-geth `TxGasContractCreation`。

**核查结论**：✅ **完全证实 + 全部 11 个常量逐项核对一致**

**Intrinsic Gas 计算演练**（contract creation, 空 calldata）：

| 组件 | op-geth | bcos-evm |
|------|---------|----------|
| 基础 gas | `TxGasContractCreation`=**53000** | `TX_BASE_GAS(21000)` + `CREATE_BASE_GAS(32000)` = **53000** |
| Init code words | `ceil(0/32) * 2` = **0** | `ceil(0/32) * 2` = **0** |
| **合计** | **53000** | **53000** ✅ |

**常量对照表**（全部 ✅）：

| 常量 | op-geth 文件:行 | op-geth 值 | bcos-evm 文件:行 | bcos-evm 值 |
|------|----------------|-----------|-----------------|------------|
| TX_BASE_GAS | `params/protocol_params.go:49` | `TxGas = 21000` | `eth/gas/ProtocolGas.h:15` | `TX_BASE_GAS = 21'000` |
| ACCESS_LIST_ADDRESS_COST | `params/protocol_params.go:110` | `TxAccessListAddressGas = 2400` | `eth/gas/ProtocolGas.h:18` | `ACCESS_LIST_ADDRESS_COST = 2'400` |
| ACCESS_LIST_STORAGE_KEY_COST | `params/protocol_params.go:111` | `TxAccessListStorageKeyGas = 1900` | `eth/gas/ProtocolGas.h:19` | `ACCESS_LIST_STORAGE_KEY_COST = 1'900` |
| PER_EMPTY_ACCOUNT_COST | `params/protocol_params.go:48` | `CallNewAccountGas = 25000` | `eth/Eip7702.h:14` | `PER_EMPTY_ACCOUNT_COST = 25'000` |
| PER_AUTH_BASE_COST | `params/protocol_params.go:112` | `TxAuthTupleGas = 12500` | `eth/Eip7702.h:12` | `PER_AUTH_BASE_COST = 12'500` |
| BLOB_GAS_PER_BLOB | `params/protocol_params.go:206` | `BlobTxBlobGasPerBlob = 1<<17 = 131072` | `eth/gas/Eip4844.h:28` | `BLOB_GAS_PER_BLOB = 131'072` |
| L1_COST_INTERCEPT | `core/types/rollup_cost.go:92` | `-42_585_600` | `opstack/OpStackConstants.h:48` | `-42'585'600` |
| L1_COST_FASTLZ_COEF | `core/types/rollup_cost.go:93` | `836_500` | `opstack/OpStackConstants.h:49` | `836'500` |
| MIN_TX_SIZE_SCALED | `core/types/rollup_cost.go:95-96` | `100 * 1e6 = 100,000,000` | `opstack/OpStackConstants.h:50` | `100'000'000` |
| FJORD_DIVISOR | `core/types/rollup_cost.go:89` | `1_000_000_000_000` | `opstack/OpStackConstants.h:51` | `1'000'000'000'000` |
| ISTHMUS_L1_ATTRIBUTES_LEN | `core/types/rollup_cost.go:46` | `176` | `opstack/OpStackConstants.h:53` | `176` |

---

### 核查 H：Gas Settlement 公式（补充核查，原代理 4 被拒绝后手动完成）

**核查结论**：✅ **全部匹配**

| 子公式 | op-geth | bcos-evm | 对齐？ |
|--------|---------|----------|--------|
| EIP-3529 refund cap | `st.gasUsed() / RefundQuotientEIP3529` (= /5)，min vs `state.GetRefund()` — `state_transition.go:807-816` | `effectiveRefundEip3529`: `min(stateRefund, gasUsed/5)` — `TxIntrinsicGas.h:113-119` | ✅ |
| floor uplift (gasUsed) | `st.gasRemaining = st.initialGas - floorDataGas` → `gasUsed = floorDataGas` — `state_transition.go:650-654` | `gasUsed = clamp(max(gasUsed, floorDataGas), gasLimit)` — `TxIntrinsicGas.h:140-143` | ✅ |
| floor uplift (peakGasUsed) | `peakGasUsed = max(peakGasUsed, floorDataGas)` — `state_transition.go:659-661` | `settlement.maxUsedGas = max(peakGasUsed, settlement.gasUsed)` — `OpStackGasSettlement.h:31` | ✅ |

---

### 核查 I：Fee Routing 全链路（补充核查）

**核查结论**：✅ **全部匹配**

| 费用流 | op-geth (`state_transition.go`) | bcos-evm (`OpStackTxFeeLedger.cpp`) |
|--------|------|------|
| sender refund | `line 826-829`: `gasRemaining * GasPrice` → `st.msg.From` | `line 139`: `gasRemaining * effectiveGasPrice` → sender |
| coinbase tip | `lines 691-704`: `effectiveTip = GasPrice - BaseFee`; `gasUsed * effectiveTip` → Coinbase | `lines 141-144`: `effectiveTip = max(0, effectiveGasPrice - baseFee)`; `gasUsed * effectiveTip` → coinbase |
| baseFee | `lines 714-719`: `gasUsed * BaseFee` → `OptimismBaseFeeRecipient` (0x4200...19) | `line 145`: `gasUsed * baseFee` → `OP_BASE_FEE_RECIPIENT` (0x4200...19) |
| L1 cost | `lines 720-725`: `l1Cost` → `OptimismL1FeeRecipient` (0x4200...1A) | `line 146`: `m_l1CostCharged` → `OP_L1_FEE_RECIPIENT` (0x4200...1a) |
| operator cost refund | `lines 836-845`: `operatorCostGasLimit - operatorCostGasUsed` → sender | `lines 105-119`: `m_operatorCostLimit - usedCost` → sender |
| operator cost pay | `lines 731-732`: `OperatorCostFunc(gasUsed)` → `OptimismOperatorFeeRecipient` (0x4200...1B) | `lines 150-153`: `m_operatorCostFunc(gasUsed)` → `OP_OPERATOR_FEE_RECIPIENT` (0x4200...1b) |

### 核查 J：L1CostFjord / OperatorCostIsthmus / FloorDataGas / FastLZ 公式

**核查结论**：✅ **全部逐行匹配**

- **L1CostFjord**（`rollup_cost.go:608-628` vs `OpStackFee.cpp:109-129`）：`estimatedSize = max(MIN, INTERCEPT + COEF*fastLzSize)`, `l1Cost = estimatedSize * l1FeeScaled / DIVISOR` — 完全一致
- **OperatorCostIsthmus**（`rollup_cost.go:254-269` vs `OpStackFee.cpp:131-141`）：`fee = gas * scalar / 1e6 + constant` — 完全一致
- **FloorDataGas**（`state_transition.go:121-133` vs `OpStackFloorGas.cpp:32-42`）：`tokens = nz*4 + z`, `floor = 21000 + tokens*10` — 完全一致
- **FastLZ**（`rollup_cost.go:669-743` vs `RollupCost.cpp:12-105`）：`FlzCompressLen` vs `flzCompressLenImpl` — C++ 逐行端口，逻辑完全一致
- **Fee scalar 解析**（`rollup_cost.go:649-659` vs `OpStackFee.cpp:154-166`）：l1BaseFeeScalar 从 bytes[16:20) 读取，l1BlobBaseFeeScalar 从 bytes[20:24)，operatorFeeScalar 从 bytes[20:24)，operatorFeeConstant 从 bytes[24:32) — 字节偏移完全一致

---

### 核查汇总

| 差异编号 | 原文结论 | 核查结果 | 状态变化 |
|---------|---------|---------|---------|
| 差异 1 (Deposit nonce) | 🔴 op-geth 可能不递增 | 🟢 两者均递增，行为等价 | **降级** |
| 差异 2 (Legacy tx) | 🔴 effectiveGasPrice=0 | 🟡 生产路径安全（fillGasCaps 正常化），直接调用有风险 | **降级** |
| 差异 3 (System deposit) | 🔴 pre-Regolith 分歧 | 🔴 证实，但 pre-Regolith 已无关紧要 | 维持 |
| 差异 4 (Floor gas 时序) | 🟡 时序差异 | 🟢 无时序差异，均在 intrinsic 扣减前检查 | **消除** |
| 差异 5 (7702 nonce) | 🟡 回滚语义 | 🟢 结构差异，行为等价 | **降级** |
| 差异 6 (NoBaseFee) | 🟡 缺少 skip | 🟡 证实（eth_call 场景） | 维持 |
| 差异 7 (CREATE_BASE_GAS) | 🔴→✅ 已对齐 | ✅ 确认对齐（21000+32000=53000） | 维持 |
| 差异 9 (Jovian operator fee) | 🟢 待实现 | ✅ `operatorCostJovian` + GPO + L1 setter 已实现 | **消除** |
| 全部常量 (11 项) | ✅ | ✅ 逐项核对一致 | 维持 |
| Gas settlement | ✅ | ✅ refund cap / floor uplift 完全匹配 | 维持 |
| Fee routing | ✅ | ✅ sender/coinbase/baseFee/L1/operator 全部匹配 | 维持 |
| L1 cost / operator cost | ✅ | ✅ 公式 + FastLZ + scalar 解析 完全匹配 | 维持 |

**最终剩余高优先级差异**：无。差异 3（system deposit）因 pre-Regolith 不相关而降级。差异 2 的生产路径安全。所有核心公式和常量已从两个代码库逐行验证一致。
