
#include "rpc_tracer.h"

#include <kj/debug.h>
#include <kj/string.h>
#include <limits.h>
#include <linux/elf.h>
#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#include "immutable_schema_registry.h"
#include "rpc_message_reassembler.h"
#include "stream_info.h"

namespace capnp_trace {

static void ReadProcessMemory(pid_t pid, uint64_t addr, uint64_t size, char* buf) {
  struct iovec local, remote;
  local.iov_base  = buf;
  local.iov_len   = size;
  remote.iov_base = reinterpret_cast<void*>(addr);
  remote.iov_len  = size;
  KJ_SYSCALL(process_vm_readv(pid, &local, 1, &remote, 1, 0));
}

static std::string GetPathFromFd(int pid, int fd) {
  // Get socket inode from /proc/PID/fd/FD
  //
  //   # readlink /proc/$(pidof app_management)/fd/8
  //   socket:[8633537]
  //
  std::string fd_dir_path =
      std::string("/proc/") + std::to_string(pid) + "/fd/" + std::to_string(fd);
  char buf[PATH_MAX] = {0};
  readlink(fd_dir_path.c_str(), buf, sizeof(buf) - 1);
  uint64_t inode;
  auto count = sscanf(buf, "socket:[%" PRIu64 "]", &inode);
  if (count != 1) {
    // fd is not socket
    return "";
  }

  // Get socket path from socket inode and /proc/PID/net/unix
  //
  //   # cat /proc/$(pidof app_management)/net/unix
  //   Num       RefCount Protocol Flags    Type St Inode Path
  //   ...
  //   ffff8fc2d4492640: 00000003 00000000 00000000 0001 03 8637583
  //   /run/arene/share/capnp.appmng.sock
  //
  std::string uds_path = std::string("/proc/") + std::to_string(pid) + "/net/unix";
  std::ifstream uds_file(uds_path);
  KJ_REQUIRE(uds_file.is_open());

  std::string line;
  std::regex re(".* " + std::to_string(inode) + R"( ([^ ]+))");
  std::smatch m;
  while (std::getline(uds_file, line)) {
    if (std::regex_match(line, m, re)) {
      return m[1].str();
    }
  }

  return "";
}

bool RpcTracer::CheckAddress(int fd) {
  if (addresses_.count(fd) == 0) {
    addresses_.emplace(fd, GetPathFromFd(pid_, fd));
    KJ_LOG(INFO, addresses_[fd]);
  }
  return std::regex_match(addresses_[fd], target_address_);
}

void RpcTracer::HandleLeaveConnect(pid_t tid, int fd, uint64_t addr, uint64_t size, int rc) {
  if (rc < 0) {
    KJ_LOG(INFO, "failed connect", rc);
    return;
  }

  std::vector<char> buf(size);
  ReadProcessMemory(tid, addr, size, buf.data());
  const struct sockaddr* saddr = reinterpret_cast<const struct sockaddr*>(buf.data());
  if (saddr->sa_family != AF_UNIX) {
    return;
  }
  const struct sockaddr_un* saddr_un = reinterpret_cast<const struct sockaddr_un*>(saddr);
  KJ_LOG(INFO, saddr_un->sun_path);

  addresses_.emplace(fd, saddr_un->sun_path);
}

void RpcTracer::HandleLeaveWrite(pid_t tid, int fd, uint64_t addr, uint64_t count, int rc) {
  if (rc < 0) {
    KJ_LOG(INFO, "failed write", rc);
    return;
  }
  if (!CheckAddress(fd)) {
    KJ_LOG(INFO, "Skip", addresses_[fd]);
    return;
  }

  if (reassemblers_out_.count(fd) == 0) {
    const StreamInfo stream_info(pid_, tid, StreamInfo::Direction::kOut, fd, addresses_[fd]);
    RpcMessageReassembler reassembler(handler_, stream_info);
    if (dump_dir_) {
      reassembler.SetDumpFile(dump_dir_->appendFile(
          kj::Path::parse(kj::str("capnp_trace.", fd, ".out.dump")),
          kj::WriteMode::CREATE | kj::WriteMode::MODIFY | kj::WriteMode::CREATE_PARENT));
    }
    reassemblers_out_.emplace(fd, kj::mv(reassembler));
  }

  std::vector<char> buf(count);
  ReadProcessMemory(tid, addr, count, buf.data());

#if 0
  for (int i = 0; i < count; i += 8) {
    fprintf(stdout, "write(%d): %02x %02x %02x %02x %02x %02x %02x %02x, %lld\n", fd, buf[i + 0],
            buf[i + 1], buf[i + 2], buf[i + 3], buf[i + 4], buf[i + 5], buf[i + 6], buf[i + 7],
            count);
  }
#endif

  reassemblers_out_.at(fd).Reassemble(buf.data(), count);
}

void RpcTracer::HandleLeaveWritev(pid_t tid, int fd, uint64_t iov_addr, uint64_t iov_count,
                                  int rc) {
  if (rc < 0) {
    KJ_LOG(INFO, "failed writev", rc);
    return;
  }
  if (!CheckAddress(fd)) {
    KJ_LOG(INFO, "Skip", addresses_[fd]);
    return;
  }

  if (reassemblers_out_.count(fd) == 0) {
    const StreamInfo stream_info(pid_, tid, StreamInfo::Direction::kOut, fd, addresses_[fd]);
    RpcMessageReassembler reassembler(handler_, stream_info);
    if (dump_dir_) {
      reassembler.SetDumpFile(dump_dir_->appendFile(
          kj::Path::parse(kj::str("capnp_trace.", fd, ".out.dump")),
          kj::WriteMode::CREATE | kj::WriteMode::MODIFY | kj::WriteMode::CREATE_PARENT));
    }
    reassemblers_out_.emplace(fd, kj::mv(reassembler));
  }

  struct iovec* iov = reinterpret_cast<struct iovec*>(alloca(sizeof(struct iovec) * iov_count));

  ReadProcessMemory(tid, iov_addr, sizeof(struct iovec) * iov_count, reinterpret_cast<char*>(iov));
  for (auto i = 0U; i < iov_count; i++) {
    std::vector<char> buf(iov[i].iov_len);
    ReadProcessMemory(tid, reinterpret_cast<uint64_t>(iov[i].iov_base), iov[i].iov_len, buf.data());

#if 0
    for (int j = 0; j < iov[i].iov_len; j += 8) {
      fprintf(stdout, "writev(%d): %02x %02x %02x %02x %02x %02x %02x %02x, %ld\n", fd, buf[j + 0],
              buf[j + 1], buf[j + 2], buf[j + 3], buf[j + 4], buf[j + 5], buf[j + 6], buf[j + 7],
              iov[i].iov_len);
    }
#endif

    reassemblers_out_.at(fd).Reassemble(buf.data(), iov[i].iov_len);
  }
}

// read(2) and recvfrom(2) have same arguments until argv[2]
void RpcTracer::HandleLeaveReadRecvfrom(pid_t tid, int fd, uint64_t addr, uint64_t count, int rc) {
  if (rc < 0) {
    KJ_LOG(INFO, "failed read/recvfrom");
    return;
  }
  if (!CheckAddress(fd)) {
    KJ_LOG(INFO, "Skip", addresses_[fd]);
    return;
  }

  if (reassemblers_in_.count(fd) == 0) {
    const StreamInfo stream_info(pid_, tid, StreamInfo::Direction::kIn, fd, addresses_[fd]);
    RpcMessageReassembler reassembler(handler_, stream_info);
    if (dump_dir_) {
      reassembler.SetDumpFile(dump_dir_->appendFile(
          kj::Path::parse(kj::str("capnp_trace.", fd, ".in.dump")),
          kj::WriteMode::CREATE | kj::WriteMode::MODIFY | kj::WriteMode::CREATE_PARENT));
    }
    reassemblers_in_.emplace(fd, kj::mv(reassembler));
  }

  std::vector<char> buf(count);
  ReadProcessMemory(tid, addr, count, buf.data());

#if 0
  for (int i = 0; i < count; i += 8) {
    fprintf(stdout, "read(%d): %02x %02x %02x %02x %02x %02x %02x %02x, %lld\n", fd, buf[i + 0],
            buf[i + 1], buf[i + 2], buf[i + 3], buf[i + 4], buf[i + 5], buf[i + 6], buf[i + 7],
            count);
  }
#endif

  reassemblers_in_.at(fd).Reassemble(buf.data(), count);
}

void RpcTracer::HandleLeaveReadv(pid_t tid, int fd, uint64_t iov_addr, uint64_t iov_count, int rc) {
  if (rc < 0) {
    KJ_LOG(INFO, "failed readv");
    return;
  }
  if (!CheckAddress(fd)) {
    KJ_LOG(INFO, "Skip", addresses_[fd]);
    return;
  }

  if (reassemblers_in_.count(fd) == 0) {
    const StreamInfo stream_info(pid_, tid, StreamInfo::Direction::kIn, fd, addresses_[fd]);
    RpcMessageReassembler reassembler(handler_, stream_info);
    if (dump_dir_) {
      reassembler.SetDumpFile(dump_dir_->appendFile(
          kj::Path::parse(kj::str("capnp_trace.", fd, ".in.dump")),
          kj::WriteMode::CREATE | kj::WriteMode::MODIFY | kj::WriteMode::CREATE_PARENT));
    }
    reassemblers_in_.emplace(fd, kj::mv(reassembler));
  }

  struct iovec* iov = reinterpret_cast<struct iovec*>(alloca(sizeof(struct iovec) * iov_count));

  ReadProcessMemory(tid, iov_addr, sizeof(struct iovec) * iov_count, reinterpret_cast<char*>(iov));
  for (auto i = 0U; i < iov_count; i++) {
    std::vector<char> buf(iov[i].iov_len);
    ReadProcessMemory(tid, (uint64_t)iov[i].iov_base, iov[i].iov_len, buf.data());

    reassemblers_in_.at(fd).Reassemble(buf.data(), iov[i].iov_len);
  }
}

void RpcTracer::HandleLeaveClose([[maybe_unused]] pid_t tid, int fd, int rc) {
  if (rc < 0) {
    KJ_LOG(INFO, "failed readv");
    return;
  }

  reassemblers_in_.erase(fd);
  reassemblers_out_.erase(fd);
  addresses_.erase(fd);
}

void RpcTracer::DispatchSyscallHandler(pid_t tid, uint64_t syscall, bool is_enter, uint64_t arg0,
                                       uint64_t arg1, uint64_t arg2, uint64_t rc) {
  // KJ_DBG(syscall, is_enter, arg0, arg1, arg2, rc);
  if (syscall == SYS_connect && !is_enter) {
    HandleLeaveConnect(tid, static_cast<int>(arg0), arg1, arg2, static_cast<int>(rc));
  } else if (syscall == SYS_read && !is_enter) {
    // for Cap'n Proto C++
    HandleLeaveReadRecvfrom(tid, static_cast<int>(arg0), arg1, arg2, static_cast<int>(rc));
  } else if (syscall == SYS_writev && !is_enter) {
    // for Cap'n Proto C++
    HandleLeaveWritev(tid, static_cast<int>(arg0), arg1, arg2, static_cast<int>(rc));
  } else if (syscall == SYS_recvfrom && !is_enter) {
    // for Cap'n Proto Rust
    HandleLeaveReadRecvfrom(tid, static_cast<int>(arg0), arg1, arg2, static_cast<int>(rc));
  } else if (syscall == SYS_write && !is_enter) {
    // for Cap'n Proto Rust
    HandleLeaveWrite(tid, static_cast<int>(arg0), arg1, arg2, static_cast<int>(rc));
  } else if (syscall == SYS_readv && !is_enter) {
    HandleLeaveReadv(tid, static_cast<int>(arg0), arg1, arg2, static_cast<int>(rc));
  } else if (syscall == SYS_close && !is_enter) {
    HandleLeaveClose(tid, static_cast<int>(arg0), static_cast<int>(rc));
  }
}

RpcTracer& RpcTracer::SetDumpDir(kj::Own<const kj::Directory> dump_dir) {
  this->dump_dir_ = kj::mv(dump_dir);
  return *this;
}

void RpcTracer::Trace() {
#if defined(__aarch64__)
  // Map for thread ID -> arg0
  std::unordered_map<pid_t, uint64_t> arg0s;
#endif

  while (1) {
    int status{-1};
    pid_t tid = waitpid(-1, &status, __WALL);
    if (WIFEXITED(status)) {
      KJ_LOG(INFO, tid, "exited", WEXITSTATUS(status));
      continue;
    } else if (WIFSIGNALED(status)) {
      KJ_LOG(WARNING, "terminated by signal", WTERMSIG(status));
    } else if (WIFSTOPPED(status)) {
      struct __ptrace_syscall_info syscall_info;
      KJ_SYSCALL(ptrace(PTRACE_GET_SYSCALL_INFO, tid, sizeof(syscall_info), &syscall_info));
      bool is_enter = syscall_info.op == PTRACE_SYSCALL_INFO_ENTRY;

#if defined(__aarch64__)
      struct user_regs_struct regs;
      struct iovec iov;
      iov.iov_len  = sizeof(regs);
      iov.iov_base = &regs;
      KJ_SYSCALL(ptrace(PTRACE_GETREGSET, tid, NT_PRSTATUS, &iov));
      uint64_t syscall = regs.regs[8];
      uint64_t rc      = regs.regs[0];
      uint64_t arg0    = regs.regs[0];
      uint64_t arg1    = regs.regs[1];
      uint64_t arg2    = regs.regs[2];

      // Remember arg0 until leaving systrace
      // because regs.regs[0] is re-used for rc and arg0 is removed
      if (is_enter) {
        arg0s.emplace(tid, arg0);
      } else {
        arg0 = arg0s[tid];
        arg0s.erase(tid);
      }
#else
      struct user_regs_struct regs;
      KJ_SYSCALL(ptrace(PTRACE_GETREGS, tid, nullptr, &regs));
      uint64_t syscall = regs.orig_rax;
      uint64_t rc      = regs.rax;
      uint64_t arg0    = regs.rdi;
      uint64_t arg1    = regs.rsi;
      uint64_t arg2    = regs.rdx;
#endif

      DispatchSyscallHandler(tid, syscall, is_enter, arg0, arg1, arg2, rc);
    }

    KJ_SYSCALL(ptrace(PTRACE_SYSCALL, tid, nullptr, nullptr));
  }
}
}  // namespace capnp_trace
