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

#include <srs_rtmp_utility.hpp>

#include <stdlib.h>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_kernel_codec.hpp>

void srs_discovery_tc_url(
    string tcUrl, 
    string& schema, string& host, string& vhost, 
    string& app, string& port, std::string& param
) {
    size_t pos = std::string::npos;
    std::string url = tcUrl;
    
    if ((pos = url.find("://")) != std::string::npos) {
        schema = url.substr(0, pos);
        url = url.substr(schema.length() + 3);
        srs_info("discovery schema=%s", schema.c_str());
    }
    
    if ((pos = url.find("/")) != std::string::npos) {
        host = url.substr(0, pos);
        url = url.substr(host.length() + 1);
        srs_info("discovery host=%s", host.c_str());
    }

    port = SRS_CONSTS_RTMP_DEFAULT_PORT;
    if ((pos = host.find(":")) != std::string::npos) {
        port = host.substr(pos + 1);
        host = host.substr(0, pos);
        srs_info("discovery host=%s, port=%s", host.c_str(), port.c_str());
    }
    
    app = url;
    vhost = host;
    srs_vhost_resolve(vhost, app, param);
}

void srs_vhost_resolve(string& vhost, string& app, string& param)
{
    // get original param
    size_t pos = 0;
    if ((pos = app.find("?")) != std::string::npos) {
        param = app.substr(pos);
    }
    
    // filter tcUrl
    app = srs_string_replace(app, ",", "?");
    app = srs_string_replace(app, "...", "?");
    app = srs_string_replace(app, "&&", "?");
    app = srs_string_replace(app, "=", "?");
    
    if ((pos = app.find("?")) != std::string::npos) {
        std::string query = app.substr(pos + 1);
        app = app.substr(0, pos);
        
        if ((pos = query.find("vhost?")) != std::string::npos) {
            query = query.substr(pos + 6);
            if (!query.empty()) {
                vhost = query;
            }
            if ((pos = vhost.find("?")) != std::string::npos) {
                vhost = vhost.substr(0, pos);
            }
        }
    }
    
    /* others */
}

void srs_random_generate(char* bytes, int size)
{
    static bool _random_initialized = false;
    if (!_random_initialized) {
        srand(0);
        _random_initialized = true;
        srs_trace("srand initialized the random.");
    }
    
    for (int i = 0; i < size; i++) {
        // the common value in [0x0f, 0xf0]
        bytes[i] = 0x0f + (rand() % (256 - 0x0f - 0x0f));
    }
}

string srs_generate_tc_url(string ip, string vhost, string app, string port, string param)
{
    string tcUrl = "rtmp://";
    
    if (vhost == SRS_CONSTS_RTMP_DEFAULT_VHOST) {
        tcUrl += ip;
    } else {
        tcUrl += vhost;
    }
    
    if (port != SRS_CONSTS_RTMP_DEFAULT_PORT) {
        tcUrl += ":";
        tcUrl += port;
    }
    
    tcUrl += "/";
    tcUrl += app;
    tcUrl += param;
    
    return tcUrl;
}

/**
* compare the memory in bytes.
*/
bool srs_bytes_equals(void* pa, void* pb, int size)
{
    u_int8_t* a = (u_int8_t*)pa;
    u_int8_t* b = (u_int8_t*)pb;
    
    if (!a && !b) {
        return true;
    }
    
    if (!a || !b) {
        return false;
    }
    
    for(int i = 0; i < size; i++){
        if(a[i] != b[i]){
            return false;
        }
    }

    return true;
}

int srs_chunk_header_c0(
    int perfer_cid, u_int32_t timestamp, int32_t payload_length,
    int8_t message_type, int32_t stream_id,
    char* cache, int nb_cache
)
{
    // to directly set the field.
    char* pp = NULL;
    
    // generate the header.
    char* p = cache;
    
    // no header.
    if (nb_cache < SRS_CONSTS_RTMP_MAX_FMT0_HEADER_SIZE) {
        return 0;
    }
    
    // write new chunk stream header, fmt is 0
    *p++ = 0x00 | (perfer_cid & 0x3F);
    
    // chunk message header, 11 bytes
    // timestamp, 3bytes, big-endian
    if (timestamp < RTMP_EXTENDED_TIMESTAMP) {
        pp = (char*)&timestamp;
        *p++ = pp[2];
        *p++ = pp[1];
        *p++ = pp[0];
    } else {
        *p++ = 0xFF;
        *p++ = 0xFF;
        *p++ = 0xFF;
    }
    
    // message_length, 3bytes, big-endian
    pp = (char*)&payload_length;
    *p++ = pp[2];
    *p++ = pp[1];
    *p++ = pp[0];
    
    // message_type, 1bytes
    *p++ = message_type;
    
    // stream_id, 4bytes, little-endian
    pp = (char*)&stream_id;
    *p++ = pp[0];
    *p++ = pp[1];
    *p++ = pp[2];
    *p++ = pp[3];
    
    // for c0
    // chunk extended timestamp header, 0 or 4 bytes, big-endian
    // 
    // for c3:
    // chunk extended timestamp header, 0 or 4 bytes, big-endian
    // 6.1.3. Extended Timestamp
    // This field is transmitted only when the normal time stamp in the
    // chunk message header is set to 0x00ffffff. If normal time stamp is
    // set to any value less than 0x00ffffff, this field MUST NOT be
    // present. This field MUST NOT be present if the timestamp field is not
    // present. Type 3 chunks MUST NOT have this field.
    // adobe changed for Type3 chunk:
    //        FMLE always sendout the extended-timestamp,
    //        must send the extended-timestamp to FMS,
    //        must send the extended-timestamp to flash-player.
    // @see: ngx_rtmp_prepare_message
    // @see: http://blog.csdn.net/win_lin/article/details/13363699
    // TODO: FIXME: extract to outer.
    if (timestamp >= RTMP_EXTENDED_TIMESTAMP) {
        pp = (char*)&timestamp;
        *p++ = pp[3];
        *p++ = pp[2];
        *p++ = pp[1];
        *p++ = pp[0];
    }
    
    // always has header
    return p - cache;
}

int srs_chunk_header_c3(
    int perfer_cid, u_int32_t timestamp, 
    char* cache, int nb_cache
)
{
    // to directly set the field.
    char* pp = NULL;
    
    // generate the header.
    char* p = cache;
    
    // no header.
    if (nb_cache < SRS_CONSTS_RTMP_MAX_FMT3_HEADER_SIZE) {
        return 0;
    }

    // write no message header chunk stream, fmt is 3
    // @remark, if perfer_cid > 0x3F, that is, use 2B/3B chunk header,
    // SRS will rollback to 1B chunk header.
    *p++ = 0xC0 | (perfer_cid & 0x3F);
    
    // for c0
    // chunk extended timestamp header, 0 or 4 bytes, big-endian
    // 
    // for c3:
    // chunk extended timestamp header, 0 or 4 bytes, big-endian
    // 6.1.3. Extended Timestamp
    // This field is transmitted only when the normal time stamp in the
    // chunk message header is set to 0x00ffffff. If normal time stamp is
    // set to any value less than 0x00ffffff, this field MUST NOT be
    // present. This field MUST NOT be present if the timestamp field is not
    // present. Type 3 chunks MUST NOT have this field.
    // adobe changed for Type3 chunk:
    //        FMLE always sendout the extended-timestamp,
    //        must send the extended-timestamp to FMS,
    //        must send the extended-timestamp to flash-player.
    // @see: ngx_rtmp_prepare_message
    // @see: http://blog.csdn.net/win_lin/article/details/13363699
    // TODO: FIXME: extract to outer.
    if (timestamp >= RTMP_EXTENDED_TIMESTAMP) {
        pp = (char*)&timestamp;
        *p++ = pp[3];
        *p++ = pp[2];
        *p++ = pp[1];
        *p++ = pp[0];
    }
    
    // always has header
    return p - cache;
}

int srs_do_rtmp_create_msg(char type, u_int32_t timestamp, char* data, int size, int stream_id, SrsSharedPtrMessage** ppmsg)
{
    int ret = ERROR_SUCCESS;
    
    *ppmsg = NULL;
    SrsSharedPtrMessage* msg = NULL;
    
    if (type == SrsCodecFlvTagAudio) {
        SrsMessageHeader header;
        header.initialize_audio(size, timestamp, stream_id);
        
        msg = new SrsSharedPtrMessage();
        if ((ret = msg->create(&header, data, size)) != ERROR_SUCCESS) {
            srs_freep(msg);
            return ret;
        }
    } else if (type == SrsCodecFlvTagVideo) {
        SrsMessageHeader header;
        header.initialize_video(size, timestamp, stream_id);
        
        msg = new SrsSharedPtrMessage();
        if ((ret = msg->create(&header, data, size)) != ERROR_SUCCESS) {
            srs_freep(msg);
            return ret;
        }
    } else if (type == SrsCodecFlvTagScript) {
        SrsMessageHeader header;
        header.initialize_amf0_script(size, stream_id);
        
        msg = new SrsSharedPtrMessage();
        if ((ret = msg->create(&header, data, size)) != ERROR_SUCCESS) {
            srs_freep(msg);
            return ret;
        }
    } else {
        ret = ERROR_STREAM_CASTER_FLV_TAG;
        srs_error("rtmp unknown tag type=%#x. ret=%d", type, ret);
        return ret;
    }

    *ppmsg = msg;

    return ret;
}

int srs_rtmp_create_msg(char type, u_int32_t timestamp, char* data, int size, int stream_id, SrsSharedPtrMessage** ppmsg)
{
    int ret = ERROR_SUCCESS;

    // only when failed, we must free the data.
    if ((ret = srs_do_rtmp_create_msg(type, timestamp, data, size, stream_id, ppmsg)) != ERROR_SUCCESS) {
        srs_freep(data);
        return ret;
    }

    return ret;
}

