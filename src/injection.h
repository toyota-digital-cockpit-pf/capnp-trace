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
  Injection(kj::StringPtr inject_option) : signal_(SIGKILL), when_(0), count_(0) {
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
  }
  ~Injection()                           = default;
  Injection(const Injection&)            = default;
  Injection& operator=(const Injection&) = default;
  Injection(Injection&&)                 = default;
  Injection& operator=(Injection&&)      = default;

  void Check(kj::StringPtr method, std::function<void()> callback) {
    if (method == method_) {
      count_++;
    }
    if (count_ == when_) {
      callback();
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
      auto signal = sentence.slice("signal="_kj.size());
      KJ_REQUIRE(signal == "KILL", "only SIGKILL is supported so far");
    } else if (sentence.startsWith("when=")) {
      when_ = sentence.slice("when="_kj.size()).parseAs<uint32_t>();
    } else {
      method_ = kj::str(sentence);
    }
  }

  uint64_t count_;
};

inline kj::String KJ_STRINGIFY(const Injection& injection) { return injection.Stringify(); }

}  // namespace capnp_trace
