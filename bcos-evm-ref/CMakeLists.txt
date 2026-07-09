cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED PROJECT_NAME)
    set(BCOS_EVM_REF_STANDALONE ON)
else()
    set(BCOS_EVM_REF_STANDALONE OFF)
endif()

project(bcos-evm-ref LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(evmone CONFIG REQUIRED)

add_library(bcos-evm-ref-eth STATIC eth/EthTransition.cpp)
add_library(bcosevmref::eth ALIAS bcos-evm-ref-eth)
# PUBLIC include = bcos-evm-ref/include，使消费者以 <bcos-evm-ref/eth/EthTransition.h> 引用，
# 且编译期强制隔离（无法静默 #include <bcos-evm/...> 越界到仓库根）。
target_include_directories(bcos-evm-ref-eth PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(bcos-evm-ref-eth PUBLIC evmone::state evmone::evmone evmone::testutils)

option(BCOS_EVM_REF_TESTS "Build bcos-evm-ref tests" ${BCOS_EVM_REF_STANDALONE})
if(BCOS_EVM_REF_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

# M3.5 Phase 1 spike：读放大测量工具（非测试目标，不进 ctest）
option(BCOS_EVM_REF_SPIKE "Build bcos-evm-ref spike tools" ${BCOS_EVM_REF_STANDALONE})
if(BCOS_EVM_REF_SPIKE)
    add_executable(bcos-evm-ref-read-amplification spike/ReadAmplification.cpp)
    target_link_libraries(bcos-evm-ref-read-amplification PRIVATE bcosevmref::eth)
endif()
