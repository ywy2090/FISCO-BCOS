# OP-Stack 功能正确性测试矩阵设计（v1）

> 目标读者：后续会话 / 评审者。目的：把「功能没问题」从口号变成**每格可判定**的矩阵。
> 原则（承接本 worktree 评审过程的经验教训）：
> 1. 每个格子必须挂一个**不来自被测代码**的 oracle（op-geth / op-node / registry / 不变量）；
> 2. 每个行为断言配三类格子：正向 / 负向 / 边界；
> 3. 「机制存在」不算通过——判定物必须是可观测行为（delta 复核口径）；
> 4. 矩阵本身也要被测：变异测试（种子 = 本轮 41 条 finding）证明矩阵能失败。
>
> 本文件是设计稿，不进 git（同 docs/ 现状）；落地时按「落地顺序」一节拆任务。

## 0. 术语与总原则

- **Cell** = 一个（输入组合, oracle 期望, 断言）三元组。
- **Oracle 层级**（证明力上限）：spec 文字 < 本仓实现 < 参考实现（op-geth/op-reth）< 真实 op-node/真实链。
  任何 cell 的 oracle 至少到第 3 层，或明确声明只断言不变量（并写明为什么不变量足够）。
- **三色判定**：`aligned`（与 oracle 一致）/ `divergent`（须为设计决策日志里的**已记录**偏离）/ `cannot-determine`（给最小读取集）。
- **禁止假绿**：报用例数；套件名错误 = 0 例 = 失败；stub 协作者不允许出现在断言路径。

## 1. 现有资产的证明力缺口（设计依据）

实测：t8n 语料 162 向量（isthmus 47 / invalid 46 / jovian 29 / fjord 11 / ecotone 8 / canyon 6 /
**regolith 4** / holocene 4 / granite 3）+ 130 golden engine 文件，CI 从 pin 的 op-geth 重生成；
EEST v5.4.0 100% 门限；`tools/engine_integration_test.sh` 是 curl smoke + python mock CL。

缺口：
1. **pre-Canyon 覆盖最薄**（regolith 4 条），而本轮 5 个 HIGH 里 3 个在这里（INT-F1 族、N1/N2/N3、NEW-1）。
2. golden 只比 `blockHash`，**不逐字段 diff**（S3-F7/S4-F4 这类字段存在性问题测不出）。
3. mock CL 只证明形状，不证明协议语义。
4. reorg/canonicality 序列没有系统性矩阵（N2/N3/NEW-1 全是序列类缺陷）。

## 2. 矩阵 M1：fork × Engine API 方法（S3 / S5+S6）

**维度**：fork ∈ {Regolith..Karst}（9）× 方法 ∈ {newPayload, FCU, getPayload} × 版本 ∈ {V1..V5}
× 场景 ∈ {timestamp 匹配, 不匹配, 越界版本}。

**每格期望**（oracle：op-node `rollup/types.go` 的
`NewPayloadVersion`/`GetPayloadVersion`/`ForkchoiceUpdatedVersion` 三窗 + op-geth `eth/catalyst/api.go`）：

| 场景 | 期望 |
|---|---|
| (fork, 版本) 在窗口内 | 正常处理 |
| 方法版本 ≠ 该 fork 的 profile | `-38005`（本仓已记录偏离：op-geth 部分场景返回 `paramsErr` -32602，见设计 §4.2/§8/§9） |
| 版本存在但 fork 未达 | `-38005` |
| caps 列表 | 恰好 = 实现窗口（不多不少），`ExchangeCapabilities` 形状对齐 op-geth 反射 |

**正向格子数**：9 forks × (newPayload 窗口 + FCU 窗口 + getPayload 窗口) 的每格 1 条正向 +
1 条「错版本」负向 + 1 条「窗口端点」边界 ≈ **40–60 cells**。
**落点**：`engine/test/unittests/engine/OpEngineApiVersionsTest.cpp`（已有 16 例 → 扩全矩阵），
真实调度器路径部分放 `opstack-executor/tests/OpNewPayloadRpcE2eTest.cpp`。
**验收**：每个 (fork, 方法) 组合至少 1 正 1 负；端点（窗口第一/最后一个 fork）各 1。

## 3. 矩阵 M2：payload 形状（S3 / S5+S6）

**维度**：fork × 以下字段的存在性组合 × 值合法性。

字段集：`withdrawals`、`withdrawalsRoot`、`blobVersionedHashes`、`parentBeaconBlockRoot`、
`executionRequests`、`extraData`（0/9/17B）、`gasLimit`、`blobGasUsed`/`excessBlobGas`。

关键 cell（这些正是历史 bug 所在）：

| cell | 期望 | 出处 |
|---|---|---|
| Regolith：`withdrawalsRoot` 缺席 vs 零哨兵 | 二者等价（同投影），且 RLP 头**不含**该字段 | INT-F1/S4-F6 |
| Regolith：beacon/blob 字段缺席 | 接受（pre-Ecotone 头不定义） | be3baa563 |
| Canyon：`withdrawals` present-empty + `withdrawalsRoot` present | 接受；缺任一 → 拒绝 | op-geth api.go:585-588 |
| Isthmus：`withdrawalsRoot` 取 payload 宣告值（非重导出） | 一致 | S3 设计 |
| Isthmus：`executionRequests` present-but-empty | 接受 | spec newPayloadV4 |
| `extraData` 0/9/17B 按 fork | 对应 `OpExtraDataLayout`；长度错 → 拒绝（含版本字节命名） | S3/S4 |
| getPayload V2 @Regolith vs @Canyon | 响应形状按 fork 瘦身（无 withdrawals/blob；有则拒绝回灌） | f2472b9a9 |
| getPayload V4/V5 @Isthmus/Jovian/Karst | 逐字节不变（回归门） | — |

**oracle**：op-geth `ExecutableDataToBlock`/`api.go` 形状分支 + spec `exec-engine.md` 各节；
对每格补一条「响应回灌 newPayload 必须被接受」的 round-trip 断言（f2472b9a9 已有 1 例 → 扩到全 fork）。
**落点**：`OpEngineApiVersionsTest.cpp` + e2e；**验收**：M2 与 M1 的叉积抽全（至少每 fork 一整行）。

## 4. 矩阵 M3：L1 费用与收据（S4）

**维度**：fee 模型 {Bedrock, Ecotone, Fjord} × 输入槽值 × 收据字段存在性。

| 维度 | 边界 cell | 期望（oracle） |
|---|---|---|
| Bedrock 公式 | `overhead` 溢出 uint64 / 极大 scalar | **饱和不回绕**（op-geth `rollup_cost.go:292-315` 是 `*big.Int`；op-reth u128 saturating） |
| Bedrock | scalar 非 1e6 倍数 | 存原始值；RPC `l1FeeScalar` 语义按上游（存在性一致，词法类型差异已在 INDEX 记录） |
| Ecotone | slot 1/3/7 组合 | `calldataGas` 公式对拍 op-geth |
| Fjord | FastLZ 成本 | 对拍 op-geth `rollup_cost.go` FastLZ 分支 |
| Holocene 激活块 | parent 未到 Holocene、新块已到 | 常数（9B extraData 的 baseFee），denominator 按**新块**时间选 50/250 |
| Canyon 切换 | 新块 ts ≥ canyon_time | `EIP1559DenominatorCanyon=250` |
| Jovian | `max(gasUsed, blobGasUsed)` 计量 + `minBaseFee` 地板 | jovian/exec-engine.md 121-128 / 47-56 |
| 收据字段 | pre-Ecotone {l1Fee,l1GasUsed,l1GasPrice,l1FeeScalar} / Ecotone+ 反转 | 存在性对拍 `gen_receipt_json.go` |

**oracle**：golden 语料逐字段 diff（见 §9）+ 独立 Python 参考实现（语料生成器已有）。
**落点**：`opstack-executor/tests/*`、`test-bcos-rpc`、t8n replay。
**验收**：每模型 ≥1 条溢出/极值 cell；收据字段存在性矩阵全 fork。

## 5. 矩阵 M4：import / FCU / canonicality 序列（S5+S6）——最高价值

**维度**：操作序列。这是序列类缺陷（N1/N2/N3/NEW-1）的系统性覆盖。

基础序列（枚举，每条后跑不变量组 I1–I6）：

| # | 序列 | 期望 |
|---|---|---|
| S1 | newPayload B1,B2,B3 → FCU(B3) 前推 | 三块 canonical，指针推进 |
| S2 | B1,B2,B3 → FCU(直接跳 ledger-canonical head，跳中段) | 指针推进，中段已 canonical |
| S3 | A-B-C canonical → import B′(父 A) → FCU(B′) **同高切换** | B′ canonical；A 的行保留；C 的行/occupant 清除；**by-hash(A/B) 仍解析，by-hash(C) 不再解析**（NEW-1 判据） |
| S4 | A-B-C-D → import B′ → FCU(B′)（head 之上多块孤儿） | 同 S3 且后续在高度 3 导入 B′ 子块 VALID（N3 判据） |
| S5 | FCU 回退（head 低于当前 tip） | 不 rewind latest；safe/finalized 刷新 |
| S6 | 重复导入同 payload | 不重执行，VALID |
| S7 | 非法 payload → 合法 payload | 错误码正确，无状态污染 |
| S8 | 被拒 FCU 后再 FCU | 指针不得被拒请求推进（N6 判据） |
| S9 | canonicalize 中链失败（在每个 merge 步注入） | backend + cache 两层可观测不变（F3 判据，**必须用带 cache 的 MLS**） |
| S10 | canonicalize 期间并发 newPayload | 无锁跨 await；失败方 SYNCING 且可重试（F5 判据） |
| S11 | finalized 推进后的内存/flat 界 | 上界收缩（F4，Tier-2 前仅断言现有 prune 行为） |
| S12 | BLOCKHASH 跨 payload 链读取 | `BLOCKHASH(第1块高度)==B1` 经真实 `RecentBlockHashes`（N4 判据） |

**不变量组**（每步后全跑，oracle = 不变量，无需上游）：

- I1 `SYS_NUMBER_2_HASH[n] ↔ SYS_HASH_2_NUMBER[hash(n)]` 双向一致（n ≤ head）；
- I2 `SYS_NUMBER_2_TXS[n]` 可解析且条数 = 头的 tx 数（N1 判据）；
- I3 `n > head` ⇒ 号映射不存在；对应 `HASH_2_NUMBER` 也不存在（NEW-1/N2 判据）；
- I4 ImportedStore occupant 图与 canonical 链一致（≤ head 的挂载点不悬空）；
- I5 canonical 后 `stateRoot(SYS_CURRENT_STATE) == head.stateRoot`；
- I6 tip 指针单调不回退（除显式回退场景）。

**落点**：`engine/test/unittests/engine/OpEngineImportFcuTest.cpp`（真实 `OpScheduler`，
不得用 `FabricatedRootsStub` 断言导入/承诺路径）+ e2e。
**层级 2**：随机序列生成器（操作 ∈ {newPayload, FCU, import-fork, finalize 推进}，长度 50–200），
只断言 I1–I6 与「无 -32603 泄漏」，跑 N 万步。

## 6. 矩阵 M5：genesis / 调度（S1 / S2）

| 维度 | cell | 期望（oracle） |
|---|---|---|
| 创世时间 × fork | ts0 在每档前/后/等于 | 字段集 16(London)/18(Canyon)/21(Cancun)/22(Isthmus)；baseline=`0:<该档>` |
| registry 全链 | **zip 内每一条链**（不只 base/op/rehearsal） | `header_hash == genesis.l2.hash`（独立重算实现） |
| 缺键 | 无 gasLimit/baseFeePerGas | op-geth `GenesisGasLimit=0x47e7c4` / `InitialBaseFee=0x3b9aca00` |
| Isthmus-at-genesis | ts0 ≥ isthmus_time | `withdrawals_root = L2ToL1MessagePasser` 存储根（op-reth golden `0x8ed4baae…`） |
| 调度 codec | 缺档 / 重复 ts / 未知名 / cap 16 / 512B 上限 | 拒绝且错误文案准确 |
| 跨语言 pin | python `EL_FORKS` vs C++ `c_opForkNames` | 解析 C++ 头后全等（名字+顺序） |
| rollup.json | 每条链 | 字段映射对齐 op-node `LoadOPStackRollupConfig`；`karst_time` 为**已记录**偏离 |

**验收**：registry 全链扫描进 CI（有 zip 则跑，无则 skip）。

## 7. 矩阵 M6：线格式 / RPC（S3 / S4）

| 维度 | cell | 期望 |
|---|---|---|
| getPayload 响应 | 每 (fork, 版本) | executionPayload 字段集 = 该 fork 头字段集；回灌 newPayload 接受 |
| receipt JSON | 每 fork | 字段存在性对拍 op-geth；`l1FeeScalar` 仅 pre-Ecotone |
| by-hash / by-number | M4 序列后 | 二者解析到同一块且同一内容 |
| block size | 含/不含 OP 元数据的收据 | tars `size()` 计入每个可选字段 |
| caps | — | 恰好实现窗口 |

## 8. 矩阵 M7：负向 / 对抗（全部段）

| 维度 | cell | 期望 |
|---|---|---|
| 承诺伪造 | 改 stateRoot/receiptsRoot/txRoot 再 newPayload | INVALID + latestValidHash 语义 |
| 错误码轴 | 不可执行 vs 无效 vs 内部 | INVALID / SYNCING / -32603 不混淆（不得把执行错折叠成 -32603） |
| 首错顺序 | 同时违反窗字段与头字段 | 窗字段消息（已如实注释，测试钉住） |
| activation 块 | 非存款交易 | 拒绝（deposits-only） |
| stale latestValidHash | 竞态 | SYNCING/重试安全 |
| RLP 头形状 | 每 fork 与 op-geth 逐字段 diff | 见 §9 |

## 9. 落地顺序（按性价比）

1. **M4 序列矩阵 + 不变量组**（直接覆盖全部 5 个 HIGH 的病灶，S9/S10/S3/S4 是新增用例最多的部分）。
2. **golden 逐字段 diff**（header RLP 字段集 / getPayload 响应 / receipt JSON 三类产物各建一个 diff 基线，
   CI 从 pin 的 op-geth 重生成）+ **pre-Canyon 语料补齐**（regolith/canyon ≥ 15 条，含首块/末块/非法）。
3. **M1/M2 叉积补全**（现有 16 例 → 全矩阵）。
4. **M5 registry 全链扫描**进 CI。
5. **变异测试**：种子 = 本轮 41 条 finding 的修复 diff（对每个修复做「反转修复」变异，要求矩阵变红）。
6. **真实 op-node e2e**（S7）作为终审；EEST 已覆盖 EVM 层。

## 10. 「证明完毕」判据

- M1–M7 每格三色判定齐备；`divergent` 全部有设计决策日志行；
- 每个行为断言有正/负/边界三类格子；
- 变异测试：对 41 条 finding 的修复逐条反转，矩阵**全部变红**；
- 语料覆盖：每 fork 至少含「首块 + 末块 + 非法载荷」，pre-Canyon 占比与其历史缺陷率匹配；
- 随机序列 I1–I6 在 ≥1e5 步内零违例；
- 真实 op-node 握手 + 建块 + 导入全绿。

## 附：与本评审台账的对应

矩阵格 ↔ 台账 finding 的映射（部分）：M4-S3/S4 ↔ N2/N3/NEW-1；M4-S9 ↔ F3；M4-S10 ↔ F5；
M4-S12 ↔ N4；M2-getPayload ↔ S3-F7；M3-收据 ↔ S4-F1/F3/F4/F5；M5-缺键 ↔ S1-F3；
M5-Isthmus ↔ S1-F4；M6-caps ↔ S3-F9；M2-Regolith哨兵 ↔ INT-F1/S4-F6。
