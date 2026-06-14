<div align="center">

[🇨🇳 中文](README_CN.md) &nbsp;|&nbsp; 🇬🇧 English

</div>

---

# lldb-mcp

LLDB MCP Server — wraps the LLDB debugger as a [Model Context Protocol (MCP)](https://modelcontextprotocol.io) server, enabling AI assistants (Claude, OpenCode, etc.) to set breakpoints, step through code, inspect variables, and more — all through the standard MCP interface.

## Feature Overview

| Category | Tool | Description |
|----------|------|-------------|
| Session | `lldb_start` | Start a new LLDB session |
| | `lldb_list_sessions` | List all active sessions |
| | `lldb_terminate` | Terminate a session |
| Program Control | `lldb_load` | Load a program for debugging |
| | `lldb_attach` | Attach to a running process |
| | `lldb_load_core` | Load a core dump file |
| | `lldb_run` | Run the loaded program |
| | `lldb_continue` | Continue execution |
| | `lldb_kill` | Kill the running process |
| Breakpoints | `lldb_set_breakpoint` | Set a breakpoint (with optional condition) |
| | `lldb_breakpoint_list` | List all breakpoints |
| | `lldb_breakpoint_delete` | Delete a breakpoint |
| | `lldb_watchpoint` | Set a memory watchpoint |
| Stepping | `lldb_step` | Step into function calls |
| | `lldb_next` | Step over function calls |
| | `lldb_finish` | Run until current function returns |
| Inspection | `lldb_print` | Print variable/expression value |
| | `lldb_expression` | Evaluate expression in current frame |
| | `lldb_examine` | Examine memory |
| | `lldb_info_registers` | Display registers |
| | `lldb_disassemble` | Disassemble code |
| | `lldb_process_info` | Show process information |
| Stack/Threads | `lldb_backtrace` | Show call stack |
| | `lldb_frame_info` | Show frame details (with local variables) |
| | `lldb_thread_list` | List all threads |
| | `lldb_thread_select` | Select a specific thread |
| Other | `lldb_command` | Execute an arbitrary LLDB command |
| | `lldb_help` | Show LLDB help |

**29 tools** in total, covering the full LLDB debugging workflow.

## Build

### Dependencies

- **CMake** >= 3.16
- **Boost** >= 1.79 (`filesystem` and `json` components)
- **C++17** compiler
- **LLDB** (runtime only; included with macOS, install separately on Linux)

### Compile

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

The output binary is `build/lldb-mcp`.

### Install Boost (macOS)

```bash
brew install boost
```

### Install Boost (Linux)

```bash
apt install libboost-filesystem-dev libboost-json-dev
# or
yum install boost-devel
```

## Usage

### Command Line

```bash
./lldb-mcp [--debug]
```

- `--debug` — print debug logs to stderr (does not interfere with MCP protocol)
- `-h` / `--help` — print help

The program reads JSON-RPC messages from stdin and responds via stdout. Two transport modes are supported:
- **Newline-delimited JSON**
- **HTTP-style Content-Length header**

### MCP Server Configuration

Add to your AI assistant's MCP config (OpenCode example, `opencode.json`):

```json
{
  "mcpServers": {
    "lldb-mcp": {
      "command": "/path/to/lldb-mcp",
      "args": []
    }
  }
}
```

## Example

Debug a Fibonacci program through the MCP interface:

```
1. lldb_start                          # Start LLDB session
2. lldb_load(session_id, "test_rec")   # Load the program
3. lldb_set_breakpoint(session_id, "fib")  # Set breakpoint at fib()
4. lldb_run(session_id)                # Run — hits breakpoint
5. lldb_print(session_id, "n")         # Print parameter n
6. lldb_backtrace(session_id)          # Show call stack
7. lldb_step(session_id)               # Step into recursion
8. lldb_continue(session_id)           # Continue execution
9. lldb_terminate(session_id)          # End session
```

## Project Structure

```
lldb-mcp/
├── CMakeLists.txt          # CMake build configuration
├── src/
│   ├── main.cpp            # Entry point, CLI parsing, MCP server startup
│   ├── lldb_session.hpp/cpp # LLDB session management (PTY fork + command execution)
│   ├── mcp_server.hpp/cpp   # MCP protocol (JSON-RPC, transport detection)
│   └── mcp_tools.hpp/cpp    # Tool registration and dispatch
└── tests/
    ├── test_rec.c          # Test program: recursive Fibonacci
    ├── test_bug.c          # Test program: buggy factorial
    └── test_args.c         # Test program: print command-line args
```

### Architecture

```
┌──────────┐  stdin (JSON-RPC)  ┌───────────┐   PTY    ┌──────┐
│ AI Agent  │ ◄──────────────────► MCP Server │ ◄───────► LLDB  │
└──────────┘  stdout             └───────────┘  fork+io └──────┘
```

- **MCP Server** communicates with the AI agent via stdin/stdout with automatic transport format detection
- **LldbSession** spawns LLDB in a child process over a PTY (pseudo-terminal) for bidirectional I/O
- **McpTools** maps all 29 tools to LLDB commands with unified error handling

## References

Built on the following technologies:

- [Model Context Protocol (MCP)](https://modelcontextprotocol.io) — AI tool integration protocol by Anthropic
- [LLDB Debugger](https://lldb.llvm.org) — The LLVM project debugger
- [Boost C++ Libraries](https://www.boost.org) — High-quality C++ library collection
- [OpenCode](https://github.com/anomalyco/opencode) — MCP-capable AI coding assistant

## License

MIT
