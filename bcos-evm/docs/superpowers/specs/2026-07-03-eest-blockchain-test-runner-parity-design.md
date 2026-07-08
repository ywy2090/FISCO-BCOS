# EEST Blockchain Test Runner Parity — Design Spec

**Date:** 2026-07-03 (updated 2026-07-07)
**Status:** Approved
**Scope:** 补齐 `eth-eest-test` 的 Blockchain Test Runner，达到与 evmone `blockchaintest_runner` 同等覆盖水平
**Architecture choice:** 路线 1 — 分层架构（Loader + Validation + Runner），测试专用类型
**Success strategy:** **B + C** — 分 fork 里程碑（Cancun 先 ≥90%）+ CI 硬门禁（smoke manifest + nightly full 不 crash）

**Frozen decisions (brainstorming):**

| Item | Choice |
|------|--------|
| Scope | P0 + P1（区块验证核心 + Cancun/Shanghai/Osaka 新特性） |
| 数据模型 | **方案 B** — 新建测试专用的 `BlockchainTestTypes.h`，不动生产代码 |
| 代码组织 | **方案 B** — 分层（Loader + Validation + Runner），和 state test 侧一致 |
| 错误码 | **方案 A** — 新建测试专用 `BlockValidationErrors` 常量集 |
| 实现路线 | **路线 1** — 重构 Runner + 新建 Loader + 独立 Validation 层 |
| 验收策略 | **B + C** — fork 里程碑 + CI 硬门禁（通过率 secondary） |
| CI 辅助 | **方案 C** — 可选 `eth-eest-blockchain-smoke.json` manifest（PR 门禁），不替代 full tree |

**Related specs:**

- `2026-07-02-evmone-inspired-eth-gtest-design.md` — 其中 §3 Pillar 3 `EthEestBlockGranular` 由本 spec 实现
- `2026-06-30-gtest-state-block-eest-migration-design.md` — 父级测试迁移设计

**evmone 复用策略（评估结论，2026-07-07）：**

evmone 仅作 **参考规格**，**不作代码依赖**。三层阻断使得直接链接/复用 evmone 测试组件不可行：

1. **测试语义反向** — evmone `blockchaintest_runner` 走 `evmone::state::transition()`（evmone 自己的 EVM+状态机）。复用它 = 测 evmone 而非 `bcos-evm`，违背 harness 目的。
2. **不可链接** — `evmone.testutils` 是 test-only 静态库，`PUBLIC` 依赖 `evmone::state` + `nlohmann_json`，`PRIVATE` 依赖 `evmc::mocked_host`。FISCO 仅通过二进制包拿到 `evmone::evmone`（VM 库）；`testutils`/`state` 不在导出集，无法 `target_link_libraries`。
3. **类型/JSON 库鸿沟** — evmone 用 `intx::uint256`/`evmc::bytes32`/`nlohmann_json`；FISCO 用 `bcos::u256`/`h256`/`boost::property_tree`。`mpt_hash<T>` 模板绑定 evmone state 类型，喂 FISCO 类型编不过。

**FISCO 已有底层能力，无需从 evmone 搬运：** `GstStateHash.cpp` 已含完整 RLP（`rlpEncodeRaw/Uint64/U256/List`）+ MPT trie（leaf/extension/branch）+ `computeStateRoot`；`keccak256(rlpEncodeList({}))` 即 `EMPTY_MPT_HASH`；Bloom 见 `helpers/BloomFilter.hpp`。因此 `MptHash.h` **扩展 `GstStateHash`** 而非新造。

**应从 evmone「照抄逻辑、用 FISCO 类型重写」的部分（规格，非依赖）：** `validate_block()` 15 项规则、`MAX_RLP_BLOCK_SIZE = 10MB − 2MB = 8MB`、`calc_excess_blob_gas`/`fake_exponential`/`compute_blob_gas_price` 公式、`blob_params.hpp` fork 参数表。

**⚠️ 实现约束：** 禁止 `target_link_libraries(... evmone::testutils)` 或 `evmone::state`；仅允许既有的 `evmone::evmone`（VM）。

**Key evmone reference files（参考规格，非依赖；行号对应本机 `/Users/octopus/octo/code/blockchain-impl/evmone`，跨版本易漂移，实现时以函数名为准）：**

- `test/blockchaintest/blockchaintest_runner.cpp` — `validate_block()` + `run_blockchain_tests()`
- `test/utils/blockchaintest.hpp` — `BlockHeader` / `TestBlock` / `BlockchainTest` 类型定义
- `test/utils/mpt_hash.hpp` — MPT hash 辅助函数（仅参考算法，不链接）
- `test/state/errors.hpp` — 验证错误码定义

---

## 1. Problem Statement

`eth-eest-test` 当前的 Blockchain Test Runner（`EthEestBlockchainRunner.cpp`）仅做了基本的 JSON 加载 + 单层 smoke 验证。对比 evmone 的 `blockchaintest_runner.cpp`（375 行），缺少以下核心能力：

1. **区块头验证** — `validate_block()` 中的 17 项共识规则检查全部缺失（其中 2 项 PoW/ommer 相关 FISCO 不涉及，本 spec 实现 15 项）
2. **无效区块 + expectedException** — 无法验证客户端对非法区块的拒绝行为及错误原因匹配
3. **规范链选择 + lastblockhash** — 无最长链选择逻辑，无 tip hash 校验
4. **区块头 MPT root 逐项校验** — stateRoot/txRoot/receiptsRoot/withdrawalRoot/requestsHash/gasUsed/logsBloom 等 7 项字段均未校验
5. **Blob gas 验证（Cancun+）** — excess_blob_gas 递推计算和 blob_gas_used 累计校验缺失
6. **Withdrawal 支持（Shanghai+）** — withdrawal RLP 解析和 withdrawal_root 校验缺失

当前覆盖率估测 ~40%，补齐后目标 ~90%。

---

## 2. Goals and Non-Goals

### 2.1 Goals（P0 + P1）

| 优先级 | 目标 | 对标 evmone 代码位置 |
|--------|------|---------------------|
| P0 | `validate_block()` 区块头验证（15 项规则，不含 PoW/ommer；见 §6.2） | `blockchaintest_runner.cpp:31-126` |
| P0 | 无效区块 + `expectedException` 匹配（三段检查） | `blockchaintest_runner.cpp:282-356` |
| P0 | 规范链选择 + `lastblockhash` 校验 | `blockchaintest_runner.cpp:358-371` |
| P0 | 区块头 MPT root 逐项校验（7 项字段） | `blockchaintest_runner.cpp:260-281` |
| P1 | Blob gas 验证（`calc_excess_blob_gas` + `compute_blob_gas_price`） | `blockchaintest_runner.cpp:93-116` |
| P1 | Withdrawal RLP 解析 + `withdrawal_root` 校验 | `blockchaintest_runner.cpp:263-267` |
| P1 | EIP-7934 RLP 区块大小限制（Osaka+） | `blockchaintest_runner.cpp:122-123` |

### 2.2 Non-Goals

- Ethash 难度计算（FISCO 不涉及 PoW，difficulty 检查跳过）
- 完整 ommers 处理（仅检查 Paris+ 无 ommers）
- Engine API sync 格式（payload RLP 解码，标记为 **Phase 5**，单独 spec）
- `--trace` / `--trace-summary` CLI 选项（P2）
- Precompile benchmarks / Fuzzer（P3）
- OPStack blockchain runner 适配（现有 OPStack 路径暂不修改）

---

## 3. Architecture

### 3.1 分层设计

```
┌─────────────────────────────────────────────┐
│  Runners（调度 + GTest 断言）                  │
│  EthEestBlockchainRunner.cpp  EthEestBlockGranular.cpp  │
├─────────────────────────────────────────────┤
│  BlockValidation（纯验证函数）                  │
│  helpers/BlockValidation.h                    │
│  validate_block()  calc_base_fee()  ...      │
├─────────────────────────────────────────────┤
│  BlockchainTestLoader（JSON → 结构体）         │
│  src/BlockchainTestLoader.cpp                 │
│  load_blockchain_tests()  parse_*() helpers  │
├─────────────────────────────────────────────┤
│  Types（测试专用数据模型）                       │
│  include/.../BlockchainTestTypes.h            │
│  TestBlockHeader  TestBlock  BlockchainTest  │
└─────────────────────────────────────────────┘
```

### 3.2 新增文件清单

```
bcos-evm/test/eth-eest-test/
  include/bcos-evm/eth-eest-test/
    BlockchainTestTypes.h        # ~120 L  测试专用类型定义
    BlockchainTestLoader.h       # ~30 L   加载器函数声明
    BlockValidationErrors.h      # ~25 L   验证错误码常量
  src/
    BlockchainTestLoader.cpp     # ~250 L  JSON fixture 解析
  helpers/
    BlockValidation.h            # ~200 L  validate_block() + 辅助计算
  # 注：tx/receipts/withdrawal/requests root 扩展现有 src/GstStateHash.cpp，不新建 MptHash.h（§7.1）
  runners/eth/
    EthEestBlockchainRunner.cpp  # ~300 L  重构（CLI standalone，输出到 std::cerr）
    EthEestBlockGranular.cpp     # ~100 L  重构（GTest dynamic registration）
```

**Runner 两种形态**（对标 evmone 同样有两种）：

| Runner | 对标 evmone | 断言方式 |
|--------|-----------|---------|
| `EthEestBlockchainRunner` | N/A（FISCO 独有，manifest/smoke 风格） | `std::cerr` + `std::exit(1)` |
| `EthEestBlockGranular` | `evmone-blockchaintest` | GTest `EXPECT_EQ`/`FAIL()`，支持 `--gtest_filter` |

两者复用同一套 `BlockValidation` + `BlockchainTestLoader`，仅在调度和输出方式上不同。

### 3.3 依赖关系

```
BlockchainTestTypes.h  ← 基础类型（TestBlockHeader, TestBlock, BlockchainTest, BlobSchedule）
    ↓（直接依赖）
┌─── BlockchainTestLoader.cpp  ← boost::property_tree（JSON 解析）
│    └── BlockchainTestLoader.h
├─── BlockValidation.h  ← ForkProfile（已有）
│    └── BlockValidationErrors.h  ← 错误码常量
├─── GstStateHash.h（扩展）  ← 已有 RLP+MPT，新增 tx/receipts/withdrawal/requests root
│
└───→ EthEestBlockchainRunner.cpp  ← BlockTransition.h（已有 applyEthBlock）
     EthEestBlockGranular.cpp     ← BlockValidation.h + BlockchainTestLoader.h + GstStateHash.h
```

---

## 4. Type Definitions

### 4.1 `BlockchainTestTypes.h` — 测试专用数据模型

对标 evmone `test/utils/blockchaintest.hpp`。

```cpp
namespace bcos::evm::reference_tests {

// ── Blob 参数（对标 evmone: state::BlobParams + BlobSchedule map）───

// 对标 evmone: state::BlobParams
struct BlobParams {
    uint16_t target = 3;                      // 对标 uint16_t
    uint16_t max = 6;
    uint32_t base_fee_update_fraction = 3338477;  // 对标 uint32_t
};

// ── 全局常量 ─────────────────────────────────────────────────

// GAS_PER_BLOB 是全局常量（对标 evmone blob_params.hpp 中的 constexpr）
constexpr uint64_t GAS_PER_BLOB = 1 << 17;  // 131072

// EMPTY_MPT_HASH = keccak256(RLP([]))（对标 evmone mpt_hash.hpp）
// 值为 0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421
// 在 §8.1 Genesis 验证中引用，唯一定义在扩展后的 GstStateHash.h（§7.1）

// 对标 evmone: using BlobSchedule = unordered_map<string, BlobParams>;
// 支持 fork-dependent 参数查询（Cancun/Prague/Amsterdam 各不相同）
// Key 约定：使用 JSON fixture 中的 network 字段值直接作为 key（如 "Cancun", "Prague"）
// 查询模式对标 evmone: get_blob_params(network, blob_schedule, timestamp)
using BlobSchedule = std::unordered_map<std::string, BlobParams>;

// ── 区块头（对标 evmone: BlockHeader）─────────────────────────

struct TestBlockHeader {
    // 基础字段
    h256 parent_hash;
    evmc_address coinbase;
    h256 state_root;
    h256 receipts_root;
    bytes logs_bloom;              // 256 字节 Bloom filter
    int64_t difficulty = 0;
    bytes32 prev_randao;
    int64_t block_number = 0;
    int64_t gas_limit = 0;
    int64_t gas_used = 0;
    int64_t timestamp = 0;
    bytes extra_data;
    uint64_t base_fee_per_gas = 0;
    h256 hash;                     // 区块哈希（RPL 编码后 Keccak-256）
    h256 transactions_root;

    // Shanghai+
    h256 withdrawal_root;

    // Cancun+
    h256 parent_beacon_block_root;
    std::optional<uint64_t> blob_gas_used;
    std::optional<uint64_t> excess_blob_gas;

    // Prague+
    h256 requests_hash;

    // Osaka+
    uint64_t slot_number = 0;
};

// ── 测试区块（对标 evmone: TestBlock）────────────────────────

struct TestBlock {
    state::BlockInfo block_info;          // 复用执行层 block 上下文（9 字段不动）
    std::vector<state::Transaction> transactions;

    // 以下字段仅在 validate_block() 中使用，对应 JSON 中的区块输入参数
    // （因 state::BlockInfo 不包含这些共识验证所需字段）
    std::optional<uint64_t> input_blob_gas_used;    // Cancun+：本区块消耗的 blob gas
    std::optional<uint64_t> input_excess_blob_gas;  // Cancun+：父区块累计 excess blob gas
    std::vector<Withdrawal> withdrawals;            // Shanghai+：withdrawal 列表
    size_t rlp_size = 0;                            // Osaka+：RLP 编码区块大小
    bool withdrawals_parse_success = true;
    std::string expected_exception;       // 空 = 有效区块

    TestBlockHeader expected_block_header;  // 期望的完整区块头输出
};

// ── 区块链测试（对标 evmone: BlockchainTest）─────────────────

struct BlockchainTest {
    struct Expectation {
        h256 last_block_hash;
        std::variant<TestStateView, h256> post_state;
    };

    std::string name;
    std::vector<TestBlock> test_blocks;
    TestBlockHeader genesis_block_header;
    TestStateView pre_state;
    std::string network;                  // "London", "Cancun", ...
    BlobSchedule blob_schedule;

    Expectation expectation;
};

}  // namespace
```

**类型映射表（evmone → FISCO）：**

| evmone | FISCO | 说明 |
|--------|-------|------|
| `intx::uint256` | `bcos::u256` | 大整数 |
| `hash256` (evmc::bytes32) | `h256` (bcos-utilities) | 32 字节哈希 |
| `bytes` (evmone 自定) | `bcos::bytes` | 字节数组 |
| `state::BloomFilter` | 复用 `helpers/BloomFilter.hpp` | 已有 |
| `state::BlockInfo` | 不变，仅用作执行上下文 | 9 字段保持不变（见 §11） |
| `TestState` (evmone) | `TestStateView` | 已有（`include/.../TestStateView.h`），可值拷贝 |
| `std::vector<Ommers>` | 省略（仅检查 Paris+ 无 ommers） | FISCO 不涉及 |
| `RevisionSchedule` | `ForkProfileRegistry` + `network` 字符串 + `resolveRevision()` 新函数 | §8.0 |

**evmone 的 `state::BlockInfo` 包含 `blob_gas_used`/`excess_blob_gas`/`withdrawals` 等字段，但 FISCO 的 `state::BlockInfo`（`eth/state/BlockInfo.hpp`）只有 9 个执行层字段（number, timestamp, gasLimit, coinbase, prevRandao, `parentHash`, baseFee, chainId, blobBaseFee）。其中 `parentHash` 已存在，故 §8.1 的 `tb.block_info.parent_hash` 有依据（对应 `.parentHash`）。共识验证所需的 blob/withdrawal 字段被安置在 `TestBlock`（input_* 前缀）和 `TestBlockHeader` 中，保持生产代码不变。**

### 4.2 `BlockValidationErrors.h` — 错误码常量

```cpp
namespace bcos::evm::reference_tests::BlockError {

inline constexpr auto INVALID_BLOCK_PARENT =
    "INVALID_BLOCK_PARENT";
inline constexpr auto INVALID_BLOCK_NUMBER =
    "INVALID_BLOCK_NUMBER";
inline constexpr auto INCORRECT_BLOCK_FORMAT =
    "INCORRECT_BLOCK_FORMAT";
inline constexpr auto INVALID_GASLIMIT =
    "INVALID_GASLIMIT";
inline constexpr auto INVALID_BLOCK_TIMESTAMP_OLDER_THAN_PARENT =
    "INVALID_BLOCK_TIMESTAMP_OLDER_THAN_PARENT";
inline constexpr auto INVALID_BASEFEE_PER_GAS =
    "INVALID_BASEFEE_PER_GAS";
inline constexpr auto INCORRECT_EXCESS_BLOB_GAS =
    "INCORRECT_EXCESS_BLOB_GAS";
inline constexpr auto RLP_BLOCK_LIMIT_EXCEEDED =
    "RLP_BLOCK_LIMIT_EXCEEDED";

}  // namespace
```

---

## 5. BlockchainTestLoader — JSON 解析层

### 5.1 文件：`src/BlockchainTestLoader.cpp`（~250 L）

对标 evmone `test/utils/blockchaintest.hpp::load_blockchain_tests()`。

**核心函数签名：**

```cpp
namespace bcos::evm::reference_tests {

// 主入口
std::vector<BlockchainTest> loadBlockchainTests(std::istream& input);

// 内部 helper（对标 evmone JSON 解析逻辑）
TestBlockHeader parseBlockHeader(const boost::property_tree::ptree& j);
TestBlock        parseTestBlock(const boost::property_tree::ptree& j);
TestStateView    parsePreState(const boost::property_tree::ptree& j);
BlockchainTest::Expectation parseExpectation(const boost::property_tree::ptree& j);
BlobSchedule     parseBlobSchedule(const boost::property_tree::ptree& j);

}  // namespace
```

**JSON 解析技术选型**：使用 `boost::property_tree`，与现有 `EthEestBlockchainRunner.cpp` 风格一致。不引入 `nlohmann::json`。注意 `EthBlockTransitionTest.cpp` 使用了 `jsoncpp`（`Json::Value`），Load 层统一用 `boost::property_tree`，避免依赖两个 JSON 库。

**需要处理的字段**（`parseBlockHeader` 中）：

```
必须解析:     parent_hash, coinbase, state_root, receipts_root,
             logs_bloom, difficulty, prev_randao, block_number,
             gas_limit, gas_used, timestamp, extra_data,
             base_fee_per_gas, hash, transactions_root

Shanghai+:   withdrawal_root
Cancun+:     parent_beacon_block_root, blob_gas_used, excess_blob_gas
Prague+:     requests_hash
Osaka+:      slot_number
```

**evmone ↔ FISCO 解析差异：**

| evmone | FISCO | 处理方式 |
|--------|-------|---------|
| `nlohmann::json` | `boost::property_tree` | 手写 `get_optional<string>` + hex 解析 |
| `from_json<T>()` 模板 | 显式 `parseXxx()` 函数 | 与 `GeneralStateTestLoader` 风格一致 |
| hex → `intx::uint256` | hex → `bcos::u256` | 已有 `fromBigQuantity()` |
| hex → `bytes` | hex → `bcos::bytes` | 已有 `bcos::fromHex()` |

---

## 6. BlockValidation — 验证引擎

### 6.1 文件：`helpers/BlockValidation.h`（~200 L）

对标 evmone `blockchaintest_runner.cpp:27-137`。

**核心函数：**

```cpp
namespace bcos::evm::reference_tests {

// ── 主验证函数（对标: validate_block()）─────────────────────

/// 返回 nullopt = 区块有效；返回 string = 错误码
std::optional<std::string> validateBlock(
    evmc_revision rev,
    const BlobSchedule& blobSchedule,
    const TestBlock& testBlock,
    const TestBlockHeader* parentHeader);

// ── 辅助计算函数 ────────────────────────────────────────────

/// 对标: evmone calc_base_fee()
uint64_t calcBaseFee(uint64_t parentGasLimit, uint64_t parentGasUsed,
                     uint64_t parentBaseFee);

/// 对标: evmone compute_blob_gas_price()
uint64_t computeBlobGasPrice(const BlobSchedule& schedule,
                             uint64_t excessBlobGas);

/// 对标: evmone calc_excess_blob_gas()
uint64_t calcExcessBlobGas(evmc_revision rev,
                           const BlobSchedule& schedule,
                           uint64_t parentBlobGasUsed,
                           uint64_t parentExcessBlobGas,
                           uint64_t parentBaseFee,
                           uint64_t parentBlobBaseFee);

/// 对标: evmone mining_reward() — 仅 M5 历史 PoW fork（<PARIS）需要；本 spec 主体 PoS 下 reward=0
/// 当前 evmone 值: <BYZANTIUM=5ETH, <CONSTANTINOPLE=3ETH, <PARIS=2ETH, >=PARIS=0
/// YAGNI：Phase 0–4 不实现（EEST PoS fixture coinbase reward=0）；仅当 M5 PoW parity 需要时补
std::optional<uint64_t> miningReward(evmc_revision rev);

}  // namespace
```

### 6.2 `validate_block()` 逐项检查清单

| # | 检查项 | 不满足时返回 |
|---|--------|------------|
| 1 | 父区块存在 | `INVALID_BLOCK_PARENT` |
| 2 | `block_number == parent->block_number + 1` | `INVALID_BLOCK_NUMBER` |
| 3 | `gas_used <= gas_limit` | `INCORRECT_BLOCK_FORMAT` |
| 4 | `gas_limit` 超过父块的 `+1/1024`（即 `>= parent + parent/1024`） | `INVALID_GASLIMIT` |
| 5 | `gas_limit` 低于父块的 `-1/1024`（即 `<= parent - parent/1024`） | `INVALID_GASLIMIT` |
| 6 | `gas_limit >= 5000`（Yellow Paper 最小值） | `INVALID_GASLIMIT` |
| 7 | `timestamp > parent->timestamp` | `INVALID_BLOCK_TIMESTAMP_OLDER_THAN_PARENT` |
| 8 | Paris+ 不允许 ommers | `INCORRECT_BLOCK_FORMAT` |
| 9 | `extra_data.size() <= 32` | `INCORRECT_BLOCK_FORMAT` |
| 10 | London+: `base_fee_per_gas == calcBaseFee(...)` | `INVALID_BASEFEE_PER_GAS` |
| 11 | Cancun+: `blob_gas_used`、`excess_blob_gas` 必须存在 | `INCORRECT_BLOCK_FORMAT` |
| 12 | Cancun+: `excess_blob_gas` 递推计算正确 | `INCORRECT_EXCESS_BLOB_GAS` |
| 13 | 非 Cancun: `blob_gas_used`、`excess_blob_gas` 不得存在 | `INCORRECT_BLOCK_FORMAT` |
| 14 | `withdrawals_parse_success == true` | `INCORRECT_BLOCK_FORMAT` |
| 15 | Osaka+: `rlp_size <= 8MB`（EIP-7934 `MAX_RLP_BLOCK_SIZE`） | `RLP_BLOCK_LIMIT_EXCEEDED` |

**注意**：`slot_number` 字段（Osaka+）仅在 JSON 解析层映射（§5.1），无对应的独立验证规则 — 它通过区块头字段比对间接验证（§8.3 `expected_block_header` 逐项对比）。

**跳过项（FISCO 不涉及）：**

| evmone 检查项 | 跳过原因 |
|-------------|---------|
| `difficulty == calculate_difficulty(...)` | FISCO 不涉及 PoW / Ethash |
| ommer delta 范围检查 (1-6) | FISCO 不涉及叔块 |

---

## 7. MPT Hash 扩展

### 7.1 文件：扩展现有 `GstStateHash.h`（**决定：扩展，不新建**）

**复用结论（关闭旧 Open Q2）：** `GstStateHash.cpp` 已含完整 RLP（`rlpEncodeRaw/Uint64/U256/List`）+ MPT trie（leaf/extension/branch node）+ `computeStateRoot`，且 `keccak256(rlpEncodeList({}))` 即 `EMPTY_MPT_HASH`。因此新 root 函数直接在此文件扩展，不新建 `MptHash.h`，不引入 evmone。

**返回类型约定：** 现有 `computeStateRoot()` / `computeLogsHash()` 返回 `evmc_bytes32`。为保持一致，**新增函数统一返回 `evmc_bytes32`**（不用 `h256`）。断言时 `TestBlockHeader` 的 `h256` 字段与 `evmc_bytes32` 通过 helper（`toEvmcBytes32(h256)` / 逐字节 `memcmp`）比较——§8.1/§8.3 的 `EXPECT_EQ` 两侧须先归一到同一类型。

```cpp
namespace bcos::evm::reference_tests {

// 对标 evmone: mpt_hash(transactions)
evmc_bytes32 computeTxRoot(std::span<const state::Transaction> txs);

// 对标 evmone: mpt_hash(receipts)
evmc_bytes32 computeReceiptsRoot(std::span<const TransactionReceipt> receipts);

// 对标 evmone: mpt_hash(withdrawals) — Withdrawal 类型定义见 §10.1
evmc_bytes32 computeWithdrawalRoot(std::span<const Withdrawal> withdrawals);

// 对标 evmone: calculate_requests_hash() — SHA256，非 MPT
// requests 为 EIP-7685 序列化后的 request 字节列表（每项 = type byte ++ payload）
evmc_bytes32 computeRequestsHash(std::span<const bytes> requests);

}  // namespace
```

**`TransactionReceipt` 结构扩展**（对标 evmone `state::TransactionReceipt`）：
需要在 `helpers/BlockTransition.h` 或 `BlockchainTestTypes.h` 中补全：

```cpp
struct TransactionReceipt {
    evmc_status_code status = EVMC_SUCCESS;
    int64_t gas_used = 0;
    int64_t gas_refund = 0;  // EIP-7778
    bytes bloom;             // 2048-bit bloom filter
    std::vector<state::LogEntry> logs;
};
```

当前 `BlockTransition.h` 中的 `TransactionReceipt` 仅有 `log` 和 `gasUsed`，需要扩展。

---

## 8. Runner 重构

### 8.0 两种 Runner 的分工

| | `EthEestBlockchainRunner` (CLI) | `EthEestBlockGranular` (GTest) |
|---|---|---|
| 对标 evmone | N/A（FISCO 独有，对应 manifest 驱动风格） | `evmone-blockchaintest` |
| 输入 | `--fixtures <dir> --limit N` | 命令行 path（目录或文件） |
| 测试粒度 | 每个 JSON fixture = 一个输出行 | 每个 JSON fixture = 一个 GTest suite |
| 断言方式 | `std::cerr << "PASS/FAIL"` + `std::exit(1)` | `EXPECT_EQ` / `EXPECT_TRUE` / `FAIL()` |

两者复用同一套 `BlockValidation` + `BlockchainTestLoader`。下文以 GTest 版本（对标 evmone）为主要展开对象，CLI 版本在主循环中调用同逻辑但替换输出方式。

**需要新增的辅助函数**：`resolveRevision(network, timestamp)` — 通过 `ForkProfileRegistry::findByUpstreamFork(network)` 查找 `ForkProfile`→提取 `RevisionConfig`（EIP feature flags）→配合 timestamp 做 fork 过渡判定→返回 `evmc_revision`。当前 `ForkProfileRegistry` 无此函数，需新建。

### 8.1 `EthEestBlockGranular.cpp` — GTest 主循环（~100 L，重构）

对标 evmone `blockchaintest_runner.cpp:165-373`。

**重构后主流程：**

```cpp
void runBlockchainFixtures(fs::path const& fixturesDir, size_t limit) {
    // 1. 扫描所有 .json 文件（已有）
    // 2. fork 检测（已有，从路径推导 upsteamFork）
    // 3. 每个 JSON 调用 loadBlockchainTests() → runOneFixture()

    for (auto& jsonPath : jsonFiles) {
        auto tests = loadBlockchainTests(inputStream);
        for (auto& test : tests) {
            runBlockchainTest(test, profile, vm, hashImpl);
        }
    }
}

void runBlockchainTest(const BlockchainTest& test, ForkProfile& profile,
                       evmc::VM& vm, bcos::crypto::Hash& hashImpl) {
    // ── 1. Genesis 验证（对标 evmone L175-183）──
    // 统一用 resolveRevision(network, timestamp)，不引入 evmone 的 rev_schedule
    const auto genesisRev = resolveRevision(test.network, test.genesis_block_header.timestamp);
    EXPECT_EQ(test.genesis_block_header.block_number, 0);
    EXPECT_EQ(test.genesis_block_header.gas_used, 0);
    EXPECT_EQ(test.genesis_block_header.transactions_root, EMPTY_MPT_HASH);
    EXPECT_EQ(test.genesis_block_header.receipts_root, EMPTY_MPT_HASH);
    // withdrawal_root: Shanghai+ 则为 EMPTY_MPT_HASH，否则为 0
    if (genesisRev >= EVMC_SHANGHAI)
        EXPECT_EQ(test.genesis_block_header.withdrawal_root, EMPTY_MPT_HASH);
    else
        EXPECT_EQ(test.genesis_block_header.withdrawal_root, h256{});
    EXPECT_EQ(test.genesis_block_header.logs_bloom, bytes(256, 0));  // 空 bloom

    // ── 2. 初始化 ──
    const auto& genesis = test.genesis_block_header;
    std::unordered_map<int64_t, h256> block_hashes;
    block_hashes[0] = genesis.hash;
    struct BlockData { const TestBlockHeader* header; TestStateView post_state;
                       uint64_t total_difficulty; };
    std::unordered_map<h256, BlockData> block_data;
    // 注意：total_difficulty 初始化为 genesis.difficulty（evmone），
    // FISCO 中 difficulty 通常为 0（非 PoW 链），从 0 开始等效
    block_data[genesis.hash] = {&genesis, test.pre_state, 0};
    TestStateView const* canonical_state = &test.pre_state;
    h256 canonical_tip = genesis.hash;
    uint64_t max_total_difficulty = 0;

    // ── 3. 逐块循环 ──
    for (auto& tb : test.test_blocks) {
        const auto rev = resolveRevision(test.network, tb.block_info.timestamp);
        const auto blob_gas_limit = maxBlobGasPerBlock(test.blob_schedule);

        // 3a. 查找父区块数据（对标 evmone L206-210）
        // BlockInfo.parentHash 在 Loader 解析时填入 tb.block_info.parentHash
        auto parent_it = block_data.find(tb.block_info.parentHash);
        auto* parent_header =
            (parent_it != block_data.end()) ? parent_it->second.header : nullptr;

        // 3b. 区块头验证
        auto block_error = validateBlock(rev, test.blob_schedule, tb, parent_header);

        if (!tb.expected_exception.empty()) {
            // ── 无效区块路径 ──（三段检查见 §8.2）
            if (block_error) {
                EXPECT_TRUE(tb.expected_exception.contains(*block_error));
                continue;  // Level 1：正确拒绝
            }
            // 区块头合法 → 从父块 post_state 起执行交易，走 Level 2/2.5/3
            if (parent_it == block_data.end()) { ADD_FAILURE(); continue; }
            auto res = applyEthBlock(
                parent_it->second.post_state, tb.transactions, tb.block_info, profile,
                vm, hashImpl);
            // ... Level 2 (res.rejected) / Level 2.5 (res.requests_error) / Level 3（字段比对）
            // 全部字段匹配 → FAIL（预期无效但实际有效）
        } else {
            // ── 有效区块路径 ──
            EXPECT_FALSE(block_error);  // 不应被 validateBlock 拒绝
            if (parent_it == block_data.end()) { ADD_FAILURE(); continue; }

            // 使用父区块 post_state 作为执行起点（对标 evmone L230-231）
            auto res = applyEthBlock(
                parent_it->second.post_state, tb.transactions, tb.block_info, profile,
                vm, hashImpl);
            // 逐项校验区块头字段（见 §8.3）

            block_hashes[tb.expected_block_header.block_number] =
                tb.expected_block_header.hash;

            // 记录该块 BlockData，并按 §8.4 更新 canonical tip
            auto const total_difficulty =
                parent_it->second.total_difficulty + tb.block_info.difficulty;
            auto [it, _] = block_data.insert_or_assign(
                tb.expected_block_header.hash,
                BlockData{&tb.expected_block_header, res.postState, total_difficulty});
            if (total_difficulty >= max_total_difficulty) {  // PoS：等效"输入顺序最后一个有效块"
                canonical_state = &it->second.post_state;
                canonical_tip = tb.expected_block_header.hash;
                max_total_difficulty = total_difficulty;
            }
        }
    }

    // ── 4. 最终验证 ──
    EXPECT_EQ(canonical_tip, test.expectation.last_block_hash);
    // postState 比对（见 2026-07-08 postState diff spec §4.4）：
    //   - postStateHash：computeStateRoot(*canonical_state) == hash
    //   - postState 账户表：逐账户 **部分字段 diff**（非 root-of-view 相等），
    //     仅比 JSON 出现的 nonce/balance/code/storage；未列账户/槽位忽略。
    assertPostState(*canonical_state, test.postExpectation, {eip158ClearEmpty});
}
```

### 8.2 无效区块的三段检查（P0）

对标 evmone `blockchaintest_runner.cpp:282-356`：

```
Level 1: validate_block() 返回错误
         → 错误原因匹配 expected_exception（字符串子串匹配，支持 | 分隔的多选一）
         → continue（正确拒绝）

Level 2: 区块头合法但交易执行失败
         → applyEthBlock() 返回 rejected 列表
         → expected_exception 包含 "TransactionException."
         → continue（交易级拒绝）

Level 2.5: 交易执行成功但 requests 收集失败（EIP-7685，对标 evmone L321-330）
           → res.requests_error 非空
           → 错误原因匹配 expected_exception（子串匹配）
           → continue（requests 收集层拒绝）

Level 3: 交易也执行成功了，requests 也正常
         → 逐项检查区块头字段（stateRoot, txRoot, ...）
         → 找到第一个不匹配的字段
         → continue（共识层面分歧，expected exception 覆盖）
         
         → 如果所有字段都匹配：
           FAIL("Expected block to be invalid but resulted valid")
```

### 8.3 有效区块的区块头字段校验（P0 + P1）

对标 evmone `blockchaintest_runner.cpp:260-281`：

```cpp
// 有效区块执行后（对标 evmone L234-281）：
ASSERT_FALSE(res.requests_error);  // EIP-7685 requests 收集不应失败
EXPECT_TRUE(res.rejected.empty())  // 有效区块中不应有交易被拒绝
    << "Invalid transaction in block expected to be valid";

// 注意：BlockApplyResult 的状态字段名为 `postState`（非 evmone 的 block_state）
EXPECT_EQ(computeStateRoot(res.postState), expected_header.state_root);
EXPECT_EQ(computeTxRoot(tb.transactions), expected_header.transactions_root);
EXPECT_EQ(computeReceiptsRoot(res.receipts), expected_header.receipts_root);

if (rev >= EVMC_SHANGHAI) {
    EXPECT_EQ(computeWithdrawalRoot(tb.withdrawals),
              tb.expected_block_header.withdrawal_root);
// 注意：withdrawals 在 TestBlock 上（§4.1），不在 state::BlockInfo 上
}

if (rev >= EVMC_PRAGUE) {
    EXPECT_EQ(computeRequestsHash(res.requests),
              expected_header.requests_hash);
}
// ⚠️ B2：res.requests 的来源见 §8.3.1 —— 若 Phase 3 未打通采集，Prague requestsHash
//        校验降级为 XFAIL 并在 §15.2 记录，不阻塞 M1(Cancun)。

EXPECT_EQ(res.gas_used, expected_header.gas_used);
EXPECT_EQ(res.bloom, expected_header.logs_bloom);

// Blob gas 一致性（Cancun+）
EXPECT_EQ(blob_gas_limit - res.blob_gas_left,
          static_cast<int64_t>(expected_header.blob_gas_used.value_or(0)));
```

### 8.3.1 ⚠️ EIP-7685 requests 采集来源（B2 — Prague+ 前置）

**问题：** §8.3 校验 `requestsHash`（Prague+），§11 让 `BlockApplyResult` 加 `requests` 字段，但 requests **不来自交易返回值**——它由三个系统合约在区块末尾产出：

| Request type | EIP | 来源 |
|--------------|-----|------|
| `0x00` deposits | EIP-6110 | 扫描本块 receipts 中 deposit 合约的 log |
| `0x01` withdrawals | EIP-7002 | 区块末尾调用 withdrawal-request 预部署合约，读其返回 |
| `0x02` consolidations | EIP-7251 | 区块末尾调用 consolidation 预部署合约，读其返回 |

**采集路径（Phase 3）：** 在 `applyEthBlock` 末尾（withdrawal credit 之后）：
1. 遍历 receipts，匹配 EIP-6110 deposit 合约地址的 log → 拼 `0x00 ++ payload`
2. 对 EIP-7002 / EIP-7251 预部署合约各发一次系统调用，取 output → `0x01/0x02 ++ output`
3. 按 type 升序拼成 `res.requests`（`std::vector<bytes>`），`computeRequestsHash` 做 `sha256(concat)` 逐段哈希

**降级策略：** 若 Phase 3 未打通系统合约调用，Prague+ 的 `requestsHash` 校验标记 `GTEST_SKIP`/XFAIL，在 §15.2 M2（Prague）baseline 中记录，**不阻塞 M1（Cancun，无 requests）**。

### 8.4 规范链选择（P0）

对标 evmone `blockchaintest_runner.cpp:247-252, 358-371`：

```cpp
// 每处理完一个区块后：
// 1. 更新该区块的 total_difficulty
auto total_difficulty = parent_data.total_difficulty + tb.block_info.difficulty;

// 2. 按最大 total_difficulty 选择规范链 tip
if (total_difficulty >= max_total_difficulty) {
    canonical_state = &current_post_state;
    canonical_tip = tb.expected_block_header.hash;
    max_total_difficulty = total_difficulty;
}

// 循环结束后（post_state 比对见 §8.1 expectPostStateMatches）：
EXPECT_EQ(canonical_tip, test.expectation.last_block_hash);
expectPostStateMatches(*canonical_state, test.expectation.post_state);
```

**PoS fixture 语义（M-3）：** FISCO 链 `difficulty = 0`，`total_difficulty` 恒不增，`>=` 比较使 canonical tip **等效为"输入顺序中最后一个通过 `validateBlock` 的块"**——这与 EEST PoS fixture 的约定一致（Paris+ 无 PoW，规范链即区块顺序）。对含竞争链/reorg 的历史 PoW fixture（M5 范围），`total_difficulty` 累加才生效；本 spec 以 PoS 语义为主，PoW reorg 归入 M5 baseline。

---

## 9. Blob Gas 完整验证（P1）

### 9.1 新增辅助函数

对标 evmone `blockchaintest_runner.cpp:93-116` 和 evmone 的 `state/` 中 blob 相关逻辑。

常量 `GAS_PER_BLOB` 已在 §4.1 BlobParams 旁定义；`target`/`max`/`base_fee_update_fraction` 均从 `BlobSchedule` map 按 fork 名查表获取（对标 evmone `get_blob_params(network, blob_schedule, timestamp)` 模式）。

```cpp
/// 对标: EIP-4844 `fake_exponential()`
uint64_t fakeExponential(uint64_t factor, uint64_t numerator, uint64_t denominator);

/// 对标: evmone `calc_excess_blob_gas()`
uint64_t calcExcessBlobGas(evmc_revision rev,
                           const BlobSchedule& schedule,
                           uint64_t parentBlobGasUsed,
                           uint64_t parentExcessBlobGas,
                           uint64_t parentBaseFee,
                           uint64_t parentBlobBaseFee);

/// 对标: evmone `compute_blob_gas_price()`
uint64_t computeBlobGasPrice(const BlobSchedule& schedule,
                             uint64_t excessBlobGas);

/// 对标: evmone `max_blob_gas_per_block()`
uint64_t maxBlobGasPerBlock(const BlobSchedule& schedule);
```

### 9.2 `validate_block()` 中 blob 检查

```cpp
// Cancun+ 检查：
if (rev >= EVMC_CANCUN) {
    // 字段存在性
    if (!tb.input_blob_gas_used.has_value() ||
        !tb.input_excess_blob_gas.has_value())
        return BlockError::INCORRECT_BLOCK_FORMAT;

    // excess_blob_gas 递推计算
    auto parent_blob_base_fee = computeBlobGasPrice(
        blobSchedule, parent_header->excess_blob_gas.value_or(0));
    auto expected_excess = calcExcessBlobGas(rev, blobSchedule,
        parent_header->blob_gas_used.value_or(0),
        parent_header->excess_blob_gas.value_or(0),
        parent_header->base_fee_per_gas,
        parent_blob_base_fee);
    if (tb.input_excess_blob_gas.value() != expected_excess)
        return BlockError::INCORRECT_EXCESS_BLOB_GAS;
} else {
    // 非 Cancun: 这些字段不得存在
    if (tb.input_blob_gas_used.has_value() ||
        tb.input_excess_blob_gas.has_value())
        return BlockError::INCORRECT_BLOCK_FORMAT;
}
```

---

## 10. Withdrawal 支持（P1）

### 10.1 数据模型

在 `state::BlockInfo` 不动的前提下，withdrawal 数据**仅在 `TestBlock` 中承载**：

在 `BlockchainTestTypes.h` 中添加：

```cpp
struct Withdrawal {
    uint64_t index;
    uint64_t validator_index;
    evmc_address address;
    uint64_t amount;  // in Gwei
};
```

在 `TestBlock` 中添加：

```cpp
struct TestBlock {
    // ... 原有字段
    std::vector<Withdrawal> withdrawals;     // Shanghai+: RLP 解析后的 withdrawal 列表
    bool withdrawals_parse_success = true;   // 已有，保持不变
};
```

同样，Cancun+ 的 blob 相关字段（`blob_gas_used`、`excess_blob_gas`）通过 JSON 解析填入 `TestBlockHeader`（`std::optional<uint64_t>`），`validate_block()` 直接用 `TestBlockHeader` 中的值做比对，不依赖 `state::BlockInfo`。

### 10.2 `withdrawal_root` 校验

对标 evmone `blockchaintest_runner.cpp:265-267`：

```cpp
if (rev >= EVMC_SHANGHAI) {
    // withdrawals 在 TestBlock 上（§4.1），不在 state::BlockInfo 上
    EXPECT_EQ(computeWithdrawalRoot(tb.withdrawals),
              tb.expected_block_header.withdrawal_root);
}
```

### 10.3 ⚠️ Withdrawal balance credit（B1 — stateRoot 通过率关键路径）

**问题：** 当前 `applyEthBlock`（`BlockTransition.h`）只执行交易，**不对 withdrawal 做 balance 增记**。而 §8.3 要校验 Shanghai+ 有效块的 `stateRoot`——若不 credit withdrawal 余额，任何含 withdrawal 的块 `stateRoot` 必然 mismatch，直接压垮 M1/M4 的通过率目标。

**修复（Phase 3，随 withdrawal 一起落地）：** 在 `applyEthBlock` 于**所有用户交易执行之后、构建 post-state 之前**，对每笔 withdrawal 执行余额增记：

```cpp
// EIP-4895: withdrawal.amount 单位为 Gwei，credit 时换算为 Wei
for (auto const& w : withdrawals) {
    account(w.address).balance += u256(w.amount) * 1'000'000'000;  // ×10^9
    // 若目标账户不存在则按 touch/create 语义处理（EIP-158 空账户规则）
}
```

**接口影响：** `applyEthBlock` 需新增 `std::span<const Withdrawal> withdrawals = {}` 形参（默认空，兼容既有 state-test 调用方）。这是对 §13 "不改 `applyEthBlock` 公共 API" 的**受控例外**——追加带默认值的形参，现有调用点不受影响。

**验收挂钩：** 该修复是 §15.2 中 **M4（Shanghai）里程碑**的前置条件；Cancun（M1）本身也含 withdrawal 用例，故 Phase 3 必须完成。

---

## 11. 对现有文件的修改

| 文件 | 修改类型 | 内容 |
|------|---------|------|
| `CMakeLists.txt` | 修改 | 新增 `BlockchainTestLoader.cpp` 源文件 + `BlockchainTestTypes.h` 头文件；**不新增 `evmone::testutils` 链接** |
| `helpers/BlockTransition.h` | 修改 | 扩展 `TransactionReceipt`（bloom, logs[], status, gas_refund）; 扩展 `BlockApplyResult`（bloom, blob_gas_left, requests, requests_error, rejected）; `applyEthBlock` 追加 `withdrawals` 默认形参（§10.3） |
| `src/GstStateHash.cpp` + `include/.../GstStateHash.h` | **扩展（非新建 MptHash.h）** | 复用现有 RLP+MPT，新增 `computeTxRoot()` / `computeReceiptsRoot()` / `computeWithdrawalRoot()` / `computeRequestsHash()`，均返回 `evmc_bytes32`（§7.1） |
| `runners/eth/EthEestBlockchainRunner.cpp` | **重构** | 替换为使用 Loader + Validation + 新 runner 逻辑 |
| `runners/eth/EthEestBlockGranular.cpp` | 修改 | 复用 Loader + Validation，改为细粒度 per-file GTest |

**不修改的文件：**
- `eth/state/BlockInfo.hpp` — 保持 9 字段不变
- `eth/state/Transaction.hpp` — 不变
- `eth/state/StateDiff.hpp` — 不变
- 所有 `eth-eest-test/src/` 下的现有 loader — 不变
- OPStack 相关文件 — 不变

---

## 12. CTest 集成

### 12.1 现有 target 不变，新增 CTest 注册

```cmake
# 重构后的 blockchain smoke（替换原 EthEestBlockchainSmoke）
# Phase 2 过渡期：cancun --limit 10；M1 完成后（Phase 4）切到 curated manifest（§15.3），
# 避免 --limit 依赖文件名排序导致门禁漂移。
add_test(NAME EthEestBlockchainSmoke COMMAND EthEestBlockchainRunner
    --manifest ${CMAKE_CURRENT_SOURCE_DIR}/manifests/eth/eth-eest-blockchain-smoke.json
    --eest-root ${EVM_REF_EEST_ROOT})
set_tests_properties(EthEestBlockchainSmoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel;eest")

# 新增：blockchain granular（per-file GTest）
add_test(NAME EthEestBlockGranularSmoke COMMAND EthEestBlockGranular
    ${EVM_REF_EEST_ROOT}/fixtures/blockchain_tests
    --gtest_filter="*Cancun*")
set_tests_properties(EthEestBlockGranularSmoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel;eest")

# 新增：blockchain full（nightly —— 后期扫全量）
add_test(NAME EthEestBlockchainFull COMMAND EthEestBlockchainRunner
    --fixtures ${EVM_REF_EEST_ROOT}/fixtures/blockchain_tests)
set_tests_properties(EthEestBlockchainFull PROPERTIES
    LABELS "specs-tests;specs-tests-full;eth-kernel;eest;nightly")
```

### 12.2 运行方式

```bash
# Smoke（PR CI）
ctest -L 'specs-tests-smoke' -R 'blockchain' --test-dir build-ref -C Debug

# Full sweep（nightly）
ctest -L 'specs-tests-full' -R 'blockchain' --test-dir build-ref -C Debug
```

---

## 13. 与现有代码的共存策略

1. **不删除任何现有文件** — `EthEestBlockchainRunner.cpp` 重构而非重写
2. **不修改现有 `BlockTransition.h` 的公共 API** — 只扩展内部字段，已有调用方不受影响
3. **不修改 `EthGSTSmoke` / `EthExecutionSpecStateTests`** — State test 路径完全不变
4. **不修改 OPStack 路径** — `OpStackEestBlockchainRunner` 保持原样
5. **新增 Loader 和 Validation 层可独立编译和测试** — 后续可加 Loader 单元测试（对标 `GeneralStateTestLoaderTest`）

---

## 14. 风险和缓解

| 风险 | 缓解措施 |
|------|---------|
| JSON fixture 格式差异（legacy 格式 vs EEST 格式） | `BlockchainTestLoader` 支持两种格式，优先 EEST，legacy 作为 fallback |
| 规范链选择在简单测试中不走（total_difficulty=0） | PoS fixture 下 `>=` 等效"选输入顺序最后一个有效块"（§8.4）；PoW reorg 归 M5 |
| **含 withdrawal 的 Shanghai+ 块 stateRoot mismatch**（未 credit 余额） | **B1**：`applyEthBlock` 末尾按 §10.3 对 withdrawal 做 `+amount×10⁹` Wei 增记 |
| **Prague+ requestsHash 无采集来源** | **B2**：§8.3.1 系统合约采集；未打通则 XFAIL 降级，不阻塞 M1 |
| Blob gas 参数依赖 EIP 版本 | `BlobSchedule` 支持配置，通过 `ForkProfileRegistry` 查表获取 |
| `applyEthBlock` 目前为单 block 逐 tx 循环，不支持 MPT root 中间计算 | 扩展 `BlockTransition.h` 中的 `BlockApplyResult` |
| ETH 测试中 ommers 字段不存在于 EEST 格式 | `parseBlockHeader` 设默认值 `ommers.empty()` |

---

## 15. 验收标准（B + C）

### 15.1 CI 硬门禁（策略 C — 优先）

| 门禁 | Target | 通过条件 | 标签 |
|------|--------|----------|------|
| PR smoke | `EthEestBlockchainSmoke` | curated manifest 或 `cancun --limit 10` **全部 PASS**；无 crash | `specs-tests-smoke` |
| PR smoke | `EthEestBlockGranularSmoke` | Cancun filter 可运行；per-file pass/fail 可解析 | `specs-tests-smoke` |
| Nightly full | `EthEestBlockchainFull` | 扫描 `blockchain_tests/` **无 segfault/未捕获 exception** | `specs-tests-full;nightly` |
| Nightly full | `EthEestBlockGranularFull` | 同上（GTest 形态） | `specs-tests-full;nightly` |
| 回归 | 现有 state test targets | 全部保持 PASS（4140/4140 manifest 不退化） | `specs-tests-smoke` |

**CI 原则：** nightly full 的 **crash-free 是硬门禁**；全树通过率不作为 nightly 阻塞条件，仅记录 baseline 供 parity loop 追踪。

### 15.2 Fork 里程碑（策略 B — 分阶段达标）

| 阶段 | Fork / 目录 | 目标通过率 | 阻塞 CI？ |
|------|-------------|-----------|----------|
| M1 | `blockchain_tests/cancun/` | **≥ 90%** 标准格式用例 | ✅ PR smoke（manifest 选自 Cancun） |
| M2 | `blockchain_tests/prague/` | **≥ 90%** | 记录 baseline；smoke 可选扩 1 条 |
| M3 | `blockchain_tests/osaka/` | **≥ 90%** | 记录 baseline |
| M4 | `blockchain_tests/shanghai/` + `berlin/` + `london/` + `paris/` | **≥ 80%** | 记录 baseline |
| M5 | 历史 fork（`istanbul/` … `frontier/`）+ `static/` | 记录 baseline；不阻塞 | ❌ |

**M1 完成定义：** `validateBlock()` + MPT root 校验 + invalid block 三段检查在 Cancun corpus 上 ≥90%；`EthEestBlockGranular --gtest_filter=*cancun*` 与 CLI runner 结果一致。

### 15.3 可选 manifest（策略 C 辅助）

新增 `manifests/eth/eth-eest-blockchain-smoke.json`（~20–50 vectors，全部来自 Cancun M1 已通过用例）：

- PR CI 跑 manifest 而非 `--limit 10` 随机文件，保证门禁稳定
- Full tree 仍由 nightly granular/runner 扫描，不依赖 manifest 维护全量语料

### 15.4 非目标（本 spec 不阻塞）

- `blockchain_tests_engine*` / `blockchain_tests_sync` — Phase 5 单独 spec
- Engine-only 格式（无 `pre` + `genesisBlockHeader`）— 跳过计数，不计入通过率分母

---

## 16. 分阶段实施计划

| Phase | 名称 | 估时 | 产出 | 里程碑 |
|-------|------|------|------|--------|
| **0** | 脚手架 | 1–2d | `BlockchainTestTypes.h`、`BlockchainTestLoader` 骨架、`resolveRevision()` | Loader 单测可解析 Cancun fixture |
| **1** | 验证引擎 P0 | 3–4d | `BlockValidation.h`、`BlockApplyResult` 扩展、invalid block 三段检查 | Cancun invalid-block vectors PASS |
| **2** | MPT + Runner 重构 | 2–3d | `GstStateHash` 扩展（tx/receipts root）、`EthEestBlockchainRunner` 调用 Loader+Validation | Cancun 标准格式 **≥50%**（M1 中途） |
| **3** | P1 特性 + Granular | 2–3d | blob gas、withdrawal、EIP-7934；`EthEestBlockGranular` 执行 | Cancun **≥90%**（**M1 完成**） |
| **4** | Full + CI | 1–2d | `EthEestBlockchainFull`、`EthEestBlockGranularFull`、nightly workflow、`eth-eest-blockchain-smoke.json` manifest（PR 门禁切换） | CI 硬门禁上线；**M1 达成** |
| **4.x** | Fork parity loop | 持续 | 逐 fork 修失败、扩 baseline | **M2 Prague → M3 Osaka → M4 Shanghai/Berlin/London/Paris → M5 历史 fork** |
| **5** | Engine/Sync | deferred | 单独 spec | — |

**合计：** ~10–14 人天（Phase 0–4，达成 M1）。M2–M5 为 Phase 4.x 持续 parity loop，按 §15.2 里程碑逐步推进，不计入初始估时。

**里程碑 ↔ Phase 映射：** M1 = Phase 3 完成（Cancun ≥90%）+ Phase 4 门禁上线；M2–M5 = Phase 4.x（各 fork 依赖 B2 requests 采集 / B1 withdrawal credit 的完成度）。

---

## 17. Open Questions

1. **`applyEthBlock` 扩展**：Phase 1 扩展 `BlockApplyResult` 添加 `bloom`、`blob_gas_left`、`requests`、`requests_error`、`rejected`（§11）；Phase 3 追加 `withdrawals` 形参做 balance credit（§10.3）。`miningReward()` 按 §6.1 YAGNI 推迟至 M5，Phase 0–4 不实现。

2. ~~**MPT 实现复用**~~ — **已关闭**：`GstStateHash.cpp` 已含完整 RLP+MPT，`MptHash` 扩展该文件（§7.1），不引入 evmone。

3. **legacy `ethereum/tests` 格式**：当前仅支持 EEST 格式。**Phase 5** 考虑支持 legacy `BlockchainTests/`（与 engine/sync 同批，单独 spec）。

4. **`resolveRevision()` 新函数**：`ForkProfileRegistry` 现有 `findByUpstreamFork()` / `findByProfileId()`，均不接受 timestamp。Phase 0 新建 `resolveRevision(network, timestamp) -> evmc_revision`（内部查 `ForkProfile` 的 `RevisionConfig` + timestamp 过渡判定）。

5. **`Withdrawal` 类型**：FISCO 无此类型，Phase 0 在 `BlockchainTestTypes.h` 从零定义（§10.1）。

6. **`TransactionReceipt` 扩展影响面**：当前 `LogEntry log` + `int64_t gasUsed`（`BlockTransition.h:20-30`）。扩展为 `status` + `bloom` + `logs[]` + `gas_refund` 后，`EthBlockTransitionTest.cpp`（已确认存在）与 OPStack 路径需回归。缓解：新增字段带默认值，`log` 保留为 `logs.front()` 的兼容别名过渡，或一次性迁移调用点。

7. **evmone 复用（已决策）**：见头部「evmone 复用策略」——仅作参考规格，禁止链接 `evmone::testutils`/`evmone::state`。
