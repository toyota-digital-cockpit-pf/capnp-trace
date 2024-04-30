@0xd508eebdc2dc42b8;

using Cxx = import "/capnp/c++.capnp";

$Cxx.namespace("capnp_trace::test");

interface TestInterface {
  foo @0 (i :UInt32, j :Bool) -> (x :Text);
}
