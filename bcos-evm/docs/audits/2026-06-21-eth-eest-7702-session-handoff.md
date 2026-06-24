# ETH Reference / EEST 7702 对齐 — Session 交接文档

**日期：** 2026-06-21  
**分支：** `worktree-feat-evm-refactor`  
**基线 commit 范围：** `92279f578` → `45c1c6c0f`（本 session 3 个 commit）  
**对话 transcript：** [7702 EEST parity session](07ec0d69-4d8e-4f3b-938d-7e5c8e69c70e)

---

## 1. 背景与目标

本 session 围绕 **ETH reference path（`ExecuteViaEth`）与 geth / EEST 的 EIP-7702 对齐**，分三波推进：

| 阶段 | 目标 | 状态 |
|------|------|------|
| **P0** | 7702 auth intrinsic 扣 gas、included-tx vmerr 语义、storage noop；ADR-015 + capability-matrix；EEST smoke 扩展 | ✅ 已 commit |
| **P1** | self_sponsored smoke 升档 `stateRoot`；EIP-3529 SSTORE refund；`EthIncludedTxVmerrTest` | ✅ 已 commit |
| **P2** | 跑 full manifest baseline；修 tx-full；W1–W4 precheck 快赢（~12 fail） | ✅ 已 commit |

**不在本 session 范围（后续 wave）：** state full 大桶 ~537 fail，主要是 `stateRoot` mismatch（~458）和 included success 路径 status 错误（~67）。

---

## 2. 已提交 Commit 清单

| Commit | 摘要 | 关键文件 |
|--------|------|----------|
| `3d0e4dc7e` | **P0** — 7702 gas + included-tx vmerr + storage noop | `ExecuteViaEth.cpp`, `EthTxGasSettlement.h`, `State.cpp`, `ExecuteViaEthAdapter.cpp`, ADR-015, smoke/full manifests |
| `d31194b41` | **P1** — smoke stateRoot + vmerr 单测 + EthHost refund | `EthHost.cpp`, `EthIncludedTxVmerrTest.cpp`, `eth-eest-state-smoke.json` |
| `45c1c6c0f` | **P2 + W1–W4** — tx-full strict decode + ExecuteViaEth precheck | `Eip7702StrictTxValidator.cpp`, `ExecuteViaEthPreCheck.*`, adapter/loader |

---

## 3. 技术决策摘要（ADR-015）

完整文档：`bcos-evm/docs/adr/015-eth-reference-7702-gas-and-included-tx-vmerr.md`

### 3.1 7702 auth intrinsic

- `ExecuteViaEth` 在 `eip7623` 下于 `executeMessage` **之前** debit `calcAuthTupleIntrinsicGas(n)`。
- 写入 `gasSettlementSnapshot.authIntrinsic`，纳入 `finalizeEthereumGasUsed`。
- existence refund（12500×已存在 authority）经 `state.get_refund()` → settlement。

### 3.2 Included top-level vmerr

- depth=0 时 EVM vmerr **不**作为 tx rejection；normalize 为 `EVMC_SUCCESS`。
- `ExecuteViaEthOutput::topLevelIncludedTxVmError` 标记后走 `settleIncludedTopLevelTransactionGas`（peakGasUsed + EIP-3529 refund cap）。
- 7702 authorization 在 `Call` 前已 apply，vmerr 后 state 仍 commit（geth `TransitionDb err == nil` 语义）。

### 3.3 Storage no-op

- `State::set_storage`：prev == value 时 early return。
- GST post-state trie 省略 zero-valued slots。

### 3.4 W1–W4 Precheck（`45c1c6c0f`）

新增 `ethExecuteViaEthPreCheck()`，在 intrinsic gas **之前**调用，对齐 `OpStackPreCheck`：

| ID | 规则 | geth 对应 |
|----|------|-----------|
| W1 | sender 有 code 且非 delegation → `Malformed` | `SENDER_NOT_EOA` |
| W2 | `authorizationListPresent && authorizations.empty()` → `Malformed` | `TYPE_4_EMPTY_AUTHORIZATION_LIST` |
| W3 | type-4 + `EVMC_CREATE` → `Malformed` | `TYPE_4_TX_CONTRACT_CREATION` |
| W4 | `gasFeeCap < gasTipCap` 或 `gasFeeCap < baseFee` → `Malformed` | fee validation |

**Adapter 修正：**

- `authorizationListPresent` 改由 JSON key 是否存在驱动（`GstTransactionTemplate.authorizationListKeyPresent`），不再用 `!authorizations.empty()`。
- `resolveWeb3TypedTxKind`：空 auth list 但 key 存在时仍识别为 type-4（`0x04`）。
- 传递 `gasTipCap` / `gasFeeCap`（EIP-1559 字段或 legacy gasPrice）。

### 3.5 tx-full strict decode（P2）

- 根因：`invalid_nonce_encoding` 等实为 authorization **chainId 非 canonical RLP**（如 `820000`）。
- 修复：`decodeStrictScalarBytes` + authorization tuple 全字段 strict decode（`Eip7702StrictTxValidator.cpp`）。

---

## 4. EEST Baseline（2026-06-21，`build-ref`）

| Manifest | Executed | Pass | Fail | 说明 |
|----------|----------|------|------|------|
| `eth-eest-tx-full.json` | 106 | **106** | 0 | 7702 transaction_tests |
| `eth-eest-state-full.json` | 1056 | **519** | **537** | 7623 230/483；7823 0/21；7702 **289/552** |
| smoke (`eth-eest-state-smoke.json` 等) | — | **13/13** | 0 | PR gate |

**Precheck 快赢：** W1–W4 共 **12 subtests**，commit 前 probe 全绿；full manifest fail 549 → **537**（+12 pass）。

**剩余 fail 分布（估算）：**

- `stateRoot` mismatch ~458
- included success 路径 status 错误 ~67
- 7623 / 7823 目录内其他执行路径差异

---

## 5. 关键文件地图

### Orchestration（ETH reference）

```
bcos-evm/eth/ExecuteViaEth.cpp          # 主入口：precheck → intrinsic → executeMessage → settlement
bcos-evm/eth/ExecuteViaEthPreCheck.cpp  # W1–W4 precheck
bcos-evm/eth/gas/EthTxGasSettlement.h   # finalizeEthereumGasUsed / settleIncludedTopLevelTransactionGas
bcos-evm/eth/state/EthHost.cpp          # EIP-3529 SSTORE refund（P1）
bcos-evm/eth/state/State.cpp            # storage no-op（P0）
```

### EEST / GST adapter

```
bcos-evm/specs-tests/src/ExecuteViaEthAdapter.cpp   # fee/auth/type-4 传播 + settlement
bcos-evm/specs-tests/src/GeneralStateTestLoader.cpp # authorizationListKeyPresent
bcos-evm/specs-tests/src/Eip7702StrictTxValidator.cpp
bcos-evm/specs-tests/src/StateTestAssert.cpp      # expectException: status != SUCCESS 即 pass
```

### Manifests

```
bcos-evm/specs-tests/manifests/eth-eest-state-smoke.json   # PR gate（114+ subtests）
bcos-evm/specs-tests/manifests/eth-eest-state-full.json      # nightly full sweep
bcos-evm/specs-tests/manifests/eth-eest-tx-full.json
bcos-evm/specs-tests/manifests/eth-eest-probe-*.json       # ADR-015 探针
```

### 单测

```
bcos-evm/test/eth/EthIncludedTxVmerrTest.cpp       # ADR-015 vmerr settlement
bcos-evm/test/eth/EthExecuteViaEthPreCheckTest.cpp # W1–W4（6 cases）
bcos-evm/specs-tests/runners/Eip7702StrictTxValidatorTest.cpp
```

### 文档 / 矩阵

```
bcos-evm/docs/adr/015-eth-reference-7702-gas-and-included-tx-vmerr.md
bcos-evm/capability-matrix.md   # 7702 precheck + 7623 settlement → explicit
bcos-evm/specs-tests/README.md
```

---

## 6. 构建与验证命令

### 环境

- **Reference tests：** `build-ref/`
- **bcos-evm 单测：** `build-bcos-evm-check/`
- **EEST assets：** 首次需 `bcos-evm/specs-tests/tools/fetch_eest_assets.sh`

### 常用命令

```bash
# PR gate smoke（13 tests）
ctest -L 'specs-tests-smoke' --test-dir build-ref -C Debug --output-on-failure

# Full state sweep（预期 ~537 fail，nightly）
./build-ref/bcos-evm/specs-tests/EthExecutionSpecStateTests \
  --manifest bcos-evm/specs-tests/manifests/eth-eest-state-full.json

# Full tx（应 106/106 pass）
./build-ref/bcos-evm/specs-tests/EthExecutionSpecTransactionTests \
  --manifest bcos-evm/specs-tests/manifests/eth-eest-tx-full.json

# 本 session 新增单测
./build-bcos-evm-check/bcos-evm/test/EthExecuteViaEthPreCheckTest
./build-bcos-evm-check/bcos-evm/test/EthIncludedTxVmerrTest

# W1–W4 定向 probe（可临时写 manifest 指向单文件）
# fixtures 路径（相对 eest root = assets/eest）：
#   fixtures/state_tests/prague/eip7702_set_code_tx/test_set_code_from_account_with_non_delegating_code.json
#   fixtures/state_tests/prague/eip7702_set_code_tx/test_empty_authorization_list.json
#   fixtures/state_tests/prague/eip7702_set_code_tx/test_contract_create.json
#   fixtures/state_tests/prague/eip7702_set_code_tx/test_set_code_transaction_fee_validations.json
```

### Probe 调试

设置 `EEST_PROBE=1` 时 `ExecuteViaEthAdapter` 会打印 status / gasUsed / stateRoot / sender 状态。

---

## 7. 工作区未提交内容

**已 commit：** 上述 3 个 commit，工作区无 staged 改动。

**未纳入 git（可忽略或按需处理）：**

| 路径 | 说明 |
|------|------|
| `bcos-evm/specs-tests/assets/eest/` | untracked；EEST fixture 树，通常由 fetch script 拉取 |
| `bcos-evm/specs-tests/assets/ethereum-tests` | submodule 指针变更（`?`） |
| `.superpowers/sdd/*`, `docs/superpowers/*`, `sdd/*` | SDD / plan 草稿，非产品代码 |
| `.codegraph/` | 本地索引 |

---

## 8. 建议后续工作（下一接手人）

### 高优先级 — stateRoot 大桶（~458）

1. 对 fail 样本做聚类（按 fixture 名 / expectException / gasUsed 差异）。
2. 重点怀疑区：
   - 7702 auth apply 顺序 vs refund 结算
   - GST adapter `applyGstTransactionSettlement` 与 geth coinbase/tip 路由
   - `buildPostStateView` / storage trie 与 geth 差异

### 中优先级 — included success status（~67）

- 可能仍有 vmerr normalize 边界或 7623 floor 与 geth 不一致。
- 参考 `EthIncludedTxVmerrTest` 与 `eth-eest-probe-*.json`。

### 低优先级 — 7623 / 7823 目录

- 7623：230/483 pass，calldata floor / intrinsic 边界
- 7823：0/21 pass，modexp upper bounds（Osaka kernel 已有，reference orchestration 可能缺 gate）

### 可选工程项

- 将 W1–W4 probe manifest 固化进 repo（session 中临时创建后又删除）
- 更新 capability-matrix 行：`ExecuteViaEthPreCheck` → 引用 `EthExecuteViaEthPreCheckTest`
- push 分支 + PR；CI 应绿 smoke（`capability-gate.yml`）

---

## 9. 参考对照（geth）

| 行为 | geth 位置 | 本仓库 |
|------|-----------|--------|
| preCheck sender code | `state_transition.go` | `ExecuteViaEthPreCheck.cpp` |
| empty auth list | type-4 validation | W2 |
| type-4 CREATE reject | preCheck | W3 |
| fee cap | `gasFeeCap >= tipCap, baseFee` | W4 |
| auth intrinsic | `IntrinsicGas` + 25000×n | `ExecuteViaEth.cpp` |
| included vmerr | `TransitionDb` + peak gas | ADR-015 + `EthTxGasSettlement.h` |

---

## 10. Session 时间线（简）

```
调查 7702/EEST vs geth 差异
  → P0 commit (3d0e4dc7e): gas + vmerr + storage + ADR-015 + smoke 扩展
  → P1 commit (d31194b41): stateRoot smoke + EthHost refund + vmerr 单测
  → P2: full baseline 1056/549 fail；tx-full 24→0 fail（strict decode）
  → W1–W4 precheck 实现 + 12/12 probe pass
  → P2+W commit (45c1c6c0f)
  → full manifest 537 fail（+12）；smoke 13/13 绿
```

---

*文档由 session 结束时自动生成，如有 baseline 数字漂移请以 `specs-tests/README.md` 与最新 full run 为准。*
