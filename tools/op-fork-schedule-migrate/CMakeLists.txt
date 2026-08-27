find_package(Boost REQUIRED program_options)

add_executable(op-fork-schedule-migrate main.cpp)
target_link_libraries(op-fork-schedule-migrate
    storage
    ledger
    tool
    protocol-tars
    bcos-crypto
    bcos-utilities
    init
    Boost::program_options)
target_include_directories(op-fork-schedule-migrate
    PUBLIC ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/libinitializer)
target_compile_options(op-fork-schedule-migrate PRIVATE -Wno-unused-variable)
