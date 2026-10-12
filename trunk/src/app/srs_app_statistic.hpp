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
    // The vhost total kbps.
    SrsKbps *kbps_;

public:
    SrsStatisticVhost();
    void assemble(); // Construct object, to avoid call function in constructor.
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
    // The publisher connection id.
    std::string publisher_id_;
    int nb_clients_;
    // The time when this stream statistic was created.
    srs_utime_t create_;

public:
    // The stream total kbps.
    SrsKbps *kbps_;
    // The fps of stream (video frames/packets).
    SrsPps *video_frames_;
    // The fps of audio (audio frames/packets).
    SrsPps *audio_frames_;

public:
    bool has_video_;
    SrsVideoCodecId vcodec_;
    // The profile_idc, ISO_IEC_14496-10-AVC-2003.pdf, page 45.
    SrsAvcProfile avc_profile_;
    // The level_idc, ISO_IEC_14496-10-AVC-2003.pdf, page 45.
    SrsAvcLevel avc_level_;
    // The profile_idc, ITU-T-H.265-2021.pdf, page 62.
    SrsHevcProfile hevc_profile_;
    // The level_idc, ITU-T-H.265-2021.pdf, page 63.
    SrsHevcLevel hevc_level_;
    // The width and height in codec info.
    int width_;
    int height_;

public:
    bool has_audio_;
    SrsAudioCodecId acodec_;
    SrsAudioSampleRate asample_rate_;
    SrsAudioChannels asound_type_;
    // The audio specified
    // audioObjectType, in 1.6.2.1 AudioSpecificConfig, page 33,
    // 1.5.1.1 Audio object type definition, page 23,
    //           in ISO_IEC_14496-3-AAC-2001.pdf.
    SrsAacObjectType aac_object_;

public:
    SrsStatisticStream();
    void assemble(); // Construct object, to avoid call function in constructor.
    virtual ~SrsStatisticStream();

public:
    virtual srs_error_t dumps(SrsJsonObject *obj);

public:
    // Publish the stream, id is the publisher.
    virtual void publish(std::string id);
    // Close the stream.
    virtual void close();
};

// The SRT connection of a client, which provides its SRT transport stats for the HTTP API.
class ISrsStatisticSrt
{
public:
    ISrsStatisticSrt();
    virtual ~ISrsStatisticSrt();

public:
    // Dumps the SRT stats, such as RTT, loss and retransmits, to obj.
    virtual srs_error_t dumps_srt(SrsJsonObject *obj) = 0;
};

struct SrsStatisticClient {
public:
    ISrsClock *clk_;

public:
    // For HTTP-API to kickoff this connection by expiring it.
    ISrsExpire *conn_;
    // For HTTP-API to dump the SRT stats, NULL if not SRT.
    ISrsStatisticSrt *srt_;

public:
    SrsStatisticStream *stream_;
    ISrsRequest *req_;
    SrsRtmpConnType type_;
    std::string id_;
    srs_utime_t create_;

public:
    // The stream total kbps.
    SrsKbps *kbps_;

public:
    SrsStatisticClient();
    void assemble(); // Construct object, to avoid call function in constructor.
    virtual ~SrsStatisticClient();

public:
    virtual srs_error_t dumps(SrsJsonObject *obj);
};

// The state of a forwarder, which pushes a publishing stream to a destination.
enum SrsForwardState {
    // Connecting and publishing to the destination, at start or on a retry.
    SrsForwardStateConnecting = 0,
    // Published to the destination, and sending the stream.
    SrsForwardStateForwarding,
    // Failed, and waiting to retry.
    SrsForwardStateRetrying,
};

// The name of the forward state in the HTTP API, such as forwarding.
extern std::string srs_forward_state_string(SrsForwardState state);

struct SrsStatisticForward {
public:
    std::string id_;
    // The stream id, vhost id, app and stream name of the publishing stream.
    std::string stream_id_;
    std::string vhost_id_;
    std::string app_;
    std::string name_;
    // The full destination url, with the stream key and query.
    std::string url_;
    SrsForwardState state_;
    // The time when it entered the state.
    srs_utime_t state_at_;

public:
    SrsStatisticForward();
    virtual ~SrsStatisticForward();

public:
    virtual srs_error_t dumps(SrsJsonObject *obj);
};

// The interface for statistic.
class ISrsStatistic
{
public:
    ISrsStatistic();
    virtual ~ISrsStatistic();

public:
    virtual void on_disconnect(std::string id, srs_error_t err) = 0;
    virtual srs_error_t on_client(std::string id, ISrsRequest *req, ISrsExpire *conn, SrsRtmpConnType type) = 0;
    virtual void on_srt_stats(std::string id, ISrsStatisticSrt *srt) = 0;
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
    // Get the server id, used to identify the server.
    // For example, when restart, the server id must changed.
    virtual std::string server_id() = 0;
    // Get the service id, used to identify the restart of service.
    virtual std::string service_id() = 0;
    // Get the service pid, used to identify the service process.
    virtual std::string service_pid() = 0;
    // Find vhost by id.
    virtual SrsStatisticVhost *find_vhost_by_id(std::string vid) = 0;
    // Find stream by id.
    virtual SrsStatisticStream *find_stream(std::string sid) = 0;
    // Find stream by url.
    virtual SrsStatisticStream *find_stream_by_url(std::string url) = 0;
    // Find client by id.
    virtual SrsStatisticClient *find_client(std::string client_id) = 0;
    // Find forward by id.
    virtual SrsStatisticForward *find_forward(std::string forward_id) = 0;
    // Dumps the vhosts to json array.
    virtual srs_error_t dumps_vhosts(SrsJsonArray *arr) = 0;
    // Dumps the streams to json array.
    virtual srs_error_t dumps_streams(SrsJsonArray *arr, int start, int count) = 0;
    // Dumps the clients to json array.
    virtual srs_error_t dumps_clients(SrsJsonArray *arr, int start, int count) = 0;
    // Dumps the forwards to json array, only of the stream if stream_id is not empty.
    virtual srs_error_t dumps_forwards(SrsJsonArray *arr, std::string stream_id, int start, int count) = 0;
    // Count the forwards, only of the stream if stream_id is not empty.
    virtual int count_forwards(std::string stream_id) = 0;
    // Dumps exporter metrics.
    virtual srs_error_t dumps_metrics(int64_t &send_bytes, int64_t &recv_bytes, int64_t &nstreams, int64_t &nclients, int64_t &total_nclients, int64_t &nerrs) = 0;
};

// The global statistic instance.
class SrsStatistic : public ISrsStatistic
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

    // The id to identify the sever.
    std::string server_id_;
    // The id to identify the service.
    std::string service_id_;
    // The generator of the service id.
    ISrsRand *rand_;
    // The pid to identify the service process.
    std::string service_pid_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The key: vhost id, value: vhost object.
    std::map<std::string, SrsStatisticVhost *>
        vhosts_;
    // The key: vhost url, value: vhost Object.
    // @remark a fast index for vhosts.
    std::map<std::string, SrsStatisticVhost *> rvhosts_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The key: stream id, value: stream Object.
    std::map<std::string, SrsStatisticStream *>
        streams_;
    // The key: stream url, value: stream Object.
    // @remark a fast index for streams.
    std::map<std::string, SrsStatisticStream *> rstreams_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The key: client id, value: stream object.
    std::map<std::string, SrsStatisticClient *>
        clients_;
    // The server total kbps.
    SrsKbps *kbps_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The key: forward id, value: forward object.
    std::map<std::string, SrsStatisticForward *> forwards_;
    // The clock to stamp the time a forward enters a state.
    ISrsClock *clk_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The total of clients connections.
    int64_t nb_clients_;
    // The total of clients errors.
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
    // When got video info for stream.
    virtual srs_error_t on_video_info(ISrsRequest *req, SrsVideoCodecId vcodec, int avc_profile, int avc_level, int width, int height);
    // When got audio info for stream.
    virtual srs_error_t on_audio_info(ISrsRequest *req, SrsAudioCodecId acodec, SrsAudioSampleRate asample_rate,
                                      SrsAudioChannels asound_type, SrsAacObjectType aac_object);
    // When got videos, update the frames.
    // We only stat the total number of video frames.
    virtual srs_error_t on_video_frames(ISrsRequest *req, int nb_frames);
    // When got audios, update the audio frames.
    // We only stat the total number of audio frames.
    virtual srs_error_t on_audio_frames(ISrsRequest *req, int nb_frames);
    // When publish stream.
    // @param req the request object of publish connection.
    // @param publisher_id The id of publish connection.
    virtual void on_stream_publish(ISrsRequest *req, std::string publisher_id);
    // When close stream.
    virtual void on_stream_close(ISrsRequest *req);

public:
    // When got a client to publish/play stream,
    // @param id, the client srs id.
    // @param req, the client request object.
    // @param conn, the physical absract connection object.
    // @param type, the type of connection.
    virtual srs_error_t on_client(std::string id, ISrsRequest *req, ISrsExpire *conn, SrsRtmpConnType type);
    // When an SRT client is on, to dump the SRT stats of its connection in the HTTP API.
    // @param id, the client srs id, after on_client.
    // @param srt, the SRT connection, which must live until on_disconnect.
    virtual void on_srt_stats(std::string id, ISrsStatisticSrt *srt);
    // Client disconnect
    // @remark the on_disconnect always call, while the on_client is call when
    //      only got the request object, so the client specified by id maybe not
    //      exists in stat.
    virtual void on_disconnect(std::string id, srs_error_t err);

public:
    // When a forwarder of a publishing stream starts, in the connecting state.
    // @param id, the forward id.
    // @param req, the request object of the publishing stream.
    // @param url, the full destination url.
    virtual void on_forward_start(std::string id, ISrsRequest *req, std::string url);
    // When a forwarder enters a state.
    virtual void on_forward_state(std::string id, SrsForwardState state);
    // When a forwarder stops.
    virtual void on_forward_stop(std::string id);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Cleanup the stream if stream is not active and for the last client.
    void cleanup_stream(SrsStatisticStream *stream);
    // Remove every forward of the stream.
    void remove_forwards_of(SrsStatisticStream *stream);

public:
    // Sample the kbps, add delta bytes of conn.
    // Use kbps_sample() to get all result of kbps stat.
    virtual void kbps_add_delta(std::string id, ISrsKbpsDelta *delta);
    // Calc the result for all kbps.
    virtual void kbps_sample();

public:
    // Get the server id, used to identify the server.
    // For example, when restart, the server id must changed.
    virtual std::string server_id();
    // Get the service id, used to identify the restart of service.
    virtual std::string service_id();
    // Get the service pid, used to identify the service process.
    virtual std::string service_pid();
    // Dumps the vhosts to amf0 array.
    virtual srs_error_t dumps_vhosts(SrsJsonArray *arr);
    // Dumps the streams to amf0 array.
    // @param start the start index, from 0.
    // @param count the max count of streams to dump.
    virtual srs_error_t dumps_streams(SrsJsonArray *arr, int start, int count);
    // Dumps the clients to amf0 array
    // @param start the start index, from 0.
    // @param count the max count of clients to dump.
    virtual srs_error_t dumps_clients(SrsJsonArray *arr, int start, int count);
    // Dumps the forwards to amf0 array.
    // @param stream_id the stream id to filter by, or empty for all forwards.
    // @param start the start index, from 0, after the filter.
    // @param count the max count of forwards to dump.
    virtual srs_error_t dumps_forwards(SrsJsonArray *arr, std::string stream_id, int start, int count);
    // Count the forwards, the total of dumps_forwards() before start and count.
    // @param stream_id the stream id to filter by, or empty for all forwards.
    virtual int count_forwards(std::string stream_id);
    // Dumps the hints about SRS server.
    void dumps_hints_kv(std::stringstream &ss);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual SrsStatisticVhost *create_vhost(ISrsRequest *req);
    virtual SrsStatisticStream *create_stream(SrsStatisticVhost *vhost, ISrsRequest *req);

public:
    // Dumps exporter metrics.
    virtual srs_error_t dumps_metrics(int64_t &send_bytes, int64_t &recv_bytes, int64_t &nstreams, int64_t &nclients, int64_t &total_nclients, int64_t &nerrs);
};

// Generate a random string id, with constant prefix.
extern std::string srs_generate_stat_vid();
extern std::string srs_generate_stat_vid(ISrsRand *rand);

// Global statistic instance.
extern SrsStatistic *_srs_stat;

#endif
