---
title: HTTP Callback
sidebar_label: HTTP Callback
hide_title: false
hide_table_of_contents: false
---

# HTTPCallback

SRS supports HTTP callback to extends SRS. The workflow is:

```text
+--------+     +--------+                    +-----------------------+
| FFmpeg |-->--+  SRS   |--HTTP-Callback-->--+  Your Business Server |
+--------+     +--------+                    +-----------------------+
```

When FFmpeg/OBS publish or play a stream to SRS, SRS will call your business server to notify the event.

## Usage

First, run SRS with HTTP callback enabled:

```bash
./objs/srs -c conf/http.hooks.callback.conf
```

Start the demo HTTP callback server, which is your business server:

```bash
go run research/api-server/server.go
```

Publish a stream to SRS, with the params:

```bash
ffmpeg -re -i doc/source.flv -c copy -f flv rtmp://localhost/live/livestream?k=v
```

Your business server will got the HTTP event:

```text
Got action=on_publish, client_id=3y1tcaw2, ip=127.0.0.1, vhost=__defaultVhost__, stream=livestream, param=?k=v
```

Note that the `k=v` can be used for authentication, for token authentication based on HTTP callbacks,
read [Token Authentication](./drm.md#token-authentication)

## Compile

SRS always enable http callbacks.

For more information, read [Build](./install.md)

## Configuring SRS

An example [conf/http.hooks.callback.conf](https://github.com/ossrs/srs/blob/develop/trunk/conf/http.hooks.callback.conf) 
is available, demonstrating the configuration of common callback events for direct use.

The config for HTTP hooks is:

```bash
vhost your_vhost {
    http_hooks {
        # whether the http hooks enable.
        # default off.
        enabled         on;
        # when client(encoder) publish to vhost/app/stream, call the hook,
        # the request in the POST data string is a object encode by json:
        #       {
        #           "action": "on_publish",
        #           "client_id": "9308h583",
        #           "ip": "192.168.1.10", "vhost": "video.test.com", "app": "live",
        #           "stream": "livestream", "param":"?token=xxx&salt=yyy", "server_id": "vid-werty",
        #           "stream_url": "video.test.com/live/livestream", "stream_id": "vid-124q9y3"
        #       }
        # if valid, the hook must return HTTP code 200(Status OK) and response
        # an int value specifies the error code(0 corresponding to success):
        #       0
        # support multiple api hooks, format:
        #       on_publish http://xxx/api0 http://xxx/api1 http://xxx/apiN
        # @remark For SRS4, the HTTPS url is supported, for example:
        #       on_publish https://xxx/api0 https://xxx/api1 https://xxx/apiN
        on_publish      http://127.0.0.1:8085/api/v1/streams http://localhost:8085/api/v1/streams;
        # when client(encoder) stop publish to vhost/app/stream, call the hook,
        # the request in the POST data string is a object encode by json:
        #       {
        #           "action": "on_unpublish",
        #           "client_id": "9308h583",
        #           "ip": "192.168.1.10", "vhost": "video.test.com", "app": "live",
        #           "stream": "livestream", "param":"?token=xxx&salt=yyy", "server_id": "vid-werty",
        #           "stream_url": "video.test.com/live/livestream", "stream_id": "vid-124q9y3"
        #       }
        # if valid, the hook must return HTTP code 200(Status OK) and response
        # an int value specifies the error code(0 corresponding to success):
        #       0
        # support multiple api hooks, format:
        #       on_unpublish http://xxx/api0 http://xxx/api1 http://xxx/apiN
        # @remark For SRS4, the HTTPS url is supported, for example:
        #       on_unpublish https://xxx/api0 https://xxx/api1 https://xxx/apiN
        on_unpublish    http://127.0.0.1:8085/api/v1/streams http://localhost:8085/api/v1/streams;
        # when client start to play vhost/app/stream, call the hook,
        # the request in the POST data string is a object encode by json:
        #       {
        #           "action": "on_play",
        #           "client_id": "9308h583",
        #           "ip": "192.168.1.10", "vhost": "video.test.com", "app": "live",
        #           "stream": "livestream", "param":"?token=xxx&salt=yyy",
        #           "pageUrl": "http://www.test.com/live.html", "server_id": "vid-werty",
        #           "stream_url": "video.test.com/live/livestream", "stream_id": "vid-124q9y3"
        #       }
        # if valid, the hook must return HTTP code 200(Status OK) and response
        # an int value specifies the error code(0 corresponding to success):
        #       0
        # support multiple api hooks, format:
        #       on_play http://xxx/api0 http://xxx/api1 http://xxx/apiN
        # @remark For SRS4, the HTTPS url is supported, for example:
        #       on_play https://xxx/api0 https://xxx/api1 https://xxx/apiN
        on_play         http://127.0.0.1:8085/api/v1/sessions http://localhost:8085/api/v1/sessions;
        # when client stop to play vhost/app/stream, call the hook,
        # the request in the POST data string is a object encode by json:
        #       {
        #           "action": "on_stop",
        #           "client_id": "9308h583",
        #           "ip": "192.168.1.10", "vhost": "video.test.com", "app": "live",
        #           "stream": "livestream", "param":"?token=xxx&salt=yyy", "server_id": "vid-werty",
        #           "stream_url": "video.test.com/live/livestream", "stream_id": "vid-124q9y3"
        #       }
        # if valid, the hook must return HTTP code 200(Status OK) and response
        # an int value specifies the error code(0 corresponding to success):
        #       0
        # support multiple api hooks, format:
        #       on_stop http://xxx/api0 http://xxx/api1 http://xxx/apiN
        # @remark For SRS4, the HTTPS url is supported, for example:
        #       on_stop https://xxx/api0 https://xxx/api1 https://xxx/apiN
        on_stop         http://127.0.0.1:8085/api/v1/sessions http://localhost:8085/api/v1/sessions;
        # when srs reap a dvr file, call the hook,
        # the request in the POST data string is a object encode by json:
        #       {
        #           "action": "on_dvr",
        #           "client_id": "9308h583",
        #           "ip": "192.168.1.10", "vhost": "video.test.com", "app": "live",
        #           "stream": "livestream", "param":"?token=xxx&salt=yyy",
        #           "cwd": "/usr/local/srs",
        #           "file": "./objs/nginx/html/live/livestream.1420254068776.flv", "server_id": "vid-werty",
        #           "stream_url": "video.test.com/live/livestream", "stream_id": "vid-124q9y3"
        #       }
        # if valid, the hook must return HTTP code 200(Status OK) and response
        # an int value specifies the error code(0 corresponding to success):
        #       0
        on_dvr          http://127.0.0.1:8085/api/v1/dvrs http://localhost:8085/api/v1/dvrs;
        # when srs reap a ts file of hls, call the hook,
        # the request in the POST data string is a object encode by json:
        #       {
        #           "action": "on_hls",
        #           "client_id": "9308h583",
        #           "ip": "192.168.1.10", "vhost": "video.test.com", "app": "live",
        #           "stream": "livestream", "param":"?token=xxx&salt=yyy",
        #           "duration": 9.36, // in seconds
        #           "cwd": "/usr/local/srs",
        #           "file": "./objs/nginx/html/live/livestream/2015-04-23/01/476584165.ts",
        #           "url": "live/livestream/2015-04-23/01/476584165.ts",
        #           "m3u8": "./objs/nginx/html/live/livestream/live.m3u8",
        #           "m3u8_url": "live/livestream/live.m3u8",
        #           "seq_no": 100, "server_id": "vid-werty",
        #           "stream_url": "video.test.com/live/livestream", "stream_id": "vid-124q9y3"
        #       }
        # if valid, the hook must return HTTP code 200(Status OK) and response
        # an int value specifies the error code(0 corresponding to success):
        #       0
        on_hls          http://127.0.0.1:8085/api/v1/hls http://localhost:8085/api/v1/hls;
        # when srs reap a ts file of hls, call this hook,
        # used to push file to cdn network, by get the ts file from cdn network.
        # so we use HTTP GET and use the variable following:
        #       [server_id], replace with the server_id
        #       [app], replace with the app.
        #       [stream], replace with the stream.
        #       [param], replace with the param.
        #       [ts_url], replace with the ts url.
        # ignore any return data of server.
        # @remark random select a url to report, not report all.
        on_hls_notify   http://127.0.0.1:8085/api/v1/hls/[server_id]/[app]/[stream]/[ts_url][param];
    }
}
```

Description about some fields:

* `stream_url`: The stream identify without extension, such as `/live/livestream`.
* `stream_id`: The id of stream, by which you can query the stream information.

> Note: The callbacks for streaming are `on_publish` and `on_unpublish`, while the callbacks for playback are `on_play` and `on_stop`.

> Note: Before SRS 4, there were `on_connect` and `on_close`, which are events defined by RTMP and only applicable to RTMP streams. These events overlap with streaming and playback events, so their use is not recommended.

> Note: You can refer to the hooks.callback.vhost.com example in the conf/full.conf configuration file.

## Protocol

The detail protocol, for example, `on_publish`:

```text
POST /api/v1/streams HTTP/1.1
Content-Type: application-json

Body:
{
  "server_id": "vid-0xk989d",
  "action": "on_publish",
  "client_id": "341w361a",
  "ip": "127.0.0.1",
  "vhost": "__defaultVhost__",
  "app": "live",
  "tcUrl": "rtmp://127.0.0.1:1935/live?vhost=__defaultVhost__",
  "stream": "livestream",
  "param": "",
  "stream_url": "video.test.com/live/livestream",
  "stream_id": "vid-124q9y3"
}
```

> Note: You can use wireshark or tcpdump to verify it.

## Heartbeat

SRS will send heartbeat to the HTTP callback server. This allows you to monitor the health of SRS server.
Enable this feature by:

```bash
# heartbeat to api server
# @remark, the ip report to server, is retrieve from system stat,
#       which need the config item stats.network.
heartbeat {
    # whether heartbeat is enabled.
    # Overwrite by env SRS_HEARTBEAT_ENABLED
    # default: off
    enabled off;
    # the interval seconds for heartbeat,
    # recommend 0.3,0.6,0.9,1.2,1.5,1.8,2.1,2.4,2.7,3,...,6,9,12,....
    # Overwrite by env SRS_HEARTBEAT_INTERVAL
    # default: 9.9
    interval 9.3;
    # when startup, srs will heartbeat to this api.
    # @remark: must be a restful http api url, where SRS will POST with following data:
    #   {
    #       "device_id": "my-srs-device",
    #       "ip": "192.168.1.100"
    #   }
    # Overwrite by env SRS_HEARTBEAT_URL
    # default: http://127.0.0.1:8085/api/v1/servers
    url http://127.0.0.1:8085/api/v1/servers;
    # the id of device.
    # Overwrite by env SRS_HEARTBEAT_DEVICE_ID
    device_id       "my-srs-device";
    # whether report with summaries
    # if on, put /api/v1/summaries to the request data:
    #   {
    #       "summaries": summaries object.
    #   }
    # @remark: optional config.
    # Overwrite by env SRS_HEARTBEAT_SUMMARIES
    # default: off
    summaries off;
}
```

By enable the `summaries`, you can get the SRS server states, such as `self.pid` and `self.srs_uptime`, so you
can use this to determine whether SRS restarted.

> Note: About fileds of `summaries`, see [HTTP API: summaries](./http-api.md#summaries) for details.

## Go Example

Write Go code to handle SRS callback, for example, handling `on_publish`:

```go
http.HandleFunc("/api/v1/streams", func(w http.ResponseWriter, r *http.Request) {
    b, err := ioutil.ReadAll(r.Body)
    if err != nil {
        http.Error(w, err.Error(), http.StatusInternalServerError)
    }

    fmt.Println(string(b))

    res, err := json.Marshal(struct {
        Code int `json:"code"`
        Message string `json:"msg"`
    }{
        0, "OK",
    })
    if err != nil {
        http.Error(w, err.Error(), http.StatusInternalServerError)
    }
    w.Write(res)
})

_ = http.ListenAndServe(":8085", nil)
```

## Nodejs Koa Example

Write Nodejs/Koa code to handle SRS callback, for example, handling `on_publish`:

```js
const Router = require('koa-router');
const router = new Router();

router.all('/api/v1/streams', async (ctx) => {
  console.log(ctx.request.body);

  ctx.body = {code: 0, msg: 'OK'};
});
```

## PHP Example

Write PHP code to handle SRS callback, for example, handling `on_publish`:

```php
$body = json_decode(file_get_contents('php://input'));
printf($body);

echo json_encode(array("code"=>0, "msg"=>"OK"));
```

## HTTP Callback Events

SRS can call HTTP callbacks for events:

* `on_publish`: When a client publishes a stream, for example, using flash or FMLE to publish a stream to the server. 
* `on_unpublish`: When a client stops publishing a stream. 
* `on_play`: When a client starts playing a stream. 
* `on_stop`: When a client stops playback. 
* `on_dvr`: When reap a DVR file.
* `on_hls`: When reap a HLS file.

For events `on_publish`和`on_play`:
* Return Code: SRS requires that the response is an int indicating the error, 0 is success.

Notes:
* Event: When this event occurs, call back to the specified HTTP URL.
* HTTP URL: Can be multiple URLs, split by spaces, SRS will notify all one by one.
* Data: SRS will POST the data to specified HTTP API.

SRS will disconnect the connection when the response is not 0, or HTTP status is not 200.

## SRS HTTP Callback Server

SRS provides a default HTTP callback server, using golang native http framework.

To start it: 

```bash
cd research/api-server && go run server.go 8085
```

```bash
#2023/01/18 22:57:40.835254 server.go:572: api server listen at port:8085, static_dir:/Users/panda/srs/trunk/static-dir
#2023/01/18 22:57:40.835600 server.go:836: start listen on::8085
```

> Remark: For SRS4, the HTTP/HTTPS url is supported, see [#1657](https://github.com/ossrs/srs/issues/1657#issuecomment-720889906).

## HTTPS Callback

HTTPS Callback is supported by SRS4, only change the callback URL from `http://` to `https://`, for example:

```
vhost your_vhost {
    http_hooks {
        enabled         on;
        on_publish      https://127.0.0.1:8085/api/v1/streams;
        on_unpublish    https://127.0.0.1:8085/api/v1/streams;
        on_play         https://127.0.0.1:8085/api/v1/sessions;
        on_stop         https://127.0.0.1:8085/api/v1/sessions;
        on_dvr          https://127.0.0.1:8085/api/v1/dvrs;
        on_hls          https://127.0.0.1:8085/api/v1/hls;
        on_hls_notify   https://127.0.0.1:8085/api/v1/hls/[app]/[stream]/[ts_url][param];
    }
}
```

## Response

If success, you must response `something` to identify the success, or SRS will reject the client, which enable you to reject the illegal client, please read [Callback Error Code](./http-api.md#error-code). 

> Note: The `on_publish` callback also could be used as advanced security, to `allow` or `deny` a client by its IP, or token in request url, or any other information of client.

Where `something` means:

* HTTP/200, which is HTTP success.
* `AND` response and int value 0, or JSON object with field code 0.

Like this:

```
HTTP/1.1 200 OK
Content-Length: 1
0
```

OR:

```
HTTP/1.1 200 OK
Content-Length: 11
{"code": 0}
```

You could run the example HTTP callback server by:

```
cd srs/trunk/research/api-server && go run server.go 8085
```

And you will finger out what's the `right` response.

## Snapshot

The HttpCallback can used to snapshot, please read [snapshot](./snapshot.md#httpcallback)

Winlin 2015.1

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/doc/en/v7/http-callback)


