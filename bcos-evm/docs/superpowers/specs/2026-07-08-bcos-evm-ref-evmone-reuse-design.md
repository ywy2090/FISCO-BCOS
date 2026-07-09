# bcos-evm-ref：最大化复用 evmone 的标准以太坊 EVM（含 OpStack）设计

**日期：** 2026-07-08（rev.3）· 2026-07-09（rev.4：§7 里程碑重排，决策前置）
**状态：** rev.1 经 brainstorming 逐节获批；rev.2/rev.3 为审查驱动修订（第三轮终审 9/10、判可冻结）；**rev.4 按用户指示重排 §7**：新增 §7.0 目的链条与排序原则、M3.5 StateView 桥接 spike 与决策点，M4/M5 改为依赖决策点。M0–M2 已实施完成（见 §7.1 里程碑表）；**四项方案级决策已经用户确认**：① Host 三处必改点走 OpHost 子类照抄（不改库）② OP 范围 Isthmus 先行 ③ 与现有 bcos-evm 严格隔离 ④ op-geth t8n 差分为 M6 硬 gate
**参考基线（唯一，已确认）：** vcpkg port 锁定的 fork REF `3585c2cb`（= 上游 tag v0.21.0 `35b7e1ee` 后第 3 个提交，仅 SM3 补丁；`test/state` 相对 v0.21.0 tag 零改动）。本文所有 API 描述、行号、照抄源均以此 REF 为准；本地 `blockchain-impl/evmone` HEAD（v0.21.0+75，含 `gas_refund` 字段、EIP-7778——均来自 tag 后未发布提交 `aee9aedd`，将随 v0.22.0 发布）**不作为**设计依据，待 v0.22.0 发布后按 §8.5 清单整体评估升级
**OP 对照基准：** op-geth v1.101702.2（本地 `blockchain-impl/op-geth`），关键行号引用 `core/state_transition.go` / `core/types/rollup_cost.go` / `core/vm/contracts.go` / `core/state_processor.go`
**关联文档：** `bcos-evm/docs/DESIGN-standard-eth-evm-opstack.md`（架构总述 rev.2）

---

## 1. 目标与范围

### 1.1 一句话

新建仓库顶层独立模块 `bcos-evm-ref`，**链接 `evmone::state` 库**复用整套状态转换：Ethereum 主网直接调用 `validate_transaction()` / `transition()` / `finalize()`（零重写）；OpStack 自建**六块薄层**——`op_validate`（OP 校验含 L1/operator fee 余额、拒 blob tx）、`op_transition`（最小 fork，仅改 fee 结算段）、**`OpHost`**（`Host` 子类：修 CHAINID、修 deposit GASPRICE、拦截 precompile 派发）、Deposit 路径、`RollupCost`（L1 data fee + operator fee）、OP precompile override 表（数据）；predeploy / vault 账户预填为数据，fork 语义由 `OpForkSchedule` 映射（返回 `OpForkConfig` 结构体）驱动。**第一版 OP 只实现并验证 Isthmus**（fork 矩阵留扩展位）。第一版由 EEST / op-geth 测试 harness 驱动。

### 1.2 决策汇总（rev.3 修订项标注）

| 维度 | 决策 |
|------|------|
| 与现有 `bcos-evm/eth/` 关系 | **并行干净模块，严格隔离**（rev.3 用户确认）：互不 `#include`、不共享代码——FastLZ 等现有 `bcos-evm/opstack/` 已有实现的算法在本模块**重新实现**，旧实现仅作黑盒对照校验用。注意此"并行"是**替换尝试的风险控制**而非终局（目的链条见 §7.0）；终局身份与切换判据见 §7.2，由 M3 + M3.5 前置回答 |
| 首个里程碑范围 | Ethereum（Cancun/Prague/Osaka*）+ **OpStack Isthmus 先行**（rev.3 用户确认）：只实现/测试 Isthmus（连带其沿用的 Fjord L1 cost 公式与 FastLZ）；Ecotone/Fjord/Granite/Holocene 四个历史 fork 在 `OpForkSchedule`/`OpFork` 枚举中保留扩展位但**不实现不测试**（Fjord 仅其公式被 Isthmus 借用，fork 本身同为留位） |
| 驱动/验证方式 | 独立库 + EEST/op-geth 测试 harness；`StateView` 接内存 fixture，暂不接真账本 |
| OpStack fee 分账 | 最小 fork `op_transition()`：照抄面 = transition 本体 89 行 + 匿名 ns 助手 ~113 行（含 3 个常量）+ **OpHost 派发段 ~50 行 + get_tx_context 覆写**（rev.3 上调）≈ **250 行** |
| **Host 三处必改点** | **rev.3 新增（用户确认）**：CHAINID 硬编码 1（`host.cpp:422`）、deposit GASPRICE 观测错（`host.cpp:408-410` 上游 TODO）、precompile 派发缝不存在（private 非虚 `execute_message`，`host.cpp:366`）——统一以 **`OpHost : evmone::state::Host` 子类**解决，不改库、不打源码补丁；照抄面计入 §9 漂移风险 |
| OP fork 语义载体 | `OpForkSchedule` 是**映射**（`OpFork -> OpForkConfig`），`OpForkConfig` 为结构体（rev.3 统一术语，消除 §1.2/§4.3 成员矛盾——`fork` 是其成员） |
| 模块位置 | 仓库顶层 `bcos-evm-ref/` |
| `evmone::state` 获取 | 扩 vcpkg overlay port：portfile 以 `vcpkg_replace_string` 注入独立 `EVMONE_STATE` option 挂 `test/state` + `test/utils`（**已实施并冒烟通过**，rev.3 按实际机制修正表述）；`evmone::precompiles` 现 port 已导出 |
| 版本基线 | 设计/照抄源/链接库统一 REF `3585c2cb`：`TransactionReceipt` 无 `gas_refund`、`transition()` 无 EIP-7778 段 |
| t8n 差分 | **rev.3 新增（用户确认）**：pinned op-geth `evm t8n` 离线生成 50–100 个含"执行中观测 fee 环境/CHAINID/GASPRICE"的合约向量入库，CI 回放，作为 **M6 硬 gate**（CI 不需要常驻 Go 工具链） |

### 1.3 非目标

- 不改动现有 `bcos-evm/`（并行验证，严格隔离）。
- 不接共识 / TxPool / 区块调度 / P2P / JSON-RPC；不接 FISCO 私链语义。
- 第一版不接真实账本持久化（仅内存 fixture）。
- 区块 RLP 解码路径（EEST blockchain fixture 的 `rlp` 字段）不做，只消费 JSON 展开字段；`blockchain_test_engine` 格式不支持。
- Holocene extraData 内 EIP-1559 参数的块级 base fee 推导属上游/fixture 职责：本设计以 `BlockInfo.base_fee` 为既成输入。
- 主网历史过渡块特例（首个 Ecotone 块回退 Bedrock 公式、首个 Isthmus 块回退 Ecotone attributes 布局）不支持——与 Isthmus 先行的范围决策一致，历史 fork 本身不在第一版。
- **Isthmus withdrawalsRoot**（区块头 withdrawalsRoot 重定义为 L2ToL1MessagePasser `0x4200…0016` 的 storage root，op-geth `block_validator.go:195`）：块头层语义，与 OP 区块级 receiptRoot 同列为 M6 后可选项（rev.3 补）。
- 块内 deposit 必须整体位于非 deposit tx 之前：块结构有效性属 derivation 职责，由 fixture 保证（rev.3 补）。

---

## 2. 复用基线：`evmone::state`

`test/state/CMakeLists.txt:7-11` 定义静态库 `evmone-state` 及**构建树内 alias** `evmone::state`（仅 alias，无 install/export 规则——使其可 `find_package` 是 §8/M0 的工作，已完成）。**REF 上该子目录的唯一挂载路径是 `EVMONE_TESTING → test/ → add_subdirectory(state)`**（rev.3 修正：rev.2 所写"由 `EVMONE_TOOLS` 挂入"是本地 HEAD 的布局，REF 上不成立；port 以自注入 option 绕开，与上游开关无关）。覆盖：`state.cpp`(validate/transition/finalize) · `host.cpp` · `precompiles.cpp` · `system_contracts.cpp`(4788/2935/7002/7251) · `requests.cpp`(7685) · `block.cpp` · `bloom_filter`。

对外接口（`state_view.hpp:17-43`）：

```cpp
class StateView {
  virtual std::optional<Account> get_account(const address&) const noexcept = 0;   // nonce/balance/code_hash/has_storage
  virtual bytes   get_account_code(const address&) const noexcept = 0;
  virtual bytes32 get_storage(const address&, const bytes32&) const noexcept = 0;
};
class BlockHashes { virtual bytes32 get_block_hash(int64_t) const noexcept = 0; };
```

三个入口（`state.hpp:136-152`）：

- `validate_transaction(view, block, tx, rev, int64_t blockGasLeft, int64_t blobGasLeft) -> variant<TransactionProperties, error_code>`——纯校验，无副作用。
- `transition(view, block, hashes, tx, rev, vm, props) -> TransactionReceipt`——buyGas → EVM → refund，`receipt.state_diff` 为结果。
- `finalize(view, rev, coinbase, reward, ommers, withdrawals) -> StateDiff`。

**blob 参数消费模型**（rev.3 补，冒烟测试实证）：REF 的 `BlockInfo`（`block.hpp:34-66`）不含 blob schedule，只有已算好的 `optional<uint256> blob_base_fee`；`BlobParams`（EIP-7840，`blob_params.hpp:17-22`）经自由函数消费——`max_blob_gas_per_block(const BlobParams&)`、`compute_blob_gas_price(const BlobParams&, excess_blob_gas)`。**调用方职责**：用 `BlobParams` 预计算 `blobGasLeft` 与 `BlockInfo.blob_base_fee` 再调 validate/transition（走 `statetest_loader` 时由 loader 代劳，`test/utils/blob_schedule.hpp` 提供 `get_blob_params(rev)`）。

`TransactionReceipt`（REF 版，`transaction.hpp:111-127`）含 `type / status / gas_used / cumulative_gas_used / logs / logs_bloom_filter / state_diff / post_state(pre-Byzantium)`，**无 `gas_refund`**。

`transition()`（`state.cpp:561-649`，89 行）：buyGas 余额预扣、EIP-3529 refund 上限 /5、EIP-7623 calldata 地板价、tip→coinbase。**base fee 销毁是隐式的**：sender 按 effective_gas_price 扣款、coinbase 只收 priority 部分，差额无人入账即销毁——OP 的 "base→Vault" 是把这笔隐式销毁显式入账。

**Host 的三处已知缺陷**（rev.3 新增，对抗性审查发现，均由 §4.3 OpHost 解决）：
1. `get_tx_context` 的 CHAINID 硬编码 `0x01`（`host.cpp:422`）——EEST 恰好用 chain 1 故 ETH 路径测试不受影响，但 **ETH 路径若部署到 chain-id ≠ 1 的网络同样踩坑**，不只是 OP 问题；
2. `get_tx_context` 对 gas price=0 的交易 effective price 计算错误（`host.cpp:408-410`，上游 TODO 自首）——影响 deposit 的 GASPRICE 观测值；
3. precompile 派发在 private 非虚 `execute_message` 内（`host.cpp:366-367`），**不存在"派发前查表"的缝**。

---

## 3. 模块布局与边界

### 3.1 目录结构

```
bcos-evm-ref/
├── CMakeLists.txt              # find_package(evmone) → evmone::state + evmone::evmone + evmone::testutils
├── adapter/
│   ├── StateViewAdapter.h          # 宿主状态源 → StateView（v1 仅头/占位，测试用 evmone TestState）
│   ├── BlockHashesAdapter.h        # 同上（v1 仅头/占位）
│   └── StateDiffWriteback.h/.cpp   # evmone StateDiff → TestState 写回
├── eth/
│   └── EthTransition.h/.cpp        # 薄封装：直接调 validate_transaction/transition/finalize
├── opstack/
│   ├── OpForkSchedule.h            # OpFork -> OpForkConfig 映射（数据）
│   ├── OpValidate.h/.cpp           # evmone validate + OP 追加检查
│   ├── OpTransition.h/.cpp         # 最小 fork：照抄 transition() + 匿名 ns 助手，仅改 fee 段
│   ├── OpHost.h/.cpp               # Host 子类：CHAINID/GASPRICE 修正 + precompile 派发拦截（照抄 ~50 行）
│   ├── OpDepositTx.h/.cpp          # 0x7E 独立路径
│   ├── RollupCost.h/.cpp           # L1 data fee（Fjord 公式 + FastLZ）+ operator fee
│   ├── OpPrecompiles.h/.cpp        # precompile override 表（数据 + OP 版实现）
│   └── OpPredeploys.h/.cpp         # L1Block / GasPriceOracle / 四 Vault 账户预填（数据，属创世状态）
└── test/
    ├── eth/     # EEST state/blockchain 对照（复用 evmone::testutils loader）
    └── opstack/ # op-geth 黄金向量 + t8n 离线向量回放
```

（rev.3：新增 `OpHost`；adapter 两个头标注 v1 占位。）

### 3.2 边界纪律（硬规则，rev.3 修订）

- `adapter/` 与 `eth/` 只依赖 `evmone::state` 公开头（头文件闭包已验证自洽且全部被 port 安装覆盖）。
- `opstack/` 依赖 `adapter/` + `evmone::state` + `lib/evmone/delegation.hpp`（照抄段调用 `get_delegate_address`/`is_code_delegated`；该头实际已随 fork 的 `install(DIRECTORY lib/evmone/)` 安装，portfile 的显式安装冗余无害）+ `evmone_precompiles/secp256k1.hpp`（照抄常量 `SECP256K1N_OVER_2` 依赖，port 已装）。
- **含照抄代码的文件恰好两个**（rev.3 修订原"唯一 fork 文件"）：
  - `OpTransition`：`transition()` 本体 89 行（`state.cpp:561-649`）+ 匿名 ns 助手 `process_authorization_list`（~87 行，EIP-7702）+ `build_message`（20 行）+ 3 个常量（`SECP256K1N_OVER_2`/`AUTHORIZATION_BASE_COST`/`AUTHORIZATION_EMPTY_ACCOUNT_COST`，`state.cpp:20-25`）≈ 202 行。照抄时可删 `state.cpp:6` 的死 include（`../utils/stdx/utility.hpp`，全文件无使用）；
  - `OpHost`：`Host::call`/`execute_message` 中 precompile 派发前语义 ~50 行（depth 检查、journal_create/touch、value 转账、checkpoint/rollback、0x03 quirk，`host.cpp:337-404`）+ `get_tx_context` 覆写。
  - 照抄总面 ≈ **250 行**，全部计入 §9 漂移风险，由 M6 差分测试 + upstream diff 脚本护栏。
- `bcos-evm-ref/` 与现有 `bcos-evm/` **严格隔离**（用户确认）：互不 `#include`、不共享代码。FastLZ、槽解包等在本模块重新实现；现有 `bcos-evm/opstack/` 的同名实现（`fee/RollupCost.cpp` 的 FlzCompressLen、`fee/OpStackFeeParams.h` 等）仅作黑盒对照校验（跑双方单测比数值），不构成代码依赖。此为显式 trade-off：接受一次重复实现，换取干净的验证边界。
- 测试 fixture 基础设施与现有 `bcos-evm/test/eth-eest-test` 共享 `EVM_REF_EEST_ROOT` 环境约定，不共享代码。

---

## 4. 组件与接口设计

**类型约定**（rev.3 补）：本节签名中 `StateView / BlockHashes / BlockInfo / Transaction / TransactionProperties / TransactionReceipt / StateDiff / BlobParams` 均为 `evmone::state::*` 类型；`DepositTx / OpFork* / OpFeeParams / OpTxProperties` 为本模块新类型；字节视图统一用 `evmc::bytes_view`。

### 4.1 适配层

v1 测试后端直接复用 evmone `test/utils/test_state.hpp` 的 `TestState : StateView` + `TestBlockHashes : BlockHashes`；`StateViewAdapter`/`BlockHashesAdapter` 仅保留抽象头供日后真账本桥接（注意：`StateView` 是同步 noexcept 接口且 `get_account_code` 按值返回整段代码，与宿主协程账本的桥接性能是 §7.2 终局判据的 go/no-go 项，由 M3.5 spike 回答）。`StateDiffWriteback` 把 `StateDiff{modified_accounts, deleted_accounts}` 写回 `TestState` 以计算 stateRoot。

**不能假设 Cancun 后 `deleted_accounts` 恒空**——两场景已源码级核实：EIP-6780 同交易自毁（`host.cpp:137` `!acc.just_created` 分支 → `state.cpp:207-212`）、EEST 预置空账户被触碰后 EIP-161 擦除（`state.cpp:289-298` touch → `:214-218`）。写回必须处理删除。

### 4.2 Ethereum 路径（薄封装，零 fee 逻辑）

```cpp
namespace bcos::evmref::eth {
Result runTransaction(const StateView&, const BlockInfo&, const BlockHashes&,
                      const Transaction&, evmc_revision, evmc::VM&,
                      int64_t blockGasLeft, int64_t blobGasLeft);   // validate → transition
StateDiff runBlockFinalize(const StateView&, evmc_revision, const address& coinbase,
                           std::optional<uint64_t> reward, ommers, withdrawals);
}
```

（rev.3 修正：补 `blockGasLeft`/`blobGasLeft` 形参——REF 的 `validate_transaction` 需要它们；`blobGasLeft` 与 `BlockInfo.blob_base_fee` 由**调用方**按 §2 的 blob 参数消费模型用 `BlobParams` 预计算，EEST 路径由 `statetest_loader` 代劳。）

`transition()` 内部 buyGas / refund(3529 上限 /5、7623 地板) / tip→coinbase / base fee 隐式销毁全部复用，不自己实现任何 fee 数学。

### 4.3 OpStack 路径（第一版仅 Isthmus）

```cpp
namespace bcos::evmref::opstack {

enum class OpFork { Ecotone, Fjord, Granite, Holocene, Isthmus };  // v1 仅 Isthmus 实现，其余留位

struct PrecompileOverrides {         // OpPrecompiles.h 提供每 fork 的 override 数据
    // v1（Isthmus）三项，均为 OP 与 L1 Prague 的行为差异：
    //   0x08 bn256Pairing:  输入限长 112687（Granite 起，Isthmus 保留）
    //   0x100 P256VERIFY:   RIP-7212，gas = 3450（op-geth P256VerifyGasFjord）
    //                       —— evmone 的 0x100 注册（EIP-7951/Osaka 门槛、gas 6900）不可用，
    //                          须由本表以 3450 注册；执行体经 §6 对照确认语义一致后可复用
    //                          evmone 的 p256verify 实现（分工：门槛/gas 归本表，算法可复用）
    //   BLS G1MSM/G2MSM/Pairing: OP 专属输入限长 513760/488448/235008
    struct Entry { address addr; int64_t gas_cost_override; size_t max_input_size; /* execute fn */ };
    std::span<const Entry> entries;
    bool contains(const address&) const noexcept;    // 含地址集扩展：0x100 在 evmone Cancun/Prague
};                                                   // 的 is_precompile 里为 false，必须由此表补上

struct OpForkConfig {                // OpForkSchedule.h: OpFork -> OpForkConfig（映射返回结构体）
    OpFork fork;                     // rev.3 补：computeL1Cost 等按此选公式，消除 rev.2 的成员矛盾
    evmc_revision rev;               // Isthmus -> EVMC_PRAGUE（op-geth 强制 PragueTime==IsthmusTime）
    const PrecompileOverrides* precompiles;
    bool disable_prague_requests;    // Isthmus: true（6110/7002/7251 禁用；4788/2935 保留）
    bool has_operator_fee;           // Isthmus: true
};

struct OpFeeParams {                 // 在本块 L1 attributes deposit 执行之后，从 L1Block 存储槽读出（共识关键）
    uint256 l1_base_fee;             // slot 1（整槽）
    uint32_t base_fee_scalar;        // slot 3 字节 [16,20)   ┐ 打包槽，布局以 op-geth
    uint32_t blob_base_fee_scalar;   // slot 3 字节 [20,24)   ┘ rollup_cost.go:70-84 为准
    uint256 blob_base_fee;           // slot 7（整槽）
    uint32_t operator_fee_scalar;    // slot 8 字节 [20,24)   ┐ rollup_cost.go:649-660
    uint64_t operator_fee_constant;  // slot 8 字节 [24,32)   ┘
};

struct DepositTx {                   // rev.3 补类型草图（0x7E，非 evmone Transaction）
    bytes32 source_hash;
    address from;
    std::optional<address> to;       // nullopt = 合约创建（地址由 from + 执行前 nonce 派生）
    uint256 mint;                    // 先无条件加到 from 余额；与 value 是两个独立字段
    uint256 value;                   // call 中正常转账
    int64_t gas_limit;
    bool is_system_tx;               // Regolith 后必须为 false，否则块不可构建
    bytes data;
};

// l1Cost 调用链（rev.3 闭合）：op_validate 收签名后完整 tx envelope（typed envelope，
// 同 op-geth tx.MarshalBinary()），内部调 computeL1Cost 一次，把结果随 props 返回；
// op_transition 直接消费 OpTxProperties.l1_cost，不重复计算。
struct OpTxProperties {
    TransactionProperties props;     // evmone 原生
    uint256 l1_cost;
    uint256 operator_cost_at_gas_limit;  // has_operator_fee 时按 gasLimit 预计价
};

// 复用 evmone validate_transaction 后追加 OP 检查：
//   1) 拒 blob tx（type-3 在 L2 禁止，evmone Cancun/Prague 会放行；blobGasLeft 内部传 0）；
//   2) 余额检查上限改为 gasLimit*maxGasPrice(=gasFeeCap) + value + l1Cost + operatorCost@gasLimit
//      —— 与 op-geth buyGas 的 balanceCheck（state_transition.go:299-310）精确一致；
//      evmone 原检查不含 l1Cost，边界余额 tx 会误通过（共识级）。
variant<OpTxProperties, error_code>
op_validate(const StateView&, const BlockInfo&, const Transaction&,
            evmc::bytes_view signedTxEnvelope,
            const OpForkConfig&, const OpFeeParams&, int64_t blockGasLeft);

TransactionReceipt op_transition(const StateView&, const BlockInfo&, const BlockHashes&,
                                 const Transaction&, const OpForkConfig&, evmc::VM&,
                                 const OpTxProperties&, const OpFeeParams&);

OpDepositReceipt runDeposit(const StateView&, const BlockInfo&, const BlockHashes&,
                            const DepositTx&, const OpForkConfig&, evmc::VM&);
// OpDepositReceipt = TransactionReceipt + depositNonce + depositReceiptVersion(=1, Canyon+)

uint256 computeL1Cost(const OpFeeParams&, evmc::bytes_view signedTxEnvelope, OpFork);
uint256 computeOperatorCost(const OpFeeParams&, uint64_t gas);  // gas*scalar/1e6 + constant（Isthmus 版）
}
```

关键设计点：

- **`OpHost : evmone::state::Host`**（rev.3 新增组件，解决 §2 的三处 Host 缺陷，用户确认走子类路线不改库）：
  1. **CHAINID**：覆写 `get_tx_context()`，chain_id 来自配置（同时修复 GASPRICE：deposit 语境下 effective price 恒 0，绕开 `host.cpp:408-410` 的 uint256 回绕 bug）；
  2. **precompile 拦截**：覆写 public 虚函数 `call()`（`host.hpp:56`，嵌套 CALL 经 evmc 虚表回到子类，深度递归可拦截）。命中 `PrecompileOverrides` 地址时，自行复刻 `Host::call/execute_message` 中派发前语义（~50 行照抄：depth 检查、journal_create/touch、value 转账、checkpoint/rollback、0x03 quirk，`host.cpp:337-404`）后执行 OP 版 precompile；未命中回落基类。**必须处理地址集扩展**：0x100 在 evmone 的 `is_precompile(PRAGUE)` 为 false，若不拦截会被当空账户调用"成功"——仅替换实现不够；
  3. 需要基类 `State&`：`Host` 构造 public（`host.hpp:48-51`），`OpHost` 经构造参数自持引用。
  4. ETH 路径也可选用 OpHost 修 CHAINID（chain-id ≠ 1 的 ETH 部署同样受 `host.cpp:422` 影响），v1 EEST 测试（chain 1）不需要。

- **`op_transition` fork 边界**：结构照抄 REF 的 `transition()` 及助手（清单见 §3.2），改动仅两处：
  1. **buyGas 段**：预扣改为 `gasLimit*effectiveGasPrice + l1_cost (+ operator_cost_at_gas_limit)` 一次性从 sender 扣（op-geth `mgval`，state_transition.go:283-297；检查在 validate 用 gasFeeCap、扣款在此用 effectiveGasPrice——**两套金额，勿混**）；不在执行前给任何 vault 入账；Host 替换为 OpHost；
  2. **尾部结算段**：tip→coinbase 原样保留（op-geth 付 header coinbase，OP 链把 coinbase 设为 SequencerFeeVault `0x4200…0011`；硬编码 vault 会在 coinbase≠vault 向量上分叉）；新增入账：`gasUsed*baseFee`→BaseFeeVault(`0x4200…0019`)、`l1_cost`→L1FeeVault(`0x4200…001A`)、`computeOperatorCost(gasUsed)`→OperatorFeeVault(`0x4200…001B`) 且退还 sender `operatorCost(gasLimit) - operatorCost(gasUsed)` 差额（三段式，op-geth state_transition.go:727-732, 836-846）。
  其余逐行照抄，保证与 evmone 数值等价。

- **Deposit 独立路径**（`runDeposit`，对照 op-geth）：
  - **跳过 buyGas 与前置检查**（不检查 nonce/fee 字段/EOA，`gasRemaining = gasLimit`，gasPrice=0 无 ETH 退款）；**但 intrinsic gas 照常计扣**（rev.3 补）：deposit 仍走 `IntrinsicGas` 计算，`gasLimit < intrinsic` 归入下述"处理级失败"分支；
  - **EIP-7623 floor 对 deposit 同样适用**（rev.3 补，共识级）：Isthmus（IsPrague=true）下 op-geth `innerExecute` 无任何 deposit 豁免（state_transition.go:547-555, 651-662）——执行成功但 `gasUsed < floorDataGas` 时 gasUsed 被抬至 floor；`gasLimit < floorDataGas` 时走处理级失败。"数据重、执行轻"的用户 deposit 常见命中；
  - `mint` 先无条件加到 from 余额，`value` 在 call 中正常转账（两个独立字段）；
  - **失败双路径**：EVM revert → failed receipt，gasUsed=实际消耗；处理级错误（mint 后仍不够 value、intrinsic 超限、7623 floor 超限）→ 回滚到 **mint 之后**快照、**强制递增 nonce**、gasUsed=**gasLimit 全额**、failed receipt 入块；
  - **`is_system_tx=true` 是块级错误**（rev.3 修正归属）：op-geth `ErrSystemTxNotSupported` 不转 failed receipt，向上冒泡 = 块不可构建（state_transition.go:486）；
  - **receipt 扩展**：`depositNonce` = **执行前** depositor nonce（state_processor.go:174-176；合约创建地址也由此 pre-nonce 派生）+ `depositReceiptVersion=1`（Canyon+）——进 receipt RLP、影响 receiptRoot，以 `OpDepositReceipt` 包装；
  - 复用 OpHost 执行：为 DepositTx 合成 Transaction 壳（gas price 三字段置 0；GASPRICE 观测值经 OpHost 修正为 0，CHAINID 为配置值）；
  - deposit 完全零 fee：无 base fee/tip/L1 fee/operator fee（测试含负向断言）。

- **L1 attributes tx 是一笔 deposit tx**（from=`0xDeaD…0001`，to=L1Block `0x4200…0015`），作为**区块首笔**经 `runDeposit` 真执行并产生自己的 receipt——不允许实现为直接写 storage。`OpFeeParams` 在其执行后从 L1Block 槽读出（op-geth lazy 读取时点不同但已论证等价：槽只有 depositor 能写）。GasPriceOracle 只是用户侧查询入口，执行层不读它。

- **`computeL1Cost`**（v1 = Fjord 公式，Isthmus 沿用）：输入为签名后完整 tx envelope。`estimatedSize = max(100e6, -42_585_600 + 836_500*fastlzSize)`（FastLZ 压缩长度需在本模块移植，严格隔离决策下不复用现有 `bcos-evm/opstack/fee/RollupCost.cpp` 的实现，但用它做黑盒对照）、`fee = estimatedSize*(16*l1BaseFee*baseFeeScalar + blobBaseFee*blobBaseFeeScalar)/1e12`（rollup_cost.go:92-96, 608-640，两轮审查核实零抄错）。Ecotone 公式（`calldataGas*(...)/16e6`）留作扩展位不实现。deposit 恒零。

- **系统调用裁剪**：以 `EVMC_PRAGUE` 喂 evmone 时按 `disable_prague_requests` **禁用 EIP-6110/7002/7251**（op-geth `IsPrague && !IsIsthmus` 门，state_processor.go:141），**4788 beacon root 与 2935 history 保留**——`finalize`/system_contracts 裁剪复用，非零写。

- **Vault/predeploy 是数据**：`OpPredeploys` 把 L1Block / GasPriceOracle / 四 Vault 账户（代码，属创世状态）预填进 TestState；逐块变化的只有 attributes tx 写入的 L1Block 存储。

### 4.4 OP 区块级编排（rev.3 新增）

单块内的完整顺序与账务（v1 由测试 harness 实现，非库代码）：

1. **pre-block 系统调用**：4788 beacon root、2935 history 写入（op-geth 在全部 tx 之前执行，含 attributes tx 之前）；
2. **首笔 = L1 attributes deposit tx** 经 `runDeposit` 执行 → 从 L1Block 槽解包 `OpFeeParams`；
3. **逐笔循环**：deposit（0x7E，全部位于非 deposit 之前，由 fixture 保证）→ `runDeposit`；普通 tx → `op_validate` → `op_transition`。**每笔之后**：`StateDiffWriteback` 写回 state_diff；`blockGasLeft -= gasUsed`（deposit 非 system tx 同样占用区块 gas pool）；`cumulative_gas_used` 跨笔累计（deposit 与普通 tx 混排累计）；
4. **块尾**：OP 块 withdrawals 列表恒空，`finalize` 以空 withdrawals 调用（触发 coinbase touch 语义与 requests 裁剪）；Isthmus withdrawalsRoot 属块头层，见 §1.3 非目标。

---

## 5. 数据流

### 5.1 Ethereum 交易（零 fork）

```
预填 pre-state → runTransaction(blockGasLeft, blobGasLeft 由调用方按 BlobParams 预计算)
  → validate_transaction
  ├─ 失败 → error_code → 拒绝, 状态不变
  └─ 通过 → transition(host.call 递归执行) → TransactionReceipt{state_diff}
     → computeStateRoot(diff) 对照 EEST 期望
```

### 5.2 OpStack 交易（rev.3 与 §4.4 编排对齐）

```
创世: OpPredeploys 预填 L1Block/Oracle/四 Vault（代码+初始存储）
区块开始:
  4788/2935 pre-block 系统调用（在所有 tx 之前）
  首笔 = L1 attributes deposit tx → runDeposit 真执行（写 L1Block 槽, 产生 OpDepositReceipt）
  → 从 L1Block 槽解包 OpFeeParams（slot 1/3/7/8, 布局见 §4.3）
逐笔（deposit 全部在前, fixture 保证; 每笔后写回 diff / 扣 gas pool / 累计 cumulative）:
├─ Deposit(0x7E): runDeposit —— 跳过 buyGas → mint → OpHost 执行（intrinsic 照扣, 7623 floor 适用）
│     成功/revert: gasUsed=max(实际, floor); 处理级失败: 回滚至 mint 后 + nonce++ + gasUsed=gasLimit
│     is_system_tx → 块级错误; receipt 附 depositNonce/Version; 全程零 fee
└─ 普通 tx: op_validate(envelope) —— 内部算 l1Cost; 余额 ≥ gasLimit*gasFeeCap + value + l1Cost + opCost@gasLimit; 拒 blob
      → op_transition:
          buyGas 段: sender 扣 gasLimit*effectiveGasPrice + l1Cost (+opCost@gasLimit)
          OpHost.call 递归执行（precompile 命中 override 表走 OP 版, CHAINID=配置值）
          尾部: tip→coinbase(=SequencerFeeVault) · gasUsed*baseFee→BaseFeeVault
               · l1Cost→L1FeeVault · opCost@gasUsed→OperatorFeeVault + 退差额
块尾: finalize(空 withdrawals, requests 按 disable_prague_requests 裁剪)
→ 对照 op-geth 期望（state_diff / OpDepositReceipt / 四 Vault 余额 / sender 守恒式）
```

### 5.3 ETH vs OP 流向差异

| 环节 | Ethereum | OpStack (Isthmus) |
|------|----------|----------|
| validate | `validate_transaction` 原样 | `op_validate`：+l1Cost/operatorCost 余额检查（gasFeeCap 计价）、拒 blob tx |
| 入口分流 | 单一 `transition` | Deposit(0x7E) 独立路径（跳过 buyGas、intrinsic/7623 照扣），其余走 `op_transition` |
| 预扣 | 仅 buyGas | buyGas + L1 fee + operator fee@gasLimit，一次扣除（effectiveGasPrice 计价） |
| EVM 执行 | evmone Host | **OpHost**：CHAINID 修正、precompile override（0x08 限长/0x100 gas3450/BLS 限长） |
| fee 结算 | tip→coinbase、base 隐式销毁 | tip→coinbase(=SequencerFeeVault)、base→BaseFeeVault、L1→L1FeeVault、operator→OperatorFeeVault(退差额) |
| 系统调用 | 4788/2935 + Prague requests | 4788/2935 保留；6110/7002/7251 禁用 |
| 区块前置 | 无 | pre-block 系统调用 → attributes deposit 首笔真执行 |

---

## 6. 测试策略

**基础设施**：复用 evmone `test/utils`（`evmone::testutils`，port 已导出并冒烟验证）：`statetest_loader` / `blockchaintest_loader` / `TestState`/`TestBlockHashes` / `mpt_hash`（stateRoot/receiptRoot）/ `logs_hash`。不独立实现 JSON 加载。

**EEST fixtures**：沿用 `EVM_REF_EEST_ROOT` 环境变量约定；pin 一个 EEST stable release tag 并与 evmone REF 配对记录；Osaka 允许 skip 清单；CI 缓存 tarball。

**op-geth 对照向量**（两路 + t8n）：
1. 移植 op-geth 单测黄金值（`rollup_cost_test.go`、deposit 处理用例），向量 JSON 自带签名 envelope、L1Block pre-state 槽值、期望 diff/receipt；
2. **t8n 差分（M6 硬 gate，用户确认）**：pinned op-geth `evm t8n` **离线**生成 50–100 个含"执行中观测 fee 环境"的合约向量（执行中读 vault 余额、CHAINID/GASPRICE/COINBASE 观测、7702+OP 预扣组合、零额 operator fee 的 EIP-161 touch 交互）入库，**CI 只回放**，不需要常驻 Go 工具链。这是唯一能发现"没想到的错"（如 CHAINID 硬编码——两套手写向量对其结构性失明）的手段。

| 层 | 测试内容 | 判据 |
|----|---------|------|
| 适配器单测 | `StateDiffWriteback`（deleted_accounts 非空：EIP-6780 / EIP-161） | 写回正确 |
| ETH state | EEST state fixtures（Cancun/Prague/Osaka*） | stateRoot + logsHash 逐位（mpt_hash） |
| ETH blockchain | EEST blockchain fixtures：rejected tx、receipt trie、withdrawals、requests；区块头验证移植自 `blockchaintest_runner`（范围按 §1.3 裁剪） | 区块 stateRoot / receiptRoot |
| OP attributes | attributes tx 自身 receipt（gasUsed 非零、depositNonce 逐块递增）；slot 1/3/7/8 解包逐字节单测（打包槽偏移是高频出错面） | receipt 字段 + OpFeeParams 值 |
| OP Deposit | mint≠value、失败双路径两种 gasUsed、**intrinsic 超限**、**7623 floor 抬升 gasUsed**（rev.3 补）、system tx 块级错误、depositNonce/Version、零 fee 负向断言 | state_diff + gasUsed + receipt 字段 |
| OP L1 fee | Fjord `computeL1Cost`（FastLZ）对照 rollup_cost_test 黄金值 + 现有 bcos-evm/opstack 实现黑盒对照；L1FeeVault 入账 | 数值 + Vault 余额 |
| OP fee 分账 | base→BaseFeeVault、tip→coinbase、operator 三段式含退差额 | 四 Vault 余额 + sender 守恒式：余额差 == gasUsed×effectiveGasPrice + l1Cost + operatorFee@gasUsed |
| OP validate | 边界余额（差 1 wei 付不起 l1Cost）拒绝、blob tx 拒绝 | error_code |
| OP precompile | 0x08 限长 112687、0x100 gas=3450（对照确认 execute 语义与 evmone 7951 版一致后可复用其实现）、BLS 三限长；**0x100 未拦截时被当空账户的负向用例** | 对照 op-geth |
| 差分回归 | 零值 OpFeeParams、operator off 的 `op_transition`+OpHost vs 同输入 evmone `transition()`+Host（向量避开被 override 的 precompile 地址） | state_diff 逐位等价——护栏仅覆盖照抄件不漂移，**不验证 OP 逻辑本身**（OP 逻辑由上面各行 + t8n 验证） |
| **t8n 差分（M6 硬 gate）** | 离线向量库回放（含执行中观测 fee 环境/CHAINID/GASPRICE 的合约） | state_diff 逐位对照 op-geth t8n 输出 |

---

## 7. 工作分解与里程碑（rev.4 重排：决策前置）

### 7.0 本模块的目的与里程碑排序原则（rev.4 新增）

**目的链条**（自上而下，均有上游文档为据）：

1. `blockchain-impl/docs/superpowers/specs/2026-04-20-fisco-bcos-op-execution-client-design.md`：FISCO-BCOS 要**在系统角色上接替 op-geth**，作为 Optimism 执行客户端（被 op-node 驱动、产出 output root、支持提款证明与 fault proof）。
2. `bcos-evm/docs/DESIGN.md`：为此把 EVM 执行抽离为独立模块，"对齐目标：Eth → geth；OP Stack → op-geth"。
3. `bcos-evm/docs/DESIGN-standard-eth-evm-opstack.md` rev.2：**"不再自建状态转换内核，而是直接链接 `evmone::state`"**——本模块即这次转向的落地。

因此 **`bcos-evm-ref` 不是测试工具，而是执行内核的替换尝试**；§1.2 "并行干净模块、验证成熟后再决定切换" 是给这次尝试套的风险控制，不是"造一个 oracle"的决定。转向的动因是可度量的：手写内核（`bcos-evm`，174 个非测试源文件）在 EEST blockchain 的 2848 个 fixture 上有 **405 个失败**（356 receiptsRoot + 37 stateRoot，判定为执行语义偏差）。

**排序原则（rev.4 修订的核心）**：决定"替换是否成立"的两个问题必须**前置**，不能留到 M6：
- **StateView 桥接可行性**：`StateView` 是同步 `noexcept`、`get_account_code` 按值返回整段代码、每 tx 重建 `State`；而宿主是 `task::Task`/`co_await` 协程栈。若桥接达不到生产性能，本模块终局只能是差分 oracle。
- **该答案决定 M4/M5 的归属**：`bcos-evm/opstack/` 已实现 FastLZ、operator fee（已达 Jovian）、deposit 建模、L1Block 槽读取。若替换不成立，OP 语义工作应贡献到 `bcos-evm/opstack/` 而非在本模块重写一遍。

故里程碑改为：**先用 M3 拿到转向的决定性证据，再用 M3.5 spike 回答 go/no-go，然后才决定 M4/M5 写在哪里。**

### 7.1 里程碑表

| 里程碑 | 内容 | 依赖 | 规模 |
|--------|------|------|------|
| **M0 build 打通** | ✅ **已完成**（2026-07-08）：port 注入 `EVMONE_STATE` option、安装两库 + `include/test/` 头树、config 追加 `evmone::state`/`evmone::testutils` | — | 中（实际 <1 天） |
| **M1 适配器** | ✅ **已完成**：`StateDiffWriteback` 缝 + 契约测试（含 EIP-6780/EIP-161 删除语义、code 保留、storage merge） | M0 | 小 |
| **M2 ETH 跑通** | ✅ **已完成**：`eth::runTransaction`/`runBlockFinalize`；EEST v5.4.0 state 对照 **2723 文件 / 55,233 个 Cancun+ case 全绿**（harness 经变异测试证伪假绿） | M1 | 小 |
| **M3 ETH blockchain（决定性证据）** | 移植 `blockchaintest_runner` 核心（~250–280 行：块执行循环、`validate_block` 头校验、侧链/canonical 追踪、四 root + requests_hash 判据），支持过渡 fork（`RevisionSchedule`，过滤 `genesis_rev >= Cancun`）；smoke + full 拆分（3.2 GB / 2848 文件为夜跑级）。**交付物含 oracle 对照表**：同一批 fixture 上 `bcos-evm` 的 405 个失败逐块归因 | M2 | 中 |
| **M3.5 StateView 桥接 spike（go/no-go）** | **rev.4 新增，前置**：把 `StateViewAdapter` 接一次真实账本（或其协程存储的最小切片），度量同步 `noexcept` 接口 + 每 tx 重建 `State` + code 按值返回的开销。**spike 级、非生产实现**。产出 §7.2 的判定：生产替换候选 / 差分 oracle | M3 | 小–中 |
| **决策点** | 依据 M3 的对照表 + M3.5 的性能数据，由用户裁定 §7.2 终局身份，并据此确定 M4/M5 的**代码归属**（本模块 vs `bcos-evm/opstack/`） | M3, M3.5 | — |
| **M4 OP 数据层** | `OpForkSchedule`（Isthmus 条目）+ `OpPredeploys` + `PrecompileOverrides` 数据 + op-geth 向量格式定义与版本 pin | 决策点 | 小 |
| **M5 OP fee/tx** | `OpHost`（三修正 + 派发照抄）、`op_validate`、`op_transition`、`runDeposit`（intrinsic/7623/receipt 扩展）、`RollupCost`（Fjord + FastLZ）、区块级编排 harness；op-geth 黄金向量绿 | M4 | 中 |
| **M6 差分回归 + t8n gate + 收尾** | 零值差分常驻 CI；t8n 离线向量库生成 + CI 回放 gate；upstream `transition()`/`host.cpp` diff 提醒脚本；文档/CI gate | M5 | 中 |

关键路径：M0 → M1 → M2 → **M3 → M3.5 → 决策点** → M4 → M5 → M6。
（rev.4 变更：原 "M3 ∥ (M4→M5)" 的并行被取消——M4/M5 现在依赖决策点，因为它们的代码归属未定；原 M6 里的 "§7.1 切换判据评估报告" 前移为 M3.5 + 决策点。）

OP 区块级 receiptRoot（OpDepositReceipt RLP 编码）与 Isthmus withdrawalsRoot 仍留作 M6 后可选项。

**M3 范围决策**（rev.4，实地核查 REF `3585c2cb` 的 `blockchaintest_runner.cpp` 422 行后确定）：
- BLOCKHASH **不需要 header RLP 编码**：`BlockHeader.hash` 由 loader 从 fixture 字段直接解析，§1.3 的 RLP 排除边界成立；
- 四个 trie（state/transactions/receipts/withdrawals）+ `calculate_requests_hash` **全部闭合在已导出的 `evmone::testutils` / `evmone::state` 内**，port 无需再改；
- **侧链/canonical 追踪完整移植**（~40 行）：post-merge 下 difficulty 恒 0 看似可砍，但 invalid block 之后的块需以最后有效块为 parent，parent-hash map 无论如何要有；
- **纳入过渡 fork**（`CancunToPragueAtTime15k` 等）：`RevisionSchedule` 已在 testutils 内，成本近零；且逐块按时间戳切 revision 与 M4/M5 的 OP fork schedule 是同一模型；
- **排除**：`blockchain_tests_engine*` / `_sync` 目录、pre-Cancun 网络、ommers 与 mining reward（post-merge 恒 nullopt/false）、`calculate_difficulty`（pre-Paris only）。EIP-7934 的 `MAX_RLP_BLOCK_SIZE` 检查用 loader 提供的 `rlp_size`（只量长度不解码），免费保留。

**M4/M5 实施 checklist 附注**（第三轮终审给出，不改设计）：
- M4 的**第一个交付物**是 op-geth 向量 JSON 的字段级 schema（签名 envelope、L1Block pre-state 槽值、期望 diff/receipt 的具体字段定义）；
- M5 的 `runDeposit` 须补"**成功路径** from nonce 同样递增"的显式断言/测试行（§4.3 仅写了处理级失败的强制递增，成功路径靠 Transaction 壳隐式携带，照抄时易漏）；
- `op_validate` 拒 blob tx 走"blobGasLeft 传 0"实现时，错误码是 blob gas 超限而非 op-geth 的 `ErrTxTypeNotSupported` 分类——被拒 tx 不进共识面，仅影响错误报告可读性，实现时注释说明即可。

### 7.2 终局身份与切换判据（rev.4：由 M3 + M3.5 前置回答）

"验证成熟"的可测量定义（**评估报告在决策点产出**，决策人为用户）：

1. **EEST 对照**（M2 ✅ / M3）：state 通过率已达 55,233/55,233；blockchain 通过率须 ≥ 现有 `bcos-evm/eth/`（基线：2848 文件、405 失败）。**M3 的 oracle 对照表是转向动因的直接证据**：若本模块 0 失败，则那 405 个确为 `bcos-evm` 的执行语义偏差，且得到逐块定位。
2. **桥接可行性**（M3.5，**go/no-go**）：若同步 `StateView` + 每 tx 重建 `State` + code 按值返回的开销不可接受 → 终局为**差分 oracle**（为现有 `bcos-evm` 提供共识对照），M4/M5 的 OP 语义工作应贡献到 `bcos-evm/opstack/` 而非在本模块重写；若可接受 → 终局为**生产替换候选**，M4/M5 写在本模块。两种终局都合法，评估报告须明确二选一。
3. **op-geth 黄金向量 + t8n gate 全绿**（M5/M6）：仅在终局为"生产替换候选"时构成合并前提。
4. 评估期内现有 `bcos-evm/opstack/` 的 fork 跟进**不冻结**（旧模块已达 Jovian）；若判定切换，由新模块按 `OpForkSchedule` 扩展位补齐差距。

**范围提醒**：OP 执行客户端所需的 output root、L2ToL1MessagePasser 提款证明、engine API、fault proof 数据均在本模块 §1.3 非目标内。`bcos-evm-ref` 是那栋楼的地基，不是楼。

---

## 8. 前置工作：vcpkg port 导出 `evmone::state`（✅ M0 已实施，rev.3 按实际修正表述）

现状：overlay port `ports/evmone/`（REF `3585c2cb`）原已导出 `evmone::evmone` + `evmone::precompiles`。M0 实施内容（已冒烟验证，**工作区改动待提交**）：

1. **portfile 以 `vcpkg_replace_string` 注入**（rev.3 修正：非 fork 源码补丁——REF 源码零改动，注入发生在 port 构建期）：顶层 `add_subdirectory(lib)` 后追加 `EVMONE_STATE` option 块，挂 `test/state` + `test/utils` + `find_package(nlohmann_json)`。不走 `EVMONE_TESTING`（其 `test/CMakeLists.txt:9-19` 的 hunter + `find_package(GTest/benchmark/CLI11 REQUIRED)` 在 `HUNTER_ENABLED=OFF` 下必炸）；REF 上 `test/state` 的上游挂载路径仅 `EVMONE_TESTING`（"EVMONE_TOOLS 挂入"是 HEAD 布局，rev.3 勘误）。
2. 配置加 `-DEVMONE_STATE=ON`；安装 `libevmone-state.a` + `libevmone.testutils.a`（glob，debug/release）+ `test/state/*.hpp` → `include/test/state` + `test/utils/*.hpp`(含 stdx) → `include/test/utils`。`delegation.hpp` 实际已随 fork 自带的 `install(DIRECTORY lib/evmone/)` 安装，portfile 显式安装冗余无害。
3. config 追加 `evmone::state`（`INTERFACE_LINK_LIBRARIES "evmone::precompiles;evmone::evmone;intx::intx"`——上游 `PRIVATE evmone` 不传递，须显式补链）与 `evmone::testutils`（链 `evmone::state;nlohmann_json::nlohmann_json`）。
4. `ports/evmone/vcpkg.json`：`port-version: 1` + `nlohmann-json` 依赖。
5. **REF 锁定 `3585c2cb`**。升级到 v0.22.0 时整体重核清单：`gas_refund` 字段、EIP-7778 结算段重写、EIP-7981/7976 intrinsic 重构（三者均已实测命中照抄面）、§2 API 描述、§3.2 照抄清单、M6 差分基线。
6. 顶层 `CMakeLists.txt` 平铺加 `add_subdirectory(bcos-evm-ref)` 挂接（M1）。C++20 兼容已验证。

---

## 9. 风险（rev.3 更新）

- **`evmone::state` 属 test 树**：漂移已实测——tag 后 75 提交 test/state 改 8 文件 115 行且三处命中照抄面。锁 REF 控制；**升级预算判据**（rev.3 补）：每次升级人工重核 ~250 行照抄 + §8.5 清单，当该成本 × 年升级次数接近自研 state 层的年 EIP 实现成本（参照现有 bcos-evm/eth/ ≈ 1.5 万行的实证）时重估路线——当前远未到临界。
- **照抄面漂移（~250 行）**：`op_transition` ~202 行 + `OpHost` 派发段 ~50 行。缓解：M6 零值差分常驻 CI（注意其只护照抄件不护 OP 逻辑）+ upstream `transition()`/`host.cpp:337-404` diff 提醒脚本。
- **OpHost 覆写 `call()` 的语义复刻风险**（rev.3 新增）：派发前语义（journal/checkpoint/0x03 quirk）复刻错误 = 共识分叉；0x100 地址集扩展遗漏 = 空账户调用静默成功。缓解：§6 的 precompile 负向用例 + t8n gate。
- **CHAINID / deposit GASPRICE**（rev.3 新增，均由 OpHost 修复）：修复本身经 t8n 向量验证；ETH 路径部署 chain-id ≠ 1 时也须启用 OpHost 的 CHAINID 修正。
- **OP validate 语义**（共识级）：evmone 余额检查不含 l1Cost/operatorCost——`op_validate` 修复，边界余额向量覆盖。
- **deposit×EIP-7623 / intrinsic**（rev.3 新增，共识级）：Isthmus 下无 deposit 豁免，gasUsed 计算三分支（floor 抬升/实际/gasLimit 全额）各有测试行。
- **OP receipt 编码**：depositNonce/Version 进 receipt RLP；tx 级对照不受影响；OP 区块级 receiptRoot 为 M6 后可选项。
- **`StateDiff.deleted_accounts` 非恒空**：EIP-6780 与 EIP-161 场景，写回必须处理（已有源码级证据）。
- **Predeploy/attributes 时序**：OpFeeParams 必须在 attributes deposit 执行后读取；attributes 必须真执行。
- **EEST 版本配对**：EEST release 与 evmone REF 配对 pin；Osaka skip 清单。
- **intx/evmc 类型转换与打包槽解包**：slot 3/8 的字节偏移已入 §4.3 布局表并有逐字节单测行。
- **严格隔离的重复实现**（rev.3 新增，用户确认的显式 trade-off）：FastLZ 等重写一遍，以现有 `bcos-evm/opstack/` 实现做黑盒数值对照降低移植错误风险。
- **共识等价性**：ETH 路径复用 evmone 天然等价（CHAINID 例外见上）；OP 路径靠黄金向量 + t8n gate。

---

## 10. 一句话总结

`bcos-evm-ref` = 链接 `evmone::state`（REF `3585c2cb`）的独立参考模块：Ethereum 直调 `transition()` 零重写（blob 参数由调用方预计算）；OpStack **Isthmus 先行**，六块薄层——`op_validate` / `op_transition` / **`OpHost`**（CHAINID、GASPRICE、precompile 三修正）/ Deposit 全语义路径（intrinsic + 7623 floor 适用）/ `RollupCost`（Fjord 公式 + FastLZ 重实现）/ precompile override 数据——由 `OpForkSchedule → OpForkConfig` 驱动；照抄面 ~250 行以零值差分 + upstream diff 脚本护栏；测试 = EEST（复用 evmone testutils）+ op-geth 黄金向量 + **t8n 离线向量 M6 硬 gate**；与现有 `bcos-evm/` 严格隔离。**目的链条与里程碑排序原则见 §7.0，终局身份（生产替换 vs 差分 oracle）与切换判据见 §7.2——由 M3 的 oracle 对照表与 M3.5 的桥接 spike 前置回答。**
