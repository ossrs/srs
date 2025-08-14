---
title: Build and Install
sidebar_label: Build and Install
hide_title: false
hide_table_of_contents: false
---

# Install

You can directly use the release binaries, or build SRS step by step. See: [Github: release](http://ossrs.net/srs.release/releases/) or [Mirror of China: release](http://www.ossrs.net/srs.release/releases/)

## OS

* <strong>Ubuntu20</strong> is recommended.
* Use [srs-docker](https://github.com/ossrs/dev-docker/tree/dev) to build SRS.
* Use [srs-docker](https://github.com/ossrs/dev-docker) to run SRS.

## Iptables and Selinux

Sometimes the stream play failed, but without any error message, or server cann't connect to. Please check the iptables and selinux.

Turn off <code>iptables</code>:

```bash
# disable the firewall
sudo /etc/init.d/iptables stop
sudo /sbin/chkconfig iptables off
```

Disable the <code>selinux</code>, to run `getenforce` to ensure the result is `Disabled`:

1. Edit the config of selinux: `sudo vi /etc/sysconfig/selinux`
1. Change the SELINUX to disabled: `SELINUX=disabled`
1. Rebot: `sudo init 6`

## Build and Run SRS

It's very easy to build SRS:

```
./configure && make
```

Also easy to start SRS:

```bash
./objs/srs -c conf/srs.conf
```

Publish RTMP, please read: [Usage: RTMP](./rtmp.md)

For service management, please read [Service](./service.md)

Run SRS in docker, please read [srs-docker](https://github.com/ossrs/dev-docker#usage)

## ARM

It's also ok to directly build on ARM server.

For ARM/MIPS or crossbuild, please read [here](./arm.md)

Winlin 2014.11

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/install)


