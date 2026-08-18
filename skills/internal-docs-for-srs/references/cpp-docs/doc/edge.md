---
title: Edge Cluster
sidebar_label: Edge Cluster
hide_title: false
hide_table_of_contents: false
---

# Edge Server

SRS edge dedicates to support huge players for a small set of streams.

![](/img/doc-main-concepts-edge-001.png)

Note: The edge server need to serve many clients, the SRS performance is ok.

Use Scenarios for Edge:
* CDN/VDN RTMP cluster, for many clients to upload(publish) or download(play).
* Small cluster, but many clients to publish. Forward is not ok for all stream is forwarded,
while edge is ok for it only fetch when user play the specified stream.
* The BGP server is costly, while the edge is cheap. Use multiple levels edge
to ensure the BGP server low bandwidth.

Note: Edge can fetch  stream from or push stream to origin. When user play
a stream on edge, edge will fetch from origin. When user publish stream to
edge, edge will push to origin.

Note: Always use Edge, except you actually know the forward. The forward will
always forward stream to multiple servers; while the edge only fetch or push
stream to a server and switch to next when error.

## Concepts

When a vhost set mode to remote, the vhost in server is edge.
When a vhost set mode to local, the vhost in server is origin.
Edge is used to cache the stream of origin.

When user publish stream to the edge server, edge will forward the stream 
to origin. For example, the origin server is in beijing, a user at shanghai needs
to pubish stream to origin server, we can add a edge server at shanghai, when 
user publish stream to shanghai edge server, the edge server will forward stream to 
beijing.

When user play the stream on edge, edge will fetch from origin when it has not 
cache it yet. When edge already cached the stream, edge will directly delivery
stream to client. That is, when many clients connect to edge, there is only one
connection to origin for each stream. This is the CDN(content delivery network).
For example, the origin server is at beijing, there are 320 edge servers on other 
provience, each edge server serves 2000 clients. There are 640,000 users play this 
stream, and the bandwidth of CDN consumed 640Gbps; the origin server only serves 320 
connections from all edge servers.

The edge server is design for huge cluster. Futhermore, the SRS edge can config with
multiple origin servers, SRS will switch to next when current origin server crash, and
the end user never disconnect when edge switch origin server.

## Config

Config the edge in vhost:

```bash
vhost __defaultVhost__ {
    # The config for cluster.
    cluster {
        # The cluster mode, local or remote.
        #       local: It's an origin server, serve streams itself.
        #       remote: It's an edge server, fetch or push stream to origin server.
        # default: local
        mode            remote;

        # For edge(mode remote), user must specifies the origin server
        # format as: <server_name|ip>[:port]
        # @remark user can specifies multiple origin for error backup, by space,
        # for example, 192.168.1.100:1935 192.168.1.101:1935 192.168.1.102:1935
        origin          127.0.0.1:1935 localhost:1935;

        # For edge(mode remote), whether open the token traverse mode,
        # if token traverse on, all connections of edge will forward to origin to check(auth),
        # it's very important for the edge to do the token auth.
        # the better way is use http callback to do the token auth by the edge,
        # but if user prefer origin check(auth), the token_traverse if better solution.
        # default: off
        token_traverse  off;

        # For edge(mode remote), the vhost to transform for edge,
        # to fetch from the specified vhost at origin,
        # if not specified, use the current vhost of edge in origin, the variable [vhost].
        # default: [vhost]
        vhost           same.edge.srs.com;

        # For edge(mode remote), when upnode(forward to, edge push to, edge pull from) is srs,
        # it's strongly recommend to open the debug_srs_upnode,
        # when connect to upnode, it will take the debug info,
        # for example, the id, source id, pid.
        # please see https://ossrs.io/lts/en-us/docs/v4/doc/log
        # default: on
        debug_srs_upnode    on;
    }
}
```

The origin can specifies multiple servers.

## Example

The example below specifies how to config a origin and edge.

The config of origin, see `origin.conf`：

```bash
listen              19350;
pid                 objs/origin.pid;
srs_log_file        ./objs/origin.log;
vhost __defaultVhost__ {
}
```

The config of edge, see `edge.conf`：

```bash
listen              1935;
pid                 objs/edge.pid;
srs_log_file        ./objs/edge.log;
vhost __defaultVhost__ {
    cluster {
        mode            remote;
        origin          127.0.0.1:19350;
    }
}
```

## HLS Edge

The edge is for RTMP, that is, when publish stream to origin, only origin server output
the HLS, all edge server never output HLS util client access the RTMP stream on edge.

That is, never config HLS on edge server, it's no use. The HLS delivery must use squid or 
traffic server to cache the HTTP origin server.

## WebRTC Edge

Currently edge cluster only support RTMP and HTTP-FLV, doesn't support WebRTC. A new version 
of Edge Cluster is planning, see [#4402](https://github.com/ossrs/srs/discussions/4402) for details.

Note that you can use new version of [Origin Cluster](./origin-cluster.md) for WebRTC, as it 
can also used to expand the capacity of streams. However, if you need to expand the capacity 
of viewers for one stream, you still need the new version of Edge Cluster.

## Transform Vhost

The design of CDN stream system, always use `up.xxxx` and `down.xxxx` to operate them, for example, user publish to cdn by host `up.srs.com` and play by `down.srs.com`.

SRS can config the edge mode to transform the host to origin, use the config `vhost down.srs.com` for up edge server.

For more information, read the config of edge server.

Winlin 2015.4

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/edge)


