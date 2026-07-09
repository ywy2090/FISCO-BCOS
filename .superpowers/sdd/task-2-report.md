# Task 2 Report: bcos-evm-ref 骨架（standalone 构建 + 空测试跑通）

- 状态: DONE
- 提交: `86b42c7` — `feat(bcos-evm-ref): module skeleton linking evmone::state (spec rev.3 M1 scaffold)`
- 分支: `feat-evm-opstack-on-evmone`
- 测试: `100% tests passed, 0 tests failed out of 1`（BcosEvmRefEthTests, 0.45 sec）

> 注：本文件原有内容是此前另一任务（postExpectation runner 接线）的遗留报告，已按协调者指令以本 Task 2（M1+M2 plan：模块骨架 + standalone 构建）的报告覆盖。

## 1. 创建的文件清单

均按 brief 逐字落盘：

| 文件 | 说明 |
|---|---|
| `bcos-evm-ref/CMakeLists.txt` | standalone 探测、`bcos-evm-ref-eth` 静态库 + `bcosevmref::eth` alias、PUBLIC include = 仓库根、PUBLIC 链接 `evmone::state`+`evmone::evmone`、`BCOS_EVM_REF_TESTS` 选项 |
| `bcos-evm-ref/vcpkg.json` | name/version-string/builtin-baseline `f6729a3a...`、依赖 evmone + gtest |
| `bcos-evm-ref/vcpkg-configuration.json` | overlay-ports 相对路径 `../ports/{evmone,intx,blst}` |
| `bcos-evm-ref/eth/EthTransition.h` | 空 namespace `bcos::evmref::eth` 占位（Task 4 填充） |
| `bcos-evm-ref/eth/EthTransition.cpp` | 仅 `#include <bcos-evm-ref/eth/EthTransition.h>` |
| `bcos-evm-ref/test/CMakeLists.txt` | `bcos-evm-ref-eth-tests` 可执行，链接 `bcosevmref::eth` + `evmone::testutils` + GTest，注册 `BcosEvmRefEthTests` |
| `bcos-evm-ref/test/eth/EthTransitionTest.cpp` | 恒真用例 `Skeleton.HeadersAndLinkOk`，真实 include `<test/state/state.hpp>` 与 `<test/utils/test_state.hpp>` 并构造 `evmone::test::TestState` |

## 2. Configure（实际命令与输出尾部）

```bash
cd /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor
cmake -B bcos-evm-ref/build -S bcos-evm-ref \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_BUILD_TYPE=Release
```

vcpkg 阶段：`Restored 6 package(s) from /Users/octopus/.cache/vcpkg/archives in 303 ms`（evmone 0.21.0#1 / intx / blst / nlohmann-json 等二进制缓存命中，evmone port ABI `0d4dd7ed...`）；gtest 1.17.0#1 从源码构建约 14 s。

输出尾部：

```
All requested installations completed successfully in: 14 s
-- Running vcpkg install - done
-- The CXX compiler identification is AppleClang 21.0.0.21000101
-- Detecting CXX compiler ABI info
-- Detecting CXX compiler ABI info - done
-- Check for working CXX compiler: /Library/Developer/CommandLineTools/usr/bin/c++ - skipped
-- Detecting CXX compile features
-- Detecting CXX compile features - done
-- Found nlohmann_json: .../bcos-evm-ref/build/vcpkg_installed/arm64-osx/share/nlohmann_json/nlohmann_jsonConfig.cmake (found version "3.12.0")
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD - Success
-- Found Threads: TRUE
-- Configuring done (20.2s)
-- Generating done (0.0s)
-- Build files have been written to: /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor/bcos-evm-ref/build
```

## 3. Build（实际命令与完整输出）

```bash
cmake --build bcos-evm-ref/build
```

```
[ 25%] Building CXX object CMakeFiles/bcos-evm-ref-eth.dir/eth/EthTransition.cpp.o
[ 50%] Linking CXX static library libbcos-evm-ref-eth.a
[ 50%] Built target bcos-evm-ref-eth
[ 75%] Building CXX object test/CMakeFiles/bcos-evm-ref-eth-tests.dir/eth/EthTransitionTest.cpp.o
[100%] Linking CXX executable bcos-evm-ref-eth-tests
ld: warning: ignoring duplicate libraries: '../vcpkg_installed/arm64-osx/lib/libgtest.a'
[100%] Built target bcos-evm-ref-eth-tests
```

## 4. ctest（实际命令与完整输出）

```bash
ctest --test-dir bcos-evm-ref/build --output-on-failure
```

```
Internal ctest changing into directory: /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor/bcos-evm-ref/build
Test project /Users/octopus/octo/code/FISCO-BCOS/.claude/worktrees/feat-evm-refactor/bcos-evm-ref/build
    Start 1: BcosEvmRefEthTests
1/1 Test #1: BcosEvmRefEthTests ...............   Passed    0.45 sec

100% tests passed, 0 tests failed out of 1

Total Test time (real) =   0.45 sec
```

## 5. 提交 hash 与 stat

```
86b42c7 feat(bcos-evm-ref): module skeleton linking evmone::state (spec rev.3 M1 scaffold)

 25  0  bcos-evm-ref/CMakeLists.txt
  1  0  bcos-evm-ref/eth/EthTransition.cpp
  6  0  bcos-evm-ref/eth/EthTransition.h
 12  0  bcos-evm-ref/test/CMakeLists.txt
        bcos-evm-ref/test/eth/EthTransitionTest.cpp
        bcos-evm-ref/vcpkg-configuration.json
        bcos-evm-ref/vcpkg.json
 7 files changed, 70 insertions(+)
```

仅 `bcos-evm-ref/` 下 7 个新文件入库；`bcos-evm-ref/build/` 未被追踪，未混入任何无关文件。

## 6. 自查（对照 brief 逐 Step）

- [x] Step 1: `bcos-evm-ref/vcpkg.json` — 与 brief 逐字一致（baseline `f6729a3ac3bfdefc999aa8e3664f8014370886b8`）
- [x] Step 2: `bcos-evm-ref/vcpkg-configuration.json` — overlay-ports 相对路径复用 worktree ports
- [x] Step 3: `bcos-evm-ref/CMakeLists.txt` — 与 brief 逐字一致（standalone 探测 / alias / PUBLIC include / PUBLIC link / 测试选项）
- [x] Step 4: `eth/EthTransition.h` 空 namespace 占位 + `eth/EthTransition.cpp` 以 `<bcos-evm-ref/...>` 路径 include 自身头，验证 PUBLIC include 布局
- [x] Step 5: `test/CMakeLists.txt` — 与 brief 逐字一致（链接 `evmone::testutils`）
- [x] Step 6: `test/eth/EthTransitionTest.cpp` — 与 brief 逐字一致（include evmone `test/state/state.hpp` + `test/utils/test_state.hpp`，构造 `evmone::test::TestState` 验证头树与链接）
- [x] Step 7: configure + build + ctest 真实执行 — configure 成功（evmone 等 6 包二进制缓存命中，gtest 源码构建 14 s），build 成功，ctest `100% tests passed, 1 tests` 符合 Expected
- [x] Step 8: `git add bcos-evm-ref/ && git commit` — 提交 `86b42c7`，提交信息与 brief 逐字一致

## 7. Concerns

- 链接时出现无害告警 `ld: warning: ignoring duplicate libraries: 'libgtest.a'` —— 因同时链接 `GTest::gtest` 与 `GTest::gtest_main`（后者已依赖前者）所致，为 brief 规定的链接列表，功能无影响；后续 Task 若介意可去掉显式 `GTest::gtest`。
- 接口产物与 brief 声明一致：`bcos-evm-ref-eth`（alias `bcosevmref::eth`）、测试可执行 `bcos-evm-ref-eth-tests`；Task 1 overlay port 提供的 `evmone::state` / `evmone::evmone` / `evmone::testutils` targets 均解析并链接成功。
- 本报告文件覆盖了同路径下另一任务（postExpectation 接线）的遗留旧报告；若该旧报告仍需保留，协调者可从会话历史或该任务的原始产出处恢复。
