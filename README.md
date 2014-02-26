Simple-RTMP-Server
==================

SRS(SIMPLE RTMP Server) over state-threads created in 2013.<br/>
SRS is a simple, RTMP/HLS, high-performance, single(plan)/multiple processes, edge(plan)/origin live server.<br/>
SRS supports [vhost](https://github.com/winlinvip/simple-rtmp-server/wiki/RtmpUrlVhost), rtmp, [HLS](https://github.com/winlinvip/simple-rtmp-server/wiki/DeliveryHLS), transcoding, forward, http hooks. <br/>
Blog: [http://blog.csdn.net/win_lin](http://blog.csdn.net/win_lin) <br/>
See also: [https://github.com/winlinvip/simple-rtmp-server](https://github.com/winlinvip/simple-rtmp-server) <br/>
See also: [http://winlinvip.github.io/simple-rtmp-server](http://winlinvip.github.io/simple-rtmp-server) <br/>
TencentQQ: [http://url.cn/WAHICw](http://url.cn/WAHICw) (Group: 212189142)

### AUTHORS
The PRIMARY AUTHORS are (and/or have been)(Authors ordered by first contribution): <br/>
* winlin([winterserver](#)): [http://blog.csdn.net/win_lin](http://blog.csdn.net/win_lin) <br/>
* wenjie([wenjiegit](https://github.com/wenjiegit/simple-rtmp-server)): [http://blog.chinaunix.net/uid/25006789.html](http://blog.chinaunix.net/uid/25006789.html) <br/>

About the primary AUTHORS: <br/>
* Contribute important features to SRS. <br/>
* Names of all PRIMARY AUTHORS response in NetConnection.connect and metadata. <br/>

And here is an inevitably incomplete list of MUCH-APPRECIATED CONTRIBUTORS --<br/>
people who have submitted patches, reported bugs, added translations, helped<br/>
answer newbie questions, and generally made SRS that much better: [AUTHORS.txt](https://github.com/winlinvip/simple-rtmp-server/blob/master/AUTHORS.txt)

### Wiki
[https://github.com/winlinvip/simple-rtmp-server/wiki](https://github.com/winlinvip/simple-rtmp-server/wiki)

### Usage(simple)
<strong>Requires: Centos6.x 64bits, others see [Build](https://github.com/winlinvip/simple-rtmp-server/wiki/Build)</strong><br/>
<strong>Step -1:</strong> get SRS<br/>
<pre>
git clone https://github.com/winlinvip/simple-rtmp-server &&
cd simple-rtmp-server/trunk
</pre>
<strong>Step 0:</strong> build SRS system.<br/>
<pre>
bash scripts/build.sh
</pre>
<strong>Step 1:</strong> start SRS all demo features.<br/>
<pre>
bash scripts/run.sh
</pre>
<strong>Step 2:</strong> SRS live show: [http://your-server-ip](http://your-server-ip) <br/>
<strong>Step 3:</strong> stop SRS demo<br/>
<pre>
bash scripts/stop.sh
</pre>

### Usage(detail)
<strong>Requires: Centos6.x 64bits, others see [Build](https://github.com/winlinvip/simple-rtmp-server/wiki/Build)</strong><br/>
<strong>Step 0:</strong> get SRS <br/>
<pre>
git clone https://github.com/winlinvip/simple-rtmp-server &&
cd simple-rtmp-server/trunk
</pre>
<strong>Step 1:</strong> build SRS <br/>
<pre>
./configure --with-ssl --with-hls --with-ffmpeg --with-http && make
</pre>
<strong>Step 2:</strong> start SRS <br/>
<pre>
./objs/srs -c conf/srs.conf
</pre>
<strong>Step 3(optinal):</strong> start SRS listen at 19350 to forward to<br/>
<pre>
./objs/srs -c conf/srs.19350.conf
</pre>
<strong>Step 4(optinal):</strong> start nginx for HLS <br/>
<pre>
sudo ./objs/nginx/sbin/nginx
</pre>
<strong>Step 5(optinal):</strong> start http hooks for SRS callback <br/>
<pre>
python ./research/api-server/server.py 8085
</pre>
<strong>Step 6:</strong> publish demo live stream <br/>
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
<strong>Step 7:</strong> publish players live stream <br/>
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
<strong>Step 8:</strong> add server ip to client hosts as demo. <br/>
<pre>
# edit the folowing file:
# linux: /etc/hosts
# windows: C:\Windows\System32\drivers\etc\hosts
# where server ip is 192.168.2.111
192.168.2.111 demo.srs.com
</pre>
<strong>Step 9:</strong> play live stream. <br/>
<pre>
players: http://demo.srs.com/players
rtmp url: rtmp://demo.srs.com/live/livestream
m3u8 url: http://demo.srs.com/live/livestream.m3u8
for android: http://demo.srs.com/live/livestream.html
</pre>
<strong>Step 10(optinal):</strong> play live stream auto transcoded<br/>
<pre>
rtmp url: rtmp://demo.srs.com/live/livestream_ld
m3u8 url: http://demo.srs.com/live/livestream_ld.m3u8
for android: http://demo.srs.com/live/livestream_ld.html
rtmp url: rtmp://demo.srs.com/live/livestream_sd
m3u8 url: http://demo.srs.com/live/livestream_sd.m3u8
for android: http://demo.srs.com/live/livestream_sd.html
</pre>
<strong>Step 11(optinal):</strong> play live stream auto forwarded, the hls dir change to /forward<br/>
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
<strong>Step 12(optinal):</strong> modify the config and reload it (all features support reload)<br/>
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
|             SRS(Simple RTMP Server)                  |
+---------------+---------------+-----------+----------+
|   API/hook    |   Transcoder  |    HLS    |   RTMP   |
|  http-parser  |  FFMPEG/x264  |  NGINX/ts | protocol |
+---------------+---------------+-----------+----------+
|              Network(state-threads)                  |
+------------------------------------------------------+
|      All Linux(RHEL,CentOS,Ubuntu,Fedora...)         |
+------------------------------------------------------+
</pre>
Stream Architecture:
<pre>
        +---------+              +----------+ 
        + Publish +              +  Deliver | 
        +---|-----+              +----|-----+ 
+-----------+-------------------------+----------------+
| Encoder   | SRS(Simple RTMP Server) |     Client     |
+-----------+-------------------------+----------------+
| (FMLE,    |   +-> RTMP protocol ----+-> Flash Player |
| FFMPEG, --+-> +-> HLS/NGINX --------+-> m3u8 player  |
| Flash,    |   +-> Fowarder ---------+-> RTMP Server  |
| XSPLIT,   |   +-> Transcoder -------+-> RTMP Server  |
|  ...)     |   +-> DVR --------------+-> FILE         |
|           |   +-> BandwidthTest ----+-> Flash/StLoad |
+-----------+-------------------------+----------------+
</pre>
RTMP cluster(origin/edge) Architecture:<br/>
Remark: cluster over forward, see [Cluster](https://github.com/winlinvip/simple-rtmp-server/wiki/Cluster)
<pre>
        +---------+              +----------+ 
        + Publish +              +  Deliver | 
        +---|-----+              +----|-----+ 
+-----------+-------------------------+----------------+
| Encoder   | SRS(Simple RTMP Server) |     Client     |
+-----------+-------------------------+----------------+
| (FMLE,    |   +-> RTMP protocol ----+-> Flash Player |
| FFMPEG, --+-> +-> HLS/NGINX --------+-> m3u8 player  |
| Flash,    |   +-> Fowarder ---------+-> RTMP Server  |
| XSPLIT,   |   +-> Transcoder -------+-> RTMP Server  |
|  ...)     |   +-> DVR --------------+-> FILE         |
|           |   +-> BandwidthTest ----+-> Flash/StLoad |
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
         
@See: class SrsBandwidth comments.
</pre>

### System Requirements
Supported operating systems and hardware:
* All Linux , both 32 and 64 bits
* All handware.

### Summary
1. Simple: also stable enough.<br/>
2. [High-performance](https://github.com/winlinvip/simple-rtmp-server/wiki/Performance): single-thread, async socket, event/st-thread driven.<br/>
3. With RTMP edge server, support origin server.<br/>
4. NO vod streaming, live streaming only.<br/>
5. With multiple processes, support single process.<br/>
6. Support vhost, support \_\_defaultVhost\_\_.<br/>
7. Support adobe rtmp live streaming.<br/>
8. Support apple [HLS(m3u8)](https://github.com/winlinvip/simple-rtmp-server/wiki/DeliveryHLS) live streaming.<br/>
9. Support reload config to enable changes.<br/>
10. Support cache last gop for flash player to fast startup.<br/>
11. Support listen at multiple ports.<br/>
12. Support long time(>4.6hours) publish/play.<br/>
13. High performace, 1800 connections(500kbps), 900Mbps, CPU 90.2%, 41MB<br/>
14. Support forward publish stream to build active-standby [cluster](https://github.com/winlinvip/simple-rtmp-server/wiki/Cluster).<br/>
15. Support broadcast by forward the stream to other servers(origin/edge).<br/>
16. Support live stream transcoding by ffmpeg.<br/>
17. Support live stream forward(acopy/vcopy) by ffmpeg.<br/>
18. Support ffmpeg filters(logo/overlay/crop), x264 params.<br/>
19. Support audio transcode only, speex/mp3 to aac<br/>
20. Support http callback api hooks(for authentication and injection).<br/>
21. Support bandwidth test api and flash client.<br/>
22. Player, publisher(encoder), and demo pages(jquery+bootstrap). <br/>
23. Demo video meeting or chat(SRS+cherrypy+jquery+bootstrap). <br/>
24. [dev] Full documents in wiki, in chineses. <br/>
25. [plan] Support RTMP edge server<br/>
26. [plan] Support multiple processes<br/>
27. [plan] Support network based cli and json result.<br/>
28. [no-plan] Support adobe flash refer/token/swf verification.<br/>
29. [no-plan] Support adobe amf3 codec.<br/>
30. [no-plan] Support dvr(record live to vod file)<br/>
31. [no-plan] Support encryption: RTMPE/RTMPS, HLS DRM<br/>
32. [no-plan] Support RTMPT, http to tranverse firewalls<br/>
33. [no-plan] Support file source, transcoding file to live stream<br/>

### Performance
1. 300 connections, 150Mbps, 500kbps, CPU 18.8%, 5956KB.
2. 600 connections, 300Mbps, 500kbps, CPU 32.1%, 9808KB.
3. 900 connections, 450Mbps, 500kbps, CPU 49.9%, 11MB.
4. 1200 connections, 600Mbps, 500kbps, CPU 72.4%, 15MB.
5. 1500 connections, 750Mbps, 500kbps, CPU 81.9%, 28MB.
6. 1800 connections, 900Mbps, 500kbps, CPU 90.2%, 41MB.

<pre>
[winlin@dev6 srs]$ dstat
----total-cpu-usage---- -dsk/total- ---net/lo-- ---paging-- ---system--
usr sys idl wai hiq siq| read  writ| recv  send|  in   out | int   csw 
 58   9  32   0   0   1|   0  4168k| 277M  277M|   0     0 |  29k   25k
 61   8  30   0   0   1|   0  1168k| 336M  336M|   0     0 |  29k   24k
 63   8  27   0   0   1|   0  2240k| 124M  124M|   0     0 |  32k   33k
 62   8  28   0   0   1|   0  1632k| 110M  110M|   0     0 |  31k   33k
 53   7  40   0   0   1|   0  1360k| 115M  115M|   0     0 |  24k   26k
 51   7  41   0   0   1|   0  1184k| 146M  146M|   0     0 |  24k   27k
 39   6  54   0   0   1|   0  1284k| 105M  105M|   0     0 |  22k   28k
 41   6  52   0   0   1|   0  1264k| 116M  116M|   0     0 |  25k   28k
 48   6  45   0   0   1|   0  1272k| 143M  143M|   0     0 |  27k   27k
</pre>
See also: [Performance Test Guide](https://github.com/winlinvip/simple-rtmp-server/wiki/Performance)

### Releases
* 2013-12-25, [Release v0.9](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.9), support bandwidth test, player/encoder/chat demos. 20926 lines.<br/>
* 2013-12-08, [Release v0.8](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.8), support http hooks callback, update [st_load](https://github.com/winlinvip/st-load). 19186 lines.<br/>
* 2013-12-03, [Release v0.7](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.7), support live stream transcoding. 17605 lines.<br/>
* 2013-11-29, [Release v0.6](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.6), support forward stream to origin/edge. 16094 lines.<br/>
* 2013-11-26, [Release v0.5](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.5), support HLS(m3u8), fragment and window. 14449 lines.<br/>
* 2013-11-10, [Release v0.4](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.4), support reload config, pause, longtime publish/play. 12500 lines.<br/>
* 2013-11-04, [Release v0.3](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.3), support vhost, refer, gop cache, listen multiple ports. 11773 lines.<br/>
* 2013-10-25, [Release v0.2](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.2), support rtmp flash publish, h264, time jitter correct. 10125 lines.<br/>
* 2013-10-23, [Release v0.1](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.1), support rtmp FMLE/FFMPEG publish, vp6. 8287 lines.<br/>
* 2013-10-17, Created.<br/>

### Compare
* SRS v0.9: 20926 lines. player/encoder/chat demos. bandwidth test for encoder/CDN.<br/>
* SRS v0.8: 19186 lines. implements http hooks refer to [nginx-rtmp](https://github.com/arut/nginx-rtmp-module). <br/>
* SRS v0.7: 17605 lines. implements transcoding(FFMPEG) feature refer to [wowza](http://www.wowza.com). <br/>
* SRS v0.6: 16094 lines. important feature forward for CDN. <br/>
* SRS v0.5: 14449 lines. implements HLS feature refer to [nginx-rtmp](https://github.com/arut/nginx-rtmp-module). <br/>
* SRS v0.4: 12500 lines. important feature reload for CDN. <br/>
* SRS v0.3: 11773 lines. implements vhost feature refer to [FMS](http://www.adobe.com/products/adobe-media-server-family.html). <br/>
* SRS v0.2: 10125 lines. implements rtmp protocol stack refer to [nginx-rtmp](https://github.com/arut/nginx-rtmp-module). <br/>
* SRS v0.1: 8287 lines. base on state-threads. <br/>
* nginx-rtmp v1.0.4: 26786 lines <br/>
* nginx v1.5.0: 139524 lines <br/>

### History
* v1.0, 2014-01-19, add wiki [DeliveryHLS](https://github.com/winlinvip/simple-rtmp-server/wiki/DeliveryHLS)
* v1.0, 2014-01-12, add wiki [HowToAskQuestion](https://github.com/winlinvip/simple-rtmp-server/wiki/HowToAskQuestion), [RtmpUrlVhost](https://github.com/winlinvip/simple-rtmp-server/wiki/RtmpUrlVhost)
* v1.0, 2014-01-11, fix jw/flower player pause bug, which send closeStream actually.
* v1.0, 2014-01-05, add wiki [Build](https://github.com/winlinvip/simple-rtmp-server/wiki/Build), [Performance](https://github.com/winlinvip/simple-rtmp-server/wiki/Performance), [Cluster](https://github.com/winlinvip/simple-rtmp-server/wiki/Cluster)
* v1.0, 2014-01-01, change listen(512), chunk-size(60000), to improve performance.
* v1.0, 2013-12-27, merge from wenjie, the bandwidth test feature.
* v0.9, 2013-12-25, [v0.9](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.9) released. 20926 lines.
* v0.9, 2013-12-25, fix the bitrate bug(in Bps), use enhanced microphone.
* v0.9, 2013-12-22, demo video meeting or chat(SRS+cherrypy+jquery+bootstrap).
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
* v0.8, 2013-12-07, update http/hls/rtmp load test tool [st_load](https://github.com/winlinvip/st-load), use SRS rtmp sdk.
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

Beijing, 2013<br/>
Winlin


