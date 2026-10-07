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
enum class OutputCapture { inherit, diagnostic, complete };
// Runs an argument vector directly; no shell parses arguments.
// Diagnostic capture retains the final 64 KiB of combined stdout/stderr.
// Use complete capture for output that will be parsed as data.
ProcessResult run(const std::vector<std::string>& args, OutputCapture capture = OutputCapture::diagnostic,
                  const Environment& environment = {});
std::optional<std::string> executable(const std::string& name);
std::string shell_quote(const std::string& value);
std::string tmux_quote(const std::string& value);
}
