#!/bin/bash
# Native Windows startup regression; run after building trunk/objs/windows/srs.exe.
set -u

case $(uname -s) in
  MINGW*|MSYS*) ;;
  *) echo 'SKIP: native Windows required'; exit 0 ;;
esac

root=$PWD
dir="$root/trunk/objs/windows-daemon-test/release path"
port=${SRS_TEST_PORT_BASE:-15600}
mkdir -p "$dir/objs" "$dir/conf" "$dir/run"
cp trunk/objs/windows/srs.exe "$dir/objs/srs.exe" || exit 1
failed=0
passed=0

stop_server() {
  local pid
  if [[ -s "$dir/objs/srs.pid" ]]; then
    pid=$(tr -d '\r\n' < "$dir/objs/srs.pid")
    case $pid in
      ''|*[!0-9]*) return ;;
    esac
    MSYS_NO_PATHCONV=1 taskkill.exe /PID "$pid" /T /F >/dev/null 2>&1 || true
  fi
}
trap stop_server EXIT

write_config() {
  cat > "$dir/conf/srs.conf" <<EOF
daemon on;
in_docker off;
disable_daemon_for_docker off;
pid ./objs/srs.pid;
srs_log_tank file;
srs_log_file ./objs/srs.log;
rtmp { listen $port; }
http_api { enabled on; listen $((port + 1)); }
EOF
}

check_start() {
  local name=$1 cwd=$2
  shift 2
  local status="$dir/$name.status" ready=0 deadline=$((SECONDS + 10))
  : > "$status"
  : > "$dir/objs/srs.pid"
  (cd "$cwd" && "$@" > "$dir/$name.log" 2>&1; echo $? > "$status") &
  while ((SECONDS < deadline)); do
    if [[ -s $status ]] && curl -fs --max-time 1 "http://127.0.0.1:$((port + 1))/api/v1/versions" >/dev/null 2>&1; then
      ready=1
      break
    fi
    [[ -s $status && $(cat "$status") != 0 ]] && break
    sleep 0.1
  done
  if [[ $ready == 1 && -s $status && $(cat "$status") == 0 && -s "$dir/objs/srs.pid" ]]; then
    echo "PASS $name: launcher exited, background server answers, pid file in release root"
    passed=$((passed + 1))
  else
    echo "FAIL $name: ready=$ready launcher=$(cat "$status")"
    tail -n 5 "$dir/$name.log"
    failed=$((failed + 1))
  fi
  stop_server
}

write_config
check_start default-from-objs "$dir/objs" "$dir/objs/srs.exe"
check_start default-from-elsewhere "$root" "$dir/objs/srs.exe"
check_start explicit-config "$dir" "$dir/objs/srs.exe" -c conf/srs.conf

# The child must resolve -c from the original cwd before applying work_dir again.
printf 'work_dir ../;\n' >> "$dir/conf/srs.conf"
check_start work-dir "$dir/run" "$dir/objs/srs.exe" -c ../conf/srs.conf
write_config
check_start env-only "$dir" env SRS_DAEMON=on SRS_PID=./objs/srs.pid \
  SRS_RTMP_LISTEN="$port" SRS_HTTP_API_ENABLED=on SRS_HTTP_API_LISTEN="$((port + 1))" \
  SRS_IN_DOCKER=off SRS_DISABLE_DAEMON_FOR_DOCKER=off "$dir/objs/srs.exe" -e

: > "$dir/objs/srs.pid"
(cd "$dir" && timeout 2s env SRS_DAEMON=off ./objs/srs.exe -c conf/srs.conf) > "$dir/foreground.log" 2>&1
status=$?
if [[ $status == 124 && -s "$dir/objs/srs.pid" ]]; then
  echo 'PASS foreground: SRS_DAEMON=off keeps launcher running'
  passed=$((passed + 1))
else
  echo "FAIL foreground: exit=$status"
  failed=$((failed + 1))
fi
stop_server

if (cd "$dir" && ./objs/srs.exe -t -c conf/srs.conf) > "$dir/check.log" 2>&1; then
  echo 'PASS config-check: daemon on accepted without starting a server'
  passed=$((passed + 1))
else
  echo 'FAIL config-check: daemon on rejected'
  failed=$((failed + 1))
fi

sed 's#pid ./objs/srs.pid;#pid ./missing/srs.pid;#' "$dir/conf/srs.conf" > "$dir/conf/bad-pid.conf"
(cd "$dir" && timeout 15s ./objs/srs.exe -c conf/bad-pid.conf) > "$dir/failure.log" 2>&1
status=$?
if [[ $status != 0 && $status != 124 ]]; then
  echo 'PASS startup-failure: launcher reports child initialization failure'
  passed=$((passed + 1))
else
  echo "FAIL startup-failure: exit=$status"
  failed=$((failed + 1))
fi
echo "RESULT windows-daemon passed=$passed failed=$failed"
[[ $failed == 0 ]]
