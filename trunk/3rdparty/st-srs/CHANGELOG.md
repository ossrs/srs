# Changelog

The changelog for the [srs](https://github.com/ossrs/state-threads/tree/srs) branch of State Threads (ST).

## ST 1.9 Changelog

* 2026-10-03, Windows: Support native Windows x64 with MSVC, MASM, and WSAPoll, built by GNU make. v1.9.2
* 2026-10-03, Tools: Add integration tools for every public API, run by auto/tools.sh in CI, [#41](https://github.com/ossrs/state-threads/pull/41). v1.9.1
* 2026-09-30, Changelog: Move the change list from README to CHANGELOG.md, and add the patch version. v1.9.0

## Before v1.9.0

Changes to the srs branch before it had patch versions, newest first:

* Support free the stack, [#38](https://github.com/ossrs/state-threads/issues/38).
* Support st_destroy to free resources for asan.
* Support set specifics for any thread.
* Check capability for backtrack.
* Define and use a new jmpbuf, because the structure is different.
* IDE: Support CLion for debugging and learning.
* AppleM1: Support Apple Silicon M1(aarch64), [#30](https://github.com/ossrs/state-threads/issues/30).
* MIPS: Support Linux/MIPS64 for loongson 3A4000/3B3000, [#21](https://github.com/ossrs/state-threads/pull/21).
* RISCV: Support RISCV for RISCV CPU, [#24](https://github.com/ossrs/state-threads/pull/28).
* System: Support Multiple Threads for Linux and Darwin. [#19](https://github.com/ossrs/state-threads/issues/19), [srs#2188](https://github.com/ossrs/srs/issues/2188).
* LOONGARCH: Support loongarch for loongson CPU, [#24](https://github.com/ossrs/state-threads/issues/24). 
* MIPS: Support Linux/MIPS for OpenWRT, [#21](https://github.com/ossrs/state-threads/issues/21).
* Windows: Support Windows 64bits. [#20](https://github.com/ossrs/state-threads/issues/20).
* System: Improve the performance of timer. [9fe8cfe5b](https://github.com/ossrs/state-threads/commit/9fe8cfe5b1c9741a2e671a46215184f267fba400), [7879c2b](https://github.com/ossrs/state-threads/commit/7879c2b), [387cddb](https://github.com/ossrs/state-threads/commit/387cddb)
* System: Only support for Linux and Darwin. [#19](https://github.com/ossrs/state-threads/issues/19), [srs#2188](https://github.com/ossrs/srs/issues/2188).
* System: Support utest by gtest and coverage by gcov/gocvr.
* System: Refine performance for sleep or epoll_wait(0), [#17](https://github.com/ossrs/state-threads/issues/17).
* OSX: Support OSX for Apple Darwin, macOS, [#11](https://github.com/ossrs/state-threads/issues/11).
* AARCH64: Merge patch [srs#1282](https://github.com/ossrs/srs/issues/1282#issuecomment-445539513) to support aarch64, [#9](https://github.com/ossrs/state-threads/issues/9).
* ARM: Support macro `MD_ST_NO_ASM` to disable ASM, [#8](https://github.com/ossrs/state-threads/issues/8).
* OSX: Patch [st.osx10.14.build.patch](https://github.com/ossrs/srs/blob/2.0release/trunk/3rdparty/patches/6.st.osx10.14.build.patch), for osx 10.14 build.
* Valgrind: Merge from [toffaletti](https://github.com/toffaletti/state-threads), support [valgrind](https://github.com/ossrs/state-threads/issues/2) for ST.
* ARM: Merge from [michaeltalyansky](https://github.com/michaeltalyansky/state-threads) and [xzh3836598](https://github.com/ossrs/state-threads/commit/9a17dec8f9c2814d93761665df7c5575a4d2d8a3), support [ARM](https://github.com/ossrs/state-threads/issues/1).
* System: [Refine TAB of code](https://github.com/ossrs/state-threads/compare/c2001d30ca58f55d72a6cc6b9b6c70391eaf14db...d2101b26988b0e0db0aabc53ddf452068c1e2cbc).
* Linux: Patch [st.disable.examples.patch](https://github.com/ossrs/srs/blob/2.0release/trunk/3rdparty/patches/4.st.disable.examples.patch), for ubuntu.
* OSX: Patch [st.osx.kqueue.patch](https://github.com/ossrs/srs/blob/2.0release/trunk/3rdparty/patches/3.st.osx.kqueue.patch), for osx.
* ARM: Patch [st.arm.patch](https://github.com/ossrs/srs/blob/2.0release/trunk/3rdparty/patches/1.st.arm.patch), for ARM.
