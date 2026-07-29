# Next-Generation Go Server Code Map

Use this reference after `skills/internal-codemap-for-srs/SKILL.md` routes a task to the next-generation Go server. The listed modules and files define the trusted navigation scope.

## Next-Generation Server Code

The next-generation server (`cmd/` + `internal/`) is written in Go and maintained by AI. It is the future of SRS. Currently it only supports proxy, but work is ongoing to support the full feature set including origin, edge, and proxy servers.

`cmd/proxy` — A stateless reverse proxy that sits in front of one or more SRS C++ origin servers. Accepts client connections (RTMP, HTTP-FLV/HLS, WebRTC WHIP/WHEP, SRT), resolves a backend origin via the load balancer, and transparently proxies traffic. Does not cache streams or process media — only forwards bytes. Configuration entirely via environment variables (or `.env` file), no config file. Supports two deployment modes: single-proxy (in-memory LB) and multi-proxy (Redis-based shared LB for horizontal scaling behind a network load balancer). Entry point: `cmd/proxy/main.go`.

`internal/bootstrap` — Server startup and lifecycle orchestration. Sets up logging context, signal handlers, loads environment, installs force-quit timer, optionally starts pprof, initializes the load balancer (memory or Redis based on `PROXY_LOAD_BALANCER_TYPE`), then starts all six servers sequentially (RTMP, WebRTC, HTTP API, SRT, System API, HTTP Stream) and blocks until context is cancelled. Deferred `Close()` on each server ensures graceful shutdown.

`internal/proxy` — Proxy server implementations. Each server accepts client connections, parses just enough of the protocol to extract the stream URL, picks a backend via the load balancer, and proxies traffic bidirectionally. Contains five proxy servers: (1) **RTMP proxy** (`rtmp.go`) — TCP listener, simple handshake, parses connect/publish/play to get stream URL, bidirectional RTMP message copying, stateless. (2) **HTTP stream proxy** (`http.go`) — serves static files, proxies HTTP-FLV/TS via reverse-proxy, proxies HLS m3u8 with `spbhid` rewriting so TS segment requests route to the same backend. (3) **WebRTC proxy** (`rtc.go`) — two-phase: WHIP/WHEP signaling (SDP rewrite to replace backend UDP port with proxy's) + UDP media transport (identifies connections by STUN ufrag, supports address migration), stateful. (4) **SRT proxy** (`srt.go`) — intercepts SRT 4-step handshake locally, parses stream ID on handshake 2, replays full handshake with backend, then proxies UDP bidirectionally, stateful per-connection. (5) **HTTP API + System API** (`api.go`) — HTTP API delegates WHIP/WHEP to WebRTC server; System API provides `/api/v1/srs/register` where backend SRS C++ servers register themselves so the load balancer knows about them.

`internal/rtmp` — RTMP protocol implementation (parsing, not proxying). Full RTMP chunk stream and message protocol: simple handshake (C0/C1/C2), chunk stream reader/writer with all four format types, extended timestamp, message reassembly from chunks. Defines all RTMP message types, chunk stream IDs, and command names. Packet types include ConnectApp, CreateStream, Publish, Play, Call, SetChunkSize, WindowAcknowledgementSize, SetPeerBandwidth, UserControl. Uses Go generics (`ExpectPacket[T]`) to read until a specific packet type arrives. Also includes full AMF0 encoder/decoder supporting Number, Boolean, String, Object, Null, Undefined, EcmaArray, StrictArray, Date, LongString — with ordered key-value maps, auto-type-discovery, and safe type converters.

`internal/lb` — Load balancer abstraction and two implementations. Defines `OriginLoadBalancer` interface and core types in `lb.go` (Initialize, Update, Pick, HLS/WebRTC state management) and `OriginServer` struct representing a backend origin (IP, listen endpoints for RTMP/HTTP/API/SRT/RTC, heartbeat tracking). **Memory LB** (`mem.go`) — in-memory using `sync.Map`, sticky random pick per stream URL, single-proxy deployment. **Redis LB** (`redis.go`) — Redis-backed shared state with TTL-based expiration, enables multi-proxy horizontal scaling behind a network load balancer. Also includes a debug helper (`debug.go`) that creates a fake backend from env vars when `PROXY_DEFAULT_BACKEND_ENABLED=on` for development without real SRS registration.

`internal/redisclient` — Thin Redis client abstraction. Defines a minimal `RedisClient` interface (`Ping`/`Get`/`Set`/`String`) satisfied by `*redis.Client` from `github.com/go-redis/redis/v8`, plus a `New(addr, password, db)` constructor. Used by `internal/lb/redis.go`.

`internal/logger` — Structured logging with context IDs. Four log levels: Debug/Info (stdout), Warn/Error (stderr). Emits JSON via `log/slog` with `pid` and `cid` attributes. Each connection/request gets a unique 7-char hex context ID for log correlation, stored in `context.Context`.

`internal/env` — Environment-based configuration. All settings via env vars (or `.env` file parsed by an in-tree custom parser — no third-party dep; supports comments, `export` prefix, quoted values, escape sequences, and inline comments). Exposes a `ProxyEnvironment` interface (with a counterfeiter-generated fake in `envfakes/` for downstream tests) with methods for each config value. Default ports: RTMP=11935, HTTP API=11985, HTTP Stream=18080, WebRTC=18000, SRT=20080, System API=12025. Timeouts: grace=20s, force=30s. Supports Redis config and default backend config for debugging.

`internal/version` — Version constants. Signature `SRSX`, version tracks the SRS project version (currently 8.0.x). Used in HTTP API responses and startup logging.

`internal/errors` — Error handling with stack traces, thin wrapper over stdlib `errors`. Provides `New`, `Errorf`, `Wrap`, `Wrapf`, `WithMessage`, `WithStack`, `Cause`, and re-exports `Is`/`As`/`Unwrap`/`Join`. Every error captures a stack trace at creation; `%+v` prints the full trace. `Cause()` walks the error chain to find the root error.

`internal/sync` — Generic sync primitives. `Map[K, V]`: type-safe generic interface over `sync.Map`, constructed via `NewMap[K, V]()`. Used throughout the codebase to avoid raw type assertions.

`internal/signal` — OS signal handling. Listens for SIGINT/SIGTERM, cancels the root context. Installs a force-quit timer (default 30s) as a safety net if graceful shutdown hangs.

`internal/debug` — Profiling. Starts a `net/http/pprof` server if `GO_PPROF` env var is set. Optional, for runtime profiling and debugging.

`internal/utils` — Shared utility functions. HTTP helpers (JSON response, error response, CORS, body parsing). URL helpers (normalize to `vhost/app/stream` format, extract app/stream from HTTP requests). Protocol helpers (classify UDP payload as STUN/RTP/SRT, parse ICE ufrag/pwd from SDP, parse SRT stream ID). Network helpers (detect peer-closed/connection-closed errors, parse listen endpoints).
