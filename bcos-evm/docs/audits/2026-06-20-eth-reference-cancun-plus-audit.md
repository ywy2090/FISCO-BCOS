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
| RevisionConfig `eip6780` | revision profile | ✅ | [EIP-6780](https://eips.ethereum.org/EIPS/eip-6780) | `EthPolicy.h:35` CANCUN+ | `enable6780` in `newCancunInstructionSet` (`jump_table.go:121`) | `CancunGasCalculator` | `RevisionConfigProfileTest` | SELFDESTRUCT 语义经 evmone revision |
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
| builtin precompiles 0x01–0x11 | kernel | 🟡 | Yellow Paper / EIP-2537/4844 | `EthPrecompiles.cpp` `precompileGasCost`+`dispatch`；`EthHost::routeCall` | `contracts.go` `PrecompiledContractsCancun/Prague` | Prague precompile classes | `ExecuteViaEthFixtureTest`（`stPrecompile_ecrecover/sha256/identity` PASS） | 0x01–0x0a gas 与 geth 一致；0x0b–0x11 无 revision 门控（CANCUN 下仍可 dispatch）；BLS MSM 折扣 → Task 4 |
| chain precompile routing | host extension | ✅ | Host extension §5.3 | `EthHostExtension` 空；`HostExtension::tryChainPrecompile` 默认 nullopt；builtin 经 `EthPrecompiles::tryDispatchInCall` | geth `evm.precompile()` active-set lookup | Besu precompile registry | smoke：`ExecuteViaEthFixtureTest` 经 `executeViaEth` 路由 0x01 | ETH reference 无链级扩展；FISCO/OPStack 在范围外 |

---

## Part 2 — 偏离项详情

（仅 🟡/🔴；Task 0 基线测试见下方）

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
