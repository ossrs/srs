simple-rtmp-server
==================

srs(simple rtmp origin live server) over state-threads.<br/>
srs is a simple, high-performance, running in single process, origin live server.<br/>
blog: [http://blog.csdn.net/win_lin](http://blog.csdn.net/win_lin) <br/>
see also: [https://github.com/winlinvip/simple-rtmp-server](https://github.com/winlinvip/simple-rtmp-server) <br/>
see also: [http://winlinvip.github.io/simple-rtmp-server](http://winlinvip.github.io/simple-rtmp-server)

### Contributors
winlin(winterserver): [http://blog.csdn.net/win_lin](http://blog.csdn.net/win_lin)

### Usage
step 1: build srs <br/>
<pre>
tar xf simple-rtmp-server-*.*.tar.gz
cd simple-rtmp-server-*.*/trunk
./configure --with-ssl --with-hls --with-ffmpeg
make
</pre>
step 2: start srs <br/>
<pre>
./objs/simple_rtmp_server -c conf/srs.conf
</pre>
step 3(optional): start nginx for HLS <br/>
<pre>
sudo ./objs/nginx/sbin/nginx
</pre>
step 4: publish live stream <br/>
<pre>
FMS URL: rtmp://127.0.0.1:1935/live
Stream:  livestream
For example, use ffmpeg to publish:
    ffmpeg -re -i source.flv -vcodec copy -acodec copy \
    -f flv -y rtmp://127.0.0.1:1935/live/livestream
</pre>
step 5: play live stream <br/>
<pre>
rtmp url: rtmp://127.0.0.1:1935/live/livestream
m3u8 url: http://127.0.0.1:80/live/livestream.m3u8
</pre>

### System Requirements
Supported operating systems and hardware:
* All Linux , both 32 and 64 bits
* All handware.

### Summary
1. simple: also stable enough.<br/>
2. high-performance: single-thread, async socket, event/st-thread driven.<br/>
3. no edge server, origin server only.<br/>
4. no vod streaming, live streaming only.<br/>
5. no multiple processes, single process only.<br/>
6. support vhost, support \_\_defaultVhost\_\_.<br/>
7. support adobe rtmp live streaming.<br/>
8. support apple hls(m3u8) live streaming.<br/>
9. support reload config to enable changes.<br/>
10. support cache last gop for flash player to fast startup.<br/>
11. support listen at multiple ports.<br/>
12. support long time(>4.6hours) publish/play.<br/>
13. high performace, 1800 connections(500kbps), 900Mbps, CPU 90.2%, 41MB<br/>
14. support forward publish stream to build active-standby cluster.<br/>
15. support broadcast by forward the stream to other servers(origin/edge).<br/>
16. support live stream transcoding by ffmpeg.<br/>
17. [plan] support full http callback api.<br/>
18. [plan] support network based cli and json result.<br/>
19. [plan] support bandwidth test api and flash client.<br/>
20. [plan] support adobe flash refer/token/swf verification.<br/>
21. [plan] support adobe amf3 codec.<br/>
22. [plan] support dvr(record live to vod file)<br/>
23. [plan] support FMS edge protocol<br/>
24. [plan] support encryption: RTMPE/RTMPS, HLS DRM<br/>
25. [plan] support RTMPT, http to tranverse firewalls<br/>

### Performance
1. 300 connections, 150Mbps, 500kbps, CPU 18.8%, 5956KB.
2. 600 connections, 300Mbps, 500kbps, CPU 32.1%, 9808KB.
3. 900 connections, 450Mbps, 500kbps, CPU 49.9%, 11MB.
4. 1200 connections, 600Mbps, 500kbps, CPU 72.4%, 15MB.
5. 1500 connections, 750Mbps, 500kbps, CPU 81.9%, 28MB.
6. 1800 connections, 900Mbps, 500kbps, CPU 90.2%, 41MB.
<pre>
----total-cpu-usage---- -dsk/total- ---net/lo-- ---paging-- ---system--
usr sys idl wai hiq siq| read  writ| recv  send|  in   out | int   csw 
 58   9  32   0   0   1|   0  4168k| 277M  277M|   0     0 |  29k   25k
 61   8  30   0   0   1|   0  1168k| 336M  336M|   0     0 |  29k   24k
 63   8  27   0   0   1|   0  2240k| 124M  124M|   0     0 |  32k   33k
 62   8  28   0   0   1|   0  1632k| 110M  110M|   0     0 |  31k   33k
 67   9  23   0   0   2|   0  1604k| 130M  130M|   0     0 |  33k   32k
 63   9  27   0   0   2|   0  1496k| 145M  145M|   0     0 |  32k   32k
 61   9  29   0   0   1|   0  1112k| 132M  132M|   0     0 |  32k   33k
 63   9  27   0   0   2|   0  1220k| 145M  145M|   0     0 |  32k   33k
 53   7  40   0   0   1|   0  1360k| 115M  115M|   0     0 |  24k   26k
 51   7  41   0   0   1|   0  1184k| 146M  146M|   0     0 |  24k   27k
 39   6  54   0   0   1|   0  1284k| 105M  105M|   0     0 |  22k   28k
 41   6  52   0   0   1|   0  1264k| 116M  116M|   0     0 |  25k   28k
 48   6  45   0   0   1|   0  1272k| 143M  143M|   0     0 |  27k   27k
</pre>

### Releases
* 2013-11-29, [release v0.6](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.6), support forward stream to origin/edge. 16094 lines.<br/>
* 2013-11-26, [release v0.5](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.5), support HLS(m3u8), fragment and window. 14449 lines.<br/>
* 2013-11-10, [release v0.4](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.4), support reload config, pause, longtime publish/play. 12500 lines.<br/>
* 2013-11-04, [release v0.3](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.3), support vhost, refer, gop cache, listen multiple ports. 11773 lines.<br/>
* 2013-10-25, [release v0.2](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.2), support rtmp flash publish, h264, time jitter correct. 10125 lines.<br/>
* 2013-10-23, [release v0.1](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.1), support rtmp FMLE/FFMPEG publish, vp6. 8287 lines.<br/>
* 2013-10-17, created.<br/>

### Compare
* srs v0.6: 16094 lines.<br/>
* srs v0.5: 14449 lines.<br/>
* srs v0.4: 12500 lines.<br/>
* srs v0.3: 11773 lines.<br/>
* srs v0.2: 10125 lines.<br/>
* srs v0.1: 8287 lines.<br/>
* nginx-rtmp v1.0.4: 26786 lines <br/>
* nginx v1.5.0: 139524 lines <br/>

### History
* v0.7, 2013-11-30, support live stream transcoding by ffmpeg.
* v0.7, 2013-11-30, support --with/without -ffmpeg, build ffmpeg-2.1.
* v0.7, 2013-11-30, add ffmpeg-2.1, x264-core138, lame-3.99.5, libaacplus-2.0.2.
* v0.6, 2013-11-29, v0.6 released. 16094 lines.
* v0.6, 2013-11-29, add performance summary, 1800 clients, 900Mbps, CPU 90.2%, 41MB.
* v0.6, 2013-11-29, support forward stream to other edge server.
* v0.6, 2013-11-29, support forward stream to other origin server.
* v0.6, 2013-11-28, fix memory leak bug, aac decode bug.
* v0.6, 2013-11-27, support --with or --without -hls and -ssl options.
* v0.6, 2013-11-27, support AAC 44100HZ sample rate for iphone, adjust the timestamp.
* v0.5, 2013-11-26, v0.5 released. 14449 lines.
* v0.5, 2013-11-24, support HLS(m3u8), fragment and window.
* v0.5, 2013-11-24, support record to ts file for HLS.
* v0.5, 2013-11-21, add ts_info tool to demux ts file.
* v0.5, 2013-11-16, add rtmp players(OSMF/jwplayer5/jwplayer6).
* v0.4, 2013-11-10, v0.4 released. 12500 lines.
* v0.4, 2013-11-10, support config and reload the pithy print.
* v0.4, 2013-11-09, support reload config(vhost and its detail).
* v0.4, 2013-11-09, support reload config(listen and chunk_size) by SIGHUP(1).
* v0.4, 2013-11-09, support longtime(>4.6hours) publish/play.
* v0.4, 2013-11-09, support config the chunk_size.
* v0.4, 2013-11-09, support pause for live stream.
* v0.3, 2013-11-04, v0.3 released. 11773 lines.
* v0.3, 2013-11-04, support refer/play-refer/publish-refer.
* v0.3, 2013-11-04, support vhosts specified config.
* v0.3, 2013-11-02, support listen multiple ports.
* v0.3, 2013-11-02, support config file in nginx-conf style.
* v0.3, 2013-10-29, support pithy print log message specified by stage.
* v0.3, 2013-10-28, support librtmp without extended-timestamp in 0xCX chunk packet.
* v0.3, 2013-10-27, support cache last gop for client fast startup.
* v0.2, 2013-10-25, v0.2 released. 10125 lines.
* v0.2, 2013-10-25, support flash publish.
* v0.2, 2013-10-25, support h264/avc codec by rtmp complex handshake.
* v0.2, 2013-10-24, support time jitter detect and correct algorithm
* v0.2, 2013-10-24, support decode codec type to cache the h264/avc sequence header.
* v0.1, 2013-10-23, v0.1 released. 8287 lines.
* v0.1, 2013-10-23, support basic amf0 codec, simplify the api using c-style api.
* v0.1, 2013-10-23, support shared ptr msg for zero memory copy.
* v0.1, 2013-10-22, support vp6 codec with rtmp protocol specified simple handshake.
* v0.1, 2013-10-20, support multiple flash client play live streaming.
* v0.1, 2013-10-20, support FMLE/FFMPEG publish live streaming.
* v0.1, 2013-10-18, support rtmp message2chunk protocol(send\_message).
* v0.1, 2013-10-17, support rtmp chunk2message protocol(recv\_message).

beijing, 2013
Winlin


