# Reproducing the problems

Run these commands from `/home/riccardo/terminal_multiplexer`.
The cases below correspond to the numbered findings in [problems.md](problems.md).
They document the behavior before the fixes; their failure assertions are no
longer expected to hold. To check the fixed behavior, run the regression tests:

```bash
make test
```

They use the existing fake dependencies and temporary configuration/state
directories, so no installed tmux, tmuxinator, plugin, or personal workspace is
needed. Temporary files are removed automatically when each case finishes.

Build first:

```bash
make -j
```

## Runnable reproduction of all four findings

Paste this entire command into a terminal from the repository directory:

```bash
python3 - <<'PY'
import json
import sys

sys.path.insert(0, 'tests')
import integration


def reproduce(number, action):
    case = integration.Integration()
    case.setUp()
    try:
        print(f'\nCase {number}', flush=True)
        action(case)
    finally:
        case.doCleanups()


def broken_config_save(case):
    case.init()
    case.start()
    config = case.config / 'projects/demo.work'
    config.write_text(config.read_text() + 'invalid_setting = yes\n')
    print(case.call('save', 'demo', code=1).strip())
    assert not case.snapshot().exists()
    assert not any(call['tool'] == 'save' for call in case.calls())
    print('Confirmed: saving failed before the plugin ran; no snapshot exists.')


def broken_config_path(case):
    case.init()
    config = case.config / 'projects/demo.work'
    config.write_text(config.read_text() + 'invalid_setting = yes\n')
    print(case.call('config', 'demo', code=1).strip())
    assert config.is_file()
    print('Confirmed: the configuration exists, but its path command fails.')


def trailing_space_root(case):
    root = case.base / 'root-ending-in-space '
    root.mkdir()
    print(case.call('init', 'spaces', '--root', str(root)).strip())
    print(case.call('open', 'spaces', '--no-attach', code=1).strip())
    assert root.is_dir()
    assert not root.with_name(root.name.rstrip()).exists()
    print('Confirmed: init accepted the root; open looked for a trimmed path.')


def truncated_pane_output(case):
    case.init()
    case.start()
    state_file = case.fake_state / 'work-demo.json'
    state = json.loads(state_file.read_text())
    state['panes'] = [
        ['demo', 0, index, str(case.root), ''] for index in range(3000)
    ]
    state_file.write_text(json.dumps(state))
    output = ''.join(
        '\t'.join(map(str, pane[:4])) + '\n' for pane in state['panes']
    )
    assert len(output.encode()) > 65536
    print(f'Fixture list-panes output: {len(output.encode())} bytes.')
    print(case.call('save', 'demo', code=1).strip())
    assert any(call['tool'] == 'save' for call in case.calls())
    assert not case.snapshot().exists()
    print('Confirmed: the plugin ran, but truncated output made saving fail.')


reproduce(1, broken_config_save)
reproduce(2, broken_config_path)
reproduce(3, trailing_space_root)
reproduce(4, truncated_pane_output)
PY
```

`case.call(..., code=1)` asserts that the CLI exits with an error. If a finding
has been fixed, that assertion may fail because the command now succeeds.

## 1. Saving after an invalid configuration edit

**How it occurs:** A user opens a workspace, then edits its `.work` file and
introduces an unknown setting. Saving the already running workspace still
parses that file first. A detach hook calls the same save command, so it is
also affected.

**Expected current result:** `work save demo` exits with code 1 and an
`Unknown window setting: invalid_setting` error. The save plugin is never
invoked. This reproduction starts without a snapshot, so none is created.
With an existing snapshot, the latest workspace changes would remain unsaved.

**Desired behavior:** A running workspace remains saveable while its startup
configuration is being edited.

## 2. Locating a configuration that needs repair

**How it occurs:** After making a syntax error, a user tries the documented
`work config demo` command to find the file and repair it.

**Expected current result:** Code 1 and the same parsing error, despite the
file existing. In `nvim "$(work config demo)"`, command substitution produces
no configuration path on stdout, so the editor does not receive the intended
filename.

**Desired behavior:** The command prints the existing configuration path even
when its contents are invalid.

## 3. A root directory whose name ends in a space

**How it occurs:** A valid Linux directory has a trailing space in its name.
The user passes its exact, quoted path to `work init ... --root ...`.
Initialization writes it literally, but opening trims the configuration value.

**Expected current result:** Initialization succeeds; opening exits with code 1
and `Project root does not exist`, showing the path without its final space.
The reproduction verifies that the original directory does exist. If both
versions of the directory existed, the trimmed version would be used instead.

**Desired behavior:** Preserve the exact root, or reject it during initialization
with a clear explanation.

## 4. Pane listings larger than the capture limit

**How it occurs:** Many panes, or many long working directory paths, cause
`tmux list-panes` output to exceed 65,536 bytes. The subprocess helper discards
the beginning of the output even though the save code needs every record.

The reproduction directly expands the fake server's state to 3,000 panes.
It checks the output size before saving; it does not create real terminals.

**Expected current result:** The save plugin runs, but validation fails and
the new `last` snapshot is removed by rollback. In the tested fixture this
reports `Unsupported tab in a pane name or directory.` because truncation
begins partway through a record. Different record lengths may instead produce
`A saved pane no longer exists.` or a pane verification error.

**Desired behavior:** Structured output is preserved completely, or an explicit
capture-limit error is reported without parsing partial data.

## Validation scope

All four fixture reproductions have been exercised during review. Case 4
confirms the save-side failure; real tmux behavior and the restore-side failure
described in `problems.md` were not exercised here. The fixtures do not execute
real detach hooks or validate real tmux command parsing. Use `make test` to run
the separate existing integration suite.
