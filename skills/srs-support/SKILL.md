---
name: srs-support
description: Answer SRS (Simple Realtime Server) and Oryx questions for users, operators, and DevOps. Covers SRS protocols, configuration, codecs, deployment, monitoring, performance, troubleshooting, ecosystem tools, and community support, plus Oryx deployment, dashboard use, authentication, HTTPS, streaming, recording, restreaming, virtual live events, IP cameras, transcoding, OpenAPI, callbacks, AI transcription, voice assistants, dubbing, and OCR. Use for publishing, playback, production operation, feature comparison, and troubleshooting; do not use for changing or explaining source code.
---

# SRS and Oryx Support

Support two related but distinct products:

- **SRS** — a simple, high-efficiency, real-time media server.
- **Oryx** — an integrated, single-node video solution built with SRS, FFmpeg, Go, React, Redis, and Nginx.

This skill is for users, operators, and DevOps. Help them use the products; do not change or teach their source code.

**SRS scope:**
- Deployment, configuration, protocols, codecs, publishing, playback, monitoring, performance, clustering, and troubleshooting
- General usage-level explanations of how SRS behaves
- Source inspection only as a last resort to answer a usage question

**Oryx scope:**
- Deployment, dashboard operation, authentication, HTTPS, OpenAPI, and callbacks
- Streaming, recording, restreaming, virtual live, IP camera, transcoding, and AI features
- Documentation-based usage and troubleshooting; do not inspect Oryx source code

**Out of scope:**
- Code changes, bug fixes, feature development, and source-code explanations
- SRS or Oryx internal implementation guidance

## Product Selection

Select the product before entering a workflow:

- Select **SRS** for standalone SRS configuration, protocols, codecs, port 1985 APIs, clustering, performance, and media-server operation.
- Select **Oryx** when the request mentions Oryx, SRS Stack, the Oryx dashboard, `/terraform/v1/`, automatic HTTPS, recording, restreaming, virtual live, camera streaming, or Oryx AI features.
- If the distinction affects the answer and the product is ambiguous, ask whether the user runs standalone SRS or Oryx.
- For a comparison or cross-product problem, run the workflows separately and label the SRS and Oryx parts. Do not merge their commands, configuration, APIs, or troubleshooting steps.

## Skill Dependencies

- `skills/internal-docs-for-srs/SKILL.md` — Route and load trusted SRS and Oryx documentation. This skill remains responsible for the support workflow and final answer.
- `skills/internal-codemap-for-srs/SKILL.md` — Route source investigation for standalone SRS only when documentation cannot answer a usage question.

## SRS Workflow

### Step 1: Setup

Use the current working directory as the SRS project root. Do not search parent directories or discover alternate repository roots.

Available directories: `trunk/`, `cmd/`, `internal/`, `cmake/`, `memory/`, `skills/`

All AI tools — OpenClaw, Codex, Claude Code, Kiro CLI — see the same relative paths.

**Path resolution:**
- Resolve bundled paths beginning with `references/`, `scripts/`, `assets/`, or `agents/` relative to the directory containing this `SKILL.md`.
- Resolve repository paths such as `trunk/`, `internal/`, `cmd/`, or `skills/` relative to the current working directory.
- Use the currently invoked skill directory. Do not search for alternate copies under `.agents/`, `.kiro/`, or `.claude/`.
- Before reporting a routed file as missing, check its fully resolved path directly.

### Step 2: Load Knowledge

Load knowledge in layers and stop when there is enough information.

**Layer 1 — Always load:**
- `references/srs-overview.md` — Protocols, codecs, transmuxing, configuration, features, ecosystem, and performance.

**Layer 2 — Load relevant SRS documentation:**
1. Load `skills/internal-docs-for-srs/SKILL.md`.
2. Use its Reference Router to select the smallest relevant SRS document.
3. Do not duplicate its routing table or guess documentation paths.

**Layer 3 — Last resort for a usage answer:**
1. Load `skills/internal-codemap-for-srs/SKILL.md`.
2. Select the smallest relevant C++ media server or next-generation Go server map and, when needed, its verification map.
3. Read only the module and file set selected by the map. Do not broadly grep directories or the repository root.

### Step 3: Answer by Topic

Apply these rules to all SRS answers:
- Ground answers in the loaded knowledge and documentation; do not invent features.
- If the knowledge base does not cover something, say: "The knowledge base doesn't cover that yet."
- Keep answers practical with commands, configuration snippets, or URLs when relevant.
- Use `trunk/doc/source.flv` for standalone SRS publish examples.

**Protocol questions**
- State whether each protocol supports publishing, playback, or both.
- Include the version and date added from `references/srs-overview.md` when relevant.
- Clarify TCP versus UDP and explain latency, compatibility, and performance tradeoffs.

**Codec questions**
- Clarify codec support per protocol.
- Specify transcoding direction, such as AAC to Opus for RTMP-to-WebRTC.
- Distinguish built-in audio transcoding from external FFmpeg video transcoding.
- Explain that SRS primarily transmuxes rather than re-encodes video.

**Configuration questions**
- Reference `trunk/conf/full.conf` as the complete configuration reference.
- Load feature-specific documentation for detailed examples.
- Mention environment-variable support for Docker and cloud-native deployments.
- Recommend `trunk/conf/console.conf` for local testing.

**Deployment and getting started**
- Provide the standard build steps: `cd trunk && ./configure && make`.
- Show publishing and playback with FFmpeg and common players.
- For Docker, reference `trunk/conf/docker.conf` and load the getting-started documentation.
- Note that SRS targets Linux; use WSL on Windows, while macOS is suitable for development.

**Architecture questions**
- Explain that SRS is single-process and single-threaded by design.
- Scale through origin or edge clusters rather than adding threads.
- Treat internal architecture and coroutine teaching as outside usage support.

**Performance questions**
- TCP protocols such as RTMP and HTTP-FLV handle thousands of connections.
- UDP protocols such as WebRTC and SRT handle hundreds; audio transcoding may reduce WebRTC capacity to dozens.
- Use an origin cluster to scale across CPUs.

**Comparison questions**
- Compare SRS with Nginx-RTMP, Janus, or Red5 using documented facts.
- Focus on protocol coverage, language and performance, and use-case fit.
- Be objective about alternatives' strengths.

**Ecosystem questions**
- **srs-bench** — Benchmark RTMP, WebRTC, HTTP-FLV, HLS, and GB28181.
- **state-threads** — An internal coroutine library; development details are outside this skill.
- **Oryx** — A separate integrated solution built on SRS. Switch to the Oryx workflow for its usage.
- SRS maintains server-side projects, not client tools.

**Community questions**
- Provide the Telegram and Discord links from `references/srs-overview.md`.
- Tell users to join a group and mention the SRS Robot.
- Recommend small, focused Telegram groups because large conversations mix unrelated context.

### Step 4: Troubleshoot SRS

Gather these details first when missing:
- SRS version: `curl http://localhost:1985/api/v1/versions`
- Config file
- Publishing tool, protocol, and command
- Playback tool, protocol, and URL
- Local, LAN, or cloud network topology, including firewall or NAT
- Error messages and relevant logs

**Diagnostic tools:**
- Streams: `curl http://localhost:1985/api/v1/streams`
- Clients: `curl http://localhost:1985/api/v1/clients`
- Server summary: `curl http://localhost:1985/api/v1/summaries`
- Logs: `trunk/objs/srs.log` or console output; use context IDs to trace connections
- Prometheus: load the exporter documentation when configured

**WebRTC fails remotely**
- Verify that `rtc_server.candidate` is the reachable public address, not `127.0.0.1` or an unreachable private address.
- Require HTTPS for browser capture outside localhost.
- Open UDP port 8000 and use the WebRTC connection-failure documentation for connectivity checks.

**HLS latency is 20–30 seconds**
- Reduce `hls_fragment` and `hls_window`, and align the encoder GOP with fragment duration.
- Account for player-side buffering.
- Recommend HTTP-FLV or WebRTC when sub-five-second latency is required.

**One protocol works but another does not**
- Verify that the required conversion is enabled, such as `rtmp_to_rtc on` or `srt_to_rtmp on`.
- Check codec compatibility for the destination protocol.

**VLC reports high latency**
- Explain that VLC adds substantial client-side buffering and is not a reliable low-latency reference.
- Recommend ffplay or browser playback through HTTP-FLV or WebRTC.

**Stream not found**
- Verify the stream through `/api/v1/streams`.
- Match the app and stream names exactly.
- Ensure publishing begins before playback except in edge mode.

**Reverse proxy breaks streaming**
- Preserve chunked transfer for HTTP-FLV.
- Use appropriate HLS proxy caching.
- Preserve required WHIP and WHEP proxy headers.
- Load the Nginx or Caddy documentation for exact configuration.

**Connection limit reached**
- Check `max_connections` and `/api/v1/summaries`.
- Remember that WebRTC uses more resources than RTMP, especially with audio transcoding.

**Ports and firewall**
- RTMP 1935/TCP, HTTP API 1985/TCP, HTTP streaming 8080/TCP, WebRTC 8000/UDP, and SRT 10080/UDP.
- Verify UDP firewall rules explicitly.

## Oryx Workflow

### Step 1: Setup

Treat Oryx as its own product. Do not translate an Oryx dashboard workflow into raw SRS configuration unless the selected Oryx documentation explicitly requires it.

- Use the current SRS workspace to load this skill and `internal-docs-for-srs`.
- Follow the Oryx path rules in `internal-docs-for-srs`, including its fixed `~/git/oryx` repository location.
- Do not inspect Oryx Go, React, packaging, or other source code under this support skill.
- Redact passwords, publish secrets, API tokens, OpenAI keys, and destination stream keys from commands, logs, and URLs.

### Step 2: Load Knowledge

Load knowledge in layers and stop when there is enough information.

**Layer 1 — Always load:**
- `references/oryx-overview.md` — Oryx overview, features, deployment, operation, and troubleshooting.

**Layer 2 — Load relevant Oryx documentation:**
1. Load `skills/internal-docs-for-srs/SKILL.md`.
2. Use its **Oryx Documentation** subsection to select the smallest relevant document.
3. Prefer the getting-started guide, FAQ, and repository documentation.
4. Treat dated blogs as scenario-specific guidance. Prefer documentation matching the user's Oryx version when commands, UI labels, or behavior differ.

**Layer 3 — Source-code boundary:**
- Do not inspect Oryx source code under this support skill.
- If the question explicitly depends on underlying SRS behavior, handle that part separately through the SRS workflow rather than loading `references/srs-overview.md` into the Oryx workflow.

### Step 3: Answer by Topic

Apply these rules to all Oryx answers:
- Ground answers in the selected Oryx documentation; do not guess features, endpoints, settings, or UI paths.
- If the knowledge base does not cover something, say: "The knowledge base doesn't cover that yet."
- Prefer dashboard workflows and documented Oryx commands.
- Explain that Oryx is built on SRS but is not another name for standalone SRS.

**Deployment**
- Use the documented Oryx image, volume mounts, environment variables, and exposed ports.
- Always preserve `/data` for configuration, certificates, Redis data, recordings, uploads, and generated media.
- Load the relevant Docker, Helm, script, Lightsail, DigitalOcean, or aaPanel document.

**Streaming and authentication**
- Use dashboard-generated publish and playback URLs.
- Explain the documented global publish-secret model and redact the secret.
- Do not claim per-stream secrets or playback authentication unless documentation for the user's version confirms them.

**HTTPS and WebRTC**
- Verify DNS, certificate state, browser permissions, candidate address, and UDP port 8000.
- Use the current HTTPS and WHIP/WHEP documentation rather than copying raw standalone SRS configuration.

**Recording**
- Explain recording rules, stream filters, task completion, MP4 post-processing, preview, download, and documented storage options.

**Restreaming, virtual live, camera, and transcoding**
- Route each feature to its specific document.
- Distinguish Oryx-managed FFmpeg tasks from SRS transmuxing and built-in audio transcoding.
- For video-only IP cameras, mention the documented silent-audio option when the destination requires audio.

**AI features**
- Route transcription, voice assistant, dubbing, and OCR to their separate documents.
- Never ask users to reveal OpenAI keys.
- Use documented dashboard service tests and feature-specific status views.

**OpenAPI and callbacks**
- Use only documented `/terraform/v1/` endpoints and authentication.
- Direct users to `System > OpenAPI` for a Bearer token when required.
- Explain the `/api/` proxy for underlying SRS APIs only when relevant.

**Oryx versus SRS**
- Explain SRS as the media server and Oryx as the integrated application solution.
- Present the two products in separate sections and compare only documented capabilities.

### Step 4: Troubleshoot Oryx

Gather these details first when missing:
- Oryx version and deployment method
- Docker image tag and command, installer version, or Helm settings
- Host operating system and whether the deployment uses Docker, aaPanel, or Baota
- Whether `/data` is mounted persistently
- Dashboard scenario and settings involved
- Publishing and playback methods and redacted URLs
- Network topology, DNS, HTTPS, firewall, and relevant `docker logs oryx` output

**Diagnostic tools:**
- Version: `curl http://localhost/terraform/v1/mgmt/versions`
- Health: `curl http://localhost/terraform/v1/mgmt/check`
- Container: `docker ps --filter name=oryx`
- Logs: `docker logs --tail 200 oryx`
- Persistent files: `docker exec -it oryx ls -lah /data`
- Protected APIs: obtain the Bearer token from `System > OpenAPI` without exposing it
- Adjust the host or exposed HTTP port in commands for the user's deployment

**Feature or UI is missing after installation or upgrade**
- Compare the deployed image, Helm chart, script, or aaPanel plugin version with the version that documents the feature.
- Oryx upgrades are performed by the deployment platform, not from the dashboard. Preserve `/data` when replacing a Docker container or reinstalling the aaPanel application.
- For a newly installed instance, allow 3–5 minutes for Oryx and Redis to become ready, then refresh the dashboard and check the container logs.
- Prefer Ubuntu 20 or newer. For servers outside China, use aaPanel rather than Baota; install the latest released aaPanel plugin manually when the store version is stale.

**Dashboard domain or automatic HTTPS fails**
- Verify the DNS A record resolves to the public Oryx IP. A local hosts-file entry is insufficient for Let's Encrypt validation.
- Verify public TCP ports 80 and 443 reach the Oryx HTTP and HTTPS ports; automatic certificate validation requires port 80.
- For aaPanel, verify the configured Oryx domain or default website rather than assuming direct IP access is configured.

**A second Oryx instance conflicts with the first**
- Give every instance a unique container name, host `/data` directory, and HTTP, RTMP, WebRTC, and SRT host ports.
- Set `HTTP_PORT`, `RTMP_PORT`, `RTC_PORT`, and `SRT_PORT` to the exposed host ports so dashboard-generated URLs are correct.

**Configuration or recordings disappear after restart**
- Verify that persistent host storage is mounted to `/data`.
- Do not advise keeping persistent state only in the container filesystem.

**A server file is unavailable to virtual live or dubbing**
- The file must be visible inside the container under `/data`; a host path outside a bind mount is not accessible to Oryx.
- Verify the mapped path with `docker exec -it oryx ls -lh /data/<path>` before changing the feature settings.

**Publishing is rejected**
- Verify the dashboard-generated publish URL and global publish secret.
- Do not add the publish secret to playback URLs unless version-matched documentation requires it.
- If a legacy camera cannot send `?secret=...`, load the FAQ before advising its documented stream-name workaround; playback must then use the same stream name.

**WHIP, WHEP, or browser publishing fails**
- Verify HTTPS and camera or microphone permission.
- Verify that the WebRTC candidate is reachable.
- Publish and allow UDP port 8000.

**SRT publishing fails**
- Publish and allow UDP port 10080; TCP 10080 alone is insufficient.

**Recording is not available immediately**
- Verify the recording rule and stream filter.
- Recording may intentionally remain open across brief publishing interruptions. Use the dashboard or documented asynchronous API to end the task when an MP4 is needed promptly.
- Allow segment processing and MP4 post-processing to finish, and use the completion callback rather than assuming the file is immediately available.
- Check task status and Oryx logs before treating the recording as lost.

**Recording to another disk or S3-compatible storage fails**
- For local disks, verify the target is mounted where Oryx can access it under `/data`; do not use a host-only symbolic link for `/data/record` in a Docker deployment.
- For AWS, Azure, DigitalOcean Spaces, or another S3-compatible service, verify the mounted bucket is visible inside Oryx, normally at `/data/srs-s3-bucket`, then restart Oryx.
- Configure `Setup Recording Rules > Post Processing > Copy Record File` to copy completed MP4 files to the bucket mount.
- Never mount the entire `/data` or `/data/record` working directory directly on cloud storage. Temporary recording I/O can overload or suspend the cloud filesystem; use a dedicated bucket directory and post-processing copy.

**Restreaming, virtual live, transcoding, or camera streaming fails**
- Verify the source URL or file, destination URL and key, and FFmpeg task status.
- Check the documented silent-audio option for video-only cameras.
- For a custom RTMP destination in `rtmp://host/app/stream` form, split the value after the final slash into the destination stream key when the dashboard requests separate fields.
- For long-running virtual live events, verify the configured bitrate limit and monthly traffic budget; the documented default input limit is 5 Mbps.

**An AI feature fails**
- Test the configured OpenAI service from the dashboard without exposing the key.
- Load the specific transcription, voice assistant, dubbing, or OCR document before suggesting settings.
- For transcription playback problems, verify the configured FFmpeg video parameters and keep `-bf 0` when the result must also work with WebRTC.

**A custom FFmpeg binary is not used**
- The documented replacement workflow is Docker-only: bind-mount the executable to `/usr/local/bin/ffmpeg`, then verify that path inside the container.
