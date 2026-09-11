# S3 Engine API 版本门与 extraData/baseFee 设计

- **日期**：2026-09-10
- **状态**：已确定（方案 A；2026-09-10 审查已吸收）
- **对照分支**：`ywy2090/feat/karst-on-release-3.18` @ `.worktrees/karst-on-5550`
- **规范权威**（实现时必须打开原文，禁止只凭本设计/计划记忆）：
  - OP specs（本地优先）：`/Users/octopus/octo/code/ethereum-optimism-specs/specs/protocol/`  
    在线：[exec-engine](https://specs.optimism.io/protocol/exec-engine.html)、[holocene/exec-engine](https://specs.optimism.io/protocol/holocene/exec-engine.html)、各 fork `*/exec-engine.md`
  - op-geth：`/Users/octopus/octo/code/op-geth/` — `eth/catalyst/api.go`、`api_optimism.go`、`consensus/misc/eip1559/{eip1559,eip1559_optimism}.go`
  - op-node：`/Users/octopus/octo/code/optimism/op-node/rollup/types.go` — `NewPayloadVersion` / `GetPayloadVersion` / `ForkchoiceUpdatedVersion`
  - 辅证：op-reth `optimism/consensus/src/validation.rs`、`rpc-engine-api`（与 geth/spec 冲突时 **以 spec 为准，其次 geth**；本设计已记录的有意偏离除外）
- **产品起点**：Bedrock 起跟官方链。S3 只开门与计价参数，不执行、不导入。
- **范围**：OP 车道 `engineApiFor` / `resolveEngineForkAt` / `newPayload`·`getPayload`·有 attrs 的 FCU 版本对表、caps、`extraData` 形状、`CalcBaseFee` 与 geth 同两套时钟。
- **入口**：Engine RPC。禁止靠 t8n 关门。

---

## 1. 理解摘要

- op-node 按 payload 时间戳调 `newPayloadV2/V3/V4`、`getPayloadV2/V3/V4`（Karst 用 V5）、有 attrs 时 `forkchoiceUpdatedV1/V2/V3`。
- 现网 `engineApiFor` 对 Isthmus/Jovian 固定 FCU V3 / getPayload V4 / newPayload V4；Karst 只把 getPayload 升到 V5。`tryEngineForkId` 不认历史档 → `UnsupportedTimestamp`。`isNewPayloadVersionSupported` 只认 V4；caps 只广告 `newPayloadV4` + `getPayloadV4/V5`。有 attrs 的 FCU 还写死「必须 V3」。
- EL 不选方法号，只做 **方法号 × timestamp 对表**。对不上 → **一律 `-38005`**。这是有意偏离：geth `NewPayloadV2` 在 Cancun 之后走 `paramsErr`（更像 `-32602`），`NewPayloadV3+` / 错 fork 的 FCU 才是 `unsupportedForkErr`。S3 不跟 geth 按方法号分错误码，避免执行时来回改。reth `UnsupportedFork` 与本选择同形。
- extraData **不**决定版本号。它只约束：本块头形状，以及 **下一块** baseFee（看**父块**是否已 Holocene）。
- 给验证者 + 再重组 BuildStart（`FCU(祖先, attrs)` 后 `getPayload`）。
- 不做：OVM；改生产 codec 名字（S2）；L1 fee / 收据（S4）；InsertWithoutSetHead / FCU 跳号（S5+S6）；Eth 车道。

## 2. 假设

1. 权威实现：本地 `op-geth` 的 `NewPayloadVn`/`GetPayloadVn`/`CalcBaseFee`，以及 op-reth 的 `validate_payload_timestamp` / `next_block_base_fee`。
2. 时间戳一律 Unix **秒**（内部 ms 先换算），与现 `requireOpEngineForkAt` 一致。
3. 生产 `parse` 仍只认 isthmus|jovian|karst（S2）。S3 单测注入带历史激活时间的 schedule；现网只有 Isthmus+ 时间时，行为与今天相同（全程 V4/V5）。
4. `getPayloadV2/V3` **广告且实现**。V3/V4/V5 的 payload **shape** 仍是 PayloadV3（geth `GetPayloadV4` 只认 `PayloadV3`）。
5. extraData → 下一块 baseFee **在 S3**。S4 隔离 t8n 可继续采用 fixture `baseFee`。
6. 通过版本/形状后，仍进现有 `runOpNewPayloadSteps` / `assembleGetPayloadData`，不按方法号分叉导入。
7. NFR：正确性优先；热路径多一次 `engineApiFor` 可接受。
8. S3 **只钉 OP Mainnet 常量**：elasticity=**6**，Bedrock denom=**50**，Canyon denom=**250**。不从 schedule/链配置覆盖（YAGNI；官方测试链若不同，S1/S7 再开）。

## 3. 决策日志

| 决定 | 备选 | 为何 |
| --- | --- | --- |
| 方案 A：扩 `EngineApiProfile` + 一套 extraData/baseFee | B 只认方法号；C 等 S4 `configAt` | 与现 `engineApiFor` / `OpBaseFee.h` 一条线；不堵在 S4 |
| 方法号由 CL 选，EL 只对表 | EL 自己降级/升级方法 | op-node 已按 fork 选好 |
| 实现并广告 getPayload V2–V5 | 只开 newPayload | 再重组 BuildStart 会 `getPayload`；用户指定 |
| extraData 语义进 S3 | 只验字节；或留给 S4 | 用户指定；geth/reth 都在 Engine/共识计价，不在 EVM |
| 版本窗用以太坊 fork 别名 | 自造 V2=Bedrock 等名字 | Canyon=Shanghai，Ecotone=Cancun，Isthmus=Prague |
| 两套时钟：形状看本块，Holocene 计价看父块 | 都看本块 | spec + reth#13060 + geth `IsOptimismHolocene(parent.Time)` |
| Canyon 分母看**新块**时间 | 看父块 | geth `BaseFeeChangeDenominator(time)` 的 time 是新块 |
| 不改生产 codec 名字 | S3 顺手放开 parse | 那是 S2 |
| 有 attrs 的 FCU 按表收 V1/V2/V3 | 继续写死 V3 | 否则 Canyon 造块/再重组 BuildStart 被拒 |
| 关门 = Engine 单测 | 等官方 e2e | 与 S4/S5 一样 |
| 错版本一律 `-38005` | 跟 geth：V2+Cancun 用 `-32602` | 一张错误表；不按方法号分码 |
| 新增 `calcOpNextBlockBaseFee`，不改 `calcOpBaseFee` 签名 | 改旧函数签名 | S5 已有 `calcOpBaseFee(parent, parentIsJovian)`；禁止两套数 |
| FCU V1（Regolith）纳入 S3 矩阵 | 推迟到补丁 | 有 attrs 的表含 V1；夹具必须清 withdrawals/beacon |
| 创世 extraData 例外归 S1 | S3 `Empty` 对 block 0 放行 | 创世不经 `newPayload`；S3 对 Engine 路径一律拒 pre-Holocene 非空 |

## 4. 方案 A

### 4.1 架构

```
payload / attrs / 缓存块 timestamp（秒）
  → resolveEngineForkAt → EngineForkContext
       api.{forkchoiceUpdated, getPayload, newPayload}
       extraDataLayout: Empty | Holocene9 | Jovian17
  → 方法号 ≠ api.*           → UnsupportedFork（-38005）
  → extraData 形状 ≠ layout  → INVALID（newPayload）或造块失败（FCU）
  → baseFeePerGas ≠ CalcBaseFee(parent, newTs) → INVALID
  → 通过 → 现有 runOpNewPayloadSteps / assembleGetPayloadData / buildOpPayload
```

`engineApiFor` 现只分 Karst / 其他。改为按 `forkAt(ts)` 填满下表。`tryEngineForkId` / `OpForkId` **必须能表示历史档**（至少能 `engineApiFor`；不必带齐 S4 的 EVM `OpForkConfig`）。`resolveEngineForkAt` 对历史档不得再回 `UnsupportedTimestamp`（除非 ts &lt; genesis/baseline）。

### 4.2 版本表（与 op-node 一致，加上本仓库 Karst V5）

| 时间窗 | newPayload | getPayload | FCU（有 attrs） | extraData（本块） |
| --- | --- | --- | --- | --- |
| ≥ baseline 且 &lt; Canyon | V2 | V2 | V1 | 空 |
| Canyon ≤ t &lt; Ecotone | V2 | V2 | V2 | 空 |
| Ecotone ≤ t &lt; Holocene | V3 | V3 | V3 | 空 |
| Holocene ≤ t &lt; Isthmus | V3 | V3 | V3 | 9B `0x00‖denom‖elasticity` |
| Isthmus ≤ t &lt; Jovian | V4 | V4 | V3 | 9B |
| Jovian ≤ t &lt; Karst | V4 | V4 | V3 | 17B `0x01‖…‖minBaseFee` |
| ≥ Karst | V4 | V5 | V3 | 17B |

Jovian 与 Isthmus 的 **方法号相同**（都是 V4/V3）；差在 extraData 17B 与 minBaseFee。`OpForkId` 仍要区分，供 layout / `hasDaFootprint`。

无 attrs 的 FCU：现网已收 V1–V3；op-node 心跳常用 V3。保持「无 attrs 不按表卡死 V3」，与 op-node `attr==nil → FCUV3` 一致。

**getPayload 兼容窗（geth）**

| 方法 | payload id shape | 缓存块 timestamp 必须落在 |
| --- | --- | --- |
| V2 | PayloadV1 或 V2 | Paris/Shanghai ≡ &lt; Ecotone |
| V3 | PayloadV3 | Cancun ≡ Ecotone ≤ t &lt; Isthmus |
| V4 | PayloadV3 | Prague ≡ Isthmus ≤ t &lt; Karst |
| V5 | PayloadV3 | Osaka ≡ Karst |

FCU 造块写入的 shape（现 `payloadShapeVersion` = `min(method, 3)`，与 geth 一致）：

| FCU 方法 | 存盘 shape |
| --- | --- |
| V1 | PayloadV1 |
| V2 | PayloadV2 |
| V3（及未实现的 V4） | PayloadV3 |

**newPayload 形状（geth NewPayloadVn + checkOptimismPayload）**

- V2 + **Regolith**（pre-Shanghai）：`withdrawals` **必须缺（nil）**；无 beacon / blob / withdrawalsRoot；extraData 空。
- V2 + **Canyon**：`withdrawals` **有且空**（不是 nil）；其余同 Regolith V2。
- V3：Cancun 窗；withdrawals 空数组；`parentBeaconBlockRoot` 必有；blob 列表空；withdrawalsRoot 必须缺。
- V4：Prague+ 窗；另要 withdrawalsRoot；`executionRequests` 现空数组。

有 attrs 的 FCU（geth `ForkchoiceUpdatedVn` + `checkOptimismPayloadAttributes`）：

- V1（Regolith）：`withdrawals` 与 `parentBeaconBlockRoot` **必须缺**；禁止 `eip1559Params`。
- V2（Canyon）：`withdrawals` 有且空；**禁止** beacon / `eip1559Params`。
- V3（Ecotone+）：`withdrawals` 有且空；beacon 必有；Holocene+ 才要 8 字节 `eip1559Params`（Ecotone/Fjord/Granite 带了 params → 拒）。

错版本（V2+Ecotone 时间、V4+Canyon 时间）→ `-38005`，不要回 INVALID 冒充执行失败。`requireOpEngineForkAt` 必须在 `handleOpNewPayload` 的 `catch (...)` **之外**，否则 `-38005` 会被收成 `-32603`。

### 4.3 extraData 与 CalcBaseFee

对齐 geth `CalcBaseFee` / `ValidateOptimismExtraData` / `DecodeOptimismExtraData`，以及 reth `next_block_base_fee`。

**本块 extraData（看本块 ts）**

- pre-Holocene：**Engine 路径**（newPayload / FCU 造块）必须空。创世 extraData 非空是 spec 例外，但创世不经 `newPayload`；放行归 **S1** 加载官方 genesis，S3 不在 `Empty` layout 里开洞。
- Holocene（未到 Jovian）：9 字节，version=0，denom/elasticity 均非 0。
- Jovian+：17 字节，version=1；1559 两段非 0；minBaseFee 任意。
- FCU attrs：Holocene+ 必须带 8 字节 `eip1559Params`；均为 0 时 extraData 填链上旧常量（Canyon 250/6）。pre-Holocene attrs 带了 params → 拒。

**本块 baseFee（两套时钟）**

```
CalcBaseFee(parent, newBlockTime):   // S3 钉死 6 / 50 / 250，不读链配置
  elasticity = 6
  denom = IsCanyon(newBlockTime) ? 250 : 50
  if IsHolocene(parent.time):          // 只看父块
      从 parent.extraData 覆盖 denom/elasticity
      Jovian 父再读 minBaseFee
  else:
      忽略父 extraData（此时应为空）
  按 EIP-1559 算；Jovian 父用 max(gasUsed, blobGasUsed) 计量
  若有 minBaseFee 则取 max(result, minBaseFee)
```

Holocene **激活块**：本块 extraData 已是 9 字节，但父块未 Holocene → 自己的 baseFee **仍用 50/250 常量**。reth 曾用本块时间去解码父 extraData，激活块会炸，已改（#13060）。

现 `calcOpBaseFee(parent, parentIsJovian)` 假定父 extraData 已是 9/17 字节，**签名不改**（S5 夹具已调用）。S3 新增 `calcOpNextBlockBaseFee(parent, OpBaseFeeClock)`：

- `parentIsHolocene == false`：用 50/250 常量（弹性 6）；忽略父 extraData。
- `parentIsHolocene == true`：委托现 `calcOpBaseFee`（含 Jovian 地板）。
- newPayload 与 FCU `buildOpPayload` **只调新函数**，禁止两套数。
- newPayload：`payload.baseFeePerGas` 必须等于该结果，否则 INVALID（`latestValidHash=parent`）。

### 4.4 组件

| 组件 | 改动 |
| --- | --- |
| `OpForkId` | 增加历史档（Regolith/Canyon/Ecotone/Fjord/Granite/Holocene）。**禁止**把整型写入账本。 |
| `tryEngineForkId` / `engineApiFor` / `resolveEngineForkAt` | 按 §4.2 填 profile；历史 ts 不再 `UnsupportedTimestamp` |
| `isNewPayloadVersionSupported` | 收 V2–V4（仍不收 V1，与 op-node 一致） |
| 有 attrs 的 FCU | 删「必须 V3」；改为 `version == ctx.api.forkchoiceUpdated` |
| `supportedOpCapabilities` | 广告 `newPayloadV2–V4`、`getPayloadV2–V5`、FCU V1–V3 |
| `validateOpNewPayloadRequest` / extraData | 按 layout + version；Regolith V2 withdrawals 必须缺 |
| `OpBaseFee.h` | 新增 `calcOpNextBlockBaseFee`；**不改** `calcOpBaseFee` 签名 |
| `assembleGetPayloadData` | **必须** V2 瘦身：`executionRequests` / `parentBeaconBlockRoot` / blob 字段对 V2 请求为缺省；不是「泄漏了再改」 |
| RPC Endpoint | `newPayloadV2/V3`、`getPayloadV2/V3` 已有则只改服务层拒收 |

### 4.5 数据流

```
op-node NewPayloadVersion(ts) → engine_newPayloadVn
  → isNewPayloadVersionSupported
  → requireOpEngineForkAt(ts)；n == api.newPayload   // 必须在 catch(...) 外
  → 形状 + extraData(layout) + baseFee == calcOpNextBlockBaseFee(parent, clock)
  → runOpNewPayloadSteps（S5 导入，与方法号无关）

op-node GetPayloadVersion(ts) → engine_getPayloadVn
  → 按 id 取缓存
  → n 与 shape 窗匹配，且缓存块 ts 落在该 fork
  → assemble（V2 瘦身）

op-node FCU(attrs) → engine_forkchoiceUpdatedVn
  → n == api.forkchoiceUpdated(attrs.ts)
  → Holocene+ 验 eip1559Params；pre-Holocene 禁止 params
  → buildOpPayload：extraData = Encode(本块 ts)；baseFee = calcOpNextBlockBaseFee(parent, clock)
```

### 4.6 错误

| 条件 | 返回 |
| --- | --- |
| 方法号与 ts 窗不符 | `-38005` UnsupportedFork |
| getPayload id shape / ts 窗不符 | 同上 |
| extraData 形状错、pre-Holocene 非空（Engine 路径） | newPayload INVALID；FCU 造块 INVALID |
| `baseFeePerGas` ≠ `calcOpNextBlockBaseFee` | INVALID，latestValidHash=parent |
| 未知 getPayload id | 现有 UnknownPayload `-38001` |
| 缺父 / 导入 | S5（SYNCING），S3 不发明 ACCEPTED |

### 4.7 测试矩阵（关门）

进程内 `test-bcos-engine`。注入含 Canyon/Ecotone/Holocene/Isthmus/Karst 时间的测试 schedule。

| 用例 | 断言 |
| --- | --- |
| 各窗正确方法 newPayload | 过门（允许随后 INVALID/SYNCING；**禁止** `-38005`） |
| V2 + Regolith：withdrawals 缺 | 过门；有 withdrawals → INVALID |
| V2 + Canyon：withdrawals 空数组 | 过门；缺 withdrawals → INVALID |
| V2 + Ecotone ts / V4 + Canyon ts | `-38005`（不是 `-32603` / `-32602`） |
| getPayloadV2 取 Canyon 块 / V3 取 Ecotone 块 / V4 取 Isthmus / V5 取 Karst | 返回 envelope；V2 **无** blob/beacon/`executionRequests` |
| getPayloadV3 取 Canyon 块 | `-38005` |
| FCU V1 + Regolith attrs（无 withdrawals/beacon/params） | 过门并返回 payloadId |
| FCU V2 + Canyon attrs（空 withdrawals、无 beacon/params） | 过门并返回 payloadId |
| FCU V3 + Ecotone attrs（有 beacon、无 eip1559Params） | 过门并返回 payloadId |
| FCU V2 + Isthmus attrs | `-38005` |
| pre-Holocene extraData 非空 | INVALID |
| Holocene 激活块：本块 9B extraData，baseFee 用 Canyon 常量（父 extra 空） | 匹配 geth 数字 |
| Holocene 下一块：baseFee 用父 9B | 匹配 |
| Jovian 父 + minBaseFee 地板 | 匹配 |
| Canyon 激活块：denom=250，elasticity=6 | 匹配 |
| caps 含 newPayloadV2/V3、getPayloadV2/V3 | 列表断言 |
| 现网仅 Isthmus+ schedule | 行为与改前相同（V4/V5） |

不测：S4 收据、S5 跳号、官方主网墙钟、生产 parse 历史名字、创世 extraData 放行（S1）。

## 5. 明确不改

- `OpForkScheduleCodec` 生产名字 / `c_maxOpForkActivations`（S2）
- L1 fee / `configForFork` EVM 语义 / 收据（S4）
- ImportedStore / SetCanonical / Tracker +1 默认（S5+S6）
- Eth `EngineServiceImpl` / `EthEngineService` 版本窗
- 生产 `loadOpFeeParams` 缺槽当 0

## 6. 风险

- 有 attrs 的 FCU 从「只 V3」放开后，Regolith V1 / Canyon V2 必须能 `buildOpPayload`。夹具不得复用 `makeOpPayloadAttributes()` 默认的 beacon/`eip1559Params`。
- `handleOpNewPayload` 的 `catch (...)` 会把 `UnsupportedFork` 收成 `-32603`。对表必须在 `try` 外。
- newPayload 与 FCU 必须同调 `calcOpNextBlockBaseFee`，禁止两套数。
- 只开方法、不算 pre-Holocene baseFee：Holocene 激活块会对不上官方 EL。
- S3 与 S5+S6 都改 `OpEngineService.inl`：**同一 worktree 禁止并行**。
- 接官方链还要 S2（名字）+ S4（历史执行）+ S5+S6（导入）。S3 单独不能同步 Mainnet。

## 7. 与其它工作项

| ID | 关系 |
| --- | --- |
| S2 | 生产加载 `canyon_time` 等名字。S3 不改 codec |
| S4 | 执行语义。S3 不改 L1 fee；S4 金向量仍禁止 newPayload |
| S5+S6 | 导入/FCU 标签。S3 通过后同一 `runOpNewPayloadSteps`。**不要与 S3 同时改 `OpEngineService.inl`** |
| S1 / S7 | 官方 genesis 与真链对拍，在 S3 开门之后 |

## 8. 执行时权威对照（每个实现 Task 强制）

冲突顺序：**OP protocol spec → op-geth → 本设计已记录的有意偏离 → 本仓库现码**。  
禁止：用 FISCO 今日实现「反推」官方语义；禁止在未打开下列原文的情况下写 `validate*` / `calcOp*` / 版本表。

| 实现主题 | 先打开 |
| --- | --- |
| 方法号 × timestamp | `optimism/op-node/rollup/types.go` 三函数；spec `exec-engine.md` Engine API 节 |
| newPayload 形状 / withdrawals | geth `api.go` `NewPayloadV2/V3/V4`；`api_optimism.go` `checkOptimismPayload` |
| FCU attrs | geth `ForkchoiceUpdatedV1/V2/V3`；`checkOptimismPayloadAttributes` |
| getPayload shape / 时间窗 | geth `GetPayloadV2`–`V5` |
| extraData / 1559 params | spec `holocene/exec-engine.md`；geth `eip1559_optimism.go` `ValidateOptimismExtraData` / `Encode` / `Decode` |
| 下一块 baseFee | geth `eip1559.go` `CalcBaseFee` + `IsOptimismHolocene(parent.Time)`；`eip1559_optimism.go` |
| caps | geth `eth/catalyst/api.go` 广告列表（全量，不按 fork 裁剪） |

**唯一允许不跟 geth 错误码的偏离：** 方法号 × timestamp 不符一律 `-38005`（geth `NewPayloadV2` 在 Cancun 后是 `paramsErr`）。其它语义（withdrawals nil/空、两套时钟、extraData）必须与 geth/spec 一致。若发现新分歧：先停，写入本设计决策日志，再改代码。

## 9. 规范对照表

| 源 | 本里程碑 |
| --- | --- |
| op-node `NewPayloadVersion` / `GetPayloadVersion` / `ForkchoiceUpdatedVersion` | §4.2 表（+ Karst getPayload V5） |
| geth `NewPayloadV2/V3/V4` + `checkFork` | 方法 × Shanghai/Cancun/Prague；V2+Cancun 的 `paramsErr` **有意改成 `-38005`** |
| geth `GetPayloadV2–V5` shape + timestamp | §4.2 兼容窗 |
| geth `checkOptimismPayload` / `NewPayloadV2` withdrawals | Regolith nil、Canyon 空数组 |
| geth `CalcBaseFee` + `IsOptimismHolocene(parent.Time)` | §4.3 |
| reth `validate_payload_timestamp` | 同错版本 `-38005` |
| reth `next_block_base_fee` + #13060 | Holocene 计价看父块时间 |
| holocene spec extraData / attrs `0,0` | §4.3 |

公式、收据、导入见 S4 / S5+S6 文档。
