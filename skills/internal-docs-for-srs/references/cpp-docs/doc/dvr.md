---
title: DVR
sidebar_label: DVR 
hide_title: false
hide_table_of_contents: false
---

# DVR

SRS supports DVR RTMP stream to FLV/MP4 file. Although the bellow using FLV as example, but MP4 is also available.

When FFmpeg/OBS publish RTMP stream to SRS, SRS will write the stream to FLV/MP4 file. The workflow is:

```text
+------------+            +-------+           +---------------+
+ FFmpeg/OBS +---RTMP-->--+  SRS  +---DVR-->--+ FLV/MP4 File  +
+------------+            +-------+           +---------------+
```

Many users want more features about DVR, please consider use [Oryx](./getting-started-oryx.md#dvr) instead, 
for example:

* Oryx supports S3 cloud storage, move the final MP4 file to S3 cloud storage.
* Oryx supports glob filters, to only record specified streams, not all streams.
* Oryx supports merge multiple publishing sessions to one MP4 file.

In facts, DVR feature can be very complicated, SRS only support basic DVR feature, while Oryx will continue 
to improve the DVR features.

## Build

DVR is always enabled for SRS3+.

For information about the dvr option, read [Build](./install.md)

## Config

The difficult of DVR is about the flv name, while SRS use app/stream+random name.
User can use http-callback to rename, for example, when DVR reap flv file.

Config for DVR:

```bash
vhost yourvhost {
    # DVR RTMP stream to file,
    # start to record to file when encoder publish,
    # reap flv/mp4 according by specified dvr_plan.
    dvr {
        # whether enabled dvr features
        # default: off
        enabled         on;
        # the filter for dvr to apply to.
        #       all, dvr all streams of all apps.
        #       <app>/<stream>, apply to specified stream of app.
        # for example, to dvr the following two streams:
        #       live/stream1 live/stream2
        # default: all
        dvr_apply       all;
        # the dvr plan. canbe:
        #       session reap flv/mp4 when session end(unpublish).
        #       segment reap flv/mp4 when flv duration exceed the specified dvr_duration.
        # @remark The plan append is removed in SRS3+, for it's no use.
        # default: session
        dvr_plan        session;
        # the dvr output path, *.flv or *.mp4.
        # we supports some variables to generate the filename.
        #       [vhost], the vhost of stream.
        #       [app], the app of stream.
        #       [stream], the stream name of stream.
        #       [2006], replace this const to current year.
        #       [01], replace this const to current month.
        #       [02], replace this const to current date.
        #       [15], replace this const to current hour.
        #       [04], replace this const to current minute.
        #       [05], replace this const to current second.
        #       [999], replace this const to current millisecond.
        #       [timestamp],replace this const to current UNIX timestamp in ms.
        # @remark we use golang time format "2006-01-02 15:04:05.999" as "[2006]-[01]-[02]_[15].[04].[05]_[999]"
        # for example, for url rtmp://ossrs.net/live/livestream and time 2015-01-03 10:57:30.776
        # 1. No variables, the rule of SRS1.0(auto add [stream].[timestamp].flv as filename):
        #       dvr_path ./objs/nginx/html;
        #       =>
        #       dvr_path ./objs/nginx/html/live/livestream.1420254068776.flv;
        # 2. Use stream and date as dir name, time as filename:
        #       dvr_path /data/[vhost]/[app]/[stream]/[2006]/[01]/[02]/[15].[04].[05].[999].flv;
        #       =>
        #       dvr_path /data/ossrs.net/live/livestream/2015/01/03/10.57.30.776.flv;
        # 3. Use stream and year/month as dir name, date and time as filename:
        #       dvr_path /data/[vhost]/[app]/[stream]/[2006]/[01]/[02]-[15].[04].[05].[999].flv;
        #       =>
        #       dvr_path /data/ossrs.net/live/livestream/2015/01/03-10.57.30.776.flv;
        # 4. Use vhost/app and year/month as dir name, stream/date/time as filename:
        #       dvr_path /data/[vhost]/[app]/[2006]/[01]/[stream]-[02]-[15].[04].[05].[999].flv;
        #       =>
        #       dvr_path /data/ossrs.net/live/2015/01/livestream-03-10.57.30.776.flv;
        # 5. DVR to mp4:
        #       dvr_path ./objs/nginx/html/[app]/[stream].[timestamp].mp4;
        #       =>
        #       dvr_path ./objs/nginx/html/live/livestream.1420254068776.mp4;
        # @see https://ossrs.io/lts/en-us/docs/v4/doc/dvr#custom-path
        # @see https://ossrs.io/lts/en-us/docs/v4/doc/dvr#custom-path
        #       segment,session apply it.
        # default: ./objs/nginx/html/[app]/[stream].[timestamp].flv
        dvr_path        ./objs/nginx/html/[app]/[stream].[timestamp].flv;
        # the duration for dvr file, reap if exceed, in seconds.
        #       segment apply it.
        #       session,append ignore.
        # default: 30
        dvr_duration    30;
        # whether wait keyframe to reap segment,
        # if off, reap segment when duration exceed the dvr_duration,
        # if on, reap segment when duration exceed and got keyframe.
        #       segment apply it.
        #       session,append ignore.
        # default: on
        dvr_wait_keyframe       on;
        # about the stream monotonically increasing:
        #   1. video timestamp is monotonically increasing,
        #   2. audio timestamp is monotonically increasing,
        #   3. video and audio timestamp is interleaved monotonically increasing.
        # it's specified by RTMP specification, @see 3. Byte Order, Alignment, and Time Format
        # however, some encoder cannot provides this feature, please set this to off to ignore time jitter.
        # the time jitter algorithm:
        #   1. full, to ensure stream start at zero, and ensure stream monotonically increasing.
        #   2. zero, only ensure stream start at zero, ignore timestamp jitter.
        #   3. off, disable the time jitter algorithm, like atc.
        # apply for all dvr plan.
        # default: full
        time_jitter             full;

        # on_dvr, never config in here, should config in http_hooks.
        # for the dvr http callback, @see http_hooks.on_dvr of vhost hooks.callback.srs.com
        # @see https://ossrs.io/lts/en-us/docs/v4/doc/dvr#http-callback
        # @see https://ossrs.io/lts/en-us/docs/v4/doc/dvr#http-callback
    }
}
```

The plan of DVR used to reap flv file:

* session: When start publish, open flv file, close file when unpublish.
* segment: Reap flv file by the dvr_duration and dvr_wait_keyframe.
* time_jitter: The time jitter algorithm to use.
* dvr_path: The path of dvr, the rules is specified at below.

The config file can also use `conf/dvr.segment.conf` or `conf/dvr.session.conf`.

## Apply

The dvr apply is a filter which enable or disable the dvr of specified stream.
This feature is similar to nginx control module, but stronger than nginx.
User can use [http raw api](./http-api.md) to control when to dvr specified stream.
Please read [351](https://github.com/ossrs/srs/issues/459#issuecomment-134983742).

The following exmaple dvr `live/stream1`和`live/stream2`, the config:
```
vhost xxx {
    dvr {
        dvr_apply live/stream1 live/stream2;
    }
}
```

About the RAW API to control DVR, read [319](https://github.com/ossrs/srs/issues/319) and [wiki](./http-api.md#raw-dvr).

## Custom Path

We can custom the dvr path(dir and filename) by rules:

* Use date and time and stream info as dir name, to avoid too many files in a dir.
* Use date and time and stream info as filename, for better search.
* Provides the data/time and stream info variables, use brackets to identify them.
* Keep SRS1.0 rule, supports write to a specified dir and uses timestamp as filename. If no filename specified(dir specified only), use `[stream].[timestamp].flv` as filename to compatible with SRS1.0 rule.

About the data and time variable, refer to go time format string, for example, use an actual year 2006 instead YYYY, it's a good design:

```
2006-01-02 15:04:05.999
```

The variables of dvr:

1. Year, [2006], replace this const to current year.
1. Month, [01], replace this const to current month.
1. Date, [02], replace this const to current date.
1. Hour, [15], replace this const to current hour.
1. Minute, [04], repleace this const to current minute.
1. Second, [05], repleace this const to current second.
1. Millisecond, [999], repleace this const to current millisecond.
1. Timestamp, [timestamp],replace this const to current UNIX timestamp in ms.
1. Stream info, refer to transcode output, variables are [vhost], [app], [stream]

For example, for url `rtmp://ossrs.net/live/livestream` and time `2015-01-03 10:57:30.776`:

1. No variables, the rule of SRS1.0(auto add `[stream].[timestamp].flv` as filename):
    * dvr_path ./objs/nginx/html;
    * =>
    * dvr_path ./objs/nginx/html/live/livestream.1420254068776.flv;

1. Use stream and date as dir name, time as filename:
    * dvr_path /data/[vhost]/[app]/[stream]/[2006]/[01]/[02]/[15].[04].[05].[999].flv;
    * =>
    * dvr_path /data/ossrs.net/live/livestream/2015/01/03/10.57.30.776.flv;

1. Use stream and year/month as dir name, date and time as filename:
    * dvr_path /data/[vhost]/[app]/[stream]/[2006]/[01]/[02]-[15].[04].[05].[999].flv;
    * =>
    * dvr_path /data/ossrs.net/live/livestream/2015/01/03-10.57.30.776.flv;

1. Use vhost/app and year/month as dir name, stream/date/time as filename:
    * dvr_path /data/[vhost]/[app]/[2006]/[01]/[stream]-[02]-[15].[04].[05].[999].flv;
    * =>
    * dvr_path /data/ossrs.net/live/2015/01/livestream-03-10.57.30.776.flv;

1. Use app as dirname, stream and timestamp as filename(the SRS1.0 rule):
    * dvr_path /data/[app]/[stream].[timestamp].flv;
    * =>
    * dvr_path /data/live/livestream.1420254068776.flv;

## Http Callback

Enable the `on_dvr` of `http_hooks`:

```
vhost your_vhost {
    dvr {
        enabled             on;
        dvr_path            ./objs/nginx/html/[app]/[stream]/[2006]/[01]/[02]/[15].[04].[05].[999].flv;
        dvr_plan            segment;
        dvr_duration        30;
        dvr_wait_keyframe   on;
    }
    http_hooks {
        enabled         on;
        on_dvr          http://127.0.0.1:8085/api/v1/dvrs;
    }
}
```

The log of api-server for api dvrs：

```
[2015-01-03 15:25:48][trace] post to dvrs, req={"action":"on_dvr","client_id":108,"ip":"127.0.0.1","vhost":"__defaultVhost__","app":"live","stream":"livestream","cwd":"/home/winlin/git/srs/trunk","file":"./objs/nginx/html/live/livestream/2015/1/3/15.25.18.442.flv"}
[2015-01-03 15:25:48][trace] srs on_dvr: client id=108, ip=127.0.0.1, vhost=__defaultVhost__, app=live, stream=livestream, cwd=/home/winlin/git/srs/trunk, file=./objs/nginx/html/live/livestream/2015/1/3/15.25.18.442.flv
127.0.0.1 - - [03/Jan/2015:15:25:48] "POST /api/v1/dvrs HTTP/1.1" 200 1 "" "SRS(Simple RTMP Server)2.0.88"
```

For more information, read about [HttpCallback](./http-callback.md)

## Bug

The bugs of dvr:

* The dir and filename rules: [#179](https://github.com/ossrs/srs/issues/179)
* The http callback for dvr: [#274](https://github.com/ossrs/srs/issues/274)
* The MP4 format support: [#738](https://github.com/ossrs/srs/issues/738)
* How to DVR multiple segments to a file?  Read [#776](https://github.com/ossrs/srs/pull/776).

## Reload

The changing of dvr and reload will restart the dvr, that is, to close current dvr file then apply new config.

Winlin 2015.1

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/dvr)


