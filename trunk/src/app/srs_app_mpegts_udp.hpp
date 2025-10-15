//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_MPEGTS_UDP_HPP
#define SRS_APP_MPEGTS_UDP_HPP

#include <srs_core.hpp>

struct sockaddr;
#include <map>
#include <string>

class SrsBuffer;
class SrsTsContext;
class ISrsTsContext;
class SrsConfDirective;
class SrsSimpleStream;
class SrsRtmpClient;
class SrsStSocket;
class ISrsRequest;
class SrsRawH264Stream;
class ISrsRawH264Stream;
class SrsMediaPacket;
class SrsRawAacStream;
class ISrsRawAacStream;
struct SrsRawAacStreamCodec;
class SrsPithyPrint;
class ISrsPithyPrint;
class SrsSimpleRtmpClient;
class ISrsBasicRtmpClient;
class SrsMpegtsOverUdp;
class ISrsIpListener;
class ISrsMpegtsOverUdp;
class ISrsAppConfig;
class ISrsAppFactory;

#include <srs_app_listener.hpp>
#include <srs_app_st.hpp>
#include <srs_kernel_ts.hpp>

// A UDP listener, for udp stream caster server.
class SrsUdpCasterListener : public ISrsListener
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsIpListener *listener_;
    ISrsMpegtsOverUdp *caster_;

public:
    SrsUdpCasterListener();
    virtual ~SrsUdpCasterListener();

public:
    srs_error_t initialize(SrsConfDirective *conf);
    srs_error_t listen();
    void close();
};

// The interface for mpegts queue.
class ISrsMpegtsQueue
{
public:
    ISrsMpegtsQueue();
    virtual ~ISrsMpegtsQueue();

public:
    virtual srs_error_t push(SrsMediaPacket *msg) = 0;
    virtual SrsMediaPacket *dequeue() = 0;
};

// The queue for mpegts over udp to send packets.
// For the aac in mpegts contains many flv packets in a pes packet,
// we must recalc the timestamp.
class SrsMpegtsQueue : public ISrsMpegtsQueue
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The key: dts, value: msg.
    std::map<int64_t, SrsMediaPacket *>
        msgs_;
    int nb_audios_;
    int nb_videos_;

public:
    SrsMpegtsQueue();
    virtual ~SrsMpegtsQueue();

public:
    virtual srs_error_t push(SrsMediaPacket *msg);
    virtual SrsMediaPacket *dequeue();
};

// The interface for mpegts over udp.
class ISrsMpegtsOverUdp : public ISrsTsHandler, public ISrsUdpHandler
{
public:
    ISrsMpegtsOverUdp();
    virtual ~ISrsMpegtsOverUdp();

public:
    virtual srs_error_t initialize(SrsConfDirective *c) = 0;
};

// The mpegts over udp stream caster.
class SrsMpegtsOverUdp : public ISrsMpegtsOverUdp
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsTsContext *context_;
    SrsSimpleStream *buffer_;
    std::string output_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsBasicRtmpClient *sdk_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRawH264Stream *avc_;
    std::string h264_sps_;
    bool h264_sps_changed_;
    std::string h264_pps_;
    bool h264_pps_changed_;
    bool h264_sps_pps_sent_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRawAacStream *aac_;
    std::string aac_specific_config_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsMpegtsQueue *queue_;
    ISrsPithyPrint *pprint_;

public:
    SrsMpegtsOverUdp();
    virtual ~SrsMpegtsOverUdp();

public:
    srs_error_t initialize(SrsConfDirective *c);
    // Interface ISrsUdpHandler
public:
    virtual srs_error_t on_udp_packet(const sockaddr *from, const int fromlen, char *buf, int nb_buf);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t on_udp_bytes(std::string host, int port, char *buf, int nb_buf);
    // Interface ISrsTsHandler
public:
    virtual srs_error_t on_ts_message(SrsTsMessage *msg);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t on_ts_video(SrsTsMessage *msg, SrsBuffer *avs);
    virtual srs_error_t write_h264_sps_pps(uint32_t dts, uint32_t pts);
    virtual srs_error_t write_h264_ipb_frame(char *frame, int frame_size, uint32_t dts, uint32_t pts);
    virtual srs_error_t on_ts_audio(SrsTsMessage *msg, SrsBuffer *avs);
    virtual srs_error_t write_audio_raw_frame(char *frame, int frame_size, SrsRawAacStreamCodec *codec, uint32_t dts);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t rtmp_write_packet(char type, uint32_t timestamp, char *data, int size);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Connect to RTMP server.
    virtual srs_error_t
    connect();
    // Close the connection to RTMP server.
    virtual void close();
};

#endif
