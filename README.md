simple-rtmp-server
==================

srs(simple rtmp origin live server) over state-threads.<br/>
srs is a simple, high-performance, running in single process, origin live server.<br/>
srs supports vhost, rtmp, HLS, transcoding, forward, http hooks. <br/>
blog: [http://blog.csdn.net/win_lin](http://blog.csdn.net/win_lin) <br/>
see also: [https://github.com/winlinvip/simple-rtmp-server](https://github.com/winlinvip/simple-rtmp-server) <br/>
see also: [http://winlinvip.github.io/simple-rtmp-server](http://winlinvip.github.io/simple-rtmp-server)

### Contributors
winlin([winterserver](#)): [http://blog.csdn.net/win_lin](http://blog.csdn.net/win_lin) <br/>
wenjie([wenjiegit](https://github.com/wenjiegit/simple-rtmp-server)): [http://blog.chinaunix.net/uid/25006789.html](http://blog.chinaunix.net/uid/25006789.html) <br/>
who is the contributors: <br/>
1. contribute important features to srs. <br/>
2. the name of all contributors will send in the response of NetConnection.connect and metadata. 

### Usage(simple)
<strong>step 1:</strong> get srs <br/>
<pre>
tar xf simple-rtmp-server-*.*.tar.gz
cd simple-rtmp-server-*.*/trunk
</pre>
or get the latest code:<br/>
<pre>
git clone https://github.com/winlinvip/simple-rtmp-server
cd simple-rtmp-server/trunk
</pre>
<strong>step 2:</strong> build and start srs all demo features.<br/>
<pre>
bash scripts/demo.sh
</pre>
<strong>step 3:</strong> srs live show: [http://demo.srs.com/players](http://demo.srs.com/players) <br/>
requires client add server ip to hosts. <br/>
<pre>
# edit the folowing file:
# linux: /etc/hosts
# windows: C:\Windows\System32\drivers\etc\hosts
# where server ip is 192.168.2.111
192.168.2.111 demo.srs.com
</pre>
<strong>step 4:</strong> stop srs demo<br/>
<pre>
bash scripts/stop.sh
</pre>

### Usage(detail)
<strong>step 1:</strong> build srs <br/>
<pre>
tar xf simple-rtmp-server-*.*.tar.gz
cd simple-rtmp-server-*.*/trunk
./configure --with-ssl --with-hls --with-ffmpeg --with-http
make
</pre>
or get the latest code:<br/>
<pre>
git clone  https://github.com/winlinvip/simple-rtmp-server
cd simple-rtmp-server/trunk
./configure --with-ssl --with-hls --with-ffmpeg --with-http
</pre>
<strong>step 2:</strong> start srs <br/>
<pre>
./objs/srs -c conf/srs.conf
</pre>
<strong>step 3(optinal):</strong> start srs listen at 19350 to forward to<br/>
<pre>
./objs/srs -c conf/srs.19350.conf
</pre>
<strong>step 4(optinal):</strong> start nginx for HLS <br/>
<pre>
sudo ./objs/nginx/sbin/nginx
</pre>
<strong>step 5(optinal):</strong> start http hooks for srs callback <br/>
<pre>
python ./research/api-server/server.py 8085
</pre>
<strong>step 6:</strong> publish demo live stream <br/>
<pre>
FMS URL: rtmp://127.0.0.1/live?vhost=demo.srs.com
Stream:  livestream
FFMPEG to publish the default demo stream:
    for((;;)); do \
        ./objs/ffmpeg/bin/ffmpeg -re -i ./doc/source.200kbps.768x320.flv \
        -vcodec copy -acodec copy \
        -f flv -y rtmp://127.0.0.1/live?vhost=demo.srs.com/livestream; \
        sleep 1; \
    done
</pre>
<strong>step 7:</strong> publish players live stream <br/>
<pre>
FMS URL: rtmp://127.0.0.1/live?vhost=players
Stream:  livestream
FFMPEG to publish the players demo stream:
    for((;;)); do \
        ./objs/ffmpeg/bin/ffmpeg -re -i ./doc/source.200kbps.768x320.flv \
        -vcodec copy -acodec copy \
        -f flv -y rtmp://127.0.0.1/live?vhost=players/livestream; \
        sleep 1; \
    done
</pre>
<strong>step 8:</strong> add server ip to client hosts as demo. <br/>
<pre>
# edit the folowing file:
# linux: /etc/hosts
# windows: C:\Windows\System32\drivers\etc\hosts
# where server ip is 192.168.2.111
192.168.2.111 demo.srs.com
</pre>
<strong>step 9:</strong> play live stream. <br/>
<pre>
players: http://demo.srs.com/players
rtmp url: rtmp://demo.srs.com/live/livestream
m3u8 url: http://demo.srs.com/live/livestream.m3u8
for android: http://demo.srs.com/live/livestream.html
</pre>
<strong>step 10(optinal):</strong> play live stream auto transcoded<br/>
<pre>
rtmp url: rtmp://demo.srs.com/live/livestream_ld
m3u8 url: http://demo.srs.com/live/livestream_ld.m3u8
for android: http://demo.srs.com/live/livestream_ld.html
rtmp url: rtmp://demo.srs.com/live/livestream_sd
m3u8 url: http://demo.srs.com/live/livestream_sd.m3u8
for android: http://demo.srs.com/live/livestream_sd.html
</pre>
<strong>step 11(optinal):</strong> play live stream auto forwarded, the hls dir change to /forward<br/>
<pre>
rtmp url: rtmp://demo.srs.com:19350/live/livestream
m3u8 url: http://demo.srs.com/forward/live/livestream.m3u8
for android: http://demo.srs.com/forward/live/livestream.html
rtmp url: rtmp://demo.srs.com:19350/live/livestream_ld
m3u8 url: http://demo.srs.com/forward/live/livestream_ld.m3u8
for android: http://demo.srs.com/forward/live/livestream_ld.html
rtmp url: rtmp://demo.srs.com:19350/live/livestream_sd
m3u8 url: http://demo.srs.com/forward/live/livestream_sd.m3u8
for android: http://demo.srs.com/forward/live/livestream_sd.html
</pre>
<strong>step 12(optinal):</strong> modify the config and reload it (all features support reload)<br/>
<pre>
killall -1 srs
</pre>
or use specified signal to reload:<br/>
<pre>
killall -s SIGHUP srs
</pre>

### Architecture
System Architecture:
<pre>
+------------------------------------------------------+
|             SRS(Simple Rtmp Server)                  |
+---------------+---------------+-----------+----------+
|   API/hook    |   Transcoder  |    HLS    |   RTMP   |
|  http-parser  |  FFMPEG/x264  |  NGINX/ts | protocol |
+---------------+---------------+-----------+----------+
|              Network(state-threads)                  |
+------------------------------------------------------+
|      All Linux(RHEL,Centos,Ubuntu,Fedora...)         |
+------------------------------------------------------+
</pre>
Stream Architecture:
<pre>
        +---------+              +----------+ 
        + Publish +              +  Deliver | 
        +---|-----+              +----|-----+ 
+-----------+-------------------------+----------------+
| Encoder   | SRS(Simple Rtmp Server) |     Client     |
+-----------+-------------------------+----------------+
| (FMLE,    |   +-> RTMP protocol ----+-> Flash Player |
| FFMPEG, --+-> +-> HLS/NGINX --------+-> m3u8 player  |
| Flash,    |   +-> Fowarder ---------+-> RTMP Server  |
| XSPLIT,   |   +-> Transcoder -------+-> RTMP Server  |
|  ...)     |   +-> DVR --------------+-> FILE         |
|           |   +-> BandwidthTest-----+-> Flash/StLoad |
+-----------+-------------------------+----------------+
</pre>
Bandwidth Test Workflow:
<pre>
   +------------+                    +----------+
   |  Client    |                    |  Server  |
   +-----+------+                    +-----+----+
         |                                 |
         |   connect vhost------------->   |
         |   &lt;-----------result(success)   |
         |                                 |
         |   &lt;----------call(start play)   |
         |   result(playing)---------->    |
         |   &lt;-------------data(playing)   |
         |   &lt;-----------call(stop play)   |
         |   result(stopped)---------->    |
         |                                 |
         |   &lt;-------call(start publish)   |
         |   result(publishing)------->    |
         |   data(publishing)--------->    |
         |   &lt;--------call(stop publish)   |
         |   result(stopped)(1)------->    |
         |                                 |
         |   &lt;--------------------report   |
         |   final(2)----------------->    |
         |           &lt;END>                 |
         
@see: class SrsBandwidth comments.
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
17. support live stream forward(acopy/vcopy) by ffmpeg.<br/>
18. support ffmpeg filters(logo/overlay/crop), x264 params.<br/>
19. support audio transcode only, speex/mp3 to aac<br/>
20. support http callback api hooks(for authentication and injection).<br/>
21. support bandwidth test api and flash client.<br/>
22. player, publisher(encoder), and demo pages(jquery+bootstrap). <br/>
23. demo video meeting or chat(srs+cherrypy+jquery+bootstrap). <br/>
24. [plan] support network based cli and json result.<br/>
25. [plan] support adobe flash refer/token/swf verification.<br/>
26. [plan] support adobe amf3 codec.<br/>
27. [plan] support dvr(record live to vod file)<br/>
28. [plan] support FMS edge protocol<br/>
29. [plan] support encryption: RTMPE/RTMPS, HLS DRM<br/>
30. [plan] support RTMPT, http to tranverse firewalls<br/>
31. [plan] support file source, transcoding file to live stream<br/>

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
* 2013-12-08, [release v0.8](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.8), support http hooks callback, update st-load. 19186 lines.<br/>
* 2013-12-03, [release v0.7](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.7), support live stream transcoding. 17605 lines.<br/>
* 2013-11-29, [release v0.6](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.6), support forward stream to origin/edge. 16094 lines.<br/>
* 2013-11-26, [release v0.5](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.5), support HLS(m3u8), fragment and window. 14449 lines.<br/>
* 2013-11-10, [release v0.4](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.4), support reload config, pause, longtime publish/play. 12500 lines.<br/>
* 2013-11-04, [release v0.3](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.3), support vhost, refer, gop cache, listen multiple ports. 11773 lines.<br/>
* 2013-10-25, [release v0.2](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.2), support rtmp flash publish, h264, time jitter correct. 10125 lines.<br/>
* 2013-10-23, [release v0.1](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.1), support rtmp FMLE/FFMPEG publish, vp6. 8287 lines.<br/>
* 2013-10-17, created.<br/>

### Compare
* srs v0.8: 19186 lines. implements http hooks refer to [nginx-rtmp](https://github.com/arut/nginx-rtmp-module). <br/>
* srs v0.7: 17605 lines. implements transcoding(FFMPEG) feature refer to [wowza](http://www.wowza.com). <br/>
* srs v0.6: 16094 lines. important feature forward for CDN. <br/>
* srs v0.5: 14449 lines. implements HLS feature refer to [nginx-rtmp](https://github.com/arut/nginx-rtmp-module). <br/>
* srs v0.4: 12500 lines. important feature reload for CDN. <br/>
* srs v0.3: 11773 lines. implements vhost feature refer to [FMS](http://www.adobe.com/products/adobe-media-server-family.html). <br/>
* srs v0.2: 10125 lines. implements rtmp protocol stack refer to [nginx-rtmp](https://github.com/arut/nginx-rtmp-module). <br/>
* srs v0.1: 8287 lines. base on state-threads. <br/>
* nginx-rtmp v1.0.4: 26786 lines <br/>
* nginx v1.5.0: 139524 lines <br/>

### History
* v0.9, 2013-12-22, demo video meeting or chat(srs+cherrypy+jquery+bootstrap).
* v0.9, 2013-12-22, merge from wenjie, support banwidth test.
* v0.9, 2013-12-22, merge from wenjie: support set chunk size at vhost level
* v0.9, 2013-12-21, add [players](http://demo.srs.com/players) for play and publish.
* v0.9, 2013-12-15, ensure the HLS(ts) is continous when republish stream.
* v0.9, 2013-12-15, fix the hls reload bug, feed it the sequence header.
* v0.9, 2013-12-15, refine protocol, use int64_t timestamp for ts and jitter.
* v0.9, 2013-12-15, support set the live queue length(in seconds), drop when full.
* v0.9, 2013-12-15, fix the forwarder reconnect bug, feed it the sequence header.
* v0.9, 2013-12-15, support reload the hls/forwarder/transcoder.
* v0.9, 2013-12-14, refine the thread model for the retry threads.
* v0.9, 2013-12-10, auto install depends tools/libs on centos/ubuntu.
* v0.8, 2013-12-08, [v0.8](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.8) released. 19186 lines.
* v0.8, 2013-12-08, support http hooks: on_connect/close/publish/unpublish/play/stop.
* v0.8, 2013-12-08, support multiple http hooks for a event.
* v0.8, 2013-12-07, support http callback hooks, on_connect.
* v0.8, 2013-12-07, support network based cli and json result, add CherryPy 3.2.4.
* v0.8, 2013-12-07, update http/hls/rtmp load test tool [st_load](https://github.com/winlinvip/st-load), use srs rtmp sdk.
* v0.8, 2013-12-06, support max_connections, drop if exceed.
* v0.8, 2013-12-05, support log_dir, write ffmpeg log to file.
* v0.8, 2013-12-05, fix the forward/hls/encoder bug.
* v0.7, 2013-12-03, [v0.7](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.7) released. 17605 lines.
* v0.7, 2013-12-01, support dead-loop detect for forwarder and transcoder.
* v0.7, 2013-12-01, support all ffmpeg filters and params.
* v0.7, 2013-11-30, support live stream transcoder by ffmpeg.
* v0.7, 2013-11-30, support --with/without -ffmpeg, build ffmpeg-2.1.
* v0.7, 2013-11-30, add ffmpeg-2.1, x264-core138, lame-3.99.5, libaacplus-2.0.2.
* v0.6, 2013-11-29, [v0.6](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.6) released. 16094 lines.
* v0.6, 2013-11-29, add performance summary, 1800 clients, 900Mbps, CPU 90.2%, 41MB.
* v0.6, 2013-11-29, support forward stream to other edge server.
* v0.6, 2013-11-29, support forward stream to other origin server.
* v0.6, 2013-11-28, fix memory leak bug, aac decode bug.
* v0.6, 2013-11-27, support --with or --without -hls and -ssl options.
* v0.6, 2013-11-27, support AAC 44100HZ sample rate for iphone, adjust the timestamp.
* v0.5, 2013-11-26, [v0.5](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.5) released. 14449 lines.
* v0.5, 2013-11-24, support HLS(m3u8), fragment and window.
* v0.5, 2013-11-24, support record to ts file for HLS.
* v0.5, 2013-11-21, add ts_info tool to demux ts file.
* v0.5, 2013-11-16, add rtmp players(OSMF/jwplayer5/jwplayer6).
* v0.4, 2013-11-10, [v0.4](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.4) released. 12500 lines.
* v0.4, 2013-11-10, support config and reload the pithy print.
* v0.4, 2013-11-09, support reload config(vhost and its detail).
* v0.4, 2013-11-09, support reload config(listen and chunk_size) by SIGHUP(1).
* v0.4, 2013-11-09, support longtime(>4.6hours) publish/play.
* v0.4, 2013-11-09, support config the chunk_size.
* v0.4, 2013-11-09, support pause for live stream.
* v0.3, 2013-11-04, [v0.3](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.3) released. 11773 lines.
* v0.3, 2013-11-04, support refer/play-refer/publish-refer.
* v0.3, 2013-11-04, support vhosts specified config.
* v0.3, 2013-11-02, support listen multiple ports.
* v0.3, 2013-11-02, support config file in nginx-conf style.
* v0.3, 2013-10-29, support pithy print log message specified by stage.
* v0.3, 2013-10-28, support librtmp without extended-timestamp in 0xCX chunk packet.
* v0.3, 2013-10-27, support cache last gop for client fast startup.
* v0.2, 2013-10-25, [v0.2](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.2) released. 10125 lines.
* v0.2, 2013-10-25, support flash publish.
* v0.2, 2013-10-25, support h264/avc codec by rtmp complex handshake.
* v0.2, 2013-10-24, support time jitter detect and correct algorithm
* v0.2, 2013-10-24, support decode codec type to cache the h264/avc sequence header.
* v0.1, 2013-10-23, [v0.1](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.1) released. 8287 lines.
* v0.1, 2013-10-23, support basic amf0 codec, simplify the api using c-style api.
* v0.1, 2013-10-23, support shared ptr msg for zero memory copy.
* v0.1, 2013-10-22, support vp6 codec with rtmp protocol specified simple handshake.
* v0.1, 2013-10-20, support multiple flash client play live streaming.
* v0.1, 2013-10-20, support FMLE/FFMPEG publish live streaming.
* v0.1, 2013-10-18, support rtmp message2chunk protocol(send\_message).
* v0.1, 2013-10-17, support rtmp chunk2message protocol(recv\_message).

beijing, 2013
Winlin


