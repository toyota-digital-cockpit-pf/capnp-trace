#pragma once

#include <kj/string.h>
#include <sys/types.h>

#include <string>

namespace capnp_trace {

class StreamInfo final {
 public:
  enum Direction {
    kUnknown,
    kIn,
    kOut,
  };

  StreamInfo() : pid_(0), tid_(0), direction_(Direction::kUnknown), fd_(0) {}
  StreamInfo(pid_t pid, pid_t tid, Direction direction, int fd, std::string address)
      : pid_(pid), tid_(tid), direction_(direction), fd_(fd), address_(kj::mv(address)) {}
  ~StreamInfo()                            = default;
  StreamInfo(const StreamInfo&)            = default;
  StreamInfo& operator=(const StreamInfo&) = default;
  StreamInfo(StreamInfo&&)                 = default;
  StreamInfo& operator=(StreamInfo&&)      = default;

  bool operator==(const StreamInfo& rhs) const {
    return (pid_ == rhs.pid_) && (tid_ == rhs.tid_) && (direction_ == rhs.direction_) &&
           (fd_ == rhs.fd_) && (address_ == rhs.address_);
  }

  bool operator!=(const StreamInfo& rhs) const { return !(*this == rhs); }

  pid_t pid_;
  pid_t tid_;
  Direction direction_;
  int fd_;
  std::string address_;
};

inline kj::StringPtr KJ_STRINGIFY(StreamInfo::Direction direction) {
  static const char* kDirectionStrings[] = {
      "kUnknown",
      "kIn",
      "kOut",
  };
  return kDirectionStrings[static_cast<uint>(direction)];
}

inline kj::String KJ_STRINGIFY(const StreamInfo& stream_info) {
  return kj::str("{pid:", stream_info.pid_, ",tid:", stream_info.tid_,
                 ",direction:", stream_info.direction_, ",fd:", stream_info.fd_,
                 ",address:", stream_info.address_, "}");
}

}  // namespace capnp_trace
