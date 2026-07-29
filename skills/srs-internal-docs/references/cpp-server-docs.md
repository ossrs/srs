# C++ Media Server Documentation

Documentation for the C++ media server. Load only the files relevant to the current support or development task.

## Tracking and Releases

- `trunk/doc/CHANGELOG.md` — Full changelog of all SRS versions, with one entry and version bump for each merged pull request.

## User Documentation

- `trunk/3rdparty/srs-docs/doc/introduction.md` — SRS overview, supported protocols, feature list, State Threads architecture, and learning path.
- `trunk/3rdparty/srs-docs/doc/getting-started.md` — Docker quick start, RTMP publishing, HTTP-FLV/HLS playback, WebRTC, HTTPS, SRT, and stream URL patterns.
- `trunk/3rdparty/srs-docs/doc/getting-started-ai.md` — SRS Robot, local AI agents, the skills system, and the project knowledge-base philosophy.
- `trunk/3rdparty/srs-docs/doc/getting-started-build.md` — Build SRS from source and cross-build for ARM or MIPS.
- `trunk/3rdparty/srs-docs/doc/getting-started-cdk.md` — Deploy SRS on AWS with srs-cdk.
- `trunk/3rdparty/srs-docs/doc/getting-started-oryx.md` — Deploy and use Oryx, including recording, forwarding, AI subtitles, HTTPS, Docker, Helm, and aaPanel.
- `trunk/3rdparty/srs-docs/doc/rtmp.md` — RTMP usage, Enhanced RTMP, configuration, RTMPS, protocol comparisons, and codec history.
- `trunk/3rdparty/srs-docs/doc/hls.md` — HLS compatibility, latency, segment configuration, HTTPS, and audio transcoding from WebRTC.
- `trunk/3rdparty/srs-docs/doc/webrtc.md` — WHIP/WHEP, SFU architecture, RTMP-to-RTC conversion, TURN/ICE, audio transcoding, and platform usage.
- `trunk/3rdparty/srs-docs/doc/flv.md` — HTTP-FLV delivery, configuration, latency, protocol comparisons, and browser compatibility.
- `trunk/3rdparty/srs-docs/doc/srt.md` — SRT transport, latency, MPEG-TS encapsulation, HEVC, configuration, stream IDs, and weak-network behavior.
- `trunk/3rdparty/srs-docs/doc/rtsp.md` — RTSP playback, TCP transport, RTMP publishing workflow, configuration, and build option.
- `trunk/3rdparty/srs-docs/doc/http-server.md` — Embedded HTTP server, HLS/static serving, API endpoint, configuration, and reverse proxies.
- `trunk/3rdparty/srs-docs/doc/hevc.md` — H.265/HEVC protocol compatibility, Enhanced RTMP, encoder setup, and bandwidth tradeoffs.
- `trunk/3rdparty/srs-docs/doc/dvr.md` — FLV/MP4 recording, plans, paths, HTTP callbacks, and Oryx recording features.
- `trunk/3rdparty/srs-docs/doc/ingest.md` — Pull external files, RTSP cameras, or HTTP streams through FFmpeg and republish them to SRS.
- `trunk/3rdparty/srs-docs/doc/forward.md` — Forward RTMP streams to other servers, configure master/slave roles, and compare forwarding with edge mode.
- `trunk/3rdparty/srs-docs/doc/security.md` — IP and CIDR allow/deny rules for publishing and playback.
- `trunk/3rdparty/srs-docs/doc/snapshot.md` — Capture stream thumbnails through HTTP callbacks or the transcoder.
- `trunk/3rdparty/srs-docs/doc/http-api.md` — HTTP API endpoints for server, stream, and client statistics, including CORS and console integration.
- `trunk/3rdparty/srs-docs/doc/http-callback.md` — Event callbacks for connection, publishing, playback, DVR, authentication, and business logic.
- `trunk/3rdparty/srs-docs/doc/exporter.md` — Prometheus metrics, Grafana integration, labels, tags, and cloud-native observability.
- `trunk/3rdparty/srs-docs/doc/origin-cluster.md` — Proxy-based load balancing across origin servers and the Go proxy architecture.
- `trunk/3rdparty/srs-docs/doc/edge.md` — Edge caching, pull-on-play, push-on-publish, and multi-level CDN topology.
- `trunk/3rdparty/srs-docs/doc/nginx-for-hls.md` — Distribute and cache HLS/DASH through NGINX.
- `trunk/3rdparty/srs-docs/doc/resource.md` — Port, firewall, HTTPS, and resource reference.
- `trunk/3rdparty/srs-docs/doc/low-latency.md` — RTMP latency tuning, GOP and queue settings, merge-write optimization, and protocol comparison.
- `trunk/3rdparty/srs-docs/doc/performance.md` — UDP tuning, perf, gprof, Valgrind, ASAN, leak detection, and benchmarking methodology.
- `trunk/3rdparty/srs-docs/doc/ffmpeg.md` — FFmpeg transcoding, multi-bitrate output, stream filtering, and per-vhost/app/stream configuration.

## Website Pages

- `trunk/3rdparty/srs-docs/pages/faq-oryx-en.md` — Oryx FAQ covering setup, upgrades, HTTPS, authentication, recording, re-streaming, and FFmpeg replacement.
- `trunk/3rdparty/srs-docs/pages/faq-server-en.md` — SRS server FAQ covering CDN, VoD, common errors, protocol issues, and community support.
- `trunk/3rdparty/srs-docs/pages/license-en.md` — SRS, State Threads, and third-party library licenses.
- `trunk/3rdparty/srs-docs/pages/product-en.md` — Release milestones, codenames, achievements, and product history.
- `trunk/3rdparty/srs-docs/pages/security-advisories-en.md` — Published CVEs, affected versions, patches, and security references.
