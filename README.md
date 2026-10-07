# work — a C++ tmux workspace manager

One command opens your project: reconnect to its running session, restore its
saved workspace, or create its initial layout.

```bash
work init multiplexer --root ~/projects/multiplexer
work open multiplexer
# Detach with Ctrl-b, then d. The workspace is saved automatically.
work
```

This is a first implementation for **Linux**, written in C++20. It
coordinates **tmuxinator** and **tmux-resurrect** rather than implementing
terminal emulation.

## What works

- One editable project file for roots, windows, pane layouts and startup commands.
- Automatic attach / restore / create selection.
- A separate tmux server and snapshot directory for every project.
- Saving on client detach, including when a terminal connection closes.
- Manual saving with `work save` or tmux's prefix followed by `Ctrl-s`.
- Opening the last project by running `work` with no arguments.
- Optional Vim/Neovim session restoration through resurrect.
- Dependency diagnostics, project listing, concurrent-operation locks and
  validation of saved/restored pane structure.
- Preserving the previous text snapshot if saving fails.
- Accepting successful saves of an unchanged workspace, and avoiding upstream's
  same-second snapshot filename collision.

## Install on Fedora

Install a compiler, tmux and the upstream dependencies:

```bash
sudo dnf install gcc-c++ make tmux ruby rubygems git
gem install --user-install tmuxinator
git clone https://github.com/tmux-plugins/tmux-resurrect \
  ~/.tmux/plugins/tmux-resurrect

export PATH="$(ruby -r rubygems -e 'puts Gem.user_dir')/bin:$HOME/.local/bin:$PATH"
```

Keep the PATH line in your shell configuration if those directories are not
already present. An existing resurrect checkout can be selected with:

```bash
export WORK_RESURRECT_DIR=/absolute/path/to/tmux-resurrect
```

No global TPM configuration is required; `work` loads resurrect into each managed
server.

Build and install:

```bash
make -j
make test
make install
work doctor
```

`make install` defaults to `~/.local/bin/work`. You can change this with
`make install PREFIX=/your/prefix`. CMake is also supported:

```bash
cmake -S . -B build-cmake -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build-cmake
ctest --test-dir build-cmake --output-on-failure
cmake --install build-cmake
```

## Define your workspace

```bash
work init multiplexer --root ~/projects/multiplexer
nvim "$(work config multiplexer)"
```

The generated project file looks like this:

```ini
# Values are literal. Do not quote paths or commands.
root = /home/riccardo/projects/multiplexer
editor_sessions = true

[window code]
layout = main-vertical
pane = nvim .
pane =

[window tools]
layout = even-horizontal
pane = git status
pane =
```

Each `[window NAME]` creates a window. Each `pane = COMMAND` creates a pane and
sends the command to its shell. A blank command leaves a shell ready for use.
Commands are intentionally shell commands: variables, pipelines and scripts
work as they would when typed into a terminal.

Layouts: `main-vertical`, `main-horizontal`, `even-vertical`,
`even-horizontal`, or `tiled`.

Global settings must come before the first window. Names use letters, digits,
underscores and hyphens; paths and commands can contain spaces and quotes.
Root paths must be absolute or start with `~/`.

The initial template assumes you have `nvim` and `git`. Edit those commands to
match your environment; the wrapper does not install pane applications.

### Template versus snapshot

The project file defines your starting arrangement. Once a snapshot exists,
`work open` restores that snapshot, including layout changes made interactively.
To use a changed project file instead, when its server is not running:

```bash
work open multiplexer --fresh
```

`--fresh` never restarts a running workspace or deletes its snapshot. If the
workspace is running, it is reused.

## Everyday commands

| Command | Behavior |
| --- | --- |
| `work` | Open the most recently opened project |
| `work open NAME` | Attach, restore or create |
| `work open NAME --no-attach` | Prepare the workspace without attaching |
| `work save NAME` | Save that running project |
| `work save` | Save the current managed project, or the most recent one |
| `work list` | List configured, saved and running projects |
| `work config NAME` | Print the editable project file's path |
| `work doctor` | Check dependencies and show configuration/state paths |

Run interactive opening from a regular terminal. This version keeps projects
on separate servers, so it avoids nesting one tmux session inside another.
Detaching leaves your programs running; exiting every shell closes the session.

Saving on detach runs asynchronously. Use `work save NAME` and wait for success
before intentionally shutting down the machine. There is no periodic background
saving yet.

## Editor sessions and restored processes

`editor_sessions = true` configures resurrect's Vim and Neovim session
strategies. Your editor must also write a `Session.vim` file. For example:

```vim
:mksession! Session.vim
```

You can use [vim-obsession](https://github.com/tpope/vim-obsession) to keep that
session file updated.

Resurrect's default supported-program list is retained. To configure additional
programs, put `resurrect_processes = ...` before the first window, following
[resurrect's process configuration](https://github.com/tmux-plugins/tmux-resurrect/blob/master/docs/restoring_programs.md).
For example, a list can include `nvim vim tail`; supplying a list replaces the
default list, so include the programs you want.

After a reboot, programs are restarted. Arbitrary process memory and unsaved
editor text are not checkpointed by this wrapper.

## Configuration and isolation

Defaults:

```text
~/.config/work/projects/NAME.work
~/.config/work/tmux.conf
~/.local/state/work/last-project
~/.local/state/work/projects/NAME/tmuxinator.yml
~/.local/state/work/projects/NAME/tmux.conf
~/.local/state/work/projects/NAME/resurrect/last
```

`XDG_CONFIG_HOME` and `XDG_STATE_HOME` are respected. `WORK_CONFIG_HOME` and
`WORK_STATE_HOME` override the complete `work` directories, which is useful for
testing.

Because upstream resurrect uses some unquoted file redirections, the state
directory currently must not contain whitespace. Project roots and configuration
directories can contain whitespace.

Each project uses the named socket `work-NAME` in tmux's per-user socket
directory. These socket names are reserved for this tool. Ordinary tmux sessions
on the default server are not loaded or changed.

Put your tmux preferences in `~/.config/work/tmux.conf`. The usual
`~/.tmux.conf` is not automatically sourced: global restore plugins could
otherwise load unrelated sessions. The wrapper owns the `client-detached`
hook and the prefix + `Ctrl-s` binding in managed servers.

Snapshot validation checks that pane identities and directories can be restored.
It does not independently verify every property restored by upstream, such as
application-internal state. Grouped sessions are not a supported workflow in
this first version.

## Code tour

| File | Responsibility |
| --- | --- |
| `src/main.cpp` | CLI, project locks, server setup, attach/restore/create and snapshot checks |
| `src/project.cpp` | Project parser, generated YAML and atomic file writes |
| `src/process.cpp` | Direct subprocess execution, pipe handling and child reaping |
| `include/work/` | Small public interfaces |
| `tests/integration.py` | CLI integration tests using controlled external tools |
| `tests/live_smoke.py` | Optional test against real upstream dependencies |

Subprocess arguments go through `posix_spawnp`, rather than through `system()`.
Shell quoting is used only where tmux itself requires a shell command, such as
its detach hook. Project operations are serialized with `flock`; configuration
and text-state writes use temporary files and atomic renames.

## Validation

`make test` runs 24 subprocess integration tests without requiring tmux. They
cover project isolation, idempotent opening, save/restore routing, malformed
snapshots, failed launches, failed saves, concurrent opening, and the detach
hook's lock ordering.

For a real round trip with tmux, tmuxinator and resurrect installed:

```bash
python3 tests/live_smoke.py ./build/work
```

It creates a temporary project with simple shell panes, adds a pane, saves it,
stops only its own test server, restores it, and compares pane structure, layout,
and active pane/window. It does not use your normal project configuration.

See [VALIDATION.md](VALIDATION.md) for checks completed in the build environment
and the remaining live-test limitation.

## Next increments

- Import an existing tmuxinator project.
- Interactive project picker.
- Periodic saves with explicit intervals.
- Dependency/readiness checks for project services.
- Integration tests across upstream versions and more complete editor tests.

Upstream projects:
[tmuxinator](https://github.com/tmuxinator/tmuxinator),
[tmux-resurrect](https://github.com/tmux-plugins/tmux-resurrect).
