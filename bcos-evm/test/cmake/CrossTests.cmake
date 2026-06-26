# Cross-layer tests: span eth kernel + bcos/opstack shells.

add_executable(RevisionConfigProfileTest cross/RevisionConfigProfileTest.cpp)
target_include_directories(RevisionConfigProfileTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(RevisionConfigProfileTest PRIVATE
    bcos-evm-eth bcos-evm-bcos protocol-tars bcos-framework)
add_test(NAME RevisionConfigProfile COMMAND RevisionConfigProfileTest)

add_executable(CallTargetCharacterizationTest cross/CallTargetCharacterizationTest.cpp)
target_include_directories(CallTargetCharacterizationTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(CallTargetCharacterizationTest PRIVATE
    bcos-evm-eth bcos-evm-bcos bcos-evm-op evmone::evmone)
add_test(NAME CallTargetCharacterization COMMAND CallTargetCharacterizationTest)

add_executable(FeeSettlementCharacterizationTest cross/FeeSettlementCharacterizationTest.cpp)
target_include_directories(FeeSettlementCharacterizationTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(FeeSettlementCharacterizationTest PRIVATE bcos-evm-eth bcos-utilities)
add_test(NAME FeeSettlementCharacterization COMMAND FeeSettlementCharacterizationTest)

add_executable(PrecompileRouterEquivalenceTest cross/PrecompileRouterEquivalenceTest.cpp)
target_include_directories(PrecompileRouterEquivalenceTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR})
target_link_libraries(PrecompileRouterEquivalenceTest PRIVATE
    bcos-evm-eth bcos-evm-op evmone::evmone)
add_test(NAME PrecompileRouterEquivalence COMMAND PrecompileRouterEquivalenceTest)
