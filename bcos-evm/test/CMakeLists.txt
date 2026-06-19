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
    ../eth/executeMessage.cpp
    ../eth/Eip7702.cpp
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

set(DEPOSIT_TX_PRECHECK_TEST_BINARY_NAME DepositTxPreCheckTest)

add_executable(${DEPOSIT_TX_PRECHECK_TEST_BINARY_NAME}
    opstack/DepositTxPreCheckTest.cpp
)

target_include_directories(${DEPOSIT_TX_PRECHECK_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${DEPOSIT_TX_PRECHECK_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME DepositTxPreCheck
    COMMAND ${DEPOSIT_TX_PRECHECK_TEST_BINARY_NAME}
)

set(DEPOSIT_MINT_TEST_BINARY_NAME DepositMintTest)

add_executable(${DEPOSIT_MINT_TEST_BINARY_NAME}
    opstack/DepositMintTest.cpp
)

target_include_directories(${DEPOSIT_MINT_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${DEPOSIT_MINT_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME DepositMint
    COMMAND ${DEPOSIT_MINT_TEST_BINARY_NAME}
)

set(DEPOSIT_NO_FEE_ROUTING_TEST_BINARY_NAME DepositNoFeeRoutingTest)

add_executable(${DEPOSIT_NO_FEE_ROUTING_TEST_BINARY_NAME}
    opstack/DepositNoFeeRoutingTest.cpp
)

target_include_directories(${DEPOSIT_NO_FEE_ROUTING_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${DEPOSIT_NO_FEE_ROUTING_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME DepositNoFeeRouting
    COMMAND ${DEPOSIT_NO_FEE_ROUTING_TEST_BINARY_NAME}
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

set(CALC_REFUND_TEST_BINARY_NAME CalcRefundTest)

add_executable(${CALC_REFUND_TEST_BINARY_NAME}
    opstack/CalcRefundTest.cpp
)

target_include_directories(${CALC_REFUND_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${CALC_REFUND_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME CalcRefund
    COMMAND ${CALC_REFUND_TEST_BINARY_NAME}
)

set(REFUND_ISTHMUS_TEST_BINARY_NAME RefundIsthmusTest)

add_executable(${REFUND_ISTHMUS_TEST_BINARY_NAME}
    opstack/RefundIsthmusTest.cpp
)

target_include_directories(${REFUND_ISTHMUS_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${REFUND_ISTHMUS_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME RefundIsthmus
    COMMAND ${REFUND_ISTHMUS_TEST_BINARY_NAME}
)

set(OPSTACK_SETTLEMENT_TEST_BINARY_NAME OpStackSettlementTest)

add_executable(${OPSTACK_SETTLEMENT_TEST_BINARY_NAME}
    opstack/OpStackSettlementTest.cpp
)

target_include_directories(${OPSTACK_SETTLEMENT_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${OPSTACK_SETTLEMENT_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME OpStackSettlement
    COMMAND ${OPSTACK_SETTLEMENT_TEST_BINARY_NAME}
)

set(L1BLOCK_PREDEPLOY_TEST_BINARY_NAME L1BlockPredeployTest)

add_executable(${L1BLOCK_PREDEPLOY_TEST_BINARY_NAME}
    opstack/L1BlockPredeployTest.cpp
)

target_include_directories(${L1BLOCK_PREDEPLOY_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_compile_definitions(${L1BLOCK_PREDEPLOY_TEST_BINARY_NAME} PRIVATE
    OPSTACK_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/opstack"
)

target_link_libraries(${L1BLOCK_PREDEPLOY_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME L1BlockPredeploy
    COMMAND ${L1BLOCK_PREDEPLOY_TEST_BINARY_NAME}
)

set(EMPTY_CODE_HOOK_TEST_BINARY_NAME EmptyCodeHookTest)

add_executable(${EMPTY_CODE_HOOK_TEST_BINARY_NAME}
    opstack/EmptyCodeHookTest.cpp
)

target_include_directories(${EMPTY_CODE_HOOK_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${EMPTY_CODE_HOOK_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME EmptyCodeHook
    COMMAND ${EMPTY_CODE_HOOK_TEST_BINARY_NAME}
)

set(L1_ATTRIBUTES_DEPOSIT_TEST_BINARY_NAME L1AttributesDepositTest)

add_executable(${L1_ATTRIBUTES_DEPOSIT_TEST_BINARY_NAME}
    opstack/L1AttributesDepositTest.cpp
)

target_include_directories(${L1_ATTRIBUTES_DEPOSIT_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_compile_definitions(${L1_ATTRIBUTES_DEPOSIT_TEST_BINARY_NAME} PRIVATE
    OPSTACK_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/opstack"
)

target_link_libraries(${L1_ATTRIBUTES_DEPOSIT_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME L1AttributesDeposit
    COMMAND ${L1_ATTRIBUTES_DEPOSIT_TEST_BINARY_NAME}
)

set(EIP7702_PRECHECK_TEST_BINARY_NAME Eip7702PreCheckTest)

add_executable(${EIP7702_PRECHECK_TEST_BINARY_NAME}
    opstack/Eip7702PreCheckTest.cpp
)

target_include_directories(${EIP7702_PRECHECK_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${EIP7702_PRECHECK_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME Eip7702PreCheck
    COMMAND ${EIP7702_PRECHECK_TEST_BINARY_NAME}
)

set(EIP7702_DELEGATION_SENDER_TEST_BINARY_NAME Eip7702DelegationSenderTest)

add_executable(${EIP7702_DELEGATION_SENDER_TEST_BINARY_NAME}
    opstack/Eip7702DelegationSenderTest.cpp
)

target_include_directories(${EIP7702_DELEGATION_SENDER_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${EIP7702_DELEGATION_SENDER_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME Eip7702DelegationSender
    COMMAND ${EIP7702_DELEGATION_SENDER_TEST_BINARY_NAME}
)

set(EIP7702_APPLY_AUTH_TEST_BINARY_NAME Eip7702ApplyAuthorizationTest)

add_executable(${EIP7702_APPLY_AUTH_TEST_BINARY_NAME}
    opstack/Eip7702ApplyAuthorizationTest.cpp
)

target_include_directories(${EIP7702_APPLY_AUTH_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${EIP7702_APPLY_AUTH_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME Eip7702ApplyAuthorization
    COMMAND ${EIP7702_APPLY_AUTH_TEST_BINARY_NAME}
)

set(EIP7702_CLEAR_DELEGATION_TEST_BINARY_NAME Eip7702ClearDelegationTest)

add_executable(${EIP7702_CLEAR_DELEGATION_TEST_BINARY_NAME}
    opstack/Eip7702ClearDelegationTest.cpp
)

target_include_directories(${EIP7702_CLEAR_DELEGATION_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${EIP7702_CLEAR_DELEGATION_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME Eip7702ClearDelegation
    COMMAND ${EIP7702_CLEAR_DELEGATION_TEST_BINARY_NAME}
)

set(CAN_TRANSFER_TEST_BINARY_NAME CanTransferTest)

add_executable(${CAN_TRANSFER_TEST_BINARY_NAME}
    opstack/CanTransferTest.cpp
)

target_include_directories(${CAN_TRANSFER_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${CAN_TRANSFER_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME CanTransfer
    COMMAND ${CAN_TRANSFER_TEST_BINARY_NAME}
)

set(ISTHMUS_POST_EXECUTION_POLICY_TEST_BINARY_NAME IsthmusPostExecutionPolicyTest)

add_executable(${ISTHMUS_POST_EXECUTION_POLICY_TEST_BINARY_NAME}
    opstack/IsthmusPostExecutionPolicyTest.cpp
)

target_include_directories(${ISTHMUS_POST_EXECUTION_POLICY_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${ISTHMUS_POST_EXECUTION_POLICY_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME IsthmusPostExecutionPolicy
    COMMAND ${ISTHMUS_POST_EXECUTION_POLICY_TEST_BINARY_NAME}
)

set(L1_ATTRIBUTES_DEPOSIT_FAILURE_TEST_BINARY_NAME L1AttributesDepositFailureTest)

add_executable(${L1_ATTRIBUTES_DEPOSIT_FAILURE_TEST_BINARY_NAME}
    opstack/L1AttributesDepositFailureTest.cpp
)

target_include_directories(${L1_ATTRIBUTES_DEPOSIT_FAILURE_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_compile_definitions(${L1_ATTRIBUTES_DEPOSIT_FAILURE_TEST_BINARY_NAME} PRIVATE
    OPSTACK_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/opstack"
)

target_link_libraries(${L1_ATTRIBUTES_DEPOSIT_FAILURE_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME L1AttributesDepositFailure
    COMMAND ${L1_ATTRIBUTES_DEPOSIT_FAILURE_TEST_BINARY_NAME}
)

set(GAS_FEE_CAP_BALANCE_TEST_BINARY_NAME GasFeeCapBalanceTest)

add_executable(${GAS_FEE_CAP_BALANCE_TEST_BINARY_NAME}
    opstack/GasFeeCapBalanceTest.cpp
)

target_include_directories(${GAS_FEE_CAP_BALANCE_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${GAS_FEE_CAP_BALANCE_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME GasFeeCapBalance
    COMMAND ${GAS_FEE_CAP_BALANCE_TEST_BINARY_NAME}
)

set(BLOB_GAS_BALANCE_TEST_BINARY_NAME BlobGasBalanceTest)

add_executable(${BLOB_GAS_BALANCE_TEST_BINARY_NAME}
    opstack/BlobGasBalanceTest.cpp
)

target_include_directories(${BLOB_GAS_BALANCE_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${BLOB_GAS_BALANCE_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME BlobGasBalance
    COMMAND ${BLOB_GAS_BALANCE_TEST_BINARY_NAME}
)

set(L1BLOCK_GETTER_TEST_BINARY_NAME L1BlockGetterTest)

add_executable(${L1BLOCK_GETTER_TEST_BINARY_NAME}
    opstack/L1BlockGetterTest.cpp
)

target_include_directories(${L1BLOCK_GETTER_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${L1BLOCK_GETTER_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME L1BlockGetter
    COMMAND ${L1BLOCK_GETTER_TEST_BINARY_NAME}
)

add_executable(ExecuteViaEthFixtureTest eth/ExecuteViaEthFixtureTest.cpp)
target_include_directories(ExecuteViaEthFixtureTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_compile_definitions(ExecuteViaEthFixtureTest PRIVATE
    ETH_STATE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/state")
target_link_libraries(ExecuteViaEthFixtureTest PRIVATE
    bcos-evm-eth evmone::evmone bcos-task bcos-crypto)
add_test(NAME ExecuteViaEthFixture COMMAND ExecuteViaEthFixtureTest)
