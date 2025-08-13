---
title: RTMP ATC
sidebar_label: RTMP ATC
hide_title: false
hide_table_of_contents: false
---

# ATC Deploy

How to deploy RTMP fault backup? When origin for edge restart, edge will 
switch to another origin, so it is easy to config fault tolerance for edge,
only need to specifies multiple origin servers.

How to deploy HLS fault backup? When edge can not got a piece of ts, it
will fetch from another origin server, so the ts in these two server must
be absolutely equals. We must use atc for HLS/HDS which over http file stream.

For the deploy of HDS/HLS, read [Adobe: HDS/HLS fault backup](http://www.adobe.com/cn/devnet/adobe-media-server/articles/varnish-sample-for-failover.html):

```bash
                        +----------+        +----------+
               +--ATC->-+  server  +--ATC->-+ packager +-+   +---------+
+----------+   | RTMP   +----------+ RTMP   +----------+ |   | Reverse |    +-------+
| encoder  +->-+                                         +->-+  Proxy  +-->-+  CDN  +
+----------+   |        +----------+        +----------+ |   | (nginx) |    +-------+
               +--ATC->-+  server  +--ATC->-+ packager +-+   +---------+
                 RTMP   +----------+ RTMP   +----------+
```

The RTMP is in ATC, the absolute time, so server or other tools can output
HLS in multiple tools.

## Config ATC on SRS

ATC of SRS is default off, the RTMP timestamp to client always start at 0.

```bash
vhost __defaultVhost__ {
    # for play client, both RTMP and other stream clients,
    # for instance, the HTTP FLV stream clients.
    play {
        # vhost for atc for hls/hds/rtmp backup.
        # generally, atc default to off, server delivery rtmp stream to client(flash) timestamp from 0.
        # when atc is on, server delivery rtmp stream by absolute time.
        # atc is used, for instance, encoder will copy stream to master and slave server,
        # server use atc to delivery stream to edge/client, where stream time from master/slave server
        # is always the same, client/tools can slice RTMP stream to HLS according to the same time,
        # if the time not the same, the HLS stream cannot slice to support system backup.
        #
        # @see http://www.adobe.com/cn/devnet/adobe-media-server/articles/varnish-sample-for-failover.html
        # @see http://www.baidu.com/#wd=hds%20hls%20atc
        #
        # default: off
        atc             off;
    }
}
```

## ATC for Adobe Flash Player

When ATC is on, flash will start play ok when:
* sequence header: The timstamp of sequence header must equals to the first packet.
* metadata: The timstamp of metadata must equals to the first packet.

We test the flash player, it ok to play the RTMP stream with or without ATC.

## ATC for encoder

The encoder can control the atc of SRS, when encoder write a field 
"bravo_atc":"true".

We can disable this feature:

```bash
vhost atc.srs.com {
    # for play client, both RTMP and other stream clients,
    # for instance, the HTTP FLV stream clients.
    play {
        # whether enable the auto atc,
        # if enabled, detect the bravo_atc="true" in onMetaData packet,
        # set atc to on if matched.
        # always ignore the onMetaData if atc_auto is off.
        # default: off
        atc_auto        off;
    }
}
```

Winlin 2014.11

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/rtmp-atc)


