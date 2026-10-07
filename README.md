# work

A Linux workspace manager built with C++20, tmuxinator, and tmux-resurrect.
`work` gives each project its own tmux server and saved workspace. Opening a
project reconnects to its running workspace, restores its snapshot, or creates
its configured windows and panes.

## Requirements

- Linux and a C++20 compiler.
- Make, or CMake 3.20 or newer.
- tmux, tmuxinator, Bash, and a tmux-resurrect checkout at runtime.
- Python 3 for the integration tests.

Pane applications such as Neovim and Git must be installed separately.

## Installation

### Fedora dependencies

```bash
sudo dnf install gcc-c++ make tmux ruby rubygems git python3
gem install --user-install tmuxinator
git clone https://github.com/tmux-plugins/tmux-resurrect \
  ~/.tmux/plugins/tmux-resurrect

export PATH="$(ruby -r rubygems -e 'puts Gem.user_dir')/bin:$HOME/.local/bin:$PATH"
```

Add the PATH setting to your shell configuration. If you already have a
resurrect checkout, select it with:

```bash
export WORK_RESURRECT_DIR=/absolute/path/to/tmux-resurrect
```

`work` loads resurrect into each managed server; global TPM setup is unnecessary.

### Build and install

Run from the repository directory:

```bash
make -j
make test
make install
work doctor
```

The binary is built at `build/work` and installed to `~/.local/bin/work`.
To change the installation prefix, use `make install PREFIX=/your/prefix`.
You can also run `./build/work` directly without installing it.

For CMake, install CMake separately and run:

```bash
cmake -S . -B build-cmake -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build-cmake
ctest --test-dir build-cmake --output-on-failure
cmake --install build-cmake
```

## Quick start

Choose an existing project directory:

```bash
work init demo --root "$HOME/projects/demo"
nvim "$(work config demo)"
work open demo
```

The generated template starts Neovim, runs `git status`, and opens two additional
shell panes. Edit the pane commands before opening if you use other tools.

Detach with the default tmux shortcut, **Ctrl-b, then d**. Your programs keep
running, and detaching triggers an asynchronous save. Run `work` to reopen the
current managed project or, outside it, the most recently opened project.

To save explicitly, use `work save demo`, or press the tmux prefix followed by
**Ctrl-s**. Wait for a successful manual save before shutting down your machine.

## Commands

| Command | Behavior |
| --- | --- |
| `work init NAME [--root DIRECTORY]` | Create a configuration; the root defaults to the current directory |
| `work` or `work open [NAME]` | Reconnect, restore, or create, then attach |
| `work open [NAME] --no-attach` | Prepare the workspace without attaching |
| `work open [NAME] --fresh` | Use the configuration when the project's server is stopped |
| `work save [NAME]` | Save a running workspace |
| `work save [NAME] --quiet` | Save without printing the success message |
| `work kill [NAME]` | Stop the project's tmux server and its panes without saving |
| `work list` | List configured projects as `configured`, `saved`, or `running` |
| `work config NAME` | Print the existing configuration file's path |
| `work doctor` | Check dependencies and display configuration/state paths |
| `work --help` | Show CLI usage |
| `work --version` | Show the version |

When `open`, `save`, or `kill` omits the name, `WORK_PROJECT` takes precedence over
the stored last project. `work init` refuses to overwrite an existing configuration.

Interactive opening requires a regular terminal outside tmux. Use `--no-attach`
for headless preparation. Saving works from inside tmux.

`work kill NAME` keeps the project's configuration and existing snapshots. To
save the latest workspace before stopping it, run:

```bash
work save demo && work kill demo
```

Killing works even if the configuration is invalid or missing, and requires only
tmux. It reports an error if the project's server is already stopped.

### Configuration versus snapshot

Opening follows this order:

1. Reuse the running project server.
2. Restore its saved snapshot if one exists.
3. Create the initial layout from its configuration.

Edits to startup pane commands and layouts apply when creating a workspace.
To use the configuration instead of a snapshot, run `work kill NAME`, then
`work open NAME --fresh`.
`--fresh` reuses a running server and retains the previous snapshot until a
subsequent save replaces it.

Opening requires a valid configuration, including when reconnecting or restoring.
Manual and detach saves continue to work if the running project's configuration
is invalid or temporarily missing. `work config NAME` can locate an existing
file even when its contents need repair.

## Project configuration

Each project is defined in `projects/NAME.work` under the configuration directory:

```ini
# Values are literal; do not wrap the entire value in quotes.
root = /home/user/projects/demo
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

Global settings go before the first window. Each window needs at least one
`pane` line; a blank value opens a shell. Commands are sent to the pane's shell
and may contain normal shell quoting, variables, pipelines, and scripts.

| Setting | Meaning |
| --- | --- |
| `root` | Required absolute project path, or a path beginning with `~/` |
| `editor_sessions` | `true` or `false`; defaults to `true` |
| `resurrect_processes` | Optional upstream process-matching configuration |
| `[window NAME]` | Start a named window section |
| `layout` | Defaults to `main-vertical` |
| `pane` | Repeat for each pane and its startup command |

Supported layouts are `main-vertical`, `main-horizontal`, `even-vertical`,
`even-horizontal`, and `tiled`.

Project and window names contain 1–48 letters, digits, underscores, or hyphens,
starting with a letter or digit. Paths may contain internal spaces and quotes.
Values are trimmed; roots ending in spaces or tabs are unsupported, and
initialization rejects them. Roots containing newlines are also rejected.
Lines beginning with `#` are comments; inline comments are not parsed.

### Editors and restored programs

`editor_sessions = true` enables resurrect's Vim and Neovim session strategies.
Your editor must also write a `Session.vim` file, for example:

```vim
:mksession! Session.vim
```

[vim-obsession](https://github.com/tpope/vim-obsession) can keep that file updated.

To customize restored programs, add `resurrect_processes` before the first
window, following [resurrect's process configuration](https://github.com/tmux-plugins/tmux-resurrect/blob/master/docs/restoring_programs.md):

```ini
resurrect_processes = nvim vim tail
```

A supplied list replaces the upstream default list. After a reboot, supported
programs are restarted; arbitrary process memory and unsaved editor text are
not checkpointed.

## Storage and tmux isolation

Default locations:

| Path | Purpose |
| --- | --- |
| `~/.config/work/projects/NAME.work` | Editable project definition |
| `~/.config/work/tmux.conf` | Shared tmux preferences |
| `~/.local/state/work/last-project` | Most recently opened project |
| `~/.local/state/work/projects/NAME/tmuxinator.yml` | Generated startup definition |
| `~/.local/state/work/projects/NAME/tmux.conf` | Generated project tmux configuration |
| `~/.local/state/work/projects/NAME/resurrect/last` | Latest snapshot reference |

`XDG_CONFIG_HOME` and `XDG_STATE_HOME` change the base directories; `work` is
appended to each. `WORK_CONFIG_HOME` and `WORK_STATE_HOME` override the complete
application directories. XDG overrides must be absolute paths.

The state path must contain no whitespace because of upstream resurrect's file
handling. Configuration directories may contain whitespace.

Each project reserves a named tmux socket, `work-NAME`. Operations target that
project's server. Put shared preferences in the application's `tmux.conf`;
the usual `~/.tmux.conf` is not automatically sourced. `work` owns the
`client-detached` hook and prefix + `Ctrl-s` binding in managed servers.
Edit the `.work` file and shared preferences rather than generated state files.

## Troubleshooting and limits

- **Missing dependencies:** Run `work doctor`, check PATH, and verify
  `WORK_RESURRECT_DIR` if using a custom plugin location.
- **Invalid configuration:** Run `work config NAME` to locate and repair it.
  A running workspace can still be saved while you edit.
- **Missing saved working directory:** Restore requires saved directories to
  exist. Recreate them, or use `--fresh` to start from the configuration.
- **State path contains whitespace:** Set `WORK_STATE_HOME` to a suitable path.
  Existing snapshots remain at their old location unless you move them.

Detaching leaves programs running; exiting every shell closes the session.
There is no periodic background save. Failed saves attempt to preserve the
previous text snapshot. Validation checks pane identities and saved directory
availability, without independently verifying application-internal state.
Grouped tmux sessions are unsupported.

## Development and validation

```bash
make -j
make test
```

The 39 integration tests use controlled external tools and temporary directories.
They cover project isolation, concurrent opening, save/restore failures,
malformed snapshots, detach saves, invalid or missing configuration, and a
7,000-pane save/restore with listings larger than 64 KiB. Kill coverage includes
project isolation, name selection, locking, command failures, and preservation
of configuration and snapshots.

For a round trip with real dependencies installed:

```bash
python3 tests/live_smoke.py ./build/work
```

The live test creates an isolated project, adds a pane, saves, uses `work kill`
to stop its own server, restores, and compares directories, pane structure,
layouts, and active selections. See [VALIDATION.md](VALIDATION.md) for recorded
results and remaining editor, desktop, and upstream-version testing gaps.

| File | Responsibility |
| --- | --- |
| `src/main.cpp` | CLI, locks, tmux setup, workspace lifecycle, snapshot checks |
| `src/project.cpp` | Configuration parsing, YAML generation, atomic writes |
| `src/process.cpp` | Subprocess execution, output capture, child handling |
| `include/work/` | C++ interfaces |
| `tests/integration.py` | Integration tests with controlled dependencies |
| `tests/live_smoke.py` | Real dependency smoke test |

Project operations use per-project locks; configuration and text-state writes
use temporary files and atomic renames. Subprocesses receive argument vectors
directly, with shell quoting where tmux requires shell commands.

The status of previously reviewed issues is recorded in
[problems.md](problems.md). [reproduce.md](reproduce.md) documents the original
failure scenarios before those fixes.

Upstream projects: [tmuxinator](https://github.com/tmuxinator/tmuxinator) and
[tmux-resurrect](https://github.com/tmux-plugins/tmux-resurrect).
