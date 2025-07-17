---
title: RTMP
sidebar_label: RTMP
hide_title: false
hide_table_of_contents: false
---

# RTMP

RTMP is a basic and de facto standard protocol for live streaming for many years.

However, Adobe neither maintaining RTMP protocol nor contributing as an RFC protocol, so many new features
aren't supported by RTMP, such as HEVC and opus. By March 2023, the [Enhanced RTMP](https://github.com/veovera/enhanced-rtmp) 
project is finally set up, supporting HEVC and AV1. SRS and OBS now support [HEVC](https://github.com/veovera/enhanced-rtmp/issues/4) 
encoding based on Enhanced RTMP.

For live streaming producing, more recent years, SRT, WebRTC and RIST have been growing rapidly. More and
more devices supported SRT or RIST in live streaming. You're also able to use WebRTC for live streaming.

For live streaming deliver, HLS is the most common used protocol, is supported by almost all CDN and devices
such as PC, iOS, Android and tablet PC. However, HLS has large (3~5s+) latency, you could use HTTP-FLV,
HTTP-TS or WebRTC for low latency use scenario.

Today, RTMP is still used in live streaming producing, for example, OBS publish RTMP stream to YouTube, Twitch, etc.
If you want to ingest stream from a device or publish to a platform, RTMP is the right choice for compatibility.

## Usage

SRS supports RTMP by default, please run by [docker](./getting-started.md) or [build from source](./getting-started-build.md):

```bash
docker run --rm -it -p 1935:1935 ossrs/srs:5 \
  ./objs/srs -c conf/rtmp.conf
```

Publish stream by [FFmpeg](https://ffmpeg.org/download.html) or [OBS](https://obsproject.com/download) :

```bash
ffmpeg -re -i ./doc/source.flv -c copy -f flv rtmp://localhost/live/livestream
```

Play stream by:

* RTMP (by [VLC](https://www.videolan.org/)): `rtmp://localhost/live/livestream`

SRS supports converting RTMP to other protocols, described in next sections.

## Config

The configuration about RTMP:

```bash
vhost __defaultVhost__ {
    # whether enable min delay mode for vhost.
    # for min latency mode:
    # 1. disable the publish.mr for vhost.
    # 2. use timeout for cond wait for consumer queue.
    # @see https://github.com/ossrs/srs/issues/257
    # default: off (for RTMP/HTTP-FLV)
    # default: on (for WebRTC)
    min_latency off;

    # whether enable the TCP_NODELAY
    # if on, set the nodelay of fd by setsockopt
    # Overwrite by env SRS_VHOST_TCP_NODELAY for all vhosts.
    # default: off
    tcp_nodelay off;

    # the default chunk size is 128, max is 65536,
    # some client does not support chunk size change,
    # vhost chunk size will override the global value.
    # Overwrite by env SRS_VHOST_CHUNK_SIZE for all vhosts.
    # default: global chunk size.
    chunk_size 128;
    
    # The input ack size, 0 to not set.
    # Generally, it's set by the message from peer,
    # but for some peer(encoder), it never send message but use a different ack size.
    # We can chnage the default ack size in server-side, to send acknowledge message,
    # or the encoder maybe blocked after publishing for some time.
    # Overwrite by env SRS_VHOST_IN_ACK_SIZE for all vhosts.
    # Default: 0
    in_ack_size 0;
    
    # The output ack size, 0 to not set.
    # This is used to notify the peer(player) to send acknowledge to server.
    # Overwrite by env SRS_VHOST_OUT_ACK_SIZE for all vhosts.
    # Default: 2500000
    out_ack_size 2500000;
    
    # the config for FMLE/Flash publisher, which push RTMP to SRS.
    publish {
        # about MR, read https://github.com/ossrs/srs/issues/241
        # when enabled the mr, SRS will read as large as possible.
        # Overwrite by env SRS_VHOST_PUBLISH_MR for all vhosts.
        # default: off
        mr off;
        # the latency in ms for MR(merged-read),
        # the performance+ when latency+, and memory+,
        #       memory(buffer) = latency * kbps / 8
        # for example, latency=500ms, kbps=3000kbps, each publish connection will consume
        #       memory = 500 * 3000 / 8 = 187500B = 183KB
        # when there are 2500 publisher, the total memory of SRS at least:
        #       183KB * 2500 = 446MB
        # the recommended value is [300, 2000]
        # Overwrite by env SRS_VHOST_PUBLISH_MR_LATENCY for all vhosts.
        # default: 350
        mr_latency 350;

        # the 1st packet timeout in ms for encoder.
        # Overwrite by env SRS_VHOST_PUBLISH_FIRSTPKT_TIMEOUT for all vhosts.
        # default: 20000
        firstpkt_timeout 20000;
        # the normal packet timeout in ms for encoder.
        # Overwrite by env SRS_VHOST_PUBLISH_NORMAL_TIMEOUT for all vhosts.
        # default: 5000
        normal_timeout 7000;
        # whether parse the sps when publish stream.
        # we can got the resolution of video for stat api.
        # but we may failed to cause publish failed.
        # @remark If disabled, HLS might never update the sps/pps, it depends on this.
        # Overwrite by env SRS_VHOST_PUBLISH_PARSE_SPS for all vhosts.
        # default: on
        parse_sps on;
        # When parsing SPS/PPS, whether try ANNEXB first. If not, try IBMF first, then ANNEXB.
        # Overwrite by env SRS_VHOST_PUBLISH_TRY_ANNEXB_FIRST for all vhosts.
        # default: on
        try_annexb_first on;
        # The timeout in seconds to disconnect publisher when idle, which means no players.
        # Note that 0 means no timeout or this feature is disabled.
        # Note that this feature conflicts with forward, because it disconnect the publisher stream.
        # Overwrite by env SRS_VHOST_PUBLISH_KICKOFF_FOR_IDLE for all vhosts.
        # default: 0
        kickoff_for_idle 0;
    }
    
    # for play client, both RTMP and other stream clients,
    # for instance, the HTTP FLV stream clients.
    play {
        # whether cache the last gop.
        # if on, cache the last gop and dispatch to client,
        #   to enabled fast startup for client, client play immediately.
        # if off, send the latest media data to client,
        #   client need to wait for the next Iframe to decode and show the video.
        # set to off if requires min delay;
        # set to on if requires client fast startup.
        # Overwrite by env SRS_VHOST_PLAY_GOP_CACHE for all vhosts.
        # default: on
        gop_cache off;

        # Limit the max frames in gop cache. It might cause OOM if video stream has no IDR frame, so we limit to N
        # frames by default. Note that it's the size of gop cache, including videos, audios and other messages.
        # Overwrite by env SRS_VHOST_PLAY_GOP_CACHE_MAX_FRAMES for all vhosts.
        # default: 2500
        gop_cache_max_frames 2500;

        # the max live queue length in seconds.
        # if the messages in the queue exceed the max length,
        # drop the old whole gop.
        # Overwrite by env SRS_VHOST_PLAY_QUEUE_LENGTH for all vhosts.
        # default: 30
        queue_length 10;

        # about the stream monotonically increasing:
        #   1. video timestamp is monotonically increasing,
        #   2. audio timestamp is monotonically increasing,
        #   3. video and audio timestamp is interleaved/mixed monotonically increasing.
        # it's specified by RTMP specification, @see 3. Byte Order, Alignment, and Time Format
        # however, some encoder cannot provides this feature, please set this to off to ignore time jitter.
        # the time jitter algorithm:
        #   1. full, to ensure stream start at zero, and ensure stream monotonically increasing.
        #   2. zero, only ensure stream start at zero, ignore timestamp jitter.
        #   3. off, disable the time jitter algorithm, like atc.
        # @remark for full, correct timestamp only when |delta| > 250ms.
        # @remark disabled when atc is on.
        # Overwrite by env SRS_VHOST_PLAY_TIME_JITTER for all vhosts.
        # default: full
        time_jitter full;
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
        # @remark when atc is on, auto off the time_jitter
        # Overwrite by env SRS_VHOST_PLAY_ATC for all vhosts.
        # default: off
        atc off;
        # whether use the interleaved/mixed algorithm to correct the timestamp.
        # if on, always ensure the timestamp of audio+video is interleaved/mixed monotonically increase.
        # if off, use time_jitter to correct the timestamp if required.
        # @remark to use mix_correct, atc should on(or time_jitter should off).
        # Overwrite by env SRS_VHOST_PLAY_MIX_CORRECT for all vhosts.
        # default: off
        mix_correct off;

        # whether enable the auto atc,
        # if enabled, detect the bravo_atc="true" in onMetaData packet,
        # set atc to on if matched.
        # always ignore the onMetaData if atc_auto is off.
        # Overwrite by env SRS_VHOST_PLAY_ATC_AUTO for all vhosts.
        # default: off
        atc_auto off;

        # set the MW(merged-write) latency in ms.
        # SRS always set mw on, so we just set the latency value.
        # the latency of stream >= mw_latency + mr_latency
        # the value recomment is [300, 1800]
        # @remark For WebRTC, we enable pass-by-timestamp mode, so we ignore this config.
        # default: 350 (For RTMP/HTTP-FLV)
        # Overwrite by env SRS_VHOST_PLAY_MW_LATENCY for all vhosts.
        # default: 0 (For WebRTC)
        mw_latency 350;

        # Set the MW(merged-write) min messages.
        # default: 0 (For Real-Time, min_latency on)
        # default: 1 (For WebRTC, min_latency off)
        # default: 8 (For RTMP/HTTP-FLV, min_latency off).
        # Overwrite by env SRS_VHOST_PLAY_MW_MSGS for all vhosts.
        mw_msgs 8;

        # the minimal packets send interval in ms,
        # used to control the ndiff of stream by srs_rtmp_dump,
        # for example, some device can only accept some stream which
        # delivery packets in constant interval(not cbr).
        # @remark 0 to disable the minimal interval.
        # @remark >0 to make the srs to send message one by one.
        # @remark user can get the right packets interval in ms by srs_rtmp_dump.
        # Overwrite by env SRS_VHOST_PLAY_SEND_MIN_INTERVAL for all vhosts.
        # default: 0
        send_min_interval 10.0;
        # whether reduce the sequence header,
        # for some client which cannot got duplicated sequence header,
        # while the sequence header is not changed yet.
        # Overwrite by env SRS_VHOST_PLAY_REDUCE_SEQUENCE_HEADER for all vhosts.
        # default: off
        reduce_sequence_header on;
    }
}
```

> Note: These configurations are for publish and play. Note that there are some other configurations in other sections,
for example, converting RTMP to [HTTP-FLV](./flv.md#config) or HTTP-TS.

## On Demand Live Streaming

In some situations, you might want to start streaming only when someone starts watching:

1. The streaming source connects to the system but doesn't send the stream to SRS.
2. The player connects to the system and requests to play the stream.
3. The system tells the streaming source to start sending the stream to SRS.
4. The player gets the stream from SRS and plays it.

> Note: The "system" here refers to your business system, not SRS.

This is called "on-demand live streaming" or "on-demand streaming." What happens if the player stops watching?

1. The system needs to tell the streaming source to stop sending the stream.
2. Or, when the last player stops watching, SRS waits for a while and then disconnects the stream.

The second solution is recommended, as it's easier to use. Your system won't need to tell the streaming source to stop, because SRS will disconnect it automatically. You just need to enable the following configuration:

```bash
# The timeout in seconds to disconnect publisher when idle, which means no players.
# Note that 0 means no timeout or this feature is disabled.
# Note that this feature conflicts with forward, because it disconnect the publisher stream.
# Overwrite by env SRS_VHOST_PUBLISH_KICKOFF_FOR_IDLE for all vhosts.
# default: 0
kickoff_for_idle 0;
```

For more details, you can refer to [this PR](https://github.com/ossrs/srs/pull/3105).

## Converting RTMP to HLS

If want to convert RTMP to HLS, please see [HLS](./hls.md).

## Converting RTMP to HTTP-FLV

If want to convert RTMP to HTTP-FLV or HTTP-TS, please see [HTTP-FLV](./flv.md).

## Converting RTMP to WebRTC

If want to convert RTMP to WebRTC, please see [WebRTC: RTMP to RTC](./webrtc.md#rtmp-to-rtc).

## Converting RTMP to MPEGTS-DASH

If want to convert RTMP to MPEGTS-DASH, please see [DASH](./sample-dash.md).

## Converting SRT to RTMP

If want to convert SRT to RTMP, please see [SRT](./srt.md).

## Converting WebRTC to RTMP

If want to convert WebRTC to RTMP, please see [WebRTC: RTC to RTMP](./webrtc.md#rtc-to-rtmp).

## RTMP Cluster

If want to support a large set of players, please see [Edge Cluster](./edge.md).

If want to support a larget set of publishers or streams, please see [Origin Cluster](./origin-cluster.md).

Note that there are lots of solutions for [load balancing](../../../blog/load-balancing-streaming-servers).

## Low Latency RTMP

If want to support low latency RTMP stream, please see [LowLatency](./low-latency.md).

## Timestamp Jitter

SRS support correcting the timestamp for RTMP, please see [Jitter](./time-jitter.md).

If wants SRS to keep the original timestamp, you can enable [ATC](./rtmp-atc.md).

## Performance

SRS use writev for high performance RTMP delivery, please follow [benchmark](./performance.md##performance-banchmark)
to test it.

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/rtmp)
