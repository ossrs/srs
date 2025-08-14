---
title: Nginx RTMP EXEC
sidebar_label: Nginx RTMP EXEC
hide_title: false
hide_table_of_contents: false
---

# Exec

## NGINX RTMP EXEC

SRS only support some exec introduced by NGINX RTMP:

1. exec/exec_publish: Support.
1. exec_pull: Not support.
1. exec_play: Not support.
1. exec_record_done: Not support.

> Note: You could use [HTTP Callback](./http-callback.md) to start FFmpeg on your backend server. It's much better solution.

## Config

The config for SRS EXEC list bellow, you can refer to `conf/exec.conf`.

```
vhost __defaultVhost__ {
    # the exec used to fork process when got some event.
    exec {
        # whether enable the exec.
        # default: off.
        enabled     off;
        # when publish stream, exec the process with variables:
        #       [vhost] the input stream vhost.
        #       [port] the intput stream port.
        #       [app] the input stream app.
        #       [stream] the input stream name.
        #       [engine] the tanscode engine name.
        # other variables for exec only:
        #       [url] the rtmp url which trigger the publish.
        #       [tcUrl] the client request tcUrl.
        #       [swfUrl] the client request swfUrl.
        #       [pageUrl] the client request pageUrl.
        # @remark empty to ignore this exec.
        publish     ./objs/ffmpeg/bin/ffmpeg -f flv -i [url] -c copy -y ./[stream].flv;
    }
}
```

Winlin 2015.8

[ne]: https://github.com/arut/nginx-rtmp-module/wiki/Directives#exec

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/nginx-exec)


