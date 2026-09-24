# Origin Cluster

How to use the proxy server to build an origin cluster for SRS media server.

## Build

To build the proxy server, you need to have Go 1.18+ installed. Then, you can build the proxy 
server by below command, and get the executable binary `bin/srs-proxy`:

```bash
cd ~/git &&
git clone https://github.com/ossrs/srs.git &&
cd proxy && make
```

> Note: You can also download the dependencies by running `go mod download` before building.

We will support the Docker image in the future, or integrate the proxy server into the Oryx 
project.

Clone and build SRS, which is the default backend origin server:

```bash
cd ~/git &&
git clone https://github.com/ossrs/srs.git &&
cd srs/trunk && ./configure && make
```

SRS will automatically register itself to the proxy server, see `Automatic Registration` in [proxy-protocol.md](./proxy-protocol.md).

You can use any other RTMP server as the backend origin server, but you need to register the backend server manually, see `Manual Registration API` in [proxy-protocol.md](./proxy-protocol.md).

## Legacy

From SRS 7.0+, the new Origin Cluster is based on proxy server, not the old MESH based SRS servers.
However, if you want to use the old origin cluster, you can switch to SRS 6.0.

## RTMP Origin Cluster

To use the RTMP origin cluster, you need to deploy the proxy server and the origin server. 
First, start the proxy server:

```bash
env PROXY_RTMP_SERVER=1935 PROXY_HTTP_SERVER=8080 \
    PROXY_HTTP_API=1985 PROXY_WEBRTC_SERVER=8000 PROXY_SRT_SERVER=10080 \
    PROXY_SYSTEM_API=12025 PROXY_LOAD_BALANCER_TYPE=memory ./bin/srs-proxy
```

> Note: Here we use the memory load balancer, you can switch to `redis` if you want to run more
> than one proxy server.

Then, deploy three origin servers, each in its own terminal, which connects to the proxy server via port `12025`:

```bash
env SRS_RTMP_LISTEN=19351 SRS_HTTP_API_ENABLED=on SRS_HTTP_API_LISTEN=19851 \
    SRS_HTTP_SERVER_ENABLED=on SRS_HTTP_SERVER_LISTEN=8081 \
    SRS_RTC_SERVER_ENABLED=on SRS_RTC_SERVER_LISTEN=8001 \
    SRS_SRT_SERVER_ENABLED=on SRS_SRT_SERVER_LISTEN=10081 \
    SRS_SRT_SERVER_TSBPDMODE=off SRS_SRT_SERVER_TLPKTDROP=off \
    SRS_HEARTBEAT_ENABLED=on SRS_HEARTBEAT_INTERVAL=9 SRS_HEARTBEAT_DEVICE_ID=origin1 \
    SRS_HEARTBEAT_URL=http://127.0.0.1:12025/api/v1/srs/register SRS_HEARTBEAT_PORTS=on \
    SRS_VHOST_HTTP_REMUX_ENABLED=on SRS_VHOST_HLS_ENABLED=on \
    SRS_VHOST_RTC_ENABLED=on SRS_VHOST_RTC_RTMP_TO_RTC=on SRS_VHOST_RTC_RTC_TO_RTMP=on \
    SRS_VHOST_SRT_ENABLED=on SRS_VHOST_SRT_SRT_TO_RTMP=on \
    ./objs/srs -e
```

```bash
env SRS_RTMP_LISTEN=19352 SRS_HTTP_API_ENABLED=on SRS_HTTP_API_LISTEN=19853 \
    SRS_HTTP_SERVER_ENABLED=on SRS_HTTP_SERVER_LISTEN=8082 \
    SRS_RTC_SERVER_ENABLED=on SRS_RTC_SERVER_LISTEN=8002 \
    SRS_SRT_SERVER_ENABLED=on SRS_SRT_SERVER_LISTEN=10082 \
    SRS_SRT_SERVER_TSBPDMODE=off SRS_SRT_SERVER_TLPKTDROP=off \
    SRS_HEARTBEAT_ENABLED=on SRS_HEARTBEAT_INTERVAL=9 SRS_HEARTBEAT_DEVICE_ID=origin2 \
    SRS_HEARTBEAT_URL=http://127.0.0.1:12025/api/v1/srs/register SRS_HEARTBEAT_PORTS=on \
    SRS_VHOST_HTTP_REMUX_ENABLED=on SRS_VHOST_HLS_ENABLED=on \
    SRS_VHOST_RTC_ENABLED=on SRS_VHOST_RTC_RTMP_TO_RTC=on SRS_VHOST_RTC_RTC_TO_RTMP=on \
    SRS_VHOST_SRT_ENABLED=on SRS_VHOST_SRT_SRT_TO_RTMP=on \
    ./objs/srs -e
```

```bash
env SRS_RTMP_LISTEN=19353 SRS_HTTP_API_ENABLED=on SRS_HTTP_API_LISTEN=19852 \
    SRS_HTTP_SERVER_ENABLED=on SRS_HTTP_SERVER_LISTEN=8083 \
    SRS_RTC_SERVER_ENABLED=on SRS_RTC_SERVER_LISTEN=8003 \
    SRS_SRT_SERVER_ENABLED=on SRS_SRT_SERVER_LISTEN=10083 \
    SRS_SRT_SERVER_TSBPDMODE=off SRS_SRT_SERVER_TLPKTDROP=off \
    SRS_HEARTBEAT_ENABLED=on SRS_HEARTBEAT_INTERVAL=9 SRS_HEARTBEAT_DEVICE_ID=origin3 \
    SRS_HEARTBEAT_URL=http://127.0.0.1:12025/api/v1/srs/register SRS_HEARTBEAT_PORTS=on \
    SRS_VHOST_HTTP_REMUX_ENABLED=on SRS_VHOST_HLS_ENABLED=on \
    SRS_VHOST_RTC_ENABLED=on SRS_VHOST_RTC_RTMP_TO_RTC=on SRS_VHOST_RTC_RTC_TO_RTMP=on \
    SRS_VHOST_SRT_ENABLED=on SRS_VHOST_SRT_SRT_TO_RTMP=on \
    ./objs/srs -e
```

> Note: The origin servers are independent, so it's recommended to deploy them as Deployments 
> in Kubernetes (K8s).

Now, you're able to publish RTMP stream to the proxy server:

```bash
ffmpeg -re -i doc/source.flv -c copy -f flv rtmp://localhost/live/livestream
```

And play the RTMP stream from the proxy server:

```bash
ffplay rtmp://localhost/live/livestream
```

Or play HTTP-FLV stream from the proxy server:

```bash
ffplay http://localhost:8080/live/livestream.flv
```

Or play HLS stream from the proxy server:

```bash
ffplay http://localhost:8080/live/livestream.m3u8
``` 

Or play the WebRTC stream via [WHEP player](http://localhost:8080/players/whep.html) from proxy server.

You can also use VLC or other players to play the stream in proxy server.

## WebRTC Origin Cluster

To use the WebRTC origin cluster, you need to deploy the proxy server and the origin server.
First, start the proxy server:

```bash
env PROXY_RTMP_SERVER=1935 PROXY_HTTP_SERVER=8080 \
    PROXY_HTTP_API=1985 PROXY_WEBRTC_SERVER=8000 PROXY_SRT_SERVER=10080 \
    PROXY_SYSTEM_API=12025 PROXY_LOAD_BALANCER_TYPE=memory ./bin/srs-proxy
```

> Note: Here we use the memory load balancer, you can switch to `redis` if you want to run more
> than one proxy server.

Then, deploy the three origin servers of [RTMP Origin Cluster](#rtmp-origin-cluster), which connects to the proxy server via port `12025`.

> Note: The origin servers are independent, so it's recommended to deploy them as Deployments
> in Kubernetes (K8s).

Now, you're able to publish WebRTC stream via [WHIP publisher](http://localhost:8080/players/whip.html) to the proxy server.

And play the WebRTC stream via [WHEP player](http://localhost:8080/players/whep.html) from proxy server.

Or play the RTMP stream from the proxy server:

```bash
ffplay rtmp://localhost/live/livestream
```

Or play HTTP-FLV stream from the proxy server:

```bash
ffplay http://localhost:8080/live/livestream.flv
```

Or play HLS stream from the proxy server:

```bash
ffplay http://localhost:8080/live/livestream.m3u8
```

You can also use VLC or other players to play the stream in proxy server.

## SRT Origin Cluster

To use the SRT origin cluster, you need to deploy the proxy server and the origin server.
First, start the proxy server:

```bash
env PROXY_RTMP_SERVER=1935 PROXY_HTTP_SERVER=8080 \
    PROXY_HTTP_API=1985 PROXY_WEBRTC_SERVER=8000 PROXY_SRT_SERVER=10080 \
    PROXY_SYSTEM_API=12025 PROXY_LOAD_BALANCER_TYPE=memory ./bin/srs-proxy
```

> Note: Here we use the memory load balancer, you can switch to `redis` if you want to run more
> than one proxy server.

Then, deploy the three origin servers of [RTMP Origin Cluster](#rtmp-origin-cluster), which connects to the proxy server via port `12025`.

> Note: The origin servers are independent, so it's recommended to deploy them as Deployments
> in Kubernetes (K8s).

Now, you're able to publish SRT stream to the proxy server:

```bash
ffmpeg -re -i ./doc/source.flv -c copy -pes_payload_size 0 -f mpegts \
  'srt://127.0.0.1:10080?streamid=#!::r=live/livestream,m=publish'
```

And play the SRT stream from the proxy server:

```bash
ffplay 'srt://127.0.0.1:10080?streamid=#!::r=live/livestream,m=request'
```

Or play the RTMP stream from the proxy server:

```bash
ffplay rtmp://localhost/live/livestream
```

Or play HTTP-FLV stream from the proxy server:

```bash
ffplay http://localhost:8080/live/livestream.flv
```

Or play HLS stream from the proxy server:

```bash
ffplay http://localhost:8080/live/livestream.m3u8
``` 

Or play the WebRTC stream via [WHEP player](http://localhost:8080/players/whep.html) from proxy server.

You can also use VLC or other players to play the stream in proxy server.
