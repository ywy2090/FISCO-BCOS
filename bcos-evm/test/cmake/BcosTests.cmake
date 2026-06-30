# bcos-evm bcos tests.

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

set(FISCO_SSTORE_STATUS_TEST_BINARY_NAME FiscoSstoreStatusTest)

add_executable(${FISCO_SSTORE_STATUS_TEST_BINARY_NAME}
    bcos/FiscoSstoreStatusTest.cpp
)

target_include_directories(${FISCO_SSTORE_STATUS_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${FISCO_SSTORE_STATUS_TEST_BINARY_NAME} PRIVATE
    bcos-evm
)

add_test(
    NAME FiscoSstoreStatus
    COMMAND ${FISCO_SSTORE_STATUS_TEST_BINARY_NAME}
)

add_executable(FiscoChainCallTargetAdapterTest bcos/FiscoChainCallTargetAdapterTest.cpp)
target_include_directories(FiscoChainCallTargetAdapterTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(FiscoChainCallTargetAdapterTest PRIVATE bcos-evm)
add_test(NAME FiscoChainCallTargetAdapter COMMAND FiscoChainCallTargetAdapterTest)

set(FISCO_ADDRESS_DERIVATION_TEST_BINARY_NAME FiscoAddressDerivationTest)

add_executable(${FISCO_ADDRESS_DERIVATION_TEST_BINARY_NAME}
    bcos/FiscoAddressDerivationTest.cpp
)

target_include_directories(${FISCO_ADDRESS_DERIVATION_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${FISCO_ADDRESS_DERIVATION_TEST_BINARY_NAME} PRIVATE
    bcos-evm
    ledger
)

add_test(
    NAME FiscoAddressDerivation
    COMMAND ${FISCO_ADDRESS_DERIVATION_TEST_BINARY_NAME}
)

set(EXECUTE_VIA_HOST_SMOKE_TEST_BINARY_NAME FiscoExecuteSmokeTest)

add_executable(${EXECUTE_VIA_HOST_SMOKE_TEST_BINARY_NAME}
    bcos/FiscoExecuteSmokeTest.cpp
)

target_include_directories(${EXECUTE_VIA_HOST_SMOKE_TEST_BINARY_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(${EXECUTE_VIA_HOST_SMOKE_TEST_BINARY_NAME} PRIVATE
    bcos-evm
)

add_test(
    NAME FiscoExecuteSmoke
    COMMAND ${EXECUTE_VIA_HOST_SMOKE_TEST_BINARY_NAME}
)

add_te_input_builder_test(FiscoTxInputBuilderTest bcos/FiscoTxInputBuilderTest.cpp)
add_executable(Bcos21000GasDeviationTest bcos/Bcos21000GasDeviationTest.cpp)
target_include_directories(Bcos21000GasDeviationTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Bcos21000GasDeviationTest PRIVATE bcos-evm bcos-task evmone::evmone)
add_test(NAME Bcos21000GasDeviation COMMAND Bcos21000GasDeviationTest)
add_executable(Bcos7702FiscoExecutePropagationTest bcos/Bcos7702FiscoExecutePropagationTest.cpp)
target_include_directories(Bcos7702FiscoExecutePropagationTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Bcos7702FiscoExecutePropagationTest PRIVATE bcos-evm bcos-task evmone::evmone)
add_test(NAME Bcos7702FiscoExecutePropagation COMMAND Bcos7702FiscoExecutePropagationTest)
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
add_executable(FiscoOrchestrationProfileTest bcos/FiscoOrchestrationProfileTest.cpp)
target_include_directories(FiscoOrchestrationProfileTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(FiscoOrchestrationProfileTest PRIVATE
    bcos-evm bcos-task evmone::evmone bcos-protocol)
add_test(NAME FiscoOrchestrationProfile COMMAND FiscoOrchestrationProfileTest)
add_executable(FiscoOrchestrationErrorPolicyTest bcos/FiscoOrchestrationErrorPolicyTest.cpp)
target_include_directories(FiscoOrchestrationErrorPolicyTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(FiscoOrchestrationErrorPolicyTest PRIVATE
    bcos-evm bcos-protocol bcos-crypto evmone::evmone)
add_test(NAME FiscoOrchestrationErrorPolicy COMMAND FiscoOrchestrationErrorPolicyTest)
add_executable(FiscoExecuteImportedFixtureTest bcos/FiscoExecuteImportedFixtureTest.cpp)
target_include_directories(FiscoExecuteImportedFixtureTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_compile_definitions(FiscoExecuteImportedFixtureTest PRIVATE
    ETH_STATE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/state")
target_link_libraries(FiscoExecuteImportedFixtureTest PRIVATE
    bcos-evm bcos-task evmone::evmone bcos-crypto)
add_test(NAME FiscoExecuteImportedFixture COMMAND FiscoExecuteImportedFixtureTest)
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
add_executable(Bcos7212FiscoExecuteTest bcos/Bcos7212FiscoExecuteTest.cpp)
target_include_directories(Bcos7212FiscoExecuteTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(Bcos7212FiscoExecuteTest PRIVATE
    bcos-evm bcos-evm-eth evmone::evmone bcos-framework protocol-tars)
add_test(NAME Bcos7212FiscoExecute COMMAND Bcos7212FiscoExecuteTest)
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
