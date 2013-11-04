simple-rtmp-server
==================

simple rtmp origin live server over state-threads, which can be used as origin server, or rtmp-server for encoder.<br/>
srs is a simple, high-performance, running in single process, origin live server, with single vhost(like FMS \_\_defaultVhost\_\_).<br/>
blog: [http://blog.csdn.net/win_lin](http://blog.csdn.net/win_lin) <br/>
see also: [https://github.com/winlinvip/simple-rtmp-server](https://github.com/winlinvip/simple-rtmp-server) <br/>
see also: [http://winlinvip.github.io/simple-rtmp-server](http://winlinvip.github.io/simple-rtmp-server)

### Usage
step 1: build srs <br/>
<pre>
tar xf srs.*.*.tar.gz
cd srs.*.*
./configure
make
./objs/simple_rtmp_server -c conf/srs.conf
</pre>
step 2: publish live stream <br/>
<pre>
FMS URL: rtmp://127.0.0.1:1935/live
Stream:  livestream
</pre>
step 3: play live stream <br/>
<pre>
url: rtmp://127.0.0.1:1935/live/livestream
</pre>

### Summary
1. simple: also stable enough.<br/>
2. high-performance: single-thread, async socket, event/st-thread driven.<br/>
3. no edge server, origin server only.<br/>
4. no vod streaming, live streaming only.<br/>
5. no vhost, \_\_defaultVhost\_\_ only.<br/>
6. no multiple processes, single process only.<br/>

### Releases
* 2013-10-25, [release v0.2](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.2), support flash publish, h264 codec, time jitter correct. 10125 lines.<br/>
* 2013-10-23, [release v0.1](https://github.com/winlinvip/simple-rtmp-server/releases/tag/0.1), support FMLE/FFMPEG publish, vp6 codec live streaming. 8287 lines.<br/>
* 2013-10-17, created.<br/>

### Compare
* srs v0.2: 10125 lines.<br/>
* srs v0.1: 8287 lines.<br/>
* nginx-rtmp v1.0.4: 26786 lines <br/>
* nginx v1.5.0: 139524 lines <br/>

### History
* v0.3, 2013-11-04, support vhosts specified config.
* v0.3, 2013-11-02, support listen multiple ports.
* v0.3, 2013-11-02, support config file in nginx-conf style.
* v0.3, 2013-10-29, support pithy print log message specified by stage.
* v0.3, 2013-10-28, support librtmp without extended-timestamp in 0xCX chunk packet.
* v0.3, 2013-10-27, support cache last gop for client fast startup.
* v0.2, 2013-10-25, v0.2 released. 10125 lines.
* v0.2, 2013-10-25, support flash publish.
* v0.2, 2013-10-25, support h264/avc codec by rtmp complex handshake(SrsComplexHandshake).
* v0.2, 2013-10-24, support time jitter detect and correct algorithm(SrsConsumer::jitter_correct).
* v0.2, 2013-10-24, support decode codec type(SrsCodec) to cache the h264/avc sequence header.
* v0.1, 2013-10-23, v0.1 released. 8287 lines.
* v0.1, 2013-10-23, support basic amf0 codec, simplify the api using c-style api.
* v0.1, 2013-10-23, support shared ptr msg(SrsSharedPtrMessage) for zero memory copy.
* v0.1, 2013-10-22, support vp6 codec with rtmp protocol specified simple handshake.
* v0.1, 2013-10-20, support multiple flash client play live streaming.
* v0.1, 2013-10-20, support FMLE/FFMPEG publish live streaming.
* v0.1, 2013-10-18, support rtmp message2chunk protocol(send\_message).
* v0.1, 2013-10-17, support rtmp chunk2message protocol(recv\_message).

Winlin
