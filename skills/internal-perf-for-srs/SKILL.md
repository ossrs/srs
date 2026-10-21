---
name: internal-perf-for-srs
description: Use for SRS performance work - when asked to load test, benchmark or profile the SRS C++ media server or the Go server (the proxy), find or fix a performance bottleneck, compare performance between versions or changes, or run or resume a performance round. Use from the srs-develop Task Router for performance tasks.
---

# SRS Performance Rounds

A round measures one server, the target, under many simulated clients, finds the bottlenecks, improves them one by one, and ends with a report. This file holds what every target shares: the rules, the tools, the round file and the workflows. What differs per target, such as its scenarios, profiles, tests and verified capacities, is in the target's reference, chosen by the [Target Router](#target-router).

- **Round file:** each round has one file `cmd/perf/out/reports/<YYYY-MM-DD>-v<version>-round.md` in this skill's own format, made from [references/round-template.md](references/round-template.md). It holds the round's data, the ranking, one chain per bottleneck, the steps and the work log. Like the reports, it is the round's working output, git-ignored and never committed. It is not an srs-autopilot task file: it has no worktree, tracker row or review table, and only this skill's workflows run it.
- **Reports:** the baseline report, and at the end an after-report, made from [references/report-template.md](references/report-template.md) in the git-ignored `cmd/perf/out/reports/`. They are not committed: their numbers hold only for the setup that made them. The round file links them.
- **Profiles:** each target's reference explains its profiles and how to rank them: Linux `perf` for the C++ server, Go profiles for the Go server.

## Skill Dependencies

- `skills/srs-develop/SKILL.md` — every code change, its tests and its commit.

## Target Router

A round measures one target, never two: a C++ round and a Go server round each have their own round file and reports. Load the reference of the target being measured:

| Target | Use when | Load |
|---|---|---|
| C++ media server | The round measures the origin or edge server in `trunk/`, on any protocol, with Linux `perf` profiles | [references/cpp-server.md](references/cpp-server.md) |
| Go server | The round measures the Go server in `cmd/` and `internal/`, today the proxy in front of C++ origins, with Go profiles | [references/go-server.md](references/go-server.md) |

When it is unclear which server the user means, ask. A new target gets its own reference and a row here; its text never goes into this file.

## Rules

- **Paths:** `scripts/` and `references/` resolve relative to the directory containing this `SKILL.md`, not the current working directory; repository paths such as `cmd/perf/` and `bin/` resolve relative to the repository root.
- **By hand:** a round starts only when the user asks. An SRS version bump in `srs-develop` asks the user whether to run one for the new version; start it only on a yes. Never from a commit, a PR, CI or a schedule. Nothing here is a pass or fail gate.
- **One at a time:** one scenario, one step, one profile. A run takes minutes, about 10 for RTMP, and a round as long as its tries need. Keep the ramp steps and holds at their usual length, so a run compares with the baseline.
- **Alone on the machine:** load skews other jobs and they skew the numbers. `perf-run.sh` waits for and holds the `srs-develop` Full-tier lock for the whole run, so it never runs beside a Full tier.
- **Linux in Docker:** the numbers are fair for profiles and for comparing two runs on the same setup, not as the capacity of a real server. Every report says so.
- **Flat CBR source:** the server's CPU follows the media bitrate, so a VBR source makes two runs, or two servers at different points of the loop, not comparable. Every measurement, on every target and protocol, uses the flat CBR source in `srs-perf:local`:
  - `/srs/perf/cbr.flv`: 60s, H.264 at 200 kbps CBR with a one-frame buffer, 25 fps, an IDR every 2s, no B frames, and AAC 32 kbps.
  - `/srs/perf/cbr-flatness.json`: its flatness, which every report records. The image fails to build when the source is not flat.
  - Not `doc/source.flv`: its video runs from a few kbps to over 1 Mbps in one second.
  - `bin/srs-perf run` and `smoke` read it by default; `-media <flv>` names another source, which needs its `<name>-flatness.json` beside it. They refuse a source whose flatness record is missing or not flat, and copy the source and its record into each result's setup. `compare` treats two sources as two setups.
  - `make perf-env-check` publishes it too; the containers mount no media.
- **Per-second samples:** `bin/srs-perf run` and `smoke` sample once a second and keep every sample in `result.json`:
  - The target's CPU is its container's cgroup `cpu.stat` `usage_usec`, which counts every thread to the microsecond. The load container runs with `--cgroupns host` and finds the cgroup from `/proc/<pid>/cgroup`; each sample keeps the raw `srs_cpu_usage_usec` too. The fields keep the `srs_` prefix for every target.
  - RSS is from `/proc/<pid>/status`, and each worker's CPU from its `/proc/<pid>/stat`.
  - Each step has a `window` with the mean and sd of each metric (CPU, RSS, the workers' average CPU, frames, bytes, lag, failures) over its counted samples; a hold's result carries its step's `window`.
  - Anything else in the target's container, such as a profiler, counts in its cgroup too, so no profiler runs in a measuring window.
- **Lag in publisher time:** a player fails when its newest message is more than 2s behind the publisher's newest:
  - SRS's default `time_jitter full` starts each player's timestamps near zero, so a player that joins after the first IDR gets timestamps about one GOP smaller than the publisher's, though it is live.
  - The load tool places a player's first video frame in the source by its payload, so every video frame of the source must have a unique payload; a run refuses a source without one.
- **No profiler in the window:** a measuring hold lets its clients settle 30s, then counts every sample of its window, 60s by default and 180s for a verdict, with nothing else running in the target's container:
  - The profilers run afterwards, in their own profile hold at the same client count, one per hold count.
  - A profile hold's CPU includes the profiler, so it is never a measurement: its result has no CPU per client, stays out of `run.json`, and `compare` refuses it.
  - The ramp only probes capacity by health. Its CPU per client includes connecting each step's new clients, so `compare` prints it as a note and never flags it.
- **A/B preferred, sequential accepted:** a comparison of two builds reports each side's CPU mean and sd over its window, and the difference with its 95% CI:
  - **A/B, preferred:** [scripts/perf-ab.sh](scripts/perf-ab.sh) runs both builds at once on their own cores, with the same source, load tool and client count, and pairs their samples per second. Both servers swing with the machine together, so the paired difference removes that shared noise.
  - **Sequential, accepted:** a run of the candidate compared with one recorded earlier under the same conditions: the source, cores, client counts and window. The CI is unpaired and noisier; say so wherever the result is written.
  - A verdict on a change uses a 180s window; both sides need the same window.
- **Temporary images:** only `srs-perf:local`, which every `make perf` rebuilds, is kept:
  - A baseline image `srs-perf-ab:<task>-<rev>` is removed by `perf-ab.sh` when it finishes, or with `docker rmi` once its comparison is done when it was kept with `-keep`.
  - A build-check image `srs-perf-build:<flag>` is removed when its check ends.
  - Remove every temporary image and container before a step ends, and log the images left. Ask the user before `docker builder prune`.
- **Shipped defaults:** each server runs with its shipped defaults and is configured only by environment variables, one env file per scenario under `cmd/perf/env/`, changed only where the scenario needs it. No config file. A tuning setting is a candidate like any other.

## Tools

The shared tools. Each target's reference lists its own scenarios, its `-target` flag and the tools only it uses.

- **`make perf`:** builds the image `srs-perf:local` and `bin/srs-perf`.
- **`make perf-unit`:** runs the load tool's Go unit tests.
- **`make perf-smoke SCENARIO=<s>`:** a short run of 100 clients of one scenario, to check that it works.
- **`make perf-smoke`:** With no `SCENARIO`, `make perf-smoke` smokes every scenario of the target in turn and fails if any failed; about 5 minutes.
- **`make perf-source-test`:** checks that the CBR source in the image is flat.
- **`make perf-env-check`:** checks the containers, `perf` symbols and the core calibration.
- **`bin/srs-perf compare <old.json> <new.json>`:** compares two results and flags a 10% change. For two holds, it gives each side's mean and sd and the 95% CI of the difference: paired per second for the two sides of one A/B run (`-ab-run <id>`), otherwise unpaired, which is noisier. The 10% flag uses the mean difference. It refuses two results of different targets.

Scripts, called by the workflows below:

- [scripts/perf-run.sh](scripts/perf-run.sh): the load run. A ramp to capacity in 5s steps, up to 40,000 clients (2,000 for SRT and WebRTC), three measuring holds (50%, 50% again for the noise, 90%), each a 30s settle and a `-window <seconds>`, then two profile holds into `profile-<50|90>/`. It prints the path of `run.json`.
- [scripts/perf-ab.sh](scripts/perf-ab.sh): the A/B run of two builds at once, the preferred comparison. The baseline is `-baseline <rev>`, or HEAD with `-revert <commit>`, that commit's change to the target taken out; the candidate is the worktree. It builds `srs-perf-ab:<task>-<rev>`, removed unless `-keep`, and writes `ab.json` and `verdict.txt`.
- [scripts/perf-ab-test.sh](scripts/perf-ab-test.sh): tests the A/B run with HEAD against itself.
- [scripts/perf-verify.sh](scripts/perf-verify.sh) `<run dir>`: checks a run before ranking, and prints its top symbols and noise.
- [scripts/perf-verify-test.sh](scripts/perf-verify-test.sh): tests the run check.
- [scripts/perf-cap-check.sh](scripts/perf-cap-check.sh): one 20s hold at the client cap, to check that every player connects.
- [scripts/perf-source.sh](scripts/perf-source.sh): encodes the CBR source.
- [scripts/perf-source-check.sh](scripts/perf-source-check.sh) `<flv> <target kbps>`: checks that a source is flat.
- [scripts/perf-source-test.sh](scripts/perf-source-test.sh): the test behind `make perf-source-test`.
- [scripts/perf-env-check.sh](scripts/perf-env-check.sh): the environment check.
- [scripts/perf-skill-test.sh](scripts/perf-skill-test.sh): checks this skill's text and scripts.

Outputs go to the git-ignored `cmd/perf/out/<run>/`: `run.json`, each result's `result.json` and noise, and the profiles in `profile-<50|90>/`.

## Workflows

Each step below names what the target's reference adds, such as its scenarios, profiles, tests and report name.

### Start a Round

1. Name it from the start date and the version of the server being measured, such as `cmd/perf/out/reports/2026-10-19-v8-round.md`, or `2026-10-19-v8-rtmp-round.md` for one protocol; the target's reference gives its name.
2. Copy [references/round-template.md](references/round-template.md) there, and fill every `<...>` placeholder: the version, the date, the target, the checkout and branch the round works in (the current one unless the user names another), the scenarios, and the limit on improvements and tries if the user gave one, such as one kept change in at most 3 tries.
3. When a baseline report already exists for the round's commit and setup, such as one made while building the tools, start from it instead of measuring again:
   - Point Background's Baseline at the report and its JSON, and tick the Baseline steps with the run and report they came from.
   - Fill the Ranking from the report's Bottlenecks, and Chain 1 for the top one, with its baseline row: capacity, CPU per client and CPU at both holds, and the top symbols with their shares.
   - Name the hold client counts of the baseline in Chain 1, since every fix is measured at the same counts.
4. Follow the target's Start a Round: which scenarios table the round keeps, how long its baseline takes, and how a fix is measured.
5. The round file is in the git-ignored `cmd/perf/out/reports/`, so it is never committed.

### Build the Tools

1. Run `make perf`.
2. Run `make perf-env-check` when the image, Docker or the machine changed since the last round. If `perf` gets no samples or no SRS symbols, stop and ask the user; do not build `perf`, switch to a VM, or use another profiler on your own.

### Run the Load and Profile

1. Run `bash scripts/perf-run.sh -scenario <s>`, with the target's flags:
   - One run, `-repeat 1`, is the default for a baseline and for a fix; about 10 minutes for RTMP.
   - The holds follow the run's own ramp. For a fix, `-hold-clients <50%>,<90%>` holds at the baseline's counts instead, such as `-hold-clients 6400,11520`, so the holds compare at the same load.
   - `-window <seconds>` sets the measuring holds' window: 60 by default, 180 for a fix's verdict. A comparison needs the same window on both sides.
   - Other flags pass through to `bin/srs-perf run` for the ramp, such as `-clients` and `-cap`.
2. Record in the round file the run directory, the capacity, each hold's CPU mean and sd and its CPU per client, the noise between the two 50% holds, and the core calibration gap. A gap over 10% marks the run noisy. The run does not measure the gap itself: it copies the one from the last `make perf-env-check`, so note that check's date.
3. Rerun only when a comparison's 95% CI spans the 10% line, such as a −9% difference with a CI of −12% to −6%: run an A/B, or run again with a 180s `-window`, and log why.

### Rank Bottlenecks

1. Run `bash scripts/perf-verify.sh <run dir>`. Fix or rerun a run it fails before ranking it. It prints each profile hold's top symbols and the noise between the two 50% holds; a noise of 10% or more is noted, so compare that scenario by an A/B or a 180s rerun. The target's reference says what else it checks.
2. Read each profile hold's files per the target's reference: its Linux `perf` profiles or Go profiles section.
3. Rank by cost at the 90% profile hold, grouped into the code path of the target that owns it.
4. Write the ranking table in the round file: the rank, the code path, its self share at both holds, the syscall counts, and the files and functions.
5. Start a chain for the new top bottleneck if it has none.

### Fix the Top Bottleneck

1. Take the current #1 of the ranking. Read its numbers and profile from the round file.
2. Change the target's code per `srs-develop`.
3. Run [Check](#check).
4. Measure:
   - **The verdict:** an A/B at the 90% hold count with the default 180s window, such as `perf-ab.sh -baseline HEAD -clients 11520` before the commit, or `-revert <commit>` after it. Its `verdict.txt` gives each side's CPU mean and sd and the paired difference with its 95% CI. The target's reference says which hold count fits its A/B.
   - **Capacity and the other scenarios:** [Run the Load and Profile](#run-the-load-and-profile) for each scenario the change targets and each scenario its code path also serves, with `-hold-clients` at the baseline's hold counts, then `bin/srs-perf compare <baseline.json> <run.json>` for each. This is a sequential comparison: its CI is unpaired and noisier.
5. Decide:
   - **Keep** when the tests pass and no measured scenario loses 10% or more in capacity or CPU per client, even when the numbers are flat. A gain may show only after the rest of a bottleneck spread over several functions is fixed.
   - **Drop** when a test fails or a scenario loses 10% or more: restore the changed files with `git checkout -- <files>`, commit nothing, and log the numbers and both symbol tables.
   - A loss counts by the mean difference; when its 95% CI spans the 10% line, rerun per [Run the Load and Profile](#run-the-load-and-profile) before dropping.
6. Add the step to the bottleneck's chain: the change, the commit, the method and window, CPU before and after as mean ± sd, the difference with its 95% CI, the capacity, and the top symbols with their shares.
7. Remove the A/B's baseline image if it was kept (`-keep`), and log the images left.
8. After a kept change, re-profile and [rank](#rank-bottlenecks) again; the next try fixes the new #1.
9. A chain with 5 kept steps and no gain overall is marked "no gain yet, needs review", is not reverted, and the round moves on to the next bottleneck.
10. The bottleneck work ends when the top bottleneck is something the target cannot reasonably fix, such as the kernel, syscalls already batched, or crypto in RTC, or when the round's limit is reached.

### Check

- After the change, the tests the target's Check names. Every layer must pass.
- `make perf-unit` when `cmd/perf` changed.

### Commit

Per the `srs-develop` commit format, one commit per kept change, on the round's branch; never push. Add the commit to its chain step. The message gives:

- The scenario, the client counts of the holds, and the run directories.
- The comparison's method and window, such as an A/B paired per second over 180s, or sequential and unpaired.
- The target's CPU before and after as mean ± sd, and the difference in points and percent with its 95% CI.
- The before and after capacity and CPU per client, and the `compare` flags.
- The top symbols with their self shares, before and after, from the profile holds.

### Write the Report

1. Make the report from [references/report-template.md](references/report-template.md) in `cmd/perf/out/reports/`, named per the target's Report, and copy the run's combined JSON next to it with the same name and `.json`. Both are git-ignored and stay on this machine, for the round and `bin/srs-perf compare`.
2. The baseline report fills every section except Before and after:
   - The Setup's Source is the result's `setup.source` and its flatness record `setup.source_flatness`: the per-second mean, min and max, the per-frame sd, and the ffmpeg version.
   - Each hold gives the target's CPU as mean ± sd over its window, from the result's `window`.
3. The after-report fills Before and after too, with `bin/srs-perf compare` against the baseline, the method and 95% CI of each comparison, and the list of the round's commits:
   - Its final run uses `-hold-clients` at the baseline's hold counts, so every hold has a partner in the compare.
   - When no version bump came between them, it has the baseline's date and version and the `-after-docker.md` name of the target's Report, with its JSON beside it; link the baseline report from Before and after.
4. Link it from the round file's Background.
5. Flame graphs and profile data stay in `cmd/perf/out/`; copy only the top-symbol tables into the report.

### Review

The user reviews the round's commits, listed in the chains, asynchronously while the round runs. Looking at the chains, the review keeps the commits that helped and drops the rest; the round needs no approval to go on.
