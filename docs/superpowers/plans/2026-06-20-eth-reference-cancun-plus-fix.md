# ETH Reference CANCUN+ 审计阻断项修复 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 单次 PR 关闭审计报告 7 项 🔴，使 ETH reference CANCUN+ 路径 Part 1 无阻断；BCOS `executeViaHost` 通过共享 `executeMessage()` 自动继承 kernel 修复。

**Architecture:** 集中修改 `bcos-evm/eth/**`（`EthPolicy`、`EthHost`、`EthPrecompiles`、`executeMessage`）；引入共享 `PrecompileActive.h` + `BlsGas.h`；每项 P0 配 ETH + BCOS 继承证明测试。不复制 kernel 到 `bcos/`。

**Tech Stack:** C++20、evmone、Boost.Test、bcos-evm、`executeViaEth` / `executeViaHost`

## Global Constraints

- **设计 spec：** `docs/superpowers/specs/2026-06-20-eth-reference-cancun-plus-fix-design.md`
- **审计报告：** `bcos-evm/docs/audits/2026-06-20-eth-reference-cancun-plus-audit.md`
- **路径：** 仅改 `bcos-evm/eth/**` + `bcos-evm/test/**` + `bcos-evm/capability-matrix.md`；不改 `bcos/FiscoHostExtension` 内核逻辑
- **继承：** BCOS 测试经 `executeViaHost`；显式开启 `feature_evm_prague` / `feature_evm_osaka`
- **EthPolicy：** reference 路径 PRAGUE+ `eip7702=true`（无 feature flag）
- **金标准：** geth v1.17.3；2537 k=2 G1MSM gas = **22776**
- **命令前缀：** `rtk`（仓库 CLAUDE.md）
- **提交：** 每 Task 一次 logical commit（同一 PR）

---

## File Map

| 文件 | 职责 |
|------|------|
| `eth/vm/EthPolicy.h` | P0-1：`eip7702` profile |
| `eth/precompiled/PrecompileActive.h`（新建） | P0-6：统一 `isActivePrecompile` |
| `eth/precompiled/BlsGas.h`（新建） | P0-3：128 项 MSM 折扣表 |
| `eth/state/EthHost.hpp` / `.cpp` | P0-2 6780、P0-6 门控、CREATE 集 |
| `eth/executeMessage.cpp` | P0-2 CREATE 注册、P0-5/6 预编译短路 |
| `eth/state/EthPrecompiles.hpp` / `.cpp` | P0-3/4/5 dispatch + gas |
| `eth/precompiled/EthBuiltinRegistry.cpp` | P0-3 DRY 引用 `BlsGas.h` |
| `test/eth/*` | ETH reference 证明测试 |
| `test/bcos/*` | BCOS baseline 继承证明测试 |
| `test/fixtures/EthFixtureAdapter.h` | `eip7702` fixture profile |
| `test/fixtures/FixtureAssert.h` | 6780 post-state 断言扩展 |
| `test/CMakeLists.txt` | 新 test target 注册 |
| `capability-matrix.md` | P2 脚注同步 |

## 实施顺序（依赖）

```
Task 1 P0-1 7702
Task 2 P0-6 门控（Task 5 前置）
Task 3 P0-3 BlsGas MSM
Task 4 P0-5 7823 modexp
Task 5 P0-4 7212 p256
Task 6 P0-2 6780 selfdestruct
Task 7 matrix + 全量回归
```

---

### Task 1: P0-1 · EIP-7702 revision enable

**Files:**
- Modify: `bcos-evm/eth/vm/EthPolicy.h`
- Modify: `bcos-evm/test/eth/RevisionConfigProfileTest.cpp`
- Modify: `bcos-evm/test/fixtures/EthFixtureAdapter.h`
- Create: `bcos-evm/test/eth/Eip7702ApplyAuthorizationEthTest.cpp`
- Modify: `bcos-evm/test/bcos/Bcos7702ExecuteViaHostPropagationTest.cpp`（增 feature flag 用例，可选）
- Modify: `bcos-evm/test/CMakeLists.txt`

**Interfaces — Produces:**
- `EthPolicy::computeRevisionConfig` 在 PRAGUE+ 设 `eip7702=true`
- `makePragueRevisionConfig()` 含 `eip7702=true`

- [ ] **Step 1: 写失败测试 — RevisionConfigProfileTest**

在 `RevisionConfigProfileTest.cpp` PRAGUE/OSAKA 用例增加：

```cpp
BOOST_CHECK(cfg.eip7702);
```

运行：

```bash
cd build && cmake --build . --target RevisionConfigProfileTest -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
./bcos-evm/test/RevisionConfigProfileTest
```

Expected: **FAIL**（当前 `eip7702` 为 false）

- [ ] **Step 2: 实现 EthPolicy 赋值**

`EthPolicy.h` 在 `cfg.eip7623` 行后添加：

```cpp
cfg.eip7702 = cfg.revision >= EVMC_PRAGUE;
```

- [ ] **Step 3: 更新 EthFixtureAdapter**

`makePragueRevisionConfig()` 添加：

```cpp
cfg.eip7702 = true;
```

- [ ] **Step 4: 新建 ETH apply 测试**

创建 `bcos-evm/test/eth/Eip7702ApplyAuthorizationEthTest.cpp`（模式同 `Bcos7702ExecuteViaHostPropagationTest.cpp`，但直调 `executeMessage`）：

```cpp
#define BOOST_TEST_MODULE Eip7702ApplyAuthorizationEthTest
#include "bcos-evm/eth/executeMessage.h"
#include "state/InMemoryStateView.h"
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

BOOST_AUTO_TEST_CASE(apply_authorization_via_executeMessage_prague)
{
    // sender 0x31, delegation target 0x42, eip7702=true, authorizationListPresent=true
    // 断言 stateDiff: sender code 23 bytes EF0100||0x42, nonce+1
}
```

CMake 追加：

```cmake
add_executable(Eip7702ApplyAuthorizationEthTest eth/Eip7702ApplyAuthorizationEthTest.cpp)
target_include_directories(Eip7702ApplyAuthorizationEthTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Eip7702ApplyAuthorizationEthTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME Eip7702ApplyAuthorizationEth COMMAND Eip7702ApplyAuthorizationEthTest)
```

- [ ] **Step 5: 运行 ETH + BCOS 7702 测试**

```bash
./bcos-evm/test/RevisionConfigProfileTest
./bcos-evm/test/Eip7702ApplyAuthorizationEthTest
./bcos-evm/test/Bcos7702ExecuteViaHostPropagationTest
```

Expected: **PASS**

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/eth/vm/EthPolicy.h bcos-evm/test/
rtk git commit -m "fix(eth): enable eip7702 in EthPolicy at PRAGUE+ (P0-1)"
```

---

### Task 2: P0-6 · Prague 预编译 revision 门控

**Files:**
- Create: `bcos-evm/eth/precompiled/PrecompileActive.h`
- Modify: `bcos-evm/eth/state/EthHost.cpp`, `EthHost.hpp`
- Modify: `bcos-evm/eth/executeMessage.cpp`
- Create: `bcos-evm/test/eth/EipPrecompileRevisionGateTest.cpp`
- Create: `bcos-evm/test/bcos/BcosPrecompileRevisionGateTest.cpp`
- Modify: `bcos-evm/test/CMakeLists.txt`
- Modify: `bcos-evm/CMakeLists.txt`（若需安装新 header）

**Interfaces — Produces:**

```cpp
// bcos-evm/eth/precompiled/PrecompileActive.h
namespace bcos::evm::precompiled {
bool isActivePrecompile(
    evmc_revision revision,
    bcos::evm_standard::RevisionConfig const& cfg,
    evmc_address const& addr) noexcept;
}
```

- [ ] **Step 1: 写失败测试 — CANCUN call 0x0b**

`EipPrecompileRevisionGateTest.cpp`：

```cpp
BOOST_AUTO_TEST_CASE(cancun_call_0x0b_not_precompile)
{
    // revision=EVMC_CANCUN, call 0x0b with empty code
    // expect: NOT precompile dispatch — empty account success or OOG, NOT BLS gas 375
    evmc_address addr{};
    addr.bytes[19] = 0x0b;
    BOOST_CHECK(!bcos::evm::precompiled::isActivePrecompile(EVMC_CANCUN, cfg, addr));
}
```

`BcosPrecompileRevisionGateTest.cpp`：同断言经 `executeViaHost` + CANCUN block。

- [ ] **Step 2: 实现 PrecompileActive.h**

```cpp
inline bool isLowPrecompile(evmc_address const& a) noexcept {
    // bytes[0..17]==0 && bytes[18]==0 && bytes[19] in 0x01..0x11
}
inline bool isP256Precompile(evmc_address const& a) noexcept {
    return a.bytes[18]==0x01 && a.bytes[19]==0x00 && /* high 18 zero */;
}
inline bool isActivePrecompile(evmc_revision revision,
    bcos::evm_standard::RevisionConfig const& cfg, evmc_address const& addr) noexcept
{
    if (isP256Precompile(addr))
        return revision >= EVMC_OSAKA && cfg.eip7212;
    if (!isLowPrecompile(addr)) return false;
    auto const suffix = addr.bytes[19];
    if (suffix >= 0x01 && suffix <= 0x0a) return true;
    if (suffix >= 0x0b && suffix <= 0x11) return revision >= EVMC_PRAGUE;
    return false;
}
```

- [ ] **Step 3: 替换 EthHost + executeMessage 调用点**

`EthHost::isBuiltinPrecompileAddress` 改为接受 `RevisionConfig` 或委托 `isActivePrecompile(m_revision, cfg, addr)`。

`executeMessage.cpp` 删除本地 `isBuiltinPrecompileAddress`；`executeMessage` 需将 `input.revisionConfig` 传入 Host（增 `EthHost` 构造参数或 setter）。

在 `executeMessage.cpp:183`：

```cpp
if (code.empty() && precompiled::isActivePrecompile(
        input.revisionConfig.revision, input.revisionConfig, codeAddress))
```

- [ ] **Step 4: 运行门控测试 + 回归**

```bash
./bcos-evm/test/EipPrecompileRevisionGateTest
./bcos-evm/test/BcosPrecompileRevisionGateTest
./bcos-evm/test/Eip2537KernelTest
```

Expected: **PASS**

- [ ] **Step 5: Commit**

```bash
rtk git commit -m "fix(eth): revision-gate precompiles 0x0b-0x11 and 0x0100 (P0-6)"
```

---

### Task 3: P0-3 · EIP-2537 MSM gas

**Files:**
- Create: `bcos-evm/eth/precompiled/BlsGas.h`
- Modify: `bcos-evm/eth/state/EthPrecompiles.cpp`
- Modify: `bcos-evm/eth/precompiled/EthBuiltinRegistry.cpp`（pricer 改用 `BlsGas.h`）
- Modify: `bcos-evm/test/eth/Eip2537KernelTest.cpp`
- Create: `bcos-evm/test/bcos/Bcos2537MsmGasTest.cpp`
- Modify: `bcos-evm/test/CMakeLists.txt`

**Interfaces — Produces:**

```cpp
// BlsGas.h
namespace bcos::evm::precompiled {
inline int64_t blsG1MsmGas(size_t k) noexcept; // k>=1, else 0
inline int64_t blsG2MsmGas(size_t k) noexcept;
}
```

- [ ] **Step 1: 写失败测试 — k=2 gas**

在 `Eip2537KernelTest.cpp` 添加：

```cpp
BOOST_AUTO_TEST_CASE(g1msm_k2_gas_matches_geth)
{
    evmc_address addr{};
    addr.bytes[19] = 0x0c;
    bcos::bytes input(320, 0); // 2 pairs * 160
    auto r = state::EthPrecompiles::dispatch(addr, input, 500000, EVMC_PRAGUE);
    BOOST_REQUIRE(r.has_value());
    BOOST_CHECK_EQUAL(r->gasCost, 22776);
}
```

Expected: **FAIL**（当前 24000）

- [ ] **Step 2: 提取 BlsGas.h**

从 `EthBuiltinRegistry.cpp:362-373` 复制 `DISCOUNTS[]` 到 `BlsGas.h`；实现：

```cpp
inline int64_t blsG1MsmGas(size_t k) {
    if (k == 0) return 0;
    auto const discount = kG1Discounts[std::min(k, 128uz) - 1];
    return 12000 * static_cast<int64_t>(discount) * static_cast<int64_t>(k) / 1000;
}
```

G2 同理（22500 基数）。

- [ ] **Step 3: 更新 precompileGasCost**

`EthPrecompiles.cpp` case `0x000c`/`0x000e`：

```cpp
case 0x000c: {
    auto const k = input.size() / 160;
    return bcos::evm::precompiled::blsG1MsmGas(k);
}
```

- [ ] **Step 4: Registry DRY**

`EthBuiltinRegistry.cpp` pricer 改为调用 `blsG1MsmGas` / `blsG2MsmGas`。

- [ ] **Step 5: Bcos2537MsmGasTest**

经 `executeViaHost`，`feature_evm_prague` ON，同 gas 断言。

- [ ] **Step 6: 运行测试 + Commit**

```bash
rtk git commit -m "fix(eth): wire EIP-2537 MSM discount table in TE path (P0-3)"
```

---

### Task 4: P0-5 · EIP-7823 modexp bounds

**Files:**
- Modify: `bcos-evm/eth/state/EthPrecompiles.hpp`, `EthPrecompiles.cpp`
- Modify: `bcos-evm/eth/executeMessage.cpp`（传 `RevisionConfig` 到 dispatch）
- Create: `bcos-evm/test/eth/Eip7823ModexpRejectTest.cpp`
- Create: `bcos-evm/test/bcos/Bcos7823ModexpRejectTest.cpp`
- Modify: `bcos-evm/test/CMakeLists.txt`

- [ ] **Step 1: 写失败测试 — len 1025 拒绝**

```cpp
BOOST_AUTO_TEST_CASE(osaka_modexp_field_1025_rejected)
{
    // build modexp input: baseLen=1025 in header (96-byte prefix + padded fields)
    // revision=EVMC_OSAKA, eip7823=true
    // expect EVMC_PRECOMPILE_FAILURE
}
```

- [ ] **Step 2: 扩展 dispatch 签名**

`EthPrecompiles.hpp`：

```cpp
static std::optional<EthPrecompileResult> dispatch(
    const evmc_address& address, bcos::bytesConstRef input, int64_t msgGas,
    evmc_revision revision, bcos::evm_standard::RevisionConfig const& cfg);
```

`executeModexp` 开头：

```cpp
if (bcos::evm::shouldRejectModexpEip7823(addr, input, cfg, revision))
    return {false, {}};
```

map 到 `EVMC_PRECOMPILE_FAILURE` in dispatch。

- [ ] **Step 3: 更新所有 dispatch 调用点**（`executeMessage.cpp`, `EthHost.cpp`）

- [ ] **Step 4: BCOS 继承测试** `Bcos7823ModexpRejectTest` + `feature_evm_osaka`

- [ ] **Step 5: Commit**

```bash
rtk git commit -m "fix(eth): wire EIP-7823 modexp bounds in TE dispatch (P0-5)"
```

---

### Task 5: P0-4 · EIP-7212 (0x0100)

**Files:**
- Modify: `bcos-evm/eth/state/EthPrecompiles.cpp`
- Create: `bcos-evm/test/eth/Eip7212KernelTest.cpp`
- Create: `bcos-evm/test/bcos/Bcos7212ExecuteViaHostTest.cpp`
- Modify: `bcos-evm/test/CMakeLists.txt`

**依赖：** Task 2 `isActivePrecompile` 已含 0x0100 门控。

- [ ] **Step 1: 写失败测试**

使用 Registry 中已知有效 160B 向量（从 `EthBuiltinRegistry.cpp:495-512` 提取或 geth `p256Verify` testdata）。

```cpp
BOOST_AUTO_TEST_CASE(p256verify_osaka_success)
{
    evmc_address addr{};
    addr.bytes[18] = 0x01; addr.bytes[19] = 0x00;
    // cfg: OSAKA, eip7212=true
    // expect output 32 bytes, last byte 0x01, gasCost 6900
}
```

- [ ] **Step 2: 实现 executeP256Verify**

在 `EthPrecompiles.cpp` 匿名命名空间添加（复制 Registry 逻辑）：

```cpp
std::pair<bool, bcos::bytes> executeP256Verify(bcos::bytesConstRef input) {
    // 同 EthBuiltinRegistry p256verify — 160B, evmmax::secp256r1::verify
}
```

扩展 `toSuffix` 或单独 `tryP256Dispatch`；`precompileGasCost` 对 0x0100 返回 6900。

`dispatch` switch 增加 `0x0100` case（或通过 suffix 编码 `0x0100`）。

- [ ] **Step 3: BCOS 继承测试** `Bcos7212ExecuteViaHostTest`

- [ ] **Step 4: Commit**

```bash
rtk git commit -m "fix(eth): add EIP-7212 p256verify dispatch on TE path (P0-4)"
```

---

### Task 6: P0-2 · EIP-6780 SELFDESTRUCT

**Files:**
- Modify: `bcos-evm/eth/state/EthHost.hpp`, `EthHost.cpp`
- Modify: `bcos-evm/eth/executeMessage.cpp`
- Modify: `bcos-evm/test/fixtures/state/imported/stSelfDestruct_basic.json`
- Modify: `bcos-evm/test/fixtures/FixtureAssert.h`
- Modify: `bcos-evm/test/fixtures/EthStateFixtureLoader.h`（若需 `expected.post` 字段）
- Create: `bcos-evm/test/bcos/Bcos6780SelfdestructTest.cpp`
- Modify: `bcos-evm/test/CMakeLists.txt`

**Interfaces — Produces:**

```cpp
// EthHost.hpp
void markCreatedInTx(evmc_address const& addr) noexcept;
bool wasCreatedInTx(evmc_address const& addr) const noexcept;
```

- [ ] **Step 1: 读 geth 金标准**

```bash
rtk grep -n "opSelfdestruct6780\|SelfDestruct6780" /Users/octopus/octo/code/blockchain-impl/go-ethereum/core/vm/instructions.go | head -5
```

确认：余额转移；仅 `isNewContract(addr)` 时删 code。

- [ ] **Step 2: 写失败 post-state 测试**

扩展 `stSelfDestruct_basic.json`：

```json
"expected": {
  "status": "success",
  "gas_used": 7603,
  "post": [
    {"address": "0x...0012", "balance": "0x0"},
    {"address": "0x...00bb", "balance": "0x10"},
    {"address": "0x...00cc", "code_nonempty": true}
  ]
}
```

（按 fixture loader 实际 schema 调整；或在测试中硬编码 post-state 断言。）

`FixtureAssert.h` 增可选 `assertFixturePostState(view, fixture)`。

`Bcos6780SelfdestructTest.cpp`：同 fixture via `executeViaHost`，CANCUN+ revision。

- [ ] **Step 3: CREATE 跟踪**

`executeMessage.cpp` 在 `installCreatedContractCode` 成功后：

```cpp
host.markCreatedInTx(createAddr);
```

- [ ] **Step 4: 实现 selfdestruct**

`EthHost::selfdestruct`：

```cpp
bool EthHost::selfdestruct(const address& addr, const address& beneficiary) noexcept
{
    if (m_extension && !m_extension->allowSelfdestruct(...)) return false;
    auto const balance = m_state.get_balance(addr);
    if (balance != 0)
        bcos::evm::transfer(m_state, addr, beneficiary, balance);
    if (m_revision >= EVMC_CANCUN) {
        if (!wasCreatedInTx(addr)) return false; // 6780: keep code
        m_state.set_code(addr, {}, {});
        // clear storage per State API
        return true;
    }
    // legacy: always delete
    m_state.set_code(addr, {}, {});
    return true;
}
```

对照 geth 精调返回值与 storage 清除。

- [ ] **Step 5: 运行 6780 测试 + fixture 回归**

```bash
./bcos-evm/test/ExecuteViaEthFixtureTest
./bcos-evm/test/Bcos6780SelfdestructTest
```

- [ ] **Step 6: Commit**

```bash
rtk git commit -m "fix(eth): implement EIP-6780 selfdestruct semantics in EthHost (P0-2)"
```

---

### Task 7: capability matrix + 全量回归

**Files:**
- Modify: `bcos-evm/capability-matrix.md`
- Optional: `bcos-evm/docs/audits/2026-06-20-eth-reference-cancun-plus-audit-fix-summary.md`

- [ ] **Step 1: 更新 matrix（§9 spec）**

- EIP-7212 ETH 列：`unsupported` → `inherited`
- EIP-6780：kernel 脚注关闭 🔴
- EIP-2537：移除 TE MSM 待接脚注
- `eip7823`：TE consumer wired
- builtin 0x01–0x11：revision 门控脚注

- [ ] **Step 2: 全量 ctest 回归**

```bash
cd build
ctest -R "Eip7702ApplyAuthorizationEth|EipPrecompileRevisionGate|BcosPrecompileRevisionGate|Eip2537|Bcos2537|Eip7823|Bcos7823|Eip7212|Bcos7212|Bcos6780|RevisionConfigProfile|ExecuteViaEthFixture|Eip2929Access|Bcos7702" --output-on-failure
```

Expected: **全部 PASS**

- [ ] **Step 3: 可选 fix-summary 一页**

- [ ] **Step 4: Commit**

```bash
rtk git commit -m "docs(matrix): sync ETH column after CANCUN+ P0 fixes"
```

---

## Self-Review（计划 vs Fix Spec）

| Spec § | Task |
|--------|------|
| P0-1 7702 | Task 1 |
| P0-2 6780 | Task 6 |
| P0-3 2537 | Task 3 |
| P0-4 7212 | Task 5 |
| P0-5 7823 | Task 4 |
| P0-6 门控 | Task 2 |
| 继承证明 12 测试 | 各 Task ETH+BCOS |
| matrix P2 | Task 7 |
| 单次 PR / logical commits | 每 Task commit |
| 无 bcos/ 内核重复 | Global Constraints |

无 TBD；测试含具体断言值（22776、6900 gas）。

---

## 预估工作量

| Task | 预估 |
|------|------|
| 1 7702 | 45 min |
| 2 门控 | 60 min |
| 3 2537 | 60 min |
| 4 7823 | 45 min |
| 5 7212 | 60 min |
| 6 6780 | 90–120 min |
| 7 回归 | 30 min |

**合计：** 约 6–8 小时
