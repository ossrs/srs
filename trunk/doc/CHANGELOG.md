

# Changelog

The changelog for SRS.

<a name="v6-changes"></a>

## SRS 6.0 Changelog
* v6.0, 2023-08-30, Merge [#3776](https://github.com/ossrs/srs/pull/3776): Compile: Add aarch64 to the conditions of use of the cbrt function. v6.0.72 (#3776)
* v6.0, 2023-08-30, Merge [#3779](https://github.com/ossrs/srs/pull/3779): Support HTTP-API for fetching reload result. v6.0.71 (#3779)
* v6.0, 2023-08-28, Merge [#3503](https://github.com/ossrs/srs/pull/3503): SrsContextId assignment can be improved without create a duplicated one. v6.0.70 (#3503)
* v6.0, 2023-08-28, Merge [#3781](https://github.com/ossrs/srs/pull/3781): HLS: Fix on_hls and hls_dispose critical zone issue. v6.0.69 (#3781)
* v6.0, 2023-08-28, Merge [#3768](https://github.com/ossrs/srs/pull/3768): Support include empty config file. v6.0.68 (#3768)
* v6.0, 2023-08-25, Merge [#3782](https://github.com/ossrs/srs/pull/3782): HLS: Support reload HLS asynchronously. v6.0.67 (#3782)
* v6.0, 2023-08-22, Merge [#3775](https://github.com/ossrs/srs/pull/3775): Bugfix: Log format output type does not match. v6.0.66 (#3699)
* v6.0, 2023-08-02, Merge [#3750](https://github.com/ossrs/srs/pull/3750): HLS: Ignore empty NALU to avoid error. v6.0.64 (#3750)
* v6.0, 2023-07-27, Merge [#3611](https://github.com/ossrs/srs/pull/3611): Design and implement helm capabilities to streamline the deployment process of an SRS cluster.. v6.0.63 (#3611)
* v6.0, 2023-07-27, Merge [#3703](https://github.com/ossrs/srs/pull/3703): The identifier "ShowCouroutines" needs to be modified to "ShowCoroutines" in order to rectify the typographical error.. v6.0.62 (#3703)
* v6.0, 2023-07-26, Merge [#3699](https://github.com/ossrs/srs/pull/3699): Bugfix: Eliminate the redundant declaration of the _srs_rtc_manager variable.. v6.0.61 (#3699)
* v6.0, 2023-07-21, Merge [#3695](https://github.com/ossrs/srs/pull/3695): API: Fix HTTPS callback issue using SNI in TLS client handshake. v6.0.61 (#3695)
* v6.0, 2023-07-18, Merge [#3515](https://github.com/ossrs/srs/pull/3515): WebRTC: Support config the bitrate of transcoding AAC to Opus. v6.0.60 (#3515)
* v6.0, 2023-07-09, Merge [#3615](https://github.com/ossrs/srs/pull/3615): Compile: Fix typo for 3rdparty. v6.0.59 (#3615)
* v6.0, 2023-07-01, Merge [#3595](https://github.com/ossrs/srs/pull/3595): WHIP: Improve WHIP deletion by token verification. v6.0.58 (#3595)
* v6.0, 2023-07-01, Merge [#3605](https://github.com/ossrs/srs/pull/3605): BugFix: Resolve the problem of srs_error_t memory leak. v6.0.57 (#3605)
* v6.0, 2023-06-30, Merge [#3596](https://github.com/ossrs/srs/pull/3596): Improve the usage of "transcode" in the "full.conf" file. v6.0.56 (#3596)
* v6.0, 2023-06-21, Merge [#3551](https://github.com/ossrs/srs/pull/3551): H264: Fix H.264 ISOM reserved bit value. v6.0.55 (#3551)
* v6.0, 2023-06-20, Merge [#3594](https://github.com/ossrs/srs/pull/3594): Docker: Refine the main dockerfile. v6.0.54 (#3592)
* v6.0, 2023-06-20, Merge [#3592](https://github.com/ossrs/srs/pull/3592): Fix Permission Issue in depend.sh for OpenSSL Compilation. v6.0.53 (#3592)
* v6.0, 2023-06-20, Merge [#3591](https://github.com/ossrs/srs/pull/3591): Fix crash when process rtcp feedback message. v6.0.52 (#3591)
* v6.0, 2023-06-15, Merge [#3581](https://github.com/ossrs/srs/pull/3581): WHIP: Add OBS support, ensuring compatibility with a unique SDP. v6.0.51 (#3581)
* v6.0, 2023-06-13, Merge [#3579](https://github.com/ossrs/srs/pull/3579): TOC: Welcome to the new TOC member, ZhangJunqin. v6.0.50 (#3579)
* v6.0, 2023-06-12, Merge [#3570](https://github.com/ossrs/srs/pull/3570): GB: Correct the range of keyframe error for compile warning. v6.0.49 (#3570)
* v6.0, 2023-06-05, Fix command injection in demonstration api-server for HTTP callback. v6.0.48
* v6.0, 2023-06-05, Merge [#3565](https://github.com/ossrs/srs/pull/3565): DTLS: Use bio callback to get fragment packet. v6.0.47 (#3565)
* v6.0, 2023-05-29, Merge [#3513](https://github.com/ossrs/srs/pull/3513): SSL: Fix SSL_get_error get the error of other coroutine. v6.0.46 (#3513)
* v6.0, 2023-05-14, Merge [#3534](https://github.com/ossrs/srs/pull/3534): Replace sprintf with snprintf to eliminate compile warnings. v6.0.45 (#3534)
* v6.0, 2023-05-13, Merge [#3541](https://github.com/ossrs/srs/pull/3541): asan: Fix memory leak in asan by releasing global IPs when run_directly_or_daemon fails. v6.0.44 (#3541)
* v6.0, 2023-05-12, Merge [#3539](https://github.com/ossrs/srs/pull/3539): WHIP: Improve HTTP DELETE for notifying server unpublish event. v6.0.43 (#3539)
* v6.0, 2023-04-08, Merge [#3495](https://github.com/ossrs/srs/pull/3495): RTMP: Support enhanced RTMP specification for HEVC. v6.0.42 (#3495)
* v6.0, 2023-04-01, Merge [#3392](https://github.com/ossrs/srs/pull/3392): Support composited bridges for 1:N protocols converting. v6.0.41 (#3392)
* v6.0, 2023-04-01, Merge [#3458](https://github.com/ossrs/srs/pull/3450): API: Support HTTP basic authentication for API. v6.0.40 (#3458)
* v6.0, 2023-03-27, Merge [#3450](https://github.com/ossrs/srs/pull/3450): WebRTC: Error message carries the SDP when failed. v6.0.39 (#3450)
* v6.0, 2023-03-25, Merge [#3477](https://github.com/ossrs/srs/pull/3477): Remove unnecessary NULL check in srs_freep. v6.0.38 (#3477)
* v6.0, 2023-03-25, Merge [#3455](https://github.com/ossrs/srs/pull/3455): RTC: Call on_play before create session, for it might be freed for timeout. v6.0.37 (#3455)
* v6.0, 2023-03-22, Merge [#3427](https://github.com/ossrs/srs/pull/3427): WHIP: Support DELETE resource for Larix Broadcaster. v6.0.36 (#3427)
* v6.0, 2023-03-20, Merge [#3460](https://github.com/ossrs/srs/pull/3460): WebRTC: Support WHIP/WHEP players. v6.0.35 (#3460)
* v6.0, 2023-03-07, Merge [#3441](https://github.com/ossrs/srs/pull/3441): HEVC: webrtc support hevc on safari. v6.0.34 (#3441)
* v6.0, 2023-03-07, Merge [#3446](https://github.com/ossrs/srs/pull/3446): WebRTC: Warning if no ideal profile. v6.0.33 (#3446)
* v6.0, 2023-03-06, Merge [#3445](https://github.com/ossrs/srs/pull/3445): Support configure for generic linux. v6.0.32 (#3445)
* v6.0, 2023-03-04, Merge [#3105](https://github.com/ossrs/srs/pull/3105): Kickoff publisher when stream is idle, which means no players. v6.0.31 (#3105)
* v6.0, 2023-02-25, Merge [#3438](https://github.com/ossrs/srs/pull/3438): Forward add question mark to the end. v6.0.30 (#3438)
* v6.0, 2023-02-25, Merge [#3416](https://github.com/ossrs/srs/pull/3416): GB: Support HEVC for regression test and load tool for GB. v6.0.29 (#3416)
* v6.0, 2023-02-25, Merge [#3424](https://github.com/ossrs/srs/pull/3424): API: Add service_id for http_hooks, which identify the process. v6.0.28 (#3424)
* v6.0, 2023-02-22, Compatible with legacy RTMP URL. v6.0.27
* v6.0, 2023-02-16, Merge [#3411](https://github.com/ossrs/srs/pull/3411): HEVC: Fix nalu vec duplicate when h265 vps/sps/pps demux. v6.0.26 (#3411)
* v6.0, 2023-02-14, Merge [#3408](https://github.com/ossrs/srs/pull/3408): GB: Support H.265 for GB28181. v6.0.25 (#3408)
* v6.0, 2023-02-12, Merge [#3409](https://github.com/ossrs/srs/pull/3409): SRT: Reduce latency to 200ms of srt2rtc.conf. v6.0.24 (#3409)
* v6.0, 2023-02-08, Merge [#3391](https://github.com/ossrs/srs/pull/3391): Config: Error when both HLS and HTTP-TS enabled. v6.0.23 (#3391)
* v6.0, 2023-02-08, Merge [#3389](https://github.com/ossrs/srs/pull/3389): Kernel: Fix demux SPS error for NVENC and LARIX. v6.0.22 (#3389)
* v6.0, 2023-01-29, Merge [#3371](https://github.com/ossrs/srs/pull/3371): HLS: support kick-off hls client. v6.0.21 (#3371)
* v6.0, 2023-01-19, Merge [#3366](https://github.com/ossrs/srs/pull/3366): H265: Support HEVC over SRT. v6.0.20 (#465) (#3366)
* v6.0, 2023-01-19, Merge [#3318](https://github.com/ossrs/srs/pull/3318): RTC: fix rtc publisher pli cid. v6.0.19 (#3318)
* v6.0, 2023-01-18, Merge [#3382](https://github.com/ossrs/srs/pull/3382): Rewrite research/api-server code by Go, remove Python. v6.0.18 (#3382)
* v6.0, 2023-01-18, Merge [#3386](https://github.com/ossrs/srs/pull/3386): SRT: fix crash when srt_to_rtmp off. v6.0.17 (#3386)
* v5.0, 2023-01-17, Merge [#3385](https://github.com/ossrs/srs/pull/3385): API: Support server/pid/service label for exporter and api. v6.0.16 (#3385)
* v6.0, 2023-01-17, Merge [#3379](https://github.com/ossrs/srs/pull/3379): H265: Support demux vps/pps info. v6.0.15
* v6.0, 2023-01-08, Merge [#3360](https://github.com/ossrs/srs/pull/3360): H265: Support DVR HEVC stream to MP4. v6.0.14
* v6.0, 2023-01-06, Merge [#3363](https://github.com/ossrs/srs/issues/3363): HTTP: Add CORS Header for private network access. v6.0.13
* v6.0, 2023-01-04, Merge [#3362](https://github.com/ossrs/srs/issues/3362): SRT: Upgrade libsrt from 1.4.1 to 1.5.1. v6.0.12
* v6.0, 2023-01-02, For [#465](https://github.com/ossrs/srs/issues/465): HLS: Support HEVC over HLS. v6.0.11
* v6.0, 2022-12-30, Support first SRS6 version. v6.0.10
* v6.0, 2022-12-26, For [#465](https://github.com/ossrs/srs/issues/465): TS: Support disable audio or video to make mpegts.js happy. v6.0.9
* v6.0, 2022-12-26, For [#465](https://github.com/ossrs/srs/issues/465): TS: Fix bug for codec detecting for HTTP-TS. v6.0.8
* v6.0, 2022-12-25, For [#296](https://github.com/ossrs/srs/issues/296): Fix [#3338](https://github.com/ossrs/srs/issues/3338): MP3: Support play HTTP-MP3 by H5(srs-player). v6.0.7
* v6.0, 2022-12-17, Merge 5.0: FLV header and SRT bugfix. v6.0.6
* v6.0, 2022-12-04, Merge [#3271](https://github.com/ossrs/srs/pull/3271): H265: The codec information is incorrect. v6.0.5
* v6.0, 2022-11-23, Merge [#3275](https://github.com/ossrs/srs/pull/3275): H265: Support HEVC over HTTP-TS. v6.0.4
* v6.0, 2022-11-23, Merge [#3274](https://github.com/ossrs/srs/pull/3274): H265: Support parse multiple NALUs in a frame. v6.0.3
* v6.0, 2022-11-22, Merge [#3272](https://github.com/ossrs/srs/pull/3272): H265: Support HEVC over RTMP or HTTP-FLV. v6.0.2
* v6.0, 2022-11-22, Merge [#3268](https://github.com/ossrs/srs/pull/3268): H265: Update mpegts.js to play HEVC over HTTP-TS/FLV. v6.0.1
* v6.0, 2022-11-22, Init SRS 6. v6.0.0

<a name="v5-changes"></a>

## SRS 5.0 Changelog
* v5.0, 2023-08-30, Merge [#3779](https://github.com/ossrs/srs/pull/3779): Support HTTP-API for fetching reload result. v5.0.176 (#3779)
* v5.0, 2023-08-28, Merge [#3503](https://github.com/ossrs/srs/pull/3503): SrsContextId assignment can be improved without create a duplicated one. v5.0.175 (#3503)
* v5.0, 2023-08-28, Merge [#3781](https://github.com/ossrs/srs/pull/3781): HLS: Fix on_hls and hls_dispose critical zone issue. v5.0.174 (#3781)
* v5.0, 2023-08-28, Merge [#3768](https://github.com/ossrs/srs/pull/3768): Support include empty config file. v5.0.173 (#3768)
* v5.0, 2023-08-25, Merge [#3782](https://github.com/ossrs/srs/pull/3782): HLS: Support reload HLS asynchronously. v5.0.172 (#3782)
* v5.0, 2023-08-22, Merge [#3775](https://github.com/ossrs/srs/pull/3775): Bugfix: Log format output type does not match. v5.0.171 (#3699)
* v5.0, 2023-08-02, HLS: Ignore empty NALU to avoid error. v5.0.170
* v5.0, 2023-07-26, Merge [#3699](https://github.com/ossrs/srs/pull/3699): Bugfix: Eliminate the redundant declaration of the _srs_rtc_manager variable. v5.0.168 (#3699)
* v5.0, 2023-07-21, Merge [#3695](https://github.com/ossrs/srs/pull/3695): API: Fix HTTPS callback issue using SNI in TLS client handshake. v5.0.168 (#3695)
* v5.0, 2023-07-18, Merge [#3515](https://github.com/ossrs/srs/pull/3515): WebRTC: Support config the bitrate of transcoding AAC to Opus. v5.0.167 (#3515)
* v5.0, 2023-07-09, Merge [#3615](https://github.com/ossrs/srs/pull/3615): Compile: Fix typo for 3rdparty. v5.0.166 (#3615)
* v5.0, 2023-07-01, Merge [#3595](https://github.com/ossrs/srs/pull/3595): WHIP: Improve WHIP deletion by token verification. v5.0.164 (#3595)
* v5.0, 2023-07-01, Merge [#3605](https://github.com/ossrs/srs/pull/3605): BugFix: Resolve the problem of srs_error_t memory leak. v5.0.163 (#3605)
* v5.0, 2023-06-30, Merge [#3596](https://github.com/ossrs/srs/pull/3596): Improve the usage of "transcode" in the "full.conf" file. v5.0.162 (#3596)
* v5.0, 2023-06-21, Merge [#3551](https://github.com/ossrs/srs/pull/3551): H264: Fix H.264 ISOM reserved bit value. v5.0.161 (#3551)
* v5.0, 2023-06-20, Merge [#3592](https://github.com/ossrs/srs/pull/3592): Fix Permission Issue in depend.sh for OpenSSL Compilation. v5.0.159 (#3592)
* v5.0, 2023-06-20, Merge [#3591](https://github.com/ossrs/srs/pull/3591): Fix crash when process rtcp feedback message. v5.0.159 (#3591)
* v5.0, 2023-06-15, Merge [#3581](https://github.com/ossrs/srs/pull/3581): WHIP: Add OBS support, ensuring compatibility with a unique SDP. v5.0.158 (#3581)
* v5.0, 2023-06-05, Fix command injection in demonstration api-server for HTTP callback. v5.0.157
* v5.0, 2023-06-05, Merge [#3565](https://github.com/ossrs/srs/pull/3565): DTLS: Use bio callback to get fragment packet. v5.0.156 (#3565)
* v5.0, 2023-05-29, Merge [#3513](https://github.com/ossrs/srs/pull/3513): SSL: Fix SSL_get_error get the error of other coroutine. v5.0.155 (#3513)
* v5.0, 2023-05-13, Merge [#3541](https://github.com/ossrs/srs/pull/3541): asan: Fix memory leak in asan by releasing global IPs when run_directly_or_daemon fails. v5.0.154 (#3541)
* v5.0, 2023-05-12, Merge [#3539](https://github.com/ossrs/srs/pull/3539): WHIP: Improve HTTP DELETE for notifying server unpublish event. v5.0.153 (#3539)
* v5.0, 2023-03-27, Merge [#3450](https://github.com/ossrs/srs/pull/3450): WebRTC: Error message carries the SDP when failed. v5.0.151 (#3450)
* v5.0, 2023-03-25, Merge [#3477](https://github.com/ossrs/srs/pull/3477): Remove unneccessary NULL check in srs_freep. v5.0.150 (#3477)
* v5.0, 2023-03-25, Merge [#3455](https://github.com/ossrs/srs/pull/3455): RTC: Call on_play before create session, for it might be freed for timeout. v5.0.149 (#3455)
* v5.0, 2023-03-22, Merge [#3427](https://github.com/ossrs/srs/pull/3427): WHIP: Support DELETE resource for Larix Broadcaster. v5.0.148 (#3427)
* v5.0, 2023-03-20, Merge [#3460](https://github.com/ossrs/srs/pull/3460): WebRTC: Support WHIP/WHEP players. v5.0.147 (#3460)
* v5.0, 2023-03-07, Merge [#3446](https://github.com/ossrs/srs/pull/3446): WebRTC: Warning if no ideal profile. v5.0.146 (#3446)
* v5.0, 2023-03-06, Merge [#3445](https://github.com/ossrs/srs/pull/3445): Support configure for generic linux. v5.0.145 (#3445)
* v5.0, 2023-03-04, Merge [#3105](https://github.com/ossrs/srs/pull/3105): Kickoff publisher when stream is idle, which means no players. v5.0.144 (#3105)
* v5.0, 2023-02-25, Merge [#3424](https://github.com/ossrs/srs/pull/3424): API: Add service_id for http_hooks, which identify the process. v5.0.143 (#3424)
* v5.0, 2023-02-22, Compatible with legacy RTMP URL. v5.0.142
* v5.0, 2023-02-12, Merge [#3409](https://github.com/ossrs/srs/pull/3409): SRT: Reduce latency to 200ms of srt2rtc.conf. v5.0.141 (#3409)
* v5.0, 2023-02-08, Merge [#3391](https://github.com/ossrs/srs/pull/3391): Config: Error when both HLS and HTTP-TS enabled. v5.0.140 (#3391)
* v5.0, 2023-01-29, Merge [#3371](https://github.com/ossrs/srs/pull/3371): HLS: support kick-off hls client. v5.0.139 (#3371)
* v5.0, 2023-01-19, Merge [#3318](https://github.com/ossrs/srs/pull/3318): RTC: fix rtc publisher pli cid. v5.0.138 (#3318)
* v5.0, 2023-01-18, Merge [#3382](https://github.com/ossrs/srs/pull/3382): Rewrite research/api-server code by Go, remove Python. v5.0.137 (#3382)
* v5.0, 2023-01-18, Merge [#3386](https://github.com/ossrs/srs/pull/3386): SRT: fix crash when srt_to_rtmp off. v5.0.136 (#3386)
* v5.0, 2023-01-17, Merge [#3385](https://github.com/ossrs/srs/pull/3385): API: Support server/pid/service label for exporter and api. v5.0.135 (#3385)
* v5.0, 2023-01-17, Merge [#3383](https://github.com/ossrs/srs/pull/3383): GB: Fix PSM parsing indicator bug. v5.0.134 (#3383)
* v5.0, 2023-01-08, Merge [#3308](https://github.com/ossrs/srs/pull/3308): DVR: Improve file write performance by fwrite with cache. v5.0.133
* v5.0, 2023-01-06, DVR: Support blackbox test based on hooks. v5.0.132
* v5.0, 2023-01-06, FFmpeg: Support build with FFmpeg native opus. v5.0.131 (#3140)
* v5.0, 2023-01-05, CORS: Refine HTTP CORS headers. v5.0.130
* v5.0, 2023-01-03, Add blackbox test for HLS and MP3 codec. v5.0.129
* v5.0, 2023-01-02, Merge [#3355](https://github.com/ossrs/srs/pull/3355): Test: Support blackbox test by FFmpeg. v5.0.128
* v5.0, 2023-01-02, Fix [#3347](https://github.com/ossrs/srs/issues/3347): Asan: Disable asan for CentOS and use statically link if possible. v5.0.127
* v5.0, 2023-01-01, For [#296](https://github.com/ossrs/srs/issues/296): MP3: Upgrade mpegts.js to support HTTP-TS with mp3. v5.0.126
* v5.0, 2023-01-01, For [#3349](https://github.com/ossrs/srs/issues/3349): API: Fix duplicated on_stop callback event bug. v5.0.125
* v5.0, 2022-12-31, GB28181: Enable regression test for gb28181. v5.0.122
* v5.0, 2022-12-31, Refine configure to guess OS automatically. v5.0.121
* v5.0, 2022-12-31, Refine default config file for SRS. v5.0.120
* v5.0, 2022-12-26, For [#939](https://github.com/ossrs/srs/issues/939): FLV: Fix bug for header flag gussing. v5.0.119
* v5.0, 2022-12-26, For [#296](https://github.com/ossrs/srs/issues/296): MP3: Convert RTMP(MP3) to WebRTC(OPUS). v5.0.118
* v5.0, 2022-12-25, For [#296](https://github.com/ossrs/srs/issues/296): MP3: Support dump stream information. v5.0.117
* v5.0, 2022-12-25, For [#296](https://github.com/ossrs/srs/issues/296): MP3: Support mp3 for RTMP/HLS/HTTP-FLV/HTTP-TS/HLS etc. v5.0.116
* v5.0, 2022-12-24, Fix [#3328](https://github.com/ossrs/srs/issues/3328): Docker: Avoiding duplicated copy files. v5.0.115
* v5.0, 2022-12-20, Merge [#3321](https://github.com/ossrs/srs/pull/3321): GB: Refine lazy object GC. v5.0.114
* v5.0, 2022-12-18, Merge [#3324](https://github.com/ossrs/srs/pull/3324): Asan: Support parse asan symbol backtrace log. v5.0.113
* v5.0, 2022-12-17, Merge [#3323](https://github.com/ossrs/srs/pull/3323): SRT: Fix srt to rtmp crash when sps or pps empty. v5.0.112
* v5.0, 2022-12-15, For [#3300](https://github.com/ossrs/srs/issues/3300): GB28181: Fix memory overlap for small packets. v5.0.111
* v5.0, 2022-12-14, For [#939](https://github.com/ossrs/srs/issues/939): FLV: Support set default has_av and disable guessing. v5.0.110
* v5.0, 2022-12-13, For [#939](https://github.com/ossrs/srs/issues/939): FLV: Drop packet if header flag is not matched. v5.0.109
* v5.0, 2022-12-13, For [#939](https://github.com/ossrs/srs/issues/939): FLV: Reset has_audio or has_video if only sequence header.
* v5.0, 2022-12-12, Merge [#3301](https://github.com/ossrs/srs/pull/3301): DASH: Fix dash crash bug when writing file. v5.0.108
* v5.0, 2022-12-09, Merge [#3296](https://github.com/ossrs/srs/pull/3296): SRT: Support SRT to RTMP to WebRTC. v5.0.107
* v5.0, 2022-12-08, Merge [#3295](https://github.com/ossrs/srs/pull/3295): API: Parse fragment of URI. v5.0.106
* v5.0, 2022-12-04, Cygwin: Enable gb28181 for Windows. v5.0.105
* v5.0, 2022-12-04, Asan: Set asan loging callback. v5.0.104
* v5.0, 2022-12-02, GB28181: Enable GB for CentOS 7 package. v5.0.103
* v5.0, 2022-12-02, Package script support extra options. v5.0.102
* v5.0, 2022-12-02, Disable CLS and APM by default. v5.0.101
* v5.0, 2022-12-01, Config: Add utest for configuring with ENV variables. v5.0.100
* v5.0, 2022-12-01, Live: Fix bug for gop cache limits. v5.0.99
* v5.0, 2022-11-25, SRT: Support transform tlpkdrop to tlpktdrop. 5.0.98
* v5.0, 2022-11-25, Config: Add ENV tips for config. 5.0.97
* v5.0, 2022-11-24, For [#299](https://github.com/ossrs/srs/issues/299), DASH: Fix number mode bug to make it run. 5.0.96
* v5.0, 2022-11-23, For [#3176](https://github.com/ossrs/srs/pull/3176): GB28181: Error and logging for HEVC. v5.0.95
* v5.0, 2022-11-22, Merge [#3236](https://github.com/ossrs/srs/pull/3236): Live: Limit cached max frames by gop_cache_max_frames. v5.0.93
* v5.0, 2022-11-22, Asan: Check libasan and show tips. v5.0.92
* v5.0, 2022-11-21, Merge [#3264](https://github.com/ossrs/srs/pull/3264): Asan: Try to fix st_memory_leak for asan check. (#3264). v5.0.91
* v5.0, 2022-11-21, Asan: Fix global ip address leak check. v5.0.90
* v5.0, 2022-11-20, For [#2532](https://github.com/ossrs/srs/issues/2532): Windows: Support cygwin pipline and packager. v5.0.89
* v5.0, 2022-11-18, Fix [#3215](https://github.com/ossrs/srs/issues/3215): Callback: Fix bug for response string 0. v5.0.88
* v5.0, 2022-11-18, For [#2532](https://github.com/ossrs/srs/issues/2532): Windows: Replace ln by cp for windows. v5.0.87
* v5.0, 2022-10-31, For [#2899](https://github.com/ossrs/srs/issues/2899): Exporter: Add metrics cpu, memory and uname. v5.0.86
* v5.0, 2022-10-30, Config: Support startting with environment variable only. v5.0.85
* v5.0, 2022-10-26, Fix [#3218](https://github.com/ossrs/srs/issues/3218): Log: Follow Java/log4j log level specs. v5.0.83
* v5.0, 2022-10-25, Log: Refine the log interface. v5.0.82
* v5.0, 2022-10-23, For [#3216](https://github.com/ossrs/srs/issues/3216): Support Google Address Sanitizer. v5.0.81
* v5.0, 2022-10-21, Kernel: Support grab backtrace stack when assert fail. v5.0.80
* v5.0, 2022-10-21, ST: Refine tools and CMakeLists.txt. Add backtrace example. v5.0.79
* v5.0, 2022-10-10, For [#2901](https://github.com/ossrs/srs/issues/2901): Edge: Fast disconnect and reconnect. v5.0.78
* v5.0, 2022-10-09, Fix [#3198](https://github.com/ossrs/srs/issues/3198): SRT: Support PUSH SRT by IP and optional port. v5.0.76
* v5.0, 2022-10-06, GB28181: Support GB28181-2016 protocol. v5.0.74
* v5.0, 2022-10-05, HTTP: Skip body and left message by upgrade. v5.0.73
* v5.0, 2022-10-02, ST: Support set context id while thread running. v5.0.72
* v5.0, 2022-09-30, RTC: Refine SDP to support GB28181 SSRC spec. v5.0.71
* v5.0, 2022-09-30, GB28181: Refine HTTP parser to support SIP. v5.0.70
* v5.0, 2022-09-30, Kernel: Support lazy sweeping simple GC. v5.0.69
* v5.0, 2022-09-30, HTTP: Support HTTP header in creating order. v5.0.68
* v5.0, 2022-09-27, For [#2899](https://github.com/ossrs/srs/issues/2899): Exporter: Support exporter for Prometheus. v5.0.67
* v5.0, 2022-09-27, For [#3167](https://github.com/ossrs/srs/issues/3167): WebRTC: Refine sequence jitter algorithm. v5.0.66
* v5.0, 2022-09-22, Fix [#3164](https://github.com/ossrs/srs/issues/3164): SRT: Choppy when audio ts gap is too large. v5.0.65
* v5.0, 2022-09-16, APM: Support distributed tracing by Tencent Cloud APM. v5.0.64
* v5.0, 2022-09-16, For [#3179](https://github.com/ossrs/srs/issues/3179): WebRTC: Make sure the same m-lines order for offer and answer. v5.0.63
* v5.0, 2022-09-10, For [#3174](https://github.com/ossrs/srs/issues/3174): WebRTC: Support Unity to publish or play stream. v5.0.62
* v5.0, 2022-09-06, Fix [#3170](https://github.com/ossrs/srs/issues/3170): WebRTC: Support WHIP(WebRTC-HTTP ingestion protocol). v5.0.61
* v5.0, 2022-09-04, Fix [#2852](https://github.com/ossrs/srs/issues/2852): WebRTC: WebRTC over TCP directly, not TURN. v5.0.60
* v5.0, 2022-09-01, Fix [#1405](https://github.com/ossrs/srs/issues/1405): Restore the stream when parsing failed. v5.0.59
* v5.0, 2022-09-01, Fix [#1405](https://github.com/ossrs/srs/issues/1405): Support guessing IBMF first. v5.0.58
* v5.0, 2022-09-01, ST: Define and use a new jmpbuf. v5.0.57
* v5.0, 2022-08-31, Fix URL parsing bug for `__defaultVhost__`. v5.0.56
* v5.0, 2022-08-30, Fix [#2837](https://github.com/ossrs/srs/issues/2837): Callback: Support stream_url and stream_id. v5.0.55
* v5.0, 2022-08-30, STAT: Refine tcUrl for SRT/RTC. v5.0.54
* v5.0, 2022-08-30, Refactor: Extract SrsNetworkKbps from SrsKbps. v5.0.53
* v5.0, 2022-08-30, Remove bandwidth check because falsh is disabled. v5.0.52
* v5.0, 2022-08-30, Refactor: Use compositor for ISrsKbpsDelta. v5.0.51
* v5.0, 2022-08-29, RTC: Stat the WebRTC clients bandwidth. v5.0.50
* v5.0, 2022-08-29, HLS: Stat the HLS streaming clients bandwidth. v5.0.49
* v5.0, 2022-08-28, URL: Use SrsHttpUri to parse URL and query. v5.0.48
* v5.0, 2022-08-28, Fix [#2881](https://github.com/ossrs/srs/issues/2881): HTTP: Support merging api to server. v5.0.47
* v5.0, 2022-08-27, Fix [#3108](https://github.com/ossrs/srs/issues/3108): STAT: Update stat for SRT. v5.0.46
* v5.0, 2022-08-26, Log: Stat the number of logs. v5.0.45
* v5.0, 2022-08-24, Log: Support write log to tencentcloud CLS. v5.0.44
* v5.0, 2022-08-22, Fix [#3114](https://github.com/ossrs/srs/issues/3114): Origin cluster config bug. v5.0.43
* v5.0, 2022-08-19, For [#2136](https://github.com/ossrs/srs/issues/2136): API: Cleanup no active streams for statistics. v5.0.42
* v5.0, 2022-08-14, Fix [#2747](https://github.com/ossrs/srs/issues/2747): Support Apple Silicon M1(aarch64). v5.0.41
* v5.0, 2022-08-12, Support crossbuild for hisiv500. v5.0.40
* v5.0, 2022-08-10, Build: Detect OS by packager. v5.0.39
* v5.0, 2022-08-06, Support MIPS 64bits for loongson 3A4000/3B3000. v5.0.38
* v5.0, 2022-07-20, Fix [#3115](https://github.com/ossrs/srs/pull/3115): ST: Support RISCV cpu. v5.0.33
* v5.0, 2022-06-29, Support multiple threads by thread pool. v5.0.32
* v5.0, 2022-06-28, ST: Support thread-local for multiple threads. v5.0.31
* v5.0, 2022-06-17, Merge [#3010](https://github.com/ossrs/srs/pull/3010): SRT: Support Coroutine Native SRT over ST. (#3010). v5.0.30
* v5.0, 2022-06-15, For [#3058](https://github.com/ossrs/srs/pull/3058): Docker: Support x86_64, armv7 and aarch64 docker image (#3058). v5.0.29
* v5.0, 2022-04-04, Support NGINX HLS Cluster, see [CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-hls-cluster) or [EN](https://ossrs.io/lts/en-us/docs/v4/doc/sample-hls-cluster). v5.0.28
* v5.0, 2022-03-30, Support DigitalOcean [Droplet SRS 1-Click](https://cloud.digitalocean.com/droplets/new?appId=104916642&size=s-1vcpu-1gb&region=sgp1&image=ossrs-srs&type=applications). v5.0.27
* v5.0, 2022-03-12, Merge [#2943](https://github.com/ossrs/srs/pull/2943): SRT: Fix typo in libsrt build options. v5.0.26
* v5.0, 2022-03-09, Merge SRS 4.0 for bugfix. v5.0.25
* v5.0, 2022-02-16, Merge [#2799](https://github.com/ossrs/srs/pull/2799): Forward: Support dynamic forwarding by backend api. (#2799). v5.0.24
* v5.0, 2022-02-14, Merge [#2878](https://github.com/ossrs/srs/pull/2878): Support include directive for config file. (#2878). v5.0.23
* v5.0, 2022-01-18, Eliminate unused *.as files for Adobe Flash. v5.0.22
* v5.0, 2022-01-13, Switch LICENSE from MIT to **MIT or MulanPSL-2.0**. v5.0.21
* v5.0, 2021-10-24, For [#2689](https://github.com/ossrs/srs/issues/2689): Support loongarch, loongson CPU. v5.0.19
* v5.0, 2021-10-17, Support daemon(fork twice) for Darwin/OSX [ST#23](https://github.com/ossrs/state-threads/issues/23). v5.0.18
* v5.0, 2021-10-16, DVR: support mp3 audio codec. (#2593) v5.0.17
* v5.0, 2021-10-03, OpenWRT: Disable mprotect of ST. 5.0.16
* v5.0, 2021-10-03, Actions: Create source tar lik srs-server-5.0.14.tar.gz
* v5.0, 2021-10-02, ST: Support Cygwin64 and MIPS. 5.0.13
* v5.0, 2021-09-23, Merge [#2578](https://github.com/ossrs/srs/pull/2578) Support http callback on_play/stop. 5.0.12
* v5.0, 2021-08-07, Fix [#2508](https://github.com/ossrs/srs/pull/2508), Support features query by API. 5.0.10
* v5.0, 2021-07-07, Refine AUTHORS.txt to AUTHORS.md, etc. 5.0.8
* v5.0, 2021-07-01, Move AUTHORS.txt to trunk for docker. 5.0.7
* v5.0, 2021-06-28, Squash: Support query lastest available version. 5.0.6
* v5.0, 2021-06-22, Squash: Support ARM platform. 5.0.5
* v5.0, 2021-06-16, Change [GB28181](https://github.com/ossrs/srs/issues/1500) to [feature/gb28181](https://github.com/ossrs/srs/tree/feature/gb28181). 5.0.4
* v5.0, 2021-05-31, Use [SPDX-License-Identifier: MIT](https://spdx.dev/ids/). 5.0.3
* v5.0, 2021-05-19, ST: Simplify it, only Linux/Darwin, epoll/kqueue, single process. 5.0.2
* v5.0, 2021-03-17, Live: Refine edge to follow client and HTTP/302. 5.0.1
* v5.0, 2021-03-15, Init SRS/5. 5.0.0

<a name="v4-changes"></a>

## SRS 4.0 Changelog
* v4.0, 2023-07-21, Merge [#3695](https://github.com/ossrs/srs/pull/3695): API: Fix HTTPS callback issue using SNI in TLS client handshake. v4.0.270 (#3695)
* v4.0, 2022-12-24, For [#296](https://github.com/ossrs/srs/issues/296): MP3: Fix bug for TS or HLS with mp3 codec. v4.0.269
* v4.0, 2022-11-22, Pick [#3079](https://github.com/ossrs/srs/issues/3079): WebRTC: Fix no audio and video issue for Firefox. v4.0.268
* v4.0, 2022-10-10, For [#2901](https://github.com/ossrs/srs/issues/2901): Edge: Fast disconnect and reconnect. v4.0.267
* v4.0, 2022-09-27, For [#3167](https://github.com/ossrs/srs/issues/3167): WebRTC: Refine sequence jitter algorithm. v4.0.266
* v4.0, 2022-09-16, For [#3179](https://github.com/ossrs/srs/issues/3179): WebRTC: Make sure the same m-lines order for offer and answer. v4.0.265
* v4.0, 2022-09-09, For [#3174](https://github.com/ossrs/srs/issues/3174): WebRTC: Support Unity to publish or play stream. v4.0.264
* v4.0, 2022-09-09, Fix [#3093](https://github.com/ossrs/srs/issues/3093): WebRTC: Ignore unknown fmtp for h.264. v4.0.263
* v4.0, 2022-09-06, Fix [#3170](https://github.com/ossrs/srs/issues/3170): WebRTC: Support WHIP(WebRTC-HTTP ingestion protocol). v4.0.262
* v4.0, 2022-09-03, Fix HTTP url parsing bug. v4.0.261
* v4.0, 2022-09-03, For [#3167](https://github.com/ossrs/srs/issues/3167): WebRTC: Play stucked when republish. v4.0.260
* v4.0, 2022-09-02, For [#307](https://github.com/ossrs/srs/issues/307): WebRTC: Support use domain name as CANDIDATE. v4.0.259
* v4.0, 2022-08-29, Copy libxml2-dev for FFmpeg. v4.0.258
* v4.0, 2022-08-24, STAT: Support config server_id and generate one if empty. v4.0.257
* v4.0, 2022-08-24, For [#2136](https://github.com/ossrs/srs/issues/2136): API: Cleanup no active streams for statistics. v4.0.256
* v4.0, 2022-08-17, RTMP URL supports domain in stream parameters. v4.0.255
* v4.0, 2022-08-10, Fix server id generator bug. v4.0.254
* v4.0, 2022-06-29, Update SRS image for r.ossrs.net. v4.0.253
* v4.0, 2022-06-11, For [#3058](https://github.com/ossrs/srs/pull/3058): Docker: Support x86_64, armv7 and aarch64 docker image (#3058). v4.0.252
* v4.0, 2022-03-19, For [#2893](https://github.com/ossrs/srs/pull/2893): SRT: Decouple publish with play url (#2893). v4.0.251
* v4.0, 2022-03-19, Merge [#2908](https://github.com/ossrs/srs/pull/2908): SRT: url supports multiple QueryStrings (#2908). v4.0.250
* v4.0, 2022-03-17, SRT: Support debug and run with CLion. v4.0.249
* v4.0, 2022-03-15, Merge [#2966](https://github.com/ossrs/srs/pull/2966): Bugfix: Fix rtcp nack blp encode bug (#2966). v4.0.248
* v4.0, 2022-03-11, Merge [#2914](https://github.com/ossrs/srs/pull/2914): Security: Enable CIDR for allow/deny play/publish.
* v4.0, 2022-03-07, RTC: Identify the WebRTC publisher in param for hooks. v4.0.247
* v4.0, 2022-03-07, SRT: Append vhost to stream, not app. v4.0.246
* v4.0, 2022-02-15, Fix warnings for uuid. v4.0.245
* v4.0, 2022-02-15, Merge [#2917](https://github.com/ossrs/srs/pull/2917): SRT: Close connection if RTMP failed. (#2917). v4.0.244
* v4.0, 2022-02-15, Refine build script for SRT to avoid warnings. v4.0.243
* v4.0, 2022-02-11, Support new fields for feature query. v4.0.241
* v4.0, 2022-02-09, Mirror docker images in TCR Singapore. v4.0.240
* v4.0, 2022-02-08, Refine the error for WebRTC H5 publisher. v4.0.239
* v4.0, 2022-02-04, Push docker to docker, acr and tcr. v4.0.238
* v4.0, 2022-02-03, Merge [#2888](https://github.com/ossrs/srs/pull/2888): Fix bug when the value of http header is empty. (#2888). v4.0.237
* v4.0, 2022-01-30, Refine docker console, preview by players at the same server. v4.0.236
* v4.0, 2022-01-30, For docker, always use the console for logging. v4.0.235
* v4.0, 2022-01-29, Merge [#2896](https://github.com/ossrs/srs/pull/2896): SRT: Reduce the SRT bug by limit the max times for retry. (#2896). v4.0.234
* v4.0, 2022-01-23, Merge [#2886](https://github.com/ossrs/srs/pull/2886): Fix bug when free addrinfo. (#2886). v4.0.233
* v4.0, 2022-01-22, Merge [#2887](https://github.com/ossrs/srs/pull/2887): Fix memory leak in SrsMetaCache. (#2887). v4.0.232
* v4.0, 2022-01-21, Support docker image for [lighthouse](https://hub.docker.com/r/ossrs/lighthouse). v4.0.231
* v4.0, 2022-01-17, Enable rtmp2rtc and rtc2rtmp for docker.conf
* v4.0, 2022-01-17, Support docker image for [droplet](https://hub.docker.com/r/ossrs/droplet). v4.0.230
* v4.0, 2022-01-13, Merge [#2872](https://github.com/ossrs/srs/pull/2872): RTC: fix play rtc judge for config rtc2rtmp on. (#2872). v4.0.229
* v4.0, 2022-01-13, Support configure with --config as default config file. v4.0.227
* v4.0, 2022-01-13, For [#2880](https://github.com/ossrs/srs/pull/2880): Add SrsAutoFreeH to release ptr with hooks. (#2880). v4.0.226
* v4.0, 2022-01-13, Support api_port to specify the WebRTC API port. v4.0.224
* v4.0, 2022-01-13, Merge [#2873](https://github.com/ossrs/srs/pull/2873): LiveSource: Refine fetch for external exposed interface. (#2873). v4.0.223
* v4.0, 2022-01-13, Add conf/vm.conf for cloud virtual machine. v4.0.222
* v4.0, 2022-01-12, Refine the running homepage. v4.0.221
* v4.0, 2022-01-12, Merge [#2863](https://github.com/ossrs/srs/pull/2863): RTC: fix play crash or no stream for rtmp2rtc tips. (#2863). v4.0.220
* v4.0, 2022-01-05, For [#2717](https://github.com/ossrs/srs/issues/2717): When reopening segment, never update the duration. (#2717). v4.0.219
* v4.0, 2022-01-04, Discover api server and ip as candidates. v4.0.218
* v4.0, 2022-01-04, Install test-on self-sign certificate. v4.0.217
* v4.0, 2022-01-03, For [#2824](https://github.com/ossrs/srs/issues/2824): Support config in_docker to fix the detect fail. (#2824). v4.0.216
* v4.0, 2021-12-31, For [#2728](https://github.com/ossrs/srs/issues/2728): Refine error log for rtmp2rtc. (#2728). v4.0.215
* v4.0, 2021-12-29, Merge [#2770](https://github.com/ossrs/srs/pull/2770), [#2820](https://github.com/ossrs/srs/pull/2820): Bugs fixed. (#2770)(#2820). v4.0.214
* v4.0, 2021-12-27, Fix [#2811](https://github.com/ossrs/srs/issues/2811): Fix ulimit issue by detecting epoll on Ubuntu. (#2811). v4.0.213
* v4.0, 2021-12-26, Fix [#2247](https://github.com/ossrs/srs/pull/2247): Cleanup server for GMC, by WaitGroup to destroy. (#2247). v4.0.212
* v4.0, 2021-12-25, For [#2809](https://github.com/ossrs/srs/issues/2809), HTTP: Fix 2GB+ mp4/flv file downloading error. (#2809)(#2780)(#2781). v4.0.211
* v4.0, 2021-12-23, For [#2800](https://github.com/ossrs/srs/issues/2800), Fix bug for large mp4(5G+) offset. (#2800). v4.0.210
* v4.0, 2021-12-23, For [#2807](https://github.com/ossrs/srs/issues/2807), Fix bug for HLS log printing. (#2807). v4.0.209
* v4.0, 2021-12-23, For [#2711](https://github.com/ossrs/srs/pull/2711), Refine the default config file. (#2711). v4.0.208
* v4.0, 2021-12-20, Merge [#2784](https://github.com/ossrs/srs/pull/2784): RTC: Support payload name AV1X/AV1. (#2784)(#2760). v4.0.207
* v4.0, 2021-12-07, Merge [#2771](https://github.com/ossrs/srs/pull/2771): RTC: Fix memory leak when replace rtp packet in cache. (#2771). v4.0.205
* v4.0, 2021-12-06, Merge [#2766](https://github.com/ossrs/srs/pull/2766): RTC: Fix nack encode seqnum. (#2766). v4.0.204
* v4.0, 2021-12-04, Merge [#2768](https://github.com/ossrs/srs/pull/2768): RTC: Fix bugs for RTC2RTMP. (#2768). v4.0.203
* v4.0, 2021-12-04, Merge [#2757](https://github.com/ossrs/srs/pull/2757): RTC: Ignore empty audio packet when transcoding (#2757). v4.0.202
* v4.0, 2021-12-01, Fix [#2762](https://github.com/ossrs/srs/pull/2762): RTC: Refine publish security error message (#2762). v4.0.200
* v4.0, 2021-11-25, Merge [#2751](https://github.com/ossrs/srs/pull/2751): RTC: Fix crash when pkt->payload() if pkt is nullptr (#2751). v4.0.199
* v4.0, 2021-11-15, For [#1708](https://github.com/ossrs/srs/pull/1708): ST: Print log when multiple thread stop one coroutine. (#1708). v4.0.198
* v4.0, 2021-11-14, Merge [#2732](https://github.com/ossrs/srs/pull/2732): WebRTC: Fail to publish RTC automatically for HTML5. (#2732). v4.0.197
* v4.0, 2021-11-13, Merge [#2729](https://github.com/ossrs/srs/pull/2729): RTC: check audio track exist when negotiate (#2729). v4.0.196
* v4.0, 2021-11-09, Merge [#2721](https://github.com/ossrs/srs/pull/2721): Rtc2Rtmp: Use RTP timestamp to identify video frames. v4.0.195
* v4.0, 2021-11-07, Merge [#2711](https://github.com/ossrs/srs/pull/2711): Config: Guess config files by [FHS](https://en.wikipedia.org/wiki/Filesystem_Hierarchy_Standard). v4.0.194
* v4.0, 2021-11-07, Merge [#2714](https://github.com/ossrs/srs/pull/2714): DVR: copy req from publish. v4.0.193
* v4.0, 2021-11-04, Merge [#2707](https://github.com/ossrs/srs/pull/2707): Refuse edge request when state is stopping. v4.0.192
* v4.0, 2021-11-02, Auto create package by github actions. v4.0.191
* v4.0, 2021-10-30, Merge [#2552](https://github.com/ossrs/srs/pull/2552): Script: Refine CentOS7 service script to restart SRS. v4.0.190
* v4.0, 2021-10-30, Merge [#2397](https://github.com/ossrs/srs/pull/2397): SRTP: Patch libsrtp2 to fix GCC10 build fail. v4.0.189
* v4.0, 2021-10-30, Merge [#2284](https://github.com/ossrs/srs/pull/2284): Forward: Fast quit when cycle fail. v4.0.188
* v4.0, 2021-10-28, Merge [#2186](https://github.com/ossrs/srs/pull/2186): Gop: Ignore zero timestamp when shrinking. v4.0.187
* v4.0, 2021-10-27, Merge [#1963](https://github.com/ossrs/srs/pull/1963): Cluster: Origin server shouldn't be it's own coworker. v4.0.186
* v4.0, 2021-10-25, Merge [#2692](https://github.com/ossrs/srs/pull/2692): API: Add server_id into http_hooks. v4.0.185
* v4.0, 2021-10-22, Merge [#2687](https://github.com/ossrs/srs/pull/2687): API: Always stat client event if auth fail. v4.0.183
* v4.0, 2021-10-20, Merge [#1758](https://github.com/ossrs/srs/pull/1758): JSON: Support escape special chars. v4.0.182
* v4.0, 2021-10-19, Merge [#1754](https://github.com/ossrs/srs/pull/1754): RTMP: If port is explicity set to 0, use default 1935. v4.0.181
* v4.0, 2021-10-18, Merge [#2670](https://github.com/ossrs/srs/pull/2670): SRT: Solve mpegts demux assert bug. v4.0.180
* v4.0, 2021-10-16, Merge [#2665](https://github.com/ossrs/srs/pull/2665): API: Fix the same 'client_id' error when asynchronous call. v4.0.179
* v4.0, 2021-10-13, Merge [#2671](https://github.com/ossrs/srs/pull/2671): SRT: Pes error when mpegts demux in srt. v4.0.178
* v4.0, 2021-10-12, Merge [#2550](https://github.com/ossrs/srs/pull/2550): API use publish params. v4.0.177
* v4.0, 2021-10-12, Merge [#2549](https://github.com/ossrs/srs/pull/2549): Fix duration issue for HLS on_hls. v4.0.176
* v4.0, 2021-10-11, Fix [#1641](https://github.com/ossrs/srs/issues/1641), HLS/RTC picture corrupt for SPS/PPS lost. v4.0.175
* v4.0, 2021-10-11, RTC: Refine config, aac to rtmp_to_rtc, bframe to keep_bframe. v4.0.174
* v4.0, 2021-10-10, For [#1641](https://github.com/ossrs/srs/issues/1641), Support RTMP publish and play regression test. v4.0.173
* v4.0, 2021-10-10, RTC: Change rtc.aac to discard by default. v4.0.172
* v4.0, 2021-10-10, Fix [#2304](https://github.com/ossrs/srs/issues/2304) Remove Push RTSP feature. v4.0.171
* v4.0, 2021-10-10, Fix [#2653](https://github.com/ossrs/srs/issues/2653) Remove HTTP RAW API. v4.0.170
* v4.0, 2021-10-08, Merge [#2654](https://github.com/ossrs/srs/pull/2654): Parse width and width from SPS/PPS. v4.0.169
* v4.0, 2021-10-08, Default to log to console for docker. v4.0.168
* v4.0, 2021-10-07, Fix bugs #2648, #2415. v4.0.167
* v4.0, 2021-10-03, Support --arch and --cross-prefix for cross compile. 4.0.166
* v4.0, 2021-10-03, Actions: Create source tar file srs-server-4.0.165.tar.gz
* v4.0, 2021-09-23, Merge [#2578](https://github.com/ossrs/srs/pull/2578) Support http callback on_play/stop. 4.0.163
* v4.0, 2021-09-23, Merge [#2618](https://github.com/ossrs/srs/pull/2618) to fix FUA bug.
* v4.0, 2021-09-05, RTC: Merge [#2581](https://github.com/ossrs/srs/pull/2581), Fix listen ipv6 and port. 4.0.161
* v4.0, 2021-09-04, For [#2282](https://github.com/ossrs/srs/pull/2282), [#2181](https://github.com/ossrs/srs/issues/2181), Move DVR async worker from SrsDvrPlan to global.
* v4.0, 2021-09-04, For [#2282](https://github.com/ossrs/srs/pull/2282), [#2181](https://github.com/ossrs/srs/issues/2181), Remove reload for dvr_apply. 4.0.160
* v4.0, 2021-08-28, RTC: Merge [#1859](https://github.com/ossrs/srs/pull/1859), Enhancement: Add param and stream to on_connect. 4.0.159
* v4.0, 2021-08-27, RTC: Merge [#2544](https://github.com/ossrs/srs/pull/2544), Support for multiple SPS/PPS, then pick the first one. 4.0.158
* v4.0, 2021-08-17, RTC: Merge [#2470](https://github.com/ossrs/srs/pull/2470), RTC: Fix rtc to rtmp sync timestamp using sender report. 4.0.157
* v4.0, 2021-08-14, Support Github Actions to publish SRS. 4.0.155
* v4.0, 2021-08-14, RTC: Merge [#2533](https://github.com/ossrs/srs/pull/2533), fix SDP comparison bug. 4.0.154
* v4.0, 2021-08-13, RTC: Merge [#2526](https://github.com/ossrs/srs/pull/2526), fix codec issue for G.711 or H.263. 4.0.152
* v4.0, 2021-08-10, RTC: Merge [#2509](https://github.com/ossrs/srs/pull/2514), support http hooks n_play/stop/publish/unpublish. 4.0.151
* v4.0, 2021-08-07, Merge [#2514](https://github.com/ossrs/srs/pull/2514), Get original client ip instead of proxy ip, for rtc api #2514. 4.0.150
* v4.0, 2021-08-07, Fix [#2508](https://github.com/ossrs/srs/pull/2508), Support features query by API. 4.0.149
* v4.0, 2021-07-25, Fix build failed. 4.0.146
* v4.0, 2021-07-24, Merge [#2373](https://github.com/ossrs/srs/pull/2373), RTC: Fix NACK negotiation bug for Firefox. 4.0.145
* v4.0, 2021-07-24, Merge [#2483](https://github.com/ossrs/srs/pull/2483), RTC: Support statistic for HTTP-API, HTTP-Callback and Security. 4.0.144
* v4.0, 2021-07-21, Merge [#2474](https://github.com/ossrs/srs/pull/2474), SRT: Use thread-safe log for multiple-threading SRT module. 4.0.143
* v4.0, 2021-07-17, Fix bugs and enhance code. 4.0.142
* v4.0, 2021-07-16, Support [CLion and cmake](https://ossrs.net/lts/zh-cn/docs/v4/doc/ide#clion) to build and debug SRS. 4.0.141
* v4.0, 2021-07-08, For [#2403](https://github.com/ossrs/srs/issues/2403), fix padding packets for RTMP2RTC. 4.0.140
* v4.0, 2021-07-04, For [#2424](https://github.com/ossrs/srs/issues/2424), use srandom/random to generate. 4.0.139
* v4.0, 2021-07-01, Merge [#2452](https://github.com/ossrs/srs/pull/2452), fix FFmpeg bug by updating channel_layout. 4.0.138
* v4.0, 2021-06-30, Merge [#2440](https://github.com/ossrs/srs/pull/2440), fix [#2390](https://github.com/ossrs/srs/issues/2390), SRT bug for zerolatency. 4.0.137
* v4.0, 2021-06-28, Merge [#2435](https://github.com/ossrs/srs/pull/2435), fix bug for HTTP-RAW-API to check vhost. 4.0.136
* v4.0, 2021-06-28, Fix [#2431](https://github.com/ossrs/srs/issues/2431), configure FFmpeg bug. 4.0.135 
* v4.0, 2021-06-28, Merge [#2444](https://github.com/ossrs/srs/pull/2444), add libavcodec/crystalhd.c for FFmpeg. 4.0.134
* v4.0, 2021-06-28, Merge [#2438](https://github.com/ossrs/srs/pull/2438), fix losing of last HLS ts file 4.0.133
* v4.0, 2021-06-27, Squash for [#2424](https://github.com/ossrs/srs/issues/2424), query the latest available version. 4.0.132
* v4.0, 2021-06-24, Merge [#2429](https://github.com/ossrs/srs/pull/2429) to fix SRT bug. 4.0.131
* v4.0, 2021-06-21, Fix [#2413](https://github.com/ossrs/srs/issues/2413), fix RTMP to RTC bug 4.0.130
* v4.0, 2021-06-20, Guess where FFmpeg is. 4.0.129
* v4.0, 2021-06-20, Fix [#1685](https://github.com/ossrs/srs/issues/1685), support RTC cross-build for armv7/armv8(aarch64). 4.0.128
* v4.0, 2021-06-17, Fix [#2214](https://github.com/ossrs/srs/issues/2214), remove detection for gmc and gmp.
* v4.0, 2021-06-16, Change [GB28181](https://github.com/ossrs/srs/issues/1500) to [feature/gb28181](https://github.com/ossrs/srs/tree/feature/gb28181). 4.0.127
* v4.0, 2021-06-01, Support --shared-ffmpeg to link with *.so for LGPL license. 4.0.126
* v4.0, 2021-06-01, Support --shared-srt to link with *.so for MPL license. 4.0.125
* v4.0, 2021-05-31, Use [SPDX-License-Identifier: MIT](https://spdx.dev/ids/). 4.0.124
* v4.0, 2021-05-21, Fix [#2370](https://github.com/ossrs/srs/issues/2370) bug for Firefox play stream(published by Chrome). 4.0.121
* v4.0, 2021-05-21, RTC: Refine sdk, migrate from onaddstream to ontrack. 4.0.120
* v4.0, 2021-05-21, Tools: Refine configure options. 4.0.119
* v4.0, 2021-05-20, Fix build fail when disable RTC by --rtc=off. 4.0.118
* v4.0, 2021-05-15, SRT: Build SRT from source by SRS. 4.0.115
* v4.0, 2021-05-15, Rename SrsConsumer* to SrsLiveConsumer*. 4.0.114
* v4.0, 2021-05-15, Rename SrsRtcStream* to SrsRtcSource*. 4.0.113
* v4.0, 2021-05-15, Rename SrsSource* to SrsLiveSource*. 4.0.112
* v4.0, 2021-05-15, Rename SrsRtpPacket2 to SrsRtpPacket. 4.0.111
* v4.0, 2021-05-14, RTC: Remove [Object Cache Pool](https://github.com/ossrs/srs/commit/14bfc98122bba369572417c19ebb2a61b373fc45#commitcomment-47655008), no effect. 4.0.110
* v4.0, 2021-05-14, Change virtual public to public. 4.0.109
* v4.0, 2021-05-14, Refine id and vid for statistic. 4.0.108
* v4.0, 2021-05-09, Refine tid for sdk and demos. 4.0.106
* v4.0, 2021-05-08, Refine shared fast timer. 4.0.105
* v4.0, 2021-05-08, Refine global or thread-local variables initialize. 4.0.104
* v4.0, 2021-05-07, RTC: Support circuit breaker. 4.0.103
* v4.0, 2021-05-07, RTC: Refine play stream find track. 4.0.102
* v4.0, 2021-05-07, RTC: Refine FastTimer to fixed interval. 4.0.101
* v4.0, 2021-05-06, RTC: Fix config bug for nack and twcc. 4.0.99
* v4.0, 2021-05-04, Add video room demo. 4.0.98
* v4.0, 2021-05-03, Add RTC stream merging demo by FFmpeg. 4.0.97
* v4.0, 2021-05-02, Add one to one demo. 4.0.96
* v4.0, 2021-04-20, Support RTC2RTMP bridger and shared FastTimer. 4.0.95
* v4.0, 2021-04-20, Refine transcoder to support aac2opus and opus2aac. 4.0.94
* v4.0, 2021-05-01, Timer: Extract and apply shared FastTimer. 4.0.93
* v4.0, 2021-04-29, RTC: Support AV1 for Chrome M90. 4.0.91
* v4.0, 2021-04-24, Change push-RTSP as deprecated feature.
* v4.0, 2021-04-24, Player: Change the default from RTMP to HTTP-FLV.
* v4.0, 2021-04-24, Disable CherryPy by --cherrypy=off. 4.0.90
* v4.0, 2021-04-01, RTC: Refine TWCC and SDP exchange. 4.0.88
* v4.0, 2021-03-24, RTC: Support WebRTC re-publish stream. 4.0.87
* v4.0, 2021-03-24, RTC: Use fast parse TWCC-ID, ignore in packet parsing. 4.0.86
* v4.0, 2021-03-09, DTLS: Fix ARQ bug, use openssl timeout. 4.0.84
* v4.0, 2021-03-08, DTLS: Fix dead loop by duplicated Alert message. 4.0.83
* v4.0, 2021-03-08, Fix bug when client DTLS is passive. 4.0.82
* v4.0, 2021-03-03, Fix [#2106](https://github.com/ossrs/srs/issues/2106), [#2011](https://github.com/ossrs/srs/issues/2011), RTMP/AAC transcode to Opus bug. 4.0.81
* v4.0, 2021-03-02, Refine build script for FFmpeg and SRTP. 4.0.80
* v4.0, 2021-03-02, Upgrade libsrtp from 2.0.0 to 2.3.0, with source code. 4.0.79
* v4.0, 2021-03-01, Upgrade openssl from 1.1.0e to 1.1.1b, with source code. 4.0.78
* v4.0, 2021-03-01, Enable Object Cache and Zero Copy Nack by default. 4.0.77
* v4.0, 2021-02-28, RTC: Support high performance [Zero Copy NACK](https://github.com/ossrs/srs/commit/36ea67359e55c94ab044cee4b6a4ec901a83a287#commitcomment-47654868). 4.0.76
* v4.0, 2021-02-27, RTC: Support [Object Cache Pool](https://github.com/ossrs/srs/commit/14bfc98122bba369572417c19ebb2a61b373fc45#commitcomment-47655008) for performance. 4.0.75
* v4.0, 2021-02-12, RTC: Support [High Resolution(about 25ms) Timer](https://github.com/ossrs/srs/commit/c5d2027f9af77fc2d34a6b6ca941c0f0fbdd10c4#commitcomment-47655747). 4.0.72
* v4.0, 2021-02-10, RTC: [Improve performance about 700+](https://github.com/ossrs/srs/commit/b431ad738c39f34a5a0a39e81beb7854223db761#commitcomment-47655935) streams. 4.0.71
* v4.0, 2021-02-04, At least wait 1ms when <1ms, to avoid epoll_wait spin loop. 4.0.66
* v4.0, 2021-01-31, Enable -std=c++11 by default. 4.0.65
* v4.0, 2021-01-25, Enable --nasm and --srtp-asm by default for performance. 4.0.64
* v4.0, 2021-01-20, Support HTTP-FLV and HLS for srs-player by H5. 4.0.63
* v4.0, 2021-01-08, HTML5 video tag resolution adaptive. 4.0.59
* v4.0, 2021-01-08, Fix memory leak and bugs for RTC. 4.0.58
* v4.0, 2021-01-06, Merge #2109, Refine srs_string_split.
* v4.0, 2020-12-24, Support disable CherryPy. 4.0.57
* v4.0, 2020-11-12, For [#1998](https://github.com/ossrs/srs/issues/1998), Support Firefox, use PT in offer. 4.0.55
* v4.0, 2020-11-11, For [#1508](https://github.com/ossrs/srs/issues/1508), Transform http header name to upper camel case. 4.0.54
* v4.0, 2020-11-06, For [#1657](https://github.com/ossrs/srs/issues/1657), Read cached data first in SSL. 4.0.48
* v4.0, 2020-11-06, For [#1657](https://github.com/ossrs/srs/issues/1657#issuecomment-722971676), support HTTPS Streaming(HTTPS-FLV, etc). 4.0.47
* v4.0, 2020-11-06, For [#1657](https://github.com/ossrs/srs/issues/1657#issuecomment-722904004), support HTTPS API. 4.0.46
* v4.0, 2020-11-03, For [#1657](https://github.com/ossrs/srs/issues/1657#issuecomment-720889906), support HTTPS client, for http-callback. 4.0.45
* v4.0, 2020-10-31, Support gdb/srs.py to stat coroutines. 4.0.44
* v4.0, 2020-09-19, RTC: Extract resource manager. Use any UDP packet to keep alive. 4.0.43
* v4.0, 2020-09-09, RTC: Refine NACK RTT and efficiency. 4.0.42
* v4.0, 2020-09-08, Refine PLI/NACK/DTLS logs. 4.0.41
* v4.0, 2020-08-30, Fix serval bugs for RTC. Refine context API. 4.0.40
* v4.0, 2020-08-18, RTC: DTLS support ARQ, covered with utest. 4.0.39
* v4.0, 2020-08-06, RTC: Refine error check. 4.0.37
* v4.0, 2020-07-25, RTC: Support multiple address for client. 4.0.36
* v4.0, 2020-07-11, Refine log context with random string. 4.0.35
* v4.0, 2020-07-04, Fix some bugs for RTC. 4.0.34
* v4.0, 2020-06-24, Support static link c++ libraries. 4.0.32
* v4.0, 2020-06-23, Change log cid from int to string. 4.0.31
* v4.0, 2020-06-03, Support enable C++11. 4.0.29
* v4.0, 2020-05-31, Remove [srs-librtmp](https://github.com/ossrs/srs/issues/1535#issuecomment-633907655). 4.0.28
* v4.0, 2020-05-21, For [#307](https://github.com/ossrs/srs/issues/307), disable GSO and sendmmsg. 4.0.27
* v4.0, 2020-05-14, For [#307](https://github.com/ossrs/srs/issues/307), refine core structure, RTMP base on frame, RTC base on RTP. 4.0.26
* v4.0, 2020-05-11, For [#307](https://github.com/ossrs/srs/issues/307), refine RTC publisher structure. 4.0.25
* v4.0, 2020-04-30, For [#307](https://github.com/ossrs/srs/issues/307), support publish RTC with passing opus. 4.0.24
* v4.0, 2020-04-14, For [#307](https://github.com/ossrs/srs/issues/307), support sendmmsg, GSO and reuseport. 4.0.23
* v4.0, 2020-04-05, For [#307](https://github.com/ossrs/srs/issues/307), SRTP ASM only works with openssl-1.0, auto detect it. 4.0.22
* v4.0, 2020-04-04, For [#307](https://github.com/ossrs/srs/issues/307), refine RTC latency from 600ms to 200ms. 4.0.20
* v4.0, 2020-04-03, For [#307](https://github.com/ossrs/srs/issues/307), build SRTP with openssl to improve performance. 4.0.19
* v4.0, 2020-03-31, Play stream by WebRTC on iOS/Android/PC browser. 4.0.17
* v4.0, 2020-03-28, Support multiple OS/Platform build cache. 4.0.16
* v4.0, 2020-03-28, For [#1250](https://github.com/ossrs/srs/issues/1250), support macOS, OSX, MacbookPro, Apple Darwin.
* v4.0, 2020-03-22, Welcome maintainers [Runner365](https://github.com/runner365), [John](https://github.com/xiaozhihong) and [B.P.Y(Bepartofyou)](https://github.com/Bepartofyou). 4.0.15
* v4.0, 2020-03-22, For [#307](https://github.com/ossrs/srs/issues/307), support play with WebRTC. 4.0.14
* v4.0, 2020-03-13, For [#1636](https://github.com/ossrs/srs/issues/1636), fix bug for mux AAC to ADTS, never overwrite by RTMP sampling rate. 4.0.13
* v4.0, 2020-03-07, For [#1612](https://github.com/ossrs/srs/issues/1612), fix crash bug for RTSP. 4.0.12
* v4.0, 2020-03-07, For [#1631](https://github.com/ossrs/srs/issues/1631), support sei_filter for SRT. 4.0.11
* v4.0, 2020-03-01, For [#1621](https://github.com/ossrs/srs/issues/1621), support mix_correct for aggregate aac for SRT. 4.0.10
* v4.0, 2020-02-25, For [#1615](https://github.com/ossrs/srs/issues/1615), support default app(live) for vmix SRT. 4.0.9
* v4.0, 2020-02-21, For [#1598](https://github.com/ossrs/srs/issues/1598), support SLB health checking by TCP. 4.0.8
* v4.0, 2020-02-19, For [#1579](https://github.com/ossrs/srs/issues/1579), support rolling update of k8s. 4.0.7
* v4.0, 2020-02-18, For [#1579](https://github.com/ossrs/srs/issues/1579), support start/final wait for gracefully quit. 4.0.6
* v4.0, 2020-02-18, For [#1579](https://github.com/ossrs/srs/issues/1579), support gracefully quit and force to. 4.0.5
* v4.0, 2020-02-13, SRT supports detail config for [DynamicEncoding](https://github.com/runner365/srt_encoder). 4.0.4
* v4.0, 2020-02-04, Update project code. 4.0.3
* v4.0, 2020-01-26, Allow use libsrt.so for SRT is MPL license. 4.0.2
* v4.0, 2020-01-24, Fix [#1147](https://github.com/ossrs/srs/issues/1147), support SRT(Secure Reliable Transport). 4.0.1

<a name="v3-changes"></a>

## SRS 3.0 Changelog
* v3.0, 2021-05-12, Fix [#2311](https://github.com/ossrs/srs/issues/2311), Copy the request for stat client. 3.0.162
* <strong>v3.0, 2021-04-28, [3.0 release5(3.0.161)](https://github.com/ossrs/srs/releases/tag/v3.0-r5) released. 122750 lines.</strong>
* v3.0, 2021-04-28, Upgrade players. 3.0.161
* <strong>v3.0, 2021-04-24, [3.0 release4(3.0.160)](https://github.com/ossrs/srs/releases/tag/v3.0-r4) released. 122750 lines.</strong>
* v3.0, 2021-04-24, Package players and console to zip and docker. 3.0.160
* v3.0, 2021-04-24, Add srs-console to research/console. 3.0.159
* v3.0, 2021-03-05, Refine usage to docker by default. 3.0.158
* v3.0, 2021-01-07, Change id from int to string for the statistics. 3.0.157
* <strong>v3.0, 2021-01-02, [3.0 release3(3.0.156)](https://github.com/ossrs/srs/releases/tag/v3.0-r3) released. 122736 lines.</strong>
* v3.0, 2020-12-26, For RTMP edge/forward, pass vhost in tcUrl, not in stream. 3.0.156
* v3.0, 2020-12-17, Fix [#1694](https://github.com/ossrs/srs/issues/1694), Support DVR 2GB+ MP4 file. 3.0.155
* v3.0, 2020-12-17, Fix [#1548](https://github.com/ossrs/srs/issues/1548), Add edts in MP4 for Windows10. 3.0.154
* <strong>v3.0, 2020-10-31, [3.0 release2(3.0.153)](https://github.com/ossrs/srs/releases/tag/v3.0-r2) released. 122663 lines.</strong>
* v3.0, 2020-10-31, Fix [#509](https://github.com/ossrs/srs/issues/509), Always malloc stack on heap. 3.0.153
* v3.0, 2020-10-31, Remove some global elements for debugging. 3.0.152
* v3.0, 2020-10-31, Use global _srs_server for debugging. 3.0.151
* v3.0, 2020-10-31, Refine source cid, track previous one. 3.0.150
* v3.0, 2020-10-25, Add hls.realtime.conf for low-latency HLS. 3.0.149
* v3.0, 2020-10-24, Refine script and startup logs. 3.0.148
* v3.0, 2020-10-23, Allow FFmpeg if exists at /usr/local/bin/ffmpeg. 3.0.147
* v3.0, 2020-10-23, Refine build script, use libssl in docker. 3.0.146
* v3.0, 2020-10-14, Fix [#1987](https://github.com/ossrs/srs/issues/1987), Fix Kbps resample bug. 3.0.145
* <strong>v3.0, 2020-10-10, [3.0 release1(3.0.144)](https://github.com/ossrs/srs/releases/tag/v3.0-r1) released. 122674 lines.</strong>
* v3.0, 2020-10-10, Fix [#1780](https://github.com/ossrs/srs/issues/1780), build fail on Ubuntu20(focal). 3.0.144
* v3.0, 2020-09-14, Prevent stop ingest for multiple times. 3.0.143
* v3.0, 2020-09-10, RTC: Change SO_REUSEPORT fail to warning. 3.0.142
* <strong>v3.0, 2020-06-27, [3.0 release0(3.0.141)](https://github.com/ossrs/srs/releases/tag/v3.0-r0) released. 122674 lines.</strong>
* v3.0, 2020-03-30, For [#1672](https://github.com/ossrs/srs/issues/1672), fix dvr close file failed bug. 3.0.140
* <strong>v3.0, 2020-03-29, [3.0 beta4(3.0.139)](https://github.com/ossrs/srs/releases/tag/v3.0-b4) released. 122674 lines.</strong>
* v3.0, 2020-03-28, Support multiple OS/Platform build cache. 3.0.139
* v3.0, 2020-03-28, For [#1250](https://github.com/ossrs/srs/issues/1250), support macOS, OSX, MacbookPro, Apple Darwin. 3.0.138
* v3.0, 2020-03-21, For [#1629](https://github.com/ossrs/srs/issues/1629), fix kickoff FLV client bug. 3.0.137
* v3.0, 2020-03-21, For [#1619](https://github.com/ossrs/srs/issues/1619), configure without utest by default. 3.0.136
* v3.0, 2020-03-21, For [#1651](https://github.com/ossrs/srs/issues/1651), fix return pnwrite of srs_write_large_iovs. 3.0.135
* <strong>v3.0, 2020-03-18, [3.0 beta3(3.0.134)](https://github.com/ossrs/srs/releases/tag/v3.0-b3) released. 122509 lines.</strong>
* v3.0, 2020-03-12, For [#1635](https://github.com/ossrs/srs/issues/1635), inotify watch ConfigMap for reload. 3.0.134
* v3.0, 2020-03-12, For [#1635](https://github.com/ossrs/srs/issues/1635), support auto reaload config by inotify. 3.0.129
* v3.0, 2020-03-12, For [#1630](https://github.com/ossrs/srs/issues/1630), disable cache for stream changing, and drop dup header. 3.0.128
* v3.0, 2020-03-12, For [#1594](https://github.com/ossrs/srs/issues/1594), detect and disable daemon for docker. 3.0.127
* v3.0, 2020-03-12, For [#1634](https://github.com/ossrs/srs/issues/1634), always check status in thread loop. 3.0.126
* v3.0, 2020-03-11, For [#1634](https://github.com/ossrs/srs/issues/1634), refactor output with datetime for ingest/encoder/exec. 3.0.125
* v3.0, 2020-03-11, For [#1634](https://github.com/ossrs/srs/issues/1634), fix quit by accident SIGTERM while killing FFMPEG. 3.0.124
* <strong>v3.0, 2020-03-05, [3.0 beta2(3.0.123)](https://github.com/ossrs/srs/releases/tag/v3.0-b2) released. 122170 lines.</strong>
* v3.0, 2020-02-21, For [#1598](https://github.com/ossrs/srs/issues/1598), support SLB health checking by TCP. 3.0.123
* v3.0, 2020-02-21, Fix bug for librtmp client ipv4/ipv6 socket. 3.0.122
* v3.0, 2020-02-18, For [#1579](https://github.com/ossrs/srs/issues/1579), support start/final wait for gracefully quit. 3.0.121
* v3.0, 2020-02-18, For [#1579](https://github.com/ossrs/srs/issues/1579), support force gracefully quit. 3.0.120
* v3.0, 2020-02-18, For [#1579](https://github.com/ossrs/srs/issues/1579), support gracefully quit. 3.0.119
* v3.0, 2020-02-17, For [#1601](https://github.com/ossrs/srs/issues/1601), flush async on_dvr/on_hls events before stop. 3.0.118
* <strong>v3.0, 2020-02-14, [3.0 beta1(3.0.117)](https://github.com/ossrs/srs/releases/tag/v3.0-b1) released. 121964 lines.</strong>
* v3.0, 2020-02-14, For [#1595](https://github.com/ossrs/srs/issues/1595), migrating streaming from ossrs.net to r.ossrs.net. 3.0.117
* v3.0, 2020-02-05, For [#665](https://github.com/ossrs/srs/issues/665), fix HTTP-FLV reloading bug. 3.0.116
* v3.0, 2020-02-05, For [#1592](https://github.com/ossrs/srs/issues/1592), fix terminal echo off by redirect process stdin. 3.0.115
* v3.0, 2020-02-04, For [#1186](https://github.com/ossrs/srs/issues/1186), refactor security check. 3.0.114
* v3.0, 2020-02-04, Fix [#939](https://github.com/ossrs/srs/issues/939), response right A/V flag in FLV header. 3.0.113
* v3.0, 2020-02-04, For [#939](https://github.com/ossrs/srs/issues/939), always enable fast FLV streaming.
* <strong>v3.0, 2020-02-02, [3.0 beta0(3.0.112)](https://github.com/ossrs/srs/releases/tag/v3.0-b0) released. 121709 lines.</strong>
* v3.0, 2020-01-29, Support isolate version file. 3.0.112
* v3.0, 2020-01-29, Fix [#1206](https://github.com/ossrs/srs/issues/1206), dispose ingester while server quiting. 3.0.111
* v3.0, 2020-01-28, Fix [#1230](https://github.com/ossrs/srs/issues/1230), racing condition in source fetch or create. 3.0.110
* v3.0, 2020-01-27, Fix [#1303](https://github.com/ossrs/srs/issues/1303), do not dispatch previous meta when not publishing. 3.0.109
* v3.0, 2020-01-26, Allow use libst.so for ST is MPL license.
* v3.0, 2020-01-26, Fix [#607](https://github.com/ossrs/srs/issues/607), set RTMP identifying recursive depth to 3.
* v3.0, 2020-01-25, Fix [#878](https://github.com/ossrs/srs/issues/878), remove deprecated #EXT-X-ALLOW-CACHE for HLS. 3.0.108
* v3.0, 2020-01-25, Fix [#703](https://github.com/ossrs/srs/issues/703), drop video data util sps/pps. 3.0.107
* v3.0, 2020-01-25, Fix [#1108](https://github.com/ossrs/srs/issues/1108), reap DVR tmp file when unpublish. 3.0.106
* <strong>v3.0, 2020-01-21, [3.0 alpha9(3.0.105)](https://github.com/ossrs/srs/releases/tag/v3.0-a9) released. 121577 lines.</strong>
* v3.0, 2020-01-21, Fix [#1221](https://github.com/ossrs/srs/issues/1221), remove complex configure options. 3.0.104
* v3.0, 2020-01-21, Fix [#1547](https://github.com/ossrs/srs/issues/1547), support crossbuild for ARM/MIPS.
* v3.0, 2020-01-21, For [#1547](https://github.com/ossrs/srs/issues/1547), support setting cc/cxx/ar/ld/randlib tools. 3.0.103
* v3.0, 2020-01-19, For [#1580](https://github.com/ossrs/srs/issues/1580), fix cid range problem. 3.0.102
* v3.0, 2020-01-19, For [#1070](https://github.com/ossrs/srs/issues/1070), define FLV CodecID for [AV1](https://github.com/ossrs/srs/issues/1070) and [opus](https://github.com/ossrs/srs/issues/307). 3.0.101
* v3.0, 2020-01-16, For [#1575](https://github.com/ossrs/srs/issues/1575), correct RTMP redirect as tcUrl, add redirect2 as RTMP URL. 3.0.100
* v3.0, 2020-01-15, For [#1509](https://github.com/ossrs/srs/issues/1509), decrease the fast vector init size from 64KB to 64B. 3.0.99
* v3.0, 2020-01-15, For [#1509](https://github.com/ossrs/srs/issues/1509), release coroutine when source is idle. 3.0.98
* <strong>v3.0, 2020-01-10, [3.0 alpha8(3.0.97)](https://github.com/ossrs/srs/releases/tag/v3.0-a8) released. 121555 lines.</strong>
* v3.0, 2020-01-09, For [#1042](https://github.com/ossrs/srs/issues/1042), improve test coverage for service. 3.0.97
* v3.0, 2020-01-08, Merge [#1554](https://github.com/ossrs/srs/issues/1554), support logrotate copytruncate. 3.0.96
* v3.0, 2020-01-05, Always use string instance to avoid crash risk. 3.0.95
* v3.0, 2020-01-05, For [#460](https://github.com/ossrs/srs/issues/460), fix ipv6 hostport parsing bug. 3.0.94
* v3.0, 2020-01-05, For [#460](https://github.com/ossrs/srs/issues/460), fix ipv6 intranet address filter bug. 3.0.93
* v3.0, 2020-01-05, For [#1543](https://github.com/ossrs/srs/issues/1543), use getpeername to retrieve client ip. 3.0.92
* v3.0, 2020-01-02, For [#1042](https://github.com/ossrs/srs/issues/1042), improve test coverage for config. 3.0.91
* v3.0, 2019-12-30, Fix mp4 security issue, check buffer when required size is variable.
* <strong>v3.0, 2019-12-29, [3.0 alpha7(3.0.90)](https://github.com/ossrs/srs/releases/tag/v3.0-a7) released. 116356 lines.</strong>
* v3.0, 2019-12-29, For [#1255](https://github.com/ossrs/srs/issues/1255), support vhost/domain in query string for HTTP streaming. 3.0.90
* v3.0, 2019-12-29, For [#299](https://github.com/ossrs/srs/issues/299), increase dash segment size for avsync issue. 3.0.89
* v3.0, 2019-12-27, For [#299](https://github.com/ossrs/srs/issues/299), fix some bugs in dash, it works now. 3.0.88
* v3.0, 2019-12-27, For [#1544](https://github.com/ossrs/srs/issues/1544), fix memory leaking for complex error. 3.0.87
* v3.0, 2019-12-27, Add links for flv.js, hls.js and dash.js.
* v3.0, 2019-12-26, For [#1105](https://github.com/ossrs/srs/issues/1105), http server support mp4 range.
* v3.0, 2019-12-26, For [#1105](https://github.com/ossrs/srs/issues/1105), dvr mp4 supports playing on Chrome/Safari/Firefox. 3.0.86
* <strong>v3.0, 2019-12-26, [3.0 alpha6(3.0.85)](https://github.com/ossrs/srs/releases/tag/v3.0-a6) released. 116056 lines.</strong>
* v3.0, 2019-12-26, For [#1488](https://github.com/ossrs/srs/issues/1488), pass client ip to http callback. 3.0.85
* v3.0, 2019-12-25, For [#1537](https://github.com/ossrs/srs/issues/1537), [#1282](https://github.com/ossrs/srs/issues/1282), support aarch64 for armv8. 3.0.84
* v3.0, 2019-12-25, For [#1538](https://github.com/ossrs/srs/issues/1538), fresh chunk allow fmt=0 or fmt=1. 3.0.83
* v3.0, 2019-12-25, Remove FFMPEG and NGINX, please use [srs-docker](https://github.com/ossrs/srs-docker) instead. 3.0.82
* v3.0, 2019-12-25, For [#1537](https://github.com/ossrs/srs/issues/1537), remove cross-build, not used patches, directly build st.
* v3.0, 2019-12-24, For [#1508](https://github.com/ossrs/srs/issues/1508), support chunk length and content in multiple parts.
* v3.0, 2019-12-23, Merge SRS2 for running srs-librtmp on Windows. 3.0.80
* v3.0, 2019-12-23, For [#1535](https://github.com/ossrs/srs/issues/1535), deprecate Adobe FMS/AMS edge token traversing([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/drm#tokentraverse), [EN](https://ossrs.io/lts/en-us/docs/v4/doc/drm#tokentraverse)) authentication. 3.0.79
* v3.0, 2019-12-23, For [#1535](https://github.com/ossrs/srs/issues/1535), deprecate BWT(bandwidth testing). 3.0.78
* v3.0, 2019-12-23, For [#1535](https://github.com/ossrs/srs/issues/1535), deprecate Adobe HDS(f4m)([CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/delivery-hds), [EN](https://ossrs.io/lts/en-us/docs/v4/doc/delivery-hds)). 3.0.77
* v3.0, 2019-12-20, Fix [#1508](https://github.com/ossrs/srs/issues/1508), http-client support read chunked response. 3.0.76
* v3.0, 2019-12-20, For [#1508](https://github.com/ossrs/srs/issues/1508), refactor srs_is_digital, support all zeros.
* <strong>v3.0, 2019-12-19, [3.0 alpha5(3.0.75)](https://github.com/ossrs/srs/releases/tag/v3.0-a5) released. 115362 lines.</strong>
* v3.0, 2019-12-19, Refine the RTMP iovs cache increasing to much faster.
* v3.0, 2019-12-19, Fix [#1524](https://github.com/ossrs/srs/issues/1524), memory leak for amf0 strict array. 3.0.75
* v3.0, 2019-12-19, Fix random build failed bug for modules.
* v3.0, 2019-12-19, Fix [#1520](https://github.com/ossrs/srs/issues/1520) and [#1223](https://github.com/ossrs/srs/issues/1223), bug for origin cluster 3+ servers. 3.0.74
* v3.0, 2019-12-18, For [#1042](https://github.com/ossrs/srs/issues/1042), add test for RAW AVC protocol.
* v3.0, 2019-12-18, Detect whether flash enabled for srs-player. 3.0.73
* v3.0, 2019-12-17, Fix HTTP CORS bug when sending response for OPTIONS. 3.0.72
* v3.0, 2019-12-17, Enhance HTTP response write for final_request.
* v3.0, 2019-12-17, Refactor HTTP stream to disconnect client when unpublish.
* v3.0, 2019-12-17, Fix HTTP-FLV and VOD-FLV conflicting bug.
* v3.0, 2019-12-17, Refactor HttpResponseWriter.write, default to single text mode.
* v3.0, 2019-12-16, For [#1042](https://github.com/ossrs/srs/issues/1042), add test for HTTP protocol.
* <strong>v3.0, 2019-12-13, [3.0 alpha4(3.0.71)](https://github.com/ossrs/srs/releases/tag/v3.0-a4) released. 112928 lines.</strong>
* v3.0, 2019-12-12, For [#547](https://github.com/ossrs/srs/issues/547), [#1506](https://github.com/ossrs/srs/issues/1506), default hls_dts_directly to on. 3.0.71
* v3.0, 2019-12-12, SrsPacket supports converting to message, so can be sent by one API.
* v3.0, 2019-12-11, For [#1042](https://github.com/ossrs/srs/issues/1042), cover RTMP client/server protocol.
* v3.0, 2019-12-11, Fix [#1445](https://github.com/ossrs/srs/issues/1445), limit the createStream recursive depth. 3.0.70
* v3.0, 2019-12-11, For [#1042](https://github.com/ossrs/srs/issues/1042), cover RTMP handshake protocol.
* v3.0, 2019-12-11, Fix [#1229](https://github.com/ossrs/srs/issues/1229), fix the security risk in logger. 3.0.69
* v3.0, 2019-12-11, For [#1229](https://github.com/ossrs/srs/issues/1229), fix the security risk in HDS. 3.0.69
* v3.0, 2019-12-05, Fix [#1506](https://github.com/ossrs/srs/issues/1506), support directly turn FLV timestamp to TS DTS. 3.0.68
* <strong>v3.0, 2019-11-30, [3.0 alpha3(3.0.67)](https://github.com/ossrs/srs/releases/tag/v3.0-a3) released. 110864 lines.</strong>
* v3.0, 2019-12-01, Fix [#1501](https://github.com/ossrs/srs/issues/1501), use request coworker for origin cluster. 3.0.67
* <strong>v3.0, 2019-11-30, [3.0 alpha2(3.0.66)](https://github.com/ossrs/srs/releases/tag/v3.0-a2) released. 110831 lines.</strong>
* v3.0, 2019-11-30, Fix [#1501](https://github.com/ossrs/srs/issues/1501), use request coworker for origin cluster. 3.0.66
* v3.0, 2019-11-30, Random tid for docker. 3.0.65
* v3.0, 2019-11-30, Refine debug info for edge. 3.0.64
* v3.0, 2019-10-30, Cover protocol stack RTMP. 3.0.63
* v3.0, 2019-10-23, Cover JSON codec. 3.0.62
* v3.0, 2019-10-13, Use http://ossrs.net as homepage.
* v3.0, 2019-10-10, Cover AMF0 codec. 3.0.61
* <strong>v3.0, 2019-10-07, [3.0 alpha1(3.0.60)](https://github.com/ossrs/srs/releases/tag/v3.0-a1) released. 107962 lines.</strong>
* v3.0, 2019-10-06, Support log rotate by init.d command. 3.0.60
* v3.0, 2019-10-06, We prefer ipv4, only use ipv6 if ipv4 is disabled. 3.0.59
* v3.0, 2019-10-05, Support systemctl service for CentOS7. 3.0.58
* v3.0, 2019-10-04, Disable SO_REUSEPORT if not supported. 3.0.57
* <strong>v3.0, 2019-10-04, [3.0 alpha0(3.0.56)](https://github.com/ossrs/srs/releases/tag/v3.0-a0) released. 107946 lines.</strong>
* v3.0, 2019-10-04, Support go-oryx rtmplb with [proxy protocol](https://github.com/ossrs/go-oryx/wiki/RtmpProxy). 3.0.56
* v3.0, 2019-10-03, Fix [#775](https://github.com/ossrs/srs/issues/775), Support SO_REUSEPORT to improve edge performance. 3.0.54
* v3.0, 2019-10-03, For [#467](https://github.com/ossrs/srs/issues/467), Remove KAFKA producer. 3.0.53
* v3.0, 2019-05-14, Covert Kernel File reader/writer. 3.0.52
* v3.0, 2019-04-30, Refine typo in files. 3.0.51
* v3.0, 2019-04-25, Upgrade http-parser from 2.1 to 2.9.2 and cover it. 3.0.50
* v3.0, 2019-04-22, Refine in time unit. 3.0.49
* v3.0, 2019-04-07, Cover ST Coroutine and time unit. 3.0.48
* v3.0, 2019-04-06, Merge [#1304](https://github.com/ossrs/srs/issues/1304), Fix ST coroutine pull error. 3.0.47
* v3.0, 2019-04-05, Merge [#1339](https://github.com/ossrs/srs/issues/1339), Support HTTP-FLV params. 3.0.46
* v3.0, 2018-11-11, Merge [#1261](https://github.com/ossrs/srs/issues/1261), Support `_definst_` for Wowza. 3.0.44
* v3.0, 2018-08-26, SRS [console](https://github.com/ossrs/srs-ngb) support both [Chinese](http://ossrs.net:1985/console/ng_index.html) and [English](http://ossrs.net:1985/console/en_index.html).
* v3.0, 2018-08-25, Fix [#1093](https://github.com/ossrs/srs/issues/1093), Support HLS encryption. 3.0.42
* v3.0, 2018-08-25, Fix [#1051](https://github.com/ossrs/srs/issues/1051), Drop ts when republishing stream. 3.0.41
* v3.0, 2018-08-12, For [#1202](https://github.com/ossrs/srs/issues/1202), Support edge/forward to Aliyun CDN. 3.0.40
* v3.0, 2018-08-11, For [#910](https://github.com/ossrs/srs/issues/910), Support HTTP FLV with HTTP callback. 3.0.39
* v3.0, 2018-08-05, Refine HTTP-FLV latency, support realtime mode.3.0.38
* v3.0, 2018-08-05, Fix [#1087](https://github.com/ossrs/srs/issues/1087), Ignore iface without address. 3.0.37
* v3.0, 2018-08-04, For [#1110](https://github.com/ossrs/srs/issues/1110), Support params in http callback. 3.0.36
* v3.0, 2018-08-02, Always use vhost in stream query, the unify uri. 3.0.35
* v3.0, 2018-08-02, For [#1031](https://github.com/ossrs/srs/issues/1031), SRS edge support douyu.com. 3.0.34
* v3.0, 2018-07-22, Replace hex to string to match MIT license. 3.0.33
* v3.0, 2018-07-22, Replace base64 to match MIT license. 3.0.32
* v3.0, 2018-07-22, Replace crc32 IEEE and MPEG by pycrc to match MIT license. 3.0.31
* v3.0, 2018-07-21, Replace crc32 IEEE by golang to match MIT license. 3.0.30
* v3.0, 2018-02-16, Fix [#464](https://github.com/ossrs/srs/issues/464), support RTMP origin cluster. 3.0.29
* v3.0, 2018-02-13, Fix [#1057](https://github.com/ossrs/srs/issues/1057), switch to simple handshake. 3.0.28
* v3.0, 2018-02-13, Fix [#1059](https://github.com/ossrs/srs/issues/1059), merge from 2.0, supports url with vhost in stream. 3.0.27
* v3.0, 2018-01-01, Fix [#913](https://github.com/ossrs/srs/issues/913), support complex error. 3.0.26
* v3.0, 2017-06-04, Fix [#299](https://github.com/ossrs/srs/issues/299), support experimental MPEG-DASH. 3.0.25
* v3.0, 2017-05-30, Fix [#821](https://github.com/ossrs/srs/issues/821), support MP4 file parser. 3.0.24
* v3.0, 2017-05-30, Fix [#904](https://github.com/ossrs/srs/issues/904), replace NXJSON(LGPL) with json-parser(BSD). 3.0.23
* v3.0, 2017-04-16, Fix [#547](https://github.com/ossrs/srs/issues/547), support HLS audio in TS. 3.0.22
* v3.0, 2017-03-26, Fix [#820](https://github.com/ossrs/srs/issues/820), extract service for modules. 3.0.21
* v3.0, 2017-03-02, Fix [#786](https://github.com/ossrs/srs/issues/786), simply don't reuse object. 3.0.20
* v3.0, 2017-03-01, For [#110](https://github.com/ossrs/srs/issues/110), refine thread object. 3.0.19
* v3.0, 2017-02-12, Fix [#301](https://github.com/ossrs/srs/issues/301), user must config the codec in right way for HLS. 3.0.18
* v3.0, 2017-02-07, fix [#738](https://github.com/ossrs/srs/issues/738), support DVR general mp4. 3.0.17
* v3.0, 2017-01-19, for [#742](https://github.com/ossrs/srs/issues/742), refine source, meta and origin hub. 3.0.16
* v3.0, 2017-01-17, for [#742](https://github.com/ossrs/srs/issues/742), refine source, timeout, live cycle. 3.0.15
* v3.0, 2017-01-11, fix [#735](https://github.com/ossrs/srs/issues/735), config transform refer_publish invalid. 3.0.14
* v3.0, 2017-01-06, for [#730](https://github.com/ossrs/srs/issues/730), support config in/out ack size. 3.0.13
* v3.0, 2017-01-06, for [#711](https://github.com/ossrs/srs/issues/711), support perfile for transcode. 3.0.12
* v3.0, 2017-01-05, Fix [#727](https://github.com/ossrs/srs/issues/727), patch ST for valgrind and ARM. 3.0.11
* v3.0, 2017-01-05, for [#324](https://github.com/ossrs/srs/issues/324), always enable hstrs. 3.0.10
* v3.0, 2016-12-15, fix [#717](https://github.com/ossrs/srs/issues/717), [#691](https://github.com/ossrs/srs/issues/691), http api/static/stream support cors. 3.0.9
* v3.0, 2016-12-08, Fix [#105](https://github.com/ossrs/srs/issues/105), support log rotate signal SIGUSR1. 3.0.8
* v3.0, 2016-12-07, fix typo and refine grammar. 3.0.7
* v3.0, 2015-10-20, fix [#502](https://github.com/ossrs/srs/issues/502), support snapshot with http-callback or transcoder. 3.0.5
* v3.0, 2015-09-19, support amf0 and json to convert with each other.
* v3.0, 2015-09-19, json objects support dumps to string.
* v3.0, 2015-09-14, fix [#459](https://github.com/ossrs/srs/issues/459), support dvr raw api. 3.0.4
* v3.0, 2015-09-14, fix [#459](https://github.com/ossrs/srs/issues/459), dvr support apply filter for ng-control dvr module.
* v3.0, 2015-09-14, fix [#319](https://github.com/ossrs/srs/issues/319), http raw api support update global and vhost. 3.0.3
* v3.0, 2015-08-31, fix [#319](https://github.com/ossrs/srs/issues/319), http raw api support query global and vhost.
* v3.0, 2015-08-28, fix [#471](https://github.com/ossrs/srs/issues/471), api response the width and height. 3.0.2
* v3.0, 2015-08-25, fix [#367](https://github.com/ossrs/srs/issues/367), support nginx-rtmp exec. 3.0.1

<a name="v2-changes"></a>

## SRS 2.0 Changelog
* <strong>v2.0, 2020-01-25, [2.0 release8(2.0.272)](https://github.com/ossrs/srs/releases/tag/v2.0-r8) released. 87292 lines.</strong>
* v2.0, 2020-01-08, Merge [#1554](https://github.com/ossrs/srs/issues/1554), support logrotate copytruncate. 2.0.272
* v2.0, 2020-01-05, Merge [#1551](https://github.com/ossrs/srs/issues/1551), fix memory leak in RTSP stack. 2.0.270
* v2.0, 2019-12-26, For [#1488](https://github.com/ossrs/srs/issues/1488), pass client ip to http callback. 2.0.269
* v2.0, 2019-12-23, Fix [srs-librtmp #22](https://github.com/ossrs/srs-librtmp/issues/22), parse vhost splited by single seperator. 2.0.268
* v2.0, 2019-12-23, Fix [srs-librtmp #25](https://github.com/ossrs/srs-librtmp/issues/25), build srs-librtmp on windows. 2.0.267
* v2.0, 2019-12-13, Support openssl versions greater than 1.1.0. 2.0.266
* <strong>v2.0, 2019-11-29, [2.0 release7(2.0.265)](https://github.com/ossrs/srs/releases/tag/v2.0-r7) released. 86994 lines.</strong>
* v2.0, 2019-11-29, For [srs-docker](https://github.com/ossrs/srs-docker/tree/master/2.0), install Cherrypy without sudo. 2.0.265
* v2.0, 2019-04-06, For [#1304](https://github.com/ossrs/srs/issues/1304), Default HSTRS to on. 2.0.264
* <strong>v2.0, 2019-04-05, [2.0 release6(2.0.263)](https://github.com/ossrs/srs/releases/tag/v2.0-r6) released. 86994 lines.</strong>
* v2.0, 2019-04-05, Merge [#1312](https://github.com/ossrs/srs/issues/1312), Fix GCC7 build error, this statement may fall through. 2.0.263
* v2.0, 2019-04-05, Merge [#1339](https://github.com/ossrs/srs/issues/1339), Support HTTP-FLV params. 2.0.262
* v2.0, 2018-12-01, Merge [#1274](https://github.com/ossrs/srs/issues/1274), Upgrade to FFMPEG 4.1 and X264 157. 2.0.261
* v2.0, 2018-11-11, Merge [#1261](https://github.com/ossrs/srs/issues/1261), Support `_definst_` for Wowza. 2.0.260
* v2.0, 2018-11-11, Merge [#1263](https://github.com/ossrs/srs/issues/1263), Fix string trim bug. 2.0.259
* <strong>v2.0, 2018-10-28, [2.0 release5(2.0.258)](https://github.com/ossrs/srs/releases/tag/v2.0-r5) released. 86916 lines.</strong>
* v2.0, 2018-10-28, Fix [#1250](https://github.com/ossrs/srs/issues/1250), Support build on OSX10.14 Mojave. 2.0.258
* v2.0, 2018-10-08, Merge [#1236](https://github.com/ossrs/srs/issues/1236), Fix sleep bug in us. 2.0.257
* v2.0, 2018-10-08, Merge [#1237](https://github.com/ossrs/srs/issues/1237), Support param for transcoder. 2.0.256
* <strong>v2.0, 2018-08-12, [2.0 release4(2.0.255)](https://github.com/ossrs/srs/releases/tag/v2.0-r4) released. 86915 lines.</strong>
* v2.0, 2018-08-12, For [#1202](https://github.com/ossrs/srs/issues/1202), Support edge/forward to Aliyun CDN. 2.0.255
* v2.0, 2018-08-11, For [#910](https://github.com/ossrs/srs/issues/910), Support HTTP FLV with HTTP callback. 2.0.254
* v2.0, 2018-08-11, For [#1110](https://github.com/ossrs/srs/issues/1110), Refine params in http callback. 2.0.253
* v2.0, 2018-08-05, Refine HTTP-FLV latency, support realtime mode. 2.0.252
* v2.0, 2018-08-04, For [#1110](https://github.com/ossrs/srs/issues/1110), Support params in http callback. 2.0.251
* v2.0, 2018-08-02, For [#1031](https://github.com/ossrs/srs/issues/1031), SRS edge support douyu.com. 2.0.250
* v2.0, 2018-07-21, Merge [#1119](https://github.com/ossrs/srs/issues/1119), fix memory leak. 2.0.249
* <strong>v2.0, 2018-07-18, [2.0 release3(2.0.248)](https://github.com/ossrs/srs/releases/tag/v2.0-r3) released. 86775 lines.</strong>
* v2.0, 2018-07-17, Merge [#1176](https://github.com/ossrs/srs/issues/1176), fix scaned issues. 2.0.248
* v2.0, 2018-02-28, Merge [#1077](https://github.com/ossrs/srs/issues/1077), fix crash for edge HLS. 2.0.247
* v2.0, 2018-02-13, Fix [#1059](https://github.com/ossrs/srs/issues/1059), support vhost in stream parameters. 2.0.246
* v2.0, 2018-01-07, Merge [#1045](https://github.com/ossrs/srs/issues/1045), fix [#1044](https://github.com/ossrs/srs/issues/1044), TCP connection alive detection. 2.0.245
* v2.0, 2018-01-04, Merge [#1039](https://github.com/ossrs/srs/issues/1039), fix bug of init.d script.
* v2.0, 2018-01-01, Merge [#1033](https://github.com/ossrs/srs/issues/1033), allow user to add some specific flags. 2.0.244
* <strong>v2.0, 2017-06-10, [2.0 release2(2.0.243)](https://github.com/ossrs/srs/releases/tag/v2.0-r2) released. 86670 lines.</strong>
* v2.0, 2017-05-29, Merge [#899](https://github.com/ossrs/srs/issues/899) to fix [#893](https://github.com/ossrs/srs/issues/893), ts PES ext length. 2.0.243
* v2.0, 2017-05-01, Fix [#865](https://github.com/ossrs/srs/issues/865), shouldn't remove ts/m3u8 when hls_dispose disabled. 2.0.242
* v2.0, 2017-04-30, Fix [#636](https://github.com/ossrs/srs/issues/636), FD leak for requesting empty HTTP stream. 2.0.241
* v2.0, 2017-04-23, Fix [#851](https://github.com/ossrs/srs/issues/851), HTTP API support number of video frames for FPS. 2.0.240
* <strong>v2.0, 2017-04-18, [2.0 release1(2.0.239)](https://github.com/ossrs/srs/releases/tag/v2.0-r1) released. 86515 lines.</strong>
* v2.0, 2017-04-18, Fix [#848](https://github.com/ossrs/srs/issues/848), crash at HTTP fast buffer grow. 2.0.239
* v2.0, 2017-04-15, Fix [#844](https://github.com/ossrs/srs/issues/844), support Haivision encoder. 2.0.238
* v2.0, 2017-04-15, Merge [#846](https://github.com/ossrs/srs/issues/846), fix fd leak for FLV stream caster. 2.0.237
* v2.0, 2017-04-15, Merge [#841](https://github.com/ossrs/srs/issues/841), avoid the duplicated sps/pps in ts. 2.0.236
* v2.0, 2017-04-09, Fix [#834](https://github.com/ossrs/srs/issues/834), crash for TS context corrupt. 2.0.235
* <strong>v2.0, 2017-03-03, [2.0 release0(2.0.234)](https://github.com/ossrs/srs/releases/tag/v2.0-r0) released. 86373 lines.</strong>
* v2.0, 2017-02-25, for [#730](https://github.com/ossrs/srs/issues/730), remove the test code. 2.0.234
* v2.0, 2017-02-09, fix [#503](https://github.com/ossrs/srs/issues/503] disable utilities when reload a source. 2.0.233
* v2.0, 2017-01-22, for [#752](https://github.com/ossrs/srs/issues/752] release the io then free it for kbps. 2.0.232
* v2.0, 2017-01-18, fix [#750](https://github.com/ossrs/srs/issues/750] use specific error code for dns resolve. 2.0.231
* <strong>v2.0, 2017-01-18, [2.0 beta4(2.0.230)](https://github.com/ossrs/srs/releases/tag/v2.0-b4) released. 86334 lines.</strong>
* v2.0, 2017-01-18, fix [#749](https://github.com/ossrs/srs/issues/749), timestamp overflow for ATC. 2.0.230
* v2.0, 2017-01-11, fix [#740](https://github.com/ossrs/srs/issues/740), convert ts aac audio private stream 1 to common. 2.0.229
* v2.0, 2017-01-11, fix [#588](https://github.com/ossrs/srs/issues/588), kbps interface error. 2.0.228
* v2.0, 2017-01-11, fix [#736](https://github.com/ossrs/srs/issues/736), recovery the hls dispose. 2.0.227
* v2.0, 2017-01-10, refine hls html5 video template.
* v2.0, 2017-01-10, fix [#635](https://github.com/ossrs/srs/issues/635), hls support NonIDR(open gop). 2.0.226
* v2.0, 2017-01-06, for [#730](https://github.com/ossrs/srs/issues/730), reset ack follow flash player rules. 2.0.225
* v2.0, 2016-12-15, for [#513](https://github.com/ossrs/srs/issues/513), remove hls ram from srs2 to srs3+. 2.0.224
* <strong>v2.0, 2016-12-13, [2.0 beta3(2.0.223)](https://github.com/ossrs/srs/releases/tag/v2.0-b3) released. 86685 lines.</strong>
* v2.0, 2016-12-13, fix [#713](https://github.com/ossrs/srs/issues/713), disable the source cleanup. 2.0.223
* v2.0, 2016-12-13, fix [#713](https://github.com/ossrs/srs/issues/713), refine source to avoid critical fetch and create. 2.0.222
* <strong>v2.0, 2016-11-09, [2.0 beta2(2.0.221)](https://github.com/ossrs/srs/releases/tag/v2.0-b2) released. 86691 lines.</strong>
* v2.0, 2016-11-05, fix [#654](https://github.com/ossrs/srs/issues/654), crash when source cleanup for edge. 2.0.221
* v2.0, 2016-10-26, fix [#666](https://github.com/ossrs/srs/issues/666), crash when source cleanup for http-flv. 2.0.220
* v2.0, 2016-10-10, fix [#661](https://github.com/ossrs/srs/issues/661), close fd after thread stopped. 2.0.219
* v2.0, 2016-09-23, support asprocess for oryx. 2.0.218
* v2.0, 2016-09-23, support change work_dir for oryx.
* v2.0, 2016-09-15, fix [#640](https://github.com/ossrs/srs/issues/640), typo for rtmp type. 2.0.217
* v2.0, 2016-09-12, fix fast stream error bug. 2.0.216
* <strong>v2.0, 2016-09-09, [2.0 beta1(2.0.215)](https://github.com/ossrs/srs/releases/tag/v2.0-b1) released. 89941 lines.</strong>
* v2.0, 2016-09-09, refine librtmp comments about NALUs. 2.0.215
* v2.0, 2016-09-05, fix memory leak at source. 2.0.214
* v2.0, 2016-09-05, fix memory leak at handshake. 2.0.213
* v2.0, 2016-09-04, support valgrind for [patched st](https://github.com/ossrs/state-threads/issues/2).
* v2.0, 2016-09-03, support all arm for [patched st](https://github.com/ossrs/state-threads/issues/1). 2.0.212
* v2.0, 2016-09-01, workaround [#511](https://github.com/ossrs/srs/issues/511) the fly stfd in close. 2.0.211
* v2.0, 2016-08-30, comment the pcr.
* v2.0, 2016-08-18, fix [srs-librtmp#4](https://github.com/ossrs/srs-librtmp/issues/4) filter frame.
* v2.0, 2016-08-10, fix socket timeout for librtmp.
* v2.0, 2016-08-08, fix the crash by srs_info log.
* <strong>v2.0, 2016-08-06, [2.0 beta0(2.0.210)](https://github.com/ossrs/srs/releases/tag/v2.0-b0) released. 89704 lines.</strong>
* v2.0, 2016-05-17, fix the sps pps parse bug.
* v2.0, 2016-01-13, fix http reader bug, support infinite chunked. 2.0.209
* v2.0, 2016-01-09, merge [#559](https://github.com/ossrs/srs/pull/559) fix memory leak bug. 2.0.208
* v2.0, 2016-01-09, merge [#558](https://github.com/ossrs/srs/pull/558) add tcUrl for on_publish.
* v2.0, 2016-01-05, add keyword XCORE for coredump to identify the version. 2.0.207
* <strong>v2.0, 2015-12-23, [2.0 alpha3(2.0.205)](https://github.com/ossrs/srs/releases/tag/v2.0-a3) released. 89544 lines.</strong>
* v2.0, 2015-12-22, for [#509](https://github.com/ossrs/srs/issues/509) always alloc big object at heap. 2.0.205
* v2.0, 2015-12-22, for [#418](https://github.com/ossrs/srs/issues/418) ignore null connect props to make RED5 happy. 2.0.204
* v2.0, 2015-12-22, for [#546](https://github.com/ossrs/srs/issues/546) thread terminate normally dispose bug. 2.0.203
* v2.0, 2015-12-22, for [#541](https://github.com/ossrs/srs/issues/541) failed when chunk size too small. 2.0.202
* v2.0, 2015-12-15, default hls_on_error to continue. 2.0.201
* v2.0, 2015-11-16, for [#518](https://github.com/ossrs/srs/issues/518) fix fd leak bug when fork. 2.0.200
* v2.0, 2015-11-05, for [#511](https://github.com/ossrs/srs/issues/511) fix bug for restart thread. 2.0.199
* v2.0, 2015-11-02, for [#515](https://github.com/ossrs/srs/issues/515) use srs_freepa and SrsAutoFreeA for array. 2.0.198
* v2.0, 2015-10-28, for [ExoPlayer #828](https://github.com/google/ExoPlayer/pull/828), remove duration for live.
* v2.0, 2015-10-28, for [ExoPlayer #828](https://github.com/google/ExoPlayer/pull/828), add av tag in flv header. 2.0.197
* v2.0, 2015-10-27, for [#512](https://github.com/ossrs/srs/issues/512) partial hotfix the hls pure audio. 2.0.196
* <strong>v2.0, 2015-10-08, [2.0 alpha2(2.0.195)](https://github.com/ossrs/srs/releases/tag/v2.0-a2) released. 89358 lines.</strong>
* v2.0, 2015-10-04, for [#448](https://github.com/ossrs/srs/issues/448) fix the bug of response of http hooks. 2.0.195
* v2.0, 2015-10-01, for [#497](https://github.com/ossrs/srs/issues/497) response error when client not found to kickoff. 2.0.194
* v2.0, 2015-10-01, for [#495](https://github.com/ossrs/srs/issues/495) decrease the srs-librtmp size. 2.0.193
* v2.0, 2015-09-23, for [#485](https://github.com/ossrs/srs/issues/485) error when arm glibc 2.15+ or not i386/x86_64/amd64. 2.0.192
* v2.0, 2015-09-23, for [#485](https://github.com/ossrs/srs/issues/485) srs for respberrypi and cubieboard. 2.0.191
* v2.0, 2015-09-21, fix [#484](https://github.com/ossrs/srs/issues/484) hotfix the openssl build script 2.0.190
* <strong>v2.0, 2015-09-14, [2.0 alpha1(2.0.189)](https://github.com/ossrs/srs/releases/tag/v2.0-a1) released. 89269 lines.</strong>
* v2.0, 2015-09-14, fix [#474](https://github.com/ossrs/srs/issues/474) config to donot parse width/height from sps. 2.0.189
* v2.0, 2015-09-14, for [#474](https://github.com/ossrs/srs/issues/474) always release publish for source.
* v2.0, 2015-09-14, for [#458](https://github.com/ossrs/srs/issues/458) http hooks use source thread cid. 2.0.188
* v2.0, 2015-09-14, for [#475](https://github.com/ossrs/srs/issues/475) fix http hooks crash for st context switch. 2.0.187
* v2.0, 2015-09-09, support reload utc_time. 2.0.186
* <strong>v2.0, 2015-08-23, [2.0 alpha0(2.0.185)](https://github.com/ossrs/srs/releases/tag/v2.0-a0) released. 89022 lines.</strong>
* v2.0, 2015-08-22, HTTP API support JSONP by specifies the query string callback=xxx.
* v2.0, 2015-08-20, fix [#380](https://github.com/ossrs/srs/issues/380), srs-librtmp send sequence header when sps or pps changed.
* v2.0, 2015-08-18, close [#454](https://github.com/ossrs/srs/issues/454), support obs restart publish. 2.0.184
* v2.0, 2015-08-14, use reduce_sequence_header for stream control.
* v2.0, 2015-08-14, use send_min_interval for stream control. 2.0.183
* v2.0, 2015-08-12, enable the SRS_PERF_TCP_NODELAY and add config tcp_nodelay. 2.0.182
* v2.0, 2015-08-11, for [#442](https://github.com/ossrs/srs/issues/442) support kickoff connected client. 2.0.181
* v2.0, 2015-07-21, for [#169](https://github.com/ossrs/srs/issues/169) support default values for transcode. 2.0.180
* v2.0, 2015-07-21, fix [#435](https://github.com/ossrs/srs/issues/435) add pageUrl for HTTP callback on_play.
* v2.0, 2015-07-20, refine the hls, ignore packet when no sequence header. 2.0.179
* v2.0, 2015-07-16, for [#441](https://github.com/ossrs/srs/issues/441) use 30s timeout for first msg. 2.0.178
* v2.0, 2015-07-14, refine hls disable the time jitter, support not mix monotonically increase. 2.0.177
* v2.0, 2015-07-01, fix [#433](https://github.com/ossrs/srs/issues/433) fix the sps parse bug. 2.0.176
* v2.0, 2015-06-10, fix [#425](https://github.com/ossrs/srs/issues/425) refine the time jitter, correct (-inf,-250)+(250,+inf) to 10ms. 2.0.175
* v2.0, 2015-06-10, fix [#424](https://github.com/ossrs/srs/issues/424) fix aggregate timestamp bug. 2.0.174
* v2.0, 2015-06-06, fix [#421](https://github.com/ossrs/srs/issues/421) drop video for unkown RTMP header.
* v2.0, 2015-06-05, fix [#420](https://github.com/ossrs/srs/issues/420) remove ts for hls ram mode.
* v2.0, 2015-05-30, fix [#209](https://github.com/ossrs/srs/issues/209) cleanup hls when stop and timeout. 2.0.173.
* v2.0, 2015-05-29, fix [#409](https://github.com/ossrs/srs/issues/409) support pure video hls. 2.0.172.
* v2.0, 2015-05-28, support [srs-dolphin](https://github.com/ossrs/srs-dolphin), the multiple-process SRS.
* v2.0, 2015-05-24, fix [#404](https://github.com/ossrs/srs/issues/404) register handler then start http thread. 2.0.167.
* v2.0, 2015-05-23, refine the thread, protocol, kbps code. 2.0.166
* v2.0, 2015-05-23, fix [#391](https://github.com/ossrs/srs/issues/391) copy request for async call.
* v2.0, 2015-05-22, fix [#397](https://github.com/ossrs/srs/issues/397) the USER_HZ maybe not 100. 2.0.165
* v2.0, 2015-05-22, for [#400](https://github.com/ossrs/srs/issues/400), parse when got entire http header, by feilong. 2.0.164.
* v2.0, 2015-05-19, merge from bravo system, add the rtmfp to bms(commercial srs). 2.0.163.
* v2.0, 2015-05-10, support push flv stream over HTTP POST to SRS.
* v2.0, 2015-04-20, support ingest hls live stream to RTMP.
* v2.0, 2015-04-15, for [#383](https://github.com/ossrs/srs/issues/383), support mix_correct algorithm. 2.0.161.
* v2.0, 2015-04-13, for [#381](https://github.com/ossrs/srs/issues/381), support reap hls/ts by gop or not. 2.0.160.
* v2.0, 2015-04-10, enhanced on_hls_notify, support HTTP GET when reap ts.
* v2.0, 2015-04-10, refine the hls deviation for floor algorithm.
* v2.0, 2015-04-08, for [#375](https://github.com/ossrs/srs/issues/375), fix hls bug, keep cc continous between ts files. 2.0.159.
* v2.0, 2015-04-04, for [#304](https://github.com/ossrs/srs/issues/304), rewrite annexb mux for ts, refer to apple sample. 2.0.157.
* v2.0, 2015-04-03, enhanced avc decode, parse the sps get width+height. 2.0.156.
* v2.0, 2015-04-03, for [#372](https://github.com/ossrs/srs/issues/372), support transform vhost of edge 2.0.155.
* v2.0, 2015-03-30, for [#366](https://github.com/ossrs/srs/issues/366), config hls to disable cleanup of ts. 2.0.154.
* v2.0, 2015-03-31, support server cycle handler. 2.0.153.
* v2.0, 2015-03-31, support on_hls for http hooks. 2.0.152.
* v2.0, 2015-03-31, enhanced hls, support deviation for duration. 2.0.151.
* v2.0, 2015-03-30, for [#351](https://github.com/ossrs/srs/issues/351), support config the m3u8/ts path for hls. 2.0.149.
* v2.0, 2015-03-17, for [#155](https://github.com/ossrs/srs/issues/155), osx(darwin) support demo with nginx and ffmpeg. 2.0.143.
* v2.0, 2015-03-15, start [2.0release branch](https://github.com/ossrs/srs/tree/2.0release), 80773 lines.
* v2.0, 2015-03-14, fix [#324](https://github.com/ossrs/srs/issues/324), support hstrs(http stream trigger rtmp source) edge mode. 2.0.140.
* v2.0, 2015-03-14, for [#324](https://github.com/ossrs/srs/issues/324), support hstrs(http stream trigger rtmp source) origin mode. 2.0.139.
* v2.0, 2015-03-12, fix [#328](https://github.com/ossrs/srs/issues/328), support adobe hds. 2.0.138.
* v2.0, 2015-03-10, fix [#155](https://github.com/ossrs/srs/issues/155), support osx(darwin) for mac pro. 2.0.137.
* v2.0, 2015-03-08, fix [#316](https://github.com/ossrs/srs/issues/316), http api provides stream/vhost/srs/server bytes, codec and count. 2.0.136.
* v2.0, 2015-03-08, fix [#310](https://github.com/ossrs/srs/issues/310), refine aac LC, support aac HE/HEv2. 2.0.134.
* v2.0, 2015-03-06, for [#322](https://github.com/ossrs/srs/issues/322), fix http-flv stream bug, support multiple streams. 2.0.133.
* v2.0, 2015-03-06, refine http request parse. 2.0.132.
* v2.0, 2015-03-01, for [#179](https://github.com/ossrs/srs/issues/179), revert dvr http api. 2.0.128.
* v2.0, 2015-02-24, for [#304](https://github.com/ossrs/srs/issues/304), fix hls bug, write pts/dts error. 2.0.124
* v2.0, 2015-02-19, refine dvr, append file when dvr file exists. 2.0.122.
* v2.0, 2015-02-19, refine pithy print to more easyer to use. 2.0.121.
* v2.0, 2015-02-18, fix [#2304](https://github.com/ossrs/srs/issues/2304#issuecomment-826009290), support push rtsp to srs. 2.0.120.
* v2.0, 2015-02-17, the join maybe failed, should use a variable to ensure thread terminated. 2.0.119.
* v2.0, 2015-02-15, for [#304](https://github.com/ossrs/srs/issues/304), support config default acodec/vcodec. 2.0.118.
* v2.0, 2015-02-15, for [#304](https://github.com/ossrs/srs/issues/304), rewrite hls/ts code, support h.264+mp3 for hls. 2.0.117.
* v2.0, 2015-02-12, for [#304](https://github.com/ossrs/srs/issues/304), use stringstream to generate m3u8, add hls_td_ratio. 2.0.116.
* v2.0, 2015-02-11, dev code ZhouGuowen for 2.0.115.
* v2.0, 2015-02-10, for [#311](https://github.com/ossrs/srs/issues/311), set pcr_base to dts. 2.0.114.
* v2.0, 2015-02-10, fix [the bug](https://github.com/ossrs/srs/commit/87519aaae835199e5adb60c0ae2c1cd24939448c) of ibmf format which decoded in annexb.
* v2.0, 2015-02-10, for [#310](https://github.com/ossrs/srs/issues/310), downcast aac SSR to LC. 2.0.113
* v2.0, 2015-02-03, fix [#136](https://github.com/ossrs/srs/issues/136), support hls without io(in ram). 2.0.112
* v2.0, 2015-01-31, for [#250](https://github.com/ossrs/srs/issues/250), support push MPEGTS over UDP to SRS. 2.0.111
* v2.0, 2015-01-29, build libfdk-aac in ffmpeg. 2.0.108
* v2.0, 2015-01-25, for [#301](https://github.com/ossrs/srs/issues/301), hls support h.264+mp3, ok for vlc. 2.0.107
* v2.0, 2015-01-25, for [#301](https://github.com/ossrs/srs/issues/301), http ts stream support h.264+mp3. 2.0.106
* v2.0, 2015-01-25, hotfix [#268](https://github.com/ossrs/srs/issues/268), refine the pcr start at 0, dts/pts plus delay. 2.0.105
* v2.0, 2015-01-25, hotfix [#151](https://github.com/ossrs/srs/issues/151), refine pcr=dts-800ms and use dts/pts directly. 2.0.104
* v2.0, 2015-01-23, hotfix [#151](https://github.com/ossrs/srs/issues/151), use absolutely overflow to make jwplayer happy. 2.0.103
* v2.0, 2015-01-22, for [#293](https://github.com/ossrs/srs/issues/293), support http live ts stream. 2.0.101.
* v2.0, 2015-01-19, for [#293](https://github.com/ossrs/srs/issues/293), support http live flv/aac/mp3 stream with fast cache. 2.0.100.
* v2.0, 2015-01-18, for [#293](https://github.com/ossrs/srs/issues/293), support rtmp remux to http flv live stream. 2.0.99.
* v2.0, 2015-01-17, fix [#277](https://github.com/ossrs/srs/issues/277), refine http server refer to go http-framework. 2.0.98
* v2.0, 2015-01-17, for [#277](https://github.com/ossrs/srs/issues/277), refine http api refer to go http-framework. 2.0.97
* v2.0, 2015-01-17, hotfix [#290](https://github.com/ossrs/srs/issues/290), use iformat only for rtmp input. 2.0.95
* v2.0, 2015-01-08, hotfix [#281](https://github.com/ossrs/srs/issues/281), fix hls bug ignore type-9 send aud. 2.0.93
* v2.0, 2015-01-03, fix [#274](https://github.com/ossrs/srs/issues/274), http-callback support on_dvr when reap a dvr file. 2.0.89
* v2.0, 2015-01-03, hotfix to remove the pageUrl for http callback. 2.0.88
* v2.0, 2015-01-03, fix [#179](https://github.com/ossrs/srs/issues/179), dvr support custom filepath by variables. 2.0.87
* v2.0, 2015-01-02, fix [#211](https://github.com/ossrs/srs/issues/211), support security allow/deny publish/play all/ip. 2.0.86
* v2.0, 2015-01-02, hotfix [#207](https://github.com/ossrs/srs/issues/207), trim the last 0 of log. 2.0.85
* v2.0, 2014-01-02, fix [#158](https://github.com/ossrs/srs/issues/158), http-callback check http status code ok(200). 2.0.84
* v2.0, 2015-01-02, hotfix [#216](https://github.com/ossrs/srs/issues/216), http-callback post in application/json content-type. 2.0.83
* v2.0, 2014-01-02, fix [#263](https://github.com/ossrs/srs/issues/263), srs-librtmp flv read tag should init size. 2.0.82
* v2.0, 2015-01-01, hotfix [#270](https://github.com/ossrs/srs/issues/270), memory leak for http client post. 2.0.81
* v2.0, 2014-12-12, fix [#266](https://github.com/ossrs/srs/issues/266), aac profile is object id plus one. 2.0.80
* v2.0, 2014-12-29, hotfix [#267](https://github.com/ossrs/srs/issues/267), the forward dest ep should use server. 2.0.79
* v2.0, 2014-12-29, hotfix [#268](https://github.com/ossrs/srs/issues/268), the hls pcr is negative when startup. 2.0.78
* v2.0, 2014-12-22, hotfix [#264](https://github.com/ossrs/srs/issues/264), ignore NALU when sequence header to make HLS happy. 2.0.76
* v2.0, 2014-12-20, hotfix [#264](https://github.com/ossrs/srs/issues/264), support disconnect publish connect when hls error. 2.0.75
* v2.0, 2014-12-12, fix [#257](https://github.com/ossrs/srs/issues/257), support 0.1s+ latency. 2.0.70
* v2.0, 2014-12-08, update wiki for mr([EN](https://ossrs.io/lts/en-us/docs/v4/doc/low-latency#merged-read), [CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/low-latency#merged-read)) and mw([EN](https://ossrs.io/lts/en-us/docs/v4/doc/low-latency#merged-write), [CN](https://ossrs.net/lts/zh-cn/docs/v4/doc/low-latency#merged-write)).
* v2.0, 2014-12-07, fix [#251](https://github.com/ossrs/srs/issues/251), 10k+ clients, use queue cond wait and fast vector. 2.0.67
* v2.0, 2014-12-05, fix [#251](https://github.com/ossrs/srs/issues/251), 9k+ clients, use fast cache for msgs queue. 2.0.57
* v2.0, 2014-12-04, fix [#241](https://github.com/ossrs/srs/issues/241), add mw(merged-write) config. 2.0.53
* v2.0, 2014-12-04, for [#241](https://github.com/ossrs/srs/issues/241), support mr(merged-read) config and reload. 2.0.52.
* v2.0, 2014-12-04, enable [#241](https://github.com/ossrs/srs/issues/241) and [#248](https://github.com/ossrs/srs/issues/248), +25% performance, 2.5k publisher. 2.0.50
* v2.0, 2014-12-04, fix [#248](https://github.com/ossrs/srs/issues/248), improve about 15% performance for fast buffer. 2.0.49
* v2.0, 2014-12-03, fix [#244](https://github.com/ossrs/srs/issues/244), conn thread use cond to wait for recv thread error. 2.0.47.
* v2.0, 2014-12-02, merge [#239](https://github.com/ossrs/srs/pull/239), traverse the token before response connect. 2.0.45.
* v2.0, 2014-12-02, srs-librtmp support hijack io apis for st-load. 2.0.42.
* v2.0, 2014-12-01, for [#237](https://github.com/ossrs/srs/issues/237), refine syscall for recv, supports 1.5k clients. 2.0.41.
* v2.0, 2014-11-30, add qtcreate project file trunk/src/qt/srs/srs-qt.pro. 2.0.39.
* v2.0, 2014-11-29, fix [#235](https://github.com/ossrs/srs/issues/235), refine handshake, replace union with template method. 2.0.38.
* v2.0, 2014-11-28, fix [#215](https://github.com/ossrs/srs/issues/215), add srs_rtmp_dump tool. 2.0.37.
* v2.0, 2014-11-25, update PRIMARY, AUTHORS, CONTRIBUTORS rule. 2.0.32.
* v2.0, 2014-11-24, fix [#212](https://github.com/ossrs/srs/issues/212), support publish aac adts raw stream. 2.0.31.
* v2.0, 2014-11-22, fix [#217](https://github.com/ossrs/srs/issues/217), remove timeout recv, support 7.5k+ 250kbps clients. 2.0.30.
* v2.0, 2014-11-21, srs-librtmp add rtmp prefix for rtmp/utils/human apis. 2.0.29.
* v2.0, 2014-11-21, refine examples of srs-librtmp, add srs_print_rtmp_packet. 2.0.28.
* v2.0, 2014-11-20, fix [#212](https://github.com/ossrs/srs/issues/212), support publish audio raw frames. 2.0.27
* v2.0, 2014-11-19, fix [#213](https://github.com/ossrs/srs/issues/213), support compile [srs-librtmp on windows](https://github.com/winlinvip/srs.librtmp), [bug #213](https://github.com/ossrs/srs/issues/213). 2.0.26
* v2.0, 2014-11-18, all wiki translated to English. 2.0.23.
* v2.0, 2014-11-15, fix [#204](https://github.com/ossrs/srs/issues/204), srs-librtmp drop duplicated sps/pps(sequence header). 2.0.22.
* v2.0, 2014-11-15, fix [#203](https://github.com/ossrs/srs/issues/203), srs-librtmp drop any video before sps/pps(sequence header). 2.0.21.
* v2.0, 2014-11-15, fix [#202](https://github.com/ossrs/srs/issues/202), fix memory leak of h.264 raw packet send in srs-librtmp. 2.0.20.
* v2.0, 2014-11-13, fix [#200](https://github.com/ossrs/srs/issues/200), deadloop when read/write 0 and ETIME. 2.0.16.
* v2.0, 2014-11-13, fix [#194](https://github.com/ossrs/srs/issues/194), writev multiple msgs, support 6k+ 250kbps clients. 2.0.15.
* v2.0, 2014-11-12, fix [#194](https://github.com/ossrs/srs/issues/194), optmized st for timeout recv. pulse to 500ms. 2.0.14.
* v2.0, 2014-11-11, fix [#195](https://github.com/ossrs/srs/issues/195), remove the confuse code st_usleep(0). 2.0.13.
* v2.0, 2014-11-08, fix [#191](https://github.com/ossrs/srs/issues/191), configure --export-librtmp-project and --export-librtmp-single. 2.0.11.
* v2.0, 2014-11-08, fix [#66](https://github.com/ossrs/srs/issues/66), srs-librtmp support write h264 raw packet. 2.0.9.
* v2.0, 2014-10-25, fix [#185](https://github.com/ossrs/srs/issues/185), AMF0 support 0x0B the date type codec. 2.0.7.
* v2.0, 2014-10-24, fix [#186](https://github.com/ossrs/srs/issues/186), hotfix for bug #186, drop connect args when not object. 2.0.6.
* v2.0, 2014-10-24, rename wiki/xxx to wiki/v4_CN_xxx. 2.0.3.
* v2.0, 2014-10-19, fix [#184](https://github.com/ossrs/srs/issues/184), support AnnexB in RTMP body for HLS. 2.0.2
* v2.0, 2014-10-18, remove supports for OSX(darwin). 2.0.1.
* v2.0, 2014-10-16, revert github srs README to English. 2.0.0.

<a name="v1-changes"></a>

## SRS 1.0 Changelog

* <strong>v1.0, 2014-12-05, [1.0 release(1.0.10)](https://github.com/ossrs/srs/releases/tag/v1.0-r0) released. 59391 lines.</strong>
* <strong>v1.0, 2014-10-09, [1.0 beta(1.0.0)](https://github.com/ossrs/srs/releases/tag/v0.9.8) released. 59316 lines.</strong>
* v1.0, 2014-10-08, fix [#151](https://github.com/ossrs/srs/issues/151), always reap ts whatever audio or video packet. 0.9.223.
* v1.0, 2014-10-08, fix [#162](https://github.com/ossrs/srs/issues/162), failed if no epoll. 0.9.222.
* v1.0, 2014-09-30, fix [#180](https://github.com/ossrs/srs/issues/180), crash for multiple edge publishing the same stream. 0.9.220.
* v1.0, 2014-09-26, fix hls bug, refine config and log, according to clion of jetbrains. 0.9.216.
* v1.0, 2014-09-25, fix [#177](https://github.com/ossrs/srs/issues/177), dvr segment add config dvr_wait_keyframe. 0.9.213.
* v1.0, 2014-08-28, fix [#167](https://github.com/ossrs/srs/issues/167), add openssl includes to utest. 0.9.209.
* v1.0, 2014-08-27, max connections is 32756, for st use mmap default. 0.9.209
* v1.0, 2014-08-24, fix [#150](https://github.com/ossrs/srs/issues/150), forward should forward the sequence header when retry. 0.9.208.
* v1.0, 2014-08-22, for [#165](https://github.com/ossrs/srs/issues/165), refine dh wrapper, ensure public key is 128bytes. 0.9.206.
* v1.0, 2014-08-19, for [#160](https://github.com/ossrs/srs/issues/160), support forward/edge to flussonic, disable debug_srs_upnode to make flussonic happy. 0.9.201.
* v1.0, 2014-08-17, for [#155](https://github.com/ossrs/srs/issues/155), refine for osx, with ssl/http, disable statistics. 0.9.198.
* v1.0, 2014-08-06, fix [#148](https://github.com/ossrs/srs/issues/148), simplify the RTMP handshake key generation. 0.9.191.
* v1.0, 2014-08-06, fix [#147](https://github.com/ossrs/srs/issues/147), support identify the srs edge. 0.9.190.
* <strong>v1.0, 2014-08-03, [1.0 mainline7(0.9.189)](https://github.com/ossrs/srs/releases/tag/v0.9.7) released. 57432 lines.</strong>
* v1.0, 2014-08-03, fix [#79](https://github.com/ossrs/srs/issues/79), fix the reload remove edge assert bug. 0.9.189.
* v1.0, 2014-08-03, fix [#57](https://github.com/ossrs/srs/issues/57), use lock(acquire/release publish) to avoid duplicated publishing. 0.9.188.
* v1.0, 2014-08-03, fix [#85](https://github.com/ossrs/srs/issues/85), fix the segment-dvr sequence header missing. 0.9.187.
* v1.0, 2014-08-03, fix [#145](https://github.com/ossrs/srs/issues/145), refine ffmpeg log, check abitrate for libaacplus. 0.9.186.
* v1.0, 2014-08-03, fix [#143](https://github.com/ossrs/srs/issues/143), fix retrieve sys stat bug for all linux. 0.9.185.
* v1.0, 2014-08-02, fix [#138](https://github.com/ossrs/srs/issues/138), fix http hooks bug, regression bug. 0.9.184.
* v1.0, 2014-08-02, fix [#142](https://github.com/ossrs/srs/issues/142), fix tcp stat slow bug, use /proc/net/sockstat instead, refer to 'ss -s'. 0.9.183.
* v1.0, 2014-07-31, fix [#141](https://github.com/ossrs/srs/issues/141), support tun0(vpn network device) ip retrieve. 0.9.179.
* v1.0, 2014-07-27, support partially build on OSX(Darwin). 0.9.177
* v1.0, 2014-07-27, api connections add udp, add disk iops. 0.9.176
* v1.0, 2014-07-26, complete config utest. 0.9.173
* v1.0, 2014-07-26, fix [#124](https://github.com/ossrs/srs/issues/124), gop cache support disable video in publishing. 0.9.171.
* v1.0, 2014-07-23, fix [#121](https://github.com/ossrs/srs/issues/121), srs_info detail log compile failed. 0.9.168.
* v1.0, 2014-07-19, fix [#119](https://github.com/ossrs/srs/issues/119), use iformat and oformat for ffmpeg transcode. 0.9.163.
* <strong>v1.0, 2014-07-13, [1.0 mainline6(0.9.160)](https://github.com/ossrs/srs/releases/tag/v0.9.6) released. 50029 lines.</strong>
* v1.0, 2014-07-13, refine the bandwidth check/test, add as/js library, use srs-librtmp for linux tool. 0.9.159
* v1.0, 2014-07-12, complete rtmp stack utest. 0.9.156
* v1.0, 2014-07-06, fix [#81](https://github.com/ossrs/srs/issues/81), fix HLS codec info, IOS ok. 0.9.153.
* v1.0, 2014-07-06, fix [#103](https://github.com/ossrs/srs/issues/103), support all aac sample rate. 0.9.150.
* v1.0, 2014-07-05, complete kernel utest. 0.9.149
* v1.0, 2014-06-30, fix [#111](https://github.com/ossrs/srs/issues/111), always use 31bits timestamp. 0.9.143.
* v1.0, 2014-06-28, response the call message with null. 0.9.137
* v1.0, 2014-06-28, fix [#110](https://github.com/ossrs/srs/issues/110), thread start segment fault, thread cycle stop destroy thread. 0.9.136
* v1.0, 2014-06-27, fix [#109](https://github.com/ossrs/srs/issues/109), fix the system jump time, adjust system startup time. 0.9.135
* <strong>v1.0, 2014-06-27, [1.0 mainline5(0.9.134)](https://github.com/ossrs/srs/releases/tag/v0.9.5) released. 41573 lines.</strong>
* v1.0, 2014-06-27, SRS online 30days with RTMP/HLS.
* v1.0, 2014-06-25, fix [#108](https://github.com/ossrs/srs/issues/108), support config time jitter for encoder non-monotonical stream. 0.9.133
* v1.0, 2014-06-23, support report summaries in heartbeat. 0.9.132
* v1.0, 2014-06-22, performance refine, support [3k+](https://ossrs.net/lts/zh-cn/docs/v4/doc/performance#performancereport4k) connections(270kbps). 0.9.130
* v1.0, 2014-06-21, support edge [token traverse](https://ossrs.net/lts/zh-cn/docs/v4/doc/drm#tokentraverse), fix [#104](https://github.com/ossrs/srs/issues/104). 0.9.129
* v1.0, 2014-06-19, add connections count to api summaries. 0.9.127
* v1.0, 2014-06-19, add srs bytes and kbps to api summaries. 0.9.126
* v1.0, 2014-06-18, add network bytes to api summaries. 0.9.125
* v1.0, 2014-06-14, fix [#98](https://github.com/ossrs/srs/issues/98), workaround for librtmp ping(fmt=1,cid=2 fresh stream). 0.9.124
* v1.0, 2014-05-29, support flv inject and flv http streaming with start=bytes. 0.9.122
* <strong>v1.0, 2014-05-28, [1.0 mainline4(0.9.120)](https://github.com/ossrs/srs/releases/tag/v0.9.4) released. 39200 lines.</strong>
* v1.0, 2014-05-27, fix [#87](https://github.com/ossrs/srs/issues/87), add source id for full trackable log. 0.9.120
* v1.0, 2014-05-27, fix [#84](https://github.com/ossrs/srs/issues/84), unpublish when edge disconnect. 0.9.119
* v1.0, 2014-05-27, fix [#89](https://github.com/ossrs/srs/issues/89), config to /dev/null to disable ffmpeg log. 0.9.117
* v1.0, 2014-05-25, fix [#76](https://github.com/ossrs/srs/issues/76), allow edge vhost to add or remove. 0.9.114
* v1.0, 2014-05-24, Johnny contribute [ossrs.net](http://ossrs.net). karthikeyan start to translate wiki to English.
* v1.0, 2014-05-22, fix [#78](https://github.com/ossrs/srs/issues/78), st joinable thread must be stop by other threads, 0.9.113
* v1.0, 2014-05-22, support amf0 StrictArray(0x0a). 0.9.111.
* v1.0, 2014-05-22, support flv parser, add amf0 to librtmp. 0.9.110
* v1.0, 2014-05-22, fix [#74](https://github.com/ossrs/srs/issues/74), add tcUrl for http callback on_connect, 0.9.109
* v1.0, 2014-05-19, support http heartbeat, 0.9.107
* <strong>v1.0, 2014-05-18, [1.0 mainline3(0.9.105)](https://github.com/ossrs/srs/releases/tag/v0.9.3) released. 37594 lines.</strong>
* v1.0, 2014-05-18, support http api json, to PUT/POST. 0.9.105
* v1.0, 2014-05-17, fix [#72](https://github.com/ossrs/srs/issues/72), also need stream_id for send_and_free_message. 0.9.101
* v1.0, 2014-05-17, rename struct to class. 0.9.100
* v1.0, 2014-05-14, fix [#67](https://github.com/ossrs/srs/issues/67] pithy print, stage must has a age. 0.9.98
* v1.0, 2014-05-13, fix mem leak for delete[] SharedPtrMessage array. 0.9.95
* v1.0, 2014-05-12, refine the kbps calc module. 0.9.93
* v1.0, 2014-05-12, fix bug [#64](https://github.com/ossrs/srs/issues/64): install_dir=DESTDIR+PREFIX
* v1.0, 2014-05-08, fix [#36](https://github.com/ossrs/srs/issues/36): never directly use \*(int32_t\*) for arm.
* v1.0, 2014-05-08, fix [#60](https://github.com/ossrs/srs/issues/60): support aggregate message
* v1.0, 2014-05-08, fix [#59](https://github.com/ossrs/srs/issues/59), edge support FMS origin server. 0.9.92
* v1.0, 2014-05-06, fix [#50](https://github.com/ossrs/srs/issues/50), ubuntu14 build error.
* v1.0, 2014-05-04, support mips linux.
* v1.0, 2014-04-30, fix bug [#34](https://github.com/ossrs/srs/issues/34): convert signal to io thread. 0.9.85
* v1.0, 2014-04-29, refine RTMP protocol completed, to 0.9.81
* <strong>v1.0, 2014-04-28, [1.0 mainline2(0.9.79)](https://github.com/ossrs/srs/releases/tag/v0.9.2) released. 35255 lines.</strong>
* v1.0, 2014-04-28, support full edge RTMP server. 0.9.79
* v1.0, 2014-04-27, support basic edge(play/publish) RTMP server. 0.9.78
* v1.0, 2014-04-25, add donation page. 0.9.76
* v1.0, 2014-04-21, support android app to start srs for internal edge. 0.9.72
* v1.0, 2014-04-19, support tool over srs-librtmp to ingest flv/rtmp. 0.9.71
* v1.0, 2014-04-17, support dvr(record live to flv file for vod). 0.9.69
* v1.0, 2014-04-11, add speex1.2 to transcode flash encoder stream. 0.9.58
* v1.0, 2014-04-10, support reload ingesters(add/remov/update). 0.9.57
* <strong>v1.0, 2014-04-07, [1.0 mainline(0.9.55)](https://github.com/ossrs/srs/releases/tag/v0.9.1) released. 30000 lines.</strong>
* v1.0, 2014-04-07, support [ingest](https://ossrs.net/lts/zh-cn/docs/v4/doc/sample-ingest) file/stream/device.
* v1.0, 2014-04-05, support [http api](https://ossrs.net/lts/zh-cn/docs/v4/doc/http-api) and [http server](https://ossrs.net/lts/zh-cn/docs/v4/doc/http-server).
* v1.0, 2014-04-03, implements http framework and api/v1/version.
* v1.0, 2014-03-30, fix bug for st detecting epoll failed, force st to use epoll.
* v1.0, 2014-03-29, add wiki [Performance for RaspberryPi](https://ossrs.net/lts/zh-cn/docs/v4/doc/raspberrypi).
* v1.0, 2014-03-29, add release binary package for raspberry-pi.
* v1.0, 2014-03-26, support RTMP ATC for HLS/HDS to support backup(failover).
* v1.0, 2014-03-23, support daemon, default start in daemon.
* v1.0, 2014-03-22, support make install/install-api and uninstall.
* v1.0, 2014-03-22, add ./etc/init.d/srs, refine to support make clean then make.
* v1.0, 2014-03-21, write pid to ./objs/srs.pid.
* v1.0, 2014-03-20, refine hls code, support pure audio HLS.
* v1.0, 2014-03-19, add vn/an for FFMPEG to drop video/audio for radio stream.
* v1.0, 2014-03-19, refine handshake, client support complex handshake, add utest.
* v1.0, 2014-03-16, fix bug on arm of st, the sp change from 20 to 8, for respberry-pi, @see [commit](https://github.com/ossrs/srs/commit/5a4373d4835758188b9a1f03005cea0b6ddc62aa)
* v1.0, 2014-03-16, support ARM([debian armhf, v7cpu](https://ossrs.net/lts/zh-cn/docs/v4/doc/arm)) with rtmp/ssl/hls/librtmp.
* v1.0, 2014-03-12, finish utest for amf0 codec.
* v1.0, 2014-03-06, add gperftools for mem leak detect, mem/cpu profile.
* v1.0, 2014-03-04, add gest framework for utest, build success.
* v1.0, 2014-03-02, srs-librtmp, client publish/play library like librtmp.
* v1.0, 2014-03-01, modularity, extract core/kernel/rtmp/app/main module.
* v1.0, 2014-02-28, support arm build(SRS/ST), add ssl to 3rdparty package.
* v1.0, 2014-02-28, add wiki [BuildArm](https://ossrs.net/lts/zh-cn/docs/v4/doc/install), [FFMPEG](https://ossrs.net/lts/zh-cn/docs/v4/doc/ffmpeg), [Reload](https://ossrs.net/lts/zh-cn/docs/v4/doc/reload)
* v1.0, 2014-02-27, add wiki [LowLatency](https://ossrs.net/lts/zh-cn/docs/v4/doc/low-latency), [HTTPCallback](https://ossrs.net/lts/zh-cn/docs/v4/doc/http-callback)
* v1.0, 2014-01-19, add wiki [DeliveryHLS](https://ossrs.net/lts/zh-cn/docs/v4/doc/delivery-hls)
* v1.0, 2014-01-11, fix jw/flower player pause bug, which send closeStream actually.
* v1.0, 2014-01-05, add wiki [Build](https://ossrs.net/lts/zh-cn/docs/v4/doc/install), [Performance](https://ossrs.net/lts/zh-cn/docs/v4/doc/performance), [Forward](https://ossrs.net/lts/zh-cn/docs/v4/doc/forward)
* v1.0, 2014-01-01, change listen(512), chunk-size(60000), to improve performance.
* v1.0, 2013-12-27, merge from wenjie, the bandwidth test feature.
* <strong>v0.9, 2013-12-25, [v0.9](https://github.com/ossrs/srs/releases/tag/v0.9.0) released. 20926 lines.</strong>
* v0.9, 2013-12-25, fix the bitrate bug(in Bps), use enhanced microphone.
* v0.9, 2013-12-22, demo video meeting or chat(SRS+cherrypy+jquery+bootstrap).
* v0.9, 2013-12-22, merge from wenjie, support banwidth test.
* v0.9, 2013-12-22, merge from wenjie: support set chunk size at vhost level
* v0.9, 2013-12-21, add [players](http://ossrs.net/players/srs_player.html) for play and publish.
* v0.9, 2013-12-15, ensure the HLS(ts) is continous when republish stream.
* v0.9, 2013-12-15, fix the hls reload bug, feed it the sequence header.
* v0.9, 2013-12-15, refine protocol, use int64_t timestamp for ts and jitter.
* v0.9, 2013-12-15, support set the live queue length(in seconds), drop when full.
* v0.9, 2013-12-15, fix the forwarder reconnect bug, feed it the sequence header.
* v0.9, 2013-12-15, support reload the hls/forwarder/transcoder.
* v0.9, 2013-12-14, refine the thread model for the retry threads.
* v0.9, 2013-12-10, auto install depends tools/libs on centos/ubuntu.
* <strong>v0.8, 2013-12-08, [v0.8](https://github.com/ossrs/srs/releases/tag/v0.8.0) released. 19186 lines.</strong>
* v0.8, 2013-12-08, support [http hooks](https://ossrs.net/lts/zh-cn/docs/v4/doc/http-callback): on_connect/close/publish/unpublish/play/stop.
* v0.8, 2013-12-08, support multiple http hooks for a event.
* v0.8, 2013-12-07, support http callback hooks, on_connect.
* v0.8, 2013-12-07, support network based cli and json result, add CherryPy 3.2.4.
* v0.8, 2013-12-07, update http/hls/rtmp load test tool [SB](https://github.com/ossrs/srs-bench), use SRS rtmp sdk.
* v0.8, 2013-12-06, support max_connections, drop if exceed.
* v0.8, 2013-12-05, support log_dir, write ffmpeg log to file.
* v0.8, 2013-12-05, fix the forward/hls/encoder bug.
* <strong>v0.7, 2013-12-03, [v0.7](https://github.com/ossrs/srs/releases/tag/v0.7.0) released. 17605 lines.</strong>
* v0.7, 2013-12-01, support dead-loop detect for forwarder and transcoder.
* v0.7, 2013-12-01, support all ffmpeg filters and params.
* v0.7, 2013-11-30, support live stream transcoder by ffmpeg.
* v0.7, 2013-11-30, support --with/without -ffmpeg, build ffmpeg-2.1.
* v0.7, 2013-11-30, add ffmpeg-2.1, x264-core138, lame-3.99.5, libaacplus-2.0.2.
* <strong>v0.6, 2013-11-29, [v0.6](https://github.com/ossrs/srs/releases/tag/v0.6.0) released. 16094 lines.</strong>
* v0.6, 2013-11-29, add performance summary, 1800 clients, 900Mbps, CPU 90.2%, 41MB.
* v0.6, 2013-11-29, support forward stream to other edge server.
* v0.6, 2013-11-29, support forward stream to other origin server.
* v0.6, 2013-11-28, fix memory leak bug, aac decode bug.
* v0.6, 2013-11-27, support --with or --without -hls and -ssl options.
* v0.6, 2013-11-27, support AAC 44100HZ sample rate for iphone, adjust the timestamp.
* <strong>v0.5, 2013-11-26, [v0.5](https://github.com/ossrs/srs/releases/tag/v0.5.0) released. 14449 lines.</strong>
* v0.5, 2013-11-24, support HLS(m3u8), fragment and window.
* v0.5, 2013-11-24, support record to ts file for HLS.
* v0.5, 2013-11-21, add ts_info tool to demux ts file.
* v0.5, 2013-11-16, add rtmp players(OSMF/jwplayer5/jwplayer6).
* <strong>v0.4, 2013-11-10, [v0.4](https://github.com/ossrs/srs/releases/tag/v0.4.0) released. 12500 lines.</strong>
* v0.4, 2013-11-10, support config and reload the pithy print.
* v0.4, 2013-11-09, support reload config(vhost and its detail).
* v0.4, 2013-11-09, support reload config(listen and chunk_size) by SIGHUP(1).
* v0.4, 2013-11-09, support longtime(>4.6hours) publish/play.
* v0.4, 2013-11-09, support config the chunk_size.
* v0.4, 2013-11-09, support pause for live stream.
* <strong>v0.3, 2013-11-04, [v0.3](https://github.com/ossrs/srs/releases/tag/v0.3.0) released. 11773 lines.</strong>
* v0.3, 2013-11-04, support refer/play-refer/publish-refer.
* v0.3, 2013-11-04, support vhosts specified config.
* v0.3, 2013-11-02, support listen multiple ports.
* v0.3, 2013-11-02, support config file in nginx-conf style.
* v0.3, 2013-10-29, support pithy print log message specified by stage.
* v0.3, 2013-10-28, support librtmp without extended-timestamp in 0xCX chunk packet.
* v0.3, 2013-10-27, support cache last gop for client fast startup.
* <strong>v0.2, 2013-10-25, [v0.2](https://github.com/ossrs/srs/releases/tag/v0.2.0) released. 10125 lines.</strong>
* v0.2, 2013-10-25, support flash publish.
* v0.2, 2013-10-25, support h264/avc codec by rtmp complex handshake.
* v0.2, 2013-10-24, support time jitter detect and correct algorithm
* v0.2, 2013-10-24, support decode codec type to cache the h264/avc sequence header.
* <strong>v0.1, 2013-10-23, [v0.1](https://github.com/ossrs/srs/releases/tag/v0.1.0) released. 8287 lines.</strong>
* v0.1, 2013-10-23, support basic amf0 codec, simplify the api using c-style api.
* v0.1, 2013-10-23, support shared ptr msg for zero memory copy.
* v0.1, 2013-10-22, support vp6 codec with rtmp protocol specified simple handshake.
* v0.1, 2013-10-20, support multiple flash client play live streaming.
* v0.1, 2013-10-20, support FMLE/FFMPEG publish live streaming.
* v0.1, 2013-10-18, support rtmp message2chunk protocol(send\_message).
* v0.1, 2013-10-17, support rtmp chunk2message protocol(recv\_message).

Winlin 2021

