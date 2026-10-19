# SRS Performance Round v<version>

A round file of the `internal-perf-for-srs` skill.

**Status: ready.** Next: build the tools.

## Goal and scope

Measure SRS v<version> under many simulated clients, fix the top bottleneck again and again, and report what improved. Started by hand on <date>.

- **Scenarios:** <scenarios, such as rtmp-rtmp, or every scenario in the registry>.
- **Limit:** <none: until the top bottleneck is something SRS cannot reasonably fix; or a limit, such as one kept change in at most <n> tries>.
- **Code:** `<checkout>` on `<branch>`, from `<base commit>` on <date>. The round measures and commits there; it needs no separate checkout.
- **In scope:** the baseline run and report, the bottleneck chains, the final run and the after-report.
- **Out of scope:** a pass or fail gate, tuned configs beyond the scenario configs, and macOS native runs.

## Background

- **Skill:** `skills/internal-perf-for-srs/SKILL.md`; every step follows its workflows.
- **Last round:** <the last round's after-report in `cmd/perf/out/reports/`, or none>.
- **Baseline:** <`cmd/perf/out/reports/<YYYY-MM-DD>-v<version>-docker.md` and its JSON, written by the baseline report step>.
- **Setup:** <machine model, cores, memory, Docker version and VM CPUs and memory, kernel>.

## Current state

| Item | State |
|---|---|
| Round | Ready |
| Code | `<checkout>` on `<branch>`, clean |
| Baseline | none |
| Top bottleneck | not ranked yet |
| Changes | 0 kept, 0 dropped |

**Next step:** build the tools.

## Decisions

These come from the skill and hold for every round:

- **Keep by default:** a change on the top bottleneck is kept when its tests pass and no measured scenario loses 10% or more in capacity or CPU per client, even when the numbers are flat. The review drops what did not help.
- **Drop:** only when a test fails or a scenario loses 10% or more. Restore the files, commit nothing, and log the numbers and both symbol tables.
- **Always the top bottleneck:** re-rank after each kept change; the next try fixes the new #1. No fixed candidate list.
- **Chain limit:** 5 kept steps with no gain overall mark the chain "no gain yet, needs review"; move on to the next bottleneck.
- **End:** when the top bottleneck is something SRS cannot reasonably fix, such as the kernel, syscalls already batched or crypto in RTC, or when the limit is reached.

Decided for this round:

- <date, decision>

## Ranking

The current ranking, replaced after each re-rank, from the 90% hold of the latest run.

| Rank | Code path | Self share at 50% and 90% | Syscalls | SRS files and functions | Chain |
|---|---|---|---|---|---|

## Chains

One chain per bottleneck: a step per change, kept or dropped, with the numbers before and after at the same hold client counts. Step 0 is the baseline the chain starts from. The Commit column is the round's list of commits for the user's review.

### Chain 1: <code path>

| Step | Change | Commit | Capacity | CPU per client | SRS CPU at 50% and 90% | Top symbols before → after | Result |
|---|---|---|---|---|---|---|---|
| 0 | Baseline, no change | <baseline commit> | <capacity> | <CPU per client> | <SRS CPU> | <top symbols with shares> | baseline |

## Steps

The round's progress. Tick a step when it is done, and log it in the Work log.

**Baseline:**

- [ ] **Build the tools:** the skill's Build the Tools. Test: `make perf` and `make perf-env-check` pass.
- [ ] **Baseline run:** the skill's Run the Load and Profile for every scenario of the round, `perf-run.sh -scenario <s>`, one run each. Record the noise between its two 50% holds.
- [ ] **Baseline report:** the skill's Write the Report, and its JSON, with every section except Before and after. No SRS code changes. Link it from Background.
- [ ] **Rank:** the skill's Rank Bottlenecks; fill the Ranking and start Chain 1 for the top bottleneck.

**Bottlenecks:** each try is the skill's Fix the Top Bottleneck, Check and Commit on the current #1, and adds a step to its chain. The work goes on until the limit or the end in Decisions.

**Final:**

- [ ] **Final run:** `perf-run.sh -scenario <s> -hold-clients <50%>,<90%>` for every scenario of the round, on the round's final commit, at the baseline's hold counts.
- [ ] **After-report:** the after-report and its JSON with `bin/srs-perf compare` against the baseline JSON, the before and after table per scenario, and the round's commits. Link it from Background.
- [ ] **Close:** a final log line in the Work log, and the Current state.

## Later-round candidates

Candidates a round may try after its bottleneck chains end. Each is judged like a bottleneck fix.

**Profiling builds:** keep or remove each, from `make perf-build-check`. A removal's gain is less code with every test passing.

- [ ] gperftools: `--gperf=on --gcp=on --gmp=on`, `--gmc`, `--gmd`, `/api/v1/tcmalloc`, `tcmalloc_release_rate` and `3rdparty/gperftools-2-fit`.
- [ ] gprof: `--gprof=on` (`-pg -lc_p`).
- [ ] valgrind: `--valgrind=on` and `/api/v1/valgrind`.

**Fixed delays:** kept when the delay drops and CPU per client in its scenarios does not rise by 10% or more, measured with the delay probes.

- [ ] The RTMP player's receive thread sleeps 350ms (`SRS_PERF_MW_SLEEP`), which delays a command sent while playing.
- [ ] Forwarders and edges dump their queue once per 500ms receive timeout (`SRS_CONSTS_RTMP_PULSE`), adding up to 500ms to a forwarded stream.
- [ ] The 3s source check in `srs_app_rtmp_source.cpp`.
- [ ] The 5s `hls_ctx` timer.

## Work log

- **<date>:** Round file made from the template. Next: build the tools.
