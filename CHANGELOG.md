# Changelog

All notable changes to bc-concurrency are documented here.

## [1.0.2]

### Changed

- **Worker signaling: pthread_mutex + two condvars instead of
  atomic + futex.** `work_ready` and `work_done` atomics are
  removed; `bc_concurrency_dispatch_to_worker`, `wait_for_worker`,
  and the worker thread routine now coordinate via a single
  per-worker `state_mutex`, a `ready_cond` (main → worker), and a
  `done_cond` (worker → main). `bc_concurrency_foreach_slot`
  additionally locks/unlocks each worker's `state_mutex` to
  re-establish an acquire fence before slot reads.

### Fixed

- **Residual tsan data races on gcc 13 ubuntu-24.04.** The raw
  `syscall(SYS_futex, …)` used previously is not tracked by
  ThreadSanitizer's happens-before model; pthread primitives are.
  This closes the remaining races in downstream consumers
  (bc-hash `walk_parallel_merge_worker_slot`,
  `dispatch_batch_iteration`, `error_collector_flush_to_stderr`).

### Performance

- No measurable regression on bc-hash bench (13 GB / 724 k files
  corpus, warm, 5 runs): sha256 1.85 s (2.35× vs `sha256sum -P16`),
  crc32c 1.02 s (3.98× vs `cksum -P16`), xxh3 1.01 s (3.79× vs
  `xxhsum -P16`), xxh128 0.99 s (3.90×).

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
[1.0.2]: https://github.com/Unmanaged-Bytes/bc-concurrency/releases/tag/v1.0.2
