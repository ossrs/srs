#!/bin/bash
# Global lock for the Full verification tier, so tiers run by different agents never overlap:
# their scripts and servers share fixed ports and /tmp logs.
#
#   full-tier-lock.sh [--product NAME] acquire [--wait SECONDS] [--pid PID]   prints a token
#   full-tier-lock.sh [--product NAME] release TOKEN
#   full-tier-lock.sh [--product NAME] status
#
# Acquire from the one script that runs the whole tier: the lock's owner is that process, the
# caller by default or --pid. A lock older than SRS_FULL_TIER_LOCK_TIMEOUT seconds (300), or whose
# owner has exited, is stale, and the next acquire takes it over.
#
# Each product has its own lock, so the tiers of different products run in parallel: srs by default,
# /tmp/srs-full-tier.lock, and any other, such as st for State Threads, /tmp/srs-full-tier-st.lock.

usage() {
  echo "Usage: $0 [--product NAME] acquire [--wait SECONDS] [--pid PID] | release TOKEN | status" >&2
  exit 2
}

PRODUCT=srs
if [[ "$1" == --product ]]; then
  PRODUCT="$2"
  shift 2
fi
[[ "$PRODUCT" =~ ^[a-z0-9-]+$ ]] || usage

LOCK_DIR=${SRS_FULL_TIER_LOCK_DIR:-/tmp/srs-full-tier.lock}
if [[ "$PRODUCT" != srs ]]; then
  LOCK_DIR="${LOCK_DIR%.lock}-$PRODUCT.lock"
fi
TIMEOUT=${SRS_FULL_TIER_LOCK_TIMEOUT:-300}

# Print the value of a key in the owner file.
owner_value() {
  sed -n "s/^$1=//p" "$LOCK_DIR/owner" 2>/dev/null
}

# Seconds since the lock was taken, from the owner file, or the directory while it is written.
lock_age() {
  local start
  start=$(owner_value start)
  if [[ -z "$start" ]]; then
    start=$(stat -f %m "$LOCK_DIR" 2>/dev/null || echo 0)
  fi
  echo $(( $(date +%s) - start ))
}

is_stale() {
  local pid
  [[ -d "$LOCK_DIR" ]] || return 1
  [[ $(lock_age) -gt $TIMEOUT ]] && return 0
  pid=$(owner_value pid)
  [[ -n "$pid" ]] && ! kill -0 "$pid" 2>/dev/null && return 0
  return 1
}

# Remove a stale lock. A second directory serializes the takeover, so two waiters never both
# judge the old lock stale and one removes the lock the other just took.
# Succeed when the lock is free afterwards, so the caller retries at once.
take_over_if_stale() {
  local takeover="$LOCK_DIR.takeover"
  if ! mkdir "$takeover" 2>/dev/null; then
    # A takeover that died midway leaves its directory; it only lives for a moment.
    if [[ $(( $(date +%s) - $(stat -f %m "$takeover" 2>/dev/null || date +%s) )) -gt 10 ]]; then
      rmdir "$takeover" 2>/dev/null
    fi
    return 1
  fi
  if is_stale; then
    # Tests widen the window between the check and the removal, to prove the takeover is serialized.
    sleep "${SRS_FULL_TIER_LOCK_TAKEOVER_DELAY:-0}"
    echo "full-tier-lock: take over a stale lock: $(tr '\n' ' ' < "$LOCK_DIR/owner" 2>/dev/null)age=$(lock_age)s" >&2
    rm -rf "$LOCK_DIR"
  fi
  rmdir "$takeover"
  [[ ! -d "$LOCK_DIR" ]]
}

acquire() {
  local wait=0 pid=$PPID token deadline
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --wait) wait="$2"; shift 2 ;;
      --pid) pid="$2"; shift 2 ;;
      *) usage ;;
    esac
  done

  token="$(date +%s)-$$-$RANDOM"
  deadline=$(( $(date +%s) + wait ))
  while true; do
    if mkdir "$LOCK_DIR" 2>/dev/null; then
      printf "token=%s\npid=%s\nstart=%s\ncwd=%s\n" "$token" "$pid" "$(date +%s)" "$PWD" > "$LOCK_DIR/owner.tmp"
      mv "$LOCK_DIR/owner.tmp" "$LOCK_DIR/owner"
      echo "$token"
      return 0
    fi

    take_over_if_stale && continue
    if [[ -d "$LOCK_DIR" && $(date +%s) -ge $deadline ]]; then
      echo "full-tier-lock: held by $(tr '\n' ' ' < "$LOCK_DIR/owner" 2>/dev/null)age=$(lock_age)s" >&2
      return 1
    fi
    sleep 1
  done
}

release() {
  [[ -n "$1" ]] || usage
  if [[ ! -d "$LOCK_DIR" ]]; then
    echo "full-tier-lock: not held" >&2
    return 1
  fi
  if [[ "$(owner_value token)" != "$1" ]]; then
    echo "full-tier-lock: token $1 does not own the lock: $(tr '\n' ' ' < "$LOCK_DIR/owner" 2>/dev/null)" >&2
    return 1
  fi
  rm -rf "$LOCK_DIR"
}

status() {
  if [[ ! -d "$LOCK_DIR" ]]; then
    echo "free"
    return 1
  fi
  echo "held: $(tr '\n' ' ' < "$LOCK_DIR/owner" 2>/dev/null)age=$(lock_age)s$(is_stale && echo ' stale')"
}

case "$1" in
  acquire) shift; acquire "$@" ;;
  release) shift; release "$@" ;;
  status) status ;;
  *) usage ;;
esac
