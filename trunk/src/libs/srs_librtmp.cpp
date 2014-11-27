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

// for srs-librtmp, @see https://github.com/winlinvip/simple-rtmp-server/issues/213
#ifndef _WIN32
#include <sys/time.h>
#endif

#include <string>
#include <sstream>
using namespace std;

#include <srs_platform.hpp>
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
    // @see: https://github.com/winlinvip/simple-rtmp-server/issues/66#issuecomment-62240521
    SrsStream h264_raw_stream;
    // about SPS, @see: 7.3.2.1.1, H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 62
    std::string h264_sps;
    std::string h264_pps;
    // whether the sps and pps sent,
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/203
    bool h264_sps_pps_sent;
    // only send the ssp and pps when both changed.
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/204
    bool h264_sps_changed;
    bool h264_pps_changed;
    // for aac raw stream,
    // @see: https://github.com/winlinvip/simple-rtmp-server/issues/212#issuecomment-64146250
    SrsStream aac_raw_stream;
    // the aac sequence header.
    std::string aac_specific_config;
    
    Context() {
        rtmp = NULL;
        skt = NULL;
        stream_id = 0;
        h264_sps_pps_sent = false;
        h264_sps_changed = false;
        h264_pps_changed = false;
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

int srs_rtmp_handshake(srs_rtmp_t rtmp)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = __srs_rtmp_dns_resolve(rtmp)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = __srs_rtmp_connect_server(rtmp)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = __srs_rtmp_do_simple_handshake(rtmp)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int __srs_rtmp_dns_resolve(srs_rtmp_t rtmp)
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

int __srs_rtmp_connect_server(srs_rtmp_t rtmp)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    if ((ret = srs_librtmp_context_connect(context)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int __srs_rtmp_do_simple_handshake(srs_rtmp_t rtmp)
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

int srs_rtmp_connect_app(srs_rtmp_t rtmp)
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

int srs_rtmp_connect_app2(srs_rtmp_t rtmp,
    char srs_server_ip[128],char srs_server[128], 
    char srs_primary[128], char srs_authors[128], 
    char srs_version[32], int* srs_id, int* srs_pid
) {
    srs_server_ip[0] = 0;
    srs_server[0] = 0;
    srs_primary[0] = 0;
    srs_authors[0] = 0;
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
    
    std::string sip, sserver, sprimary, sauthors, sversion;
    
    if ((ret = context->rtmp->connect_app2(context->app, tcUrl, NULL, true,
        sip, sserver, sprimary, sauthors, sversion, *srs_id, *srs_pid)) != ERROR_SUCCESS) {
        return ret;
    }
    
    snprintf(srs_server_ip, 128, "%s", sip.c_str());
    snprintf(srs_server, 128, "%s", sserver.c_str());
    snprintf(srs_primary, 128, "%s", sprimary.c_str());
    snprintf(srs_authors, 128, "%s", sauthors.c_str());
    snprintf(srs_version, 32, "%s", sversion.c_str());
    
    return ret;
}

int srs_rtmp_play_stream(srs_rtmp_t rtmp)
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

int srs_rtmp_publish_stream(srs_rtmp_t rtmp)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    if ((ret = context->rtmp->fmle_publish(context->stream, context->stream_id)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int srs_rtmp_bandwidth_check(srs_rtmp_t rtmp, 
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

int srs_rtmp_read_packet(srs_rtmp_t rtmp, char* type, u_int32_t* timestamp, char** data, int* size)
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

int srs_rtmp_write_packet(srs_rtmp_t rtmp, char type, u_int32_t timestamp, char* data, int size)
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

/**
* directly write a audio frame.
*/
int __srs_write_audio_raw_frame(Context* context,
    char sound_format, char sound_rate, char sound_size, char sound_type,
    char aac_packet_type, char* frame, int frame_size, u_int32_t timestamp
) {
    
    // for audio frame, there is 1 or 2 bytes header:
    //      1bytes, SoundFormat|SoundRate|SoundSize|SoundType
    //      1bytes, AACPacketType for SoundFormat == 10, 0 is sequence header.
    int size = frame_size + 1;
    if (sound_format == SrsCodecAudioAAC) {
        size += 1;
    }
    char* data = new char[size];
    char* p = data;
    
    u_int8_t audio_header = sound_type & 0x01;
    audio_header |= (sound_size << 1) & 0x02;
    audio_header |= (sound_rate << 2) & 0x0c;
    audio_header |= (sound_format << 4) & 0xf0;
    
    *p++ = audio_header;
    
    if (sound_format == SrsCodecAudioAAC) {
        *p++ = aac_packet_type;
    }
    
    memcpy(p, frame, frame_size);
    
    return srs_rtmp_write_packet(context, SRS_RTMP_TYPE_AUDIO, timestamp, data, size);
}

/**
* write aac frame in adts.
*/
int __srs_write_aac_adts_frame(Context* context,
    char sound_format, char sound_rate, char sound_size, char sound_type,
    char aac_profile, char aac_samplerate, char aac_channel,
    char* frame, int frame_size, u_int32_t timestamp
) {
    int ret = ERROR_SUCCESS;
    
    // override the aac samplerate by user specified.
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/212#issuecomment-64146899
    switch (sound_rate) {
        case SrsCodecAudioSampleRate11025: 
            aac_samplerate = 0x0a; break;
        case SrsCodecAudioSampleRate22050: 
            aac_samplerate = 0x07; break;
        case SrsCodecAudioSampleRate44100: 
            aac_samplerate = 0x04; break;
        default:
            break;
    }
    
    // send out aac sequence header if not sent.
    if (context->aac_specific_config.empty()) {
        char ch = 0;
        // @see aac-mp4a-format-ISO_IEC_14496-3+2001.pdf
        // AudioSpecificConfig (), page 33
        // 1.6.2.1 AudioSpecificConfig
        // audioObjectType; 5 bslbf
        ch = (aac_profile << 3) & 0xf8;
        // 3bits left.
        
        // samplingFrequencyIndex; 4 bslbf
        ch |= (aac_samplerate >> 1) & 0x07;
        context->aac_specific_config += ch;
        ch = (aac_samplerate << 7) & 0x80;
        if (aac_samplerate == 0x0f) {
            return ERROR_AAC_DATA_INVALID;
        }
        // 7bits left.
        
        // channelConfiguration; 4 bslbf
        ch |= (aac_channel << 3) & 0x70;
        // 3bits left.
        
        // only support aac profile 1-4.
        if (aac_profile < 1 || aac_profile > 4) {
            return ERROR_AAC_DATA_INVALID;
        }
        // GASpecificConfig(), page 451
        // 4.4.1 Decoder configuration (GASpecificConfig)
        // frameLengthFlag; 1 bslbf
        // dependsOnCoreCoder; 1 bslbf
        // extensionFlag; 1 bslbf
        context->aac_specific_config += ch;
        
        if ((ret = __srs_write_audio_raw_frame(context, 
            sound_format, sound_rate, sound_size, sound_type, 
            0, (char*)context->aac_specific_config.data(), 
            context->aac_specific_config.length(), 
            timestamp)) != ERROR_SUCCESS
        ) {
            return ret;
        }
    }
    
    return __srs_write_audio_raw_frame(context, 
        sound_format, sound_rate, sound_size, sound_type, 
        1, frame, frame_size, timestamp);
}

/**
* write aac frames in adts.
*/
int __srs_write_aac_adts_frames(Context* context,
    char sound_format, char sound_rate, char sound_size, char sound_type,
    char* frame, int frame_size, u_int32_t timestamp
) {
    int ret = ERROR_SUCCESS;
    
    SrsStream* stream = &context->aac_raw_stream;
    if ((ret = stream->initialize(frame, frame_size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    while (!stream->empty()) {
        int adts_header_start = stream->pos();
        
        // decode the ADTS.
        // @see aac-mp4a-format-ISO_IEC_14496-3+2001.pdf, page 75,
        //      1.A.2.2 Audio_Data_Transport_Stream frame, ADTS
        // @see https://github.com/winlinvip/simple-rtmp-server/issues/212#issuecomment-64145885
        // byte_alignment()
        
        // adts_fixed_header:
        //      12bits syncword,
        //      16bits left.
        // adts_variable_header:
        //      28bits
        //      12+16+28=56bits
        // adts_error_check:
        //      16bits if protection_absent
        //      56+16=72bits
        // if protection_absent:
        //      require(7bytes)=56bits
        // else
        //      require(9bytes)=72bits
        if (!stream->require(7)) {
            return ERROR_AAC_ADTS_HEADER;
        }
        
        // for aac, the frame must be ADTS format.
        if (!srs_aac_startswith_adts(stream)) {
            return ERROR_AAC_REQUIRED_ADTS;
        }
        
        // Syncword 12 bslbf
        stream->read_1bytes();
        // 4bits left.
        // adts_fixed_header(), 1.A.2.2.1 Fixed Header of ADTS
        // ID 1 bslbf
        // Layer 2 uimsbf
        // protection_absent 1 bslbf
        int8_t fh0 = (stream->read_1bytes() & 0x0f);
        /*int8_t fh_id = (fh0 >> 3) & 0x01;*/
        /*int8_t fh_layer = (fh0 >> 1) & 0x03;*/
        int8_t fh_protection_absent = fh0 & 0x01;
        
        int16_t fh1 = stream->read_2bytes();
        // Profile_ObjectType 2 uimsbf
        // sampling_frequency_index 4 uimsbf
        // private_bit 1 bslbf
        // channel_configuration 3 uimsbf
        // original/copy 1 bslbf
        // home 1 bslbf
        int8_t fh_Profile_ObjectType = (fh1 >> 14) & 0x03;
        int8_t fh_sampling_frequency_index = (fh1 >> 10) & 0x0f;
        /*int8_t fh_private_bit = (fh1 >> 9) & 0x01;*/
        int8_t fh_channel_configuration = (fh1 >> 6) & 0x07;
        /*int8_t fh_original = (fh1 >> 5) & 0x01;*/
        /*int8_t fh_home = (fh1 >> 4) & 0x01;*/
        // @remark, Emphasis is removed, 
        //      @see https://github.com/winlinvip/simple-rtmp-server/issues/212#issuecomment-64154736
        //int8_t fh_Emphasis = (fh1 >> 2) & 0x03;
        // 4bits left.
        // adts_variable_header(), 1.A.2.2.2 Variable Header of ADTS
        // copyright_identification_bit 1 bslbf
        // copyright_identification_start 1 bslbf
        /*int8_t fh_copyright_identification_bit = (fh1 >> 3) & 0x01;*/
        /*int8_t fh_copyright_identification_start = (fh1 >> 2) & 0x01;*/
        // aac_frame_length 13 bslbf: Length of the frame including headers and error_check in bytes.
        // use the left 2bits as the 13 and 12 bit,
        // the aac_frame_length is 13bits, so we move 13-2=11.
        int16_t fh_aac_frame_length = (fh1 << 11) & 0x0800;
        
        int32_t fh2 = stream->read_3bytes();
        // aac_frame_length 13 bslbf: consume the first 13-2=11bits
        // the fh2 is 24bits, so we move right 24-11=13.
        fh_aac_frame_length |= (fh2 >> 13) & 0x07ff;
        // adts_buffer_fullness 11 bslbf
        /*int16_t fh_adts_buffer_fullness = (fh2 >> 2) & 0x7ff;*/
        // no_raw_data_blocks_in_frame 2 uimsbf
        /*int16_t fh_no_raw_data_blocks_in_frame = fh2 & 0x03;*/
        // adts_error_check(), 1.A.2.2.3 Error detection
        if (!fh_protection_absent) {
            if (!stream->require(2)) {
                return ERROR_AAC_ADTS_HEADER;
            }
            // crc_check 16 Rpchof
            /*int16_t crc_check = */stream->read_2bytes();
        }
        
        // TODO: check the fh_sampling_frequency_index
        // TODO: check the fh_channel_configuration
        
        // raw_data_blocks
        int adts_header_size = stream->pos() - adts_header_start;
        int raw_data_size = fh_aac_frame_length - adts_header_size;
        if (!stream->require(raw_data_size)) {
            return ERROR_AAC_ADTS_HEADER;
        }
        
        char* raw_data = stream->data() + stream->pos();
        if ((ret = __srs_write_aac_adts_frame(context,
            sound_format, sound_rate, sound_size, sound_type,
            fh_Profile_ObjectType, fh_sampling_frequency_index, fh_channel_configuration,
            raw_data, raw_data_size, timestamp)) != ERROR_SUCCESS
        ) {
            return ret;
        }
        stream->skip(raw_data_size);
    }
    
    return ret;
}

/**
* write audio raw frame to SRS.
*/
int srs_audio_write_raw_frame(srs_rtmp_t rtmp, 
    char sound_format, char sound_rate, char sound_size, char sound_type,
    char* frame, int frame_size, u_int32_t timestamp
) {
    int ret = ERROR_SUCCESS;
    
    Context* context = (Context*)rtmp;
    srs_assert(context);
    
    if (sound_format == SrsCodecAudioAAC) {
        // for aac, the frame must be ADTS format.
        if (!srs_aac_is_adts(frame, frame_size)) {
            return ERROR_AAC_REQUIRED_ADTS;
        }
        
        // for aac, demux the ADTS to RTMP format.
        return __srs_write_aac_adts_frames(context, 
            sound_format, sound_rate, sound_size, sound_type, 
            frame, frame_size, timestamp);
    } else {
        // for other data, directly write frame.
        return __srs_write_audio_raw_frame(context, 
            sound_format, sound_rate, sound_size, sound_type, 
            0, frame, frame_size, timestamp);
    }
    
    
    return ret;
}

/**
* whether aac raw data is in adts format,
* which bytes sequence matches '1111 1111 1111'B, that is 0xFFF.
*/
srs_bool srs_aac_is_adts(char* aac_raw_data, int ac_raw_size)
{
    SrsStream stream;
    if (stream.initialize(aac_raw_data, ac_raw_size) != ERROR_SUCCESS) {
        return false;
    }
    
    return srs_aac_startswith_adts(&stream);
}

/**
* parse the adts header to get the frame size.
*/
int srs_aac_adts_frame_size(char* aac_raw_data, int ac_raw_size)
{
    int size = -1;
    
    if (!srs_aac_is_adts(aac_raw_data, ac_raw_size)) {
        return size;
    }
    
    // adts always 7bytes.
    if (ac_raw_size <= 7) {
        return size;
    }
    
    // last 2bits
    int16_t ch3 = aac_raw_data[3];
    // whole 8bits
    int16_t ch4 = aac_raw_data[4];
    // first 3bits
    int16_t ch5 = aac_raw_data[5];
    
    size = ((ch3 << 11) & 0x1800) | ((ch4 << 3) & 0x07f8) | ((ch5 >> 5) & 0x0007);
    
    return size;
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
    
    // h.264 raw data.
    memcpy(p, h264_raw_data, h264_raw_size);
    
    return srs_rtmp_write_packet(context, SRS_RTMP_TYPE_VIDEO, timestamp, data, size);
}

/**
* write the h264 sps/pps in context over RTMP.
*/
int __srs_write_h264_sps_pps(Context* context, u_int32_t dts, u_int32_t pts)
{
    int ret = ERROR_SUCCESS;
    
    // only send when both sps and pps changed.
    if (!context->h264_sps_changed || !context->h264_pps_changed) {
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
    context->h264_sps_changed = false;
    context->h264_pps_changed = false;
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
        
        std::string sps;
        sps.append(frame, frame_size);
        
        if (context->h264_sps == sps) {
            return ERROR_H264_DUPLICATED_SPS;
        }
        context->h264_sps_changed = true;
        context->h264_sps = sps;
        
        return __srs_write_h264_sps_pps(context, dts, pts);
    } else if (nal_unit_type == 8) {
        
        std::string pps;
        pps.append(frame, frame_size);
        
        if (context->h264_pps == pps) {
            return ERROR_H264_DUPLICATED_PPS;
        }
        context->h264_pps_changed = true;
        context->h264_pps = pps;
        
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
    
    // use the last error
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/203
    // @see https://github.com/winlinvip/simple-rtmp-server/issues/204
    int error_code_return = ret;
    
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

        // it may be return error, but we must process all packets.
        if ((ret = __srs_write_h264_raw_frame(context, frame, size, dts, pts)) != ERROR_SUCCESS) {
            error_code_return = ret;
            
            // ignore known error, process all packets.
            if (srs_h264_is_dvbsp_error(ret)
                || srs_h264_is_duplicated_sps_error(ret)
                || srs_h264_is_duplicated_pps_error(ret)
            ) {
                continue;
            }
            
            return ret;
        }
    }
    
    return error_code_return;
}

srs_bool srs_h264_is_dvbsp_error(int error_code)
{
    return error_code == ERROR_H264_DROP_BEFORE_SPS_PPS;
}

srs_bool srs_h264_is_duplicated_sps_error(int error_code)
{
    return error_code == ERROR_H264_DUPLICATED_SPS;
}

srs_bool srs_h264_is_duplicated_pps_error(int error_code)
{
    return error_code == ERROR_H264_DUPLICATED_PPS;
}

srs_bool srs_h264_startswith_annexb(char* h264_raw_data, int h264_raw_size, int* pnb_start_code)
{
    SrsStream stream;
    if (stream.initialize(h264_raw_data, h264_raw_size) != ERROR_SUCCESS) {
        return false;
    }
    
    return srs_avc_startswith_annexb(&stream, pnb_start_code);
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

srs_bool srs_flv_is_eof(int error_code)
{
    return error_code == ERROR_SYSTEM_FILE_EOF;
}

srs_bool srs_flv_is_sequence_header(char* data, int32_t size)
{
    return SrsFlvCodec::video_is_sequence_header(data, (int)size);
}

srs_bool srs_flv_is_keyframe(char* data, int32_t size)
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
    
    if (nparsed) {
        *nparsed = stream.pos();
    }
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

srs_bool srs_amf0_is_string(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_string();
}

srs_bool srs_amf0_is_boolean(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_boolean();
}

srs_bool srs_amf0_is_number(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_number();
}

srs_bool srs_amf0_is_null(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_null();
}

srs_bool srs_amf0_is_object(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_object();
}

srs_bool srs_amf0_is_ecma_array(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_ecma_array();
}

srs_bool srs_amf0_is_strict_array(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_strict_array();
}

const char* srs_amf0_to_string(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->to_str_raw();
}

srs_bool srs_amf0_to_boolean(srs_amf0_t amf0)
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

int64_t srs_utils_time_ms()
{
    srs_update_system_time_ms();
    return srs_get_system_time_ms();
}

int64_t srs_utils_send_bytes(srs_rtmp_t rtmp)
{
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    return context->rtmp->get_send_bytes();
}

int64_t srs_utils_recv_bytes(srs_rtmp_t rtmp)
{
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    return context->rtmp->get_recv_bytes();
}

int srs_utils_parse_timestamp(
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

char srs_utils_flv_video_codec_id(char* data, int size)
{
    if (size < 1) {
        return 0;
    }

    char codec_id = data[0];
    codec_id = codec_id & 0x0F;
    
    return codec_id;
}

char srs_utils_flv_video_avc_packet_type(char* data, int size)
{
    if (size < 2) {
        return -1;
    }
    
    if (!SrsFlvCodec::video_is_h264(data, size)) {
        return -1;
    }
    
    u_int8_t avc_packet_type = data[1];
    
    if (avc_packet_type > 2) {
        return -1;
    }
    
    return avc_packet_type;
}

char srs_utils_flv_video_frame_type(char* data, int size)
{
    if (size < 1) {
        return -1;
    }
    
    if (!SrsFlvCodec::video_is_h264(data, size)) {
        return -1;
    }
    
    u_int8_t frame_type = data[0];
    frame_type = (frame_type >> 4) & 0x0f;
    if (frame_type < 1 || frame_type > 5) {
        return -1;
    }
    
    return frame_type;
}

char srs_utils_flv_audio_sound_format(char* data, int size)
{
    if (size < 1) {
        return -1;
    }
    
    u_int8_t sound_format = data[0];
    sound_format = (sound_format >> 4) & 0x0f;
    if (sound_format > 15 || sound_format == 12 || sound_format == 13) {
        return -1;
    }
    
    return sound_format;
}

char srs_utils_flv_audio_sound_rate(char* data, int size)
{
    if (size < 1) {
        return -1;
    }
    
    u_int8_t sound_rate = data[0];
    sound_rate = (sound_rate >> 2) & 0x03;
    if (sound_rate > 3) {
        return -1;
    }
    
    return sound_rate;
}

char srs_utils_flv_audio_sound_size(char* data, int size)
{
    if (size < 1) {
        return -1;
    }
    
    u_int8_t sound_size = data[0];
    sound_size = (sound_size >> 1) & 0x01;
    if (sound_size > 1) {
        return -1;
    }
    
    return sound_size;
}

char srs_utils_flv_audio_sound_type(char* data, int size)
{
    if (size < 1) {
        return -1;
    }
    
    u_int8_t sound_type = data[0];
    sound_type = sound_type & 0x01;
    if (sound_type > 1) {
        return -1;
    }
    
    return sound_type;
}

char srs_utils_flv_audio_aac_packet_type(char* data, int size)
{
    if (size < 2) {
        return -1;
    }
    
    if (srs_utils_flv_audio_sound_format(data, size) != 10) {
        return -1;
    }
    
    u_int8_t aac_packet_type = data[1];
    aac_packet_type = aac_packet_type;
    if (aac_packet_type > 1) {
        return -1;
    }
    
    return aac_packet_type;
}

char* srs_human_amf0_print(srs_amf0_t amf0, char** pdata, int* psize)
{
    if (!amf0) {
        return NULL;
    }
    
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    
    return any->human_print(pdata, psize);
}

const char* srs_human_flv_tag_type2string(char type)
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

const char* srs_human_flv_video_codec_id2string(char codec_id)
{
    static const char* h263 = "H.263";
    static const char* screen = "Screen";
    static const char* vp6 = "VP6";
    static const char* vp6_alpha = "VP6Alpha";
    static const char* screen2 = "Screen2";
    static const char* h264 = "H.264";
    static const char* unknown = "Unknown";
    
    switch (codec_id) {
        case 2: return h263;
        case 3: return screen;
        case 4: return vp6;
        case 5: return vp6_alpha;
        case 6: return screen2;
        case 7: return h264;
        default: return unknown;
    }
    
    return unknown;
}

const char* srs_human_flv_video_avc_packet_type2string(char avc_packet_type)
{
    static const char* sps_pps = "SH";
    static const char* nalu = "Nalu";
    static const char* sps_pps_end = "SpsPpsEnd";
    static const char* unknown = "Unknown";
    
    switch (avc_packet_type) {
        case 0: return sps_pps;
        case 1: return nalu;
        case 2: return sps_pps_end;
        default: return unknown;
    }
    
    return unknown;
}

const char* srs_human_flv_video_frame_type2string(char frame_type)
{
    static const char* keyframe = "I";
    static const char* interframe = "P/B";
    static const char* disposable_interframe = "DI";
    static const char* generated_keyframe = "GI";
    static const char* video_infoframe = "VI";
    static const char* unknown = "Unknown";
    
    switch (frame_type) {
        case 1: return keyframe;
        case 2: return interframe;
        case 3: return disposable_interframe;
        case 4: return generated_keyframe;
        case 5: return video_infoframe;
        default: return unknown;
    }
    
    return unknown;
}

const char* srs_human_flv_audio_sound_format2string(char sound_format)
{
    static const char* linear_pcm = "LinearPCM";
    static const char* ad_pcm = "ADPCM";
    static const char* mp3 = "MP3";
    static const char* linear_pcm_le = "LinearPCMLe";
    static const char* nellymoser_16khz = "NellymoserKHz16";
    static const char* nellymoser_8khz = "NellymoserKHz8";
    static const char* nellymoser = "Nellymoser";
    static const char* g711_a_pcm = "G711APCM";
    static const char* g711_mu_pcm = "G711MuPCM";
    static const char* reserved = "Reserved";
    static const char* aac = "AAC";
    static const char* speex = "Speex";
    static const char* mp3_8khz = "MP3KHz8";
    static const char* device_specific = "DeviceSpecific";
    static const char* unknown = "Unknown";
    
    switch (sound_format) {
        case 0: return linear_pcm;
        case 1: return ad_pcm;
        case 2: return mp3;
        case 3: return linear_pcm_le;
        case 4: return nellymoser_16khz;
        case 5: return nellymoser_8khz;
        case 6: return nellymoser;
        case 7: return g711_a_pcm;
        case 8: return g711_mu_pcm;
        case 9: return reserved;
        case 10: return aac;
        case 11: return speex;
        case 14: return mp3_8khz;
        case 15: return device_specific;
        default: return unknown;
    }
    
    return unknown;
}

const char* srs_human_flv_audio_sound_rate2string(char sound_rate)
{
    static const char* khz_5_5 = "5.5KHz";
    static const char* khz_11 = "11KHz";
    static const char* khz_22 = "22KHz";
    static const char* khz_44 = "44KHz";
    static const char* unknown = "Unknown";
    
    switch (sound_rate) {
        case 0: return khz_5_5;
        case 1: return khz_11;
        case 2: return khz_22;
        case 3: return khz_44;
        default: return unknown;
    }
    
    return unknown;
}

const char* srs_human_flv_audio_sound_size2string(char sound_size)
{
    static const char* bit_8 = "8bit";
    static const char* bit_16 = "16bit";
    static const char* unknown = "Unknown";
    
    switch (sound_size) {
        case 0: return bit_8;
        case 1: return bit_16;
        default: return unknown;
    }
    
    return unknown;
}

const char* srs_human_flv_audio_sound_type2string(char sound_type)
{
    static const char* mono = "Mono";
    static const char* stereo = "Stereo";
    static const char* unknown = "Unknown";
    
    switch (sound_type) {
        case 0: return mono;
        case 1: return stereo;
        default: return unknown;
    }
    
    return unknown;
}

const char* srs_human_flv_audio_aac_packet_type2string(char aac_packet_type)
{
    static const char* sps_pps = "SH";
    static const char* raw = "Raw";
    static const char* unknown = "Unknown";
    
    switch (aac_packet_type) {
        case 0: return sps_pps;
        case 1: return raw;
        default: return unknown;
    }
    
    return unknown;
}

int srs_human_print_rtmp_packet(char type, u_int32_t timestamp, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    u_int32_t pts;
    if (srs_utils_parse_timestamp(timestamp, type, data, size, &pts) != 0) {
        return ret;
    }
    
    if (type == SRS_RTMP_TYPE_VIDEO) {
        srs_human_trace("Video packet type=%s, dts=%d, pts=%d, size=%d, %s(%s,%s)", 
            srs_human_flv_tag_type2string(type), timestamp, pts, size,
            srs_human_flv_video_codec_id2string(srs_utils_flv_video_codec_id(data, size)),
            srs_human_flv_video_avc_packet_type2string(srs_utils_flv_video_avc_packet_type(data, size)),
            srs_human_flv_video_frame_type2string(srs_utils_flv_video_frame_type(data, size))
        );
    } else if (type == SRS_RTMP_TYPE_AUDIO) {
        srs_human_trace("Audio packet type=%s, dts=%d, pts=%d, size=%d, %s(%s,%s,%s,%s)", 
            srs_human_flv_tag_type2string(type), timestamp, pts, size,
            srs_human_flv_audio_sound_format2string(srs_utils_flv_audio_sound_format(data, size)),
            srs_human_flv_audio_sound_rate2string(srs_utils_flv_audio_sound_rate(data, size)),
            srs_human_flv_audio_sound_size2string(srs_utils_flv_audio_sound_size(data, size)),
            srs_human_flv_audio_sound_type2string(srs_utils_flv_audio_sound_type(data, size)),
            srs_human_flv_audio_aac_packet_type2string(srs_utils_flv_audio_aac_packet_type(data, size))
        );
    } else if (type == SRS_RTMP_TYPE_SCRIPT) {
        srs_human_verbose("Data packet type=%s, time=%d, size=%d", 
            srs_human_flv_tag_type2string(type), timestamp, size);
        int nparsed = 0;
        while (nparsed < size) {
            int nb_parsed_this = 0;
            srs_amf0_t amf0 = srs_amf0_parse(data + nparsed, size - nparsed, &nb_parsed_this);
            if (amf0 == NULL) {
                break;
            }
            
            nparsed += nb_parsed_this;
            
            char* amf0_str = NULL;
            srs_human_raw("%s", srs_human_amf0_print(amf0, &amf0_str, NULL));
            srs_amf0_free_bytes(amf0_str);
        }
    } else {
        srs_human_trace("Unknown packet type=%s, dts=%d, pts=%d, size=%d", 
            srs_human_flv_tag_type2string(type), timestamp, pts, size);
    }
    
    return ret;
}

const char* srs_human_format_time()
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
    if ((tm = localtime((const time_t*)&tv.tv_sec)) == NULL) {
        return buf;
    }
    
    snprintf(buf, sizeof(buf), 
        "%d-%02d-%02d %02d:%02d:%02d.%03d", 
        1900 + tm->tm_year, 1 + tm->tm_mon, tm->tm_mday, 
        tm->tm_hour, tm->tm_min, tm->tm_sec, 
        (int)(tv.tv_usec / 1000));
        
    // for srs-librtmp, @see https://github.com/winlinvip/simple-rtmp-server/issues/213
    buf[sizeof(buf) - 1] = 0;
    
    return buf;
}

#ifdef __cplusplus
}
#endif

