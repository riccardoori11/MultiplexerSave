# work

A Linux workspace manager built with C++20, tmuxinator, and tmux-resurrect.
`work` gives each project its own tmux server and saved workspace. Opening a
project reconnects to its running workspace, restores its snapshot, or creates
its configured windows and panes.

## Requirements

- Linux and a C++20 compiler.
- Make, or CMake 3.20 or newer.
- tmux, tmuxinator, Bash, and a tmux-resurrect checkout at runtime.


## Development and testing

The application is implemented in C++20. Python is used only by the test tools,
which are maintained on the `integration/updates` branch. The `master` branch
contains the application without the Python test suite or test build targets.

Build with `make -j`, or use CMake:

```sh
cmake -S . -B build-cmake
cmake --build build-cmake
```

To run integration tests, use a checkout of `integration/updates` and run
`make test`. That branch also includes `tests/live_smoke.py` for testing with
real tmux dependencies.
