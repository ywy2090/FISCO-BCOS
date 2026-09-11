# 评审过程设计：五段里程碑的独立评审与可追溯

- **日期**：2026-09-10
- **状态**：已确认（范围=五段全部重扫 / 独立性=全部新会话 / 台账=保持全本地 / 新增=强制上游对齐轴）
- **工作树**：`.worktrees/karst-on-5550` @ `feat/karst-on-release-3.18`（分支已推送至 `ywy2090/feat/karst-on-release-3.18`）
- **目标**：让「每段里程碑 → 独立评审扫描 → 发现的问题 → 修复提交 → 复核」形成**闭环且可追溯**的流水线，且每条行为性断言都必须**对齐 OP specs 与 op-geth / op-reth 的上游实现**。
- **不做**：历史重写（不改已发布提交，不 force-push）。
- **产物位置**：台账与评审中间件保持本地（`.agents/reviews/` 已被 `.gitignore:95` 忽略）；本设计文档写在 worktree 的 `docs/`（untracked，与 S2 文档对一致），**不提交到分支**。

---

## 1. 背景与现状

分支为**线性**，五段里程碑区间互不重叠，因此"每段一个评审单元"天然成立：

| 里程碑 | 区间 | 提交数 | 文件 | 模块¹ | +insertions | 评审模式 |
| --- | --- | --- | --- | --- | --- | --- |
| S4 Bedrock→Holocene | `c0045d186`..`51115a169` | 6 | 20 | 8 | 552 | single-pass |
| S5+S6 Import+FCU | `51115a169`..`a961fe249` | 14 | 14 | 5 | 2402 | single-pass |
| S2 codec 放开历史名 | `a961fe249`..`530fc3f1a` | 5 | 7 | 5 | 223 | single-pass |
| S3 Engine 版本门 | `530fc3f1a`..`106da9938` | 12 | 17 | 7 | 1301 | single-pass |
| S1 官方 genesis+rollup | `106da9938`..`cf75010da` | 10 | 7 | 3 | 1003 | single-pass |

模式判定依据 fisco-review 的 full 门槛（**>2000 行且 >5 模块**）：五段均未同时越线，全部走主线程单遍。
¹ 模块数按「改动文件的二级目录前缀」统计（比 skill 的 Module map 更细）；按 skill 的 map（`engine/`、`bcos-rpc/`、
`bcos-framework/`、`opstack-executor/`、`bcos-evm/`、`libinitializer/`）计数更少，**单遍的结论两种口径下都成立**。

**现有评审台账状态（本地）**：

| 段 | 台账目录 | 状态 | 缺口 |
| --- | --- | --- | --- |
| S4 | 无 | — | 从未评审 |
| S5+S6 | `.agents/reviews/s5s6-4a915443a/` | F1–F9 全部 `open` | 修复已落在 `a961fe249`，但台账未收口、无 delta 轮 |
| S2 | 无 | — | 无 fisco-review（仅有 cpp-pro 与两轮 coding-standards） |
| S3 | `.agents/reviews/s3-2aefd8554/` | F1–F6 `fixed@d0823c12b` + `round2-delta.md` | 已收敛 |
| S1 | `.agents/reviews/s1-8a99ee401/` | F1–F4 `fixed@cf75010da` + `round2-delta.md` | 已收敛 |
| （历史） | `.agents/reviews/pr-karst-on-5550-a10a3dddd/` 等 | F1–F12 `open`；另两个空目录 | 属 S1–S6 之前的 Karst/5550 工作，本设计不覆盖 |

按本设计，**五段全部重扫**（含已收敛的 S1/S3），以获得口径统一、独立性一致的基线；重扫时把上一轮台账作为 known-items 输入。

---

## 2. 目标 / 非目标

**目标**

1. 五段各一个**独立评审**：新会话、单遍、带目标闭合（跑遍该段受影响 target）。
2. 每条行为性断言**对齐上游**：OP specs → op-geth → op-reth，报告独立成节，逐条给引用与裁决。
3. **可追溯的闭环**：问题（`milestone`）→ 状态（`status`）→ 修复（`fix_sha` / `origin_milestone`）→ 复核（delta 轮）。
4. 跨段交互由一轮**接缝集成评审**兜住（scope 限定在被 ≥2 段改过的文件）。
5. 不改历史；台账全本地。

**非目标**

- 历史重写 / squash / force-push。
- 把评审台账或本设计文档提交到分支（用户选择保持本地）。
- 在评审会话内改代码（评审与实现分离：评审只出 finding 与建议，修复另起会话）。
- 构建或运行 op-geth / op-reth（只按源码引用；见 §6 边界）。
- 覆盖历史 `pr-karst-on-5550-*` 目录（另议）。

---

## 3. 决策日志

| 决定 | 备选 | 为何 |
| --- | --- | --- |
| 范围 = 五段全部重扫 | 只补三个缺口（S5+S6 收口 / S2 / S4） | 用户选定：要口径统一、独立性一致的基线，不愿 S1/S3 用旧口径 |
| 独立性 = 全部新会话 | 混合（本会话收口 S5+S6 + 新会话扫 S2/S4）；全部本会话 | 用户选定：作者不得兼评审。提示词不得含作者结论（§8） |
| 台账 = 保持全本地 | 改 `.gitignore` 入库 findings/REPORT；只提交索引 | 用户选定：`.agents/reviews/` 已忽略，维持现状 |
| 新增**强制上游对齐轴** | 只作为背景约束（当前设计的做法） | 用户要求：每段必须与 op-geth / op-reth 实现对齐并严格引 OP specs |
| 修复落在分支尾部、归属由台账承担 | 插回各段之后（需 force-push） | 五段均已 push，插回即改写已发布历史；用户排除历史重写 |
| 5 段串行执行 | 并行 | 共用同一 `build/` 与 CPU；并发 ninja/test 会互相竞争（实测过） |
| 每段"单遍"而非 sub-agent 全模式 | full 模式 | skill 实测：该体量下 sub-agent 拆文件切断跨文件对称性、召回更低、单 agent 2.4–5.6M token |
| 集成轮 scope = 接缝文件 | 整支重扫 | 整支会越过 full 门槛（5481 行 / 30 文件）；接缝文件精确覆盖跨段交互面 |

---

## 4. 评审单元与边界

- **单元** = 里程碑 + 固定 `base..head`（§1 表）。
- **台账目录命名**：`<milestone>-<该轮评审时的 head 短写>`——首轮取该段末尾，delta 轮取修复后的 head。既有目录符合该约定（`s1-8a99ee401`、`s3-2aefd8554`、`s5s6-4a915443a`，后者是 S5+S6 修复前的 head）。里程碑 id 固定为小写 `s4` / `s5s6` / `s2` / `s3` / `s1`，使 `glob .agents/reviews/<id>-*` 能按「最新非空」定位上一轮（skill 的跨会话续接约定）。
- 每轮进入时先取该目录的文件锁（skill 的 `scripts/lock.sh`），结束时释放；materialize head 树后必须 `scripts/verify_tree.sh` 校验，任何 mismatch 立即失败（否则报告里的行号全部失效）。

---

## 5. 组件

1. **评审会话 ×5**（新会话，每段一个）
   - 输入：区间 + 设计与计划文档路径 + §6 权威清单（本地路径与 pin）+ §10 环境陷阱 + 上一轮台账（若有，作为 known-items）。
   - 输出：`findings.json`、`known-clean.json`、`tree/`、`REPORT.md`（skill 的六节结构 + §6 的"上游对齐"节）。
2. **台账 schema**（本设计扩展 skill 的字段）
   - skill 原字段：`id`、`severity`、`origin`、`scope`、`title`、`location`、`status`、`fix_sha`；`rejected` 需 `premise`。
   - 本设计新增：`milestone`（问题属于哪段）、`origin_milestone`（修复落在哪段，跨段时与 `milestone` 不同）、`round`（轮次）、`claims[]`（见 §6）。
3. **`INDEX.md`（本地，跨会话续接的唯一入口）**
   - 每段一行：区间 / 当前轮次 / 台账目录 / `fixed|open|rejected` 计数 / 是否收敛 / `fix_sha` 列表 / 对齐计数（`aligned|divergent|cannot-determine`）。
   - 每次 delta 轮结束更新。因台账不入库，INDEX 是唯一的全局状态记录。
- **每段同一时刻只有一个「有效」台账轮**：新轮取代旧轮（例如 S5+S6 的 `s5s6-4a915443a` 会被
  `s5s6-a961fe249` 取代）。取代时在**旧轮** `findings.json` 顶层写 `superseded_by: <新轮目录>`，
  并把旧轮每条 finding 的结论映射进新轮（新轮 id 可与旧轮同名以保持追溯）。INDEX 的台账目录列
  只指向有效轮——否则旧轮的 `open` 会与新轮的 `fixed` 长期并存，`fix_sha` 归属与收口判据都会读错。
4. **修复尾段**：所有修复以"按段分组的提交"追加在分支尾部（`fix(S5+S6): …`、`fix(S3): …`），台账用 `fix_sha` + `origin_milestone` 建立归属。
5. **delta 轮 ×5**：每段修复后，由**该段评审会话**核对每个 `Fixed in <sha>`（对照修复 diff + 前提），并把状态改为 `fixed`（或 `rejected + premise`）。
5. **修复由实现侧完成**：要改代码、加测试、提交，由作者在实现流程里做；评审会话只做核对与裁定——保持「评审—修复」分离。
6. **接缝集成轮 ×1**：由**第 6 个新会话**执行，scope = 被 ≥2 段改过的 10 个文件（§7）；同样只出 finding，不改代码。

---

**技能分工（本轮各轴用哪个 skill）**

统一骨干只用一个技能，是为了让五段的结论**口径可比**（这正是"全部重扫"的目的）；其余轴只在历史缺口处补。

| 轴 | 技能 | 本轮适用范围 |
| --- | --- | --- |
| 正确性 / 范围 / 声明审计 / 上游对齐 / 严重度 / 收敛 | **fisco-review（单遍）** | 五段 + 集成轮，全部 |
| 内存安全与 UB（ASan+UBSan）、性能实测、惯用法 | cpp-pro | **仅 S4**（唯一从未跑过 sanitizer 轴的段）。S2 / S3 / S5+S6 已在各自的 cpp-pro 轮覆盖，不重复 |
| C++ Core Guidelines 逐条 | cpp-coding-standards | 本轮不跑（S2 / S3 已覆盖；其余段无新增头文件/API 的合规面） |
| finding 证据链核实 | fisco-verify-findings | 按需（某条 finding 的证据链有争议时） |
| 作者侧修复与回复 | fisco-respond | 修复尾段与 delta 回复 |
| C/C++ 安全审计（内存破坏 / 竞态 / 类型混淆） | c-review | **不可用**：该 skill 需要 `Workflow` 编排工具，本环境未暴露（已实测） |
| GitHub PR 形态的全量扫描 | fisco-review-pr / review-pr | 本轮不走（按分支区间评审，无 PR 号） |

补充规则：若某段评审暴露出需要 sanitizer 才能复现或定级的怀疑，该段**临时升级**跑一次 cpp-pro 的 sanitizer 轴，并在报告边界里写明"因某 finding 升级"。

## 6. 上游对齐轴（强制）

**权威顺序**：OP specs → op-geth → op-reth。本仓"有意偏离"只能在这三者之后作为依据，且必须核对"文档在、代码仍一致、报告已披露"。op-node 单独保留为 **S3「CL 选方法号」** 的权威（specs 与 op-geth 不描述 op-node 的选号逻辑）。

**必须钉住的 pin（写进每份报告头，否则结论不可复现）**

| 源 | 本地路径 | pin |
| --- | --- | --- |
| OP specs | `/Users/octopus/octo/code/ethereum-optimism-specs` | `564a0ceae302eaf465edc7ff8ab55850624a11a0` |
| op-geth | `/Users/octopus/octo/code/op-geth` | `d0734fd5f44234cde3b0a7c4beb1256fc6feedef` |
| op-node + op-reth | `/Users/octopus/octo/code/optimism`（`op-node/` 与 **in-tree** 的 `op-reth/` 同一 repo、同一 pin） | `76e4fad54244ec6bd07dad07e42c82a16ab5113a` |
| registry 工件（仅 S1） | `superchain-configs.zip` 内 `COMMIT` | `9cf0456abad0d2ee5d4834978aa84e6c4c00e76e`（与 op-geth 源码 pin 不同，需分别记录） |
| **引用树清洁性（纪律）** | 上表各路径 | **引用行号前必须确认工作树干净**（`git status --short`）：本机 `/Users/octopus/octo/code/op-geth` 的 `eth/catalyst/api.go` 与 `miner/*` 有未提交的**注释翻译**（纯注释、无功能差异），导致行号整体错位（api.go HEAD 1272 行 vs 工作树 1073 行，`:183↔:218`、`:644↔:769`、`:584↔:709`）。规则：**行号一律用 `git show <pin>:<path>` 取**；若读了工作树，报告须注明树状态 |
| 线上 spec（补充） | `https://github.com/ethereum-optimism/specs/tree/main/specs/protocol` | 本地页缺失/过旧时按 URL 引用并注明日期 |

**做法**

1. 枚举该段 diff 的**行为性断言**（比较运算、错误码、fork 门控、费用公式、字段集决策、版本窗、stateRoot 构造…）。作者注释与 commit message 是待核实断言，不是证据。
2. 逐条落到上游原文并引用：spec 页 + 小节；op-geth `file:line`；op-reth `crate/file:line`（第二份独立实现）。
3. 每条给裁决：`aligned`（带引用）/ `divergent`（上游行号 + 后果 → 进 finding）/ `cannot-determine`（给出最小读取集）。
4. `divergent` 仅当**精确等于**该段设计决策日志里已记录的"有意偏离"时才不算 finding；否则按 skill 的严重度语义定级（live path 分歧可至 HIGH/BLOCKER；离线工具为 MEDIUM/LOW）。
5. **覆盖度**：对齐节必须列出"断言→引用→裁决"清单。**含行为性改动而无对齐条目的段落视为评审不完整，不是 clean。**
6. spec 页不存在时**不得编引用**。实测：`karst/` 仅有 `overview.md`；`canyon/`、`regolith/`、`delta/` 仅有 `overview.md`（其 EL 语义在顶层 `exec-engine.md` 与 `superchain-upgrades.md`）。此情形以"该档 overview + 上游代码"为准并在报告说明。
7. **边界**：op-geth / op-reth **只按源码引用，不构建不运行**（op-reth 为 Rust workspace，构建成本不合算）。若某断言只有执行才能判定，记 `cannot-determine` 并给出最小实验方案。

**逐段上游焦点（文件存在性已按本地 pin 核实）**

| 段 | specs/protocol | op-geth | op-reth |
| --- | --- | --- | --- |
| S4 | `exec-engine.md`（Bedrock L1 fee slot 1/5/6）、`ecotone/l1-attributes.md`、`fjord/exec-engine.md`（FastLZ）、`granite/exec-engine.md`、`holocene/exec-engine.md`（9B extraData 与 1559 参数） | `consensus/misc/eip1559/{eip1559.go,eip1559_optimism.go}`、`params/config.go`（denominator/elasticity） | `crates/evm/src/l1.rs`（L1 fee 公式）、`crates/evm/src/config.rs`、`crates/consensus/src/lib.rs` |
| S5+S6 | `exec-engine.md`（newPayload/FCU 语义）、`derivation.md`（canonical/safe/finalized） | `eth/catalyst/api.go`（`ForkchoiceUpdatedVn`/`NewPayloadVn` 分支）、`api_optimism.go` | `crates/consensus/src/validation/mod.rs`（校验）、`crates/payload/src/{builder,payload}.rs`（造块）、`crates/node/src/engine.rs` |
| S2 | `superchain-upgrades.md`（fork 列表与顺序） | `params/config_op.go`（顺序、无 DeltaTime）、`params/config.go:510-521` | `crates/hardforks/src/lib.rs`（fork 枚举）、`crates/chainspec/src/{op,basefee}.rs` |
| S3 | `exec-engine.md` + 各档 `exec-engine.md`、`superchain-upgrades.md` | 同上 + op-node `rollup/types.go`（三函数） | `crates/node/src/engine.rs`、`crates/rpc/src/engine.rs`、`crates/payload/src/{builder,payload}.rs`、`crates/consensus/src/validation/mod.rs` |
| S1 | `superchain-config.md`、`superchain-upgrades.md` | `superchain/chain.go`（embed 与 COMMIT）、`params/superchain.go` | `crates/chainspec/src/{op,op_sepolia,basefee}.rs`（genesis/rollup 映射） |

---

## 7. 数据流

```
区间 + 文档 + §6权威(pin) + §10陷阱 + 上一轮台账(known-items)
  → 评审会话(单遍, 含对齐轴) → 台账(open) + REPORT
  → 修复尾段(按段分组提交)  → delta 轮(核 Fixed in <sha>)  → 台账(fixed) + INDEX 更新
  → 接缝集成轮(10 文件)     → 集成台账
  → 收口: INDEX 显示 5/5 收敛 + 集成轮闭合
```

**接缝文件（集成轮 scope，10 个；被 ≥2 段改过）**

| 文件 | 涉及段 |
| --- | --- |
| `bcos-evm/bcos-evm/opstack/OpForkSchedule.h` / `.cpp` / `bcos-evm/test/opstack/OpForkScheduleTest.cpp` | S2 × S4 |
| `bcos-tool/test/unittests/libtool/NodeConfigOpForkScheduleTest.cpp` | S1 × S2 |
| `engine/bcos-engine/OpEngineService.h` / `.inl` | S3 × S5+S6 |
| `engine/test/unittests/engine/OpEngineImportFcuTest.cpp` / `OpEngineServiceParityTest.cpp` / `support/OpEngineKarstTestHarness.h` | S3 × S5+S6 |
| `opstack-executor/OpSchedulerSeam.h` | S3 × S4 |

---

**执行顺序（推荐）**

1. 评审（串行，按风险降序）：**S5+S6 → S3 → S4 → S2 → S1**。
   - S5+S6 优先：体量最大（2402 行）且台账陈旧（F1–F9 `open`，而修复已在 `a961fe249`，需在这轮核实并收口）。
   - S3 次之：Engine 版本门是 live path 上后果最重的一段。
   - S2 / S1 最后：改动面小，S1 还是离线工具。
2. 集成轮：五段收敛后，对 §7 的 10 个接缝文件跑一轮。
3. 修复尾段：按里程碑顺序分组（`fix(S5+S6): …`、`fix(S3): …`、…）追加在分支尾部，使尾段可读作本轮评审自身的顺序。
4. delta 轮 ×5：每段修复后立即核对该段，收口台账并更新 `INDEX.md`。

## 8. 独立性机制

- **评审会话只接收**：区间与 SHA、设计与计划文档路径、§6 权威清单（含 pin）、§10 环境陷阱、上一轮台账（作为 known-items）。
- **评审会话不得接收**：作者的批次报告、commit message 里的结论、任何 finding 猜测或"已知问题"提示（fisco-review 的 delegation 契约明确禁止答案注入——它会污染召回，把评审退化为确认）。
- **评审会话不得改代码**：只产出 finding 与建议；修复另起会话（保持"评审—修复"分离，利于 delta 复核）。
- 会话开头必须声明"我是评审者、不是作者"，并在报告头写明所用 pin。

---

## 9. 修复提交归属（对"修复能否紧跟任务提交之后"的回答）

规则：

> 修复能紧跟在"产生它的那段"之后 —— **仅当该段尚未 push**。已 push 的段，后来发现的跨段修复只能落在当前段。

- 五段**均已 push**，故本轮扫描产生的新修复**不可能**插回各段之后（除非 force-push 改写已发布历史，已排除）→ **归属由台账承担**：`milestone`（问题属于哪段）+ `origin_milestone`（修复落在尾段哪个分组）+ `fix_sha`。
- **前五段的历史事实**：S5+S6 的修复是 `a961fe249`（段末）、S2 是 `ec234f5ac`+`530fc3f1a`（段末）、S3 是 `d0823c12b`+`106da9938`（段末）、S1 是 `cf75010da`（段末）——即"段内任务提交 → 段末修复提交"本就是既有形状。
- **已知跨段例外**：`a961fe249` 里含 `opstack-executor/tests/OpL1BlockDepositTest.cpp`，属 **S4 领域**的 UBSan 修复（`putBe` 64 位移位），因 S4 已 push 而落在 S5+S6 段。此类必须在台账标 `milestone: S4` / `origin_milestone: S5+S6`。
- **未来段约定**：新段在 push 之前完成该段评审与修复；需要跨段修复时同样用 `origin_milestone` 标注。
- **BLOCKER/HIGH 落在已推送代码上**：本轮五段都已 push（可能正在被他人评审或待合并），因此这类 finding 必须
  1) 在 `INDEX.md` 与报告里**单列**为「影响已推送代码」；2) 其修复**不按段序排队**，作为下一笔提交优先落地；
  3) 报告中明确提示作者该分支在修复前不宜继续合并。**
- **真实但未记录的偏离**：若某分歧正确、但该段设计决策日志里没有记录，处置是「补记录 + `wontfix`」——
  由用户/作者把该偏离写入**主仓 docs/ 的对应设计文档**（评审侧不改主仓），评审只在报告里核验「文档已存在」。

---

## 10. 环境陷阱清单（随提示词交付给每个会话）

| 项 | 事实 |
| --- | --- |
| Boost suite 名 ≠ 文件名 | `OpEngineKarstProfileTest.cpp` 内是 `OpEngineKarstProfileSuite`；写错 suite 名 → 空跑 exit 0（假绿） |
| ASan 构建目录 GLOB 陈旧 | `build-asan` 不会自动收新测试文件，须先 `cmake` 重配；否则新 suite 在 sanitizer 下完全没跑（表现为 rc=200 且用例数比正常构建少） |
| python | 本机 `python3`(3.14) 无 pytest，用 `python3.11 -m pytest`；S1 的 zip 解压需 `zstd -d -D dictionary` |
| clang-tidy | homebrew LLVM 的 libc++ 缺 `char_traits<unsigned char>`，对用到 `bcos::bytes` 的头文件只能部分解析 → 只做新旧差分，不单边下结论 |
| t8n 语料 | `opstack-executor/tests/t8n` 是 symlink → `~/.cache/fisco-t8n-corpus`；CI 会 checkout `op-stack-e2e-tests` 再重新 symlink，本地裸检出会悬空 |
| 工具 | `rg -r` 是 replace（会毁输出）；macOS 无 `timeout`；`git commit` 的 clang-format 钩子可能先拒一次（重提或 `--amend`）；两个 test 进程并发会互相拖慢 |
| 串行要求 | 5 段评审串行（共用 `build/` 与 CPU） |

**每段受影响 target（目标闭合用）**

| 段 | 必跑 target |
| --- | --- |
| S4 | `opstack-executor-block-tests`、`opstack-executor-tests`、`opstack-executor-receipt-tests`、`bcos-evm-opstack-tests` |
| S5+S6 | `test-bcos-engine`（含 ASan 侧，先重配 cmake）、`opstack-executor-block-tests`、`opstack-executor-tests` |
| S2 | `bcos-evm-opstack-tests`、`test-bcos-tool`、`test-bcos-ledger` |
| S3 | `test-bcos-engine`(+ASan)、`opstack-executor-block-tests`、`test-bcos-tars-protocol`、`bcos-evm-opstack-tests` |
| S1 | `test-bcos-ledger --run_test=GenesisEthHeaderTest`、`test-bcos-tool`（`NodeConfigEthGenesisHeaderTest` / `NodeConfigOpForkScheduleTest`）、`python3.11 -m pytest tools/opstack-genesis/*`、real-zip 实跑两链 |

---

## 11. 验收与收敛判据

**每段通过条件**：受影响 target 全跑绿 **且** §6 对齐节覆盖该段全部行为性断言。

**收敛（每段）**：所有 finding 为 `fixed@<sha>` 或 `rejected + premise`（前提可核）∧ 无新 finding ∧ 无未披露行为变更。**轮次上限 3**；出现新 HIGH/BLOCKER 或未披露行为变更即重置收敛。

**整体完成**：五段全部收敛 + 接缝集成轮闭合 + `INDEX.md` 全绿 + 每个 finding 都有 `fix_sha` 或已验证前提。

---

## 12. 风险

| 风险 | 处置 |
| --- | --- |
| 台账全本地 → 换机器/新 clone 丢失 | 接受（用户选择）；`INDEX.md` 本地存在是跨会话续接的必需条件；若日后要共享，最小代价是只提交 INDEX |
| 五段均已 push → 修复只能成尾段，无法与任务提交相邻 | 由台账 `milestone`/`origin_milestone`/`fix_sha` 承担归属（§9） |
| 上游 pin 漂移（specs/op-geth 更新） | 报告头固定 pin；换 pin 需重跑对齐节 |
| spec 页缺失（karst/canyon/regolith 等） | 以 overview + 上游代码为准并明确说明，禁止编引用（§6.6） |
| op-geth/op-reth 只读未执行 → 少数断言无法判定 | 记 `cannot-determine` + 最小实验方案（§6.7） |
| 评审会话被迫"确认"作者结论 | 提示词白名单（§8）；报告中每条 finding 必须带原码引用与可达场景 |
| 5 段串行耗时 | 单遍成本为"几万 token/几分钟"级别；集成轮最便宜 |

---

## 13. 明确不做

- 历史重写 / squash / force-push。
- 把台账或本设计文档提交到分支。
- 评审会话内改代码。
- 构建或运行 op-geth / op-reth。
- 覆盖历史 `pr-karst-on-5550-*` 目录。
- 全模式（sub-agent）评审。

---

## 14. 评审会话提示词模板（每段一份）

```
你是【评审者】，不是作者。本会话只出 finding 与建议，不得修改任何代码。

评审单元：<milestone>，区间 <base>..<head>（说明：两者之间的提交恰好是本段工作）
工作树：/Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550
方法与格式：按 /Users/octopus/.agents/skills/fisco-review/SKILL.md 执行（单遍模式；
  报告含 Summary / Findings / Scorecard / What this PR is doing /
  Checked-not-a-problem / Verification boundary，外加"Upstream alignment"一节）。

必读（先读再动手）：
  - 设计：<design path>
  - 计划：<plan path>
  - 权威（本地，报告头必须记录 pin）：
      specs   /Users/octopus/octo/code/ethereum-optimism-specs        @ 564a0ce
      op-geth /Users/octopus/octo/code/op-geth                        @ d0734fd5
      op-node /Users/octopus/octo/code/optimism/op-node               @ 76e4fad5
      op-reth /Users/octopus/octo/code/optimism/op-reth               （与 op-node 同 repo 同 pin，见上）
  - 该段上游焦点：<§6 表中该段一行>
  - 上一轮台账（若有，作为 known-items，不再重复报；但每条都要在新 head 上核 premise）：
      <ledger path 或 "无">

对齐轴（强制）：枚举本段 diff 的每条行为性断言，逐条给
  spec 页+小节 / op-geth file:line / op-reth crate:file:line 与裁决
  （aligned | divergent | cannot-determine）；divergent 进 finding，
  除非它精确等于设计决策日志里已记录的"有意偏离"（需核对文档在、代码一致、已披露）。
  含行为性改动而无对齐条目 = 评审不完整。spec 页缺失时说明，不得编引用。

附加轴：<仅 S4> 另跑一轮 cpp-pro 的内存安全/UB 轴（ASan+UBSan，注意 build-asan 需先 cmake 重配）。
       <其余段> 无附加轴；若评审中确需 sanitizer 复现，临时升级并在报告边界注明原因。

目标闭合：编译并运行 <§10 表中该段 target 列表>，逐个报结果与用例数。

环境陷阱（已知，勿重复踩）：<§10 表>

输出：台账（findings.json / known-clean.json，字段含 milestone/round/claims[]
  /fix_sha/origin_milestone）+ REPORT.md，写入 .agents/reviews/<milestone>-<head短写>/，
  并先取 scripts/lock.sh 的锁、materialize 后跑 scripts/verify_tree.sh 校验。
```
