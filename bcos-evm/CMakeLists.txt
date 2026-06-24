cmake_minimum_required(VERSION 3.28)

find_package(evmone CONFIG REQUIRED)
include(GNUInstallDirs)

set(BCOS_EVM_ETH_SOURCES
    eth/vm/VMInstance.cpp
    eth/EVMCResult.cpp
    eth/Eip7702.cpp
    eth/ExecuteViaEth.cpp
    eth/ExecuteViaEthPreCheck.cpp
    eth/orchestration/OrchestrationPipeline.cpp
    eth/ExecuteMessage.cpp
    eth/state/State.cpp
    eth/state/EthHost.cpp
    eth/state/BloomFilter.cpp
    eth/state/EthPrecompiles.cpp
    eth/state/Transition.cpp
    eth/precompiled/EthBuiltinRegistry.cpp
    eth/precompiled/PrecompiledContract.cpp
    eth/precompiled/ModexpGas.cpp
    eth/precompiled/PrecompileRouter.cpp
)

set(BCOS_EVM_BCOS_SOURCES
    bcos/ExecuteViaHost.cpp
    bcos/FiscoHostExtension.cpp
    bcos/FiscoOrchestrationProfile.cpp
    bcos/FiscoStateView.cpp
)

set(BCOS_EVM_OP_SOURCES
    opstack/OpStackExecuteViaHost.cpp
    opstack/OpStackOrchestrationProfile.cpp
    opstack/OpStackPreCheck.cpp
    opstack/OpStackPreDebitEntry.cpp
    opstack/OpStackTxExecutor.cpp
    opstack/RollupCost.cpp
    opstack/OpStackFee.cpp
    opstack/OpStackFloorGas.cpp
    opstack/L1BlockStorage.cpp
    opstack/L1BlockPredeploy.cpp
    opstack/GasPriceOraclePredeploy.cpp
)

add_library(bcos-evm-eth STATIC ${BCOS_EVM_ETH_SOURCES})
add_library(bcos-evm-bcos STATIC ${BCOS_EVM_BCOS_SOURCES})
add_library(bcos-evm-op STATIC ${BCOS_EVM_OP_SOURCES})
add_library(bcos-evm ALIAS bcos-evm-bcos)

target_include_directories(bcos-evm-eth PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_include_directories(bcos-evm-bcos PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_include_directories(bcos-evm-op PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(bcos-evm-eth PUBLIC
    evmone::evmone
    bcos-task
    bcos-framework
    ledger
    bcos-protocol
    bcos-utilities
    bcos-crypto
    codec
)

target_link_libraries(bcos-evm-bcos PUBLIC
    bcos-evm-eth
)

target_link_libraries(bcos-evm-op PUBLIC
    bcos-evm-eth
)

install(TARGETS bcos-evm-eth bcos-evm-bcos bcos-evm-op EXPORT fiscobcosTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(DIRECTORY "include/bcos-evm" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")

option(BCOS_EVM_SPECS_TESTS "Build EVM reference test runners" OFF)

if(TESTS)
    include(CTest)
    add_subdirectory(test)
    if(BCOS_EVM_SPECS_TESTS)
        add_subdirectory(specs-tests)
    endif()
endif()
