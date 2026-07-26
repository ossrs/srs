# Features

A catalog of what the proxy server (`cmd/proxy`) does today. The proxy is a
**stateless media-streaming reverse proxy with built-in load balancing**: it
accepts client connections, parses just enough of each protocol to extract the
stream URL, picks a backend origin via the load balancer, and forwards bytes. It
does not cache streams or transcode media — the origin servers do the real work.

This document tracks implemented capabilities. For the *why* behind the design,
see [proxy-design.md](proxy-design.md); for usage, see [proxy-usage.md](proxy-usage.md).

## Protocol Proxying

The proxy runs six servers in parallel, started by `internal/bootstrap`. Each
parses the minimum needed to route, then proxies traffic to the chosen backend.

**RTMP proxy** (`internal/proxy/rtmp.go`)

- TCP listener with a simple RTMP handshake (C0/C1/C2) on both the client and
  backend side.
- Parses `connect` (tcUrl), `createStream`, and `publish`/`play` to identify the
  client as a publisher or viewer and extract the stream name.
- Connects to the backend, replays the publish/play handshake, then copies RTMP
  messages bidirectionally between client and backend.
- Fully stateless — all routing state lives in the client request, so no
  cross-proxy synchronization is needed.

**HTTP-FLV / HTTP-TS proxy** (`internal/proxy/http.go`)

- Serves `.flv` and `.ts` pseudo-streaming requests by reverse-proxying to the
  backend's HTTP port and streaming the response body to the client.
- Stateless; CORS is always allowed.

**HLS proxy** (`internal/proxy/http.go`)

- Serves `.m3u8` playlists by proxying to the backend, then rewriting TS segment
  URLs to carry an `spbhid` query parameter (SRS Proxy Backend HLS ID).
- Subsequent TS segment requests carrying `spbhid` are routed back to the *same*
  backend, keeping a multi-request HLS session pinned to one origin (with the
  memory LB; see Notes for the current Redis limitation).

**Static file server** (`internal/proxy/http.go`)

- Optionally serves a static web directory (`PROXY_STATIC_FILES`, default
  `./trunk/research`) for player pages, falling through to `404` when unset.

**WebRTC WHIP/WHEP proxy** (`internal/proxy/rtc.go`)

- Two-phase: (1) signaling — forwards the WHIP/WHEP SDP exchange to the backend's
  HTTP API and rewrites the backend's WebRTC UDP port in the SDP answer to the
  proxy's own port; (2) media — relays UDP packets bidirectionally.
- Identifies connections by ICE ufrag parsed from the STUN binding request, so a
  client that migrates to a new UDP address is re-associated with its stream on
  the same proxy. (Cross-proxy re-association is designed for via the shared
  load balancer, but does not work with the Redis LB yet — see Notes.)
- Stateful per-connection; the ufrag→connection mapping is stored in the load
  balancer and cached locally for fast lookup.
- Backward compatible with the legacy SRS `/rtc/v1/publish/` and `/rtc/v1/play/`
  APIs, including unwrapping the `{"sdp":"..."}` JSON envelope before parsing ICE
  attributes.

**SRT proxy** (`internal/proxy/srt.go`)

- Intercepts the SRT 4-step handshake locally: answers induction (handshake 0→1),
  parses the stream ID from the conclusion (handshake 2), then replays the full
  handshake against the backend and relays the result back to the client.
- Parses the SRT stream ID (e.g. `#!::r=app/stream,m=publish|request`) to build
  the stream URL and pick a backend: `h=` (optional vhost, defaults to
  `localhost`) and `r=` (app/stream) are used for routing; `m=` is ignored.
- Relays UDP bidirectionally per connection, keyed by SRT socket ID. Does **not**
  support client address migration (unlike WebRTC).

## HTTP APIs

**HTTP API server** (`internal/proxy/api.go`, default port 11985)

- `/api/v1/versions` — version / health check.
- `/rtc/v1/whip/`, `/rtc/v1/whep/` — WebRTC signaling, delegated to the WebRTC
  proxy server.
- `/rtc/v1/publish/`, `/rtc/v1/play/` — legacy SRS WebRTC signaling aliases (used
  by srs-bench), delegated to the same handlers.

**System API server** (`internal/proxy/api.go`, default port 12025)

- `/api/v1/srs/register` — backend origin servers register themselves here so the
  load balancer learns their per-protocol listen endpoints.
- `/api/v1/versions` — version / health check.

## Load Balancing

(`internal/lb` — interface in `lb.go`, implementations in `mem.go` / `redis.go`)

- **Pluggable interface** (`OriginLoadBalancer`) selected at startup by
  `PROXY_LOAD_BALANCER_TYPE`.
- **Memory load balancer** — in-memory `sync.Map` state for single-proxy
  deployments; lowest latency, no external dependencies.
- **Redis load balancer** — shared state with TTL-based expiration for
  multi-proxy deployments scaling horizontally behind a network load balancer.
- **Redis key namespaces** — optional `PROXY_REDIS_KEY_PREFIX`; empty by default
  for backward compatibility, or set to isolate independent proxy clusters that
  share one Redis database.
- **Stream-level stickiness** — the first request for a stream URL picks a
  backend; every later request for that stream routes to the same backend.
  Stream→server mappings never expire.
- **Health-based selection** — servers with a heartbeat within 300s
  (`ServerAliveDuration`) are preferred; selection among them is random for even
  distribution. The memory LB falls back to all registered servers when none are
  healthy; the Redis LB expires dead servers via a 300s TTL instead.
- **HLS session state** — dual-indexed by stream URL and by `spbhid`. In Redis,
  entries expire after 120s (`HLSAliveDuration`); the memory LB keeps them
  in-process without expiration.
- **WebRTC session state** — dual-indexed by stream URL and by ICE ufrag. In
  Redis, entries expire after 120s (`RTCAliveDuration`); the memory LB keeps
  them in-process without expiration.

## Backend Registration

(`internal/proxy/api.go`, `internal/lb/debug.go`; see [proxy-protocol.md](proxy-protocol.md))

- **Automatic registration** — SRS 5.0+ origins self-register and heartbeat via
  the heartbeat feature pointed at the System API (recommended for production).
- **Manual registration** — any media server (or a custom script) can POST to
  `/api/v1/srs/register` with its identity and listen endpoints.
- **Default backend** — for development/testing, the proxy can fabricate a single
  backend from `PROXY_DEFAULT_BACKEND_*` env vars when
  `PROXY_DEFAULT_BACKEND_ENABLED=on`, re-registering it every 30s to keep it
  alive.
- Each origin advertises per-protocol endpoints (RTMP mandatory; HTTP, API, SRT,
  RTC optional) plus identity fields (`ip`, `server`, `service`, `pid` mandatory;
  `device_id` optional). Endpoints accept `port`, `proto://ip:port`, or
  `proto://:port`.

## Deployment Modes

(see [proxy-design.md](proxy-design.md))

- **Single-proxy** — one proxy with the memory load balancer in front of multiple
  origins; for moderate stream counts.
- **Multi-proxy** — multiple stateless proxies sharing a Redis load balancer
  behind an NLB, for horizontal scaling.
- **Complete cluster (future)** — edge servers implemented as proxies with caching
  enabled, aggregating viewer connections in front of the proxy/origin tiers.

## Configuration

(`internal/env/env.go`)

- **Entirely environment-variable driven** — no config file. Sensible defaults are
  filled in for every setting at startup.
- **Built-in `.env` parser** — no third-party dependency; supports comments, the
  `export` prefix, quoted values (escape sequences in double quotes, raw literals
  in single quotes), and inline comments. Existing process env vars are never
  overwritten.
- Default ports: RTMP `11935`, HTTP API `11985`, HTTP Stream `18080`, WebRTC
  `18000` (UDP), SRT `20080` (UDP), System API `12025`.

## Operations & Lifecycle

- **Graceful shutdown** (`internal/bootstrap`, `internal/signal`) — SIGINT/SIGTERM
  cancels the root context; each server closes in turn. A grace timeout
  (`PROXY_GRACE_QUIT_TIMEOUT`, default 20s) bounds HTTP server drain.
- **Force-quit safety net** — a force timer (`PROXY_FORCE_QUIT_TIMEOUT`, default
  30s) guarantees the process exits even if a graceful shutdown hangs.
- **Structured logging** (`internal/logger`) — JSON via `log/slog`, with `pid`,
  `version`, and a per-connection 7-char hex context ID (`cid`) for correlation.
- **Profiling** (`internal/debug`) — optional `net/http/pprof` server enabled by
  the `GO_PPROF` env var. See [../perf/proxy-whep.md](../perf/proxy-whep.md).
- **Version / health endpoints** — `/api/v1/versions` is exposed on the HTTP API,
  System API, and HTTP Stream servers and doubles as a health check.

## Notes

- The proxy forwards bytes only; transmuxing between protocols (e.g. playing an
  RTMP-published stream as HLS/HTTP-FLV/WebRTC) is performed by the **backend
  origin**, not the proxy. The proxy's job is to route each protocol's request to
  an origin that can serve it.
- **Redis LB limitation** — the Redis load balancer cannot yet deserialize HLS
  and WebRTC session objects back from Redis (`LoadHLSBySPBHID` and
  `LoadWebRTCByUfrag` return an error). So with
  `PROXY_LOAD_BALANCER_TYPE=redis`, HLS playback through the proxy fails on TS
  segment requests, and a WebRTC client is only recognized by the proxy that
  handled its signaling (no cross-proxy migration). RTMP, HTTP-FLV/TS, and SRT
  are unaffected.
- Current version: SRS `8.0.x` (next-generation Go server, signature `SRSX`).
