# bcos-evm state tests (journal, refund, sstore — no EthHost).

set(TEST_BINARY_NAME StateJournalRevertTest)

add_executable(${TEST_BINARY_NAME}
    state/StateJournalRevertTest.cpp
)

target_include_directories(${TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${TEST_BINARY_NAME} PRIVATE
    bcos-evm-eth
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

set(PREPARE_STATE_TEST_BINARY_NAME PrepareStateTest)

add_executable(${PREPARE_STATE_TEST_BINARY_NAME}
    state/PrepareStateTest.cpp
)

target_include_directories(${PREPARE_STATE_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${PREPARE_STATE_TEST_BINARY_NAME} PRIVATE
    bcos-evm
    ledger
)

add_test(
    NAME PrepareState
    COMMAND ${PREPARE_STATE_TEST_BINARY_NAME}
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
