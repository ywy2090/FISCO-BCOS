vcpkg_from_git(
    OUT_SOURCE_PATH SOURCE_PATH
    URL "https://github.com/ywy2090/evmone"
    REF 3585c2cb0c2a4752ef6e31e17803d4bfc2d44a69
    FETCH_REF refs/heads/v0.21.0
)

# 1b. Remove find_dependency(evmc) from the auto-generated evmoneConfig.cmake.
#     evmc is bundled inside evmone and is not a separate vcpkg package.
vcpkg_replace_string("${SOURCE_PATH}/lib/evmone/CMakeLists.txt"
    "@PACKAGE_INIT@\\ninclude(CMakeFindDependencyMacro)\\nfind_dependency(evmc CONFIG)\\ncheck_required_components(\"@PROJECT_NAME@\")\\n"
    "@PACKAGE_INIT@\\ncheck_required_components(\"@PROJECT_NAME@\")\\n"
)

# 2. Remove EXPORT from install(TARGETS) to avoid export set errors
#    (evmone depends on evmc and evmone_precompiles which are not in the export set)
vcpkg_replace_string("${SOURCE_PATH}/CMakeLists.txt"
    "install(TARGETS \${install_targets} EXPORT evmoneTargets"
    "install(TARGETS \${install_targets}"
)

# 2a. Add an EVMONE_STATE option that builds test/state (evmone::state) and
#     test/utils (evmone::testutils) WITHOUT going through EVMONE_TESTING.
#     EVMONE_TESTING pulls test/CMakeLists.txt whose hunter_add_package +
#     find_package(GTest/benchmark/CLI11 REQUIRED) fail under HUNTER_ENABLED=OFF.
#     Both subdirectories are self-contained: test/state needs only targets that
#     exist after add_subdirectory(lib) (evmone, evmone::precompiles, evmc);
#     test/utils additionally needs nlohmann_json (provided by vcpkg).
vcpkg_replace_string("${SOURCE_PATH}/CMakeLists.txt"
    "add_subdirectory(lib)"
    "add_subdirectory(lib)
if(EVMONE_STATE)
    find_package(nlohmann_json CONFIG REQUIRED)
    add_subdirectory(test/state)
    add_subdirectory(test/utils)
endif()"
)

vcpkg_cmake_configure(
    SOURCE_PATH ${SOURCE_PATH}
    OPTIONS
        -DBUILD_SHARED_LIBS=OFF
        -DEVMONE_TESTING=OFF
        -DEVMONE_FUZZING=OFF
        -DEVMONE_TOOLS=OFF
        -DEVMONE_STATE=ON
        -DHUNTER_ENABLED=OFF
)
vcpkg_cmake_install()

# Remove evmone's auto-generated cmake config (it lacks the imported target definition
# and previously referenced the non-existent evmc package). Our manual config below
# replaces it for both release and debug.
file(REMOVE_RECURSE
    "${CURRENT_PACKAGES_DIR}/lib/cmake/evmone"
    "${CURRENT_PACKAGES_DIR}/debug/lib/cmake/evmone"
)

# 4. Install evmc headers (evmone's build doesn't install them)
file(INSTALL "${SOURCE_PATH}/evmc/include/evmc/"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include/evmc")

# 4a. Install evmone precompiles static library because evmone.a depends on it transitively.
# Use platform-aware library name: .lib on Windows, .a on Unix.
if(VCPKG_TARGET_IS_WINDOWS)
    set(_precomp_lib_name "evmone_precompiles.lib")
else()
    set(_precomp_lib_name "libevmone_precompiles.a")
endif()

file(GLOB_RECURSE _precomp_dbg_libs
    "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-dbg/${_precomp_lib_name}")
file(GLOB_RECURSE _precomp_rel_libs
    "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/${_precomp_lib_name}")

if(NOT DEFINED VCPKG_BUILD_TYPE OR VCPKG_BUILD_TYPE STREQUAL "debug")
    if(NOT _precomp_dbg_libs)
        message(FATAL_ERROR "debug ${_precomp_lib_name} not found in evmone build tree")
    endif()
    list(GET _precomp_dbg_libs 0 _precomp_dbg_lib)
    file(INSTALL "${_precomp_dbg_lib}" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")
endif()

if(NOT DEFINED VCPKG_BUILD_TYPE OR VCPKG_BUILD_TYPE STREQUAL "release")
    if(NOT _precomp_rel_libs)
        message(FATAL_ERROR "release ${_precomp_lib_name} not found in evmone build tree")
    endif()
    list(GET _precomp_rel_libs 0 _precomp_rel_lib)
    file(INSTALL "${_precomp_rel_lib}" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
endif()

# 4a-1. Install evmone-state and evmone.testutils static libraries (bcos-evm-ref).
#       Neither has an install() rule upstream; grab them from the build tree
#       the same way as evmone_precompiles above.
foreach(_state_lib_base IN ITEMS "evmone-state" "evmone.testutils")
    if(VCPKG_TARGET_IS_WINDOWS)
        set(_state_lib_name "${_state_lib_base}.lib")
    else()
        set(_state_lib_name "lib${_state_lib_base}.a")
    endif()

    file(GLOB_RECURSE _state_dbg_libs
        "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-dbg/${_state_lib_name}")
    file(GLOB_RECURSE _state_rel_libs
        "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/${_state_lib_name}")

    if(NOT DEFINED VCPKG_BUILD_TYPE OR VCPKG_BUILD_TYPE STREQUAL "debug")
        if(NOT _state_dbg_libs)
            message(FATAL_ERROR "debug ${_state_lib_name} not found in evmone build tree")
        endif()
        list(GET _state_dbg_libs 0 _state_dbg_lib)
        file(INSTALL "${_state_dbg_lib}" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")
    endif()

    if(NOT DEFINED VCPKG_BUILD_TYPE OR VCPKG_BUILD_TYPE STREQUAL "release")
        if(NOT _state_rel_libs)
            message(FATAL_ERROR "release ${_state_lib_name} not found in evmone build tree")
        endif()
        list(GET _state_rel_libs 0 _state_rel_lib)
        file(INSTALL "${_state_rel_lib}" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
    endif()
endforeach()

# 4a-2. Install evmone::state / evmone::testutils public headers preserving the
#       test/ tree layout: headers cross-reference via <test/state/...> and
#       <test/utils/...>, and evmone-state's PUBLIC include dir is the project
#       root, so include/test/{state,utils} keeps every include path working.
file(GLOB _evmone_state_headers "${SOURCE_PATH}/test/state/*.hpp")
file(INSTALL ${_evmone_state_headers}
    DESTINATION "${CURRENT_PACKAGES_DIR}/include/test/state")
file(GLOB _evmone_testutils_headers "${SOURCE_PATH}/test/utils/*.hpp")
file(INSTALL ${_evmone_testutils_headers}
    DESTINATION "${CURRENT_PACKAGES_DIR}/include/test/utils")
file(INSTALL "${SOURCE_PATH}/test/utils/stdx"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include/test/utils")

# 4b. Install evmone internal headers needed by FISCO-BCOS
#     (baseline.hpp, vm.hpp, and transitive deps are in lib/evmone/, not installed by default)
file(INSTALL
    "${SOURCE_PATH}/lib/evmone/baseline.hpp"
    "${SOURCE_PATH}/lib/evmone/vm.hpp"
    "${SOURCE_PATH}/lib/evmone/execution_state.hpp"
    "${SOURCE_PATH}/lib/evmone/tracing.hpp"
    "${SOURCE_PATH}/lib/evmone/delegation.hpp"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include/evmone")

# 4c. Install phase-1 precompile crypto headers used directly by FISCO-BCOS.
file(INSTALL
    "${SOURCE_PATH}/lib/evmone_precompiles/sha256.hpp"
    "${SOURCE_PATH}/lib/evmone_precompiles/ripemd160.hpp"
    "${SOURCE_PATH}/lib/evmone_precompiles/kzg.hpp"
    "${SOURCE_PATH}/lib/evmone_precompiles/blake2b.hpp"
    "${SOURCE_PATH}/lib/evmone_precompiles/bn254.hpp"
    "${SOURCE_PATH}/lib/evmone_precompiles/ecc.hpp"
    "${SOURCE_PATH}/lib/evmone_precompiles/hash_types.h"
    "${SOURCE_PATH}/lib/evmone_precompiles/keccak.h"
    "${SOURCE_PATH}/lib/evmone_precompiles/keccak.hpp"
    "${SOURCE_PATH}/lib/evmone_precompiles/mulmod.hpp"
    "${SOURCE_PATH}/lib/evmone_precompiles/secp256k1.hpp"
    "${SOURCE_PATH}/lib/evmone_precompiles/secp256r1.hpp"
    "${SOURCE_PATH}/lib/evmone_precompiles/modexp.hpp"
    "${SOURCE_PATH}/lib/evmone_precompiles/bls.hpp"
    DESTINATION "${CURRENT_PACKAGES_DIR}/include/evmone_precompiles")

file(INSTALL "${SOURCE_PATH}/lib/evmone_precompiles/pairing"
     DESTINATION "${CURRENT_PACKAGES_DIR}/include/evmone_precompiles")

# 5. Write a manual cmake config file (avoids install(EXPORT) issues)
#    Creates evmone::evmone imported target with proper dependencies.
#    Uses platform-aware library suffixes (.lib on Windows, .a on Unix).
set(EVMONE_CONFIG "${CURRENT_PACKAGES_DIR}/share/evmone/evmoneConfig.cmake")
file(WRITE "${EVMONE_CONFIG}" [=[
include(CMakeFindDependencyMacro)
find_dependency(intx)
find_dependency(blst)

if(NOT TARGET evmone::evmone)
    if(WIN32)
        set(_evmone_lib_prefix "")
        set(_evmone_lib_suffix ".lib")
    else()
        set(_evmone_lib_prefix "lib")
        set(_evmone_lib_suffix ".a")
    endif()

    add_library(evmone::evmone STATIC IMPORTED)
    set_target_properties(evmone::evmone PROPERTIES
        IMPORTED_LOCATION_RELEASE
            "${CMAKE_CURRENT_LIST_DIR}/../../lib/${_evmone_lib_prefix}evmone${_evmone_lib_suffix}"
        IMPORTED_LOCATION_DEBUG
            "${CMAKE_CURRENT_LIST_DIR}/../../debug/lib/${_evmone_lib_prefix}evmone${_evmone_lib_suffix}"
        IMPORTED_LOCATION
            "${CMAKE_CURRENT_LIST_DIR}/../../lib/${_evmone_lib_prefix}evmone${_evmone_lib_suffix}"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../include"
        INTERFACE_LINK_LIBRARIES
            "intx::intx;evmone::precompiles"
    )

    # Separate target for evmone_precompiles so blst can be a proper dependency
    add_library(evmone::precompiles STATIC IMPORTED)
    set_target_properties(evmone::precompiles PROPERTIES
        IMPORTED_LOCATION
            "${CMAKE_CURRENT_LIST_DIR}/../../lib/${_evmone_lib_prefix}evmone_precompiles${_evmone_lib_suffix}"
        INTERFACE_LINK_LIBRARIES "blst"
    )
endif()

# evmone::state (test/state state transition) + evmone::testutils (EEST
# loaders, TestState, mpt_hash) for bcos-evm-ref. Upstream links evmone
# PRIVATE into evmone-state, which does not propagate through an imported
# target, so evmone::evmone is added to the interface explicitly.
if(NOT TARGET evmone::state)
    find_dependency(nlohmann_json)

    if(WIN32)
        set(_evmone_lib_prefix "")
        set(_evmone_lib_suffix ".lib")
    else()
        set(_evmone_lib_prefix "lib")
        set(_evmone_lib_suffix ".a")
    endif()

    add_library(evmone::state STATIC IMPORTED)
    set_target_properties(evmone::state PROPERTIES
        IMPORTED_LOCATION_RELEASE
            "${CMAKE_CURRENT_LIST_DIR}/../../lib/${_evmone_lib_prefix}evmone-state${_evmone_lib_suffix}"
        IMPORTED_LOCATION_DEBUG
            "${CMAKE_CURRENT_LIST_DIR}/../../debug/lib/${_evmone_lib_prefix}evmone-state${_evmone_lib_suffix}"
        IMPORTED_LOCATION
            "${CMAKE_CURRENT_LIST_DIR}/../../lib/${_evmone_lib_prefix}evmone-state${_evmone_lib_suffix}"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../include"
        INTERFACE_LINK_LIBRARIES
            "evmone::precompiles;evmone::evmone;intx::intx"
    )

    add_library(evmone::testutils STATIC IMPORTED)
    set_target_properties(evmone::testutils PROPERTIES
        IMPORTED_LOCATION_RELEASE
            "${CMAKE_CURRENT_LIST_DIR}/../../lib/${_evmone_lib_prefix}evmone.testutils${_evmone_lib_suffix}"
        IMPORTED_LOCATION_DEBUG
            "${CMAKE_CURRENT_LIST_DIR}/../../debug/lib/${_evmone_lib_prefix}evmone.testutils${_evmone_lib_suffix}"
        IMPORTED_LOCATION
            "${CMAKE_CURRENT_LIST_DIR}/../../lib/${_evmone_lib_prefix}evmone.testutils${_evmone_lib_suffix}"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../include"
        INTERFACE_LINK_LIBRARIES
            "evmone::state;nlohmann_json::nlohmann_json"
    )
endif()
]=])

# Write version file
set(EVMONE_VERSION_CONFIG "${CURRENT_PACKAGES_DIR}/share/evmone/evmoneConfigVersion.cmake")
file(WRITE "${EVMONE_VERSION_CONFIG}" [=[
set(PACKAGE_VERSION "0.21.0")
if(PACKAGE_VERSION VERSION_LESS PACKAGE_FIND_VERSION)
    set(PACKAGE_VERSION_COMPATIBLE FALSE)
else()
    set(PACKAGE_VERSION_COMPATIBLE TRUE)
    if(PACKAGE_FIND_VERSION STREQUAL PACKAGE_VERSION)
        set(PACKAGE_VERSION_EXACT TRUE)
    endif()
endif()
]=])

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
