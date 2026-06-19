# Task 19 Report: 公共 API facade 头

## 实现内容

1. 新增 facade 头目录与文件：`bcos-evm/include/bcos-evm/`
   - `executor.hpp`
     - 仅转发 `bcos-evm/eth/executeMessage.h`
   - `eth_executor.hpp`
     - 转发 `bcos-evm/eth/state/transition.hpp`
     - 转发 `bcos-evm/eth/EthTxExecutor.h`
   - `fisco_executor.hpp`
     - 转发 `bcos-evm/bcos/ExecuteViaHost.h`
     - 转发 `bcos-evm/bcos/FiscoTxExecutor.h`
   - `op_executor.hpp`
     - 转发 `bcos-evm/opstack/OpStackExecuteViaHost.h`

2. 修改 `bcos-evm/CMakeLists.txt`
   - 引入 `GNUInstallDirs`
   - 三个目标 `bcos-evm-eth` / `bcos-evm-bcos` / `bcos-evm-op` 的头文件搜索路径统一为：
     - `BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}`
     - `BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include`
     - `INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}`
   - 新增安装导出规则：
     - `install(TARGETS ... EXPORT fiscobcosTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})`
     - `install(DIRECTORY include/bcos-evm DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")`

## 规范对齐

- 对齐 `docs/superpowers/specs/2026-06-18-bcos-evm-layer-refactor-design.md` §7.2：
  - `executor.hpp`（executeMessage）
  - `eth_executor.hpp`（transition + EthTxExecutor）
  - `fisco_executor.hpp`（executeViaHost + FiscoTxExecutor）
  - `op_executor.hpp`（OpStackExecuteViaHost）

## 验证

1. `rtk cmake --build build-c3-3 --target bcos-evm-eth bcos-evm-bcos bcos-evm-op -j8`
   - 结果：PASS
