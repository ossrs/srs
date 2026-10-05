#!/bin/bash
# Test full-tier-lock.sh with a private lock directory, so a real Full tier is never disturbed.
SCRIPT_DIR="$(cd -P "$(dirname "$0")" && pwd)"
LOCK="$SCRIPT_DIR/full-tier-lock.sh"
WORK=$(mktemp -d)
export SRS_FULL_TIER_LOCK_DIR="$WORK/lock"
PASS=0
FAIL=0
HOLDERS=""

cleanup() {
  [[ -n "$HOLDERS" ]] && kill $HOLDERS 2>/dev/null
  rm -rf "$WORK"
}
trap cleanup EXIT

check() {
  if [[ "$2" == "$3" ]]; then
    echo "PASS: $1"
    PASS=$((PASS + 1))
  else
    echo "FAIL: $1: got '$2', want '$3'"
    FAIL=$((FAIL + 1))
  fi
}

# Start a live process to own a lock, with its pid in $pid.
holder() {
  sleep 60 > /dev/null 2>&1 &
  pid=$!
  disown $pid
  HOLDERS="$HOLDERS $pid"
}

reset() {
  rm -rf "$SRS_FULL_TIER_LOCK_DIR" "$SRS_FULL_TIER_LOCK_DIR.takeover"
}

echo "=== acquire, status and release"
reset
holder
token=$(bash "$LOCK" acquire --pid $pid)
check "acquire a free lock" "$?" "0"
bash "$LOCK" status > /dev/null
check "status reports it held" "$?" "0"
bash "$LOCK" release "$token"
check "release with the token" "$?" "0"
bash "$LOCK" status > /dev/null
check "status reports it free" "$?" "1"

echo "=== a held lock blocks others until the wait ends"
reset
holder
token=$(bash "$LOCK" acquire --pid $pid)
t0=$(date +%s)
bash "$LOCK" acquire --wait 2 --pid $pid > /dev/null 2>&1
check "second acquire fails" "$?" "1"
check "second acquire waited about 2s" "$(( $(date +%s) - t0 >= 2 ))" "1"
bash "$LOCK" release "$token"

echo "=== a waiting acquire gets the lock once released"
reset
holder
token=$(bash "$LOCK" acquire --pid $pid)
bash "$LOCK" acquire --wait 10 --pid $pid > "$WORK/waiter" 2>/dev/null &
waiter=$!
sleep 2
bash "$LOCK" release "$token"
wait $waiter
check "waiter acquires after release" "$?" "0"
check "waiter owns the lock" "$(sed -n 's/^token=//p' "$SRS_FULL_TIER_LOCK_DIR/owner")" "$(cat "$WORK/waiter")"

echo "=== only the token owner releases"
reset
holder
token=$(bash "$LOCK" acquire --pid $pid)
bash "$LOCK" release "not-$token" 2> /dev/null
check "release with a wrong token fails" "$?" "1"
bash "$LOCK" status > /dev/null
check "lock still held" "$?" "0"
bash "$LOCK" release "$token"

echo "=== a lock whose owner exited is taken over at once"
reset
sleep 0 &
dead=$!
wait $dead
bash "$LOCK" acquire --pid $dead > /dev/null
t0=$(date +%s)
holder
bash "$LOCK" acquire --pid $pid > /dev/null 2>&1
check "acquire over a dead owner" "$?" "0"
check "took over without waiting" "$(( $(date +%s) - t0 <= 1 ))" "1"

echo "=== a lock older than the timeout is taken over"
reset
holder
SRS_FULL_TIER_LOCK_TIMEOUT=2 bash "$LOCK" acquire --pid $pid > /dev/null
SRS_FULL_TIER_LOCK_TIMEOUT=2 bash "$LOCK" acquire --pid $pid > /dev/null 2>&1
check "acquire before the timeout fails" "$?" "1"
sleep 3
SRS_FULL_TIER_LOCK_TIMEOUT=2 bash "$LOCK" acquire --pid $pid > /dev/null 2>&1
check "acquire after the timeout" "$?" "0"

echo "=== racing acquires, one winner"
reset
holder
racers=""
for i in 1 2 3 4 5 6 7 8; do
  (bash "$LOCK" acquire --pid $pid > /dev/null 2>&1 && echo won >> "$WORK/race") &
  racers="$racers $!"
done
wait $racers
check "exactly one of 8 acquires wins" "$(grep -c won "$WORK/race")" "1"

echo "=== racing takeovers of a dead lock, one winner"
reset
bash "$LOCK" acquire --pid $dead > /dev/null
holder
rm -f "$WORK/race"
racers=""
for i in 1 2 3 4 5 6 7 8; do
  # Starts 0.3s apart with a 2s takeover put a late racer's stale check before an early racer's
  # acquire, and its removal after it, unless the takeover is serialized.
  (sleep $((i * 3 / 10)).$((i * 3 % 10)); SRS_FULL_TIER_LOCK_TAKEOVER_DELAY=2 bash "$LOCK" acquire --wait 10 --pid $pid > /dev/null 2>&1 && echo won >> "$WORK/race") &
  racers="$racers $!"
done
wait $racers
check "exactly one of 8 takeovers wins" "$(grep -c won "$WORK/race")" "1"

echo "=== each product has its own lock"
reset
rm -rf "$SRS_FULL_TIER_LOCK_DIR-st.lock"
holder
token=$(bash "$LOCK" acquire --pid $pid)
st=$(bash "$LOCK" --product st acquire --pid $pid)
check "st acquires while srs is held" "$?" "0"
bash "$LOCK" --product st acquire --pid $pid > /dev/null 2>&1
check "a second st acquire fails" "$?" "1"
check "st status names its own lock" "$(bash "$LOCK" --product st status | sed -n 's/.*token=\([^ ]*\).*/\1/p')" "$st"
bash "$LOCK" --product st release "$token" 2> /dev/null
check "the srs token does not release st" "$?" "1"
bash "$LOCK" --product st release "$st"
check "release st" "$?" "0"
bash "$LOCK" status > /dev/null
check "srs is still held" "$?" "0"
bash "$LOCK" release "$token"
bash "$LOCK" --product 'a b' status > /dev/null 2>&1
check "reject a product that is not a plain name" "$?" "2"

echo ""
echo "full-tier-lock-test: $PASS passed, $FAIL failed"
[[ $FAIL -eq 0 ]]
