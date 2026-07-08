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
# PUBLIC include = 仓库根，使消费者以 <bcos-evm-ref/eth/EthTransition.h> 引用
target_include_directories(bcos-evm-ref-eth PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(bcos-evm-ref-eth PUBLIC evmone::state evmone::evmone)

option(BCOS_EVM_REF_TESTS "Build bcos-evm-ref tests" ${BCOS_EVM_REF_STANDALONE})
if(BCOS_EVM_REF_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
