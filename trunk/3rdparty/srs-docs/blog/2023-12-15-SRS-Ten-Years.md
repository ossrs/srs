---
slug: srs-ten-years
title: SRS 10th Anniversary and 2023 Year-end Review
authors: []
tags: [srs, community]
custom_edit_url: null
---

# SRS 10th Anniversary and 2023 Year-end Review

As we reach the end of 2023, it's time for an annual summary, which we can look back on and laugh at 
our immaturity in the coming years.

<!--truncate-->

## SRS Server

SRS 5.0 will be officially released as a stable version in the next two weeks. After a year of feature 
freeze and six months of stability improvement, 5.0 is now ready for production use.

5.0 started development in March 2021, with key changes as follows:

* ST (State-Threads): Supports Windows/RISCV/LOONGARCH/MIPS/AppleM1 chips and multi-threading capabilities.
* SRT: Rewritten based on ST, aligning APIs and callbacks, supporting direct IP streaming, upgrading libsrt, improving latency, and completing configuration options.
* WebRTC: Supports WHIP and WHEP protocols, TCP transmission, Unity, improved error reporting, MP3 to OPUS conversion, and native OPUS support in FFmpeg.
* WHIP: Supports WHIP protocol, adapts to Larix and OBS WHIP, improves security, and supports resource deletion.
* GB28181: Supports 2016/TCP protocol, stress testing tools, and regression testing.
* Live: Fixes RBSP parsing issues, compatibility with FFmpeg timecode, ignores empty NALUs, supports HLS pseudo-streams, HLS client kicking, and resolves DASH crash issues.

There are also some bug fixes. For a detailed list of changes, please refer to the official 
website's Changelog.

Looking back, it seems not much has been done in two years.

## Oryx

Oryx is an integrated video cloud that includes multiple audio and video projects, centered around 
audio and video application scenarios, and offers out-of-the-box solutions.

This year, Oryx was revamped, with major changes as follows:

* Transitioned from microservices architecture to monolithic application, reducing image size by 90%, increasing development speed by 10 times, and making microservices architecture the most misleading.
* Supports Docker and script installations, DigitalOcean and Lighthouse images, BT and aaPanel plugin installations, and HELM installations.
* Tests cover all installation methods, features, and APIs.
* Supports new scenarios such as virtual live streaming, AI-generated subtitles, transcoding, callbacks, HLS clusters, and stream pulling and pushing.

If SRS is simple and easy to use, Oryx truly simplifies building audio and video businesses.

## Community

SRS started as an open-source project in 2013, officially began building an open-source community in 
2020, and expanded to a global community in 2022. This year, the community has grown rapidly and is 
now self-sustaining.

The number of monthly paying users worldwide and OpenCollective subscribers has increased from around 
5 to 41. It is expected that in three to five years, dedicated time can be invested in supporting community 
development.

This year, we also began offering paid support to the community, with overseas users paying $5 per month, 
which has been well received by users.

We maintain open-source projects while also benefiting from other audio and video open-source projects. 
Therefore, we will donate half of our revenue to other audio and video open-source projects we depend on,
such as FFmpeg and OBS.

Additionally, we will continue to enhance the community's AI capabilities. For example, we use GPT to 
automatically translate community Issues and Pull Requests, reducing communication barriers. We also 
deployed AI Talk on the official website, making it easy for everyone to experience the new capabilities 
of audio, video, and AI integration.

We are grateful for the global developer support and recognition of SRS, and our development will continue
to improve.

## Cloud Service

At SRS, our goal is to establish a non-profit, open-source community dedicated to creating an all-in-one,
out-of-the-box, open-source video solution for live streaming and WebRTC online services.

Additionally, we offer a [Cloud](../cloud) service for those who prefer to use cloud service instead of building from
scratch. Our cloud service features global network acceleration, enhanced congestion control algorithms,
client SDKs for all platforms, and some free quota.

To learn more about our cloud service, click [here](../cloud).

## Conclusion

No matter how the open-source wind blows, understanding the value of what we do will ensure longevity.

Numerous open-source projects provide a never-ending foundation for the global software ecosystem. Being 
a contributor to this system is a programmer's pride.

Providing paid support to users is valuable. While some regions may not appreciate it as much, we can 
offer more services where it is more recognized, such as overseas.

As for professional open-source and custom development, some community developers are already doing 
this on Upwork, and flexible employment is a good option.

Lastly, regarding commercialization, at least SRS won't pursue it. SRS aims to become a widely used 
audio and video open-source project globally, and commercialization would hinder this goal.

In the open-source community, there is no winter in sight.

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/blog-en/2023-12-15-SRS-Ten-Years)
