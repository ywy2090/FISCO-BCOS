# Task 15 Report: Step 2 验收

## 验收命令与结果

1. `grep bcos-executor in bcos-evm/`（用 `rtk rg` 等价执行）
   - 命令：
     - `rtk rg "bcos-executor" bcos-evm`
     - `rtk rg -c "bcos-executor" bcos-evm`
   - 结果：仍有 4 个文件共 30 处引用
     - `bcos-evm/bcos/PrecompiledManager.cpp`: 19
     - `bcos-evm/bcos/PrecompiledImpl.h`: 6
     - `bcos-evm/bcos/Precompiled.h`: 3
     - `bcos-evm/bcos/AuthCheck.h`: 2

2. `ctest bcos-evm/test` 全量
   - 命令：`rtk ctest --test-dir build-c3-3/bcos-evm/test --output-on-failure`
   - 初次结果：11 个用例因可执行文件缺失 `Not Run`
   - 修复后结果：14 个中 12 个通过，2 个失败
     - `WarmTransactionEntry`
     - `NestedCallHost`

3. `ctest -R CompatExecuteViaHost`（50/50）
   - 命令：`rtk ctest --test-dir build-c3-3 -R "CompatExecuteViaHost" --output-on-failure`
   - 结果：**50/50 PASS**

4. `ctest -R ExecuteViaHostCompat + FIB101`
   - 命令：`rtk ctest --test-dir build-c3-3 -R "ExecuteViaHostCompat|FIB101_102_103_104_SchedulerTest" --output-on-failure`
   - 结果：17 个中 14 个通过，3 个失败
     - `ExecuteViaHostCompatTest/fib88_insufficient_balance_consumes_all_gas`
     - `ExecuteViaHostCompatTest/fib88_not_found_code_revert_preserves_gas`
     - `ExecuteViaHostCompat`（聚合套件，包含上述失败）
   - `FIB101_102_103_104_SchedulerTest`：10/10 PASS

## 本次修复

- 为使 Step 2 全量 `bcos-evm/test` 可执行，修复 `state` 系列测试目标链接缺失：
  - 文件：`bcos-evm/test/CMakeLists.txt`
  - 变更：在以下 target 增加 `../eth/executeMessage.cpp`
    - `PragueStateTest`
    - `NestedCallHostTest`
    - `PrecompileInCallTest`
    - `BlockHashHostTest`
    - `NestedRevertWarmTest`

## 结论

- Step 2 验收已执行完成，`CompatExecuteViaHost` 与 `FIB101` 主线通过。
- 当前仍有 4 个失败点（`bcos-evm/test` 2 个 + `ExecuteViaHostCompat` 2 个独立 case），需在后续任务继续收敛。
