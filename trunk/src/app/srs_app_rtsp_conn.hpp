//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RTSP_CONN_HPP
#define SRS_APP_RTSP_CONN_HPP

#include <srs_core.hpp>
#include <srs_app_listener.hpp>
#include <srs_protocol_st.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_app_hybrid.hpp>
#include <srs_app_hourglass.hpp>
#include <srs_app_rtc_sdp.hpp>
#include <srs_app_reload.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_rtc_rtcp.hpp>
#include <srs_app_rtc_queue.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_app_conn.hpp>
#include <srs_app_async_call.hpp>
#include <srs_core_autofree.hpp>

#include <string>
#include <map>
#include <vector>
#include <sys/socket.h>

class SrsUdpMuxSocket;
class SrsLiveConsumer;
class SrsStunPacket;
class SrsRtcServer;
class SrsRtcConnection;
class SrsSharedPtrMessage;
class SrsRtcSource;
class SrsRtpPacket;
class ISrsCodec;
class SrsRtpNackForReceiver;
class SrsRtpIncommingVideoFrame;
class SrsRtpRingBuffer;
class SrsRtcConsumer;
class SrsRtspAudioSendTrack;
class SrsRtspVideoSendTrack;
class SrsErrorPithyPrint;
class SrsPithyPrint;
class SrsStatistic;
class SrsRtcUserConfig;
class SrsRtspSendTrack;
class SrsRtcPublishStream;
class SrsEphemeralDelta;
class SrsRtcNetworks;
class SrsRtcUdpNetwork;
class ISrsRtcNetwork;
class SrsRtcTcpNetwork;
class SrsRtspConnection;
class SrsRtcConnection2;

// A RTSP play stream, client pull and play stream from SRS.
class SrsRtspPlayStream : public ISrsCoroutineHandler, public ISrsRtcSourceChangeCallback
{
private:
    SrsContextId cid_;
    SrsFastCoroutine* trd_;
    SrsRtcConnection2* session_;
private:
    SrsRequest* req_;
    SrsSharedPtr<SrsRtspSource> source_;
    // key: publish_ssrc, value: send track to process rtp/rtcp
    std::map<uint32_t, SrsRtspAudioSendTrack*> audio_tracks_;
    std::map<uint32_t, SrsRtspVideoSendTrack*> video_tracks_;
private:
    // Fast cache for tracks.
    uint32_t cache_ssrc0_;
    uint32_t cache_ssrc1_;
    uint32_t cache_ssrc2_;
    SrsRtspSendTrack* cache_track0_;
    SrsRtspSendTrack* cache_track1_;
    SrsRtspSendTrack* cache_track2_;
private:
    // Whether player started.
    bool is_started;
public:
    SrsRtspPlayStream(SrsRtcConnection2* s, const SrsContextId& cid);
    virtual ~SrsRtspPlayStream();
public:
    srs_error_t initialize(SrsRequest* request, std::map<uint32_t, SrsRtcTrackDescription*> sub_relations);
// Interface ISrsRtcSourceChangeCallback
public:
    void on_stream_change(SrsRtcSourceDescription* desc);
public:
    virtual const SrsContextId& context_id();
public:
    virtual srs_error_t start();
    virtual void stop();
public:
    virtual srs_error_t cycle();
private:
    srs_error_t send_packet(SrsRtpPacket*& pkt);
public:
    // Directly set the status of track, generally for init to set the default value.
    void set_all_tracks_status(bool status);
};

// A RTC Peer Connection, SDP level object.
//
// For performance, we use non-public from resource,
// see https://stackoverflow.com/questions/3747066/c-cannot-convert-from-base-a-to-derived-type-b-via-virtual-base-a
class SrsRtcConnection2 : public ISrsResource, public ISrsDisposingHandler, public ISrsExpire
{
    friend class SrsRtcPlayStream;
public:
    bool disposing_;
private:
    // TODO: FIXME: Rename it.
    // The timeout of session, keep alive by STUN ping pong.
    srs_utime_t session_timeout;
    // TODO: FIXME: Rename it.
    srs_utime_t last_stun_time;
private:
    // For each RTC session, we use a specified cid for debugging logs.
    SrsContextId cid_;
public:
    SrsRtcConnection2(const SrsContextId& cid);
    virtual ~SrsRtcConnection2();
// interface ISrsDisposingHandler
public:
    virtual void on_before_dispose(ISrsResource* c);
    virtual void on_disposing(ISrsResource* c);
// Interface ISrsResource.
public:
    virtual const SrsContextId& get_id();
    virtual std::string desc();
// Interface ISrsExpire.
public:
    virtual void expire();
public:
    void switch_to_context();
    const SrsContextId& context_id();
public:
    bool is_alive();
    void alive();
public:
    virtual srs_error_t do_send_packet(SrsRtpPacket* pkt) = 0;
};

#endif

