---
title: HEVC
sidebar_label: HEVC
hide_title: false
hide_table_of_contents: false
---

# HEVC

HEVC, also known as H.265, is the next-generation encoding after H.264 and belongs to the same generation of codecs
as AV1. H.265 can save about half the bandwidth compared to H.264, or provide double the clarity and image quality at
the same bandwidth.

However, the problem with H.265 is that it's not yet widely supported by clients. Almost all devices support H.264,
including low-performance phones or boxes, which have dedicated chips for H.264 support. Although H.265 has been
developed for almost ten years, there are still not enough devices that support it. In specific scenarios, like when
the device clearly supports H.265, you can choose H.265; otherwise, stick with H.264.

Additionally, the support for H.265 in transport protocols is gradually improving, but not all protocols support it
yet. MPEG-TS was the first to support H.265, and since SRT and HLS are based on TS, they also support it. RTMP and
HTTP-FLV only started supporting HEVC and AV1 in March 2023 with the [Enhanced RTMP](https://github.com/veovera/enhanced-rtmp)
project. As for WebRTC, only Safari supports it currently, and Chrome is said to be in development.

SRS 6.0 officially supports the H.265 feature. If you want to use the H.265 function, please switch to the SRS 
6.0 version. Please refer to [#465](https://github.com/ossrs/srs/issues/465) for the detailed research and development process.

## Overview

The architecutre for SRS to support H.265(or HEVC):

```text
FFmpeg --RTMP(h.265)---> SRS ----RTMP/FLV/TS/HLS/WebRTC(h.265)--> Chrome/Safari
```

For live streaming:

* [Chrome 105+](https://caniuse.com/?search=HEVC) supports HEVC by default, see [this post](https://zhuanlan.zhihu.com/p/541082191).
    * You're able to play mp4 directly by H5 video, or by MSE if HTTP-FLV/HTTP-TS/HLS etc.
    * Please use [mpegts.js](https://github.com/xqq/mpegts.js) to play HTTP-TS with HEVC.
    * There is a plan for mpegts.js to support HTTP-FLV with HEVC, see [mpegts.js#64](https://github.com/xqq/mpegts.js/issues/64)
* [OBS 29+](https://github.com/obsproject/obs-studio/releases/tag/29.1.3) supports HEVC over RTMP.
* FFmpeg or ffplay supports libx265
    * FFmpeg 6 supports HEVC over RTMP, see [637c761b](https://github.com/FFmpeg/FFmpeg/commit/637c761be1bf9c3e1f0f347c5c3a390d7c32b282) for detail.
    * FFmpeg 4 or 5, need some patch for HEVC over RTMP/FLV, see **[FFmpeg Tools](#ffmpeg-tools)** bellow.
* SRS also supports HEVC.
    * We have merged HEVC support into SRS 6.0
    * The original supports for HEVC is [srs-gb28181/feature/h265](https://github.com/ossrs/srs-gb28181/commits/feature/h265) by [runner365](https://github.com/runner365)

> Note: To check if your Chrome support HEVC, please open `chrome://gpu` and search `hevc`.

For WebRTC:

* Chrome does not support HEVC right now(2022.11), but supports AV1, please see [#2324](https://github.com/ossrs/srs/pull/2324)
* Safari supports HEVC if user enable it, please see this [section](#safari-webrtc)
* SRS also only supports AV1, because Chrome does not support HEVC yet.

## Usage

Please make sure your SRS is `6.0.4+`, build with h265:

```bash
docker run --rm -it -p 1935:1935 -p 8080:8080 ossrs/srs:6 \
  ./objs/srs -c conf/hevc.flv.conf
```

> Note: Besides environment variables, you can also use `conf/hevc.flv.conf` or `conf/hevc.ts.conf` config files.
> Note: Recommend `conf/hevc.ts.conf` because TS is better for HEVC.

Build and patch FFmpeg, see [FFmpeg Tools](#ffmpeg-tools):

```bash
# For macOS
docker run --rm -it ossrs/srs:encoder ffmpeg -stream_loop -1 -re -i doc/source.flv \
  -acodec copy -vcodec libx265 -f flv rtmp://host.docker.internal/live/livestream

# For linux
docker run --net=host --rm -it ossrs/srs:encoder ffmpeg -stream_loop -1 -re -i doc/source.flv \
  -acodec copy -vcodec libx265 -f flv rtmp://127.0.0.1/live/livestream
```

> Note: Please change the ip `host.docker.internal` to your SRS's IP.

Play the HEVC live streams by:

* HTTP-FLV(by H5):  [http://localhost:8080/live/livestream.flv](http://localhost:8080/players/srs_player.html?autostart=true)
* HLS(by VLC or fflay): `http://localhost:8080/live/livestream.m3u8`

> Note: Please enable MPEG-DASH by `SRS_VHOST_DASH_ENABLED=on` then use VLC/ffplay to play stream `http://localhost:8080/live/livestream.mpd`

> Note: Please enable HTTP-TS by `SRS_VHOST_HTTP_REMUX_MOUNT=[vhost]/[app]/[stream].ts` then use H5/VLC/ffplay to play stream `http://localhost:8080/live/livestream.ts`

> Note: Please enable DVR MP4 by `SRS_VHOST_DVR_ENABLED=on SRS_VHOST_DVR_DVR_PATH=./objs/nginx/html/[app]/[stream].[timestamp].mp4` if want to covert live stream to MP4 file.

> Note: The detail about available protocols and tools for HEVC, please see [Status of HEVC in SRS](#status-of-hevc-in-srs).

> Note: The H5 player uses [mpegts.js](https://github.com/xqq/mpegts.js).

## Status of HEVC in SRS

The status of protocols and HEVC:

* [x] PUSH HEVC over RTMP by FFmpeg. [v6.0.2](https://github.com/ossrs/srs/commit/178e40a5fc3cf0856ace914ae61696a73007f5bf)
* [x] PUSH HEVC over SRT by FFmpeg. [v6.0.20](https://github.com/ossrs/srs/pull/3366)
* [x] PUSH HEVC over RTMP by OBS. [#3464](https://github.com/ossrs/srs/issues/3464) https://github.com/obsproject/obs-studio/pull/8522
* [x] PUSH HEVC over SRT by OBS. [v6.0.20](https://github.com/ossrs/srs/pull/3366)
* [x] PUSH HEVC over GB28181. [v6.0.25](https://github.com/ossrs/srs/pull/3408)
* [x] PULL HEVC over RTMP by FFmpeg, with [patch](#ffmpeg-tools) for FFmpeg. [v6.0.2](https://github.com/ossrs/srs/commit/178e40a5fc3cf0856ace914ae61696a73007f5bf)
* [x] PULL HEVC over HTTP-FLV by FFmpeg, with [patch](#ffmpeg-tools) for FFmpeg. [v6.0.2](https://github.com/ossrs/srs/commit/178e40a5fc3cf0856ace914ae61696a73007f5bf)
* [x] PULL HEVC over HTTP-TS by FFmpeg [v6.0.4](https://github.com/ossrs/srs/commit/70d5618979e5c8dc41b7cd87c78db7ca2b8a10e8)
* [x] PULL HEVC over HLS by FFmpeg [v6.0.11](https://github.com/ossrs/srs/commit/fff8d9863c3fba769b01782428257edf40f80a12)
* [x] PULL HEVC over MPEG-DASH  by FFmpeg [v6.0.14](https://github.com/ossrs/srs/commit/edba2c25f13c0fa915bd8e8093a4005df6077858)
* [x] PULL HEVC over SRT by FFmpeg. [v6.0.20](https://github.com/ossrs/srs/pull/3366)
* [x] PUSH HEVC over WebRTC by Safari. [v6.0.34](https://github.com/ossrs/srs/pull/3441)
* [x] PULL HEVC over WebRTC by Safari. [v6.0.34](https://github.com/ossrs/srs/pull/3441)
* [ ] PUSH HEVC over WebRTC by Chrome/Firefox
* [ ] PULL HEVC over WebRTC by Chrome/Firefox
* [x] Play HEVC over HTTP-TS by [mpegts.js](https://github.com/xqq/mpegts.js), by Chrome 105+ MSE, **NO WASM**. [v6.0.1](https://github.com/ossrs/srs/commit/7e02d972ea74faad9f4f96ae881d5ece0b89f33b)
* [x] Play pure video(no audio) HEVC over HTTP-TS by [mpegts.js](https://github.com/xqq/mpegts.js). [v6.0.9](https://github.com/ossrs/srs/commit/d5bf0ba2da30698e18700b210d2b12eed5b21d29)
* [x] Play HEVC over HTTP-FLV by [mpegts.js](https://github.com/xqq/mpegts.js), by Chrome 105+ MSE, **NO WASM**. [v6.0.1](https://github.com/ossrs/srs/commit/7e02d972ea74faad9f4f96ae881d5ece0b89f33b)
* [ ] Play HEVC over HLS by [hls.js](https://github.com/video-dev/hls.js)
* [ ] Play HEVC over MPEG-DASH by [dash.js](https://github.com/Dash-Industry-Forum/dash.js)
* [x] Play HEVC over HTTP-TS by ffplay, by offical release. [v6.0.4](https://github.com/ossrs/srs/commit/70d5618979e5c8dc41b7cd87c78db7ca2b8a10e8)
* [x] PULL HEVC over RTMP by ffplay, with [patch](#ffmpeg-tools) for FFmpeg. [v6.0.2](https://github.com/ossrs/srs/commit/178e40a5fc3cf0856ace914ae61696a73007f5bf)
* [x] Play HEVC over HTTP-FLV by ffplay, with [patch](#ffmpeg-tools) for FFmpeg. [v6.0.2](https://github.com/ossrs/srs/commit/178e40a5fc3cf0856ace914ae61696a73007f5bf)
* [x] Play pure video(no audio) HEVC by ffplay.
* [x] Play HEVC over HLS by ffplay. [v6.0.11](https://github.com/ossrs/srs/commit/fff8d9863c3fba769b01782428257edf40f80a12)
* [x] Play HEVC over MPEG-DASH by ffplay. [v6.0.14](https://github.com/ossrs/srs/commit/edba2c25f13c0fa915bd8e8093a4005df6077858)
* [x] Play HEVC over SRT by ffplay. [v6.0.20](https://github.com/ossrs/srs/pull/3366)
* [x] Play HEVC over HTTP-TS by VLC, by official release. [v6.0.4](https://github.com/ossrs/srs/commit/70d5618979e5c8dc41b7cd87c78db7ca2b8a10e8)
* [x] Play HEVC over SRT by VLC, by official. [v6.0.20](https://github.com/ossrs/srs/pull/3366)
* [x] Play pure video(no audio) HEVC by VLC.
* [ ] Play HEVC over RTMP by VLC.
* [ ] Play HEVC over HTTP-FLV by VLC.
* [x] Play HEVC over HLS by VLC. [v6.0.11](https://github.com/ossrs/srs/commit/fff8d9863c3fba769b01782428257edf40f80a12)
* [x] Play HEVC over MPEG-DASH by VLC. [v6.0.14](https://github.com/ossrs/srs/commit/edba2c25f13c0fa915bd8e8093a4005df6077858)
* [x] DVR HEVC to MP4/FLV file. [v6.0.14](https://github.com/ossrs/srs/commit/edba2c25f13c0fa915bd8e8093a4005df6077858)
* [x] HTTP API contains HEVC metadata.
* [ ] HTTP Callback takes HEVC metadata.
* [ ] Prometheus Exporter supports HEVC metadata.
* [ ] Improve coverage for HEVC.
* [x] Add regression/blackbox tests for HEVC.
* [ ] Supports benchmark for HEVC by [srs-bench](https://github.com/ossrs/srs-bench).
* [x] Support patched FFmpeg for SRS dockers: [CentOS7](https://github.com/ossrs/dev-docker/commit/0691d016adfe521f77350728d15cead8086d527d), [Ubuntu20](https://github.com/ossrs/dev-docker/commit/0e36323d15544ffe2901d10cfd255d9ef08fb250) and [Encoder](https://github.com/ossrs/dev-docker/commit/782bb31039653f562e0765a0c057d9f9babd1d1f).
* [x] Update [WordPress plugin SrsPlayer](https://github.com/ossrs/WordPress-Plugin-SrsPlayer) for HEVC.
* [ ] Update [srs-cloud](https://github.com/ossrs/srs-cloud) for HEVC.
* [ ] Edge server supports publish HEVC stream to origin.
* [ ] Edge server supprots play HEVC stream from origin.
* [ ] [HEVC: Error empty SPS/PPS when coverting RTMP to HEVC.](https://github.com/ossrs/srs/issues/3407)

> Note: We're merging HEVC support to SRS 6.0, the original supports for HEVC is [srs-gb28181/feature/h265](https://github.com/ossrs/srs-gb28181/commits/feature/h265) by [runner365](https://github.com/runner365)

## FFmpeg Tools

The FFmpeg in `ossrs/srs:encoder` or `ossrs/srs:6` is built with libx265 and patched with HEVC over RTMP support. So you're able to directly use:

```bash
docker run --rm -it --net host ossrs/srs:encoder \
  ffmpeg -re -i doc/source.flv -acodec copy -vcodec libx265 \
    -f flv rtmp://localhost/live/livestream
```

If you want to build from code, please read the bellow instructions. Before build FFmpeg, we must build 
[libx264](https://www.videolan.org/developers/x264.html):

```bash
git clone https://code.videolan.org/videolan/x264.git ~/git/x264
cd ~/git/x264
./configure --prefix=$(pwd)/build --disable-asm --disable-cli --disable-shared --enable-static
make -j10
make install
```

And then [libx265](https://www.videolan.org/developers/x265.html):

```bash
git clone https://bitbucket.org/multicoreware/x265_git.git ~/git/x265_git
cd ~/git/x265_git/build/linux
cmake -DCMAKE_INSTALL_PREFIX=$(pwd)/build -DENABLE_SHARED=OFF ../../source
make -j10
make install
```

Keep in mind that FFmpeg 6.0 does not support HEVC over RTMP until the following commit 
[637c761b](https://github.com/FFmpeg/FFmpeg/commit/637c761be1bf9c3e1f0f347c5c3a390d7c32b282):

```
commit 637c761be1bf9c3e1f0f347c5c3a390d7c32b282
Author: Steven Liu <liuqi05@kuaishou.com>
Date:   Mon Aug 28 09:59:24 2023 +0800

    avformat/rtmpproto: support enhanced rtmp
    
    add option named rtmp_enhanced_codec,
    it would support hvc1,av01,vp09 now,
    the fourcc is using Array of strings.
    
    Signed-off-by: Steven Liu <lq@chinaffmpeg.org>
```

So, if you are using FFmpeg 6, you can build FFmpeg without any patch, directly by the following commands:

```bash
git clone -b master https://github.com/FFmpeg/FFmpeg.git ~/git/FFmpeg
cd ~/git/FFmpeg
env PKG_CONFIG_PATH=~/git/x264/build/lib/pkgconfig:~/git/x265_git/build/linux/build/lib/pkgconfig \
./configure \
  --prefix=$(pwd)/build \
  --enable-gpl --enable-nonfree --enable-pthreads --extra-libs=-lpthread \
  --disable-asm --disable-x86asm --disable-inline-asm \
  --enable-decoder=aac --enable-decoder=aac_fixed --enable-decoder=aac_latm --enable-encoder=aac \
  --enable-libx264 --enable-libx265 \
  --pkg-config-flags='--static'
make -j10
```

Push HEVC over RTMP to SRS:

```bash
./ffmpeg -stream_loop -1 -re -i ~/srs/doc/source.flv -acodec copy -vcodec libx265 \
  -f flv rtmp://localhost/live/livestream
```

Play HEVC over RTMP by ffplay:

```bash
./ffplay rtmp://localhost/live/livestream
```

It works like magic!

If you want to use HEVC over RTMP in FFmpeg 4.1 or 5.1, please read the following instructions. Please clone FFmepg
and checkout to 5.1:

> Note: The [specfication](https://github.com/ksvc/FFmpeg/wiki) and [usage](https://github.com/ksvc/FFmpeg/wiki/hevcpush)
to support HEVC over RTMP or FLV. There is a [patch for FFmpeg 4.1/5.1/6.0](https://github.com/runner365/ffmpeg_rtmp_h265)
from [runner365](https://github.com/runner365) for FFmpeg to support HEVC over RTMP or FLV. There is also a
[patch](https://github.com/VCDP/CDN/blob/master/FFmpeg_patches/0001-Add-SVT-HEVC-FLV-support-on-FFmpeg.patch)
from Intel for this feature.

```bash
git clone -b n5.1.2 https://github.com/FFmpeg/FFmpeg.git ~/git/FFmpeg
```

Then, patch for [HEVC over RTMP/FLV](https://github.com/runner365/ffmpeg_rtmp_h265):

```bash
git clone -b 5.1 https://github.com/runner365/ffmpeg_rtmp_h265.git ~/git/ffmpeg_rtmp_h265
cp ~/git/ffmpeg_rtmp_h265/flv.h ~/git/FFmpeg/libavformat/
cp ~/git/ffmpeg_rtmp_h265/flv*.c ~/git/FFmpeg/libavformat/
```

Finally, follow the previous instructions to build FFmpeg.

## MSE for HEVC

[MSE](https://caniuse.com/?search=mse) is a base technology for [mpegts.js](https://github.com/xqq/mpegts.js), [hls.js](https://github.com/video-dev/hls.js/) and [dash.js](https://github.com/Dash-Industry-Forum/dash.js).

Now [Chrome 105+](https://caniuse.com/?search=HEVC) supports HEVC by default, see [this post](https://zhuanlan.zhihu.com/p/541082191), which means, MSE(Chrome 105+) is available for HEVC.

You can verify this feature, by generating a HEVC mp4 file:

```bash
ffmpeg -i ~/git/srs/trunk/doc/source.flv -acodec copy \
  -vcodec libx265 -y source.hevc.mp4
```

> Note: Please make sure your FFmpeg is 5.0 and libx265 is enabled.

Open `source.hevc.mp4` in Chrome 105+ directly, it should works.

You can also move the file to SRS webserver:

```bash
mkdir -p ~/git/srs/trunk/objs/nginx/html/vod/
mv source.hevc.mp4 ~/git/srs/trunk/objs/nginx/html/vod
```

Then open by [srs-player](http://localhost:8080/players/srs_player.html?app=vod&stream=source.hevc.mp4&autostart=true)

## Safari WebRTC

Safari supports WebRTC, if you enable it by:

* English version: `Develop > Experimental Features > WebRTC H265 codec`
* Chinese version: `Development > Experimental Features > WebRTC H265 codec`

Then open the url in safari, to publish or play WebRTC stream:

* Play [http://localhost:1985/rtc/v1/whep/?app=live&stream=livestream&codec=hevc](http://localhost:8080/players/whep.html?autostart=true&codec=hevc)
* Publish [http://localhost:1985/rtc/v1/whip/?app=live&stream=livestream&codec=hevc](http://localhost:8080/players/whip.html?autostart=true&codec=hevc)

Please follow other section to publish HEVC stream.

## Thanks for Contributors

There is a list of commits and contributors about HEVC in SRS:

* [H265: For #1747, Support HEVC/H.265 in SRT/RTMP/HLS.](https://github.com/ossrs/srs-gb28181/commit/3ca11071b45495e82d2d6958e5d0f7eab05e71e5)
* [H265: For #1747, Fix build fail bug for H.265](https://github.com/ossrs/srs-gb28181/commit/e355f3c37228f3602c88fed68e8fe5e6ba1153ea)
* [H265: For #1747, GB28181 support h.265 (#2037)](https://github.com/ossrs/srs-gb28181/commit/b846217bc7f94034b33bdf918dc3a49fb17947e0)
* [H265: fix some important bugs (#2156)](https://github.com/ossrs/srs-gb28181/commit/26218965dd083d13173af6eb31fcdf9868b753c6)
* [H265: Deliver the right hevc nalu and dump the wrong nalu. (#2447)](https://github.com/ossrs/srs-gb28181/commit/a13b9b54938a14796abb9011e7a8ee779439a452)
* [H265: Fix multi nal hevc frame demux fail. #2494](https://github.com/ossrs/srs-gb28181/commit/6c5e6090d7c82eb37530e109c230cabaedf948e1)
* [H265: Fix build error #2657 #2664](https://github.com/ossrs/srs-gb28181/commit/eac99e19fba6063279b9e47272523014f5e3334a)
* [H265: Update mpegts demux in srt. #2678](https://github.com/ossrs/srs-gb28181/commit/391c1426fc484c990e4324a4ae2f0de900074578)
* [H265: Fix the stat issue for h265. (#1949)](https://github.com/ossrs/srs-gb28181/commit/b4486e3b51281b4c227b2cc4f58d2b06db599ce0)
* [H265: Add h265 codec written support for MP4 format. (#2697)](https://github.com/ossrs/srs-gb28181/commit/3175d7e26730a04b27724e55dc95ef86c1f2886e)
* [H265: Add h265 for SRT.](https://github.com/runner365/srs/commit/0fa86e4f23847e8a46e3d0e91e0acd2c27047e11)

We will merge some of these commits to SRS 6.0, but not all commits.

* [PULL HEVC over WebRTC by Safari. v6.0.34](https://github.com/ossrs/srs/pull/3441)
* [GB: Support H.265 for GB28181. v6.0.25 (#3408)](https://github.com/ossrs/srs/pull/3408)
* [H265: Support HEVC over SRT. v6.0.20 (#465) (#3366)](https://github.com/ossrs/srs/pull/3366)
* [H265: Support DVR HEVC stream to MP4. v6.0.14](https://github.com/ossrs/srs/pull/3360)
* HLS: Support HEVC over HLS. v6.0.11
* [HEVC: The codec information is incorrect. v6.0.5](https://github.com/ossrs/srs/issues/3271)
* FFmpeg support libx265 and HEVC over RTMP/FLV: [CentOS7](https://github.com/ossrs/dev-docker/commit/0691d016adfe521f77350728d15cead8086d527d), [Ubuntu20](https://github.com/ossrs/dev-docker/commit/0e36323d15544ffe2901d10cfd255d9ef08fb250) and [Encoder](https://github.com/ossrs/dev-docker/commit/782bb31039653f562e0765a0c057d9f9babd1d1f).
* [H265: Support HEVC over HTTP-TS. v6.0.4](https://github.com/ossrs/srs/commit/70d5618979e5c8dc41b7cd87c78db7ca2b8a10e8)
* [H265: Support parse multiple NALUs in a frame. v6.0.3](https://github.com/ossrs/srs/commit/f316e9a0de3a892d25f2d8e7efd28ee9334f5bd6)
* [H265: Support HEVC over RTMP or HTTP-FLV. v6.0.2](https://github.com/ossrs/srs/commit/178e40a5fc3cf0856ace914ae61696a73007f5bf)
* [H265: Update mpegts.js to play HEVC over HTTP-TS/FLV. v6.0.1](https://github.com/ossrs/srs/commit/7e02d972ea74faad9f4f96ae881d5ece0b89f33b)

## Known Issues

1. HEVC over Safari WebRTC, only support WebRTC to WebRTC, doesn't support converting to RTMP.
2. Chrome/Firefox does not support HEVC, no any plan as I know.
3. Almost all browsers supports MSE, except iOS. HEVC over MSE requires hardware decoder.
4. Apart from mpegts.js, other H5 players such as hls.js/dash.js doesn't support HEVC.

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/hevc)


