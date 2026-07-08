# Fisco OrchestrationProfile (P1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 `ExecuteViaHost.cpp` 内链策略 hook 迁入具名 `FiscoOrchestrationProfile` module（Session + `buildHooks`），零语义变更，wrapper 仅保留 extension/ctx/error hook/output 映射。

**Architecture:** 新增 `bcos/FiscoOrchestrationProfile.{h,cpp}`；`buildHooks(Session&)` 返回含 capturing lambda 的 `OrchestrationHooks`；`mapException` / `mapIntrinsicFailure` **留** `ExecuteViaHost.cpp` 并标注 `// TODO: OrchestrationErrorPolicy (candidate 4)`。共享 helper（`maybeTransferValue`、`NotFoundCodeError`）放 `FiscoOrchestrationInternals.h` 供 Profile 与 wrapper catch 共用。

**Tech Stack:** C++20 · Boost.Test · bcos-evm-bcos · evmone · ADR-005/019

## Global Constraints

- **不修改** `ExecuteViaHostInput` / `ExecuteViaHostOutput` 布局
- **不新增** 公开 `include/bcos-evm/` facade
- **`eth/` 不得** `#include` `bcos/` 或 `opstack/`（ADR-005）
- **error hook 不在 Profile 范围**（grilling Q8）
- **零语义变更**：FISCO smoke 全绿为 merge gate
- **文件名 PascalCase**（ADR-020）：`FiscoOrchestrationProfile.h` / `.cpp`

---

## File Map

| File | Action | Responsibility |
| --- | --- | --- |
| `bcos-evm/bcos/FiscoOrchestrationProfile.h` | Create | `Session` struct + `buildHooks` 声明 |
| `bcos-evm/bcos/FiscoOrchestrationInternals.h` | Create | `NotFoundCodeError`、`maybeTransferValue` 等 preKernel 共用 helper |
| `bcos-evm/bcos/FiscoOrchestrationProfile.cpp` | Create | 链策略 hook 实现体 |
| `bcos-evm/bcos/ExecuteViaHost.cpp` | Modify | 删 inline hook 体；Session + Profile；保留 error hook |
| `bcos-evm/CMakeLists.txt` | Modify | `BCOS_EVM_BCOS_SOURCES` 增加 Profile.cpp |
| `bcos-evm/test/bcos/FiscoOrchestrationProfileTest.cpp` | Create | ≥3 hook wiring cases |
| `bcos-evm/test/CMakeLists.txt` | Modify | 注册新 test target |

---

### Task 1: Internals header + Profile scaffold

**Files:**
- Create: `bcos-evm/bcos/FiscoOrchestrationInternals.h`
- Create: `bcos-evm/bcos/FiscoOrchestrationProfile.h`
- Create: `bcos-evm/bcos/FiscoOrchestrationProfile.cpp`（stub）
- Modify: `bcos-evm/CMakeLists.txt:25-29`

**Interfaces:**
- Produces: `NotFoundCodeError`, `maybeTransferValue(...)`, `FiscoOrchestrationProfile::Session`, `FiscoOrchestrationProfile::buildHooks(Session&) -> OrchestrationHooks`

- [ ] **Step 1: 创建 `FiscoOrchestrationInternals.h`**

从 `ExecuteViaHost.cpp` 匿名 namespace **剪切**（非复制）以下 helper 到 internals 头：

```cpp
#pragma once

#include "bcos-evm/eth/state/State.hpp"
#include "bcos-framework/protocol/Exceptions.h"
#include <evmc/evmc.h>
#include <boost/throw_exception.hpp>
#include <cstring>

namespace bcos::evm {

struct NotFoundCodeError : public std::runtime_error
{
    NotFoundCodeError() : std::runtime_error("code not found") {}
};

inline bool isCreateKind(evmc_call_kind kind) noexcept
{
    return kind == EVMC_CREATE || kind == EVMC_CREATE2;
}

inline void maybeTransferValue(state::State& state, evmc_message const& msg,
    bool fixDelegateCallTransfer)
{
    // 从 ExecuteViaHost.cpp:70-106 原样迁入（hasNonZeroValue / addressEqual 一并 inline 或同文件 static）
}

}  // namespace bcos::evm
```

- [ ] **Step 2: 创建 `FiscoOrchestrationProfile.h`**

```cpp
#pragma once

#include "bcos-evm/bcos/ExecuteViaHost.h"
#include "bcos-evm/bcos/FiscoHostExtension.h"
#include "bcos-evm/eth/orchestration/OrchestrationHooks.h"

namespace bcos::evm {

struct FiscoOrchestrationProfile
{
    struct Session
    {
        ExecuteViaHostInput const& input;
        ExecuteViaHostOutput& output;
        FiscoHostExtension& extension;
        bool fixErrorHandling{false};
        bool eip7623Enabled{false};
    };

    static OrchestrationHooks buildHooks(Session& session);
};

}  // namespace bcos::evm
```

- [ ] **Step 3: 创建 stub `FiscoOrchestrationProfile.cpp`**

```cpp
#include "bcos-evm/bcos/FiscoOrchestrationProfile.h"

namespace bcos::evm {

OrchestrationHooks FiscoOrchestrationProfile::buildHooks(Session& /*session*/)
{
    return OrchestrationHooks{};
}

}  // namespace bcos::evm
```

- [ ] **Step 4: 更新 CMake**

`bcos-evm/CMakeLists.txt` 的 `BCOS_EVM_BCOS_SOURCES` 追加：

```cmake
    bcos/FiscoOrchestrationProfile.cpp
```

- [ ] **Step 5: 编译验证**

Run:

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor
cmake --build build --target bcos-evm-bcos -j$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
```

Expected: 链接成功，无 undefined symbol。

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/bcos/FiscoOrchestrationProfile.h \
  bcos-evm/bcos/FiscoOrchestrationProfile.cpp \
  bcos-evm/bcos/FiscoOrchestrationInternals.h \
  bcos-evm/CMakeLists.txt
rtk git commit -m "refactor(bcos-evm): scaffold FiscoOrchestrationProfile module"
```

---

### Task 2: Profile 单元测试（TDD）

**Files:**
- Create: `bcos-evm/test/bcos/FiscoOrchestrationProfileTest.cpp`
- Modify: `bcos-evm/test/CMakeLists.txt`（在 `BcosAuthOrchestratorHookTest` 块附近）

**Interfaces:**
- Consumes: `FiscoOrchestrationProfile::buildHooks`, `OrchestrationContext`, `runOrchestration`
- Produces: 3 passing BOOST tests

- [ ] **Step 1: 写 failing test — intrinsicPolicy Eip7623**

```cpp
#define BOOST_TEST_MODULE FiscoOrchestrationProfileTest

#include "bcos-evm/bcos/FiscoOrchestrationProfile.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/bcos/FiscoHostExtension.h"
#include "state/InMemoryStateView.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
BOOST_AUTO_TEST_CASE(intrinsic_policy_eip7623_when_web3_and_flag_enabled)
{
    state::test::InMemoryStateView stateView;
    evmc_message message{};
    message.gas = 100'000;

    ExecuteViaHostInput input;
    input.web3Tx = true;
    input.revisionConfig.eth().eip7623 = true;

    ExecuteViaHostOutput output;
    FiscoHostExtension::FiscoHostExtensionDeps deps;
    deps.state = nullptr; // hook test 不执行 extension
    FiscoHostExtension extension(false, std::move(deps));

    FiscoOrchestrationProfile::Session session{
        input, output, extension, false, true /* eip7623Enabled */};

    auto hooks = FiscoOrchestrationProfile::buildHooks(session);
    BOOST_CHECK_EQUAL(static_cast<int>(hooks.intrinsicPolicy.mode),
        static_cast<int>(IntrinsicDebitMode::Eip7623));
}
}  // namespace bcos::evm::test
```

- [ ] **Step 2: 写 failing test — auth preExecute early exit**

```cpp
class MockAuthPort final : public AuthPort
{
public:
    std::optional<EVMCResult> checkAuth(evmc_message const&) override
    {
        evmc_result fail{};
        fail.status_code = EVMC_REJECTED;
        return EVMCResult(fail, protocol::TransactionStatus::PermissionDenied);
    }
    void createAuthTable(evmc_message const&, std::string_view) override {}
};

BOOST_AUTO_TEST_CASE(pre_execute_auth_sets_early_exit)
{
    state::test::InMemoryStateView stateView;
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};

    evmc_message message{};
    message.gas = 50'000;

    ExecuteViaHostInput input;
    input.revisionConfig.enable_auth_check = true;
    MockAuthPort authPort;
    input.authPort = &authPort;

    ExecuteViaHostOutput output;
    FiscoHostExtension::FiscoHostExtensionDeps deps;
    deps.state = nullptr;
    FiscoHostExtension extension(false, std::move(deps));

    OrchestrationContext ctx{stateView, message, input.revisionConfig.eth(), bcos::u256(0)};
    ctx.inputs.vm = &vm;
    ctx.inputs.hashImpl = &hashImpl;

    FiscoOrchestrationProfile::Session session{input, output, extension, false, false};
    auto hooks = FiscoOrchestrationProfile::buildHooks(session);
    hooks.preExecute(ctx);

    BOOST_CHECK(ctx.earlyExit);
    BOOST_CHECK_EQUAL(static_cast<int>(ctx.exitKind),
        static_cast<int>(OrchestrationExitKind::PreExecuteRejected));
}
```

- [ ] **Step 3: 写 failing test — prepareMessage CREATE 地址**

```cpp
BOOST_AUTO_TEST_CASE(prepare_message_create_sets_recipient_for_legacy_tx)
{
    state::test::InMemoryStateView stateView;
    crypto::Keccak256 hashImpl;

    evmc_message message{};
    message.kind = EVMC_CREATE;
    message.sender.bytes[19] = 0xAA;

    ExecuteViaHostInput input;
    input.web3Tx = false;
    input.hashImpl = &hashImpl;
    input.blockInfo.number = 42;
    input.contextID = 1;
    input.seq = 2;
    input.nonce = 0;

    ExecuteViaHostOutput output;
    FiscoHostExtension::FiscoHostExtensionDeps deps;
    deps.state = nullptr;
    FiscoHostExtension extension(false, std::move(deps));

    OrchestrationContext ctx{stateView, message, input.revisionConfig.eth(), bcos::u256(0)};

    FiscoOrchestrationProfile::Session session{input, output, extension, false, false};
    auto hooks = FiscoOrchestrationProfile::buildHooks(session);
    hooks.prepareMessage(ctx);

    BOOST_CHECK(ctx.message.kind == EVMC_CREATE);
    BOOST_CHECK(std::memcmp(ctx.message.recipient.bytes, ctx.message.code_address.bytes,
                    sizeof(ctx.message.recipient.bytes)) == 0);
    // legacy: recipient 非全零（deriveMessage 已运行）
    bool allZero = true;
    for (auto b : ctx.message.recipient.bytes)
        if (b != 0) { allZero = false; break; }
    BOOST_CHECK(!allZero);
}
```

- [ ] **Step 4: 注册 CMake test target**

在 `bcos-evm/test/CMakeLists.txt` `BcosAuthOrchestratorHookTest` 块后追加：

```cmake
add_executable(FiscoOrchestrationProfileTest bcos/FiscoOrchestrationProfileTest.cpp)
target_include_directories(FiscoOrchestrationProfileTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(FiscoOrchestrationProfileTest PRIVATE bcos-evm)
add_test(NAME FiscoOrchestrationProfile COMMAND FiscoOrchestrationProfileTest)
```

- [ ] **Step 5: 运行 test 确认 FAIL**

Run:

```bash
cd build && ctest -R FiscoOrchestrationProfile --output-on-failure
```

Expected: intrinsic test FAIL（mode == None）；auth/prepare 可能 FAIL（空 hook）。

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/test/bcos/FiscoOrchestrationProfileTest.cpp bcos-evm/test/CMakeLists.txt
rtk git commit -m "test(bcos-evm): add FiscoOrchestrationProfile hook wiring tests"
```

---

### Task 3: 实现 `buildHooks`（从 ExecuteViaHost 迁出链策略 hook）

**Files:**
- Modify: `bcos-evm/bcos/FiscoOrchestrationProfile.cpp`（完整实现）
- Modify: `bcos-evm/bcos/ExecuteViaHost.cpp`（删除已迁出代码）

**Interfaces:**
- Consumes: `deriveMessage`（`FiscoTxAdapter.h`）、`FiscoOrchestrationInternals.h`、`FiscoConstants.h`（`BALANCE_TRANSFER_GAS`）
- Produces: 与迁移前 **行为等价** 的 `OrchestrationHooks`

- [ ] **Step 1: 实现 `buildHooks` — 从 ExecuteViaHost.cpp L234-339 剪切**

`FiscoOrchestrationProfile.cpp` 完整骨架：

```cpp
#include "bcos-evm/bcos/FiscoOrchestrationProfile.h"
#include "bcos-evm/bcos/FiscoOrchestrationInternals.h"
#include "bcos-evm/bcos/FiscoConstants.h"
#include "bcos-evm/bcos/FiscoTxAdapter.h"
#include "bcos-evm/bcos/ports/AuthPort.h"
#include "bcos-evm/eth/orchestration/OrchestrationContext.h"
#include "bcos-framework/protocol/Exceptions.h"
#include <boost/throw_exception.hpp>
#include <cstring>

namespace bcos::evm {

OrchestrationHooks FiscoOrchestrationProfile::buildHooks(Session& session)
{
    auto& input = session.input;
    OrchestrationHooks hooks;

    hooks.prepareMessage = [&input](OrchestrationContext& orchestrationCtx) {
        orchestrationCtx.message = deriveMessage(FiscoTxAdapterInput{.web3Tx = input.web3Tx,
            .message = orchestrationCtx.message,
            .blockNumber = input.blockInfo.number,
            .contextID = input.contextID,
            .seq = input.seq,
            .nonce = input.nonce,
            .hashImpl = input.hashImpl});
    };

    hooks.preExecute = [&input](OrchestrationContext& orchestrationCtx) {
        if (input.revisionConfig.enable_auth_check && input.authPort != nullptr)
        {
            if (auto authResult =
                    const_cast<AuthPort*>(input.authPort)->checkAuth(orchestrationCtx.message);
                authResult.has_value())
            {
                orchestrationCtx.evmcResult = std::move(*authResult);
                orchestrationCtx.earlyExit = true;
                orchestrationCtx.exitKind = OrchestrationExitKind::PreExecuteRejected;
            }
        }
    };

    hooks.intrinsicPolicy.mode = session.eip7623Enabled ? IntrinsicDebitMode::Eip7623 :
                                                          IntrinsicDebitMode::None;
    hooks.intrinsicPolicy.authorizationListPresent = input.authorizationListPresent;
    hooks.intrinsicPolicy.authTupleCount = input.authorizations.size();
    hooks.intrinsicPolicy.accessList = input.accessList.get();
    hooks.intrinsicPolicy.web3TypedTxKind = input.web3TypedTxKind;

    hooks.preKernel = [&input, eip7623Enabled = session.eip7623Enabled](
                          OrchestrationContext& orchestrationCtx) {
        if (input.revisionConfig.enable_balance_transfer)
        {
            maybeTransferValue(orchestrationCtx.state, orchestrationCtx.message,
                input.revisionConfig.fix_delegatecall_transfer);
        }
        if (!eip7623Enabled)
        {
            if (orchestrationCtx.message.gas < BALANCE_TRANSFER_GAS)
                BOOST_THROW_EXCEPTION(protocol::OutOfGas{});
            orchestrationCtx.message.gas -= BALANCE_TRANSFER_GAS;
        }
        if (!isCreateKind(orchestrationCtx.message.kind))
        {
            auto const code =
                orchestrationCtx.state.get_code(orchestrationCtx.message.code_address);
            if (code.empty() && orchestrationCtx.message.input_size > 0)
                BOOST_THROW_EXCEPTION(NotFoundCodeError{});
        }
    };

    hooks.tuneKernelInput = [&input](ExecuteMessageInput& executeInput) {
        executeInput.fixStorageStatus = input.revisionConfig.fix_storage_status;
        executeInput.fixNonceInit = input.revisionConfig.fix_nonce_init;
        executeInput.revisionConfig = input.revisionConfig.eth();
    };

    hooks.postAdopt = [](OrchestrationContext& orchestrationCtx) {
        if ((orchestrationCtx.message.kind == EVMC_CREATE ||
                orchestrationCtx.message.kind == EVMC_CREATE2) &&
            orchestrationCtx.evmcResult.status_code == EVMC_SUCCESS &&
            std::memcmp(orchestrationCtx.evmcResult.create_address.bytes, EMPTY_EVM_ADDRESS.bytes,
                sizeof(orchestrationCtx.evmcResult.create_address.bytes)) == 0)
        {
            orchestrationCtx.evmcResult.create_address = orchestrationCtx.message.recipient;
        }
    };

    hooks.postSettle = [fixRevertLogs = input.revisionConfig.fix_revert_logs](
                           OrchestrationContext& orchestrationCtx) {
        if (fixRevertLogs && orchestrationCtx.evmcResult.status_code != EVMC_SUCCESS)
            orchestrationCtx.kernelOutput.logs.clear();
    };

    return hooks;
}

}  // namespace bcos::evm
```

**注意：** `EMPTY_EVM_ADDRESS` 需 `#include "bcos-evm/bcos/FiscoConstants.h"` 或等价定义。

- [ ] **Step 2: 运行 Profile 单测**

Run:

```bash
cd build && ctest -R FiscoOrchestrationProfile --output-on-failure
```

Expected: 3 tests PASS

- [ ] **Step 3: Commit**

```bash
rtk git add bcos-evm/bcos/FiscoOrchestrationProfile.cpp
rtk git commit -m "feat(bcos-evm): implement FiscoOrchestrationProfile buildHooks"
```

---

### Task 4: 瘦身 `ExecuteViaHost.cpp` + 接 Profile

**Files:**
- Modify: `bcos-evm/bcos/ExecuteViaHost.cpp`

**Interfaces:**
- Consumes: `FiscoOrchestrationProfile::buildHooks`, `FiscoOrchestrationInternals.h`（`NotFoundCodeError` catch）
- Produces: 行为等价的 `executeViaHost()`

- [ ] **Step 1: 替换 hook inline 块为 Profile 调用**

在 `#include` 区追加：

```cpp
#include "bcos-evm/bcos/FiscoOrchestrationProfile.h"
#include "bcos-evm/bcos/FiscoOrchestrationInternals.h"
```

删除匿名 namespace 中已迁出的 helper（`maybeTransferValue`、`NotFoundCodeError` 等）；**保留** `convertLogs`。

将 L234-339（链策略 hook）替换为：

```cpp
    FiscoOrchestrationProfile::Session session{
        input, output, extension, fixErrorHandling, eip7623Enabled};
    auto hooks = FiscoOrchestrationProfile::buildHooks(session);

    // TODO: OrchestrationErrorPolicy (candidate 4) — mapIntrinsicFailure / mapException
    hooks.mapIntrinsicFailure = [fixErrorHandling, hashImpl = input.hashImpl](
                                    OrchestrationContext& orchestrationCtx,
                                    IntrinsicDebitFailure failure) {
        // 从原 ExecuteViaHost.cpp L266-287 原样保留
    };

    hooks.mapException = [fixErrorHandling, hashImpl = input.hashImpl](
                             OrchestrationContext& c, std::exception_ptr exceptionPtr) {
        // 从原 ExecuteViaHost.cpp L341-384 原样保留（catch NotFoundCodeError）
    };

    runOrchestration(ctx, hooks);
```

`deriveMessage` 实现**保留**在 `ExecuteViaHost.cpp`（已在 `FiscoTxAdapter.h` 声明；后续可迁 `FiscoTxAdapter.cpp` 但不属 P1）。

- [ ] **Step 2: 编译**

Run:

```bash
cmake --build build --target bcos-evm-bcos ExecuteViaHostSmokeTest BcosAuthOrchestratorHookTest FiscoOrchestrationProfileTest -j$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
```

Expected: 全部 build OK

- [ ] **Step 3: Commit**

```bash
rtk git add bcos-evm/bcos/ExecuteViaHost.cpp
rtk git commit -m "refactor(bcos-evm): executeViaHost delegates hooks to FiscoOrchestrationProfile"
```

---

### Task 5: Regression gate

**Files:** （无新文件）

- [ ] **Step 1: 运行 P1 相关测试**

Run:

```bash
cd build && ctest -R 'FiscoOrchestrationProfile|ExecuteViaHostSmoke|BcosAuthOrchestratorHook' --output-on-failure
```

Expected: 全部 PASS

- [ ] **Step 2: 运行扩展 FISCO smoke（可选但推荐）**

Run:

```bash
cd build && ctest -R 'ExecuteViaHost|Bcos7702|Bcos7212|Bcos7623|FiscoHostExtension' --output-on-failure
```

Expected: 无回归

- [ ] **Step 3: ADR-005 门禁 — eth 无 bcos include**

Run:

```bash
rg '#include.*bcos-evm/bcos' bcos-evm/eth --glob '*.h' --glob '*.cpp'
```

Expected: 无匹配

- [ ] **Step 4: 最终 commit（若有遗漏文件）**

```bash
rtk git status
# 若有 unstaged fix:
rtk git add -A && rtk git commit -m "test(bcos-evm): verify Fisco OrchestrationProfile P1 regression green"
```

---

## Spec Self-Review

| Spec 要求 | 对应 Task |
| --- | --- |
| Session + buildHooks | Task 1, 3 |
| error hook 留 wrapper | Task 4 |
| Input/Output 不变 | 全 plan 无 Input/Output 修改 |
| ≥3 Profile test cases | Task 2 |
| smoke regression | Task 5 |
| 不新增公开 include | 无 `include/bcos-evm/` 变更 |
| ADR-005 | Task 5 Step 3 |
| P2/P3 不在范围 | 未包含 OpStack/Eth Profile |

无 TBD / placeholder。

---

## Follow-up（不在 P1）

| Phase | 内容 | 前置 |
| --- | --- | --- |
| P2 | `OpStackOrchestrationProfile` | 无；Session 含 `OpStackTxExecutionData&` |
| P3 | `EthOrchestrationProfile` | ExecutionFrame PR2 |
| — | OrchestrationErrorPolicy（候选 4） | 独立 plan |

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-24-orchestration-profile-p1-fisco.md`. Two execution options:

**1. Subagent-Driven (recommended)** — 每个 Task 派 fresh subagent，Task 间 review，迭代快

**2. Inline Execution** — 本会话按 Task 顺序直接实现，checkpoint 处暂停 review

Which approach?
