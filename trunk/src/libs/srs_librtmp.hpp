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

/*************************************************************
**************************************************************
* RTMP protocol context
**************************************************************
*************************************************************/
// the RTMP handler.
typedef void* srs_rtmp_t;

/**
* create/destroy a rtmp protocol stack.
* @url rtmp url, for example: 
*         rtmp://localhost/live/livestream
*
* @return a rtmp handler, or NULL if error occured.
*/
extern srs_rtmp_t srs_rtmp_create(const char* url);
/**
* create rtmp with url, used for connection specified application.
* @param url the tcUrl, for exmple:
*         rtmp://localhost/live
* @remark this is used to create application connection-oriented,
*       for example, the bandwidth client used this, no stream specified.
*
* @return a rtmp handler, or NULL if error occured.
*/
extern srs_rtmp_t srs_rtmp_create2(const char* url);
/**
* close and destroy the rtmp stack.
* @remark, user should use the rtmp again.
*/
extern void srs_rtmp_destroy(srs_rtmp_t rtmp);

/*************************************************************
**************************************************************
* RTMP protocol stack
**************************************************************
*************************************************************/
/**
* connect and handshake with server
* category: publish/play
* previous: rtmp-create
* next: connect-app
*
* @return 0, success; otherswise, failed.
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
extern int srs_simple_handshake(srs_rtmp_t rtmp);
// parse uri, create socket, resolve host
extern int __srs_dns_resolve(srs_rtmp_t rtmp);
// connect socket to server
extern int __srs_connect_server(srs_rtmp_t rtmp);
// do simple handshake over socket.
extern int __srs_do_simple_handshake(srs_rtmp_t rtmp);

/**
* connect to rtmp vhost/app
* category: publish/play
* previous: handshake
* next: publish or play
*
* @return 0, success; otherswise, failed.
*/
extern int srs_connect_app(srs_rtmp_t rtmp);

/**
* connect to server, get the debug srs info.
* 
* SRS debug info:
* @param srs_server_ip, 128bytes, debug info, server ip client connected at.
* @param srs_server, 128bytes, server info.
* @param srs_primary_authors, 128bytes, primary authors.
* @param srs_version, 32bytes, server version.
* @param srs_id, int, debug info, client id in server log.
* @param srs_pid, int, debug info, server pid in log.
*
* @return 0, success; otherswise, failed.
*/
extern int srs_connect_app2(srs_rtmp_t rtmp,
    char srs_server_ip[128], char srs_server[128], char srs_primary_authors[128], 
    char srs_version[32], int* srs_id, int* srs_pid
);

/**
* play a live/vod stream.
* category: play
* previous: connect-app
* next: destroy
* @return 0, success; otherwise, failed.
*/
extern int srs_play_stream(srs_rtmp_t rtmp);

/**
* publish a live stream.
* category: publish
* previous: connect-app
* next: destroy
* @return 0, success; otherwise, failed.
*/
extern int srs_publish_stream(srs_rtmp_t rtmp);

/**
* do bandwidth check with srs server.
* 
* bandwidth info:
* @param start_time, output the start time, in ms.
* @param end_time, output the end time, in ms.
* @param play_kbps, output the play/download kbps.
* @param publish_kbps, output the publish/upload kbps.
* @param play_bytes, output the play/download bytes.
* @param publish_bytes, output the publish/upload bytes.
* @param play_duration, output the play/download test duration, in ms.
* @param publish_duration, output the publish/upload test duration, in ms.
*
* @return 0, success; otherswise, failed.
*/
extern int srs_bandwidth_check(srs_rtmp_t rtmp, 
    int64_t* start_time, int64_t* end_time, 
    int* play_kbps, int* publish_kbps,
    int* play_bytes, int* publish_bytes,
    int* play_duration, int* publish_duration
);

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
extern const char* srs_type2string(int type);
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
*
* @return 0, success; otherswise, failed.
*/
extern int srs_read_packet(srs_rtmp_t rtmp, 
    int* type, u_int32_t* timestamp, char** data, int* size
);
extern int srs_write_packet(srs_rtmp_t rtmp, 
    int type, u_int32_t timestamp, char* data, int size
);

// get protocol stack version
extern int srs_version_major();
extern int srs_version_minor();
extern int srs_version_revision();

/*************************************************************
**************************************************************
* utilities
**************************************************************
*************************************************************/
extern int64_t srs_get_time_ms();
extern int64_t srs_get_nsend_bytes(srs_rtmp_t rtmp);
extern int64_t srs_get_nrecv_bytes(srs_rtmp_t rtmp);


/*************************************************************
**************************************************************
* flv codec
**************************************************************
*************************************************************/
typedef void* srs_flv_t;
typedef int flv_bool;
/* open flv file for both read/write. */
extern srs_flv_t srs_flv_open_read(const char* file);
extern srs_flv_t srs_flv_open_write(const char* file);
extern void srs_flv_close(srs_flv_t flv);
/* read the flv header. 9bytes header. drop the 4bytes zero previous tag size */
extern int srs_flv_read_header(srs_flv_t flv, char header[9]);
/* read the flv tag header, 1bytes tag, 3bytes data_size, 4bytes time, 3bytes stream id. */
extern int srs_flv_read_tag_header(srs_flv_t flv, 
    char* ptype, int32_t* pdata_size, u_int32_t* ptime
);
/* read the tag data. drop the 4bytes previous tag size */
extern int srs_flv_read_tag_data(srs_flv_t flv, char* data, int32_t size);
/* write flv header to file, auto write the 4bytes zero previous tag size. */
extern int srs_flv_write_header(srs_flv_t flv, char header[9]);
/* write flv tag to file, auto write the 4bytes previous tag size */
extern int srs_flv_write_tag(srs_flv_t flv, char type, int32_t time, char* data, int size);
/* get the tag size, for flv injecter to adjust offset, size=tag_header+data+previous_tag */
extern int srs_flv_size_tag(int data_size);
/* file stream */
/* file stream tellg to get offset */
extern int64_t srs_flv_tellg(srs_flv_t flv);
/* seek file stream, offset is form the start of file */
extern void srs_flv_lseek(srs_flv_t flv, int64_t offset);
/* error code */
/* whether the error code indicates EOF */
extern flv_bool srs_flv_is_eof(int error_code);
/* media codec */
/* whether the video body is sequence header */
extern flv_bool srs_flv_is_sequence_header(char* data, int32_t size);
/* whether the video body is keyframe */
extern flv_bool srs_flv_is_keyframe(char* data, int32_t size);

/*************************************************************
**************************************************************
* amf0 codec
**************************************************************
*************************************************************/
/* the output handler. */
typedef void* srs_amf0_t;
typedef int srs_amf0_bool;
typedef double srs_amf0_number;
extern srs_amf0_t srs_amf0_parse(char* data, int size, int* nparsed);
extern srs_amf0_t srs_amf0_create_number(srs_amf0_number value);
extern srs_amf0_t srs_amf0_create_ecma_array();
extern srs_amf0_t srs_amf0_create_strict_array();
extern srs_amf0_t srs_amf0_create_object();
extern void srs_amf0_free(srs_amf0_t amf0);
extern void srs_amf0_free_bytes(char* data);
/* size and to bytes */
extern int srs_amf0_size(srs_amf0_t amf0);
extern int srs_amf0_serialize(srs_amf0_t amf0, char* data, int size);
/* type detecter */
extern srs_amf0_bool srs_amf0_is_string(srs_amf0_t amf0);
extern srs_amf0_bool srs_amf0_is_boolean(srs_amf0_t amf0);
extern srs_amf0_bool srs_amf0_is_number(srs_amf0_t amf0);
extern srs_amf0_bool srs_amf0_is_null(srs_amf0_t amf0);
extern srs_amf0_bool srs_amf0_is_object(srs_amf0_t amf0);
extern srs_amf0_bool srs_amf0_is_ecma_array(srs_amf0_t amf0);
extern srs_amf0_bool srs_amf0_is_strict_array(srs_amf0_t amf0);
/* value converter */
extern const char* srs_amf0_to_string(srs_amf0_t amf0);
extern srs_amf0_bool srs_amf0_to_boolean(srs_amf0_t amf0);
extern srs_amf0_number srs_amf0_to_number(srs_amf0_t amf0);
/* value setter */
extern void srs_amf0_set_number(srs_amf0_t amf0, srs_amf0_number value);
/* object value converter */
extern int srs_amf0_object_property_count(srs_amf0_t amf0);
extern const char* srs_amf0_object_property_name_at(srs_amf0_t amf0, int index);
extern srs_amf0_t srs_amf0_object_property_value_at(srs_amf0_t amf0, int index);
extern srs_amf0_t srs_amf0_object_property(srs_amf0_t amf0, const char* name);
extern void srs_amf0_object_property_set(srs_amf0_t amf0, const char* name, srs_amf0_t value);
extern void srs_amf0_object_clear(srs_amf0_t amf0);
/* ecma array value converter */
extern int srs_amf0_ecma_array_property_count(srs_amf0_t amf0);
extern const char* srs_amf0_ecma_array_property_name_at(srs_amf0_t amf0, int index);
extern srs_amf0_t srs_amf0_ecma_array_property_value_at(srs_amf0_t amf0, int index);
extern srs_amf0_t srs_amf0_ecma_array_property(srs_amf0_t amf0, const char* name);
extern void srs_amf0_ecma_array_property_set(srs_amf0_t amf0, const char* name, srs_amf0_t value);
/* strict array value converter */
extern int srs_amf0_strict_array_property_count(srs_amf0_t amf0);
extern srs_amf0_t srs_amf0_strict_array_property_at(srs_amf0_t amf0, int index);
extern void srs_amf0_strict_array_append(srs_amf0_t amf0, srs_amf0_t value);
/**
* human readable print 
* @param pdata, output the heap data, NULL to ignore.
* user must use srs_amf0_free_bytes to free it.
* @return return the *pdata for print. NULL to ignore.
*/
extern char* srs_amf0_human_print(srs_amf0_t amf0, char** pdata, int* psize);

/*************************************************************
**************************************************************
* h264 raw codec
**************************************************************
*************************************************************/
/**
* convert h264 stream data to rtmp packet.
* @param h264_raw_data the input h264 raw data, a encoded h.264 I/P/B frame data.
* @paam h264_raw_size the size of h264 raw data.
* @param dts the dts of h.264 raw data.
* @param pts the pts of h.264 raw data.
* @param prtmp_data the output rtmp format packet, which can be send by srs_write_packet.
* @param prtmp_size the size of rtmp packet, for srs_write_packet.
* @param ptimestamp the timestamp of rtmp packet, for srs_write_packet.
* @remark, user should free the h264_raw_data.
* @remark, user should free the prtmp_data if success.
* 
* @return 0, success; otherswise, failed.
*/
extern int srs_h264_to_rtmp(
    char* h264_raw_data, int h264_raw_size, u_int32_t dts, u_int32_t pts,
    char** prtmp_data, int* prtmp_size, u_int32_t* ptimestamp
);
/**
* whether h264 raw data starts with the annexb,
* which bytes sequence matches N[00] 00 00 01, where N>=0.
* @param h264_raw_data the input h264 raw data, a encoded h.264 I/P/B frame data.
* @paam h264_raw_size the size of h264 raw data.
* @param pnb_start_code output the size of start code, must >=3. 
*       NULL to ignore.
*
* @return 0 false; otherwise, true.
*/
extern int srs_h264_startswith_annexb(
    char* h264_raw_data, int h264_raw_size, 
    int* pnb_start_code
);

#ifdef __cplusplus
}
#endif

#endif

