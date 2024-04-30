set(TEST_CAPNP_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(SUT_SOURCES
  ${capnp_trace_src_dir}/rpc_message_reassembler.cc
  ${capnp_trace_src_dir}/rpc_message_recorder.cc
)
set(TEST_SOURCES
  rpc_message_reassembler_test.cc
  rpc_message_recorder_test.cc
  stream_info_test.cc
  injection_test.cc
  immutable_schema_registry_stub.cc
)

set(CAPNPC_SRC_PREFIX ${TEST_CAPNP_DIR})
set(CAPNPC_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/unittest/")
file(MAKE_DIRECTORY ${CAPNPC_OUTPUT_DIR})
capnp_generate_cpp(
  TEST_CAPNP_SOURCES
  TEST_CAPNP_HEADERS
  ${TEST_CAPNP_DIR}/test.capnp)

add_executable(capnp_trace_unittest
  ${SUT_SOURCES}
  ${TEST_SOURCES}
  ${TEST_CAPNP_SOURCES}
)
target_include_directories(capnp_trace_unittest
  PRIVATE
  ${capnp_trace_src_dir}
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CAPNPC_OUTPUT_DIR}
)
target_link_libraries(capnp_trace_unittest
  PRIVATE
  gtest_main
  gtest
  CapnProto::capnp-rpc
)

add_test(
  NAME capnp_trace_unittest
  COMMAND capnp_trace_unittest
)

add_executable(capnp_test_interface
  capnp_test_interface.cc
  ${TEST_CAPNP_SOURCES}
)
target_include_directories(capnp_test_interface
  PRIVATE
  ${CAPNPC_OUTPUT_DIR}
)
target_link_libraries(capnp_test_interface
  PRIVATE
  CapnProto::capnp-rpc
)

add_custom_command(
  TARGET capnp_trace_unittest POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/testdata
    ${CMAKE_CURRENT_BINARY_DIR}/testdata
)
