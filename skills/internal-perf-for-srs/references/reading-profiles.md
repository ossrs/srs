# Reading the Profiles

Each profile hold in `cmd/perf/out/<run>/repeat-<n>/profile-<50|90>/` has three files from `scripts/perf-profile.sh`; a proxy run has Go profiles instead, see [Go profiles](#go-profiles). A profile hold runs after the measuring holds at the same client count, and perf starts once its clients have settled 30s:

- **`flamegraph.svg`:** the stacks of the 60s `perf record -g` of the SRS process.
- **`top-symbols.txt`:** `perf report --no-children --sort symbol`, every symbol with its self share and its callers.
- **`syscalls.txt`:** 10s of syscall counts, taken right after the record, inside the same profile hold, from `perf trace -s`, or `strace -c -f` when `perf trace` does not work. The tracer and `perf record` add SRS CPU, so a profile hold's CPU is never a measurement; compare syscall counts only between profile holds.

The profile hold's own `result.json` has no CPU per client and no result window, and stays out of `run.json`: perf's CPU counts in SRS's cgroup, so take SRS CPU only from a measuring hold.

The share of a sample is a share of SRS's own CPU time. With the window mean of SRS CPU in the measuring hold at the same client count, from its `result.json` in `run.json`, it becomes an absolute cost: a symbol with 10% self share at 80% SRS CPU costs 8% of a core.

## Flame graph

- Each box is a function; its width is the time spent in it and everything it calls.
- The top edge of a stack is where the CPU was; a wide top edge is a hotspot.
- Read from the bottom: the coroutine entry (`_st_thread_main`), then the SRS cycle that owns the work, such as `SrsRecvThread::cycle` for a publisher or `SrsRtmpConn::do_playing` for a player.
- Open it in a browser to search a symbol and see its total share.
- A wide bare `srs` box with nothing above it is samples whose frame-pointer call chain came out empty. For RTMP, these are the players' send path (`SrsRtmpConn::do_playing`, `writev` and the kernel's TCP send). Their self shares in `top-symbols.txt` are right; only their callers are lost, so group them by their leaf symbols.

## Symbol table

- Each top-level line is a symbol and its **self share**: the time spent in the symbol itself, not in what it calls.
- `[.]` is user space, such as SRS, ST and libc; `[k]` is the kernel.
- The indented tree under a symbol lists its callers, with the share each caller adds. Use it to charge libc and kernel time, such as `malloc`, `memcpy` or `writev`, to the SRS code that called it.
- List only the top-level lines with `grep -E '^ +[0-9]+\.[0-9]+%' top-symbols.txt | head -30`.
- `_st_epoll_dispatch` under `_st_idle_thread_start` is ST waiting for and dispatching events; a large share at a low load is idle time, not work.

## Syscall counts

- Read the calls and the share of syscall time per syscall. Divide the calls by 10s and by the hold's clients to get calls per client per second.
- `writev` per player shows how well SRS batches messages into one write; more calls per client per second means smaller batches.
- `epoll_pwait` calls show how often SRS wakes; fixed timers show as a floor that does not grow with the clients.
- Calls with no clear owner, such as many `getpid` or `write` calls, often come from logging; find the caller in the symbol table.

## Hotspot moved

A change can be real without raising the capacity, so compare the profiles at the **same client count**, not only the numbers:

- **Moved:** the target symbol's self share drops, and the shares of other symbols rise to fill the gap.
- **Cheaper:** the SRS CPU window mean at the same client count drops, measured in measuring holds or an A/B, never in the profile holds; its 95% CI says whether the drop is more than noise.
- **Flat:** neither. The change may still help once the rest of a bottleneck spread over several functions is fixed, so it is kept when its tests pass and no scenario loses 10% or more; the review decides.
- Record both symbol tables with their shares and the SRS CPU% before and after; no threshold decides whether a hotspot moved.

## Ranking

1. Take the 90% profile hold of the latest run; use the 50% profile hold to confirm the cost grows with the load.
2. Charge each libc and kernel symbol to its SRS caller from the caller tree, and group symbols into the SRS code path that owns them, such as packet copying in `SrsLiveConsumer::enqueue` or message sending in `SrsRtmpConn::do_playing`.
3. Rank the code paths by absolute cost: their total self share times the window mean of SRS CPU in the 90% measuring hold.
4. Name the SRS files and functions of each code path, and the syscalls it makes.
5. The top bottleneck is the #1 code path that SRS can reasonably change. The kernel, syscalls already batched, and crypto in RTC are not.

## Go profiles

A proxy profile hold has the files of `scripts/perf-profile-go.sh`, listed in the skill's [Proxy Profiles](../SKILL.md#proxy-profiles). Read the `.pb.gz` files with `pprof` in the image or `go tool pprof` on the host, and `trace.out` with `go tool trace` on the host. As for SRS, the profile hold's CPU is never a measurement; take the proxy's CPU from the measuring hold at the same client count.

- **CPU, flat and cum:** `top-symbols.txt` is `pprof -top cpu.pb.gz`:
  - `flat` is the time in the function itself, like `perf`'s self share; `cum` adds what it calls.
  - Its header's `Total samples` over `Duration` is the proxy's CPU while profiled, so a share is a share of that.
  - `pprof -top -cum cpu.pb.gz` finds the proxy's own code path, such as `srsx/internal/proxy.(*rtmpConnection).serve`, and `pprof -peek '<symbol>' cpu.pb.gz` shows who calls a runtime or syscall symbol, such as `internal/runtime/syscall.Syscall6` for each read and write.
  - Go's profiler records whole stacks, so `flamegraph.svg` has no bare boxes of lost callers.
- **Allocations per packet:** `allocs.pb.gz` covers the same 60s. `pprof -sample_index=alloc_objects -top allocs.pb.gz` gives the objects per function, `alloc_space` the bytes. Divide the objects by the messages or packets the proxy forwarded in those 60s, the clients times the source's frames per second (more RTP packets for WebRTC) times 60. One or more per packet is a forwarding path that allocates per packet, such as a new reader or header buffer per message.
- **Heap:** `pprof -sample_index=inuse_space -top heap.pb.gz` is the memory held at the end of the hold; divide by the clients for the memory per client, beside the hold's RSS.
- **Goroutines:** `goroutine.txt` starts with `goroutine profile: total <n>`, then each group of goroutines with its count and stack. Divide the total by the clients: each protocol has a fixed number per client, such as a reader and a writer per connection. A count that grows between the two profile holds by more than their clients is a leak.
- **GC share:** `gc.json` holds the GC count `num_gc` and pause total `pause_total_ns` before and after the profiles, about 72s apart; their difference gives the GCs per second and the stop-the-world pause share. The GC's CPU is larger than its pauses: its share is the `cum` of `runtime.gcBgMarkWorker` plus `runtime.gcAssistAlloc` in the CPU profile. Fewer allocations lower it.
- **Mutex and block:** `pprof -top mutex.pb.gz` and `block.pb.gz` give where goroutines waited on locks and on channels or I/O. With `GOMAXPROCS=1` lock contention is rare; long blocks in idle goroutines waiting for a client are normal.
- **Trace:** `go tool trace trace.out` opens the 10s runtime trace in a browser:
  - The **Scheduler latency** profile shows how long runnable goroutines waited for the proxy's one core; a long wait is a core already full.
  - The **Syscall** profile shows the time in syscalls by stack, which replaces `perf trace`'s counts; `go tool trace -pprof=syscall trace.out > syscall.pb` makes it a pprof profile.
  - The goroutine analysis counts each goroutine's time running, waiting and in syscalls.
- **Ranking:** as for SRS: charge `runtime` and syscall time to the proxy code that called it, group into the proxy's code paths in `srsx/internal/proxy` and the protocol packages below it, and rank by `cum` share times the proxy's CPU at the 90% measuring hold. Syscalls per packet and allocations per packet are changes the proxy can make; the kernel's own send and receive is not.
- **Report:** a proxy report, `<YYYY-MM-DD>-v<proxy version>-proxy-docker.md`, copies the top 30 lines of `top-symbols.txt` and the allocation, goroutine and GC numbers of each profile hold.
