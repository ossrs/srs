---
title: Reload
sidebar_label: Reload
hide_title: false
hide_table_of_contents: false
---

# Reload

Almost all features of SRS support reload, donot disconnect 
all connection and apply the new config.

## NotSupportedFeatures

The bellow features can not reload:
* deamon: whether start as deamon mode.
* mode: the mode of vhost.

The daemon never support reload.

The mode of vhost, to make the vhost origin or edge, should never directly 
change the mode, because of:

* The origin and edge switch is too complex.
* The origin always put in a device group, never change to edge actually.
* The upnode or origin restart have no effect to user, edge will retry.

A workaround to modify the mode of vhost:
* Delete the vhost and reload.
* Ensure the vhost is deleted, for the reload is async.
* Add vhost with new mode, then reload.

## Use Scenario

The use scenario of reload:
* Donot restart server to apply new config, only `killall -1 srs`.
* Donot disconnect user connections.

## Usage

The usage of reload: `killall -1 srs`

Or send signal to process: `kill -1 7635`

Or use SRS scripts: `/etc/init.d/srs reload`

Winlin 2014.11

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/reload)


