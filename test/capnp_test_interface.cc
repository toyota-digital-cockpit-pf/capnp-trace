#include <capnp/ez-rpc.h>
#include <kj/async-io.h>
#include <kj/timer.h>

#include <cassert>
#include <iostream>

#include "test.capnp.h"

namespace capnp_trace {
namespace test {

class TestInterfaceImpl final : public TestInterface::Server {
 protected:
  kj::Promise<void> foo(TestInterface::Server::FooContext context) override {
    context.getResults().setX("test result");
    return kj::READY_NOW;
  }
};

static void serverMain(const char* addr) {
  capnp::EzRpcServer server(kj::heap<TestInterfaceImpl>(), kj::str("unix:", addr));
  kj::NEVER_DONE.wait(server.getWaitScope());
}

static void clientMain(const char* addr) {
  capnp::EzRpcClient client(kj::str("unix:", addr));
  auto cap = client.getMain<TestInterface>();
  auto req = cap.fooRequest();
  req.setI(1234);
  req.setJ(true);
  KJ_DBG(req.send().wait(client.getWaitScope()).getX());
}

static void exitUsage(const std::string command) {
  std::cerr << "[USAGE] " << command << " server|client SERVER_ADDRESS" << std::endl;
  exit(EXIT_FAILURE);
}

}  // namespace test
}  // namespace capnp_trace

int main(const int argc, const char* const argv[]) {
  if (argc <= 2) {
    capnp_trace::test::exitUsage(argv[0]);
  }

  const std::string type(argv[1]);
  if (type == "server") {
    capnp_trace::test::serverMain(argv[2]);
  } else if (type == "client") {
    capnp_trace::test::clientMain(argv[2]);
  } else {
    capnp_trace::test::exitUsage(argv[0]);
  }

  return 0;
}
