# Validation of the first version

Completed in the build environment:

- Built with GCC 13.3 using C++20 and warning flags.
- Configured and built with CMake 3.28.3.
- Installed into an isolated test prefix using CMake.
- Passed 24 subprocess integration tests using controlled tmux, tmuxinator and
  resurrect fixtures.
- Examined the real upstream script interfaces, including snapshot fields,
  completion hooks, unchanged-save behavior and timestamp naming.

Real dependencies were prepared separately:

- tmux 3.4
- tmuxinator 3.1.0
- tmux-resurrect commit cff343c

The real smoke test was attempted, but this execution environment rejects
tmux's Unix socket connections with “Operation not permitted,” including in a
fresh private socket directory. Therefore a complete round trip with real
tmux sessions has **not** been verified here. The controlled tests do not
substitute for that check.

Run the provided test on Fedora after installing the dependencies:

```bash
python3 tests/live_smoke.py ./build/work
```

It uses a temporary configuration, state directory and private socket location.
Its project root includes spaces and quotes. It compares pane structure,
directories, window layouts and active selections after saving and restoring.

Not yet verified: real editor-session restoration, interactive tmux behavior
on a normal desktop terminal, or compatibility across multiple upstream
versions.
