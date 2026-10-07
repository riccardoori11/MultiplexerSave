#include "work/process.hpp"
#include "work/project.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <thread>
#include <vector>
#include <unistd.h>

namespace work {
namespace {
class ProjectLock {
public:
    ProjectLock(const Paths& paths, const std::string& name) {
        const auto dir = paths.project_state(name);
        fs::create_directories(dir);
        fd_ = ::open((dir / "lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600);
        if (fd_ < 0) throw std::runtime_error("Cannot open project lock: " + std::string(std::strerror(errno)));
        while (::flock(fd_, LOCK_EX) < 0) {
            if (errno == EINTR) continue;
            ::close(fd_); fd_ = -1;
            throw std::runtime_error("Cannot lock project.");
        }
    }
    ~ProjectLock() { if (fd_ >= 0) ::close(fd_); }
    ProjectLock(const ProjectLock&) = delete;
    ProjectLock& operator=(const ProjectLock&) = delete;
private:
    int fd_{-1};
};

std::vector<std::string> tmux_args(const std::string& name, std::vector<std::string> args) {
    std::vector<std::string> result{"tmux", "-L", "work-" + name};
    result.insert(result.end(), args.begin(), args.end());
    return result;
}
const Environment clean_tmux{{"TMUX", std::nullopt}, {"TMUX_PANE", std::nullopt}};
ProcessResult tmux(const std::string& name, std::vector<std::string> args,
                   OutputCapture capture = OutputCapture::diagnostic) {
    return run(tmux_args(name, std::move(args)), capture, clean_tmux);
}
void require_executable(const std::string& name) {
    if (!executable(name)) throw std::runtime_error("Missing dependency: " + name + ". Run work doctor for setup instructions.");
}
void check(const ProcessResult& result, const std::string& context) {
    if (result.exit_code != 0)
        throw std::runtime_error(context + " (exit " + std::to_string(result.exit_code) + ")\n" + trim(result.output));
}
void tmux_check(const std::string& name, std::vector<std::string> args, const std::string& context) {
    check(tmux(name, std::move(args)), context);
}
bool running(const std::string& name) { return tmux(name, {"has-session"}).exit_code == 0; }

std::optional<fs::path> plugin_directory() {
    if (const char* path = std::getenv("WORK_RESURRECT_DIR"); path && *path) {
        const auto dir = fs::absolute(path);
        if (fs::is_regular_file(dir / "resurrect.tmux") && fs::is_regular_file(dir / "scripts/save.sh") &&
            fs::is_regular_file(dir / "scripts/restore.sh")) return dir;
        throw std::runtime_error("WORK_RESURRECT_DIR does not point to a tmux-resurrect checkout: " + dir.string());
    }
    const fs::path user_home(std::getenv("HOME"));
    std::vector<fs::path> candidates{user_home / ".tmux/plugins/tmux-resurrect",
                                   user_home / ".config/tmux/plugins/tmux-resurrect"};
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        candidates.insert(candidates.begin(), fs::path(xdg) / "tmux/plugins/tmux-resurrect");
    for (const auto& dir : candidates) {
        if (fs::is_regular_file(dir / "resurrect.tmux") && fs::is_regular_file(dir / "scripts/save.sh") &&
            fs::is_regular_file(dir / "scripts/restore.sh")) return dir;
    }
    return std::nullopt;
}
fs::path require_plugin() {
    const auto dir = plugin_directory();
    if (!dir) throw std::runtime_error("tmux-resurrect was not found. Install it under ~/.tmux/plugins/tmux-resurrect, or set WORK_RESURRECT_DIR.");
    require_executable("bash");
    return *dir;
}
std::string own_executable() {
    return fs::read_symlink("/proc/self/exe").string();
}
std::string save_command(const Paths& paths, const std::string& name, const fs::path& plugin) {
    return "env WORK_CONFIG_HOME=" + shell_quote(paths.config.string()) +
           " WORK_STATE_HOME=" + shell_quote(paths.state.string()) +
           " WORK_RESURRECT_DIR=" + shell_quote(plugin.string()) + " " +
           shell_quote(own_executable()) + " save " + shell_quote(name) + " --quiet";
}
void prepare_snapshot_directory(const Paths& paths, const std::string& name) {
    const auto snapshot_path = paths.snapshots(name).string();
    // Upstream resurrect currently uses unquoted file redirections for this path.
    if (snapshot_path.find_first_of(" \t\r\n") != std::string::npos)
        throw std::runtime_error("tmux-resurrect requires a state path without whitespace. Set WORK_STATE_HOME to a suitable directory.");
    fs::create_directories(snapshot_path);
}
void prepare_config(const Paths& paths, const Project& project) {
    prepare_snapshot_directory(paths, project.name);
    const auto dir = paths.project_state(project.name);
    const auto snapshot_path = paths.snapshots(project.name).string();
    std::ostringstream config;
    config << "# Generated by work. Shared preferences belong in " << (paths.config / "tmux.conf").string() << "\n"
           << "source-file -q " << tmux_quote((paths.config / "tmux.conf").string()) << "\n"
           << "set-environment -g WORK_PROJECT " << tmux_quote(project.name) << "\n"
           << "set-option -g @resurrect-dir " << tmux_quote(snapshot_path) << "\n";
    write_atomic(dir / "tmux.conf", config.str());
    write_atomic(dir / "tmuxinator.yml", tmuxinator_yaml(project, dir / "tmux.conf"));
}
void configure_server(const Paths& paths, const Project& project, const fs::path& plugin) {
    const auto& name = project.name;
    tmux_check(name, {"set-option", "-g", "@resurrect-dir", paths.snapshots(name).string()}, "Cannot set snapshot directory");
    if (project.editor_sessions) {
        tmux_check(name, {"set-option", "-g", "@resurrect-strategy-vim", "session"}, "Cannot configure Vim sessions");
        tmux_check(name, {"set-option", "-g", "@resurrect-strategy-nvim", "session"}, "Cannot configure Neovim sessions");
    } else {
        (void)tmux(name, {"set-option", "-gu", "@resurrect-strategy-vim"});
        (void)tmux(name, {"set-option", "-gu", "@resurrect-strategy-nvim"});
    }
    if (!project.resurrect_processes.empty())
        tmux_check(name, {"set-option", "-g", "@resurrect-processes", project.resurrect_processes}, "Cannot configure restored programs");
    else (void)tmux(name, {"set-option", "-gu", "@resurrect-processes"});
    tmux_check(name, {"run-shell", "bash " + shell_quote((plugin / "resurrect.tmux").string())}, "Cannot load tmux-resurrect");
    const auto command = save_command(paths, name, plugin);
    tmux_check(name, {"set-hook", "-g", "client-detached", "run-shell -b " + tmux_quote(command)}, "Cannot configure saving on detach");
    tmux_check(name, {"bind-key", "C-s", "run-shell", command}, "Cannot configure save shortcut");
}
Environment plugin_environment(const std::string& name) {
    const auto context = tmux(name, {"display-message", "-p", "#{socket_path},#{pid},0"}, OutputCapture::complete);
    check(context, "Cannot identify project tmux server");
    const auto pane = tmux(name, {"display-message", "-p", "#{pane_id}"}, OutputCapture::complete);
    check(pane, "Cannot identify active pane");
    return {{"TMUX", trim(context.output)}, {"TMUX_PANE", trim(pane.output)}};
}
std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    for (;;) {
        const auto end = line.find('\t', start);
        fields.push_back(line.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return fields;
}
std::set<std::string> saved_panes(const fs::path& last, bool check_directories) {
    const auto text = read_file(last);
    std::istringstream input(text);
    std::string line;
    std::set<std::string> result;
    while (std::getline(input, line)) {
        const auto fields = split_tabs(line);
        if (fields.empty() || fields.front() != "pane") continue;
        if (fields.size() < 11 || fields[1].empty() || fields[2].empty() || fields[5].empty() ||
            fields[7].empty() || fields[7][0] != ':')
            throw std::runtime_error("Invalid pane record in " + last.string() + ". The saved file was left untouched.");
        const auto dir = fields[7].substr(1);
        if (check_directories && !fs::is_directory(dir))
            throw std::runtime_error("Saved working directory no longer exists: " + dir + ". Use --fresh to open the project configuration instead.");
        result.insert(fields[1] + "\t" + fields[2] + "\t" + fields[5]);
    }
    if (result.empty()) throw std::runtime_error("Snapshot has no valid panes: " + last.string() + ". Use --fresh to open the project configuration instead.");
    return result;
}
std::set<std::string> live_panes(const std::string& name) {
    auto response = tmux(name, {"list-panes", "-a", "-F", "#{session_name}\t#{window_index}\t#{pane_index}"},
                         OutputCapture::complete);
    check(response, "Cannot inspect restored panes");
    std::istringstream input(response.output);
    std::set<std::string> result;
    std::string line;
    while (std::getline(input, line)) if (!line.empty()) result.insert(line);
    return result;
}
void normalize_snapshot_directories(const std::string& name, const fs::path& last) {
    const auto response = tmux(name, {"list-panes", "-a", "-F",
        "#{session_name}\t#{window_index}\t#{pane_index}\t#{pane_current_path}"}, OutputCapture::complete);
    check(response, "Cannot inspect saved directories");
    std::map<std::string, std::string> directories;
    std::istringstream live(response.output);
    std::string line;
    while (std::getline(live, line)) {
        const auto fields = split_tabs(line);
        if (fields.size() != 4) throw std::runtime_error("Unsupported tab in a pane name or directory.");
        directories[fields[0] + "\t" + fields[1] + "\t" + fields[2]] = fields[3];
    }
    const auto original = read_file(last);
    std::istringstream input(original);
    std::ostringstream output;
    while (std::getline(input, line)) {
        auto fields = split_tabs(line);
        if (fields.size() >= 11 && fields[0] == "pane") {
            const auto key = fields[1] + "\t" + fields[2] + "\t" + fields[5];
            const auto found = directories.find(key);
            if (found == directories.end()) throw std::runtime_error("A saved pane no longer exists.");
            // Current resurrect versions escape spaces while saving. Restore
            // uses quoted directory arguments; use the actual tmux path.
            fields[7] = ":" + found->second;
            for (std::size_t i = 0; i < fields.size(); ++i) {
                if (i) output << '\t';
                output << fields[i];
            }
            output << '\n';
        } else output << line << '\n';
    }
    if (output.str() != original) write_atomic(fs::canonical(last), output.str());
}
void restore_selection(const std::string& name, const fs::path& last) {
    // Resurrect normally runs inside an attached client. Its switch-client calls
    // cannot select windows during our headless restore, so apply selections
    // with explicit session/window/pane targets before attaching.
    std::istringstream input(read_file(last));
    std::vector<std::string> windows;
    std::string line;
    while (std::getline(input, line)) {
        const auto fields = split_tabs(line);
        if (fields.size() >= 11 && fields[0] == "pane" && fields[8] == "1") {
            tmux_check(name, {"select-pane", "-t", "=" + fields[1] + ":" + fields[2] + "." + fields[5]},
                       "Cannot restore active pane");
        } else if (fields.size() >= 8 && fields[0] == "window" &&
                   (fields[4] == "1" || fields[5].find('*') != std::string::npos)) {
            windows.push_back("=" + fields[1] + ":" + fields[2]);
        }
    }
    for (const auto& target : windows)
        tmux_check(name, {"select-window", "-t", target}, "Cannot restore active window");
}
std::string current_project(const Paths& paths) {
    if (const char* project = std::getenv("WORK_PROJECT"); project && *project) {
        validate_name(project);
        return project;
    }
    const auto file = paths.state / "last-project";
    if (!fs::is_regular_file(file))
        throw std::runtime_error("No previous project. Start with: work init NAME --root DIRECTORY");
    const auto name = trim(read_file(file));
    validate_name(name);
    return name;
}
void save_project(const Paths& paths, const std::string& name, bool quiet) {
    validate_name(name);
    require_executable("tmux");
    const auto plugin = require_plugin();
    ProjectLock lock(paths, name);
    if (!running(name)) throw std::runtime_error("Project '" + name + "' is not running; its previous snapshot was left untouched.");
    prepare_snapshot_directory(paths, name);
    tmux_check(name, {"set-option", "-g", "@resurrect-dir", paths.snapshots(name).string()}, "Cannot set snapshot directory");
    const auto last = paths.snapshots(name) / "last";
    const bool previous_link = fs::is_symlink(last);
    const auto previous_target = previous_link ? fs::read_symlink(last) : fs::path{};
    const bool previous_file = fs::is_regular_file(last);
    const auto previous_path = previous_file ? fs::canonical(last) : fs::path{};
    const auto previous_content = previous_file ? read_file(last) : std::string{};
    const std::string hook_option = "@resurrect-hook-post-save-all";
    const auto old_hook_result = tmux(name, {"show-option", "-gqv", hook_option}, OutputCapture::complete);
    check(old_hook_result, "Cannot inspect save completion hook");
    const auto old_hook = trim(old_hook_result.output);
    const auto marker = paths.project_state(name) / ("save-completed-" + std::to_string(::getpid()));
    std::error_code ignored;
    fs::remove(marker, ignored);
    const auto completion = "printf done > " + shell_quote(marker.string());
    const auto hook = old_hook.empty() ? completion : "( " + old_hook + "\n) &&\n" + completion;
    tmux_check(name, {"set-option", "-g", hook_option, hook}, "Cannot configure save completion check");
    const auto reset_hook = [&] {
        try {
            if (old_hook.empty()) (void)tmux(name, {"set-option", "-gu", hook_option});
            else (void)tmux(name, {"set-option", "-g", hook_option, old_hook});
        } catch (...) { /* Best-effort cleanup if the server exited meanwhile. */ }
        std::error_code remove_error;
        fs::remove(marker, remove_error);
    };
    try {
        // Upstream snapshot filenames have one-second resolution. Avoid
        // overwriting (and subsequently deleting) the previous save when a
        // detach and a manual save occur in the same second.
        if (previous_file) {
            using namespace std::chrono;
            const auto now = system_clock::now();
            const auto previous_save = fs::file_time_type::clock::to_sys(fs::last_write_time(last));
            if (floor<seconds>(previous_save) == floor<seconds>(now))
                std::this_thread::sleep_until(floor<seconds>(now) + seconds(1) + milliseconds(20));
        }
        check(run({"bash", (plugin / "scripts/save.sh").string()}, OutputCapture::diagnostic, plugin_environment(name)), "Saving failed");
        if (!fs::is_regular_file(marker))
            throw std::runtime_error("Resurrect did not complete saving. The previous snapshot was retained.");
        if (!fs::is_regular_file(last))
            throw std::runtime_error("Resurrect did not produce a snapshot. Check the plugin and tmux versions.");
        normalize_snapshot_directories(name, last);
        const auto expected = live_panes(name);
        if (saved_panes(last, false) != expected)
            throw std::runtime_error("Snapshot does not contain all current panes. Saving was not verified.");
        reset_hook();
    } catch (...) {
        reset_hook();
        // A failing plugin must not replace the last usable text snapshot.
        if (previous_file) write_atomic(previous_path, previous_content);
        if (previous_link) {
            const auto temporary_link = last.string() + ".rollback-" + std::to_string(::getpid());
            std::error_code link_error;
            fs::remove(temporary_link, link_error);
            fs::create_symlink(previous_target, temporary_link);
            fs::rename(temporary_link, last);
        } else if (previous_file) write_atomic(last, previous_content);
        else { std::error_code last_error; fs::remove(last, last_error); }
        throw;
    }
    if (!quiet) std::cout << "Saved " << name << "\n";
}
void open_project(const Paths& paths, const std::string& name, bool no_attach, bool fresh) {
    const auto project = read_project(paths, name);
    require_executable("tmux");
    if (!no_attach) {
        if (!::isatty(STDIN_FILENO) || !::isatty(STDOUT_FILENO))
            throw std::runtime_error("Opening interactively needs a terminal. Use --no-attach for headless startup.");
        if (const char* value = std::getenv("TMUX"); value && *value)
            throw std::runtime_error("Open projects from a regular terminal to avoid nesting tmux servers. Saving works from inside tmux.");
    }
    const auto plugin = require_plugin();
    {
        ProjectLock lock(paths, name);
        prepare_config(paths, project);
        if (running(name)) {
            configure_server(paths, project, plugin);
            std::cout << "Reusing running project " << name << "\n";
        } else {
            const auto last = paths.snapshots(name) / "last";
            const bool has_snapshot = fs::exists(last) || fs::is_symlink(last);
            if (has_snapshot && !fresh) {
                const auto expected = saved_panes(last, true);
                tmux_check(name, {"-f", (paths.project_state(name) / "tmux.conf").string(),
                                 "new-session", "-d", "-s", "0"}, "Cannot start restoration server");
                try {
                    configure_server(paths, project, plugin);
                    check(run({"bash", (plugin / "scripts/restore.sh").string()}, OutputCapture::diagnostic, plugin_environment(name)), "Restoration failed");
                    if (live_panes(name) != expected)
                        throw std::runtime_error("Restoration did not recreate the saved panes. The snapshot was left untouched.");
                    restore_selection(name, last);
                } catch (...) {
                    (void)tmux(name, {"kill-server"});
                    throw;
                }
                std::cout << "Restored " << name << "\n";
            } else {
                if (!fs::is_directory(project.root))
                    throw std::runtime_error("Project root does not exist: " + project.root.string());
                require_executable("tmuxinator");
                try {
                    check(run({"tmuxinator", "start", name, "-p",
                               (paths.project_state(name) / "tmuxinator.yml").string()}, OutputCapture::diagnostic, clean_tmux), "Project startup failed");
                    if (!running(name)) throw std::runtime_error("tmuxinator did not create the expected project server.");
                    configure_server(paths, project, plugin);
                } catch (...) {
                    (void)tmux(name, {"kill-server"});
                    throw;
                }
                std::cout << "Created workspace for " << name << "\n";
            }
        }
        write_atomic(paths.state / "last-project", name + "\n");
    } // Release the lock before attaching: the detach hook needs it to save.
    if (!no_attach) {
        std::cout.flush();
        check(tmux(name, {"attach-session"}, OutputCapture::inherit), "Attaching failed");
    }
}
void list_projects(const Paths& paths) {
    const auto dir = paths.config / "projects";
    if (!fs::is_directory(dir)) { std::cout << "No projects. Create one with work init NAME --root DIRECTORY\n"; return; }
    std::vector<std::string> names;
    for (const auto& entry : fs::directory_iterator(dir))
        if (entry.is_regular_file() && entry.path().extension() == ".work") names.push_back(entry.path().stem().string());
    std::sort(names.begin(), names.end());
    const bool has_tmux = executable("tmux").has_value();
    for (const auto& name : names) {
        validate_name(name);
        const bool active = has_tmux && running(name);
        const bool snapshot = fs::is_regular_file(paths.snapshots(name) / "last");
        std::cout << name << "\t" << (active ? "running" : snapshot ? "saved" : "configured");
        if (!has_tmux) std::cout << " (tmux unavailable)";
        std::cout << "\n";
    }
}
int doctor(const Paths& paths) {
    bool okay = true;
    for (const auto& name : {"tmux", "tmuxinator", "bash"}) {
        const auto found = executable(name);
        std::cout << (found ? "OK      " : "MISSING ") << name;
        if (found) std::cout << " — " << *found;
        std::cout << "\n";
        okay = okay && found.has_value();
    }
    const auto plugin = plugin_directory();
    std::cout << (plugin ? "OK      " : "MISSING ") << "tmux-resurrect";
    if (plugin) std::cout << " — " << plugin->string();
    std::cout << "\nConfig: " << paths.config.string() << "\nState:  " << paths.state.string() << "\n";
    if (!okay || !plugin)
        std::cout << "Install tmux, tmuxinator and tmux-resurrect; see README.md for Fedora instructions.\n";
    return okay && plugin ? 0 : 1;
}
void help() {
    std::cout <<
        "work 0.1.0 — project workspaces powered by tmuxinator + tmux-resurrect\n\n"
        "  work                              Open the most recent project\n"
        "  work init NAME [--root DIRECTORY] Create a project configuration\n"
        "  work open [NAME] [--no-attach] [--fresh]\n"
        "  work save [NAME] [--quiet]        Save a running project\n"
        "  work list                        List projects and their state\n"
        "  work config NAME                 Print the editable configuration path\n"
        "  work doctor                      Check dependencies\n\n"
        "Each project uses its own tmux server. Detaching automatically saves it.\n"
        "--fresh uses the project configuration when no project server is running.\n";
}
}
int main_impl(int argc, char** argv) {
    ::umask(0077);
    if (argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "help")) { help(); return 0; }
    if (argc > 1 && std::string(argv[1]) == "--version") { std::cout << "work 0.1.0\n"; return 0; }
    const Paths paths = Paths::from_environment();
    const std::string command = argc > 1 ? argv[1] : "open";
    if (command == "doctor" || command == "list") {
        if (argc > 2) throw std::runtime_error("Unexpected arguments for work " + command);
        if (command == "doctor") return doctor(paths);
        list_projects(paths); return 0;
    }
    if (command == "config") {
        if (argc != 3) throw std::runtime_error("Usage: work config NAME");
        std::cout << existing_project_file(paths, argv[2]).string() << "\n"; return 0;
    }
    if (command == "init") {
        if (argc < 3) throw std::runtime_error("Usage: work init NAME [--root DIRECTORY]");
        const std::string name(argv[2]);
        validate_name(name);
        fs::path root = fs::current_path();
        if (argc == 5 && std::string(argv[3]) == "--root") root = fs::absolute(argv[4]);
        else if (argc != 3) throw std::runtime_error("Usage: work init NAME [--root DIRECTORY]");
        if (!fs::is_directory(root)) throw std::runtime_error("Project root does not exist: " + root.string());
        root = fs::canonical(root);
        if (root.string().find_first_of("\r\n") != std::string::npos)
            throw std::runtime_error("Project root cannot contain a newline.");
        if (trim(root.string()) != root.string())
            throw std::runtime_error("Project root cannot end in whitespace; project configuration values are trimmed.");
        ProjectLock lock(paths, name);
        if (fs::exists(paths.project(name))) throw std::runtime_error("Project already exists: " + name + ". Existing configuration was left untouched.");
        write_atomic(paths.project(name), project_template(root));
        std::cout << "Created " << name << "\nEdit: " << paths.project(name).string()
                  << "\nOpen: work open " << name << "\n";
        return 0;
    }
    if (command == "open" || command == "save") {
        std::optional<std::string> name;
        bool no_attach = false, fresh = false, quiet = false;
        for (int i = 2; i < argc; ++i) {
            const std::string arg(argv[i]);
            if (arg == "--no-attach" && command == "open") no_attach = true;
            else if (arg == "--fresh" && command == "open") fresh = true;
            else if (arg == "--quiet" && command == "save") quiet = true;
            else if (arg.starts_with("-")) throw std::runtime_error("Unknown option: " + arg);
            else if (!name) name = arg;
            else throw std::runtime_error("Unexpected argument: " + arg);
        }
        if (!name) name = current_project(paths);
        validate_name(*name);
        if (command == "open") open_project(paths, *name, no_attach, fresh);
        else save_project(paths, *name, quiet);
        return 0;
    }
    throw std::runtime_error("Unknown command: " + command + ". Run work --help.");
}
}

int main(int argc, char** argv) {
    try { return work::main_impl(argc, argv); }
    catch (const std::exception& error) {
        std::cerr << "work: " << error.what() << "\n";
        return 1;
    }
}
