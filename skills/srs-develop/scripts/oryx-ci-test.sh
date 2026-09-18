#!/bin/bash
# Runs the Oryx repository's OWN test suite -- the same Go and jest tests the
# GitHub Actions workflows invoke -- restricted to the cases that leave no
# trace on the local development environment.
#
# This is not a way to run GitHub Actions locally. The workflows only install
# Go/Node/FFmpeg and then call plain `go test` and `npm test`; this script
# calls those same commands directly.
#
# Two parts:
#   1. Unit tests (no running stack needed): platform, releases, and ui.
#      Exactly what the root `make test` target runs.
#   2. Black-box API cases from oryx/test/ that only read (see TIER_A_CASES).
#      These need the shared local stack -- start it with oryx-stack-start.sh.
#
# Deliberately NOT run here, because each one dirties the local environment:
#   - TestApi_SetupWebsiteFooter   sets the ICP footer to "TestFooter" and has
#                                 no restore defer, unlike its Title sibling.
#   - TestApi_SslUpdateCert        overwrites nginx.crt/nginx.key in
#                                 platform/containers/data/config/.
#   - TestApi_LetsEncryptUpdateCert, TestApi_TutorialsQueryBilibili
#                                 reach real external services.
#   - TestApi_UpdatePublishSecret  restores by defer, but a crash leaves the
#                                 publish secret as "TestPublish", breaking the
#                                 dashboard and every other oryx-*-test.sh.
#   - TestApi_SetupHpHLS*, TestApi_SetupHlsLowLatency*
#                                 restore by defer, but each rewrites
#                                 srs.server.conf/srs.vhost.conf via
#                                 srsGenerateConfig() and reloads SRS.
#   - scenario_test.go, camera_test.go, media_test.go, liveroom_test.go
#                                 global record/transcode toggles, MP4s on
#                                 disk, persistent Redis platform entries.
#   - openai_test.go               real, billed OpenAI calls.
# The skill's own oryx-*-test.sh scripts already cover that ground locally,
# with the cleanup these Go cases lack. Leave the rest to CI.
#
# Usage:
#   bash oryx-ci-test.sh                 # unit + black-box (needs the stack)
#   bash oryx-ci-test.sh --unit-only     # unit only (no stack needed)
#   bash oryx-ci-test.sh --blackbox-only # black-box only (needs the stack)

SCRIPT_DIR="$(cd -P "$(dirname "$0")" && pwd)"
# Walk up from SCRIPT_DIR looking for go.mod, the SRS repo root. This avoids
# brittle "../../../.." counting when the skills directory is reached via a
# symlink (which changes the symbolic vs. physical depth).
WORKSPACE="$SCRIPT_DIR"
while [[ "$WORKSPACE" != "/" && ! -f "$WORKSPACE/go.mod" ]]; do
  WORKSPACE="$(dirname "$WORKSPACE")"
done
if [[ ! -f "$WORKSPACE/go.mod" ]]; then
  echo "Error: go.mod not found walking up from: $SCRIPT_DIR" >&2
  exit 1
fi

ORYX_DIR="$WORKSPACE/oryx"
PLATFORM_DIR="$ORYX_DIR/platform"
ENV_FILE="$PLATFORM_DIR/containers/data/config/.env"
ENDPOINT="${ORYX_ENDPOINT:-http://localhost:2022}"
SRS_API="${SRS_API_ENDPOINT:-http://localhost:1985}"

RUN_UNIT=yes
RUN_BLACKBOX=yes
case "$1" in
  --unit-only) RUN_BLACKBOX=no ;;
  --blackbox-only) RUN_UNIT=no ;;
  "") ;;
  *) echo "Unknown option: $1" >&2; exit 1 ;;
esac

# Read-only black-box cases, verified one by one against the source. Keep this
# an explicit allowlist, never a prefix wildcard: a future TestApi_SrsApiXxx
# that writes something must not be swept in by accident. Read the case before
# adding it, and record here why it is safe.
TIER_A_CASES=(
  TestSystem_Empty                # no-op; what root `make test` runs
  TestSystem_Ready                # waits for readiness, reads only
  TestSystem_QueryPublishSecret   # secret/query
  TestSystem_LoginByPassword      # login, issues a token, changes nothing
  TestSystem_BootstrapQueryEnvs   # mgmt/envs
  TestSystem_BootstrapQueryInit   # mgmt/init, query form
  TestSystem_BootstrapQueryCheck  # mgmt/check
  TestSystem_BootstrapQueryVersions # mgmt/versions
  TestSystem_CandidateByEip       # WHIP offer, ?eip= candidate in the answer
  TestSystem_CandidateByHostIp    # WHIP offer, X-Real-Host candidate
  TestApi_SrsApiNoAuth            # SRS API auth boundary, reads only
  TestApi_SrsApiCorsNoOrigin      # CORS headers, reads only
  TestApi_SrsApiCorsWithOrigin    # CORS headers, reads only
)
# The WHIP cases post a canned SDP offer and never close the session, leaving a
# few RTC publishers on the local SRS until they time out. Nothing persists.

# Read-only cases that still cannot pass against this local stack, with the
# reason. Listed and printed rather than silently dropped, so the gap stays
# visible instead of turning into a quiet blind spot.
SKIPPED_CASES=(
  "TestApi_SrsApiWithAuth|asserts the SRS major version is 7 (api_test.go:591). The local stack runs SRS built from trunk/, currently 8.x, so it always fails here. Oryx CI still covers it, because the Oryx image bundles SRS 7."
)

# Plain indexed arrays only -- the default macOS /bin/bash is 3.2 and has no
# associative arrays (declare -A). STEP_STATUS[i] matches STEP_NAMES[i].
STEP_NAMES=()
STEP_STATUS=()

record_step() {
  STEP_NAMES+=("$1")
  STEP_STATUS+=("$2")
  echo "$2: $1"
  echo ""
}

echo "=== Oryx CI Test Suite (side-effect-free subset) ==="
echo "Oryx: $ORYX_DIR"
echo "Unit: $RUN_UNIT, Black-box: $RUN_BLACKBOX"
echo ""

# --- Part 1: unit tests, the root `make test` set, no stack required ---
if [[ "$RUN_UNIT" == yes ]]; then
  if ! command -v go >/dev/null 2>&1; then
    echo "FAIL: go not found in PATH." >&2
    exit 1
  fi

  echo "=== platform: go test -mod=vendor ./... ==="
  if (cd "$PLATFORM_DIR" && go test -mod=vendor ./...); then
    record_step "platform go test" PASS
  else
    record_step "platform go test" FAIL
  fi

  echo "=== releases: go test ./... ==="
  if (cd "$ORYX_DIR/releases" && go test ./...); then
    record_step "releases go test" PASS
  else
    record_step "releases go test" FAIL
  fi

  echo "=== ui: npm run lint && npm test ==="
  # CI=true makes react-scripts run jest once and exit instead of watching.
  # The ui Makefile skips lint when GITHUB_ACTION is set, so CI never lints;
  # run it here, since locally is the only place it ever runs.
  if [[ ! -d "$ORYX_DIR/ui/node_modules" ]]; then
    echo "SKIP: ui/node_modules missing, run (cd oryx/ui && npm install) first."
    record_step "ui lint + jest" SKIP
  elif (cd "$ORYX_DIR/ui" && CI=true npm run lint && CI=true npm test -- --runInBand); then
    record_step "ui lint + jest" PASS
  else
    record_step "ui lint + jest" FAIL
  fi
fi

# --- Part 2: read-only black-box cases against the shared local stack ---
if [[ "$RUN_BLACKBOX" == yes ]]; then
  if ! curl -sS -m 2 -o /dev/null "$SRS_API/api/v1/versions" 2>/dev/null || \
     ! curl -sS -m 2 -o /dev/null "$ENDPOINT/terraform/v1/mgmt/versions" 2>/dev/null; then
    echo "FAIL: local Oryx stack is not running. Start it first:" >&2
    echo "  bash $SCRIPT_DIR/oryx-stack-start.sh" >&2
    echo "Or run the unit half alone: bash $0 --unit-only" >&2
    exit 1
  fi

  # TestMain resolves ffmpeg/ffprobe with a hardcoded PATH that omits
  # ~/.local/bin (main_test.go tryOpenFile), and exits -1 if either is missing
  # -- even for cases that never touch media. Pass absolute paths instead.
  FFMPEG="${ORYX_TEST_FFMPEG:-}"
  FFPROBE="${ORYX_TEST_FFPROBE:-}"
  if [[ -z "$FFMPEG" ]]; then
    FFMPEG="$(command -v ffmpeg 2>/dev/null)"
    [[ -z "$FFMPEG" && -x "$HOME/.local/bin/ffmpeg" ]] && FFMPEG="$HOME/.local/bin/ffmpeg"
  fi
  if [[ -z "$FFPROBE" ]]; then
    FFPROBE="$(command -v ffprobe 2>/dev/null)"
    [[ -z "$FFPROBE" && -x "$HOME/.local/bin/ffprobe" ]] && FFPROBE="$HOME/.local/bin/ffprobe"
  fi
  if [[ ! -x "$FFMPEG" || ! -x "$FFPROBE" ]]; then
    echo "FAIL: ffmpeg/ffprobe not found. Set ORYX_TEST_FFMPEG/ORYX_TEST_FFPROBE," >&2
    echo "or build one with: bash $SCRIPT_DIR/setup-ffmpeg-with-whip.sh" >&2
    exit 1
  fi

  # prepareTest loads the first .env it finds, starting with oryx/test/.env,
  # and stops there. That file is a leftover from whatever ran last and can
  # hold credentials for a different instance, which would fail every
  # authenticated case. Pass the live values explicitly -- an explicit flag
  # beats the .env-derived default -- and never write the file.
  API_SECRET="${SRS_PLATFORM_SECRET:-}"
  if [[ -z "$API_SECRET" ]] && command -v redis-cli >/dev/null 2>&1; then
    API_SECRET=$(redis-cli hget SRS_PLATFORM_SECRET token 2>/dev/null)
  fi
  PASSWORD="${MGMT_PASSWORD:-}"
  if [[ -z "$PASSWORD" && -f "$ENV_FILE" ]]; then
    # godotenv quotes values, e.g. MGMT_PASSWORD="abc123" -- strip the quotes.
    PASSWORD=$(sed -n 's/^MGMT_PASSWORD="\(.*\)"$/\1/p' "$ENV_FILE")
  fi
  if [[ -z "$API_SECRET" || -z "$PASSWORD" ]]; then
    echo "FAIL: could not resolve the API secret or mgmt password." >&2
    echo "Set SRS_PLATFORM_SECRET and MGMT_PASSWORD, or initialize Oryx once." >&2
    exit 1
  fi
  # Print byte-lengths only, never the values themselves.
  echo "Credentials resolved: secret=${#API_SECRET}B, password=${#PASSWORD}B"

  for entry in "${SKIPPED_CASES[@]}"; do
    echo "SKIP: ${entry%%|*} -- ${entry#*|}"
  done
  echo ""

  RUN_REGEX="^($(IFS='|'; echo "${TIER_A_CASES[*]}"))\$"
  echo "=== test: ${#TIER_A_CASES[@]} read-only cases ==="
  if (cd "$ORYX_DIR/test" && go test -mod=vendor -v \
      -api-secret="$API_SECRET" -system-password="$PASSWORD" \
      -srs-ffmpeg="$FFMPEG" -srs-ffprobe="$FFPROBE" \
      -endpoint="$ENDPOINT" -test.timeout=5m -test.run "$RUN_REGEX" .); then
    record_step "black-box read-only cases" PASS
  else
    record_step "black-box read-only cases" FAIL
  fi
fi

# --- Summary ---
echo "=== Summary ==="
FAILED=0
for i in "${!STEP_NAMES[@]}"; do
  echo "  ${STEP_STATUS[$i]}: ${STEP_NAMES[$i]}"
  [[ "${STEP_STATUS[$i]}" == FAIL ]] && FAILED=$((FAILED + 1))
done
echo ""

if [[ "$FAILED" -gt 0 ]]; then
  echo "=== Oryx CI Test Suite FAILED ($FAILED step(s)) ==="
  exit 1
fi
echo "=== Oryx CI Test Suite PASSED ==="
