//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_gb28181.hpp>

#include <srs_app_config.hpp>
#include <srs_app_http_api.hpp>
#include <srs_app_listener.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_server.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_kernel_ps.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_http_conn.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_raw_avc.hpp>
#include <srs_protocol_sdp.hpp>
#include <srs_protocol_utility.hpp>

#include <sstream>
using namespace std;

#define SRS_GB_MAX_RECOVER 16
#define SRS_GB_MAX_TIMEOUT 3
#define SRS_GB_LARGE_PACKET 1500
#define SRS_GB_SESSION_DRIVE_INTERVAL (300 * SRS_UTIME_MILLISECONDS)

extern bool srs_is_rtcp(const uint8_t *data, size_t len);

std::string srs_gb_session_state(SrsGbSessionState state)
{
    switch (state) {
    case SrsGbSessionStateInit:
        return "Init";
    case SrsGbSessionStateConnecting:
        return "Connecting";
    case SrsGbSessionStateEstablished:
        return "Established";
    default:
        return "Invalid";
    }
}

std::string srs_gb_state(SrsGbSessionState ostate, SrsGbSessionState state)
{
    return srs_fmt_sprintf("%s->%s", srs_gb_session_state(ostate).c_str(), srs_gb_session_state(state).c_str());
}

SrsGbSession::SrsGbSession() : media_(new SrsGbMediaTcpConn())
{
    wrapper_ = NULL;
    owner_coroutine_ = NULL;
    owner_cid_ = NULL;

    muxer_ = new SrsGbMuxer(this);
    state_ = SrsGbSessionStateInit;

    connecting_starttime_ = 0;
    nn_timeout_ = 0;
    reinviting_starttime_ = 0;

    ppp_ = new SrsAlonePithyPrint();
    startime_ = srs_time_now_realtime();
    total_packs_ = 0;
    total_msgs_ = 0;
    total_recovered_ = 0;
    total_msgs_dropped_ = 0;
    total_reserved_ = 0;

    media_id_ = 0;
    media_msgs_ = 0;
    media_packs_ = 0;
    media_starttime_ = startime_;
    media_recovered_ = 0;
    media_msgs_dropped_ = 0;
    media_reserved_ = 0;

    cid_ = _srs_context->generate_id();
    _srs_context->set_id(cid_); // Also change current coroutine cid as session's.
}

SrsGbSession::~SrsGbSession()
{
    srs_freep(muxer_);
    srs_freep(ppp_);
}

void SrsGbSession::setup(SrsConfDirective *conf)
{
    std::string output = _srs_config->get_stream_caster_output(conf);
    muxer_->setup(output);

    srs_trace("Session: Start output=%s", output.c_str());
}

void SrsGbSession::setup_owner(SrsSharedResource<SrsGbSession> *wrapper, ISrsInterruptable *owner_coroutine, ISrsContextIdSetter *owner_cid)
{
    wrapper_ = wrapper;
    owner_coroutine_ = owner_coroutine;
    owner_cid_ = owner_cid;
}

void SrsGbSession::on_executor_done(ISrsInterruptable *executor)
{
    owner_coroutine_ = NULL;
}

void SrsGbSession::on_ps_pack(SrsPackContext *ctx, SrsPsPacket *ps, const std::vector<SrsTsMessage *> &msgs)
{
    // Got a new context, that is new media transport.
    if (media_id_ != ctx->media_id_) {
        total_msgs_ += media_msgs_;
        total_packs_ += media_packs_;
        total_recovered_ += media_recovered_;
        total_msgs_dropped_ += media_msgs_dropped_;
        total_reserved_ += media_reserved_;

        media_msgs_ = media_packs_ = 0;
        media_recovered_ = media_msgs_dropped_ = 0;
        media_reserved_ = 0;
    }

    // Update data for current context.
    media_id_ = ctx->media_id_;
    media_packs_++;
    media_msgs_ += msgs.size();
    media_starttime_ = ctx->media_startime_;
    media_recovered_ = ctx->media_nn_recovered_;
    media_msgs_dropped_ = ctx->media_nn_msgs_dropped_;
    media_reserved_ = ctx->media_reserved_;

    // Group all video in pack to a video frame, because only allows one video for each PS pack.
    SrsUniquePtr<SrsTsMessage> video(new SrsTsMessage());

    for (vector<SrsTsMessage *>::const_iterator it = msgs.begin(); it != msgs.end(); ++it) {
        SrsTsMessage *msg = *it;

        // Group all videos to one video.
        if (msg->sid_ == SrsTsPESStreamIdVideoCommon) {
            video->ps_helper_ = msg->ps_helper_;
            video->dts_ = msg->dts_;
            video->pts_ = msg->pts_;
            video->sid_ = msg->sid_;
            video->payload_->append(msg->payload_);
            continue;
        }

        // Directly mux audio message.
        srs_error_t err = muxer_->on_ts_message(msg);
        if (err != srs_success) {
            srs_warn("Muxer: Ignore audio err %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
    }

    // Send the generated video message.
    if (video->payload_->length() > 0) {
        srs_error_t err = muxer_->on_ts_message(video.get());
        if (err != srs_success) {
            srs_warn("Muxer: Ignore video err %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
    }
}

void SrsGbSession::on_media_transport(SrsSharedResource<SrsGbMediaTcpConn> media)
{
    media_ = media;

    // Change id of SIP and all its child coroutines.
    media_->set_cid(cid_);
}

srs_error_t SrsGbSession::cycle()
{
    srs_error_t err = srs_success;

    // Update all context id to cid of session.
    _srs_context->set_id(cid_);
    owner_cid_->set_cid(cid_);

    media_->set_cid(cid_);

    // Drive the session cycle.
    err = do_cycle();

    // Interrupt the media transport when session terminated.
    media_->interrupt();

    // success.
    if (err == srs_success) {
        srs_trace("client finished.");
        return err;
    }

    // It maybe success with message.
    if (srs_error_code(err) == ERROR_SUCCESS) {
        srs_trace("client finished %s.", srs_error_summary(err).c_str());
        srs_freep(err);
        return err;
    }

    // client close peer.
    // TODO: FIXME: Only reset the error when client closed it.
    if (srs_is_client_gracefully_close(err)) {
        srs_warn("client disconnect peer. ret=%d", srs_error_code(err));
    } else if (srs_is_server_gracefully_close(err)) {
        srs_warn("server disconnect. ret=%d", srs_error_code(err));
    } else {
        srs_error("serve error %s", srs_error_desc(err).c_str());
    }

    srs_freep(err);
    return srs_success;
}

srs_error_t SrsGbSession::do_cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if (!owner_coroutine_)
            return err;
        if ((err = owner_coroutine_->pull()) != srs_success) {
            return srs_error_wrap(err, "pull");
        }

        // Drive the state in a fixed interval.
        srs_usleep(SRS_GB_SESSION_DRIVE_INTERVAL);

        // Regular state, driven by state of transport.
        if ((err = drive_state()) != srs_success) {
            return srs_error_wrap(err, "drive");
        }

        ppp_->elapse();
        if (ppp_->can_print()) {
            int alive = srsu2msi(srs_time_now_realtime() - startime_) / 1000;
            int pack_alive = srsu2msi(srs_time_now_realtime() - media_starttime_) / 1000;
            srs_trace("Session: Alive=%ds, packs=%" PRId64 ", recover=%" PRId64 ", reserved=%" PRId64 ", msgs=%" PRId64 ", drop=%" PRId64 ", media(id=%u, alive=%ds, packs=%" PRId64 " recover=%" PRId64 ", reserved=%" PRId64 ", msgs=%" PRId64 ", drop=%" PRId64 ")",
                      alive, (total_packs_ + media_packs_), (total_recovered_ + media_recovered_), (total_reserved_ + media_reserved_),
                      (total_msgs_ + media_msgs_), (total_msgs_dropped_ + media_msgs_dropped_), media_id_, pack_alive, media_packs_,
                      media_recovered_, media_reserved_, media_msgs_, media_msgs_dropped_);
        }
    }

    return err;
}

srs_error_t SrsGbSession::drive_state()
{
    srs_error_t err = srs_success;

#define SRS_GB_CHANGE_STATE_TO(state)                                        \
    {                                                                        \
        SrsGbSessionState ostate = set_state(state);                         \
        srs_trace("Session: Change device=%s, state=%s", device_id_.c_str(), \
                  srs_gb_state(ostate, state_).c_str());                     \
    }

    if (state_ == SrsGbSessionStateInit) {
        // With external SIP server, we assume media connection is handled externally
        if (media_->is_connected()) {
            SRS_GB_CHANGE_STATE_TO(SrsGbSessionStateEstablished);
        }
    }

    if (state_ == SrsGbSessionStateEstablished) {
        // Simple state management - just check if media is still connected
        if (!media_->is_connected()) {
            SRS_GB_CHANGE_STATE_TO(SrsGbSessionStateInit);
        }
    }

    return err;
}

SrsGbSessionState SrsGbSession::set_state(SrsGbSessionState v)
{
    SrsGbSessionState state = state_;
    state_ = v;
    return state;
}

const SrsContextId &SrsGbSession::get_id()
{
    return cid_;
}

std::string SrsGbSession::desc()
{
    return "GBS";
}

SrsGbListener::SrsGbListener()
{
    conf_ = NULL;
    media_listener_ = new SrsTcpListener(this);
}

SrsGbListener::~SrsGbListener()
{
    srs_freep(conf_);
    srs_freep(media_listener_);
}

srs_error_t SrsGbListener::initialize(SrsConfDirective *conf)
{
    srs_error_t err = srs_success;

    srs_freep(conf_);
    conf_ = conf->copy();

    string ip = srs_net_address_any();
    if (true) {
        int port = _srs_config->get_stream_caster_listen(conf);
        media_listener_->set_endpoint(ip, port)->set_label("GB-TCP");
    }

    return err;
}

srs_error_t SrsGbListener::listen()
{
    srs_error_t err = srs_success;

    if ((err = media_listener_->listen()) != srs_success) {
        return srs_error_wrap(err, "listen");
    }

    if ((err = listen_api()) != srs_success) {
        return srs_error_wrap(err, "listen api");
    }

    return err;
}

srs_error_t SrsGbListener::listen_api()
{
    srs_error_t err = srs_success;

    ISrsHttpServeMux *mux = _srs_server->api_server();
    if ((err = mux->handle("/gb/v1/publish/", new SrsGoApiGbPublish(conf_))) != srs_success) {
        return srs_error_wrap(err, "handle publish");
    }

    return err;
}

void SrsGbListener::close()
{
}

srs_error_t SrsGbListener::on_tcp_client(ISrsListener *listener, srs_netfd_t stfd)
{
    srs_error_t err = srs_success;

    // Handle TCP connections.
    if (listener == media_listener_) {
        SrsGbMediaTcpConn *raw_conn = new SrsGbMediaTcpConn();
        raw_conn->setup(stfd);

        SrsSharedResource<SrsGbMediaTcpConn> *conn = new SrsSharedResource<SrsGbMediaTcpConn>(raw_conn);
        _srs_gb_manager->add(conn, NULL);

        SrsExecutorCoroutine *executor = new SrsExecutorCoroutine(_srs_gb_manager, conn, raw_conn, raw_conn);
        raw_conn->setup_owner(conn, executor, executor);

        if ((err = executor->start()) != srs_success) {
            srs_freep(executor);
            return srs_error_wrap(err, "gb media");
        }
    } else {
        srs_warn("GB: Ignore TCP client");
        srs_close_stfd(stfd);
    }

    return err;
}

ISrsPsPackHandler::ISrsPsPackHandler()
{
}

ISrsPsPackHandler::~ISrsPsPackHandler()
{
}

SrsGbMediaTcpConn::SrsGbMediaTcpConn()
{
    pack_ = new SrsPackContext(this);
    buffer_ = new uint8_t[65535];
    conn_ = NULL;

    wrapper_ = NULL;
    owner_coroutine_ = NULL;
    owner_cid_ = NULL;
    cid_ = _srs_context->get_id();

    session_ = NULL;
    connected_ = false;
    nn_rtcp_ = 0;
}

SrsGbMediaTcpConn::~SrsGbMediaTcpConn()
{
    srs_freep(conn_);
    srs_freepa(buffer_);
    srs_freep(pack_);
}

void SrsGbMediaTcpConn::setup(srs_netfd_t stfd)
{
    srs_freep(conn_);
    conn_ = new SrsTcpConnection(stfd);
}

void SrsGbMediaTcpConn::setup_owner(SrsSharedResource<SrsGbMediaTcpConn> *wrapper, ISrsInterruptable *owner_coroutine, ISrsContextIdSetter *owner_cid)
{
    wrapper_ = wrapper;
    owner_coroutine_ = owner_coroutine;
    owner_cid_ = owner_cid;
}

void SrsGbMediaTcpConn::on_executor_done(ISrsInterruptable *executor)
{
    owner_coroutine_ = NULL;
}

bool SrsGbMediaTcpConn::is_connected()
{
    return connected_;
}

void SrsGbMediaTcpConn::interrupt()
{
    if (owner_coroutine_)
        owner_coroutine_->interrupt();
}

void SrsGbMediaTcpConn::set_cid(const SrsContextId &cid)
{
    if (owner_cid_)
        owner_cid_->set_cid(cid);
    cid_ = cid;
}

const SrsContextId &SrsGbMediaTcpConn::get_id()
{
    return cid_;
}

std::string SrsGbMediaTcpConn::desc()
{
    return "GB-Media-TCP";
}

srs_error_t SrsGbMediaTcpConn::cycle()
{
    srs_error_t err = do_cycle();

    // Should disconnect the TCP connection when stop cycle, especially when we stop first. In this situation, the
    // connection won't be closed because it's shared by other objects.
    srs_freep(conn_);

    // Change state to disconnected.
    connected_ = false;
    srs_trace("PS: Media disconnect, code=%d", srs_error_code(err));

    // success.
    if (err == srs_success) {
        srs_trace("client finished.");
        return err;
    }

    // It maybe success with message.
    if (srs_error_code(err) == ERROR_SUCCESS) {
        srs_trace("client finished%s.", srs_error_summary(err).c_str());
        srs_freep(err);
        return err;
    }

    // client close peer.
    // TODO: FIXME: Only reset the error when client closed it.
    if (srs_is_client_gracefully_close(err)) {
        srs_warn("client disconnect peer. ret=%d", srs_error_code(err));
    } else if (srs_is_server_gracefully_close(err)) {
        srs_warn("server disconnect. ret=%d", srs_error_code(err));
    } else {
        srs_error("serve error %s", srs_error_desc(err).c_str());
    }

    srs_freep(err);
    return srs_success;
}

srs_error_t SrsGbMediaTcpConn::do_cycle()
{
    srs_error_t err = srs_success;

    // The PS context to decode all PS packets.
    SrsRecoverablePsContext context;

    // If bytes is not enough(defined by SRS_PS_MIN_REQUIRED), ignore.
    context.ctx_.set_detect_ps_integrity(true);

    // Previous left bytes, to parse in next loop.
    uint32_t reserved = 0;

    for (;;) {
        if (!owner_coroutine_)
            return err;
        if ((err = owner_coroutine_->pull()) != srs_success) {
            return srs_error_wrap(err, "pull");
        }

        // RFC4571, 2 bytes length.
        uint16_t length = 0;
        if (true) {
            uint8_t lbuffer[2];
            if ((err = conn_->read_fully(lbuffer, sizeof(lbuffer), NULL)) != srs_success) {
                return srs_error_wrap(err, "read");
            }

            length = ((uint16_t)lbuffer[0]) << 8 | (uint16_t)lbuffer[1];
            if (!length) {
                return srs_error_new(ERROR_GB_PS_MEDIA, "Invalid length");
            }
        }

        if (length > SRS_GB_LARGE_PACKET) {
            const SrsPsDecodeHelper &h = context.ctx_.helper_;
            srs_warn("PS: Large length=%u, previous-seq=%u, previous-ts=%u", length, h.rtp_seq_, h.rtp_ts_);
        }

        // Read length of bytes of RTP packet.
        if ((err = conn_->read_fully(buffer_ + reserved, length, NULL)) != srs_success) {
            return srs_error_wrap(err, "read");
        }

        // Drop all RTCP packets.
        if (srs_is_rtcp(buffer_ + reserved, length)) {
            nn_rtcp_++;
            srs_warn("PS: Drop RTCP packets nn=%d", nn_rtcp_);
            continue;
        }

        // If no session, try to finger out it.
        if (!session_) {
            SrsRtpPacket rtp;
            SrsBuffer b((char *)(buffer_ + reserved), length);
            if ((err = rtp.decode(&b)) != srs_success) {
                srs_warn("PS: Ignore packet length=%d for err %s", length, srs_error_desc(err).c_str());
                srs_freep(err); // We ignore any error when decoding the RTP packet.
                continue;
            }

            if ((err = bind_session(rtp.header_.get_ssrc(), &session_)) != srs_success) {
                return srs_error_wrap(err, "bind session");
            }
        }
        if (!session_) {
            srs_warn("PS: Ignore packet length=%d for no session", length);
            continue; // Ignore any media packet when no session.
        }

        // Show tips about the buffer to parse.
        if (reserved) {
            string bytes = srs_strings_dumps_hex((const char *)(buffer_ + reserved), length, 16);
            srs_trace("PS: Consume reserved=%dB, length=%d, bytes=[%s]", reserved, length, bytes.c_str());
        }

        // Parse RTP over TCP, RFC4571.
        SrsBuffer b((char *)buffer_, length + reserved);
        if ((err = context.decode_rtp(&b, reserved, pack_)) != srs_success) {
            return srs_error_wrap(err, "decode pack");
        }

        // There might some messages left to parse in next loop.
        reserved = b.left();
        if (reserved > 128) {
            srs_warn("PS: Drop too many reserved=%d bytes", reserved);
            reserved = 0; // Avoid reserving too much data.
        }
        if (reserved) {
            string bytes = srs_strings_dumps_hex(b.head(), reserved, 16);
            srs_trace("PS: Reserved bytes for next loop, pos=%d, left=%d, total=%d, bytes=[%s]",
                      b.pos(), b.left(), b.size(), bytes.c_str());
            // Copy the bytes left to the start of buffer. Note that the left(reserved) bytes might be overlapped with
            // buffer, so we must use memmove not memcpy, see https://github.com/ossrs/srs/issues/3300#issuecomment-1352907075
            memmove(buffer_, b.head(), reserved);
            pack_->media_reserved_++;
        }
    }

    return err;
}

srs_error_t SrsGbMediaTcpConn::on_ps_pack(SrsPsPacket *ps, const std::vector<SrsTsMessage *> &msgs)
{
    srs_error_t err = srs_success;

    // Change state to connected.
    if (!connected_) {
        connected_ = true;
        srs_trace("PS: Media connected");
    }

    // Notify session about the media pack.
    session_->on_ps_pack(pack_, ps, msgs);

    // for (vector<SrsTsMessage*>::const_iterator it = msgs.begin(); it != msgs.end(); ++it) {
    //     SrsTsMessage* msg = *it;
    //     uint8_t* p = (uint8_t*)msg->payload->bytes();
    //     srs_trace("PS: Handle message %s, dts=%" PRId64 ", payload=%dB, %#x, %#x, %#x, %#x, %#x, %#x, %#x, %#x",
    //         msg->is_video() ? "Video" : "Audio", msg->dts, msg->PES_packet_length,
    //         p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
    // }

    return err;
}

srs_error_t SrsGbMediaTcpConn::bind_session(uint32_t ssrc, SrsGbSession **psession)
{
    srs_error_t err = srs_success;

    if (!ssrc)
        return err;

    // Find exists session for register, might be created by another object and still alive.
    SrsSharedResource<SrsGbSession> *session = dynamic_cast<SrsSharedResource<SrsGbSession> *>(_srs_gb_manager->find_by_fast_id(ssrc));
    if (!session)
        return err;

    SrsGbSession *raw_session = (*session).get();
    srs_assert(raw_session);

    // Notice session to use current media connection.
    raw_session->on_media_transport(*wrapper_);
    *psession = raw_session;

    return err;
}

SrsMpegpsQueue::SrsMpegpsQueue()
{
    nb_audios_ = nb_videos_ = 0;
}

SrsMpegpsQueue::~SrsMpegpsQueue()
{
    std::map<int64_t, SrsMediaPacket *>::iterator it;
    for (it = msgs_.begin(); it != msgs_.end(); ++it) {
        SrsMediaPacket *msg = it->second;
        srs_freep(msg);
    }
    msgs_.clear();
}

srs_error_t SrsMpegpsQueue::push(SrsMediaPacket *msg)
{
    srs_error_t err = srs_success;

    // TODO: FIXME: use right way.
    for (int i = 0; i < 10; i++) {
        if (msgs_.find(msg->timestamp_) == msgs_.end()) {
            break;
        }

        // adjust the ts, add 1ms.
        msg->timestamp_ += 1;

        if (i >= 100) {
            srs_warn("Muxer: free the msg for dts exists, dts=%" PRId64, msg->timestamp_);
            srs_freep(msg);
            return err;
        }
    }

    if (msg->is_audio()) {
        nb_audios_++;
    }

    if (msg->is_video()) {
        nb_videos_++;
    }

    msgs_[msg->timestamp_] = msg;

    return err;
}

SrsMediaPacket *SrsMpegpsQueue::dequeue()
{
    // got 2+ videos and audios, ok to dequeue.
    bool av_ok = nb_videos_ >= 2 && nb_audios_ >= 2;
    // 100 videos about 30s, while 300 audios about 30s
    bool av_overflow = nb_videos_ > 100 || nb_audios_ > 300;

    if (av_ok || av_overflow) {
        std::map<int64_t, SrsMediaPacket *>::iterator it = msgs_.begin();
        SrsMediaPacket *msg = it->second;
        msgs_.erase(it);

        if (msg->is_audio()) {
            nb_audios_--;
        }

        if (msg->is_video()) {
            nb_videos_--;
        }

        return msg;
    }

    return NULL;
}

SrsGbMuxer::SrsGbMuxer(SrsGbSession *session)
{
    sdk_ = NULL;
    session_ = session;

    avc_ = new SrsRawH264Stream();
    h264_sps_changed_ = false;
    h264_pps_changed_ = false;
    h264_sps_pps_sent_ = false;

    hevc_ = new SrsRawHEVCStream();
    vps_sps_pps_sent_ = false;
    vps_sps_pps_change_ = false;

    aac_ = new SrsRawAacStream();

    queue_ = new SrsMpegpsQueue();
    pprint_ = SrsPithyPrint::create_caster();
}

SrsGbMuxer::~SrsGbMuxer()
{
    close();

    srs_freep(avc_);
    srs_freep(hevc_);
    srs_freep(aac_);
    srs_freep(queue_);
    srs_freep(pprint_);
}

void SrsGbMuxer::setup(std::string output)
{
    output_ = output;
}

srs_error_t SrsGbMuxer::on_ts_message(SrsTsMessage *msg)
{
    srs_error_t err = srs_success;

    SrsBuffer avs(msg->payload_->bytes(), msg->payload_->length());
    if (msg->sid_ == SrsTsPESStreamIdVideoCommon) {
        if ((err = on_ts_video(msg, &avs)) != srs_success) {
            return srs_error_wrap(err, "ts: consume video");
        }
    } else {
        if ((err = on_ts_audio(msg, &avs)) != srs_success) {
            return srs_error_wrap(err, "ts: consume audio");
        }
    }

    return err;
}

srs_error_t SrsGbMuxer::on_ts_video(SrsTsMessage *msg, SrsBuffer *avs)
{
    srs_error_t err = srs_success;

    // ensure rtmp connected.
    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }

    SrsPsDecodeHelper *h = (SrsPsDecodeHelper *)msg->ps_helper_;
    srs_assert(h && h->ctx_ && h->ps_);

    if (h->ctx_->video_stream_type_ == SrsTsStreamVideoH264) {
        if ((err = mux_h264(msg, avs)) != srs_success) {
            return srs_error_wrap(err, "mux h264");
        }
    } else if (h->ctx_->video_stream_type_ == SrsTsStreamVideoHEVC) {
        if ((err = mux_h265(msg, avs)) != srs_success) {
            return srs_error_wrap(err, "mux hevc");
        }
    } else {
        return srs_error_new(ERROR_STREAM_CASTER_TS_CODEC, "ts: unsupported stream codec=%d", h->ctx_->video_stream_type_);
    }

    return err;
}

srs_error_t SrsGbMuxer::mux_h264(SrsTsMessage *msg, SrsBuffer *avs)
{
    srs_error_t err = srs_success;

    // ts tbn to flv tbn.
    uint32_t dts = (uint32_t)(msg->dts_ / 90);
    uint32_t pts = (uint32_t)(msg->dts_ / 90);

    // send each frame.
    while (!avs->empty()) {
        char *frame = NULL;
        int frame_size = 0;
        if ((err = avc_->annexb_demux(avs, &frame, &frame_size)) != srs_success) {
            return srs_error_wrap(err, "demux avc annexb");
        }

        // 5bits, 7.3.1 NAL unit syntax,
        // ISO_IEC_14496-10-AVC-2003.pdf, page 44.
        //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
        SrsAvcNaluType nt = (SrsAvcNaluType)(frame[0] & 0x1f);

        // Ignore the nalu except video frames:
        //      7: SPS, 8: PPS, 5: I Frame, 1: P Frame, 6: SEI, 9: AUD
        if (
            nt != SrsAvcNaluTypeSPS && nt != SrsAvcNaluTypePPS && nt != SrsAvcNaluTypeIDR &&
            nt != SrsAvcNaluTypeNonIDR && nt != SrsAvcNaluTypeSEI && nt != SrsAvcNaluTypeAccessUnitDelimiter) {
            string bytes = srs_strings_dumps_hex(frame, frame_size, 4);
            srs_warn("GB: Ignore NALU nt=%d, frame=[%s]", nt, bytes.c_str());
            return err;
        }
        if (nt == SrsAvcNaluTypeSEI || nt == SrsAvcNaluTypeAccessUnitDelimiter) {
            continue;
        }

        // for sps
        if (avc_->is_sps(frame, frame_size)) {
            std::string sps;
            if ((err = avc_->sps_demux(frame, frame_size, sps)) != srs_success) {
                return srs_error_wrap(err, "demux sps");
            }

            if (h264_sps_ == sps) {
                continue;
            }
            h264_sps_changed_ = true;
            h264_sps_ = sps;

            if ((err = write_h264_sps_pps(dts, pts)) != srs_success) {
                return srs_error_wrap(err, "write sps/pps");
            }
            continue;
        }

        // for pps
        if (avc_->is_pps(frame, frame_size)) {
            std::string pps;
            if ((err = avc_->pps_demux(frame, frame_size, pps)) != srs_success) {
                return srs_error_wrap(err, "demux pps");
            }

            if (h264_pps_ == pps) {
                continue;
            }
            h264_pps_changed_ = true;
            h264_pps_ = pps;

            if ((err = write_h264_sps_pps(dts, pts)) != srs_success) {
                return srs_error_wrap(err, "write sps/pps");
            }
            continue;
        }

        // ibp frame.
        // TODO: FIXME: we should group all frames to a rtmp/flv message from one ts message.
        srs_info("Muxer: demux avc ibp frame size=%d, dts=%d", frame_size, dts);
        if ((err = write_h264_ipb_frame(frame, frame_size, dts, pts)) != srs_success) {
            return srs_error_wrap(err, "write frame");
        }
    }

    return err;
}

srs_error_t SrsGbMuxer::write_h264_sps_pps(uint32_t dts, uint32_t pts)
{
    srs_error_t err = srs_success;

    // TODO: FIMXE: there exists bug, see following comments.
    // when sps or pps changed, update the sequence header,
    // for the pps maybe not changed while sps changed.
    // so, we must check when each video ts message frame parsed.
    if (!h264_sps_changed_ || !h264_pps_changed_) {
        return err;
    }

    // h264 raw to h264 packet.
    std::string sh;
    if ((err = avc_->mux_sequence_header(h264_sps_, h264_pps_, sh)) != srs_success) {
        return srs_error_wrap(err, "mux sequence header");
    }

    // h264 packet to flv packet.
    int8_t frame_type = SrsVideoAvcFrameTypeKeyFrame;
    int8_t avc_packet_type = SrsVideoAvcFrameTraitSequenceHeader;
    char *flv = NULL;
    int nb_flv = 0;
    if ((err = avc_->mux_avc2flv(sh, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "avc to flv");
    }

    // the timestamp in rtmp message header is dts.
    uint32_t timestamp = dts;
    if ((err = rtmp_write_packet(SrsFrameTypeVideo, timestamp, flv, nb_flv)) != srs_success) {
        return srs_error_wrap(err, "write packet");
    }

    // reset sps and pps.
    h264_sps_changed_ = false;
    h264_pps_changed_ = false;
    h264_sps_pps_sent_ = true;

    return err;
}

srs_error_t SrsGbMuxer::write_h264_ipb_frame(char *frame, int frame_size, uint32_t dts, uint32_t pts)
{
    srs_error_t err = srs_success;

    // when sps or pps not sent, ignore the packet.
    if (!h264_sps_pps_sent_) {
        return srs_error_new(ERROR_H264_DROP_BEFORE_SPS_PPS, "drop for no sps/pps");
    }

    // 5bits, 7.3.1 NAL unit syntax,
    // ISO_IEC_14496-10-AVC-2003.pdf, page 44.
    //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
    SrsAvcNaluType nal_unit_type = (SrsAvcNaluType)(frame[0] & 0x1f);

    // for IDR frame, the frame is keyframe.
    SrsVideoAvcFrameType frame_type = SrsVideoAvcFrameTypeInterFrame;
    if (nal_unit_type == SrsAvcNaluTypeIDR) {
        frame_type = SrsVideoAvcFrameTypeKeyFrame;
    }

    std::string ibp;
    if ((err = avc_->mux_ipb_frame(frame, frame_size, ibp)) != srs_success) {
        return srs_error_wrap(err, "mux frame");
    }

    int8_t avc_packet_type = SrsVideoAvcFrameTraitNALU;
    char *flv = NULL;
    int nb_flv = 0;
    if ((err = avc_->mux_avc2flv(ibp, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "mux avc to flv");
    }

    // the timestamp in rtmp message header is dts.
    uint32_t timestamp = dts;
    return rtmp_write_packet(SrsFrameTypeVideo, timestamp, flv, nb_flv);
}

srs_error_t SrsGbMuxer::mux_h265(SrsTsMessage *msg, SrsBuffer *avs)
{
    srs_error_t err = srs_success;

    // ts tbn to flv tbn.
    uint32_t dts = (uint32_t)(msg->dts_ / 90);
    uint32_t pts = (uint32_t)(msg->dts_ / 90);

    // send each frame.
    while (!avs->empty()) {
        char *frame = NULL;
        int frame_size = 0;
        if ((err = hevc_->annexb_demux(avs, &frame, &frame_size)) != srs_success) {
            return srs_error_wrap(err, "demux hevc annexb");
        }

        // 6bits, 7.4.2.2 NAL unit header semantics
        // ITU-T-H.265-2021.pdf, page 85.
        // 32: VPS, 33: SPS, 34: PPS ...
        SrsHevcNaluType nt = SrsHevcNaluTypeParse(frame[0]);
        if (nt == SrsHevcNaluType_SEI || nt == SrsHevcNaluType_SEI_SUFFIX || nt == SrsHevcNaluType_ACCESS_UNIT_DELIMITER) {
            continue;
        }

        // for vps
        if (hevc_->is_vps(frame, frame_size)) {
            std::string vps;
            if ((err = hevc_->vps_demux(frame, frame_size, vps)) != srs_success) {
                return srs_error_wrap(err, "demux vps");
            }

            if (h265_vps_ == vps) {
                continue;
            }

            vps_sps_pps_change_ = true;
            h265_vps_ = vps;

            if ((err = write_h265_vps_sps_pps(dts, pts)) != srs_success) {
                return srs_error_wrap(err, "write vps");
            }
            continue;
        }

        // for sps
        if (hevc_->is_sps(frame, frame_size)) {
            std::string sps;
            if ((err = hevc_->sps_demux(frame, frame_size, sps)) != srs_success) {
                return srs_error_wrap(err, "demux sps");
            }

            if (h265_sps_ == sps) {
                continue;
            }
            vps_sps_pps_change_ = true;
            h265_sps_ = sps;

            if ((err = write_h265_vps_sps_pps(dts, pts)) != srs_success) {
                return srs_error_wrap(err, "write sps");
            }
            continue;
        }

        // for pps
        if (hevc_->is_pps(frame, frame_size)) {
            std::string pps;
            if ((err = hevc_->pps_demux(frame, frame_size, pps)) != srs_success) {
                return srs_error_wrap(err, "demux pps");
            }

            if (h265_pps_ == pps) {
                continue;
            }
            vps_sps_pps_change_ = true;
            h265_pps_ = pps;

            if ((err = write_h265_vps_sps_pps(dts, pts)) != srs_success) {
                return srs_error_wrap(err, "write pps");
            }
            continue;
        }

        // ibp frame.
        // TODO: FIXME: we should group all frames to a rtmp/flv message from one ts message.
        srs_info("Muxer: demux avc ibp frame size=%d, dts=%d", frame_size, dts);
        if ((err = write_h265_ipb_frame(frame, frame_size, dts, pts)) != srs_success) {
            return srs_error_wrap(err, "write frame");
        }
    }

    return err;
}

srs_error_t SrsGbMuxer::write_h265_vps_sps_pps(uint32_t dts, uint32_t pts)
{
    srs_error_t err = srs_success;

    if (!vps_sps_pps_change_) {
        return err;
    }

    if (h265_vps_.empty() || h265_sps_.empty() || h265_pps_.empty()) {
        return err;
    }

    std::vector<std::string> h265_pps;
    h265_pps.push_back(h265_pps_);

    std::string sh;
    if ((err = hevc_->mux_sequence_header(h265_vps_, h265_sps_, h265_pps, sh)) != srs_success) {
        return srs_error_wrap(err, "hevc mux sequence header");
    }

    // h265 packet to flv packet.
    int8_t frame_type = SrsVideoAvcFrameTypeKeyFrame;
    int8_t hevc_packet_type = SrsVideoAvcFrameTraitSequenceHeader;

    char *flv = NULL;
    int nb_flv = 0;

    if ((err = hevc_->mux_hevc2flv(sh, frame_type, hevc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "hevc to flv");
    }

    // the timestamp in rtmp message header is dts.
    uint32_t timestamp = dts;
    if ((err = rtmp_write_packet(SrsFrameTypeVideo, timestamp, flv, nb_flv)) != srs_success) {
        return srs_error_wrap(err, "hevc write packet");
    }

    // reset vps/sps/pps.
    vps_sps_pps_change_ = false;
    vps_sps_pps_sent_ = true;

    return err;
}

srs_error_t SrsGbMuxer::write_h265_ipb_frame(char *frame, int frame_size, uint32_t dts, uint32_t pts)
{
    srs_error_t err = srs_success;

    // when sps or pps not sent, ignore the packet.
    if (!vps_sps_pps_sent_) {
        return srs_error_new(ERROR_H264_DROP_BEFORE_SPS_PPS, "drop for no vps/sps/pps");
    }

    SrsHevcNaluType nt = SrsHevcNaluTypeParse(frame[0]);

    // F.3.29 intra random access point (IRAP) picture
    // ITU-T-H.265-2021.pdf, page 462.
    SrsVideoAvcFrameType frame_type = SrsVideoAvcFrameTypeInterFrame;
    if (SrsIsIRAP(nt)) {
        frame_type = SrsVideoAvcFrameTypeKeyFrame;
    }

    string ipb;
    if ((err = hevc_->mux_ipb_frame(frame, frame_size, ipb)) != srs_success) {
        return srs_error_wrap(err, "hevc mux ipb frame");
    }

    int8_t hevc_packet_type = SrsVideoAvcFrameTraitNALU;
    char *flv = NULL;
    int nb_flv = 0;

    if ((err = hevc_->mux_hevc2flv(ipb, frame_type, hevc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "hevc to flv");
    }

    // the timestamp in rtmp message header is dts.
    uint32_t timestamp = dts;
    if ((err = rtmp_write_packet(SrsFrameTypeVideo, timestamp, flv, nb_flv)) != srs_success) {
        return srs_error_wrap(err, "hevc write packet");
    }

    return err;
}

srs_error_t SrsGbMuxer::on_ts_audio(SrsTsMessage *msg, SrsBuffer *avs)
{
    srs_error_t err = srs_success;

    // ensure rtmp connected.
    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }

    // ts tbn to flv tbn.
    uint32_t dts = (uint32_t)(msg->dts_ / 90);

    // send each frame.
    while (!avs->empty()) {
        char *frame = NULL;
        int frame_size = 0;
        SrsRawAacStreamCodec codec;
        if ((err = aac_->adts_demux(avs, &frame, &frame_size, codec)) != srs_success) {
            return srs_error_wrap(err, "demux adts");
        }

        // ignore invalid frame,
        //  * atleast 1bytes for aac to decode the data.
        if (frame_size <= 0) {
            continue;
        }
        srs_info("Muxer: demux aac frame size=%d, dts=%d", frame_size, dts);

        // generate sh.
        if (aac_specific_config_.empty()) {
            std::string sh;
            if ((err = aac_->mux_sequence_header(&codec, sh)) != srs_success) {
                return srs_error_wrap(err, "mux sequence header");
            }
            aac_specific_config_ = sh;

            codec.aac_packet_type_ = 0;

            if ((err = write_audio_raw_frame((char *)sh.data(), (int)sh.length(), &codec, dts)) != srs_success) {
                return srs_error_wrap(err, "write raw audio frame");
            }
        }

        // audio raw data.
        codec.aac_packet_type_ = 1;
        if ((err = write_audio_raw_frame(frame, frame_size, &codec, dts)) != srs_success) {
            return srs_error_wrap(err, "write audio raw frame");
        }
    }

    return err;
}

srs_error_t SrsGbMuxer::write_audio_raw_frame(char *frame, int frame_size, SrsRawAacStreamCodec *codec, uint32_t dts)
{
    srs_error_t err = srs_success;

    char *data = NULL;
    int size = 0;
    if ((err = aac_->mux_aac2flv(frame, frame_size, codec, dts, &data, &size)) != srs_success) {
        return srs_error_wrap(err, "mux aac to flv");
    }

    return rtmp_write_packet(SrsFrameTypeAudio, dts, data, size);
}

srs_error_t SrsGbMuxer::rtmp_write_packet(char type, uint32_t timestamp, char *data, int size)
{
    srs_error_t err = srs_success;

    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }

    SrsRtmpCommonMessage *cmsg = NULL;
    if ((err = srs_rtmp_create_msg(type, timestamp, data, size, sdk_->sid(), &cmsg)) != srs_success) {
        return srs_error_wrap(err, "create message");
    }

    SrsMediaPacket *msg = new SrsMediaPacket();
    cmsg->to_msg(msg);
    srs_freep(cmsg);

    // push msg to queue.
    if ((err = queue_->push(msg)) != srs_success) {
        return srs_error_wrap(err, "push to queue");
    }

    // for all ready msg, dequeue and send out.
    for (;;) {
        if ((msg = queue_->dequeue()) == NULL) {
            break;
        }

        if (pprint_->can_print()) {
            srs_trace("Muxer: send msg %s age=%d, dts=%" PRId64 ", size=%d",
                      msg->is_audio() ? "A" : msg->is_video() ? "V"
                                                              : "N",
                      pprint_->age(), msg->timestamp_, msg->size());
        }

        // send out encoded msg.
        if ((err = sdk_->send_and_free_message(msg)) != srs_success) {
            close();
            return srs_error_wrap(err, "send messages");
        }
    }

    return err;
}

srs_error_t SrsGbMuxer::connect()
{
    srs_error_t err = srs_success;

    // Ignore when connected.
    if (sdk_) {
        return err;
    }

    // Cleanup the data before connect again.
    close();

    string url = srs_strings_replace(output_, "[stream]", session_->device_id_);
    srs_trace("Muxer: Convert GB to RTMP %s", url.c_str());

    srs_utime_t cto = SRS_CONSTS_RTMP_TIMEOUT;
    srs_utime_t sto = SRS_CONSTS_RTMP_PULSE;
    sdk_ = new SrsSimpleRtmpClient(url, cto, sto);

    if ((err = sdk_->connect()) != srs_success) {
        close();
        return srs_error_wrap(err, "connect %s failed, cto=%dms, sto=%dms.", url.c_str(), srsu2msi(cto), srsu2msi(sto));
    }

    if ((err = sdk_->publish(SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE)) != srs_success) {
        close();
        return srs_error_wrap(err, "publish");
    }

    return err;
}

void SrsGbMuxer::close()
{
    srs_freep(sdk_);

    // Regenerate the AAC sequence header.
    aac_specific_config_ = "";

    // Wait for the next AVC sequence header.
    h264_sps_pps_sent_ = false;
    h264_sps_ = "";
    h264_pps_ = "";
}

SrsPackContext::SrsPackContext(ISrsPsPackHandler *handler)
{
    static uint32_t gid = 0;
    media_id_ = ++gid;

    media_startime_ = srs_time_now_realtime();
    media_nn_recovered_ = 0;
    media_nn_msgs_dropped_ = 0;
    media_reserved_ = 0;

    ps_ = new SrsPsPacket(NULL);
    handler_ = handler;
}

SrsPackContext::~SrsPackContext()
{
    clear();
    srs_freep(ps_);
}

void SrsPackContext::clear()
{
    for (vector<SrsTsMessage *>::iterator it = msgs_.begin(); it != msgs_.end(); ++it) {
        SrsTsMessage *msg = *it;
        srs_freep(msg);
    }

    msgs_.clear();
}

srs_error_t SrsPackContext::on_ts_message(SrsTsMessage *msg)
{
    srs_error_t err = srs_success;

    SrsPsDecodeHelper *h = (SrsPsDecodeHelper *)msg->ps_helper_;
    srs_assert(h && h->ctx_ && h->ps_);

    // We got new pack header and an optional system header.
    // if (ps_->id_ != h->ps_->id_) {
    //    stringstream ss;
    //    if (h->ps_->has_pack_header_) ss << srs_fmt_sprintf(", clock=%" PRId64 ", rate=%d", h->ps_->system_clock_reference_base_, h->ps_->program_mux_rate_);
    //    if (h->ps_->has_system_header_) ss << srs_fmt_sprintf(", rate_bound=%d, video_bound=%d, audio_bound=%d", h->ps_->rate_bound_, h->ps_->video_bound_, h->ps_->audio_bound_);
    //    srs_trace("PS: New pack header=%d, system=%d%s", h->ps_->has_pack_header_, h->ps_->has_system_header_, ss.str().c_str());
    //}

    // Correct DTS/PS to the last one.
    if (!msgs_.empty() && (!msg->dts_ || !msg->pts_)) {
        SrsTsMessage *last = msgs_.back();
        if (!msg->dts_)
            msg->dts_ = last->dts_;
        if (!msg->pts_)
            msg->pts_ = last->pts_;
    }

    // uint8_t* p = (uint8_t*)msg->payload->bytes();
    // srs_trace("PS: Got message %s, dts=%" PRId64 ", seq=%u, base=%" PRId64 ", payload=%dB, %#x, %#x, %#x, %#x, %#x, %#x, %#x, %#x",
    //     msg->is_video() ? "Video" : "Audio", msg->dts, h->rtp_seq_, h->ps_->system_clock_reference_base_, msg->PES_packet_length,
    //     p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);

    // Notify about the previous pack.
    if (ps_->id_ != h->ps_->id_) {
        // Handle all messages of previous pack, note that we must free them.
        if (!msgs_.empty()) {
            err = handler_->on_ps_pack(ps_, msgs_);
            clear();
        }

        // Directly copy the pack headers to current.
        *ps_ = *h->ps_;
    }

    // Store the message to current pack.
    msgs_.push_back(msg->detach());

    return err;
}

void SrsPackContext::on_recover_mode(int nn_recover)
{
    // Only update stat for the first time.
    if (nn_recover <= 1) {
        media_nn_recovered_++;
    }

    // Always update the stat for messages.
    if (!msgs_.empty()) {
        media_nn_msgs_dropped_ += msgs_.size();
        clear();
    }
}

SrsRecoverablePsContext::SrsRecoverablePsContext()
{
    recover_ = 0;
}

SrsRecoverablePsContext::~SrsRecoverablePsContext()
{
}

srs_error_t SrsRecoverablePsContext::decode_rtp(SrsBuffer *stream, int reserved, ISrsPsMessageHandler *handler)
{
    srs_error_t err = srs_success;

    // Start to parse from the reserved bytes.
    stream->skip(reserved);

    SrsRtpPacket rtp;
    int pos = stream->pos();
    if ((err = rtp.decode(stream)) != srs_success) {
        return enter_recover_mode(stream, handler, pos, srs_error_wrap(err, "decode rtp"));
    }

    SrsRtpRawPayload *rtp_raw = dynamic_cast<SrsRtpRawPayload *>(rtp.payload());
    srs_assert(rtp_raw); // It must be a RTP RAW payload, by default.

    // If got reserved bytes, move to the start of payload.
    if (reserved) {
        // Move the reserved bytes to the start of payload, from which we should parse.
        char *src = stream->head() - stream->pos();
        char *dst = stream->head() - reserved;
        memmove(dst, src, reserved);

        // The payload also should skip back to the reserved bytes.
        rtp_raw->payload_ -= reserved;
        rtp_raw->nn_payload_ += reserved;

        // The stream also skip back to the not parsed bytes.
        stream->skip(-1 * reserved);
    }

    SrsBuffer b((char *)rtp_raw->payload_, rtp_raw->nn_payload_);
    // srs_trace("GB: Got RTP length=%d, payload=%d, seq=%u, ts=%d", length, rtp_raw->nn_payload, rtp.header_.get_sequence(), rtp.header_.get_timestamp());

    ctx_.helper_.rtp_seq_ = rtp.header_.get_sequence();
    ctx_.helper_.rtp_ts_ = rtp.header_.get_timestamp();
    ctx_.helper_.rtp_pt_ = rtp.header_.get_payload_type();
    if ((err = decode(&b, handler)) != srs_success) {
        return srs_error_wrap(err, "decode");
    }

    // Consume the stream, because there might be data left in stream.
    stream->skip(b.pos());

    return err;
}

srs_error_t SrsRecoverablePsContext::decode(SrsBuffer *stream, ISrsPsMessageHandler *handler)
{
    srs_error_t err = srs_success;

    // Ignore if empty packet.
    if (stream->empty())
        return err;

    // For recover mode, we drop bytes util pack header(00 00 01 ba).
    if (recover_) {
        int pos = stream->pos();
        if (!srs_skip_util_pack(stream)) {
            stream->skip(pos - stream->pos());
            return enter_recover_mode(stream, handler, pos, srs_error_new(ERROR_GB_PS_HEADER, "no pack"));
        }
        quit_recover_mode(stream, handler);
    }

    // Got packet to decode.
    if ((err = ctx_.decode(stream, handler)) != srs_success) {
        return enter_recover_mode(stream, handler, stream->pos(), srs_error_wrap(err, "decode pack"));
    }
    return err;
}

srs_error_t SrsRecoverablePsContext::enter_recover_mode(SrsBuffer *stream, ISrsPsMessageHandler *handler, int pos, srs_error_t err)
{
    // Enter recover mode. Increase the recover counter because we might fail for many times.
    recover_++;

    // Print the error information for debugging.
    int npos = stream->pos();
    stream->skip(pos - stream->pos());
    string bytes = srs_strings_dumps_hex(stream->head(), stream->left(), 8);

    SrsPsDecodeHelper &h = ctx_.helper_;
    uint16_t pack_seq = h.pack_first_seq_;
    uint16_t pack_msgs = h.pack_nn_msgs_;
    uint16_t lsopm = h.pack_pre_msg_last_seq_;
    SrsTsMessage *last = ctx_.last();
    srs_warn("PS: Enter recover=%d, seq=%u, ts=%u, pt=%u, pack=%u, msgs=%u, lsopm=%u, last=%u/%u, bytes=[%s], pos=%d, left=%d for err %s",
             recover_, h.rtp_seq_, h.rtp_ts_, h.rtp_pt_, pack_seq, pack_msgs, lsopm, last->PES_packet_length_, last->payload_->length(),
             bytes.c_str(), npos, stream->left(), srs_error_desc(err).c_str());

    // If RTP packet exceed SRS_GB_LARGE_PACKET, which is large packet, might be correct length and impossible to
    // recover, so we directly fail it and re-inivte.
    if (stream->size() > SRS_GB_LARGE_PACKET) {
        return srs_error_wrap(err, "no recover for large packet length=%dB", stream->size());
    }

    // Sometimes, we're unable to recover it, so we limit the max retry.
    if (recover_ > SRS_GB_MAX_RECOVER) {
        return srs_error_wrap(err, "exceed max recover, pack=%u, pack-seq=%u, seq=%u",
                              h.pack_id_, h.pack_first_seq_, h.rtp_seq_);
    }

    // Reap and dispose last incomplete message.
    SrsTsMessage *msg = ctx_.reap();
    srs_freep(msg);
    // Skip all left bytes in buffer, reset error because recovered.
    stream->skip(stream->left());
    srs_freep(err);

    // Notify handler to cleanup previous messages in pack.
    handler->on_recover_mode(recover_);

    return err;
}

void SrsRecoverablePsContext::quit_recover_mode(SrsBuffer *stream, ISrsPsMessageHandler *handler)
{
    string bytes = srs_strings_dumps_hex(stream->head(), stream->left(), 8);
    srs_warn("PS: Quit recover=%d, seq=%u, bytes=[%s], pos=%d, left=%d", recover_, ctx_.helper_.rtp_seq_,
             bytes.c_str(), stream->pos(), stream->left());
    recover_ = 0;
}

bool srs_skip_util_pack(SrsBuffer *stream)
{
    while (stream->require(4)) {
        uint8_t *p = (uint8_t *)stream->head();

        // When searching pack header from payload, mostly not zero.
        if (p[0] != 0x00 && p[1] != 0x00 && p[2] != 0x00 && p[3] != 0x00) {
            stream->skip(4);
        } else if (p[0] != 0x00 && p[1] != 0x00 && p[2] != 0x00) {
            stream->skip(3);
        } else if (p[0] != 0x00 && p[1] != 0x00) {
            stream->skip(2);
        } else {
            if (p[0] == 0x00 && p[1] == 0x00 && p[2] == 0x01 && p[3] == 0xba) {
                return true;
            }
            stream->skip(1);
        }
    }

    return false;
}

SrsGoApiGbPublish::SrsGoApiGbPublish(SrsConfDirective *conf)
{
    conf_ = conf->copy();
}

SrsGoApiGbPublish::~SrsGoApiGbPublish()
{
    srs_freep(conf_);
}

srs_error_t SrsGoApiGbPublish::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsJsonObject> res(SrsJsonAny::object());

    if ((err = do_serve_http(w, r, res.get())) != srs_success) {
        srs_warn("GB error %s", srs_error_desc(err).c_str());
        res->set("code", SrsJsonAny::integer(srs_error_code(err)));
        res->set("desc", SrsJsonAny::str(srs_error_code_str(err).c_str()));
        srs_freep(err);
        return srs_api_response(w, r, res->dumps());
    }

    return srs_api_response(w, r, res->dumps());
}

srs_error_t SrsGoApiGbPublish::do_serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, SrsJsonObject *res)
{
    srs_error_t err = srs_success;

    // For each GB session, we use short-term HTTP connection.
    w->header()->set("Connection", "Close");

    // Parse req, the request json object, from body.
    SrsSharedPtr<SrsJsonObject> req;
    if (true) {
        string req_json;
        if ((err = r->body_read_all(req_json)) != srs_success) {
            return srs_error_wrap(err, "read body");
        }

        SrsJsonAny *json = SrsJsonAny::loads(req_json);
        if (!json || !json->is_object()) {
            srs_freep(json);
            return srs_error_new(ERROR_HTTP_DATA_INVALID, "invalid body %s", req_json.c_str());
        }

        req = SrsSharedPtr<SrsJsonObject>(json->to_object());
    }

    // Fetch params from req object.
    SrsJsonAny *prop = NULL;
    if ((prop = req->ensure_property_string("id")) == NULL) {
        return srs_error_new(ERROR_HTTP_DATA_INVALID, "id required");
    }
    string id = prop->to_str();

    if ((prop = req->ensure_property_string("ssrc")) == NULL) {
        return srs_error_new(ERROR_HTTP_DATA_INVALID, "ssrc required");
    }
    uint64_t ssrc = atoi(prop->to_str().c_str());

    if ((err = bind_session(id, ssrc)) != srs_success) {
        return srs_error_wrap(err, "bind session");
    }

    res->set("code", SrsJsonAny::integer(ERROR_SUCCESS));
    int port = _srs_config->get_stream_caster_listen(conf_);
    res->set("port", SrsJsonAny::integer(port));
    res->set("is_tcp", SrsJsonAny::boolean(true)); // only tcp supported

    srs_trace("GB publish id: %s, ssrc=%lu", id.c_str(), ssrc);

    return err;
}

srs_error_t SrsGoApiGbPublish::bind_session(std::string id, uint64_t ssrc)
{
    srs_error_t err = srs_success;

    SrsSharedResource<SrsGbSession> *session = NULL;
    session = dynamic_cast<SrsSharedResource<SrsGbSession> *>(_srs_gb_manager->find_by_id(id));
    if (session) {
        return srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "stream already exists");
    }

    session = dynamic_cast<SrsSharedResource<SrsGbSession> *>(_srs_gb_manager->find_by_fast_id(ssrc));
    if (session) {
        return srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "ssrc already exists");
    }

    // Create new GB session.
    SrsGbSession *raw_session = new SrsGbSession();
    raw_session->setup(conf_);

    session = new SrsSharedResource<SrsGbSession>(raw_session);
    _srs_gb_manager->add_with_id(id, session);
    _srs_gb_manager->add_with_fast_id(ssrc, session);

    SrsExecutorCoroutine *executor = new SrsExecutorCoroutine(_srs_gb_manager, session, raw_session, raw_session);
    raw_session->setup_owner(session, executor, executor);
    raw_session->device_id_ = id;

    if ((err = executor->start()) != srs_success) {
        srs_freep(executor);
        return srs_error_wrap(err, "gb session");
    }

    return err;
}

SrsResourceManager *_srs_gb_manager = NULL;
