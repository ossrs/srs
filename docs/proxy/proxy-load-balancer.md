# Load Balancer

## Overview

The proxy-go load balancer distributes client streams across multiple backend origin servers. It provides a pluggable interface with two implementations:

1. **Memory Load Balancer** - For single proxy deployments
2. **Redis Load Balancer** - For multi-proxy deployments with shared state

Both implementations maintain stream-to-server mappings to ensure stream consistency - once a stream is assigned to a backend server, all subsequent requests for that stream route to the same server.

## Core Responsibilities

1. Server Management

**Backend Server Registration**:
- Origin servers register themselves with the proxy via System API
- Servers provide their endpoints for each protocol (RTMP, HTTP, WebRTC, SRT)
- Registration includes server identity (ServerID, ServiceID, PID)
- Heartbeat mechanism maintains server health status

**Server Selection**:
- Pick appropriate backend server for new streams
- Consider server health (last heartbeat time)
- Random selection from healthy servers for load distribution
- Maintain stream-to-server mapping for consistency

2. Stream State Management

**Protocol-Specific State**:

- **HLS Streams**: Dual-index storage for M3U8 playlists and TS segments
  - Index by stream URL for initial playlist requests
  - Index by SPBHID (SRS Proxy Backend HLS ID) for segment requests

- **WebRTC Connections**: Dual-index for session management
  - Index by stream URL for initial connection setup
  - Index by ufrag (ICE username) for STUN binding requests

3. Load Balancing Strategy

**Stream-Level Stickiness**:
- First request for a stream selects a backend server
- All subsequent requests for that stream use the same server
- Ensures session continuity and state consistency on backend

**Health-Based Selection**:
- Only consider servers with recent heartbeats (within 300 seconds)
- Fallback to any registered server if no healthy servers available
- Random selection among healthy servers for even distribution

## Architecture

The load balancer uses a clean interface-based architecture:

**Core Interface**: `SRSLoadBalancer`
- Initialization and lifecycle management
- Server registration and updates
- Stream routing (Pick operation)
- Protocol-specific state management (HLS, WebRTC)

**Data Models**:
- `SRSServer`: Backend origin server representation
- `HLSPlayStream`: Interface for HLS streaming sessions
- `RTCConnection`: Interface for WebRTC connections

## Memory Load Balancer

1. Design

**Storage**: In-memory maps for fast access
- Server registry with thread-safe concurrent access
- Stream-to-server mappings
- Protocol-specific session state (HLS, WebRTC)

**Use Case**: Single proxy instance handling moderate stream counts

**Characteristics**:
- Lowest latency (no network operations)
- Simple deployment (no external dependencies)
- State limited to single proxy instance
- Best for deployments where proxy isn't the bottleneck

2. Configuration

```bash
PROXY_LOAD_BALANCER_TYPE=memory
```

## Redis Load Balancer

1. Design

**Storage**: Shared Redis instance for distributed state
- All proxies read/write to same Redis
- TTL-based expiration for automatic cleanup
- JSON serialization for cross-process communication

**Use Case**: Multiple proxy instances sharing load

**Characteristics**:
- Enables horizontal scaling of proxies
- Higher latency (network + serialization overhead)
- Requires Redis infrastructure
- Best for large deployments with many streams

2. Configuration

```bash
PROXY_LOAD_BALANCER_TYPE=redis
PROXY_REDIS_HOST=127.0.0.1
PROXY_REDIS_PORT=6379
PROXY_REDIS_PASSWORD=
PROXY_REDIS_DB=0
```

3. Redis Key Design

**Server Keys**:
- `srs-proxy-server:{serverID}` - Server registration (300s TTL)
- `srs-proxy-all-servers` - Server list index (no expiration)

**Stream Mapping Keys**:
- `srs-proxy-url:{streamURL}` - Stream-to-server mapping (no expiration)

**Session State Keys**:
- `srs-proxy-hls:{streamURL}` - HLS by URL (120s TTL)
- `srs-proxy-spbhid:{spbhid}` - HLS by SPBHID (120s TTL)
- `srs-proxy-rtc:{streamURL}` - WebRTC by URL (120s TTL)
- `srs-proxy-ufrag:{ufrag}` - WebRTC by ufrag (120s TTL)

## Expiration and Cleanup

**Server Heartbeat**: 300 seconds
- Servers must send updates every 30 seconds (recommended)
- Considered dead if no update within 300 seconds
- Memory LB: filtered during selection
- Redis LB: automatic TTL expiration

**Session State**: 120 seconds
- HLS and WebRTC sessions expire after 120 seconds of inactivity
- Automatic cleanup via TTL (Redis) or garbage collection (Memory)
- Sessions renewed on each request

**Stream Mappings**: No expiration
- Stream-to-server mappings persist indefinitely
- Ensures consistent routing for long-running streams
- Only reset when backend server dies or mapping explicitly cleared

## Comparison: Memory vs Redis

| Aspect | Memory Load Balancer | Redis Load Balancer |
|--------|---------------------|---------------------|
| **Deployment** | Single proxy | Multiple proxies |
| **State Storage** | Local memory | Shared Redis |
| **Latency** | Lowest (in-process) | Network + serialization |
| **Scalability** | Single instance | Horizontal scaling |
| **Dependencies** | None | Redis required |
| **Complexity** | Simple | Moderate |
| **Fault Tolerance** | Single point of failure | Multiple proxies |
| **Best For** | Moderate traffic | High traffic, high availability |

