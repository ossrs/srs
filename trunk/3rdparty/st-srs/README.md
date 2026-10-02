# state-threads

![](http://ossrs.net:8000/gif/v1/sls.gif?site=github.com&path=/srs/srsst)
[![](https://github.com/ossrs/state-threads/actions/workflows/test.yml/badge.svg?branch=srs)](https://github.com/ossrs/state-threads/actions?query=workflow%3ATest+branch%3Asrs)
[![](https://codecov.io/gh/ossrs/state-threads/branch/srs/graph/badge.svg)](https://codecov.io/gh/ossrs/state-threads/branch/srs)

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

Linux with ASAN(Google Address Sanitizer):

```bash
make linux-debug EXTRA_CFLAGS="-DMD_ASAN"
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

The branch [srs](https://github.com/ossrs/state-threads/tree/srs) was patched and refined. See [CHANGELOG.md](CHANGELOG.md) for the changes and versions.

Planned:

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

Winlin 2016
