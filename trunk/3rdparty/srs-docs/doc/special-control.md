---
title: Special Control
sidebar_label: Special Control
hide_title: false
hide_table_of_contents: false
---

# SpecialControl

SRS provides a set of config to ingerate with other systems.

## Send Minimal Interval

```
vhost __defaultVhost__ {
    # for play client, both RTMP and other stream clients,
    # for instance, the HTTP FLV stream clients.
    play {
        # the minimal packets send interval in ms,
        # used to control the ndiff of stream by srs_rtmp_dump,
        # for example, some device can only accept some stream which
        # delivery packets in constant interval(not cbr).
        # @remark 0 to disable the minimal interval.
        # @remark >0 to make the srs to send message one by one.
        # @remark user can get the right packets interval in ms by srs_rtmp_dump.
        # default: 0
        send_min_interval       10.0;
    }
}
```

## Reduce Sequence Header

```
vhost __defaultVhost__ {
    # for play client, both RTMP and other stream clients,
    # for instance, the HTTP FLV stream clients.
    play {
        # whether reduce the sequence header,
        # for some client which cannot got duplicated sequence header,
        # while the sequence header is not changed yet.
        # default: off
        reduce_sequence_header  on;
    }
}
```

## Publish 1st Packet Timeout

```
vhost __defaultVhost__ {
    # the config for FMLE/Flash publisher, which push RTMP to SRS.
    publish {
        # the 1st packet timeout in ms for encoder.
        # default: 20000
        firstpkt_timeout    20000;
    }
}
```

## Publish Normal Timeout

```
vhost __defaultVhost__ {
    # the config for FMLE/Flash publisher, which push RTMP to SRS.
    publish {
        # the normal packet timeout in ms for encoder.
        # default: 5000
        normal_timeout      7000;
    }
}
```

## Debug SRS Upnode

```
vhost __defaultVhost__ {
    # when upnode(forward to, edge push to, edge pull from) is srs,
    # it's strongly recommend to open the debug_srs_upnode,
    # when connect to upnode, it will take the debug info, 
    # for example, the id, source id, pid.
    # please see https://ossrs.io/lts/en-us/docs/v4/doc/log
    # default: on
    debug_srs_upnode    on;
}
```

## UTC Time

```
# whether use utc_time to generate the time struct,
# if off, use localtime() to generate it,
# if on, use gmtime() instead, which use UTC time.
# default: off
utc_time            off;
```

## HLS TS Floor

```
vhost __defaultVhost__ {
    hls {
        # whether use floor for the hls_ts_file path generation.
        # if on, use floor(timestamp/hls_fragment) as the variable [timestamp],
        #       and use enahanced algorithm to calc deviation for segment.
        # @remark when floor on, recommend the hls_segment>=2*gop.
        # default: off
        hls_ts_floor    off;
    }
}
```

## HLS Wait Keyframe

```
vhost __defaultVhost__ {
    hls {
        # whether wait keyframe to reap segment,
        # if off, reap segment when duration exceed the fragment,
        # if on, reap segment when duration exceed and got keyframe.
        # default: on
        hls_wait_keyframe       on;
    }
}
```

## HttpHooks On HLS Notify

```
vhost __defaultVhost__ {
    http_hooks {
        # when srs reap a ts file of hls, call this hook,
        # used to push file to cdn network, by get the ts file from cdn network.
        # so we use HTTP GET and use the variable following:
        #       [app], replace with the app.
        #       [stream], replace with the stream.
        #       [ts_url], replace with the ts url.
        # ignore any return data of server.
        # @remark random select a url to report, not report all.
        on_hls_notify   http://127.0.0.1:8085/api/v1/hls/[app]/[stream][ts_url];
    }
}
```

## TCP NoDelay

```
vhost __defaultVhost__ {
    # whether enable the TCP_NODELAY
    # if on, set the nodelay of fd by setsockopt
    # default: off
    tcp_nodelay     on;
}
```

## ATC Auto

```
vhost __defaultVhost__ {
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

Winlin 2015.8

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/special-control)


