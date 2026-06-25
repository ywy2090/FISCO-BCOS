# EthHost integration tests (EIP-2929 warm access, nested calls, precompile routing).

set(SMOKE_TEST_BINARY_NAME StateHostSmokeTest)

add_executable(${SMOKE_TEST_BINARY_NAME}
    eth/host/StateHostSmokeTest.cpp
)

target_include_directories(${SMOKE_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${SMOKE_TEST_BINARY_NAME} PRIVATE
    bcos-evm
    bcos-evm-test-state
)

add_test(
    NAME StateHostSmoke
    COMMAND ${SMOKE_TEST_BINARY_NAME}
)

set(EIP2929_ACCESS_HOST_TEST_BINARY_NAME Eip2929AccessHostTest)

add_executable(${EIP2929_ACCESS_HOST_TEST_BINARY_NAME}
    eth/host/Eip2929AccessHostTest.cpp
)

target_include_directories(${EIP2929_ACCESS_HOST_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${EIP2929_ACCESS_HOST_TEST_BINARY_NAME} PRIVATE
    bcos-evm
)

add_test(
    NAME Eip2929AccessHost
    COMMAND ${EIP2929_ACCESS_HOST_TEST_BINARY_NAME}
)

set(NESTED_CALL_HOST_TEST_BINARY_NAME NestedCallHostTest)

add_executable(${NESTED_CALL_HOST_TEST_BINARY_NAME}
    eth/host/NestedCallHostTest.cpp
)

target_include_directories(${NESTED_CALL_HOST_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${NESTED_CALL_HOST_TEST_BINARY_NAME} PRIVATE
    bcos-evm-eth
    evmone::evmone
    bcos-task
    bcos-framework
    ledger
    bcos-protocol
    bcos-utilities
)

add_test(
    NAME NestedCallHost
    COMMAND ${NESTED_CALL_HOST_TEST_BINARY_NAME}
)

set(PRECOMPILE_IN_CALL_TEST_BINARY_NAME PrecompileInCallTest)

add_executable(${PRECOMPILE_IN_CALL_TEST_BINARY_NAME}
    eth/host/PrecompileInCallTest.cpp
)

target_include_directories(${PRECOMPILE_IN_CALL_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${PRECOMPILE_IN_CALL_TEST_BINARY_NAME} PRIVATE
    bcos-evm-eth
    evmone::evmone
    bcos-task
    bcos-framework
    ledger
    bcos-protocol
    bcos-utilities
)

add_test(
    NAME PrecompileInCall
    COMMAND ${PRECOMPILE_IN_CALL_TEST_BINARY_NAME}
)

set(BLOCK_HASH_HOST_TEST_BINARY_NAME BlockHashHostTest)

add_executable(${BLOCK_HASH_HOST_TEST_BINARY_NAME}
    eth/host/BlockHashHostTest.cpp
)

target_include_directories(${BLOCK_HASH_HOST_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${BLOCK_HASH_HOST_TEST_BINARY_NAME} PRIVATE
    bcos-evm-eth
    evmone::evmone
    bcos-task
    bcos-framework
    ledger
    bcos-protocol
    bcos-utilities
)

add_test(
    NAME BlockHashHost
    COMMAND ${BLOCK_HASH_HOST_TEST_BINARY_NAME}
)

set(NESTED_REVERT_WARM_TEST_BINARY_NAME NestedRevertWarmTest)

add_executable(${NESTED_REVERT_WARM_TEST_BINARY_NAME}
    eth/host/NestedRevertWarmTest.cpp
)

target_include_directories(${NESTED_REVERT_WARM_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${NESTED_REVERT_WARM_TEST_BINARY_NAME} PRIVATE
    bcos-evm-eth
    evmone::evmone
    bcos-task
    bcos-framework
    ledger
    bcos-protocol
    bcos-utilities
)

add_test(
    NAME NestedRevertWarm
    COMMAND ${NESTED_REVERT_WARM_TEST_BINARY_NAME}
)

set(CREATE_WARM_PIN_REVERT_TEST_BINARY_NAME CreateWarmPinRevertTest)

add_executable(${CREATE_WARM_PIN_REVERT_TEST_BINARY_NAME}
    eth/host/CreateWarmPinRevertTest.cpp
    ../eth/state/State.cpp
)

target_include_directories(${CREATE_WARM_PIN_REVERT_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${CREATE_WARM_PIN_REVERT_TEST_BINARY_NAME} PRIVATE
    evmone::evmone
    bcos-utilities
)

add_test(
    NAME CreateWarmPinRevert
    COMMAND ${CREATE_WARM_PIN_REVERT_TEST_BINARY_NAME}
)
