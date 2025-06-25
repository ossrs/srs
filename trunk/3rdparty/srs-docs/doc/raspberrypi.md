---
title: RaspBerryPi
sidebar_label: RaspBerryPi
hide_title: false
hide_table_of_contents: false
---

# Performance benchmark for SRS on RaspberryPi

SRS can running on armv6(RaspberryPi) or armv7(Android). 
The bellow data show the performance benchmark.

## Install SRS

Download the binary for armv6 from [Github](http://ossrs.net/srs.release/releases/) 
or [SRS Server](http://ossrs.net/srs/releases/)

## RaspberryPi

The hardware of raspberrypi:
* [RaspberryPi](http://item.jd.com/1014155.html)：Type B
* <strong>SoC</strong> BroadcomBCM2835(CPU,GPU,DSP,SDRAM,USB)
* <strong>CPU</strong> ARM1176JZF-S(ARM11) 700MHz
* <strong>GPU</strong> Broadcom VideoCore IV, OpenGL ES 2.0, 1080p 30 h.264/MPEG-4 AVC decoder
* <strong>RAM</strong> 512MByte
* <strong>USB</strong> 2 x USB2.0
* <strong>VideoOutput</strong> Composite RCA(PAL&NTSC), HDMI(rev 1.3&1.4), raw LCD Panels via DSI 14 HDMI resolution from 40x350 to 1920x1200 plus various PAL and NTSC standards
* <strong>AudioOutput</strong> 3.5mm, HDMI
* <strong>Storage</strong> SD/MMC/SDIO socket
* <strong>Network</strong> 10/100 ethernet
* <strong>Device</strong> 8xGPIO, UART, I2C, SPI bus, +3.3V, +5V, ground(nagetive)
* <strong>Power</strong> 700mA(3.5W) 5V
* <strong>Size</strong> 85.60 x 53.98 mm(3.370 x 2.125 in)
* <strong>OS</strong> Debian GNU/linux, Fedora, Arch Linux ARM, RISC OS, XBMC

Software:
* RaspberryPi img：2014-01-07-wheezy-raspbian.img
* <strong>uname</strong>: Linux raspberrypi 3.10.25+ #622 PREEMPT Fri Jan 3 18:41:00 GMT 2014 armv6l GNU/Linux
* <strong>cpu</strong>: arm61
* <strong>Server</strong>: srs 0.9.38
* <strong>ServerType</strong>: raspberry pi
* <strong>Client</strong>：[srs-bench](https://github.com/ossrs/srs-bench)
* <strong>ClientType</strong>: Virtual Machine Centos6
* <strong>Play</strong>: PC win7, flash
* <strong>Network</strong>: 100Mbps

Stream information:
* Video Bitrate: 200kbps
* Resolution: 768x320
* Audio Bitrate: 30kbps

For arm [SRS: arm](./arm.md#raspberrypi)

## OS settings

Login as root, set the fd limits:

* Set limit: `ulimit -HSn 10240`
* View the limit:

```bash
[root@dev6 ~]# ulimit -n
10240
```

* Restart SRS：`sudo /etc/init.d/srs restart`

## Publish and Play

Use centos to publish to SRS:

* Start FFMPEG:

```bash
for((;;)); do \
    ./objs/ffmpeg/bin/ffmpeg \
        -re -i doc/source.flv \
        -acodec copy -vcodec copy \
        -f flv rtmp://192.168.1.105:1935/live/livestream; \
    sleep 1; 
done
```

* Play RTMP: `rtmp://192.168.1.105:1935/live/livestream`
* Online Play: [Online Player](http://localhost:8080/players/srs_player.html?autostart=true&stream=livestream.flv&port=8080&schema=http)

## Client

The RTMP load test tool, read [srs-bench](https://github.com/ossrs/srs-bench)

The sb_rtmp_load used to test RTMP load, support 800-3k concurrency for each process.

* Build: `./configure && make`
* Start: `./objs/sb_rtmp_load -c 800 -r <rtmp_url>`

## Record Data

Record data before test:

* The cpu for SRS:

```bash
pid=`ps aux|grep srs|grep objs|awk '{print $2}'` && top -p $pid
```

* The cpu for srs-bench:

```bash
pid=`ps aux|grep load|grep rtmp|awk '{print $2}'` && top -p $pid
```

* The connections:

```bash
for((;;)); do \
    srs_connections=`sudo netstat -anp|grep 1935|grep ESTABLISHED|wc -l`;  \
    echo "srs_connections: $srs_connections";  \
    sleep 5;  \
done
```

* The bandwidth in NBps:

```bash
[winlin@dev6 ~]$ dstat 30
----total-cpu-usage---- -dsk/total- -net/lo- ---paging-- ---system--
usr sys idl wai hiq siq| read  writ| recv  send|  in   out | int   csw 
  0   0  96   0   0   3|   0     0 |1860B   58k|   0     0 |2996   465 
  0   1  96   0   0   3|   0     0 |1800B   56k|   0     0 |2989   463 
  0   0  97   0   0   2|   0     0 |1500B   46k|   0     0 |2979   461 
```

* The table

| Server | CPU | Mem | Conn | ENbps | ANbps | sb | Lat |
| ------ | --- | ------ | ------- | ---------- | ---------- | ------- | ------- |
| SRS | 1.0% | 3MB | 3 | - | - | - | 0.8s |

Memory(Mem): The memory usage for server.

Clients(Conn): The cocurrency connections to server.

ExpectNbps(ENbps): The expect network bandwidth in Xbps.

ActualNbps(ANbps): The actual network bandwidth in Xbps.

## Benchmark SRS 0.9.38

Let's start performance benchmark.

* The data for 10 clients:

```bash
./objs/sb_rtmp_load -c 10 -r rtmp://192.168.1.105:1935/live/livestream >/dev/null &
```

| Server | CPU | Mem | Conn | ENbps | ANbps | sb | Lat |
| ------ | --- | ------ | ------- | ---------- | ---------- | ------- | ------- |
| SRS | 17% | 1.4MB | 11 | 2.53Mbps | 2.6Mbps | 1.3% | 1.7s |

* The data for 20 clients:

| Server | CPU | Mem | Conn | ENbps | ANbps | sb | Lat |
| ------ | --- | ------ | ------- | ---------- | ---------- | ------- | ------- |
| SRS | 23% | 2MB | 21 | 4.83Mbps | 5.5Mbps | 2.3% | 1.5s |

* The data for 30 clients:

| Server | CPU | Mem | Conn | ENbps | ANbps | sb | Lat |
| ------ | --- | ------ | ------- | ---------- | ---------- | ------- | ------- |
| SRS | 50% | 4MB | 31 | 7.1Mbps | 8Mbps | 4% | 2s |

The summary for RaspberryPi Type B, 230kbps performance:

| Server | CPU | Mem | Conn | ENbps | ANbps | sb | Lat |
| ------ | --- | ------ | ------- | ---------- | ---------- | ------- | ------- |
| SRS | 17% | 1.4MB | 11 | 2.53Mbps | 2.6Mbps | 1.3% | 1.7s |
| SRS | 23% | 2MB | 21 | 4.83Mbps | 5.5Mbps | 2.3% | 1.5s |
| SRS | 50% | 4MB | 31 | 7.1Mbps | 8Mbps | 4% | 2s |

## Benchmark SRS 0.9.72

The benchmark for RTMP SRS 0.9.72.

| Server | CPU | Mem | Conn | ENbps | ANbps | sb | Lat |
| ------ | --- | ------ | ------- | ---------- | ---------- | ------- | ------- |
| SRS | 5% | 2MB | 2 | 1Mbps | 1.2Mbps | 0% | 1.5s |
| SRS | 20% | 2MB | 12 | 6.9Mbps | 6.6Mbps | 2.8% | 2s |
| SRS | 36% | 2.4MB | 22 | 12.7Mbps | 12.9Mbps | 2.3% | 2.5s |
| SRS | 47% | 3.1MB | 32 | 18.5Mbps | 18.5Mbps | 5% | 2.0s |
| SRS | 62% | 3.4MB | 42 | 24.3Mbps | 25.7Mbps | 9.3% | 3.4s |
| SRS | 85% | 3.7MB | 52 | 30.2Mbps | 30.7Mbps | 13.6% | 3.5s |

## cubieboard benchmark

No data.

Winlin 2014.11

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/raspberrypi)


