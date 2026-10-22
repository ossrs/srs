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
# programs.
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

# The fake cmake logs its arguments, one call per line. A configure exits with STUB_CMAKE_EXIT. A
# --build exits with STUB_CMAKE_BUILD_EXIT and makes the six link-check programs, except the one named
# STUB_CHECK_MISSING. Each one logs its name, prints two lines, and exits 1 when its name is
# STUB_CHECK_FAIL.
cat > /tmp/srs-windows-runner-test/bin/cmake <<'EOF'
#!/bin/bash
echo "$*" >> /tmp/srs-windows-runner-test/cmake.log
echo "fake cmake output"
[[ $1 == --build ]] || exit ${STUB_CMAKE_EXIT:-0}
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

# The fake cygpath prints the path unchanged, as if Windows and MSYS2 paths were the same.
cat > /tmp/srs-windows-runner-test/bin/cygpath <<'EOF'
#!/bin/bash
echo "${@: -1}"
EOF
chmod +x /tmp/srs-windows-runner-test/bin/cygpath

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
mkdir -p $FAKE/trunk/src/core $FAKE/trunk/3rdparty/ffmpeg-4-fit
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
git -C $FAKE init -q
git -C $FAKE add -A
git -C $FAKE -c user.name=test -c user.email=test@localhost commit -q -m init

out() { cat /tmp/srs-windows-runner-test/out; }
# Run the runner in the fake checkout with the fake uname, cmake and cygpath on PATH.
run() {
  rm -f /tmp/srs-windows-runner-test/cmake.log /tmp/srs-windows-runner-test/configure.log \
    /tmp/srs-windows-runner-test/pkg-config-path.log /tmp/srs-windows-runner-test/st-make.log \
    /tmp/srs-windows-runner-test/checks.log
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
check "deps: passes" 'out | grep -q "^RESULT windows-x64 deps PASS"'
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

# The steps after deps are not built yet, so each says not yet and exits 0.
run build
check "build: not yet" 'out | grep -q "^RESULT windows-x64 build SKIP not yet"'
run build
check "build: runs deps first" '[[ $CODE == 0 ]] && [[ $(steps) == "deps build" ]]'
run utest
check "utest: runs deps and build first" '[[ $CODE == 0 ]] && [[ $(steps) == "deps build utest" ]]'
run integ
check "integ: runs the steps before it" '[[ $CODE == 0 ]] && [[ $(steps) == "deps build utest integ" ]]'
run integ 'TestRtmp_|TestHttpFlv_'
check "integ with a pattern: the pattern shown" '[[ $CODE == 0 ]] && out | grep -q "^RESULT windows-x64 integ SKIP not yet.*TestRtmp_|TestHttpFlv_"'
run obs
check "obs: runs the steps before it" '[[ $CODE == 0 ]] && [[ $(steps) == "deps build utest integ obs" ]]'
run full
check "full: runs every step" '[[ $CODE == 0 ]] && [[ $(steps) == "deps build utest integ obs full" ]]'
check "full: a summary line" 'out | tail -1 | grep -q "^RESULT windows-x64 all PASS"'
check "full: no plan IDs in the output" '! out | grep -qE "\bP[0-9]+(\.[0-9]+)?\b"'

echo "SUMMARY passed=$PASSED failed=$FAILED"
[[ $FAILED == 0 ]]
