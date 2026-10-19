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
has "$round" "| Step | Change | Commit | Capacity | CPU per client | SRS CPU at 50% and 90% | Top symbols before → after | Result |"
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

# Every local report follows the template, and its JSON has results.
reports=$(ls ../../cmd/perf/out/reports/*-docker.md 2>/dev/null)
for report in $reports; do
    name=$(basename "$report" .md)
    for section in "## Setup" "## Caveats" "## Method" "## Results" "## Bottlenecks" \
        "## Syscalls" "## Re-run and compare"; do
        has "$report" "$section"
    done
    if grep -qE '<(version|YYYY-MM-DD|commit|scenario|gap|noise|kernel)>' "$report"; then
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
    if grep -qE '\(([0-9]{4}-[0-9]{2}-[0-9]{2}-v[^)]*-docker)\.md\)' <<< "$after"; then
        pass "$name: links its baseline"
    else
        fail "$name: links its baseline"
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

if [[ "$failed" == 0 ]]; then echo "Skill test passed"; else echo "Skill test failed"; fi
exit "$failed"
