#include "stream_info.h"

#include <gtest/gtest.h>

class StreamInfoTest : public ::testing::Test {};

TEST_F(StreamInfoTest, CreateEmptyInstance) {
  // Arrange

  // Act
  capnp_trace::StreamInfo stream_info{};

  // Assert
  ASSERT_EQ(0, stream_info.pid_);
  ASSERT_EQ(0, stream_info.tid_);
  ASSERT_EQ(capnp_trace::StreamInfo::Direction::kUnknown, stream_info.direction_);
  ASSERT_EQ(0, stream_info.fd_);
  ASSERT_EQ("", stream_info.address_);
}

TEST_F(StreamInfoTest, CreateFilledInstance) {
  // Arrange

  // Act
  capnp_trace::StreamInfo stream_info{1, 2, capnp_trace::StreamInfo::Direction::kIn, 3, "test"};

  // Assert
  ASSERT_EQ(1, stream_info.pid_);
  ASSERT_EQ(2, stream_info.tid_);
  ASSERT_EQ(capnp_trace::StreamInfo::Direction::kIn, stream_info.direction_);
  ASSERT_EQ(3, stream_info.fd_);
  ASSERT_EQ("test", stream_info.address_);
}

TEST_F(StreamInfoTest, SameContentsStreamInfoReturnsEqual) {
  // Arrange

  // Act
  capnp_trace::StreamInfo stream_info1{1, 2, capnp_trace::StreamInfo::Direction::kIn, 3, "test"};
  capnp_trace::StreamInfo stream_info2{1, 2, capnp_trace::StreamInfo::Direction::kIn, 3, "test"};

  // Assert
  ASSERT_EQ(stream_info1, stream_info2);
}

TEST_F(StreamInfoTest, DifferentContentsStreamInfoReturnsNotEqual) {
  // Arrange

  // Act
  capnp_trace::StreamInfo stream_info_orig{1, 2, capnp_trace::StreamInfo::Direction::kIn, 3,
                                           "test"};
  capnp_trace::StreamInfo stream_info_pid{2, 2, capnp_trace::StreamInfo::Direction::kIn, 3, "test"};
  capnp_trace::StreamInfo stream_info_tid{1, 3, capnp_trace::StreamInfo::Direction::kIn, 3, "test"};
  capnp_trace::StreamInfo stream_info_direction{1, 2, capnp_trace::StreamInfo::Direction::kOut, 3,
                                                "test"};
  capnp_trace::StreamInfo stream_info_fd{1, 2, capnp_trace::StreamInfo::Direction::kIn, 4, "test"};
  capnp_trace::StreamInfo stream_info_address{1, 2, capnp_trace::StreamInfo::Direction::kIn, 3,
                                              "test1"};

  // Assert
  ASSERT_NE(stream_info_orig, stream_info_pid);
  ASSERT_NE(stream_info_orig, stream_info_tid);
  ASSERT_NE(stream_info_orig, stream_info_direction);
  ASSERT_NE(stream_info_orig, stream_info_fd);
  ASSERT_NE(stream_info_orig, stream_info_address);
}
