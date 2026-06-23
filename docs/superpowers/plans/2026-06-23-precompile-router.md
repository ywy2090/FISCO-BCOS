# PrecompileRouter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 `bcos-evm/eth` 引入 `PrecompileRouter`，统一 `executeMessage`（empty code）与 `EthHost::call`（CALL/STATICCALL/DELEGATECALL，排除 CREATE）的 precompile dispatch precedence 与 execution envelope。

**Architecture:** 单一 `dispatchPrecompile()` 实现 chain→builtin→EmptyAccountSuccess；双入口调用（§spec 3.4）；删除 `executeMessage.cpp` / `EthHost.cpp` 重复短路；TDD 先 characterization 再 router 再集成。

**Tech Stack:** C++20、Boost.Test、evmone、EVMC、`bcos-evm-eth` 静态库。

**Spec:** [`docs/superpowers/specs/2026-06-23-precompile-router-design.md`](../specs/2026-06-23-precompile-router-design.md) v1.2

## Global Constraints

- Phase 1 only：不收敛顶层 spine 到 `EthHost::call`；不碰 `PrecompiledManager` / `bcos-executor`。
- `eth/` 不得新增 `bcos/`、`opstack/` include。
- 统一 precedence：**chain → builtin**；builtin 需 `empty code && isActivePrecompile`。
- 等价承诺：**C1–C5 empty code only**；C7 记录 non-empty `[PRECOMPILED]` 不对称，不要求等价。
- C5 余额不足：**`EVMC_INSUFFICIENT_BALANCE`，`gas_left=0`**，不 checkpoint（grilling 方案 A）。
- 不修改三 orchestrator；不修改 `HostExtension` interface。
- 使用 `rtk` 前缀 shell/git 命令（仓库 CLAUDE.md）。

---

## File Map

| 文件 | 动作 | 职责 |
|------|------|------|
| `bcos-evm/eth/precompiled/PrecompileRouter.h` | Create | Public interface |
| `bcos-evm/eth/precompiled/PrecompileRouter.cpp` | Create | dispatch + envelope |
| `bcos-evm/CMakeLists.txt` | Modify | 加入 `PrecompileRouter.cpp` |
| `bcos-evm/eth/executeMessage.cpp` | Modify | empty code → router |
| `bcos-evm/eth/state/EthHost.cpp` | Modify | CALL/STATICCALL/DELEGATECALL → router（排除 CREATE） |
| `bcos-evm/test/eth/PrecompileRouterCharacterizationTest.cpp` | Create | PR-1 baseline |
| `bcos-evm/test/eth/PrecompileRouterEquivalenceTest.cpp` | Create | C1–C5 |
| `bcos-evm/test/eth/PrecompileRouterEnvelopeTest.cpp` | Create | C5 + commit/revert |
| `bcos-evm/test/eth/PrecompileRouterPrecedenceTest.cpp` | Create | chain before builtin |
| `bcos-evm/test/CMakeLists.txt` | Modify | 四个 test target |
| `bcos-evm/capability-matrix.md` | Modify | Test ref 两行 |

---

### Task 1: PR-1 — Characterization baseline (C1–C7)

**Files:**
- Create: `bcos-evm/test/eth/PrecompileRouterCharacterizationTest.cpp`
- Modify: `bcos-evm/test/CMakeLists.txt`

**Interfaces:**
- Consumes: 现有 `executeMessage()`, `EthHost::call()`, `InMemoryStateView`；C2 另需 `OpHostExtension`（`bcos-evm-op`）
- Produces: 命名常量 `kDepth0*` / `kDepth1*` 或 inline 注释 baseline，供 Task 6 对照

- [ ] **Step 1: 添加 CMake target**

在 `bcos-evm/test/CMakeLists.txt` 追加（链接 **`bcos-evm`** + **`bcos-evm-op`**（C2），与 `ExecuteMessageSmokeTest` / `EmptyCodeHookTest` 模式一致）：

```cmake
set(PRECOMPILE_ROUTER_CHAR_TEST_BINARY_NAME PrecompileRouterCharacterizationTest)

add_executable(${PRECOMPILE_ROUTER_CHAR_TEST_BINARY_NAME}
    eth/PrecompileRouterCharacterizationTest.cpp
)

target_include_directories(${PRECOMPILE_ROUTER_CHAR_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${PRECOMPILE_ROUTER_CHAR_TEST_BINARY_NAME} PRIVATE
    bcos-evm
    bcos-evm-op
)

add_test(
    NAME PrecompileRouterCharacterization
    COMMAND ${PRECOMPILE_ROUTER_CHAR_TEST_BINARY_NAME}
)
```

- [ ] **Step 2: 实现 characterization 测试骨架**

```cpp
// bcos-evm/test/eth/PrecompileRouterCharacterizationTest.cpp
#define BOOST_TEST_MODULE PrecompileRouterCharacterizationTest

#include "bcos-evm/eth/executeMessage.h"
#include "bcos-evm/eth/state/EthHost.hpp"
#include "bcos-evm/opstack/OpHostExtension.h"
#include "bcos-evm/opstack/OpStackConstants.h"
#include "state/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>
#include <evmone/evmone.h>

namespace bcos::evm::test
{
namespace
{
struct CallOutcome
{
    evmc_status_code status{};
    int64_t gasLeft{};
};

CallOutcome runDepth0EmptyCall(ExecuteMessageInput input)
{
    auto output = executeMessage(std::move(input));
    return {output.result.status_code, output.result.gas_left};
}

CallOutcome runDepth1EmptyCall(state::State& state, state::EthHost& host, evmc_message msg)
{
    auto result = host.call(msg);
    return {result.status_code, result.gas_left};
}
}  // namespace

// C1: identity 0x04 — 记录 depth0 vs depth1（合并前允许不等价，但必须 PASS）
BOOST_AUTO_TEST_CASE(c1_identity_precompile_characterization)
{
    // setup sender balance, empty target 0x04, input 0xdeadbeef
    // depth0 via executeMessage; depth1 via EthHost::call with depth=1
    // BOOST_CHECK 记录两者 status/gasLeft；注释 // BASELINE(pre-router):
}

// C2: Op L1Block chain hook — 参考 EmptyCodeHookTest
BOOST_AUTO_TEST_CASE(c2_chain_precompile_characterization) { /* ... */ }

// C3: empty EOA
BOOST_AUTO_TEST_CASE(c3_empty_eoa_characterization) { /* ... */ }

// C4: DELEGATECALL → precompile + allowDelegateCallToPrecompile=false
//     depth1 via EthHost::call；记录 PRECOMPILE_FAILURE baseline
BOOST_AUTO_TEST_CASE(c4_delegatecall_precompile_gate_characterization) { /* ... */ }

// C5: CALL + non-zero value → builtin 0x04
//     记录 depth0/1 status/gasLeft/balance（合并前允许不等价）
BOOST_AUTO_TEST_CASE(c5_value_transfer_precompile_characterization) { /* ... */ }

// C6: modexp / BLS revision gate（复用 PrecompileRevisionGate 或 Eip2537 fixture 数据）
BOOST_AUTO_TEST_CASE(c6_revision_gate_characterization) { /* ... */ }

// C7: 明确断言 depth0 != depth1 或 document 差异（non-empty [PRECOMPILED] 可 skip 若 fixture 过重，至少 nested-only chain hit）
BOOST_AUTO_TEST_CASE(c7_non_empty_precompiled_asymmetry_documented) { /* ... */ }

}  // namespace bcos::evm::test
```

- [ ] **Step 3: 构建并运行**

```bash
cmake --build build --target PrecompileRouterCharacterizationTest
rtk test ctest --test-dir build/bcos-evm/test -R PrecompileRouterCharacterization --output-on-failure
```

Expected: **PASS**（记录 baseline，不要求 depth0≡depth1）

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-evm/test/eth/PrecompileRouterCharacterizationTest.cpp bcos-evm/test/CMakeLists.txt
rtk git commit -m "$(cat <<'EOF'
test(eth): Add PrecompileRouter characterization baseline (PR-1).

Capture depth=0 vs depth=1 precompile behavior before router merge.
EOF
)"
```

---

### Task 2: PrecompileRouter module skeleton

**Files:**
- Create: `bcos-evm/eth/precompiled/PrecompileRouter.h`
- Create: `bcos-evm/eth/precompiled/PrecompileRouter.cpp`
- Modify: `bcos-evm/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `bcos::evm::precompiled::PrecompileDispatchOutcome`
  - `bcos::evm::precompiled::PrecompileRouterInput`
  - `bcos::evm::precompiled::PrecompileRouterOutput`
  - `bcos::evm::precompiled::dispatchPrecompile(PrecompileRouterInput const&)`

- [ ] **Step 1: 添加头文件**（与 spec §3.2 一致）

`bcos-evm/eth/precompiled/PrecompileRouter.h` — 复制 spec 中完整 struct + 函数声明。

- [ ] **Step 2: 添加 stub 实现**

```cpp
// bcos-evm/eth/precompiled/PrecompileRouter.cpp
#include "bcos-evm/eth/precompiled/PrecompileRouter.h"

namespace bcos::evm::precompiled
{
PrecompileRouterOutput dispatchPrecompile(PrecompileRouterInput const& input)
{
    (void)input;
    return {};
}
}  // namespace
```

- [ ] **Step 3: CMake**

`bcos-evm/CMakeLists.txt` → `BCOS_EVM_ETH_SOURCES` 追加 `eth/precompiled/PrecompileRouter.cpp`。

- [ ] **Step 4: 构建**

```bash
cmake --build build --target bcos-evm-eth
```

Expected: 编译成功

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/eth/precompiled/PrecompileRouter.h bcos-evm/eth/precompiled/PrecompileRouter.cpp bcos-evm/CMakeLists.txt
rtk git commit -m "feat(eth): Add PrecompileRouter module skeleton."
```

---

### Task 3: Implement `dispatchPrecompile` core

**Files:**
- Modify: `bcos-evm/eth/precompiled/PrecompileRouter.cpp`
- Test: `bcos-evm/test/eth/PrecompileRouterPrecedenceTest.cpp`（先写）

**Interfaces:**
- Consumes: `Transfer.h`, `PrecompileActive.h`, `EthPrecompiles.hpp`, `HostExtension.h`
- Produces: 完整 `dispatchPrecompile` 语义（§spec 4.1 + 5.1）

- [ ] **Step 1: Precedence 失败测试 — mock extension chain 先于 builtin**

```cpp
// MockHostExtension : HostExtension
// tryChainPrecompile returns SUCCESS with output "chain"
// 目标地址同时为 active builtin (0x04)
// 断言 dispatch 结果 output 为 "chain" 而非 identity 输出

class ChainFirstExtension : public state::HostExtension
{
public:
    std::optional<evmc_result> tryChainPrecompile(evmc_revision, evmc_message const&) override
    {
        evmc_result r{};
        r.status_code = EVMC_SUCCESS;
        r.gas_left = 1000;
        static uint8_t out[] = {0xca, 0xfe};
        r.output_data = out;
        r.output_size = 2;
        return r;
    }
};
```

- [ ] **Step 2: 运行测试 — 预期 FAIL**

```bash
rtk test ctest --test-dir build/bcos-evm/test -R PrecompileRouterPrecedence --output-on-failure
```

- [ ] **Step 3: 实现 dispatchPrecompile**

核心逻辑（伪代码，实现时逐行翻译；**v1.2**：step 1 含显式 `transfer()`）：

```cpp
PrecompileRouterOutput dispatchPrecompile(PrecompileRouterInput const& input)
{
    PrecompileRouterOutput output;
    auto const code = input.state.get_code(input.target);
    bool const emptyCode = code.empty();

    if (!isZeroBytes32(input.message.value) && !input.skipValueTransfer)
    {
        auto const value = state::fromEvmC(input.message.value);
        if (!canTransfer(input.state, input.message.sender, value))
        {
            output.outcome = PrecompileDispatchOutcome::Dispatched;
            output.result = evmc::Result(makeInsufficientBalanceResult());
            return output;
        }
        transfer(input.state, input.message.sender, input.target, value);
    }

    input.state.checkpoint();

    if (input.extension != nullptr)
    {
        if (auto chain = input.extension->tryChainPrecompile(
                input.revision.revision, input.message))
        {
            output.outcome = PrecompileDispatchOutcome::Dispatched;
            output.result = evmc::Result(std::move(*chain));
            finalizeEnvelope(input.state, output);
            return output;
        }
    }

    if (emptyCode && isActivePrecompile(input.revision.revision, input.revision, input.target))
    {
        if (auto builtin = state::EthPrecompiles::tryDispatchInCall(
                input.target, input.message, input.revision.revision, input.revision))
        {
            output.outcome = PrecompileDispatchOutcome::Dispatched;
            output.result = std::move(*builtin);
            finalizeEnvelope(input.state, output);
            return output;
        }
    }

    if (emptyCode)
    {
        output.outcome = PrecompileDispatchOutcome::EmptyAccountSuccess;
        evmc_result ok{};
        ok.status_code = EVMC_SUCCESS;
        ok.gas_left = input.message.gas;
        output.result = evmc::Result(ok);
        input.state.commit();
        output.gasRefund = static_cast<int64_t>(input.state.get_refund());
        return output;
    }

    input.state.revert();
    output.outcome = PrecompileDispatchOutcome::NotApplicable;
    return output;
}
```

`makeInsufficientBalanceResult()`：`EVMC_INSUFFICIENT_BALANCE`，`gas_left=0`。

`finalizeEnvelope`: SUCCESS → commit + gasRefund；否则 revert。

- [ ] **Step 4: 运行 Precedence + 单元直调测试 — PASS**

- [ ] **Step 5: Commit**

```bash
rtk git commit -m "feat(eth): Implement PrecompileRouter dispatch and envelope."
```

---

### Task 4: Integrate `executeMessage.cpp`

**Files:**
- Modify: `bcos-evm/eth/executeMessage.cpp`（删除 214–273，插入 spec §6.1 块）

- [ ] **Step 1: 运行 ExecuteMessageSmoke + EmptyCodeHook — baseline PASS**

- [ ] **Step 2: 替换 empty-code 块为 `dispatchPrecompile` 调用**（spec §6.1；确保 `output.gasRefund = routed.gasRefund`）

- [ ] **Step 3: 删除 dead code（214–273：旧 builtin-first / chain / empty SUCCESS 三段）**

- [ ] **Step 4: 回归**

```bash
rtk test ctest --test-dir build/bcos-evm/test -R 'ExecuteMessageSmoke|EmptyCodeHook' --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
rtk git commit -m "refactor(eth): Route executeMessage empty-code via PrecompileRouter."
```

---

### Task 5: Integrate `EthHost.cpp`

**Files:**
- Modify: `bcos-evm/eth/state/EthHost.cpp`

- [ ] **Step 1: 保留 DELEGATECALL 门控（277–281 等价，仍用 `routed.hasPrecompileTarget`）**

- [ ] **Step 2: 删除 268–290 独立 precompile 块；在 prepareMessage 前对非 CREATE invoke router**（spec §6.2）

```cpp
if (!isCreateKind(callMessage.kind))
{
    bool const skipVt = m_extension && m_extension->skipHostValueTransfer();
    auto const target = isZeroAddress(callMessage.code_address) ?
                            callMessage.recipient :
                            callMessage.code_address;
    auto out = precompiled::dispatchPrecompile({...});
    if (out.outcome != PrecompileDispatchOutcome::NotApplicable)
        return Result(std::move(out.result));
}
```

**不得**对 CREATE/CREATE2 invoke router（否则会 EmptyAccountSuccess 跳过 init code）。

- [ ] **Step 3: 回归**

```bash
rtk test ctest --test-dir build/bcos-evm/test -R 'PrecompileInCall|NestedCallHost|FiscoHostExtension' --output-on-failure
```

- [ ] **Step 4: Commit**

```bash
rtk git commit -m "refactor(eth): Route EthHost::call via PrecompileRouter."
```

---

### Task 6: Equivalence + Envelope tests (C1–C5)

**Files:**
- Create: `bcos-evm/test/eth/PrecompileRouterEquivalenceTest.cpp`
- Create: `bcos-evm/test/eth/PrecompileRouterEnvelopeTest.cpp`
- Modify: `bcos-evm/test/CMakeLists.txt`

- [ ] **Step 1: Equivalence — C1/C3 断言 depth0.status/gasLeft == depth1**

```cpp
BOOST_AUTO_TEST_CASE(c1_identity_depth0_equals_depth1)
{
    // 同 Task 1 setup；合并后必须相等
    BOOST_REQUIRE_EQUAL(d0.status, d1.status);
    BOOST_REQUIRE_EQUAL(d0.gasLeft, d1.gasLeft);
}
```

- [ ] **Step 2: Envelope C5 — 余额不足**

```cpp
BOOST_AUTO_TEST_CASE(c5_insufficient_balance_both_depths)
{
    // sender balance < value；depth0 与 depth1 均 EVMC_INSUFFICIENT_BALANCE, gas_left==0
}
```

- [ ] **Step 3: Envelope — successful value transfer 后 balance 一致**

- [ ] **Step 4: 全量 kernel 回归**

```bash
rtk test ctest --test-dir build/bcos-evm/test -R \
  'PrecompileRouter|PrecompileInCall|ExecuteMessageSmoke|EmptyCodeHook|NestedCallHost|Eip7212|Eip7823' \
  --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
rtk git commit -m "test(eth): Add PrecompileRouter equivalence and envelope tests."
```

---

### Task 7: Matrix + TE regression + docs

**Files:**
- Modify: `bcos-evm/capability-matrix.md`

- [ ] **Step 1: 更新 Test ref 列**（spec §7.3）

- [ ] **Step 2: TE 回归（若已构建）**

```bash
rtk test ctest --test-dir build/transaction-executor/test -R 'CompatExecuteViaHost|ExecuteViaHostCompat' --output-on-failure
```

- [ ] **Step 3: 完整 bcos-evm test suite**

```bash
rtk test ctest --test-dir build/bcos-evm/test --output-on-failure
```

- [ ] **Step 4: Commit**

```bash
rtk git commit -m "docs(bcos-evm): Link PrecompileRouter tests in capability matrix."
```

---

## Spec Coverage Checklist

| Spec § | Task |
|--------|------|
| 3.4 双入口 | Task 4 + 5 |
| 4.1 precedence + isActivePrecompile | Task 3 |
| 4.2 C1–C7 characterization | Task 1 |
| 5.1 envelope + C5 方案 A | Task 3 + 6 |
| 5.2 DELEGATECALL 门控 | Task 5 |
| 6.1 executeMessage 集成 | Task 4 |
| 6.2 EthHost invoke（排除 CREATE） | Task 5 |
| 7.1 四个测试 target | Task 1 + 3 + 6 |
| 7.3 matrix | Task 7 |

## Plan Self-Review（v1.2）

- 无 TBD/TODO 占位
- Task 3 `transfer()` + checkpoint 顺序与 spec §5.1 一致
- Task 5 CREATE 排除与 spec §6.2 一致
- Task 1 链接 `bcos-evm` + `bcos-evm-op`；C1–C7 骨架完整
- PR-1 与 PR-2 分 commit，符合 spec §8
- C7 仅在 Task 1 记录，Task 6 不要求等价
