#include "rpc_message_reassembler.h"

#include <capnp/common.h>
#include <capnp/message.h>
#include <capnp/rpc.capnp.h>
#include <capnp/serialize.h>
#include <kj/debug.h>

#include <cassert>
#include <cstdio>
#include <iostream>

namespace capnp_trace {
void RpcMessageReassembler::CallbackRpcMessageHandler(kj::ArrayPtr<kj::byte> buf) {
  // Since this is a debug tool, lift the usual security limits.  Worse case is
  // the process crashes or has to be killed.
  capnp::ReaderOptions options;
  options.nestingLimit          = kj::maxValue;
  options.traversalLimitInWords = kj::maxValue;

  kj::ArrayInputStream input_stream(buf);
  capnp::InputStreamMessageReader reader(input_stream, options);

  auto message = reader.getRoot<capnp::rpc::Message>();

  handler_(stream_info_, kj::mv(message), buf);
}

RpcMessageReassembler::RpcMessageReassembler(RpcMessageHandler handler, StreamInfo stream_info)
    : stream_info_(stream_info),
      carry_buf_(kj::heap<kj::VectorOutputStream>()),
      handler_(handler) {}

RpcMessageReassembler::~RpcMessageReassembler() {}

RpcMessageReassembler& RpcMessageReassembler::SetDumpFile(kj::Own<kj::AppendableFile> dump_file) {
  dump_file_ = kj::mv(dump_file);
  return *this;
}

// https://github.com/capnproto/capnproto/blob/v0.9.1/doc/encoding.md
void RpcMessageReassembler::Reassemble(char* buf, size_t len) {
  if (dump_file_) {
    dump_file_->write(buf, len);
    return;
  }

  const uint32_t header_segment_num_size = 4;
  uint32_t header_segment_size_size      = 0;
  uint32_t payload_size                  = 0;

  // Copy whole incoming data to carry_buf_
  carry_buf_->write(buf, len);
  const auto carry_size = carry_buf_->getArray().size();

  if (carry_size < header_segment_num_size) {
    // KJ_LOG(INFO, "carry because header(segment num) is incomplete", carry_size);
    return;
  }

  // Parse segment num
  uint32_t segment_num = le32toh((reinterpret_cast<uint32_t*>(&carry_buf_->getArray()[0]))[0]) + 1;

  header_segment_size_size = (((segment_num + 2) & ~1) - 1) * 4;
  if (carry_size < header_segment_num_size + header_segment_size_size) {
    // KJ_LOG(INFO, "carry because header(segment size) is incomplete", carry_size);
    return;
  }

  // Parse segment sizes
  uint32_t* segment_sizes = reinterpret_cast<uint32_t*>(alloca(segment_num * sizeof(uint32_t)));
  for (uint32_t i = 0; i < segment_num; i++) {
    segment_sizes[i] =
        le32toh((reinterpret_cast<uint32_t*>(&carry_buf_->getArray()[0]))[i + 1]) * 8;
    payload_size += segment_sizes[i];
  }

  const uint32_t total_message_size =
      header_segment_num_size + header_segment_size_size + payload_size;
  if (carry_size < total_message_size) {
    // KJ_LOG(INFO, "carry because payload is incomplete", carry_size);
    return;
  }

  // Parse ONE rpc::Message if payload exists
  if (payload_size > 0) {
    auto message = kj::arrayPtr<kj::byte>(&carry_buf_->getArray()[0], total_message_size);
    CallbackRpcMessageHandler(message);
  }

  // Carry rest data if remains
  auto next_carry_buf = kj::heap<kj::VectorOutputStream>();
  if (carry_size > total_message_size) {
    next_carry_buf->write(&carry_buf_->getArray()[total_message_size],
                          carry_size - total_message_size);
  }
  carry_buf_ = kj::mv(next_carry_buf);

  // Reassemble rest data recursively if remains
  if (carry_buf_->getArray().size() > 0) {
    KJ_LOG(INFO, "Reassemble rest data recursively", len);
    Reassemble(nullptr, 0);
  }
}

}  // namespace capnp_trace
