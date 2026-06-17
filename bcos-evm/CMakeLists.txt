cmake_minimum_required(VERSION 3.28)

add_library(bcos-evm STATIC
    ethereum/vm/VMInstance.cpp
    ethereum/EVMCResult.cpp
    ethereum/precompiled/PrecompiledRegistrar.cpp
    ethereum/precompiled/PrecompiledContract.cpp
    ethereum/precompiled/ModexpGas.cpp
    bcos/FiscoPolicy.h
    bcos/PrecompiledManager.cpp
)

target_include_directories(bcos-evm PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(bcos-evm PUBLIC
    evmone::evmone
    bcos-task
    bcos-framework
)
