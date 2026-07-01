# OpStack Jovian Design (Scope B)

**Status:** Implemented  
**Date:** 2026-06-26  
**Scope:** `bcos-evm/opstack` execution layer + L1 attributes (Jovian setter)  
**Reference:** op-geth `core/types/rollup_cost.go`, `core/types/receipt_opstack_test.go`; [OP Spec — Jovian L1 attributes](https://specs.optimism.io/protocol/jovian/l1-attributes.html)  
**Prerequisite:** Isthmus implementation complete (`OpStackForkSchedule`, `operatorCostIsthmus`, `applySetterIsthmus`, GPO predeploy)  
**Parent context:** `docs/superpowers/plans/2026-06-25-opstack-op-geth-diff-report.md` (差异 9)

---

## 1. Goal

补齐 OP Stack **Jovian** 硬分叉在 `bcos-evm/opstack` 执行层的核心语义，使单 tx 执行与 L1 attributes deposit 在 Jovian 激活后与 op-geth 对齐。

### 1.1 In scope (Scope B)

| 能力 | 说明 |
|------|------|
| Fork 门控 | `OpStackForkSchedule` 增加 `jovianTime`；运行时按 `blockInfo.timestamp` 分支 |
| Operator fee fix | Jovian 公式 `gas × scalar × 100 + constant` |
| GPO 预部署 | `isJovian()` / `getOperatorFee()` 按 fork 动态返回 |
| Jovian L1 attributes | selector `0x3db6be2b`，178B calldata，写入 `daFootprintGasScalar` |

### 1.2 Out of scope

| 能力 | 原因 |
|------|------|
| Minimum base fee (header extraData 17B) | 共识/出块层 |
| DA footprint 区块限流 (miner tx selection) | TE/出块层 |
| Receipt `daFootprintGasScalar` / RPC 字段 | Integration 层 |
| Jovian 预编译输入上限 (BLS/BN256) | eth 层独立变更 |
| TE 默认 schedule 切换 | 保持 `makeIsthmusPlusForkSchedule()` 默认 |
| Fusaka readiness | 节点/L1 适配，非执行语义 |

### 1.3 Success criteria

1. `operatorCostJovian` 与 op-geth `newOperatorCostFuncOperatorFeeFix` 数值一致（canonical fixture）。
2. `makeCachedOperatorCostFunc` 在 `isOpStackJovian(schedule, blockTime)` 为 true 时使用 Jovian 公式；false 时行为与当前 Isthmus 完全一致。
3. Jovian L1 attributes deposit（178B，`0x3db6be2b`）正确写入 L1Block slot 1/3/7/8；`daFootprintGasScalar` 持久化于 slot 8 bytes[18:19]，`kDaFootprintGasScalar` getter 可读。
4. Isthmus setter（176B，`0x098999be`）保持可用（激活块兼容）。
5. GPO：`kIsJovian` 在 post-Jovian 返回 1，pre-Jovian 返回 0；`kGetOperatorFee` 使用对应公式。
6. 默认 `makeIsthmusPlusForkSchedule()` 下全部现有 opstack 测试通过（零回归）。
7. 新增 Jovian 测试矩阵 T1–T9 全部通过。

---

## 2. Design decisions (brainstorming)

| # | Topic | Decision |
|---|-------|----------|
| D1 | 范围 | **B** — 执行层 + L1 attributes；不含出块/共识 |
| D2 | Fork 注入 | 新增 `makeJovianPlusForkSchedule()`（`fjord=0, isthmus=0, jovian=0` = 全激活，供测试） |
| D3 | 默认策略 | **C** — 运行时按 `forkSchedule` + `blockInfo.timestamp` 动态分支；TE 默认仍为 Isthmus+ |
| D4 | 实现路径 | **方案 2** — 集中 fork helper（`isOpStackJovian`），不引入 ForkContext 对象 |
| D5 | L1 setter 路由 | 双 setter 按 **selector** 分发，不按 fork 拒绝 Isthmus setter |
| D6 | GPO fork 感知 | 扩展 `GasPriceOraclePredeploy::dispatch` 与 `OpStack chain call-target adapter` 传 `forkSchedule` + `blockTime` |

---

## 3. Architecture

### 3.1 Fork schedule

```text
isOpStackJovian(schedule, blockTime)
  := schedule.jovianTime.has_value() && *schedule.jovianTime <= blockTime

TE 默认:  makeIsthmusPlusForkSchedule()  → jovianTime = nullopt → pre-Jovian 行为
测试:     makeJovianPlusForkSchedule()  → jovianTime = 0           → Jovian 全激活
未来:     jovianTime = 1764691201       → 按 Superchain 主网时间戳切换
```

`OpStackForkSchedule` 扩展：

```cpp
struct OpStackForkSchedule
{
    std::optional<uint64_t> fjordTime;
    std::optional<uint64_t> isthmusTime;
    std::optional<uint64_t> jovianTime;   // 新增
};

inline bool isOpStackJovian(OpStackForkSchedule const& schedule, uint64_t blockTime);
inline OpStackForkSchedule makeJovianPlusForkSchedule();
```

`OpStackMessageRequest::forkSchedule` 默认值不变（`makeIsthmusPlusForkSchedule()`）。

### 3.2 Operator fee

| Fork | 公式 | op-geth |
|------|------|---------|
| pre-Jovian | `gas × scalar / 1'000'000 + constant` | `newOperatorCostFuncIsthmus` |
| post-Jovian | `gas × scalar × 100 + constant` | `newOperatorCostFuncOperatorFeeFix` |

分支点：`makeCachedOperatorCostFunc`（`OpStackFee.cpp`）在 cache refresh 时根据 `isOpStackJovian` 选择函数。

消费路径不变：`buyGas` → `refundIsthmusOperatorCost` → `refundGas` → `projectNormalReceiptMeta`。

### 3.3 L1 attributes — Jovian setter

Calldata 布局（[Jovian L1 attributes spec](https://specs.optimism.io/protocol/jovian/l1-attributes.html)）：

| Offset | 字段 | 说明 |
|--------|------|------|
| 0–3 | `0x3db6be2b` | `keccak256("setL1BlockValuesJovian()")[:4]` |
| 4–175 | 同 Isthmus | baseFeeScalar … operatorFeeConstant |
| 176–177 | `daFootprintGasScalar` | uint16 big-endian |

Storage 写入与 Isthmus 相同（slot 0/1/2/3/4/7/8）。Slot 8 布局：

```text
OPERATOR_FEE_PARAMS_SLOT:
  bytes[18:19] = daFootprintGasScalar   (Jovian 新增)
  bytes[20:23] = operatorFeeScalar
  bytes[24:31] = operatorFeeConstant
```

`unpackDaFootprintGasScalar` 与 `kDaFootprintGasScalar` getter **已存在**；本次补齐 setter 侧 `packOperatorFeeParams` 与 `applySetterJovian`。

**激活块边界**（spec）：Jovian 激活块仍可使用 Isthmus setter（176B）。实现上双 setter 并存，按 selector 路由，不按 fork 互斥。

### 3.4 GPO predeploy

`GasPriceOraclePredeploy::dispatch` 签名扩展：

```cpp
static std::optional<evmc_result> dispatch(
    state::State& state,
    evmc_message const& msg,
    bcos::u256 l2BaseFee,
    OpStackForkSchedule const& forkSchedule,
    uint64_t blockTime);
```

| Selector | pre-Jovian | post-Jovian |
|----------|-----------|-------------|
| `kIsJovian` (`0x105d0b81`) | 0 | 1 |
| `kGetOperatorFee` (`0x275aedd2`) | `operatorCostIsthmus` | `operatorCostJovian` |

`OpStack chain call-target adapter` 构造时接收 `forkSchedule` + `blockTimestamp`，由 `OpStackTxLifecycle` 从 `input` 传入。

### 3.5 Data flow

```text
L1 attributes deposit (type 0x7E)
  → executeMessage → OpStack chain call-target adapter::tryChainPrecompile
    → L1BlockPredeploy::dispatch
      → [0x098999be] applySetterIsthmus  (176B, 不变)
      → [0x3db6be2b] applySetterJovian   (178B, 新增)

User tx
  → runOpStackTxLifecycle
    → wireOperatorCostFuncWithState(forkSchedule, state)
      → isOpStackJovian ? operatorCostJovian : operatorCostIsthmus
    → buyGas / refundGas / receiptMeta.operatorFee
```

---

## 4. File-level changes

| 文件 | 变更 |
|------|------|
| `OpStackForkSchedule.h` | +`jovianTime`, `isOpStackJovian()`, `makeJovianPlusForkSchedule()` |
| `OpStackConstants.h` | +`JOVIAN_L1_ATTRIBUTES_LEN = 178` |
| `OpStackFee.h` | +`operatorCostJovian()` |
| `OpStackFee.cpp` | Jovian 公式；`makeCachedOperatorCostFunc` fork 分支 |
| `L1BlockSelectors.h` | +`kSetL1BlockValuesJovian = 0x3db6be2b` |
| `L1BlockStorage.h` | +`JovianL1Attributes`；+`parseJovianL1Attributes()`；`packOperatorFeeParams` 第三参数 |
| `L1BlockStorage.cpp` | parser + pack bytes[18:19] |
| `L1BlockPredeploy.cpp` | +`applySetterJovian()`；dispatch case |
| `GasPriceOraclePredeploy.h/.cpp` | dispatch 扩展 fork 参数；`kIsJovian` / `kGetOperatorFee` 分支 |
| `OpStack chain call-target adapter.h` | 构造携带 `forkSchedule` + `blockTimestamp` |
| `OpStackTxLifecycle.cpp` | 构造 EvmHostHooks 时传入 fork 上下文 |

**不改动：**

- `transaction-executor/.../OpStackTransactionExecutorImpl.h`（默认 schedule）
- `OpStackExecutionBridge.h`（`forkSchedule` 默认值）

---

## 5. Error handling

| 场景 | 行为 |
|------|------|
| Jovian calldata < 178B | `applySetterJovian` → `EVMC_REVERT` |
| Jovian selector 不匹配 | REVERT（仅 Jovian dispatch case） |
| 非 depositor 调用 setter | REVERT + `kNotDepositor`（与 Isthmus 一致） |
| `jovianTime = nullopt` | 全局 pre-Jovian；与当前行为完全一致 |
| operator scalar & constant 均为 0 | 返回 0（两种公式） |
| `daFootprintGasScalar = 0` in calldata | 按 spec 链上默认 400 由 op-node/SystemConfig 设置；执行层忠实写入 calldata 值 |

---

## 6. Test matrix

| # | 测试 | 文件 | 验证点 |
|---|------|------|--------|
| T1 | `operatorCostJovian` 单元 | `OpStackFeeTest.cpp` | 对齐 op-geth fixture（gas=21000, scalar=1439103868, constant=1256417826609331460） |
| T2 | Isthmus 回归 | `OpStackFeeTest.cpp` | 现有 Isthmus 用例不退化 |
| T3 | fee cache fork 分支 | `OpStackFeeTest.cpp` | Isthmus schedule → Isthmus 公式；Jovian schedule → Jovian 公式 |
| T4 | `parseJovianL1Attributes` | `L1BlockPredeployTest.cpp` | 178B fixture 字段解析 |
| T5 | pack + getter | `L1BlockPredeployTest.cpp` | daFootprint 写入 slot 8；`kDaFootprintGasScalar` 可读 |
| T6 | Jovian setter E2E | `L1AttributesDepositTest.cpp` | deposit → slot 更新 + daFootprint 持久化 |
| T7 | attributes → user tx fee | `L1AttributesDepositTest.cpp` | Jovian schedule 下 user tx operator fee 用 Jovian 公式 |
| T8 | Isthmus 默认回归 | 现有 opstack 测试 | `makeIsthmusPlusForkSchedule()` 全绿 |
| T9 | GPO fork 分支 | `GasPriceOracleTest.cpp`（新建或扩展现有） | `isJovian` / `getOperatorFee` pre/post-Jovian |

**Fixture：** op-geth `receipt_opstack_test.go` Jovian payload hex（`daFootprintGasScalar=400`）→ `test/opstack/fixtures/jovian_l1_attributes.bin`。

---

## 7. Implementation order

```text
1. OpStackForkSchedule (+jovianTime, helpers)
2. operatorCostJovian + makeCachedOperatorCostFunc branch
3. L1BlockStorage parser/packer + applySetterJovian
4. GPO + OpStack chain call-target adapter parameter plumbing
5. OpStackTxLifecycle EvmHostHooks wiring
6. Tests T1–T9
7. 更新 op-geth diff 报告（差异 9 → 已对齐）
```

---

## 8. Non-goals reminder

本 spec **不**改变以下行为（留待后续 spec）：

- 区块 `blobGasUsed` 复用为 DA footprint 累计值
- `CalcDAFootprint` 区块验证
- Min base fee 在 `CalcBaseFee` 中的下限强制
- Receipt / RPC 暴露 `daFootprintGasScalar`

---

## 9. References

- OP Stack Upgrade 17 — Jovian Hardfork（2025-11 Sepolia / 2025-12 Mainnet）
- op-geth `core/types/rollup_cost.go` — `JovianL1AttributesLen`, `ExtractDAFootprintGasScalar`, `newOperatorCostFuncOperatorFeeFix`
- op-geth `core/types/receipt_opstack_test.go` — Jovian L1 attributes hex fixture
- `docs/superpowers/plans/2026-06-25-opstack-op-geth-diff-report.md` — 差异 9 原始分析
