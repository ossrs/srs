/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#include <srs_utest_config.hpp>

using namespace std;

#include <srs_app_config.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_source.hpp>

// full.conf
std::string __full_conf = ""
    "# all config for srs                                                                                                                   \n"
    "                                                                                                                                       \n"
    "#############################################################################################                                          \n"
    "# RTMP sections                                                                                                                        \n"
    "#############################################################################################                                          \n"
    "# the rtmp listen ports, split by space.                                                                                               \n"
    "listen              1935;                                                                                                              \n"
    "# the pid file                                                                                                                         \n"
    "# to ensure only one process can use a pid file                                                                                        \n"
    "# and provides the current running process id, for script,                                                                             \n"
    "# for example, init.d script to manage the server.                                                                                     \n"
    "# default: ./objs/srs.pid                                                                                                              \n"
    "pid                 ./objs/srs.pid;                                                                                                    \n"
    "# the default chunk size is 128, max is 65536,                                                                                         \n"
    "# some client does not support chunk size change,                                                                                      \n"
    "# however, most clients supports it and it can improve                                                                                 \n"
    "# performance about 10%.                                                                                                               \n"
    "# default: 60000                                                                                                                       \n"
    "chunk_size          60000;                                                                                                             \n"
    "# the logs dir.                                                                                                                        \n"
    "# if enabled ffmpeg, each stracoding stream will create a log file.                                                                    \n"
    "# /dev/null to disable the log.                                                                                                        \n"
    "# default: ./objs                                                                                                                      \n"
    "ff_log_dir          ./objs;                                                                                                            \n"
    "# the log tank, console or file.                                                                                                       \n"
    "# if console, print log to console.                                                                                                    \n"
    "# if file, write log to file. requires srs_log_file if log to file.                                                                    \n"
    "# default: file.                                                                                                                       \n"
    "srs_log_tank        file;                                                                                                              \n"
    "# the log level, for all log tanks.                                                                                                    \n"
    "# can be: verbose, info, trace, warn, error                                                                                            \n"
    "# default: trace                                                                                                                       \n"
    "srs_log_level       trace;                                                                                                             \n"
    "# when srs_log_tank is file, specifies the log file.                                                                                   \n"
    "# default: ./objs/srs.log                                                                                                              \n"
    "srs_log_file        ./objs/srs.log;                                                                                                    \n"
    "# the max connections.                                                                                                                 \n"
    "# if exceed the max connections, server will drop the new connection.                                                                  \n"
    "# default: 12345                                                                                                                       \n"
    "max_connections     1000;                                                                                                              \n"
    "# whether start as deamon                                                                                                              \n"
    "# @remark: donot support reload.                                                                                                       \n"
    "# default: on                                                                                                                          \n"
    "daemon              on;                                                                                                                \n"
    "# heartbeat to api server                                                                                                              \n"
    "heartbeat {                                                                                                                            \n"
    "    # whether heartbeat is enalbed.                                                                                                    \n"
    "    # default: off                                                                                                                     \n"
    "    enabled         on;                                                                                                               \n"
    "    # the interval seconds for heartbeat,                                                                                              \n"
    "    # recommend 0.3,0.6,0.9,1.2,1.5,1.8,2.1,2.4,2.7,3,...,6,9,12,....                                                                  \n"
    "    # default: 9.9                                                                                                                     \n"
    "    interval        9.3;                                                                                                               \n"
    "    # when startup, srs will heartbeat to this api.                                                                                    \n"
    "    # @remark: must be a restful http api url, where SRS will POST with following data:                                                \n"
    "    #   {                                                                                                                              \n"
    "    #       \"device_id\": \"my-srs-device\",                                                                                          \n"
    "    #       \"ip\": \"192.168.1.100\"                                                                                                  \n"
    "    #   }                                                                                                                              \n"
    "    # default: http://127.0.0.1:8085/api/v1/servers                                                                                    \n"
    "    url             http://127.0.0.1:8085/api/v1/servers;                                                                              \n"
    "    # the id of devide.                                                                                                                \n"
    "    device_id       \"my-srs-device\";                                                                                                 \n"
    "    # the index of device ip.                                                                                                          \n"
    "    # we may retrieve more than one network device.                                                                                    \n"
    "    # default: 0                                                                                                                       \n"
    "    device_index    0;                                                                                                                 \n"
    "    # whether report with summaries                                                                                                    \n"
    "    # if true, put /api/v1/summaries to the request data:                                                                              \n"
    "    #   {                                                                                                                              \n"
    "    #       \"summaries\": summaries object.                                                                                           \n"
    "    #   }                                                                                                                              \n"
    "    # @remark: optional config.                                                                                                        \n"
    "    # default: off                                                                                                                     \n"
    "    summaries       off;                                                                                                               \n"
    "}                                                                                                                                      \n"
    "                                                                                                                                       \n"
    "#############################################################################################                                          \n"
    "# HTTP sections                                                                                                                        \n"
    "#############################################################################################                                          \n"
    "# api of srs.                                                                                                                          \n"
    "# the http api config, export for external program to manage srs.                                                                      \n"
    "# user can access http api of srs in browser directly, for instance, to access by:                                                     \n"
    "#       curl http://192.168.1.170:1985/api/v1/reload                                                                                   \n"
    "# which will reload srs, like cmd killall -1 srs, but the js can also invoke the http api,                                             \n"
    "# where the cli can only be used in shell/terminate.                                                                                   \n"
    "http_api {                                                                                                                             \n"
    "    # whether http api is enabled.                                                                                                     \n"
    "    # default: off                                                                                                                     \n"
    "    enabled         on;                                                                                                                \n"
    "    # the http api port                                                                                                                \n"
    "    # default: 1985                                                                                                                    \n"
    "    listen          1985;                                                                                                              \n"
    "}                                                                                                                                      \n"
    "# embeded http server in srs.                                                                                                          \n"
    "# the http streaming config, for HLS/HDS/DASH/HTTPProgressive                                                                          \n"
    "# global config for http streaming, user must config the http section for each vhost.                                                  \n"
    "# the embed http server used to substitute nginx in ./objs/nginx,                                                                      \n"
    "# for example, srs runing in arm, can provides RTMP and HTTP service, only with srs installed.                                         \n"
    "# user can access the http server pages, generally:                                                                                    \n"
    "#       curl http://192.168.1.170:80/srs.html                                                                                          \n"
    "# which will show srs version and welcome to srs.                                                                                      \n"
    "# @remark, the http embeded stream need to config the vhost, for instance, the __defaultVhost__                                        \n"
    "# need to open the feature http of vhost.                                                                                              \n"
    "http_stream {                                                                                                                          \n"
    "    # whether http streaming service is enabled.                                                                                       \n"
    "    # default: off                                                                                                                     \n"
    "    enabled         on;                                                                                                                \n"
    "    # the http streaming port                                                                                                          \n"
    "    # @remark, if use lower port, for instance 80, user must start srs by root.                                                        \n"
    "    # default: 8080                                                                                                                    \n"
    "    listen          8080;                                                                                                              \n"
    "    # the default dir for http root.                                                                                                   \n"
    "    # default: ./objs/nginx/html                                                                                                       \n"
    "    dir             ./objs/nginx/html;                                                                                                 \n"
    "}                                                                                                                                      \n"
    "                                                                                                                                       \n"
    "#############################################################################################                                          \n"
    "# RTMP/HTTP VHOST sections                                                                                                             \n"
    "#############################################################################################                                          \n"
    "# vhost list, the __defaultVhost__ is the default vhost                                                                                \n"
    "# for example, user use ip to access the stream: rtmp://192.168.1.2/live/livestream.                                                   \n"
    "# for which cannot identify the required vhost.                                                                                        \n"
    "vhost __defaultVhost__ {                                                                                                               \n"
    "}                                                                                                                                      \n"
    "                                                                                                                                       \n"
    "# vhost for edge, edge and origin is the same vhost                                                                                    \n"
    "vhost same.edge.srs.com {                                                                                                              \n"
    "    # the mode of vhost, local or remote.                                                                                              \n"
    "    #       local: vhost is origin vhost, which provides stream source.                                                                \n"
    "    #       remote: vhost is edge vhost, which pull/push to origin.                                                                    \n"
    "    # default: local                                                                                                                   \n"
    "    mode            remote;                                                                                                            \n"
    "    # for edge(remote mode), user must specifies the origin server                                                                     \n"
    "    # format as: <server_name|ip>[:port]                                                                                               \n"
    "    # @remark user can specifies multiple origin for error backup, by space,                                                           \n"
    "    # for example, 192.168.1.100:1935 192.168.1.101:1935 192.168.1.102:1935                                                            \n"
    "    origin          127.0.0.1:1935 localhost:1935;                                                                                     \n"
    "    # for edge, whether open the token traverse mode,                                                                                  \n"
    "    # if token traverse on, all connections of edge will forward to origin to check(auth),                                             \n"
    "    # it's very important for the edge to do the token auth.                                                                           \n"
    "    # the better way is use http callback to do the token auth by the edge,                                                            \n"
    "    # but if user prefer origin check(auth), the token_traverse if better solution.                                                    \n"
    "    # default: off                                                                                                                     \n"
    "    token_traverse  off;                                                                                                               \n"
    "}                                                                                                                                      \n"
    "# vhost for edge, change vhost.                                                                                                        \n"
    "vhost change.edge.srs.com {                                                                                                            \n"
    "    mode            remote;                                                                                                            \n"
    "    # TODO: FIXME: support extra params.                                                                                               \n"
    "    origin          127.0.0.1:1935 localhost:1935 {                                                                                    \n"
    "        # specify the vhost to override the vhost in client request.                                                                   \n"
    "        vhost       edge2.srs.com;                                                                                                     \n"
    "        # specify the refer(pageUrl) to override the refer in client request.                                                          \n"
    "        refer       http://srs/index.html;                                                                                             \n"
    "    }                                                                                                                                  \n"
    "}                                                                                                                                      \n"
    "                                                                                                                                       \n"
    "# vhost for dvr                                                                                                                        \n"
    "vhost dvr.srs.com {                                                                                                                    \n"
    "    # dvr RTMP stream to file,                                                                                                         \n"
    "    # start to record to file when encoder publish,                                                                                    \n"
    "    # reap flv according by specified dvr_plan.                                                                                        \n"
    "    # http callbacks:                                                                                                                  \n"
    "    # @see http callback on_dvr_hss_reap_flv on http_hooks section.                                                                    \n"
    "    dvr {                                                                                                                              \n"
    "        # whether enabled dvr features                                                                                                 \n"
    "        # default: off                                                                                                                 \n"
    "        enabled         on;                                                                                                            \n"
    "        # the dvr output path.                                                                                                         \n"
    "        # the app dir is auto created under the dvr_path.                                                                              \n"
    "        # for example, for rtmp stream:                                                                                                \n"
    "        #   rtmp://127.0.0.1/live/livestream                                                                                           \n"
    "        #   http://127.0.0.1/live/livestream.m3u8                                                                                      \n"
    "        # where dvr_path is /dvr, srs will create the following files:                                                                 \n"
    "        #   /dvr/live       the app dir for all streams.                                                                               \n"
    "        #   /dvr/live/livestream.{time}.flv   the dvr flv file.                                                                        \n"
    "        # @remark, the time use system timestamp in ms, user can use http callback to rename it.                                       \n"
    "        # in a word, the dvr_path is for vhost.                                                                                        \n"
    "        # default: ./objs/nginx/html                                                                                                   \n"
    "        dvr_path        ./objs/nginx/html;                                                                                             \n"
    "        # the dvr plan. canbe:                                                                                                         \n"
    "        #   session reap flv when session end(unpublish).                                                                              \n"
    "        #   segment reap flv when flv duration exceed the specified dvr_duration.                                                      \n"
    "        #   hss     reap flv required by bravo(chnvideo.com) p2p system.                                                               \n"
    "        # default: session                                                                                                             \n"
    "        dvr_plan        session;                                                                                                       \n"
    "        # the param for plan(segment), in seconds.                                                                                     \n"
    "        # default: 30                                                                                                                  \n"
    "        dvr_duration    30;                                                                                                            \n"
    "        # about the stream monotonically increasing:                                                                                   \n"
    "        #   1. video timestamp is monotonically increasing,                                                                            \n"
    "        #   2. audio timestamp is monotonically increasing,                                                                            \n"
    "        #   3. video and audio timestamp is interleaved monotonically increasing.                                                      \n"
    "        # it's specified by RTMP specification, @see 3. Byte Order, Alignment, and Time Format                                         \n"
    "        # however, some encoder cannot provides this feature, please set this to off to ignore time jitter.                            \n"
    "        # the time jitter algorithm:                                                                                                   \n"
    "        #   1. full, to ensure stream start at zero, and ensure stream monotonically increasing.                                       \n"
    "        #   2. zero, only ensure sttream start at zero, ignore timestamp jitter.                                                       \n"
    "        #   3. off, disable the time jitter algorithm, like atc.                                                                       \n"
    "        # default: full                                                                                                                \n"
    "        time_jitter             full;                                                                                                  \n"
    "    }                                                                                                                                  \n"
    "}                                                                                                                                      \n"
    "                                                                                                                                       \n"
    "# vhost for ingest                                                                                                                     \n"
    "vhost ingest.srs.com {                                                                                                                 \n"
    "    # ingest file/stream/device then push to SRS over RTMP.                                                                            \n"
    "    # the name/id used to identify the ingest, must be unique in global.                                                               \n"
    "    # ingest id is used in reload or http api management.                                                                              \n"
    "    ingest livestream {                                                                                                                \n"
    "        # whether enabled ingest features                                                                                              \n"
    "        # default: off                                                                                                                 \n"
    "        enabled      on;                                                                                                               \n"
    "        # input file/stream/device                                                                                                     \n"
    "        # @remark only support one input.                                                                                              \n"
    "        input {                                                                                                                        \n"
    "            # the type of input.                                                                                                       \n"
    "            # can be file/stream/device, that is,                                                                                      \n"
    "            #   file: ingest file specifies by url.                                                                                    \n"
    "            #   stream: ingest stream specifeis by url.                                                                                \n"
    "            #   device: not support yet.                                                                                               \n"
    "            # default: file                                                                                                            \n"
    "            type    file;                                                                                                              \n"
    "            # the url of file/stream.                                                                                                  \n"
    "            url     ./doc/source.200kbps.768x320.flv;                                                                                  \n"
    "        }                                                                                                                              \n"
    "        # the ffmpeg                                                                                                                   \n"
    "        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;                                                                                          \n"
    "        # the transcode engine, @see all.transcode.srs.com                                                                             \n"
    "        # @remark, the output is specified following.                                                                                  \n"
    "        engine {                                                                                                                       \n"
    "            # @see enabled of transcode engine.                                                                                        \n"
    "            # if disabled or vcodec/acodec not specified, use copy.                                                                    \n"
    "            # default: off.                                                                                                            \n"
    "            enabled          off;                                                                                                      \n"
    "            # output stream. variables:                                                                                                \n"
    "            # [vhost] current vhost which start the ingest.                                                                            \n"
    "            # [port] system RTMP stream port.                                                                                          \n"
    "            output          rtmp://127.0.0.1:[port]/live?vhost=[vhost]/livestream;                                                     \n"
    "        }                                                                                                                              \n"
    "    }                                                                                                                                  \n"
    "}                                                                                                                                      \n"
    "                                                                                                                                       \n"
    "# vhost for http                                                                                                                       \n"
    "vhost http.srs.com {                                                                                                                   \n"
    "    # http vhost specified config                                                                                                      \n"
    "    http {                                                                                                                             \n"
    "        # whether enabled the http streaming service for vhost.                                                                        \n"
    "        # default: off                                                                                                                 \n"
    "        enabled     on;                                                                                                                \n"
    "        # the virtual directory root for this vhost to mount at                                                                        \n"
    "        # for example, if mount to /hls, user access by http://server/hls                                                              \n"
    "        # default: /                                                                                                                   \n"
    "        mount       /hls;                                                                                                              \n"
    "        # main dir of vhost,                                                                                                           \n"
    "        # to delivery HTTP stream of this vhost.                                                                                       \n"
    "        # default: ./objs/nginx/html                                                                                                   \n"
    "        dir         ./objs/nginx/html/hls;                                                                                             \n"
    "    }                                                                                                                                  \n"
    "}                                                                                                                                      \n"
    "                                                                                                                                       \n"
    "# the vhost with hls specified.                                                                                                        \n"
    "vhost with-hls.srs.com {                                                                                                               \n"
    "    hls {                                                                                                                              \n"
    "        # whether the hls is enabled.                                                                                                  \n"
    "        # if off, donot write hls(ts and m3u8) when publish.                                                                           \n"
    "        # default: off                                                                                                                 \n"
    "        enabled         on;                                                                                                            \n"
    "        # the hls output path.                                                                                                         \n"
    "        # the app dir is auto created under the hls_path.                                                                              \n"
    "        # for example, for rtmp stream:                                                                                                \n"
    "        #   rtmp://127.0.0.1/live/livestream                                                                                           \n"
    "        #   http://127.0.0.1/live/livestream.m3u8                                                                                      \n"
    "        # where hls_path is /hls, srs will create the following files:                                                                 \n"
    "        #   /hls/live       the app dir for all streams.                                                                               \n"
    "        #   /hls/live/livestream.m3u8   the HLS m3u8 file.                                                                             \n"
    "        #   /hls/live/livestream-1.ts   the HLS media/ts file.                                                                         \n"
    "        # in a word, the hls_path is for vhost.                                                                                        \n"
    "        # default: ./objs/nginx/html                                                                                                   \n"
    "        hls_path        ./objs/nginx/html;                                                                                             \n"
    "        # the hls fragment in seconds, the duration of a piece of ts.                                                                  \n"
    "        # default: 10                                                                                                                  \n"
    "        hls_fragment    10;                                                                                                            \n"
    "        # the hls window in seconds, the number of ts in m3u8.                                                                         \n"
    "        # default: 60                                                                                                                  \n"
    "        hls_window      60;                                                                                                            \n"
    "    }                                                                                                                                  \n"
    "}                                                                                                                                      \n"
    "# the vhost with hls disabled.                                                                                                         \n"
    "vhost no-hls.srs.com {                                                                                                                 \n"
    "    hls {                                                                                                                              \n"
    "        # whether the hls is enabled.                                                                                                  \n"
    "        # if off, donot write hls(ts and m3u8) when publish.                                                                           \n"
    "        # default: off                                                                                                                 \n"
    "        enabled         off;                                                                                                           \n"
    "    }                                                                                                                                  \n"
    "}                                                                                                                                      \n"
    "                                                                                                                                       \n"
    "# the http hook callback vhost, srs will invoke the hooks for specified events.                                                        \n"
    "vhost hooks.callback.srs.com {                                                                                                         \n"
    "    http_hooks {                                                                                                                       \n"
    "        # whether the http hooks enalbe.                                                                                               \n"
    "        # default off.                                                                                                                 \n"
    "        enabled         on;                                                                                                            \n"
    "        # when client connect to vhost/app, call the hook,                                                                             \n"
    "        # the request in the POST data string is a object encode by json:                                                              \n"
    "        #       {                                                                                                                      \n"
    "        #           \"action\": \"on_connect\",                                                                                        \n"
    "        #           \"client_id\": 1985,                                                                                               \n"
    "        #           \"ip\": \"192.168.1.10\", \"vhost\": \"video.test.com\", \"app\": \"live\",                                        \n"
    "        #           \"tcUrl\": \"rtmp://video.test.com/live?key=d2fa801d08e3f90ed1e1670e6e52651a\",                                    \n"
    "        #           \"pageUrl\": \"http://www.test.com/live.html\"                                                                     \n"
    "        #       }                                                                                                                      \n"
    "        # if valid, the hook must return HTTP code 200(Stauts OK) and response                                                         \n"
    "        # an int value specifies the error code(0 corresponding to success):                                                           \n"
    "        #       0                                                                                                                      \n"
    "        # support multiple api hooks, format:                                                                                          \n"
    "        #       on_connect http://xxx/api0 http://xxx/api1 http://xxx/apiN                                                             \n"
    "        on_connect      http://127.0.0.1:8085/api/v1/clients http://localhost:8085/api/v1/clients;                                     \n"
    "        # when client close/disconnect to vhost/app/stream, call the hook,                                                             \n"
    "        # the request in the POST data string is a object encode by json:                                                              \n"
    "        #       {                                                                                                                      \n"
    "        #           \"action\": \"on_close\",                                                                                          \n"
    "        #           \"client_id\": 1985,                                                                                               \n"
    "        #           \"ip\": \"192.168.1.10\", \"vhost\": \"video.test.com\", \"app\": \"live\"                                         \n"
    "        #       }                                                                                                                      \n"
    "        # if valid, the hook must return HTTP code 200(Stauts OK) and response                                                         \n"
    "        # an int value specifies the error code(0 corresponding to success):                                                           \n"
    "        #       0                                                                                                                      \n"
    "        # support multiple api hooks, format:                                                                                          \n"
    "        #       on_close http://xxx/api0 http://xxx/api1 http://xxx/apiN                                                               \n"
    "        on_close        http://127.0.0.1:8085/api/v1/clients http://localhost:8085/api/v1/clients;                                     \n"
    "        # when client(encoder) publish to vhost/app/stream, call the hook,                                                             \n"
    "        # the request in the POST data string is a object encode by json:                                                              \n"
    "        #       {                                                                                                                      \n"
    "        #           \"action\": \"on_publish\",                                                                                        \n"
    "        #           \"client_id\": 1985,                                                                                               \n"
    "        #           \"ip\": \"192.168.1.10\", \"vhost\": \"video.test.com\", \"app\": \"live\",                                        \n"
    "        #           \"stream\": \"livestream\"                                                                                         \n"
    "        #       }                                                                                                                      \n"
    "        # if valid, the hook must return HTTP code 200(Stauts OK) and response                                                         \n"
    "        # an int value specifies the error code(0 corresponding to success):                                                           \n"
    "        #       0                                                                                                                      \n"
    "        # support multiple api hooks, format:                                                                                          \n"
    "        #       on_publish http://xxx/api0 http://xxx/api1 http://xxx/apiN                                                             \n"
    "        on_publish      http://127.0.0.1:8085/api/v1/streams http://localhost:8085/api/v1/streams;                                     \n"
    "        # when client(encoder) stop publish to vhost/app/stream, call the hook,                                                        \n"
    "        # the request in the POST data string is a object encode by json:                                                              \n"
    "        #       {                                                                                                                      \n"
    "        #           \"action\": \"on_unpublish\",                                                                                      \n"
    "        #           \"client_id\": 1985,                                                                                               \n"
    "        #           \"ip\": \"192.168.1.10\", \"vhost\": \"video.test.com\", \"app\": \"live\",                                        \n"
    "        #           \"stream\": \"livestream\"                                                                                         \n"
    "        #       }                                                                                                                      \n"
    "        # if valid, the hook must return HTTP code 200(Stauts OK) and response                                                         \n"
    "        # an int value specifies the error code(0 corresponding to success):                                                           \n"
    "        #       0                                                                                                                      \n"
    "        # support multiple api hooks, format:                                                                                          \n"
    "        #       on_unpublish http://xxx/api0 http://xxx/api1 http://xxx/apiN                                                           \n"
    "        on_unpublish    http://127.0.0.1:8085/api/v1/streams http://localhost:8085/api/v1/streams;                                     \n"
    "        # when client start to play vhost/app/stream, call the hook,                                                                   \n"
    "        # the request in the POST data string is a object encode by json:                                                              \n"
    "        #       {                                                                                                                      \n"
    "        #           \"action\": \"on_play\",                                                                                           \n"
    "        #           \"client_id\": 1985,                                                                                               \n"
    "        #           \"ip\": \"192.168.1.10\", \"vhost\": \"video.test.com\", \"app\": \"live\",                                        \n"
    "        #           \"stream\": \"livestream\"                                                                                         \n"
    "        #       }                                                                                                                      \n"
    "        # if valid, the hook must return HTTP code 200(Stauts OK) and response                                                         \n"
    "        # an int value specifies the error code(0 corresponding to success):                                                           \n"
    "        #       0                                                                                                                      \n"
    "        # support multiple api hooks, format:                                                                                          \n"
    "        #       on_play http://xxx/api0 http://xxx/api1 http://xxx/apiN                                                                \n"
    "        on_play         http://127.0.0.1:8085/api/v1/sessions http://localhost:8085/api/v1/sessions;                                   \n"
    "        # when client stop to play vhost/app/stream, call the hook,                                                                    \n"
    "        # the request in the POST data string is a object encode by json:                                                              \n"
    "        #       {                                                                                                                      \n"
    "        #           \"action\": \"on_stop\",                                                                                           \n"
    "        #           \"client_id\": 1985,                                                                                               \n"
    "        #           \"ip\": \"192.168.1.10\", \"vhost\": \"video.test.com\", \"app\": \"live\",                                        \n"
    "        #           \"stream\": \"livestream\"                                                                                         \n"
    "        #       }                                                                                                                      \n"
    "        # if valid, the hook must return HTTP code 200(Stauts OK) and response                                                         \n"
    "        # an int value specifies the error code(0 corresponding to success):                                                           \n"
    "        #       0                                                                                                                      \n"
    "        # support multiple api hooks, format:                                                                                          \n"
    "        #       on_stop http://xxx/api0 http://xxx/api1 http://xxx/apiN                                                                \n"
    "        on_stop         http://127.0.0.1:8085/api/v1/sessions http://localhost:8085/api/v1/sessions;                                   \n"
    "        #                                                                                                                              \n"
    "        # for dvr(dvr_plan is hss).                                                                                                    \n"
    "        # when dvr got flv header, call the hook,                                                                                      \n"
    "        # the request in the POST data string is a object encode by json:                                                              \n"
    "        #       {                                                                                                                      \n"
    "        #           \"action\": \"on_dvr_hss_reap_flv_header\",                                                                        \n"
    "        #           \"vhost\": \"video.test.com\", \"app\": \"live\",                                                                  \n"
    "        #           \"stream\": \"livestream\",                                                                                        \n"
    "        #           \"segment\": {                                                                                                     \n"
    "        #               \"cwd\": \"/usr/local/srs\",                                                                                   \n"
    "        #               \"path\": \"./objs/nginx/html/live/livestream.header.flv\"                                                     \n"
    "        #           }                                                                                                                  \n"
    "        #       }                                                                                                                      \n"
    "        # when dvr reap flv file, call the hook,                                                                                       \n"
    "        # the request in the POST data string is a object encode by json:                                                              \n"
    "        #       {                                                                                                                      \n"
    "        #           \"action\": \"on_dvr_hss_reap_flv\",                                                                               \n"
    "        #           \"vhost\": \"video.test.com\", \"app\": \"live\",                                                                  \n"
    "        #           \"stream\": \"livestream\",                                                                                        \n"
    "        #           \"segment\": {                                                                                                     \n"
    "        #               \"cwd\": \"/usr/local/srs\",                                                                                   \n"
    "        #               \"path\": \"./objs/nginx/html/live/livestream.1398315892865.flv\",                                             \n"
    "        #               \"duration\": 1001, \"offset\":0,                                                                              \n"
    "        #               \"has_keyframe\": true, \"pts\":1398315895958                                                                  \n"
    "        #           }                                                                                                                  \n"
    "        #       }                                                                                                                      \n"
    "        # if valid, the hook must return HTTP code 200(Stauts OK) and response                                                         \n"
    "        # an int value specifies the error code(0 corresponding to success):                                                           \n"
    "        #       0                                                                                                                      \n"
    "        # support multiple api hooks, format:                                                                                          \n"
    "        #       on_stop http://xxx/api0 http://xxx/api1 http://xxx/apiN                                                                \n"
    "        on_dvr_hss_reap_flv     http://127.0.0.1:8085/api/v1/dvrs http://localhost:8085/api/v1/dvrs;                                   \n"
    "    }                                                                                                                                  \n"
    "}                                                                                                                                      \n"
    "                                                                                                                                       \n"
    "# the vhost for min delay, donot cache any stream.                                                                                     \n"
    "vhost min.delay.com {                                                                                                                  \n"
    "    # whether cache the last gop.                                                                                                      \n"
    "    # if on, cache the last gop and dispatch to client,                                                                                \n"
    "    #   to enabled fast startup for client, client play immediately.                                                                   \n"
    "    # if off, send the latest media data to client,                                                                                    \n"
    "    #   client need to wait for the next Iframe to decode and show the video.                                                          \n"
    "    # set to off if requires min delay;                                                                                                \n"
    "    # set to on if requires client fast startup.                                                                                       \n"
    "    # default: on                                                                                                                      \n"
    "    gop_cache       off;                                                                                                               \n"
    "    # the max live queue length in seconds.                                                                                            \n"
    "    # if the messages in the queue exceed the max length,                                                                              \n"
    "    # drop the old whole gop.                                                                                                          \n"
    "    # default: 30                                                                                                                      \n"
    "    queue_length    10;                                                                                                                \n"
    "}                                                                                                                                      \n"
    "                                                                                                                                       \n"
    "# the vhost for antisuck.                                                                                                              \n"
    "vhost refer.anti_suck.com {                                                                                                            \n"
    "    # the common refer for play and publish.                                                                                           \n"
    "    # if the page url of client not in the refer, access denied.                                                                       \n"
    "    # if not specified this field, allow all.                                                                                          \n"
    "    # default: not specified.                                                                                                          \n"
    "    refer           github.com github.io;                                                                                              \n"
    "    # refer for publish clients specified.                                                                                             \n"
    "    # the common refer is not overrided by this.                                                                                       \n"
    "    # if not specified this field, allow all.                                                                                          \n"
    "    # default: not specified.                                                                                                          \n"
    "    refer_publish   github.com github.io;                                                                                              \n"
    "    # refer for play clients specified.                                                                                                \n"
    "    # the common refer is not overrided by this.                                                                                       \n"
    "    # if not specified this field, allow all.                                                                                          \n"
    "    # default: not specified.                                                                                                          \n"
    "    refer_play      github.com github.io;                                                                                              \n"
    "}                                                                                                                                      \n"
    "                                                                                                                                       \n"
    "# the vhost which forward publish streams.                                                                                             \n"
    "vhost same.vhost.forward.srs.com {                                                                                                     \n"
    "    # forward all publish stream to the specified server.                                                                              \n"
    "    # this used to split/forward the current stream for cluster active-standby,                                                        \n"
    "    # active-active for cdn to build high available fault tolerance system.                                                            \n"
    "    # format: {ip}:{port} {ip_N}:{port_N}                                                                                              \n"
    "    # or specify the vhost by params, @see: change.vhost.forward.srs.com                                                               \n"
    "    # if vhost not specified, use the request vhost instead.                                                                           \n"
    "    forward         127.0.0.1:1936 127.0.0.1:1937;                                                                                     \n"
    "}                                                                                                                                      \n"
    "# TODO: FIXME: support extra params.                                                                                                   \n"
    "# [plan] the vhost which forward publish streams to other vhosts.                                                                      \n"
    "vhost change.vhost.forward.srs.com {                                                                                                   \n"
    "    forward         127.0.0.1:1936 127.0.0.1:1937 {                                                                                    \n"
    "        # specify the vhost to override the vhost in client request.                                                                   \n"
    "        vhost       forward2.srs.com;                                                                                                  \n"
    "        # specify the refer(pageUrl) to override the refer in client request.                                                          \n"
    "        refer       http://srs/index.html;                                                                                             \n"
    "    }                                                                                                                                  \n"
    "    forward         127.0.0.1:1938 {                                                                                                   \n"
    "        vhost       forward3.srs.com;                                                                                                  \n"
    "    }                                                                                                                                  \n"
    "}                                                                                                                                      \n"
    "                                                                                                                                       \n"
    "# the mirror filter of ffmpeg, @see: http://ffmpeg.org/ffmpeg-filters.html#Filtering-Introduction                                      \n"
    "vhost mirror.transcode.srs.com {                                                                                                       \n"
    "    transcode {                                                                                                                        \n"
    "        enabled     on;                                                                                                                \n"
    "        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;                                                                                          \n"
    "        engine mirror {                                                                                                                \n"
    "            enabled         on;                                                                                                        \n"
    "            vfilter {                                                                                                                  \n"
    "                vf                  'split [main][tmp]; [tmp] crop=iw:ih/2:0:0, vflip [flip]; [main][flip] overlay=0:H/2';             \n"
    "            }                                                                                                                          \n"
    "            vcodec          libx264;                                                                                                   \n"
    "            vbitrate        300;                                                                                                       \n"
    "            vfps            20;                                                                                                        \n"
    "            vwidth          768;                                                                                                       \n"
    "            vheight         320;                                                                                                       \n"
    "            vthreads        2;                                                                                                         \n"
    "            vprofile        baseline;                                                                                                  \n"
    "            vpreset         superfast;                                                                                                 \n"
    "            vparams {                                                                                                                  \n"
    "            }                                                                                                                          \n"
    "            acodec          libaacplus;                                                                                                \n"
    "            abitrate        45;                                                                                                        \n"
    "            asample_rate    44100;                                                                                                     \n"
    "            achannels       2;                                                                                                         \n"
    "            aparams {                                                                                                                  \n"
    "            }                                                                                                                          \n"
    "            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                             \n"
    "        }                                                                                                                              \n"
    "    }                                                                                                                                  \n"
    "}                                                                                                                                      \n"
    "#                                                                                                                                      \n"
    "# the drawtext filter of ffmpeg, @see: http://ffmpeg.org/ffmpeg-filters.html#drawtext-1                                                \n"
    "# remark: we remove the libfreetype which always cause build failed, you must add it manual if needed.                                 \n"
    "#                                                                                                                                      \n"
    "#######################################################################################################                                \n"
    "# the crop filter of ffmpeg, @see: http://ffmpeg.org/ffmpeg-filters.html#crop                                                          \n"
    "vhost crop.transcode.srs.com {                                                                                                         \n"
    "    transcode {                                                                                                                        \n"
    "        enabled     on;                                                                                                                \n"
    "        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;                                                                                          \n"
    "        engine crop {                                                                                                                  \n"
    "            enabled         on;                                                                                                        \n"
    "            vfilter {                                                                                                                  \n"
    "                vf                  'crop=in_w-20:in_h-160:10:80';                                                                     \n"
    "            }                                                                                                                          \n"
    "            vcodec          libx264;                                                                                                   \n"
    "            vbitrate        300;                                                                                                       \n"
    "            vfps            20;                                                                                                        \n"
    "            vwidth          768;                                                                                                       \n"
    "            vheight         320;                                                                                                       \n"
    "            vthreads        2;                                                                                                         \n"
    "            vprofile        baseline;                                                                                                  \n"
    "            vpreset         superfast;                                                                                                 \n"
    "            vparams {                                                                                                                  \n"
    "            }                                                                                                                          \n"
    "            acodec          libaacplus;                                                                                                \n"
    "            abitrate        45;                                                                                                        \n"
    "            asample_rate    44100;                                                                                                     \n"
    "            achannels       2;                                                                                                         \n"
    "            aparams {                                                                                                                  \n"
    "            }                                                                                                                          \n"
    "            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                             \n"
    "        }                                                                                                                              \n"
    "    }                                                                                                                                  \n"
    "}                                                                                                                                      \n"
    "# the logo filter of ffmpeg, @see: http://ffmpeg.org/ffmpeg-filters.html#overlay                                                       \n"
    "vhost logo.transcode.srs.com {                                                                                                         \n"
    "    transcode {                                                                                                                        \n"
    "        enabled     on;                                                                                                                \n"
    "        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;                                                                                          \n"
    "        engine logo {                                                                                                                  \n"
    "            enabled         on;                                                                                                        \n"
    "            vfilter {                                                                                                                  \n"
    "                i               ./doc/ffmpeg-logo.png;                                                                                 \n"
    "                filter_complex      'overlay=10:10';                                                                                   \n"
    "            }                                                                                                                          \n"
    "            vcodec          libx264;                                                                                                   \n"
    "            vbitrate        300;                                                                                                       \n"
    "            vfps            20;                                                                                                        \n"
    "            vwidth          768;                                                                                                       \n"
    "            vheight         320;                                                                                                       \n"
    "            vthreads        2;                                                                                                         \n"
    "            vprofile        baseline;                                                                                                  \n"
    "            vpreset         superfast;                                                                                                 \n"
    "            vparams {                                                                                                                  \n"
    "            }                                                                                                                          \n"
    "            acodec          libaacplus;                                                                                                \n"
    "            abitrate        45;                                                                                                        \n"
    "            asample_rate    44100;                                                                                                     \n"
    "            achannels       2;                                                                                                         \n"
    "            aparams {                                                                                                                  \n"
    "            }                                                                                                                          \n"
    "            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                             \n"
    "        }                                                                                                                              \n"
    "    }                                                                                                                                  \n"
    "}                                                                                                                                      \n"
    "# audio transcode only.                                                                                                                \n"
    "# for example, FMLE publish audio codec in mp3, and donot support HLS output,                                                          \n"
    "# we can transcode the audio to aac and copy video to the new stream with HLS.                                                         \n"
    "vhost audio.transcode.srs.com {                                                                                                        \n"
    "    transcode {                                                                                                                        \n"
    "        enabled     on;                                                                                                                \n"
    "        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;                                                                                          \n"
    "        engine acodec {                                                                                                                \n"
    "            enabled         on;                                                                                                        \n"
    "            vcodec          copy;                                                                                                      \n"
    "            acodec          libaacplus;                                                                                                \n"
    "            abitrate        45;                                                                                                        \n"
    "            asample_rate    44100;                                                                                                     \n"
    "            achannels       2;                                                                                                         \n"
    "            aparams {                                                                                                                  \n"
    "            }                                                                                                                          \n"
    "            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                             \n"
    "        }                                                                                                                              \n"
    "    }                                                                                                                                  \n"
    "}                                                                                                                                      \n"
    "# disable video, transcode/copy audio.                                                                                                 \n"
    "# for example, publish pure audio stream.                                                                                              \n"
    "vhost vn.transcode.srs.com {                                                                                                           \n"
    "    transcode {                                                                                                                        \n"
    "        enabled     on;                                                                                                                \n"
    "        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;                                                                                          \n"
    "        engine vn {                                                                                                                    \n"
    "            enabled         on;                                                                                                        \n"
    "            vcodec          vn;                                                                                                        \n"
    "            acodec          libaacplus;                                                                                                \n"
    "            abitrate        45;                                                                                                        \n"
    "            asample_rate    44100;                                                                                                     \n"
    "            achannels       2;                                                                                                         \n"
    "            aparams {                                                                                                                  \n"
    "            }                                                                                                                          \n"
    "            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                             \n"
    "        }                                                                                                                              \n"
    "    }                                                                                                                                  \n"
    "}                                                                                                                                      \n"
    "# ffmpeg-copy(forward implements by ffmpeg).                                                                                           \n"
    "# copy the video and audio to a new stream.                                                                                            \n"
    "vhost copy.transcode.srs.com {                                                                                                         \n"
    "    transcode {                                                                                                                        \n"
    "        enabled     on;                                                                                                                \n"
    "        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;                                                                                          \n"
    "        engine copy {                                                                                                                  \n"
    "            enabled         on;                                                                                                        \n"
    "            vcodec          copy;                                                                                                      \n"
    "            acodec          copy;                                                                                                      \n"
    "            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                             \n"
    "        }                                                                                                                              \n"
    "    }                                                                                                                                  \n"
    "}                                                                                                                                      \n"
    "# transcode all app and stream of vhost                                                                                                \n"
    "vhost all.transcode.srs.com {                                                                                                          \n"
    "    # the streaming transcode configs.                                                                                                 \n"
    "    transcode {                                                                                                                        \n"
    "        # whether the transcode enabled.                                                                                               \n"
    "        # if off, donot transcode.                                                                                                     \n"
    "        # default: off.                                                                                                                \n"
    "        enabled     on;                                                                                                                \n"
    "        # the ffmpeg                                                                                                                   \n"
    "        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;                                                                                          \n"
    "        # the transcode engine for matched stream.                                                                                     \n"
    "        # all matched stream will transcoded to the following stream.                                                                  \n"
    "        # the transcode set name(ie. hd) is optional and not used.                                                                     \n"
    "        engine ffsuper {                                                                                                               \n"
    "            # whether the engine is enabled                                                                                            \n"
    "            # default: off.                                                                                                            \n"
    "            enabled         on;                                                                                                        \n"
    "            # input format, can be:                                                                                                    \n"
    "            # off, do not specifies the format, ffmpeg will guess it.                                                                  \n"
    "            # flv, for flv or RTMP stream.                                                                                             \n"
    "            # other format, for example, mp4/aac whatever.                                                                             \n"
    "            # default: flv                                                                                                             \n"
    "            iformat         flv;                                                                                                       \n"
    "            # ffmpeg filters, follows the main input.                                                                                  \n"
    "            vfilter {                                                                                                                  \n"
    "                # the logo input file.                                                                                                 \n"
    "                i               ./doc/ffmpeg-logo.png;                                                                                 \n"
    "                # the ffmpeg complex filter.                                                                                           \n"
    "                # for filters, @see: http://ffmpeg.org/ffmpeg-filters.html                                                             \n"
    "                filter_complex  'overlay=10:10';                                                                                       \n"
    "            }                                                                                                                          \n"
    "            # video encoder name. can be:                                                                                              \n"
    "            # libx264: use h.264(libx264) video encoder.                                                                               \n"
    "            # copy: donot encoder the video stream, copy it.                                                                           \n"
    "            # vn: disable video output.                                                                                                \n"
    "            vcodec          libx264;                                                                                                   \n"
    "            # video bitrate, in kbps                                                                                                   \n"
    "            vbitrate        1500;                                                                                                      \n"
    "            # video framerate.                                                                                                         \n"
    "            vfps            25;                                                                                                        \n"
    "            # video width, must be even numbers.                                                                                       \n"
    "            vwidth          768;                                                                                                       \n"
    "            # video height, must be even numbers.                                                                                      \n"
    "            vheight         320;                                                                                                       \n"
    "            # the max threads for ffmpeg to used.                                                                                      \n"
    "            vthreads        12;                                                                                                        \n"
    "            # x264 profile, @see x264 -help, can be:                                                                                   \n"
    "            # high,main,baseline                                                                                                       \n"
    "            vprofile        main;                                                                                                      \n"
    "            # x264 preset, @see x264 -help, can be:                                                                                    \n"
    "            # ultrafast,superfast,veryfast,faster,fast                                                                                 \n"
    "            # medium,slow,slower,veryslow,placebo                                                                                      \n"
    "            vpreset         medium;                                                                                                    \n"
    "            # other x264 or ffmpeg video params                                                                                        \n"
    "            vparams {                                                                                                                  \n"
    "                # ffmpeg options, @see: http://ffmpeg.org/ffmpeg.html                                                                  \n"
    "                t               100;                                                                                                   \n"
    "                # 264 params, @see: http://ffmpeg.org/ffmpeg-codecs.html#libx264                                                       \n"
    "                coder           1;                                                                                                     \n"
    "                b_strategy      2;                                                                                                     \n"
    "                bf              3;                                                                                                     \n"
    "                refs            10;                                                                                                    \n"
    "            }                                                                                                                          \n"
    "            # audio encoder name. can be:                                                                                              \n"
    "            # libaacplus: use aac(libaacplus) audio encoder.                                                                           \n"
    "            # copy: donot encoder the audio stream, copy it.                                                                           \n"
    "            # an: disable audio output.                                                                                                \n"
    "            acodec          libaacplus;                                                                                                \n"
    "            # audio bitrate, in kbps. [16, 72] for libaacplus.                                                                         \n"
    "            abitrate        70;                                                                                                        \n"
    "            # audio sample rate. for flv/rtmp, it must be:                                                                             \n"
    "            # 44100,22050,11025,5512                                                                                                   \n"
    "            asample_rate    44100;                                                                                                     \n"
    "            # audio channel, 1 for mono, 2 for stereo.                                                                                 \n"
    "            achannels       2;                                                                                                         \n"
    "            # other ffmpeg audio params                                                                                                \n"
    "            aparams {                                                                                                                  \n"
    "                # audio params, @see: http://ffmpeg.org/ffmpeg-codecs.html#Audio-Encoders                                              \n"
    "                profile:a   aac_low;                                                                                                   \n"
    "            }                                                                                                                          \n"
    "            # output format, can be:                                                                                                   \n"
    "            # off, do not specifies the format, ffmpeg will guess it.                                                                  \n"
    "            # flv, for flv or RTMP stream.                                                                                             \n"
    "            # other format, for example, mp4/aac whatever.                                                                             \n"
    "            # default: flv                                                                                                             \n"
    "            oformat         flv;                                                                                                       \n"
    "            # output stream. variables:                                                                                                \n"
    "            # [vhost] the input stream vhost.                                                                                          \n"
    "            # [port] the intput stream port.                                                                                           \n"
    "            # [app] the input stream app.                                                                                              \n"
    "            # [stream] the input stream name.                                                                                          \n"
    "            # [engine] the tanscode engine name.                                                                                       \n"
    "            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                             \n"
    "        }                                                                                                                              \n"
    "        engine ffhd {                                                                                                                  \n"
    "            enabled         on;                                                                                                        \n"
    "            vcodec          libx264;                                                                                                   \n"
    "            vbitrate        1200;                                                                                                      \n"
    "            vfps            25;                                                                                                        \n"
    "            vwidth          1382;                                                                                                      \n"
    "            vheight         576;                                                                                                       \n"
    "            vthreads        6;                                                                                                         \n"
    "            vprofile        main;                                                                                                      \n"
    "            vpreset         medium;                                                                                                    \n"
    "            vparams {                                                                                                                  \n"
    "            }                                                                                                                          \n"
    "            acodec          libaacplus;                                                                                                \n"
    "            abitrate        70;                                                                                                        \n"
    "            asample_rate    44100;                                                                                                     \n"
    "            achannels       2;                                                                                                         \n"
    "            aparams {                                                                                                                  \n"
    "            }                                                                                                                          \n"
    "            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                             \n"
    "        }                                                                                                                              \n"
    "        engine ffsd {                                                                                                                  \n"
    "            enabled         on;                                                                                                        \n"
    "            vcodec          libx264;                                                                                                   \n"
    "            vbitrate        800;                                                                                                       \n"
    "            vfps            25;                                                                                                        \n"
    "            vwidth          1152;                                                                                                      \n"
    "            vheight         480;                                                                                                       \n"
    "            vthreads        4;                                                                                                         \n"
    "            vprofile        main;                                                                                                      \n"
    "            vpreset         fast;                                                                                                      \n"
    "            vparams {                                                                                                                  \n"
    "            }                                                                                                                          \n"
    "            acodec          libaacplus;                                                                                                \n"
    "            abitrate        60;                                                                                                        \n"
    "            asample_rate    44100;                                                                                                     \n"
    "            achannels       2;                                                                                                         \n"
    "            aparams {                                                                                                                  \n"
    "            }                                                                                                                          \n"
    "            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                             \n"
    "        }                                                                                                                              \n"
    "        engine fffast {                                                                                                                \n"
    "            enabled     on;                                                                                                            \n"
    "            vcodec          libx264;                                                                                                   \n"
    "            vbitrate        300;                                                                                                       \n"
    "            vfps            20;                                                                                                        \n"
    "            vwidth          768;                                                                                                       \n"
    "            vheight         320;                                                                                                       \n"
    "            vthreads        2;                                                                                                         \n"
    "            vprofile        baseline;                                                                                                  \n"
    "            vpreset         superfast;                                                                                                 \n"
    "            vparams {                                                                                                                  \n"
    "            }                                                                                                                          \n"
    "            acodec          libaacplus;                                                                                                \n"
    "            abitrate        45;                                                                                                        \n"
    "            asample_rate    44100;                                                                                                     \n"
    "            achannels       2;                                                                                                         \n"
    "            aparams {                                                                                                                  \n"
    "            }                                                                                                                          \n"
    "            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                             \n"
    "        }                                                                                                                              \n"
    "        engine vcopy {                                                                                                                 \n"
    "            enabled     on;                                                                                                            \n"
    "            vcodec          copy;                                                                                                      \n"
    "            acodec          libaacplus;                                                                                                \n"
    "            abitrate        45;                                                                                                        \n"
    "            asample_rate    44100;                                                                                                     \n"
    "            achannels       2;                                                                                                         \n"
    "            aparams {                                                                                                                  \n"
    "            }                                                                                                                          \n"
    "            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                             \n"
    "        }                                                                                                                              \n"
    "        engine acopy {                                                                                                                 \n"
    "            enabled     on;                                                                                                            \n"
    "            vcodec          libx264;                                                                                                   \n"
    "            vbitrate        300;                                                                                                       \n"
    "            vfps            20;                                                                                                        \n"
    "            vwidth          768;                                                                                                       \n"
    "            vheight         320;                                                                                                       \n"
    "            vthreads        2;                                                                                                         \n"
    "            vprofile        baseline;                                                                                                  \n"
    "            vpreset         superfast;                                                                                                 \n"
    "            vparams {                                                                                                                  \n"
    "            }                                                                                                                          \n"
    "            acodec          copy;                                                                                                      \n"
    "            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                             \n"
    "        }                                                                                                                              \n"
    "        engine copy {                                                                                                                  \n"
    "            enabled     on;                                                                                                            \n"
    "            vcodec          copy;                                                                                                      \n"
    "            acodec          copy;                                                                                                      \n"
    "            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                             \n"
    "        }                                                                                                                              \n"
    "    }                                                                                                                                  \n"
    "}                                                                                                                                      \n"
    "# transcode all stream using the empty ffmpeg demo, donothing.                                                                         \n"
    "vhost ffempty.transcode.srs.com {                                                                                                      \n"
    "    transcode {                                                                                                                        \n"
    "        enabled     on;                                                                                                                \n"
    "        ffmpeg ./objs/research/ffempty;                                                                                                \n"
    "        engine empty {                                                                                                                 \n"
    "            enabled         on;                                                                                                        \n"
    "            vcodec          libx264;                                                                                                   \n"
    "            vbitrate        300;                                                                                                       \n"
    "            vfps            20;                                                                                                        \n"
    "            vwidth          768;                                                                                                       \n"
    "            vheight         320;                                                                                                       \n"
    "            vthreads        2;                                                                                                         \n"
    "            vprofile        baseline;                                                                                                  \n"
    "            vpreset         superfast;                                                                                                 \n"
    "            vparams {                                                                                                                  \n"
    "            }                                                                                                                          \n"
    "            acodec          libaacplus;                                                                                                \n"
    "            abitrate        45;                                                                                                        \n"
    "            asample_rate    44100;                                                                                                     \n"
    "            achannels       2;                                                                                                         \n"
    "            aparams {                                                                                                                  \n"
    "            }                                                                                                                          \n"
    "            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                             \n"
    "        }                                                                                                                              \n"
    "    }                                                                                                                                  \n"
    "}                                                                                                                                      \n"
    "# transcode all app and stream of app                                                                                                  \n"
    "vhost app.transcode.srs.com {                                                                                                          \n"
    "    # the streaming transcode configs.                                                                                                 \n"
    "    # if app specified, transcode all streams of app.                                                                                  \n"
    "    transcode live {                                                                                                                   \n"
    "        enabled     on;                                                                                                                \n"
    "        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;                                                                                          \n"
    "        engine {                                                                                                                       \n"
    "            enabled     off;                                                                                                           \n"
    "        }                                                                                                                              \n"
    "    }                                                                                                                                  \n"
    "}                                                                                                                                      \n"
    "# transcode specified stream.                                                                                                          \n"
    "vhost stream.transcode.srs.com {                                                                                                       \n"
    "    # the streaming transcode configs.                                                                                                 \n"
    "    # if stream specified, transcode the matched stream.                                                                               \n"
    "    transcode live/livestream {                                                                                                        \n"
    "        enabled     on;                                                                                                                \n"
    "        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;                                                                                          \n"
    "        engine {                                                                                                                       \n"
    "            enabled     off;                                                                                                           \n"
    "        }                                                                                                                              \n"
    "    }                                                                                                                                  \n"
    "}                                                                                                                                      \n"
    "                                                                                                                                       \n"
    "# vhost for bandwidth check                                                                                                            \n"
    "# generally, the bandcheck vhost must be: bandcheck.srs.com,                                                                           \n"
    "# or need to modify the vhost of client.                                                                                               \n"
    "vhost bandcheck.srs.com {                                                                                                              \n"
    "    enabled         on;                                                                                                                \n"
    "    chunk_size      65000;                                                                                                             \n"
    "    # bandwidth check config.                                                                                                          \n"
    "    bandcheck {                                                                                                                        \n"
    "        # whether support bandwidth check,                                                                                             \n"
    "        # default: off.                                                                                                                \n"
    "        enabled         on;                                                                                                            \n"
    "        # the key for server to valid,                                                                                                 \n"
    "        # if invalid key, server disconnect and abort the bandwidth check.                                                             \n"
    "        key             \"35c9b402c12a7246868752e2878f7e0e\";                                                                          \n"
    "        # the interval in seconds for bandwidth check,                                                                                 \n"
    "        # server donot allow new test request.                                                                                         \n"
    "        # default: 30                                                                                                                  \n"
    "        interval        30;                                                                                                            \n"
    "        # the max available check bandwidth in kbps.                                                                                   \n"
    "        # to avoid attack of bandwidth check.                                                                                          \n"
    "        # default: 1000                                                                                                                \n"
    "        limit_kbps      4000;                                                                                                          \n"
    "    }                                                                                                                                  \n"
    "}                                                                                                                                      \n"
    "                                                                                                                                       \n"
    "# set the chunk size of vhost.                                                                                                         \n"
    "vhost chunksize.srs.com {                                                                                                              \n"
    "    # the default chunk size is 128, max is 65536,                                                                                     \n"
    "    # some client does not support chunk size change,                                                                                  \n"
    "    # vhost chunk size will override the global value.                                                                                 \n"
    "    # default: global chunk size.                                                                                                      \n"
    "    chunk_size      128;                                                                                                               \n"
    "}                                                                                                                                      \n"
    "                                                                                                                                       \n"
    "# vhost for time jitter                                                                                                                \n"
    "vhost jitter.srs.com {                                                                                                                 \n"
    "    # about the stream monotonically increasing:                                                                                       \n"
    "    #   1. video timestamp is monotonically increasing,                                                                                \n"
    "    #   2. audio timestamp is monotonically increasing,                                                                                \n"
    "    #   3. video and audio timestamp is interleaved monotonically increasing.                                                          \n"
    "    # it's specified by RTMP specification, @see 3. Byte Order, Alignment, and Time Format                                             \n"
    "    # however, some encoder cannot provides this feature, please set this to off to ignore time jitter.                                \n"
    "    # the time jitter algorithm:                                                                                                       \n"
    "    #   1. full, to ensure stream start at zero, and ensure stream monotonically increasing.                                           \n"
    "    #   2. zero, only ensure sttream start at zero, ignore timestamp jitter.                                                           \n"
    "    #   3. off, disable the time jitter algorithm, like atc.                                                                           \n"
    "    # default: full                                                                                                                    \n"
    "    time_jitter             full;                                                                                                      \n"
    "}                                                                                                                                      \n"
    "                                                                                                                                       \n"
    "# vhost for atc.                                                                                                                       \n"
    "vhost atc.srs.com {                                                                                                                    \n"
    "    # vhost for atc for hls/hds/rtmp backup.                                                                                           \n"
    "    # generally, atc default to off, server delivery rtmp stream to client(flash) timestamp from 0.                                    \n"
    "    # when atc is on, server delivery rtmp stream by absolute time.                                                                    \n"
    "    # atc is used, for instance, encoder will copy stream to master and slave server,                                                  \n"
    "    # server use atc to delivery stream to edge/client, where stream time from master/slave server                                     \n"
    "    # is always the same, client/tools can slice RTMP stream to HLS according to the same time,                                        \n"
    "    # if the time not the same, the HLS stream cannot slice to support system backup.                                                  \n"
    "    #                                                                                                                                  \n"
    "    # @see http://www.adobe.com/cn/devnet/adobe-media-server/articles/varnish-sample-for-failover.html                                 \n"
    "    # @see http://www.baidu.com/#wd=hds%20hls%20atc                                                                                    \n"
    "    #                                                                                                                                  \n"
    "    # default: off                                                                                                                     \n"
    "    atc             on;                                                                                                                \n"
    "    # whether enable the auto atc,                                                                                                     \n"
    "    # if enabled, detect the bravo_atc=\"true\" in onMetaData packet,                                                                  \n"
    "    # set atc to on if matched.                                                                                                        \n"
    "    # always ignore the onMetaData if atc_auto is off.                                                                                 \n"
    "    # default: on                                                                                                                      \n"
    "    atc_auto        on;                                                                                                                \n"
    "}                                                                                                                                      \n"
    "                                                                                                                                       \n"
    "# the vhost disabled.                                                                                                                  \n"
    "vhost removed.srs.com {                                                                                                                \n"
    "    # whether the vhost is enabled.                                                                                                    \n"
    "    # if off, all request access denied.                                                                                               \n"
    "    # default: on                                                                                                                      \n"
    "    enabled         off;                                                                                                               \n"
    "}                                                                                                                                      \n"
    "                                                                                                                                       \n"
    "# config for the pithy print,                                                                                                          \n"
    "# which always print constant message specified by interval,                                                                           \n"
    "# whatever the clients in concurrency.                                                                                                 \n"
    "pithy_print {                                                                                                                          \n"
    "    # shared print interval for all publish clients, in milliseconds.                                                                  \n"
    "    # default: 10000                                                                                                                   \n"
    "    publish         10000;                                                                                                             \n"
    "    # shared print interval for all play clients, in milliseconds.                                                                     \n"
    "    # default: 10000                                                                                                                   \n"
    "    play            10000;                                                                                                             \n"
    "    # shared print interval for all forwarders, in milliseconds.                                                                       \n"
    "    # default: 10000                                                                                                                   \n"
    "    forwarder       10000;                                                                                                             \n"
    "    # shared print interval for all encoders, in milliseconds.                                                                         \n"
    "    # default: 10000                                                                                                                   \n"
    "    encoder         10000;                                                                                                             \n"
    "    # shared print interval for all ingesters, in milliseconds.                                                                        \n"
    "    # default: 10000                                                                                                                   \n"
    "    ingester        10000;                                                                                                             \n"
    "    # shared print interval for all hls, in milliseconds.                                                                              \n"
    "    # default: 10000                                                                                                                   \n"
    "    hls             10000;                                                                                                             \n"
    "    # shared print interval for all edge, in milliseconds.                                                                             \n"
    "    # default: 10000                                                                                                                   \n"
    "    edge            10000;                                                                                                             \n"
    "}                                                                                                                                      \n"
;

MockSrsConfigBuffer::MockSrsConfigBuffer(string buf)
{
    // read all.
    int filesize = (int)buf.length();
    
    if (filesize <= 0) {
        return;
    }
    
    // create buffer
    pos = last = start = new char[filesize];
    end = start + filesize;
    
    memcpy(start, buf.data(), filesize);
}

MockSrsConfigBuffer::~MockSrsConfigBuffer()
{
}

int MockSrsConfigBuffer::fullfill(const char* /*filename*/)
{
    return ERROR_SUCCESS;
}

MockSrsConfig::MockSrsConfig()
{
}

MockSrsConfig::~MockSrsConfig()
{
}

int MockSrsConfig::parse(string buf)
{
    MockSrsConfigBuffer buffer(buf);
    return parse_buffer(&buffer);
}

VOID TEST(ConfigTest, CheckMacros)
{
#ifndef SRS_CONSTS_LOCALHOST
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_PID_FILE
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_COFNIG_FILE
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_MAX_CONNECTIONS
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_HLS_PATH
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_HLS_FRAGMENT
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_HLS_WINDOW
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_DVR_PATH
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_DVR_PLAN_SESSION
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_DVR_PLAN_SEGMENT
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_DVR_PLAN_HSS
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_DVR_PLAN
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_DVR_DURATION
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_TIME_JITTER
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_QUEUE_LENGTH
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_PAUSED_LENGTH
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_BANDWIDTH_INTERVAL
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_BANDWIDTH_LIMIT_KBPS
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_HTTP_MOUNT
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_HTTP_DIR
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_HTTP_STREAM_PORT
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_HTTP_API_PORT
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_HTTP_HEAETBEAT_ENABLED
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_HTTP_HEAETBEAT_INTERVAL
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_HTTP_HEAETBEAT_URL
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_HTTP_HEAETBEAT_INDEX
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_HTTP_HEAETBEAT_SUMMARIES
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_STAGE_PLAY_USER_INTERVAL_MS
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_STAGE_PUBLISH_USER_INTERVAL_MS
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_STAGE_FORWARDER_INTERVAL_MS
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_STAGE_ENCODER_INTERVAL_MS
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_STAGE_INGESTER_INTERVAL_MS
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_STAGE_HLS_INTERVAL_MS
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_STAGE_EDGE_INTERVAL_MS
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_INGEST_TYPE_FILE
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_INGEST_TYPE_STREAM
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_TRANSCODE_IFORMAT
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_TRANSCODE_OFORMAT
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_LOG_FILE
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONSTS_NULL_FILE
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_FF_LOG_DIR
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_LOG_LEVEL
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_LOG_TANK_CONSOLE
    EXPECT_TRUE(false);
#endif
}

VOID TEST(ConfigDirectiveTest, ParseEmpty)
{
    MockSrsConfigBuffer buf("");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(0, (int)conf.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseNameOnly)
{
    MockSrsConfigBuffer buf("winlin;");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());
    
    SrsConfDirective& dir = *conf.directives.at(0);
    EXPECT_STREQ("winlin", dir.name.c_str());
    EXPECT_EQ(0, (int)dir.args.size());
    EXPECT_EQ(0, (int)dir.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseNameArg0Only)
{
    MockSrsConfigBuffer buf("winlin arg0;");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());
    
    SrsConfDirective& dir = *conf.directives.at(0);
    EXPECT_STREQ("winlin", dir.name.c_str());
    EXPECT_EQ(1, (int)dir.args.size());
    EXPECT_STREQ("arg0", dir.arg0().c_str());
    EXPECT_EQ(0, (int)dir.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseNameArg1Only)
{
    MockSrsConfigBuffer buf("winlin arg0 arg1;");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());
    
    SrsConfDirective& dir = *conf.directives.at(0);
    EXPECT_STREQ("winlin", dir.name.c_str());
    EXPECT_EQ(2, (int)dir.args.size());
    EXPECT_STREQ("arg0", dir.arg0().c_str());
    EXPECT_STREQ("arg1", dir.arg1().c_str());
    EXPECT_EQ(0, (int)dir.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseNameArg2Only)
{
    MockSrsConfigBuffer buf("winlin arg0 arg1 arg2;");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());
    
    SrsConfDirective& dir = *conf.directives.at(0);
    EXPECT_STREQ("winlin", dir.name.c_str());
    EXPECT_EQ(3, (int)dir.args.size());
    EXPECT_STREQ("arg0", dir.arg0().c_str());
    EXPECT_STREQ("arg1", dir.arg1().c_str());
    EXPECT_STREQ("arg2", dir.arg2().c_str());
    EXPECT_EQ(0, (int)dir.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseNameArg2Dir0)
{
    MockSrsConfigBuffer buf("winlin arg0 arg1 arg2 {dir0;}");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());
    
    SrsConfDirective& dir = *conf.directives.at(0);
    EXPECT_STREQ("winlin", dir.name.c_str());
    EXPECT_EQ(3, (int)dir.args.size());
    EXPECT_STREQ("arg0", dir.arg0().c_str());
    EXPECT_STREQ("arg1", dir.arg1().c_str());
    EXPECT_STREQ("arg2", dir.arg2().c_str());
    EXPECT_EQ(1, (int)dir.directives.size());
    
    SrsConfDirective& dir0 = *dir.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(0, (int)dir0.args.size());
    EXPECT_EQ(0, (int)dir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseNameArg2Dir0NoEmpty)
{
    MockSrsConfigBuffer buf("winlin arg0 arg1 arg2{dir0;}");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());
    
    SrsConfDirective& dir = *conf.directives.at(0);
    EXPECT_STREQ("winlin", dir.name.c_str());
    EXPECT_EQ(3, (int)dir.args.size());
    EXPECT_STREQ("arg0", dir.arg0().c_str());
    EXPECT_STREQ("arg1", dir.arg1().c_str());
    EXPECT_STREQ("arg2", dir.arg2().c_str());
    EXPECT_EQ(1, (int)dir.directives.size());
    
    SrsConfDirective& dir0 = *dir.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(0, (int)dir0.args.size());
    EXPECT_EQ(0, (int)dir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseNameArg2_Dir0Arg0)
{
    MockSrsConfigBuffer buf("winlin arg0 arg1 arg2 {dir0 dir_arg0;}");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());
    
    SrsConfDirective& dir = *conf.directives.at(0);
    EXPECT_STREQ("winlin", dir.name.c_str());
    EXPECT_EQ(3, (int)dir.args.size());
    EXPECT_STREQ("arg0", dir.arg0().c_str());
    EXPECT_STREQ("arg1", dir.arg1().c_str());
    EXPECT_STREQ("arg2", dir.arg2().c_str());
    EXPECT_EQ(1, (int)dir.directives.size());
    
    SrsConfDirective& dir0 = *dir.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(1, (int)dir0.args.size());
    EXPECT_STREQ("dir_arg0", dir0.arg0().c_str());
    EXPECT_EQ(0, (int)dir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseNameArg2_Dir0Arg0_Dir0)
{
    MockSrsConfigBuffer buf("winlin arg0 arg1 arg2 {dir0 dir_arg0 {ddir0;}}");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());
    
    SrsConfDirective& dir = *conf.directives.at(0);
    EXPECT_STREQ("winlin", dir.name.c_str());
    EXPECT_EQ(3, (int)dir.args.size());
    EXPECT_STREQ("arg0", dir.arg0().c_str());
    EXPECT_STREQ("arg1", dir.arg1().c_str());
    EXPECT_STREQ("arg2", dir.arg2().c_str());
    EXPECT_EQ(1, (int)dir.directives.size());
    
    SrsConfDirective& dir0 = *dir.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(1, (int)dir0.args.size());
    EXPECT_STREQ("dir_arg0", dir0.arg0().c_str());
    EXPECT_EQ(1, (int)dir0.directives.size());
    
    SrsConfDirective& ddir0 = *dir0.directives.at(0);
    EXPECT_STREQ("ddir0", ddir0.name.c_str());
    EXPECT_EQ(0, (int)ddir0.args.size());
    EXPECT_EQ(0, (int)ddir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseNameArg2_Dir0Arg0_Dir0Arg0)
{
    MockSrsConfigBuffer buf("winlin arg0 arg1 arg2 {dir0 dir_arg0 {ddir0 ddir_arg0;}}");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());
    
    SrsConfDirective& dir = *conf.directives.at(0);
    EXPECT_STREQ("winlin", dir.name.c_str());
    EXPECT_EQ(3, (int)dir.args.size());
    EXPECT_STREQ("arg0", dir.arg0().c_str());
    EXPECT_STREQ("arg1", dir.arg1().c_str());
    EXPECT_STREQ("arg2", dir.arg2().c_str());
    EXPECT_EQ(1, (int)dir.directives.size());
    
    SrsConfDirective& dir0 = *dir.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(1, (int)dir0.args.size());
    EXPECT_STREQ("dir_arg0", dir0.arg0().c_str());
    EXPECT_EQ(1, (int)dir0.directives.size());
    
    SrsConfDirective& ddir0 = *dir0.directives.at(0);
    EXPECT_STREQ("ddir0", ddir0.name.c_str());
    EXPECT_EQ(1, (int)ddir0.args.size());
    EXPECT_STREQ("ddir_arg0", ddir0.arg0().c_str());
    EXPECT_EQ(0, (int)ddir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, Parse2SingleDirs)
{
    MockSrsConfigBuffer buf("dir0 arg0;dir1 arg1;");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(2, (int)conf.directives.size());
    
    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(1, (int)dir0.args.size());
    EXPECT_STREQ("arg0", dir0.arg0().c_str());
    EXPECT_EQ(0, (int)dir0.directives.size());
    
    SrsConfDirective& dir1 = *conf.directives.at(1);
    EXPECT_STREQ("dir1", dir1.name.c_str());
    EXPECT_EQ(1, (int)dir1.args.size());
    EXPECT_STREQ("arg1", dir1.arg0().c_str());
    EXPECT_EQ(0, (int)dir1.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseSingleComplexDirs)
{
    MockSrsConfigBuffer buf("dir0 arg0;dir1 {dir2 arg2;}");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(2, (int)conf.directives.size());
    
    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(1, (int)dir0.args.size());
    EXPECT_STREQ("arg0", dir0.arg0().c_str());
    EXPECT_EQ(0, (int)dir0.directives.size());
    
    SrsConfDirective& dir1 = *conf.directives.at(1);
    EXPECT_STREQ("dir1", dir1.name.c_str());
    EXPECT_EQ(0, (int)dir1.args.size());
    EXPECT_EQ(1, (int)dir1.directives.size());
    
    SrsConfDirective& dir2 = *dir1.directives.at(0);
    EXPECT_STREQ("dir2", dir2.name.c_str());
    EXPECT_EQ(1, (int)dir2.args.size());
    EXPECT_STREQ("arg2", dir2.arg0().c_str());
    EXPECT_EQ(0, (int)dir2.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseStringArgs)
{
    MockSrsConfigBuffer buf("dir0 arg0 \"str_arg\" 100;");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());
    
    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(3, (int)dir0.args.size());
    EXPECT_STREQ("arg0", dir0.arg0().c_str());
    EXPECT_STREQ("str_arg", dir0.arg1().c_str());
    EXPECT_STREQ("100", dir0.arg2().c_str());
    EXPECT_EQ(0, (int)dir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseStringArgsWithSpace)
{
    MockSrsConfigBuffer buf("dir0 arg0 \"str_arg space\" 100;");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());
    
    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(3, (int)dir0.args.size());
    EXPECT_STREQ("arg0", dir0.arg0().c_str());
    EXPECT_STREQ("str_arg space", dir0.arg1().c_str());
    EXPECT_STREQ("100", dir0.arg2().c_str());
    EXPECT_EQ(0, (int)dir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseNumberArgs)
{
    MockSrsConfigBuffer buf("dir0 100 101 102;");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());
    
    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(3, (int)dir0.args.size());
    EXPECT_EQ(100, ::atoi(dir0.arg0().c_str()));
    EXPECT_EQ(101, ::atoi(dir0.arg1().c_str()));
    EXPECT_EQ(102, ::atoi(dir0.arg2().c_str()));
    EXPECT_EQ(0, (int)dir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseFloatArgs)
{
    MockSrsConfigBuffer buf("dir0 100.01 101.02 102.03;");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());
    
    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(3, (int)dir0.args.size());
    EXPECT_FLOAT_EQ(100.01, ::atof(dir0.arg0().c_str()));
    EXPECT_FLOAT_EQ(101.02, ::atof(dir0.arg1().c_str()));
    EXPECT_FLOAT_EQ(102.03, ::atof(dir0.arg2().c_str()));
    EXPECT_EQ(0, (int)dir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseComments)
{
    MockSrsConfigBuffer buf("#commnets\ndir0 arg0;\n#end-comments");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());
    
    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(1, (int)dir0.args.size());
    EXPECT_STREQ("arg0", dir0.arg0().c_str());
    EXPECT_EQ(0, (int)dir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseCommentsInline)
{
    MockSrsConfigBuffer buf("#commnets\ndir0 arg0;#inline comments\n#end-comments");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());
    
    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(1, (int)dir0.args.size());
    EXPECT_STREQ("arg0", dir0.arg0().c_str());
    EXPECT_EQ(0, (int)dir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseCommentsInlineWithSpace)
{
    MockSrsConfigBuffer buf(" #commnets\ndir0 arg0; #inline comments\n #end-comments");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());
    
    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(1, (int)dir0.args.size());
    EXPECT_STREQ("arg0", dir0.arg0().c_str());
    EXPECT_EQ(0, (int)dir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseCommentsInlinemixed)
{
    MockSrsConfigBuffer buf("#commnets\ndir0 arg0;#inline comments\n#end-comments\ndir1 arg1;");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(2, (int)conf.directives.size());
    
    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(1, (int)dir0.args.size());
    EXPECT_STREQ("arg0", dir0.arg0().c_str());
    EXPECT_EQ(0, (int)dir0.directives.size());
    
    SrsConfDirective& dir1 = *conf.directives.at(1);
    EXPECT_STREQ("dir1", dir1.name.c_str());
    EXPECT_EQ(1, (int)dir1.args.size());
    EXPECT_STREQ("arg1", dir1.arg0().c_str());
    EXPECT_EQ(0, (int)dir1.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseSpecialChars)
{
    MockSrsConfigBuffer buf("dir0 http://www.ossrs.net/api/v1/versions?level=major;");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());
    
    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(1, (int)dir0.args.size());
    EXPECT_STREQ("http://www.ossrs.net/api/v1/versions?level=major", dir0.arg0().c_str());
    EXPECT_EQ(0, (int)dir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseSpecialChars2)
{
    MockSrsConfigBuffer buf("dir0 rtmp://[server]:[port]/[app]/[stream]_[engine];");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());
    
    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(1, (int)dir0.args.size());
    EXPECT_STREQ("rtmp://[server]:[port]/[app]/[stream]_[engine]", dir0.arg0().c_str());
    EXPECT_EQ(0, (int)dir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseInvalidNoEndOfDirective)
{
    MockSrsConfigBuffer buf("dir0");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS != conf.parse(&buf));
}

VOID TEST(ConfigDirectiveTest, ParseInvalidNoEndOfSubDirective)
{
    MockSrsConfigBuffer buf("dir0 {");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS != conf.parse(&buf));
}

VOID TEST(ConfigDirectiveTest, ParseInvalidNoStartOfSubDirective)
{
    MockSrsConfigBuffer buf("dir0 }");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS != conf.parse(&buf));
}

VOID TEST(ConfigDirectiveTest, ParseInvalidEmptyName)
{
    MockSrsConfigBuffer buf(";");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS != conf.parse(&buf));
}

VOID TEST(ConfigDirectiveTest, ParseInvalidEmptyName2)
{
    MockSrsConfigBuffer buf("{}");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS != conf.parse(&buf));
}

VOID TEST(ConfigDirectiveTest, ParseInvalidEmptyDirective)
{
    MockSrsConfigBuffer buf("dir0 {}");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());
    
    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(0, (int)dir0.args.size());
    EXPECT_EQ(0, (int)dir0.directives.size());
}

VOID TEST(ConfigDirectiveTest, ParseLine)
{
    MockSrsConfigBuffer buf("dir0 {}");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());
    
    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(0, (int)dir0.args.size());
    EXPECT_EQ(0, (int)dir0.directives.size());
    EXPECT_EQ(1, (int)dir0.conf_line);
}

VOID TEST(ConfigDirectiveTest, ParseLine2)
{
    MockSrsConfigBuffer buf("\n\ndir0 {}");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());
    
    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(0, (int)dir0.args.size());
    EXPECT_EQ(0, (int)dir0.directives.size());
    EXPECT_EQ(3, (int)dir0.conf_line);
}

VOID TEST(ConfigDirectiveTest, ParseLine3)
{
    MockSrsConfigBuffer buf("dir0 {\n\ndir1 arg0;}");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());
    
    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(0, (int)dir0.args.size());
    EXPECT_EQ(1, (int)dir0.directives.size());
    EXPECT_EQ(1, (int)dir0.conf_line);
    
    SrsConfDirective& dir1 = *dir0.directives.at(0);
    EXPECT_STREQ("dir1", dir1.name.c_str());
    EXPECT_EQ(1, (int)dir1.args.size());
    EXPECT_STREQ("arg0", dir1.arg0().c_str());
    EXPECT_EQ(0, (int)dir1.directives.size());
    EXPECT_EQ(3, (int)dir1.conf_line);
}

VOID TEST(ConfigDirectiveTest, ParseLine4)
{
    MockSrsConfigBuffer buf("dir0 {\n\ndir1 \n\narg0;dir2 arg1;}");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());
    
    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(0, (int)dir0.args.size());
    EXPECT_EQ(2, (int)dir0.directives.size());
    EXPECT_EQ(1, (int)dir0.conf_line);
    
    SrsConfDirective& dir1 = *dir0.directives.at(0);
    EXPECT_STREQ("dir1", dir1.name.c_str());
    EXPECT_EQ(1, (int)dir1.args.size());
    EXPECT_STREQ("arg0", dir1.arg0().c_str());
    EXPECT_EQ(0, (int)dir1.directives.size());
    EXPECT_EQ(3, (int)dir1.conf_line);
    
    SrsConfDirective& dir2 = *dir0.directives.at(1);
    EXPECT_STREQ("dir2", dir2.name.c_str());
    EXPECT_EQ(1, (int)dir2.args.size());
    EXPECT_STREQ("arg1", dir2.arg0().c_str());
    EXPECT_EQ(0, (int)dir2.directives.size());
    EXPECT_EQ(5, (int)dir2.conf_line);
}

VOID TEST(ConfigDirectiveTest, ParseLineNormal)
{
    MockSrsConfigBuffer buf("dir0 {\ndir1 {\ndir2 arg2;\n}\n}");
    SrsConfDirective conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(&buf));
    EXPECT_EQ(0, (int)conf.name.length());
    EXPECT_EQ(0, (int)conf.args.size());
    EXPECT_EQ(1, (int)conf.directives.size());
    
    SrsConfDirective& dir0 = *conf.directives.at(0);
    EXPECT_STREQ("dir0", dir0.name.c_str());
    EXPECT_EQ(0, (int)dir0.args.size());
    EXPECT_EQ(1, (int)dir0.directives.size());
    EXPECT_EQ(1, (int)dir0.conf_line);
    
    SrsConfDirective& dir1 = *dir0.directives.at(0);
    EXPECT_STREQ("dir1", dir1.name.c_str());
    EXPECT_EQ(0, (int)dir1.args.size());
    EXPECT_EQ(1, (int)dir1.directives.size());
    EXPECT_EQ(2, (int)dir1.conf_line);
    
    SrsConfDirective& dir2 = *dir1.directives.at(0);
    EXPECT_STREQ("dir2", dir2.name.c_str());
    EXPECT_EQ(1, (int)dir2.args.size());
    EXPECT_STREQ("arg2", dir2.arg0().c_str());
    EXPECT_EQ(0, (int)dir2.directives.size());
    EXPECT_EQ(3, (int)dir2.conf_line);
}

VOID TEST(ConfigMainTest, ParseEmpty)
{
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS != conf.parse(""));
}

VOID TEST(ConfigMainTest, ParseMinConf)
{
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse("listen 1935;"));
    
    vector<string> listens = conf.get_listen();
    EXPECT_EQ(1, (int)listens.size());
    EXPECT_STREQ("1935", listens.at(0).c_str());
}

VOID TEST(ConfigMainTest, ParseInvalidDirective)
{
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS != conf.parse("listens 1935;"));
}

VOID TEST(ConfigMainTest, ParseInvalidDirective2)
{
    MockSrsConfig conf;
    // check error for user not specified the listen directive.
    EXPECT_TRUE(ERROR_SUCCESS != conf.parse("chunk_size 4096;"));
}

VOID TEST(ConfigMainTest, ParseFullConf)
{
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(__full_conf));
    
    vector<string> listens = conf.get_listen();
    EXPECT_EQ(1, (int)listens.size());
    EXPECT_STREQ("1935", listens.at(0).c_str());
    
    EXPECT_STREQ("./objs/srs.pid", conf.get_pid_file().c_str());
    EXPECT_EQ(60000, conf.get_chunk_size(""));
    EXPECT_STREQ("./objs", conf.get_ffmpeg_log_dir().c_str());
    EXPECT_TRUE(conf.get_log_tank_file());
    EXPECT_STREQ("trace", conf.get_log_level().c_str());
    EXPECT_STREQ("./objs/srs.log", conf.get_log_file().c_str());
    EXPECT_EQ(1000, conf.get_max_connections());
    EXPECT_TRUE(conf.get_deamon());
    
    EXPECT_TRUE(conf.get_heartbeat_enabled());
    EXPECT_EQ(9300, conf.get_heartbeat_interval());
    EXPECT_STREQ("http://127.0.0.1:8085/api/v1/servers", conf.get_heartbeat_url().c_str());
    EXPECT_STREQ("my-srs-device", conf.get_heartbeat_device_id().c_str());
    EXPECT_EQ(0, conf.get_heartbeat_device_index());
    EXPECT_FALSE(conf.get_heartbeat_summaries());
    
    EXPECT_TRUE(conf.get_http_api_enabled());
    EXPECT_EQ(1985, conf.get_http_api_listen());
    
    EXPECT_TRUE(conf.get_http_stream_enabled());
    EXPECT_EQ(8080, conf.get_http_stream_listen());
    EXPECT_STREQ("./objs/nginx/html", conf.get_http_stream_dir().c_str());
    
    EXPECT_EQ(10000, conf.get_pithy_print_publish());
    EXPECT_EQ(10000, conf.get_pithy_print_play());
    EXPECT_EQ(10000, conf.get_pithy_print_forwarder());
    EXPECT_EQ(10000, conf.get_pithy_print_encoder());
    EXPECT_EQ(10000, conf.get_pithy_print_ingester());
    EXPECT_EQ(10000, conf.get_pithy_print_hls());
    EXPECT_EQ(10000, conf.get_pithy_print_edge());
    
    EXPECT_TRUE(NULL != conf.get_vhost("__defaultVhost__"));
    EXPECT_TRUE(NULL != conf.get_vhost("same.edge.srs.com"));
    EXPECT_TRUE(NULL != conf.get_vhost("change.edge.srs.com"));
    EXPECT_TRUE(NULL != conf.get_vhost("dvr.srs.com"));
    EXPECT_TRUE(NULL != conf.get_vhost("ingest.srs.com"));
    EXPECT_TRUE(NULL != conf.get_vhost("http.srs.com"));
    EXPECT_TRUE(NULL != conf.get_vhost("with-hls.srs.com"));
    EXPECT_TRUE(NULL != conf.get_vhost("no-hls.srs.com"));
    EXPECT_TRUE(NULL != conf.get_vhost("hooks.callback.srs.com"));
    EXPECT_TRUE(NULL != conf.get_vhost("min.delay.com"));
    EXPECT_TRUE(NULL != conf.get_vhost("refer.anti_suck.com"));
    EXPECT_TRUE(NULL != conf.get_vhost("same.vhost.forward.srs.com"));
    EXPECT_TRUE(NULL != conf.get_vhost("change.vhost.forward.srs.com"));
    EXPECT_TRUE(NULL != conf.get_vhost("mirror.transcode.srs.com"));
    EXPECT_TRUE(NULL != conf.get_vhost("crop.transcode.srs.com"));
    EXPECT_TRUE(NULL != conf.get_vhost("logo.transcode.srs.com"));
    EXPECT_TRUE(NULL != conf.get_vhost("audio.transcode.srs.com"));
    EXPECT_TRUE(NULL != conf.get_vhost("vn.transcode.srs.com"));
    EXPECT_TRUE(NULL != conf.get_vhost("copy.transcode.srs.com"));
    EXPECT_TRUE(NULL != conf.get_vhost("all.transcode.srs.com"));
    EXPECT_TRUE(NULL != conf.get_vhost("ffempty.transcode.srs.com"));
    EXPECT_TRUE(NULL != conf.get_vhost("app.transcode.srs.com"));
    EXPECT_TRUE(NULL != conf.get_vhost("stream.transcode.srs.com"));
    EXPECT_TRUE(NULL != conf.get_vhost("bandcheck.srs.com"));
    EXPECT_TRUE(NULL != conf.get_vhost("chunksize.srs.com"));
    EXPECT_TRUE(NULL != conf.get_vhost("jitter.srs.com"));
    EXPECT_TRUE(NULL != conf.get_vhost("atc.srs.com"));
    EXPECT_TRUE(NULL != conf.get_vhost("removed.srs.com"));
    
    string vhost;
    
    ////////////////////////////////////////////////////////////////
    // default vhost template.
    ////////////////////////////////////////////////////////////////
    vhost = "__defaultVhost__";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_FALSE(conf.get_atc(vhost));
    EXPECT_TRUE(conf.get_atc_auto(vhost));
    EXPECT_TRUE(conf.get_time_jitter(vhost) == SrsRtmpJitterAlgorithmFULL);
    EXPECT_FLOAT_EQ(30, conf.get_queue_length(vhost));
    EXPECT_TRUE(NULL == conf.get_refer(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_play(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_publish(vhost));
    EXPECT_EQ(60000, conf.get_chunk_size(vhost));
    EXPECT_TRUE(NULL == conf.get_forward(vhost));
    EXPECT_FALSE(conf.get_vhost_http_hooks_enabled(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_connect(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_close(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_publish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_unpublish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_play(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_stop(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_dvr_hss_reap_flv(vhost));
    EXPECT_FALSE(conf.get_bw_check_enabled(vhost));
    EXPECT_TRUE(conf.get_bw_check_key(vhost).empty());
    EXPECT_EQ(30000, conf.get_bw_check_interval_ms(vhost));
    EXPECT_EQ(1000, conf.get_bw_check_limit_kbps(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(conf.get_vhost(vhost)));
    EXPECT_TRUE(NULL == conf.get_vhost_edge_origin(vhost));
    EXPECT_FALSE(conf.get_vhost_edge_token_traverse(vhost));
    EXPECT_TRUE(NULL == conf.get_transcode(vhost, ""));
    EXPECT_FALSE(conf.get_transcode_enabled(NULL));
    EXPECT_TRUE(conf.get_transcode_ffmpeg(NULL).empty());
    EXPECT_TRUE(conf.get_transcode_engines(NULL).size() == 0);
    EXPECT_FALSE(conf.get_engine_enabled(NULL));
    EXPECT_STREQ("flv", conf.get_engine_iformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_vfilter(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_vcodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vbitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vfps(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vwidth(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vheight(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vthreads(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vprofile(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vpreset(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vparams(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_acodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_abitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_asample_rate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_achannels(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_aparams(NULL).size() == 0);
    EXPECT_STREQ("flv", conf.get_engine_oformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_output(NULL).empty());
    EXPECT_TRUE(conf.get_ingesters(vhost).size() == 0);
    EXPECT_TRUE(NULL == conf.get_ingest_by_id(vhost, ""));
    EXPECT_FALSE(conf.get_ingest_enabled(NULL));
    EXPECT_TRUE(conf.get_ingest_ffmpeg(NULL).empty());
    EXPECT_STREQ("file", conf.get_ingest_input_type(NULL).c_str());
    EXPECT_TRUE(conf.get_ingest_input_url(NULL).empty());
    EXPECT_FALSE(conf.get_hls_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_hls_path(vhost).c_str());
    EXPECT_EQ(10, conf.get_hls_fragment(vhost));
    EXPECT_EQ(60, conf.get_hls_window(vhost));
    EXPECT_FALSE(conf.get_dvr_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_dvr_path(vhost).c_str());
    EXPECT_STREQ("session", conf.get_dvr_plan(vhost).c_str());
    EXPECT_EQ(30, conf.get_dvr_duration(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
    ////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////
}

VOID TEST(ConfigMainTest, ParseFullConf_same_edge)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(__full_conf));
    
    vhost = "same.edge.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_FALSE(conf.get_atc(vhost));
    EXPECT_TRUE(conf.get_atc_auto(vhost));
    EXPECT_TRUE(conf.get_time_jitter(vhost) == SrsRtmpJitterAlgorithmFULL);
    EXPECT_FLOAT_EQ(30, conf.get_queue_length(vhost));
    EXPECT_TRUE(NULL == conf.get_refer(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_play(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_publish(vhost));
    EXPECT_EQ(60000, conf.get_chunk_size(vhost));
    EXPECT_TRUE(NULL == conf.get_forward(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_connect(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_close(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_publish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_unpublish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_play(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_stop(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_dvr_hss_reap_flv(vhost));
    EXPECT_FALSE(conf.get_bw_check_enabled(vhost));
    EXPECT_TRUE(conf.get_bw_check_key(vhost).empty());
    EXPECT_EQ(30000, conf.get_bw_check_interval_ms(vhost));
    EXPECT_EQ(1000, conf.get_bw_check_limit_kbps(vhost));
    EXPECT_TRUE(conf.get_vhost_is_edge(vhost));
    EXPECT_TRUE(conf.get_vhost_is_edge(conf.get_vhost(vhost)));
    EXPECT_TRUE(NULL != conf.get_vhost_edge_origin(vhost));
    if (true) {
        SrsConfDirective* edge = conf.get_vhost_edge_origin(vhost);
        EXPECT_STREQ("127.0.0.1:1935", edge->arg0().c_str());
        EXPECT_STREQ("localhost:1935", edge->arg1().c_str());
    }
    EXPECT_FALSE(conf.get_vhost_edge_token_traverse(vhost));
    EXPECT_TRUE(NULL == conf.get_transcode(vhost, ""));
    EXPECT_FALSE(conf.get_transcode_enabled(NULL));
    EXPECT_TRUE(conf.get_transcode_ffmpeg(NULL).empty());
    EXPECT_TRUE(conf.get_transcode_engines(NULL).size() == 0);
    EXPECT_FALSE(conf.get_engine_enabled(NULL));
    EXPECT_STREQ("flv", conf.get_engine_iformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_vfilter(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_vcodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vbitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vfps(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vwidth(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vheight(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vthreads(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vprofile(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vpreset(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vparams(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_acodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_abitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_asample_rate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_achannels(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_aparams(NULL).size() == 0);
    EXPECT_STREQ("flv", conf.get_engine_oformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_output(NULL).empty());
    EXPECT_TRUE(conf.get_ingesters(vhost).size() == 0);
    EXPECT_TRUE(NULL == conf.get_ingest_by_id(vhost, ""));
    EXPECT_FALSE(conf.get_ingest_enabled(NULL));
    EXPECT_TRUE(conf.get_ingest_ffmpeg(NULL).empty());
    EXPECT_STREQ("file", conf.get_ingest_input_type(NULL).c_str());
    EXPECT_TRUE(conf.get_ingest_input_url(NULL).empty());
    EXPECT_FALSE(conf.get_hls_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_hls_path(vhost).c_str());
    EXPECT_EQ(10, conf.get_hls_fragment(vhost));
    EXPECT_EQ(60, conf.get_hls_window(vhost));
    EXPECT_FALSE(conf.get_dvr_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_dvr_path(vhost).c_str());
    EXPECT_STREQ("session", conf.get_dvr_plan(vhost).c_str());
    EXPECT_EQ(30, conf.get_dvr_duration(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
}

VOID TEST(ConfigMainTest, ParseFullConf_change_edge)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(__full_conf));
    
    vhost = "change.edge.srs.com";
    // TODO: FIXME: implements it.
    /*EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_FALSE(conf.get_atc(vhost));
    EXPECT_TRUE(conf.get_atc_auto(vhost));
    EXPECT_TRUE(conf.get_time_jitter(vhost) == SrsRtmpJitterAlgorithmFULL);
    EXPECT_FLOAT_EQ(30, conf.get_queue_length(vhost));
    EXPECT_TRUE(NULL == conf.get_refer(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_play(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_publish(vhost));
    EXPECT_EQ(60000, conf.get_chunk_size(vhost));
    EXPECT_TRUE(NULL == conf.get_forward(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_connect(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_close(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_publish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_unpublish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_play(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_stop(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_dvr_hss_reap_flv(vhost));
    EXPECT_FALSE(conf.get_bw_check_enabled(vhost));
    EXPECT_TRUE(conf.get_bw_check_key(vhost).empty());
    EXPECT_EQ(30000, conf.get_bw_check_interval_ms(vhost));
    EXPECT_EQ(1000, conf.get_bw_check_limit_kbps(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(conf.get_vhost(vhost)));
    EXPECT_TRUE(NULL == conf.get_vhost_edge_origin(vhost));
    EXPECT_FALSE(conf.get_vhost_edge_token_traverse(vhost));
    EXPECT_TRUE(NULL == conf.get_transcode(vhost, ""));
    EXPECT_FALSE(conf.get_transcode_enabled(NULL));
    EXPECT_TRUE(conf.get_transcode_ffmpeg(NULL).empty());
    EXPECT_TRUE(conf.get_transcode_engines(NULL).size() == 0);
    EXPECT_FALSE(conf.get_engine_enabled(NULL));
    EXPECT_STREQ("flv", conf.get_engine_iformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_vfilter(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_vcodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vbitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vfps(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vwidth(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vheight(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vthreads(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vprofile(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vpreset(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vparams(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_acodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_abitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_asample_rate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_achannels(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_aparams(NULL).size() == 0);
    EXPECT_STREQ("flv", conf.get_engine_oformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_output(NULL).empty());
    EXPECT_TRUE(conf.get_ingesters(vhost).size() == 0);
    EXPECT_TRUE(NULL == conf.get_ingest_by_id(vhost, ""));
    EXPECT_FALSE(conf.get_ingest_enabled(NULL));
    EXPECT_TRUE(conf.get_ingest_ffmpeg(NULL).empty());
    EXPECT_STREQ("file", conf.get_ingest_input_type(NULL).c_str());
    EXPECT_TRUE(conf.get_ingest_input_url(NULL).empty());
    EXPECT_FALSE(conf.get_hls_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_hls_path(vhost).c_str());
    EXPECT_EQ(10, conf.get_hls_fragment(vhost));
    EXPECT_EQ(60, conf.get_hls_window(vhost));
    EXPECT_FALSE(conf.get_dvr_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_dvr_path(vhost).c_str());
    EXPECT_STREQ("session", conf.get_dvr_plan(vhost).c_str());
    EXPECT_EQ(30, conf.get_dvr_duration(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));*/
}

VOID TEST(ConfigMainTest, ParseFullConf_dvr)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(__full_conf));
    
    vhost = "dvr.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_FALSE(conf.get_atc(vhost));
    EXPECT_TRUE(conf.get_atc_auto(vhost));
    EXPECT_TRUE(conf.get_time_jitter(vhost) == SrsRtmpJitterAlgorithmFULL);
    EXPECT_FLOAT_EQ(30, conf.get_queue_length(vhost));
    EXPECT_TRUE(NULL == conf.get_refer(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_play(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_publish(vhost));
    EXPECT_EQ(60000, conf.get_chunk_size(vhost));
    EXPECT_TRUE(NULL == conf.get_forward(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_connect(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_close(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_publish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_unpublish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_play(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_stop(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_dvr_hss_reap_flv(vhost));
    EXPECT_FALSE(conf.get_bw_check_enabled(vhost));
    EXPECT_TRUE(conf.get_bw_check_key(vhost).empty());
    EXPECT_EQ(30000, conf.get_bw_check_interval_ms(vhost));
    EXPECT_EQ(1000, conf.get_bw_check_limit_kbps(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(conf.get_vhost(vhost)));
    EXPECT_TRUE(NULL == conf.get_vhost_edge_origin(vhost));
    EXPECT_FALSE(conf.get_vhost_edge_token_traverse(vhost));
    EXPECT_TRUE(NULL == conf.get_transcode(vhost, ""));
    EXPECT_FALSE(conf.get_transcode_enabled(NULL));
    EXPECT_TRUE(conf.get_transcode_ffmpeg(NULL).empty());
    EXPECT_TRUE(conf.get_transcode_engines(NULL).size() == 0);
    EXPECT_FALSE(conf.get_engine_enabled(NULL));
    EXPECT_STREQ("flv", conf.get_engine_iformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_vfilter(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_vcodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vbitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vfps(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vwidth(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vheight(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vthreads(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vprofile(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vpreset(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vparams(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_acodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_abitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_asample_rate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_achannels(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_aparams(NULL).size() == 0);
    EXPECT_STREQ("flv", conf.get_engine_oformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_output(NULL).empty());
    EXPECT_TRUE(conf.get_ingesters(vhost).size() == 0);
    EXPECT_TRUE(NULL == conf.get_ingest_by_id(vhost, ""));
    EXPECT_FALSE(conf.get_ingest_enabled(NULL));
    EXPECT_TRUE(conf.get_ingest_ffmpeg(NULL).empty());
    EXPECT_STREQ("file", conf.get_ingest_input_type(NULL).c_str());
    EXPECT_TRUE(conf.get_ingest_input_url(NULL).empty());
    EXPECT_FALSE(conf.get_hls_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_hls_path(vhost).c_str());
    EXPECT_EQ(10, conf.get_hls_fragment(vhost));
    EXPECT_EQ(60, conf.get_hls_window(vhost));
    EXPECT_TRUE(conf.get_dvr_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_dvr_path(vhost).c_str());
    EXPECT_STREQ("session", conf.get_dvr_plan(vhost).c_str());
    EXPECT_EQ(30, conf.get_dvr_duration(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
}

VOID TEST(ConfigMainTest, ParseFullConf_ingest)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(__full_conf));
    
    vhost = "ingest.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_FALSE(conf.get_atc(vhost));
    EXPECT_TRUE(conf.get_atc_auto(vhost));
    EXPECT_TRUE(conf.get_time_jitter(vhost) == SrsRtmpJitterAlgorithmFULL);
    EXPECT_FLOAT_EQ(30, conf.get_queue_length(vhost));
    EXPECT_TRUE(NULL == conf.get_refer(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_play(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_publish(vhost));
    EXPECT_EQ(60000, conf.get_chunk_size(vhost));
    EXPECT_TRUE(NULL == conf.get_forward(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_connect(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_close(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_publish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_unpublish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_play(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_stop(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_dvr_hss_reap_flv(vhost));
    EXPECT_FALSE(conf.get_bw_check_enabled(vhost));
    EXPECT_TRUE(conf.get_bw_check_key(vhost).empty());
    EXPECT_EQ(30000, conf.get_bw_check_interval_ms(vhost));
    EXPECT_EQ(1000, conf.get_bw_check_limit_kbps(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(conf.get_vhost(vhost)));
    EXPECT_TRUE(NULL == conf.get_vhost_edge_origin(vhost));
    EXPECT_FALSE(conf.get_vhost_edge_token_traverse(vhost));
    EXPECT_TRUE(NULL == conf.get_transcode(vhost, ""));
    EXPECT_FALSE(conf.get_transcode_enabled(NULL));
    EXPECT_TRUE(conf.get_transcode_ffmpeg(NULL).empty());
    EXPECT_TRUE(conf.get_transcode_engines(NULL).size() == 0);
    EXPECT_FALSE(conf.get_engine_enabled(NULL));
    EXPECT_STREQ("flv", conf.get_engine_iformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_vfilter(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_vcodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vbitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vfps(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vwidth(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vheight(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vthreads(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vprofile(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vpreset(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vparams(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_acodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_abitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_asample_rate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_achannels(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_aparams(NULL).size() == 0);
    EXPECT_STREQ("flv", conf.get_engine_oformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_output(NULL).empty());
    ASSERT_TRUE(conf.get_ingesters(vhost).size() == 1);
    if (true) {
        SrsConfDirective* ingest = conf.get_ingesters(vhost).at(0);
        EXPECT_STREQ("livestream", ingest->arg0().c_str());
        
        EXPECT_TRUE(ingest == conf.get_ingest_by_id(vhost, "livestream"));
        EXPECT_TRUE(conf.get_ingest_enabled(ingest));
        EXPECT_STREQ(
            "./objs/ffmpeg/bin/ffmpeg", 
            conf.get_ingest_ffmpeg(ingest).c_str()
        );
        EXPECT_STREQ("file", conf.get_ingest_input_type(ingest).c_str());
        EXPECT_STREQ(
            "./doc/source.200kbps.768x320.flv", 
            conf.get_ingest_input_url(ingest).c_str()
        );
        
        vector<SrsConfDirective*> engines = conf.get_transcode_engines(ingest);
        ASSERT_EQ(1, (int)engines.size());
        
        SrsConfDirective* transcode = engines.at(0);
        EXPECT_FALSE(conf.get_transcode_enabled(transcode));
        EXPECT_STREQ(
            "rtmp://127.0.0.1:[port]/live?vhost=[vhost]/livestream", 
            conf.get_engine_output(transcode).c_str()
        );
    }
    EXPECT_FALSE(conf.get_hls_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_hls_path(vhost).c_str());
    EXPECT_EQ(10, conf.get_hls_fragment(vhost));
    EXPECT_EQ(60, conf.get_hls_window(vhost));
    EXPECT_FALSE(conf.get_dvr_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_dvr_path(vhost).c_str());
    EXPECT_STREQ("session", conf.get_dvr_plan(vhost).c_str());
    EXPECT_EQ(30, conf.get_dvr_duration(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
}

VOID TEST(ConfigMainTest, ParseFullConf_http)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(__full_conf));
    
    vhost = "http.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_FALSE(conf.get_atc(vhost));
    EXPECT_TRUE(conf.get_atc_auto(vhost));
    EXPECT_TRUE(conf.get_time_jitter(vhost) == SrsRtmpJitterAlgorithmFULL);
    EXPECT_FLOAT_EQ(30, conf.get_queue_length(vhost));
    EXPECT_TRUE(NULL == conf.get_refer(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_play(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_publish(vhost));
    EXPECT_EQ(60000, conf.get_chunk_size(vhost));
    EXPECT_TRUE(NULL == conf.get_forward(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_connect(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_close(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_publish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_unpublish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_play(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_stop(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_dvr_hss_reap_flv(vhost));
    EXPECT_FALSE(conf.get_bw_check_enabled(vhost));
    EXPECT_TRUE(conf.get_bw_check_key(vhost).empty());
    EXPECT_EQ(30000, conf.get_bw_check_interval_ms(vhost));
    EXPECT_EQ(1000, conf.get_bw_check_limit_kbps(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(conf.get_vhost(vhost)));
    EXPECT_TRUE(NULL == conf.get_vhost_edge_origin(vhost));
    EXPECT_FALSE(conf.get_vhost_edge_token_traverse(vhost));
    EXPECT_TRUE(NULL == conf.get_transcode(vhost, ""));
    EXPECT_FALSE(conf.get_transcode_enabled(NULL));
    EXPECT_TRUE(conf.get_transcode_ffmpeg(NULL).empty());
    EXPECT_TRUE(conf.get_transcode_engines(NULL).size() == 0);
    EXPECT_FALSE(conf.get_engine_enabled(NULL));
    EXPECT_STREQ("flv", conf.get_engine_iformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_vfilter(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_vcodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vbitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vfps(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vwidth(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vheight(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vthreads(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vprofile(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vpreset(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vparams(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_acodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_abitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_asample_rate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_achannels(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_aparams(NULL).size() == 0);
    EXPECT_STREQ("flv", conf.get_engine_oformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_output(NULL).empty());
    EXPECT_TRUE(conf.get_ingesters(vhost).size() == 0);
    EXPECT_TRUE(NULL == conf.get_ingest_by_id(vhost, ""));
    EXPECT_FALSE(conf.get_ingest_enabled(NULL));
    EXPECT_TRUE(conf.get_ingest_ffmpeg(NULL).empty());
    EXPECT_STREQ("file", conf.get_ingest_input_type(NULL).c_str());
    EXPECT_TRUE(conf.get_ingest_input_url(NULL).empty());
    EXPECT_FALSE(conf.get_hls_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_hls_path(vhost).c_str());
    EXPECT_EQ(10, conf.get_hls_fragment(vhost));
    EXPECT_EQ(60, conf.get_hls_window(vhost));
    EXPECT_FALSE(conf.get_dvr_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_dvr_path(vhost).c_str());
    EXPECT_STREQ("session", conf.get_dvr_plan(vhost).c_str());
    EXPECT_EQ(30, conf.get_dvr_duration(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_TRUE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("/hls", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html/hls", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_hls_enabled)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(__full_conf));
    
    vhost = "with-hls.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_FALSE(conf.get_atc(vhost));
    EXPECT_TRUE(conf.get_atc_auto(vhost));
    EXPECT_TRUE(conf.get_time_jitter(vhost) == SrsRtmpJitterAlgorithmFULL);
    EXPECT_FLOAT_EQ(30, conf.get_queue_length(vhost));
    EXPECT_TRUE(NULL == conf.get_refer(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_play(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_publish(vhost));
    EXPECT_EQ(60000, conf.get_chunk_size(vhost));
    EXPECT_TRUE(NULL == conf.get_forward(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_connect(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_close(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_publish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_unpublish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_play(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_stop(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_dvr_hss_reap_flv(vhost));
    EXPECT_FALSE(conf.get_bw_check_enabled(vhost));
    EXPECT_TRUE(conf.get_bw_check_key(vhost).empty());
    EXPECT_EQ(30000, conf.get_bw_check_interval_ms(vhost));
    EXPECT_EQ(1000, conf.get_bw_check_limit_kbps(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(conf.get_vhost(vhost)));
    EXPECT_TRUE(NULL == conf.get_vhost_edge_origin(vhost));
    EXPECT_FALSE(conf.get_vhost_edge_token_traverse(vhost));
    EXPECT_TRUE(NULL == conf.get_transcode(vhost, ""));
    EXPECT_FALSE(conf.get_transcode_enabled(NULL));
    EXPECT_TRUE(conf.get_transcode_ffmpeg(NULL).empty());
    EXPECT_TRUE(conf.get_transcode_engines(NULL).size() == 0);
    EXPECT_FALSE(conf.get_engine_enabled(NULL));
    EXPECT_STREQ("flv", conf.get_engine_iformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_vfilter(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_vcodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vbitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vfps(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vwidth(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vheight(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vthreads(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vprofile(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vpreset(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vparams(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_acodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_abitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_asample_rate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_achannels(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_aparams(NULL).size() == 0);
    EXPECT_STREQ("flv", conf.get_engine_oformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_output(NULL).empty());
    EXPECT_TRUE(conf.get_ingesters(vhost).size() == 0);
    EXPECT_TRUE(NULL == conf.get_ingest_by_id(vhost, ""));
    EXPECT_FALSE(conf.get_ingest_enabled(NULL));
    EXPECT_TRUE(conf.get_ingest_ffmpeg(NULL).empty());
    EXPECT_STREQ("file", conf.get_ingest_input_type(NULL).c_str());
    EXPECT_TRUE(conf.get_ingest_input_url(NULL).empty());
    EXPECT_TRUE(conf.get_hls_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_hls_path(vhost).c_str());
    EXPECT_EQ(10, conf.get_hls_fragment(vhost));
    EXPECT_EQ(60, conf.get_hls_window(vhost));
    EXPECT_FALSE(conf.get_dvr_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_dvr_path(vhost).c_str());
    EXPECT_STREQ("session", conf.get_dvr_plan(vhost).c_str());
    EXPECT_EQ(30, conf.get_dvr_duration(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_hls_disabled)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(__full_conf));
    
    vhost = "no-hls.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_FALSE(conf.get_atc(vhost));
    EXPECT_TRUE(conf.get_atc_auto(vhost));
    EXPECT_TRUE(conf.get_time_jitter(vhost) == SrsRtmpJitterAlgorithmFULL);
    EXPECT_FLOAT_EQ(30, conf.get_queue_length(vhost));
    EXPECT_TRUE(NULL == conf.get_refer(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_play(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_publish(vhost));
    EXPECT_EQ(60000, conf.get_chunk_size(vhost));
    EXPECT_TRUE(NULL == conf.get_forward(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_connect(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_close(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_publish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_unpublish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_play(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_stop(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_dvr_hss_reap_flv(vhost));
    EXPECT_FALSE(conf.get_bw_check_enabled(vhost));
    EXPECT_TRUE(conf.get_bw_check_key(vhost).empty());
    EXPECT_EQ(30000, conf.get_bw_check_interval_ms(vhost));
    EXPECT_EQ(1000, conf.get_bw_check_limit_kbps(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(conf.get_vhost(vhost)));
    EXPECT_TRUE(NULL == conf.get_vhost_edge_origin(vhost));
    EXPECT_FALSE(conf.get_vhost_edge_token_traverse(vhost));
    EXPECT_TRUE(NULL == conf.get_transcode(vhost, ""));
    EXPECT_FALSE(conf.get_transcode_enabled(NULL));
    EXPECT_TRUE(conf.get_transcode_ffmpeg(NULL).empty());
    EXPECT_TRUE(conf.get_transcode_engines(NULL).size() == 0);
    EXPECT_FALSE(conf.get_engine_enabled(NULL));
    EXPECT_STREQ("flv", conf.get_engine_iformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_vfilter(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_vcodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vbitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vfps(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vwidth(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vheight(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vthreads(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vprofile(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vpreset(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vparams(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_acodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_abitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_asample_rate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_achannels(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_aparams(NULL).size() == 0);
    EXPECT_STREQ("flv", conf.get_engine_oformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_output(NULL).empty());
    EXPECT_TRUE(conf.get_ingesters(vhost).size() == 0);
    EXPECT_TRUE(NULL == conf.get_ingest_by_id(vhost, ""));
    EXPECT_FALSE(conf.get_ingest_enabled(NULL));
    EXPECT_TRUE(conf.get_ingest_ffmpeg(NULL).empty());
    EXPECT_STREQ("file", conf.get_ingest_input_type(NULL).c_str());
    EXPECT_TRUE(conf.get_ingest_input_url(NULL).empty());
    EXPECT_FALSE(conf.get_hls_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_hls_path(vhost).c_str());
    EXPECT_EQ(10, conf.get_hls_fragment(vhost));
    EXPECT_EQ(60, conf.get_hls_window(vhost));
    EXPECT_FALSE(conf.get_dvr_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_dvr_path(vhost).c_str());
    EXPECT_STREQ("session", conf.get_dvr_plan(vhost).c_str());
    EXPECT_EQ(30, conf.get_dvr_duration(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_http_hooks)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(__full_conf));
    
    vhost = "hooks.callback.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_FALSE(conf.get_atc(vhost));
    EXPECT_TRUE(conf.get_atc_auto(vhost));
    EXPECT_TRUE(conf.get_time_jitter(vhost) == SrsRtmpJitterAlgorithmFULL);
    EXPECT_FLOAT_EQ(30, conf.get_queue_length(vhost));
    EXPECT_TRUE(NULL == conf.get_refer(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_play(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_publish(vhost));
    EXPECT_EQ(60000, conf.get_chunk_size(vhost));
    EXPECT_TRUE(NULL == conf.get_forward(vhost));
    EXPECT_TRUE(conf.get_vhost_http_hooks_enabled(vhost));
    EXPECT_TRUE(NULL != conf.get_vhost_on_connect(vhost));
    if (true) {
        SrsConfDirective* callback = conf.get_vhost_on_connect(vhost);
        EXPECT_STREQ("http://127.0.0.1:8085/api/v1/clients", callback->arg0().c_str());
        EXPECT_STREQ("http://localhost:8085/api/v1/clients", callback->arg1().c_str());
    }
    EXPECT_TRUE(NULL != conf.get_vhost_on_close(vhost));
    if (true) {
        SrsConfDirective* callback = conf.get_vhost_on_close(vhost);
        EXPECT_STREQ("http://127.0.0.1:8085/api/v1/clients", callback->arg0().c_str());
        EXPECT_STREQ("http://localhost:8085/api/v1/clients", callback->arg1().c_str());
    }
    EXPECT_TRUE(NULL != conf.get_vhost_on_publish(vhost));
    if (true) {
        SrsConfDirective* callback = conf.get_vhost_on_publish(vhost);
        EXPECT_STREQ("http://127.0.0.1:8085/api/v1/streams", callback->arg0().c_str());
        EXPECT_STREQ("http://localhost:8085/api/v1/streams", callback->arg1().c_str());
    }
    EXPECT_TRUE(NULL != conf.get_vhost_on_unpublish(vhost));
    if (true) {
        SrsConfDirective* callback = conf.get_vhost_on_unpublish(vhost);
        EXPECT_STREQ("http://127.0.0.1:8085/api/v1/streams", callback->arg0().c_str());
        EXPECT_STREQ("http://localhost:8085/api/v1/streams", callback->arg1().c_str());
    }
    EXPECT_TRUE(NULL != conf.get_vhost_on_play(vhost));
    if (true) {
        SrsConfDirective* callback = conf.get_vhost_on_play(vhost);
        EXPECT_STREQ("http://127.0.0.1:8085/api/v1/sessions", callback->arg0().c_str());
        EXPECT_STREQ("http://localhost:8085/api/v1/sessions", callback->arg1().c_str());
    }
    EXPECT_TRUE(NULL != conf.get_vhost_on_stop(vhost));
    if (true) {
        SrsConfDirective* callback = conf.get_vhost_on_stop(vhost);
        EXPECT_STREQ("http://127.0.0.1:8085/api/v1/sessions", callback->arg0().c_str());
        EXPECT_STREQ("http://localhost:8085/api/v1/sessions", callback->arg1().c_str());
    }
    EXPECT_TRUE(NULL != conf.get_vhost_on_dvr_hss_reap_flv(vhost));
    if (true) {
        SrsConfDirective* callback = conf.get_vhost_on_dvr_hss_reap_flv(vhost);
        EXPECT_STREQ("http://127.0.0.1:8085/api/v1/dvrs", callback->arg0().c_str());
        EXPECT_STREQ("http://localhost:8085/api/v1/dvrs", callback->arg1().c_str());
    }
    EXPECT_FALSE(conf.get_bw_check_enabled(vhost));
    EXPECT_TRUE(conf.get_bw_check_key(vhost).empty());
    EXPECT_EQ(30000, conf.get_bw_check_interval_ms(vhost));
    EXPECT_EQ(1000, conf.get_bw_check_limit_kbps(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(conf.get_vhost(vhost)));
    EXPECT_TRUE(NULL == conf.get_vhost_edge_origin(vhost));
    EXPECT_FALSE(conf.get_vhost_edge_token_traverse(vhost));
    EXPECT_TRUE(NULL == conf.get_transcode(vhost, ""));
    EXPECT_FALSE(conf.get_transcode_enabled(NULL));
    EXPECT_TRUE(conf.get_transcode_ffmpeg(NULL).empty());
    EXPECT_TRUE(conf.get_transcode_engines(NULL).size() == 0);
    EXPECT_FALSE(conf.get_engine_enabled(NULL));
    EXPECT_STREQ("flv", conf.get_engine_iformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_vfilter(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_vcodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vbitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vfps(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vwidth(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vheight(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vthreads(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vprofile(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vpreset(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vparams(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_acodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_abitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_asample_rate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_achannels(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_aparams(NULL).size() == 0);
    EXPECT_STREQ("flv", conf.get_engine_oformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_output(NULL).empty());
    EXPECT_TRUE(conf.get_ingesters(vhost).size() == 0);
    EXPECT_TRUE(NULL == conf.get_ingest_by_id(vhost, ""));
    EXPECT_FALSE(conf.get_ingest_enabled(NULL));
    EXPECT_TRUE(conf.get_ingest_ffmpeg(NULL).empty());
    EXPECT_STREQ("file", conf.get_ingest_input_type(NULL).c_str());
    EXPECT_TRUE(conf.get_ingest_input_url(NULL).empty());
    EXPECT_FALSE(conf.get_hls_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_hls_path(vhost).c_str());
    EXPECT_EQ(10, conf.get_hls_fragment(vhost));
    EXPECT_EQ(60, conf.get_hls_window(vhost));
    EXPECT_FALSE(conf.get_dvr_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_dvr_path(vhost).c_str());
    EXPECT_STREQ("session", conf.get_dvr_plan(vhost).c_str());
    EXPECT_EQ(30, conf.get_dvr_duration(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_min_delay)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(__full_conf));
    
    vhost = "min.delay.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_FALSE(conf.get_gop_cache(vhost));
    EXPECT_FALSE(conf.get_atc(vhost));
    EXPECT_TRUE(conf.get_atc_auto(vhost));
    EXPECT_TRUE(conf.get_time_jitter(vhost) == SrsRtmpJitterAlgorithmFULL);
    EXPECT_FLOAT_EQ(10, conf.get_queue_length(vhost));
    EXPECT_TRUE(NULL == conf.get_refer(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_play(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_publish(vhost));
    EXPECT_EQ(60000, conf.get_chunk_size(vhost));
    EXPECT_TRUE(NULL == conf.get_forward(vhost));
    EXPECT_FALSE(conf.get_vhost_http_hooks_enabled(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_connect(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_close(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_publish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_unpublish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_play(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_stop(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_dvr_hss_reap_flv(vhost));
    EXPECT_FALSE(conf.get_bw_check_enabled(vhost));
    EXPECT_TRUE(conf.get_bw_check_key(vhost).empty());
    EXPECT_EQ(30000, conf.get_bw_check_interval_ms(vhost));
    EXPECT_EQ(1000, conf.get_bw_check_limit_kbps(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(conf.get_vhost(vhost)));
    EXPECT_TRUE(NULL == conf.get_vhost_edge_origin(vhost));
    EXPECT_FALSE(conf.get_vhost_edge_token_traverse(vhost));
    EXPECT_TRUE(NULL == conf.get_transcode(vhost, ""));
    EXPECT_FALSE(conf.get_transcode_enabled(NULL));
    EXPECT_TRUE(conf.get_transcode_ffmpeg(NULL).empty());
    EXPECT_TRUE(conf.get_transcode_engines(NULL).size() == 0);
    EXPECT_FALSE(conf.get_engine_enabled(NULL));
    EXPECT_STREQ("flv", conf.get_engine_iformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_vfilter(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_vcodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vbitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vfps(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vwidth(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vheight(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vthreads(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vprofile(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vpreset(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vparams(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_acodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_abitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_asample_rate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_achannels(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_aparams(NULL).size() == 0);
    EXPECT_STREQ("flv", conf.get_engine_oformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_output(NULL).empty());
    EXPECT_TRUE(conf.get_ingesters(vhost).size() == 0);
    EXPECT_TRUE(NULL == conf.get_ingest_by_id(vhost, ""));
    EXPECT_FALSE(conf.get_ingest_enabled(NULL));
    EXPECT_TRUE(conf.get_ingest_ffmpeg(NULL).empty());
    EXPECT_STREQ("file", conf.get_ingest_input_type(NULL).c_str());
    EXPECT_TRUE(conf.get_ingest_input_url(NULL).empty());
    EXPECT_FALSE(conf.get_hls_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_hls_path(vhost).c_str());
    EXPECT_EQ(10, conf.get_hls_fragment(vhost));
    EXPECT_EQ(60, conf.get_hls_window(vhost));
    EXPECT_FALSE(conf.get_dvr_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_dvr_path(vhost).c_str());
    EXPECT_STREQ("session", conf.get_dvr_plan(vhost).c_str());
    EXPECT_EQ(30, conf.get_dvr_duration(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_refer_anti_suck)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(__full_conf));

    vhost = "refer.anti_suck.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_FALSE(conf.get_atc(vhost));
    EXPECT_TRUE(conf.get_atc_auto(vhost));
    EXPECT_TRUE(conf.get_time_jitter(vhost) == SrsRtmpJitterAlgorithmFULL);
    EXPECT_FLOAT_EQ(30, conf.get_queue_length(vhost));
    EXPECT_TRUE(NULL != conf.get_refer(vhost));
    if (true) {
        SrsConfDirective* refer = conf.get_refer(vhost);
        EXPECT_STREQ("github.com", refer->arg0().c_str());
        EXPECT_STREQ("github.io", refer->arg1().c_str());
    }
    EXPECT_TRUE(NULL != conf.get_refer_play(vhost));
    if (true) {
        SrsConfDirective* refer = conf.get_refer_play(vhost);
        EXPECT_STREQ("github.com", refer->arg0().c_str());
        EXPECT_STREQ("github.io", refer->arg1().c_str());
    }
    EXPECT_TRUE(NULL != conf.get_refer_publish(vhost));
    if (true) {
        SrsConfDirective* refer = conf.get_refer_publish(vhost);
        EXPECT_STREQ("github.com", refer->arg0().c_str());
        EXPECT_STREQ("github.io", refer->arg1().c_str());
    }
    EXPECT_EQ(60000, conf.get_chunk_size(vhost));
    EXPECT_TRUE(NULL == conf.get_forward(vhost));
    EXPECT_FALSE(conf.get_vhost_http_hooks_enabled(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_connect(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_close(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_publish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_unpublish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_play(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_stop(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_dvr_hss_reap_flv(vhost));
    EXPECT_FALSE(conf.get_bw_check_enabled(vhost));
    EXPECT_TRUE(conf.get_bw_check_key(vhost).empty());
    EXPECT_EQ(30000, conf.get_bw_check_interval_ms(vhost));
    EXPECT_EQ(1000, conf.get_bw_check_limit_kbps(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(conf.get_vhost(vhost)));
    EXPECT_TRUE(NULL == conf.get_vhost_edge_origin(vhost));
    EXPECT_FALSE(conf.get_vhost_edge_token_traverse(vhost));
    EXPECT_TRUE(NULL == conf.get_transcode(vhost, ""));
    EXPECT_FALSE(conf.get_transcode_enabled(NULL));
    EXPECT_TRUE(conf.get_transcode_ffmpeg(NULL).empty());
    EXPECT_TRUE(conf.get_transcode_engines(NULL).size() == 0);
    EXPECT_FALSE(conf.get_engine_enabled(NULL));
    EXPECT_STREQ("flv", conf.get_engine_iformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_vfilter(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_vcodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vbitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vfps(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vwidth(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vheight(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vthreads(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vprofile(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vpreset(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vparams(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_acodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_abitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_asample_rate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_achannels(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_aparams(NULL).size() == 0);
    EXPECT_STREQ("flv", conf.get_engine_oformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_output(NULL).empty());
    EXPECT_TRUE(conf.get_ingesters(vhost).size() == 0);
    EXPECT_TRUE(NULL == conf.get_ingest_by_id(vhost, ""));
    EXPECT_FALSE(conf.get_ingest_enabled(NULL));
    EXPECT_TRUE(conf.get_ingest_ffmpeg(NULL).empty());
    EXPECT_STREQ("file", conf.get_ingest_input_type(NULL).c_str());
    EXPECT_TRUE(conf.get_ingest_input_url(NULL).empty());
    EXPECT_FALSE(conf.get_hls_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_hls_path(vhost).c_str());
    EXPECT_EQ(10, conf.get_hls_fragment(vhost));
    EXPECT_EQ(60, conf.get_hls_window(vhost));
    EXPECT_FALSE(conf.get_dvr_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_dvr_path(vhost).c_str());
    EXPECT_STREQ("session", conf.get_dvr_plan(vhost).c_str());
    EXPECT_EQ(30, conf.get_dvr_duration(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_forward_same_vhost)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(__full_conf));

    vhost = "same.vhost.forward.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_FALSE(conf.get_atc(vhost));
    EXPECT_TRUE(conf.get_atc_auto(vhost));
    EXPECT_TRUE(conf.get_time_jitter(vhost) == SrsRtmpJitterAlgorithmFULL);
    EXPECT_FLOAT_EQ(30, conf.get_queue_length(vhost));
    EXPECT_TRUE(NULL == conf.get_refer(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_play(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_publish(vhost));
    EXPECT_EQ(60000, conf.get_chunk_size(vhost));
    EXPECT_TRUE(NULL != conf.get_forward(vhost));
    if (true) {
        SrsConfDirective* forward = conf.get_forward(vhost);
        EXPECT_STREQ("127.0.0.1:1936", forward->arg0().c_str());
        EXPECT_STREQ("127.0.0.1:1937", forward->arg1().c_str());
    }
    EXPECT_FALSE(conf.get_vhost_http_hooks_enabled(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_connect(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_close(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_publish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_unpublish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_play(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_stop(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_dvr_hss_reap_flv(vhost));
    EXPECT_FALSE(conf.get_bw_check_enabled(vhost));
    EXPECT_TRUE(conf.get_bw_check_key(vhost).empty());
    EXPECT_EQ(30000, conf.get_bw_check_interval_ms(vhost));
    EXPECT_EQ(1000, conf.get_bw_check_limit_kbps(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(conf.get_vhost(vhost)));
    EXPECT_TRUE(NULL == conf.get_vhost_edge_origin(vhost));
    EXPECT_FALSE(conf.get_vhost_edge_token_traverse(vhost));
    EXPECT_TRUE(NULL == conf.get_transcode(vhost, ""));
    EXPECT_FALSE(conf.get_transcode_enabled(NULL));
    EXPECT_TRUE(conf.get_transcode_ffmpeg(NULL).empty());
    EXPECT_TRUE(conf.get_transcode_engines(NULL).size() == 0);
    EXPECT_FALSE(conf.get_engine_enabled(NULL));
    EXPECT_STREQ("flv", conf.get_engine_iformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_vfilter(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_vcodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vbitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vfps(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vwidth(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vheight(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vthreads(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vprofile(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vpreset(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vparams(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_acodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_abitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_asample_rate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_achannels(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_aparams(NULL).size() == 0);
    EXPECT_STREQ("flv", conf.get_engine_oformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_output(NULL).empty());
    EXPECT_TRUE(conf.get_ingesters(vhost).size() == 0);
    EXPECT_TRUE(NULL == conf.get_ingest_by_id(vhost, ""));
    EXPECT_FALSE(conf.get_ingest_enabled(NULL));
    EXPECT_TRUE(conf.get_ingest_ffmpeg(NULL).empty());
    EXPECT_STREQ("file", conf.get_ingest_input_type(NULL).c_str());
    EXPECT_TRUE(conf.get_ingest_input_url(NULL).empty());
    EXPECT_FALSE(conf.get_hls_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_hls_path(vhost).c_str());
    EXPECT_EQ(10, conf.get_hls_fragment(vhost));
    EXPECT_EQ(60, conf.get_hls_window(vhost));
    EXPECT_FALSE(conf.get_dvr_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_dvr_path(vhost).c_str());
    EXPECT_STREQ("session", conf.get_dvr_plan(vhost).c_str());
    EXPECT_EQ(30, conf.get_dvr_duration(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_forward_change_vhost)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(__full_conf));

    vhost = "change.vhost.forward.srs.com";
    // TODO: FIXME: implements it.
    /*EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_FALSE(conf.get_atc(vhost));
    EXPECT_TRUE(conf.get_atc_auto(vhost));
    EXPECT_TRUE(conf.get_time_jitter(vhost) == SrsRtmpJitterAlgorithmFULL);
    EXPECT_FLOAT_EQ(30, conf.get_queue_length(vhost));
    EXPECT_TRUE(NULL == conf.get_refer(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_play(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_publish(vhost));
    EXPECT_EQ(60000, conf.get_chunk_size(vhost));
    EXPECT_TRUE(NULL == conf.get_forward(vhost));
    EXPECT_FALSE(conf.get_vhost_http_hooks_enabled(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_connect(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_close(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_publish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_unpublish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_play(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_stop(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_dvr_hss_reap_flv(vhost));
    EXPECT_FALSE(conf.get_bw_check_enabled(vhost));
    EXPECT_TRUE(conf.get_bw_check_key(vhost).empty());
    EXPECT_EQ(30000, conf.get_bw_check_interval_ms(vhost));
    EXPECT_EQ(1000, conf.get_bw_check_limit_kbps(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(conf.get_vhost(vhost)));
    EXPECT_TRUE(NULL == conf.get_vhost_edge_origin(vhost));
    EXPECT_FALSE(conf.get_vhost_edge_token_traverse(vhost));
    EXPECT_TRUE(NULL == conf.get_transcode(vhost, ""));
    EXPECT_FALSE(conf.get_transcode_enabled(NULL));
    EXPECT_TRUE(conf.get_transcode_ffmpeg(NULL).empty());
    EXPECT_TRUE(conf.get_transcode_engines(NULL).size() == 0);
    EXPECT_FALSE(conf.get_engine_enabled(NULL));
    EXPECT_STREQ("flv", conf.get_engine_iformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_vfilter(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_vcodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vbitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vfps(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vwidth(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vheight(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vthreads(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vprofile(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vpreset(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vparams(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_acodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_abitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_asample_rate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_achannels(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_aparams(NULL).size() == 0);
    EXPECT_STREQ("flv", conf.get_engine_oformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_output(NULL).empty());
    EXPECT_TRUE(conf.get_ingesters(vhost).size() == 0);
    EXPECT_TRUE(NULL == conf.get_ingest_by_id(vhost, ""));
    EXPECT_FALSE(conf.get_ingest_enabled(NULL));
    EXPECT_TRUE(conf.get_ingest_ffmpeg(NULL).empty());
    EXPECT_STREQ("file", conf.get_ingest_input_type(NULL).c_str());
    EXPECT_TRUE(conf.get_ingest_input_url(NULL).empty());
    EXPECT_FALSE(conf.get_hls_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_hls_path(vhost).c_str());
    EXPECT_EQ(10, conf.get_hls_fragment(vhost));
    EXPECT_EQ(60, conf.get_hls_window(vhost));
    EXPECT_FALSE(conf.get_dvr_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_dvr_path(vhost).c_str());
    EXPECT_STREQ("session", conf.get_dvr_plan(vhost).c_str());
    EXPECT_EQ(30, conf.get_dvr_duration(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());*/
}

VOID TEST(ConfigMainTest, ParseFullConf_transcode_mirror)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(__full_conf));

    vhost = "mirror.transcode.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_FALSE(conf.get_atc(vhost));
    EXPECT_TRUE(conf.get_atc_auto(vhost));
    EXPECT_TRUE(conf.get_time_jitter(vhost) == SrsRtmpJitterAlgorithmFULL);
    EXPECT_FLOAT_EQ(30, conf.get_queue_length(vhost));
    EXPECT_TRUE(NULL == conf.get_refer(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_play(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_publish(vhost));
    EXPECT_EQ(60000, conf.get_chunk_size(vhost));
    EXPECT_TRUE(NULL == conf.get_forward(vhost));
    EXPECT_FALSE(conf.get_vhost_http_hooks_enabled(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_connect(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_close(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_publish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_unpublish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_play(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_stop(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_dvr_hss_reap_flv(vhost));
    EXPECT_FALSE(conf.get_bw_check_enabled(vhost));
    EXPECT_TRUE(conf.get_bw_check_key(vhost).empty());
    EXPECT_EQ(30000, conf.get_bw_check_interval_ms(vhost));
    EXPECT_EQ(1000, conf.get_bw_check_limit_kbps(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(conf.get_vhost(vhost)));
    EXPECT_TRUE(NULL == conf.get_vhost_edge_origin(vhost));
    EXPECT_FALSE(conf.get_vhost_edge_token_traverse(vhost));
    EXPECT_TRUE(NULL != conf.get_transcode(vhost, ""));
    if (true) {
        SrsConfDirective* transcode = conf.get_transcode(vhost, "");
        
        EXPECT_TRUE(conf.get_transcode_enabled(transcode));
        EXPECT_STREQ("./objs/ffmpeg/bin/ffmpeg", conf.get_transcode_ffmpeg(transcode).c_str());
        
        vector<SrsConfDirective*> engines = conf.get_transcode_engines(transcode);
        ASSERT_TRUE((int)engines.size() != 0);
        
        SrsConfDirective* engine = engines.at(0);
        EXPECT_TRUE(conf.get_engine_enabled(engine));
        EXPECT_STREQ("flv", conf.get_engine_iformat(engine).c_str());
        EXPECT_TRUE((int)conf.get_engine_vfilter(engine).size() > 0);
        EXPECT_STREQ("libx264", conf.get_engine_vcodec(engine).c_str());
        EXPECT_EQ(300, conf.get_engine_vbitrate(engine));
        EXPECT_EQ(20, conf.get_engine_vfps(engine));
        EXPECT_EQ(768, conf.get_engine_vwidth(engine));
        EXPECT_EQ(320, conf.get_engine_vheight(engine));
        EXPECT_EQ(2, conf.get_engine_vthreads(engine));
        EXPECT_STREQ("baseline", conf.get_engine_vprofile(engine).c_str());
        EXPECT_STREQ("superfast", conf.get_engine_vpreset(engine).c_str());
        EXPECT_TRUE((int)conf.get_engine_vparams(engine).size() == 0);
        EXPECT_STREQ("libaacplus", conf.get_engine_acodec(engine).c_str());
        EXPECT_EQ(45, conf.get_engine_abitrate(engine));
        EXPECT_EQ(44100, conf.get_engine_asample_rate(engine));
        EXPECT_EQ(2, conf.get_engine_achannels(engine));
        EXPECT_TRUE((int)conf.get_engine_aparams(engine).size() == 0);
        EXPECT_STREQ("flv", conf.get_engine_oformat(engine).c_str());
        EXPECT_STREQ(
            "rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine]", 
            conf.get_engine_output(engine).c_str()
        );
    }
    EXPECT_TRUE(conf.get_ingesters(vhost).size() == 0);
    EXPECT_TRUE(NULL == conf.get_ingest_by_id(vhost, ""));
    EXPECT_FALSE(conf.get_ingest_enabled(NULL));
    EXPECT_TRUE(conf.get_ingest_ffmpeg(NULL).empty());
    EXPECT_STREQ("file", conf.get_ingest_input_type(NULL).c_str());
    EXPECT_TRUE(conf.get_ingest_input_url(NULL).empty());
    EXPECT_FALSE(conf.get_hls_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_hls_path(vhost).c_str());
    EXPECT_EQ(10, conf.get_hls_fragment(vhost));
    EXPECT_EQ(60, conf.get_hls_window(vhost));
    EXPECT_FALSE(conf.get_dvr_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_dvr_path(vhost).c_str());
    EXPECT_STREQ("session", conf.get_dvr_plan(vhost).c_str());
    EXPECT_EQ(30, conf.get_dvr_duration(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_transcode_crop)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(__full_conf));

    vhost = "crop.transcode.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_FALSE(conf.get_atc(vhost));
    EXPECT_TRUE(conf.get_atc_auto(vhost));
    EXPECT_TRUE(conf.get_time_jitter(vhost) == SrsRtmpJitterAlgorithmFULL);
    EXPECT_FLOAT_EQ(30, conf.get_queue_length(vhost));
    EXPECT_TRUE(NULL == conf.get_refer(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_play(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_publish(vhost));
    EXPECT_EQ(60000, conf.get_chunk_size(vhost));
    EXPECT_TRUE(NULL == conf.get_forward(vhost));
    EXPECT_FALSE(conf.get_vhost_http_hooks_enabled(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_connect(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_close(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_publish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_unpublish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_play(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_stop(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_dvr_hss_reap_flv(vhost));
    EXPECT_FALSE(conf.get_bw_check_enabled(vhost));
    EXPECT_TRUE(conf.get_bw_check_key(vhost).empty());
    EXPECT_EQ(30000, conf.get_bw_check_interval_ms(vhost));
    EXPECT_EQ(1000, conf.get_bw_check_limit_kbps(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(conf.get_vhost(vhost)));
    EXPECT_TRUE(NULL == conf.get_vhost_edge_origin(vhost));
    EXPECT_FALSE(conf.get_vhost_edge_token_traverse(vhost));
    EXPECT_TRUE(NULL != conf.get_transcode(vhost, ""));
    if (true) {
        SrsConfDirective* transcode = conf.get_transcode(vhost, "");
        
        EXPECT_TRUE(conf.get_transcode_enabled(transcode));
        EXPECT_STREQ("./objs/ffmpeg/bin/ffmpeg", conf.get_transcode_ffmpeg(transcode).c_str());
        
        vector<SrsConfDirective*> engines = conf.get_transcode_engines(transcode);
        ASSERT_TRUE((int)engines.size() != 0);
        
        SrsConfDirective* engine = engines.at(0);
        EXPECT_TRUE(conf.get_engine_enabled(engine));
        EXPECT_STREQ("flv", conf.get_engine_iformat(engine).c_str());
        EXPECT_TRUE((int)conf.get_engine_vfilter(engine).size() > 0);
        EXPECT_STREQ("libx264", conf.get_engine_vcodec(engine).c_str());
        EXPECT_EQ(300, conf.get_engine_vbitrate(engine));
        EXPECT_EQ(20, conf.get_engine_vfps(engine));
        EXPECT_EQ(768, conf.get_engine_vwidth(engine));
        EXPECT_EQ(320, conf.get_engine_vheight(engine));
        EXPECT_EQ(2, conf.get_engine_vthreads(engine));
        EXPECT_STREQ("baseline", conf.get_engine_vprofile(engine).c_str());
        EXPECT_STREQ("superfast", conf.get_engine_vpreset(engine).c_str());
        EXPECT_TRUE((int)conf.get_engine_vparams(engine).size() == 0);
        EXPECT_STREQ("libaacplus", conf.get_engine_acodec(engine).c_str());
        EXPECT_EQ(45, conf.get_engine_abitrate(engine));
        EXPECT_EQ(44100, conf.get_engine_asample_rate(engine));
        EXPECT_EQ(2, conf.get_engine_achannels(engine));
        EXPECT_TRUE((int)conf.get_engine_aparams(engine).size() == 0);
        EXPECT_STREQ("flv", conf.get_engine_oformat(engine).c_str());
        EXPECT_STREQ(
            "rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine]", 
            conf.get_engine_output(engine).c_str()
        );
    }
    EXPECT_TRUE(conf.get_ingesters(vhost).size() == 0);
    EXPECT_TRUE(NULL == conf.get_ingest_by_id(vhost, ""));
    EXPECT_FALSE(conf.get_ingest_enabled(NULL));
    EXPECT_TRUE(conf.get_ingest_ffmpeg(NULL).empty());
    EXPECT_STREQ("file", conf.get_ingest_input_type(NULL).c_str());
    EXPECT_TRUE(conf.get_ingest_input_url(NULL).empty());
    EXPECT_FALSE(conf.get_hls_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_hls_path(vhost).c_str());
    EXPECT_EQ(10, conf.get_hls_fragment(vhost));
    EXPECT_EQ(60, conf.get_hls_window(vhost));
    EXPECT_FALSE(conf.get_dvr_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_dvr_path(vhost).c_str());
    EXPECT_STREQ("session", conf.get_dvr_plan(vhost).c_str());
    EXPECT_EQ(30, conf.get_dvr_duration(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_transcode_logo)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(__full_conf));

    vhost = "logo.transcode.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_FALSE(conf.get_atc(vhost));
    EXPECT_TRUE(conf.get_atc_auto(vhost));
    EXPECT_TRUE(conf.get_time_jitter(vhost) == SrsRtmpJitterAlgorithmFULL);
    EXPECT_FLOAT_EQ(30, conf.get_queue_length(vhost));
    EXPECT_TRUE(NULL == conf.get_refer(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_play(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_publish(vhost));
    EXPECT_EQ(60000, conf.get_chunk_size(vhost));
    EXPECT_TRUE(NULL == conf.get_forward(vhost));
    EXPECT_FALSE(conf.get_vhost_http_hooks_enabled(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_connect(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_close(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_publish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_unpublish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_play(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_stop(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_dvr_hss_reap_flv(vhost));
    EXPECT_FALSE(conf.get_bw_check_enabled(vhost));
    EXPECT_TRUE(conf.get_bw_check_key(vhost).empty());
    EXPECT_EQ(30000, conf.get_bw_check_interval_ms(vhost));
    EXPECT_EQ(1000, conf.get_bw_check_limit_kbps(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(conf.get_vhost(vhost)));
    EXPECT_TRUE(NULL == conf.get_vhost_edge_origin(vhost));
    EXPECT_FALSE(conf.get_vhost_edge_token_traverse(vhost));
    EXPECT_TRUE(NULL != conf.get_transcode(vhost, ""));
    if (true) {
        SrsConfDirective* transcode = conf.get_transcode(vhost, "");
        
        EXPECT_TRUE(conf.get_transcode_enabled(transcode));
        EXPECT_STREQ("./objs/ffmpeg/bin/ffmpeg", conf.get_transcode_ffmpeg(transcode).c_str());
        
        vector<SrsConfDirective*> engines = conf.get_transcode_engines(transcode);
        ASSERT_TRUE((int)engines.size() != 0);
        
        SrsConfDirective* engine = engines.at(0);
        EXPECT_TRUE(conf.get_engine_enabled(engine));
        EXPECT_STREQ("flv", conf.get_engine_iformat(engine).c_str());
        EXPECT_TRUE((int)conf.get_engine_vfilter(engine).size() > 0);
        EXPECT_STREQ("libx264", conf.get_engine_vcodec(engine).c_str());
        EXPECT_EQ(300, conf.get_engine_vbitrate(engine));
        EXPECT_EQ(20, conf.get_engine_vfps(engine));
        EXPECT_EQ(768, conf.get_engine_vwidth(engine));
        EXPECT_EQ(320, conf.get_engine_vheight(engine));
        EXPECT_EQ(2, conf.get_engine_vthreads(engine));
        EXPECT_STREQ("baseline", conf.get_engine_vprofile(engine).c_str());
        EXPECT_STREQ("superfast", conf.get_engine_vpreset(engine).c_str());
        EXPECT_TRUE((int)conf.get_engine_vparams(engine).size() == 0);
        EXPECT_STREQ("libaacplus", conf.get_engine_acodec(engine).c_str());
        EXPECT_EQ(45, conf.get_engine_abitrate(engine));
        EXPECT_EQ(44100, conf.get_engine_asample_rate(engine));
        EXPECT_EQ(2, conf.get_engine_achannels(engine));
        EXPECT_TRUE((int)conf.get_engine_aparams(engine).size() == 0);
        EXPECT_STREQ("flv", conf.get_engine_oformat(engine).c_str());
        EXPECT_STREQ(
            "rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine]", 
            conf.get_engine_output(engine).c_str()
        );
    }
    EXPECT_TRUE(conf.get_ingesters(vhost).size() == 0);
    EXPECT_TRUE(NULL == conf.get_ingest_by_id(vhost, ""));
    EXPECT_FALSE(conf.get_ingest_enabled(NULL));
    EXPECT_TRUE(conf.get_ingest_ffmpeg(NULL).empty());
    EXPECT_STREQ("file", conf.get_ingest_input_type(NULL).c_str());
    EXPECT_TRUE(conf.get_ingest_input_url(NULL).empty());
    EXPECT_FALSE(conf.get_hls_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_hls_path(vhost).c_str());
    EXPECT_EQ(10, conf.get_hls_fragment(vhost));
    EXPECT_EQ(60, conf.get_hls_window(vhost));
    EXPECT_FALSE(conf.get_dvr_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_dvr_path(vhost).c_str());
    EXPECT_STREQ("session", conf.get_dvr_plan(vhost).c_str());
    EXPECT_EQ(30, conf.get_dvr_duration(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_transcode_audio)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(__full_conf));

    vhost = "audio.transcode.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_FALSE(conf.get_atc(vhost));
    EXPECT_TRUE(conf.get_atc_auto(vhost));
    EXPECT_TRUE(conf.get_time_jitter(vhost) == SrsRtmpJitterAlgorithmFULL);
    EXPECT_FLOAT_EQ(30, conf.get_queue_length(vhost));
    EXPECT_TRUE(NULL == conf.get_refer(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_play(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_publish(vhost));
    EXPECT_EQ(60000, conf.get_chunk_size(vhost));
    EXPECT_TRUE(NULL == conf.get_forward(vhost));
    EXPECT_FALSE(conf.get_vhost_http_hooks_enabled(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_connect(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_close(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_publish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_unpublish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_play(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_stop(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_dvr_hss_reap_flv(vhost));
    EXPECT_FALSE(conf.get_bw_check_enabled(vhost));
    EXPECT_TRUE(conf.get_bw_check_key(vhost).empty());
    EXPECT_EQ(30000, conf.get_bw_check_interval_ms(vhost));
    EXPECT_EQ(1000, conf.get_bw_check_limit_kbps(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(conf.get_vhost(vhost)));
    EXPECT_TRUE(NULL == conf.get_vhost_edge_origin(vhost));
    EXPECT_FALSE(conf.get_vhost_edge_token_traverse(vhost));
    EXPECT_TRUE(NULL != conf.get_transcode(vhost, ""));
    if (true) {
        SrsConfDirective* transcode = conf.get_transcode(vhost, "");
        
        EXPECT_TRUE(conf.get_transcode_enabled(transcode));
        EXPECT_STREQ("./objs/ffmpeg/bin/ffmpeg", conf.get_transcode_ffmpeg(transcode).c_str());
        
        vector<SrsConfDirective*> engines = conf.get_transcode_engines(transcode);
        ASSERT_TRUE((int)engines.size() != 0);
        
        if (true) {
            SrsConfDirective* engine = engines.at(0);
            EXPECT_TRUE(conf.get_engine_enabled(engine));
            EXPECT_STREQ("flv", conf.get_engine_iformat(engine).c_str());
            EXPECT_TRUE((int)conf.get_engine_vfilter(engine).size() == 0);
            EXPECT_STREQ("copy", conf.get_engine_vcodec(engine).c_str());
            EXPECT_STREQ("libaacplus", conf.get_engine_acodec(engine).c_str());
            EXPECT_EQ(45, conf.get_engine_abitrate(engine));
            EXPECT_EQ(44100, conf.get_engine_asample_rate(engine));
            EXPECT_EQ(2, conf.get_engine_achannels(engine));
            EXPECT_TRUE((int)conf.get_engine_aparams(engine).size() == 0);
            EXPECT_STREQ("flv", conf.get_engine_oformat(engine).c_str());
            EXPECT_STREQ(
                "rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine]", 
                conf.get_engine_output(engine).c_str()
            );
        }
    }
    EXPECT_TRUE(conf.get_ingesters(vhost).size() == 0);
    EXPECT_TRUE(NULL == conf.get_ingest_by_id(vhost, ""));
    EXPECT_FALSE(conf.get_ingest_enabled(NULL));
    EXPECT_TRUE(conf.get_ingest_ffmpeg(NULL).empty());
    EXPECT_STREQ("file", conf.get_ingest_input_type(NULL).c_str());
    EXPECT_TRUE(conf.get_ingest_input_url(NULL).empty());
    EXPECT_FALSE(conf.get_hls_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_hls_path(vhost).c_str());
    EXPECT_EQ(10, conf.get_hls_fragment(vhost));
    EXPECT_EQ(60, conf.get_hls_window(vhost));
    EXPECT_FALSE(conf.get_dvr_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_dvr_path(vhost).c_str());
    EXPECT_STREQ("session", conf.get_dvr_plan(vhost).c_str());
    EXPECT_EQ(30, conf.get_dvr_duration(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_transcode_vn)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(__full_conf));

    vhost = "vn.transcode.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_FALSE(conf.get_atc(vhost));
    EXPECT_TRUE(conf.get_atc_auto(vhost));
    EXPECT_TRUE(conf.get_time_jitter(vhost) == SrsRtmpJitterAlgorithmFULL);
    EXPECT_FLOAT_EQ(30, conf.get_queue_length(vhost));
    EXPECT_TRUE(NULL == conf.get_refer(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_play(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_publish(vhost));
    EXPECT_EQ(60000, conf.get_chunk_size(vhost));
    EXPECT_TRUE(NULL == conf.get_forward(vhost));
    EXPECT_FALSE(conf.get_vhost_http_hooks_enabled(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_connect(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_close(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_publish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_unpublish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_play(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_stop(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_dvr_hss_reap_flv(vhost));
    EXPECT_FALSE(conf.get_bw_check_enabled(vhost));
    EXPECT_TRUE(conf.get_bw_check_key(vhost).empty());
    EXPECT_EQ(30000, conf.get_bw_check_interval_ms(vhost));
    EXPECT_EQ(1000, conf.get_bw_check_limit_kbps(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(conf.get_vhost(vhost)));
    EXPECT_TRUE(NULL == conf.get_vhost_edge_origin(vhost));
    EXPECT_FALSE(conf.get_vhost_edge_token_traverse(vhost));
    EXPECT_TRUE(NULL != conf.get_transcode(vhost, ""));
    if (true) {
        SrsConfDirective* transcode = conf.get_transcode(vhost, "");
        
        EXPECT_TRUE(conf.get_transcode_enabled(transcode));
        EXPECT_STREQ("./objs/ffmpeg/bin/ffmpeg", conf.get_transcode_ffmpeg(transcode).c_str());
        
        vector<SrsConfDirective*> engines = conf.get_transcode_engines(transcode);
        ASSERT_TRUE((int)engines.size() != 0);
        
        if (true) {
            SrsConfDirective* engine = engines.at(0);
            EXPECT_TRUE(conf.get_engine_enabled(engine));
            EXPECT_STREQ("flv", conf.get_engine_iformat(engine).c_str());
            EXPECT_TRUE((int)conf.get_engine_vfilter(engine).size() == 0);
            EXPECT_STREQ("vn", conf.get_engine_vcodec(engine).c_str());
            EXPECT_STREQ("libaacplus", conf.get_engine_acodec(engine).c_str());
            EXPECT_EQ(45, conf.get_engine_abitrate(engine));
            EXPECT_EQ(44100, conf.get_engine_asample_rate(engine));
            EXPECT_EQ(2, conf.get_engine_achannels(engine));
            EXPECT_TRUE((int)conf.get_engine_aparams(engine).size() == 0);
            EXPECT_STREQ("flv", conf.get_engine_oformat(engine).c_str());
            EXPECT_STREQ(
                "rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine]", 
                conf.get_engine_output(engine).c_str()
            );
        }
    }
    EXPECT_TRUE(conf.get_ingesters(vhost).size() == 0);
    EXPECT_TRUE(NULL == conf.get_ingest_by_id(vhost, ""));
    EXPECT_FALSE(conf.get_ingest_enabled(NULL));
    EXPECT_TRUE(conf.get_ingest_ffmpeg(NULL).empty());
    EXPECT_STREQ("file", conf.get_ingest_input_type(NULL).c_str());
    EXPECT_TRUE(conf.get_ingest_input_url(NULL).empty());
    EXPECT_FALSE(conf.get_hls_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_hls_path(vhost).c_str());
    EXPECT_EQ(10, conf.get_hls_fragment(vhost));
    EXPECT_EQ(60, conf.get_hls_window(vhost));
    EXPECT_FALSE(conf.get_dvr_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_dvr_path(vhost).c_str());
    EXPECT_STREQ("session", conf.get_dvr_plan(vhost).c_str());
    EXPECT_EQ(30, conf.get_dvr_duration(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_transcode_copy)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(__full_conf));

    vhost = "copy.transcode.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_FALSE(conf.get_atc(vhost));
    EXPECT_TRUE(conf.get_atc_auto(vhost));
    EXPECT_TRUE(conf.get_time_jitter(vhost) == SrsRtmpJitterAlgorithmFULL);
    EXPECT_FLOAT_EQ(30, conf.get_queue_length(vhost));
    EXPECT_TRUE(NULL == conf.get_refer(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_play(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_publish(vhost));
    EXPECT_EQ(60000, conf.get_chunk_size(vhost));
    EXPECT_TRUE(NULL == conf.get_forward(vhost));
    EXPECT_FALSE(conf.get_vhost_http_hooks_enabled(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_connect(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_close(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_publish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_unpublish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_play(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_stop(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_dvr_hss_reap_flv(vhost));
    EXPECT_FALSE(conf.get_bw_check_enabled(vhost));
    EXPECT_TRUE(conf.get_bw_check_key(vhost).empty());
    EXPECT_EQ(30000, conf.get_bw_check_interval_ms(vhost));
    EXPECT_EQ(1000, conf.get_bw_check_limit_kbps(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(conf.get_vhost(vhost)));
    EXPECT_TRUE(NULL == conf.get_vhost_edge_origin(vhost));
    EXPECT_FALSE(conf.get_vhost_edge_token_traverse(vhost));
    EXPECT_TRUE(NULL != conf.get_transcode(vhost, ""));
    if (true) {
        SrsConfDirective* transcode = conf.get_transcode(vhost, "");
        
        EXPECT_TRUE(conf.get_transcode_enabled(transcode));
        EXPECT_STREQ("./objs/ffmpeg/bin/ffmpeg", conf.get_transcode_ffmpeg(transcode).c_str());
        
        vector<SrsConfDirective*> engines = conf.get_transcode_engines(transcode);
        ASSERT_TRUE((int)engines.size() != 0);
        
        if (true) {
            SrsConfDirective* engine = engines.at(0);
            EXPECT_TRUE(conf.get_engine_enabled(engine));
            EXPECT_STREQ("flv", conf.get_engine_iformat(engine).c_str());
            EXPECT_TRUE((int)conf.get_engine_vfilter(engine).size() == 0);
            EXPECT_STREQ("copy", conf.get_engine_vcodec(engine).c_str());
            EXPECT_STREQ("copy", conf.get_engine_acodec(engine).c_str());
            EXPECT_STREQ("flv", conf.get_engine_oformat(engine).c_str());
            EXPECT_STREQ(
                "rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine]", 
                conf.get_engine_output(engine).c_str()
            );
        }
    }
    EXPECT_TRUE(conf.get_ingesters(vhost).size() == 0);
    EXPECT_TRUE(NULL == conf.get_ingest_by_id(vhost, ""));
    EXPECT_FALSE(conf.get_ingest_enabled(NULL));
    EXPECT_TRUE(conf.get_ingest_ffmpeg(NULL).empty());
    EXPECT_STREQ("file", conf.get_ingest_input_type(NULL).c_str());
    EXPECT_TRUE(conf.get_ingest_input_url(NULL).empty());
    EXPECT_FALSE(conf.get_hls_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_hls_path(vhost).c_str());
    EXPECT_EQ(10, conf.get_hls_fragment(vhost));
    EXPECT_EQ(60, conf.get_hls_window(vhost));
    EXPECT_FALSE(conf.get_dvr_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_dvr_path(vhost).c_str());
    EXPECT_STREQ("session", conf.get_dvr_plan(vhost).c_str());
    EXPECT_EQ(30, conf.get_dvr_duration(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_transcode_all)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(__full_conf));

    vhost = "all.transcode.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_FALSE(conf.get_atc(vhost));
    EXPECT_TRUE(conf.get_atc_auto(vhost));
    EXPECT_TRUE(conf.get_time_jitter(vhost) == SrsRtmpJitterAlgorithmFULL);
    EXPECT_FLOAT_EQ(30, conf.get_queue_length(vhost));
    EXPECT_TRUE(NULL == conf.get_refer(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_play(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_publish(vhost));
    EXPECT_EQ(60000, conf.get_chunk_size(vhost));
    EXPECT_TRUE(NULL == conf.get_forward(vhost));
    EXPECT_FALSE(conf.get_vhost_http_hooks_enabled(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_connect(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_close(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_publish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_unpublish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_play(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_stop(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_dvr_hss_reap_flv(vhost));
    EXPECT_FALSE(conf.get_bw_check_enabled(vhost));
    EXPECT_TRUE(conf.get_bw_check_key(vhost).empty());
    EXPECT_EQ(30000, conf.get_bw_check_interval_ms(vhost));
    EXPECT_EQ(1000, conf.get_bw_check_limit_kbps(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(conf.get_vhost(vhost)));
    EXPECT_TRUE(NULL == conf.get_vhost_edge_origin(vhost));
    EXPECT_FALSE(conf.get_vhost_edge_token_traverse(vhost));
    EXPECT_TRUE(NULL != conf.get_transcode(vhost, ""));
    if (true) {
        SrsConfDirective* transcode = conf.get_transcode(vhost, "");
        
        EXPECT_TRUE(conf.get_transcode_enabled(transcode));
        EXPECT_STREQ("./objs/ffmpeg/bin/ffmpeg", conf.get_transcode_ffmpeg(transcode).c_str());
        
        vector<SrsConfDirective*> engines = conf.get_transcode_engines(transcode);
        ASSERT_TRUE((int)engines.size() != 0);
        
        if (true) {
            SrsConfDirective* engine = engines.at(0);
            EXPECT_TRUE(conf.get_engine_enabled(engine));
            EXPECT_STREQ("flv", conf.get_engine_iformat(engine).c_str());
            EXPECT_TRUE((int)conf.get_engine_vfilter(engine).size() > 0);
            EXPECT_STREQ("libx264", conf.get_engine_vcodec(engine).c_str());
            EXPECT_EQ(1500, conf.get_engine_vbitrate(engine));
            EXPECT_EQ(25, conf.get_engine_vfps(engine));
            EXPECT_EQ(768, conf.get_engine_vwidth(engine));
            EXPECT_EQ(320, conf.get_engine_vheight(engine));
            EXPECT_EQ(12, conf.get_engine_vthreads(engine));
            EXPECT_STREQ("main", conf.get_engine_vprofile(engine).c_str());
            EXPECT_STREQ("medium", conf.get_engine_vpreset(engine).c_str());
            EXPECT_TRUE((int)conf.get_engine_vparams(engine).size() > 0);
            EXPECT_STREQ("libaacplus", conf.get_engine_acodec(engine).c_str());
            EXPECT_EQ(70, conf.get_engine_abitrate(engine));
            EXPECT_EQ(44100, conf.get_engine_asample_rate(engine));
            EXPECT_EQ(2, conf.get_engine_achannels(engine));
            EXPECT_TRUE((int)conf.get_engine_aparams(engine).size() > 0);
            EXPECT_STREQ("flv", conf.get_engine_oformat(engine).c_str());
            EXPECT_STREQ(
                "rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine]", 
                conf.get_engine_output(engine).c_str()
            );
        }
        if (true) {
            SrsConfDirective* engine = engines.at(1);
            EXPECT_TRUE(conf.get_engine_enabled(engine));
            EXPECT_STREQ("flv", conf.get_engine_iformat(engine).c_str());
            EXPECT_TRUE((int)conf.get_engine_vfilter(engine).size() == 0);
            EXPECT_STREQ("libx264", conf.get_engine_vcodec(engine).c_str());
            EXPECT_EQ(1200, conf.get_engine_vbitrate(engine));
            EXPECT_EQ(25, conf.get_engine_vfps(engine));
            EXPECT_EQ(1382, conf.get_engine_vwidth(engine));
            EXPECT_EQ(576, conf.get_engine_vheight(engine));
            EXPECT_EQ(6, conf.get_engine_vthreads(engine));
            EXPECT_STREQ("main", conf.get_engine_vprofile(engine).c_str());
            EXPECT_STREQ("medium", conf.get_engine_vpreset(engine).c_str());
            EXPECT_TRUE((int)conf.get_engine_vparams(engine).size() == 0);
            EXPECT_STREQ("libaacplus", conf.get_engine_acodec(engine).c_str());
            EXPECT_EQ(70, conf.get_engine_abitrate(engine));
            EXPECT_EQ(44100, conf.get_engine_asample_rate(engine));
            EXPECT_EQ(2, conf.get_engine_achannels(engine));
            EXPECT_TRUE((int)conf.get_engine_aparams(engine).size() == 0);
            EXPECT_STREQ("flv", conf.get_engine_oformat(engine).c_str());
            EXPECT_STREQ(
                "rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine]", 
                conf.get_engine_output(engine).c_str()
            );
        }
        if (true) {
            SrsConfDirective* engine = engines.at(2);
            EXPECT_TRUE(conf.get_engine_enabled(engine));
            EXPECT_STREQ("flv", conf.get_engine_iformat(engine).c_str());
            EXPECT_TRUE((int)conf.get_engine_vfilter(engine).size() == 0);
            EXPECT_STREQ("libx264", conf.get_engine_vcodec(engine).c_str());
            EXPECT_EQ(800, conf.get_engine_vbitrate(engine));
            EXPECT_EQ(25, conf.get_engine_vfps(engine));
            EXPECT_EQ(1152, conf.get_engine_vwidth(engine));
            EXPECT_EQ(480, conf.get_engine_vheight(engine));
            EXPECT_EQ(4, conf.get_engine_vthreads(engine));
            EXPECT_STREQ("main", conf.get_engine_vprofile(engine).c_str());
            EXPECT_STREQ("fast", conf.get_engine_vpreset(engine).c_str());
            EXPECT_TRUE((int)conf.get_engine_vparams(engine).size() == 0);
            EXPECT_STREQ("libaacplus", conf.get_engine_acodec(engine).c_str());
            EXPECT_EQ(60, conf.get_engine_abitrate(engine));
            EXPECT_EQ(44100, conf.get_engine_asample_rate(engine));
            EXPECT_EQ(2, conf.get_engine_achannels(engine));
            EXPECT_TRUE((int)conf.get_engine_aparams(engine).size() == 0);
            EXPECT_STREQ("flv", conf.get_engine_oformat(engine).c_str());
            EXPECT_STREQ(
                "rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine]", 
                conf.get_engine_output(engine).c_str()
            );
        }
        if (true) {
            SrsConfDirective* engine = engines.at(3);
            EXPECT_TRUE(conf.get_engine_enabled(engine));
            EXPECT_STREQ("flv", conf.get_engine_iformat(engine).c_str());
            EXPECT_TRUE((int)conf.get_engine_vfilter(engine).size() == 0);
            EXPECT_STREQ("libx264", conf.get_engine_vcodec(engine).c_str());
            EXPECT_EQ(300, conf.get_engine_vbitrate(engine));
            EXPECT_EQ(20, conf.get_engine_vfps(engine));
            EXPECT_EQ(768, conf.get_engine_vwidth(engine));
            EXPECT_EQ(320, conf.get_engine_vheight(engine));
            EXPECT_EQ(2, conf.get_engine_vthreads(engine));
            EXPECT_STREQ("baseline", conf.get_engine_vprofile(engine).c_str());
            EXPECT_STREQ("superfast", conf.get_engine_vpreset(engine).c_str());
            EXPECT_TRUE((int)conf.get_engine_vparams(engine).size() == 0);
            EXPECT_STREQ("libaacplus", conf.get_engine_acodec(engine).c_str());
            EXPECT_EQ(45, conf.get_engine_abitrate(engine));
            EXPECT_EQ(44100, conf.get_engine_asample_rate(engine));
            EXPECT_EQ(2, conf.get_engine_achannels(engine));
            EXPECT_TRUE((int)conf.get_engine_aparams(engine).size() == 0);
            EXPECT_STREQ("flv", conf.get_engine_oformat(engine).c_str());
            EXPECT_STREQ(
                "rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine]", 
                conf.get_engine_output(engine).c_str()
            );
        }
        if (true) {
            SrsConfDirective* engine = engines.at(4);
            EXPECT_TRUE(conf.get_engine_enabled(engine));
            EXPECT_STREQ("flv", conf.get_engine_iformat(engine).c_str());
            EXPECT_TRUE((int)conf.get_engine_vfilter(engine).size() == 0);
            EXPECT_STREQ("copy", conf.get_engine_vcodec(engine).c_str());
            EXPECT_STREQ("libaacplus", conf.get_engine_acodec(engine).c_str());
            EXPECT_EQ(45, conf.get_engine_abitrate(engine));
            EXPECT_EQ(44100, conf.get_engine_asample_rate(engine));
            EXPECT_EQ(2, conf.get_engine_achannels(engine));
            EXPECT_TRUE((int)conf.get_engine_aparams(engine).size() == 0);
            EXPECT_STREQ("flv", conf.get_engine_oformat(engine).c_str());
            EXPECT_STREQ(
                "rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine]", 
                conf.get_engine_output(engine).c_str()
            );
        }
        if (true) {
            SrsConfDirective* engine = engines.at(5);
            EXPECT_TRUE(conf.get_engine_enabled(engine));
            EXPECT_STREQ("flv", conf.get_engine_iformat(engine).c_str());
            EXPECT_TRUE((int)conf.get_engine_vfilter(engine).size() == 0);
            EXPECT_STREQ("libx264", conf.get_engine_vcodec(engine).c_str());
            EXPECT_EQ(300, conf.get_engine_vbitrate(engine));
            EXPECT_EQ(20, conf.get_engine_vfps(engine));
            EXPECT_EQ(768, conf.get_engine_vwidth(engine));
            EXPECT_EQ(320, conf.get_engine_vheight(engine));
            EXPECT_EQ(2, conf.get_engine_vthreads(engine));
            EXPECT_STREQ("baseline", conf.get_engine_vprofile(engine).c_str());
            EXPECT_STREQ("superfast", conf.get_engine_vpreset(engine).c_str());
            EXPECT_TRUE((int)conf.get_engine_vparams(engine).size() == 0);
            EXPECT_STREQ("copy", conf.get_engine_acodec(engine).c_str());
            EXPECT_STREQ("flv", conf.get_engine_oformat(engine).c_str());
            EXPECT_STREQ(
                "rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine]", 
                conf.get_engine_output(engine).c_str()
            );
        }
        if (true) {
            SrsConfDirective* engine = engines.at(6);
            EXPECT_TRUE(conf.get_engine_enabled(engine));
            EXPECT_STREQ("flv", conf.get_engine_iformat(engine).c_str());
            EXPECT_TRUE((int)conf.get_engine_vfilter(engine).size() == 0);
            EXPECT_STREQ("copy", conf.get_engine_vcodec(engine).c_str());
            EXPECT_STREQ("copy", conf.get_engine_acodec(engine).c_str());
            EXPECT_STREQ("flv", conf.get_engine_oformat(engine).c_str());
            EXPECT_STREQ(
                "rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine]", 
                conf.get_engine_output(engine).c_str()
            );
        }
    }
    EXPECT_TRUE(conf.get_ingesters(vhost).size() == 0);
    EXPECT_TRUE(NULL == conf.get_ingest_by_id(vhost, ""));
    EXPECT_FALSE(conf.get_ingest_enabled(NULL));
    EXPECT_TRUE(conf.get_ingest_ffmpeg(NULL).empty());
    EXPECT_STREQ("file", conf.get_ingest_input_type(NULL).c_str());
    EXPECT_TRUE(conf.get_ingest_input_url(NULL).empty());
    EXPECT_FALSE(conf.get_hls_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_hls_path(vhost).c_str());
    EXPECT_EQ(10, conf.get_hls_fragment(vhost));
    EXPECT_EQ(60, conf.get_hls_window(vhost));
    EXPECT_FALSE(conf.get_dvr_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_dvr_path(vhost).c_str());
    EXPECT_STREQ("session", conf.get_dvr_plan(vhost).c_str());
    EXPECT_EQ(30, conf.get_dvr_duration(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_transcode_ffempty)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(__full_conf));

    vhost = "ffempty.transcode.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_FALSE(conf.get_atc(vhost));
    EXPECT_TRUE(conf.get_atc_auto(vhost));
    EXPECT_TRUE(conf.get_time_jitter(vhost) == SrsRtmpJitterAlgorithmFULL);
    EXPECT_FLOAT_EQ(30, conf.get_queue_length(vhost));
    EXPECT_TRUE(NULL == conf.get_refer(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_play(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_publish(vhost));
    EXPECT_EQ(60000, conf.get_chunk_size(vhost));
    EXPECT_TRUE(NULL == conf.get_forward(vhost));
    EXPECT_FALSE(conf.get_vhost_http_hooks_enabled(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_connect(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_close(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_publish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_unpublish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_play(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_stop(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_dvr_hss_reap_flv(vhost));
    EXPECT_FALSE(conf.get_bw_check_enabled(vhost));
    EXPECT_TRUE(conf.get_bw_check_key(vhost).empty());
    EXPECT_EQ(30000, conf.get_bw_check_interval_ms(vhost));
    EXPECT_EQ(1000, conf.get_bw_check_limit_kbps(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(conf.get_vhost(vhost)));
    EXPECT_TRUE(NULL == conf.get_vhost_edge_origin(vhost));
    EXPECT_FALSE(conf.get_vhost_edge_token_traverse(vhost));
    EXPECT_TRUE(NULL != conf.get_transcode(vhost, ""));
    if (true) {
        SrsConfDirective* transcode = conf.get_transcode(vhost, "");
        
        EXPECT_TRUE(conf.get_transcode_enabled(transcode));
        EXPECT_STREQ("./objs/research/ffempty", conf.get_transcode_ffmpeg(transcode).c_str());
        
        vector<SrsConfDirective*> engines = conf.get_transcode_engines(transcode);
        ASSERT_TRUE((int)engines.size() != 0);
        
        SrsConfDirective* engine = engines.at(0);
        EXPECT_TRUE(conf.get_engine_enabled(engine));
        EXPECT_STREQ("flv", conf.get_engine_iformat(engine).c_str());
        EXPECT_TRUE((int)conf.get_engine_vfilter(engine).size() == 0);
        EXPECT_STREQ("libx264", conf.get_engine_vcodec(engine).c_str());
        EXPECT_EQ(300, conf.get_engine_vbitrate(engine));
        EXPECT_EQ(20, conf.get_engine_vfps(engine));
        EXPECT_EQ(768, conf.get_engine_vwidth(engine));
        EXPECT_EQ(320, conf.get_engine_vheight(engine));
        EXPECT_EQ(2, conf.get_engine_vthreads(engine));
        EXPECT_STREQ("baseline", conf.get_engine_vprofile(engine).c_str());
        EXPECT_STREQ("superfast", conf.get_engine_vpreset(engine).c_str());
        EXPECT_TRUE((int)conf.get_engine_vparams(engine).size() == 0);
        EXPECT_STREQ("libaacplus", conf.get_engine_acodec(engine).c_str());
        EXPECT_EQ(45, conf.get_engine_abitrate(engine));
        EXPECT_EQ(44100, conf.get_engine_asample_rate(engine));
        EXPECT_EQ(2, conf.get_engine_achannels(engine));
        EXPECT_TRUE((int)conf.get_engine_aparams(engine).size() == 0);
        EXPECT_STREQ("flv", conf.get_engine_oformat(engine).c_str());
        EXPECT_STREQ(
            "rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine]", 
            conf.get_engine_output(engine).c_str()
        );
    }
    EXPECT_TRUE(conf.get_ingesters(vhost).size() == 0);
    EXPECT_TRUE(NULL == conf.get_ingest_by_id(vhost, ""));
    EXPECT_FALSE(conf.get_ingest_enabled(NULL));
    EXPECT_TRUE(conf.get_ingest_ffmpeg(NULL).empty());
    EXPECT_STREQ("file", conf.get_ingest_input_type(NULL).c_str());
    EXPECT_TRUE(conf.get_ingest_input_url(NULL).empty());
    EXPECT_FALSE(conf.get_hls_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_hls_path(vhost).c_str());
    EXPECT_EQ(10, conf.get_hls_fragment(vhost));
    EXPECT_EQ(60, conf.get_hls_window(vhost));
    EXPECT_FALSE(conf.get_dvr_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_dvr_path(vhost).c_str());
    EXPECT_STREQ("session", conf.get_dvr_plan(vhost).c_str());
    EXPECT_EQ(30, conf.get_dvr_duration(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_transcode_app)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(__full_conf));

    vhost = "app.transcode.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_FALSE(conf.get_atc(vhost));
    EXPECT_TRUE(conf.get_atc_auto(vhost));
    EXPECT_TRUE(conf.get_time_jitter(vhost) == SrsRtmpJitterAlgorithmFULL);
    EXPECT_FLOAT_EQ(30, conf.get_queue_length(vhost));
    EXPECT_TRUE(NULL == conf.get_refer(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_play(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_publish(vhost));
    EXPECT_EQ(60000, conf.get_chunk_size(vhost));
    EXPECT_TRUE(NULL == conf.get_forward(vhost));
    EXPECT_FALSE(conf.get_vhost_http_hooks_enabled(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_connect(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_close(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_publish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_unpublish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_play(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_stop(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_dvr_hss_reap_flv(vhost));
    EXPECT_FALSE(conf.get_bw_check_enabled(vhost));
    EXPECT_TRUE(conf.get_bw_check_key(vhost).empty());
    EXPECT_EQ(30000, conf.get_bw_check_interval_ms(vhost));
    EXPECT_EQ(1000, conf.get_bw_check_limit_kbps(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(conf.get_vhost(vhost)));
    EXPECT_TRUE(NULL == conf.get_vhost_edge_origin(vhost));
    EXPECT_FALSE(conf.get_vhost_edge_token_traverse(vhost));
    EXPECT_TRUE(NULL == conf.get_transcode(vhost, ""));
    EXPECT_FALSE(conf.get_transcode_enabled(NULL));
    EXPECT_TRUE(conf.get_transcode_ffmpeg(NULL).empty());
    EXPECT_TRUE(conf.get_transcode_engines(NULL).size() == 0);
    EXPECT_FALSE(conf.get_engine_enabled(NULL));
    EXPECT_STREQ("flv", conf.get_engine_iformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_vfilter(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_vcodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vbitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vfps(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vwidth(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vheight(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vthreads(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vprofile(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vpreset(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vparams(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_acodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_abitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_asample_rate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_achannels(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_aparams(NULL).size() == 0);
    EXPECT_STREQ("flv", conf.get_engine_oformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_output(NULL).empty());
    EXPECT_TRUE(NULL != conf.get_transcode(vhost, "live"));
    if (true) {
        SrsConfDirective* transcode = conf.get_transcode(vhost, "live");
        EXPECT_TRUE(conf.get_transcode_enabled(transcode));
        
        vector<SrsConfDirective*> engines = conf.get_transcode_engines(transcode);
        ASSERT_TRUE((int)engines.size() != 0);
        
        SrsConfDirective* engine = engines.at(0);
        EXPECT_FALSE(conf.get_engine_enabled(engine));
    }
    EXPECT_TRUE(conf.get_ingesters(vhost).size() == 0);
    EXPECT_TRUE(NULL == conf.get_ingest_by_id(vhost, ""));
    EXPECT_FALSE(conf.get_ingest_enabled(NULL));
    EXPECT_TRUE(conf.get_ingest_ffmpeg(NULL).empty());
    EXPECT_STREQ("file", conf.get_ingest_input_type(NULL).c_str());
    EXPECT_TRUE(conf.get_ingest_input_url(NULL).empty());
    EXPECT_FALSE(conf.get_hls_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_hls_path(vhost).c_str());
    EXPECT_EQ(10, conf.get_hls_fragment(vhost));
    EXPECT_EQ(60, conf.get_hls_window(vhost));
    EXPECT_FALSE(conf.get_dvr_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_dvr_path(vhost).c_str());
    EXPECT_STREQ("session", conf.get_dvr_plan(vhost).c_str());
    EXPECT_EQ(30, conf.get_dvr_duration(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_transcode_stream)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(__full_conf));

    vhost = "stream.transcode.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_FALSE(conf.get_atc(vhost));
    EXPECT_TRUE(conf.get_atc_auto(vhost));
    EXPECT_TRUE(conf.get_time_jitter(vhost) == SrsRtmpJitterAlgorithmFULL);
    EXPECT_FLOAT_EQ(30, conf.get_queue_length(vhost));
    EXPECT_TRUE(NULL == conf.get_refer(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_play(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_publish(vhost));
    EXPECT_EQ(60000, conf.get_chunk_size(vhost));
    EXPECT_TRUE(NULL == conf.get_forward(vhost));
    EXPECT_FALSE(conf.get_vhost_http_hooks_enabled(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_connect(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_close(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_publish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_unpublish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_play(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_stop(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_dvr_hss_reap_flv(vhost));
    EXPECT_FALSE(conf.get_bw_check_enabled(vhost));
    EXPECT_TRUE(conf.get_bw_check_key(vhost).empty());
    EXPECT_EQ(30000, conf.get_bw_check_interval_ms(vhost));
    EXPECT_EQ(1000, conf.get_bw_check_limit_kbps(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(conf.get_vhost(vhost)));
    EXPECT_TRUE(NULL == conf.get_vhost_edge_origin(vhost));
    EXPECT_FALSE(conf.get_vhost_edge_token_traverse(vhost));
    EXPECT_TRUE(NULL == conf.get_transcode(vhost, ""));
    EXPECT_FALSE(conf.get_transcode_enabled(NULL));
    EXPECT_TRUE(conf.get_transcode_ffmpeg(NULL).empty());
    EXPECT_TRUE(conf.get_transcode_engines(NULL).size() == 0);
    EXPECT_FALSE(conf.get_engine_enabled(NULL));
    EXPECT_STREQ("flv", conf.get_engine_iformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_vfilter(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_vcodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vbitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vfps(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vwidth(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vheight(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vthreads(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vprofile(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vpreset(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vparams(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_acodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_abitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_asample_rate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_achannels(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_aparams(NULL).size() == 0);
    EXPECT_STREQ("flv", conf.get_engine_oformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_output(NULL).empty());
    EXPECT_TRUE(NULL != conf.get_transcode(vhost, "live/livestream"));
    if (true) {
        SrsConfDirective* transcode = conf.get_transcode(vhost, "live/livestream");
        EXPECT_TRUE(conf.get_transcode_enabled(transcode));
        
        vector<SrsConfDirective*> engines = conf.get_transcode_engines(transcode);
        ASSERT_TRUE((int)engines.size() != 0);
        
        SrsConfDirective* engine = engines.at(0);
        EXPECT_FALSE(conf.get_engine_enabled(engine));
    }
    EXPECT_TRUE(conf.get_ingesters(vhost).size() == 0);
    EXPECT_TRUE(NULL == conf.get_ingest_by_id(vhost, ""));
    EXPECT_FALSE(conf.get_ingest_enabled(NULL));
    EXPECT_TRUE(conf.get_ingest_ffmpeg(NULL).empty());
    EXPECT_STREQ("file", conf.get_ingest_input_type(NULL).c_str());
    EXPECT_TRUE(conf.get_ingest_input_url(NULL).empty());
    EXPECT_FALSE(conf.get_hls_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_hls_path(vhost).c_str());
    EXPECT_EQ(10, conf.get_hls_fragment(vhost));
    EXPECT_EQ(60, conf.get_hls_window(vhost));
    EXPECT_FALSE(conf.get_dvr_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_dvr_path(vhost).c_str());
    EXPECT_STREQ("session", conf.get_dvr_plan(vhost).c_str());
    EXPECT_EQ(30, conf.get_dvr_duration(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_bandcheck)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(__full_conf));

    vhost = "bandcheck.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_FALSE(conf.get_atc(vhost));
    EXPECT_TRUE(conf.get_atc_auto(vhost));
    EXPECT_TRUE(conf.get_time_jitter(vhost) == SrsRtmpJitterAlgorithmFULL);
    EXPECT_FLOAT_EQ(30, conf.get_queue_length(vhost));
    EXPECT_TRUE(NULL == conf.get_refer(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_play(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_publish(vhost));
    EXPECT_EQ(60000, conf.get_chunk_size(vhost));
    EXPECT_TRUE(NULL == conf.get_forward(vhost));
    EXPECT_FALSE(conf.get_vhost_http_hooks_enabled(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_connect(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_close(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_publish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_unpublish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_play(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_stop(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_dvr_hss_reap_flv(vhost));
    EXPECT_FALSE(conf.get_bw_check_enabled(vhost));
    EXPECT_TRUE(conf.get_bw_check_key(vhost).empty());
    EXPECT_EQ(30000, conf.get_bw_check_interval_ms(vhost));
    EXPECT_EQ(1000, conf.get_bw_check_limit_kbps(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(vhost));
    EXPECT_FALSE(conf.get_vhost_is_edge(conf.get_vhost(vhost)));
    EXPECT_TRUE(NULL == conf.get_vhost_edge_origin(vhost));
    EXPECT_FALSE(conf.get_vhost_edge_token_traverse(vhost));
    EXPECT_TRUE(NULL == conf.get_transcode(vhost, ""));
    EXPECT_FALSE(conf.get_transcode_enabled(NULL));
    EXPECT_TRUE(conf.get_transcode_ffmpeg(NULL).empty());
    EXPECT_TRUE(conf.get_transcode_engines(NULL).size() == 0);
    EXPECT_FALSE(conf.get_engine_enabled(NULL));
    EXPECT_STREQ("flv", conf.get_engine_iformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_vfilter(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_vcodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vbitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vfps(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vwidth(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vheight(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vthreads(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_vprofile(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vpreset(NULL).empty());
    EXPECT_TRUE(conf.get_engine_vparams(NULL).size() == 0);
    EXPECT_TRUE(conf.get_engine_acodec(NULL).empty());
    EXPECT_TRUE(conf.get_engine_abitrate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_asample_rate(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_achannels(NULL) == 0);
    EXPECT_TRUE(conf.get_engine_aparams(NULL).size() == 0);
    EXPECT_STREQ("flv", conf.get_engine_oformat(NULL).c_str());
    EXPECT_TRUE(conf.get_engine_output(NULL).empty());
    EXPECT_TRUE(conf.get_ingesters(vhost).size() == 0);
    EXPECT_TRUE(NULL == conf.get_ingest_by_id(vhost, ""));
    EXPECT_FALSE(conf.get_ingest_enabled(NULL));
    EXPECT_TRUE(conf.get_ingest_ffmpeg(NULL).empty());
    EXPECT_STREQ("file", conf.get_ingest_input_type(NULL).c_str());
    EXPECT_TRUE(conf.get_ingest_input_url(NULL).empty());
    EXPECT_FALSE(conf.get_hls_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_hls_path(vhost).c_str());
    EXPECT_EQ(10, conf.get_hls_fragment(vhost));
    EXPECT_EQ(60, conf.get_hls_window(vhost));
    EXPECT_FALSE(conf.get_dvr_enabled(vhost));
    EXPECT_STREQ("./objs/nginx/html", conf.get_dvr_path(vhost).c_str());
    EXPECT_STREQ("session", conf.get_dvr_plan(vhost).c_str());
    EXPECT_EQ(30, conf.get_dvr_duration(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}
