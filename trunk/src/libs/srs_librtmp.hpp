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

#ifndef SRS_LIB_RTMP_HPP
#define SRS_LIB_RTMP_HPP

/*
#include <srs_librtmp.h>
*/

#include <sys/types.h>

/**
* srs-librtmp is a librtmp like library,
* used to play/publish rtmp stream from/to rtmp server.
* socket: use sync and block socket to connect/recv/send data with server.
* depends: no need other libraries; depends on ssl if use srs_complex_handshake.
* thread-safe: no
*/

#ifdef __cplusplus
extern "C"{
#endif

// the output handler.
typedef void* srs_rtmp_t;

/**
* create/destroy a rtmp protocol stack.
* @url rtmp url, for example: 
*         rtmp://127.0.0.1/live/livestream
* @return a rtmp handler, or NULL if error occured.
*/
srs_rtmp_t srs_rtmp_create(const char* url);
void srs_rtmp_destroy(srs_rtmp_t rtmp);

/**
* handshake with server
* category: publish/play
* previous: rtmp-create
* next: connect-app
* @return 0, success; otherwise, failed.
*/
/**
* simple handshake specifies in rtmp 1.0,
* not depends on ssl.
*/
int srs_simple_handshake(srs_rtmp_t rtmp);
/**
* complex handshake is specified by adobe Flash player,
* depends on ssl, user must compile srs with ssl, then
* link user program libssl.a and libcrypt.a
* @remark user can use srs_ssl_enabled() to detect 
* whether ssl is ok.
*/
int srs_complex_handshake(srs_rtmp_t rtmp);

/**
* connect to rtmp vhost/app
* category: publish/play
* previous: handshake
* next: publish or play
* @return 0, success; otherwise, failed.
*/
int srs_connect_app(srs_rtmp_t rtmp);

/**
* play a live/vod stream.
* category: play
* previous: connect-app
* next: destroy
* @return 0, success; otherwise, failed.
*/
int srs_play_stream(srs_rtmp_t rtmp);

/**
* publish a live stream.
* category: publish
* previous: connect-app
* next: destroy
* @return 0, success; otherwise, failed.
*/
int srs_publish_stream(srs_rtmp_t rtmp);

/**
* E.4.1 FLV Tag, page 75
*/
// 8 = audio
#define SRS_RTMP_TYPE_AUDIO 8
// 9 = video
#define SRS_RTMP_TYPE_VIDEO 9
// 18 = script data
#define SRS_RTMP_TYPE_SCRIPT 18
/**
* convert the flv tag type to string.
*     SRS_RTMP_TYPE_AUDIO to "Audio"
*     SRS_RTMP_TYPE_VIDEO to "Video"
*     SRS_RTMP_TYPE_SCRIPT to "Data"
*     otherwise, "Unknown"
*/
const char* srs_type2string(int type);
/**
* read a audio/video/script-data packet from rtmp stream.
* @param type, output the packet type, macros:
*            SRS_RTMP_TYPE_AUDIO, FlvTagAudio
*            SRS_RTMP_TYPE_VIDEO, FlvTagVideo
*            SRS_RTMP_TYPE_SCRIPT, FlvTagScript
* @param timestamp, in ms, overflow in 50days
* @param data, the packet data, according to type:
*             FlvTagAudio, @see "E.4.2.1 AUDIODATA"
*            FlvTagVideo, @see "E.4.3.1 VIDEODATA"
*            FlvTagScript, @see "E.4.4.1 SCRIPTDATA"
* @param size, size of packet.
* @return the error code. 0 for success; otherwise, error.
*
* @remark: for read, user must free the data.
* @remark: for write, user should never free the data, even if error.
*/
int srs_read_packet(srs_rtmp_t rtmp, int* type, u_int32_t* timestamp, char** data, int* size);
int srs_write_packet(srs_rtmp_t rtmp, int type, u_int32_t timestamp, char* data, int size);

/**
* whether srs is compiled with ssl,
* that is, compile srs with ssl: ./configure --with-ssl,.
* if no ssl, complex handshake always error.
* @return 0 for false, otherwise, true.
*/
int srs_ssl_enabled();

/**
* get protocol stack version
*/
int srs_version_major();
int srs_version_minor();
int srs_version_revision();

#ifdef __cplusplus
}
#endif

#endif
