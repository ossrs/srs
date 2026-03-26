---
name: st-develop
description: Anything related to coroutines, State Threads (ST), or SRS's concurrency model. Use when discussing coroutine concepts, updating coroutine knowledge (srs-coroutines.md), developing/debugging/porting ST source code, porting ST to new CPU architectures or OSes, debugging coroutine context switching, analyzing ST scheduler behavior, adding new platform assembly, fixing ASAN/Valgrind/SEH issues, or understanding ST internals (sched, stk, sync, key, io, event, context switch ASM).
---

# ST Development

State Threads (ST) is a C coroutine library. 

## Setup

All files are in the current working directory. Find everything from here — no discovery logic needed.

Available directories: `trunk/`, `cmd/`, `internal/`, `cmake/`, `docs/`, `memory/`

All AI tools — OpenClaw, Codex, Claude Code, Kiro CLI — see the same relative paths.

## Load Knowledge Base (MANDATORY)

Before any ST work, use the `read` tool to load the knowledge base. Do NOT use memory_search — read the full file directly.

- `memory/srs-coroutines.md`
- `memory/srs-codebase-map.md` — Load this when you need to find or navigate source files. Always use the codebase map to reason about which files are relevant before searching. Never grep from the repository root blindly.

## Loading ST Source Code (ON REQUEST)

When the user asks to load the ST codebase (or needs you to work directly with the source), load **ALL** ST source files — no partial loads.

All under `trunk/3rdparty/st-srs/`:

Headers: `public.h`, `common.h`, `md.h`

Core C: `sched.c`, `stk.c`, `sync.c`, `key.c`, `io.c`, `event.c`, `common.c`

Platform ASM: `md_darwin.S`, `md_linux.S`, `md_linux2.S`, `md_cygwin64.S`

Build: `Makefile`

**Load every single file listed above — no shortcuts, no skipping.**

## Unit Tests (utest)

ST has a Google Test-based unit test suite in `trunk/3rdparty/st-srs/utest/`:

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

This script runs unit tests in `trunk/3rdparty/st-srs`.
Always run verification before considering a change complete.
