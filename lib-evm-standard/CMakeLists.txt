cmake_minimum_required(VERSION 3.28)

add_library(bcos-evm-standard STATIC
    src/EVMCResult.cpp
    src/vm/VMInstance.cpp
    src/precompiled/PrecompiledRegistrar.cpp
    src/precompiled/PrecompiledContract.cpp
    src/precompiled/ModexpGas.cpp
)

target_include_directories(bcos-evm-standard PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(bcos-evm-standard PUBLIC
    evmone::evmone
    bcos-task
)
