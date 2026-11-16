# Historical failure reproductions

The Python reproduction scripts and their test fixtures are maintained on the
`integration/updates` branch. Use that branch to read the full reproduction
instructions and run `make test` to check the fixed behavior.

The original scripts assert behavior from before the fixes and are expected to
fail after those fixes. [problems.md](problems.md) records the historical review
findings and validation limits.
