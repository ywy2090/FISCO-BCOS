cmake_minimum_required(VERSION 3.28)

add_library(bcos-evm-reference-tests-core STATIC
    src/ManifestLoader.cpp
    src/GeneralStateTestLoader.cpp
    src/ForkProfileRegistry.cpp
    src/StateTestMatcher.cpp
    src/StateTestAssert.cpp
)

target_include_directories(bcos-evm-reference-tests-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
)

target_compile_definitions(bcos-evm-reference-tests-core PUBLIC
    EVM_REFERENCE_TESTS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(bcos-evm-reference-tests-core PUBLIC
    bcos-utilities
    bcos-evm-eth
    evmone::evmone
)

add_library(bcos-evm-reference-tests-eth STATIC
    src/ExecuteViaEthAdapter.cpp
)

target_link_libraries(bcos-evm-reference-tests-eth PUBLIC
    bcos-evm-reference-tests-core
    bcos-evm-eth
    bcos-crypto
    bcos-task
)

function(add_reference_test target source)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE bcos-evm-reference-tests-core)
    target_compile_definitions(${target} PRIVATE
        EVM_REFERENCE_TESTS_MANIFEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/manifests"
    )
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES
        LABELS "evm-reference-tests;evm-reference-tests-smoke"
    )
endfunction()

add_reference_test(ManifestLoaderTest runners/ManifestLoaderTest.cpp)
add_reference_test(GeneralStateTestLoaderTest runners/GeneralStateTestLoaderTest.cpp)
add_reference_test(ForkProfileRegistryTest runners/ForkProfileRegistryTest.cpp)
add_reference_test(StateTestMatcherTest runners/StateTestMatcherTest.cpp)
add_reference_test(StateTestAssertTest runners/StateTestAssertTest.cpp)

add_executable(ExecuteViaEthAdapterTest runners/ExecuteViaEthAdapterTest.cpp)
target_link_libraries(ExecuteViaEthAdapterTest PRIVATE bcos-evm-reference-tests-eth)
add_test(NAME ExecuteViaEthAdapterTest COMMAND ExecuteViaEthAdapterTest)
set_tests_properties(ExecuteViaEthAdapterTest PROPERTIES
    LABELS "evm-reference-tests;evm-reference-tests-smoke"
)

add_executable(EthGSTSmoke runners/EthGSTSmoke.cpp)
target_link_libraries(EthGSTSmoke PRIVATE bcos-evm-reference-tests-eth)
target_compile_definitions(EthGSTSmoke PRIVATE
    EVM_REFERENCE_TESTS_MANIFEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/manifests"
)
add_test(NAME EthGSTSmoke COMMAND EthGSTSmoke
    --manifest ${CMAKE_CURRENT_SOURCE_DIR}/manifests/eth-gst-prague-smoke.json
    --ethereum-tests-root ${CMAKE_CURRENT_SOURCE_DIR}/assets/ethereum-tests
    --expectations ${CMAKE_CURRENT_SOURCE_DIR}/manifests/expectations.json
)
set_tests_properties(EthGSTSmoke PROPERTIES
    LABELS "evm-reference-tests;evm-reference-tests-smoke;eth-kernel"
)

add_test(
    NAME ManifestLint
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/tools/manifest_lint.py
        --manifests ${CMAKE_CURRENT_SOURCE_DIR}/manifests
        --capability-rows ${CMAKE_CURRENT_SOURCE_DIR}/manifests/capability-rows.json
)
set_tests_properties(ManifestLint PROPERTIES
    LABELS "evm-reference-tests;evm-reference-tests-smoke"
)
