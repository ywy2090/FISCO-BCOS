file(GLOB_RECURSE SOURCES "*.cpp")
# Legacy HostContext/ExecuteFrame harness — pending migration to executeViaHost (Plan C C4).
list(REMOVE_ITEM SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/CompatHostContextTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestHostContext.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/FIB88_92_ErrorHandlingTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Web3AccessListResolverTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestEthTransactionExecutorFixture.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/TestOpStackTransactionExecutorFixture.cpp"
)
add_executable(test-transaction-executor ${SOURCES})

find_package(Boost REQUIRED serialization unit_test_framework)

target_link_libraries(test-transaction-executor transaction-executor executor ledger protocol-tars bcos-framework rpc evmone::evmone bcos-evm Boost::unit_test_framework)
target_include_directories(test-transaction-executor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../bcos-evm/test
)
# Ensure libexecutor.a is fully archived before linking (UNITY_BUILD static lib).
add_dependencies(test-transaction-executor executor)
set_source_files_properties("main.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties("Eip2929AccessStateTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties("Eip2929HelperTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties("EthTxGasSettlementTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties("Task0EvmoneRefundSemanticsTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties("Web3AccessListResolverTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties("EthTxGasSettlementExecutorTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties("Modexp7823TeTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties("ExecuteViaHostCompatTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties("CompatExecuteViaHostTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties("CompatExecuteViaHostPhaseBTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties("CompatExecuteViaHostPhaseCTest.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_source_files_properties("TestOpStackTransactionExecutorFixture.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
set_target_properties(test-transaction-executor PROPERTIES UNITY_BUILD "ON")
# add_test(NAME test-transaction-executor WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY} COMMAND test-transaction-executor)
config_test_cases("" "${SOURCES}" "test-transaction-executor" "" "")

# Minimal regression harness target for executeViaHost compat.
# This bypasses transaction-executor's transitive executor linkage while that area is under refactor.
add_executable(test-execute-via-host-compat
    main.cpp
    ExecuteViaHostCompatTest.cpp
)
target_link_libraries(test-execute-via-host-compat
    PRIVATE
    bcos-evm
    evmone::evmone
    Boost::unit_test_framework
)
add_test(NAME ExecuteViaHostCompat WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    COMMAND test-execute-via-host-compat)

add_executable(test-eth-transaction-executor-fixture
    main.cpp
    TestEthTransactionExecutorFixture.cpp
)
target_include_directories(test-eth-transaction-executor-fixture PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../bcos-evm/test
    ${PROJECT_SOURCE_DIR}
)
target_compile_definitions(test-eth-transaction-executor-fixture PRIVATE
    ETH_STATE_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../bcos-evm/test/fixtures/state"
)
target_link_libraries(test-eth-transaction-executor-fixture PRIVATE
    transaction-executor
    protocol-tars
    bcos-evm-eth
    bcos-framework
    ledger
    executor
    Boost::unit_test_framework
)
add_dependencies(test-eth-transaction-executor-fixture executor)
add_test(NAME EthTransactionExecutorFixture
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    COMMAND test-eth-transaction-executor-fixture)

add_executable(test-opstack-transaction-executor-fixture
    main.cpp
    TestOpStackTransactionExecutorFixture.cpp
)
target_include_directories(test-opstack-transaction-executor-fixture PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../bcos-evm/test
    ${PROJECT_SOURCE_DIR}
)
target_link_libraries(test-opstack-transaction-executor-fixture PRIVATE
    transaction-executor
    protocol-tars
    bcos-evm
    bcos-framework
    ledger
    executor
    Boost::unit_test_framework
)
add_dependencies(test-opstack-transaction-executor-fixture executor)
add_test(NAME OpStackTransactionExecutorFixture
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    COMMAND test-opstack-transaction-executor-fixture)
