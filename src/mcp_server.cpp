#include "mcp_server.hpp"

#include <iostream>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <stdexcept>
#include <ctime>
#include <unistd.h>

namespace lldb_mcp {

McpServer::McpServer(const std::string& name, const std::string& version)
    : server_name_(name)
    , server_version_(version)
{
    server_capabilities_ = json::object{
        {"tools", json::object{{"listChanged", true}}}
    };
    tools_list_ = json::array();
}

void McpServer::setToolHandler(ToolHandler handler) {
    tool_handler_ = std::move(handler);
}

void McpServer::registerTool(const std::string& name,
                              const std::string& description,
                              const json::value& input_schema) {
    json::object tool;
    tool["name"] = name;
    tool["description"] = description;
    tool["inputSchema"] = input_schema;
    tools_list_.as_array().push_back(tool);
}

void McpServer::sendResponse(const json::value& response) {
    std::string body = json::serialize(response);
    std::ostringstream oss;
    if (use_newline_delimited_) {
        oss << body << "\n";
    } else {
        oss << "Content-Length: " << body.size() << "\r\n\r\n" << body;
    }
    std::string msg = oss.str();
    write(STDOUT_FILENO, msg.c_str(), msg.size());
}

static ssize_t readExact(int fd, void* buf, size_t n) {
    size_t remaining = n;
    char* p = static_cast<char*>(buf);
    while (remaining > 0) {
        ssize_t r = read(fd, p, remaining);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) {
            return n - remaining;
        }
        remaining -= r;
        p += r;
    }
    return n;
}

json::value McpServer::readMessage() {
    while (true) {
        if (transport_detected_) {
            if (use_newline_delimited_) {
                size_t nl = read_buffer_.find('\n');
                if (nl != std::string::npos) {
                    std::string line = read_buffer_.substr(0, nl);
                    read_buffer_.erase(0, nl + 1);
                    return json::parse(line);
                }
            } else {
                size_t header_end = read_buffer_.find("\r\n\r\n");
                if (header_end != std::string::npos) {
                    header_end += 4;
                    std::string header = read_buffer_.substr(0, header_end);
                    size_t content_length = 0;
                    size_t cl_pos = header.find("Content-Length: ");
                    if (cl_pos != std::string::npos) {
                        size_t start = cl_pos + 16;
                        size_t end = header.find('\r', start);
                        std::string cl_str = header.substr(start, end - start);
                        content_length = std::stoul(cl_str);
                    }
                    size_t total_needed = header_end + content_length;
                    if (read_buffer_.size() >= total_needed) {
                        std::string body = read_buffer_.substr(header_end, content_length);
                        read_buffer_.erase(0, total_needed);
                        return json::parse(body);
                    }
                }
            }
        }

        char buf[65536];
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf) - 1);
        if (n <= 0) {
            throw std::runtime_error("Failed to read from stdin");
        }
        read_buffer_.append(buf, n);

        if (!transport_detected_) {
            const char* p = read_buffer_.c_str();
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
            if (*p == '{') {
                use_newline_delimited_ = true;
            }
            transport_detected_ = true;
        }
    }
}

void McpServer::run() {
    setbuf(stdout, nullptr);

    while (true) {
        try {
            json::value request = readMessage();

            if (!request.as_object().contains("method")) {
                continue;
            }

            auto& obj = request.as_object();
            std::string method = json::value_to<std::string>(obj["method"]);
            bool has_id = obj.contains("id") && !obj["id"].is_null();
            json::value id;
            if (has_id) {
                id = obj["id"];
            }

            if (method == "initialize" && has_id) {
                json::value params;
                auto it = obj.find("params");
                if (it != obj.end()) params = it->value();
                json::value result = handleInitialize(params);
                json::value response = json::object{
                    {"jsonrpc", "2.0"},
                    {"id", id},
                    {"result", result}
                };
                sendResponse(response);
            }
            else if (method == "initialized") {
                initialized_ = true;
            }
            else if (method == "tools/list" && has_id) {
                json::value result = handleToolsList();
                json::value response = json::object{
                    {"jsonrpc", "2.0"},
                    {"id", id},
                    {"result", result}
                };
                sendResponse(response);
            }
            else if (method == "tools/call" && has_id) {
                json::value params;
                auto it = obj.find("params");
                if (it != obj.end()) params = it->value();
                json::value result = handleToolsCall(params);
                json::value response = json::object{
                    {"jsonrpc", "2.0"},
                    {"id", id},
                    {"result", result}
                };
                sendResponse(response);
            }
            else if (has_id) {
                json::value response = json::object{
                    {"jsonrpc", "2.0"},
                    {"id", id},
                    {"error", json::object{
                        {"code", -32601},
                        {"message", "Method not found: " + method}
                    }}
                };
                sendResponse(response);
            }
        }
        catch (const std::runtime_error& e) {
            std::string msg = e.what();
            if (msg.find("Failed to read from stdin") != std::string::npos ||
                msg.find("EOF on stdin") != std::string::npos) {
                break;
            }
            json::value response = json::object{
                {"jsonrpc", "2.0"},
                {"error", json::object{
                    {"code", -32603},
                    {"message", msg}
                }}
            };
            sendResponse(response);
        }
        catch (const std::exception& e) {
            json::value response = json::object{
                {"jsonrpc", "2.0"},
                {"error", json::object{
                    {"code", -32603},
                    {"message", std::string("Internal error: ") + e.what()}
                }}
            };
            sendResponse(response);
        }
    }
}

json::value McpServer::handleInitialize(const json::value& /*params*/) {
    return json::object{
        {"protocolVersion", "2024-11-05"},
        {"capabilities", server_capabilities_},
        {"serverInfo", json::object{
            {"name", server_name_},
            {"version", server_version_}
        }}
    };
}

json::value McpServer::handleToolsList() {
    return json::object{
        {"tools", tools_list_}
    };
}

json::value McpServer::handleToolsCall(const json::value& params) {
    auto& obj = params.as_object();
    std::string tool_name;
    auto name_it = obj.find("name");
    if (name_it != obj.end()) {
        tool_name = json::value_to<std::string>(name_it->value());
    }

    json::value arguments;
    auto args_it = obj.find("arguments");
    if (args_it != obj.end()) {
        arguments = args_it->value();
    } else {
        arguments = json::object();
    }

    if (!tool_handler_) {
        json::array content;
        content.push_back(json::object{{"type", "text"}, {"text", "Error: No tool handler registered"}});
        return json::object{{"content", content}, {"isError", true}};
    }

    try {
        json::value result = tool_handler_(tool_name, arguments);
        if (result.as_object().contains("isError") &&
            result.as_object().at("isError").as_bool()) {
            return result;
        }
        if (result.as_object().contains("content")) {
            return result;
        }
        json::array content;
        content.push_back(json::object{
            {"type", "text"},
            {"text", json::serialize(result)}
        });
        return json::object{{"content", content}};
    }
    catch (const std::exception& e) {
        json::array content;
        content.push_back(json::object{
            {"type", "text"},
            {"text", std::string("Error: ") + e.what()}
        });
        return json::object{{"content", content}, {"isError", true}};
    }
}

} // namespace lldb_mcp
