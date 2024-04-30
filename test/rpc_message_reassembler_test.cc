#include "rpc_message_reassembler.h"

#include <capnp/dynamic.h>
#include <capnp/rpc.capnp.h>
#include <gtest/gtest.h>
#include <kj/filesystem.h>

#include "immutable_schema_registry.h"

class RpcMessageReassemblerTest : public ::testing::Test {
 protected:
  void SetUp() { capnp_trace::ImmutableSchemaRegistry::Init(); }
};

TEST_F(RpcMessageReassemblerTest, CreateInstance) {
  // Arrange

  // Act
  capnp_trace::RpcMessageReassembler reassembler([](...) {}, {});

  // Assert
}

static void AssertTestInterfaceFooCall(capnp::rpc::Message::Reader&& message) {
  ASSERT_TRUE(message.isCall());

  auto call = message.getCall();
  ASSERT_EQ(1, call.getQuestionId());
  ASSERT_TRUE(call.hasParams());
  ASSERT_TRUE(call.getParams().hasContent());

  auto interface = capnp_trace::ImmutableSchemaRegistry::GetInterface(call.getInterfaceId());
  EXPECT_STREQ("test.capnp:TestInterface", interface.getProto().getDisplayName().cStr());

  auto method = interface.getMethods()[call.getMethodId()];
  EXPECT_STREQ("foo", method.getProto().getName().cStr());

  auto content = call.getParams().getContent();
  auto param   = content.getAs<capnp::DynamicStruct>(method.getParamType());
  EXPECT_STREQ("(i = 1234, j = true)", kj::str(param).cStr());
}

TEST_F(RpcMessageReassemblerTest, ReassembleTestInterfaceFooAtOnce) {
  // Arrange
  int call_count = 0;
  capnp_trace::RpcMessageReassembler reassembler(
      [&call_count](capnp_trace::StreamInfo stream_info, capnp::rpc::Message::Reader&& message,
                    [[maybe_unused]] kj::ArrayPtr<kj::byte> raw_message) {
        ASSERT_EQ(capnp_trace::StreamInfo{}, stream_info);
        if (!message.isCall()) {
          // Test only call message
          return;
        }
        call_count++;
        AssertTestInterfaceFooCall(kj::mv(message));
      },
      {});
  auto file = kj::newDiskFilesystem()->getCurrent().openFile(
      kj::Path({"testdata", "capnp_trace.TestInterface.in.dump"}));
  auto bytes = file->readAllBytes();

  // Act
  reassembler.Reassemble(bytes.asChars().begin(), bytes.size());

  // Assert
  ASSERT_EQ(3, call_count);
}

TEST_F(RpcMessageReassemblerTest, ReassembleTestInterfaceFooByteByByte) {
  // Arrange
  int call_count = 0;
  capnp_trace::RpcMessageReassembler reassembler(
      [&call_count](capnp_trace::StreamInfo stream_info, capnp::rpc::Message::Reader&& message,
                    [[maybe_unused]] kj::ArrayPtr<kj::byte> raw_message) {
        ASSERT_EQ(capnp_trace::StreamInfo{}, stream_info);
        if (!message.isCall()) {
          // Test only call message
          return;
        }
        call_count++;
        AssertTestInterfaceFooCall(kj::mv(message));
      },
      {});
  auto file = kj::newDiskFilesystem()->getCurrent().openFile(
      kj::Path({"testdata", "capnp_trace.TestInterface.in.dump"}));
  auto bytes = file->readAllBytes();

  // Act
  // provide data byte by byte
  for (auto i = 0U; i < bytes.size(); i++) {
    reassembler.Reassemble(bytes.asChars().begin() + i, 1);
  }

  // Assert
  ASSERT_EQ(3, call_count);
}

TEST_F(RpcMessageReassemblerTest, ReassemblerPassesSameStreamInfo) {
  // Arrange
  bool called = false;
  capnp_trace::StreamInfo stream_info_exp(0x1234, 0x5678, capnp_trace::StreamInfo::Direction::kIn,
                                          0x90ab, "test address");
  capnp_trace::RpcMessageReassembler reassembler(
      [&called, stream_info_exp](capnp_trace::StreamInfo stream_info,
                                 [[maybe_unused]] capnp::rpc::Message::Reader&& message,
                                 [[maybe_unused]] kj::ArrayPtr<kj::byte> raw_message) {
        ASSERT_EQ(stream_info_exp, stream_info);
        called = true;
      },
      stream_info_exp);
  auto file = kj::newDiskFilesystem()->getCurrent().openFile(
      kj::Path({"testdata", "capnp_trace.TestInterface.in.dump"}));
  auto bytes = file->readAllBytes();

  // Act
  reassembler.Reassemble(bytes.asChars().begin(), bytes.size());

  // Assert
  ASSERT_TRUE(called);
}
