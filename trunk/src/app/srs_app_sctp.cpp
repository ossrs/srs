/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 John
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

#include <srs_app_sctp.hpp>

using namespace std;

#include <stdarg.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include <sstream>

#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_config.hpp>
#include <srs_app_rtc_conn.hpp>

enum SrsDataChannelMessageType
{
    SrsDataChannelMessageTypeAck  = 2,
    SrsDataChannelMessageTypeOpen = 3,
};

enum SrsDataChannelType
{
    SrsDataChannelTypeReliable                  = 0x00,
    SrsDataChannelTypeReliableUnordered         = 0x80,
    SrsDataChannelTypeUnreliableRexmit          = 0x01,
    SrsDataChannelTypeUnreliableRexmitUnordered = 0x81,
    SrsDataChannelTypeUnreliableTimed           = 0x02,
    SrsDataChannelTypeUnreliableTimedUnordered  = 0x82,
};

enum SrsDataChannelPPID
{
    SrsDataChannelPPIDControl = 50,
    SrsDataChannelPPIDString  = 51,
    SrsDataChannelPPIDBinary  = 53,
};

uint16_t event_types[] =
{
    SCTP_ADAPTATION_INDICATION,
    SCTP_ASSOC_CHANGE,
    SCTP_ASSOC_RESET_EVENT,
    SCTP_REMOTE_ERROR,
    SCTP_SHUTDOWN_EVENT,
    SCTP_SEND_FAILED_EVENT,
    SCTP_STREAM_RESET_EVENT,
    SCTP_STREAM_CHANGE_EVENT
};

const int kSctpPort = 5000;
const int kMaxInSteam = 128;
const int kMaxOutStream = 128;

#ifdef SRS_DEBUG
static void sctp_debug_log(const char* format, ...)
{
	char buffer[4096];
    va_list ap;

    va_start(ap, format);
    int nb = vsnprintf(buffer, sizeof(buffer), format, ap);
    if (nb > 0) {
        if (buffer[nb - 1] == '\n') { 
            if (nb >= 2 && buffer[nb - 2] == '\r') {
                buffer[nb - 2] = '\0';
            } else {
                buffer[nb - 1] = '\0';
            }
        } else {
            buffer[nb] = '\0';
        }
        srs_trace("%s", buffer);
    }

    va_end(ap);
}
#endif

static int on_recv_sctp_data(struct socket* sock, union sctp_sockstore addr, 
    void* data, size_t len, struct sctp_rcvinfo rcv, int flags, void* ulp_inffo)
{
    srs_error_t err = srs_success;

	SrsSctp* sctp = reinterpret_cast<SrsSctp*>(ulp_inffo);
    if (flags & MSG_NOTIFICATION) {
        err = sctp->on_sctp_event(rcv, data, len);
    } else {
        err = sctp->on_sctp_data(rcv, data, len);
    }

    // TODO: FIXME: Handle error.
    if (err != srs_success) {
        srs_warn("SCTP: ignore error=%s", srs_error_desc(err).c_str());
        srs_error_reset(err);
    }

	return 1;
}

static int on_send_sctp_data(void* addr, void* data, size_t len, uint8_t tos, uint8_t set_df)
{
    srs_error_t err = srs_success;

    SrsSctp* sctp = reinterpret_cast<SrsSctp*>(addr);
    srs_assert(sctp);

    err = sctp->rtc_dtls_->send(reinterpret_cast<const char*>(data), len);

    // TODO: FIXME: Handle error.
    if (err != srs_success) {
        srs_warn("SCTP: ignore error=%s", srs_error_desc(err).c_str());
        srs_error_reset(err);
    }

    return 0;
}

SrsDataChannel::SrsDataChannel()
{
    label_ = "";
    sid_ = 0;

    channel_type_ = 0;
    reliability_params_ = 0;
    status_ = DataChannelStatusClosed;
};


SrsSctpGlobalEnv::SrsSctpGlobalEnv()
{
#ifdef SRS_DEBUG
    usrsctp_init_nothreads(0, on_send_sctp_data, sctp_debug_log);
    usrsctp_sysctl_set_sctp_debug_on(SCTP_DEBUG_ALL);
#else
    usrsctp_init_nothreads(0, on_send_sctp_data, NULL);
#endif

    // TODO: FIXME: Use one hourglass for performance issue.
    sctp_timer_ = new SrsHourGlass("sctp", this, 200 * SRS_UTIME_MILLISECONDS);

    srs_error_t err = srs_success;
	if ((err = sctp_timer_->tick(0 * SRS_UTIME_MILLISECONDS)) != srs_success) {
        srs_error_reset(err);
    }

    // TODO: FIXME: Handle error.
    // TODO: FIXME: Should not do it in constructor.
    if ((err = sctp_timer_->start()) != srs_success) {
        srs_error_reset(err);
    }
}

SrsSctpGlobalEnv::~SrsSctpGlobalEnv()
{
    usrsctp_finish();
    srs_freep(sctp_timer_);
}

srs_error_t SrsSctpGlobalEnv::notify(int type, srs_utime_t interval, srs_utime_t tick)
{
    srs_error_t err = srs_success;

    srs_info("SCTP: timer every %ums, type=%u", srsu2ms(interval), type);

    usrsctp_handle_timers(interval / 1000);

    return err;
}

SrsSctpGlobalEnv* _srs_sctp_env = NULL;

SrsSctp::SrsSctp(SrsDtls* dtls)
{
    rtc_dtls_ = dtls;

    // TODO: FIXME: Should start in server init event.
    if (_srs_sctp_env == NULL) {
        _srs_sctp_env = new SrsSctpGlobalEnv();
    }

    if (true) {
        usrsctp_register_address(static_cast<void*>(this));
	    sctp_socket = usrsctp_socket(AF_CONN, SOCK_STREAM, IPPROTO_SCTP, on_recv_sctp_data, NULL, 0, static_cast<void*>(this));

        usrsctp_set_ulpinfo(sctp_socket, static_cast<void*>(this));

        int ret = usrsctp_set_non_blocking(sctp_socket, 1);
        // TODO: FIXME: Handle error.
        if (ret < 0) {
            srs_warn("usrrsctp set non blocking failed, ret=%d", ret);
        }

        struct sctp_assoc_value av;

        av.assoc_value = SCTP_ENABLE_RESET_STREAM_REQ | SCTP_ENABLE_RESET_ASSOC_REQ | SCTP_ENABLE_CHANGE_ASSOC_REQ;
        ret = usrsctp_setsockopt(sctp_socket, IPPROTO_SCTP, SCTP_ENABLE_STREAM_RESET, &av, sizeof(av));
        // TODO: FIXME: Handle error.
        if (ret < 0) {
            srs_warn("usrrsctp set SCTP_ENABLE_STREAM_RESET failed, ret=%d", ret);
        }

        uint32_t no_delay = 1;
        ret = usrsctp_setsockopt(sctp_socket, IPPROTO_SCTP, SCTP_NODELAY, &no_delay, sizeof(no_delay));
        // TODO: FIXME: Handle error.
        if (ret < 0) {
            srs_warn("usrrsctp set SCTP_NODELAY failed, ret=%d", ret);
        }

        struct sctp_event event;
        memset(&event, 0, sizeof(event));
        event.se_on = 1;

        for (size_t i = 0; i < sizeof(event_types) / sizeof(uint16_t); ++i)
        {
            event.se_type = event_types[i];
            // TODO: FIXME: Handle error.
            ret = usrsctp_setsockopt(sctp_socket, IPPROTO_SCTP, SCTP_EVENT, &event, sizeof(event));
        	if (ret < 0) {
        	    srs_warn("usrrsctp set SCTP_NODELAY failed, ret=%d", ret);
        	}
        }

        // Init message.
        struct sctp_initmsg initmsg;
        memset(&initmsg, 0, sizeof(initmsg));
        initmsg.sinit_num_ostreams  = kMaxOutStream;
        initmsg.sinit_max_instreams = kMaxInSteam;

        // TODO: FIXME: Handle error.
        ret = usrsctp_setsockopt(sctp_socket, IPPROTO_SCTP, SCTP_INITMSG, &initmsg, sizeof(initmsg));
        if (ret < 0) {
            srs_warn("usrrsctp set SCTP_INITMSG failed, ret=%d", ret);
        }

        struct sockaddr_conn sconn;
        memset(&sconn, 0, sizeof(sconn));
        sconn.sconn_family = AF_CONN;
        sconn.sconn_port   = htons(kSctpPort);
        sconn.sconn_addr   = static_cast<void*>(this);

        // TODO: FIXME: Handle error.
        ret = usrsctp_bind(sctp_socket, reinterpret_cast<struct sockaddr*>(&sconn), sizeof(sconn));
        if (ret < 0) {
            srs_warn("usrrsctp bind failed, ret=%d", ret);
        }
    }
}

SrsSctp::~SrsSctp()
{
    if (sctp_socket) {
        usrsctp_close(sctp_socket);
    }
}

srs_error_t SrsSctp::connect_to_class()
{
    srs_error_t err = srs_success;

    struct sockaddr_conn rconn;
    memset(&rconn, 0, sizeof(rconn));
    rconn.sconn_family = AF_CONN;
    rconn.sconn_port   = htons(kSctpPort);
    rconn.sconn_addr   = static_cast<void*>(this);
    // usrsctp_connect is no socket connect, it just bind usrsctp socket to this class.
    int ret = usrsctp_connect(sctp_socket, reinterpret_cast<struct sockaddr*>(&rconn), sizeof(rconn));
    if (ret < 0 && errno != EINPROGRESS) {
        return srs_error_new(ERROR_RTC_SCTP, "sctp connect, ret=%d, errno=%d", ret, errno);
    }

    struct sctp_paddrparams peer_addr_param;
    memset(&peer_addr_param, 0, sizeof(peer_addr_param));
    memcpy(&peer_addr_param.spp_address, &rconn, sizeof(rconn));
    peer_addr_param.spp_flags = SPP_PMTUD_DISABLE;
    peer_addr_param.spp_pathmtu = 1200 - sizeof(struct sctp_common_header);

    ret = usrsctp_setsockopt(sctp_socket, IPPROTO_SCTP, SCTP_PEER_ADDR_PARAMS, &peer_addr_param, sizeof(peer_addr_param));
    if (ret < 0) {
        return srs_error_new(ERROR_RTC_SCTP, "sctp setsockopt, ret=%d", ret);
    }

    srs_trace("SCTP: connect peer success.");

    return err;
}

void SrsSctp::feed(const char* buf, const int nb_buf)
{
    usrsctp_conninput(this, buf, nb_buf, 0);
}

srs_error_t SrsSctp::on_sctp_event(const struct sctp_rcvinfo& rcv, void* data, size_t len) 
{
    srs_error_t err = srs_success;

    union sctp_notification* sctp_notify = reinterpret_cast<union sctp_notification*>(data);
    if (sctp_notify->sn_header.sn_length != len) {
        return srs_error_new(ERROR_RTC_SCTP, "sctp notify header, sn=%d, len=%d", sctp_notify->sn_header.sn_length, len);
    }

    srs_verbose("sctp event type=%d", (int)sctp_notify->sn_header.sn_type);

    switch (sctp_notify->sn_header.sn_type) {
        case SCTP_ASSOC_CHANGE:
            break;
        case SCTP_REMOTE_ERROR:
            break;
        case SCTP_SHUTDOWN_EVENT:
            break;
        case SCTP_ADAPTATION_INDICATION:
            break;
        case SCTP_PARTIAL_DELIVERY_EVENT:
            break;
        case SCTP_AUTHENTICATION_EVENT:
            break;
        case SCTP_SENDER_DRY_EVENT:
            break;
        case SCTP_NOTIFICATIONS_STOPPED_EVENT:
            break;
        case SCTP_SEND_FAILED_EVENT: {
            const struct sctp_send_failed_event& ssfe = sctp_notify->sn_send_failed_event;
            // TODO: FIXME: Return error or just print it?
            srs_error("SCTP_SEND_FAILED_EVENT, ppid=%u, sid=%u", ntohl(ssfe.ssfe_info.snd_ppid), ssfe.ssfe_info.snd_sid);
            break;
        }
        case SCTP_STREAM_RESET_EVENT:
            break;
        case SCTP_ASSOC_RESET_EVENT:
            break;
        case SCTP_STREAM_CHANGE_EVENT:
            break;
        case SCTP_PEER_ADDR_CHANGE:
            break;
        default:
            break;
    }

    return err;
}

srs_error_t SrsSctp::on_sctp_data(const struct sctp_rcvinfo& rcv, void* data, size_t len)
{
    srs_error_t err = srs_success;

    SrsBuffer* stream = new SrsBuffer(static_cast<char*>(data), len);
    SrsAutoFree(SrsBuffer, stream);

    uint32_t ppid = ntohl(rcv.rcv_ppid);
    switch (ppid) {
        case SrsDataChannelPPIDControl:
            err = on_data_channel_control(rcv, stream);
            break;
        case SrsDataChannelPPIDString:
        case SrsDataChannelPPIDBinary:
            err = on_data_channel_msg(rcv, stream);
            break;
        default:
            break;
    }
    return err;
}

srs_error_t SrsSctp::on_data_channel_control(const struct sctp_rcvinfo& rcv, SrsBuffer* stream)
{
    srs_error_t err = srs_success;

    if (! stream->require(1)) {
        return srs_error_new(ERROR_RTC_SCTP, "sctp data length invalid");
    }

    uint8_t msg_type = stream->read_1bytes();
    switch (msg_type) {
        case SrsDataChannelMessageTypeOpen: {
            if (data_channels_.count(rcv.rcv_sid)) {
                return srs_error_new(ERROR_RTC_SCTP, "data channel %d already opened", rcv.rcv_sid);
            }
            /* 
             @see: https://tools.ietf.org/html/draft-ietf-rtcweb-data-protocol-08#section-5.1
		    	 0                   1                   2                   3
     	    	 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     	    	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     	    	|  Message Type |  Channel Type |            Priority           |
     	    	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     	    	|                    Reliability Parameter                      |
     	    	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     	    	|         Label Length          |       Protocol Length         |
     	    	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     	    	\                                                               /
     	    	|                             Label                             |
     	    	/                                                               \
     	    	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     	    	\                                                               /
     	    	|                            Protocol                           |
     	    	/                                                               \
     	    	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
            */
            if (! stream->require(11)) {
                return srs_error_new(ERROR_RTC_SCTP, "sctp data length invalid");
            }

            uint8_t channel_type = stream->read_1bytes();
            /*uint16_t priority = */stream->read_2bytes();
            uint32_t reliability_params = stream->read_4bytes();
            uint16_t label_length = stream->read_2bytes();
            uint16_t protocol_length = stream->read_2bytes();

            switch (channel_type) {
                case SrsDataChannelTypeReliable:                    srs_trace("SCTP: reliable|ordered channel"); break;
                case SrsDataChannelTypeReliableUnordered:           srs_trace("SCTP: reliable|unordered channel"); break;
                case SrsDataChannelTypeUnreliableRexmit:            srs_trace("SCTP: unreliable|ordered|rtx(%u) channel", reliability_params); break;
                case SrsDataChannelTypeUnreliableRexmitUnordered:   srs_trace("SCTP: unreliable|unordered|rtx(%u) channel", reliability_params); break;
                case SrsDataChannelTypeUnreliableTimed:             srs_trace("SCTP: unreliable|ordered|life(%u) channel", reliability_params); break;
                case SrsDataChannelTypeUnreliableTimedUnordered:    srs_trace("SCTP: unreliable|unordered|life(%u) channel", reliability_params); break;
            }

            string label = "";
            if (label_length > 0) {
                if (! stream->require(label_length)) {
                    return srs_error_new(ERROR_RTC_SCTP, "label length %d", label_length);
                }

                label = stream->read_string(label_length);
            }

            if (protocol_length > 0) {
                if (! stream->require(protocol_length)) { 
                    return srs_error_new(ERROR_RTC_SCTP, "protocol length %d", protocol_length);
                }
            }

            srs_verbose("channel_type=%u, priority=%u, reliability_params=%u, label_length=%u, protocol_length=%u, label=%s",
                channel_type, priority, reliability_params, label_length, protocol_length, label.c_str());

            SrsDataChannel data_channel;
            data_channel.label_ = label;
            data_channel.sid_ = rcv.rcv_sid;
            data_channel.channel_type_ = channel_type;
            data_channel.reliability_params_ = reliability_params;
            data_channel.status_ = DataChannelStatusOpen;

            data_channels_.insert(make_pair(data_channel.sid_, data_channel));

            break;
        }
        case SrsDataChannelMessageTypeAck: {
            break;
        }
        default: {
            return srs_error_new(ERROR_RTC_SCTP, "invalid msg_type=%d", msg_type);
        }
    }

    return err;
}

srs_error_t SrsSctp::on_data_channel_msg(const struct sctp_rcvinfo& rcv, SrsBuffer* stream)
{
    srs_error_t err = srs_success;

    srs_trace("SCTP: RECV %uB MSG: %.*s", stream->size(), stream->size(), stream->data());

    // TODO: FIXME: echo test code.
    if (true) {
        // TODO: FIXME: Handle error.
        send(rcv.rcv_sid, stream->data(), stream->size());
    }

    return err;
}

srs_error_t SrsSctp::send(const uint16_t sid, const char* buf, const int len)
{
    srs_error_t err = srs_success;

    map<uint16_t, SrsDataChannel>::iterator iter = data_channels_.find(sid);
    if (iter == data_channels_.end()) {
        return srs_error_new(ERROR_RTC_SCTP, "can not found sid=%d", sid);
    }

    const SrsDataChannel& data_channel = iter->second;

    if (data_channel.status_ != DataChannelStatusOpen) {
        return srs_error_new(ERROR_RTC_SCTP, "data channel %d no opened, status=%d", sid, data_channel.status_);
    }

    struct sctp_sendv_spa spa;

    memset(&spa, 0, sizeof(spa));
    spa.sendv_flags             = SCTP_SEND_SNDINFO_VALID;
    spa.sendv_sndinfo.snd_sid   = sid;
    spa.sendv_sndinfo.snd_ppid  = htonl(SrsDataChannelPPIDString);
    spa.sendv_sndinfo.snd_flags = SCTP_EOR;

    if (data_channel.channel_type_ & 0x80) {
        spa.sendv_sndinfo.snd_flags |= SCTP_UNORDERED;
    }
    
    if (data_channel.channel_type_ == SrsDataChannelTypeUnreliableRexmitUnordered
        || data_channel.channel_type_ == SrsDataChannelTypeUnreliableRexmit) {
        spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_RTX;
        spa.sendv_prinfo.pr_value = data_channel.reliability_params_;
        spa.sendv_flags |= SCTP_SEND_PRINFO_VALID;
    } else if (data_channel.channel_type_ == SrsDataChannelTypeUnreliableTimedUnordered
        || data_channel.channel_type_ == SrsDataChannelTypeUnreliableTimed) {
        spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_TTL;
        spa.sendv_prinfo.pr_value = data_channel.reliability_params_;
        spa.sendv_flags |= SCTP_SEND_PRINFO_VALID;
    } else {
        spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_NONE;
        spa.sendv_prinfo.pr_value = 0;
    }

    // Fake IO, which call on_send_sctp_data actually.
    int ret = usrsctp_sendv(sctp_socket, buf, len, NULL, 0, &spa, sizeof(spa), SCTP_SENDV_SPA, 0);

    // If error, ret is set to -1, and the variable errno is then set appropriately.
    if (ret < 0) {
        return srs_error_new(ERROR_RTC_SCTP, "sctp notify header");
    }

    return err;
}

void SrsSctp::broadcast(const char* buf, const int len)
{
    map<uint16_t, SrsDataChannel>::iterator iter = data_channels_.begin();
    for ( ; iter != data_channels_.end(); ++iter) {
        send(iter->first, buf, len);
    }
}
