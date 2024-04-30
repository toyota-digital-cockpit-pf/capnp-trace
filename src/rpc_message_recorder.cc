#include "rpc_message_recorder.h"

#include <capnp/serialize.h>

#include <string>

#include "immutable_schema_registry.h"

namespace capnp_trace {

static const uint32_t kMagicNumber   = 0xCAB92ACE;
static const uint32_t kFormatVersion = 1;

RpcMessageRecorder::RpcMessageRecorder(kj::Own<kj::AppendableFile>&& output_file)
    : output_file_(kj::mv(output_file)) {
  output_file_->write(&kMagicNumber, sizeof(kMagicNumber));
  output_file_->write(&kFormatVersion, sizeof(kFormatVersion));
}

RpcMessageRecorder::~RpcMessageRecorder() {}

static inline uint64_t GetMonotonicMicroSec() {
  struct timespec tp;
  KJ_SYSCALL(clock_gettime(CLOCK_MONOTONIC, &tp));
  return tp.tv_sec * 1000000 + tp.tv_nsec / 1000;
}

#define RECORD_VAR(type, val)                                             \
  do {                                                                    \
    type tmp_var_for_record = val;                                        \
    output_file_->write(&tmp_var_for_record, sizeof(tmp_var_for_record)); \
  } while (false)

void RpcMessageRecorder::Record(StreamInfo stream_info,
                                [[maybe_unused]] capnp::rpc::Message::Reader&& message,
                                kj::ArrayPtr<kj::byte> raw_message) {
  auto timestamp = GetMonotonicMicroSec();
  RECORD_VAR(uint32_t, kMagicNumber);
  RECORD_VAR(uint64_t, timestamp);
  RECORD_VAR(uint64_t, stream_info.pid_);
  RECORD_VAR(uint64_t, stream_info.tid_);
  RECORD_VAR(uint64_t, stream_info.fd_);
  RECORD_VAR(uint32_t, stream_info.direction_);
  RECORD_VAR(uint32_t, static_cast<uint32_t>(stream_info.address_.length()));
  output_file_->write(stream_info.address_.c_str(), stream_info.address_.length());
  auto padding_len = stream_info.address_.length() % 4;
  if (padding_len) {
    // 4-byte align to find magic_number easily
    uint8_t* padding = reinterpret_cast<uint8_t*>(alloca(padding_len));
    memset(padding, 0, padding_len);
    output_file_->write(padding, padding_len);
  }
  RECORD_VAR(uint64_t, raw_message.size());
  output_file_->write(raw_message.begin(), raw_message.size());
  KJ_LOG(INFO, timestamp, stream_info, raw_message.size());
}

#define PARSE_VAR(type, var) \
  type var;                  \
  offset_ +=                 \
      input_file_->read(offset_, kj::arrayPtr(reinterpret_cast<kj::byte*>(&var), sizeof(var)));

RpcMessageRecorder::Parser::Parser(kj::Own<const kj::ReadableFile>&& input_file,
                                   RpcMessageHandler handler)
    : input_file_(kj::mv(input_file)), handler_(handler), offset_(0) {
  PARSE_VAR(uint32_t, magic_number);
  KJ_REQUIRE(kMagicNumber == magic_number);

  PARSE_VAR(uint32_t, format_version);
  KJ_REQUIRE(kFormatVersion == format_version);
}

RpcMessageRecorder::Parser::~Parser() {}

void RpcMessageRecorder::Parser::ParseAll() {
  // Since this is a debug tool, lift the usual security limits.  Worse case is
  // the process crashes or has to be killed.
  capnp::ReaderOptions options;
  options.nestingLimit          = kj::maxValue;
  options.traversalLimitInWords = kj::maxValue;

  size_t size = input_file_->stat().size;
  while (offset_ < size) {
    PARSE_VAR(uint32_t, magic_number);
    if (magic_number != kMagicNumber) {
      KJ_LOG(WARNING, "Magic Number not found", offset_);
      continue;
    }
    PARSE_VAR(uint64_t, timestamp);
    PARSE_VAR(uint64_t, pid);
    PARSE_VAR(uint64_t, tid);
    PARSE_VAR(uint64_t, fd);
    PARSE_VAR(uint32_t, direction);
    PARSE_VAR(uint32_t, address_length);
    uint32_t aligned_address_length = (address_length + 3) & ~3;  // 4-byte align
    char* aligned_address_buf       = reinterpret_cast<char*>(alloca(aligned_address_length));
    offset_ += input_file_->read(
        offset_,
        kj::arrayPtr(reinterpret_cast<kj::byte*>(aligned_address_buf), aligned_address_length));
    std::string address{aligned_address_buf, address_length};
    PARSE_VAR(uint64_t, payload_size);

    StreamInfo stream_info(static_cast<pid_t>(pid), static_cast<pid_t>(tid),
                           static_cast<StreamInfo::Direction>(direction), static_cast<int>(fd),
                           address);
    if (payload_size == 0) {
      KJ_LOG(WARNING, "Skip because of no payload", stream_info, aligned_address_length,
             address_length);
      continue;
    }

    auto buf = kj::heapArray<kj::byte>(payload_size);
    offset_ += input_file_->read(offset_, buf);
    KJ_LOG(INFO, timestamp, stream_info, payload_size, offset_);

    kj::ArrayInputStream input_stream(buf);
    capnp::InputStreamMessageReader reader(input_stream, options);
    auto message = reader.getRoot<capnp::rpc::Message>();

    handler_(stream_info, kj::mv(message), buf);
  }
}
}  // namespace capnp_trace
