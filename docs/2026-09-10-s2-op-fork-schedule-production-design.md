# S2 生产 OP fork-schedule codec 放开历史 EL fork 名设计

- **日期**：2026-09-10
- **状态**：已确认（scope / 名字集 / 严格连续三项均锁定）
- **工作项**：S2「生产 codec 放开历史 fork 名 + `c_maxOpForkActivations` 评估」
- **实现树（唯一改代码处）**：`.worktrees/karst-on-5550` @ `feat/karst-on-release-3.18`
- **前序**：[S3 Engine API 版本门设计](2026-09-09-s3-engine-api-versions-design.md)、[会话交接](2026-09-10-session-handoff-s3.md)
- **规范权威**：
  - op-node fork 顺序与命名：`/Users/octopus/octo/code/optimism/op-node/rollup/types.go`（`forEachFork`、`checkFork`）
  - OP protocol specs：`/Users/octopus/octo/code/ethereum-optimism-specs/specs/protocol/`
  - 本仓既有约束：`docs/plans/2026-09-08-karst-on-5550.md`（K1–K3 codec）

---

## 1. 背景与现状

产品目标：从 **Bedrock** 起把 FISCO-BCOS opstack 执行层做成 Optimism 验证者 EL。要接官方 OP Mainnet / Base / Sepolia，生产必须能加载含历史 fork 的 fork schedule。

现状（`.worktrees/karst-on-5550`）：

- `bcos-framework/.../ledger/OpForkScheduleCodec.h` 是 **Isthmus+ 收窄版**：
  - `detail::forkOrder`（`:65-74`）只认 `isthmus(4)` / `jovian(5)` / `karst(6)`，其它返回 `-1` → `unknown or pre-Isthmus fork`（`:141`）。
  - `detail::isAllowedBaseline`（`:76-79`）只允许 `isthmus|jovian`。
  - `validateScheduleRecords`（`:119-168`）要求 baseline `ts==0`，另有一条特例「Karst 需要 Jovian」。
  - `c_maxOpForkActivations = 8`（`:185`）。
- `bcos-evm/.../opstack/OpForkSchedule.cpp`：
  - `forkFromName`（`:13-22`）只映射 isthmus/jovian/karst。
  - `forkNameFromEnum`（`:50-63`）对 pre-Isthmus 抛 `unknown or pre-Isthmus fork`。
- 生产加载链已存在，但只能喂 Isthmus+ schedule：
  - `bcos-tool/.../NodeConfig.cpp:1265` `loadOpForkSchedule` 读 `[op_fork_schedule].canonical` → `parseOpForkSchedule` → `canonicalOpForkSchedule`。
  - `libinitializer/Initializer.cpp:536-548` 读 metadata/genesis → `resolveOpForkScheduleCanonical` → `OpForkSchedule::parse(canonical)`。
- 现行 canonical 形态（`tools/opstack-genesis/chain-config.template.yaml:15-19`）：`0:jovian,1781712001:karst`、`0:isthmus,1764691201:jovian,1783526401:karst`。**baseline `ts=0` + 后续绝对 Unix 秒**。
- 执行配置齐全：`OpFork` 枚举（Regolith…Karst）与 `configForFork`（`OpForkSchedule.cpp:24-48,92-233`）已存在，S2 不改执行语义。

**Engine 解析/版本表不在 S2**：`OpForkId` 历史档、`tryEngineForkId`、`engineApiFor`、`resolveEngineForkAt`、`OpExtraDataLayout`、`calcOpNextBlockBaseFee`、`isNewPayloadVersionSupported` V2–V4、caps 均属 [S3](2026-09-09-s3-engine-api-versions-design.md)（其 §4.4 / plan Task 1–8）。截至本设计，S3 只有设计+计划，**代码未落地**（`OpForkId.h` 仍 `Isthmus/Jovian/Karst`，`tryEngineForkId` 历史档仍 `nullopt`）。

---

## 2. 目标 / 非目标

**目标**

- 生产加载链 `[op_fork_schedule].canonical` → codec → `OpForkSchedule::parse` → `configAt` 能接受含 **Regolith…Karst** 的 schedule。
- 名字表变成**单一真相源**，消除 `forkOrder` / `forkFromName` / `forkNameFromEnum` 三处重复。
- 校验规则「严格连续」，把现有 Karst/Jovian 特例泛化为通用不变式。
- 评估并调整 activation 上限。

**非目标**

- Engine 解析 / 版本表 / extraData / baseFee / caps（S3）。
- 执行语义 / L1 fee / 收据（S4）。
- 官方 genesis / superchain registry 接入（S1）。
- `delta`（无 EL 语义）、`interop` / `pectra_blob_schedule`（实验/CL）。
- 创世 extraData 例外（S1）。

---

## 3. 决策日志

| 决定 | 备选 | 为何 |
| --- | --- | --- |
| S2 = 仅「生产加载层」 | 一路到 Engine 解析 | S3 spec/plan 已把 `OpForkId`/解析/版本表划归 S3；避免两边改 `OpSchedulerSeam.h` |
| 名字集 = 9 档 EL fork | 加 delta 占位；或全量 op-node 名字 | 与 `OpFork` 枚举 1:1；delta 无 EL 语义（S4 已定），interop 不在范围 |
| 单一权威有序表 | 两张 switch + 一致性测试 | S2 本质是扩名字表，一处增删最省事 |
| 严格连续校验 | 宽松（仅递增/去重） | 对齐 S4「中间档不得跳过」+ op-node `checkFork` 单调；防静默归错 fork |
| baseline 可为任一 EL 档（仍 `ts==0`） | 保持 `isthmus\|jovian` | 链可从任一 fork 起；`0:isthmus` / `0:jovian` 仍合法 |
| 上限 8 → 16 | 保持 8；或 9 | 9 档至少 9；留 Interop/后继余量，仍远小于 512 字节上限 |
| timestamp-0 baseline 不变 | 允许绝对 baseline | 现行 canonical 就是 `0:<fork>` + 绝对秒；不改数据形态 |

---

## 4. 名字表与单一真相源

在 `OpForkScheduleCodec.h` 增加**协议顺序**的权威表：

```cpp
namespace detail
{
// 协议顺序即数组下标；OpFork 枚举顺序必须与此一致（测试锁死）。
inline constexpr std::array<std::string_view, 9> c_opForkNames = {
    "regolith", "canyon", "ecotone", "fjord", "granite",
    "holocene", "isthmus", "jovian", "karst",
};

// 返回协议顺序索引；未知名字返回 -1（沿用现语义）。
inline constexpr int forkOrder(std::string_view forkName)
{
    for (std::size_t i = 0; i < c_opForkNames.size(); ++i)
        if (c_opForkNames[i] == forkName)
            return static_cast<int>(i);
    return -1;
}

inline constexpr bool isAllowedBaseline(std::string_view forkName)
{
    return forkOrder(forkName) >= 0;
}
}  // namespace detail
```

- 表放在 `namespace detail`，紧邻 `forkOrder`（`OpForkSchedule.cpp` 已用 `ledger::detail::validateScheduleRecords`，同层访问一致）；**只此一处**；codec 需补 `#include <array>`。
- 名字比对沿用 `normalizeForkName`（trim + 转小写，`:108-117`）。
- `parseOpForkSchedule` / `canonicalOpForkSchedule` / `keccakOpForkScheduleHash` 签名不变。

`bcos-evm/.../opstack/OpForkSchedule.cpp` 改为复用该表：

```cpp
OpFork forkFromName(std::string_view n)
{
    const int i = ledger::detail::forkOrder(n);
    if (i < 0) ledger::throwInvalidOpForkSchedule("unknown fork");
    return static_cast<OpFork>(i);
}

std::string forkNameFromEnum(OpFork f)
{
    const auto i = static_cast<std::size_t>(f);
    if (i >= ledger::detail::c_opForkNames.size())
        ledger::throwInvalidOpForkSchedule("unknown fork");
    return std::string(ledger::detail::c_opForkNames[i]);
}
```

- 依赖方向合法：`OpForkSchedule.cpp` 已 `#include .../OpForkScheduleCodec.h`（`:3`）。
- 用测试断言 `static_cast<std::size_t>(OpFork::Karst) == c_opForkNames.size() - 1` 且逐项名字匹配，锁死两枚举顺序一致。若将来在 `OpFork` 中间插 fork，测试立即红。
- **同步更新陈旧注释**（否则与代码矛盾）：`OpForkScheduleCodec.h:37-38`「baseline remains isthmus\|jovian」、`OpForkSchedule.h:14-18,40,101-103`「production parse ... Isthmus+-only」「Karst requires Jovian first」。

---

## 5. 校验规则（严格连续）

`detail::validateScheduleRecords`（`OpForkScheduleCodec.h:119-168`）改为：

1. **非空**：`activations.empty()` → `empty schedule`。
2. **baseline**：`front().timestamp == 0` → 否则 `missing timestamp-0 baseline`；`isAllowedBaseline(front().forkName)` → 否则 `invalid baseline fork`（baseline 可为任一已知档）。
3. **逐条**（baseline 也查名字/顺序/重复；连续比较从第 2 条起）：
   - `forkOrder(name) < 0` → `unknown or pre-Isthmus fork`。
   - `timestamp < previousTimestamp` → `timestamps out of order`。
   - `timestamp == previousTimestamp` 且非首条 → `duplicate timestamp`。
   - **严格连续**：仅对**第 2 条起**的激活检查 `order != previousOrder + 1` → `forks out of protocol order`；baseline 是锚点，不参与「前一条 +1」检查（实现上按索引循环，索引 0 只设锚点，索引 ≥1 才比较）。连续规则使 `order` 严格递增，**重复 fork 不可能出现**，故删除原 `duplicate fork` 扫描（不再维护 `seenForks`）。
4. **删除**特例 `if (hasKarst && baseline == "isthmus" && !hasJovian) throw`——由通用连续规则取代（`0:isthmus,…,karst` 缺 jovian 时第 2 条 `order==8 != 6+1` → 报错）。

兼容性核对：

| 输入 | 结果 |
| --- | --- |
| `0:isthmus` | 合法（baseline=isthmus） |
| `0:jovian` | 合法 |
| `0:isthmus,1764691201:jovian` | 合法 |
| `0:isthmus,1764691201:jovian,1783526401:karst` | 合法 |
| `0:regolith,<c>:canyon,<e>:ecotone,<f>:fjord,<g>:granite,<h>:holocene,<i>:isthmus,<j>:jovian,<k>:karst` | 合法（官方形态） |
| `0:isthmus,1783526401:karst` | 拒（跳档：6→8） |
| `0:jovian,1:isthmus` | 拒（乱序） |
| `0:ecotone` | **由拒变合法**（baseline=ecotone） |
| `0:karst` | **由拒变合法**（baseline=karst） |
| `0:regolith,<e>:ecotone`（跳 canyon） | 拒 |

---

## 6. 上限

- `c_maxOpForkActivations`：**8 → 16**（`OpForkScheduleCodec.h:185`）。
  - 9 档 EL fork 严格连续时最多 9 条 → 8 会拒官方 schedule，必须放大。
  - 16 = 9 档 + 余量（Interop / 后继 fork），仍满足 `c_maxOpForkScheduleBytes = 512`（9 条 ≈ 110 字节）。
- `c_maxOpForkScheduleBytes` 512 不变。

---

## 7. 不改 / 依赖 / 顺序

**不改**

- `bcos-framework/.../engine/OpForkId.h`、`opstack-executor/OpSchedulerSeam.h`、`engine/bcos-engine/OpEngineService.{h,inl,cpp}`、`bcos-framework/.../engine/OpBaseFee.h`（S3）。
- `configForFork` / EVM 语义 / L1 fee（S4）。
- ImportedStore / SetCanonical（S5+S6）。

**依赖与顺序**

- S2 只保证「能加载 + `configAt` 正确」。含历史档的 schedule 在 Engine `resolveEngineForkAt` 仍会 `UnsupportedTimestamp`，直到 S3 落地。
- 生产官方链 = S2（加载）+ S3（开门）+ S4（历史执行）+ S5+S6（导入）。S2 单独不能让节点同步 Mainnet。
- 与 S3 无文件冲突（S2 只碰 `OpForkScheduleCodec.h` + `OpForkSchedule.{h,cpp}` 及其测试；S3 不碰这些）。可与 S3 并行。

---

## 8. 数据流

```
config.genesis [op_fork_schedule].canonical
  → NodeConfig::loadOpForkSchedule            (bcos-tool, 不改)
    → parseOpForkSchedule / canonicalOpForkSchedule   (codec, S2 扩名/严格连续)
      → 存入 s_chain_metadata (op_fork_schedule / _hash / _genesis)
Initializer::init                              (libinitializer, 不改)
  → resolveOpForkScheduleCanonical (metadata → genesis → legacy fallback)
    → OpForkSchedule::parse                    (S2 扩 forkFromName)
      → OpForkSchedule ctor → validateActivations → validateScheduleRecords
      → configAt(ts) → configForFork           (不改)
```

---

## 9. 错误

- codec 抛 `ledger::InvalidOpForkSchedule`（`:48`），经 `throwInvalidOpForkSchedule` 统一带 `errinfo_comment`。
- `NodeConfig::loadOpForkSchedule`（`NodeConfig.cpp:1286-1291`）catch → `InvalidConfig`。
- `Initializer.cpp:555` catch `InvalidOpForkSchedule` → `InvalidConfig`。
- S2 不新增错误类型、不改错误码。

---

## 10. 测试矩阵

**新增 / 扩 `OpForkScheduleCodecSuite`（`bcos-evm/test/opstack/OpForkScheduleCodecTest.cpp`，target `bcos-evm-opstack-tests`）**

- 9 名字逐个 `0:<name>` 合法（baseline 任一档）。
- 官方全链 `0:regolith,…:karst` 合法，`size==9`。
- 大小写 / trim 规范化（`0: Canyon `）。
- 严格连续负例：跳档（`0:regolith,<e>:ecotone`）、乱序、重复 ts、缺 `ts=0`、trailing comma、timestamp overflow（重复 fork 由连续规则先拦，不再单测）。
- 上限：9 条合法；>16 条 → `too many activations`。
- 名字↔枚举一致性：`c_opForkNames` 逐项 == `OpFork` 顺序（放 `OpForkScheduleSuite` 或本 suite）。

**必须改写的现有测试（S2 会翻转其断言）**

| 文件 | 用例 | 现状 | S2 后 |
| --- | --- | --- | --- |
| `OpForkScheduleCodecTest.cpp:27` | `RejectsKarstWithoutJovian` | 拒 `0:karst`（invalid baseline）；`0:isthmus,<t>:karst` 报 "Jovian …" | `0:karst` 合法；改成跳档负例、断言新文案 |
| `:52` | `RejectsTooManyActivations` | 9 段→too many | 阈值改 16；构造 >16 段 |
| `:64` | `RejectsEmptyMissingBaselineAndOrder` | `0:ecotone` 拒 | `0:ecotone` 合法，负例换成未知名 |
| `OpForkScheduleTest.cpp:128` | `ParseStillRejectsRegolithName` | `parse` 拒 regolith | 改为 `ParseAcceptsRegolithBaseline`（成功 + `configAt`==regolithConfig） |
| `NodeConfigOpForkScheduleTest.cpp:42` | `rejectsKarstWithoutJovian` | 断言 errinfo 含 "Jovian" | `0:isthmus,1:karst` 仍拒，但原因是连续（文案 "forks out of protocol order"）；改断言/改名 |

**仍过但语义已变（无需改断言，建议更新注释）**：`OpForkScheduleTest.cpp:199` 的 `parse("0:isthmus,1:karst")` 仍抛（跳档），只是原因由「Karst 需 Jovian」变为「严格连续」。

**加载链**

- `bcos-ledger/test/unittests/ledger/test_OpForkScheduleMetadata.cpp`：官方式 canonical 的 metadata 往返 / hash / genesis 绑定。
- `bcos-tool/test/unittests/libtool/NodeConfigOpForkScheduleTest.cpp`：`[op_fork_schedule].canonical=0:regolith,…:karst` 可加载；legacy `0:isthmus`/`0:jovian` 不回归。
- `OpForkScheduleSuite`（`bcos-evm/test/opstack/OpForkScheduleTest.cpp`）：`forkFromName`/`forkNameFromEnum` 全档往返；`jovianAndLaterActivations` 不变。

**回归**：现有 `0:isthmus,1764691201:jovian,1783526401:karst` 及 legacy 行为逐字节不变。

---

## 11. 风险

- **翻转既有测试**：上表 5 处是 S2 的预期改动，不是回归；改测试时必须保留负例覆盖，禁止只删断言。
- **严格连续可能拒合法 chain**：若某链的真实 schedule 缺中间档，会被拒。已确认 9 档 EL fork 各有 EL 变更（S4），无合法跳档；若 S1/S7 发现例外，回来改本设计决策日志。
- **S2 先于 S3 落地**：含历史档的生产 schedule 会通过加载但在 Engine 得 `UnsupportedTimestamp`，非 bug；文档与 commit message 需写明顺序依赖。
- **枚举顺序漂移**：靠 `OpFork`↔`c_opForkNames` 一致性测试兜底，禁止在 `OpFork` 中间插档而不改表。
- **S1 生成 canonical 不得带 `delta`/`interop`**：op-node/superchain 的 `*_time` 键含 `delta_time`（见 `tools/opstack-genesis/gen_rollup_config.py:36-45`），但 EL 无 delta 档；FISCO canonical 必须只发 9 档 EL fork，否则 codec 以 `unknown` 拒。若 S1 确需 delta，先回来改本设计名字集。
- **`forkNameFromEnum` 放开**：`OpForkSchedule::validateActivations` 会用它对所有档生成记录；确保与 codec 表一致（同一真相源后自然一致）。

---

## 12. 执行时权威对照（强制）

**冲突顺序：OP protocol spec → op-geth → 本设计已记录的决定 → 本仓库现码。** 每个实现 Step 动手前必须打开下表对应原文（至少 `rg` 到函数/字段名），**禁止用 FISCO 现码反推官方语义**。

| 主题 | 先打开 |
| --- | --- |
| EL fork 顺序/字段（权威） | `/Users/octopus/octo/code/op-geth/params/config_op.go`（fork timestamp 检查 `:13-40`、banner `:48-75`：Regolith→Canyon→Ecotone→Fjord→Granite→Holocene→Isthmus→Jovian→Karst→Interop，**无 Delta**）；`params/config.go:510-521` 字段名/顺序 |
| fork 顺序与命名（CL） | `/Users/octopus/octo/code/optimism/op-node/rollup/types.go`（`forEachFork` :831-846、`checkFork` :343-361） |
| fork 列表/激活 | `/Users/octopus/octo/code/ethereum-optimism-specs/specs/protocol/superchain-upgrades.md`（:250-278）、`overview.md`、各 `*/exec-engine.md` |
| 本仓既有约束 | `docs/plans/2026-09-08-karst-on-5550.md` K1–K3；`OpForkScheduleCodecTest.cpp` |

**关键结论（op-geth 佐证）**：op-geth 的 OP EL 配置字段 **不含 `DeltaTime`**（`params/config.go:510-521`、`config_op.go` fork 列表无 Delta）。这直接支持本设计「只认 9 档 EL fork、拒绝 `delta`」——`delta` 只影响 derivation，不是 EL 语义。

发现任何与 op-geth/spec 的分歧：**先停下**，写入本设计决策日志，再改代码。

---

## 13. 明确不做（S2）

- `OpForkId` / Engine 版本表 / extraData / baseFee / caps（S3）
- L1 fee / 收据 / EVM `configForFork`（S4）
- ImportedStore / SetCanonical（S5+S6）
- `delta` / `interop` / `pectra_blob_schedule` 名字
- 创世 extraData 放行（S1）
- 官方 genesis / superchain registry 接入（S1）
