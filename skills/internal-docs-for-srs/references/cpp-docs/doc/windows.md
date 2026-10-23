---
title: Windows
sidebar_label: Windows
hide_title: false
hide_table_of_contents: false
---

# SRS for Windows

SRS 8.0 runs natively on Windows: `srs.exe` is built with MSVC for Windows x64, and runs as a console
program, on the same State Threads coroutines as Linux and macOS.

It needs Windows 10 1809 or Windows Server 2019 or later, on x64. 32-bit Windows, Windows on ARM,
Cygwin and MinGW are not supported.

> Note: SRS 5.0.89 to 7.0.59 built on Windows with Cygwin64, which was removed in 7.0.60 because it
> was too complex to maintain. SRS 8.0 builds without Cygwin. WSL (Windows Subsystem for Linux) runs
> the Linux build too.

## Download

Every release from 8.0 has a zip for Windows, `SRS-Windows-x86_64-<version>.zip`, on the
[releases](https://github.com/ossrs/srs/releases) page. It holds:

* `objs\srs.exe`: the server. Every library links statically, so it needs no DLL.
* `conf\`: the config files, with `server.key` and `server.crt` for HTTPS.
* `objs\nginx\html\`: the HTTP root, with the players and the console.

Unzip it and run SRS from the unzipped folder, so the default paths in the config work:

```bash
cd SRS-Windows-x86_64-<version>
objs\srs.exe -c conf\console.conf
```

For WebRTC, set the candidate to the IP the clients reach SRS on, before you start it; for example in
PowerShell:

```bash
$env:CANDIDATE="192.168.1.10"
objs\srs.exe -c conf\console.conf
```

Then publish and play as on Linux, see [Getting Started](./getting-started.md): RTMP on port 1935,
HTTP-FLV and HLS on 8080, the HTTP API on 1985, WebRTC on UDP 8000, SRT on UDP 10080 and RTSP on
8554. The first start may show a Windows Firewall prompt for `srs.exe`; allow it for the networks the
clients come from.

## Build

The Windows build is a CMake build, `trunk/cmake/windows/CMakeLists.txt`, that reads the same source
lists as `./configure` (`trunk/auto/module_files.txt`). The libraries come from:

* vcpkg, with the versions pinned in `trunk/vcpkg.json`: OpenSSL 3, libsrtp, libsrt, Opus and
  googletest.
* MSYS2, which builds the bundled FFmpeg (`trunk/3rdparty/ffmpeg-4-fit`) with the MSVC toolchain, for
  the AAC and Opus transcoding of WebRTC.
* The bundled State Threads, built with GNU make.

Install the tools:

* Visual Studio 2022, or its Build Tools, with "Desktop development with C++": MSVC x64, CMake,
  Ninja and vcpkg come with it.
* Git for Windows, for Git Bash.
* GNU make in Git Bash, for example `choco install make`.
* MSYS2 in `C:\msys64`, then in an MSYS2 shell: `pacman -S make nasm pkgconf diffutils`.

Open a Git Bash with the MSVC x64 environment: open the "x64 Native Tools Command Prompt for VS 2022",
which runs `vcvars64.bat`, and start Git Bash in it with `"C:\Program Files\Git\bin\bash.exe"`, not
`bash`, which may start WSL. Then build in the SRS checkout with
`skills/srs-develop/scripts/srs-windows-runner.sh`, which the CI and the release use too:

```bash
git clone -b develop https://github.com/ossrs/srs.git
cd srs
bash skills/srs-develop/scripts/srs-windows-runner.sh build
```

The `build` step builds the libraries, runs a small link check for each, builds
`trunk/objs/windows/srs.exe`, and starts it once to check that it answers on RTMP, HTTP and the HTTP
API. The first build takes a while for vcpkg and FFmpeg; later builds rebuild only what changed. The
logs are `trunk/objs/windows-*.log`.

Other steps:

* `package [version]`: builds `srs.exe` for a release and zips it as
  `trunk/objs/windows/package/SRS-Windows-x86_64-<version>.zip`, as the release does.
* `utest`: builds and runs `trunk/objs/windows/srs_utest.exe`.
* `integ [pattern]`: runs the `cmd/integ` black-box tests against `srs.exe`; it needs Go and FFmpeg
  in the `PATH`.

The test steps build `srs.exe` with the utest and the RTC simulator, as the Linux test builds do.
Run the server from `trunk`, where the paths of the config files lead:

```bash
cd trunk
./objs/windows/srs.exe -c conf/console.conf
```

### Visual Studio

To build, run and debug SRS in the Visual Studio IDE, generate its projects with the `vs` step, which
runs `deps` first:

```bash
bash skills/srs-develop/scripts/srs-windows-runner.sh vs
```

CMake writes the projects, with the paths of the machine, into `trunk\objs\windows-vs`. The solution
`.vs\srs.slnx` in the repository opens them; it needs Visual Studio 2022 17.13 or later, and shows the
projects as not loaded until the `vs` step has run. Open it in Visual Studio, then:

* `srs` is the startup project; if it is not, right-click `srs` in the Solution Explorer and pick Set
  as Startup Project.
* Build the solution in the `Debug` configuration, into `trunk\objs\windows-vs\Debug`.
* Press F5 to debug `srs.exe -c conf/console.conf`, run in `trunk`. To debug the utest, set
  `srs_utest` as the startup project, with a `--gtest_filter` in its Debugging properties.
* To debug the coroutine switch of State Threads, which is assembly, open
  `trunk\3rdparty\st-srs\md_win64.asm` with File > Open > File, and set a breakpoint in
  `_st_md_cxt_save` or `_st_md_cxt_restore`. When it stops, F10 steps one instruction, and Debug >
  Windows > Registers shows the registers.

The solution configures CMake again by itself when a CMake file changes. Run the `vs` step again when
the ST or FFmpeg source changes.

## Run

`srs.exe` is a console program: it runs in the foreground, until it is stopped.

* Daemon mode is not supported. A config with `daemon on`, such as `conf/srs.conf`, is refused at
  start with `daemon is not supported on this OS`; set `daemon off`, or `SRS_DAEMON=off`. A config
  without `daemon` runs in the foreground.
* There is no Windows service and no installer; to run SRS as a service, use a service wrapper.
* The log goes to the console or to the file of `srs_log_file`, as on Linux.
* Write the paths in the config with `/`, such as `./objs/nginx/html`; a path that starts with `/`,
  such as `/tmp/srs`, is on the current drive, such as `C:\tmp\srs`.
* For `ingest`, `transcode` and `exec`, `ffmpeg` is the path of the FFmpeg binary, with or without
  `.exe`; `/dev/null` is Windows' `NUL`.
* All features listen with the same ports and config as on Linux; see [Features](#features).

## Control

Windows has no signals, so SRS takes the console events and the HTTP RAW API instead:

| Linux | Windows | Action |
|---|---|---|
| `SIGINT` | Ctrl+C | Fast quit. |
| `SIGTERM` | Ctrl+Break, closing the console, system shutdown | Fast quit, or graceful quit with `force_grace_quit on`. |
| `SIGQUIT` | none | Use Ctrl+Break with `force_grace_quit on`. |
| `SIGHUP` | `rpc=reload` | Reload the config. |
| `SIGUSR1` | `rpc=reopen-log` | Reopen the log file. |

A tool or a supervisor stops one `srs.exe` with Ctrl+Break, sent to the process group SRS runs in;
Ctrl+C goes to the whole console.

The RAW API needs `http_api.raw_api.enabled on`; `rpc=reload` also needs `allow_reload on`, while
`rpc=reopen-log` does not, since it changes no config:

```bash
curl http://127.0.0.1:1985/api/v1/raw?rpc=reload
curl http://127.0.0.1:1985/api/v1/raw?rpc=reopen-log
```

Log rotation works as on Linux: rename the log file while SRS writes it, then call `rpc=reopen-log`,
and SRS writes to a new file with the old name. See [Log Rotate](./log-rotate.md).

## Features

These features run on Windows, and pass the same black-box tests (`cmd/integ`) as on Linux, but the
[skip list](#skip-list):

* RTMP, RTMPS and Enhanced RTMP; HTTP-FLV, HTTP-TS, and HTTP AAC and MP3 streams; HTTPS.
* HLS (TS and fMP4, with keys) and DASH.
* WebRTC (WHIP and WHEP), with NACK, RTX and TWCC, and RTMP to WebRTC and back, with AAC and Opus
  transcoding.
* SRT, publish and play, and SRT to RTMP, HTTP-FLV, HLS, DASH and WebRTC.
* RTSP play, and GB28181 over TCP.
* DVR to FLV and MP4, static files, and FLV, MP4 and HLS VOD.
* HTTP API, HTTP callbacks, the Prometheus exporter, security rules.
* Edge, origin cluster, forward and heartbeat.
* Ingest, transcode and exec with FFmpeg, as child processes that end with SRS.
* File names in UTF-8, such as Chinese names.

## Skip List

The black-box test cases that do not run on Windows, with the reason, are in
`cmd/integ/internal/check/skips.go`. Each is a signal Windows does not have, and another case covers
the same action on Windows:

| Cases | Reason | Covered on Windows by |
|---|---|---|
| `TestTimer_Signal_Quit/QuitGraceful`, `TestTimer_Signal_Quit/TermDuringGracefulQuit` | No `SIGQUIT`. | `ForceGraceQuit`: Ctrl+Break with `force_grace_quit on`. |
| `TestTimer_Signal_LogFile/Usr1*`, `TestTimer_Signal_Console/*` | No `SIGUSR1`. | The `Signal_LogFile` RAW API cases: `rpc=reopen-log`. |
| `TestTimer_Reload_Sighup/*` | No `SIGHUP`. | `TestTimer_Reload_RawApi`: `rpc=reload`. |

The cases that read Linux `/proc` stats are skipped on Windows and macOS by their own check, not by
the list.

## Limits

* **Timer resolution.** Sleeps and timeouts have millisecond resolution on the Windows system timer,
  which ticks every 15.6 ms, so a short wait, such as a 1 ms sleep, takes up to one tick. This adds
  latency to SRT, whose event loop sleeps between polls, and to small timeouts.
* **Event backend.** State Threads uses `WSAPoll` on Windows, not IOCP, which scans every socket on
  each poll, so it serves fewer connections than epoll on Linux. The black-box tests run with
  `-integ.limit-scale 4` on Windows, four times the Linux time limits, as in CI. Windows performance
  is not tuned or measured.
* **Stats.** The system stats that Linux reads from `/proc` are not there: CPU usage, memory, disk,
  network, SNMP and load average are 0 or not ok in the HTTP API and the exporter. The process times
  and peak memory (`/api/v1/rusages`) and the uptime are there.
* **Config reload.** `inotify_auto_reload` does nothing; reload with `rpc=reload`.
* **Ports.** No `SO_REUSEPORT`, so `rtc_server.reuseport` is always 1. A second SRS cannot listen on
  a port that is in use.
* **Refused connects.** A connect to a remote host that refuses it, such as a down origin or hook
  server, fails after about 2 s, since Windows sends the SYN again; to a loopback address, it fails
  at once.
* **File names.** UTF-8 names need Windows 10 1903 or later; on 1809, only ASCII names work.
* **Time zone.** The `TZ` variable takes the Windows CRT form, a name of three letters and the
  offset, such as `TZ=CST-8`, not a zone name such as `Asia/Shanghai`. Without `TZ`, SRS uses the
  Windows time zone.
* **OpenSSL 3.** The build uses OpenSSL 3 from vcpkg. `dtls_version dtls1.0` works, but the peer must
  allow DTLS 1.0 too. With `rtc_server.ecdsa off`, the DTLS certificate is RSA 2048.
* **Network interfaces.** SRS lists the IPs from `GetAdaptersAddresses` for the WebRTC candidate and
  the stats, with the adapter names, such as `Ethernet` or `Wi-Fi`.
* **Not built.** Daemon mode, a Windows service, an installer, IOCP, gperftools, sanitizers, Valgrind
  and backtraces. Windows on ARM and 32-bit Windows are not supported.

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/windows)

