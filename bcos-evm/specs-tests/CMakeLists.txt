cmake_minimum_required(VERSION 3.28)

option(BCOS_EVM_SPECS_TESTS_FETCH_ASSETS
    "Download pinned reference test assets via FetchContent at configure time"
    ON)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/FetchReferenceAssets.cmake")

add_library(bcos-evm-specs-tests-core STATIC
    src/ManifestLoader.cpp
    src/GeneralStateTestLoader.cpp
    src/ForkProfileRegistry.cpp
    src/StateTestMatcher.cpp
    src/StateTestAssert.cpp
    src/GstStateHash.cpp
    src/EestStateTestLoader.cpp
    src/EestTransactionTestLoader.cpp
    src/Eip7702StrictTxValidator.cpp
)

target_include_directories(bcos-evm-specs-tests-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)

target_compile_definitions(bcos-evm-specs-tests-core PUBLIC
    SPECS_TESTS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    SPECS_TESTS_ETHEREUM_TESTS_ROOT="${EVM_REF_ETHEREUM_TESTS_ROOT}"
    SPECS_TESTS_EEST_ROOT="${EVM_REF_EEST_ROOT}"
)

target_link_libraries(bcos-evm-specs-tests-core PUBLIC
    bcos-utilities
    bcos-evm-eth
    bcos-crypto
    codec
    evmone::evmone
)

add_library(bcos-evm-specs-tests-eth STATIC
    src/ExecuteViaEthAdapter.cpp
)

target_link_libraries(bcos-evm-specs-tests-eth PUBLIC
    bcos-evm-specs-tests-core
    bcos-evm-eth
    bcos-crypto
    bcos-task
)

add_library(bcos-evm-specs-tests-opstack STATIC
    runners/OpStackEestAdapter.cpp
)

target_include_directories(bcos-evm-specs-tests-opstack PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)

target_link_libraries(bcos-evm-specs-tests-opstack PUBLIC
    bcos-evm-specs-tests-core
    bcos-evm-op
    bcos-evm-eth
    bcos-crypto
    bcos-task
)

function(add_reference_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE bcos-evm-specs-tests-core)
    target_compile_definitions(${target} PRIVATE
        SPECS_TESTS_MANIFEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/manifests"
    )
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES
        LABELS "specs-tests;specs-tests-smoke"
    )
endfunction()

add_reference_test(GstStateHashTest runners/GstStateHashTest.cpp)
add_reference_test(ManifestLoaderTest runners/ManifestLoaderTest.cpp)
add_reference_test(GeneralStateTestLoaderTest runners/GeneralStateTestLoaderTest.cpp)
add_reference_test(ForkProfileRegistryTest runners/ForkProfileRegistryTest.cpp)
add_reference_test(Eip7702StrictTxValidatorTest runners/Eip7702StrictTxValidatorTest.cpp)
add_reference_test(StateTestMatcherTest runners/StateTestMatcherTest.cpp)
add_reference_test(StateTestAssertTest runners/StateTestAssertTest.cpp)

add_executable(ExecuteViaEthAdapterTest runners/ExecuteViaEthAdapterTest.cpp)
target_link_libraries(ExecuteViaEthAdapterTest PRIVATE bcos-evm-specs-tests-eth)
add_test(NAME ExecuteViaEthAdapterTest COMMAND ExecuteViaEthAdapterTest)
set_tests_properties(ExecuteViaEthAdapterTest PROPERTIES
    LABELS "specs-tests;specs-tests-smoke"
)

add_executable(EthGSTSmoke runners/EthGSTSmoke.cpp)
target_link_libraries(EthGSTSmoke PRIVATE bcos-evm-specs-tests-eth)
target_compile_definitions(EthGSTSmoke PRIVATE
    SPECS_TESTS_MANIFEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/manifests"
)
add_test(NAME EthGSTSmoke COMMAND EthGSTSmoke
    --manifest ${CMAKE_CURRENT_SOURCE_DIR}/manifests/eth-gst-prague-smoke.json
    --ethereum-tests-root ${EVM_REF_ETHEREUM_TESTS_ROOT}
    --expectations ${CMAKE_CURRENT_SOURCE_DIR}/manifests/expectations.json
)
set_tests_properties(EthGSTSmoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel"
)

add_executable(EthGSTOsakaSmoke runners/EthGSTSmoke.cpp)
target_link_libraries(EthGSTOsakaSmoke PRIVATE bcos-evm-specs-tests-eth)
target_compile_definitions(EthGSTOsakaSmoke PRIVATE
    SPECS_TESTS_MANIFEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/manifests"
)
add_test(NAME EthGSTOsakaSmoke COMMAND EthGSTOsakaSmoke
    --manifest ${CMAKE_CURRENT_SOURCE_DIR}/manifests/eth-gst-osaka-smoke.json
    --ethereum-tests-root ${EVM_REF_ETHEREUM_TESTS_ROOT}
    --expectations ${CMAKE_CURRENT_SOURCE_DIR}/manifests/expectations.json
)
set_tests_properties(EthGSTOsakaSmoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel"
)

add_executable(EthGSTFull runners/EthGSTFull.cpp)
target_link_libraries(EthGSTFull PRIVATE bcos-evm-specs-tests-eth)
target_compile_definitions(EthGSTFull PRIVATE
    SPECS_TESTS_MANIFEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/manifests"
)
add_test(NAME EthGSTFull COMMAND EthGSTFull
    --ethereum-tests-root ${EVM_REF_ETHEREUM_TESTS_ROOT}
    --expectations ${CMAKE_CURRENT_SOURCE_DIR}/manifests/expectations.json
    --fork-profiles eth-cancun,eth-prague,eth-osaka
    --case-prefix GeneralStateTests/stExample
    --limit 3
)
set_tests_properties(EthGSTFull PROPERTIES
    LABELS "specs-tests;specs-tests-full;eth-kernel"
)

set(EEST_MANIFEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/manifests)

add_executable(EthExecutionSpecStateTests runners/EthExecutionSpecStateTests.cpp)
target_link_libraries(EthExecutionSpecStateTests PRIVATE bcos-evm-specs-tests-eth)
target_compile_definitions(EthExecutionSpecStateTests PRIVATE
    SPECS_TESTS_MANIFEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/manifests"
)
add_test(NAME EthExecutionSpecStateTests COMMAND EthExecutionSpecStateTests
    --manifest ${EEST_MANIFEST_DIR}/eth-eest-state-smoke.json
    --eest-root ${EVM_REF_EEST_ROOT}
    --expectations ${EEST_MANIFEST_DIR}/expectations.json
)
set_tests_properties(EthExecutionSpecStateTests PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel;eest"
)

add_test(NAME EthExecutionSpecStateTestsFull COMMAND EthExecutionSpecStateTests
    --manifest ${EEST_MANIFEST_DIR}/eth-eest-state-full.json
    --eest-root ${EVM_REF_EEST_ROOT}
    --expectations ${EEST_MANIFEST_DIR}/expectations.json
)
set_tests_properties(EthExecutionSpecStateTestsFull PROPERTIES
    LABELS "specs-tests;specs-tests-full;eth-kernel;eest"
)

add_test(NAME EthExecutionSpec1559GaspriceProbe COMMAND EthExecutionSpecStateTests
    --manifest ${EEST_MANIFEST_DIR}/eth-eest-1559-gasprice-probe.json
    --eest-root ${EVM_REF_EEST_ROOT}
    --expectations ${EEST_MANIFEST_DIR}/expectations.json
)
set_tests_properties(EthExecutionSpec1559GaspriceProbe PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel;eest;1559-gasprice"
)

add_executable(EthExecutionSpecTransactionTests runners/EthExecutionSpecTransactionTests.cpp)
target_link_libraries(EthExecutionSpecTransactionTests PRIVATE bcos-evm-specs-tests-core)
target_compile_definitions(EthExecutionSpecTransactionTests PRIVATE
    SPECS_TESTS_MANIFEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/manifests"
)
add_test(NAME EthExecutionSpecTransactionTests COMMAND EthExecutionSpecTransactionTests
    --manifest ${EEST_MANIFEST_DIR}/eth-eest-tx-smoke.json
    --eest-root ${EVM_REF_EEST_ROOT}
)
set_tests_properties(EthExecutionSpecTransactionTests PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel;eest"
)

add_test(NAME EthExecutionSpecTransactionTestsFull COMMAND EthExecutionSpecTransactionTests
    --manifest ${EEST_MANIFEST_DIR}/eth-eest-tx-full.json
    --eest-root ${EVM_REF_EEST_ROOT}
)
set_tests_properties(EthExecutionSpecTransactionTestsFull PROPERTIES
    LABELS "specs-tests;specs-tests-full;eth-kernel;eest"
)

add_test(
    NAME ManifestLint
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/manifest_lint.py
        --manifests ${CMAKE_CURRENT_SOURCE_DIR}/manifests
        --capability-rows ${CMAKE_CURRENT_SOURCE_DIR}/manifests/capability-rows.json
)
set_tests_properties(ManifestLint PROPERTIES
    LABELS "specs-tests;specs-tests-smoke"
)

# --- OPStack EEST State Runner ---

add_executable(OpStackEestStateTest
    runners/OpStackEestStateRunner.cpp
    runners/OpStackEestAdapter.cpp
)
target_link_libraries(OpStackEestStateTest PRIVATE
    bcos-evm-specs-tests-core
    bcos-evm-op
    bcos-evm-eth
    bcos-crypto
    bcos-task
)
target_compile_definitions(OpStackEestStateTest PRIVATE
    SPECS_TESTS_MANIFEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/manifests"
)

# Smoke test: runs a curated subset of EEST state tests through OPStack path
add_test(NAME OpStackEestStateSmoke COMMAND OpStackEestStateTest
    --fixtures ${EVM_REF_EEST_ROOT}/fixtures/state_tests
    --smoke
    --limit 50
)
set_tests_properties(OpStackEestStateSmoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;opstack;eest"
)

# Full test: runs all compatible EEST state tests through OPStack path (nightly)
add_test(NAME OpStackEestStateFull COMMAND OpStackEestStateTest
    --fixtures ${EVM_REF_EEST_ROOT}/fixtures/state_tests
    --skip-list ${CMAKE_CURRENT_SOURCE_DIR}/opstack-skip-list.json
)
set_tests_properties(OpStackEestStateFull PROPERTIES
    LABELS "specs-tests;specs-tests-full;opstack;eest;nightly"
)

# --- OPStack EEST Blockchain Runner ---

add_executable(OpStackEestBlockchainTest
    runners/OpStackEestBlockchainRunner.cpp
    runners/OpStackEestAdapter.cpp
)
target_link_libraries(OpStackEestBlockchainTest PRIVATE
    bcos-evm-specs-tests-core
    bcos-evm-op
    bcos-evm-eth
    bcos-crypto
    bcos-task
)
add_test(NAME OpStackEestBlockchainSmoke COMMAND OpStackEestBlockchainTest
    --fixtures ${EVM_REF_EEST_ROOT}/fixtures/blockchain_tests
    --limit 10
)
set_tests_properties(OpStackEestBlockchainSmoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;opstack;eest"
)

add_test(NAME OpStackEestBlockchainFull COMMAND OpStackEestBlockchainTest
    --fixtures ${EVM_REF_EEST_ROOT}/fixtures/blockchain_tests
)
set_tests_properties(OpStackEestBlockchainFull PROPERTIES
    LABELS "specs-tests;specs-tests-full;opstack;eest;nightly"
)

# --- OPStack EEST Transaction Runner ---

add_executable(OpStackEestTxTest
    runners/OpStackEestTxRunner.cpp
)
target_link_libraries(OpStackEestTxTest PRIVATE
    bcos-evm-specs-tests-core
    bcos-evm-eth
    bcos-utilities
)
add_test(NAME OpStackEestTxSmoke COMMAND OpStackEestTxTest
    --fixtures ${EVM_REF_EEST_ROOT}/fixtures/transaction_tests
    --limit 10
)
set_tests_properties(OpStackEestTxSmoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;opstack;eest"
)

add_test(NAME OpStackEestTxFull COMMAND OpStackEestTxTest
    --fixtures ${EVM_REF_EEST_ROOT}/fixtures/transaction_tests
)
set_tests_properties(OpStackEestTxFull PROPERTIES
    LABELS "specs-tests;specs-tests-full;opstack;eest;nightly"
)
