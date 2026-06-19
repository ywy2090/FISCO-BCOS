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

set(FISCO_HOST_EXTENSION_TEST_BINARY_NAME FiscoHostExtensionTest)

add_executable(${FISCO_HOST_EXTENSION_TEST_BINARY_NAME}
    FiscoHostExtensionTest.cpp
)

target_link_libraries(${FISCO_HOST_EXTENSION_TEST_BINARY_NAME} PRIVATE
    bcos-evm
)

add_test(
    NAME FiscoHostExtension
    COMMAND ${FISCO_HOST_EXTENSION_TEST_BINARY_NAME}
)

set(EXECUTE_VIA_HOST_SMOKE_TEST_BINARY_NAME ExecuteViaHostSmokeTest)

add_executable(${EXECUTE_VIA_HOST_SMOKE_TEST_BINARY_NAME}
    ExecuteViaHostSmokeTest.cpp
)

target_include_directories(${EXECUTE_VIA_HOST_SMOKE_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${EXECUTE_VIA_HOST_SMOKE_TEST_BINARY_NAME} PRIVATE
    bcos-evm
)

add_test(
    NAME ExecuteViaHostSmoke
    COMMAND ${EXECUTE_VIA_HOST_SMOKE_TEST_BINARY_NAME}
)

set(EXECUTE_MESSAGE_SMOKE_TEST_BINARY_NAME ExecuteMessageSmokeTest)

add_executable(${EXECUTE_MESSAGE_SMOKE_TEST_BINARY_NAME}
    ExecuteMessageSmokeTest.cpp
)

target_include_directories(${EXECUTE_MESSAGE_SMOKE_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${EXECUTE_MESSAGE_SMOKE_TEST_BINARY_NAME} PRIVATE
    bcos-evm
)

add_test(
    NAME ExecuteMessageSmoke
    COMMAND ${EXECUTE_MESSAGE_SMOKE_TEST_BINARY_NAME}
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

set(PRAGUE_STATE_TEST_BINARY_NAME PragueStateTest)

add_executable(${PRAGUE_STATE_TEST_BINARY_NAME}
    state/PragueStateTest.cpp
    ../eth/executeMessage.cpp
    ../eth/state/State.cpp
    ../eth/state/EthHost.cpp
    ../eth/state/bloom_filter.cpp
    ../eth/state/EthPrecompiles.cpp
    ../eth/state/transition.cpp
    ../eth/precompiled/EthBuiltinRegistry.cpp
    ../eth/precompiled/PrecompiledContract.cpp
    ../eth/precompiled/ModexpGas.cpp
)

target_include_directories(${PRAGUE_STATE_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_compile_definitions(${PRAGUE_STATE_TEST_BINARY_NAME} PRIVATE
    PRAGUE_STATE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/state"
)

target_link_libraries(${PRAGUE_STATE_TEST_BINARY_NAME} PRIVATE
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
    ../eth/executeMessage.cpp
    ../eth/state/State.cpp
    ../eth/state/EthHost.cpp
    ../eth/state/bloom_filter.cpp
    ../eth/state/EthPrecompiles.cpp
    ../eth/state/transition.cpp
    ../eth/precompiled/EthBuiltinRegistry.cpp
    ../eth/precompiled/PrecompiledContract.cpp
    ../eth/precompiled/ModexpGas.cpp
)

target_include_directories(${NESTED_CALL_HOST_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${NESTED_CALL_HOST_TEST_BINARY_NAME} PRIVATE
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
    ../eth/executeMessage.cpp
    ../eth/state/State.cpp
    ../eth/state/EthHost.cpp
    ../eth/state/bloom_filter.cpp
    ../eth/state/EthPrecompiles.cpp
    ../eth/state/transition.cpp
    ../eth/precompiled/EthBuiltinRegistry.cpp
    ../eth/precompiled/PrecompiledContract.cpp
    ../eth/precompiled/ModexpGas.cpp
)

target_include_directories(${PRECOMPILE_IN_CALL_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${PRECOMPILE_IN_CALL_TEST_BINARY_NAME} PRIVATE
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
    ../eth/executeMessage.cpp
    ../eth/state/State.cpp
    ../eth/state/EthHost.cpp
    ../eth/state/bloom_filter.cpp
    ../eth/state/EthPrecompiles.cpp
    ../eth/state/transition.cpp
    ../eth/precompiled/EthBuiltinRegistry.cpp
    ../eth/precompiled/PrecompiledContract.cpp
    ../eth/precompiled/ModexpGas.cpp
)

target_include_directories(${BLOCK_HASH_HOST_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${BLOCK_HASH_HOST_TEST_BINARY_NAME} PRIVATE
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
    ../eth/executeMessage.cpp
    ../eth/state/State.cpp
    ../eth/state/EthHost.cpp
    ../eth/state/bloom_filter.cpp
    ../eth/state/EthPrecompiles.cpp
    ../eth/state/transition.cpp
    ../eth/precompiled/EthBuiltinRegistry.cpp
    ../eth/precompiled/PrecompiledContract.cpp
    ../eth/precompiled/ModexpGas.cpp
)

target_include_directories(${NESTED_REVERT_WARM_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${NESTED_REVERT_WARM_TEST_BINARY_NAME} PRIVATE
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

set(EVMONE_REFUND_SPIKE_TEST_BINARY_NAME EvmoneRefundSpikeTest)

add_executable(${EVMONE_REFUND_SPIKE_TEST_BINARY_NAME}
    opstack/EvmoneRefundSpikeTest.cpp
    ../eth/executeMessage.cpp
    ../eth/state/State.cpp
    ../eth/state/EthHost.cpp
    ../eth/state/bloom_filter.cpp
    ../eth/state/EthPrecompiles.cpp
    ../eth/state/transition.cpp
    ../eth/precompiled/EthBuiltinRegistry.cpp
    ../eth/precompiled/PrecompiledContract.cpp
    ../eth/precompiled/ModexpGas.cpp
)

target_include_directories(${EVMONE_REFUND_SPIKE_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${EVMONE_REFUND_SPIKE_TEST_BINARY_NAME} PRIVATE
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

set(OPSTACK_EXECUTE_VIA_HOST_SMOKE_TEST_BINARY_NAME OpStackExecuteViaHostSmokeTest)

add_executable(${OPSTACK_EXECUTE_VIA_HOST_SMOKE_TEST_BINARY_NAME}
    opstack/OpStackExecuteViaHostSmokeTest.cpp
)

target_include_directories(${OPSTACK_EXECUTE_VIA_HOST_SMOKE_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${OPSTACK_EXECUTE_VIA_HOST_SMOKE_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME OpStackExecuteViaHost
    COMMAND ${OPSTACK_EXECUTE_VIA_HOST_SMOKE_TEST_BINARY_NAME}
)

set(ROLLUP_COST_TEST_BINARY_NAME RollupCostTest)

add_executable(${ROLLUP_COST_TEST_BINARY_NAME}
    opstack/RollupCostTest.cpp
)

target_include_directories(${ROLLUP_COST_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${ROLLUP_COST_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME RollupCost
    COMMAND ${ROLLUP_COST_TEST_BINARY_NAME}
)

set(OPSTACK_FEE_TEST_BINARY_NAME OpStackFeeTest)

add_executable(${OPSTACK_FEE_TEST_BINARY_NAME}
    opstack/OpStackFeeTest.cpp
)

target_include_directories(${OPSTACK_FEE_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_compile_definitions(${OPSTACK_FEE_TEST_BINARY_NAME} PRIVATE
    OPSTACK_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/opstack"
)

target_link_libraries(${OPSTACK_FEE_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME OpStackFee
    COMMAND ${OPSTACK_FEE_TEST_BINARY_NAME}
)

set(OPSTACK_FLOOR_GAS_TEST_BINARY_NAME OpStackFloorGasTest)

add_executable(${OPSTACK_FLOOR_GAS_TEST_BINARY_NAME}
    opstack/OpStackFloorGasTest.cpp
)

target_include_directories(${OPSTACK_FLOOR_GAS_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${OPSTACK_FLOOR_GAS_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME OpStackFloorGas
    COMMAND ${OPSTACK_FLOOR_GAS_TEST_BINARY_NAME}
)
