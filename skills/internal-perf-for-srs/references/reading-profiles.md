# Reading the Profiles

Each hold in `cmd/perf/out/<run>/repeat-<n>/hold-<50|50-noise|90>/` has three files from `scripts/perf-profile.sh`:

- **`flamegraph.svg`:** the stacks of the 60s `perf record -g` of the SRS process.
- **`top-symbols.txt`:** `perf report --no-children --sort symbol`, every symbol with its self share and its callers.
- **`syscalls.txt`:** 10s of syscall counts, taken right after the record while the hold still runs, from `perf trace -s`, or `strace -c -f` when `perf trace` does not work. The tracer adds SRS CPU in the last 20s of the hold, as `perf record` does, so compare holds only with holds.

The share of a sample is a share of SRS's own CPU time. With the SRS CPU% of the same hold, from its `result.json`, it becomes an absolute cost: a symbol with 10% self share at 80% SRS CPU costs 8% of a core.

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
- **Cheaper:** the SRS CPU% at the same hold drops, so the same clients cost less.
- **Flat:** neither. The change may still help once the rest of a bottleneck spread over several functions is fixed, so it is kept when its tests pass and no scenario loses 10% or more; the review decides.
- Record both symbol tables with their shares and the SRS CPU% before and after; no threshold decides whether a hotspot moved.

## Ranking

1. Take the 90% hold of the latest run; use the 50% hold to confirm the cost grows with the load.
2. Charge each libc and kernel symbol to its SRS caller from the caller tree, and group symbols into the SRS code path that owns them, such as packet copying in `SrsLiveConsumer::enqueue` or message sending in `SrsRtmpConn::do_playing`.
3. Rank the code paths by absolute cost: their total self share times the SRS CPU%.
4. Name the SRS files and functions of each code path, and the syscalls it makes.
5. The top bottleneck is the #1 code path that SRS can reasonably change. The kernel, syscalls already batched, and crypto in RTC are not.
