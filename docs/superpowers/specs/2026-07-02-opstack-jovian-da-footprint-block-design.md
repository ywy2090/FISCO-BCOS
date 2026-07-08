# OpStack Jovian DA Footprint — 设计规格（Block / Scope C）

**Status:** 待评审  
**Date:** 2026-07-02  
**Branch:** feat-evm-refactor  
**Parent:** [2026-07-01-opstack-jovian-da-footprint-design.md](2026-07-01-opstack-jovian-da-footprint-design.md)（Phase 1 / Receipt，已实现）  
**Grandparent:** [2026-06-26-opstack-jovian-design.md](2026-06-26-opstack-jovian-design.md)（Scope B，已实现）  
**Audit:** N2 / D10b — [2026-07-01-opstack-vs-op-geth-parity-round2-reverify.md](../../bcos-evm/docs/audits/2026-07-01-opstack-vs-op-geth-parity-round2-reverify.md)  
**Reference:** op-geth `core/types/rollup_cost.go`（`CalcDAFootprint`）、`core/block_validator.go:119-134`、`consensus/beacon/consensus.go:429-437`、`miner/worker.go:572-734`

---

## 0. 工作范围总览（必读）

Phase 1 已补齐 **单笔 receipt** 的 `daFootprintGasScalar` 与 per-tx `blobGasUsed`（DA footprint 语义）。本 spec（**Scope C / Phase 2**）补齐 **块级累计、验块、执行期 DA 预算池、出块筛选** 四层，使 Jovian DA footprint 与 op-geth 全量 parity。

### 0.1 本 spec 实现范围（Phase 2 — Scope C）

| ID | 能力 | op-geth 参照 | 层级 | 本阶段 |
|----|------|--------------|------|--------|
| P2-1 | 块级 `calcDAFootprint(txs)` | `rollup_cost.go:559-590` | `bcos-evm/opstack/fee` | ✅ 做 |
| P2-2 | 块累计 DA footprint → **`ExecutionPayload.blobGasUsed` only**（FISCO `BlockHeader` 不持久化，见 D8） | Jovian 复用 `Header.BlobGasUsed` 语义 | engine / finalize | ✅ 做 |
| P2-3 | `ExecutionPayload.blobGasUsed` 写入与校验 | Engine API V3 | `bcos-engine` | ✅ 做 |
| P2-4 | `BlockDAFootprintPool`（类比 `BlockGasPool`） | miner `daFootprintLeft` | TE `beginBlock` | ✅ 做 |
| P2-5 | Entry 拒绝：超 DA 预算 tx | `miner/worker.go:652-658` + ST apply | TE / apply | ✅ 做 |
| P2-6 | Sealer / getPayload 按 DA 筛 tx | `miner/worker.go:589-596` | `bcos-engine` | ✅ 做 |

**Phase 2 成功标准：** Jovian 激活块上，(1) 块级 `calcDAFootprint` 与 op-geth 一致；(2) `ExecutionPayload.blobGasUsed` = 块累计 DA footprint；(3) 超 DA 预算的普通 tx 在 entry 被拒绝（与 gas pool 拒绝同语义）；(4) getPayload 打包不会纳入放不进 DA 预算的 tx；(5) newPayload 验块拒绝 `blobGasUsed` 不匹配或 `daFootprint > gasLimit` 的块；(6) N2 / D10b 标为 **全量闭合**。

### 0.2 明确不在本 spec（可另开 PR）

| ID | 能力 | 原因 |
|----|------|------|
| P2-7 | RPC `eth_getTransactionReceipt` / `eth_getBlockBy*` 暴露字段 | Integration 层；Phase 1 sidecar 已就绪，RPC wiring 独立 PR |
| P2-8 | Minimum base fee（header extraData 17B） | Jovian 共识/出块经济模型，非 DA footprint 计量 |
| — | L2 type-3 (EIP-4844) blob tx | bcos 当前不支持；与 Jovian DA footprint 互斥（见 Phase 1 D3） |
| — | `MaxDABlockSize` sequencer throttling | op-geth miner 可选配置；非 Jovian 硬分叉必需语义 |

### 0.3 与 Phase 1 的边界

| 层 | Phase 1（已实现） | Phase 2（本 spec） |
|----|------------------|-------------------|
| 公式 | `estimatedDASize` / 单笔 `size × scalar` | `calcDAFootprint` 块级 Σ |
| Receipt | `OpStackReceiptMeta` + sidecar | 不变；块 finalize 可交叉校验 Σ receipt |
| TE pool | `BlockGasPool` only | 新增 `BlockDAFootprintPool` |
| Header | 不改 | `ExecutionPayload.blobGasUsed`（Engine V3） |
| 默认 schedule | Isthmus+ 零回归 | 同上；Jovian 测试用 `makeJovianPlusForkSchedule()` |

---

## 1. 背景

### 1.1 op-geth Jovian 块级语义

Jovian 将 **块 DA footprint 上限** 存入标准 header 字段 `BlobGasUsed`（**非** Cancun EIP-4844 blob gas）：

```text
txDAFootprint     = estimatedDASize(rollupCostData) × daFootprintGasScalar
blockDAFootprint  = Σ txDAFootprint   （跳过 deposit tx）
daFootprintLeft   = blockGasLimit - header.BlobGasUsed   （出块/entry 时）
```

- **标量来源：** 块首 L1 attributes deposit calldata（178B Jovian setter）；Scope B 已写入 L1Block slot 8。
- **激活块特例：** 首笔 deposit 仍为 176B Isthmus setter、块内无 user tx → `CalcDAFootprint` 返回 0（`rollup_cost.go:568-576`）。
- **验块：** post-Jovian 块 `BlobGasUsed` 不得为 nil；须等于 `CalcDAFootprint(txs)` 且 `≤ GasLimit`（`block_validator.go:119-134`）。

### 1.2 bcos 现状（Phase 1 之后）

| 已有 | 缺失（Phase 2 补） |
|------|-------------------|
| `estimatedDASize` / receipt per-tx footprint | `calcDAFootprint(txs)` |
| `OpStackReceiptMeta.daFootprint` | 块累计 → `ExecutionPayload.blobGasUsed` |
| `BlockGasPool` + hooks | `BlockDAFootprintPool` + hooks |
| `ExecutionPayload.blobGasUsed` 字段存在（常为 0） | finalize 写入 + newPayload 校验 |
| Engine getPayload 执行全 tx 列表 | 打包前 DA 预算预筛 |

---

## 2. 设计决策

| # | 主题 | 决策 |
|---|------|------|
| D1 | 块级公式位置 | `bcos-evm/opstack/fee/RollupCost.h/cpp` 新增 `calcTxDAFootprint(data, scalar)` 与 `calcDAFootprint(txs, fork, blockTime)`；单笔逻辑复用 Phase 1 `estimatedDASize` |
| D2 | 标量解析 | 块级 `calcDAFootprint`/验块从 **首笔 deposit calldata** 解析（对齐 geth `ExtractDAFootprintGasScalar`）；176B → 激活块路径；178B → 正常 Jovian 块。块级 API 须能在 **无 state** 路径工作。执行期 entry hook 改用 state（见 D12）|
| D3 | `blobGasUsed` 语义 | Jovian 下 **仅** DA footprint；`ExecutionPayload.excessBlobGas` 保持 0（Isthmus OP-Stack 既有约定）；不与 receipt sidecar per-tx `blobGasUsed` 混淆 |
| D4 | DA 预算上限 | **`daFootprintLimit = blockGasLimit`**（对齐 op-geth `daFootprintLeft = gasLimit - *header.BlobGasUsed`） |
| D5 | Pool 生命周期 | 与 `BlockGasPool` 同生命周期：`OpStackTransactionExecutorImpl::beginBlock` 创建（仅 Jovian），`endBlock` 销毁；经 `daFootprintPoolSubHook` / `daFootprintPoolCommitHook` / `daFootprintPoolReleaseHook` 注入 `applyOpStackMessage`（归还语义见 §3.5） |
| D6 | Entry 门控 | 仅 **Jovian + 非 deposit + 有 rollupCostData** 时 `tryConsume(txFootprint)`；失败 → 与 `gasPoolSubGasHook` 失败相同 exit（buyGas 前 return，无 settle/refund） |
| D7 | 块 finalize | Engine `getPayload` 执行完成后：`calcDAFootprint(txs)` → 写入 `executionPayload.blobGasUsed`；可选交叉校验 Σ receipt `blobGasUsed()` |
| D8 | FISCO `BlockHeader` | **不**扩展 tars BlockHeader 新字段（避免共识 hash 变更）；块级 DA footprint **仅** 经 Engine `ExecutionPayload.blobGasUsed` 暴露与校验。PBFT 链若需持久化，后续 OPF2 extraData 扩展另开 spec |
| D9 | pre-Jovian | 不创建 DA pool、不跑块级校验；`ExecutionPayload.blobGasUsed` 按 **既有 V3 schema 填 0**（不写 DA 语义值） |
| D10 | Deposit 顺序 | **共识假设** 块内 deposit 先于 user tx（与 op-geth、bcos Engine deposit 注入一致）；`calcDAFootprint` **遍历全部 tx 并跳过 deposit**，不依赖 index 切片（见 §3.3） |
| **D11** | **Fork schedule 注入（前置条件）** | **当前 TE `applyOpStackMessageTx` 硬编码 `makeIsthmusPlusForkSchedule()`（`OpStackTransactionExecutorImpl.h:224`），Engine 层无任何 fork 引用 → Jovian 门控不会触发。Phase 2 必须先打通 schedule 注入：`OpStackForkSchedule` 由 `LedgerConfig`/chain config 解析（或 Engine 构造注入），经 `beginBlock` 与 `applyOpStackMessageTx` 使用**同一** schedule。测试路径须可注入 `makeJovianPlusForkSchedule()`。此为 Task 0，先于所有 Jovian 行为** |
| **D12** | **Scalar 来源双路径** | **Entry hook（执行路径，有 state）用 `loadOpStackFeeParams(state).daFootprintGasScalar`（与 Phase 1 `projectNormalReceiptMeta` 同源）；`calcDAFootprint`/newPayload 验块（无 state）用首笔 deposit calldata `ExtractDAFootprintGasScalar`。二者在 L1 attributes deposit 执行后等价（Phase 1 D2' 已证）。测试须断言同块两路径 scalar 相同** |

---

## 3. 架构

### 3.1 数据流（Jovian 块）

```text
getPayload / executeBlock
  Scheduler::beginBlock(gasLimit, forkSchedule, timestamp)   // D11: schedule 注入
    TE::beginBlock → BlockGasPool + (Jovian? BlockDAFootprintPool(limit=gasLimit))

  foreach tx (Jovian normal tx):
    buildRollupCostData(tx)
    applyOpStackMessage
      gasPoolSubGasHook(gasLimit)            // 先 gas
      daFootprintPoolSubHook(txFootprint)    // 后 DA，both before buyGas（§3.5 hook 顺序）
      buyGas → execute → settle
      终局:
        纳入块(SUCCESS/REVERT) → daFootprintPoolCommitHook(txFootprint)
        buyGas 后 abort        → daFootprintPoolReleaseHook(txFootprint)

  Engine finalize
    blockDAFootprint = calcDAFootprint(txs, schedule, timestamp)   // D12: calldata scalar
    executionPayload.blobGasUsed = blockDAFootprint

newPayload validation (Jovian)
    recomputed = calcDAFootprint(payload.transactions, ...)
    assert recomputed == payload.blobGasUsed
    assert recomputed <= payload.gasLimit
```

**Hook 顺序（entry）：** `gasPoolSubGasHook` → `daFootprintPoolSubHook` → `buyGas`（与 miner「先 gas 后 DA」一致）。任一失败即 tx 被拒、不进块，无 settle/refund，DA 侧未 consume 则无需 release。

### 3.2 模块边界

| 模块 | 文件（拟） | 职责 |
|------|-----------|------|
| 公式 | `opstack/fee/RollupCost.h/cpp` | `calcTxDAFootprint`, `calcDAFootprint`, `isOpStackDepositTx` |
| 解析 | `opstack/l1/L1BlockStorage.h`（已有） | `parseJovianL1Attributes` / 176B 长度判断 |
| Pool | **`bcos-evm/opstack/fee/BlockDAFootprintPool.h`（新建，勿放 TE header）** | `BlockDAFootprintPool`；置于 `bcos-evm` 使公式/pool 单测不依赖 `transaction-executor`，TE 仅 include |
| Hooks | `opstack/apply/ApplyOpStackMessage.h/cpp` | `daFootprintPoolSubHook` / `CommitHook` / `ReleaseHook` |
| TE | `OpStackTransactionExecutorImpl.h` | `beginBlock` 扩展、hook 注入、fork schedule（D11） |
| Scheduler | `SchedulerSerialImpl.h` / `SchedulerParallelImpl.h` | 传递 fork schedule + timestamp 到 `beginBlock`（D11） |
| Engine | `EngineServiceImpl.h/cpp` | finalize `blobGasUsed`、newPayload 校验、getPayload 预筛 |
| 测试 | `bcos-evm/test/opstack/`、`transaction-executor/tests/` | 公式、pool、TE E2E、Engine 校验 |

**不修改（Phase 2）：** receipt meta 写入路径、L1Block setter、operator fee、bcos-rpc JSON 字段（P2-7）。

### 3.3 `calcDAFootprint` 算法（对齐 op-geth）

```cpp
// 单笔；scalar 已由调用方从 L1 attrs 或 state 取得
uint64_t calcTxDAFootprint(RollupCostData const& data, uint64_t daFootprintGasScalar) noexcept;

// 块级；pre-Jovian 调用为 UB — 调用方须门控
// 返回 expected<uint64_t, CalcDAFootprintError>
expected<uint64_t, std::string> calcDAFootprint(
    span<protocol::Transaction const* const> txs,
    OpStackForkSchedule const& schedule,
    uint64_t blockTimestamp);
```

**逻辑（与 geth `rollup_cost.go:563-590` 逐行对齐）：**

1. `!isOpStackJovian(schedule, blockTimestamp)` → 调用方不应调用；若防御性调用返回 0。
2. `txs.empty()` 或 `!isOpStackDepositTx(*txs[0])` → error `"missing deposit transaction"`。
3. `data = txs[0]` 的 L1 attributes calldata（deposit `input`）：
   - `len(data) == ISTHMUS_L1_ATTRIBUTES_LEN (176)`（Jovian 激活块，首 deposit 仍是 Isthmus setter）：
     - 检查 **最后一笔** `!isOpStackDepositTx(*txs.back())`（deposit 先于 user tx，检查尾部即可判断有无 user tx）→ error `"unexpected non-deposit transactions in Jovian activation block"`；
     - 否则 return **0**。
   - 否则 `parseJovianL1Attributes(data)` → scalar（内部含长度校验，非 178B / 非 Jovian selector → error）。
4. **遍历全部 `txs`**，对每个非 deposit tx 累加：`footprint += calcTxDAFootprint(buildRollupCostData(tx), scalar)`。
   - **不** 用 `txs[1..]` 切片：与 geth `for _, tx := range txs { if IsDepositTx continue }` 一致，deposit-first 只是共识假设（D10），不作为算法前提。
   - 累加用 checked add；`uint64` 溢出（理论上不可达，footprint ≤ gasLimit）返回 error 而非回绕。
5. return footprint。

> **RollupCost 构建：** 用 `opstack_tx::buildRollupCostData(tx)`（`OpStackTxInputBuilder.h:126`），它对 Web3 signed tx 走 signed RLP、对 deposit 返回空 struct——**不要** 直接 `newRollupCostData(tx.input())`。
>
> **deposit 判定：** 新增 `bool isOpStackDepositTx(protocol::Transaction const&) noexcept`，判据 `extra[0] == bcos::executor::DEPOSIT_TX_TYPE`（与 `buildRollupCostData` / `decodeOpStackDepositTx` 一致）。`OpStackMessageRequest` 内的 `isDepositTx(input)` 是执行期结构，块级 API 吃 `protocol::Transaction`，故需此 helper。

### 3.4 `BlockDAFootprintPool`

API 借鉴 `BlockGasPool`（`OpStackTxInputBuilder.h:29-74`），但 **归还语义不同**（见 §3.5）：

```cpp
class BlockDAFootprintPool {
public:
    explicit BlockDAFootprintPool(uint64_t limit) noexcept;

    // Entry 预占：remaining 减 footprint；不足返回 false（tx 被拒，不进块）。
    bool tryConsume(uint64_t footprint) noexcept;

    // tx 纳入块（SUCCESS / REVERT）：预占转为永久占用，仅累加 cumulative，remaining 不变。
    void commit(uint64_t footprint) noexcept;

    // tx 未纳入块（buyGas 后 abort）：把已 tryConsume 的预占还回 remaining，cumulative 不变。
    void release(uint64_t footprint) noexcept;

    uint64_t remaining() const noexcept;      // = limit - Σ 已占用（预占 + 已 commit）
    uint64_t cumulativeUsed() const noexcept;  // = Σ 已 commit footprint = 块 blobGasUsed
};
```

- `tryConsume` 在 buyGas **之前**占用 `txFootprint`（= receipt 将写的值）。
- Jovian 最小 tx footprint = `estimatedDASize(empty) × scalar` = `100 × scalar`（geth `MinTransactionSize × scalar`）；pool `remaining < min` 时 sealer 停止加 tx。

### 3.5 Pool 语义 vs op-geth miner（关键差异）

**DA footprint 与 execution gas 的本质区别：gas 有 unused 部分需按 `gasUsed` 结算并归还剩余；DA footprint 一旦 tx 纳入块（无论 SUCCESS 还是 REVERT）就 100% 占用，无 unused 概念。** 因此 **不能** 照搬 gas pool 的 `returnGas(remaining, used)`。

op-geth miner 在 **commit 成功后** 执行 `*header.BlobGasUsed += txDAFootprint`（`worker.go:734`），entry 阶段不对 footprint 做 SubGas，也从不归还。

bcos Phase 2 用 tryConsume（entry 预占）+ commit/release（终局）模型，与 geth 语义等价：

| 阶段 | Gas pool | DA pool（本 spec） |
|------|----------|-------------------|
| Entry（buyGas 前） | `tryConsume(gasLimit)` | `tryConsume(txFootprint)` |
| 纳入块（SUCCESS / **REVERT**） | `returnGas(gasRemaining, gasUsed)` 归还未用 gas | `commit(txFootprint)`：**cumulative += fp，remaining 不再归还** |
| buyGas 后 abort（intrinsic/afford reject） | `returnGas(fullLimit, 0)` 全额释放 | `release(txFootprint)`：预占还回 remaining，**cumulative 不变** |
| Reject before buyGas | 未 consume | 未 consume |

**关键：REVERT 仍 `commit`**（geth `case errors.Is(err, nil)` 分支涵盖 REVERT，footprint 照常累加）；只有 **未纳入块** 的 tx（entry reject、buyGas 后 abort）才 `release`。

**验收等价性：** 块结束 `pool.cumulativeUsed()` == `calcDAFootprint(txs)` == `ExecutionPayload.blobGasUsed`。

### 3.6 Engine 集成

#### getPayload finalize（`EngineServiceImpl.h` Step 2i 之后）

```cpp
if (isOpStackJovian(forkSchedule, payloadAttributes.timestamp)) {
    auto footprint = calcDAFootprint(transactions, forkSchedule, timestamp);
    // footprint 为 expected；error → fail getPayload
    executionPayload.blobGasUsed = u256(*footprint);
} else if (version >= V3) {
    executionPayload.blobGasUsed = u256(0);  // 既有 V3 schema
}
```

#### newPayload 校验（`EngineServiceImpl.cpp` validateExecutionPayload 扩展）

post-Jovian：`calcDAFootprint` 重算并与 `executionPayload.blobGasUsed` 比较；且 `footprint <= gasLimit`。

#### getPayload tx 预筛（P2-6）

在将 tx 加入 `sealedTxs` **之前**（deposit 注入之后）：

```cpp
if (isJovian && !isOpStackDepositTx(tx)) {
    auto fp = calcTxDAFootprint(buildRollupCostData(tx), scalarFromFirstDeposit);
    if (daFootprintScheduled + fp > gasLimit) continue;  // skip tx
    daFootprintScheduled += fp;
}
```

`scalarFromFirstDeposit` 来自已注入的 L1 attributes deposit calldata（getPayload 路径 deposit 恒为块首）。

### 3.7 Fork 门控时间戳的 op-geth asymmetry

op-geth 三处门控用的时间戳不同：

| 位置 | geth |
|------|------|
| `prepareWork` 提取 scalar | `IsJovian(**parent**.Time)` |
| `commitTransactions` DA 限流 | `IsJovian(**header**.Time)` |
| `block_validator` / `finalize` | `IsJovian(**header**.Time)` |

bcos 统一用 `isOpStackJovian(schedule, blockHeader.timestamp)`（即 header time）。差异仅影响 **Jovian 激活块**（parent 未激活、header 已激活）：geth 该块因 parent 未激活而 miner 不提 scalar，但 validator 用 header time 仍跑 `CalcDAFootprint`——此时算法命中「176B 首 deposit + 无 user tx → 0」特例，两侧结果均为 0，**验收等价**。spec 明确记录此差异，避免误判为 bug。

---

## 4. 错误处理与边界

| 场景 | Phase 2 行为 |
|------|----------------|
| pre-Jovian | 无 DA pool；`blobGasUsed = 0`（V3）；不校验 |
| Jovian 激活块（176B 首 deposit，仅 deposit） | `calcDAFootprint = 0`；pool cumulative = 0 |
| Jovian 激活块含 user tx | `calcDAFootprint` error；getPayload/验块 fail |
| 普通 Jovian 块缺首 deposit | error |
| 单笔 tx footprint > blockGasLimit | entry `tryConsume` fail；sealer 跳过 |
| 块累计 footprint > gasLimit | newPayload 拒绝 |
| deposit tx | 不占 DA pool；不计入 Σ |
| scalar = 0 | footprint = 0；仍走 pool（`tryConsume(0)` 恒成功，`commit(0)` 无副作用） |
| **REVERT（纳入块）** | gas pool 按 gasUsed 结算；DA pool **`commit(txFootprint)`**（footprint 照常计入，对齐 geth `case nil` 含 REVERT） |
| **buyGas 后 abort（intrinsic/afford reject，不进块）** | gas pool 全额释放；DA pool **`release(txFootprint)`**（预占还回，cumulative 不变） |
| entry reject（buyGas 前） | 未 tryConsume，无需 release |

---

## 5. 测试计划（Phase 2）

| # | 测试 | 断言 |
|---|------|------|
| T1 | `CalcDAFootprintTest` | 对齐 geth 手工向量：激活块（176B + 仅 deposit）=0；激活块 + user tx → error；多 tx 求和；缺首 deposit → error；deposit 混在中间仍被跳过（验证全量遍历而非切片） |
| T2 | `BlockDAFootprintPoolTest` | `tryConsume`/`commit`/`release`/`cumulativeUsed`；commit 后 remaining 不归还；release 后 remaining 恢复且 cumulative 不变；耗尽后 reject |
| T3 | `DaFootprintEntryRejectTest` | Jovian 块内第二笔 tx DA 超限 → entry reject（tx 不进块、无 receipt），pool cumulative 不含该笔 |
| T4 | TE fixture | 两笔 normal tx，第二笔 footprint 超剩余 → reject；`endBlock` 前 `cumulativeUsed` == 首笔 fp |
| T5 | `EngineService` / unit | getPayload 后 `blobGasUsed == calcDAFootprint`；篡改 `blobGasUsed` 后 newPayload invalid；`footprint > gasLimit` → invalid |
| T6 | 交叉校验 | Σ receipt `blobGasUsed()`（per-tx，含 REVERT）== block `blobGasUsed` |
| T7 | **REVERT 仍计 footprint** | Jovian tx REVERT → `commit` 生效，cumulative +fp，receipt 有 `blobGasUsed` |
| T8 | **buyGas 后 abort 释放** | intrinsic/afford reject → `release`，cumulative 不变，remaining 恢复 |
| T9 | **scalar 双路径一致（D12）** | 同块 calldata scalar（`calcDAFootprint`）== state scalar（entry hook）|
| T10 | **fork 注入 E2E（D11）** | TE 经注入 `makeJovianPlusForkSchedule()` 后 DA pool 生效；默认 Isthmus 下不创建 pool |
| T11 | pre-Jovian 回归 | Isthmus schedule 全量 opstack ctest 无回归；V3 payload `blobGasUsed==0` |

**Fixture：** 复用 `jovian_l1_attributes.bin`（scalar=400，178B）、`isthmus_l1_attributes.bin`（176B，激活块）、`empty_tx.bin`、Phase 1 `DaFootprintReceiptTest` 向量。

---

## 6. 依赖与顺序

```text
Task 0: D11 fork schedule 注入（前置，阻塞所有 Jovian 行为）
  → P2-1 (calcDAFootprint + isOpStackDepositTx)
      → P2-4 (BlockDAFootprintPool)
          → P2-5 (entry sub + commit/release hooks)  ← D12 state scalar
              → P2-6 (sealer 预筛)                    ← D12 calldata scalar
      → P2-2/P2-3 (Engine finalize + validate)       // 可与 P2-4 并行
```

> **Task 0（D11）不可省略**：当前 TE 硬编码 `makeIsthmusPlusForkSchedule()`、Engine 无 fork 引用，若跳过，P2-4~P2-6 的 Jovian 门控永不触发，所有测试将退化为 no-op。

---

## 7. 参考

- Phase 1 spec：`docs/superpowers/specs/2026-07-01-opstack-jovian-da-footprint-design.md`
- op-geth `rollup_cost.go:559-590`, `block_validator.go:119-134`, `miner/worker.go:418-426,572-734`
- op-geth `miner/miner_optimism_test.go`（`TestDAFootprintMining`）
- bcos `BlockGasPool`：`transaction-executor/.../OpStackTxInputBuilder.h`
- bcos `ExecutionPayload`：`bcos-framework/engine/Types.h:96-97`
- Audit N2/D10b：`bcos-evm/docs/audits/2026-07-01-opstack-vs-op-geth-parity-round2-reverify.md`

---

## 8. 评审检查清单

- [ ] §0 Phase 2 vs P2-7/P2-8 out-of-scope 清晰
- [ ] `calcDAFootprint` 含 Jovian 激活块 176B 特例 + 全量遍历 skip deposit（非切片）
- [ ] DA pool commit/release 语义与 gas pool 差异说明充分（footprint 无 unused，REVERT 仍计）
- [ ] D11 fork schedule 注入列为 Task 0 前置条件
- [ ] D12 scalar 双路径（state / calldata）等价性 + 一致性测试
- [ ] Hook 顺序（gas → DA → buyGas）明确
- [ ] FISCO BlockHeader 不扩展（D8）与 Engine-only 暴露一致；P2-2 文案统一
- [ ] fork 门控时间戳 asymmetry（§3.7）说明
- [ ] N2/D10b 全量闭合标准可验证
- [ ] pre-Jovian 零行为变化（含 V3 `blobGasUsed==0`）
