cmake_minimum_required(VERSION 3.28)

find_package(evmone CONFIG REQUIRED)

set(BCOS_EVM_ETH_SOURCES
    eth/vm/VMInstance.cpp
    eth/EVMCResult.cpp
    eth/executeMessage.cpp
    eth/state/State.cpp
    eth/state/EthHost.cpp
    eth/state/bloom_filter.cpp
    eth/state/EthPrecompiles.cpp
    eth/state/transition.cpp
    eth/precompiled/EthBuiltinRegistry.cpp
    eth/precompiled/PrecompiledContract.cpp
    eth/precompiled/ModexpGas.cpp
)

set(BCOS_EVM_BCOS_SOURCES
    bcos/ExecuteViaHost.cpp
    bcos/FiscoHostExtension.cpp
    bcos/FiscoStateView.cpp
)

set(BCOS_EVM_OP_SOURCES
    opstack/OpStackExecuteViaHost.cpp
)

add_library(bcos-evm-eth STATIC ${BCOS_EVM_ETH_SOURCES})
add_library(bcos-evm-bcos STATIC ${BCOS_EVM_BCOS_SOURCES})
add_library(bcos-evm-op STATIC ${BCOS_EVM_OP_SOURCES})
add_library(bcos-evm ALIAS bcos-evm-bcos)

target_include_directories(bcos-evm-eth PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(bcos-evm-bcos PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(bcos-evm-op PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(bcos-evm-eth PUBLIC
    evmone::evmone
    bcos-task
    bcos-framework
    ledger
    bcos-protocol
    bcos-utilities
)

target_link_libraries(bcos-evm-bcos PUBLIC
    bcos-evm-eth
)

target_link_libraries(bcos-evm-op PUBLIC
    bcos-evm-eth
)

if(TESTS)
    add_subdirectory(test)
endif()
