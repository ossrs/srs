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

The multi-threading support in ST works by making each pthread run its own independent ST scheduler. The key mechanism is GCC's `__thread` storage class — every global/static variable that holds scheduler state becomes thread-local, so each pthread gets its own copy.

This approach was pioneered by [toffaletti's fork](https://github.com/toffaletti/state-threads) and later adopted by ossrs/state-threads ([state-threads#19](https://github.com/ossrs/state-threads/issues/19)).

All global and static variables that hold scheduler state were converted to `__thread`: the VP struct, current thread pointer, active count, time cache in sched.c; the event system pointer and per-backend data structs (select/poll/kqueue/epoll) in event.c; the free stack list and counters in stk.c; the destructor table and key counter in key.c; plus matching `extern` declarations in common.h and sync.c.

Beyond adding `__thread`, several supporting changes were needed: each event backend got a `free()` method for per-pthread cleanup; `__thread` variables with dynamic initializers had to be initialized explicitly in `st_init()`; I/O init was wrapped in `pthread_once` to run only once; a primordial thread pointer was added to `_st_vp` so the idle thread can return cleanly; and the netfd freelist — the only truly shared resource — got a `pthread_mutex_t` since file descriptors can be passed between threads.

**The design principle:** Each pthread gets a complete, isolated ST scheduler — its own run queues, event loop, coroutine stacks, and timers. No locking needed for scheduler operations. The only shared state requiring a mutex is the netfd freelist. This is why the approach is clean — it's essentially N independent single-threaded ST instances that happen to live in the same process.

**Why SRS moved away from this:** The `__thread` approach works at the ST library level — each thread runs an independent coroutine scheduler correctly. But at the application level (SRS), threads must still communicate and share streams. Load balancing across threads proved nearly impossible to observe or manage (see "Multi-CPU: Cluster, Not Multi-Threading" above). The cluster architecture solves multi-CPU without these problems.

## Porting ST to New Platforms

Porting ST to a new OS/CPU is simpler than it sounds. The core task is implementing two assembly functions: `_st_md_cxt_save` (save registers) and `_st_md_cxt_restore` (restore registers) — the custom replacements for `setjmp`/`longjmp`.

**Current platform support (from [state-threads#22](https://github.com/ossrs/state-threads/issues/22)):**

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

Note: All `.S` files check `MD_ST_NO_ASM` — historically this allowed disabling assembly and falling back to libc's `setjmp`/`longjmp`. Since the libc setjmp path has been removed (all platforms now require assembly), this macro no longer works — defining it will cause linker errors. It remains in the code as a leftover.

**What registers to save?**
Only the **callee-saved registers** matter — these are the registers a function must preserve across calls. The actual registers saved by ST's assembly (from the `.S` files):

- **i386 (Linux):** ebx, esi, edi, ebp, esp, pc
- **x86-64 (Linux/Darwin/Cygwin64):** rbx, rbp, r12-r15, rsp, pc
- **ARM v7 (Linux):** v1-v6, sl, fp, sp, lr (i.e., r4-r9, r10, r11, r13, r14); optionally VFP d8-d15 and iWMMXt wr10-wr15
- **AArch64 (Linux/Darwin):** x19-x28 (callee-saved), x29 (frame pointer), x30/lr (link register), sp, plus floating-point d8-d15
- **MIPS/MIPS64 (Linux):** sp, gp, fp/s8, s0-s7, ra
- **LoongArch64 (Linux):** sp (r3), ra (r1), fp (r22), s0-s8 (r23-r31)
- **RISC-V (Linux):** sp, ra, fp/s0, s1-s11

**The jmpbuf problem:**
Different platforms define `jmp_buf` differently. Most use a field named `__jmpbuf`, but MIPS uses `__jb`, and field sizes differ (MIPS has 4-byte pointers with 8-byte `long long` jmpbuf entries). [state-threads#29](https://github.com/ossrs/state-threads/pull/29) addressed this by having ST define and use its own jmpbuf structure where possible, rather than relying on platform-specific layouts.

The macro `MD_GET_SP(_t)` in `md.h` defines how to read/write the stack pointer in the jmpbuf for each platform. This is critical for `MD_INIT_CONTEXT` — when creating a coroutine, the SP in the saved context must be updated to point at the heap-allocated stack, since the coroutine can't use the creator's stack.

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
2. **Study calling conventions:** Compile `tools/pcs.c` and use GDB's `si` (step instruction) to step through function call assembly — identify which registers are callee-saved (these are the ones you must save/restore in ST). Also refer to vendor docs (ARM/MIPS/RISC-V reference manuals) for the full callee-saved register list
3. **Run jmpbuf.c:** Compile and debug `tools/jmpbuf.c` to learn which registers are saved by setjmp and understand the jmp_buf layout on your platform
4. **Run porting.c:** Compile and run `tools/porting.c` to see register layout and check if setjmp stores registers in plaintext. Use GDB's `disassemble` on setjmp to see exactly which registers it saves and in what order — this is a quick way to learn and confirm the register list
5. **Add empty stubs:** In the appropriate `.S` file, add `_st_md_cxt_save` and `_st_md_cxt_restore` under a new `#elif defined(__your_cpu__)` — empty functions that just return. Build `verify.c` and `helloworld.c` to confirm compilation and linking succeed, even though they won't run correctly yet
6. **Implement the assembly:** Fill in the actual `sw`/`lw` (MIPS), `str`/`ldr` (ARM), `sd`/`ld` (RISC-V) instructions to save/restore each callee-saved register to/from the jmpbuf. `_st_md_cxt_save` returns 0; `_st_md_cxt_restore` sets return value to 1 and jumps to the saved return address
7. **Define MD_GET_SP:** In `md.h`, add the macro for your platform so `MD_INIT_CONTEXT` can replace the SP with the coroutine's heap-allocated stack address
8. **Test with helloworld:** If it prints messages with `st_sleep` pauses, context switching works
9. **Test with verify:** Run `verify.c` for full API test — thread creation, mutex, cond variable, usleep, thread join. Also use it early (after adding empty stubs) to verify compilation and linking before implementing the assembly

**Platform-specific build commands:**
- Linux: `make linux-debug` (auto-detects CPU)
- macOS: `make darwin-debug`
- Windows: `make cygwin64-debug`
- Force CPU: `make linux-debug EXTRA_CFLAGS="-D__aarch64__"` (if auto-detection fails)

**Community contributions:**
Several ports came from the community — RISC-V support ([state-threads#28](https://github.com/ossrs/state-threads/pull/28)) was contributed by T-bagwell (Steven Liu, Kuaishou) and later adopted by Arch Linux RISC-V. LoongArch64 ([state-threads#24](https://github.com/ossrs/state-threads/issues/24)) was driven by Loongson's new ISA replacing their earlier MIPS-based chips (3A4000 used mips64, 3A5000+ uses loongarch64). The Apple M1 port ([state-threads#30](https://github.com/ossrs/state-threads/issues/30)) required separate work from Linux aarch64 because Darwin has different calling conventions — notably Apple's [ARM64 platform requirements](https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms).

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

When you call `st_read(fd, buf, n, timeout)`, internally ST computes the deadline as `due = last_clock + timeout`. The `last_clock` value is updated in `_st_vp_check_clock()`, which only runs during scheduler cycles — in the idle thread loop after `dispatch()` returns, and in `st_thread_yield()`. Between those points, `last_clock` is frozen.

This means if a coroutine does CPU work between its last context switch and the I/O call, part of the timeout has already been "consumed" by that CPU time. For example:

- `last_clock` was set 8ms ago, you call `st_read()` with a 10ms timeout → effective wait is at most 2ms
- `last_clock` was set 12ms ago, you call `st_read()` with a 10ms timeout → deadline is already in the past, may return as timed out immediately on the next scheduler cycle

ST timeouts are suitable for coarse-grained purposes — detecting broken connections, idle peers, or stuck operations. Realistic timeouts should be on the order of seconds (e.g., 5s, 30s), where `last_clock` staleness is negligible. They are not designed for precise sub-millisecond timing.

## `st_init()` — How the Coroutine World is Built

`st_init()` is the entry point that bootstraps the entire coroutine runtime. It creates the scheduler data structures, the event system, the idle thread, and wraps the calling OS thread as the first coroutine. Here's what happens step by step:

**Step 1: Set up the I/O event system.** Calls `st_set_eventsys(ST_EVENTSYS_DEFAULT)` to select the OS-level I/O multiplexer (epoll on Linux, kqueue on macOS), then `_st_io_init()` for one-time I/O setup (via `pthread_once`). Then calls `(*_st_eventsys->init)()` to create the actual epoll/kqueue file descriptor.

**Step 2: Initialize all scheduler queues.** Four empty linked lists:
- `run_q` — coroutines ready to run
- `io_q` — coroutines blocked on socket I/O
- `zombie_q` — dead coroutines awaiting cleanup
- `_st_free_stacks` — recycled stack free list for reuse

Also captures `pagesize` (for stack guard pages) and `last_clock` (current timestamp for timeout calculations).

**Step 3: Create the Idle Thread.** This is the heart of ST. The idle thread is created via `st_thread_create(_st_idle_thread_start)`, then marked with `_ST_FL_IDLE_THREAD`, decremented from `_st_active_count` (it doesn't count as an "active" coroutine), and removed from the run queue (it's managed specially by the scheduler).

The idle thread's loop is the core scheduler cycle:
1. `(*_st_eventsys->dispatch)()` — calls `epoll_wait`/`kqueue`, blocking until I/O is ready or the earliest timeout fires. This is the **only place the process truly blocks**.
2. `_st_vp_check_clock()` — walks the sleep heap, moves timed-out coroutines to the run queue, updates `last_clock`.
3. `_st_switch_context(me)` — yields CPU to a ready coroutine from the run queue.
4. When that coroutine eventually switches back (hits I/O or yields), the idle thread loops again.
5. When `_st_active_count` drops to 0 (no more coroutines), the idle thread calls `exit(0)`.

**Step 4: Create the Primordial Thread.** The current OS thread (the one calling `st_init()`) is wrapped into an `_st_thread_t` struct via `calloc`. It gets no new stack — it reuses the existing process stack. Its state is set to `_ST_ST_RUNNING`, flagged as `_ST_FL_PRIMORDIAL`, and assigned to `_st_this_thread`. This becomes the first running coroutine and `_st_active_count` is incremented to 1.

**After `st_init()` returns**, the coroutine world is ready: event system initialized, idle thread created and waiting, primordial thread running. Control returns to `main()`, which is now executing as the primordial coroutine. From here, calling `st_thread_create()` spawns new coroutines, and the scheduler workflow activates once those coroutines hit their first I/O call.
