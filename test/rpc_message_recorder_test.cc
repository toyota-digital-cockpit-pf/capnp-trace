#include "rpc_message_recorder.h"

#include <capnp/dynamic.h>
#include <capnp/rpc.capnp.h>
#include <gtest/gtest.h>
#include <kj/filesystem.h>

#include "immutable_schema_registry.h"

class RpcMessageRecorderTest : public ::testing::Test {
 protected:
  void SetUp() { kj::newDiskFilesystem()->getCurrent().tryRemove(kOutputPath); }

  const kj::Path kOutputPath{"rpc_message_recorder_test.output"};
  const kj::Path kTestDataPath{"testdata",
                               "capnp_trace.TestInterface.recorded"};
};

TEST_F(RpcMessageRecorderTest, CreateRecorderInstance) {
  // Arrange
  auto output_file =
      kj::newDiskFilesystem()->getCurrent().appendFile(kOutputPath, kj::WriteMode::CREATE);

  // Act
  capnp_trace::RpcMessageRecorder recorder{kj::mv(output_file)};

  // Assert
}

TEST_F(RpcMessageRecorderTest, OutputFileIsCreatedAfterRecord) {
  // Arrange
  auto fs          = kj::newDiskFilesystem();
  auto output_file = fs->getCurrent().appendFile(kOutputPath, kj::WriteMode::CREATE);
  capnp_trace::RpcMessageRecorder recorder{kj::mv(output_file)};

  // Act
  recorder.Record({}, {}, {});

  // Assert
  output_file = nullptr;  // dispose
  ASSERT_TRUE(fs->getCurrent().exists(kOutputPath));
  ASSERT_GT(fs->getCurrent().lstat(kOutputPath).size, 0);
}

TEST_F(RpcMessageRecorderTest, CreateParserInstance) {
  // Arrange
  auto parse_file = kj::newDiskFilesystem()->getCurrent().openFile(kTestDataPath);

  // Act
  capnp_trace::RpcMessageRecorder::Parser parser{kj::mv(parse_file), [](...) {}};

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

TEST_F(RpcMessageRecorderTest, ParseAllCallbackHandler) {
  // Arrange
  auto parse_file = kj::newDiskFilesystem()->getCurrent().openFile(kTestDataPath);
  int call_count  = 0;
  const capnp_trace::StreamInfo exp_stream_info_in{
      2490, 2490, capnp_trace::StreamInfo::Direction::kIn, 7, "/tmp/hoge.sock"};
  const capnp_trace::StreamInfo exp_stream_info_out{
      2490, 2490, capnp_trace::StreamInfo::Direction::kOut, 7, "/tmp/hoge.sock"};
  capnp_trace::RpcMessageRecorder::Parser parser{
      kj::mv(parse_file),
      [&call_count, exp_stream_info_in, exp_stream_info_out](
          capnp_trace::StreamInfo stream_info, capnp::rpc::Message::Reader&& message,
          [[maybe_unused]] kj::ArrayPtr<kj::byte> raw_message) {
        ASSERT_TRUE((stream_info == exp_stream_info_in) || (stream_info == exp_stream_info_out))
            << kj::str(stream_info).cStr();
        if (!message.isCall()) {
          // Test only call message
          return;
        }
        call_count++;
        AssertTestInterfaceFooCall(kj::mv(message));
      }};

  // Act
  parser.ParseAll();

  // Assert
  ASSERT_EQ(3, call_count);
}
