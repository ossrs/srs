# SRS Coroutines

SRS uses **State Threads (ST)** — a C coroutine library that provides lightweight user-space threads. It is the cornerstone of SRS's architecture.

**Key insight:** ST gives SRS the programming model of Go (one coroutine per connection, sequential code, state in local variables) but in C/C++. It's essentially a C implementation of Go's concurrency model, used by SRS's C++ codebase.

This is why the code is maintainable despite handling thousands of concurrent connections — each connection handler reads like a simple sequential function.

## Why a Media Server Needs to Manage State per Connection

A media server must serve many connections simultaneously — thousands of RTMP, HTTP, WebRTC clients at once. Each connection has **state**: handshake data, protocol parameters, stream URLs, buffers. This state lives in local variables and function call stacks.

Example: An RTMP client connects → you accept the TCP connection → do RTMP handshake (read bytes, generate response, store handshake state) → then enter the RTMP connect phase (read TC URL, host, stream ID, parameters). All of this is state for that one connection.

Now multiply by thousands of connections. You need to serve one, switch to another, come back to the first — like talking to hundreds of people at once, each conversation having its own context.

## Three Approaches to Managing Per-Connection State

**1. OS Threads (one per connection)**
- Each thread has its own call stack and local variables → state is naturally stored
- Writing code is easy — just sequential logic per connection
- **Problem:** OS threads are expensive. Thousands of threads = expensive context switching, poor performance

**2. Async/Event Loop (Nginx model)**
- Single thread, big poll loop: poll/wait → serve connection A → save state → return → serve connection B → ...
- **Problem:** You must manually save ALL state into a connection struct because local variables are destroyed when you return from the function. Code becomes much more complex to maintain.

**3. Coroutines / User-Space Threads (Go model, SRS model)**
- Lightweight threads in user space — same benefits as OS threads (local variables, call stacks, natural state storage) but without the OS overhead
- Each connection gets its own coroutine with its own stack
- Code reads like simple sequential logic (like OS threads) but performs like async
- **This is what Go does with goroutines. SRS does the same thing using the State Threads (ST) library — a C library used by SRS's C++ codebase.**

## How Coroutine Switching Works

The tradeoff: coroutines make application code easy, but **someone has to implement the coroutine mechanism itself**.

When serving a connection, you can't just call a function to switch to another connection — that would push onto the same call stack. Instead, you need a **lightweight thread switch**: save the current coroutine's CPU registers to its memory, then load the target coroutine's saved registers and resume from where it left off.

This is the same concept as OS thread context switching, but:
- **OS thread switch:** heavy, involves kernel, expensive
- **Coroutine switch:** user-space only, just save/restore registers, very cheap

ST originally used libc's `setjmp`/`longjmp` for context switching. But glibc later started encrypting (mangling) the saved context for security, making it impossible to manipulate the stack pointer from user code. So ST had to reimplement setjmp/longjmp in pure assembly — that's what `_st_md_cxt_save`/`_st_md_cxt_restore` are. They do exactly what setjmp/longjmp do (save and restore callee-saved registers, stack pointer, and program counter) but without glibc's encryption, giving ST full control over coroutine stacks.

To implement this, you need to understand how function calls work at the CPU level — registers, stack pointers, program counters. The coroutine library handles all of this so application code never has to think about it.

## Timeout Heap: How ST Manages Sleeping Coroutines

When a coroutine calls `st_usleep()` or any I/O function with a timeout, ST puts it to sleep and must wake it at the right time. This requires a data structure that efficiently tracks all sleeping coroutines ordered by their wake-up time.

**Original design (pre-1.5):** A sorted linked list. Insertion was O(N) — for every new sleeper, ST walked the list to find the right position. With thousands of sleeping coroutines, this became a bottleneck.

**Current design (since ST 1.5):** A binary heap implemented as a balanced binary tree using pointers (not an array). This gives O(log N) insertion and removal. In benchmarks, 1 million sleep queue insertions/removals dropped from 100 seconds to 12 seconds.

**Why a pointer-based tree instead of an array?** ST's codebase is structured around linking `_st_thread_t` objects via embedded pointers — no auxiliary data structures. The heap reuses this pattern: each thread object has `left` and `right` child pointers, and `_st_this_vp.sleep_q` points to the root (the thread with the earliest timeout). The tree stays fully balanced and left-adjusted, numbered like an implicit array heap (node N has children 2N and 2N+1), but navigated via pointers from root to leaves using the binary digits of the target index.

**The heap invariant:** Parents always time out before children, so the root is always the next coroutine to wake. This is how ST's scheduler knows which timer fires next when it enters epoll/kqueue — it just checks the root's timeout.

## ST Library Origin and Design

State Threads is derived from Netscape Portable Runtime (NSPR). It's not a general-purpose threading library — it specifically targets Internet Applications (servers that are network I/O driven).

**License:** ST is dual-licensed under **MPL 1.1 or GPLv2** (user's choice). Example code is BSD-licensed. MPL 1.1 is a weak copyleft — changes to MPL-licensed *files* must stay open, but MPL code can be combined with code under other licenses (including proprietary). This is compatible with SRS's MIT license. The GPLv2 option is there for projects that prefer GPL-family licensing.

Key design properties:
- **Deterministic scheduling:** Context switch can only happen at I/O points or explicit synchronization points — never preemptive, never time-sliced
- **No locks needed:** Because switching is deterministic, global data doesn't need mutex protection in most cases. The entire application can freely use static variables and non-reentrant library functions
- **Minimal syscalls:** No per-thread signal mask (unlike POSIX threads), so no save/restore of signal mask on context switch — eliminates two syscalls per switch
- **~5000 lines of code:** Small enough to understand completely, but requires assembly per CPU/OS platform

SRS maintains the fork at `ossrs/state-threads` (branch `srs`), continuously updating it to support modern CPUs and OSes including Linux, macOS, Windows, and architectures like x86_64, ARMv7, AARCH64, Apple M1, RISC-V, LoongArch, and MIPS.

## Non-Network I/O: The Disk Read Problem

ST's non-blocking I/O only works for network sockets. **Disk I/O, device I/O, and stdin all block the entire process** — every coroutine stalls until the operation completes. Disk writes are usually fine (they go to buffer cache), but disk reads can block for unpredictable durations. This is a known limitation of ST's architecture that still exists in SRS.

## ST's Key Constraint and Design Tradeoffs

ST has one fundamental constraint: **all socket I/O must use ST's own I/O functions** (`st_read`, `st_write`, `st_accept`, etc.). If application code calls raw `read(2)` or `write(2)` on a socket, it bypasses ST's scheduler — the entire process blocks, and all coroutines stall. This is why integrating third-party libraries (like libsrt) requires wrapping their I/O to be coroutine-aware (see "Coroutine-Native SRT" above).

**Signal handling:** ST's scheduler only detects two event types: I/O readiness and timeouts. To handle signals (like `SIGINT` or `SIGUSR1`), the standard pattern is to convert them to I/O events — the signal handler writes a byte to a pipe, and a coroutine reads from that pipe via `st_read`. This works because `write(2)` is async-signal-safe.

## The Burden: Maintaining a C Coroutine Library

Coroutines are a fantastic idea for a media server, but unlike Go (where goroutines are built into the language and runtime), **C/C++ has no standard coroutine library for this model**. (Note: C++20 co_await/co_yield is a different mechanism — not the same as user-space threads with full stacks.)

**Platform Support Matrix**
The coroutine switch must be implemented in **assembly language per CPU architecture**: ARM, ARMv8/AArch64, x86_64, MIPS — each has different register conventions. Multiply by OS (Linux, macOS, Windows) and you get a support matrix that is a maintenance burden.

Nobody else actively maintains this library — SRS must maintain it ourselves. Very few people understand coroutine switching at this level.

**The Windows/SRT Problem (Why SRS 6 Dropped Windows)**
- SRS added Windows support using a custom coroutine implementation (fiber/win64-based)
- SRT (Secure Reliable Transport) is a multi-thread library that uses **C++ exceptions**
- On Windows, C++ exceptions use a platform-specific mechanism (SEH — Structured Exception Handling) that **conflicts with the coroutine stack switching**
- This caused crashes that were extremely difficult to diagnose
- William investigated but could not fix it — the interaction between SEH and custom stacks is poorly documented
- **Result:** SRS 6 removed Windows support because SRT + coroutines couldn't coexist on Windows
- **Important distinction:** SRS (the server) dropped Windows support, but ST (the coroutine library) still retains Cygwin64 support — Cygwin64-related files were not removed from ST. ST is a standalone coroutine library and there's no reason to remove working platform support from it just because SRS the server no longer targets Windows.

**Toolchain Gap**
Go provides built-in tools: goroutine stack traces, scheduling profilers, debuggers that understand goroutines. ST has a simple coroutine scheduler driven by I/O events and timers (not an OS thread scheduler), and includes basic `DEBUG_STATS` instrumentation (scheduler timing distribution, thread run/idle/yield counts, per-I/O-call and EAGAIN stats, epoll dispatch stats). But compared to Go's tooling:
- ST includes **GDB helper scripts**: `nn_coroutines` (show count of coroutines) and `show_coroutines` (list all coroutines with their caller functions). These provide basic coroutine-aware debugging within GDB. However, compared to Go's integrated tooling (goroutine stack traces in panics, `runtime/pprof`, scheduler tracing), these are manual GDB extensions rather than native runtime instrumentation.
- No high-level performance analysis or visualization for coroutine scheduling
- Instrumentation exists but is basic counters, not integrated tooling

**Debugging and Profiling Limitations**
- `perf -g` (stack traces) does not work with ST because ST modifies the stack pointer (SP), breaking frame pointer-based stack walking
- Valgrind requires ST-specific hooks, supported since SRS 3+
- ASAN (Address Sanitizer) is supported since SRS 5+, enabled by default in SRS 5, disabled by default in SRS 6 because it sometimes causes crashes for unknown reasons
- **Testing:** ST has a unit test suite using Google Test (gtest), with code coverage via gcov/gcovr. Tests can be built with `make linux-debug-utest` or `make darwin-debug-utest`.
- These tools help but are workarounds — there are still no native tools that understand coroutine scheduling the way Go's runtime tools understand goroutines

**Can AI Help?**
This is a niche domain — not common knowledge. But AI has access to all the code, assembly specs, and documentation. There's hope that AI could maintain the coroutine library (especially for new CPU/OS ports), but it's unproven. The Windows/SEH problem is an example of something that might be too complex even for AI — or might be exactly where AI excels.

## Valgrind Support

Valgrind can't track ST coroutines by default because `setjmp`/`longjmp` switches the stack pointer to custom-allocated stacks that Valgrind doesn't know about, causing false positives.

**The fix** (merged from [toffaletti's fork](https://github.com/toffaletti/state-threads/commit/7f57fc9acc05e657bca1223f1e5b9b1a45ed929b), [commit 4cca7a0](https://github.com/ossrs/state-threads/commit/4cca7a0272b70b184742dd68065af8a9a42e030f)):
- Uses `VALGRIND_STACK_REGISTER(top, bottom)` when creating a coroutine to tell Valgrind about the custom stack
- Uses `VALGRIND_STACK_DEREGISTER(id)` when the coroutine exits
- Stores the registration ID in `_st_stack_t.valgrind_stack_id`
- Skips the primordial thread (its stack is the normal process stack, already known to Valgrind)

**Opt-in via compile flag:** `-DMD_VALGRIND -I/usr/local/include`. Zero overhead when not enabled — `NVALGRIND` is defined by default to disable all Valgrind macros.

**What changed (3 files):**
- **common.h** — Added `MD_VALGRIND`/`NVALGRIND` macro logic; added `valgrind_stack_id` field to `_st_stack_t`
- **sched.c** — Included `<valgrind/valgrind.h>`; added `VALGRIND_STACK_REGISTER` in `st_thread_create()` and `VALGRIND_STACK_DEREGISTER` in `st_thread_exit()`
- **README** — Added build instructions for Linux with Valgrind

## Stack Memory Management: Cache vs Free

By default, ST caches all thread stacks forever — when a coroutine exits, its stack goes onto a free list and gets reused by the next `_st_stack_new` call. This is efficient for long-running servers with stable thread counts, but wastes memory when threads are short-lived (stacks accumulate and never shrink).

**Compile-time flag `MD_CACHE_STACK`** ([state-threads#38](https://github.com/ossrs/state-threads/issues/38), [commit b019860](https://github.com/ossrs/state-threads/commit/b01986064cf01de86cea7b24a2f95e7114ba3d75)) controls the behavior:

- **With `MD_CACHE_STACK`** (original behavior): Freed stacks stay on the free list. `_st_stack_new` searches the list for a stack of sufficient size before allocating a new one.
- **Without `MD_CACHE_STACK`**: Stacks are actually freed (`munmap`/`free`). When `_st_stack_new` runs, it first drains the entire free list — unmapping every cached stack — then allocates fresh.

**Why not free immediately in `_st_stack_free`?** When a coroutine exits, it's still *running on its own stack* during cleanup. Freeing the stack out from under a running coroutine would crash. So `_st_stack_free` always appends to the free list, and the actual deallocation happens later in `_st_stack_new` (when a different coroutine is running on a different stack). The re-enabled `_st_delete_stk_segment` function handles the actual `munmap` or `free`.

## Coroutine-Native SRT

SRS 4.0 (2019) added SRT support, but the initial implementation used libsrt's own threads and async I/O, separate from ST. This caused complex async code was difficult to maintain.

In SRS 5.0, SRT was rewritten to be **coroutine-native** ([srs#3010](https://github.com/ossrs/srs/pull/3010)). The pattern for making any protocol coroutine-native:

1. Call the protocol's API (e.g., `srt_recvmsg`)
2. If success, return the data
3. If the error is not "would block" (e.g., `SRT_EASYNCRCV`), return the error
4. If "would block", switch the current coroutine via `st_cond_t` condition variable and let other coroutines run
5. When the fd becomes ready (detected by `srt_epoll_uwait` in a poller coroutine), signal the condition variable to wake the waiting coroutine
6. Repeat from step 1

This is the same pattern ST uses internally for TCP (`st_read` handles `EAGAIN` the same way), just adapted to SRT's epoll API.

**The maintainability win:** In callback/async style, connection state must live in global data structures and gets modified by different event callbacks — the object lifecycle is scattered across the event loop. In coroutine-native style, state lives in local variables on the coroutine stack, and the lifecycle is linear and contained in one coroutine function. This is the fundamental reason SRS uses coroutines.

**Remaining issue:** libsrt uses C++ exceptions internally, which still causes the Windows/SEH compatibility problem described above. The coroutine-native rewrite solved the threading and maintainability issues but did not solve Windows portability. The fix requires either rewriting libsrt to avoid C++ exceptions or fixing the SEH/coroutine stack interaction on Windows. Not fixed yet, planned for the future.

## Multi-CPU: Cluster, Not Multi-Threading

**Problem:** SRS uses single-threaded coroutines → only saturates one CPU core. Modern servers have many cores.

**Why Not Multi-Threading?**
ST library actually supports multi-threading, and William added multi-thread support. But it turned out to be a disaster:

- Even with thread-local isolation (separate thread-safe coroutine schedulers), threads must still **communicate** with each other
- **The biggest problem: load balancing between threads is nearly impossible to estimate.** Different threads have different capacity, and you can't easily observe the load distribution
- With single-thread: observing load is trivial — one CPU, 60-70% threshold, done
- With multi-thread: complexity explodes, load becomes opaque
- **William's verdict: multi-threading doesn't solve the multi-CPU problem — it creates new, worse problems. It's a trauma maker.**

**The Right Solution: Proxy + Origin + Edge Cluster**

This is a **settled and confirmed decision**: SRS will remain single-process, single-threaded with coroutines. Multi-threading will be removed from SRS. The multi-CPU problem is solved entirely by the cluster architecture:

- **Proxy** (implemented in Go): Stateless, horizontally scalable, synchronizes state through Redis. Supports all protocols (RTMP/FLV/HLS/SRT/WebRTC). Proxies API and media traffic to Origin servers.
- **Origin** (SRS, C++): Single-threaded with coroutines. Handles stream processing and protocol conversion.
- **Edge** (SRS, C++): Single-threaded with coroutines. Caches streams from Origin for massive playback distribution.

Multiple Origins behind a Proxy, combined with Edge servers, can scale to thousands of streams and tens of thousands of viewers per stream. Each component stays simple and observable — one CPU, one process, coroutines.

## Multi-threading Timeline (Historical)

SRS has traditionally been single-process, single-threaded, akin to a single-process version of Nginx, with the addition of coroutines for concurrent processing. Coroutines are implemented using the StateThreads library, which has been modified to support thread-local functionality for operation in a multi-threaded environment.

Despite experimenting and analyzing thread-local handling for a media architecture over the years, SRS has not adopted a thread-local approach but rather a different multi-threaded architecture that is still in the planning stage: Stream processing occurs on a single thread, while blocking operations like logging, file writing, and DNS resolution are handled by separate threads. In essence, SRS uses multi-threading to address blocking issues. If Linux supports fully asynchronous I/O in the future, multi-threading may not be necessary, as seen in liburing.

StateThreads multi-threading faces issues with Windows C++ exception handling. Windows' exception mechanism differs from Linux, causing compatibility problems when StateThreads implements setjmp and longjmp, as discussed in SEH.

Challenges with multi-thread scheduling and load balancing: While thread-local multi-threading addresses multi-core utilization, it still limits the need for streaming and playback to a single thread, preventing complete load balancing across multiple threads. Without thread-local functionality, serious locking and competition issues arise. Essentially, it's like running multiple K8s Pods within a single process and handling scheduling, monitoring, and load balancing internally, which can be quite complex.

In SRS 5.0, StateThreads were restructured to support thread-local functionality and initiated a main thread and subthreads to transition the architecture into a multi-threaded model. However, various issues arose during subsequent stages, leading to a default return to a single-threaded architecture in SRS 6.0. Multi-threading capabilities will be removed as the Proxy and Edge cluster architecture fully replaces them.

Additionally, we explored another potential architecture where specific capabilities are distributed across different threads, like using separate threads for WebRTC encryption and decryption. However, this approach transforms into a typical multi-threaded program rather than a thread-local architecture, resulting in performance overhead from locks and reduced stability — not an ideal direction.

## How `__thread` Makes ST Thread-Safe

ST's multi-threading model is simple: **one pthread, one ST scheduler**. It uses GCC's `__thread` so scheduler state is thread-local, not shared global state.

This approach came from [toffaletti's fork](https://github.com/toffaletti/state-threads) and was later adopted by ossrs/state-threads ([state-threads#19](https://github.com/ossrs/state-threads/issues/19)).

In practice, key runtime state is thread-local: current thread, VP/scheduler state, event backend data, free stack list, and key/destructor tables. `st_init()` initializes each thread's runtime (including calling `_st_io_init()` directly). In current code, the netfd freelist is also thread-local (`static __thread _st_netfd_t *_st_netfd_freelist`), so no mutex is needed there.

**Design takeaway:** ST scales by isolation, not heavy locking. Each pthread runs an independent coroutine runtime with its own run queue, timers, and event loop.

**Why SRS still moved away:** This works well inside ST, but SRS still faced hard cross-thread coordination and load-balancing problems at the application level. The project chose Proxy + Origin + Edge cluster architecture for multi-CPU scaling instead.

## Porting ST to New Platforms

Porting ST to a new OS/CPU is simpler than it sounds. The core task is implementing two assembly functions: `_st_md_cxt_save` (save registers) and `_st_md_cxt_restore` (restore registers) — the custom replacements for `setjmp`/`longjmp`.

**Current platform support (from [state-threads#22](https://github.com/ossrs/state-threads/issues/22)):**

- **Linux + i386** — Stable. 32-bit x86 systems.
- **Linux + x86-64** — Stable. CentOS, Ubuntu server, etc.
- **Linux + ARM (v7)** — Stable. Raspberry Pi and ARM devices. ([state-threads#1](https://github.com/ossrs/state-threads/issues/1))
- **Linux + AArch64 (ARMv8)** — Stable. ARM servers. ([state-threads#9](https://github.com/ossrs/state-threads/issues/9))
- **Linux + MIPS** — Dev. OpenWRT devices. ([state-threads#21](https://github.com/ossrs/state-threads/issues/21))
- **Linux + MIPS64** — Dev. Loongson 3A4000/3B3000. ([state-threads#21](https://github.com/ossrs/state-threads/issues/21))
- **Linux + LoongArch64** — Dev. Loongson 3A5000/3B5000, new ISA replacing MIPS. ([state-threads#24](https://github.com/ossrs/state-threads/issues/24))
- **Linux + RISC-V** — Dev. StarFive boards. ([state-threads#28](https://github.com/ossrs/state-threads/pull/28))
- **macOS + x86-64** — Stable. Intel Macs. ([state-threads#11](https://github.com/ossrs/state-threads/issues/11))
- **macOS + AArch64 (M1/M2)** — Dev. Apple Silicon. ([state-threads#30](https://github.com/ossrs/state-threads/issues/30))
- **Windows + x86-64 (Cygwin64)** — Dev. 64-bit only, no 32-bit Windows. ([state-threads#20](https://github.com/ossrs/state-threads/issues/20))

"Stable" means production-tested in SRS deployments. "Dev" means implemented and working but less field-tested.

**Why custom assembly instead of libc's setjmp/longjmp?**
Early ST used glibc's `setjmp`, then modified the `jmp_buf` to swap the stack pointer to a heap-allocated coroutine stack. This required knowing glibc's internal `jmp_buf` layout. But newer glibc versions started **encrypting (pointer-mangling)** the saved registers inside `jmp_buf`, making it impossible to modify the SP from user code. The fix: implement save/restore entirely in assembly with ST's own `jmp_buf` layout. This is actually more portable — CPU register ABIs are stable and well-documented, while glibc internals are not. **All platforms now use custom assembly exclusively** — the libc setjmp path has been completely removed (attempting to use it is a compile error). Every OS/CPU goes through `_st_md_cxt_save`/`_st_md_cxt_restore` in the `.S` files.

**Assembly files are organized by OS, not CPU:**
- `md_linux.S` — Linux x86 platforms: i386, amd64/x86_64
- `md_linux2.S` — Linux non-x86 platforms: aarch64, arm, riscv, mips64, mips, loongarch64
- `md_darwin.S` — macOS/Darwin (different calling conventions and object format)
- `md_cygwin64.S` — Windows via Cygwin64

Within each file, CPU-specific sections are selected by `#ifdef` macros (`__x86_64__`, `__aarch64__`, `__mips__`, `__loongarch64`, `__riscv`, etc.).

> Note: All `.S` files check `MD_ST_NO_ASM` — historically this allowed disabling assembly and falling back to libc's `setjmp`/`longjmp`. Since the libc setjmp path has been removed (all platforms now require assembly), this macro no longer works — defining it will cause linker errors. It remains in the code as a leftover.

**What registers to save?**
Only the **callee-saved registers** matter — these are the registers a function must preserve across calls. The actual registers saved by ST's assembly (from the `.S` files):

- **i386 (Linux):** ebx, esi, edi, ebp, esp, pc
- **x86-64 (Linux/Darwin/Cygwin64):** rbx, rbp, r12-r15, rsp, pc
- **ARM v7 (Linux):** v1-v6, sl, fp, sp, lr (i.e., r4-r9, r10, r11, r13, r14); optionally VFP d8-d15 and iWMMXt wr10-wr15
- **AArch64 (Linux/Darwin):** x19-x28 (callee-saved), x29 (frame pointer), x30/lr (link register), sp, plus floating-point d8-d15
- **MIPS/MIPS64 (Linux):** sp, gp, fp/s8, s0-s7, ra
- **LoongArch64 (Linux):** sp (r3), ra (r1), fp (r22), s0-s8 (r23-r31)
- **RISC-V (Linux):** sp, ra, fp/s0, s1-s11

**The jmpbuf problem (historical, now resolved):**
Different platforms define `jmp_buf` differently — field names (`__jmpbuf` vs `__jb`), field sizes, and layouts all varied. This was a problem when ST relied on the platform's `jmp_buf`. [state-threads#29](https://github.com/ossrs/state-threads/pull/29) resolved this by having ST define and use its own `_st_jmp_buf_t` structure (`long[22]` — sized for the largest platform, AArch64) instead of relying on platform-specific layouts. All platforms now use this unified structure, eliminating the cross-platform jmpbuf compatibility issue entirely.

The macro `MD_GET_SP(_t)` in `md.h` defines how to read/write the stack pointer in ST's own jmpbuf for each platform. This is critical for `MD_INIT_CONTEXT` — when creating a coroutine, the SP in the saved context must be updated to point at the heap-allocated stack, since the coroutine can't use the creator's stack.

**Porting toolkit (`tools/` directory):**
Six utilities help with any new port:

- **`porting.c`** — Prints detected OS/CPU macros, pointer sizes, and calling convention info. Run this first to understand your platform.
- **`helloworld.c`** — Minimal ST validation: `st_init()` + loop with `st_sleep()`. If this prints, context switching works.
- **`verify.c`** — Full API test: thread creation, mutex, cond variable, usleep, thread join. Validates the complete ST threading model.
- **`jmpbuf.c`** — Shows the platform's `jmp_buf` struct definition via preprocessor expansion, useful for understanding field layout differences.
- **`pcs.c`** — Analyzes the Procedure Call Standard (which registers are caller vs callee-saved).
- **`stack.c`** — Inspects stack behavior on the platform.

**Porting steps (using MIPS/OpenWRT as the reference example from [state-threads#21](https://github.com/ossrs/state-threads/issues/21)):**

1. **Detect CPU macro:** `g++ -dM -E - </dev/null | grep -i aarch64` to find the `#define` your compiler provides (here `aarch64` is just an example — replace it with your target CPU name, e.g. `mips`, `riscv`, `loongarch`)
2. **Understand the platform:** Run `tools/porting.c` to see detected OS/CPU macros and pointer sizes. Compile `tools/pcs.c` and use GDB's `si` (step instruction) to step through function call assembly — identify which registers are callee-saved (these are the ones you must save/restore in ST). Also refer to vendor docs (ARM/MIPS/RISC-V reference manuals) for the full callee-saved register list. Optionally run `tools/jmpbuf.c` to see how the platform's libc setjmp saves registers — this is a useful cross-reference for confirming the callee-saved register list, even though ST uses its own jmpbuf and doesn't depend on libc's layout
3. **Add empty stubs:** In the appropriate `.S` file, add `_st_md_cxt_save` and `_st_md_cxt_restore` under a new `#elif defined(__your_cpu__)` — empty functions that just return. Build `verify.c` and `helloworld.c` to confirm compilation and linking succeed, even though they won't run correctly yet
4. **Implement the assembly:** Fill in the actual save/restore instructions for each callee-saved register to/from the jmpbuf — e.g., `sw`/`lw` (MIPS32), `sd`/`ld` (MIPS64, RISC-V), `stp`/`ldp` (AArch64), `stmia`/`ldmia` (ARM v7 — block load/store with register lists). `_st_md_cxt_save` returns 0; `_st_md_cxt_restore` sets return value to 1 and jumps to the saved return address
5. **Define MD_GET_SP:** In `md.h`, add the macro for your platform so `MD_INIT_CONTEXT` can replace the SP with the coroutine's heap-allocated stack address
6. **Test with helloworld:** If it prints messages with `st_sleep` pauses, context switching works
7. **Test with verify:** Run `verify.c` for full API test — thread creation, mutex, cond variable, usleep, thread join. Also use it early (after adding empty stubs) to verify compilation and linking before implementing the assembly

**Platform-specific build commands:**
- Linux: `make linux-debug` (auto-detects CPU)
- macOS: `make darwin-debug`
- Windows: `make cygwin64-debug`
- Force CPU: `make linux-debug EXTRA_CFLAGS="-D__aarch64__"` (if auto-detection fails)

## Future Direction: Refactor ST Internals from C to C++

The current ST codebase is written in C with heavy use of macros and manual struct patterns (embedded linked lists, struct casting for "inheritance", macro-based queue operations). This code is difficult to read and understand — both for humans and AI. The macro layer obscures the actual logic, and C's manual patterns for data structures are not straightforward.

**The plan:** Refactor ST's internal implementation from C to C++, while keeping the external C API unchanged (`st_read`, `st_write`, `st_accept`, etc. remain `extern "C"`). This is an internal rewrite only — no API changes for consumers.

**Why C++:**
- Replace opaque macros with readable C++ constructs (classes, templates, inline functions)
- Replace manual linked list macros and struct casting with proper C++ data structures and type safety
- RAII for resource management (stack allocation/deallocation, fd lifecycle)
- The code becomes much clearer and more maintainable — critical for AI-managed maintenance

**Why this matters for the AI strategy:**
- AI can reason about C++ code far more easily than macro-heavy C
- This directly enables the vision of AI maintaining ST long-term
- Better code quality → fewer bugs → more confidence in AI-generated changes

**Approach:** Incremental — start with the worst offenders (likely the macros in `common.h` and queue management in `sched.c`), convert piece by piece, verify with tests at each step.

## Can AI Replace RUST for ST Maintenance?

RUST (specifically tokio) is conceptually similar to ST — both are polling-based async with cooperative scheduling. RUST offers advantages: no assembly needed, built-in multi-thread support, cross-platform without manual porting, better tooling. The "Hidden Flaws of SRS" blog explored RUST as a potential future direction.

However, the real question is not about language features but about **ecosystem and maintenance capability**. If AI proves capable of maintaining ST's assembly code — understanding CPU register conventions, porting to new architectures, debugging platform-specific issues like Windows/SEH — then the ST maintenance burden disappears and there's no compelling reason to switch languages. The C++ ecosystem for the media industry (FFmpeg, libsrt, libwebrtc, and other open-source media streaming projects) matters more than language features.

RUST is a fallback path if AI cannot handle the low-level ST maintenance. It's not an inevitable direction. The deciding factor is AI capability, not language preference.

## Backtrace Support for Coroutines

ST supports `backtrace()` and `backtrace_symbols()` for dumping stack traces from within coroutines ([state-threads#34](https://github.com/ossrs/state-threads/issues/34)). Since each coroutine has its own stack, standard backtrace works naturally — you get a full call chain like `bar → foo → start → _st_thread_main → st_thread_create`.

**Usage:** Build and run the example in `tools/backtrace/`. Works on both Linux and Darwin.

**Key details:**
- On Linux, compile with `-rdynamic` to get function names in `backtrace_symbols()` output; without it you get raw offsets like `(+0x204b)`
- On Darwin, uses `__builtin_return_address` to walk the stack
- The return address points to the **instruction after the call** (the return site), so `addr2line` shows the next source line, not the call line itself — this is normal
- Use `addr2line -C -p -s -f -a -e <binary> <address>` to convert offsets to source file:line
- Use `nm <binary> | grep <func>` to find function base addresses, then compute offsets
- `objdump -d <binary>` can verify the relationship between addresses and instructions

This complements ST's GDB helper scripts (`nn_coroutines`, `show_coroutines`) as another debugging tool for coroutine-based code.

## Timeout Semantics

ST timeouts have a subtle but important behavior: the timeout parameter is relative to `last_clock` (the timestamp of the last scheduler cycle), not the moment the function is called.

When you call `st_read(fd, buf, n, timeout)`, internally ST computes the deadline as `due = last_clock + timeout` (in `_st_add_sleep_q`). The `last_clock` value is updated in `_st_vp_check_clock()`, which only runs during scheduler cycles — in the idle thread loop after `dispatch()` returns, and in `st_thread_yield()`. Between those points, `last_clock` is frozen.

**The cancelling effect:** Both the deadline and the `epoll_wait` timeout are computed from the same stale `last_clock`: `due = last_clock + timeout`, `min_timeout = due - last_clock = timeout`. The staleness cancels out — `epoll_wait` always receives the full `timeout` value. On a busy server, frequent I/O keeps `last_clock` fresh and deadlines fire on time. On a quiet server, the actual wait approximates the full `timeout` regardless of staleness. For example:

- `last_clock` was set 8ms ago, you call `st_read()` with a 10ms timeout → `due = last_clock + 10ms` (only 2ms from now), but dispatch computes `min_timeout = due - last_clock = 10ms` → `epoll_wait` blocks for the full 10ms
- On a busy server with frequent I/O, `last_clock` stays fresh — dispatch would compute `min_timeout = 2ms` and the deadline fires on time

ST timeouts are suitable for coarse-grained purposes — detecting broken connections, idle peers, or stuck operations. Realistic timeouts should be on the order of seconds (e.g., 5s, 30s), where `last_clock` staleness is negligible. They are not designed for precise sub-millisecond timing.

**Related test cases (`st_utest_learn_kb.cpp`):**
- `TEST(LearnKB, BasicNetfdReadTimeout)`
- `TEST(LearnKB, CondTimedwaitTimeout)`

## `st_init()` — How the Coroutine World is Built

`st_init()` is the entry point that bootstraps the entire coroutine runtime. It creates the scheduler data structures, the event system, the idle thread, and wraps the calling OS thread as the first coroutine. Here's what happens step by step:

**Step 1: Set event system and initialize I/O.** SRS calls `st_set_eventsys(ST_EVENTSYS_ALT)` before `st_init()` to select the platform-optimal backend — epoll on Linux, kqueue on macOS. Inside `st_init()`, `st_set_eventsys(ST_EVENTSYS_DEFAULT)` is called but returns `EBUSY` (since the event system is already set) and is harmlessly ignored — hence the code comment "We can ignore return value here". Then `_st_io_init()` runs for one-time I/O setup (ignores SIGPIPE, sets fd limits).

**Step 2: Initialize all scheduler queues and create the event system.** First initializes the thread-local free stack list (`_st_free_stacks`), then zeroes the VP struct (`memset(&_st_this_vp, 0, ...)`), then initializes three empty linked lists:
- `run_q` — coroutines ready to run
- `io_q` — coroutines blocked on socket I/O
- `zombie_q` — dead coroutines awaiting cleanup

Then calls `(*_st_eventsys->init)()` to create the actual epoll/kqueue file descriptor. Also captures `pagesize` (for stack guard pages) and `last_clock` (current timestamp for timeout calculations).

**Step 3: Create the Idle Thread.** This is the heart of ST. The idle thread is created via `st_thread_create(_st_idle_thread_start)`, then marked with `_ST_FL_IDLE_THREAD`, decremented from `_st_active_count` (it doesn't count as an "active" coroutine), and removed from the run queue (it's managed specially by the scheduler).

The idle thread's loop is the core scheduler cycle:
1. `(*_st_eventsys->dispatch)()` — calls `epoll_wait`/`kqueue`, blocking until I/O is ready or the earliest timeout fires. This is the **only place the process truly blocks**.
2. `_st_vp_check_clock()` — updates `last_clock`, then walks the sleep heap and moves timed-out coroutines to the run queue.
3. `_st_switch_context(me)` — yields CPU to a ready coroutine from the run queue.
4. When that coroutine eventually switches back (hits I/O or yields), the idle thread loops again.
5. When `_st_active_count` drops to 0 (no more coroutines), the idle thread calls `exit(0)`.

**Step 4: Create the Primordial Thread.** The current OS thread (the one calling `st_init()`) is wrapped into an `_st_thread_t` struct via `calloc`. It gets no new stack — it reuses the existing process stack. Its state is set to `_ST_ST_RUNNING`, flagged as `_ST_FL_PRIMORDIAL`, and assigned to `_st_this_thread`. This becomes the first running coroutine and `_st_active_count` is incremented to 1.

**After `st_init()` returns**, the coroutine world is ready: event system initialized, idle thread created and waiting, primordial thread running. Control returns to `main()`, which is now executing as the primordial coroutine. From here, calling `st_thread_create()` spawns new coroutines, and the scheduler workflow activates once those coroutines hit their first I/O call.

**Related test cases (`st_utest_learn_kb.cpp`):**
- `TEST(LearnKB, EventSysSelectedAndLockedAfterInit)`
- `TEST(LearnKB, CoroutineRunsOnSeparateStack)`
- `TEST(LearnKB, StartRoutineNotExecutedInline)`
- `TEST(LearnKB, JoinDrivesFirstRunWhenNoManualYield)`

## `st_thread_create()` — How a Coroutine is Born

`st_thread_create(start, arg, joinable, stk_size)` allocates a new coroutine, sets up its execution context, and places it on the run queue — all without actually running it yet.

**Step 1: Allocate the stack.** The requested size (default `ST_DEFAULT_STACK_SIZE` = 128KB) is rounded up to page alignment, then `_st_stack_new()` either reuses a stack from the free list or allocates fresh memory (via `mmap` or `malloc`). In DEBUG builds (without `MD_NO_PROTECT`), guard pages are set at both ends of the stack via `mprotect(..., PROT_NONE)` to catch overflow via SIGSEGV; in release builds there are no guard pages.

**Step 2: Carve thread metadata from the top of the stack.** The thread control block (`_st_thread_t`) and per-thread data array (`ptds[ST_KEYS_MAX]`) are placed at the top of the stack, growing downward. Then the stack pointer is 64-byte aligned. The layout from high to low address:

- `ptds[ST_KEYS_MAX]` — per-thread data slots (like thread-local storage)
- `_st_thread_t` — the thread control block
- 64-byte alignment padding
- 128-byte pad (`_ST_STACK_PAD_SIZE`) reserved below the aligned SP
- `stack->sp` — where the coroutine's actual execution stack begins (grows downward)
- Guard page at the bottom (DEBUG builds only)

Both `thread` and `ptds` are zeroed with `memset` after being carved from the stack. This is efficient: one allocation provides both the stack and the control block — no separate `malloc` for the thread struct.

**Step 3: Set up the initial context (the core trick).** This is the most subtle part:

```c
/* Note that we must directly call rather than call any functions. */
if (_st_md_cxt_save(thread->context)) {
    _st_thread_main();
}
MD_GET_SP(thread) = (long)(stack->sp);
```

The code comment is a correctness constraint: `_st_md_cxt_save` must be called directly at this site, not wrapped in a helper function. It captures the current PC (return address) — when `_st_md_cxt_restore` later restores this context, execution resumes right here at the `if` check. If `_st_md_cxt_save` were called inside a helper function, the saved PC would point into that helper's frame, and the restored execution would return into a function frame that doesn't exist on the new coroutine's stack — crash.

- `_st_md_cxt_save()` saves the **creator's** current CPU registers into `thread->context` and returns **0** (like `setjmp`)
- Since it returns 0, the `if` body is **skipped** — `_st_thread_main()` is NOT called now
- `MD_GET_SP()` then **overwrites the saved stack pointer** in the context to point at the new coroutine's heap-allocated stack

Later, when the scheduler switches to this coroutine via `_st_md_cxt_restore(thread->context)`, it restores these saved registers — but with the **modified SP** pointing at the new stack. The restore returns **1** (non-zero), so the `if` is entered and `_st_thread_main()` executes — now running on the coroutine's own stack. `_st_thread_main()` simply calls `thread->start(thread->arg)` (the user's function), and when it returns, calls `st_thread_exit()`.

This save-then-patch-SP trick is how ST creates a coroutine without running it: capture a register snapshot, swap the stack pointer to the new stack, defer execution until scheduled.

**Step 4: Set up joinability.** If `joinable` is true, a condition variable (`thread->term`) is allocated so another coroutine can call `st_thread_join()` and block until this coroutine finishes.

**Step 5: Make it runnable.** The thread's state is set to `_ST_ST_RUNNABLE`, `_st_active_count` is incremented, and the thread is inserted into `run_q`. The coroutine won't actually execute until the current coroutine yields (hits I/O, sleeps, or calls `st_thread_yield()`), at which point the scheduler picks it off the run queue.

**Valgrind integration:** If `MD_VALGRIND` is enabled and the thread is not the primordial thread, `VALGRIND_STACK_REGISTER()` is called to register the custom stack region with Valgrind, preventing false positives from stack pointer switching.

**Related test cases (`st_utest_learn_kb.cpp`):**
- `TEST(LearnKB, CoroutineRunsOnSeparateStack)`
- `TEST(LearnKB, StartRoutineNotExecutedInline)`
- `TEST(LearnKB, JoinDrivesFirstRunWhenNoManualYield)`
- `TEST(LearnKB, LocalStatePreservedAcrossYield)`
- `TEST(LearnKB, ReturnValueThroughJoin)`

## Epoll-Driven I/O Workflow — How Coroutines Sleep and Wake

This section traces exactly what happens when a coroutine does I/O — from `st_read()` through epoll and back. This is the core mechanism that makes ST work.

**The I/O functions all follow the same pattern** (`st_read`, `st_write`, `st_accept`, `st_connect`, `st_recvfrom`, `st_sendto`, `st_recvmsg`, `st_sendmsg`):

1. Try the raw syscall (`read`, `write`, `accept`, etc.) immediately
2. If it succeeds → return the result. Zero overhead, no coroutine machinery involved
3. If `EINTR` → retry the syscall
4. If `EAGAIN`/`EWOULDBLOCK` → the socket isn't ready, enter the coroutine wait path via `st_netfd_poll()`
5. If any other error → return error

This "try first" design means ST adds zero overhead when data is already available — it's just a normal syscall.

**`st_netfd_poll()` → `st_poll()`:** The poll wrapper converts a single fd into a `struct pollfd` and calls `st_poll()`, which is where the coroutine actually suspends.

**`st_poll()` — The Coroutine Suspension Point (sched.c):**

1. **Register with epoll:** Calls `_st_eventsys->pollset_add(pds, npds)`, which increments per-fd reference counts (`_ST_EPOLL_READ_CNT`, `_ST_EPOLL_WRITE_CNT`, `_ST_EPOLL_EXCEP_CNT` — one per event direction), then computes the new event mask from those counts and calls `epoll_ctl` — `EPOLL_CTL_ADD` if the fd had no prior watchers, or `EPOLL_CTL_MOD` if it already did. Multiple coroutines can watch the same fd — reference counts track this.

2. **Create a poll queue entry on the stack:** A `_st_pollq_t` struct links the pollfd array to the waiting coroutine (`pq.thread = me`) and sets `pq.on_ioq = 1`.

3. **Insert into `io_q`:** The poll queue entry is linked into the global I/O wait list. Later, `_st_epoll_dispatch()` walks this list to find which coroutines to wake.

4. **Add to sleep heap (if timeout specified):** `_st_add_sleep_q(me, timeout)` sets `me->due = last_clock + timeout` and inserts into the binary heap. This ensures the coroutine wakes even if I/O never arrives.

5. **Set state to `_ST_ST_IO_WAIT`** and call `_st_switch_context(me)` — the coroutine suspends. Registers are saved, and the scheduler picks the next runnable coroutine (or the idle thread if nothing else is ready).

**`_st_vp_schedule()` — The Scheduler (sched.c):**

When `_st_switch_context()` is called, `_st_vp_schedule()` decides what runs next:
- If `run_q` is non-empty → pull the first thread off the head, switch to it
- If `run_q` is empty → switch to the idle thread

The idle thread is where `epoll_wait` lives — it's the "nobody else has anything to do, so let's wait for I/O" fallback.

**The Idle Thread Loop (sched.c):**

The idle thread runs a tight loop until no active coroutines remain:
1. `_st_eventsys->dispatch()` → calls `_st_epoll_dispatch()`
2. `_st_vp_check_clock()` → process expired timeouts
3. Set self to `RUNNABLE`, call `_st_switch_context()` → yield to a ready coroutine
4. When that coroutine eventually suspends, we return here and loop

**`_st_epoll_dispatch()` — The Heart of I/O Multiplexing (event.c):**

This function does three things: wait for I/O, wake coroutines, and clean up epoll state.

*Phase 1 — Calculate epoll timeout from sleep heap:*
- If `sleep_q` is NULL (no sleeping coroutines) → `timeout = -1` (block forever)
- Otherwise → `timeout = sleep_q->due - last_clock` (wake at earliest deadline)
- Special case: if timeout computes to 0ms but `min_timeout > 0` (sub-millisecond), round up to 1ms to avoid a spin loop (epoll_wait only has millisecond granularity)

*Phase 2 — `epoll_wait()`:*
- `nfd = epoll_wait(epfd, evtlist, evtlist_size, timeout)` — **this is the only true blocking point in the entire process**. The OS suspends the process until at least one fd is ready or the timeout expires.

*Phase 3 — Mark fired fds:*
- For each event returned by epoll, store the fired events in `_ST_EPOLL_REVENTS(osfd)`. If `EPOLLERR` or `EPOLLHUP` is set, also OR in the fd's currently-registered event bits (`_ST_EPOLL_EVENTS(osfd)` — whichever of `EPOLLIN`/`EPOLLOUT`/`EPOLLPRI` have non-zero ref counts) so waiting coroutines see the error.

*Phase 4 — Walk `io_q`, wake matching coroutines:*
- For each `_st_pollq_t` entry on `io_q`, check each fd in its pollfd array against `_ST_EPOLL_REVENTS`. If any fd has matching events, set `pds->revents` and mark `notify = 1`.
- If notify: remove the poll queue entry from `io_q` (`pq->on_ioq = 0`), call `_st_epoll_pollset_del()` which decrements reference counts for all fds in the pollfd array and calls `EPOLL_CTL_MOD`/`EPOLL_CTL_DEL` only for fds that did NOT fire (fds with `_ST_EPOLL_REVENTS != 0` are skipped — they are cleaned up later in Phase 5). Then remove the thread from `sleep_q` if present, set `thread->state = _ST_ST_RUNNABLE`, and insert into `run_q`.

*Phase 5 — Clean up fired fds in epoll:*
- For each event in the epoll result list, clear `_ST_EPOLL_REVENTS`, then either `EPOLL_CTL_MOD` (if other coroutines still watch this fd) or `EPOLL_CTL_DEL` (if no more watchers). This keeps epoll's internal state in sync with ST's reference counts.

**`_st_vp_check_clock()` — Timeout Processing (sched.c):**

After dispatch returns, the idle thread checks for expired timeouts:
1. Update `last_clock = st_utime()`
2. Walk the sleep heap root: while `sleep_q->due <= now`, remove the thread from the heap
3. If the thread was in `_ST_ST_COND_WAIT` state, set the `_ST_FL_TIMEDOUT` flag
4. Set `thread->state = _ST_ST_RUNNABLE` and insert at the **head** of `run_q` (using `st_clist_insert_after`)

**Priority detail:** Timed-out coroutines go to the head of `run_q`; I/O-ready coroutines go to the tail. Timeouts get priority because they represent deadlines.

**Resumption — Back in `st_poll()`:**

When the scheduler switches back to our coroutine, execution resumes right after the `_st_switch_context()` call in `st_poll()`:
- If `pq.on_ioq == 0` → dispatch already removed us from `io_q`, I/O is ready. Count fds with non-zero `revents` and return the count.
- If `pq.on_ioq == 1` → we timed out (woken by `_st_vp_check_clock`, not by dispatch). Remove ourselves from `io_q`, call `pollset_del` to clean up epoll registration, return 0.

Back in `st_read()`, if `st_netfd_poll()` succeeded, the loop retries `read()` — which now succeeds because data is available. The data is returned to the application.

**The `on_ioq` flag is the key mechanism** that distinguishes timeout wakeup from I/O wakeup. The dispatch function clears it (`pq->on_ioq = 0`) when I/O fires; `_st_vp_check_clock` does NOT touch it. So when `st_poll()` resumes, it checks this flag to know why it woke up.

**Reference counting for shared fds:** Multiple coroutines can wait on the same fd (e.g., multiple readers on a UDP socket). `_ST_EPOLL_READ_CNT(fd)`, `_ST_EPOLL_WRITE_CNT(fd)`, and `_ST_EPOLL_EXCEP_CNT(fd)` track how many coroutines watch each direction. `epoll_ctl` is only called when the computed event mask (`_ST_EPOLL_EVENTS(fd)`) changes — i.e., when counts transition between 0 and non-zero. When a coroutine's I/O completes, `_st_epoll_pollset_del` decrements the counts — if all reach 0, the fd is removed from epoll; otherwise it's modified to reflect remaining watchers.

**Related test cases (`st_utest_learn_kb.cpp`):**
- `TEST(LearnKB, BasicNetfdWriteThenRead)`
- `TEST(LearnKB, BasicNetfdReadTimeout)`

## `st_usleep()` — Pure Timer-Based Coroutine Sleep

`st_usleep(usecs)` suspends the current coroutine for a specified duration. Unlike I/O functions (`st_read`, `st_write`), it involves **no I/O at all** — the coroutine is placed only in the sleep heap and woken purely by timeout expiration.

**The function (sync.c):**

1. **Check interrupt flag.** If `_ST_FL_INTERRUPT` is set on the current thread, clear it and return `EINTR` immediately — the coroutine was interrupted by `st_thread_interrupt()` before it even started sleeping.

2. **Set state and enter sleep heap.** If a finite timeout is given: set `me->state = _ST_ST_SLEEPING`, then `_st_add_sleep_q(me, usecs)` which computes `me->due = last_clock + usecs`, sets the `_ST_FL_ON_SLEEPQ` flag, assigns a heap index, and inserts into the binary heap (O(log N)). If `ST_UTIME_NO_TIMEOUT` is passed (via `st_sleep(-1)`), the state is set to `_ST_ST_SUSPENDED` instead — no sleep queue entry, the coroutine hangs indefinitely until explicitly interrupted.

3. **Suspend.** `_st_switch_context(me)` saves the coroutine's CPU registers via `_st_md_cxt_save(me->context)` (returns 0 on save), then calls `_st_vp_schedule(me)` which picks the next runnable coroutine from `run_q` — or the idle thread if nothing else is ready — and switches to it via `_st_restore_context`.

4. **The coroutine is now frozen.** Its registers are saved in `me->context`, and it sits in the sleep heap with a computed deadline. It is **not** in `io_q` and has no epoll registration — this is the key difference from I/O wait.

5. **The idle thread wakes it.** The idle thread's `epoll_wait` uses the sleep heap root's deadline as its timeout. When `epoll_wait` returns (either from I/O on other fds or timeout expiration), `_st_vp_check_clock()` runs: it reads `now = st_utime()`, updates `last_clock`, then walks the sleep heap — any thread with `due <= now` is removed from the heap, set to `_ST_ST_RUNNABLE`, and inserted at the **head** of `run_q` (timed-out coroutines get priority over I/O-ready ones).

6. **Resume.** When the scheduler switches back, `_st_md_cxt_restore(me->context, 1)` restores registers and returns 1 (non-zero), so the `if` in `_st_switch_context` is skipped and execution continues after the `_st_switch_context()` call in `st_usleep()`. A final interrupt check is performed, then `return 0` — sleep complete.

**Comparison with I/O wait path:**

- `st_usleep` uses **only the sleep heap** — no `io_q`, no `epoll_ctl`, no epoll registration. The coroutine is woken exclusively by `_st_vp_check_clock()`.
- `st_read`/`st_write` (EAGAIN path) uses **both `io_q` and the sleep heap** — epoll watches the fd, and the sleep heap provides timeout fallback. The coroutine can be woken by either `_st_epoll_dispatch()` (I/O ready) or `_st_vp_check_clock()` (timeout), distinguished by the `on_ioq` flag.
- In both cases, `epoll_wait`'s timeout is derived from the sleep heap root, so pure-sleep coroutines still influence when `epoll_wait` returns.

**Timeout precision note:** The deadline is `last_clock + usecs`, not `now + usecs`. If CPU work happened since the last scheduler cycle (the last time `_st_vp_check_clock` updated `last_clock`), part of the sleep duration is already "consumed." For typical sleep durations (seconds), this staleness is negligible. This is why ST timeouts are designed for coarse-grained use — detecting broken connections or idle peers, not sub-millisecond timing.

**Related test cases (`st_utest_learn_kb.cpp`):**
- `TEST(LearnKB, ThreadInterruptWakeupFromUsleep)`
- `TEST(LearnKB, LocalStatePreservedAcrossYield)`
- `TEST(LearnKB, StartRoutineNotExecutedInline)`

## `st_mutex` — Cooperative Mutex Workflow

ST's mutex is simple because cooperative scheduling eliminates the need for atomic operations, spinlocks, or memory barriers — just pointer manipulation and coroutine switching.

**The struct (common.h):**
- `_st_thread_t *owner` — current mutex owner, NULL means unlocked
- `_st_clist_t wait_q` — linked list of coroutines waiting to acquire the mutex

**`st_mutex_lock()` (sync.c):**

1. **Check interrupt flag.** If `_ST_FL_INTERRUPT` is set on the current thread, clear it and return `EINTR` immediately — the coroutine was interrupted before it even attempted to acquire the lock. (Same pattern as `st_usleep` and `st_cond_timedwait`.)

2. **Uncontended (owner == NULL).** Set `lock->owner = me`, return 0. Instant — just a pointer comparison and assignment, the cheapest possible lock. Safe because cooperative scheduling guarantees no other coroutine runs between the check and the assignment.

3. **Same owner (owner == me).** Return `EDEADLK`. Deadlock detection — if you try to lock a mutex you already own, ST catches it immediately instead of hanging forever.

4. **Contended (owner == someone else).** The coroutine must wait:
   - Set `me->state = _ST_ST_LOCK_WAIT`
   - Insert `me` into `lock->wait_q` (FIFO — insert before tail via `st_clist_insert_before`)
   - Call `_st_switch_context(me)` — save registers, yield to scheduler
   - The coroutine is now frozen: sitting on `lock->wait_q`, **not** on `sleep_q` (no timeout), **not** on `io_q` (no I/O). It can only be woken by `st_mutex_unlock()` or `st_thread_interrupt()`
   - When resumed: remove self from `wait_q`, check if interrupted (return `EINTR` if interrupted and not the owner), otherwise return 0 — the coroutine now owns the mutex

**`st_mutex_unlock()` (sync.c):**

First checks that the caller actually owns the mutex (returns `EPERM` if not). Then walks `lock->wait_q` looking for a thread in `_ST_ST_LOCK_WAIT` state:

- **Waiter found:** Direct ownership transfer — sets `lock->owner = waiter` immediately, sets `waiter->state = _ST_ST_RUNNABLE`, inserts waiter into `run_q` (at tail, normal priority). The unlocker does **not** yield — it keeps running. The waiter resumes when the current coroutine eventually hits I/O or yields.
- **No waiters:** Simply sets `lock->owner = NULL`.

**Key design properties:**

- **No timeout.** Unlike `st_usleep` or `st_read`, `st_mutex_lock` has no timeout parameter. A waiting coroutine is not placed on the sleep heap — it waits indefinitely until unlocked or interrupted. For timed locking semantics, use `st_cond_timedwait` instead.
- **Direct ownership transfer.** When unlocking with waiters, `lock->owner = waiter` is set before the waiter even resumes. This prevents a third coroutine from grabbing the mutex between the unlock and the waiter's resumption. Safe because cooperative scheduling means no preemption between these operations.
- **FIFO fairness.** Waiters are added to the tail of `wait_q`; `st_mutex_unlock` walks from the head. First to wait is first to acquire. No starvation.
- **No spin, no atomic ops, no syscalls.** The uncontended path is a pointer comparison and assignment. The contended path suspends the coroutine entirely — no busy-waiting. All of this works because no other coroutine can run between a check and an assignment in cooperative scheduling.

**Related test cases (`st_utest_learn_kb.cpp`):**
- `TEST(LearnKB, MutexCooperativeWorkflow)`
- `TEST(LearnKB, ThreadInterruptWakeupFromMutexWait)`

## `st_cond` — Condition Variable Workflow

ST's condition variable (`st_cond`) follows a similar pattern to `st_mutex` — `wait_q` linked list, `_st_switch_context` to suspend, wake by setting state to `RUNNABLE` — but solves a fundamentally different problem: **waiting for a condition/event to happen**, not exclusive resource ownership.

**The struct (common.h):** Just a `wait_q` linked list — no `owner` field. Nobody "owns" a condition variable.

**Comparison with `st_mutex`:**
- **Purpose:** `st_mutex` = exclusive ownership of a resource. `st_cond` = wait for something to happen.
- **Owner:** `st_mutex` has `lock->owner`. `st_cond` has no ownership concept.
- **Timeout:** `st_mutex_lock` has no timeout — waits forever. `st_cond_timedwait` supports timeout via the sleep heap.
- **Wake semantics:** `st_mutex_unlock` transfers ownership to ONE waiter. `st_cond_signal` wakes ONE waiter, `st_cond_broadcast` wakes ALL waiters — no ownership transfer.
- **Wait state:** `st_mutex` uses `_ST_ST_LOCK_WAIT`. `st_cond` uses `_ST_ST_COND_WAIT`.

**`st_cond_timedwait()` (sync.c):**

1. **Check interrupt flag.** Return `EINTR` if `_ST_FL_INTERRUPT` is set.
2. **Enter wait queue.** Set `me->state = _ST_ST_COND_WAIT`, insert into `cvar->wait_q` (FIFO).
3. **Optionally enter sleep heap.** If `timeout != ST_UTIME_NO_TIMEOUT`, call `_st_add_sleep_q(me, timeout)`. This is the key difference from `st_mutex` — the coroutine lands in **two places simultaneously**: `cvar->wait_q` AND the sleep heap. It gets woken by whichever fires first.
4. **Suspend.** `_st_switch_context(me)` saves registers and yields to the scheduler.
5. **Resume.** Remove self from `wait_q`. Check `_ST_FL_TIMEDOUT` → return `ETIME`. Check `_ST_FL_INTERRUPT` → return `EINTR`. Otherwise return 0 (signaled successfully).

**Dual-wakeup mechanism:** When both `wait_q` and sleep heap are active, the coroutine wakes from whichever fires first:
- **Signal fires first** → `_st_cond_signal` removes the coroutine from the sleep heap (`_st_del_sleep_q`), sets it RUNNABLE. On resume, no `_ST_FL_TIMEDOUT` flag → returns 0.
- **Timeout fires first** → `_st_vp_check_clock` finds the coroutine in `_ST_ST_COND_WAIT` state, sets the `_ST_FL_TIMEDOUT` flag, moves it to `run_q` (at head — timeout priority). On resume, flag is set → returns `ETIME`. The coroutine is still on `wait_q` but removes itself upon resumption.

This is the same dual-wakeup pattern as the I/O path (`io_q` + sleep heap), but with `wait_q` instead of `io_q`.

**`_st_cond_signal()` (sync.c):**

Walks `cvar->wait_q` from head, for each thread in `_ST_ST_COND_WAIT` state: if the thread is on the sleep heap, removes it (`_st_del_sleep_q`); sets `thread->state = _ST_ST_RUNNABLE`; inserts into `run_q`. If not broadcast, breaks after the first waiter. If broadcast, continues to wake all waiters.

**`st_cond_wait()` is simply `st_cond_timedwait(cvar, ST_UTIME_NO_TIMEOUT)`** — no sleep heap entry, waits indefinitely until signaled or interrupted.

**Why SRS uses `st_cond` far more than `st_mutex`:** In a cooperative coroutine system, there is no preemption — no other coroutine runs between a check and an assignment, so mutual exclusion is rarely needed. What SRS needs constantly is "wait until something happens" — data arrives on an SRT socket, a stream becomes available, a client connects. `st_cond` is the primary tool for this. The coroutine-native SRT pattern is a perfect example: a coroutine calls `st_cond_wait` when `srt_recvmsg` returns EAGAIN, and a poller coroutine calls `st_cond_signal` when the fd becomes ready.

**Related test cases (`st_utest_learn_kb.cpp`):**
- `TEST(LearnKB, CondSignalWakeOne)`
- `TEST(LearnKB, CondBroadcastWakeAll)`
- `TEST(LearnKB, CondTimedwaitTimeout)`
- `TEST(LearnKB, ThreadInterruptWakeupFromCondWait)`

## `st_thread_exit()` — How a Coroutine Dies

`st_thread_exit(retval)` is called when a coroutine finishes — either explicitly by the user or implicitly via `_st_thread_main()` after the start function returns. It handles cleanup, joinability, and stack recycling. The coroutine never returns from this function.

**Step 1: Store return value and run destructors.** Sets `thread->retval = retval`, then calls `_st_thread_cleanup(thread)` which iterates all created thread-specific data keys (up to `key_max`, not `ST_KEYS_MAX`). For each key that has both a non-NULL value and a registered destructor function, it calls the destructor and clears the slot. This is ST's equivalent of pthread key destructors.

**Step 2: Decrement active count.** `_st_active_count--` — one fewer active coroutine. When this reaches 0, the idle thread will call `exit(0)` and the process terminates.

**Step 3: Handle joinable threads (the zombie path).** If `thread->term` is non-NULL (thread was created with `joinable = 1`):

1. Set `thread->state = _ST_ST_ZOMBIE` — the thread is dead but its resources are preserved for the joiner to inspect.
2. Insert into `zombie_q` — this keeps the thread struct alive so `st_thread_join()` can read `retval`.
3. `st_cond_signal(thread->term)` — wake any coroutine blocked in `st_thread_join()` waiting on this thread's termination condition variable.
4. `_st_switch_context(thread)` — the zombie suspends. It will only be rescheduled by `st_thread_join()` after the joiner has read the return value.
5. When rescheduled (by the joiner): destroy the termination condvar (`st_cond_destroy(thread->term)`), set `thread->term = NULL`.

**Step 4: Free the stack.** If the thread is not the primordial thread, `_st_stack_free(thread->stack)` puts the stack on the free list. (Valgrind deregistration happens just before this if `MD_VALGRIND` is enabled.) The primordial thread's stack is the process stack — it's never freed.

**Step 5: Final switch — no return.** `_st_switch_context(thread)` is called one last time. The scheduler picks the next runnable coroutine. Since the exiting thread is not on any queue (not in `run_q`, not in `zombie_q` anymore for non-joinable threads), it will never be scheduled again. Its stack has been freed (or put on the free list), and execution never returns here.

**Why zombies need two context switches:** The first `_st_switch_context` (step 3) suspends the zombie so the joiner can run and read `retval`. The joiner then puts the zombie back on `run_q` (see `st_thread_join` below). The second `_st_switch_context` (step 5) is the final one — after the joiner has extracted what it needs, the zombie resumes briefly to destroy its condvar and free its stack, then switches away forever.

**Non-joinable threads skip the zombie path entirely** — no condvar signal, no zombie queue, no waiting for a joiner. They go straight from cleanup → stack free → final switch.

**Related test cases (`st_utest_learn_kb.cpp`):**
- `TEST(LearnKB, ThreadExitExplicitRetvalThroughJoin)`
- `TEST(LearnKB, ThreadExitNonJoinableCannotJoin)`
- `TEST(LearnKB, ReturnValueThroughJoin)`

## `st_thread_join()` — Waiting for a Coroutine to Finish

`st_thread_join(thread, retvalp)` blocks the calling coroutine until the target thread exits. It's the mechanism for "wait for this coroutine to complete and get its result."

**Precondition checks:**

1. `thread->term == NULL` → thread is not joinable (created with `joinable = 0`). Returns `EINVAL`.
2. `_st_this_thread == thread` → trying to join yourself. Returns `EDEADLK`.
3. `term->wait_q` is non-empty → another coroutine is already waiting to join this thread. Returns `EINVAL`. Only one joiner is allowed per joinable thread.

**The wait loop:**

```c
while (thread->state != _ST_ST_ZOMBIE) {
    if (st_cond_timedwait(term, ST_UTIME_NO_TIMEOUT) != 0)
        return -1;
}
```

The joiner calls `st_cond_timedwait` on the target's termination condvar with no timeout — it suspends indefinitely. When `st_thread_exit()` signals this condvar, the joiner wakes up and checks if the target is in `_ST_ST_ZOMBIE` state. If yes, the loop exits. If not (for example, a spurious wakeup), it waits again. If interrupted via `st_thread_interrupt`, `st_cond_timedwait` returns an error and `st_thread_join()` returns `-1` immediately.

**After the target is zombie:**

1. Read the return value: `*retvalp = thread->retval` (if `retvalp` is non-NULL).
2. **Reschedule the zombie for final cleanup:** Remove the zombie from `zombie_q`, set its state to `_ST_ST_RUNNABLE`, insert into `run_q`. This lets the zombie resume in `st_thread_exit()` to destroy its condvar and free its stack (the second `_st_switch_context` in `st_thread_exit`).

**Why the joiner reschedules the zombie instead of cleaning up directly:** The zombie's stack contains the `_st_thread_t` struct itself (thread metadata is carved from the top of the stack — see `st_thread_create`). If the joiner freed the stack, it would destroy the thread struct it's still reading from. Instead, the joiner puts the zombie back on `run_q`, and the zombie cleans up its own resources when it gets scheduled — running on its own stack, which is safe to free at the very end (the final `_st_switch_context` switches away before the stack memory is actually reused).

**The complete lifecycle of a joinable thread:**

1. `st_thread_create(start, arg, joinable=1, ...)` — born, placed on `run_q`
2. Scheduled → runs `_st_thread_main()` → runs `start(arg)`
3. `start` returns → `st_thread_exit(retval)` called
4. Cleanup runs, state → `_ST_ST_ZOMBIE`, inserted into `zombie_q`
5. `st_cond_signal(term)` wakes the joiner
6. First `_st_switch_context` — zombie suspends
7. Joiner wakes, reads `retval`, moves zombie from `zombie_q` to `run_q`
8. Zombie resumes, destroys condvar, frees stack
9. Final `_st_switch_context` — zombie is gone forever

**Related test cases (`st_utest_learn_kb.cpp`):**
- `TEST(LearnKB, ReturnValueThroughJoin)`
- `TEST(LearnKB, JoinDrivesFirstRunWhenNoManualYield)`
- `TEST(LearnKB, ThreadExitExplicitRetvalThroughJoin)`
- `TEST(LearnKB, ThreadExitNonJoinableCannotJoin)`
- `TEST(LearnKB, StartRoutineNotExecutedInline)`

## `st_thread_interrupt()` — Waking a Coroutine From Any Wait State

`st_thread_interrupt(thread)` is the "cancel" mechanism — it forces a coroutine to wake up regardless of what it's waiting on (I/O, sleep, condvar, mutex). The interrupted coroutine sees `EINTR` when it resumes.

**The function (sched.c):**

1. **Dead thread check.** If `thread->state == _ST_ST_ZOMBIE`, return immediately — can't interrupt a dead thread.

2. **Set the interrupt flag.** `thread->flags |= _ST_FL_INTERRUPT`. This flag persists until the interrupted coroutine checks and clears it upon resumption.

3. **Already running/runnable check.** If the thread is `_ST_ST_RUNNING` or `_ST_ST_RUNNABLE`, just return — the flag is set, and the thread will see it next time it enters an I/O or wait function. No queue manipulation needed.

4. **Remove from sleep heap (if present).** If `_ST_FL_ON_SLEEPQ` is set, call `_st_del_sleep_q(thread)` to remove it from the timeout heap. This is necessary because the thread is being woken prematurely — its timeout is no longer relevant.

5. **Make runnable.** Set `thread->state = _ST_ST_RUNNABLE`, insert into `run_q` (at tail via `st_clist_insert_before`).

**What the function does NOT do:** It does not remove the thread from `io_q` or `cvar->wait_q`. This is handled by the interrupted coroutine itself when it resumes — the same pattern as timeout wakeups in `st_poll()` (checks `pq.on_ioq`), `st_cond_timedwait()` (removes self from `wait_q`), and `st_mutex_lock()` (removes self from `wait_q`).

**How each wait function detects interruption:**

- **`st_poll()` (I/O wait):** After resuming from `_st_switch_context`, checks `me->flags & _ST_FL_INTERRUPT`. If set, clears the flag, sets `errno = EINTR`, returns -1. The `pq.on_ioq` flag is still 1 (dispatch didn't wake us), so the poll entry is also cleaned up from `io_q` and epoll.

- **`st_usleep()` (sleep):** Checks the interrupt flag both before sleeping and after resuming. If set, clears it, returns `EINTR`.

- **`st_cond_timedwait()` (condvar wait):** After resuming, removes self from `wait_q`, then checks `_ST_FL_INTERRUPT`. If set, clears it, returns `EINTR`.

- **`st_mutex_lock()` (mutex wait):** After resuming, removes self from `wait_q`. If the interrupt flag is set AND the thread is not the mutex owner (another thread could have unlocked the mutex at the same time as the interrupt), returns `EINTR`.

**The interrupt flag is "sticky" until consumed:** Once set, it stays set until a blocking path checks and clears it. If the thread is already running, the flag has no immediate effect. At the next wait point with explicit interrupt checks (`st_poll`, `st_usleep`, `st_cond_timedwait`, `st_mutex_lock`), the call returns `EINTR` instead of remaining blocked. For I/O wrappers like `st_read`/`st_write`, interruption is observed when they enter `st_poll` (typically after `EAGAIN`); if the syscall succeeds immediately, they may return data instead of `EINTR`. This design prevents races where interrupt arrives between deciding to wait and actually suspending.

**Interrupt vs. the sleep heap:** When a thread is on both `io_q`/`wait_q` and the sleep heap (e.g., `st_cond_timedwait` with timeout), `st_thread_interrupt` only removes it from the sleep heap. The thread remains on `io_q`/`wait_q` until it resumes and cleans up. This is safe because the cleanup is always done by the thread itself after waking.

**Use in SRS:** `st_thread_interrupt` is how SRS implements graceful shutdown. When SRS needs to stop (e.g., SIGINT), it interrupts all active coroutines. Each coroutine's I/O call returns `EINTR`, the coroutine sees the shutdown flag, and exits cleanly. Without this mechanism, coroutines blocked on I/O would never wake up to check if the server is shutting down.

**Critical lifecycle rule (do not assume immediate termination):** `st_thread_interrupt()` does **not** terminate the target coroutine synchronously. It only marks/wakes the coroutine so its current blocking call can return (typically `-1` with `errno=EINTR`, for example `st_read`). The coroutine must then cooperatively unwind and return from its entry function. Therefore, the interrupter should use a join/synchronization step (for joinable threads, `st_thread_join`) to wait for actual thread exit, instead of assuming interrupt == already dead.

**Related test cases (`st_utest_learn_kb.cpp`):**
- `TEST(LearnKB, ThreadInterruptWakeupFromUsleep)`
- `TEST(LearnKB, ThreadInterruptWakeupFromCondWait)`
- `TEST(LearnKB, ThreadInterruptWakeupFromMutexWait)`

## The Netfd Abstraction (`_st_netfd_t`) — ST's File Descriptor Wrapper

Most socket/file descriptors used with ST's convenience I/O APIs are wrapped in a `_st_netfd_t`. This struct is the bridge between raw OS file descriptors and ST's coroutine I/O system. Most ST I/O helpers (`st_read`, `st_write`, `st_accept`, `st_connect`, etc.) take `_st_netfd_t*` instead of raw `int` fds, while the lower-level `st_poll()` API still accepts raw `struct pollfd` descriptors.

**The struct (common.h):**

```c
typedef struct _st_netfd {
    int osfd;                    /* Underlying OS file descriptor */
    int inuse;                   /* In-use flag */
    void *private_data;          /* Per-descriptor private data */
    _st_destructor_t destructor; /* Private data destructor function */
    void *aux_data;              /* Auxiliary data for internal use */
    struct _st_netfd *next;      /* For putting on the free list */
} _st_netfd_t;
```

- `osfd` — The actual OS file descriptor number (what you'd pass to `read(2)`, `write(2)`)
- `inuse` — Whether this wrapper is active (1) or recycled on the free list (0)
- `private_data` + `destructor` — Per-fd user data with cleanup callback, set via `st_netfd_setspecific()`. SRS uses this to attach application-level connection objects to the fd
- `aux_data` — Reserved for internal use (currently a no-op; historically used for accept serialization in multi-process setups)
- `next` — Singly-linked list pointer for the free list

**The Free List — Object Recycling:**

`_st_netfd_t` objects are recycled via a thread-local singly-linked free list (`_st_netfd_freelist`). When a netfd is freed (`st_netfd_free`), it's pushed onto the free list. When a new netfd is needed (`_st_netfd_new`), the free list is checked first — if non-empty, a recycled object is popped; otherwise a fresh one is `calloc`'d. This avoids malloc/free churn for the most frequently created/destroyed objects in a server (one per connection).

The free list is `static __thread` — each pthread in a multi-threaded ST setup has its own free list, requiring no locking. Note: the earlier documentation mentioned the netfd freelist having a pthread mutex as the only shared-state lock. Looking at the current code, the freelist is actually thread-local (`static __thread _st_netfd_t *_st_netfd_freelist`), so no mutex is needed. The mutex for shared netfd state existed in older versions of the toffaletti multi-threading fork but the current ossrs/state-threads code uses `__thread` isolation instead.

**Creating a Netfd — `_st_netfd_new(osfd, nonblock, is_socket)`:**

This is the internal constructor called by all public creation functions:

1. **Notify the event system:** Calls `_st_eventsys->fd_new(osfd)`. For epoll, this is `_st_epoll_fd_new` which ensures the per-fd data array (`fd_data`) is large enough to hold the fd's index — expanding it via `realloc` if needed. For kqueue, this does the same with its own `fd_data` array. This is how ST tracks per-fd reference counts and revents.
2. **Get or allocate the wrapper:** Pop from `_st_netfd_freelist` if available, otherwise `calloc` a new one.
3. **Set non-blocking mode:** If `nonblock` is true (for example in `st_netfd_open_socket`, and in `st_accept` on platforms where accepted sockets do not inherit non-blocking mode), sets `O_NONBLOCK`. For sockets, tries `ioctl(FIONBIO)` first (one syscall) — if that fails, falls back to `fcntl(F_GETFL)` + `fcntl(F_SETFL, O_NONBLOCK)` (two syscalls). Non-blocking mode is essential — without it, `read`/`write` would block the entire process instead of returning `EAGAIN` for the coroutine to handle.
4. **Return the wrapper** with `osfd` set and `inuse = 1`.

**Public creation functions:**

- `st_netfd_open(osfd)` — Wrap any fd, set non-blocking. Used for pipes, FIFOs, etc. Calls `_st_netfd_new(osfd, 1, 0)` — `is_socket=0` means it skips the `ioctl(FIONBIO)` shortcut.
- `st_netfd_open_socket(osfd)` — Wrap a socket fd, set non-blocking. Calls `_st_netfd_new(osfd, 1, 1)` — `is_socket=1` enables the faster `ioctl` path.
- `st_open(path, oflags, mode)` — Open a file/FIFO with `O_NONBLOCK` added to `oflags`, then wrap it. The fd is already non-blocking from the `open()` call, so `_st_netfd_new` is called with `nonblock=0` (no need to set it again).

**Closing a Netfd — `st_netfd_close(fd)`:**

1. **Notify the event system:** Calls `_st_eventsys->fd_close(fd->osfd)`. For epoll, `_st_epoll_fd_close` checks that no coroutines are still watching this fd (all reference counts must be zero) — if any are non-zero, it returns `EBUSY` and the close fails. This prevents closing an fd that other coroutines are waiting on.
2. **Recycle the wrapper:** Calls `st_netfd_free(fd)` which clears `inuse`, calls the private data destructor if set, and pushes the wrapper onto the free list.
3. **Close the OS fd:** `close(fd->osfd)`.

**Freeing without closing — `st_netfd_free(fd)`:**

Sometimes you want to release the ST wrapper without closing the underlying OS fd (e.g., if another library owns the fd lifecycle). `st_netfd_free` does this: clears `aux_data`, calls the private data destructor, sets `inuse = 0`, and pushes to the free list. The OS fd remains open.

**The Per-Fd Data Array in the Event System:**

The event system maintains an array indexed by OS fd number. Initial sizing is backend-specific (epoll starts from `fd_hint`, derived from fd limits and `ST_EPOLL_EVTLIST_SIZE`; kqueue starts from `FD_SETSIZE`), and then grows dynamically via `realloc` as larger fd values appear. For epoll, this is `_st_epoll_data->fd_data` — an array of `_epoll_fd_data_t`:

```c
typedef struct _epoll_fd_data {
    int rd_ref_cnt;   /* Number of coroutines waiting for read */
    int wr_ref_cnt;   /* Number of coroutines waiting for write */
    int ex_ref_cnt;   /* Number of coroutines waiting for exception */
    int revents;      /* Fired events from last epoll_wait */
} _epoll_fd_data_t;
```

This is **not** inside `_st_netfd_t` — it's a separate array in the event system, indexed by raw fd number. The reference counts track how many coroutines are watching each direction on each fd. When `st_poll()` registers a coroutine via `_st_epoll_pollset_add`, it increments the appropriate counts and calls `epoll_ctl(EPOLL_CTL_ADD)` or `epoll_ctl(EPOLL_CTL_MOD)`. During wakeup/timeout cleanup, `_st_epoll_pollset_del` decrements counts and updates epoll only for descriptors that did **not** fire in the current dispatch pass; fired descriptors are cleaned up in the dispatch function's second pass.

This separation (netfd wrapper vs. event system per-fd data) is a clean design: the netfd is the application-facing handle, while the per-fd data is the event system's internal bookkeeping. Multiple netfd wrappers could theoretically point to the same osfd (though this would be unusual), and the event system tracks watchers by raw fd number regardless.

**I/O Initialization — `_st_io_init()`:**

Called once during `st_init()`, this function does two things:

1. **Ignore SIGPIPE.** Sets `SIGPIPE` handler to `SIG_IGN` via `sigaction`. Without this, writing to a closed socket would kill the process. With it, `write()` just returns `EPIPE` which ST handles normally.
2. **Maximize fd limit.** Reads `RLIMIT_NOFILE` via `getrlimit`, raises `rlim_cur` to `rlim_max` via `setrlimit`, and stores the result in `_st_osfd_limit`. On macOS where `rlim_max` can be negative (a platform quirk), it falls back to the event system's limit or `rlim_cur`. This fd limit influences backend initial sizing, but per-fd arrays are still expanded dynamically as needed.

**Per-Fd Private Data — `st_netfd_setspecific` / `st_netfd_getspecific`:**

These let application code attach arbitrary data to a netfd, similar to pthread key-specific data but per-fd instead of per-thread. `st_netfd_setspecific(fd, value, destructor)` stores a pointer and a cleanup function; when the netfd is freed, the destructor is called automatically. SRS uses this to associate connection handler objects with their socket fds.

**Why most ST network I/O helpers take `_st_netfd_t*` (with `st_poll` as the raw-fd exception):**

The netfd wrapper ensures three invariants: (1) the fd is always non-blocking, (2) the event system knows about the fd and can track watchers, and (3) the fd can carry application-specific data. Without the wrapper, application code could accidentally use a blocking fd with ST's helper I/O functions, bypassing the coroutine scheduler and stalling the entire process. For APIs that require `_st_netfd_t*`, this is a compile-time guard against passing raw `int` fds; when direct raw-fd polling is needed, ST exposes `st_poll()` for that purpose.

**Related test cases (`st_utest_learn_kb.cpp`):**
- `TEST(LearnKB, NetfdSpecificAndDestructorOnClose)`
- `TEST(LearnKB, NetfdFreeKeepsOsfdOpen)`
- `TEST(LearnKB, BasicNetfdWriteThenRead)`
- `TEST(LearnKB, BasicNetfdReadTimeout)`

## The Event System Abstraction (`_st_eventsys_t`) — How ST Swaps I/O Backends

ST uses a vtable pattern — a struct of function pointers — so the scheduler can call I/O multiplexing operations without knowing which backend (epoll, kqueue, or select) is active.

**The vtable struct (common.h):**

```c
typedef struct _st_eventsys_ops {
    const char *name;                          /* "select", "kqueue", "epoll" */
    int  val;                                  /* ST_EVENTSYS_SELECT or ST_EVENTSYS_ALT */
    int  (*init)(void);                        /* Create the OS multiplexer */
    void (*dispatch)(void);                    /* The blocking wait + wake coroutines loop */
    int  (*pollset_add)(struct pollfd *, int); /* Register fds when a coroutine starts waiting */
    void (*pollset_del)(struct pollfd *, int); /* Unregister fds when I/O completes or times out */
    int  (*fd_new)(int);                       /* Notify backend about a new fd (expand arrays) */
    int  (*fd_close)(int);                     /* Check fd can be closed (no active watchers) */
    int  (*fd_getlimit)(void);                 /* Hard fd limit (FD_SETSIZE for select, 0=unlimited) */
    void (*destroy)(void);                     /* Tear down the event system */
} _st_eventsys_t;
```

The global pointer `__thread _st_eventsys_t *_st_eventsys` is thread-local — each pthread in a multi-threaded ST setup gets its own event system instance. Each backend defines a static instance of this struct with its functions filled in (e.g., `_st_epoll_eventsys`, `_st_kq_eventsys`, `_st_select_eventsys`), and `st_set_eventsys()` points the global at the chosen one.

**The pollfd bridge:** All three backends speak `struct pollfd` at the interface level — `pollset_add` and `pollset_del` take `struct pollfd*`. This is the abstraction layer. The scheduler only knows about `POLLIN`/`POLLOUT`/`POLLPRI`. Each backend translates these to its native API internally (epoll events, kqueue filters, or fd_sets).

**How the scheduler uses the vtable:** The scheduler never calls `epoll_wait` or `kevent` or `select` directly. Every call goes through `_st_eventsys->`:

- `st_init()` → `_st_eventsys->init()` — creates epoll fd / kqueue fd / initializes fd_sets
- Idle thread loop → `_st_eventsys->dispatch()` — the big blocking wait
- `st_poll()` → `_st_eventsys->pollset_add(pds, npds)` — when a coroutine suspends on I/O
- I/O completion / timeout → `_st_eventsys->pollset_del(pds, npds)` — cleanup registrations
- `_st_netfd_new()` → `_st_eventsys->fd_new(osfd)` — expand per-fd arrays if needed
- `st_netfd_close()` → `_st_eventsys->fd_close(osfd)` — check no active watchers before close

**Three backends, compile-time selected:**

- **select** — fallback backend (used on Cygwin64 and also compiled on Darwin/Linux). Compile flag `MD_HAVE_SELECT`. Val `ST_EVENTSYS_SELECT` (1). Hard limit of `FD_SETSIZE` fds. State stored in `__thread` `fd_set`s with per-fd reference count arrays.
- **kqueue** — macOS/Darwin. Compile flag `MD_HAVE_KQUEUE`. Val `ST_EVENTSYS_ALT` (3). No fd limit. State in `__thread` struct with per-fd data array, plus add/delete kevent lists.
- **epoll** — Linux. Compile flag `MD_HAVE_EPOLL`. Val `ST_EVENTSYS_ALT` (3). No fd limit. State in `__thread` struct with per-fd data array (read/write/exception reference counts + revents).

Kqueue and epoll both use `ST_EVENTSYS_ALT` — they're interchangeable "advanced" backends. Select is the fallback. The Makefile defines which flags are set per platform: Darwin gets `MD_HAVE_KQUEUE` + `MD_HAVE_SELECT`, Linux gets `MD_HAVE_EPOLL` + `MD_HAVE_SELECT`, Cygwin64 gets only `MD_HAVE_SELECT`. If none of the three are defined, compilation fails with `#error`.

**Backend selection (`st_set_eventsys`):**

`st_init()` calls `st_set_eventsys(ST_EVENTSYS_DEFAULT)`, which maps to select (it resolves to `ST_EVENTSYS_SELECT` internally). To get epoll or kqueue, application code must call `st_set_eventsys(ST_EVENTSYS_ALT)` **before** `st_init()`. For `ST_EVENTSYS_ALT`: if `MD_HAVE_KQUEUE` is defined, kqueue is used; else if `MD_HAVE_EPOLL` is defined and `_st_epoll_is_supported()` confirms the kernel actually supports it (by probing `epoll_ctl` for `ENOSYS`), epoll is used. Once set, the pointer is locked — calling `st_set_eventsys` again returns `EBUSY`. SRS calls `st_set_eventsys(ST_EVENTSYS_ALT)` to get epoll on Linux and kqueue on macOS.

**The uniform dispatch pattern:** Despite different OS APIs, all three `dispatch` functions follow the same structure:

1. Calculate timeout from sleep heap root (`sleep_q->due - last_clock`)
2. Call the OS blocking function (`select` / `kevent` / `epoll_wait`)
3. Mark fired fds in per-fd state
4. Walk `io_q` — for each waiting coroutine, check its fds against fired results
5. If any fd matched → remove from `io_q` (`on_ioq = 0`), call `pollset_del` to clean up registrations, remove from sleep heap if present, set `_ST_ST_RUNNABLE`, insert into `run_q`
6. Clean up OS-level state for fired fds

The scheduler, idle thread, coroutine suspension/resumption — all identical regardless of backend. Only the vtable functions differ.

**Backend-specific details:**

*Select:*
- Maintains three `__thread` `fd_set`s (read/write/exception) with per-fd reference counts. `dispatch` must copy all three fd_sets before calling `select()` because select modifies them in place.
- Has a `_st_select_find_bad_fd()` recovery handler — when `select` returns `EBADF`, it walks all waiting fds with `fcntl(F_GETFL)` to identify and remove the bad one.
- The `maxfd` tracker is maintained across add/del/dispatch — select requires the highest fd number + 1 as its first argument.
- The select backend data is `__thread` (`_st_select_data`), so each pthread keeps independent select bookkeeping.

*Kqueue:*
- Uses `EV_ONESHOT` flag — each registration fires once and auto-deregisters from kqueue. Elegant: no need to explicitly delete fired fds. Only unfired fds need explicit `EV_DELETE`.
- Batches additions via an `addlist` — `pollset_add` queues `struct kevent` entries, and `dispatch` submits them all in a single `kevent()` call via the changelist parameter. This reduces syscalls.
- Deletions are synchronous — `pollset_del` calls `kevent()` immediately with a `dellist` to avoid stale fd problems (can't defer because the fd might be closed before the next dispatch).
- Handles **fork recovery** — if `getpid()` changes after `kevent` returns `EBADF`, it re-creates the kqueue fd and re-registers all fds from `io_q`. Kqueue fds don't survive `fork()`.
- Timeout uses `struct timespec` (nanosecond precision).
- `destroy` exists in the vtable, but current kqueue backend cleanup is a TODO (`_st_kq_destroy` is not implemented yet).

*Epoll:*
- Level-triggered (no `EPOLLET` flag) — simplest model, events re-fire on every `epoll_wait` until the fd is consumed or removed.
- Uses reference counting (`rd_ref_cnt`, `wr_ref_cnt`, `ex_ref_cnt`) per fd to support multiple coroutines watching the same fd. `epoll_ctl` is called to `ADD`/`MOD`/`DEL` as reference counts transition between 0 and non-zero.
- `dispatch` has two cleanup passes for fired fds: the first pass (inside the `io_q` walk) calls `pollset_del` which handles unfired fds — but skips fired fds because their `_ST_EPOLL_REVENTS` is still set. The second pass (after the `io_q` walk) iterates the epoll result list, clears revents, and calls `EPOLL_CTL_MOD` or `EPOLL_CTL_DEL` based on remaining reference counts. This two-pass design avoids modifying epoll state while still iterating results that depend on it.
- Timeout uses milliseconds (epoll_wait limitation). Rounds up sub-millisecond timeouts to 1ms to avoid spin loops — if `min_timeout > 0` but computes to 0ms, it's bumped to 1ms.
- `_st_epoll_is_supported()` probes at selection time by calling `epoll_ctl(-1, EPOLL_CTL_ADD, -1, &ev)` — if errno is `ENOSYS`, epoll syscalls are stubs and the backend is rejected.

**Related test cases (`st_utest_learn_kb.cpp`):**
- `TEST(LearnKB, EventSysSelectedAndLockedAfterInit)`
- `TEST(LearnKB, BasicNetfdReadTimeout)`
- `TEST(LearnKB, BasicNetfdWriteThenRead)`
