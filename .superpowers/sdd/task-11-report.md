# Task 11 Report: executeMessage empty-code hook (§8.4)

## 完成内容

1. `executeMessage` empty-code 路径改造
   - 修改 `bcos-evm/eth/executeMessage.cpp`。
   - 将非创建交易的空代码快捷路径改为：
     - `state.checkpoint()`
     - 调用 `HostExtension::tryChainPrecompile(...)`
     - 若 hook 命中：
       - `EVMC_SUCCESS` -> `state.commit()` + 输出 `stateDiff`
       - 非成功状态 -> `state.revert()`
     - 若 hook 未命中：保持 empty-call 成功返回语义。
   - 去掉了“empty path 无条件 commit”带来的语义偏差，满足 §8.4。

2. 新增 `EmptyCodeHookTest`（TDD Red -> Green）
   - 新增 `bcos-evm/test/opstack/EmptyCodeHookTest.cpp`。
   - 用例：top-level `CALL` 到 `0x4200...0015`，输入 setter selector `0x098999be`（短输入），验证不走 empty success，而是命中链级 predeploy hook 返回 `EVMC_REVERT`。

3. 新增 `L1AttributesDepositTest` skeleton
   - 新增 `bcos-evm/test/opstack/L1AttributesDepositTest.cpp`。
   - 作为 Task 14 的测试占位，当前仅保留最小 skeleton。

4. 测试构建接线
   - 修改 `bcos-evm/test/CMakeLists.txt`，注册：
     - `EmptyCodeHookTest` / `EmptyCodeHook`
     - `L1AttributesDepositTest` / `L1AttributesDeposit`

5. 计划文档更新
   - 修改 `docs/superpowers/plans/2026-06-18-opstack-isthmus.md`。
   - Task 11 的 Step 1~3 已标记为完成。

## TDD 与验证

1. Red（先失败）
   - `rtk ctest --test-dir build-task10/bcos-evm/test -R EmptyCodeHook --output-on-failure`
   - 结果：失败，`success != revert`（符合预期，证明测试有效）。

2. Green（改实现后转绿）
   - `rtk cmake --build build-task10 --target EmptyCodeHookTest L1AttributesDepositTest -j$(sysctl -n hw.ncpu)`
   - `rtk ctest --test-dir build-task10/bcos-evm/test -R "(EmptyCodeHook|L1AttributesDeposit)" --output-on-failure`
   - 结果：2/2 通过。

3. bcos-evm 全量回归
   - 构建全部 `bcos-evm/test` 可执行后执行：
   - `rtk ctest --test-dir build-task10/bcos-evm/test --output-on-failure`
   - 结果：30/30 通过。

4. 额外回归观察（非 Task 11 直接范围）
   - `transaction-executor/tests` 中 `EthTxGasSettlementExecutor/*` 若干 case 失败（10/35），表现为 `gasUsed` 断言不匹配（如 42000 vs 21000）。
   - `ExecuteViaHostCompat*` 相关 case 通过。

