# bcos-evm eth tests.

set(HOOKS_TEST_BINARY_NAME EthVmHostPolicyHooksTest)

add_executable(${HOOKS_TEST_BINARY_NAME}
    eth/EthVmHostPolicyHooksTest.cpp
)

target_include_directories(${HOOKS_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${HOOKS_TEST_BINARY_NAME} PRIVATE
    bcos-evm-eth evmone::evmone
)

add_test(
    NAME EthVmHostPolicyHooks
    COMMAND ${HOOKS_TEST_BINARY_NAME}
)

set(EXECUTE_MESSAGE_SMOKE_TEST_BINARY_NAME InnerExecuteSmokeTest)

add_executable(${EXECUTE_MESSAGE_SMOKE_TEST_BINARY_NAME}
    eth/InnerExecuteSmokeTest.cpp
)

target_include_directories(${EXECUTE_MESSAGE_SMOKE_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${EXECUTE_MESSAGE_SMOKE_TEST_BINARY_NAME} PRIVATE
    bcos-evm-eth evmone::evmone
)

add_test(
    NAME InnerExecuteSmoke
    COMMAND ${EXECUTE_MESSAGE_SMOKE_TEST_BINARY_NAME}
)

set(TX_EXECUTION_ADAPTER_TEST_BINARY_NAME TxExecutionRunnerTest)

add_executable(${TX_EXECUTION_ADAPTER_TEST_BINARY_NAME}
    eth/TxExecutionRunnerTest.cpp
)

target_include_directories(${TX_EXECUTION_ADAPTER_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${TX_EXECUTION_ADAPTER_TEST_BINARY_NAME} PRIVATE
    bcos-evm-eth evmone::evmone
)

add_test(
    NAME TxExecutionRunner
    COMMAND ${TX_EXECUTION_ADAPTER_TEST_BINARY_NAME}
)

add_executable(Eip2929OpcodeGasTest eth/Eip2929OpcodeGasTest.cpp)
target_include_directories(Eip2929OpcodeGasTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Eip2929OpcodeGasTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME Eip2929OpcodeGas COMMAND Eip2929OpcodeGasTest)

add_executable(Eip7702DelegatedCallGasTest eth/Eip7702DelegatedCallGasTest.cpp)
target_include_directories(Eip7702DelegatedCallGasTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Eip7702DelegatedCallGasTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME Eip7702DelegatedCallGas COMMAND Eip7702DelegatedCallGasTest)

add_executable(EthMessageFixtureTest eth/EthMessageFixtureTest.cpp)
target_include_directories(EthMessageFixtureTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_compile_definitions(EthMessageFixtureTest PRIVATE
    ETH_STATE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/state")
target_link_libraries(EthMessageFixtureTest PRIVATE
    bcos-evm-eth evmone::evmone bcos-task bcos-crypto)
add_test(NAME EthMessageFixture COMMAND EthMessageFixtureTest)
add_executable(EthIncludedTxVmerrTest eth/EthIncludedTxVmerrTest.cpp)
target_include_directories(EthIncludedTxVmerrTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(EthIncludedTxVmerrTest PRIVATE
    bcos-evm-eth evmone::evmone bcos-task bcos-crypto)
add_test(NAME EthIncludedTxVmerr COMMAND EthIncludedTxVmerrTest)
add_executable(EthIntrinsicGasFailureCharacterizationTest
    eth/EthIntrinsicGasFailureCharacterizationTest.cpp)
target_include_directories(EthIntrinsicGasFailureCharacterizationTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(EthIntrinsicGasFailureCharacterizationTest PRIVATE
    bcos-evm-eth evmone::evmone bcos-task bcos-crypto bcos-protocol)
add_test(NAME EthIntrinsicGasFailure COMMAND EthIntrinsicGasFailureCharacterizationTest)
add_executable(EthEip1559GasTest eth/EthEip1559GasTest.cpp)
target_include_directories(EthEip1559GasTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(EthEip1559GasTest PRIVATE bcos-evm-eth bcos-utilities)
add_test(NAME EthEip1559Gas COMMAND EthEip1559GasTest)
add_executable(Eip1559GateTest eth/Eip1559GateTest.cpp)
target_include_directories(Eip1559GateTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Eip1559GateTest PRIVATE bcos-evm-eth bcos-utilities)
add_test(NAME Eip1559Gate COMMAND Eip1559GateTest)
add_executable(TxFeeSettlementTest eth/TxFeeSettlementTest.cpp)
target_include_directories(TxFeeSettlementTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(TxFeeSettlementTest PRIVATE bcos-evm-eth bcos-utilities)
add_test(NAME TxFeeSettlement COMMAND TxFeeSettlementTest)
add_executable(Web3TypedTxKindTest eth/Web3TypedTxKindTest.cpp)
target_include_directories(Web3TypedTxKindTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Web3TypedTxKindTest PRIVATE bcos-evm-eth bcos-utilities)
add_test(NAME Web3TypedTxKind COMMAND Web3TypedTxKindTest)
add_executable(EthMessage1559GasPriceTest eth/EthMessage1559GasPriceTest.cpp)
target_include_directories(EthMessage1559GasPriceTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(EthMessage1559GasPriceTest PRIVATE
    bcos-evm-eth evmone::evmone bcos-task bcos-crypto)
add_test(NAME EthMessage1559GasPrice COMMAND EthMessage1559GasPriceTest)
add_executable(StateTransitionExecuteTest eth/StateTransitionExecuteTest.cpp)
target_include_directories(StateTransitionExecuteTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(StateTransitionExecuteTest PRIVATE
    bcos-evm-eth evmone::evmone bcos-task bcos-crypto)
add_test(NAME StateTransitionExecute COMMAND StateTransitionExecuteTest)
add_executable(KernelCanonicalNamingTest eth/KernelCanonicalNamingTest.cpp)
target_include_directories(KernelCanonicalNamingTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(KernelCanonicalNamingTest PRIVATE
    bcos-evm-eth evmone::evmone bcos-task bcos-crypto)
add_test(NAME KernelCanonicalNaming COMMAND KernelCanonicalNamingTest)
add_executable(StateTransitionContextTest eth/StateTransitionContextTest.cpp)
target_include_directories(StateTransitionContextTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(StateTransitionContextTest PRIVATE
    bcos-evm-eth evmone::evmone bcos-task bcos-crypto)
add_test(NAME StateTransitionContext COMMAND StateTransitionContextTest)
add_executable(EvmTxContextViewPropagationTest eth/EvmTxContextViewPropagationTest.cpp)
target_include_directories(EvmTxContextViewPropagationTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(EvmTxContextViewPropagationTest PRIVATE
    bcos-evm-eth bcos-evm-op evmone::evmone bcos-task bcos-crypto)
add_test(NAME EvmTxContextViewPropagation COMMAND EvmTxContextViewPropagationTest)
add_executable(EthStateTransitionBindingsTest eth/EthStateTransitionBindingsTest.cpp)
target_include_directories(EthStateTransitionBindingsTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(EthStateTransitionBindingsTest PRIVATE
    bcos-evm-eth bcos-protocol)
add_test(NAME EthStateTransitionBindings COMMAND EthStateTransitionBindingsTest)
add_executable(EthStateTransitionErrorPolicyTest eth/EthStateTransitionErrorPolicyTest.cpp)
target_include_directories(EthStateTransitionErrorPolicyTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(EthStateTransitionErrorPolicyTest PRIVATE
    bcos-evm-eth bcos-protocol)
add_test(NAME EthStateTransitionErrorPolicy COMMAND EthStateTransitionErrorPolicyTest)
add_te_input_builder_test(EthTxInputBuilderTest eth/EthTxInputBuilderTest.cpp)
add_executable(Eip7702ApplyAuthorizationEthTest eth/Eip7702ApplyAuthorizationEthTest.cpp)
target_include_directories(Eip7702ApplyAuthorizationEthTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Eip7702ApplyAuthorizationEthTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME Eip7702ApplyAuthorizationEth COMMAND Eip7702ApplyAuthorizationEthTest)
add_executable(TxFeaturePrepareTest eth/TxFeaturePrepareTest.cpp)
target_include_directories(TxFeaturePrepareTest PRIVATE ${PROJECT_SOURCE_DIR})
target_link_libraries(TxFeaturePrepareTest PRIVATE bcos-evm-eth)
add_test(NAME TxFeaturePrepare COMMAND TxFeaturePrepareTest)
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
add_executable(PrecompileActiveGateMatrixTest eth/PrecompileActiveGateMatrixTest.cpp)
target_include_directories(PrecompileActiveGateMatrixTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(PrecompileActiveGateMatrixTest PRIVATE bcos-evm-eth)
add_test(NAME PrecompileActiveGateMatrix COMMAND PrecompileActiveGateMatrixTest)
add_executable(Eip7823ModexpRejectTest eth/Eip7823ModexpRejectTest.cpp)
target_include_directories(Eip7823ModexpRejectTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Eip7823ModexpRejectTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME Eip7823ModexpReject COMMAND Eip7823ModexpRejectTest)
add_executable(Eip7212KernelTest eth/Eip7212KernelTest.cpp)
target_include_directories(Eip7212KernelTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Eip7212KernelTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME Eip7212Kernel COMMAND Eip7212KernelTest)
add_executable(Eip7623PrecheckTest eth/Eip7623PrecheckTest.cpp)
target_include_directories(Eip7623PrecheckTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Eip7623PrecheckTest PRIVATE
    bcos-evm-eth evmone::evmone bcos-task bcos-crypto bcos-framework)
add_test(NAME Eip7623Precheck COMMAND Eip7623PrecheckTest)
add_executable(EthTxPrecheckTest eth/EthTxPrecheckTest.cpp)
target_include_directories(EthTxPrecheckTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(EthTxPrecheckTest PRIVATE
    bcos-evm-eth evmone::evmone bcos-task bcos-crypto bcos-framework)
add_test(NAME EthTxPrecheck COMMAND EthTxPrecheckTest)
add_executable(Eip1153TransientStorageTest eth/Eip1153TransientStorageTest.cpp)
target_include_directories(Eip1153TransientStorageTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Eip1153TransientStorageTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME Eip1153TransientStorage COMMAND Eip1153TransientStorageTest)

set(PRECOMPILE_ROUTER_ENVELOPE_TEST_BINARY_NAME PrecompileRouterEnvelopeTest)

add_executable(${PRECOMPILE_ROUTER_ENVELOPE_TEST_BINARY_NAME}
    eth/PrecompileRouterEnvelopeTest.cpp
)

target_include_directories(${PRECOMPILE_ROUTER_ENVELOPE_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${PRECOMPILE_ROUTER_ENVELOPE_TEST_BINARY_NAME} PRIVATE
    bcos-evm-eth evmone::evmone
)

add_test(
    NAME PrecompileRouterEnvelope
    COMMAND ${PRECOMPILE_ROUTER_ENVELOPE_TEST_BINARY_NAME}
)

set(INSUFFICIENT_BALANCE_GAS_LEFT_TEST_BINARY_NAME InsufficientBalanceGasLeftTest)

add_executable(${INSUFFICIENT_BALANCE_GAS_LEFT_TEST_BINARY_NAME}
    eth/InsufficientBalanceGasLeftTest.cpp
)

target_include_directories(${INSUFFICIENT_BALANCE_GAS_LEFT_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${INSUFFICIENT_BALANCE_GAS_LEFT_TEST_BINARY_NAME} PRIVATE
    bcos-evm-eth evmone::evmone
)

add_test(
    NAME InsufficientBalanceGasLeft
    COMMAND ${INSUFFICIENT_BALANCE_GAS_LEFT_TEST_BINARY_NAME}
)

set(PRECOMPILE_ROUTER_7702_TEST_BINARY_NAME PrecompileRouter7702Test)

add_executable(${PRECOMPILE_ROUTER_7702_TEST_BINARY_NAME}
    eth/PrecompileRouter7702Test.cpp
)

target_include_directories(${PRECOMPILE_ROUTER_7702_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${PRECOMPILE_ROUTER_7702_TEST_BINARY_NAME} PRIVATE
    bcos-evm-eth evmone::evmone
)

add_test(
    NAME PrecompileRouter7702
    COMMAND ${PRECOMPILE_ROUTER_7702_TEST_BINARY_NAME}
)

add_executable(DeductIntrinsicGasTest eth/DeductIntrinsicGasTest.cpp)
target_include_directories(DeductIntrinsicGasTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(DeductIntrinsicGasTest PRIVATE bcos-evm-eth)
add_test(NAME DeductIntrinsicGas COMMAND DeductIntrinsicGasTest)

add_executable(FrameTargetResolverTest eth/FrameTargetResolverTest.cpp)
target_include_directories(FrameTargetResolverTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(FrameTargetResolverTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME FrameTargetResolver COMMAND FrameTargetResolverTest)

add_executable(CallTargetResolverTest eth/CallTargetResolverTest.cpp)
target_include_directories(CallTargetResolverTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(CallTargetResolverTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME CallTargetResolver COMMAND CallTargetResolverTest)

add_executable(PrecompileEnvelopeTest eth/PrecompileEnvelopeTest.cpp)
target_include_directories(PrecompileEnvelopeTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(PrecompileEnvelopeTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME PrecompileEnvelope COMMAND PrecompileEnvelopeTest)

add_executable(FrameTargetRoutingCharacterizationTest eth/FrameTargetRoutingCharacterizationTest.cpp)
target_include_directories(FrameTargetRoutingCharacterizationTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(FrameTargetRoutingCharacterizationTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME FrameTargetRoutingCharacterization COMMAND FrameTargetRoutingCharacterizationTest)

add_executable(FrameValueTransferTest eth/FrameValueTransferTest.cpp)
target_include_directories(FrameValueTransferTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(FrameValueTransferTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME FrameValueTransfer COMMAND FrameValueTransferTest)

add_executable(EvmCallFrameTest eth/EvmCallFrameTest.cpp)
target_include_directories(EvmCallFrameTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(EvmCallFrameTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME EvmCallFrame COMMAND EvmCallFrameTest)

add_executable(EthDelegateCallPrecompileTest eth/EthDelegateCallPrecompileTest.cpp)
target_include_directories(EthDelegateCallPrecompileTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(EthDelegateCallPrecompileTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME EthDelegateCallPrecompile COMMAND EthDelegateCallPrecompileTest)

add_executable(ResolveExecutionCodeTest eth/ResolveExecutionCodeTest.cpp)
target_include_directories(ResolveExecutionCodeTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(ResolveExecutionCodeTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME ResolveExecutionCode COMMAND ResolveExecutionCodeTest)

add_executable(TopLevelInsufficientBalanceStateDiffTest eth/TopLevelInsufficientBalanceStateDiffTest.cpp)
target_include_directories(TopLevelInsufficientBalanceStateDiffTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(TopLevelInsufficientBalanceStateDiffTest PRIVATE
    bcos-evm-eth evmone::evmone bcos-task bcos-crypto)
add_test(NAME TopLevelInsufficientBalanceStateDiff COMMAND TopLevelInsufficientBalanceStateDiffTest)

add_executable(EvmcStatusMappingTest eth/EvmcStatusMappingTest.cpp)
target_include_directories(EvmcStatusMappingTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(EvmcStatusMappingTest PRIVATE
    bcos-evm-eth evmone::evmone bcos-crypto)
add_test(NAME EvmcStatusMapping COMMAND EvmcStatusMappingTest)
