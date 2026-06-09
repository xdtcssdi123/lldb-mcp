#pragma once

#include <string>
#include <functional>
#include <memory>
#include <boost/json.hpp>

namespace lldb_mcp {

namespace json = boost::json;

using ToolHandler = std::function<json::value(const std::string& tool_name, const json::value& params)>;

class McpServer {
public:
    explicit McpServer(const std::string& name, const std::string& version = "1.0.0");

    void setToolHandler(ToolHandler handler);
    void registerTool(const std::string& name,
                      const std::string& description,
                      const json::value& input_schema);
    void run();

private:
    void handleRequest(const json::value& request);
    void sendResponse(const json::value& response);
    json::value readMessage();
    json::value handleInitialize(const json::value& params);
    json::value handleToolsList();
    json::value handleToolsCall(const json::value& params);

    std::string server_name_;
    std::string server_version_;
    json::value server_capabilities_;
    json::value tools_list_;
    ToolHandler tool_handler_;
    bool initialized_ = false;
    bool use_newline_delimited_ = false;
    bool transport_detected_ = false;
    std::string read_buffer_;
};

} // namespace lldb_mcp
