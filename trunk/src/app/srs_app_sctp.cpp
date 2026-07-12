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

#include <srs_app_hikvision.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_hourglass.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>

using namespace std;

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
    if (!sctp || !data) {
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
    if (!sctp || !data || len <= 0) {
        return 0;
    }

    srs_error_t err = sctp->write_dtls(reinterpret_cast<const char *>(data), (int)len);
    if (err != srs_success) {
        srs_warn("SCTP: write DTLS failed, err=%s", srs_error_desc(err).c_str());
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

    if (_srs_sctp_env == NULL) {
        _srs_sctp_env = new SrsSctpGlobalEnv();
    }

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

SrsSctp::~SrsSctp()
{
    if (sctp_socket_) {
        usrsctp_close(sctp_socket_);
        sctp_socket_ = NULL;
    }
    usrsctp_deregister_address(static_cast<void *>(this));
    dtls_writer_ = NULL;
    handler_ = NULL;
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
    if (!buf || nb_buf <= 0) {
        return;
    }
    usrsctp_conninput(this, buf, (size_t)nb_buf, 0);
}

srs_error_t SrsSctp::write_dtls(const char *data, int len)
{
    if (!dtls_writer_ || !data || len <= 0) {
        return srs_error_new(ERROR_RTC_SCTP, "invalid dtls write");
    }
    return dtls_writer_->write_dtls_data((void *)data, len);
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
    srs_trace("SCTP: DataChannel msg sid=%u label=%s len=%d stream=%s",
              rcv.rcv_sid, label.c_str(), len, stream_context_.c_str());

    if (handler_) {
        if ((err = handler_->on_datachannel_message(rcv.rcv_sid, label, data, len)) != srs_success) {
            return srs_error_wrap(err, "handler");
        }
        return err;
    }

#ifdef SRS_HIKVISION
    // Default: treat UTF-8 JSON as Hikvision PTZ control when no custom handler.
    if (_srs_hikvision && len > 0) {
        string json(data, len);
        if ((err = _srs_hikvision->handle_control_json(json, stream_context_)) != srs_success) {
            // Reply error text to the same channel.
            string reply = string("{\"code\":-1,\"msg\":\"") + srs_error_summary(err) + "\"}";
            srs_warn("SCTP: hikvision control failed, err=%s", srs_error_desc(err).c_str());
            srs_freep(err);
            send(rcv.rcv_sid, reply.data(), (int)reply.size());
            return srs_success;
        }
        const char *ok = "{\"code\":0,\"msg\":\"ok\"}";
        send(rcv.rcv_sid, ok, (int)strlen(ok));
        return err;
    }
#endif

    return err;
}

srs_error_t SrsSctp::send(uint16_t sid, const char *buf, int len)
{
    srs_error_t err = srs_success;
    if (!sctp_socket_ || !buf || len <= 0) {
        return srs_error_new(ERROR_RTC_SCTP, "invalid send");
    }

    map<uint16_t, SrsDataChannelInfo>::iterator iter = data_channels_.find(sid);
    if (iter == data_channels_.end()) {
        // Allow send before OPEN tracked (some clients); still try.
    } else if (iter->second.status_ != SrsDataChannelStatusOpen) {
        return srs_error_new(ERROR_RTC_SCTP, "channel sid=%u not open", sid);
    }

    struct sctp_sendv_spa spa;
    memset(&spa, 0, sizeof(spa));
    spa.sendv_flags = SCTP_SEND_SNDINFO_VALID;
    spa.sendv_sndinfo.snd_sid = sid;
    spa.sendv_sndinfo.snd_ppid = htonl(SrsDataChannelPPIDString);
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

    int ret = usrsctp_sendv(sctp_socket_, buf, (size_t)len, NULL, 0, &spa, sizeof(spa), SCTP_SENDV_SPA, 0);
    if (ret < 0) {
        return srs_error_new(ERROR_RTC_SCTP, "usrsctp_sendv ret=%d errno=%d", ret, errno);
    }
    return err;
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
