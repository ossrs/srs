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

Winlin
