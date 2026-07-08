# ETH Reference CANCUN+ 全量 EIP 合规审计 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 对 `bcos-evm/eth/**` 的 ETH reference 路径（`executeViaEth`）从 CANCUN 至 OSAKA 涉及的 EIP 做一次性全量合规审计，产出完整审计包（Part 0–4）至 `bcos-evm/docs/audits/2026-06-20-eth-reference-cancun-plus-audit.md`。

**Architecture:** 以 capability matrix ETH 列 + `EthPolicy.h` 生成审计清单；每条 EIP 走统一六步流水线（spec → FB 实现 → geth → Besu → 测试 → 断言）；按分叉分 Task 并行审计，最后汇总严重等级与合并判定。不修改生产代码，只写审计报告与可选 matrix 建议。

**Tech Stack:** C++ 源码走读、本地 geth v1.17.3、Besu tag 26.6.0、Boost.Test 测试断言审计、`fisco-evm-review` / `fisco-evm-test-coverage` skill 流程

## Global Constraints

- **路径：** 仅 `executeViaEth` → `executeMessage()` → `EthHost` / `State` / `evmone`；代码根 `bcos-evm/eth/**`
- **排除：** `bcos-evm/bcos/**`、`bcos-evm/opstack/**`、`bcos-executor/**`；evmone opcode 实现体（仅审 revision 传递）
- **分叉：** `EVMC_CANCUN` ≤ revision ≤ `EVMC_OSAKA`（`EthPolicy.h` 区块阈值：19,426,587 / 22,000,000 / 25,000,000）
- **参考客户端：** geth `/Users/octopus/octo/code/blockchain-impl/go-ethereum` v1.17.3；Besu `/Users/octopus/octo/code/blockchain-impl/besu` tag 26.6.0
- **冲突裁决：** EIP MUST / execution-specs > geth ≈ Besu > FB 测试 > 直觉
- **禁止**跳过本地 geth/Besu 直接 WebFetch raw（除非本地不可读）
- **EIP-2537：** BLS 折扣表 128 项逐项对比，不可抽样
- **发现 🔴 仍完成全部 EIP 审计**
- **设计 spec：** `docs/superpowers/specs/2026-06-20-eth-reference-cancun-plus-audit-design.md`
- 命令前缀使用 `rtk`（仓库 CLAUDE.md 规则）

---

## File Map

| 文件 | 审计职责 |
|------|----------|
| `bcos-evm/eth/vm/EthPolicy.h` | revision 阶梯、`computeRevisionConfig` 各 EIP 开关 |
| `bcos-evm/eth/RevisionConfig.h` | profile 字段定义；ADR-004 profile-only 判定 |
| `bcos-evm/eth/ExecuteViaEth.cpp` | orchestration：warm tx entry、7623 precheck/settlement、7702 传播 |
| `bcos-evm/eth/ExecuteViaEth.h` | `ExecuteViaEthInput` / `Output` 契约 |
| `bcos-evm/eth/executeMessage.cpp` | 共享内核入口、revision 传入 evmone |
| `bcos-evm/eth/vm/VMInstance.cpp` | `evmc_revision` → evmone |
| `bcos-evm/eth/state/EthHost.cpp` | Host 回调：2929、SELFDESTRUCT/6780、7702 delegation、storage |
| `bcos-evm/eth/state/EthHost.hpp` | Host 接口声明 |
| `bcos-evm/eth/state/State.cpp` | 状态转换、1153 transient、refund |
| `bcos-evm/eth/state/EthPrecompiles.cpp` | 预编译 0x01–0x11、2537、4844 point eval |
| `bcos-evm/eth/precompiled/ModexpGas.cpp` | modexp gas；7823 长度门控（若 wired） |
| `bcos-evm/eth/Eip7702.cpp` | 7702 authorization apply |
| `bcos-evm/eth/eip/Eip7623.h` | 7623 calldata floor 常量与计算 |
| `bcos-evm/eth/eip/EthTxGasSettlement.h` | `finalizeEthereumGasUsed` |
| `bcos-evm/eth/execution/warmTransactionEntry.h` | 2929 tx-entry warm |
| `bcos-evm/eth/execution/Eip2929PrecompileWarm.h` | 2929 预编译 warm |
| `bcos-evm/capability-matrix.md` | ETH 列 baseline 清单 |
| `bcos-evm/docs/audits/2026-06-20-eth-reference-cancun-plus-audit.md` | **交付物**（本计划创建） |
| `bcos-evm/docs/audits/_work/` | 各 Task 中间笔记（可选，不提交或 .gitignore） |

### 测试与 fixture（断言审计范围）

| 文件 | 关联 EIP |
|------|----------|
| `bcos-evm/test/eth/RevisionConfigProfileTest.cpp` | profile 全字段 |
| `bcos-evm/test/eth/ExecuteViaEthFixtureTest.cpp` | 端到端 fixture |
| `bcos-evm/test/eth/EthTxInputBuilderTest.cpp` | 7702 tx 字段 |
| `bcos-evm/test/eth/Eip2537KernelTest.cpp` | 2537 |
| `bcos-evm/test/eth/TxFeaturePrepareTest.cpp` | tx feature prepare |
| `bcos-evm/test/state/Eip2929AccessHostTest.cpp` | 2929 runtime |
| `bcos-evm/test/state/WarmTransactionEntryTest.cpp` | 2929 tx-entry |
| `bcos-evm/test/state/SstoreRefundTest.cpp` | 3529 refund（6780 前置） |
| `bcos-evm/test/state/PragueStateTest.cpp` | Prague transition |
| `bcos-evm/test/fixtures/state/imported/stEIP7702_delegation.json` | 7702 fixture |
| `bcos-evm/test/fixtures/state/imported/stSelfDestruct_basic.json` | 6780/3529 |
| `bcos-evm/test/fixtures/state/imported/stBLS_add.json` | 2537 |
| `bcos-evm/test/fixtures/state/imported/stModExp_basic.json` | modexp / 7823 前置 |

### geth / Besu 对照入口（symbol 搜索，不用行号）

| EIP | geth symbol / 文件 | Besu symbol / 文件 |
|-----|-------------------|-------------------|
| 2929 | `operations_acl.go`, `gas_table.go` | `BerlinGasCalculator` |
| 1153 | evmone revision | `CancunGasCalculator` |
| 6780 | `instructions.go` `selfdestruct`, `evm.go` | Cancun operation semantics |
| 2537 | `contracts.go` `bls12381*`, `protocol_params.go` 折扣表 | BLS precompile classes |
| 7623 | `state_transition.go` `FloorDataGas` | `PragueGasCalculator` |
| 7702 | `state_transition.go` delegation | transaction validation / delegation |
| 7212 | `contracts.go` `p256Verify` | `P256VerifyPrecompiledContract` |
| 7823 | `contracts.go` modexp 长度 | `BigIntegerModularExponentiationPrecompiledContract` |

---

## 并行策略

```
Task 0 (bootstrap) ──┬── Task 2 (2929+precompile) ──┬── Task 3 (Cancun)
                     │                               ├── Task 4 (2537)
Task 1 (profile)  ───┤                               ├── Task 5 (7623)
                     │                               ├── Task 6 (7702)
                     └── Task 8 (断言审计，可与 2–7 并行）── Task 7 (Osaka)
                                                              │
                                                              ▼
                                                         Task 9 (汇总)
```

---

### Task 0: 环境校验与报告骨架

**Files:**
- Create: `bcos-evm/docs/audits/2026-06-20-eth-reference-cancun-plus-audit.md`
- Create: `bcos-evm/docs/audits/_work/inventory.md`（中间笔记）
- Read: `bcos-evm/capability-matrix.md`, `bcos-evm/eth/vm/EthPolicy.h`

**Interfaces — Produces:**
- 报告文件含 Part 0–4 空标题与表头
- `inventory.md` 含从 matrix ETH 列裁剪的审计行清单（约 20 行）

- [ ] **Step 1: 校验参考客户端版本**

```bash
cd /Users/octopus/octo/code/blockchain-impl/go-ethereum && rtk git describe --tags --exact-match 2>/dev/null || rtk git describe --tags | head -1
cd /Users/octopus/octo/code/blockchain-impl/besu && rtk git describe --tags | head -1
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor && rtk git rev-parse --abbrev-ref HEAD && rtk git rev-parse --short HEAD
```

Expected: geth 含 `v1.17.3`；Besu 含 `26.6.0`；记录 FB branch + commit

- [ ] **Step 2: 生成审计清单写入 `_work/inventory.md`**

从 `capability-matrix.md` 提取 ETH 列非 `unsupported` 行，并标注 CANCUN+ 相关：

```markdown
| # | Capability | Layer | Matrix ETH status | Audit? |
|---|------------|-------|-------------------|--------|
| 1 | EIP-2929 runtime warm | kernel | inherited | YES (prereq) |
| 2 | EIP-2929 tx-entry destination warm | tx input | inherited | YES |
| 3 | EIP-2929 tx-entry coinbase warm | tx input | inherited | YES |
| 4 | builtin precompiles 0x01–0x11 | kernel | inherited | YES (prereq) |
| 5 | RevisionConfig eip1153 | revision profile | inherited | YES |
| 6 | RevisionConfig eip4844 | revision profile | inherited | YES |
| 7 | EIP-4844 blob orchestration | orchestration | unsupported | YES (📋 boundary) |
| 8 | RevisionConfig eip5656 | revision profile | inherited | YES (evmone-delegated) |
| 9 | RevisionConfig eip6780 | revision profile | inherited | YES |
| 10 | EIP-2537 precompiles | kernel | inherited | YES |
| 11 | EIP-7623 entry precheck | orchestration | explicit | YES |
| 12 | EIP-7623 settlement | orchestration | explicit | YES |
| 13 | EIP-7702 authorization apply | kernel | inherited | YES |
| 14 | EIP-7702 tx field propagation | tx input | inherited | YES |
| 15 | EIP-7702 revision enable | revision profile | inherited | YES |
| 16 | EIP-7212 precompile | kernel | unsupported | YES (📋/gap) |
| 17 | RevisionConfig eip7823 | revision profile | feature-gated profile-only | YES |
| 18 | chain precompile routing | host extension | inherited | YES (smoke) |
```

跳过 ETH 列 `unsupported` 且非 CANCUN+ 边界项：`BCOS auth`、OPStack deposit 等。

- [ ] **Step 3: 创建报告骨架**

```bash
mkdir -p bcos-evm/docs/audits/_work
```

写入 `bcos-evm/docs/audits/2026-06-20-eth-reference-cancun-plus-audit.md`：

```markdown
# ETH Reference CANCUN+ EIP 合规审计报告

**日期：** 2026-06-20
**分支/commit：** <填>
**geth：** v1.17.3 @ <sha>
**Besu：** 26.6.0 @ <sha>
**范围：** executeViaEth, CANCUN–OSAKA

## Part 0 — 执行摘要
（Task 9 填写）

## Part 1 — 合规矩阵
| EIP | 层级 | 状态 | Spec 依据 | FB 实现 | geth 对照 | Besu 对照 | FB 测试 | 缺口 |

## Part 2 — 偏离项详情
（仅 🟡/🔴）

## Part 3 — 测试断言审计
| 测试文件 | 用例 | 断言状态 | 金标准来源 | 备注 |

## Part 4 — 后续动作
```

- [ ] **Step 4: 确认 FB 测试可编译运行（基线）**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor/build
cmake --build . --target ExecuteViaEthFixtureTest Eip2537KernelTest RevisionConfigProfileTest Eip2929AccessHostTest -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
ctest -R "ExecuteViaEthFixture|Eip2537|RevisionConfigProfile|Eip2929Access" --output-on-failure
```

Expected: 全部 PASS（若 FAIL，记入报告 Part 2 作为 🔴 起点，继续审计）

- [ ] **Step 5: Commit（仅审计文档）**

```bash
rtk git add bcos-evm/docs/audits/2026-06-20-eth-reference-cancun-plus-audit.md bcos-evm/docs/audits/_work/inventory.md
rtk git commit -m "docs(audit): scaffold ETH reference CANCUN+ compliance audit report"
```

---

### Task 1: Revision Profile 审计（CANCUN / PRAGUE / OSAKA）

**Files:**
- Read: `bcos-evm/eth/vm/EthPolicy.h`, `bcos-evm/eth/RevisionConfig.h`
- Read: `bcos-evm/docs/adr/004-revision-config-field-consumption.md`
- Test: `bcos-evm/test/eth/RevisionConfigProfileTest.cpp`
- Modify: `bcos-evm/docs/audits/2026-06-20-eth-reference-cancun-plus-audit.md`（追加 Part 1 行）
- Work: `bcos-evm/docs/audits/_work/task1-revision-profile.md`

**Interfaces — Consumes:** Task 0 `inventory.md`  
**Interfaces — Produces:** Part 1 中 revision-profile 层各行（`eip1153`, `eip4844`, `eip5656`, `eip6780`, `eip2537`, `eip7623`, `eip7702`, `eip7212`, `eip7823`, `warm_access`, profile-only 字段）

- [ ] **Step 1: 对照 `EthPolicy::computeRevisionConfig` 与 matrix 声明**

读取 `EthPolicy.h` 第 27–41 行，逐 revision 记录：

| blockNum 夹逼 | revision | 应 true 的 cfg 字段 |
|---------------|----------|---------------------|
| 19,426,587 | CANCUN | `eip1153`, `eip4844`, `eip5656`, `eip6780`, `warm_access` |
| 22,000,000 | PRAGUE | 上述 + `eip2537`, `eip7623`, `calldata_floor_per_token=10` |
| 25,000,000 | OSAKA | 上述 + `eip7212`, `eip7823` |

**重点检查：** `eip7702` 是否在 `EthPolicy` 赋值（`RevisionConfig` 有字段但 `computeRevisionConfig` 可能未设）— 若缺失标 🟡/🔴。

- [ ] **Step 2: 运行 profile 测试并对照断言**

```bash
cd build && ./bcos-evm/test/RevisionConfigProfileTest --log_level=test_suite
```

打开 `RevisionConfigProfileTest.cpp`，确认每个 `REVISION_CONFIG_BOOL_FIELDS` 在 CANCUN/PRAGUE/OSAKA 块的 `BOOST_CHECK` 与 Step 1 表一致。

- [ ] **Step 3: ADR-004 profile-only 判定**

对 `warm_access`, `eip1559`, `eip3651`, `prague_post_execution`, `eip7823`：在 `bcos-evm/eth/**` grep consumer：

```bash
rtk grep -n "cfg\.eip7823\|cfg\.eip1559\|cfg\.prague_post_execution" bcos-evm/eth/
```

无 consumer → 标 📋 `feature-gated (profile-only; ADR-004)`，不标 🔴（除非 matrix 声称 baseline-reachable）。

- [ ] **Step 4: 写入 Part 1 行 + `_work/task1-revision-profile.md` 笔记**

每行格式：

```markdown
| RevisionConfig eip1153 | revision-profile | ✅ | EIP-1153 §... | EthPolicy.h:32 | evmone via VMInstance | CancunGasCalculator | RevisionConfigProfileTest | — |
```

- [ ] **Step 5: Commit 工作笔记**

```bash
rtk git add bcos-evm/docs/audits/
rtk git commit -m "docs(audit): task1 revision profile findings"
```

---

### Task 2: 前置依赖 — EIP-2929 + Builtin Precompiles

**Files:**
- Read: `bcos-evm/eth/state/EthHost.cpp`, `bcos-evm/eth/execution/Eip2929PrecompileWarm.h`, `bcos-evm/eth/execution/warmTransactionEntry.h`, `bcos-evm/eth/ExecuteViaEth.cpp`
- Read: `bcos-evm/eth/state/EthPrecompiles.cpp`
- Test: `bcos-evm/test/state/Eip2929AccessHostTest.cpp`, `bcos-evm/test/state/WarmTransactionEntryTest.cpp`
- geth: `core/vm/operations_acl.go`, `core/vm/gas_table.go`
- Besu: `BerlinGasCalculator`

- [ ] **Step 1: 2929 runtime — FB vs geth cold/warm 常量**

```bash
rtk grep -n "COLD\|WARM\|access\|warm" bcos-evm/eth/state/EthHost.cpp bcos-evm/eth/execution/Eip2929PrecompileWarm.h
rtk grep -n "ColdAccountAccessCost\|WarmStorageReadCost" /Users/octopus/octo/code/blockchain-impl/go-ethereum/core/vm/gas_table.go
```

对照 `Eip2929AccessHostTest.cpp` 中 `SLOAD`/`SSTORE`/`BALANCE` 期望 gas 与 geth 常量是否一致。

- [ ] **Step 2: 2929 tx-entry — `ExecuteViaEth.cpp` warm 传播**

读 `warmTransactionEntry.h` 与 `ExecuteViaEth.cpp` 中 `setWarmDestinationFromKind` 调用；对照 `WarmTransactionEntryTest.cpp` 中断言的 destination/coinbase warm 行为。

geth 对照：`evm.go` / `statedb.Prepare` Shanghai coinbase warm（symbol `Prepare`）。

- [ ] **Step 3: Builtin precompiles 0x01–0x11**

读 `EthPrecompiles.cpp` 地址表；对照 geth `contracts.go` 前 10 个预编译地址与 gas pricer。

运行 fixture：

```bash
cd build && ctest -R ExecuteViaEthFixture --output-on-failure
```

确认 `stPrecompile_ecrecover.json`, `stPrecompile_sha256.json`, `stPrecompile_identity.json` 通过且 `FixtureAssert` 期望值合理。

- [ ] **Step 4: 写入 Part 1 三行（2929 runtime, 2929 tx-entry ×2, builtin precompiles）+ Part 2 若有偏离**

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/docs/audits/
rtk git commit -m "docs(audit): task2 EIP-2929 and precompile findings"
```

---

### Task 3: Cancun 簇 — EIP-1153 / 4844 / 5656 / 6780

**Files:**
- Read: `bcos-evm/eth/state/State.cpp`（1153 transient storage）
- Read: `bcos-evm/eth/state/EthHost.cpp`（`selfdestruct`、6780）
- Read: `bcos-evm/eth/vm/EthPolicy.h`（`selfdestruct` 返回 false = EIP-3529）
- Read: `bcos-evm/eth/vm/VMInstance.cpp`（5656 revision）
- Test: `bcos-evm/test/fixtures/state/imported/stSelfDestruct_basic.json`, `ExecuteViaEthFixtureTest`
- Spec: [EIP-1153](https://eips.ethereum.org/EIPS/eip-1153), [EIP-6780](https://eips.ethereum.org/EIPS/eip-6780)

- [ ] **Step 1: EIP-1153 transient storage**

在 `State.cpp` / `EthHost.cpp` 搜索 `transient` / `TLOAD` / `TSTORE` 相关 Host 回调实现。

geth：execution-specs Cancun `transient_storage` 或 evmone revision `EVMC_CANCUN` 启用确认。

若 FB 完全委托 evmone 且 `VMInstance.cpp` 在 CANCUN 传 `EVMC_CANCUN` → 标 ✅ `evmone-delegated` + 记录 revision 传递证据。

- [ ] **Step 2: EIP-6780 SELFDESTRUCT 限制**

读 `EthHost.cpp` 中 `selfdestruct` 实现；对照 geth `instructions.go` Cancun 行为（仅终止创建同 tx 内合约等）。

运行 `stSelfDestruct_basic.json` fixture；读 `FixtureAssert.h` 对 status/gas 的断言。

- [ ] **Step 3: EIP-4844 profile 边界**

`EthPolicy` 设 `eip4844=true` at CANCUN；确认 ETH reference **无** blob orchestration（matrix 📋）。

检查 `EthPrecompiles.cpp` 是否含 point evaluation precompile `0x0a`；对照 geth `pointEvaluation` precompile。

- [ ] **Step 4: EIP-5656 MCOPY（evmone-delegated）**

```bash
rtk grep -n "EVMC_CANCUN\|revision" bcos-evm/eth/vm/VMInstance.cpp bcos-evm/eth/executeMessage.cpp
```

确认 CANCUN+ 消息带 `EVMC_CANCUN` 或更高 revision 进入 `vm.execute()`。无需审 evmone MCOPY 实现。

- [ ] **Step 5: 写入 Part 1 四行 + Part 2 偏离项**

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/docs/audits/
rtk git commit -m "docs(audit): task3 Cancun cluster findings"
```

---

### Task 4: Prague 簇 — EIP-2537（128 项折扣表）

**Files:**
- Read: `bcos-evm/eth/state/EthPrecompiles.cpp`, `bcos-evm/eth/precompiled/PrecompileTraits.h`
- Read: geth `params/protocol_params.go` BLS 折扣表 + `core/vm/contracts.go` `bls12381*`
- Read: Besu BLS precompile gas 类
- Test: `bcos-evm/test/eth/Eip2537KernelTest.cpp`, `stBLS_add.json`

- [ ] **Step 1: 导出 geth 128 项折扣表**

```bash
rtk grep -n "bls12381DiscountTable\|Bls12381" /Users/octopus/octo/code/blockchain-impl/go-ethereum/params/protocol_params.go | head -20
```

在 geth 源中定位 `bls12381DiscountTable` 数组（长度 128），复制到 `_work/eip2537-geth-discounts.txt`。

- [ ] **Step 2: 导出 FB 折扣表**

```bash
rtk grep -n -i "discount\|bls" bcos-evm/eth/precompiled/ bcos-evm/eth/state/EthPrecompiles.cpp
```

复制 FB 对应数组到 `_work/eip2537-fb-discounts.txt`。

- [ ] **Step 3: 逐项 diff（脚本或手工，不可抽样）**

```bash
diff _work/eip2537-geth-discounts.txt _work/eip2537-fb-discounts.txt || true
```

任一不等 → Part 2 🔴，注明索引位置。

- [ ] **Step 4: 预编译地址 0x0b–0x11 可达性**

读 `Eip2537KernelTest.cpp` 断言；运行：

```bash
cd build && ./bcos-evm/test/Eip2537KernelTest --log_level=test_suite
```

- [ ] **Step 5: 写入 Part 1 + Part 2；Commit**

```bash
rtk git add bcos-evm/docs/audits/
rtk git commit -m "docs(audit): task4 EIP-2537 full discount table comparison"
```

---

### Task 5: Prague 簇 — EIP-7623（precheck + settlement）

**Files:**
- Read: `bcos-evm/eth/ExecuteViaEth.cpp`, `bcos-evm/eth/eip/Eip7623.h`, `bcos-evm/eth/eip/EthTxGasSettlement.h`
- geth: `core/state_transition.go` — `FloorDataGas`, `floorDataGas`
- Besu: `PragueGasCalculator`
- Test: 搜索 `7623` in `bcos-evm/test/`（可能无专项 ETH 测试 → 标覆盖缺口）

- [ ] **Step 1: 读 EIP-7623 MUST — calldata floor 公式**

记录规范公式：`floor = 10 * (zeros + 40 * nonzeros)` 或 execution-specs 等价表述。

- [ ] **Step 2: FB 实现对照**

```bash
rtk grep -n "7623\|floor\|FloorData\|calldata_floor" bcos-evm/eth/
```

读 `Eip7623.h` 常量 `10` 是否与 geth `FloorDataGas` token 一致。

读 `ExecuteViaEth.cpp` 中 `eip7623` 门控 precheck 路径；读 `finalizeEthereumGasUsed` settlement。

- [ ] **Step 3: geth 对照**

```bash
rtk grep -n "FloorDataGas\|floorDataGas" /Users/octopus/octo/code/blockchain-impl/go-ethereum/core/state_transition.go
```

- [ ] **Step 4: Besu 对照**

```bash
rtk grep -rn "floor.*calldata\|FloorData" /Users/octopus/octo/code/blockchain-impl/besu/evm/src/main/java/org/hyperledger/besu/evm/gascalculator/PragueGasCalculator.java | head -10
```

- [ ] **Step 5: 测试覆盖评估**

若无 `bcos-evm/test/eth/*7623*` 专项：Part 1 缺口列写「无 ETH reference 专项测试」；Part 3 标 🟡。

canonical case：参考 `fisco-evm-review/references/canonical-cases.md` 7623 receipt gas 样例（若适用 orchestration 层）。

- [ ] **Step 6: 写入 Part 1 两行（entry precheck, settlement）+ Part 2；Commit**

```bash
rtk git add bcos-evm/docs/audits/
rtk git commit -m "docs(audit): task5 EIP-7623 findings"
```

---

### Task 6: Prague 簇 — EIP-7702（kernel + tx-input + revision）

**Files:**
- Read: `bcos-evm/eth/Eip7702.cpp`, `bcos-evm/eth/eip/Eip7702.h`, `bcos-evm/eth/state/EthHost.cpp`, `bcos-evm/eth/ExecuteViaEth.cpp`
- Test: `bcos-evm/test/eth/EthTxInputBuilderTest.cpp`, `stEIP7702_delegation.json`, `ExecuteViaEthFixtureTest`
- geth: `state_transition.go` SET_CODE_TX_TYPE, delegation
- Besu: EIP-7702 transaction validation

- [ ] **Step 1: revision enable 链**

确认 PRAGUE 路径上 `RevisionConfig.eip7702` 如何变为 true：
- `EthPolicy::computeRevisionConfig` 是否设置？
- `ExecuteViaEth.cpp` / `TxFeaturePrepare.h` 是否另有赋值？

若 profile 未设但测试手动设 → 标 🟡「profile 与 runtime 分裂」。

- [ ] **Step 2: tx field propagation**

读 `EthTxInputBuilderTest.cpp` 中断言的 authorization list、chain_id、nonce 字段映射。

对照 geth type-4 tx 解码字段名。

- [ ] **Step 3: kernel authorization apply**

读 `Eip7702.cpp` `applyAuthorization`（或等价 symbol）；对照 geth 设置 code delegation 的 state 转换。

读 `EthHost.cpp` 对 delegation code 的 `get_code` / `call` 行为。

- [ ] **Step 4: fixture 断言审计**

```bash
rtk read bcos-evm/test/fixtures/state/imported/stEIP7702_delegation.json
```

运行 `ExecuteViaEthFixtureTest`；确认 fixture 是否真正覆盖 delegation 语义还是 smoke（architecture-known-gaps 曾注：plain CALL smoke）。

若仅 smoke → Part 3 标 🟡 假覆盖风险。

- [ ] **Step 5: 写入 Part 1 三行（authorization, tx propagation, revision）+ Part 2/3；Commit**

```bash
rtk git add bcos-evm/docs/audits/
rtk git commit -m "docs(audit): task6 EIP-7702 findings"
```

---

### Task 7: Osaka 簇 — EIP-7212 / EIP-7823

**Files:**
- Read: `bcos-evm/eth/state/EthPrecompiles.cpp`, `bcos-evm/eth/precompiled/ModexpGas.cpp`
- Read: `bcos-evm/capability-matrix.md` 行 EIP-7212（ETH: unsupported）
- geth: `p256Verify` in `contracts.go`
- Besu: `P256VerifyPrecompiledContract`

- [ ] **Step 1: EIP-7212 — matrix 声称 unsupported 的正面确认**

```bash
rtk grep -rn "7212\|p256\|0x0100\|secp256r1" bcos-evm/eth/
```

预期：`EthPrecompiles` 无 0x0100 → 标 📋 `unsupported (matrix 一致)`，但若 `EthPolicy` 在 OSAKA 设 `eip7212=true` 却无实现 → 🔴 配置分裂。

- [ ] **Step 2: EIP-7823 — profile-only vs modexp 长度检查**

```bash
rtk grep -rn "7823\|maxModExp\|MODEXP" bcos-evm/eth/precompiled/
```

对照 geth Osaka `modexp` 输入长度上限。若无 TE consumer（ADR-004）→ 📋 profile-only；若 `ModexpGas.cpp` 有长度检查则审 MUST。

- [ ] **Step 3: 写入 Part 1 两行 + Part 2；Commit**

```bash
rtk git add bcos-evm/docs/audits/
rtk git commit -m "docs(audit): task7 Osaka cluster findings"
```

---

### Task 8: 测试断言审计（横向）

**Files:**
- Read: `bcos-evm/test/eth/*.cpp`, `bcos-evm/test/state/Eip2929*.cpp`, `bcos-evm/test/state/WarmTransactionEntryTest.cpp`, `bcos-evm/test/fixtures/FixtureAssert.h`, `bcos-evm/test/fixtures/EthFixtureAdapter.h`
- Skill ref: `fisco-evm-test-coverage/references/assertion-audit.md`

**Interfaces — Consumes:** Task 2–7 已识别的 FB 测试列表  
**Interfaces — Produces:** 报告 Part 3 完整表

- [ ] **Step 1: 枚举 ETH reference 相关测试用例**

```bash
rtk grep -n "BOOST_AUTO_TEST_CASE\|BOOST_DATA_TEST_CASE" bcos-evm/test/eth/ bcos-evm/test/state/Eip2929AccessHostTest.cpp bcos-evm/test/state/WarmTransactionEntryTest.cpp bcos-evm/test/state/WarmTransactionEntryTest.cpp
```

生成 `_work/test-inventory.md` 列表（文件 → 用例名）。

- [ ] **Step 2: 对每个用例做断言审计（模板）**

每个用例填表：

```markdown
| EthTxInputBuilderTest.cpp | type4_auth_list_roundtrip | ✅ | geth types.Transaction | 字段字节一致 |
| ExecuteViaEthFixtureTest | stEIP7702_delegation | 🟡 | execution-specs 7702 delegation | 仅 smoke CALL，未断言 delegation code |
```

必读断言体：不只看用例名。打开源文件读 `BOOST_CHECK*` / `EXPECT_*` 具体期望值。

- [ ] **Step 3: fixture 金标准 spot-check（至少 5 个 imported）**

对以下 fixture 核对 `expected.gas_used` / `expected.status` / `output` 与 geth `testdata` 或 Besu referencetests 同源：

- `stEIP7702_delegation.json`
- `stBLS_add.json`
- `stSelfDestruct_basic.json`
- `stModExp_basic.json`
- `stEIP2930_accessList.json`

- [ ] **Step 4: 写入 Part 3 全表；Commit**

```bash
rtk git add bcos-evm/docs/audits/
rtk git commit -m "docs(audit): task8 test assertion audit table"
```

---

### Task 9: 汇总 — Part 0 / Part 4 / 合并判定

**Files:**
- Modify: `bcos-evm/docs/audits/2026-06-20-eth-reference-cancun-plus-audit.md`（完成 Part 0、Part 4）
- Read: 全部 `_work/task*.md`

- [ ] **Step 1: 统计 Part 1 状态计数**

```bash
rtk grep -c "| ✅ |" bcos-evm/docs/audits/2026-06-20-eth-reference-cancun-plus-audit.md
rtk grep -c "| 🟡 |" bcos-evm/docs/audits/2026-06-20-eth-reference-cancun-plus-audit.md
rtk grep -c "| 🔴 |" bcos-evm/docs/audits/2026-06-20-eth-reference-cancun-plus-audit.md
rtk grep -c "| 📋 |" bcos-evm/docs/audits/2026-06-20-eth-reference-cancun-plus-audit.md
```

- [ ] **Step 2: 写 Part 0 执行摘要**

模板：

```markdown
## Part 0 — 执行摘要

| 指标 | 值 |
|------|-----|
| 审计行数 | N |
| ✅ 一致 | n1 |
| 🟡 警告 | n2 |
| 🔴 阻断 | n3 |
| 📋 设计选择 | n4 |
| **合并判定** | ❌ / ⚠️ / ✅ |

### Top 阻断项（若有）
1. ...
```

合并规则：任一 🔴 → ❌；无 🔴 有 🟡 → ⚠️；否则 ✅。

- [ ] **Step 3: 写 Part 4 后续动作**

分三列表：
- P0 代码修复（🔴，附文件路径）
- P1 补测/改断言（🟡）
- P2 matrix 建议更新（若 ETH 列与实现不符）

- [ ] **Step 4: spec Done 标准自检**

对照 `docs/superpowers/specs/2026-06-20-eth-reference-cancun-plus-audit-design.md` §8：

- [ ] Part 0–4 齐全
- [ ] CANCUN+ matrix ETH 行均有 Part 1 条目
- [ ] 每条 🟡/🔴 有规范引用 + 文件指针
- [ ] Part 3 覆盖 `test/eth/**` 与列出的 state 测试
- [ ] 报告头含 branch/commit、geth/Besu tag

- [ ] **Step 5: 最终 Commit**

```bash
rtk git add bcos-evm/docs/audits/2026-06-20-eth-reference-cancun-plus-audit.md
rtk git commit -m "docs(audit): complete ETH reference CANCUN+ compliance audit report"
```

---

## Self-Review（计划 vs Spec）

| Spec § | 覆盖 Task |
|--------|-----------|
| §2 审计目标 / 非目标 | Task 0 清单裁剪 + Global Constraints |
| §3.1–3.5 范围与 evmone 边界 | Task 3 Step 4, Task 1 ADR-004 |
| §4.1–4.6 方法论 | 每 Task 六步流水线 |
| §5 严重等级 | Task 9 合并判定 |
| §6 交付物 Part 0–4 | Task 0 骨架 + Task 9 汇总 + 各 Task 增量写入 |
| §4.6 EIP-2537 128 项 | Task 4 专 Task |
| §4.6 发现 🔴 仍完成全部 | 并行策略不提前终止 |
| §8 Done 标准 | Task 9 Step 4 |

无 TBD/占位符；无「稍后填写」步骤。

---

## 预估工作量

| Task | 并行 | 预估 |
|------|------|------|
| 0 Bootstrap | — | 30 min |
| 1 Profile | 与 2 并行 | 45 min |
| 2 2929+Precompile | 与 1 并行 | 60 min |
| 3 Cancun | 2 完成后 | 60 min |
| 4 2537 | 可并行 | 90 min（128 项表） |
| 5 7623 | 可并行 | 45 min |
| 6 7702 | 可并行 | 60 min |
| 7 Osaka | 可并行 | 30 min |
| 8 断言审计 | Task 0 后全程 | 90 min |
| 9 汇总 | 最后 | 30 min |

**合计：** 约 6–8 小时（单 agent 串行）；Task 3–7 + 8 可分 3 个 subagent 并行缩短至 ~3 小时。
