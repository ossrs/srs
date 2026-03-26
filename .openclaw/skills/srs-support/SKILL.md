---
name: srs-support
description: Answer SRS (Simple Realtime Server) questions for developers and users — protocols, configuration, architecture, codecs, ecosystem tools, deployment, and troubleshooting. Use when anyone asks about SRS features, how SRS works, supported protocols (RTMP, SRT, WebRTC/WHIP/WHEP, HLS, DASH, HTTP-FLV, RTSP, GB28181), codec support, transmuxing, transcoding, configuration, performance, or the SRS ecosystem (srs-bench, state-threads). Also use when someone asks how to publish or play streams, compare SRS to other media servers, or troubleshoot streaming issues.
---

# SRS Support

Answer questions about SRS — a simple, high-efficiency, real-time media server — using the project's knowledge base.

This skill is for **answering questions and providing guidance**. If the user wants to learn SRS internals, write code, or work through hands-on exercises, hand off to `srs-learn` or `st-develop` instead.

## Setup

All files are in the current working directory. Find everything from here — no discovery logic needed.

Available directories: `trunk/`, `cmd/`, `internal/`, `cmake/`, `docs/`, `memory/`

All AI tools — OpenClaw, Codex, Claude Code, Kiro CLI — see the same relative paths.

## Loading Knowledge

Load knowledge selectively based on the question topic:

- **Always load first:** `memory/srs-overview.md` — this covers protocols, codecs, transmuxing, configuration, features, ecosystem, performance, and most support questions.
- **Load on demand:** `memory/srs-coroutines.md` — only load this when the question is specifically about SRS architecture internals, coroutines, State Threads, or how SRS handles concurrency. Most user questions don't need this. Note: this knowledge base is evaluated by the `st-develop` skill's evals, not by this skill's evals.
- **Load on demand:** `memory/srs-codebase-map.md` — only load this when you need to navigate the source code (e.g., the knowledge base doesn't answer the question and you need to find specific files or modules). Most support questions can be answered from the overview alone.

As the knowledge base grows, new `srs-*.md` files will appear. List `memory/srs-*.md` to discover them, and load only the ones relevant to the question.

## Codebase Navigation (CRITICAL)

When the knowledge base doesn't have the answer and you need to search the source code, **DO NOT grep from the repository root**. Instead:

1. Load `memory/srs-codebase-map.md` first
2. Reason about which module/files are relevant to the question
3. Go directly to those files

This rule applies to all code searching — config lookups, feature investigations, implementation details. Always use the codebase map to navigate, never blind search.

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
- Reference `trunk/conf/full.conf` as the complete configuration reference
- For specific features, point to the relevant config option and its vhost setting
- Mention environment variable support for Docker/cloud-native deployments
- For getting started, recommend `trunk/conf/console.conf` for local testing

### Deployment & Getting Started
- Provide the standard build steps: `cd trunk && ./configure && make`
- Show the basic publish/play workflow with FFmpeg and common players
- For Docker questions, reference `trunk/conf/docker.conf`
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
- Use the `trunk/doc/source.flv` test file for publish examples (it ships with the repo)
- When a question spans support and learning (e.g., "how does the RTMP handshake work internally?"), answer the high-level question here and suggest `srs-learn` for the deep dive
