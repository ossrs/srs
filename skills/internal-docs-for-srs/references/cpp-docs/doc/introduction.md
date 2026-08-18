---
title: Introduction
sidebar_label: Introduction
hide_title: false
hide_table_of_contents: false
---

# Introduction

> Remark: SRS6 is developing and not stable.

SRS is a open-source ([MIT Licensed](../../../license)), simple, high-efficiency, real-time video server supporting RTMP, 
WebRTC, HLS, HTTP-FLV, SRT, MPEG-DASH, and GB28181. SRS media server works with clients like [FFmpeg](https://ffmpeg.org),
[OBS](https://obsproject.com), [VLC](https://www.videolan.org), and [WebRTC](https://webrtc.org) to provide 
the ability to [receive and distribute streams](./getting-started.md) in a typical publish (push) and 
subscribe (play) server model. SRS supports widely used internet audio and video protocol conversions, 
such as converting [RTMP](./rtmp.md) or [SRT](./srt.md) to [HLS](./hls.md), [HTTP-FLV](./flv.md), or 
[WebRTC](./webrtc.md).

SRS is primarily used in the Live streaming and WebRTC fields. In the live streaming domain, SRS supports typical 
protocols such as RTMP, HLS, SRT, MPEG-DASH, and HTTP-FLV. In the WebRTC field, SRS supports protocols like WebRTC, 
WHIP, and WHEP. SRS facilitates protocol conversion for both Live streaming and WebRTC. As a media server, SRS 
typically works alongside other open-source projects such as FFmpeg, OBS, and WebRTC. Oryx as an out-of-the-box 
media solution, incorporating numerous open-source projects and tools, please refer to the [introduction](./getting-started-oryx.md#introduction) 
of Oryx.

SRS provides an [HTTP API](./http-api.md) open interface to query system and stream status. It also supports 
[HTTP Callback](./http-callback.md) for callback capabilities, actively notifying your system and implementing 
stream authentication and business customization (such as dynamic DVR). SRS also supports the official 
[Prometheus Exporter](./exporter.md) for integration with cloud-native monitoring systems, offering powerful 
observability. SRS supports session-level [traceable logs](./log.md), greatly reducing system maintenance costs.

If you are new to audio, video, and streaming media or new to SRS, we recommend reading [Getting Started](./getting-started.md) 
and [Learning Path](/guide). Please take the time to read the following documentation, as reading and 
familiarizing yourself with the documentation is a basic requirement of the community. If you encounter any 
problems, please first search in the [FAQ](../../../faq), then in [Issues](https://github.com/ossrs/srs/issues) and 
[Discussions](https://github.com/ossrs/srs/discussions) to find answers to almost all questions.

SRS is developed using ANSI C++ (98) and only uses basic C++ capabilities. It can run on multiple platforms 
such as Linux, Windows, and macOS. We recommend using Ubuntu 20+ for development and debugging. The image 
we provide [ossrs/srs](https://hub.docker.com/r/ossrs/srs) is also built on Ubuntu 20 (focal).

> Note: To solve the long connection and complex state machine problems in complex streaming media processing, 
> SRS uses [ST(State Threads)](https://github.com/ossrs/state-threads) coroutine technology (similar 
> to [Goroutine](https://go.dev/doc/effective_go#goroutines)) and continuously enhances and maintains 
> ST's capabilities, supporting multiple platforms such as Linux, Windows, macOS, and various CPU 
> architectures like X86_64, ARMv7, AARCH64, M1, RISCV, LOONGARCH, and MIPS.

## Features

Functionality is often a major concern for people and the richness of features is an important reason for choosing a
project. You can view the detailed feature list at [Features](https://github.com/ossrs/srs/blob/develop/trunk/doc/Features.md#features). 
We have listed the main features' versions, along with related Issue and PR links.

Additionally, in the detailed description of [Milestones](/product), the supported features for each major version 
are introduced.

> Note: If you want to see the Issues for each milestone, you can check them at [Milestones](https://github.com/ossrs/srs/milestones).

Please note that although not many, SRS still marks some features as [Deprecated](https://github.com/ossrs/srs/blob/develop/trunk/doc/Features.md#features). 
You can search for 'Deprecated' or 'Removed' on the page. We will also explain in detail why we are removing a 
particular feature.

If you want to know about the features we are currently working on, you can join our [Discord](/contact#discussion) 
and [Blog](../../../blog). Once new features are completed, we will post articles on Discord and Blog, so stay tuned.

## Who's using SRS?

SRS users are spread all over the world, and we welcome everyone to showcase their SRS applications 
in [SRS Use Cases](https://github.com/ossrs/srs/discussions/3771).

## Governance

We welcome everyone to participate in the development and maintenance of SRS. We recommend starting by
resolving issues from [Contribute](https://github.com/ossrs/srs/contribute) and [submitting PRs](/how-to-file-pr).
All contributors will be showcased in [Contributors](https://github.com/ossrs/srs#authors).

SRS is a non-commercial open-source community where active developers have their own jobs and contribute to SRS's 
development in their spare time.

Since the SRS system is highly efficient, we can spend minimal time making continuous improvements, delivering 
feature-rich and stable high-quality products. Customizing based on SRS is also easy.

We are a global open-source community with developer communities both domestically and abroad. We welcome developers 
to join us:

* Great sense of accomplishment: Your code can impact global users, change the audio and video industry, and transform various sectors as SRS is widely used.
* Solid technical progress: You can interact with top audio and video developers worldwide, master high-quality software development skills, and mutually enhance technical capabilities.

SRS currently uses the following techniques and rules to ensure high quality and efficiency:

* Long-term discussions on architecture and solutions. For significant features and plans, extensive discussions are required, such as the 7-year discussion on [HEVC/H.265](https://github.com/ossrs/srs/issues/465) support.
* Careful and thorough code reviews. Each pull request must be approved by at least two TOCs and developers and pass all Actions before merging.
* Comprehensive unit tests (over 500), code coverage (around 60%), black-box testing, etc., ensuring ample testing time with one year of development and one year of testing.
* Full pipeline: Each pull request has a pipeline, and each release is automatically completed by the pipeline.

We welcome you to join us. For more information, please visit [Contribute](https://github.com/ossrs/srs/contribute)
and submit a pull request as required.

## Milestone

SRS releases a major version approximately every two years, with one year for development and one year
for stability improvement. For more details, please refer to [Milestone](/product).

If you want to use SRS online, it's recommended to use the stable version. If you want to use new features, use 
the development version.

SRS has branches based on versions, such as:

* [develop](https://github.com/ossrs/srs/tree/develop) SRS 6.0, development branch, unstable, but with the most new features.
* [5.0release](https://github.com/ossrs/srs/tree/5.0release#releases) SRS 5.0, currently stable, depending on the branch's status.
* [4.0release](https://github.com/ossrs/srs/tree/4.0release#releases) SRS 4.0, currently the stable branch, and will become more stable over time.

To determine if a branch is stable, check the Releases tag, such as [SRS 4.0](https://github.com/ossrs/srs/tree/4.0release#releases):

* 2022-06-11, Release v4.0-r0, this is a stable release version.
* 2021-12-01, Release v4.0-b0, this is a relatively stable beta version, also known as a public test version.
* 2021-11-15, Release v4.0.198, this version is an unstable development version.

> Note: In addition to beta versions, there are alpha versions, such as `v5.0-a0`, which are less stable internal 
> test versions compared to beta.

> Note: Each alpha, beta, and release version will correspond to a specific version number, such as `v5.0-a0` 
> corresponding to `v5.0.98`.

For SRS, generally, once it reaches the beta version, it can be used online.

## Strategy

SRS doesn't develop client-side applications because there are already mature and large open-source communities like 
FFmpeg, OBS, VLC, and WebRTC. SRS collaborates with these communities and uses their products.

In addition to the SRS server, they also work on Oryx and WordPress plugins, with the main goal of creating 
simpler application methods for different industries, including:

* [Oryx](https://github.com/ossrs/oryx): Oryx(SRS Stack) is an out-of-the-box, single-machine video cloud solution that includes FFmpeg and SRS. It's designed for users who aren't familiar with command lines and allows them to set up audio and video applications through Tencent Cloud images or BaoTa with mouse operations.
* [WordPress-Plugin-SrsPlayer](https://github.com/ossrs/WordPress-Plugin-SrsPlayer): This plugin is for the publishing industry, such as personal blogs and media websites, making it easy for users to utilize audio and video capabilities.
* [srs-unity](https://github.com/ossrs/srs-unity): This project is for the gaming industry, integrating Unity's WebRTC SDK to use audio and video capabilities.

SRS will continue to improve its toolchain. Developers may not use SRS but might have used the SB stress testing tool:

* [srs-bench](https://github.com/ossrs/srs-bench): An audio and video stress testing tool that supports RTMP, FLV, WebRTC, GB28181, etc., with plans for future improvements.
* [state-threads](https://github.com/ossrs/state-threads): A C coroutine library that can be considered a C version of Go. It's a small but powerful server library that will be continuously improved.
* [tea](https://github.com/ossrs/tea): This project explores eBPF for network simulation in weak network conditions and load balancing.

SRS aims to continuously improve audio and video toolchains, solutions, and scenario-based capabilities, making it 
possible for various industries to utilize audio and video capabilities.

## Sponsors

SRS is committed to building a non-profit open-source project and community. We provide special community
support for friends who sponsor SRS. Please see [Sponsor](/contact#donation).

Audio and video developers often face challenges, and they might be used to the close support from cloud 
service providers. When joining an open-source community, it might feel unfamiliar.

Don't panic when encountering issues. Most problems have solutions that can be found in the [FAQ](../../../faq) 
or the documentation [Docs](./getting-started.md).

You can also join the Discord channel through [Support](/contact) to communicate with other developers. 
However, please follow community guidelines, or you won't receive help.

As developers, we must learn to read documentation, investigate issues, and then discuss them within the community.

For advanced developers, we suggest becoming a `Backer/Sponsor`. See [Support](/contact#donation).

SRS has no commercial plans. We are currently working hard to build a global, active developer community. 
The value of open-source will grow, and community support will increase.

## About Oryx

Oryx is a lightweight, open-source video cloud solution based on Go, Reactjs, SRS, FFmpeg, WebRTC,
and more. For more details, please refer to [Oryx](./getting-started-oryx.md).

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/introduction)


