# Shared helpers for bcos-evm unit tests.

function(add_te_input_builder_test NAME SOURCE)
    add_executable(${NAME} ${SOURCE})
    target_include_directories(${NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/transaction-executor)
    target_link_libraries(${NAME} PRIVATE
        bcos-evm bcos-evm-op executor protocol-tars bcos-crypto codec)
    add_test(NAME ${NAME} COMMAND ${NAME})
endfunction()

