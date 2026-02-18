---
name: st-develop
description: Anything related to coroutines, State Threads (ST), or SRS's concurrency model. Use when discussing coroutine concepts, updating coroutine knowledge (srs-coroutines.md), developing/debugging/porting ST source code, porting ST to new CPU architectures or OSes, debugging coroutine context switching, analyzing ST scheduler behavior, adding new platform assembly, fixing ASAN/Valgrind/SEH issues, or understanding ST internals (sched, stk, sync, key, io, event, context switch ASM).
---

# ST Development

State Threads (ST) is a C coroutine library. Source lives in the SRS repo at `trunk/3rdparty/st-srs/`.

The SRS repo should be at `~/git/srs/`. If it's not there, ask the user to clone it:
```
git clone https://github.com/ossrs/srs.git ~/git/srs
```

## Setup: Load Knowledge Base (MANDATORY)

Before any ST work, use the `read` tool to load the knowledge base. Do NOT use memory_search — read the full file directly.

- `memory/srs-coroutines.md`

## Loading ST Source Code (ON REQUEST)

When the user asks to load the ST codebase (or needs you to work directly with the source), load **ALL** ST source files — no partial loads.

All under `~/git/srs/trunk/3rdparty/st-srs/`:

Headers: `public.h`, `common.h`, `md.h`

Core C: `sched.c`, `stk.c`, `sync.c`, `key.c`, `io.c`, `event.c`, `common.c`

Platform ASM: `md_darwin.S`, `md_linux.S`, `md_linux2.S`, `md_cygwin64.S`

Build: `Makefile`

**Load every single file listed above — no shortcuts, no skipping.**

## Verifying Changes

After any ST code change, run `scripts/verify.sh` from the ST source directory to build and execute unit tests. Always run this before considering a change complete.
