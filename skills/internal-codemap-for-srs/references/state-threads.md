# State Threads Code Map

Use this reference after `skills/internal-codemap-for-srs/SKILL.md` routes a task to State Threads (ST), the C coroutine library used by the C++ media server. The Go server does not use ST.

## Repository Boundary

- Authoritative repository: `https://github.com/ossrs/state-threads`
- Project-root-relative checkout path: `state-threads/`
- Check this path directly before using the route. It may be a directory or a symlink to the user's preferred checkout. If it is unavailable, stop and ask the user to make the `https://github.com/ossrs/state-threads` checkout available there; do not create it automatically or search for another checkout.
- Keep the SRS repository as the current working directory. Use `git -C state-threads/ ...` for Git operations and paths beginning with `state-threads/` for file operations. Never replace this relative path with the symlink target or an absolute path.
- Branches: `srs` is the maintained default branch. `master` and `st-1.9` are the legacy upstream 1.9 line; do not develop on them.

Before editing, verify both repositories are clean and record their branches and commits:

```bash
git status --short --branch
git rev-parse HEAD
git -C state-threads/ status --short --branch
git -C state-threads/ rev-parse HEAD
```

## Source of Truth and the SRS Mirror

`state-threads/` is the ST code. `trunk/3rdparty/st-srs/` is only a vendored mirror that the SRS build compiles; never read, search, or edit it for ST work. Change `state-threads/`, then sync the mirror with the project-root scripts:

- `./scripts/copy-from-st.sh` — Overwrite the mirror with `state-threads/`; the normal direction after an ST change.
- `./scripts/copy-to-st.sh` — Overwrite `state-threads/` with the mirror; only when the user asks.

## Library Source

- `public.h` — Public API, installed as `st.h`. SRS code includes only this header.
- `common.h` — Internal types: thread, stack, clist, poll queue, virtual processor, and the per-event-system dispatch table.
- `common.c` — Shared internals.
- `md.h` — Platform and CPU detection, the `jmpbuf` layout, and the context-switch macros per architecture.
- `md_linux.S` — Linux context-switch assembly for i386 and x86_64.
- `md_linux2.S` — Linux context-switch assembly for arm, aarch64, mips, mips64, loongarch64, and riscv.
- `md_darwin.S` — macOS context switch for x86_64 and Apple Silicon aarch64.
- `md_cygwin64.S` — Windows (Cygwin64) x86_64 context switch.
- `sched.c` — Scheduler: `st_init`, `st_destroy`, thread create, exit, join, interrupt, the idle thread, and the timeout heap described in `docs/timeout_heap.txt`.
- `stk.c` — Stack allocation, the free-stack list, and `MALLOC_STACK`.
- `sync.c` — Time functions and the time cache, sleep, condition variables, and mutexes.
- `key.c` — Thread-specific data (`st_key_create`, `st_thread_setspecific`).
- `io.c` — Non-blocking socket and file I/O, `st_netfd_t`, and read/write/accept/connect/sendmsg with timeouts.
- `event.c` — Event systems: select, poll, kqueue, and epoll.
- `libst.def`, `st.pc.in`, `st.spec` — Library export, pkg-config, and packaging metadata.

## Build

- `Makefile` — Targets `darwin-debug`, `darwin-optimized`, `linux-debug`, `linux-optimized`, and `cygwin64-debug`, which write to `<OS>_<release>_<DBG|OPT>/` and point `obj` at it. The comment block before `CFLAGS` documents every optional macro, such as `MD_VALGRIND`, `MD_ASAN`, `MALLOC_STACK`, `DEBUG_STATS`, `MD_HAVE_EPOLL`, and `MD_OSX_NO_CLOCK_GETTIME`, passed through `EXTRA_CFLAGS`.
- `cmake/CMakeLists.txt` — IDE build of the library and utest for CLion.

SRS builds its copy from `trunk/auto/depends.sh` in the "state-threads" section. It copies `trunk/3rdparty/st-srs/` into `trunk/objs/<platform>/st-srs/`, always builds the debug target with `-DMALLOC_STACK`, and adds `MD_VALGRIND`, `MD_ASAN`, `-fsanitize=address`, `DEBUG_STATS`, epoll, or clock macros from its configure options. It reuses `trunk/objs/<platform>/3rdparty/st/libst.a` when that file exists, so delete `trunk/objs/<platform>/3rdparty/st/` before configuring SRS to rebuild a changed ST.

## Tests and Verification

`utest/` holds the Google Test suite, built into `obj/st_utest`:

- `st_utest.cpp`, `st_utest.hpp` — Test main and shared helpers.
- `st_utest_coroutines.cpp` — Coroutine start, parameters, and switching.
- `st_utest_tcp.cpp` — TCP connect and I/O.
- `st_utest_learn_kb.cpp` — Behavior tests for stacks and yields, join and exit, mutexes, condition variables, interrupts, netfd I/O and timeouts, and event-system selection.
- `Makefile` — Builds the library and the utest binary; `UTEST_FLAGS` adds compiler and linker flags.

Run from the SRS project root so paths stay relative. Build outputs are ignored by Git.

```bash
# macOS
make -C state-threads/ darwin-debug-utest && (cd state-threads && ./obj/st_utest)

# macOS with the stack mode SRS uses
make -C state-threads/ clean && make -C state-threads/ darwin-debug-utest EXTRA_CFLAGS="-DMALLOC_STACK" && (cd state-threads && ./obj/st_utest)

# macOS with ASAN; the utest link needs the sanitizer too
make -C state-threads/ clean && make -C state-threads/ darwin-debug-utest \
    EXTRA_CFLAGS="-DMALLOC_STACK -DMD_ASAN -fsanitize=address -fno-omit-frame-pointer" \
    UTEST_FLAGS="-fsanitize=address" && (cd state-threads && ./obj/st_utest)

# Linux in the SRS development image
docker run --rm -v "$(pwd)/state-threads":/st -w /st ossrs/srs:ubuntu20 \
    bash -c 'make linux-debug-utest && ./obj/st_utest'
```

Use `linux-debug-gcov` or `darwin-debug-gcov`, then `auto/coverage.sh`, for a coverage report. `auto/fast.sh` rebuilds and reruns the utest with coverage.

`tools/` holds the integration tests: standalone programs that link `obj/libst.a` through `st.h` as SRS does, each in its own folder with a `Makefile`, and each exits non-zero on failure. The new tools share `tools/tool.h`, which has the `CHECK` macro, the event system setup, and loopback helpers. The tools:

- `helloworld` — sleeps in a loop.
- `verify` — a coroutine, sleep, mutex, condition variable, and join.
- `porting` — prints the OS and CPU macros, and fails on a pair `md.h` does not support.
- `backtrace` — `backtrace()` unwinds from a coroutine stack.
- `lifecycle` — event system choice, primordial stack, `st_init`, descriptor limit, and `st_destroy`.
- `thread` — create, join, exit, detach, yield, interrupt, stack options, switch callbacks, and the DEBUG functions.
- `key` — coroutine specific data and its destructors.
- `sync` — condition variables and mutexes.
- `time` — the clock, sleeps, the time cache, and a custom clock.
- `tcp` — a TCP echo with every read and write call, timeouts, and netfd data, over IPv4 and IPv6.
- `udp` — a UDP echo with `st_recvfrom`, `st_sendto`, `st_recvmsg`, and `st_sendmsg`, over IPv4 and IPv6.
- `unix` — Unix stream and datagram sockets, and socketpairs.
- `pipe` — the SRS signal pipe, a full pipe, and `st_open` on a FIFO and a file.
- `poll` — `st_poll` and `st_netfd_poll`.
- `stress` — many connections and coroutines at once, stack reuse, and contention.

`auto/tools.sh` rebuilds the library with `EXTRA_CFLAGS` and runs every tool folder under both event systems, select and kqueue or epoll. Run it after every utest run, on each platform, in the ST default build and the SRS build:

```bash
# macOS
(cd state-threads && ./auto/tools.sh && EXTRA_CFLAGS=-DMALLOC_STACK ./auto/tools.sh)

# Linux in the SRS development image
docker run --rm -v "$(pwd)/state-threads":/st -w /st ossrs/srs:ubuntu20 \
    bash -c './auto/tools.sh && EXTRA_CFLAGS=-DMALLOC_STACK ./auto/tools.sh'
```

CI:

- `.github/workflows/test.yml` — Runs the Linux utest and coverage directly on an Ubuntu runner on push and pull request, and uploads the `gcovr` report to Codecov.
- `Dockerfile.test`, `Dockerfile.cov`, `auto/codecov.sh` — The former CentOS 7 CI images and Codecov bash uploader; CI no longer uses them.

## SRS Consumers

Only `trunk/src/protocol/srs_protocol_st.cpp` includes `st.h` and calls ST in the server; all other code goes through its wrappers. Check these when an ST API or behavior changes:

- `trunk/src/protocol/srs_protocol_st.hpp`, `.cpp` — ST initialization, `srs_*` thread, sleep, mutex, condition-variable, and netfd wrappers, and TCP and UDP helpers.
- `trunk/src/kernel/srs_kernel_st.hpp`, `.cpp` — Coroutine interfaces (`ISrsCoroutine`, `ISrsCoroutineHandler`, `ISrsCond`).
- `trunk/src/app/srs_app_st.hpp`, `.cpp` — Coroutine implementations (`SrsSTCoroutine`, `SrsFastCoroutine`, `SrsDummyCoroutine`).
- `trunk/src/utest/srs_utest_manual_st.cpp` — Human-maintained SRS tests of the ST wrappers; add AI tests to the `srs_utest_ai*` files instead, as `references/testing.md` describes.
