cmake_minimum_required(VERSION 3.28)

include(CTest)
enable_testing()

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

set(HOOKS_TEST_BINARY_NAME EthHostExtensionHooksTest)

add_executable(${HOOKS_TEST_BINARY_NAME}
    eth/EthHostExtensionHooksTest.cpp
)

target_include_directories(${HOOKS_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${HOOKS_TEST_BINARY_NAME} PRIVATE
    bcos-evm
)

add_test(
    NAME EthHostExtensionHooks
    COMMAND ${HOOKS_TEST_BINARY_NAME}
)

set(CHAIN_PRECOMPILE_PORT_TEST_BINARY_NAME ChainPrecompilePortTest)

add_executable(${CHAIN_PRECOMPILE_PORT_TEST_BINARY_NAME}
    bcos/ChainPrecompilePortTest.cpp
)

target_include_directories(${CHAIN_PRECOMPILE_PORT_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${CHAIN_PRECOMPILE_PORT_TEST_BINARY_NAME} PRIVATE
    bcos-evm
)

add_test(
    NAME ChainPrecompilePort
    COMMAND ${CHAIN_PRECOMPILE_PORT_TEST_BINARY_NAME}
)

set(FISCO_HOST_EXTENSION_TEST_BINARY_NAME FiscoHostExtensionTest)

add_executable(${FISCO_HOST_EXTENSION_TEST_BINARY_NAME}
    bcos/FiscoHostExtensionTest.cpp
)

target_include_directories(${FISCO_HOST_EXTENSION_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
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
    bcos/ExecuteViaHostSmokeTest.cpp
)

target_include_directories(${EXECUTE_VIA_HOST_SMOKE_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
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
    eth/ExecuteMessageSmokeTest.cpp
)

target_include_directories(${EXECUTE_MESSAGE_SMOKE_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
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

add_executable(Eip2929OpcodeGasTest eth/Eip2929OpcodeGasTest.cpp)
target_include_directories(Eip2929OpcodeGasTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Eip2929OpcodeGasTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME Eip2929OpcodeGas COMMAND Eip2929OpcodeGasTest)

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
    ../eth/ExecuteMessage.cpp
    ../eth/Eip7702.cpp
    ../eth/state/State.cpp
    ../eth/state/EthHost.cpp
    ../eth/state/BloomFilter.cpp
    ../eth/state/EthPrecompiles.cpp
    ../eth/state/Transition.cpp
    ../eth/precompiled/EthBuiltinRegistry.cpp
    ../eth/precompiled/PrecompiledContract.cpp
    ../eth/precompiled/ModexpGas.cpp
    ../eth/precompiled/PrecompileRouter.cpp
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
    ../eth/ExecuteMessage.cpp
    ../eth/Eip7702.cpp
    ../eth/state/State.cpp
    ../eth/state/EthHost.cpp
    ../eth/state/BloomFilter.cpp
    ../eth/state/EthPrecompiles.cpp
    ../eth/state/Transition.cpp
    ../eth/precompiled/EthBuiltinRegistry.cpp
    ../eth/precompiled/PrecompiledContract.cpp
    ../eth/precompiled/ModexpGas.cpp
    ../eth/precompiled/PrecompileRouter.cpp
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
    ../eth/ExecuteMessage.cpp
    ../eth/Eip7702.cpp
    ../eth/state/State.cpp
    ../eth/state/EthHost.cpp
    ../eth/state/BloomFilter.cpp
    ../eth/state/EthPrecompiles.cpp
    ../eth/state/Transition.cpp
    ../eth/precompiled/EthBuiltinRegistry.cpp
    ../eth/precompiled/PrecompiledContract.cpp
    ../eth/precompiled/ModexpGas.cpp
    ../eth/precompiled/PrecompileRouter.cpp
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
    ../eth/ExecuteMessage.cpp
    ../eth/Eip7702.cpp
    ../eth/state/State.cpp
    ../eth/state/EthHost.cpp
    ../eth/state/BloomFilter.cpp
    ../eth/state/EthPrecompiles.cpp
    ../eth/state/Transition.cpp
    ../eth/precompiled/EthBuiltinRegistry.cpp
    ../eth/precompiled/PrecompiledContract.cpp
    ../eth/precompiled/ModexpGas.cpp
    ../eth/precompiled/PrecompileRouter.cpp
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
    ../eth/ExecuteMessage.cpp
    ../eth/Eip7702.cpp
    ../eth/state/State.cpp
    ../eth/state/EthHost.cpp
    ../eth/state/BloomFilter.cpp
    ../eth/state/EthPrecompiles.cpp
    ../eth/state/Transition.cpp
    ../eth/precompiled/EthBuiltinRegistry.cpp
    ../eth/precompiled/PrecompiledContract.cpp
    ../eth/precompiled/ModexpGas.cpp
    ../eth/precompiled/PrecompileRouter.cpp
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
    ../eth/ExecuteMessage.cpp
    ../eth/Eip7702.cpp
    ../eth/state/State.cpp
    ../eth/state/EthHost.cpp
    ../eth/state/BloomFilter.cpp
    ../eth/state/EthPrecompiles.cpp
    ../eth/state/Transition.cpp
    ../eth/precompiled/EthBuiltinRegistry.cpp
    ../eth/precompiled/PrecompiledContract.cpp
    ../eth/precompiled/ModexpGas.cpp
    ../eth/precompiled/PrecompileRouter.cpp
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

add_executable(OpStackIntrinsicGasSyncTest
    opstack/OpStackIntrinsicGasSyncTest.cpp
    ../opstack/OpStackExecuteViaHost.cpp
    ../opstack/OpStackPreCheck.cpp
    ../opstack/OpStackTxExecutor.cpp
    ../opstack/RollupCost.cpp
    ../opstack/OpStackFee.cpp
    ../opstack/OpStackFloorGas.cpp
    ../opstack/L1BlockStorage.cpp
    ../opstack/L1BlockPredeploy.cpp
    ../opstack/GasPriceOraclePredeploy.cpp
    ../opstack/OpStackPreDebitEntry.cpp)
target_compile_definitions(OpStackIntrinsicGasSyncTest PRIVATE BCOS_EVM_TESTING)
target_include_directories(OpStackIntrinsicGasSyncTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(OpStackIntrinsicGasSyncTest PRIVATE
    bcos-evm-eth bcos-task evmone::evmone bcos-framework ledger
    bcos-protocol bcos-utilities bcos-crypto codec)
add_test(NAME OpStackIntrinsicGasSync COMMAND OpStackIntrinsicGasSyncTest)

add_executable(OpStackPreDebitOrderTest opstack/OpStackPreDebitOrderTest.cpp)
target_include_directories(OpStackPreDebitOrderTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(OpStackPreDebitOrderTest PRIVATE bcos-evm-op)
add_test(NAME OpStackPreDebitOrder COMMAND OpStackPreDebitOrderTest)

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

set(OPSTACK_PRECHECK_4844_TEST_BINARY_NAME OpStackPreCheck4844Test)

add_executable(${OPSTACK_PRECHECK_4844_TEST_BINARY_NAME}
    opstack/OpStackPreCheck4844Test.cpp
)

target_include_directories(${OPSTACK_PRECHECK_4844_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${OPSTACK_PRECHECK_4844_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME OpStackPreCheck4844
    COMMAND ${OPSTACK_PRECHECK_4844_TEST_BINARY_NAME}
)

set(BLOCK_GAS_POOL_TEST_BINARY_NAME BlockGasPoolTest)

add_executable(${BLOCK_GAS_POOL_TEST_BINARY_NAME}
    opstack/BlockGasPoolTest.cpp
)

target_include_directories(${BLOCK_GAS_POOL_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${BLOCK_GAS_POOL_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME BlockGasPool
    COMMAND ${BLOCK_GAS_POOL_TEST_BINARY_NAME}
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

set(DEPOSIT_CREATE_NONCE_TEST_BINARY_NAME DepositCreateNonceTest)

add_executable(${DEPOSIT_CREATE_NONCE_TEST_BINARY_NAME}
    opstack/DepositCreateNonceTest.cpp
)

target_include_directories(${DEPOSIT_CREATE_NONCE_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${DEPOSIT_CREATE_NONCE_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME DepositCreateNonce
    COMMAND ${DEPOSIT_CREATE_NONCE_TEST_BINARY_NAME}
)

set(OPSTACK_BLOCK_HEADER_EXTENSION_TEST_BINARY_NAME OpStackBlockHeaderExtensionTest)

add_executable(${OPSTACK_BLOCK_HEADER_EXTENSION_TEST_BINARY_NAME}
    opstack/OpStackBlockHeaderExtensionTest.cpp
)

target_include_directories(${OPSTACK_BLOCK_HEADER_EXTENSION_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/bcos-tars-protocol
)

target_link_libraries(${OPSTACK_BLOCK_HEADER_EXTENSION_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
    protocol-tars
)

add_test(
    NAME OpStackBlockHeaderExtension
    COMMAND ${OPSTACK_BLOCK_HEADER_EXTENSION_TEST_BINARY_NAME}
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

set(OPSTACK_FORK_SCHEDULE_TEST_BINARY_NAME OpStackForkScheduleTest)
add_executable(${OPSTACK_FORK_SCHEDULE_TEST_BINARY_NAME} opstack/OpStackForkScheduleTest.cpp)
target_include_directories(${OPSTACK_FORK_SCHEDULE_TEST_BINARY_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(${OPSTACK_FORK_SCHEDULE_TEST_BINARY_NAME} PRIVATE bcos-evm-op)
add_test(NAME OpStackForkSchedule COMMAND ${OPSTACK_FORK_SCHEDULE_TEST_BINARY_NAME})

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

set(GPO_PREDEPLOY_TEST_BINARY_NAME GasPriceOraclePredeployTest)

add_executable(${GPO_PREDEPLOY_TEST_BINARY_NAME}
    opstack/GasPriceOraclePredeployTest.cpp
)

target_include_directories(${GPO_PREDEPLOY_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${GPO_PREDEPLOY_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME GasPriceOraclePredeploy
    COMMAND ${GPO_PREDEPLOY_TEST_BINARY_NAME}
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

add_executable(EthIncludedTxVmerrTest eth/EthIncludedTxVmerrTest.cpp)
target_include_directories(EthIncludedTxVmerrTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(EthIncludedTxVmerrTest PRIVATE
    bcos-evm-eth evmone::evmone bcos-task bcos-crypto)
add_test(NAME EthIncludedTxVmerr COMMAND EthIncludedTxVmerrTest)

add_executable(EthEip1559GasTest eth/EthEip1559GasTest.cpp)
target_include_directories(EthEip1559GasTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(EthEip1559GasTest PRIVATE bcos-evm-eth bcos-utilities)
add_test(NAME EthEip1559Gas COMMAND EthEip1559GasTest)

add_executable(Web3TypedTxKindTest eth/Web3TypedTxKindTest.cpp)
target_include_directories(Web3TypedTxKindTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Web3TypedTxKindTest PRIVATE bcos-evm-eth bcos-utilities)
add_test(NAME Web3TypedTxKind COMMAND Web3TypedTxKindTest)

add_executable(EthExecuteViaEth1559GasPriceTest eth/EthExecuteViaEth1559GasPriceTest.cpp)
target_include_directories(EthExecuteViaEth1559GasPriceTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(EthExecuteViaEth1559GasPriceTest PRIVATE
    bcos-evm-eth evmone::evmone bcos-task bcos-crypto)
add_test(NAME EthExecuteViaEth1559GasPrice COMMAND EthExecuteViaEth1559GasPriceTest)

add_executable(OrchestrationPipelineTest eth/OrchestrationPipelineTest.cpp)
target_include_directories(OrchestrationPipelineTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(OrchestrationPipelineTest PRIVATE
    bcos-evm-eth evmone::evmone bcos-task bcos-crypto)
add_test(NAME OrchestrationPipeline COMMAND OrchestrationPipelineTest)

add_executable(OpStack7702ExecuteViaHostPropagationTest opstack/OpStack7702ExecuteViaHostPropagationTest.cpp)
target_include_directories(OpStack7702ExecuteViaHostPropagationTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(OpStack7702ExecuteViaHostPropagationTest PRIVATE
    bcos-evm-op bcos-task evmone::evmone)
add_test(NAME OpStack7702ExecuteViaHostPropagation COMMAND OpStack7702ExecuteViaHostPropagationTest)

add_executable(OpStack67802537KernelSmokeTest opstack/OpStack67802537KernelSmokeTest.cpp)
target_include_directories(OpStack67802537KernelSmokeTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(OpStack67802537KernelSmokeTest PRIVATE
    bcos-evm-op bcos-task evmone::evmone)
add_test(NAME OpStack67802537KernelSmoke COMMAND OpStack67802537KernelSmokeTest)

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

add_te_input_builder_test(EthTxInputBuilderTest eth/EthTxInputBuilderTest.cpp)
add_te_input_builder_test(FiscoTxInputBuilderTest eth/FiscoTxInputBuilderTest.cpp)
add_te_input_builder_test(OpStackTxInputBuilderTest opstack/OpStackTxInputBuilderTest.cpp)
target_include_directories(OpStackTxInputBuilderTest PRIVATE ${CMAKE_SOURCE_DIR}/bcos-rpc)
target_link_libraries(OpStackTxInputBuilderTest PRIVATE rpc)

add_executable(RevisionConfigProfileTest eth/RevisionConfigProfileTest.cpp)
target_include_directories(RevisionConfigProfileTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(RevisionConfigProfileTest PRIVATE
    bcos-evm bcos-evm-eth protocol-tars bcos-framework)
add_test(NAME RevisionConfigProfile COMMAND RevisionConfigProfileTest)

add_executable(Eip7702ApplyAuthorizationEthTest eth/Eip7702ApplyAuthorizationEthTest.cpp)
target_include_directories(Eip7702ApplyAuthorizationEthTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Eip7702ApplyAuthorizationEthTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME Eip7702ApplyAuthorizationEth COMMAND Eip7702ApplyAuthorizationEthTest)

add_executable(TxFeaturePrepareTest eth/TxFeaturePrepareTest.cpp)
target_include_directories(TxFeaturePrepareTest PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(TxFeaturePrepareTest PRIVATE bcos-evm-eth)
add_test(NAME TxFeaturePrepare COMMAND TxFeaturePrepareTest)

add_executable(OpStackTxPropsTest opstack/OpStackTxPropsTest.cpp)
target_include_directories(OpStackTxPropsTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/transaction-executor)
target_link_libraries(OpStackTxPropsTest PRIVATE bcos-evm-op bcos-evm-eth)
add_test(NAME OpStackTxProps COMMAND OpStackTxPropsTest)

add_executable(Bcos21000GasDeviationTest bcos/Bcos21000GasDeviationTest.cpp)
target_include_directories(Bcos21000GasDeviationTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Bcos21000GasDeviationTest PRIVATE bcos-evm bcos-task evmone::evmone)
add_test(NAME Bcos21000GasDeviation COMMAND Bcos21000GasDeviationTest)

add_executable(Bcos7702ExecuteViaHostPropagationTest bcos/Bcos7702ExecuteViaHostPropagationTest.cpp)
target_include_directories(Bcos7702ExecuteViaHostPropagationTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Bcos7702ExecuteViaHostPropagationTest PRIVATE bcos-evm bcos-task evmone::evmone)
add_test(NAME Bcos7702ExecuteViaHostPropagation COMMAND Bcos7702ExecuteViaHostPropagationTest)

add_executable(Bcos7623PrecheckTest bcos/Bcos7623PrecheckTest.cpp)
target_include_directories(Bcos7623PrecheckTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Bcos7623PrecheckTest PRIVATE
    bcos-evm bcos-task evmone::evmone bcos-protocol)
add_test(NAME Bcos7623Precheck COMMAND Bcos7623PrecheckTest)

add_executable(BcosAuthOrchestratorHookTest bcos/BcosAuthOrchestratorHookTest.cpp)
target_include_directories(BcosAuthOrchestratorHookTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(BcosAuthOrchestratorHookTest PRIVATE
    bcos-evm bcos-task evmone::evmone bcos-protocol)
add_test(NAME BcosAuthOrchestratorHook COMMAND BcosAuthOrchestratorHookTest)

add_executable(ExecuteViaHostImportedFixtureTest bcos/ExecuteViaHostImportedFixtureTest.cpp)
target_include_directories(ExecuteViaHostImportedFixtureTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_compile_definitions(ExecuteViaHostImportedFixtureTest PRIVATE
    ETH_STATE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/state")
target_link_libraries(ExecuteViaHostImportedFixtureTest PRIVATE
    bcos-evm bcos-task evmone::evmone bcos-crypto)
add_test(NAME ExecuteViaHostImportedFixture COMMAND ExecuteViaHostImportedFixtureTest)

add_executable(Eip2537KernelTest eth/Eip2537KernelTest.cpp)
target_include_directories(Eip2537KernelTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_compile_definitions(Eip2537KernelTest PRIVATE
    ETH_STATE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/state")
target_link_libraries(Eip2537KernelTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME Eip2537Kernel COMMAND Eip2537KernelTest)

add_executable(EipPrecompileRevisionGateTest eth/EipPrecompileRevisionGateTest.cpp)
target_include_directories(EipPrecompileRevisionGateTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_compile_definitions(EipPrecompileRevisionGateTest PRIVATE
    ETH_STATE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/state")
target_link_libraries(EipPrecompileRevisionGateTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME EipPrecompileRevisionGate COMMAND EipPrecompileRevisionGateTest)

add_executable(BcosPrecompileRevisionGateTest bcos/BcosPrecompileRevisionGateTest.cpp)
target_include_directories(BcosPrecompileRevisionGateTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_compile_definitions(BcosPrecompileRevisionGateTest PRIVATE
    ETH_STATE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/state")
target_link_libraries(BcosPrecompileRevisionGateTest PRIVATE bcos-evm bcos-task evmone::evmone)
add_test(NAME BcosPrecompileRevisionGate COMMAND BcosPrecompileRevisionGateTest)

add_executable(Eip7823ModexpRejectTest eth/Eip7823ModexpRejectTest.cpp)
target_include_directories(Eip7823ModexpRejectTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Eip7823ModexpRejectTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME Eip7823ModexpReject COMMAND Eip7823ModexpRejectTest)

add_executable(Bcos7823ModexpRejectTest bcos/Bcos7823ModexpRejectTest.cpp)
target_include_directories(Bcos7823ModexpRejectTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Bcos7823ModexpRejectTest PRIVATE
    bcos-evm bcos-evm-eth evmone::evmone bcos-framework protocol-tars)
add_test(NAME Bcos7823ModexpReject COMMAND Bcos7823ModexpRejectTest)

add_executable(Eip7212KernelTest eth/Eip7212KernelTest.cpp)
target_include_directories(Eip7212KernelTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Eip7212KernelTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME Eip7212Kernel COMMAND Eip7212KernelTest)

add_executable(Bcos7212ExecuteViaHostTest bcos/Bcos7212ExecuteViaHostTest.cpp)
target_include_directories(Bcos7212ExecuteViaHostTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Bcos7212ExecuteViaHostTest PRIVATE
    bcos-evm bcos-evm-eth evmone::evmone bcos-framework protocol-tars)
add_test(NAME Bcos7212ExecuteViaHost COMMAND Bcos7212ExecuteViaHostTest)

add_executable(Bcos2537MsmGasTest bcos/Bcos2537MsmGasTest.cpp)
target_include_directories(Bcos2537MsmGasTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Bcos2537MsmGasTest PRIVATE bcos-evm bcos-evm-eth evmone::evmone)
add_test(NAME Bcos2537MsmGas COMMAND Bcos2537MsmGasTest)

add_executable(Bcos6780SelfdestructTest bcos/Bcos6780SelfdestructTest.cpp)
target_include_directories(Bcos6780SelfdestructTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_compile_definitions(Bcos6780SelfdestructTest PRIVATE
    ETH_STATE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/state")
target_link_libraries(Bcos6780SelfdestructTest PRIVATE
    bcos-evm bcos-task evmone::evmone bcos-crypto)
add_test(NAME Bcos6780Selfdestruct COMMAND Bcos6780SelfdestructTest)

add_executable(Eip7623PrecheckTest eth/Eip7623PrecheckTest.cpp)
target_include_directories(Eip7623PrecheckTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Eip7623PrecheckTest PRIVATE
    bcos-evm-eth evmone::evmone bcos-task bcos-crypto bcos-framework)
add_test(NAME Eip7623Precheck COMMAND Eip7623PrecheckTest)

add_executable(EthExecuteViaEthPreCheckTest eth/EthExecuteViaEthPreCheckTest.cpp)
target_include_directories(EthExecuteViaEthPreCheckTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(EthExecuteViaEthPreCheckTest PRIVATE
    bcos-evm-eth evmone::evmone bcos-task bcos-crypto bcos-framework)
add_test(NAME EthExecuteViaEthPreCheck COMMAND EthExecuteViaEthPreCheckTest)

add_executable(Eip1153TransientStorageTest eth/Eip1153TransientStorageTest.cpp)
target_include_directories(Eip1153TransientStorageTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Eip1153TransientStorageTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME Eip1153TransientStorage COMMAND Eip1153TransientStorageTest)

set(PRECOMPILE_ROUTER_PRECEDENCE_TEST_BINARY_NAME PrecompileRouterPrecedenceTest)

add_executable(${PRECOMPILE_ROUTER_PRECEDENCE_TEST_BINARY_NAME}
    eth/PrecompileRouterPrecedenceTest.cpp
)

target_include_directories(${PRECOMPILE_ROUTER_PRECEDENCE_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${PRECOMPILE_ROUTER_PRECEDENCE_TEST_BINARY_NAME} PRIVATE
    bcos-evm-eth
)

add_test(
    NAME PrecompileRouterPrecedence
    COMMAND ${PRECOMPILE_ROUTER_PRECEDENCE_TEST_BINARY_NAME}
)

set(PRECOMPILE_ROUTER_CHAR_TEST_BINARY_NAME PrecompileRouterCharacterizationTest)

add_executable(${PRECOMPILE_ROUTER_CHAR_TEST_BINARY_NAME}
    eth/PrecompileRouterCharacterizationTest.cpp
)

target_include_directories(${PRECOMPILE_ROUTER_CHAR_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${PRECOMPILE_ROUTER_CHAR_TEST_BINARY_NAME} PRIVATE
    bcos-evm
    bcos-evm-op
)

add_test(
    NAME PrecompileRouterCharacterization
    COMMAND ${PRECOMPILE_ROUTER_CHAR_TEST_BINARY_NAME}
)

set(PRECOMPILE_ROUTER_EQUIVALENCE_TEST_BINARY_NAME PrecompileRouterEquivalenceTest)

add_executable(${PRECOMPILE_ROUTER_EQUIVALENCE_TEST_BINARY_NAME}
    eth/PrecompileRouterEquivalenceTest.cpp
)

target_include_directories(${PRECOMPILE_ROUTER_EQUIVALENCE_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${PRECOMPILE_ROUTER_EQUIVALENCE_TEST_BINARY_NAME} PRIVATE
    bcos-evm
    bcos-evm-op
)

add_test(
    NAME PrecompileRouterEquivalence
    COMMAND ${PRECOMPILE_ROUTER_EQUIVALENCE_TEST_BINARY_NAME}
)

set(PRECOMPILE_ROUTER_ENVELOPE_TEST_BINARY_NAME PrecompileRouterEnvelopeTest)

add_executable(${PRECOMPILE_ROUTER_ENVELOPE_TEST_BINARY_NAME}
    eth/PrecompileRouterEnvelopeTest.cpp
)

target_include_directories(${PRECOMPILE_ROUTER_ENVELOPE_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${PRECOMPILE_ROUTER_ENVELOPE_TEST_BINARY_NAME} PRIVATE
    bcos-evm
    bcos-evm-op
)

add_test(
    NAME PrecompileRouterEnvelope
    COMMAND ${PRECOMPILE_ROUTER_ENVELOPE_TEST_BINARY_NAME}
)

add_executable(DebitIntrinsicGasTest eth/DebitIntrinsicGasTest.cpp)
target_include_directories(DebitIntrinsicGasTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(DebitIntrinsicGasTest PRIVATE bcos-evm-eth)
add_test(NAME DebitIntrinsicGas COMMAND DebitIntrinsicGasTest)
