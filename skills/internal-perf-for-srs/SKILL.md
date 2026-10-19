---
name: internal-perf-for-srs
description: Use for SRS performance work - when asked to load test, benchmark or profile SRS, find or fix a performance bottleneck, compare performance between versions or changes, or run or resume a performance round. Use from the srs-develop Task Router for performance tasks.
---

# SRS Performance Rounds

A round measures SRS under many simulated clients, finds the bottlenecks, improves them one by one, and ends with a report:

- **Round file:** each round has one file `cmd/perf/out/reports/<YYYY-MM-DD>-v<version>-round.md` in this skill's own format, made from [references/round-template.md](references/round-template.md). It holds the round's data, the ranking, one chain per bottleneck, the steps and the work log. Like the reports, it is the round's working output, git-ignored and never committed. It is not an srs-autopilot task file: it has no worktree, tracker row or review table, and only this skill's workflows run it.
- **Reports:** the baseline report, and at the end an after-report, made from [references/report-template.md](references/report-template.md) in the git-ignored `cmd/perf/out/reports/`. They are not committed: their numbers hold only for the setup that made them. The round file links them.
- **Profiles:** [references/reading-profiles.md](references/reading-profiles.md) explains the flame graph, the symbol table, the syscall counts and how to rank.

## Skill Dependencies

- `skills/srs-develop/SKILL.md` — every code change, its tests and its commit.

## Rules

- **By hand:** a round starts only when the user asks. An SRS version bump in `srs-develop` asks the user whether to run one for the new version; start it only on a yes. Never from a commit, a PR, CI or a schedule. Nothing here is a pass or fail gate.
- **One at a time:** one scenario, one step, one profile. A run takes minutes, about 10 for RTMP, and a round as long as its tries need. Keep the ramp steps and holds at their usual length, so a run compares with the baseline.
- **Alone on the machine:** load skews other jobs and they skew the numbers. `perf-run.sh` waits for and holds the `srs-develop` Full-tier lock for the whole run, so it never runs beside a Full tier.
- **Linux in Docker:** the numbers are fair for profiles and for comparing two runs on the same setup, not as the capacity of a real server. Every report says so.
- **Flat CBR source:** SRS CPU follows the media bitrate, so a VBR source makes two runs, or two servers at different points of the loop, not comparable. Every measurement, on every protocol, uses the flat CBR source in `srs-perf:local`:
  - `/srs/perf/cbr.flv`: 60s, H.264 at 200 kbps CBR with a one-frame buffer, 25 fps, an IDR every 2s, no B frames, and AAC 32 kbps.
  - `/srs/perf/cbr-flatness.json`: its flatness, which every report records. The image fails to build when the source is not flat.
  - Not `doc/source.flv`: its video runs from a few kbps to over 1 Mbps in one second.
  - `bin/srs-perf run` and `smoke` read it by default; `-media <flv>` names another source, which needs its `<name>-flatness.json` beside it. They refuse a source whose flatness record is missing or not flat, and copy the source and its record into each result's setup. `compare` treats two sources as two setups.
  - `make perf-env-check` publishes it too; the containers mount no media.
- **Per-second samples:** `bin/srs-perf run` and `smoke` sample once a second and keep every sample in `result.json`:
  - SRS CPU is the SRS container's cgroup `cpu.stat` `usage_usec`, which counts every thread to the microsecond. The load container runs with `--cgroupns host` and finds the cgroup from `/proc/<srs pid>/cgroup`; each sample keeps the raw `srs_cpu_usage_usec` too.
  - RSS is from `/proc/<srs pid>/status`, and each worker's CPU from its `/proc/<pid>/stat`.
  - Each step has a `window` with the mean and sd of each metric (SRS CPU, RSS, the workers' average CPU, frames, bytes, lag, failures) over its counted samples; a hold's result carries its step's `window`.
  - Anything else in the SRS container, such as `perf record`, counts in its cgroup too, so no profiler runs in a measuring window.
- **Lag in publisher time:** a player fails when its newest message is more than 2s behind the publisher's newest:
  - SRS's default `time_jitter full` starts each player's timestamps near zero, so a player that joins after the first IDR gets timestamps about one GOP smaller than the publisher's, though it is live.
  - The load tool places a player's first video frame in the source by its payload, so every video frame of the source must have a unique payload; a run refuses a source without one.
- **No profiler in the window:** a measuring hold lets its clients settle 30s, then counts every sample of its window, 60s by default and 180s for a verdict, with nothing else running in the SRS container:
  - `perf record` and `perf trace` run afterwards, in their own profile hold at the same client count, one per hold count.
  - A profile hold's CPU includes perf, so it is never a measurement: its result has no CPU per client, stays out of `run.json`, and `compare` refuses it.
  - The ramp only probes capacity by health. Its CPU per client includes connecting each step's new clients, so `compare` prints it as a note and never flags it.
- **A/B preferred, sequential accepted:** a comparison of two SRS builds reports each side's SRS CPU mean and sd over its window, and the difference with its 95% CI:
  - **A/B, preferred:** [scripts/perf-ab.sh](scripts/perf-ab.sh) runs both builds at once on their own cores, with the same source, load tool and client count, and pairs their samples per second. Both servers swing with the machine together, so the paired difference removes that shared noise.
  - **Sequential, accepted:** a run of the candidate compared with one recorded earlier under the same conditions: the source, cores, client counts and window. The CI is unpaired and noisier; say so wherever the result is written.
  - A verdict on a change uses a 180s window; both sides need the same window.
- **Temporary images:** only `srs-perf:local`, which every `make perf` rebuilds, is kept:
  - A baseline image `srs-perf-ab:<task>-<rev>` is removed by `perf-ab.sh` when it finishes, or with `docker rmi` once its comparison is done when it was kept with `-keep`.
  - A build-check image `srs-perf-build:<flag>` is removed when its check ends.
  - Remove every temporary image and container before a step ends, and log the images left. Ask the user before `docker builder prune`.
- **Shipped defaults:** SRS runs with `-e`, configured only by environment variables, one env file per scenario in `cmd/perf/env/<scenario>.env`, changed only where the scenario needs it. No config file. A tuning setting is a candidate like any other.

## Tools

- **`make perf`:** builds the profiling image `srs-perf:local` and `bin/srs-perf`, nothing else; minutes.
- **`make perf-unit`:** the load tool's Go unit tests; run before each commit of `cmd/perf`.
- **`make perf-smoke SCENARIO=<s>`:** about 10s at the first ramp step, in the containers.
- **`make perf-source-test`:** builds the image, then checks its CBR source is flat and that the flatness check rejects a VBR file and a CBR encode with a large buffer.
- **`make perf-env-check`:** checks the containers, `perf` samples with SRS symbols, and the core calibration.
- **`make perf-up` and `make perf-down`:** start and stop the SRS and load containers.
- **`bin/srs-perf compare <old.json> <new.json>`:** compares two results on the same setup and flags a 10% change in capacity, or in a hold's CPU per client; the ramp's CPU per client is a note. Given a combined `run.json` or a report's JSON, it pairs the results by mode and client count. For two holds it also gives:
  - SRS CPU, each side's mean and sd over its window.
  - The difference of SRS CPU in points and percent, with its 95% CI and whether the CI contains 0.
  - The difference is paired per second when both holds are the two sides of one A/B run: `bin/srs-perf run` with `-ab-run <id>` gives both the same id, and each sample keeps its wall-clock time, so a second pairs with the other side's nearest sample within half a second.
  - Otherwise unpaired, a sequential comparison with Welch's degrees of freedom, marked noisier than a paired A/B.
  - The 10% flag of CPU per client uses the mean difference.

The scripts, called by the workflows below:

- [scripts/perf-run.sh](scripts/perf-run.sh): the load run. It builds with `make perf`, takes and refreshes the lock, checks the scenario ports are free, then per repeat:
  - A `ramp` to capacity. Each step lasts 5s: the first 2s let the new clients settle, and only the last 3 one-second samples count. The ramp stops at 90% SRS CPU, a failed health check, or the cap of 40,000 clients for the TCP protocols.
  - Three measuring holds, `-mode hold`: at 50% of that capacity, again at 50% to measure the noise, and at 90%. Each lets its clients settle 30s, then counts a `-window <seconds>`, 60 by default, with no profiler.
  - Two profile holds, `-mode profile`, at the 50% and 90% counts, after the measuring holds: each settles 30s, then [scripts/perf-profile.sh](scripts/perf-profile.sh) takes a 60s `perf record`, 10s syscall counts, a flame graph and a symbol table.
  - The noise: `bin/srs-perf compare` of the two 50% holds, which shows how much CPU per client moves between two identical holds.
  - It prints the path of `run.json`.
- [scripts/perf-ab.sh](scripts/perf-ab.sh): the A/B run, the preferred comparison of two SRS builds, paired per second:
  - `-baseline <rev>` builds the baseline SRS from a commit's `trunk/`; `-revert <commit>` from HEAD's `trunk/` with that commit's trunk change taken out, so a commit that also changed the tools, which later commits changed again, still reverts. The candidate is `srs-perf:local`, built from the worktree, so a change can be measured before it is committed.
  - The baseline's tree is streamed from `git archive` into `cmd/perf/Dockerfile.ab`, which rebuilds only SRS on `srs-perf:local`: the worktree is never touched, and both sides share the source and the load tool. Its image is `srs-perf-ab:<task>-<rev>`, named by `-task`, such as the round step.
  - Holding the lock, it starts the baseline SRS on core 1 and the candidate on core 2, each with its own load container on cores 3–9, and runs one hold of `-clients <n>` on both at once with one `-ab-run` id: a 30s settle, then a 180s `-window` by default.
  - It writes `ab.json` (both results, the baseline's rev, tree and image, and the candidate's `git describe`) and `verdict.txt` (the paired `compare`) to `cmd/perf/out/<run>-ab/`, and prints the verdict.
  - It removes its containers, and the baseline image unless `-keep`; remove a kept image with `docker rmi` once its comparison is done. `-dry-run` prints the baseline's tree and image and builds nothing.
- [scripts/perf-ab-test.sh](scripts/perf-ab-test.sh): its test, with a short A/B of HEAD against itself whose CI must contain 0.
- [scripts/perf-profile.sh](scripts/perf-profile.sh): the profile of one hold, run inside the SRS container.
- [scripts/perf-source.sh](scripts/perf-source.sh): encodes the CBR source with its fixed parameters and writes its flatness JSON; the image build runs it.
- [scripts/perf-source-check.sh](scripts/perf-source-check.sh) `<flv> <target kbps>`: prints a source's flatness JSON, and fails when a whole second after the first leaves ±3% of the target or the per-frame sd is over 3% of the mean. The first second and the first IDR are reported, not judged.
- [scripts/perf-source-test.sh](scripts/perf-source-test.sh): the test behind `make perf-source-test`.
- [scripts/perf-env-check.sh](scripts/perf-env-check.sh): the environment check.
- [scripts/perf-cap-check.sh](scripts/perf-cap-check.sh): one 20s hold at the client cap, without a publisher, which checks that all of its players connect (file limits, ephemeral ports, `max_connections`). Run it after changing the cap or the containers.
- [scripts/perf-skill-test.sh](scripts/perf-skill-test.sh): checks this skill's text, a dry run of [Start a Round](#start-a-round), the `srs-develop` route, and the reports in `cmd/perf/out/reports/` where they exist.

Outputs go to the git-ignored `cmd/perf/out/<run>/`: `run.json` with the results and the noise, and per repeat each result's `result.json`, `noise.txt` and `noise.json`, and each profile hold's `profile-<50|90>/` with `flamegraph.svg`, `top-symbols.txt`, `syscalls.txt`, `perf.data` and its own `result.json`, which is not in `run.json`.

## Workflows

### Start a Round

1. Name it from the start date and the SRS version being measured, such as `cmd/perf/out/reports/2026-10-19-v8-round.md`, or `2026-10-19-v8-rtmp-round.md` for one protocol.
2. Copy [references/round-template.md](references/round-template.md) there, and fill every `<...>` placeholder: the version, the date, the checkout and branch the round works in (the current one unless the user names another), the scenarios, and the limit on improvements and tries if the user gave one, such as one kept change in at most 3 tries.
3. When a baseline report already exists for the round's commit and setup, such as one made while building the tools, start from it instead of measuring again:
   - Point Background's Baseline at the report and its JSON, and tick the Baseline steps with the run and report they came from.
   - Fill the Ranking from the report's Bottlenecks, and Chain 1 for the top one, with its baseline row: capacity, CPU per client and SRS CPU at both holds, and the top symbols with their shares.
   - Name the hold client counts of the baseline in Chain 1, since every fix is measured at the same counts.
4. The round file is in the git-ignored `cmd/perf/out/reports/`, so it is never committed.

### Build the Tools

1. Run `make perf`.
2. Run `make perf-env-check` when the image, Docker or the machine changed since the last round. If `perf` gets no samples or no SRS symbols, stop and ask the user; do not build `perf`, switch to a VM, or use another profiler on your own.

### Run the Load and Profile

1. Run `bash skills/internal-perf-for-srs/scripts/perf-run.sh -scenario <s>`:
   - One run, `-repeat 1`, is the default for a baseline and for a fix; about 10 minutes for RTMP.
   - The holds follow the run's own ramp. For a fix, `-hold-clients <50%>,<90%>` holds at the baseline's counts instead, such as `-hold-clients 6400,11520`, so the holds compare at the same load.
   - `-window <seconds>` sets the measuring holds' window: 60 by default, 180 for a fix's verdict. A comparison needs the same window on both sides.
   - Other flags pass through to `bin/srs-perf run` for the ramp, such as `-clients` and `-cap`.
2. Record in the round file the run directory, the capacity, each hold's SRS CPU mean and sd and its CPU per client, the noise between the two 50% holds, and the core calibration gap. A gap over 10% marks the run noisy. The run does not measure the gap itself: it copies the one from the last `make perf-env-check`, so note that check's date.
3. Rerun only when a comparison's 95% CI spans the 10% line, such as a −9% difference with a CI of −12% to −6%: run an A/B, or run again with a 180s `-window`, and log why.

### Rank Bottlenecks

1. Read each profile hold's `top-symbols.txt`, `syscalls.txt` and flame graph per [references/reading-profiles.md](references/reading-profiles.md).
2. Rank by cost at the 90% profile hold, grouped into the SRS code path that owns it.
3. Write the ranking table in the round file: the rank, the code path, its self share at both holds, the syscall counts, and the SRS files and functions.
4. Start a chain for the new top bottleneck if it has none.

### Fix the Top Bottleneck

1. Take the current #1 of the ranking. Read its numbers and profile from the round file.
2. Change the SRS code per `srs-develop`.
3. Run [Check](#check).
4. Measure:
   - **The verdict:** an A/B at the 90% hold count with the default 180s window, such as `perf-ab.sh -baseline HEAD -clients 11520` before the commit, or `-revert <commit>` after it. Its `verdict.txt` gives each side's SRS CPU mean and sd and the paired difference with its 95% CI.
   - **Capacity and the other scenarios:** [Run the Load and Profile](#run-the-load-and-profile) for each scenario the change targets and each scenario its code path also serves, with `-hold-clients` at the baseline's hold counts, then `bin/srs-perf compare <baseline.json> <run.json>` for each. This is a sequential comparison: its CI is unpaired and noisier.
5. Decide:
   - **Keep** when the tests pass and no measured scenario loses 10% or more in capacity or CPU per client, even when the numbers are flat. A gain may show only after the rest of a bottleneck spread over several functions is fixed.
   - **Drop** when a test fails or a scenario loses 10% or more: restore the changed files with `git checkout -- <files>`, commit nothing, and log the numbers and both symbol tables.
   - A loss counts by the mean difference; when its 95% CI spans the 10% line, rerun per [Run the Load and Profile](#run-the-load-and-profile) before dropping.
6. Add the step to the bottleneck's chain: the change, the commit, the method and window, SRS CPU before and after as mean ± sd, the difference with its 95% CI, the capacity, and the top symbols with their shares.
7. Remove the A/B's baseline image if it was kept (`-keep`), and log the images left.
8. After a kept change, re-profile and [rank](#rank-bottlenecks) again; the next try fixes the new #1.
9. A chain with 5 kept steps and no gain overall is marked "no gain yet, needs review", is not reverted, and the round moves on to the next bottleneck.
10. The bottleneck work ends when the top bottleneck is something SRS cannot reasonably fix, such as the kernel, syscalls already batched, or crypto in RTC, or when the round's limit is reached.

### Check

- After the change, all the tests: the `srs-develop` Full tier. Every layer must pass.
- `make perf-unit` when `cmd/perf` changed.

### Commit

Per the `srs-develop` commit format, one commit per kept change, on the round's branch; never push. Add the commit to its chain step. The message gives:

- The scenario, the client counts of the holds, and the run directories.
- The comparison's method and window, such as an A/B paired per second over 180s, or sequential and unpaired.
- SRS CPU before and after as mean ± sd, and the difference in points and percent with its 95% CI.
- The before and after capacity and CPU per client, and the `compare` flags.
- The top symbols with their self shares, before and after, from the profile holds.

### Write the Report

1. Make `cmd/perf/out/reports/<YYYY-MM-DD>-v<version>-docker.md` from [references/report-template.md](references/report-template.md), and copy the run's combined JSON next to it with the same name and `.json`. Both are git-ignored and stay on this machine, for the round and `bin/srs-perf compare`.
2. The baseline report fills every section except Before and after:
   - The Setup's Source is the result's `setup.source` and its flatness record `setup.source_flatness`: the per-second mean, min and max, the per-frame sd, and the ffmpeg version.
   - Each hold gives SRS CPU as mean ± sd over its window, from the result's `window`.
3. The after-report fills Before and after too, with `bin/srs-perf compare` against the baseline, the method and 95% CI of each comparison, and the list of the round's commits:
   - Its final run uses `-hold-clients` at the baseline's hold counts, so every hold has a partner in the compare.
   - When no version bump came between them, it has the baseline's date and version; name it `<YYYY-MM-DD>-v<version>-after-docker.md`, with its JSON beside it, and link the baseline report from Before and after.
4. Link it from the round file's Background.
5. Flame graphs and `perf.data` stay in `cmd/perf/out/`; copy only the top-symbol tables into the report.

### Review

The user reviews the round's commits, listed in the chains, asynchronously while the round runs. Looking at the chains, the review keeps the commits that helped and drops the rest; the round needs no approval to go on.
