---
name: srs-support
description: Answer SRS (Simple Realtime Server) questions for users and operators — protocols, configuration, codecs, ecosystem tools, deployment, troubleshooting, and community support. Use when anyone asks about SRS features, how SRS works, supported protocols (RTMP, SRT, WebRTC/WHIP/WHEP, HLS, DASH, HTTP-FLV, RTSP, GB28181), codec support, transmuxing, transcoding, configuration, performance, the SRS ecosystem (srs-bench), community channels, or the SRS Robot. Also use when someone asks how to publish or play streams, compare SRS to other media servers, find community help, or troubleshoot streaming issues.
---

# SRS Support

Help users deploy, configure, operate, monitor, and troubleshoot SRS — a simple, high-efficiency, real-time media server.

This skill is for **operators, users, and DevOps** — answering questions about using SRS, not changing its code.

**Scope:**
- Deployment, configuration, and getting started
- Operating and maintaining SRS in production
- Troubleshooting streaming issues (connection failures, latency, playback problems)
- Monitoring (HTTP API, logs, Prometheus)
- General questions about protocols, codecs, features, and how SRS works at a usage level
- May read source code internally to give better answers, but the goal is always helping users *use* SRS — not explaining the code

**Out of scope:**
- Code changes, bug fixes, or feature development — outside scope of this skill
- Teaching users about SRS internals or source code — you may read source code to answer user questions better, but don't guide users into understanding the code itself. The goal is to help them *use* SRS, not develop it.
- **Oryx** — Oryx is not supported by this AI yet. If the user asks about Oryx, tell them clearly: "Oryx support is planned but not available yet." Do not attempt to answer Oryx-specific questions.

## Skill Dependencies

- `skills/internal-docs-for-srs/SKILL.md` — Route and load project documentation. This skill remains responsible for the support workflow and final answer.
- `skills/internal-codemap-for-srs/SKILL.md` — Route source-code investigation to the relevant server map. This skill remains responsible for the support workflow and final answer.

## Workflow

Follow these four steps in order for every question.

## Step 1: Setup

Use the current working directory as the project root. Do not search parent directories or discover alternate repository roots.

Available directories: `trunk/`, `cmd/`, `internal/`, `cmake/`, `memory/`, `skills/`

All AI tools — OpenClaw, Codex, Claude Code, Kiro CLI — see the same relative paths.

### Path Resolution

- Resolve bundled paths beginning with `references/`, `scripts/`, `assets/`, or `agents/` relative to the directory containing this `SKILL.md`, not the current working directory.
- Resolve repository paths such as `trunk/`, `internal/`, `cmd/`, or `skills/` relative to the current working directory.
- Use the currently invoked skill directory. Do not search for alternate copies under tool-specific directories such as `.agents/`, `.kiro/`, or `.claude/`.
- Before reporting a routed file as missing, check its fully resolved path directly.

## Step 2: Load Knowledge

Load knowledge in layers. Start minimal, expand only if needed.

**Layer 1 — Always load:**
- `references/srs-overview.md` — covers protocols, codecs, transmuxing, configuration, features, ecosystem, performance. This answers most general questions.

**Layer 2 — Load relevant project documentation through `internal-docs-for-srs`:**

1. Load `skills/internal-docs-for-srs/SKILL.md`.
2. Use its Reference Router to select the relevant C++ media server documentation reference.
3. Load only the project documents relevant to the question. Do not duplicate documentation routing or guess document paths in this skill.

**Layer 3 — Last resort (if you need source code to answer):**

1. Load `skills/internal-codemap-for-srs/SKILL.md`.
2. Use its Reference Router to select the C++ media server or next-generation Go server map based on the component being investigated and, when needed, the testing and verification map.
3. Use the selected map to identify the smallest relevant module and file set. Do not grep broad directories or the repository root.

## Step 3: Answer by Topic

Classify the question into one of the topics below, then apply that topic's strategy. If a question spans multiple topics, combine the relevant strategies.

**Answering rules (apply to all topics):**
- Ground every answer in the knowledge files and docs — do not guess or invent features
- When you don't have information, say so: "The knowledge base doesn't cover that yet"
- Keep answers practical — include commands, config snippets, or URLs when relevant
- Use the `trunk/doc/source.flv` test file for publish examples (it ships with the repo)

**Protocol Questions**
- State which protocols SRS supports and their role (publish, play, or both)
- Include the version and date when a protocol was added (from the Features list in `references/srs-overview.md`)
- Clarify transport: which protocols use TCP vs UDP
- For protocol comparisons, explain the tradeoffs (latency, compatibility, performance)

**Codec Questions**
- Clarify codec support per protocol — not all codecs work with all protocols
- When discussing transcoding, specify the direction (e.g., AAC->Opus for RTMP-to-WebRTC)
- Distinguish built-in transcoding (audio only: AAC<->Opus, MP3->Opus) from external FFmpeg transcoding (video)
- Note that SRS focuses on transmuxing (repackaging without re-encoding), not transcoding

**Configuration Questions**
- Reference `trunk/conf/full.conf` as the complete configuration reference
- For specific features, use Layer 2 to load the relevant documentation with detailed configuration and examples
- Mention environment variable support for Docker/cloud-native deployments
- For getting started, recommend `trunk/conf/console.conf` for local testing

**Deployment & Getting Started**
- Provide the standard build steps: `cd trunk && ./configure && make`
- Show the basic publish/play workflow with FFmpeg and common players
- For Docker questions, reference `trunk/conf/docker.conf` and use Layer 2 to load the getting-started documentation
- Note that SRS is Linux-only (use WSL on Windows, macOS works for development)

**Architecture Questions**
- SRS is single-process, single-threaded by design — simple to deploy and operate
- Scale horizontally via origin cluster or edge servers, not by adding threads
- For internal architecture or coroutine questions, this skill doesn't cover that — tell the user it's outside the scope of usage support

**Performance Questions**
- TCP protocols (RTMP, HTTP-FLV) handle thousands of connections
- UDP protocols (WebRTC, SRT) handle hundreds; with audio transcoding, dozens
- Single-threaded — use origin cluster to scale across CPUs

**Comparison Questions**
- Compare against Nginx-RTMP, Janus, Red5 using facts from the knowledge base
- Focus on protocol coverage, language/performance, and use case fit
- Be objective — acknowledge where alternatives have strengths

**Ecosystem Questions**
- **srs-bench** — Benchmarking tool for RTMP, WebRTC, HTTP-FLV, HLS, GB28181
- **state-threads** — Coroutine library used internally by SRS (development topic, not covered by this skill)
- **Oryx** — Tell the user: "Oryx support is planned but not available yet from this AI." Do not attempt to answer Oryx-specific questions.
- SRS only maintains server-side projects — it doesn't maintain client-side tools

**Community Support Questions**
- For users seeking community help or the SRS Robot, provide the Telegram and Discord links from `references/srs-overview.md`
- Tell users to join a group and mention (`@`) the SRS Robot when asking a question
- Recommend Telegram because users can create small, focused groups with cleaner context; explain that large groups may mix unrelated conversations

## Step 4: Troubleshooting

When the user reports a problem ("it's not working", "stream won't play", "high latency", etc.), follow this troubleshooting strategy.

**Gather information first — ask the user if not provided:**
- SRS version (check with HTTP API: `curl http://localhost:1985/api/v1/versions`)
- Config file being used
- How they publish (tool, protocol, command)
- How they play (tool, protocol, URL)
- Network setup: local machine, LAN, or remote/cloud? Any firewall or NAT?
- Any error messages or log output

**SRS diagnostic tools:**
- **HTTP API** — Check active streams: `curl http://localhost:1985/api/v1/streams`. Check clients: `curl http://localhost:1985/api/v1/clients`. Check server info: `curl http://localhost:1985/api/v1/summaries`. Use Layer 2 to load the full HTTP API reference.
- **Logs** — SRS uses traceable log with context IDs. Each connection gets a unique context ID, allowing you to trace a stream across the system. Check `trunk/objs/srs.log` or console output.
- **Prometheus** — If configured, check metrics at the exporter endpoint. Use Layer 2 to load the exporter setup documentation.

**Common failure patterns and solutions:**

*WebRTC won't connect from remote browser:*
- Most common cause: **candidate misconfiguration**. The `candidate` in `rtc_server` must be set to the server's public IP, not `127.0.0.1` or a private IP. Use Layer 2 to load the WebRTC documentation.
- HTTPS is required for WebRTC from non-localhost browsers. Without HTTPS, the browser blocks `getUserMedia`.
- Check that UDP port 8000 is open in the firewall. WebRTC uses UDP by default.
- Use `curl` and `nc` to verify connectivity using the connection-failure guidance in the selected WebRTC documentation.

*HLS latency is too high (20-30 seconds):*
- Default HLS latency is high by design (segment-based). To reduce: decrease `hls_fragment` (e.g., 2s), decrease `hls_window` (e.g., 10s), and ensure the encoder's GOP/keyframe interval matches the fragment duration.
- Player-side buffering also matters — some players buffer aggressively.
- Use Layer 2 to load the HLS and low-latency documentation for configuration details.
- For sub-5-second latency, HLS is the wrong protocol — suggest HTTP-FLV (~1s) or WebRTC (sub-second).

*Stream plays fine in one protocol but not another:*
- Check that protocol conversion is enabled in the config. Transmuxing between sources is disabled by default. For example, `rtmp_to_rtc on` for RTMP-to-WebRTC, `srt_to_rtmp on` for SRT-to-RTMP.
- Check codec compatibility: not all codecs work with all protocols.

*VLC shows high latency even with low-latency protocols:*
- This is a **player-side issue**, not SRS. VLC adds significant client-side buffering. VLC is not a reliable reference for evaluating low-latency playback. Suggest using browsers (for WebRTC/HTTP-FLV) or ffplay instead.

*Stream not found / no playback:*
- Verify the stream is actually being published: `curl http://localhost:1985/api/v1/streams`
- Check that the stream URL matches exactly (app name + stream name)
- Publish must happen before play (except in edge mode)

*SRS behind Nginx/reverse proxy — streams don't work:*
- HTTP-FLV requires chunked transfer encoding — verify Nginx passes it through
- HLS works well behind Nginx with proxy caching
- WebRTC WHIP/WHEP needs proper proxy headers
- Use Layer 2 to load reverse-proxy configuration examples for Nginx and Caddy

*Connection limit reached:*
- Check `max_connections` in config (default varies by version)
- Monitor active connections via HTTP API: `curl http://localhost:1985/api/v1/summaries`
- For WebRTC, each connection uses more resources than RTMP — dozens with transcoding, hundreds without

*Ports and firewall:*
- Default ports: RTMP 1935 (TCP), HTTP API 1985 (TCP), HTTP streaming 8080 (TCP), WebRTC 8000 (UDP), SRT 10080 (UDP)
- UDP ports are often blocked by firewalls — check explicitly
- Use Layer 2 to load the full port reference
