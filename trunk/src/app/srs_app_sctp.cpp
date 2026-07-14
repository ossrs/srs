//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_sctp.hpp>

#ifdef SRS_SCTP

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <set>

#include <srs_app_hikvision.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_hourglass.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>

using namespace std;

// Live SrsSctp instances. usrsctp send/recv C callbacks must not touch freed objects
// (RTC dispose can freep SrsSctp while timers or in-flight usrsctp_sendv still fire).
static set<SrsSctp *> g_sctp_live;

enum SrsDataChannelMessageType {
    SrsDataChannelMessageTypeAck = 2,
    SrsDataChannelMessageTypeOpen = 3,
};

enum SrsDataChannelType {
    SrsDataChannelTypeReliable = 0x00,
    SrsDataChannelTypeReliableUnordered = 0x80,
    SrsDataChannelTypeUnreliableRexmit = 0x01,
    SrsDataChannelTypeUnreliableRexmitUnordered = 0x81,
    SrsDataChannelTypeUnreliableTimed = 0x02,
    SrsDataChannelTypeUnreliableTimedUnordered = 0x82,
};

enum SrsDataChannelPPID {
    SrsDataChannelPPIDControl = 50,
    SrsDataChannelPPIDString = 51,
    SrsDataChannelPPIDBinary = 53,
};

static uint16_t g_sctp_event_types[] = {
    SCTP_ADAPTATION_INDICATION,
    SCTP_ASSOC_CHANGE,
    SCTP_ASSOC_RESET_EVENT,
    SCTP_REMOTE_ERROR,
    SCTP_SHUTDOWN_EVENT,
    SCTP_SEND_FAILED_EVENT,
    SCTP_STREAM_RESET_EVENT,
    SCTP_STREAM_CHANGE_EVENT,
};

// WebRTC default SCTP port (a=sctp-port:5000).
static const int kSctpPort = 5000;
static const int kMaxInStream = 128;
static const int kMaxOutStream = 128;

ISrsSctpHandler::ISrsSctpHandler()
{
}

ISrsSctpHandler::~ISrsSctpHandler()
{
}

static int on_recv_sctp_data(struct socket *sock, union sctp_sockstore addr, void *data, size_t len,
                             struct sctp_rcvinfo rcv, int flags, void *ulp_info)
{
    SrsSctp *sctp = reinterpret_cast<SrsSctp *>(ulp_info);
    if (!data) {
        return 1;
    }
    if (!SrsSctp::is_live(sctp) || sctp->is_closed()) {
        free(data);
        return 1;
    }

    srs_error_t err = srs_success;
    if (flags & MSG_NOTIFICATION) {
        err = sctp->on_sctp_event(rcv, data, len);
    } else {
        err = sctp->on_sctp_data(rcv, data, len);
    }
    if (err != srs_success) {
        srs_warn("SCTP: ignore error=%s", srs_error_desc(err).c_str());
        srs_freep(err);
    }

    free(data);
    return 1;
}

static int on_send_sctp_data(void *addr, void *data, size_t len, uint8_t /*tos*/, uint8_t /*set_df*/)
{
    SrsSctp *sctp = reinterpret_cast<SrsSctp *>(addr);
    // Drop late/timer sends after RTC dispose (object freep or close()).
    if (!SrsSctp::is_live(sctp) || !data || len <= 0 || sctp->is_closed()) {
        return -1;
    }

    srs_error_t err = sctp->write_dtls(reinterpret_cast<const char *>(data), (int)len);
    if (err != srs_success) {
        // During teardown this is common; do not spam as hard failure.
        srs_info("SCTP: write DTLS failed, err=%s", srs_error_desc(err).c_str());
        srs_freep(err);
        return -1;
    }
    return 0;
}

SrsDataChannelInfo::SrsDataChannelInfo()
{
    sid_ = 0;
    channel_type_ = 0;
    reliability_params_ = 0;
    status_ = SrsDataChannelStatusClosed;
}

SrsSctpGlobalEnv::SrsSctpGlobalEnv()
{
    // Single-threaded usrsctp: no internal threads (SRS ST model).
    usrsctp_init_nothreads(0, on_send_sctp_data, NULL);

    sctp_timer_ = new SrsHourGlass("sctp", this, 200 * SRS_UTIME_MILLISECONDS);
    srs_error_t err = srs_success;
    if ((err = sctp_timer_->tick(1, 200 * SRS_UTIME_MILLISECONDS)) != srs_success) {
        srs_warn("SCTP: timer tick failed, err=%s", srs_error_desc(err).c_str());
        srs_freep(err);
    }
    if ((err = sctp_timer_->start()) != srs_success) {
        srs_warn("SCTP: timer start failed, err=%s", srs_error_desc(err).c_str());
        srs_freep(err);
    }
    srs_trace("SCTP: global env initialized (usrsctp no-threads)");
}

SrsSctpGlobalEnv::~SrsSctpGlobalEnv()
{
    srs_freep(sctp_timer_);
    usrsctp_finish();
}

srs_error_t SrsSctpGlobalEnv::notify(int /*event*/, srs_utime_t interval, srs_utime_t /*tick*/)
{
    // usrsctp_handle_timers expects milliseconds.
    usrsctp_handle_timers((uint32_t)srsu2ms(interval));
    return srs_success;
}

SrsSctpGlobalEnv *_srs_sctp_env = NULL;

SrsSctp::SrsSctp(ISrsDtlsCallback *dtls_writer, ISrsSctpHandler *handler)
{
    dtls_writer_ = dtls_writer;
    handler_ = handler;
    sctp_socket_ = NULL;
    talk_audio_sid_ = 0;
    talk_audio_sid_set_ = false;
    closed_ = false;
    busy_count_ = 0;
    orphan_ = false;
    flv_ack_got_ = 0;

    if (_srs_sctp_env == NULL) {
        _srs_sctp_env = new SrsSctpGlobalEnv();
    }

    g_sctp_live.insert(this);
    usrsctp_register_address(static_cast<void *>(this));
    sctp_socket_ = usrsctp_socket(AF_CONN, SOCK_STREAM, IPPROTO_SCTP, on_recv_sctp_data, NULL, 0,
                                  static_cast<void *>(this));
    if (!sctp_socket_) {
        srs_warn("SCTP: usrsctp_socket failed");
        return;
    }

    usrsctp_set_ulpinfo(sctp_socket_, static_cast<void *>(this));

    int ret = usrsctp_set_non_blocking(sctp_socket_, 1);
    if (ret < 0) {
        srs_warn("SCTP: set non-blocking failed ret=%d", ret);
    }

    struct sctp_assoc_value av;
    memset(&av, 0, sizeof(av));
    av.assoc_value = SCTP_ENABLE_RESET_STREAM_REQ | SCTP_ENABLE_RESET_ASSOC_REQ | SCTP_ENABLE_CHANGE_ASSOC_REQ;
    ret = usrsctp_setsockopt(sctp_socket_, IPPROTO_SCTP, SCTP_ENABLE_STREAM_RESET, &av, sizeof(av));
    if (ret < 0) {
        srs_warn("SCTP: SCTP_ENABLE_STREAM_RESET failed ret=%d", ret);
    }

    uint32_t no_delay = 1;
    ret = usrsctp_setsockopt(sctp_socket_, IPPROTO_SCTP, SCTP_NODELAY, &no_delay, sizeof(no_delay));
    if (ret < 0) {
        srs_warn("SCTP: SCTP_NODELAY failed ret=%d", ret);
    }

    // Larger send buffer for bulk FLV transfer over DataChannel (avoids early EAGAIN).
    int sndbuf = 2 * 1024 * 1024;
    ret = usrsctp_setsockopt(sctp_socket_, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    if (ret < 0) {
        srs_warn("SCTP: SO_SNDBUF failed ret=%d", ret);
    }
    int rcvbuf = 2 * 1024 * 1024;
    ret = usrsctp_setsockopt(sctp_socket_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    if (ret < 0) {
        srs_warn("SCTP: SO_RCVBUF failed ret=%d", ret);
    }

    struct sctp_event event;
    memset(&event, 0, sizeof(event));
    event.se_on = 1;
    for (size_t i = 0; i < sizeof(g_sctp_event_types) / sizeof(uint16_t); ++i) {
        event.se_type = g_sctp_event_types[i];
        ret = usrsctp_setsockopt(sctp_socket_, IPPROTO_SCTP, SCTP_EVENT, &event, sizeof(event));
        if (ret < 0) {
            srs_warn("SCTP: SCTP_EVENT type=%u failed ret=%d", g_sctp_event_types[i], ret);
        }
    }

    struct sctp_initmsg initmsg;
    memset(&initmsg, 0, sizeof(initmsg));
    initmsg.sinit_num_ostreams = kMaxOutStream;
    initmsg.sinit_max_instreams = kMaxInStream;
    ret = usrsctp_setsockopt(sctp_socket_, IPPROTO_SCTP, SCTP_INITMSG, &initmsg, sizeof(initmsg));
    if (ret < 0) {
        srs_warn("SCTP: SCTP_INITMSG failed ret=%d", ret);
    }

    struct sockaddr_conn sconn;
    memset(&sconn, 0, sizeof(sconn));
    sconn.sconn_family = AF_CONN;
    sconn.sconn_port = htons(kSctpPort);
    sconn.sconn_addr = static_cast<void *>(this);
    ret = usrsctp_bind(sctp_socket_, reinterpret_cast<struct sockaddr *>(&sconn), sizeof(sconn));
    if (ret < 0) {
        srs_warn("SCTP: bind failed ret=%d errno=%d", ret, errno);
    }
}

void SrsSctp::close()
{
    // Idempotent. Order matters: mark closed + detach writer BEFORE usrsctp_close,
    // because usrsctp_close may re-enter on_send_sctp_data on this object.
    if (closed_) {
        if (sctp_socket_) {
            usrsctp_close(sctp_socket_);
            sctp_socket_ = NULL;
        }
        return;
    }
    closed_ = true;
    // Detach transport first so late usrsctp send callbacks cannot touch freed DTLS.
    dtls_writer_ = NULL;
    handler_ = NULL;
    data_channels_.clear();
    if (sctp_socket_) {
        usrsctp_close(sctp_socket_);
        sctp_socket_ = NULL;
    }
    // Stop timer-driven sends using this address as soon as the assoc is closed.
    usrsctp_deregister_address(static_cast<void *>(this));
}

bool SrsSctp::is_closed() const
{
    return closed_;
}

bool SrsSctp::is_busy() const
{
    return busy_count_ > 0;
}

void SrsSctp::mark_orphan()
{
    orphan_ = true;
}

void SrsSctp::acquire()
{
    busy_count_++;
}

void SrsSctp::release()
{
    if (busy_count_ > 0) {
        busy_count_--;
    }
    if (busy_count_ == 0 && orphan_) {
        // Transport already gone; finish deferred delete.
        delete this;
    }
}

bool SrsSctp::is_live(SrsSctp *s)
{
    return s != NULL && g_sctp_live.find(s) != g_sctp_live.end();
}

SrsSctp::~SrsSctp()
{
    // Unregister before close so C callbacks never see a half-destroyed this.
    g_sctp_live.erase(this);
#ifdef SRS_HIKVISION
    if (_srs_hikvision) {
        _srs_hikvision->talk_remove_listener(this);
    }
#endif
    close();
    // deregister may already have run in close(); safe to call again only if still registered.
    // usrsctp docs: deregister when done; double-deregister is undefined — only if close skipped it.
    orphan_ = false;
    busy_count_ = 0;
}

void SrsSctp::set_handler(ISrsSctpHandler *h)
{
    handler_ = h;
}

void SrsSctp::set_stream_context(const string &stream)
{
    stream_context_ = stream;
}

string SrsSctp::stream_context() const
{
    return stream_context_;
}

srs_error_t SrsSctp::connect_peer()
{
    srs_error_t err = srs_success;
    if (!sctp_socket_) {
        return srs_error_new(ERROR_RTC_SCTP, "sctp socket null");
    }

    struct sockaddr_conn rconn;
    memset(&rconn, 0, sizeof(rconn));
    rconn.sconn_family = AF_CONN;
    rconn.sconn_port = htons(kSctpPort);
    rconn.sconn_addr = static_cast<void *>(this);

    int ret = usrsctp_connect(sctp_socket_, reinterpret_cast<struct sockaddr *>(&rconn), sizeof(rconn));
    if (ret < 0 && errno != EINPROGRESS) {
        return srs_error_new(ERROR_RTC_SCTP, "sctp connect ret=%d errno=%d", ret, errno);
    }

    struct sctp_paddrparams peer_addr_param;
    memset(&peer_addr_param, 0, sizeof(peer_addr_param));
    memcpy(&peer_addr_param.spp_address, &rconn, sizeof(rconn));
    peer_addr_param.spp_flags = SPP_PMTUD_DISABLE;
    peer_addr_param.spp_pathmtu = 1200 - sizeof(struct sctp_common_header);
    ret = usrsctp_setsockopt(sctp_socket_, IPPROTO_SCTP, SCTP_PEER_ADDR_PARAMS, &peer_addr_param, sizeof(peer_addr_param));
    if (ret < 0) {
        return srs_error_new(ERROR_RTC_SCTP, "sctp peer addr params ret=%d", ret);
    }

    srs_trace("SCTP: peer connect started, stream_context=%s", stream_context_.c_str());
    return err;
}

void SrsSctp::feed(const char *buf, int nb_buf)
{
    if (closed_ || !sctp_socket_ || !buf || nb_buf <= 0) {
        return;
    }
    usrsctp_conninput(this, buf, (size_t)nb_buf, 0);
}

srs_error_t SrsSctp::write_dtls(const char *data, int len)
{
    // After RTC dispose, usrsctp may still flush; ignore silently.
    // Snapshot writer: close() can run re-entrantly from SSL/network paths and null it.
    if (closed_ || !data || len <= 0) {
        return srs_success;
    }
    ISrsDtlsCallback *writer = dtls_writer_;
    if (!writer) {
        return srs_success;
    }
    if (closed_ || dtls_writer_ != writer) {
        return srs_success;
    }
    // Must SSL_write (DTLS encrypt). Raw write_dtls_data leaves SCTP unencrypted and
    // browsers never complete DataChannel (INIT retransmits forever).
    return writer->write_dtls_application_data(data, len);
}

srs_error_t SrsSctp::on_sctp_event(const struct sctp_rcvinfo & /*rcv*/, void *data, size_t len)
{
    srs_error_t err = srs_success;
    union sctp_notification *sctp_notify = reinterpret_cast<union sctp_notification *>(data);
    if (!sctp_notify || sctp_notify->sn_header.sn_length != (uint16_t)len) {
        return srs_error_new(ERROR_RTC_SCTP, "sctp notify length mismatch");
    }

    srs_info("SCTP: event type=%d", (int)sctp_notify->sn_header.sn_type);
    switch (sctp_notify->sn_header.sn_type) {
    case SCTP_ASSOC_CHANGE:
        srs_trace("SCTP: ASSOC_CHANGE");
        break;
    case SCTP_SEND_FAILED_EVENT: {
        const struct sctp_send_failed_event &ssfe = sctp_notify->sn_send_failed_event;
        srs_warn("SCTP: SEND_FAILED ppid=%u sid=%u", ntohl(ssfe.ssfe_info.snd_ppid), ssfe.ssfe_info.snd_sid);
        break;
    }
    default:
        break;
    }
    return err;
}

srs_error_t SrsSctp::on_sctp_data(const struct sctp_rcvinfo &rcv, void *data, size_t len)
{
    srs_error_t err = srs_success;
    SrsBuffer stream(static_cast<char *>(data), (int)len);

    uint32_t ppid = ntohl(rcv.rcv_ppid);
    switch (ppid) {
    case SrsDataChannelPPIDControl:
        err = on_data_channel_control(rcv, &stream);
        break;
    case SrsDataChannelPPIDString:
    case SrsDataChannelPPIDBinary:
        err = on_data_channel_msg(rcv, &stream);
        break;
    default:
        srs_info("SCTP: ignore ppid=%u len=%d", ppid, (int)len);
        break;
    }
    return err;
}

srs_error_t SrsSctp::on_data_channel_control(const struct sctp_rcvinfo &rcv, SrsBuffer *stream)
{
    srs_error_t err = srs_success;
    if (!stream->require(1)) {
        return srs_error_new(ERROR_RTC_SCTP, "dcep short");
    }

    uint8_t msg_type = stream->read_1bytes();
    if (msg_type == SrsDataChannelMessageTypeOpen) {
        // https://datatracker.ietf.org/doc/html/rfc8832#section-5.1
        if (!stream->require(11)) {
            return srs_error_new(ERROR_RTC_SCTP, "dcep open short");
        }
        uint8_t channel_type = stream->read_1bytes();
        stream->read_2bytes(); // priority
        uint32_t reliability_params = stream->read_4bytes();
        uint16_t label_length = stream->read_2bytes();
        uint16_t protocol_length = stream->read_2bytes();

        string label;
        if (label_length > 0) {
            if (!stream->require(label_length)) {
                return srs_error_new(ERROR_RTC_SCTP, "dcep label short");
            }
            label = stream->read_string(label_length);
        }
        if (protocol_length > 0) {
            if (!stream->require(protocol_length)) {
                return srs_error_new(ERROR_RTC_SCTP, "dcep protocol short");
            }
            stream->read_string(protocol_length);
        }

        SrsDataChannelInfo ch;
        ch.label_ = label;
        ch.sid_ = rcv.rcv_sid;
        ch.channel_type_ = channel_type;
        ch.reliability_params_ = reliability_params;
        ch.status_ = SrsDataChannelStatusOpen;
        data_channels_[ch.sid_] = ch;

        // Only bind talk audio sid for the audio channel — never steal control DC.
        if (label == "hik-audio") {
            talk_audio_sid_ = ch.sid_;
            talk_audio_sid_set_ = true;
        }

        srs_trace("SCTP: DataChannel OPEN sid=%u label=%s type=%u stream=%s",
                  ch.sid_, label.c_str(), channel_type, stream_context_.c_str());

        // DCEP ACK (message type 2).
        char ack = (char)SrsDataChannelMessageTypeAck;
        struct sctp_sendv_spa spa;
        memset(&spa, 0, sizeof(spa));
        spa.sendv_flags = SCTP_SEND_SNDINFO_VALID;
        spa.sendv_sndinfo.snd_sid = rcv.rcv_sid;
        spa.sendv_sndinfo.snd_ppid = htonl(SrsDataChannelPPIDControl);
        spa.sendv_sndinfo.snd_flags = SCTP_EOR;
        int ret = usrsctp_sendv(sctp_socket_, &ack, 1, NULL, 0, &spa, sizeof(spa), SCTP_SENDV_SPA, 0);
        if (ret < 0) {
            srs_warn("SCTP: send DCEP ACK failed ret=%d errno=%d", ret, errno);
        }
        return err;
    }

    if (msg_type == SrsDataChannelMessageTypeAck) {
        srs_info("SCTP: DCEP ACK sid=%u", rcv.rcv_sid);
        return err;
    }

    return srs_error_new(ERROR_RTC_SCTP, "unknown dcep type=%u", msg_type);
}

srs_error_t SrsSctp::on_data_channel_msg(const struct sctp_rcvinfo &rcv, SrsBuffer *stream)
{
    srs_error_t err = srs_success;
    string label;
    map<uint16_t, SrsDataChannelInfo>::iterator it = data_channels_.find(rcv.rcv_sid);
    if (it != data_channels_.end()) {
        label = it->second.label_;
    }

    const char *data = stream->data();
    int len = stream->size();
    uint32_t ppid = ntohl(rcv.rcv_ppid);

    if (handler_) {
        if ((err = handler_->on_datachannel_message(rcv.rcv_sid, label, data, len)) != srs_success) {
            return srs_error_wrap(err, "handler");
        }
        return err;
    }

#ifdef SRS_HIKVISION
    if (_srs_hikvision && len > 0) {
        // Binary (or label hik-audio): talk uplink G.711 frames.
        bool is_binary = (ppid == (uint32_t)SrsDataChannelPPIDBinary) || (label == "hik-audio");
        // Heuristic: non-JSON first byte for binary audio on string channel.
        if (!is_binary && len >= 2 && data[0] != '{' && data[0] != '[') {
            is_binary = true;
        }

        if (is_binary) {
            talk_audio_sid_ = rcv.rcv_sid;
            talk_audio_sid_set_ = true;
            // Pass `this` so uplink routes to the talk session this DC peer joined
            // (talk channel may differ from preview stream channel).
            if ((err = _srs_hikvision->talk_send_uplink(stream_context_, data, len, this)) != srs_success) {
                srs_warn("SCTP: talk uplink failed, err=%s", srs_error_desc(err).c_str());
                srs_freep(err);
            }
            return srs_success;
        }

        // UTF-8 JSON control (PTZ / talk / search_record).
        string json(data, len);
        srs_trace("SCTP: DataChannel ctrl sid=%u label=%s len=%d stream=%s",
                  rcv.rcv_sid, label.c_str(), len, stream_context_.c_str());
        string reply;
        if ((err = _srs_hikvision->handle_control_json(json, stream_context_, this, &reply)) != srs_success) {
            string msg = srs_error_summary(err);
            // Minimal escape for error text in JSON.
            string safe;
            for (size_t i = 0; i < msg.size(); i++) {
                char c = msg[i];
                if (c == '"' || c == '\\') {
                    safe.push_back('\\');
                }
                if (c == '\n' || c == '\r') {
                    safe.push_back(' ');
                    continue;
                }
                if ((unsigned char)c >= 0x20) {
                    safe.push_back(c);
                }
            }
            reply = string("{\"code\":-1,\"msg\":\"") + safe + "\"}";
            srs_warn("SCTP: hikvision control failed, err=%s", srs_error_desc(err).c_str());
            srs_freep(err);
            send(rcv.rcv_sid, reply.data(), (int)reply.size(), true);
            return srs_success;
        }
        // play_ack uses noreply to avoid ACK-of-ACK traffic during bulk FLV.
        if (reply == "__noreply__") {
            return err;
        }
        if (reply.empty()) {
            reply = "{\"code\":0,\"msg\":\"ok\"}";
        }
        // Large search_record replies may exceed one SCTP msg; usrsctp handles multi-chunk.
        send(rcv.rcv_sid, reply.data(), (int)reply.size(), true);
        return err;
    }
#endif

    srs_trace("SCTP: DataChannel msg sid=%u label=%s len=%d stream=%s",
              rcv.rcv_sid, label.c_str(), len, stream_context_.c_str());
    return err;
}

#ifdef SRS_HIKVISION
void SrsSctp::on_talk_downlink(const string & /*talk_key*/, const char *data, int len)
{
    if (!data || len <= 0) {
        return;
    }
    uint16_t sid = talk_audio_sid_set_ ? talk_audio_sid_ : 0;
    if (!talk_audio_sid_set_) {
        // Fall back to any open channel.
        map<uint16_t, SrsDataChannelInfo>::iterator it = data_channels_.begin();
        if (it == data_channels_.end()) {
            return;
        }
        sid = it->first;
    }
    srs_error_t err = send(sid, data, len, false);
    if (err != srs_success) {
        srs_warn("SCTP: talk downlink send failed, err=%s", srs_error_desc(err).c_str());
        srs_freep(err);
    }
}

// Control / bulk FLV: prefer labeled "hikvision" or sid 0; never prefer hik-audio.
static uint16_t srs_sctp_pick_control_sid(const map<uint16_t, SrsDataChannelInfo> &chs)
{
    map<uint16_t, SrsDataChannelInfo>::const_iterator it;
    for (it = chs.begin(); it != chs.end(); ++it) {
        if (it->second.status_ == SrsDataChannelStatusOpen && it->second.label_ == "hikvision") {
            return it->first;
        }
    }
    it = chs.find(0);
    if (it != chs.end() && it->second.status_ == SrsDataChannelStatusOpen && it->second.label_ != "hik-audio") {
        return 0;
    }
    for (it = chs.begin(); it != chs.end(); ++it) {
        if (it->second.status_ == SrsDataChannelStatusOpen && it->second.label_ != "hik-audio") {
            return it->first;
        }
    }
    for (it = chs.begin(); it != chs.end(); ++it) {
        if (it->second.status_ == SrsDataChannelStatusOpen) {
            return it->first;
        }
    }
    return 0;
}

srs_error_t SrsSctp::dc_send_text(const string &s)
{
    if (s.empty()) {
        return srs_success;
    }
    if (closed_) {
        return srs_error_new(ERROR_RTC_SCTP, "dc closed");
    }
    uint16_t sid = srs_sctp_pick_control_sid(data_channels_);
    return send(sid, s.data(), (int)s.size(), true);
}

srs_error_t SrsSctp::dc_send_binary(const char *data, int len)
{
    if (!data || len <= 0) {
        return srs_success;
    }
    if (closed_) {
        return srs_error_new(ERROR_RTC_SCTP, "dc closed");
    }
    // Bulk FLV and generic binary go on control channel (not talk audio).
    uint16_t sid = srs_sctp_pick_control_sid(data_channels_);
    return send(sid, data, len, false);
}

void SrsSctp::dc_acquire()
{
    acquire();
}

void SrsSctp::dc_release()
{
    release();
}

void SrsSctp::dc_note_play_ack(int64_t got)
{
    // got < 0 resets watermark for a new FLV transfer.
    if (got < 0) {
        flv_ack_got_ = 0;
        return;
    }
    if (got >= flv_ack_got_) {
        flv_ack_got_ = got;
    }
}

int64_t SrsSctp::dc_play_ack_got() const
{
    return flv_ack_got_;
}

bool SrsSctp::dc_alive() const
{
    return !closed_ && sctp_socket_ != NULL;
}
#endif

srs_error_t SrsSctp::send(uint16_t sid, const char *buf, int len, bool as_string)
{
    srs_error_t err = srs_success;
    if (closed_ || !sctp_socket_ || !buf || len <= 0) {
        return srs_error_new(ERROR_RTC_SCTP, "invalid send closed=%d", closed_ ? 1 : 0);
    }

    // Hold this alive across srs_usleep yields so RTC dispose cannot freep mid-send
    // (that race caused GPF in write_dtls via dangling dtls_writer_).
    acquire();

    map<uint16_t, SrsDataChannelInfo>::iterator iter = data_channels_.find(sid);
    if (iter == data_channels_.end()) {
        // Allow send before OPEN tracked (some clients); still try.
    } else if (iter->second.status_ != SrsDataChannelStatusOpen) {
        release();
        return srs_error_new(ERROR_RTC_SCTP, "channel sid=%u not open", sid);
    }

    struct sctp_sendv_spa spa;
    memset(&spa, 0, sizeof(spa));
    spa.sendv_flags = SCTP_SEND_SNDINFO_VALID;
    spa.sendv_sndinfo.snd_sid = sid;
    spa.sendv_sndinfo.snd_ppid = htonl(as_string ? SrsDataChannelPPIDString : SrsDataChannelPPIDBinary);
    spa.sendv_sndinfo.snd_flags = SCTP_EOR;
    spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_NONE;

    if (iter != data_channels_.end()) {
        const SrsDataChannelInfo &ch = iter->second;
        if (ch.channel_type_ & 0x80) {
            spa.sendv_sndinfo.snd_flags |= SCTP_UNORDERED;
        }
        if (ch.channel_type_ == SrsDataChannelTypeUnreliableRexmitUnordered ||
            ch.channel_type_ == SrsDataChannelTypeUnreliableRexmit) {
            spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_RTX;
            spa.sendv_prinfo.pr_value = ch.reliability_params_;
            spa.sendv_flags |= SCTP_SEND_PRINFO_VALID;
        } else if (ch.channel_type_ == SrsDataChannelTypeUnreliableTimedUnordered ||
                   ch.channel_type_ == SrsDataChannelTypeUnreliableTimed) {
            spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_TTL;
            spa.sendv_prinfo.pr_value = ch.reliability_params_;
            spa.sendv_flags |= SCTP_SEND_PRINFO_VALID;
        }
    }

    // Non-blocking: brief EAGAIN retry only (control/talk). Bulk FLV no longer uses DC.
    // Long retry loops previously starved ST and made the whole process appear hung.
    const int kMaxAttempts = 100; // ~1s with 10ms sleep
    int ret = -1;
    int last_errno = 0;
    for (int attempt = 0; attempt < kMaxAttempts; attempt++) {
        if (closed_ || !sctp_socket_) {
            release();
            return srs_error_new(ERROR_RTC_SCTP, "dc closed during send");
        }
        ret = usrsctp_sendv(sctp_socket_, buf, (size_t)len, NULL, 0, &spa, sizeof(spa), SCTP_SENDV_SPA, 0);
        if (ret >= 0) {
            release();
            return err;
        }
        last_errno = errno;
        if (last_errno != EAGAIN && last_errno != EWOULDBLOCK) {
            break;
        }
        usrsctp_handle_timers(10);
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
    release();
    return srs_error_new(ERROR_RTC_SCTP, "usrsctp_sendv ret=%d errno=%d len=%d after retries", ret, last_errno, len);
}

void SrsSctp::broadcast(const char *buf, int len)
{
    map<uint16_t, SrsDataChannelInfo>::iterator iter = data_channels_.begin();
    for (; iter != data_channels_.end(); ++iter) {
        srs_error_t err = send(iter->first, buf, len);
        if (err != srs_success) {
            srs_freep(err);
        }
    }
}

#endif // SRS_SCTP
