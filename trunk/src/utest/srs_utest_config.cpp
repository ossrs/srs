/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(simple-rtmp-server)

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
#include <srs_core_performance.hpp>

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
    int ret = ERROR_SUCCESS;

    MockSrsConfigBuffer buffer(buf);

    if ((ret = parse_buffer(&buffer)) != ERROR_SUCCESS) {
        return ret;
    }

    return check_config();
}

#ifdef ENABLE_UTEST_CONFIG

// full.conf
std::string _full_conf = ""
"listen              1935;                                                                                                                               \n          "
"pid                 ./objs/srs.pid;                                                                                                                     \n          "
"chunk_size          60000;                                                                                                                              \n          "
"ff_log_dir          ./objs;                                                                                                                             \n          "
"srs_log_tank        file;                                                                                                                               \n          "
"srs_log_level       trace;                                                                                                                              \n          "
"srs_log_file        ./objs/srs.log;                                                                                                                     \n          "
"max_connections     1000;                                                                                                                               \n          "
"daemon              on;                                                                                                                                 \n          "
"                                                                                                                                                        \n          "
"heartbeat {                                                                                                                                             \n          "
"    enabled         off;                                                                                                                                \n          "
"    interval        9.3;                                                                                                                                \n          "
"    url             http://127.0.0.1:8085/api/v1/servers;                                                                                               \n          "
"    device_id       \"my-srs-device\";                                                                                                                    \n        "
"    summaries       off;                                                                                                                                \n          "
"}                                                                                                                                                       \n          "
"                                                                                                                                                        \n          "
"stats {                                                                                                                                                 \n          "
"    network         0;                                                                                                                                  \n          "
"    disk            sda sdb xvda xvdb;                                                                                                                  \n          "
"}                                                                                                                                                       \n          "
"                                                                                                                                                        \n          "
"http_api {                                                                                                                                              \n          "
"    enabled         on;                                                                                                                                 \n          "
"    listen          1985;                                                                                                                               \n          "
"    crossdomain     on;                                                                                                                                 \n          "
"}                                                                                                                                                       \n          "
"http_server {                                                                                                                                           \n          "
"    enabled         on;                                                                                                                                 \n          "
"    listen          8080;                                                                                                                               \n          "
"    dir             ./objs/nginx/html;                                                                                                                  \n          "
"}                                                                                                                                                       \n          "
"                                                                                                                                                        \n          "
"stream_caster {                                                                                                                                         \n          "
"    enabled         off;                                                                                                                                \n          "
"    caster          mpegts_over_udp;                                                                                                                    \n          "
"    output          rtmp://127.0.0.1/live/livestream;                                                                                                   \n          "
"    listen          8935;                                                                                                                               \n          "
"    rtp_port_min    57200;                                                                                                                              \n          "
"    rtp_port_max    57300;                                                                                                                              \n          "
"}                                                                                                                                                       \n          "
"stream_caster {                                                                                                                                         \n          "
"    enabled         off;                                                                                                                                \n          "
"    caster          mpegts_over_udp;                                                                                                                    \n          "
"    output          rtmp://127.0.0.1/live/livestream;                                                                                                   \n          "
"    listen          8935;                                                                                                                               \n          "
"}                                                                                                                                                       \n          "
"stream_caster {                                                                                                                                         \n          "
"    enabled         off;                                                                                                                                \n          "
"    caster          rtsp;                                                                                                                               \n          "
"    output          rtmp://127.0.0.1/[app]/[stream];                                                                                                    \n          "
"    listen          554;                                                                                                                                \n          "
"    rtp_port_min    57200;                                                                                                                              \n          "
"    rtp_port_max    57300;                                                                                                                              \n          "
"}                                                                                                                                                       \n          "
"                                                                                                                                                        \n          "
"vhost __defaultVhost__ {                                                                                                                                \n          "
"}                                                                                                                                                       \n          "
"                                                                                                                                                        \n          "
"vhost security.srs.com {                                                                                                                                \n          "
"    security {                                                                                                                                          \n          "
"        enabled         on;                                                                                                                             \n          "
"        allow           play        all;                                                                                                                \n          "
"        allow           publish     all;                                                                                                                \n          "
"    }                                                                                                                                                   \n          "
"}                                                                                                                                                       \n          "
"                                                                                                                                                        \n          "
"vhost mrw.srs.com {                                                                                                                                     \n          "
"    min_latency     off;                                                                                                                                \n          "
"    mr {                                                                                                                                                \n          "
"        enabled     on;                                                                                                                                 \n          "
"        latency     350;                                                                                                                                \n          "
"    }                                                                                                                                                   \n          "
"    mw_latency      350;                                                                                                                                \n          "
"}                                                                                                                                                       \n          "
"                                                                                                                                                        \n          "
"vhost same.edge.srs.com {                                                                                                                               \n          "
"    mode            remote;                                                                                                                             \n          "
"    origin          127.0.0.1:1935 localhost:1935;                                                                                                      \n          "
"    token_traverse  off;                                                                                                                                \n          "
"}                                                                                                                                                       \n          "
"                                                                                                                                                        \n          "
"vhost dvr.srs.com {                                                                                                                                     \n          "
"    dvr {                                                                                                                                               \n          "
"        enabled         on;                                                                                                                             \n          "
"        dvr_plan        session;                                                                                                                        \n          "
"        dvr_path        ./objs/nginx/html;                                                                                                              \n          "
"        dvr_duration    30;                                                                                                                             \n          "
"        dvr_wait_keyframe       on;                                                                                                                     \n          "
"        time_jitter             full;                                                                                                                   \n          "
"                                                                                                                                                        \n          "
"    }                                                                                                                                                   \n          "
"}                                                                                                                                                       \n          "
"                                                                                                                                                        \n          "
"vhost ingest.srs.com {                                                                                                                                  \n          "
"    ingest livestream {                                                                                                                                 \n          "
"        enabled      on;                                                                                                                                \n          "
"        input {                                                                                                                                         \n          "
"            type    file;                                                                                                                               \n          "
"            url     ./doc/source.200kbps.768x320.flv;                                                                                                   \n          "
"        }                                                                                                                                               \n          "
"        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;                                                                                                           \n          "
"        engine {                                                                                                                                        \n          "
"            enabled          off;                                                                                                                       \n          "
"            output          rtmp://127.0.0.1:[port]/live?vhost=[vhost]/livestream;                                                                      \n          "
"        }                                                                                                                                               \n          "
"    }                                                                                                                                                   \n          "
"}                                                                                                                                                       \n          "
"                                                                                                                                                        \n          "
"vhost http.static.srs.com {                                                                                                                             \n          "
"    http {                                                                                                                                              \n          "
"        enabled     on;                                                                                                                                 \n          "
"        mount       [vhost]/hls;                                                                                                                        \n          "
"        dir         ./objs/nginx/html/hls;                                                                                                              \n          "
"    }                                                                                                                                                   \n          "
"}                                                                                                                                                       \n          "
"                                                                                                                                                        \n          "
"vhost http.remux.srs.com {                                                                                                                              \n          "
"    http_remux {                                                                                                                                        \n          "
"        enabled     on;                                                                                                                                 \n          "
"        fast_cache  30;                                                                                                                                 \n          "
"        mount       [vhost]/[app]/[stream].flv;                                                                                                         \n          "
"    }                                                                                                                                                   \n          "
"}                                                                                                                                                       \n          "
"                                                                                                                                                        \n          "
"vhost with-hls.srs.com {                                                                                                                                \n          "
"    hls {                                                                                                                                               \n          "
"        enabled         on;                                                                                                                             \n          "
"        hls_fragment    10;                                                                                                                             \n          "
"        hls_td_ratio    1.5;                                                                                                                            \n          "
"        hls_window      60;                                                                                                                             \n          "
"        hls_on_error    ignore;                                                                                                                         \n          "
"        hls_storage     disk;                                                                                                                           \n          "
"        hls_path        ./objs/nginx/html;                                                                                                              \n          "
"        hls_mount       [vhost]/[app]/[stream].m3u8;                                                                                                    \n          "
"        hls_acodec      aac;                                                                                                                            \n          "
"        hls_vcodec      h264;                                                                                                                           \n          "
"    }                                                                                                                                                   \n          "
"}                                                                                                                                                       \n          "
"vhost no-hls.srs.com {                                                                                                                                  \n          "
"    hls {                                                                                                                                               \n          "
"        enabled         off;                                                                                                                            \n          "
"    }                                                                                                                                                   \n          "
"}                                                                                                                                                       \n          "
"                                                                                                                                                        \n          "
"vhost hooks.callback.srs.com {                                                                                                                          \n          "
"    http_hooks {                                                                                                                                        \n          "
"        enabled         on;                                                                                                                             \n          "
"        on_connect      http://127.0.0.1:8085/api/v1/clients http://localhost:8085/api/v1/clients;                                                      \n          "
"        on_close        http://127.0.0.1:8085/api/v1/clients http://localhost:8085/api/v1/clients;                                                      \n          "
"        on_publish      http://127.0.0.1:8085/api/v1/streams http://localhost:8085/api/v1/streams;                                                      \n          "
"        on_unpublish    http://127.0.0.1:8085/api/v1/streams http://localhost:8085/api/v1/streams;                                                      \n          "
"        on_play         http://127.0.0.1:8085/api/v1/sessions http://localhost:8085/api/v1/sessions;                                                    \n          "
"        on_stop         http://127.0.0.1:8085/api/v1/sessions http://localhost:8085/api/v1/sessions;                                                    \n          "
"        on_dvr          http://127.0.0.1:8085/api/v1/dvrs http://localhost:8085/api/v1/dvrs;                                                            \n          "
"    }                                                                                                                                                   \n          "
"}                                                                                                                                                       \n          "
"                                                                                                                                                        \n          "
"vhost debug.srs.com {                                                                                                                                   \n          "
"    debug_srs_upnode    on;                                                                                                                             \n          "
"}                                                                                                                                                       \n          "
"                                                                                                                                                        \n          "
"vhost min.delay.com {                                                                                                                                   \n          "
"    min_latency     on;                                                                                                                                 \n          "
"    mr {                                                                                                                                                \n          "
"        enabled     off;                                                                                                                                \n          "
"    }                                                                                                                                                   \n          "
"    mw_latency      100;                                                                                                                                \n          "
"    gop_cache       off;                                                                                                                                \n          "
"    queue_length    10;                                                                                                                                 \n          "
"}                                                                                                                                                       \n          "
"                                                                                                                                                        \n          "
"vhost refer.anti_suck.com {                                                                                                                             \n          "
"    refer           github.com github.io;                                                                                                               \n          "
"    refer_publish   github.com github.io;                                                                                                               \n          "
"    refer_play      github.com github.io;                                                                                                               \n          "
"}                                                                                                                                                       \n          "
"                                                                                                                                                        \n          "
"vhost same.vhost.forward.srs.com {                                                                                                                      \n          "
"    forward         127.0.0.1:1936 127.0.0.1:1937;                                                                                                      \n          "
"}                                                                                                                                                       \n          "
"                                                                                                                                                        \n          "
"vhost example.transcode.srs.com {                                                                                                                       \n          "
"    transcode {                                                                                                                                         \n          "
"        enabled     on;                                                                                                                                 \n          "
"        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;                                                                                                           \n          "
"        engine example {                                                                                                                                \n          "
"            enabled         on;                                                                                                                         \n          "
"            iformat         flv;                                                                                                                        \n          "
"            vfilter {                                                                                                                                   \n          "
"                i               ./doc/ffmpeg-logo.png;                                                                                                  \n          "
"                filter_complex  'overlay=10:10';                                                                                                        \n          "
"            }                                                                                                                                           \n          "
"            vcodec          libx264;                                                                                                                    \n          "
"            vbitrate        1500;                                                                                                                       \n          "
"            vfps            25;                                                                                                                         \n          "
"            vwidth          768;                                                                                                                        \n          "
"            vheight         320;                                                                                                                        \n          "
"            vthreads        12;                                                                                                                         \n          "
"            vprofile        main;                                                                                                                       \n          "
"            vpreset         medium;                                                                                                                     \n          "
"            vparams {                                                                                                                                   \n          "
"                t               100;                                                                                                                    \n          "
"                coder           1;                                                                                                                      \n          "
"                b_strategy      2;                                                                                                                      \n          "
"                bf              3;                                                                                                                      \n          "
"                refs            10;                                                                                                                     \n          "
"            }                                                                                                                                           \n          "
"            acodec          libfdk_aac;                                                                                                                 \n          "
"            abitrate        70;                                                                                                                         \n          "
"            asample_rate    44100;                                                                                                                      \n          "
"            achannels       2;                                                                                                                          \n          "
"            aparams {                                                                                                                                   \n          "
"                profile:a   aac_low;                                                                                                                    \n          "
"            }                                                                                                                                           \n          "
"            oformat         flv;                                                                                                                        \n          "
"            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                                              \n          "
"        }                                                                                                                                               \n          "
"    }                                                                                                                                                   \n          "
"}                                                                                                                                                       \n          "
"vhost mirror.transcode.srs.com {                                                                                                                        \n          "
"    transcode {                                                                                                                                         \n          "
"        enabled     on;                                                                                                                                 \n          "
"        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;                                                                                                           \n          "
"        engine mirror {                                                                                                                                 \n          "
"            enabled         on;                                                                                                                         \n          "
"            vfilter {                                                                                                                                   \n          "
"                vf                  'split [main][tmp]; [tmp] crop=iw:ih/2:0:0, vflip [flip]; [main][flip] overlay=0:H/2';                              \n          "
"            }                                                                                                                                           \n          "
"            vcodec          libx264;                                                                                                                    \n          "
"            vbitrate        300;                                                                                                                        \n          "
"            vfps            20;                                                                                                                         \n          "
"            vwidth          768;                                                                                                                        \n          "
"            vheight         320;                                                                                                                        \n          "
"            vthreads        2;                                                                                                                          \n          "
"            vprofile        baseline;                                                                                                                   \n          "
"            vpreset         superfast;                                                                                                                  \n          "
"            vparams {                                                                                                                                   \n          "
"            }                                                                                                                                           \n          "
"            acodec          libfdk_aac;                                                                                                                 \n          "
"            abitrate        45;                                                                                                                         \n          "
"            asample_rate    44100;                                                                                                                      \n          "
"            achannels       2;                                                                                                                          \n          "
"            aparams {                                                                                                                                   \n          "
"            }                                                                                                                                           \n          "
"            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                                              \n          "
"        }                                                                                                                                               \n          "
"    }                                                                                                                                                   \n          "
"}                                                                                                                                                       \n          "
"vhost crop.transcode.srs.com {                                                                                                                          \n          "
"    transcode {                                                                                                                                         \n          "
"        enabled     on;                                                                                                                                 \n          "
"        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;                                                                                                           \n          "
"        engine crop {                                                                                                                                   \n          "
"            enabled         on;                                                                                                                         \n          "
"            vfilter {                                                                                                                                   \n          "
"                vf                  'crop=in_w-20:in_h-160:10:80';                                                                                      \n          "
"            }                                                                                                                                           \n          "
"            vcodec          libx264;                                                                                                                    \n          "
"            vbitrate        300;                                                                                                                        \n          "
"            vfps            20;                                                                                                                         \n          "
"            vwidth          768;                                                                                                                        \n          "
"            vheight         320;                                                                                                                        \n          "
"            vthreads        2;                                                                                                                          \n          "
"            vprofile        baseline;                                                                                                                   \n          "
"            vpreset         superfast;                                                                                                                  \n          "
"            vparams {                                                                                                                                   \n          "
"            }                                                                                                                                           \n          "
"            acodec          libfdk_aac;                                                                                                                 \n          "
"            abitrate        45;                                                                                                                         \n          "
"            asample_rate    44100;                                                                                                                      \n          "
"            achannels       2;                                                                                                                          \n          "
"            aparams {                                                                                                                                   \n          "
"            }                                                                                                                                           \n          "
"            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                                              \n          "
"        }                                                                                                                                               \n          "
"    }                                                                                                                                                   \n          "
"}                                                                                                                                                       \n          "
"vhost logo.transcode.srs.com {                                                                                                                          \n          "
"    transcode {                                                                                                                                         \n          "
"        enabled     on;                                                                                                                                 \n          "
"        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;                                                                                                           \n          "
"        engine logo {                                                                                                                                   \n          "
"            enabled         on;                                                                                                                         \n          "
"            vfilter {                                                                                                                                   \n          "
"                i               ./doc/ffmpeg-logo.png;                                                                                                  \n          "
"                filter_complex      'overlay=10:10';                                                                                                    \n          "
"            }                                                                                                                                           \n          "
"            vcodec          libx264;                                                                                                                    \n          "
"            vbitrate        300;                                                                                                                        \n          "
"            vfps            20;                                                                                                                         \n          "
"            vwidth          768;                                                                                                                        \n          "
"            vheight         320;                                                                                                                        \n          "
"            vthreads        2;                                                                                                                          \n          "
"            vprofile        baseline;                                                                                                                   \n          "
"            vpreset         superfast;                                                                                                                  \n          "
"            vparams {                                                                                                                                   \n          "
"            }                                                                                                                                           \n          "
"            acodec          libfdk_aac;                                                                                                                 \n          "
"            abitrate        45;                                                                                                                         \n          "
"            asample_rate    44100;                                                                                                                      \n          "
"            achannels       2;                                                                                                                          \n          "
"            aparams {                                                                                                                                   \n          "
"            }                                                                                                                                           \n          "
"            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                                              \n          "
"        }                                                                                                                                               \n          "
"    }                                                                                                                                                   \n          "
"}                                                                                                                                                       \n          "
"vhost audio.transcode.srs.com {                                                                                                                         \n          "
"    transcode {                                                                                                                                         \n          "
"        enabled     on;                                                                                                                                 \n          "
"        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;                                                                                                           \n          "
"        engine acodec {                                                                                                                                 \n          "
"            enabled         on;                                                                                                                         \n          "
"            vcodec          copy;                                                                                                                       \n          "
"            acodec          libfdk_aac;                                                                                                                 \n          "
"            abitrate        45;                                                                                                                         \n          "
"            asample_rate    44100;                                                                                                                      \n          "
"            achannels       2;                                                                                                                          \n          "
"            aparams {                                                                                                                                   \n          "
"            }                                                                                                                                           \n          "
"            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                                              \n          "
"        }                                                                                                                                               \n          "
"    }                                                                                                                                                   \n          "
"}                                                                                                                                                       \n          "
"vhost vn.transcode.srs.com {                                                                                                                            \n          "
"    transcode {                                                                                                                                         \n          "
"        enabled     on;                                                                                                                                 \n          "
"        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;                                                                                                           \n          "
"        engine vn {                                                                                                                                     \n          "
"            enabled         on;                                                                                                                         \n          "
"            vcodec          vn;                                                                                                                         \n          "
"            acodec          libfdk_aac;                                                                                                                 \n          "
"            abitrate        45;                                                                                                                         \n          "
"            asample_rate    44100;                                                                                                                      \n          "
"            achannels       2;                                                                                                                          \n          "
"            aparams {                                                                                                                                   \n          "
"            }                                                                                                                                           \n          "
"            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                                              \n          "
"        }                                                                                                                                               \n          "
"    }                                                                                                                                                   \n          "
"}                                                                                                                                                       \n          "
"vhost copy.transcode.srs.com {                                                                                                                          \n          "
"    transcode {                                                                                                                                         \n          "
"        enabled     on;                                                                                                                                 \n          "
"        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;                                                                                                           \n          "
"        engine copy {                                                                                                                                   \n          "
"            enabled         on;                                                                                                                         \n          "
"            vcodec          copy;                                                                                                                       \n          "
"            acodec          copy;                                                                                                                       \n          "
"            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                                              \n          "
"        }                                                                                                                                               \n          "
"    }                                                                                                                                                   \n          "
"}                                                                                                                                                       \n          "
"vhost all.transcode.srs.com {                                                                                                                           \n          "
"    transcode {                                                                                                                                         \n          "
"        enabled     on;                                                                                                                                 \n          "
"        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;                                                                                                           \n          "
"        engine ffsuper {                                                                                                                                \n          "
"            enabled         on;                                                                                                                         \n          "
"            iformat         flv;                                                                                                                        \n          "
"            vfilter {                                                                                                                                   \n          "
"                i               ./doc/ffmpeg-logo.png;                                                                                                  \n          "
"                filter_complex  'overlay=10:10';                                                                                                        \n          "
"            }                                                                                                                                           \n          "
"            vcodec          libx264;                                                                                                                    \n          "
"            vbitrate        1500;                                                                                                                       \n          "
"            vfps            25;                                                                                                                         \n          "
"            vwidth          768;                                                                                                                        \n          "
"            vheight         320;                                                                                                                        \n          "
"            vthreads        12;                                                                                                                         \n          "
"            vprofile        main;                                                                                                                       \n          "
"            vpreset         medium;                                                                                                                     \n          "
"            vparams {                                                                                                                                   \n          "
"                t               100;                                                                                                                    \n          "
"                coder           1;                                                                                                                      \n          "
"                b_strategy      2;                                                                                                                      \n          "
"                bf              3;                                                                                                                      \n          "
"                refs            10;                                                                                                                     \n          "
"            }                                                                                                                                           \n          "
"            acodec          libfdk_aac;                                                                                                                 \n          "
"            abitrate        70;                                                                                                                         \n          "
"            asample_rate    44100;                                                                                                                      \n          "
"            achannels       2;                                                                                                                          \n          "
"            aparams {                                                                                                                                   \n          "
"                profile:a   aac_low;                                                                                                                    \n          "
"            }                                                                                                                                           \n          "
"            oformat         flv;                                                                                                                        \n          "
"            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                                              \n          "
"        }                                                                                                                                               \n          "
"        engine ffhd {                                                                                                                                   \n          "
"            enabled         on;                                                                                                                         \n          "
"            vcodec          libx264;                                                                                                                    \n          "
"            vbitrate        1200;                                                                                                                       \n          "
"            vfps            25;                                                                                                                         \n          "
"            vwidth          1382;                                                                                                                       \n          "
"            vheight         576;                                                                                                                        \n          "
"            vthreads        6;                                                                                                                          \n          "
"            vprofile        main;                                                                                                                       \n          "
"            vpreset         medium;                                                                                                                     \n          "
"            vparams {                                                                                                                                   \n          "
"            }                                                                                                                                           \n          "
"            acodec          libfdk_aac;                                                                                                                 \n          "
"            abitrate        70;                                                                                                                         \n          "
"            asample_rate    44100;                                                                                                                      \n          "
"            achannels       2;                                                                                                                          \n          "
"            aparams {                                                                                                                                   \n          "
"            }                                                                                                                                           \n          "
"            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                                              \n          "
"        }                                                                                                                                               \n          "
"        engine ffsd {                                                                                                                                   \n          "
"            enabled         on;                                                                                                                         \n          "
"            vcodec          libx264;                                                                                                                    \n          "
"            vbitrate        800;                                                                                                                        \n          "
"            vfps            25;                                                                                                                         \n          "
"            vwidth          1152;                                                                                                                       \n          "
"            vheight         480;                                                                                                                        \n          "
"            vthreads        4;                                                                                                                          \n          "
"            vprofile        main;                                                                                                                       \n          "
"            vpreset         fast;                                                                                                                       \n          "
"            vparams {                                                                                                                                   \n          "
"            }                                                                                                                                           \n          "
"            acodec          libfdk_aac;                                                                                                                 \n          "
"            abitrate        60;                                                                                                                         \n          "
"            asample_rate    44100;                                                                                                                      \n          "
"            achannels       2;                                                                                                                          \n          "
"            aparams {                                                                                                                                   \n          "
"            }                                                                                                                                           \n          "
"            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                                              \n          "
"        }                                                                                                                                               \n          "
"        engine fffast {                                                                                                                                 \n          "
"            enabled     on;                                                                                                                             \n          "
"            vcodec          libx264;                                                                                                                    \n          "
"            vbitrate        300;                                                                                                                        \n          "
"            vfps            20;                                                                                                                         \n          "
"            vwidth          768;                                                                                                                        \n          "
"            vheight         320;                                                                                                                        \n          "
"            vthreads        2;                                                                                                                          \n          "
"            vprofile        baseline;                                                                                                                   \n          "
"            vpreset         superfast;                                                                                                                  \n          "
"            vparams {                                                                                                                                   \n          "
"            }                                                                                                                                           \n          "
"            acodec          libfdk_aac;                                                                                                                 \n          "
"            abitrate        45;                                                                                                                         \n          "
"            asample_rate    44100;                                                                                                                      \n          "
"            achannels       2;                                                                                                                          \n          "
"            aparams {                                                                                                                                   \n          "
"            }                                                                                                                                           \n          "
"            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                                              \n          "
"        }                                                                                                                                               \n          "
"        engine vcopy {                                                                                                                                  \n          "
"            enabled         on;                                                                                                                         \n          "
"            vcodec          copy;                                                                                                                       \n          "
"            acodec          libfdk_aac;                                                                                                                 \n          "
"            abitrate        45;                                                                                                                         \n          "
"            asample_rate    44100;                                                                                                                      \n          "
"            achannels       2;                                                                                                                          \n          "
"            aparams {                                                                                                                                   \n          "
"            }                                                                                                                                           \n          "
"            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                                              \n          "
"        }                                                                                                                                               \n          "
"        engine acopy {                                                                                                                                  \n          "
"            enabled     on;                                                                                                                             \n          "
"            vcodec          libx264;                                                                                                                    \n          "
"            vbitrate        300;                                                                                                                        \n          "
"            vfps            20;                                                                                                                         \n          "
"            vwidth          768;                                                                                                                        \n          "
"            vheight         320;                                                                                                                        \n          "
"            vthreads        2;                                                                                                                          \n          "
"            vprofile        baseline;                                                                                                                   \n          "
"            vpreset         superfast;                                                                                                                  \n          "
"            vparams {                                                                                                                                   \n          "
"            }                                                                                                                                           \n          "
"            acodec          copy;                                                                                                                       \n          "
"            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                                              \n          "
"        }                                                                                                                                               \n          "
"        engine copy {                                                                                                                                   \n          "
"            enabled         on;                                                                                                                         \n          "
"            vcodec          copy;                                                                                                                       \n          "
"            acodec          copy;                                                                                                                       \n          "
"            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                                              \n          "
"        }                                                                                                                                               \n          "
"    }                                                                                                                                                   \n          "
"}                                                                                                                                                       \n          "
"vhost ffempty.transcode.srs.com {                                                                                                                       \n          "
"    transcode {                                                                                                                                         \n          "
"        enabled     on;                                                                                                                                 \n          "
"        ffmpeg ./objs/research/ffempty;                                                                                                                 \n          "
"        engine empty {                                                                                                                                  \n          "
"            enabled         on;                                                                                                                         \n          "
"            vcodec          libx264;                                                                                                                    \n          "
"            vbitrate        300;                                                                                                                        \n          "
"            vfps            20;                                                                                                                         \n          "
"            vwidth          768;                                                                                                                        \n          "
"            vheight         320;                                                                                                                        \n          "
"            vthreads        2;                                                                                                                          \n          "
"            vprofile        baseline;                                                                                                                   \n          "
"            vpreset         superfast;                                                                                                                  \n          "
"            vparams {                                                                                                                                   \n          "
"            }                                                                                                                                           \n          "
"            acodec          libfdk_aac;                                                                                                                 \n          "
"            abitrate        45;                                                                                                                         \n          "
"            asample_rate    44100;                                                                                                                      \n          "
"            achannels       2;                                                                                                                          \n          "
"            aparams {                                                                                                                                   \n          "
"            }                                                                                                                                           \n          "
"            output          rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];                                                              \n          "
"        }                                                                                                                                               \n          "
"    }                                                                                                                                                   \n          "
"}                                                                                                                                                       \n          "
"vhost app.transcode.srs.com {                                                                                                                           \n          "
"    transcode live {                                                                                                                                    \n          "
"        enabled     on;                                                                                                                                 \n          "
"        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;                                                                                                           \n          "
"        engine {                                                                                                                                        \n          "
"            enabled     off;                                                                                                                            \n          "
"        }                                                                                                                                               \n          "
"    }                                                                                                                                                   \n          "
"}                                                                                                                                                       \n          "
"vhost stream.transcode.srs.com {                                                                                                                        \n          "
"    transcode live/livestream {                                                                                                                         \n          "
"        enabled     on;                                                                                                                                 \n          "
"        ffmpeg      ./objs/ffmpeg/bin/ffmpeg;                                                                                                           \n          "
"        engine {                                                                                                                                        \n          "
"            enabled     off;                                                                                                                            \n          "
"        }                                                                                                                                               \n          "
"    }                                                                                                                                                   \n          "
"}                                                                                                                                                       \n          "
"                                                                                                                                                        \n          "
"vhost bandcheck.srs.com {                                                                                                                               \n          "
"    enabled         on;                                                                                                                                 \n          "
"    chunk_size      65000;                                                                                                                              \n          "
"    bandcheck {                                                                                                                                         \n          "
"        enabled         on;                                                                                                                             \n          "
"        key             \"35c9b402c12a7246868752e2878f7e0e\";                                                                                             \n        "
"        interval        30;                                                                                                                             \n          "
"        limit_kbps      4000;                                                                                                                           \n          "
"    }                                                                                                                                                   \n          "
"}                                                                                                                                                       \n          "
"                                                                                                                                                        \n          "
"vhost chunksize.srs.com {                                                                                                                               \n          "
"    chunk_size      128;                                                                                                                                \n          "
"}                                                                                                                                                       \n          "
"                                                                                                                                                        \n          "
"vhost jitter.srs.com {                                                                                                                                  \n          "
"    time_jitter             full;                                                                                                                       \n          "
"}                                                                                                                                                       \n          "
"                                                                                                                                                        \n          "
"vhost atc.srs.com {                                                                                                                                     \n          "
"    atc             on;                                                                                                                                 \n          "
"    atc_auto        on;                                                                                                                                 \n          "
"}                                                                                                                                                       \n          "
"                                                                                                                                                        \n          "
"vhost removed.srs.com {                                                                                                                                 \n          "
"    enabled         off;                                                                                                                                \n          "
"}                                                                                                                                                       \n          "
"                                                                                                                                                        \n          "
"pithy_print_ms      10000;                                                                                                                              \n          "
;

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
#ifndef SRS_CONF_DEFAULT_DVR_PLAN
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_DVR_DURATION
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_TIME_JITTER
    EXPECT_TRUE(false);
#endif
#ifndef SRS_PERF_PLAY_QUEUE
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
#ifndef SRS_CONF_DEFAULT_HTTP_HEAETBEAT_SUMMARIES
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_STATS_NETWORK_DEVICE_INDEX
    EXPECT_TRUE(false);
#endif
#ifndef SRS_CONF_DEFAULT_PITHY_PRINT_MS
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
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF));
    
    vector<string> listens = conf.get_listens();
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
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));
    
    vector<string> listens = conf.get_listens();
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
    
    EXPECT_FALSE(conf.get_heartbeat_enabled());
    EXPECT_EQ(9300, conf.get_heartbeat_interval());
    EXPECT_STREQ("http://127.0.0.1:8085/api/v1/servers", conf.get_heartbeat_url().c_str());
    EXPECT_STREQ("my-srs-device", conf.get_heartbeat_device_id().c_str());
    EXPECT_FALSE(conf.get_heartbeat_summaries());

    EXPECT_EQ(0, conf.get_stats_network());
    ASSERT_TRUE(conf.get_stats_disk_device() != NULL);
    EXPECT_EQ(4, (int)conf.get_stats_disk_device()->args.size());
    
    EXPECT_TRUE(conf.get_http_api_enabled());
    EXPECT_STREQ("1985", conf.get_http_api_listen().c_str());
    
    EXPECT_TRUE(conf.get_http_stream_enabled());
    EXPECT_STREQ("8080", conf.get_http_stream_listen().c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_http_stream_dir().c_str());
    
    EXPECT_EQ(10000, conf.get_pithy_print_ms());
    
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
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
    ////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////
}

VOID TEST(ConfigMainTest, ParseFullConf_same_edge)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));
    
    vhost = "same.edge.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
}

VOID TEST(ConfigMainTest, ParseFullConf_change_edge)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));
    
    vhost = "change.edge.srs.com";
    // TODO: FIXME: implements it.
    /*EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));*/
}

VOID TEST(ConfigMainTest, ParseFullConf_dvr)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));
    
    vhost = "dvr.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
}

VOID TEST(ConfigMainTest, ParseFullConf_ingest)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));
    
    vhost = "ingest.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
}

VOID TEST(ConfigMainTest, ParseFullConf_http)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));
    
    vhost = "http.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_hls_enabled)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));
    
    vhost = "with-hls.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_hls_disabled)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));
    
    vhost = "no-hls.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_http_hooks)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));
    
    vhost = "hooks.callback.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_min_delay)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));
    
    vhost = "min.delay.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_FALSE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_refer_anti_suck)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));

    vhost = "refer.anti_suck.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_forward_same_vhost)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));

    vhost = "same.vhost.forward.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_forward_change_vhost)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));

    vhost = "change.vhost.forward.srs.com";
    // TODO: FIXME: implements it.
    /*EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());*/
}

VOID TEST(ConfigMainTest, ParseFullConf_transcode_mirror)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));

    vhost = "mirror.transcode.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
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
        EXPECT_STREQ("libfdk_aac", conf.get_engine_acodec(engine).c_str());
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_transcode_crop)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));

    vhost = "crop.transcode.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
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
        EXPECT_STREQ("libfdk_aac", conf.get_engine_acodec(engine).c_str());
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_transcode_logo)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));

    vhost = "logo.transcode.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
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
        EXPECT_STREQ("libfdk_aac", conf.get_engine_acodec(engine).c_str());
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_transcode_audio)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));

    vhost = "audio.transcode.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
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
            EXPECT_STREQ("libfdk_aac", conf.get_engine_acodec(engine).c_str());
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_transcode_vn)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));

    vhost = "vn.transcode.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
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
            EXPECT_STREQ("libfdk_aac", conf.get_engine_acodec(engine).c_str());
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_transcode_copy)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));

    vhost = "copy.transcode.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_transcode_all)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));

    vhost = "all.transcode.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
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
            EXPECT_STREQ("libfdk_aac", conf.get_engine_acodec(engine).c_str());
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
            EXPECT_STREQ("libfdk_aac", conf.get_engine_acodec(engine).c_str());
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
            EXPECT_STREQ("libfdk_aac", conf.get_engine_acodec(engine).c_str());
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
            EXPECT_STREQ("libfdk_aac", conf.get_engine_acodec(engine).c_str());
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
            EXPECT_STREQ("libfdk_aac", conf.get_engine_acodec(engine).c_str());
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_transcode_ffempty)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));

    vhost = "ffempty.transcode.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
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
        EXPECT_STREQ("libfdk_aac", conf.get_engine_acodec(engine).c_str());
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_transcode_app)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));

    vhost = "app.transcode.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_transcode_stream)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));

    vhost = "stream.transcode.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_bandcheck)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));

    vhost = "bandcheck.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
    EXPECT_FALSE(conf.get_atc(vhost));
    EXPECT_TRUE(conf.get_atc_auto(vhost));
    EXPECT_TRUE(conf.get_time_jitter(vhost) == SrsRtmpJitterAlgorithmFULL);
    EXPECT_FLOAT_EQ(30, conf.get_queue_length(vhost));
    EXPECT_TRUE(NULL == conf.get_refer(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_play(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_publish(vhost));
    EXPECT_EQ(65000, conf.get_chunk_size(vhost));
    EXPECT_TRUE(NULL == conf.get_forward(vhost));
    EXPECT_FALSE(conf.get_vhost_http_hooks_enabled(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_connect(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_close(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_publish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_unpublish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_play(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_stop(vhost));
    EXPECT_TRUE(conf.get_bw_check_enabled(vhost));
    EXPECT_STREQ("35c9b402c12a7246868752e2878f7e0e", conf.get_bw_check_key(vhost).c_str());
    EXPECT_EQ(30000, conf.get_bw_check_interval_ms(vhost));
    EXPECT_EQ(4000, conf.get_bw_check_limit_kbps(vhost));
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_chunksize)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));

    vhost = "chunksize.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
    EXPECT_FALSE(conf.get_atc(vhost));
    EXPECT_TRUE(conf.get_atc_auto(vhost));
    EXPECT_TRUE(conf.get_time_jitter(vhost) == SrsRtmpJitterAlgorithmFULL);
    EXPECT_FLOAT_EQ(30, conf.get_queue_length(vhost));
    EXPECT_TRUE(NULL == conf.get_refer(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_play(vhost));
    EXPECT_TRUE(NULL == conf.get_refer_publish(vhost));
    EXPECT_EQ(128, conf.get_chunk_size(vhost));
    EXPECT_TRUE(NULL == conf.get_forward(vhost));
    EXPECT_FALSE(conf.get_vhost_http_hooks_enabled(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_connect(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_close(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_publish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_unpublish(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_play(vhost));
    EXPECT_TRUE(NULL == conf.get_vhost_on_stop(vhost));
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_jitter)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));

    vhost = "jitter.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_atc)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));

    vhost = "atc.srs.com";
    EXPECT_TRUE(conf.get_vhost_enabled(vhost));
    EXPECT_TRUE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
    EXPECT_TRUE(conf.get_atc(vhost));
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, ParseFullConf_removed)
{
    string vhost;
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_full_conf));

    vhost = "removed.srs.com";
    EXPECT_FALSE(conf.get_vhost_enabled(vhost));
    EXPECT_FALSE(conf.get_vhost_enabled(conf.get_vhost(vhost)));
    EXPECT_TRUE(conf.get_gop_cache(vhost));
    EXPECT_TRUE(conf.get_debug_srs_upnode(vhost));
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
    EXPECT_TRUE(conf.get_dvr_wait_keyframe(vhost));
    EXPECT_TRUE(SrsRtmpJitterAlgorithmFULL == conf.get_dvr_time_jitter(vhost));
    EXPECT_FALSE(conf.get_vhost_http_enabled(vhost));
    EXPECT_STREQ("[vhost]/", conf.get_vhost_http_mount(vhost).c_str());
    EXPECT_STREQ("./objs/nginx/html", conf.get_vhost_http_dir(vhost).c_str());
}

VOID TEST(ConfigMainTest, CheckConf_listen)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse("listens 1935;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse("listen 0;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse("listen -1;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse("listen -1935;"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_pid)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"pids ./objs/srs.pid;"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_chunk_size)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"chunk_size 60000;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"chunk_sizes 60000;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"chunk_size 0;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"chunk_size 1;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"chunk_size 127;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"chunk_size -1;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"chunk_size -4096;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"chunk_size 65537;"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_ff_log_dir)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"ff_log_dir ./objs;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"ff_log_dirs ./objs;"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_srs_log_level)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"srs_log_level trace;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"srs_log_levels trace;"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_srs_log_file)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"srs_log_file ./objs/srs.log;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"srs_log_files ./objs/srs.log;"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_max_connections)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"max_connections 1000;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"max_connectionss 1000;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"max_connections 0;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"max_connections 1000000;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"max_connections -1;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"max_connections -1024;"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_daemon)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"daemon on;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"daemons on;"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_heartbeat)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"heartbeat{}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"heartbeats{}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"heartbeat{enabled on;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"heartbeat{enableds on;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"heartbeat{interval 9;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"heartbeat{intervals 9;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"heartbeat{url http://127.0.0.1:8085/api/v1/servers;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"heartbeat{urls http://127.0.0.1:8085/api/v1/servers;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"heartbeat{device_id 0;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"heartbeat{device_ids 0;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"heartbeat{summaries on;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"heartbeat{summariess on;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_http_api)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"http_api{}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"http_apis{}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"http_api{enableds on;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"http_api{listens 1985;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_stats)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"stats{}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"statss{}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"stats{network 0;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"stats{networks 0;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"stats{network -100;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"stats{network -1;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"stats{disk sda;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"stats{disks sda;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_http_stream)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"http_stream{}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"http_streams{}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"http_stream{enableds on;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"http_stream{listens 8080;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"http_stream{dirs ./objs/nginx/html;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_vhost)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhosts{}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_vhost_edge)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{mode remote;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{modes remote;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{origin 127.0.0.1:1935 localhost:1935;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{origins 127.0.0.1:1935 localhost:1935;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{token_traverse off;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{token_traverses off;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_vhost_dvr)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{dvr{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{dvrs{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{dvr{enabled on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{dvr{enableds on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{dvr{dvr_path ./objs/nginx/html;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{dvr{dvr_paths ./objs/nginx/html;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{dvr{dvr_plan session;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{dvr{dvr_plans session;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{dvr{dvr_duration 30;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{dvr{dvr_durations 30;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{dvr{dvr_wait_keyframe on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{dvr{dvr_wait_keyframes on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{dvr{time_jitter full;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{dvr{time_jitters full;}}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_vhost_ingest)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{ingest{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{ingests{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{ingest{enabled on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{ingest{enableds on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{ingest{input{}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{ingest{inputs{}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{ingest{ffmpeg ./objs/ffmpeg/bin/ffmpeg;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{ingest{ffmpegs ./objs/ffmpeg/bin/ffmpeg;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{ingest{engine{}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{ingest{engines{}}}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_vhost_http)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{http{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{https{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{http{enabled on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{http{enableds on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{http{mount /hls;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{http{mounts /hls;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{http{dir ./objs/nginx/html/hls;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{http{dirs ./objs/nginx/html/hls;}}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_vhost_hls)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{hls{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{hlss{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{hls{enabled on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{hls{enableds on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{hls{hls_path ./objs/nginx/html;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{hls{hls_paths ./objs/nginx/html;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{hls{hls_fragment 10;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{hls{hls_fragments 10;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{hls{hls_window 60;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{hls{hls_windows 60;}}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_hooks)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{http_hooks{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{http_hookss{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{http_hooks{enabled on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{http_hooks{enableds on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_connect http://127.0.0.1:8085/api/v1/clients http://localhost:8085/api/v1/clients;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_connects http://127.0.0.1:8085/api/v1/clients http://localhost:8085/api/v1/clients;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_close http://127.0.0.1:8085/api/v1/clients http://localhost:8085/api/v1/clients;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_closes http://127.0.0.1:8085/api/v1/clients http://localhost:8085/api/v1/clients;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_publish http://127.0.0.1:8085/api/v1/streams http://localhost:8085/api/v1/streams;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_publishs http://127.0.0.1:8085/api/v1/streams http://localhost:8085/api/v1/streams;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_unpublish http://127.0.0.1:8085/api/v1/streams http://localhost:8085/api/v1/streams;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_unpublishs http://127.0.0.1:8085/api/v1/streams http://localhost:8085/api/v1/streams;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_play http://127.0.0.1:8085/api/v1/sessions http://localhost:8085/api/v1/sessions;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_plays http://127.0.0.1:8085/api/v1/sessions http://localhost:8085/api/v1/sessions;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_stop http://127.0.0.1:8085/api/v1/sessions http://localhost:8085/api/v1/sessions;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{http_hooks{on_stops http://127.0.0.1:8085/api/v1/sessions http://localhost:8085/api/v1/sessions;}}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_gop_cache)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{gop_cache off;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{gop_caches off;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{queue_length 10;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{queue_lengths 10;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_debug_srs_upnode)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{debug_srs_upnode off;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{debug_srs_upnodes off;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_refer)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{refer github.com github.io;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{refers github.com github.io;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{refer_publish github.com github.io;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{refer_publishs github.com github.io;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{refer_play github.com github.io;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{refer_plays github.com github.io;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_forward)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{forward 127.0.0.1:1936;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{forwards 127.0.0.1:1936;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_transcode)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcodes{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{enabled on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{enableds on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{ffmpeg ./objs/ffmpeg/bin/ffmpeg;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{ffmpegs ./objs/ffmpeg/bin/ffmpeg;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engines {}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {enabled on;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {enableds on;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vfilter {}}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vfilters {}}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vcodec libx264;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vcodecs libx264;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vbitrate 300;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vbitrates 300;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vfps 20;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vfpss 20;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vwidth 768;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vwidths 768;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vheight 320;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vheights 320;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vthreads 2;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vthreadss 2;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vprofile baseline;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vprofiles baseline;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vpreset superfast;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vpresets superfast;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vparams {}}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {vparamss {}}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {acodec libfdk_aac;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {acodecs libfdk_aac;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {abitrate 45;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {abitrates 45;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {asample_rate 44100;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {asample_rates 44100;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {achannels 2;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {achannelss 2;}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {aparams {}}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {aparamss {}}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {output rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];}}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{transcode{engine {outputs rtmp://127.0.0.1:[port]/[app]?vhost=[vhost]/[stream]_[engine];}}}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_bandcheck)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{bandcheck{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{bandchecks{}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{bandcheck{enabled on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{bandcheck{enableds on;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{bandcheck{key \"35c9b402c12a7246868752e2878f7e0e\";}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{bandcheck{keys \"35c9b402c12a7246868752e2878f7e0e\";}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{bandcheck{interval 30;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{bandcheck{intervals 30;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{bandcheck{limit_kbps 4000;}}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{bandcheck{limit_kbpss 4000;}}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_chunk_size2)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{chunk_size 128;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{chunk_sizes 128;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{chunk_size 127;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{chunk_size 0;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{chunk_size -1;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{chunk_size -128;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{chunk_size 65537;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_jitter)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{time_jitter full;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{time_jitters full;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_atc)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{atc on;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{atcs on;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{atc_auto on;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{atc_autos on;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{enabled on;}"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{enableds on;}"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_pithy_print)
{
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"pithy_print_ms 1000;"));
    }
    
    if (true) {
        MockSrsConfig conf;
        EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"pithy_print_mss 1000;"));
    }
}

VOID TEST(ConfigMainTest, CheckConf_vhost_ingest_id)
{
    MockSrsConfig conf;
    EXPECT_TRUE(ERROR_SUCCESS == conf.parse(_MIN_OK_CONF"vhost v{ingest id{}}"));
    EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{ingest id{} ingest id{}}"));
    EXPECT_TRUE(ERROR_SUCCESS != conf.parse(_MIN_OK_CONF"vhost v{ingest{} ingest{}}"));
}

#endif

