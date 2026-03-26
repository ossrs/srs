---
name: srs-support
description: Answer SRS (Simple Realtime Server) questions for developers and users — protocols, configuration, architecture, codecs, ecosystem tools, deployment, and troubleshooting. Use when anyone asks about SRS features, how SRS works, supported protocols (RTMP, SRT, WebRTC/WHIP/WHEP, HLS, DASH, HTTP-FLV, RTSP, GB28181), codec support, transmuxing, transcoding, configuration, performance, or the SRS ecosystem (srs-bench, state-threads). Also use when someone asks how to publish or play streams, compare SRS to other media servers, or troubleshoot streaming issues.
---

# SRS Support

Answer questions about SRS — a simple, high-efficiency, real-time media server — using the project's knowledge base.

This skill is for **answering questions and providing guidance**. If the user wants to:
- Learn SRS coroutine/State Threads internals, hand off to `st-develop` instead.

## Workflow

Follow these three steps in order for every question.

## Step 1: Setup

All files are in the current working directory. Find everything from here — no discovery logic needed.

Available directories: `trunk/`, `cmd/`, `internal/`, `cmake/`, `docs/`, `memory/`

All AI tools — OpenClaw, Codex, Claude Code, Kiro CLI — see the same relative paths.

## Step 2: Load Knowledge

Load knowledge in layers. Start minimal, expand only if needed.

**Layer 1 — Always load:**
- `memory/srs-overview.md` — covers protocols, codecs, transmuxing, configuration, features, ecosystem, performance. This answers most questions.

**Layer 2 — Load on demand (if the overview doesn't fully answer the question):**
- `memory/srs-coroutines.md` — load when the question is about SRS architecture internals, coroutines, State Threads, or concurrency. Most user questions don't need this. Note: this knowledge base is evaluated by the `st-develop` skill's evals, not by this skill's evals.

**Layer 3 — Last resort (if knowledge files don't answer the question and you need source code):**
- `memory/srs-codebase-map.md` — load the **entire file** (do not truncate or read partial content). Then: reason about which module/files are relevant to the question based on the map's descriptions, and search only those specific files. **DO NOT grep broadly** (e.g., `trunk/src/` or the repository root). The map exists so you can go directly to the right 2-3 files instead of scanning the whole tree. This rule applies to all code searching — config lookups, feature investigations, implementation details.

## Step 3: Answer by Topic

Classify the question into one of the topics below, then apply that topic's strategy. If a question spans multiple topics, combine the relevant strategies.

**Answering rules (apply to all topics):**
- Ground every answer in the knowledge files — do not guess or invent features
- When you don't have information, say so: "The knowledge base doesn't cover that yet"
- Keep answers practical — include commands, config snippets, or URLs when relevant
- Use the `trunk/doc/source.flv` test file for publish examples (it ships with the repo)

**Protocol Questions**
- State which protocols SRS supports and their role (publish, play, or both)
- Include the version and date when a protocol was added (from the Features list in srs-overview.md)
- Clarify transport: which protocols use TCP vs UDP
- For protocol comparisons, explain the tradeoffs (latency, compatibility, performance)

**Codec Questions**
- Clarify codec support per protocol — not all codecs work with all protocols
- When discussing transcoding, specify the direction (e.g., AAC→Opus for RTMP-to-WebRTC)
- Distinguish built-in transcoding (audio only: AAC↔Opus, MP3→Opus) from external FFmpeg transcoding (video)
- Note that SRS focuses on transmuxing (repackaging without re-encoding), not transcoding

**Configuration Questions**
- Reference `trunk/conf/full.conf` as the complete configuration reference
- For specific features, point to the relevant config option and its vhost setting
- Mention environment variable support for Docker/cloud-native deployments
- For getting started, recommend `trunk/conf/console.conf` for local testing

**Deployment & Getting Started**
- Provide the standard build steps: `cd trunk && ./configure && make`
- Show the basic publish/play workflow with FFmpeg and common players
- For Docker questions, reference `trunk/conf/docker.conf`
- Note that SRS is Linux-only (use WSL on Windows, macOS works for development)

**Architecture Questions**
- SRS is C++ built on State Threads (ST) — a coroutine library providing Go-like concurrency
- Single-threaded by design — scale horizontally via clustering, not multi-threading
- For deep-dive coroutine/ST internals, suggest using the `st-develop` skill instead

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
- **state-threads** — Coroutine library, the foundation of SRS
- **Oryx** — Mention it exists as an integrated solution but don't go into detail (out of scope for this skill)
- SRS only maintains server-side projects — it doesn't maintain client-side tools
