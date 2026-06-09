#include "mcp_tools.hpp"
#include "mcp_server.hpp"

#include <sstream>
#include <regex>
#include <cstdlib>
#include <cstdio>
#include <array>
#include <memory>

#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <boost/filesystem.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

namespace lldb_mcp {

LldbSession* McpTools::getSession(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        throw std::runtime_error("No active LLDB session with ID: " + session_id);
    }
    return it->second.get();
}

json::value McpTools::handleToolCall(const std::string& tool_name, const json::value& arguments) {
    if (tool_name == "lldb_start") return lldbStart(arguments);
    if (tool_name == "lldb_load") return lldbLoad(arguments);
    if (tool_name == "lldb_command") return lldbCommand(arguments);
    if (tool_name == "lldb_terminate") return lldbTerminate(arguments);
    if (tool_name == "lldb_list_sessions") return lldbListSessions(arguments);
    if (tool_name == "lldb_attach") return lldbAttach(arguments);
    if (tool_name == "lldb_load_core") return lldbLoadCore(arguments);
    if (tool_name == "lldb_set_breakpoint") return lldbSetBreakpoint(arguments);
    if (tool_name == "lldb_continue") return lldbContinue(arguments);
    if (tool_name == "lldb_step") return lldbStep(arguments);
    if (tool_name == "lldb_next") return lldbNext(arguments);
    if (tool_name == "lldb_finish") return lldbFinish(arguments);
    if (tool_name == "lldb_backtrace") return lldbBacktrace(arguments);
    if (tool_name == "lldb_print") return lldbPrint(arguments);
    if (tool_name == "lldb_examine") return lldbExamine(arguments);
    if (tool_name == "lldb_info_registers") return lldbInfoRegisters(arguments);
    if (tool_name == "lldb_watchpoint") return lldbWatchpoint(arguments);
    if (tool_name == "lldb_frame_info") return lldbFrameInfo(arguments);
    if (tool_name == "lldb_run") return lldbRun(arguments);
    if (tool_name == "lldb_kill") return lldbKill(arguments);
    if (tool_name == "lldb_thread_list") return lldbThreadList(arguments);
    if (tool_name == "lldb_thread_select") return lldbThreadSelect(arguments);
    if (tool_name == "lldb_breakpoint_list") return lldbBreakpointList(arguments);
    if (tool_name == "lldb_breakpoint_delete") return lldbBreakpointDelete(arguments);
    if (tool_name == "lldb_expression") return lldbExpression(arguments);
    if (tool_name == "lldb_process_info") return lldbProcessInfo(arguments);
    if (tool_name == "lldb_disassemble") return lldbDisassemble(arguments);
    if (tool_name == "lldb_help") return lldbHelp(arguments);

    throw std::runtime_error("Unknown tool: " + tool_name);
}

static json::value makeError(const std::string& msg) {
    json::array content;
    content.push_back(json::object{{"type", "text"}, {"text", "Error: " + msg}});
    return json::object{{"content", content}, {"isError", true}};
}

static std::string generateUuid() {
    static boost::uuids::random_generator gen;
    boost::uuids::uuid u = gen();
    return boost::uuids::to_string(u);
}

static std::pair<bool, std::string> verifyLldbPath(const std::string& path) {
    pid_t pid = fork();
    if (pid < 0) {
        return {false, "fork failed"};
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execlp(path.c_str(), path.c_str(), "--version", nullptr);
        _exit(127);
    }

    int status;
    for (int i = 0; i < 50; ++i) {
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            return {WIFEXITED(status) && WEXITSTATUS(status) == 0, ""};
        }
        usleep(100000);
    }
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return {false, "Timeout verifying LLDB path"};
}

static std::string getCurrentDir() {
    char buf[4096];
    if (getcwd(buf, sizeof(buf))) {
        return std::string(buf);
    }
    return ".";
}

static std::string joinResult(const std::string& prefix, const std::string& output) {
    return prefix + "\n\n" + output;
}

json::value McpTools::lldbStart(const json::value& args) {
    try {
        std::string lldb_path = getStr(args, "lldb_path", "lldb");
        std::string working_dir = getStr(args, "working_dir");
        if (working_dir.empty()) {
            working_dir = getCurrentDir();
        }

        auto [ok, error_msg] = verifyLldbPath(lldb_path);
        if (!ok) {
            std::string err = error_msg.empty()
                ? "Invalid lldb path: " + lldb_path
                : "Failed to start LLDB: " + error_msg;
            return makeError(err);
        }

        std::string session_id = generateUuid();
        auto session = std::make_unique<LldbSession>(
            session_id, lldb_path, working_dir);

        std::string output;
        try {
            output = session->start();
        }
        catch (const std::exception& e) {
            return makeError("Failed to start LLDB: " + std::string(e.what()));
        }

        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            sessions_[session_id] = std::move(session);
        }

        std::ostringstream oss;
        oss << "LLDB session started with ID: " << session_id
            << "\n\nOutput:\n" << output;
        return json::object{
            {"content", json::array{json::object{
                {"type", "text"}, {"text", oss.str()}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to start LLDB: ") + e.what());
    }
}

json::value McpTools::lldbLoad(const json::value& args) {
    try {
        std::string session_id = getStr(args, "session_id");
        std::string program = getStr(args, "program");
        auto* session = getSession(session_id);

        if (!boost::filesystem::path(program).is_absolute()) {
            program = (boost::filesystem::path(session->workingDir()) / program).string();
        }

        std::string output = session->executeCommand("file \"" + program + "\"");

        if (args.is_object() && args.as_object().contains("arguments")) {
            auto& args_arr = args.as_object().at("arguments").as_array();
            std::ostringstream args_oss;
            for (const auto& arg : args_arr) {
                args_oss << " \"" << json::value_to<std::string>(arg) << "\"";
            }
            std::string args_output = session->executeCommand(
                "settings set -- target.run-args" + args_oss.str());
            output += "\n" + args_output;
        }

        session->setTarget(program);

        return json::object{
            {"content", json::array{json::object{
                {"type", "text"},
                {"text", joinResult("Program loaded: " + program, output)}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to load program: ") + e.what());
    }
}

json::value McpTools::lldbCommand(const json::value& args) {
    try {
        std::string session_id = getStr(args, "session_id");
        std::string command = getStr(args, "command");
        auto* session = getSession(session_id);
        std::string output = session->executeCommand(command);
        return json::object{
            {"content", json::array{json::object{
                {"type", "text"},
                {"text", joinResult("Command: " + command, output)}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to execute command: ") + e.what());
    }
}

json::value McpTools::lldbTerminate(const json::value& args) {
    try {
        std::string session_id = getStr(args, "session_id");
        LldbSession* session = nullptr;
        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            auto it = sessions_.find(session_id);
            if (it == sessions_.end()) {
                return makeError("No active LLDB session with ID: " + session_id);
            }
            session = it->second.get();
        }

        session->cleanup();

        {
            std::lock_guard<std::mutex> lock(sessions_mutex_);
            sessions_.erase(session_id);
        }

        return json::object{
            {"content", json::array{json::object{
                {"type", "text"},
                {"text", "LLDB session terminated: " + session_id}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to terminate session: ") + e.what());
    }
}

json::value McpTools::lldbListSessions(const json::value& /*args*/) {
    try {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        json::array session_list;
        for (const auto& [id, session] : sessions_) {
            session_list.push_back(json::object{
                {"id", id},
                {"target", session->target().empty() ? "No program loaded" : session->target()},
                {"working_dir", session->workingDir()}
            });
        }

        std::ostringstream oss;
        oss << "Active LLDB Sessions (" << sessions_.size() << "):\n\n"
            << json::serialize(session_list);
        return json::object{
            {"content", json::array{json::object{
                {"type", "text"}, {"text", oss.str()}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(e.what());
    }
}

json::value McpTools::lldbAttach(const json::value& args) {
    try {
        std::string session_id = getStr(args, "session_id");
        int pid = getInt(args, "pid");
        auto* session = getSession(session_id);
        std::string output = session->executeCommand("process attach -p " + std::to_string(pid));
        return json::object{
            {"content", json::array{json::object{
                {"type", "text"},
                {"text", joinResult("Attached to process " + std::to_string(pid), output)}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to attach to process: ") + e.what());
    }
}

json::value McpTools::lldbLoadCore(const json::value& args) {
    try {
        std::string session_id = getStr(args, "session_id");
        std::string program = getStr(args, "program");
        std::string core_path = getStr(args, "core_path");
        auto* session = getSession(session_id);

        std::string file_output = session->executeCommand("file \"" + program + "\"");
        std::string core_output = session->executeCommand("target core \"" + core_path + "\"");
        std::string bt_output = session->executeCommand("bt");

        std::ostringstream oss;
        oss << "Core file loaded: " << core_path
            << "\n\nOutput:\n" << file_output << "\n" << core_output
            << "\n\nBacktrace:\n" << bt_output;
        return json::object{
            {"content", json::array{json::object{
                {"type", "text"}, {"text", oss.str()}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to load core file: ") + e.what());
    }
}

json::value McpTools::lldbSetBreakpoint(const json::value& args) {
    try {
        std::string session_id = getStr(args, "session_id");
        std::string location = getStr(args, "location");
        auto* session = getSession(session_id);

        std::string output = session->executeCommand("breakpoint set --name \"" + location + "\"");

        std::string condition = getStr(args, "condition");
        if (!condition.empty()) {
            std::regex bp_regex(R"(Breakpoint (\d+):)");
            std::smatch match;
            if (std::regex_search(output, match, bp_regex)) {
                std::string bp_num = match[1];
                std::string cond_output = session->executeCommand(
                    "breakpoint modify -c \"" + condition + "\" " + bp_num);
                output += "\n" + cond_output;
            }
        }

        std::ostringstream oss;
        oss << "Breakpoint set at: " << location;
        if (!condition.empty()) {
            oss << " with condition: " << condition;
        }
        oss << "\n\nOutput:\n" << output;
        return json::object{
            {"content", json::array{json::object{
                {"type", "text"}, {"text", oss.str()}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to set breakpoint: ") + e.what());
    }
}

json::value McpTools::lldbContinue(const json::value& args) {
    try {
        auto* session = getSession(getStr(args, "session_id"));
        std::string output = session->executeCommand("continue");
        return json::object{
            {"content", json::array{json::object{
                {"type", "text"},
                {"text", joinResult("Continued execution", output)}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to continue: ") + e.what());
    }
}

json::value McpTools::lldbStep(const json::value& args) {
    try {
        auto* session = getSession(getStr(args, "session_id"));
        bool instructions = getBool(args, "instructions", false);
        std::string output = session->executeCommand(instructions ? "si" : "s");
        return json::object{
            {"content", json::array{json::object{
                {"type", "text"},
                {"text", joinResult("Stepped " + std::string(instructions ? "instruction" : "line"), output)}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to step: ") + e.what());
    }
}

json::value McpTools::lldbNext(const json::value& args) {
    try {
        auto* session = getSession(getStr(args, "session_id"));
        bool instructions = getBool(args, "instructions", false);
        std::string output = session->executeCommand(instructions ? "ni" : "n");
        return json::object{
            {"content", json::array{json::object{
                {"type", "text"},
                {"text", joinResult("Stepped over " + std::string(instructions ? "instruction" : "function call"), output)}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to step over: ") + e.what());
    }
}

json::value McpTools::lldbFinish(const json::value& args) {
    try {
        auto* session = getSession(getStr(args, "session_id"));
        std::string output = session->executeCommand("finish");
        return json::object{
            {"content", json::array{json::object{
                {"type", "text"},
                {"text", joinResult("Finished current function", output)}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to finish: ") + e.what());
    }
}

json::value McpTools::lldbBacktrace(const json::value& args) {
    try {
        auto* session = getSession(getStr(args, "session_id"));
        std::string cmd = "bt";
        bool full = getBool(args, "full", false);
        if (full) cmd += " all";
        if (args.is_object() && args.as_object().contains("limit")) {
            cmd += " -c " + std::to_string(getInt(args, "limit"));
        }
        std::string output = session->executeCommand(cmd);

        std::string desc = "Backtrace";
        if (full) desc += " (full)";
        if (args.is_object() && args.as_object().contains("limit")) {
            desc += " (limit: " + std::to_string(getInt(args, "limit")) + ")";
        }
        return json::object{
            {"content", json::array{json::object{
                {"type", "text"}, {"text", desc + ":\n\n" + output}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to get backtrace: ") + e.what());
    }
}

json::value McpTools::lldbPrint(const json::value& args) {
    try {
        auto* session = getSession(getStr(args, "session_id"));
        std::string expression = getStr(args, "expression");
        std::string output = session->executeCommand("p " + expression);
        return json::object{
            {"content", json::array{json::object{
                {"type", "text"},
                {"text", joinResult("Print " + expression, output)}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to print: ") + e.what());
    }
}

json::value McpTools::lldbExamine(const json::value& args) {
    try {
        auto* session = getSession(getStr(args, "session_id"));
        std::string expression = getStr(args, "expression");
        std::string format = getStr(args, "format", "x");
        int count = getInt(args, "count", 1);

        static std::unordered_map<std::string, std::string> format_map = {
            {"x", "x"}, {"d", "d"}, {"u", "u"}, {"o", "o"},
            {"t", "t"}, {"i", "i"}, {"c", "c"}, {"f", "f"}, {"s", "s"}
        };

        auto it = format_map.find(format);
        std::string lldb_format = (it != format_map.end()) ? it->second : "x";

        std::string cmd = "memory read -f " + lldb_format + " -c " +
                          std::to_string(count) + " " + expression;
        std::string output = session->executeCommand(cmd);

        std::ostringstream oss;
        oss << "Examine " << expression << " (format: " << format
            << ", count: " << count << "):\n\n" << output;
        return json::object{
            {"content", json::array{json::object{
                {"type", "text"}, {"text", oss.str()}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to examine memory: ") + e.what());
    }
}

json::value McpTools::lldbInfoRegisters(const json::value& args) {
    try {
        auto* session = getSession(getStr(args, "session_id"));
        std::string cmd = "register read";
        std::string reg_name = getStr(args, "register");
        if (!reg_name.empty()) cmd += " " + reg_name;
        std::string output = session->executeCommand(cmd);

        std::string desc = "Register info";
        if (!reg_name.empty()) desc += " for " + reg_name;
        return json::object{
            {"content", json::array{json::object{
                {"type", "text"}, {"text", desc + ":\n\n" + output}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to get register info: ") + e.what());
    }
}

json::value McpTools::lldbWatchpoint(const json::value& args) {
    try {
        auto* session = getSession(getStr(args, "session_id"));
        std::string expression = getStr(args, "expression");
        std::string watch_type = getStr(args, "watch_type", "write");

        static std::unordered_map<std::string, std::string> watch_map = {
            {"read", "r"}, {"write", "w"}, {"read_write", "rw"}
        };

        auto it = watch_map.find(watch_type);
        std::string option = (it != watch_map.end()) ? it->second : "w";

        std::string cmd = "watchpoint set expression -- " + expression + " -w " + option;
        std::string output = session->executeCommand(cmd);

        std::ostringstream oss;
        oss << "Watchpoint set on " << expression << " (type: " << watch_type
            << ")\n\nOutput:\n" << output;
        return json::object{
            {"content", json::array{json::object{
                {"type", "text"}, {"text", oss.str()}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to set watchpoint: ") + e.what());
    }
}

json::value McpTools::lldbFrameInfo(const json::value& args) {
    try {
        auto* session = getSession(getStr(args, "session_id"));
        int frame_index = getInt(args, "frame_index", 0);

        std::string frame_output = session->executeCommand(
            "frame select " + std::to_string(frame_index));
        std::string vars_output = session->executeCommand("frame variable");
        std::string source_output = session->executeCommand("source list");

        std::ostringstream oss;
        oss << "Frame " << frame_index << " info:\n\n"
            << frame_output << "\n\nVariables:\n"
            << vars_output << "\n\nSource:\n" << source_output;
        return json::object{
            {"content", json::array{json::object{
                {"type", "text"}, {"text", oss.str()}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to get frame info: ") + e.what());
    }
}

json::value McpTools::lldbRun(const json::value& args) {
    try {
        auto* session = getSession(getStr(args, "session_id"));
        std::string output = session->executeCommand("run");
        return json::object{
            {"content", json::array{json::object{
                {"type", "text"},
                {"text", joinResult("Running program", output)}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to run: ") + e.what());
    }
}

json::value McpTools::lldbKill(const json::value& args) {
    try {
        auto* session = getSession(getStr(args, "session_id"));
        std::string output = session->executeCommand("process kill");
        return json::object{
            {"content", json::array{json::object{
                {"type", "text"},
                {"text", joinResult("Killed process", output)}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to kill: ") + e.what());
    }
}

json::value McpTools::lldbThreadList(const json::value& args) {
    try {
        auto* session = getSession(getStr(args, "session_id"));
        std::string output = session->executeCommand("thread list");
        return json::object{
            {"content", json::array{json::object{
                {"type", "text"},
                {"text", "Thread list:\n\n" + output}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to list threads: ") + e.what());
    }
}

json::value McpTools::lldbThreadSelect(const json::value& args) {
    try {
        auto* session = getSession(getStr(args, "session_id"));
        int thread_id = getInt(args, "thread_id");
        std::string output = session->executeCommand(
            "thread select " + std::to_string(thread_id));
        std::string bt_output = session->executeCommand("bt");

        std::ostringstream oss;
        oss << "Selected thread " << thread_id
            << "\n\nOutput:\n" << output << "\n\nBacktrace:\n" << bt_output;
        return json::object{
            {"content", json::array{json::object{
                {"type", "text"}, {"text", oss.str()}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to select thread: ") + e.what());
    }
}

json::value McpTools::lldbBreakpointList(const json::value& args) {
    try {
        auto* session = getSession(getStr(args, "session_id"));
        std::string output = session->executeCommand("breakpoint list");
        return json::object{
            {"content", json::array{json::object{
                {"type", "text"},
                {"text", "Breakpoint list:\n\n" + output}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to list breakpoints: ") + e.what());
    }
}

json::value McpTools::lldbBreakpointDelete(const json::value& args) {
    try {
        auto* session = getSession(getStr(args, "session_id"));
        int breakpoint_id = getInt(args, "breakpoint_id");
        std::string output = session->executeCommand(
            "breakpoint delete " + std::to_string(breakpoint_id));
        return json::object{
            {"content", json::array{json::object{
                {"type", "text"},
                {"text", "Deleted breakpoint " + std::to_string(breakpoint_id)
                         + "\n\nOutput:\n" + output}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to delete breakpoint: ") + e.what());
    }
}

json::value McpTools::lldbExpression(const json::value& args) {
    try {
        auto* session = getSession(getStr(args, "session_id"));
        std::string expression = getStr(args, "expression");
        std::string output = session->executeCommand("expression -- " + expression);
        return json::object{
            {"content", json::array{json::object{
                {"type", "text"},
                {"text", joinResult("Expression evaluation: " + expression, output)}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to evaluate expression: ") + e.what());
    }
}

json::value McpTools::lldbProcessInfo(const json::value& args) {
    try {
        auto* session = getSession(getStr(args, "session_id"));
        std::string output = session->executeCommand("process status");
        std::string detail = session->executeCommand("process info");
        return json::object{
            {"content", json::array{json::object{
                {"type", "text"},
                {"text", "Process information:\n\n" + output + "\n\nDetails:\n" + detail}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to get process info: ") + e.what());
    }
}

json::value McpTools::lldbDisassemble(const json::value& args) {
    try {
        auto* session = getSession(getStr(args, "session_id"));
        int count = getInt(args, "count", 10);
        std::string cmd = "disassemble";
        std::string location = getStr(args, "location");
        if (!location.empty()) {
            cmd += " --name " + location;
        }
        cmd += " -c " + std::to_string(count);

        std::string output = session->executeCommand(cmd);

        std::string desc = "Disassembly";
        if (!location.empty()) desc += " of " + location;
        return json::object{
            {"content", json::array{json::object{
                {"type", "text"}, {"text", desc + ":\n\n" + output}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to disassemble: ") + e.what());
    }
}

json::value McpTools::lldbHelp(const json::value& args) {
    try {
        auto* session = getSession(getStr(args, "session_id"));
        std::string cmd_name = getStr(args, "command");
        std::string cmd = cmd_name.empty() ? "help" : "help " + cmd_name;
        std::string output = session->executeCommand(cmd);

        std::string desc = cmd_name.empty()
            ? "LLDB help overview"
            : "Help for '" + cmd_name + "'";
        return json::object{
            {"content", json::array{json::object{
                {"type", "text"}, {"text", desc + ":\n\n" + output}
            }}}
        };
    }
    catch (const std::exception& e) {
        return makeError(std::string("Failed to get help: ") + e.what());
    }
}

// Helper to build tool input schemas
static json::value simpleSchema() {
    return json::object{{"type", "object"}, {"properties", json::object()}};
}

static json::value sessionSchema() {
    return json::object{
        {"type", "object"},
        {"properties", json::object{
            {"session_id", json::object{{"type", "string"}}}
        }},
        {"required", json::array{"session_id"}}
    };
}

void McpTools::registerAllTools(McpServer& server) {
    server.registerTool("lldb_start", "Start a new LLDB session",
        json::object{
            {"type", "object"},
            {"properties", json::object{
                {"lldb_path", json::object{{"type", "string"}, {"description", "Path to LLDB binary (default: lldb)"}}},
                {"working_dir", json::object{{"type", "string"}, {"description", "Working directory"}}}
            }}
        });

    server.registerTool("lldb_load", "Load a program into LLDB",
        json::object{
            {"type", "object"},
            {"properties", json::object{
                {"session_id", json::object{{"type", "string"}}},
                {"program", json::object{{"type", "string"}}},
                {"arguments", json::object{{"type", "array"}, {"items", json::object{{"type", "string"}}}}}
            }},
            {"required", json::array{"session_id", "program"}}
        });

    server.registerTool("lldb_command", "Execute an LLDB command",
        json::object{
            {"type", "object"},
            {"properties", json::object{
                {"session_id", json::object{{"type", "string"}}},
                {"command", json::object{{"type", "string"}}}
            }},
            {"required", json::array{"session_id", "command"}}
        });

    server.registerTool("lldb_terminate", "Terminate an LLDB session",
        sessionSchema());

    server.registerTool("lldb_list_sessions", "List all active LLDB sessions",
        simpleSchema());

    server.registerTool("lldb_attach", "Attach to a running process",
        json::object{
            {"type", "object"},
            {"properties", json::object{
                {"session_id", json::object{{"type", "string"}}},
                {"pid", json::object{{"type", "integer"}}}
            }},
            {"required", json::array{"session_id", "pid"}}
        });

    server.registerTool("lldb_load_core", "Load a core dump file",
        json::object{
            {"type", "object"},
            {"properties", json::object{
                {"session_id", json::object{{"type", "string"}}},
                {"program", json::object{{"type", "string"}}},
                {"core_path", json::object{{"type", "string"}}}
            }},
            {"required", json::array{"session_id", "program", "core_path"}}
        });

    server.registerTool("lldb_set_breakpoint", "Set a breakpoint",
        json::object{
            {"type", "object"},
            {"properties", json::object{
                {"session_id", json::object{{"type", "string"}}},
                {"location", json::object{{"type", "string"}}},
                {"condition", json::object{{"type", "string"}}}
            }},
            {"required", json::array{"session_id", "location"}}
        });

    server.registerTool("lldb_continue", "Continue program execution", sessionSchema());
    server.registerTool("lldb_step", "Step program execution",
        json::object{
            {"type", "object"},
            {"properties", json::object{
                {"session_id", json::object{{"type", "string"}}},
                {"instructions", json::object{{"type", "boolean"}}}
            }},
            {"required", json::array{"session_id"}}
        });

    server.registerTool("lldb_next", "Step over function calls",
        json::object{
            {"type", "object"},
            {"properties", json::object{
                {"session_id", json::object{{"type", "string"}}},
                {"instructions", json::object{{"type", "boolean"}}}
            }},
            {"required", json::array{"session_id"}}
        });

    server.registerTool("lldb_finish", "Execute until the current function returns", sessionSchema());

    server.registerTool("lldb_backtrace", "Show call stack",
        json::object{
            {"type", "object"},
            {"properties", json::object{
                {"session_id", json::object{{"type", "string"}}},
                {"full", json::object{{"type", "boolean"}}},
                {"limit", json::object{{"type", "integer"}}}
            }},
            {"required", json::array{"session_id"}}
        });

    server.registerTool("lldb_print", "Print value of expression",
        json::object{
            {"type", "object"},
            {"properties", json::object{
                {"session_id", json::object{{"type", "string"}}},
                {"expression", json::object{{"type", "string"}}}
            }},
            {"required", json::array{"session_id", "expression"}}
        });

    server.registerTool("lldb_examine", "Examine memory",
        json::object{
            {"type", "object"},
            {"properties", json::object{
                {"session_id", json::object{{"type", "string"}}},
                {"expression", json::object{{"type", "string"}}},
                {"format", json::object{{"type", "string"}}},
                {"count", json::object{{"type", "integer"}}}
            }},
            {"required", json::array{"session_id", "expression"}}
        });

    server.registerTool("lldb_info_registers", "Display registers",
        json::object{
            {"type", "object"},
            {"properties", json::object{
                {"session_id", json::object{{"type", "string"}}},
                {"register", json::object{{"type", "string"}}}
            }},
            {"required", json::array{"session_id"}}
        });

    server.registerTool("lldb_watchpoint", "Set a watchpoint on a variable or memory address",
        json::object{
            {"type", "object"},
            {"properties", json::object{
                {"session_id", json::object{{"type", "string"}}},
                {"expression", json::object{{"type", "string"}}},
                {"watch_type", json::object{{"type", "string"}, {"enum", json::array{"read", "write", "read_write"}}}}
            }},
            {"required", json::array{"session_id", "expression"}}
        });

    server.registerTool("lldb_frame_info", "Get detailed information about a stack frame",
        json::object{
            {"type", "object"},
            {"properties", json::object{
                {"session_id", json::object{{"type", "string"}}},
                {"frame_index", json::object{{"type", "integer"}}}
            }},
            {"required", json::array{"session_id"}}
        });

    server.registerTool("lldb_run", "Run the loaded program", sessionSchema());
    server.registerTool("lldb_kill", "Kill the running process", sessionSchema());
    server.registerTool("lldb_thread_list", "List all threads in the current process", sessionSchema());

    server.registerTool("lldb_thread_select", "Select a specific thread",
        json::object{
            {"type", "object"},
            {"properties", json::object{
                {"session_id", json::object{{"type", "string"}}},
                {"thread_id", json::object{{"type", "integer"}}}
            }},
            {"required", json::array{"session_id", "thread_id"}}
        });

    server.registerTool("lldb_breakpoint_list", "List all breakpoints", sessionSchema());

    server.registerTool("lldb_breakpoint_delete", "Delete a breakpoint",
        json::object{
            {"type", "object"},
            {"properties", json::object{
                {"session_id", json::object{{"type", "string"}}},
                {"breakpoint_id", json::object{{"type", "integer"}}}
            }},
            {"required", json::array{"session_id", "breakpoint_id"}}
        });

    server.registerTool("lldb_expression", "Evaluate an expression in the current frame",
        json::object{
            {"type", "object"},
            {"properties", json::object{
                {"session_id", json::object{{"type", "string"}}},
                {"expression", json::object{{"type", "string"}}}
            }},
            {"required", json::array{"session_id", "expression"}}
        });

    server.registerTool("lldb_process_info", "Get information about the current process", sessionSchema());

    server.registerTool("lldb_disassemble", "Disassemble code",
        json::object{
            {"type", "object"},
            {"properties", json::object{
                {"session_id", json::object{{"type", "string"}}},
                {"location", json::object{{"type", "string"}}},
                {"count", json::object{{"type", "integer"}}}
            }},
            {"required", json::array{"session_id"}}
        });

    server.registerTool("lldb_help", "Get help for LLDB commands",
        json::object{
            {"type", "object"},
            {"properties", json::object{
                {"session_id", json::object{{"type", "string"}}},
                {"command", json::object{{"type", "string"}}}
            }},
            {"required", json::array{"session_id"}}
        });
}

} // namespace lldb_mcp
