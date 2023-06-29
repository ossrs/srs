//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_gb28181.hpp>

#include <srs_app_config.hpp>
#include <srs_app_listener.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_http_conn.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_conn.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_app_rtc_sdp.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_ps.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_conn.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_protocol_raw_avc.hpp>

#include <sstream>
using namespace std;

// See https://www.ietf.org/rfc/rfc3261.html#section-8.1.1.7
#define SRS_GB_BRANCH_MAGIC "z9hG4bK"
#define SRS_GB_SIP_PORT 5060
#define SRS_GB_MAX_RECOVER 16
#define SRS_GB_MAX_TIMEOUT 3
#define SRS_GB_LARGE_PACKET 1500
#define SRS_GB_SESSION_DRIVE_INTERVAL (300 * SRS_UTIME_MILLISECONDS)

extern bool srs_is_rtcp(const uint8_t* data, size_t len);

std::string srs_gb_session_state(SrsGbSessionState state)
{
    switch (state) {
        case SrsGbSessionStateInit: return "Init";
        case SrsGbSessionStateConnecting: return "Connecting";
        case SrsGbSessionStateEstablished: return "Established";
        default: return "Invalid";
    }
}

std::string srs_gb_state(SrsGbSessionState ostate, SrsGbSessionState state)
{
    return srs_fmt("%s->%s", srs_gb_session_state(ostate).c_str(), srs_gb_session_state(state).c_str());
}

std::string srs_gb_sip_state(SrsGbSipState state)
{
    switch (state) {
        case SrsGbSipStateInit: return "Init";
        case SrsGbSipStateRegistered: return "Registered";
        case SrsGbSipStateInviting: return "Inviting";
        case SrsGbSipStateTrying: return "Trying";
        case SrsGbSipStateStable: return "Stable";
        case SrsGbSipStateReinviting: return "Re-inviting";
        case SrsGbSipStateBye: return "Bye";
        default: return "Invalid";
    }
}

std::string srs_sip_state(SrsGbSipState ostate, SrsGbSipState state)
{
    return srs_fmt("%s->%s", srs_gb_sip_state(ostate).c_str(), srs_gb_sip_state(state).c_str());
}

SrsLazyGbSession::SrsLazyGbSession(SrsLazyObjectWrapper<SrsLazyGbSession>* wrapper_root)
{
    wrapper_root_ = wrapper_root;
    sip_ = new SrsLazyObjectWrapper<SrsLazyGbSipTcpConn>();
    media_ = new SrsLazyObjectWrapper<SrsLazyGbMediaTcpConn>();
    muxer_ = new SrsGbMuxer(this);
    state_ = SrsGbSessionStateInit;

    connecting_starttime_ = 0;
    connecting_timeout_ = 0;
    nn_timeout_ = 0;
    reinviting_starttime_ = 0;
    reinvite_wait_ = 0;

    ppp_ = new SrsAlonePithyPrint();
    startime_ = srs_update_system_time();
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
    trd_ = new SrsSTCoroutine("GBS", this, cid_);
}

SrsLazyGbSession::~SrsLazyGbSession()
{
    srs_freep(trd_);
    srs_freep(sip_);
    srs_freep(media_);
    srs_freep(muxer_);
    srs_freep(ppp_);
}

srs_error_t SrsLazyGbSession::initialize(SrsConfDirective* conf)
{
    srs_error_t err = srs_success;

    pip_ = candidate_ = _srs_config->get_stream_caster_sip_candidate(conf);
    if (candidate_ == "*") {
        pip_ = srs_get_public_internet_address(true);
    }

    std::string output = _srs_config->get_stream_caster_output(conf);
    if ((err = muxer_->initialize(output)) != srs_success) {
        return srs_error_wrap(err, "muxer");
    }

    connecting_timeout_ = _srs_config->get_stream_caster_sip_timeout(conf);
    reinvite_wait_ = _srs_config->get_stream_caster_sip_reinvite(conf);
    srs_trace("Session: Start timeout=%dms, reinvite=%dms, candidate=%s, pip=%s, output=%s", srsu2msi(connecting_timeout_),
        srsu2msi(reinvite_wait_), candidate_.c_str(), pip_.c_str(), output.c_str());

    return err;
}

void SrsLazyGbSession::on_ps_pack(SrsPackContext* ctx, SrsPsPacket* ps, const std::vector<SrsTsMessage*>& msgs)
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
    SrsTsMessage* video = new SrsTsMessage();
    SrsAutoFree(SrsTsMessage, video);

    for (vector<SrsTsMessage*>::const_iterator it = msgs.begin(); it != msgs.end(); ++it) {
        SrsTsMessage* msg = *it;

        // Group all videos to one video.
        if (msg->sid == SrsTsPESStreamIdVideoCommon) {
            video->ps_helper_ = msg->ps_helper_;
            video->dts = msg->dts;
            video->pts = msg->pts;
            video->sid = msg->sid;
            video->payload->append(msg->payload);
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
    if (video->payload->length() > 0) {
        srs_error_t err = muxer_->on_ts_message(video);
        if (err != srs_success) {
            srs_warn("Muxer: Ignore video err %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
    }
}

void SrsLazyGbSession::on_sip_transport(SrsLazyObjectWrapper<SrsLazyGbSipTcpConn>* sip)
{
    srs_freep(sip_);
    sip_ = sip->copy();

    // Change id of SIP and all its child coroutines.
    sip_->resource()->set_cid(cid_);
}

SrsLazyObjectWrapper<SrsLazyGbSipTcpConn>* SrsLazyGbSession::sip_transport()
{
    return sip_;
}

void SrsLazyGbSession::on_media_transport(SrsLazyObjectWrapper<SrsLazyGbMediaTcpConn>* media)
{
    srs_freep(media_);
    media_ = media->copy();

    // Change id of SIP and all its child coroutines.
    media_->resource()->set_cid(cid_);
}

std::string SrsLazyGbSession::pip()
{
    return pip_;
}

srs_error_t SrsLazyGbSession::start()
{
    srs_error_t err = srs_success;

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "coroutine");
    }

    return err;
}

srs_error_t SrsLazyGbSession::cycle()
{
    srs_error_t err = do_cycle();

    // Interrupt the SIP and media transport when session terminated.
    sip_->resource()->interrupt();
    media_->resource()->interrupt();

    // Note that we added wrapper to manager, so we must free the wrapper, not this connection.
    SrsLazyObjectWrapper<SrsLazyGbSession>* wrapper = wrapper_root_;
    srs_assert(wrapper); // The creator wrapper MUST never be null, because we created it.
    _srs_gb_manager->remove(wrapper);

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

srs_error_t SrsLazyGbSession::do_cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "pull");
        }

        // Drive the state in a fixed interval.
        srs_usleep(SRS_GB_SESSION_DRIVE_INTERVAL);

        // Client send bye, we should dispose the session.
        if (sip_->resource()->is_bye()) {
            return err;
        }

        // Regular state, driven by state of SIP and transport.
        if ((err = drive_state()) != srs_success) {
            return srs_error_wrap(err, "drive");
        }

        ppp_->elapse();
        if (ppp_->can_print()) {
            int alive = srsu2msi(srs_update_system_time() - startime_) / 1000;
            int pack_alive = srsu2msi(srs_update_system_time() - media_starttime_) / 1000;
            srs_trace("Session: Alive=%ds, packs=%" PRId64 ", recover=%" PRId64 ", reserved=%" PRId64 ", msgs=%" PRId64 ", drop=%" PRId64 ", media(id=%u, alive=%ds, packs=%" PRId64 " recover=%" PRId64", reserved=%" PRId64 ", msgs=%" PRId64 ", drop=%" PRId64 ")",
                alive, (total_packs_ + media_packs_), (total_recovered_ + media_recovered_), (total_reserved_ + media_reserved_),
                (total_msgs_ + media_msgs_), (total_msgs_dropped_ + media_msgs_dropped_), media_id_, pack_alive, media_packs_,
                media_recovered_, media_reserved_, media_msgs_, media_msgs_dropped_);
        }
    }

    return err;
}

srs_error_t SrsLazyGbSession::drive_state()
{
    srs_error_t err = srs_success;

    #define SRS_GB_CHANGE_STATE_TO(state) { \
        SrsGbSessionState ostate = set_state(state); \
        srs_trace("Session: Change device=%s, state=%s", sip_->resource()->device_id().c_str(), \
            srs_gb_state(ostate, state_).c_str()); \
    }

    if (state_ == SrsGbSessionStateInit) {
        // Set to connecting, whatever media is connected or not, because the connecting state will handle it if media
        // is connected, so we don't need to handle it here.
        if (sip_->resource()->is_registered()) {
            SRS_GB_CHANGE_STATE_TO(SrsGbSessionStateConnecting);
            connecting_starttime_ = srs_update_system_time();
        }

        // Invite if media is not connected.
        if (sip_->resource()->is_registered() && !media_->resource()->is_connected()) {
            uint32_t ssrc = 0;
            if ((err = sip_->resource()->invite_request(&ssrc)) != srs_success) {
                return srs_error_wrap(err, "invite");
            }

            // Now, we're able to query session by ssrc, for media packets.
            SrsLazyObjectWrapper<SrsLazyGbSession>* wrapper = wrapper_root_;
            srs_assert(wrapper); // It MUST never be NULL, because this method is in the cycle of coroutine.
            _srs_gb_manager->add_with_fast_id(ssrc, wrapper);
        }
    }

    if (state_ == SrsGbSessionStateConnecting) {
        if (srs_update_system_time() - connecting_starttime_ >= connecting_timeout_) {
            if ((nn_timeout_++) > SRS_GB_MAX_TIMEOUT) {
                return srs_error_new(ERROR_GB_TIMEOUT, "timeout");
            }

            srs_trace("Session: Connecting timeout, nn=%d, state=%s, sip=%s, media=%d", nn_timeout_, srs_gb_session_state(state_).c_str(),
                srs_gb_sip_state(sip_->resource()->state()).c_str(), media_->resource()->is_connected());
            sip_->resource()->reset_to_register();
            SRS_GB_CHANGE_STATE_TO(SrsGbSessionStateInit);
        }

        if (sip_->resource()->is_stable() && media_->resource()->is_connected()) {
            SRS_GB_CHANGE_STATE_TO(SrsGbSessionStateEstablished);
        }
    }

    if (state_ == SrsGbSessionStateEstablished) {
        if (sip_->resource()->is_bye()) {
            srs_trace("Session: Dispose for client bye");
            return err;
        }

        // When media disconnected, we wait for a while then reinvite.
        if (!media_->resource()->is_connected()) {
            if (!reinviting_starttime_) {
                reinviting_starttime_ = srs_update_system_time();
            }
            if (srs_get_system_time() - reinviting_starttime_ > reinvite_wait_) {
                reinviting_starttime_ = 0;
                srs_trace("Session: Re-invite for disconnect, state=%s, sip=%s, media=%d", srs_gb_session_state(state_).c_str(),
                    srs_gb_sip_state(sip_->resource()->state()).c_str(), media_->resource()->is_connected());
                sip_->resource()->reset_to_register();
                SRS_GB_CHANGE_STATE_TO(SrsGbSessionStateInit);
            }
        }
    }

    return err;
}

SrsGbSessionState SrsLazyGbSession::set_state(SrsGbSessionState v)
{
    SrsGbSessionState state = state_;
    state_ = v;
    return state;
}

const SrsContextId& SrsLazyGbSession::get_id()
{
    return cid_;
}

std::string SrsLazyGbSession::desc()
{
    return "GBS";
}

SrsGbListener::SrsGbListener()
{
    conf_ = NULL;
    sip_listener_ = new SrsTcpListener(this);
    media_listener_ = new SrsTcpListener(this);
}

SrsGbListener::~SrsGbListener()
{
    srs_freep(conf_);
    srs_freep(sip_listener_);
    srs_freep(media_listener_);
}

srs_error_t SrsGbListener::initialize(SrsConfDirective* conf)
{
    srs_error_t err = srs_success;

    srs_freep(conf_);
    conf_ = conf->copy();

    string ip = srs_any_address_for_listener();
    if (true) {
        int port = _srs_config->get_stream_caster_listen(conf);
        media_listener_->set_endpoint(ip, port)->set_label("GB-TCP");
    }

    bool sip_enabled = _srs_config->get_stream_caster_sip_enable(conf);
    if (!sip_enabled) {
        return srs_error_new(ERROR_GB_CONFIG, "GB SIP is required");
    }

    int port = _srs_config->get_stream_caster_sip_listen(conf);
    sip_listener_->set_endpoint(ip, port)->set_label("SIP-TCP");

    return err;
}

srs_error_t SrsGbListener::listen()
{
    srs_error_t err = srs_success;

    if ((err = media_listener_->listen()) != srs_success) {
        return srs_error_wrap(err, "listen");
    }

    if ((err = sip_listener_->listen()) != srs_success) {
        return srs_error_wrap(err, "listen");
    }

    return err;
}

void SrsGbListener::close()
{
}

srs_error_t SrsGbListener::on_tcp_client(ISrsListener* listener, srs_netfd_t stfd)
{
    srs_error_t err = srs_success;

    // Handle TCP connections.
    if (listener == sip_listener_) {
        SrsLazyObjectWrapper<SrsLazyGbSipTcpConn>* conn = new SrsLazyObjectWrapper<SrsLazyGbSipTcpConn>();
        SrsLazyGbSipTcpConn* resource = dynamic_cast<SrsLazyGbSipTcpConn*>(conn->resource());
        resource->setup(conf_, sip_listener_, media_listener_, stfd);

        if ((err = resource->start()) != srs_success) {
            srs_freep(conn);
            return srs_error_wrap(err, "gb sip");
        }

        _srs_gb_manager->add(conn, NULL);
    } else if (listener == media_listener_) {
        SrsLazyObjectWrapper<SrsLazyGbMediaTcpConn>* conn = new SrsLazyObjectWrapper<SrsLazyGbMediaTcpConn>();
        SrsLazyGbMediaTcpConn* resource = dynamic_cast<SrsLazyGbMediaTcpConn*>(conn->resource());
        resource->setup(stfd);

        if ((err = resource->start()) != srs_success) {
            srs_freep(conn);
            return srs_error_wrap(err, "gb media");
        }

        _srs_gb_manager->add(conn, NULL);
    } else {
        srs_warn("GB: Ignore TCP client");
        srs_close_stfd(stfd);
    }

    return err;
}

SrsLazyGbSipTcpConn::SrsLazyGbSipTcpConn(SrsLazyObjectWrapper<SrsLazyGbSipTcpConn>* wrapper_root)
{
    wrapper_root_ = wrapper_root;
    session_ = NULL;
    state_ = SrsGbSipStateInit;
    register_ = new SrsSipMessage();
    invite_ok_ = new SrsSipMessage();
    ssrc_v_ = 0;

    conf_ = NULL;
    sip_listener_ = NULL;
    media_listener_ = NULL;
    conn_ = NULL;
    receiver_ = NULL;
    sender_ = NULL;

    trd_ = new SrsSTCoroutine("sip", this);
}

SrsLazyGbSipTcpConn::~SrsLazyGbSipTcpConn()
{
    srs_freep(trd_);
    srs_freep(receiver_);
    srs_freep(sender_);
    srs_freep(conn_);
    srs_freep(session_);
    srs_freep(register_);
    srs_freep(invite_ok_);
    srs_freep(conf_);
}

void SrsLazyGbSipTcpConn::setup(SrsConfDirective* conf, SrsTcpListener* sip, SrsTcpListener* media, srs_netfd_t stfd)
{
    srs_freep(conf_);
    conf_ = conf->copy();

    session_ = NULL;
    sip_listener_ = sip;
    media_listener_ = media;
    conn_ = new SrsTcpConnection(stfd);
    receiver_ = new SrsLazyGbSipTcpReceiver(this, conn_);
    sender_ = new SrsLazyGbSipTcpSender(conn_);
}

std::string SrsLazyGbSipTcpConn::device_id()
{
    return register_->device_id();
}

void SrsLazyGbSipTcpConn::set_cid(const SrsContextId& cid)
{
    trd_->set_cid(cid);
    receiver_->set_cid(cid);
    sender_->set_cid(cid);
}

void SrsLazyGbSipTcpConn::query_ports(int* sip, int* media)
{
    if (sip) *sip = sip_listener_->port();
    if (media) *media = media_listener_->port();
}

srs_error_t SrsLazyGbSipTcpConn::on_sip_message(SrsSipMessage* msg)
{
    srs_error_t err = srs_success;

    // Finger out the GB session to handle SIP messages.
    if (!session_ && (err = bind_session(msg, &session_)) != srs_success) {
        return srs_error_wrap(err, "bind session");
    }

    // Ignore if session not found.
    if (!session_) {
        srs_warn("SIP: No session, drop message type=%d, id=%s, body=%s", msg->type_,
            msg->device_id().c_str(), msg->body_escaped_.c_str());
        return err;
    }

    // For state to use device id from register message.
    if (msg->is_register()) {
        srs_freep(register_); register_ = msg->copy(); // Cache the register request message.
    }

    // Drive state machine of SIP connection.
    drive_state(msg);

    // Notify session about the SIP message.
    if (msg->is_register()) {
        register_response(msg); // Response for REGISTER.
    } else if (msg->is_message()) {
        // Response for MESSAGE, the heartbeat message.
        // Set 403 to require client register, see https://www.ietf.org/rfc/rfc3261.html#section-21.4
        // Please note that it does not work for GB device, which just ignore 4xx packets like no response.
        message_response(msg, (state_ == SrsGbSipStateInit ? HTTP_STATUS_FORBIDDEN : HTTP_STATUS_OK));
    } else if (msg->is_invite_ok()) {
        srs_freep(invite_ok_);
        invite_ok_ = msg->copy(); // Cache the invite ok message.
        invite_ack(msg); // Response for INVITE OK.
    } else if (msg->is_bye()) {
        bye_response(msg); // Response for Bye OK.
    } else if (msg->is_trying() || msg->is_bye_ok()) {
        // Ignore SIP message 100(Trying).
        // Ignore BYE ok.
    } else {
        srs_warn("SIP: Ignore message type=%d, status=%d, method=%d, body=%s", msg->type_,
            msg->status_, msg->method_, msg->body_escaped_.c_str());
    }

    return err;
}

void SrsLazyGbSipTcpConn::enqueue_sip_message(SrsSipMessage* msg)
{
    // Drive state machine when enqueue message.
    drive_state(msg);

    // TODO: Support SIP transaction and wait for response for request?
    sender_->enqueue(msg);
}

void SrsLazyGbSipTcpConn::drive_state(SrsSipMessage* msg)
{
    srs_error_t err = srs_success;

    #define SRS_GB_SIP_CHANGE_STATE_TO(state) { \
        SrsGbSipState ostate = set_state(state); \
        srs_trace("SIP: Change device=%s, state=%s", register_->device_id().c_str(), \
            srs_sip_state(ostate, state_).c_str()); \
    }

    //const char* mt = msg->type_ == HTTP_REQUEST ? "REQUEST" : "RESPONSE";
    //const char* mm = msg->type_ == HTTP_REQUEST ? http_method_str(msg->method_) : "Response";
    //int ms = msg->type_ == HTTP_REQUEST ? 200 : msg->status_;
    //srs_trace("SIP: Got message type=%s, method=%s, status=%d, expire=%d", mt, mm, ms, msg->expires_);

    if (state_ == SrsGbSipStateInit) {
        // The register message, we will invite it automatically.
        if (msg->is_register() && msg->expires_ > 0) SRS_GB_SIP_CHANGE_STATE_TO(SrsGbSipStateRegistered);
        // Client bye or unregister, we should destroy the session because it might never publish again.
        if (msg->is_register() && msg->expires_ == 0) SRS_GB_SIP_CHANGE_STATE_TO(SrsGbSipStateBye);
        // When got heartbeat message, we restore to stable state.
        if (msg->is_message()) SRS_GB_SIP_CHANGE_STATE_TO(SrsGbSipStateStable);
    }

    if (state_ == SrsGbSipStateRegistered) {
        if (msg->is_invite()) SRS_GB_SIP_CHANGE_STATE_TO(SrsGbSipStateInviting);
    }

    if (state_ == SrsGbSipStateInviting) {
        if (msg->is_trying()) SRS_GB_SIP_CHANGE_STATE_TO(SrsGbSipStateTrying);
        if (msg->is_invite_ok()) SRS_GB_SIP_CHANGE_STATE_TO(SrsGbSipStateStable);

        // If device got invite request and disconnect, it might register again, we should re-invite.
        if (msg->is_register()) {
            srs_warn("SIP: Re-invite for got REGISTER in state=%s", srs_gb_sip_state(state_).c_str());
            if ((err = invite_request(NULL)) != srs_success) {
                // TODO: FIXME: Should fail the SIP session.
                srs_freep(err);
            }
        }
    }

    if (state_ == SrsGbSipStateTrying) {
        if (msg->is_invite_ok()) SRS_GB_SIP_CHANGE_STATE_TO(SrsGbSipStateStable);
    }

    if (state_ == SrsGbSipStateStable) {
        // Client bye or unregister, we should destroy the session because it might never publish again.
        if (msg->is_register() && msg->expires_ == 0) SRS_GB_SIP_CHANGE_STATE_TO(SrsGbSipStateBye);
        if (msg->is_bye()) SRS_GB_SIP_CHANGE_STATE_TO(SrsGbSipStateBye);
    }

    if (state_ == SrsGbSipStateReinviting) {
        if (msg->is_bye_ok()) SRS_GB_SIP_CHANGE_STATE_TO(SrsGbSipStateInviting);
    }
}

void SrsLazyGbSipTcpConn::register_response(SrsSipMessage* msg)
{
    SrsSipMessage* res = new SrsSipMessage();

    res->type_ = HTTP_RESPONSE;
    res->status_ = HTTP_STATUS_OK;
    res->via_ = msg->via_;
    res->from_ = msg->from_;
    res->to_ = msg->to_;
    res->cseq_ = msg->cseq_;
    res->call_id_ = msg->call_id_;
    res->contact_ = msg->contact_;
    res->expires_ = msg->expires_;

    enqueue_sip_message(res);
}

void SrsLazyGbSipTcpConn::message_response(SrsSipMessage* msg, http_status status)
{
    SrsSipMessage* res = new SrsSipMessage();

    res->type_ = HTTP_RESPONSE;
    res->status_ = status;
    res->via_ = msg->via_;
    res->from_ = msg->from_;
    res->to_ = msg->to_;
    res->cseq_ = msg->cseq_;
    res->call_id_ = msg->call_id_;

    enqueue_sip_message(res);
}

void SrsLazyGbSipTcpConn::invite_ack(SrsSipMessage* msg)
{
    string pip = session_->resource()->pip(); // Parse from CANDIDATE
    int sip_port; query_ports(&sip_port, NULL);
    string gb_device_id = srs_fmt("sip:%s@%s", msg->to_address_user_.c_str(), msg->to_address_host_.c_str());
    string branch = srs_random_str(6);

    SrsSipMessage* req = new SrsSipMessage();
    req->type_ = HTTP_REQUEST;
    req->method_ = HTTP_ACK;
    req->request_uri_ = gb_device_id;
    req->via_ = srs_fmt("SIP/2.0/TCP %s:%d;rport;branch=%s%s", pip.c_str(), sip_port, SRS_GB_BRANCH_MAGIC, branch.c_str());
    req->from_ = msg->from_;
    req->to_ = msg->to_;
    req->cseq_ = srs_fmt("%d ACK", msg->cseq_number_);
    req->call_id_ = msg->call_id_;
    req->max_forwards_ = 70;

    enqueue_sip_message(req);
}

void SrsLazyGbSipTcpConn::bye_response(SrsSipMessage* msg)
{
    SrsSipMessage* res = new SrsSipMessage();

    res->type_ = HTTP_RESPONSE;
    res->status_ = HTTP_STATUS_OK;
    res->via_ = msg->via_;
    res->from_ = msg->from_;
    res->to_ = msg->to_;
    res->cseq_ = msg->cseq_;
    res->call_id_ = msg->call_id_;

    enqueue_sip_message(res);
}

srs_error_t SrsLazyGbSipTcpConn::invite_request(uint32_t* pssrc)
{
    srs_error_t err = srs_success;

    srs_assert(register_);

    if (true) {
        // Generate SSRC, detect conflict.
        string ssrc = ssrc_str_;
        for (int i = 0; ssrc.empty() && i < 16; i++) {
            int flag = 0; // 0 is realtime.
            string ssrc_str = srs_fmt("%d%s%04d", flag, register_->ssrc_domain_id().c_str(), srs_random() % 10000);
            uint32_t ssrc_v = (uint32_t) ::atol(ssrc_str.c_str());
            if (!_srs_gb_manager->find_by_fast_id(ssrc_v)) {
                ssrc = ssrc_str;
                break;
            }
        }
        if (ssrc.empty()) {
            return srs_error_new(ERROR_GB_SSRC_GENERATE, "Generate SSRC failed");
        }

        // Update and cache the SSRC for re-invite.
        ssrc_str_ = ssrc;
        ssrc_v_ = (uint32_t) ::atol(ssrc_str_.c_str());
        if (pssrc) *pssrc = ssrc_v_;
    }

    string pip = session_->resource()->pip(); // Parse from CANDIDATE
    int sip_port, media_port; query_ports(&sip_port, &media_port);
    string srs_device_id = srs_fmt("sip:%s@%s", register_->request_uri_user_.c_str(), register_->request_uri_host_.c_str());
    string gb_device_id = srs_fmt("sip:%s@%s", register_->from_address_user_.c_str(), register_->from_address_host_.c_str());
    string subject = srs_fmt("%s:%s,%s:0", register_->from_address_user_.c_str(), ssrc_str_.c_str(), register_->request_uri_user_.c_str());
    string branch = srs_random_str(6);
    string tag = srs_random_str(8);
    string call_id = srs_random_str(16);
    int cseq = (int)(srs_random()%1000); // TODO: FIXME: Increase.

    SrsSdp local_sdp;
    local_sdp.version_ = "0";
    local_sdp.username_ = register_->contact_user_;
    local_sdp.session_id_ = "0";
    local_sdp.session_version_ = "0";
    local_sdp.nettype_ = "IN";
    local_sdp.addrtype_ = "IP4";
    local_sdp.unicast_address_ = pip; // Parse from CANDIDATE
    local_sdp.session_name_ = "Play";
    local_sdp.start_time_ = 0;
    local_sdp.end_time_ = 0;
    local_sdp.ice_lite_ = ""; // Disable this line.
    local_sdp.connection_ = srs_fmt("c=IN IP4 %s", pip.c_str()); // Session level connection.

    local_sdp.media_descs_.push_back(SrsMediaDesc("video"));
    SrsMediaDesc& media = local_sdp.media_descs_.at(0);
    media.port_ = media_port; // Read from config.
    media.protos_ = "TCP/RTP/AVP";
    media.connection_ = ""; // Disable media level connection.
    media.recvonly_ = true;

    media.payload_types_.push_back(SrsMediaPayloadType(96));
    SrsMediaPayloadType& ps = media.payload_types_.at(0);
    ps.encoding_name_ = "PS";
    ps.clock_rate_ = 90000;

    media.ssrc_infos_.push_back(SrsSSRCInfo());
    SrsSSRCInfo& ssrc_info = media.ssrc_infos_.at(0);
    ssrc_info.cname_ = ssrc_str_;
    ssrc_info.ssrc_ = ssrc_v_;
    ssrc_info.label_ = "gb28181";

    ostringstream ss;
    if ((err = local_sdp.encode(ss)) != srs_success) {
        return srs_error_wrap(err, "encode sdp");
    }

    SrsSipMessage* req = new SrsSipMessage();
    req->type_ = HTTP_REQUEST;
    req->method_ = HTTP_INVITE;
    req->request_uri_ = gb_device_id;
    req->via_ = srs_fmt("SIP/2.0/TCP %s:%d;rport;branch=%s%s", pip.c_str(), sip_port, SRS_GB_BRANCH_MAGIC, branch.c_str());
    req->from_ = srs_fmt("<%s>;tag=SRS%s", srs_device_id.c_str(), tag.c_str());
    req->to_ = srs_fmt("<%s>", gb_device_id.c_str());
    req->cseq_ = srs_fmt("%d INVITE", cseq);
    req->call_id_ = call_id;
    req->content_type_ = "Application/SDP";
    req->contact_ = srs_fmt("<%s>", srs_device_id.c_str());
    req->max_forwards_ = 70;
    req->subject_ = subject;
    req->set_body(ss.str());

    enqueue_sip_message(req);
    srs_trace("SIP: INVITE device=%s, branch=%s, tag=%s, call=%s, ssrc=%s, sdp is %s", gb_device_id.c_str(), branch.c_str(),
        tag.c_str(), call_id.c_str(), ssrc_str_.c_str(), req->body_escaped_.c_str());

    return err;
}

void SrsLazyGbSipTcpConn::interrupt()
{
    receiver_->interrupt();
    sender_->interrupt();
    trd_->interrupt();
}

SrsGbSipState SrsLazyGbSipTcpConn::state()
{
    return state_;
}

void SrsLazyGbSipTcpConn::reset_to_register()
{
    state_ = SrsGbSipStateRegistered;
}

bool SrsLazyGbSipTcpConn::is_registered()
{
    return state_ >= SrsGbSipStateRegistered && state_ <= SrsGbSipStateStable;
}

bool SrsLazyGbSipTcpConn::is_stable()
{
    return state_ == SrsGbSipStateStable;
}

bool SrsLazyGbSipTcpConn::is_bye()
{
    return state_ == SrsGbSipStateBye;
}

SrsGbSipState SrsLazyGbSipTcpConn::set_state(SrsGbSipState v)
{
    SrsGbSipState state = state_;
    state_ = v;
    return state;
}

const SrsContextId& SrsLazyGbSipTcpConn::get_id()
{
    return trd_->cid();
}

std::string SrsLazyGbSipTcpConn::desc()
{
    return "GB-SIP-TCP";
}

srs_error_t SrsLazyGbSipTcpConn::start()
{
    srs_error_t err = srs_success;

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "sip");
    }

    if ((err = receiver_->start()) != srs_success) {
        return srs_error_wrap(err, "receiver");
    }

    if ((err = sender_->start()) != srs_success) {
        return srs_error_wrap(err, "sender");
    }

    return err;
}

srs_error_t SrsLazyGbSipTcpConn::cycle()
{
    srs_error_t err = do_cycle();

    // Interrupt the receiver and sender coroutine.
    receiver_->interrupt();
    sender_->interrupt();

    // Note that we added wrapper to manager, so we must free the wrapper, not this connection.
    SrsLazyObjectWrapper<SrsLazyGbSipTcpConn>* wrapper = wrapper_root_;
    srs_assert(wrapper); // The creator wrapper MUST never be null, because we created it.
    _srs_gb_manager->remove(wrapper);

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

srs_error_t SrsLazyGbSipTcpConn::do_cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "pull");
        }

        // TODO: Handle other messages.
        srs_usleep(SRS_UTIME_NO_TIMEOUT);
    }

    return err;
}

srs_error_t SrsLazyGbSipTcpConn::bind_session(SrsSipMessage* msg, SrsLazyObjectWrapper<SrsLazyGbSession>** psession)
{
    srs_error_t err = srs_success;

    string device = msg->device_id();
    if (device.empty()) return err;

    // Only create session for REGISTER request.
    if (msg->type_ != HTTP_REQUEST || msg->method_ != HTTP_REGISTER) return err;

    // The lazy-sweep wrapper for this resource.
    SrsLazyObjectWrapper<SrsLazyGbSipTcpConn>* wrapper = wrapper_root_;
    srs_assert(wrapper); // It MUST never be NULL, because this method is in the cycle of coroutine of receiver.

    // Find exists session for register, might be created by another object and still alive.
    SrsLazyObjectWrapper<SrsLazyGbSession>* session = dynamic_cast<SrsLazyObjectWrapper<SrsLazyGbSession>*>(_srs_gb_manager->find_by_id(device));
    if (!session) {
        // Create new GB session.
        session = new SrsLazyObjectWrapper<SrsLazyGbSession>();

        if ((err = session->resource()->initialize(conf_)) != srs_success) {
            srs_freep(session);
            return srs_error_wrap(err, "initialize");
        }

        if ((err = session->resource()->start()) != srs_success) {
            srs_freep(session);
            return srs_error_wrap(err, "start");
        }

        _srs_gb_manager->add_with_id(device, session);
    }

    // Try to load state from previous SIP connection.
    SrsLazyGbSipTcpConn* pre = dynamic_cast<SrsLazyGbSipTcpConn*>(session->resource()->sip_transport()->resource());
    if (pre) {
        state_ = pre->state_;
        ssrc_str_ = pre->ssrc_str_;
        ssrc_v_ = pre->ssrc_v_;
        srs_freep(register_); register_ = pre->register_->copy();
        srs_freep(invite_ok_); invite_ok_ = pre->invite_ok_->copy();
    }

    // Notice SIP session to use current SIP connection.
    session->resource()->on_sip_transport(wrapper);
    *psession = session->copy();

    return err;
}

SrsLazyGbSipTcpReceiver::SrsLazyGbSipTcpReceiver(SrsLazyGbSipTcpConn* sip, SrsTcpConnection* conn)
{
    sip_ = sip;
    conn_ = conn;
    trd_ = new SrsSTCoroutine("sip-receiver", this);
}

SrsLazyGbSipTcpReceiver::~SrsLazyGbSipTcpReceiver()
{
    srs_freep(trd_);
}

void SrsLazyGbSipTcpReceiver::interrupt()
{
    trd_->interrupt();
}

void SrsLazyGbSipTcpReceiver::set_cid(const SrsContextId& cid)
{
    trd_->set_cid(cid);
}

srs_error_t SrsLazyGbSipTcpReceiver::start()
{
    srs_error_t err = srs_success;

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "coroutine");
    }

    return err;
}

srs_error_t SrsLazyGbSipTcpReceiver::cycle()
{
    srs_error_t err = do_cycle();

    // TODO: FIXME: Notify SIP transport to cleanup.
    if (err != srs_success) {
        srs_error("SIP: Receive err %s", srs_error_desc(err).c_str());
    }

    return err;
}

srs_error_t SrsLazyGbSipTcpReceiver::do_cycle()
{
    srs_error_t err = srs_success;

    SrsHttpParser* parser = new SrsHttpParser();
    SrsAutoFree(SrsHttpParser, parser);

    // We might get SIP request or response message.
    if ((err = parser->initialize(HTTP_BOTH)) != srs_success) {
        return srs_error_wrap(err, "init parser");
    }

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "pull");
        }

        // Use HTTP parser to parse SIP messages.
        ISrsHttpMessage* hmsg = NULL;
        SrsAutoFree(ISrsHttpMessage, hmsg);
        if ((err = parser->parse_message(conn_, &hmsg)) != srs_success) {
            return srs_error_wrap(err, "parse message");
        }

        SrsSipMessage smsg;
        if ((err = smsg.parse(hmsg)) != srs_success) {
            srs_warn("SIP: Drop msg type=%d, method=%d, err is %s", hmsg->message_type(), hmsg->method(), srs_error_summary(err).c_str());
            srs_freep(err); continue;
        }

        if ((err = sip_->on_sip_message(&smsg)) != srs_success) {
            srs_warn("SIP: Ignore on msg err %s", srs_error_desc(err).c_str());
            srs_freep(err); continue;
        }
    }

    return err;
}

SrsLazyGbSipTcpSender::SrsLazyGbSipTcpSender(SrsTcpConnection* conn)
{
    conn_ = conn;
    wait_ = srs_cond_new();
    trd_ = new SrsSTCoroutine("sip-sender", this);
}

SrsLazyGbSipTcpSender::~SrsLazyGbSipTcpSender()
{
    srs_freep(trd_);
    srs_cond_destroy(wait_);

    for (vector<SrsSipMessage*>::iterator it = msgs_.begin(); it != msgs_.end(); ++it) {
        SrsSipMessage* msg = *it;
        srs_freep(msg);
    }
}

void SrsLazyGbSipTcpSender::enqueue(SrsSipMessage* msg)
{
    msgs_.push_back(msg);
    srs_cond_signal(wait_);
}

void SrsLazyGbSipTcpSender::interrupt()
{
    trd_->interrupt();
}

void SrsLazyGbSipTcpSender::set_cid(const SrsContextId& cid)
{
    trd_->set_cid(cid);
}

srs_error_t SrsLazyGbSipTcpSender::start()
{
    srs_error_t err = srs_success;

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "coroutine");
    }

    return err;
}

srs_error_t SrsLazyGbSipTcpSender::cycle()
{
    srs_error_t err = do_cycle();

    // TODO: FIXME: Notify SIP transport to cleanup.
    if (err != srs_success) {
        srs_error("SIP: Send err %s", srs_error_desc(err).c_str());
    }

    return err;
}

srs_error_t SrsLazyGbSipTcpSender::do_cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if (msgs_.empty()) {
            srs_cond_wait(wait_);
        }

        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "pull");
        }

        SrsSipMessage* msg = msgs_.front();
        msgs_.erase(msgs_.begin());
        SrsAutoFree(SrsSipMessage, msg);

        if (msg->type_ == HTTP_RESPONSE) {
            SrsSipResponseWriter res(conn_);
            res.header()->set("Via", msg->via_);
            res.header()->set("From", msg->from_);
            res.header()->set("To", msg->to_);
            res.header()->set("CSeq", msg->cseq_);
            res.header()->set("Call-ID", msg->call_id_);
            res.header()->set("User-Agent", RTMP_SIG_SRS_SERVER);
            if (!msg->contact_.empty()) res.header()->set("Contact", msg->contact_);
            if (msg->expires_ != UINT32_MAX) res.header()->set("Expires", srs_int2str(msg->expires_));

            res.header()->set_content_length(msg->body_.length());
            res.write_header(msg->status_);
            if (!msg->body_.empty()) res.write((char*) msg->body_.c_str(), msg->body_.length());
            if ((err = res.final_request()) != srs_success) {
                return srs_error_wrap(err, "response");
            }
        } else if (msg->type_ == HTTP_REQUEST) {
            SrsSipRequestWriter req(conn_);
            req.header()->set("Via", msg->via_);
            req.header()->set("From", msg->from_);
            req.header()->set("To", msg->to_);
            req.header()->set("CSeq", msg->cseq_);
            req.header()->set("Call-ID", msg->call_id_);
            req.header()->set("User-Agent", RTMP_SIG_SRS_SERVER);
            if (!msg->contact_.empty()) req.header()->set("Contact", msg->contact_);
            if (!msg->subject_.empty()) req.header()->set("Subject", msg->subject_);
            if (msg->max_forwards_) req.header()->set("Max-Forwards", srs_int2str(msg->max_forwards_));

            if (!msg->content_type_.empty()) req.header()->set_content_type(msg->content_type_);
            req.header()->set_content_length(msg->body_.length());
            req.write_header(http_method_str(msg->method_), msg->request_uri_);
            if (!msg->body_.empty()) req.write((char*) msg->body_.c_str(), msg->body_.length());
            if ((err = req.final_request()) != srs_success) {
                return srs_error_wrap(err, "request");
            }
        } else {
            srs_warn("SIP: Sender drop message type=%d, method=%s, body=%dB", msg->type_,
                http_method_str(msg->method_), msg->body_.length());
        }
    }

    return err;
}

ISrsPsPackHandler::ISrsPsPackHandler()
{
}

ISrsPsPackHandler::~ISrsPsPackHandler()
{
}

SrsLazyGbMediaTcpConn::SrsLazyGbMediaTcpConn(SrsLazyObjectWrapper<SrsLazyGbMediaTcpConn>* wrapper_root)
{
    wrapper_root_ = wrapper_root;
    pack_ = new SrsPackContext(this);
    trd_ = new SrsSTCoroutine("media", this);
    buffer_ = new uint8_t[65535];
    conn_ = NULL;

    session_ = NULL;
    connected_ = false;
    nn_rtcp_ = 0;
}

SrsLazyGbMediaTcpConn::~SrsLazyGbMediaTcpConn()
{
    srs_freep(trd_);
    srs_freep(conn_);
    srs_freepa(buffer_);
    srs_freep(pack_);
    srs_freep(session_);
}

void SrsLazyGbMediaTcpConn::setup(srs_netfd_t stfd)
{
    srs_freep(conn_);
    conn_ = new SrsTcpConnection(stfd);
}

bool SrsLazyGbMediaTcpConn::is_connected()
{
    return connected_;
}

void SrsLazyGbMediaTcpConn::interrupt()
{
    trd_->interrupt();
}

void SrsLazyGbMediaTcpConn::set_cid(const SrsContextId& cid)
{
    trd_->set_cid(cid);
}

const SrsContextId& SrsLazyGbMediaTcpConn::get_id()
{
    return _srs_context->get_id();
}

std::string SrsLazyGbMediaTcpConn::desc()
{
    return "GB-Media-TCP";
}

srs_error_t SrsLazyGbMediaTcpConn::start()
{
    srs_error_t err = srs_success;

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "coroutine");
    }

    return err;
}

srs_error_t SrsLazyGbMediaTcpConn::cycle()
{
    srs_error_t err = do_cycle();

    // Should disconnect the TCP connection when stop cycle, especially when we stop first. In this situation, the
    // connection won't be closed because it's shared by other objects.
    srs_freep(conn_);

    // Change state to disconnected.
    connected_ = false;
    srs_trace("PS: Media disconnect, code=%d", srs_error_code(err));

    // Note that we added wrapper to manager, so we must free the wrapper, not this connection.
    SrsLazyObjectWrapper<SrsLazyGbMediaTcpConn>* wrapper = wrapper_root_;
    srs_assert(wrapper); // The creator wrapper MUST never be null, because we created it.
    _srs_gb_manager->remove(wrapper);

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

srs_error_t SrsLazyGbMediaTcpConn::do_cycle()
{
    srs_error_t err = srs_success;

    // The PS context to decode all PS packets.
    SrsRecoverablePsContext context;

    // If bytes is not enough(defined by SRS_PS_MIN_REQUIRED), ignore.
    context.ctx_.set_detect_ps_integrity(true);

    // Previous left bytes, to parse in next loop.
    uint32_t reserved = 0;

    for (;;) {
        if ((err = trd_->pull()) != srs_success) {
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
            const SrsPsDecodeHelper& h = context.ctx_.helper_;
            srs_warn("PS: Large length=%u, previous-seq=%u, previous-ts=%u", length, h.rtp_seq_, h.rtp_ts_);
        }

        // Read length of bytes of RTP packet.
        if ((err = conn_->read_fully(buffer_ + reserved, length, NULL)) != srs_success) {
            return srs_error_wrap(err, "read");
        }

        // Drop all RTCP packets.
        if (srs_is_rtcp(buffer_ + reserved, length)) {
            nn_rtcp_++; srs_warn("PS: Drop RTCP packets nn=%d", nn_rtcp_);
            continue;
        }

        // If no session, try to finger out it.
        if (!session_) {
            SrsRtpPacket rtp;
            SrsBuffer b((char*)(buffer_ + reserved), length);
            if ((err = rtp.decode(&b)) != srs_success) {
                srs_warn("PS: Ignore packet length=%d for err %s", length, srs_error_desc(err).c_str());
                srs_freep(err); // We ignore any error when decoding the RTP packet.
                continue;
            }

            if ((err = bind_session(rtp.header.get_ssrc(), &session_)) != srs_success) {
                return srs_error_wrap(err, "bind session");
            }
        }
        if (!session_) {
            srs_warn("PS: Ignore packet length=%d for no session", length);
            continue; // Ignore any media packet when no session.
        }

        // Show tips about the buffer to parse.
        if (reserved) {
            string bytes = srs_string_dumps_hex((const char*)(buffer_ + reserved), length, 16);
            srs_trace("PS: Consume reserved=%dB, length=%d, bytes=[%s]", reserved, length, bytes.c_str());
        }

        // Parse RTP over TCP, RFC4571.
        SrsBuffer b((char*)buffer_, length + reserved);
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
            string bytes = srs_string_dumps_hex(b.head(), reserved, 16);
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

srs_error_t SrsLazyGbMediaTcpConn::on_ps_pack(SrsPsPacket* ps, const std::vector<SrsTsMessage*>& msgs)
{
    srs_error_t err = srs_success;

    // Change state to connected.
    if (!connected_) {
        connected_ = true;
        srs_trace("PS: Media connected");
    }

    // Notify session about the media pack.
    session_->resource()->on_ps_pack(pack_, ps, msgs);

    //for (vector<SrsTsMessage*>::const_iterator it = msgs.begin(); it != msgs.end(); ++it) {
    //    SrsTsMessage* msg = *it;
    //    uint8_t* p = (uint8_t*)msg->payload->bytes();
    //    srs_trace("PS: Handle message %s, dts=%" PRId64 ", payload=%dB, %#x, %#x, %#x, %#x, %#x, %#x, %#x, %#x",
    //        msg->is_video() ? "Video" : "Audio", msg->dts, msg->PES_packet_length,
    //        p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
    //}

    return err;
}

srs_error_t SrsLazyGbMediaTcpConn::bind_session(uint32_t ssrc, SrsLazyObjectWrapper<SrsLazyGbSession>** psession)
{
    srs_error_t err = srs_success;

    if (!ssrc) return err;

    // The lazy-sweep wrapper for this resource.
    SrsLazyObjectWrapper<SrsLazyGbMediaTcpConn>* wrapper = wrapper_root_;
    srs_assert(wrapper); // It MUST never be NULL, because this method is in the cycle of coroutine.

    // Find exists session for register, might be created by another object and still alive.
    SrsLazyObjectWrapper<SrsLazyGbSession>* session = dynamic_cast<SrsLazyObjectWrapper<SrsLazyGbSession>*>(_srs_gb_manager->find_by_fast_id(ssrc));
    if (!session) return err;

    _srs_gb_manager->add_with_fast_id(ssrc, session);
    session->resource()->on_media_transport(wrapper);
    *psession = session->copy();

    return err;
}

SrsMpegpsQueue::SrsMpegpsQueue()
{
    nb_audios = nb_videos = 0;
}

SrsMpegpsQueue::~SrsMpegpsQueue()
{
    std::map<int64_t, SrsSharedPtrMessage*>::iterator it;
    for (it = msgs.begin(); it != msgs.end(); ++it) {
        SrsSharedPtrMessage* msg = it->second;
        srs_freep(msg);
    }
    msgs.clear();
}

srs_error_t SrsMpegpsQueue::push(SrsSharedPtrMessage* msg)
{
    srs_error_t err = srs_success;

    // TODO: FIXME: use right way.
    for (int i = 0; i < 10; i++) {
        if (msgs.find(msg->timestamp) == msgs.end()) {
            break;
        }

        // adjust the ts, add 1ms.
        msg->timestamp += 1;

        if (i >= 100) {
            srs_warn("Muxer: free the msg for dts exists, dts=%" PRId64, msg->timestamp);
            srs_freep(msg);
            return err;
        }
    }

    if (msg->is_audio()) {
        nb_audios++;
    }

    if (msg->is_video()) {
        nb_videos++;
    }

    msgs[msg->timestamp] = msg;

    return err;
}

SrsSharedPtrMessage* SrsMpegpsQueue::dequeue()
{
    // got 2+ videos and audios, ok to dequeue.
    bool av_ok = nb_videos >= 2 && nb_audios >= 2;
    // 100 videos about 30s, while 300 audios about 30s
    bool av_overflow = nb_videos > 100 || nb_audios > 300;

    if (av_ok || av_overflow) {
        std::map<int64_t, SrsSharedPtrMessage*>::iterator it = msgs.begin();
        SrsSharedPtrMessage* msg = it->second;
        msgs.erase(it);

        if (msg->is_audio()) {
            nb_audios--;
        }

        if (msg->is_video()) {
            nb_videos--;
        }

        return msg;
    }

    return NULL;
}

SrsGbMuxer::SrsGbMuxer(SrsLazyGbSession* session)
{
    sdk_ = NULL;
    session_ = session;

    avc_ = new SrsRawH264Stream();
    h264_sps_changed_ = false;
    h264_pps_changed_ = false;
    h264_sps_pps_sent_ = false;

#ifdef SRS_H265
    hevc_ = new SrsRawHEVCStream();
    vps_sps_pps_sent_ = false;
    vps_sps_pps_change_ = false;
#endif

    aac_ = new SrsRawAacStream();

    queue_ = new SrsMpegpsQueue();
    pprint_ = SrsPithyPrint::create_caster();
}

SrsGbMuxer::~SrsGbMuxer()
{
    close();

    srs_freep(avc_);
#ifdef SRS_H265
    srs_freep(hevc_);
#endif
    srs_freep(aac_);
    srs_freep(queue_);
    srs_freep(pprint_);
}

srs_error_t SrsGbMuxer::initialize(std::string output)
{
    srs_error_t err = srs_success;

    output_ = output;

    return err;
}

srs_error_t SrsGbMuxer::on_ts_message(SrsTsMessage* msg)
{
    srs_error_t err = srs_success;

    SrsBuffer avs(msg->payload->bytes(), msg->payload->length());
    if (msg->sid == SrsTsPESStreamIdVideoCommon) {
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

srs_error_t SrsGbMuxer::on_ts_video(SrsTsMessage* msg, SrsBuffer* avs)
{
    srs_error_t err = srs_success;

    // ensure rtmp connected.
    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }

    SrsPsDecodeHelper* h = (SrsPsDecodeHelper*)msg->ps_helper_;
    srs_assert(h && h->ctx_ && h->ps_);

    if (h->ctx_->video_stream_type_ == SrsTsStreamVideoH264) {
        if ((err = mux_h264(msg, avs)) != srs_success){
            return srs_error_wrap(err, "mux h264");
        }
#ifdef SRS_H265
    } else if (h->ctx_->video_stream_type_ == SrsTsStreamVideoHEVC) {
        if ((err = mux_h265(msg, avs)) != srs_success){
            return srs_error_wrap(err, "mux hevc");
        }
#endif
    } else {
        return srs_error_new(ERROR_STREAM_CASTER_TS_CODEC, "ts: unsupported stream codec=%d", h->ctx_->video_stream_type_);
    }

    return err;
}

srs_error_t SrsGbMuxer::mux_h264(SrsTsMessage *msg, SrsBuffer *avs)
{
    srs_error_t err = srs_success;

    // ts tbn to flv tbn.
    uint32_t dts = (uint32_t)(msg->dts / 90);
    uint32_t pts = (uint32_t)(msg->dts / 90);

    // send each frame.
    while (!avs->empty()) {
        char* frame = NULL;
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
            nt != SrsAvcNaluTypeNonIDR && nt != SrsAvcNaluTypeSEI && nt != SrsAvcNaluTypeAccessUnitDelimiter
        ) {
            string bytes = srs_string_dumps_hex(frame, frame_size, 4);
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
    char* flv = NULL;
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

srs_error_t SrsGbMuxer::write_h264_ipb_frame(char* frame, int frame_size, uint32_t dts, uint32_t pts)
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
    char* flv = NULL;
    int nb_flv = 0;
    if ((err = avc_->mux_avc2flv(ibp, frame_type, avc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "mux avc to flv");
    }

    // the timestamp in rtmp message header is dts.
    uint32_t timestamp = dts;
    return rtmp_write_packet(SrsFrameTypeVideo, timestamp, flv, nb_flv);
}

#ifdef SRS_H265
srs_error_t SrsGbMuxer::mux_h265(SrsTsMessage *msg, SrsBuffer *avs)
{
    srs_error_t err = srs_success;

    // ts tbn to flv tbn.
    uint32_t dts = (uint32_t)(msg->dts / 90);
    uint32_t pts = (uint32_t)(msg->dts / 90);

    // send each frame.
    while (!avs->empty()) {
        char* frame = NULL;
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

    if (!vps_sps_pps_change_){
        return err;
    }

    if (h265_vps_.empty() || h265_sps_.empty() || h265_pps_.empty()) {
        return err;
    }

    std::string sh;
    if ((err = hevc_->mux_sequence_header(h265_vps_, h265_sps_, h265_pps_, sh)) != srs_success) {
        return srs_error_wrap(err, "hevc mux sequence header");
    }

    // h265 packet to flv packet.
    int8_t frame_type = SrsVideoAvcFrameTypeKeyFrame;
    int8_t hevc_packet_type = SrsVideoAvcFrameTraitSequenceHeader;

    char* flv = NULL;
    int nb_flv = 0;

    if ((err = hevc_->mux_avc2flv(sh, frame_type, hevc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success) {
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


srs_error_t SrsGbMuxer::write_h265_ipb_frame(char* frame, int frame_size, uint32_t dts, uint32_t pts)
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
    if (nt >= SrsHevcNaluType_CODED_SLICE_BLA && nt <= SrsHevcNaluType_RESERVED_23) {
        frame_type = SrsVideoAvcFrameTypeKeyFrame;
    }

    string ipb;
    if ((err = hevc_->mux_ipb_frame(frame, frame_size, ipb)) != srs_success){
        return srs_error_wrap(err, "hevc mux ipb frame");
    }

    int8_t hevc_packet_type = SrsVideoAvcFrameTraitNALU;
    char* flv = NULL;
    int nb_flv = 0;

    if ((err = hevc_->mux_avc2flv(ipb, frame_type, hevc_packet_type, dts, pts, &flv, &nb_flv)) != srs_success) {
        return srs_error_wrap(err, "hevc to flv");
    }

    // the timestamp in rtmp message header is dts.
    uint32_t timestamp = dts;
    if (( err = rtmp_write_packet(SrsFrameTypeVideo, timestamp, flv, nb_flv)) != srs_success){
        return srs_error_wrap(err, "hevc write packet");
    }

    return err;
}
#endif

srs_error_t SrsGbMuxer::on_ts_audio(SrsTsMessage* msg, SrsBuffer* avs)
{
    srs_error_t err = srs_success;

    // ensure rtmp connected.
    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }

    // ts tbn to flv tbn.
    uint32_t dts = (uint32_t)(msg->dts / 90);

    // send each frame.
    while (!avs->empty()) {
        char* frame = NULL;
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

            codec.aac_packet_type = 0;

            if ((err = write_audio_raw_frame((char*)sh.data(), (int)sh.length(), &codec, dts)) != srs_success) {
                return srs_error_wrap(err, "write raw audio frame");
            }
        }

        // audio raw data.
        codec.aac_packet_type = 1;
        if ((err = write_audio_raw_frame(frame, frame_size, &codec, dts)) != srs_success) {
            return srs_error_wrap(err, "write audio raw frame");
        }
    }

    return err;
}

srs_error_t SrsGbMuxer::write_audio_raw_frame(char* frame, int frame_size, SrsRawAacStreamCodec* codec, uint32_t dts)
{
    srs_error_t err = srs_success;

    char* data = NULL;
    int size = 0;
    if ((err = aac_->mux_aac2flv(frame, frame_size, codec, dts, &data, &size)) != srs_success) {
        return srs_error_wrap(err, "mux aac to flv");
    }

    return rtmp_write_packet(SrsFrameTypeAudio, dts, data, size);
}

srs_error_t SrsGbMuxer::rtmp_write_packet(char type, uint32_t timestamp, char* data, int size)
{
    srs_error_t err = srs_success;

    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "connect");
    }

    SrsSharedPtrMessage* msg = NULL;

    if ((err = srs_rtmp_create_msg(type, timestamp, data, size, sdk_->sid(), &msg)) != srs_success) {
        return srs_error_wrap(err, "create message");
    }
    srs_assert(msg);

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
                      msg->is_audio()? "A":msg->is_video()? "V":"N", pprint_->age(), msg->timestamp, msg->size);
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

    string url = srs_string_replace(output_, "[stream]", session_->sip_transport()->resource()->device_id());
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

SrsSipResponseWriter::SrsSipResponseWriter(ISrsProtocolReadWriter* io) : SrsHttpResponseWriter(io)
{
}

SrsSipResponseWriter::~SrsSipResponseWriter()
{
}

srs_error_t SrsSipResponseWriter::build_first_line(std::stringstream& ss, char* data, int size)
{
    // Write status line for response.
    ss << "SIP/2.0 " << status << " " << srs_generate_http_status_text(status) << SRS_HTTP_CRLF;
    return srs_success;
}

SrsSipRequestWriter::SrsSipRequestWriter(ISrsProtocolReadWriter* io) : SrsHttpRequestWriter(io)
{
}

SrsSipRequestWriter::~SrsSipRequestWriter()
{
}

srs_error_t SrsSipRequestWriter::build_first_line(std::stringstream& ss, char* data, int size)
{
    // Write status line for response.
    ss << method_ << " " << path_ << " SIP/2.0" << SRS_HTTP_CRLF;
    return srs_success;
}

SrsSipMessage::SrsSipMessage()
{
    method_ = HTTP_GET;
    cseq_number_ = 0;
    expires_ = UINT32_MAX; // Never use 0 because it means unregister.
    max_forwards_ = 0;
    via_send_by_port_ = SRS_GB_SIP_PORT;
    contact_host_port_ = SRS_GB_SIP_PORT;
}

SrsSipMessage::~SrsSipMessage()
{
}

SrsSipMessage* SrsSipMessage::copy()
{
    SrsSipMessage* cp = new SrsSipMessage();
    *cp = *this;
    return cp;
}

const std::string& SrsSipMessage::device_id()
{
    // If request is sent by device, then the "from" address must be the ID of device. While we use id to identify the
    // requests of device, so we can use the "from" address.
    return from_address_user_;
}

std::string SrsSipMessage::ssrc_domain_id()
{
    // The request uri user is GB domain, the 4-8 is used as domain id for SSRC, so the length must be 8+ bytes,
    // see https://openstd.samr.gov.cn/bzgk/gb/newGbInfo?hcno=469659DC56B9B8187671FF08748CEC89
    return (request_uri_user_.length() < 8) ? "00000" : request_uri_user_.substr(3, 5);
}

SrsSipMessage* SrsSipMessage::set_body(std::string v)
{
    body_ = v;
    body_escaped_ = v;
    body_escaped_ = srs_string_replace(body_escaped_, "\r", "\\r");
    body_escaped_ = srs_string_replace(body_escaped_, "\n", "\\n");
    return this;
}

srs_error_t SrsSipMessage::parse(ISrsHttpMessage* m)
{
    srs_error_t err = srs_success;

    // Parse body if exists any. Note that we must read body even the message is invalid, because we might need to parse
    // the next message when skip current invalid message.
    string v;
    ISrsHttpResponseReader* br = m->body_reader();
    if (!br->eof() && (err = srs_ioutil_read_all(br, v)) != srs_success) {
        return srs_error_wrap(err, "read body");
    }

    set_body(v);

    // Parse the first line.
    type_ = (http_parser_type)m->message_type();
    if (type_ == HTTP_REQUEST) {
        // Parse request line.
        method_ = (http_method) m->method();
        request_uri_ = srs_string_trim_start(m->path(), "/");
        srs_sip_parse_address(request_uri_, request_uri_user_, request_uri_host_);
    } else if (type_ == HTTP_RESPONSE) {
        // Parse status line for response.
        status_ = (http_status)m->status_code();
    } else {
        return srs_error_new(ERROR_GB_SIP_HEADER, "Invalid message type=%d", type_);
    }

    // Check fields for SIP request.
    if (type_ == HTTP_REQUEST) {
        if (method_ < HTTP_REGISTER || method_ > HTTP_BYE) {
            return srs_error_new(ERROR_GB_SIP_MESSAGE, "Invalid method=%d(%s) of message", method_, http_method_str(method_));
        }
        if (request_uri_.empty()) return srs_error_new(ERROR_GB_SIP_MESSAGE, "No Request-URI in message");
    }

    // Get fields of SIP.
    via_ = m->header()->get("Via");
    from_ = m->header()->get("From");
    to_ = m->header()->get("To");
    call_id_ = m->header()->get("Call-ID");
    cseq_ = m->header()->get("CSeq");
    contact_ = m->header()->get("Contact");
    subject_ = m->header()->get("Subject");
    content_type_ = m->header()->content_type();

    string expires = m->header()->get("Expires");
    if (!expires.empty()) {
        expires_ = (uint32_t)::atol(expires.c_str());
        // See https://www.ietf.org/rfc/rfc3261.html#section-20.19
        if (!expires_ && expires != "0") {
            return srs_error_new(ERROR_GB_SIP_HEADER, "Invalid Expires=%s in header", expires.c_str());
        }
    }

    string max_forwards = m->header()->get("Max-Forwards");
    if (!max_forwards.empty() && max_forwards != "0") {
        max_forwards_ = (uint32_t)::atol(max_forwards.c_str());
        // See https://www.ietf.org/rfc/rfc3261.html#section-20.22
        if (!max_forwards_) {
            return srs_error_new(ERROR_GB_SIP_HEADER, "Invalid Max-Forwards=%s in header", max_forwards.c_str());
        }
    }

    if (via_.empty()) return srs_error_new(ERROR_GB_SIP_HEADER, "No Via in header");
    if (from_.empty()) return srs_error_new(ERROR_GB_SIP_HEADER, "No From in header");
    if (to_.empty()) return srs_error_new(ERROR_GB_SIP_HEADER, "No To in header");
    if (call_id_.empty()) return srs_error_new(ERROR_GB_SIP_HEADER, "No Call-ID in header");
    if (cseq_.empty()) return srs_error_new(ERROR_GB_SIP_HEADER, "No CSeq in header");

    // Parse more information from fields.
    if ((err = parse_via(via_)) != srs_success) {
        return srs_error_wrap(err, "parse via=%s", via_.c_str());
    }
    if ((err = parse_from(from_)) != srs_success) {
        return srs_error_wrap(err, "parse from=%s", from_.c_str());
    }
    if ((err = parse_to(to_)) != srs_success) {
        return srs_error_wrap(err, "parse to=%s", to_.c_str());
    }
    if ((err = parse_cseq(cseq_)) != srs_success) {
        return srs_error_wrap(err, "parse cseq=%s", cseq_.c_str());
    }
    if ((err = parse_contact(contact_)) != srs_success) {
        return srs_error_wrap(err, "parse contact=%s", contact_.c_str());
    }

    srs_sip_parse_address(from_address_, from_address_user_, from_address_host_);
    srs_sip_parse_address(to_address_, to_address_user_, to_address_host_);

    // Except REGISTER, the initial Request-URI of the message SHOULD be set to the value of the URI in the To field.
    // See https://www.ietf.org/rfc/rfc3261.html#section-8.1.1.1
    if (type_ == HTTP_REQUEST && method_ != HTTP_REGISTER && to_address_user_ != request_uri_user_) {
        return srs_error_new(ERROR_GB_SIP_HEADER, "User of Request-URI=%s not in To=%s", request_uri_.c_str(), to_.c_str());
    }

    return err;
}

srs_error_t SrsSipMessage::parse_via(const std::string& via)
{
    srs_error_t err = srs_success;

    if (!srs_string_starts_with(via, "SIP/2.0/")) {
        return srs_error_new(ERROR_GB_SIP_HEADER, "Via protocol invalid");
    }

    if (srs_string_starts_with(via, "SIP/2.0/TCP")) {
        via_transport_ = "TCP";
    } else if (srs_string_starts_with(via, "SIP/2.0/UDP")) {
        via_transport_ = "UDP";
    } else {
        return srs_error_new(ERROR_GB_SIP_HEADER, "Via transport invalid");
    }

    vector<string> vs = srs_string_split(via, " ");
    if (vs.size() <= 1) return srs_error_new(ERROR_GB_SIP_HEADER, "Via no send-by");

    vector<string> params = srs_string_split(vs[1], ";");
    if (params.size() <= 1) return srs_error_new(ERROR_GB_SIP_HEADER, "Via no params");

    via_send_by_ = params[0];
    srs_parse_hostport(via_send_by_, via_send_by_address_, via_send_by_port_);

    for (int i = 1; i < (int)params.size(); i++) {
        string param = params[i];
        if (srs_string_starts_with(param, "rport")) {
            via_rport_ = param;
        } else if (srs_string_starts_with(param, "branch")) {
            via_branch_ = param;
        }
    }

    // Before a request is sent, the client transport MUST insert a value of the "sent-by" field into the Via header
    // field. See https://www.ietf.org/rfc/rfc3261.html#section-18.1.1
    if (via_send_by_.empty()) return srs_error_new(ERROR_GB_SIP_HEADER, "Via no sent-by");
    // The Via header field value MUST contain a branch parameter.
    // See https://www.ietf.org/rfc/rfc3261.html#section-8.1.1.7
    if (via_branch_.empty()) return srs_error_new(ERROR_GB_SIP_HEADER, "Via no branch");
    // The branch ID inserted by an element compliant with this specification MUST always begin with the characters
    // "z9hG4bK". See https://www.ietf.org/rfc/rfc3261.html#section-8.1.1.7
    if (!srs_string_starts_with(via_branch_, string("branch=")+SRS_GB_BRANCH_MAGIC)) {
        return srs_error_new(ERROR_GB_SIP_HEADER, "Invalid branch=%s", via_branch_.c_str());
    }

    return err;
}

srs_error_t SrsSipMessage::parse_from(const std::string& from)
{
    srs_error_t err = srs_success;

    vector<string> params = srs_string_split(from, ";");
    if (params.size() < 2) return srs_error_new(ERROR_GB_SIP_HEADER, "From no params");

    from_address_ = params[0];
    for (int i = 1; i < (int)params.size(); i++) {
        string param = params[i];
        if (srs_string_starts_with(param, "tag")) {
            from_tag_ = param;
        }
    }

    // The From field MUST contain a new "tag" parameter, chosen by the UAC.
    // See https://www.ietf.org/rfc/rfc3261.html#section-8.1.1.3
    if (from_tag_.empty()) return srs_error_new(ERROR_GB_SIP_HEADER, "From no tag");

    return err;
}

srs_error_t SrsSipMessage::parse_to(const std::string& to)
{
    srs_error_t err = srs_success;

    vector<string> params = srs_string_split(to, ";");
    if (params.size() < 1) return srs_error_new(ERROR_GB_SIP_HEADER, "To is empty");

    to_address_ = params[0];
    for (int i = 1; i < (int)params.size(); i++) {
        string param = params[i];
        if (srs_string_starts_with(param, "tag")) {
            to_tag_ = param;
        }
    }

    return err;
}

srs_error_t SrsSipMessage::parse_cseq(const std::string& cseq)
{
    srs_error_t err = srs_success;

    vector<string> params = srs_string_split(cseq, " ");
    if (params.size() < 2) return srs_error_new(ERROR_GB_SIP_HEADER, "CSeq is empty");

    string sno = params[0];
    if (sno != "0") {
        cseq_number_ = (uint32_t)::atol(sno.c_str());

        // The sequence number MUST be expressible as a 32-bit unsigned integer.
        // See https://www.ietf.org/rfc/rfc3261.html#section-20.16
        if (!cseq_number_) return srs_error_new(ERROR_GB_SIP_HEADER, "CSeq number is invalid");
    }

    cseq_method_ = params[1];
    // The method part of CSeq is case-sensitive. See https://www.ietf.org/rfc/rfc3261.html#section-20.16
    if (type_ == HTTP_REQUEST && string(http_method_str(method_)) != cseq_method_) {
        return srs_error_new(ERROR_GB_SIP_HEADER, "CSeq method=%s is invalid, expect=%d(%s)", cseq_method_.c_str(), method_, http_method_str(method_));
    }

    return err;
}

srs_error_t SrsSipMessage::parse_contact(const std::string& contact)
{
    srs_error_t err = srs_success;

    srs_sip_parse_address(contact, contact_user_, contact_host_);
    srs_parse_hostport(contact_host_, contact_host_address_, contact_host_port_);

    return err;
}

SrsPackContext::SrsPackContext(ISrsPsPackHandler* handler)
{
    static uint32_t gid = 0;
    media_id_ = ++gid;

    media_startime_ = srs_update_system_time();
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
    for (vector<SrsTsMessage*>::iterator it = msgs_.begin(); it != msgs_.end(); ++it) {
        SrsTsMessage* msg = *it;
        srs_freep(msg);
    }

    msgs_.clear();
}

srs_error_t SrsPackContext::on_ts_message(SrsTsMessage* msg)
{
    srs_error_t err = srs_success;

    SrsPsDecodeHelper* h = (SrsPsDecodeHelper*)msg->ps_helper_;
    srs_assert(h && h->ctx_ && h->ps_);

    // We got new pack header and an optional system header.
    //if (ps_->id_ != h->ps_->id_) {
    //    stringstream ss;
    //    if (h->ps_->has_pack_header_) ss << srs_fmt(", clock=%" PRId64 ", rate=%d", h->ps_->system_clock_reference_base_, h->ps_->program_mux_rate_);
    //    if (h->ps_->has_system_header_) ss << srs_fmt(", rate_bound=%d, video_bound=%d, audio_bound=%d", h->ps_->rate_bound_, h->ps_->video_bound_, h->ps_->audio_bound_);
    //    srs_trace("PS: New pack header=%d, system=%d%s", h->ps_->has_pack_header_, h->ps_->has_system_header_, ss.str().c_str());
    //}

    // Correct DTS/PS to the last one.
    if (!msgs_.empty() && (!msg->dts || !msg->pts)) {
        SrsTsMessage* last = msgs_.back();
        if (!msg->dts) msg->dts = last->dts;
        if (!msg->pts) msg->pts = last->pts;
    }

    //uint8_t* p = (uint8_t*)msg->payload->bytes();
    //srs_trace("PS: Got message %s, dts=%" PRId64 ", seq=%u, base=%" PRId64 ", payload=%dB, %#x, %#x, %#x, %#x, %#x, %#x, %#x, %#x",
    //    msg->is_video() ? "Video" : "Audio", msg->dts, h->rtp_seq_, h->ps_->system_clock_reference_base_, msg->PES_packet_length,
    //    p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);

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

srs_error_t SrsRecoverablePsContext::decode_rtp(SrsBuffer* stream, int reserved, ISrsPsMessageHandler* handler)
{
    srs_error_t err = srs_success;

    // Start to parse from the reserved bytes.
    stream->skip(reserved);

    SrsRtpPacket rtp;
    int pos = stream->pos();
    if ((err = rtp.decode(stream)) != srs_success) {
        return enter_recover_mode(stream, handler, pos, srs_error_wrap(err, "decode rtp"));
    }

    SrsRtpRawPayload* rtp_raw = dynamic_cast<SrsRtpRawPayload*>(rtp.payload());
    srs_assert(rtp_raw); // It must be a RTP RAW payload, by default.

    // If got reserved bytes, move to the start of payload.
    if (reserved) {
        // Move the reserved bytes to the start of payload, from which we should parse.
        char* src = stream->head() - stream->pos();
        char* dst = stream->head() - reserved;
        memmove(dst, src, reserved);

        // The payload also should skip back to the reserved bytes.
        rtp_raw->payload -= reserved;
        rtp_raw->nn_payload += reserved;

        // The stream also skip back to the not parsed bytes.
        stream->skip(-1 * reserved);
    }

    SrsBuffer b((char*)rtp_raw->payload, rtp_raw->nn_payload);
    //srs_trace("GB: Got RTP length=%d, payload=%d, seq=%u, ts=%d", length, rtp_raw->nn_payload, rtp.header.get_sequence(), rtp.header.get_timestamp());

    ctx_.helper_.rtp_seq_ = rtp.header.get_sequence();
    ctx_.helper_.rtp_ts_ = rtp.header.get_timestamp();
    ctx_.helper_.rtp_pt_ = rtp.header.get_payload_type();
    if ((err = decode(&b, handler)) != srs_success) {
        return srs_error_wrap(err, "decode");
    }

    // Consume the stream, because there might be data left in stream.
    stream->skip(b.pos());

    return err;
}

srs_error_t SrsRecoverablePsContext::decode(SrsBuffer* stream, ISrsPsMessageHandler* handler)
{
    srs_error_t err = srs_success;

    // Ignore if empty packet.
    if (stream->empty()) return err;

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
#ifndef SRS_H265
    // Check stream type, error if HEVC, because not supported yet.
    if (ctx_.video_stream_type_ == SrsTsStreamVideoHEVC) {
        return srs_error_new(ERROR_GB_PS_HEADER, "HEVC is not supported");
    }
#endif
    return err;
}

srs_error_t SrsRecoverablePsContext::enter_recover_mode(SrsBuffer* stream, ISrsPsMessageHandler* handler, int pos, srs_error_t err)
{
    // Enter recover mode. Increase the recover counter because we might fail for many times.
    recover_++;

    // Print the error information for debugging.
    int npos = stream->pos();
    stream->skip(pos - stream->pos());
    string bytes = srs_string_dumps_hex(stream->head(), stream->left(), 8);

    SrsPsDecodeHelper& h = ctx_.helper_;
    uint16_t pack_seq = h.pack_first_seq_;
    uint16_t pack_msgs = h.pack_nn_msgs_;
    uint16_t lsopm = h.pack_pre_msg_last_seq_;
    SrsTsMessage* last = ctx_.last();
    srs_warn("PS: Enter recover=%d, seq=%u, ts=%u, pt=%u, pack=%u, msgs=%u, lsopm=%u, last=%u/%u, bytes=[%s], pos=%d, left=%d for err %s",
        recover_, h.rtp_seq_, h.rtp_ts_, h.rtp_pt_, pack_seq, pack_msgs, lsopm, last->PES_packet_length, last->payload->length(),
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
    SrsTsMessage* msg = ctx_.reap(); srs_freep(msg);
    // Skip all left bytes in buffer, reset error because recovered.
    stream->skip(stream->left()); srs_freep(err);

    // Notify handler to cleanup previous messages in pack.
    handler->on_recover_mode(recover_);

    return err;
}

void SrsRecoverablePsContext::quit_recover_mode(SrsBuffer* stream, ISrsPsMessageHandler* handler)
{
    string bytes = srs_string_dumps_hex(stream->head(), stream->left(), 8);
    srs_warn("PS: Quit recover=%d, seq=%u, bytes=[%s], pos=%d, left=%d", recover_, ctx_.helper_.rtp_seq_,
        bytes.c_str(), stream->pos(), stream->left());
    recover_ = 0;
}

bool srs_skip_util_pack(SrsBuffer* stream)
{
    while (stream->require(4)) {
        uint8_t* p = (uint8_t*)stream->head();

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

void srs_sip_parse_address(const std::string& address, std::string& user, std::string& host)
{
    string v = address;

    size_t pos;

    if ((pos = v.find("<")) != string::npos) {
        v = v.substr(pos + 1);
    }
    if ((pos = v.find(">")) != string::npos) {
        v = v.substr(0, pos);
    }
    if ((pos = v.find("sip:")) != string::npos) {
        v = v.substr(4);
    }

    user = v;
    if ((pos = v.find("@")) != string::npos) {
        user = v.substr(0, pos);
        host = v.substr(pos + 1);
    }
}

SrsResourceManager* _srs_gb_manager = NULL;

