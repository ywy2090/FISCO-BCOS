# bcos-evm-ref：最大化复用 evmone 的标准以太坊 EVM（含 OpStack）设计

**日期：** 2026-07-08
**状态：** 已通过 brainstorming 逐节获批，待用户复核
**参考基线：** `/Users/octopus/octo/code/blockchain-impl/evmone`（`lib/evmone` 解释器 + `test/state` 状态转换，均以 STATIC 库导出）
**关联文档：** `bcos-evm/docs/DESIGN-standard-eth-evm-opstack.md`（架构总述 rev.2）

---

## 1. 目标与范围

### 1.1 一句话

新建仓库顶层独立模块 `bcos-evm-ref`，**链接 `evmone::state` 库**复用整套状态转换：Ethereum 主网直接调用 `validate_transaction()` / `transition()` / `finalize()`（零重写）；OpStack 仅自建三块薄层（`op_transition()` 最小 fork、Deposit 路径、L1 data fee），predeploy / vault / fork schedule 全部数据预填。第一版由 EEST / op-geth 测试哈布驱动。

### 1.2 决策汇总（brainstorming 已确认）

| 维度 | 决策 |
|------|------|
| 与现有 `bcos-evm/eth/` 关系 | **并行干净模块**，互不 `#include`；验证成熟后再决定切换 |
| 首个里程碑范围 | **Ethereum + OpStack 一次到位** |
| 驱动/验证方式 | **独立库 + EEST/op-geth 测试哈布**；`StateView` 接内存 fixture，暂不接真账本（保持抽象以便日后桥接） |
| OpStack fee 分账 | **B：最小 fork `op_transition()`**（~80 行，仅改 fee 结算段），与 op-geth fork go-ethereum 一致 |
| 目标 fork | **Cancun+**：ETH = Cancun / Prague / Osaka；OP = Ecotone … Isthmus |
| 模块位置 | 仓库顶层 `bcos-evm-ref/` |
| `evmone::state` 获取 | **① 扩 vcpkg port** 导出 `evmone::state` + `evmone::precompiles` |

### 1.3 非目标

- 不改动现有 `bcos-evm/`（并行验证）。
- 不接共识 / TxPool / 区块调度 / P2P / JSON-RPC。
- 不接 FISCO 私链语义（权限表 / 余额策略）。
- 第一版不接真实账本持久化（仅内存 fixture）。

---

## 2. 复用基线：`evmone::state`

`test/state/CMakeLists.txt` 将状态转换导出为静态库 `evmone::state`，覆盖：`state.cpp`(validate/transition/finalize) · `host.cpp` · `precompiles.cpp` · `system_contracts.cpp`(4788/7002/7251) · `requests.cpp`(7685) · `block.cpp` · `bloom_filter`。

对外接口（`test/state/state_view.hpp`）：

```cpp
class StateView {
  virtual std::optional<Account> get_account(const address&) const noexcept = 0;   // nonce/balance/code_hash/has_storage
  virtual bytes   get_account_code(const address&) const noexcept = 0;
  virtual bytes32 get_storage(const address&, const bytes32&) const noexcept = 0;
};
class BlockHashes { virtual bytes32 get_block_hash(int64_t) const noexcept = 0; };
```

三个入口（`test/state/state.hpp`）：

- `validate_transaction(view, block, tx, rev, blockGasLeft, blobGasLeft) -> variant<TransactionProperties, error_code>`——纯校验，无副作用。
- `transition(view, block, hashes, tx, rev, vm, props) -> TransactionReceipt`——buyGas → EVM → refund，`receipt.state_diff` 为结果。
- `finalize(view, rev, coinbase, reward, ommers, withdrawals) -> StateDiff`。

`TransactionReceipt` 含 `status / gas_used / gas_refund / cumulative_gas_used / logs / logs_bloom_filter / state_diff / post_state(pre-Byzantium)`。

---

## 3. 模块布局与边界

### 3.1 目录结构

```
bcos-evm-ref/
├── CMakeLists.txt              # find_package(evmone) → 链接 evmone::state + evmone::evmone
├── adapter/
│   ├── StateViewAdapter.h/.cpp     # 宿主状态源/内存 → evmone::state::StateView
│   ├── BlockHashesAdapter.h/.cpp   # → evmone::state::BlockHashes
│   └── StateDiffWriteback.h/.cpp   # evmone StateDiff → 宿主写回（v1 仅内存）
├── eth/
│   └── EthTransition.h/.cpp        # 薄封装：直接调 validate_transaction/transition/finalize
├── opstack/
│   ├── OpTransition.h/.cpp         # 最小 fork：照抄 transition()，仅改 fee 分账 → 三 Vault
│   ├── OpDepositTx.h/.cpp          # 0x7E mint+execute 独立路径
│   ├── RollupCost.h/.cpp           # L1 data fee（Ecotone/Fjord/…）
│   ├── OpPredeploys.h/.cpp         # L1Block / GasPriceOracle / Vault 账户预填（数据）
│   └── OpForkSchedule.h            # OP fork → evmc_revision 映射（数据）
└── test/
    ├── eth/     # EEST state/blockchain 对照
    └── opstack/ # op-geth 对照
```

### 3.2 边界纪律（硬规则）

- `adapter/` 与 `eth/` 只依赖 `evmone::state`；`opstack/` 依赖 `adapter/` + `evmone::state`。
- `OpTransition` 是**唯一**有意 fork 的文件（~80 行），其余全部链接库。
- `bcos-evm-ref/` 不依赖现有 `bcos-evm/`，反之亦然。

---

## 4. 组件与接口设计

### 4.1 适配层

```cpp
class StateViewAdapter final : public evmone::state::StateView {
    std::optional<Account> get_account(const address&) const noexcept override;
    bytes   get_account_code(const address&) const noexcept override;
    bytes32 get_storage(const address&, const bytes32&) const noexcept override;
};

class BlockHashesAdapter final : public evmone::state::BlockHashes {
    bytes32 get_block_hash(int64_t) const noexcept override;
};
```

v1 后端为内存 fixture（预填账户/存储/区块哈希）；接口保持抽象，日后可换成宿主账本桥接。`StateDiffWriteback` 负责把 evmone `StateDiff{modified_accounts, deleted_accounts}` 转换为宿主写回格式（v1 仅回写内存以计算 stateRoot）。

### 4.2 Ethereum 路径（薄封装，零 fee 逻辑）

```cpp
namespace bcos::evmref::eth {
Result runTransaction(const StateView&, const BlockInfo&, const BlockHashes&,
                      const Transaction&, evmc_revision, evmc::VM&);   // validate → transition
StateDiff runBlockFinalize(const StateView&, evmc_revision, const address& coinbase,
                           std::optional<uint64_t> reward, ommers, withdrawals);
}
```

`transition()` 内部 buyGas / refund(3529 上限 /5、7623 地板) / tip→coinbase / base fee 销毁全部复用，不自己实现任何 fee 数学。

### 4.3 OpStack 路径

```cpp
namespace bcos::evmref::opstack {
TransactionReceipt op_transition(const StateView&, const BlockInfo&, const BlockHashes&,
                                 const Transaction&, evmc_revision, evmc::VM&,
                                 const TransactionProperties&, const OpFeeParams&);
TransactionReceipt runDeposit(const StateView&, const BlockInfo&, const BlockHashes&,
                              const DepositTx&, evmc_revision, evmc::VM&);
uint256 computeL1Cost(const OpFeeParams&, bytesConstRef rlpTx, evmc_revision);
}
```

关键设计点：

- **`op_transition` fork 边界**：结构照抄 `evmone::state::transition()`，只替换尾部 `sender/coinbase` 结算（base fee 销毁 → BaseFeeVault、`coinbase += tip` → SequencerVault），前置插入 L1 fee 预扣 → L1FeeVault；其余（nonce / buyGas / host.call / refund / 7623）逐行照抄，保证与 evmone 数值等价。
- **Deposit 独立路径**：`DepositTx` 非 `evmone::state::Transaction`，不能进 `transition()`；单独实现 mint（增发 `value`）+ 复用 `Host` 执行 + Regolith 后 gasUsed 规则 + 失败不回滚 mint。
- **Vault/predeploy 是数据**：`OpPredeploys` 把 L1Block(`0x42…15`) / GasPriceOracle(`0x42…0F`) / 三 Vault 账户预填进 `StateViewAdapter`，不改 Host。
- **fork 映射**：`OpForkSchedule` 把 Ecotone…Isthmus 映射到 `evmc_revision`（+ OP 专属参数如 Holocene 可配 base fee 分母）。

---

## 5. 数据流

### 5.1 Ethereum 交易（零 fork）

```
预填 pre-state → runTransaction → validate_transaction
  ├─ 失败 → error_code → 拒绝, 状态不变
  └─ 通过 → transition(host.call 递归执行) → TransactionReceipt{state_diff}
     → computeStateRoot(diff) 对照 EEST 期望
```

### 5.2 OpStack 交易（Deposit / 普通分流）

```
区块开始: OpPredeploys 预填 L1Block/Oracle/Vaults → applyL1Attributes(首笔写 L1Block)
├─ Deposit(0x7E): mint(value) → 复用 Host 执行 → gasUsed(Regolith), 失败不回滚 mint
└─ 普通 tx: computeL1Cost → 预扣 sender → L1FeeVault
            → op_transition(host.call 递归, 与 ETH 同)
            → 尾部 fee: base→BaseFeeVault, tip→SequencerVault
→ 对照 op-geth 期望
```

### 5.3 ETH vs OP 流向差异

| 环节 | Ethereum | OpStack |
|------|----------|---------|
| 入口分流 | 单一 `transition` | Deposit(0x7E) 独立路径，其余走 `op_transition` |
| 预扣 | 仅 buyGas | buyGas **+ L1 data fee** |
| EVM 执行 | evmone `host.call` | **同一** evmone `host.call`（复用） |
| fee 结算 | tip→coinbase、base 销毁 | tip→SequencerVault、base→BaseFeeVault、L1→L1FeeVault |
| 区块前置 | 无 | predeploy 预填 + L1 属性写入 |

---

## 6. 测试策略

| 层 | 测试内容 | 判据 |
|----|---------|------|
| 适配器单测 | `StateViewAdapter` 三方法读命中/未命中 | 边界正确 |
| ETH state | EEST state fixtures（Cancun/Prague/Osaka） | `stateRoot` + `logsHash` 逐位对照 |
| ETH blockchain | EEST blockchain fixtures：rejected tx、receipt trie、withdrawals、requests(7685) | 区块 stateRoot / receiptRoot |
| OP Deposit | op-geth 向量：mint、失败不回滚、Regolith gasUsed | state_diff + gasUsed |
| OP L1 fee | Ecotone/Fjord RollupCost 各版本、L1FeeVault 入账 | Vault 余额 |
| OP fee 分账 | base→BaseFeeVault、tip→SequencerVault | 三 Vault 余额 |
| 退化验证 | 关闭 OP（走 eth 路径）时结果 == evmone | stateRoot 等价 |

驱动：复用 EEST fixture 的 JSON 加载思路，但在 `bcos-evm-ref/test` 内独立实现，不 `#include` 现有 `bcos-evm/test`。

---

## 7. 工作分解与里程碑

| 里程碑 | 内容 | 依赖 | 规模 |
|--------|------|------|------|
| **M0 build 打通** | 扩 vcpkg port 导出 `evmone::state` + `evmone::precompiles`；`bcos-evm-ref` 空壳链接编译通过 | — | 中 |
| **M1 适配器** | `StateViewAdapter`/`BlockHashesAdapter`/`StateDiffWriteback`（内存 fixture）+ 单测 | M0 | 小 |
| **M2 ETH 跑通** | `eth::runTransaction`/`runBlockFinalize` 直调 evmone；EEST state 对照绿 | M1 | 小 |
| **M3 ETH blockchain** | 区块级 finalize/requests/receipt trie；EEST blockchain 对照绿 | M2 | 中 |
| **M4 OP 数据层** | `OpPredeploys` + `OpForkSchedule` + L1 属性写入 | M2 | 小 |
| **M5 OP fee/tx** | `op_transition` 最小 fork、`runDeposit`、`RollupCost`；op-geth 对照绿 | M3, M4 | 中 |
| **M6 退化验证 + 收尾** | OP-off == evmone；文档/CI gate | M5 | 小 |

关键路径：M0 → M1 → M2（build + 适配 + ETH 硬前置）；M4 可与 M3 并行；M5 汇合。

---

## 8. 前置工作：vcpkg port 导出 `evmone::state`

现 `ports/evmone/portfile.cmake` 只装 `evmone::evmone`。需：

1. 打开 evmone `test/state` 子目录构建（`evmone-state` target 默认仅 `EVMONE_TESTING=ON` 时构建）。
2. `install(TARGETS evmone-state ...)` + 导出 `evmone::state` target 与公开头（`state.hpp`/`state_view.hpp`/`transaction.hpp`/`block.hpp`/`state_diff.hpp` 等；注意 `PUBLIC` include 为 `PROJECT_SOURCE_DIR`）。
3. 依赖链：`evmone-state PUBLIC evmone::precompiles evmc::evmc_cpp PRIVATE evmone`——一并导出 `evmone::precompiles`。
4. 锁定 REF（现 `3585c2cb… / v0.21.0`）规避 test 树 API 漂移。

---

## 9. 风险

- **`evmone::state` 属 test 树**：upstream 不保证 API/ABI 稳定；锁 REF 控制，升级集中适配。
- **`op_transition` fork 漂移**：upstream 改 `transition()` 时需同步那 ~80 行。
- **StateDiff 语义**：`deleted_accounts` 在 Cancun 后恒空（SELFDESTRUCT 语义变更）；转换时注意。
- **Predeploy 时序**：L1 属性必须在区块首笔 Deposit 之前写入。
- **共识等价性**：ETH 路径复用 evmone 天然等价；OP 路径需 op-geth 对照，重点在 L1 fee 与 vault 分账数值。

---

## 10. 一句话总结

`bcos-evm-ref` = 链接 `evmone::state` 的独立参考模块：Ethereum 直调 `transition()` 零重写，OpStack 仅 `op_transition` 最小 fork + Deposit + L1 fee 三薄层，predeploy/vault/fork 全数据预填；第一版 EEST/op-geth 测试哈布驱动，关闭 OP 即退化为主网。
