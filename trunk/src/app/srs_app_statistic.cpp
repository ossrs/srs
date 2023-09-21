//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_statistic.hpp>

#include <unistd.h>
#include <sstream>
using namespace std;

#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_kbps.hpp>
#include <srs_app_conn.hpp>
#include <srs_app_config.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_app_tencentcloud.hpp>
#include <srs_kernel_kbps.hpp>
#include <srs_app_utility.hpp>

string srs_generate_stat_vid()
{
    return "vid-" + srs_random_str(7);
}

SrsStatisticVhost::SrsStatisticVhost()
{
    id = srs_generate_stat_vid();
    
    kbps = new SrsKbps();

    nb_clients = 0;
    nb_streams = 0;
}

SrsStatisticVhost::~SrsStatisticVhost()
{
    srs_freep(kbps);
}

srs_error_t SrsStatisticVhost::dumps(SrsJsonObject* obj)
{
    srs_error_t err = srs_success;
    
    // dumps the config of vhost.
    bool hls_enabled = _srs_config->get_hls_enabled(vhost);
    bool enabled = _srs_config->get_vhost_enabled(vhost);
    
    obj->set("id", SrsJsonAny::str(id.c_str()));
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
        hls->set("fragment", SrsJsonAny::number(srsu2msi(_srs_config->get_hls_fragment(vhost))/1000.0));
    }
    
    return err;
}

SrsStatisticStream::SrsStatisticStream()
{
    id = srs_generate_stat_vid();
    vhost = NULL;
    active = false;

    has_video = false;
    vcodec = SrsVideoCodecIdReserved;
    avc_profile = SrsAvcProfileReserved;
    avc_level = SrsAvcLevelReserved;
    
    has_audio = false;
    acodec = SrsAudioCodecIdReserved1;
    asample_rate = SrsAudioSampleRateReserved;
    asound_type = SrsAudioChannelsReserved;
    aac_object = SrsAacObjectTypeReserved;
    width = 0;
    height = 0;
    
    kbps = new SrsKbps();

    nb_clients = 0;
    frames = new SrsPps();
}

SrsStatisticStream::~SrsStatisticStream()
{
    srs_freep(kbps);
    srs_freep(frames);
}

srs_error_t SrsStatisticStream::dumps(SrsJsonObject* obj)
{
    srs_error_t err = srs_success;
    
    obj->set("id", SrsJsonAny::str(id.c_str()));
    obj->set("name", SrsJsonAny::str(stream.c_str()));
    obj->set("vhost", SrsJsonAny::str(vhost->id.c_str()));
    obj->set("app", SrsJsonAny::str(app.c_str()));
    obj->set("tcUrl", SrsJsonAny::str(tcUrl.c_str()));
    obj->set("url", SrsJsonAny::str(url.c_str()));
    obj->set("live_ms", SrsJsonAny::integer(srsu2ms(srs_get_system_time())));
    obj->set("clients", SrsJsonAny::integer(nb_clients));
    obj->set("frames", SrsJsonAny::integer(frames->sugar));
    obj->set("send_bytes", SrsJsonAny::integer(kbps->get_send_bytes()));
    obj->set("recv_bytes", SrsJsonAny::integer(kbps->get_recv_bytes()));
    
    SrsJsonObject* okbps = SrsJsonAny::object();
    obj->set("kbps", okbps);
    
    okbps->set("recv_30s", SrsJsonAny::integer(kbps->get_recv_kbps_30s()));
    okbps->set("send_30s", SrsJsonAny::integer(kbps->get_send_kbps_30s()));
    
    SrsJsonObject* publish = SrsJsonAny::object();
    obj->set("publish", publish);
    
    publish->set("active", SrsJsonAny::boolean(active));
    if (!publisher_id.empty()) {
        publish->set("cid", SrsJsonAny::str(publisher_id.c_str()));
    }
    
    if (!has_video) {
        obj->set("video", SrsJsonAny::null());
    } else {
        SrsJsonObject* video = SrsJsonAny::object();
        obj->set("video", video);
        
        video->set("codec", SrsJsonAny::str(srs_video_codec_id2str(vcodec).c_str()));

        if (vcodec == SrsVideoCodecIdAVC) {
            video->set("profile", SrsJsonAny::str(srs_avc_profile2str(avc_profile).c_str()));
            video->set("level", SrsJsonAny::str(srs_avc_level2str(avc_level).c_str()));
#ifdef SRS_H265
        } else if (vcodec == SrsVideoCodecIdHEVC) {
            video->set("profile", SrsJsonAny::str(srs_hevc_profile2str(hevc_profile).c_str()));
            video->set("level", SrsJsonAny::str(srs_hevc_level2str(hevc_level).c_str()));
#endif
        } else {
            video->set("profile", SrsJsonAny::str("Other"));
            video->set("level", SrsJsonAny::str("Other"));
        }

        video->set("width", SrsJsonAny::integer(width));
        video->set("height", SrsJsonAny::integer(height));
    }
    
    if (!has_audio) {
        obj->set("audio", SrsJsonAny::null());
    } else {
        SrsJsonObject* audio = SrsJsonAny::object();
        obj->set("audio", audio);
        
        audio->set("codec", SrsJsonAny::str(srs_audio_codec_id2str(acodec).c_str()));
        audio->set("sample_rate", SrsJsonAny::integer(srs_flv_srates[asample_rate]));
        audio->set("channel", SrsJsonAny::integer(asound_type + 1));
        audio->set("profile", SrsJsonAny::str(srs_aac_object2str(aac_object).c_str()));
    }
    
    return err;
}

void SrsStatisticStream::publish(std::string id)
{
    // To prevent duplicated publish event by bridge.
    if (active) {
        return;
    }

    publisher_id = id;
    active = true;
    
    vhost->nb_streams++;
}

void SrsStatisticStream::close()
{
    // To prevent duplicated close event.
    if (!active) {
        return;
    }

    has_video = false;
    has_audio = false;
    active = false;
    
    vhost->nb_streams--;
}

SrsStatisticClient::SrsStatisticClient()
{
    stream = NULL;
    conn = NULL;
    req = NULL;
    type = SrsRtmpConnUnknown;
    create = srs_get_system_time();

    kbps = new SrsKbps();
}

SrsStatisticClient::~SrsStatisticClient()
{
    srs_freep(kbps);
	srs_freep(req);
}

srs_error_t SrsStatisticClient::dumps(SrsJsonObject* obj)
{
    srs_error_t err = srs_success;
    
    obj->set("id", SrsJsonAny::str(id.c_str()));
    obj->set("vhost", SrsJsonAny::str(stream->vhost->id.c_str()));
    obj->set("stream", SrsJsonAny::str(stream->id.c_str()));
    obj->set("ip", SrsJsonAny::str(req->ip.c_str()));
    obj->set("pageUrl", SrsJsonAny::str(req->pageUrl.c_str()));
    obj->set("swfUrl", SrsJsonAny::str(req->swfUrl.c_str()));
    obj->set("tcUrl", SrsJsonAny::str(req->tcUrl.c_str()));
    obj->set("url", SrsJsonAny::str(req->get_stream_url().c_str()));
    obj->set("name", SrsJsonAny::str(req->stream.c_str()));
    obj->set("type", SrsJsonAny::str(srs_client_type_string(type).c_str()));
    obj->set("publish", SrsJsonAny::boolean(srs_client_type_is_publish(type)));
    obj->set("alive", SrsJsonAny::number(srsu2ms(srs_get_system_time() - create) / 1000.0));
    obj->set("send_bytes", SrsJsonAny::integer(kbps->get_send_bytes()));
    obj->set("recv_bytes", SrsJsonAny::integer(kbps->get_recv_bytes()));

    SrsJsonObject* okbps = SrsJsonAny::object();
    obj->set("kbps", okbps);

    okbps->set("recv_30s", SrsJsonAny::integer(kbps->get_recv_kbps_30s()));
    okbps->set("send_30s", SrsJsonAny::integer(kbps->get_send_kbps_30s()));
    
    return err;
}

SrsStatistic* SrsStatistic::_instance = NULL;

SrsStatistic::SrsStatistic()
{
    kbps = new SrsKbps();

    nb_clients_ = 0;
    nb_errs_ = 0;
}

SrsStatistic::~SrsStatistic()
{
    srs_freep(kbps);

    if (true) {
        std::map<std::string, SrsStatisticVhost*>::iterator it;
        for (it = vhosts.begin(); it != vhosts.end(); it++) {
            SrsStatisticVhost* vhost = it->second;
            srs_freep(vhost);
        }
    }
    if (true) {
        std::map<std::string, SrsStatisticStream*>::iterator it;
        for (it = streams.begin(); it != streams.end(); it++) {
            SrsStatisticStream* stream = it->second;
            srs_freep(stream);
        }
    }
    if (true) {
        std::map<std::string, SrsStatisticClient*>::iterator it;
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
    if (_instance == NULL) {
        _instance = new SrsStatistic();
    }
    return _instance;
}

SrsStatisticVhost* SrsStatistic::find_vhost_by_id(std::string vid)
{
    std::map<string, SrsStatisticVhost*>::iterator it;
    if ((it = vhosts.find(vid)) != vhosts.end()) {
        return it->second;
    }
    return NULL;
}

SrsStatisticVhost* SrsStatistic::find_vhost_by_name(string name)
{
    if (rvhosts.empty()) {
        return NULL;
    }

    std::map<string, SrsStatisticVhost*>::iterator it;
    if ((it = rvhosts.find(name)) != rvhosts.end()) {
        return it->second;
    }
    return NULL;
}

SrsStatisticStream* SrsStatistic::find_stream(string sid)
{
    std::map<std::string, SrsStatisticStream*>::iterator it;
    if ((it = streams.find(sid)) != streams.end()) {
        return it->second;
    }
    return NULL;
}

SrsStatisticStream* SrsStatistic::find_stream_by_url(string url)
{
    std::map<std::string, SrsStatisticStream*>::iterator it;
    if ((it = rstreams.find(url)) != rstreams.end()) {
        return it->second;
    }
    return NULL;
}

SrsStatisticClient* SrsStatistic::find_client(string client_id)
{
    std::map<std::string, SrsStatisticClient*>::iterator it;
    if ((it = clients.find(client_id)) != clients.end()) {
        return it->second;
    }
    return NULL;
}

srs_error_t SrsStatistic::on_video_info(SrsRequest* req, SrsVideoCodecId vcodec, int profile, int level, int width, int height)
{
    srs_error_t err = srs_success;
    
    SrsStatisticVhost* vhost = create_vhost(req);
    SrsStatisticStream* stream = create_stream(vhost, req);
    
    stream->has_video = true;
    stream->vcodec = vcodec;

    if (vcodec == SrsVideoCodecIdAVC) {
        stream->avc_profile = (SrsAvcProfile)profile;
        stream->avc_level = (SrsAvcLevel)level;
#ifdef SRS_H265
    } else if (vcodec == SrsVideoCodecIdHEVC) {
        stream->hevc_profile = (SrsHevcProfile)profile;
        stream->hevc_level = (SrsHevcLevel)level;
#endif
    } else {
        stream->avc_profile = (SrsAvcProfile)profile;
        stream->avc_level = (SrsAvcLevel)level;
    }

    stream->width = width;
    stream->height = height;
    
    return err;
}

srs_error_t SrsStatistic::on_audio_info(SrsRequest* req, SrsAudioCodecId acodec, SrsAudioSampleRate asample_rate, SrsAudioChannels asound_type, SrsAacObjectType aac_object)
{
    srs_error_t err = srs_success;
    
    SrsStatisticVhost* vhost = create_vhost(req);
    SrsStatisticStream* stream = create_stream(vhost, req);
    
    stream->has_audio = true;
    stream->acodec = acodec;
    stream->asample_rate = asample_rate;
    stream->asound_type = asound_type;
    stream->aac_object = aac_object;
    
    return err;
}

srs_error_t SrsStatistic::on_video_frames(SrsRequest* req, int nb_frames)
{
    srs_error_t err = srs_success;
    
    SrsStatisticVhost* vhost = create_vhost(req);
    SrsStatisticStream* stream = create_stream(vhost, req);
    
    stream->frames->sugar += nb_frames;
    
    return err;
}

void SrsStatistic::on_stream_publish(SrsRequest* req, std::string publisher_id)
{
    SrsStatisticVhost* vhost = create_vhost(req);
    SrsStatisticStream* stream = create_stream(vhost, req);
    
    stream->publish(publisher_id);
}

void SrsStatistic::on_stream_close(SrsRequest* req)
{
    SrsStatisticVhost* vhost = create_vhost(req);
    SrsStatisticStream* stream = create_stream(vhost, req);
    stream->close();
}

srs_error_t SrsStatistic::on_client(std::string id, SrsRequest* req, ISrsExpire* conn, SrsRtmpConnType type)
{
    srs_error_t err = srs_success;

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
    client->type = type;
    stream->nb_clients++;
    vhost->nb_clients++;

    // The req might be freed, in such as SrsLiveStream::update, so we must copy it.
    // @see https://github.com/ossrs/srs/issues/2311
    srs_freep(client->req);
    client->req = req->copy();

    nb_clients_++;
    
    return err;
}

void SrsStatistic::on_disconnect(std::string id, srs_error_t err)
{
    std::map<std::string, SrsStatisticClient*>::iterator it = clients.find(id);
    if (it == clients.end()) return;

    SrsStatisticClient* client = it->second;
    SrsStatisticStream* stream = client->stream;
    SrsStatisticVhost* vhost = stream->vhost;
    
    srs_freep(client);
    clients.erase(it);
    
    stream->nb_clients--;
    vhost->nb_clients--;

    if (srs_error_code(err) != ERROR_SUCCESS) {
        nb_errs_++;
    }

    cleanup_stream(stream);
}

void SrsStatistic::cleanup_stream(SrsStatisticStream* stream)
{
    // If stream has publisher(not active) or player(clients), never cleanup it.
    if (stream->active || stream->nb_clients > 0) {
        return;
    }

    // There should not be any clients referring to the stream.
    for (std::map<std::string, SrsStatisticClient*>::iterator it = clients.begin(); it != clients.end(); ++it) {
        SrsStatisticClient* client = it->second;
        srs_assert(client->stream != stream);
    }

    // Do cleanup streams.
    if (true) {
        std::map<std::string, SrsStatisticStream*>::iterator it;
        if ((it = streams.find(stream->id)) != streams.end()) {
            streams.erase(it);
        }
    }

    if (true) {
        std::map<std::string, SrsStatisticStream*>::iterator it;
        if ((it = rstreams.find(stream->url)) != rstreams.end()) {
            rstreams.erase(it);
        }
    }

    // It's safe to delete the stream now.
    srs_freep(stream);
}

void SrsStatistic::kbps_add_delta(std::string id, ISrsKbpsDelta* delta)
{
    if (!delta) return;

    map<string, SrsStatisticClient*>::iterator it = clients.find(id);
    if (it == clients.end()) return;

    SrsStatisticClient* client = it->second;
    
    // resample the kbps to collect the delta.
    int64_t in, out;
    delta->remark(&in, &out);
    
    // add delta of connection to kbps.
    // for next sample() of server kbps can get the stat.
    kbps->add_delta(in, out);
    client->kbps->add_delta(in, out);
    client->stream->kbps->add_delta(in, out);
    client->stream->vhost->kbps->add_delta(in, out);
}

void SrsStatistic::kbps_sample()
{
    kbps->sample();
    if (true) {
        std::map<std::string, SrsStatisticVhost*>::iterator it;
        for (it = vhosts.begin(); it != vhosts.end(); it++) {
            SrsStatisticVhost* vhost = it->second;
            vhost->kbps->sample();
        }
    }
    if (true) {
        std::map<std::string, SrsStatisticStream*>::iterator it;
        for (it = streams.begin(); it != streams.end(); it++) {
            SrsStatisticStream* stream = it->second;
            stream->kbps->sample();
            stream->frames->update();
        }
    }
    if (true) {
        std::map<std::string, SrsStatisticClient*>::iterator it;
        for (it = clients.begin(); it != clients.end(); it++) {
            SrsStatisticClient* client = it->second;
            client->kbps->sample();
        }
    }

    // Update server level data.
    srs_update_rtmp_server((int)clients.size(), kbps);
}

std::string SrsStatistic::server_id()
{
    if (server_id_.empty()) {
        server_id_ = _srs_config->get_server_id();
    }
    return server_id_;
}

std::string SrsStatistic::service_id()
{
    if (service_id_.empty()) {
        service_id_ = srs_random_str(8);
    }

    return service_id_;
}

std::string SrsStatistic::service_pid()
{
    if (service_pid_.empty()) {
        service_pid_ = srs_int2str(getpid());
    }

    return service_pid_;
}

srs_error_t SrsStatistic::dumps_vhosts(SrsJsonArray* arr)
{
    srs_error_t err = srs_success;
    
    std::map<std::string, SrsStatisticVhost*>::iterator it;
    for (it = vhosts.begin(); it != vhosts.end(); it++) {
        SrsStatisticVhost* vhost = it->second;
        
        SrsJsonObject* obj = SrsJsonAny::object();
        arr->append(obj);
        
        if ((err = vhost->dumps(obj)) != srs_success) {
            return srs_error_wrap(err, "dump vhost");
        }
    }
    
    return err;
}

srs_error_t SrsStatistic::dumps_streams(SrsJsonArray* arr, int start, int count)
{
    srs_error_t err = srs_success;

    std::map<std::string, SrsStatisticStream*>::iterator it = streams.begin();
    for (int i = 0; i < start + count && it != streams.end(); it++, i++) {
        if (i < start) {
            continue;
        }

        SrsStatisticStream* stream = it->second;
        
        SrsJsonObject* obj = SrsJsonAny::object();
        arr->append(obj);
        
        if ((err = stream->dumps(obj)) != srs_success) {
            return srs_error_wrap(err, "dump stream");
        }
    }
    
    return err;
}

srs_error_t SrsStatistic::dumps_clients(SrsJsonArray* arr, int start, int count)
{
    srs_error_t err = srs_success;
    
    std::map<std::string, SrsStatisticClient*>::iterator it = clients.begin();
    for (int i = 0; i < start + count && it != clients.end(); it++, i++) {
        if (i < start) {
            continue;
        }
        
        SrsStatisticClient* client = it->second;
        
        SrsJsonObject* obj = SrsJsonAny::object();
        arr->append(obj);
        
        if ((err = client->dumps(obj)) != srs_success) {
            return srs_error_wrap(err, "dump client");
        }
    }
    
    return err;
}

void SrsStatistic::dumps_hints_kv(std::stringstream & ss)
{
    if (!streams.empty()) {
        ss << "&streams=" << streams.size();
    }
    if (!clients.empty()) {
        ss << "&clients=" << clients.size();
    }
    if (kbps->get_recv_kbps_30s()) {
        ss << "&recv=" << kbps->get_recv_kbps_30s();
    }
    if (kbps->get_send_kbps_30s()) {
        ss << "&send=" << kbps->get_send_kbps_30s();
    }

#ifdef SRS_H265
    // For HEVC, we should check active stream which is HEVC codec.
    for (std::map<std::string, SrsStatisticStream*>::iterator it = streams.begin(); it != streams.end(); it++) {
        SrsStatisticStream* stream = it->second;
        if (stream->vcodec == SrsVideoCodecIdHEVC) {
            ss << "&h265=1";
            break;
        }
    }
#endif
}

#ifdef SRS_APM
void SrsStatistic::dumps_cls_summaries(SrsClsSugar* sugar)
{
    if (!vhosts.empty()) {
        sugar->kv("vhosts", srs_fmt("%d", (int)vhosts.size()));
    }
    if (!streams.empty()) {
        sugar->kv("streams", srs_fmt("%d", (int)streams.size()));
    }
    if (!clients.empty()) {
        sugar->kv("clients", srs_fmt("%d", (int)clients.size()));
    }
}

void SrsStatistic::dumps_cls_streams(SrsClsSugars* sugars)
{
    for (std::map<std::string, SrsStatisticStream*>::iterator it = streams.begin(); it != streams.end(); ++it) {
        SrsStatisticStream* stream = it->second;
        if (!stream->active || !stream->nb_clients) {
            continue;
        }

        SrsClsSugar* sugar = sugars->create();
        sugar->kv("hint", "stream");
        sugar->kv("version", RTMP_SIG_SRS_VERSION);
        sugar->kv("pid", srs_fmt("%d", getpid()));

        sugar->kv("sid", stream->id);
        sugar->kv("url", stream->url);

        if (stream->frames->r30s()) {
            sugar->kv("fps", srs_fmt("%d", stream->frames->r30s()));
        }
        if (stream->width) {
            sugar->kv("width", srs_fmt("%d", stream->width));
        }
        if (stream->height) {
            sugar->kv("height", srs_fmt("%d", stream->height));
        }

        SrsStatisticClient* pub = find_client(stream->publisher_id);
        if (pub) {
            if (pub->kbps->get_recv_kbps_30s()) {
                sugar->kv("recv", srs_fmt("%d", pub->kbps->get_recv_kbps_30s()));
            }
            if (pub->kbps->get_send_kbps_30s()) {
                sugar->kv("send", srs_fmt("%d", pub->kbps->get_send_kbps_30s()));
            }
        }

        sugar->kv("clients", srs_fmt("%d", stream->nb_clients));
        if (stream->kbps->get_recv_kbps_30s()) {
            sugar->kv("recv2", srs_fmt("%d", stream->kbps->get_recv_kbps_30s()));
        }
        if (stream->kbps->get_send_kbps_30s()) {
            sugar->kv("send2", srs_fmt("%d", stream->kbps->get_send_kbps_30s()));
        }
    }
}
#endif

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
    // To identify a stream, use url without extension, for example, the bellow are the same stream:
    //      ossrs.io/live/livestream
    //      ossrs.io/live/livestream.flv
    //      ossrs.io/live/livestream.m3u8
    // Note that we also don't use schema, and vhost is optional.
    string url = req->get_stream_url();
    
    SrsStatisticStream* stream = NULL;
    
    // create stream if not exists.
    if (rstreams.find(url) == rstreams.end()) {
        stream = new SrsStatisticStream();
        stream->vhost = vhost;
        stream->stream = req->stream;
        stream->app = req->app;
        stream->url = url;
        stream->tcUrl = req->tcUrl;
        rstreams[url] = stream;
        streams[stream->id] = stream;
        return stream;
    }
    
    stream = rstreams[url];
    
    return stream;
}

srs_error_t SrsStatistic::dumps_metrics(int64_t& send_bytes, int64_t& recv_bytes, int64_t& nstreams, int64_t& nclients, int64_t& total_nclients, int64_t& nerrs)
{
    srs_error_t err = srs_success;

    send_bytes = kbps->get_send_bytes();
    recv_bytes = kbps->get_recv_bytes();

    nstreams = streams.size();
    nclients = clients.size();

    total_nclients = nb_clients_;
    nerrs = nb_errs_;

    return err;
}

