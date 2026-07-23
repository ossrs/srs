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

// Global depth of any usrsctp API that may hold assoc/INP locks (conninput / handle_timers / sendv).
// ST must never enter sendv/conninput from another coroutine while this is > 0.
// NOTE: even usrsctp_init_nothreads still spawns "SCTP iterator" pthread; it mostly waits,
// but TCB locks are non-recursive — yield while holding them (DTLS UDP send) deadlocks feed.
static int g_usrsctp_api_depth = 0;
// Re-entrancy guard for drain_after_usrsctp (process_pending may srs_usleep).
static int g_sctp_draining = 0;

static void srs_sctp_enter_api(const char *where)
{
    g_usrsctp_api_depth++;
    if (g_usrsctp_api_depth > 1) {
        srs_warn("SCTP: re-enter usrsctp api depth=%d via %s", g_usrsctp_api_depth, where ? where : "?");
    } else {
        srs_info("SCTP: enter usrsctp api via %s depth=%d", where ? where : "?", g_usrsctp_api_depth);
    }
}

static void srs_sctp_leave_api(const char *where)
{
    if (g_usrsctp_api_depth > 0) {
        g_usrsctp_api_depth--;
    }
    srs_info("SCTP: leave usrsctp api via %s depth=%d", where ? where : "?", g_usrsctp_api_depth);
}

static bool srs_sctp_api_idle()
{
    return g_usrsctp_api_depth == 0;
}

// After leaving usrsctp: flush deferred DTLS for all live peers (UDP/SSL may ST-yield safely).
static void srs_sctp_flush_all_dtls_out()
{
    for (set<SrsSctp *>::iterator it = g_sctp_live.begin(); it != g_sctp_live.end(); ++it) {
        if (*it) {
            (*it)->flush_dtls_out();
        }
    }
}

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

    // usrsctp holds association locks here — never usrsctp_sendv re-entrantly.
    sctp->enter_recv_callback();

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

    sctp->leave_recv_callback();
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

    // CRITICAL: usrsctp holds TCB/INP locks here. SSL_write → UDP sendto can ST-yield;
    // then UDP coroutine feed()→conninput waits forever on SCTP_TCB_LOCK.
    // Only copy into deferred queue; real DTLS write runs after leave_api.
    sctp->queue_dtls_out(reinterpret_cast<const char *>(data), (int)len);
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
    // CRITICAL: never usrsctp_handle_timers() then usrsctp_sendv() in the same call —
    // that deadlocks in sctp_lower_sosend (assoc lock). Alternate ticks instead.
    // Never enter usrsctp if another path is mid API (g_usrsctp_api_depth).
    static int s_sctp_tick = 0;
    s_sctp_tick++;
    if ((s_sctp_tick & 1) == 0) {
        if (!srs_sctp_api_idle()) {
            srs_warn("SCTP: timer skip flush (usrsctp busy depth=%d deferred peers=%d)", g_usrsctp_api_depth,
                     (int)g_sctp_live.size());
            return srs_success;
        }
        // Even ticks: drain deferred DC sends + any leftover DTLS outs.
        for (set<SrsSctp *>::iterator it = g_sctp_live.begin(); it != g_sctp_live.end(); ++it) {
            if (*it) {
                (*it)->flush_deferred_sends();
                (*it)->flush_dtls_out();
            }
        }
    } else {
        if (!srs_sctp_api_idle()) {
            srs_warn("SCTP: timer skip handle_timers (usrsctp busy depth=%d)", g_usrsctp_api_depth);
            return srs_success;
        }
        // Odd ticks: usrsctp timers only (may queue DTLS via on_send).
        srs_sctp_enter_api("handle_timers");
        usrsctp_handle_timers((uint32_t)srsu2ms(interval));
        srs_sctp_leave_api("handle_timers");
        srs_sctp_flush_all_dtls_out();
    }
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
    in_recv_callback_ = 0;
    in_usrsctp_send_ = 0;
    deferred_sends_.clear();
    pending_app_msgs_.clear();
    pending_dtls_out_.clear();
    pending_feeds_.clear();

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
    deferred_sends_.clear();
    pending_app_msgs_.clear();
    pending_dtls_out_.clear();
    pending_feeds_.clear();
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

void SrsSctp::do_conninput(const char *buf, int nb_buf)
{
    if (closed_ || !sctp_socket_ || !buf || nb_buf <= 0) {
        return;
    }
    srs_sctp_enter_api("conninput");
    usrsctp_conninput(this, buf, (size_t)nb_buf, 0);
    srs_sctp_leave_api("conninput");
    // SACK/DATA may have queued DTLS packets while TCB was locked — flush now.
    flush_dtls_out();
}

void SrsSctp::feed(const char *buf, int nb_buf)
{
    if (closed_ || !sctp_socket_ || !buf || nb_buf <= 0) {
        return;
    }
    // If another ST path is mid usrsctp (or we re-enter from drain), only queue.
    // Holds no locks — safe to copy; drained after that path leaves usrsctp.
    if (!srs_sctp_api_idle() || g_sctp_draining) {
        if (pending_feeds_.size() < 64) {
            pending_feeds_.push_back(string(buf, nb_buf));
            srs_trace("SCTP: feed deferred len=%d api=%d drain=%d q=%d", nb_buf, g_usrsctp_api_depth,
                      g_sctp_draining, (int)pending_feeds_.size());
        } else {
            srs_warn("SCTP: feed drop (queue full) len=%d", nb_buf);
        }
        return;
    }

    // 1) conninput only: recv queues DCEP ACK + app msgs; output → pending_dtls_out_.
    // 2) Outside locks: DTLS write, app dispatch (may srs_usleep), deferred DC sends.
    do_conninput(buf, nb_buf);
    if (closed_) {
        return;
    }
    drain_after_usrsctp();
}

void SrsSctp::drain_after_usrsctp()
{
    if (closed_ || g_sctp_draining) {
        return;
    }
    g_sctp_draining = 1;

    // Loop: process_pending may srs_usleep; other feeds queue into pending_feeds_.
    // sendv queues more DTLS; drain until quiet or closed.
    for (int guard = 0; guard < 32 && !closed_; guard++) {
        bool work = false;

        if (!pending_feeds_.empty() && srs_sctp_api_idle()) {
            vector<string> feeds;
            feeds.swap(pending_feeds_);
            srs_trace("SCTP: drain pending_feeds n=%d", (int)feeds.size());
            for (size_t i = 0; i < feeds.size() && !closed_; i++) {
                do_conninput(feeds[i].data(), (int)feeds[i].size());
            }
            work = true;
        }

        if (!pending_app_msgs_.empty()) {
            process_pending_app_messages();
            work = true;
        }

        if (!deferred_sends_.empty() && usrsctp_send_safe()) {
            flush_deferred_sends();
            work = true;
        }

        flush_dtls_out();

        if (!work && pending_feeds_.empty() && pending_app_msgs_.empty() && deferred_sends_.empty() &&
            pending_dtls_out_.empty()) {
            break;
        }
        if (!work) {
            break;
        }
    }

    g_sctp_draining = 0;
}

void SrsSctp::enter_recv_callback()
{
    in_recv_callback_++;
    srs_info("SCTP: enter_recv depth=%d api=%d", in_recv_callback_, g_usrsctp_api_depth);
}

void SrsSctp::leave_recv_callback()
{
    if (in_recv_callback_ > 0) {
        in_recv_callback_--;
    }
    srs_info("SCTP: leave_recv depth=%d api=%d pending_app=%d deferred=%d", in_recv_callback_, g_usrsctp_api_depth,
             (int)pending_app_msgs_.size(), (int)deferred_sends_.size());
}

bool SrsSctp::usrsctp_send_safe() const
{
    // Global api depth covers conninput/handle_timers on any peer; local flags cover this assoc.
    return in_recv_callback_ == 0 && in_usrsctp_send_ == 0 && !closed_ && srs_sctp_api_idle();
}

// DataChannel over DTLS: keep one SCTP message well under path MTU / usrsctp comfort.
// Large search_record JSON (16KB+) deadlocks usrsctp_sendv on this build.
static const int kSctpMaxAppMsg = 3500;

void SrsSctp::queue_send(uint16_t sid, const char *buf, int len, bool as_string, bool dcep_control)
{
    if (closed_ || !buf || len <= 0) {
        return;
    }
    PendingSend p;
    p.sid_ = sid;
    p.as_string_ = as_string;
    p.dcep_control_ = dcep_control;
    if (!dcep_control && as_string && len > kSctpMaxAppMsg) {
        // Truncate JSON safely for control path (prefer small error over deadlock).
        static const char *kTooBig =
            "{\"code\":-1,\"msg\":\"reply too large for DataChannel; reduce search max\"}";
        p.data_.assign(kTooBig);
        srs_warn("SCTP: drop oversized DC text len=%d sid=%u (max=%d)", len, sid, kSctpMaxAppMsg);
    } else if (len > kSctpMaxAppMsg && !dcep_control) {
        srs_warn("SCTP: drop oversized DC binary len=%d sid=%u", len, sid);
        return;
    } else {
        p.data_.assign(buf, len);
    }
    deferred_sends_.push_back(p);
    srs_trace("SCTP: queue_send sid=%u len=%d str=%d dcep=%d q=%d safe=%d api=%d recv=%d", sid, len,
              as_string ? 1 : 0, dcep_control ? 1 : 0, (int)deferred_sends_.size(), usrsctp_send_safe() ? 1 : 0,
              g_usrsctp_api_depth, in_recv_callback_);
}

void SrsSctp::flush_deferred_sends()
{
    if (closed_ || deferred_sends_.empty()) {
        return;
    }
    if (!usrsctp_send_safe()) {
        srs_warn("SCTP: flush skip n=%d (safe=0 api=%d recv=%d send=%d closed=%d)", (int)deferred_sends_.size(),
                 g_usrsctp_api_depth, in_recv_callback_, in_usrsctp_send_, closed_ ? 1 : 0);
        return;
    }
    // Swap out so nested cannot re-process; send_now re-queues if still unsafe.
    vector<PendingSend> pending;
    pending.swap(deferred_sends_);
    srs_trace("SCTP: flush begin n=%d", (int)pending.size());
    for (size_t i = 0; i < pending.size(); i++) {
        if (closed_) {
            break;
        }
        if (!usrsctp_send_safe()) {
            // Put remainder back (including current).
            deferred_sends_.insert(deferred_sends_.begin(), pending.begin() + i, pending.end());
            srs_warn("SCTP: flush pause at %d/%d requeue=%d api=%d", (int)i, (int)pending.size(),
                     (int)deferred_sends_.size(), g_usrsctp_api_depth);
            break;
        }
        const PendingSend &p = pending[i];
        srs_trace("SCTP: flush item %d/%d sid=%u len=%d dcep=%d", (int)i + 1, (int)pending.size(), p.sid_,
                  (int)p.data_.size(), p.dcep_control_ ? 1 : 0);
        srs_error_t err = send_now(p.sid_, p.data_.data(), (int)p.data_.size(), p.as_string_, p.dcep_control_);
        if (err != srs_success) {
            srs_warn("SCTP: deferred send failed sid=%u len=%d err=%s", p.sid_, (int)p.data_.size(),
                     srs_error_desc(err).c_str());
            srs_freep(err);
        }
    }
    srs_trace("SCTP: flush end left=%d", (int)deferred_sends_.size());
}

void SrsSctp::queue_dtls_out(const char *data, int len)
{
    if (closed_ || !data || len <= 0) {
        return;
    }
    if (pending_dtls_out_.size() >= 256) {
        srs_warn("SCTP: drop dtls out (queue full) len=%d", len);
        return;
    }
    pending_dtls_out_.push_back(string(data, len));
    srs_info("SCTP: queue_dtls_out len=%d q=%d api=%d", len, (int)pending_dtls_out_.size(), g_usrsctp_api_depth);
}

void SrsSctp::flush_dtls_out()
{
    if (closed_ || pending_dtls_out_.empty()) {
        return;
    }
    // Only write when not inside usrsctp (UDP/SSL must not run under TCB lock).
    if (!srs_sctp_api_idle()) {
        srs_warn("SCTP: flush_dtls_out skip n=%d api=%d", (int)pending_dtls_out_.size(), g_usrsctp_api_depth);
        return;
    }
    vector<string> pending;
    pending.swap(pending_dtls_out_);
    srs_trace("SCTP: flush_dtls_out n=%d", (int)pending.size());
    for (size_t i = 0; i < pending.size(); i++) {
        if (closed_) {
            break;
        }
        srs_error_t err = write_dtls(pending[i].data(), (int)pending[i].size());
        if (err != srs_success) {
            srs_info("SCTP: write DTLS failed, err=%s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
    }
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

        // DCEP ACK (message type 2). Must not usrsctp_sendv while still in recv callback.
        char ack = (char)SrsDataChannelMessageTypeAck;
        if ((err = send_now(rcv.rcv_sid, &ack, 1, false, true)) != srs_success) {
            // send_now queues if in_recv; only real failures reach here after flush.
            srs_warn("SCTP: queue/send DCEP ACK failed, err=%s", srs_error_desc(err).c_str());
            srs_freep(err);
            err = srs_success;
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
    // Runs under usrsctp_conninput with TCB lock held — only enqueue; never SDK / usleep / sendv.
    srs_error_t err = srs_success;
    string label;
    map<uint16_t, SrsDataChannelInfo>::iterator it = data_channels_.find(rcv.rcv_sid);
    if (it != data_channels_.end()) {
        label = it->second.label_;
    }

    const char *data = stream->data();
    int len = stream->size();
    if (!data || len <= 0) {
        return err;
    }
    uint32_t ppid = ntohl(rcv.rcv_ppid);

    bool is_binary = (ppid == (uint32_t)SrsDataChannelPPIDBinary) || (label == "hik-audio");
    if (!is_binary && len >= 2 && data[0] != '{' && data[0] != '[') {
        is_binary = true;
    }

    PendingAppMsg m;
    m.sid_ = rcv.rcv_sid;
    m.label_ = label;
    m.data_.assign(data, len);
    m.is_binary_ = is_binary;
    pending_app_msgs_.push_back(m);
    srs_trace("SCTP: defer app msg sid=%u label=%s len=%d bin=%d q=%d stream=%s", rcv.rcv_sid, label.c_str(), len,
              is_binary ? 1 : 0, (int)pending_app_msgs_.size(), stream_context_.c_str());
    return err;
}

void SrsSctp::process_pending_app_messages()
{
    if (closed_ || pending_app_msgs_.empty()) {
        return;
    }
    vector<PendingAppMsg> pending;
    pending.swap(pending_app_msgs_);
    srs_trace("SCTP: process_pending_app n=%d", (int)pending.size());
    for (size_t i = 0; i < pending.size(); i++) {
        if (closed_) {
            break;
        }
        const PendingAppMsg &m = pending[i];
        srs_error_t err =
            dispatch_app_message(m.sid_, m.label_, m.data_.data(), (int)m.data_.size(), m.is_binary_);
        if (err != srs_success) {
            srs_warn("SCTP: dispatch app msg failed sid=%u len=%d err=%s", m.sid_, (int)m.data_.size(),
                     srs_error_desc(err).c_str());
            srs_freep(err);
        }
    }
}

srs_error_t SrsSctp::dispatch_app_message(uint16_t sid, const string &label, const char *data, int len,
                                          bool is_binary)
{
    srs_error_t err = srs_success;
    if (!data || len <= 0) {
        return err;
    }

    if (handler_) {
        if ((err = handler_->on_datachannel_message(sid, label, data, len)) != srs_success) {
            return srs_error_wrap(err, "handler");
        }
        return err;
    }

#ifdef SRS_HIKVISION
    if (_srs_hikvision) {
        if (is_binary) {
            talk_audio_sid_ = sid;
            talk_audio_sid_set_ = true;
            // Pass `this` so uplink routes to the talk session this DC peer joined.
            if ((err = _srs_hikvision->talk_send_uplink(stream_context_, data, len, this)) != srs_success) {
                srs_warn("SCTP: talk uplink failed, err=%s", srs_error_desc(err).c_str());
                srs_freep(err);
            }
            return srs_success;
        }

        // UTF-8 JSON control (PTZ / talk / search_record) — may srs_usleep / SDK block.
        string json(data, len);
        srs_trace("SCTP: DataChannel ctrl sid=%u label=%s len=%d stream=%s", sid, label.c_str(), len,
                  stream_context_.c_str());
        string reply;
        if ((err = _srs_hikvision->handle_control_json(json, stream_context_, this, &reply)) != srs_success) {
            string msg = srs_error_summary(err);
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
            send(sid, reply.data(), (int)reply.size(), true);
            return srs_success;
        }
        if (reply == "__noreply__") {
            return err;
        }
        if (reply.empty()) {
            reply = "{\"code\":0,\"msg\":\"ok\"}";
        }
        srs_trace("SCTP: ctrl reply sid=%u len=%d", sid, (int)reply.size());
        send(sid, reply.data(), (int)reply.size(), true);
        return err;
    }
#endif

    srs_trace("SCTP: DataChannel msg sid=%u label=%s len=%d stream=%s", sid, label.c_str(), len,
              stream_context_.c_str());
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
    // Always queue first. Flush only when usrsctp is idle (timer/feed also drain).
    queue_send(sid, s.data(), (int)s.size(), true, false);
    if (usrsctp_send_safe()) {
        flush_deferred_sends();
    } else {
        srs_trace("SCTP: dc_send_text queued only (not safe) len=%d api=%d", (int)s.size(), g_usrsctp_api_depth);
    }
    return srs_success;
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
    queue_send(sid, data, len, false, false);
    if (usrsctp_send_safe()) {
        flush_deferred_sends();
    } else {
        srs_trace("SCTP: dc_send_binary queued only (not safe) len=%d api=%d", len, g_usrsctp_api_depth);
    }
    return srs_success;
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
    return send_now(sid, buf, len, as_string, false);
}

srs_error_t SrsSctp::send_now(uint16_t sid, const char *buf, int len, bool as_string, bool dcep_control)
{
    srs_error_t err = srs_success;
    if (closed_ || !sctp_socket_ || !buf || len <= 0) {
        return srs_error_new(ERROR_RTC_SCTP, "invalid send closed=%d", closed_ ? 1 : 0);
    }

    // Never re-enter usrsctp while recv callback or another sendv holds the assoc lock.
    if (!usrsctp_send_safe()) {
        srs_trace("SCTP: send_now defer sid=%u len=%d (api=%d recv=%d send=%d)", sid, len, g_usrsctp_api_depth,
                  in_recv_callback_, in_usrsctp_send_);
        queue_send(sid, buf, len, as_string, dcep_control);
        return err;
    }

    // Hold this alive across srs_usleep yields so RTC dispose cannot freep mid-send
    // (that race caused GPF in write_dtls via dangling dtls_writer_).
    acquire();
    in_usrsctp_send_++;
    srs_sctp_enter_api("sendv");

    map<uint16_t, SrsDataChannelInfo>::iterator iter = data_channels_.find(sid);
    if (!dcep_control) {
        if (iter == data_channels_.end()) {
            // Allow send before OPEN tracked (some clients); still try.
        } else if (iter->second.status_ != SrsDataChannelStatusOpen) {
            srs_sctp_leave_api("sendv");
            in_usrsctp_send_--;
            release();
            return srs_error_new(ERROR_RTC_SCTP, "channel sid=%u not open", sid);
        }
    }

    struct sctp_sendv_spa spa;
    memset(&spa, 0, sizeof(spa));
    spa.sendv_flags = SCTP_SEND_SNDINFO_VALID;
    spa.sendv_sndinfo.snd_sid = sid;
    if (dcep_control) {
        spa.sendv_sndinfo.snd_ppid = htonl(SrsDataChannelPPIDControl);
    } else {
        spa.sendv_sndinfo.snd_ppid = htonl(as_string ? SrsDataChannelPPIDString : SrsDataChannelPPIDBinary);
    }
    spa.sendv_sndinfo.snd_flags = SCTP_EOR;
    spa.sendv_prinfo.pr_policy = SCTP_PR_SCTP_NONE;

    if (!dcep_control && iter != data_channels_.end()) {
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

    // Single attempt — no timer re-entry / usleep while holding in_usrsctp_send_ (deadlock risk).
    int ret = -1;
    int last_errno = 0;
    srs_trace("SCTP: usrsctp_sendv begin sid=%u len=%d dcep=%d", sid, len, dcep_control ? 1 : 0);
    if (!closed_ && sctp_socket_) {
        ret = usrsctp_sendv(sctp_socket_, buf, (size_t)len, NULL, 0, &spa, sizeof(spa), SCTP_SENDV_SPA, 0);
        if (ret < 0) {
            last_errno = errno;
        }
    }
    srs_trace("SCTP: usrsctp_sendv end sid=%u len=%d ret=%d errno=%d", sid, len, ret, last_errno);
    srs_sctp_leave_api("sendv");
    in_usrsctp_send_--;
    // DTLS packets queued during sendv — flush while not holding usrsctp locks.
    flush_dtls_out();
    release();

    if (ret >= 0) {
        return err;
    }
    // EAGAIN: re-queue for timer/feed flush (do not spin with usrsctp_handle_timers here).
    if (last_errno == EAGAIN || last_errno == EWOULDBLOCK) {
        queue_send(sid, buf, len, as_string, dcep_control);
        return err;
    }
    return srs_error_new(ERROR_RTC_SCTP, "usrsctp_sendv ret=%d errno=%d len=%d", ret, last_errno, len);
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
