# Shared helpers for bcos-evm unit tests.

include(${CMAKE_CURRENT_LIST_DIR}/EvmCallStack.cmake)

add_library(bcos-evm-test-state STATIC
    helpers/BloomFilter.cpp
    helpers/Transition.cpp
)

target_include_directories(bcos-evm-test-state PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(bcos-evm-test-state PUBLIC
    bcos-evm-eth
)

function(add_te_input_builder_test NAME SOURCE)
    add_executable(${NAME} ${SOURCE})
    target_include_directories(${NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/transaction-executor)
    target_link_libraries(${NAME} PRIVATE
        bcos-evm bcos-evm-op executor protocol-tars bcos-crypto codec)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

