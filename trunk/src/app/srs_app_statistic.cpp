//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_statistic.hpp>

#include <sstream>
#include <unistd.h>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_protocol_conn.hpp>

#include <srs_app_utility.hpp>
#include <srs_kernel_kbps.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_utility.hpp>

string srs_generate_stat_vid()
{
    SrsRand rand;
    return "vid-" + rand.gen_str(7);
}

SrsStatisticVhost::SrsStatisticVhost()
{
    id_ = srs_generate_stat_vid();

    kbps_ = new SrsKbps();

    nb_clients_ = 0;
    nb_streams_ = 0;
}

SrsStatisticVhost::~SrsStatisticVhost()
{
    srs_freep(kbps_);
}

srs_error_t SrsStatisticVhost::dumps(SrsJsonObject *obj)
{
    srs_error_t err = srs_success;

    // dumps the config of vhost.
    bool hls_enabled = _srs_config->get_hls_enabled(vhost_);
    bool enabled = _srs_config->get_vhost_enabled(vhost_);

    obj->set("id", SrsJsonAny::str(id_.c_str()));
    obj->set("name", SrsJsonAny::str(vhost_.c_str()));
    obj->set("enabled", SrsJsonAny::boolean(enabled));
    obj->set("clients", SrsJsonAny::integer(nb_clients_));
    obj->set("streams", SrsJsonAny::integer(nb_streams_));
    obj->set("send_bytes", SrsJsonAny::integer(kbps_->get_send_bytes()));
    obj->set("recv_bytes", SrsJsonAny::integer(kbps_->get_recv_bytes()));

    SrsJsonObject *okbps = SrsJsonAny::object();
    obj->set("kbps", okbps);

    okbps->set("recv_30s", SrsJsonAny::integer(kbps_->get_recv_kbps_30s()));
    okbps->set("send_30s", SrsJsonAny::integer(kbps_->get_send_kbps_30s()));

    SrsJsonObject *hls = SrsJsonAny::object();
    obj->set("hls", hls);

    hls->set("enabled", SrsJsonAny::boolean(hls_enabled));
    if (hls_enabled) {
        hls->set("fragment", SrsJsonAny::number(srsu2msi(_srs_config->get_hls_fragment(vhost_)) / 1000.0));
    }

    return err;
}

SrsStatisticStream::SrsStatisticStream()
{
    id_ = srs_generate_stat_vid();
    vhost_ = NULL;
    active_ = false;

    has_video_ = false;
    vcodec_ = SrsVideoCodecIdReserved;
    avc_profile_ = SrsAvcProfileReserved;
    avc_level_ = SrsAvcLevelReserved;

    has_audio_ = false;
    acodec_ = SrsAudioCodecIdReserved1;
    asample_rate_ = SrsAudioSampleRateReserved;
    asound_type_ = SrsAudioChannelsReserved;
    aac_object_ = SrsAacObjectTypeReserved;
    width_ = 0;
    height_ = 0;

    kbps_ = new SrsKbps();

    nb_clients_ = 0;
    video_frames_ = new SrsPps();
    audio_frames_ = new SrsPps();
}

SrsStatisticStream::~SrsStatisticStream()
{
    srs_freep(kbps_);
    srs_freep(video_frames_);
    srs_freep(audio_frames_);
}

srs_error_t SrsStatisticStream::dumps(SrsJsonObject *obj)
{
    srs_error_t err = srs_success;

    obj->set("id", SrsJsonAny::str(id_.c_str()));
    obj->set("name", SrsJsonAny::str(stream_.c_str()));
    obj->set("vhost", SrsJsonAny::str(vhost_->id_.c_str()));
    obj->set("app", SrsJsonAny::str(app_.c_str()));
    obj->set("tcUrl", SrsJsonAny::str(tcUrl_.c_str()));
    obj->set("url", SrsJsonAny::str(url_.c_str()));
    obj->set("live_ms", SrsJsonAny::integer(srsu2ms(srs_time_now_cached())));
    obj->set("clients", SrsJsonAny::integer(nb_clients_));
    obj->set("frames", SrsJsonAny::integer(video_frames_->sugar_ + audio_frames_->sugar_));
    obj->set("audio_frames", SrsJsonAny::integer(audio_frames_->sugar_));
    obj->set("video_frames", SrsJsonAny::integer(video_frames_->sugar_));
    obj->set("send_bytes", SrsJsonAny::integer(kbps_->get_send_bytes()));
    obj->set("recv_bytes", SrsJsonAny::integer(kbps_->get_recv_bytes()));

    SrsJsonObject *okbps = SrsJsonAny::object();
    obj->set("kbps", okbps);

    okbps->set("recv_30s", SrsJsonAny::integer(kbps_->get_recv_kbps_30s()));
    okbps->set("send_30s", SrsJsonAny::integer(kbps_->get_send_kbps_30s()));

    SrsJsonObject *publish = SrsJsonAny::object();
    obj->set("publish", publish);

    publish->set("active", SrsJsonAny::boolean(active_));
    if (!publisher_id_.empty()) {
        publish->set("cid", SrsJsonAny::str(publisher_id_.c_str()));
    }

    if (!has_video_) {
        obj->set("video", SrsJsonAny::null());
    } else {
        SrsJsonObject *video = SrsJsonAny::object();
        obj->set("video", video);

        video->set("codec", SrsJsonAny::str(srs_video_codec_id2str(vcodec_).c_str()));

        if (vcodec_ == SrsVideoCodecIdAVC) {
            video->set("profile", SrsJsonAny::str(srs_avc_profile2str(avc_profile_).c_str()));
            video->set("level", SrsJsonAny::str(srs_avc_level2str(avc_level_).c_str()));
        } else if (vcodec_ == SrsVideoCodecIdHEVC) {
            video->set("profile", SrsJsonAny::str(srs_hevc_profile2str(hevc_profile_).c_str()));
            video->set("level", SrsJsonAny::str(srs_hevc_level2str(hevc_level_).c_str()));
        } else if (vcodec_ == SrsVideoCodecIdAV1) {
            video->set("profile", SrsJsonAny::null());
            video->set("level", SrsJsonAny::null());
        } else if (vcodec_ == SrsVideoCodecIdVP9) {
            video->set("profile", SrsJsonAny::null());
            video->set("level", SrsJsonAny::null());
        } else {
            video->set("profile", SrsJsonAny::str("Other"));
            video->set("level", SrsJsonAny::str("Other"));
        }

        video->set("width", SrsJsonAny::integer(width_));
        video->set("height", SrsJsonAny::integer(height_));
    }

    if (!has_audio_) {
        obj->set("audio", SrsJsonAny::null());
    } else {
        SrsJsonObject *audio = SrsJsonAny::object();
        obj->set("audio", audio);

        audio->set("codec", SrsJsonAny::str(srs_audio_codec_id2str(acodec_).c_str()));
        audio->set("sample_rate", SrsJsonAny::integer(srs_audio_sample_rate2number(asample_rate_)));
        audio->set("channel", SrsJsonAny::integer(asound_type_ + 1));
        audio->set("profile", SrsJsonAny::str(srs_aac_object2str(aac_object_).c_str()));
    }

    return err;
}

void SrsStatisticStream::publish(std::string id)
{
    // To prevent duplicated publish event by bridge.
    if (active_) {
        return;
    }

    publisher_id_ = id;
    active_ = true;

    vhost_->nb_streams_++;
}

void SrsStatisticStream::close()
{
    // To prevent duplicated close event.
    if (!active_) {
        return;
    }

    has_video_ = false;
    has_audio_ = false;
    active_ = false;

    vhost_->nb_streams_--;
}

SrsStatisticClient::SrsStatisticClient()
{
    stream_ = NULL;
    conn_ = NULL;
    req_ = NULL;
    type_ = SrsRtmpConnUnknown;
    create_ = srs_time_now_cached();

    kbps_ = new SrsKbps();
}

SrsStatisticClient::~SrsStatisticClient()
{
    srs_freep(kbps_);
    srs_freep(req_);
}

srs_error_t SrsStatisticClient::dumps(SrsJsonObject *obj)
{
    srs_error_t err = srs_success;

    obj->set("id", SrsJsonAny::str(id_.c_str()));
    obj->set("vhost", SrsJsonAny::str(stream_->vhost_->id_.c_str()));
    obj->set("stream", SrsJsonAny::str(stream_->id_.c_str()));
    obj->set("ip", SrsJsonAny::str(req_->ip_.c_str()));
    obj->set("pageUrl", SrsJsonAny::str(req_->pageUrl_.c_str()));
    obj->set("swfUrl", SrsJsonAny::str(req_->swfUrl_.c_str()));
    obj->set("tcUrl", SrsJsonAny::str(req_->tcUrl_.c_str()));
    obj->set("url", SrsJsonAny::str(req_->get_stream_url().c_str()));
    obj->set("name", SrsJsonAny::str(req_->stream_.c_str()));
    obj->set("type", SrsJsonAny::str(srs_client_type_string(type_).c_str()));
    obj->set("publish", SrsJsonAny::boolean(srs_client_type_is_publish(type_)));
    obj->set("alive", SrsJsonAny::number(srsu2ms(srs_time_now_cached() - create_) / 1000.0));
    obj->set("send_bytes", SrsJsonAny::integer(kbps_->get_send_bytes()));
    obj->set("recv_bytes", SrsJsonAny::integer(kbps_->get_recv_bytes()));

    SrsJsonObject *okbps = SrsJsonAny::object();
    obj->set("kbps", okbps);

    okbps->set("recv_30s", SrsJsonAny::integer(kbps_->get_recv_kbps_30s()));
    okbps->set("send_30s", SrsJsonAny::integer(kbps_->get_send_kbps_30s()));

    return err;
}

ISrsStatistic::ISrsStatistic()
{
}

ISrsStatistic::~ISrsStatistic()
{
}

SrsStatistic *_srs_stat = NULL;

SrsStatistic::SrsStatistic()
{
    kbps_ = new SrsKbps();

    nb_clients_ = 0;
    nb_errs_ = 0;
}

SrsStatistic::~SrsStatistic()
{
    srs_freep(kbps_);

    if (true) {
        std::map<std::string, SrsStatisticVhost *>::iterator it;
        for (it = vhosts_.begin(); it != vhosts_.end(); it++) {
            SrsStatisticVhost *vhost = it->second;
            srs_freep(vhost);
        }
    }
    if (true) {
        std::map<std::string, SrsStatisticStream *>::iterator it;
        for (it = streams_.begin(); it != streams_.end(); it++) {
            SrsStatisticStream *stream = it->second;
            srs_freep(stream);
        }
    }
    if (true) {
        std::map<std::string, SrsStatisticClient *>::iterator it;
        for (it = clients_.begin(); it != clients_.end(); it++) {
            SrsStatisticClient *client = it->second;
            srs_freep(client);
        }
    }

    vhosts_.clear();
    rvhosts_.clear();
    streams_.clear();
    rstreams_.clear();
}

SrsStatisticVhost *SrsStatistic::find_vhost_by_id(std::string vid)
{
    std::map<string, SrsStatisticVhost *>::iterator it;
    if ((it = vhosts_.find(vid)) != vhosts_.end()) {
        return it->second;
    }
    return NULL;
}

SrsStatisticVhost *SrsStatistic::find_vhost_by_name(string name)
{
    if (rvhosts_.empty()) {
        return NULL;
    }

    std::map<string, SrsStatisticVhost *>::iterator it;
    if ((it = rvhosts_.find(name)) != rvhosts_.end()) {
        return it->second;
    }
    return NULL;
}

SrsStatisticStream *SrsStatistic::find_stream(string sid)
{
    std::map<std::string, SrsStatisticStream *>::iterator it;
    if ((it = streams_.find(sid)) != streams_.end()) {
        return it->second;
    }
    return NULL;
}

SrsStatisticStream *SrsStatistic::find_stream_by_url(string url)
{
    std::map<std::string, SrsStatisticStream *>::iterator it;
    if ((it = rstreams_.find(url)) != rstreams_.end()) {
        return it->second;
    }
    return NULL;
}

SrsStatisticClient *SrsStatistic::find_client(string client_id)
{
    std::map<std::string, SrsStatisticClient *>::iterator it;
    if ((it = clients_.find(client_id)) != clients_.end()) {
        return it->second;
    }
    return NULL;
}

srs_error_t SrsStatistic::on_video_info(ISrsRequest *req, SrsVideoCodecId vcodec, int profile, int level, int width, int height)
{
    srs_error_t err = srs_success;

    SrsStatisticVhost *vhost = create_vhost(req);
    SrsStatisticStream *stream = create_stream(vhost, req);

    stream->has_video_ = true;
    stream->vcodec_ = vcodec;

    if (vcodec == SrsVideoCodecIdAVC) {
        stream->avc_profile_ = (SrsAvcProfile)profile;
        stream->avc_level_ = (SrsAvcLevel)level;
    } else if (vcodec == SrsVideoCodecIdHEVC) {
        stream->hevc_profile_ = (SrsHevcProfile)profile;
        stream->hevc_level_ = (SrsHevcLevel)level;
    } else {
        stream->avc_profile_ = (SrsAvcProfile)profile;
        stream->avc_level_ = (SrsAvcLevel)level;
    }

    stream->width_ = width;
    stream->height_ = height;

    return err;
}

srs_error_t SrsStatistic::on_audio_info(ISrsRequest *req, SrsAudioCodecId acodec, SrsAudioSampleRate asample_rate, SrsAudioChannels asound_type, SrsAacObjectType aac_object)
{
    srs_error_t err = srs_success;

    SrsStatisticVhost *vhost = create_vhost(req);
    SrsStatisticStream *stream = create_stream(vhost, req);

    stream->has_audio_ = true;
    stream->acodec_ = acodec;
    stream->asample_rate_ = asample_rate;
    stream->asound_type_ = asound_type;
    stream->aac_object_ = aac_object;

    return err;
}

srs_error_t SrsStatistic::on_video_frames(ISrsRequest *req, int nb_frames)
{
    srs_error_t err = srs_success;

    SrsStatisticVhost *vhost = create_vhost(req);
    SrsStatisticStream *stream = create_stream(vhost, req);

    stream->video_frames_->sugar_ += nb_frames;

    return err;
}

srs_error_t SrsStatistic::on_audio_frames(ISrsRequest *req, int nb_frames)
{
    srs_error_t err = srs_success;

    SrsStatisticVhost *vhost = create_vhost(req);
    SrsStatisticStream *stream = create_stream(vhost, req);

    stream->audio_frames_->sugar_ += nb_frames;

    return err;
}

void SrsStatistic::on_stream_publish(ISrsRequest *req, std::string publisher_id)
{
    SrsStatisticVhost *vhost = create_vhost(req);
    SrsStatisticStream *stream = create_stream(vhost, req);

    stream->publish(publisher_id);
}

void SrsStatistic::on_stream_close(ISrsRequest *req)
{
    SrsStatisticVhost *vhost = create_vhost(req);
    SrsStatisticStream *stream = create_stream(vhost, req);
    stream->close();
}

srs_error_t SrsStatistic::on_client(std::string id, ISrsRequest *req, ISrsExpire *conn, SrsRtmpConnType type)
{
    srs_error_t err = srs_success;

    SrsStatisticVhost *vhost = create_vhost(req);
    SrsStatisticStream *stream = create_stream(vhost, req);

    // create client if not exists
    SrsStatisticClient *client = NULL;
    if (clients_.find(id) == clients_.end()) {
        client = new SrsStatisticClient();
        client->id_ = id;
        client->stream_ = stream;
        clients_[id] = client;
    } else {
        client = clients_[id];
    }

    // got client.
    client->conn_ = conn;
    client->type_ = type;
    stream->nb_clients_++;
    vhost->nb_clients_++;

    // The req might be freed, in such as SrsLiveStream::update, so we must copy it.
    // @see https://github.com/ossrs/srs/issues/2311
    srs_freep(client->req_);
    client->req_ = req->copy();

    nb_clients_++;

    return err;
}

void SrsStatistic::on_disconnect(std::string id, srs_error_t err)
{
    std::map<std::string, SrsStatisticClient *>::iterator it = clients_.find(id);
    if (it == clients_.end())
        return;

    SrsStatisticClient *client = it->second;
    SrsStatisticStream *stream = client->stream_;
    SrsStatisticVhost *vhost = stream->vhost_;

    srs_freep(client);
    clients_.erase(it);

    stream->nb_clients_--;
    vhost->nb_clients_--;

    if (srs_error_code(err) != ERROR_SUCCESS) {
        nb_errs_++;
    }

    cleanup_stream(stream);
}

void SrsStatistic::cleanup_stream(SrsStatisticStream *stream)
{
    // If stream has publisher(not active) or player(clients), never cleanup it.
    if (stream->active_ || stream->nb_clients_ > 0) {
        return;
    }

    // There should not be any clients referring to the stream.
    for (std::map<std::string, SrsStatisticClient *>::iterator it = clients_.begin(); it != clients_.end(); ++it) {
        SrsStatisticClient *client = it->second;
        srs_assert(client->stream_ != stream);
    }

    // Do cleanup streams.
    if (true) {
        std::map<std::string, SrsStatisticStream *>::iterator it;
        if ((it = streams_.find(stream->id_)) != streams_.end()) {
            streams_.erase(it);
        }
    }

    if (true) {
        std::map<std::string, SrsStatisticStream *>::iterator it;
        if ((it = rstreams_.find(stream->url_)) != rstreams_.end()) {
            rstreams_.erase(it);
        }
    }

    // It's safe to delete the stream now.
    srs_freep(stream);
}

void SrsStatistic::kbps_add_delta(std::string id, ISrsKbpsDelta *delta)
{
    if (!delta)
        return;

    map<string, SrsStatisticClient *>::iterator it = clients_.find(id);
    if (it == clients_.end())
        return;

    SrsStatisticClient *client = it->second;

    // resample the kbps to collect the delta.
    int64_t in, out;
    delta->remark(&in, &out);

    // add delta of connection to kbps.
    // for next sample() of server kbps can get the stat.
    kbps_->add_delta(in, out);
    client->kbps_->add_delta(in, out);
    client->stream_->kbps_->add_delta(in, out);
    client->stream_->vhost_->kbps_->add_delta(in, out);
}

void SrsStatistic::kbps_sample()
{
    kbps_->sample();
    if (true) {
        std::map<std::string, SrsStatisticVhost *>::iterator it;
        for (it = vhosts_.begin(); it != vhosts_.end(); it++) {
            SrsStatisticVhost *vhost = it->second;
            vhost->kbps_->sample();
        }
    }
    if (true) {
        std::map<std::string, SrsStatisticStream *>::iterator it;
        for (it = streams_.begin(); it != streams_.end(); it++) {
            SrsStatisticStream *stream = it->second;
            stream->kbps_->sample();
            stream->video_frames_->update();
            stream->audio_frames_->update();
        }
    }
    if (true) {
        std::map<std::string, SrsStatisticClient *>::iterator it;
        for (it = clients_.begin(); it != clients_.end(); it++) {
            SrsStatisticClient *client = it->second;
            client->kbps_->sample();
        }
    }

    // Update server level data.
    srs_update_rtmp_server((int)clients_.size(), kbps_);
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
        SrsRand rand;
        service_id_ = rand.gen_str(8);
    }

    return service_id_;
}

std::string SrsStatistic::service_pid()
{
    if (service_pid_.empty()) {
        service_pid_ = srs_strconv_format_int(getpid());
    }

    return service_pid_;
}

srs_error_t SrsStatistic::dumps_vhosts(SrsJsonArray *arr)
{
    srs_error_t err = srs_success;

    std::map<std::string, SrsStatisticVhost *>::iterator it;
    for (it = vhosts_.begin(); it != vhosts_.end(); it++) {
        SrsStatisticVhost *vhost = it->second;

        SrsJsonObject *obj = SrsJsonAny::object();
        arr->append(obj);

        if ((err = vhost->dumps(obj)) != srs_success) {
            return srs_error_wrap(err, "dump vhost");
        }
    }

    return err;
}

srs_error_t SrsStatistic::dumps_streams(SrsJsonArray *arr, int start, int count)
{
    srs_error_t err = srs_success;

    std::map<std::string, SrsStatisticStream *>::iterator it = streams_.begin();
    for (int i = 0; i < start + count && it != streams_.end(); it++, i++) {
        if (i < start) {
            continue;
        }

        SrsStatisticStream *stream = it->second;

        SrsJsonObject *obj = SrsJsonAny::object();
        arr->append(obj);

        if ((err = stream->dumps(obj)) != srs_success) {
            return srs_error_wrap(err, "dump stream");
        }
    }

    return err;
}

srs_error_t SrsStatistic::dumps_clients(SrsJsonArray *arr, int start, int count)
{
    srs_error_t err = srs_success;

    std::map<std::string, SrsStatisticClient *>::iterator it = clients_.begin();
    for (int i = 0; i < start + count && it != clients_.end(); it++, i++) {
        if (i < start) {
            continue;
        }

        SrsStatisticClient *client = it->second;

        SrsJsonObject *obj = SrsJsonAny::object();
        arr->append(obj);

        if ((err = client->dumps(obj)) != srs_success) {
            return srs_error_wrap(err, "dump client");
        }
    }

    return err;
}

void SrsStatistic::dumps_hints_kv(std::stringstream &ss)
{
    if (!streams_.empty()) {
        ss << "&streams=" << streams_.size();
    }
    if (!clients_.empty()) {
        ss << "&clients=" << clients_.size();
    }
    if (kbps_->get_recv_kbps_30s()) {
        ss << "&recv=" << kbps_->get_recv_kbps_30s();
    }
    if (kbps_->get_send_kbps_30s()) {
        ss << "&send=" << kbps_->get_send_kbps_30s();
    }

    // For HEVC, we should check active stream which is HEVC codec.
    for (std::map<std::string, SrsStatisticStream *>::iterator it = streams_.begin(); it != streams_.end(); it++) {
        SrsStatisticStream *stream = it->second;
        if (stream->vcodec_ == SrsVideoCodecIdHEVC) {
            ss << "&h265=1";
            break;
        }
    }
}

SrsStatisticVhost *SrsStatistic::create_vhost(ISrsRequest *req)
{
    SrsStatisticVhost *vhost = NULL;

    // create vhost if not exists.
    if (rvhosts_.find(req->vhost_) == rvhosts_.end()) {
        vhost = new SrsStatisticVhost();
        vhost->vhost_ = req->vhost_;
        rvhosts_[req->vhost_] = vhost;
        vhosts_[vhost->id_] = vhost;
        return vhost;
    }

    vhost = rvhosts_[req->vhost_];

    return vhost;
}

SrsStatisticStream *SrsStatistic::create_stream(SrsStatisticVhost *vhost, ISrsRequest *req)
{
    // To identify a stream, use url without extension, for example, the bellow are the same stream:
    //      ossrs.io/live/livestream
    //      ossrs.io/live/livestream.flv
    //      ossrs.io/live/livestream.m3u8
    // Note that we also don't use schema, and vhost is optional.
    string url = req->get_stream_url();

    SrsStatisticStream *stream = NULL;

    // create stream if not exists.
    if (rstreams_.find(url) == rstreams_.end()) {
        stream = new SrsStatisticStream();
        stream->vhost_ = vhost;
        stream->stream_ = req->stream_;
        stream->app_ = req->app_;
        stream->url_ = url;
        stream->tcUrl_ = req->tcUrl_;
        rstreams_[url] = stream;
        streams_[stream->id_] = stream;
        return stream;
    }

    stream = rstreams_[url];

    return stream;
}

srs_error_t SrsStatistic::dumps_metrics(int64_t &send_bytes, int64_t &recv_bytes, int64_t &nstreams, int64_t &nclients, int64_t &total_nclients, int64_t &nerrs)
{
    srs_error_t err = srs_success;

    send_bytes = kbps_->get_send_bytes();
    recv_bytes = kbps_->get_recv_bytes();

    nstreams = streams_.size();
    nclients = clients_.size();

    total_nclients = nb_clients_;
    nerrs = nb_errs_;

    return err;
}
