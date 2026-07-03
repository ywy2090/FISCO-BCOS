# OpStack Jovian DA Footprint — 设计规格（Receipt 阶段）

**Status:** 已实现（Phase 1）  
**Date:** 2026-07-01  
**Branch:** feat/adr-030-geth-naming  
**Parent:** [2026-06-26-opstack-jovian-design.md](2026-06-26-opstack-jovian-design.md)（Scope B，已实现）  
**Audit:** N2 / D10b — [2026-07-01-opstack-vs-op-geth-parity-round2-reverify.md](../../bcos-evm/docs/audits/2026-07-01-opstack-vs-op-geth-parity-round2-reverify.md)  
**Reference:** op-geth `core/types/rollup_cost.go`（`EstimatedDASize`, `CalcDAFootprint`, `ExtractDAFootprintGasScalar`）、`core/types/receipt_opstack.go`（`deriveOPStackFields`）

---

## 0. 工作范围总览（必读）

Jovian DA footprint 在 op-geth 中横跨 **单笔 receipt**、**块级累计/验证**、**出块限流** 三层。本 spec **仅覆盖第一层**；块与出块相关内容 **明确延后**，在 §8 列出完整 backlog。

### 0.1 当前阶段（Phase 1 — 本 spec 实现范围）

| ID | 能力 | 层级 | 本阶段 |
|----|------|------|--------|
| P1-1 | `estimatedDASize(RollupCostData)` | `bcos-evm/opstack/fee` | ✅ 做 |
| P1-2 | 单笔 `txDAFootprint = estimatedDASize × daFootprintGasScalar` | 执行 / TE | ✅ 做 |
| P1-3 | `OpStackReceiptMeta` 字段 | opstack types | ✅ 做 |
| P1-4 | `TransactionReceipt` sidecar + TE `makeReceipt` | protocol / TE | ✅ 做 |
| P1-5 | Jovian 门控；deposit 跳过 | `ApplyOpStackMessage` | ✅ 做 |
| P1-6 | 单元 + opstack + TE fixture 测试 | test | ✅ 做 |

**Phase 1 成功标准：** Jovian 激活块上，非 deposit 普通 tx 的 receipt 含 `daFootprintGasScalar` 与 per-tx `blobGasUsed`（DA footprint 语义），数值与 op-geth `deriveOPStackFields` 单笔公式一致；pre-Jovian / deposit **零行为变化**。

实现提交见 plan [2026-07-02-opstack-jovian-da-footprint-receipt.md](../plans/2026-07-02-opstack-jovian-da-footprint-receipt.md)。

**Phase 1 对审计的闭合程度：** N2 / D10b **部分闭合**（receipt / 执行时 meta）；块级与出块仍 🟡。

### 0.2 后续阶段（Phase 2+ — 本 spec 不实现，仅记录）

| ID | 能力 | op-geth 参照 | 层级 | 本阶段 |
|----|------|--------------|------|--------|
| P2-1 | 块级 `calcDAFootprint(txs)` | `rollup_cost.go:CalcDAFootprint` | types / finalize | 📋 [Phase 2 spec](2026-07-02-opstack-jovian-da-footprint-block-design.md) |
| P2-2 | Header `BlobGasUsed` = 块 DA footprint 累计 | Jovian 复用该字段 | 共识 / engine | 📋 Phase 2（Engine `ExecutionPayload`；FISCO BlockHeader 不扩展） |
| P2-3 | `ExecutionPayload.blobGasUsed`（块级） | Engine API | engine / RPC | 📋 Phase 2 |
| P2-4 | 块内 DA 预算池（类比 `BlockGasPool`） | miner `daFootprintLeft` | TE `beginBlock` | 📋 Phase 2 |
| P2-5 | Entry 拒绝：超 DA 预算 tx | `miner/worker.go` | TE / txpool | 📋 Phase 2 |
| P2-6 | Sealer / PBFT 打包按 DA 筛 tx | miner tx selection | 共识 / scheduler | 📋 Phase 2 |
| P2-7 | RPC `eth_getTransactionReceipt` 暴露字段 | `internal/ethapi/api.go` | bcos-rpc | ❌ 不做（可另开 integration PR） |
| P2-8 | Minimum base fee（header extraData 17B） | Jovian 共识 | 出块 | ❌ 不做 |

> **备注：** Phase 2 完成后，N2 / D10b 方可标为 **全量闭合**。Scope C 独立 spec/plan：[2026-07-02-opstack-jovian-da-footprint-block-design.md](2026-07-02-opstack-jovian-da-footprint-block-design.md) / [plan](../plans/2026-07-02-opstack-jovian-da-footprint-block.md)。

---

## 1. 背景

### 1.1 什么是 DA footprint

Rollup 将 L2 交易数据发布到 L1。Jovian 起，除 execution gas 与 L1 fee 外，还需计量每笔交易占用的 **数据可用性（DA）字节预算**：

```text
estimatedDASize(tx) = Fjord 线性回归（intercept + fastLzCoef × fastLzSize，下限 minTxSize）
txDAFootprint       = estimatedDASize × daFootprintGasScalar
```

- `daFootprintGasScalar`：由块首 **Jovian L1 attributes deposit**（178B，`0x3db6be2b`）写入 L1Block slot 8 bytes `[18:20)`（**Scope B 已实现**）。
- op-geth 在块 finalize 时 `deriveOPStackFields` 为每笔非 deposit receipt 写入 `daFootprintGasScalar` 与 `blobGasUsed`（此处 **不是** EIP-4844 blob gas）。

### 1.2 bcos 现状

| 已有 | 缺失（Phase 1 补） | 缺失（Phase 2+ 补） |
|------|-------------------|---------------------|
| `RollupCostData` + signed RLP `buildRollupCostData` | `estimatedDASize()` 独立导出 | `calcDAFootprint` |
| Fjord 公式内含 estimated size 片段 | receipt meta + sidecar | header `BlobGasUsed` |
| L1Block 持久化 `daFootprintGasScalar` | TE `makeReceipt` wire | DA 预算池 / 出块限流 |

---

## 2. 设计决策

| # | 主题 | 决策 |
|---|------|------|
| D1 | 与 geth derive 时机 | bcos **执行时逐笔**写入 meta（同 `l1Fee` / `operatorFeeScalar`），不实现块级 batch derive |
| D2 | 标量来源 | **扩展 `OpStackFeeParams` 加 `daFootprintGasScalar` 字段**，在 `loadOpStackFeeParams`（`OpStackFeeParams.cpp:155-163`）补一行 `unpackDaFootprintGasScalar(operatorFeeParams)`（helper 已存在 `L1BlockStorage.cpp:188-191`）。**不**新增独立 `loadDAFootprintGasScalar`。 |
| D2' | state 读 vs geth calldata 读 | op-geth 从**首笔 L1 attributes deposit calldata** 取 scalar（`ExtractDAFootprintGasScalar`）；bcos 从 **state slot 8 `[18:20)`** 读。二者**等价**：Scope B setter 已把 calldata 值写入 slot，且 deposit 先于 user tx 执行（op-geth 注释标 consensus-critical 的顺序），故 user tx 执行时 slot 必已就绪。 |
| D3 | `blobGasUsed` 命名 | sidecar 沿用 op-geth / JSON-RPC 字段名；注释标明 Jovian 下为 DA footprint。**互斥约束：** op-geth 该字段本用于 4844 type-3（`receipt.go:431`），Jovian 下被 `receipt_opstack.go:50` 覆盖为 DA footprint；bcos 当前**不支持 L2 type-3 tx**故无冲突；未来若支持 4844，须保证同一 receipt 上「4844 blob gas」与「Jovian DA footprint」二选一写入。 |
| D4 | 门控 | 仅 `isOpStackJovian(schedule, blockTime)`；Isthmus+ 默认 schedule 无回归 |
| D5 | Deposit | 不设置 footprint 字段（对齐 geth skip deposit） |
| D6 | 块级 | **本 spec 零改动** `beginBlock` / `endBlock` / header / sealer |

---

## 3. 架构（Phase 1）

```text
OpStackTransactionExecutorImpl::executeTransaction
  buildRollupCostData(tx)                    [已有]
  applyOpStackMessage
    loadOpStackFeeParams / loadDAFootprintGasScalar(state)
    if Jovian && !deposit && rollupCostData:
      meta.daFootprintGasScalar = scalar
      meta.txDaFootprint = estimatedDASize(rcd) × scalar
  makeReceipt
    setDaFootprintGasScalar("0x...")
    setBlobGasUsed("0x...")   // = txDaFootprint
```

### 3.1 模块边界

| 模块 | 文件（拟） | 职责 |
|------|-----------|------|
| 公式 | `opstack/fee/RollupCost.h/cpp` | `estimatedDASize()`；`l1CostFjord` 复用 |
| 辅助 | `opstack/fee/DaFootprint.h`（可选内联于 RollupCost） | `calcTxDAFootprint(data, scalar)` |
| 标量 | `opstack/fee/OpStackFeeParams.*` | `loadDAFootprintGasScalar(state)` 或 `OpStackFeeParams::daFootprintGasScalar` |
| Meta | `opstack/types/OpStackReceiptMeta.h` | 两字段 optional |
| 执行 | `opstack/apply/ApplyOpStackMessage.cpp` | Jovian 非 deposit 赋值 |
| 协议 | `TransactionReceipt.h` / `TransactionReceiptImpl.h/.cpp` | sidecar getter/setter + 成员 + static_assert |
| 协议拷贝 | `TransactionReceiptFactoryImpl.cpp` | `createReceipt(input)` 拷贝路径复制新字段（N1 同款，勿漏） |
| TE | `OpStackTransactionExecutorImpl.h` | `makeReceipt` 映射 hex |

**不修改：** `BlockGasPool`、`beginBlock`/`endBlock`、block header、PBFT sealer、bcos-rpc（Phase 2+）。

### 3.2 写入位置

写在 **`OpStackNormalTxFeeCoordinator::projectNormalReceiptMeta`**（`OpStackNormalTxFeeCoordinator.cpp`），与 `l1Fee` / `operatorFee` **同源同址**——而非裸写在 `ApplyOpStackMessage`。原因：

- `projectNormalReceiptMeta` 只在**非拒绝路径**被调用；`completeAfterPipeline` 在 entry-reject 时提前 `co_return`（`OpStackNormalTxFeeCoordinator.cpp:70-73`），不设 meta。**对齐 op-geth「被拒 tx 不进块、无 receipt derive」**。
- 若改在 `ApplyOpStackMessage.cpp:146-163` 之间裸写，会在 reject 路径（148-153 行）也写入 → 与 geth 不一致（真实缺陷）。
- 该函数已可拿到 `feeParams`、`view`（`view.rollupCostData()` 见 `OpStackSettlementProjection.cpp:79-82`、`view.blockInfo().timestamp`），无需额外传参。

门控：`isOpStackJovian(input.forkSchedule, view.blockInfo().timestamp)`。deposit 分支（`ApplyOpStackMessage.cpp:86-122`）本就不经此函数，天然不写——对齐 op-geth `receipt_opstack.go:34-36` skip deposit。

> **与 N1 相反：** `depositReceiptVersion` 只在 deposit 分支写；DA footprint 只在普通 tx 结算路径写。

---

## 4. 数据模型

### 4.1 `OpStackReceiptMeta` 扩展

```cpp
std::optional<uint64_t> daFootprintGasScalar;
std::optional<uint64_t> txDaFootprint;  // 内部名；对外 receipt = blobGasUsed
```

### 4.2 `TransactionReceipt` sidecar

```cpp
virtual std::optional<std::string> daFootprintGasScalar() const = 0;
virtual void setDaFootprintGasScalar(std::string) = 0;
virtual std::optional<std::string> blobGasUsed() const = 0;  // Jovian: per-tx DA footprint
virtual void setBlobGasUsed(std::string) = 0;
```

**冲突核查（已确认）：** `bcos-framework/protocol/TransactionReceipt.h` 当前 **无** `blobGasUsed` 字段；`blobGasUsed` 仅存在于块级 `ExecutionPayload`（`bcos-framework/engine/Types.h`）与 `bcos-rpc` 的 `BlockResponse`，二者均为 Phase 2 块级范畴，与本 receipt sidecar **不冲突**。故 sidecar 直接使用 `blobGasUsed` 名称即可，无需 alias。

### 4.3 `AnyTransactionReceipt` 缓冲

当前常量 **272**（N1 加 1 个 `optional<string>` 从 240 → 272，步长 +32）。本 spec 再加 **2 个** `optional<string>` sidecar，按同步长预计 **272 → ~336**（若该平台 `optional<string>=40B` 则至 ~352）。需**同步更新两处**：

- `bcos-framework/protocol/TransactionReceipt.h:103-105` `AnyHolder<TransactionReceipt, N>`
- `bcos-tars-protocol/.../TransactionReceiptImpl.h:114-116` `static_assert(sizeof(...) <= N)`

实现时以编译期 `static_assert` 实测值为准，向上取整。

### 4.4 序列化边界（重要）

sidecar 为**执行期内存字段**，**不进 tars 序列化**：`encode`/`decode` 只处理 tars inner struct（`TransactionReceiptImpl.cpp:40-42`），与既有 `l1Fee` / `depositReceiptVersion` 行为一致。即 receipt 一旦 encode/decode，footprint 字段丢失。Phase 1 可接受（RPC 暴露 = P2-7）；跨序列化持久化留待 Phase 2。

---

## 5. 公式（与 op-geth 对齐）

```cpp
uint64_t estimatedDASize(RollupCostData const& data) noexcept
{
    // 中间量必须用有符号 s256（L1_COST_INTERCEPT = -42'585'600 为负，
    // fastLzSize 很小时 intercept + coef*size 会为负，再由 MIN 兜底）。
    // 复用 OpStackFeeParams.cpp:106-107 的既有 s256 模式：
    //   s256 scaled = s256(L1_COST_INTERCEPT) + s256(L1_COST_FASTLZ_COEF) * s256(fastLzSize);
    //   if (scaled < s256(MIN_TX_SIZE_SCALED)) scaled = s256(MIN_TX_SIZE_SCALED);
    //   return static_cast<uint64_t>(scaled / 1'000'000);  // max 后 >=100，恒正
}
```

> ⚠️ **绝不可用 `uint64_t` 直接算中间量**：`int64_t(-42585600)` 隐式转 `uint64_t` 会得 `2^64-42585600` 巨值，`max` 后返回天文数字 → 真实 bug。返回类型 `uint64_t` 本身安全（`max(MIN, ...)/1e6 >= 100`）。

**复用而非重写：** 将该逻辑抽为独立 `estimatedDASize()`，并让现有 `l1CostFjord`（`OpStackFeeParams.cpp:106-113`）改为调用它，消除公式双份、防漂移。

**验收向量：** op-geth `receipt_opstack_test.go` / bcos `empty_tx.bin` + Jovian fixture `jovian_l1_attributes.bin`（scalar=400）。

---

## 6. 错误处理与边界

| 场景 | Phase 1 行为 |
|------|----------------|
| pre-Jovian | 不写 meta / sidecar |
| deposit tx | 不写（对齐 geth `deriveOPStackFields` skip deposit） |
| Jovian + 非 deposit（默认） | **恒写** `daFootprintGasScalar` 与 `blobGasUsed`，与 op-geth `deriveOPStackFields` 一致（isJovian 分支无条件赋值） |
| Jovian 但 scalar=0 | 仍写：`daFootprintGasScalar=0`、`blobGasUsed=0`（geth 乘 0 得 0 且照常赋值；不 omit，保证 RPC 字段稳定） |
| 无 `rollupCostData`（不应发生在 TE 普通 tx） | 不写 footprint（防御性）|
| 块超 DA 上限 | **不拒绝**（Phase 2）；块仍可执行 |

---

## 7. 测试计划（Phase 1）

| # | 测试 | 断言 |
|---|------|------|
| T1 | `EstimatedDASizeTest` | 与 geth 已知 `RollupCostData` 一致；含 **fastLzSize 极小 → 命中 MIN 兜底**（验证有符号中间量，防 underflow） |
| T2 | `DaFootprintReceiptTest` | Jovian 非 deposit user tx → meta 字段 = `scalar × estimatedDASize` |
| T3 | `L1AttributesDepositTest` 扩展 | post-Jovian calldata 后 user tx footprint 非零 |
| T4 | TE fixture | `receipt->daFootprintGasScalar()`、`receipt->blobGasUsed()` 有值且正确 |
| T5 | **scalar=0 分支** | Jovian + scalar=0：字段**存在**且为 `0x0`（不 omit，对齐 geth 恒写） |
| T6 | **deposit 不写** | deposit receipt：`!daFootprintGasScalar().has_value()` 且 `!blobGasUsed().has_value()` |
| T7 | **pre-Jovian 不写** | Isthmus schedule 下 user tx：两字段均 `!has_value()` |
| T8 | 回归 | 默认 Isthmus schedule 全量 opstack ctest 无回归 |

---

## 8. 后续完整实现 backlog（Phase 2 — 已独开 spec）

实现 **op-geth Jovian DA footprint 全量 parity** 的块级/出块部分，见 **[2026-07-02-opstack-jovian-da-footprint-block-design.md](2026-07-02-opstack-jovian-da-footprint-block-design.md)**（plan: [2026-07-02-opstack-jovian-da-footprint-block.md](../plans/2026-07-02-opstack-jovian-da-footprint-block.md)）。摘要：

1. **`calcDAFootprint`** — 跳过 deposit，累加非 deposit tx footprint；Jovian 激活块特例（176B 首 deposit、无 user tx → 0）。
2. **Header / payload** — 块 `BlobGasUsed` 存累计 DA footprint（非 Cancun blob gas 语义）；Engine `executionPayload.blobGasUsed` 同步。
3. **`BlockDAFootprintPool`** — TE `beginBlock` 初始化（上限来源：链 config / op-geth params，实现时锁定常量）。
4. **Entry 拒绝** — `tryConsume(txFootprint)` 失败则与普通 tx gas pool 拒绝同路径；失败 revert 归还 footprint。
5. **Sealer 集成** — PBFT/txpool 打包时预留 DA 空间（对标 `miner/worker.go`）；依赖共识层接口。
6. **RPC** — `eth_getTransactionReceipt` / `eth_getBlockBy*` 暴露 `daFootprintGasScalar`、块级 `blobGasUsed`。
7. **Min base fee** — header extraData 17B（若产品需要完整 Jovian 经济模型）。

**依赖关系建议：** P2-1/P2-2 → P2-4 → P2-5 → P2-6；P2-7 可与 P2-1 并行。

---

## 9. 参考

- OP Stack [Jovian L1 attributes](https://specs.optimism.io/protocol/jovian/l1-attributes.html)
- op-geth `core/types/receipt_opstack.go:48-51`
- op-geth `core/types/rollup_cost.go:559-590`, `642-647`
- op-geth `miner/worker.go`（DA 限流，Phase 2+）
- bcos `docs/superpowers/specs/2026-06-26-opstack-jovian-design.md` §8 non-goals
- Audit N2/D10b：`bcos-evm/docs/audits/2026-07-01-opstack-vs-op-geth-parity-round2-reverify.md`

---

## 10. 评审检查清单

- [x] §0 当前范围与 §8 后续范围边界清晰
- [x] Phase 1 不触碰 block / sealer / RPC
- [x] `blobGasUsed` receipt 层无冲突（§4.2 已核实）；D3 注释策略明确
- [x] 审计 N2 部分闭合 vs 全量闭合表述准确
- [x] Jovian + 非 deposit 恒写字段，与 geth `deriveOPStackFields` 对齐（§6）
- [x] meta 写入位置明确：`projectNormalReceiptMeta`（继承非拒绝门控，§3.2）
- [x] §5 中间量用 s256 有符号，防 uint64 underflow（独立审查 🟡-1）
- [x] 缓冲区 272→~336 + `TransactionReceiptFactoryImpl.cpp` 拷贝路径（🟡-3）
- [x] §7 覆盖 scalar=0 / deposit / pre-Jovian 全分支（🟡-4）
- [x] D2 钉死扩展 `OpStackFeeParams`；state-vs-calldata 等价性说明（🔵-1）
- [x] sidecar 不进 tars 序列化，已注明（§4.4，🔵-3）
