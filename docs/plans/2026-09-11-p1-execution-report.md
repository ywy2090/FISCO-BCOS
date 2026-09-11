# OP-Stack 测试体系 P1 —— 执行报告（2026-09-11）

> 本文件与其它过程文档一样留在 worktree `docs/`（untracked，不入库）。
> 所有数字都是本机实测（命令与期望一并给出），不是推断。

## 0. 结论

**P1 的全部可本地验证判据已达成**：两个 oracle dumper 并入 `regen.sh` 仪式（一次跑通 rc=0、幂等）、
M1 矩阵测试绿且 `matrix cells checked: 26`（>0）、M4 序列矩阵 15 例绿（每步跑 I1–I6）、
变异 harness 三个变体全部 `RED as required` 且归因成立、spec §7 的 **10 个 target 全绿且用例数不减**。

产物分两处，**都还只在本地**（推送需要你的授权）：

| 仓 | 分支 | 提交 | 推送状态 |
|---|---|---|---|
| FISCO 工作树 | `feat/karst-on-release-3.18` | 6 笔（`4e0d2c893..5812ebe12`） | **未推送**（ahead 6） |
| 语料库仓 `~/.cache/fisco-t8n-corpus` | `feat/test-matrix-oracles` | 6 笔（`91c6a9f..938df36`） | **未推送**（无 upstream） |

## 1. 逐任务结果

### Task 0 — 前置校验与语料分支
- pin 复核：语料 op-geth `e8800cffe53d459cde8a07c8e8f1de9d86e79e07`（工作树 0 脏项）；
  optimism `76e4fad54244ec6bd07dad07e42c82a16ab5113a`（仅 `packages/contracts-bedrock` 脏，非 Go）。
- 基线 `regen.sh` rc=0（既验证机制，也确认语料与 pin 一致）。分支 `feat/test-matrix-oracles` 建立。
- 两棵 pin 树残留检查：0。

### Task 1 — `dump-geth-caps`
- 产出 `matrix/caps.json`：**36 条** caps，与计划里的预测量一致；含
  `engine_newPayloadV1..V5`、`engine_forkchoiceUpdatedV1..V4`、`engine_getPayloadV1..V6`；
  **不含** `engine_exchangeCapabilities`（op-geth 反射跳过自身）。
- `.gitignore` 只追加两个生成物；实测 `known_deviations.json` **未被**忽略（可入库）。
- 提交 `8de9d77`。

### Task 2 — `dump-opnodewin`
- 9 行窗口与预测量逐行一致：regolith → newPayloadV2/fcuV1/getPayloadV2；canyon → V2/V2/V2；
  ecotone–holocene → V3/V3/V3；isthmus/jovian/karst → V4/V3/V4。
- optimism 树无 `cmd/` 残留（`rmdir` 生效）。
- 提交 `8fafa07`。

### Task 3 — `known_deviations.json`
- 恰好 1 条：karst/getPayload，`expected_from_pin=engine_getPayloadV4` → `implemented=engine_getPayloadV5`，附设计决策出处。
- 提交 `458a904`。

### Task 4 — 并入 `regen.sh`
- 先写判据 4 并确认 RED：`rm matrix/caps.json` → rc=1 且打印 `matrix: caps.json missing`。
- 再实现：变量提到头部、op-node/op-service 子树清洁性 + HEAD==pin 断言、两个 dumper 各自在对应
  pin 树内 build、`cleanup()` 扩到两棵树（含 `rmdir "$OP_NODE_REPO/cmd"`）、判据 ③ 契约清单扩三行、
  `manifest.txt`/`SHA256SUMS` 幂等生成。
- 全量重跑 rc=0；**幂等**：第二次重跑后 `git diff --exit-code -- matrix/{manifest.txt,SHA256SUMS}` 无输出。
- 提交 `f1fc9a3`。

### Task 5 — M1 矩阵测试
- `OP_MATRIX_DIR` 编译定义 + `engine/test/unittests/engine/OpEngineApiMatrixTest.cpp`。
- 结果：`OpEngineApiMatrixSuite` **2 例绿**，打印 **`matrix cells checked: 26`**（9 fork × 3 方法 − 1 条已登记偏离）。
  只有 karst/getPayload 一条 recorded deviation。
- 仓库当前 **31 条 caps** 广告；断言成立（`forkchoiceUpdatedV4` 不广告、V1 系列不广告、
  除 `engine_exchangeCapabilities` 外都在 op-geth 列表里）。
- 提交 `4438ff3fd`。

### Task 6 — I1–I6 不变量库
- `support/SequenceInvariants.h`；提交 `219707108`。
- 注意：计划里"编译（空跑）"这一步是**构造上无效**的（此头文件此刻还没被任何 TU include）。
  因此我用 engine test target 的真实编译参数做了 `-fsyntax-only -include` 校验（rc=0）后才提交。

### Task 7a — 夹具抽取（纯重构）
- 两个匿名 namespace 的符号（11 个顶层 + 依赖辅助 + `ImportServiceFixtureT` 与两个别名）
  整体移入 `support/OpEngineKarstTestHarness.h` 的**既有** `op_engine_parity_test` 命名空间。
- 验收：`OpEngineImportFcuTest` 仍 **30 例全绿**；`nm` 里每个被移函数**只有 1 份定义**（无重复定义）；
  engine 总数不变。提交 `a5a1adf6f`。

### Task 7b — M4 序列矩阵
- `engine/test/unittests/engine/OpEngineSequenceMatrixTest.cpp`：S1–S14 + 门控 S15 = **15 例**。
- S15 实测跑通（`OPSEQUENCE_MATRIX_STEPS=200/2000` 各一次，绿）。
- 提交 `a37884033`。

### Task 8 — 变异 harness
- `tools/mutation/{run.sh,make-variant.sh,variants/{mapping.json,N1,N2,NEW-3}.patch}`。
- 三个变体都是**手工最小逆向 patch**（不是整份 `git apply -R`），三个都实测 `git apply` 干净。
- 实测：`bash tools/mutation/run.sh N1 N2 NEW-3` → **rc=0**，三行 `RED as required`，
  三条负控制（`also_green`）全绿，无 `NOT ATTRIBUTED` / `STILL GREEN`，跑完工作树干净。
- 提交 `3b9551413`。

### Task 9 Step 1 — CI 侧 provision CL 参照树
- `ensure-vectors.sh` 增加 `ensure_op_node()`（复用/克隆 + pin 断言 + 残留清理），
  `OP_NODE_PIN` 与 OPGETH 一样从 `regen.sh` 提取（单一事实源）。
- `action.yml`：pin 解析步骤同时解析 op-node pin；新增 optimism 树 cache；regen 步骤导出 `OP_NODE_REPO`。
- **端到端实测**（复用本机两棵 pin 树跑真实 CI 路径）：`ensure-vectors.sh` rc=0，
  regen 全判据通过，两棵树 0 残留，matrix 契约无变化。
- 提交 `6c1665d`、`938df36`。

### Task 9b — 把 scratch harness 转成正式回归钉子
评审期的临时夹具 `S5S6FindingsVerifyTest.cpp`（5 例）此前未跟踪。逐条评估后**只提升 2 例**，
写进新文件 `engine/test/unittests/engine/OpEngineImportPlaneTest.cpp`（Apache 头 + clang-format，
用例名按所守行为命名，改用共享的 `ImportSchedulerFixture`，不再复制一整套夹具）：

| 原用例 | 处置 | 理由 |
|---|---|---|
| V4 `mergeToBackends` FIFO 免疫 | **提升**为 `MergeToBackendsIsFifoImmune` | 全树唯一覆盖：没有任何测试同时驱动 `pushView` + `mergeToBackends` |
| V1 flat 是否含本块写入 | **提升**为 `FlatCapturesTheBlocksOwnWrites` | F1 的工件级直接断言，便宜且非重复 |
| V2 flat 平面 == 已 canonical 父状态 | 不提升 | 已被 `OpEngineImportFcuTest/ChainedImportMatchesCanonicalParentState` 覆盖，且那个版本更强（走真实 FCU canonicalize，而非手工 `canonicalizedTo`+merge） |
| V3 剪枝父平面 → SYNCING | 不提升 | 断言是 `failed \|\| a4.header != nullptr`，**恒真**；其真实断言按文件自述已在 `OpEngineImportFcuTest/PrunedParentPlaneIsSyncingNotEmptyReExecute` |
| V5 代价基准 | 不提升 | 唯一断言是 `perCallUs >= 0`（无符号，**恒真**）；计时进 PR gate 只会带来 flaky。实测数字留档：`backend rows=10`、`verifyCanonicalStateRoot=9us/call`、`txs=1`、`payloadBytesCopiedPerLookup=464` |

- 结果：engine 316 → **313**（−5 例 scratch，+2 例新钉子）；旧 suite 已从二进制消失；
  变异 harness 在新二进制上**重跑仍 rc=0、三个变体全部 RED 且归因成立**。
- 提交 `5812ebe12`。

### P1 验收（spec §9）
| 判据 | 实测 |
|---|---|
| 两个 dumper 并入 regen，一次跑通并产出工件 | rc=0；`matrix/{caps.json,engine_api_windows.json}` 存在；契约幂等 |
| M1 绿且 `matrix cells checked > 0`，只有一条 recorded deviation | 2 例绿；`26`；1 条 |
| M4 15 例全绿，每步 I1–I6 | 15 例绿（含随机 smoke） |
| 变异 `run.sh N1 N2 NEW-3` rc=0，逐行 RED 且归因成立 | rc=0；3×RED；负控制全绿 |
| Task 7a 是纯重构 | 30 例全绿，无重复定义 |
| spec §7 全部 target 绿且用例数不减 | 见下表，10/10 ok |

| target | Running | 基线 | rc |
|---|---|---|---|
| test-bcos-engine | **313** | 294 | 0 |
| opstack-executor-block-tests | 142 | 142 | 0 |
| opstack-executor-tests | 123 | 123 | 0 |
| opstack-executor-scheduler-tests | 37 | 37 | 0 |
| opstack-executor-receipt-tests | 26 | 26 | 0 |
| bcos-evm-opstack-tests | 173 | 173 | 0 |
| test-bcos-tool | 111 | 111 | 0 |
| test-bcos-ledger | 234 | 234 | 0 |
| test-bcos-rpc | 313 | 313 | 0 |
| test-bcos-tars-protocol | 136 | 136 | 0 |

`test-bcos-engine` 的 313 = 294（基线）+ 2（M1）+ 15（M4）+ 2（`OpEngineImportPlaneTest`，见 Task 9b）。
也就是说本次工作共增加 19 例，全部入库。

## 2. 与计划的偏离（都是实测驱动，逐条给理由）

1. **夹具命名空间**：计划写 `op_engine_test`，实现放进**既有** `op_engine_parity_test`
   （仍在 `OpEngineKarstTestHarness.h`）。理由：被移代码是按未限定名写的（`MLS`、`makeCryptoSuite`、
   `fixtureHeadHash`…），且每个使用者都已有 `using namespace op_engine_parity_test;`；
   新命名空间会额外引入一层跨命名空间查找，且与头里 20+ 个既有符号不一致。
2. **`also_green` 是数组**（逐个跑），不是计划里的冒号分隔串。理由：Boost `--run_test`
   不吃逗号/冒号多过滤（会 `Test setup error: no test cases matching filter`），
   只能一个过滤器一次进程。计划原本的 `also_green` 示例（N1 用 S3+S4）也与实际不符——
   S4 末步是**前推** FCU，在 N1 下同样会红。我用的是"分支不同"的负控制：
   N1→S3（switch 分支）、N2→S1（纯前推）、NEW-3→S3（cache-less 同场景）。
3. **`git apply --3way` 隐含 `--index`**：因此还原必须 `git checkout HEAD -- <path>`。
   第一轮我用 `git checkout -- <path>`，工作树是从**被污染的 index** 还原的 → 变体逐轮累积，
   于是出现假的 `NOT ATTRIBUTED` 和"跑完文件仍被修改"。已修，重跑后三个变体全部正确归因。
4. **M1 的运行期 −38005 驱动没有重复实现**：计划允许的兜底条款成立——该格子已由
   `OpEngineApiVersionsTest::NewPayloadWrongVersionIsUnsupportedFork`（V4@Canyon、V2@Ecotone）、
   `OpEngineServiceParityTest`（Jovian 上 getPayload V5）、`EngineServiceTest.cpp:1889` 覆盖。
   我把这条交叉引用写进了 M1 用例注释。
5. **计划里 S3/S13 的期望缺 `allowRewind = true`**：同高切换会把 tip 从 3 退到 2，
   I6 会因此判红。这是计划的笔误（计划自己在 Task 7b 的注意里也说要给显式回退场景置
   `allowRewind`，只是 S3 的示例代码漏了）。已补，并给 S14 也置上。
6. **验收计数改用「执行数」`Running N test cases`**：
   - Boost 把 `--list_content` 输出写到 **stderr**，计划里的 `2>/dev/null` 会得到 0 例（我第一轮就踩了）；
   - `--list_content` 对 `test-bcos-tool` **不完整**（4 个 suite 的用例根本不列，
     实测 4 空格缩进只有 107 行，而二进制自称 111 例）。
   决定性证据：`git diff --stat 4e0d2c893..HEAD -- bcos-tool/` 为空——本次会话根本没碰 bcos-tool，
   107 是量法差异不是回归；用执行数复核，bcos-tool = 111 = 基线。
7. **NEW-3 逆向 patch 的写法**：`storage2::merge(m_globalStateStorage.m_latestBackend, stagedDelta)`。
   `m_latestBackend` 是 public（`MultiLayerStorage.h:496`，`mergeIntoBackends` 在
   `:616/:628` 用的正是它），所以"只写 backend、绕过 cache"可以在类外精确表达；
   而 `mergeToBackends` 走的是 backend+cache 两路。
8. **S15 随机序列不生成"把 safe/finalized 推到 tip"这一类操作**：其后随之而来的 prune 语义
   正是 F4 的 Tier-2 延期项，而 fixture 内部的 `BOOST_REQUIRE` 会让整条 suite 中止而不是让单个
   用例失败。S15 只生成 扩展 / tip 心跳 / 回退到更早 canonical 高度（不改 tip） 三类操作，
   每步断言 I1/I3。注释里写明了这个取舍。
9. **Task 7a 附带 5 处名字/头文件修正**：被移代码原先依赖 `.cpp` 的文件级 `using`
   （`decodeDepositEnvelope` 在 `bcos::executor_v1::opstack`、`encodeDepositEnvelope` 在
   `bcos::evm::opstack`、`Error`→`bcos::Error`、以及 `BlockHeaderImpl.h`），移入头后必须显式限定/包含。

## 3. 仍然阻塞在授权上的事

1. **推送语料分支并开 PR**（remote `FISCO-BCOS/op-stack-e2e-tests`）。语料侧 6 笔提交都在本地
   `feat/test-matrix-oracles`。
2. **合并后 bump FISCO `.github/workflows/workflow.yml` 三处 SHA**（`:144` checkout `ref`、
   `:157`/`:401` action SHA）——**不能在语料 PR 合并前先改**（会指向不存在的提交），
   所以 Task 9 的 Step 2/3 按计划保持在"待 bump 清单"状态。
3. **推送 FISCO 的 6 笔提交**。

（原先第 4 项「scratch harness 去留」已按你的决定处理：见 Task 9b。）

## 4. 复现命令

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550

# 语料侧：一条命令重建全部工件 + 契约（复用本机 pin 树）
cd ~/.cache/fisco-t8n-corpus
OPGETH=/Users/octopus/octo/code/blockchain-impl/op-geth \
OP_NODE_REPO=/Users/octopus/octo/code/optimism \
  bash opstack-executor/tests/t8n/generator/ensure-vectors.sh     # rc=0

# FISCO 侧
cd /Users/octopus/octo/code/FISCO-BCOS/.worktrees/karst-on-5550
cmake -B build -S . && ninja -C build test-bcos-engine
build/engine/test/test-bcos-engine --run_test=OpEngineApiMatrixSuite --log_level=message   # -> matrix cells checked: 26
build/engine/test/test-bcos-engine --run_test=OpEngineSequenceMatrixSuite                 # -> 15 cases green
OPSEQUENCE_MATRIX_STEPS=2000 OPSEQUENCE_MATRIX_SEED=2 \
  build/engine/test/test-bcos-engine --run_test=OpEngineSequenceMatrixSuite/S15_RandomizedSequences
bash tools/mutation/run.sh N1 N2 NEW-3                                                    # rc=0
```
