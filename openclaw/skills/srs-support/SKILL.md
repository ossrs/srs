---
name: srs-support
description: Answer SRS (Simple Realtime Server) questions for developers and users — protocols, configuration, architecture, codecs, ecosystem tools, deployment, and troubleshooting. Use when anyone asks about SRS features, how SRS works, supported protocols (RTMP, SRT, WebRTC/WHIP/WHEP, HLS, DASH, HTTP-FLV, RTSP, GB28181), codec support, transmuxing, transcoding, configuration, performance, or the SRS ecosystem (srs-bench, state-threads). Also use when someone asks how to publish or play streams, compare SRS to other media servers, or troubleshoot streaming issues.
---

# SRS Support

Answer questions about SRS — a simple, high-efficiency, real-time media server — using the project's knowledge base.

This skill is for **answering questions and providing guidance**. If the user wants to learn SRS internals, write code, or work through hands-on exercises, hand off to `srs-learn` or `st-develop` instead.

## Setup

Resolve `SRS_ROOT` dynamically (do not hardcode paths):

1. If `SRS_ROOT` env is set and contains `openclaw/memory/srs-overview.md`, use it.
2. Else, if the current workspace (or its git root) contains `openclaw/memory/srs-overview.md`, use that.
3. Else, if `~/git/srs/openclaw/memory/srs-overview.md` exists, use `~/git/srs`.
4. Else, ask the user for their SRS repo root.

All paths below are relative to `$SRS_ROOT`.

## Loading Knowledge

Load knowledge selectively based on the question topic:

- **Always load first:** `openclaw/memory/srs-overview.md` — this covers protocols, codecs, transmuxing, configuration, features, ecosystem, performance, and most support questions.
- **Load on demand:** `openclaw/memory/srs-coroutines.md` — only load this when the question is specifically about SRS architecture internals, coroutines, State Threads, or how SRS handles concurrency. Most user questions don't need this. Note: this knowledge base is evaluated by the `st-develop` skill's evals, not by this skill's evals.

As the knowledge base grows, new `srs-*.md` files will appear. List `openclaw/memory/srs-*.md` to discover them, and load only the ones relevant to the question.

## Answering by Topic

### Protocol Questions
- State which protocols SRS supports and their role (publish, play, or both)
- Include the version and date when a protocol was added (from the Features list in srs-overview.md)
- Clarify transport: which protocols use TCP vs UDP
- For protocol comparisons, explain the tradeoffs (latency, compatibility, performance)

### Codec Questions
- Clarify codec support per protocol — not all codecs work with all protocols
- When discussing transcoding, specify the direction (e.g., AAC→Opus for RTMP-to-WebRTC)
- Distinguish built-in transcoding (audio only: AAC↔Opus, MP3→Opus) from external FFmpeg transcoding (video)
- Note that SRS focuses on transmuxing (repackaging without re-encoding), not transcoding

### Configuration Questions
- Reference `conf/full.conf` as the complete configuration reference
- For specific features, point to the relevant config option and its vhost setting
- Mention environment variable support for Docker/cloud-native deployments
- For getting started, recommend `conf/console.conf` for local testing

### Deployment & Getting Started
- Provide the standard build steps: `cd srs/trunk && ./configure && make`
- Show the basic publish/play workflow with FFmpeg and common players
- For Docker questions, reference `conf/docker.conf`
- Note that SRS is Linux-only (use WSL on Windows, macOS works for development)

### Architecture Questions
- SRS is C++ built on State Threads (ST) — a coroutine library providing Go-like concurrency
- Single-threaded by design — scale horizontally via clustering, not multi-threading
- Load `srs-coroutines.md` only if the user wants deeper architectural detail
- For deep-dive architecture learning, suggest using the `srs-learn` skill instead

### Performance Questions
- TCP protocols (RTMP, HTTP-FLV) handle thousands of connections
- UDP protocols (WebRTC, SRT) handle hundreds; with audio transcoding, dozens
- Single-threaded — use origin cluster to scale across CPUs

### Comparison Questions
- Compare against Nginx-RTMP, Janus, Red5 using facts from the knowledge base
- Focus on protocol coverage, language/performance, and use case fit
- Be objective — acknowledge where alternatives have strengths

### Ecosystem Questions
- **srs-bench** — Benchmarking tool for RTMP, WebRTC, HTTP-FLV, HLS, GB28181
- **state-threads** — Coroutine library, the foundation of SRS
- **Oryx** — Mention it exists as an integrated solution but don't go into detail (out of scope for this skill)
- SRS only maintains server-side projects — it doesn't maintain client-side tools

## Answering Guidelines

- Ground every answer in the knowledge files — do not guess or invent features
- When you don't have information, say so: "The knowledge base doesn't cover that yet"
- Keep answers practical — include commands, config snippets, or URLs when relevant
- Use the `doc/source.flv` test file for publish examples (it ships with the repo)
- When a question spans support and learning (e.g., "how does the RTMP handshake work internally?"), answer the high-level question here and suggest `srs-learn` for the deep dive
