# ETH EIP-1559 费用市场结算 — 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 对齐 geth EIP-1559 execution gas 结算：TE `buyGas/refund/coinbase/burn`、ExecuteViaEth `GASPRICE` normalization、GST adapter 去重；不改 `protocol::effectiveGasPrice()`。

**Architecture:** 新增 header-only `eth/gas/Eip1559.h` 作为唯一公式源；Prepare 阶段在 buyGas 前缓存 caps/blockInfo；`EthTxExecutor` 负责余额 orchestration；`ExecuteViaEth` 仅 normalize gasPrice；adapter 删 duplicate 函数。

**Tech Stack:** C++17、Boost.Test、bcos-evm-eth、transaction-executor、evm-reference-tests、CMake/CTest

**Design spec:** `docs/superpowers/specs/2026-06-21-eth-eip1559-settlement-design.md` (v1.2)

## Global Constraints

- 方案 B：shared `eth/gas/Eip1559.h` + TE orchestration；**不**在 `ExecuteViaEth` 内核做 balance settlement
- **不改** `protocol::effectiveGasPrice()`；eth TE 显式用 `resolveEffectiveGasPrice`
- 1559 识别：**唯一判据** `isEip1559GasCapsTx(web3TypedTxKind, hasExplicitFeeCaps)`；禁止 `gasTipCap != 0` 启发式
- Base fee **销毁**（不 credit coinbase）；coinbase 只收 `finalGasUsed × tipPerGas`
- **`buyGas` 在 `executeViaEthTx` 之前** — caps/blockInfo 必须在 Prepare 或 Execute 开头缓存
- Blob (type-3) **不在范围**；OpStack 去重 **defer**
- 命令前缀使用 `rtk`（仓库 CLAUDE.md 规则）
- 构建目录：`build-bcos-evm-check/`（bcos-evm 单测）、`build-ref/`（EEST reference tests）

---

## 文件职责图

| 文件 | 职责 |
|------|------|
| `bcos-evm/eth/gas/Eip1559.h` | `isEip1559GasCapsTx`、`resolveEffectiveGasPrice`、`tipPerGas`、`normalizeGasCaps`、`maxBalanceGasDebit` |
| `bcos-evm/eth/ExecuteViaEth.h` | `ExecuteViaEthInput::hasExplicitFeeCaps` |
| `bcos-evm/eth/ExecuteViaEth.cpp` | preCheck 后 gasPrice normalization |
| `transaction-executor/.../EthTransactionExecutorImpl.h` | Prepare 缓存 caps/blockInfo；vmerr settle；`m_topLevelIncludedTxVmError` |
| `transaction-executor/.../EthTxInputBuilder.h` | `fillTransactionGasFields`（Prepare 用） |
| `bcos-evm/eth/EthTxExecutor.h` | buyGas/refundGas/penalty/coinbase |
| `bcos-evm/evm-reference-tests/src/ExecuteViaEthAdapter.cpp` | 删 `effectiveGasPriceForSettlement`，用 `Eip1559.h` |
| `bcos-evm/test/eth/EthEip1559GasTest.cpp` | 公式单元测试 |
| `transaction-executor/tests/EthTxExecutor1559Test.cpp` | TE buy/refund/coinbase 集成测试 |
| `bcos-evm/docs/adr/016-eth-eip1559-settlement.md` | ADR |
| `bcos-evm/capability-matrix.md` | 新 capability 行 |

**PR 切分建议：** PR-1 = Task 1–2；PR-2 = Task 3–5；PR-3 = Task 6–8

---

### Task 0: effectiveGasPrice 路径审计

**Files:**
- Create: `bcos-evm/docs/audits/_work/eip1559-effectiveGasPrice-audit.md`（grep 结果清单）

**Interfaces:**
- Produces: eth TE 路径 grep 清单，对照 spec §2.3

- [ ] **Step 1: 运行 grep 审计**

Run:
```bash
rtk grep effectiveGasPrice transaction-executor/bcos-transaction-executor bcos-evm/eth
```

- [ ] **Step 2: 写入审计清单**

记录每个命中：文件、行号、是否 eth TE 路径、本 PR 动作（改/不变）。

预期 eth TE 必改项：
- `EthTransactionExecutorImpl.h:143` Prepare warm `tx.gasPrice = protocol::effectiveGasPrice(...)` → 删除
- `EthTransactionExecutorImpl.h:224` executeViaEthTx → 改传 legacy gasPrice + caps
- `EthTxExecutor.h:31,87` buyGas/refundGas → 改 `Eip1559.h`

- [ ] **Step 3: Commit**

```bash
rtk git add bcos-evm/docs/audits/_work/eip1559-effectiveGasPrice-audit.md
rtk git commit -m "$(cat <<'EOF'
docs(eth): audit effectiveGasPrice call sites for EIP-1559 PR

EOF
)"
```

---

### Task 1: `Eip1559.h` + 公式单元测试

**Files:**
- Create: `bcos-evm/eth/gas/Eip1559.h`
- Create: `bcos-evm/test/eth/EthEip1559GasTest.cpp`
- Modify: `bcos-evm/test/CMakeLists.txt`（在 `EthIncludedTxVmerrTest` 块后追加）

**Interfaces:**
- Produces:
  ```cpp
  namespace bcos::evm::gas {
  bool isEip1559GasCapsTx(uint8_t web3TypedTxKind, bool hasExplicitFeeCapsFromTx) noexcept;
  bcos::u256 resolveEffectiveGasPrice(bcos::u256 const&, bcos::u256 const&, bcos::u256 const&) noexcept;
  bcos::u256 tipPerGas(bcos::u256 const& effectiveGasPrice, bcos::u256 const& baseFee) noexcept;
  struct GasCaps { bcos::u256 gasTipCap; bcos::u256 gasFeeCap; bool isEip1559Caps; };
  GasCaps normalizeGasCaps(bcos::u256 gasPrice, bcos::u256 gasTipCap, bcos::u256 gasFeeCap,
      uint8_t web3TypedTxKind, bool hasExplicitFeeCapsFromTx) noexcept;
  bcos::u256 maxBalanceGasDebit(int64_t gasLimit, GasCaps const& caps) noexcept;
  }
  ```

- [ ] **Step 1: 写失败测试**

Create `bcos-evm/test/eth/EthEip1559GasTest.cpp`:

```cpp
#define BOOST_TEST_MODULE EthEip1559GasTest
#include "bcos-evm/eth/gas/Eip1559.h"
#include <boost/test/included/unit_test.hpp>

namespace bcos::evm::test
{
using bcos::evm::gas::isEip1559GasCapsTx;
using bcos::evm::gas::maxBalanceGasDebit;
using bcos::evm::gas::normalizeGasCaps;
using bcos::evm::gas::resolveEffectiveGasPrice;
using bcos::evm::gas::tipPerGas;

BOOST_AUTO_TEST_SUITE(EthEip1559GasTest)

BOOST_AUTO_TEST_CASE(tip_plus_base_below_fee_cap)
{
    auto const eff = resolveEffectiveGasPrice(2, 100, 10);
    BOOST_CHECK_EQUAL(eff, 12);
}

BOOST_AUTO_TEST_CASE(tip_plus_base_above_fee_cap)
{
    auto const eff = resolveEffectiveGasPrice(50, 40, 10);
    BOOST_CHECK_EQUAL(eff, 40);
}

BOOST_AUTO_TEST_CASE(legacy_type0_not_1559)
{
    BOOST_CHECK(!isEip1559GasCapsTx(0, false));
    auto const caps = normalizeGasCaps(7, 7, 7, 0, false);
    BOOST_CHECK(!caps.isEip1559Caps);
    BOOST_CHECK_EQUAL(caps.gasTipCap, 7);
    BOOST_CHECK_EQUAL(resolveEffectiveGasPrice(caps.gasTipCap, caps.gasFeeCap, 10), 7);
}

BOOST_AUTO_TEST_CASE(type1_not_1559)
{
    BOOST_CHECK(!isEip1559GasCapsTx(0x01, false));
}

BOOST_AUTO_TEST_CASE(type2_zero_priority_fee)
{
    auto const eff = resolveEffectiveGasPrice(0, 100, 10);
    BOOST_CHECK_EQUAL(eff, 10);
}

BOOST_AUTO_TEST_CASE(type4_is_1559)
{
    BOOST_CHECK(isEip1559GasCapsTx(0x04, false));
}

BOOST_AUTO_TEST_CASE(max_balance_debit_1559)
{
    gas::GasCaps caps{.gasTipCap = 2, .gasFeeCap = 100, .isEip1559Caps = true};
    BOOST_CHECK_EQUAL(maxBalanceGasDebit(21'000, caps), u256(21'000) * 100);
}

BOOST_AUTO_TEST_CASE(max_balance_debit_legacy)
{
    gas::GasCaps caps{.gasTipCap = 7, .gasFeeCap = 7, .isEip1559Caps = false};
    BOOST_CHECK_EQUAL(maxBalanceGasDebit(21'000, caps), u256(21'000) * 7);
}

BOOST_AUTO_TEST_CASE(tip_per_gas_clamps_at_zero)
{
    BOOST_CHECK_EQUAL(tipPerGas(10, 10), 0);
    BOOST_CHECK_EQUAL(tipPerGas(15, 10), 5);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::evm::test
```

Append to `bcos-evm/test/CMakeLists.txt` after `EthIncludedTxVmerrTest` block:

```cmake
add_executable(EthEip1559GasTest eth/EthEip1559GasTest.cpp)
target_include_directories(EthEip1559GasTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(EthEip1559GasTest PRIVATE bcos-evm-eth bcos-utilities)
add_test(NAME EthEip1559Gas COMMAND EthEip1559GasTest)
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cd build-bcos-evm-check && cmake --build . --target EthEip1559GasTest -j && ctest -R EthEip1559Gas -V`  
Expected: FAIL（`Eip1559.h` 不存在）

- [ ] **Step 3: 实现 `Eip1559.h`**

Create `bcos-evm/eth/gas/Eip1559.h`:

```cpp
#pragma once
#include <bcos-utilities/Common.h>
#include <algorithm>

namespace bcos::evm::gas
{

inline bool isEip1559GasCapsTx(uint8_t web3TypedTxKind, bool hasExplicitFeeCapsFromTx) noexcept
{
    if (web3TypedTxKind == 0x02 || web3TypedTxKind == 0x04)
        return true;
    return hasExplicitFeeCapsFromTx;
}

inline bcos::u256 resolveEffectiveGasPrice(
    bcos::u256 const& gasTipCap, bcos::u256 const& gasFeeCap, bcos::u256 const& baseFee) noexcept
{
    auto const tipPlusBase = gasTipCap + baseFee;
    return gasFeeCap < tipPlusBase ? gasFeeCap : tipPlusBase;
}

inline bcos::u256 tipPerGas(
    bcos::u256 const& effectiveGasPrice, bcos::u256 const& baseFee) noexcept
{
    return effectiveGasPrice > baseFee ? effectiveGasPrice - baseFee : bcos::u256{0};
}

struct GasCaps
{
    bcos::u256 gasTipCap;
    bcos::u256 gasFeeCap;
    bool isEip1559Caps{false};
};

inline GasCaps normalizeGasCaps(bcos::u256 gasPrice, bcos::u256 gasTipCap, bcos::u256 gasFeeCap,
    uint8_t web3TypedTxKind, bool hasExplicitFeeCapsFromTx) noexcept
{
    GasCaps caps{};
    caps.isEip1559Caps = isEip1559GasCapsTx(web3TypedTxKind, hasExplicitFeeCapsFromTx);
    if (caps.isEip1559Caps)
    {
        caps.gasTipCap = gasTipCap;
        caps.gasFeeCap = gasFeeCap;
    }
    else
    {
        caps.gasTipCap = gasPrice;
        caps.gasFeeCap = gasPrice;
    }
    return caps;
}

inline bcos::u256 maxBalanceGasDebit(int64_t gasLimit, GasCaps const& caps) noexcept
{
    auto const price = caps.isEip1559Caps ? caps.gasFeeCap : caps.gasTipCap;
    return bcos::u256(gasLimit) * price;
}

}  // namespace bcos::evm::gas
```

- [ ] **Step 4: 运行测试确认通过**

Run: `cd build-bcos-evm-check && cmake --build . --target EthEip1559GasTest -j && ctest -R EthEip1559Gas -V`  
Expected: PASS（8 cases）

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/eth/gas/Eip1559.h bcos-evm/test/eth/EthEip1559GasTest.cpp bcos-evm/test/CMakeLists.txt
rtk git commit -m "$(cat <<'EOF'
feat(eth): add shared EIP-1559 gas formula helpers

Header-only Eip1559.h with isEip1559GasCapsTx, resolveEffectiveGasPrice,
tipPerGas, normalizeGasCaps, maxBalanceGasDebit and unit tests.
EOF
)"
```

---

### Task 2: `ExecuteViaEth` GASPRICE normalization

**Files:**
- Modify: `bcos-evm/eth/ExecuteViaEth.h:35-42`
- Modify: `bcos-evm/eth/ExecuteViaEth.cpp`（preCheck 之后、intrinsic gas 之前）
- Create: `bcos-evm/test/eth/EthExecuteViaEth1559GasPriceTest.cpp`
- Modify: `bcos-evm/test/CMakeLists.txt`

**Interfaces:**
- Consumes: `gas::normalizeGasCaps`, `gas::resolveEffectiveGasPrice`, `gas::isEip1559GasCapsTx`
- Produces: `ExecuteViaEthInput::hasExplicitFeeCaps`；1559 tx 执行时 `input.gasPrice == effective`

- [ ] **Step 1: 写失败测试**

Create `bcos-evm/test/eth/EthExecuteViaEth1559GasPriceTest.cpp` — 合约 bytecode 执行 `GASPRICE` + `BASEFEE` 并 return 两者之和的 low 32 bytes（或用已有 fixture 模式）。最小 harness：

```cpp
#define BOOST_TEST_MODULE EthExecuteViaEth1559GasPriceTest
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-evm/eth/ExecuteViaEth.h"
#include "state/InMemoryStateView.h"
#include <bcos-task/Wait.h>
#include <evmone/evmone.h>
#include <boost/test/included/unit_test.hpp>

// Bytecode: PUSH1 0x3a GASPRICE PUSH1 0x3a BASEFEE ADD MSTORE PUSH1 32 PUSH1 0 MSTORE RETURN
// Returns 32-byte word: effective + baseFee (for assertion: effective = min(tip+base, feeCap))
static bytes const kGasPriceBaseFeeSumReturn = fromHex(
    "603a3a0160206000536020600053f3");

namespace bcos::evm::test
{
BOOST_AUTO_TEST_CASE(executeViaEth_normalizes_type2_gasprice_for_opcode)
{
    crypto::Keccak256 hashImpl;
    evmc::VM vm{evmc_create_evmone()};

    state::test::InMemoryStateView view;
    evmc_address sender{};
    sender.bytes[19] = 0x01;
    evmc_address target{};
    target.bytes[19] = 0x02;

    state::Account senderAccount;
    senderAccount.balance = 1'000'000'000'000'000;
    view.insertAccount(sender, senderAccount);

    state::Account contract;
    contract.code = kGasPriceBaseFeeSumReturn;
    view.insertAccount(target, contract);

    ExecuteViaEthInput input;
    input.stateView = &view;
    input.vm = &vm;
    input.hashImpl = &hashImpl;
    input.blockInfo.baseFee = 10;
    input.gasTipCap = 2;
    input.gasFeeCap = 100;
    input.hasExplicitFeeCaps = true;
    input.web3TypedTxKind = 0x02;
    input.gasPrice = 100;  // wrong pre-normalization maxFee
    input.revisionConfig.revision = EVMC_CANCUN;

    evmc_message msg{};
    msg.kind = EVMC_CALL;
    msg.depth = 0;
    msg.gas = 200'000;
    msg.sender = sender;
    msg.recipient = target;
    msg.code_address = target;
    msg.input_data = nullptr;
    msg.input_size = 0;
    input.message = msg;

    auto output = task::syncWait(executeViaEth(std::move(input)));
    BOOST_REQUIRE_EQUAL(output.evmcResult.status_code, EVMC_SUCCESS);
    BOOST_REQUIRE_GE(output.evmcResult.output_size, 32u);
    // effective=12, baseFee=10 → sum=22
    auto const word = intx::be::load<intx::uint256>(
        *reinterpret_cast<evmc_bytes32 const*>(output.evmcResult.output_data));
    BOOST_CHECK_EQUAL(word, intx::uint256{22});
}
}  // namespace bcos::evm::test
```

（若 `fromHex` 不可用，改用 inline bytes 数组。）

- [ ] **Step 2: 运行测试确认失败**

Run: `cd build-bcos-evm-check && cmake --build . --target EthExecuteViaEth1559GasPriceTest -j && ctest -R EthExecuteViaEth1559GasPrice -V`  
Expected: FAIL（normalization 未实现或 gasPrice 仍为 100 → sum=110）

- [ ] **Step 3: 修改 `ExecuteViaEth.h`**

在 `ExecuteViaEthInput` 增加：
```cpp
bool hasExplicitFeeCaps{false};
```

- [ ] **Step 4: 修改 `ExecuteViaEth.cpp`**

在 `#include` 区增加 `#include "bcos-evm/eth/gas/Eip1559.h"`。

在 `ethExecuteViaEthPreCheck` 成功返回后、intrinsic gas 计算前插入：

```cpp
{
    auto const caps = gas::normalizeGasCaps(input.gasPrice, input.gasTipCap, input.gasFeeCap,
        input.web3TypedTxKind, input.hasExplicitFeeCaps);
    if (caps.isEip1559Caps)
    {
        input.gasPrice = gas::resolveEffectiveGasPrice(
            caps.gasTipCap, caps.gasFeeCap, input.blockInfo.baseFee);
    }
}
```

- [ ] **Step 5: 运行测试确认通过**

Run: `cd build-bcos-evm-check && cmake --build . --target EthExecuteViaEth1559GasPriceTest -j && ctest -R EthExecuteViaEth1559GasPrice -V`  
Expected: PASS

- [ ] **Step 6: 回归**

Run: `cd build-bcos-evm-check && ctest -R 'EthIncludedTxVmerr|EthExecuteViaEthPreCheck' -V`  
Expected: PASS

- [ ] **Step 7: Commit**

```bash
rtk git add bcos-evm/eth/ExecuteViaEth.h bcos-evm/eth/ExecuteViaEth.cpp \
  bcos-evm/test/eth/EthExecuteViaEth1559GasPriceTest.cpp bcos-evm/test/CMakeLists.txt
rtk git commit -m "$(cat <<'EOF'
feat(eth): normalize GASPRICE for EIP-1559 in ExecuteViaEth

Apply resolveEffectiveGasPrice after preCheck for type-2/4 txs so
GASPRICE opcode matches geth; includes eth_call path field support.
EOF
)"
```

---

### Task 3: Prepare 阶段缓存 caps + blockInfo

**Files:**
- Modify: `transaction-executor/bcos-transaction-executor/EthTxInputBuilder.h`
- Modify: `transaction-executor/bcos-transaction-executor/EthTransactionExecutorImpl.h:66-153,209-228`

**Interfaces:**
- Consumes: `eth_tx::buildEthBlockInfo`, `executor::resolveWeb3AccessList`
- Produces:
  ```cpp
  // Data 新字段
  bcos::u256 m_gasTipCap{0}, m_gasFeeCap{0}, m_gasPriceLegacy{0};
  bool m_hasExplicitFeeCaps{false};
  uint8_t m_web3TypedTxKind{0};
  bcos::u256 m_effectiveGasPrice{0};
  state::BlockInfo m_blockInfo{};
  bool m_topLevelIncludedTxVmError{false};

  // EthTxInputBuilder
  void fillTransactionGasFields(protocol::Transaction const& tx, Data& data);
  ```

- [ ] **Step 1: 在 `EthTxInputBuilder.h` 增加 helper**

```cpp
inline void fillTransactionGasFields(protocol::Transaction const& tx, auto& data)
{
    data.m_blockInfo = buildEthBlockInfo(data.m_blockHeader.get(), data.m_ledgerConfig.get());

    data.m_gasTipCap = 0;
    data.m_gasFeeCap = 0;
    data.m_hasExplicitFeeCaps = false;
    if (auto tip = tx.maxPriorityFeePerGas(); !tip.empty())
        data.m_gasTipCap = u256(tip);
    if (auto fee = tx.maxFeePerGas(); !fee.empty())
    {
        data.m_gasFeeCap = u256(fee);
        data.m_hasExplicitFeeCaps = true;
    }
    data.m_gasPriceLegacy = u256(tx.gasPrice());

    auto const resolved = executor::resolveWeb3AccessList(tx);
    data.m_web3TypedTxKind = resolved.web3TypedTxKind;
    if (data.m_web3TypedTxKind == 0 && !tx.extraTransactionBytes().empty())
        data.m_web3TypedTxKind = static_cast<uint8_t>(tx.extraTransactionBytes()[0]);
}
```

- [ ] **Step 2: 修改 `EthTransactionExecutorImpl.h` Data 结构**

在 `Data` 中增加 §5.2.1 所列字段（见 Interfaces）。

- [ ] **Step 3: Prepare 阶段调用 fillTransactionGasFields**

在 Prepare 分支开头（warm 之前）：

```cpp
eth_tx::fillTransactionGasFields(m_data->m_transaction.get(), *m_data);
```

删除 warm 用的错误行：
```cpp
tx.gasPrice = protocol::effectiveGasPrice(m_data->m_transaction.get());
```
改为 legacy gasPrice（warm 不需要精确 effective，可省略或设 0）：
```cpp
tx.gasPrice = m_data->m_gasPriceLegacy;
```

- [ ] **Step 4: Execute 阶段 buyGas 前二次保障**

在 Execute 分支 `if (!m_data->m_call)` 之前（防御性，若 Prepare 跳过）：

```cpp
if (m_data->m_blockInfo.number == 0 && m_data->m_blockInfo.baseFee == 0)
    eth_tx::fillTransactionGasFields(m_data->m_transaction.get(), *m_data);
```

（或用 `bool m_gasFieldsFilled` 标志更干净。）

- [ ] **Step 5: 修改 executeViaEthTx**

```cpp
input.gasPrice = m_data->m_gasPriceLegacy;
input.gasTipCap = m_data->m_gasTipCap;
input.gasFeeCap = m_data->m_gasFeeCap;
input.hasExplicitFeeCaps = m_data->m_hasExplicitFeeCaps;
input.blockInfo = m_data->m_blockInfo;
eth_tx::fillWeb3Fields(m_data->m_transaction.get(), input);

auto output = co_await executeViaEth(std::move(input));
m_data->m_topLevelIncludedTxVmError = output.topLevelIncludedTxVmError;
co_return output;
```

删除 `protocol::effectiveGasPrice` 调用；删除 duplicate `buildEthBlockInfo`（用缓存的 `m_blockInfo`）。

- [ ] **Step 6: 编译验证**

Run: `cd build-bcos-evm-check && cmake --build . --target transaction-executor -j 2>&1 | tail -20`  
Expected: 编译通过（buyGas 尚未改，行为暂与旧版相同）

- [ ] **Step 7: Commit**

```bash
rtk git add transaction-executor/bcos-transaction-executor/EthTxInputBuilder.h \
  transaction-executor/bcos-transaction-executor/EthTransactionExecutorImpl.h
rtk git commit -m "$(cat <<'EOF'
refactor(eth-te): cache EIP-1559 caps and blockInfo before buyGas

Prepare phase fills gas fields into ExecuteContext::Data so buyGas can
run before executeViaEthTx; removes protocol::effectiveGasPrice from TE path.
EOF
)"
```

---

### Task 4: `settleGasUsedFromEvmResult` vmerr 分支

**Files:**
- Modify: `transaction-executor/bcos-transaction-executor/EthTransactionExecutorImpl.h:189-207`

**Interfaces:**
- Consumes: `m_topLevelIncludedTxVmError`, `gas::settleIncludedTopLevelTransactionGas`
- Produces: `m_gasUsed = finalGasUsed`（含 ADR-015 vmerr peak）

- [ ] **Step 1: 写失败测试扩展**

在 `transaction-executor/tests/EthTxGasSettlementExecutorTest.cpp` 或新建 case：1559 type-2 tx 触发 included vmerr 后 `m_gasUsed` 等于 `settleIncludedTopLevelTransactionGas` 结果。可先跳过若 harness 过重，依赖 Task 5 集成测试 `included_vmerr_still_routes_tip`。

- [ ] **Step 2: 修改 settleGasUsedFromEvmResult**

```cpp
void settleGasUsedFromEvmResult()
{
    auto& evmcResult = *m_data->m_evmcResult;
    auto const& snapshot = m_data->m_executionContext.gasSettlementSnapshot;
    auto const isWeb3 = m_data->m_transaction.get().type() == protocol::TransactionType::Web3Transaction;
    auto const eip7623 = m_data->m_executionContext.revisionConfig.eip7623;

    if (m_data->m_topLevelIncludedTxVmError && eip7623)
    {
        m_data->m_gasUsed = gas::settleIncludedTopLevelTransactionGas(
            m_data->m_gasLimit, evmcResult.gas_left, snapshot.evmGasRefund,
            m_data->m_executionContext.revisionConfig.calldata_floor_per_token, snapshot.calldata);
        return;
    }

    if (snapshot.gasLimit > 0 && isWeb3 && eip7623)
    {
        auto ctx = snapshot;
        ctx.evmGasLeft = evmcResult.gas_left;
        ctx.evmGasRefund = evmcResult.gas_refund;
        m_data->m_gasUsed = gas::finalizeEthereumGasUsed(
            ctx, m_data->m_executionContext.revisionConfig.calldata_floor_per_token);
        return;
    }

    m_data->m_gasUsed = m_data->m_gasLimit - evmcResult.gas_left;
}
```

- [ ] **Step 3: 回归 EthIncludedTxVmerrTest**

Run: `cd build-bcos-evm-check && ctest -R EthIncludedTxVmerr -V`  
Expected: PASS

- [ ] **Step 4: Commit**

```bash
rtk git add transaction-executor/bcos-transaction-executor/EthTransactionExecutorImpl.h
rtk git commit -m "$(cat <<'EOF'
fix(eth-te): settle finalGasUsed for included top-level vmerr (ADR-015)

Align EthTransactionExecutorImpl with GST adapter for EIP-7623 + vmerr
peak gas before EIP-1559 refund.
EOF
)"
```

---

### Task 5: `EthTxExecutor` buyGas / refundGas / coinbase

**Files:**
- Modify: `bcos-evm/eth/EthTxExecutor.h`
- Create: `transaction-executor/tests/EthTxExecutor1559Test.cpp`
- Modify: `transaction-executor/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Data::m_gasTipCap`, `m_gasFeeCap`, `m_hasExplicitFeeCaps`, `m_web3TypedTxKind`, `m_gasPriceLegacy`, `m_blockInfo`, `m_effectiveGasPrice`, `m_gasUsed`
- Produces: buyGas 预扣 `gasLimit × effective`；refundGas sender 退 remaining×effective + coinbase tip

- [ ] **Step 1: 写失败测试**

Create `transaction-executor/tests/EthTxExecutor1559Test.cpp`（参考 `EthTxGasSettlementExecutorTest.cpp` 的 `makeWeb3Type2Transaction`）：

```cpp
BOOST_AUTO_TEST_CASE(buy_gas_debits_effective_times_limit)
{
    // sender balance 1e18, type-2: tip=1, feeCap=20, baseFee=10 → effective=11
    // gasLimit=100000 → debit 1_100_000
    // assert sender balance after buyGas
}

BOOST_AUTO_TEST_CASE(refund_and_coinbase_tip_after_successful_call)
{
    // Simple contract return; assert coinbase += used * (eff - base)
    // assert sender refund; burn_identity: sender_net - coinbase = used * baseFee
}

BOOST_AUTO_TEST_CASE(insufficient_balance_uses_fee_cap_for_check)
{
    // balance < gasLimit * feeCap → EVMC_INSUFFICIENT_BALANCE, no full debit
}

BOOST_AUTO_TEST_CASE(revert_still_pays_tip_no_state_rollback_of_gas)
{
    // REVERT tx: coinbase tip > 0, gas pre-debit not fully refunded incorrectly
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cd build-bcos-evm-check && cmake --build . --target EthTxExecutor1559Test -j && ctest -R EthTxExecutor1559 -V`  
Expected: FAIL

- [ ] **Step 3: 重写 `EthTxExecutor::buyGas`**

```cpp
#include "bcos-evm/eth/gas/Eip1559.h"

template <class Data>
task::Task<bool> buyGas(Data& data)
{
    if (data.m_call)
        co_return true;
    if (data.m_gasLimit <= 0)
        co_return true;

    auto const caps = gas::normalizeGasCaps(data.m_gasPriceLegacy, data.m_gasTipCap,
        data.m_gasFeeCap, data.m_web3TypedTxKind, data.m_hasExplicitFeeCaps);
    data.m_effectiveGasPrice = gas::resolveEffectiveGasPrice(
        caps.gasTipCap, caps.gasFeeCap, data.m_blockInfo.baseFee);

    if (data.m_effectiveGasPrice == 0)
        co_return true;

    auto const maxGasCost = u256(data.m_gasLimit) * data.m_effectiveGasPrice;
    auto const balanceCheck = gas::maxBalanceGasDebit(data.m_gasLimit, caps)
                            + u256(data.m_transaction.get().value());
    auto const txValue = u256(data.m_transaction.get().value());

    auto& msg = data.m_executionContext.message;
    ledger::account::EVMAccount senderAccount(data.m_rollbackableStorage, msg.sender, false);
    auto senderBalance = co_await senderAccount.balance();

    if (senderBalance < balanceCheck)
    {
        // penalty path — effective not maxFee
        constexpr static int64_t INTRINSIC_GAS = 21000;
        auto const intrinsicCost = u256(INTRINSIC_GAS) * data.m_effectiveGasPrice;
        auto const penalty = std::min(senderBalance, intrinsicCost);
        // ... existing fail result + m_gasUsed = penalty / effective ...
        co_return false;
    }

    co_await senderAccount.setBalance(senderBalance - maxGasCost);
    data.m_afterBuyGasSavepoint = data.m_rollbackableStorage.current();
    data.m_gasPriceStr = "0x" + data.m_effectiveGasPrice.str(256, std::ios_base::hex);
    co_return true;
}
```

- [ ] **Step 4: 重写 `EthTxExecutor::refundGas`**

```cpp
template <class Data>
task::Task<void> refundGas(Data& data)
{
    if (data.m_call)
        co_return;
    if (data.m_effectiveGasPrice == 0)
        co_return;

    auto& evmcResult = *data.m_evmcResult;

    if (evmcResult.status_code != EVMC_SUCCESS && evmcResult.status_code != EVMC_REVERT)
        co_await data.m_rollbackableStorage.rollback(data.m_afterBuyGasSavepoint);

    int64_t const refundGasUnits = std::max<int64_t>(0, data.m_gasLimit - data.m_gasUsed);
    if (refundGasUnits > 0)
    {
        ledger::account::EVMAccount senderAccount(
            data.m_rollbackableStorage, data.m_executionContext.message.sender, false);
        auto balance = co_await senderAccount.balance();
        co_await senderAccount.setBalance(
            balance + u256(refundGasUnits) * data.m_effectiveGasPrice);
    }

    auto const tipCredit = u256(data.m_gasUsed)
        * gas::tipPerGas(data.m_effectiveGasPrice, data.m_blockInfo.baseFee);
    if (tipCredit > 0)
    {
        ledger::account::EVMAccount coinbaseAccount(data.m_rollbackableStorage,
            data.m_blockInfo.coinbase, false);
        auto bal = co_await coinbaseAccount.balance();
        co_await coinbaseAccount.setBalance(bal + tipCredit);
    }
    // base fee burned — no credit
}
```

- [ ] **Step 5: 运行测试确认通过**

Run: `cd build-bcos-evm-check && cmake --build . --target EthTxExecutor1559Test -j && ctest -R EthTxExecutor1559 -V`  
Expected: PASS

- [ ] **Step 6: 回归**

Run: `cd build-bcos-evm-check && ctest -R 'EthTxGasSettlement|EthTransactionExecutorFixture' -V`  
Expected: PASS（或记录 fixture delta 供 review）

- [ ] **Step 7: Commit**

```bash
rtk git add bcos-evm/eth/EthTxExecutor.h transaction-executor/tests/EthTxExecutor1559Test.cpp \
  transaction-executor/tests/CMakeLists.txt
rtk git commit -m "$(cat <<'EOF'
feat(eth-te): EIP-1559 buyGas refundGas coinbase tip and base fee burn

Replace protocol::effectiveGasPrice with shared Eip1559 helpers; route
tip to coinbase; REVERT keeps gas debit; penalty uses effective price.
EOF
)"
```

---

### Task 6: GST adapter 去重

**Files:**
- Modify: `bcos-evm/evm-reference-tests/src/ExecuteViaEthAdapter.cpp:112-121,294-301`

**Interfaces:**
- Consumes: `gas::resolveEffectiveGasPrice`, `gas::isEip1559GasCapsTx`
- Produces: 无 local `effectiveGasPriceForSettlement`；settlement 数值不变

- [ ] **Step 1: 写 adapter 回归测试**

Create `bcos-evm/test/eth/EthAdapter1559FormulaTest.cpp` 或 inline 在现有测试中：对同一组 (tip, feeCap, base, kind) 断言 adapter settlement effective 与 `Eip1559.h` 一致。

- [ ] **Step 2: 修改 ExecuteViaEthAdapter.cpp**

删除 `effectiveGasPriceForSettlement` 函数。

`#include "bcos-evm/eth/gas/Eip1559.h"`

在构建 input 处：
```cpp
input.hasExplicitFeeCaps = tmpl.maxFeePerGas != 0 || tmpl.maxPriorityFeePerGas != 0;
```

在 settlement 处：
```cpp
auto const is1559 = gas::isEip1559GasCapsTx(
    resolveWeb3TypedTxKind(tmpl), input.hasExplicitFeeCaps);
auto const effectiveGasPrice = is1559 ?
    gas::resolveEffectiveGasPrice(input.gasTipCap, input.gasFeeCap, testCase.env.baseFee) :
    tx.gasPrice;
```

- [ ] **Step 3: 运行 smoke**

Run: `cd build-ref && ctest -L evm-reference-tests-smoke -V`  
Expected: 13/13 PASS

- [ ] **Step 4: 回归 precheck / vmerr**

Run: `cd build-bcos-evm-check && ctest -R 'EthExecuteViaEthPreCheck|EthIncludedTxVmerr|EthEip1559Gas' -V`  
Expected: PASS

- [ ] **Step 5: Commit**

```bash
rtk git add bcos-evm/evm-reference-tests/src/ExecuteViaEthAdapter.cpp \
  bcos-evm/test/eth/EthAdapter1559FormulaTest.cpp bcos-evm/test/CMakeLists.txt
rtk git commit -m "$(cat <<'EOF'
refactor(eest): dedupe EIP-1559 settlement formula to Eip1559.h

Remove effectiveGasPriceForSettlement from ExecuteViaEthAdapter; behavior
unchanged; GASPRICE normalization delegated to ExecuteViaEth.
EOF
)"
```

---

### Task 7: 1559 GASPRICE probe manifest + 文档

**Files:**
- Create: `bcos-evm/evm-reference-tests/manifests/eth-eest-1559-gasprice-probe.json`
- Modify: `bcos-evm/evm-reference-tests/README.md`（或等价文档）
- Modify: `bcos-evm/capability-matrix.md`
- Create: `bcos-evm/docs/adr/016-eth-eip1559-settlement.md`

**Interfaces:**
- Produces: probe manifest；ADR-016；matrix 行 **EIP-1559 effective gas + tip settlement (ETH TE)** — `explicit`

- [ ] **Step 1: 创建 probe manifest**

```json
{
  "manifestVersion": 1,
  "entries": [
    {
      "evidenceId": "eth.eest.london.state.eip1559.gasprice",
      "sourceSuite": "eest",
      "casePath": "fixtures/state_tests/london/eip1559/varying_context.json",
      "forkProfileId": "eth-london",
      "path": "Reference",
      "evidenceKind": "ReferenceParity",
      "capabilityRowIds": ["eip1559-settlement"],
      "assertLevels": ["transitional"]
    }
  ]
}
```

（若 fixture 路径不存在，grep `GASPRICE` in `assets/eest` 并替换 `casePath`。）

- [ ] **Step 2: 运行 probe 并记录 baseline**

Run:
```bash
cd build-ref && ctest -R eth-eest-1559-gasprice-probe -V 2>&1 | tee /tmp/1559-probe.log
```
在 README 追加 footnote：probe 日期、pass/fail 数、**允许 0 delta** 说明。

- [ ] **Step 3: 写 ADR-016**

内容覆盖：公式、burn 语义、finalGasUsed × 7623/ADR-015、ADR-005 边界、已知 `protocol::effectiveGasPrice` 债务。

- [ ] **Step 4: 更新 capability-matrix.md**

新增行：
```markdown
| EIP-1559 effective gas + tip settlement (ETH TE) | explicit | EthTxExecutor buyGas/refundGas; Eip1559.h |
```

- [ ] **Step 5: 全量 smoke + 单测回归**

Run:
```bash
cd build-ref && ctest -L evm-reference-tests-smoke -V
cd build-bcos-evm-check && ctest -R 'EthEip1559|EthExecuteViaEth1559|EthTxExecutor1559|EthIncludedTxVmerr|EthExecuteViaEthPreCheck' -V
```
Expected: 全部 PASS

- [ ] **Step 6: Commit**

```bash
rtk git add bcos-evm/evm-reference-tests/manifests/eth-eest-1559-gasprice-probe.json \
  bcos-evm/evm-reference-tests/README.md bcos-evm/capability-matrix.md \
  bcos-evm/docs/adr/016-eth-eip1559-settlement.md
rtk git commit -m "$(cat <<'EOF'
docs(eth): ADR-016 EIP-1559 settlement and 1559 GASPRICE probe manifest

Document burn/tip/finalGasUsed rules; add capability matrix row and EEST
probe baseline (delta may be zero per spec §1.1).
EOF
)"
```

---

## Self-Review Checklist

| Spec § | Task |
|--------|------|
| §4 Eip1559.h API | Task 1 |
| §5.1 ExecuteViaEth normalization + hasExplicitFeeCaps | Task 2 |
| §5.2.1 Prepare 时序 | Task 3 |
| §5.3 buyGas/refund/penalty/REVERT | Task 5 |
| §5.4 adapter 去重 | Task 6 |
| §5.6 finalGasUsed + vmerr | Task 4 |
| §7 测试 | Task 1,2,5,6,7 |
| §8 ADR/matrix | Task 7 |
| §2.3 grep 审计 | Task 0 |
| §2.3 不改 protocol::effectiveGasPrice | 全任务遵守 |
| OpStack defer | 无 task |

**Placeholder scan:** 无 TBD/TODO/similar-to。

**Type consistency:** `m_effectiveGasPrice`、`hasExplicitFeeCaps`、`resolveEffectiveGasPrice(tip,fee,base)` 全 plan 一致。

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-06-21-eth-eip1559-settlement.md`. Two execution options:

**1. Subagent-Driven (recommended)** — 每个 Task 派 fresh subagent，task 间 review，快速迭代

**2. Inline Execution** — 本会话用 executing-plans 批量执行，checkpoint 处 review

**Which approach?**
