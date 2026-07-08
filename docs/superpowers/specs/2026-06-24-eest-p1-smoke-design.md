# EEST P1 Smoke 扩展 — 设计规格

**日期：** 2026-06-24  
**状态：** 已批准（门禁策略 A：严格绿门禁）  
**范围：** specs-tests 第一优先级 — manifest + CMake + 文档；不改 runner/adapter/内核

---

## 1. 背景与动机

`bcos-evm/specs-tests` 已通过 EEST v5.4.0 向量验证 ETH Reference 路径（`ethReferenceExecute`）。当前 smoke 覆盖偏窄：

- state：`eth-eest-state-smoke.json`（7623/7823/7702 warming + self_sponsored + invalid auth）
- tx：`eth-eest-tx-smoke.json`（1 条 7702 invalid RLP）
- 专题：`nonce-smoke`、`1559-gasprice-probe`

仓库内另有 **8 个 probe manifest 已编写但未注册 CTest**。第一优先级目标是在**不扩大 runner 架构**的前提下扩展证据覆盖，且 **PR smoke（`specs-tests-smoke`）保持全绿**。

已知风险：`eth-eest-state-smoke.json` 中 `self_sponsored.*` 带 `stateRoot` 的 10 条仍失败；本 spec **不包含**该 parity 修复。

---

## 2. 门禁策略（已决策）

**策略 A — 严格绿门禁：**

- 仅本地已验证全绿的用例进入 `specs-tests-smoke`
- 带 `stateRoot` 且可能失败的 probe → `specs-tests-full`（nightly）
- 新建 7702 state smoke 使用 `transitional` + `expectException`，**不含 stateRoot**
- 6780 smoke 可声明 `stateRoot`，但 **合入前必须本地 3/3 绿**；否则推迟或降级 assertLevels

---

## 3. 范围

### 3.1 在范围内

| 编号 | 交付物 | 说明 |
|------|--------|------|
| P1-1 | 8 个已有 probe manifest 接 CTest | 默认 label `specs-tests-full` |
| P1-2 | `eth-eest-6780-smoke.json` | 3 条 Cancun EIP-6780 |
| P1-3 | `eth-eest-7702-core-smoke.json` | 6 条小文件 7702 state |
| P1-4 | 扩展 `eth-eest-tx-smoke.json` | 1 → 5 条 7702 transaction_tests |
| — | `CMakeLists.txt` | 新 `add_test` 注册 |
| — | `README.md` | manifest 表更新 |
| — | `capability-matrix.md` | Test ref 补 EEST manifest 名 |

### 3.2 不在范围内

- 修改 `EthExecutionSpecStateTests.cpp`、`EthReferenceBridgeAdapter.cpp`、`executeMessage` 等执行逻辑
- 扩展 `capability-rows.json`（复用现有 5 个 row id）
- 修复 `self_sponsored` stateRoot parity
- 接入 `blockchain_tests`、巨型 gas fixture（`test_gas_cost.json` 等）进 smoke
- runner 自动扫描 manifest 目录（方案 3，留待后续）

### 3.3 Done 标准

- [ ] `ctest -L specs-tests-smoke` 全绿
- [ ] `ctest -R ManifestLint` 通过
- [ ] 新 smoke 用例：6780×3 + 7702-core×6 + tx×5 已注册
- [ ] probe manifest 已注册为 `specs-tests-full`（至少 6 条 CTest）
- [ ] README manifest 表与 CMake label 一致
- [ ] capability-matrix 相关行 Test ref 已更新

---

## 4. 架构

### 4.1 组件边界（无新组件）

```
manifest/*.json  ──►  EthExecutionSpecStateTests / EthExecutionSpecTransactionTests
                              │
                              ▼
                    EthReferenceBridgeAdapter（不变）
                              │
                              ▼
                    ethReferenceExecute → runOrchestration → executeMessage（不变）
```

P1 仅变更「考纲（manifest）」与「阅卷清单（CTest）」。

### 4.2 Manifest 组织（已选方案 1）

每个新专题使用**独立 manifest + 独立 CTest 名**，与 `eth-eest-nonce-smoke.json` / `EthExecutionSpecNonceTests` 模式一致。不合并进 `eth-eest-state-smoke.json`。

---

## 5. Manifest 规格

### 5.1 新建 `eth-eest-6780-smoke.json`

| 字段 | 值 |
|------|-----|
| `forkProfileId` | `eth-cancun` |
| `capabilityRowIds` | `["eip6780-selfdestruct-kernel"]` |
| `assertLevels` | `["transitional","expectException","stateRoot"]` |
| `path` | `Reference` |
| `evidenceKind` | `ReferenceParity` |
| `sourceSuite` | `eest` |

| evidenceId | casePath（相对 EEST root） |
|------------|---------------------------|
| `eth.eest.cancun.eip6780.not_created_revert` | `fixtures/state_tests/cancun/eip6780_selfdestruct/test_selfdestruct_not_created_in_same_tx_with_revert.json` |
| `eth.eest.cancun.eip6780.created_revert` | `fixtures/state_tests/cancun/eip6780_selfdestruct/test_selfdestruct_created_in_same_tx_with_revert.json` |
| `eth.eest.cancun.eip6780.initcode_create_tx` | `fixtures/state_tests/cancun/eip6780_selfdestruct/test_self_destructing_initcode_create_tx.json` |

### 5.2 新建 `eth-eest-7702-core-smoke.json`

| 字段 | 值 |
|------|-----|
| `forkProfileId` | `eth-osaka` |
| `capabilityRowIds` | `["eip7702-set-code-tx"]` |
| `assertLevels` | `["transitional","expectException"]` |

| evidenceId | casePath |
|------------|----------|
| `eth.eest.prague.state.eip7702.eip7702` | `fixtures/state_tests/prague/eip7702_set_code_tx/test_eip_7702.json` |
| `eth.eest.prague.state.eip7702.empty_auth_list` | `.../test_empty_authorization_list.json` |
| `eth.eest.prague.state.eip7702.contract_create` | `.../test_contract_create.json` |
| `eth.eest.prague.state.eip7702.delegation_clearing` | `.../test_delegation_clearing.json` |
| `eth.eest.prague.state.eip7702.eoa_init_pointer` | `.../test_eoa_init_as_pointer.json` |
| `eth.eest.prague.state.eip7702.nonce_overflow_auth` | `.../test_nonce_overflow_after_first_authorization.json` |

无 `variantKey`（整文件加载）。若 CI 超时，为对应 entry 补 `variantKey`。

### 5.3 扩展 `eth-eest-tx-smoke.json`

保留现有 `test_invalid_tx_invalid_rlp_encoding.json` 条目，新增 4 条：

| evidenceId | casePath |
|------------|----------|
| `eth.eest.prague.tx.eip7702.empty_auth_list` | `fixtures/transaction_tests/prague/eip7702_set_code_tx/test_empty_authorization_list.json` |
| `eth.eest.prague.tx.eip7702.invalid_auth_chain_id` | `.../test_invalid_tx_invalid_auth_chain_id.json` |
| `eth.eest.prague.tx.eip7702.invalid_nonce` | `.../test_invalid_tx_invalid_nonce.json` |
| `eth.eest.prague.tx.eip7702.invalid_auth_signature` | `.../test_invalid_auth_signature.json` |

统一字段：`forkProfileId: eth-prague`，`capabilityRowIds: ["eip7702-set-code-tx"]`，`assertLevels: ["transitional"]`。

现有 RLP 条目应将 `capabilityRowIds` 从 `eip2929-runtime-warm` 更正为 `eip7702-set-code-tx`。

### 5.4 已有 probe manifest（P1-1）

| Manifest | CTest label | 备注 |
|----------|-------------|------|
| `eth-eest-precompile-probe.json` | 本地验证后决定 smoke 或 full | 含 stateRoot + logsHash |
| `eth-eest-opcode-probe.json` | 同上 | 同上 |
| `eth-eest-probe-revert.json` | `specs-tests-full` | 与 failing self_sponsored 同类 |
| `eth-eest-probe-return.json` | `specs-tests-full` | 同上 |
| `eth-eest-probe-oog.json` | `specs-tests-full` | 同上 |
| `eth-eest-probe-invalid.json` | `specs-tests-full` | 同上 |
| `eth-eest-precompile-probe-full.json` | `specs-tests-full` | 整文件多 variant |
| `probe-gas-cost-one.json` | `specs-tests-full` | 大文件 + stateRoot |

**不修改 probe JSON 内容**；通过 CTest label 分流，避免用降级 assertLevels 掩盖 parity 问题。

若 `precompile-probe` / `opcode-probe` 本地全绿，可升格为 `specs-tests-smoke`。

---

## 6. CMake / CTest

在 `bcos-evm/specs-tests/CMakeLists.txt` 的 `EEST_MANIFEST_DIR` 块内新增：

| CTest 名 | 可执行文件 | Manifest | LABELS |
|----------|-----------|----------|--------|
| `EthExecutionSpec6780Smoke` | `EthExecutionSpecStateTests` | `eth-eest-6780-smoke.json` | `specs-tests-smoke;eth-kernel;eest;6780` |
| `EthExecutionSpec7702CoreSmoke` | `EthExecutionSpecStateTests` | `eth-eest-7702-core-smoke.json` | `specs-tests-smoke;eth-kernel;eest;7702` |
| `EthExecutionSpecProbeRevert` 等 | `EthExecutionSpecStateTests` | 各 probe json | `specs-tests-full;eth-kernel;eest;probe` |

`EthExecutionSpecTransactionTests` 无需新 target；扩展 manifest 后原 CTest 自动多跑条目。

**capability-gate**（`.github/workflows/capability-gate.yml`）无需修改：仍 `ctest -L specs-tests-smoke`。

---

## 7. 文档

| 文件 | 变更 |
|------|------|
| `bcos-evm/specs-tests/README.md` | manifest 表增加 6780-smoke、7702-core-smoke；注明 probe → full |
| `bcos-evm/capability-matrix.md` | `EIP-6780 SELFDESTRUCT`、`EIP-7702 tx field propagation` 等行 Test ref 补 manifest 名 |

不修改：`schema.json`、`capability-rows.json`、`expectations.json`。

---

## 8. PR 拆分与实施顺序

| PR | 内容 | 预期风险 |
|----|------|----------|
| PR-1 | P1-4 tx smoke 扩展 + matrix ref | 低 |
| PR-2 | P1-3 7702-core smoke + CMake | 低 |
| PR-3 | P1-2 6780 smoke + CMake（合入前 3/3 绿） | 中 |
| PR-4 | P1-1 probe → full CTest | 低（不进 PR gate） |

每 PR 合入前：

```bash
ctest -R ManifestLint --test-dir build-ref
ctest -L specs-tests-smoke --test-dir build-ref -C Debug --output-on-failure
```

---

## 9. 验收与回滚

**验收：** 见 §3.3 Done 标准。

**回滚：** 删除对应 manifest 条目与 CMake `add_test` 块；无内核副作用。

**后续（本 spec 外）：**

- `self_sponsored` stateRoot 修复后，probe 可升格 smoke
- 7702-core 稳定后，可选为条目增加 `stateRoot` assertLevel

---

## 10. 风险

| 风险 | 缓解 |
|------|------|
| 6780 stateRoot 失败 | PR-3 合入前本地验证；失败则推迟或仅 transitional |
| 7702 小文件多变体 CI 慢 | 补 variantKey 或 `--limit` |
| 现有 state-smoke 10 条红 | 不在本 spec 修复；不新增 stateRoot 到 smoke |
| probe 误标 smoke | 默认 full label；升格需本地先绿 |

---

## 11. 参考

- `bcos-evm/specs-tests/README.md` — 现有 manifest 表与 full-run baseline
- `bcos-evm/specs-tests/manifests/schema.json` — entry 字段约束
- `bcos-evm/capability-matrix.md` — capability row 语义
- EEST pin：`bcos-evm/specs-tests/assets/upstream-pins.json` v5.4.0
