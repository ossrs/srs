#!/bin/bash
# Build ffmpeg from source with the codecs and protocols the SRS proxy E2E
# tests need — in particular WHIP (WebRTC-HTTP Ingestion Protocol), which the
# default Homebrew formulas (vanilla + homebrew-ffmpeg tap) do not enable.
#
# Modelled on the ossrs/dev-docker ubuntu20 base images:
#   https://github.com/ossrs/dev-docker/blob/ubuntu20/Dockerfile.base
#   https://github.com/ossrs/dev-docker/blob/ubuntu20/Dockerfile.base2
#   https://github.com/ossrs/dev-docker/blob/ubuntu20/Dockerfile.base3
# The Dockerfiles describe *which* libraries and configure flags to enable;
# on macOS we install those deps via Homebrew (shared libs) instead of
# rebuilding each one from tarballs. ffmpeg itself is built from source so we
# can pass --enable-libsrtp / --enable-openssl, which neither Homebrew formula
# turns on.
#
# Output:
#   ~/.local/src/ffmpeg                 (git clone)
#   ~/.local/bin/{ffmpeg,ffprobe,ffplay}
#   ~/.local/lib, ~/.local/share, ...   (ffmpeg --prefix tree)
#
# Re-running is safe: deps already installed are skipped, the git clone is
# reused (fetch + checkout), and ffmpeg is rebuilt incrementally.
set -e

FFMPEG_TAG="${FFMPEG_TAG:-n8.1.1}"
PREFIX="${PREFIX:-$HOME/.local}"
SRC_DIR="${SRC_DIR:-$HOME/.local/src/ffmpeg}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

REQUIRED_BREW_PKGS=(
  # build toolchain (Homebrew renamed pkg-config → pkgconf in 2024)
  pkgconf nasm
  # WHIP needs DTLS (openssl). ffmpeg's WHIP muxer uses ffmpeg's *internal*
  # SRTP implementation (srtp_protocol_select="rtp_protocol srtp" in configure)
  # — no external libsrtp dependency, so no extra brew package here.
  openssl@3
  # SRT
  srt
  # video codecs (matches Dockerfile.base / .base2)
  x264 x265 libvpx
  # audio codecs (fdk-aac requires --enable-nonfree below)
  fdk-aac lame opus
  # subtitle / font stack (matches Dockerfile.base3)
  freetype fontconfig harfbuzz fribidi libass
)

log() { printf '\n=== %s ===\n' "$*"; }

# --- Pre-checks ---------------------------------------------------------------

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "Error: this script targets macOS. For Linux, follow Dockerfile.base/2/3 directly." >&2
  exit 1
fi

if [[ "$(id -u)" -eq 0 ]]; then
  echo "Error: do not run as root. Homebrew refuses, and the prefix is your \$HOME." >&2
  exit 1
fi

if ! command -v brew &>/dev/null; then
  echo "Error: Homebrew not found. Install from https://brew.sh and retry." >&2
  exit 1
fi

if ! command -v git &>/dev/null; then
  echo "Error: git not found in PATH." >&2
  exit 1
fi

log "Configuration"
echo "FFmpeg tag : $FFMPEG_TAG"
echo "Prefix     : $PREFIX"
echo "Source dir : $SRC_DIR"
echo "Parallel   : $JOBS jobs"

# --- Step 1: Install Homebrew deps -------------------------------------------

log "Step 1: Installing Homebrew dependencies"
INSTALLED="$(brew list --formula 2>/dev/null || true)"
TO_INSTALL=()
for pkg in "${REQUIRED_BREW_PKGS[@]}"; do
  if echo "$INSTALLED" | grep -qx "$pkg"; then
    echo "  ok    $pkg"
  else
    echo "  miss  $pkg"
    TO_INSTALL+=("$pkg")
  fi
done

if [[ ${#TO_INSTALL[@]} -gt 0 ]]; then
  echo ""
  echo "Installing missing packages: ${TO_INSTALL[*]}"
  brew install "${TO_INSTALL[@]}"
else
  echo ""
  echo "All required Homebrew packages already installed."
fi

# --- Step 2: Clone or refresh ffmpeg source ----------------------------------

log "Step 2: Fetching ffmpeg source at $FFMPEG_TAG"
mkdir -p "$(dirname "$SRC_DIR")"
if [[ ! -d "$SRC_DIR/.git" ]]; then
  # Shallow clone of just the tag we want — full history is ~600 MB and
  # takes minutes to index; this drops to ~80 MB and seconds.
  git clone --depth 1 --branch "$FFMPEG_TAG" \
    https://github.com/FFmpeg/FFmpeg.git "$SRC_DIR"
else
  cd "$SRC_DIR"
  git fetch --depth 1 --tags --quiet origin "$FFMPEG_TAG"
fi
cd "$SRC_DIR"
git checkout --quiet "$FFMPEG_TAG"
echo "Checked out: $(git describe --tags --always)"

# --- Step 3: Configure --------------------------------------------------------

log "Step 3: Configuring ffmpeg"

# openssl@3 is keg-only in Homebrew, so its .pc files are not on the default
# PKG_CONFIG_PATH. Other deps (lame in particular) ship no .pc at all, so
# also pass --extra-cflags / --extra-ldflags pointing at the Homebrew prefix
# (works for both Apple Silicon /opt/homebrew and Intel /usr/local).
BREW_PREFIX="$(brew --prefix)"
OPENSSL_PREFIX="$(brew --prefix openssl@3)"
export PKG_CONFIG_PATH="$OPENSSL_PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
echo "PKG_CONFIG_PATH=$PKG_CONFIG_PATH"

# --enable-nonfree     : required by --enable-libfdk-aac
# --enable-gpl         : required by --enable-libx264 / --enable-libx265 / --enable-libass
# --enable-version3    : allow (L)GPLv3 components alongside GPL/nonfree
# --enable-openssl     : DTLS backend; the WHIP muxer needs this for the
#                       DTLS-SRTP handshake. ffmpeg's WHIP muxer uses ffmpeg's
#                       *internal* SRTP (libavformat/srtp.c), not external
#                       libsrtp — so no --enable-libsrtp flag exists/is needed.
# --enable-libsrt      : SRT protocol (publish/play)
./configure \
  --prefix="$PREFIX" \
  --extra-cflags="-I$BREW_PREFIX/include" \
  --extra-ldflags="-L$BREW_PREFIX/lib" \
  --enable-gpl \
  --enable-nonfree \
  --enable-version3 \
  --enable-openssl \
  --enable-libsrt \
  --enable-libx264 \
  --enable-libx265 \
  --enable-libvpx \
  --enable-libfdk-aac \
  --enable-libmp3lame \
  --enable-libopus \
  --enable-libass \
  --enable-libfreetype \
  --enable-libfontconfig \
  --enable-libharfbuzz \
  --enable-libfribidi \
  --enable-videotoolbox \
  --enable-audiotoolbox \
  --disable-debug

# --- Step 4: Build and install -----------------------------------------------

log "Step 4: Building ffmpeg ($JOBS jobs)"
make -j"$JOBS"

log "Step 5: Installing to $PREFIX"
make install

# --- Step 5: Verify -----------------------------------------------------------

log "Step 6: Verifying installed binary"
FFMPEG_BIN="$PREFIX/bin/ffmpeg"
if [[ ! -x "$FFMPEG_BIN" ]]; then
  echo "Error: $FFMPEG_BIN missing after install." >&2
  exit 1
fi

echo ""
"$FFMPEG_BIN" -version | head -2
echo ""

if "$FFMPEG_BIN" -hide_banner -muxers 2>/dev/null | grep -qw whip; then
  echo "PASS: WHIP muxer is available."
else
  echo "FAIL: WHIP muxer is NOT in the installed ffmpeg." >&2
  exit 1
fi

if "$FFMPEG_BIN" -hide_banner -protocols 2>/dev/null | grep -qw srt; then
  echo "PASS: SRT protocol is available."
else
  echo "FAIL: SRT protocol is NOT in the installed ffmpeg." >&2
  exit 1
fi

if "$FFMPEG_BIN" -hide_banner -codecs 2>/dev/null | grep -E "libx264|libx265" | grep -q libx264; then
  echo "PASS: libx264 encoder is available."
else
  echo "FAIL: libx264 encoder missing." >&2
  exit 1
fi

if "$FFMPEG_BIN" -hide_banner -codecs 2>/dev/null | grep -q libx265; then
  echo "PASS: libx265 encoder is available."
else
  echo "FAIL: libx265 encoder missing." >&2
  exit 1
fi

echo ""
log "Done"
echo "Binary:    $FFMPEG_BIN"
echo "Version:   $("$FFMPEG_BIN" -version | head -1)"
echo ""

# Warn if PATH won't pick up the new binary.
if ! echo ":$PATH:" | grep -q ":$PREFIX/bin:"; then
  echo "NOTE: $PREFIX/bin is not on your PATH."
  echo "      Add this to ~/.zshrc (or your shell rc):"
  echo "        export PATH=\"\$HOME/.local/bin:\$PATH\""
  echo "      Then 'which ffmpeg' should resolve to $FFMPEG_BIN."
else
  RESOLVED="$(command -v ffmpeg || true)"
  if [[ "$RESOLVED" == "$FFMPEG_BIN" ]]; then
    echo "PATH check: 'ffmpeg' resolves to $RESOLVED ✓"
  else
    echo "NOTE: $PREFIX/bin is on PATH but 'ffmpeg' currently resolves to:"
    echo "        $RESOLVED"
    echo "      Reorder PATH so $PREFIX/bin comes before $(dirname "$RESOLVED")."
  fi
fi
