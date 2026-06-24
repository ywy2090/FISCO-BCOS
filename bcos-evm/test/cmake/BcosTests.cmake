# bcos-evm bcos tests.

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

set(FISCO_HOST_EXTENSION_TEST_BINARY_NAME FiscoVmHostPolicyTest)

add_executable(${FISCO_HOST_EXTENSION_TEST_BINARY_NAME}
    bcos/FiscoVmHostPolicyTest.cpp
)

target_include_directories(${FISCO_HOST_EXTENSION_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${FISCO_HOST_EXTENSION_TEST_BINARY_NAME} PRIVATE
    bcos-evm
)

add_test(
    NAME FiscoVmHostPolicy
    COMMAND ${FISCO_HOST_EXTENSION_TEST_BINARY_NAME}
)

set(EXECUTE_VIA_HOST_SMOKE_TEST_BINARY_NAME FiscoExecutionBridgeSmokeTest)

add_executable(${EXECUTE_VIA_HOST_SMOKE_TEST_BINARY_NAME}
    bcos/FiscoExecutionBridgeSmokeTest.cpp
)

target_include_directories(${EXECUTE_VIA_HOST_SMOKE_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${EXECUTE_VIA_HOST_SMOKE_TEST_BINARY_NAME} PRIVATE
    bcos-evm
)

add_test(
    NAME FiscoExecutionBridgeSmoke
    COMMAND ${EXECUTE_VIA_HOST_SMOKE_TEST_BINARY_NAME}
)

add_te_input_builder_test(FiscoTxInputBuilderTest eth/FiscoTxInputBuilderTest.cpp)
add_executable(Bcos21000GasDeviationTest bcos/Bcos21000GasDeviationTest.cpp)
target_include_directories(Bcos21000GasDeviationTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Bcos21000GasDeviationTest PRIVATE bcos-evm bcos-task evmone::evmone)
add_test(NAME Bcos21000GasDeviation COMMAND Bcos21000GasDeviationTest)
add_executable(Bcos7702FiscoExecutionBridgePropagationTest bcos/Bcos7702FiscoExecutionBridgePropagationTest.cpp)
target_include_directories(Bcos7702FiscoExecutionBridgePropagationTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Bcos7702FiscoExecutionBridgePropagationTest PRIVATE bcos-evm bcos-task evmone::evmone)
add_test(NAME Bcos7702FiscoExecutionBridgePropagation COMMAND Bcos7702FiscoExecutionBridgePropagationTest)
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
add_executable(FiscoPipelineHookBinderTest bcos/FiscoPipelineHookBinderTest.cpp)
target_include_directories(FiscoPipelineHookBinderTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(FiscoPipelineHookBinderTest PRIVATE
    bcos-evm bcos-task evmone::evmone bcos-protocol)
add_test(NAME FiscoPipelineHookBinder COMMAND FiscoPipelineHookBinderTest)
add_executable(FiscoExecutionBridgeImportedFixtureTest bcos/FiscoExecutionBridgeImportedFixtureTest.cpp)
target_include_directories(FiscoExecutionBridgeImportedFixtureTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_compile_definitions(FiscoExecutionBridgeImportedFixtureTest PRIVATE
    ETH_STATE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/state")
target_link_libraries(FiscoExecutionBridgeImportedFixtureTest PRIVATE
    bcos-evm bcos-task evmone::evmone bcos-crypto)
add_test(NAME FiscoExecutionBridgeImportedFixture COMMAND FiscoExecutionBridgeImportedFixtureTest)
add_executable(BcosPrecompileRevisionGateTest bcos/BcosPrecompileRevisionGateTest.cpp)
target_include_directories(BcosPrecompileRevisionGateTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_compile_definitions(BcosPrecompileRevisionGateTest PRIVATE
    ETH_STATE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/state")
target_link_libraries(BcosPrecompileRevisionGateTest PRIVATE bcos-evm bcos-task evmone::evmone)
add_test(NAME BcosPrecompileRevisionGate COMMAND BcosPrecompileRevisionGateTest)
add_executable(Bcos7823ModexpRejectTest bcos/Bcos7823ModexpRejectTest.cpp)
target_include_directories(Bcos7823ModexpRejectTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Bcos7823ModexpRejectTest PRIVATE
    bcos-evm bcos-evm-eth evmone::evmone bcos-framework protocol-tars)
add_test(NAME Bcos7823ModexpReject COMMAND Bcos7823ModexpRejectTest)
add_executable(Bcos7212FiscoExecutionBridgeTest bcos/Bcos7212FiscoExecutionBridgeTest.cpp)
target_include_directories(Bcos7212FiscoExecutionBridgeTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Bcos7212FiscoExecutionBridgeTest PRIVATE
    bcos-evm bcos-evm-eth evmone::evmone bcos-framework protocol-tars)
add_test(NAME Bcos7212FiscoExecutionBridge COMMAND Bcos7212FiscoExecutionBridgeTest)
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
