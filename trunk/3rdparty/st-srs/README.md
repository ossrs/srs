# state-threads

![](http://ossrs.net:8000/gif/v1/sls.gif?site=github.com&path=/srs/srsst)
[![](https://cloud.githubusercontent.com/assets/2777660/22814959/c51cbe72-ef92-11e6-81cc-32b657b285d5.png)](https://github.com/ossrs/srs/wiki/v1_CN_Contact#wechat)

Fork from http://sourceforge.net/projects/state-threads, patched for [SRS](https://github.com/ossrs/srs/tree/2.0release).

> See: https://github.com/ossrs/state-threads/blob/srs/README

For original ST without any changes, checkout the [ST master branch](https://github.com/ossrs/state-threads/tree/master).

## Branch SRS

The branch [srs](https://github.com/ossrs/state-threads/tree/srs) will be patched the following patches:

- [x] Patch [st.arm.patch](https://github.com/ossrs/srs/blob/2.0release/trunk/3rdparty/patches/1.st.arm.patch), for ARM.
- [x] Patch [st.osx.kqueue.patch](https://github.com/ossrs/srs/blob/2.0release/trunk/3rdparty/patches/3.st.osx.kqueue.patch), for osx.
- [x] Patch [st.disable.examples.patch](https://github.com/ossrs/srs/blob/2.0release/trunk/3rdparty/patches/4.st.disable.examples.patch), for ubuntu.
- [x] [Refine TAB of code](https://github.com/ossrs/state-threads/compare/c2001d30ca58f55d72a6cc6b9b6c70391eaf14db...d2101b26988b0e0db0aabc53ddf452068c1e2cbc).
- [x] Merge from [michaeltalyansky](https://github.com/michaeltalyansky/state-threads) and [xzh3836598](https://github.com/ossrs/state-threads/commit/9a17dec8f9c2814d93761665df7c5575a4d2d8a3), support [ARM](https://github.com/ossrs/state-threads/issues/1).
- [x] Merge from [toffaletti](https://github.com/toffaletti/state-threads), support [valgrind](https://github.com/ossrs/state-threads/issues/2) for ST.
- [x] Patch [st.osx10.14.build.patch](https://github.com/ossrs/srs/blob/2.0release/trunk/3rdparty/patches/6.st.osx10.14.build.patch), for osx 10.14 build.
- [x] Support macro `MD_ST_NO_ASM` to disable ASM, [#8](https://github.com/ossrs/state-threads/issues/8).
- [x] Merge patch [srs#1282](https://github.com/ossrs/srs/issues/1282#issuecomment-445539513) to support aarch64, [#9](https://github.com/ossrs/state-threads/issues/9).
- [x] Support OSX for Apple Darwin, macOS, [#11](https://github.com/ossrs/state-threads/issues/11).
- [x] Support sendmmsg for UDP, [#12](https://github.com/ossrs/state-threads/issues/12).

## Docs

* Introduction: http://ossrs.github.io/state-threads/docs/st.html
* API reference: http://ossrs.github.io/state-threads/docs/reference.html
* Programming notes: http://ossrs.github.io/state-threads/docs/notes.html

## Analysis

* About setjmp and longjmp, read [setjmp](https://gitee.com/winlinvip/srs-wiki/raw/master/images/st-setjmp.jpg).
* About the stack structure, read [stack](https://gitee.com/winlinvip/srs-wiki/raw/master/images/st-stack.jpg)
* About asm code comments, read [#91d530e](https://github.com/ossrs/state-threads/commit/91d530e#diff-ed9428b14ff6afda0e9ab04cc91d4445R25).
* About the scheduler, read [#13-scheduler](https://github.com/ossrs/state-threads/issues/13#issuecomment-616025527).
* About the IO event system, read [#13-IO](https://github.com/ossrs/state-threads/issues/13#issuecomment-616096568).

## Usage

Get code:

```
git clone https://github.com/ossrs/state-threads.git st-1.9 &&
git checkout -b srs origin/srs
```

For Linux:

```
make linux-debug EXTRA_CFLAGS="-DMD_HAVE_EPOLL"
```

For OSX:

```
make darwin-debug EXTRA_CFLAGS="-DMD_HAVE_KQUEUE"
```

Linux with valgrind:

```
make linux-debug EXTRA_CFLAGS="-DMD_VALGRIND"
```

> Remark: User must install valgrind, for instance, in centos6 `sudo yum install -y valgrind valgrind-devel`.

Linux with valgrind and epoll:

```
make linux-debug EXTRA_CFLAGS="-DMD_HAVE_EPOLL -DMD_VALGRIND"
```

For OSX, user must specifies the valgrind header files:

```
make darwin-debug EXTRA_CFLAGS="-DMD_HAVE_KQUEUE -DMD_VALGRIND -I/usr/local/include"
```

> Remark: Latest OSX does not support ST, please use docker to run ST.

## Valgrind

How to debug with gdb under valgrind, read [valgrind manual](http://valgrind.org/docs/manual/manual-core-adv.html#manual-core-adv.gdbserver-simple).

About startup parameters, read [valgrind cli](http://valgrind.org/docs/manual/mc-manual.html#mc-manual.options).

Important cli options:

1. `--undef-value-errors=<yes|no> [default: yes]`, Controls whether Memcheck reports uses of undefined value errors. Set this to no if you don't want to see undefined value errors. It also has the side effect of speeding up Memcheck somewhat.
1. `--leak-check=<no|summary|yes|full> [default: summary]`, When enabled, search for memory leaks when the client program finishes. If set to summary, it says how many leaks occurred. If set to full or yes, each individual leak will be shown in detail and/or counted as an error, as specified by the options `--show-leak-kinds` and `--errors-for-leak-kinds`.
1. `--track-origins=<yes|no> [default: no]`, Controls whether Memcheck tracks the origin of uninitialised values. By default, it does not, which means that although it can tell you that an uninitialised value is being used in a dangerous way, it cannot tell you where the uninitialised value came from. This often makes it difficult to track down the root problem.
1. `--show-reachable=<yes|no> , --show-possibly-lost=<yes|no>`, to show the using memory.

Winlin 2016
