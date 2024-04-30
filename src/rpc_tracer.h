#pragma once

#include <kj/filesystem.h>
#include <kj/string.h>
#include <kj/vector.h>
#include <sys/types.h>
#include <sys/user.h>

#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

#include "rpc_message_reassembler.h"

namespace capnp_trace {
class RpcTracer final {
 public:
  RpcTracer(pid_t pid, kj::StringPtr address, RpcMessageHandler handler)
      : pid_(pid), target_address_(address.cStr()), handler_(handler) {}
  RpcTracer(const RpcTracer&)            = delete;
  RpcTracer& operator=(const RpcTracer&) = delete;
  RpcTracer(RpcTracer&&)                 = delete;
  RpcTracer& operator=(RpcTracer&&)      = delete;
  ~RpcTracer() {}

  /// @brief Enable dump raw unix domain socket data for debug
  /// @param dump_dir Directory where the dump data will be stored
  RpcTracer& SetDumpDir(kj::Own<const kj::Directory> dump_dir);

  /// @brief Start Cap'n Proto RPC tracing
  /// @details This method does not return
  void Trace();

 private:
  bool CheckAddress(int fd);
  void HandleLeaveConnect(pid_t tid, int fd, uint64_t addr, uint64_t size, int rc);
  void HandleLeaveWrite(pid_t tid, int fd, uint64_t addr, uint64_t count, int rc);
  void HandleLeaveWritev(pid_t tid, int fd, uint64_t iov_addr, uint64_t iov_count, int rc);
  void HandleLeaveReadRecvfrom(pid_t tid, int fd, uint64_t addr, uint64_t count, int rc);
  void HandleLeaveReadv(pid_t tid, int fd, uint64_t iov_addr, uint64_t iov_count, int rc);
  void HandleLeaveClose(pid_t tid, int fd, int rc);
  void DispatchSyscallHandler(pid_t tid, uint64_t syscall, bool is_enter, uint64_t arg0,
                              uint64_t arg1, uint64_t arg2, uint64_t rc);

  // PID to be traced
  pid_t pid_;

  // server address to be traced
  std::regex target_address_;

  // Callback function to be called when read/write Cap'n Proto RPC messages
  RpcMessageHandler handler_;

  // server address to be traced
  kj::Own<const kj::Directory> dump_dir_;

  // Map for fd -> server address
  std::unordered_map<int, std::string> addresses_;

  // Map for fd -> incoming message RpcMessageReassembler
  std::unordered_map<int, RpcMessageReassembler> reassemblers_in_;

  // Map for fd -> outgoing message RpcMessageReassembler
  std::unordered_map<int, RpcMessageReassembler> reassemblers_out_;
};
}  // namespace capnp_trace
