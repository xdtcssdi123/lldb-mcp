#include "mcp_server.hpp"
#include "mcp_tools.hpp"
#include "lldb_session.hpp"

#include <cstring>
#include <iostream>
#include <boost/json.hpp>

static bool debug_enabled = false;

static void printUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " [--debug]\n"
              << "  LLDB MCP Server - Model Context Protocol server for LLDB\n"
              << "  --debug    Enable debug logging to stderr\n";
}

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--debug") == 0) {
            debug_enabled = true;
        }
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        }
        else {
            std::cerr << "Unknown option: " << argv[i] << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    if (debug_enabled) {
        fprintf(stderr, "[DEBUG] Debug logging enabled\n");
    }

    lldb_mcp::setDebug(debug_enabled);

    try {
        lldb_mcp::McpServer server("lldb-mcp", "1.0.0");
        lldb_mcp::McpTools tools;

        tools.registerAllTools(server);

        server.setToolHandler(
            [&tools](const std::string& name, const lldb_mcp::json::value& args) {
                return tools.handleToolCall(name, args);
            });

        if (debug_enabled) {
            fprintf(stderr, "[DEBUG] MCP server starting...\n");
        }

        server.run();
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
