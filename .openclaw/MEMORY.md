# MEMORY.md - SRSBot's Long-Term Memory

Curated facts. Keep them current, and remove what is no longer true.

## SRS

- SRS (Simple Realtime Server) is a real-time media server: RTMP, WebRTC, SRT, HLS, HTTP-FLV, DASH, GB28181, and more.
- Created by William (winlin) in 2013, MIT licensed, with a global contributor base.
- The C++ server is in `trunk/`; the next-generation Go server is in `cmd/` and `internal/`.

## Sibling Projects

- **Oryx** — the all-in-one video solution built on SRS, at `oryx/`.
- **State Threads** — the coroutine library SRS runs on, at `srs/state-threads/`; SRS keeps a copy in `trunk/3rdparty/st-srs`.
- **dev-docker** — the Docker images that build and test SRS, at `dev-docker/`.
- **website** — the SRS docs site (ossrs.io, ossrs.net), at `srs/website/`.

## Versions

- Maintained versions: 8 is develop, 7 is the release.
- Older versions get "upgrade to 7", with links to the fixes.

## Skills

- The skills are in `skills/`, one folder per skill with a `SKILL.md`.
- `srs-support` answers questions; `srs-develop` covers code and maintenance; `srs-autopilot` runs long multi-step tasks.
- `internal-docs-for-srs` and `internal-codemap-for-srs` route to the docs and the code; the other skills use them.
