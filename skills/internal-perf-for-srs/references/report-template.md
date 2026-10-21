# SRS v<version> Performance in Docker, <YYYY-MM-DD>

<One paragraph: the round, the scenarios, the capacity found, and the top bottleneck. The after-report adds what improved.>

- **Round:** [<YYYY-MM-DD>-v<version>-round.md](<YYYY-MM-DD>-v<version>-round.md), beside this report.
- **Data:** `<YYYY-MM-DD>-v<version>-docker.json` beside this report, the combined `run.json` of the run `<run>`.
- **SRS:** v<version> at `<commit>`, built in `srs-perf:local` with the image's release flags and frame pointers.

**The proxy target:** a report on the Go proxy, measured with `-target proxy`, is named `<YYYY-MM-DD>-v<proxy version>-proxy-docker.md` (its after-report `-proxy-after-docker.md`, a baseline on several origins `-proxy-origins-docker.md` with the one-origin capacity, the bound and each origin's CPU beside each scenario's capacity on N origins), titled `SRS Proxy v<proxy version> Performance in Docker`, and differs only where this template says so:

- Its Setup fills the Target, Proxy version and Origins rows, and the containers are the proxy's.
- Its Results and Before and after give the proxy's CPU in the SRS CPU columns, headed Proxy CPU, with the origin CPU beside it from the result's `origin_cpu_percent`, and mark a capacity ended `origin-bound`.
- Its Bottlenecks come from the Go profiles: `top-symbols.txt` with `flat` and `cum`, allocations per packet, goroutines and the GC share, per the Go profiles of `go-server.md`. Its Syscalls come from the trace's Syscall profile, not `perf trace`.

## Setup

| Item | Value |
|---|---|
| Machine | <model, performance and efficiency cores, memory> |
| Docker | <version>, VM with <CPUs> CPUs and <memory> |
| Kernel | <kernel> |
| Target | <`srs`, or `proxy`> |
| Proxy version | <the result's `setup.proxy_version`, or — for SRS> |
| Origins | <the result's `setup.origins`: 1 to 4, three by default; — for SRS> |
| SRS container | vCPU 1, privileged for `perf`; for the proxy: the proxy on vCPU 1 with `GOMAXPROCS=1`, origins on vCPUs 2 to N+1 |
| Load container | vCPUs 2–9, shares the SRS pid namespace and network; for the proxy: the rest of the cores, sharing the proxy's |
| Core calibration gap | <gap>; <noisy when over 10%, or not noisy> |
| Source | `<source>`; <flatness>: per-second mean, min and max in kbps, per-frame sd, ffmpeg version, from the result's `setup.source_flatness` |

## Caveats

- Linux in Docker on a desktop machine: the numbers are fair for profiles and for comparing two runs on the same setup, not as the capacity of a real server.
- SRS serves media on one thread, so 100% CPU means one core is saturated.
- The profile samples `cpu-clock`, since the VM has no hardware PMU.
- The ramp's CPU per client includes connecting each step's new clients; it is a note, not a measurement.
- <Anything else that skews this run, such as a noisy calibration.>

## Method

- **Ramp:** doubles the clients each step; each step lasts 5s, lets the new clients settle for 2s and counts the last 3 one-second samples. It stops at 90% SRS CPU, a failed health check, or the cap of 40,000 clients for the TCP protocols. Capacity is the last healthy step.
- **Source:** the flat CBR source in the Setup, for every scenario; SRS CPU follows the bitrate, so a VBR source is never compared.
- **Samples:** once a second. SRS CPU is the SRS container's cgroup `usage_usec`, RSS is from `/proc`; every sample is kept in the JSON, and each hold gives each metric's mean ± sd over its window.
- **Health:** a player fails when its lag passes 2s, or when it gets no frame in a ramp step or for 5s in a hold. A failed step is load-bound, not an SRS limit, when the load workers average above 95% CPU in one second.
- **Hold:** measuring holds at 50% of the ramp's capacity, again at 50%, and at 90%; each settles 30s, then counts a <window>s window with no profiler. Then a profile hold at the 50% and the 90% count, each with a 60s `perf record -F 999 -g`, a flame graph, a symbol table and 10s syscall counts; its CPU is not a measurement.
- **Noise:** the change in CPU per client between the two 50% holds: <noise>, with its unpaired 95% CI. A comparison whose 95% CI spans the 10% line is rerun.
- **Runs:** one per scenario, by `skills/internal-perf-for-srs/scripts/perf-run.sh`, unless a rerun is logged here.

## Results

| Scenario | Capacity | CPU per client | Noise | SRS CPU at 50% and 90%, mean ± sd | RSS at 90% |
|---|---|---|---|---|---|

## Bottlenecks

Ranked by cost at the 90% profile hold, per the target's reference, such as the Ranking of `skills/internal-perf-for-srs/references/cpp-server.md`.

| Rank | Code path | Self share at 50% and 90% | Syscalls | SRS files and functions |
|---|---|---|---|---|

### <scenario> top symbols at 90%

```
<the top 30 lines of top-symbols.txt, self share and symbol>
```

## Syscalls

| Scenario | Hold | Syscall | Calls in 10s | Calls per client per second | Share of time |
|---|---|---|---|---|---|

## Before and after

Only in the after-report. `bin/srs-perf compare <baseline.json> <after.json>` per scenario, and the A/B of each change where one ran:

- **Method:** <method>: an A/B by `skills/internal-perf-for-srs/scripts/perf-ab.sh`, paired per second, or a sequential comparison of two runs, unpaired and noisier than a paired A/B; and the window.

| Scenario | Capacity before → after | CPU per client before → after | Flags |
|---|---|---|---|

| Scenario | Hold clients | Method and window | SRS CPU before, mean ± sd | SRS CPU after, mean ± sd | Difference, 95% CI |
|---|---|---|---|---|---|

The round's commits:

| Commit | Bottleneck | Subject |
|---|---|---|

## Re-run and compare

1. `make perf`
2. `bash skills/internal-perf-for-srs/scripts/perf-run.sh -scenario <s>`
3. `bin/srs-perf compare cmd/perf/out/reports/<YYYY-MM-DD>-v<version>-docker.json cmd/perf/out/<run>/run.json`, a sequential comparison.
4. For a change, prefer an A/B: `bash skills/internal-perf-for-srs/scripts/perf-ab.sh -baseline <rev> -clients <n>`, or `-revert` with the change's commit.
5. For a proxy report, add `-target proxy` to `perf-run.sh` and `perf-ab.sh`, with `-scenario <s>`.

A sequential comparison is made only when both runs record the same machine, cores, memory, Docker version, kernel, source and scenario parameters, window included; otherwise the new run is a new baseline.
