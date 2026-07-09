# M3 报告：bcos-evm-ref EEST blockchain 级对照 harness

## 1. 移植对照

源：evmone REF `3585c2cb`（vcpkg 锁定 fork，= 上游 v0.21.0 后第 3 个提交，仅 SM3 补丁）：
- `test/blockchaintest/blockchaintest_runner.cpp`（422 行，`git show 3585c2cb:...` 全文读过）
- `test/utils/blockchaintest.hpp`（`BlockchainTest`/`TestBlock`/`BlockHeader` 数据结构）

对照文件：`bcos-evm-ref/test/eth/EestBlockchainTest.cpp`。

### 1.1 逐段照抄的部分

| 上游函数/段落 | 本文件对应 | 说明 |
|---|---|---|
| `validate_block()` 头校验主体 | `validateBlock()` | gasLimit ±1/1024 与 ≥5000、gas_used ≤ gas_limit、块号连续、timestamp 单调、extra_data ≤32、EIP-1559 base fee 重算、EIP-4844/7918 excess_blob_gas 重算+blob gas 上限、withdrawals 解析失败判无效、EIP-7934 `MAX_RLP_BLOCK_SIZE`（用 loader 给的 `rlp_size`，不解码 RLP）逐条照抄 |
| `apply_block()` 主干 | `applyBlock()` | 逐块 `system_call_block_start` → 逐笔交易 → requests 收集（EIP-6110 deposits + `system_call_block_end`）→ 收尾 → `compute_bloom_filter`，结构与字段名（`rejected`/`requests`/`gas_used`/`bloom`/`blob_gas_left`/`block_state`）一一对应 |
| `run_blockchain_tests()` 主循环 | `runBlockchainTest()` | genesis 头不变式断言、`TestBlockHashes` 登记、`block_data`（本文件 `BlockData`）按 block hash 建 map、`total_difficulty` 累计、`canonical_state` 指针追踪、四 root（state/transactions/receipts/withdrawals）+ `requests_hash` + `gas_used` + logsBloom 判据、invalid block 的"必须被拒绝"反向判据、末尾 `last_block_hash`/`post_state`（`variant<TestState, hash256>`）两种形态 | 逐行对照，仅在下表列出的点做了范围裁剪 |
| `RevisionSchedule`/`to_rev_schedule` | 直接复用 `evmone::test` 内已导出的实现 | 过渡 fork（如 `CancunToPragueAtTime15k`）按块 timestamp 取 revision，成本为零 |

### 1.2 按 spec §7.1 "M3 范围决策" 裁剪的部分（附理由）

| 上游内容 | 处理 | 理由 |
|---|---|---|
| `calculate_difficulty()`（pre-Paris ethash 难度调整） | 不移植；`validateBlock()` 直接断言 `difficulty == 0` | M3 范围恒为 genesis_rev ≥ Cancun，即 rev 恒 ≥ EVMC_PARIS；EIP-3675 后区块难度恒为 0，与 `calculate_difficulty(rev≥PARIS)` 的返回值语义等价 |
| `mining_reward()`（pre-Paris 区块奖励表） | 不移植；`applyBlock()` 调 `runBlockFinalize` 时把 block reward 硬编码为 `std::nullopt` | Cancun+ 下 `mining_reward()` 恒返回 `nullopt`（post-merge 无区块奖励） |
| `parent_has_ommers` 簿记（喂给 `calculate_difficulty`） | 不移植 | 与难度校验同源被排除；`validateBlock()` 仍保留"post-merge 区块 ommers 必须为空"这条独立校验（不依赖 `parent_has_ommers`） |
| ommer delta 范围校验（1..6） | 不移植 | 该校验是难度/奖励机制的一部分，post-merge ommers 恒空后这段代码路径不可达 |
| `print_state()` 调试函数 | 不移植 | 纯调试辅助，判据不依赖 |
| `blockchain_tests_engine*`/`blockchain_tests_sync` 目录 | 不遍历，只扫 `blockchain_tests/` | spec §7.1 明确排除 |
| `genesis_rev < EVMC_CANCUN` 的整个 test | 在 `runFixtureFile()` 里逐 test case 跳过（计入 `skipped_cases`），不是逐块跳过 | 过渡 fork 的起点早于 Cancun 时，转变前的区块落在 M3 目标 fork（Cancun+）之外；整条 test 跳过而非逐块跳过，避免"部分执行部分跳过"污染侧链追踪状态 |

### 1.3 与上游的关键架构差异（非裁剪，是 bcos-evm-ref 的既定契约）

上游 `apply_block()` 直接调用 `test::transition()`，原地改写 `TestState&`。本模块的 `EthTransition.h` 契约规定 `bcos::evmref::eth::runTransaction` **不写回状态**（返回 `variant<TransactionReceipt, error_code>`，调用方需显式 `applyStateDiff`）。因此 `applyBlock()` 在每笔交易成功后插入了一行 `bcos::evmref::applyStateDiff(blockState, receipt.state_diff)`，`runBlockFinalize`（withdrawals 收尾）同理。`system_call_block_start`/`system_call_block_end`（EIP-4788/2935/系统合约）没有 bcos 包装层，直接调用 evmone 原始的 `test::system_call_block_start`/`system_call_block_end`（这两个函数本就原地改写 `TestState&`，不在"不写回"契约的覆盖范围内）。

## 2. 运行结果

环境：`EVM_REF_EEST_ROOT=$PWD/build-bcos-evm-check/_deps/evm_ref_eest_fixtures-src`（`blockchain_tests/` 下 2848 个 fixture，3.2GB）。

### 2.1 Smoke（`EestBlockchain.Smoke`，进 ctest 常驻）

范围：`blockchain_tests/cancun/` 下按路径排序取前 20 个文件。

```
EEST blockchain (smoke): files=20 skipped_cases=0 unsupported_files=0 failed_files=0
```
耗时：~2.9–3.6s（含一次性 evmc VM 创建开销）。

### 2.2 Full（`EestBlockchain.Full`，默认 GTEST_SKIP，需 `EVM_REF_EEST_BLOCKCHAIN_FULL=1`）

范围：`blockchain_tests/` 全量 2848 个 fixture 文件。

```
EEST blockchain: unsupported feature in .../cancun/eip4844_blobs/test_reject_valid_full_blob_in_block_rlp.json: tests with invalidly rlp-encoded blocks are not supported
EEST blockchain: unsupported feature in .../cancun/eip4844_blobs/test_invalid_blob_tx_contract_creation.json: tests with invalidly rlp-encoded blocks are not supported
EEST blockchain (full): files=2848 skipped_cases=8947 unsupported_files=2 failed_files=0
```
耗时：60953 ms（约 61 秒，远低于预估的"夜跑级"——远小于给定的 40 分钟上限）。

**结论：0 failed_files。** 2848 个 fixture 文件中，2846 个被完整加载并执行，其中 8947 个具名 test case（跨全部 fixture 文件累加）因 `genesis_rev < EVMC_CANCUN` 被逐 case 跳过（如 berlin/byzantium/frontier/homestead/istanbul/london/paris/shanghai/constantinople/static 目录下的绝大部分 fixture，以及部分过渡 fork 中早于 Cancun 的起始 revision），Cancun+ 范围内的全部 case 逐块四 root（state/transactions/receipts/withdrawals）+ requests_hash + gas_used + logsBloom 判据、侧链追踪、canonical 分支选择、末尾 post_state 比对均通过；2 个文件因 loader 主动声明"不支持"而未纳入判据（见 §3）。

## 3. 未支持样本清单（2 个，均为同一类，非"失败"）

| 文件 | 异常类型 | 消息 | 归类 |
|---|---|---|---|
| `blockchain_tests/cancun/eip4844_blobs/test_reject_valid_full_blob_in_block_rlp.json` | `evmone::test::UnsupportedTestFeature` | `tests with invalidly rlp-encoded blocks are not supported` | RLP 解码路径（loader 内建限制） |
| `blockchain_tests/cancun/eip4844_blobs/test_invalid_blob_tx_contract_creation.json` | 同上 | 同上 | 同上 |

归因：这两个 fixture 需要 loader 对"故意构造的无效 RLP 字节流"做原始 RLP 解码/校验才能重建 `TestBlock`，evmone 自身的 loader（甚至上游官方 harness）就主动拒绝支持这条路径（抛 `UnsupportedTestFeature`），与 bcos-evm-ref 设计文档 §1.2 "区块 RLP 解码路径（EEST blockchain fixture 的 `rlp` 字段）不做，只消费 JSON 展开字段" 完全一致——不是本模块新增的缺口，而是复用的上游 loader 本就没有实现这条能力。全量扫描（2848 个文件逐个用最小复现程序单独加载）确认：**只有这 2 个文件**触发该异常，未发现其它异常类型。

### 3.1 一个 ABI 细节（记录以防后续踩坑）

`evmone::test::UnsupportedTestFeature : std::runtime_error` 是完全内联定义在 vcpkg 安装头文件（`blockchaintest.hpp`）里的类。实测发现：在本工具链（Apple clang、`-std=gnu++20`、`-O3 -DNDEBUG`）下，跨"vcpkg 静态库对象文件"与"消费侧对象文件"两个编译单元边界，`catch (const std::exception&)` **有时不命中**这个从库内抛出的异常（尽管它确实是 `std::exception` 的公开派生类）——但按其**精确类型** `catch (const evmone::test::UnsupportedTestFeature&)` 总能命中。用一个独立最小复现程序（同样链接 `evmone.testutils`/`evmone-state`/`evmone`/`evmone_precompiles`/`blst`）交叉验证：`nm -m` 显示该类型的 typeinfo/vtable 符号在库内被编码为 `weak private external`（弱符号+隐藏可见性），推测是消费侧从未在自己的编译单元里实例化过这个类型（没有一份本地弱符号副本参与链接期折叠），导致基类 `std::exception`/`std::runtime_error` 链路的 RTTI 匹配未能可靠工作，具体机制未继续深挖（不影响交付：全量扫描确认加载阶段只有这一种异常类型，已按精确类型捕获+防御性 `catch(...)` 兜底处理，详见 `EestBlockchainTest.cpp` 内 `runFixtureFile()` 的注释）。

## 4. Oracle 对照表（本模块 vs bcos-evm 的 405 个失败）

### 4.1 bcos-evm 405 失败的原始出处（核实结果）

冻结基线日志：`.superpowers/sdd/blockchain-full-head.log`（`.superpowers/sdd/progress.md:47-49,68` 引用为 Task 3 回归证据，commit 区间 `bd959d0..9b5070b`）：
```
Results: 47288 passed, 405 failed, 0 skipped (2848 files)
```
逐行核实该日志：`receiptsRoot` 失败 365 条、`stateRoot` 失败 40 条，**365 + 40 = 405**——设计文档 §7.0（"356 receiptsRoot + 37 stateRoot"）与实际日志有出入（356+37=393 ≠ 405），`progress.md` 里 "356+"/"37+" 的模糊写法是不精确复述；本报告以 `blockchain-full-head.log` 的精确切分（365/40）为准。

### 4.2 关键发现：405 个失败全部在 pre-Cancun（frontier/homestead）范围内

逐条核实 `blockchain-full-head.log` 里的 `FAIL tests/...` 路径，聚类结果：

| 目录 | 失败数 | 备注 |
|---|---|---|
| `tests/frontier/opcodes` | 300 | 以 `test_stack_overflow.json`（190）为主，覆盖 PUSH1–PUSH32 在 legacy fork 下的栈高 1025 用例 |
| `tests/frontier/precompiles` | 58 | |
| `tests/frontier/scenarios` | 34 | 按 opcode 参数化的场景用例，跨 Frontier/Homestead/Byzantium/Istanbul/London |
| `tests/frontier/create` | 7 | |
| `tests/frontier/validation` | 3 | |
| `homestead/coverage` | 1 | |
| `frontier/touch` | 1 | |
| `frontier/examples` | 1 | |

**没有一条落在 Cancun/Prague/Osaka 的 EIP 目录**（`eip4844_blobs`/`eip1153_tstore`/`eip7702` 等）——`bcos-evm/test/eth-eest-test/reports/eest-granular-full-failures.md` 逐目录列出的 Cancun+ EIP 子目录全部是 0 失败。日志另指出这 405 个失败 **全部是区块头 root 计算不一致**（receiptsRoot/stateRoot trie 根不匹配），**账户级 postState/state-diff 比对全部通过**（"ZERO assertPostState failures"）——即最终账户状态（余额/存储/代码）是对的，只是 trie 根编码环节有偏差。

（供参考：更新的运行 `bcos-evm/test/eth-eest-test/reports/a1-fulltree-blockchain-2026070*-summary.txt` 显示该数字已降至 354（345 receiptsRoot + 9 stateRoot，2814 文件），说明 405 基线之后 bcos-evm 一直在对这批 legacy fork 问题做增量修复，与本次 M3 无关，仅供背景参考。）

### 4.3 M3 与 405 基线的诚实对照结论

**M3 的 0/2848 failed_files 不能直接"解释掉"或"驳倒"那 405 个失败**，原因是范围不重叠：

- bcos-evm-ref 依 spec §7.1 的既定范围决策，**只执行 `genesis_rev ≥ EVMC_CANCUN` 的 test case**；本次 Full 运行里 8947 个 test case 因 `genesis_rev < EVMC_CANCUN` 被整体跳过（`skipped_cases=8947`），这批跳过的 case 正好覆盖 bcos-evm 405 个失败所在的 frontier/homestead 范围。
- bcos-evm 自己的 Cancun+ EIP 目录本来就是 0 失败（`eest-granular-full-failures.md`）——也就是说，**在 M3 与 bcos-evm 重叠的范围（Cancun+）里，两边都是干净的**：本次 M3 用独立复用的 `evmone::state` 内核重新验证了一遍 Cancun+ 全量四 root + requests_hash 判据，结果与 bcos-evm 自报的 Cancun+ 0 失败一致，是**独立确认**而非新证据。
- 405 个失败真正落在的 frontier/homestead 范围，**M3 完全没有触达**（不在 harness 执行范围内，只在 loader 层面被跳过计数），因此无法用本次结果判断这批 legacy fork 问题是否也存在于 `evmone::state` 内核，或是否为 bcos-evm 手写内核独有的偏差。

**对 §7.2 决策点的实际意义**：M3 证明的是"Cancun+ 目标范围内 `evmone::state` 与 bcos-evm 手写内核战平（两者皆 0 失败）"，而不是"`evmone::state` 修复了 bcos-evm 那 405 个已知失败"。405 个失败的真实归因（是否为纯 trie 根编码 bug、是否 bcos-evm 独有）需要另外扩大 M3/M3.5 范围到 pre-Cancun fork 才能验证——这在当前 spec §7.1 的范围决策之外，留给决策点或后续里程碑判断是否值得补做。

## 5. 提交

提交 hash：`<见下方最终返回消息>`（`test(bcos-evm-ref): M3 EEST blockchain harness (four roots + requests, side-chain tracking, transition forks)`）。
