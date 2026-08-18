# FAQ

About Q&A, please follow [rules](https://stackoverflow.com/help/product-support)：

* Please read the [Wiki](../docs/v4/doc/introduction) first.
* Search your issue in this FAQ and [issues](https://github.com/ossrs/srs/issues)
* How do I? -- [Stack Overflow](https://stackoverflow.com/questions/tagged/simple-realtime-server)
* I got this error, why? -- [Stack Overflow](https://stackoverflow.com/questions/tagged/simple-realtime-server)
* I got this error and I'm sure it's a bug -- [file an issue](https://github.com/ossrs/srs/issues/new) or [PR](./how-to-file-pr)
* I have an idea/request -- [file an issue](https://github.com/ossrs/srs/issues/new) or [PR](./how-to-file-pr)
* Why do you? -- [discord community](https://discord.gg/yZ4BnPmHAd) (developer forum etc)
* When will you? -- [discord community](https://discord.gg/yZ4BnPmHAd)

## FAQ

Here are some common questions. If you can't find your question, please search in this [Issue](https://github.com/ossrs/srs/issues) 
first. If you are sure it is a bug and it has not been submitted before, please submit an 
Issue according to the requirements.

> Note: This is FAQ about SRS, please see [Oryx FAQ](./faq-oryx) for Oryx.

<a name='cdn'></a> <a name='vod'></a>

### [CDN](#cdn)
* Questions about RTMP/HTTP-FLV/WebRTC live streaming?
  > 1. SRS only supports streaming protocols, such as live streaming or WebRTC. For details, please refer to the cluster section in the WiKi.
* Questions about HLS/DASH segmented live streaming, or on-demand/recording/VoD/DVR?
  > 1. SRS can record as on-demand files. Please refer to [DVR](../docs/v4/doc/dvr)
  > 1. SRS can generate HLS or DASH. Please refer to [HLS](../docs/v4/doc/delivery-hls)
* Questions about HLS/DASH/VoD/DVR distribution clusters?
  > 1. These are all HTTP files, and for HTTP file distribution clusters, it is recommended to use NGINX. Please refer to [HLS Cluster](../docs/v4/doc/sample-hls-cluster)
  > 1. You can use NGINX in conjunction with SRS Edge to distribute HTTP-FLV, implementing the distribution of all HTTP protocols. Please refer to [Nginx For HLS](../docs/v4/doc/nginx-for-hls#work-with-srs-edge-server)
* SRS source cluster, multi-stream hot backup, stream switching, push stream disaster recovery, questions about live stream disaster recovery and switching, refer to [link](https://stackoverflow.com/a/70629002/17679565).
* How can you build a server network to provide nearby services and expand server capacity? You can use the SRS Edge cluster as a solution. For more information, refer to this [link](https://stackoverflow.com/a/71030396/17679565).
* How to create multi-stream backup and switch between them: Use multiple streams and select one that is available. For stream disaster recovery and switching, refer to this [link](https://stackoverflow.com/a/77363633/17679565).

<a name="console"></a>

### [Console](#console)
* `Pagination`: For pagination issues related to console streams and clients, refer to [#3451](https://github.com/ossrs/srs/issues/3451)
  > 1. The default API parameters are `start=0`, `count=10`, and the Console does not support pagination. It is planned to be supported in the new Console.

<a name='cors'></a>

### [CORS](#cors)
* `CORS`: How to setup cross-domain access for HTTP APIs or streams
  > 1. SRS 3.0 supports cross-domain (CORS) access, so there is no need for additional HTTP proxies, as it is built-in and enabled by default. Please refer to [#717](https://github.com/ossrs/srs/issues/717) [#798](https://github.com/ossrs/srs/issues/798) [#1002](https://github.com/ossrs/srs/issues/1002)
  > 1. Of course, using an Nginx proxy server can also solve cross-domain issues, so there is no need to set it in SRS. Note that you only need to proxy the API, not the media stream, because the bandwidth consumption of the stream is too high, which will cause the proxy to crash and is not necessary.
  > 1. Use Nginx or Caddy proxy to provide a unified HTTP/HTTPS service. Please refer to [#2881](https://github.com/ossrs/srs/issues/2881)

<a name='cpu-and-os'></a>

### [CPU and OS](#cpu-and-os)
* `CPU and OS`: What's the CPU architecture and OS operating system supported by SRS
  > 1. SRS supports common CPU architectures, such as x86_64 or amd64, as well as armv7/aarch64/AppleM1, MIPS or RISCV, and Loongson loongarch. For other CPU adaptations, please refer to [ST#22](https://github.com/ossrs/state-threads/issues/22).
  > 1. SRS supports commonly used operating systems, such as Linux including CentOS and Ubuntu, macOS, and Windows.
  > 1. SRS also supports domestic Xin Chuang systems. If you need to adapt to a new domestic Xin Chuang system, you can submit an issue.
* `Windows`: Special notes about Windows
  > 1. Generally, Windows is less used as a server, but there are some application scenarios. SRS 5.0 currently supports Windows, and each version will have a Windows installation package for download.
  > 1. Since it is difficult for everyone to download from Github, we provide a Gitee mirror download. Please see [Gitee: Releases](https://gitee.com/ossrs/srs/releases) for each version's attachments.
  > 1. There are still some issues on the Windows platform that have not been resolved, and we will continue to improve support. For details, please refer to [#2532](https://github.com/ossrs/srs/issues/2532).

<a name='dvr'></a>

### [DVR](#dvr)

* `Dynamic DVR`: How to do dynamic recording, regular expression matching for streams that need to be recorded, etc.
  > 1. You can use `on_publish` to callback the business system and implement complex rules.
  > 1. For specific recording files, use `on_hls` to copy the slices to the recording directory or cloud storage.
  > 1. You can refer to the DVR implementation in [oryx](https://github.com/ossrs/oryx/blob/main/platform/srs-hooks.go).
  > 1. SRS will not support dynamic DVR, but some solutions are provided. You can also refer to [#1577](https://github.com/ossrs/srs/issues/1577).
* Why does recording WebRTC as MP4 fail in SRS? Refer to this [link](https://stackoverflow.com/a/75861599/17679565) for more information.

<a name='edge-hls-dvr-rtc'></a>

### [Edge HLS/DVR/RTC](#edge-hls-dvr-rtc)
* `Edge HLS/DVR/RTC`: Does Edge Cluster support for HLS/DVR/RTC, etc.
  > 1. Edge is a live streaming cluster that only supports live streaming protocols such as RTMP and FLV. Only the origin server can support HLS/DVR/RTC. Refer to [#1066](https://github.com/ossrs/srs/issues/1066)
  > 1. Currently, there is no restriction on using HLS/DVR/RTC capabilities in Edge, but they will be disabled in the future. So please do not use them this way, and they won't work.
  > 1. For the HLS cluster, please refer to the documentation [HLS Edge Cluster](../docs/v5/doc/nginx-for-hls)
  > 1. The development of WebRTC and SRT clustering capabilities is in progress. Refer to [#3138](ttps://github.com/ossrs/srs/issues/3138)

<a name='ffmpeg'></a>

### [FFmpeg](#ffmpeg)
* `FFmpeg`: Questions related to FFmpeg
  > 1. If FFmpeg is not found, the error `terminate, please restart it` appears, compilation fails with `No FFmpeg found`, or FFmpeg does not support h.265 or other codecs, you need to compile or download FFmpeg yourself and place it in the specified path, then SRS will detect it. Please refer to [#1523](https://github.com/ossrs/srs/issues/1523)
  > 1. If you have questions about using FFmpeg, please do not submit issues in SRS. Instead, go to the FFmpeg community. Issues about FFmpeg in SRS will be deleted directly. Don't be lazy.

<a name='features'></a>

### [Features](#features)
* About supported features, outdated features, and plans?
  > 1. Each version supports different features, which are listed on the Github homepage, such as [develop/5.0](https://github.com/ossrs/srs/blob/develop/trunk/doc/Features.md#features), [release/4.0](https://github.com/ossrs/srs/blob/4.0release/trunk/doc/Features.md#features), [release/3.0](https://github.com/ossrs/srs/tree/3.0release#features).
  > 1. The changes in each version are also different and are listed on the Github homepage, such as [develop/5.0](https://github.com/ossrs/srs/blob/develop/trunk/doc/CHANGELOG.md#changelog), [release/4.0](https://github.com/ossrs/srs/blob/4.0release/trunk/doc/CHANGELOG.md#changelog), [release/3.0](https://github.com/ossrs/srs/tree/3.0release#v3-changes).
  > 1. In addition to adding new features, SRS will also remove unsuitable features, such as RTSP push streaming, srs-librtmp, GB SIP signaling, etc. These features may be useless, inappropriate, or provided in a more suitable way. See [#1535](https://github.com/ossrs/srs/issues/1535) for more information.

<a name='gb28181'></a>

### [GB28181](#gb28181)
* `GB28181`: What about GB28181 status and roadmap
  > 1. GB has been moved to a separate repository [srs-gb28181](https://github.com/ossrs/srs-gb28181), please refer to [#2845](https://github.com/ossrs/srs/issues/2845)
  > 1. For GB usage, please refer to [#1500](https://github.com/ossrs/srs/issues/1500). Currently, GB is still in the [feature/gb28181](https://github.com/ossrs/srs-gb28181/tree/feature/gb28181) branch. It will be merged into develop and then released after it is stable. It is expected to be released in SRS 5.0.
  > 1. SRS support for GB will not be comprehensive, and will only be used as an access protocol. The highly concerned [intercom](https://github.com/ossrs/srs-gb28181/issues/1898) is planned to be supported.

<a name='help'></a>

### [Help](#help)
* No one answers questions in the WeChat group? The art of asking questions in the community?
  > 1. Please search in the various documents of the community first, and do not ask questions that already have answers.
  > 1. Please describe the background of the problem in detail, and show the efforts you have made.
  > 1. Open source community means you need to be able to solve problems yourself. If not, please consider paid consultation.

<a name="hevc"></a>

### [HEVC/H.265](#hevc)

* `RTMP for HEVC`: Does RTMP support HEVC.
  > 1. How to support RTMP FLV HEVC streaming, refer to the [link](https://video.stackexchange.com/a/36922/42693).

<a name='hls-fragments'></a>

### [HLS Fragments](#hls-fragments)
* `HLS Fragment Duration`: How to setup HLS segment duration
  > 1. HLS segment duration is determined by three factors: GOP length, whether to wait for a keyframe (`hls_wait_keyframe`), and segment duration (`hls_fragment`).
  > 1. For example, if the GOP is set to 2s, the segment length is `hls_fragment:5`, and `hls_wait_keyframe:on`, then the actual duration of each TS segment may be around 5~6 seconds, as it needs to wait for a complete GOP before closing the segment.
  > 1. For example, if the GOP is set to 10s, the segment length is `hls_fragment:5`, and `hls_wait_keyframe:on`, then the actual duration of each TS segment is also over 10 seconds.
  > 1. For example, if the GOP is set to 10s, the segment length is `hls_fragment:5`, and `hls_wait_keyframe:off`, then the actual duration of each TS segment is around 5 seconds. The segment does not start with a keyframe, so some players may experience screen artifacts or slower video playback.
  > 1. For example, if the GOP is set to 2s, the segment length is `hls_fragment:2`, and `hls_wait_keyframe:on`, then the actual duration of each TS segment may be around 2 seconds. This way, the HLS delay is relatively low, and there will be no screen artifacts or decoding issues, but the encoding quality may be slightly compromised due to the smaller GOP.
  > 1. Although the segment size can be set to less than 1 second, such as `hls_fragment:0.5`, the `#EXT-X-TARGETDURATION` is still 1 second because it is an integer. Moreover, having too small segments can lead to an excessive number of segments, which is not conducive to CDN caching or player caching, so it is not recommended to set too small segments.
  > 1. If you want to reduce latency, do not set the segment duration to less than 1 second; setting it to 1 or 2 seconds is more appropriate. Because even if it is set to 1 second, due to the player's segment fetching strategy and caching policy, the latency will not be the same as RTMP or HTTP-FLV streams. The minimum latency for HLS is generally over 5 seconds.
  > 1. GOP refers to the number of frames between two keyframes, which needs to be set in the encoder. For example, the FFmpeg parameter `-r 25 -g 50` sets the frame rate to 25fps and the GOP to 50 frames, which is equivalent to 2 seconds.
  > 1. In OBS, there is a `Keyframe Interval(0=auto)` setting. Its minimum value is 1s. If set to 0, it actually means automatic, not the lowest latency setting. For low latency, it is recommended to set it to 1s or 2s.

<a name='http-api'></a>

### [HTTP API](#http-api)
* `HTTP RAW API`: Why removed RAW API, dynamic recording DVR, etc.
  > 1. Due to various problems with the RAW API, it may lead to overuse. The feature has been removed in version 4.0. For detailed reasons, please see [#2653](https://github.com/ossrs/srs/issues/2653).
  > 1. Again, do not use HTTP RAW API for business implementation. This is what your business system should do. You can use Go or Node.js to handle it.

* `Secure HTTP API`: How to do API authentication, API security, etc.
  > 1. Regarding HTTP API authentication and how to prevent everyone from accessing it, it is currently recommended to use Nginx proxy to solve this issue. The support will be enhanced in the future. For details, please see [#1657](https://github.com/ossrs/srs/issues/1657).
  > 1. You can also use HTTP Callback to implement authentication. When pushing or playing a stream, call your business system's API to implement the hook.

* `HTTP Callback`: How to do HTTP callback and authentication.
  > 1. SRS uses HTTP callback for authentication. To learn how to return error codes in HTTP Callback and Response, please refer to this [link](https://stackoverflow.com/a/70358233/17679565).

<a name='api-security'></a> <a name='https'></a> <a name='https-h2-3'></a>

### [HTTPS & HTTP2/3](#https-h2-3)
* `HTTPS`: How to use HTTPS services, API, Callback, Streaming, WebRTC, etc.
  > 1. [HTTPS API](../docs/v4/doc/http-api#https-api) provides transport layer security for the API. WebRTC push streaming requires HTTPS pages, which can only access HTTPS APIs.
  > 1. [HTTPS Callback](../docs/v4/doc/http-callback#https-callback) calls back to HTTPS services. If your server uses the HTTPS protocol, most business systems use HTTPS for security purposes.
  > 1. [HTTPS Live Streaming](../docs/v4/doc/delivery-http-flv#https-flv-live-stream) provides transport layer security for streaming, mainly because HTTPS pages can only access HTTPS resources.
  > 1. Automatically apply for SSL certificates from `letsencrypt` for a single domain, making it easier for small and medium-sized enterprises to deploy SRS and avoiding the high overhead of HTTPS proxies for streaming media businesses. See [#2864](https://github.com/ossrs/srs/issues/2864)
  > 1. Use Nginx or Caddy as reverse proxies for HTTP/HTTPS Proxy to provide unified HTTP/HTTPS services. See [#2881](https://github.com/ossrs/srs/issues/2881)
* `HTTP2`: How to do HTTP2-FLV or HTTP2 HLS, etc.
  > 1. SRS will not implement HTTP2 or HTTP3, but instead recommends using reverse proxies to convert protocols, such as Nginx or Go.
  > 1. Since HTTP is a very mature protocol, existing tools and reverse proxy capabilities are very comprehensive, and SRS does not need to implement a complete protocol.
  > 1. SRS has implemented a simple HTTP 1.0 protocol, mainly providing API and Callback capabilities.

<a name='latency'></a>

### [Latency](#latency)
* `Latency`: How to reduce latency, how to do low-latency live streaming, and how much latency WebRTC has.
  > 1. Live streaming latency is generally 1 to 3 seconds, WebRTC latency is around 100ms, why is the latency of the self-built environment so high?
  > 1. The most common reason for high latency is using the VLC player, which has a latency of tens of seconds. Please switch to the SRS H5 player.
  > 1. Latency is related to each link, not just SRS reducing latency. It is also related to the push tool (FFmpeg/OBS) and the player. Please refer to [Realtime](../docs/v4/doc/sample-realtime) and follow the steps to set up a low-latency environment. Don't start with your own fancy operations, just follow the documentation.
  > 1. If you still find high latency after following the steps, how to troubleshoot? Please refer to [#2742](https://github.com/ossrs/srs/issues/2742)
* `HLS Latency`: How to reduce the latency of HLS.
  > 1. HLS has a large delay, and it takes a long time to watch after switching content. How to reduce HLS latency? Refer to the [link](https://video.stackexchange.com/a/36923/42693).
  > 1. How to config SRS for [HLS Latency](../docs/v6/doc/hls#hls-low-latency)
* `Benchmark`: How to benchmark and testing latency.
  > 1. How to measure and optimize live streaming latency, latency in different stages and protocols, how to improve and measure latency, refer to this [link](https://stackoverflow.com/a/70402476/17679565).

<a name='performance'></a> <a name='memory'></a>

### [Performance](#performance) and [Memory](#memory)
* `Performance`: How to do performance optimization, concurrency, stress testing, and memory leaks
  > 1. Performance is a comprehensive topic, including the quality of the project, the capacity and concurrency it supports, how to optimize performance, and even memory issues, such as memory leaks (leading to reduced performance), out-of-bounds and wild pointer problems.
  > 1. If you need to understand the concurrency of SRS, you must divide it into separate concurrency for live streaming and WebRTC. Live streaming can use [srs-bench](https://github.com/ossrs/srs-bench), and WebRTC can use the [feature/rtc](https://github.com/ossrs/srs-bench/tree/feature/rtc) branch for stress testing to obtain the concurrency supported by your hardware and software environment under specific bitrates, latency, and business characteristics.
  > 1. SRS also provides official concurrency data, which can be found in [Performance](https://github.com/ossrs/srs/blob/4.0release/trunk/doc/PERFORMANCE.md#performance). It also explains how to measure this concurrency, the conditions under which the data is obtained, and specific optimization code.
  > 1. If you need to investigate performance issues, memory leaks, or wild pointer problems, you must use system-related tools such as perf, valgrind, or gperftools. For more information, please refer to [SRS Performance (CPU), Memory Optimization Tool Usage](https://www.jianshu.com/p/6d4a89359352) or [Perf](../docs/v4/doc/perf).
  > 1. It is important to note that valgrind has been supported since SRS 3.0 (inclusive), and the ST patch has been applied.

<a name='player'></a>

### [Player](#player)

* `Player`: How to choose players and OS platforms.
  > 1. How to choose a live streaming player, as well as the introduction of corresponding protocols and latency, recommend RTMP for playing HTTP-FLV/HLS/WebRTC: refer to the [link](https://stackoverflow.com/a/70358918/17679565)
  > 1. How to play HTTP-FLV with HTML5, MSE compatibility, HTML5 players on various platforms, and how to use WASM to play FLV on iOS: refer to the [link](https://stackoverflow.com/a/70429640/17679565)

<a name='rtsp'></a>

### [RTSP](#rtsp)
* `RTSP`: How to support RTSP streaming, RTSP server, RTSP playback, etc.
  > 1. SRS supports pulling RTSP with Ingest, but does not support pushing RTSP stream to SRS, which is not the correct usage. For detailed reasons, please refer to [#2304](https://github.com/ossrs/srs/issues/2304).
  > 1. Of course, RTSP server and RTSP playback will not be supported either, please refer to [#476](https://github.com/ossrs/srs/issues/476).
  > 1. If you need a large number of camera connections, such as 10,000, using FFmpeg may be more difficult. For such large-scale businesses, the recommended solution is to use ST+SRS code to implement an RTSP forwarding server.
* `Browser RTSP`: How to play RTSP streams in a browser
  > 1. How to play RTSP streams in HTML5, using FFmpeg to pull RTSP streams, and how to reduce latency. Refer to this [link](https://stackoverflow.com/a/70400665/17679565).
  > 1. How to watch RTSP streams from IP cameras in a web browser. Refer to this [link](https://stackoverflow.com/a/77335988/17679565).
* How can we use a single server to receive all IPC streams, convert internal network RTSP to public network live streaming or RTC? Refer to this [link](https://stackoverflow.com/a/70901153/17679565) for more information.

<a name='solution'></a>

### [Solution](#solution)
* `Media Stream Server`: What's the difference between media servers.
  > 1. How to do live streaming or calls, the differences and focus points between live streaming and RTC (Real-Time Communication), refer to this [link](https://stackoverflow.com/a/70401471/17679565).
  > 1. How to do live streaming between Android devices, including live streaming servers and players, and how to transfer video between two Android devices, refer to this [link](https://stackoverflow.com/a/70400557/17679565).
  > 1. Recommended media servers and protocol introductions, various protocols used in live streaming, refer to this [link](https://stackoverflow.com/a/70400495/17679565).
* `Raspberry Pi`: How to run in Raspberry Pi.
  > 1. Remote control of Raspberry Pi camera and car, live streaming and pure WebRTC solution, refer to this [link](https://stackoverflow.com/a/70675353/17679565).
* `Others`: Other solutions and common questions.
  > 1. Why do two RTMP streams gradually go out of sync, and how can SRT or WebRTC be used to keep two different streams synchronized? Refer to this [link](https://stackoverflow.com/a/71273229/17679565).
  > 1. How does the SRS origin cluster support HLS, and how are the sliced files distributed? Refer to this [link](https://stackoverflow.com/a/70416358/17679565).
  > 1. How can the SRS origin cluster be expanded, and how can MESH communication issues be resolved? Refer to this [link](https://stackoverflow.com/a/70416254/17679565).
  > 1. Record video using WebRTC and use SRS to convert WebRTC to RTMP for recording. Refer to this [link](https://stackoverflow.com/a/70402235/17679565).
  > 1. The differences between RTSP and RTP, and between RTSP and WebRTC. Refer to this [link](https://stackoverflow.com/a/70401047/17679565).
  > 1. The meaning of SRS log abbreviations and connection-based logs. Refer to this [link](https://stackoverflow.com/a/70374760/17679565).
  > 1. Why FPS is not accurate, the meaning of TBN, and conversion errors. Refer to this [link](https://stackoverflow.com/a/70373364/17679565).
  > 1. What is RTMP's tcURL, and how to get the stream address? Refer to this [link](https://stackoverflow.com/a/70920881/17679565).
  > 1. How to play RTMP streams in H5 without using Flash and Nginx? Refer to this [link](https://stackoverflow.com/a/70920989/17679565).
  > 1. Can WebRTC replace RTMP, and is live streaming only possible with WebRTC? Refer to this [link](https://stackoverflow.com/a/75491330/17679565).
  > 1. How to do a video live stream through a VPS? Refer to this [link](https://video.stackexchange.com/a/36925/42693).

<a name='source-cleanup'></a>

### [Source Cleanup](#source-cleanup)
* `Source Cleanup`: How to fix memory growth for a large number of streams
  > 1. The Source object for push streaming is not cleaned up, and memory will increase as the number of push streams increases. For now, you can use [Gracefully Quit](https://github.com/ossrs/srs/issues/413#issuecomment-917771521) as a workaround, and this issue will be addressed in the future. See [#413](https://github.com/ossrs/srs/issues/413)
  > 1. To reiterate, you can use [Gracefully Quit](https://github.com/ossrs/srs/issues/413#issuecomment-917771521) as a workaround. Even if this issue is resolved in the future, this solution is the most reliable and optimal one. Restarting is always a good option.

<a name='threading'></a>

### [Threading](#threading)

* Why doesn't SRS support multi-threading, and how can you scale your SRS? Refer to this [link](https://stackoverflow.com/a/75566192/17679565) for more information.

<a name='video-guides'></a>

### [Video Guides](#video-guides)

Here is the video material for the Q&A session, which provides a detailed explanation of a certain 
topic. If your question is similar, please watch the video directly:

* [Unlock the Power of SRS: Real-World Use Cases and Boosting Your Business with Simple Realtime Server.](https://youtu.be/WChYr6z7EpY)
* [Ultra Low Latency Streaming with OBS WHIP](https://youtu.be/SqrazCPWcV0)

<a name='webrtc-cluster'></a>

### [WebRTC Cluster](#webrtc-cluster)
* `WebRTC+Cluster`: Does SRS support WebRTC clustering?
  > 1. WebRTC clustering is not the same as live streaming clustering (Edge+Origin Cluster), but it is called WebRTC cascading. Please refer to [#2091](https://github.com/ossrs/srs/issues/2091)
  > 1. In addition to the clustering solution, SRS will also support the Proxy solution, which is simpler than clustering and will have scalability and disaster recovery capabilities. Please refer to [#3138](https://github.com/ossrs/srs/issues/3138)

<a name='webrtc-live'></a>

### [WebRTC Live](#webrtc-live)
* `WebRTC+Live`: How to convert Live stream with WebRTC.
  > 1. For the conversion between WebRTC and RTMP, such as RTMP2RTC (RTMP push stream RTC playback) or RTC2RTMP (RTC push stream RTMP playback), you must specify the conversion configuration. Audio transcoding is not enabled by default to avoid significant performance loss. Please refer to [#2728](https://github.com/ossrs/srs/issues/2728)
  > 1. If SRS 4.0.174 or earlier works, but it does not work after updating, it is because `rtc.conf` does not enable RTMP to RTC by default. You need to use `rtmp2rtc.conf` or `rtc2rtmp.conf`. Please refer to 71ed6e5dc51df06eaa90637992731a7e75eabcd7
  > 1. In the future, the conversion between RTC and RTMP will not be enabled automatically, because SRS must consider the independent RTMP and independent RTC scenarios. The conversion scenario is just one of them, but due to the serious performance problems caused by the conversion scenario, it cannot be enabled by default, which will cause major problems in independent scenarios.
* How can WebRTC support one-to-many broadcasting and accommodate a large number of streaming clients? For WebRTC to be used in live streaming, you can refer to this [link](https://stackoverflow.com/a/71019599/17679565).
* How to achieve low-latency live streaming with FFmpeg and HTML5, using Raspberry Pi as a streaming device for remote assistance in medical equipment. For more information, refer to this [link](https://stackoverflow.com/a/71984507/17679565).

<a name='webrtc'></a>

### [WebRTC](#webrtc)
* `WebRTC`: Questions about WebRTC push and pull streams or conferences
  > 1. WebRTC is much more complicated than live streaming. For many WebRTC issues, do not submit issues in SRS, but search for the problem on Google first. If you do not have this ability, do not use WebRTC. There are many pitfalls, and if you do not have the ability to crawl out of them, do not jump into them.
  > 1. A common issue is that the Candidate setting is incorrect, causing the push and pull streams to fail. For details, see the WebRTC usage instructions: [#307](https://github.com/ossrs/srs/issues/307)
  > 1. There are also issues with UDP ports being inaccessible, which may be due to firewall settings or network issues. Please use tools to test, refer to [#2843](https://github.com/ossrs/srs/issues/2843)
  > 1. Another common issue is the conversion between RTMP and WebRTC. Please see the description above <a name='webrtc-live' href='#webrtc-live'>#webrtc-live</a>.
  > 1. Then there are WebRTC permission issues, such as being able to push streams locally but not on the public network. This is a Chrome security setting issue. Please refer to [#2762](https://github.com/ossrs/srs/issues/2762)
  > 1. There are also less common issues, such as not being able to play non-HTTPS SRS streams with the official player. This is also a Chrome security policy issue. Please refer to [#2787](https://github.com/ossrs/srs/issues/2787)
  > 1. When mapping ports in docker, if you change the port, you need to modify the configuration file or specify it through eip. Please refer to [#2907](https://github.com/ossrs/srs/issues/2907)

* `WebRTC RTMP`: Questions related to WebRTC and live streaming.
  > 1. For WebRTC to RTMP conversion, using WebRTC for live streaming, HTML5 push streaming, or low-latency live streaming, refer to this [link](https://stackoverflow.com/a/70402692/17679565).
  > 1. For RTMP to WebRTC conversion, low-latency live streaming solutions, HTTP-TS, and HEVC live streaming, refer to this [link](https://stackoverflow.com/a/75569582/17679565).
  > 1. To learn how to use WebRTC to push streams to YouTube, while also recording and watching streams with WebRTC, refer to this [link](https://stackoverflow.com/a/76913341/17679565).

* What are the roles and application scenarios of WebRTC's SFU (Selective Forwarding Unit), and how do different SFUs compare in functionality? For more information, refer to this [link](https://stackoverflow.com/a/75491178/17679565).

<a name='websocket'></a>

### [Websocket](#websocket)
* `WebSocket/WS`: How to support WS-FLV or WS-TS?
  > 1. You can use a Go proxy to convert it once, with a few lines of key code for stability and reliability. Please refer to [mse.go](https://github.com/winlinvip/videojs-flow/blob/master/demo/mse.go)

## Q&A

### WebRTC Demo Failed

**Question** Failed to join RTC room or start conversation
> According to the 5.0 documentation for [SFU: One to One](../docs/v5/doc/webrtc#sfu-one-to-one), I have completed the following configurations:
> 1. Configured the CANDIDATE to use the internal IP address 192.168.100.140.
> 1. Used Docker to start RTC service, Signaling service, and HTTPS service.
> 1. Successfully accessed http://192.168.100.140/demos/ and was able to open it without any issues.

> However, when I click on "Start Conversation" or "Join Room," my computer's camera briefly lights up but there is no response. 
> I have already used a self-signed OpenSSL key and crt certificate, but encountered a TLS certificate handshake error.

**Answer**
> 1. First, it is important to clarify that you strictly followed the documentation.[SFU: One to One](../docs/v5/doc/webrtc#sfu-one-to-one)
> 1. In order to identify the cause, you can investigate potential factors such as certificate problems, HTTPS connection issues, and browser permission settings etc.

## Deleting

Refer this FAQ by:

```text
See FAQ:
* Chinese: https://ossrs.net/lts/zh-cn/faq
* English: https://ossrs.io/lts/en-us/faq
```

Duplicate or pre-existing issues may be removed, as they are already present in the issues or FAQ section:

```
For discussion or idea, please ask in [discord](https://discord.gg/yZ4BnPmHAd).

This issue will be eliminated, see #2716
```

```
Please ask this question on Stack Overflow using the [#simple-realtime-server tag](https://stackoverflow.com/questions/tagged/simple-realtime-server).

If want some discussion, here's the [discord](https://discord.gg/yZ4BnPmHAd).

This issue will be eliminated, see #2716
```

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/pages/faq-en)
