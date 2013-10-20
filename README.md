simple-rtmp-server
==================

simple rtmp origin live server over state-threads.<br/>
can be used as origin server, or rtmp-server for encoder.

Summary:<br/>
srs is a simple, high-performance, running in single process, <br/>
origin live server, with single vhost(like FMS \_\_defaultVhost\_\_).<br/>

Features:<br/>
1. simple: also stable enough.
2. high-performance: single-thread, async socket, event/st-thread driven.
3. no edge server, origin server only.
4. no vod streaming, live streaming only.
5. no vhost, \_\_defaultVhost\_\_ only.
6. no multiple processes, single process only.

Winlin
