# OP 特性逐档代码 review 方案（9 个单元 × 4 技能）

> 本文件与其它过程文档一样留在 worktree `docs/`（untracked，不入库）。
> 状态：**待你批准后执行**；本方案本身不产出代码提交。

## 1. Goal

对分支 `feat/karst-on-release-3.18` 上的 **9 组 OP 特性**各跑一次**独立的、单遍的**代码 review，
每条 finding 都要过一遍**独立复核**（fisco-verify-findings）才能进台账；
另加两条专业轴（cpp-pro 的内存/UB 与并发、cpp-coding-standards 的 Core Guidelines）。

**与已完成评审的关系（必须先读，否则会做无用功）**

本分支的 S1–S6 已在上一轮做过六轮独立评审（`.agents/reviews/`：43 条 finding，41 fixed + 2 wontfix），
区间是 `c0045d186..cf75010da`——**与本方案的区间高度重叠**。所以本方案的增量价值只有三条：

1. **切分维度不同**：上轮按里程碑切（S4 / S5+S6 / S2 / S3 / S1），本方案按**特性**切。
   同一特性横跨多个里程碑（例如「费用模型」同时在 S4 的 EVM 侧和 S5+S6 的引擎侧），
   单里程碑切片看不到这类跨模块对称性问题——上轮只能靠一个「接缝集成轮」看 10 个文件来补，覆盖有限。
2. **新增两条轴**：上轮只有 S4 跑过一次 sanitizer；cpp-coding-standards 从未系统跑过。
3. **补上独立复核闸门**：上轮**没有** verifier，delta 复核是我自己做的；
   本次每条 finding 都要拿到 `confirmed`/`refuted` 级别的证据（含复现命令）才算数。

**去重是硬要求**：上轮 **43 条**（41 fixed + 2 wontfix）必须作为 known-items 传入每个单元，**不得重报**。
台账按文件前缀机械过滤生成，不靠人回忆（来源清单见 §5）。

## 2. 固定 pin 与冻结规则

| 项 | 值 |
|---|---|
| base | `c0045d186`（OP 特性开发起点） |
| head | `5812ebe12`（方案写就时；**执行前必须重新 `git rev-parse HEAD` 复核**） |
| 区间 | `c0045d186..<head>`（含 P1 的 6 笔测试提交） |
| 工作树 | `/Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550` |
| 上游 pin | specs `564a0cea…` / op-geth `d0734fd5…` / op-node `76e4fad5…` / op-reth in-tree 同 op-node / corpus op-geth `e8800cffe…` |

**冻结规则（fisco-review 把 head 漂移定义成 review 自己的 bug）**：

- 整个 campaign 期间**不得向本分支提交**。任何一次提交都会让 9 个单元里已 materialize 的树行号失效。
- 每个单元开工前重新 `git rev-parse HEAD`，与 pin 不符即停下重钉（并作废该单元已产出的行号引用）。
- 写报告前再复核一次 head；若已变化，把新提交列进 verification boundary，且该单元**不得判收敛**。
- 引用 op-geth 行号一律 `git show d0734fd5f44234cde3b0a7c4beb1256fc6feedef:<path>`
  （本机该树工作区脏，纯注释改动，行号错位约 200 行）。

## 3. 九个审核单元

`dossier` = 该单元开审前要一次性喂给 review 会话的输入（文件集合 + 上游焦点 + target 清单）。
`受影响 target` 是**目标闭合**用的：技能要求逐个编译并运行，只报「某个 suite 绿」不算。

| # | 单元 | 主要文件集合 | 上游权威 | 受影响 target |
|---|---|---|---|---|
| U1 | Fork 身份、调度与方法档 | `bcos-framework/.../engine/OpForkId.h`、`OpTime.h`；`bcos-evm/opstack/OpForkSchedule.{h,cpp}`；`bcos-framework/ledger/{OpForkScheduleCodec.h,ChainMetadata.h}`；`bcos-ledger/Ledger.cpp`（元数据写）；`bcos-tool/NodeConfig.cpp`（`op_fork_schedule`）；`libinitializer/Initializer.cpp`；`opstack-executor/OpSchedulerSeam.h` | op-geth `params/config_op.go`（**无 Delta 项**）、`params/config.go` 字段序；op-node `rollup/types.go` `checkFork`（严格连续性的依据）；specs `superchain-upgrades.md`；op-reth `crates/hardforks`、`crates/chainspec` | engine、bcos-evm-opstack、tool、ledger |
| U2 | 费用模型（L1 fee / baseFee 时钟） | `bcos-evm/opstack/{OpFeeParams,RollupCost}.{h,cpp}`、`OpTransition.cpp`（收费与金库路由）、`OpHost.cpp`（BLOBBASEFEE）；`bcos-framework/engine/OpBaseFee.h`、`OpPredeploys.h`；`engine/bcos-engine/EngineServiceCommon.cpp`（extraData 编码） | specs `ecotone/l1-attributes.md`、`fjord/exec-engine.md`、`granite/exec-engine.md`、`holocene/exec-engine.md`；op-geth `core/types/rollup_cost.go`、`consensus/misc/eip1559/eip1559_optimism.go`；op-reth `crates/evm/src/l1.rs` + `alloy-op-evm 0.27.0` | bcos-evm-opstack、opstack-executor-tests/-receipt-tests/-block-tests |
| U3 | Deposit 与 L1 attributes | `opstack-executor/{OpDepositEncode.h,OpstackExecutor.h,OpBlockExecute.{h,cpp}}`、`OpScheduler.h`（块路径的门）；`bcos-evm/opstack/OpTransition.{h,cpp}`（`runDeposit`）；`bcos-rpc/.../model/DepositTransaction.*` | specs `deposits.md`、`exec-engine.md`；op-geth `core/types/deposit_tx.go`、`core/state_transition.go`、`miner/` | opstack-executor-*（4 个）、engine、rpc |
| U4 | 头部承诺与字段集 | `opstack-executor/OpBlockExecute.cpp`（`sealOpBlock`）、`OpBlockExecute.h`、`OpCommitments.h`；`engine/bcos-engine/OpEngineService.cpp`（`rebuildOpEthHeader`）、`EngineServiceCommon.h` | specs `isthmus/exec-engine.md`、`canyon/`、`ecotone/`；op-geth `core/types/block.go`、`core/genesis.go`；op-reth `crates/chainspec` | opstack-executor-block-tests、engine、bcos-evm-opstack |
| U5 | Engine API 方法窗与 −38005 | `engine/bcos-engine/OpEngineService.{h,inl,cpp}`（窗口/广告/档校验）、`EngineServiceCommon.{h,cpp}`（payload 形状）；`bcos-rpc/.../endpoints/EngineEndpoint.{h,cpp}`、`web3jsonrpc/utils/Common.h` | **op-node `rollup/types.go` 的 `NewPayloadVersion`/`GetPayloadVersion`/`ForkchoiceUpdatedVersion`（CL 选方法号的唯一权威）**；op-geth `eth/catalyst/api.go`（方法号×时间窗、caps 反射）；specs `exec-engine.md` 各档 | engine、rpc、bcos-evm-opstack |
| U6 | 导入与 canonicalize | `engine/bcos-engine/OpEngineService.{h,inl}`（`canonicalizeImportedHead`、undo journal、switch/back-reorg、prune）；`opstack-executor/{OpScheduler.h,RecentBlockHashes.h,OpSchedulerSeam.h}`；`bcos-framework/storage2/{MultiLayerStorage.h,MemoryStorage.h}` | specs `derivation.md`（canonical/safe/finalized）、`exec-engine.md`；op-geth `eth/catalyst/api.go`（`SetFinalized`/`SetSafe`/`InsertBlockWithoutSetHead`/`delayPayloadImport`）、`core/blockchain.go SetCanonical`；op-reth `crates/consensus/src/validation/mod.rs` | engine、opstack-executor-block-tests |
| U7 | 官方创世与 rollup.json | `tools/opstack-genesis/*.py`（含 `gen_official_genesis.py`）；`bcos-tool/NodeConfig.cpp`（eth genesis 头）；`bcos-ledger/Ledger.cpp`（创世写）；`bcos-framework/ledger/GenesisConfig.h` | specs `superchain-config.md`、`superchain-upgrades.md`；op-geth `superchain/chain.go`、`params/superchain.go`；op-node `rollup/superchain.go` `LoadOPStackRollupConfig`；registry zip 内 `COMMIT 9cf0456a…`（**与源码 pin 不同，分别记录**） | ledger、tool、`tools/opstack-genesis` 的 pytest、以及**真跑一次 CLI** |
| U8 | Karst 档（本分支特有） | `OpForkSchedule.cpp`（`karstConfig`/`ensureKarstIsOsaka`）、`OpPrecompiles.cpp`（`karstPrecompileOverrides`）、`OpForkId.h`（karst 行）、`OpForkScheduleCodec.h`（`"karst"`）、`tools/opstack-genesis`（`--extra-fork`）及相关测试 | **上游不存在 Karst**：对齐轴必须记 `n/a (local design assertion)`，oracle 换成内部设计文档 + Osaka EVM 一致性 + op-geth 在等价 revision 下的行为 | bcos-evm-opstack、engine、opstack-executor-* |
| U9 | RPC / 收据 / 交易池 | `bcos-tars-protocol/.../{TransactionReceiptImpl.cpp,TransactionImpl.cpp,Web3TarsBridge.cpp}`；`bcos-txpool/.../TxValidator.cpp`；`bcos-tx-validator/{Normalize.cpp,CheckSet.h}`；`bcos-framework/engine/RawTransactionDispatch.h`；`bcos-tool/NodeConfig.cpp`（`op_engine_rpc`） | specs `exec-engine.md` 收据字段；op-geth `core/types/receipt_opstack.go`、`gen_receipt_json.go`、`core/types/rollup_cost.go` | tars-protocol、rpc、txpool、tx-validator |

**U8 的特殊性**：它没有上游 oracle，因此该单元「无法对齐」是**预期结果**而不是缺陷，
但必须逐条声明为 `n/a (local design assertion)` 并附内部设计文档出处——不得编造上游引用。

## 4. 三段式流程（每个单元跑一遍）

### Stage A —— 主审（fisco-review v1.20.1，单遍模式）

- 判定路径：`git diff c0045d186...<head> -- <单元文件集合>`（三点），materialize 后 `verify_tree.sh` 校验，
  **任何 mismatch 立即停下**。
- **单遍**（主线程通读），不派 sub-agent：本仓的实测是本 skill 的 sub-agent 路径成本 3–5× 预算而召回更低；
  只有当某单元 diff 超过 **>2000 行 / >5 模块** 时才考虑 full 模式，且必须走聚合闸门。
- 报告六节 + **额外的 Upstream alignment 节**：枚举该单元 diff 的**每条行为性断言**
  （比较运算、错误码、fork 门控、费用公式、字段集决策、版本窗、stateRoot 构造…），
  逐条给 specs / op-geth / op-reth 三处引用与 `aligned | divergent | cannot-determine`。
  `divergent` 进 finding，除非它精确等于设计决策日志里已记录的有意偏离。
- **目标闭合**：编译并运行该单元「受影响 target」列的**全部** target，逐个报用例数。
- 产出：`findings.json`（沿用上轮 schema：`id/severity/origin/scope/title/location/status/fixes/milestone`
  + `claims[]`，并新增 `unit` / `axis` / `round` / `verification_verdict`）、`known-clean.json`、`REPORT.md`。
- **不发 finding 的核查项**要写进「Checked, and not a problem」，每条附证明其安全的原码引用。

### Stage B —— 两条专业轴（与 A 分开跑，独立归属）

分开跑是刻意的：这样每条发现能归因到具体轴，聚合时才看得出哪个轴在产出真问题。

**B1. cpp-pro（内存安全 / UB / 并发 / 性能声明）**
- 在 `build-asan` 中对受影响 target 跑 **ASan + UBSan**（需先 `cmake` 重配，见 §9 陷阱）。
- 并发轴：atomics 使用、锁是否跨 `co_await`、数据竞争、无锁结构。
  已知先例：`MemoryStorage::mergeConcurrent` 的双写竞争就是这类问题（上轮由 delta 复核抓到）。
- 性能：技能规定「无测量不下性能结论」（Per.6）。任何性能主张必须带测量数据，否则不写成 finding。
- 产出 finding 标 `axis: cpp-pro`，证据 = sanitizer 原始日志或测量数字。

**B2. cpp-coding-standards（Core Guidelines 机械轴）**
- 只审该单元**改动过的文件**；逐条比 P/I/F/C/R/ES/E/Con/CP/T/SL/Enum/SF/NL/Per 清单。
- **噪声门禁（重要）**：只报两类，否则会被格式化噪声淹没——
  (a) **正确性相邻**的规则（`CP.20` RAII 锁、`CP.22` 抱锁调未知代码、`ES.46` 收窄转换、
  `ES.50` 去 const、`F.43` 返回局部引用、`E.16` 析构抛异常…）；
  (b) **本仓约定**的违反（命名、`bcos::` 限定、`noexcept` 标注、头文件自包含）。
  纯格式问题不报（仓库已有 clang-format 钩子强制）。severity 上限 MEDIUM，且只在 (a) 类可达时报。
- 产出 finding 标 `axis: standards`。

### Stage C —— 独立复核（fisco-verify-findings v1.1.0）

对 **A ∪ B1 ∪ B2 的全部 finding** 以及 A 的 Upstream alignment 节里所有 `divergent` 声称，逐条判定：

| 判定 | 处理 |
|---|---|
| `confirmed` | 进台账 open 集（需复现命令 + expected/observed 原始输出） |
| `confirmed_symptom_mechanism_differs` | 进 open 集，但**改写 finding 的机制描述**（不得沿用 reviewer 的错误归因） |
| `refuted` | 挡在 open 集之外，记反证要点 |
| `stale` | 挡在 open 集之外，记 `fixed_at` |
| `unreproducible` | 只允许记为 hardening，**不得**据此 recommend fix，**不得**升级成 `refuted` |
| `undecidable` | 写清 `missing_inputs` |

硬门禁（技能原文）：无 pin 不判定；证据只读 `git show <PIN>:<path>`；**无复现不下 `confirmed`/`refuted`**；
复现不得改被测生产代码（改了就按 mutation probe 处理：记录 diff、验证症状、还原后 `git diff` 必须为空）；
必须显式判定可达性（live path / 仅测试 / 死代码）。

**同时复核「未实现清单」**：我上一轮列出的 10 条「确认没有实现」也都是**声称**，
逐条用 `git grep` 在 pin 上做反向确认（结构性声称，复现 = grep 结果 + 退出码）。
这一项不新增 review 单元，作为 Stage C 的一个固定清单跑。

产出：`verification_U<k>.json`，必须能过
`python3 <fisco-verify-findings>/scripts/validate_verification.py <file> --repo .`
（**一定带 `--repo`**，否则只做结构校验，行号与代码引用全不检查）。

## 5. 台账与产物布局（本地，untracked）

```
.agents/reviews/
  prompts/_preamble.md                 # 复用上轮前缀，逐字保留；不得含作者结论
  prompts/opfeat-U<k>.md               # = 前缀 + 该单元 dossier
  opfeat-U<k>-<head8>/                 # 每个单元一个目录
    tree/                              # materialize + verify_tree 过的头树
    findings.json                      # A/B1/B2 的 finding 合并（带 axis/verification_verdict）
    known-clean.json
    REPORT.md                          # 六节 + Upstream alignment
    verification_U<k>.json             # Stage C 产物
  INDEX-OPFEAT.md                      # 全局状态：每单元的轮次/台账目录/状态计数/收敛/验证计数
```

- **known-items 机械生成**：对每个单元，从下表来源的 `findings.json` 里按 `location` 的文件前缀
  与该单元文件集合求交集，作为 known-items 传入——**不靠回忆**。
  再叠加同一 campaign 内更早单元的已闭合项（跨单元重复也要挡）。

  | 来源 | 条数 | 说明 |
  |---|---|---|
  | `s5s6-a961fe249` | 18 | 里程碑 campaign 的有效轮 |
  | `s3-106da9938` | 9 | 同上 |
  | `s4-51115a169` | 6 | 同上 |
  | `s1-cf75010da` | 4 | 同上 |
  | `s2-530fc3f1a` | 3 | 同上 |
  | `integration-cf75010da` | 3 | 接缝集成轮 |
  | **小计** | **43** | 41 fixed + 2 wontfix |
  | `pr-karst-on-5550-a10a3dddd` | 12 | **可选并入**：更早的 PR 评审 campaign，同一批代码；行号已过期，须按 symbol 重定位后再用 |

  **不要**读 `s1-8a99ee401` / `s3-2aefd8554` / `s5s6-4a915443a`：它们已被新轮取代
  （目录内有 `superseded_by.json`），读了会把旧结论当成有效 known-items。
  过滤规则：glob `.agents/reviews/*/findings.json` 时跳过带 `superseded_by.json` 的目录。
- 目录名用 `opfeat-` 前缀，与上轮的 `s1-`/`s5s6-`/`pr-*` 目录区分，互不覆盖。
- 台账、报告、验证 JSON **一律不入库**；分支上只允许出现代码与测试。

## 6. 执行顺序与轮次

**串行**（共用同一 `build/` 与 CPU；并发 ninja/test 会互相竞争并互相拖慢，上轮已实测）。

顺序按爆炸半径排（先审最靠近生产路径、耦合最广的，让后续单元能消费已验证的结论）：

```
U6 导入与 canonicalize（风险最高、改动最大）
→ U5 Engine API 方法窗
→ U2 费用模型
→ U3 Deposit
→ U4 头部承诺
→ U1 Fork 调度与方法档
→ U9 RPC/收据/交易池
→ U7 创世与 rollup.json
→ U8 Karst
```

**轮次上限：每单元 2 轮**。Round 1 = 全量单遍；Round 2 = 仅当该单元有 finding 被修后做 delta 复核。
修不修、何时修由你定；本方案的 Stage A/B/C 只出结论。
注意技能的硬规则：**「a fix is not a re-review」**——Round 2 必须在修复实际改动的函数/区域上
**重跑原 finding 所属的 checklist 项**，只确认「机制存在」不算修好。

## 7. 每单元的验收判据（收敛）

一个单元判收敛需**同时**满足：

1. 该单元每条 finding 都有 Stage C 的判定；open 集内**零未复核**项。
2. `refuted` / `stale` 项已从 open 集移出并记录反证/`fixed_at`。
3. **目标闭合**：受影响 target 列全部编译并运行通过，逐个报了用例数（口径见 §9）。
4. Upstream alignment 覆盖该单元每条行为性断言；无「有行为性改动却没对齐条目」的遗漏。
5. `verification_U<k>.json` 通过 `validate_verification.py --repo .`。
6. 无未披露的行为变更；有则必须与代码一致（**文字与行为矛盾 = mis-disclosed，阻塞收敛**）。
7. 未解决的 `cannot-determine` 已在下游台账显式列出「最小读取集 + 为何在树内无法解决」，
   不得静默消失。

九个单元全部收敛后，产出一份 campaign 汇总：按 `axis` 统计（哪个轴产出真问题最多）、
跨单元重复项、以及跨文件对称性发现（这类是本次切分的主要收益）。

## 8. 提示词纪律与硬约束

- 提示词**不得包含作者结论**：不出现批次报告、commit message 结论、「已知问题」提示、上轮的裁决结论。
  作者注释与 commit message 都是**待核实的断言**，不是证据。
- 评审会话（A/B/C）**不得修改代码**；只出 finding、判定与建议。
- 只改 worktree；**禁止碰主仓脏工作区**。
- 不改写历史 / 不 squash / 不 force-push；campaign 期间不提交。
- 对照树只读、**不构建不运行** op-geth / op-reth（只按源码引用）。
- 报告正文英文（沿用仓库评审历史口径），生命周期标记与 `INDEX-OPFEAT.md` 用中文。
- 每种严重度语义按 fisco-review 的定义（BLOCKER/HIGH/MEDIUM/LOW），不自行发明。

## 9. 环境陷阱清单（照抄进每份提示词，别再踩）

- **`--list_content` 不可信**：Boost 把它写 **stderr**（`2>/dev/null` 会得到 0 例），
  且对 `test-bcos-tool` 会漏列 4 个 suite（列 107 而实跑 111）。**计数一律用 `Running N test cases`**。
- Boost 成功串 `*** No errors detected` **含子串 `errors detected`**——判红不得用它做子串匹配。
- Boost `--run_test` **不吃逗号分隔**；多过滤只能多进程。
- 套件名写错 = 0 例 = **假绿**；每个 suite 都要报实际用例数。
- `engine/test/CMakeLists.txt` 用**配置期 GLOB**：新增测试文件后必须 `cmake -B build -S .` 重配，
  `build-asan` 同理（否则 sanitizer 下该 suite 根本没跑，表现为用例数比正常构建少）。
- op-geth 评审树工作区脏（纯注释），行号**必须** `git show <pin>:<path>`。
- `opstack-executor/tests/t8n` 是指向语料仓的 **symlink**；本仓不追踪生成物。
- `tools/opstack-genesis` 的 pytest 用 `python3.11 -m pytest`（本机 `python3` 无 pytest）；
  registry zip 解压需 `zstd -d -D dictionary`。
- clang-tidy：homebrew LLVM 的 libc++ 缺 `char_traits<unsigned char>`，对用到 `bcos::bytes` 的头只能部分解析
  → **只做新旧差分，不单边下结论**。
- `git commit` 的 clang-format 钩子可能先拒一次（本 campaign 不提交，但若你中途提交需知道）。
- stock macOS `/bin/bash` 3.2 无 `mapfile`。
- 可能另有 worktree 在跑构建——看到构建进程先确认是不是自己的。

## 10. 成本估算（供你决定是否裁剪）

| 阶段 | 每单元 | 9 单元合计 |
|---|---|---|
| A 主审（单遍） | 数十 k tokens，分钟级 | ~0.5M tokens |
| B1 cpp-pro（ASan/UBSan 构建 + 跑） | 一次 asan 重配 + N 个 target 运行 | 主要是机时（构建占大头） |
| B2 standards（机械清单） | ~10–20k tokens | ~0.15M tokens |
| C 独立复核（逐条复现） | ~10–20k tokens × finding 数 | 取决于 finding 数，量级 ~0.3M |

合计大致 **≤1.5M tokens + 数十次 target 构建/运行**。若想压缩：
优先砍 U7/U9（离核心共识路径最远），但**不要砍 Stage C**——上轮的教训正是缺复核会让假 finding 进闭环。

## 11. 明确不做

- 不在本 campaign 内修代码、不开 PR、不推分支。
- 不重报上轮 43 条（41 fixed + 2 wontfix）finding（known-items 机械去重）。
- 不把「未实现清单」当成缺陷——它是**声称**，Stage C 只做真伪确认。
- 不为 U8（Karst）编造上游引用。
- 不使用 c-review（本环境需要未暴露的 `Workflow` 编排工具）；内存破坏/竞态类怀疑一律走 cpp-pro 的 sanitizer 证据，
  拿不到证据的记 `unreproducible` + 边界，而不是声称已由安全审计覆盖。
