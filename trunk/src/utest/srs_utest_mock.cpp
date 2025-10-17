//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_mock.hpp>

#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_kernel_error.hpp>

// MockRtcTrackDescriptionFactory implementation
MockRtcTrackDescriptionFactory::MockRtcTrackDescriptionFactory()
{
    audio_ssrc_ = 12345;
    video_ssrc_ = 67890;
    screen_ssrc_ = 98765;
}

MockRtcTrackDescriptionFactory::~MockRtcTrackDescriptionFactory()
{
}

std::map<uint32_t, SrsRtcTrackDescription *> MockRtcTrackDescriptionFactory::create_audio_video_tracks()
{
    std::map<uint32_t, SrsRtcTrackDescription *> sub_relations;

    // Create audio track
    SrsRtcTrackDescription *audio_desc = create_audio_track(audio_ssrc_, "audio-track-1", "0");
    sub_relations[audio_desc->ssrc_] = audio_desc;

    // Create video track
    SrsRtcTrackDescription *video_desc = create_video_track(video_ssrc_, "video-track-1", "1");
    sub_relations[video_desc->ssrc_] = video_desc;

    // Create screen share track
    SrsRtcTrackDescription *screen_desc = create_video_track(screen_ssrc_, "screen-track-1", "2");
    sub_relations[screen_desc->ssrc_] = screen_desc;

    return sub_relations;
}

SrsRtcTrackDescription *MockRtcTrackDescriptionFactory::create_audio_track(uint32_t ssrc, std::string id, std::string mid)
{
    SrsRtcTrackDescription *audio_desc = new SrsRtcTrackDescription();
    audio_desc->type_ = "audio";
    audio_desc->ssrc_ = ssrc;
    audio_desc->id_ = id;
    audio_desc->is_active_ = true;
    audio_desc->direction_ = "sendrecv";
    audio_desc->mid_ = mid;
    audio_desc->media_ = new SrsAudioPayload(111, "opus", 48000, 2);
    return audio_desc;
}

SrsRtcTrackDescription *MockRtcTrackDescriptionFactory::create_video_track(uint32_t ssrc, std::string id, std::string mid)
{
    SrsRtcTrackDescription *video_desc = new SrsRtcTrackDescription();
    video_desc->type_ = "video";
    video_desc->ssrc_ = ssrc;
    video_desc->id_ = id;
    video_desc->is_active_ = true;
    video_desc->direction_ = "sendrecv";
    video_desc->mid_ = mid;
    video_desc->media_ = new SrsVideoPayload(96, "H264", 90000);
    return video_desc;
}

// MockExpire implementation
MockExpire::MockExpire() : expired_(false)
{
}

MockExpire::~MockExpire()
{
}

void MockExpire::expire()
{
    expired_ = true;
}

// MockRtcAsyncCallRequest implementation
MockRtcAsyncCallRequest::MockRtcAsyncCallRequest(std::string vhost, std::string app, std::string stream)
{
    vhost_ = vhost;
    app_ = app;
    stream_ = stream;
}

MockRtcAsyncCallRequest::~MockRtcAsyncCallRequest()
{
}

ISrsRequest *MockRtcAsyncCallRequest::copy()
{
    MockRtcAsyncCallRequest *cp = new MockRtcAsyncCallRequest(vhost_, app_, stream_);
    return cp;
}

std::string MockRtcAsyncCallRequest::get_stream_url()
{
    return "rtmp://" + vhost_ + "/" + app_ + "/" + stream_;
}

void MockRtcAsyncCallRequest::update_auth(ISrsRequest *req)
{
}

void MockRtcAsyncCallRequest::strip()
{
}

ISrsRequest *MockRtcAsyncCallRequest::as_http()
{
    return this;
}

// MockRtcSourceManager implementation
MockRtcSourceManager::MockRtcSourceManager()
{
    initialize_error_ = srs_success;
    fetch_or_create_error_ = srs_success;
    initialize_count_ = 0;
    fetch_or_create_count_ = 0;
    mock_source_ = SrsSharedPtr<SrsRtcSource>(new SrsRtcSource());
}

MockRtcSourceManager::~MockRtcSourceManager()
{
    srs_freep(initialize_error_);
    srs_freep(fetch_or_create_error_);
}

srs_error_t MockRtcSourceManager::initialize()
{
    initialize_count_++;
    return srs_error_copy(initialize_error_);
}

srs_error_t MockRtcSourceManager::fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsRtcSource> &pps)
{
    fetch_or_create_count_++;
    if (fetch_or_create_error_ != srs_success) {
        return srs_error_copy(fetch_or_create_error_);
    }
    pps = mock_source_;
    return srs_success;
}

SrsSharedPtr<SrsRtcSource> MockRtcSourceManager::fetch(ISrsRequest *r)
{
    return mock_source_;
}

void MockRtcSourceManager::set_initialize_error(srs_error_t err)
{
    srs_freep(initialize_error_);
    initialize_error_ = srs_error_copy(err);
}

void MockRtcSourceManager::set_fetch_or_create_error(srs_error_t err)
{
    srs_freep(fetch_or_create_error_);
    fetch_or_create_error_ = srs_error_copy(err);
}

void MockRtcSourceManager::reset()
{
    srs_freep(initialize_error_);
    srs_freep(fetch_or_create_error_);
    initialize_error_ = srs_success;
    fetch_or_create_error_ = srs_success;
    initialize_count_ = 0;
    fetch_or_create_count_ = 0;
}

// MockRtcStatistic implementation
MockRtcStatistic::MockRtcStatistic()
{
    on_client_error_ = srs_success;
    on_client_count_ = 0;
    on_disconnect_count_ = 0;
    last_client_id_ = "";
    last_client_req_ = NULL;
    last_client_conn_ = NULL;
    last_client_type_ = SrsRtmpConnUnknown;
}

MockRtcStatistic::~MockRtcStatistic()
{
    srs_freep(on_client_error_);
}

void MockRtcStatistic::on_disconnect(std::string id, srs_error_t err)
{
    on_disconnect_count_++;
}

srs_error_t MockRtcStatistic::on_client(std::string id, ISrsRequest *req, ISrsExpire *conn, SrsRtmpConnType type)
{
    on_client_count_++;
    last_client_id_ = id;
    last_client_req_ = req;
    last_client_conn_ = conn;
    last_client_type_ = type;
    return srs_error_copy(on_client_error_);
}

srs_error_t MockRtcStatistic::on_video_info(ISrsRequest *req, SrsVideoCodecId vcodec, int avc_profile, int avc_level, int width, int height)
{
    return srs_success;
}

srs_error_t MockRtcStatistic::on_audio_info(ISrsRequest *req, SrsAudioCodecId acodec, SrsAudioSampleRate asample_rate, SrsAudioChannels asound_type, SrsAacObjectType aac_object)
{
    return srs_success;
}

void MockRtcStatistic::on_stream_publish(ISrsRequest *req, std::string publisher_id)
{
}

void MockRtcStatistic::on_stream_close(ISrsRequest *req)
{
}

void MockRtcStatistic::kbps_add_delta(std::string id, ISrsKbpsDelta *delta)
{
}

void MockRtcStatistic::kbps_sample()
{
}

srs_error_t MockRtcStatistic::on_video_frames(ISrsRequest *req, int nb_frames)
{
    return srs_success;
}

std::string MockRtcStatistic::server_id()
{
    return "";
}

std::string MockRtcStatistic::service_id()
{
    return "";
}

std::string MockRtcStatistic::service_pid()
{
    return "";
}

SrsStatisticVhost *MockRtcStatistic::find_vhost_by_id(std::string vid)
{
    return NULL;
}

SrsStatisticStream *MockRtcStatistic::find_stream(std::string sid)
{
    return NULL;
}

SrsStatisticStream *MockRtcStatistic::find_stream_by_url(std::string url)
{
    return NULL;
}

SrsStatisticClient *MockRtcStatistic::find_client(std::string client_id)
{
    return NULL;
}

srs_error_t MockRtcStatistic::dumps_vhosts(SrsJsonArray *arr)
{
    return srs_success;
}

srs_error_t MockRtcStatistic::dumps_streams(SrsJsonArray *arr, int start, int count)
{
    return srs_success;
}

srs_error_t MockRtcStatistic::dumps_clients(SrsJsonArray *arr, int start, int count)
{
    return srs_success;
}

srs_error_t MockRtcStatistic::dumps_metrics(int64_t &send_bytes, int64_t &recv_bytes, int64_t &nstreams, int64_t &nclients, int64_t &total_nclients, int64_t &nerrs)
{
    return srs_success;
}

void MockRtcStatistic::set_on_client_error(srs_error_t err)
{
    srs_freep(on_client_error_);
    on_client_error_ = srs_error_copy(err);
}

void MockRtcStatistic::reset()
{
    srs_freep(on_client_error_);
    on_client_error_ = srs_success;
    on_client_count_ = 0;
    on_disconnect_count_ = 0;
    last_client_id_ = "";
    last_client_req_ = NULL;
    last_client_conn_ = NULL;
    last_client_type_ = SrsRtmpConnUnknown;
}

// MockRtcAsyncTaskExecutor implementation
MockRtcAsyncTaskExecutor::MockRtcAsyncTaskExecutor()
{
    exec_error_ = srs_success;
    exec_count_ = 0;
    last_task_ = NULL;
}

MockRtcAsyncTaskExecutor::~MockRtcAsyncTaskExecutor()
{
}

srs_error_t MockRtcAsyncTaskExecutor::exec_rtc_async_work(ISrsAsyncCallTask *t)
{
    exec_count_++;
    last_task_ = t;
    return exec_error_;
}

void MockRtcAsyncTaskExecutor::set_exec_error(srs_error_t err)
{
    exec_error_ = err;
}

void MockRtcAsyncTaskExecutor::reset()
{
    exec_error_ = srs_success;
    exec_count_ = 0;
    last_task_ = NULL;
}

// MockRtcPacketSender implementation
MockRtcPacketSender::MockRtcPacketSender()
{
    send_packet_error_ = srs_success;
    send_packet_count_ = 0;
    last_sent_packet_ = NULL;
}

MockRtcPacketSender::~MockRtcPacketSender()
{
}

srs_error_t MockRtcPacketSender::do_send_packet(SrsRtpPacket *pkt)
{
    send_packet_count_++;
    last_sent_packet_ = pkt;
    return send_packet_error_;
}

void MockRtcPacketSender::set_send_packet_error(srs_error_t err)
{
    send_packet_error_ = err;
}

void MockRtcPacketSender::reset()
{
    send_packet_error_ = srs_success;
    send_packet_count_ = 0;
    last_sent_packet_ = NULL;
}

// MockAppConfig implementation
MockAppConfig::MockAppConfig()
{
    http_hooks_enabled_ = true;
    on_stop_directive_ = NULL;
    on_unpublish_directive_ = NULL;
    rtc_nack_enabled_ = true;
    rtc_nack_no_copy_ = false;
    rtc_drop_for_pt_ = 0;
    rtc_twcc_enabled_ = true;
    srt_enabled_ = false;
    rtc_to_rtmp_ = false;
    dash_dispose_ = 0;
    dash_enabled_ = false;
    api_as_candidates_ = true;
    resolve_api_domain_ = true;
    keep_api_domain_ = false;
    mw_msgs_ = 8;
}

MockAppConfig::~MockAppConfig()
{
    clear_on_stop_directive();
    clear_on_unpublish_directive();
}

srs_utime_t MockAppConfig::get_pithy_print()
{
    return 10 * SRS_UTIME_SECONDS;
}

std::string MockAppConfig::get_default_app_name()
{
    return "live";
}

void MockAppConfig::subscribe(ISrsReloadHandler *handler)
{
    // Do nothing in mock
}

void MockAppConfig::unsubscribe(ISrsReloadHandler *handler)
{
    // Do nothing in mock
}

bool MockAppConfig::get_vhost_http_hooks_enabled(std::string vhost)
{
    return http_hooks_enabled_;
}

SrsConfDirective *MockAppConfig::get_vhost_on_stop(std::string vhost)
{
    return on_stop_directive_;
}

SrsConfDirective *MockAppConfig::get_vhost_on_unpublish(std::string vhost)
{
    return on_unpublish_directive_;
}

SrsConfDirective *MockAppConfig::get_vhost_on_dvr(std::string vhost)
{
    return NULL;
}

bool MockAppConfig::get_rtc_nack_enabled(std::string vhost)
{
    return rtc_nack_enabled_;
}

bool MockAppConfig::get_rtc_nack_no_copy(std::string vhost)
{
    return rtc_nack_no_copy_;
}

bool MockAppConfig::get_realtime_enabled(std::string vhost, bool is_rtc)
{
    return true;
}

int MockAppConfig::get_mw_msgs(std::string vhost, bool is_realtime, bool is_rtc)
{
    return mw_msgs_;
}

int MockAppConfig::get_rtc_drop_for_pt(std::string vhost)
{
    return rtc_drop_for_pt_;
}

bool MockAppConfig::get_rtc_twcc_enabled(std::string vhost)
{
    return rtc_twcc_enabled_;
}

bool MockAppConfig::get_srt_enabled()
{
    return srt_enabled_;
}

bool MockAppConfig::get_srt_enabled(std::string vhost)
{
    return srt_enabled_;
}

std::string MockAppConfig::get_srt_default_streamid()
{
    return "#!::r=live/livestream,m=request";
}

bool MockAppConfig::get_srt_to_rtmp(std::string vhost)
{
    return true;
}

bool MockAppConfig::get_rtc_to_rtmp(std::string vhost)
{
    return rtc_to_rtmp_;
}

srs_utime_t MockAppConfig::get_rtc_stun_timeout(std::string vhost)
{
    return 30 * SRS_UTIME_SECONDS;
}

bool MockAppConfig::get_rtc_stun_strict_check(std::string vhost)
{
    return false;
}

std::string MockAppConfig::get_rtc_dtls_role(std::string vhost)
{
    return "passive";
}

std::string MockAppConfig::get_rtc_dtls_version(std::string vhost)
{
    return "auto";
}

SrsConfDirective *MockAppConfig::get_vhost_on_hls(std::string vhost)
{
    return NULL;
}

SrsConfDirective *MockAppConfig::get_vhost_on_hls_notify(std::string vhost)
{
    return NULL;
}

bool MockAppConfig::get_hls_enabled(std::string vhost)
{
    return false;
}

bool MockAppConfig::get_hls_enabled(SrsConfDirective *vhost)
{
    return false;
}

bool MockAppConfig::get_hls_use_fmp4(std::string vhost)
{
    return false;
}

std::string MockAppConfig::get_hls_entry_prefix(std::string vhost)
{
    return "";
}

std::string MockAppConfig::get_hls_path(std::string vhost)
{
    return "./objs/nginx/html";
}

std::string MockAppConfig::get_hls_m3u8_file(std::string vhost)
{
    return "[app]/[stream].m3u8";
}

std::string MockAppConfig::get_hls_ts_file(std::string vhost)
{
    return "[app]/[stream]-[seq].ts";
}

std::string MockAppConfig::get_hls_fmp4_file(std::string vhost)
{
    return "[app]/[stream]-[seq].m4s";
}

std::string MockAppConfig::get_hls_init_file(std::string vhost)
{
    return "[app]/[stream]-init.mp4";
}

bool MockAppConfig::get_hls_ts_floor(std::string vhost)
{
    return false;
}

srs_utime_t MockAppConfig::get_hls_fragment(std::string vhost)
{
    return 10 * SRS_UTIME_SECONDS;
}

double MockAppConfig::get_hls_td_ratio(std::string vhost)
{
    return 1.5;
}

double MockAppConfig::get_hls_aof_ratio(std::string vhost)
{
    return 2.0;
}

srs_utime_t MockAppConfig::get_hls_window(std::string vhost)
{
    return 60 * SRS_UTIME_SECONDS;
}

std::string MockAppConfig::get_hls_on_error(std::string vhost)
{
    return "continue";
}

bool MockAppConfig::get_hls_cleanup(std::string vhost)
{
    return true;
}

srs_utime_t MockAppConfig::get_hls_dispose(std::string vhost)
{
    return 120 * SRS_UTIME_SECONDS;
}

bool MockAppConfig::get_hls_wait_keyframe(std::string vhost)
{
    return true;
}

bool MockAppConfig::get_hls_keys(std::string vhost)
{
    return false;
}

int MockAppConfig::get_hls_fragments_per_key(std::string vhost)
{
    return 5;
}

std::string MockAppConfig::get_hls_key_file(std::string vhost)
{
    return "[app]/[stream]-[seq].key";
}

std::string MockAppConfig::get_hls_key_file_path(std::string vhost)
{
    return "./objs/nginx/html";
}

std::string MockAppConfig::get_hls_key_url(std::string vhost)
{
    return "";
}

int MockAppConfig::get_vhost_hls_nb_notify(std::string vhost)
{
    return 64;
}

bool MockAppConfig::get_vhost_hls_dts_directly(std::string vhost)
{
    return true;
}

bool MockAppConfig::get_hls_ctx_enabled(std::string vhost)
{
    return true;
}

bool MockAppConfig::get_hls_ts_ctx_enabled(std::string vhost)
{
    return true;
}

bool MockAppConfig::get_hls_recover(std::string vhost)
{
    return true;
}

bool MockAppConfig::get_forward_enabled(std::string vhost)
{
    return false;
}

SrsConfDirective *MockAppConfig::get_forwards(std::string vhost)
{
    return NULL;
}

srs_utime_t MockAppConfig::get_queue_length(std::string vhost)
{
    return 30 * SRS_UTIME_SECONDS;
}

SrsConfDirective *MockAppConfig::get_forward_backend(std::string vhost)
{
    return NULL;
}

bool MockAppConfig::get_atc(std::string vhost)
{
    return false;
}

int MockAppConfig::get_time_jitter(std::string vhost)
{
    return SrsRtmpJitterAlgorithmFULL;
}

bool MockAppConfig::get_mix_correct(std::string vhost)
{
    return false;
}

bool MockAppConfig::try_annexb_first(std::string vhost)
{
    return true;
}

bool MockAppConfig::get_vhost_is_edge(std::string vhost)
{
    return false;
}

bool MockAppConfig::get_atc_auto(std::string vhost)
{
    return false;
}

bool MockAppConfig::get_reduce_sequence_header(std::string vhost)
{
    return false;
}

bool MockAppConfig::get_parse_sps(std::string vhost)
{
    return true;
}

void MockAppConfig::set_http_hooks_enabled(bool enabled)
{
    http_hooks_enabled_ = enabled;
}

void MockAppConfig::set_on_stop_urls(const std::vector<std::string> &urls)
{
    clear_on_stop_directive();
    if (!urls.empty()) {
        on_stop_directive_ = new SrsConfDirective();
        on_stop_directive_->name_ = "on_stop";
        on_stop_directive_->args_ = urls;
    }
}

void MockAppConfig::clear_on_stop_directive()
{
    srs_freep(on_stop_directive_);
}

void MockAppConfig::set_on_unpublish_urls(const std::vector<std::string> &urls)
{
    clear_on_unpublish_directive();
    if (!urls.empty()) {
        on_unpublish_directive_ = new SrsConfDirective();
        on_unpublish_directive_->name_ = "on_unpublish";
        on_unpublish_directive_->args_ = urls;
    }
}

void MockAppConfig::clear_on_unpublish_directive()
{
    srs_freep(on_unpublish_directive_);
}

void MockAppConfig::set_rtc_nack_enabled(bool enabled)
{
    rtc_nack_enabled_ = enabled;
}

void MockAppConfig::set_rtc_nack_no_copy(bool no_copy)
{
    rtc_nack_no_copy_ = no_copy;
}

void MockAppConfig::set_rtc_drop_for_pt(int pt)
{
    rtc_drop_for_pt_ = pt;
}

void MockAppConfig::set_rtc_twcc_enabled(bool enabled)
{
    rtc_twcc_enabled_ = enabled;
}

void MockAppConfig::set_srt_enabled(bool enabled)
{
    srt_enabled_ = enabled;
}

void MockAppConfig::set_rtc_to_rtmp(bool enabled)
{
    rtc_to_rtmp_ = enabled;
}

void MockAppConfig::set_api_as_candidates(bool enabled)
{
    api_as_candidates_ = enabled;
}

void MockAppConfig::set_resolve_api_domain(bool enabled)
{
    resolve_api_domain_ = enabled;
}

void MockAppConfig::set_keep_api_domain(bool enabled)
{
    keep_api_domain_ = enabled;
}
