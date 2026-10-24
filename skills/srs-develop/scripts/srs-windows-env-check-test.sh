#!/bin/bash
# Test srs-windows-env-check.sh without a Windows host: a fake ssh and scp on PATH stand in for the
# host and print the tool lines a stub asks for.
#
#   srs-windows-env-check-test.sh
#
# Covers the skips, the exit code for every tool found, one tool missing and a run cut short, and
# that the host script checks every tool the plan needs.
#
# Prints PASS or FAIL per check, then a summary. Exit 0 when every check passed, 1 otherwise.

SCRIPTS=$(cd "$(dirname "$0")" && pwd)
CHECK="$SCRIPTS/srs-windows-env-check.sh"
PASSED=0
FAILED=0

pass() { echo "PASS $1"; PASSED=$((PASSED + 1)); }
fail() { echo "FAIL $1"; sed 's/^/  | /' /tmp/srs-windows-env-check-test/out; FAILED=$((FAILED + 1)); }
check() { if eval "$2"; then pass "$1"; else fail "$1"; fi; }

rm -rf /tmp/srs-windows-env-check-test
mkdir -p /tmp/srs-windows-env-check-test/bin /tmp/srs-windows-env-check-test/scp

# The fake host: ssh answers the reachability check with STUB_SSH_EXIT, and the check run with
# STUB_OUTPUT and STUB_EXIT. scp keeps a copy of each file it is given.
cat > /tmp/srs-windows-env-check-test/bin/ssh <<'EOF'
#!/bin/bash
cmd=${!#}
[[ $cmd == exit ]] && exit ${STUB_SSH_EXIT:-0}
[[ -n $STUB_OUTPUT ]] && printf '%b\n' "$STUB_OUTPUT"
exit ${STUB_EXIT:-0}
EOF
cat > /tmp/srs-windows-env-check-test/bin/scp <<'EOF'
#!/bin/bash
src=
for a in "$@"; do [[ $a == -* ]] || { src=$a; break; }; done
cp "$src" /tmp/srs-windows-env-check-test/scp/
EOF
chmod +x /tmp/srs-windows-env-check-test/bin/ssh /tmp/srs-windows-env-check-test/bin/scp
export PATH=/tmp/srs-windows-env-check-test/bin:$PATH
export SRS_TEST_WINDOWS_HOST=fake-host

out() { cat /tmp/srs-windows-env-check-test/out; }
run() {
  bash "$CHECK" "$@" > /tmp/srs-windows-env-check-test/out 2>&1
  CODE=$?
}

ALL='TOOL vs: 17.14\nTOOL cl: 19.44\nTOOL cmake: 3.31\nTOOL vcpkg: 2025\nTOOL git-bash: 5.2\nTOOL make: 4.4\nTOOL go: 1.25\nTOOL ffmpeg: 7.1\nTOOL obs: 31.0\nTOOL msys2: C:\\msys64\nTOOL msys2-make: 4.4\nTOOL msys2-nasm: 2.16\nTOOL msys2-pkgconf: 2.3\nEND'

SRS_TEST_WINDOWS_HOST= run
check "no host: skip, exit 2" '[[ $CODE == 2 ]] && out | grep -q "^SKIP windows: SRS_TEST_WINDOWS_HOST is not set"'
STUB_SSH_EXIT=255 run
check "host unreachable: skip, exit 2" '[[ $CODE == 2 ]] && out | grep -q "^SKIP windows: cannot ssh to fake-host"'
run extra
check "an argument: exit 3 with usage" '[[ $CODE == 3 ]] && out | grep -q "^Usage: "'

STUB_OUTPUT=$ALL run
check "every tool found: exit 0" '[[ $CODE == 0 ]]'
check "every tool found: tool lines printed" '[[ $(out | grep -c "^TOOL ") == 13 ]]'
check "every tool found: RESULT PASS" 'out | grep -q "^RESULT windows-env PASS"'

STUB_OUTPUT=${ALL/TOOL go: 1.25/MISSING go: install Go} run
check "one tool missing: exit 1" '[[ $CODE == 1 ]]'
check "one tool missing: the tool named" 'out | grep -q "^MISSING go: "'
check "one tool missing: RESULT FAIL with the count" 'out | grep -q "^RESULT windows-env FAIL 1 missing"'

STUB_OUTPUT='TOOL vs: 17.14' STUB_EXIT=1 run
check "run cut short: exit 3" '[[ $CODE == 3 ]] && out | grep -q "^SETUP_FAIL: "'

# The host script checks every tool the plan needs.
PS1=$(ls /tmp/srs-windows-env-check-test/scp/*.ps1 2>/dev/null | head -1)
for tool in vs cl cmake vcpkg git-bash make go ffmpeg obs msys2 msys2-make msys2-nasm msys2-pkgconf; do
  check "host script checks $tool" '[[ -n $PS1 ]] && grep -q "'"'$tool'"'" "$PS1"'
done

echo "SUMMARY passed=$PASSED failed=$FAILED"
[[ $FAILED == 0 ]]
