![Cap'n Proto RPC tracer](doc/logo.png)

## Abstract

`capnp_trace` is a debug tool that intercepts and records Cap'n Proto RPC which are called by a process and received by a process.

## 💪 Features

- Launch sub process and trace its Cap'n Proto RPC
- Attach existing process and trace its Cap'n Proto RPC
- Record Cap'n Proto RPC and parse it offline
- Signal injection based on Cap'n Proto RPC

### Supported OS

- Linux (x86-64, aarch64)

## 📥️ Installation

```
cmake -B build -S . -D CAPNP_TRACE_SCHEMA_DIRS="import_schema_dir1/;import_schema_dir2/"
cmake --build build
cmake --install build --prefix build/prefix
```

## 🚀 Usage

## 📜 License

[MIT License](https://opensource.org/license/mit)
