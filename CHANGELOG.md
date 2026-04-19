# Changelog

All notable changes to bc-concurrency are documented here.

## [1.0.1]

### Fixed

- **Data race on worker slot reads** (tsan): `bc_concurrency_foreach_slot`
  now re-acquires each worker's `work_done` atomic (with
  `memory_order_acquire`) before reading its slot. This ensures
  a tsan-visible happens-before from worker task completion to the
  caller's slot read, even when the acquire chain from
  `dispatch_and_wait` is interrupted by the worker thread resuming
  its main loop. Visible under 2-CPU scheduling with callers that
  store heap pointers in the slot (e.g. bc-hash error collector).

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
[1.0.1]: https://github.com/Unmanaged-Bytes/bc-concurrency/releases/tag/v1.0.1
