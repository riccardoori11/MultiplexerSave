# work

A Linux workspace manager built with C++20, tmuxinator, and tmux-resurrect.
`work` gives each project its own tmux server and saved workspace. Opening a
project reconnects to its running workspace, restores its snapshot, or creates
its configured windows and panes.

## Requirements

- Linux and a C++20 compiler.
- Make, or CMake 3.20 or newer.
- tmux, tmuxinator, Bash, and a tmux-resurrect checkout at runtime.

