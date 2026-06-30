# Task 3 — Cancun 簇审计笔记（EIP-1153 / 4844 / 5656 / 6780）

**日期：** 2026-06-20  
**范围：** inventory #5–9；`executeViaEth` ETH reference 路径  
**参考：** geth v1.17.3 `jump_table.go` / `eips.go` / `instructions.go` / `contracts.go`

---

## Step 1: EIP-1153 transient storage (#5)

### FB 实现

| 组件 | 位置 | 行为 |
|------|------|------|
| Profile | `EthChainPolicy.h:32` | `eip1153 = revision >= EVMC_CANCUN` |
| Host read | `EthHost.cpp:328-341` | `get_transient_storage` → `Account::transientStorage`；无账户返回零 |
| Host write | `EthHost.cpp:344-347` → `State.cpp:225-229` | `set_transient_storage` journal 后写入 `transientStorage` |
| 数据模型 | `Account.hpp:38` | `StorageMap transientStorage` |
| Opcode | evmone `tload`/`tstore`（`instructions.hpp:769-785`） | CANCUN+ revision 启用；经 Host 回调 |
| Revision 传递 | `ExecuteMessage.cpp:227-228` → `VMInstance.cpp:23-24` | `input.revisionConfig.revision` 传入 `evmone::baseline::execute(..., rev, ...)` |

### geth 对照

- `enable1153`（`eips.go:184-196`）：TLOAD/TSTORE gas = `WarmStorageReadCostEIP2929`（100）
- Cancun `newCancunInstructionSet`（`jump_table.go:119`）

### 测试

- `RevisionConfigProfileTest`：CANCUN+ `eip1153=true` ✅
- **无** ETH path 专用 TLOAD/TSTORE 集成测试或 state fixture
- `ExecuteViaEthFixtureTest`：无 1153 向量

### 判定

✅ **evmone-delegated**（opcode gas + TLOAD/TSTORE 解释）；Host 回调完整实现 within-tx 读写。  
🟡 **tx 结束清 transient**：`State::build_diff()` 含 `transientStorage` 字段，`bcos-evm/eth/` 无 tx 末 purge；依赖上层 diff 应用忽略（ADR/executor 范围外）。  
🟡 **测试缺口**：无 1153 行为断言。

---

## Step 2: EIP-6780 SELFDESTRUCT (#9)

### FB 实现

```cpp
// EthHost.cpp:145-156 — 仅 extension hook + return true
bool EthHost::selfdestruct(...) {
    (void)beneficiary;
    if (m_extension && !m_extension->allowSelfdestruct(account)) return false;
    return true;  // 无余额转移、无删除、无 IsNewContract 门控
}
```

- `EthChainPolicy::selfdestruct`（`EthChainPolicy.h:53-55`）返回 `false` = EIP-3529 无 selfdestruct refund；**EthHost 未调用**该 policy 方法。
- evmone `selfdestruct`（`instructions.hpp:981-1013`）：gas 计费后调用 `host.selfdestruct()`；**不在 evmone 内实现 6780 状态变更**；返回值仅用于 `rev < LONDON` 的 24000 refund。

### geth 对照 — `opSelfdestruct6780`（`instructions.go:908-949`）

| 条件 | 行为 |
|------|------|
| `IsNewContract(this)` | 余额转移 + `SelfDestruct(this)`（删除） |
| `!IsNewContract(this)` | 仅余额转移（若 beneficiary ≠ self）；**不删除** |
| Amsterdam+ | 额外 transfer/burn log |

FB Host **均未实现**。

### 测试

```bash
cd build && ./bcos-evm/test/ExecuteViaEthFixtureTest   # PASS
```

- `stSelfDestruct_basic.json` / `prague_selfdestruct.json`：bytecode `PUSH20 0xbb SELFDESTRUCT`；期望 `EVMC_SUCCESS`, `gas_used=7603`
- `FixtureAssert.h`：仅断言 status / output / logs / gas；**无 post-state**（余额是否转至 0xbb、0x12 是否保留）
- `PragueStateTest` 同样只断言 receipt 字段

### 判定

🔴 **Host SELFDESTRUCT 语义缺失** — 与 geth `opSelfdestruct6780` 分叉；gas/status fixture 通过但 state 未验证。  
Profile flag `eip6780` ✅（Task 1）；**kernel 不可声称 compliant**。

---

## Step 3: EIP-4844 profile 边界 (#6, #7)

### #6 Revision profile

- `EthChainPolicy.h:33`：`eip4844 = revision >= EVMC_CANCUN` ✅
- `RevisionConfigProfileTest` CANCUN+ 断言 ✅
- geth `enable4844`（`eips.go:302-308`）：BLOBHASH opcode；Besu `CancunGasCalculator`

### #7 Blob orchestration（📋 boundary）

- Matrix ETH 列：`unsupported (no blob precheck on reference path)`（`capability-matrix.md:61`）
- `ExecuteViaEth.cpp`：**无** blob versioned-hash 校验、blob gas、4844 tx type 路由
- `ExecuteMessage.cpp:71`：`context.blob_base_fee` 填入 tx context（供 BLOBBASEFEE opcode）；非 orchestration
- OPStack 路径有 `OpStackPreCheck` blob 字段（范围外）

**判定：** 📋 ** intentional unsupported** — profile/on-chain opcode 与 matrix 一致；非 🔴。

### Point eval precompile 0x0a（4844 子集，Task 2 已审）

- `EthPrecompiles.cpp:261-295` `executePointEvaluation`：192 字节输入、KZG verify、固定返回值
- Gas 50000 = geth `BlobTxPointEvaluationPrecompileGas`
- `Eip2929PrecompileWarm.h:25-29`：CANCUN+ warm 0x0a
- Task 2 已记入 builtin precompiles 行 ✅

---

## Step 4: EIP-5656 MCOPY (#8)

### FB 实现

- Profile：`EthChainPolicy.h:34` CANCUN+ ✅
- evmone `mcopy`（`instructions.hpp:896-915`）：memory copy + dynamic gas
- Revision 证据链：
  - `ExecuteMessage.cpp:145-147,227-228`：`EthHost(..., input.revisionConfig.revision, ...)` + `vm->execute(..., revision, ...)`
  - `VMInstance.cpp:23-24`：`evmone::baseline::execute(..., rev, *msg, *analysis)`
  - `EthHost::call` 嵌套 call 同样 `m_vm.execute(*this, m_revision, ...)`（`EthHost.cpp:224`）

### geth 对照

- `enable5656`（`eips.go:252-260`）：MCOPY + `gasMcopy`

### 测试

- `RevisionConfigProfileTest`：`eip5656=true` @ CANCUN+ ✅
- **无** MCOPY opcode 级 fixture

### 判定

✅ **evmone-delegated** — revision 传递证据完整；无需审 evmone MCOPY 实现体。

---

## 汇总 — inventory #5–9

| # | Capability | 状态 | 备注 |
|---|------------|------|------|
| 5 | RevisionConfig `eip1153` | ✅ | profile + Host 回调；opcode evmone-delegated |
| 6 | EIP-4844 revision profile | ✅ | CANCUN+ flag；BLOBHASH/BLOBBASEFEE via evmone |
| 7 | EIP-4844 blob orchestration | 📋 | matrix unsupported；ExecuteViaEth 无 blob tx 路径 |
| 8 | RevisionConfig `eip5656` | ✅ | MCOPY evmone-delegated；revision 传递已验证 |
| 9 | RevisionConfig `eip6780` / SELFDESTRUCT kernel | 🔴 | profile ✅；EthHost::selfdestruct stub |

**Task 3 整体：** DONE_WITH_CONCERNS（6780 Host 🔴；1153/5656/6780 无 state 级测试 🟡）

---

## 测试运行记录

```bash
cd build && ./bcos-evm/test/ExecuteViaEthFixtureTest --log_level=test_suite  # PASS（含 stSelfDestruct_basic.json）
```
