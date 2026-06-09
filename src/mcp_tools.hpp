#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <boost/json.hpp>

#include "lldb_session.hpp"

namespace lldb_mcp {

namespace json = boost::json;

class McpTools {
public:
    McpTools() = default;

    json::value handleToolCall(const std::string& tool_name, const json::value& arguments);
    void registerAllTools(class McpServer& server);

private:
    LldbSession* getSession(const std::string& session_id);

    json::value lldbStart(const json::value& args);
    json::value lldbLoad(const json::value& args);
    json::value lldbCommand(const json::value& args);
    json::value lldbTerminate(const json::value& args);
    json::value lldbListSessions(const json::value& args);
    json::value lldbAttach(const json::value& args);
    json::value lldbLoadCore(const json::value& args);
    json::value lldbSetBreakpoint(const json::value& args);
    json::value lldbContinue(const json::value& args);
    json::value lldbStep(const json::value& args);
    json::value lldbNext(const json::value& args);
    json::value lldbFinish(const json::value& args);
    json::value lldbBacktrace(const json::value& args);
    json::value lldbPrint(const json::value& args);
    json::value lldbExamine(const json::value& args);
    json::value lldbInfoRegisters(const json::value& args);
    json::value lldbWatchpoint(const json::value& args);
    json::value lldbFrameInfo(const json::value& args);
    json::value lldbRun(const json::value& args);
    json::value lldbKill(const json::value& args);
    json::value lldbThreadList(const json::value& args);
    json::value lldbThreadSelect(const json::value& args);
    json::value lldbBreakpointList(const json::value& args);
    json::value lldbBreakpointDelete(const json::value& args);
    json::value lldbExpression(const json::value& args);
    json::value lldbProcessInfo(const json::value& args);
    json::value lldbDisassemble(const json::value& args);
    json::value lldbHelp(const json::value& args);

    std::mutex sessions_mutex_;
    std::unordered_map<std::string, std::unique_ptr<LldbSession>> sessions_;
};

// Helper: get string value from JSON object with default
inline std::string getStr(const json::value& v, const char* key, const std::string& def = "") {
    if (!v.is_object()) return def;
    auto& o = v.as_object();
    auto it = o.find(key);
    if (it == o.end()) return def;
    return json::value_to<std::string>(it->value());
}

// Helper: get int value from JSON object with default
inline int getInt(const json::value& v, const char* key, int def = 0) {
    if (!v.is_object()) return def;
    auto& o = v.as_object();
    auto it = o.find(key);
    if (it == o.end()) return def;
    return static_cast<int>(it->value().as_int64());
}

// Helper: get bool value from JSON object with default
inline bool getBool(const json::value& v, const char* key, bool def = false) {
    if (!v.is_object()) return def;
    auto& o = v.as_object();
    auto it = o.find(key);
    if (it == o.end()) return def;
    return it->value().as_bool();
}

} // namespace lldb_mcp
