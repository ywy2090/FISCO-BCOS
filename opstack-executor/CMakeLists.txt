cmake_minimum_required(VERSION 3.25)

# OpstackExecutor is header-only (a template driven by the storage type), so the module is an
# INTERFACE library. It reuses the ethereum-executor adapter (BCOS<->evmone conversions,
# StorageStateView, state-diff writeback, receipt conversion) and drives the OP transition
# pipeline from bcos-evm-opstack.
#
# DEPENDENCY: the `ethereum-executor` target is provided by PR #5366. This module must be built
# on top of it (see README.md). The add_subdirectory in the repo-root CMakeLists is guarded on
# the ethereum-executor target existing so the tree stays buildable before #5366 lands.

add_library(opstack-executor INTERFACE)
target_include_directories(opstack-executor INTERFACE
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(opstack-executor INTERFACE
    ethereum-executor      # adapter: BCOS2Evmone.h, StorageStateView (PR #5366)
    bcos-evm-opstack       # opValidateFromState / opTransition / OpReceipt / OpForkSchedule
    bcos-evm-eth
    bcos-framework)

if (TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
