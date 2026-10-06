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

Native Windows x64 builds with the Visual Studio (MSVC) toolchain, `cl`, `ml64`, and `lib`, through GNU make
and the same Makefile. There is no Cygwin, MinGW, WSL, or POSIX emulation layer: GNU make and Git Bash only
run the build, and the output is the static library `obj/libst.a` built by MSVC. It needs Windows 10 1809 or
Windows Server 2019 or later, x64.

Install the build tools:

* Visual Studio 2022, or its Build Tools, with the MSVC x64 tools (the "Desktop development with C++" workload).
* [Git for Windows](https://git-scm.com/download/win), for Git Bash.
* GNU make, for example `winget install ezwinports.make` or `choco install make`.

Open the `x64 Native Tools Command Prompt for VS 2022`, which runs `vcvars64.bat` and puts `cl`, `ml64`, and
`lib` on `PATH`, then start Git Bash in it:

```
"C:\Program Files\Git\bin\bash.exe"
```

For Windows, build the debug or the optimized library:

```bash
make win64-debug
make win64-optimized
```

> Note: Programs that include `st.h` must include it before `windows.h`, or define `WIN32_LEAN_AND_MEAN`. The
> library links `ws2_32.lib` by itself. For what works differently on Windows, read the
> [porting notes](docs/notes.html#windows).

Windows with ASAN(Google Address Sanitizer), where cl has no `-fno-omit-frame-pointer`:

```bash
make win64-debug EXTRA_CFLAGS="-DMD_ASAN -fsanitize=address"
```

> Remark: Cygwin64 is deprecated, do not use it. Use native Windows, as above.

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

## Windows: UTest

> Note: We use [Google test](https://github.com/google/googletest/releases/tag/release-1.11.0) in `utest/gtest-fit`.

To make ST with utest and run it, in Git Bash with the MSVC environment (see [Windows: Usage](#windows-usage)):

```bash
make win64-debug-utest && ./obj/st_utest.exe
```

The tests that need POSIX features (fork, signals, pipes, files) are not built on Windows, and a few are
skipped with a reason, for example the write tests that need a partial send, which Winsock never does.

To debug in Visual Studio, generate a solution with CMake, which comes with Visual Studio, from the
`x64 Native Tools Command Prompt for VS 2022`, then open `C:\st-build\st.sln`:

```
cmake -S cmake -B C:\st-build -G "Visual Studio 17 2022" -A x64
cmake --build C:\st-build --config Debug --target st_utest
```

> Note: Use a short build directory, because MSBuild fails when its paths are too long. The Makefile stays the
> main build, and CMake also builds the `helloworld`, `porting`, and `verify` tools.

## Windows: Tools

The integration tools in `tools/` build with MSVC and run with the same runner as on Linux and macOS, in Git
Bash with the MSVC environment (see [Windows: Usage](#windows-usage)). It builds the library with
`make -B win64-debug`, then builds each tool and runs `<name>.exe` with `ST_TOOL_EVENTSYS=select` and `alt`
(both are WSAPoll on Windows), and prints `All tools OK`. Run it in the ST default build and in the SRS
build, as CI does:

```bash
./auto/tools.sh
EXTRA_CFLAGS=-DMALLOC_STACK ./auto/tools.sh
```

The arguments name the tools to run, for example `./auto/tools.sh tcp udp`. The runner leaves the library
built with `EXTRA_CFLAGS`, so rebuild with `make -B win64-debug-utest` before the utest.

On Windows, ST is sockets only, so the tools make each pipe a loopback TCP pair and each `socketpair` an
`AF_UNIX` stream pair, or a loopback UDP pair for datagrams. The `pipe` tool, which needs pipes, FIFOs,
`st_open` files, and signals, prints `SKIP pipe: <why>` instead of `OK`. A few checks that Windows does not
support are skipped there, each with a comment: the partial writes on timeout in `tcp` (Winsock never sends
part of a non-blocking send), the `RLIMIT_NOFILE` checks in `lifecycle` and `stress`, reading back
`O_NONBLOCK` in `poll`, and the `AF_UNIX` datagram echo and the `ENOENT` of a missing path in `unix`.

The `exception` tool is C++: it throws and catches C++ exceptions on coroutine stacks, and on Windows also
raises and catches SEH exceptions there. Windows builds C++ exceptions on SEH, which rejects frames outside
the stack bounds in the TIB, so this works only because ST switches those bounds with each coroutine stack.

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
