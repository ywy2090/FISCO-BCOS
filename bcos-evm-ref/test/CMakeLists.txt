find_package(GTest CONFIG REQUIRED)

add_executable(bcos-evm-ref-eth-tests
    eth/EthTransitionTest.cpp
    eth/StateDiffWritebackTest.cpp
    eth/EestStateTest.cpp
)
target_link_libraries(bcos-evm-ref-eth-tests PRIVATE
    bcosevmref::eth
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME BcosEvmRefEthTests COMMAND bcos-evm-ref-eth-tests)
