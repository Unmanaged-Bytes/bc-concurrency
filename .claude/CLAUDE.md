# bc-concurrency — project context

Concurrency primitives for the `bc-*` ecosystem: MPMC queue-backed
worker pool with configurable worker count, typed dispatch, a shared
context carrying user state across workers, and signal-safe shutdown
coordination.


## Invariants (do not break)

- **No comments in `.c` files** — code names itself. Public `.h`
  may carry one-line contracts if the signature is insufficient.
- **No defensive null-checks at function entry.** Return `false`
  on legitimate failure; never assert in production paths.
- **SPDX-License-Identifier: MIT** header on every `.c` and `.h`.
- **Strict C11** with `-Wall -Wextra -Wpedantic -Werror`.
- **Sanitizers (asan/tsan/ubsan/memcheck) stay green** in CI.
  **tsan is load-bearing** — this library manages threads.
- **cppcheck stays clean**; never edit `cppcheck-suppressions.txt`
  to hide real findings.
- **No shared mutable state across workers without atomics or
  the queue.** The MPMC queue is the only legal cross-thread
  message path.
