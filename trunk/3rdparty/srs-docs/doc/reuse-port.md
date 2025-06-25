---
title: Reuse Port
sidebar_label: Reuse Port
hide_title: false
hide_table_of_contents: false
---

# Reuse Port

You can use REUSE_PORT for different use scenarios.

## For Edge Server

The [performance of SRS2](https://github.com/ossrs/srs/tree/2.0release#performance) is improved huge, but is it enough?
Absolutely NOT! In SRS3, we provide [OriginCluster](./sample-origin-cluster.md) for multiple origin servers to work together,
and [go-oryx](https://github.com/ossrs/go-oryx) as a tcp proxy for edge server, and these are not good enough, so we support
SO_REUSEPORT feature for multiple processes edge server.

![](/img/doc-guides-reuse-port-001.png)

> Remark: The SO_REUSEPORT requires Linux Kernel 3.9+, so you should upgrade your kernel for CentOS6, or you could choose Ubuntu20.

First, we start a edge server which listen at 1935:

```
./objs/srs -c conf/edge.conf
```

Then, at the same server, start another edge server which also listen at 1935:

```
./objs/srs -c conf/edge2.conf
```

> Note: They should use different pid file, or it will fail to start the second edge server.

There are two SRS edge servers:

```
[root@bf2e88b31f9b trunk]# ps aux|grep srs
root       381  0.1  0.0  19888  5752 pts/2    S+   08:03   0:01 ./objs/srs -c conf/edge.conf
root       383  0.0  0.0  19204  5468 pts/1    S+   08:04   0:00 ./objs/srs -c conf/edge2.conf

[root@bf2e88b31f9b trunk]# lsof -p 381
srs     381 root    7u     IPv6  18835      0t0        TCP *:macromedia-fcs (LISTEN)
[root@bf2e88b31f9b trunk]# lsof -p 383
srs     383 root    7u     IPv6  17831      0t0        TCP *:macromedia-fcs (LISTEN)
```

After that, we start the origin server, from which these edge server to pull streams:

```
./objs/srs -c conf/origin.conf 
```

Finally, we could publish to origin/edge, and play stream from each edge server:

```
    for((;;)); do \
        ./objs/ffmpeg/bin/ffmpeg -re -i ./doc/source.flv \
        -c copy \
        -f flv rtmp://192.168.1.170/live/livestream; \
        sleep 1; \
    done
```

Use VLC to play the RTMP stream: `rtmp://192.168.1.170:1935/live/livestream`

## For Origin Server

You can use REUSE_PORT in Origin Server. Each Origin Server is isolated, only works for HLS:

```
              +-----------------+
Client --->-- + Origin Servers  +------> Player
              +-----------------+
```

> Note: If need to deliver RTMP or HTTP-FLV, pelease use [OriginCluster](./sample-origin-cluster.md).

Start the first Origin Server, listen at `1935` and `8080`, covert RTMP to HLS:

```bash
./objs/srs -c conf/origin.hls.only1.conf
```

Start the second Origin Server, listen at `1935` and `8080`, covert RTMP to HLS:

```bash
./objs/srs -c conf/origin.hls.only2.conf
```

Publish stream to origin, system will select a random Origin Server:

```bash
./objs/ffmpeg/bin/ffmpeg -re -i ./doc/source.flv -c copy -f flv rtmp://localhost/live/livestream1
```

Publish another stream to origin, system will select a random Origin Server:

```bash
./objs/ffmpeg/bin/ffmpeg -re -i ./doc/source.flv -c copy -f flv rtmp://localhost/live/livestream2
```

> Note: It works only for HLS, please use [OriginCluster](./sample-origin-cluster.md) for RTMP or HTTP-FLV.

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/reuse-port)


