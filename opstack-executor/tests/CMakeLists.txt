find_package(Boost REQUIRED unit_test_framework)
find_package(evmone REQUIRED)
# rlp_encode.cpp left the production lib (zero callers); its only consumer, OpReceiptEncodeTest,
# ADL-calls evmone::state::rlp_encode. Re-derive EVMONE_TEST_UTILS_DIR here — set only in sibling bcos-evm scope.
get_target_property(_evmone_inc_dirs evmone::evmone INTERFACE_INCLUDE_DIRECTORIES)
list(GET _evmone_inc_dirs 0 _evmone_inc_dir)
cmake_path(NORMAL_PATH _evmone_inc_dir)
set(EVMONE_TEST_UTILS_DIR "${_evmone_inc_dir}/test/utils")

# opstack-executor-tests was the repo's only GTest target; converted to Boost.Test to drop the
# gtest dependency (not in vcpkg.json, broke CI configure). BOOST_TEST_MODULE is defined in the TU.
add_executable(opstack-executor-tests OpstackExecutorTest.cpp)
target_link_libraries(opstack-executor-tests PRIVATE
    opstack-executor           # INTERFACE: pulls ethereum-executor + bcos-evm-opstack + bcos-evm-eth + bcos-framework
    protocol-tars              # TransactionImpl / BlockHeaderImpl / TransactionReceiptFactoryImpl
    bcos-crypto                # CryptoSuite / Keccak256
    codec                      # rlp::decode for the web3 tx fixture
    rpc                        # bcos::rpc::Web3Transaction (model .cpp)
    evmone::evmone
    Boost::unit_test_framework)
# Web3Transaction + tars aggregate init are not unity-build friendly here.
set_target_properties(opstack-executor-tests PROPERTIES UNITY_BUILD OFF)
add_test(NAME OpstackExecutorTests COMMAND opstack-executor-tests)

find_package(jsoncpp CONFIG REQUIRED)
# StateBackendRocksDB drives raw rocksdb::DB directly (no bcos::storage::RocksDBStorage);
# bcos-storage is added after this scope at the top level, so the RocksDB imported target
# must be resolved here.
find_package(RocksDB REQUIRED)

# evmone_precompiles' interface adds a bare `-lblst`; with the shared-vcpkg worktree layout the
# vcpkg lib dir is not on the linker search path, so resolve the library to a full path here.
find_library(BLST_LIBRARY NAMES blst PATHS
    "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/lib"
    NO_DEFAULT_PATH REQUIRED)
get_filename_component(BLST_LIB_DIR ${BLST_LIBRARY} DIRECTORY)

add_executable(opstack-executor-block-tests
    TestMain.cpp
    OpT8nReplayTest.cpp
    OpReceiptEncodeTest.cpp
    OpL1EdgeGateTest.cpp
    OpNewPayloadRpcE2eTest.cpp
    OpSchedulerSeamSmokeTest.cpp
    OpEngineBranchSmokeTest.cpp
    OpSchedulerTest.cpp
    OpBlockInjectorTest.cpp
    OpDualPathEquivalenceTest.cpp
    Storage2StatePoisonTest.cpp
    # op-alignment line test additions ported across (adapted to the refactored headers)
    OpL1BlockDepositTest.cpp
    OpDaMatrixSchemaTest.cpp
    OpJovianShapeTest.cpp
    OpKarstReleaseGateTest.cpp
    OpKarstActivationTest.cpp
    OpGoldenCorpusProvenanceTest.cpp
    support/SeedPreStateTest.cpp
    support/GoldenSampleTest.cpp
    support/KarstNutHelpers.cpp
    support/StateBackendRocksDB.cpp
    StateBackendRocksDBTest.cpp
    ${EVMONE_TEST_UTILS_DIR}/rlp_encode.cpp
    ${EVMONE_TEST_UTILS_DIR}/test_state.cpp
)
# 与既有 GTest target 同因：Web3Transaction + tars aggregate init 不宜 unity-build
set_target_properties(opstack-executor-block-tests PROPERTIES UNITY_BUILD OFF)
# Vendored evmone test util — same -Wno-missing-field-initializers as the upstream build.
set_source_files_properties(${EVMONE_TEST_UTILS_DIR}/rlp_encode.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers")
set_source_files_properties(${EVMONE_TEST_UTILS_DIR}/test_state.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers")
target_link_libraries(opstack-executor-block-tests PRIVATE
    opstack-executor bcosevm::opstack Boost::unit_test_framework
    codec protocol-tars ledger engine bcos-utilities jsoncpp_static rpc
    transaction-scheduler)  # OpSchedulerTest: SchedulerSkeleton::current() (BaselineScheduler.cpp)
# StateBackendRocksDB: raw rocksdb::DB over the vcpkg RocksDB (live-chain state backend).
target_link_libraries(opstack-executor-block-tests PRIVATE RocksDB::rocksdb)
# evmone_precompiles' bare -lblst needs the vcpkg lib dir on the search path (shared-vcpkg
# worktree layout does not put it in the implicit link directories).
target_link_libraries(opstack-executor-block-tests PRIVATE ${BLST_LIBRARY})
target_include_directories(opstack-executor-block-tests PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/bcos-ledger
    ${CMAKE_SOURCE_DIR}/bcos-rpc
    ${EVMONE_TEST_UTILS_DIR})
target_compile_definitions(opstack-executor-block-tests PRIVATE
    OP_T8N_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/t8n/vectors"
    OP_T8N_GOLDEN_ENGINE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/t8n/golden/engine"
    OP_DA_MATRIX_PATH="${CMAKE_CURRENT_SOURCE_DIR}/da-matrix/da_matrix.json"
    OP_KARST_NUT_BUNDLE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/karst_nut_bundle.json")
add_test(NAME OpstackExecutorBlockTests COMMAND opstack-executor-block-tests)

add_custom_target(op-karst-release-gate
    COMMAND ${CMAKE_SOURCE_DIR}/tools/check-op-karst-release-gate.sh
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/opstack-executor-block-tests --run_test=OpKarstReleaseGateSuite
    DEPENDS opstack-executor-block-tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Karst atomic release gate (scanner + OpKarstReleaseGateSuite)")
if(TARGET fisco-bcos)
    add_custom_target(op-karst-release DEPENDS fisco-bcos op-karst-release-gate)
endif()
add_test(NAME OpKarstReleaseGate
    COMMAND bash -c
        "${CMAKE_SOURCE_DIR}/tools/check-op-karst-release-gate.sh && \
        ${CMAKE_CURRENT_BINARY_DIR}/opstack-executor-block-tests --run_test=OpKarstReleaseGateSuite")

# opstack-mainnet-replay — live-chain replay CLI (ReplayGate.h + RocksDB backend).
# Shares the replay machinery with the corpus gate; zero-tolerance per-block compare,
# sidecar-bootstrapped RocksDB state, --check-import-root tripwire.
add_executable(opstack-mainnet-replay
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/op-mainnet-replay/replay_cli.cpp
    support/StateBackendRocksDB.cpp)
set_target_properties(opstack-mainnet-replay PROPERTIES UNITY_BUILD OFF)
target_include_directories(opstack-mainnet-replay PRIVATE
    ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(opstack-mainnet-replay PRIVATE
    opstack-executor bcosevm::opstack jsoncpp_static evmone::evmone intx::intx
    bcos-utilities protocol-tars RocksDB::rocksdb ${BLST_LIBRARY})

# op-mainnet-replay smoke gates (fixtures committed under tools/op-mainnet-replay/fixtures).
set(REPLAY_FIXTURES ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/op-mainnet-replay/fixtures)
add_test(NAME ReplayCliSmokeGreen COMMAND opstack-mainnet-replay
    --chain ${REPLAY_FIXTURES}/smoke_green.json)
add_test(NAME ReplayCliSmokeRed COMMAND opstack-mainnet-replay
    --chain ${REPLAY_FIXTURES}/smoke_red.json)
set_tests_properties(ReplayCliSmokeRed PROPERTIES WILL_FAIL TRUE)
add_test(NAME ReplayCliAllowlist COMMAND opstack-mainnet-replay
    --chain ${REPLAY_FIXTURES}/smoke_red.json
    --allowlist ${REPLAY_FIXTURES}/smoke_allowlist.json)
add_test(NAME ReplayCliLiveSidecar COMMAND opstack-mainnet-replay
    --chain ${REPLAY_FIXTURES}/live_smoke.json
    --sidecar ${REPLAY_FIXTURES}/live_smoke.sidecar
    --rocks ${CMAKE_CURRENT_BINARY_DIR}/replay-cli-live-rocks
    --skip-poststate --chain-id 0x2105)
add_test(NAME ReplayCliBadChainId COMMAND opstack-mainnet-replay
    --chain ${REPLAY_FIXTURES}/smoke_green.json --chain-id abc)
set_tests_properties(ReplayCliBadChainId PROPERTIES WILL_FAIL TRUE)
add_test(NAME ReplayCliSidecarNeedsRocks COMMAND opstack-mainnet-replay
    --chain ${REPLAY_FIXTURES}/live_smoke.json --sidecar ${REPLAY_FIXTURES}/live_smoke.sidecar)
set_tests_properties(ReplayCliSidecarNeedsRocks PROPERTIES WILL_FAIL TRUE)

# Extracted pure-function layer (OpRlpDecode.h / Storage2StateHelpers.h): direct unit tests
# over the decode primitives and state-bridge helpers, no class-template instantiation.
add_executable(opstack-executor-detail-tests
    TestMain.cpp
    OpRlpDecodeTest.cpp
    Storage2StateHelpersTest.cpp
    OpMismatchedFieldTest.cpp
)
set_target_properties(opstack-executor-detail-tests PROPERTIES UNITY_BUILD OFF)
target_link_libraries(opstack-executor-detail-tests PRIVATE
    opstack-executor codec evmone::evmone intx::intx Boost::unit_test_framework bcos-utilities protocol-tars ${BLST_LIBRARY})
add_test(NAME OpstackExecutorDetailTests COMMAND opstack-executor-detail-tests)

# DA/operator-fee matrix runner (op-alignment line). Consumes da-matrix/da_matrix.json,
# computes L1 data fee + charged operator fee per case, writes out_fisco.json; --check mode
# compares value-level against the committed op-geth / op-revm golden snapshots
# (known_divergence rows skipped+counted; zero-compare run fails).
add_executable(opstack-da-matrix-runner da-matrix/run_fisco.cpp)
set_target_properties(opstack-da-matrix-runner PROPERTIES UNITY_BUILD OFF)
target_link_libraries(opstack-da-matrix-runner PRIVATE
    opstack-executor bcosevm::opstack jsoncpp_static evmone::evmone intx::intx bcos-utilities ${BLST_LIBRARY})
add_test(NAME DaMatrixFiscoBaseline COMMAND opstack-da-matrix-runner
    --grid ${CMAKE_CURRENT_SOURCE_DIR}/da-matrix/da_matrix.json
    --out ${CMAKE_CURRENT_BINARY_DIR}/out_fisco.json)
add_test(NAME DaMatrixFiscoCheckOpgeth COMMAND opstack-da-matrix-runner
    --grid ${CMAKE_CURRENT_SOURCE_DIR}/da-matrix/da_matrix.json
    --check opgeth
    --golden ${CMAKE_CURRENT_SOURCE_DIR}/da-matrix/golden/opgeth)
add_test(NAME DaMatrixFiscoCheckOpRevm COMMAND opstack-da-matrix-runner
    --grid ${CMAKE_CURRENT_SOURCE_DIR}/da-matrix/da_matrix.json
    --check oprevm
    --golden ${CMAKE_CURRENT_SOURCE_DIR}/da-matrix/golden/oprevm)
