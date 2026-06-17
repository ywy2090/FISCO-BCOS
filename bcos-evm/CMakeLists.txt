cmake_minimum_required(VERSION 3.28)

find_package(evmone CONFIG REQUIRED)

add_library(bcos-evm STATIC
    eth/vm/VMInstance.cpp
    eth/EVMCResult.cpp
    eth/precompiled/PrecompiledRegistrar.cpp
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
    bcos-protocol
    bcos-utilities
)
