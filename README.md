# lldb-mcp

<details open>
<summary>🇨🇳 中文</summary>

LLDB MCP Server — 将 LLDB 调试器封装为 [Model Context Protocol (MCP)](https://modelcontextprotocol.io) 服务器，让 AI 助手（如 Claude、OpenCode 等）通过标准 MCP 接口直接对本地程序进行断点调试、单步执行、变量查看等操作。

## 功能概览

| 分类 | 工具 | 说明 |
|------|------|------|
| 会话管理 | `lldb_start` | 启动一个新的 LLDB 会话 |
| | `lldb_list_sessions` | 列出所有活跃会话 |
| | `lldb_terminate` | 终止一个会话 |
| 程序控制 | `lldb_load` | 加载被调试程序 |
| | `lldb_attach` | 附加到运行中的进程 |
| | `lldb_load_core` | 加载 core dump 文件 |
| | `lldb_run` | 运行加载的程序 |
| | `lldb_continue` | 继续执行 |
| | `lldb_kill` | 结束进程 |
| 断点 | `lldb_set_breakpoint` | 设置断点（支持条件） |
| | `lldb_breakpoint_list` | 列出所有断点 |
| | `lldb_breakpoint_delete` | 删除断点 |
| | `lldb_watchpoint` | 设置内存观察点 |
| 单步调试 | `lldb_step` | 单步执行（进入函数） |
| | `lldb_next` | 单步执行（跳过函数） |
| | `lldb_finish` | 执行到当前函数返回 |
| 查看/检查 | `lldb_print` | 打印变量/表达式值 |
| | `lldb_expression` | 在当前帧求值表达式 |
| | `lldb_examine` | 检查内存 |
| | `lldb_info_registers` | 查看寄存器 |
| | `lldb_disassemble` | 反汇编 |
| | `lldb_process_info` | 查看进程信息 |
| 堆栈/线程 | `lldb_backtrace` | 显示调用栈 |
| | `lldb_frame_info` | 查看栈帧详情（含局部变量） |
| | `lldb_thread_list` | 列出所有线程 |
| | `lldb_thread_select` | 切换线程 |
| 其他 | `lldb_command` | 执行任意 LLDB 命令 |
| | `lldb_help` | 查看 LLDB 帮助 |

共 **29 个工具**，完整覆盖 LLDB 常用调试操作。

## 构建

### 依赖

- **CMake** >= 3.16
- **Boost** >= 1.79 (需 `filesystem` 和 `json` 组件)
- **C++17** 编译器
- **LLDB** (运行时需要，macOS 自带 / Linux 需安装)

### 编译

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

生成的可执行文件为 `build/lldb-mcp`。

### 安装 Boost (macOS)

```bash
brew install boost
```

### 安装 Boost (Linux)

```bash
apt install libboost-filesystem-dev libboost-json-dev
# 或
yum install boost-devel
```

## 使用

### 命令行

```bash
./lldb-mcp [--debug]
```

- `--debug` — 打印调试日志到 stderr（不影响 MCP 协议通信）
- `-h` / `--help` — 打印帮助信息

程序会从 stdin 读取 JSON-RPC 消息，通过 stdout 响应。支持两种 MCP 传输模式：
- **新行分隔 JSON**（newline-delimited）
- **HTTP 风格 Content-Length 头**

### 配置为 MCP 服务

在 AI 助手的 MCP 配置中添加（以 OpenCode 为例，`opencode.json`）：

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

## 使用示例

通过 MCP 接口调试 Fibonacci 程序：

```
1. lldb_start                          # 启动 LLDB 会话
2. lldb_load(session_id, "test_rec")   # 加载程序
3. lldb_set_breakpoint(session_id, "fib")  # 在 fib 函数设断点
4. lldb_run(session_id)                # 运行，命中断点
5. lldb_print(session_id, "n")         # 打印参数 n
6. lldb_backtrace(session_id)          # 查看调用栈
7. lldb_step(session_id)               # 单步进入递归
8. lldb_continue(session_id)           # 继续执行
9. lldb_terminate(session_id)          # 结束会话
```

## 项目结构

```
lldb-mcp/
├── CMakeLists.txt          # CMake 构建配置
├── src/
│   ├── main.cpp            # 入口，命令行解析，启动 MCP Server
│   ├── lldb_session.hpp/cpp # LLDB 会话管理（PTY fork + 命令执行）
│   ├── mcp_server.hpp/cpp   # MCP 协议实现（JSON-RPC, 传输检测）
│   └── mcp_tools.hpp/cpp    # 所有工具注册与调度
└── tests/
    ├── test_rec.c          # 测试程序：递归 Fibonacci
    ├── test_bug.c          # 测试程序：含 bug 的 factorial
    └── test_args.c         # 测试程序：命令行参数打印
```

### 架构说明

```
┌──────────┐  stdin (JSON-RPC)  ┌───────────┐   PTY    ┌──────┐
│ AI 助手   │ ◄──────────────────► MCP Server │ ◄───────► LLDB  │
└──────────┘  stdout             └───────────┘  fork+io └──────┘
```

- **MCP Server** 通过 stdin/stdout 与 AI 助手通信，支持自动检测传输格式
- **LldbSession** 通过 PTY (pseudo-terminal) fork 子进程运行 LLDB，实现双向 IO
- **McpTools** 将 29 个工具映射到 LLDB 命令，统一错误处理

## 引用

本项目基于以下技术构建：

- [Model Context Protocol (MCP)](https://modelcontextprotocol.io) — Anthropic 提出的 AI 工具集成协议
- [LLDB Debugger](https://lldb.llvm.org) — LLVM 项目的调试器
- [Boost C++ Libraries](https://www.boost.org) — 高质量的 C++ 库集合
- [OpenCode](https://github.com/anomalyco/opencode) — 支持 MCP 扩展的 AI 编程助手

</details>

<details>
<summary>🇬🇧 English</summary>

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

</details>

## License

MIT
