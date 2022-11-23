# state-threads

![](http://ossrs.net:8000/gif/v1/sls.gif?site=github.com&path=/srs/srsst)
[![](https://github.com/ossrs/state-threads/actions/workflows/test.yml/badge.svg?branch=srs)](https://github.com/ossrs/state-threads/actions?query=workflow%3ATest+branch%3Asrs)
[![](https://codecov.io/gh/ossrs/state-threads/branch/srs/graph/badge.svg)](https://codecov.io/gh/ossrs/state-threads/branch/srs)
[![](https://cloud.githubusercontent.com/assets/2777660/22814959/c51cbe72-ef92-11e6-81cc-32b657b285d5.png)](https://ossrs.net/lts/zh-cn/contact)

Fork from http://sourceforge.net/projects/state-threads, patched for [SRS](https://github.com/ossrs/srs/tree/2.0release).

> See: https://github.com/ossrs/state-threads/blob/srs/README

For original ST without any changes, checkout the [ST master branch](https://github.com/ossrs/state-threads/tree/master).

## LICENSE

[state-threads](https://github.com/ossrs/state-threads/blob/srs/README#L68) is licenced under [MPL or GPLv2](https://ossrs.net/lts/zh-cn/license#state-threads).

## Linux: Usage

Get code:

```bash
git clone -b srs https://github.com/ossrs/state-threads.git
```

For Linux:

```bash
make linux-debug
```

For Linux aarch64, which fail with `Unknown CPU architecture`:

```bash
make linux-debug EXTRA_CFLAGS="-D__aarch64__"
```

> Note: For more CPU architectures, please see [#22](https://github.com/ossrs/state-threads/issues/22)

Linux with valgrind:

```bash
make linux-debug EXTRA_CFLAGS="-DMD_VALGRIND"
```

> Remark: User must install valgrind, for instance, in centos6 `sudo yum install -y valgrind valgrind-devel`.

Linux with valgrind and epoll:

```bash
make linux-debug EXTRA_CFLAGS="-DMD_HAVE_EPOLL -DMD_VALGRIND"
```

## Mac: Usage

Get code:

```bash
git clone -b srs https://github.com/ossrs/state-threads.git
```

For OSX:

```bash
make darwin-debug
```

For OSX, user must specifies the valgrind header files:

```bash
make darwin-debug EXTRA_CFLAGS="-DMD_HAVE_KQUEUE -DMD_VALGRIND -I/usr/local/include"
```

> Remark: M1 is unsupported by ST, please use docker to run, please read [SRS#2747](https://github.com/ossrs/srs/issues/2747).

## Windows: Usage

Get code:

```bash
git clone -b srs https://github.com/ossrs/state-threads.git
```

For Cygwin(Windows):

```
make cygwin64-debug
```

> Remark: Windows native build is unsupported right now.

## Branch SRS

The branch [srs](https://github.com/ossrs/state-threads/tree/srs) was patched and refined:

- [x] ARM: Patch [st.arm.patch](https://github.com/ossrs/srs/blob/2.0release/trunk/3rdparty/patches/1.st.arm.patch), for ARM.
- [x] OSX: Patch [st.osx.kqueue.patch](https://github.com/ossrs/srs/blob/2.0release/trunk/3rdparty/patches/3.st.osx.kqueue.patch), for osx.
- [x] Linux: Patch [st.disable.examples.patch](https://github.com/ossrs/srs/blob/2.0release/trunk/3rdparty/patches/4.st.disable.examples.patch), for ubuntu.
- [x] System: [Refine TAB of code](https://github.com/ossrs/state-threads/compare/c2001d30ca58f55d72a6cc6b9b6c70391eaf14db...d2101b26988b0e0db0aabc53ddf452068c1e2cbc).
- [x] ARM: Merge from [michaeltalyansky](https://github.com/michaeltalyansky/state-threads) and [xzh3836598](https://github.com/ossrs/state-threads/commit/9a17dec8f9c2814d93761665df7c5575a4d2d8a3), support [ARM](https://github.com/ossrs/state-threads/issues/1).
- [x] Valgrind: Merge from [toffaletti](https://github.com/toffaletti/state-threads), support [valgrind](https://github.com/ossrs/state-threads/issues/2) for ST.
- [x] OSX: Patch [st.osx10.14.build.patch](https://github.com/ossrs/srs/blob/2.0release/trunk/3rdparty/patches/6.st.osx10.14.build.patch), for osx 10.14 build.
- [x] ARM: Support macro `MD_ST_NO_ASM` to disable ASM, [#8](https://github.com/ossrs/state-threads/issues/8).
- [x] AARCH64: Merge patch [srs#1282](https://github.com/ossrs/srs/issues/1282#issuecomment-445539513) to support aarch64, [#9](https://github.com/ossrs/state-threads/issues/9).
- [x] OSX: Support OSX for Apple Darwin, macOS, [#11](https://github.com/ossrs/state-threads/issues/11).
- [x] System: Refine performance for sleep or epoll_wait(0), [#17](https://github.com/ossrs/state-threads/issues/17).
- [x] System: Support utest by gtest and coverage by gcov/gocvr.
- [x] System: Only support for Linux and Darwin. [#19](https://github.com/ossrs/state-threads/issues/19), [srs#2188](https://github.com/ossrs/srs/issues/2188).
- [x] System: Improve the performance of timer. [9fe8cfe5b](https://github.com/ossrs/state-threads/commit/9fe8cfe5b1c9741a2e671a46215184f267fba400), [7879c2b](https://github.com/ossrs/state-threads/commit/7879c2b), [387cddb](https://github.com/ossrs/state-threads/commit/387cddb)
- [x] Windows: Support Windows 64bits. [#20](https://github.com/ossrs/state-threads/issues/20).
- [x] MIPS: Support Linux/MIPS for OpenWRT, [#21](https://github.com/ossrs/state-threads/issues/21).
- [x] LOONGARCH: Support loongarch for loongson CPU, [#24](https://github.com/ossrs/state-threads/issues/24). 
- [x] System: Support Multiple Threads for Linux and Darwin. [#19](https://github.com/ossrs/state-threads/issues/19), [srs#2188](https://github.com/ossrs/srs/issues/2188).
- [x] RISCV: Support RISCV for RISCV CPU, [#24](https://github.com/ossrs/state-threads/pull/28).
- [x] MIPS: Support Linux/MIPS64 for loongson 3A4000/3B3000, [#21](https://github.com/ossrs/state-threads/pull/21).
- [x] AppleM1: Support Apple Silicon M1(aarch64), [#30](https://github.com/ossrs/state-threads/issues/30).
- [x] IDE: Support CLion for debugging and learning.
- [x] Define and use a new jmpbuf, because the structure is different.
- [x] Check capability for backtrack.
- [x] Support set specifics for any thread.
- [x] Support st_destroy to free resources for asan.
- [ ] System: Support sendmmsg for UDP, [#12](https://github.com/ossrs/state-threads/issues/12).

## GDB Tools

- [x] Support [nn_coroutines](https://github.com/ossrs/state-threads/issues/15#issuecomment-742218041), show number of coroutines.
- [x] Support [show_coroutines](https://github.com/ossrs/state-threads/issues/15#issuecomment-742218612), show all coroutines and caller function.

## Valgrind

How to debug with gdb under valgrind, read [valgrind manual](http://valgrind.org/docs/manual/manual-core-adv.html#manual-core-adv.gdbserver-simple).

About startup parameters, read [valgrind cli](http://valgrind.org/docs/manual/mc-manual.html#mc-manual.options).

Important cli options:

1. `--undef-value-errors=<yes|no> [default: yes]`, Controls whether Memcheck reports uses of undefined value errors. Set this to no if you don't want to see undefined value errors. It also has the side effect of speeding up Memcheck somewhat.
1. `--leak-check=<no|summary|yes|full> [default: summary]`, When enabled, search for memory leaks when the client program finishes. If set to summary, it says how many leaks occurred. If set to full or yes, each individual leak will be shown in detail and/or counted as an error, as specified by the options `--show-leak-kinds` and `--errors-for-leak-kinds`.
1. `--track-origins=<yes|no> [default: no]`, Controls whether Memcheck tracks the origin of uninitialised values. By default, it does not, which means that although it can tell you that an uninitialised value is being used in a dangerous way, it cannot tell you where the uninitialised value came from. This often makes it difficult to track down the root problem.
1. `--show-reachable=<yes|no> , --show-possibly-lost=<yes|no>`, to show the using memory.

## Linux: UTest

> Note: We use [Google test](https://github.com/google/googletest/releases/tag/release-1.11.0) in `utest/gtest-fit`.

To make ST with utest and run it:

```bash
make linux-debug-utest && ./obj/st_utest
```

Note that the gcc(4.8) of CentOS is too old, please use docker(`ossrs/srs:dev-gcc7`) to run:

```bash
docker run --rm -it -v $(pwd):/state-threads -w /state-threads \
    registry.cn-hangzhou.aliyuncs.com/ossrs/srs:dev-gcc7 \
    bash -c 'make linux-debug-utest && ./obj/st_utest'
```

## Mac: UTest

> Note: We use [Google test](https://github.com/google/googletest/releases/tag/release-1.11.0) in `utest/gtest-fit`.

To make ST with utest and run it:

```bash
make darwin-debug-utest && ./obj/st_utest
```

## Linux: Coverage

> Note: We use [Google test](https://github.com/google/googletest/releases/tag/release-1.11.0) in `utest/gtest-fit`.

To make ST with utest and run it:

```bash
make linux-debug-gcov && ./obj/st_utest
```

Note that the gcc(4.8) of CentOS is too old, please use docker(`ossrs/srs:dev-gcc7`) to run:

```bash
docker run --rm -it -v $(pwd):/state-threads -w /state-threads \
    registry.cn-hangzhou.aliyuncs.com/ossrs/srs:dev-gcc7 \
    bash -c 'make linux-debug-gcov && ./obj/st_utest'
```

Then, install [gcovr](https://gcovr.com/en/stable/guide.html) for coverage:

```bash
yum install -y python2-pip &&
pip install lxml && pip install gcovr
```

Finally, run test and get the report:

```bash
bash auto/coverage.sh
```

## Mac: Coverage

> Note: We use [Google test](https://github.com/google/googletest/releases/tag/release-1.11.0) in `utest/gtest-fit`.

To make ST with utest and run it:

```bash
make darwin-debug-gcov && ./obj/st_utest
```

Then, install [gcovr](https://gcovr.com/en/stable/guide.html) for coverage:

```bash
pip install gcovr
```

Finally, run test and get the report:

```bash
bash auto/coverage.sh
```

## Docs & Analysis

* Introduction: http://ossrs.github.io/state-threads/docs/st.html
* API reference: http://ossrs.github.io/state-threads/docs/reference.html
* Programming notes: http://ossrs.github.io/state-threads/docs/notes.html

* [How to porting ST to other OS/CPU?](https://github.com/ossrs/state-threads/issues/22)
* About setjmp and longjmp, read [setjmp](https://gitee.com/winlinvip/srs-wiki/raw/master/images/st-setjmp.jpg).
* About the stack structure, read [stack](https://gitee.com/winlinvip/srs-wiki/raw/master/images/st-stack.jpg)
* About asm code comments, read [#91d530e](https://github.com/ossrs/state-threads/commit/91d530e#diff-ed9428b14ff6afda0e9ab04cc91d4445R25).
* About the scheduler, read [#13-scheduler](https://github.com/ossrs/state-threads/issues/13#issuecomment-616025527).
* About the IO event system, read [#13-IO](https://github.com/ossrs/state-threads/issues/13#issuecomment-616096568).
* Code analysis, please read [#15](https://github.com/ossrs/state-threads/issues/15).

## CLion

Use [CLion](https://www.jetbrains.com/clion/) to open directory state-threads.

Then, open `ide/st_clion/CMakeLists.txt` and click `Load CMake project`.

Finally, select a configuration to run or debug.

Winlin 2016
