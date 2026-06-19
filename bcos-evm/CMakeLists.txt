cmake_minimum_required(VERSION 3.28)

find_package(evmone CONFIG REQUIRED)

add_library(bcos-evm STATIC
    bcos/ExecuteViaHost.cpp
    bcos/FiscoHostExtension.cpp
    bcos/FiscoStateView.cpp
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

target_include_directories(bcos-evm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(bcos-evm PUBLIC
    evmone::evmone
    bcos-task
    bcos-framework
    ledger
    bcos-protocol
    bcos-utilities
)

if(TESTS)
    add_subdirectory(test)
endif()
