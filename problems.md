# Code review findings — current status

Reviewed on 2026-10-07 against the current working tree.
All four previously reported issues are resolved for their original reproduction
cases. No unresolved defect was found in this focused review of those fixes.
Source code was not changed during this review.

## 1. Invalid configuration prevented saving — resolved

Previously, a typo in the startup configuration blocked saving an already
running workspace, including saves triggered by detaching.

`save_project()` now validates the project name, obtains its lock, checks the
running server and prepares the snapshot directory without parsing the startup
configuration (`src/main.cpp:248`). Saving also works when the configuration
file is empty or missing. Snapshot path validation and project isolation remain.

Verified by `test_save_ignores_invalid_or_missing_config` and
`test_detach_hook_saves_with_invalid_config` in `tests/integration.py`.
The manual-save test also checks that generated configuration and the other
project's server are unchanged.

## 2. Broken configuration could not be located — resolved

Previously, `work config NAME` parsed the file before printing its path,
preventing users from locating a file that needed repair.

The command now uses `existing_project_file()` (`src/main.cpp:433`;
`src/project.cpp:109`). This checks the project name and requires an existing
regular file without parsing its contents.

Verified by `test_config_path_ignores_invalid_contents` and
`test_config_requires_existing_file_and_valid_name`. Invalid, incomplete and
empty files return their paths; missing files, directories and invalid names
still produce errors.

## 3. Initialization accepted roots that would be trimmed — resolved by rejection

Previously, initialization accepted a directory ending in whitespace, but
opening the project removed that whitespace and used a different path.

Initialization now checks the canonical root and rejects trailing whitespace
before creating the project configuration or acquiring a project lock
(`src/main.cpp:451`). The error explains that configuration values are trimmed.

Verified by `test_init_rejects_root_with_trailing_whitespace`, covering trailing
spaces and tabs both with and without a matching trimmed directory. The test
checks that no project configuration or project state is created.

Remaining format limitation: trailing-whitespace roots are unsupported. The
parser still trims manually edited values, and this guard does not repair
project files created before the fix.

## 4. Structured command output was truncated — resolved

Previously, all captured output was limited to its final 65,536 bytes, including
pane listings consumed as structured data.

`run()` now distinguishes diagnostic, complete and inherited output capture.
Only diagnostic capture is truncated (`src/process.cpp:112`). Pane listings
used by `live_panes()` and `normalize_snapshot_directories()` explicitly request
complete capture (`src/main.cpp:174` and `src/main.cpp:184`). Server-context and
save-hook queries also request complete output.

Verified by `test_large_workspace_save_and_restore_preserves_all_panes`.
It creates 7,000 fixture panes, checks that both listing formats exceed 65,536
bytes, then verifies all saved records and all restored panes. The snapshot
also remains unchanged after restoration.

## Validation and remaining limits

- Current review: `make test` passed all 32 integration tests.
- These checks use isolated temporary configuration/state paths and controlled
  external tools. The detach test executes the generated save command through
  the fixture; it does not reproduce real tmux's asynchronous detach behavior.
- `VALIDATION.md` records an earlier successful live smoke test outside the
  sandbox. That result was not rerun during this review.
- Real tmux tests for the large workspace and invalid-configuration detach cases,
  real editor restoration, desktop interaction and compatibility across upstream
  versions remain unverified here.
- [reproduce.md](reproduce.md) preserves the original failure scenarios. Its
  script asserts the old behavior and is expected to fail after these fixes;
  use `make test` to validate the current behavior.
