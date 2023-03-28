# srs-bench

SB(SRS Bench) is a set of benchmark and regression test tools, for SRS and other media servers, supports HTTP-FLV, RTMP,
HLS, WebRTC and GB28181.

For RTMP/HLS/FLV benchmark, please use branch [master](https://github.com/ossrs/srs-bench/tree/master).

## Usage

下载代码和编译：

```bash
git clone -b feature/rtc https://github.com/ossrs/srs-bench.git && 
cd srs-bench && make
```

编译会生成下面的工具：

* `./objs/srs_bench` 压测，模拟大量客户端的负载测试，支持SRS、GB28181和Janus三种场景。 
* `./objs/srs_test` 回归测试(SRS)，SRS服务器的回归测试。
* `./objs/srs_gb28181_test` 回归测试(GB28181)，GB服务器的回归测试。
* `./objs/srs_blackbox_test` 黑盒测试(SRS)，SRS服务器的黑盒测试，也可以换成其他媒体服务器。

> Note: 查看工具的全部参数请执行`./objs/xx -h`

有些场景，若需要编译和启动SRS:

```bash
git clone https://github.com/ossrs/srs.git &&
cd srs/trunk && ./configure --h265=on --gb28181=on && make &&
./objs/srs -c conf/console.conf
```

具体场景，请按下面的操作启动测试。

## Player for Live

直播播放压测，一个流，很多个播放。

首先，推流到SRS：

```bash
ffmpeg -re -i doc/source.200kbps.768x320.flv -c copy -f flv -y rtmp://localhost/live/livestream
```

然后，启动压测，比如100个：

```bash
./objs/srs_bench -sr webrtc://localhost/live/livestream -nn 100
```

## Publisher for Live or RTC

直播或会议场景推流压测，一般会推多个流。

首先，推流依赖于录制的文件，请参考[DVR](#dvr)。

然后，启动推流压测，比如100个流：

```bash
./objs/srs_bench -pr webrtc://localhost/live/livestream_%d -sn 100 -sa a.ogg -sv v.h264 -fps 25
```

> 注意：帧率是原始视频的帧率，由于264中没有这个信息所以需要传递。

## Multipel Player or Publisher for RTC

会议场景的播放压测，会多个客户端播放多个流，比如3人会议，那么就有3个推流，每个流有2个播放。

首先，启动推流压测，比如3个流：

```bash
./objs/srs_bench -pr webrtc://localhost/live/livestream_%d -sn 3 -sa a.ogg -sv v.h264 -fps 25
```

然后，每个流都启动播放压测，比如每个流2个播放：

```bash
./objs/srs_bench -sr webrtc://localhost/live/livestream_%d -sn 3 -nn 2
```

> 备注：压测都是基于流，可以任意设计推流和播放的流路数，实现不同的场景。

> 备注：URL的变量格式参考Go的`fmt.Sprintf`，比如可以用`webrtc://localhost/live/livestream_%03d`。

<a name="dvr"></a>
## DVR for Benchmark

录制场景，主要是把内容录制下来后，可分析，也可以用于推流。

首先，推流到SRS，参考[live](#player-for-live)。

```bash
ffmpeg -re -i doc/source.200kbps.768x320.flv -c copy -f flv -y rtmp://localhost/live/livestream
```

然后，启动录制：

```bash
./objs/srs_bench -sr webrtc://localhost/live/livestream -da a.ogg -dv v.h264
```

> 备注：录制下来的`a.ogg`和`v.h264`可以用做推流。

## RTC Plaintext

压测RTC明文播放：

首先，推流到SRS：

```bash
ffmpeg -re -i doc/source.200kbps.768x320.flv -c copy -f flv -y rtmp://localhost/live/livestream
```

然后，启动压测，指定是明文（非加密），比如100个：

```bash
./objs/srs_bench -sr webrtc://localhost/live/livestream?encrypt=false -nn 100
```

> Note: 可以传递更多参数，详细参考SRS支持的参数。

## Regression Test

回归测试需要先启动[SRS](https://github.com/ossrs/srs/issues/307)，支持WebRTC推拉流：

```bash
./objs/srs -c conf/rtc.conf
```

然后运行回归测试用例，如果只跑一次，可以直接运行：

```bash
go test ./srs -mod=vendor -v -count=1
```

也可以用make编译出重复使用的二进制：

```bash
make && ./objs/srs_test -test.v
```

> Note: 注意由于pion不支持`DTLS 1.0`，所以SFU必须要支持`DTLS 1.2`才行。

运行结果如下：

```bash
$ make && ./objs/srs_test -test.v
=== RUN   TestRTCServerVersion
--- PASS: TestRTCServerVersion (0.00s)
=== RUN   TestRTCServerPublishPlay
--- PASS: TestRTCServerPublishPlay (1.28s)
PASS
```

可以给回归测试传参数，这样可以测试不同的序列，比如：

```bash
go test ./srs -mod=vendor -v -count=1 -srs-server=127.0.0.1
# Or
make && ./objs/srs_test -test.v -srs-server=127.0.0.1
```

可以只运行某个用例，并打印详细日志，比如：

```bash
make && ./objs/srs_test -test.v -srs-log -test.run TestRtcBasic_PublishPlay
```

支持的参数如下：

* `-srs-server`，RTC服务器地址。默认值：`127.0.0.1`
* `-srs-stream`，RTC流地址，一般会加上随机的后缀。默认值：`/rtc/regression`
* `-srs-timeout`，每个Case的超时时间，毫秒。默认值：`5000`，即5秒。
* `-srs-publish-audio`，推流时，使用的音频文件。默认值：`avatar.ogg`
* `-srs-publish-video`，推流时，使用的视频文件。默认值：`avatar.h264`
* `-srs-publish-video-fps`，推流时，视频文件的FPS。默认值：`25`
* `-srs-vnet-client-ip`，设置[pion/vnet](https://github.com/ossrs/srs-bench/blob/feature/rtc/vnet/example_test.go)客户端的虚拟IP，不能和服务器IP冲突。默认值：`192.168.168.168`

其他不常用参数：

* `-srs-log`，是否开启详细日志。默认值：`false`
* `-srs-play-ok-packets`，播放时，收到多少个包认为是测试通过，默认值：`10`
* `-srs-publish-ok-packets`，推流时，发送多少个包认为时测试通过，默认值：`10`
* `-srs-https`，是否连接HTTPS-API。默认值：`false`，即连接HTTP-API。
* `-srs-play-pli`，播放时，PLI的间隔，毫秒。默认值：`5000`，即5秒。
* `-srs-dtls-drop-packets`，DTLS丢包测试，丢了多少个包算成功，默认值：`5`

> Note: 查看全部参数请执行`./objs/srs_test -h`

<a name="gb28181"></a>
## GB28181 Test

支持GB28181的压测，使用选项`-sfu gb28181`可以查看帮助：

```bash
make && ./objs/srs_bench -sfu gb28181 --help
```

运行回归测试用例，更多命令请参考[Regression Test](#regression-test)：

```bash
go test ./gb28181 -mod=vendor -v -count=1
```

也可以用make编译出重复使用的二进制：

```bash
make && ./objs/srs_gb28181_test -test.v
```

支持的参数如下：

* `-srs-sip`，SIP服务器地址。默认值：`tcp://127.0.0.1:5060`
* `-srs-stream`，GB的user，即流名称，一般会加上随机的后缀。默认值：`3402000000`
* `-srs-timeout`，每个Case的超时时间，毫秒。默认值：`11000`，即11秒。
* `-srs-publish-audio`，推流时，使用的音频文件。默认值：`avatar.aac`
* `-srs-publish-video`，推流时，使用的视频文件，注意：扩展名`.h264`表明编码格式为`AVC`，`.h265`表明编码格式为`HEVC`。默认值：`avatar.h264`
* `-srs-publish-video-fps`，推流时，视频文件的FPS。默认值：`25`

其他不常用参数：

* `-srs-log`，是否开启详细日志。默认值：`false`

> Note: 查看全部参数请执行`./objs/srs_gb28181_test -h`

## Blackbox Test

使用FFmpeg作为客户端，对流媒体服务器SRS进行黑盒压测，完全黑盒的回归测试。

运行回归测试用例，如果只跑一次，可以直接运行：

```bash
go test ./blackbox -mod=vendor -v -count=1
```

也可以用make编译出重复使用的二进制：

```bash
make && ./objs/srs_blackbox_test -test.v
```

由于黑盒测试依赖特殊的FFmpeg，可以在Docker中编译和启动：

```bash
docker run --rm -it -v $(pwd):/g -w /g ossrs/srs:ubuntu20 bash
make && ./objs/srs_blackbox_test -test.v
```

> Note: 依赖SRS二进制，当然也可以在这个Docker中编译SRS，具体请参考SRS的Wiki。

支持的参数如下：

* `-srs-binary`，每个测试用例都需要启动一个SRS服务，因此需要设置SRS的位置。默认值：`../../objs/srs`
* `-srs-ffmpeg`，FFmpeg工具的位置，用来推流和录制。默认值：`ffmpeg`
* `-srs-ffprobe`，ffprobe工具的位置，用来分析流的信息。默认值：`ffprobe`
* `-srs-timeout`，每个Case的超时时间，毫秒。默认值：`64000`，即64秒。
* `-srs-publish-avatar`，测试源文件路径。默认值：`avatar.flv`。
* `-srs-ffprobe-duration`，每个Case的探测时间，毫秒。默认值：`16000`，即16秒。
* `-srs-ffprobe-timeout`，每个Case的探测超时时间，毫秒。默认值：`21000`，即21秒。

其他不常用参数：

* `-srs-log`，是否开启详细日志。默认值：`false`
* `-srs-stdout`，是否开启SRS的stdout详细日志。默认值：`false`
* `-srs-ffmpeg-stderr`，是否开启FFmpeg的stderr详细日志。默认值：`false`
* `-srs-dvr-stderr`，是否开启DVR的stderr详细日志。默认值：`false`
* `-srs-ffprobe-stdout`，是否开启FFprobe的stdout详细日志。默认值：`false`

由于每个黑盒的用例时间都很长，可以开启并行：

```bash
./objs/srs_blackbox_test -test.v -test.parallel 8
```

> Note: 查看全部参数请执行`./objs/srs_blackbox_test -h`

## GCOVR

本机生成覆盖率时，我们使用工具[gcovr](https://gcovr.com/en/stable/guide.html)。

在macOS上安装gcovr：

```bash
pip3 install gcovr
```

在CentOS上安装gcovr：

```bash
yum install -y python2-pip &&
pip install lxml && pip install gcovr
```

## Janus

支持Janus的压测，使用选项`-sfu janus`可以查看帮助：

```bash
make && ./objs/srs_bench -sfu janus --help
```

首先需要启动Janus，推荐使用[janus-docker](https://github.com/winlinvip/janus-docker#usage):

```bash
ip=$(ifconfig en0 inet|grep inet|awk '{print $2}') &&
sed -i '' "s/nat_1_1_mapping.*/nat_1_1_mapping=\"$ip\"/g" janus.jcfg &&
docker run --rm -it -p 8080:8080 -p 8443:8443 -p 20000-20010:20000-20010/udp \
    -v $(pwd)/janus.jcfg:/usr/local/etc/janus/janus.jcfg \
    -v $(pwd)/janus.plugin.videoroom.jcfg:/usr/local/etc/janus/janus.plugin.videoroom.jcfg \
    registry.cn-hangzhou.aliyuncs.com/ossrs/janus:v1.0.7
```

> 若启动成功，打开页面，可以自动入会：[http://localhost:8080](http://localhost:8080)

模拟5个推流入会，可以在页面看到入会的流：

```bash
make -j10 && ./objs/srs_bench -sfu janus \
  -pr webrtc://localhost:8080/2345/livestream \
  -sa avatar.ogg -sv avatar.h264 -fps 25 -sn 5
```

模拟5个拉流入会，只拉流不推流：

```bash
make -j10 && ./objs/srs_bench -sfu janus \
  -sr webrtc://localhost:8080/2345/livestream \
  -nn 5
```

2021.01, Winlin
