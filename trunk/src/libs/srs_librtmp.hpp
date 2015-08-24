/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(simple-rtmp-server)

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

/**
* srs-librtmp is a librtmp like library,
* used to play/publish rtmp stream from/to rtmp server.
* socket: use sync and block socket to connect/recv/send data with server.
* depends: no need other libraries; depends on ssl if use srs_complex_handshake.
* thread-safe: no
*/

/*************************************************************
**************************************************************
* Windows SRS-LIBRTMP pre-declare
**************************************************************
*************************************************************/
// for srs-librtmp, @see https://github.com/simple-rtmp-server/srs/issues/213
#ifdef _WIN32
    // include windows first.
    #include <windows.h>
    // the type used by this header for windows.
    typedef unsigned long long u_int64_t;
    typedef long long int64_t;
    typedef unsigned int u_int32_t;
    typedef u_int32_t uint32_t;
    typedef int int32_t;
    typedef unsigned char u_int8_t;
    typedef char int8_t;
    typedef unsigned short u_int16_t;
    typedef short int16_t;
    typedef int64_t ssize_t;
    struct iovec {
        void  *iov_base;    /* Starting address */
        size_t iov_len;     /* Number of bytes to transfer */
    };
#endif

#include <sys/types.h>

#ifdef __cplusplus
extern "C"{
#endif

// typedefs
typedef int srs_bool;

/*************************************************************
**************************************************************
* srs-librtmp version
**************************************************************
*************************************************************/
extern int srs_version_major();
extern int srs_version_minor();
extern int srs_version_revision();

/*************************************************************
**************************************************************
* RTMP protocol context
**************************************************************
*************************************************************/
// the RTMP handler.
typedef void* srs_rtmp_t;
typedef void* srs_amf0_t;

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
* @remark, user should never use the rtmp again.
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
* srs_rtmp_handshake equals to invoke:
*       srs_rtmp_dns_resolve()
*       srs_rtmp_connect_server()
*       srs_rtmp_do_simple_handshake()
* user can use these functions if needed.
*/
extern int srs_rtmp_handshake(srs_rtmp_t rtmp);
// parse uri, create socket, resolve host
extern int srs_rtmp_dns_resolve(srs_rtmp_t rtmp);
// connect socket to server
extern int srs_rtmp_connect_server(srs_rtmp_t rtmp);
// do simple handshake over socket.
extern int srs_rtmp_do_simple_handshake(srs_rtmp_t rtmp);
// do complex handshake over socket.
extern int srs_rtmp_do_complex_handshake(srs_rtmp_t rtmp);

/**
* set the args of connect packet for rtmp.
* @param args, the extra amf0 object args.
* @remark, all params can be NULL to ignore.
* @remark, user should never free the args for we directly use it.
*/
extern int srs_rtmp_set_connect_args(srs_rtmp_t rtmp, 
    const char* tcUrl, const char* swfUrl, const char* pageUrl, srs_amf0_t args
);

/**
* connect to rtmp vhost/app
* category: publish/play
* previous: handshake
* next: publish or play
*
* @return 0, success; otherswise, failed.
*/
extern int srs_rtmp_connect_app(srs_rtmp_t rtmp);

/**
* connect to server, get the debug srs info.
* 
* SRS debug info:
* @param srs_server_ip, 128bytes, debug info, server ip client connected at.
* @param srs_server, 128bytes, server info.
* @param srs_primary, 128bytes, primary authors.
* @param srs_authors, 128bytes, authors.
* @param srs_version, 32bytes, server version.
* @param srs_id, int, debug info, client id in server log.
* @param srs_pid, int, debug info, server pid in log.
*
* @return 0, success; otherswise, failed.
*/
extern int srs_rtmp_connect_app2(srs_rtmp_t rtmp,
    char srs_server_ip[128], char srs_server[128], 
    char srs_primary[128], char srs_authors[128], 
    char srs_version[32], int* srs_id, int* srs_pid
);

/**
* play a live/vod stream.
* category: play
* previous: connect-app
* next: destroy
* @return 0, success; otherwise, failed.
*/
extern int srs_rtmp_play_stream(srs_rtmp_t rtmp);

/**
* publish a live stream.
* category: publish
* previous: connect-app
* next: destroy
* @return 0, success; otherwise, failed.
*/
extern int srs_rtmp_publish_stream(srs_rtmp_t rtmp);

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
extern int srs_rtmp_bandwidth_check(srs_rtmp_t rtmp, 
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
* read a audio/video/script-data packet from rtmp stream.
* @param type, output the packet type, macros:
*            SRS_RTMP_TYPE_AUDIO, FlvTagAudio
*            SRS_RTMP_TYPE_VIDEO, FlvTagVideo
*            SRS_RTMP_TYPE_SCRIPT, FlvTagScript
*            otherswise, invalid type.
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
* @example /trunk/research/librtmp/srs_play.c
* @example /trunk/research/librtmp/srs_publish.c
*
* @return 0, success; otherswise, failed.
*/
extern int srs_rtmp_read_packet(srs_rtmp_t rtmp, 
    char* type, u_int32_t* timestamp, char** data, int* size
);
extern int srs_rtmp_write_packet(srs_rtmp_t rtmp, 
    char type, u_int32_t timestamp, char* data, int size
);

/**
* whether type is script data and the data is onMetaData.
*/
extern srs_bool srs_rtmp_is_onMetaData(char type, char* data, int size);

/*************************************************************
**************************************************************
* audio raw codec
**************************************************************
*************************************************************/
/**
* write an audio raw frame to srs.
* not similar to h.264 video, the audio never aggregated, always
* encoded one frame by one, so this api is used to write a frame.
*
* @param sound_format Format of SoundData. The following values are defined:
*               0 = Linear PCM, platform endian
*               1 = ADPCM
*               2 = MP3
*               3 = Linear PCM, little endian
*               4 = Nellymoser 16 kHz mono
*               5 = Nellymoser 8 kHz mono
*               6 = Nellymoser
*               7 = G.711 A-law logarithmic PCM
*               8 = G.711 mu-law logarithmic PCM
*               9 = reserved
*               10 = AAC
*               11 = Speex
*               14 = MP3 8 kHz
*               15 = Device-specific sound
*               Formats 7, 8, 14, and 15 are reserved.
*               AAC is supported in Flash Player 9,0,115,0 and higher.
*               Speex is supported in Flash Player 10 and higher.
* @param sound_rate Sampling rate. The following values are defined:
*               0 = 5.5 kHz
*               1 = 11 kHz
*               2 = 22 kHz
*               3 = 44 kHz
* @param sound_size Size of each audio sample. This parameter only pertains to
*               uncompressed formats. Compressed formats always decode
*               to 16 bits internally.
*               0 = 8-bit samples
*               1 = 16-bit samples
* @param sound_type Mono or stereo sound
*               0 = Mono sound
*               1 = Stereo sound
* @param timestamp The timestamp of audio.
*
* @example /trunk/research/librtmp/srs_aac_raw_publish.c
* @example /trunk/research/librtmp/srs_audio_raw_publish.c
*
* @remark for aac, the frame must be in ADTS format. 
*       @see aac-mp4a-format-ISO_IEC_14496-3+2001.pdf, page 75, 1.A.2.2 ADTS
* @remark for aac, only support profile 1-4, AAC main/LC/SSR/LTP,
*       @see aac-mp4a-format-ISO_IEC_14496-3+2001.pdf, page 23, 1.5.1.1 Audio object type
*
* @see https://github.com/simple-rtmp-server/srs/issues/212
* @see E.4.2.1 AUDIODATA of video_file_format_spec_v10_1.pdf
* 
* @return 0, success; otherswise, failed.
*/
extern int srs_audio_write_raw_frame(srs_rtmp_t rtmp, 
    char sound_format, char sound_rate, char sound_size, char sound_type,
    char* frame, int frame_size, u_int32_t timestamp
);

/**
* whether aac raw data is in adts format,
* which bytes sequence matches '1111 1111 1111'B, that is 0xFFF.
* @param aac_raw_data the input aac raw data, a encoded aac frame data.
* @param ac_raw_size the size of aac raw data.
*
* @reamrk used to check whether current frame is in adts format.
*       @see aac-mp4a-format-ISO_IEC_14496-3+2001.pdf, page 75, 1.A.2.2 ADTS
* @example /trunk/research/librtmp/srs_aac_raw_publish.c
*
* @return 0 false; otherwise, true.
*/
extern srs_bool srs_aac_is_adts(char* aac_raw_data, int ac_raw_size);

/**
* parse the adts header to get the frame size,
* which bytes sequence matches '1111 1111 1111'B, that is 0xFFF.
* @param aac_raw_data the input aac raw data, a encoded aac frame data.
* @param ac_raw_size the size of aac raw data.
*
* @return failed when <=0 failed; otherwise, ok.
*/
extern int srs_aac_adts_frame_size(char* aac_raw_data, int ac_raw_size);

/*************************************************************
**************************************************************
* h264 raw codec
**************************************************************
*************************************************************/
/**
* write h.264 raw frame over RTMP to rtmp server.
* @param frames the input h264 raw data, encoded h.264 I/P/B frames data.
*       frames can be one or more than one frame,
*       each frame prefixed h.264 annexb header, by N[00] 00 00 01, where N>=0, 
*       for instance, frame = header(00 00 00 01) + payload(67 42 80 29 95 A0 14 01 6E 40)
*       about annexb, @see H.264-AVC-ISO_IEC_14496-10.pdf, page 211.
* @param frames_size the size of h264 raw data. 
*       assert frames_size > 0, at least has 1 bytes header.
* @param dts the dts of h.264 raw data.
* @param pts the pts of h.264 raw data.
* 
* @remark, user should free the frames.
* @remark, the tbn of dts/pts is 1/1000 for RTMP, that is, in ms.
* @remark, cts = pts - dts
* @remark, use srs_h264_startswith_annexb to check whether frame is annexb format.
* @example /trunk/research/librtmp/srs_h264_raw_publish.c
* @see https://github.com/simple-rtmp-server/srs/issues/66
* 
* @return 0, success; otherswise, failed.
*       for dvbsp error, @see srs_h264_is_dvbsp_error().
*       for duplictated sps error, @see srs_h264_is_duplicated_sps_error().
*       for duplictated pps error, @see srs_h264_is_duplicated_pps_error().
*/
/**
For the example file: 
    http://winlinvip.github.io/srs.release/3rdparty/720p.h264.raw
The data sequence is:
    // SPS
    000000016742802995A014016E40
    // PPS
    0000000168CE3880
    // IFrame
    0000000165B8041014C038008B0D0D3A071.....
    // PFrame
    0000000141E02041F8CDDC562BBDEFAD2F.....
User can send the SPS+PPS, then each frame:
    // SPS+PPS
    srs_h264_write_raw_frames('000000016742802995A014016E400000000168CE3880', size, dts, pts)
    // IFrame
    srs_h264_write_raw_frames('0000000165B8041014C038008B0D0D3A071......', size, dts, pts)
    // PFrame
    srs_h264_write_raw_frames('0000000141E02041F8CDDC562BBDEFAD2F......', size, dts, pts)
User also can send one by one:
    // SPS
    srs_h264_write_raw_frames('000000016742802995A014016E4', size, dts, pts)
    // PPS
    srs_h264_write_raw_frames('00000000168CE3880', size, dts, pts)
    // IFrame
    srs_h264_write_raw_frames('0000000165B8041014C038008B0D0D3A071......', size, dts, pts)
    // PFrame
    srs_h264_write_raw_frames('0000000141E02041F8CDDC562BBDEFAD2F......', size, dts, pts) 
*/
extern int srs_h264_write_raw_frames(srs_rtmp_t rtmp, 
    char* frames, int frames_size, u_int32_t dts, u_int32_t pts
);
/**
* whether error_code is dvbsp(drop video before sps/pps/sequence-header) error.
*
* @see https://github.com/simple-rtmp-server/srs/issues/203
* @example /trunk/research/librtmp/srs_h264_raw_publish.c
* @remark why drop video?
*       some encoder, for example, ipcamera, will send sps/pps before each IFrame,
*       so, when error and reconnect the rtmp, the first video is not sps/pps(sequence header),
*       this will cause SRS server to disable HLS.
*/
extern srs_bool srs_h264_is_dvbsp_error(int error_code);
/**
* whether error_code is duplicated sps error.
* 
* @see https://github.com/simple-rtmp-server/srs/issues/204
* @example /trunk/research/librtmp/srs_h264_raw_publish.c
*/
extern srs_bool srs_h264_is_duplicated_sps_error(int error_code);
/**
* whether error_code is duplicated pps error.
* 
* @see https://github.com/simple-rtmp-server/srs/issues/204
* @example /trunk/research/librtmp/srs_h264_raw_publish.c
*/
extern srs_bool srs_h264_is_duplicated_pps_error(int error_code);
/**
* whether h264 raw data starts with the annexb,
* which bytes sequence matches N[00] 00 00 01, where N>=0.
* @param h264_raw_data the input h264 raw data, a encoded h.264 I/P/B frame data.
* @paam h264_raw_size the size of h264 raw data.
* @param pnb_start_code output the size of start code, must >=3. 
*       NULL to ignore.
*
* @reamrk used to check whether current frame is in annexb format.
* @example /trunk/research/librtmp/srs_h264_raw_publish.c
*
* @return 0 false; otherwise, true.
*/
extern srs_bool srs_h264_startswith_annexb(
    char* h264_raw_data, int h264_raw_size, 
    int* pnb_start_code
);

/*************************************************************
**************************************************************
* flv codec
* @example /trunk/research/librtmp/srs_flv_injecter.c
* @example /trunk/research/librtmp/srs_flv_parser.c
* @example /trunk/research/librtmp/srs_ingest_flv.c
* @example /trunk/research/librtmp/srs_ingest_rtmp.c
**************************************************************
*************************************************************/
typedef void* srs_flv_t;
/* open flv file for both read/write. */
extern srs_flv_t srs_flv_open_read(const char* file);
extern srs_flv_t srs_flv_open_write(const char* file);
extern void srs_flv_close(srs_flv_t flv);
/**
* read the flv header. 9bytes header. 
* @param header, @see E.2 The FLV header, flv_v10_1.pdf in SRS doc.
*   3bytes, signature, "FLV",
*   1bytes, version, 0x01,
*   1bytes, flags, UB[5] 0, UB[1] audio present, UB[1] 0, UB[1] video present.
*   4bytes, dataoffset, 0x09, The length of this header in bytes
*
* @return 0, success; otherswise, failed.
* @remark, drop the 4bytes zero previous tag size.
*/
extern int srs_flv_read_header(srs_flv_t flv, char header[9]);
/**
* read the flv tag header, 1bytes tag, 3bytes data_size, 
* 4bytes time, 3bytes stream id. 
* @param ptype, output the type of tag, macros:
*            SRS_RTMP_TYPE_AUDIO, FlvTagAudio
*            SRS_RTMP_TYPE_VIDEO, FlvTagVideo
*            SRS_RTMP_TYPE_SCRIPT, FlvTagScript
* @param pdata_size, output the size of tag data.
* @param ptime, output the time of tag, the dts in ms.
*
* @return 0, success; otherswise, failed.
* @remark, user must ensure the next is a tag, srs never check it.
*/
extern int srs_flv_read_tag_header(srs_flv_t flv, 
    char* ptype, int32_t* pdata_size, u_int32_t* ptime
);
/**
* read the tag data. drop the 4bytes previous tag size 
* @param data, the data to read, user alloc and free it.
* @param size, the size of data to read, get by srs_flv_read_tag_header().
* @remark, srs will ignore and drop the 4bytes previous tag size.
*/
extern int srs_flv_read_tag_data(srs_flv_t flv, char* data, int32_t size);
/**
* write the flv header. 9bytes header. 
* @param header, @see E.2 The FLV header, flv_v10_1.pdf in SRS doc.
*   3bytes, signature, "FLV",
*   1bytes, version, 0x01,
*   1bytes, flags, UB[5] 0, UB[1] audio present, UB[1] 0, UB[1] video present.
*   4bytes, dataoffset, 0x09, The length of this header in bytes
*
* @return 0, success; otherswise, failed.
* @remark, auto write the 4bytes zero previous tag size.
*/
extern int srs_flv_write_header(srs_flv_t flv, char header[9]);
/**
* write the flv tag to file.
*
* @return 0, success; otherswise, failed.
* @remark, auto write the 4bytes zero previous tag size.
*/
/* write flv tag to file, auto write the 4bytes previous tag size */
extern int srs_flv_write_tag(srs_flv_t flv, 
    char type, int32_t time, char* data, int size
);
/**
* get the tag size, for flv injecter to adjust offset, 
*       size = tag_header(11B) + data_size + previous_tag(4B)
* @return the size of tag.
*/
extern int srs_flv_size_tag(int data_size);
/* file stream */
/* file stream tellg to get offset */
extern int64_t srs_flv_tellg(srs_flv_t flv);
/* seek file stream, offset is form the start of file */
extern void srs_flv_lseek(srs_flv_t flv, int64_t offset);
/* error code */
/* whether the error code indicates EOF */
extern srs_bool srs_flv_is_eof(int error_code);
/* media codec */
/**
* whether the video body is sequence header 
* @param data, the data of tag, read by srs_flv_read_tag_data().
* @param size, the size of tag, read by srs_flv_read_tag_data().
*/
extern srs_bool srs_flv_is_sequence_header(char* data, int32_t size);
/**
* whether the video body is keyframe 
* @param data, the data of tag, read by srs_flv_read_tag_data().
* @param size, the size of tag, read by srs_flv_read_tag_data().
*/
extern srs_bool srs_flv_is_keyframe(char* data, int32_t size);

/*************************************************************
**************************************************************
* amf0 codec
* @example /trunk/research/librtmp/srs_ingest_flv.c
* @example /trunk/research/librtmp/srs_ingest_rtmp.c
**************************************************************
*************************************************************/
/* the output handler. */
typedef double srs_amf0_number;
/**
* parse amf0 from data.
* @param nparsed, the parsed size, NULL to ignore.
* @return the parsed amf0 object. NULL for error.
* @remark user must free the parsed or created object by srs_amf0_free.
*/
extern srs_amf0_t srs_amf0_parse(char* data, int size, int* nparsed);
extern srs_amf0_t srs_amf0_create_string(const char* value);
extern srs_amf0_t srs_amf0_create_number(srs_amf0_number value);
extern srs_amf0_t srs_amf0_create_ecma_array();
extern srs_amf0_t srs_amf0_create_strict_array();
extern srs_amf0_t srs_amf0_create_object();
extern srs_amf0_t srs_amf0_ecma_array_to_object(srs_amf0_t ecma_arr);
extern void srs_amf0_free(srs_amf0_t amf0);
/* size and to bytes */
extern int srs_amf0_size(srs_amf0_t amf0);
extern int srs_amf0_serialize(srs_amf0_t amf0, char* data, int size);
/* type detecter */
extern srs_bool srs_amf0_is_string(srs_amf0_t amf0);
extern srs_bool srs_amf0_is_boolean(srs_amf0_t amf0);
extern srs_bool srs_amf0_is_number(srs_amf0_t amf0);
extern srs_bool srs_amf0_is_null(srs_amf0_t amf0);
extern srs_bool srs_amf0_is_object(srs_amf0_t amf0);
extern srs_bool srs_amf0_is_ecma_array(srs_amf0_t amf0);
extern srs_bool srs_amf0_is_strict_array(srs_amf0_t amf0);
/* value converter */
extern const char* srs_amf0_to_string(srs_amf0_t amf0);
extern srs_bool srs_amf0_to_boolean(srs_amf0_t amf0);
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

/*************************************************************
**************************************************************
* utilities
**************************************************************
*************************************************************/
/**
* get the current system time in ms.
* use gettimeofday() to get system time.
*/
extern int64_t srs_utils_time_ms();

/**
* get the send bytes.
*/
extern int64_t srs_utils_send_bytes(srs_rtmp_t rtmp);

/**
* get the recv bytes.
*/
extern int64_t srs_utils_recv_bytes(srs_rtmp_t rtmp);

/**
* parse the dts and pts by time in header and data in tag,
* or to parse the RTMP packet by srs_rtmp_read_packet().
*
* @param time, the timestamp of tag, read by srs_flv_read_tag_header().
* @param type, the type of tag, read by srs_flv_read_tag_header().
* @param data, the data of tag, read by srs_flv_read_tag_data().
* @param size, the size of tag, read by srs_flv_read_tag_header().
* @param ppts, output the pts in ms,
*
* @return 0, success; otherswise, failed.
* @remark, the dts always equals to @param time.
* @remark, the pts=dts for audio or data.
* @remark, video only support h.264.
*/
extern int srs_utils_parse_timestamp(
    u_int32_t time, char type, char* data, int size,
    u_int32_t* ppts
);
    
/**
 * whether the flv tag specified by param type is ok.
 * @return true when tag is video/audio/script-data; otherwise, false.
 */
extern srs_bool srs_utils_flv_tag_is_ok(char type);
extern srs_bool srs_utils_flv_tag_is_audio(char type);
extern srs_bool srs_utils_flv_tag_is_video(char type);
extern srs_bool srs_utils_flv_tag_is_av(char type);

/**
* get the CodecID of video tag.
* Codec Identifier. The following values are defined:
*           2 = Sorenson H.263
*           3 = Screen video
*           4 = On2 VP6
*           5 = On2 VP6 with alpha channel
*           6 = Screen video version 2
*           7 = AVC
* @return the code id. 0 for error.
*/
extern char srs_utils_flv_video_codec_id(char* data, int size);

/**
* get the AVCPacketType of video tag.
* The following values are defined:
*           0 = AVC sequence header
*           1 = AVC NALU
*           2 = AVC end of sequence (lower level NALU sequence ender is
*               not required or supported)
* @return the avc packet type. -1(0xff) for error.
*/
extern char srs_utils_flv_video_avc_packet_type(char* data, int size);

/**
* get the FrameType of video tag.
* Type of video frame. The following values are defined:
*           1 = key frame (for AVC, a seekable frame)
*           2 = inter frame (for AVC, a non-seekable frame)
*           3 = disposable inter frame (H.263 only)
*           4 = generated key frame (reserved for server use only)
*           5 = video info/command frame
* @return the frame type. 0 for error.
*/
extern char srs_utils_flv_video_frame_type(char* data, int size);

/**
* get the SoundFormat of audio tag.
* Format of SoundData. The following values are defined:
*               0 = Linear PCM, platform endian
*               1 = ADPCM
*               2 = MP3
*               3 = Linear PCM, little endian
*               4 = Nellymoser 16 kHz mono
*               5 = Nellymoser 8 kHz mono
*               6 = Nellymoser
*               7 = G.711 A-law logarithmic PCM
*               8 = G.711 mu-law logarithmic PCM
*               9 = reserved
*               10 = AAC
*               11 = Speex
*               14 = MP3 8 kHz
*               15 = Device-specific sound
*               Formats 7, 8, 14, and 15 are reserved.
*               AAC is supported in Flash Player 9,0,115,0 and higher.
*               Speex is supported in Flash Player 10 and higher.
* @return the sound format. -1(0xff) for error.
*/
extern char srs_utils_flv_audio_sound_format(char* data, int size);

/**
* get the SoundRate of audio tag.
* Sampling rate. The following values are defined:
*               0 = 5.5 kHz
*               1 = 11 kHz
*               2 = 22 kHz
*               3 = 44 kHz
* @return the sound rate. -1(0xff) for error.
*/
extern char srs_utils_flv_audio_sound_rate(char* data, int size);

/**
* get the SoundSize of audio tag.
* Size of each audio sample. This parameter only pertains to
* uncompressed formats. Compressed formats always decode
* to 16 bits internally.
*               0 = 8-bit samples
*               1 = 16-bit samples
* @return the sound size. -1(0xff) for error.
*/
extern char srs_utils_flv_audio_sound_size(char* data, int size);

/**
* get the SoundType of audio tag.
* Mono or stereo sound
*               0 = Mono sound
*               1 = Stereo sound
* @return the sound type. -1(0xff) for error.
*/
extern char srs_utils_flv_audio_sound_type(char* data, int size);

/**
* get the AACPacketType of audio tag.
* The following values are defined:
*               0 = AAC sequence header
*               1 = AAC raw
* @return the aac packet type. -1(0xff) for error.
*/
extern char srs_utils_flv_audio_aac_packet_type(char* data, int size);

/*************************************************************
**************************************************************
* human readable print.
**************************************************************
*************************************************************/
/**
* human readable print 
* @param pdata, output the heap data, NULL to ignore.
*       user must use srs_amf0_free_bytes to free it.
* @return return the *pdata for print. NULL to ignore.
*/
extern char* srs_human_amf0_print(srs_amf0_t amf0, char** pdata, int* psize);
/**
* convert the flv tag type to string.
*     SRS_RTMP_TYPE_AUDIO to "Audio"
*     SRS_RTMP_TYPE_VIDEO to "Video"
*     SRS_RTMP_TYPE_SCRIPT to "Data"
*     otherwise, "Unknown"
* @remark user never free the return char*, 
*   it's static shared const string.
*/
extern const char* srs_human_flv_tag_type2string(char type);

/**
* get the codec id string.
*           H.263 = Sorenson H.263
*           Screen = Screen video
*           VP6 = On2 VP6
*           VP6Alpha = On2 VP6 with alpha channel
*           Screen2 = Screen video version 2
*           H.264 = AVC
*           otherwise, "Unknown"
* @remark user never free the return char*, 
*   it's static shared const string.
*/
extern const char* srs_human_flv_video_codec_id2string(char codec_id);

/**
* get the avc packet type string.
*           SH = AVC sequence header
*           Nalu = AVC NALU
*           SpsPpsEnd = AVC end of sequence
*           otherwise, "Unknown"
* @remark user never free the return char*, 
*   it's static shared const string.
*/
extern const char* srs_human_flv_video_avc_packet_type2string(char avc_packet_type);

/**
* get the frame type string.
*           I = key frame (for AVC, a seekable frame)
*           P/B = inter frame (for AVC, a non-seekable frame)
*           DI = disposable inter frame (H.263 only)
*           GI = generated key frame (reserved for server use only)
*           VI = video info/command frame
*           otherwise, "Unknown"
* @remark user never free the return char*, 
*   it's static shared const string.
*/
extern const char* srs_human_flv_video_frame_type2string(char frame_type);

/**
* get the SoundFormat string.
* Format of SoundData. The following values are defined:
*               LinearPCM = Linear PCM, platform endian
*               ADPCM = ADPCM
*               MP3 = MP3
*               LinearPCMLe = Linear PCM, little endian
*               NellymoserKHz16 = Nellymoser 16 kHz mono
*               NellymoserKHz8 = Nellymoser 8 kHz mono
*               Nellymoser = Nellymoser
*               G711APCM = G.711 A-law logarithmic PCM
*               G711MuPCM = G.711 mu-law logarithmic PCM
*               Reserved = reserved
*               AAC = AAC
*               Speex = Speex
*               MP3KHz8 = MP3 8 kHz
*               DeviceSpecific = Device-specific sound
*               otherwise, "Unknown"
* @remark user never free the return char*, 
*   it's static shared const string.
*/
extern const char* srs_human_flv_audio_sound_format2string(char sound_format);

/**
* get the SoundRate of audio tag.
* Sampling rate. The following values are defined:
*               5.5KHz = 5.5 kHz
*               11KHz = 11 kHz
*               22KHz = 22 kHz
*               44KHz = 44 kHz
*               otherwise, "Unknown"
* @remark user never free the return char*, 
*   it's static shared const string.
*/
extern const char* srs_human_flv_audio_sound_rate2string(char sound_rate);

/**
* get the SoundSize of audio tag.
* Size of each audio sample. This parameter only pertains to
* uncompressed formats. Compressed formats always decode
* to 16 bits internally.
*               8bit = 8-bit samples
*               16bit = 16-bit samples
*               otherwise, "Unknown"
* @remark user never free the return char*, 
*   it's static shared const string.
*/
extern const char* srs_human_flv_audio_sound_size2string(char sound_size);

/**
* get the SoundType of audio tag.
* Mono or stereo sound
*               Mono = Mono sound
*               Stereo = Stereo sound
*               otherwise, "Unknown"
* @remark user never free the return char*, 
*   it's static shared const string.
*/
extern const char* srs_human_flv_audio_sound_type2string(char sound_type);

/**
* get the AACPacketType of audio tag.
* The following values are defined:
*               SH = AAC sequence header
*               Raw = AAC raw
*               otherwise, "Unknown"
* @remark user never free the return char*, 
*   it's static shared const string.
*/
extern const char* srs_human_flv_audio_aac_packet_type2string(char aac_packet_type);

/**
* print the rtmp packet, use srs_human_trace/srs_human_verbose for packet,
* and use srs_human_raw for script data body.
* @return an error code for parse the timetstamp to dts and pts.
 */
extern int srs_human_print_rtmp_packet(char type, u_int32_t timestamp, char* data, int size);
/**
 * @param pre_timestamp the previous timestamp in ms to calc the diff.
 */
extern int srs_human_print_rtmp_packet2(char type, u_int32_t timestamp, char* data, int size, u_int32_t pre_timestamp);
/**
 * @param pre_now the previous system time in ms to calc the ndiff.
 */
extern int srs_human_print_rtmp_packet3(char type, u_int32_t timestamp, char* data, int size, u_int32_t pre_timestamp, int64_t pre_now);
/**
 * @param starttime the rtmpdump starttime in ms.
 * @param nb_packets the number of packets received, to calc the packets interval in ms.
 */
extern int srs_human_print_rtmp_packet4(char type, u_int32_t timestamp, char* data, int size, u_int32_t pre_timestamp, int64_t pre_now, int64_t starttime, int64_t nb_packets);

// log to console, for use srs-librtmp application.
extern const char* srs_human_format_time();
    
#ifndef _WIN32
    // for getpid.
    #include <unistd.h>
#endif
// when disabled log, donot compile it.
#ifdef SRS_DISABLE_LOG
    #define srs_human_trace(msg, ...) (void)0
    #define srs_human_verbose(msg, ...) (void)0
    #define srs_human_raw(msg, ...) (void)0
#else
    #define srs_human_trace(msg, ...) printf("[%s][%d] ", srs_human_format_time(), getpid());printf(msg, ##__VA_ARGS__);printf("\n")
    #define srs_human_verbose(msg, ...) printf("[%s][%d] ", srs_human_format_time(), getpid());printf(msg, ##__VA_ARGS__);printf("\n")
    #define srs_human_raw(msg, ...) printf(msg, ##__VA_ARGS__)
#endif

/*************************************************************
**************************************************************
* IO hijack, use your specified io functions.
**************************************************************
 *************************************************************/
// the void* will convert to your handler for io hijack.
typedef void* srs_hijack_io_t;
#ifdef SRS_HIJACK_IO
    #ifndef _WIN32
        // for iovec.
        #include <sys/uio.h>
    #endif
    /**
    * get the hijack io object in rtmp protocol sdk.
    * @remark, user should never provides this method, srs-librtmp provides it.
    */
    extern srs_hijack_io_t srs_hijack_io_get(srs_rtmp_t rtmp);
#endif
// define the following macro and functions in your module to hijack the io.
// the example @see https://github.com/winlinvip/st-load
// which use librtmp but use its own io(use st also).
#ifdef SRS_HIJACK_IO
    /**
    * create hijack.
    * @return NULL for error; otherwise, ok.
    */
    extern srs_hijack_io_t srs_hijack_io_create();
    /**
    * destroy the context, user must close the socket.
    */
    extern void srs_hijack_io_destroy(srs_hijack_io_t ctx);
    /**
    * create socket, not connect yet.
    * @return 0, success; otherswise, failed.
    */
    extern int srs_hijack_io_create_socket(srs_hijack_io_t ctx);
    /**
    * connect socket at server_ip:port.
    * @return 0, success; otherswise, failed.
    */
    extern int srs_hijack_io_connect(srs_hijack_io_t ctx, const char* server_ip, int port);
    /**
    * read from socket.
    * @return 0, success; otherswise, failed.
    */
    extern int srs_hijack_io_read(srs_hijack_io_t ctx, void* buf, size_t size, ssize_t* nread);
    /**
    * set the socket recv timeout.
    * @return 0, success; otherswise, failed.
    */
    extern void srs_hijack_io_set_recv_timeout(srs_hijack_io_t ctx, int64_t timeout_us);
    /**
    * get the socket recv timeout.
    * @return 0, success; otherswise, failed.
    */
    extern int64_t srs_hijack_io_get_recv_timeout(srs_hijack_io_t ctx);
    /**
    * get the socket recv bytes.
    * @return 0, success; otherswise, failed.
    */
    extern int64_t srs_hijack_io_get_recv_bytes(srs_hijack_io_t ctx);
    /**
    * set the socket send timeout.
    * @return 0, success; otherswise, failed.
    */
    extern void srs_hijack_io_set_send_timeout(srs_hijack_io_t ctx, int64_t timeout_us);
    /**
    * get the socket send timeout.
    * @return 0, success; otherswise, failed.
    */
    extern int64_t srs_hijack_io_get_send_timeout(srs_hijack_io_t ctx);
    /**
    * get the socket send bytes.
    * @return 0, success; otherswise, failed.
    */
    extern int64_t srs_hijack_io_get_send_bytes(srs_hijack_io_t ctx);
    /**
    * writev of socket.
    * @return 0, success; otherswise, failed.
    */
    extern int srs_hijack_io_writev(srs_hijack_io_t ctx, const iovec *iov, int iov_size, ssize_t* nwrite);
    /**
    * whether the timeout is never timeout.
    * @return 0, success; otherswise, failed.
    */
    extern bool srs_hijack_io_is_never_timeout(srs_hijack_io_t ctx, int64_t timeout_us);
    /**
    * read fully, fill the buf exactly size bytes.
    * @return 0, success; otherswise, failed.
    */
    extern int srs_hijack_io_read_fully(srs_hijack_io_t ctx, void* buf, size_t size, ssize_t* nread);
    /**
    * write bytes to socket.
    * @return 0, success; otherswise, failed.
    */
    extern int srs_hijack_io_write(srs_hijack_io_t ctx, void* buf, size_t size, ssize_t* nwrite);
#endif

/*************************************************************
**************************************************************
* Windows SRS-LIBRTMP solution
**************************************************************
*************************************************************/
// for srs-librtmp, @see https://github.com/simple-rtmp-server/srs/issues/213
#ifdef _WIN32
    // for time.
    #define _CRT_SECURE_NO_WARNINGS
    #include <time.h>
    int gettimeofday(struct timeval* tv, struct timezone* tz);
    #define PRId64 "lld"
    
    // for inet helpers.
    typedef int socklen_t;
    const char *inet_ntop(int af, const void *src, char *dst, socklen_t size);
    
    // for mkdir().
    #include<direct.h>
    
    // for open().
    typedef int mode_t;
    #define S_IRUSR 0
    #define S_IWUSR 0
    #define S_IXUSR 0
    #define S_IRGRP 0
    #define S_IWGRP 0
    #define S_IXGRP 0
    #define S_IROTH 0
    #define S_IXOTH 0
    
    // for file seek.
    #include <io.h>
    #include <fcntl.h>
    #define open _open
    #define close _close
    #define lseek _lseek
    #define write _write
    #define read _read
    
    // for pid.
    typedef int pid_t;
    pid_t getpid(void);
    
    // for socket.
    ssize_t writev(int fd, const struct iovec *iov, int iovcnt);
    typedef int64_t useconds_t;
    int usleep(useconds_t usec);
    int socket_setup();
    int socket_cleanup();
    
    // others.
    #define snprintf _snprintf
#endif

#ifdef __cplusplus
}
#endif

#endif

