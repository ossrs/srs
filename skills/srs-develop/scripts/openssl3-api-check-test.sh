#!/bin/bash
# Test openssl3-api-check.sh without the SRS sources or a real OpenSSL 3: a fake trunk has an
# objs/Makefile with three sources, and fake OpenSSL 1.1 and 3 headers stand in for the real ones.
#
#   openssl3-api-check-test.sh
#
# Covers the argument and setup checks, that the OpenSSL 3 headers replace the bundled ones, the
# deprecated and removed uses found and counted, a clean tree passing, and a source that does not
# compile.
#
# Prints PASS or FAIL per check, then a summary. Exit 0 when every check passed, 1 otherwise.

SCRIPTS=$(cd "$(dirname "$0")" && pwd)
CHECK="$SCRIPTS/openssl3-api-check.sh"
T=/tmp/openssl3-api-check-test
PASSED=0
FAILED=0

pass() { echo "PASS $1"; PASSED=$((PASSED + 1)); }
fail() { echo "FAIL $1"; sed 's/^/  | /' $T/out; FAILED=$((FAILED + 1)); }
check() { if eval "$2"; then pass "$1"; else fail "$1"; fi; }

rm -rf $T
mkdir -p $T/trunk/objs/openssl/include/openssl $T/trunk/src/app $T/trunk/src/kernel $T/ossl3/openssl $T/ossl1/openssl

# The bundled OpenSSL 1.1 headers declare everything without deprecation, so a check that keeps them
# finds nothing.
cat > $T/trunk/objs/openssl/include/openssl/opensslv.h <<'EOF'
#define OPENSSL_VERSION_NUMBER 0x1010100fL
#define OPENSSL_VERSION_TEXT "OpenSSL 1.1.1l  24 Aug 2021"
EOF
cat > $T/trunk/objs/openssl/include/openssl/fake.h <<'EOF'
int OLD_fn(int a);
int NEW_fn(int a);
#define OLD_MACRO(a) ((a) != 0)
EOF
cp $T/trunk/objs/openssl/include/openssl/* $T/ossl1/openssl/

# The OpenSSL 3 headers: OLD_fn is deprecated, and both it and OLD_MACRO go away with
# OPENSSL_NO_DEPRECATED, as the 3.x headers do.
cat > $T/ossl3/openssl/opensslv.h <<'EOF'
#define OPENSSL_VERSION_NUMBER 0x30600040L
#define OPENSSL_VERSION_TEXT "OpenSSL 3.6.4 30 Sep 2026"
EOF
cat > $T/ossl3/openssl/fake.h <<'EOF'
#ifndef OPENSSL_NO_DEPRECATED
__attribute__((deprecated("Since OpenSSL 3.0"))) int OLD_fn(int a);
#define OLD_MACRO(a) ((a) != 0)
#endif
int NEW_fn(int a);
EOF

# a.cpp uses OLD_fn twice and OLD_MACRO once, b.cpp uses OLD_fn once, c.cpp uses only NEW_fn.
cat > $T/trunk/src/app/a.cpp <<'EOF'
#include <openssl/fake.h>
int a1() { return OLD_fn(1); }
int a2() { return OLD_fn(2) + OLD_MACRO(3); }
EOF
cat > $T/trunk/src/kernel/b.cpp <<'EOF'
#include <openssl/fake.h>
int b1() { return OLD_fn(1); }
EOF
cat > $T/trunk/src/kernel/c.cpp <<'EOF'
#include <openssl/fake.h>
int c1() { return NEW_fn(1); }
EOF
cat > $T/trunk/objs/Makefile <<'EOF'
CXX = c++
CXXFLAGS = -ansi -Wall -g -O0 -fsanitize=address -fno-omit-frame-pointer
INCS = -I./src/app -I./src/kernel -I./objs -I./objs/openssl/include

default: ./objs/src/app/a.o ./objs/src/kernel/b.o ./objs/src/kernel/c.o

./objs/src/app/a.o: ./src/app/a.cpp
	$(CXX) -c $(CXXFLAGS) \
    $(INCS)\
    -o ./objs/src/app/a.o \
    ./src/app/a.cpp
./objs/src/kernel/b.o: ./src/kernel/b.cpp
	$(CXX) -c $(CXXFLAGS) \
    $(INCS)\
    -o ./objs/src/kernel/b.o \
    ./src/kernel/b.cpp
./objs/src/kernel/c.o: ./src/kernel/c.cpp
	$(CXX) -c $(CXXFLAGS) \
    $(INCS)\
    -o ./objs/src/kernel/c.o \
    ./src/kernel/c.cpp
EOF

out() { cat $T/out; }
run() {
  (cd "$1" && shift && bash "$CHECK" "$@") > $T/out 2>&1
  CODE=$?
}

# Arguments and setup.
run $T/trunk extra args
check "bad arguments: exit 3 with usage" '[[ $CODE == 3 ]] && out | grep -q "^Usage: "'
run $T
check "not in a configured trunk: exit 3" '[[ $CODE == 3 ]] && out | grep -q "^SETUP_FAIL: "'
run $T/trunk $T/nowhere
check "no OpenSSL headers: skip, exit 2" '[[ $CODE == 2 ]] && out | grep -q "^SKIP openssl3: "'
run $T/trunk $T/ossl1
check "OpenSSL 1.1 headers: skip, exit 2" '[[ $CODE == 2 ]] && out | grep -q "^SKIP openssl3: .*1.1.1l"'

# The deprecated uses in a.cpp and b.cpp.
run $T/trunk $T/ossl3
check "deprecated uses: exit 1" '[[ $CODE == 1 ]]'
check "the OpenSSL 3 version is printed" 'out | grep -q "^OPENSSL3 .*OpenSSL 3.6.4"'
check "every deprecated use, with file and line" 'out | grep -q "^DEPRECATED src/app/a.cpp:2 OLD_fn$" && out | grep -q "^DEPRECATED src/app/a.cpp:3 OLD_fn$" && out | grep -q "^DEPRECATED src/kernel/b.cpp:2 OLD_fn$"'
check "three deprecated lines" '[[ $(out | grep -c "^DEPRECATED ") == 3 ]]'
check "the macro gone without deprecated APIs is found" 'out | grep -q "^REMOVED src/app/a.cpp:3 OLD_MACRO$"'
check "the clean source is not listed" '! out | grep -q "c.cpp"'
check "a summary per API" 'out | grep -q "^API OLD_fn 3 uses in src/app/a.cpp src/kernel/b.cpp$" && out | grep -q "^API OLD_MACRO 1 uses in src/app/a.cpp$"'
check "RESULT FAIL with the counts" 'out | grep -q "^RESULT openssl3-api FAIL 3 deprecated and 4 removed uses of 2 APIs in 2 files, 3 sources checked"'
check "no object files written" '! ls $T/trunk/objs/src >/dev/null 2>&1'

# The default include dir comes from SRS_OPENSSL3_INCLUDE.
(cd $T/trunk && SRS_OPENSSL3_INCLUDE=$T/ossl3 bash "$CHECK") > $T/out 2>&1
CODE=$?
check "SRS_OPENSSL3_INCLUDE is the default" '[[ $CODE == 1 ]] && out | grep -q "^OPENSSL3 $T/ossl3 "'

# A tree with no deprecated use passes.
cp $T/trunk/src/kernel/c.cpp $T/trunk/src/app/a.cpp
cp $T/trunk/src/kernel/c.cpp $T/trunk/src/kernel/b.cpp
run $T/trunk $T/ossl3
check "no deprecated use: exit 0 and RESULT PASS" '[[ $CODE == 0 ]] && out | grep -q "^RESULT openssl3-api PASS 0 deprecated and 0 removed uses of 0 APIs in 0 files, 3 sources checked"'

# A source that does not compile for another reason fails the check and says where.
echo 'int broken() { return undeclared_thing; }' >> $T/trunk/src/kernel/b.cpp
run $T/trunk $T/ossl3
check "compile error: exit 1 with an ERROR line" '[[ $CODE == 1 ]] && out | grep -q "^ERROR src/kernel/b.cpp:3 " && out | grep -q "^RESULT openssl3-api FAIL .*3 sources checked, 1 do not compile"'

echo "SUMMARY passed=$PASSED failed=$FAILED"
[[ $FAILED == 0 ]]
