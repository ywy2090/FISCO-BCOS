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
| EIP-7702 authorization apply | kernel | 🟡 | [EIP-7702](https://eips.ethereum.org/EIPS/eip-7702) §Set code | `Eip7702.cpp:53-96` `applyAuthorizations`；门控 `executeMessage.cpp:173-181` | `applyAuthorization` (`state_transition.go:743-767`) | Prague tx validation + apply | `Eip7702ApplyAuthorizationTest`（opstack，manual `eip7702=true`）；**无** ETH reference E2E | 逻辑与 geth 对齐；EthPolicy 未设 flag → reference 不可达；`EthHost` 无 delegation 解析（evmone PRAGUE 委托） |
| EIP-7702 tx field propagation | tx input | ✅ | EIP-7702 type-4 tx | `Web3Eip7702Decoder.h` + `EthTxInputBuilder.h:34-44` → `ExecuteViaEth.cpp:112-113` | `SetCodeAuthorizations` on Message (`state_transition.go:222`) | Besu type-4 decode | `EthTxInputBuilderTest` PASS | 解码/recover authority ✅；无 builder→executeViaEth post-state E2E（profile 阻断） |
| EIP-7702 revision enable (`eip7702`) | revision profile | 🔴 | [EIP-7702](https://eips.ethereum.org/EIPS/eip-7702) | **`EthPolicy.h` 未赋值**；default `false`；consumer `executeMessage.cpp:173` | `enable7702` in `newPragueInstructionSet` (`jump_table.go:111`) | `PragueGasCalculator` | `RevisionConfigProfileTest` 期望 PRAGUE/OSAKA 仍为 false | Task 1 🔴：matrix 声称 PRAGUE+ inherited；FiscoPolicy/makeIsthmus 设 true；reference apply 不可达 |
| RevisionConfig `eip7212` | revision profile | 🔴 | [EIP-7212](https://eips.ethereum.org/EIPS/eip-7212) | `EthPolicy.h:38` OSAKA+ → true | Osaka `PrecompiledContractsOsaka` + `p256Verify` (`contracts.go:171`) | `OsakaGasCalculator`; `P256VerifyPrecompiledContract` | `RevisionConfigProfileTest` block 25,000,000+ | Policy 启用但 TE 无 consumer；Host 仍认 0x0100 为 builtin（见 #16 🔴） |
| RevisionConfig `eip7823` | revision profile | 🔴 | [EIP-7823](https://eips.ethereum.org/EIPS/eip-7823) | `EthPolicy.h:39` OSAKA+ → true | `bigModExp` `eip7823 && max(len)>1024` → error (`contracts.go:631-632`) | `BigIntegerModularExponentiationPrecompiledContract` upperBound=1024 | `RevisionConfigProfileTest` | ADR-004 profile-only；`validateModexpEip7823` 已实现但 TE `executeModexp` 未调用（见 Task 7） |
| RevisionConfig `warm_access` | revision profile | 🟡 | [EIP-2929](https://eips.ethereum.org/EIPS/eip-2929) | `EthPolicy.h:31` BERLIN+ → true | Berlin ACL via revision | `BerlinGasCalculator` | `RevisionConfigProfileTest` | ADR-004 profile-only；flag 传入 `executeMessage.cpp:140,147` 但语义门控为 revision |
| RevisionConfig `eip1559` | revision profile | 📋 | [EIP-1559](https://eips.ethereum.org/EIPS/eip-1559) | EthPolicy 未赋值（default false） | London fee market via revision | `LondonGasCalculator` | `RevisionConfigProfileTest` 期望 false | ADR-004 profile-only；`bcos-evm/eth/` 无 consumer |
| RevisionConfig `eip3651` | revision profile | 📋 | [EIP-3651](https://eips.ethereum.org/EIPS/eip-3651) | EthPolicy 未赋值（default false） | Shanghai coinbase warm via `txProps` | `ShanghaiGasCalculator` | `RevisionConfigProfileTest` 期望 false | ADR-004 profile-only；coinbase warm 走 `txProps` 非 flag |
| RevisionConfig `prague_post_execution` | revision profile | 📋 | Prague execution-spec | EthPolicy 未赋值（default false） | Prague post-exec hooks | `PragueGasCalculator` | `RevisionConfigProfileTest` 期望 false | ADR-004 profile-only；无 TE consumer |
| EIP-2929 runtime warm | kernel | ✅ | [EIP-2929](https://eips.ethereum.org/EIPS/eip-2929) §Cold/warm | `EthHost.cpp:310-326` → `State::warm_up_*`；gas 无 FB 常量 | `operations_acl.go` + `ColdAccountAccessCostEIP2929=2600` 等 (`protocol_params.go:68-70`) | `BerlinGasCalculator` | `Eip2929AccessHostTest`（COLD/WARM 状态）；`StateJournalRevertTest` | gas 由 evmone 委托；FB 无 opcode 级 gas 断言 |
| EIP-2929 tx-entry destination warm | tx input | ✅ | EIP-2929 tx access list | `ExecuteViaEth.cpp:58` `setWarmDestinationFromKind`；`warmTransactionEntry.h:62-65` | `statedb.Prepare` dst warm when non-create (`statedb.go:1417-1419`) | Berlin+ Prepare | `WarmTransactionEntryTest`; `TxFeaturePrepareTest` | CREATE/CREATE2 跳过 destination warm，与 geth 一致 |
| EIP-2929 tx-entry coinbase warm | tx input | ✅ | [EIP-3651](https://eips.ethereum.org/EIPS/eip-3651) | `TransactionProperties::warmCoinbase{true}` 默认；`warmTransactionEntry.h:67-70` `rev>=SHANGHAI` | `Prepare` `rules.IsShanghai` coinbase warm (`statedb.go:1430-1432`) | `ShanghaiGasCalculator` | `WarmTransactionEntryTest` @ `EVMC_SHANGHAI` | orchestrator 未显式赋值；implicit-default（ADR-002） |
| builtin precompiles 0x01–0x11 | kernel | 🟡 | Yellow Paper / EIP-4844 | `EthPrecompiles.cpp` `precompileGasCost`+`dispatch`；`EthHost::routeCall` | `contracts.go` `PrecompiledContractsCancun/Prague` | Prague precompile classes | `ExecuteViaEthFixtureTest`（`stPrecompile_ecrecover/sha256/identity` PASS） | 0x01–0x0a gas 与 geth 一致；0x0b–0x11 无 revision 门控；见 inventory #10 MSM 🔴 |
| EIP-2537 precompiles (0x0b–0x11) | kernel | 🔴 | [EIP-2537](https://eips.ethereum.org/EIPS/eip-2537) §Gas | TE：`EthPrecompiles.cpp:449-462`（MSM 线性 gas）；`EthBuiltinRegistry.cpp:362-428` 128 项表正确但未 wired | `protocol_params.go` `Bls12381*DiscountTable` + `contracts.go` `bls12381G1/G2MultiExp` | Besu Prague BLS precompile gas | `Eip2537KernelTest` PASS（G1Add @0x0b）；`stBLS_add.json` | EthBuiltinRegistry 256/256 表项 ✅；TE 0x0c/0x0e 缺折扣 🔴；revision 门控 🟡 |
| EIP-7623 entry precheck | orchestration | 🟡 | [EIP-7623](https://eips.ethereum.org/EIPS/eip-7623) §Floor | `ExecuteViaEth.cpp:64-78`：`eip7623` 门控；`gas < normalCost` → OOG；扣减 normalCost | intrinsic 后 `gasLimit < FloorDataGas`（`state_transition.go:572-580`） | `PragueGasCalculator.transactionFloorCost`（准入在 validator） | `RevisionConfigProfileTest`（profile）；`Bcos7623PrecheckTest` 为 FISCO `executeViaHost` | **无 ETH reference 专项测试**；floor 准入在 txpool `gasLimitMinimum`；ExecuteViaEth 无 `web3Tx` 门控 |
| EIP-7623 settlement / floor gas | orchestration | ✅ | EIP-7623 post-refund floor | `EthTxGasSettlement.h:110-127` `finalizeEthereumGasUsed`；snapshot `ExecuteViaEth.cpp:80-90`；TE `EthTransactionExecutorImpl.h:189-201` | refund 后 top-up 至 `floorDataGas`（`state_transition.go:650-660`） | `PragueGasCalculator.calculateGasRefund` | TE：`EthTxGasSettlementTest`、`EthTxGasSettlementExecutorTest`（27216 canonical）；**无** `bcos-evm/test/eth/*7623*` | helper `Eip7623.h` token=1/4 floorPerToken=10 与 geth/Besu 一致 |
| chain precompile routing | host extension | ✅ | Host extension §5.3 | `EthHostExtension` 空；`HostExtension::tryChainPrecompile` 默认 nullopt；builtin 经 `EthPrecompiles::tryDispatchInCall` | geth `evm.precompile()` active-set lookup | Besu precompile registry | smoke：`ExecuteViaEthFixtureTest` 经 `executeViaEth` 路由 0x01 | ETH reference 无链级扩展；FISCO/OPStack 在范围外 |
| EIP-7212 precompile (0x0100) | kernel | 🔴 | [EIP-7212](https://eips.ethereum.org/EIPS/eip-7212) §Precompile | TE：`EthPrecompiles` `toSuffix`/`dispatch` 仅 0x01–0x11；`EthBuiltinRegistry.cpp:491-518` 有 `p256verify` 但未 wired | `p256Verify` 6900 gas / 160B input (`contracts.go:1431-1454`) | `P256VerifyPrecompiledContract` | **无** TE 测试 | matrix unsupported ✅；`EthHost.cpp:382` 仍认 0x0100 → `tryDispatchInCall` 失败 → 空账户成功（非 geth 语义） |
| EIP-7823 modexp input bounds | kernel | 🔴 | [EIP-7823](https://eips.ethereum.org/EIPS/eip-7823) §Bounds | `ModexpGas.cpp:170-194` `MODEXP_MAX=1024`；`shouldRejectModexpEip7823` 仅 FISCO `PrecompiledImpl.h:78`；TE `executeModexp` 无门控 | Osaka `bigModExp` len>1024 reject | Besu modexp `upperBound=1024` | `stModExp_basic.json` smoke only | OSAKA+ 仍可执行 oversized modexp；与 geth/Besu MUST 不符 |

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

### Task 5 — EIP-7623（precheck + settlement）

#### ✅ Floor 公式与 settlement — `Eip7623.h` + `finalizeEthereumGasUsed`

**公式：** 零字节 token=1 / normal=4；非零 token=4 / normal=16；`floor = tokens × 10`；`floorDataGas = 21000 + floor`。与 geth `TxTokenPerNonZeroByte`/`TxCostFloorPerToken` 及 Besu `PragueGasCalculator.TOTAL_COST_FLOOR_PER_TOKEN` 一致。

**结算：** `finalizeEthereumGasUsed`（`EthTxGasSettlement.h:110-127`）在 EIP-3529 capped refund 后取 `max(used, 21000 + tokenCount × calldata_floor_per_token)`；access list 不计入 floor。对照 geth `state_transition.go:650-660` refund 后 top-up 及 Besu `calculateGasRefund`。

**TE 覆盖：** `EthTxGasSettlementTest` / `EthTxGasSettlementExecutorTest` 含 mixed calldata floor-dominated receipt、EIP-2930 + 1 非零字节 → 27216（canonical case）。

#### 🟡 Entry precheck — orchestration 层与 geth 分工不同

**现象：** `ExecuteViaEth.cpp:64-78` 在 `eip7623` 下仅检查 `message.gas < normalCost` 并扣减 normalCost；**未**在 orchestration 复现 geth `ErrFloorDataGas`（`gasLimit < 21000 + floor`）。Floor 准入由 txpool `TxValidator::validateEip7623GasFloor` → `computeTxIntrinsicGas().gasLimitMinimum()` 承担（范围外）。

**额外差异：** `ExecuteViaEth` precheck 无 `web3Tx` 门控（`ExecuteViaHost` 有；TE settlement 有 `Web3Transaction` 门控）。

**影响：** 公式与 receipt floor 正确；direct `executeViaEth` 调用若绕过 txpool 可能缺少 floor 准入。正常 Web3 路径由 txpool + TE settlement 闭合。

**建议：** 在 `bcos-evm/test/eth/` 增加 `ExecuteViaEth` 7623 precheck/settlement fixture；可选在 `ExecuteViaEth` 对齐 `web3Tx` 门控。

#### 🟡 测试缺口 — 无 ETH reference 目录专项

**现象：** `bcos-evm/test/eth/` 无 `*7623*` 测试；`Bcos7623PrecheckTest` 覆盖 FISCO `executeViaHost`。Helper/settlement 由 `transaction-executor/tests/` 间接验证。

**建议：** 增加 `ExecuteViaEth` 层 OOG precheck 与 floor receipt 断言（可复用 canonical 27216 向量）。

---

### Task 6 — EIP-7702（authorization / tx-input / revision）

#### 🔴 revision enable — EthPolicy 未设 `eip7702`（交叉引用 Task 1）

**现象：** `RevisionConfig.eip7702` 在 `executeMessage.cpp:173` 门控 `applyAuthorizations` + `warmDelegationTarget`。`EthPolicy::computeRevisionConfig`（`EthPolicy.h:27-41`）PRAGUE/OSAKA 区块从未赋值 → 恒为 `false`。

**对照：** geth Prague 经 `newPragueInstructionSet` → `enable7702`；`FiscoPolicy.h:66` 在 `feature_evm_prague` 时设 true；`makeIsthmusRevisionConfig()` 设 true。

**影响：** ETH reference `executeViaEth` 路径即使 `fillWeb3Fields` 传入 authorization list，也不会 apply delegation。Matrix 行「inherited (`EthPolicy` at PRAGUE+)」与实现不符。

**测试：** `RevisionConfigProfileTest` ETH 行 PRAGUE/OSAKA 期望 `eip7702=false`（与 EthPolicy 一致，与 matrix 冲突）。

**建议：** EthPolicy PRAGUE+ 设 `eip7702=true`，或更新 matrix + profile test 为 intentional off。

#### 🟡 kernel `applyAuthorizations` — 已实现但 reference baseline 不可达

**现象：** `Eip7702.cpp` 实现 chainId/nonce/code/refund/delegation 前缀规则，与 geth `validateAuthorization` + `applyAuthorization` 一致。`warmDelegationTarget` 在 `warm_access` 时预热 delegate target。

**EthHost：** `resolveExecutionCode` 返回原始 code；delegation 执行语义由 evmone + `EVMC_PRAGUE` revision 委托（Host 无 `parseDelegationTarget` 于 call 路径）。

**测试：** `Eip7702ApplyAuthorizationTest` / `Bcos7702ExecuteViaHostPropagationTest` 在 **手动** `eip7702=true` 下 PASS；无 `executeViaEth` + EthPolicy 集成测试。

#### 🟡 `stEIP7702_delegation.json` — plain CALL smoke，非 7702 E2E

**现象：** fixture `pre[0xbb]` 为普通 RETURN-42 字节码（非 `0xEF0100‖addr`）；tx 为 plain CALL，无 authorization list。`EthFixtureAdapter::makePragueRevisionConfig()` 不设 `eip7702`、不填 authorizations。

**测试：** `ExecuteViaEthFixtureTest` PASS — 仅 smoke；`FixtureAssert.h` 不验 delegation post-state。与 `architecture-known-gaps.md` 记录一致。

**建议：** 真 7702 fixture（auth list + delegation 安装 + 经 delegatee 执行）或 `ExecuteViaEth` auth apply 集成测试；`makePragueRevisionConfig` 同步 `eip7702=true`。

#### ✅ tx field propagation

**现象：** `EthTxInputBuilder::fillWeb3Fields` 对 type `0x04` 解码 authorization list（chainId/address/nonce/authority）；`ExecuteViaEth.cpp:112-113` 传入 `executeMessage`。

**测试：** `EthTxInputBuilderTest` PASS。

---

### Task 7 — Osaka 簇（EIP-7212 / EIP-7823）

#### 🔴 EIP-7212 — profile 启用 + Host 误认，TE 无 dispatch

**现象：** `EthPolicy.h:38` OSAKA+ 设 `eip7212=true`。`capability-matrix.md:59` 正确标 TE `unsupported`。完整实现在 `EthBuiltinRegistry.cpp:491-518`（6900 gas、`evmmax::secp256r1::verify`，与 geth `p256Verify` 一致），但 TE 路径 `EthPrecompiles.cpp:55-66` `toSuffix` 上限 `0x0011`，`dispatch` 无 `0x0100` case。

**Host 分裂：** `EthHost::isBuiltinPrecompileAddress`（`EthHost.cpp:378-382`）将 `0x0100` 视为 builtin → `routeCall` / `executeMessage` 调用 `tryDispatchInCall` → 返回 `nullopt` → 嵌套 CALL 落回空 code；顶层直调走 `makeSuccessResult`。**静默成功**，非 geth 6900 gas + p256 输出语义。

**geth 对照：** `PrecompiledContractsOsaka` 注册 `p256Verify`（`contracts.go:171`）；`RequiredGas` = 6900；160B 输入。

**测试：** `bcos-evm/test/**` 无 `7212`/`p256`/`0x0100` 用例；`RevisionConfigProfileTest` 仅 profile 快照。

**建议：** TE `EthPrecompiles` 接入 `0x0100`（或 OSAKA 前从 `isBuiltinPrecompileAddress` 移除）；增加 OSAKA p256verify fixture。

#### 🔴 EIP-7823 — `ModexpGas` 验证已实现，TE modexp 未 wired

**现象：** `EthPolicy.h:39` OSAKA+ 设 `eip7823=true`。`ModexpGas.cpp:170-194` 实现 `validateModexpEip7823`（各 field ≤1024，与 EIP MUST 一致）。`shouldRejectModexpEip7823` 仅在 FISCO `PrecompiledImpl.h:78-82`（`callBuiltinPrecompiled`）调用；TE `EthPrecompiles::executeModexp`（`EthPrecompiles.cpp:116-152`）**无**长度检查，直接 `evmone::crypto::modexp`。

**geth 对照：** Osaka `bigModExp.Run`（`contracts.go:631-632`）：`eip7823 && max(baseLen,expLen,modLen) > 1024` → error。

**Besu 对照：** `BigIntegerModularExponentiationPrecompiledContract.computePrecompile`（`:106-118`）各 length > 1024 → `PRECOMPILE_ERROR`。

**ADR-004：** `eip7823` 仍标 profile-only until wired；helper 已就绪，TE consumer 缺失 → OSAKA modexp 行为与 reference 客户端分叉。

**测试：** `stModExp_basic.json` 小输入 smoke；无 >1024 拒绝断言。

**建议：** `EthPrecompiles::tryDispatchInCall` 或 `executeModexp` 入口调用 `shouldRejectModexpEip7823`（需传入 `RevisionConfig`）；增加 modexp len=1025 OSAKA 向量。

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

**Task 8 明细：** `_work/test-inventory.md`、`_work/task8-assertion-audit.md`  
**汇总：** 断言 ✅ 16 | 🟡 25 | 🔴 0（41 行含跨路径引用）

### 显式单元用例（16）

| 测试文件 | 用例 | 断言状态 | 金标准来源 | 备注 |
|----------|------|----------|------------|------|
| `RevisionConfigProfileTest.cpp` | `revision_config_bool_field_macro_count` | ✅ | `RevisionConfig.h` 宏 | 13 个 bool 字段 |
| `RevisionConfigProfileTest.cpp` | `eth_policy_full_fork_snapshots` | ✅ | `EthPolicy.h` | 5 分叉全字段；PRAGUE `eip7623`+`calldata_floor_per_token=10` |
| `RevisionConfigProfileTest.cpp` | `fisco_policy_feature_gate_snapshots` | 🟡 | `FiscoPolicy.h` | FISCO 路径；PRAGUE `eip7702=true` 与 EthPolicy 分裂 |
| `RevisionConfigProfileTest.cpp` | `isthmus_helper_sparse_profile_all_fields` | 🟡 | `makeIsthmusRevisionConfig()` | OPStack helper |
| `EthTxInputBuilderTest.cpp` | `fillWeb3Fields_maps_eip7702_authorizations` | ✅ | EIP-7702 type-4 RLP | 仅 input 层 |
| `FiscoTxInputBuilderTest.cpp` | `fillWeb3Fields_maps_eip7702_authorizations` | 🟡 | 同上 | FISCO builder 重复 |
| `Eip2537KernelTest.cpp` | `stBLS_add_precompile_0x0b_via_executeMessage` | ✅ | geth `blsG1Add.json` #1 | output 128B 一致 |
| `TxFeaturePrepareTest.cpp` | `setWarmDestinationFromKind_matches_create_vs_call` | ✅ | geth Prepare CREATE skip | helper |
| `Eip2929AccessHostTest.cpp` | `access_account_cold_then_warm` | ✅ | EIP-2929 | 生产 EthHost |
| `Eip2929AccessHostTest.cpp` | `access_storage_cold_then_warm` | ✅ | EIP-2929 | 生产 EthHost |
| `Eip2929AccessHostTest.cpp` | `journal_revert_rolls_back_child_warm_address` | ✅ | journal revert | 含否定断言 |
| `Eip2929AccessHostTest.cpp` | `access_account_disabled_when_warm_access_off` | ✅ | flag OFF | 否定路径 |
| `WarmTransactionEntryTest.cpp` | `warms_sender_to_and_coinbase_for_call_transaction` | ✅ | geth Prepare + EIP-3651 | SHANGHAI coinbase |
| `WarmTransactionEntryTest.cpp` | `warms_access_list_address_and_storage_keys` | ✅ | EIP-2930 W2 | type-1 + 2 keys |
| `WarmTransactionEntryTest.cpp` | `builds_block_info_with_expected_fields` | 🟡 | BlockInfoBuilder | smoke |
| `ExecuteViaEthFixtureTest.cpp` | `existing_prague_fixtures_via_execute_via_eth` | — | 见 fixture 子表 | 驱动 21 JSON |

### Fixture 子项（`ExecuteViaEthFixtureTest` 循环）

| Fixture | 断言状态 | 金标准 | 备注 |
|---------|----------|--------|------|
| `stExample_return42.json` | ✅ | return-42 | gas=18 |
| `stRevert_revertBasic.json` | ✅ | revert | gas=6 |
| `stRevert_revertDepth.json` | ✅ | nested revert | gas=2632 |
| `stBLS_add.json` | ✅ | geth `blsG1Add.json` | gas 跳过 |
| `stCreate2_basic.json` | 🟡 | CREATE2 | 无 created address |
| `stCreate_initCode.json` | 🟡 | CREATE | 无 post-state |
| `stExample_gasPrice0.json` | 🟡 | smoke | gas 跳过 |
| `stCall_emptyAccount.json` | 🟡 | empty account | gas 跳过 |
| `stPrecompile_sha256.json` | 🟡 | sha256 precompile | gas 跳过 |
| `stPrecompile_identity.json` | 🟡 | identity | gas 跳过 |
| `stPrecompile_ecrecover.json` | 🟡 | ecrecover | output 空未对照 JSON |
| `stModExp_basic.json` | 🟡 | modexp | gas 跳过 |
| `stSelfDestruct_basic.json` | 🟡 | stSelfDestruct | gas=7603；无 post-state |
| `prague_selfdestruct.json` | 🟡 | 同上 | duplicate |
| `stEIP7702_delegation.json` | 🟡 | stEIP7702 | **假覆盖**：plain CALL smoke |
| `stEIP2930_accessList.json` | 🟡 | stEIP2930 | 无 access_list JSON |
| `prague_call_return_word.json` | 🟡 | smoke | |
| `prague_call_revert.json` | 🟡 | smoke | |
| `prague_call_empty_account.json` | 🟡 | smoke | |
| `prague_create_empty_initcode.json` | 🟡 | smoke | |

### Spot-check：5 imported fixture vs geth 金标准

| Fixture | 断言状态 | 对照结果 |
|---------|----------|----------|
| `stBLS_add.json` | ✅ | geth `blsG1Add.json` `bls_g1add_(inf+g1=g1)` Expected 逐字节一致 |
| `stEIP7702_delegation.json` | 🟡 | 非 7702 tx；pre 无 delegation code |
| `stSelfDestruct_basic.json` | 🟡 | gas 合理；无 6780 post-state 断言 |
| `stModExp_basic.json` | 🟡 | 无 geth 完全匹配向量；gas 跳过 |
| `stEIP2930_accessList.json` | 🟡 | 无 access_list 字段；preset warm flags only |

### 跨路径引用（范围外枚举，Part 1 关联）

| 测试文件 | 用例 | 断言状态 | 金标准来源 | 备注 |
|----------|------|----------|------------|------|
| `Bcos7623PrecheckTest` | calldata OOG precheck | 🟡 | `Eip7623.h` | FISCO `executeViaHost` |
| `EthTxGasSettlementTest` | `finalizeEthereumGasUsed` / `gasLimitMinimum` | ✅ | geth `FloorDataGas` | TE 路径 |
| `EthTxGasSettlementExecutorTest` | mixed calldata; type2 **27216** | ✅ | canonical-cases.md | TE e2e |
| *(gap)* | `ExecuteViaEth` 7623 | 🟡 | geth Prague | 无 `test/eth/*7623*` |
| `Eip7702ApplyAuthorizationTest` | `valid_auth_installs_delegation_*` | 🟡 | geth applyAuthorization | opstack；manual flag |

---

## Part 4 — 后续动作

（Task 9 填写）
