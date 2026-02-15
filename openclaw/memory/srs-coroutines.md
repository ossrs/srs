# SRS Coroutines

SRS uses **State Threads (ST)** — a C++ coroutine library that provides lightweight threads. It is the cornerstone of SRS's architecture.

**Key insight:** ST gives SRS the programming model of Go (one coroutine per connection, sequential code, state in local variables) but in C++. It's essentially a C++ version of Go's concurrency model.

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
- **This is what Go does with goroutines. SRS does the same thing in C++ using the State Threads (ST) library.**

## How Coroutine Switching Works

The tradeoff: coroutines make application code easy, but **someone has to implement the coroutine mechanism itself**.

When serving a connection, you can't just call a function to switch to another connection — that would push onto the same call stack. Instead, you need a **lightweight thread switch**: save the current coroutine's CPU registers to its memory, then load the target coroutine's saved registers and resume from where it left off.

This is the same concept as OS thread context switching, but:
- **OS thread switch:** heavy, involves kernel, expensive
- **Coroutine switch:** user-space only, just save/restore registers via function jumps (e.g., `setjmp`/`longjmp` or similar), very cheap

To implement this, you need to understand how function calls work at the CPU level — registers, stack pointers, program counters. The coroutine library handles all of this so application code never has to think about it.

## ST Library Origin and Design

State Threads is derived from Netscape Portable Runtime (NSPR), reduced from 400+ source files to just 8. It's not a general-purpose threading library — it specifically targets Internet Applications (servers that are network I/O driven).

Key design properties:
- **Deterministic scheduling:** Context switch can only happen at I/O points or explicit synchronization points — never preemptive, never time-sliced
- **No locks needed:** Because switching is deterministic, global data doesn't need mutex protection in most cases. The entire application can freely use static variables and non-reentrant library functions
- **Minimal syscalls:** No per-thread signal mask (unlike POSIX threads), so no save/restore of signal mask on context switch — eliminates two syscalls per switch
- **~5000 lines of code:** Small enough to understand completely, but requires assembly per CPU/OS platform

SRS maintains the fork at `ossrs/state-threads` (branch `srs`), continuously updating it to support modern CPUs and OSes including Linux, macOS, Windows, and architectures like x86_64, ARMv7, AARCH64, Apple M1, RISC-V, LoongArch, and MIPS.

## The Burden: Maintaining a C++ Coroutine Library

Coroutines are a fantastic idea for a C++ media server, but unlike Go (where goroutines are built into the language and runtime), **C++ has no standard coroutine library for this model**. (Note: C++20 co_await/co_yield is a different mechanism — not the same as user-space threads with full stacks.)

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

**Toolchain Gap**
Go provides built-in tools: goroutine stack traces, scheduling profilers, debuggers that understand goroutines. With ST coroutines:
- No built-in scheduler visualization
- No standard debugging tools that understand coroutine stacks
- No performance analysis tools for coroutine scheduling
- Everything must be built from scratch

**Debugging and Profiling Limitations**
- `perf -g` (stack traces) does not work with ST because ST modifies the stack pointer (SP), breaking frame pointer-based stack walking
- Valgrind requires ST-specific hooks, supported since SRS 3+
- ASAN (Address Sanitizer) is supported since SRS 5+, enabled by default in SRS 5, disabled by default in SRS 6 because it sometimes causes crashes for unknown reasons
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
