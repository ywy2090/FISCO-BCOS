# bcos-evm opstack tests.

set(OPSTACK_EXECUTE_VIA_HOST_SMOKE_TEST_BINARY_NAME OpStackExecuteSmokeTest)

add_executable(${OPSTACK_EXECUTE_VIA_HOST_SMOKE_TEST_BINARY_NAME}
    opstack/OpStackExecuteSmokeTest.cpp
)

target_include_directories(${OPSTACK_EXECUTE_VIA_HOST_SMOKE_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${OPSTACK_EXECUTE_VIA_HOST_SMOKE_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME OpStackExecuteSmoke
    COMMAND ${OPSTACK_EXECUTE_VIA_HOST_SMOKE_TEST_BINARY_NAME}
)
add_executable(OpStackIntrinsicGasSyncTest
    opstack/OpStackIntrinsicGasSyncTest.cpp
    ../opstack/ApplyOpStackMessage.cpp
    ../opstack/OpStackTxLifecycle.cpp
    ../opstack/OpStackSettlement.cpp
    ../opstack/OpStackNormalTxFeeCoordinator.cpp
    ../opstack/OpStackStateTransitionBindings.cpp
    ../opstack/OpStackStateTransitionHooks.cpp
    ../opstack/OpStackFeeSettlement.cpp
    ../opstack/fee/OpStackPreDebitPlan.cpp
    ../opstack/fee/OpStackPostSettlementPlan.cpp
    ../opstack/OpStackSettlementFacade.cpp
    ../opstack/OpStackChainCallTargetAdapter.cpp
    ../opstack/fee/RollupCost.cpp
    ../opstack/fee/OpStackFee.cpp
    ../opstack/fee/OpStackFloorGas.cpp
    ../opstack/l1/L1BlockStorage.cpp
    ../opstack/l1/L1BlockPredeploy.cpp
    ../opstack/l1/GasPriceOraclePredeploy.cpp
    ../opstack/fee/OpStackFloorGasPrecheck.cpp)
target_compile_definitions(OpStackIntrinsicGasSyncTest PRIVATE BCOS_EVM_TESTING)
target_include_directories(OpStackIntrinsicGasSyncTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(OpStackIntrinsicGasSyncTest PRIVATE
    bcos-evm-eth bcos-task evmone::evmone bcos-framework ledger
    bcos-protocol bcos-utilities bcos-crypto codec)
add_test(NAME OpStackIntrinsicGasSync COMMAND OpStackIntrinsicGasSyncTest)
add_executable(OpStackFloorGasPrecheckOrderTest opstack/OpStackFloorGasPrecheckOrderTest.cpp)
target_include_directories(OpStackFloorGasPrecheckOrderTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(OpStackFloorGasPrecheckOrderTest PRIVATE bcos-evm-op)
add_test(NAME OpStackPreDebitOrder COMMAND OpStackFloorGasPrecheckOrderTest)
add_executable(OpStackStateTransitionBindingsTest opstack/OpStackStateTransitionBindingsTest.cpp)
target_include_directories(OpStackStateTransitionBindingsTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(OpStackStateTransitionBindingsTest PRIVATE bcos-evm-op)
add_test(NAME OpStackStateTransitionBindings COMMAND OpStackStateTransitionBindingsTest)
add_executable(OpStackStateTransitionHooksTest opstack/OpStackStateTransitionHooksTest.cpp)
target_include_directories(OpStackStateTransitionHooksTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(OpStackStateTransitionHooksTest PRIVATE bcos-evm-op)
add_test(NAME OpStackStateTransitionHooks COMMAND OpStackStateTransitionHooksTest)
add_executable(OpStackStateTransitionErrorPolicyTest opstack/OpStackStateTransitionErrorPolicyTest.cpp)
target_include_directories(OpStackStateTransitionErrorPolicyTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(OpStackStateTransitionErrorPolicyTest PRIVATE bcos-evm-op bcos-protocol evmone::evmone)
add_test(NAME OpStackStateTransitionErrorPolicy COMMAND OpStackStateTransitionErrorPolicyTest)

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

set(OPSTACK_DEPOSIT_SETTLEMENT_TEST_BINARY_NAME OpStackDepositSettlementTest)

add_executable(${OPSTACK_DEPOSIT_SETTLEMENT_TEST_BINARY_NAME}
    opstack/OpStackDepositSettlementTest.cpp
)

target_include_directories(${OPSTACK_DEPOSIT_SETTLEMENT_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${OPSTACK_DEPOSIT_SETTLEMENT_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME OpStackDepositSettlement
    COMMAND ${OPSTACK_DEPOSIT_SETTLEMENT_TEST_BINARY_NAME}
)

set(OPSTACK_SETTLE_ASYNC_TEST_BINARY_NAME OpStackSettleAsyncTest)

add_executable(${OPSTACK_SETTLE_ASYNC_TEST_BINARY_NAME}
    opstack/OpStackSettleAsyncTest.cpp
)

target_include_directories(${OPSTACK_SETTLE_ASYNC_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${OPSTACK_SETTLE_ASYNC_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME OpStackSettleAsync
    COMMAND ${OPSTACK_SETTLE_ASYNC_TEST_BINARY_NAME}
)

add_executable(OpStackNormalTxFeeCoordinatorTest opstack/OpStackNormalTxFeeCoordinatorTest.cpp)
target_include_directories(OpStackNormalTxFeeCoordinatorTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(OpStackNormalTxFeeCoordinatorTest PRIVATE bcos-evm-op)
add_test(NAME OpStackNormalTxFeeCoordinator COMMAND OpStackNormalTxFeeCoordinatorTest)

add_executable(OpStackPreDebitCharacterizationTest opstack/OpStackPreDebitCharacterizationTest.cpp)
target_include_directories(OpStackPreDebitCharacterizationTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(OpStackPreDebitCharacterizationTest PRIVATE bcos-evm-op)
add_test(NAME OpStackPreDebitCharacterization COMMAND OpStackPreDebitCharacterizationTest)

add_executable(OpStackPostSettlementCharacterizationTest opstack/OpStackPostSettlementCharacterizationTest.cpp)
target_include_directories(OpStackPostSettlementCharacterizationTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(OpStackPostSettlementCharacterizationTest PRIVATE bcos-evm-op)
add_test(NAME OpStackPostSettlementCharacterization COMMAND OpStackPostSettlementCharacterizationTest)

add_executable(OpStackSettlementCharacterizationTest
    opstack/OpStackSettlementCharacterizationTest.cpp
)

target_include_directories(OpStackSettlementCharacterizationTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(OpStackSettlementCharacterizationTest PRIVATE
    bcos-evm-op
)

add_test(
    NAME OpStackSettlementCharacterization
    COMMAND OpStackSettlementCharacterizationTest
)

set(OPSTACK_TX_LIFECYCLE_CHARACTERIZATION_TEST_BINARY_NAME OpStackTxLifecycleCharacterizationTest)

add_executable(${OPSTACK_TX_LIFECYCLE_CHARACTERIZATION_TEST_BINARY_NAME}
    opstack/OpStackTxLifecycleCharacterizationTest.cpp
)

target_include_directories(${OPSTACK_TX_LIFECYCLE_CHARACTERIZATION_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${OPSTACK_TX_LIFECYCLE_CHARACTERIZATION_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME OpStackTxLifecycleCharacterization
    COMMAND ${OPSTACK_TX_LIFECYCLE_CHARACTERIZATION_TEST_BINARY_NAME}
)

set(OPSTACK_TX_FEE_LEDGER_CTX_TEST_BINARY_NAME OpStackFeeSettlementCtxTest)

add_executable(${OPSTACK_TX_FEE_LEDGER_CTX_TEST_BINARY_NAME}
    opstack/OpStackFeeSettlementCtxTest.cpp
)

target_include_directories(${OPSTACK_TX_FEE_LEDGER_CTX_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${OPSTACK_TX_FEE_LEDGER_CTX_TEST_BINARY_NAME} PRIVATE
    bcos-evm-op
)

add_test(
    NAME OpStackFeeSettlementCtx
    COMMAND ${OPSTACK_TX_FEE_LEDGER_CTX_TEST_BINARY_NAME}
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

add_executable(OpStackChainCallTargetAdapterTest opstack/OpStackChainCallTargetAdapterTest.cpp)
target_include_directories(OpStackChainCallTargetAdapterTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(OpStackChainCallTargetAdapterTest PRIVATE bcos-evm-op bcos-evm-eth)
add_test(NAME OpStackChainCallTargetAdapter COMMAND OpStackChainCallTargetAdapterTest)

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

get_filename_component(BCOS_EVM_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

add_executable(${ISTHMUS_POST_EXECUTION_POLICY_TEST_BINARY_NAME}
    opstack/IsthmusPostExecutionPolicyTest.cpp
)

target_include_directories(${ISTHMUS_POST_EXECUTION_POLICY_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_compile_definitions(${ISTHMUS_POST_EXECUTION_POLICY_TEST_BINARY_NAME} PRIVATE
    BCOS_EVM_SOURCE_DIR="${BCOS_EVM_ROOT}"
)

target_link_libraries(${ISTHMUS_POST_EXECUTION_POLICY_TEST_BINARY_NAME} PRIVATE
    bcos-evm-eth
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
add_executable(OpStack7702ExecutePropagationTest opstack/OpStack7702ExecutePropagationTest.cpp)
target_include_directories(OpStack7702ExecutePropagationTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(OpStack7702ExecutePropagationTest PRIVATE
    bcos-evm-op bcos-task evmone::evmone)
add_test(NAME OpStack7702ExecutePropagation COMMAND OpStack7702ExecutePropagationTest)
add_executable(OpStack67802537KernelSmokeTest opstack/OpStack67802537KernelSmokeTest.cpp)
target_include_directories(OpStack67802537KernelSmokeTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(OpStack67802537KernelSmokeTest PRIVATE
    bcos-evm-op bcos-task evmone::evmone)
add_test(NAME OpStack67802537KernelSmoke COMMAND OpStack67802537KernelSmokeTest)
add_te_input_builder_test(OpStackTxInputBuilderTest opstack/OpStackTxInputBuilderTest.cpp)
target_include_directories(OpStackTxInputBuilderTest PRIVATE ${CMAKE_SOURCE_DIR}/bcos-rpc)
target_link_libraries(OpStackTxInputBuilderTest PRIVATE rpc)
add_executable(OpStackTxPropsTest opstack/OpStackTxPropsTest.cpp)
target_include_directories(OpStackTxPropsTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/transaction-executor)
target_link_libraries(OpStackTxPropsTest PRIVATE bcos-evm-op bcos-evm-eth)
add_test(NAME OpStackTxProps COMMAND OpStackTxPropsTest)
