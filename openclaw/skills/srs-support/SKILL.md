---
name: srs-support
description: Answer SRS (Simple Realtime Server) questions for developers and users — protocols, configuration, architecture, codecs, ecosystem tools, deployment, and troubleshooting. Use when anyone asks about SRS features, how SRS works, supported protocols (RTMP, SRT, WebRTC/WHIP/WHEP, HLS, DASH, HTTP-FLV, RTSP, GB28181), codec support, transmuxing, transcoding, configuration, performance, or the SRS ecosystem (Oryx, srs-bench, WordPress plugin).
---

# SRS Support

Answer questions about SRS using the knowledge base in the SRS repository.

## Setup

The user must have the SRS repository cloned locally. The knowledge files live in the `openclaw/` directory of the repo.

## Finding the Repository

The default and recommended path is `~/git/srs/`. Check there first for `openclaw/memory/srs-overview.md`.

If not found, ask the user to either:
1. Clone the SRS repo to `~/git/srs/` (recommended): `git clone https://github.com/ossrs/srs.git ~/git/srs`
2. Tell you where their existing SRS repo is located

## Loading Knowledge

On first question, load **all** `srs-*.md` files from `openclaw/memory/` into context:

```bash
ls openclaw/memory/srs-*.md
```

Read every file found. Do not selectively load or search — load the entire knowledge base. Modern LLMs have 200K–1M token windows, which is more than enough for the full SRS knowledge base.

## Knowledge Files

All files are in `openclaw/memory/` within the SRS repo:

- **srs-overview.md** — Core reference: what SRS is, supported protocols and codecs, transmuxing/transcoding, sources (Live/SRT/RTC), configuration (`conf/` files and env vars), ecosystem tools, dependencies, community context, performance notes, feature list with versions/dates

More `srs-*.md` files will be added over time as the knowledge base grows.

## Answering Guidelines

- Answer grounded in the knowledge files — do not guess or hallucinate features
- When asked about protocol support, include version and date from the features list
- When asked about codec transcoding, clarify direction (e.g., AAC→Opus for RTMP-to-WebRTC)
- When asked about configuration, reference `conf/full.conf` as the complete reference
- Distinguish between SRS server-side scope and client-side tools (SRS doesn't maintain client-side projects)
- SRS is C++ built on state-threads (ST) coroutine library — mention this for architecture questions
- For questions outside the knowledge base, be upfront: "I don't have detailed knowledge about that aspect yet"
