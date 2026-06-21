# OPStack R3-ORCH-1/2：Header Fee 数据源对齐设计

**日期：** 2026-06-21  
**状态：** Approved — 方案 A（extraData OPF1）+ A1；**无 Legacy fallback**（2026-06-21 修订）  
**审计来源：** `R3-ORCH-1`、`R3-ORCH-2` @ `2026-06-21-opstack-isthmus-reaudit-wave3.md`  
**op-geth 锚点：** `core/evm.go:58-63`；`eip4844/eip4844.go:188-196`  
**FB 锚点：** `OpStackTxInputBuilder.h:115-127`

---

## 1. 问题陈述

| ID | 现状 | op-geth 期望 | 风险 |
|----|------|--------------|------|
| R3-ORCH-1 | `resolveOpStackBaseFee` 读 `ledgerConfig.gasPrice()` | `header.BaseFee` → EVM context | effectiveGasPrice / preCheck / base fee 路由偏差 |
| R3-ORCH-2 | `resolveOpStackBlobBaseFee` 读 L1Block slot 7 | `CalcBlobFee(excessBlobGas)`；OP excess=0 → **1 wei** | blob preCheck / buyGas 误用 L1 数据费参数 |

**必须解耦：** L1Block slot 1/7 仍由 `loadOpStackFeeParams()` 服务 Fjord L1 cost，**不得**注入 `blockInfo.blobBaseFee`。

---

## 2. 目标与非目标

### 目标

1. 定义 `extraData` OP fee 线格式（`OPF1` + baseFee + excessBlobGas）。
2. TE **仅**从 header OPF1 解析 L2 `baseFee` / `blobBaseFee`。
3. 移除 `resolveOpStackBlobBaseFee(stateView)` 对 L1Block slot 7 的依赖。
4. **无 OPF1 的 header 视为无效** — 不 fallback 到 `gasPrice()` 或隐式 blob fee。
5. 全部 OpStack 测试 / TE fixture / 出块路径写入 OPF1。
6. 闭合 R3-ORCH-1/2。

### 非目标

- Legacy header fallback（`ledgerConfig.gasPrice()` 作为执行层 baseFee 回退）
- R3-ORCH-3、Jovian fix、L1 slot 语义变更
- 完整 L2 EIP-1559 动态算法（Phase 2 ADR）

---

## 3. 方案

**方案 A + A1，严格模式**

- extraData **必须**含 OPF1 段（≥ 64 字节）。
- 解析失败 → `requireOpStackHeaderFees()` 抛 `std::invalid_argument`（TE 不静默降级）。
- OP Isthmus：`excessBlobGas == 0` → `blobBaseFee = 1`。

---

## 4. extraData 线格式

```
[0..19]   coinbase (20B)
[20..23]  magic = 0x4F504631 ("OPF1")
[24..55]  baseFee (32B BE u256)
[56..63]  excessBlobGas (8B BE u64; Isthmus 恒 0)
Minimum size: 64 bytes
```

### 4.1 解析（唯一路径）

| 条件 | 行为 |
|------|------|
| `size >= 64` 且 magic=`OPF1` | 读 baseFee + excessBlobGas → `calcOpStackBlobBaseFee` |
| 否则 | **`requireOpStackHeaderFees` 抛异常** — 不处理 Legacy |

### 4.2 OP CalcBlobFee 等价

```cpp
bcos::u256 calcOpStackBlobBaseFee(uint64_t excessBlobGas);
// excessBlobGas == 0 → 1
// excessBlobGas != 0 → throw std::invalid_argument (Isthmus)
```

---

## 5. 组件与 API

**`bcos-evm/opstack/OpStackBlockHeaderExtension.h`**

```cpp
struct OpStackHeaderFees {
    bcos::u256 baseFee;
    uint64_t excessBlobGas{0};
};

std::optional<OpStackHeaderFees> tryParseOpStackHeaderFees(
    bcos::protocol::BlockHeader const& header);  // 仅测试 / 内部

OpStackHeaderFees requireOpStackHeaderFees(
    bcos::protocol::BlockHeader const& header);    // TE 生产路径

bcos::u256 calcOpStackBlobBaseFee(uint64_t excessBlobGas);
bcos::bytes encodeOpStackHeaderExtra(
    evmc_address coinbase, bcos::u256 baseFee, uint64_t excessBlobGas = 0);
```

**`OpStackTxInputBuilder.h` — 签名简化（去掉 ledgerConfig）：**

```cpp
bcos::u256 resolveOpStackBaseFee(bcos::protocol::BlockHeader const& blockHeader);
bcos::u256 resolveOpStackBlobBaseFee(bcos::protocol::BlockHeader const& blockHeader);
// 内部均调用 requireOpStackHeaderFees；删除 stateView blob 路径
```

---

## 6. 出块写路径（P0 必做）

OpStack 块 **必须**在出块时写入 OPF1 extraData：

- 测试 harness：`makeBlockHeader()` / 直连 `opStackExecuteViaHost` 用例统一调用 `encodeOpStackHeaderExtra`。
- 生产：BlockExecutive / sealer（`ExecutionPath::OpStack`）写入 baseFee + excessBlobGas=0。
- Phase 1 出块 baseFee 可来自 config；**写入 header 后执行只读 header**。

---

## 7. 错误处理

| 场景 | 行为 |
|------|------|
| extraData 无 OPF1 / 长度不足 | `requireOpStackHeaderFees` → `std::invalid_argument` |
| OPF1 + excessBlobGas ≠ 0 | `calcOpStackBlobBaseFee` → `std::invalid_argument` |
| baseFee = 0 | 合法；沿用现有 `noBaseFee` / zero fee 路径 |

**不存在的路径：** `ledgerConfig.gasPrice()` fallback、L1Block slot 7 → execution blobBaseFee。

---

## 8. 测试计划

| 范围 | 要求 |
|------|------|
| 新增 `OpStackBlockHeaderExtensionTest` | round-trip；缺失 OPF1 → tryParse nullopt；require 抛异常 |
| `OpStackTxInputBuilderTest` | resolver 读 OPF1；无 OPF1 抛异常 |
| `BlobGasBalanceTest` 等直连用例 | 通过 header helper 设 fee，或显式 `blockInfo` + 文档化仅单测 |
| **全量 OpStack CTest + TE fixture** | 全部 header 带 OPF1 |
| 解耦断言 | slot7=999 不影响 blob preCheck（blobBaseFee 来自 header/解析，=1） |

---

## 9. 批准记录

- [x] 方案 A extraData
- [x] 子方案 A1
- [x] **无 Legacy fallback**（2026-06-21 修订）
- [x] L1 slot 7 解耦
- [x] 出块写 OPF1 为 P0 必做（**TE/测试路径已写**；consensus/sealer 生产写路径 follow-up）

**下一步：** 按修订后 plan 实现
