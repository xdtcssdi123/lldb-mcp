cmake_minimum_required(VERSION 3.16)
project(lldb-mcp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Boost 1.79+
set(Boost_NO_BOOST_CMAKE OFF)
find_package(Boost 1.79 CONFIG REQUIRED COMPONENTS filesystem json)

add_executable(lldb-mcp
    src/main.cpp
    src/lldb_session.cpp
    src/mcp_server.cpp
    src/mcp_tools.cpp
)

target_include_directories(lldb-mcp PRIVATE src)

target_link_libraries(lldb-mcp PRIVATE
    Boost::filesystem
    Boost::json
)

if(APPLE)
    target_link_libraries(lldb-mcp PRIVATE util)
endif()
