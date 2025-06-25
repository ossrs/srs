---
title: ARM and CrossBuild
sidebar_label: ARM and CrossBuild
hide_title: false
hide_table_of_contents: false
---

# SRS for linux-arm

How to run SRS on ARM pcu?

* Run SRS on ARM: Client can play stream from ARM server.

## Why run SRS on ARM?

The use scenario:

* Run SRS on ARM server, see [#1282](https://github.com/ossrs/srs/issues/1282#issue-386077124).
* Crossbuild for ARM embeded device, see [#1547](https://github.com/ossrs/srs/issues/1547#issue-543780097).

## RaspberryPi

User is able to build and run SRS on RespberryPI. Please don't use crossbuild.

<a name="armv8-and-aarch64"></a>

## ARM Server: armv7, armv8(aarch64)

User is able to build and run SRS on ARM servers. Please don't use crossbuild.

```
./configure && make
```

Build SRS in ARM server docker, see [aarch64](https://github.com/ossrs/dev-docker/tree/aarch64#usage)

```
docker run -it --rm -v `pwd`:/srs -w /srs ossrs/srs:aarch64 \
    bash -c "./configure && make"
```

For armv8 or aarch64, user should specify the arch, if the CPU arch is not identified automatically, see [#1282](https://github.com/ossrs/srs/issues/1282#issuecomment-568891854):

```bash
./configure --extra-flags='-D__aarch64__' && make
```

Run SRS:

```
./objs/srs -c conf/console.conf
```

Publish stream:

```
ffmpeg -re -i doc/source.flv -c copy -f flv rtmp://127.0.0.1:1935/live/livestream
```

Play stream：[http://localhost:8080/live/livestream.flv](http://localhost:8080/players/srs_player.html?autostart=true&stream=livestream.flv&port=8080&schema=http)

![image](https://user-images.githubusercontent.com/2777660/72774670-7108c980-3c46-11ea-9e8b-d4fb3a475ea2.png)

<a name="ubuntu-cross-build-srs"></a>

## Ubuntu Cross Build SRS: ARMv8(aarch64)

Build SRS in docker(Ubuntu20(xenial))：

```
cd ~/git/srs/trunk
docker run --rm -it -v `pwd`:/srs -w /srs ossrs/srs:ubuntu20 bash
```

Install toolchain(optional):

```
apt-get install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
```

Cross build SRS:

```
./configure --cross-build --cross-prefix=aarch64-linux-gnu-
make
```

Run SRS on [aarch64 docker](https://hub.docker.com/r/arm64v8/ubuntu):

```
cd ~/git/srs/trunk && docker run --rm -it -v `pwd`:/srs -w /srs \
    -p 1935:1935 -p 1985:1985 -p 8080:8080 arm64v8/ubuntu \
    ./objs/srs -c conf/console.conf
```

Publish stream:

```
ffmpeg -re -i doc/source.flv -c copy -f flv rtmp://127.0.0.1:1935/live/livestream
```

Play stream：[http://localhost:8080/live/livestream.flv](http://localhost:8080/players/srs_player.html?autostart=true&stream=livestream.flv&port=8080&schema=http)

## Ubuntu Cross Build SRS: ARMv7

Cross build ST and OpenSSL on Ubuntu20.

Build SRS in docker(Ubuntu20(xenial))：

```
cd ~/git/srs/trunk
docker run --rm -it -v `pwd`:/srs -w /srs ossrs/srs:ubuntu20 bash
```

Install toolchain(optional), for example [Acqua or RoadRunner board](https://www.acmesystems.it/arm9_toolchain)

```
apt-get install -y gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf
```

Cross build SRS:

```
./configure --cross-build --cross-prefix=arm-linux-gnueabihf-
make
```

Run SRS on [ARMv7 docker](https://hub.docker.com/r/armv7/armhf-ubuntu):

```
cd ~/git/srs/trunk && docker run --rm -it -v `pwd`:/srs -w /srs \
    -p 1935:1935 -p 1985:1985 -p 8080:8080 armv7/armhf-ubuntu \
    ./objs/srs -c conf/console.conf
```

Publish stream:

```
ffmpeg -re -i doc/source.flv -c copy -f flv rtmp://127.0.0.1:1935/live/livestream
```

Play stream：[http://localhost:8080/live/livestream.flv](http://localhost:8080/players/srs_player.html?autostart=true&stream=livestream.flv&port=8080&schema=http)

## Ubuntu Cross Build SRS: hisiv500(arm)

TBD.

## Use Other Cross build tools

SRS configure options for cross build:

```bash
./configure -h

Presets:
  --cross-build             Enable cross-build, please set bellow Toolchain also. Default: off
  
Cross Build options:        @see https://ossrs.io/lts/en-us/docs/v7/doc/arm#ubuntu-cross-build-srs
  --cpu=<CPU>               Toolchain: Select the minimum required CPU. For example: --cpu=24kc
  --arch=<ARCH>             Toolchain: Select architecture. For example: --arch=aarch64
  --host=<BUILD>            Toolchain: Build programs to run on HOST. For example: --host=aarch64-linux-gnu
  --cross-prefix=<PREFIX>   Toolchain: Use PREFIX for tools. For example: --cross-prefix=aarch64-linux-gnu-

Toolchain options:
  --static=on|off           Whether add '-static' to link options. Default: off
  --cc=<CC>                 Toolchain: Use c compiler CC. Default: gcc
  --cxx=<CXX>               Toolchain: Use c++ compiler CXX. Default: g++
  --ar=<AR>                 Toolchain: Use archive tool AR. Default: g++
  --ld=<LD>                 Toolchain: Use linker tool LD. Default: g++
  --randlib=<RANDLIB>       Toolchain: Use randlib tool RANDLIB. Default: g++
  --extra-flags=<EFLAGS>    Set EFLAGS as CFLAGS and CXXFLAGS. Also passed to ST as EXTRA_CFLAGS.
```

Winlin 2014.11

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/arm)


