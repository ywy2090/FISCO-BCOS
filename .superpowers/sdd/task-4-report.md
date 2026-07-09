# Task 4 Report: M2 — `eth::runTransaction` / `runBlockFinalize`（TDD）

## Status

**DONE**

## Commit

- `516688edbd1f535dc7caa3853d9fa0bd07e5afca` — feat(bcos-evm-ref): M2 eth::runTransaction/runBlockFinalize thin wrappers

```
 bcos-evm-ref/eth/EthTransition.cpp                 |  23 ++++
 bcos-evm-ref/include/bcos-evm-ref/eth/EthTransition.h |  20 ++-
 bcos-evm-ref/test/eth/EthTransitionTest.cpp        | 137 ++++++++++++++++++++-
 3 files changed, 175 insertions(+), 5 deletions(-)
```

## 布局说明（brief 之后的变更，已按此执行）

- 公开头位于 `bcos-evm-ref/include/bcos-evm-ref/eth/EthTransition.h`（不是 brief 里写的
  `bcos-evm-ref/eth/EthTransition.h`）；`eth/EthTransition.cpp` 仍在原处。`<bcos-evm-ref/...>`
  包含路径不变。
- `bcos-evm-ref/CMakeLists.txt` 中 `bcos-evm-ref-eth` 已 PUBLIC 链接 `evmone::testutils`
  （第 20 行），故测试目标 `bcos-evm-ref/test/CMakeLists.txt` 只需链 `bcosevmref::eth` +
  `GTest::gtest` + `GTest::gtest_main`，未额外加 `evmone::testutils`——现状本就如此，未改动。
- `EthTransitionTest.cpp` 是独立文件；brief 代码中的 `1'000'000'000'000'000'000_u256`
  与 `5'000'000'000_u256` 两处带分隔符字面量在 pinned intx 0.15.0 下编译不过（数字分隔符
  `'` 不被 `from_dec_digit` 识别，consteval 阶段抛 `invalid_argument`），已比照
  `StateDiffWritebackTest.cpp` 的 `kFunding` 先例改为匿名 namespace 内两个具名常量
  `kFunding`（1e18 wei）与 `kWithdrawalWei`（5e9 wei），并保留等价 NOTE 注释说明原因。

## Step 记录

### Step 0（基线确认，brief 外补做）

替换测试文件前先跑一次基线，确认"Skeleton 1 + StateDiffWriteback 5 = 6/6 全绿"的前提为真：

```
$ cmake --build bcos-evm-ref/build
[100%] Built target bcos-evm-ref-eth-tests   # 无重新编译，已是最新

$ ctest --test-dir bcos-evm-ref/build --output-on-failure
100% tests passed, 0 tests failed out of 1

$ ./bcos-evm-ref/build/test/bcos-evm-ref-eth-tests
[==========] Running 6 tests from 2 test suites.
[----------] 1 test from Skeleton
[       OK ] Skeleton.HeadersAndLinkOk (0 ms)
[----------] 5 tests from StateDiffWriteback
[       OK ] StateDiffWriteback.ContractDeletesListedAccount (0 ms)
[       OK ] StateDiffWriteback.ContractStorageZeroMeansErase (0 ms)
[       OK ] StateDiffWriteback.ContractNulloptCodePreservesExisting (0 ms)
[       OK ] StateDiffWriteback.DeletesSameTxSelfdestruct (0 ms)
[       OK ] StateDiffWriteback.ErasesTouchedEmptyAccount (0 ms)
[  PASSED  ] 6 tests.
```

基线确认：6/6 绿，与任务说明一致。

### Step 1: 重写 `EthTransitionTest.cpp` 为失败测试

替换恒真的 `Skeleton.HeadersAndLinkOk` 占位用例为 brief 给出的四个用例
（`SimpleTransfer21000` / `InvalidTxRejectedWithoutSideEffect` /
`BlobTxRejectedWhenNoBlobGasLeft` / `FinalizeAppliesWithdrawalGweiToWei`），
按上述具名常量修正做数值字面量替换。文件：
`bcos-evm-ref/test/eth/EthTransitionTest.cpp`。

### Step 2: 编译确认失败（真实观察）

```
$ cmake --build bcos-evm-ref/build 2>&1 | tail -60
...
bcos-evm-ref/test/eth/EthTransitionTest.cpp:47:41: error: no member named 'runTransaction' in namespace 'bcos::evmref::eth'
   47 |     const auto res = bcos::evmref::eth::runTransaction(
      |                                         ^~~~~~~~~~~~~~
bcos-evm-ref/test/eth/EthTransitionTest.cpp:68:22: error: variable has incomplete type 'state::BlockInfo'
   68 |     state::BlockInfo block;
      |                      ^
.../test/utils/test_state.hpp:17:8: note: forward declaration of 'evmone::state::BlockInfo'
   17 | struct BlockInfo;
      |        ^
bcos-evm-ref/test/eth/EthTransitionTest.cpp:73:24: error: variable has incomplete type 'state::Transaction'
...
bcos-evm-ref/test/eth/EthTransitionTest.cpp:84:41: error: no member named 'runTransaction' in namespace 'bcos::evmref::eth'
...
bcos-evm-ref/test/eth/EthTransitionTest.cpp:120:41: error: no member named 'runTransaction' in namespace 'bcos::evmref::eth'
...
bcos-evm-ref/test/eth/EthTransitionTest.cpp:133:29: error: variable has incomplete type 'const state::Withdrawal'
...
bcos-evm-ref/test/eth/EthTransitionTest.cpp:135:42: error: no member named 'runBlockFinalize' in namespace 'bcos::evmref::eth'
14 errors generated.
make[2]: *** [test/CMakeFiles/bcos-evm-ref-eth-tests.dir/eth/EthTransitionTest.cpp.o] Error 1
```

Expected 符合：`runTransaction`/`runBlockFinalize` 未定义；`BlockInfo`/`Transaction`/`Withdrawal`
在占位头（仅 `#pragma once` + 空命名空间，未 `#include <test/state/state.hpp>`）下是不完整类型，
因此 `state::BlockInfo`/`Transaction`/`Withdrawal` 的具体用法也一并报错——这是占位头缺 include
的连带效应，符合"重写占位实现"的预期红。

（过程注记：第一次跑 Step 2 时误留了一处带分隔符的 `5'000'000'000_u256` 字面量，
额外报出 consteval 字面量错误，与 runTransaction 缺失错误混在一起；随即按 Step 1
的具名常量修正补掉该字面量，再跑一次得到上面这份纯净的"红"记录。）

### Step 3: 实现 `EthTransition.h` / `.cpp`

按 brief 逐字落盘（路径改为 `bcos-evm-ref/include/bcos-evm-ref/eth/EthTransition.h`）：

- `runTransaction`: `validate_transaction` → 失败提前返回 `error_code`；成功则转 `transition`。
- `runBlockFinalize`: 直接透传 `evmone::state::finalize`。

两者均为薄封装，不写回状态，签名与 brief 一致（`blockGasLeft`/`blobGasLeft` 顺序未换）。

提交后 pre-commit 钩子跑了一次 `clang-format -i`（仅折行/空白变化，见下方 diff 摘要），
未改变语义，重新编译验证通过后再提交成功。

### Step 4: 跑测试确认通过

```
$ cmake --build bcos-evm-ref/build
[ 20%] Building CXX object CMakeFiles/bcos-evm-ref-eth.dir/eth/EthTransition.cpp.o
[ 40%] Linking CXX static library libbcos-evm-ref-eth.a
[ 40%] Built target bcos-evm-ref-eth
[ 60%] Building CXX object test/CMakeFiles/bcos-evm-ref-eth-tests.dir/eth/EthTransitionTest.cpp.o
[ 80%] Linking CXX executable bcos-evm-ref-eth-tests
[100%] Built target bcos-evm-ref-eth-tests

$ ctest --test-dir bcos-evm-ref/build --output-on-failure
Test project bcos-evm-ref/build
    Start 1: BcosEvmRefEthTests
1/1 Test #1: BcosEvmRefEthTests ...............   Passed    0.50 sec
100% tests passed, 0 tests failed out of 1
Total Test time (real) =   0.51 sec

$ ./bcos-evm-ref/build/test/bcos-evm-ref-eth-tests
Running main() from .../gtest_main.cc
[==========] Running 9 tests from 2 test suites.
[----------] Global test environment set-up.
[----------] 4 tests from EthTransition
[ RUN      ] EthTransition.SimpleTransfer21000
[       OK ] EthTransition.SimpleTransfer21000 (0 ms)
[ RUN      ] EthTransition.InvalidTxRejectedWithoutSideEffect
[       OK ] EthTransition.InvalidTxRejectedWithoutSideEffect (0 ms)
[ RUN      ] EthTransition.BlobTxRejectedWhenNoBlobGasLeft
[       OK ] EthTransition.BlobTxRejectedWhenNoBlobGasLeft (0 ms)
[ RUN      ] EthTransition.FinalizeAppliesWithdrawalGweiToWei
[       OK ] EthTransition.FinalizeAppliesWithdrawalGweiToWei (0 ms)
[----------] 4 tests from EthTransition (0 ms total)

[----------] 5 tests from StateDiffWriteback
[ RUN      ] StateDiffWriteback.ContractDeletesListedAccount
[       OK ] StateDiffWriteback.ContractDeletesListedAccount (0 ms)
[ RUN      ] StateDiffWriteback.ContractStorageZeroMeansErase
[       OK ] StateDiffWriteback.ContractStorageZeroMeansErase (0 ms)
[ RUN      ] StateDiffWriteback.ContractNulloptCodePreservesExisting
[       OK ] StateDiffWriteback.ContractNulloptCodePreservesExisting (0 ms)
[ RUN      ] StateDiffWriteback.DeletesSameTxSelfdestruct
[       OK ] StateDiffWriteback.DeletesSameTxSelfdestruct (0 ms)
[ RUN      ] StateDiffWriteback.ErasesTouchedEmptyAccount
[       OK ] StateDiffWriteback.ErasesTouchedEmptyAccount (0 ms)
[----------] 5 tests from StateDiffWriteback (0 ms total)

[----------] Global test environment tear-down
[==========] 9 tests from 2 test suites ran. (0 ms total)
[  PASSED  ] 9 tests.
```

**9/9 全绿**（EthTransition 4 + StateDiffWriteback 5），与更新后的预期一致
（brief 原文"8 用例"基于旧的 Skeleton+StateDiffWriteback(4) 基线，已按当前
StateDiffWriteback=5 的实际基线修正为 9）。

### Step 5: Commit

第一次 `git commit` 被 pre-commit 钩子的 clang-format 检查拦下（`-Wclang-format-violations`），
钩子已自动 `clang-format -i` 修正了三个文件的折行/空白（无语义变化，已用 `git diff` 核对：
仅参数换行位置、`blob_hashes` 单行化等），随后重新编译 + 跑测试确认仍 9/9 绿，
`git add` 三个改动文件后重新提交成功：

```
$ git commit -m "feat(bcos-evm-ref): M2 eth::runTransaction/runBlockFinalize thin wrappers"
# 第一次：clang-format 校验失败，钩子已就地格式化文件，退出码 1（未提交）
$ cmake --build bcos-evm-ref/build && ./bcos-evm-ref/build/test/bcos-evm-ref-eth-tests
# 9/9 PASSED（复核格式化未破坏语义）
$ git add bcos-evm-ref/eth/ bcos-evm-ref/test/ bcos-evm-ref/include/bcos-evm-ref/eth/EthTransition.h
$ git commit -m "feat(bcos-evm-ref): M2 eth::runTransaction/runBlockFinalize thin wrappers"
# ok 516688e
```

## Commit stat

```
$ git show --stat HEAD
commit 516688edbd1f535dc7caa3853d9fa0bd07e5afca
Author: octopus <912554887@qq.com>
Date:   Thu Jul 9 09:49:30 2026 +0800

    feat(bcos-evm-ref): M2 eth::runTransaction/runBlockFinalize thin wrappers

 bcos-evm-ref/eth/EthTransition.cpp                 |  23 ++++
 bcos-evm-ref/include/bcos-evm-ref/eth/EthTransition.h |  20 ++-
 bcos-evm-ref/test/eth/EthTransitionTest.cpp        | 137 ++++++++++++++++++++-
 3 files changed, 175 insertions(+), 5 deletions(-)
```

## 自查

| Check | Result |
|-------|--------|
| 签名与 brief 逐字一致（`Result`/`runTransaction`/`runBlockFinalize` 参数顺序不变，尤其 `blockGasLeft, blobGasLeft` 不换序） | PASS |
| 包装不写回状态（`const StateView&` 输入，diff 交由 `applyStateDiff`） | PASS |
| 头文件路径按 review fix wave 1 使用 `include/bcos-evm-ref/eth/EthTransition.h` | PASS |
| 测试目标只链 `bcosevmref::eth` + GTest（未额外加 `evmone::testutils`） | PASS（`test/CMakeLists.txt` 本就如此，未改动） |
| 裸分隔符字面量替换为具名常量（`kFunding`, `kWithdrawalWei`），含 NOTE 注释 | PASS |
| Step 2 编译红真实执行并记录（而非假设） | PASS |
| 最终用例数 = 9（StateDiffWriteback 5 + EthTransition 4） | PASS |
| 仅提交 `bcos-evm-ref/eth/`、`bcos-evm-ref/test/`、`bcos-evm-ref/include/.../EthTransition.h` 三个文件，未误带其他并发改动（`task-2-report.md`/`task-3-report.md` 等未 staged） | PASS |
| clang-format 自动改动核对为纯格式无语义变化 | PASS（已重跑 9/9 绿复核） |

## Concerns

无阻塞性 concern。唯一值得后续任务（Task 5/6）注意的点：`EthTransitionTest.cpp` 中
`FinalizeAppliesWithdrawalGweiToWei` 是 M2 范围内对 `runBlockFinalize` withdrawals 路径
的唯一覆盖（EEST state 测试约定本身不触达该路径），Task 5 的 EEST harness 若需要
ommer/block-reward 路径的覆盖，需要另外补测试。
