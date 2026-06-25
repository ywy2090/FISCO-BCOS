# bcos-evm state tests.

set(TEST_BINARY_NAME StateJournalRevertTest)

add_executable(${TEST_BINARY_NAME}
    state/StateJournalRevertTest.cpp
)

target_include_directories(${TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${TEST_BINARY_NAME} PRIVATE
    bcos-evm
)

add_test(
    NAME StateJournalRevert
    COMMAND ${TEST_BINARY_NAME}
)

set(STATE_REFUND_TEST_BINARY_NAME StateRefundTest)

add_executable(${STATE_REFUND_TEST_BINARY_NAME}
    state/StateRefundTest.cpp
)

target_link_libraries(${STATE_REFUND_TEST_BINARY_NAME} PRIVATE
    bcos-evm
)

add_test(
    NAME StateRefund
    COMMAND ${STATE_REFUND_TEST_BINARY_NAME}
)

set(SMOKE_TEST_BINARY_NAME StateHostSmokeTest)

add_executable(${SMOKE_TEST_BINARY_NAME}
    state/StateHostSmokeTest.cpp
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

set(WARM_ENTRY_TEST_BINARY_NAME WarmTransactionEntryTest)

add_executable(${WARM_ENTRY_TEST_BINARY_NAME}
    state/WarmTransactionEntryTest.cpp
)

target_include_directories(${WARM_ENTRY_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${WARM_ENTRY_TEST_BINARY_NAME} PRIVATE
    bcos-evm
    ledger
)

add_test(
    NAME WarmTransactionEntry
    COMMAND ${WARM_ENTRY_TEST_BINARY_NAME}
)

set(EIP2929_ACCESS_HOST_TEST_BINARY_NAME Eip2929AccessHostTest)

add_executable(${EIP2929_ACCESS_HOST_TEST_BINARY_NAME}
    state/Eip2929AccessHostTest.cpp
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
set(SSTORE_STATUS_TEST_BINARY_NAME SstoreStatusTest)

add_executable(${SSTORE_STATUS_TEST_BINARY_NAME}
    state/SstoreStatusTest.cpp
)

target_include_directories(${SSTORE_STATUS_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${SSTORE_STATUS_TEST_BINARY_NAME} PRIVATE
    bcos-evm
)

add_test(
    NAME SstoreStatus
    COMMAND ${SSTORE_STATUS_TEST_BINARY_NAME}
)

set(SSTORE_REFUND_TEST_BINARY_NAME SstoreRefundTest)

add_executable(${SSTORE_REFUND_TEST_BINARY_NAME}
    state/SstoreRefundTest.cpp
)

target_include_directories(${SSTORE_REFUND_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${SSTORE_REFUND_TEST_BINARY_NAME} PRIVATE
    bcos-evm
)

add_test(
    NAME SstoreRefund
    COMMAND ${SSTORE_REFUND_TEST_BINARY_NAME}
)

set(PRAGUE_STATE_TEST_BINARY_NAME PragueStateTest)

add_executable(${PRAGUE_STATE_TEST_BINARY_NAME}
    state/PragueStateTest.cpp
)

target_include_directories(${PRAGUE_STATE_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_compile_definitions(${PRAGUE_STATE_TEST_BINARY_NAME} PRIVATE
    PRAGUE_STATE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/state"
)

target_link_libraries(${PRAGUE_STATE_TEST_BINARY_NAME} PRIVATE
    bcos-evm-eth
    bcos-evm-test-state
    evmone::evmone
    bcos-task
    bcos-framework
    ledger
    bcos-protocol
    bcos-utilities
)

add_test(
    NAME PragueState
    COMMAND ${PRAGUE_STATE_TEST_BINARY_NAME}
)

set(NESTED_CALL_HOST_TEST_BINARY_NAME NestedCallHostTest)

add_executable(${NESTED_CALL_HOST_TEST_BINARY_NAME}
    state/NestedCallHostTest.cpp
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
    state/PrecompileInCallTest.cpp
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
    state/BlockHashHostTest.cpp
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
    state/NestedRevertWarmTest.cpp
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
    state/CreateWarmPinRevertTest.cpp
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

set(EVMONE_REFUND_SPIKE_TEST_BINARY_NAME EvmoneRefundSpikeTest)

add_executable(${EVMONE_REFUND_SPIKE_TEST_BINARY_NAME}
    opstack/EvmoneRefundSpikeTest.cpp
)

target_include_directories(${EVMONE_REFUND_SPIKE_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${EVMONE_REFUND_SPIKE_TEST_BINARY_NAME} PRIVATE
    bcos-evm-eth
    evmone::evmone
    bcos-task
    bcos-framework
    ledger
    bcos-protocol
    bcos-utilities
)

add_test(
    NAME EvmoneRefundSpike
    COMMAND ${EVMONE_REFUND_SPIKE_TEST_BINARY_NAME}
)

