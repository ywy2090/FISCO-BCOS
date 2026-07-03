# Task 2 — EIP-2929 + Builtin Precompiles 审计笔记

**日期：** 2026-06-20  
**范围：** inventory #1–4, #18；`executeViaEth` ETH reference 路径

---

## Step 1: EIP-2929 runtime warm

### FB 实现

- `EthHost::access_account` / `access_storage`（`EthHost.cpp:310–326`）在 `m_warmAccess=true` 时调用 `State::warm_up_address` / `warm_up_storage`，首次返回 `EVMC_ACCESS_COLD`，再次 `EVMC_ACCESS_WARM`；`m_warmAccess=false` 时恒返回 COLD（Berlin 前行为）。
- `m_warmAccess` 来自 `ExecuteMessageInput.revisionConfig.eip2929`（`ExecuteMessage.cpp:147`），CANCUN+ profile 为 true（`EthChainPolicy.h:31`）。
- **FB 源码不含 cold/warm gas 常量**；gas 由 evmone 在 Host 返回 COLD/WARM 后按 EIP-2929 计费。

### geth 对照

`params/protocol_params.go`:

| 常量 | 值 |
|------|-----|
| `ColdAccountAccessCostEIP2929` | 2600 |
| `ColdSloadCostEIP2929` | 2100 |
| `WarmStorageReadCostEIP2929` | 100 |

`operations_acl.go` 在 `access_account`/`access_storage` 等价路径上叠加上述增量；evmone 使用相同 spec 常量（委托，未在 FB 重复定义）。

### FB 测试

- `Eip2929AccessHostTest`（4 cases）：断言 COLD→WARM 状态与 journal revert；**不测 opcode gas 数值**。
- `StateJournalRevertTest`：checkpoint revert 后 warm 集回滚。

### 判定

✅ Host 访问语义与 geth journal 模型一致；gas 由 evmone 委托，常量与 geth 对齐。  
🟡 测试缺口：无 SLOAD/BALANCE/CALL 级 gas 断言对照 geth 2600/2100/100。

---

## Step 2: EIP-2929 tx-entry warm

### destination warm (#2)

- `ExecuteViaEth.cpp:57–58` 调用 `setWarmDestinationFromKind(txProps, message.kind)`。
- `TxFeaturePrepare.h:13–16`：`CREATE`/`CREATE2` → `warmDestination=false`；其余 kind → `true`。
- `PrepareState.h:62–65`：`props.warmDestination && tx.to` 时 `warm_up_address_no_journal(*tx.to)`。
- geth `statedb.Prepare`（`statedb.go:1416–1419`）：`dst != nil` 时 warm；create-tx 不设 dst → 不 warm。一致。

**测试：** `PrepareStateTest`（sender/to/coinbase warm）、`TxFeaturePrepareTest`（create vs call）。

### coinbase warm (#3, EIP-3651)

- `TransactionProperties::warmCoinbase{true}` 默认（`Transaction.hpp:44`）；`ExecuteViaEth` 不显式赋值，依赖 implicit-default（ADR-002 / capability-matrix footnote）。
- `PrepareState.h:67–70`：`props.warmCoinbase && rev >= EVMC_SHANGHAI` → warm coinbase。
- geth `Prepare`（`statedb.go:1430–1432`）：`rules.IsShanghai` → warm coinbase。一致。

**测试：** `PrepareStateTest::warms_sender_to_and_coinbase_for_call_transaction` 在 `EVMC_SHANGHAI` 断言 coinbase warm。

### active precompile warm（tx-entry 子集）

- `PrepareState.h:77–82`：`rev >= EVMC_BERLIN` 时对 `forEachActivePrecompileAddress(rev, …)` 全部 warm。
- `Eip2929PrecompileWarm.h`：1–9 恒活；CANCUN+ 加 0x0a；PRAGUE+ 加 0x0b–0x11；OSAKA+ 加 0x0100。
- geth `Prepare` 传入 `vm.ActivePrecompiles(rules)` — 分叉阶梯一致（Cancun 无 0x0b–0x11；Prague 有）。

### 判定

✅ destination / coinbase / precompile tx-entry warm 与 geth `Prepare` 语义一致。

---

## Step 3: Builtin precompiles 0x01–0x11 (#4)

### 地址表

- `EthPrecompiles::toSuffix` / `EthHost::isBuiltinPrecompileAddress`：0x0001–0x0011（高 18 字节零）；另含 0x0100（OSAKA EIP-7212，inventory #16 另审）。
- geth `PrecompiledContractsCancun`：0x01–0x0a；`PrecompiledContractsPrague`：0x01–0x11。

### Gas 对照（`precompileGasCost` vs geth `RequiredGas`）

| 地址 | FB | geth | 一致 |
|------|-----|------|------|
| 0x01 ecrecover | 3000 | `EcrecoverGas=3000` | ✅ |
| 0x02 sha256 | 60+12×words | `Sha256BaseGas`+`Sha256PerWordGas` | ✅ |
| 0x03 ripemd160 | 600+120×words | 同 | ✅ |
| 0x04 identity | 15+3×words | `IdentityBaseGas`+`IdentityPerWordGas` | ✅ |
| 0x05 modexp | `calcModexpGas(revision)` | `bigModExp.RequiredGas` (EIP-2565 Berlin+) | ✅ |
| 0x06 bn254 add | 150 (Istanbul+) | `Bn256AddGasIstanbul=150` | ✅ |
| 0x07 bn254 mul | 6000 | `Bn256ScalarMulGasIstanbul=6000` | ✅ |
| 0x08 bn254 pairing | 45000+34000×pairs | 同 | ✅ |
| 0x09 blake2F | rounds from input[0:4] | 同 | ✅ |
| 0x0a point eval | 50000 | `BlobTxPointEvaluationPrecompileGas=50000` | ✅ |
| 0x0b–0x11 BLS | 375/12000×n/…/23800 | `Bls12381*` constants | ✅ 基线常数；0x0c/0x0e MSM **折扣表** → Task 4 |

### 实现

- 密码学委托 evmone_precompiles / evmmax；ecrecover v 仅 27/28（与 geth 一致）。
- 路由：`ExecuteMessage.cpp:183–194` 顶层 tx 直调；`EthHost::call` `routeCall` + `tryDispatchInCall` 嵌套 call。

### 测试

```bash
./bcos-evm/test/ExecuteViaEthFixtureTest   # PASS，含 stPrecompile_ecrecover/sha256/identity
./bcos-evm/test/PrepareStateTest   # PASS
./bcos-evm/test/Eip2929AccessHostTest      # PASS
```

### 偏离

🟡 **revision 门控缺失：** `isBuiltinPrecompileAddress` / `EthPrecompiles::dispatch` 不检查 revision；CANCUN revision 下调用 0x0b 仍会执行 BLS，geth 仅把 0x0b–0x11 注册在 Prague+。CANCUN+ 审计范围内 Prague/OSAKA 路径正常；CANCUN 边界为 fork 前激活风险。

---

## Step 4: chain precompile routing (#18 smoke)

- `EthHostExtension` 空继承 `HostExtension`；`tryChainPrecompile` 默认 `nullopt`（无链级扩展预编译）。
- ETH reference：`EthHost::call` → `tryChainPrecompile`（跳过）→ `EthPrecompiles::tryDispatchInCall`（builtin）。
- FISCO/OPStack 链级路由在 `bcos/` / `opstack/`（审计范围外）。

**判定：** ✅ ETH reference smoke — builtin 路由可达，链级 hook 为空符合预期。

---

## 汇总

| # | Capability | 状态 |
|---|------------|------|
| 1 | EIP-2929 runtime warm | ✅（gas evmone-delegated；测试 🟡） |
| 2 | EIP-2929 tx-entry destination warm | ✅ |
| 3 | EIP-2929 tx-entry coinbase warm | ✅（implicit-default `warmCoinbase{true}`） |
| 4 | builtin precompiles 0x01–0x11 | 🟡（gas/语义 Prague+ OK；CANCUN 下 0x0b–0x11 无 revision 门控） |
| 18 | chain precompile routing | ✅（ETH path smoke） |

**Task 2 整体：** DONE_WITH_CONCERNS
