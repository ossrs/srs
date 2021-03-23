# srs-bench

WebRTC benchmark on [pion/webrtc](https://github.com/pion/webrtc) for [SRS](https://github.com/ossrs/srs).

## Usage

编译和使用：

```bash
git clone https://github.com/ossrs/srs-bench.git && git checkout feature/rtc && 
make && ./objs/srs_bench -h
```

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

## DVR

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
if [[ ! -z $(ifconfig en0 inet| grep 'inet '|awk '{print $2}') ]]; then 
  docker run -p 1935:1935 -p 8080:8080 -p 1985:1985 -p 8000:8000/udp \
      --rm --env CANDIDATE=$(ifconfig en0 inet| grep 'inet '|awk '{print $2}')\
      registry.cn-hangzhou.aliyuncs.com/ossrs/srs:v4.0.76 objs/srs -c conf/rtc.conf
fi
```

然后运行回归测试用例，如果只跑一次，可以直接运行：

```bash
go test ./srs -mod=vendor -v
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
go test ./srs -mod=vendor -v -srs-server=127.0.0.1
# Or
make && ./objs/srs_test -test.v -srs-server=127.0.0.1
```

可以只运行某个用例，并打印详细日志，比如：

```bash
make && ./objs/srs_test -test.v -srs-log -test.run TestRtcBasic_PublishPlay
```

支持的参数如下：

* `-srs-server`，RTC服务器地址。默认值：`127.0.0.1`
* `-srs-stream`，RTC流地址。默认值：`/rtc/regression`
* `-srs-timeout`，每个Case的超时时间，毫秒。默认值：`3000`，即3秒。
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

2021.01, Winlin
