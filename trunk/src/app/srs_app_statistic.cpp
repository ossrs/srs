/*
The MIT License (MIT)

Copyright (c) 2013-2017 SRS(ossrs)

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

#include <srs_app_statistic.hpp>

#include <unistd.h>
#include <sstream>
using namespace std;

#include <srs_rtmp_stack.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_kbps.hpp>
#include <srs_app_conn.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>

int64_t srs_gvid = getpid() * 3;

int64_t srs_generate_id()
{
    return srs_gvid++;
}

SrsStatisticVhost::SrsStatisticVhost()
{
    id = srs_generate_id();
    
    kbps = new SrsKbps();
    kbps->set_io(NULL, NULL);
    
    nb_clients = 0;
    nb_streams = 0;
}

SrsStatisticVhost::~SrsStatisticVhost()
{
    srs_freep(kbps);
}

int SrsStatisticVhost::dumps(SrsJsonObject* obj)
{
    int ret = ERROR_SUCCESS;
    
    // dumps the config of vhost.
    bool hls_enabled = _srs_config->get_hls_enabled(vhost);
    bool enabled = _srs_config->get_vhost_enabled(vhost);
    
    obj->set("id", SrsJsonAny::integer(id));
    obj->set("name", SrsJsonAny::str(vhost.c_str()));
    obj->set("enabled", SrsJsonAny::boolean(enabled));
    obj->set("clients", SrsJsonAny::integer(nb_clients));
    obj->set("streams", SrsJsonAny::integer(nb_streams));
    obj->set("send_bytes", SrsJsonAny::integer(kbps->get_send_bytes()));
    obj->set("recv_bytes", SrsJsonAny::integer(kbps->get_recv_bytes()));
    
    SrsJsonObject* okbps = SrsJsonAny::object();
    obj->set("kbps", okbps);
    
    okbps->set("recv_30s", SrsJsonAny::integer(kbps->get_recv_kbps_30s()));
    okbps->set("send_30s", SrsJsonAny::integer(kbps->get_send_kbps_30s()));
    
    SrsJsonObject* hls = SrsJsonAny::object();
    obj->set("hls", hls);
    
    hls->set("enabled", SrsJsonAny::boolean(hls_enabled));
    if (hls_enabled) {
        hls->set("fragment", SrsJsonAny::number(_srs_config->get_hls_fragment(vhost)));
    }
    
    return ret;
}

SrsStatisticStream::SrsStatisticStream()
{
    id = srs_generate_id();
    vhost = NULL;
    active = false;
    connection_cid = -1;
    
    has_video = false;
    vcodec = SrsCodecVideoReserved;
    avc_profile = SrsAvcProfileReserved;
    avc_level = SrsAvcLevelReserved;
    
    has_audio = false;
    acodec = SrsCodecAudioReserved1;
    asample_rate = SrsCodecAudioSampleRateReserved;
    asound_type = SrsCodecAudioSoundTypeReserved;
    aac_object = SrsAacObjectTypeReserved;
    width = 0;
    height = 0;
    
    kbps = new SrsKbps();
    kbps->set_io(NULL, NULL);
    
    nb_clients = 0;
}

SrsStatisticStream::~SrsStatisticStream()
{
    srs_freep(kbps);
}

int SrsStatisticStream::dumps(SrsJsonObject* obj)
{
    int ret = ERROR_SUCCESS;
    
    obj->set("id", SrsJsonAny::integer(id));
    obj->set("name", SrsJsonAny::str(stream.c_str()));
    obj->set("vhost", SrsJsonAny::integer(vhost->id));
    obj->set("app", SrsJsonAny::str(app.c_str()));
    obj->set("live_ms", SrsJsonAny::integer(srs_get_system_time_ms()));
    obj->set("clients", SrsJsonAny::integer(nb_clients));
    obj->set("send_bytes", SrsJsonAny::integer(kbps->get_send_bytes()));
    obj->set("recv_bytes", SrsJsonAny::integer(kbps->get_recv_bytes()));
    
    SrsJsonObject* okbps = SrsJsonAny::object();
    obj->set("kbps", okbps);
    
    okbps->set("recv_30s", SrsJsonAny::integer(kbps->get_recv_kbps_30s()));
    okbps->set("send_30s", SrsJsonAny::integer(kbps->get_send_kbps_30s()));
    
    SrsJsonObject* publish = SrsJsonAny::object();
    obj->set("publish", publish);
    
    publish->set("active", SrsJsonAny::boolean(active));
    publish->set("cid", SrsJsonAny::integer(connection_cid));
    
    if (!has_video) {
        obj->set("video", SrsJsonAny::null());
    } else {
        SrsJsonObject* video = SrsJsonAny::object();
        obj->set("video", video);
        
        video->set("codec", SrsJsonAny::str(srs_codec_video2str(vcodec).c_str()));
        video->set("profile", SrsJsonAny::str(srs_codec_avc_profile2str(avc_profile).c_str()));
        video->set("level", SrsJsonAny::str(srs_codec_avc_level2str(avc_level).c_str()));
        video->set("width", SrsJsonAny::integer(width));
        video->set("height", SrsJsonAny::integer(height));
    }
    
    if (!has_audio) {
        obj->set("audio", SrsJsonAny::null());
    } else {
        SrsJsonObject* audio = SrsJsonAny::object();
        obj->set("audio", audio);
        
        audio->set("codec", SrsJsonAny::str(srs_codec_audio2str(acodec).c_str()));
        audio->set("sample_rate", SrsJsonAny::integer(flv_sample_rates[asample_rate]));
        audio->set("channel", SrsJsonAny::integer(asound_type + 1));
        audio->set("profile", SrsJsonAny::str(srs_codec_aac_object2str(aac_object).c_str()));
    }
    
    return ret;
}

void SrsStatisticStream::publish(int cid)
{
    connection_cid = cid;
    active = true;
    
    vhost->nb_streams++;
}

void SrsStatisticStream::close()
{
    has_video = false;
    has_audio = false;
    active = false;
    
    vhost->nb_streams--;
}

SrsStatisticClient::SrsStatisticClient()
{
    id = 0;
    stream = NULL;
    conn = NULL;
    req = NULL;
    type = SrsRtmpConnUnknown;
    create = srs_get_system_time_ms();
}

SrsStatisticClient::~SrsStatisticClient()
{
}

int SrsStatisticClient::dumps(SrsJsonObject* obj)
{
    int ret = ERROR_SUCCESS;
    
    obj->set("id", SrsJsonAny::integer(id));
    obj->set("vhost", SrsJsonAny::integer(stream->vhost->id));
    obj->set("stream", SrsJsonAny::integer(stream->id));
    obj->set("ip", SrsJsonAny::str(req->ip.c_str()));
    obj->set("pageUrl", SrsJsonAny::str(req->pageUrl.c_str()));
    obj->set("swfUrl", SrsJsonAny::str(req->swfUrl.c_str()));
    obj->set("tcUrl", SrsJsonAny::str(req->tcUrl.c_str()));
    obj->set("url", SrsJsonAny::str(req->get_stream_url().c_str()));
    obj->set("type", SrsJsonAny::str(srs_client_type_string(type).c_str()));
    obj->set("publish", SrsJsonAny::boolean(srs_client_type_is_publish(type)));
    obj->set("alive", SrsJsonAny::number((srs_get_system_time_ms() - create) / 1000.0));
    
    return ret;
}

SrsStatistic* SrsStatistic::_instance = new SrsStatistic();

SrsStatistic::SrsStatistic()
{
    _server_id = srs_generate_id();
    
    kbps = new SrsKbps();
    kbps->set_io(NULL, NULL);
}

SrsStatistic::~SrsStatistic()
{
    srs_freep(kbps);
    
    if (true) {
        std::map<int64_t, SrsStatisticVhost*>::iterator it;
        for (it = vhosts.begin(); it != vhosts.end(); it++) {
            SrsStatisticVhost* vhost = it->second;
            srs_freep(vhost);
        }
    }
    if (true) {
        std::map<int64_t, SrsStatisticStream*>::iterator it;
        for (it = streams.begin(); it != streams.end(); it++) {
            SrsStatisticStream* stream = it->second;
            srs_freep(stream);
        }
    }
    if (true) {
        std::map<int, SrsStatisticClient*>::iterator it;
        for (it = clients.begin(); it != clients.end(); it++) {
            SrsStatisticClient* client = it->second;
            srs_freep(client);
        }
    }
    
    vhosts.clear();
    rvhosts.clear();
    streams.clear();
    rstreams.clear();
}

SrsStatistic* SrsStatistic::instance()
{
    return _instance;
}

SrsStatisticVhost* SrsStatistic::find_vhost(int vid)
{
    std::map<int64_t, SrsStatisticVhost*>::iterator it;
    if ((it = vhosts.find(vid)) != vhosts.end()) {
        return it->second;
    }
    return NULL;
}

SrsStatisticVhost* SrsStatistic::find_vhost(string name)
{
    std::map<string, SrsStatisticVhost*>::iterator it;
    if ((it = rvhosts.find(name)) != rvhosts.end()) {
        return it->second;
    }
    return NULL;
}

SrsStatisticStream* SrsStatistic::find_stream(int sid)
{
    std::map<int64_t, SrsStatisticStream*>::iterator it;
    if ((it = streams.find(sid)) != streams.end()) {
        return it->second;
    }
    return NULL;
}

SrsStatisticClient* SrsStatistic::find_client(int cid)
{
    std::map<int, SrsStatisticClient*>::iterator it;
    if ((it = clients.find(cid)) != clients.end()) {
        return it->second;
    }
    return NULL;
}

int SrsStatistic::on_video_info(SrsRequest* req, 
    SrsCodecVideo vcodec, SrsAvcProfile avc_profile, SrsAvcLevel avc_level,
    int width, int height
) {
    int ret = ERROR_SUCCESS;
    
    SrsStatisticVhost* vhost = create_vhost(req);
    SrsStatisticStream* stream = create_stream(vhost, req);

    stream->has_video = true;
    stream->vcodec = vcodec;
    stream->avc_profile = avc_profile;
    stream->avc_level = avc_level;
    
    stream->width = width;
    stream->height = height;
    
    return ret;
}

int SrsStatistic::on_audio_info(SrsRequest* req,
    SrsCodecAudio acodec, SrsCodecAudioSampleRate asample_rate, SrsCodecAudioSoundType asound_type,
    SrsAacObjectType aac_object
) {
    int ret = ERROR_SUCCESS;
    
    SrsStatisticVhost* vhost = create_vhost(req);
    SrsStatisticStream* stream = create_stream(vhost, req);

    stream->has_audio = true;
    stream->acodec = acodec;
    stream->asample_rate = asample_rate;
    stream->asound_type = asound_type;
    stream->aac_object = aac_object;
    
    return ret;
}

void SrsStatistic::on_stream_publish(SrsRequest* req, int cid)
{
    SrsStatisticVhost* vhost = create_vhost(req);
    SrsStatisticStream* stream = create_stream(vhost, req);

    stream->publish(cid);
}

void SrsStatistic::on_stream_close(SrsRequest* req)
{
    SrsStatisticVhost* vhost = create_vhost(req);
    SrsStatisticStream* stream = create_stream(vhost, req);

    stream->close();
}

int SrsStatistic::on_client(int id, SrsRequest* req, SrsConnection* conn, SrsRtmpConnType type)
{
    int ret = ERROR_SUCCESS;
    
    SrsStatisticVhost* vhost = create_vhost(req);
    SrsStatisticStream* stream = create_stream(vhost, req);

    // create client if not exists
    SrsStatisticClient* client = NULL;
    if (clients.find(id) == clients.end()) {
        client = new SrsStatisticClient();
        client->id = id;
        client->stream = stream;
        clients[id] = client;
    } else {
        client = clients[id];
    }
    
    // got client.
    client->conn = conn;
    client->req = req;
    client->type = type;
    stream->nb_clients++;
    vhost->nb_clients++;

    return ret;
}

void SrsStatistic::on_disconnect(int id)
{
    std::map<int, SrsStatisticClient*>::iterator it;
    if ((it = clients.find(id)) == clients.end()) {
        return;
    }

    SrsStatisticClient* client = it->second;
    SrsStatisticStream* stream = client->stream;
    SrsStatisticVhost* vhost = stream->vhost;
    
    srs_freep(client);
    clients.erase(it);
    
    stream->nb_clients--;
    vhost->nb_clients--;
}

void SrsStatistic::kbps_add_delta(SrsConnection* conn)
{
    int id = conn->srs_id();
    if (clients.find(id) == clients.end()) {
        return;
    }
    
    SrsStatisticClient* client = clients[id];
    
    // resample the kbps to collect the delta.
    conn->resample();
    
    // add delta of connection to kbps.
    // for next sample() of server kbps can get the stat.
    kbps->add_delta(conn);
    client->stream->kbps->add_delta(conn);
    client->stream->vhost->kbps->add_delta(conn);
    
    // cleanup the delta.
    conn->cleanup();
}

SrsKbps* SrsStatistic::kbps_sample()
{
    kbps->sample();
    if (true) {
        std::map<int64_t, SrsStatisticVhost*>::iterator it;
        for (it = vhosts.begin(); it != vhosts.end(); it++) {
            SrsStatisticVhost* vhost = it->second;
            vhost->kbps->sample();
        }
    }
    if (true) {
        std::map<int64_t, SrsStatisticStream*>::iterator it;
        for (it = streams.begin(); it != streams.end(); it++) {
            SrsStatisticStream* stream = it->second;
            stream->kbps->sample();
        }
    }
    
    return kbps;
}

int64_t SrsStatistic::server_id()
{
    return _server_id;
}

int SrsStatistic::dumps_vhosts(SrsJsonArray* arr)
{
    int ret = ERROR_SUCCESS;

    std::map<int64_t, SrsStatisticVhost*>::iterator it;
    for (it = vhosts.begin(); it != vhosts.end(); it++) {
        SrsStatisticVhost* vhost = it->second;
        
        SrsJsonObject* obj = SrsJsonAny::object();
        arr->append(obj);
        
        if ((ret = vhost->dumps(obj)) != ERROR_SUCCESS) {
            return ret;
        }
    }

    return ret;
}

int SrsStatistic::dumps_streams(SrsJsonArray* arr)
{
    int ret = ERROR_SUCCESS;
    
    std::map<int64_t, SrsStatisticStream*>::iterator it;
    for (it = streams.begin(); it != streams.end(); it++) {
        SrsStatisticStream* stream = it->second;
        
        SrsJsonObject* obj = SrsJsonAny::object();
        arr->append(obj);

        if ((ret = stream->dumps(obj)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
}

int SrsStatistic::dumps_clients(SrsJsonArray* arr, int start, int count)
{
    int ret = ERROR_SUCCESS;
    
    std::map<int, SrsStatisticClient*>::iterator it = clients.begin();
    for (int i = 0; i < start + count && it != clients.end(); it++, i++) {
        if (i < start) {
            continue;
        }
        
        SrsStatisticClient* client = it->second;
        
        SrsJsonObject* obj = SrsJsonAny::object();
        arr->append(obj);
        
        if ((ret = client->dumps(obj)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    return ret;
}

SrsStatisticVhost* SrsStatistic::create_vhost(SrsRequest* req)
{
    SrsStatisticVhost* vhost = NULL;
    
    // create vhost if not exists.
    if (rvhosts.find(req->vhost) == rvhosts.end()) {
        vhost = new SrsStatisticVhost();
        vhost->vhost = req->vhost;
        rvhosts[req->vhost] = vhost;
        vhosts[vhost->id] = vhost;
        return vhost;
    }

    vhost = rvhosts[req->vhost];
    
    return vhost;
}

SrsStatisticStream* SrsStatistic::create_stream(SrsStatisticVhost* vhost, SrsRequest* req)
{
    std::string url = req->get_stream_url();
    
    SrsStatisticStream* stream = NULL;
    
    // create stream if not exists.
    if (rstreams.find(url) == rstreams.end()) {
        stream = new SrsStatisticStream();
        stream->vhost = vhost;
        stream->stream = req->stream;
        stream->app = req->app;
        stream->url = url;
        rstreams[url] = stream;
        streams[stream->id] = stream;
        return stream;
    }
    
    stream = rstreams[url];
    
    return stream;
}

