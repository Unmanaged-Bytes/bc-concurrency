# Changelog

All notable changes to bc-concurrency are documented here.

## [1.0.0]

Initial public release.

### Added

- **Worker pool** (`bc_concurrency.h`): MPMC queue-backed worker
  pool with a configurable worker count and per-slot lifetime
  callbacks.
- **Dispatch / context**: typed work submission and a shared
  context carrying user state across workers.
- **Signal handling** (`bc_concurrency_signal.h`): signal-safe
  shutdown coordination for the worker pool.

### Quality

- Unit tests under `tests/`, built with cmocka.
- Sanitizers (asan / tsan / ubsan / memcheck) pass.
- cppcheck clean on the project sources.
- MIT-licensed, static `.a` published as Debian `.deb` via GitHub
  Releases.

[1.0.0]: https://github.com/Unmanaged-Bytes/bc-concurrency/releases/tag/v1.0.0
