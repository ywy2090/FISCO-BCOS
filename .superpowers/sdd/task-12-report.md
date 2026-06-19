# Task 12 Report: EIP-7702

## 完成内容

1. 新增 `bcos-evm/eth/Eip7702.h/.cpp`
   - 实现 `parseDelegationTarget` / `addressToDelegation`。
   - 实现 `applyAuthorizations`：逐条授权处理，非法授权按 tuple 忽略，已有账户补偿 refund `12500`。
   - 实现 `warmDelegationTarget`，在 CALL 前预热委托目标。

2. 打通执行链路
   - `executeMessage` 新增授权输入：`authorizationListPresent` + `authorizations`。
   - 在 non-create 路径、EVM 执行前调用 `applyAuthorizations`。
   - `OpStackExecuteViaHost` 将授权列表传入 `executeMessage`。

3. preCheck 形状校验
   - `OpStackPreCheck` 新增：若显式携带授权字段但列表为空，返回 `Malformed`。

4. 测试
   - 新增 `Eip7702PreCheckTest.cpp`。
   - 新增 `Eip7702ApplyAuthorizationTest.cpp`。
   - 更新 `bcos-evm/test/CMakeLists.txt` 注册对应 target/test。

## TDD 与验证

- RED：新增测试后先失败（缺失 `authorizationListPresent` / `make...` 相关能力）。
- GREEN：补齐实现后通过：
  - `ctest -R 'Eip7702PreCheck|Eip7702ApplyAuthorization'` 2/2 通过。
- 全量：
  - `ctest --test-dir BUILD/bcos-evm/test --output-on-failure` 32/32 通过（Task 12 时点）。
