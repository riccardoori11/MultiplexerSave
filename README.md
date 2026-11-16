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

The application is implemented in C++20. The repository no longer contains a
test suite or test build targets. The former Python integration and live smoke
tests remain in git history at commit `1e2336f`.

Build with `make -j`, or use CMake:

```sh
cmake -S . -B build-cmake
cmake --build build-cmake
```
