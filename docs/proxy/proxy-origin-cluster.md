# Origin Cluster

How to use the proxy server to build an origin cluster for SRS media server.

## Build

To build the proxy server, you need to have Go 1.18+ installed. Then, you can build the proxy 
server by below command, and get the executable binary `./srs-proxy`:

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
    PROXY_SYSTEM_API=12025 PROXY_LOAD_BALANCER_TYPE=memory ./srs-proxy
```

> Note: Here we use the memory load balancer, you can switch to `redis` if you want to run more
> than one proxy server.

Then, deploy three origin servers, which connects to the proxy server via port `12025`:

```bash
./objs/srs -c conf/origin1-for-proxy.conf
./objs/srs -c conf/origin2-for-proxy.conf
./objs/srs -c conf/origin3-for-proxy.conf
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
    PROXY_SYSTEM_API=12025 PROXY_LOAD_BALANCER_TYPE=memory ./srs-proxy
```

> Note: Here we use the memory load balancer, you can switch to `redis` if you want to run more
> than one proxy server.

Then, deploy three origin servers, which connects to the proxy server via port `12025`:

```bash
./objs/srs -c conf/origin1-for-proxy.conf
./objs/srs -c conf/origin2-for-proxy.conf
./objs/srs -c conf/origin3-for-proxy.conf
```

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
    PROXY_SYSTEM_API=12025 PROXY_LOAD_BALANCER_TYPE=memory ./srs-proxy
```

> Note: Here we use the memory load balancer, you can switch to `redis` if you want to run more
> than one proxy server.

Then, deploy three origin servers, which connects to the proxy server via port `12025`:

```bash
./objs/srs -c conf/origin1-for-proxy.conf
./objs/srs -c conf/origin2-for-proxy.conf
./objs/srs -c conf/origin3-for-proxy.conf
```

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
