#!/bin/bash
# Run one step of the SRS native Windows build and tests on the Windows host, in an SRS checkout, and
# print one line per step:
#   RESULT windows-x64 <step> PASS|FAIL|SKIP <detail> (<seconds>s)
# then RESULT windows-x64 all with the overall result.
#
#   srs-windows-runner.sh [--alone] <step> [pattern]
#
# Run it from the root of the SRS checkout, in Git Bash with the MSVC x64 environment loaded. From
# macOS or Linux, windows-host-run.sh does both on the host:
#   windows-host-run.sh [--wip] ~/projects/srs-windows srs-windows-runner.sh <step> [pattern]
#
# The steps, in order; each step runs the steps before it, and stops at the first failure. With
# --alone, only the given step runs, for a caller that ran the steps before it, such as the Full tier:
#   deps    builds ST and the third-party libraries and runs a link check per library
#   build   builds srs.exe, counts the files of each module that compiled, and starts srs.exe once
#           to check it answers on RTMP, HTTP and the API, at the ports from SRS_TEST_PORT_BASE
#   utest   builds and runs srs_utest.exe; the optional pattern goes to --gtest_filter, such as
#           'WindowsPlatformTest.*:PlatformTest.*', to run only those tests
#   integ   runs cmd/integ against srs.exe; the optional pattern goes to go test -run, such as
#           'TestRtmp_|TestHttpFlv_'
#   obs     runs the OBS cases against srs.exe, with OBS on the host
#   full    runs the Full tier on the host, which runs the steps before it itself, so they do not
#           run first
#   package builds srs.exe for a release, without the test options, and zips it as
#           SRS-Windows-x86_64-<version>.zip; only deps and build run before it, and the optional
#           version names the zip, by default the one srs.exe prints
#   vs      generates the Visual Studio projects in trunk/objs/windows-vs, and the solution
#           .vs/srs.slnx that opens them, to build, run and debug srs.exe and srs_utest.exe in the
#           IDE; only deps runs before it
# A step that is not built yet, or obs when no case ran, prints SKIP and counts as passed. FFmpeg
# builds under the MSYS2 in SRS_MSYS2_ROOT, by default /c/msys64.
#
# It runs only on native Windows, where uname -s is MINGW64* or MSYS* in Git Bash; anywhere else,
# WSL included, it refuses to start.
#
# Exit 0 when every step passed or is not built yet, 1 when a step failed, 2 when refused because
# this is not native Windows, and 3 for bad arguments or when not run in an SRS checkout.

usage() {
  echo "Usage: $0 [--alone] deps|build|utest [filter]|integ [pattern]|obs|full|package [version]|vs"
  exit 3
}

# The options of auto/depends.sh for ffmpeg-4-fit with libopus, without the Unix -lpthread and -lm.
FFMPEG_OPTIONS="--disable-everything --disable-asm --disable-x86asm --disable-inline-asm"
FFMPEG_OPTIONS="$FFMPEG_OPTIONS --enable-decoder=libopus --enable-encoder=libopus --enable-libopus"
FFMPEG_OPTIONS="$FFMPEG_OPTIONS --disable-avdevice --disable-avformat --disable-swscale --disable-postproc --disable-avfilter --disable-network"
FFMPEG_OPTIONS="$FFMPEG_OPTIONS --disable-dwt --disable-error-resilience --disable-lsp --disable-lzo --disable-faan --disable-pixelutils"
FFMPEG_OPTIONS="$FFMPEG_OPTIONS --disable-hwaccels --disable-devices --disable-audiotoolbox --disable-videotoolbox --disable-cuvid"
FFMPEG_OPTIONS="$FFMPEG_OPTIONS --disable-d3d11va --disable-dxva2 --disable-ffnvcodec --disable-nvdec --disable-nvenc --disable-v4l2-m2m --disable-vaapi"
FFMPEG_OPTIONS="$FFMPEG_OPTIONS --disable-vdpau --disable-appkit --disable-coreimage --disable-avfoundation --disable-securetransport --disable-iconv"
FFMPEG_OPTIONS="$FFMPEG_OPTIONS --disable-lzma --disable-sdl2"
FFMPEG_OPTIONS="$FFMPEG_OPTIONS --enable-decoder=aac --enable-decoder=aac_fixed --enable-decoder=aac_latm --enable-encoder=aac"
FFMPEG_OPTIONS="$FFMPEG_OPTIONS --enable-decoder=mp3 --enable-dct"

# The MSYS2 half of the FFmpeg build, which step_deps starts in MSYS2's bash with the checkout as a
# Windows path. FFmpeg's configure needs MSYS2's make and shell tools, and Git Bash's environment does
# not carry over, so the MSVC environment comes from a file step_deps writes.
if [[ $1 == --msys2-ffmpeg ]]; then
  export PATH=/usr/bin:$PATH
  cd "$(cygpath -u "$2")" || exit 3
  source trunk/objs/windows/msvc-env.sh || exit 3
  export PATH="/usr/bin:$(cygpath -u -p "$SRS_WIN_PATH")"
  export PKG_CONFIG_PATH=$PWD/trunk/objs/windows/vcpkg_installed/x64-windows-static-release/lib/pkgconfig
  ROOT=$PWD
  mkdir -p trunk/objs/windows/ffmpeg-4-fit && cd trunk/objs/windows/ffmpeg-4-fit || exit 1
  "$ROOT/trunk/3rdparty/ffmpeg-4-fit/configure" --toolchain=msvc --prefix="$ROOT/trunk/objs/windows/ffmpeg" \
    --pkg-config=pkg-config --pkg-config-flags=--static $FFMPEG_OPTIONS || exit 1
  make -j${NUMBER_OF_PROCESSORS:-8} || exit 1
  make install || exit 1
  exit 0
fi

OS=$(uname -s)
case $OS in
  MINGW64*|MSYS*) ;;
  *)
    echo "SKIP windows: uname -s is $OS, not native Windows; run it on the host with windows-host-run.sh"
    exit 2
    ;;
esac

STEPS="deps build utest integ obs full"
ALONE=
if [[ $1 == --alone ]]; then
  ALONE=1
  shift
fi
TARGET=$1
PATTERN=
VERSION=
case $TARGET in
  integ|utest) [[ $# -le 2 ]] || usage; PATTERN=$2 ;;
  package) [[ $# -le 2 ]] || usage; VERSION=$2 ;;
  deps|build|obs|full|vs) [[ $# == 1 ]] || usage ;;
  *) usage ;;
esac
[[ -z $ALONE && $TARGET != full ]] || STEPS=$TARGET
[[ -n $ALONE || $TARGET != package ]] || STEPS="deps build package"
[[ -n $ALONE || $TARGET != vs ]] || STEPS="deps vs"

# The tests build srs.exe and srs_utest.exe in one tree, with the options of the Unix dev and CI
# builds, so a step never flips the auto header. A release has no utest and no RTC simulator.
CMAKE_OPTIONS="-DSRS_UTEST=ON -DSRS_SIMULATOR=ON -DSRS_GB28181=ON -DSRS_RTSP=ON"
[[ $TARGET != package ]] || CMAKE_OPTIONS="-DSRS_UTEST=OFF -DSRS_SIMULATOR=OFF -DSRS_GB28181=ON -DSRS_RTSP=ON"

if [[ ! -f trunk/src/core/srs_core.hpp ]]; then
  echo "SETUP_FAIL: $(pwd) is not an SRS checkout; run it from the checkout root"
  exit 3
fi

# ST comes first, since the Windows CMake build links it. Configuring that build installs the vcpkg
# libraries. vcpkg keeps them in trunk/objs/windows/vcpkg_installed and in its binary cache, so a later
# run only checks them. With Ninja, CMake takes the first compiler it finds in PATH, which is clang++
# when a Visual Studio with its clang tools puts them first, so the build names cl.
step_deps() {
  mkdir -p trunk/objs/windows
  build_st || return 1
  if ! cmake -S trunk/cmake/windows -B trunk/objs/windows -G Ninja -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl \
      $CMAKE_OPTIONS > trunk/objs/windows-deps.log 2>&1; then
    tail -n 40 trunk/objs/windows-deps.log
    echo "See trunk/objs/windows-deps.log"
    return 1
  fi
  build_ffmpeg || return 1
  run_deps_checks
}

# ST is built as auto/depends.sh builds it for ./configure, a static debug library with MALLOC_STACK,
# from a copy of the source in trunk/objs/windows/st-srs. st.h and libst.a go to trunk/objs/windows/st,
# where the Windows CMake build links them. The build is kept and skipped while the ST source and the
# options are the same as last time.
ST_MAKE_ARGS="win64-debug STATIC_ONLY=yes"
ST_EXTRA_CFLAGS="-DMALLOC_STACK"
build_st() {
  local stamp
  stamp="$(git rev-parse HEAD:trunk/3rdparty/st-srs) $ST_MAKE_ARGS EXTRA_CFLAGS=$ST_EXTRA_CFLAGS"
  if [[ -f trunk/objs/windows/st/libst.a &&
        $(cat trunk/objs/windows/st.stamp 2>/dev/null) == "$stamp" ]]; then
    return 0
  fi
  rm -f trunk/objs/windows/st.stamp
  # The copy goes over the last one with new file times, so make builds every file again.
  if ! { mkdir -p trunk/objs/windows/st-srs trunk/objs/windows/st &&
         cp -R trunk/3rdparty/st-srs/. trunk/objs/windows/st-srs/ &&
         env EXTRA_CFLAGS="$ST_EXTRA_CFLAGS" make -C trunk/objs/windows/st-srs $ST_MAKE_ARGS &&
         cp -f trunk/objs/windows/st-srs/obj/st.h trunk/objs/windows/st-srs/obj/libst.a trunk/objs/windows/st/
       } > trunk/objs/windows-st.log 2>&1; then
    tail -n 40 trunk/objs/windows-st.log
    echo "See trunk/objs/windows-st.log"
    return 1
  fi
  echo "$stamp" > trunk/objs/windows/st.stamp
}

# FFmpeg-fit is built with MSVC under MSYS2, with libopus from vcpkg, into trunk/objs/windows/ffmpeg.
# The build is kept and skipped while the fit source and the options are the same as last time.
MSYS2=${SRS_MSYS2_ROOT:-/c/msys64}
build_ffmpeg() {
  local stamp
  stamp="$(git rev-parse HEAD:trunk/3rdparty/ffmpeg-4-fit) $FFMPEG_OPTIONS"
  if [[ -f trunk/objs/windows/ffmpeg/lib/libavcodec.a &&
        $(cat trunk/objs/windows/ffmpeg.stamp 2>/dev/null) == "$stamp" ]]; then
    return 0
  fi
  if [[ ! -x $MSYS2/usr/bin/bash.exe ]]; then
    echo "No MSYS2 at $MSYS2, which builds FFmpeg; srs-windows-env-check.sh lists what the host needs"
    return 1
  fi
  mkdir -p trunk/objs/windows && rm -f trunk/objs/windows/ffmpeg.stamp
  {
    printf 'export INCLUDE=%q LIB=%q LIBPATH=%q\n' "$INCLUDE" "$LIB" "$LIBPATH"
    printf 'export SRS_WIN_PATH=%q\n' "$(cygpath -w -p "$PATH")"
  } > trunk/objs/windows/msvc-env.sh
  if ! "$MSYS2/usr/bin/bash.exe" "$(cygpath -w "$0")" --msys2-ffmpeg "$(cygpath -w "$PWD")" \
      > trunk/objs/windows-ffmpeg.log 2>&1; then
    tail -n 40 trunk/objs/windows-ffmpeg.log
    echo "See trunk/objs/windows-ffmpeg.log"
    return 1
  fi
  echo "$stamp" > trunk/objs/windows/ffmpeg.stamp
}

# One small program per library, in trunk/cmake/windows/deps-check, shows that it links and runs
# with MSVC. Each prints one line, shown after its name. All run, even after one fails.
DEPS_CHECKS="openssl srtp srt ffmpeg st gtest"
run_deps_checks() {
  local c output failed=0
  if ! cmake --build trunk/objs/windows --target srs-deps-check > trunk/objs/windows-deps-check.log 2>&1; then
    tail -n 40 trunk/objs/windows-deps-check.log
    echo "See trunk/objs/windows-deps-check.log"
    return 1
  fi
  for c in $DEPS_CHECKS; do
    if output=$(trunk/objs/windows/deps-check/deps-check-$c.exe 2>&1); then
      echo "CHECK $c PASS: $(echo "$output" | tail -n 1)"
    else
      echo "CHECK $c FAIL"
      echo "$output" | tail -n 20
      failed=1
    fi
    echo "$output" >> trunk/objs/windows-deps-check.log
  done
  return $failed
}

# srs.exe builds with Ninja going on after a file fails, so one run shows every file that does not
# compile yet. A BUILD line counts the files of each module that compiled, from the source list the
# CMake configure writes, and each compiler error is shown once, not the whole log.
step_build() {
  local ok=0
  cmake --build trunk/objs/windows --target srs -- -k 0 > trunk/objs/windows-build.log 2>&1 && ok=1
  build_summary trunk/objs/windows/srs-sources.txt trunk/objs/windows-build.log
  if [[ $ok == 1 ]]; then
    start_check
    return
  fi
  grep -E ': (fatal )?error ' trunk/objs/windows-build.log | awk '!seen[$0]++' | head -n 40
  echo "See trunk/objs/windows-build.log"
  return 1
}

# Once srs.exe links, it starts in trunk, as SRS runs from there: -v prints the version, the default
# config file is refused, since its daemon mode does not work on Windows, and env-only srs.exe listens
# on RTMP +0, HTTP +1 and the API +2 from SRS_TEST_PORT_BASE, answers /api/v1/versions with the same
# version, and stops. Its output goes to trunk/objs/windows-start.log.
PORT_BASE=${SRS_TEST_PORT_BASE:-15500}
start_check() {
  local version output rtmp=$PORT_BASE http=$((PORT_BASE + 1)) api=$((PORT_BASE + 2)) p pid i answer
  if ! version=$(trunk/objs/windows/srs.exe -v 2>&1) || [[ ! $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "START version FAIL: $version"
    return 1
  fi
  echo "START version PASS: $version"

  output=$(cd trunk && SRS_LOG_TANK=console ./objs/windows/srs.exe -t -c conf/srs.conf 2>&1)
  if [[ $? == 0 || $output != *"daemon is not supported"* ]]; then
    echo "START daemon FAIL: conf/srs.conf not refused for daemon mode"
    echo "$output" | tail -n 5
    return 1
  fi
  echo "START daemon PASS: conf/srs.conf refused for daemon mode"

  for p in $rtmp $http $api; do
    if port_open $p; then
      echo "START env-only FAIL: port $p in use before srs.exe starts"
      return 1
    fi
  done
  (cd trunk && exec env SRS_RTMP_LISTEN=$rtmp SRS_HTTP_SERVER_ENABLED=on SRS_HTTP_SERVER_LISTEN=$http \
    SRS_HTTP_API_ENABLED=on SRS_HTTP_API_LISTEN=$api ./objs/windows/srs.exe -e) > trunk/objs/windows-start.log 2>&1 &
  pid=$!
  for i in $(seq 1 20); do
    answer=$(curl -s -m 2 http://127.0.0.1:$api/api/v1/versions) && break
    kill -0 $pid 2>/dev/null || break
    sleep 0.5
  done
  output=
  if ! kill -0 $pid 2>/dev/null; then
    output="srs.exe quit at startup"
  elif [[ -z $answer ]]; then
    output="no answer from the API at $api"
  elif [[ $answer != *"\"version\":\"$version\""* ]]; then
    output="the API at $api answered $answer"
  elif ! port_open $rtmp; then
    output="RTMP $rtmp not listening"
  elif ! port_open $http; then
    output="HTTP $http not listening"
  fi
  stop_srs $pid
  if [[ -n $output ]]; then
    echo "START env-only FAIL: $output"
    tail -n 20 trunk/objs/windows-start.log
    echo "See trunk/objs/windows-start.log"
    return 1
  fi
  echo "START env-only PASS: RTMP $rtmp, HTTP $http, API $api version $version"
  for i in $(seq 1 10); do
    port_open $api || break
    sleep 0.5
  done
  if port_open $api; then
    echo "START stop FAIL: API $api still listening"
    return 1
  fi
  echo "START stop PASS"
}

# Read from netstat, not by connecting: Windows retries a refused connection to a local port for about
# two seconds, so a connect cannot tell a closed port from a server that does not answer.
port_open() {
  netstat -an | grep -qE "^ *TCP +[^ ]+:$1 +[^ ]+ +LISTENING"
}

stop_srs() {
  kill $1 2>/dev/null
  wait $1 2>/dev/null
}

# Prints the BUILD line for the sources listed in $1, from the Ninja log $2.
build_summary() {
  local failed
  failed=$(grep '^FAILED: ' $2 | grep -oE 'src/[a-z]+/[A-Za-z0-9_]+\.cpp\.obj' |
    sed 's/\.obj$//' | sort -u | tr '\n' ' ')
  awk -F/ -v failed="$failed" '
    BEGIN { n = split(failed, f, " "); for (i = 1; i <= n; i++) bad[f[i]] = 1 }
    NF == 3 { if (!($2 in total)) order[++m] = $2; total[$2]++; if (!($0 in bad)) ok[$2]++ }
    END { for (i = 1; i <= m; i++) s = s (i > 1 ? ", " : "") order[i] " " ok[order[i]] + 0 " of " total[order[i]];
          print "BUILD " s " compiled" }
  ' $1
}

# srs_utest.exe builds like srs.exe, with a BUILD line for the utest files, then runs in trunk, as
# ./objs/srs_utest runs on Unix. A hung test would hold the host, so it runs under a timeout. Its
# output goes to trunk/objs/windows-utest.log; the gtest summary is shown, with the failed tests.
UTEST_TIMEOUT=1800
step_utest() {
  local ok=0 code
  cmake --build trunk/objs/windows --target srs_utest -- -k 0 > trunk/objs/windows-utest-build.log 2>&1 && ok=1
  build_summary trunk/objs/windows/utest-sources.txt trunk/objs/windows-utest-build.log
  if [[ $ok != 1 ]]; then
    grep -E ': (fatal )?error ' trunk/objs/windows-utest-build.log | awk '!seen[$0]++' | head -n 40
    echo "See trunk/objs/windows-utest-build.log"
    return 1
  fi
  (cd trunk && timeout -k 10 $UTEST_TIMEOUT ./objs/windows/srs_utest.exe ${PATTERN:+"--gtest_filter=$PATTERN"}) \
    > trunk/objs/windows-utest.log 2>&1
  code=$?
  if ! grep -q '^\[==========\] .* ran\.' trunk/objs/windows-utest.log; then
    echo "UTEST srs_utest.exe exited $code with no summary"
    tail -n 20 trunk/objs/windows-utest.log
    echo "See trunk/objs/windows-utest.log"
    return 1
  fi
  grep -E '^\[(==========|  PASSED  |  FAILED  )\] ' trunk/objs/windows-utest.log | awk '!seen[$0]++' | head -n 60
  [[ $code == 0 ]] && return 0
  echo "See trunk/objs/windows-utest.log"
  return 1
}

# A step that is not built yet sets NOT_YET to say so.

# cmd/integ makes its test media with the srs-integ CLI, then runs its cases against srs.exe, its
# default binary on Windows. ST's timers tick every 15.6 ms on Windows, so the class limits are scaled
# by 4, as in CI. A pattern runs as one go test; without one, the main and the timer cases run one
# after the other, as in CI. The output goes to trunk/objs/windows-integ.log; the media line and the
# summary are shown, with the failing tests when a case fails.
INTEG_ARGS="-integ.limit-scale=4"
step_integ() {
  local ok=0
  {
    go -C cmd/integ build -o ../../trunk/objs/windows/srs-integ.exe . &&
      trunk/objs/windows/srs-integ.exe prepare &&
      if [[ -n $PATTERN ]]; then
        go -C cmd/integ/cases test -count=1 -run "$PATTERN" -parallel 24 -args $INTEG_ARGS
      else
        make integ-main INTEG_ARGS="$INTEG_ARGS" && make integ-timer INTEG_ARGS="$INTEG_ARGS"
      fi
  } > trunk/objs/windows-integ.log 2>&1 && ok=1
  grep -E '^(test media in |unavailable, |srs-integ |  (failed|skipped|known bug) )' trunk/objs/windows-integ.log
  [[ $ok == 1 ]] && return 0
  if grep -q '^--- FAIL: ' trunk/objs/windows-integ.log; then
    grep '^--- FAIL: ' trunk/objs/windows-integ.log | head -n 40
  else
    tail -n 40 trunk/objs/windows-integ.log
  fi
  echo "See trunk/objs/windows-integ.log"
  return 1
}

# The OBS cases run through obs-windows-test.sh against the srs.exe the build step made, each with its
# own OBS and srs.exe. The output goes to trunk/objs/windows-obs.log; the result lines are shown. When
# no case ran, such as on a host without OBS, the step is a skip, not a pass.
step_obs() {
  local ok=0
  bash skills/srs-develop/scripts/obs-windows-test.sh --host-cases > trunk/objs/windows-obs.log 2>&1 && ok=1
  grep -E '^(PASS|KNOWN|FAIL|SKIP|LANE) ' trunk/objs/windows-obs.log
  if [[ $ok != 1 ]]; then
    echo "See trunk/objs/windows-obs.log"
    return 1
  fi
  grep -q '^PASS ' trunk/objs/windows-obs.log ||
    NOT_YET="no OBS case ran: $(sed -n 's/^SKIP //p' trunk/objs/windows-obs.log | head -n 1)"
}
step_full() {
  bash skills/srs-develop/scripts/srs-full-tier.sh
}

# The package step stages srs.exe with the files make install takes on Unix, the config files with
# the certificate and the http root pages, in trunk/objs/windows/package, and zips it there with the
# tar of Windows, which writes a zip for a .zip name. srs.exe must be the release build, need no DLL
# that Windows lacks, since the zip has none, and check conf/console.conf from the staged directory.
step_package() {
  local version=$VERSION name dir=trunk/objs/windows/package system dlls dll missing= output
  if ! grep -qE '^SRS_UTEST:[A-Z]+=OFF$' trunk/objs/windows/CMakeCache.txt 2>/dev/null ||
     ! grep -qE '^SRS_SIMULATOR:[A-Z]+=OFF$' trunk/objs/windows/CMakeCache.txt; then
    echo "PACKAGE build FAIL: srs.exe is not a release build, SRS_UTEST or SRS_SIMULATOR is not OFF; run package without --alone"
    return 1
  fi
  [[ -n $version ]] || version=$(trunk/objs/windows/srs.exe -v 2>&1 | tr -d '\r')
  if [[ ! $version =~ ^[0-9][0-9A-Za-z.-]*$ ]]; then
    echo "PACKAGE version FAIL: $version"
    return 1
  fi
  name=SRS-Windows-x86_64-$version
  rm -rf trunk/objs/windows/package
  mkdir -p $dir/$name/objs/nginx/html $dir/$name/conf &&
    cp trunk/objs/windows/srs.exe $dir/$name/objs/ &&
    cp trunk/conf/*.conf trunk/conf/server.key trunk/conf/server.crt $dir/$name/conf/ &&
    cp trunk/research/index.html trunk/research/favicon.ico $dir/$name/objs/nginx/html/ &&
    cp -R trunk/research/players trunk/research/console trunk/3rdparty/signaling/www/demos $dir/$name/objs/nginx/html/ ||
    return 1

  system=$(cygpath -u "${SYSTEMROOT:-C:\\Windows}")/System32
  dlls=$(cd $dir && dumpbin -nologo -dependents $name/objs/srs.exe | tr -d '\r' | sed -n 's/^ *\([^ ]*\.[dD][lL][lL]\)$/\1/p')
  for dll in $dlls; do
    [[ -f $system/$dll ]] || missing="$missing $dll"
  done
  if [[ -z $dlls || -n $missing ]]; then
    echo "PACKAGE dlls FAIL:${missing:- none listed by dumpbin}; the zip has no DLL, so srs.exe may need only those of Windows"
    return 1
  fi
  echo "PACKAGE dlls PASS: $(echo $dlls)"

  if ! output=$(cd $dir/$name && ./objs/srs.exe -t -c conf/console.conf 2>&1); then
    echo "PACKAGE config FAIL: conf/console.conf"
    echo "$output" | tail -n 5
    return 1
  fi
  echo "PACKAGE config PASS: conf/console.conf"

  if ! (cd $dir && "$system/tar.exe" -a -c -f $name.zip $name) > trunk/objs/windows-package.log 2>&1; then
    tail -n 20 trunk/objs/windows-package.log
    echo "See trunk/objs/windows-package.log"
    return 1
  fi
  echo "PACKAGE $dir/$name.zip"
}

# The vs step configures the CMake build again in trunk/objs/windows-vs with the generator CMake picks
# by default, the newest Visual Studio, and the options of the tests. The solution configures it again
# by itself when a CMake file changes, and builds Debug by default, with ST and FFmpeg from deps.
#
# The projects hold the paths of this machine, so they stay in trunk/objs. The committed .vs/srs.slnx
# is the generated .slnx with the project paths made relative to .vs, in the form Visual Studio saves
# it in, so opening it in Visual Studio leaves it unchanged: no XML declaration, no project type, the
# id after the other attributes, a space before />, and one Build line for a project no
# configuration builds. The ids come from the target names, see trunk/cmake/windows/CMakeLists.txt,
# so the file is the same on every machine. The step writes it, and git shows a change to commit when
# the targets change.
VS_SOLUTION=.vs/srs.slnx
step_vs() {
  local sln solution
  if ! cmake -S trunk/cmake/windows -B trunk/objs/windows-vs -A x64 $CMAKE_OPTIONS > trunk/objs/windows-vs.log 2>&1; then
    tail -n 40 trunk/objs/windows-vs.log
    echo "See trunk/objs/windows-vs.log"
    return 1
  fi
  sln=$(ls trunk/objs/windows-vs/srs.sln* 2>/dev/null | head -n 1)
  if [[ -z $sln ]]; then
    echo "VS solution FAIL: no trunk/objs/windows-vs/srs.sln; see trunk/objs/windows-vs.log"
    return 1
  fi
  echo "VS $(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' trunk/objs/windows-vs/CMakeCache.txt): $(cygpath -w "$PWD/$sln")"
  if [[ $sln != *.slnx ]]; then
    echo "VS $VS_SOLUTION not written: the solution is not an .slnx, which Visual Studio 2022 17.13 or later makes; open $sln"
    return 0
  fi
  solution=$(tr -d '\r' < "$sln" | sed -E -e 's/^<\?xml .*\?>$//' -e 's/ Type="[^"]*"//' \
    -e 's/( Id="[^"]*")( DefaultStartup="true")/\2\1/' \
    -e 's#(<Project Path|<BuildDependency Project)="#\1="../trunk/objs/windows-vs/#g' \
    -e 's#"/>#" />#g' -e 's#<Build Solution="[^"]*" Project="false" />#<Build Project="false" />#' | uniq)
  mkdir -p "$(dirname $VS_SOLUTION)"
  if [[ -f $VS_SOLUTION && $(cat $VS_SOLUTION) == "$solution" ]]; then
    echo "VS $VS_SOLUTION is up to date: $(cygpath -w "$PWD/$VS_SOLUTION")"
    return 0
  fi
  echo "$solution" > $VS_SOLUTION
  echo "VS $VS_SOLUTION written, commit it if git shows a change: $(cygpath -w "$PWD/$VS_SOLUTION")"
}

START=$(date +%s)
for s in $STEPS; do
  NOT_YET=
  begin=$(date +%s)
  if ! step_$s; then
    echo "RESULT windows-x64 $s FAIL ($(( $(date +%s) - begin ))s)"
    echo "RESULT windows-x64 all FAIL at $s ($(( $(date +%s) - START ))s)"
    exit 1
  fi
  if [[ -n $NOT_YET ]]; then
    echo "RESULT windows-x64 $s SKIP $NOT_YET ($(( $(date +%s) - begin ))s)"
  else
    echo "RESULT windows-x64 $s PASS ($(( $(date +%s) - begin ))s)"
  fi
  [[ $s == "$TARGET" ]] && break
done
echo "RESULT windows-x64 all PASS through $TARGET ($(( $(date +%s) - START ))s)"
