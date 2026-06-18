cmake_minimum_required(VERSION 3.28)

include(CTest)
enable_testing()

set(TEST_BINARY_NAME StateJournalRevertTest)

add_executable(${TEST_BINARY_NAME}
    StateJournalRevertTest.cpp
)

target_link_libraries(${TEST_BINARY_NAME} PRIVATE
    bcos-evm
)

add_test(
    NAME StateJournalRevert
    COMMAND ${TEST_BINARY_NAME}
)

set(HOOKS_TEST_BINARY_NAME EthHostExtensionHooksTest)

add_executable(${HOOKS_TEST_BINARY_NAME}
    EthHostExtensionHooksTest.cpp
)

target_link_libraries(${HOOKS_TEST_BINARY_NAME} PRIVATE
    bcos-evm
)

add_test(
    NAME EthHostExtensionHooks
    COMMAND ${HOOKS_TEST_BINARY_NAME}
)
