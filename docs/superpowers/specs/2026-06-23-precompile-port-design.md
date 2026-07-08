# Precompile Port — bcos-evm 解耦 bcos-executor — 设计规格

**日期：** 2026-06-23  
**版本：** v1.2（grilling 闭合）  
**状态：** 已批准  
**任务 ID：** A-1（[remaining-architecture-tasks](./2026-06-23-bcos-evm-remaining-architecture-tasks.md)）  
**迁移策略：** **P1 Big bang** — 单 PR 完成 Port 定义、文件迁移、TE wiring、删 lambda  
**Grilling 决策：** 方案 **B**（拆分 Port）+ **B2**（Manager 整包迁入 TE adapter）+ **B2a**（`ExecuteViaHostInput` 显式 Port 指针）+ **G1=A**（`bcos-executor` include shim 纳入同一 PR）

**前置：**

- [2026-06-18-bcos-evm-layer-refactor-design.md](./2026-06-18-bcos-evm-layer-refactor-design.md) Step 2 §5.1、§5.3、§13.3
- [2026-06-23-precompile-router-design.md](./2026-06-23-precompile-router-design.md)（Phase 1 已完成；本 spec 为独立 follow-up）
- `bcos-evm/docs/adr/005-orchestration-domain-boundaries.md`
- `bcos-evm/docs/adr/007-te-web3-decoder-dependency.md`（Web3 decoder **不在**本 spec 范围）

**v1.1 修订摘要：**

- §1.1：修正 executor 引用计数；说明 TE `precompiled/` 与 orphan 副本关系
- §5.1：合并 `TE/precompiled/` → `adapters/`；删除 orphan `bcos-evm/bcos/PrecompiledManager.cpp`
- §4.5 / §6：闭合 `AuthPort` → `FiscoHostExtension::prepareMessage` wiring；nullable Port 语义
- §7 / §9.3：补全 lambda 测试迁移清单 + `TestPortAdapter` helper
- §8.3 / §10.1：CI grep 限定源码扩展名（文档 `.md` 允许提及 executor）
- §12：ADR-008 与 PR 同提交

**v1.2 修订摘要（grilling 闭合）：**

- §2.2：**G1=A** — `bcos-executor/src/vm/Precompiled.h` include 修复纳入 A-1 同一 PR
- §5.1：canonical adapter include 路径；**以 TE 活跃 TU 为准**合并；删除全部 `precompiled/` 空壳
- §5.2：Port 指针 **单一真相源**（Extension 只读 `deps`）
- §4.5 / §10.3：`createAuthTable` 与 `enable_auth_check` 解耦；lookup miss 语义
- §6.3：统一 `FiscoHostExtension` 单构造；删 `PrecompiledManager` forward decl
- §9.3：补全 TE compat 测试 + executor shim 修改清单

---

## 1. 背景与动机

### 1.1 问题

Step 2 §5.1 要求 `bcos-evm` **源码**零 `bcos-executor` include。当前 **`bcos/` 下 4 个源文件**含 executor 引用：

| 文件 | executor 依赖 | 编译归属 |
|------|----------------|----------|
| `PrecompiledImpl.h` | `TransactionExecutive`、`BlockContext`、`ExecutiveWrapper` | header-only；TE 编译 TU 间接 include |
| `AuthCheck.h` | `CallParameters`、`buildLegacyExecutive` | 同上 |
| `Precompiled.h` | `PrecompiledGas`、`PrecompiledResult`、`Utilities` | 同上 |
| `PrecompiledManager.cpp` | 全部 FISCO native precompile 实现头 | **orphan** — 未编入 `bcos-evm-bcos` |

**TE 侧已有活跃实现：** `transaction-executor/bcos-transaction-executor/precompiled/PrecompiledManager.cpp` 与 `PrecompiledEntry.cpp` 已编入 `transaction-executor` 库，include `bcos-evm/bcos/PrecompiledManager.h` 等头文件。迁移需 **合并** TE `precompiled/` 与 `bcos-evm/bcos/` 头文件，而非在 TE 新建第二套副本。

**已有 partial seam：** 策略 A §13.3 的 `FiscoPrecompileCaller` 注入已落地 — `FiscoHostExtension::tryChainPrecompile` 不直接调 executor。但 **adapter implementation header 仍住在 `bcos-evm/bcos/`**，编译边界 grep 未达标，测试无法 mock executor 栈。

> **注：** `bcos-evm/docs/*.md`、`capability-matrix.md` 等文档可提及 `bcos-executor`（描述排除范围）；CI grep **仅扫描源码**（见 §8.3）。

### 1.2 与 PrecompileRouter 的关系

PrecompileRouter Phase 1 统一 **kernel** dispatch（builtin + envelope）。本 spec 解决 **chain precompile 执行** 与 **auth** 对 `bcos-executor` 的编译耦合，二者正交、可独立合并。

---

## 2. 目标与非目标

### 2.1 目标

1. `bcos-evm` **源码**（`*.cpp` / `*.h` / `*.hpp` / `CMakeLists.txt`）**零** `#include "bcos-executor/..."`（Step 2 §5.1 + Step 4 §7.3）。
2. 定义两个 **Port interface**（无 executor 类型）：
   - `ChainPrecompilePort` — 帧级 FISCO precompile dispatch
   - `AuthPort` — tx 级 auth 检查 + CREATE auth 表创建
3. **Prod adapter** 在 `transaction-executor/adapters/`，封装 `PrecompiledManager`、`PrecompiledImpl`、`AuthCheck`、`ExecutiveWrapper`。
4. `ExecuteViaHostInput` 以 **`const*` Port 指针**注入（B2a）；删除 auth / precompile / createAuthTable 三类 `std::function` typedef（**保留** `recipientPathResolver`）。
5. **Test adapter**（in-memory stub + 薄 wrapper）供 `bcos-evm` 单测，不链 executor。
6. 行为不退化：`CompatExecuteViaHost`、auth hook 测试、PrecompileRouter 套件全绿。

### 2.2 非目标

- **ADR-007 Web3 decoder**（`Web3AccessListResolver`）迁移 — 仍留 TE，独立任务 A-3。
- 修改 `PrecompileRouter` kernel precedence / envelope。
- 修改 `bcos-executor` **DAG 拓扑** / 旧 `HostContext` 路径（**不含** §9.3 列出的最小 include 路径修复 — **G1=A**）。
- Phase 2 spine 合并（B-1）或 C7 产品对齐（B-2）。
- 将 builtin 0x01–0x11 dispatch 迁入 Port（仍由 `eth/` PrecompileRouter + `EthPrecompiles` 处理）。
- 在 `bcos-evm/bcos/` 保留 deprecated re-export shim（与 grep 目标冲突；**不采用 G1=C**）。

---

## 3. 架构决策（Grilling 记录）

| 决策 | 选项 | 结论 | 理由 |
|------|------|------|------|
| Port 拆分 | 合并 / 拆分 | **拆分** AuthPort + ChainPrecompilePort | ADR-005：auth=orchestrator tx 级；precompile=HostExtension 帧级 |
| Manager 位置 | B1 留 bcos-evm / B2 迁 TE | **B2** | bcos-evm 零 FISCO precompile 类型；lookup + feature gate 封入 adapter |
| 注入方式 | B2a 显式指针 / B2b lambda facade | **B2a** | call site 显式依赖 Port；deletion test 可删 typedef |
| 迁移策略 | P1 big bang / P2 两阶段 | **P1** | 与最彻底目标形态一致；避免中间态双轨 |
| executor include 修复 | A 纳入 PR / B shim 留 bcos-evm / C 推迟 | **A** | `bcos-executor/src/vm/Precompiled.h` 改指向 `adapters/`；mv 后全树可编译 |

---

## 4. Port Interface

### 4.1 位置

```
bcos-evm/bcos/ports/ChainPrecompilePort.h
bcos-evm/bcos/ports/AuthPort.h
```

链接目标：`bcos-evm-bcos`（仅 header + vtable；无 executor 依赖）。

**Port header 自包含要求：**

- `AuthPort.h` — `#include "bcos-evm/eth/EVMCResult.h"`、`<optional>`、`<string_view>`
- `ChainPrecompilePort.h` — `#include <evmc/evmc.h>`、`<optional>`

### 4.2 ChainPrecompilePort

```cpp
namespace bcos::evm {

struct ChainPrecompilePort {
    virtual ~ChainPrecompilePort() = default;
    /// 输入 msg 已由 FiscoHostExtension 完成 [PRECOMPILED] 路由与 target resolve。
    /// 返回 nullopt 表示 lookup miss（caller 继续 EVM）。
    virtual std::optional<evmc_result> dispatch(
        evmc_revision rev, evmc_message const& msg) = 0;
};

}  // namespace bcos::evm
```

**不在 interface 内：** 地址表、`ledger::Features`、revision feature gate — 均由 TE adapter implementation 持有。

### 4.3 AuthPort

```cpp
namespace bcos::evm {

struct AuthPort {
    virtual ~AuthPort() = default;
    /// tx 级 auth；nullopt = 通过；有值 = 短路返回 EVMCResult。
    virtual std::optional<EVMCResult> checkAuth(evmc_message const& msg) = 0;
    /// CREATE 帧 auth 表创建（原 createAuthTableInvoker）。
    virtual void createAuthTable(evmc_message const& msg, std::string_view tablePath) = 0;
};

}  // namespace bcos::evm
```

### 4.4 留在 FiscoHostExtension 的 routing（无 executor 类型）

| 方法 | 职责 |
|------|------|
| `isFiscoPrecompileAddress` | `0x1000+` 地址族判断 |
| `parseDynamicPrecompileTarget` | `[PRECOMPILED]` code → resolved address |
| `tryChainPrecompile` | 路由后调用 `ChainPrecompilePort::dispatch` |

builtin 0x01–0x11 仍走 `PrecompileRouter` → `EthPrecompiles`，不经 Port。

### 4.5 Nullable Port 语义

| 指针 / 条件 | 语义 |
|-------------|------|
| `chainPrecompilePort == nullptr` | 等价于当前空 `FiscoPrecompileCaller` — `tryChainPrecompile` **入口即**返回 `nullopt`（**在** `isFiscoPrecompileAddress` / `[PRECOMPILED]` routing **之前**，与现网一致） |
| `chainPrecompilePort != nullptr` 且 lookup miss | Port 返回 `nullopt` → caller **继续 EVM**（非 REVERT） |
| `authPort == nullptr` | 跳过 tx 级 `checkAuth` **与** `createAuthTable`（即使 `enable_auth_check == true` 亦无效 — TE prod 路径始终注入非 null） |
| `enable_auth_check && authPort != nullptr` | 执行 tx 级 `checkAuth` |
| `authPort != nullptr` 且 `enable_auth_check == false` | **仍**可在 CREATE `prepareMessage` 调 `createAuthTable`（与现网 TE 一致 — **createAuthTable 不绑定 enable_auth_check**） |

---

## 5. Prod Adapter（transaction-executor）

### 5.1 位置与合并策略

**目标目录：**

```
transaction-executor/bcos-transaction-executor/adapters/
  ExecutorPrecompileAdapter.h/.cpp
  ExecutorAuthAdapter.h/.cpp
  PrecompiledManager.h/.cpp      ← 自 bcos-evm/bcos/ 迁入
  PrecompiledImpl.h
  Precompiled.h
  PrecompiledEntry.h/.cpp        ← 合并自 TE/precompiled/PrecompiledEntry.cpp
  AuthCheck.h
```

**Canonical include 路径（迁后唯一合法路径，无 `bcos-evm/bcos/` re-export shim）：**

```
transaction-executor/bcos-transaction-executor/adapters/Precompiled.h
transaction-executor/bcos-transaction-executor/adapters/PrecompiledImpl.h
transaction-executor/bcos-transaction-executor/adapters/PrecompiledManager.h
transaction-executor/bcos-transaction-executor/adapters/PrecompiledEntry.h
transaction-executor/bcos-transaction-executor/adapters/AuthCheck.h
```

**合并步骤（P1 单 PR）：**

1. **git mv** `bcos-evm/bcos/` 下 executor 耦合 **头文件** → `adapters/`。
2. **git mv** `transaction-executor/.../precompiled/PrecompiledManager.cpp` 与 `PrecompiledEntry.cpp` → `adapters/`（**以 TE 活跃 TU 为准** — 使用 `PrecompiledContract` 无 `executor::` 前缀；**勿**与 orphan `bcos-evm/bcos/PrecompiledManager.cpp` 做内容 merge）。
3. **删除** orphan `bcos-evm/bcos/PrecompiledManager.cpp`（未编入任何 CMake target）。
4. **删除** `transaction-executor/.../precompiled/` 下全部空壳 re-export：
   - `PrecompiledManager.h`
   - `PrecompiledImpl.h`
   - `AuthCheck.h`
   - `ExecutiveWrapper.h`（`#include` TE 根目录 `ExecutiveWrapper.h` 的 shim，无 call site）
5. 更新 `transaction-executor/CMakeLists.txt`：`precompiled/` 源文件路径 → `adapters/`；确保 adapter 头目录对 `bcos-executor` / TE tests **PUBLIC** include。

`ExecutiveWrapper.h` 留在 `transaction-executor/bcos-transaction-executor/` 根目录；adapter 直接 include。

**跨 repo include 修复（G1=A）：**

```cpp
// bcos-executor/src/vm/Precompiled.h — 迁后
#include "transaction-executor/bcos-transaction-executor/adapters/Precompiled.h"
```

仅此 **一行路径变更**；不修改 executor DAG 拓扑或 `HostContext` 行为。

### 5.2 Session-scoped adapter（B2a lifetime）

每次 `TransactionExecutorImpl` 执行 tx 时，在 **栈上**构造 adapter，生命周期与 `executeViaHost` 调用一致：

```cpp
ExecutorAuthAdapter authAdapter{sessionCtx};
ExecutorPrecompileAdapter precompileAdapter{sessionCtx, precompiledManager};

ExecuteViaHostInput input;
input.authPort = &authAdapter;
input.chainPrecompilePort = &precompileAdapter;

FiscoHostExtension::FiscoHostExtensionDeps deps;
deps.authPort = input.authPort;
deps.chainPrecompilePort = input.chainPrecompilePort;
// ... 其余 deps 字段 ...
FiscoHostExtension extension(input.revisionConfig.enable_balance_transfer, std::move(deps));

co_return co_await executeViaHost(std::move(input));
```

**Port 指针单一真相源：** `FiscoHostExtension` **只读** `FiscoHostExtensionDeps` 内的 `authPort` / `chainPrecompilePort`；不在 extension 内再读 `ExecuteViaHostInput` 字段。TE call site 在构造 `extension` 前将 `input.*Port` copy 进 `deps`（见上例）；`executeViaHost` 内同样 copy 一次后 extension 生命周期内 deps 不变。

**Session context**（adapter 构造参数，定义在 TE，不暴露 executor 类型到 Port header）：

- `RollbackableStorage&` / storage ref
- `protocol::BlockHeader const&`
- `evmc_address origin`
- `int64_t contextID`, `seq`
- `FiscoRevisionConfig const&`
- `ledger::Features const&`
- `crypto::Hash const&`
- `PrecompiledManager&`（仅 PrecompileAdapter）

### 5.3 ExecutorPrecompileAdapter 行为

等价于当前 TE lambda（`TransactionExecutorImpl.h:306–327`）：

1. `PrecompiledManager::getPrecompiled(recipient, revisionConfig, features)`
2. miss → `nullopt`
3. hit → `callPrecompiled(...)` → `evmc_result`

### 5.4 ExecutorAuthAdapter 行为

等价于当前：

- `checkAuth` → 原 `AuthCheck.h::checkAuth`
- `createAuthTable` → 原 `AuthCheck.h::createAuthTable`（内部 `task::syncWait` 保持不变）

---

## 6. ExecuteViaHost / FiscoHostExtension 变更

### 6.1 ExecuteViaHostInput

| 删除 | 新增 |
|------|------|
| `std::function<...> authChecker` | `AuthPort const* authPort{nullptr}` |
| `FiscoPrecompileCaller precompileCaller` | `ChainPrecompilePort const* chainPrecompilePort{nullptr}` |
| `CreateAuthTableInvoker createAuthTableInvoker` | （并入 AuthPort，经 deps 传入 extension） |

保留：`recipientPathResolver`（纯路径策略，无 executor）。

### 6.2 ExecuteViaHost.cpp

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
// ... populate deps ...
FiscoHostExtension extension(input.revisionConfig.enable_balance_transfer, std::move(deps));
```

### 6.3 FiscoHostExtension

| 删除 | 变更 |
|------|------|
| `FiscoPrecompileCaller` typedef | `FiscoHostExtensionDeps::chainPrecompilePort` |
| `CreateAuthTableInvoker` typedef | `FiscoHostExtensionDeps::authPort` |
| `FiscoHostExtensionDeps::precompiledManager` | 删除（dead field） |
| `FiscoHostExtensionDeps::externalCaller` | 删除（dead field，cpp 内未使用） |
| `class PrecompiledManager;` forward decl | 删除 |
| 双构造 `(skip, caller)` / `(skip, deps, caller)` | **统一**为 `FiscoHostExtension(bool skip, FiscoHostExtensionDeps deps)` |
| `#include` executor 相关 | 零 |

**tryChainPrecompile：**

```cpp
if (m_chainPrecompilePort == nullptr) {
    return std::nullopt;
}
// ... routing ...
return m_chainPrecompilePort->dispatch(rev, routedMessage);
```

**prepareMessage（保留现有 guard）：**

```cpp
if (m_blockNumber != 0 && m_authPort != nullptr) {
    m_authPort->createAuthTable(msg, resolveAuthTablePath(msg));
}
```

---

## 7. Test Adapter

### 7.1 位置

```
bcos-evm/test/bcos/adapters/
  InMemoryChainPrecompileAdapter.h    # 固定响应 stub
  InMemoryAuthAdapter.h
  TestPortAdapter.h                   # 薄 wrapper：内部可持 std::function，实现 Port interface
```

`TestPortAdapter` 供现有 lambda 测试低成本迁移：

```cpp
// 示例：替代 input.authChecker = lambda
InMemoryAuthAdapter authPort([](evmc_message const&) -> std::optional<EVMCResult> { ... });
input.authPort = &authPort;
```

### 7.2 用途

| 测试文件 | 变更 |
|----------|------|
| `test/bcos/BcosAuthOrchestratorHookTest.cpp` | `authChecker` lambda → `InMemoryAuthAdapter` / `TestPortAdapter` |
| `test/bcos/FiscoHostExtensionTest.cpp` | 3 处 callback → `InMemoryChainPrecompileAdapter` |
| `test/eth/PrecompileRouterCharacterizationTest.cpp` | C7 callback → `InMemoryChainPrecompileAdapter` |
| 新增 `ChainPrecompilePortTest`（可选） | 验证 `tryChainPrecompile` → Port dispatch wiring |

**不**替代 `CompatExecuteViaHost`（仍走 TE prod adapter E2E）。

---

## 8. CMake / 编译边界

### 8.1 目标依赖

```
bcos-evm-bcos
  SOURCES: ExecuteViaHost.cpp, FiscoHostExtension.cpp, FiscoStateView.cpp  # 不变
  PUBLIC bcos-evm-eth
  # 不得 PUBLIC/PRIVATE link bcos-executor

transaction-executor
  SOURCES: adapters/* (含原 precompiled/*.cpp)
  PUBLIC bcos-evm-bcos bcos-executor ...
```

### 8.2 bcos-evm 测试

链接 `bcos-evm-bcos` 的测试 **不得** 编译 `adapters/PrecompiledManager.cpp` 或 include `PrecompiledImpl.h`；一律通过 Port stub / `TestPortAdapter`。

### 8.3 CI 门禁（A-2，本 PR 一并落地）

**源码限定**（文档 `.md` 排除）：

```bash
! grep -r 'bcos-executor' bcos-evm/ \
    --include='*.cpp' --include='*.h' --include='*.hpp' --include='CMakeLists.txt'
! grep -rE 'bcos/Fisco' bcos-evm/eth/ \
    --include='*.cpp' --include='*.h' --include='*.hpp'
```

写入 `.github/workflows/`（新建 `compile-boundary-gate.yml` 或扩展 `capability-gate.yml`）。

---

## 9. 文件清单

### 9.1 新增

| 路径 | 说明 |
|------|------|
| `bcos-evm/bcos/ports/ChainPrecompilePort.h` | Port interface |
| `bcos-evm/bcos/ports/AuthPort.h` | Port interface |
| `transaction-executor/.../adapters/ExecutorPrecompileAdapter.*` | Prod adapter |
| `transaction-executor/.../adapters/ExecutorAuthAdapter.*` | Prod adapter |
| `bcos-evm/test/bcos/adapters/InMemory*.h` | Test stub |
| `bcos-evm/test/bcos/adapters/TestPortAdapter.h` | Lambda → Port 薄 wrapper |
| `bcos-evm/docs/adr/008-fisco-precompile-port.md` | ADR-008 |
| `.github/workflows/` grep step | A-2 门禁 |

### 9.2 迁移 / 合并 / 删除

| 操作 | 路径 |
|------|------|
| git mv | `bcos-evm/bcos/PrecompiledManager.h` → `adapters/` |
| git mv | `bcos-evm/bcos/PrecompiledImpl.h` → `adapters/` |
| git mv | `bcos-evm/bcos/Precompiled.h` → `adapters/` |
| git mv | `bcos-evm/bcos/PrecompiledEntry.h` → `adapters/` |
| git mv | `bcos-evm/bcos/AuthCheck.h` → `adapters/` |
| git mv | `TE/precompiled/PrecompiledManager.cpp` → `adapters/` |
| git mv | `TE/precompiled/PrecompiledEntry.cpp` → `adapters/` |
| **删除** | `bcos-evm/bcos/PrecompiledManager.cpp`（orphan 副本） |
| **删除** | `TE/precompiled/PrecompiledManager.h`（空壳 re-export） |
| **删除** | `TE/precompiled/PrecompiledImpl.h`（空壳 re-export） |
| **删除** | `TE/precompiled/AuthCheck.h`（空壳 re-export） |
| **删除** | `TE/precompiled/ExecutiveWrapper.h`（空壳 re-export） |
| **删除** | `TE/precompiled/` 目录（迁移后为空） |

### 9.3 修改

| 路径 | 要点 |
|------|------|
| `bcos-evm/bcos/ExecuteViaHost.h/.cpp` | Port 指针；deps 填充 |
| `bcos-evm/bcos/FiscoHostExtension.h/.cpp` | Port deps；删 typedef / dead fields / forward decl；单构造 |
| `transaction-executor/CMakeLists.txt` | `precompiled/` → `adapters/` |
| `transaction-executor/.../TransactionExecutorImpl.h` | 栈 adapter；删 lambda；include → `adapters/` |
| `bcos-executor/src/vm/Precompiled.h` | **G1=A** — include → `adapters/Precompiled.h` |
| `transaction-executor/tests/CompatExecuteViaHostPhaseBTest.cpp` | include → `adapters/PrecompiledImpl.h` |
| `transaction-executor/tests/CompatExecuteViaHostPhaseCTest.cpp` | 同上 |
| `transaction-executor/tests/CompatExecuteViaHostPhaseETest.cpp` | include → `adapters/`（`PrecompiledManager` / `AuthCheck`） |
| `transaction-executor/tests/Modexp7823TeTest.cpp` | include → `adapters/PrecompiledImpl.h` |
| `bcos-evm/test/bcos/BcosAuthOrchestratorHookTest.cpp` | Port adapter |
| `bcos-evm/test/bcos/FiscoHostExtensionTest.cpp` | Port adapter；构造 → `(skip, deps)` |
| `bcos-evm/test/eth/PrecompileRouterCharacterizationTest.cpp` | Port adapter |
| `bcos-evm/test/CMakeLists.txt` | adapter header path |
| `bcos-evm/capability-matrix.md` | architecture note：chain precompile 执行经 Port（Test ref 不变） |
| `docs/.../remaining-architecture-tasks.md` | A-1 → [x]；A-2 → [x] |

---

## 10. 验收

### 10.1 编译边界

```bash
! grep -r 'bcos-executor' bcos-evm/ \
    --include='*.cpp' --include='*.h' --include='*.hpp' --include='CMakeLists.txt'
! grep -rE 'bcos/Fisco' bcos-evm/eth/ \
    --include='*.cpp' --include='*.h' --include='*.hpp'
cmake --build build --target bcos-evm-eth bcos-evm-bcos bcos-evm-op transaction-executor
```

### 10.2 回归

```bash
ctest -R 'CompatExecuteViaHost|ExecuteViaHostCompat'
ctest -R 'BcosAuthOrchestratorHook|FiscoHostExtension|PrecompileRouter|Bcos7702'
ctest --test-dir build/bcos-evm/test --output-on-failure
# transaction-executor 全量 ctest（UNITY_BUILD 下 adapter 头迁入后必跑）
ctest --test-dir build/transaction-executor/test --output-on-failure
```

### 10.3 行为等价

- TE `precompileCaller` lambda 与 `ExecutorPrecompileAdapter::dispatch` 输出一致（同输入 msg/recipient/features）。
- Auth 短路路径与迁移前一致（tx 级 `checkAuth` 在 `executeMessage` 之前）。
- `createAuthTable` 仅在 `blockNumber != 0 && authPort != nullptr` 的 CREATE `prepareMessage` 内调用（保留现有 guard）；**不**受 `enable_auth_check` 门控。
- FISCO 地址空间内 lookup miss → 继续 EVM（非 REVERT）；`0x1000+` 族外地址不经 Port。

---

## 11. 风险与缓解

| 风险 | 缓解 |
|------|------|
| P1 diff 大、review 负担 | 文件用 `git mv`；adapter 首版机械搬运 lambda 体 |
| TE `precompiled/` 与 bcos-evm orphan 双副本 | §5.1 合并清单；删 orphan `.cpp` |
| bcos-evm 测试 lambda 编译失败 | §7 `TestPortAdapter`；§9.3 三文件显式列出 |
| TE compat 测试 include 断裂 | §9.3 四文件 + canonical path §5.1 |
| `bcos-executor` 反向依赖 | **G1=A** — §9.3 `Precompiled.h` shim |
| orphan / TE 双份 `.cpp` 误 merge | §5.1 以 TE TU 为准、删 orphan |
| Port 指针双存储不一致 | §5.2 单一真相源 = `deps` |
| `Precompiled` 析构跨 TU | `PrecompiledEntry.cpp` 与 `Precompiled.h` 同目录 |
| CI grep 文档误报 | §8.3 限定源码扩展名 |
| ADR-007 混淆 | spec 明确 Web3 decoder 不在范围 |
| UNITY_BUILD + 大量头迁入 adapters | §10.2 TE 全量 ctest |

---

## 12. ADR

**ADR-008: FISCO Precompile Port** — 与实现 PR **同提交**（`bcos-evm/docs/adr/008-fisco-precompile-port.md`）。

- **Decision：** FISCO precompile 执行与 auth 通过 `ChainPrecompilePort` / `AuthPort` 注入；implementation 驻 TE `adapters/` + executor；bcos-evm 源码零 include。
- **Consequences：** 源码 grep 门禁可固化；bcos-evm 单测可 mock Port；PrecompileRouter 与 Port 正交。

---

## 13. Spec 自检

| 检查 | 结果 |
|------|------|
| TBD / TODO 占位 | 无 |
| TE `precompiled/` 合并路径 | ✅ §5.1 |
| AuthPort → prepareMessage 闭合 | ✅ §6.3 |
| Nullable Port 语义 | ✅ §4.5 |
| 测试迁移清单完整 | ✅ §7.2 / §9.3（含 TE compat + executor shim） |
| CI grep 范围 | ✅ §8.3 源码限定 |
| executor 反向依赖闭合 | ✅ G1=A §5.1 / §9.3 |
| Port 单一真相源 | ✅ §5.2 |
| createAuthTable / enable_auth_check 解耦 | ✅ §4.5 / §10.3 |
| 与 Step 2 §5.1 / §13.3 一致 | ✅ |
| 与 ADR-005 domain 边界一致 | ✅ |
| 与 PrecompileRouter spec 不冲突 | ✅ |
| 与 ADR-007 范围分离 | ✅ |
| 可单 PR 实施 | ✅ P1 |

---

## 14. 变更记录

| 日期 | 变更 |
|------|------|
| 2026-06-23 | v1.0：brainstorming + grilling（B + B2 + B2a + P1）初版 |
| 2026-06-23 | v1.1：review 修订 — TE 合并策略、AuthPort wiring、nullable 语义、grep 范围、测试清单 |
| 2026-06-23 | v1.2：grilling 闭合 — G1=A executor shim；canonical include；空壳删清单；Port SSoT；行为等价补全；**已批准** |
