# Revision 门控单一真相源 — 设计文档

**日期:** 2026-06-23
**范围:** `bcos-evm` revision/EIP 门控规则集中化(架构候选 2)
**词汇:** 取自 `codebase-design`(module / interface / implementation / depth / seam / adapter / leverage / locality)
**关联:** capability-matrix.md · ADR-004 · ADR-005 · ADR-006 · 新增 ADR-018

---

## 1. 问题陈述

"EIP → 最小 revision" 的派生规则(形如 `字段 = revision >= FORK`)在生产路径与测试基建中至少重复 5 处,且已发生漂移:

| # | 位置 | 角色 | 现状 |
| --- | --- | --- | --- |
| 1 | `eth/vm/EthPolicy.h:31-41` | 规范参考路径 | 10 个 EIP 行 |
| 2 | `bcos/FiscoPolicy.h:57-69` | FISCO 生产 | 同 10 行 + `&& features.get(flag)` + `eip1559` |
| 3 | `specs-tests/src/ForkProfileRegistry.cpp:15-25` | 测试 fork 表 | 逐字拷贝,注释 "Keep aligned with EthPolicy" |
| 4 | `eth/RevisionConfig.h:62-72` `makeIsthmusRevisionConfig` | OP Isthmus profile | **已漂移**:只设 4 个标志,缺 `eip1153/eip5656/eip6780/eip2537` 等 |
| 5 | `test/fixtures/FiscoFixtureAdapter.h` | 测试 fixture | 拷贝 Cancun 四件套 |

此外,读取侧存在绕过 `RevisionConfig` 布尔位、直接比较 `revision` 的消费者,形成"读取侧第二真相":

- `eth/state/EthHost.cpp:211,242` — `m_revisionConfig.revision >= EVMC_CANCUN`(EIP-6780 自毁语义)
- `eth/precompiled/PrecompileActive.h:46,59` — 混读 `revision >=` 与 `cfg.eip7212`

**后果(locality 失效):** "某 EIP 何时激活" 与 "如何判断某 EIP 激活" 都散落多处,随时可再漂移;Isthmus 的 config 快照与内核实际行为已不一致。

## 2. 目标与非目标

### 目标(范围 = B:派生规则 + 消费端归一)
1. 抽出唯一的派生函数 `revisionConfigFromRevision(evmc_revision)`,所有作者侧从它派生。
2. 把受控-EIP 的读取侧统一为读 `RevisionConfig` 布尔位,使接口成为唯一测试面。
3. 加等价性测试 + CI grep 闸门,使未来漂移**结构上不可能**(集中化 + 测试 + 闸门)。
4. Isthmus config 与 revision 对齐(稠密化),snapshot 诚实反映内核行为。

### 非目标(YAGNI)
- 不重构 `FiscoRevisionConfig` 的 `.eth()` 包裹(候选 3)。
- 不动编排层 / gas 结算(候选 1、4)。
- **不改任何运行时语义** — 只改"规则来源 + 读取入口 + 快照稠密度 + 防漂移"。

## 3. 架构(derive-then-mask,方案 1)

```
                eth/RevisionConfig.h  (内核 · 单一真相源)
                ┌───────────────────────────────────────────────┐
                │ revisionConfigFromRevision(evmc_revision)       │  深函数:1 入参 → 整张激活表
                │   B 类: 纯 revision 派生                          │
                │   A 类: 也按 revision 全开(规范最大集)            │
                │ REVISION_CONFIG_GATED_FIELDS(X)  宏列表           │  A 类(可被 FISCO feature 关掉)分区
                └──────────┬───────────────────┬──────────────────┘
        EthPolicy ─────────┘                   │           └────── ForkProfileRegistry
        = derive(rev)                          │                   = derive(rev)
                              FiscoPolicy: cfg.eth() = derive(rev)
                                + applyFiscoFeatureGates(cfg.eth(), features)   // 只对 A 类 &&= flag
                                + 仅设外层 FISCO 位(bugfix_*/use_*/enable_*)  // 无 EIP overlay
                                          │
                              makeIsthmus = derive(PRAGUE)   // prague_post_execution 默认 false,无 overlay
```

**seam 纪律:** `derive` 与 A/B 宏分区是内核知识(`RevisionConfig.h` 本就有 A/B/C 注释);**feature flag 身份只出现在 FISCO 层 `applyFiscoFeatureGates`**,内核不反向依赖 FISCO(守 ADR-005 Rule 1)。

## 4. 组件

| module | interface | implementation(藏在 seam 后) |
| --- | --- | --- |
| `revisionConfigFromRevision(rev)`(新,内核) | `evmc_revision → RevisionConfig` | 所有 `字段 = rev >= FORK` 规则,唯一住所 |
| `REVISION_CONFIG_GATED_FIELDS(X)`(新宏,内核) | A 类字段名清单 | 与 `REVISION_CONFIG_BOOL_FIELDS` 平行;FISCO mask 与 grep 闸门共用 |
| `applyFiscoFeatureGates(cfg, features)`(新,FISCO 层) | `(RevisionConfig&, ledger::Features const&)` | 遍历 A 类字段 `&&= features.get(flag(field))`;映射由 `FISCO_GATED_FLAG_MAP(X)` X-macro 生成 |
| `FISCO_GATED_FLAG_MAP(X)`(新宏,FISCO 层) | `(field, flag)` 对清单 | 同时生成 mask 代码 + 完整性 `static_assert`;flag 身份不泄漏进内核 |
| `EthPolicy::computeRevisionConfig`(改) | 不变 | `return derive(evmcRevisionFromBlockNumber(header.number()))` |
| `FiscoPolicy::computeRevisionConfig`(改) | 不变 | `cfg.eth()=derive(rev)` → `applyFiscoFeatureGates(cfg.eth(),features)` → 仅设外层 FISCO 位 |
| `makeIsthmusRevisionConfig`(改) | 不变 | `return derive(PRAGUE)`(`prague_post_execution` 默认 false,无 overlay) |
| `ForkProfileRegistry::makeReferenceRevisionConfig`(改) | 不变 | 删 11 行拷贝 → `return derive(rev)` |

**A 类可门控集(盖全集,Q1)**:`warm_access`(flag `feature_evm_eip2929`)、`eip2537`/`eip7623`/`eip7702`(`feature_evm_prague`)、`eip7212`/`eip7823`(`feature_evm_osaka`)。其中 prague/osaka 组的 `&& feature` 在当前 `toFiscoRevision` 下**冗余但显式保留**(defense-in-depth + 兑现 ADR-006 书面门控);`warm_access` 的 `feature_evm_eip2929` 是唯一真正独立的 flag。
**B 类纯 revision**:`eip1153`/`eip4844`/`eip5656`/`eip6780`/`eip1559`/`eip3651`。`eip1559`/`eip3651` 由 `derive` 统一设(Q5),**不再由 FISCO overlay 补**。

## 5. 数据流

- **作者侧:** 四个产出点(EthPolicy / FiscoPolicy / Isthmus / ForkProfileRegistry)全部经 `derive(rev)`;FISCO 多一步 `applyFiscoFeatureGates` + overlay。`derive` 是唯一写规则处。
- **读取侧:** 受控-EIP 判断统一读 `cfg.eipNN`(见 §6),不再内联比 `revision`。

## 6. 读取侧归一

| 位置 | 现在 | 改为 | 风险 |
| --- | --- | --- | --- |
| `EthHost.cpp:211` | `revision >= EVMC_CANCUN`(6780) | `m_revisionConfig.eip6780` | 低(B 类,恒等价) |
| `EthHost.cpp:242` | `revision >= EVMC_CANCUN` | `m_revisionConfig.eip6780` | 低 |
| `PrecompileActive.h:46` | `revision >= EVMC_OSAKA && cfg.eip7212` | `cfg.eip7212` | 中(A 类,需等价证明) |
| `PrecompileActive.h:59` | `revision >= EVMC_PRAGUE`(0x0b–0x11) | `cfg.eip2537` | 中(A 类) |

**不动**的合法底层 revision 读取(非 EIP 位面,进 allowlist):`CreateExecution.h`(`SPURIOUS_DRAGON`/`LONDON`)、`EthPrecompiles.cpp`(`ISTANBUL` gas 档位)、`RevisionConfig.h:77` `isIsthmusOrchestrationProfile`。

## 7. 行为等价证明义务(承重)

A 类读取归一仅在"revision 够高但 feature 关闭"时可能改变行为。等价论证:

- **EthPolicy / ForkProfileRegistry:** 无 mask,`cfg.eip2537 == rev>=PRAGUE`,恒等价。
- **FiscoPolicy:** `toFiscoRevision` 保证 `revision>=PRAGUE ⟹ feature_evm_prague 必开`,故 `cfg.eip2537==true`,等价。
- **Isthmus:** 稠密化后 `derive(PRAGUE)` 设 `eip2537=true`,与 `revision>=PRAGUE` 一致。

→ 所有可达 profile 运行时行为不变。回归证据:`Bcos2537MsmGasTest`、`Eip2537KernelTest`、`Bcos7212ExecuteViaHostTest`、`Bcos6780SelfdestructTest`。

## 8. 必须更新的快照 delta(全部 profile-only / 运行时惰性,ADR-004)

`derive` 为规范最大集,三套快照变稠密;`RevisionConfigProfileTest` 期望值同 PR 更新:

- **Isthmus**(`assertIsthmusHelperProfile`):新增 `eip1153, eip5656, eip6780, eip2537, eip1559, eip3651 = true`(`prague_post_execution` 仍 false,由默认值而非 overlay)。
- **EthPolicy**(`eth_policy_full_fork_snapshots`):新增 `eip1559`(LONDON+)、`eip3651`(SHANGHAI+)。
- **FiscoPolicy**(`fisco_policy_feature_gate_snapshots`):新增 `eip3651`(CANCUN 楼面恒 true)。

**已验证运行时惰性(grilling):** `eip3651` 全库零引用;`eip1559` 仅测试引用,生产 1559 结算走 gas caps 而非该布尔位。densify 仅动测试快照,不改运行时。

**决策:** 采用规范最大集(`derive` 统一设 `eip1559/eip3651`),与单一真相源一致;放弃"EthPolicy 快照零改动"的收窄替代,因其保留 Eth/Fisco 在 `eip1559` 上的分歧。

## 9. 测试(等价性,验收线)

复用现有 `REVISION_CONFIG_BOOL_FIELDS` 宏与断言器,新增/扩展:
1. `derive(rev)` 全 fork(PARIS..OSAKA)逐字段快照。
2. 等式断言:`EthPolicy.compute == derive(rev)`;`makeIsthmus == derive(PRAGUE)`;`ForkProfileRegistry == derive`;`FiscoPolicy == derive + applyFiscoFeatureGates`(+ 外层 FISCO 位,EIP 字段不受其影响)。
3. A/B 分区不变量:`REVISION_CONFIG_GATED_FIELDS ⊂ REVISION_CONFIG_BOOL_FIELDS`;非 A 类字段经 mask 不变;`FISCO_GATED_FLAG_MAP` 键集 == `REVISION_CONFIG_GATED_FIELDS`(编译期 `static_assert`,Q2)。
4. 读取侧归一回归(§7 列出的 CTest)。

## 10. CI grep 闸门

新增 `tools/ci/check-revision-single-source.sh`,接入 `.github/workflows/capability-gate.yml`。

**仅做 A 类规则(Q6):** 禁止 `RevisionConfig.h` 以外出现 `<gatedField> = ... revision >= EVMC_`(字段名取自 X-macro 表,高信号、近零误报)。

**不做** B 类(消费侧裸 `revision >=`)的结构性禁令——因其与 `CreateExecution.h`/`EthPrecompiles.cpp` 的合法 opcode-gas 比较同形,无法语法区分。消费侧归一的正确性改由**等价性测试**保障(§7、§9),而非闸门。残留风险:未来若有人新写消费侧内联 `revision>=` 做 EIP 门控,闸门照不到,只能靠 review + 测试覆盖发现。

## 11. 文档更新(同 PR,矩阵 CI 强制)

- `capability-matrix.md`:`RevisionConfig eip1153/eip6780/eip2537` 等 OPStack 列改注 "via derive 统一派生";重写 FIX-12 Wave-2 注记(Isthmus 不再稀疏)。
- `ADR-004`:profile-only 字段现由单一 `derive` 统一赋值,更新消费规则。
- 新增 `ADR-018: revision gating single source`(记录 derive + mask + grep 闸门)。

## 12. 验收标准

- [ ] `revisionConfigFromRevision` 落 `eth/RevisionConfig.h`,为唯一派生处。
- [ ] EthPolicy / FiscoPolicy / makeIsthmus / ForkProfileRegistry 均经 `derive`,零拷贝派生规则。
- [ ] §6 四处读取侧归一为 `cfg` 布尔位;§7 等价证明被 CTest 覆盖。
- [ ] 等价性测试(§9)全绿;`RevisionConfigProfileTest` 按 §8 更新期望。
- [ ] CI grep 闸门生效,故意引入裸 `eipNN = revision>=` 会令 CI 红。
- [ ] capability-matrix / ADR-004 / ADR-018 同 PR 更新。
- [ ] 所有既有 CTest 维持运行时行为不变。

## 13. 风险与缓解

| 风险 | 缓解 |
| --- | --- |
| A 类读取归一在某 profile 改变行为 | §7 等价证明 + 既有 2537/7212/6780 CTest 回归 |
| 快照 delta 漏改导致 CI 红 | §8 明确列出三套 delta;`REVISION_CONFIG_BOOL_FIELDS` 宏全字段断言 |
| grep 闸门误伤合法底层 revision 读取 | 仅做 A 类赋值模式(`<gatedField> = revision>=`),不碰裸 `revision>=`;字段名取自 X-macro 表 |
| 消费侧 `revision>=` 未来回流(闸门照不到) | 已知残留风险(Q6),靠等价性测试 + review;§7 列出的 CTest 守护 |
| Isthmus 稠密化被误读为行为变更 | 文档说明 profile-only/运行时惰性;matrix + FIX-12 注记同步 |

## 14. Grilling 决议(2026-06-23)与实施顺序

**决议(覆盖前文一切冲突措辞):**

| # | 议题 | 结论 |
| --- | --- | --- |
| Q1 | A 类 mask 范围 | 盖**全集**(warm_access + prague/osaka 组);prague/osaka 的 `&& feature` 冗余但显式保留 |
| Q2 | field→flag 映射完整性 | `FISCO_GATED_FLAG_MAP(X)` X-macro 同时生成 mask + `static_assert`(键集==内核 GATED 集) |
| Q3 | `prague_post_execution` | 删 `makeIsthmus` 的 `=false` overlay,依结构体默认;标注未来删除候选 |
| Q4 | 单一真相源边界 | 只管"给定 revision 的 EIP 门控";`evmcRevisionFromBlockNumber`/`toFiscoRevision` 的 blockNum/features→revision 仍在各 policy,**不进 `derive`** |
| Q5 | `eip1559`/`eip3651` 稠密化 | `derive` 统一设;profile-only/运行时惰性,仅动 EthPolicy/Fisco 快照 |
| Q6 | grep 闸门 | **仅 A 类赋值规则**;消费侧靠等价性测试,不设结构禁令 |
| Q7 | 消费侧归一范围 | `EthHost.cpp`(6780)+ `PrecompileActive.h`(2537/7212)**一次性**归一,不分步 |
| Q8 | 实施顺序 | 分阶段保持每步全绿(见下) |

**实施顺序(每步独立可测、全绿):**

1. **纯新增**:`derive`(`revisionConfigFromRevision`)+ `REVISION_CONFIG_GATED_FIELDS`/`FISCO_GATED_FLAG_MAP` 宏 + `static_assert` + `derive` 全 fork 快照测试。不改任何调用方。
2. **改派生侧**:EthPolicy / FiscoPolicy / makeIsthmus / ForkProfileRegistry 重指向 `derive`(+`applyFiscoFeatureGates`);同 commit 更新 §8 三套快照 delta。
3. **改消费侧**:`EthHost.cpp` + `PrecompileActive.h` 由内联 `revision>=` 改读 `cfg` 布尔位(Q7 一次性);靠 §7 等价证明 + 既有 CTest 回归。
4. **加闸门**:`tools/ci/check-revision-single-source.sh` + 接 `capability-gate.yml`(Q6 仅 A 类)。
5. **文档**:capability-matrix / ADR-004 / 新增 ADR-018。

> 硬依赖:步骤 2 的稠密化必须先于步骤 3 的消费侧归一落地,否则 `cfg` 布尔位在归一点上尚不可靠。
