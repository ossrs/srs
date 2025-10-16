---
title: HLS
sidebar_label: HLS
hide_title: false
hide_table_of_contents: false
---

# HLS

HLS is the best streaming protocol for adaptability and compatibility. Almost all devices in the world support HLS, 
including PCs, Android, iOS, OTT, SmartTV, and more. Various browsers also support HLS well, including Chrome, Safari, 
Firefox, Edge, and mobile browsers.

If your users are diverse, especially if their devices have lower performance, HLS is the best choice. If you want 
to be compatible with more devices, HLS is the best choice. If you want to distribute your live stream on any CDN 
and globally, HLS is the best choice.

Of course, HLS is not perfect; its main issue is high latency, usually around 30 seconds. Although it can be optimized 
to about 8 seconds, different players' behavior may vary. Compared to other streaming protocols, the optimized latency 
is still high. So if you care about live streaming latency, please use RTMP or HTTP-FLV protocols.

The main application scenarios of HLS include:
* Cross-platform: The main live streaming solution for PCs is HLS, which can be played using the hls.js library. So if you choose one protocol for PC/Android/iOS, it's HLS.
* Strict stability requirements on iOS: HLS is the most stable on iOS, with stability comparable to RTMP and HTTP-FLV.
* Friendly CDN distribution: HLS is based on HTTP, so CDN integration and distribution are more complete than RTMP. HLS can switch between various CDNs.
* Fewer simple issues: HLS is a very simple streaming protocol, well supported by Apple. Android's support for HLS will also improve.

HLS is the core protocol of SRS and will be continuously maintained and updated, constantly improving support for HLS. 
SRS converts RTMP, SRT, or WebRTC streams into HLS streams, especially WebRTC, where SRS implements audio transcoding 
capabilities.

## Usage

SRS has built-in HLS support, which you can use with [docker](./getting-started.md) or [compile from source](./getting-started-build.md):

```bash
docker run --rm -it -p 1935:1935 -p 8080:8080 ossrs/srs:5 \
  ./objs/srs -c conf/hls.conf
```

Use [FFmpeg(click to download)](https://ffmpeg.org/download.html) or [OBS(click to download)](https://obsproject.com/download) to stream:

```bash
ffmpeg -re -i ./doc/source.flv -c copy -f flv rtmp://localhost/live/livestream
```

Open the following page to play the stream (if SRS is not on your local machine, replace localhost with the server IP):

* HLS by SRS player: [http://localhost:8080/live/livestream.m3u8](http://localhost:8080/players/srs_player.html?stream=livestream.m3u8)

> Note: Please wait about 10 seconds before playing the stream, otherwise it will fail, as it takes some time to generate the first segment.

## Config

The config for HLS:

```bash
vhost __defaultVhost__ {
    hls {        # whether the hls is enabled.
        # if off, do not write hls(ts and m3u8) when publish.
        # Overwrite by env SRS_VHOST_HLS_ENABLED for all vhosts.
        # default: off
        enabled on;

        # whether to use fmp4 as container
        # The default value is off, then HLS use ts as container format,
        # if on, HLS use fmp4 as container format.
        # Overwrite by env SRS_VHOST_HLS_HLS_USE_FMP4 for all vhosts.
        # default: off
        hls_use_fmp4 on;

        # the hls fragment in seconds, the duration of a piece of ts.
        # Overwrite by env SRS_VHOST_HLS_HLS_FRAGMENT for all vhosts.
        # default: 10
        hls_fragment 10;
        # the hls m3u8 target duration ratio,
        #   EXT-X-TARGETDURATION = hls_td_ratio * hls_fragment // init
        #   EXT-X-TARGETDURATION = max(ts_duration, EXT-X-TARGETDURATION) // for each ts
        # Overwrite by env SRS_VHOST_HLS_HLS_TD_RATIO for all vhosts.
        # default: 1.0
        hls_td_ratio 1.0;
        # the audio overflow ratio.
        # for pure audio, the duration to reap the segment.
        # for example, the hls_fragment is 10s, hls_aof_ratio is 1.2,
        # the segment will reap to 12s for pure audio.
        # Overwrite by env SRS_VHOST_HLS_HLS_AOF_RATIO for all vhosts.
        # default: 2.1
        hls_aof_ratio 2.1;
        # the hls window in seconds, the number of ts in m3u8.
        # Overwrite by env SRS_VHOST_HLS_HLS_WINDOW for all vhosts.
        # default: 60
        hls_window 60;
        # the error strategy. can be:
        #       ignore, disable the hls.
        #       disconnect, require encoder republish.
        #       continue, ignore failed try to continue output hls.
        # Overwrite by env SRS_VHOST_HLS_HLS_ON_ERROR for all vhosts.
        # default: continue
        hls_on_error continue;
        # the hls output path.
        # the m3u8 file is configured by hls_path/hls_m3u8_file, the default is:
        #       ./objs/nginx/html/[app]/[stream].m3u8
        # the ts file is configured by hls_path/hls_ts_file, the default is:
        #       ./objs/nginx/html/[app]/[stream]-[seq].ts
        # @remark the hls_path is compatible with srs v1 config.
        # Overwrite by env SRS_VHOST_HLS_HLS_PATH for all vhosts.
        # default: ./objs/nginx/html
        hls_path ./objs/nginx/html;
        # the hls m3u8 file name.
        # we supports some variables to generate the filename.
        #       [vhost], the vhost of stream.
        #       [app], the app of stream.
        #       [stream], the stream name of stream.
        # Overwrite by env SRS_VHOST_HLS_HLS_M3U8_FILE for all vhosts.
        # default: [app]/[stream].m3u8
        hls_m3u8_file [app]/[stream].m3u8;
        # the hls ts file name.
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
        #       [seq], the sequence number of ts.
        #       [duration], replace this const to current ts duration.
        # @see https://ossrs.io/lts/en-us/docs/v7/doc/dvr#custom-path
        # @see https://ossrs.io/lts/en-us/docs/v7/doc/hls#config
        # Overwrite by env SRS_VHOST_HLS_HLS_TS_FILE for all vhosts.
        # default: [app]/[stream]-[seq].ts
        hls_ts_file [app]/[stream]-[seq].ts;
        # the hls fmp4 file name.
        # we supports some variables to generate the filename.
        #       [vhost], the vhost of stream.
        #       [app], the app of stream.
        #       [stream], the stream name of stream.
        #       [2006], replace this const to current year.
        #       [01], replace this const to current month.
        #       [02], replace this const to current date.
        #       [15], replace this const to current hour.
        #       [04], replace this const to current minute.
        #       [05], replace this const to current second.p
        #       [999], replace this const to current millisecond.
        #       [timestamp],replace this const to current UNIX timestamp in ms.
        #       [seq], the sequence number of fmp4.
        #       [duration], replace this const to current ts duration.
        # @see https://ossrs.net/lts/zh-cn/docs/v4/doc/dvr#custom-path
        # @see https://ossrs.net/lts/zh-cn/docs/v4/doc/delivery-hls#hls-config
        # Overwrite by env SRS_VHOST_HLS_HLS_FMP4_FILE for all vhosts.
        # default: [app]/[stream]-[seq].m4s
        hls_fmp4_file [app]/[stream]-[seq].m4s;
        # the hls init mp4 file name.
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
        # @see https://ossrs.net/lts/zh-cn/docs/v4/doc/dvr#custom-path
        # @see https://ossrs.net/lts/zh-cn/docs/v4/doc/delivery-hls#hls-config
        # Overwrite by env SRS_VHOST_HLS_HLS_INIT_FILE for all vhosts.
        # default: [app]/[stream]/init.mp4
        hls_init_file [app]/[stream]/init.mp4;
        # the hls entry prefix, which is base url of ts url.
        # for example, the prefix is:
        #         http://your-server/
        # then, the ts path in m3u8 will be like:
        #         http://your-server/live/livestream-0.ts
        #         http://your-server/live/livestream-1.ts
        #         ...
        # Overwrite by env SRS_VHOST_HLS_HLS_ENTRY_PREFIX for all vhosts.
        # optional, default to empty string.
        hls_entry_prefix http://your-server;
        # the default audio codec of hls.
        # when codec changed, write the PAT/PMT table, but maybe ok util next ts.
        # so user can set the default codec for mp3.
        # the available audio codec:
        #       aac, mp3, an
        # Overwrite by env SRS_VHOST_HLS_HLS_ACODEC for all vhosts.
        # default: aac
        hls_acodec aac;
        # the default video codec of hls.
        # when codec changed, write the PAT/PMT table, but maybe ok util next ts.
        # so user can set the default codec for pure audio(without video) to vn.
        # the available video codec:
        #       h264, vn
        # Overwrite by env SRS_VHOST_HLS_HLS_VCODEC for all vhosts.
        # default: h264
        hls_vcodec h264;
        # whether cleanup the old expired ts files.
        # Overwrite by env SRS_VHOST_HLS_HLS_CLEANUP for all vhosts.
        # default: on
        hls_cleanup on;
        # If there is no incoming packets, dispose HLS in this timeout in seconds,
        # which removes all HLS files including m3u8 and ts files.
        # @remark 0 to disable dispose for publisher.
        # @remark apply for publisher timeout only, while "etc/init.d/srs stop" always dispose hls.
        # Overwrite by env SRS_VHOST_HLS_HLS_DISPOSE for all vhosts.
        # default: 120
        hls_dispose 120;
        # whether wait keyframe to reap segment,
        # if off, reap segment when duration exceed the fragment,
        # if on, reap segment when duration exceed and got keyframe.
        # Overwrite by env SRS_VHOST_HLS_HLS_WAIT_KEYFRAME for all vhosts.
        # default: on
        hls_wait_keyframe on;
        # whether use floor for the hls_ts_file path generation.
        # if on, use floor(timestamp/hls_fragment) as the variable [timestamp],
        #       and use enhanced algorithm to calc deviation for segment.
        # @remark when floor on, recommend the hls_segment>=2*gop.
        # Overwrite by env SRS_VHOST_HLS_HLS_TS_FLOOR for all vhosts.
        # default: off
        hls_ts_floor off;
        # the max size to notify hls,
        # to read max bytes from ts of specified cdn network,
        # @remark only used when on_hls_notify is config.
        # Overwrite by env SRS_VHOST_HLS_HLS_NB_NOTIFY for all vhosts.
        # default: 64
        hls_nb_notify 64;

        # Whether enable hls_ctx for HLS streaming, for which we create a "fake" connection for HTTP API and callback.
        # For each HLS streaming session, we use a child m3u8 with a session identified by query "hls_ctx", it simply
        # work as the session id.
        # Once the HLS streaming session is created, we will cleanup it when timeout in 2*hls_window seconds. So it
        # takes a long time period to identify the timeout.
        # Now we got a HLS stremaing session, just like RTMP/WebRTC/HTTP-FLV streaming, we're able to stat the session
        # as a "fake" connection, do HTTP callback when start playing the HLS streaming. You're able to do querying and
        # authentication.
        # Note that it will make NGINX edge cache always missed, so never enable HLS streaming if use NGINX edges.
        # Overwrite by env SRS_VHOST_HLS_HLS_CTX for all vhosts.
        # Default: on
        hls_ctx on;
        # For HLS pseudo streaming, whether enable the session for each TS segment.
        # If enabled, SRS HTTP API will show the statistics about HLS streaming bandwidth, both m3u8 and ts file. Please
        # note that it also consumes resource, because each ts file should be served by SRS, all NGINX cache will be
        # missed because we add session id to each ts file.
        # Note that it will make NGINX edge cache always missed, so never enable HLS streaming if use NGINX edges.
        # Overwrite by env SRS_VHOST_HLS_HLS_TS_CTX for all vhosts.
        # Default: on
        hls_ts_ctx on;

        # whether using AES encryption.
        # Overwrite by env SRS_VHOST_HLS_HLS_KEYS for all vhosts.
        # default: off
        hls_keys on;
        # the number of clear ts which one key can encrypt.
        # Overwrite by env SRS_VHOST_HLS_HLS_FRAGMENTS_PER_KEY for all vhosts.
        # default: 5
        hls_fragments_per_key 5;
        # the hls key file name.
        # we supports some variables to generate the filename.
        #       [vhost], the vhost of stream.
        #       [app], the app of stream.
        #       [stream], the stream name of stream.
        #       [seq], the sequence number of key corresponding to the ts.
        # Overwrite by env SRS_VHOST_HLS_HLS_KEY_FILE for all vhosts.
        hls_key_file [app]/[stream]-[seq].key;
        # the key output path.
        # the key file is configed by hls_path/hls_key_file, the default is:
        # ./objs/nginx/html/[app]/[stream]-[seq].key
        # Overwrite by env SRS_VHOST_HLS_HLS_KEY_FILE_PATH for all vhosts.
        hls_key_file_path ./objs/nginx/html;
        # the key root URL, use this can support https.
        # @remark It's optional.
        # Overwrite by env SRS_VHOST_HLS_HLS_KEY_URL for all vhosts.
        hls_key_url https://localhost:8080;

        # Special control controls.
        ###########################################
        # Whether calculate the DTS of audio frame directly.
        # If on, guess the specific DTS by AAC samples, please read https://github.com/ossrs/srs/issues/547#issuecomment-294350544
        # If off, directly turn the FLV timestamp to DTS, which might cause corrupt audio stream.
        # @remark Recommend to set to off, unless your audio stream sample-rate and timestamp is not correct.
        # Overwrite by env SRS_VHOST_HLS_HLS_DTS_DIRECTLY for all vhosts.
        # Default: on
        hls_dts_directly on;

        # on_hls, never config in here, should config in http_hooks.
        # for the hls http callback, @see http_hooks.on_hls of vhost hooks.callback.srs.com
        # @see https://ossrs.io/lts/en-us/docs/v7/doc/hls#http-callback

        # on_hls_notify, never config in here, should config in http_hooks.
        # we support the variables to generate the notify url:
        #       [app], replace with the app.
        #       [stream], replace with the stream.
        #       [param], replace with the param.
        #       [ts_url], replace with the ts url.
        # for the hls http callback, @see http_hooks.on_hls_notify of vhost hooks.callback.srs.com
        # @see https://ossrs.io/lts/en-us/docs/v7/doc/hls#on-hls-notify
    }
}
```

> Note: These settings are only for playing HLS. For streaming settings, please follow your protocol, like referring to [RTMP](./rtmp.md#config), [SRT](./srt.md#config), or [WebRTC](./webrtc.md#config) streaming configurations.

Here are the main settings:
* enabled: Turn HLS on/off, default is off.
* hls_fragment: Seconds, specify the minimum length of ts slices. For the actual length of ts files, please refer to the detailed description of [HLS TS Duration](#hls-ts-duration).
* hls_td_ratio: Normal slice duration multiple. For the actual length of ts files, please refer to the detailed description of [HLS TS Duration](#hls-ts-duration).
* hls_wait_keyframe: Whether to slice by top, i.e., wait for the keyframe before slicing. For the actual length of ts files, please refer to the detailed description of [HLS TS Duration](#hls-ts-duration).
* hls_aof_ratio: Pure audio slice duration multiple. For pure audio, when the ts duration exceeds the configured ls_fragment multiplied by this factor, the file is cut. For the actual length of ts files, please refer to the detailed description of [HLS TS Duration](#hls-ts-duration).
* hls_window: Seconds, specify the HLS window size, i.e., the sum of ts file durations in the m3u8, which determines the number of ts files in the m3u8. For more details, refer to [HLS TS Files](#hls-ts-files).
* hls_path: The path where the HLS m3u8 and ts files are saved. Both m3u8 and ts files are saved in this directory.
* hls_m3u8_file: The file name of the HLS m3u8, including replaceable `[vhost]`, `[app]`, and `[stream]` variables.
* hls_ts_file: The file name of the HLS ts, including a series of replaceable variables. Refer to [dvr variables](./dvr.md#custom-path). Also, `[seq]` is the ts sequence number.
* hls_entry_prefix: The base url of TS. Optional, default is an empty string; when not empty, it is added in front of ts as the base url.
* hls_acodec: Default audio codec. When the stream codec changes, the PMT/PAT information will be updated; the default is aac, so the default PMT/PAT information is aac; if the stream is mp3, this parameter can be set to mp3 to avoid PMT/PAT changes.
* hls_vcodec: Default video codec. When the stream codec changes, the PMT/PAT information will be updated; the default is h264. If it is a pure audio HLS, it can be set to vn, which can reduce the time for SRS to detect pure audio and directly enter pure audio mode.
* hls_cleanup: Whether to delete expired ts slices that are not in the hls_window. You can turn off ts slice cleanup to implement time-shifting and storage, using your own slice management system.
* hls_dispose: When there is no stream, the HLS cleanup expiration time (seconds). When the system restarts or exceeds this time, all HLS files, including m3u8 and ts, will be cleaned up. If set to 0, no cleanup will be done.
* hls_nb_notify: The length of data read from the notify server.
* on_hls: When a slice is generated, callback this url using POST. Used to integrate with your own system, such as implementing slice movement.
* on_hls_notify: When a slice is generated, callback this url using GET. Used to integrate with the system, you can use the `[ts_url]` variable to implement pre-distribution (i.e., download a ts slice once).

## HLS TS Duration

How is the duration of HLS TS segments determined? It depends on the configuration and the characteristics of the stream.

If there is video, the segment duration is `max(hls_fragment*hls_td_ratio, gop_size*N)`, which is the maximum value of `hls_fragment` and `gop_size`. The `gop_size` is determined by the encoder, for example, OBS can set the GOP size in seconds, while FFmpeg uses the number of frames combined with the frame rate to calculate seconds.

For example, if the stream's frame rate is 25 and the GOP is 50 frames, then the `gop_size` is 2 seconds:

* If `hls_fragment` is 10 seconds, the final TS segment duration is 10 seconds.
* If `hls_fragment` is 5 seconds, the final TS segment duration is 6 seconds, with 3 GOPs.
* If `hls_fragment` is 5 seconds and `hls_td_ratio` is 2, the final TS segment duration is 10 seconds.

If `hls_wait_keyframe off` is configured, the GOP size is no longer considered, and the TS segment duration is determined by `hls_fragment` regardless of the GOP size. For example, if the GOP is 10 seconds:

* If `hls_fragment` is 10 seconds, the final TS segment duration is 10 seconds.
* If `hls_fragment` is 5 seconds, the final TS segment duration is 5 seconds.
* If `hls_fragment` is 3 seconds and `hls_td_ratio` is 2, the final TS segment duration is 6 seconds.

> Note: Turning off `hls_wait_keyframe` can reduce segment size and latency, but some players may experience screen artifacts when starting playback with a non-keyframe.

For audio-only HLS, the segment duration is determined by `hls_fragment*hls_aof_ratio`:

* If `hls_fragment` is 10 seconds and `hls_aof_ratio` is 1.2, the final TS segment duration is 12 seconds.
* If `hls_fragment` is 5 seconds and `hls_aof_ratio` is 1, the final TS segment duration is 5 seconds.

Note that if the segment duration is unusually long, exceeding a certain size (usually 3 times the maximum segment length), it will be discarded.

## HLS TS Files

The number of TS files in the m3u8 is determined by the TS duration and `hls_window`. When the total duration of TS files exceeds `hls_window`, the first segment in the m3u8 is discarded until the total TS duration is within the configured range.

SRS ensures the following formula:

```bash
hls_window >= sum(duration of each ts in m3u8)
```

For example, if `hls_window` is 60 seconds and `hls_fragment` is 10 seconds, and the actual TS segment duration is 10 seconds, there will be 6 TS files in the m3u8. The actual TS segment duration may be larger than `hls_fragment`, see [HLS TS Duration](#hls-ts-duration) for details.

## HTTP Callback

You can set up an `on_hls` callback in the `http_hooks` section, not in the HLS section.

Note: HLS hot backup can be implemented based on this callback, see [#351](https://github.com/ossrs/srs/issues/351).

Note: HLS hot backup must ensure that the slices on both servers are exactly the same, because the load balancer or edge may fetch slices from both servers. Ensuring that the slices on both servers are exactly the same is a very complex streaming media issue. However, through the callback and business system, you can achieve a simple and reliable HLS hot backup system by choosing slices from both servers.

## HLS Authentication

SRS supports HLS client playback and online user statistics. By default, it will enable `hls_ctx` and `hls_ts_ctx`. This way, HLS and other protocols can implement authentication playback and data statistics through callbacks. For example, when playing HLS, you can use the `on_play` callback to return an error and reject client playback.

```bash
vhost __defaultVhost__ {
    hls {
        enabled  on;
        hls_ctx on;
        hls_ts_ctx on;
    }
}
```

However, this feature will cause HLS cache to fail on CDN because each playback will have a different ctx_id, similar to a session ID function. Therefore, in [HLS Cluster](./nginx-for-hls.md), you must disable these two options.

## HLS Dispose

If the stream is stopped, the HLS client can still play the previous content because the slice files still exist.

Sometimes during a live broadcast, you may need to temporarily stop the stream, change encoding parameters or streaming devices, and then restart the stream. Therefore, SRS should not delete HLS files when stopping the stream.

By default, SRS will clean up the HLS slice files after the `hls_dispose` configured time. This time is set to 120 seconds (2 minutes) by default.

```bash
vhost __defaultVhost__ {
    hls {
        enabled  on;
        hls_dispose 120;
    }
}
```

If you need to clean up faster, you can shorten this cleanup time. However, this configuration should not be too short. It is recommended not to be less than `hls_window`, otherwise, it may cause early cleanup when restarting the stream, making the HLS stream inaccessible to the player.

## HLS in RAM

If you need to increase the number of concurrent HLS streams, you can try distributing HLS directly from memory without writing to disk.

You can mount memory as a disk directory and then write HLS slices to the memory disk:

```bash
mkdir -p /ramdisk &&
mount -o size=7G -t tmpfs none /ramdisk
```

> Note: To unmount the memory disk, use the command `unmount /randisk`.

> Note: If you don't have many streams and don't need much disk space, you can write HLS slices to the `/tmp` directory, which is a memory disk by default.

Then configure `hls_path` or create a soft link to the directory.

## HLS Delivery Cluster

To deploy an HLS distribution cluster and edge distribution cluster for your own CDN to handle a large number of viewers, please refer to [Nginx for HLS](./nginx-for-hls.md).

## HLS Low Latency

How to reduce HLS latency? The key is to reduce the number of slices and the number of TS files in the m3u8. SRS's default configuration is 10 seconds per slice and 60 seconds per m3u8, resulting in a latency of about 30 seconds. Some players start requesting slices from the middle position, so there will be a delay of 3 slices.

You can adjust the following three settings to reduce latency to about 6-8 seconds:

* Reduce the GOP size, e.g., set OBS's GOP to 1 second or FFmpeg's GOP to the number of FPS frames.
* Reduce the encoder's delay, for example, set OBS to `Profile` as `baseline` and choose `Tune` as `zerolatency`.
* Reduce `hls_fragment`, e.g., set it to 2 seconds or 1 second.
* Reduce `hls_window`, e.g., set it to 10 seconds or 5 seconds.
* Use low-latency players like hls.js, ijkplayer, or ffplay, and avoid high-latency players like VLC.

Refer to the configuration file `conf/hls.realtime.conf`:

```bash
vhost __defaultVhost__ {
    hls {
        enabled  on;
        hls_fragment 2;
        hls_window 10;
    }
}
```

> Note: If you can't adjust the encoder's OGP size, consider setting `hls_wait_keyframe off` to ignore GOP, but this may cause screen artifacts. Test your device's compatibility.

Of course, you can't reduce it too much, as it may cause insufficient buffering for the player or skipping when the player's network is poor, possibly resulting in playback failure. The lower the latency, the higher the chance of buffering. HLS latency cannot be less than 5 seconds, especially considering CDN and player compatibility.

Even after adjusting, the HLS delay won't be less than 5 seconds, and the LLHLS protocol can't reduce it further. This is because LLHLS only tries to solve the impact of the initial GOP during playback. In the above settings, we also reduced the GOP's impact through the encoder's configuration. However, network jitter and player strategy are reasons for the higher HLS delay, and they can't be solved.
If you need latency within 5 seconds, consider using protocols like [HTTP-FLV](./flv.md), [SRT](./srt.md), or [WebRTC](./webrtc.md).

## ON HLS Notify

You can configure `on_hls_notify` for CDN pre-distribution. This should be set in `http_hooks` rather than in the HLS configuration.

## HLS Audio Corrupt

HLS might have loud noise issues, which is caused by the sampling rate of AAC causing a small error when switching between FLV (tbn=1000) and TS (tbn=90000). SRS3 uses the number of samples to calculate the exact timestamp, for more details, refer to [HLS Loud Noise](https://github.com/ossrs/srs/issues/547#issuecomment-294350544).

> Note: To solve the HLS loud noise problem, you need to manually disable `hls_dts_directly` (set to off).

After SRS3 is corrected, it is found that some audio streams have problems with their timestamps, causing the timestamps calculated from the AAC sample count to be incorrect. Therefore, the configuration item `hls_dts_directly` is provided to force the use of the original timestamp, refer to [HLS Force Original Timestamp](https://github.com/ossrs/srs/issues/547#issuecomment-563942711).

## HLS Audio Only

SRS supports distributing HLS pure audio streams. When the RTMP stream has no video and the audio is AAC (you can use transcoding to convert to AAC, refer to [Usage: Transcode2HLS](./sample-transcode-to-hls.md)), SRS only slices the audio.

If the RTMP stream already has video and audio, and you need to support pure audio HLS streams, you can use transcoding to remove the video, refer to: [Transcoding: Disable Stream](./ffmpeg.md#%E7%A6%81%E7%94%A8). Then distribute the audio stream.

Distributing pure audio streams does not require special configuration, just like HLS distribution.

## HLS and Forward

Forward streams are not distinguished from ordinary streams. If the forward stream's VHOST is configured with HLS, the HLS configuration will be applied for slicing.

Therefore, you can transcode the original stream to ensure that the stream meets the h.264/aac standard, and then forward it to multiple VHOSTs configured with HLS for slicing. This supports hot backup for multiple source stations.

## HLS and Transcode

HLS requires the RTMP stream encoding to be h.264+aac/mp3, otherwise, HLS will be automatically disabled, and you may see RTMP streams but not HLS streams (or the HLS streams you see are from previous streams).

Transcoding the RTMP stream allows SRS to access any encoded RTMP stream and then convert it to the h.264/aac/mp3 encoding required by HLS.

When configuring Transcode, if you need to control the ts length, you need to [configure the ffmpeg encoding gop](http://ffmpeg.org/ffmpeg-codecs.html#Options-7), for example:
```bash
vhost hls.transcode.vhost.com {
    transcode {
        enabled     on;
        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;
        engine hls {
            enabled         on;
            vfilter {
            }
            vcodec          libx264;
            vbitrate        500;
            vfps            20;
            vwidth          768;
            vheight         320;
            vthreads        2;
            vprofile        baseline;
            vpreset         superfast;
            vparams {
                g           100;
            }
            acodec          libaacplus;
            abitrate        45;
            asample_rate    44100;
            achannels       2;
            aparams {
            }
            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];
        }
    }
}
```
This FFMPEG transcoding parameter specifies the gop duration as 100/20=5 seconds, fps frame rate (vfps=20), and gop frame count (g=100).

## HLS Multiple Bitrate

SRS currently does not support HLS adaptive bitrate, as it generally requires transcoding a single stream into multiple streams and requires GOP alignment. You can use FFmpeg to achieve this, refer to [How to generate multiple resolutions HLS using FFmpeg for live streaming](https://stackoverflow.com/a/71985380/17679565).

## Apple Examples

Apple's HLS example files:

https://developer.apple.com/library/ios/technotes/tn2288/_index.html

## HLS Encryption

SRS3 supports slice encryption, for specific usage, refer to [#1093](https://github.com/ossrs/srs/issues/1093#issuecomment-415971022).

## HLS fMP4

SRS (7.0.51+) supports HLS with fMP4 (fragmented MP4) segments instead of traditional MPEG-TS segments. fMP4 is the modern container format for HLS that offers several advantages:

* **Better codec support**: Required for HEVC/H.265 and essential for Low-Latency HLS (LL-HLS)
* **Lower overhead**: fMP4 has smaller file overhead compared to MPEG-TS segments
* **DVR compatibility**: fMP4 segments can be used for DVR and time-shifting applications
* **Future-proof**: fMP4 is the recommended format for modern HLS implementations

To enable HLS with fMP4 segments, set `hls_use_fmp4 on` in your configuration:

```bash
vhost __defaultVhost__ {
    hls {
        enabled on;
        hls_use_fmp4 on;
        hls_fragment 10;
        hls_window 60;
        hls_m3u8_file   [app]/[stream].m3u8;
        hls_init_file   [app]/[stream]-init.mp4;
        hls_fmp4_file   [app]/[stream]-[seq].m4s;
    }
}
```

When fMP4 is enabled, SRS generates:
* **Initialization segment**: `init.mp4` file containing codec information
* **Media segments**: `.m4s` files containing the actual media data

You can customize the file naming using these configuration options:
* `hls_fmp4_file`: Media segment filename pattern (default: `[app]/[stream]-[seq].m4s`)
* `hls_init_file`: Initialization segment filename pattern (default: `[app]/[stream]/init.mp4`)

For example, start SRS with fMP4 enabled:

```bash
docker run --rm -it -p 1935:1935 -p 8080:8080 ossrs/srs:7 \
  ./objs/srs -c conf/hls.mp4.conf
```

Publish a stream:

```bash
ffmpeg -re -i ./doc/source.flv -c copy -f flv rtmp://localhost/live/livestream
```

The generated m3u8 playlist will reference fMP4 segments instead of TS segments, making it compatible with modern HLS players and Low-Latency HLS requirements.

Play the stream by SRS player: [http://localhost:8080/live/livestream.m3u8](http://localhost:8080/players/srs_player.html?stream=livestream.m3u8)

## IPv6

SRS (v7.0.67+) supports IPv6 for HLS streaming, enabling dual-stack (IPv4/IPv6) operation for HTTP-based live streaming. This allows HLS clients to access streams using IPv6 addresses while maintaining full compatibility with existing IPv4 infrastructure.

IPv6 support is enabled automatically when SRS detects IPv6 addresses in the HTTP server configuration. Configure the HTTP server to listen on IPv6 addresses:

```bash
http_server {
    enabled on;
    # Listen on both IPv4 and IPv6
    listen 8080 [::]:8080;
    dir ./objs/nginx/html;
}
```

Access HLS streams via IPv6:

```bash
# HLS stream via IPv6
http://[::1]:8080/live/livestream.m3u8

# HLS segments are also accessible via IPv6
http://[::1]:8080/live/livestream-1.ts
http://[::1]:8080/live/livestream-2.ts
```

Play HLS stream via IPv6 using FFplay:

```bash
ffplay 'http://[::1]:8080/live/livestream.m3u8'
```

SRS supports dual-stack HLS operation, allowing both IPv4 and IPv6 clients to access streams simultaneously:

```bash
http_server {
    enabled on;
    # Listen on both IPv4 and IPv6
    listen 8080 [::]:8080;
    dir ./objs/nginx/html;
}
```

This configuration allows:
- IPv4 clients: `http://192.168.1.100:8080/live/livestream.m3u8`
- IPv6 clients: `http://[2001:db8::1]:8080/live/livestream.m3u8`

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/hls)

