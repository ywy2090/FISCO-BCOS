# Task 20 Report: 最终验收 + progress

## 验收执行（build-c3-3）

1. `rtk ctest --test-dir build-c3-3/bcos-evm/test --output-on-failure`
   - 结果：PASS（15/15）

2. `rtk ctest --test-dir build-c3-3 -R 'CompatExecuteViaHost|ExecuteViaHostCompat|FIB101|OpStackExecuteViaHost' --output-on-failure`
   - 首轮：FAIL（`ExecuteViaHostCompat` 聚合套件失败）
   - 根因：`test-execute-via-host-compat` 未包含在 `test-transaction-executor` 目标中，聚合测试命中旧二进制
   - 处置：执行 `rtk cmake --build build-c3-3 --target test-execute-via-host-compat -j8` 后复跑
   - 复跑结果：PASS（67/67）

3. `rtk cmake --build build-c3-3 --target test-transaction-executor -j8`
   - 结果：PASS

4. `rtk rg "bcos-executor" bcos-evm`
   - 剩余命中集中在 FISCO 预编译耦合层（`AuthCheck` / `Precompiled*`）：
     - `bcos-evm/bcos/AuthCheck.h`
     - `bcos-evm/bcos/Precompiled.h`
     - `bcos-evm/bcos/PrecompiledImpl.h`
     - `bcos-evm/bcos/PrecompiledManager.cpp`
   - 说明：与 Task 14/19 记录一致，属于已知遗留耦合，不阻塞 Step 1–4 验收

5. `rtk cmake --build build-c3-3 --target bcos-evm-eth bcos-evm-bcos bcos-evm-op -j8`
   - 结果：PASS

## 文档更新

- 已更新 `sdd/progress.md`：
  - 标记 `Layer Refactor Step 1–4: complete`
  - 追加 Task 20 验收结论与命令结果

## 结论

Task 20 完成。Step 1–4 最终验收通过，当前分层结果可进入后续提交流程。
