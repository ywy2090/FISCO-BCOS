# M-T：op-geth 差分 gate 架设到 `bcos-evm/opstack/`

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给 `bcos-evm/opstack/`（4,485 行、目前只有 6 份**人工** parity 审计文档守护）架设**机器可验**的 op-geth 差分 gate：离线生成向量入库，CI 纯 C++ 回放，任何分歧即 finding。这是 spec §7.0 认定的"唯一能为 evmone 替换论证提供*正确性*证据的实验"——若 gate 打出 opstack 的真实缺陷，替换论证才第一次获得正确性依据；若全绿，则以机器强度确认 opstack 的 op-geth 等价性。

**关键事实修正（本 plan 与 spec rev.3 原设想的偏差，实地核查所得）：**
op-geth v1.101702.2 的 `evm t8n` **不支持 OP**——fork 注册表（`tests/init.go`）只含 L1 fork（…Prague/Osaka/Verkle，无 Ecotone/Isthmus/Jovian）；`t8ntool` 无 0x7E deposit 处理（唯一 "Deposit" 命中是 EIP-6110 的 L1 requests 解析，无关）；`loadTransactions` 无法解析 deposit tx。optimism monorepo 本地检出亦无现成 OP 版 t8n。**故生成器自建**：一个 ~300 行的 Go 程序，把 op-geth **当库用**，执行循环照抄它自己的 `cmd/evm/internal/t8ntool/execution.go`，仅替换两处——① chainConfig 构造（手工装配含 OP 字段的 `params.ChainConfig`，全部 fork time 置 0 或按向量指定）② tx 解析（支持 deposit 字段）。生成器源码**提交进本仓库**保证可追溯，构建时拷入 op-geth 检出编译（离线一次性，CI 不需要 Go）。

**Architecture:**
```
[离线，一次性]                                    [CI，每次]
bcos-evm/test/opstack/t8n/generator/*.go          bcos-evm/test/opstack/T8nVectorReplayTest.cpp
  --拷入--> blockchain-impl/op-geth/cmd/opt8n/      (Boost.Test, 独立可执行, OpStackTests.cmake)
  --build & run--> vectors/*.json (提交入库) -----> 加载向量 → InMemoryStateView 播种 pre
                                                    → 逐 tx applyOpStackMessage
                                                    → 对照 post alloc + receipts
```

**Tech Stack:** Go（仅离线生成器，import op-geth v1.101702.2 as library）· C++20 + Boost.Test（回放器，模式照 `bcos-evm/test/opstack/DepositMintTest.cpp`：`applyOpStackMessage` + `helpers/InMemoryStateView.h` + `task::syncWait`）· 向量 = 自描述 JSON

**授权依据：** spec rev.7 决策 D3（用户授权改动 `bcos-evm`）+ D4（本 plan 先行）。三持久原则：不涉 FISCO；不重写 evmone 已有之物（本 plan 不碰 evmone）；plan 先行。

## 预注册的 gate 纪律（先于任何向量生成写死）

1. **分歧即 finding，不许静默 skip**：回放失败的向量进入 `vectors/DIVERGENCES.md`，每条必须三选一归因——(a) `bcos-evm/opstack` 缺陷（开 issue）/(b) 生成器缺陷（修生成器重生成）/(c) 已知可接受差异（**须用户逐条签核**才能入 allowlist）。gate 断言 = "全部向量通过 ∨ 在已签核 allowlist 中"。
2. **期望值只能来自生成器输出**，不许手工调整任何 expected 字段——手改期望值 = Phase 1 式挪门柱，一律视为缺陷。
3. 生成器与向量**同 commit 入库**；重生成必须整批（防止新旧生成器输出混排）。

## 向量 JSON 格式（v1，字段级定案——这正是 rev.3 终审要求的"M4 第一交付物"，移交至此）

```json
{
  "meta": { "generator": "opt8n@<op-geth-commit>", "fork": "isthmus|jovian", "description": "..." },
  "env": {
    "coinbase": "0x4200...0011", "number": "0x2", "timestamp": "0x64",
    "gasLimit": "0x1c9c380", "baseFee": "0x7", "prevRandao": "0x...",
    "parentBeaconBlockRoot": "0x..."
  },
  "pre":  { "<address>": { "balance": "0x..", "nonce": "0x..", "code": "0x..", "storage": { "<slot>": "<value>" } } },
  "txs": [
    { "type": "0x7e", "sourceHash": "0x..", "from": "0x..", "to": "0x..|null",
      "mint": "0x..", "value": "0x..", "gas": "0x..", "isSystemTx": false, "input": "0x.." },
    { "raw": "0x02f8..." }
  ],
  "expected": {
    "post": { "<address>": { "balance": "0x..", "nonce": "0x..", "code": "0x..", "storage": { } } },
    "receipts": [ { "status": 1, "gasUsed": "0x..", "logsCount": 2, "logsBloom": "0x..",
                    "depositNonce": "0x..", "depositReceiptVersion": 1 } ],
    "blockGasUsed": "0x.."
  }
}
```
普通 tx 一律 `raw`（签名后 envelope，与 L1 cost 计算的输入一致）；deposit 用字段形式（无签名）。`post` 只列**被触碰**的账户（生成器 dump 后按触碰集过滤），storage 只列非零槽。

## 向量矩阵（目标 ~50 条，两个 fork：Isthmus 与 Jovian——opstack 已实现到 Jovian）

| 类别 | 条数 | 覆盖点 |
|---|---|---|
| L1 attributes deposit（首笔） | 4 | 两 fork × {`test/fixtures/opstack/{isthmus,jovian}_l1_attributes.bin` 的真实 calldata}；receipt 的 gasUsed/depositNonce |
| 用户 deposit | 8 | mint≠value、成功、EVM revert（gasUsed=实际）、处理级失败（gasUsed=gasLimit、nonce 仍 bump）、to=null 创建、mint=0 |
| 普通转账 + L1 fee | 6 | L1Block 预置槽（两 fork 布局）、fee 三/四 vault 入账、sender 守恒式 |
| **fee 环境观测合约** | 12 | 执行中读 `CHAINID`/`GASPRICE`/`COINBASE`/`BASEFEE`/vault 余额并 SSTORE——正是手写向量永远想不到、t8n 才能对出的那类 |
| EIP-7702 × OP | 6 | 授权 + 预扣交互、delegated sender |
| operator fee（Isthmus/Jovian 公式差异） | 8 | 三段式退差额、gasLimit vs gasUsed 计价、Jovian 的 `gas*scalar*100+constant` |
| 边界 | 6 | 余额恰好差 1 wei 付不起 l1Cost、blob tx 拒绝、intrinsic 不足、7623 floor 抬 gasUsed |

## Global Constraints

- 仓库根（worktree）：`/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor`；op-geth 检出：`/Users/octopus/octo/code/blockchain-impl/op-geth`（tag v1.101702.2，与 `bcos-evm/test/eth-eest-test/assets/upstream-pins.json` 的 pin 一致——生成器 meta 记录其确切 commit）。
- **CI 不需要 Go**：回放器纯 C++；生成器只在向量重生成时离线运行。
- **不碰 `bcos-evm-ref`**；不碰 evmone；FISCO 不考虑。
- 回放器走 **`applyOpStackMessage` 入口**（`bcos-evm/opstack/apply/ApplyOpStackMessage.h`，`task::syncWait` 驱动）——与既有 opstack 测试同一入口，模式照抄 `DepositMintTest.cpp`（同仓先例，:4-10 include 集、:97 调用形态）。
- CMake：照 `OpStackTests.cmake` 既有模式（独立 `add_executable` + `target_link_libraries(... bcos-evm-op ...)` + `add_test`）。
- pre-commit clang-format 照旧；Go 代码 `gofmt`。

## File Structure

```
bcos-evm/test/opstack/t8n/generator/main.go        # 生成器（源码入库；拷入 op-geth cmd/opt8n 编译）
bcos-evm/test/opstack/t8n/generator/README.md      # 构建/重生成手顺 + op-geth commit pin
bcos-evm/test/opstack/t8n/vectors/*.json           # 向量（生成器输出原样入库）
bcos-evm/test/opstack/t8n/vectors/DIVERGENCES.md   # 分歧台账（归因 + 签核记录）
bcos-evm/test/opstack/T8nVectorReplayTest.cpp      # 回放器
bcos-evm/test/cmake/OpStackTests.cmake             # 追加独立目标
```

---

### Task 1: 生成器骨架 + 首条向量（纯转账，Isthmus）

**Files:**
- Create: `bcos-evm/test/opstack/t8n/generator/main.go`、`generator/README.md`
- Create: `bcos-evm/test/opstack/t8n/vectors/isthmus_transfer_basic.json`（生成器输出）

**Interfaces:**
- Produces: 生成器 CLI `opt8n --input <case.json> --output <vector.json>`（input = 无 `expected` 的向量；生成器补全 `expected` 段）。

- [ ] **Step 1: 生成器实现**。执行循环**照抄** op-geth `cmd/evm/internal/t8ntool/execution.go` 的 `(*Prestate).Apply`（StateDB 构造、逐 tx `core.ApplyTransaction`、receipt 收集、最终 `statedb.Commit`+dump），仅两处替换：
  1. **chainConfig**：不走 `tests.GetChainConfig`（无 OP fork），手工装配——以 `params.OptimismTestConfig`（若存在；否则复制 `params.ChainConfig` 全字段并置 `Optimism: &params.OptimismConfig{EIP1559Elasticity:6, EIP1559Denominator:50, ...}`）为底，L1 fork time 全 0，OP fork time 按 `--fork` 参数置 0/nil（`isthmus`: RegolithTime…IsthmusTime=0，JovianTime=nil；`jovian`: 全 0）。**执行者第一步先 `grep -rn "IsthmusTime\|OptimismConfig" params/` 确认字段名并在 README 记录装配代码**；
  2. **tx 解析**：`txs[]` 里 `raw` 走 `tx.UnmarshalBinary`；deposit 字段走手工构造 `types.DepositTx{SourceHash, From, To, Mint, Value, Gas, IsSystemTransaction, Data}` → `types.NewTx`。
  Receipt 侧额外导出 `DepositNonce`/`DepositReceiptVersion`（op-geth receipt 已有字段）。
- [ ] **Step 2: 构建与首跑**：
```bash
cp -r bcos-evm/test/opstack/t8n/generator /Users/octopus/octo/code/blockchain-impl/op-geth/cmd/opt8n
cd /Users/octopus/octo/code/blockchain-impl/op-geth && go build ./cmd/opt8n && cd -
# 输入：两个 EOA、一笔 raw 签名转账、L1Block 预置槽（Isthmus 布局，slot1/3/7/8）
./blockchain-impl/op-geth/opt8n --fork isthmus --input transfer_basic.in.json \
    --output bcos-evm/test/opstack/t8n/vectors/isthmus_transfer_basic.json
```
Expected: 输出向量含非空 `expected.post`（sender 扣 gas+L1 fee、receiver 加值、SequencerFeeVault/BaseFeeVault/L1FeeVault 入账）与 receipt。**人工 sanity check 一次**（fee 守恒式手算核对），此后不再人审。
- [ ] **Step 3: Commit**（生成器源 + README + 首向量；信息 `test(bcos-evm): opt8n generator skeleton + first Isthmus transfer vector`）

---

### Task 2: 回放器骨架 + 首向量绿

**Files:**
- Create: `bcos-evm/test/opstack/T8nVectorReplayTest.cpp`
- Modify: `bcos-evm/test/cmake/OpStackTests.cmake`

**Interfaces:**
- Consumes: `applyOpStackMessage`（`bcos-evm/opstack/apply/ApplyOpStackMessage.h`）、`helpers/InMemoryStateView.h`、`OpStackLifecycleTestHelpers.h`（区块级编排：L1 attributes、gas pool——照 `DepositMintTest.cpp` 与 lifecycle helpers 的既有用法）。
- Produces: Boost.Test 目标 `OpStackT8nVectorReplayTest`；向量目录遍历 + 逐向量 `BOOST_TEST_CONTEXT`。

- [ ] **Step 1: 回放器**：boost::json（或仓库既定 JSON 库——看 `bcos-evm/test` 现用哪个，照抄）解析向量 → `InMemoryStateView` 播种 `pre`（账户 + 槽）→ 逐 tx 构造 `OpStackMessageInput`（raw tx 走既有解码路径；deposit 走字段）→ `task::syncWait(applyOpStackMessage(...))` → 收集 diff/receipt →与 `expected` 逐字段 `BOOST_CHECK_EQUAL`（post 只比向量列出的账户/槽；receipt 比 status/gasUsed/logsCount/depositNonce/Version）。分歧输出统一格式：`DIVERGE <vector> <field> want=<> got=<>`。
- [ ] **Step 2: CMake 注册**（独立目标，链 `bcos-evm-op` 等，照 `OpStackTests.cmake` 首个目标的模式）+ 跑首向量到绿。
- [ ] **Step 3: Commit**（`test(bcos-evm): t8n vector replayer green on first vector`）

---

### Task 3: 向量波次一——deposit 全路径 + L1 attributes（~18 条）

- [ ] 生成矩阵前两类 + 普通转账类；回放；**分歧全部进 `DIVERGENCES.md` 归因**（预期这里最可能打出真 finding：deposit 失败双路径的 gasUsed、depositNonce 语义）。归因为 (a) 的开 issue 并在台账链接；不修 bcos-evm（gate 只报告，修复另立 plan——保持 gate 与修复解耦）。
- [ ] Commit（向量 + 台账）。

### Task 4: 向量波次二——fee 观测合约 + 7702 + operator fee + 边界（~32 条）

- [ ] 观测合约字节码用手写 asm（CHAINID/GASPRICE/BASEFEE/BALANCE(vault) 各 SSTORE 到固定槽），生成 → 回放 → 归因。**这一波是 gate 的灵魂**——机器对出人审永远漏的观测值类分歧。
- [ ] Commit。

### Task 5: CI 接线 + 报告

- [ ] `add_test` 已含；确认进默认 ctest 标签（与其他 opstack 测试同层）；README 记录重生成手顺与 allowlist 签核流程。
- [ ] **产出《opstack op-geth 机器差分报告》**：向量通过率、DIVERGENCES 台账终态、对 spec §7.2 的意义（若 0 分歧：opstack 的 op-geth 等价性首次获得机器强度确认，替换论证的"正确性缺口"论据正式关闭；若有分歧：列出，它们就是替换论证等待的正确性证据）。spec §7.1 M-T 行标记完成 + 结论。
- [ ] Commit。

---

## 风险与缓解

| 风险 | 缓解 |
|---|---|
| 生成器 chainConfig 与 opstack 的 fork 语义对不齐（最大风险） | Task 1 Step 1 先 grep 确认 `params/` 字段并把装配代码写进 README；首向量人工 fee 守恒核对一次 |
| 生成器自身 bug 污染期望值 | 执行循环照抄 op-geth 自己的 `execution.go`（它被 geth 全家 CI 守护）；只改两处且都有上游模式可抄 |
| op-geth 检出被污染 | 只**新增** `cmd/opt8n`（拷入，不改任何现有文件）；源码真身在 FISCO 仓库 |
| 期望值被手调（挪门柱） | 预注册纪律第 2 条；review 时 diff 向量文件必须整批来自生成器运行 |
| InMemoryStateView 与 op-geth StateDB 的语义缝隙（如空账户处理） | 分歧台账机制本身就是探测器；归因 (b)/(c) 路径兜住 |
| L1Block 槽布局两 fork 差异写错 | pre 里的槽值由生成器输入侧与回放侧共用同一 JSON，错也错得对称——真正的布局对错由 fee 数额分歧暴露 |

## 开口（如实声明）

- 生成器 Go 代码是"照抄 execution.go + 两处替换"的指令级描述而非全文成品——上游模板就在 pinned 检出里，两处替换各给了确切函数/类型名；执行者需按 `params/` 实际字段完成装配并记录。
- 向量矩阵条数（50）与两 fork 范围是提案值；用户可在 Task 3 前调整。
