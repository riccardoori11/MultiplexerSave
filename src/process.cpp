#include "work/process.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <map>
#include <spawn.h>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace work {
namespace {
class Descriptor {
public:
    explicit Descriptor(int fd = -1) : fd_(fd) {}
    ~Descriptor() { close(); }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    int get() const { return fd_; }
    void close() {
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    }
private:
    int fd_;
};

class SpawnActions {
public:
    SpawnActions() {
        const int error = posix_spawn_file_actions_init(&actions);
        if (error) throw std::runtime_error(std::strerror(error));
    }
    ~SpawnActions() { posix_spawn_file_actions_destroy(&actions); }
    SpawnActions(const SpawnActions&) = delete;
    SpawnActions& operator=(const SpawnActions&) = delete;
    posix_spawn_file_actions_t actions{};
};

int wait_for(pid_t pid) {
    int status{};
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) throw std::runtime_error("waitpid: " + std::string(std::strerror(errno)));
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 1;
}
}

ProcessResult run(const std::vector<std::string>& args, bool capture,
                  const Environment& overrides) {
    if (args.empty()) throw std::runtime_error("Cannot run an empty command.");
    std::vector<char*> argv;
    for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    std::map<std::string, std::string> env;
    for (char** item = environ; *item; ++item) {
        std::string value(*item);
        const auto equal = value.find('=');
        if (equal != std::string::npos) env[value.substr(0, equal)] = value.substr(equal + 1);
    }
    for (const auto& [key, value] : overrides) {
        if (value) env[key] = *value; else env.erase(key);
    }
    std::vector<std::string> storage;
    for (const auto& [key, value] : env) storage.push_back(key + "=" + value);
    std::vector<char*> envp;
    for (auto& item : storage) envp.push_back(item.data());
    envp.push_back(nullptr);

    int fds[2]{-1, -1};
    if (capture && ::pipe2(fds, O_CLOEXEC) < 0)
        throw std::runtime_error("pipe: " + std::string(std::strerror(errno)));
    Descriptor reader(fds[0]), writer(fds[1]);
    SpawnActions actions;
    if (capture) {
        for (const int target : {STDOUT_FILENO, STDERR_FILENO}) {
            const int error = posix_spawn_file_actions_adddup2(&actions.actions, writer.get(), target);
            if (error) throw std::runtime_error(std::strerror(error));
        }
        for (const int fd : fds) {
            const int error = posix_spawn_file_actions_addclose(&actions.actions, fd);
            if (error) throw std::runtime_error(std::strerror(error));
        }
    }
    pid_t pid{};
    const int error = posix_spawnp(&pid, argv[0], &actions.actions, nullptr, argv.data(), envp.data());
    if (error) throw std::runtime_error("Cannot run " + args[0] + ": " + std::strerror(error));
    writer.close();

    ProcessResult result;
    try {
        if (capture) {
            char buffer[4096];
            for (;;) {
                const ssize_t count = ::read(reader.get(), buffer, sizeof buffer);
                if (count == 0) break;
                if (count < 0) {
                    if (errno == EINTR) continue;
                    throw std::runtime_error("read: " + std::string(std::strerror(errno)));
                }
                result.output.append(buffer, static_cast<std::size_t>(count));
                // Keep diagnostics bounded, but always drain the child pipe.
                constexpr std::size_t limit = 65536;
                if (result.output.size() > limit) result.output.erase(0, result.output.size() - limit);
            }
        }
    } catch (...) {
        reader.close();
        (void)wait_for(pid);
        throw;
    }
    reader.close();
    result.exit_code = wait_for(pid);
    return result;
}

std::optional<std::string> executable(const std::string& name) {
    if (name.find('/') != std::string::npos) {
        if (::access(name.c_str(), X_OK) == 0) return name;
        return std::nullopt;
    }
    const char* raw = std::getenv("PATH");
    if (!raw) return std::nullopt;
    std::string paths(raw);
    std::size_t start = 0;
    for (;;) {
        const auto end = paths.find(':', start);
        auto dir = paths.substr(start, end == std::string::npos ? end : end - start);
        if (dir.empty()) dir = ".";
        const auto path = std::filesystem::path(dir) / name;
        if (::access(path.c_str(), X_OK) == 0 && !std::filesystem::is_directory(path))
            return std::filesystem::absolute(path).string();
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return std::nullopt;
}

std::string shell_quote(const std::string& value) {
    std::string result = "'";
    for (const char c : value) result += c == '\'' ? "'\\''" : std::string(1, c);
    return result + "'";
}

std::string tmux_quote(const std::string& value) {
    std::string result = "\"";
    for (const char c : value) {
        if (c == '\\' || c == '\"' || c == '$') result += '\\';
        result += c;
    }
    return result + "\"";
}
}
