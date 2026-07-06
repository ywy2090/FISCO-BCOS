# EEST Blockchain Test Runner Parity — Design Spec

**Date:** 2026-07-03
**Status:** Draft — pending user review
**Scope:** 补齐 `eth-eest-test` 的 Blockchain Test Runner，达到与 evmone `blockchaintest_runner` 同等覆盖水平
**Architecture choice:** 路线 1 — 分层架构（Loader + Validation + Runner），测试专用类型

**Frozen decisions (brainstorming):**

| Item | Choice |
|------|--------|
| Scope | P0 + P1（区块验证核心 + Cancun/Shanghai/Osaka 新特性） |
| 数据模型 | **方案 B** — 新建测试专用的 `BlockchainTestTypes.h`，不动生产代码 |
| 代码组织 | **方案 B** — 分层（Loader + Validation + Runner），和 state test 侧一致 |
| 错误码 | **方案 A** — 新建测试专用 `BlockValidationErrors` 常量集 |
| 实现路线 | **路线 1** — 重构 Runner + 新建 Loader + 独立 Validation 层 |

**Related specs:**

- `2026-07-02-evmone-inspired-eth-gtest-design.md` — 其中 §3 Pillar 3 `EthEestBlockGranular` 由本 spec 实现
- `2026-06-30-gtest-state-block-eest-migration-design.md` — 父级测试迁移设计

**Key evmone reference files:**

- `test/blockchaintest/blockchaintest_runner.cpp` — `validate_block()` + `run_blockchain_tests()`
- `test/utils/blockchaintest.hpp` — `BlockHeader` / `TestBlock` / `BlockchainTest` 类型定义
- `test/utils/mpt_hash.hpp` — MPT hash 辅助函数
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
| P0 | `validate_block()` 区块头验证（10 项规则，不含 PoW/ommer） | `blockchaintest_runner.cpp:31-126` |
| P0 | 无效区块 + `expectedException` 匹配（三段检查） | `blockchaintest_runner.cpp:282-356` |
| P0 | 规范链选择 + `lastblockhash` 校验 | `blockchaintest_runner.cpp:358-371` |
| P0 | 区块头 MPT root 逐项校验（7 项字段） | `blockchaintest_runner.cpp:260-281` |
| P1 | Blob gas 验证（`calc_excess_blob_gas` + `compute_blob_gas_price`） | `blockchaintest_runner.cpp:93-116` |
| P1 | Withdrawal RLP 解析 + `withdrawal_root` 校验 | `blockchaintest_runner.cpp:263-267` |
| P1 | EIP-7934 RLP 区块大小限制（Osaka+） | `blockchaintest_runner.cpp:122-123` |

### 2.2 Non-Goals

- Ethash 难度计算（FISCO 不涉及 PoW，difficulty 检查跳过）
- 完整 ommers 处理（仅检查 Paris+ 无 ommers）
- Engine API sync 格式（payload RLP 解码，标记为 Phase 2）
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
    MptHash.h                    # ~60 L   tx/withdrawal/receipts MPT root 扩展
  runners/eth/
    EthEestBlockchainRunner.cpp  # ~300 L  重构（CLI standalone，输出到 std::cerr）
    EthEestBlockGranular.cpp     # ~100 L  重构（GTest dynamic registration）

**Runner 两种形态**（对标 evmone 同样有两种）：

| Runner | 对标 evmone | 断言方式 |
|--------|-----------|---------|
| `EthEestBlockchainRunner` | N/A（FISCO 独有，manifest/smoke 风格） | `std::cerr` + `std::exit(1)` |
| `EthEestBlockGranular` | `evmone-blockchaintest` | GTest `EXPECT_EQ`/`FAIL()`，支持 `--gtest_filter` |

两者复用同一套 `BlockValidation` + `BlockchainTestLoader`，仅在调度和输出方式上不同。
```

### 3.3 依赖关系

```
BlockchainTestTypes.h  ← 基础类型（TestBlockHeader, TestBlock, BlockchainTest, BlobSchedule）
    ↓（直接依赖）
┌─── BlockchainTestLoader.cpp  ← boost::property_tree（JSON 解析）
│    └── BlockchainTestLoader.h
├─── BlockValidation.h  ← ForkProfile（已有）
│    └── BlockValidationErrors.h  ← 错误码常量
├─── MptHash.h  ← GstStateHash（已有 state root/logs hash 可复用）
│
└───→ EthEestBlockchainRunner.cpp  ← BlockTransition.h（已有 applyEthBlock）
     EthEestBlockGranular.cpp     ← BlockValidation.h + BlockchainTestLoader.h + MptHash.h
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
// 在 §8.1 Genesis 验证中引用，实际值由 MptHash.h 提供

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

**evmone 的 `state::BlockInfo` 包含 `blob_gas_used`/`excess_blob_gas`/`withdrawals` 等字段，但 FISCO 的 `state::BlockInfo` 只有 9 个执行层字段（number, timestamp, gasLimit, coinbase, prevRandao, baseFee, chainId, blobBaseFee）。因此这些共识验证所需字段被安置在 `TestBlock`（input_* 前缀）和 `TestBlockHeader` 中，保持生产代码不变。**

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

/// 对标: evmone mining_reward() — Phase 2（区块 reward/finalize 重构时启用）
/// 当前 evmone 值: <BYZANTIUM=5ETH, <CONSTANTINOPLE=3ETH, <PARIS=2ETH, >=PARIS=0
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

### 7.1 文件：`helpers/MptHash.h`（~80 L，或扩展现有 `GstStateHash.h`）

对标 evmone `test/utils/mpt_hash.hpp`。当前 FISCO 已有 `computeStateRoot()` 和 `computeLogsHash()`，需要新增：

```cpp
namespace bcos::evm::reference_tests {

// 对标 evmone: mpt_hash(transactions)
h256 computeTxRoot(std::span<const state::Transaction> txs);

// 对标 evmone: mpt_hash(receipts)
h256 computeReceiptsRoot(std::span<const TransactionReceipt> receipts);

// 对标 evmone: mpt_hash(withdrawals)
// Withdrawal 类型定义见 §10.1
h256 computeWithdrawalRoot(std::span<const Withdrawal> withdrawals);

// 对标 evmone: calculate_requests_hash()
// RequestsList = std::vector<Requests> where Requests is the serialized EIP-7685 request type
// （SHA256，非 MPT；具体类型在 BlockchainTestTypes.h 中定义）
h256 computeRequestsHash(std::span<const bytes> requests);

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
    EXPECT_EQ(test.genesis_block_header.block_number, 0);
    EXPECT_EQ(test.genesis_block_header.gas_used, 0);
    EXPECT_EQ(test.genesis_block_header.transactions_root, EMPTY_MPT_HASH);
    EXPECT_EQ(test.genesis_block_header.receipts_root, EMPTY_MPT_HASH);
    // withdrawal_root: Shanghai+ 则为 EMPTY_MPT_HASH，否则为 0
    if (rev_schedule.get_revision(test.genesis_block_header.timestamp) >= EVMC_SHANGHAI)
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
    auto* canonical_state = &test.pre_state;
    h256 canonical_tip = genesis.hash;

    // ── 3. 逐块循环 ──
    for (auto& tb : test.test_blocks) {
        auto rev = resolveRevision(test.network, tb.block_info.timestamp);
        auto blob_gas_limit = maxBlobGasPerBlock(test.blob_schedule);

        // 3a. 查找父区块数据（对标 evmone L206-210）
        auto parent_it = block_data.find(tb.block_info.parent_hash);  // 需在 BlockInfo 解析时填入
        auto* parent_header = (parent_it != block_data.end()) ? parent_it->second.header : nullptr;

        // 3b. 区块头验证
        auto block_error = validateBlock(rev, test.blob_schedule, tb, parent_header);

        if (!tb.expected_exception.empty()) {
            // ── 无效区块路径 ──
            if (block_error) {
                EXPECT_TRUE(tb.expected_exception.contains(*block_error));
                continue;  // 正确拒绝
            }
            // 执行交易 → 检查交易级错误 → 检查区块头字段不一致
            auto res = applyEthBlock(pre_state, vm, bi, ...);
            // ... （三段检查见 §8.2）
            // 如果所有字段都匹配 → FAIL（预期无效但实际有效）
        } else {
            // ── 有效区块路径 ──
            EXPECT_FALSE(block_error);  // 不应该被 validateBlock 拒绝

            // 使用父区块的 post_state 作为执行起点（对标 evmone L230-231）
            const auto& pre_state = parent_data_it->second.post_state;
            auto res = applyEthBlock(pre_state, vm, bi, ...);
            // 逐项校验区块头字段（见 §8.3）

            block_hashes[tb.expected_block_header.block_number] = tb.expected_block_header.hash;
            // 更新 block_data, canonical_state, canonical_tip
        }
    }

    // ── 4. 最终验证 ──
    EXPECT_EQ(canonical_tip, test.expectation.last_block_hash);
    EXPECT_EQ(computeStateRoot(finalState), expectedPostHash);
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

EXPECT_EQ(computeStateRoot(res.block_state), expected_header.state_root);
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

EXPECT_EQ(res.gas_used, expected_header.gas_used);
EXPECT_EQ(res.bloom, expected_header.logs_bloom);

// Blob gas 一致性（Cancun+）
EXPECT_EQ(blob_gas_limit - res.blob_gas_left,
          static_cast<int64_t>(expected_header.blob_gas_used.value_or(0)));
```

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

// 循环结束后：
EXPECT_EQ(canonical_tip, test.expectation.last_block_hash);
EXPECT_EQ(computeStateRoot(*canonical_state), expectedPostHash);
```

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

---

## 11. 对现有文件的修改

| 文件 | 修改类型 | 内容 |
|------|---------|------|
| `CMakeLists.txt` | 修改 | 新增 `BlockchainTestLoader.cpp` 源文件 + 新增 `BlockchainTestTypes.h` 头文件 |
| `helpers/BlockTransition.h` | 修改 | 扩展 `TransactionReceipt` 字段（增加 bloom, logs[]，替换单 log）; 扩展 `BlockApplyResult` 字段（bloom, blob_gas_left, requests, requests_error, rejected） |
| `helpers/GstStateHash.h` | 修改/新增 | 新增 `computeTxRoot()` / `computeReceiptsRoot()` / `computeWithdrawalRoot()` / `computeRequestsHash()` |
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
# 使用 blockchain_tests/cancun 子目录对应 §15 验收标准
add_test(NAME EthEestBlockchainSmoke COMMAND EthEestBlockchainRunner
    --fixtures ${EVM_REF_EEST_ROOT}/fixtures/blockchain_tests/cancun --limit 10)
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
| 规范链选择在简单测试中不走（total_difficulty=0） | runner 保持 evmone 的 `>=` 逻辑，默认选择最后一个有效区块 |
| Blob gas 参数依赖 EIP 版本 | `BlobSchedule` 支持配置，通过 `ForkProfileRegistry` 查表获取 |
| `applyEthBlock` 目前为单 block 逐 tx 循环，不支持 MPT root 中间计算 | 扩展 `BlockTransition.h` 中的 `BlockApplyResult` |
| ETH 测试中 ommers 字段不存在于 EEST 格式 | `parseBlockHeader` 设默认值 `ommers.empty()` |

---

## 15. 验收标准

1. **Smoke**：`EthEestBlockchainSmoke` 在 `blockchain_tests/cancun` 下至少有 5 个测试通过（当前 0 个有意义的结构化比较）
2. **Granular**：`EthEestBlockGranularSmoke` 可以按 `--gtest_filter` 按分叉筛选并输出 per-file pass/fail
3. **全量**：`EthEestBlockchainFull` 扫描 `blockchain_tests/` 全目录无 segfault/exception 崩溃
4. **现有测试不受影响**：所有 `specs-tests-smoke` 中的 state test target 全部通过

---

## 16. Open Questions

1. **`applyEthBlock` 扩展**：当前 `BlockTransition.h` 的 `applyEthBlock()` 只做逐 tx 执行 + state 累积。Phase 1 必须扩展 `BlockApplyResult` 添加 `bloom`、`blob_gas_left`、`requests`、`rejected` 字段（§11）。`miningReward()` Phase 1 仅声明不调用；Phase 2 重构 `apply_block()` 时启用（evmone reward 值: <BYZANTIUM=5ETH, <CONSTANTINOPLE=3ETH, <PARIS=2ETH, >=PARIS=0）。

2. **MPT 实现**：`computeTxRoot()` / `computeReceiptsRoot()` 需要 RLP 编码 + Keccak-256 MPT。目前 `GstStateHash.cpp` 已有 MPT 实现，评估其是否可直接复用。

3. **legacy `ethereum/tests` 格式**：当前仅支持 EEST 格式。Phase 2 考虑支持 legacy `BlockchainTests/`。

4. **`resolveRevision()` 新函数**：当前 `ForkProfileRegistry` 只有 `findByUpstreamFork()` 和 `findByProfileId()`，都不接受 timestamp 参数。需新建 `resolveRevision(network, timestamp) -> evmc_revision`。

5. **`Withdrawal` 类型**：FISCO 代码库中不存在此类型，需在 `BlockchainTestTypes.h` 中从零定义（§10.1）。

6. **`TransactionReceipt` 扩展影响面**：当前仅有 `LogEntry log` + `int64_t gasUsed`。扩展为 `status` + `bloom` + `logs[]` + `gas_refund` 后，需评估对 `EthBlockTransitionTest.cpp` 和 OPStack 路径的影响。
