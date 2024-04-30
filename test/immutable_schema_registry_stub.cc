#include <capnp/schema-loader.h>

#include "test.capnp.h"
#include "immutable_schema_registry.h"

namespace capnp_trace {
static capnp::SchemaLoader loader;

void ImmutableSchemaRegistry::Init() {
  loader.loadCompiledTypeAndDependencies<capnp_trace::test::TestInterface>();
}

capnp::InterfaceSchema ImmutableSchemaRegistry::GetInterface(uint64_t id) {
  return loader.get(id).asInterface();
}
}  // namespace capnp_trace
