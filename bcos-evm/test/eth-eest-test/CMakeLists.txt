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
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../..>
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
    src/EthMessageAdapter.cpp
)

target_link_libraries(bcos-evm-specs-tests-eth PUBLIC
    bcos-evm-specs-tests-core
    bcos-evm-eth
    bcos-crypto
    bcos-task
)

add_library(bcos-evm-specs-tests-opstack STATIC
    src/OpStackEestAdapter.cpp
    src/OpStackManifestAdapter.cpp
)

target_include_directories(bcos-evm-specs-tests-opstack PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../..>
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

add_reference_test(GstStateHashTest test/GstStateHashTest.cpp)
add_reference_test(ManifestLoaderTest test/ManifestLoaderTest.cpp)
add_reference_test(GeneralStateTestLoaderTest test/GeneralStateTestLoaderTest.cpp)
add_reference_test(ForkProfileRegistryTest test/ForkProfileRegistryTest.cpp)
add_reference_test(Eip7702StrictTxValidatorTest test/Eip7702StrictTxValidatorTest.cpp)
add_reference_test(StateTestMatcherTest test/StateTestMatcherTest.cpp)
add_reference_test(StateTestAssertTest test/StateTestAssertTest.cpp)

add_executable(EthMessageAdapterTest test/EthMessageAdapterTest.cpp)
target_link_libraries(EthMessageAdapterTest PRIVATE bcos-evm-specs-tests-eth)
add_test(NAME EthMessageAdapterTest COMMAND EthMessageAdapterTest)
set_tests_properties(EthMessageAdapterTest PROPERTIES
    LABELS "specs-tests;specs-tests-smoke"
)

add_executable(EthGSTSmoke runners/eth/EthGSTSmoke.cpp)
target_link_libraries(EthGSTSmoke PRIVATE bcos-evm-specs-tests-eth)
target_compile_definitions(EthGSTSmoke PRIVATE
    SPECS_TESTS_MANIFEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/manifests"
)
add_test(NAME EthGSTSmoke COMMAND EthGSTSmoke
    --manifest ${CMAKE_CURRENT_SOURCE_DIR}/manifests/eth/eth-gst-prague-smoke.json
    --ethereum-tests-root ${EVM_REF_ETHEREUM_TESTS_ROOT}
    --expectations ${CMAKE_CURRENT_SOURCE_DIR}/manifests/expectations.json
)
set_tests_properties(EthGSTSmoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel"
)

add_executable(EthGSTOsakaSmoke runners/eth/EthGSTSmoke.cpp)
target_link_libraries(EthGSTOsakaSmoke PRIVATE bcos-evm-specs-tests-eth)
target_compile_definitions(EthGSTOsakaSmoke PRIVATE
    SPECS_TESTS_MANIFEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/manifests"
)
add_test(NAME EthGSTOsakaSmoke COMMAND EthGSTOsakaSmoke
    --manifest ${CMAKE_CURRENT_SOURCE_DIR}/manifests/eth/eth-gst-osaka-smoke.json
    --ethereum-tests-root ${EVM_REF_ETHEREUM_TESTS_ROOT}
    --expectations ${CMAKE_CURRENT_SOURCE_DIR}/manifests/expectations.json
)
set_tests_properties(EthGSTOsakaSmoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel"
)

add_executable(EthGSTFull runners/eth/EthGSTFull.cpp)
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

add_executable(EthExecutionSpecStateTests runners/eth/EthExecutionSpecStateTests.cpp)
target_link_libraries(EthExecutionSpecStateTests PRIVATE bcos-evm-specs-tests-eth)
target_compile_definitions(EthExecutionSpecStateTests PRIVATE
    SPECS_TESTS_MANIFEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/manifests"
)
add_test(NAME EthExecutionSpecStateTests COMMAND EthExecutionSpecStateTests
    --manifest ${EEST_MANIFEST_DIR}/eth/eth-eest-state-smoke.json
    --eest-root ${EVM_REF_EEST_ROOT}
    --expectations ${EEST_MANIFEST_DIR}/expectations.json
)
set_tests_properties(EthExecutionSpecStateTests PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel;eest"
)

add_test(NAME EthExecutionSpecStateTestsFull COMMAND EthExecutionSpecStateTests
    --manifest ${EEST_MANIFEST_DIR}/eth/eth-eest-state-full.json
    --eest-root ${EVM_REF_EEST_ROOT}
    --expectations ${EEST_MANIFEST_DIR}/expectations.json
)
set_tests_properties(EthExecutionSpecStateTestsFull PROPERTIES
    LABELS "specs-tests;specs-tests-full;eth-kernel;eest"
)

add_test(NAME EthExecutionSpec1559GaspriceProbe COMMAND EthExecutionSpecStateTests
    --manifest ${EEST_MANIFEST_DIR}/eth/eth-eest-1559-gasprice-probe.json
    --eest-root ${EVM_REF_EEST_ROOT}
    --expectations ${EEST_MANIFEST_DIR}/expectations.json
)
set_tests_properties(EthExecutionSpec1559GaspriceProbe PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel;eest;1559-gasprice"
)

add_test(NAME EthExecutionSpecNonceTests COMMAND EthExecutionSpecStateTests
    --manifest ${EEST_MANIFEST_DIR}/eth/eth-eest-nonce-smoke.json
    --eest-root ${EVM_REF_EEST_ROOT}
    --expectations ${EEST_MANIFEST_DIR}/expectations.json
)
set_tests_properties(EthExecutionSpecNonceTests PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel;eest;nonce"
)

add_test(NAME EthExecutionSpec7702CoreSmoke COMMAND EthExecutionSpecStateTests
    --manifest ${EEST_MANIFEST_DIR}/eth/eth-eest-7702-core-smoke.json
    --eest-root ${EVM_REF_EEST_ROOT}
    --expectations ${EEST_MANIFEST_DIR}/expectations.json
)
set_tests_properties(EthExecutionSpec7702CoreSmoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel;eest;7702"
)

add_test(NAME EthExecutionSpec6780Smoke COMMAND EthExecutionSpecStateTests
    --manifest ${EEST_MANIFEST_DIR}/eth/eth-eest-6780-smoke.json
    --eest-root ${EVM_REF_EEST_ROOT}
    --expectations ${EEST_MANIFEST_DIR}/expectations.json
)
set_tests_properties(EthExecutionSpec6780Smoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel;eest;6780"
)

# --- High-priority EIP smoke manifests ---
add_test(NAME EthExecutionSpec2537BlsSmoke COMMAND EthExecutionSpecStateTests
    --manifest ${EEST_MANIFEST_DIR}/eth/eth-eest-2537-bls-smoke.json
    --eest-root ${EVM_REF_EEST_ROOT}
    --expectations ${EEST_MANIFEST_DIR}/expectations.json
)
set_tests_properties(EthExecutionSpec2537BlsSmoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel;eest;2537"
)

add_test(NAME EthExecutionSpec1153TstoreSmoke COMMAND EthExecutionSpecStateTests
    --manifest ${EEST_MANIFEST_DIR}/eth/eth-eest-1153-tstore-smoke.json
    --eest-root ${EVM_REF_EEST_ROOT}
    --expectations ${EEST_MANIFEST_DIR}/expectations.json
)
set_tests_properties(EthExecutionSpec1153TstoreSmoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel;eest;1153"
)

add_test(NAME EthExecutionSpec5656McopySmoke COMMAND EthExecutionSpecStateTests
    --manifest ${EEST_MANIFEST_DIR}/eth/eth-eest-5656-mcopy-smoke.json
    --eest-root ${EVM_REF_EEST_ROOT}
    --expectations ${EEST_MANIFEST_DIR}/expectations.json
)
set_tests_properties(EthExecutionSpec5656McopySmoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel;eest;5656"
)

add_test(NAME EthExecutionSpec3855Push0Smoke COMMAND EthExecutionSpecStateTests
    --manifest ${EEST_MANIFEST_DIR}/eth/eth-eest-3855-push0-smoke.json
    --eest-root ${EVM_REF_EEST_ROOT}
    --expectations ${EEST_MANIFEST_DIR}/expectations.json
)
set_tests_properties(EthExecutionSpec3855Push0Smoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel;eest;3855"
)

add_test(NAME EthExecutionSpec3860InitcodeSmoke COMMAND EthExecutionSpecStateTests
    --manifest ${EEST_MANIFEST_DIR}/eth/eth-eest-3860-initcode-smoke.json
    --eest-root ${EVM_REF_EEST_ROOT}
    --expectations ${EEST_MANIFEST_DIR}/expectations.json
)
set_tests_properties(EthExecutionSpec3860InitcodeSmoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel;eest;3860"
)

# --- Phase 2 EIP smoke manifests ---
add_test(NAME EthExecutionSpec3651WarmCoinbaseSmoke COMMAND EthExecutionSpecStateTests
    --manifest ${EEST_MANIFEST_DIR}/eth/eth-eest-3651-warm-coinbase-smoke.json
    --eest-root ${EVM_REF_EEST_ROOT}
    --expectations ${EEST_MANIFEST_DIR}/expectations.json
)
set_tests_properties(EthExecutionSpec3651WarmCoinbaseSmoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel;eest;3651"
)

add_test(NAME EthExecutionSpec4844BlobSmoke COMMAND EthExecutionSpecStateTests
    --manifest ${EEST_MANIFEST_DIR}/eth/eth-eest-4844-blob-smoke.json
    --eest-root ${EVM_REF_EEST_ROOT}
    --expectations ${EEST_MANIFEST_DIR}/expectations.json
)
set_tests_properties(EthExecutionSpec4844BlobSmoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel;eest;4844"
)

add_test(NAME EthExecutionSpec7951P256Smoke COMMAND EthExecutionSpecStateTests
    --manifest ${EEST_MANIFEST_DIR}/eth/eth-eest-7951-p256-smoke.json
    --eest-root ${EVM_REF_EEST_ROOT}
    --expectations ${EEST_MANIFEST_DIR}/expectations.json
)
set_tests_properties(EthExecutionSpec7951P256Smoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel;eest;7951"
)

add_test(NAME EthExecutionSpec7883ModexpGasSmoke COMMAND EthExecutionSpecStateTests
    --manifest ${EEST_MANIFEST_DIR}/eth/eth-eest-7883-modexp-gas-smoke.json
    --eest-root ${EVM_REF_EEST_ROOT}
    --expectations ${EEST_MANIFEST_DIR}/expectations.json
)
set_tests_properties(EthExecutionSpec7883ModexpGasSmoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel;eest;7883"
)

add_test(NAME EthExecutionSpec7825GasLimitCapSmoke COMMAND EthExecutionSpecStateTests
    --manifest ${EEST_MANIFEST_DIR}/eth/eth-eest-7825-gas-limit-cap-smoke.json
    --eest-root ${EVM_REF_EEST_ROOT}
    --expectations ${EEST_MANIFEST_DIR}/expectations.json
)
set_tests_properties(EthExecutionSpec7825GasLimitCapSmoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel;eest;7825"
)

# --- Phase 3 static regression ---
add_test(NAME EthExecutionSpecStaticRegressionSmoke COMMAND EthExecutionSpecStateTests
    --manifest ${EEST_MANIFEST_DIR}/eth/eth-eest-static-regression-smoke.json
    --eest-root ${EVM_REF_EEST_ROOT}
    --expectations ${EEST_MANIFEST_DIR}/expectations.json
)
set_tests_properties(EthExecutionSpecStaticRegressionSmoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel;eest;static-regression"
)

add_test(NAME EthExecutionSpecStaticRegressionFull COMMAND EthExecutionSpecStateTests
    --manifest ${EEST_MANIFEST_DIR}/eth/eth-eest-static-regression-full.json
    --eest-root ${EVM_REF_EEST_ROOT}
    --expectations ${EEST_MANIFEST_DIR}/expectations.json
)
set_tests_properties(EthExecutionSpecStaticRegressionFull PROPERTIES
    LABELS "specs-tests;specs-tests-full;eth-kernel;eest;static-regression"
)

# --- EEST probe manifests (nightly; stateRoot parity radar) ---
set(_evm_eest_probe_manifests
    eth/eth-eest-probe-revert.json
    eth/eth-eest-probe-return.json
    eth/eth-eest-probe-oog.json
    eth/eth-eest-probe-invalid.json
    eth/eth-eest-precompile-probe-full.json
    eth/probe-gas-cost-one.json
    eth/eth-eest-precompile-probe.json
    eth/eth-eest-opcode-probe.json
)
set(_evm_eest_probe_ctest_names
    EthExecutionSpecProbeRevert
    EthExecutionSpecProbeReturn
    EthExecutionSpecProbeOog
    EthExecutionSpecProbeInvalid
    EthExecutionSpecPrecompileProbeFull
    EthExecutionSpecProbeGasCostOne
    EthExecutionSpecPrecompileProbe
    EthExecutionSpecOpcodeProbe
)
list(LENGTH _evm_eest_probe_manifests _evm_eest_probe_count)
math(EXPR _evm_eest_probe_last "${_evm_eest_probe_count} - 1")
foreach(i RANGE ${_evm_eest_probe_last})
    list(GET _evm_eest_probe_manifests ${i} _probe_manifest)
    list(GET _evm_eest_probe_ctest_names ${i} _probe_ctest)
    add_test(NAME ${_probe_ctest} COMMAND EthExecutionSpecStateTests
        --manifest ${EEST_MANIFEST_DIR}/${_probe_manifest}
        --eest-root ${EVM_REF_EEST_ROOT}
        --expectations ${EEST_MANIFEST_DIR}/expectations.json
    )
    set_tests_properties(${_probe_ctest} PROPERTIES
        LABELS "specs-tests;specs-tests-full;eth-kernel;eest;probe"
    )
endforeach()

add_executable(EthExecutionSpecTransactionTests runners/eth/EthExecutionSpecTransactionTests.cpp)
target_link_libraries(EthExecutionSpecTransactionTests PRIVATE bcos-evm-specs-tests-core)
target_compile_definitions(EthExecutionSpecTransactionTests PRIVATE
    SPECS_TESTS_MANIFEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/manifests"
)
add_test(NAME EthExecutionSpecTransactionTests COMMAND EthExecutionSpecTransactionTests
    --manifest ${EEST_MANIFEST_DIR}/eth/eth-eest-tx-smoke.json
    --eest-root ${EVM_REF_EEST_ROOT}
)
set_tests_properties(EthExecutionSpecTransactionTests PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;eth-kernel;eest"
)

add_test(NAME EthExecutionSpecTransactionTestsFull COMMAND EthExecutionSpecTransactionTests
    --manifest ${EEST_MANIFEST_DIR}/eth/eth-eest-tx-full.json
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

add_executable(OpStackEestStateTest runners/opstack/OpStackEestStateRunner.cpp)
target_link_libraries(OpStackEestStateTest PRIVATE
    bcos-evm-specs-tests-opstack
    bcos-evm-specs-tests-core
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
    --skip-list ${CMAKE_CURRENT_SOURCE_DIR}/manifests/opstack/opstack-skip-list.json
)
set_tests_properties(OpStackEestStateFull PROPERTIES
    LABELS "specs-tests;specs-tests-full;opstack;eest;nightly"
)

# --- OPStack EEST Blockchain Runner ---

add_executable(OpStackEestBlockchainTest runners/opstack/OpStackEestBlockchainRunner.cpp)
target_link_libraries(OpStackEestBlockchainTest PRIVATE
    bcos-evm-specs-tests-opstack
    bcos-evm-specs-tests-core
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
    runners/opstack/OpStackEestTxRunner.cpp
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

# --- OPStack Manifest-Driven State Runner ---

add_executable(OpStackEestManifestStateTest
    runners/opstack/OpStackEestManifestStateTests.cpp
)
target_link_libraries(OpStackEestManifestStateTest PRIVATE
    bcos-evm-specs-tests-opstack
    bcos-evm-specs-tests-core
)
target_compile_definitions(OpStackEestManifestStateTest PRIVATE
    SPECS_TESTS_MANIFEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/manifests"
)

add_test(NAME OpStackEestManifestStateSmoke COMMAND OpStackEestManifestStateTest
    --manifest ${EEST_MANIFEST_DIR}/opstack/opstack-eest-state-smoke.json
    --eest-root ${EVM_REF_EEST_ROOT}
    --expectations ${EEST_MANIFEST_DIR}/opstack/opstack-expectations.json
)
set_tests_properties(OpStackEestManifestStateSmoke PROPERTIES
    LABELS "specs-tests;specs-tests-smoke;opstack;eest;manifest"
)

add_test(NAME OpStackEestManifestStateFull COMMAND OpStackEestManifestStateTest
    --manifest ${EEST_MANIFEST_DIR}/opstack/opstack-eest-state-full.json
    --eest-root ${EVM_REF_EEST_ROOT}
    --expectations ${EEST_MANIFEST_DIR}/opstack/opstack-expectations.json
)
set_tests_properties(OpStackEestManifestStateFull PROPERTIES
    LABELS "specs-tests;specs-tests-full;opstack;eest;manifest;nightly"
    TIMEOUT 7200
)

# Per-collection manifest slices (run individually; avoid monolithic Full timeout).
set(_OPSTACK_MANIFEST_SLICE_DIR ${EEST_MANIFEST_DIR}/opstack/slices)
set(_OPSTACK_MANIFEST_SLICE_COMMON
    COMMAND OpStackEestManifestStateTest
    --eest-root ${EVM_REF_EEST_ROOT}
    --expectations ${EEST_MANIFEST_DIR}/opstack/opstack-expectations.json
)
set(_OPSTACK_MANIFEST_SLICE_LABELS "specs-tests;specs-tests-slice;opstack;eest;manifest")

add_test(NAME OpStackEestManifestSliceEip3855 ${_OPSTACK_MANIFEST_SLICE_COMMON}
    --manifest ${_OPSTACK_MANIFEST_SLICE_DIR}/eip3855-push0.json
)
set_tests_properties(OpStackEestManifestSliceEip3855 PROPERTIES
    LABELS "${_OPSTACK_MANIFEST_SLICE_LABELS}" TIMEOUT 600)

add_test(NAME OpStackEestManifestSliceEip3860 ${_OPSTACK_MANIFEST_SLICE_COMMON}
    --manifest ${_OPSTACK_MANIFEST_SLICE_DIR}/eip3860-initcode.json
)
set_tests_properties(OpStackEestManifestSliceEip3860 PROPERTIES
    LABELS "${_OPSTACK_MANIFEST_SLICE_LABELS}" TIMEOUT 600)

add_test(NAME OpStackEestManifestSliceEip1153 ${_OPSTACK_MANIFEST_SLICE_COMMON}
    --manifest ${_OPSTACK_MANIFEST_SLICE_DIR}/eip1153-tstore.json
)
set_tests_properties(OpStackEestManifestSliceEip1153 PROPERTIES
    LABELS "${_OPSTACK_MANIFEST_SLICE_LABELS}" TIMEOUT 14400)

add_test(NAME OpStackEestManifestSliceEip1153Oog ${_OPSTACK_MANIFEST_SLICE_COMMON}
    --manifest ${_OPSTACK_MANIFEST_SLICE_DIR}/eip1153-tstore-oog.json
)
set_tests_properties(OpStackEestManifestSliceEip1153Oog PROPERTIES
    LABELS "${_OPSTACK_MANIFEST_SLICE_LABELS}" TIMEOUT 3600)

add_test(NAME OpStackEestManifestSliceEip5656 ${_OPSTACK_MANIFEST_SLICE_COMMON}
    --manifest ${_OPSTACK_MANIFEST_SLICE_DIR}/eip5656-mcopy.json
)
set_tests_properties(OpStackEestManifestSliceEip5656 PROPERTIES
    LABELS "${_OPSTACK_MANIFEST_SLICE_LABELS}" TIMEOUT 7200)

add_test(NAME OpStackEestManifestSliceEip7702 ${_OPSTACK_MANIFEST_SLICE_COMMON}
    --manifest ${_OPSTACK_MANIFEST_SLICE_DIR}/eip7702-set-code.json
)
set_tests_properties(OpStackEestManifestSliceEip7702 PROPERTIES
    LABELS "${_OPSTACK_MANIFEST_SLICE_LABELS}" TIMEOUT 3600)
