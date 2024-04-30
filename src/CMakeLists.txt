set(CAPNPC_IMPORT_DIRS ${CAPNP_TRACE_SCHEMA_DIRS})
set(SCHEMA_DIR_INDEX 0)

# Convert all capnp schemas in CAPNP_TRACE_SCHEMA_DIRS
foreach(internal_schema_dir IN LISTS CAPNP_TRACE_SCHEMA_DIRS)
  file(GLOB internal_capnp_trace_schemas ${internal_schema_dir}/*/*.capnp)
  set(CAPNPC_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/schema_dir_${SCHEMA_DIR_INDEX}/")
  MATH(EXPR SCHEMA_DIR_INDEX "${SCHEMA_DIR_INDEX}+1")
  file(MAKE_DIRECTORY ${CAPNPC_OUTPUT_DIR})
  set(CAPNPC_SRC_PREFIX ${internal_schema_dir})

  message("Generating from ${internal_schema_dir} into ${CAPNPC_OUTPUT_DIR} ...")
  capnp_generate_cpp(
    internal_capnp_generated_sources
    internal_capnp_generated_headers 
    ${internal_capnp_trace_schemas}
  )

  list(APPEND CAPNP_TRACE_SCHEMAS
    ${internal_capnp_trace_schemas}
  )
  list(APPEND CAPNP_TRACE_GENERATED_HEADERS
    ${internal_capnp_generated_headers}
  )
  list(APPEND CAPNP_TRACE_GENERATED_SOURCES
    ${internal_capnp_generated_sources}
  )
  list(APPEND CAPNP_TRACE_INCLUDE_DIRECTORIES
    ${CAPNPC_OUTPUT_DIR}
  )
endforeach()

# Prepare include directives for immutable_scheam_registry.cc
set(CAPNP_TRACE_INCLUDE_DIRECTIVES "")
foreach(header ${CAPNP_TRACE_GENERATED_HEADERS})
  set(CAPNP_TRACE_INCLUDE_DIRECTIVES "${CAPNP_TRACE_INCLUDE_DIRECTIVES}\n#include \"${header}\"")
endforeach()

# Prepare load interface calls for immutable_scheam_registry.cc
execute_process(
  COMMAND awk -f ${capnp_trace_tool_dir}/generate_load_interface.awk
    ${CAPNP_TRACE_SCHEMAS}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE CAPNP_TRACE_LOAD_INTERFACES
)

configure_file(
  immutable_schema_registry.cc.in
  immutable_schema_registry.cc
  @ONLY
  NEWLINE_STYLE UNIX)


add_executable(capnp_trace
  capnp_trace.cc
  rpc_message_reassembler.cc
  rpc_message_recorder.cc
  rpc_tracer.cc
  ${CMAKE_CURRENT_BINARY_DIR}/immutable_schema_registry.cc
  ${CAPNP_TRACE_GENERATED_SOURCES}
)

target_include_directories(capnp_trace
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CAPNP_TRACE_INCLUDE_DIRECTORIES}
)
target_compile_options(capnp_trace PUBLIC -Wno-unused-result)
target_link_libraries(capnp_trace PUBLIC CapnProto::capnp-rpc)

install(TARGETS
  capnp_trace
)
