simple-rtmp-server
==================

simple rtmp origin live server over state-threads.<br/>
can be used as origin server, or rtmp-server for encoder.

Summary:<br/>
srs is a simple, high-performance, running in single process, <br/>
origin live server, with single vhost(like FMS \_\_defaultVhost\_\_).<br/>

Features:<br/>
1. simple: also stable enough.<br/>
2. high-performance: single-thread, async socket, event/st-thread driven.<br/>
3. no edge server, origin server only.<br/>
4. no vod streaming, live streaming only.<br/>
5. no vhost, \_\_defaultVhost\_\_ only.<br/>
6. no multiple processes, single process only.<br/>

DevLog:<br/>
* 2013-10-23, release v0.1, support FMLE/FFMPEG publish, vp6 codec live streaming. 8287 lines.<br/>
* 2013-10-17, created.<br/>

Compare:<br/>
* srs v0.1: 8287 lines.<br/>
* nginx-rtmp v1.0.4: 26786 lines <br/>
* nginx v1.5.0: 139524 lines <br/>

Features:<br/>
* v0.2, 2013-10-23, v0.2 released.
* v0.2, 2013-10-25, support flash publish.
* v0.2, 2013-10-25, support h264/avc codec by rtmp complex handshake(SrsComplexHandshake).
* v0.2, 2013-10-24, support time jitter detect and correct algorithm(SrsConsumer::jitter_correct).
* v0.2, 2013-10-24, support decode codec type(SrsCodec) to cache the h264/avc sequence header.
* v0.1, 2013-10-23, v0.1 released.
* v0.1, 2013-10-23, support basic amf0 codec, simplify the api using c-style api.
* v0.1, 2013-10-23, support shared ptr msg(SrsSharedPtrMessage) for zero memory copy.
* v0.1, 2013-10-22, support vp6 codec with rtmp protocol specified simple handshake.
* v0.1, 2013-10-20, support multiple flash client play live streaming.
* v0.1, 2013-10-20, support FMLE/FFMPEG publish live streaming.
* v0.1, 2013-10-18, support rtmp message2chunk protocol(send message).
* v0.1, 2013-10-17, support rtmp chunk2message protocol(recv message).

Winlin
