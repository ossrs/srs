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
* connect and handshake with server
* category: publish/play
* previous: rtmp-create
* next: connect-app
* @return 0, success; otherwise, failed.
*/
/**
* simple handshake specifies in rtmp 1.0,
* not depends on ssl.
*/
/**
* srs_simple_handshake equals to invoke:
*       __srs_dns_resolve()
*       __srs_connect_server()
*       __srs_do_simple_handshake()
* user can use these functions if needed.
*/
int srs_simple_handshake(srs_rtmp_t rtmp);
// parse uri, create socket, resolve host
int __srs_dns_resolve(srs_rtmp_t rtmp);
// connect socket to server
int __srs_connect_server(srs_rtmp_t rtmp);
// do simple handshake over socket.
int __srs_do_simple_handshake(srs_rtmp_t rtmp);

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
* @remark user never free the return char*, 
*   it's static shared const string.
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
* get protocol stack version
*/
int srs_version_major();
int srs_version_minor();
int srs_version_revision();

/**
* utilities
*/
int64_t srs_get_time_ms();
int64_t srs_get_nsend_bytes(srs_rtmp_t rtmp);
int64_t srs_get_nrecv_bytes(srs_rtmp_t rtmp);

/**
* flv codec
*/
typedef void* srs_flv_t;
typedef int flv_bool;
/* open flv file for both read/write. */
srs_flv_t srs_flv_open(const char* file);
void srs_flv_close(srs_flv_t flv);
/* read the flv header. 9bytes header. drop the 4bytes zero previous tag size */
int srs_flv_read_header(srs_flv_t flv, char header[9]);
/* read the flv tag header, 1bytes tag, 3bytes data_size, 4bytes time, 3bytes stream id. */
int srs_flv_read_tag_header(srs_flv_t flv, char* ptype, int32_t* pdata_size, u_int32_t* ptime);
/* read the tag data. drop the 4bytes previous tag size */
int srs_flv_read_tag_data(srs_flv_t flv, char* data, int32_t size);
/* write flv header to file, auto write the 4bytes zero previous tag size. */
int srs_flv_write_header(srs_flv_t flv, char header[9]);
/* write flv tag to file, auto write the 4bytes previous tag size */
int srs_flv_write_tag(srs_flv_t flv, char type, int32_t time, char* data, int size);
/* get the tag size, for flv injecter to adjust offset, size=tag_header+data+previous_tag */
int srs_flv_size_tag(int data_size);
/* file stream */
/* file stream tellg to get offset */
int64_t srs_flv_tellg(srs_flv_t flv);
/* seek file stream, offset is form the start of file */
void srs_flv_lseek(srs_flv_t flv, int64_t offset);
/* error code */
/* whether the error code indicates EOF */
flv_bool srs_flv_is_eof(int error_code);
/* media codec */
/* whether the video body is sequence header */
flv_bool srs_flv_is_sequence_header(char* data, int32_t size);
/* whether the video body is keyframe */
flv_bool srs_flv_is_keyframe(char* data, int32_t size);

/**
* amf0 codec
*/
/* the output handler. */
typedef void* srs_amf0_t;
typedef int amf0_bool;
typedef double amf0_number;
srs_amf0_t srs_amf0_parse(char* data, int size, int* nparsed);
srs_amf0_t srs_amf0_create_number(amf0_number value);
srs_amf0_t srs_amf0_create_ecma_array();
srs_amf0_t srs_amf0_create_strict_array();
srs_amf0_t srs_amf0_create_object();
void srs_amf0_free(srs_amf0_t amf0);
void srs_amf0_free_bytes(char* data);
/* size and to bytes */
int srs_amf0_size(srs_amf0_t amf0);
int srs_amf0_serialize(srs_amf0_t amf0, char* data, int size);
/* type detecter */
amf0_bool srs_amf0_is_string(srs_amf0_t amf0);
amf0_bool srs_amf0_is_boolean(srs_amf0_t amf0);
amf0_bool srs_amf0_is_number(srs_amf0_t amf0);
amf0_bool srs_amf0_is_null(srs_amf0_t amf0);
amf0_bool srs_amf0_is_object(srs_amf0_t amf0);
amf0_bool srs_amf0_is_ecma_array(srs_amf0_t amf0);
amf0_bool srs_amf0_is_strict_array(srs_amf0_t amf0);
/* value converter */
const char* srs_amf0_to_string(srs_amf0_t amf0);
amf0_bool srs_amf0_to_boolean(srs_amf0_t amf0);
amf0_number srs_amf0_to_number(srs_amf0_t amf0);
/* value setter */
void srs_amf0_set_number(srs_amf0_t amf0, amf0_number value);
/* object value converter */
int srs_amf0_object_property_count(srs_amf0_t amf0);
const char* srs_amf0_object_property_name_at(srs_amf0_t amf0, int index);
srs_amf0_t srs_amf0_object_property_value_at(srs_amf0_t amf0, int index);
srs_amf0_t srs_amf0_object_property(srs_amf0_t amf0, const char* name);
void srs_amf0_object_property_set(srs_amf0_t amf0, const char* name, srs_amf0_t value);
void srs_amf0_object_clear(srs_amf0_t amf0);
/* ecma array value converter */
int srs_amf0_ecma_array_property_count(srs_amf0_t amf0);
const char* srs_amf0_ecma_array_property_name_at(srs_amf0_t amf0, int index);
srs_amf0_t srs_amf0_ecma_array_property_value_at(srs_amf0_t amf0, int index);
srs_amf0_t srs_amf0_ecma_array_property(srs_amf0_t amf0, const char* name);
void srs_amf0_ecma_array_property_set(srs_amf0_t amf0, const char* name, srs_amf0_t value);
/* strict array value converter */
int srs_amf0_strict_array_property_count(srs_amf0_t amf0);
srs_amf0_t srs_amf0_strict_array_property_at(srs_amf0_t amf0, int index);
void srs_amf0_strict_array_append(srs_amf0_t amf0, srs_amf0_t value);
/**
* human readable print 
* @param pdata, output the heap data, NULL to ignore.
* user must use srs_amf0_free_bytes to free it.
* @return return the *pdata for print. NULL to ignore.
*/
char* srs_amf0_human_print(srs_amf0_t amf0, char** pdata, int* psize);

#ifdef __cplusplus
}
#endif

#endif
