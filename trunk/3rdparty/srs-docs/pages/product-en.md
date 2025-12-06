# Product

About milestones of SRS.

* [Release 7.0](#release70), 2025~Now, Code: Kai
* [Release 6.0](#release60), 2023~2025, Code: Hang
* [Release 5.0](#release50), 2022~2023, Code: Bee
* [Release 4.0](#release40), 2020~2021, Code: Leo
* [Release 3.0](#release30), 2018~2019, Code: OuXuli
* [Release 2.0](#release20), 2015~2017, Code: ZhouGuowen
* [Release 1.0](#release10), 2013~2014, Code: HuKaiqun

For detail features of SRS, please see [FEATURES](https://github.com/ossrs/srs/blob/develop/trunk/doc/Features.md#features).

## History

Let's briefly introduce the history of SRS in reverse order.

From June to December 2025, AI became a major contributor to SRS development. AI was used extensively to cleanup
issues, review pull requests, and improve unit test coverage from 40% to 88%. AI also helped implement new features
and enhancements. While AI is now a major contributor, the human maintainer still reviews each line of code generated
by AI and makes all final decisions, ensuring code quality and project direction remain under human oversight.

In January 2025, the paid star planet was closed and all paid support services were discontinued. The decision was
made to build a purely volunteer-driven community without any monetization. While donations, sponsors, and backers
are still needed and welcomed to support the project, no special paid services will be provided to anyone. With AI
now working very well as a question answerer and problem solver, anyone who needs help should use AI first.

In January 2023, Star exceeded 20K and launched the Paid Star Planet. Oryx supported Virtual Live Broadcasting,
confirmed the development codename for version 6.0 as Hang, and introduced new TOC rules.

In November 2022, the SRS TOC and developer community were established, with the number of active developers reaching 47. 
SRS 5.0 was completed, with new features including Forward Enhancement, GB28181, Windows, Apple M1, RISCV, MIPS, Loongson, 
DASH Enhancement, AddressSanitizer, Prometheus Exporter, SRT Enhancement, Unity WebRTC, WHIP, and WebRTC over TCP.

In January 2021, the Open Source Technology Committee was established. In April, SRS shared best practices on Alibaba 
Cloud at LVS and supported AV1. In May, RTC documentation was improved, including RTC and RTMP conversion, one-to-one 
calls, live streaming with guests, and multi-person conferences. In May, SRT was improved.

In 2020, SRS 4.0 development began. In January, SRT was supported, K8S was supported in February, and WebRTC was 
supported in March. In June, SRS 3.0 was released, and in October, the official App was launched along with Flutter. 
In November, HTTPS was supported, and in December, it became the world's top open-source video server.

In December 2019, SRS 3's core protocols HTTP/RTMP had a coverage rate of 95%, with an overall coverage rate of 42%. 
Key progress was made in stability work, and it entered the Alpha release stage. SRS fully supported Docker.

In February 2018, source station clustering was supported, and the live streaming cluster (source station cluster and 
edge cluster) was improved.

On March 3, 2017, SRS 2.0 r0 was officially released, delayed by 2 years mainly due to being too busy with work and not 
having enough time to maintain SRS. In early 2017, after changing jobs and having 2 months of dedicated work, r0 was 
released. It took 869 days (+100%), 234 updates (+5%), 1550 new submissions (+80%), averaging 1.78 submissions per day 
(-60%), adding 26,900 lines of code (+45%), and resolving 229 issues (+27%).

February 2017, support for recording as MP4 and MP4 format. April support for Haivision encoder, June support for DASH.

May 2016, compared the differences between SRS/BMS/NGINX and CDN. At that time, tried open-source commercialization 
solutions, provided SRS open-source free version, BMS was a commercial paid version, but this path was not easy in China; 
China's open-source needs to follow its own unique path, and foreign models may not necessarily work when brought over.

October 2014, started the development of SRS2.0, with an estimated development cycle of about 6 months. The main goal
was to fully understand and master ST, simplify the server's client model, and improve other small features. The bigger 
direction was support for 3.0+.

October 2014, SRS1.0 beta released. From 0 to 1.0, SRS took 1 year, 17 milestones, 7 development versions, 223 revisions, 
43,700 lines of functional code, 15,616 lines of utest code, 1,803 commits, 161 bugs and features, resolved 117, can 
run on 1 platform (Linux), supports 4 types of CPUs (x86/x64/arm/mips), 11 core features (origin, edge, vhost, 
transcode, ingest, dvr, forward, http-api, http-callback, reload, tracable-log), 35 feature points, 58 wiki articles, 
SRS QQ group has 245 members, 141 active members, 2 main authors, 12 contributors, 14 donors, at least ChinaCache, VeryCloud, 
VeryCDN, Tsinghua TV Station are using or based on SRS to modify their own servers, hundreds of companies in various 
industries are using SRS, mainly including video surveillance, mobile, online education, showrooms and KTV, interactive 
video, TV stations, IoT, students.

March 2014, entered the feedback period, Raspberry Pi, Extreme Router, Cubieboard and other embedded devices were asked 
if they could be supported. I bought a Raspberry Pi myself, successfully ran it, and fixed a bug in ST. From this time 
on, it was a period of feature explosion, receiving feedback from group members. Transcoding, forwarding, collection, 
and recording were all tasks during this period.

November 2013, joined chnvideo to be responsible for R&D management. Later, chnvideo wanted to make encoders, and the 
encoders needed to output to RTMP servers. Since nginx-rtmp often had problems, they decided to use my SRS to replace 
nginx-rtmp. During the encoder's launch process, I gradually improved SRS, which was a rapid growth period for SRS. 
Opening the server allowed customers to use our encoder better, and our encoder could support pull mode. This stage 
was mainly the origin server stage.

October 2013, SRS was created. SRS was a simple live streaming origin server I wrote after leaving ChinaCache in September 
2013, referring to nginx-rtmp. The colleague who took over my work at ChinaCache could also see how the server was built 
step by step. ChinaCache's customers could also use this origin server, as dealing with those messy origin servers was too
troublesome. I wanted to use my spare time to build a product that was not easily influenced by customers, only adding 
features that followed core values.

## Vision

SRS is the world's top open-source video server, supporting live streaming and WebRTC, applicable to various video 
scenarios and industries.

* Mission: Empower small and micro enterprises and developers with audio and video capabilities without barriers.
* Vision: Every small and micro enterprise has audio and video capabilities.
* Values: Simplicity, openness, and pragmatism.

For a detailed interpretation, please see Welcome to SRS: Mission, Vision, and Values.

## Release 7.0

Code name: Kai. Named by TOC member [Haibo Chen](https://github.com/duiniuluantanqin). Development started August 2024, planned for release by the end of 2026.

> Code Name Story: I am Haibo Chen, a core maintainer of SRS and a TOC member. The code name Kai is inspired by my son Chen Kaiqi's name. As a father, I aim to set a good example by doing meaningful and interesting work. I appreciate the support and collaboration from everyone in the community, making it more vibrant and warm. This upgrade aims to provide users with more powerful features and a smoother experience, laying a strong foundation for SRS's future.

**New Protocols & Major Features**

- [x] RTSP Playback - Support for viewing streams over RTSP protocol. [#4333](https://github.com/ossrs/srs/pull/4333)
- [x] RTMPS Server - Support for RTMP over SSL server. [#4443](https://github.com/ossrs/srs/pull/4443)
- [x] Proxy Cluster - Support for proxy server functionality with more stream paths. [#4158](https://github.com/ossrs/srs/pull/4158)
- [x] IPv6 Support - Full IPv6 support for all protocols: RTMP, HTTP/HTTPS, WebRTC, SRT, RTSP. [#4457](https://github.com/ossrs/srs/pull/4457)

**WebRTC Enhancements**

- [x] VP9 Codec - Support for VP9 codec in WebRTC-to-WebRTC streaming. [#4548](https://github.com/ossrs/srs/pull/4548)
- [x] G.711 Audio - Support for G.711 (PCMU/PCMA) audio codec for WebRTC. [#4075](https://github.com/ossrs/srs/pull/4075)
- [x] HEVC Support - RTMP-to-WebRTC and WebRTC-to-RTMP conversion with HEVC. [#4289](https://github.com/ossrs/srs/pull/4289), [#4349](https://github.com/ossrs/srs/pull/4349)
- [x] Audio Jitter Buffer - RTC audio packet jitter buffer for improved quality. [#4295](https://github.com/ossrs/srs/pull/4295)
- [x] Dual Video Track - Support for dual video track with multiple codecs (AVC and HEVC) in RTMP2RTC bridge. [#4413](https://github.com/ossrs/srs/pull/4413)
- [x] WHEP Support - WebRTC HTTP Egress Protocol for playback. [#3404](https://github.com/ossrs/srs/pull/3404)

**HLS Improvements**

- [x] HLS fMP4 - HLS protocol support for fMP4 segments for HEVC/LLHLS. [#4159](https://github.com/ossrs/srs/pull/4159)
- [x] Query String Support - Support query string in hls_key_url for JWT tokens. [#4426](https://github.com/ossrs/srs/pull/4426)
- [x] Master M3U8 Path - Support hls_master_m3u8_path_relative for reverse proxy. [#4338](https://github.com/ossrs/srs/pull/4338)

**Architecture Changes**

- [x] Single-Thread Architecture - Removed multi-threading support, changed to single-thread architecture for simplicity. [#4445](https://github.com/ossrs/srs/pull/4445)
- [x] Always Enable WebRTC - WebRTC is now always enabled and enforces C++98 compatibility. [#4447](https://github.com/ossrs/srs/pull/4447)
- [x] Always Enable SRT - SRT protocol is now always enabled. [#4460](https://github.com/ossrs/srs/pull/4460)
- [x] Unified Server - Merged SRT and RTC servers into unified SrsServer. [#4459](https://github.com/ossrs/srs/pull/4459)
- [x] HTTP Parser Upgrade - Upgraded from http-parser to llhttp. [#4469](https://github.com/ossrs/srs/pull/4469)
- [x] Stream Publish Token - Implemented stream publish token system to prevent race conditions across all protocols. [#4452](https://github.com/ossrs/srs/pull/4452)

**DVR & Recording**

- [x] HEVC DVR to FLV - Support H.265 FLV fragments for DVR. [#4296](https://github.com/ossrs/srs/pull/4296)
- [x] MP4 DVR Sync - Fix audio/video synchronization issues in WebRTC recordings. [#4230](https://github.com/ossrs/srs/pull/4230)

**Code Quality & Build**

- [x] Clang Format - Use clang-format to unify coding style. [#4366](https://github.com/ossrs/srs/pull/4366), [#4433](https://github.com/ossrs/srs/pull/4433)
- [x] Memory Management - Multiple memory leak fixes and improved error handling throughout the codebase.
- [x] Build Improvements - Better dependency detection and reporting. [#4293](https://github.com/ossrs/srs/pull/4293)

**Other Improvements**

- [x] GB28181 Changes - Removed embedded SIP server, enforce external SIP usage. [#4466](https://github.com/ossrs/srs/pull/4466)
- [x] RTMP Config Section - Moved RTMP configs to rtmp{} section. [#4454](https://github.com/ossrs/srs/pull/4454)
- [x] Cloud Features Removed - Removed cloud CLS and APM. [#4456](https://github.com/ossrs/srs/pull/4456)

SRS 7.0 development started at 2024.08.

## Release 6.0

Development codename: Hang. Named by TOC [John](https://github.com/xiaozhihong). Released on December 3, 2024, now the stable version replacing SRS 5.0.

> Note: The development codename Hang is named by TOC [John](https://github.com/xiaozhihong), and the specific meaning
> is left for everyone to appreciate and ponder.

**HEVC/H.265 Support (Major Feature)**

- [x] HEVC over RTMP/HTTP-FLV - Full H.265 support for RTMP and HTTP-FLV streaming. [#465](https://github.com/ossrs/srs/issues/465)
- [x] HEVC over HLS - H.265 support in HLS delivery.
- [x] HEVC over HTTP-TS - H.265 support for MPEG-TS streaming.
- [x] HEVC over SRT - H.265 support for SRT protocol.
- [x] HEVC DVR to MP4 - Record H.265 streams to MP4 format.
- [x] HEVC DVR to FLV - Record H.265 streams to FLV fragments.

**WebRTC Enhancements**

- [x] WHEP Support - WebRTC-HTTP Egress Protocol for playback.
- [x] OPUS Stereo SDP option - Support for stereo audio in WebRTC.
- [x] Configurable AAC to Opus bitrate - Customize transcoding bitrate.

**Security & Authentication**

- [x] IP Whitelist - Security support for HTTP-FLV, HLS, WebRTC, and SRT. [#3902](https://github.com/ossrs/srs/pull/3902)
- [x] Basic Authentication - HTTP API authentication feature. [#3458](https://github.com/ossrs/srs/pull/3458)

**SRT Improvements**

- [x] Upgraded libsrt to v1.5.3 - Latest SRT library version.
- [x] Configurable default_streamid - Customizable SRT stream IDs.
- [x] Reduced SRT-to-RTC latency - Down to 200ms.

**HLS Features**

- [x] Kick-off HLS clients - Ability to disconnect HLS clients.

**Other Improvements**

- [x] Smart pointers (SrsUniquePtr and SrsSharedPtr) - Improved memory management and fixed multiple memory leak issues. [#4089](https://github.com/ossrs/srs/pull/4089), [#4109](https://github.com/ossrs/srs/pull/4109)
- [x] GB28181 protocol - Support for external SIP servers. [#4101](https://github.com/ossrs/srs/pull/4101), [srs-sip](https://github.com/ossrs/srs-sip)

SRS 6.0 was released at 2024.12, see [6.0-r0](https://github.com/ossrs/srs/releases/tag/v6.0-r0).

# Release 5.0

Development codename: Bee, representing that SRS officially begins open-source community-driven development. 
Collaboration is the main feature, and it constantly reminds us that to do well in open-source projects, we need to put 
in time every day, just like bees. Thanks to all the 300+ developers and the core developers of the Technical Committee, 
especially the TOC for their continuous efforts. In June 2021, SRS entered the Mulans Open Source Community incubation, 
thanks to mentors Alibaba Cloud Chen Xu, Professor Zhou Minghui, Tencent Dan Zhihao, and the strong support of Mulan 
Community Director Yang Liyun. Special thanks to Tencent's Tommy (Li Yutao), Eddie (Xue Di), Leo (Liu Lianxiang), 
Vulture (Li Zhicheng), Dragon (Lan Yulong), and all developer leaders for their recognition of SRS and support for 
developers to participate in open-source contributions. Special thanks to community managers Geng Hang and Liu Qi for
their contributions to community promotion and development.

- [x] Support for amd/armv7/aarch64 multi-CPU architecture Docker images. [#3058](https://github.com/ossrs/srs/issues/3058)
- [x] Enhanced Forward, supporting dynamic Forward, allowing flexible customization of forwarding strategies. [#2799](https://github.com/ossrs/srs/issues/2799)
- [x] GB28181, supporting GB2016 standard, built-in SIP signaling, and TCP port reuse for transmission. [#3176](https://github.com/ossrs/srs/issues/3176)
- [x] Windows, supporting Cygwin compilation, pipeline packaging, and GITEE mirror downloads. [#2532](https://github.com/ossrs/srs/issues/2532)
- [x] Apple M1, supporting Apple M1 chip, new MacPro compilation, and debugging. [#2747](https://github.com/ossrs/srs/issues/2747)
- [x] RISCV architecture support, modifying ST assembly to support RISCV CPU architecture. [#3115](https://github.com/ossrs/srs/issues/3115)
- [x] MIPS architecture support, Cygwin platform support, and ARMv7 and AARCH64 support.
- [x] Loongarch, supporting Loongson architecture and Loongarch64 server platform. [#2689](https://github.com/ossrs/srs/issues/2689)
- [x] Enhanced DASH, solving DASH freezing issues, reaching a commercially viable standard. [#3240](https://github.com/ossrs/srs/issues/3240)
- [x] Support for Google Address Sanitizer, solving wild pointer location issues. [#3216](https://github.com/ossrs/srs/issues/3216)
- [x] Prometheus Exporter, supporting cloud-native observability capabilities, and also supporting Tencent Cloud CLS and APM docking. [#2899](https://github.com/ossrs/srs/issues/2899)
- [x] Enhanced SRT, coroutine-native SRT improvements, more convenient maintenance, and stability. [#3010](https://github.com/ossrs/srs/issues/3010)
- [x] Unity WebRTC, supporting Unity platform docking with SRS, using WHIP protocol. srs-unity
- [x] Support for WHIP protocol, push and pull streams. [#2324](https://github.com/ossrs/srs/issues/2324)
- [x] WebRTC over TCP, supporting TCP transmission of WebRTC, and TCP port reuse. [#2852](https://github.com/ossrs/srs/issues/2852)
- [x] Support for HTTP API, HTTP Stream, HTTP Server, and WebRTC TCP port reuse. [#2881](https://github.com/ossrs/srs/issues/2881)

SRS 5.0 was released at 2023.12, see [5.0-r0](https://github.com/ossrs/srs/releases/tag/v5.0-r0). 

## Release 4.0

Development codename: Leo. Thanks to my streaming media and management career leader, former ChinaCache VP Fu 
Liang (Leo), for his firm support in the development of streaming media and for personally coaching and communicating 
when I first started leading a team. Thanks to the leaders I have met in my decade-long career, including He Li and 
Zhu Hui from Datang, Shu Shi from Microsoft, Fu Liang, Zhang Wei, Michael, Liu Qi, Miao Quan, and Wen Jie from ChinaCache, 
Yu Bing from Kuaishou, Yang Mohan and Lei Jian from chnvideo, Bao Yan from LVS, and Shu Du, Zhi Fan, Hua Da,
Wen Jing, Shi Hao, and Huan Jian from Alibaba. Thanks to my classmates who have grown and coded together.

- [x] Support WebRTC push and playback, refer to [#307](https://github.com/ossrs/srs/issues/307)
- [x] Support RTMP to RTC, low-latency live streaming scenarios, refer to [#307](https://github.com/ossrs/srs/issues/307)
- [x] Support RTC to RTMP, conference recording, refer to [#307](https://github.com/ossrs/srs/issues/307)
- [x] Support RTC single-port reuse, avoiding multi-port issues, refer to [#307](https://github.com/ossrs/srs/issues/307)
- [x] Improve HTTP-API, support WebRTC and HLS, refer to [#2578](https://github.com/ossrs/srs/issues/2587), [#2483]((https://github.com/ossrs/srs/issues/2483), [#2509](https://github.com/ossrs/srs/issues/2509)
- [x] Enhance HTTPS, support HTTPS-FLV, HTTPS-API, HTTPS-Callback
- [x] Support Docker and K8S docking, cloud-native transformation, refer to [#1579](https://github.com/ossrs/srs/issues/1579), [#1595](https://github.com/ossrs/srs/issues/1595)
- [x] Support RTC client network switching, multi-network card switching issues, refer to [#307](https://github.com/ossrs/srs/issues/307)
- [x] Support regression testing, RTC automatic testing, refer to srs-bench
- [x] [experimental] Support SRT push, widely supported new protocol in broadcasting. Refer to: [#1147](https://github.com/ossrs/srs/issues/1147).
- [x] [feature] Support GB28181 push, camera push through national standard protocol. Refer to: [#1500](https://github.com/ossrs/srs/issues/1500).

SRS 4.0 was released at 2022.06, see [4.0-r0](https://github.com/ossrs/srs/releases/tag/v4.0-r0).

## Release 3.0

Development codename: Ou Xuli. Thanks to my university teacher, Mr. Ou Xuli (Ou Gong), who created Zhongqin 
Online, allowing me to practice real knowledge while studying software theory in college. Thanks to my classmates at 
Zhongqin, Chen Zhe, Liu Xiaojing, Sheng Xiehua, Yi Nianhua, Ma Yan, and other classmates at Zhongqin. I hope SRS can 
carry our initial dreams and go further.

[SRS Release 3.0](https://github.com/ossrs/srs/tree/3.0release), in the development stage. The main goals are:

- [x] Support NGINX-RTMP's EXEC feature. Refer to: [#367](https://github.com/ossrs/srs/issues/367).
- [x] Support NGINX-RTMP's DVR control module feature. Refer to: [#459](https://github.com/ossrs/srs/issues/459).
- [x] Support secure, readable, and writable HTTP API (HTTP Security Raw API). Refer to: [#470](https://github.com/ossrs/srs/issues/470), [#319]((https://github.com/ossrs/srs/issues/319), [#459](https://github.com/ossrs/srs/issues/459).
- [x] Support DVR as MP4 files. Refer to: [#738](https://github.com/ossrs/srs/issues/738).
- [x] Support screenshots, HttpCallback, and Transcoder in two ways. Refer to: [#502](https://github.com/ossrs/srs/issues/502).
- [x] Rewrite error and log handling, use complex errors, and simplify logs. Refer to: [#913](https://github.com/ossrs/srs/issues/913).
- [x] Rewrite error handling workflow, accurately define exceptions. Refer to: [#1043](https://github.com/ossrs/srs/issues/1043).
- [x] Learn English, rewrite English WIKI. Refer to: [#967](https://github.com/ossrs/srs/issues/967).
- [x] Support source station cluster, load balancing, and hot backup. Refer to: [#464](https://github.com/ossrs/srs/issues/464), RTMP 302.
- [x] Add UTest, covering core critical logic code. Refer to: [#1042](https://github.com/ossrs/srs/issues/1042).
- [x] [experimental] Support MPEG-DASH, possible future standard. Refer to: [#299](https://github.com/ossrs/srs/issues/299).

SRS 3.0 was released at 2020.06, see [3.0-r0](https://github.com/ossrs/srs/releases/tag/v3.0-r0).

## Release 2.0

Development codename: ZhouGuowen. Thanks to my high school teacher Mr. Zhou Guowen for teaching me to be 
independent and opening a new chapter in my life.

[SRS release 2.0](https://github.com/ossrs/srs/tree/2.0release) is expected to have a development cycle of about one year. 
The main goals are:

- [x] Translate Chinese wiki into English.
- [x] Improve performance, support 10k+ playback and 4.5k+ streaming. See: [#194](https://github.com/ossrs/srs/issues/194), [#237](https://github.com/ossrs/srs/issues/237) and [#251](https://github.com/ossrs/srs/issues/251).
- [x] srs-librtmp supports sending h.264 and aac raw streams. See: [#66](https://github.com/ossrs/srs/issues/66) and [#212](https://github.com/ossrs/srs/issues/212).
- [x] Learn and simplify st, only keep linux/arm part of the code. See: [#182](https://github.com/ossrs/srs/issues/182).
- [x] srs-librtmp supports Windows platform. See: bug [#213](https://github.com/ossrs/srs/issues/213), and srs-librtmp
- [x] Simplify handshake, use template method instead of union. See: [#235](https://github.com/ossrs/srs/issues/235).
- [x] srs-librtmp supports hijacking IO, applied to srs-bench.
- [x] Support real-time mode, with a minimum delay of 0.1 seconds. See: [#257](https://github.com/ossrs/srs/issues/257).
- [x] Support allowing and prohibiting clients from streaming or playing. See: [#211](https://github.com/ossrs/srs/issues/211).
- [x] DVR supports custom file path and DVR http callback.
- [x] Commercially available built-in HTTP server, referring to GO's http module. See: [#277](https://github.com/ossrs/srs/issues/277).
- [x] RTMP stream encapsulation as HTTP Live flv/aac/mp3/ts stream distribution. See: [#293](https://github.com/ossrs/srs/issues/293).
- [x] Enhanced DVR, supports Append/callback, see: [#179](https://github.com/ossrs/srs/issues/179).
- [x] Enhanced HTTP API, supports stream/vhost query, see: [#316](https://github.com/ossrs/srs/issues/316).
- [x] Support HSTRS (HTTP stream triggers RTMP back-to-source), support HTTP-FLV waiting, support edge back-to-source, see: [#324](https://github.com/ossrs/srs/issues/324).
- [x] [experimental] Support HDS, see: [#328](https://github.com/ossrs/srs/issues/328).
- [x] [experimental] Support Push MPEG-TS over UDP to SRS, see: [#250](https://github.com/ossrs/srs/issues/250).
- [x] [experimental] Support Push RTSP to SRS, see: [#133](https://github.com/ossrs/srs/issues/133).
- [x] [experimental] Support remote console, link: [console](https://github.com/ossrs/srs-console).
- [x] Other small feature improvements.

[SRS Release 2.0](https://github.com/ossrs/srs/tree/2.0release) was officially released on March 3, 2017.

## Release 1.0

Development codename: HuKaiqun. Thanks to my junior high school teachers Hu Kaiqun and Gao Ang for teaching me 
to love what I do.

[SRS release 1.0](https://github.com/ossrs/srs/tree/1.0release) is expected to have a development cycle of about one year. 
The main goals are:

- [x] Provide core business functions for internet live streaming, i.e., RTMP/HLS live streaming. Able to connect to any encoder and player, cluster support for connecting to any source server.
- [x] Provide a rich set of peripheral streaming media functions, such as Forward, Transcode, Ingest, DVR. Convenient for various source station businesses.
- [x] Perfect operation interface, reload, HTTP API, complete and up-to-date wiki. In addition, provide supporting commercial monitoring and troubleshooting systems.
- [x] Complete utest mechanism, as well as gperf (gmc, gmp, gcp) and gprof performance and optimization mechanisms. Provide a satisfactory performance and memory error detection mechanism at the C++ level.
- [x] Run on ARM/MIPS and other embedded CPU devices with Linux. In addition, provide supporting intranet monitoring and troubleshooting, cubieboard/raspberry-pi embedded servers.
- [x] High-performance server, supporting 2.7k concurrent connections.

[SRS Release 1.0](https://github.com/ossrs/srs/tree/1.0release) was released on schedule on December 5, 2014.

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/pages/product-en)


