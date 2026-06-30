# Task 5 — Prague 簇审计笔记（EIP-7623 precheck + settlement）

**日期：** 2026-06-20  
**范围：** inventory #11–12；`executeViaEth` ETH reference 路径  
**参考：** geth v1.17.3 `state_transition.go` `FloorDataGas`；Besu 26.6.0 `PragueGasCalculator`

---

## Step 1: EIP-7623 规范公式

Prague calldata floor（execution-specs / EIP-7623 §Specification）：

| 字节类型 | token 计数 | 常规 intrinsic 单价 |
|----------|------------|---------------------|
| 零字节 | 1 | 4 |
| 非零字节 | 4 | 16 |

- `tokens = zeros × 1 + nonzeros × 4`
- `floorCost = tokens × 10`
- `floorDataGas = 21000 + floorCost`（**不含** access list；Prague）
- 准入：`gasLimit ≥ max(intrinsic, floorDataGas)`（geth 先扣 intrinsic，再单独校验 `gasLimit < floorDataGas`）
- 结算：refund 后 `gasUsed = max(usedAfterRefund, floorDataGas)`

Canonical case（`fisco-evm-review/references/canonical-cases.md`）：EIP-2930 + access list + 1 字节非零 calldata → admission = receipt = **27216**（非 EIP 页面字面 27240）。

---

## Step 2: FB 实现对照

### 共享 helper — `Eip7623.h`

```cpp
constexpr int64_t TOKENS_PER_NONZERO_BYTE = 4;
constexpr int64_t TOTAL_COST_FLOOR_PER_TOKEN = 10;
// zero: normalCost += 4, tokenCount += 1
// nonzero: normalCost += 16, tokenCount += TOKENS_PER_NONZERO_BYTE
// floorCost = tokenCount * TOTAL_COST_FLOOR_PER_TOKEN
```

与 geth `params.TxTokenPerNonZeroByte=4`、`TxCostFloorPerToken=10` 及 Besu `TOTAL_COST_FLOOR_PER_TOKEN=10`、`tokensInCallData()` 一致 ✅

Profile：`EthChainPolicy.h:37-40` — PRAGUE+ 设 `eip7623=true`，`calldata_floor_per_token=10`。

### Entry precheck — `ExecuteViaEth.cpp:64-78`

| 步骤 | FB | geth `state_transition.go` |
|------|-----|---------------------------|
| 门控 | `revisionConfig.eip7623` | `rules.IsPrague` |
| 检查 | `message.gas < normalCost` → OOG | intrinsic 扣费后 `gasLimit < floorDataGas` → `ErrFloorDataGas` |
| 扣减 | `message.gas -= normalCost` | intrinsic 含 21000 + normal calldata + access + CREATE |

差异：

1. orchestration 层**未**复现 geth `ErrFloorDataGas`；floor 准入由 txpool `TxValidator::validateEip7623GasFloor` → `gasLimitMinimum()` 承担（`bcos-txpool/`，范围外）。
2. `ExecuteViaEth` **无** `web3Tx` 门控；`ExecuteViaHost` 为 `web3Tx && eip7623`（`ExecuteViaHost.cpp:228`）。TE settlement 有 `Web3Transaction` 门控（`EthTransactionExecutorImpl.h:193-195`）。
3. 7623 precheck 仅校验/扣减 calldata **normalCost**，21000 / access list / CREATE 在 settlement snapshot 的 `fixedIntrinsic` / `createTerm` 中记账。

### Settlement — `EthTxGasSettlement.h`

**Snapshot**（`ExecuteViaEth.cpp:80-90`）：`gasLimit`、`gasBeforeEvm`（post-normal-debit）、`calldata` components、`fixedIntrinsic`（21000 + access list）、`createTerm`。

**`finalizeEthereumGasUsed`**（`EthTxGasSettlement.h:110-127`）：

```cpp
gasUsedBeforeRefund = fixedIntrinsic + calldata.normalCost + executionBurn + createExtra;
gasUsedAfterRefund = gasUsedBeforeRefund - effectiveRefundEip3529(...);
floorDataGas = TX_BASE_GAS + tokenCount * calldataFloorPerToken;
return max(gasUsedAfterRefund, floorDataGas);
```

与 geth Prague 结算（`state_transition.go:648-660`：refund 后若 `used < floorDataGas` 则 top-up）同族 ✅。Access list 不计入 floor ✅。

**调用链：** `executeViaEth` 填 snapshot → `EthTransactionExecutorImpl::settleGasUsedFromEvmResult` → `finalizeEthereumGasUsed(ctx, calldata_floor_per_token)`（Web3 + eip7623 + snapshot.gasLimit>0）。

---

## Step 3: geth 对照

`FloorDataGas`（`state_transition.go:143-196`，Pre-Amsterdam/Prague 分支）：

- `tokens = nz*4 + z`
- `tokenCost = params.TxCostFloorPerToken` (=10)
- 返回 `params.TxGas + tokens*tokenCost` (= 21000 + floor)

Precheck（`:572-580`）：intrinsic 扣费后 `msg.GasLimit < floorDataGas` → reject。

Settlement（`:650-660`）：refund 后若 `gasUsed() < floorDataGas`，charge 差额。

---

## Step 4: Besu 对照

`PragueGasCalculator.java`：

- `TOTAL_COST_FLOOR_PER_TOKEN = 10L`
- `transactionFloorCost = getMinimumTransactionCost() + tokensInCallData(...) * 10`
- `tokensInCallData = zeroBytes + (payloadSize - zeroBytes) * 4`
- `calculateGasRefund`: `totalGasUsed = max(executionGasUsed, transactionFloorCost)`

与 FB helper + `finalizeEthereumGasUsed` 一致 ✅

---

## Step 5: 测试覆盖

```bash
rtk find bcos-evm/test -name '*7623*'     # 无 ETH reference 专项
rtk grep -l 7623 bcos-evm/test/eth/       # 无匹配
```

| 测试 | 路径 | 覆盖 |
|------|------|------|
| `RevisionConfigProfileTest` | `bcos-evm/test/eth/` | profile `eip7623` / `calldata_floor_per_token` ✅ |
| `Bcos7623PrecheckTest` | `bcos-evm/test/bcos/` | `executeViaHost` precheck（FISCO 路径，非 ETH reference） |
| `EthTxGasSettlementTest` | `transaction-executor/tests/` | helper 单元：formula、`gasLimitMinimum`、`finalizeEthereumGasUsed` geth 对齐 ✅ |
| `EthTxGasSettlementExecutorTest` | `transaction-executor/tests/` | TE e2e：mixed calldata floor receipt、type2 access list 27216 ✅ |
| `TransactionValidatorTest` | `bcos-txpool/test/` | txpool `gasLimitMinimum` ✅ |

**缺口：** `bcos-evm/test/eth/` **无** orchestration 专项（无 `ExecuteViaEth` precheck OOG / settlement receipt 断言）。共享 helper 由 TE 测试覆盖，但 audit 范围 `executeViaEth` 路径缺 direct fixture。

---

## Step 6: 判定汇总

| Inventory | 能力 | 状态 | 说明 |
|-----------|------|------|------|
| #11 | entry precheck | 🟡 | formula ✅；orchestration 仅 normalCost OOG；floor 准入在 txpool；无 web3Tx 门控 |
| #12 | settlement / floor gas | ✅ | `finalizeEthereumGasUsed` 与 geth/Besu Prague 一致；TE 有 e2e 测试 |

**Part 3 测试断言：** 🟡 — ETH reference 目录无 7623 专项；依赖 TE/txpool 间接覆盖。
