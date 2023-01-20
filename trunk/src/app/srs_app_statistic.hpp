//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_STATISTIC_HPP
#define SRS_APP_STATISTIC_HPP

#include <srs_core.hpp>

#include <map>
#include <string>
#include <vector>
#include <sstream>

#include <srs_kernel_codec.hpp>
#include <srs_protocol_rtmp_stack.hpp>

class SrsKbps;
class SrsWallClock;
class SrsRequest;
class ISrsExpire;
class SrsJsonObject;
class SrsJsonArray;
class ISrsKbpsDelta;
class SrsClsSugar;
class SrsClsSugars;
class SrsPps;

struct SrsStatisticVhost
{
public:
    std::string id;
    std::string vhost;
    int nb_streams;
    int nb_clients;
public:
    // The vhost total kbps.
    SrsKbps* kbps;
public:
    SrsStatisticVhost();
    virtual ~SrsStatisticVhost();
public:
    virtual srs_error_t dumps(SrsJsonObject* obj);
};

struct SrsStatisticStream
{
public:
    std::string id;
    SrsStatisticVhost* vhost;
    std::string app;
    std::string stream;
    std::string url;
    std::string tcUrl;
    bool active;
    // The publisher connection id.
    std::string publisher_id;
    int nb_clients;
public:
    // The stream total kbps.
    SrsKbps* kbps;
    // The fps of stream.
    SrsPps* frames;
public:
    bool has_video;
    SrsVideoCodecId vcodec;
    // The profile_idc, ISO_IEC_14496-10-AVC-2003.pdf, page 45.
    SrsAvcProfile avc_profile;
    // The level_idc, ISO_IEC_14496-10-AVC-2003.pdf, page 45.
    SrsAvcLevel avc_level;
#ifdef SRS_H265
    // The profile_idc, ITU-T-H.265-2021.pdf, page 62.
    SrsHevcProfile hevc_profile;
    // The level_idc, ITU-T-H.265-2021.pdf, page 63.
    SrsHevcLevel hevc_level;
#endif
    // The width and height in codec info.
    int width;
    int height;
public:
    bool has_audio;
    SrsAudioCodecId acodec;
    SrsAudioSampleRate asample_rate;
    SrsAudioChannels asound_type;
    // The audio specified
    // audioObjectType, in 1.6.2.1 AudioSpecificConfig, page 33,
    // 1.5.1.1 Audio object type definition, page 23,
    //           in ISO_IEC_14496-3-AAC-2001.pdf.
    SrsAacObjectType aac_object;
public:
    SrsStatisticStream();
    virtual ~SrsStatisticStream();
public:
    virtual srs_error_t dumps(SrsJsonObject* obj);
public:
    // Publish the stream, id is the publisher.
    virtual void publish(std::string id);
    // Close the stream.
    virtual void close();
};

struct SrsStatisticClient
{
public:
    // For HTTP-API to kickoff this connection by expiring it.
    ISrsExpire* conn;
public:
    SrsStatisticStream* stream;
    SrsRequest* req;
    SrsRtmpConnType type;
    std::string id;
    srs_utime_t create;
public:
    // The stream total kbps.
    SrsKbps* kbps;
public:
    SrsStatisticClient();
    virtual ~SrsStatisticClient();
public:
    virtual srs_error_t dumps(SrsJsonObject* obj);
};

class SrsStatistic
{
private:
    static SrsStatistic *_instance;
    // The id to identify the sever.
    std::string server_id_;
    // The id to identify the service.
    std::string service_id_;
    // The pid to identify the service process.
    std::string service_pid_;
private:
    // The key: vhost id, value: vhost object.
    std::map<std::string, SrsStatisticVhost*> vhosts;
    // The key: vhost url, value: vhost Object.
    // @remark a fast index for vhosts.
    std::map<std::string, SrsStatisticVhost*> rvhosts;
private:
    // The key: stream id, value: stream Object.
    std::map<std::string, SrsStatisticStream*> streams;
    // The key: stream url, value: stream Object.
    // @remark a fast index for streams.
    std::map<std::string, SrsStatisticStream*> rstreams;
private:
    // The key: client id, value: stream object.
    std::map<std::string, SrsStatisticClient*> clients;
    // The server total kbps.
    SrsKbps* kbps;
private:
    // The total of clients connections.
    int64_t nb_clients_;
    // The total of clients errors.
    int64_t nb_errs_;
private:
    SrsStatistic();
    virtual ~SrsStatistic();
public:
    static SrsStatistic* instance();
public:
    virtual SrsStatisticVhost* find_vhost_by_id(std::string vid);
    virtual SrsStatisticVhost* find_vhost_by_name(std::string name);
    virtual SrsStatisticStream* find_stream(std::string sid);
    virtual SrsStatisticStream* find_stream_by_url(std::string url);
    virtual SrsStatisticClient* find_client(std::string client_id);
public:
    // When got video info for stream.
    virtual srs_error_t on_video_info(SrsRequest* req, SrsVideoCodecId vcodec, int avc_profile, int avc_level, int width, int height);
    // When got audio info for stream.
    virtual srs_error_t on_audio_info(SrsRequest* req, SrsAudioCodecId acodec, SrsAudioSampleRate asample_rate,
        SrsAudioChannels asound_type, SrsAacObjectType aac_object);
    // When got videos, update the frames.
    // We only stat the total number of video frames.
    virtual srs_error_t on_video_frames(SrsRequest* req, int nb_frames);
    // When publish stream.
    // @param req the request object of publish connection.
    // @param publisher_id The id of publish connection.
    virtual void on_stream_publish(SrsRequest* req, std::string publisher_id);
    // When close stream.
    virtual void on_stream_close(SrsRequest* req);
public:
    // When got a client to publish/play stream,
    // @param id, the client srs id.
    // @param req, the client request object.
    // @param conn, the physical absract connection object.
    // @param type, the type of connection.
    virtual srs_error_t on_client(std::string id, SrsRequest* req, ISrsExpire* conn, SrsRtmpConnType type);
    // Client disconnect
    // @remark the on_disconnect always call, while the on_client is call when
    //      only got the request object, so the client specified by id maybe not
    //      exists in stat.
    virtual void on_disconnect(std::string id, srs_error_t err);
private:
    // Cleanup the stream if stream is not active and for the last client.
    void cleanup_stream(SrsStatisticStream* stream);
public:
    // Sample the kbps, add delta bytes of conn.
    // Use kbps_sample() to get all result of kbps stat.
    virtual void kbps_add_delta(std::string id, ISrsKbpsDelta* delta);
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
    virtual srs_error_t dumps_vhosts(SrsJsonArray* arr);
    // Dumps the streams to amf0 array.
    // @param start the start index, from 0.
    // @param count the max count of streams to dump.
    virtual srs_error_t dumps_streams(SrsJsonArray* arr, int start, int count);
    // Dumps the clients to amf0 array
    // @param start the start index, from 0.
    // @param count the max count of clients to dump.
    virtual srs_error_t dumps_clients(SrsJsonArray* arr, int start, int count);
    // Dumps the hints about SRS server.
    void dumps_hints_kv(std::stringstream & ss);
#ifdef SRS_APM
public:
    // Dumps the CLS summary.
    void dumps_cls_summaries(SrsClsSugar* sugar);
    void dumps_cls_streams(SrsClsSugars* sugars);
#endif
private:
    virtual SrsStatisticVhost* create_vhost(SrsRequest* req);
    virtual SrsStatisticStream* create_stream(SrsStatisticVhost* vhost, SrsRequest* req);
public:
    // Dumps exporter metrics.
    virtual srs_error_t dumps_metrics(int64_t& send_bytes, int64_t& recv_bytes, int64_t& nstreams, int64_t& nclients, int64_t& total_nclients, int64_t& nerrs);
};

// Generate a random string id, with constant prefix.
extern std::string srs_generate_stat_vid();

#endif
