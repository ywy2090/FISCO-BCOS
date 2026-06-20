# ETH Reference CANCUN+ EIP 合规审计报告

**日期：** 2026-06-20  
**分支/commit：** `worktree-feat-evm-refactor` @ `b91cf3c14`  
**geth：** v1.17.3 @ `117e067f0`  
**Besu：** 26.6.0 @ `de8d3f0e20`  
**范围：** `executeViaEth` → `executeMessage()` → `EthHost` / `State` / evmone；`EVMC_CANCUN`–`EVMC_OSAKA`（区块阈值 19,426,587 / 22,000,000 / 25,000,000，`EthPolicy.h`）  
**审计清单：** `bcos-evm/docs/audits/_work/inventory.md`（22 行，含 4 条 profile-only）

---

## Part 0 — 执行摘要

（Task 9 填写）

---

## Part 1 — 合规矩阵

| EIP | 层级 | 状态 | Spec 依据 | FB 实现 | geth 对照 | Besu 对照 | FB 测试 | 缺口 |
|-----|------|------|-----------|---------|-----------|-----------|---------|------|
| RevisionConfig `eip1153` | revision profile | ✅ | [EIP-1153](https://eips.ethereum.org/EIPS/eip-1153) | `EthPolicy.h:32` CANCUN+ | `enable1153` in `newCancunInstructionSet` (`jump_table.go:119`) | `CancunGasCalculator` | `RevisionConfigProfileTest` block 19,426,587+ | evmone 经 `revision` 生效；flag 为 profile 快照 |
| EIP-4844 revision profile (`eip4844`) | revision profile | ✅ | [EIP-4844](https://eips.ethereum.org/EIPS/eip-4844) | `EthPolicy.h:33` CANCUN+ | `enable4844` in `newCancunInstructionSet` (`jump_table.go:117`) | `CancunGasCalculator` | `RevisionConfigProfileTest` | blob orchestration 另见 inventory #7（📋 unsupported） |
| RevisionConfig `eip5656` | revision profile | ✅ | [EIP-5656](https://eips.ethereum.org/EIPS/eip-5656) | `EthPolicy.h:34` CANCUN+ | `enable5656` in `newCancunInstructionSet` (`jump_table.go:120`) | `CancunGasCalculator` | `RevisionConfigProfileTest` | MCOPY 由 evmone revision 委托 |
| RevisionConfig `eip6780` | revision profile | ✅ | [EIP-6780](https://eips.ethereum.org/EIPS/eip-6780) | `EthPolicy.h:35` CANCUN+ | `enable6780` in `newCancunInstructionSet` (`jump_table.go:121`) | `CancunGasCalculator` | `RevisionConfigProfileTest` | profile flag only；kernel 见下行 🔴 |
| EIP-1153 transient storage (TLOAD/TSTORE) | kernel | ✅ | [EIP-1153](https://eips.ethereum.org/EIPS/eip-1153) §Transient | `EthHost.cpp:328-347` + `State.cpp:225-229` Host 回调；`Account.hpp:38` | `enable1153` TLOAD/TSTORE (`eips.go:184-196`) | `CancunGasCalculator` | `RevisionConfigProfileTest`（profile）；**无** 1153 runtime fixture | opcode gas evmone-delegated；🟡 tx 末 transient 未在 `eth/State` purge |
| EIP-4844 blob orchestration | orchestration | 📋 | [EIP-4844](https://eips.ethereum.org/EIPS/eip-4844) §Tx | matrix `unsupported`；`ExecuteViaEth.cpp` 无 blob precheck/gas | blob tx validation in `state_transition.go` | blob fee market | N/A（by design） | `executeMessage.cpp:71` 仅填 `blob_base_fee` 供 opcode；OPStack 另路径 |
| EIP-5656 MCOPY | kernel | ✅ | [EIP-5656](https://eips.ethereum.org/EIPS/eip-5656) | revision → `VMInstance.cpp:23-24` → evmone `mcopy` | `enable5656` (`eips.go:252-260`) | `CancunGasCalculator` | `RevisionConfigProfileTest` | evmone-delegated；`executeMessage.cpp:227-228` 传 `revision` |
| EIP-6780 SELFDESTRUCT (kernel) | kernel | 🔴 | [EIP-6780](https://eips.ethereum.org/EIPS/eip-6780) | `EthHost.cpp:145-156` stub（return true；无 transfer/delete/IsNewContract） | `opSelfdestruct6780` + `IsNewContract` (`instructions.go:908-949`) | Cancun SELFDESTRUCT semantics | `stSelfDestruct_basic.json` PASS（仅 gas/status） | evmone 委托 Host 做状态变更；fixture 无 post-state |
| RevisionConfig `eip2537` | revision profile | ✅ | [EIP-2537](https://eips.ethereum.org/EIPS/eip-2537) | `EthPolicy.h:36` PRAGUE+ | Prague precompiles via `evm.go:158` `IsPrague` | `PragueGasCalculator` | `RevisionConfigProfileTest` block 22,000,000+ | flag 仅 FISCO manager；TE 用 `revision` |
| RevisionConfig `eip7623` | revision profile | ✅ | [EIP-7623](https://eips.ethereum.org/EIPS/eip-7623) | `EthPolicy.h:37-40` PRAGUE+; `calldata_floor_per_token=10` | Prague rules + floor gas in `state_transition.go` | `PragueGasCalculator` | `RevisionConfigProfileTest`; orchestration 见 Task 5 | consumed by `ExecuteViaEth.cpp:64` |
| EIP-7702 revision enable (`eip7702`) | revision profile | 🔴 | [EIP-7702](https://eips.ethereum.org/EIPS/eip-7702) | **`EthPolicy.h` 未赋值**；default `false`；consumer `executeMessage.cpp:173` | `enable7702` in `newPragueInstructionSet` (`jump_table.go:111`) | `PragueGasCalculator` | `RevisionConfigProfileTest` 期望 PRAGUE/OSAKA 仍为 false | matrix 声称 PRAGUE+ inherited；FiscoPolicy/makeIsthmus 设 true；reference 7702 apply 不可达 |
| RevisionConfig `eip7212` | revision profile | 🟡 | [EIP-7212](https://eips.ethereum.org/EIPS/eip-7212) | `EthPolicy.h:38` OSAKA+ | Osaka rules `evm.go:153` `IsOsaka` | `OsakaGasCalculator`; `P256VerifyPrecompiledContract` | `RevisionConfigProfileTest` block 25,000,000+ | profile true 但 kernel 无 0x0100（inventory #16 unsupported） |
| RevisionConfig `eip7823` | revision profile | 📋 | [EIP-7823](https://eips.ethereum.org/EIPS/eip-7823) | `EthPolicy.h:39` OSAKA+ | modexp bounds in `contracts.go` | `BigIntegerModularExponentiationPrecompiledContract` | `RevisionConfigProfileTest` | ADR-004 profile-only；`ModexpGas.h:30` 无 TE 调用点 |
| RevisionConfig `warm_access` | revision profile | 🟡 | [EIP-2929](https://eips.ethereum.org/EIPS/eip-2929) | `EthPolicy.h:31` BERLIN+ → true | Berlin ACL via revision | `BerlinGasCalculator` | `RevisionConfigProfileTest` | ADR-004 profile-only；flag 传入 `executeMessage.cpp:140,147` 但语义门控为 revision |
| RevisionConfig `eip1559` | revision profile | 📋 | [EIP-1559](https://eips.ethereum.org/EIPS/eip-1559) | EthPolicy 未赋值（default false） | London fee market via revision | `LondonGasCalculator` | `RevisionConfigProfileTest` 期望 false | ADR-004 profile-only；`bcos-evm/eth/` 无 consumer |
| RevisionConfig `eip3651` | revision profile | 📋 | [EIP-3651](https://eips.ethereum.org/EIPS/eip-3651) | EthPolicy 未赋值（default false） | Shanghai coinbase warm via `txProps` | `ShanghaiGasCalculator` | `RevisionConfigProfileTest` 期望 false | ADR-004 profile-only；coinbase warm 走 `txProps` 非 flag |
| RevisionConfig `prague_post_execution` | revision profile | 📋 | Prague execution-spec | EthPolicy 未赋值（default false） | Prague post-exec hooks | `PragueGasCalculator` | `RevisionConfigProfileTest` 期望 false | ADR-004 profile-only；无 TE consumer |
| EIP-2929 runtime warm | kernel | ✅ | [EIP-2929](https://eips.ethereum.org/EIPS/eip-2929) §Cold/warm | `EthHost.cpp:310-326` → `State::warm_up_*`；gas 无 FB 常量 | `operations_acl.go` + `ColdAccountAccessCostEIP2929=2600` 等 (`protocol_params.go:68-70`) | `BerlinGasCalculator` | `Eip2929AccessHostTest`（COLD/WARM 状态）；`StateJournalRevertTest` | gas 由 evmone 委托；FB 无 opcode 级 gas 断言 |
| EIP-2929 tx-entry destination warm | tx input | ✅ | EIP-2929 tx access list | `ExecuteViaEth.cpp:58` `setWarmDestinationFromKind`；`warmTransactionEntry.h:62-65` | `statedb.Prepare` dst warm when non-create (`statedb.go:1417-1419`) | Berlin+ Prepare | `WarmTransactionEntryTest`; `TxFeaturePrepareTest` | CREATE/CREATE2 跳过 destination warm，与 geth 一致 |
| EIP-2929 tx-entry coinbase warm | tx input | ✅ | [EIP-3651](https://eips.ethereum.org/EIPS/eip-3651) | `TransactionProperties::warmCoinbase{true}` 默认；`warmTransactionEntry.h:67-70` `rev>=SHANGHAI` | `Prepare` `rules.IsShanghai` coinbase warm (`statedb.go:1430-1432`) | `ShanghaiGasCalculator` | `WarmTransactionEntryTest` @ `EVMC_SHANGHAI` | orchestrator 未显式赋值；implicit-default（ADR-002） |
| builtin precompiles 0x01–0x11 | kernel | 🟡 | Yellow Paper / EIP-4844 | `EthPrecompiles.cpp` `precompileGasCost`+`dispatch`；`EthHost::routeCall` | `contracts.go` `PrecompiledContractsCancun/Prague` | Prague precompile classes | `ExecuteViaEthFixtureTest`（`stPrecompile_ecrecover/sha256/identity` PASS） | 0x01–0x0a gas 与 geth 一致；0x0b–0x11 无 revision 门控；见 inventory #10 MSM 🔴 |
| EIP-2537 precompiles (0x0b–0x11) | kernel | 🔴 | [EIP-2537](https://eips.ethereum.org/EIPS/eip-2537) §Gas | TE：`EthPrecompiles.cpp:449-462`（MSM 线性 gas）；`EthBuiltinRegistry.cpp:362-428` 128 项表正确但未 wired | `protocol_params.go` `Bls12381*DiscountTable` + `contracts.go` `bls12381G1/G2MultiExp` | Besu Prague BLS precompile gas | `Eip2537KernelTest` PASS（G1Add @0x0b）；`stBLS_add.json` | EthBuiltinRegistry 256/256 表项 ✅；TE 0x0c/0x0e 缺折扣 🔴；revision 门控 🟡 |
| chain precompile routing | host extension | ✅ | Host extension §5.3 | `EthHostExtension` 空；`HostExtension::tryChainPrecompile` 默认 nullopt；builtin 经 `EthPrecompiles::tryDispatchInCall` | geth `evm.precompile()` active-set lookup | Besu precompile registry | smoke：`ExecuteViaEthFixtureTest` 经 `executeViaEth` 路由 0x01 | ETH reference 无链级扩展；FISCO/OPStack 在范围外 |

---

## Part 2 — 偏离项详情

（仅 🟡/🔴；Task 0 基线测试见下方）

### Task 4 — EIP-2537（BLS12-381 / 128 项折扣表）

#### ✅ EthBuiltinRegistry 静态折扣表 — 256/256 与 geth 一致

**方法：** 自 `params/protocol_params.go` 导出 `Bls12381G1MultiExpDiscountTable` + `Bls12381G2MultiExpDiscountTable`（各 128 项）；自 `EthBuiltinRegistry.cpp` 导出 `bls12_g1msm` / `bls12_g2msm` 的 `DISCOUNTS[]`；`diff` 零差异（`_work/eip2537-*-discounts.txt`）。

**用途：** FISCO `PrecompiledImpl` / `PrecompiledManager` 经 `builtinPricerBySuffix` 使用；**非** ETH reference TE 路径。

#### 🔴 TE 路径 G1MSM/G2MSM gas 未应用折扣表

**现象：** `executeMessage`（`executeMessage.cpp:185-186`）与 `EthHost::routeCall` 调用 `EthPrecompiles::tryDispatchInCall` → `precompileGasCost`（`EthPrecompiles.cpp:451-456`）：

- `0x000c`：`12000 × k`（k = len/160）
- `0x000e`：`22500 × k`（k = len/288）

无 `discountTable[k-1]/1000` 因子。

**geth 对照：** `bls12381G1MultiExp.RequiredGas` / `G2MultiExp`（`contracts.go:970-990`）使用 `params.Bls12381G{1,2}MultiExpDiscountTable`。

**影响：** k≥2 时 TE 路径 MSM gas **高于** geth（例 k=2 G1MSM：FB 24000 vs geth 22776）。固定 gas 预编译（G1Add/G2Add/Pairing/Map）与 geth 一致。

**测试：** `Eip2537KernelTest` / `stBLS_add.json` 仅覆盖 0x0b G1Add；**无** MSM gas 断言 → 缺口未捕获。

**建议：** 在 `EthPrecompiles::precompileGasCost` 接入与 geth 相同的 128 项表（共享常量或委托 pricer）；增加 MSM gas 单元测试。

#### 🟡 revision 门控（交叉引用 Task 2）

CANCUN revision 下 `executeMessage` 仍 dispatch 0x0b–0x11；geth 仅 `IsPrague` 注册。见 Task 2 Part 2。

---

### Task 3 — Cancun 簇（6780 / 1153）

#### 🔴 EIP-6780 — `EthHost::selfdestruct` 未实现 Cancun 语义

**现象：** `EthHost::selfdestruct`（`EthHost.cpp:145-156`）忽略 `beneficiary`，不转移余额、不标记/清除账户，不区分 `IsNewContract`（同 tx 新建 vs 预存合约）。evmone `selfdestruct`（`instructions.hpp:981-1013`）在 gas 计费后完全依赖 Host 回调做状态变更。

**geth 对照：** `opSelfdestruct6780`（`instructions.go:908-949`）：`newContract` → 删除 + 转账；`!newContract` → 仅转账、保留代码与 storage。

**测试：** `stSelfDestruct_basic.json` / `ExecuteViaEthFixtureTest` PASS，但 `FixtureAssert.h` 只断言 status/output/gas/logs，不验证 0x12 余额是否转至 0xbb 或账户是否保留。

**影响：** ETH reference 路径 SELFDESTRUCT 执行后链上状态与 geth/Besu 分叉；gas 路径仍可 PASS。

**建议：** 在 `EthHost::selfdestruct` 实现 6780 规则（跟踪 tx 内 CREATE/CREATE2 地址集、`transfer()` 余额、条件 `set_code` 清空）；增加 post-state fixture。

#### 🟡 EIP-1153 — 无 runtime 测试；transient 未 tx 末清除

**现象：** Host TLOAD/TSTORE 回调已实现；`State::build_diff()` 可含 `transientStorage`。`bcos-evm/eth/` 无 tx 结束 purge。

**影响：** 若上层持久化 diff 全字段，可能违反 1153「不跨 tx 持久」；当前无测试捕获。

**建议：** `build_diff` 或 tx 末 strip `transientStorage`；增加 TSTORE/TLOAD fixture。

#### 🟡 EIP-5656 / 6780 — 无 opcode 级测试

**现象：** MCOPY 与 SELFDESTRUCT 均依赖 evmone + revision 传递；除 profile 测试外无 dedicated fixture。

**建议：** 导入 Cancun state test 向量或最小 MCOPY/6780 手工 fixture。

---

### Task 2 — EIP-2929 / Precompiles

#### 🟡 builtin precompiles 0x0b–0x11 缺少 revision 门控

**现象：** `EthHost::isBuiltinPrecompileAddress`（`EthHost.cpp:360-383`）与 `executeMessage.cpp:183-194` 在任意 `evmc_revision` 下将 0x000b–0x0011 识别为 builtin 并调用 `EthPrecompiles::dispatch`，不检查 `revision >= EVMC_PRAGUE`。

**geth 对照：** `activePrecompiledContracts(rules)` 仅在 `IsPrague` 时注册 0x0b–0x11（`contracts.go:126-144`）；CANCUN revision 下调用 0x0b 为 empty-account 行为，非预编译。

**影响：** CANCUN 区块高度 + CANCUN revision 组合下，若交易直接 call 0x0b–0x11，FB 与 geth 结果分叉。Prague/OSAKA 正常路径不受影响。

**建议：** `tryDispatchInCall` 或 `isBuiltinPrecompileAddress` 增加 revision 与 `forEachActivePrecompileAddress` 一致的 active-set 门控。

#### 🟡 EIP-2929 runtime 无 opcode 级 gas 测试

**现象：** `Eip2929AccessHostTest` 仅断言 `EVMC_ACCESS_COLD`/`EVMC_ACCESS_WARM` 状态，未对照 geth 常量验证 BALANCE/SLOAD/CALL 实际 gas 消耗。

**影响：** Host 语义正确且 evmone 委托 spec 常量，但回归无法捕获 evmone revision 传递错误或 gas 表变更。

**建议：** 增加 state test fixture 或 evmone 集成用例，断言 cold/warm gas delta 与 2600/2100/100 一致。

---

### Task 0 基线测试

**构建：** PASS — `ExecuteViaEthFixtureTest`, `Eip2537KernelTest`, `RevisionConfigProfileTest`, `Eip2929AccessHostTest` 均已编译。

**运行：**

| 测试 | 方式 | 结果 |
|------|------|------|
| `Eip2929AccessStateCheckpoint/*` (13 cases) | `ctest -R "Eip2929Access"` | PASS |
| `ExecuteViaEthFixtureTest` | 直接执行二进制 | PASS |
| `Eip2537KernelTest` | 直接执行二进制 | PASS |
| `RevisionConfigProfileTest` (4 cases) | 直接执行二进制 | PASS |
| `Eip2929AccessHostTest` (4 cases) | 直接执行二进制 | PASS |

**备注：** 顶层 CTest 名称 `ExecuteViaEthFixture` / `Eip2537Kernel` / `RevisionConfigProfile` / `Eip2929AccessHost` 未出现在 `ctest -N -R` 枚举中（Boost.Test 子用例以 `Eip2929AccessStateCheckpoint/...` 注册）；二进制直接运行全部通过，无 🔴 基线失败项。

---

## Part 3 — 测试断言审计

| 测试文件 | 用例 | 断言状态 | 金标准来源 | 备注 |
|----------|------|----------|------------|------|

---

## Part 4 — 后续动作

（Task 9 填写）
