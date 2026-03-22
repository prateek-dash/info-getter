cmake_minimum_required(VERSION 3.20)

cmake_policy(SET CMP0135 NEW)

project(info_getter LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(INFO_GETTER_BUILD_TESTS "Build unit and integration tests" ON)

include(FetchContent)

find_package(Threads REQUIRED)

FetchContent_Declare(
  cpp_httplib
  URL https://codeload.github.com/yhirose/cpp-httplib/tar.gz/refs/heads/master
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(
  nlohmann_json
  URL https://codeload.github.com/nlohmann/json/tar.gz/refs/heads/develop
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(
  sqlite_amalgamation
  URL https://www.sqlite.org/2025/sqlite-autoconf-3490100.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(cpp_httplib nlohmann_json sqlite_amalgamation)

file(GLOB_RECURSE HTTPLIB_HEADERS "${cpp_httplib_SOURCE_DIR}/httplib.h")
if(NOT HTTPLIB_HEADERS)
  message(FATAL_ERROR "Failed to locate httplib.h from fetched cpp-httplib source")
endif()
list(GET HTTPLIB_HEADERS 0 HTTPLIB_HEADER)
get_filename_component(HTTPLIB_INCLUDE_DIR "${HTTPLIB_HEADER}" DIRECTORY)

add_library(httplib_headers INTERFACE)
target_include_directories(httplib_headers INTERFACE "${HTTPLIB_INCLUDE_DIR}")

file(GLOB_RECURSE SQLITE_C_FILES "${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c")
file(GLOB_RECURSE SQLITE_H_FILES "${sqlite_amalgamation_SOURCE_DIR}/sqlite3.h")
if(NOT SQLITE_C_FILES OR NOT SQLITE_H_FILES)
  message(FATAL_ERROR "Failed to locate sqlite3.c/sqlite3.h from fetched sqlite source")
endif()
list(GET SQLITE_C_FILES 0 SQLITE_C_FILE)
list(GET SQLITE_H_FILES 0 SQLITE_H_FILE)
get_filename_component(SQLITE_INCLUDE_DIR "${SQLITE_H_FILE}" DIRECTORY)

add_library(sqlite3 STATIC "${SQLITE_C_FILE}")
target_include_directories(sqlite3 PUBLIC "${SQLITE_INCLUDE_DIR}")
target_compile_definitions(sqlite3 PRIVATE
  SQLITE_OMIT_LOAD_EXTENSION
  SQLITE_THREADSAFE=1
)

add_library(info_getter_lib STATIC
  src/time_utils.cpp
  src/json_codec.cpp
  src/service.cpp
  src/sqlite_repository.cpp
  src/http_api.cpp
)
target_include_directories(info_getter_lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(info_getter_lib PUBLIC
  sqlite3
  httplib_headers
  nlohmann_json::nlohmann_json
  Threads::Threads
)

add_executable(info_getter_server
  src/main.cpp
)
target_link_libraries(info_getter_server PRIVATE
  info_getter_lib
)

if(INFO_GETTER_BUILD_TESTS)
  enable_testing()

  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(googletest)

  add_executable(unit_tests
    tests/unit_tests.cpp
  )
  target_link_libraries(unit_tests PRIVATE
    info_getter_lib
    GTest::gtest_main
  )
  add_test(NAME unit_tests COMMAND unit_tests)

  add_executable(integration_tests
    tests/integration_tests.cpp
  )
  target_link_libraries(integration_tests PRIVATE
    info_getter_lib
    GTest::gtest_main
  )
  add_test(NAME integration_tests COMMAND integration_tests)
endif()
