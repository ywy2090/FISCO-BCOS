# Task 3 报告：M1 — StateDiffWriteback 与 deleted_accounts 语义（TDD）

工作目录：`/Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor`
分支：`feat-evm-opstack-on-evmone`

## 前置核实

在动手前核实了依赖头文件与 brief 假设是否一致（均一致）：

- `bcos-evm-ref/build/vcpkg_installed/arm64-osx/include/test/utils/test_state.hpp`：`TestAccount{nonce, balance, storage, code}`，`TestState : StateView, map<address,TestAccount>`，含 `void apply(const state::StateDiff&)`。
- `.../test/state/state_diff.hpp`：确认了 brief 所述"过时注释"——`deleted_accounts` 字段注释写着"Note that from the Cancun revision ... this list is always empty"，但 `apply()` 实码（见下）确实会处理该字段，印证 brief 判断。
- `.../test/utils/test_state.cpp`（源仓库 `blockchain-impl/evmone/test/utils/test_state.cpp:30-49`）：`TestState::apply` 逐条应用 `modified_accounts`（`modified_storage` 值为 0 时 `erase`，否则 `insert_or_assign`；`code` 仅 `has_value()` 时覆盖），随后对 `deleted_accounts` 逐个 `erase(addr)`——与 brief 描述的契约完全一致。
- `.../test/state/state.hpp`：`state::transition(...)` 返回值是 `TransactionReceipt`（非 variant），`validate_transaction(...)` 返回 `variant<TransactionProperties, error_code>`——与 brief Step1 测试代码里的 `run()` 辅助函数签名匹配。
- `.../test/state/host.hpp`：`compute_create_address(sender, nonce)` 存在，签名匹配。

## 逐 Step 执行记录

### Step 1: 写失败测试

按 brief 逐字创建 `bcos-evm-ref/test/eth/StateDiffWritebackTest.cpp`（4 个 TEST：`ContractDeletesListedAccount`、`ContractStorageZeroMeansErase`、`DeletesSameTxSelfdestruct`、`ErasesTouchedEmptyAccount`）。

### Step 2: 追加 CMakeLists.txt，确认编译失败

`bcos-evm-ref/test/CMakeLists.txt` 的 `add_executable` 源列表追加 `eth/StateDiffWritebackTest.cpp`。

```
$ cmake --build bcos-evm-ref/build 2>&1 | tail -5
[ 40%] Built target bcos-evm-ref-eth
[ 60%] Building CXX object test/CMakeFiles/bcos-evm-ref-eth-tests.dir/eth/StateDiffWritebackTest.cpp.o
/…/bcos-evm-ref/test/eth/StateDiffWritebackTest.cpp:1:10: fatal error: 'bcos-evm-ref/adapter/StateDiffWriteback.h' file not found
    1 | #include <bcos-evm-ref/adapter/StateDiffWriteback.h>
      |          ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
1 error generated.
```

与 brief 预期完全一致：FAIL — `'bcos-evm-ref/adapter/StateDiffWriteback.h' file not found`。

### Step 3: 空转占位实现

按 brief 逐字创建：
- `bcos-evm-ref/adapter/StateDiffWriteback.h`（`applyStateDiff` 空转，`(void)state; (void)diff;`）
- `bcos-evm-ref/adapter/StateViewAdapter.h`（`StateView`/`BlockHashes` 别名）

编译该占位版本时发现一个**brief 未预期的编译期问题**（见下节"发现"），已就地修复后本 Step 才算完成——"编译通过"这一半在修复后达成。

### 发现：`_u256` 数字分隔符字面量在本仓库锁定的 intx 0.15.0 下编译失败

Step 1 提供的测试代码中 `1'000'000'000'000'000'000_u256`（两处，`DeletesSameTxSelfdestruct` 与 `ErasesTouchedEmptyAccount`）编译报错：

```
error: call to consteval function 'intx::literals::operator""_u256' is not a constant expression
note: non-constexpr function 'throw_<std::invalid_argument>' cannot be used in a constant expression
note: in call to 'from_dec_digit(39)'
```

根因：`bcos-evm-ref/build/vcpkg_installed/arm64-osx/include/intx/intx.hpp:802` 的 `from_dec_digit(char c)` 只接受 `'0'`~`'9'`，不识别数字分隔符 `'`（ASCII 39）；`from_string` 在 consteval 上下文里遇到分隔符字符直接抛出并让整个字面量非常量表达式。这是本仓库 vcpkg overlay port 锁定的 intx 0.15.0（`bcos-evm-ref/ports/intx` 版本字符串）的限制，与 brief 的语义假设（`state::transition`/`StateDiff`/`TestState::apply` 等）无关，纯粹是数字分隔符语法在这个 intx 版本下不受支持（检索过本地 evmone 源码 `test/state/*.cpp`，未发现任何 `_u256` 字面量使用分隔符的先例，仅有普通 `int` 字面量如 `30'000'000` 使用分隔符——那是编译器内建的数字分隔符处理，不经过 `intx::from_string`）。

处理方式：仅去除分隔符（`1000000000000000000_u256`），数值不变（仍为 1e18 wei / 1 ETH），**未改动任何断言、未改动任何测试语义**。已在代码中就地加注释说明原因。判定：这是纯语法/版本兼容性问题，不属于"为了凑绿而改断言"范畴，故直接修复而非停下等待决策。

### Step 4: 跑测试，观察真实断言红（占位实现下）

```
$ cmake --build bcos-evm-ref/build 2>&1 | tail -5
[100%] Built target bcos-evm-ref-eth-tests   （无警告无错误，仅 linker 一条 duplicate libraries 警告）

$ ctest --test-dir bcos-evm-ref/build --output-on-failure 2>&1 | tail -60
Test project …/bcos-evm-ref/build
    Start 1: BcosEvmRefEthTests
1/1 Test #1: BcosEvmRefEthTests ...............***Failed    0.77 sec
[==========] Running 5 tests from 2 test suites.
[----------] 1 test from Skeleton
[ RUN      ] Skeleton.HeadersAndLinkOk
[       OK ] Skeleton.HeadersAndLinkOk (0 ms)
[----------] 4 tests from StateDiffWriteback
[ RUN      ] StateDiffWriteback.ContractDeletesListedAccount
/…/StateDiffWritebackTest.cpp:71: Failure
Expected equality of these values:
  state.count(victim)
    Which is: 1
  0u
    Which is: 0
[  FAILED  ] StateDiffWriteback.ContractDeletesListedAccount (0 ms)
[ RUN      ] StateDiffWriteback.ContractStorageZeroMeansErase
unknown file: Failure
Unknown C++ exception thrown in the test body.
[  FAILED  ] StateDiffWriteback.ContractStorageZeroMeansErase (0 ms)
[ RUN      ] StateDiffWriteback.DeletesSameTxSelfdestruct
[       OK ] StateDiffWriteback.DeletesSameTxSelfdestruct (0 ms)
[ RUN      ] StateDiffWriteback.ErasesTouchedEmptyAccount
/…/StateDiffWritebackTest.cpp:148: Failure
Expected equality of these values:
  pre.count(empty)
    Which is: 1
  0u
    Which is: 0
[  FAILED  ] StateDiffWriteback.ErasesTouchedEmptyAccount (0 ms)
[  PASSED  ] 2 tests.
[  FAILED  ] 3 tests, listed below:
[  FAILED  ] StateDiffWriteback.ContractDeletesListedAccount
[  FAILED  ] StateDiffWriteback.ContractStorageZeroMeansErase
[  FAILED  ] StateDiffWriteback.ErasesTouchedEmptyAccount
 3 FAILED TESTS
```

**实际观察：3 红 2 绿，与 brief 预期完全一致**，逐条对照：

| 用例 | 预期 | 实际 | 备注 |
|---|---|---|---|
| `ContractDeletesListedAccount` | 红：空转不删除 | 红 | `EXPECT_EQ(state.count(victim), 0u)` 失败，实际仍为 1——空转确实没删除，符合预期 |
| `ContractStorageZeroMeansErase` | 红：空转不写 storage | 红，但**表现形式与 brief 字面描述略有出入** | 失败信息是 `Unknown C++ exception thrown in the test body`，而非一条 `EXPECT_EQ` 不等失败。原因：空转下 `k1` 从未被写入 `storage`，断言代码 `state.at(acct).storage.at(k1)` 直接抛 `std::out_of_range`（`.at()` 语义），导致测试在到达 `EXPECT_EQ` 之前就因未捕获异常而失败退出。这仍是"空转不写 storage"这一真实缺陷的忠实反映，只是失败的具体呈现方式是异常而非断言不等——已如实记录，未修改断言或测试逻辑 |
| `DeletesSameTxSelfdestruct` | 绿：验证的是 evmone 产出的 diff 内容，apply 对空转恰为平凡真 | 绿 | 与 brief 分析一致：该用例真正断言的是 `receipt->state_diff.deleted_accounts` 的内容（evmone 真实产出），随后对空转 `applyStateDiff` 的断言 `pre.count(...) == 0` 因为 pre 里那个地址原本就不存在（`count` 恒为 0）而对空转恰好平凡为真 |
| `ErasesTouchedEmptyAccount` | 红：空转不删除 | 红 | `EXPECT_EQ(pre.count(empty), 0u)` 失败，实际仍为 1，符合预期 |
| `Skeleton.HeadersAndLinkOk` | 绿 | 绿 | 未受影响 |

结论：红/绿分布与 brief 预期一致（3 红 2 绿），无需停下报告"不符"情形；唯一值得记录的细节是 `ContractStorageZeroMeansErase` 的红以未捕获异常形式出现而非纯断言失败，已如上说明，语义判定仍以"空转确实没有正确处理 storage 写入/删除"为准，与 brief 一致。

### Step 5: 换真实现

`bcos-evm-ref/adapter/StateDiffWriteback.h` 按 brief 逐字替换为 `state.apply(diff)` 委托实现，并附上契约注释（deleted_accounts 必须删除 / storage 值 0 = erase / code 仅 has_value 时覆盖）。

### Step 6: 跑测试确认全绿

```
$ cmake --build bcos-evm-ref/build 2>&1 | tail -6
[ 40%] Built target bcos-evm-ref-eth
[ 60%] Building CXX object test/CMakeFiles/bcos-evm-ref-eth-tests.dir/eth/StateDiffWritebackTest.cpp.o
[ 80%] Linking CXX executable bcos-evm-ref-eth-tests
ld: warning: ignoring duplicate libraries: '../vcpkg_installed/arm64-osx/lib/libgtest.a'
[100%] Built target bcos-evm-ref-eth-tests

$ ctest --test-dir bcos-evm-ref/build --output-on-failure
Test project …/bcos-evm-ref/build
    Start 1: BcosEvmRefEthTests
1/1 Test #1: BcosEvmRefEthTests ...............   Passed    0.43 sec
100% tests passed, 0 tests failed out of 1
Total Test time (real) =   0.43 sec
```

直接跑二进制核对 5 个用例逐条：

```
$ ./bcos-evm-ref/build/test/bcos-evm-ref-eth-tests
[==========] Running 5 tests from 2 test suites.
[ RUN      ] Skeleton.HeadersAndLinkOk
[       OK ] Skeleton.HeadersAndLinkOk (0 ms)
[ RUN      ] StateDiffWriteback.ContractDeletesListedAccount
[       OK ] StateDiffWriteback.ContractDeletesListedAccount (0 ms)
[ RUN      ] StateDiffWriteback.ContractStorageZeroMeansErase
[       OK ] StateDiffWriteback.ContractStorageZeroMeansErase (0 ms)
[ RUN      ] StateDiffWriteback.DeletesSameTxSelfdestruct
[       OK ] StateDiffWriteback.DeletesSameTxSelfdestruct (0 ms)
[ RUN      ] StateDiffWriteback.ErasesTouchedEmptyAccount
[       OK ] StateDiffWriteback.ErasesTouchedEmptyAccount (0 ms)
[  PASSED  ] 5 tests.
```

全绿，5 用例（Skeleton 1 + StateDiffWriteback 4），与 brief 预期一致。

### Step 7: Commit

```
$ git add bcos-evm-ref/adapter/ bcos-evm-ref/test/
$ git commit -m "feat(bcos-evm-ref): M1 StateDiffWriteback with EIP-6780/EIP-161 deletion semantics tests"
[feat-evm-opstack-on-evmone 30d2e3959] feat(bcos-evm-ref): M1 StateDiffWriteback with EIP-6780/EIP-161 deletion semantics tests
 4 files changed, 183 insertions(+)
 create mode 100644 bcos-evm-ref/adapter/StateDiffWriteback.h
 create mode 100644 bcos-evm-ref/adapter/StateViewAdapter.h
 create mode 100644 bcos-evm-ref/test/eth/StateDiffWritebackTest.cpp
```

`git show --stat HEAD`:
```
 bcos-evm-ref/adapter/StateDiffWriteback.h        |  18 +++
 bcos-evm-ref/adapter/StateViewAdapter.h          |  15 +++
 bcos-evm-ref/test/CMakeLists.txt                 |   1 +
 bcos-evm-ref/test/eth/StateDiffWritebackTest.cpp | 149 +++++++++++++++++++++++
 4 files changed, 183 insertions(+)
```

`git status --short bcos-evm-ref/` 提交后为空，无残留未跟踪/未暂存改动（提交时 pre-commit hook 对 `bcos-evm-ref/eth/EthTransition.{h,cpp}` 及 `bcos-evm-ref/test/eth/EthTransitionTest.cpp` 也跑了 `clang-format -i`，但这些文件无实际 diff，未被计入本次 commit stat，工作区在 commit 后确认为干净）。

Commit hash: `30d2e3959`
提交前两个 commit（供上下文核对）：
```
30d2e3959 feat(bcos-evm-ref): M1 StateDiffWriteback with EIP-6780/EIP-161 deletion semantics tests
86b42c7ee feat(bcos-evm-ref): module skeleton linking evmone::state (spec rev.3 M1 scaffold)
0150fb3cf docs(bcos-evm-ref): spec rev.3 and M1+M2 implementation plan
```

## 最终验证（干净重配置后再跑一次，排除 build 目录残留状态干扰）

（排查过程中曾误删 `test/CMakeFiles/bcos-evm-ref-eth-tests.dir` 构建元数据一次，随即用 `cmake .` 重新 configure 并整体重新 build+ctest 确认无异常，记录如下）

```
$ cd bcos-evm-ref/build && cmake .
… -- Configuring done -- Generating done -- Build files have been written to: …/bcos-evm-ref/build

$ cmake --build bcos-evm-ref/build 2>&1 | tail -15
[ 40%] Built target bcos-evm-ref-eth
[ 60%] Building CXX object test/CMakeFiles/bcos-evm-ref-eth-tests.dir/eth/EthTransitionTest.cpp.o
[ 80%] Building CXX object test/CMakeFiles/bcos-evm-ref-eth-tests.dir/eth/StateDiffWritebackTest.cpp.o
[100%] Linking CXX executable bcos-evm-ref-eth-tests
[100%] Built target bcos-evm-ref-eth-tests

$ ctest --test-dir bcos-evm-ref/build --output-on-failure
Test project …/bcos-evm-ref/build
    Start 1: BcosEvmRefEthTests
1/1 Test #1: BcosEvmRefEthTests ...............   Passed    0.64 sec
100% tests passed, 0 tests failed out of 1
```

## 自查

- [x] Step 1~7 全部按 brief 顺序执行，无跳步。
- [x] Step 2 的红（编译期，头文件缺失）与 brief 逐字预期一致。
- [x] Step 4 的红（真实断言/异常观察）真实记录，3 红 2 绿分布与 brief 预期一致；`ContractStorageZeroMeansErase` 的红以未捕获异常形式呈现这一细节已如实说明，未做任何"为了凑绿"的断言改动。
- [x] 未修改任何 `EXPECT_*`/`ASSERT_*` 断言；唯一对 Step1 提供代码的修改是把 `1'000'...'_u256` 数字分隔符字面量改为无分隔符形式（数值不变），原因是本仓库锁定的 intx 0.15.0 的 `from_string` 不支持该分隔符语法，纯编译期兼容性修复，已加注释说明，且已在本报告"发现"一节中完整披露。
- [x] Step 5 真实现严格照抄 brief 给定代码（`state.apply(diff)` + 契约注释）。
- [x] Step 6 全绿，5 用例，与 brief 预期数量一致。
- [x] `git add` 只暂存了 `bcos-evm-ref/adapter/` 与 `bcos-evm-ref/test/`，commit stat 核对为预期的 4 个文件、183 行新增，无越界改动。
- [x] 提交后 `git status --short bcos-evm-ref/` 为空，工作区干净。
- [x] 复原 build 目录（中途误删过一次 `CMakeFiles/bcos-evm-ref-eth-tests.dir` 排查残留状态，已用 `cmake .` 重新 configure 并整体重新 build+ctest 验证无异常）。

## 备注

本文件路径此前存在一份完全无关的旧报告（`Task 3 Report — ADR-032 Wave 3 Chain Adapter Promotion`，2026-06-30，属于另一个任务），已按本次任务要求整体覆盖。
