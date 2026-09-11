# 五段里程碑独立评审与可追溯 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 对 S1–S6 五段里程碑各跑一轮独立（新会话、单遍）的 fisco-review，强制与 OP specs / op-geth / op-reth 对齐，并把「问题 → 修复 → 复核」用本地台账与 `INDEX.md` 串成可追溯闭环。

**Architecture:** 每段一个评审会话（只读、只出 finding）+ 一轮接缝集成评审（10 个跨段文件）+ 一个修复尾段（按段分组提交）+ 每段一轮 delta 复核。台账与提示词全部本地（`.agents/reviews/`，已被 `.gitignore:95` 忽略），**不提交代码以外的任何东西、不改写历史**。

**Tech Stack:** fisco-review v1.20.1（单遍模式 + Upstream alignment 扩节）、cpp-pro（仅 S4 的 sanitizer 轴）、本地对照树（specs / op-geth / op-node / op-reth）、CMake+Ninja、python3.11（pytest 与 JSON 校验）。

**Spec:** `docs/2026-09-10-review-process-design.md`（本 worktree 内，untracked）

## Global Constraints

- 唯一工作树：`/Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550` @ `feat/karst-on-release-3.18`。**禁止**改主仓脏工作区。
- **不改写历史**：五段均已 push（分支 tip 现为 `cf75010da`）。修复只能追加在尾部。
- **评审会话不得改代码**：只出 finding 与建议（设计 §8）。修复由实现侧在 Task 7 做。
- **提示词不得包含作者结论**：不得出现批次报告、commit message 结论、"已知问题"提示（fisco-review 的 delegation 契约禁止答案注入）。
- **串行执行**：5 段评审串行（共用同一 `build/` 与 CPU；并发 ninja/test 会互相竞争）。
- **本计划不产生代码提交**（唯一例外是 Task 7 的修复尾段）；每段任务的"提交"动作 = 写台账 + 更新 `INDEX.md`（均本地）。
- 报告与台账语言：报告正文用英文（fisco-review 的历史口径），生命周期标记与 `INDEX.md` 用中文。
- 所有对照树**只读引用，不构建**（op-reth 是 Rust workspace）；无法仅靠阅读判定的断言记 `cannot-determine` 并给最小实验方案。
- **每段只有一个有效台账轮**：新轮取代旧轮时，在旧轮 `findings.json` 顶层写 `superseded_by`，把旧轮每条
  finding 的结论映射进新轮，INDEX 只指向有效轮（否则旧轮的 open 与新轮的 fixed 并存，`fix_sha` 归属会读错）。
- 各轴用哪个技能见设计 §5 的「技能分工」表：统一骨干 = fisco-review 单遍；**cpp-pro 的 sanitizer 轴只补 S4**；
  `c-review` 在本环境**不可用**（需要 `Workflow` 编排工具，未暴露），故任何「内存破坏/竞态」类的怀疑都在报告里记为
  `cannot-determine` + 最小实验方案，而不是声称已由安全审计覆盖。

**固定 pin（写进每份报告头）**

| 源 | 路径 | pin |
| --- | --- | --- |
| OP specs | `/Users/octopus/octo/code/ethereum-optimism-specs` | `564a0ceae302eaf465edc7ff8ab55850624a11a0` |
| op-geth | `/Users/octopus/octo/code/op-geth` | `d0734fd5f44234cde3b0a7c4beb1256fc6feedef` |
| op-node | `/Users/octopus/octo/code/optimism/op-node` | `76e4fad54244ec6bd07dad07e42c82a16ab5113a` |
| op-reth | `/Users/octopus/octo/code/optimism/op-reth`（**in-tree** 目录，无独立 HEAD） | 由上一行的同一 commit 固定 |
| registry 工件（S1） | `superchain-configs.zip` 内 `COMMIT` | `9cf0456abad0d2ee5d4834978aa84e6c4c00e76e` |

**文件结构**

| 路径 | 动作 | 责任 |
| --- | --- | --- |
| `.agents/reviews/prompts/_preamble.md` | 建 | 五段共用的提示词前缀（任务定义、方法、权威表、对齐轴、目标闭合、输出格式、禁用词） |
| `.agents/reviews/prompts/s5s6.md`、`s3.md`、`s4.md`、`s2.md`、`s1.md`、`integration.md` | 建 | 各段提示词 = 前缀 + 该段块（值已填好） |
| `.agents/reviews/INDEX.md` | 建 | 全局状态：每段区间/轮次/台账目录/状态计数/收敛/`fix_sha`/对齐计数 |
| `.agents/reviews/<seg>-<head>/` | 评审会话产出 | `findings.json`、`known-clean.json`、`tree/`、`REPORT.md`（会话内创建） |
| 代码（Task 7） | 修 | 修复尾段提交（按段分组） |

---

### Task 0: 准备（pin 记录 + 提示词前缀 + INDEX 骨架）

**Files:**
- Create: `.agents/reviews/prompts/_preamble.md`
- Create: `.agents/reviews/INDEX.md`

- [ ] **Step 1: 记录 pin（把当前值写进 INDEX 头部）**

Run:
```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550
for r in /Users/octopus/octo/code/ethereum-optimism-specs /Users/octopus/octo/code/op-geth \
         /Users/octopus/octo/code/optimism/op-node /Users/octopus/octo/code/optimism/op-reth; do
  printf "%s %s\n" "$(basename $r)" "$(git -C $r rev-parse HEAD)"
done
git rev-parse HEAD
```
Expected: specs `564a0ce…`、op-geth `d0734fd5…`、op-node `76e4fad5…`、op-reth 的 HEAD、分支 head `cf75010da…`。与上表不符则先停下核对（pin 漂移会让对齐结论失效）。

- [ ] **Step 2: 写提示词前缀**

Create `.agents/reviews/prompts/_preamble.md`：
```markdown
你是【评审者】，不是作者。本会话只出 finding 与建议，**不得修改任何代码**。

## 方法与格式
按 /Users/octopus/.agents/skills/fisco-review/SKILL.md 执行，**单遍模式**（主线程通读全 diff，
不派 sub-agent）。报告含六节：Summary / Findings / Scorecard / What this PR is doing /
Checked and not a problem / Verification boundary，**外加一节 Upstream alignment**。
报告正文用英文；生命周期标记用中文。报告头必须记录：被评审区间与 head、以及下面四个对照树的 pin。

## 独立性与输入白名单
你只能接收：本段区间与 head、设计与计划文档路径、对照树路径与 pin、环境陷阱清单、
上一轮台账（若给出，作为 known-items）。
不得要求、也不得使用：作者的批次报告、commit message 里的结论、任何"已知问题"提示。
作者注释与 commit message 都是**待核实的断言**，不是证据。

## 评审纪律
- 每条 finding 必须带 ≤6 行原码引用（来自 materialized tree 的 file:line）、可达场景、可执行的修法；
  没有证据不写 finding。
- 每个目标都要"目标闭合"：编译并运行下面列出的全部 target，逐个报结果与用例数；
  只报"某个 suite 绿"不算。
- 先取锁、materialize、校验树：
  dir="<本段块里给出的「会话产出」目录，例如 s5s6-a961fe249>"
  bash /Users/octopus/.agents/skills/fisco-review/scripts/lock.sh acquire "$dir"
  mkdir -p "$dir/tree" && git archive <head> <该段涉及的目录> | tar -x -C "$dir/tree"
  bash /Users/octopus/.agents/skills/fisco-review/scripts/verify_tree.sh <head> "$dir/tree"
  任何 mismatch 立即停下（否则报告里的行号全部失效）。结束时 release 锁。
- 严重度用 skill 的语义（BLOCKER/HIGH/MEDIUM/LOW）；`rejected` 必须给可核实的 `premise`。

## 上游对齐轴（强制，报告独立成节）
枚举本段 diff 的**每条行为性断言**（比较运算、错误码、fork 门控、费用公式、字段集决策、
版本窗、stateRoot 构造…），逐条给出：
  - spec：本地 specs 页 + 小节（如 specs/protocol/exec-engine.md §…）；
  - op-geth：file:line；
  - op-reth：crate/file:line（第二份独立实现）；
  - 裁决：aligned | divergent | cannot-determine。
`divergent` 进 finding，**除非**它精确等于该段设计决策日志里已记录的"有意偏离"
（此时仍需核对：文档在、代码一致、报告已披露）。`cannot-determine` 必须给最小读取集。
含行为性改动而无对齐条目 = **评审不完整**，不是 clean。
spec 页不存在时如实说明（实测 karst/、canyon/、regolith/、delta/ 仅有 overview.md），
**不得编造引用**。对照树只按源码引用，不构建不运行。

## 输出
写入 `.agents/reviews/<seg>-<head 短写>/`：
- `findings.json`：数组，元素字段 = skill 原字段（id/severity/origin/scope/title/location/
  status/fix_sha；rejected 另加 premise）+ 本设计新增（milestone/origin_milestone/round/claims[]）；
- `known-clean.json`：已核实无问题项（每条附引用原文）；
- `REPORT.md`：六节 + Upstream alignment。
完成后打印：`review <seg> 结束`。

## 环境陷阱（已知，勿重复踩）
- Boost suite 名 ≠ 文件名：`OpEngineKarstProfileTest.cpp` 内是 `OpEngineKarstProfileSuite`；
  写错 suite 名 → 空跑 exit 0（假绿）。
- `build-asan` 的 configure 时 GLOB 陈旧：新增测试文件必须先 `cmake` 重配，否则
  sanitizer 下该 suite 完全没跑（表现为 rc=200 且用例数比正常构建少）。
- python：本机 `python3`(3.14) 无 pytest，用 `python3.11 -m pytest`；S1 的 zip 解压需
  `zstd -d -D dictionary`。
- clang-tidy：homebrew LLVM 的 libc++ 缺 `char_traits<unsigned char>`，对用到 `bcos::bytes`
  的头文件只能部分解析 → 只做新旧差分，不单边下结论。
- `opstack-executor/tests/t8n` 是指向 `~/.cache/fisco-t8n-corpus` 的 symlink；CI 会 checkout
  `op-stack-e2e-tests` 再重新 symlink，本地裸检出会悬空。
- 工具：`rg -r` 是 replace（会毁输出）；macOS 无 `timeout`；`git commit` 的 clang-format
  钩子可能先拒一次；两个 test 进程并发会互相拖慢。
```
Expected: 文件写出，内容与上面逐字一致。

- [ ] **Step 3: 写 INDEX 骨架**

Create `.agents/reviews/INDEX.md`：
```markdown
# 里程碑评审状态（本地，不入库）

pins：specs __SPECS__ / op-geth __GETH__ / op-node __NODE__ / op-reth __RETH__

| 段 | 区间 | 轮次 | 台账目录 | fixed | open | rejected | 对齐 aligned/divergent | 收敛 | fix_sha |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| S5+S6 | 51115a169..a961fe249 | | | | | | | 否 | |
| S3 | 530fc3f1a..106da9938 | | | | | | | 否 | |
| S4 | c0045d186..51115a169 | | | | | | | 否 | |
| S2 | a961fe249..530fc3f1a | | | | | | | 否 | |
| S1 | 106da9938..cf75010da | | | | | | | 否 | |
| 集成轮 | 接缝 10 文件 | | | | | | | 否 | |
```
（`__SPECS__` 等占位按 Step 1 的实测值填写。）

- [ ] **Step 4: 校验锁脚本可用**

Run:
```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550
bash /Users/octopus/.agents/skills/fisco-review/scripts/lock.sh acquire /tmp/lock-probe && \
bash /Users/octopus/.agents/skills/fisco-review/scripts/lock.sh release /tmp/lock-probe && echo LOCK-OK
```
Expected: `LOCK-OK`。

---

### Task 1: S5+S6 独立评审（`51115a169..a961fe249`，14 提交 / +2402）

**Files:**
- Create: `.agents/reviews/prompts/s5s6.md`
- 会话产出：`.agents/reviews/s5s6-a961fe249/`

- [ ] **Step 1: 写该段提示词**

Create `.agents/reviews/prompts/s5s6.md` = `_preamble.md` 全文 + 下面这段（值已填好）：
```markdown
## 本段
milestone: s5s6      区间: 51115a169..a961fe249（head a961fe249，14 提交，含段末修复 a961fe249）
设计与计划（先读）:
  /Users/octopus/octo/code/FISCO-BCOS/docs/2026-09-09-s5-s6-engine-import-fcu-design.md
  /Users/octopus/octo/code/FISCO-BCOS/docs/plans/2026-09-09-s5-s6-engine-import-fcu.md
上一轮台账（known-items；每条都要在新 head 上核 premise，不再重复报）:
  .agents/reviews/s5s6-4a915443a/findings.json（原始 JSON；其记录的状态**不要沿用**，逐条在 head 上核实）
materialize 目录: engine/bcos-engine engine/test/unittests/engine bcos-framework/bcos-framework/dispatcher opstack-executor
上游焦点:
  - specs: exec-engine.md（newPayload/FCU 语义）、derivation.md（canonical/safe/finalized）
  - op-geth: eth/catalyst/api.go（ForkchoiceUpdatedVn / NewPayloadVn 分支）、api_optimism.go
  - op-reth: crates/consensus/src/validation/mod.rs（校验）、crates/payload/src/{builder,payload}.rs（造块）、
    crates/node/src/engine.rs（engine api）
另需核对的仓内契约（checklist #6 原语契约）: MultiLayerStorage.h 的 deque/merge 顺序警告注释
  与每个新调用点是否相符。
目标闭合（逐个跑并报用例数）:
  ninja -C build test-bcos-engine opstack-executor-block-tests opstack-executor-tests
  build/engine/test/test-bcos-engine
  build/opstack-executor/tests/opstack-executor-block-tests
  build/opstack-executor/tests/opstack-executor-tests
  ASan 侧：ninja -C build-asan test-bcos-engine opstack-executor-block-tests（先 cmake 重配）
```

- [ ] **Step 2: 校验提示词不含作者结论**

Run:
```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550
grep -nEi "已修复|已确认|据称|F[0-9]+ (是|为)|批次报告里|我的结论|known issue|conclusion" \
  .agents/reviews/prompts/s5s6.md || echo PROMPT-CLEAN
# 规则：提示词里除「路径」外不得出现上一轮 finding 的标题或结论文本（grep 只是启发式，词表不全时人工复核）
```
Expected: `PROMPT-CLEAN`（出现任何命中则删掉该句再继续）。

- [ ] **Step 3: 交付给新会话并等其完成**

把 `_preamble.md` + `s5s6.md` 一起交给一个**新会话**（不是本会话）。若你（用户）自己开新会话，把两个文件内容原样粘贴即可；该会话按 fisco-review 单遍执行、只出 finding。

- [ ] **Step 4: 收集并校验产物**

Run:
```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550
ls .agents/reviews/s5s6-a961fe249/
python3.11 - <<'PY'
import json
f=json.load(open('.agents/reviews/s5s6-a961fe249/findings.json'))
need={'id','severity','origin','scope','title','location','status','milestone','round','claims'}
for i in f:
    missing=need-set(i)
    assert not missing, (i.get('id'), missing)
    assert i['status'] in {'open','fixed','partial','rejected','wontfix'}, i
    if i['status']=='rejected': assert i.get('premise'), i['id']
    for c in i['claims']:
        assert set(c) == {'claim','spec','geth','reth','verdict'}, (i['id'], c)
        assert c['verdict'] in {'aligned','divergent','cannot-determine'}, (i['id'], c)
print('findings:', len(f), '| claims checked | ok')
PY
bash /Users/octopus/.agents/skills/fisco-review/scripts/verify_tree.sh a961fe249 .agents/reviews/s5s6-a961fe249/tree
```
Expected: schema OK + tree `OK: N files match`。

- [ ] **Step 5: 更新 INDEX**

在 `.agents/reviews/INDEX.md` 的 S5+S6 行填入：轮次 1、台账目录、状态计数（用上一步的 `findings.json` 统计）、对齐计数（从 `claims[]` 统计）、收敛=否（待 delta）。

---

### Task 2: S3 独立评审（`530fc3f1a..106da9938`，12 提交 / +1301）

**Files:**
- Create: `.agents/reviews/prompts/s3.md`
- 会话产出：`.agents/reviews/s3-106da9938/`

- [ ] **Step 1: 写该段提示词**

Create `.agents/reviews/prompts/s3.md` = `_preamble.md` + ：
```markdown
## 本段
milestone: s3      区间: 530fc3f1a..106da9938（head 106da9938，12 提交，含段末修复 d0823c12b / 106da9938）
设计与计划:
  /Users/octopus/octo/code/FISCO-BCOS/docs/2026-09-09-s3-engine-api-versions-design.md
  /Users/octopus/octo/code/FISCO-BCOS/docs/plans/2026-09-10-s3-engine-api-versions.md
上一轮台账（known-items；核对 premise，不重复报）:
  .agents/reviews/s3-2aefd8554/findings.json（F1–F6 fixed@d0823c12b）
materialize 目录: engine/bcos-engine engine/test/unittests/engine bcos-framework/bcos-framework/engine bcos-tars-protocol/test opstack-executor
上游焦点:
  - specs: exec-engine.md + 各档 exec-engine.md（fjord/granite/holocene/isthmus/jovian）、
    superchain-upgrades.md；注意 karst/ 只有 overview.md（如实说明，不得编引用）
  - op-geth: eth/catalyst/api.go（方法号×时间窗、caps）、eip1559/eip1559_optimism.go（extraData/两套时钟）
  - op-node: rollup/types.go 的 NewPayloadVersion / GetPayloadVersion / ForkchoiceUpdatedVersion
    （**CL 选方法号的唯一权威**）
  - op-reth: crates/node/src/engine.rs、crates/rpc/src/engine.rs、crates/payload/src/{builder,payload}.rs、
    crates/consensus/src/validation/mod.rs
另需核对: 设计里已记录的"有意偏离"（方法号×时间不符一律 -38005）是否仍然只有这一处。
目标闭合:
  ninja -C build test-bcos-engine opstack-executor-block-tests test-bcos-tars-protocol bcos-evm-opstack-tests
  build/engine/test/test-bcos-engine（284 例）
  build/opstack-executor/tests/opstack-executor-block-tests
  build/bcos-tars-protocol/test/test-bcos-tars-protocol
  build/bcos-evm/test/bcos-evm-opstack-tests
  ASan 侧：ninja -C build-asan test-bcos-engine（注意重配）
```

- [ ] **Step 2: 校验提示词**（同 Task 1 Step 2 的命令，路径换成 `s3.md`）→ Expected `PROMPT-CLEAN`
- [ ] **Step 3: 交付新会话并等完成**
- [ ] **Step 4: 收集并校验产物**（同 Task 1 Step 4，目录换 `s3-106da9938`、head 换 `106da9938`）
- [ ] **Step 5: 更新 INDEX 的 S3 行**

---

### Task 3: S4 独立评审（`c0045d186..51115a169`，6 提交 / +552）+ cpp-pro sanitizer 轴

**Files:**
- Create: `.agents/reviews/prompts/s4.md`
- 会话产出：`.agents/reviews/s4-51115a169/`

- [ ] **Step 1: 写该段提示词**

Create `.agents/reviews/prompts/s4.md` = `_preamble.md` + ：
```markdown
## 本段
milestone: s4      区间: c0045d186..51115a169（head 51115a169，6 提交）
设计与计划:
  /Users/octopus/octo/code/FISCO-BCOS/docs/2026-09-09-s4-bedrock-holocene-design.md
  /Users/octopus/octo/code/FISCO-BCOS/docs/plans/2026-09-09-s4-bedrock-holocene.md
上一轮台账: 无（本段从未评审；这是首轮）
materialize 目录: bcos-evm bcos-framework/bcos-framework/engine bcos-framework/bcos-framework/ledger opstack-executor opstack-executor/tests
上游焦点:
  - specs: exec-engine.md（Bedrock L1 fee slot 1/5/6 与 1559 参数）、ecotone/l1-attributes.md、
    fjord/exec-engine.md（FastLZ 成本函数）、granite/exec-engine.md、holocene/exec-engine.md（9B extraData）
  - op-geth: consensus/misc/eip1559/{eip1559.go,eip1559_optimism.go}、params/config.go（denominator/elasticity）
  - op-reth: crates/evm/src/l1.rs（L1 fee 公式）、crates/evm/src/config.rs、crates/consensus/src/lib.rs
另需核对: 金向量语料（opstack-executor/tests/t8n）与 op-geth 的一致性；语料是 symlink，注意悬空风险。
附加轴（本段唯一）: 除 fisco-review 外，**另跑一轮 cpp-pro 的内存安全/UB 轴**
  （ASan+UBSan；`build-asan` 需先 cmake 重配），把 sanitizer 结论并入你的报告；
  这是五段里唯一从未跑过 sanitizer 的段落。
目标闭合:
  ninja -C build opstack-executor-block-tests opstack-executor-tests opstack-executor-receipt-tests bcos-evm-opstack-tests
  build/opstack-executor/tests/opstack-executor-block-tests
  build/opstack-executor/tests/opstack-executor-tests
  build/opstack-executor/tests/opstack-executor-receipt-tests
  build/bcos-evm/test/bcos-evm-opstack-tests
  ASan 侧：ninja -C build-asan opstack-executor-block-tests opstack-executor-tests
```

- [ ] **Step 2: 校验提示词**（路径 `s4.md`）→ `PROMPT-CLEAN`
- [ ] **Step 3: 交付新会话并等完成**（含 cpp-pro sanitizer 轴）
- [ ] **Step 4: 收集并校验产物**（目录 `s4-51115a169`、head `51115a169`）
- [ ] **Step 5: 更新 INDEX 的 S4 行**

---

### Task 4: S2 独立评审（`a961fe249..530fc3f1a`，5 提交 / +223）

**Files:**
- Create: `.agents/reviews/prompts/s2.md`
- 会话产出：`.agents/reviews/s2-530fc3f1a/`

- [ ] **Step 1: 写该段提示词**

Create `.agents/reviews/prompts/s2.md` = `_preamble.md` + ：
```markdown
## 本段
milestone: s2      区间: a961fe249..530fc3f1a（head 530fc3f1a，5 提交，含段末修复 ec234f5ac / 530fc3f1a）
设计与计划:
  /Users/octopus/octo/code/FISCO-BCOS/docs/2026-09-10-s2-op-fork-schedule-production-design.md
  /Users/octopus/octo/code/FISCO-BCOS/docs/plans/2026-09-10-s2-op-fork-schedule-production.md
  （两文档也可能在 worktree 的 docs/ 下，两处都找一下）
上一轮台账: 无 fisco-review（仅有过 cpp-pro 与两轮 coding-standards）→ 首轮
materialize 目录: bcos-framework/bcos-framework/ledger bcos-evm/bcos-evm/opstack bcos-evm/test/opstack bcos-tool/test/unittests/libtool bcos-ledger/test/unittests/ledger
上游焦点:
  - specs: superchain-upgrades.md（fork 列表与顺序，karst 是否在列）
  - op-geth: params/config_op.go（fork 顺序、**无** DeltaTime）、params/config.go:510-521（字段名与顺序）
  - op-node: rollup/types.go 的 checkFork（"后一档存在则前一档必须已设"正是严格连续的依据）
  - op-reth: crates/hardforks/src/lib.rs（fork 枚举）、crates/chainspec/src/{op,basefee}.rs
另需核对: 名字表 `c_opForkNames` 与 OpFork 枚举的 1:1；cap 8→16 的理由与 512 字节上限的关系。
目标闭合:
  ninja -C build bcos-evm-opstack-tests test-bcos-tool test-bcos-ledger
  build/bcos-evm/test/bcos-evm-opstack-tests（含 OpForkScheduleCodecSuite / OpForkScheduleSuite）
  build/bcos-tool/test/test-bcos-tool --run_test=NodeConfigOpForkScheduleTest
  build/bcos-ledger/test/test-bcos-ledger --run_test=OpForkScheduleMetadataTest
```

- [ ] **Step 2: 校验提示词**（路径 `s2.md`）→ `PROMPT-CLEAN`
- [ ] **Step 3: 交付新会话并等完成**
- [ ] **Step 4: 收集并校验产物**（目录 `s2-530fc3f1a`、head `530fc3f1a`）
- [ ] **Step 5: 更新 INDEX 的 S2 行**

---

### Task 5: S1 独立评审（`106da9938..cf75010da`，10 提交 / +1003）

**Files:**
- Create: `.agents/reviews/prompts/s1.md`
- 会话产出：`.agents/reviews/s1-cf75010da/`

- [ ] **Step 1: 写该段提示词**

Create `.agents/reviews/prompts/s1.md` = `_preamble.md` + ：
```markdown
## 本段
milestone: s1      区间: 106da9938..cf75010da（head cf75010da，10 提交，含段末修复 cf75010da）
设计与计划:
  /Users/octopus/octo/code/FISCO-BCOS/docs/2026-09-10-s1-official-genesis-rollup-design.md
  /Users/octopus/octo/code/FISCO-BCOS/docs/plans/2026-09-10-s1-official-genesis-rollup.md
上一轮台账（known-items；核对 premise）:
  .agents/reviews/s1-8a99ee401/findings.json（F1–F4 fixed@cf75010da）
materialize 目录: tools/opstack-genesis bcos-ledger/test/unittests/ledger bcos-tool/test/unittests/libtool bcos-tool/bcos-tool/NodeConfig.cpp bcos-ledger/bcos-ledger/Ledger.cpp bcos-framework/bcos-framework/ledger
上游焦点:
  - specs: superchain-config.md、superchain-upgrades.md（fork 时间与 genesis 语义）
  - op-geth: superchain/chain.go（embed 与 COMMIT）、params/superchain.go（registry→config 映射）
  - op-node: rollup/superchain.go 的 LoadOPStackRollupConfig / applyHardforks（rollup.json 字段映射）
  - op-reth: crates/chainspec/src/{op,op_sepolia,basefee}.rs（genesis 映射）
另需核对: registry 工件 pin（zip 内 COMMIT 9cf0456a…）与 op-geth 源码 pin 不同，需分别记录；
  rollup.json 含 karst_time 是**有意行为**（对照 checkout 的 op-node 无 KarstTime 且
  DisallowUnknownFields，会拒收 —— 若报告要判它 divergent，必须先核对这条是否已记录为有意偏离）。
目标闭合:
  ninja -C build test-bcos-ledger test-bcos-tool
  build/bcos-ledger/test/test-bcos-ledger --run_test=GenesisEthHeaderTest
  build/bcos-tool/test/test-bcos-tool --run_test=NodeConfigEthGenesisHeaderTest
  build/bcos-tool/test/test-bcos-tool --run_test=NodeConfigOpForkScheduleTest
  cd tools/opstack-genesis && python3.11 -m pytest test_gen_official_genesis.py test_gen_eth_header_fixture.py test_gen_rollup_config.py test_build_allocs.py
  实跑：python3.11 gen_official_genesis.py --zip /Users/octopus/octo/code/op-geth/superchain/superchain-configs.zip --chain mainnet/base --out-dir /tmp/s1-review-base
        并将 sepolia/op 与 --extra-fork karst:1781712001 一并核（header_hash == expected_l2_hash）
```

- [ ] **Step 2: 校验提示词**（路径 `s1.md`）→ `PROMPT-CLEAN`
- [ ] **Step 3: 交付新会话并等完成**
- [ ] **Step 4: 收集并校验产物**（目录 `s1-cf75010da`、head `cf75010da`）
- [ ] **Step 5: 更新 INDEX 的 S1 行**

---

### Task 6: 接缝集成评审（10 个跨段文件）

**Files:**
- Create: `.agents/reviews/prompts/integration.md`
- 会话产出：`.agents/reviews/integration-cf75010da/`

- [ ] **Step 1: 写集成轮提示词**

Create `.agents/reviews/prompts/integration.md` = `_preamble.md` + ：
```markdown
## 本段
milestone: integration      区间: 五段全部（c0045d186..cf75010da），但**只审下面 10 个接缝文件**
上一轮台账: 五段各自的台账（.agents/reviews/s5s6-*、s3-*、s4-*、s2-*、s1-*）——作为 known-items
目的: 单段切片看不到的跨段交互（同一个文件被 ≥2 段改过）。对每个文件追问：
  - 后一段的改动是否推翻/弱化了前一段的断言（例如 S3 与 S5+S6 都改 OpEngineService.inl、
    S2 与 S4 都改 OpForkSchedule、S1 与 S2 都碰 NodeConfigOpForkScheduleTest）？
  - 跨段的 fork 语义是否一致（fork 名/顺序/时间窗/错误码）？
  - 是否存在同一语义的两份实现或同一常量的多处字面量（checklist #21/#37）？
接缝文件:
  bcos-evm/bcos-evm/opstack/OpForkSchedule.h, .cpp, bcos-evm/test/opstack/OpForkScheduleTest.cpp
  bcos-tool/test/unittests/libtool/NodeConfigOpForkScheduleTest.cpp
  engine/bcos-engine/OpEngineService.h, .inl
  engine/test/unittests/engine/OpEngineImportFcuTest.cpp, OpEngineServiceParityTest.cpp,
    support/OpEngineKarstTestHarness.h
  opstack-executor/OpSchedulerSeam.h
上游焦点: 按每个接缝文件所属主题取对应权威（S2 段：config_op.go/superchain-upgrades.md；
  S3/S5+S6 段：api.go/api_optimism.go/types.go；S4 段：eip1559*）。
目标闭合: 五段 target 的并集（与 Task 1–5 相同），只需跑与接缝文件相关的 target：
  test-bcos-engine, opstack-executor-block-tests, bcos-evm-opstack-tests, test-bcos-tool
```

- [ ] **Step 2: 校验提示词**（路径 `integration.md`）→ `PROMPT-CLEAN`
- [ ] **Step 3: 交付第 6 个新会话并等完成**
- [ ] **Step 4: 收集并校验产物**（目录 `integration-cf75010da`、head `cf75010da`）
- [ ] **Step 5: 更新 INDEX 的集成轮行**

---

### Task 7: 修复尾段（按段分组提交）

**Files:**
- Modify: 依各段 finding 的实际位置而定（只改发现问题的文件）
- 台账：各段 `findings.json` 的 `fix_sha` / `origin_milestone`

- [ ] **Step 1: 汇总待修 finding（按 milestone 归并）**

Run:
```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550
python3.11 - <<'PY'
import json, glob, collections
by=collections.defaultdict(list)
for p in glob.glob('.agents/reviews/*/findings.json'):
    for i in json.load(open(p)):
        if i['status'] in ('open','partial'):
            by[i.get('milestone','?')].append((i['id'], i['severity'], i['title'], i['location']))
for m in ('s5s6','s3','s4','s2','s1','integration'):
    for r in sorted(by.get(m,[]), key=lambda x: x[1]):
        print(f"[{m}] {r[0]} {r[1]:7s} {r[2][:60]}  @{r[3]}")
PY
```
Expected: 清单按段分组。若为空，跳到 Task 8（全部已闭合）。

**排序规则**：`severity == BLOCKER|HIGH` 且位置在**已推送代码**上的 finding **不按段序排队**——排在所有修复
之前先落地（五段都已 push，这类问题可能影响正在进行的评审/合并），并在 `INDEX.md` 的该段行标注
「影响已推送代码」。

- [ ] **Step 2: 逐条修复（TDD：先红后绿）**

对每条 finding：先写一个能复现它的测试（红）→ 最小修 → 跑绿。**只改 finding 指出的代码**，不顺手重构。

- [ ] **Step 3: 按段分组提交**

Run（**逐段分组**：文件清单由台账的 `milestone` + `location` 决定，不要手抄）：
```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550
for m in s5s6 s3 s4 s2 s1 integration; do
  files=$(python3.11 - "$m" <<'PYINNER'
import json, glob, sys
m=sys.argv[1]; out=set()
for p in glob.glob('.agents/reviews/*/findings.json'):
    for i in json.load(open(p)):
        if i.get('milestone')==m and i['status'] in ('open','partial'):
            out.add(i['location'].split(':')[0])
print(' '.join(sorted(out)))
PYINNER
)
  [ -z "$files" ] && { echo "skip $m (no findings)"; continue; }
  echo "== $m -> $files"
  git add $files && git commit -m "fix($m): close milestone review findings"
done
git log --oneline -12
```
Expected: 每段一笔提交（无 finding 的段跳过），全部位于分支尾部。

- [ ] **Step 4: 回填台账**

对每条已修的 finding：把 `status` 改 `fixed`、`fix_sha` 填该组提交的短 sha；若修复落在与 `milestone` 不同的段（例如 S4 的问题只能落在后来的段），另填 `origin_milestone`。

Run:
```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550
python3.11 - <<'PY'
import json, glob, subprocess
sha=subprocess.run(['git','rev-parse','--short','HEAD'],capture_output=True,text=True).stdout.strip()
n=0
for p in glob.glob('.agents/reviews/*/findings.json'):
    f=json.load(open(p)); changed=False
    for i in f:
        if i['status'] in ('open','partial'):
            i['status']='fixed'; i['fix_sha']=sha; changed=True; n+=1
    if changed: json.dump(f, open(p,'w'), indent=2)
print('closed:', n, 'at', sha)
PY
```
Expected: 打印关闭条数与 sha（正式闭环前仍需 Task 8 的 delta 复核）。

- [ ] **Step 4b: 处置「真实但未记录的有意偏离」**

若某条 finding 的结论是「该分歧正确，但设计决策日志里没有记录」，则**不改代码**，改为：

1. 把该偏离写成一段请示（上游引用 + 本仓行为 + 为何有意）交给用户；
2. 由用户/作者写入**主仓** `docs/` 对应设计文档的决策日志（评审侧不改主仓）；
3. 台账记 `wontfix` + `deferred_until`（"设计决策日志已补记录"）；
4. 报告里核验「文档已存在」并附路径。

- [ ] **Step 5: 更新 INDEX 的 `fix_sha` 列**

---

### Task 8: delta 复核 ×5（含集成轮）

**Files:**
- Modify: 各段 `findings.json`（status/premise）、`.agents/reviews/INDEX.md`

- [ ] **Step 1: 逐段核对 `Fixed in <sha>`**

对每段：取回该段评审会话（或新会话），把「修复提交的 diff + 修复前该条 finding 的原文」交给它，要求：

1. 逐条回答 `fixed` / `partial` / `rejected + premise`；
2. **在修复实际改动过的函数/区域上，重跑原 finding 所属的 checklist 项**（fisco-review 的硬规则：
   「a fix is not a re-review」——确认「机制存在」不等于修好了；半修会通过「命名机制存在」的检查）；
3. **重跑该段 target**；
4. 判 `fixed` 的前提是 (1)(2)(3) 全部通过；任何一项不过 → `partial`，退回 Task 7。

- [ ] **Step 2: 记录 delta 轮**

每段写 `.agents/reviews/<seg>-<修复后 head>/round2-delta.md`（含状态表 + 回归结果 + 边界声明），并把该段台账状态定格。

- [ ] **Step 3: 校验收敛判据**

每段必须满足：所有 finding 为 `fixed@<sha>` 或 `rejected + premise` ∧ 无新 finding ∧ 无未披露行为变更。不满足则回到 Task 7（轮次上限 3）。

- [ ] **Step 4: 更新 INDEX**

把每段的**轮次** +1、**台账目录**改为本轮（delta）的目录（`<seg>-<修复后 head 短写>`；按设计 §4 的命名规则，
head 变了就是新目录）、**状态计数**按定格后的台账重算、**收敛**列置「是」。

---

### Task 9: 收口

- [ ] **Step 1: INDEX 全绿检查**

Run:
```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550
grep -c "否" .agents/reviews/INDEX.md | sed 's/^/未收敛行数: /'
```
Expected: `0`。

- [ ] **Step 2: 越界检查**

Run:
```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550
git log --oneline cf75010da..HEAD | cat   # 只应出现 Task 7 的修复尾段提交
git status --short                          # 只应有长期 untracked（docs/、验证 harness）
```
Expected: 分支尾部只有修复提交；无生产代码以外的意外改动。

- [ ] **Step 3: 落实"未 push 段先评审"的约定**

把设计 §9 的规则写进下一次里程碑的开工清单（新段在 push 前完成该段评审与修复，使修复天然紧跟本段任务之后）。

---

## 关门检查（全部 Task 后）

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550
# 1) 五段 + 集成轮共 6 个台账目录都存在且 findings.json schema 合法
python3.11 - <<'PY'
import json, glob
need={'id','severity','origin','scope','title','location','status','milestone','round','claims'}
dirs=[d for d in glob.glob('.agents/reviews/*/findings.json') if any(k in d for k in ('s5s6-','s3-','s4-','s2-','s1-','integration-'))]
assert len(dirs)>=6, dirs
for p in dirs:
    f=json.load(open(p))
    for i in f:
        assert not (need-set(i)), (p, i.get('id'))
        if i['status']=='rejected': assert i.get('premise'), (p,i['id'])
        for c in i['claims']:
            assert c['verdict'] in {'aligned','divergent','cannot-determine'}, (p,i['id'],c)
print('ledgers ok:', len(dirs))
PY
# 2) INDEX 每行收敛
grep -E "^\| S|^\| 集成" .agents/reviews/INDEX.md
# 3) 每段目标回归（与 Task 1–5 相同命令）
```
Expected: 台账 6 份合法；INDEX 六行全部「是」；五段 target 全绿。

## 明确不做

- 不改写历史 / 不 squash / 不 force-push。
- 不把台账、提示词或本设计提交到分支（`.agents/reviews/` 保持本地）。
- 评审会话内不改代码。
- 不构建或运行 op-geth / op-reth（只按源码引用）。
- 不覆盖历史 `pr-karst-on-5550-*` 目录。
- 不启用 fisco-review 的 full（sub-agent）模式。
