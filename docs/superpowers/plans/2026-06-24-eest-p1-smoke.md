# EEST P1 Smoke 扩展 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 扩展 `bcos-evm/specs-tests` EEST smoke 证据覆盖（6780×3、7702-core×6、tx×5），并将 8 个已有 probe manifest 注册为 nightly CTest，且 `ctest -L specs-tests-smoke` 保持全绿。

**Architecture:** 仅新增/修改 manifest JSON、在 `bcos-evm/specs-tests/CMakeLists.txt` 注册 `add_test`、更新 README 与 capability-matrix。**不修改** runner（`EthExecutionSpecStateTests.cpp`）、adapter（`EthReferenceBridgeAdapter.cpp`）或内核。复用现有 `EthExecutionSpecStateTests` / `EthExecutionSpecTransactionTests` 可执行文件。

**Tech Stack:** CMake 3.28+、CTest、Python 3（`manifest_lint.py`）、EEST v5.4.0（`upstream-pins.json`）、C++ specs-tests runners

**Spec:** `docs/superpowers/specs/2026-06-24-eest-p1-smoke-design.md`

## Global Constraints

- 门禁策略 **A（严格绿门禁）**：仅本地已验证全绿的用例进入 `specs-tests-smoke`
- 不修改 `EthExecutionSpecStateTests.cpp`、`EthReferenceBridgeAdapter.cpp`、`executeMessage.*`
- 不扩展 `capability-rows.json`（仅用现有 5 个 row id）
- 不修复 `eth-eest-state-smoke.json` 中 `self_sponsored` stateRoot 失败
- 7702 state smoke **不含** `stateRoot` assertLevel
- 6780 smoke 可含 `stateRoot`；合入前必须本地 3/3 绿，否则推迟 PR-3 或降级 assertLevels
- probe manifest **不修改 JSON 内容**；默认 label `specs-tests-full`
- 命令前缀使用 `rtk`（仓库 CLAUDE.md 规则）
- 构建目录假定 `build-ref`；configure 需 `-DTESTS=ON -DBCOS_EVM_SPECS_TESTS=ON`

---

## File Map

| 文件 | 操作 | 职责 |
|------|------|------|
| `bcos-evm/specs-tests/manifests/eth-eest-tx-smoke.json` | Modify | PR-1：1→5 条 tx smoke |
| `bcos-evm/specs-tests/manifests/eth-eest-7702-core-smoke.json` | Create | PR-2：6 条 7702 state |
| `bcos-evm/specs-tests/manifests/eth-eest-6780-smoke.json` | Create | PR-3：3 条 6780 state |
| `bcos-evm/specs-tests/CMakeLists.txt` | Modify | 注册新 CTest |
| `bcos-evm/specs-tests/README.md` | Modify | manifest 表 |
| `bcos-evm/capability-matrix.md` | Modify | Test ref 列 |
| `bcos-evm/specs-tests/manifests/eth-eest-*-probe*.json` | 不变 | PR-4 仅 CMake 接线 |

---

## Prerequisites（所有 Task 之前执行一次）

- [ ] **Step 1: Configure specs-tests build**

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor
CC=clang CXX=clang++ cmake -S . -B build-ref \
  -DCMAKE_BUILD_TYPE=Debug \
  -DTESTS=ON \
  -DBCOS_EVM_SPECS_TESTS=ON
```

Expected: configure 成功；日志含 `specs-tests EEST root: .../evm_ref_eest_fixtures-src`

- [ ] **Step 2: Build runners**

```bash
cmake --build build-ref --target EthExecutionSpecStateTests EthExecutionSpecTransactionTests ManifestLint -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
```

Expected: 三个 target 编译成功

- [ ] **Step 3: Baseline smoke（记录当前状态）**

```bash
rtk ctest -R ManifestLint --test-dir build-ref -C Debug
rtk ctest -L specs-tests-smoke --test-dir build-ref -C Debug --output-on-failure
```

Expected: `ManifestLint` PASS；smoke 除已知 `self_sponsored` stateRoot 失败外与当前分支一致（本计划不修复该问题）

---

### Task 1: PR-1 — 扩展 7702 transaction_tests smoke

**Files:**
- Modify: `bcos-evm/specs-tests/manifests/eth-eest-tx-smoke.json`
- Modify: `bcos-evm/capability-matrix.md`（`EIP-7702 tx field propagation` 行）
- Modify: `bcos-evm/specs-tests/README.md`（tx smoke 描述）

**Interfaces:**
- Consumes: 现有 `EthExecutionSpecTransactionTests` CTest（无需新 CMake）
- Produces: 5-entry `eth-eest-tx-smoke.json`；matrix Test ref 含 `eth-eest-tx-smoke.json`

- [ ] **Step 1: 替换 `eth-eest-tx-smoke.json` 全文**

```json
{
  "manifestVersion": 1,
  "entries": [
    {
      "evidenceId": "eth.eest.prague.tx.eip7702.invalid_rlp",
      "sourceSuite": "eest",
      "casePath": "fixtures/transaction_tests/prague/eip7702_set_code_tx/test_invalid_tx_invalid_rlp_encoding.json",
      "forkProfileId": "eth-prague",
      "path": "Reference",
      "evidenceKind": "ReferenceParity",
      "capabilityRowIds": ["eip7702-set-code-tx"],
      "assertLevels": ["transitional"]
    },
    {
      "evidenceId": "eth.eest.prague.tx.eip7702.empty_auth_list",
      "sourceSuite": "eest",
      "casePath": "fixtures/transaction_tests/prague/eip7702_set_code_tx/test_empty_authorization_list.json",
      "forkProfileId": "eth-prague",
      "path": "Reference",
      "evidenceKind": "ReferenceParity",
      "capabilityRowIds": ["eip7702-set-code-tx"],
      "assertLevels": ["transitional"]
    },
    {
      "evidenceId": "eth.eest.prague.tx.eip7702.invalid_auth_chain_id",
      "sourceSuite": "eest",
      "casePath": "fixtures/transaction_tests/prague/eip7702_set_code_tx/test_invalid_tx_invalid_auth_chain_id.json",
      "forkProfileId": "eth-prague",
      "path": "Reference",
      "evidenceKind": "ReferenceParity",
      "capabilityRowIds": ["eip7702-set-code-tx"],
      "assertLevels": ["transitional"]
    },
    {
      "evidenceId": "eth.eest.prague.tx.eip7702.invalid_nonce",
      "sourceSuite": "eest",
      "casePath": "fixtures/transaction_tests/prague/eip7702_set_code_tx/test_invalid_tx_invalid_nonce.json",
      "forkProfileId": "eth-prague",
      "path": "Reference",
      "evidenceKind": "ReferenceParity",
      "capabilityRowIds": ["eip7702-set-code-tx"],
      "assertLevels": ["transitional"]
    },
    {
      "evidenceId": "eth.eest.prague.tx.eip7702.invalid_auth_signature",
      "sourceSuite": "eest",
      "casePath": "fixtures/transaction_tests/prague/eip7702_set_code_tx/test_invalid_auth_signature.json",
      "forkProfileId": "eth-prague",
      "path": "Reference",
      "evidenceKind": "ReferenceParity",
      "capabilityRowIds": ["eip7702-set-code-tx"],
      "assertLevels": ["transitional"]
    }
  ]
}
```

- [ ] **Step 2: 更新 capability-matrix.md 第 54 行 Test ref**

将：
```
`EthTxInputBuilderTest`, `FiscoTxInputBuilderTest`, `Bcos7702ExecuteViaHostPropagationTest`, `OpStack7702ExecuteViaHostPropagationTest`, `OpStackTxInputBuilderTest`
```
改为：
```
`EthTxInputBuilderTest`, `FiscoTxInputBuilderTest`, `Bcos7702ExecuteViaHostPropagationTest`, `OpStack7702ExecuteViaHostPropagationTest`, `OpStackTxInputBuilderTest`, EEST `eth-eest-tx-smoke.json`
```

- [ ] **Step 3: 更新 README.md manifest 表**

将 `eth-eest-tx-smoke.json` 行 Scope 改为：
`Curated 7702 tx validation (RLP + empty auth + invalid chain id/nonce/signature)`

- [ ] **Step 4: Lint + 跑 tx smoke**

```bash
rtk ctest -R ManifestLint --test-dir build-ref -C Debug
rtk ctest -R EthExecutionSpecTransactionTests -V --test-dir build-ref -C Debug --output-on-failure
```

Expected:
- `manifest lint ok`
- 输出含 `All 5 EEST transaction test(s) passed`（或等价 5 PASS）

- [ ] **Step 5: Commit PR-1**

```bash
rtk git add bcos-evm/specs-tests/manifests/eth-eest-tx-smoke.json \
  bcos-evm/capability-matrix.md \
  bcos-evm/specs-tests/README.md
rtk git commit -m "$(cat <<'EOF'
test(specs-tests): expand EEST 7702 tx smoke to five invalid-tx vectors

EOF
)"
```

---

### Task 2: PR-2 — 7702 core state smoke

**Files:**
- Create: `bcos-evm/specs-tests/manifests/eth-eest-7702-core-smoke.json`
- Modify: `bcos-evm/specs-tests/CMakeLists.txt`（在 `EthExecutionSpecNonceTests` 块之后插入）
- Modify: `bcos-evm/specs-tests/README.md`
- Modify: `bcos-evm/capability-matrix.md`（`EIP-7702 authorization apply` 行）

**Interfaces:**
- Consumes: `EthExecutionSpecStateTests` binary、`EVM_REF_EEST_ROOT`、`EEST_MANIFEST_DIR`
- Produces: CTest 名 `EthExecutionSpec7702CoreSmoke`；label `specs-tests-smoke;eth-kernel;eest;7702`

- [ ] **Step 1: 创建 `eth-eest-7702-core-smoke.json`**

```json
{
  "manifestVersion": 1,
  "entries": [
    {
      "evidenceId": "eth.eest.prague.state.eip7702.eip7702",
      "sourceSuite": "eest",
      "casePath": "fixtures/state_tests/prague/eip7702_set_code_tx/test_eip_7702.json",
      "forkProfileId": "eth-osaka",
      "path": "Reference",
      "evidenceKind": "ReferenceParity",
      "capabilityRowIds": ["eip7702-set-code-tx"],
      "assertLevels": ["transitional", "expectException"]
    },
    {
      "evidenceId": "eth.eest.prague.state.eip7702.empty_auth_list",
      "sourceSuite": "eest",
      "casePath": "fixtures/state_tests/prague/eip7702_set_code_tx/test_empty_authorization_list.json",
      "forkProfileId": "eth-osaka",
      "path": "Reference",
      "evidenceKind": "ReferenceParity",
      "capabilityRowIds": ["eip7702-set-code-tx"],
      "assertLevels": ["transitional", "expectException"]
    },
    {
      "evidenceId": "eth.eest.prague.state.eip7702.contract_create",
      "sourceSuite": "eest",
      "casePath": "fixtures/state_tests/prague/eip7702_set_code_tx/test_contract_create.json",
      "forkProfileId": "eth-osaka",
      "path": "Reference",
      "evidenceKind": "ReferenceParity",
      "capabilityRowIds": ["eip7702-set-code-tx"],
      "assertLevels": ["transitional", "expectException"]
    },
    {
      "evidenceId": "eth.eest.prague.state.eip7702.delegation_clearing",
      "sourceSuite": "eest",
      "casePath": "fixtures/state_tests/prague/eip7702_set_code_tx/test_delegation_clearing.json",
      "forkProfileId": "eth-osaka",
      "path": "Reference",
      "evidenceKind": "ReferenceParity",
      "capabilityRowIds": ["eip7702-set-code-tx"],
      "assertLevels": ["transitional", "expectException"]
    },
    {
      "evidenceId": "eth.eest.prague.state.eip7702.eoa_init_pointer",
      "sourceSuite": "eest",
      "casePath": "fixtures/state_tests/prague/eip7702_set_code_tx/test_eoa_init_as_pointer.json",
      "forkProfileId": "eth-osaka",
      "path": "Reference",
      "evidenceKind": "ReferenceParity",
      "capabilityRowIds": ["eip7702-set-code-tx"],
      "assertLevels": ["transitional", "expectException"]
    },
    {
      "evidenceId": "eth.eest.prague.state.eip7702.nonce_overflow_auth",
      "sourceSuite": "eest",
      "casePath": "fixtures/state_tests/prague/eip7702_set_code_tx/test_nonce_overflow_after_first_authorization.json",
      "forkProfileId": "eth-osaka",
      "path": "Reference",
      "evidenceKind": "ReferenceParity",
      "capabilityRowIds": ["eip7702-set-code-tx"],
      "assertLevels": ["transitional", "expectException"]
    }
  ]
}
```

- [ ] **Step 2: 在 `CMakeLists.txt` 第 180 行后插入**

```cmake
add_test(NAME EthExecutionSpec7702CoreSmoke COMMAND EthExecutionSpecStateTests
    --manifest ${EEST_MANIFEST_DIR}/eth-eest-7702-core-smoke.json
    --eest-root ${EVM_REF_EEST_ROOT}
    --expectations ${EEST_MANIFEST_DIR}/expectations.json
)
set_tests_properties(EthExecutionSpec7702CoreSmoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel;eest;7702"
)
```

- [ ] **Step 3: Re-configure（CMake 变更后必须）**

```bash
CC=clang CXX=clang++ cmake -S . -B build-ref \
  -DCMAKE_BUILD_TYPE=Debug -DTESTS=ON -DBCOS_EVM_SPECS_TESTS=ON
```

- [ ] **Step 4: 跑 7702 core smoke**

```bash
rtk ctest -R EthExecutionSpec7702CoreSmoke -V --test-dir build-ref -C Debug --output-on-failure
```

Expected: 全部 PASS，无 `stateRoot mismatch`

**若失败且因 subtest 过多/超时：** 对失败 entry 从 EEST JSON 顶层 key 复制 `variantKey`（参考 `eth-eest-state-smoke.json` 中 self_sponsored 条目格式），仅跑单变体后重试。

- [ ] **Step 5: 更新 README manifest 表**

新增行：
```
| `eth-eest-7702-core-smoke.json` | 7702 core state (eip7702, empty auth, create, delegation, pointer, nonce overflow) | `specs-tests-smoke`, `7702` |
```

- [ ] **Step 6: 更新 capability-matrix.md 第 53 行 Test ref**

在末尾追加：`, EEST `eth-eest-7702-core-smoke.json``

- [ ] **Step 7: Lint + smoke 回归**

```bash
rtk ctest -R ManifestLint --test-dir build-ref -C Debug
rtk ctest -L specs-tests-smoke --test-dir build-ref -C Debug --output-on-failure
```

- [ ] **Step 8: Commit PR-2**

```bash
rtk git add bcos-evm/specs-tests/manifests/eth-eest-7702-core-smoke.json \
  bcos-evm/specs-tests/CMakeLists.txt \
  bcos-evm/specs-tests/README.md \
  bcos-evm/capability-matrix.md
rtk git commit -m "$(cat <<'EOF'
test(specs-tests): add EEST 7702 core state smoke manifest

EOF
)"
```

---

### Task 3: PR-3 — EIP-6780 SELFDESTRUCT smoke（合入前门禁）

**Files:**
- Create: `bcos-evm/specs-tests/manifests/eth-eest-6780-smoke.json`
- Modify: `bcos-evm/specs-tests/CMakeLists.txt`
- Modify: `bcos-evm/specs-tests/README.md`
- Modify: `bcos-evm/capability-matrix.md`（第 80 行 `EIP-6780 SELFDESTRUCT`）

**Interfaces:**
- Consumes: `EthExecutionSpecStateTests`、`forkProfileId: eth-cancun`
- Produces: CTest 名 `EthExecutionSpec6780Smoke`；label `specs-tests-smoke;eth-kernel;eest;6780`

- [ ] **Step 1: 创建 `eth-eest-6780-smoke.json`**

```json
{
  "manifestVersion": 1,
  "entries": [
    {
      "evidenceId": "eth.eest.cancun.eip6780.not_created_revert",
      "sourceSuite": "eest",
      "casePath": "fixtures/state_tests/cancun/eip6780_selfdestruct/test_selfdestruct_not_created_in_same_tx_with_revert.json",
      "forkProfileId": "eth-cancun",
      "path": "Reference",
      "evidenceKind": "ReferenceParity",
      "capabilityRowIds": ["eip6780-selfdestruct-kernel"],
      "assertLevels": ["transitional", "expectException", "stateRoot"]
    },
    {
      "evidenceId": "eth.eest.cancun.eip6780.created_revert",
      "sourceSuite": "eest",
      "casePath": "fixtures/state_tests/cancun/eip6780_selfdestruct/test_selfdestruct_created_in_same_tx_with_revert.json",
      "forkProfileId": "eth-cancun",
      "path": "Reference",
      "evidenceKind": "ReferenceParity",
      "capabilityRowIds": ["eip6780-selfdestruct-kernel"],
      "assertLevels": ["transitional", "expectException", "stateRoot"]
    },
    {
      "evidenceId": "eth.eest.cancun.eip6780.initcode_create_tx",
      "sourceSuite": "eest",
      "casePath": "fixtures/state_tests/cancun/eip6780_selfdestruct/test_self_destructing_initcode_create_tx.json",
      "forkProfileId": "eth-cancun",
      "path": "Reference",
      "evidenceKind": "ReferenceParity",
      "capabilityRowIds": ["eip6780-selfdestruct-kernel"],
      "assertLevels": ["transitional", "expectException", "stateRoot"]
    }
  ]
}
```

- [ ] **Step 2: 在 `CMakeLists.txt` `EthExecutionSpec7702CoreSmoke` 块之后插入**

```cmake
add_test(NAME EthExecutionSpec6780Smoke COMMAND EthExecutionSpecStateTests
    --manifest ${EEST_MANIFEST_DIR}/eth-eest-6780-smoke.json
    --eest-root ${EVM_REF_EEST_ROOT}
    --expectations ${EEST_MANIFEST_DIR}/expectations.json
)
set_tests_properties(EthExecutionSpec6780Smoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel;eest;6780"
)
```

- [ ] **Step 3: Re-configure + 跑 6780 smoke**

```bash
CC=clang CXX=clang++ cmake -S . -B build-ref \
  -DCMAKE_BUILD_TYPE=Debug -DTESTS=ON -DBCOS_EVM_SPECS_TESTS=ON
rtk ctest -R EthExecutionSpec6780Smoke -V --test-dir build-ref -C Debug --output-on-failure
```

Expected: `All N EEST state subtest(s) passed` 且 N≥3

**若 stateRoot 失败（策略 A 分支）：**
1. 将三条 entry 的 `assertLevels` 改为 `["transitional","expectException"]`
2. 在 README 加脚注：`6780 stateRoot parity pending`
3. **不要**合入 smoke label 直至绿；或推迟 PR-3

**调试可选：**
```bash
EEST_PROBE=1 rtk ctest -R EthExecutionSpec6780Smoke -V --test-dir build-ref -C Debug
```

- [ ] **Step 4: 更新 README + capability-matrix**

README 新增行：
```
| `eth-eest-6780-smoke.json` | Cancun EIP-6780 same-tx SELFDESTRUCT (3 vectors) | `specs-tests-smoke`, `6780` |
```

capability-matrix 第 80 行 Test ref 末尾追加：`, EEST `eth-eest-6780-smoke.json``

- [ ] **Step 5: Lint + smoke 回归**

```bash
rtk ctest -R ManifestLint --test-dir build-ref -C Debug
rtk ctest -L specs-tests-smoke --test-dir build-ref -C Debug --output-on-failure
```

- [ ] **Step 6: Commit PR-3**

```bash
rtk git add bcos-evm/specs-tests/manifests/eth-eest-6780-smoke.json \
  bcos-evm/specs-tests/CMakeLists.txt \
  bcos-evm/specs-tests/README.md \
  bcos-evm/capability-matrix.md
rtk git commit -m "$(cat <<'EOF'
test(specs-tests): add EEST EIP-6780 selfdestruct smoke manifest

EOF
)"
```

---

### Task 4: PR-4 — Probe manifest 注册为 nightly

**Files:**
- Modify: `bcos-evm/specs-tests/CMakeLists.txt`（在 `ManifestLint` 之前插入 8 条 `add_test`）
- Modify: `bcos-evm/specs-tests/README.md`

**Interfaces:**
- Consumes: 已有 8 个 probe manifest（内容不变）
- Produces: 8 个 CTest，label 均为 `specs-tests-full`（`precompile-probe`/`opcode-probe` 若 Step 2 全绿可改 smoke）

- [ ] **Step 1: 本地探测 precompile/opcode probe（决定是否 smoke）**

```bash
build-ref/bcos-evm/specs-tests/EthExecutionSpecStateTests \
  --manifest bcos-evm/specs-tests/manifests/eth-eest-precompile-probe.json \
  --eest-root build-ref/_deps/evm_ref_eest_fixtures-src \
  --expectations bcos-evm/specs-tests/manifests/expectations.json

build-ref/bcos-evm/specs-tests/EthExecutionSpecStateTests \
  --manifest bcos-evm/specs-tests/manifests/eth-eest-opcode-probe.json \
  --eest-root build-ref/_deps/evm_ref_eest_fixtures-src \
  --expectations bcos-evm/specs-tests/manifests/expectations.json
```

记录 PASS/FAIL。全绿 → Step 3 中对应用 `specs-tests-smoke`；否则 → `specs-tests-full`。

- [ ] **Step 2: 在 `CMakeLists.txt` 第 202 行（`add_test(NAME ManifestLint`）之前插入**

```cmake
# --- EEST probe manifests (nightly by default; precompile/opcode may promote to smoke) ---
set(_evm_eest_probe_manifests
    eth-eest-probe-revert.json
    eth-eest-probe-return.json
    eth-eest-probe-oog.json
    eth-eest-probe-invalid.json
    eth-eest-precompile-probe-full.json
    probe-gas-cost-one.json
    eth-eest-precompile-probe.json
    eth-eest-opcode-probe.json
)
set(_evm_eest_probe_ctest_names
    EthExecutionSpecProbeRevert
    EthExecutionSpecProbeReturn
    EthExecutionSpecProbeOog
    EthExecutionSpecProbeInvalid
    EthExecutionSpecPrecompileProbeFull
    EthExecutionSpecProbeGasCostOne
    EthExecutionSpecPrecompileProbe
    EthExecutionSpecOpcodeProbe
)
list(LENGTH _evm_eest_probe_manifests _evm_eest_probe_count)
math(EXPR _evm_eest_probe_last "${_evm_eest_probe_count} - 1")
foreach(i RANGE ${_evm_eest_probe_last})
    list(GET _evm_eest_probe_manifests ${i} _probe_manifest)
    list(GET _evm_eest_probe_ctest_names ${i} _probe_ctest)
    add_test(NAME ${_probe_ctest} COMMAND EthExecutionSpecStateTests
        --manifest ${EEST_MANIFEST_DIR}/${_probe_manifest}
        --eest-root ${EVM_REF_EEST_ROOT}
        --expectations ${EEST_MANIFEST_DIR}/expectations.json
    )
    set(_probe_labels "specs-tests;specs-tests-full;eth-kernel;eest;probe")
  # 若 Step 1 precompile/opcode 全绿，将下面两行取消注释并删除默认 full label 逻辑：
  #  if(_probe_ctest STREQUAL "EthExecutionSpecPrecompileProbe" OR _probe_ctest STREQUAL "EthExecutionSpecOpcodeProbe")
  #    set(_probe_labels "specs-tests;specs-tests-smoke;eth-kernel;eest;probe")
  #  endif()
    set_tests_properties(${_probe_ctest} PROPERTIES LABELS "${_probe_labels}")
endforeach()
```

- [ ] **Step 3: Re-configure**

```bash
CC=clang CXX=clang++ cmake -S . -B build-ref \
  -DCMAKE_BUILD_TYPE=Debug -DTESTS=ON -DBCOS_EVM_SPECS_TESTS=ON
```

- [ ] **Step 4: 验证 probe 已注册（不要求全绿）**

```bash
rtk ctest -N --test-dir build-ref -C Debug | rg 'EthExecutionSpecProbe|PrecompileProbe|OpcodeProbe|ProbeGas'
rtk ctest -L specs-tests-full --test-dir build-ref -C Debug -R 'Probe|PrecompileProbe|OpcodeProbe' --output-on-failure
```

Expected: 8 个测试名出现在 ctest 列表；full 运行允许失败（parity 雷达）

- [ ] **Step 5: 更新 README**

在 manifest 表后增加小节 **Probe manifests (nightly)**：

```
| `eth-eest-probe-*.json`, `eth-eest-precompile-probe*.json`, `probe-gas-cost-one.json` | 7702 行为/gas 切片探测 | `specs-tests-full`, `probe` |
```

- [ ] **Step 6: 确认 smoke 仍绿**

```bash
rtk ctest -L specs-tests-smoke --test-dir build-ref -C Debug --output-on-failure
```

- [ ] **Step 7: Commit PR-4**

```bash
rtk git add bcos-evm/specs-tests/CMakeLists.txt bcos-evm/specs-tests/README.md
rtk git commit -m "$(cat <<'EOF'
test(specs-tests): register EEST probe manifests as nightly CTests

EOF
)"
```

---

### Task 5: 最终验收（全计划 Done）

- [ ] **Step 1: ManifestLint**

```bash
rtk ctest -R ManifestLint --test-dir build-ref -C Debug
```

Expected: `manifest lint ok`

- [ ] **Step 2: Smoke 全量**

```bash
rtk ctest -L specs-tests-smoke --test-dir build-ref -C Debug --output-on-failure
```

Expected: 全部 PASS（含 `EthExecutionSpec7702CoreSmoke`、`EthExecutionSpec6780Smoke`、扩展后的 `EthExecutionSpecTransactionTests`）

- [ ] **Step 3: 核对 Done 清单（对照 spec §3.3）**

| 项 | 命令/证据 |
|----|-----------|
| 6780×3 | `ctest -R EthExecutionSpec6780Smoke` PASS |
| 7702-core×6 | `ctest -R EthExecutionSpec7702CoreSmoke` PASS |
| tx×5 | `ctest -R EthExecutionSpecTransactionTests` PASS（非 Full） |
| probe≥6 full | `ctest -N \| rg Probe` 显示 8 条 |
| README | manifest 表含 6780、7702-core、probe |
| matrix | 6780、7702 tx、7702 apply 行已更新 |

- [ ] **Step 4: Full 探测（可选，记录 baseline）**

```bash
rtk ctest -L specs-tests-full --test-dir build-ref -C Debug --output-on-failure | tail -20
```

---

## Spec Coverage Self-Review

| Spec § | Task |
|--------|------|
| P1-1 probe CTest | Task 4 |
| P1-2 6780 smoke | Task 3 |
| P1-3 7702-core smoke | Task 2 |
| P1-4 tx smoke | Task 1 |
| CMake 注册 | Task 2–4 |
| README | Task 1–4 |
| capability-matrix | Task 1–3 |
| 策略 A 绿门禁 | 各 Task 验证步骤 |
| 不改 runner/adapter | 全计划无 C++ 修改 |
| 不扩 capability-rows | 仅用现有 5 row |

无 TBD/占位符；PR 顺序 Task 1→2→3→4 与 spec §8 一致。

---

## 回滚

删除对应 manifest + CMake `add_test` 块 + README/matrix 行；`git revert` 各 PR commit。无内核副作用。
