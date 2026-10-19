#!/bin/bash
# Checks the skill text, a dry run of "Start a Round" from the round template,
# the srs-develop route, and the reports in cmd/perf/out/reports where they exist.
set -uo pipefail

cd "$(dirname "$0")/.."
failed=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; failed=1; }
has() {
    if grep -qF -- "$2" "$1" 2>/dev/null; then pass "$1: $2"; else fail "$1: $2"; fi
}

has SKILL.md "name: internal-perf-for-srs"
for workflow in "### Start a Round" "### Build the Tools" "### Run the Load and Profile" \
    "### Rank Bottlenecks" "### Fix the Top Bottleneck" "### Check" "### Commit" \
    "### Write the Report" "### Review"; do
    has SKILL.md "$workflow"
done
for link in $(grep -oE '\]\((references|scripts)/[^)#]+' SKILL.md | sed 's/^](//' | sort -u); do
    if [[ -f "$link" ]]; then pass "link $link"; else fail "link $link"; fi
done
for text in "**Flat CBR source:**" "/srs/perf/cbr.flv" "make perf-source-test" \
    "](scripts/perf-source.sh)" "](scripts/perf-source-check.sh)" "](scripts/perf-source-test.sh)"; do
    has SKILL.md "$text"
done
# Runs, the smoke and the env check use the CBR source in the image, never doc/source.flv.
has SKILL.md '`-media <flv>`'
has scripts/perf-env-check.sh "-i /srs/perf/cbr.flv"
for script in scripts/perf-up.sh scripts/perf-env-check.sh scripts/perf-smoke.sh scripts/perf-run.sh; do
    if grep -qF "doc/source.flv" "$script"; then fail "$script: no doc/source.flv"; else pass "$script: no doc/source.flv"; fi
done
# SRS CPU is sampled each second from its container's cgroup, and every sample is kept.
for text in "**Per-second samples:**" "usage_usec" "--cgroupns host" '`window`'; do
    has SKILL.md "$text"
done
has scripts/perf-up.sh "--cgroupns host"
# Lag is in publisher time, since SRS starts each player's timestamps near zero.
for text in "**Lag in publisher time:**" '`time_jitter full`' "unique payload"; do
    has SKILL.md "$text"
done
# The A/B run: the baseline SRS from a git tree, never touching the worktree, and its image removed.
for text in "](scripts/perf-ab.sh)" "](scripts/perf-ab-test.sh)" '`-baseline <rev>`' \
    '`-revert <commit>`' '`-keep`' '`srs-perf-ab:<task>-<rev>`' "ab.json" "verdict.txt"; do
    has SKILL.md "$text"
done
has SKILL.md "that commit's trunk change taken out"
has scripts/perf-ab.sh "git archive"
has scripts/perf-ab.sh "acquire --wait"
has scripts/perf-ab.sh "docker rmi"
if grep -qE 'git (checkout|stash|reset|revert)' scripts/perf-ab.sh; then
    fail "perf-ab.sh: the worktree untouched"
else
    pass "perf-ab.sh: the worktree untouched"
fi
for file in scripts/perf-run.sh SKILL.md; do
    if grep -qF "integ-prepare" "$file"; then fail "$file: no integ-prepare"; else pass "$file: no integ-prepare"; fi
done
if grep -rqF --include='*.go' --exclude='*_test.go' '"doc/source.flv"' ../../cmd/perf; then
    fail "cmd/perf: no doc/source.flv"
else
    pass "cmd/perf: no doc/source.flv"
fi

for section in "## Setup" "## Caveats" "## Method" "## Results" "## Bottlenecks" \
    "## Syscalls" "## Before and after" "## Re-run and compare"; do
    has references/report-template.md "$section"
done
for topic in "## Flame graph" "## Symbol table" "## Syscall counts" "## Hotspot moved" "## Ranking"; do
    has references/reading-profiles.md "$topic"
done

if grep -rqiE 'william|/Users/|winlin' SKILL.md references; then
    fail "no personal names or local paths"
else
    pass "no personal names or local paths"
fi

mkdir -p /tmp/srs-perf-round-test
round=/tmp/srs-perf-round-test/srs-perf-v0.0.0-dryrun.md
sed -e 's/<version>/0.0.0-dryrun/g' -e 's/<date>/2000-01-01/g' \
    references/round-template.md > "$round"
for section in "## Goal and scope" "## Background" "## Current state" "## Decisions" \
    "## Ranking" "## Chains" "## Steps" "## Later-round candidates" "## Work log"; do
    has "$round" "$section"
done
# A round file is this skill's own format, not an srs-autopilot task file: no worktree,
# tracker row, review table or autopilot tasks, so the autopilot loop never runs it.
has "$round" "A round file of the \`internal-perf-for-srs\` skill."
for text in "## Repositories" "## Review" "## Phases and tasks" "tracker" "worktree" "cherry-pick"; do
    if grep -qiF -- "$text" "$round"; then fail "round file: no $text"; else pass "round file: no $text"; fi
done
if grep -F 'srs-autopilot' SKILL.md | grep -vqF 'not an srs-autopilot'; then
    fail "SKILL.md does not depend on srs-autopilot"
else
    pass "SKILL.md does not depend on srs-autopilot"
fi
has "$round" "| Step | Change | Commit | Method and window | SRS CPU before → after, mean ± sd | Difference, 95% CI | Capacity | Top symbols before → after | Result |"
for candidate in "--gperf=on" "--gprof=on" "--valgrind=on" "SRS_PERF_MW_SLEEP" \
    "SRS_CONSTS_RTMP_PULSE" "3s source check" "hls_ctx"; do
    has "$round" "$candidate"
done
for last in "perf-run.sh" "bin/srs-perf compare" "after-report" "final log line"; do
    has "$round" "$last"
done
if grep -qE '<version>|<date>' "$round"; then fail "dry run filled version and date"; else pass "dry run filled version and date"; fi
# A chain starts from its baseline row, so each step compares with the same numbers.
has "$round" "| 0 | Baseline, no change |"
# A round may start from a baseline report made before it, and may limit its tries.
start=$(sed -n '/^### Start a Round/,/^### /p' SKILL.md)
for text in "baseline report already exists" "Baseline steps" "Ranking" "Chain 1" "tries"; do
    if grep -qF -- "$text" <<< "$start"; then pass "Start a Round: $text"; else fail "Start a Round: $text"; fi
done
has references/round-template.md "at most <n> tries"
# A code change passes every test, not only the unit tests and integ: the srs-develop Full tier.
check=$(sed -n '/^### Check/,/^### /p' SKILL.md)
if grep -qF 'Full tier' <<< "$check"; then pass "Check runs the Full tier"; else fail "Check runs the Full tier"; fi
# A round file is the skill's working output, like its reports: git-ignored beside them, not in tasks/.
has SKILL.md "cmd/perf/out/reports/<YYYY-MM-DD>-v<version>-round.md"
if grep -qF 'tasks/' SKILL.md references/*.md; then fail "no round file in tasks/"; else pass "no round file in tasks/"; fi
if git check-ignore -q ../../cmd/perf/out/reports/x-round.md; then pass "round files git-ignored"; else fail "round files git-ignored"; fi
rm -rf /tmp/srs-perf-round-test

# Every relative link in file $1 resolves from the file's own folder.
links_exist() {
    local dir link
    dir=$(dirname "$1")
    [[ -f "$1" ]] || { fail "$1 exists"; return; }
    for link in $(grep -oE '\]\([^)# ]+' "$1" | sed 's/^](//' | grep -v '^[a-z]*:' | sort -u); do
        if [[ -e "$dir/$link" ]]; then pass "$1 link $link"; else fail "$1 link $link"; fi
    done
}

develop=../srs-develop/SKILL.md
deps=$(sed -n '/^## Skill Dependencies/,/^## /p' "$develop")
router=$(sed -n '/^## Task Router/,$p' "$develop")
if grep -qF 'skills/internal-perf-for-srs/SKILL.md' <<< "$deps"; then
    pass "srs-develop Skill Dependencies names the skill"
else
    fail "srs-develop Skill Dependencies names the skill"
fi
row=$(grep -F 'internal-perf-for-srs' <<< "$router")
for word in performance benchmark "load test" profile; do
    if grep -qiF "$word" <<< "$row"; then pass "router row: $word"; else fail "router row: $word"; fi
done
links_exist "$develop"

# Reports and their data hold only for the setup that made them, so none is committed.
if [[ -z "$(git ls-files ../../doc/performance ../../cmd/perf/out)" ]]; then
    pass "no report committed"
else
    fail "no report committed"
fi
if git check-ignore -q ../../cmd/perf/out/reports/x-docker.md; then pass "reports git-ignored"; else fail "reports git-ignored"; fi
# The 10s syscall count must fall inside the 75s hold: it runs right after the 60s
# record, before the report and flame graph, and perf trace stops on its own, since
# under timeout it is killed before it prints the summary.
profile=scripts/perf-profile.sh
record_line=$(grep -n 'perf record' "$profile" | head -1 | cut -d: -f1)
trace_line=$(grep -n 'perf trace' "$profile" | head -1 | cut -d: -f1)
report_line=$(grep -n 'perf report' "$profile" | head -1 | cut -d: -f1)
if [[ -n "$trace_line" && "$record_line" -lt "$trace_line" && "$trace_line" -lt "$report_line" ]]; then
    pass "$profile: syscall count after the record, before the report"
else
    fail "$profile: syscall count after the record, before the report"
fi
has "$profile" "perf trace -s -p 1 -- sleep 10"
if grep -qE 'timeout [0-9]+ perf trace' "$profile"; then
    fail "$profile: perf trace not under timeout"
else
    pass "$profile: perf trace not under timeout"
fi

# No profiler in a measuring window: the measuring holds settle 30s and count a
# -window with nothing else in the SRS container; perf runs afterwards in its
# own profile hold per count, whose CPU is never a measurement.
run=scripts/perf-run.sh
has "$run" "window=60"
has "$run" '-mode hold'
has "$run" '-window "$window"'
has "$run" '-mode profile'
last_hold=$(grep -n -- '-mode hold' "$run" | tail -1 | cut -d: -f1)
first_profile=$(grep -n 'perf-profile.sh' "$run" | head -1 | cut -d: -f1)
first_profile_hold=$(grep -n -- '-mode profile' "$run" | head -1 | cut -d: -f1)
if [[ -n "$last_hold" && -n "$first_profile" && -n "$first_profile_hold" \
    && "$last_hold" -lt "$first_profile_hold" && "$last_hold" -lt "$first_profile" ]]; then
    pass "$run: profile holds after the measuring holds"
else
    fail "$run: profile holds after the measuring holds"
fi
if grep -qE 'perf-profile\.sh.*&[[:space:]]*$' "$run"; then
    fail "$run: no profiler beside a measuring hold"
else
    pass "$run: no profiler beside a measuring hold"
fi
if grep -qE -- '-seconds 75|-seconds "' "$run"; then
    fail "$run: holds use -settle and -window, not -seconds"
else
    pass "$run: holds use -settle and -window, not -seconds"
fi
for text in "**No profiler in the window:**" "profile hold" '`-window <seconds>`' "180s" "settle 30s" "profile-<50|90>"; do
    has SKILL.md "$text"
done
has references/reading-profiles.md "profile-<50|90>"
# compare gives each side's mean and sd and the 95% CI of the difference:
# paired per second for the two sides of an A/B run, unpaired otherwise.
for text in "each side's mean and sd" "95% CI" "paired per second" '`-ab-run <id>`' "unpaired" "noisier" "mean difference"; do
    has SKILL.md "$text"
done
for doc in SKILL.md references/reading-profiles.md references/report-template.md; do
    if grep -qE '75s hold|75s at 50%|last 20 counted|inside the 75s|while the hold still runs' "$doc"; then
        fail "$doc: no profiler in the measuring hold"
    else
        pass "$doc: no profiler in the measuring hold"
    fi
done

# Every local report follows the template, and its JSON has results.
reports=$(ls ../../cmd/perf/out/reports/*-docker.md 2>/dev/null)
for report in $reports; do
    name=$(basename "$report" .md)
    for section in "## Setup" "## Caveats" "## Method" "## Results" "## Bottlenecks" \
        "## Syscalls" "## Re-run and compare"; do
        has "$report" "$section"
    done
    if grep -qE '<(version|YYYY-MM-DD|commit|scenario|gap|noise|kernel|window)>' "$report"; then
        fail "$name: placeholders filled"
    else
        pass "$name: placeholders filled"
    fi
    json="../../cmd/perf/out/reports/$name.json"
    if [[ -f "$json" ]]; then
        if jq -e '.results | length > 0 and all(.[]; .scenario.name != null)' "$json" >/dev/null 2>&1; then
            pass "$name: JSON has results"
        else
            fail "$name: JSON has results"
        fi
        if jq -e '[.results[] | select(.scenario.parameters.mode == "ramp") | .capacity] | length > 0 and all(.[]; . > 0)' \
            "$json" >/dev/null 2>&1; then
            pass "$name: JSON ramps have capacity"
        else
            fail "$name: JSON ramps have capacity"
        fi
    fi
    links_exist "$report"
done

# An after-report on the baseline's date and version gets its own name, and fills
# Before and after with the compare table, the fix's holds and the round's commits.
write=$(sed -n '/^### Write the Report/,/^### /p' SKILL.md)
for text in "-after-docker.md" "-hold-clients"; do
    if grep -qF -- "$text" <<< "$write"; then pass "Write the Report: $text"; else fail "Write the Report: $text"; fi
done
afters=$(ls ../../cmd/perf/out/reports/*-after-docker.md 2>/dev/null)
for report in $afters; do
    name=$(basename "$report" .md)
    after=$(sed -n '/^## Before and after/,/^## /p' "$report")
    if grep -qF "Only in the after-report" <<< "$after"; then
        fail "$name: Before and after filled"
    else
        pass "$name: Before and after filled"
    fi
    for text in "| Scenario | Capacity before → after | CPU per client before → after | Flags |" \
        "| Commit | Bottleneck | Subject |" "bin/srs-perf compare"; do
        if grep -qF -- "$text" <<< "$after"; then pass "$name: $text"; else fail "$name: $text"; fi
    done
    if grep -qE '^\| `[0-9a-f]{9}` \|' <<< "$after"; then pass "$name: commit rows"; else fail "$name: commit rows"; fi
    # A change's verdict is an A/B, paired per second, with its 95% CI.
    for text in "**Method:**" \
        "| Scenario | Hold clients | Method and window | SRS CPU before, mean ± sd | SRS CPU after, mean ± sd | Difference, 95% CI |" \
        "paired per second"; do
        if grep -qF -- "$text" <<< "$after"; then pass "$name: $text"; else fail "$name: $text"; fi
    done
    json="../../cmd/perf/out/reports/$name.json"
    if [[ -f "$json" ]]; then
        if jq -e '.ab | length > 0 and all(.[]; .ab_run != null and .difference.ci95 != null)' "$json" >/dev/null 2>&1; then
            pass "$name: JSON has the A/B"
        else
            fail "$name: JSON has the A/B"
        fi
    fi
    if grep -qE '\(([0-9]{4}-[0-9]{2}-[0-9]{2}-v[^)]*-docker)\.md\)' <<< "$after"; then
        pass "$name: links its baseline"
    else
        fail "$name: links its baseline"
    fi
done

# A report measured before the flat CBR source, whose results record no source,
# says it was VBR, so nobody compares it with a CBR run.
for report in $reports; do
    name=$(basename "$report" .md)
    json="../../cmd/perf/out/reports/$name.json"
    [[ -f "$json" ]] || continue
    if jq -e 'all(.results[]; .setup.source == null)' "$json" >/dev/null 2>&1; then
        has "$report" "VBR"
    fi
done

# A report on the flat CBR source records the source and its flatness, gives each
# hold's SRS CPU as mean ± sd over its window, and the noise with its 95% CI; its
# JSON holds only measuring holds with a window, no profile hold.
for report in $reports; do
    name=$(basename "$report" .md)
    json="../../cmd/perf/out/reports/$name.json"
    [[ -f "$json" ]] || continue
    jq -e '.results | length > 0 and all(.[]; .setup.source != null)' "$json" >/dev/null 2>&1 || continue
    source=$(jq -r '.results[0].setup.source' "$json")
    has "$report" "| Source | \`$source\`"
    mean=$(printf '%.1f' "$(jq -r '.results[0].setup.source_flatness.second.mean' "$json")")
    ffmpeg=$(jq -r '.results[0].setup.source_flatness.ffmpeg' "$json" | awk '{print $3}')
    setup=$(sed -n '/^## Setup/,/^## /p' "$report")
    for text in "$mean kbps" "$ffmpeg"; do
        if grep -qF -- "$text" <<< "$setup"; then pass "$name: Source $text"; else fail "$name: Source $text"; fi
    done
    results=$(sed -n '/^## Results/,/^## /p' "$report")
    if grep -qF "mean ± sd" <<< "$results"; then pass "$name: holds mean ± sd"; else fail "$name: holds mean ± sd"; fi
    noise=$(grep -F '**Noise:**' "$report")
    if grep -qF "95% CI" <<< "$noise"; then pass "$name: noise 95% CI"; else fail "$name: noise 95% CI"; fi
    if grep -qE 'VBR source, profiler in the hold' "$report"; then
        fail "$name: no VBR caveat on a CBR report"
    else
        pass "$name: no VBR caveat on a CBR report"
    fi
    if jq -e '[.results[] | select(.scenario.parameters.mode == "hold")] | length > 0 and all(.[]; .window.srs_cpu_percent.sd != null)' \
        "$json" >/dev/null 2>&1; then
        pass "$name: JSON holds have a window"
    else
        fail "$name: JSON holds have a window"
    fi
    if jq -e 'all(.results[]; .scenario.parameters.mode != "profile")' "$json" >/dev/null 2>&1; then
        pass "$name: JSON has no profile hold"
    else
        fail "$name: JSON has no profile hold"
    fi
done


# The step length, cap and run count the skill states match the tools.
if grep -qx 'repeat=1' scripts/perf-run.sh; then pass "perf-run.sh: one run by default"; else fail "perf-run.sh: one run by default"; fi
has scripts/perf-run.sh "50-noise"
for doc in SKILL.md references/report-template.md; do
    has "$doc" "5s"
    has "$doc" "40,000"
    has "$doc" "noise"
    if grep -qE -- '-repeat 3`, the default|holds 30s|last 20s|10,000' "$doc"; then
        fail "$doc: no old step, cap or repeat text"
    else
        pass "$doc: no old step, cap or repeat text"
    fi
done
# A fix holds at the baseline's client counts, and compare reads a combined run.json.
has scripts/perf-run.sh "-hold-clients"
has SKILL.md "-hold-clients"
has SKILL.md "run.json"
if grep -qF "does not read the combined JSON" SKILL.md references/*.md; then
    fail "compare reads the combined JSON"
else
    pass "compare reads the combined JSON"
fi
if grep -qE -- '-repeat 3' references/round-template.md; then
    fail "round template: one run by default"
else
    pass "round template: one run by default"
fi

# The measurement rules in every place a round reads them: a flat CBR source, per-second
# samples, no profiler in the window, A/B preferred and sequential accepted, and temporary
# images removed.
for text in "**A/B preferred, sequential accepted:**" "**Temporary images:**" \
    '`srs-perf-build:<flag>`' "docker builder prune" "images left"; do
    has SKILL.md "$text"
done
section() { sed -n "/^### $1/,/^### /p" SKILL.md; }
in_section() {
    if grep -qF -- "$3" <<< "$(section "$1")"; then pass "$2: $3"; else fail "$2: $3"; fi
}
in_section "Run the Load and Profile" "Run the Load" "mean and sd"
in_section "Run the Load and Profile" "Run the Load" "95% CI"
in_section "Fix the Top Bottleneck" "Fix" "perf-ab.sh"
in_section "Fix the Top Bottleneck" "Fix" "180s"
in_section "Fix the Top Bottleneck" "Fix" "95% CI"
in_section "Commit" "Commit" "95% CI"
in_section "Commit" "Commit" "method"
in_section "Write the Report" "Write the Report" "flatness"
has SKILL.md "one 20s hold"
if grep -qF "one 10s hold" SKILL.md; then fail "SKILL.md: cap check is 20s"; else pass "SKILL.md: cap check is 20s"; fi
template=references/report-template.md
for text in "| Source |" "<flatness>" "usage_usec" "mean ± sd" "95% CI" "paired per second" \
    "sequential" "noisier" "perf-ab.sh" "<method>"; do
    has "$template" "$text"
done
if grep -qF "within the noise of the 10% line" "$template"; then
    fail "$template: rerun by the CI, not the noise"
else
    pass "$template: rerun by the CI, not the noise"
fi
decisions=$(sed -n '/^## Decisions/,/^## /p' references/round-template.md)
for text in "**Flat CBR source:**" "**Per-second samples:**" "**No profiler in the window:**" \
    "**A/B preferred, sequential accepted:**" "**Temporary images:**"; do
    if grep -qF -- "$text" <<< "$decisions"; then pass "round Decisions: $text"; else fail "round Decisions: $text"; fi
done
has references/round-template.md "**Source:**"
has references/round-template.md "perf-ab.sh"
for text in "window mean" "90% profile hold" "no CPU per client"; do
    has references/reading-profiles.md "$text"
done
if grep -rqF "doc/performance" SKILL.md references; then
    fail "no doc/performance in the skill text"
else
    pass "no doc/performance in the skill text"
fi

if [[ "$failed" == 0 ]]; then echo "Skill test passed"; else echo "Skill test failed"; fi
exit "$failed"
