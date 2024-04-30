#pragma once

#include <capnp/rpc.capnp.h>
#include <capnp/schema.h>
#include <kj/filesystem.h>
#include <unistd.h>

#include <array>
#include <functional>
#include <unordered_map>

#include "stream_info.h"

namespace capnp_trace {

using RpcMessageHandler =
    std::function<void(StreamInfo, capnp::rpc::Message::Reader&&, kj::ArrayPtr<kj::byte>)>;

/// @brief Reassembler for Cap'n Proto RPC Message
class RpcMessageReassembler final {
 public:
  RpcMessageReassembler(RpcMessageHandler handler, StreamInfo stream_info);
  ~RpcMessageReassembler();
  RpcMessageReassembler(const RpcMessageReassembler&)            = delete;
  RpcMessageReassembler& operator=(const RpcMessageReassembler&) = delete;
  RpcMessageReassembler(RpcMessageReassembler&&)                 = default;
  RpcMessageReassembler& operator=(RpcMessageReassembler&&)      = default;

  /// @brief Enable dump raw unix domain socket data for debug
  /// @param dump_file File path where the dump data will be stored
  RpcMessageReassembler& SetDumpFile(kj::Own<kj::AppendableFile> dump_file);

  /// @brief Reassemble Cap'n Proto RPC message from divided stream
  /// @param buf stream data to be reassembled
  /// @param len size of `buf` in bytes
  void Reassemble(char* buf, size_t len);

 private:
  void CallbackRpcMessageHandler(kj::ArrayPtr<kj::byte> buf);

  StreamInfo stream_info_;
  kj::Own<kj::VectorOutputStream> carry_buf_;
  RpcMessageHandler handler_;
  kj::Own<kj::AppendableFile> dump_file_;
};

}  // namespace capnp_trace
