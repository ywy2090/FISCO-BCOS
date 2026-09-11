# OP-Stack 测试体系设计 —— oracle 工件化与 M1–M7 落地

> 状态：brainstorm 定稿（分期 A／三层 CI A／P4 归 S7 A／自研变异 harness A／总体架构方案一），
> 并已按独立评审的 14 条 finding（R1–R14）修订。
> 本文是实施计划（writing-plans）的输入。姐妹文档 `docs/2026-09-11-opstack-test-matrix-design.md`
> 定义 M1–M7 的格子语义与"证明完毕"判据；**两者落点冲突处以本文为准**（见 §5.3）。
> 惯例：本仓过程文档不入库，本文留在 worktree `docs/`（untracked）。

## 1. 背景与目标

对 S1–S6 六轮独立评审产出 43 条 finding：**BLOCKER 1 / HIGH 8 / MEDIUM 11 / LOW 23**
（= 41 fixed + 2 有据延后 wontfix：S5+S6 的 F4 prune、N5 RPC by-hash，均转 Tier-2）。
其中「已推送代码上的 HIGH、进入 Task 7 优先队列先落地」的 5 条是
**F3 / N1 / N2 / N3 / INT-F1**；delta 轮另发现 2 条 HIGH（NEW-1 由修复引入、NEW-3 预存）。
统计口径：对六个有效轮 `findings.json` 的 `severity` 计数。

8 条 HIGH 里有 3 个（**INT-F1、NEW-1、NEW-3**）**只有跨段对拍与"修后重跑原检查项"才能暴露**。
目标：把"功能没问题"变成可执行、防漂移、可 CI 化的测试体系——oracle 全部落到 pin 的上游工件
（op-geth / op-reth / op specs / op-node / registry / EEST）上，且矩阵本身被变异测试验证。

### 1.1 既有基线（复用，不重建）

**语料流水线在两个仓里**（这一点决定所有落点，见 §5）：

- FISCO-BCOS 仓**只追踪一个 symlink**：`opstack-executor/tests/t8n`（mode `120000`）→
  `/Users/octopus/.cache/fisco-t8n-corpus/opstack-executor/tests/t8n`。
- 实际内容在外部仓 **`FISCO-BCOS/op-stack-e2e-tests`**（本地 checkout `~/.cache/fisco-t8n-corpus`，
  HEAD `91c6a9f`）：`generator/*`（含 `regen.sh`、`main.go`、`cases*.go`）、`.t8n-pin`、
  `golden/engine/SHA256SUMS` 等契约文件在此追踪；生成物 `vectors/*.json`、`cases/`、
  `golden/engine/*.golden.json`/`chained/*.json` 由**该仓** `.gitignore` 忽略。
- 规模（计数命令见下）：`vectors/*.json` = **158**（其中 manifest 注册 **156**）；
  `golden/engine/**/*.golden.json` = **116**（`golden/` 目录下所有文件共 130，含 `manifest.txt`、
  `SHA256SUMS`、chained 的 `.pre/.post` 输入）。
  ```bash
  ls  .../t8n/vectors/*.json | wc -l                      # 158
  grep -v '^#' .../t8n/vectors/manifest.txt | sed '/^$/d' | wc -l   # 156
  find .../t8n/golden -name '*.golden.json' | wc -l        # 116
  ```
- `regen.sh` 的三条判据（脚本头部原文）：① 每个 `cases/*.in.json` 都产出对应的
  `vectors/<base>.json` 与 `golden/engine/<base>.golden.json`，缺一即失败；② manifest 的
  非注释非空行集合 == cases ∪ 三模式产物集合（sort+diff 对拍，防孤儿/漏注册）；
  ③ `git diff --exit-code` 只应覆盖非生成契约文件（`vectors/manifest.txt`、`vectors/*.md`、
  `golden/engine/manifest.txt`、`golden/engine/SHA256SUMS`）——即 regen 不得改动它们。
- CI：`FISCO-BCOS/op-stack-e2e-tests/.github/actions/opstack-t8n-regen@759a9af0` 在测试前
  checkout 资产并 re-symlink（`.github/workflows/workflow.yml:151` 为 symlink 创建行）。
- EEST v5.4.0（`execution-spec-tests` release `v5.4.0`），CI 100% 通过率门限。

### 1.2 引用树的清洁性（前置纪律）

**oracle 行号一律以 `git show <pin>:<path>` 为准，禁止直接 grep 本机工作树。**
现状：`/Users/octopus/octo/code/op-geth` 工作树**脏**（`eth/catalyst/api.go` 与 `miner/*` 的
注释被翻译成中文，纯注释改动、无功能差异，方法签名与存在性 HEAD/工作树一致），
但行号整体错位（api.go HEAD 1272 行 vs 工作树 1073 行）。本文 §6 的 op-geth 行号已按
pin `d0734fd5…` 重取；评审台账中同源引用需按此规则加注或重取。
**流程动作**：评审/引用前的参照树必须 `git status --short` 干净，否则先记录树状态或改用 `git show`。

## 2. 已确认决策

| # | 决策 | 选择 |
| --- | --- | --- |
| D1 | 交付范围 | 分期完整设计：M1–M7 全部设计到位，实施分四期（P1–P4） |
| D2 | CI 形态 | 三层：PR gate / nightly / weekly |
| D3 | 真实 op-node e2e | 实现归 S7；本体系只交付 conformance 清单与占位判据 |
| D4 | 变异测试 | 自研"修复反转"harness：**41 个可反转种子**（= fixed 的 finding；2 条 wontfix 无修复可反转，免变体） |
| D5 | 总体架构 | **工件中心**（oracle 全部为生成工件，测试只读工件） |

## 3. 方案选型（D5 的依据）

| 方案 | 说明 | 取舍 |
| --- | --- | --- |
| **工件中心（选）** | oracle 均为生成工件（JSON/golden），由 Go dumper + 语料生成器产出；测试只读工件 | CI 快而确定；与现有 `opt8n-ref` 机制零摩擦（判据/仪式现成）；pin 漂移在 regen 时即暴露。oracle 时效绑定再生成节奏；活体对拍缺位（S7 承接） |
| 活体对拍中心 | 测试时并行拉起 op-geth 与被测节点逐请求对拍 | oracle 永远最新、负向覆盖广；但 CI 重且不稳定、三方归因难、与 unity 体系整合差、与 S7 重叠 |
| 双层混合 | 工件中心 + 每周抽样活体对拍 | 兼得；但多一套 op-geth 运行时编排，抽检统计意义有限 |

**工具链要求**：dumper 必须用 **Go 1.24**——两棵 pin 树的 `go.mod` 都声明 `go 1.24`（op-geth 还带 `tool` 块，`GOTOOLCHAIN=local` 会直接解析失败）；本机 go1.23.4 之所以能构建是靠 `GOTOOLCHAIN=auto` 自动下载 1.24.0（需要网络），CI 侧 action 已用 `go-version: '1.24.x'`。离线环境需预置 1.24。

## 4. 架构与数据流

```
pin（语料 op-geth e8800cffe / op-node+op-reth 76e4fad5 / registry zip 9cf0456a）
        │  regen.sh（既有仪式扩展，跑在语料库仓内）
        ▼
语料库仓 op-stack-e2e-tests
  generator/  dump-geth-caps（在 op-geth pin 树内 build）
              dump-opnode   （在 optimism pin 树内 build）
              opt8n-ref 扩展（getPayload golden / receipt / header 基线）
        │
        ▼
  t8n/matrix/  工件（caps.json / engine_api_windows.json 生成物 ignore；manifest.txt / SHA256SUMS / known_deviations.json 入库）
        │  经 symlink opstack-executor/tests/t8n 暴露给 FISCO 仓
        ▼
FISCO 仓 C++ 矩阵测试（只读工件）
   engine/test/unittests/engine/  OpEngineApiMatrixTest（M1/M2）
                                  OpEngineSequenceMatrixTest（M4 枚举 + 随机）
   opstack-executor/tests/ · bcos-rpc  M3/M6 逐字段 diff
        │
        ▼
三层 CI（§7）── gate 阻塞合并；nightly/weekly 开 issue
FISCO 仓并行：tools/mutation/（修复反转 harness，§8）── weekly 全量
```

防漂移闭环：pin 升级 → `regen.sh` 重生成 → 判据 ③ 发现期望变化 → 要么矩阵过时（修测试）
要么实现漂移（修代码）。新 finding 修复合入 → 同步追加变异变体 → weekly 验证检出力。

## 5. 组件与落点

### 5.1 按仓分工（R1）

| 变更 | 仓 | 具体路径 |
| --- | --- | --- |
| `dump-geth-caps`、`dump-opnode`、`opt8n-ref` 扩展 | **op-stack-e2e-tests** | `opstack-executor/tests/t8n/generator/` |
| `matrix/` 工件 + 契约文件 | **op-stack-e2e-tests** | `opstack-executor/tests/t8n/matrix/`。**入库契约**：`manifest.txt`、`SHA256SUMS`（由 `regen.sh` 幂等生成、禁止手改）、`known_deviations.json`（人工维护的已记录偏离）；**生成物（ignore）**：`caps.json`、`engine_api_windows.json` |
| 该仓 `.gitignore` 增量 | **op-stack-e2e-tests** | **只**追加两个生成物：`matrix/caps.json`、`matrix/engine_api_windows.json`（不要用 `matrix/*.json` 通配——`known_deviations.json` 是人工契约、必须入库） |
| C++ 矩阵测试 | **FISCO-BCOS** | `engine/test/unittests/engine/`（新文件，进 unity GLOB）、`opstack-executor/tests/`、`bcos-rpc/test/` |
| 变异 harness | **FISCO-BCOS** | `tools/mutation/`（新建） |
| CI 接线 | **FISCO-BCOS** | 现有 `opstack-t8n-regen` action 已 checkout 语料资产并 re-symlink；只需让新 dumper 纳入该 action 的构建步骤 |

两个仓各自独立 PR；FISCO 侧的矩阵测试在**没有语料资产时 skip**（与现有
`test_real_registry_*` 的 skip 语义一致），不因缺工件而红。

### 5.2 组件

| 组件 | 说明 |
| --- | --- |
| `dump-geth-caps` | **类型反射**，不实例化节点：`reflect.TypeOf((*catalyst.ConsensusAPI)(nil))` 取方法集，复刻 `api.go:1122-1133` 的命名规则（`engine_` + 首字母小写，跳过 `ExchangeCapabilities`），产出 `caps.json`。构造 `ConsensusAPI` 需完整 eth 后端（`api.go` 构造函数读 `eth.BlockChain().Config()`），故**不可**实例化调用 |
| `dump-opnode` | 在 optimism pin 树内 build（op-node 是其 in-tree module），调用 `rollup` 的三个版本窗函数对时间轴扫描，产出 `engine_api_windows.json`。**不**与 op-geth 共用一个 Go module——每个 dumper 各自在对应 pin 树内 build（与 `opt8n-ref` 同机制） |
| `opt8n-ref` 扩展 | 增发 getPayload golden（V2–V5 × fork）、receipt 字段基线、header RLP 字段集基线 |
| 工件 schema | `engine_api_windows.json`：顶层对象 `{"pin":…,"generated_by":"cmd/opnodewin","schedule":"generator/matrix/input_schedule.json","windows":[{"fork":"regolith","timestamp":0,"newPayload":"engine_newPayloadV2","forkchoiceUpdated":"engine_forkchoiceUpdatedV1","getPayload":"engine_getPayloadV2"}, …]}`（扁平字段，值是 op-node `eth.EngineAPIMethod` 的**完整方法名**——已实测；测试按 `engine_<family>V<n>` 渲染本仓 profile 后直接比对）。`caps.json`：`{"pin":…,"generated_by":"cmd/caps","caps":["engine_...","..."]}`。负向格由测试按"时间落在该 fork 窗内但方法版本不符"驱动（M1 Step 4b） |
| M1/M2 测试 | 新 `OpEngineApiMatrixTest.cpp`，读 `engine_api_windows.json` + `caps.json` 驱动叉积 |
| M4 测试 | 新 `OpEngineSequenceMatrixTest.cpp` + `SequenceInvariants.h`（I1–I6 断言库）；随机序列步数 `OPSEQUENCE_MATRIX_STEPS`（默认 0＝PR gate 跳过）、种子 `OPSEQUENCE_MATRIX_SEED`（默认固定值，CI 另注入） |
| M3/M6 测试 | `opstack-executor/tests/`、`bcos-rpc/test/` 现有二进制内新增 diff 用例 |
| 变异 harness | `tools/mutation/`（见 §8） |

真实调度器要求：M2/M4 的一切导入/承诺路径断言必须用**真实 `OpScheduler`**（含带 cache 的
`CacheMLS` 组合，夹具见 `OpEngineKarstTestHarness.h` / `OpEngineImportFcuTest.cpp`）；stub 只允许构造输入。

### 5.3 落点策略与姐妹稿的关系（R9）

- **新建** `OpEngineApiMatrixTest.cpp`、`OpEngineSequenceMatrixTest.cpp`；
  `OpEngineApiVersionsTest.cpp`（16 例）与 `OpEngineImportFcuTest.cpp`（30 例）**保留**，
  作为矩阵的子集/smoke 继续在 PR gate 跑。
- 姐妹稿中「把 16 例扩全矩阵」「M4 落在 `OpEngineImportFcuTest.cpp`」的表述**就落点而言作废**，
  以本文为准；格子语义仍以姐妹稿为准。

## 6. 各矩阵规格（oracle 工件 / 上游源引 / 覆盖）

**行号规则**：op-geth 行号均取自 pin `d0734fd5…` 的 HEAD（`git show`），不放工作树行号（§1.2）。

### M1 fork × 方法版本（PR gate；P1）
- 工件：`engine_api_windows.json`、`caps.json`。
- 上游源引：
  - op-node `rollup/types.go`：`ForkchoiceUpdatedVersion:676`、`NewPayloadVersion:696`、`GetPayloadVersion:708`、`checkFork:415`；`ParseRollupConfig` 的 `DisallowUnknownFields:850`。
  - op-geth `eth/catalyst/api.go`：方法本体 `ForkchoiceUpdatedV1:166 / V2:180 / V3:198 / V4:218`、
    `GetPayloadV1:447 / V2:461 / V3:472 / V4:483 / V5:497`、
    `NewPayloadV2:702 / V3:723 / V4:742 / V5:769`、`ExchangeCapabilities:1122`；
    错误码 `paramsErr("can't use newPayloadV2 post-cancun"):709`、
    `unsupportedForkErr("newPayloadV3 …"):736`、NewPayloadV4 窗口 `:754-759`。
  - `GetPayloadV1` 存在但**不在实现窗口**：本体系把 V1 作为**负向维度格**（应缺席/被拒），
    实现窗口为 V2–V5（姐妹稿 M1 的 "V1..V5" 维度即含此负向格，R13）。
  - `specs/protocol/exec-engine.md` 方法页；karst/canyon/regolith/delta 仅 overview → 相应行 verdict
    标 `op-node+op-geth 互证`，不编造引用。
- 覆盖：S3-F9。

### M2 payload 形状（PR gate；P1/P2）
- 工件：`payload_shape_baseline.json` + getPayload golden。
- 上游源引：op-geth `eth/catalyst/api_optimism.go:12`（`checkOptimismPayload`：Canyon 空 withdrawals
  `:14-17`、Isthmus withdrawalsRoot `:26-32`）与 `:40`（`checkOptimismPayloadAttributes`：Canyon `:46-49`、
  Holocene extraData `:53`）；`api.go:702/723/742` 的 V2/V3/V4 形状分支；
  **op-node `p2p/gossip.go:353-362`**（畸形 payload 的 gossip 层 `ValidationReject`，第三意见）；
  specs `holocene/exec-engine.md:33-52`、`jovian:32-56`、`isthmus:55-132`。
- 覆盖：S3-F7、INT-F1、S4-F6。

### M3 L1 费用/收据（PR gate；P2）
- 工件：`receipt_field_baseline.json` + t8n 语料扩展（overhead 溢出、非 1e6 倍 scalar）。
- 上游源引：op-geth `core/types/rollup_cost.go:292-315`（Bedrock `*big.Int` 计算）、
  `:402-406`（`intToScaledFloat`＝scalar/1e6）；`receipt.go:91-93`（`l1FeeScalar` pre-Ecotone 有、
  Ecotone 后 nil）、`receipt_opstack.go:38-43`、`gen_receipt_json.go:38-40`；
  `consensus/misc/eip1559/eip1559.go:64-107`（`:77` = `IsOptimismHolocene(parent.Time)`）、
  `eip1559_optimism.go:22-47 / 147-210`；specs `fjord/holocene/jovian` 页。
- **op-reth 的引用要拆开写**（R6）：
  - RPC 层的饱和语义**在树内**：`crates/rpc/src/eth/receipt.rs:180-195`（`saturating_to()`）。
  - Bedrock 费用公式本体**不在**被引的 `crates/evm/src/l1.rs`（该文件只 `parse` + 分发
    `self.calculate_tx_l1_cost(...)`，见 `l1.rs:327-338`）；函数定义在**外部 crate**
    （`alloy-op-evm 0.27.0` / `reth-optimism-primitives 1.10.2`，registry 依赖）→
    该行 verdict 记 `cannot-determine（实现不在 pin 树内）`，最小读取集 = 对应 crate 版本源码。
- 覆盖：S4-F1/F3/F4/F5/F6。

### M4 import/FCU 序列（枚举 PR gate / 随机 nightly；P1/P3）
**14 条序列 ↔ finding 守格表**（R7；每条序列后全跑 I1–I6）：

| # | 序列 | 守格 finding |
| --- | --- | --- |
| S1 | newPayload B1..B3 → FCU(B3) 前推 | 基线；**N1**（经不变量 I2 的 by-number 读断言） |
| S2 | 跳到 ledger-canonical head（跳中段） | N6 |
| S3 | A-B-C → import B′ → FCU(B′) **同高切换** | N2、NEW-1 |
| S4 | A-B-C-D → import B′ → FCU(B′)（head 之上**多块**孤儿） | N3 |
| S5 | FCU 回退（head 低于 tip） | N2（裁剪） |
| S6 | 重复导入同 payload | — |
| S7 | 非法 payload → 合法 payload | — |
| S8 | 被拒 FCU 后再 FCU | N6 |
| S9 | canonicalize 中链失败注入（每个 merge 步） | F3 |
| S10 | canonicalize 期间并发 newPayload | F5 |
| S11 | finalized 推进后的 flat 界 | F4（Tier-2 前只断言现有 prune 行为） |
| S12 | BLOCKHASH 跨 payload 链读取 | N4 |
| **S13** | **暖 cache 后同高切换（`CacheMLS`；先前推 B1..B3 使 cache 变热）** | **NEW-3** |
| **S14** | **back-reorg canonicalize：A-B-C → B′@2 → FCU 回 C@3（或回 B@2）** | **NEW-2** |

- oracle：不变量组自洽 + 行为对照 op-geth `api.go:888/899`（`delayPayloadImport`）、
  `:908`（`InsertBlockWithoutSetHead`）、`:352/:366`（`SetFinalized`/`SetSafe`）、`:942`（`delayPayloadImport` 本体）。
- 覆盖：N1/N2/N3、NEW-1/NEW-2/NEW-3、F3/F5/N4/N6。

### M5 genesis/调度（nightly 全链；P2）
- 落点：`tools/opstack-genesis/` pytest 全链参数化（**无 registry zip 时 skip**，沿用现有 skip 语义）。
- 源引：registry zip（`genesis.l2.hash`，COMMIT `9cf0456a…`）；op-geth `core/genesis.go:658-673`
  （`GenesisGasLimit`/`InitialBaseFee` 缺省）、`:711-719`（Isthmus MessagePasser 覆盖）、
  `params/protocol_params.go:31/40/146`；op-reth `crates/chainspec/src/lib.rs:560`
  （golden `0x8ed4baae…`）；op-node `rollup/superchain.go:17` + `types.go:850`。
- **karst 行不引入第四棵树**（R3）：`alloy-op-hardforks@0.4.4` 的 `OpHardfork` **无 Karst**
  （Karst 首见于 0.5.0，据 docs.rs；本地 pin 树内 0.4.7 亦无——全树 `grep -rli karst --include='*.rs'` = 0 命中）。
  故 karst 行维持 `cannot-determine`，最小读取集 = 一个 karst-aware 的 op-node/op-reth pin。
  仅当将来钉入 0.5.0 级 crate 时再升级 verdict。
- 覆盖：S1-F1/F2/F3/F4。

### M6 线格式/RPC（PR gate；P2）
- 工件：`receipt_field_baseline.json` + getPayload golden（与 M2 共用）。
- 覆盖：S4-F4、S3-F7 回归门。

### M7 负向/对抗（随 M1/M2；P1/P2）
- 错误码轴（op-geth `api.go:709/736/754-759`）、首错顺序（S3-F8）、deposits-only、
  stale latestValidHash；EEST 作为执行层格子的既有证据引用，不重复覆盖。

## 7. CI 三层

| 层 | 触发 | 内容 | 预算 | 失败路由 |
| --- | --- | --- | --- | --- |
| PR gate | 每次 push（现有 build job 追加） | 语料 regen（action 扩入两个 dumper）→ M1 矩阵 + M4 枚举 14 条 + M2/M6 形状与回灌格 + M3/M6 diff 用例 + 现有全部套件与 EEST | 增量 <5 分钟 | 阻塞合并 |
| nightly | schedule / dispatch | M4 随机序列（`OPSEQUENCE_MATRIX_STEPS=100000`）；M2/M3 逐字段 diff 全量；M5 registry 全链扫描 | 1–2 小时 | 失败步骤执行 `gh issue create/comment --label test-matrix`，不阻塞 PR |
| weekly | schedule | 变异 harness 全量；conformance 清单状态检查（P4 占位） | 数小时 | 同上 |

现有 target 计数口径：本文引用的一组（`test-bcos-engine` 294、block 142、exec 123、
scheduler 37、ledger 234、rpc 313 等）为 **as-of `4e0d2c893`**（12 笔修复后）；
`INDEX.md` 中较早的一组（279/141/…）是修复前时点，两者不是回归。

## 8. 变异 harness（`tools/mutation/`）

- `variants/<finding-id>.patch`：**41 个**可反转种子（fixed 的 finding）各一个定向反转变体；
  2 条 wontfix（F4/N5）**免变体**并在此注明原因。非整笔 revert——会连锁破坏编译。
  例：NEW-3 = switch 的 `mergeToBackends(stagedDelta)` 换回直写 `m_latestBackend`；
  S2-F1 = 字面 ordinal 表两位对调；S4-F5 = 恢复截断整数除法。
- `variants/mapping.json` schema：
  `[{"variant":"NEW-3","finding":"NEW-3","target":"test-bcos-engine","binary":"engine/test/test-bcos-engine","filter":"OpEngineSequenceMatrixSuite/S13_*","also_green":"<除该 filter 外的显式序列列表>","expect":"only_mapped_red"}…]`
  （`target` = CMake target/二进制名；`filter` = Boost `--run_test` 表达式；`expect` 仅 `red`）。
- `run.sh`：逐变体 apply → 增量 build 对应 target → 跑 `filter` → **断言红** → 还原。
  变体套用后仍全绿 = 矩阵盲区 → 开 issue 补格。
- 预算：每变体 3–8 分钟，41 个全量 ≈ 3–5 小时（weekly）。
- 维护：评审流程收尾清单加入"新 finding 修复合入时同步追加变体"；pin 升级后全量复跑一次。

## 9. 分期与验收判据

### P1 oracle 工件化 + M1 矩阵 + M4 枚举序列
- 两个 dumper 并入 `regen.sh` 仪式（各自在对应 pin 树内 build），三条既有判据全过；
- M1 读工件驱动：每 (fork, 方法) ≥1 正 + 1 负 + 窗口端点各 1；V1 仅作负向格；
- M4 的 **14 条**序列全绿且每步 I1–I6 全过；§6 守格表覆盖的 **10 条 finding** 全部有对应序列（另 F4 由 S11 部分覆盖）；
- 变异 harness 骨架 + NEW-3、N2、N1 三个变体跑通（反转必红）；
- 全部既有 target 不回归（as-of 本期的计数，§7）。

### P2 叉积补全 + 逐字段 diff + 全链扫描
- getPayload golden 覆盖 V2–V5 × 9 forks；receipt/header 字段基线入语料；
- M2 每响应格带回灌断言；V4/V5 回归门（字节不变）；
- M3 溢出 / 非 1e6 倍 cell（费用公式本体的 oracle 标 cannot-determine，见 §6 M3）；M6 基线 diff；
- M5 registry 全链 nightly 跑通；karst 行维持 cannot-determine 并注明最小读取集。

### P3 随机序列 + 变异全量
- 随机序列 `OPSEQUENCE_MATRIX_STEPS=100000` 下 I1–I6 零违例；
- 41 个变体全部被矩阵抓红，盲区清单为空（或补格完成）；
- 变体定义入库 + 流程收尾清单更新。

### P4 conformance 清单（实现归 S7）
- 握手 / 建块 / 导入 / safe·finalized 格子与判据文档化；S7 交付后逐格打勾。

## 10. 风险与开放项

1. **两个 op-geth checkout 的角色**：语料生成器用 `blockchain-impl/op-geth`（pin `e8800cffe…`，
   `regen.sh:13-14,43` 校验，目录实测存在）；评审/引用用 `code/op-geth`（pin `d0734fd5…`，工作树脏，§1.2）。
   **新 dumper 一律以语料库 pin（`e8800cffe…`）为准**（caps 反射也用它）；评审 pin 仅供读源码引线号，
   且必须用 `git show`。是否统一两 pin 由 regen 时按需决定。
2. **`dump-opnode` 的构建**：需在 optimism pin 树内 build（op-node 是其 in-tree module）。
   若其 module 依赖过重导致构建不可行，降级：静态表 + 源码行号注释 + regen 判据 ③ 防漂移。
3. **karst 对照不可得**（§6 M5）：维持 cannot-determine；将来钉入 karst-aware op-node/op-reth（或
   `alloy-op-hardforks@0.5.0` 级 crate）时再升级。
4. **`MemoryStorage` move→copy 的开销**：`4a59505a1` 已改 copy（正确性优先）。矩阵大规模 merge 后
   若性能回归，备选 per-pass 源快照（另议）。
5. **随机序列的随机性**：固定种子 + CI 注入种子（`OPSEQUENCE_MATRIX_SEED`）两部分，保证可复现与覆盖并重。

## 11. 明确不做

- 活体对拍进 PR gate / nightly（S7 承接）；
- OP Mainnet；
- EVM 执行层重复覆盖（EEST 已有 100% 门限）；
- mull 类泛化变异工具（D4）；
- 在 FISCO 仓内新增语料 generator 或 matrix 工件（它们属语料库仓，§5.1）；
- 把 `.agents/reviews/` 台账或本设计入库。

## 12. 与既有文档的关系

- `docs/2026-09-11-opstack-test-matrix-design.md`：M1–M7 格子语义、"证明完毕"判据、finding↔格子映射。
  落点冲突以本文 §5.3 为准。
- `.agents/reviews/INDEX.md`：43 条 finding 台账与 12 笔修复提交（变异种子库）。
  其 op-geth 行号引用需按 §1.2 加注。
