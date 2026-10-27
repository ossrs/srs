#!/bin/bash
# Test srs-windows-runner.sh on macOS or Linux: a fake uname on PATH stands in for Git Bash on
# Windows, and the real uname checks the refusal.
#
#   srs-windows-runner-test.sh
#
# Covers the argument checks, the refusal off native Windows, the checkout check, and that each step
# runs the steps before it, in order, with the steps not built yet reported as skipped. A fake cmake
# stands in for the deps step's vcpkg install, and the vcpkg manifest is checked for the libraries.
# The steps run in a fake checkout, a git repository whose ffmpeg-4-fit configure is a fake, with a
# fake MSYS2 bash and cygpath, so the FFmpeg build is checked for its options, its cache and failures.
# A fake ST Makefile checks the ST build the same way, and the fake cmake's build makes fake link-check
# programs, a fake srs.exe, whose start a fake curl and netstat check, and a fake srs_utest.exe. A
# fake dumpbin and a fake Windows directory with tar.exe check the release package.
#
# Prints PASS or FAIL per check, then a summary. Exit 0 when every check passed, 1 otherwise.

SCRIPTS=$(cd "$(dirname "$0")" && pwd)
RUNNER="$SCRIPTS/srs-windows-runner.sh"
CHECKOUT=$(cd "$SCRIPTS/../../.." && pwd)
PATH0=$PATH
PASSED=0
FAILED=0

pass() { echo "PASS $1"; PASSED=$((PASSED + 1)); }
fail() { echo "FAIL $1"; sed 's/^/  | /' /tmp/srs-windows-runner-test/out; FAILED=$((FAILED + 1)); }
check() { if eval "$2"; then pass "$1"; else fail "$1"; fi; }

rm -rf /tmp/srs-windows-runner-test
mkdir -p /tmp/srs-windows-runner-test/bin

# The fake uname prints STUB_UNAME for -s.
cat > /tmp/srs-windows-runner-test/bin/uname <<'EOF'
#!/bin/bash
echo "$STUB_UNAME"
EOF
chmod +x /tmp/srs-windows-runner-test/bin/uname

# The fake cmake logs its arguments, one call per line. A configure into trunk/objs/windows-vs writes a
# CMakeCache.txt with the Visual Studio generator and, unless STUB_VS_NO_SLN, an srs.slnx with CRLF line
# ends like CMake's, or srs.sln with STUB_VS_SLN, and exits with STUB_CMAKE_EXIT. Any other configure writes the list of srs sources,
# a CMakeCache.txt with each -D option as a BOOL, and exits with STUB_CMAKE_EXIT. A --build of the srs target prints STUB_SRS_BUILD_OUTPUT and exits
# with STUB_SRS_BUILD_EXIT; when that is 0, it copies the fake srs.exe below into trunk/objs/windows.
# Any other --build exits with STUB_CMAKE_BUILD_EXIT and makes the six link-check programs, except the
# one named STUB_CHECK_MISSING. Each one logs its name, prints two lines, and exits 1 when its name is
# STUB_CHECK_FAIL.
cat > /tmp/srs-windows-runner-test/bin/cmake <<'EOF'
#!/bin/bash
echo "$*" >> /tmp/srs-windows-runner-test/cmake.log
echo "fake cmake output"
if [[ $1 != --build && " $* " == *" -B trunk/objs/windows-vs "* ]]; then
  mkdir -p trunk/objs/windows-vs
  echo "CMAKE_GENERATOR:INTERNAL=Visual Studio 18 2026" > trunk/objs/windows-vs/CMakeCache.txt
  if [[ -n $STUB_VS_SLN ]]; then
    : > trunk/objs/windows-vs/srs.sln
  elif [[ -z $STUB_VS_NO_SLN ]]; then
    printf '%s\r\n' '<?xml version="1.0" encoding="UTF-8"?>' '<Solution>' \
      '  <Project Path="ALL_BUILD.vcxproj" Type="8bc9" Id="f780">' \
      '    <BuildDependency Project="ZERO_CHECK.vcxproj"/>' \
      '    <BuildDependency Project="srs.vcxproj"/>' \
      '    <Build Solution="Debug|*" Project="false"/>' \
      '    <Build Solution="Release|*" Project="false"/>' \
      '  </Project>' \
      '  <Project Path="deps-check/deps-check-st.vcxproj" Type="8bc9" Id="268b"/>' \
      '  <Project Path="srs.vcxproj" Type="8bc9" Id="f8a6" DefaultStartup="true"/>' \
      '</Solution>' > trunk/objs/windows-vs/srs.slnx
  fi
  exit ${STUB_CMAKE_EXIT:-0}
fi
if [[ $1 != --build ]]; then
  mkdir -p trunk/objs/windows
  printf 'src/core/srs_a.cpp\nsrc/core/srs_b.cpp\nsrc/kernel/srs_c.cpp\nsrc/main/srs_main_server.cpp\n' \
    > trunk/objs/windows/srs-sources.txt
  printf 'src/utest/srs_utest.cpp\nsrc/utest/srs_utest_ai01.cpp\nsrc/utest/srs_utest_ai02.cpp\n' \
    > trunk/objs/windows/utest-sources.txt
  : > trunk/objs/windows/CMakeCache.txt
  for a in "$@"; do
    [[ $a == -D*=* ]] && echo "${a#-D}" | sed 's/=/:BOOL=/' >> trunk/objs/windows/CMakeCache.txt
  done
  exit ${STUB_CMAKE_EXIT:-0}
fi
if [[ " $* " == *" --target srs "* ]]; then
  printf '%b' "$STUB_SRS_BUILD_OUTPUT"
  [[ ${STUB_SRS_BUILD_EXIT:-0} == 0 ]] && cp /tmp/srs-windows-runner-test/srs.exe trunk/objs/windows/srs.exe
  exit ${STUB_SRS_BUILD_EXIT:-0}
fi
if [[ " $* " == *" --target srs_utest "* ]]; then
  printf '%b' "$STUB_UTEST_BUILD_OUTPUT"
  [[ ${STUB_UTEST_BUILD_EXIT:-0} == 0 ]] && cp /tmp/srs-windows-runner-test/srs_utest.exe trunk/objs/windows/srs_utest.exe
  exit ${STUB_UTEST_BUILD_EXIT:-0}
fi
mkdir -p trunk/objs/windows/deps-check
for c in openssl srtp srt ffmpeg st gtest; do
  [[ $c == "$STUB_CHECK_MISSING" ]] && continue
  printf '#!/bin/bash\necho %s >> /tmp/srs-windows-runner-test/checks.log\necho "fake check output"\necho "fake %s 1.0"\n[[ $STUB_CHECK_FAIL != %s ]]\n' \
    $c $c $c > trunk/objs/windows/deps-check/deps-check-$c.exe
  chmod +x trunk/objs/windows/deps-check/deps-check-$c.exe
done
exit ${STUB_CMAKE_BUILD_EXIT:-0}
EOF
chmod +x /tmp/srs-windows-runner-test/bin/cmake

# The fake srs.exe, which a passing build of the srs target copies into trunk/objs/windows. It logs
# each call with its directory and the env it reads, one line per call. With -v it prints 8.0.64 and
# exits with STUB_SRS_V_EXIT. With -t it refuses daemon, or passes with STUB_SRS_DAEMON_OK, and
# passes conf/console.conf, which sets daemon off, unless STUB_SRS_CONSOLE_FAIL. With -e it
# exits at once with STUB_SRS_START_EXIT, or writes the ports it listens on to the listening file,
# except the one named by STUB_SRS_CLOSED (rtmp, http or api), and runs until SIGTERM removes it.
cat > /tmp/srs-windows-runner-test/srs.exe <<'EOF'
#!/bin/bash
T=/tmp/srs-windows-runner-test
echo "$* DIR=$PWD TANK=$SRS_LOG_TANK RTMP=$SRS_RTMP_LISTEN HTTP=$SRS_HTTP_SERVER_ENABLED:$SRS_HTTP_SERVER_LISTEN API=$SRS_HTTP_API_ENABLED:$SRS_HTTP_API_LISTEN" >> $T/srs.log
case $1 in
  -v) echo 8.0.64; exit ${STUB_SRS_V_EXIT:-0} ;;
  -t)
    echo "fake srs output"
    if [[ -n $STUB_SRS_DAEMON_OK || ( $3 == conf/console.conf && -z $STUB_SRS_CONSOLE_FAIL ) ]]; then
      echo "config file $3 test is successful"; exit 0
    fi
    echo "invalid config code=1023(ConfigInvalid) : daemon is not supported on this OS, set daemon off or SRS_DAEMON=off in $3"
    exit 1
    ;;
esac
echo "fake srs output"
[[ -n $STUB_SRS_START_EXIT ]] && exit $STUB_SRS_START_EXIT
ports=
[[ $STUB_SRS_CLOSED == rtmp ]] || ports="$ports $SRS_RTMP_LISTEN"
[[ $STUB_SRS_CLOSED == http ]] || ports="$ports $SRS_HTTP_SERVER_LISTEN"
[[ $STUB_SRS_CLOSED == api ]] || ports="$ports $SRS_HTTP_API_LISTEN"
sleep 60 &
trap 'rm -f $T/listening; echo stopped >> $T/srs.log; kill $!; exit 0' TERM
echo "$ports" > $T/listening
wait
EOF
chmod +x /tmp/srs-windows-runner-test/srs.exe

# The fake srs_utest.exe, which a passing build of the srs_utest target copies into trunk/objs/windows.
# It logs its arguments and directory, and prints a gtest summary: all passed, two failed with
# STUB_UTEST_FAIL, or it exits 3 at once, with no summary, with STUB_UTEST_CRASH.
cat > /tmp/srs-windows-runner-test/srs_utest.exe <<'EOF'
#!/bin/bash
echo "$* DIR=$PWD" >> /tmp/srs-windows-runner-test/utest.log
echo "fake utest output"
[[ -n $STUB_UTEST_CRASH ]] && exit 3
echo "[==========] 3 tests from 2 test suites ran. (5 ms total)"
if [[ -z $STUB_UTEST_FAIL ]]; then
  echo "[  PASSED  ] 3 tests."
  exit 0
fi
echo "[  PASSED  ] 1 test."
echo "[  FAILED  ] 2 tests, listed below:"
echo "[  FAILED  ] KernelTest.A"
echo "[  FAILED  ] KernelTest.B"
echo ""
echo " 2 FAILED TESTS"
exit 1
EOF
chmod +x /tmp/srs-windows-runner-test/srs_utest.exe

# The fake timeout logs its options and runs the command, as GNU timeout in Git Bash does.
cat > /tmp/srs-windows-runner-test/bin/timeout <<'EOF'
#!/bin/bash
opts=
while [[ $1 == -* ]]; do opts="$opts $1 $2"; shift 2; done
echo "$opts $1 $2" >> /tmp/srs-windows-runner-test/timeout.log
shift
exec "$@"
EOF
chmod +x /tmp/srs-windows-runner-test/bin/timeout

# The fake curl connects to the ports in the listening file and exits 7, as curl does when it cannot
# connect, for any other. On the API port it answers /api/v1/versions with STUB_SRS_API_VERSION, by
# default 8.0.64.
cat > /tmp/srs-windows-runner-test/bin/curl <<'EOF'
#!/bin/bash
for a in "$@"; do [[ $a == http://* ]] && url=$a; done
rest=${url#http://127.0.0.1:}
port=${rest%%/*}
echo "$port /${rest#*/}" >> /tmp/srs-windows-runner-test/curl.log
grep -qw "$port" /tmp/srs-windows-runner-test/listening 2>/dev/null || exit 7
if [[ /${rest#*/} == /api/v1/versions ]]; then
  echo "{\"code\":0,\"server\":\"vid-0\",\"service\":\"x\",\"pid\":\"1\",\"data\":{\"major\":8,\"minor\":0,\"revision\":64,\"version\":\"${STUB_SRS_API_VERSION:-8.0.64}\"}}"
fi
exit 0
EOF
chmod +x /tmp/srs-windows-runner-test/bin/curl

# The fake netstat lists the ports in the listening file as Windows netstat -an does, among a client
# connection and a UDP port that are not listening.
cat > /tmp/srs-windows-runner-test/bin/netstat <<'EOF'
#!/bin/bash
echo "Active Connections"
echo "  Proto  Local Address          Foreign Address        State"
echo "  TCP    127.0.0.1:20001        127.0.0.1:20002        ESTABLISHED"
for p in $(cat /tmp/srs-windows-runner-test/listening 2>/dev/null); do
  echo "  TCP    0.0.0.0:$p          0.0.0.0:0              LISTENING"
done
echo "  UDP    0.0.0.0:20003          *:*"
EOF
chmod +x /tmp/srs-windows-runner-test/bin/netstat

# The fake go logs each call with its directory, one line per call. A build exits with
# STUB_GO_BUILD_EXIT, or writes a fake srs-integ.exe at the -o path, which logs its arguments, prints
# the media line and exits with STUB_PREPARE_EXIT. A test prints a srs-integ summary, with a failed
# case when it exits with STUB_GO_TEST_EXIT.
cat > /tmp/srs-windows-runner-test/bin/go <<'EOF'
#!/bin/bash
echo "$* DIR=$PWD" >> /tmp/srs-windows-runner-test/go.log
echo "fake go output"
args=" $* "
if [[ $args == *" build "* ]]; then
  [[ ${STUB_GO_BUILD_EXIT:-0} == 0 ]] || exit $STUB_GO_BUILD_EXIT
  while [[ $# -gt 0 && $1 != -o ]]; do shift; done
  out=$2
  [[ $args == *" -C cmd/integ "* ]] && out=cmd/integ/$2
  printf '#!/bin/bash\necho "$* DIR=$PWD" >> /tmp/srs-windows-runner-test/integ-cli.log\necho "fake prepare output"\necho "test media in cmd/integ/testdata/media: 0 made, 19 kept"\nexit ${STUB_PREPARE_EXIT:-0}\n' > "$out"
  chmod +x "$out"
  exit 0
fi
if [[ ${STUB_GO_TEST_EXIT:-0} == 0 ]]; then
  echo "srs-integ main: 3 cases, 2 passed, 0 failed, 1 skipped, 0 known bugs"
else
  echo "--- FAIL: TestRtmp_Common/Play (0.12s)"
  echo "srs-integ main: 3 cases, 1 passed, 1 failed, 1 skipped, 0 known bugs"
  echo "  failed TestRtmp_Common/Play"
fi
echo "  skipped TestHttpFlv_Common/Audio: no tool"
echo "  slowest:"
echo "    120ms Normal TestRtmp_Common/Play"
exit ${STUB_GO_TEST_EXIT:-0}
EOF
chmod +x /tmp/srs-windows-runner-test/bin/go

# The fake make logs its arguments, one call per line, and prints a srs-integ summary.
cat > /tmp/srs-windows-runner-test/bin/make <<EOF
#!/bin/bash
if [[ \$1 == integ-* ]]; then
  echo "\$*" >> /tmp/srs-windows-runner-test/make.log
  echo "srs-integ \${1#integ-}: 2 cases, 2 passed, 0 failed, 0 skipped, 0 known bugs"
  exit 0
fi
exec $(command -v make) "\$@"
EOF
chmod +x /tmp/srs-windows-runner-test/bin/make

# The fake cygpath prints the path unchanged, as if Windows and MSYS2 paths were the same.
cat > /tmp/srs-windows-runner-test/bin/cygpath <<'EOF'
#!/bin/bash
echo "${@: -1}"
EOF
chmod +x /tmp/srs-windows-runner-test/bin/cygpath

# The fake dumpbin lists the DLLs of an image as dumpbin /dependents does: two system DLLs, and
# STUB_DLL too when set.
cat > /tmp/srs-windows-runner-test/bin/dumpbin <<'EOF'
#!/bin/bash
echo "$*" >> /tmp/srs-windows-runner-test/dumpbin.log
echo "Dump of file ${@: -1}"
echo ""
echo "  Image has the following dependencies:"
echo ""
echo "    WS2_32.dll"
echo "    KERNEL32.dll"
[[ -n $STUB_DLL ]] && echo "    $STUB_DLL"
echo ""
echo "  Summary"
echo ""
echo "        1000 .data"
EOF
chmod +x /tmp/srs-windows-runner-test/bin/dumpbin

# The fake Windows directory has the two system DLLs, and a System32 tar.exe that logs its arguments
# and directory and makes the zip with zip, as Windows' tar makes one with -a for a .zip name.
mkdir -p /tmp/srs-windows-runner-test/windows/System32
touch /tmp/srs-windows-runner-test/windows/System32/WS2_32.dll /tmp/srs-windows-runner-test/windows/System32/KERNEL32.dll
cat > /tmp/srs-windows-runner-test/windows/System32/tar.exe <<'EOF'
#!/bin/bash
echo "$* DIR=$PWD" >> /tmp/srs-windows-runner-test/tar.log
[[ -z $STUB_TAR_FAIL ]] || exit 1
[[ $1 == -a && $2 == -c && $3 == -f ]] || exit 2
exec zip -q -r "$4" "${@:5}"
EOF
chmod +x /tmp/srs-windows-runner-test/windows/System32/tar.exe
export SYSTEMROOT=/tmp/srs-windows-runner-test/windows

# The fake MSYS2 is a bash.exe that runs the local bash.
mkdir -p /tmp/srs-windows-runner-test/msys64/usr/bin
cat > /tmp/srs-windows-runner-test/msys64/usr/bin/bash.exe <<'EOF'
#!/bin/bash
exec /bin/bash "$@"
EOF
chmod +x /tmp/srs-windows-runner-test/msys64/usr/bin/bash.exe
export SRS_MSYS2_ROOT=/tmp/srs-windows-runner-test/msys64

# The fake checkout. Its FFmpeg configure logs the arguments and PKG_CONFIG_PATH, one call per line,
# and exits with STUB_CONFIGURE_EXIT. It writes a Makefile whose build exits with STUB_MAKE_EXIT and
# whose install puts the libraries under the prefix.
FAKE=/tmp/srs-windows-runner-test/checkout
mkdir -p $FAKE/trunk/src/core $FAKE/trunk/3rdparty/ffmpeg-4-fit $FAKE/cmd/integ
touch $FAKE/trunk/src/core/srs_core.hpp
cat > $FAKE/trunk/3rdparty/ffmpeg-4-fit/configure <<'EOF'
#!/bin/bash
echo "$*" >> /tmp/srs-windows-runner-test/configure.log
echo "$PKG_CONFIG_PATH" >> /tmp/srs-windows-runner-test/pkg-config-path.log
echo "fake configure output"
[[ ${STUB_CONFIGURE_EXIT:-0} == 0 ]] || exit $STUB_CONFIGURE_EXIT
for a in "$@"; do [[ $a == --prefix=* ]] && PREFIX=${a#--prefix=}; done
printf 'all:\n\t@echo fake make output; exit $${STUB_MAKE_EXIT:-0}\n' > Makefile
printf 'install:\n\tmkdir -p %s/lib\n\ttouch %s/lib/libavcodec.a %s/lib/libavutil.a %s/lib/libswresample.a\n' \
  "$PREFIX" "$PREFIX" "$PREFIX" "$PREFIX" >> Makefile
EOF
chmod +x $FAKE/trunk/3rdparty/ffmpeg-4-fit/configure
# Its ST Makefile logs the target, STATIC_ONLY, EXTRA_CFLAGS and the directory, one build per line,
# exits with STUB_ST_EXIT, and makes libst.a and st.h in a platform directory that obj points at.
mkdir -p $FAKE/trunk/3rdparty/st-srs
echo "/* fake public.h */" > $FAKE/trunk/3rdparty/st-srs/public.h
cat > $FAKE/trunk/3rdparty/st-srs/Makefile <<'EOF'
win64-debug:
	@echo "$@ STATIC_ONLY=$(STATIC_ONLY) EXTRA_CFLAGS=$(EXTRA_CFLAGS) DIR=$(CURDIR)" >> /tmp/srs-windows-runner-test/st-make.log
	@echo fake st make output; exit $${STUB_ST_EXIT:-0}
	mkdir -p WIN64_test_DBG && touch WIN64_test_DBG/libst.a && cp public.h WIN64_test_DBG/st.h
	rm -f obj && ln -s WIN64_test_DBG obj
EOF
# Its OBS script logs its arguments and directory, prints a PASS and a LANE line and exits with
# STUB_OBS_EXIT, or with STUB_OBS_SKIP only skips, as when the host has no OBS.
mkdir -p $FAKE/skills/srs-develop/scripts
cat > $FAKE/skills/srs-develop/scripts/obs-windows-test.sh <<'EOS'
#!/bin/bash
echo "$* DIR=$PWD" >> /tmp/srs-windows-runner-test/obs.log
echo "fake obs output"
if [[ -n $STUB_OBS_SKIP ]]; then
  echo "SKIP OBS not available"
  echo "LANE obs passed=0 skipped=3 known=0 failed=0 elapsed=1s"
  exit 0
fi
echo "PASS API H264 AAC 640x360"
echo "KNOWN F2 API frames 0"
[[ ${STUB_OBS_EXIT:-0} == 0 ]] || echo "FAIL http-flv missing H264/aac playback"
echo "LANE obs passed=3 skipped=0 known=1 failed=0 elapsed=20s"
exit ${STUB_OBS_EXIT:-0}
EOS
# Its Full tier logs its arguments and directory, prints a lock line and a summary row, and exits with
# STUB_FULL_EXIT.
cat > $FAKE/skills/srs-develop/scripts/srs-full-tier.sh <<'EOS'
#!/bin/bash
echo "$* DIR=$PWD" >> /tmp/srs-windows-runner-test/full.log
echo "lock: acquired after 0s"
echo "windows-integ                        ${STUB_FULL_EXIT:-0}      2      0      9"
exit ${STUB_FULL_EXIT:-0}
EOS
# The files the release package takes, as make install takes them on Unix: the config files with
# the certificate, and the http root pages.
mkdir -p $FAKE/trunk/conf $FAKE/trunk/research/players/js $FAKE/trunk/research/console \
  $FAKE/trunk/3rdparty/signaling/www/demos
echo "daemon off;" > $FAKE/trunk/conf/console.conf
echo "daemon on;" > $FAKE/trunk/conf/srs.conf
echo "key" > $FAKE/trunk/conf/server.key
echo "crt" > $FAKE/trunk/conf/server.crt
echo "notes" > $FAKE/trunk/conf/README.md
echo "index" > $FAKE/trunk/research/index.html
echo "icon" > $FAKE/trunk/research/favicon.ico
echo "player" > $FAKE/trunk/research/players/js/srs.sdk.js
echo "console" > $FAKE/trunk/research/console/index.html
echo "demo" > $FAKE/trunk/3rdparty/signaling/www/demos/index.html
git -C $FAKE init -q
git -C $FAKE add -A
git -C $FAKE -c user.name=test -c user.email=test@localhost commit -q -m init

out() { cat /tmp/srs-windows-runner-test/out; }
# Run the runner in the fake checkout with the fake uname, cmake and cygpath on PATH.
run() {
  rm -f /tmp/srs-windows-runner-test/cmake.log /tmp/srs-windows-runner-test/configure.log \
    /tmp/srs-windows-runner-test/pkg-config-path.log /tmp/srs-windows-runner-test/st-make.log \
    /tmp/srs-windows-runner-test/checks.log /tmp/srs-windows-runner-test/srs.log /tmp/srs-windows-runner-test/curl.log \
    /tmp/srs-windows-runner-test/go.log /tmp/srs-windows-runner-test/integ-cli.log /tmp/srs-windows-runner-test/make.log \
    /tmp/srs-windows-runner-test/utest.log /tmp/srs-windows-runner-test/timeout.log /tmp/srs-windows-runner-test/obs.log \
    /tmp/srs-windows-runner-test/full.log /tmp/srs-windows-runner-test/dumpbin.log /tmp/srs-windows-runner-test/tar.log
  (cd $FAKE && PATH=/tmp/srs-windows-runner-test/bin:$PATH0 bash "$RUNNER" "$@") \
    > /tmp/srs-windows-runner-test/out 2>&1
  CODE=$?
}
configured() { [[ -s /tmp/srs-windows-runner-test/configure.log ]]; }
st_built() { [[ -s /tmp/srs-windows-runner-test/st-make.log ]]; }
# The step names of the RESULT lines, in order, on one line, without the summary line.
steps() { out | grep '^RESULT ' | awk '$3 != "all" {print $3}' | tr '\n' ' ' | sed 's/ $//'; }

export STUB_UNAME=MINGW64_NT-10.0-26100

# Arguments.
run
check "no step: exit 3 with usage" '[[ $CODE == 3 ]] && out | grep -q "^Usage: "'
run nosuch
check "unknown step: exit 3 with usage" '[[ $CODE == 3 ]] && out | grep -q "^Usage: "'
run deps extra
check "argument to deps: exit 3 with usage" '[[ $CODE == 3 ]] && out | grep -q "^Usage: "'
run integ 'TestRtmp_' extra
check "two arguments to integ: exit 3 with usage" '[[ $CODE == 3 ]] && out | grep -q "^Usage: "'
run package 8.0.64 extra
check "two arguments to package: exit 3 with usage" '[[ $CODE == 3 ]] && out | grep -q "^Usage: .*package \[version\]"'

# Native Windows only.
STUB_UNAME=Darwin run deps
check "macOS: refused, exit 2" '[[ $CODE == 2 ]] && out | grep -q "^SKIP windows: .*Darwin" && [[ -z $(steps) ]]'
STUB_UNAME=Linux run deps
check "Linux or WSL: refused, exit 2" '[[ $CODE == 2 ]] && out | grep -q "^SKIP windows: .*Linux" && [[ -z $(steps) ]]'
STUB_UNAME=CYGWIN_NT-10.0 run deps
check "Cygwin: refused, exit 2" '[[ $CODE == 2 ]] && out | grep -q "^SKIP windows: "'
STUB_UNAME=MSYS_NT-10.0-26100 run deps
check "MSYS: runs, exit 0" '[[ $CODE == 0 ]] && [[ $(steps) == deps ]]'
(cd "$CHECKOUT" && bash "$RUNNER" deps) > /tmp/srs-windows-runner-test/out 2>&1
CODE=$?
check "real uname of this OS: refused, exit 2" '[[ $CODE == 2 ]] && out | grep -q "^SKIP windows: "'

# An SRS checkout only.
(cd /tmp/srs-windows-runner-test && PATH=/tmp/srs-windows-runner-test/bin:$PATH0 bash "$RUNNER" deps) \
  > /tmp/srs-windows-runner-test/out 2>&1
CODE=$?
check "not in an SRS checkout: exit 3" '[[ $CODE == 3 ]] && out | grep -q "^SETUP_FAIL: "'

# The vcpkg manifest pins the registry and lists the libraries. gtest stays at 1.16, the last release
# that builds as C++14.
MANIFEST=$CHECKOUT/trunk/vcpkg.json
check "vcpkg.json: a pinned builtin-baseline" 'jq -e ".\"builtin-baseline\" | test(\"^[0-9a-f]{40}$\")" "$MANIFEST" >/dev/null'
check "vcpkg.json: openssl, libsrtp with openssl, libsrt, opus and gtest" \
  '[[ $(jq -r "[.dependencies[] | if type == \"string\" then . else .name + \"[\" + (.features | join(\",\")) + \"]\" end] | sort | join(\" \")" "$MANIFEST") == "gtest libsrt libsrtp[openssl] openssl opus" ]]'
check "vcpkg.json: gtest overridden to 1.16.0" '[[ $(jq -r ".overrides[] | select(.name == \"gtest\") | .version" "$MANIFEST") == 1.16.0 ]]'

# The deps step installs the libraries by configuring the Windows CMake build, which runs vcpkg.
run deps
check "deps: configures trunk/cmake/windows into trunk/objs/windows, exit 0" \
  '[[ $CODE == 0 ]] && grep -q "^-S trunk/cmake/windows -B trunk/objs/windows " /tmp/srs-windows-runner-test/cmake.log'
check "deps: configures with SRS_UTEST on, as --utest=on, so srs_utest.exe builds in the same tree" \
  'grep -q "^-S trunk/cmake/windows -B trunk/objs/windows .*-DSRS_UTEST=ON" /tmp/srs-windows-runner-test/cmake.log'
check "deps: configures with SRS_SIMULATOR on, as --simulator=on, so the integ NACK cases run" \
  'grep -q "^-S trunk/cmake/windows -B trunk/objs/windows .*-DSRS_SIMULATOR=ON" /tmp/srs-windows-runner-test/cmake.log'
check "deps: configures with SRS_GB28181 on, as --gb28181=on, so the integ GB28181 cases run" \
  'grep -q "^-S trunk/cmake/windows -B trunk/objs/windows .*-DSRS_GB28181=ON" /tmp/srs-windows-runner-test/cmake.log'
check "deps: configures with SRS_RTSP on, as --rtsp=on, so the integ RTSP cases run" \
  'grep -q "^-S trunk/cmake/windows -B trunk/objs/windows .*-DSRS_RTSP=ON" /tmp/srs-windows-runner-test/cmake.log'
check "deps: configures with cl as the C and C++ compiler, not the first compiler in PATH such as clang" \
  'grep -q "^-S trunk/cmake/windows -B trunk/objs/windows -G Ninja -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl " /tmp/srs-windows-runner-test/cmake.log'
check "deps: passes"'out | grep -q "^RESULT windows-x64 deps PASS"'
check "deps: the cmake output goes to the log, not the console" '! out | grep -q "fake cmake output"'
STUB_CMAKE_EXIT=1 run build
check "deps: cmake fails, exit 1, the later steps do not run" \
  '[[ $CODE == 1 ]] && [[ $(steps) == deps ]] && out | grep -q "^RESULT windows-x64 deps FAIL" && out | grep -q "^RESULT windows-x64 all FAIL at deps"'
check "deps: cmake fails, the log tail shown" 'out | grep -q "fake cmake output"'
check "deps: cmake fails, FFmpeg not configured, since libopus comes from vcpkg" '! configured'

# FFmpeg-fit is configured with the options of auto/depends.sh for a static build with libopus, except
# the Unix -lpthread and -lm, and with MSVC as the toolchain.
DEPENDS_OPTIONS=$( (grep -E '^    FFMPEG_OPTIONS=' "$CHECKOUT/trunk/auto/depends.sh";
    grep -E 'FFMPEG_OPTIONS=.*--enable-libopus' "$CHECKOUT/trunk/auto/depends.sh") |
  sed -E 's/^ *FFMPEG_OPTIONS="(\$FFMPEG_OPTIONS )?//; s/"$//' | tr ' ' '\n' | grep . | sort | tr '\n' ' ')
rm -rf /tmp/srs-windows-runner-test/checkout/trunk/objs
run deps
check "deps: FFmpeg built, exit 0" \
  '[[ $CODE == 0 ]] && out | grep -q "^RESULT windows-x64 deps PASS" && [[ $(grep -c . /tmp/srs-windows-runner-test/configure.log) == 1 ]]'
check "deps: FFmpeg with the options of auto/depends.sh" \
  '[[ $(tr " " "\n" < /tmp/srs-windows-runner-test/configure.log | grep -vE "^--(toolchain|prefix|pkg-config|pkg-config-flags)=" | grep . | sort | tr "\n" " ") == "$DEPENDS_OPTIONS" ]]'
check "deps: FFmpeg with the MSVC toolchain and static pkg-config" \
  'grep -q -- "--toolchain=msvc" /tmp/srs-windows-runner-test/configure.log && grep -q -- "--pkg-config-flags=--static" /tmp/srs-windows-runner-test/configure.log'
check "deps: FFmpeg installed into trunk/objs/windows/ffmpeg" \
  'grep -q -- "--prefix=$FAKE/trunk/objs/windows/ffmpeg\( \|$\)" /tmp/srs-windows-runner-test/configure.log && [[ -f $FAKE/trunk/objs/windows/ffmpeg/lib/libavcodec.a ]]'
check "deps: libopus from the vcpkg pkg-config files" \
  '[[ $(cat /tmp/srs-windows-runner-test/pkg-config-path.log) == $FAKE/trunk/objs/windows/vcpkg_installed/x64-windows-static-release/lib/pkgconfig ]]'
check "deps: the FFmpeg output goes to the log, not the console" \
  '! out | grep -q "fake configure output" && grep -q "fake make output" $FAKE/trunk/objs/windows-ffmpeg.log'
run deps
check "deps: FFmpeg cached, not configured again" '[[ $CODE == 0 ]] && ! configured'
echo "# changed" >> $FAKE/trunk/3rdparty/ffmpeg-4-fit/configure
git -C $FAKE -c user.name=test -c user.email=test@localhost commit -q -a -m change
run deps
check "deps: FFmpeg source changed, built again" '[[ $CODE == 0 ]] && configured'
rm -rf /tmp/srs-windows-runner-test/checkout/trunk/objs/windows/ffmpeg
run deps
check "deps: FFmpeg libraries missing, built again" '[[ $CODE == 0 ]] && configured && [[ -f $FAKE/trunk/objs/windows/ffmpeg/lib/libavcodec.a ]]'

rm -rf /tmp/srs-windows-runner-test/checkout/trunk/objs
STUB_CONFIGURE_EXIT=1 run build
check "deps: FFmpeg configure fails, exit 1, the later steps do not run" \
  '[[ $CODE == 1 ]] && [[ $(steps) == deps ]] && out | grep -q "^RESULT windows-x64 deps FAIL"'
check "deps: FFmpeg configure fails, the log tail shown" 'out | grep -q "fake configure output"'
STUB_MAKE_EXIT=1 run deps
check "deps: FFmpeg make fails, exit 1" '[[ $CODE == 1 ]] && out | grep -q "fake make output"'
run deps
check "deps: after a failed build, FFmpeg built again" '[[ $CODE == 0 ]] && configured'
rm -rf /tmp/srs-windows-runner-test/checkout/trunk/objs
SRS_MSYS2_ROOT=/tmp/srs-windows-runner-test/no-msys64 run deps
check "deps: no MSYS2, exit 1 naming it" '[[ $CODE == 1 ]] && out | grep -q "MSYS2"'

# ST is built as ./configure builds it, a static debug library with the flags of auto/depends.sh, from
# a copy of the source, into trunk/objs/windows/st, before the CMake build that links it.
DEPENDS_ST_CFLAGS=$(sed -nE 's/^_ST_EXTRA_CFLAGS="([^$"]*)"$/\1/p' "$CHECKOUT/trunk/auto/depends.sh")
rm -rf /tmp/srs-windows-runner-test/checkout/trunk/objs
run deps
check "deps: ST built once with make win64-debug, exit 0" \
  '[[ $CODE == 0 ]] && [[ $(grep -c . /tmp/srs-windows-runner-test/st-make.log) == 1 ]] && grep -q "^win64-debug " /tmp/srs-windows-runner-test/st-make.log'
check "deps: ST static only, with the EXTRA_CFLAGS of auto/depends.sh" \
  '[[ -n $DEPENDS_ST_CFLAGS ]] && grep -q " STATIC_ONLY=yes EXTRA_CFLAGS=$DEPENDS_ST_CFLAGS DIR=" /tmp/srs-windows-runner-test/st-make.log'
check "deps: ST built in trunk/objs/windows/st-srs, not in the source" \
  'grep -q " DIR=.*/checkout/trunk/objs/windows/st-srs$" /tmp/srs-windows-runner-test/st-make.log && [[ -z $(git -C $FAKE status --porcelain --ignored trunk/3rdparty) ]]'
check "deps: st.h and libst.a in trunk/objs/windows/st" \
  '[[ -f $FAKE/trunk/objs/windows/st/libst.a ]] && grep -q "fake public.h" $FAKE/trunk/objs/windows/st/st.h'
check "deps: the ST output goes to the log, not the console" \
  '! out | grep -q "fake st make output" && grep -q "fake st make output" $FAKE/trunk/objs/windows-st.log'
run deps
check "deps: ST cached, not built again" '[[ $CODE == 0 ]] && ! st_built'
echo "/* changed */" >> $FAKE/trunk/3rdparty/st-srs/public.h
git -C $FAKE -c user.name=test -c user.email=test@localhost commit -q -a -m change
run deps
check "deps: ST source changed, built again from the new source" \
  '[[ $CODE == 0 ]] && st_built && grep -q "changed" $FAKE/trunk/objs/windows/st/st.h'
rm -f $FAKE/trunk/objs/windows/st/libst.a
run deps
check "deps: libst.a missing, built again" '[[ $CODE == 0 ]] && st_built && [[ -f $FAKE/trunk/objs/windows/st/libst.a ]]'
rm -f $FAKE/trunk/objs/windows/st/libst.a
STUB_ST_EXIT=1 run build
check "deps: ST make fails, exit 1, the later steps do not run" \
  '[[ $CODE == 1 ]] && [[ $(steps) == deps ]] && out | grep -q "^RESULT windows-x64 deps FAIL"'
check "deps: ST make fails, the log tail shown" 'out | grep -q "fake st make output"'
check "deps: ST make fails, the CMake build not configured, since it links ST" \
  '[[ ! -s /tmp/srs-windows-runner-test/cmake.log ]]'
run deps
check "deps: after a failed build, ST built again" '[[ $CODE == 0 ]] && st_built'

# The Windows CMake build links ST from where the deps step puts it.
CMAKE_FILE=$CHECKOUT/trunk/cmake/windows/CMakeLists.txt
check "CMake: an imported ST library at objs/windows/st/libst.a with ws2_32" \
  'grep -q "add_library(st STATIC IMPORTED)" "$CMAKE_FILE" && grep -q "objs/windows/st\"" "$CMAKE_FILE" && grep -q "/libst.a\"" "$CMAKE_FILE" && grep -q "ws2_32" "$CMAKE_FILE"'

# The deps step ends by building the link-check programs, one per library, and running each.
CHECKS="openssl srtp srt ffmpeg st gtest"
DEPS_CHECK_DIR=$CHECKOUT/trunk/cmake/windows/deps-check
check "deps-check: one source per library, and no other" \
  '[[ $(cd "$DEPS_CHECK_DIR" 2>/dev/null && ls *.cpp | tr "\n" " ") == "ffmpeg.cpp gtest.cpp openssl.cpp srt.cpp srtp.cpp st.cpp " ]]'
check "deps-check: the CMake build adds the deps-check directory" \
  'grep -q "^add_subdirectory(deps-check)" "$CMAKE_FILE"'
check "deps-check: its CMake file builds each library check" \
  '(for c in $CHECKS; do grep -q "^srs_deps_check($c " "$DEPS_CHECK_DIR/CMakeLists.txt" || exit 1; done)'
rm -rf /tmp/srs-windows-runner-test/checkout/trunk/objs
run deps
check "deps: builds the srs-deps-check target after FFmpeg, exit 0" \
  '[[ $CODE == 0 ]] && [[ $(tail -1 /tmp/srs-windows-runner-test/cmake.log) == "--build trunk/objs/windows --target srs-deps-check" ]] && [[ -f $FAKE/trunk/objs/windows/ffmpeg/lib/libavcodec.a ]]'
check "deps: runs every link check once, in order" \
  '[[ $(tr "\n" " " < /tmp/srs-windows-runner-test/checks.log) == "$CHECKS " ]]'
check "deps: one PASS line per link check, with its last output line" \
  '(for c in $CHECKS; do out | grep -q "^CHECK $c PASS: fake $c 1.0$" || exit 1; done)'
check "deps: the build and check output goes to the log, not the console" \
  '! out | grep -q "fake check output" && grep -q "fake check output" $FAKE/trunk/objs/windows-deps-check.log'
run deps
check "deps: link checks run again when the build is cached" '[[ $CODE == 0 ]] && [[ -s /tmp/srs-windows-runner-test/checks.log ]]'
STUB_CHECK_FAIL=srt run build
check "deps: a link check fails, exit 1, the later steps do not run" \
  '[[ $CODE == 1 ]] && [[ $(steps) == deps ]] && out | grep -q "^RESULT windows-x64 deps FAIL"'
check "deps: a link check fails, its output shown, the other checks still run" \
  'out | grep -q "^CHECK srt FAIL" && out | grep -q "fake check output" && [[ $(grep -c . /tmp/srs-windows-runner-test/checks.log) == 6 ]]'
STUB_CMAKE_BUILD_EXIT=1 run deps
check "deps: the link-check build fails, exit 1, the log tail shown, no check runs" \
  '[[ $CODE == 1 ]] && out | grep -q "fake cmake output" && out | grep -q "windows-deps-check.log" && [[ ! -s /tmp/srs-windows-runner-test/checks.log ]]'
rm -f $FAKE/trunk/objs/windows/deps-check/deps-check-gtest.exe
STUB_CHECK_MISSING=gtest run deps
check "deps: a link-check program missing after the build, exit 1 naming it" \
  '[[ $CODE == 1 ]] && out | grep -q "^CHECK gtest FAIL"'

# The build step builds the srs target after deps, going on after a file fails, and counts the files
# of each module that compiled.
run build
check "build: builds the srs target with ninja -k 0 after deps, exit 0" \
  '[[ $CODE == 0 ]] && [[ $(steps) == "deps build" ]] && [[ $(tail -1 /tmp/srs-windows-runner-test/cmake.log) == "--build trunk/objs/windows --target srs -- -k 0" ]]'
check "build: passes, with every file compiled per module" \
  'out | grep -q "^RESULT windows-x64 build PASS" && out | grep -q "^BUILD core 2 of 2, kernel 1 of 1, main 1 of 1 compiled$"'
check "build: the build output goes to the log, not the console" \
  '! out | grep -q "fake cmake output" && grep -q "fake cmake output" $FAKE/trunk/objs/windows-build.log'

# After it links, the build step starts srs.exe: -v prints the version, the default config file is
# refused for its daemon mode, and env-only srs.exe listens on RTMP, HTTP and the API at the ports
# from SRS_TEST_PORT_BASE, answers /api/v1/versions with the same version, and stops.
srs_calls() { cat /tmp/srs-windows-runner-test/srs.log 2>/dev/null; }
check "start: srs.exe -v prints the version" 'out | grep -q "^START version PASS: 8.0.64$"'
check "start: conf/srs.conf refused for daemon mode, in trunk, logging to the console" \
  'out | grep -q "^START daemon PASS" && srs_calls | grep -q "^-t -c conf/srs.conf DIR=$FAKE/trunk TANK=console "'
check "start: env-only srs.exe in trunk with RTMP, HTTP and API at the default ports" \
  'srs_calls | grep -q "^-e DIR=$FAKE/trunk TANK= RTMP=15500 HTTP=on:15501 API=on:15502$"'
check "start: RTMP, HTTP and the API answer the same version" \
  'out | grep -q "^START env-only PASS: RTMP 15500, HTTP 15501, API 15502 version 8.0.64$"'
check "start: srs.exe stopped, its ports closed" \
  'out | grep -q "^START stop PASS" && srs_calls | grep -qx stopped && [[ ! -f /tmp/srs-windows-runner-test/listening ]]'
check "start: the srs.exe output goes to the log, not the console" \
  '! out | grep -q "fake srs output" && grep -q "fake srs output" $FAKE/trunk/objs/windows-start.log'
SRS_TEST_PORT_BASE=16500 run build
check "start: ports from SRS_TEST_PORT_BASE" \
  '[[ $CODE == 0 ]] && srs_calls | grep -q "^-e .* RTMP=16500 HTTP=on:16501 API=on:16502$"'
STUB_SRS_V_EXIT=1 run utest
check "start: -v fails, the build step fails, the later steps do not run" \
  '[[ $CODE == 1 ]] && out | grep -q "^START version FAIL" && [[ $(steps) == "deps build" ]] && ! srs_calls | grep -q "^-e "'
STUB_SRS_DAEMON_OK=1 run build
check "start: conf/srs.conf accepted with daemon on, fails" \
  '[[ $CODE == 1 ]] && out | grep -q "^START daemon FAIL" && out | grep -q "test is successful"'
STUB_SRS_START_EXIT=1 run build
check "start: srs.exe quits at startup, fails with the log tail" \
  '[[ $CODE == 1 ]] && out | grep -q "^START env-only FAIL" && out | grep -q "fake srs output" && out | grep -q "windows-start.log"'
for p in rtmp http api; do
  STUB_SRS_CLOSED=$p run build
  check "start: $p not listening, fails, srs.exe stopped" \
    '[[ $CODE == 1 ]] && out | grep -qi "^START env-only FAIL.*$p" && srs_calls | grep -qx stopped && [[ ! -f /tmp/srs-windows-runner-test/listening ]]'
done
STUB_SRS_API_VERSION=8.0.1 run build
check "start: the API answers another version, fails, srs.exe stopped" \
  '[[ $CODE == 1 ]] && out | grep -q "^START env-only FAIL.*8.0.1" && srs_calls | grep -qx stopped'
echo " 15502" > /tmp/srs-windows-runner-test/listening
run build
check "start: a port already in use, fails before srs.exe starts" \
  '[[ $CODE == 1 ]] && out | grep -q "^START env-only FAIL.*15502" && ! srs_calls | grep -q "^-e "'
rm -f /tmp/srs-windows-runner-test/listening
# Two files fail; the first error comes from a header both include, so it is shown once.
STUB_SRS_BUILD_EXIT=1 STUB_SRS_BUILD_OUTPUT='[1/4] Building CXX object a\nFAILED: [code=2] CMakeFiles/srs.dir/__/__/src/core/srs_b.cpp.obj\nC:/srs/trunk/src/core/srs_core.hpp(69): fatal error C1189: #error:  "Only support"\nFAILED: CMakeFiles/srs.dir/__/__/src/kernel/srs_c.cpp.obj\nC:/srs/trunk/src/core/srs_core.hpp(69): fatal error C1189: #error:  "Only support"\nC:/srs/trunk/src/kernel/srs_c.cpp(9): error C2065: x: undeclared identifier\nninja: build stopped: cannot make progress due to previous errors.\n' \
  run utest
check "build: a file fails, exit 1, the later steps do not run" \
  '[[ $CODE == 1 ]] && [[ $(steps) == "deps build" ]] && out | grep -q "^RESULT windows-x64 build FAIL"'
check "build: a file fails, the files compiled per module" \
  'out | grep -q "^BUILD core 1 of 2, kernel 0 of 1, main 1 of 1 compiled$"'
check "build: a file fails, each compiler error shown once, not the rest of the log" \
  '[[ $(out | grep -c "fatal error C1189") == 1 ]] && out | grep -q "error C2065" && ! out | grep -q "Building CXX" && out | grep -q "windows-build.log"'
check "build: a file fails, srs.exe not started" '! srs_calls | grep -q .'

# The Windows CMake build reads the module lists and makes the auto header with two helper files,
# which run here in CMake script mode.
CMAKE_DIR=$CHECKOUT/trunk/cmake/windows
cat > /tmp/srs-windows-runner-test/driver.cmake <<'EOF'
include("${SRS_CMAKE_DIR}/../srs-modules.cmake")
include("${SRS_CMAKE_DIR}/srs-auto-headers.cmake")
srs_server_sources(files)
string(REPLACE ";" "\n" text "${files}")
file(WRITE "${OUT_SOURCES}" "${text}\n")
if(DEFINED OUT_HEADER)
  srs_write_auto_headers("${OUT_HEADER}")
endif()
EOF
# Run the helpers with RTSP, GB28181, FFMPEG_FIT, SIMULATOR and UTEST as ON or OFF, in that order.
cmake_p() {
  cmake -DSRS_CMAKE_DIR="$CMAKE_DIR" -DSRS_RTSP=$1 -DSRS_GB28181=$2 -DSRS_FFMPEG_FIT=$3 \
    -DSRS_SIMULATOR=$4 -DSRS_UTEST=$5 -DOUT_SOURCES=/tmp/srs-windows-runner-test/sources.txt \
    -DOUT_HEADER=/tmp/srs-windows-runner-test/srs_auto_headers.hpp "${@:6}" \
    -P /tmp/srs-windows-runner-test/driver.cmake > /tmp/srs-windows-runner-test/out 2>&1
  CODE=$?
}
# The srs sources of the module list, for RTSP, GB28181 and FFMPEG_FIT as YES or NO, from its lists
# in the order ./configure takes them.
expected_sources() {
  (
    . "$CHECKOUT/trunk/auto/module_files.sh"
    in_dir() { local dir=$1; shift; printf "$dir/%s.cpp\n" "$@"; }
    in_dir src/core "${SRS_CORE_FILES[@]}"
    in_dir src/kernel "${SRS_KERNEL_FILES[@]}"
    in_dir src/protocol "${SRS_PROTOCOL_FILES[@]}"
    if [[ $1 == YES ]]; then in_dir src/protocol "${SRS_PROTOCOL_RTSP_FILES[@]}"; fi
    in_dir src/app "${SRS_APP_FILES[@]}"
    if [[ $1 == YES ]]; then in_dir src/app "${SRS_APP_RTSP_FILES[@]}"; fi
    if [[ $3 == YES ]]; then in_dir src/app "${SRS_APP_FFMPEG_FIT_FILES[@]}"; fi
    if [[ $2 == YES ]]; then in_dir src/app "${SRS_APP_GB28181_FILES[@]}"; fi
    in_dir src/main "${SRS_SERVER_FILES[@]}"
  )
}
yn() { [[ $1 == ON ]] && echo YES || echo NO; }
if ! command -v cmake >/dev/null; then
  echo "SETUP_FAIL: cmake is not installed; the CMake helper checks need it"
  FAILED=$((FAILED + 1))
fi
SOURCES_OK=yes
for rtsp in ON OFF; do for gb in ON OFF; do for fit in ON OFF; do
  cmake_p $rtsp $gb $fit OFF OFF
  [[ $(cat /tmp/srs-windows-runner-test/sources.txt 2>/dev/null) == "$(expected_sources $(yn $rtsp) $(yn $gb) $(yn $fit))" ]] ||
    { SOURCES_OK="no, RTSP=$rtsp GB28181=$gb FFMPEG_FIT=$fit"; break 3; }
done; done; done
check "CMake: the srs sources from auto/module_files.sh, by feature ($SOURCES_OK)" '[[ $SOURCES_OK == yes ]]'
# The same files, in the same order, as the srs link line of the Makefile ./configure made here.
UNIX_OBJS=$CHECKOUT/trunk/objs
if [[ -f $UNIX_OBJS/Makefile && -f $UNIX_OBJS/srs_auto_headers.hpp ]]; then
  on_in_unix() { grep -q "^#define SRS_$1$" "$UNIX_OBJS/srs_auto_headers.hpp" && echo ON || echo OFF; }
  cmake_p $(on_in_unix RTSP) $(on_in_unix GB28181) $(on_in_unix FFMPEG_FIT) OFF OFF
  check "CMake: the srs sources equal the objects of ./objs/srs in the configured Makefile" \
    '[[ $(cat /tmp/srs-windows-runner-test/sources.txt) == "$(grep -m 1 "^\./objs/srs:" "$UNIX_OBJS/Makefile" | tr " " "\n" | sed -nE "s|^\./objs/(src/.*)\.o$|\1.cpp|p")" ]]'
else
  echo "SKIP CMake sources against the Makefile: run ./configure in trunk first"
fi
mkdir -p /tmp/srs-windows-runner-test/trunk/auto
printf 'SRS_CORE_FILES=(srs_core)\nSRS_CORE_NOSUCH_FILES=(srs_core_x)\n' > /tmp/srs-windows-runner-test/trunk/auto/module_files.sh
cmake_p ON ON ON OFF OFF -DSRS_TRUNK_DIR=/tmp/srs-windows-runner-test/trunk
check "CMake: an unknown feature in the module list fails, naming it" '[[ $CODE != 0 ]] && out | grep -q "NOSUCH"'
printf 'SRS_CORE_FILES=(srs_core\n    srs-core-x)\n' > /tmp/srs-windows-runner-test/trunk/auto/module_files.sh
cmake_p ON ON ON OFF OFF -DSRS_TRUNK_DIR=/tmp/srs-windows-runner-test/trunk
check "CMake: a malformed file name in the module list fails, naming it" '[[ $CODE != 0 ]] && out | grep -q "srs-core-x"'

# The auto header has the macros ./configure writes, in the same order, with the Windows values.
mkdir -p /tmp/srs-windows-runner-test/unix
(cd "$CHECKOUT/trunk" && SRS_OBJS=/tmp/srs-windows-runner-test/unix bash auto/auto_headers.sh) >/dev/null 2>&1
HEADER=/tmp/srs-windows-runner-test/srs_auto_headers.hpp
macro_names() { grep -oE '^#(define|undef) [A-Za-z0-9_]+' "$1" | awk '{print $2}'; }
authors() { sed -n '/^#define RTMP_SIG_SRS_AUTHORS/,/^"$/p' "$1"; }
rm -f $HEADER
cmake_p OFF ON ON OFF OFF
check "auto header: the macros of auto/auto_headers.sh, in order" \
  '[[ -s /tmp/srs-windows-runner-test/unix/srs_auto_headers.hpp ]] && [[ $(macro_names $HEADER) == "$(macro_names /tmp/srs-windows-runner-test/unix/srs_auto_headers.hpp)" ]]'
check "auto header: the authors of AUTHORS.md" \
  '[[ -n $(authors $HEADER) ]] && [[ $(authors $HEADER) == "$(authors /tmp/srs-windows-runner-test/unix/srs_auto_headers.hpp)" ]]'
on_off() { local m; for m in "$@"; do grep -qx "#define $m" $HEADER && echo "$m on" || { grep -qx "#undef $m" $HEADER && echo "$m off"; }; done | tr '\n' ' '; }
check "auto header: always on for Windows" \
  '[[ $(on_off SRS_SRT SRS_RTC SRS_H265 SRS_HTTPS SRS_FFMPEG_STUB SRS_TRACE SRS_SINGLE_THREAD SRS_LOG_LEVEL_V2) == "SRS_SRT on SRS_RTC on SRS_H265 on SRS_HTTPS on SRS_FFMPEG_STUB on SRS_TRACE on SRS_SINGLE_THREAD on SRS_LOG_LEVEL_V2 on " ]]'
check "auto header: off for Windows, such as OSX, backtrace, sanitizer and gperf" \
  '[[ $(on_off SRS_OSX SRS_BACKTRACE SRS_SANITIZER SRS_SANITIZER_LOG SRS_VALGRIND SRS_GPERF SRS_CROSSBUILD SRS_CXX11 SRS_CXX14 SRS_FFMPEG_OPUS) == "SRS_OSX off SRS_BACKTRACE off SRS_SANITIZER off SRS_SANITIZER_LOG off SRS_VALGRIND off SRS_GPERF off SRS_CROSSBUILD off SRS_CXX11 off SRS_CXX14 off SRS_FFMPEG_OPUS off " ]]'
check "auto header: the options, with FORCE_PUBLIC4UTEST following UTEST" \
  '[[ $(on_off SRS_RTSP SRS_GB28181 SRS_FFMPEG_FIT SRS_SIMULATOR SRS_UTEST SRS_FORCE_PUBLIC4UTEST) == "SRS_RTSP off SRS_GB28181 on SRS_FFMPEG_FIT on SRS_SIMULATOR off SRS_UTEST off SRS_FORCE_PUBLIC4UTEST off " ]]'
cmake_p ON OFF OFF ON ON
check "auto header: the options turned the other way" \
  '[[ $(on_off SRS_RTSP SRS_GB28181 SRS_FFMPEG_FIT SRS_SIMULATOR SRS_UTEST SRS_FORCE_PUBLIC4UTEST) == "SRS_RTSP on SRS_GB28181 off SRS_FFMPEG_FIT off SRS_SIMULATOR on SRS_UTEST on SRS_FORCE_PUBLIC4UTEST on " ]]'
check "auto header: the CMake options in SRS_USER_CONFIGURE" \
  'grep -q "^#define SRS_USER_CONFIGURE \".*-DSRS_RTSP=ON .*-DSRS_UTEST=ON" $HEADER'
# SRS_CONFIGURE is the options2 of /api/v1/features, with a configure switch per feature, as on Unix.
switches() { local o m; for o in "$@"; do m=SRS_$(echo ${o%%=*} | tr 'a-z-' 'A-Z_'); grep -q "^#define SRS_CONFIGURE \".*--$o=on" $HEADER && echo "$m on" || { grep -q "^#define SRS_CONFIGURE \".*--$o=off" $HEADER && echo "$m off"; }; done | tr '\n' ' '; }
FEATURES="srt rtc rtsp gb28181 ffmpeg-fit simulator utest sanitizer https"
check "auto header: SRS_CONFIGURE has a switch per feature, as the macros" \
  '[[ $(switches $FEATURES) == "$(on_off SRS_SRT SRS_RTC SRS_RTSP SRS_GB28181 SRS_FFMPEG_FIT SRS_SIMULATOR SRS_UTEST SRS_SANITIZER SRS_HTTPS)" ]]'
cmake_p OFF ON ON OFF OFF
check "auto header: SRS_CONFIGURE with the options turned the other way" \
  '[[ $(switches $FEATURES) == "$(on_off SRS_SRT SRS_RTC SRS_RTSP SRS_GB28181 SRS_FFMPEG_FIT SRS_SIMULATOR SRS_UTEST SRS_SANITIZER SRS_HTTPS)" ]] && [[ $(switches rtsp utest) == "SRS_RTSP off SRS_UTEST off " ]]'
cmake_p ON OFF OFF ON ON
# The runner configures on every run, so an unchanged header keeps its build time and file, or every
# file would build again.
sed -i.bak -E 's/^#define SRS_BUILD_TS ".*"/#define SRS_BUILD_TS "1"/' $HEADER
touch /tmp/srs-windows-runner-test/header-mark
sleep 1
cmake_p ON OFF OFF ON ON
check "auto header: options unchanged, the file and its build time kept" \
  'grep -qx "#define SRS_BUILD_TS \"1\"" $HEADER && [[ ! $HEADER -nt /tmp/srs-windows-runner-test/header-mark ]]'
cmake_p OFF OFF OFF ON ON
check "auto header: an option changed, a new build time" \
  'grep -q "^#define SRS_BUILD_TS \"[0-9]\{10\}\"" $HEADER && grep -qx "#undef SRS_RTSP" $HEADER'

# The CMake file builds srs.exe from those sources, with the MSVC options.
check "CMake: an option per feature of the module list" \
  '(for f in $(sed -nE "s/^SRS_[A-Z]+_([A-Z0-9_]+)_FILES=.*/\1/p" "$CHECKOUT/trunk/auto/module_files.sh" | sort -u); do grep -q "^option(SRS_$f " "$CMAKE_FILE" || exit 1; done)'
check "CMake: srs.exe from srs_server_sources, with the auto header and the source list" \
  'grep -q "^srs_server_sources(" "$CMAKE_FILE" && grep -q "^add_executable(srs " "$CMAKE_FILE" && grep -q "^srs_write_auto_headers(" "$CMAKE_FILE" && grep -q "srs-sources.txt" "$CMAKE_FILE"'
check "CMake: C++14, /W3 and no /CETCOMPAT" \
  'grep -q "CXX_STANDARD 14" "$CMAKE_FILE" && grep -q "/W3" "$CMAKE_FILE" && grep -q "/CETCOMPAT:NO" "$CMAKE_FILE"'
check "CMake: the OpenSSL 1.x APIs SRS calls build without deprecation warnings" \
  'grep -q "OPENSSL_API_COMPAT=0x10" "$CMAKE_FILE"'
MANIFEST_FILE=$CHECKOUT/trunk/cmake/windows/srs.manifest
check "CMake: srs.exe and srs_utest.exe embed a manifest with the UTF-8 code page" \
  'grep -q "<activeCodePage [^>]*>UTF-8</activeCodePage>" "$MANIFEST_FILE" && grep -q "^add_executable(srs .*srs.manifest" "$CMAKE_FILE" && grep -q "add_executable(srs_utest .*srs.manifest" "$CMAKE_FILE"'

# The utest sources: the srs modules but main, then the utest files of the module list, by feature,
# as ./configure links srs_utest.
cat > /tmp/srs-windows-runner-test/driver-utest.cmake <<'EOF'
include("${SRS_CMAKE_DIR}/../srs-modules.cmake")
srs_utest_sources(files)
string(REPLACE ";" "\n" text "${files}")
file(WRITE "${OUT_SOURCES}" "${text}\n")
EOF
expected_utest_sources() {
  expected_sources $1 $2 $3 | grep -v '^src/main/'
  (
    . "$CHECKOUT/trunk/auto/module_files.sh"
    printf "src/utest/%s.cpp\n" "${SRS_UTEST_FILES[@]}"
    if [[ $2 == YES ]]; then printf "src/utest/%s.cpp\n" "${SRS_UTEST_GB28181_FILES[@]}"; fi
    if [[ $1 == YES ]]; then printf "src/utest/%s.cpp\n" "${SRS_UTEST_RTSP_FILES[@]}"; fi
  )
}
UTEST_SOURCES_OK=yes
for rtsp in ON OFF; do for gb in ON OFF; do
  cmake -DSRS_CMAKE_DIR="$CMAKE_DIR" -DSRS_RTSP=$rtsp -DSRS_GB28181=$gb -DSRS_FFMPEG_FIT=ON -DSRS_SIMULATOR=OFF \
    -DSRS_UTEST=ON -DOUT_SOURCES=/tmp/srs-windows-runner-test/sources.txt \
    -P /tmp/srs-windows-runner-test/driver-utest.cmake > /tmp/srs-windows-runner-test/out 2>&1
  [[ $(cat /tmp/srs-windows-runner-test/sources.txt 2>/dev/null) == "$(expected_utest_sources $(yn $rtsp) $(yn $gb) YES)" ]] ||
    { UTEST_SOURCES_OK="no, RTSP=$rtsp GB28181=$gb"; break 2; }
done; done
check "CMake: the srs_utest sources, the modules but main and the utest files, by feature ($UTEST_SOURCES_OK)" \
  '[[ $UTEST_SOURCES_OK == yes ]]'
check "CMake: srs_utest.exe from srs_utest_sources with gtest, its own main, and the utest source list" \
  'grep -q "^  srs_utest_sources(" "$CMAKE_FILE" && grep -q "add_executable(srs_utest " "$CMAKE_FILE" && grep -q "GTest::gtest[^_]" "$CMAKE_FILE" && ! grep -q "GTest::gtest_main" "$CMAKE_FILE" && grep -q "utest-sources.txt" "$CMAKE_FILE"'

# The IDE CMake for macOS and Linux runs ./configure for the deps and the auto header, then takes the
# srs and utest sources from the same helper, with the features of the header configure wrote.
IDE_FILE=$CHECKOUT/trunk/cmake/CMakeLists.txt
check "IDE CMake: srs and utest from srs-modules.cmake with the configured features, no glob of src" \
  'grep -q "^include(.*/srs-modules.cmake\")" "$IDE_FILE" && grep -q "^srs_module_options_from_header(" "$IDE_FILE" && grep -q "^srs_server_sources(" "$IDE_FILE" && grep -q "^srs_utest_sources(" "$IDE_FILE" && ! grep -qi "AUX_SOURCE_DIRECTORY" "$IDE_FILE"'
check "CMake: the Windows and the IDE CMake include the one srs-modules.cmake in trunk/cmake" \
  '[[ -f $CHECKOUT/trunk/cmake/srs-modules.cmake && ! -e $CMAKE_DIR/srs-modules.cmake ]] && grep -q "^include(\"\${CMAKE_CURRENT_SOURCE_DIR}/../srs-modules.cmake\")" "$CMAKE_FILE"'
cat > /tmp/srs-windows-runner-test/driver-ide.cmake <<'EOF'
include("${SRS_CMAKE_DIR}/../srs-modules.cmake")
srs_module_options_from_header("${SRS_HEADER}")
srs_server_sources(files)
string(REPLACE ";" "\n" text "${files}")
file(WRITE "${OUT_SOURCES}" "${text}\n")
srs_utest_sources(files)
string(REPLACE ";" "\n" text "${files}")
file(WRITE "${OUT_UTEST_SOURCES}" "${text}\n")
EOF
cmake_ide() {
  cmake -DSRS_CMAKE_DIR="$CMAKE_DIR" -DSRS_HEADER="$1" -DOUT_SOURCES=/tmp/srs-windows-runner-test/sources.txt \
    -DOUT_UTEST_SOURCES=/tmp/srs-windows-runner-test/utest-sources.txt \
    -P /tmp/srs-windows-runner-test/driver-ide.cmake > /tmp/srs-windows-runner-test/out 2>&1
  CODE=$?
}
# Only the exact #define turns a feature on, not its _BOOL macro or an #undef.
printf '#define SRS_RTSP\n#define SRS_GB28181_BOOL true\n#undef SRS_FFMPEG_FIT\n' > /tmp/srs-windows-runner-test/ide-header.hpp
cmake_ide /tmp/srs-windows-runner-test/ide-header.hpp
check "IDE CMake: a feature is on when the header defines SRS_<FEATURE>" \
  '[[ $CODE == 0 ]] && [[ $(cat /tmp/srs-windows-runner-test/sources.txt 2>/dev/null) == "$(expected_sources YES NO NO)" ]] && [[ $(cat /tmp/srs-windows-runner-test/utest-sources.txt 2>/dev/null) == "$(expected_utest_sources YES NO NO)" ]]'
cmake_ide /tmp/srs-windows-runner-test/no-such-header.hpp
check "IDE CMake: no header, fails, naming it" '[[ $CODE != 0 ]] && out | grep -q "no-such-header.hpp"'
# The same files, in the same order, as the srs and srs_utest link lines of the Makefiles ./configure
# made here, for whatever flags it was given.
UTEST_MAKEFILE=$(ls $UNIX_OBJS/Platform-*/utest/Makefile 2>/dev/null | head -n 1)
if [[ -f $UNIX_OBJS/Makefile && -f $UNIX_OBJS/srs_auto_headers.hpp && -n $UTEST_MAKEFILE ]]; then
  cmake_ide "$UNIX_OBJS/srs_auto_headers.hpp"
  unix_utest_objects() {
    grep -m 1 "^SRS_SOURCE_OBJS = " "$UTEST_MAKEFILE" | tr " " "\n" | sed -nE "s|^\.\./\.\./\.\./\./objs/(src/.*)\.o$|\1.cpp|p"
    grep -m 1 "^\.\./\.\./\.\./\./objs/srs_utest :" "$UTEST_MAKEFILE" | tr " " "\n" | sed -nE "s|^(srs_utest.*)\.o$|src/utest/\1.cpp|p"
  }
  check "IDE CMake: the srs sources equal the objects of ./objs/srs in the configured Makefile" \
    '[[ $CODE == 0 ]] && [[ $(cat /tmp/srs-windows-runner-test/sources.txt) == "$(grep -m 1 "^\./objs/srs:" "$UNIX_OBJS/Makefile" | tr " " "\n" | sed -nE "s|^\./objs/(src/.*)\.o$|\1.cpp|p")" ]]'
  check "IDE CMake: the utest sources equal the objects of srs_utest in the configured utest Makefile" \
    '[[ $CODE == 0 ]] && [[ $(cat /tmp/srs-windows-runner-test/utest-sources.txt) == "$(unix_utest_objects)" ]]'
else
  echo "SKIP IDE CMake sources against the Makefiles: run ./configure --utest=on in trunk first"
fi

# The utest step builds srs_utest.exe after build, going on after a file fails, then runs it in trunk.
utest_calls() { cat /tmp/srs-windows-runner-test/utest.log 2>/dev/null; }
run utest
check "utest: builds the srs_utest target with ninja -k 0 after build, exit 0" \
  '[[ $CODE == 0 ]] && [[ $(steps) == "deps build utest" ]] && [[ $(grep -- "--target srs_utest" /tmp/srs-windows-runner-test/cmake.log) == "--build trunk/objs/windows --target srs_utest -- -k 0" ]]'
check "utest: the utest files compiled" 'out | grep -q "^BUILD utest 3 of 3 compiled$"'
check "utest: runs srs_utest.exe once in trunk, under a timeout" \
  '[[ $(utest_calls) == " DIR=$FAKE/trunk" ]] && grep -q "srs_utest.exe$" /tmp/srs-windows-runner-test/timeout.log'
check "utest: passes, with the gtest summary shown" \
  'out | grep -q "^RESULT windows-x64 utest PASS" && out | grep -q "^\[==========\] 3 tests from 2 test suites ran" && out | grep -q "^\[  PASSED  \] 3 tests\.$"'
check "utest: the build and test output goes to the logs, not the console" \
  '! out | grep -q "fake cmake output" && ! out | grep -q "fake utest output" && grep -q "fake cmake output" $FAKE/trunk/objs/windows-utest-build.log && grep -q "fake utest output" $FAKE/trunk/objs/windows-utest.log'
run utest 'WindowsPlatformTest.*:PlatformTest.*'
check "utest: a filter runs only those tests, as --gtest_filter" \
  '[[ $CODE == 0 ]] && [[ $(steps) == "deps build utest" ]] && [[ $(utest_calls) == "--gtest_filter=WindowsPlatformTest.*:PlatformTest.* DIR=$FAKE/trunk" ]]'
run utest 'PlatformTest.*' extra
check "two arguments to utest: exit 3 with usage" '[[ $CODE == 3 ]] && out | grep -q "^Usage: .*utest \[filter\]"'
STUB_UTEST_FAIL=1 run integ
check "utest: two tests fail, exit 1, the failed tests shown, the later steps do not run" \
  '[[ $CODE == 1 ]] && [[ $(steps) == "deps build utest" ]] && out | grep -q "^RESULT windows-x64 utest FAIL" && out | grep -q "^\[  FAILED  \] KernelTest.B$" && out | grep -q "windows-utest.log" && [[ ! -s /tmp/srs-windows-runner-test/go.log ]]'
STUB_UTEST_CRASH=1 run utest
check "utest: srs_utest.exe quits with no summary, exit 1, its exit code and the log tail shown" \
  '[[ $CODE == 1 ]] && out | grep -q "^UTEST srs_utest.exe exited 3 with no summary" && out | grep -q "fake utest output"'
STUB_UTEST_BUILD_EXIT=1 STUB_UTEST_BUILD_OUTPUT='[1/3] Building CXX object a\nFAILED: CMakeFiles/srs_utest.dir/__/__/src/utest/srs_utest_ai01.cpp.obj\nC:/srs/trunk/src/utest/srs_utest_ai01.cpp(9): error C3861: fork: identifier not found\nFAILED: CMakeFiles/srs_utest.dir/__/__/src/utest/srs_utest_ai02.cpp.obj\nC:/srs/trunk/src/utest/srs_utest.hpp(9): fatal error C1083: Cannot open include file\nninja: build stopped: cannot make progress due to previous errors.\n' \
  run utest
check "utest: a file fails, exit 1, the files compiled and each error shown, no run" \
  '[[ $CODE == 1 ]] && out | grep -q "^RESULT windows-x64 utest FAIL" && out | grep -q "^BUILD utest 1 of 3 compiled$" && out | grep -q "error C3861" && out | grep -q "fatal error C1083" && ! out | grep -q "Building CXX" && out | grep -q "windows-utest-build.log" && ! utest_calls | grep -q .'
STUB_SRS_BUILD_EXIT=1 run utest
check "utest: the srs build fails, srs_utest not built" \
  '[[ $CODE == 1 ]] && [[ $(steps) == "deps build" ]] && ! grep -q -- "--target srs_utest" /tmp/srs-windows-runner-test/cmake.log'

# The integ step makes the test media with the srs-integ CLI, then runs the cases against srs.exe, the
# suite's default binary on Windows, with the class limits scaled by 4.
go_calls() { cat /tmp/srs-windows-runner-test/go.log 2>/dev/null; }
run integ 'TestRtmp_|TestHttpFlv_'
check "integ: runs the steps before it, passes" \
  '[[ $CODE == 0 ]] && [[ $(steps) == "deps build utest integ" ]] && out | grep -q "^RESULT windows-x64 integ PASS"'
check "integ: builds the srs-integ CLI into trunk/objs/windows" \
  'go_calls | grep -q "^-C cmd/integ build -o ../../trunk/objs/windows/srs-integ.exe \. DIR=$FAKE$"'
check "integ: makes the test media from the checkout root, before the cases" \
  '[[ $(cat /tmp/srs-windows-runner-test/integ-cli.log 2>/dev/null) == "prepare DIR=$FAKE" ]] && [[ $(go_calls | tail -1) == "-C cmd/integ/cases test "* ]]'
check "integ: the pattern to go test -run, with -integ.limit-scale=4 and no -integ.srs" \
  '[[ $(go_calls | tail -1) == "-C cmd/integ/cases test -count=1 -run TestRtmp_|TestHttpFlv_ -parallel 24 -args -integ.limit-scale=4 DIR=$FAKE" ]]'
check "integ: the media line and the summary shown, the go output in the log" \
  'out | grep -q "^test media in " && out | grep -q "^srs-integ main: 3 cases, 2 passed" && out | grep -q "^  skipped TestHttpFlv_Common/Audio: no tool" && ! out | grep -q "fake go output" && grep -q "fake go output" $FAKE/trunk/objs/windows-integ.log'
check "integ: the slowest cases not shown" '! out | grep -q "120ms Normal"'
STUB_GO_TEST_EXIT=1 run integ 'TestRtmp_'
check "integ: a case fails, exit 1, the later steps do not run" \
  '[[ $CODE == 1 ]] && [[ $(steps) == "deps build utest integ" ]] && out | grep -q "^RESULT windows-x64 integ FAIL"'
check "integ: a case fails, the failed case and the log shown" \
  'out | grep -q "^  failed TestRtmp_Common/Play" && out | grep -q "^--- FAIL: TestRtmp_Common/Play" && out | grep -q "windows-integ.log"'
STUB_PREPARE_EXIT=1 run integ 'TestRtmp_'
check "integ: the test media fails, exit 1, no case runs, the output shown" \
  '[[ $CODE == 1 ]] && ! go_calls | grep -q " test " && out | grep -q "fake prepare output"'
STUB_GO_BUILD_EXIT=1 run integ 'TestRtmp_'
check "integ: the CLI does not build, exit 1, no case runs" \
  '[[ $CODE == 1 ]] && ! go_calls | grep -q " test " && out | grep -q "fake go output"'
run integ
check "integ without a pattern: the main then the timer cases, as CI runs them" \
  '[[ $CODE == 0 ]] && [[ $(cat /tmp/srs-windows-runner-test/make.log 2>/dev/null | tr "\n" "|") == "integ-main INTEG_ARGS=-integ.limit-scale=4|integ-timer INTEG_ARGS=-integ.limit-scale=4|" ]] && out | grep -q "^srs-integ main: " && out | grep -q "^srs-integ timer: "'
STUB_SRS_BUILD_EXIT=1 run integ 'TestRtmp_'
check "integ: the build fails, no case runs" '[[ $CODE == 1 ]] && ! go_calls | grep -q .'
# The obs step runs the OBS cases with obs-windows-test.sh against the srs.exe the build step made.
run obs
check "obs: runs the steps before it, passes" \
  '[[ $CODE == 0 ]] && [[ $(steps) == "deps build utest integ obs" ]] && out | grep -q "^RESULT windows-x64 obs PASS"'
check "obs: runs only the cases of obs-windows-test.sh, once, from the checkout root" \
  '[[ $(cat /tmp/srs-windows-runner-test/obs.log 2>/dev/null) == "--host-cases DIR=$FAKE" ]]'
check "obs: the PASS, KNOWN and LANE lines shown, the rest in the log" \
  'out | grep -q "^PASS API H264 AAC 640x360$" && out | grep -q "^KNOWN F2 API frames 0$" && out | grep -q "^LANE obs passed=3 " && ! out | grep -q "fake obs output" && grep -q "fake obs output" $FAKE/trunk/objs/windows-obs.log'
STUB_OBS_EXIT=1 run obs
check "obs: a case fails, exit 1, the FAIL line and the log shown" \
  '[[ $CODE == 1 ]] && out | grep -q "^RESULT windows-x64 obs FAIL" && out | grep -q "^FAIL http-flv missing" && out | grep -q "windows-obs.log"'
STUB_OBS_SKIP=1 run obs
check "obs: no case ran, such as without OBS, a skip with the reason" \
  '[[ $CODE == 0 ]] && out | grep -q "^RESULT windows-x64 obs SKIP no OBS case ran: OBS not available" && out | grep -q "^SKIP OBS not available$"'
STUB_UTEST_FAIL=1 run obs
check "obs: utest fails, no case runs" '[[ $CODE == 1 ]] && [[ ! -s /tmp/srs-windows-runner-test/obs.log ]]'
# --alone runs one step without the steps before it, for a caller that ran them, such as the Full
# tier's lane W.
rm -rf /tmp/srs-windows-runner-test/checkout/trunk/objs
run build
run --alone utest
check "--alone utest: only utest runs, passes" \
  '[[ $CODE == 0 ]] && [[ $(steps) == utest ]] && out | grep -q "^RESULT windows-x64 utest PASS" && out | tail -1 | grep -q "^RESULT windows-x64 all PASS through utest"'
check "--alone utest: no deps, no srs.exe build or start" \
  '! st_built && ! grep -q -e "^-S " -e "--target srs " -e "--target srs-deps-check" /tmp/srs-windows-runner-test/cmake.log && [[ ! -s /tmp/srs-windows-runner-test/srs.log ]] && [[ -s /tmp/srs-windows-runner-test/utest.log ]]'
run --alone integ 'TestRtmp_'
check "--alone integ: only integ runs, with the pattern" \
  '[[ $CODE == 0 ]] && [[ $(steps) == integ ]] && [[ ! -s /tmp/srs-windows-runner-test/utest.log ]] && go_calls | grep -q " -run TestRtmp_ "'
STUB_GO_TEST_EXIT=1 run --alone integ 'TestRtmp_'
check "--alone integ: a case fails, exit 1" '[[ $CODE == 1 ]] && out | grep -q "^RESULT windows-x64 integ FAIL"'
run --alone
check "--alone without a step: exit 3 with usage" '[[ $CODE == 3 ]] && out | grep -q "^Usage: "'
run --alone utest 'PlatformTest.*' extra
check "--alone, two arguments to utest: exit 3 with usage" '[[ $CODE == 3 ]] && out | grep -q "^Usage: "'

# The full step runs the Full tier from the checkout root, which runs the build, utest, integ and OBS
# steps itself, so the runner does not run them first.
run full
check "full: only the Full tier runs, once, from the checkout root, passes" \
  '[[ $CODE == 0 ]] && [[ $(steps) == full ]] && [[ $(cat /tmp/srs-windows-runner-test/full.log 2>/dev/null) == " DIR=$FAKE" ]] && out | grep -q "^RESULT windows-x64 full PASS"'
check "full: no step before it" '! st_built && [[ ! -s /tmp/srs-windows-runner-test/cmake.log ]] && [[ ! -s /tmp/srs-windows-runner-test/utest.log ]] && ! go_calls | grep -q .'
check "full: the tier output shown" 'out | grep -q "^lock: acquired after 0s$" && out | grep -q "^windows-integ  "'
check "full: a summary line" 'out | tail -1 | grep -q "^RESULT windows-x64 all PASS through full"'
check "full: no plan IDs in the output" '! out | grep -qE "\bP[0-9]+(\.[0-9]+)?\b"'
STUB_FULL_EXIT=1 run full
check "full: the tier fails, exit 1" '[[ $CODE == 1 ]] && out | grep -q "^RESULT windows-x64 full FAIL"'

# The package step builds srs.exe for a release, after deps and build but without the test steps and
# options, and zips it with the files make install takes on Unix as SRS-Windows-x86_64-<version>.zip.
PKG=$FAKE/trunk/objs/windows/package
P=SRS-Windows-x86_64-8.0.64
zip_files() { zipinfo -1 "$1" 2>/dev/null | grep -v '/$' | LC_ALL=C sort | tr '\n' ' '; }
WANT="$P/conf/console.conf $P/conf/server.crt $P/conf/server.key $P/conf/srs.conf"
WANT="$WANT $P/objs/nginx/html/console/index.html $P/objs/nginx/html/demos/index.html"
WANT="$WANT $P/objs/nginx/html/favicon.ico $P/objs/nginx/html/index.html"
WANT="$WANT $P/objs/nginx/html/players/js/srs.sdk.js $P/objs/srs.exe "
rm -rf /tmp/srs-windows-runner-test/checkout/trunk/objs
run package
check "package: runs deps and build before it, not the test steps, passes" \
  '[[ $CODE == 0 ]] && [[ $(steps) == "deps build package" ]] && out | grep -q "^RESULT windows-x64 package PASS" && out | tail -1 | grep -q "^RESULT windows-x64 all PASS through package" && [[ ! -s /tmp/srs-windows-runner-test/utest.log ]] && ! go_calls | grep -q .'
check "package: configures with cl, without the test options, with GB28181 and RTSP" \
  '[[ $(grep "^-S " /tmp/srs-windows-runner-test/cmake.log) == "-S trunk/cmake/windows -B trunk/objs/windows -G Ninja -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DSRS_UTEST=OFF -DSRS_SIMULATOR=OFF -DSRS_GB28181=ON -DSRS_RTSP=ON" ]]'
check "package: srs.exe started after the build, as for the tests" 'srs_calls | grep -q "^-e "'
check "package: the zip named for the version srs.exe prints, its path shown" \
  '[[ -f $PKG/$P.zip ]] && out | grep -q "^PACKAGE trunk/objs/windows/package/$P.zip$"'
check "package: the zip made by the Windows tar in the package dir" \
  '[[ $(cat /tmp/srs-windows-runner-test/tar.log 2>/dev/null) == "-a -c -f $P.zip $P DIR=$PKG" ]]'
check "package: srs.exe, the config files with the certificate, and the http root in the zip" \
  '[[ $(zip_files $PKG/$P.zip) == "$WANT" ]]'
check "package: the DLLs of the packaged srs.exe are all in System32" \
  '[[ $(cat /tmp/srs-windows-runner-test/dumpbin.log 2>/dev/null) == "-nologo -dependents $P/objs/srs.exe" ]] && out | grep -q "^PACKAGE dlls PASS: WS2_32.dll KERNEL32.dll$"'
check "package: the packaged srs.exe checks conf/console.conf in the package dir" \
  'srs_calls | grep -q "^-t -c conf/console.conf DIR=$PKG/$P " && out | grep -q "^PACKAGE config PASS: conf/console.conf$"'
mkdir -p $PKG/$P/objs && touch $PKG/$P/objs/old.exe
run package 8.0-r1
check "package: the version from the argument, no file of an earlier package" \
  '[[ $CODE == 0 ]] && [[ -f $PKG/SRS-Windows-x86_64-8.0-r1.zip ]] && ! zip_files $PKG/SRS-Windows-x86_64-8.0-r1.zip | grep -q old.exe && [[ ! -e $PKG/$P ]]'
STUB_DLL=libcrypto-3-x64.dll run package
check "package: a DLL not in System32, exit 1 naming it, no zip" \
  '[[ $CODE == 1 ]] && out | grep -q "^PACKAGE dlls FAIL: libcrypto-3-x64.dll" && [[ ! -f $PKG/$P.zip ]] && [[ ! -s /tmp/srs-windows-runner-test/tar.log ]]'
STUB_SRS_CONSOLE_FAIL=1 run package
check "package: the packaged srs.exe refuses conf/console.conf, exit 1, its output shown, no zip" \
  '[[ $CODE == 1 ]] && out | grep -q "^PACKAGE config FAIL: conf/console.conf" && out | grep -q "fake srs output" && [[ ! -f $PKG/$P.zip ]]'
STUB_TAR_FAIL=1 run package
check "package: the zip fails, exit 1" '[[ $CODE == 1 ]] && out | grep -q "^RESULT windows-x64 package FAIL"'
STUB_SRS_BUILD_EXIT=1 run package
check "package: the build fails, nothing packaged" \
  '[[ $CODE == 1 ]] && [[ $(steps) == "deps build" ]] && [[ ! -s /tmp/srs-windows-runner-test/tar.log ]]'
run package
run --alone package
check "--alone package: after a release build, only package runs, passes" \
  '[[ $CODE == 0 ]] && [[ $(steps) == package ]] && [[ ! -s /tmp/srs-windows-runner-test/cmake.log ]] && [[ -f $PKG/$P.zip ]]'
run build
run --alone package
check "--alone package: after a build for the tests, exit 1, nothing packaged" \
  '[[ $CODE == 1 ]] && out | grep -q "^PACKAGE build FAIL: .*SRS_UTEST" && [[ ! -s /tmp/srs-windows-runner-test/tar.log ]]'

# The vs step generates the Visual Studio solution after deps, with the options of the tests and the
# generator CMake picks, and builds or runs nothing.
rm -rf /tmp/srs-windows-runner-test/checkout/trunk/objs
run vs
check "vs: runs deps before it, not the build or test steps, passes" \
  '[[ $CODE == 0 ]] && [[ $(steps) == "deps vs" ]] && out | tail -1 | grep -q "^RESULT windows-x64 all PASS through vs" && [[ -z $(srs_calls) ]] && [[ ! -s /tmp/srs-windows-runner-test/utest.log ]]'
check "vs: configures trunk/objs/windows-vs for x64 with the test options and no -G, so the newest Visual Studio" \
  '[[ $(grep -- "-B trunk/objs/windows-vs " /tmp/srs-windows-runner-test/cmake.log) == "-S trunk/cmake/windows -B trunk/objs/windows-vs -A x64 -DSRS_UTEST=ON -DSRS_SIMULATOR=ON -DSRS_GB28181=ON -DSRS_RTSP=ON" ]]'
check "vs: builds nothing" '! grep -q -- "--build .*windows-vs" /tmp/srs-windows-runner-test/cmake.log'
check "vs: the generator and the solution path shown" \
  'out | grep -q "^VS Visual Studio 18 2026: .*trunk/objs/windows-vs/srs.slnx$"'
check "vs: the cmake output goes to the log, not the console" '! out | grep -q "fake cmake output"'
VS_WANT=$(printf '%s\n' '' '<Solution>' \
  '  <Project Path="../trunk/objs/windows-vs/ALL_BUILD.vcxproj" Id="f780">' \
  '    <BuildDependency Project="../trunk/objs/windows-vs/ZERO_CHECK.vcxproj" />' \
  '    <BuildDependency Project="../trunk/objs/windows-vs/srs.vcxproj" />' \
  '    <Build Project="false" />' \
  '  </Project>' \
  '  <Project Path="../trunk/objs/windows-vs/deps-check/deps-check-st.vcxproj" Id="268b" />' \
  '  <Project Path="../trunk/objs/windows-vs/srs.vcxproj" DefaultStartup="true" Id="f8a6" />' \
  '</Solution>')
VS_SLNX=/tmp/srs-windows-runner-test/checkout/.vs/srs.slnx
check "vs: writes .vs/srs.slnx as Visual Studio saves it, the project paths relative to .vs, no CRs" \
  '[[ $(cat $VS_SLNX) == "$VS_WANT" ]] && ! grep -q $'"'"'\r'"'"' $VS_SLNX && out | grep -q "^VS .vs/srs.slnx written, commit it if git shows a change: .*/.vs/srs.slnx$"'
run --alone vs
check "vs: .vs/srs.slnx up to date, not written again" \
  '[[ $CODE == 0 ]] && [[ $(cat $VS_SLNX) == "$VS_WANT" ]] && out | grep -q "^VS .vs/srs.slnx is up to date: "'
echo stale > $VS_SLNX
run --alone vs
check "vs: a stale .vs/srs.slnx written again" '[[ $(cat $VS_SLNX) == "$VS_WANT" ]] && out | grep -q "^VS .vs/srs.slnx written, "'
rm -f /tmp/srs-windows-runner-test/checkout/trunk/objs/windows-vs/srs.slnx
STUB_VS_SLN=1 run --alone vs
check "vs: an older Visual Studio makes srs.sln, which is shown, .vs/srs.slnx not written, passes" \
  '[[ $CODE == 0 ]] && out | grep -q "^VS .*trunk/objs/windows-vs/srs.sln$" && out | grep -q "^VS .vs/srs.slnx not written: " && [[ $(cat $VS_SLNX) == "$VS_WANT" ]]'
rm -f /tmp/srs-windows-runner-test/checkout/trunk/objs/windows-vs/srs.sln
rm -f /tmp/srs-windows-runner-test/checkout/trunk/objs/windows-vs/srs.slnx
STUB_VS_NO_SLN=1 run --alone vs
check "--alone vs: only vs runs" '[[ $(steps) == vs ]] && [[ $(grep -c "^-S " /tmp/srs-windows-runner-test/cmake.log) == 1 ]]'
check "vs: no solution generated, exit 1" '[[ $CODE == 1 ]] && out | grep -q "^VS solution FAIL: "'
STUB_CMAKE_EXIT=1 run --alone vs
check "vs: cmake fails, exit 1, the log tail shown" \
  '[[ $CODE == 1 ]] && out | grep -q "fake cmake output" && out | grep -q "^RESULT windows-x64 vs FAIL"'
run vs x
check "argument to vs: exit 3 with usage" '[[ $CODE == 3 ]] && out | grep -q "^Usage: .*|vs"'

echo "SUMMARY passed=$PASSED failed=$FAILED"
[[ $FAILED == 0 ]]
