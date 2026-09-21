# SMTP library tests

The suite uses the existing Unity framework in `harness/`. The framework,
Makefile, CI workflows, and report generator are unchanged.

Build and run:

```sh
make -B test
make check
```

`-B` forces a rebuild, including changes to test headers with the existing
Makefile. Each suite groups tests by the function being exercised. Error cases
appear in the final `Exception testing` region and run after ordinary cases.

## Files

- `lab-test.c`: shared runner, fixture reset, and allocation cleanup checks.
- `protocol-test.c`: reply parsing, command construction, dot stuffing, DATA
  payloads, limits, invalid arguments, injection rejection, and library failures.
- `session-test.c`: buffered reads, partial writes, multiline replies, exact
  command ordering, wrong codes at all seven stages, and failures at every
  reply byte, outgoing byte, allocation, and formatting call.
- `socket-test.c`: resolver arguments, address fallback, socket options,
  resource cleanup, interrupted operations, partial transfers, and socket errors.
- `support-test.c` and `test-support.h`: in-memory server and deterministic
  library/system-call fakes.
- `test-hooks.h`: substitutions enabled only inside application files compiled
  with `TEST`. Release and ordinary debug builds use the real system functions.

## Isolation and failure checks

No test opens a real socket or contacts a mail server. The session tests supply
read/write callbacks. The ordered conversation refuses reads before the expected
request and rejects incorrect request bytes. Separate stream cases exercise
coalesced replies and reads split at arbitrary byte boundaries.

Socket tests execute the actual adapter logic with fake resolver and socket
operations. They verify call arguments and descriptor cleanup without DNS or
network access. These unit tests do not replace a real transport integration
check on the required course platforms.

Application allocations are registered by the test-only allocator. Every test
must release all of them; teardown detects leaks and then frees leftovers even
after a failed assertion. Allocation and formatting failures are injected at
each call in a complete session. Error branches are tested rather than excluded
from coverage.

## Sanitizers and coverage

```sh
make -B debug-test
make leak-test
make report
```

Run the leak target on a platform with LeakSanitizer support, such as the course
Linux environments. Apple's AddressSanitizer does not support `detect_leaks=1`.
On that platform, bounds/use-after-free checks can still run with:

```sh
ASAN_OPTIONS=detect_leaks=0 ./build/debug-test/myapp_td
```

Allocation-balance assertions remain enabled in both builds. They supplement
AddressSanitizer; they are not a replacement for Linux LeakSanitizer.

The existing coverage target requires `gcovr` and a coverage reader matching the
compiler. For Apple Clang, LLVM's reader is available through
`xcrun llvm-cov gcov`. The library coverage scope is `protocol.c`, `session.c`,
`socket_transport.c`, and `lab.c`. `main.c` is excluded by the existing Makefile;
these are library unit tests, not a claim of command-line coverage.
