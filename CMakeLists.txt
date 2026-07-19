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

add_builtin(sturnkey::runtime SRC runtime/src/runtime.cpp)
target_compile_definitions(
  ${LIB_NAME} PRIVATE STURNKEY_VERSION="${PROJECT_VERSION}")

set(STURNKEY_WASM "${CMAKE_BINARY_DIR}/sturnkey.wasm")
add_custom_command(
  OUTPUT "${STURNKEY_WASM}"
  COMMAND
    "${CMAKE_COMMAND}" -E copy_if_different
    "${STARLINGMONKEY_BINARY_DIR}/starling.wasm" "${STURNKEY_WASM}"
  DEPENDS starling
  COMMENT "Creating sturnkey.wasm"
  VERBATIM)
add_custom_target(sturnkey ALL DEPENDS "${STURNKEY_WASM}")

find_program(CLANG_FORMAT_EXECUTABLE NAMES clang-format)
if(CLANG_FORMAT_EXECUTABLE)
  add_custom_target(
    format-check
    COMMAND "${CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror
            "${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/runtime.cpp"
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
  else()
    message(WARNING "Wasmtime not found; runtime tests are disabled")
  endif()
endif()
