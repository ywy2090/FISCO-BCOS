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

set(EXECUTE_MESSAGE_SMOKE_TEST_BINARY_NAME ExecuteMessageSmokeTest)

add_executable(${EXECUTE_MESSAGE_SMOKE_TEST_BINARY_NAME}
    eth/ExecuteMessageSmokeTest.cpp
)

target_include_directories(${EXECUTE_MESSAGE_SMOKE_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${EXECUTE_MESSAGE_SMOKE_TEST_BINARY_NAME} PRIVATE
    bcos-evm-eth evmone::evmone
)

add_test(
    NAME ExecuteMessageSmoke
    COMMAND ${EXECUTE_MESSAGE_SMOKE_TEST_BINARY_NAME}
)

add_executable(Eip2929OpcodeGasTest eth/Eip2929OpcodeGasTest.cpp)
target_include_directories(Eip2929OpcodeGasTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Eip2929OpcodeGasTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME Eip2929OpcodeGas COMMAND Eip2929OpcodeGasTest)

add_executable(EthReferenceBridgeFixtureTest eth/EthReferenceBridgeFixtureTest.cpp)
target_include_directories(EthReferenceBridgeFixtureTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_compile_definitions(EthReferenceBridgeFixtureTest PRIVATE
    ETH_STATE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/state")
target_link_libraries(EthReferenceBridgeFixtureTest PRIVATE
    bcos-evm-eth evmone::evmone bcos-task bcos-crypto)
add_test(NAME EthReferenceBridgeFixture COMMAND EthReferenceBridgeFixtureTest)
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
add_executable(EthReferenceBridge1559GasPriceTest eth/EthReferenceBridge1559GasPriceTest.cpp)
target_include_directories(EthReferenceBridge1559GasPriceTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(EthReferenceBridge1559GasPriceTest PRIVATE
    bcos-evm-eth evmone::evmone bcos-task bcos-crypto)
add_test(NAME EthReferenceBridge1559GasPrice COMMAND EthReferenceBridge1559GasPriceTest)
add_executable(TxPipelineTest eth/TxPipelineTest.cpp)
target_include_directories(TxPipelineTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(TxPipelineTest PRIVATE
    bcos-evm-eth evmone::evmone bcos-task bcos-crypto)
add_test(NAME TxPipeline COMMAND TxPipelineTest)
add_executable(EthPipelineHookBinderTest eth/EthPipelineHookBinderTest.cpp)
target_include_directories(EthPipelineHookBinderTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(EthPipelineHookBinderTest PRIVATE
    bcos-evm-eth bcos-protocol)
add_test(NAME EthPipelineHookBinder COMMAND EthPipelineHookBinderTest)
add_executable(OrchestrationErrorPolicyTest eth/OrchestrationErrorPolicyTest.cpp)
target_include_directories(OrchestrationErrorPolicyTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(OrchestrationErrorPolicyTest PRIVATE
    bcos-evm-eth bcos-protocol)
add_test(NAME OrchestrationErrorPolicy COMMAND OrchestrationErrorPolicyTest)
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
add_executable(DebitIntrinsicGasTest eth/DebitIntrinsicGasTest.cpp)
target_include_directories(DebitIntrinsicGasTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(DebitIntrinsicGasTest PRIVATE bcos-evm-eth)
add_test(NAME DebitIntrinsicGas COMMAND DebitIntrinsicGasTest)

add_executable(RouteMessageTest eth/RouteMessageTest.cpp)
target_include_directories(RouteMessageTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(RouteMessageTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME RouteMessage COMMAND RouteMessageTest)

add_executable(FrameValueTransferTest eth/FrameValueTransferTest.cpp)
target_include_directories(FrameValueTransferTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(FrameValueTransferTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME FrameValueTransfer COMMAND FrameValueTransferTest)

add_executable(ExecutionFrameTest eth/ExecutionFrameTest.cpp)
target_include_directories(ExecutionFrameTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(ExecutionFrameTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME ExecutionFrame COMMAND ExecutionFrameTest)

add_executable(ResolveExecutionCodeTest eth/ResolveExecutionCodeTest.cpp)
target_include_directories(ResolveExecutionCodeTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(ResolveExecutionCodeTest PRIVATE bcos-evm-eth evmone::evmone)
add_test(NAME ResolveExecutionCode COMMAND ResolveExecutionCodeTest)
