#pragma once

#include <capnp/schema.h>

namespace capnp_trace {
class ImmutableSchemaRegistry final {
 public:
  static void Init();
  static capnp::InterfaceSchema GetInterface(uint64_t id);

 private:
  ImmutableSchemaRegistry()                                          = delete;
  ImmutableSchemaRegistry(const ImmutableSchemaRegistry&)            = delete;
  ImmutableSchemaRegistry& operator=(const ImmutableSchemaRegistry&) = delete;
  ImmutableSchemaRegistry(ImmutableSchemaRegistry&&)                 = delete;
  ImmutableSchemaRegistry& operator=(ImmutableSchemaRegistry&&)      = delete;
  ~ImmutableSchemaRegistry()                                         = delete;
};
}  // namespace capnp_trace
