# ETH Reference CANCUN+ 审计阻断项修复 — 设计规格

**日期：** 2026-06-20  
**状态：** 待评审  
**前置：** `docs/superpowers/specs/2026-06-20-eth-reference-cancun-plus-audit-design.md`  
**审计报告：** `bcos-evm/docs/audits/2026-06-20-eth-reference-cancun-plus-audit.md`（合并判定 ❌，7×🔴）

---

## 1. 背景

2026-06-20 ETH reference CANCUN+ 全量审计在 `executeViaEth` 路径发现 7 项 🔴 阻断，集中在：

| # | 能力 | 根因摘要 |
|---|------|----------|
| 1 | EIP-7702 revision | `EthPolicy` 未设 `eip7702` |
| 2 | EIP-6780 SELFDESTRUCT | `EthHost::selfdestruct` 为 stub |
| 3 | EIP-2537 MSM gas | TE 线性 gas，未用 128 项折扣表 |
| 4 | EIP-7212 (0x0100) | 无 dispatch，Host 误认 builtin → 静默成功 |
| 5 | EIP-7823 modexp | `validateModexpEip7823` 未 wired 到 TE |
| 6 | 0x0b–0x11 门控 | CANCUN 下可误 dispatch Prague 预编译 |
| 7 | profile 交叉引用 | `eip7212`/`eip7823` profile ON、TE consumer 缺失 |

本 spec 定义**单次 PR**修复方案，并满足 **BCOS TE baseline 继承合同**（ADR-001）：kernel 修在 `bcos-evm/eth/**`，`executeViaHost` 通过共享 `executeMessage()` 自动继承，不在 `bcos/` 重复实现。

---

## 2. 范围决策（已确认）

| 维度 | 选择 |
|------|------|
| 代码范围 | **P0 全部 6 项**（P0-1～P0-6，含可选门控 P0-6 作为必选） |
| 继承 | **P0 + 继承合同** — BCOS baseline 自动继承 kernel 修复 |
| 交付 | **单次 PR** |
| 测试 | **继承证明优先（D）** — 每项 P0 ≥1 ETH reference 测试 + ≥1 BCOS baseline 测试 |

### 2.1 在范围内

- `bcos-evm/eth/**` kernel/orchestration 修复
- `bcos-evm/test/eth/**`、`bcos-evm/test/bcos/**` 继承证明测试
- `bcos-evm/test/fixtures/**` 最小 fixture 调整（6780 post-state 等）
- `capability-matrix.md` 脚注同步（P2，同 PR）

### 2.2 不在范围内

- Part 4 P1 全量（1153 tx 末 purge、2929 opcode 级 gas、全 fixture 金标准对齐）
- OPStack 专项 orchestration（除非共享 kernel 自然覆盖）
- legacy `bcos-executor` / DAG
- 新建 `eth/kernel/` 大重构（方案 2）

---

## 3. 架构决策

### 3.1 选定方案：集中改 `eth/` 内核（方案 1）

所有语义修复落在 `EthPolicy`、`EthHost`、`EthPrecompiles`、`executeMessage`。`EthBuiltinRegistry` 仅作为**常量/算法复用源**（2537 折扣表、p256verify），不维持 TE 与 Registry 双轨执行路径。

**拒绝方案 3（双路径）：** 审计已证明 Registry wired、TE 未 wired 是 🔴 根因之一。

### 3.2 继承证明契约

每个 P0 修复交付时必须满足：

| 层 | 要求 |
|----|------|
| ETH reference | 测试经 `executeViaEth` 或直调 `executeMessage` + `EthHost`，证明 kernel 语义 |
| BCOS baseline | 测试经 `executeViaHost`，在 **相应 `feature_*` ON + revision** 下证明同一 kernel 可达 |

BCOS 测试**不得**在 `bcos/` 复制 kernel 逻辑；仅验证 orchestrator 传参与 flag 门控。

### 3.3 BCOS feature flag 与 ETH reference 差异

| 字段 | EthPolicy（reference） | FiscoPolicy（BCOS） |
|------|------------------------|---------------------|
| `eip7702` | PRAGUE+ 恒 `true` | PRAGUE+ **且** `feature_evm_prague` |
| `eip2537` | PRAGUE+ 恒 `true` | PRAGUE+ **且** `feature_evm_prague` |
| `eip7212` | OSAKA+ 恒 `true` | OSAKA+ **且** `feature_evm_osaka` |
| `eip7823` | OSAKA+ 恒 `true` | OSAKA+ **且** `feature_evm_osaka` |

继承测试在 BCOS 侧显式开启对应 flag；ETH reference 无 flag 层。

---

## 4. P0 技术设计

### P0-1 · EIP-7702 revision enable

**问题：** `RevisionConfig.eip7702` 存在但 `EthPolicy::computeRevisionConfig` 从未赋值 → `executeMessage.cpp:173` 门控永不满足。

**修改：**

```cpp
// EthPolicy.h — inside computeRevisionConfig, after eip7623:
cfg.eip7702 = cfg.revision >= EVMC_PRAGUE;
```

**联动：**

- `RevisionConfigProfileTest`：PRAGUE/OSAKA 块期望 `eip7702 == true`
- `EthFixtureAdapter::makePragueRevisionConfig()`：`eip7702 = true`
- 新建 `bcos-evm/test/eth/Eip7702ApplyAuthorizationEthTest.cpp`：经 `executeViaEth` 或 `executeMessage`，验证 auth apply 写入 delegation code

**BCOS 继承测试：** 扩展 `Bcos7702ExecuteViaHostPropagationTest`：`feature_evm_prague` ON 时 authorization list 到达 kernel 且 apply 执行。

**验收：** matrix 行「EIP-7702 revision enable」ETH 列 ✅；kernel apply 在 reference 路径可达。

---

### P0-2 · EIP-6780 SELFDESTRUCT

**问题：** `EthHost::selfdestruct`（`EthHost.cpp:145-156`）仅 `return true`，无余额转移、无 `IsNewContract` 门控、无删码。

**修改：**

1. **Tx 内 CREATE 跟踪**  
   - `EthHost` 或 `executeMessage` 维护 `std::unordered_set<evmc_address>`（或等价）记录本 tx 内 `EVMC_CREATE`/`EVMC_CREATE2` 成功创建的地址。  
   - 在 `installCreatedContractCode` 成功后插入；tx 结束销毁。

2. **`EthHost::selfdestruct(addr, beneficiary)` 语义（Cancun+ / `eip6780`）：**  
   - 若 `extension->allowSelfdestruct` 返回 false → `return false`（保留现有钩子）。  
   - 将 `addr` 余额转至 `beneficiary`（`Transfer.h`）。  
   - 若 `addr` ∈ 本 tx CREATE 集 → 清除 code/storage（及 nonce 按 geth 规则）；否则**保留 code/storage**（EIP-6780）。  
   - 返回值：是否执行了「合约终结」语义（对齐 evmone 对 Host 返回值的期望）。

3. **Pre-Cancun：** `revision < EVMC_CANCUN` 时保持 legacy 自毁（全额转移 + 删码），或按当前 `evmc_revision` 分支；以 geth 同 revision 行为为准。

**测试：**

| 测试 | 路径 | 断言 |
|------|------|------|
| `stSelfDestruct_basic.json` 增强 | ETH `ExecuteViaEthFixtureTest` | post-state：0x12 余额→0xbb；预存 0xcc code 仍存在 |
| `Bcos6780SelfdestructTest`（新建）或扩展 `ExecuteViaHostImportedFixtureTest` | BCOS | 同上 fixture via `executeViaHost` |

**验收：** Part 1 行「EIP-6780 SELFDESTRUCT (kernel)」🔴 → ✅。

---

### P0-3 · EIP-2537 MSM gas

**问题：** `EthPrecompiles::precompileGasCost` 对 `0x0c`/`0x0e` 使用 `12000×k` / `22500×k`；`EthBuiltinRegistry` 静态表与 geth 256/256 一致但 TE 未使用。

**修改：**

1. 新建 `bcos-evm/eth/precompiled/BlsGas.h`（或从 `EthBuiltinRegistry.cpp` 提取）：  
   - `blsG1MsmGas(size_t k)`、`blsG2MsmGas(size_t k)`，`k ∈ [1,128]`，表与 geth `bls12381DiscountTable` 一致。  
2. `precompileGasCost` case `0x000c`/`0x000e`：  
   - `k = input.size() / 160`（G1）或 `/ 288`（G2）；  
   - `return blsG1MsmGas(k)` / `blsG2MsmGas(k)`。  
3. `EthBuiltinRegistry` 改为 include 同一表（DRY）。

**金标准：** k=2 G1MSM → **22776** gas（非 24000）。

**测试：**

| 测试 | 路径 |
|------|------|
| `Eip2537KernelTest` 增 `G1MsmGas_k2` | ETH |
| `Bcos2537MsmGasTest` 或 fixture via `ExecuteViaHostImportedFixtureTest` | BCOS，`feature_evm_prague` ON |

**验收：** TE MSM gas 与 geth `contracts.go` 一致；静态表仍 0 diff。

---

### P0-4 · EIP-7212 (0x0100 p256verify)

**问题：** `isBuiltinPrecompileAddress` 认 `0x0100`，但 `toSuffix` 仅 `0x0001–0x0011` → `tryDispatchInCall` 失败 → empty code 静默成功。

**修改：**

1. 引入统一 **`isActivePrecompile(revision, RevisionConfig, address)`**（见 P0-6），OSAKA+ 且 `eip7212` 时包含 `0x0100`。  
2. `EthPrecompiles` 增 `0x0100` dispatch：  
   - gas **6900**；  
   - 输入 160 字节；  
   - `evmmax::secp256r1::verify`（与 Registry 实现一致）。  
3. 非 OSAKA 或 `!eip7212`：`0x0100` **不是** active precompile。

**测试：**

| 测试 | 路径 |
|------|------|
| `Eip7212KernelTest`（新建） | ETH，OSAKA revision |
| `Bcos7212ExecuteViaHostTest`（新建） | BCOS，`feature_evm_osaka` ON |

**验收：** matrix EIP-7212 ETH 列由 `unsupported` → `inherited`（若 TE dispatch 完成）。

---

### P0-5 · EIP-7823 modexp bounds

**问题：** `validateModexpEip7823` / `shouldRejectModexpEip7823` 仅在 FISCO `PrecompiledImpl`；`EthPrecompiles::executeModexp` 无长度检查。

**修改：**

1. `EthPrecompiles::dispatch` / `tryDispatchInCall` 传入 `RevisionConfig`（或 `eip7823` + `revision`）。  
2. modexp（`0x0005`）执行前：若 `shouldRejectModexpEip7823(...)` → `EVMC_PRECOMPILE_FAILURE`，不执行 `evmone::crypto::modexp`。  
3. 复用 `ModexpGas.cpp` 现有函数，不复制 1024 上限常量。

**测试：**

| 测试 | 路径 |
|------|------|
| `Eip7823ModexpRejectTest`（新建） | ETH，OSAKA，字段 len=1025 |
| `Bcos7823ModexpRejectTest`（新建） | BCOS，`feature_evm_osaka` ON |

**验收：** ADR-004 profile-only → TE consumer wired；matrix `eip7823` 脚注更新。

---

### P0-6 · Prague 预编译 revision 门控

**问题：** `isBuiltinPrecompileAddress` 对 `0x01–0x11` 无 revision 检查；CANCUN 下 call `0x0b` 误走预编译。

**修改：** 单一函数替代分散判断：

```cpp
bool isActivePrecompile(
    evmc_revision revision,
    const bcos::evm_standard::RevisionConfig& cfg,
    const evmc_address& addr) noexcept;
```

| 条件 | Active 集合 |
|------|-------------|
| 恒真 | `0x01`–`0x0a` |
| `revision >= EVMC_PRAGUE` | `0x0b`–`0x11` |
| `revision >= EVMC_OSAKA && cfg.eip7212` | `0x0100` |

**调用点（必须全部替换）：**

- `EthHost::isBuiltinPrecompileAddress` → 委托或内联 `isActivePrecompile`
- `EthHost::routeCall` / `RoutedCall`
- `executeMessage.cpp:183` 空 code 预编译短路

**测试：**

| 测试 | 路径 |
|------|------|
| `EipPrecompileRevisionGateTest`（新建） | ETH，CANCUN call `0x0b` → 非预编译 |
| `BcosPrecompileRevisionGateTest`（新建） | BCOS，同断言 |

---

## 5. 文件 touch 清单

| 文件 | P0 项 |
|------|-------|
| `bcos-evm/eth/vm/EthPolicy.h` | 1 |
| `bcos-evm/eth/state/EthHost.cpp`, `EthHost.hpp` | 2, 6 |
| `bcos-evm/eth/executeMessage.cpp` | 2, 5, 6 |
| `bcos-evm/eth/state/EthPrecompiles.cpp`, `.hpp` | 3, 4, 5, 6 |
| `bcos-evm/eth/precompiled/BlsGas.h`（新建） | 3 |
| `bcos-evm/eth/precompiled/EthBuiltinRegistry.cpp` | 3（DRY 引用） |
| `bcos-evm/eth/precompiled/ModexpGas.h` | 5 |
| `bcos-evm/test/eth/RevisionConfigProfileTest.cpp` | 1 |
| `bcos-evm/test/fixtures/EthFixtureAdapter.h` | 1, 2 |
| `bcos-evm/test/eth/Eip2537KernelTest.cpp` | 3 |
| `bcos-evm/test/eth/Eip7702ApplyAuthorizationEthTest.cpp`（新建） | 1 |
| `bcos-evm/test/eth/Eip7212KernelTest.cpp`（新建） | 4 |
| `bcos-evm/test/eth/Eip7823ModexpRejectTest.cpp`（新建） | 5 |
| `bcos-evm/test/eth/EipPrecompileRevisionGateTest.cpp`（新建） | 6 |
| `bcos-evm/test/bcos/Bcos7702ExecuteViaHostPropagationTest.cpp` | 1 |
| `bcos-evm/test/bcos/Bcos6780SelfdestructTest.cpp`（新建） | 2 |
| `bcos-evm/test/bcos/Bcos2537MsmGasTest.cpp`（新建） | 3 |
| `bcos-evm/test/bcos/Bcos7212ExecuteViaHostTest.cpp`（新建） | 4 |
| `bcos-evm/test/bcos/Bcos7823ModexpRejectTest.cpp`（新建） | 5 |
| `bcos-evm/test/bcos/BcosPrecompileRevisionGateTest.cpp`（新建） | 6 |
| `bcos-evm/test/fixtures/state/imported/stSelfDestruct_basic.json` | 2 |
| `bcos-evm/test/CMakeLists.txt` | 新 target 注册 |
| `bcos-evm/capability-matrix.md` | 脚注 |

**明确不修改（除非钩子签名扩展）：** `bcos/FiscoHostExtension.*` 内核逻辑。

---

## 6. 测试矩阵（继承证明）

| P0 | ETH 测试 | BCOS 测试 | feature / revision |
|----|----------|-----------|------------------|
| P0-1 7702 | `Eip7702ApplyAuthorizationEthTest` | `Bcos7702ExecuteViaHostPropagationTest` | PRAGUE + `feature_evm_prague` |
| P0-2 6780 | `ExecuteViaEthFixtureTest` + post-state | `Bcos6780SelfdestructTest` | CANCUN+ |
| P0-3 2537 MSM | `Eip2537KernelTest` k=2 | `Bcos2537MsmGasTest` | PRAGUE + `feature_evm_prague` |
| P0-4 7212 | `Eip7212KernelTest` | `Bcos7212ExecuteViaHostTest` | OSAKA + `feature_evm_osaka` |
| P0-5 7823 | `Eip7823ModexpRejectTest` | `Bcos7823ModexpRejectTest` | OSAKA + `feature_evm_osaka` |
| P0-6 门控 | `EipPrecompileRevisionGateTest` | `BcosPrecompileRevisionGateTest` | CANCUN vs PRAGUE |

**回归：** Task 0 基线（`ExecuteViaEthFixtureTest`、`Eip2537KernelTest`、`RevisionConfigProfileTest`、`Eip2929AccessHostTest`）全绿。

---

## 7. 验收标准（Done）

- [ ] Part 1 对应 7 条 🔴 全部关闭（可保留 🟡）
- [ ] 上表 12 个测试（6 ETH + 6 BCOS）注册 ctest 且 PASS
- [ ] `EthBuiltinRegistry` 与 geth 2537 折扣表仍 0 diff
- [ ] `capability-matrix.md` ETH 列脚注与行状态更新（7212、7823、6780 kernel、7702）
- [ ] 无 `bcos/` 内核逻辑重复
- [ ] 可选：附 `bcos-evm/docs/audits/2026-06-20-eth-reference-cancun-plus-audit-fix-summary.md` 一页摘要

**合并判定目标：** 复审计后 ≥ ⚠️（无 🔴）；理想 ✅。

---

## 8. 风险与缓解

| 风险 | 缓解 |
|------|------|
| P0-2 CREATE 集生命周期与 nested call | 仅在 top-level `executeMessage` tx 范围持有；与 geth `StateDB` 同 tx 创建集对照 |
| P0-6 与 P0-4 地址判定不一致 | 单一 `isActivePrecompile` 函数，所有 call site 强制使用 |
| BCOS flag OFF 时继承测试 SKIP | 测试内显式 `Features` builder 开启 flag，不依赖全局默认 |
| 单次 PR 过大 | 按 P0-1→6 顺序提交 logical commits（同一 PR 内） |
| evmone 6780 与 Host 职责边界 | 以 geth Host 回调为金标准；对照 `instructions.go` + evmone PRAGUE 文档 |

---

## 9. capability matrix 更新（P2，同 PR）

| 行 | 变更 |
|----|------|
| EIP-7702 revision enable | 保持 `inherited`（P0-1 后名副其实） |
| EIP-6780 SELFDESTRUCT kernel | profile ✅ + kernel ✅ |
| EIP-2537 | 移除「TE MSM 待接」脚注 |
| EIP-7212 | `unsupported` → `inherited`（P0-4 后） |
| RevisionConfig `eip7823` | 脚注：TE consumer wired（P0-5 后） |
| builtin 0x01–0x11 | 脚注：P0-6 revision 门控 |

---

## 10. 后续

用户评审本 spec 通过后，使用 `writing-plans` 生成实施计划（单次 PR、Subagent-Driven 或 Inline 执行）。

**相关文档：**

- 审计设计：`2026-06-20-eth-reference-cancun-plus-audit-design.md`
- 审计计划：`docs/superpowers/plans/2026-06-20-eth-reference-cancun-plus-audit.md`
- ADR-001、ADR-004
