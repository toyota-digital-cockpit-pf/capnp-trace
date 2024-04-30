cmake_minimum_required(VERSION 3.16)
project(capnp_trace CXX)
set(CMAKE_CXX_STANDARD 14)

option(BUILD_TESTS "Build tests" OFF)
set(CAPNP_TRACE_SCHEMA_DIRS "" CACHE PATH "Where to look for additional Cap'n Proto schema files (can be ;-separated list of paths)")
if("x${CAPNP_TRACE_SCHEMA_DIRS}" STREQUAL "x")
  message(FATAL_ERROR "CAPNP_TRACE_SCHEMA_DIRS must be specified")
endif()
find_package(CapnProto REQUIRED)

set(capnp_trace_src_dir ${CMAKE_CURRENT_SOURCE_DIR}/src/)
set(capnp_trace_tool_dir ${CMAKE_CURRENT_SOURCE_DIR}/tool/)

add_subdirectory(src)

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(test)
endif(BUILD_TESTS)
