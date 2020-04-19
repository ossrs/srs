/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRS_APP_STATISTIC_HPP
#define SRS_APP_STATISTIC_HPP

#include <srs_core.hpp>

#include <map>
#include <string>
#include <vector>

#include <srs_kernel_codec.hpp>
#include <srs_rtmp_stack.hpp>

class SrsKbps;
class SrsWallClock;
class SrsRequest;
class SrsConnection;
class SrsJsonObject;
class SrsJsonArray;

struct SrsStatisticVhost
{
public:
    int64_t id;
    std::string vhost;
    int nb_streams;
    int nb_clients;
public:
    // The vhost total kbps.
    SrsKbps* kbps;
    SrsWallClock* clk;
public:
    SrsStatisticVhost();
    virtual ~SrsStatisticVhost();
public:
    virtual srs_error_t dumps(SrsJsonObject* obj);
};

struct SrsStatisticStream
{
public:
    int64_t id;
    SrsStatisticVhost* vhost;
    std::string app;
    std::string stream;
    std::string url;
    bool active;
    int connection_cid;
    int nb_clients;
    uint64_t nb_frames;
public:
    // The stream total kbps.
    SrsKbps* kbps;
    SrsWallClock* clk;
public:
    bool has_video;
    SrsVideoCodecId vcodec;
    // The profile_idc, ISO_IEC_14496-10-AVC-2003.pdf, page 45.
    SrsAvcProfile avc_profile;
    // The level_idc, ISO_IEC_14496-10-AVC-2003.pdf, page 45.
    SrsAvcLevel avc_level;
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
    // Publish the stream.
    virtual void publish(int cid);
    // Close the stream.
    virtual void close();
};

struct SrsStatisticClient
{
public:
    SrsStatisticStream* stream;
    SrsConnection* conn;
    SrsRequest* req;
    SrsRtmpConnType type;
    int id;
    srs_utime_t create;
public:
    SrsStatisticClient();
    virtual ~SrsStatisticClient();
public:
    virtual srs_error_t dumps(SrsJsonObject* obj);
};

class SrsStatisticCategory
{
public:
    uint64_t nn;
public:
    uint64_t a;
    uint64_t b;
    uint64_t c;
    uint64_t d;
    uint64_t e;
public:
    uint64_t f;
    uint64_t g;
    uint64_t h;
    uint64_t i;
    uint64_t j;
public:
    SrsStatisticCategory();
    virtual ~SrsStatisticCategory();
};

class SrsStatistic : public ISrsProtocolPerf
{
private:
    static SrsStatistic *_instance;
    // The id to identify the sever.
    int64_t _server_id;
private:
    // The key: vhost id, value: vhost object.
    std::map<int64_t, SrsStatisticVhost*> vhosts;
    // The key: vhost url, value: vhost Object.
    // @remark a fast index for vhosts.
    std::map<std::string, SrsStatisticVhost*> rvhosts;
private:
    // The key: stream id, value: stream Object.
    std::map<int64_t, SrsStatisticStream*> streams;
    // The key: stream url, value: stream Object.
    // @remark a fast index for streams.
    std::map<std::string, SrsStatisticStream*> rstreams;
private:
    // The key: client id, value: stream object.
    std::map<int, SrsStatisticClient*> clients;
    // The server total kbps.
    SrsKbps* kbps;
    SrsWallClock* clk;
    // The perf stat for mw(merged write).
    SrsStatisticCategory* perf_iovs;
    SrsStatisticCategory* perf_msgs;
    SrsStatisticCategory* perf_sendmmsg;
    SrsStatisticCategory* perf_gso;
    SrsStatisticCategory* perf_rtp;
    SrsStatisticCategory* perf_rtc;
    SrsStatisticCategory* perf_bytes;
    SrsStatisticCategory* perf_dropped;
private:
    SrsStatistic();
    virtual ~SrsStatistic();
public:
    static SrsStatistic* instance();
public:
    virtual SrsStatisticVhost* find_vhost(int vid);
    virtual SrsStatisticVhost* find_vhost(std::string name);
    virtual SrsStatisticStream* find_stream(int sid);
    virtual SrsStatisticClient* find_client(int cid);
public:
    // When got video info for stream.
    virtual srs_error_t on_video_info(SrsRequest* req, SrsVideoCodecId vcodec, SrsAvcProfile avc_profile,
        SrsAvcLevel avc_level, int width, int height);
    // When got audio info for stream.
    virtual srs_error_t on_audio_info(SrsRequest* req, SrsAudioCodecId acodec, SrsAudioSampleRate asample_rate,
        SrsAudioChannels asound_type, SrsAacObjectType aac_object);
    // When got videos, update the frames.
    // We only stat the total number of video frames.
    virtual srs_error_t on_video_frames(SrsRequest* req, int nb_frames);
    // When publish stream.
    // @param req the request object of publish connection.
    // @param cid the cid of publish connection.
    virtual void on_stream_publish(SrsRequest* req, int cid);
    // When close stream.
    virtual void on_stream_close(SrsRequest* req);
public:
    // When got a client to publish/play stream,
    // @param id, the client srs id.
    // @param req, the client request object.
    // @param conn, the physical absract connection object.
    // @param type, the type of connection.
    virtual srs_error_t on_client(int id, SrsRequest* req, SrsConnection* conn, SrsRtmpConnType type);
    // Client disconnect
    // @remark the on_disconnect always call, while the on_client is call when
    //      only got the request object, so the client specified by id maybe not
    //      exists in stat.
    virtual void on_disconnect(int id);
    // Sample the kbps, add delta bytes of conn.
    // Use kbps_sample() to get all result of kbps stat.
    // TODO: FIXME: the add delta must use ISrsKbpsDelta interface instead.
    virtual void kbps_add_delta(SrsConnection* conn);
    // Calc the result for all kbps.
    // @return the server kbps.
    virtual SrsKbps* kbps_sample();
public:
    // Get the server id, used to identify the server.
    // For example, when restart, the server id must changed.
    virtual int64_t server_id();
    // Dumps the vhosts to amf0 array.
    virtual srs_error_t dumps_vhosts(SrsJsonArray* arr);
    // Dumps the streams to amf0 array.
    virtual srs_error_t dumps_streams(SrsJsonArray* arr);
    // Dumps the clients to amf0 array
    // @param start the start index, from 0.
    // @param count the max count of clients to dump.
    virtual srs_error_t dumps_clients(SrsJsonArray* arr, int start, int count);
public:
    // Stat for packets merged written, nb_msgs is the number of RTMP messages.
    // For example, publish by FFMPEG, Audio and Video frames.
    virtual void perf_on_msgs(int nb_msgs);
    virtual srs_error_t dumps_perf_msgs(SrsJsonObject* obj);
public:
    // Stat for packets merged written, nb_packets is the number of RTC packets.
    // For example, a RTMP/AAC audio packet maybe transcoded to two RTC/opus packets.
    virtual void perf_on_rtc_packets(int nb_packets);
    virtual srs_error_t dumps_perf_rtc_packets(SrsJsonObject* obj);
public:
    // Stat for packets merged written, nb_packets is the number of RTP packets.
    // For example, a RTC/opus packet maybe package to three RTP packets.
    virtual void perf_on_rtp_packets(int nb_packets);
    virtual srs_error_t dumps_perf_rtp_packets(SrsJsonObject* obj);
public:
    // Stat for packets UDP GSO, nb_packets is the merged RTP packets.
    // For example, three RTP/audio packets maybe GSO to one msghdr.
    virtual void perf_on_gso_packets(int nb_packets);
    virtual srs_error_t dumps_perf_gso(SrsJsonObject* obj);
public:
    // Stat for TCP writev, nb_iovs is the total number of iovec.
    virtual void perf_on_writev_iovs(int nb_iovs);
    virtual srs_error_t dumps_perf_writev_iovs(SrsJsonObject* obj);
public:
    // Stat for packets UDP sendmmsg, nb_packets is the vlen for sendmmsg.
    virtual void perf_on_sendmmsg_packets(int nb_packets);
    virtual srs_error_t dumps_perf_sendmmsg(SrsJsonObject* obj);
public:
    // Stat for bytes, nn_bytes is the size of bytes, nb_padding is padding bytes.
    virtual void perf_on_rtc_bytes(int nn_bytes, int nn_rtp_bytes, int nn_padding);
    virtual srs_error_t dumps_perf_bytes(SrsJsonObject* obj);
public:
    // Stat for rtc messages, nn_rtc is rtc messages, nn_dropped is dropped messages.
    virtual void perf_on_dropped(int nn_msgs, int nn_rtc, int nn_dropped);
    virtual srs_error_t dumps_perf_dropped(SrsJsonObject* obj);
public:
    // Reset all perf stat data.
    virtual void reset_perf();
private:
    virtual void perf_on_packets(SrsStatisticCategory* p, int nb_msgs);
    virtual srs_error_t dumps_perf(SrsStatisticCategory* p, SrsJsonObject* obj);
private:
    virtual SrsStatisticVhost* create_vhost(SrsRequest* req);
    virtual SrsStatisticStream* create_stream(SrsStatisticVhost* vhost, SrsRequest* req);
};

#endif
