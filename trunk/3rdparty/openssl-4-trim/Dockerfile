# The cross compilers for the Linux targets of linux-all.sh.
#   docker build -t srs-openssl-trim:local trunk/3rdparty/openssl-4-trim
FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential perl make \
    gcc-x86-64-linux-gnu gcc-aarch64-linux-gnu gcc-arm-linux-gnueabihf gcc-mipsel-linux-gnu \
    gcc-mips64el-linux-gnuabi64 gcc-riscv64-linux-gnu gcc-14-loongarch64-linux-gnu \
    libc6-dev-amd64-cross libc6-dev-arm64-cross libc6-dev-armhf-cross libc6-dev-mipsel-cross \
    libc6-dev-mips64el-cross libc6-dev-riscv64-cross libc6-dev-loong64-cross \
    && rm -rf /var/lib/apt/lists/*
# Ubuntu 24.04 names the LoongArch compiler by its version only; Configure looks for <prefix>gcc.
RUN ln -s loongarch64-linux-gnu-gcc-14 /usr/bin/loongarch64-linux-gnu-gcc
