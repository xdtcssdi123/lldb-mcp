#pragma once

#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <chrono>
#include <optional>

namespace lldb_mcp {

void setDebug(bool enabled);

class LldbSession {
public:
    LldbSession(const std::string& session_id,
                const std::string& lldb_path,
                const std::string& working_dir);
    ~LldbSession();

    LldbSession(const LldbSession&) = delete;
    LldbSession& operator=(const LldbSession&) = delete;

    std::string start();
    std::string executeCommand(const std::string& command);
    void cleanup();

    bool isReady() const { return ready_.load(); }
    const std::string& id() const { return id_; }
    const std::string& target() const { return target_; }
    void setTarget(const std::string& t) { target_ = t; }
    const std::string& workingDir() const { return working_dir_; }

private:
    bool createPty();
    void destroyPty();
    std::string readUntilPrompt(double timeout_secs = 10.0);

    std::string id_;
    std::string lldb_path_;
    std::string working_dir_;
    std::string target_;
    std::atomic<bool> ready_{false};

    int master_fd_ = -1;
    int slave_fd_ = -1;
    pid_t child_pid_ = -1;

    std::mutex mutex_;
};

} // namespace lldb_mcp
