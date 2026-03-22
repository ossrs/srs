# Protocol

## Backend Server Registration

The origin server can register itself to the proxy server, so the proxy server can load balance
the backend servers.

### Default Backend Server (For Debugging)

The proxy can automatically register a default backend server for testing and debugging purposes, controlled by environment variables:

```bash
# Enable default backend server
PROXY_DEFAULT_BACKEND_ENABLED=on

# Default backend server configuration
PROXY_DEFAULT_BACKEND_IP=127.0.0.1
PROXY_DEFAULT_BACKEND_RTMP=1935
PROXY_DEFAULT_BACKEND_HTTP=8080       # Optional
PROXY_DEFAULT_BACKEND_API=1985        # Optional
PROXY_DEFAULT_BACKEND_RTC=8000        # Optional (UDP)
PROXY_DEFAULT_BACKEND_SRT=10080       # Optional (UDP)
```

When enabled, the proxy automatically registers this default backend server at startup and sends heartbeats every 30 seconds to keep it alive. This is useful for:
- Quick testing without setting up backend server registration
- Development and debugging scenarios
- Single-server deployments

### Automatic Registration

SRS 5.0+ has built-in support for automatic registration to the proxy server using the heartbeat feature. Configure SRS to send heartbeats to the proxy's System API:

```nginx
# For example, conf/origin1-for-proxy.conf in SRS.
heartbeat {
    enabled on;
    interval 9;
    url http://127.0.0.1:12025/api/v1/srs/register;
    device_id origin1;
    ports on;
}
```

When heartbeat is enabled:
- SRS automatically registers itself on startup
- Sends periodic heartbeats (default: every 30 seconds) to keep the registration alive
- Proxy marks servers as unavailable if heartbeats stop (after 300 seconds)
- No manual intervention required - fully automatic

This is the **recommended approach** for production deployments with SRS backend servers.

### Manual Registration API

For non-SRS backend servers or custom integrations, use the HTTP API directly:

```bash
curl -X POST http://127.0.0.1:12025/api/v1/srs/register \
     -H "Connection: Close" \
     -H "Content-Type: application/json" \
     -H "User-Agent: curl" \
     -d '{
          "device_id": "origin2",
          "ip": "10.78.122.184",
          "server": "vid-46p14mm",
          "service": "z2s3w865",
          "pid": "42583",
          "rtmp": ["19352"],
          "http": ["8082"],
          "api": ["19853"],
          "srt": ["10082"],
          "rtc": ["udp://0.0.0.0:8001"]
        }'
#{"code":0,"pid":"53783"}
```

### Registration Fields

* `ip`: Mandatory, the IP of the backend server. Make sure the proxy server can access the backend server via this IP.
* `server`: Mandatory, the server id of backend server. For SRS, it stores in file, may not change.
* `service`: Mandatory, the service id of backend server. For SRS, it always changes when restarted.
* `pid`: Mandatory, the process id of backend server. Used to identify whether process restarted.
* `rtmp`: Mandatory, the RTMP listen endpoints of backend server. Proxy server will connect backend server via this port for RTMP protocol.
* `http`: Optional, the HTTP listen endpoints of backend server. Proxy server will connect backend server via this port for HTTP-FLV or HTTP-TS protocol.
* `api`: Optional, the HTTP API listen endpoints of backend server. Proxy server will connect backend server via this port for HTTP-API, such as WHIP and WHEP.
* `srt`: Optional, the SRT listen endpoints of backend server. Proxy server will connect backend server via this port for SRT protocol.
* `rtc`: Optional, the WebRTC listen endpoints of backend server. Proxy server will connect backend server via this port for WebRTC protocol.
* `device_id`: Optional, the device id of backend server. Used as a label for the backend server.

### Listen Endpoint Format

The listen endpoint format is `port`, or `protocol://ip:port`, or `protocol://:port`, for example:

* `1935`: Listen on port 1935 and any IP for TCP protocol.
* `tcp://:1935`: Listen on port 1935 and any IP for TCP protocol.
* `tcp://0.0.0.0:1935`: Listen on port 1935 and any IP for TCP protocol.
* `tcp://192.168.3.10:1935`: Listen on port 1935 and specified IP for TCP protocol.

### Integration Options Summary

There are three ways to register backend servers to the proxy:

1. **Automatic Registration (Recommended for Production)**
   - Use SRS 5.0+ with heartbeat feature
   - Fully automatic, no manual scripts needed
   - Self-healing: automatically re-registers if proxy restarts
   - See "Automatic Registration (SRS 5.0+ Heartbeat)" section above

2. **Manual Registration API**
   - For non-SRS media servers (nginx-rtmp, Node-Media-Server, etc.)
   - Requires custom registration script or service
   - More flexible for heterogeneous environments
   - See "Manual Registration API" section above

3. **Default Backend (Development/Testing Only)**
   - Quick setup via environment variables
   - No backend server configuration needed
   - Use for development, testing, and debugging
   - See "Default Backend Server (For Debugging)" section above
