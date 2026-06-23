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
                              FiscoPolicy = derive(rev)
                                + applyFiscoFeatureGates(cfg, features)   // 只对 A 类 &&= flag
                                + FISCO overlay(eip1559 等)
                                          │
                              makeIsthmus = derive(PRAGUE) + overlay(prague_post_execution=false)
```

**seam 纪律:** `derive` 与 A/B 宏分区是内核知识(`RevisionConfig.h` 本就有 A/B/C 注释);**feature flag 身份只出现在 FISCO 层 `applyFiscoFeatureGates`**,内核不反向依赖 FISCO(守 ADR-005 Rule 1)。

## 4. 组件

| module | interface | implementation(藏在 seam 后) |
| --- | --- | --- |
| `revisionConfigFromRevision(rev)`(新,内核) | `evmc_revision → RevisionConfig` | 所有 `字段 = rev >= FORK` 规则,唯一住所 |
| `REVISION_CONFIG_GATED_FIELDS(X)`(新宏,内核) | A 类字段名清单 | 与 `REVISION_CONFIG_BOOL_FIELDS` 平行;FISCO mask 与 grep 闸门共用 |
| `applyFiscoFeatureGates(cfg, features)`(新,FISCO 层) | `(RevisionConfig&, ledger::Features const&)` | 遍历 A 类字段 `&&= features.get(flag(field))` |
| `EthPolicy::computeRevisionConfig`(改) | 不变 | `return derive(evmcRevisionFromBlockNumber(header.number()))` |
| `FiscoPolicy::computeRevisionConfig`(改) | 不变 | `derive(rev)` → `applyFiscoFeatureGates` → FISCO overlay |
| `makeIsthmusRevisionConfig`(改) | 不变 | `derive(PRAGUE)` + overlay(`prague_post_execution=false`) |
| `ForkProfileRegistry::makeReferenceRevisionConfig`(改) | 不变 | 删 11 行拷贝 → `return derive(rev)` |

**A 类可门控集**(以等价性测试为准):`warm_access`(flag `feature_evm_eip2929`)、`eip2537`/`eip7623`/`eip7702`(`feature_evm_prague`)、`eip7212`/`eip7823`(`feature_evm_osaka`)。
**B 类纯 revision**:`eip1153`/`eip4844`/`eip5656`/`eip6780`/`eip1559`/`eip3651`。

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

- **Isthmus**(`assertIsthmusHelperProfile`):新增 `eip1153, eip5656, eip6780, eip2537, eip1559, eip3651 = true`。
- **EthPolicy**(`eth_policy_full_fork_snapshots`):新增 `eip1559`(LONDON+)、`eip3651`(SHANGHAI+)。
- **FiscoPolicy**(`fisco_policy_feature_gate_snapshots`):新增 `eip3651`(CANCUN 楼面恒 true)。

**决策:** 采用规范最大集(`derive` 统一设 `eip1559/eip3651`),与单一真相源一致;放弃"EthPolicy 快照零改动"的收窄替代,因其保留 Eth/Fisco 在 `eip1559` 上的分歧。

## 9. 测试(等价性,验收线)

复用现有 `REVISION_CONFIG_BOOL_FIELDS` 宏与断言器,新增/扩展:
1. `derive(rev)` 全 fork(PARIS..OSAKA)逐字段快照。
2. 等式断言:`EthPolicy.compute == derive(rev)`;`makeIsthmus == derive(PRAGUE)+overlay`;`ForkProfileRegistry == derive`;`FiscoPolicy == derive + applyFiscoFeatureGates + overlay`。
3. A/B 分区不变量:`REVISION_CONFIG_GATED_FIELDS ⊂ REVISION_CONFIG_BOOL_FIELDS`;非 A 类字段经 mask 不变。
4. 读取侧归一回归(§7 列出的 CTest)。

## 10. CI grep 闸门

新增 `tools/ci/check-revision-single-source.sh`,接入 `.github/workflows/capability-gate.yml`:
- 禁止 `RevisionConfig.h` 以外出现 `<gatedField> = ... revision >= EVMC_`。
- 禁止对受控 EIP 的内联 `revision >= EVMC_xxx`(按字段名/精编译地址域匹配)。
- allowlist:`CreateExecution.h`、`EthPrecompiles.cpp`、`isIsthmusOrchestrationProfile`、`RevisionConfig.h`。

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
| grep 闸门误伤合法底层 revision 读取 | allowlist + 按字段名/地址域精确匹配 |
| Isthmus 稠密化被误读为行为变更 | 文档说明 profile-only/运行时惰性;matrix + FIX-12 注记同步 |
