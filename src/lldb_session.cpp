#include "lldb_session.hpp"

#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <sstream>
#include <stdexcept>

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <signal.h>

#ifdef __APPLE__
extern "C" int posix_openpt(int);
extern "C" int grantpt(int);
extern "C" int unlockpt(int);
extern "C" char* ptsname(int);
#endif

namespace lldb_mcp {

static bool debug_enabled = false;
void setDebug(bool enabled) { debug_enabled = enabled; }

static void debugLog(const std::string& msg) {
    if (debug_enabled) {
        fprintf(stderr, "[DEBUG] %s\n", msg.c_str());
    }
}

LldbSession::LldbSession(const std::string& session_id,
                         const std::string& lldb_path,
                         const std::string& working_dir)
    : id_(session_id)
    , lldb_path_(lldb_path)
    , working_dir_(working_dir)
{
}

LldbSession::~LldbSession() {
    cleanup();
}

bool LldbSession::createPty() {
    master_fd_ = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd_ < 0) {
        debugLog("posix_openpt failed: " + std::string(strerror(errno)));
        return false;
    }

    if (grantpt(master_fd_) < 0) {
        debugLog("grantpt failed: " + std::string(strerror(errno)));
        close(master_fd_);
        master_fd_ = -1;
        return false;
    }

    if (unlockpt(master_fd_) < 0) {
        debugLog("unlockpt failed: " + std::string(strerror(errno)));
        close(master_fd_);
        master_fd_ = -1;
        return false;
    }

    char* slave_name = ptsname(master_fd_);
    if (!slave_name) {
        debugLog("ptsname failed");
        close(master_fd_);
        master_fd_ = -1;
        return false;
    }
    debugLog(std::string("Slave PTY: ") + slave_name);

    slave_fd_ = open(slave_name, O_RDWR | O_NOCTTY);
    if (slave_fd_ < 0) {
        debugLog("open slave failed: " + std::string(strerror(errno)));
        close(master_fd_);
        master_fd_ = -1;
        return false;
    }

    // Set slave terminal to raw mode (disable echo)
    struct termios t;
    if (tcgetattr(slave_fd_, &t) == 0) {
        cfmakeraw(&t);
        tcsetattr(slave_fd_, TCSANOW, &t);
    }

    // Make master fd non-blocking
    int flags = fcntl(master_fd_, F_GETFL, 0);
    fcntl(master_fd_, F_SETFL, flags | O_NONBLOCK);

    return true;
}

void LldbSession::destroyPty() {
    if (master_fd_ >= 0) {
        close(master_fd_);
        master_fd_ = -1;
    }
    if (slave_fd_ >= 0) {
        close(slave_fd_);
        slave_fd_ = -1;
    }
}

std::string LldbSession::start() {
    debugLog("Starting LLDB process with path: " + lldb_path_);

    if (!createPty()) {
        throw std::runtime_error("Failed to create PTY");
    }

    pid_t pid = fork();
    if (pid < 0) {
        destroyPty();
        throw std::runtime_error("fork failed: " + std::string(strerror(errno)));
    }

    if (pid == 0) {
        // Child process
        setsid();

        // Make slave fd the controlling terminal
        if (ioctl(slave_fd_, TIOCSCTTY, 0) < 0) {
            debugLog("TIOCSCTTY failed: " + std::string(strerror(errno)));
        }

        // Redirect stdin/stdout/stderr to slave
        dup2(slave_fd_, STDIN_FILENO);
        dup2(slave_fd_, STDOUT_FILENO);
        dup2(slave_fd_, STDERR_FILENO);

        // Close all fds except 0,1,2
        if (slave_fd_ > 2) close(slave_fd_);
        if (master_fd_ > 2) close(master_fd_);

        // Set TERM
        setenv("TERM", "xterm-256color", 1);

        // Change working directory
        if (!working_dir_.empty()) {
            chdir(working_dir_.c_str());
        }

        // Execute LLDB
        execlp(lldb_path_.c_str(), lldb_path_.c_str(), nullptr);
        _exit(127);
    }

    // Parent process
    child_pid_ = pid;
    close(slave_fd_);
    slave_fd_ = -1;

    debugLog("LLDB process created with PID: " + std::to_string(child_pid_));

    // Wait for initial prompt
    debugLog("Waiting for initial prompt...");
    std::string output = readUntilPrompt(10.0);
    debugLog("Initial prompt received");

    ready_ = true;

    // Send version command to confirm LLDB is working
    debugLog("Sending version command");
    std::string version_output = executeCommand("version");
    output += version_output;

    return output;
}

std::string LldbSession::executeCommand(const std::string& command) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!ready_) {
        throw std::runtime_error("LLDB session is not ready");
    }
    if (master_fd_ < 0) {
        throw std::runtime_error("PTY not initialized");
    }
    if (child_pid_ <= 0) {
        throw std::runtime_error("No LLDB process");
    }

    // Check if process has terminated
    int status;
    pid_t result = waitpid(child_pid_, &status, WNOHANG);
    if (result == child_pid_) {
        ready_ = false;
        throw std::runtime_error("LLDB process has terminated with code: " +
                                 std::to_string(WEXITSTATUS(status)));
    }

    debugLog("Executing command: " + command);
    std::string cmd_line = command + "\n";
    ssize_t written = write(master_fd_, cmd_line.c_str(), cmd_line.size());
    if (written < 0) {
        throw std::runtime_error("Failed to write command to PTY: " +
                                 std::string(strerror(errno)));
    }

    return readUntilPrompt();
}

std::string LldbSession::readUntilPrompt(double timeout_secs) {
    std::string buffer;
    const std::string prompt = "(lldb)";
    double start_time = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    debugLog("Starting to read until prompt");

    while (true) {
        double current_time = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        if (current_time - start_time > timeout_secs) {
            debugLog("Timeout reached after " +
                     std::to_string(current_time - start_time) + " seconds");
            return buffer + "\n[Timeout waiting for LLDB response]";
        }

        // Check if process has terminated
        int status;
        pid_t result = waitpid(child_pid_, &status, WNOHANG);
        if (result == child_pid_) {
            debugLog("Process terminated with code: " +
                     std::to_string(WEXITSTATUS(status)));
            if (!buffer.empty()) return buffer;
            throw std::runtime_error("LLDB process terminated with code " +
                                     std::to_string(WEXITSTATUS(status)));
        }

        // Use select for timeout-based reading
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(master_fd_, &read_fds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms

        int ret = select(master_fd_ + 1, &read_fds, nullptr, nullptr, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            debugLog("select error: " + std::string(strerror(errno)));
            if (!buffer.empty()) return buffer;
            throw std::runtime_error("Error reading from PTY: " +
                                     std::string(strerror(errno)));
        }

        if (ret > 0 && FD_ISSET(master_fd_, &read_fds)) {
            char chunk[4096];
            ssize_t n = read(master_fd_, chunk, sizeof(chunk) - 1);
            if (n > 0) {
                chunk[n] = '\0';
                debugLog("Read " + std::to_string(n) + " bytes from PTY");
                buffer += chunk;

                // Check for prompt - look for "(lldb) " at end or "(lldb)" followed by space/newline
                if (buffer.find(prompt) != std::string::npos) {
                    debugLog("Found LLDB prompt in buffer");
                    return buffer;
                }
            } else if (n == 0) {
                debugLog("EOF from PTY");
                if (!buffer.empty()) return buffer;
                throw std::runtime_error("LLDB PTY closed");
            } else {
                // EAGAIN/EWOULDBLOCK for non-blocking fd means no data
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    debugLog("read error: " + std::string(strerror(errno)));
                    if (!buffer.empty()) return buffer;
                    throw std::runtime_error("Error reading from PTY: " +
                                             std::string(strerror(errno)));
                }
            }
        }
    }
}

void LldbSession::cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);
    debugLog("Cleaning up LLDB session");

    if (master_fd_ >= 0 && child_pid_ > 0) {
        int status;
        pid_t result = waitpid(child_pid_, &status, WNOHANG);
        if (result == 0) {
            std::string quit_cmd = "quit\n";
            write(master_fd_, quit_cmd.c_str(), quit_cmd.size());
        }
    }

    // Close PTY first to avoid deadlock: LLDB may wait for
    // PTY close before fully exiting
    destroyPty();

    if (child_pid_ > 0) {
        int status;
        pid_t result = waitpid(child_pid_, &status, WNOHANG);
        if (result == 0) {
            usleep(200000);
            result = waitpid(child_pid_, &status, WNOHANG);
        }
        if (result == 0) {
            kill(child_pid_, SIGTERM);
            usleep(200000);
            result = waitpid(child_pid_, &status, WNOHANG);
        }
        if (result == 0) {
            kill(child_pid_, SIGKILL);
            waitpid(child_pid_, &status, 0);
        }
    }

    child_pid_ = -1;
    ready_ = false;
    debugLog("LLDB session cleanup completed");
}

} // namespace lldb_mcp
