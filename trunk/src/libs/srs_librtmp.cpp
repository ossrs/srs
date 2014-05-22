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

#include <string>
#include <sstream>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_protocol_rtmp.hpp>
#include <srs_lib_simple_socket.hpp>
#include <srs_kernel_log.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_protocol_amf0.hpp>

// if user want to define log, define the folowing macro.
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
    std::string port;
    std::string vhost;
    std::string app;
    std::string stream;
    
    SrsRtmpClient* rtmp;
    SimpleSocketStream* skt;
    int stream_id;
    
    Context() {
        rtmp = NULL;
        skt = NULL;
        stream_id = 0;
    }
    virtual ~Context() {
        srs_freep(rtmp);
        srs_freep(skt);
    }
};

int srs_librtmp_context_connect(Context* context) 
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
    // schema
    if ((pos = uri.find("rtmp://")) != string::npos) {
        uri = uri.substr(pos + 7);
    }
    // host/vhost/port
    if ((pos = uri.find(":")) != string::npos) {
        context->vhost = context->host = uri.substr(0, pos);
        uri = uri.substr(pos + 1);
        
        if ((pos = uri.find("/")) != string::npos) {
            context->port = uri.substr(0, pos);
            uri = uri.substr(pos + 1);
        }
    } else {
        if ((pos = uri.find("/")) != string::npos) {
            context->vhost = context->host = uri.substr(0, pos);
            uri = uri.substr(pos + 1);
        }
        context->port = RTMP_DEFAULT_PORT;
    }
    // app
    context->app = uri;
    // query of app
    if ((pos = uri.find("?")) != string::npos) {
        context->app = uri.substr(0, pos);
        string query = uri.substr(pos + 1);
        if ((pos = query.find("vhost=")) != string::npos) {
            context->vhost = query.substr(pos + 6);
            if ((pos = context->vhost.find("&")) != string::npos) {
                context->vhost = context->vhost.substr(pos);
            }
        }
    }
    
    // create socket
    srs_freep(context->skt);
    context->skt = new SimpleSocketStream();
    
    if ((ret = context->skt->create_socket()) != ERROR_SUCCESS) {
        return ret;
    }
    
    // connect to server:port
    string server = srs_dns_resolve(context->host);
    if (server.empty()) {
        return -1;
    }
    if ((ret = context->skt->connect(server.c_str(), ::atoi(context->port.c_str()))) != ERROR_SUCCESS) {
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

void srs_rtmp_destroy(srs_rtmp_t rtmp)
{
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    srs_freep(context);
}

int srs_simple_handshake(srs_rtmp_t rtmp)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    // parse uri, resolve host, connect to server:port
    if ((ret = srs_librtmp_context_connect(context)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // simple handshake
    srs_freep(context->rtmp);
    context->rtmp = new SrsRtmpClient(context->skt);
    
    if ((ret = context->rtmp->simple_handshake()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int srs_complex_handshake(srs_rtmp_t rtmp)
{
#ifndef SRS_AUTO_SSL
    return ERROR_RTMP_HS_SSL_REQUIRE;
#endif

    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    // parse uri, resolve host, connect to server:port
    if ((ret = srs_librtmp_context_connect(context)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // complex handshake
    srs_freep(context->rtmp);
    context->rtmp = new SrsRtmpClient(context->skt);
    
    if ((ret = context->rtmp->complex_handshake()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int srs_connect_app(srs_rtmp_t rtmp)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    string tcUrl = "rtmp://";
    tcUrl += context->vhost;
    tcUrl += ":";
    tcUrl += context->port;
    tcUrl += "/";
    tcUrl += context->app;
    
    if ((ret = context->rtmp->connect_app(context->app, tcUrl)) != ERROR_SUCCESS) {
        return ret;
    }
    
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

const char* srs_type2string(int type)
{
    switch (type) {
        case SRS_RTMP_TYPE_AUDIO: return "Audio";
        case SRS_RTMP_TYPE_VIDEO: return "Video";
        case SRS_RTMP_TYPE_SCRIPT: return "Data";
        default: return "Unknown";
    }
    
    return "Unknown";
}

int srs_read_packet(srs_rtmp_t rtmp, int* type, u_int32_t* timestamp, char** data, int* size)
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

int srs_write_packet(srs_rtmp_t rtmp, int type, u_int32_t timestamp, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(rtmp != NULL);
    Context* context = (Context*)rtmp;
    
    SrsSharedPtrMessage* msg = NULL;
    
    if (type == SRS_RTMP_TYPE_AUDIO) {
        SrsMessageHeader header;
        header.initialize_audio(size, timestamp, context->stream_id);
        
        msg = new SrsSharedPtrMessage();
        if ((ret = msg->initialize(&header, data, size)) != ERROR_SUCCESS) {
            srs_freep(data);
            return ret;
        }
    } else if (type == SRS_RTMP_TYPE_VIDEO) {
        SrsMessageHeader header;
        header.initialize_video(size, timestamp, context->stream_id);
        
        msg = new SrsSharedPtrMessage();
        if ((ret = msg->initialize(&header, data, size)) != ERROR_SUCCESS) {
            srs_freep(data);
            return ret;
        }
    } else if (type == SRS_RTMP_TYPE_SCRIPT) {
        SrsMessageHeader header;
        header.initialize_amf0_script(size, context->stream_id);
        
        msg = new SrsSharedPtrMessage();
        if ((ret = msg->initialize(&header, data, size)) != ERROR_SUCCESS) {
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

int srs_ssl_enabled()
{
#ifndef SRS_AUTO_SSL
    return false;
#endif
    return true;
}

int srs_version_major()
{
    return ::atoi(VERSION_MAJOR);
}

int srs_version_minor()
{
    return ::atoi(VERSION_MINOR);
}

int srs_version_revision()
{
    return ::atoi(VERSION_REVISION);
}

int64_t srs_get_time_ms()
{
    srs_update_system_time_ms();
    return srs_get_system_time_ms();
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
    
    stream.reset();
    if ((ret = any->read(&stream)) != ERROR_SUCCESS) {
        srs_freep(any);
        return amf0;
    }
    
    *nparsed = stream.pos();
    amf0 = (srs_amf0_t)any;
    
    return amf0;
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

amf0_bool srs_amf0_is_string(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_string();
}

amf0_bool srs_amf0_is_boolean(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_boolean();
}

amf0_bool srs_amf0_is_number(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_number();
}

amf0_bool srs_amf0_is_null(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_null();
}

amf0_bool srs_amf0_is_object(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_object();
}

amf0_bool srs_amf0_is_ecma_array(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->is_ecma_array();
}

const char* srs_amf0_to_string(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->to_str_raw();
}

amf0_bool srs_amf0_to_boolean(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->to_boolean();
}

amf0_number srs_amf0_to_number(srs_amf0_t amf0)
{
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    return any->to_number();
}

int srs_amf0_object_property_count(srs_amf0_t amf0)
{
    SrsAmf0Object* obj = (SrsAmf0Object*)amf0;
    return obj->count();
}

const char* srs_amf0_object_property_name_at(srs_amf0_t amf0, int index)
{
    SrsAmf0Object* obj = (SrsAmf0Object*)amf0;
    return obj->key_raw_at(index);
}

srs_amf0_t srs_amf0_object_property_value_at(srs_amf0_t amf0, int index)
{
    SrsAmf0Object* obj = (SrsAmf0Object*)amf0;
    return (srs_amf0_t)obj->value_at(index);
}

int srs_amf0_array_property_count(srs_amf0_t amf0)
{
    SrsAmf0EcmaArray * obj = (SrsAmf0EcmaArray*)amf0;
    return obj->count();
}

const char* srs_amf0_array_property_name_at(srs_amf0_t amf0, int index)
{
    SrsAmf0EcmaArray* obj = (SrsAmf0EcmaArray*)amf0;
    return obj->key_raw_at(index);
}

srs_amf0_t srs_amf0_array_property_value_at(srs_amf0_t amf0, int index)
{
    SrsAmf0EcmaArray* obj = (SrsAmf0EcmaArray*)amf0;
    return (srs_amf0_t)obj->value_at(index);
}

void __srs_amf0_do_print(SrsAmf0Any* any, stringstream& ss, int& level)
{
    if (true) {
        for (int i = 0; i < level; i++) {
            ss << "    ";
        }
    }
    
    if (any->is_boolean()) {
        ss << "Boolean " << (any->to_boolean()? "true":"false") << endl;
    } else if (any->is_number()) {
        ss << "Number " << std::fixed << any->to_number() << endl;
    } else if (any->is_string()) {
        ss << "String " << any->to_str() << endl;
    } else if (any->is_null()) {
        ss << "Null" << endl;
    } else if (any->is_ecma_array()) {
        SrsAmf0EcmaArray* obj = any->to_ecma_array();
        ss << "EcmaArray " << "(" << obj->count() << " items)" << endl;
        for (int i = 0; i < obj->count(); i++) {
            ss << "    Property '" << obj->key_at(i) << "' ";
            if (obj->value_at(i)->is_object() || obj->value_at(i)->is_ecma_array()) {
                int next_level = level + 1;
                __srs_amf0_do_print(obj->value_at(i), ss, next_level);
            } else {
                int next_level = 0;
                __srs_amf0_do_print(obj->value_at(i), ss, next_level);
            }
        }
    } else if (any->is_object()) {
        SrsAmf0Object* obj = any->to_object();
        ss << "Object " << "(" << obj->count() << " items)" << endl;
        for (int i = 0; i < obj->count(); i++) {
            ss << "    Property '" << obj->key_at(i) << "' ";
            if (obj->value_at(i)->is_object() || obj->value_at(i)->is_ecma_array()) {
                int next_level = level + 1;
                __srs_amf0_do_print(obj->value_at(i), ss, next_level);
            } else {
                int next_level = 0;
                __srs_amf0_do_print(obj->value_at(i), ss, next_level);
            }
        }
    } else {
        ss << "Unknown" << endl;
    }
}

char* srs_amf0_human_print(srs_amf0_t amf0, char** pdata, int* psize)
{
    stringstream ss;
    
    ss.precision(1);
    
    SrsAmf0Any* any = (SrsAmf0Any*)amf0;
    
    int level = 0;
    __srs_amf0_do_print(any, ss, level);
    
    string str = ss.str();
    if (str.empty()) {
        return NULL;
    }
    
    *pdata = new char[str.length()];
    *psize = str.length();
    memcpy(*pdata, str.data(), str.length());
    
    return *pdata;
}

#ifdef __cplusplus
}
#endif
