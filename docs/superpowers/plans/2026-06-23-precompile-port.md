# Precompile Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 FISCO chain precompile 执行与 auth 从 `bcos-evm/bcos/` 解耦至 TE `adapters/`，通过 `ChainPrecompilePort` / `AuthPort` 注入；`bcos-evm` 源码零 `bcos-executor` include。

**Architecture:** 拆分 Port（bcos-evm header-only）+ Prod adapter（TE `adapters/`，封装 `PrecompiledManager` / `AuthCheck`）+ Test adapter（`bcos-evm/test/bcos/adapters/`）；`ExecuteViaHostInput` 显式 `const*` Port 指针；Extension 只读 `FiscoHostExtensionDeps`（SSoT）；P1 单 PR big bang。

**Tech Stack:** C++20、Boost.Test、EVMC、evmone、`bcos-evm-bcos`、`transaction-executor`（UNITY_BUILD ON）。

**Spec:** [`docs/superpowers/specs/2026-06-23-precompile-port-design.md`](../specs/2026-06-23-precompile-port-design.md) v1.2

## Global Constraints

- **P1 big bang** — 单 PR；文件用 `git mv` 保留历史。
- `bcos-evm` 源码（`*.cpp` / `*.h` / `*.hpp` / `CMakeLists.txt`）**零** `#include "bcos-executor/..."`。
- **G1=A** — `bcos-executor/src/vm/Precompiled.h` include 改指向 `adapters/Precompiled.h`（同一 PR）。
- **勿**在 `bcos-evm/bcos/` 保留 deprecated re-export shim。
- 合并 `PrecompiledManager.cpp` 时 **以 TE 活跃 TU 为准**；删 orphan `bcos-evm/bcos/PrecompiledManager.cpp`。
- Port 指针 SSoT = `FiscoHostExtensionDeps`；Extension 不读 `ExecuteViaHostInput` Port 字段。
- `createAuthTable` **不**绑定 `enable_auth_check`；`chainPrecompilePort == nullptr` 时 `tryChainPrecompile` **入口即** return（routing 前）。
- 不修改 PrecompileRouter kernel precedence；不迁移 ADR-007 Web3 decoder。
- ADR 文件编号：**017**（spec 写 ADR-008 但仓库 008 已占用 → `017-fisco-precompile-port.md`）。
- Shell/git 命令前缀 `rtk`（仓库 CLAUDE.md）。

---

## File Map

| 文件 | 动作 | 职责 |
|------|------|------|
| `bcos-evm/bcos/ports/ChainPrecompilePort.h` | Create | 帧级 precompile Port |
| `bcos-evm/bcos/ports/AuthPort.h` | Create | tx 级 auth Port |
| `bcos-evm/bcos/ExecuteViaHost.h/.cpp` | Modify | Port 指针；删 lambda typedef 字段 |
| `bcos-evm/bcos/FiscoHostExtension.h/.cpp` | Modify | deps Port；单构造；删 caller typedef |
| `bcos-evm/test/bcos/adapters/InMemoryChainPrecompileAdapter.h` | Create | Test stub |
| `bcos-evm/test/bcos/adapters/InMemoryAuthAdapter.h` | Create | Test stub |
| `bcos-evm/test/bcos/adapters/TestPortAdapter.h` | Create | lambda → Port 薄 wrapper |
| `bcos-evm/test/bcos/ChainPrecompilePortTest.cpp` | Create | Port wiring 单测 |
| `bcos-evm/test/bcos/BcosAuthOrchestratorHookTest.cpp` | Modify | `authChecker` → `InMemoryAuthAdapter` |
| `bcos-evm/test/bcos/FiscoHostExtensionTest.cpp` | Create | CMake 已引用但文件缺失 — 补最小 Port 测试 |
| `bcos-evm/test/eth/PrecompileRouterCharacterizationTest.cpp` | Modify | C7 callback → Port |
| `transaction-executor/bcos-transaction-executor/adapters/*` | git mv + Create | 迁出 precompile 实现 + prod adapter |
| `transaction-executor/bcos-transaction-executor/TransactionExecutorImpl.h` | Modify | 栈 adapter；删 lambda |
| `transaction-executor/CMakeLists.txt` | Modify | `precompiled/` → `adapters/` |
| `bcos-executor/src/vm/Precompiled.h` | Modify | G1=A shim |
| `transaction-executor/tests/CompatExecuteViaHostPhase*Test.cpp` | Modify | include → `adapters/` |
| `transaction-executor/tests/Modexp7823TeTest.cpp` | Modify | include → `adapters/` |
| `bcos-evm/docs/adr/017-fisco-precompile-port.md` | Create | ADR |
| `bcos-evm/capability-matrix.md` | Modify | architecture note |
| `.github/workflows/capability-gate.yml` | Modify | compile-boundary grep step |

**删除：**

- `bcos-evm/bcos/PrecompiledManager.cpp`（orphan）
- `bcos-evm/bcos/PrecompiledManager.h`、`PrecompiledImpl.h`、`Precompiled.h`、`PrecompiledEntry.h`、`AuthCheck.h`（git mv 后不存在于 bcos-evm）
- `transaction-executor/.../precompiled/` 下全部空壳 + 已迁 `.cpp`

---

### Task 1: Port interface headers

**Files:**
- Create: `bcos-evm/bcos/ports/ChainPrecompilePort.h`
- Create: `bcos-evm/bcos/ports/AuthPort.h`

**Interfaces:**
- Consumes: `<evmc/evmc.h>`, `bcos-evm/eth/EVMCResult.h`
- Produces: `bcos::evm::ChainPrecompilePort`, `bcos::evm::AuthPort` — 供 Task 2–7 实现/注入

- [ ] **Step 1: 创建 `ChainPrecompilePort.h`**

```cpp
#pragma once

#include <evmc/evmc.h>
#include <optional>

namespace bcos::evm {

struct ChainPrecompilePort {
    virtual ~ChainPrecompilePort() = default;
    virtual std::optional<evmc_result> dispatch(
        evmc_revision rev, evmc_message const& msg) = 0;
};

}  // namespace bcos::evm
```

- [ ] **Step 2: 创建 `AuthPort.h`**

```cpp
#pragma once

#include "bcos-evm/eth/EVMCResult.h"
#include <evmc/evmc.h>
#include <optional>
#include <string_view>

namespace bcos::evm {

struct AuthPort {
    virtual ~AuthPort() = default;
    virtual std::optional<EVMCResult> checkAuth(evmc_message const& msg) = 0;
    virtual void createAuthTable(evmc_message const& msg, std::string_view tablePath) = 0;
};

}  // namespace bcos::evm
```

- [ ] **Step 3: 验证 Port header 无 executor 依赖**

Run:

```bash
rtk grep 'bcos-executor' bcos-evm/bcos/ports/
```

Expected: 无匹配

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-evm/bcos/ports/ChainPrecompilePort.h bcos-evm/bcos/ports/AuthPort.h
rtk git commit -m "$(cat <<'EOF'
feat(bcos-evm): add ChainPrecompilePort and AuthPort interfaces

EOF
)"
```

---

### Task 2: Test adapters + Port wiring tests

**Files:**
- Create: `bcos-evm/test/bcos/adapters/InMemoryChainPrecompileAdapter.h`
- Create: `bcos-evm/test/bcos/adapters/InMemoryAuthAdapter.h`
- Create: `bcos-evm/test/bcos/adapters/TestPortAdapter.h`
- Create: `bcos-evm/test/bcos/ChainPrecompilePortTest.cpp`
- Modify: `bcos-evm/test/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 Port headers, `FiscoHostExtension`
- Produces: `InMemoryChainPrecompileAdapter`, `InMemoryAuthAdapter` — 供 Task 4 测试迁移

- [ ] **Step 1: 添加 CMake target**

在 `bcos-evm/test/CMakeLists.txt` 的 `FiscoHostExtensionTest` 块 **之前**插入：

```cmake
set(CHAIN_PRECOMPILE_PORT_TEST_BINARY_NAME ChainPrecompilePortTest)

add_executable(${CHAIN_PRECOMPILE_PORT_TEST_BINARY_NAME}
    bcos/ChainPrecompilePortTest.cpp
)

target_include_directories(${CHAIN_PRECOMPILE_PORT_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${CHAIN_PRECOMPILE_PORT_TEST_BINARY_NAME} PRIVATE
    bcos-evm
)

add_test(
    NAME ChainPrecompilePort
    COMMAND ${CHAIN_PRECOMPILE_PORT_TEST_BINARY_NAME}
)
```

- [ ] **Step 2: 创建 `InMemoryChainPrecompileAdapter.h`**

```cpp
#pragma once

#include "bcos-evm/bcos/ports/ChainPrecompilePort.h"
#include <functional>
#include <optional>

namespace bcos::evm::test {

class InMemoryChainPrecompileAdapter final : public ChainPrecompilePort {
public:
    using Handler = std::function<std::optional<evmc_result>(evmc_revision, evmc_message const&)>;

    explicit InMemoryChainPrecompileAdapter(Handler handler = {})
      : m_handler(std::move(handler))
    {}

    std::optional<evmc_result> dispatch(
        evmc_revision rev, evmc_message const& msg) override
    {
        if (!m_handler)
        {
            return std::nullopt;
        }
        return m_handler(rev, msg);
    }

private:
    Handler m_handler;
};

}  // namespace bcos::evm::test
```

- [ ] **Step 3: 创建 `InMemoryAuthAdapter.h`**

```cpp
#pragma once

#include "bcos-evm/bcos/ports/AuthPort.h"
#include <functional>
#include <optional>

namespace bcos::evm::test {

class InMemoryAuthAdapter final : public AuthPort {
public:
    using CheckHandler = std::function<std::optional<EVMCResult>(evmc_message const&)>;
    using CreateHandler = std::function<void(evmc_message const&, std::string_view)>;

    explicit InMemoryAuthAdapter(CheckHandler check = {}, CreateHandler create = {})
      : m_check(std::move(check)), m_create(std::move(create))
    {}

    std::optional<EVMCResult> checkAuth(evmc_message const& msg) override
    {
        return m_check ? m_check(msg) : std::nullopt;
    }

    void createAuthTable(evmc_message const& msg, std::string_view tablePath) override
    {
        if (m_create)
        {
            m_create(msg, tablePath);
        }
    }

private:
    CheckHandler m_check;
    CreateHandler m_create;
};

}  // namespace bcos::evm::test
```

- [ ] **Step 4: 创建 `TestPortAdapter.h`（可选 alias，与 InMemory* 等价）**

```cpp
#pragma once

#include "InMemoryAuthAdapter.h"
#include "InMemoryChainPrecompileAdapter.h"

namespace bcos::evm::test {
using TestAuthPort = InMemoryAuthAdapter;
using TestChainPrecompilePort = InMemoryChainPrecompileAdapter;
}  // namespace bcos::evm::test
```

- [ ] **Step 5: 编写 `ChainPrecompilePortTest.cpp`（先 RED — 依赖 Task 3 单构造）**

```cpp
#define BOOST_TEST_MODULE ChainPrecompilePortTest

#include "bcos-evm/bcos/FiscoHostExtension.h"
#include "bcos/test/bcos/adapters/InMemoryChainPrecompileAdapter.h"
#include <boost/test/included/unit_test.hpp>
#include <cstring>

namespace bcos::evm::test {
namespace {

evmc_address fiscoPrecompileAddress(uint16_t suffix)
{
    evmc_address addr{};
    addr.bytes[18] = static_cast<uint8_t>((suffix >> 8U) & 0xFFU);
    addr.bytes[19] = static_cast<uint8_t>(suffix & 0xFFU);
    return addr;
}

}  // namespace

BOOST_AUTO_TEST_CASE(null_port_returns_before_routing)
{
    FiscoHostExtension::FiscoHostExtensionDeps deps;
    deps.chainPrecompilePort = nullptr;
    FiscoHostExtension extension(true, std::move(deps));

    evmc_message msg{};
    msg.recipient = fiscoPrecompileAddress(0x1003);
    msg.code_address = msg.recipient;

    auto result = extension.tryChainPrecompile(EVMC_CANCUN, msg);
    BOOST_CHECK(!result.has_value());
}

BOOST_AUTO_TEST_CASE(port_dispatch_invoked_for_fisco_address)
{
    bool invoked = false;
    InMemoryChainPrecompileAdapter port([&invoked](evmc_revision /*rev*/,
                                        evmc_message const& message) -> std::optional<evmc_result> {
        invoked = true;
        evmc_result raw{};
        raw.status_code = EVMC_SUCCESS;
        raw.gas_left = message.gas;
        return raw;
    });

    FiscoHostExtension::FiscoHostExtensionDeps deps;
    deps.chainPrecompilePort = &port;
    FiscoHostExtension extension(true, std::move(deps));

    evmc_message msg{};
    msg.gas = 50'000;
    msg.recipient = fiscoPrecompileAddress(0x1003);
    msg.code_address = msg.recipient;

    auto result = extension.tryChainPrecompile(EVMC_CANCUN, msg);
    BOOST_REQUIRE(result.has_value());
    BOOST_CHECK(invoked);
    BOOST_CHECK_EQUAL(result->status_code, EVMC_SUCCESS);
}

}  // namespace bcos::evm::test
```

> **Note:** include 路径 `bcos/test/bcos/adapters/` 对应 `bcos-evm/test/bcos/adapters/`（CMake `target_include_directories` 已含 `${CMAKE_CURRENT_SOURCE_DIR}`）。

- [ ] **Step 6: 运行测试（Task 3 完成前预期 compile FAIL）**

Run:

```bash
cmake --build build --target ChainPrecompilePortTest
ctest -R ChainPrecompilePort --output-on-failure
```

Expected（Task 3 前）: 编译失败（`FiscoHostExtension` 尚无 `chainPrecompilePort` deps / 单构造）

- [ ] **Step 7: Commit test scaffolding**

```bash
rtk git add bcos-evm/test/bcos/adapters/ bcos-evm/test/bcos/ChainPrecompilePortTest.cpp bcos-evm/test/CMakeLists.txt
rtk git commit -m "$(cat <<'EOF'
test(bcos-evm): add Port test adapters and ChainPrecompilePortTest

EOF
)"
```

---

### Task 3: Wire ExecuteViaHost + FiscoHostExtension to Ports

**Files:**
- Modify: `bcos-evm/bcos/FiscoHostExtension.h`
- Modify: `bcos-evm/bcos/FiscoHostExtension.cpp`
- Modify: `bcos-evm/bcos/ExecuteViaHost.h`
- Modify: `bcos-evm/bcos/ExecuteViaHost.cpp`

**Interfaces:**
- Consumes: Task 1 `ChainPrecompilePort`, `AuthPort`
- Produces: `ExecuteViaHostInput{ authPort, chainPrecompilePort }`；`FiscoHostExtension(bool, FiscoHostExtensionDeps)` 单构造；`deps.chainPrecompilePort` / `deps.authPort`

- [ ] **Step 1: 更新 `FiscoHostExtension.h`**

关键 diff：

```cpp
#include "bcos-evm/bcos/ports/AuthPort.h"
#include "bcos-evm/bcos/ports/ChainPrecompilePort.h"
// 删除: class PrecompiledManager; forward decl
// 删除: FiscoPrecompileCaller, CreateAuthTableInvoker typedef

struct FiscoHostExtensionDeps {
    // ... 保留 storageRef, blockHeader, ledgerConfig, blockNumber, contextID, seq,
    //     hashImpl, persistContractCreateNonce, origin, revisionFlags, state,
    //     recipientPathResolver ...
    AuthPort const* authPort{nullptr};
    ChainPrecompilePort const* chainPrecompilePort{nullptr};
    // 删除: precompiledManager, externalCaller, createAuthTableInvoker
};

explicit FiscoHostExtension(bool skipEvmNativeValueTransfer, FiscoHostExtensionDeps deps);
// 删除双构造 overload

private:
    AuthPort const* m_authPort{nullptr};
    ChainPrecompilePort const* m_chainPrecompilePort{nullptr};
    // 删除: m_precompileCaller, m_precompiledManager, m_externalCaller, m_createAuthTableInvoker
```

- [ ] **Step 2: 更新 `FiscoHostExtension.cpp`**

```cpp
FiscoHostExtension::FiscoHostExtension(
    bool skipEvmNativeValueTransfer, FiscoHostExtensionDeps deps)
  : m_skipEvmNativeValueTransfer(skipEvmNativeValueTransfer),
    m_authPort(deps.authPort),
    m_chainPrecompilePort(deps.chainPrecompilePort),
    m_storageRef(deps.storageRef),
    // ... 其余 deps 字段赋值 ...
{
    if (deps.recipientPathResolver) {
        m_recipientPathResolver = std::move(deps.recipientPathResolver);
    } else {
        m_recipientPathResolver = [](const evmc_message& message) {
            return std::string(USER_APPS_PREFIX) + hexAddress(message.recipient);
        };
    }
}

std::optional<evmc_result> FiscoHostExtension::tryChainPrecompile(
    evmc_revision rev, const evmc_message& msg)
{
    if (m_chainPrecompilePort == nullptr) {
        return std::nullopt;
    }
    // ... 保留现有 routing（parseDynamicPrecompileTarget / isFiscoPrecompileAddress）...
    return m_chainPrecompilePort->dispatch(rev, routedMessage);
}

void FiscoHostExtension::prepareMessage(evmc_revision rev, evmc_message& msg)
{
    // ...
    if (m_blockNumber != 0 && m_authPort != nullptr) {
        m_authPort->createAuthTable(msg, resolveAuthTablePath(msg));
    }
    // ...
}
```

- [ ] **Step 3: 更新 `ExecuteViaHost.h`**

```cpp
#include "bcos-evm/bcos/ports/AuthPort.h"
#include "bcos-evm/bcos/ports/ChainPrecompilePort.h"

struct ExecuteViaHostInput {
    // ... 现有字段 ...
    AuthPort const* authPort{nullptr};
    ChainPrecompilePort const* chainPrecompilePort{nullptr};
    std::function<void(const evmc_address&, uint64_t)> persistContractCreateNonce;
    FiscoHostExtension::RecipientPathResolver recipientPathResolver;
    // 删除: authChecker, precompileCaller, createAuthTableInvoker
};
```

- [ ] **Step 4: 更新 `ExecuteViaHost.cpp` auth + extension 构造**

替换现有块（约 L219–301）：

```cpp
if (input.revisionConfig.enable_auth_check && input.authPort != nullptr) {
    if (auto authResult = input.authPort->checkAuth(message)) {
        output.evmcResult = std::move(*authResult);
        co_return output;
    }
}

FiscoHostExtension::FiscoHostExtensionDeps deps;
deps.authPort = input.authPort;
deps.chainPrecompilePort = input.chainPrecompilePort;
// ... populate 其余 deps 字段（storageRef, blockHeader, state, ...）...
FiscoHostExtension extension(input.revisionConfig.enable_balance_transfer, std::move(deps));
```

- [ ] **Step 5: 构建并运行 Task 2 测试**

Run:

```bash
cmake --build build --target ChainPrecompilePortTest bcos-evm-bcos
ctest -R ChainPrecompilePort --output-on-failure
```

Expected: PASS

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/bcos/FiscoHostExtension.h bcos-evm/bcos/FiscoHostExtension.cpp \
  bcos-evm/bcos/ExecuteViaHost.h bcos-evm/bcos/ExecuteViaHost.cpp
rtk git commit -m "$(cat <<'EOF'
feat(bcos-evm): wire ExecuteViaHost and FiscoHostExtension to Port interfaces

EOF
)"
```

---

### Task 4: Migrate bcos-evm unit tests to Port adapters

**Files:**
- Modify: `bcos-evm/test/bcos/BcosAuthOrchestratorHookTest.cpp`
- Create: `bcos-evm/test/bcos/FiscoHostExtensionTest.cpp`
- Modify: `bcos-evm/test/eth/PrecompileRouterCharacterizationTest.cpp`
- Modify: `bcos-evm/test/CMakeLists.txt`（`FiscoHostExtensionTest` include path）

**Interfaces:**
- Consumes: Task 2 `InMemoryAuthAdapter`, `InMemoryChainPrecompileAdapter`
- Produces: 绿测 `BcosAuthOrchestratorHook`, `FiscoHostExtension`, `PrecompileRouterCharacterization`

- [ ] **Step 1: 迁移 `BcosAuthOrchestratorHookTest.cpp`**

```cpp
#include "bcos/test/bcos/adapters/InMemoryAuthAdapter.h"

// 替换 input.authChecker = lambda 为:
InMemoryAuthAdapter authPort([](evmc_message const&) -> std::optional<EVMCResult> {
    evmc_result fail{};
    fail.status_code = EVMC_REJECTED;
    fail.gas_left = 0;
    return EVMCResult(fail, protocol::TransactionStatus::PermissionDenied);
});
input.authPort = &authPort;
```

- [ ] **Step 2: 创建 `FiscoHostExtensionTest.cpp`（CMake 已引用，文件缺失）**

```cpp
#define BOOST_TEST_MODULE FiscoHostExtensionTest

#include "bcos-evm/bcos/FiscoHostExtension.h"
#include "bcos/test/bcos/adapters/InMemoryAuthAdapter.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test {

BOOST_AUTO_TEST_CASE(create_auth_table_runs_without_enable_auth_check)
{
    bool createCalled = false;
    InMemoryAuthAdapter authPort({}, [&createCalled](evmc_message const&, std::string_view path) {
        createCalled = true;
        BOOST_CHECK(!path.empty());
    });

    FiscoHostExtension::FiscoHostExtensionDeps deps;
    deps.blockNumber = 1;
    deps.authPort = &authPort;
    FiscoHostExtension extension(true, std::move(deps));

    evmc_message msg{};
    msg.kind = EVMC_CREATE;
    extension.prepareMessage(EVMC_CANCUN, msg);
    BOOST_CHECK(createCalled);
}

}  // namespace bcos::evm::test
```

- [ ] **Step 3: 迁移 `PrecompileRouterCharacterizationTest.cpp` C7**

将 `FiscoHostExtension(..., deps, callback)` 改为：

```cpp
#include "bcos/test/bcos/adapters/InMemoryChainPrecompileAdapter.h"

InMemoryChainPrecompileAdapter port(callback);  // callback 签名不变
deps.chainPrecompilePort = &port;
FiscoHostExtension extension(true, std::move(deps));
```

所有 C7 内 `depth0Ext` / `depth1Ext` 同样改法。

- [ ] **Step 4: 运行 bcos-evm 相关测试**

Run:

```bash
cmake --build build --target BcosAuthOrchestratorHookTest FiscoHostExtensionTest \
  PrecompileRouterCharacterizationTest
ctest -R 'BcosAuthOrchestratorHook|FiscoHostExtension|PrecompileRouterCharacterization' \
  --output-on-failure
```

Expected: PASS（TE 路径仍可能因 lambda 未删而 compile fail — Task 6 修复）

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/test/bcos/BcosAuthOrchestratorHookTest.cpp \
  bcos-evm/test/bcos/FiscoHostExtensionTest.cpp \
  bcos-evm/test/eth/PrecompileRouterCharacterizationTest.cpp
rtk git commit -m "$(cat <<'EOF'
test(bcos-evm): migrate auth and chain precompile tests to Port adapters

EOF
)"
```

---

### Task 5: git mv precompile implementation to TE adapters/

**Files:**
- git mv: `bcos-evm/bcos/{PrecompiledManager.h,PrecompiledImpl.h,Precompiled.h,PrecompiledEntry.h,AuthCheck.h}` → `transaction-executor/bcos-transaction-executor/adapters/`
- git mv: `transaction-executor/.../precompiled/{PrecompiledManager.cpp,PrecompiledEntry.cpp}` → `adapters/`
- Delete: `bcos-evm/bcos/PrecompiledManager.cpp`
- Delete: `transaction-executor/.../precompiled/{PrecompiledManager.h,PrecompiledImpl.h,AuthCheck.h,ExecutiveWrapper.h}`
- Modify: `transaction-executor/CMakeLists.txt`
- Modify: moved headers 内 `#include` 路径

**Interfaces:**
- Consumes: canonical paths §spec 5.1
- Produces: `transaction-executor/bcos-transaction-executor/adapters/Precompiled*.h/cpp`, `AuthCheck.h`

- [ ] **Step 1: git mv 头文件与 TU**

```bash
cd /path/to/repo
mkdir -p transaction-executor/bcos-transaction-executor/adapters

rtk git mv bcos-evm/bcos/PrecompiledManager.h \
  transaction-executor/bcos-transaction-executor/adapters/
rtk git mv bcos-evm/bcos/PrecompiledImpl.h \
  transaction-executor/bcos-transaction-executor/adapters/
rtk git mv bcos-evm/bcos/Precompiled.h \
  transaction-executor/bcos-transaction-executor/adapters/
rtk git mv bcos-evm/bcos/PrecompiledEntry.h \
  transaction-executor/bcos-transaction-executor/adapters/
rtk git mv bcos-evm/bcos/AuthCheck.h \
  transaction-executor/bcos-transaction-executor/adapters/

rtk git mv transaction-executor/bcos-transaction-executor/precompiled/PrecompiledManager.cpp \
  transaction-executor/bcos-transaction-executor/adapters/
rtk git mv transaction-executor/bcos-transaction-executor/precompiled/PrecompiledEntry.cpp \
  transaction-executor/bcos-transaction-executor/adapters/

rtk git rm bcos-evm/bcos/PrecompiledManager.cpp
rtk git rm transaction-executor/bcos-transaction-executor/precompiled/PrecompiledManager.h \
  transaction-executor/bcos-transaction-executor/precompiled/PrecompiledImpl.h \
  transaction-executor/bcos-transaction-executor/precompiled/AuthCheck.h \
  transaction-executor/bcos-transaction-executor/precompiled/ExecutiveWrapper.h
rmdir transaction-executor/bcos-transaction-executor/precompiled 2>/dev/null || true
```

- [ ] **Step 2: 更新 `PrecompiledManager.cpp` include**

```cpp
#include "transaction-executor/bcos-transaction-executor/adapters/PrecompiledManager.h"
```

（`PrecompiledEntry.cpp` 同理改 `PrecompiledEntry.h`）

- [ ] **Step 3: 更新 `transaction-executor/CMakeLists.txt`**

```cmake
add_library(transaction-executor
    bcos-transaction-executor/adapters/PrecompiledEntry.cpp
    bcos-transaction-executor/adapters/PrecompiledManager.cpp
    bcos-transaction-executor/TransactionExecutorImpl.cpp
)
```

- [ ] **Step 4: 验证 bcos-evm grep 边界**

Run:

```bash
rtk grep -r 'bcos-executor' bcos-evm/ \
  --include='*.cpp' --include='*.h' --include='*.hpp' --include='CMakeLists.txt'
```

Expected: 无匹配

- [ ] **Step 5: Commit**

```bash
rtk git add -A transaction-executor/ bcos-evm/bcos/
rtk git commit -m "$(cat <<'EOF'
refactor(te): move FISCO precompile implementation to adapters/

EOF
)"
```

---

### Task 6: Prod adapters + TE TransactionExecutorImpl wiring

**Files:**
- Create: `transaction-executor/bcos-transaction-executor/adapters/ExecutorSessionContext.h`
- Create: `transaction-executor/bcos-transaction-executor/adapters/ExecutorPrecompileAdapter.h`
- Create: `transaction-executor/bcos-transaction-executor/adapters/ExecutorAuthAdapter.h`
- Modify: `transaction-executor/bcos-transaction-executor/TransactionExecutorImpl.h`

**Interfaces:**
- Consumes: Task 1 Ports; Task 5 `PrecompiledManager`, `AuthCheck`, `PrecompiledImpl`
- Produces: `ExecutorPrecompileAdapter::dispatch`, `ExecutorAuthAdapter::checkAuth/createAuthTable`

- [ ] **Step 1: 创建 `ExecutorSessionContext.h`**

```cpp
#pragma once

#include "bcos-evm/bcos/FiscoRevisionConfig.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/BlockHeader.h"
#include "transaction-executor/bcos-transaction-executor/RollbackableStorage.h"
#include <evmc/evmc.h>

namespace bcos::crypto { class Hash; }
namespace bcos::evm { class PrecompiledManager; }

namespace bcos::transaction_executor {

struct ExecutorSessionContext {
    RollbackableStorage& storage;
    protocol::BlockHeader const& blockHeader;
    ledger::LedgerConfig const& ledgerConfig;
    evmc_address origin;
    int64_t contextID;
    int64_t seq;
    evm::FiscoRevisionConfig const& revisionConfig;
    crypto::Hash const& hashImpl;
    evm::PrecompiledManager& precompiledManager;
};

}  // namespace bcos::transaction_executor
```

- [ ] **Step 2: 创建 `ExecutorPrecompileAdapter.h`**

```cpp
#pragma once

#include "bcos-evm/bcos/ports/ChainPrecompilePort.h"
#include "transaction-executor/bcos-transaction-executor/adapters/ExecutorSessionContext.h"
#include "transaction-executor/bcos-transaction-executor/adapters/PrecompiledImpl.h"
#include <optional>

namespace bcos::transaction_executor {

class ExecutorPrecompileAdapter final : public evm::ChainPrecompilePort {
public:
    explicit ExecutorPrecompileAdapter(ExecutorSessionContext const& ctx)
      : m_ctx(ctx)
    {}

    std::optional<evmc_result> dispatch(
        evmc_revision rev, evmc_message const& msg) override
    {
        auto const* precompiled = m_ctx.precompiledManager.getPrecompiled(
            msg.recipient, m_ctx.revisionConfig, m_ctx.ledgerConfig.features());
        if (precompiled == nullptr) {
            return std::nullopt;
        }
        auto result = evm::callPrecompiled(*precompiled, m_ctx.storage, m_ctx.blockHeader, msg,
            m_ctx.origin, evm::noOpExternalCaller(), m_ctx.precompiledManager, m_ctx.contextID,
            m_ctx.seq, m_ctx.revisionConfig.enable_auth_check, m_ctx.revisionConfig.eth(), rev,
            m_ctx.revisionConfig.fix_error_handling);
        evmc_result raw = result;
        result.output_data = nullptr;
        result.output_size = 0;
        result.release = nullptr;
        return raw;
    }

private:
    ExecutorSessionContext const& m_ctx;
};

}  // namespace bcos::transaction_executor
```

- [ ] **Step 3: 创建 `ExecutorAuthAdapter.h`**

```cpp
#pragma once

#include "bcos-evm/bcos/ports/AuthPort.h"
#include "transaction-executor/bcos-transaction-executor/adapters/AuthCheck.h"
#include "transaction-executor/bcos-transaction-executor/adapters/ExecutorSessionContext.h"
#include "bcos-task/Wait.h"

namespace bcos::transaction_executor {

class ExecutorAuthAdapter final : public evm::AuthPort {
public:
    explicit ExecutorAuthAdapter(ExecutorSessionContext const& ctx) : m_ctx(ctx) {}

    std::optional<evm::EVMCResult> checkAuth(evmc_message const& msg) override
    {
        return evm::checkAuth(m_ctx.storage, m_ctx.blockHeader, msg, m_ctx.origin,
            m_ctx.precompiledManager, m_ctx.contextID, m_ctx.seq, m_ctx.hashImpl,
            m_ctx.revisionConfig.fix_auth_check);
    }

    void createAuthTable(evmc_message const& msg, std::string_view tablePath) override
    {
        task::syncWait(evm::createAuthTable(m_ctx.storage, m_ctx.blockHeader, msg, m_ctx.origin,
            tablePath, evm::noOpExternalCaller(), m_ctx.precompiledManager, m_ctx.contextID,
            m_ctx.seq, m_ctx.ledgerConfig));
    }

private:
    ExecutorSessionContext const& m_ctx;
};

}  // namespace bcos::transaction_executor
```

- [ ] **Step 4: 更新 `TransactionExecutorImpl.h` `executeViaHostTx()`**

替换 `#include`：

```cpp
#include "adapters/AuthCheck.h"
#include "adapters/PrecompiledImpl.h"
#include "adapters/PrecompiledManager.h"
#include "adapters/ExecutorAuthAdapter.h"
#include "adapters/ExecutorPrecompileAdapter.h"
#include "adapters/ExecutorSessionContext.h"
```

替换 lambda 块（L300–335）为：

```cpp
transaction_executor::ExecutorSessionContext sessionCtx{
    m_data->m_rollbackableStorage,
    m_data->m_blockHeader.get(),
    m_data->m_ledgerConfig.get(),
    m_data->m_origin,
    m_data->m_contextID,
    m_data->m_seq,
    m_data->m_executionContext.revisionConfig,
    *m_data->m_executor.get().m_hashImpl,
    *m_data->m_executor.get().m_precompiledManager};

transaction_executor::ExecutorAuthAdapter authAdapter{sessionCtx};
transaction_executor::ExecutorPrecompileAdapter precompileAdapter{sessionCtx};

input.authPort = &authAdapter;
input.chainPrecompilePort = &precompileAdapter;
```

- [ ] **Step 5: 构建 transaction-executor**

Run:

```bash
cmake --build build --target transaction-executor
```

Expected: 成功

- [ ] **Step 6: Commit**

```bash
rtk git add transaction-executor/bcos-transaction-executor/adapters/Executor*.h \
  transaction-executor/bcos-transaction-executor/TransactionExecutorImpl.h
rtk git commit -m "$(cat <<'EOF'
feat(te): add prod Port adapters and wire TransactionExecutorImpl

EOF
)"
```

---

### Task 7: Cross-repo include fixes (G1=A)

**Files:**
- Modify: `bcos-executor/src/vm/Precompiled.h`
- Modify: `transaction-executor/tests/CompatExecuteViaHostPhaseBTest.cpp`
- Modify: `transaction-executor/tests/CompatExecuteViaHostPhaseCTest.cpp`
- Modify: `transaction-executor/tests/CompatExecuteViaHostPhaseETest.cpp`
- Modify: `transaction-executor/tests/Modexp7823TeTest.cpp`

**Interfaces:**
- Consumes: Task 5 canonical adapter paths
- Produces: 全树 compile 通过

- [ ] **Step 1: 更新 `bcos-executor/src/vm/Precompiled.h`**

```cpp
#include "transaction-executor/bcos-transaction-executor/adapters/Precompiled.h"
```

- [ ] **Step 2: 更新 TE compat 测试 includes**

| 文件 | 旧 | 新 |
|------|----|----|
| `CompatExecuteViaHostPhaseBTest.cpp` | `bcos-evm/bcos/PrecompiledImpl.h` | `transaction-executor/bcos-transaction-executor/adapters/PrecompiledImpl.h` |
| `CompatExecuteViaHostPhaseCTest.cpp` | 同上 | 同上 |
| `CompatExecuteViaHostPhaseETest.cpp` | `bcos-evm/bcos/PrecompiledManager.h`, `AuthCheck.h` | `adapters/` 路径 |
| `Modexp7823TeTest.cpp` | `bcos-evm/bcos/PrecompiledImpl.h` | `adapters/PrecompiledImpl.h` |

- [ ] **Step 3: 全量回归**

Run:

```bash
cmake --build build --target bcos-evm-eth bcos-evm-bcos transaction-executor
ctest -R 'CompatExecuteViaHost|ExecuteViaHostCompat' --output-on-failure
ctest --test-dir build/transaction-executor/test --output-on-failure
ctest --test-dir build/bcos-evm/test --output-on-failure
```

Expected: 全绿

- [ ] **Step 4: Commit**

```bash
rtk git add bcos-executor/src/vm/Precompiled.h transaction-executor/tests/
rtk git commit -m "$(cat <<'EOF'
fix: update executor and TE compat includes for adapters/ canonical path

EOF
)"
```

---

### Task 8: CI gate, ADR-017, capability-matrix, docs

**Files:**
- Create: `bcos-evm/docs/adr/017-fisco-precompile-port.md`
- Modify: `bcos-evm/capability-matrix.md`
- Modify: `.github/workflows/capability-gate.yml`
- Modify: `docs/superpowers/specs/2026-06-23-bcos-evm-remaining-architecture-tasks.md`

- [ ] **Step 1: 创建 ADR-017**

```markdown
# ADR-017: FISCO Precompile Port

**Status:** Accepted  
**Date:** 2026-06-23  
**Related:** ADR-005, spec 2026-06-23-precompile-port-design v1.2

## Decision

FISCO chain precompile 执行与 auth 经 `ChainPrecompilePort` / `AuthPort` 注入；
implementation 驻 `transaction-executor/adapters/` + `bcos-executor` precompiled TU；
`bcos-evm` 源码零 `bcos-executor` include。

## Consequences

- compile-boundary grep 可固化（A-2）
- bcos-evm 单测可 mock Port
- PrecompileRouter（kernel）与 Port（chain）正交
```

- [ ] **Step 2: 扩展 `capability-gate.yml` matrix-lint job**

在 `Lint capability matrix tokens` 之后追加：

```yaml
      - name: Compile boundary — bcos-evm zero bcos-executor includes
        run: |
          set -euo pipefail
          if grep -r 'bcos-executor' bcos-evm/ \
              --include='*.cpp' --include='*.h' --include='*.hpp' \
              --include='CMakeLists.txt'; then
            echo "error: bcos-evm must not include bcos-executor" >&2
            exit 1
          fi
          if grep -rE 'bcos/Fisco' bcos-evm/eth/ \
              --include='*.cpp' --include='*.h' --include='*.hpp'; then
            echo "error: bcos-evm/eth must not include bcos/Fisco" >&2
            exit 1
          fi
```

- [ ] **Step 3: 更新 `capability-matrix.md`**

在 orchestration / BCOS 节追加一行 note（Test ref 不变）：

```markdown
| FISCO chain precompile dispatch | orchestration | Port (`ChainPrecompilePort`) via TE adapter | CompatExecuteViaHost* |
```

- [ ] **Step 4: 更新 remaining-architecture-tasks A-1 / A-2 → [x]**

- [ ] **Step 5: 最终验收命令**

Run:

```bash
rtk grep -r 'bcos-executor' bcos-evm/ \
  --include='*.cpp' --include='*.h' --include='*.hpp' --include='CMakeLists.txt'
rtk grep -rE 'bcos/Fisco' bcos-evm/eth/ \
  --include='*.cpp' --include='*.h' --include='*.hpp'
ctest -R 'CompatExecuteViaHost|BcosAuthOrchestratorHook|FiscoHostExtension|PrecompileRouter|ChainPrecompilePort' \
  --output-on-failure
```

Expected: grep 无输出；ctest 全 PASS

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/docs/adr/017-fisco-precompile-port.md \
  bcos-evm/capability-matrix.md .github/workflows/capability-gate.yml \
  docs/superpowers/specs/2026-06-23-bcos-evm-remaining-architecture-tasks.md
rtk git commit -m "$(cat <<'EOF'
docs: ADR-017 precompile port, compile-boundary CI gate, matrix note

EOF
)"
```

---

## Spec Coverage Self-Review

| Spec § | Task |
|--------|------|
| §4 Port interfaces | Task 1 |
| §4.5 Nullable semantics | Task 2–3 tests |
| §5 Prod adapter + session | Task 6 |
| §5.1 git mv + TE TU | Task 5 |
| §5.2 SSoT deps | Task 3 |
| §6 ExecuteViaHost / Extension | Task 3 |
| §7 Test adapter | Task 2, 4 |
| §8 CMake / grep | Task 5, 8 |
| §9.3 全文件清单 | Task 4, 5, 7 |
| §10 验收 | Task 7, 8 |
| §12 ADR | Task 8（017 非 spec 写的 008） |
| G1=A executor shim | Task 7 |

**Gap note:** spec §9.1 写 `ExecutorPrecompileAdapter.cpp` — 本计划用 **header-only** adapter（lambda 体足够小）；若 UNITY_BUILD 报 ODR/inline 问题再拆 `.cpp`（YAGNI）。

---

**Plan complete and saved to `docs/superpowers/plans/2026-06-23-precompile-port.md`. Two execution options:**

**1. Subagent-Driven (recommended)** — 每个 Task 派 fresh subagent，Task 间 review，迭代快

**2. Inline Execution** — 本会话用 executing-plans 批量执行，checkpoint 处 review

**Which approach?**
