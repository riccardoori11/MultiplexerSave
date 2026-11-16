# Historical failure reproductions

The Python reproduction scripts and test fixtures have been removed from the
repository. The full reproduction instructions and tests remain in git history
at commit `1e2336f`; read them with `git show 1e2336f:reproduce.md`.

The original scripts assert behavior from before the fixes and are expected to
fail after those fixes. [problems.md](problems.md) records the historical review
findings and validation limits.
