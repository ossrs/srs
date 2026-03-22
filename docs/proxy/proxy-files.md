# Codebase Structure

This document provides an overview of the Go codebase organization.

## Directory Structure

```
/
├── cmd/proxy-go/
│   └── main.go                 # Application entry point
└── internal/
    ├── debug/                  # Go profiling support
    ├── env/                    # Configuration management
    ├── errors/                 # Error handling with stack traces
    ├── lb/                     # Load balancer (memory/Redis)
    ├── logger/                 # Logging and request tracing
    ├── protocol/               # Protocol servers (RTMP, HTTP, WebRTC, SRT, API)
    ├── rtmp/                   # RTMP protocol implementation
    ├── signal/                 # Graceful shutdown handling
    ├── sync/                   # Concurrency utilities
    ├── utils/                  # Common utilities
    └── version/                # Version information
```

## Internal Packages

### debug
Go profiling support via pprof, controlled by `GO_PPROF` environment variable.

### env
Configuration management using environment variables. Loads `.env` file and provides defaults for all server settings.

### errors
Enhanced error handling with stack traces. Provides error wrapping and root cause extraction.

### lb
Load balancer system supporting both single-proxy (memory-based) and multi-proxy (Redis-based) deployments.
- `lb.go` - Core interfaces and types
- `mem.go` - Memory-based load balancer
- `redis.go` - Redis-based load balancer
- `debug.go` - Default backend for testing

### logger
Structured logging with context-based request tracing. Provides log levels: Verbose, Debug, Warning, Error.

### protocol
Protocol server implementations for all supported streaming protocols:
- `rtmp.go` - RTMP protocol stack
- `http.go` - HTTP streaming (HLS, HTTP-FLV, HTTP-TS)
- `rtc.go` - WebRTC server (WHIP/WHEP)
- `srt.go` - SRT server
- `api.go` - HTTP API server

### rtmp
Low-level RTMP protocol implementation including handshake and AMF0 serialization.

### signal
Graceful shutdown coordination. Catches SIGINT/SIGTERM and implements timeout-based shutdown.

### sync
Thread-safe generic Map wrapper around `sync.Map` for connection tracking and caching.

### utils
Common utility functions for HTTP responses, JSON marshaling, and parsing.

### version
Version information and server identification.
