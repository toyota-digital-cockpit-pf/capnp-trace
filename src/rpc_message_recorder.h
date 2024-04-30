#pragma once

#include <kj/filesystem.h>

#include "rpc_message_reassembler.h"

namespace capnp_trace {

/// @brief Reassembler for Cap'n Proto RPC Message
class RpcMessageRecorder final {
 public:
  /// @param answer_id_map RPC message context which is identified by answer ID
  /// @note answer_id_map must be shared between reassemblers for same
  /// session(fd)
  RpcMessageRecorder(kj::Own<kj::AppendableFile>&& output_file);
  ~RpcMessageRecorder();
  RpcMessageRecorder(const RpcMessageRecorder&)            = delete;
  RpcMessageRecorder& operator=(const RpcMessageRecorder&) = delete;
  RpcMessageRecorder(RpcMessageRecorder&&)                 = delete;
  RpcMessageRecorder& operator=(RpcMessageRecorder&&)      = delete;

  /// @brief Record Cap'n Proto RPC message
  void Record(StreamInfo stream_info, capnp::rpc::Message::Reader&& message,
              kj::ArrayPtr<kj::byte> raw_message);

 private:
  kj::Own<kj::AppendableFile> output_file_;

 public:
  class Parser final {
   public:
    Parser(kj::Own<const kj::ReadableFile>&& input_file, RpcMessageHandler handler);
    ~Parser();
    Parser(const Parser&)            = delete;
    Parser& operator=(const Parser&) = delete;
    Parser(Parser&&)                 = delete;
    Parser& operator=(Parser&&)      = delete;
    void ParseAll();

   private:
    kj::Own<const kj::ReadableFile> input_file_;
    RpcMessageHandler handler_;
    uint64_t offset_;
  };
};

}  // namespace capnp_trace
