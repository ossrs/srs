---
name: internal-docs-for-srs
description: Route SRS and Oryx tasks to the smallest relevant set of trusted project documentation and maintain the project documentation bundled with this skill. Use whenever support or development work requires locating, choosing, reading, creating, updating, or reviewing SRS documentation, or whenever Oryx work requires project documentation for deployment, streaming, authentication, recording to local or S3-compatible storage, restreaming, virtual live, camera, transcoding, HTTPS, APIs, callbacks, AI features, or development. Includes the C++ media server documentation, Oryx project documentation, website pages, changelog, executable API examples, and next-generation Go server and performance documentation.
---

# SRS Internal Documentation

Route SRS and Oryx tasks to focused documentation references. The parent skill owns the user-facing task; this skill owns documentation navigation and the bundled project documentation.

## Core Rules

- Use the current working directory as the project root. Do not search parent directories or discover alternate repository roots.
- Use the Reference Router before loading any reference or bundled document.
- Load only the references and bundled documents relevant to the task. Do not load everything.
- Treat only project files listed in this skill or a selected reference as trusted documentation.
- If no route covers the task, report that the documentation router does not cover it. Do not scan or broadly grep documentation directories.
- Select the smallest relevant set of project documents from their descriptions.
- Keep trusted document routing in this skill. Do not duplicate topic-to-file tables in dependent skills.

## Path Resolution

- Resolve bundled paths beginning with `references/`, `scripts/`, `assets/`, or `agents/` relative to the directory containing this `SKILL.md`, not the current working directory.
- Resolve repository paths such as `trunk/`, `internal/`, `cmd/`, or `skills/` relative to the current working directory.
- For Oryx documentation stored in its repository, use the project-root-relative `oryx/` path. The path may be a directory or a symlink to the user's preferred checkout. Do not resolve the symlink or search for alternate Oryx roots. If the path is unavailable, ask the user to make the `https://github.com/ossrs/oryx` checkout available there; do not create it automatically.
- Use the currently invoked skill directory. Do not search for alternate copies under tool-specific directories such as `.agents/`, `.kiro/`, or `.claude/`.
- Before reporting a routed file as missing, check its fully resolved path directly.

## Reference Router

### C++ Media Server Releases

For C++ media server tracking and releases, load:

- `trunk/doc/CHANGELOG.md` — Full changelog of all SRS versions, with one entry and version bump for each merged pull request.

### C++ Media Server User Documentation

For C++ media server user documentation, select the smallest relevant document:

- `references/cpp-docs/doc/introduction.md` — SRS overview, supported protocols, feature list, State Threads architecture, and learning path.
- `references/cpp-docs/doc/getting-started.md` — Docker quick start, RTMP publishing, HTTP-FLV/HLS playback, WebRTC, HTTPS, SRT, and stream URL patterns.
- `references/cpp-docs/doc/getting-started-ai.md` — SRS Robot, local AI agents, the skills system, and the project knowledge-base philosophy.
- `references/cpp-docs/doc/getting-started-build.md` — Build SRS from source and cross-build for ARM or MIPS.
- `references/cpp-docs/doc/getting-started-cdk.md` — Deploy SRS on AWS with srs-cdk.
- `references/cpp-docs/doc/rtmp.md` — RTMP usage, Enhanced RTMP, configuration, RTMPS, protocol comparisons, and codec history.
- `references/cpp-docs/doc/hls.md` — HLS compatibility, latency, segment configuration, HTTPS, and audio transcoding from WebRTC.
- `references/cpp-docs/doc/webrtc.md` — WHIP/WHEP, SFU architecture, RTMP-to-RTC conversion, TURN/ICE, audio transcoding, and platform usage.
- `references/cpp-docs/doc/flv.md` — HTTP-FLV delivery, configuration, latency, protocol comparisons, and browser compatibility.
- `references/cpp-docs/doc/srt.md` — SRT transport, latency, MPEG-TS encapsulation, HEVC, configuration, stream IDs, and weak-network behavior.
- `references/cpp-docs/doc/rtsp.md` — RTSP playback, TCP transport, RTMP publishing workflow, configuration, and build option.
- `references/cpp-docs/doc/gb28181.md` — External-SIP GB28181 media publishing, HTTP session creation, TCP transport, timeout configuration, and session lifecycle.
- `references/cpp-docs/doc/http-server.md` — Embedded HTTP server, HLS/static serving, API endpoint, configuration, and reverse proxies.
- `references/cpp-docs/doc/hevc.md` — H.265/HEVC protocol compatibility, Enhanced RTMP, encoder setup, and bandwidth tradeoffs.
- `references/cpp-docs/doc/dvr.md` — FLV/MP4 recording, plans, paths, HTTP callbacks, and Oryx recording features.
- `references/cpp-docs/doc/ingest.md` — Pull external files, RTSP cameras, or HTTP streams through FFmpeg and republish them to SRS.
- `references/cpp-docs/doc/forward.md` — Forward RTMP streams to other servers, configure master/slave roles, and compare forwarding with edge mode.
- `references/cpp-docs/doc/security.md` — IP and CIDR allow/deny rules for publishing and playback.
- `references/cpp-docs/doc/snapshot.md` — Capture stream thumbnails through HTTP callbacks or the transcoder.
- `references/cpp-docs/doc/http-api.md` — HTTP API endpoints for server, stream, and client statistics, including CORS and console integration.
- `references/cpp-docs/doc/http-callback.md` — Event callbacks for connection, publishing, playback, DVR, authentication, and business logic.
- `references/cpp-docs/doc/exporter.md` — Prometheus metrics, Grafana integration, labels, tags, and cloud-native observability.
- `references/cpp-docs/doc/origin-cluster.md` — Proxy-based load balancing across origin servers and the Go proxy architecture.
- `references/cpp-docs/doc/edge.md` — Edge caching, pull-on-play, push-on-publish, and multi-level CDN topology.
- `references/cpp-docs/doc/nginx-for-hls.md` — Distribute and cache HLS/DASH through NGINX.
- `references/cpp-docs/doc/resource.md` — Port, firewall, HTTPS, and resource reference.
- `references/cpp-docs/doc/low-latency.md` — RTMP latency tuning, GOP and queue settings, merge-write optimization, and protocol comparison.
- `references/cpp-docs/doc/performance.md` — UDP tuning, perf, gprof, Valgrind, ASAN, leak detection, and benchmarking methodology.
- `references/cpp-docs/doc/ffmpeg.md` — FFmpeg transcoding, multi-bitrate output, stream filtering, and per-vhost/app/stream configuration.

### C++ Media Server Website Pages

For C++ media server website pages, select the smallest relevant page:

- `references/cpp-docs/pages/faq-server-en.md` — SRS server FAQ covering CDN, VoD, common errors, protocol issues, and community support.
- `references/cpp-docs/pages/license-en.md` — SRS, State Threads, and third-party library licenses.
- `references/cpp-docs/pages/product-en.md` — Release milestones, codenames, achievements, and product history.
- `references/cpp-docs/pages/security-advisories-en.md` — Published CVEs, affected versions, patches, and security references.

### Oryx Documentation

For Oryx documentation, select the smallest relevant document:

- `references/cpp-docs/doc/getting-started-oryx.md` — Oryx overview, deployment, comparison with SRS, authentication, recording, forwarding, virtual live, transcoding, AI features, OpenAPI, and HTTP callbacks.
- `references/cpp-docs/pages/faq-oryx-en.md` — Oryx setup, upgrades, HTTPS, authentication, recording to local disk or S3-compatible cloud storage, re-streaming, and common operational questions.
- `oryx/README.md` — Oryx overview, Docker deployment, ports, persistent `/data` layout, environment variables, features, and dependencies.
- `oryx/DEVELOPER.md` — Oryx OpenAPI, environment variables, ports, deployment variants, development workflows, testing, and changelog.
- `trunk/3rdparty/srs-docs/blog/2022-04-09-Oryx-Tutorial.md` — One-click, Docker, Lightsail, and DigitalOcean deployment, plus RTMP, WebRTC, and SRT getting started.
- `trunk/3rdparty/srs-docs/blog/2022-04-12-Oryx-HTTPS.md` — DNS, automatic HTTPS, Let's Encrypt, and certificate renewal.
- `trunk/3rdparty/srs-docs/blog/2022-04-15-Oryx-WordPress-Plugin.md` — Embed HLS, HTTP-FLV, and WebRTC streams in WordPress.
- `trunk/3rdparty/srs-docs/blog/2022-04-29-BT-aaPanel.md` — Install and operate Oryx through aaPanel.
- `trunk/3rdparty/srs-docs/blog/2023-08-29-Oryx-Ensuring-Authentication-for-Live-Streaming-Publishing.md` — Global publish-secret authentication and its limitations.
- `trunk/3rdparty/srs-docs/blog/2023-09-09-Oryx-Multi-Platform-Streaming.md` — Restream to YouTube, Twitch, and Facebook.
- `trunk/3rdparty/srs-docs/blog/2023-09-10-Oryx-Record-Live-Streaming.md` — Server-side recording, MP4 generation, filters, and local or S3-compatible cloud storage.
- `trunk/3rdparty/srs-docs/blog/2023-09-11-Oryx-Virtual-Live-Events.md` — Publish prerecorded files as live streams.
- `trunk/3rdparty/srs-docs/blog/2023-10-11-Oryx-Stream-IP-Camera-Events.md` — Pull RTSP cameras and forward them to streaming platforms.
- `trunk/3rdparty/srs-docs/blog/2023-10-21-Oryx-Live-Transcoding.md` — FFmpeg-based bitrate and resolution transcoding.
- `trunk/3rdparty/srs-docs/blog/2023-11-28-Oryx-Live-Streams-Transcription.md` — Whisper transcription, embedded subtitles, and WebVTT.
- `trunk/3rdparty/srs-docs/blog/2023-12-12-Oryx-OBS-WHIP-Service.md` — Publish WHIP from OBS and play through WHEP.
- `trunk/3rdparty/srs-docs/blog/2024-01-31-Browser-Voice-Driven-GPT.md` — Browser voice assistant, live rooms, language coaching, and translation.
- `trunk/3rdparty/srs-docs/blog/2024-02-21-Dubbing-Translating.md` — Video translation, ASR, TTS, segment editing, and export.
- `trunk/3rdparty/srs-docs/blog/2024-05-20-OCR-Video-Streams.md` — Live-stream OCR, configurable AI instructions, and callbacks.

For Oryx, prefer the getting-started guide, FAQ, and repository documentation. Treat dated blogs as scenario-specific guidance. If commands, UI labels, tooling, or behavior differ, prefer documentation matching the user's Oryx version.

### RTMP Go API Examples

For RTMP Go API examples, load:

- `internal/rtmp/example_test.go` — RTMP API examples for AMF0, handshake, and protocol workflows.

### WHEP Performance Analysis

For WHEP performance analysis, load:

- `references/perf/proxy-whep.md` — Profile WHEP with pprof and srs-bench and compare CPU, allocation, heap, goroutine, and trace data.

### Next-Generation Go Proxy Documentation

For next-generation Go proxy documentation, select the smallest relevant document:

- `references/proxy/features.md` — Proxy feature status and limitations, including implemented protocols, APIs, load balancing, deployment, configuration, operations, and current limitations.
- `references/proxy/proxy-design.md` — Proxy architecture, including stateless proxy design, built-in load balancing, Redis mode, and horizontal scaling.
- `references/proxy/proxy-protocol.md` — Backend registration, debugging backend, heartbeat protocol, and environment variables.
- `references/proxy/proxy-usage.md` — Getting started with the proxy: build, start, register, publish, and verify with an SRS origin.
- `references/proxy/proxy-load-balancer.md` — Load-balancer behavior for memory and Redis load balancers, stream mapping, health tracking, and protocol state.
- `references/proxy/proxy-origin-cluster.md` — Production origin clusters: advanced usage for configuring and verifying a multi-origin cluster through the proxy.

If a task spans multiple areas, load only the required references or bundled documents from the lists.

## Workflow

1. Classify the documentation need with the Reference Router.
2. Resolve the selected path according to [Path Resolution](#path-resolution), then load the project or bundled document directly.
3. Load only the documents required for the task.
4. Return control to the parent skill for answering, development, troubleshooting, review, or editing.

When invoked directly, follow the same routing workflow and then apply the relevant support or development workflow.

## Maintaining the Router

- Update the Reference Router when a trusted document is added, removed, renamed, or changes responsibility.
- Keep router summaries and file descriptions concise and focused on navigation.
- Do not duplicate document content or implementation details in the router.
