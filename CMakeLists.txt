cmake_minimum_required(VERSION 3.27 FATAL_ERROR)

set(STARLINGMONKEY_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/StarlingMonkey"
    CACHE PATH "Path to a StarlingMonkey source checkout")

if(NOT EXISTS "${STARLINGMONKEY_SOURCE}/CMakeLists.txt")
  message(FATAL_ERROR
          "StarlingMonkey is missing. Run: git submodule update --init --recursive")
endif()

cmake_path(ABSOLUTE_PATH STARLINGMONKEY_SOURCE NORMALIZE)
set(CMAKE_MODULE_PATH "${STARLINGMONKEY_SOURCE}/cmake")

if(NOT DEFINED ENV{CPM_SOURCE_CACHE})
  set(ENV{CPM_SOURCE_CACHE} "${CMAKE_BINARY_DIR}/cpm-cache")
endif()

include(CPM)
include(toolchain)
project(sturnkey VERSION 0.1.0 LANGUAGES C CXX)

option(STURNKEY_BUILD_TESTING "Build Sturnkey tests" ON)

set(STARLINGMONKEY_BINARY_DIR "${CMAKE_BINARY_DIR}/starlingmonkey")
set(BUILD_TESTING OFF CACHE BOOL "Build upstream tests" FORCE)
add_subdirectory("${STARLINGMONKEY_SOURCE}" "${STARLINGMONKEY_BINARY_DIR}")
include("${STARLINGMONKEY_SOURCE}/cmake/add_builtin.cmake")

add_builtin(
  sturnkey::runtime
  SRC runtime/src/runtime.cpp
  INCLUDE_DIRS "${STARLINGMONKEY_SOURCE}/host-apis/wasi-0.2.10/bindings"
  DEPENDENCIES host_api)
target_compile_definitions(
  ${LIB_NAME} PRIVATE STURNKEY_VERSION="${PROJECT_VERSION}")
add_builtin(sturnkey::filesystem SRC runtime/src/filesystem.cpp)
add_builtin(
  sturnkey::network
  SRC runtime/src/network.cpp
  INCLUDE_DIRS "${STARLINGMONKEY_SOURCE}/host-apis/wasi-0.2.10/bindings"
  DEPENDENCIES host_api)

set(STURNKEY_WASM "${CMAKE_BINARY_DIR}/sturnkey.wasm")
add_custom_command(
  OUTPUT "${STURNKEY_WASM}"
  COMMAND
    "${CMAKE_COMMAND}" -E copy_if_different
    "${STARLINGMONKEY_BINARY_DIR}/starling.wasm" "${STURNKEY_WASM}"
  DEPENDS starling "${STARLINGMONKEY_BINARY_DIR}/starling.wasm"
  COMMENT "Creating sturnkey.wasm"
  VERBATIM)
add_custom_target(sturnkey ALL DEPENDS "${STURNKEY_WASM}")

find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)
if(CLANG_FORMAT_EXECUTABLE)
  add_custom_target(
    format-check
    COMMAND "${CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror
            "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/filesystem.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/network.cpp"
    COMMENT "Checking Sturnkey C++ formatting"
    VERBATIM)
endif()

if(STURNKEY_BUILD_TESTING)
  enable_testing()
  find_program(WASMTIME_EXECUTABLE NAMES wasmtime)
  if(WASMTIME_EXECUTABLE)
    add_test(
      NAME dynamic-js
      COMMAND
        "${WASMTIME_EXECUTABLE}" -S http --dir
        "${CMAKE_CURRENT_SOURCE_DIR}/examples"
        "${STURNKEY_WASM}"
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/hello.js")
    set_tests_properties(
      dynamic-js PROPERTIES PASS_REGULAR_EXPRESSION
                            "Hello from Sturnkey 0.1.0")

    add_test(
      NAME async-sleep
      COMMAND
        "${WASMTIME_EXECUTABLE}" -S http --dir
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        "${STURNKEY_WASM}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/async-sleep.js")
    set_tests_properties(
      async-sleep PROPERTIES PASS_REGULAR_EXPRESSION
                             "async sleep completed")

    add_test(
      NAME async-invalid
      COMMAND
        "${WASMTIME_EXECUTABLE}" -S http --dir
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        "${STURNKEY_WASM}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/async-invalid.js")
    set_tests_properties(
      async-invalid PROPERTIES PASS_REGULAR_EXPRESSION
                               "invalid sleep arguments rejected")

    add_test(
      NAME async-rejection
      COMMAND
        bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/expect-failure.sh"
        "expected top-level rejection" "${WASMTIME_EXECUTABLE}" -S http --dir
        "${CMAKE_CURRENT_SOURCE_DIR}/tests"
        "${STURNKEY_WASM}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/async-rejection.js")

    set(FILESYSTEM_TEST_DIR "${CMAKE_CURRENT_BINARY_DIR}/filesystem-test")
    file(MAKE_DIRECTORY "${FILESYSTEM_TEST_DIR}")
    add_test(
      NAME filesystem
      COMMAND
        "${WASMTIME_EXECUTABLE}" -S http --dir
        "${CMAKE_CURRENT_SOURCE_DIR}/tests" --dir
        "${FILESYSTEM_TEST_DIR}::/sandbox" "${STURNKEY_WASM}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/filesystem.js")
    set_tests_properties(
      filesystem PROPERTIES PASS_REGULAR_EXPRESSION
                            "filesystem capability test completed")

    find_program(NODE_EXECUTABLE NAMES node)
    if(NODE_EXECUTABLE)
      add_test(
        NAME tcp-client
        COMMAND
          "${NODE_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/tcp-client.mjs"
          "${WASMTIME_EXECUTABLE}" "${STURNKEY_WASM}"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/tcp-client.js")
      set_tests_properties(
        tcp-client PROPERTIES PASS_REGULAR_EXPRESSION
                              "tcp client test completed" TIMEOUT 20)

      add_test(
        NAME tcp-listener
        COMMAND
          "${NODE_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/tcp-listener.mjs"
          "${WASMTIME_EXECUTABLE}" "${STURNKEY_WASM}"
          "${CMAKE_CURRENT_SOURCE_DIR}/tests/tcp-listener.js")
      set_tests_properties(
        tcp-listener PROPERTIES PASS_REGULAR_EXPRESSION
                                "tcp listener test completed" TIMEOUT 30)
    endif()
  else()
    message(WARNING "Wasmtime not found; runtime tests are disabled")
  endif()
endif()
