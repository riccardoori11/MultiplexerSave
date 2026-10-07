#include "work/project.hpp"
#include "work/process.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

namespace work {
namespace {
fs::path env_path(const char* override_name, const char* xdg, const fs::path& fallback) {
    if (const char* value = std::getenv(override_name); value && *value)
        return fs::absolute(value).lexically_normal();
    if (const char* value = std::getenv(xdg); value && *value) {
        if (!fs::path(value).is_absolute())
            throw std::runtime_error(std::string(xdg) + " must be an absolute path.");
        return fs::path(value) / "work";
    }
    return fallback / "work";
}
bool identifier_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-';
}
std::string yaml_scalar(const std::string& value) {
    std::string result = "'";
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\'') result += "''";
        else if (value[i] == '<' && i + 1 < value.size() && value[i + 1] == '%') {
            // tmuxinator evaluates ERB before reading YAML; preserve literal text.
            result += "<%%";
            ++i;
        } else result += value[i];
    }
    return result + "'";
}
}

std::string trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}
void validate_name(const std::string& name) {
    if (name.empty() || name.size() > 48 ||
        !((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= 'A' && name[0] <= 'Z') ||
          (name[0] >= '0' && name[0] <= '9')) ||
        !std::all_of(name.begin(), name.end(), identifier_char))
        throw std::runtime_error("Project names must be 1–48 letters, digits, underscores or hyphens, starting with a letter or digit.");
}
Paths Paths::from_environment() {
    const char* raw_home = std::getenv("HOME");
    if (!raw_home || !*raw_home) throw std::runtime_error("HOME is not set.");
    const fs::path user_home(raw_home);
    return {env_path("WORK_CONFIG_HOME", "XDG_CONFIG_HOME", user_home / ".config"),
            env_path("WORK_STATE_HOME", "XDG_STATE_HOME", user_home / ".local/state")};
}
fs::path Paths::project(const std::string& name) const { return config / "projects" / (name + ".work"); }
fs::path Paths::project_state(const std::string& name) const { return state / "projects" / name; }
fs::path Paths::snapshots(const std::string& name) const { return project_state(name) / "resurrect"; }

std::string read_file(const fs::path& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Cannot read " + path.string());
    std::ostringstream out;
    out << in.rdbuf();
    if (in.bad()) throw std::runtime_error("Read failed: " + path.string());
    return out.str();
}
void write_atomic(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    auto pattern = (path.parent_path() / (path.filename().string() + ".tmp.XXXXXX")).string();
    std::vector<char> bytes(pattern.begin(), pattern.end());
    bytes.push_back('\0');
    const int fd = ::mkstemp(bytes.data());
    if (fd < 0) throw std::runtime_error("Cannot create " + path.string() + ": " + std::strerror(errno));
    const fs::path temp(bytes.data());
    bool closed = false;
    try {
        std::size_t offset = 0;
        while (offset < content.size()) {
            const auto count = ::write(fd, content.data() + offset, content.size() - offset);
            if (count < 0) {
                if (errno == EINTR) continue;
                throw std::runtime_error("Cannot write " + path.string() + ": " + std::strerror(errno));
            }
            if (count == 0) throw std::runtime_error("Short write: " + path.string());
            offset += static_cast<std::size_t>(count);
        }
        if (::fsync(fd) < 0) throw std::runtime_error("Cannot sync " + path.string());
        closed = true;
        if (::close(fd) < 0) throw std::runtime_error("Cannot close " + path.string());
        fs::rename(temp, path);
    } catch (...) {
        if (!closed) ::close(fd);
        std::error_code ignored;
        fs::remove(temp, ignored);
        throw;
    }
}

Project read_project(const Paths& paths, const std::string& name) {
    validate_name(name);
    const auto file = paths.project(name);
    if (!fs::is_regular_file(file))
        throw std::runtime_error("Unknown project '" + name + "'. Create it with: work init " + name + " --root DIRECTORY");
    Project result;
    result.name = name;
    std::istringstream in(read_file(file));
    std::string line;
    std::size_t line_number = 0;
    std::set<std::string> global_keys, window_names, window_keys;
    Window* window = nullptr;
    const auto fail = [&](const std::string& message) {
        throw std::runtime_error(file.string() + ":" + std::to_string(line_number) + ": " + message);
    };
    while (std::getline(in, line)) {
        ++line_number;
        if (line.find('\0') != std::string::npos) fail("NUL characters are not supported.");
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        if (line.starts_with("[window ") && line.ends_with("]")) {
            const auto window_name = trim(line.substr(8, line.size() - 9));
            validate_name(window_name);
            if (!window_names.insert(window_name).second) fail("Duplicate window: " + window_name);
            result.windows.push_back(Window{window_name, "main-vertical", {}});
            window = &result.windows.back();
            window_keys.clear();
            continue;
        }
        const auto equal = line.find('=');
        if (equal == std::string::npos) fail("Expected key = value or [window NAME].");
        const auto key = trim(line.substr(0, equal));
        const auto value = trim(line.substr(equal + 1));
        if (!window) {
            if (!global_keys.insert(key).second) fail("Duplicate setting: " + key);
            if (key == "root") {
                if (value.empty()) fail("root cannot be empty.");
                result.root = fs::path(value);
                if (value == "~" || value.starts_with("~/"))
                    result.root = fs::path(std::getenv("HOME")) / (value.size() > 2 ? value.substr(2) : "");
                if (!result.root.is_absolute()) fail("root must be absolute (or start with ~/).");
            } else if (key == "editor_sessions") {
                if (value != "true" && value != "false") fail("editor_sessions must be true or false.");
                result.editor_sessions = value == "true";
            } else if (key == "resurrect_processes") result.resurrect_processes = value;
            else fail("Unknown setting: " + key);
        } else {
            if (key == "pane") window->panes.push_back(value);
            else if (key == "layout") {
                if (!window_keys.insert(key).second) fail("Duplicate layout.");
                const std::set<std::string> layouts{"main-vertical", "main-horizontal", "even-vertical", "even-horizontal", "tiled"};
                if (!layouts.contains(value)) fail("Use main-vertical, main-horizontal, even-vertical, even-horizontal or tiled.");
                window->layout = value;
            } else fail("Unknown window setting: " + key);
        }
    }
    if (result.root.empty()) fail("Missing root setting.");
    if (result.windows.empty()) fail("At least one [window NAME] is required.");
    for (const auto& item : result.windows)
        if (item.panes.empty()) fail("Window '" + item.name + "' needs at least one pane = line.");
    return result;
}

std::string project_template(const fs::path& root) {
    return "# Project configuration for work. Values are literal; do not add quotes.\n"
           "# A blank pane value opens a shell. Edit commands to fit your project.\n"
           "root = " + root.string() + "\n"
           "editor_sessions = true\n"
           "# Optional: use resurrect's process matching syntax for additional programs.\n"
           "# resurrect_processes = nvim vim tail\n\n"
           "[window code]\nlayout = main-vertical\npane = nvim .\npane = \n\n"
           "[window tools]\nlayout = even-horizontal\npane = git status\npane = \n";
}
std::string tmuxinator_yaml(const Project& project, const fs::path& config) {
    std::ostringstream out;
    out << "# Generated by work. Edit the .work project file instead.\n"
        << "name: " << yaml_scalar(project.name) << "\n"
        << "root: " << yaml_scalar(project.root.string()) << "\n"
        << "socket_name: " << yaml_scalar("work-" + project.name) << "\n"
        << "tmux_options: " << yaml_scalar("-f " + shell_quote(config.string())) << "\n"
        << "attach: false\nwindows:\n";
    for (const auto& window : project.windows) {
        out << "  - " << yaml_scalar(window.name) << ":\n"
            << "      layout: " << yaml_scalar(window.layout) << "\n      panes:\n";
        for (const auto& pane : window.panes) out << "        - " << yaml_scalar(pane) << "\n";
    }
    return out.str();
}
}
