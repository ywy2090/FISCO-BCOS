# Task 18 Report: 三库 CMake 拆分

## 实现内容

1. 修改 `bcos-evm/CMakeLists.txt`
   - 拆分源列表为三个逻辑层：
     - `BCOS_EVM_ETH_SOURCES`
     - `BCOS_EVM_BCOS_SOURCES`
     - `BCOS_EVM_OP_SOURCES`
   - 新增三个静态库目标：
     - `bcos-evm-eth`
     - `bcos-evm-bcos`
     - `bcos-evm-op`
   - 增加兼容别名：
     - `add_library(bcos-evm ALIAS bcos-evm-bcos)`
   - 依赖关系调整为：
     - `bcos-evm-eth` 持有 `evmone::evmone`、`bcos-task`、`bcos-framework`、`ledger`、`bcos-protocol`、`bcos-utilities`
     - `bcos-evm-bcos` 依赖 `bcos-evm-eth`
     - `bcos-evm-op` 依赖 `bcos-evm-eth`

2. 更新下游 CMake
   - 修改 `bcos-evm/test/CMakeLists.txt`
   - `OpStackExecuteViaHostSmokeTest` 的链接库从 `bcos-evm` 调整为 `bcos-evm-op`，避免依赖兼容别名时丢失 OP 目标实现符号。

## 边界对齐说明

- `bcos-evm-eth` 仅包含 `eth/` 源文件（无 `bcos/`、`opstack/` 源）
- `bcos-evm-bcos` 仅包含 `bcos/` 源文件（无 `opstack/` 源）
- `bcos-evm-op` 仅包含 `opstack/` 源文件（无 `bcos/` 源）

## 验证

1. `rtk cmake -S . -B build-c3-3`
   - 结果：PASS

2. `rtk cmake --build build-c3-3 --target bcos-evm-eth bcos-evm-bcos bcos-evm-op -j8`
   - 结果：PASS
