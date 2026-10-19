# SRS v<version> Performance in Docker, <YYYY-MM-DD>

<One paragraph: the round, the scenarios, the capacity found, and the top bottleneck. The after-report adds what improved.>

- **Round:** [<YYYY-MM-DD>-v<version>-round.md](<YYYY-MM-DD>-v<version>-round.md), beside this report.
- **Data:** `<YYYY-MM-DD>-v<version>-docker.json` beside this report, the combined `run.json` of the run `<run>`.
- **SRS:** v<version> at `<commit>`, built in `srs-perf:local` with the image's release flags and frame pointers.

## Setup

| Item | Value |
|---|---|
| Machine | <model, performance and efficiency cores, memory> |
| Docker | <version>, VM with <CPUs> CPUs and <memory> |
| Kernel | <kernel> |
| SRS container | vCPU 1, privileged for `perf` |
| Load container | vCPUs 2–9, shares the SRS pid namespace and network |
| Core calibration gap | <gap>; <noisy when over 10%, or not noisy> |

## Caveats

- Linux in Docker on a desktop machine: the numbers are fair for profiles and for comparing two runs on the same setup, not as the capacity of a real server.
- SRS serves media on one thread, so 100% CPU means one core is saturated.
- The profile samples `cpu-clock`, since the VM has no hardware PMU.
- <Anything else that skews this run, such as a noisy calibration.>

## Method

- **Ramp:** doubles the clients each step; each step lasts 5s, lets the new clients settle for 2s and counts the last 3 one-second samples. It stops at 90% SRS CPU, a failed health check, or the cap of 40,000 clients for the TCP protocols. Capacity is the last healthy step.
- **Health:** a player fails when its lag passes 2s, or when it gets no frame in a ramp step or for 5s in a hold. A failed step is load-bound, not an SRS limit, when the load workers average above 95% CPU in one second.
- **Hold:** 75s at 50% of the ramp's capacity, again at 50%, and at 90%, each with a 60s `perf record -F 999 -g`, a flame graph, a symbol table and 10s syscall counts.
- **Noise:** the change in CPU per client between the two 50% holds: <noise>. A comparison within the noise of the 10% line is rerun.
- **Runs:** one per scenario, by `skills/internal-perf-for-srs/scripts/perf-run.sh`, unless a rerun is logged here.

## Results

| Scenario | Capacity | CPU per client | Noise | SRS CPU at 50% and 90% | RSS at 90% |
|---|---|---|---|---|---|

## Bottlenecks

Ranked by cost at the 90% hold, per `skills/internal-perf-for-srs/references/reading-profiles.md`.

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

Only in the after-report. `bin/srs-perf compare <baseline.json> <after.json>` per scenario:

| Scenario | Capacity before → after | CPU per client before → after | Flags |
|---|---|---|---|

The round's commits:

| Commit | Bottleneck | Subject |
|---|---|---|

## Re-run and compare

1. `make perf`
2. `bash skills/internal-perf-for-srs/scripts/perf-run.sh -scenario <s>`
3. `bin/srs-perf compare cmd/perf/out/reports/<YYYY-MM-DD>-v<version>-docker.json cmd/perf/out/<run>/run.json`

A comparison is made only when both runs record the same machine, cores, memory, Docker version, kernel and scenario parameters; otherwise the new run is a new baseline.
