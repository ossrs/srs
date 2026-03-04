---
name: st-develop
description: Anything related to coroutines, State Threads (ST), or SRS's concurrency model. Use when discussing coroutine concepts, updating coroutine knowledge (srs-coroutines.md), developing/debugging/porting ST source code, porting ST to new CPU architectures or OSes, debugging coroutine context switching, analyzing ST scheduler behavior, adding new platform assembly, fixing ASAN/Valgrind/SEH issues, or understanding ST internals (sched, stk, sync, key, io, event, context switch ASM).
---

# ST Development

State Threads (ST) is a C coroutine library. Source lives in `trunk/3rdparty/st-srs/` inside the SRS repo.

Default SRS repo path is `~/git/srs`, but do **not** hardcode this path.
Always resolve `SRS_ROOT` dynamically:

1. If `SRS_ROOT` env is set and contains `trunk/3rdparty/st-srs`, use it.
2. Else, if current workspace (or its git root) contains `trunk/3rdparty/st-srs`, use that.
3. Else, if `~/git/srs/trunk/3rdparty/st-srs` exists, use `~/git/srs`.
4. Else, ask the user for the SRS repo root.

All ST source paths below are relative to `$SRS_ROOT`.

## Setup: Load Knowledge Base (MANDATORY)

Before any ST work, use the `read` tool to load the knowledge base. Do NOT use memory_search — read the full file directly.

- `memory/srs-coroutines.md`

## Loading ST Source Code (ON REQUEST)

When the user asks to load the ST codebase (or needs you to work directly with the source), load **ALL** ST source files — no partial loads.

All under `$SRS_ROOT/trunk/3rdparty/st-srs/`:

Headers: `public.h`, `common.h`, `md.h`

Core C: `sched.c`, `stk.c`, `sync.c`, `key.c`, `io.c`, `event.c`, `common.c`

Platform ASM: `md_darwin.S`, `md_linux.S`, `md_linux2.S`, `md_cygwin64.S`

Build: `Makefile`

**Load every single file listed above — no shortcuts, no skipping.**

## Unit Tests (utest)

ST has a Google Test-based unit test suite in `$SRS_ROOT/trunk/3rdparty/st-srs/utest/`:

- `st_utest.cpp` / `st_utest.hpp` — Test main and shared helpers
- `st_utest_coroutines.cpp` — Coroutine tests (start, params, multiple coroutines, addition across yields)
- `st_utest_tcp.cpp` — TCP connection test
- `gtest-fit/` — Embedded Google Test framework

**Build targets** (in the ST Makefile):
- `darwin-debug-utest` — macOS debug build + utest
- `linux-debug-utest` — Linux debug build + utest
- `cygwin64-debug-utest` — Cygwin64 debug build + utest

Coverage variants: `darwin-debug-gcov`, `linux-debug-gcov` (adds `-fprofile-arcs -ftest-coverage`).

The build compiles ST as a static library first, then builds and links the utest binary at `obj/st_utest`.

## Verifying Changes

After any ST change (including utest-only changes), run the verifier script in this skill folder (not in the ST codebase):

- `scripts/verify.sh`

This script must resolve `SRS_ROOT` dynamically and run unit tests in `$SRS_ROOT/trunk/3rdparty/st-srs`.
Always run verification before considering a change complete.
