# Repository Guidelines

## Scope & Project Structure

This guide resides in `/home/riccardo`, which is not a Git repository. Its project-specific instructions apply to `terminal_multiplexer/`, the Linux C++20 `work` workspace manager. Run the commands below from that directory; keep changes confined to the relevant project.

- `src/main.cpp`: CLI, locks, tmux server setup, and workspace lifecycle.
- `src/project.cpp`: project configuration parsing, YAML generation, and atomic writes.
- `src/process.cpp`: subprocess execution and child handling.
- `include/work/`: C++ interfaces.
- `tests/`: Python integration tests, fake dependencies, and optional live smoke tests.
- `README.md` and `VALIDATION.md`: usage and validation status. Generated binaries belong in `build/` or `build-cmake/`.

## Build, Test, and Development Commands

- `cd /home/riccardo/terminal_multiplexer`: enter the repository.
- `make -j`: build `build/work` with C++20 and compiler warnings.
- `make test`: build and run integration tests without real tmux dependencies.
- `cmake -S . -B build-cmake && cmake --build build-cmake`: use the alternative CMake build.
- `ctest --test-dir build-cmake --output-on-failure`: run CMake-registered tests.
- `./build/work doctor`: check runtime dependencies and configuration paths.
- `python3 tests/live_smoke.py ./build/work`: exercise real save/restore behavior with tmux, tmuxinator, and tmux-resurrect installed.

## Coding Style & Naming Conventions

Follow surrounding code: four-space indentation in C++ and Python, snake_case C++ functions and variables, and PascalCase C++ types. Keep headers under `include/work/` and implementation under `src/`. Preserve the configured warning flags, including `-Wconversion` and `-Wshadow`. No dedicated formatter or linter configuration is present.

## Testing Guidelines

Python integration tests use `unittest`; name methods `test_<behavior>`. Add regression coverage for changed CLI behavior, failure handling, configuration parsing, and project isolation. Use temporary directories and controlled external-tool fixtures. No numeric coverage threshold is configured. Report live-test limitations separately from integration results.

## Commit & Pull Request Guidelines

History contains only `first commit`, so no established commit convention is evident. Use concise, imperative subjects describing one coherent change. Pull requests should explain behavior changes, list validation commands and results, link relevant issues, and update usage documentation when needed.

## Configuration & Process Safety

Use isolated `WORK_CONFIG_HOME` and `WORK_STATE_HOME` values during manual testing; state paths must exclude whitespace. Preserve direct subprocess argument handling, atomic writes, and per-project locking. Operate only on the intended project's tmux server.
