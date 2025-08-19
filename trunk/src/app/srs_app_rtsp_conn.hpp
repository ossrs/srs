//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RTSP_CONN_HPP
#define SRS_APP_RTSP_CONN_HPP

#include <srs_app_conn.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_core.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_st.hpp>

#include <map>
#include <string>
#include <sys/socket.h>
#include <vector>

class SrsRequest;
class SrsRtpPacket;
class SrsRtspSource;
class SrsRtspAudioSendTrack;
class SrsRtspVideoSendTrack;
class SrsRtspSendTrack;
class SrsEphemeralDelta;
class SrsRtspConnection;
class SrsSecurity;
class SrsRtspRequest;
class SrsRtspStack;

// A RTSP play stream, client pull and play stream from SRS.
class SrsRtspPlayStream : public ISrsCoroutineHandler, public ISrsRtcSourceChangeCallback
{
private:
    SrsContextId cid_;
    SrsFastCoroutine *trd_;
    SrsRtspConnection *session_;

private:
    SrsRequest *req_;
    SrsSharedPtr<SrsRtspSource> source_;
    // key: publish_ssrc, value: send track to process rtp/rtcp
    std::map<uint32_t, SrsRtspAudioSendTrack *> audio_tracks_;
    std::map<uint32_t, SrsRtspVideoSendTrack *> video_tracks_;

private:
    // Fast cache for tracks.
    uint32_t cache_ssrc0_;
    uint32_t cache_ssrc1_;
    uint32_t cache_ssrc2_;
    SrsRtspSendTrack *cache_track0_;
    SrsRtspSendTrack *cache_track1_;
    SrsRtspSendTrack *cache_track2_;

private:
    // Whether player started.
    bool is_started;

public:
    SrsRtspPlayStream(SrsRtspConnection *s, const SrsContextId &cid);
    virtual ~SrsRtspPlayStream();

public:
    srs_error_t initialize(SrsRequest *request, std::map<uint32_t, SrsRtcTrackDescription *> sub_relations);
    // Interface ISrsRtcSourceChangeCallback
public:
    void on_stream_change(SrsRtcSourceDescription *desc);

public:
    virtual const SrsContextId &context_id();

public:
    virtual srs_error_t start();
    virtual void stop();

public:
    virtual srs_error_t cycle();

private:
    srs_error_t send_packet(SrsRtpPacket *&pkt);

public:
    // Directly set the status of track, generally for init to set the default value.
    void set_all_tracks_status(bool status);
};

class SrsRtspConnection : public ISrsResource, public ISrsDisposingHandler, public ISrsExpire, public ISrsCoroutineHandler, public ISrsStartable
{
private:
    bool disposing_;

private:
    // TODO: FIXME: Rename it.
    // The timeout of session, keep alive by STUN ping pong.
    srs_utime_t session_timeout;
    // TODO: FIXME: Rename it.
    srs_utime_t last_stun_time;
    SrsContextId cid_;
    SrsRequest *request_;
    // The manager object to manage the connection.
    ISrsResourceManager *manager_;
    // Each connection start a green thread,
    // when thread stop, the connection will be delete by server.
    SrsCoroutine *trd_;
    // The ip and port of client.
    std::string ip_;
    int port_;
    SrsRtspStack *rtsp_;
    std::string session_id_;
    SrsSharedPtr<SrsRtspSource> source_;
    SrsEphemeralDelta *delta_;
    ISrsProtocolReadWriter *skt_;
    SrsSecurity *security_;
    iovec *cache_iov_;
    SrsBuffer *cache_buffer_;
    // key: ssrc
    std::map<uint32_t, SrsRtcTrackDescription *> tracks_;
    // key: ssrc
    std::map<uint32_t, ISrsStreamWriter *> networks_;
    SrsRtspPlayStream *player_;

public:
    SrsRtspConnection(ISrsResourceManager *cm, ISrsProtocolReadWriter *skt, std::string cip, int port);
    virtual ~SrsRtspConnection();
    // interface ISrsDisposingHandler
public:
    virtual void on_before_dispose(ISrsResource *c);
    virtual void on_disposing(ISrsResource *c);

public:
    virtual srs_error_t do_send_packet(SrsRtpPacket *pkt);

public:
    ISrsKbpsDelta *delta();

private:
    virtual srs_error_t do_describe(SrsRtspRequest *req, std::string &sdp);
    virtual srs_error_t do_setup(SrsRtspRequest *req, uint32_t *ssrc);
    virtual srs_error_t do_play(SrsRtspRequest *req, SrsRtspConnection *conn);
    virtual srs_error_t do_teardown();
    // Interface ISrsResource.
public:
    virtual std::string desc();
    virtual const SrsContextId &get_id();
    // Interface ISrsConnection.
public:
    virtual std::string remote_ip();
    // Interface ISrsStartable
public:
    // Start the client green thread.
    // when server get a client from listener,
    // 1. server will create an concrete connection(for instance, RTMP connection),
    // 2. then add connection to its connection manager,
    // 3. start the client thread by invoke this start()
    // when client cycle thread stop, invoke the on_thread_stop(), which will use server
    // To remove the client by server->remove(this).
    virtual srs_error_t start();
    // Interface ISrsCoroutineHandler
public:
    virtual srs_error_t cycle();
    // Interface ISrsExpire.
public:
    virtual void expire();

public:
    void switch_to_context();
    const SrsContextId &context_id();

public:
    bool is_alive();
    void alive();

private:
    srs_error_t do_cycle();

private:
    srs_error_t http_hooks_on_play(SrsRequest *req);
    srs_error_t get_ssrc_by_stream_id(uint32_t stream_id, uint32_t *ssrc);
};

class SrsRtspTcpNetwork : public ISrsStreamWriter
{
private:
    ISrsProtocolReadWriter *skt_;
    int channel_;

public:
    SrsRtspTcpNetwork(ISrsProtocolReadWriter *skt, int ch);
    virtual ~SrsRtspTcpNetwork();
    // Interface ISrsStreamWriter.
public:
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite);
};

#endif
