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

#include <srs_librtmp.hpp>

#include <stdlib.h>
#include <sys/time.h>

#include <string>
#include <sstream>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_protocol_rtmp.hpp>
#include <srs_lib_simple_socket.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_protocol_stack.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_file.hpp>
#include <srs_lib_bandwidth.hpp>

// if want to use your log, define the folowing macro.
#ifndef SRS_RTMP_USER_DEFINED_LOG
    // kernel module.
    ISrsLog* _srs_log = new ISrsLog();
    ISrsThreadContext* _srs_context = new ISrsThreadContext();
#endif

/**
* export runtime context.
*/
struct Context
{
    std::string url;
    std::string tcUrl;
    std::string host;
    std::string ip;
    std::string port;
    std::string vhost;
    std::string app;
    std::string stream;
    std::string param;
    
    SrsRtmpClient* rtmp;
    SimpleSocketStream* skt;
    int stream_id;
    
    // for h264 raw stream, 
    // see: https://github.com/winlinvip/simple-rtmp-server/issues/66#issuecomment-62240521
    SrsStream h264_raw_stream;
    // about SPS, @see: 7.3.2.1.1, H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 62
    std::string h264_sps;
    std::string h264_pps;
    // whether the sps and pps sent,
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/203
    bool h264_sps_pps_sent;
    
    Context() {
        rtmp = NULL;
        skt = NULL;
        stream_id = 0;
        h264_sps_pps_sent = false;
    }
    virtual ~Context() {
        srs_freep(rtmp);
        srs_freep(skt);
    }
};

int srs_librtmp_context_parse_uri(Context* context) 
{
    int ret = ERROR_SUCCESS;
    
    // parse uri
    size_t pos = string::npos;
    string uri = context->url;
    // tcUrl, stream
    if ((pos = uri.rfind("/")) != string::npos) {
        context->stream = uri.substr(pos + 1);
        context->tcUrl = uri = uri.substr(0, pos);
    }
    
    std::string schema;
    srs_discovery_tc_url(context->tcUrl, 
        schema, context->host, context->vhost, context->app, context->port,
        context->param);
    
    return ret;
}

int srs_librtmp_context_resolve_host(Context* context) 
{
    int ret = ERROR_SUCCESS;
    
    // create socket
    srs_freep(context->skt);
    context->skt = new SimpleSocketStream();
    
    if ((ret = context->skt->create_socket()) != ERROR_SUCCESS) {
        return ret;
    }
    
    // connect to server:port
    context->ip = srs_dns_resolve(context->host);
    if (context->ip.empty()) {
        return -1;
    }
    
    return ret;
}

int srs_librtmp_context_connect(Context* context) 
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(context->skt);
    
    std::string ip = context->ip;
    int port = ::atoi(context->port.c_str());
    
    if ((ret = context->skt->connect(ip.c_str(), port)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

#ifdef __cplusplus
extern "C"{
#endif

srs_rtmp_t srs_rtmp_create(const char* url)
{
    Context* context = new Context();
    context->url = url;
    return context;
}

srs_rtmp_t srs_rtmp_create2(const char* url)
{
    Context* context = new Context();
    
    // use url as tcUrl.
    context->url = url;
    // auto append stream.
    context->url += "/livestream";
    
    return context;
}

void srs_rtmp_destroy(srs_rtmp_t rtmp)
{
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    srs_freep(context);
}

int srs_simple_handshake(srs_rtmp_t rtmp)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = __srs_dns_resolve(rtmp)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = __srs_connect_server(rtmp)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = __srs_do_simple_handshake(rtmp)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int __srs_dns_resolve(srs_rtmp_t rtmp)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    // parse uri
    if ((ret = srs_librtmp_context_parse_uri(context)) != ERROR_SUCCESS) {
        return ret;
    }
    // resolve host
    if ((ret = srs_librtmp_context_resolve_host(context)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int __srs_connect_server(srs_rtmp_t rtmp)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    if ((ret = srs_librtmp_context_connect(context)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int __srs_do_simple_handshake(srs_rtmp_t rtmp)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    srs_assert(context->skt != NULL);
    
    // simple handshake
    srs_freep(context->rtmp);
    context->rtmp = new SrsRtmpClient(context->skt);
    
    if ((ret = context->rtmp->simple_handshake()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int srs_connect_app(srs_rtmp_t rtmp)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    string tcUrl = srs_generate_tc_url(
        context->ip, context->vhost, context->app, context->port,
        context->param
    );
    
    if ((ret = context->rtmp->connect_app(
        context->app, tcUrl, NULL, true)) != ERROR_SUCCESS) 
    {
        return ret;
    }
    
    return ret;
}

int srs_connect_app2(srs_rtmp_t rtmp,
    char srs_server_ip[128],char srs_server[128], char srs_primary_authors[128], 
    char srs_version[32], int* srs_id, int* srs_pid
) {
    srs_server_ip[0] = 0;
    srs_server[0] = 0;
    srs_primary_authors[0] = 0;
    srs_version[0] = 0;
    *srs_id = 0;
    *srs_pid = 0;

    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    string tcUrl = srs_generate_tc_url(
        context->ip, context->vhost, context->app, context->port,
        context->param
    );
    
    std::string sip, sserver, sauthors, sversion;
    
    if ((ret = context->rtmp->connect_app2(context->app, tcUrl, NULL, true,
        sip, sserver, sauthors, sversion, *srs_id, *srs_pid)) != ERROR_SUCCESS) {
        return ret;
    }
    
    snprintf(srs_server_ip, 128, "%s", sip.c_str());
    snprintf(srs_server, 128, "%s", sserver.c_str());
    snprintf(srs_primary_authors, 128, "%s", sauthors.c_str());
    snprintf(srs_version, 32, "%s", sversion.c_str());
    
    return ret;
}

int srs_play_stream(srs_rtmp_t rtmp)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    if ((ret = context->rtmp->create_stream(context->stream_id)) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = context->rtmp->play(context->stream, context->stream_id)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int srs_publish_stream(srs_rtmp_t rtmp)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    if ((ret = context->rtmp->fmle_publish(context->stream, context->stream_id)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

const char* srs_type2string(char type)
{
    static const char* audio = "Audio";
    static const char* video = "Video";
    static const char* data = "Data";
    static const char* unknown = "Unknown";
    
    switch (type) {
        case SRS_RTMP_TYPE_AUDIO: return audio;
        case SRS_RTMP_TYPE_VIDEO: return video;
        case SRS_RTMP_TYPE_SCRIPT: return data;
        default: return unknown;
    }
    
    return unknown;
}

int srs_bandwidth_check(srs_rtmp_t rtmp, 
    int64_t* start_time, int64_t* end_time, 
    int* play_kbps, int* publish_kbps,
    int* play_bytes, int* publish_bytes,
    int* play_duration, int* publish_duration
) {
    *start_time = 0;
    *end_time = 0;
    *play_kbps = 0;
    *publish_kbps = 0;
    *play_bytes = 0;
    *publish_bytes = 0;
    *play_duration = 0;
    *publish_duration = 0;
    
    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    SrsBandwidthClient client;

    if ((ret = client.initialize(context->rtmp)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = client.bandwidth_check(
        start_time, end_time, play_kbps, publish_kbps,
        play_bytes, publish_bytes, play_duration, publish_duration)) != ERROR_SUCCESS
    ) {
        return ret;
    }
    
    return ret;
}

int srs_read_packet(srs_rtmp_t rtmp, char* type, u_int32_t* timestamp, char** data, int* size)
{
    *type = 0;
    *timestamp = 0;
    *data = NULL;
    *size = 0;
    
    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    for (;;) {
        SrsMessage* msg = NULL;
        if ((ret = context->rtmp->recv_message(&msg)) != ERROR_SUCCESS) {
            return ret;
        }
        if (!msg) {
            continue;
        }
        
        SrsAutoFree(SrsMessage, msg);
        
        if (msg->header.is_audio()) {
            *type = SRS_RTMP_TYPE_AUDIO;
            *timestamp = (u_int32_t)msg->header.timestamp;
            *data = (char*)msg->payload;
            *size = (int)msg->size;
            // detach bytes from packet.
            msg->payload = NULL;
        } else if (msg->header.is_video()) {
            *type = SRS_RTMP_TYPE_VIDEO;
            *timestamp = (u_int32_t)msg->header.timestamp;
            *data = (char*)msg->payload;
            *size = (int)msg->size;
            // detach bytes from packet.
            msg->payload = NULL;
        } else if (msg->header.is_amf0_data() || msg->header.is_amf3_data()) {
            *type = SRS_RTMP_TYPE_SCRIPT;
            *data = (char*)msg->payload;
            *size = (int)msg->size;
            // detach bytes from packet.
            msg->payload = NULL;
        } else {
            // ignore and continue
            continue;
        }
        
        // got expected message.
        break;
    }
    
    return ret;
}

int srs_write_packet(srs_rtmp_t rtmp, char type, u_int32_t timestamp, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    SrsSharedPtrMessage* msg = NULL;
    
    if (type == SRS_RTMP_TYPE_AUDIO) {
        SrsMessageHeader header;
        header.initialize_audio(size, timestamp, context->stream_id);
        
        msg = new SrsSharedPtrMessage();
        if ((ret = msg->create(&header, data, size)) != ERROR_SUCCESS) {
            srs_freep(data);
            return ret;
        }
    } else if (type == SRS_RTMP_TYPE_VIDEO) {
        SrsMessageHeader header;
        header.initialize_video(size, timestamp, context->stream_id);
        
        msg = new SrsSharedPtrMessage();
        if ((ret = msg->create(&header, data, size)) != ERROR_SUCCESS) {
            srs_freep(data);
            return ret;
        }
    } else if (type == SRS_RTMP_TYPE_SCRIPT) {
        SrsMessageHeader header;
        header.initialize_amf0_script(size, context->stream_id);
        
        msg = new SrsSharedPtrMessage();
        if ((ret = msg->create(&header, data, size)) != ERROR_SUCCESS) {
            srs_freep(data);
            return ret;
        }
    }
    
    if (msg) {
        // send out encoded msg.
        if ((ret = context->rtmp->send_and_free_message(msg, context->stream_id)) != ERROR_SUCCESS) {
            return ret;
        }
    } else {
        // directly free data if not sent out.
        srs_freep(data);
    }
    
    return ret;
}

int srs_version_major()
{
    return VERSION_MAJOR;
}

int srs_version_minor()
{
    return VERSION_MINOR;
}

int srs_version_revision()
{
    return VERSION_REVISION;
}

int64_t srs_get_time_ms()
{
    srs_update_system_time_ms();
    return srs_get_system_time_ms();
}

int64_t srs_get_nsend_bytes(srs_rtmp_t rtmp)
{
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    return context->rtmp->get_send_bytes();
}

int64_t srs_get_nrecv_bytes(srs_rtmp_t rtmp)
{
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    return context->rtmp->get_recv_bytes();
}

int srs_parse_timestamp(
    u_int32_t time, char type, char* data, int size,
    u_int32_t* ppts
) {
    int ret = ERROR_SUCCESS;
    
    if (type != SRS_RTMP_TYPE_VIDEO) {
        *ppts = time;
        return ret;
    }

    if (!SrsFlvCodec::video_is_h264(data, size)) {
        return ERROR_FLV_INVALID_VIDEO_TAG;
    }

    if (SrsFlvCodec::video_is_sequence_header(data, size)) {
        *ppts = time;
        return ret;
    }
    
    // 1bytes, frame type and codec id.
    // 1bytes, avc packet type.
    // 3bytes, cts, composition time,
    //      pts = dts + cts, or 
    //      cts = pts - dts.
    if (size < 5) {
        return ERROR_FLV_INVALID_VIDEO_TAG;
    }
    
    u_int32_t cts = 0;
    char* p = data + 2;
    char* pp = (char*)&cts;
    pp[2] = *p++;
    pp[1] = *p++;
    pp[0] = *p++;

    *ppts = time + cts;
    
    return ret;
}

const char* srs_format_time()
{
    struct timeval tv;
    static char buf[23];
    
    memset(buf, 0, sizeof(buf));
    
    // clock time
    if (gettimeofday(&tv, NULL) == -1) {
        return buf;
    }
    
    // to calendar time
    struct tm* tm;
    if ((tm = localtime(&tv.tv_sec)) == NULL) {
        return buf;
    }
    
    snprintf(buf, sizeof(buf), 
        "%d-%02d-%02d %02d:%02d:%02d.%03d", 
        1900 + tm->tm_year, 1 + tm->tm_mon, tm->tm_mday, 
        tm->tm_hour, tm->tm_min, tm->tm_sec, 
        (int)(tv.tv_usec / 1000));
    
    return buf;
}

struct FlvContext
{
    SrsFileReader reader;
    SrsFileWriter writer;
    SrsFlvEncoder enc;
    SrsFlvDecoder dec;
};

srs_flv_t srs_flv_open_read(const char* file)
{
    int ret = ERROR_SUCCESS;
    
    FlvContext* flv = new FlvContext();
    
    if ((ret = flv->reader.open(file)) != ERROR_SUCCESS) {
        srs_freep(flv);
        return NULL;
    }
    
    if ((ret = flv->dec.initialize(&flv->reader)) != ERROR_SUCCESS) {
        srs_freep(flv);
        return NULL;
    }
    
    return flv;
}

srs_flv_t srs_flv_open_write(const char* file)
{
    int ret = ERROR_SUCCESS;
    
    FlvContext* flv = new FlvContext();
    
    if ((ret = flv->writer.open(file)) != ERROR_SUCCESS) {
        srs_freep(flv);
        return NULL;
    }
    
    if ((ret = flv->enc.initialize(&flv->writer)) != ERROR_SUCCESS) {
        srs_freep(flv);
        return NULL;
    }
    
    return flv;
}

void srs_flv_close(srs_flv_t flv)
{
    FlvContext* context = (FlvContext*)flv;
    srs_freep(context);
}

int srs_flv_read_header(srs_flv_t flv, char header[9])
{
    int ret = ERROR_SUCCESS;
    
    FlvContext* context = (FlvContext*)flv;

    if (!context->reader.is_open()) {
        return ERROR_SYSTEM_IO_INVALID;
    }
    
    if ((ret = context->dec.read_header(header)) != ERROR_SUCCESS) {
        return ret;
    }
    
    char ts[4]; // tag size
    if ((ret = context->dec.read_previous_tag_size(ts)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int srs_flv_read_tag_header(srs_flv_t flv, char* ptype, int32_t* pdata_size, u_int32_t* ptime)
{
    int ret = ERROR_SUCCESS;
    
    FlvContext* context = (FlvContext*)flv;

    if (!context->reader.is_open()) {
        return ERROR_SYSTEM_IO_INVALID;
    }
    
    if ((ret = context->dec.read_tag_header(ptype, pdata_size, ptime)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int srs_flv_read_tag_data(srs_flv_t flv, char* data, int32_t size)
{
    int ret = ERROR_SUCCESS;
    
    FlvContext* context = (FlvContext*)flv;

    if (!context->reader.is_open()) {
        return ERROR_SYSTEM_IO_INVALID;
    }
    
    if ((ret = context->dec.read_tag_data(data, size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    char ts[4]; // tag size
    if ((ret = context->dec.read_previous_tag_size(ts)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int srs_flv_write_header(srs_flv_t flv, char header[9])
{
    int ret = ERROR_SUCCESS;
    
    FlvContext* context = (FlvContext*)flv;

    if (!context->writer.is_open()) {
        return ERROR_SYSTEM_IO_INVALID;
    }
    
    if ((ret = context->enc.write_header(header)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int srs_flv_write_tag(srs_flv_t flv, char type, int32_t time, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    FlvContext* context = (FlvContext*)flv;

    if (!context->writer.is_open()) {
        return ERROR_SYSTEM_IO_INVALID;
    }
    
    if (type == SRS_RTMP_TYPE_AUDIO) {
        return context->enc.write_audio(time, data, size);
    } else if (type == SRS_RTMP_TYPE_VIDEO) {
        return context->enc.write_video(time, data, size);
    } else {
        return context->enc.write_metadata(data, size);
    }

    return ret;
}

int srs_flv_size_tag(int data_size)
{
    return SrsFlvEncoder::size_tag(data_size);
}

int64_t srs_flv_tellg(srs_flv_t flv)
{
    FlvContext* context = (FlvContext*)flv;
    return context->reader.tellg();
}

void srs_flv_lseek(srs_flv_t flv, int64_t offset)
{
    FlvContext* context = (FlvContext*)flv;
    context->reader.lseek(offset);
}

srs_flv_bool srs_flv_is_eof(int error_code)
{
    return error_code == ERROR_SYSTEM_FILE_EOF;
}

srs_flv_bool srs_flv_is_sequence_header(char* data, int32_t size)
{
    return SrsFlvCodec::video_is_sequence_header(data, (int)size);
}

srs_flv_bool srs_flv_is_keyframe(char* data, int32_t size)
{
    return SrsFlvCodec::video_is_keyframe(data, (int)size);
}

srs_amf0_t srs_amf0_parse(char* data, int size, int* nparsed)
{
    int ret = ERROR_SUCCESS;
    
    srs_amf0_t amf0 = NULL;
    
    SrsStream stream;
    if ((ret = stream.initialize(data, size)) != ERROR_SUCCESS) {
        return amf0;
    }
    
    SrsAmf0Any* any = NULL;
    if ((ret = SrsAmf0Any::discovery(&stream, &any)) != ERROR_SUCCESS) {
        return amf0;
    }
    
    stream.skip(-1 * stream.pos());
    if ((ret = any->read(&stream)) != ERROR_SUCCESS) {
        srs_freep(any);
        return amf0;
    }
    
    *nparsed = stream.pos();
    amf0 = (srs_amf0_t)any;
    
    return amf0;
}

srs_amf0_t srs_amf0_create_number(srs_amf0_number value)
{
    return SrsAmf0Any::number(value);
}

srs_amf0_t srs_amf0_create_ecma_array()
{
    return SrsAmf0Any::ecma_array();
}

srs_amf0_t srs_amf0_create_strict_array()
{
    return SrsAmf0Any::strict_array();
}

srs_amf0_t srs_amf0_create_object()
{
    return SrsAmf0Any::object();
}

void srs_amf0_free(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_freep(any);
}

void srs_amf0_free_bytes(char* data)
{
    srs_freep(data);
}

int srs_amf0_size(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->total_size();
}

int srs_amf0_serialize(srs_amf0_t amf0, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    
    SrsStream stream;
    if ((ret = stream.initialize(data, size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = any->write(&stream)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

srs_amf0_bool srs_amf0_is_string(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_string();
}

srs_amf0_bool srs_amf0_is_boolean(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_boolean();
}

srs_amf0_bool srs_amf0_is_number(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_number();
}

srs_amf0_bool srs_amf0_is_null(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_null();
}

srs_amf0_bool srs_amf0_is_object(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_object();
}

srs_amf0_bool srs_amf0_is_ecma_array(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_ecma_array();
}

srs_amf0_bool srs_amf0_is_strict_array(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_strict_array();
}

const char* srs_amf0_to_string(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->to_str_raw();
}

srs_amf0_bool srs_amf0_to_boolean(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->to_boolean();
}

srs_amf0_number srs_amf0_to_number(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->to_number();
}

void srs_amf0_set_number(srs_amf0_t amf0, srs_amf0_number value)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    any->set_number(value);
}

int srs_amf0_object_property_count(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_object());

    SrsAmf0Object* obj = (SrsAmf0Object*)amf0;
    return obj->count();
}

const char* srs_amf0_object_property_name_at(srs_amf0_t amf0, int index)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_object());

    SrsAmf0Object* obj = (SrsAmf0Object*)amf0;
    return obj->key_raw_at(index);
}

srs_amf0_t srs_amf0_object_property_value_at(srs_amf0_t amf0, int index)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_object());

    SrsAmf0Object* obj = (SrsAmf0Object*)amf0;
    return (srs_amf0_t)obj->value_at(index);
}

srs_amf0_t srs_amf0_object_property(srs_amf0_t amf0, const char* name)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_object());

    SrsAmf0Object* obj = (SrsAmf0Object*)amf0;
    return (srs_amf0_t)obj->get_property(name);
}

void srs_amf0_object_property_set(srs_amf0_t amf0, const char* name, srs_amf0_t value)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_object());

    SrsAmf0Object* obj = (SrsAmf0Object*)amf0;
    any = (SrsAmf0Any*)value;
    obj->set(name, any);
}

void srs_amf0_object_clear(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_object());

    SrsAmf0Object* obj = (SrsAmf0Object*)amf0;
    obj->clear();
}

int srs_amf0_ecma_array_property_count(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_ecma_array());

    SrsAmf0EcmaArray * obj = (SrsAmf0EcmaArray*)amf0;
    return obj->count();
}

const char* srs_amf0_ecma_array_property_name_at(srs_amf0_t amf0, int index)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_ecma_array());

    SrsAmf0EcmaArray* obj = (SrsAmf0EcmaArray*)amf0;
    return obj->key_raw_at(index);
}

srs_amf0_t srs_amf0_ecma_array_property_value_at(srs_amf0_t amf0, int index)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_ecma_array());

    SrsAmf0EcmaArray* obj = (SrsAmf0EcmaArray*)amf0;
    return (srs_amf0_t)obj->value_at(index);
}

srs_amf0_t srs_amf0_ecma_array_property(srs_amf0_t amf0, const char* name)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_ecma_array());

    SrsAmf0EcmaArray* obj = (SrsAmf0EcmaArray*)amf0;
    return (srs_amf0_t)obj->get_property(name);
}

void srs_amf0_ecma_array_property_set(srs_amf0_t amf0, const char* name, srs_amf0_t value)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_ecma_array());

    SrsAmf0EcmaArray* obj = (SrsAmf0EcmaArray*)amf0;
    any = (SrsAmf0Any*)value;
    obj->set(name, any);
}

int srs_amf0_strict_array_property_count(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_strict_array());

    SrsAmf0StrictArray * obj = (SrsAmf0StrictArray*)amf0;
    return obj->count();
}

srs_amf0_t srs_amf0_strict_array_property_at(srs_amf0_t amf0, int index)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_strict_array());

    SrsAmf0StrictArray* obj = (SrsAmf0StrictArray*)amf0;
    return (srs_amf0_t)obj->at(index);
}

void srs_amf0_strict_array_append(srs_amf0_t amf0, srs_amf0_t value)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    srs_assert(any->is_strict_array());

    SrsAmf0StrictArray* obj = (SrsAmf0StrictArray*)amf0;
    any = (SrsAmf0Any*)value;
    obj->append(any);
}

char* srs_amf0_human_print(srs_amf0_t amf0, char** pdata, int* psize)
{
    if (!amf0) {
        return NULL;
    }
    
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    
    return any->human_print(pdata, psize);
}

/**
* write h264 packet, with rtmp header.
* @param frame_type, SrsCodecVideoAVCFrameKeyFrame or SrsCodecVideoAVCFrameInterFrame.
* @param avc_packet_type, SrsCodecVideoAVCTypeSequenceHeader or SrsCodecVideoAVCTypeNALU.
* @param h264_raw_data the h.264 raw data, user must free it.
*/
int __srs_write_h264_packet(Context* context, 
    int8_t frame_type, int8_t avc_packet_type, 
    char* h264_raw_data, int h264_raw_size, u_int32_t dts, u_int32_t pts
) {
    // the timestamp in rtmp message header is dts.
    u_int32_t timestamp = dts;
    
    // for h264 in RTMP video payload, there is 5bytes header:
    //      1bytes, FrameType | CodecID
    //      1bytes, AVCPacketType
    //      3bytes, CompositionTime, the cts.
    // @see: E.4.3 Video Tags, video_file_format_spec_v10_1.pdf, page 78
    int size = h264_raw_size + 5;
    char* data = new char[size];
    memcpy(data + 5, h264_raw_data, h264_raw_size);
    char* p = data;
    
    // @see: E.4.3 Video Tags, video_file_format_spec_v10_1.pdf, page 78
    // Frame Type, Type of video frame.
    // CodecID, Codec Identifier.
    // set the rtmp header
    *p++ = (frame_type << 4) | SrsCodecVideoAVC;
    
    // AVCPacketType
    *p++ = avc_packet_type;

    // CompositionTime
    // pts = dts + cts, or 
    // cts = pts - dts.
    // where cts is the header in rtmp video packet payload header.
    u_int32_t cts = pts - dts;
    char* pp = (char*)&cts;
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];
    
    return srs_write_packet(context, SRS_RTMP_TYPE_VIDEO, timestamp, data, size);
}

/**
* write the h264 sps/pps in context over RTMP.
*/
int __srs_write_h264_sps_pps(Context* context, u_int32_t dts, u_int32_t pts)
{
    int ret = ERROR_SUCCESS;
    
    // when pps or sps not ready, ignore.
    if (context->h264_pps.empty() || context->h264_sps.empty()) {
        return ret;
    }
    
    // 5bytes sps/pps header:
    //      configurationVersion, AVCProfileIndication, profile_compatibility,
    //      AVCLevelIndication, lengthSizeMinusOne
    // 3bytes size of sps:
    //      numOfSequenceParameterSets, sequenceParameterSetLength(2B)
    // Nbytes of sps.
    //      sequenceParameterSetNALUnit
    // 3bytes size of pps:
    //      numOfPictureParameterSets, pictureParameterSetLength
    // Nbytes of pps:
    //      pictureParameterSetNALUnit
    int nb_packet = 5 
        + 3 + (int)context->h264_sps.length() 
        + 3 + (int)context->h264_pps.length();
    char* packet = new char[nb_packet];
    SrsAutoFree(char, packet);
    
    // use stream to generate the h264 packet.
    SrsStream stream;
    if ((ret = stream.initialize(packet, nb_packet)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // decode the SPS: 
    // @see: 7.3.2.1.1, H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 62
    if (true) {
        srs_assert((int)context->h264_sps.length() >= 4);
        char* frame = (char*)context->h264_sps.data();
    
        // @see: Annex A Profiles and levels, H.264-AVC-ISO_IEC_14496-10.pdf, page 205
        //      Baseline profile profile_idc is 66(0x42).
        //      Main profile profile_idc is 77(0x4d).
        //      Extended profile profile_idc is 88(0x58).
        u_int8_t profile_idc = frame[1];
        //u_int8_t constraint_set = frame[2];
        u_int8_t level_idc = frame[3];
        
        // generate the sps/pps header
        // 5.3.4.2.1 Syntax, H.264-AVC-ISO_IEC_14496-15.pdf, page 16
        // configurationVersion
        stream.write_1bytes(0x01);
        // AVCProfileIndication
        stream.write_1bytes(profile_idc);
        // profile_compatibility
        stream.write_1bytes(0x00);
        // AVCLevelIndication
        stream.write_1bytes(level_idc);
        // lengthSizeMinusOne, or NAL_unit_length, always use 4bytes size,
        // so we always set it to 0x03.
        stream.write_1bytes(0x03);
    }
    
    // sps
    if (true) {
        // 5.3.4.2.1 Syntax, H.264-AVC-ISO_IEC_14496-15.pdf, page 16
        // numOfSequenceParameterSets, always 1
        stream.write_1bytes(0x01);
        // sequenceParameterSetLength
        stream.write_2bytes(context->h264_sps.length());
        // sequenceParameterSetNALUnit
        stream.write_string(context->h264_sps);
    }
    
    // pps
    if (true) {
        // 5.3.4.2.1 Syntax, H.264-AVC-ISO_IEC_14496-15.pdf, page 16
        // numOfPictureParameterSets, always 1
        stream.write_1bytes(0x01);
        // pictureParameterSetLength
        stream.write_2bytes(context->h264_pps.length());
        // pictureParameterSetNALUnit
        stream.write_string(context->h264_pps);
    }
    
    // reset sps and pps.
    context->h264_pps = "";
    context->h264_sps = "";
    context->h264_sps_pps_sent = true;
    
    // TODO: FIXME: for more profile.
    // 5.3.4.2.1 Syntax, H.264-AVC-ISO_IEC_14496-15.pdf, page 16
    // profile_idc == 100 || profile_idc == 110 || profile_idc == 122 || profile_idc == 144
    
    // send out h264 packet.
    int8_t frame_type = SrsCodecVideoAVCFrameKeyFrame;
    int8_t avc_packet_type = SrsCodecVideoAVCTypeSequenceHeader;
    return __srs_write_h264_packet(
        context, frame_type, avc_packet_type,
        packet, nb_packet, dts, pts
    );
}

/**
* write h264 IPB-frame.
*/
int __srs_write_h264_ipb_frame(Context* context, 
    char* data, int size, u_int32_t dts, u_int32_t pts
) {
    int ret = ERROR_SUCCESS;
    
    // when sps or pps not sent, ignore the packet.
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/203
    if (!context->h264_sps_pps_sent) {
        return ERROR_H264_DROP_BEFORE_SPS_PPS;
    }
    
    // 5bits, 7.3.1 NAL unit syntax, 
    // H.264-AVC-ISO_IEC_14496-10.pdf, page 44.
    //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
    u_int8_t nal_unit_type = (char)data[0] & 0x1f;
    
    // 4bytes size of nalu:
    //      NALUnitLength
    // Nbytes of nalu.
    //      NALUnit
    int nb_packet = 4 + size;
    char* packet = new char[nb_packet];
    SrsAutoFree(char, packet);
    
    // use stream to generate the h264 packet.
    SrsStream stream;
    if ((ret = stream.initialize(packet, nb_packet)) != ERROR_SUCCESS) {
        return ret;
    }

    // 5.3.4.2.1 Syntax, H.264-AVC-ISO_IEC_14496-15.pdf, page 16
    // lengthSizeMinusOne, or NAL_unit_length, always use 4bytes size
    u_int32_t NAL_unit_length = size;
    
    // mux the avc NALU in "ISO Base Media File Format" 
    // from H.264-AVC-ISO_IEC_14496-15.pdf, page 20
    // NALUnitLength
    stream.write_4bytes(NAL_unit_length);
    // NALUnit
    stream.write_bytes(data, size);
    
    // send out h264 packet.
    int8_t frame_type = SrsCodecVideoAVCFrameInterFrame;
    if (nal_unit_type != 1) {
        frame_type = SrsCodecVideoAVCFrameKeyFrame;
    }
    int8_t avc_packet_type = SrsCodecVideoAVCTypeNALU;
    return __srs_write_h264_packet(
        context, frame_type, avc_packet_type,
        packet, nb_packet, dts, pts
    );
    
    return ret;
}

/**
* write h264 raw frame, maybe sps/pps/IPB-frame.
*/
int __srs_write_h264_raw_frame(Context* context, 
    char* frame, int frame_size, u_int32_t dts, u_int32_t pts
) {
    int ret = ERROR_SUCCESS;
    
    // ignore invalid frame,
    // atleast 1bytes for SPS to decode the type
    if (frame_size < 1) {
        return ret;
    }
    
    // 5bits, 7.3.1 NAL unit syntax, 
    // H.264-AVC-ISO_IEC_14496-10.pdf, page 44.
    //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
    u_int8_t nal_unit_type = (char)frame[0] & 0x1f;
    
    if (nal_unit_type == 7) {
        // atleast 1bytes for SPS to decode the type, profile, constrain and level.
        if (frame_size < 4) {
            return ret;
        }
        
        context->h264_sps = "";
        context->h264_sps.append(frame, frame_size);
        
        return __srs_write_h264_sps_pps(context, dts, pts);
    } else if (nal_unit_type == 8) {
        context->h264_pps = "";
        context->h264_pps.append(frame, frame_size);
        
        return __srs_write_h264_sps_pps(context, dts, pts);
    } else {
        return __srs_write_h264_ipb_frame(context, frame, frame_size, dts, pts);
    }
    
    return ret;
}

/**
* write h264 multiple frames, in annexb format.
*/
int srs_h264_write_raw_frames(srs_rtmp_t rtmp, 
    char* frames, int frames_size, u_int32_t dts, u_int32_t pts
) {
    int ret = ERROR_SUCCESS;
    
    srs_assert(frames != NULL);
    srs_assert(frames_size > 0);
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    if ((ret = context->h264_raw_stream.initialize(frames, frames_size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // send each frame.
    while (!context->h264_raw_stream.empty()) {
        // each frame must prefixed by annexb format.
        // about annexb, @see H.264-AVC-ISO_IEC_14496-10.pdf, page 211.
        int pnb_start_code = 0;
        if (!srs_avc_startswith_annexb(&context->h264_raw_stream, &pnb_start_code)) {
            return ERROR_H264_API_NO_PREFIXED;
        }
        int start = context->h264_raw_stream.pos() + pnb_start_code;
        
        // find the last frame prefixed by annexb format.
        context->h264_raw_stream.skip(pnb_start_code);
        while (!context->h264_raw_stream.empty()) {
            if (srs_avc_startswith_annexb(&context->h264_raw_stream, NULL)) {
                break;
            }
            context->h264_raw_stream.skip(1);
        }
        int size = context->h264_raw_stream.pos() - start;
        
        // send out the frame.
        char* frame = context->h264_raw_stream.data() + start;
        if ((ret = __srs_write_h264_raw_frame(context, frame, size, dts, pts)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
}

srs_h264_bool srs_h264_is_dvbsp_error(int error_code)
{
    return error_code == ERROR_H264_DROP_BEFORE_SPS_PPS;
}

int srs_h264_startswith_annexb(char* h264_raw_data, int h264_raw_size, int* pnb_start_code)
{
    SrsStream stream;
    if (stream.initialize(h264_raw_data, h264_raw_size) != ERROR_SUCCESS) {
        return false;
    }
    
    return srs_avc_startswith_annexb(&stream, pnb_start_code);
}

#ifdef __cplusplus
}
#endif

