//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_STATISTIC_HPP
#define SRS_APP_STATISTIC_HPP

#include <srs_core.hpp>

#include <map>
#include <sstream>
#include <string>
#include <vector>

#include <srs_kernel_codec.hpp>
#include <srs_protocol_rtmp_stack.hpp>

class SrsKbps;
class ISrsClock;
class ISrsAppConfig;
class ISrsRand;
class SrsWallClock;
class ISrsRequest;
class ISrsExpire;
class SrsJsonObject;
class SrsJsonArray;
class ISrsKbpsDelta;
class SrsClsSugar;
class SrsClsSugars;
class SrsPps;

struct SrsStatisticVhost {
public:
    ISrsAppConfig *config_;
    ISrsRand *rand_;

public:
    std::string id_;
    std::string vhost_;
    int nb_streams_;
    int nb_clients_;

public:
    SrsKbps *kbps_;

public:
    SrsStatisticVhost();
    void assemble(); // Not in the constructor, to avoid calling functions there.
    virtual ~SrsStatisticVhost();

public:
    virtual srs_error_t dumps(SrsJsonObject *obj);
};

struct SrsStatisticStream {
public:
    ISrsClock *clk_;
    ISrsRand *rand_;

public:
    std::string id_;
    SrsStatisticVhost *vhost_;
    std::string app_;
    std::string stream_;
    std::string url_;
    std::string tcUrl_;
    bool active_;
    std::string publisher_id_;
    int nb_clients_;
    srs_utime_t create_;

public:
    SrsKbps *kbps_;
    SrsPps *video_frames_;
    SrsPps *audio_frames_;

public:
    bool has_video_;
    SrsVideoCodecId vcodec_;
    // profile_idc, ISO_IEC_14496-10-AVC-2003.pdf, page 45.
    SrsAvcProfile avc_profile_;
    // level_idc, ISO_IEC_14496-10-AVC-2003.pdf, page 45.
    SrsAvcLevel avc_level_;
    // profile_idc, ITU-T-H.265-2021.pdf, page 62.
    SrsHevcProfile hevc_profile_;
    // level_idc, ITU-T-H.265-2021.pdf, page 63.
    SrsHevcLevel hevc_level_;
    int width_;
    int height_;

public:
    bool has_audio_;
    SrsAudioCodecId acodec_;
    SrsAudioSampleRate asample_rate_;
    SrsAudioChannels asound_type_;
    // audioObjectType, 1.6.2.1 AudioSpecificConfig, page 33, and
    // 1.5.1.1 Audio object type definition, page 23, ISO_IEC_14496-3-AAC-2001.pdf.
    SrsAacObjectType aac_object_;

public:
    SrsStatisticStream();
    void assemble(); // Not in the constructor, to avoid calling functions there.
    virtual ~SrsStatisticStream();

public:
    virtual srs_error_t dumps(SrsJsonObject *obj);

public:
    virtual void publish(std::string id);
    virtual void close();
};

struct SrsStatisticClient {
public:
    ISrsClock *clk_;

public:
    // The HTTP API kicks off this connection by expiring it.
    ISrsExpire *conn_;

public:
    SrsStatisticStream *stream_;
    ISrsRequest *req_;
    SrsRtmpConnType type_;
    std::string id_;
    srs_utime_t create_;

public:
    SrsKbps *kbps_;

public:
    SrsStatisticClient();
    void assemble(); // Not in the constructor, to avoid calling functions there.
    virtual ~SrsStatisticClient();

public:
    virtual srs_error_t dumps(SrsJsonObject *obj);
};

enum SrsForwardState {
    // Connecting and publishing to the destination, at start or on a retry.
    SrsForwardStateConnecting = 0,
    // Published to the destination, and sending the stream.
    SrsForwardStateForwarding,
    // Failed, and waiting to retry.
    SrsForwardStateRetrying,
};

extern std::string srs_forward_state_string(SrsForwardState state);

struct SrsStatisticForward {
public:
    std::string id_;
    std::string stream_id_;
    std::string vhost_id_;
    std::string app_;
    std::string name_;
    // The full destination url, with the stream key and query.
    std::string url_;
    SrsForwardState state_;
    srs_utime_t state_at_;

public:
    SrsStatisticForward();
    virtual ~SrsStatisticForward();

public:
    virtual srs_error_t dumps(SrsJsonObject *obj);
};

class ISrsStatistic
{
public:
    ISrsStatistic();
    virtual ~ISrsStatistic();

public:
    virtual void on_disconnect(std::string id, srs_error_t err) = 0;
    virtual srs_error_t on_client(std::string id, ISrsRequest *req, ISrsExpire *conn, SrsRtmpConnType type) = 0;
    virtual srs_error_t on_video_info(ISrsRequest *req, SrsVideoCodecId vcodec, int avc_profile, int avc_level, int width, int height) = 0;
    virtual srs_error_t on_audio_info(ISrsRequest *req, SrsAudioCodecId acodec, SrsAudioSampleRate asample_rate,
                                      SrsAudioChannels asound_type, SrsAacObjectType aac_object) = 0;
    virtual void on_stream_publish(ISrsRequest *req, std::string publisher_id) = 0;
    virtual void on_stream_close(ISrsRequest *req) = 0;
    virtual void kbps_add_delta(std::string id, ISrsKbpsDelta *delta) = 0;
    virtual void kbps_sample() = 0;
    virtual srs_error_t on_video_frames(ISrsRequest *req, int nb_frames) = 0;
    virtual srs_error_t on_audio_frames(ISrsRequest *req, int nb_frames) = 0;
    virtual void on_forward_start(std::string id, ISrsRequest *req, std::string url) = 0;
    virtual void on_forward_state(std::string id, SrsForwardState state) = 0;
    virtual void on_forward_stop(std::string id) = 0;

public:
    virtual std::string server_id() = 0;
    // Identifies a restart of the service.
    virtual std::string service_id() = 0;
    virtual std::string service_pid() = 0;
    virtual SrsStatisticVhost *find_vhost_by_id(std::string vid) = 0;
    virtual SrsStatisticStream *find_stream(std::string sid) = 0;
    virtual SrsStatisticStream *find_stream_by_url(std::string url) = 0;
    virtual SrsStatisticClient *find_client(std::string client_id) = 0;
    virtual SrsStatisticForward *find_forward(std::string forward_id) = 0;
    virtual srs_error_t dumps_vhosts(SrsJsonArray *arr) = 0;
    virtual srs_error_t dumps_streams(SrsJsonArray *arr, int start, int count) = 0;
    virtual srs_error_t dumps_clients(SrsJsonArray *arr, int start, int count) = 0;
    virtual srs_error_t dumps_forwards(SrsJsonArray *arr, std::string stream_id, int start, int count) = 0;
    virtual int count_forwards(std::string stream_id) = 0;
    virtual srs_error_t dumps_metrics(int64_t &send_bytes, int64_t &recv_bytes, int64_t &nstreams, int64_t &nclients, int64_t &total_nclients, int64_t &nerrs) = 0;
};

class SrsStatistic : public ISrsStatistic
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

    std::string server_id_;
    std::string service_id_;
    ISrsRand *rand_;
    std::string service_pid_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Key: vhost id.
    std::map<std::string, SrsStatisticVhost *>
        vhosts_;
    // Key: vhost name, a fast index of the same vhosts.
    std::map<std::string, SrsStatisticVhost *> rvhosts_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Key: stream id.
    std::map<std::string, SrsStatisticStream *>
        streams_;
    // Key: stream url, a fast index of the same streams.
    std::map<std::string, SrsStatisticStream *> rstreams_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Key: client id.
    std::map<std::string, SrsStatisticClient *>
        clients_;
    SrsKbps *kbps_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Key: forward id.
    std::map<std::string, SrsStatisticForward *> forwards_;
    ISrsClock *clk_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The total of client connections since start.
    int64_t nb_clients_;
    int64_t nb_errs_;

public:
    SrsStatistic();
    virtual ~SrsStatistic();

public:
    virtual SrsStatisticVhost *find_vhost_by_id(std::string vid);
    virtual SrsStatisticVhost *find_vhost_by_name(std::string name);
    virtual SrsStatisticStream *find_stream(std::string sid);
    virtual SrsStatisticStream *find_stream_by_url(std::string url);
    virtual SrsStatisticClient *find_client(std::string client_id);
    virtual SrsStatisticForward *find_forward(std::string forward_id);

public:
    virtual srs_error_t on_video_info(ISrsRequest *req, SrsVideoCodecId vcodec, int avc_profile, int avc_level, int width, int height);
    virtual srs_error_t on_audio_info(ISrsRequest *req, SrsAudioCodecId acodec, SrsAudioSampleRate asample_rate,
                                      SrsAudioChannels asound_type, SrsAacObjectType aac_object);
    virtual srs_error_t on_video_frames(ISrsRequest *req, int nb_frames);
    virtual srs_error_t on_audio_frames(ISrsRequest *req, int nb_frames);
    virtual void on_stream_publish(ISrsRequest *req, std::string publisher_id);
    virtual void on_stream_close(ISrsRequest *req);

public:
    virtual srs_error_t on_client(std::string id, ISrsRequest *req, ISrsExpire *conn, SrsRtmpConnType type);
    // Called for every connection, but on_client only once the request is parsed, so the client may
    // be missing.
    virtual void on_disconnect(std::string id, srs_error_t err);

public:
    virtual void on_forward_start(std::string id, ISrsRequest *req, std::string url);
    virtual void on_forward_state(std::string id, SrsForwardState state);
    virtual void on_forward_stop(std::string id);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    void cleanup_stream(SrsStatisticStream *stream);
    void remove_forwards_of(SrsStatisticStream *stream);

public:
    virtual void kbps_add_delta(std::string id, ISrsKbpsDelta *delta);
    virtual void kbps_sample();

public:
    virtual std::string server_id();
    virtual std::string service_id();
    virtual std::string service_pid();
    virtual srs_error_t dumps_vhosts(SrsJsonArray *arr);
    virtual srs_error_t dumps_streams(SrsJsonArray *arr, int start, int count);
    virtual srs_error_t dumps_clients(SrsJsonArray *arr, int start, int count);
    virtual srs_error_t dumps_forwards(SrsJsonArray *arr, std::string stream_id, int start, int count);
    virtual int count_forwards(std::string stream_id);
    void dumps_hints_kv(std::stringstream &ss);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual SrsStatisticVhost *create_vhost(ISrsRequest *req);
    virtual SrsStatisticStream *create_stream(SrsStatisticVhost *vhost, ISrsRequest *req);

public:
    virtual srs_error_t dumps_metrics(int64_t &send_bytes, int64_t &recv_bytes, int64_t &nstreams, int64_t &nclients, int64_t &total_nclients, int64_t &nerrs);
};

extern std::string srs_generate_stat_vid();
extern std::string srs_generate_stat_vid(ISrsRand *rand);

extern SrsStatistic *_srs_stat;

#endif
