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
# Every relative link in file $1 resolves from the file's own folder.
links_exist() {
    local dir link
    dir=$(dirname "$1")
    [[ -f "$1" ]] || { fail "$1 exists"; return; }
    for link in $(grep -oE '\]\([^)# ]+' "$1" | sed 's/^](//' | grep -v '^[a-z]*:' | sort -u); do
        if [[ -e "$dir/$link" ]]; then pass "$1 link $link"; else fail "$1 link $link"; fi
    done
}
# The text $3 is in SKILL.md's workflow $1, reported as $2.
section() { sed -n "/^### $1/,/^### /p" SKILL.md; }
in_section() {
    if grep -qF -- "$3" <<< "$(section "$1")"; then pass "$2: $3"; else fail "$2: $3"; fi
}
cpp=references/cpp-server.md
go=references/go-server.md
# The text of section $2 (a ## heading) of the target reference $1.
ref_section() { sed -n "/^## $2\$/,/^## /p" "$1"; }

has SKILL.md "name: internal-perf-for-srs"
for workflow in "### Start a Round" "### Build the Tools" "### Run the Load and Profile" \
    "### Rank Bottlenecks" "### Fix the Top Bottleneck" "### Check" "### Commit" \
    "### Write the Report" "### Review"; do
    has SKILL.md "$workflow"
done
for link in $(grep -oE '\]\((references|scripts)/[^)#]+' SKILL.md | sed 's/^](//' | sort -u); do
    if [[ -f "$link" ]]; then pass "link $link"; else fail "link $link"; fi
done
# SKILL.md routes each target to its own reference, whose links resolve.
has SKILL.md "## Target Router"
for ref in "](references/cpp-server.md)" "](references/go-server.md)"; do
    has SKILL.md "$ref"
done
links_exist "$cpp"
links_exist "$go"
if grep -qE '^## (Proxy|Verified Proxy Scenarios|Scenarios)$' SKILL.md; then fail "SKILL.md: no target section"; else pass "SKILL.md: no target section"; fi
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
# Every scenario in the registry has its SRS env, is listed in the skill, and
# the smoke and the run accept it.
for s in $(sed -nE 's/^	"([a-z0-9-]+)": +\{Name:.*/\1/p' ../../cmd/perf/main.go); do
    if [[ -f "../../cmd/perf/env/$s.env" ]]; then pass "env $s"; else fail "env $s"; fi
    has "$cpp" "\`$s\`"
    has scripts/perf-smoke.sh "$s"
    has scripts/perf-run.sh "$s"
done
# With no scenario, the smoke runs every scenario of the registry, and only those.
registry=$(sed -nE 's/^	"([a-z0-9-]+)": +\{Name:.*/\1/p' ../../cmd/perf/main.go | sort | tr '\n' ' ')
listed=$(sed -nE 's/^scenarios=\((.*)\)$/\1/p' scripts/perf-smoke.sh | tr ' ' '\n' | sort | tr '\n' ' ')
if [[ -n "$registry" && "$registry" == "$listed" ]]; then pass "perf-smoke.sh lists the registry"; else fail "perf-smoke.sh lists the registry: [$listed] != [$registry]"; fi
has scripts/perf-smoke.sh 'list=("${scenarios[@]}")'
has scripts/perf-smoke.sh 'for s in "${list[@]}"; do'
has scripts/perf-smoke.sh 'bash "$0" "$s"'
has SKILL.md 'With no `SCENARIO`, `make perf-smoke` smokes every scenario'
# A stream smoke fails when no player got video, which only lag shows; the
# skill says which bridges SRS has and why whip-flv turns off guess_has_av.
has scripts/perf-smoke.sh "([.steps[0].samples[].lag_millis] | max) > 0"
for text in "**Cross-protocol scenarios:**" "SRS has no other bridge" '`guess_has_av` off' "two sender reports" "sender reports on the pace's clock, none from pion"; do
    has "$cpp" "$text"
done
# The hand-over: the C++ reference says how to start a round on every protocol; the round template
# has a row for every scenario, so a round on all protocols starts from the whole registry.
scenarios=$(sed -n '/^## Scenarios/,/^## /p' references/round-template.md)
for s in $(sed -nE 's/^	"([a-z0-9-]+)": +\{Name:.*/\1/p' ../../cmd/perf/main.go); do
    if grep -qF "| \`$s\` |" <<< "$scenarios"; then pass "round template scenario $s"; else fail "round template scenario $s"; fi
done
start=$(ref_section "$cpp" "Start a Round")
for text in "**All protocols:**" "registry in \`cmd/perf/main.go\`" "-cap 1000" "one chain per code path"; do
    if grep -qF -- "$text" <<< "$start"; then pass "Start a Round: $text"; else fail "Start a Round: $text"; fi
done
has references/round-template.md "measured with the \`delays\` scenario"
# The run refuses to start while another server listens on any scenario port.
ports=$(grep -m1 -E '^for port in ' scripts/perf-run.sh)
for port in $(sed -nE 's/^SRS_[A-Z_]*LISTEN=([0-9]+)$/\1/p' ../../cmd/perf/env/*.env | sort -u); do
    if grep -qw -- "$port" <<< "$ports"; then pass "perf-run.sh checks port $port"; else fail "perf-run.sh checks port $port"; fi
done
# SRT and WebRTC media listen on UDP, so their ports are checked as UDP ones.
for port in $(sed -nE 's/^SRS_(SRT|RTC)_SERVER_LISTEN=([0-9]+)$/\2/p' ../../cmd/perf/env/*.env | sort -u); do
    if grep -qF -- "$port/udp" <<< "$ports"; then pass "perf-run.sh checks UDP port $port"; else fail "perf-run.sh checks UDP port $port"; fi
done
# The delay probes run a forward target and an edge SRS beside the origin, in its
# network namespace, each from its own env; perf-down removes them, and the run
# measures only the delays, with no ramp, holds or profiles.
has scripts/perf-up.sh '"$PWD/cmd/perf/env/$scenario-$role.env"'
has scripts/perf-up.sh '--network "container:$owner"'
has scripts/perf-down.sh '"$name-forward" "$name-edge"'
# -target proxy: the Go proxy from the image on its own core, its one to four
# origins behind it in its network namespace; perf-down removes them all.
has ../../cmd/perf/Dockerfile "COPY --from=proxy /out/srs-proxy /srs/bin/srs-proxy"
has scripts/perf-up.sh '--env-file "$PWD/cmd/perf/env/proxy/proxy.env"'
has scripts/perf-up.sh '--env-file "$PWD/cmd/perf/env/proxy/$file"'
has scripts/perf-up.sh '[[ "$origins" =~ ^[1-4]$ ]]'
has scripts/perf-down.sh '"$name-origin1" "$name-origin2" "$name-origin3" "$name-origin4" "$name-proxy"'
has ../../cmd/perf/env/proxy/proxy.env "GOMAXPROCS=1"
has ../../cmd/perf/env/proxy/proxy.env "GO_PPROF=127.0.0.1:"
has ../../cmd/perf/env/proxy/origin.env "SRS_HEARTBEAT_DEVICE_ID=origin1"
for i in 2 3 4; do
    has "../../cmd/perf/env/proxy/origin$i.env" "SRS_HEARTBEAT_DEVICE_ID=origin$i"
done
# Each origin listens on its own ports, so four fit in one network namespace.
for key in SRS_RTMP_LISTEN SRS_HTTP_SERVER_LISTEN SRS_HTTP_API_LISTEN SRS_RTC_SERVER_LISTEN SRS_SRT_SERVER_LISTEN; do
    ports=$(cat ../../cmd/perf/env/proxy/origin.env ../../cmd/perf/env/proxy/origin[234].env 2>/dev/null | sed -n "s/^$key=//p" | sort -u | wc -l | tr -d ' ')
    if [[ "$ports" == 4 ]]; then pass "four origins: $key distinct"; else fail "four origins: $key distinct ($ports)"; fi
done
# The load tool reads each origin's streams from its HTTP API, origin i's 10
# ports above origin i-1's, to place one stream on each origin; the smoke
# checks every origin's share.
api=19851
for file in origin.env origin2.env origin3.env origin4.env; do
    has "../../cmd/perf/env/proxy/$file" "SRS_HTTP_API_ENABLED=on"
    has "../../cmd/perf/env/proxy/$file" "SRS_HTTP_API_LISTEN=$api"
    api=$((api + 10))
done
has ../../cmd/perf/placement.go "const originAPIBase = 19851"
has scripts/perf-smoke.sh '.setup.shares'
has "$go" 'one stream on each origin'
# The proxy scenarios are one list, every protocol the proxy relays without
# conversion: each is in the registry and none is the delay probe; every one
# runs on three origins by default. perf-up and the smoke read it.
proxy_list=scripts/perf-proxy-scenarios.sh
proxy_listed=$(sed -nE 's/^proxy_scenarios=\((.*)\)$/\1/p' "$proxy_list" 2>/dev/null | tr ' ' '\n' | sort | tr '\n' ' ')
want="many-streams rtmp-flv rtmp-hls rtmp-pub rtmp-rtmp rtmp-ts srt-srt whip-pub whip-whep "
if [[ "$proxy_listed" == "$want" ]]; then pass "proxy scenarios"; else fail "proxy scenarios: [$proxy_listed] != [$want]"; fi
for s in $proxy_listed; do
    if [[ " $registry" == *" $s "* && "$s" != delays ]]; then pass "proxy scenario $s in the registry"; else fail "proxy scenario $s in the registry"; fi
done
origins_of() { (source "$proxy_list" && proxy_origins "$1") 2>/dev/null; }
for s in $proxy_listed; do
    if [[ "$(origins_of "$s")" == 3 ]]; then pass "proxy origins: $s on 3"; else fail "proxy origins: $s on 3"; fi
done
# SRS_PERF_ORIGINS overrides the default, in perf-up and in the smoke's check.
has scripts/perf-up.sh 'origins="${SRS_PERF_ORIGINS:-}"'
has scripts/perf-smoke.sh '${SRS_PERF_ORIGINS:-$(proxy_origins "$scenario")}'
has "$go" '`perf-up.sh -target proxy -origins <1-4>`'
has "$go" '`origin4.env`'
has scripts/perf-up.sh 'source "$(dirname "$0")/perf-proxy-scenarios.sh"'
has scripts/perf-smoke.sh 'source "$(dirname "$0")/perf-proxy-scenarios.sh"'
has scripts/perf-smoke.sh 'list=("${proxy_scenarios[@]}")'
has scripts/perf-smoke.sh 'srs-perf smoke -scenario "$scenario" -target "$target"'
has scripts/perf-smoke.sh '.setup.target == $target'
has ../../Makefile 'SRS_PERF_TARGET="$(TARGET)" bash skills/internal-perf-for-srs/scripts/perf-smoke.sh'
for text in '`make perf-smoke TARGET=proxy`' "](../scripts/perf-proxy-scenarios.sh)"; do
    has "$go" "$text"
done
for text in '`perf-up.sh -target proxy -origins <1-4>`' "](../scripts/perf-proxy-up-test.sh)" '`bin/srs-perf run -target proxy`' '`origin-bound`'; do
    has "$go" "$text"
done
# perf-run.sh -target proxy runs the proxy scenarios through the proxy and
# profiles the proxy with Go's tools in the profile holds, from the load
# container, which shares the proxy's loopback.
has scripts/perf-run.sh 'source "$(dirname "$0")/perf-proxy-scenarios.sh"'
has scripts/perf-run.sh 'SRS_PERF_TARGET="$target" SRS_PERF_SCENARIO="$scenario"'
has scripts/perf-run.sh '-target "$target"'
has scripts/perf-run.sh 'docker exec srs-perf-load bash /perf-scripts/perf-profile-go.sh "$profile"'
# perf-run.sh -origins <1-4> sets a proxy run's origin count for perf-up.sh;
# it refuses a count out of range and the SRS target before building anything.
has scripts/perf-run.sh 'SRS_PERF_ORIGINS="$origins"'
for text in 'or N with `-origins <1-4>`' "the bound that stopped the ramp" "each hold's shares"; do
    has "$go" "$text"
done
for args in "-target srs -origins 3" "-target proxy -origins 0" "-target proxy -origins 5" "-target proxy -origins x"; do
    # shellcheck disable=SC2086
    bash scripts/perf-run.sh $args >/dev/null 2>&1
    code=$?
    if [[ "$code" == 2 ]]; then pass "perf-run.sh $args: refused"; else fail "perf-run.sh $args: refused (exit $code)"; fi
done
go_profile=scripts/perf-profile-go.sh
for file in cpu.pb.gz allocs.pb.gz heap.pb.gz goroutine.txt mutex.pb.gz block.pb.gz trace.out \
    gc.json top-symbols.txt flamegraph.svg; do
    has "$go_profile" "$file"
done
# Mutex and block sampling only while the profiles record, off before the trace.
rates_on=$(grep -n 'rates?mutex=$mutex_rate&block=$block_rate' "$go_profile" | head -1 | cut -d: -f1)
cpu_line=$(grep -n 'cpu.pb.gz' "$go_profile" | head -1 | cut -d: -f1)
rates_off=$(grep -n 'rates?mutex=0&block=0' "$go_profile" | head -1 | cut -d: -f1)
trace_line=$(grep -n 'trace.out' "$go_profile" | head -1 | cut -d: -f1)
if [[ -n "$rates_on" && -n "$rates_off" && -n "$cpu_line" && -n "$trace_line" \
    && "$rates_on" -lt "$cpu_line" && "$rates_off" -lt "$trace_line" ]]; then
    pass "$go_profile: mutex and block rates on for the profiles, off before the trace"
else
    fail "$go_profile: mutex and block rates on for the profiles, off before the trace"
fi
has ../../cmd/perf/Dockerfile "COPY --from=proxy /out/pprof /usr/local/bin/pprof"
for text in '`perf-run.sh -target proxy`' "](../scripts/perf-profile-go.sh)" "](../scripts/perf-profile-go-test.sh)"; do
    has "$go" "$text"
done
has scripts/perf-run.sh '-mode probe'
has scripts/perf-smoke.sh '.delays.pause_millis.samples'
for text in "C7" "NetStream.Pause.Notify" '`delays-forward.env`' '`delays-edge.env`'; do
    has "$cpp" "$text"
done
# Lag is in publisher time, since SRS starts each player's timestamps near zero.
for text in "**Lag in publisher time:**" '`time_jitter full`' "unique payload"; do
    has SKILL.md "$text"
done
# The A/B run: the baseline SRS from a git tree, never touching the worktree, and its image removed.
for text in "](scripts/perf-ab.sh)" "](scripts/perf-ab-test.sh)" '`-baseline <rev>`' \
    '`-revert <commit>`' '`-keep`' '`srs-perf-ab:<task>-<rev>`' "ab.json" "verdict.txt"; do
    has SKILL.md "$text"
done
has "$cpp" "that commit's trunk change taken out"
has scripts/perf-ab.sh "git archive"
has scripts/perf-ab.sh "acquire --wait"
has scripts/perf-ab.sh "docker rmi"
if grep -qE 'git (checkout|stash|reset|revert)' scripts/perf-ab.sh; then
    fail "perf-ab.sh: the worktree untouched"
else
    pass "perf-ab.sh: the worktree untouched"
fi
# The proxy A/B: the baseline proxy from a git tree, each side on its own cores.
for text in '`perf-ab.sh -target proxy`' "that commit's proxy change taken out" '`perf-ab-test.sh -target proxy`'; do
    has "$go" "$text"
done
has scripts/perf-ab.sh "cmd/perf/Dockerfile.ab-proxy"
has scripts/perf-ab.sh "SRS_PERF_PROXY_IMAGE"
has scripts/perf-ab.sh "perf-proxy-scenarios.sh"
has ../../cmd/perf/Dockerfile.ab-proxy "FROM srs-perf:local"
has ../../cmd/perf/Dockerfile.ab-proxy "COPY --from=proxy /out/srs-proxy /srs/bin/srs-proxy"
# The Go server has its own reference, one line per tool and then its guide in
# sections; SKILL.md's Tools keep only the shared tools.
proxy_section=$(cat "$go" 2>/dev/null)
go_tools=$(ref_section "$go" "Tools")
tools_section=$(sed -n '/^## Tools$/,/^## /p' SKILL.md)
for text in '`perf-up.sh -target proxy -origins <1-4>`' '`make perf-smoke TARGET=proxy`' \
    '`bin/srs-perf run -target proxy`' 'one stream on each origin' '`perf-run.sh -target proxy`' '`perf-ab.sh -target proxy`' \
    "](../scripts/perf-proxy-scenarios.sh)" "](../scripts/perf-profile-go.sh)" "](../scripts/perf-profile-go-test.sh)" \
    "](../scripts/perf-proxy-up-test.sh)" '`perf-ab-test.sh -target proxy`' "-proxy-docker.md"; do
    if grep -qF -- "$text" <<< "$go_tools"; then pass "Go Tools: $text"; else fail "Go Tools: $text"; fi
done
if grep -qE -- '-target proxy|perf-profile-go|perf-proxy-|perf-build-check|perf-profile\.sh' <<< "$tools_section"; then
    fail "Tools: no target's own tool"
else
    pass "Tools: no target's own tool"
fi
if [[ -n "$go_tools" ]] && ! grep -vE '^(- \*\*|- \[|## |$)' <<< "$go_tools" | grep -q .; then
    pass "Go Tools: one line per tool"
else
    fail "Go Tools: one line per tool"
fi
# The proxy guide, moved from internal-docs into the Go server's reference, with the
# manual steps as a short fallback.
if [[ -e references/proxy.md ]]; then fail "no references/proxy.md"; else pass "no references/proxy.md"; fi
for section in "## Topology" "## Scenarios" "## Measuring" "## Profiles" "## Go profiles" "## A/B" \
    "## Manual Profiling" "## Verified Scenarios" "## Start a Round" "## Check" "## Report"; do
    if grep -qxF -- "$section" "$go"; then pass "Go server: $section"; else fail "Go server: $section"; fi
done
for section in "## Tools" "## Topology" "## Scenarios" "## Measuring" "## Profiles" "## Linux perf profiles" \
    "## A/B" "## Verified Scenarios" "## Start a Round" "## Check" "## Report"; do
    if grep -qxF -- "$section" "$cpp"; then pass "C++ server: $section"; else fail "C++ server: $section"; fi
done
# The C++ hand-over: every scenario of the registry has a verified row with its
# capacity and top bottleneck.
verified_cpp=$(ref_section "$cpp" "Verified Scenarios")
if grep -qxF -- "| Scenario | Capacity | Top bottleneck |" <<< "$verified_cpp"; then pass "verified C++ header"; else fail "verified C++ header"; fi
for s in $registry; do
    if grep -qE "^\| \`$s\` \| [^|]+ \| [^|]+ \|$" <<< "$verified_cpp"; then pass "verified C++ $s"; else fail "verified C++ $s"; fi
done
guide=$go
for text in "perf-up.sh -target proxy" "perf-run.sh -target proxy" "perf-ab.sh -target proxy" \
    "perf-profile-go.sh" "GOMAXPROCS=1" "GO_PPROF" "origin-bound" "-proxy-docker.md" "pprof -base"; do
    has "$guide" "$text"
done
for s in $proxy_listed; do
    has "$guide" "\`$s\`"
done
docs=../internal-docs-for-srs
if [[ -e "$docs/references/perf/proxy-whep.md" ]]; then fail "old proxy-whep.md removed"; else pass "old proxy-whep.md removed"; fi
if grep -rqF "proxy-whep" "$docs" SKILL.md references; then fail "no proxy-whep link"; else pass "no proxy-whep link"; fi
# The proxy guide is a skill, not a doc: internal-docs routes no doc to it, and
# srs-develop's Task Router sends proxy performance work to this skill.
if grep -rqF "internal-perf-for-srs" "$docs"; then fail "internal-docs: no route to the perf skill"; else pass "internal-docs: no route to the perf skill"; fi
if grep -E '^- \*\*Performance\*\*' ../srs-develop/SKILL.md | grep -qF "Go proxy"; then pass "srs-develop: Performance row names the Go proxy"; else fail "srs-develop: Performance row names the Go proxy"; fi
links_exist "$docs/references/proxy/features.md"
# Reading Go profiles, and the proxy target in the round and report templates.
go_read=$(ref_section "$go" "Go profiles")
for text in "flat" "cum" "allocations per packet" "goroutine" "GC share" "gc.json" "go tool trace" \
    "Scheduler latency" "Syscall" "-proxy-docker.md"; do
    if grep -qF -- "$text" <<< "$go_read"; then pass "Go profiles: $text"; else fail "Go profiles: $text"; fi
done
for text in "**Target:**" "## Proxy scenarios" "Go profiles" "origin-bound" "-target proxy"; do
    has references/round-template.md "$text"
done
round_proxy=$(sed -n '/^## Proxy scenarios/,/^## /p' references/round-template.md)
for s in $proxy_listed; do
    if grep -qF "| \`$s\` |" <<< "$round_proxy"; then pass "round template proxy scenario $s"; else fail "round template proxy scenario $s"; fi
done
for text in "| Target |" "| Proxy version |" "| Origins |" "origin CPU" "-proxy-docker.md" "Go profiles" "-target proxy"; do
    has references/report-template.md "$text"
done
in_section "Write the Report" "Write the Report" "target's Report"
in_section "Rank Bottlenecks" "Rank" "Go profiles"
# The proxy hand-over: the Go server reference lists every verified proxy scenario with its
# capacity and top bottleneck, says how to start a proxy round, how to check a
# proxy change, and names the proxy after-report.
verified_proxy=$(ref_section "$go" "Verified Scenarios")
if [[ -n "$verified_proxy" ]]; then pass "Go server: ## Verified Scenarios"; else fail "Go server: ## Verified Scenarios"; fi
# On several origins (D113): each proxy scenario's capacity on one origin and on
# three, and the bound that stopped its ramp, as perf-verify.sh names it.
proxy_header="| Scenario | Capacity on one origin | Capacity on three origins | Bound | Top bottleneck |"
if grep -qxF -- "$proxy_header" <<< "$verified_proxy"; then pass "verified proxy header"; else fail "verified proxy header"; fi
for s in $proxy_listed; do
    if grep -qE "^\| \`$s\` \| [^|]+ \| [^|]+ \| (proxy|origin|load|cap|failure)[^|]* \| [^|]+ \|$" <<< "$verified_proxy"; then
        pass "verified proxy $s"
    else
        fail "verified proxy $s"
    fi
done
for text in "on three origins" "-proxy-origins-docker.md"; do
    if grep -qF -- "$text" <<< "$verified_proxy"; then pass "Verified Proxy: $text"; else fail "Verified Proxy: $text"; fi
done
# A capacity run uses several origins, an A/B one origin per side: the Go server
# reference's Tools, its Start a Round, the guide and the templates say so.
for text in "**Several origins:**" "one origin per side" "-proxy-origins-docker.md"; do
    if grep -qF -- "$text" <<< "$proxy_section"; then pass "Proxy: $text"; else fail "Proxy: $text"; fi
done
for text in "10 vCPUs" "below the one-origin bound"; do
    if grep -qF -- "$text" <<< "$proxy_section"; then pass "Proxy: $text"; else fail "Proxy: $text"; fi
done
proxy_round_header=$(grep -m1 '^| Scenario |' <<< "$round_proxy")
for col in "| Origins |" "| Bound |"; do
    if grep -qF -- "$col" <<< "$proxy_round_header"; then pass "round template proxy column $col"; else fail "round template proxy column $col"; fi
done
for s in $proxy_listed; do
    origins_cell=$(grep -F "| \`$s\` |" <<< "$round_proxy" | awk -F' [|] ' '{print $3}')
    if [[ "$origins_cell" == "$(origins_of "$s")" ]]; then
        pass "round template proxy $s on its default origins"
    else
        fail "round template proxy $s on its default origins ($origins_cell)"
    fi
done
has references/report-template.md "1 to 4, three by default"
has references/report-template.md "-proxy-origins-docker.md"
if grep -qF "origins on vCPUs 2 and 3" references/report-template.md; then
    fail "report template: origins on vCPUs 2 to N+1"
else
    pass "report template: origins on vCPUs 2 to N+1"
fi
proxy_start=$(ref_section "$go" "Start a Round")
for text in "-proxy-round.md" "-target proxy" "## Proxy scenarios" "Verified Scenarios" "-cap 1000" \
    "-cap 6400" "three origins" "one origin per side" "perf-verify.sh" "perf-ab.sh -target proxy"; do
    if grep -qF -- "$text" <<< "$proxy_start"; then pass "Start a proxy round: $text"; else fail "Start a proxy round: $text"; fi
done
for text in "proxy-utest.sh" "proxy E2E"; do
    if grep -qF -- "$text" <<< "$(ref_section "$go" "Check")"; then pass "Go Check: $text"; else fail "Go Check: $text"; fi
done
has "$go" "-proxy-after-docker.md"
if grep -qF -- "-proxy-after-docker.md" <<< "$proxy_section"; then pass "Proxy: -proxy-after-docker.md"; else fail "Proxy: -proxy-after-docker.md"; fi
# The build check of the profiling builds: each in its own image, removed at the end.
for text in '`make perf-build-check`' "](../scripts/perf-build-check.sh)" "](../scripts/perf-build-check-test.sh)" \
    '`--gperf=on --gcp=on --gmp=on`' '`--gprof=on`' '`--valgrind=on`' "build-check.json" "cmd/perf/Dockerfile.build"; do
    has "$cpp" "$text"
done
has scripts/perf-build-check.sh "acquire --wait"
has scripts/perf-build-check.sh "docker rmi"
has references/round-template.md "build-check.json"
for file in scripts/perf-run.sh SKILL.md "$cpp" "$go"; do
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
for topic in "## Linux perf profiles" "### Flame graph" "### Symbol table" "### Syscall counts" "### Hotspot moved" "### Ranking"; do
    has "$cpp" "$topic"
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
for section in "## Goal and scope" "## Background" "## Scenarios" "## Current state" "## Decisions" \
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
check=$(ref_section "$cpp" "Check")
if grep -qF 'Full tier' <<< "$check"; then pass "Check runs the Full tier"; else fail "Check runs the Full tier"; fi
# A round file is the skill's working output, like its reports: git-ignored beside them, not in tasks/.
has SKILL.md "cmd/perf/out/reports/<YYYY-MM-DD>-v<version>-round.md"
if grep -qF 'tasks/' SKILL.md references/*.md; then fail "no round file in tasks/"; else pass "no round file in tasks/"; fi
if git check-ignore -q ../../cmd/perf/out/reports/x-round.md; then pass "round files git-ignored"; else fail "round files git-ignored"; fi
rm -rf /tmp/srs-perf-round-test

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
if grep -qE 'perf-profile(-go)?\.sh.*&[[:space:]]*$' "$run"; then
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
has "$cpp" "profile-<50|90>"
# compare gives each side's mean and sd and the 95% CI of the difference:
# paired per second for the two sides of an A/B run, unpaired otherwise.
for text in "each side's mean and sd" "95% CI" "paired per second" '`-ab-run <id>`' "unpaired" "noisier" "mean difference"; do
    has SKILL.md "$text"
done
for doc in SKILL.md "$cpp" references/report-template.md; do
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
    # A change's verdict is an A/B, paired per second, with its 95% CI; a proxy
    # after-report names the proxy's CPU, so it never reads as SRS's.
    subject=SRS
    [[ "$name" == *-proxy-after-docker ]] && subject=Proxy
    for text in "**Method:**" \
        "| Scenario | Hold clients | Method and window | $subject CPU before, mean ± sd | $subject CPU after, mean ± sd | Difference, 95% CI |" \
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

# A proxy report: its title, the proxy rows of its Setup, the origin CPU
# beside the proxy's, the proxy's Go symbols in its Bottlenecks, and a JSON of
# proxy results with a ramp for every proxy scenario.
proxy_reports=$(ls ../../cmd/perf/out/reports/*-proxy-docker.md ../../cmd/perf/out/reports/*-proxy-after-docker.md \
    ../../cmd/perf/out/reports/*-proxy-origins-docker.md 2>/dev/null)
for report in $proxy_reports; do
    name=$(basename "$report" .md)
    has "$report" "# SRS Proxy v"
    setup=$(sed -n '/^## Setup/,/^## /p' "$report")
    for text in "| Target | \`proxy\` |" "| Proxy version |" "| Origins |"; do
        if grep -qF -- "$text" <<< "$setup"; then pass "$name: Setup $text"; else fail "$name: Setup $text"; fi
    done
    results=$(sed -n '/^## Results/,/^## /p' "$report")
    if grep -qF "origin CPU" <<< "$results"; then pass "$name: origin CPU"; else fail "$name: origin CPU"; fi
    bottlenecks=$(sed -n '/^## Bottlenecks/,/^## Syscalls/p' "$report")
    if grep -qF "srsx/internal/" <<< "$bottlenecks"; then pass "$name: proxy symbols"; else fail "$name: proxy symbols"; fi
    json="../../cmd/perf/out/reports/$name.json"
    if jq -e 'all(.results[]; .setup.target == "proxy")' "$json" >/dev/null 2>&1; then
        pass "$name: JSON is the proxy's"
    else
        fail "$name: JSON is the proxy's"
    fi
    for s in $proxy_listed; do
        if jq -e --arg s "$s" 'any(.results[]; .scenario.name == $s and .scenario.parameters.mode == "ramp")' \
            "$json" >/dev/null 2>&1; then
            pass "$name: ramp of $s"
        else
            fail "$name: ramp of $s"
        fi
    done
done

# A proxy report on several origins puts each scenario's capacity on one
# origin (Phase 7's final runs, linked) beside its capacity on N, with the
# origin count and the bound that stopped the run; every result ran on more
# than one origin, and every hold shows each origin carrying a share.
for report in $(ls ../../cmd/perf/out/reports/*-proxy-origins-docker.md 2>/dev/null); do
    name=$(basename "$report" .md)
    results=$(sed -n '/^## Results/,/^## /p' "$report")
    header="| Scenario | Capacity on one origin | Origins | Capacity on N origins | Bound | Proxy CPU per client | Proxy and origin CPU at the 90% hold |"
    if grep -qF -- "$header" <<< "$results"; then pass "$name: capacity table"; else fail "$name: capacity table"; fi
    for s in $proxy_listed; do
        if grep -qE "^\| \`$s\` \| [^|]+ \| [1-4] \| [^|]+ \| (proxy|origin|load|cap|failure)[^|]* \|" <<< "$results"; then
            pass "$name: capacity row of $s"
        else
            fail "$name: capacity row of $s"
        fi
    done
    if grep -qE '\]\([0-9]{4}-[0-9]{2}-[0-9]{2}-v[^)]*-proxy-after-docker\.md\)' "$report"; then
        pass "$name: links the one-origin runs"
    else
        fail "$name: links the one-origin runs"
    fi
    json="../../cmd/perf/out/reports/$name.json"
    if jq -e '.results | length > 0 and all(.[]; (.setup.origins // 1) > 1)' "$json" >/dev/null 2>&1; then
        pass "$name: JSON on several origins"
    else
        fail "$name: JSON on several origins"
    fi
    if jq -e '[.results[] | select(.scenario.parameters.mode == "hold")]
        | length > 0 and all(.[]; (.setup.shares | length) == .setup.origins
            and all(.setup.shares[]; (.players // 0) > 0 or (.streams // 0) > 0))' "$json" >/dev/null 2>&1; then
        pass "$name: JSON holds share every origin"
    else
        fail "$name: JSON holds share every origin"
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
in_section "Run the Load and Profile" "Run the Load" "mean and sd"
in_section "Run the Load and Profile" "Run the Load" "95% CI"
in_section "Fix the Top Bottleneck" "Fix" "perf-ab.sh"
in_section "Fix the Top Bottleneck" "Fix" "180s"
in_section "Fix the Top Bottleneck" "Fix" "95% CI"
in_section "Commit" "Commit" "95% CI"
in_section "Commit" "Commit" "method"
in_section "Write the Report" "Write the Report" "flatness"
# A run is verified, and its top SRS symbols listed, before it is ranked.
for text in "](scripts/perf-verify.sh)" "](scripts/perf-verify-test.sh)"; do
    has SKILL.md "$text"
done
in_section "Rank Bottlenecks" "Rank" "perf-verify.sh"
# It notes a noise of 10% or more, which a later comparison must handle.
in_section "Rank Bottlenecks" "Rank" "noise"
# A delays run is checked too: its probes and delays, with no capacity.
has "$cpp" "delays run"
# A proxy run is checked too: its Go profiles, the proxy's and origins' CPU,
# and an origin-bound capacity; no text says the check cannot read one.
has "$go" "a proxy run, it checks the Go profiles"
has "$go" "origin-bound"
has "$go" "**\`perf-verify.sh\` on a proxy run:**"
if grep -qF -- "perf-verify.sh <run dir>" <<< "$proxy_section"; then pass "Proxy: perf-verify.sh"; else fail "Proxy: perf-verify.sh"; fi
if grep -rqF "does not read a proxy run" SKILL.md references; then
    fail "the run check reads a proxy run"
else
    pass "the run check reads a proxy run"
fi
# whip-pub at its 2,000 cap runs the Docker VM out of memory in the holds.
has "$cpp" "perf-run.sh -scenario whip-pub -cap 1000"
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
    has "$cpp" "$text"
done
if grep -rqF "doc/performance" SKILL.md references; then
    fail "no doc/performance in the skill text"
else
    pass "no doc/performance in the skill text"
fi

if [[ "$failed" == 0 ]]; then echo "Skill test passed"; else echo "Skill test failed"; fi
exit "$failed"
