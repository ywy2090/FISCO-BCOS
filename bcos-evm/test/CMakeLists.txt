cmake_minimum_required(VERSION 3.28)

include(CTest)
enable_testing()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/TestHelpers.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/StateTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/EthTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/BcosTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/OpStackTests.cmake)
