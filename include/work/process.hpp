#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace work {
using Environment = std::vector<std::pair<std::string, std::optional<std::string>>>;
struct ProcessResult {
    int exit_code{};
    std::string output;
};
// Runs an argument vector directly; no shell parses arguments.
ProcessResult run(const std::vector<std::string>& args, bool capture = true,
                  const Environment& environment = {});
std::optional<std::string> executable(const std::string& name);
std::string shell_quote(const std::string& value);
std::string tmux_quote(const std::string& value);
}
