---
title: Windows
sidebar_label: Windows
hide_title: false
hide_table_of_contents: false
---

# SRS for Windows

SRS 5.0.89+ supports Windows(Cygwin64).

## Build from code

Please install [Cygwin64](https://cygwin.com/install.html).

Install packages `gcc-g++` `make` `automake` `patch` `pkg-config` `tcl` `cmake`, please see [packages](https://github.com/cygwin/cygwin-install-action#parameters).

Build SRS with cygwin terminal:

```bash
git checkout develop
./configure
make
```

If success, there should be a `./objs/srs.exe`, please follow [Getting Started](./getting-started.md) to use it.

## Install from binary

For each [release](https://github.com/ossrs/srs/releases) of SRS, from SRS 5.0.89, there is always a binary installer of SRS Windows, normally as the artifact of release, which allows you to install and run SRS very easy.

Bellow is some examples, note that you should always use the latest [release](https://github.com/ossrs/srs/releases), not the fixed one:

* [Latest release](https://github.com/ossrs/srs/releases)
* [SRS-Windows-x86_64-5.0.89-setup.exe](https://github.com/ossrs/srs/releases/tag/v5.0.89)
* [SRS-Windows-x86_64-5.0.19-setup.exe](https://github.com/ossrs/srs/releases/tag/v5.0.19)

> Note: SRS 5.0.89+ supports cygwin pipeline, to build and packge automatically by GitHub Actions.

![](/img/windows-2022-11-20-001.png)

Run SRS as administrator:

![](/img/windows-2022-11-20-002.png)

Publish to SRS Windows by FFmpeg:

```bash
ffmpeg -re -i ~/srs/doc/source.flv -c copy -f flv rtmp://win11/live/livestream
```

Play by VLC or [srs-player](http://win11:8080/)

![](/img/windows-2022-11-20-003.png)

Most of SRS features are available in Windows, for example, RTMP, HTTP-FLV, HLS, WebRTC, HTTP-API, Prometheus Exporter, etc.

## Package by NSIS

If want to package by [NSIS](https://nsis.sourceforge.io/Download), please run in cygwin terminal:

```bash
"/cygdrive/c/Program Files (x86)/NSIS/makensis.exe" \
    /DSRS_VERSION=$(./objs/srs -v 2>&1) \
    /DCYGWIN_DIR="C:\cygwin64" \
    packaging/nsis/srs.nsi
```

## Known Issues

* [Cygwin: Build with SRT is ok, but crash when running. #3251](https://github.com/ossrs/srs/issues/3251)
* [Cygwin: Support address sanitizer for windows. #3252](https://github.com/ossrs/srs/issues/3252)
* [Cygwin: ST stuck when working in multiple threads mode. #3253](https://github.com/ossrs/srs/issues/3253)
* [Cygwin: Support iocp and windows native build. #3256](https://github.com/ossrs/srs/issues/3256)
* [Cygwin: Build srtp with openssl fail for no srtp_aes_icm_ctx_t #3254](https://github.com/ossrs/srs/issues/3254)

## Links

ST supports windows: https://github.com/ossrs/state-threads/issues/20

Commits about SRS Windows: https://github.com/ossrs/srs-windows/issues/2

Windows docker also works for SRS, however, `srs.exe` is more popular for windows developers.

Winlin 2022.11

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/windows)


