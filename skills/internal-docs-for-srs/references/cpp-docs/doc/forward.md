---
title: Forward
sidebar_label: Forward 
hide_title: false
hide_table_of_contents: false
---

# Forward For Small Cluster

SRS is design for live server, the forward is a important feature, used to 
forward stream on server to other live servers.

Note: The information about edge, read [Edge](./edge.md),
the best solution for large cluster and huge concurrency.

Note: The edge is for both play and publish.

Note: Use edge first, except need to copy a stream to multiple servers in a time.

The forward is used for fault backup, the origin can forward a stream to multiple origin servers, 
the edge can use multiple origin server for backup.

For the usage of forward, read [Usage: Forward](./sample-forward.md)

## Keywords

The forward defined some roles:

* master: The master server which forward stream to slave server.
* slave: The slave server which accept stream from master.

Although the origin/edge can be master/slave, but it is too complex, it is strongly recomments that
the forward(master/slave) only for origin, never use edge to forward stream.

## Config

Please refer to the vhost `same.vhost.forward.srs.com` of `full.conf`:

```
vhost __defaultVhost__ {
    # forward stream to other servers.
    forward {
        # whether enable the forward.
        # default: off
        enabled on;
        # forward all publish stream to the specified server.
        # this used to split/forward the current stream for cluster active-standby,
        # active-active for cdn to build high available fault tolerance system.
        # format: {ip}:{port} {ip_N}:{port_N}
        destination 127.0.0.1:1936 127.0.0.1:1937;

        # when client(encoder) publish to vhost/app/stream, call the hook in creating backend forwarder.
        # the request in the POST data string is a object encode by json:
        #       {
        #           "action": "on_forward",
        #           "server_id": "vid-k21d7y2",
        #           "client_id": "9o7g1330",
        #           "ip": "127.0.0.1",
        #           "vhost": "__defaultVhost__",
        #           "app": "live",
        #           "tcUrl": "rtmp://127.0.0.1:1935/live",
        #           "stream": "livestream",
        #           "param": ""
        #       }
        # if valid, the hook must return HTTP code 200(Status OK) and response
        # an int value specifies the error code(0 corresponding to success):
        #       {
        #          "code": 0,
        #          "data": {
        #              "urls":[
        #                 "rtmp://127.0.0.1:19350/test/teststream"
        #              ]
        #          }
        #       }
        # PS: you can transform params to backend service, such as:
        #       { "param": "?forward=rtmp://127.0.0.1:19351/test/livestream" }
        #     then backend return forward's url in response.
        # if backend return empty urls, destanition is still disabled.
        # only support one api hook, format:
        #       backend http://xxx/api0
        backend http://127.0.0.1:8085/api/v1/forward;
    }
}
```

## Dynamic Forward

SRS support dynamic forwarding, to query the forwarding config from your backend API.

So you must write a backend server, which is an HTTP server, or web server. It accepts HTTP requests from SRS, and then 
responses the content with configs for SRS to do forward. It works like this:

```text
                        +------+
Client ---Push-RTMP-->--+ SRS  +---HTTP-Request---> Your Backend Server
                        |      |                        +
                        +      +--<---Forward-Config----+
                        |      |
                        +      +----Push-RTMP----> RTMP Server
                        +------+
```

First, config the `backend` of forward:

```
vhost __defaultVhost__ {
    forward {
        enabled on;
        backend http://127.0.0.1:8085/api/v1/forward;
    }
}
```

While client publishing to SRS, SRS will request your HTTP backend server, with request body:

```json
{
    "action": "on_forward",
    "server_id": "vid-k21d7y2",
    "client_id": "9o7g1330",
    "ip": "127.0.0.1",
    "vhost": "__defaultVhost__",
    "app": "live",
    "tcUrl": "rtmp://127.0.0.1:1935/live",
    "stream": "livestream",
    "param": ""
}
```

If your backend server responses with RTMP urls, SRS will start forwarding to the RTMP server:

```json
{
   "code": 0,
   "data": {
       "urls":[
          "rtmp://127.0.0.1:19350/test/teststream"
       ]
   }
}
```

> Note: If urls is empty array, SRS won't forward it.

For more details about dynamic forwarding, please read [#1342](https://github.com/ossrs/srs/issues/1342).

> Note: If both `backend` and `destination` are configured, SRS uses only the `backend`, even if it returns no urls.

## Keyframe Restart

Each forwarder is an RTMP client that publishes to its destination. When the destination is down, the forwarder
retries every 3 seconds, while the stream keeps coming from the encoder.

After each successful publish to the destination, the first one and every reconnect, the forwarder starts fresh from live:

* It drops the media queued before the publish succeeded, so the destination never gets a burst of stale media.
* It sends the metadata, then the video and audio sequence headers, once each.
* It drops video until the next keyframe, so the destination never starts in the middle of a GOP. Audio is never dropped.

The forwarder queue is bounded by `play.queue_length` of the vhost. When the destination is too slow and the queue
overflows, the forwarder drops the queued media and waits for the next keyframe the same way.

This is always on, with no config.

## Forward Status

SRS lists each forwarder of each publishing stream by the HTTP API `/api/v1/forwards/`, for both `destination`
and `backend` forwarders. It is read-only:

```bash
curl http://127.0.0.1:1985/api/v1/forwards/
```

```json
{
  "code": 0,
  "server": "vid-0w2w3z5",
  "service": "x0e5c1u5",
  "pid": "50234",
  "total": 1,
  "forwards": [
    {
      "id": "fwd-3k9x1zq",
      "stream": "vid-a4r1n2m",
      "vhost": "vid-7m2c0d1",
      "app": "live",
      "name": "livestream",
      "url": "rtmp://192.168.1.6:1935/live/livestream",
      "state": "forwarding",
      "state_at": 1760086530123
    }
  ]
}
```

The `total` is the number of forwards, after the `?stream=` filter if any. The list has at most `count` forwards
from `start` (default 0 and 10), so use `total` to page through them.

The fields of each forward:

* `id`: The id of the forward, for `/api/v1/forwards/{id}`. It stays the same across retries; each publish creates new forwarders with new ids.
* `stream`: The id of the publishing stream, the same as `/api/v1/streams/`. The `vhost`, `app` and `name` are those of the publishing stream.
* `url`: The RTMP url the forwarder publishes to, with the stream key and query. For `destination`, it is the destination with the app, stream and query of the publisher; for `backend`, it is the url the backend returned. SRS may append a `vhost` param, for example, `?vhost=localhost` when the publisher used `rtmp://localhost/live/livestream`, or the IP of a backend url without a `vhost` param.
* `state`: `connecting` until the publish to the destination succeeds, `forwarding` while sending, or `retrying` during the 3 seconds wait after an error.
* `state_at`: When the forward entered this state, in Unix milliseconds.

A forwarder exists only while its stream is publishing; it is removed when the stream unpublishes. To query the forwards
of one stream, use `?stream=<stream id>`; to query one forward, use its id:

```bash
curl http://127.0.0.1:1985/api/v1/forwards/?stream=vid-a4r1n2m
curl http://127.0.0.1:1985/api/v1/forwards/fwd-3k9x1zq
```

See [HTTP API: Forwards](./http-api.md#forwards) for details.

> Note: The url contains the stream key, so please protect the HTTP API by `http_api.auth`.

## Add or Remove a Forward

SRS creates the forwarders of a stream only when the stream is published, and deletes them when it unpublishes.
There is no API to add or remove a forward of a live stream, and `/api/v1/forwards/` is read-only: `DELETE`
returns HTTP 405. Removing a forwarder is the same as kicking its publisher, so SRS keeps only these primitives:

* For dynamic forward, your backend decides which forwards to create, on each publish.
* Kicking the publisher deletes every forwarder of the stream.

To stop a forward that is not created yet, your backend returns no url for it in the next `on_forward`.

To stop an active forward, your backend stops returning its url, then kicks the publisher. When the encoder
republishes, SRS asks your backend again, and creates only the forwards it returns. Adding a forward to a live
stream works the same way: return the new url, then kick the publisher.

To kick the publisher of a forward, find the stream id of the forward, then the client id of the publisher:

```bash
# 1. The stream id of the forward, the field `stream`.
curl http://127.0.0.1:1985/api/v1/forwards/
# 2. The client id of the publisher, the field `stream.publish.cid`.
curl http://127.0.0.1:1985/api/v1/streams/vid-a4r1n2m
# 3. Kick the publisher.
curl -X DELETE http://127.0.0.1:1985/api/v1/clients/4o3v9172
```

After the kick, the forwards of the stream are removed from `/api/v1/forwards/`. The encoder must republish by itself,
for example, by a loop in a script; the new forwarders get new ids.

> Note: For `destination`, kicking the publisher and republishing creates the same forwards again. To change the
> destinations, change the config and restart SRS.

## For Small Cluster

Forward can also used to build a small cluster:

```bash
                                   +-------------+    +---------------+
                               +-->+ Slave(1935) +->--+  Player(3000) +
                               |   +-------------+    +---------------+
                               |   +-------------+    +---------------+
                               |-->+ Slave(1936) +->--+  Player(3000) +
         publish       forward |   +-------------+    +---------------+
+-----------+    +--------+    |     192.168.1.6                       
|  Encoder  +-->-+ Master +-->-|                                       
+-----------+    +--------+    |   +-------------+    +---------------+
 192.168.1.3    192.168.1.5    +-->+ Slave(1935) +->--+  Player(3000) +
                               |   +-------------+    +---------------+
                               |   +-------------+    +---------------+
                               +-->+ Slave(1936) +->--+  Player(3000) +
                                   +-------------+    +---------------+
                                     192.168.1.7                          
```

The below sections is the example for this small cluster.

### Encoder

Use FFMPEG as encoder to publish stream to master:

```bash
for((;;)); do\
    ./objs/ffmpeg/bin/ffmpeg -re -i doc/source.flv \
        -c copy -f flv rtmp://192.168.1.5:1935/live/livestream; \
done
```

### SRS-Master Server

The SRS master server(192.168.1.5) config:

```bash
listen              1935;
pid                 ./objs/srs.pid;
max_connections     10240;
vhost __defaultVhost__ {
    forward {
        enabled on;
        destination 192.168.1.6:1935 192.168.1.6:1936 192.168.1.7:1935 192.168.1.7:1936;
    }
}
```

The RTMP play url on master is: `rtmp://192.168.1.5/live/livestream`

The master will forward stream to four slaves on two servers.

### SRS-Slave Server

The slave server can use different port to run on multiple cpu server.
The slave on the same server must use different port and pid file.

For example, the slave server 192.168.1.6, start two SRS servers, listen at 1935 and 1936.

The config file for port 1935 `srs.1935.conf`:

```bash
listen              1935;
pid                 ./objs/srs.1935.pid;
max_connections     10240;
vhost __defaultVhost__ {
}
```

The config file for port 1936 `srs.1936.conf`:

```bash
listen              1936;
pid                 ./objs/srs.1936.pid;
max_connections     10240;
vhost __defaultVhost__ {
}
```

Start these two SRS processes:

```bash
nohup ./objs/srs -c srs.1935.conf >/dev/null 2>&1 &
nohup ./objs/srs -c srs.1936.conf >/dev/null 2>&1 &
```

The player random access these streams:
* `rtmp://192.168.1.6:1935/live/livestream`
* `rtmp://192.168.1.6:1936/live/livestream`

The other slave server 192.168.1.7 is similar to 192.168.1.6

### Stream in Service

The stream in service:

| Url | Server | Port | Clients |
| ---- | ----- | ----- | ------- |
| rtmp://192.168.1.6:1935/live/livestream | 192.168.1.6 | 1935 | 3000 |
| rtmp://192.168.1.6:1936/live/livestream | 192.168.1.6 | 1936 | 3000 |
| rtmp://192.168.1.7:1935/live/livestream | 192.168.1.7 | 1935 | 3000 |
| rtmp://192.168.1.7:1936/live/livestream | 192.168.1.7 | 1936 | 3000 |

This architecture can support 12k clients. 
User can add more slave or start new ports.

## Forward VS Edge

The forward is not used in cdn, because CDN has thousands of servers, thousands of streams. 
The forward will always forward all stream to slave servers.

CDN or large cluster must use edge, never use forward.

## Other Use Scenarios

Forward used for transcoder, we can transcode a h.264+speex stream to a vhost, while this vhost forward
stream to slave. Then all stream on slave is h.264+aac, to delivery HLS.

Winlin 2014.11

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/forward)


