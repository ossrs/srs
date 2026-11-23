#!/bin/bash
# A daemon started over SSH on the Windows test host outlives the SSH session that started it, as on
# Linux. Run it from macOS or Linux, after srs-windows-runner.sh build:
#
#   srs-windows-daemon-session-test.sh <host-checkout>
#
#   host-checkout   the SRS checkout on the host that holds trunk\objs\windows\srs.exe, relative to the
#                   host's home, such as projects\srs
#
# The host comes from SRS_TEST_WINDOWS_HOST. Uses ports 16500-16501 on the host by default, moved by
# SRS_TEST_PORT_BASE.
set -u
host=${SRS_TEST_WINDOWS_HOST:?set SRS_TEST_WINDOWS_HOST}
checkout=${1:?usage: srs-windows-daemon-session-test.sh <host-checkout>}
port=${SRS_TEST_PORT_BASE:-16500}
dir="~\\$checkout\\trunk\\objs\\windows-session-test"

remote() {
  ssh -o BatchMode=yes "$host" "$1" | tr -d '\r'
}

remote "New-Item -ItemType Directory -Force $dir\\objs, $dir\\conf > \$null;
  Copy-Item ~\\$checkout\\trunk\\objs\\windows\\srs.exe $dir\\objs\\srs.exe;
  Set-Content -Encoding ascii $dir\\conf\\srs.conf 'daemon on; in_docker off; disable_daemon_for_docker off;
pid ./objs/srs.pid; srs_log_tank file; srs_log_file ./objs/srs.log;
rtmp { listen $port; } http_api { enabled on; listen $((port + 1)); }'" || exit 1

probe="\$p = Get-Content objs\\srs.pid; \$a = [bool](Get-Process -Id \$p -ErrorAction SilentlyContinue);
  try { \$c = (Invoke-WebRequest -UseBasicParsing -TimeoutSec 2 http://127.0.0.1:$((port + 1))/api/v1/versions).StatusCode } catch { \$c = 0 };
  \"pid=\$p alive=\$a api=\$c\""

# Session 1 starts the daemon, sees it answer, and ends.
s1=$(remote "cd $dir; Remove-Item -ErrorAction SilentlyContinue objs\\srs.pid;
  .\\objs\\srs.exe -c conf\\srs.conf > \$null; \"launcher=\$LASTEXITCODE\"; Start-Sleep 3; $probe")
echo "session 1: $(echo $s1)"
# Session 2 finds the daemon still running.
s2=$(remote "cd $dir; $probe")
echo "session 2: $s2"

pid=$(echo "$s2" | sed -n 's/^pid=\([0-9]*\).*/\1/p')
[[ -n $pid ]] && remote "Stop-Process -Id $pid -Force -ErrorAction SilentlyContinue" >/dev/null 2>&1

if [[ $s1 == *"launcher=0"*"alive=True api=200"* && $s2 == *"alive=True api=200"* ]]; then
  echo "PASS daemon-outlives-session"
  exit 0
fi
echo "FAIL daemon-outlives-session: the daemon did not outlive the SSH session that started it"
exit 1
