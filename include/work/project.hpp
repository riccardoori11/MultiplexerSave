#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace work {
namespace fs = std::filesystem;
struct Window {
    std::string name;
    std::string layout{"main-vertical"};
    std::vector<std::string> panes;
};
struct Project {
    std::string name;
    fs::path root;
    bool editor_sessions{true};
    std::string resurrect_processes;
    std::vector<Window> windows;
};
struct Paths {
    fs::path config;
    fs::path state;
    fs::path project(const std::string& name) const;
    fs::path project_state(const std::string& name) const;
    fs::path snapshots(const std::string& name) const;
    static Paths from_environment();
};
std::string trim(std::string text);
void validate_name(const std::string& name);
Project read_project(const Paths& paths, const std::string& name);
std::string project_template(const fs::path& root);
std::string tmuxinator_yaml(const Project& project, const fs::path& config);
void write_atomic(const fs::path& path, const std::string& content);
std::string read_file(const fs::path& path);
}
