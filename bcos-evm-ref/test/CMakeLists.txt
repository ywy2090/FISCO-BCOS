find_package(GTest CONFIG REQUIRED)

add_executable(bcos-evm-ref-eth-tests
    eth/EthTransitionTest.cpp
)
target_link_libraries(bcos-evm-ref-eth-tests PRIVATE
    bcosevmref::eth
    evmone::testutils
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME BcosEvmRefEthTests COMMAND bcos-evm-ref-eth-tests)
