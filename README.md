Simple-RTMP-Server
==================

SRS(SIMPLE RTMP Server) over state-threads created in 2013.<br/>
SRS is a simple, [RTMP](https://github.com/winlinvip/simple-rtmp-server/wiki/DeliveryRTMP)/[HLS](https://github.com/winlinvip/simple-rtmp-server/wiki/DeliveryHLS), [high-performance](https://github.com/winlinvip/simple-rtmp-server/wiki/Performance), single/multiple(plan) processes, edge(plan)/origin live server, compile depends on [st](http://sourceforge.net/projects/state-threads), [ssl](http://www.openssl.org/) and [http-parser](https://github.com/joyent/http-parser), use [nginx](http://nginx.org/), [ffmpeg](http://ffmpeg.org/) and [cherrypy](http://www.cherrypy.org/) as tools.<br/>
SRS supports [vhost](https://github.com/winlinvip/simple-rtmp-server/wiki/RtmpUrlVhost), rtmp([encoder push](https://github.com/winlinvip/simple-rtmp-server/wiki/DeliveryRTMP), client/edge pull), ingester(srs pull), [HLS](https://github.com/winlinvip/simple-rtmp-server/wiki/DeliveryHLS), [transcoding](https://github.com/winlinvip/simple-rtmp-server/wiki/FFMPEG), [forward](https://github.com/winlinvip/simple-rtmp-server/wiki/FFMPEG), [http hooks](https://github.com/winlinvip/simple-rtmp-server/wiki/HTTPCallback). <br/>
Blog: [http://blog.csdn.net/win_lin](http://blog.csdn.net/win_lin) <br/>
See also: [https://github.com/winlinvip/simple-rtmp-server](https://github.com/winlinvip/simple-rtmp-server) <br/>
Github demo(with your SRS): [http://winlinvip.github.io/simple-rtmp-server](http://winlinvip.github.io/simple-rtmp-server) <br/>
Wiki: [https://github.com/winlinvip/simple-rtmp-server/wiki](https://github.com/winlinvip/simple-rtmp-server/wiki) <br/>
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

### Usage

<strong>Step 1:</strong> get SRS <br/>
<pre>
git clone https://github.com/winlinvip/simple-rtmp-server &&
cd simple-rtmp-server/trunk
</pre>
<strong>Step 2:</strong> build SRS,
<strong>Requires Centos6.x 64bits, others see [Build](https://github.com/winlinvip/simple-rtmp-server/wiki/Build)</strong><br/>
<pre>
./configure --with-ssl --without-hls --without-ffmpeg --without-http && make
</pre>
<strong>Step 3:</strong> start SRS <br/>
<pre>
./objs/srs -c conf/srs.conf
</pre>
<strong>See also:</strong><br/>
[Usage: How to delivery RTMP?](https://github.com/winlinvip/simple-rtmp-server/wiki/SampleRTMP)<br/>
[Usage: How to delivery HLS?](https://github.com/winlinvip/simple-rtmp-server/wiki/SampleHLS)<br/>
[Usage: How to show the demo of SRS?](https://github.com/winlinvip/simple-rtmp-server/wiki/SampleDemo)<br/>

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
Modularity Architecture:
<pre>
+------------------------------------------------------+
|             Main(srs/bandwidth/librtmp)              |
+------------------------------------------------------+
|           App(Server/Client application)             |
+------------------------------------------------------+
|               RTMP(Protocol stack)                   |
+------------------------------------------------------+
|      Kernel(depends on Core, provides error/log)     |
+------------------------------------------------------+
|         Core(depends only on system apis)            |
+------------------------------------------------------+
</pre>
Stream Architecture:
<pre>
               +---------+              +----------+
               + Publish +              +  Deliver |
               +---|-----+              +----|-----+
+------------------+-------------------------+----------------+
|     Input        | SRS(Simple RTMP Server) |     Output     |
+------------------+-------------------------+----------------+
|    Encoder(1)    |   +-> RTMP protocol ----+-> Flash Player |
|  (FMLE,FFMPEG, --+->-+-> HLS/NGINX --------+-> m3u8 player  |
|  Flash,XSPLIT,   |   +-> Fowarder ---------+-> RTMP Server  |
|  ......)         |   +-> Transcoder -------+-> RTMP Server  |
|                  |   +-> DVR --------------+-> FILE         |
|                  |   +-> BandwidthTest ----+-> Flash/StLoad |
+------------------+                         |                |
|  MediaSource(2)  |                         |                |
|  (RTSP,FILE,     |                         |                |
|   HTTP,HLS,    --+->-- Ingester -----------+-> SRS          |
|   Device,        |                         |                |
|   ......)        |                         |                |
+------------------+-------------------------+----------------+

Remark:
(1) Encoder: encoder must push RTMP stream to SRS server.
(2) MediaSource: any media source, which can be ingest by ffmpeg.
</pre>
(plan) RTMP cluster(origin/edge) Architecture:<br/>
Remark: cluster over forward, see [Cluster](https://github.com/winlinvip/simple-rtmp-server/wiki/Cluster)
<pre>
+---------+       +-----------------+     +-----------------------+ 
+ Encoder +--+-->-+  SRS(RTMP Edge) +--->-+     (RTMP Origin)     | 
+---------+  |    +-----------------+     |   SRS/FMS/NGINX-RTMP  |
             |                            |    Red5/HELIX/CRTMP   |
             +-------------------------->-+         ......        |
                                          +-----------------------+ 
Schema#1: Any RTMP encoder push RTMP stream to RTMP (origin/edge)server,
    where SRS RTMP Edge server will forward stream to origin.


+-------------+    +-----------------+      +--------------------+
| RTMP Origin +-->-+  SRS(RTMP Edge) +--+->-+  Client(RTMP/HLS)  |
+-------------+    +-----------------+  |   |  Flash/IOS/Android |
                                        |   +--------------------+
                                        |
                                        |   +-----------------+
                                        +->-+  SRS(RTMP Edge) +
                                            +-----------------+
Schema#2: SRS RTMP Edge server pull stream from origin (or upstream SRS 
    RTMP Edge server), then delivery to Client.
</pre>
(plan) SRS Multiple processes Architecture(design by wenjie):<br/>
<pre>
                 +---------------+              +--------+
                 | upnode server |              + client +
                 +-------+-------+              +---+----+
            -------------+------------network-------+---------
                         |                          |
 +--------+         +----+-----------+         +----+----------+
 | master +--fork->-+ back source(1) +-->-pull-+ stream 1-N(2) +
 +---+----+         +----------------+         +-------+-------+
     |                                                 |
     +-------------------------------------fork--->-----+
     |                           +-------------+
     +-------------------fork-->-+ http/vod(3) |
                                 +-------------+
Remark:
(1) back source process: create by master process, get stream from 
    upnode server if edge, create stream if origin, serve the stream 
    process.
(2) stream process: create by master process, get stream from back
    source process, serve the client.
(3) the embeded mininum http server, also provides vod service. for
    http server, it provides http api, hls(live/vod) delivery. for
    vod server, it slice the file to hls(m3u8/ts).
Remark:
(a) This multiple processes architecture is design by wenjie, it's a
    very simple and powerful multiple process architecture, for the
    master no need to pass between stream process.
(b) The CLI architecture is similar to this, instead, cli process
    will collect informations from all stream process, master process
    only send signals to child processes.
</pre>
CLI Architecture:
<pre>
                       +---------+
                    +--+ stream1 +---------+
                    |  +---------+         |
 +--------+         |  +---------+         |   +-------+
 | master +--fork->-+--+ streamN +---amf0--+>--+  cli  +
 +--------+         |  +---------+         |   +-------+
                    |  +-------------+     |
                    +--+ back source +-----+
                       +-------------+
Remark:
(1) master listen the global api port, for example, 33330
(2) back source and stream processes listen at private api port, 
    for example, 33331, 33332, 33333
(3) work processes(stream and back-source), report private api
    port to master global api port.
(4) cli connect to master global api port, get all other private
    api ports
(5) cli connect to each stream/back-source process to get api data,
    cli analysis and summary the data, return to user.
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
20. Support [http callback api hooks](https://github.com/winlinvip/simple-rtmp-server/wiki/HTTPCallback)(for authentication and injection).<br/>
21. Support bandwidth test api and flash client.<br/>
22. Player, publisher(encoder), and demo pages(jquery+bootstrap). <br/>
23. Demo video meeting or chat(SRS+cherrypy+jquery+bootstrap). <br/>
24. Full documents in wiki, in chineses. <br/>
25. Support RTMP(play-publish) library: srs-librtmp<br/>
26. [plan] Support system utest<br/>
27. [plan] Support embeded http server for api and hls(live/vod)<br/>
28. [plan] Support vod(file to hls stream)<br/>
29. [plan] Support stream ingester using ffmpeg.<br/>
30. [plan] Support RTSP(RTP, SDP)<br/>
31. [plan] Support network based cli and json result.<br/>
32. [plan] Support HLS cluster, use RTMP ATC to generate the TS<br/>
33. [plan] Support RTMP edge server, push/pull stream from any RTMP server<br/>
34. [plan] Support multiple processes, for both origin and edge<br/>
35. [no-plan] Support adobe flash refer/token/swf verification.<br/>
36. [no-plan] Support adobe amf3 codec.<br/>
37. [no-plan] Support dvr(record live to vod file)<br/>
38. [no-plan] Support encryption: RTMPE/RTMPS, HLS DRM<br/>
39. [no-plan] Support RTMPT, http to tranverse firewalls<br/>
40. [no-plan] Support file source, transcoding file to live stream<br/>

### Performance
1.  300 connections, 150Mbps, 500kbps, CPU 18.8%, MEM 5956KB.
2.  600 connections, 300Mbps, 500kbps, CPU 32.1%, MEM 9808KB.
3.  900 connections, 450Mbps, 500kbps, CPU 49.9%, MEM 11MB.
4. 1200 connections, 600Mbps, 500kbps, CPU 72.4%, MEM 15MB.
5. 1500 connections, 750Mbps, 500kbps, CPU 81.9%, MEM 28MB.
6. 1800 connections, 900Mbps, 500kbps, CPU 90.2%, MEM 41MB.

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
* v1.0, 2014-03-06, add gperftools for mem leak detect, mem/cpu profile.
* v1.0, 2014-03-04, add gest framework for utest, build success.
* v1.0, 2014-03-02, add wiki [srs-librtmp](https://github.com/winlinvip/simple-rtmp-server/wiki/SrsLibrtmp), [srs for arm](https://github.com/winlinvip/simple-rtmp-server/wiki/SrsLinuxArm), [product](https://github.com/winlinvip/simple-rtmp-server/wiki/Product)
* v1.0, 2014-03-02, srs-librtmp, client publish/play library like librtmp.
* v1.0, 2014-03-01, modularity, extract core/kernel/rtmp/app/main module.
* v1.0, 2014-02-28, support arm build(SRS/ST), add ssl to 3rdparty package.
* v1.0, 2014-02-28, add wiki [BuildArm](https://github.com/winlinvip/simple-rtmp-server/wiki/Build), [FFMPEG](https://github.com/winlinvip/simple-rtmp-server/wiki/FFMPEG), [Reload](https://github.com/winlinvip/simple-rtmp-server/wiki/Reload)
* v1.0, 2014-02-27, add wiki [LowLatency](https://github.com/winlinvip/simple-rtmp-server/wiki/LowLatency), [HTTPCallback](https://github.com/winlinvip/simple-rtmp-server/wiki/HTTPCallback), [ServerSideScript](https://github.com/winlinvip/simple-rtmp-server/wiki/ServerSideScript), [IDE](https://github.com/winlinvip/simple-rtmp-server/wiki/IDE)
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
* v0.8, 2013-12-08, support [http hooks](https://github.com/winlinvip/simple-rtmp-server/wiki/HTTPCallback): on_connect/close/publish/unpublish/play/stop.
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


