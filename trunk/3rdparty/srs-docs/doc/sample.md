---
title: Use Scenarios
sidebar_label: Use Scenarios
hide_title: false
hide_table_of_contents: false
---

# Use Scenarios

一般来讲，SRS的应用方式有以下几类：

1. 搭建大规模CDN集群，可以在CDN内部的源站和边缘部署SRS。
2. 小型业务快速搭建几台流媒体集群，譬如学校、企业等，需要分发的流不多，同时CDN覆盖不如自己部署几个节点，可以用SRS搭建自己的小集群。
3. SRS作为源站，CDN作为加速边缘集群。比如推流到CDN后CDN转推到源站，播放时CDN会从源站取流。这样可以同时使用多个CDN。同时还可以在源站做DRM和DVR，输出HLS，更重要的是如果直接推CDN一般CDN之间不是互通的，当一个CDN出现故障无法快速切换到其他CDN。
4. 编码器可以集成SRS支持拉流。一般编码器支持推RTMP/UDP流，如果集成SRS后，可以支持拉多种流。
5. 协议转换网关，比如可以推送FLV到SRS转成RTMP协议，或者拉RTSP转RTMP，还有拉HLS转RTMP。SRS只要能接入流，就能输出能输出的协议。
6. 学习流媒体可以用SRS。SRS提供了大量的协议的文档，wiki，和文档对应的代码，详细的issues，流媒体常见的功能实现，还有新流媒体技术的尝试。
7. 还可以装逼用，在SRS微信群里涉及到很多流媒体和传输的问题，是个装逼的好平台。 

## Quzhibo

趣直播，一个知识直播平台，目前直播技术为主。

主要流程：

* obs 直播
* 有三台hls 服务器，主 srs 自动 forward 到 srs，然后那三台切割
* 有两台 flv 服务器，remote 拉群，发现有时会挂掉，用了个监控srs的脚本，一发现挂掉立马重启
* srs 推流到七牛，利用七牛接口，来生成 m3u8 回放 这样可以结束后，立马看到回放

## Europe: Forward+EXEC

BEGINHO STREAMING PROJECT

I needed solution for pushing streams from origin server 
to edge server. On origin server all streams are avaliable 
in multicast (prepared with ffmpeg, h264 in mpegts container). 
But routing multicast through GRE tunnel to the edge 
server was very buggy. Any networks hickups in origin-edge 
route were affecting streams in bad way (freezeing, pixelation and such)...
So, I found SRS project and after some reading of docs, I 
decided to give it a try. Most intereseting feature of SRS 
to me was a "forward" option. It allows to push all streams 
you have avaliable on local server (SRS origin) to remote 
server (SRS edge) with a single line in config file.
https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-forward

SRS2 config on origin server:
```
    vhost __defaultVhost__ {
           forward         xxx:19350;
    }
```

I "told" to ffmpegs on transcoder to publish stream to rtmp, 
instead of multicast (and yes, I used multicast group as rtmp stream name):
```
    ffmpeg -i udp://xxx:1234 -vcodec libx264 -acodec libfdk_aac \
      -metadata service_name="Channel 1" -metadata service_provider="PBS" \
      -f flv rtmp://xxx:1935/live/xxx:1234
```

Tested stream with ffprobe:
```
    [root@encoder1 ~]# ffprobe rtmp://xxx:1935/live/xxx:1234
    Input #0, flv, from 'rtmp://xxx:1935/live/xxx:1234':
      Metadata:
        service_name    : Channel 1
        service_provider: PBS
        encoder         : Lavf57.24.100
        server          : SRS/2.0.209(ZhouGuowen)
        srs_primary     : SRS/1.0release
        srs_authors     : winlin,wenjie.zhao
        server_version  : 2.0.209
      Duration: N/A, start: 0.010000, bitrate: N/A
        Stream #0:0: Audio: aac (LC), 48000 Hz, stereo, fltp, 128 kb/s
        Stream #0:1: Video: h264 (High), yuvj420p(pc, bt709), 720x576 [SAR 16:11 DAR 20:11], 24 fps, 24 tbr, 1k tbn
```

On edge server (example IP xxx), there is a streaming software 
wich accepts only mpegts as source. So, after receiving rtmp streams 
from origin server, I needed all streams back to mpegts.
SRS have support for several types for output (hls, hds, rtmp, http-flv...) 
but not mpegts, and i need udp mpegts. Then I asked Winlin for help 
and he suggested to use SRS3 on edge server, as SRS3 have an feature 
that SRS2 dont, and thats "exec" option. In SRS3 config, you can use 
exec option, to call ffmpeg for every incoming stream and convert it to
whatever you like. I compiled SRS3 with "--with-ffmpeg" switch 
(yes, source tree comes with ffmpeg in it) on edge server and...

SRS3 config on edge:
```
    listen              19350;
    max_connections     1024;
    srs_log_tank        file;
    srs_log_file        ./objs/srs.slave.log;
    srs_log_level       error;
    vhost __defaultVhost__ {
        exec {
            enabled     on;
            publish     ./objs/ffmpeg/bin/ffmpeg -v quiet -re -i rtmp://127.0.0.1:1935/[app]?vhost=[vhost]/[stream] -c copy -vbsf h264_mp4toannexb -f mpegts "udp://[stream]?localaddr=127.0.0.1&pkt_size=1316";
        }
    }
```

FFmpeg will convert all incoming streams to udp mpegts, binding them 
to lo (127.0.0.1) interface (you dont want multicast to leak all around).
SRS3 will use [stream] for udp address, thats why rtmp stream have name 
by its multicast group on origin server ;)
When converting from rtmp to mpegts, "-vbsf h264_mp4toannexb" option is needed!
After starting SRS3 with this config, i checked is stream forwarded from 
master server properly. So, ffprobe again, now on edge server:
```
    [root@edge ~]# ffprobe  udp://xxx:1234?localaddr=127.0.0.1
    Input #0, mpegts, from 'udp://xxx:5002?localaddr=127.0.0.1':
      Duration: N/A, start: 29981.146500, bitrate: 130 kb/s
      Program 1
        Metadata:
          service_name    : Channel 1
          service_provider: PBS
        Stream #0:0[0x100]: Video: h264 (High) ([27][0][0][0] / 0x001B), yuvj420p(pc, bt709), 720x576 [SAR 16:11 DAR 20:11], 24 fps, 24 tbr, 90k tbn, 180k tbc
        Stream #0:1[0x101]: Audio: aac ([15][0][0][0] / 0x000F), 48000 Hz, stereo, fltp, 130 kb/s
```

I keep adding new streams with ffmpeg at origin server and they are avaliable 
on slave server after second or two. Its almost a year when I started this origin 
and edge SRS instances and they are still working without single restart ;)

Many thanks to Winlin!

## LijiangTV

[丽江热线](https://www.lijiangtv.com/live/)，丽江广播电视台。

## UPYUN

2015，[又拍云直播部分](https://www.upyun.com/solutions/video.html)，在SRS3基础上深度定制的版本。

## bravovcloud

2015，[观止云直播服务器](http://www.bravovcloud.com/product/yff/)，在SRS3基础上深度定制的版本。

## gosun

2014.11，[高升CDN直播部分](http://www.gosun.com/service/streaming_acceleration.html)，在SRS1的基础上深度定制的版本。

## 北京云博视

2014.10.10 by 谁变  63110982<br/>
[http://www.y-bos.com/](http://www.y-bos.com/)

## verycdn

[verycdn](http://www.verycdn.cn/) 开始用SRS。

2014.9.13 by 1163202026 11:19:35<br/>
目前SRS在测试中，没用过别的，直接上的srs，目前测试下来比较OK，没什么大问题。

## SRS产品使用者

2014.7.23 by 阿才(1426953942) 11:04:01 <br/>
我接触srs才几个月，不敢发表什么意见，只是通过这段时间的学习，觉得这个项目做得相当棒，作者及项目团队工作相当出色，精神非常值得赞赏，目前还在学习中。

2014.7.23 by 随想曲(156530446) 11:04:48 <br/>
我作为使用者来说，就是这玩意完全当成正规高大上的产品用啦！

2014.7.23 by 湖中鱼(283946467) 11:06:23 <br/>
me没怎么去具体分析srs只是觉得作者文档写得比较流畅不乏幽默感。但是目前我用到的功能只有rtmp推送直播，及hls这些nginx-rtmp都有，所以还是选择了用老外的东西

2014.7.23 by 我是蝈蝈(383854294)  11:11:59 <br/>
为什么用SRS？轻便，省资源，有中文说明。SRS那些一站式的脚本与演示demo就能看出来作者是很用心的

## web秀场

2014.7 by 刘重驰

我们目前正在调研 准备用到web秀场 和 移动端流媒体服务上

## 视频直播

2014.7 by 大腰怪

## 远程视频直播

2014.7 by 韧

我们的分发服务器用的就是srs，简单易用，稳定性好

我们以前也用过几个分发软件，都没有srs好用，真心的

## chnvideo

2014.7 [chnvideo](http://chnvideo.com/)编码器内置SRS提供RTMP和HLS拉服务。

## 某工厂监控系统

2014.4 by 斗破苍穷(154554381)

某工厂的监控系统主要组成：
* 采集端：采集端采用IPC摄像头安装在工厂重要监控位置，通过网线或者wifi连接到监控中心交换机。
* 监控中心：中心控制服务器，负责管理采集端和流媒体服务器，提供PC/Android/IOS观看平台。
* 流媒体服务器：负责接收采集端的流，提供观看端RTMP/HLS的流。
* 观看端：PC/Android/IOS。要求PC端的延迟在3秒内。Android/IOS延迟在20秒之内。

主要流程包括：
* 采集端启动：IPC摄像头像监控中心注册，获得发布地址，并告知监控中心采集端的信息，譬如摄像头设备名，ip地址，位置信息之类。
* 采集端开始推流：IPC摄像头使用librtmp发布到地址，即将音频视频数据推送到RTMP流媒体服务器。
* 流媒体服务器接收流：流媒体服务器使用SRS，接收采集端的RTMP流。FMS-3/3.5/4.5都有问题，估计是和librtmp对接问题。
* 观看端观看：用户使用PC/Android/IOS登录监控中心后，监控中心返回所有的摄像头信息和流地址。PC端使用flash，延迟在3秒之内；Android/IOS使用HLS，延迟在20秒之内。
* 时移：监控中心会开启录制计划，将RTMP流录制为FLV文件。用户可以在监控中心观看录制的历史视频。

## 网络摄像机

2014.4 by camer(2504296471)

网络摄像机使用hi3518芯片，如何用网页无插件直接观看网络摄像机的流呢？

目前有应用方式如下：
* hi3518上跑采集和推流程序（用srslibrtmp）
* 同时hi3518上还跑了srs/nginx-rtmp作为服务器
* 推流程序推到hi3518本机的nginx服务器
* PC上网页直接观看hi3518上的流

## IOS可以看的监控

2014.3 by 独孤不孤独(378668966)

一般监控摄像头只支持输出RTMP/RTSP，或者支持RTSP方式读取流。如果想在IOS譬如IPad上看监控的流，怎么办？先部署一套rtmp服务器譬如nginx-rtmp/crtmpd/wowza/red5之类，然后用ffmpeg把rtsp流转成rtmp（或者摄像头直接推流到rtmp服务器），然后让服务器切片成hls输出，在IOS上观看。想想都觉得比较麻烦额，如果摄像头比较多怎么办？一个服务器还扛不住，部署集群？

最简单的方式是什么？摄像头自己支持输出HLS流不就好了？也就是摄像头有个内网ip作为服务器，摄像头给出一个hls的播放地址，IOS客户端譬如IPad可以播放这个HLS地址。

SRS最适合做这个事情，依赖很少，提供[arm编译脚本](./sample-arm.md)，只需要[改下configure的交叉编译工具](./arm.md#%E4%BD%BF%E7%94%A8%E5%85%B6%E4%BB%96%E4%BA%A4%E5%8F%89%E7%BC%96%E8%AF%91%E5%B7%A5%E5%85%B7)就可以编译了。

主要流程：
* 编译arm下的srs，部署到树莓派，在摄像头中启动srs。
* 使用ffmpeg将摄像头的rtsp以RTMP方式推到srs。或者用自己程序采集设备数据推送RTMP流到srs。
* srs分发RTMP流和HLS流。其实PC上也可以看了。
* IOS譬如IPad上播放HLS地址。

## 清华活动直播

2014.2 by youngcow(5706022)

清华大学每周都会有活动，譬如名家演讲等，使用SRS支持，少量的机器即可满足高并发。

主要流程：
* 在教室使用播控系统（摄像机+采集卡或者摄像机+导播台）推送RTMP流到主SRS
* 主SRS自动Forward给从SRS（参考[Forward](./forward.md)）
* PC客户端（Flash）使用FlowerPlayer，支持多个服务器的负载均衡
* FlowerPlayer支持在两个主从SRS，自动选择一个服务器，实现负载均衡

主要的活动包括：
* 2014-02-23，丘成桐清华演讲

## 某农场监控

2014.1 by 孙悟空

农场中摄像头支持RTSP访问，FFMPEG将RTSP转换成RTMP推送到SRS，flash客户端播放RTMP流。同时flash客户端可以和控制服务器通信，控制农场的浇水和施肥。

![农场植物开花了](http://ossrs.net/srs.release/wiki/images/application/farm.jpg)

截图：农场的植物开花了，据说种的是萝卜。。。

Winlin 2014.2

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/sample)


