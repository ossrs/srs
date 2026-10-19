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
- **One at a time:** one scenario, one step, one profile. A run takes minutes, about 6–7 for RTMP, and a round as long as its tries need. Keep the ramp steps and holds at their usual length, so a run compares with the baseline.
- **Alone on the machine:** load skews other jobs and they skew the numbers. `perf-run.sh` waits for and holds the `srs-develop` Full-tier lock for the whole run, so it never runs beside a Full tier.
- **Linux in Docker:** the numbers are fair for profiles and for comparing two runs on the same setup, not as the capacity of a real server. Every report says so.
- **Flat CBR source:** SRS CPU follows the media bitrate, so a VBR source makes two runs, or two servers at different points of the loop, not comparable. Every measurement, on every protocol, uses the flat CBR source in `srs-perf:local`:
  - `/srs/perf/cbr.flv`: 60s, H.264 at 200 kbps CBR with a one-frame buffer, 25 fps, an IDR every 2s, no B frames, and AAC 32 kbps.
  - `/srs/perf/cbr-flatness.json`: its flatness, which every report records. The image fails to build when the source is not flat.
  - Not `doc/source.flv`: its video runs from a few kbps to over 1 Mbps in one second.
- **Shipped defaults:** SRS runs with `-e`, configured only by environment variables, one env file per scenario in `cmd/perf/env/<scenario>.env`, changed only where the scenario needs it. No config file. A tuning setting is a candidate like any other.

## Tools

- **`make perf`:** builds the profiling image `srs-perf:local` and `bin/srs-perf`, nothing else; minutes.
- **`make perf-unit`:** the load tool's Go unit tests; run before each commit of `cmd/perf`.
- **`make perf-smoke SCENARIO=<s>`:** about 10s at the first ramp step, in the containers.
- **`make perf-source-test`:** builds the image, then checks its CBR source is flat and that the flatness check rejects a VBR file and a CBR encode with a large buffer.
- **`make perf-env-check`:** checks the containers, `perf` samples with SRS symbols, and the core calibration.
- **`make perf-up` and `make perf-down`:** start and stop the SRS and load containers.
- **`bin/srs-perf compare <old.json> <new.json>`:** compares two results on the same setup and flags a 10% change. Given a combined `run.json` or a report's JSON, it pairs the results by mode and client count.

The scripts, called by the workflows below:

- [scripts/perf-run.sh](scripts/perf-run.sh): the load run. It builds with `make perf`, takes and refreshes the lock, checks the scenario ports are free, runs `make integ-prepare`, then per repeat:
  - A `ramp` to capacity. Each step lasts 5s: the first 2s let the new clients settle, and only the last 3 one-second samples count. The ramp stops at 90% SRS CPU, a failed health check, or the cap of 40,000 clients for the TCP protocols.
  - Three 75s holds: at 50% of that capacity, again at 50% to measure the noise, and at 90%. Each has a 60s `perf record`, a flame graph, a symbol table and 10s syscall counts.
  - The noise: `bin/srs-perf compare` of the two 50% holds, which shows how much CPU per client moves between two identical holds.
  - It prints the path of `run.json`.
- [scripts/perf-profile.sh](scripts/perf-profile.sh): the profile of one hold, run inside the SRS container.
- [scripts/perf-source.sh](scripts/perf-source.sh): encodes the CBR source with its fixed parameters and writes its flatness JSON; the image build runs it.
- [scripts/perf-source-check.sh](scripts/perf-source-check.sh) `<flv> <target kbps>`: prints a source's flatness JSON, and fails when a whole second after the first leaves ±3% of the target or the per-frame sd is over 3% of the mean. The first second and the first IDR are reported, not judged.
- [scripts/perf-source-test.sh](scripts/perf-source-test.sh): the test behind `make perf-source-test`.
- [scripts/perf-env-check.sh](scripts/perf-env-check.sh): the environment check.
- [scripts/perf-cap-check.sh](scripts/perf-cap-check.sh): one 10s hold at the client cap, which checks that all of its players connect (file limits, ephemeral ports, `max_connections`). Run it after changing the cap or the containers.
- [scripts/perf-skill-test.sh](scripts/perf-skill-test.sh): checks this skill's text, a dry run of [Start a Round](#start-a-round), the `srs-develop` route, and the reports in `cmd/perf/out/reports/` where they exist.

Outputs go to the git-ignored `cmd/perf/out/<run>/`: `run.json` with the results and the noise, and per repeat each result's `result.json`, `noise.txt` and `noise.json`, and each hold's `flamegraph.svg`, `top-symbols.txt`, `syscalls.txt` and `perf.data`.

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
   - One run, `-repeat 1`, is the default for a baseline and for a fix; about 6–7 minutes for RTMP.
   - The holds follow the run's own ramp. For a fix, `-hold-clients <50%>,<90%>` holds at the baseline's counts instead, such as `-hold-clients 6400,11520`, so the holds compare at the same load.
   - Other flags pass through to `bin/srs-perf run`, such as `-clients`, `-seconds` and `-cap` of the ramp.
2. Record in the round file the run directory, the capacity, CPU per client, the noise between the two 50% holds, and the core calibration gap. A gap over 10% marks the run noisy. The run does not measure the gap itself: it copies the one from the last `make perf-env-check`, so note that check's date.
3. Rerun only when a comparison lands near the 10% line: when the change is within the run's noise of 10%, such as 8% with a 3% noise. Use `-repeat 3`, or run again, and log why.

### Rank Bottlenecks

1. Read each hold's `top-symbols.txt`, `syscalls.txt` and flame graph per [references/reading-profiles.md](references/reading-profiles.md).
2. Rank by cost at the 90% hold, grouped into the SRS code path that owns it.
3. Write the ranking table in the round file: the rank, the code path, its self share at both holds, the syscall counts, and the SRS files and functions.
4. Start a chain for the new top bottleneck if it has none.

### Fix the Top Bottleneck

1. Take the current #1 of the ranking. Read its numbers and profile from the round file.
2. Change the SRS code per `srs-develop`.
3. Run [Check](#check).
4. Measure with [Run the Load and Profile](#run-the-load-and-profile) for each scenario the change targets and each scenario its code path also serves, with `-hold-clients` at the baseline's hold counts, then `bin/srs-perf compare <baseline.json> <run.json>` for each.
5. Decide:
   - **Keep** when the tests pass and no measured scenario loses 10% or more in capacity or CPU per client, even when the numbers are flat. A gain may show only after the rest of a bottleneck spread over several functions is fixed.
   - **Drop** when a test fails or a scenario loses 10% or more: restore the changed files with `git checkout -- <files>`, commit nothing, and log the numbers and both symbol tables.
6. Add the step to the bottleneck's chain: the change, the commit, the before and after capacity, CPU per client, SRS CPU at both holds, and the top symbols with their shares.
7. After a kept change, re-profile and [rank](#rank-bottlenecks) again; the next try fixes the new #1.
8. A chain with 5 kept steps and no gain overall is marked "no gain yet, needs review", is not reverted, and the round moves on to the next bottleneck.
9. The bottleneck work ends when the top bottleneck is something SRS cannot reasonably fix, such as the kernel, syscalls already batched, or crypto in RTC, or when the round's limit is reached.

### Check

- After the change, all the tests: the `srs-develop` Full tier. Every layer must pass.
- `make perf-unit` when `cmd/perf` changed.

### Commit

Per the `srs-develop` commit format, one commit per kept change, on the round's branch; never push. Add the commit to its chain step. The message gives:

- The scenario, the client counts of the holds, and the run directories.
- The before and after capacity and CPU per client, and the `compare` flags.
- The top symbols with their self shares, before and after.

### Write the Report

1. Make `cmd/perf/out/reports/<YYYY-MM-DD>-v<version>-docker.md` from [references/report-template.md](references/report-template.md), and copy the run's combined JSON next to it with the same name and `.json`. Both are git-ignored and stay on this machine, for the round and `bin/srs-perf compare`.
2. The baseline report fills every section except Before and after. The after-report fills Before and after too, with `bin/srs-perf compare` against the baseline and the list of the round's commits:
   - Its final run uses `-hold-clients` at the baseline's hold counts, so every hold has a partner in the compare.
   - When no version bump came between them, it has the baseline's date and version; name it `<YYYY-MM-DD>-v<version>-after-docker.md`, with its JSON beside it, and link the baseline report from Before and after.
3. Link it from the round file's Background.
4. Flame graphs and `perf.data` stay in `cmd/perf/out/`; copy only the top-symbol tables into the report.

### Review

The user reviews the round's commits, listed in the chains, asynchronously while the round runs. Looking at the chains, the review keeps the commits that helped and drops the rest; the round needs no approval to go on.
