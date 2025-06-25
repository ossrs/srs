---
title: Linux Service
sidebar_label: Linux Service
hide_title: false
hide_table_of_contents: false
---

# SRS Linux Service

There are many ways to startup SRS:
* Directly run srs at the trunk/objs, and need start again when system restart.
* Linux service, the init.d scirpt at `srs/trunk/etc/init.d/srs`, and user can add to linux service when linked to the /etc/init.d/srs then add as service `/sbin/chkconfig --add srs`.

The SRS release binary can be downloaded from release site, we can install as system service, see: [Github: release](http://ossrs.net/srs.release) or [Mirror for China: release](http://www.ossrs.net)

## Manual

We donot need to add to linux service to directly start SRS:

```bash
cd srs/trunk &&
./etc/init.d/srs start
```

or

```bash
cd srs/trunk &&
./objs/srs -c conf/srs.conf
```

## init.d

Install and startup SRS as linux system service:
* Build SRS: the install script will modify the INSTALL ROOT of init.d script.
* Link to init.d: link the `trunk/etc/init.d/srs` to `/etc/init.d/srs`
* Add to linux service: use /sbin/chkconfig for Centos.

<strong>Step1:</strong> Build and Install SRS

Intall SRS when build ok:

```bash
make && sudo make install
```

the install of make will install srs to the prefix dir, default to `/usr/local/srs`, which is specified by configure, for instance, ```./configure --prefix=`pwd`/_release``` set the install dir to _release of current dir to use `make install` without sudo.

<strong>Step2:</strong> Link script to init.d:

```bash
sudo ln -sf \
    /usr/local/srs/etc/init.d/srs \
    /etc/init.d/srs
```

<strong>Step3:</strong>Add as linux service:

```bash
#centos 6
sudo /sbin/chkconfig --add srs
```

or

```bash
#ubuntu12
sudo update-rc.d srs defaults
```

Use init.d script

Get the status of SRS:

```bash
/etc/init.d/srs status
```

Start SRS：

```bash
/etc/init.d/srs start
```

Stop SRS：

```bash
/etc/init.d/srs stop
```

Restart SRS：

```bash
/etc/init.d/srs restart
```

Reload SRS：

```bash
/etc/init.d/srs reload
```

For logrotate(`SIGUSR1`):

```bash
/etc/init.d/srs rotate
```

For Gracefully Quit(`SIGQUIT`):

```bash
/etc/init.d/srs grace
```

## systemctl

Ubuntu20 use systemctl to manage services, we also need to install init.d service, then add to systemctl:

```
./configure && make && sudo make install &&
sudo ln -sf /usr/local/srs/etc/init.d/srs /etc/init.d/srs &&
sudo cp -f /usr/local/srs/usr/lib/systemd/system/srs.service /usr/lib/systemd/system/srs.service &&
sudo systemctl daemon-reload && sudo systemctl enable srs
```

> Remark: We MUST copy the srs.service, or we couldn't enable the service by systemctl.

Use systemctl to start SRS:

```
sudo systemctl start srs
```

## Gracefully Upgrade

Gracefully Upgrade allows upgrade with zero downtime, it can be done by:

* New SRS and old SRS should be able to listen at the same ports. They provide services in the same ports simultaneously.
* The old SRS then closes listeners, and quit util all connections closed, this is Gracefully Quit.

> Note: About more informations, please see [#1579](https://github.com/ossrs/srs/issues/1579#issuecomment-587233844).

SRS3 supports Gracefully Quit:

* Use signal `SIGQUIT`, or command `/etc/init.d/srs grace`
* A new config `grace_start_wait` to wait for a while then start gracefully quit, default 2.3s
* A new config `grace_final_wait` allows wait for a few minutes finally, default 3.2s
* A new config `force_grace_quit` to force gracefully quit, see [#1579](https://github.com/ossrs/srs/issues/1579#issuecomment-587475077).

```bash
# For gracefully quit, wait for a while then close listeners,
# because K8S notify SRS with SIGQUIT and update Service simultaneously,
# maybe there is some new connections incoming before Service updated.
# @see https://github.com/ossrs/srs/issues/1595#issuecomment-587516567
# default: 2300
grace_start_wait 2300;
# For gracefully quit, final wait for cleanup in milliseconds.
# @see https://github.com/ossrs/srs/issues/1579#issuecomment-587414898
# default: 3200
grace_final_wait 3200;
# Whether force gracefully quit, never fast quit.
# By default, SIGTERM which means fast quit, is sent by K8S, so we need to
# force SRS to treat SIGTERM as gracefully quit for gray release or canary.
# @see https://github.com/ossrs/srs/issues/1579#issuecomment-587475077
# default: off
force_grace_quit off;
```

> Note: There is a example for Gracefully Quit, see [#1579](https://github.com/ossrs/srs/issues/1579#issuecomment-587414898)

Winlin 2019.10

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/service)


