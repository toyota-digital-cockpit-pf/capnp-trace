#pragma once

#include <kj/debug.h>
#include <kj/string.h>
#include <signal.h>
#include <sys/types.h>

#include <functional>
#include <string>

namespace capnp_trace {

class Injection final {
 public:
  // inject_option format is based on strace inject option, but delimiter is '@'.
  // Ex: "method@signal=KILL@when=3"
  Injection(kj::StringPtr inject_option) : signal_(0), when_(0), count_(0) {
    const char kDelimiter = '@';
    auto ptr              = inject_option;
    while (true) {
      KJ_IF_MAYBE (pos, ptr.findFirst(kDelimiter)) {
        // If delimiter is found, sentence is [0, pos)
        auto sentence = kj::str(ptr.slice(0, *pos));
        ParseSentence(sentence);

        // proceed ptr to the back of delimiter
        ptr = ptr.slice(*pos + 1);
      } else {
        // If no delimiter is found, sentence is whole string
        ParseSentence(ptr);
        break;
      }
    }
    KJ_REQUIRE(signal_ != 0, "No signal is not supported so far...");
  }
  ~Injection()                           = default;
  Injection(const Injection&)            = default;
  Injection& operator=(const Injection&) = default;
  Injection(Injection&&)                 = default;
  Injection& operator=(Injection&&)      = default;

  void Check(kj::StringPtr method, std::function<void(int)> callback) {
    if (method == method_) {
      count_++;
    }
    if (count_ == when_) {
      callback(signal_);
    }
  }

  kj::String Stringify() const {
    return kj::str("{method:\"", method_, "\",signal:", signal_, ",when:", when_, ",count:", count_,
                   "}");
  }

  kj::String method_;
  int signal_;
  uint64_t when_;

 private:
  void ParseSentence(kj::StringPtr sentence) {
    if (sentence.startsWith("signal=")) {
      signal_ = ConvertSignal(sentence.slice("signal="_kj.size()));
    } else if (sentence.startsWith("when=")) {
      when_ = sentence.slice("when="_kj.size()).parseAs<uint32_t>();
    } else {
      method_ = kj::str(sentence);
    }
  }

  static int ConvertSignal(kj::StringPtr signal) {
    // NOTE: generated form `kill -l`
    if (signal == "HUP") {
      return SIGHUP;
    } else if (signal == "INT") {
      return SIGINT;
    } else if (signal == "QUIT") {
      return SIGQUIT;
    } else if (signal == "ILL") {
      return SIGILL;
    } else if (signal == "TRAP") {
      return SIGTRAP;
    } else if (signal == "ABRT") {
      return SIGABRT;
    } else if (signal == "BUS") {
      return SIGBUS;
    } else if (signal == "FPE") {
      return SIGFPE;
    } else if (signal == "KILL") {
      return SIGKILL;
    } else if (signal == "USR1") {
      return SIGUSR1;
    } else if (signal == "SEGV") {
      return SIGSEGV;
    } else if (signal == "USR2") {
      return SIGUSR2;
    } else if (signal == "PIPE") {
      return SIGPIPE;
    } else if (signal == "ALRM") {
      return SIGALRM;
    } else if (signal == "TERM") {
      return SIGTERM;
    } else if (signal == "STKFLT") {
      return SIGSTKFLT;
    } else if (signal == "CHLD") {
      return SIGCHLD;
    } else if (signal == "CONT") {
      return SIGCONT;
    } else if (signal == "STOP") {
      return SIGSTOP;
    } else if (signal == "TSTP") {
      return SIGTSTP;
    } else if (signal == "TTIN") {
      return SIGTTIN;
    } else if (signal == "TTOU") {
      return SIGTTOU;
    } else if (signal == "URG") {
      return SIGURG;
    } else if (signal == "XCPU") {
      return SIGXCPU;
    } else if (signal == "XFSZ") {
      return SIGXFSZ;
    } else if (signal == "VTALRM") {
      return SIGVTALRM;
    } else if (signal == "PROF") {
      return SIGPROF;
    } else if (signal == "WINCH") {
      return SIGWINCH;
    } else if (signal == "POLL") {
      return SIGPOLL;
    } else if (signal == "PWR") {
      return SIGPWR;
    } else if (signal == "SYS") {
      return SIGSYS;
    }
    return 0;
  }

  uint64_t count_;
};

inline kj::String KJ_STRINGIFY(const Injection& injection) { return injection.Stringify(); }

}  // namespace capnp_trace
