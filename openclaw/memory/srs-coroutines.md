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

## Coroutine-Native SRT

SRS 4.0 (2019) added SRT support, but the initial implementation used libsrt's own threads and async I/O, separate from ST. This caused complex async code was difficult to maintain.

In SRS 5.0, SRT was rewritten to be **coroutine-native** (PR#3010). The pattern for making any protocol coroutine-native:

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

## Can AI Replace RUST for ST Maintenance?

RUST (specifically tokio) is conceptually similar to ST — both are polling-based async with cooperative scheduling. RUST offers advantages: no assembly needed, built-in multi-thread support, cross-platform without manual porting, better tooling. The "Hidden Flaws of SRS" blog explored RUST as a potential future direction.

However, the real question is not about language features but about **ecosystem and maintenance capability**. If AI proves capable of maintaining ST's assembly code — understanding CPU register conventions, porting to new architectures, debugging platform-specific issues like Windows/SEH — then the ST maintenance burden disappears and there's no compelling reason to switch languages. The C++ ecosystem for the media industry (FFmpeg, libsrt, libwebrtc, and other open-source media streaming projects) matters more than language features.

RUST is a fallback path if AI cannot handle the low-level ST maintenance. It's not an inevitable direction. The deciding factor is AI capability, not language preference.

## Timeout Semantics

ST timeouts have a subtle but important behavior: the timeout parameter is relative to `last_clock` (the timestamp of the last scheduler cycle), not the moment the function is called.

When you call `st_read(fd, buf, n, timeout)`, internally ST computes the deadline as `due = last_clock + timeout`. The `last_clock` value is updated in `_st_vp_check_clock()`, which only runs during scheduler cycles — in the idle thread loop after `dispatch()` returns, and in `st_thread_yield()`. Between those points, `last_clock` is frozen.

This means if a coroutine does CPU work between its last context switch and the I/O call, part of the timeout has already been "consumed" by that CPU time. For example:

- `last_clock` was set 8ms ago, you call `st_read()` with a 10ms timeout → effective wait is at most 2ms
- `last_clock` was set 12ms ago, you call `st_read()` with a 10ms timeout → deadline is already in the past, may return as timed out immediately on the next scheduler cycle

ST timeouts are suitable for coarse-grained purposes — detecting broken connections, idle peers, or stuck operations. Realistic timeouts should be on the order of seconds (e.g., 5s, 30s), where `last_clock` staleness is negligible. They are not designed for precise sub-millisecond timing.
