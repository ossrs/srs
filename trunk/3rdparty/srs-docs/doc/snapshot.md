---
title: Snapshot
sidebar_label: Snapshot 
hide_title: false
hide_table_of_contents: false
---

# Snapshot

SRS provides workaround for snapshots:

1. HttpCallback: Use http callbacks to handle `on_publish` event to snapshot by FFMPEG, and to stop FFMPEG when got `on_unpublish` event.
1. Transcoder: Use transcoder to snapshot.

## HttpCallback

This section describes how to use http callbacks to snapshot.

First, start the sample api server:
```
cd research/api-server && go run server.go 8085
```

Second, write the config for SRS to snapshot:
```
# snapshot.conf
listen              1935;
max_connections     1000;
daemon              off;
srs_log_tank        console;
vhost __defaultVhost__ {
    http_hooks {
        enabled on;
        on_publish http://127.0.0.1:8085/api/v1/snapshots;
        on_unpublish http://127.0.0.1:8085/api/v1/snapshots;
    }
    ingest {
        enabled on;
        input {
            type file;
            url ./doc/source.flv;
        }
        ffmpeg ./objs/ffmpeg/bin/ffmpeg;
        engine {
            enabled off;
            output rtmp://127.0.0.1:[port]/live?vhost=[vhost]/livestream;
        }
    }
}
```

Thrird, start SRS and the ingest will publish RTMP stream, which will trigger the `on_publish` event, then api will snapshot:
```
./objs/srs -c snapshot.conf
```

The snapshot generate thumbnails to directory:
```
winlin:srs winlin$ ls -lh research/api-server/static-dir/live/*.png
-rw-r--r--  1 winlin  staff    73K Oct 20 13:35 livestream-001.png
-rw-r--r--  1 winlin  staff    91K Oct 20 13:35 livestream-002.png
-rw-r--r--  1 winlin  staff    11K Oct 20 13:35 livestream-003.png
-rw-r--r--  1 winlin  staff   167K Oct 20 13:35 livestream-004.png
-rw-r--r--  1 winlin  staff   172K Oct 20 13:35 livestream-005.png
-rw-r--r--  1 winlin  staff   264K Oct 20 13:35 livestream-006.png
lrwxr-xr-x  1 winlin  staff   105B Oct 20 13:35 livestream-best.png -> livestream-006.png
```

The thumbnail `live-livestream-best.png` will link to the big one to avoid blank image.

User can access it by http server: [http://localhost:8085/live/livestream-best.png](http://localhost:8085/live/livestream-best.png)

## Transcoder

The transcoder can used for snapshot:

```
listen              1935;
max_connections     1000;
daemon              off;
srs_log_tank        console;
vhost __defaultVhost__ {
    transcode {
        enabled on;
        ffmpeg ./objs/ffmpeg/bin/ffmpeg;
        engine snapshot {
            enabled on;
            iformat flv;
            vfilter {
                vf fps=1;
            }
            vcodec png;
            vparams {
                vframes 6;
            }
            acodec an;
            oformat image2;
            output ./objs/nginx/html/[app]/[stream]-%03d.png;
        }
    }
    ingest {
        enabled on;
        input {
            type file;
            url ./doc/source.flv;
        }
        ffmpeg ./objs/ffmpeg/bin/ffmpeg;
        engine {
            enabled off;
            output rtmp://127.0.0.1:[port]/live?vhost=[vhost]/livestream;
        }
    }
}
```

The thumbnails:
```
winlin:srs winlin$ ls -lh objs/nginx/html/live/*.png
-rw-r--r--  1 winlin  staff   265K Oct 20 14:52 livestream-001.png
-rw-r--r--  1 winlin  staff   265K Oct 20 14:52 livestream-002.png
-rw-r--r--  1 winlin  staff   287K Oct 20 14:52 livestream-003.png
-rw-r--r--  1 winlin  staff   235K Oct 20 14:52 livestream-004.png
-rw-r--r--  1 winlin  staff   315K Oct 20 14:52 livestream-005.png
-rw-r--r--  1 winlin  staff   405K Oct 20 14:52 livestream-006.png
```

Note: SRS never choose the best thumbnail.

Winlin 2015.10

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/snapshot)


