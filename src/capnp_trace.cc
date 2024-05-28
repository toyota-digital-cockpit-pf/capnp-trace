#include <capnp/any.h>
#include <capnp/common.h>
#include <capnp/compat/json.h>
#include <capnp/message.h>
#include <capnp/rpc.capnp.h>
#include <capnp/schema.h>
#include <capnp/serialize.h>
#include <dirent.h>
#include <kj/debug.h>
#include <kj/filesystem.h>
#include <kj/main.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ptrace.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <unordered_map>

#include "immutable_schema_registry.h"
#include "injection.h"
#include "rpc_message_reassembler.h"
#include "rpc_message_recorder.h"
#include "rpc_tracer.h"

namespace capnp_trace {

static const char VERSION_STRING[] = "capnp_trace v0.1.1";

static inline std::string GetTimeStamp() {
  struct timespec tp;
  KJ_SYSCALL(clock_gettime(CLOCK_MONOTONIC, &tp));
  std::ostringstream stream;
  stream << std::setw(6) << std::setfill('0') << tp.tv_sec << "." << std::setw(4)
         << (tp.tv_nsec / 100000);
  return stream.str();
}

class TraceMain final {
 public:
  explicit TraceMain(kj::ProcessContext& context)
      : context(context),
        handler_(KJ_BIND_METHOD(*this, OutputRpcMessage)),
        ptrace_options_(PTRACE_O_TRACESYSGOOD),
        argc_(0),
        is_follow_(false),
        is_color_(false),
        is_parse_raw_(false) {
    capnp_trace::ImmutableSchemaRegistry::Init();
  }

  kj::MainFunc getMain() {
    return kj::MainBuilder(context, VERSION_STRING,
                           "Command-line tool for Cap'n Proto RPC tracing.")
        .addSubCommand("attach", KJ_BIND_METHOD(*this, GetAttachMain),
                       "Attach to the existing thread and trace it.")
        .addSubCommand("exec", KJ_BIND_METHOD(*this, GetExecMain),
                       "Fork and exec new process and trace it.")
        .addSubCommand("parse", KJ_BIND_METHOD(*this, GetParseMain),
                       "Parse recoreded/dumped files.")
        .build();
  }

  kj::MainFunc GetAttachMain() {
    kj::MainBuilder builder(context, VERSION_STRING, "Attach to the existing thread and trace it.");
    builder.expectArg("SERVER_ADDRESS", KJ_BIND_METHOD(*this, SetAddress))
        .expectArg("PID", KJ_BIND_METHOD(*this, SetPid))
        .callAfterParsing(KJ_BIND_METHOD(*this, AttachMain));
    AddCommonOption(builder);
    AddOutputOption(builder);
    return builder.build();
  }

  kj::MainFunc GetExecMain() {
    kj::MainBuilder builder(context, VERSION_STRING, "Fork and exec new process and trace it.");
    builder.expectArg("SERVER_ADDRESS", KJ_BIND_METHOD(*this, SetAddress))
        .expectOneOrMoreArgs("command <args>", KJ_BIND_METHOD(*this, SetCommand))
        .callAfterParsing(KJ_BIND_METHOD(*this, ExecMain));
    AddCommonOption(builder);
    AddOutputOption(builder);
    return builder.build();
  }

  kj::MainFunc GetParseMain() {
    kj::MainBuilder builder(context, VERSION_STRING, "Parse recorded/dumped files.");
    builder
        .addOption({'r', "raw"}, KJ_BIND_METHOD(*this, SetParseRaw),
                   "Parse raw dumped files.\n"
                   "CAUTION\n"
                   "  Don't trust timestamp information in this mode.\n"
                   "  It shows parsing time because raw dump file\n"
                   "  does not contain timestamp information.")
        .expectOneOrMoreArgs("file", KJ_BIND_METHOD(*this, SetParseFile))
        .callAfterParsing(KJ_BIND_METHOD(*this, ParseMain));
    AddOutputOption(builder);
    return builder.build();
  }

  kj::MainBuilder::Validity SetAddress(kj::StringPtr addr) {
    address_ = addr;
    return true;
  }

  kj::MainBuilder::Validity SetPid(kj::StringPtr pid) {
    char* end;
    pid_ = static_cast<pid_t>(strtol(pid.cStr(), &end, 0));
    if (pid.size() == 0 || *end != '\0') {
      return "not an integer";
    }
    return true;
  }

  kj::MainBuilder::Validity SetCommand(kj::StringPtr command) {
    command_[argc_++] = command.cStr();
    return true;
  }

  kj::MainBuilder::Validity SetFollow() {
    is_follow_ = true;
    ptrace_options_ |= PTRACE_O_TRACECLONE;
    return true;
  }

  kj::MainBuilder::Validity SetColor() {
    is_color_ = true;
    return true;
  }

  kj::MainBuilder::Validity SetRecord(kj::StringPtr record_path) {
    kj::Own<kj::AppendableFile> record_file;
    if (record_path[0] == '/') {
      // Absolute path
      //   `kj::Path::parse` doesn't support absolute path
      record_file = kj::newDiskFilesystem()->getRoot().appendFile(
          kj::Path(nullptr).eval(record_path),
          kj::WriteMode::CREATE | kj::WriteMode::CREATE_PARENT | kj::WriteMode::MODIFY);
    } else {
      // Relative path
      record_file = kj::newDiskFilesystem()->getCurrent().appendFile(
          kj::Path::parse(record_path),
          kj::WriteMode::CREATE | kj::WriteMode::CREATE_PARENT | kj::WriteMode::MODIFY);
    }
    recorder_ = kj::heap<RpcMessageRecorder>(kj::mv(record_file));
    handler_  = KJ_BIND_METHOD(*recorder_, Record);
    return true;
  }

  kj::MainBuilder::Validity SetDump(kj::StringPtr dump_path) {
    if (dump_path[0] == '/') {
      // Absolute path
      //   `kj::Path::parse` doesn't support absolute path
      dump_dir_ = kj::newDiskFilesystem()->getRoot().openSubdir(
          kj::Path(nullptr).eval(dump_path),
          kj::WriteMode::CREATE | kj::WriteMode::CREATE_PARENT | kj::WriteMode::MODIFY);
    } else {
      // Relative path
      dump_dir_ = kj::newDiskFilesystem()->getCurrent().openSubdir(
          kj::Path::parse(dump_path),
          kj::WriteMode::CREATE | kj::WriteMode::CREATE_PARENT | kj::WriteMode::MODIFY);
    }
    return true;
  }

  kj::MainBuilder::Validity SetInject(kj::StringPtr inject_option) {
    injection_.emplace(inject_option);
    return true;
  }

  kj::MainBuilder::Validity ExecMain() {
    pid_t pid;
    KJ_SYSCALL(pid = fork());
    if (pid == 0) {
      KJ_SYSCALL(ptrace(PTRACE_TRACEME, 0, nullptr, nullptr));
      KJ_SYSCALL(execvp(command_[0], const_cast<char* const*>(command_)));
    }
    waitpid(-1, nullptr, __WALL);

    KJ_SYSCALL(ptrace(PTRACE_SETOPTIONS, pid, nullptr, ptrace_options_));
    KJ_SYSCALL(ptrace(PTRACE_SYSCALL, pid, nullptr, nullptr));

    RpcTracer(pid, address_, handler_).SetDumpDir(kj::mv(dump_dir_)).Trace();

    return true;
  }

  kj::MainBuilder::Validity AttachMain() {
    if (is_follow_) {
      AttachAllThreads(pid_);
    } else {
      AttachThread(pid_);
    }

    RpcTracer(pid_, address_, handler_).SetDumpDir(kj::mv(dump_dir_)).Trace();

    return true;
  }

  kj::MainBuilder::Validity SetParseRaw() {
    is_parse_raw_ = true;
    return true;
  }

  kj::MainBuilder::Validity SetParseFile(kj::StringPtr parse_file) {
    if (parse_file[0] == '/') {
      // Absolute path
      //   `kj::Path::parse` doesn't support absolute path
      parse_files_.add(
          kj::newDiskFilesystem()->getRoot().openFile(kj::Path(nullptr).eval(parse_file)));

    } else {
      // Relative path
      parse_files_.add(kj::newDiskFilesystem()->getCurrent().openFile(kj::Path::parse(parse_file)));
    }
    return true;
  }

  kj::MainBuilder::Validity ParseRawFormat() {
    for (auto& parse_file : parse_files_) {
      auto bytes = parse_file->readAllBytes();
      AnswerIdMap answer_ids;
      // Use empty StreamInfo because raw dumped data does not contain StreamInfo
      RpcMessageReassembler reassembler(handler_, StreamInfo{});
      reassembler.Reassemble(bytes.asChars().begin(), bytes.size());
    }
    return true;
  }

  kj::MainBuilder::Validity ParseMain() {
    if (is_parse_raw_) {
      return ParseRawFormat();
    }

    for (auto& parse_file : parse_files_) {
      RpcMessageRecorder::Parser parser(kj::mv(parse_file),
                                        KJ_BIND_METHOD(*this, OutputRpcMessage));
      parser.ParseAll();
    }
    return true;
  }

 private:
  void AddCommonOption(kj::MainBuilder& builder) {
    builder.addOption({'f', "follow"}, KJ_BIND_METHOD(*this, SetFollow),
                      "Trace child threads as they are created by currently "
                      "traced threads as a result of the clone(2) system calls. "
                      "Note  that attach -f PID will attach "
                      "all threads of process PID if it is multi-threaded, not "
                      "only thread with thread_id = PID.");
    builder.addOptionWithArg({'r', "record"}, KJ_BIND_METHOD(*this, SetRecord), "<output_path>",
                             "Record Cap'n Proto RPC messages to <output_path>");
    builder.addOptionWithArg({'d', "dump"}, KJ_BIND_METHOD(*this, SetDump), "<output_path>",
                             "Dump unix domain socket communication raw data to <output_path>");
    builder.addOptionWithArg({'i', "inject"}, KJ_BIND_METHOD(*this, SetInject),
                             "method;signal=sig;when=expr",
                             "Perform tampering for the specified method");
  }

  void AddOutputOption(kj::MainBuilder& builder) {
    builder.addOption({'c', "color"}, KJ_BIND_METHOD(*this, SetColor), "Colorize the output.");
  }

  void AttachThread(int pid) {
    KJ_SYSCALL(ptrace(PTRACE_ATTACH, pid, nullptr, nullptr));
    waitpid(pid, nullptr, 0);
    KJ_SYSCALL(ptrace(PTRACE_SETOPTIONS, pid, nullptr, ptrace_options_));
    KJ_SYSCALL(ptrace(PTRACE_SYSCALL, pid, nullptr, nullptr));
  }

  std::vector<pid_t> GetTids(pid_t pid) {
    std::vector<pid_t> tids;
    std::string taskdir_path = std::string("/proc/") + std::to_string(pid) + "/task";
    DIR* taskdir             = opendir(taskdir_path.c_str());
    KJ_REQUIRE(taskdir);

    struct dirent* d = readdir(taskdir);
    while (d) {
      std::string dir_name(d->d_name);
      if (dir_name == "." || dir_name == "..") {
        d = readdir(taskdir);
        continue;
      }
      tids.push_back(std::stoi(dir_name));

      d = readdir(taskdir);
    }
    closedir(taskdir);
    return tids;
  }

  void AttachAllThreads(int pid) {
    // Suspend tracee to get thread list
    kill(pid, SIGSTOP);

    // Attach all threads
    for (auto tid : GetTids(pid)) {
      KJ_SYSCALL(ptrace(PTRACE_ATTACH, tid, 0, 0));
      waitpid(-1, nullptr, __WALL);
      KJ_SYSCALL(ptrace(PTRACE_SETOPTIONS, tid, nullptr, ptrace_options_));
    }

    // Resume all threads
    for (auto tid : GetTids(pid)) {
      KJ_SYSCALL(ptrace(PTRACE_SYSCALL, tid, nullptr, nullptr));
    }

    // Resume tracee to run even after capnp_trace exited
    kill(pid, SIGCONT);
  }

  enum Color { RED, GREEN, BLUE };

  inline kj::String Colorize(Color color, kj::StringPtr message) {
    kj::StringPtr start_color, end_color;
    if (is_color_) {
      switch (color) {
        case RED:
          start_color = "\033[0;1;31m";
          break;
        case GREEN:
          start_color = "\033[0;1;32m";
          break;
        case BLUE:
          start_color = "\033[0;1;34m";
          break;
      }
      end_color = "\033[0m";
    }

    return kj::str(start_color, message, end_color);
  }

  inline kj::String GetMessageType(capnp::rpc::Message::Which type) {
    // https://github.com/capnproto/capnproto/blob/v0.9.1/c%2B%2B/src/capnp/rpc.capnp#L215-L273
    static const char* kTypeStrings[] = {
        "UNIMPLEMENTED", "ABORT",   "CALL",          "RETURN",     "FINISH",
        "RESOLVE",       "RELEASE", "OBSOLETE_SAVE", "BOOTSTRAP",  "OBSOLETE_DELETE",
        "PROVIDE",       "ACCEPT",  "JOIN",          "DISEMBARGO",
    };

    switch (type) {
      case capnp::rpc::Message::UNIMPLEMENTED:
      case capnp::rpc::Message::ABORT:
        return Colorize(RED, kTypeStrings[type]);

      // Level 0 features
      case capnp::rpc::Message::BOOTSTRAP:
      case capnp::rpc::Message::CALL:
      case capnp::rpc::Message::RETURN:
      case capnp::rpc::Message::FINISH:
        return Colorize(BLUE, kTypeStrings[type]);

      // Level 1 features
      case capnp::rpc::Message::RESOLVE:
      case capnp::rpc::Message::RELEASE:
      case capnp::rpc::Message::DISEMBARGO:
        return Colorize(GREEN, kTypeStrings[type]);

      // Level 2-4 features
      default:
        return Colorize(RED, kTypeStrings[type]);
    }
  }

  inline kj::String MakePrefix(StreamInfo stream, capnp::rpc::Message::Which type) {
    const char* direction = stream.direction_ == StreamInfo::Direction::kIn    ? " <- "
                            : stream.direction_ == StreamInfo::Direction::kOut ? " -> "
                                                                               : " - ";
    return kj::str(GetTimeStamp(), " ", stream.pid_, "/", stream.tid_, direction, stream.address_,
                   "(", stream.fd_, ") ", GetMessageType(type));
  }

  kj::String MakeOutputForBootstrap(capnp::rpc::Bootstrap::Reader bootstrap) {
    return kj::str("(", bootstrap.getQuestionId(), ")");
  }

  // Stringify DynamicStruct without exception even if it contains external capability
  kj::String Stringify(const capnp::DynamicStruct::Reader& value) {
    auto schema = value.getSchema();
    auto fields = schema.getFields();
    kj::Vector<kj::StringTree> printed_fields(fields.size());
    for (auto field : fields) {
      if (field.getType().isInterface()) {
        printed_fields.add(kj::strTree(field.getProto().getName(), " = <external capability>"));
        continue;
      }
      kj::String field_value;
      auto maybe_exception = kj::runCatchingExceptions([&value, &field, &field_value](){
        field_value = kj::str(value.get(field));
      });
      KJ_IF_MAYBE(exception, maybe_exception) {
        KJ_LOG(INFO, exception);
        field_value = kj::str("<external capability>");
      }
      printed_fields.add(kj::strTree(field.getProto().getName(), " = ", field_value));
    }

    return kj::str("(", kj::StringTree(printed_fields.releaseAsArray(), ", "), ")");
  }

  kj::String MakeOutputForCall(capnp::rpc::Call::Reader call,
                               kj::Maybe<capnp::StructSchema> detail_param_type) {
    auto interface = capnp_trace::ImmutableSchemaRegistry::GetInterface(call.getInterfaceId());
    auto method    = interface.getMethods()[call.getMethodId()];
    auto content   = call.getParams().getContent();
    auto param_value =
        content.getAs<capnp::DynamicStruct>(detail_param_type.orDefault(method.getParamType()));

    auto method_name =
        kj::str(interface.getProto().getDisplayName(), ".", method.getProto().getName());

    KJ_IF_MAYBE (injection, injection_) {
      // When injection condition is satisfied, send SIGKILL to tracee process.
      injection->Check(method_name, [injection, pid = pid_](int sig) {
        KJ_LOG(WARNING, *injection, "fired");
        kill(pid, sig);
        exit(EXIT_SUCCESS);
      });
    }

    return kj::str("(", call.getQuestionId(), ") ", method_name, Stringify(param_value));
  }

  kj::String MakeOutputForReturn(capnp::rpc::Return::Reader ret, capnp::StructSchema result_type) {
    auto content = ret.getResults().getContent();
    kj::String result_str;
    if (content.isCapability()) {
      result_str = kj::str("(<external capability>)");
    } else if (content.isStruct()) {
      result_str = Stringify(content.getAs<capnp::DynamicStruct>(result_type));
    } else if (content.isNull()) {
      result_str = kj::str("()");
    } else if (content.isList()) {
      result_str = kj::str("[List]");
    } else {
      KJ_UNIMPLEMENTED();
    }
    return kj::str("(", ret.getAnswerId(), ") ", result_type.getProto().getDisplayName(), " ",
                   result_str);
  }

  kj::String MakeOutputForFinish(capnp::rpc::Finish::Reader finish) {
    return kj::str("(", finish.getQuestionId(), ")");
  }

  void OutputRpcMessage(StreamInfo stream_info, capnp::rpc::Message::Reader&& message,
                        [[maybe_unused]] kj::ArrayPtr<kj::byte> raw_message) {
    // NOTE: OutputRpcMessage cannot release answer_id_maps when fd is closed because this handler
    // doesn't know the timing. It causes slight leak. And if fd and answer_id are re-used,
    // answer_id_maps_ for them should be over-written before used so there should be no problem.
    // The same applies to cap_descriptor_map.
    AnswerIdMap& answer_id_map           = answer_id_maps_[stream_info.fd_];
    CapDescriptorMap& cap_descriptor_map = cap_descriptor_maps_[stream_info.fd_];

    // Only when target is imported capability and its registered,
    // we decode parameter with registered type information (detail_param_type).
    // It's because type information from ImmutableSchemaRegistry doesn't contain generics.
    kj::Maybe<capnp::StructSchema> detail_param_type;

    if (message.isCall()) {
      // Register result type for RETURN
      auto call      = message.getCall();
      auto interface = capnp_trace::ImmutableSchemaRegistry::GetInterface(call.getInterfaceId());
      auto method    = interface.getMethods()[call.getMethodId()];
      answer_id_map.emplace(call.getQuestionId(), method.getResultType());

      // If capabilities are passed as parameters
      if (call.getParams().getCapTable().size() > 0) {
        if (call.getParams().getCapTable().size() > 1) {
          KJ_LOG(WARNING,
                 "capnp_trace doesn't support multiple capabilities in one message so far.");
        }

        for (auto field : method.getParamType().getFields()) {
          if (field.getType().isInterface()) {
            // Register the passed capability into cap_descriptor_map
            // TODO(t-kondo-tmc): Fix CapTable index according to the following specification:
            //   https://github.com/capnproto/capnproto/blob/v0.9.1/doc/encoding.md#capabilities-interfaces
            auto export_id = call.getParams().getCapTable()[0].getSenderHosted();
            KJ_LOG(INFO, "Register cap_descriptor_maps[", stream_info.fd_, "][", export_id,
                   "] = ", field.getProto().getName(), " of ",
                   interface.getProto().getDisplayName(), ".", method.getProto().getName());
            cap_descriptor_map.emplace(export_id, field.getType().asInterface());
          }
        }
      }

      // If RPC is called to imported capability, search cap_descriptor_map for detail type
      // information, like generics arguments
      if (call.getTarget().isImportedCap()) {
        auto import_id = call.getTarget().getImportedCap();
        if (cap_descriptor_map.count(import_id)) {
          auto cap = cap_descriptor_map.at(import_id);
          KJ_IF_MAYBE (detail_method, cap.findMethodByName(method.getProto().getName())) {
            detail_param_type = detail_method->getParamType();
          } else {
            KJ_LOG(INFO, "method", method.getProto().getName(), "is not found in ",
                   cap.getShortDisplayName());
          }
        } else {
          KJ_LOG(INFO, "import_id()", import_id, "is not found in cap_descriptor_map[",
                 stream_info.fd_, "]");
        }
      }
    } else if (message.isFinish()) {
      // Unregister result type
      auto finish = message.getFinish();
      answer_id_map.erase(finish.getQuestionId());
    }

    kj::String output = MakePrefix(stream_info, message.which());
    switch (message.which()) {
      case capnp::rpc::Message::UNIMPLEMENTED:
      case capnp::rpc::Message::ABORT:
        break;

      // Level 0 features
      case capnp::rpc::Message::BOOTSTRAP:
        output = kj::str(output, MakeOutputForBootstrap(message.getBootstrap()));
        break;
      case capnp::rpc::Message::CALL:
        output = kj::str(output, MakeOutputForCall(message.getCall(), detail_param_type));
        break;
      case capnp::rpc::Message::RETURN:
        output =
            kj::str(output, MakeOutputForReturn(message.getReturn(),
                                                answer_id_map[message.getReturn().getAnswerId()]));
        break;
      case capnp::rpc::Message::FINISH:
        output = kj::str(output, MakeOutputForFinish(message.getFinish()));
        break;

      // Level 1-4 features
      default:
        break;
    }

    std::cerr << output.cStr() << std::endl;
  }

  kj::ProcessContext& context;
  RpcMessageHandler handler_;
  kj::StringPtr address_;
  pid_t pid_;
  uint64_t ptrace_options_;
  uint32_t argc_;
  const char* command_[1024];
  bool is_follow_;
  bool is_color_;
  kj::Own<RpcMessageRecorder> recorder_;
  kj::Own<const kj::Directory> dump_dir_;
  bool is_parse_raw_;
  kj::Vector<kj::Own<const kj::ReadableFile>> parse_files_;

  // Map for Cap'n Proto answer ID (i.e. request ID) -> Return type StructSchema
  using AnswerIdMap = std::unordered_map<uint64_t, capnp::StructSchema>;
  // Map for fd -> AnswerIdMap
  std::unordered_map<int, AnswerIdMap> answer_id_maps_;

  // Map for CapDescriptor -> InterfaceSchema
  using CapDescriptorMap = std::unordered_map<uint32_t, capnp::InterfaceSchema>;
  // Map for fd -> CapDescriptorMap
  std::unordered_map<int, CapDescriptorMap> cap_descriptor_maps_;

  kj::Maybe<Injection> injection_;
};
}  // namespace capnp_trace

KJ_MAIN(capnp_trace::TraceMain)
