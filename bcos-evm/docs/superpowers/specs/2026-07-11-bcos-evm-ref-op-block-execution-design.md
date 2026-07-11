# bcos-evm-ref OP 区块执行适配设计（M-B1/B2/B3）

**日期：** 2026-07-11（rev.1 → **rev.2：对抗性审查对 op-geth v1.101702.2 全文核验——0 处错误声称；5 个"待钉"项就地钉死（EncodeIndex root 编码、withdrawalsRoot 构建侧、requestsHash 定值、DEPOSITOR 常量与校验层级、Jovian BlobGasUsed 重用）；决策点 1/2 补"自加严"标注**）
**状态：** 待用户审阅
**上游文档：** 主 spec `2026-07-08-bcos-evm-ref-evmone-reuse-design.md`（rev.8.2）§4.4/§5.2 的块级编排草图——本文是其实施级展开；D5 口径（与 `bcos-evm` 无关联）与 D4 工作流（先 plan 后动手）继续约束本文全部里程碑。
**基线：** evmone REF `3585c2cb`（system_contracts/finalize/mpt 相关 API 以此为准）；op-geth v1.101702.2（唯一 OP 正确性基准，`core/state_processor.go` / `core/types/receipt.go` / `block_validator.go`）。

---

## 1. 目标与范围

> 把 bcos-evm-ref 从「逐笔正确」升级到「逐块正确」：给出库级块执行入口 `processOpBlock`，
> 覆盖 §4.4 的完整顺序（pre-block 系统调用 → L1 attributes 首笔 → 逐笔 + 账务 → 块尾 finalize），
> 并补齐块头承诺字段（receipts-root / logsBloom / withdrawalsRoot）所需的 0x7E receipt 编码。

三个里程碑（各自按 D4 先 plan）：

| 里程碑 | 交付 | 依赖 |
|---|---|---|
| **M-B1 编排核心** | pre-block 系统调用接线 + `processOpBlock` 库化 + `finalizeOpBlock` 接线（D-10 闭环） | 无 |
| **M-B2 receipt/块头根** | 0x7E typed receipt RLP 编码器、receipts-root、块级 logsBloom、Isthmus withdrawalsRoot、requestsHash 口径 | M-B1 |
| **M-B3 块级差分 gate** | op-geth 当库生成块级黄金向量（receipts-root/块头字段）+ 回放；与 M6 tx 级 t8n gate 共用生成器基建 | M-B1/B2 |

## 2. 非目标

- derivation / engine API / fault proof / output root（主 spec §1.3 原口径不变）；
- **块头自校验**（base fee 按 Holocene EIP-1559 参数重算、gasLimit 校验等 derivation/共识层职责）：`processOpBlock` **信任调用方给定的 `BlockInfo`**，只产出执行结果与承诺字段，不裁决块头合法性——例外见 §4.1 的两类块级错误；
- 真实账本写回：`processOpBlock` 面向 `StateView` + `applyStateDiff` 缝工作，生产账本桥接是 **M3.5 Phase 2** 的闸门（其 plan 已就绪、待阈值裁定），两线平行互不阻塞；
- FISCO 路径（用户持久裁定）。

## 3. 现状地基与缺口

| §4.4 步骤 | 现状 | 缺口 |
|---|---|---|
| 1. pre-block 系统调用（4788/2935） | **零**。`OpBlockHarnessTest.cpp:119` 注释称"evmone 未导出"——**该注释过时**：REF `test/state/system_contracts.hpp` 导出 `system_call_block_start`，eth 侧 `EestBlockchainTest.cpp:93` 已在用 | M-B1 接线（注释一并勘正） |
| 2. L1 attributes 首笔 + fee 解包 | 测试代码演示过（OpBlockHarnessTest:122-140） | M-B1 库化 + 时序固化 |
| 3. 逐笔循环 + gas pool + cumulative | tx 级函数全备（`blockGasLeft` 形参、块级错误抛出均已在 rev.2 落地）；**无循环本体**，`cumulative_gas_used` 恒 0 | M-B1 |
| 4. 块尾 finalize | `finalizeOpBlock` 已存在、无调用方（台账 D-10 🔶） | M-B1 接线即闭环 |
| 块头承诺字段 | 全缺（0x7E 编码器不存在；D-06 只修了 type 字段） | M-B2 |
| 块级验证 | EEST 无 0x7E；无块级差分 | M-B3 |

## 4. 设计

### 4.1 `processOpBlock`（M-B1，新文件 `bcos-evm-ref/opstack/OpBlockExecute.{h,cpp}`）

```cpp
/// 单块输入：块头信息 + 有序交易列表（deposit 必须整体位于非 deposit 之前——
/// 结构违规是块级错误，见下）。
struct OpBlockTx
{
    std::variant<DepositTx, evmone::state::Transaction> tx;
    evmc::bytes signedEnvelope;   // 普通 tx 的签名 envelope（L1 fee 输入）；deposit 传空
};

struct OpBlockResult
{
    std::vector<OpTxReceipt> txReceipts;          // 普通 tx（含 meta）
    std::vector<OpDepositReceipt> depositReceipts;
    // M-B1 阶段 receipt 内 cumulative_gas_used 已按混排顺序填好；
    // 块级承诺字段（receiptsRoot/logsBloom/withdrawalsRoot）由 M-B2 的 seal 函数从本结构计算。
    int64_t gasUsed = 0;                          // = 末笔 cumulative
    evmone::state::StateDiff finalizeDiff;        // 块尾 finalize 产出的 diff
};

/// 执行整块。写回经由调用方提供的回调（缝与 StateDiffWriteback 同构）：
/// 每笔 tx 产 diff 后立即回调，下一笔的 view 必须已可见前笔效果。
/// 抛 std::runtime_error 的情形（块级错误，与 tx 级既有约定一致）：
///   - is_system_tx deposit（既有）
///   - 任一 tx gasLimit > 剩余块 gas pool（既有 GAS_LIMIT_REACHED 语义，扩展到普通 tx 路径）
///   - deposit 出现在非 deposit 之后（块结构违规；见 §6 决策点 1 的裁定结果）
///   - 首笔非 L1 attributes deposit（Ecotone+ 每块必有；识别方式见 §6 决策点 2）
OpBlockResult processOpBlock(const evmone::state::StateView& view,
    const evmone::state::BlockInfo& block, const evmone::state::BlockHashes& hashes,
    std::span<const OpBlockTx> txs, const OpForkConfig& cfg, evmc::VM& vm, uint64_t chainId,
    const std::function<void(const evmone::state::StateDiff&)>& applyDiff);
```

执行顺序（= §4.4，逐条对 op-geth）：

1. `evmone::state::system_call_block_start(view, block, hashes, cfg.rev, vm)` → `applyDiff`。该函数内部按 rev 门控（4788 需 Cancun+、2935 需 Prague+），与 OP fork→rev 映射（Ecotone/Fjord/Granite/Holocene=CANCUN、Isthmus+=PRAGUE）**天然对齐，无需自写门控**；op-geth 对 OP 不豁免这两个调用（`state_processor.go:90-95`）。**细化（rev.2 核验）**：op-geth 侧 4788 的直接触发条件是 `header.ParentBeaconRoot != nil` 而非 fork 判断（`state_processor.go:90`）——对 OP 恒真由 header 规则闭环（Cancun 块必有 beaconRoot：`consensus/beacon/consensus.go:296-299`；OP 强制 `CancunTime == EcotoneTime`：`params/config.go:1690-1691`）；2935 的 `IsPrague ≡ Isthmus` 同由 `PragueTime == IsthmusTime` 强制（`config.go:1693-1694`）。BlockInfo 需携带 parent beacon root 供 evmone 侧使用。
2. 首笔 L1 attributes deposit 经 `runDeposit` 执行、`applyDiff` 写回，**然后** `loadOpFeeParams(view)` 解包（时序：fee params 必须来自本块 attributes 写入后的槽值）。**等价性依据（rev.2 核验）**：op-geth 是惰性 + **per-block 缓存**读槽（`rollup_cost.go:199-207`；deposit 短路不触发读 `:196-198`），首次读发生在第一笔普通 tx——其注释 `:162-164` 明言"允许本块 deposit 先处理…This behavior is consensus critical!"。「attributes 后解包一次、全块复用」与之等价（attributes 与首笔普通 tx 之间只可能有 deposit，而 L1Block 槽仅 DEPOSITOR 可写）。
3. 逐笔：deposit → `runDeposit(…, blockGasLeft)`；普通 tx → `opValidate(…, fee, blockGasLeft)` → `opTransition`。每笔后：`applyDiff(receipt.state_diff)`；`blockGasLeft -= gasUsed`；`cumulative_gas_used = 前值 + gasUsed`（deposit 与普通 tx 混排累计，op-geth 同口径）。普通 tx 的 `opValidate` 返回 `GAS_LIMIT_REACHED` 时**同样升块级错误**（对齐 rev.2 给 deposit 定的口径；op-geth `gp.SubGas` 对两类 tx 同一 gas pool）。
4. 块尾：`finalizeOpBlock(view, cfg, coinbase)` → `applyDiff`——D-10 台账状态由 🔶 升 ✅ 的条件即本步（回填须引用接线 commit）。

**明确不做**：`system_call_block_end`（7002/7251 requests 收集）——OP 禁 requests，`finalizeOpBlock` 的护栏已结构性排除；块级编排**不调**它。

### 4.2 receipt 编码与块头承诺（M-B2，新文件 `bcos-evm-ref/opstack/OpReceiptEncode.{h,cpp}` + `OpBlockSeal.{h,cpp}`）

- **0x7E receipt envelope**（**rev.2 钉死**）：**receipts-root 的编码语义 = `Receipts.EncodeIndex`（`core/types/receipt.go:568-592`），不是 `MarshalBinary`（`:279-288`）**——二者对"nonce 有、version 无"的 receipt 刻意不同（函数头注释 `:564-567` 明令不许改）。root 编码：`0x7E ‖ rlp([status, cumulativeGasUsed, logsBloom, logs, depositNonce, depositReceiptVersion])`，末两字段**成对**、以 `DepositReceiptVersion != nil` 为门（`depositReceiptRLP` 结构 `:136-148`，`rlp:"optional"`）；"只有 nonce"的形态永不进 root。赋值：Regolith+ 设 nonce、Canyon+ 设 version=1（`state_processor.go:217-227`、`receipt.go:52`）——本模块支持面 Ecotone+ ⊂ Canyon+，**恒成对附加**；字段值已由 `OpDepositReceipt.deposit_nonce/deposit_receipt_version` 携带。普通 tx 沿用标准 typed envelope（type 0/1/2/4，evmone 已有编码可对照）。
- **receipts-root**：复用 evmone 的 MPT 机制（testutils 侧，eth 路径四 root 判据已在用），叶值换成上述 OP 编码；具体接缝（generic mpt insert vs 自建 trie 组装）在 M-B2 plan 里对 REF 源码钉定。
- **块级 logsBloom**：`evmone::state::compute_bloom_filter(receipts)` 重载现成——但入参是 evmone `TransactionReceipt` 序列，deposit/普通混排的序列组装由 seal 函数负责。
- **Isthmus withdrawalsRoot**：= L2ToL1MessagePasser `0x4200…0016`（`params/protocol_params.go:31`）的 storage root——验证侧 `block_validator.go:190-198`（读取在 `:195`）；**构建侧（rev.2 钉死）`consensus/beacon/consensus.go:416-427`：在 `IntermediateRoot`（块尾 finalize 后）之后取**，即快照时点必须是**块执行完毕后**的状态——这是 §6 决策点 3 的硬约束。需要"单账户 storage root"能力（StateView 无枚举接口，见 §6 决策点 3）。
- **requestsHash**（**rev.2 钉死，从开放问题移除**）：OP Isthmus 块头 `RequestsHash` = **`EmptyRequestsHash` = sha256("") = `0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`**（`core/types/hashes.go:43-44`；构建侧 `miner/worker.go:283-290` 对空 requests 列表算 `CalcRequestsHash`；验证侧 `block_validator.go:177-184` 与 Process 的 nil requests 恒匹配）。
- **Jovian**（**rev.2 钉死**）：**`header.BlobGasUsed` 被重用为 DA footprint 存储位**——验证 `block_validator.go:119-134`（nil 拒绝、须 == `CalcDAFootprint(txs)`、超块 gasLimit 拒绝）；构建 `consensus/beacon/consensus.go:429-437`；计算 `rollup_cost.go:563-593`（scalar 取自 txs[0] attributes 载荷，仅对非 deposit 累加）；4844 整数倍检查对 OP 豁免（`consensus/misc/eip4844/eip4844.go:113-123`）。

### 4.3 验证策略（M-B1 单测 + M-B3 差分）

- **M-B1 单测**（`OpBlockExecuteTest.cpp` + 升级 `OpBlockHarnessTest` 为调库）：
  - cumulative 混排累计（deposit+普通交错，逐笔断言）；
  - gas pool 递减与恰等边界（复用 rev.2 的 `GasLimitExactlyBlockBudget` 口径，块级化：第二笔恰好吃满剩余）；
  - deposit-only 块（sequencer 空块：仅 L1 attributes）；
  - 系统调用效果：4788 槽写入（beacon root 可读回）、2935 history（Isthmus config 下）、且 **Fjord config（CANCUN）下 2935 不发生**——fork 门控的负向断言；
  - 块结构违规 / 首笔非 attributes → throw；
  - 写回时序：第二笔读到第一笔的效果（跨笔 storage 依赖用例）。
- **M-B3 差分**：op-geth 当库写块级生成器（M-T 的 opt8n 照抄执行循环经验直接复用，这次照抄 `state_processor.go` 的 `Process`），产出「pre-state + 有序 txs + 期望 receipts（含 cumulative）+ 期望块头承诺字段」向量入库，C++ 回放走 `processOpBlock` + seal。**与 M6 tx 级 t8n gate 合并立项共用生成器**（一套 op-geth 库依赖、一套向量 schema、同一 DIVERGENCES 纪律：分歧只报不修、期望值只许来自生成器实跑、豁免须四元组钉扎并经用户签核）。

## 5. 复用清单（本文引入的新照抄面）

| 复用 | 来源 | 性质 |
|---|---|---|
| `system_call_block_start` | evmone REF `system_contracts.hpp`（直接链接调用） | 零照抄 |
| `finalize` | 经 `finalizeOpBlock`（已有） | 零照抄 |
| MPT/receipts-root | evmone testutils（接缝待 M-B2 plan 钉） | 预期零照抄或极薄 |
| 块执行循环结构 | op-geth `Process()`（M-B1 的 processOpBlock 结构参照；M-B3 生成器照抄其循环） | 生成器侧照抄，C++ 侧为参照非照抄 |

`processOpBlock` 本体是 OP 语义组装、无 evmone 对应物——不入 upstream-diff 照抄面；M-B3 生成器的照抄面进其自己的 manifest（沿 M-T 先例）。

## 6. 开放问题 / 决策点（plan 前须裁定或 plan 内钉定）

1. **deposits-first 校验归属**（**rev.2 补事实**：op-geth EL **不校验**此不变量——`ValidateBody`/`Process` 全文无检查，`rollup_cost.go:573` 注释把它当前提用；不变量由 op-node derivation 维护；Jovian 起 EL 仅经 `CalcDAFootprint` 间接检查首笔是 deposit）。本文 §4.1 将其升级为 `processOpBlock` 内的块级错误——**这是超出 op-geth EL 的自加严**（语义 = 把 op-node 的 CL 不变量下沉到 EL）。**需用户裁定：自加严 or 对齐 op-geth EL（信任上游）**。
2. **L1 attributes deposit 的识别方式**（**rev.2 钉死常量与层级**）：`DEPOSITOR_ACCOUNT = 0xDeaDDEaDDeAdDeAdDEAdDEaddeAddEAdDEAd0001`（规范常量在 op-node `derive/l1_block_info.go:40`；op-geth 内仅 `eth/downloader/receiptreference.go:28` 非共识用途）；L1Block predeploy = `0x4200…0015`（`rollup_cost.go:67-68`）。**"首笔必须是 L1 info tx"的校验在 op-node CL 层**（`derive/payload_util.go:27-40`），op-geth EL（pre-Jovian）零检查。建议维持"按内容校验 + 位置约定"双重——同为**自加严**，与决策点 1 一并裁定。
3. **withdrawalsRoot 需要枚举账户 storage**，evmone `StateView` 无枚举接口——候选：(a) 由调用方（账本侧）提供该账户 storage 快照作 seal 的显式入参；(b) 扩宽本模块适配器（不动 evmone）。倾向 (a)（保持 3 方法窄接口，与 M3.5 的接口宽度结论一致）。**rev.2 补硬约束**：快照时点必须是块尾 finalize 之后（op-geth 构建侧 `consensus.go:413→421` 顺序）。M-B2 plan 定稿。
4. ~~requestsHash 确切口径~~（**rev.2 已钉死**，见 §4.2——`EmptyRequestsHash`，不再是开放问题）。
5. **M-B3 与 M6 合并立项**：建议合并（省一套生成器基建）；如分开，M6 先行（tx 级向量已有 M-T 方法论，交付更快）。

## 7. 约束（沿主 spec，全文有效）

- 尽量复用 evmone；op-geth 唯一基准；先 plan 后动手（每里程碑独立 plan + 用户过目）；断言数值纪律（差分锚定方可改数）；范围收窄须显式标注"更正"。
- 测试 GTest；每 task 全绿；`processOpBlock` 落 `bcos-evm-ref/bcos-evm-ref/opstack/`（新双名布局）。
