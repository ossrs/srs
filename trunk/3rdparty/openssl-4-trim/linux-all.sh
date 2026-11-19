#!/bin/bash
# Runs inside the srs-openssl-trim container: every Linux target trunk/auto/depends.sh builds
# openssl-4-fit for. /src is the tree (read only), /trim holds these scripts, /out gets the results.
# The builds run on the container's own disk, where atime works.
set -u
: "${OPTIONS:?set OPTIONS, see options.sh}"
W=/work
mkdir -p $W /out
FAILED=0
run() {
    local name=$1
    bash /trim/inventory.sh /src $W "$@" 2>&1 | tee -a /out/summary.txt
    [[ ${PIPESTATUS[0]} -eq 0 ]] || FAILED=1
    perl /trim/verify.pl $W/$name 2>&1 | sed "s|$W/||" | tee -a /out/summary.txt
    [[ ${PIPESTATUS[0]} -eq 0 ]] || FAILED=1
    mkdir -p /out/$name
    cp $W/$name/read.txt $W/$name/all.txt $W/$name/configdata.pm $W/$name/libs.txt \
       $W/$name/configure.log /out/$name/ 2>/dev/null
    cp $W/$name/make.log /out/$name/ 2>/dev/null
    rm -rf $W/$name/tree $W/$name/prefix
}
: > /out/summary.txt
# Native: ./config picks the host's target, as a native SRS build runs it.
run linux-native gcc ./config
# Linux x86_64 and aarch64, which ./config picks on those hosts.
run linux-x86_64 x86_64-linux-gnu-gcc ./Configure linux-x86_64 --cross-compile-prefix=x86_64-linux-gnu-
run linux-aarch64 aarch64-linux-gnu-gcc ./Configure linux-aarch64 --cross-compile-prefix=aarch64-linux-gnu-
# The --cross-build targets, with a compiler of each arch.
run linux-armv4 arm-linux-gnueabihf-gcc ./Configure linux-armv4 --cross-compile-prefix=arm-linux-gnueabihf-
run linux-mips32 mipsel-linux-gnu-gcc ./Configure linux-mips32 --cross-compile-prefix=mipsel-linux-gnu-
run linux-generic32 arm-linux-gnueabihf-gcc ./Configure linux-generic32 --cross-compile-prefix=arm-linux-gnueabihf-
# OS_IS_MIPS64 hosts use ./Configure linux64-mips64; RISC-V and LoongArch hosts run ./config.
run linux64-mips64 mips64el-linux-gnuabi64-gcc ./Configure linux64-mips64 --cross-compile-prefix=mips64el-linux-gnuabi64-
run linux64-riscv64 riscv64-linux-gnu-gcc ./Configure linux64-riscv64 --cross-compile-prefix=riscv64-linux-gnu-
run linux64-loongarch64 loongarch64-linux-gnu-gcc ./Configure linux64-loongarch64 --cross-compile-prefix=loongarch64-linux-gnu-
echo "DONE failed=$FAILED" | tee -a /out/summary.txt
exit $FAILED
