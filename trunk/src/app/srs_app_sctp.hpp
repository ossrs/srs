//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_SCTP_HPP
#define SRS_APP_SCTP_HPP

#include <srs_core.hpp>

#ifdef SRS_SCTP

#include <srs_kernel_hourglass.hpp>
#ifdef SRS_HIKVISION
#include <srs_app_hikvision.hpp>
#endif

#include <map>
#include <string>
#include <vector>

#include <usrsctp.h>

class SrsBuffer;
class ISrsHourGlass;
class ISrsDtlsCallback;

// Callback when a WebRTC DataChannel message (string/binary) is received.
class ISrsSctpHandler
{
public:
    ISrsSctpHandler();
    virtual ~ISrsSctpHandler();

public:
    // label: DataChannel label from DCEP OPEN; empty if unknown.
    // data/len: message payload (UTF-8 string for PPID 51).
    virtual srs_error_t on_datachannel_message(uint16_t sid, const std::string &label, const char *data, int len) = 0;
};

enum SrsDataChannelStatus {
    SrsDataChannelStatusClosed = 1,
    SrsDataChannelStatusOpen = 2,
};

struct SrsDataChannelInfo {
    std::string label_;
    uint16_t sid_;
    uint8_t channel_type_;
    uint32_t reliability_params_;
    SrsDataChannelStatus status_;

    SrsDataChannelInfo();
};

// Global usrsctp init + timer pump (single-threaded, no usrsctp threads).
class SrsSctpGlobalEnv : public ISrsHourGlassHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsHourGlass *sctp_timer_;

public:
    SrsSctpGlobalEnv();
    virtual ~SrsSctpGlobalEnv();

public:
    virtual srs_error_t notify(int event, srs_utime_t interval, srs_utime_t tick);
};

// Forward decl for talk downlink without requiring hikvision when SCTP-only.
class ISrsHikvisionTalkListener;

// SCTP association over DTLS for one RTC peer connection (WebRTC DataChannel).
// Also implements talk downlink sink when SRS_HIKVISION is enabled (see .cpp).
class SrsSctp
#ifdef SRS_HIKVISION
    : public ISrsHikvisionTalkListener
#endif
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Used to send SCTP packets via DTLS (write_dtls_data).
    ISrsDtlsCallback *dtls_writer_;
    ISrsSctpHandler *handler_;
    struct socket *sctp_socket_;
    std::map<uint16_t, SrsDataChannelInfo> data_channels_;
    // Stream context for control messages (e.g. SerialNO_CHANNEL_SUBCHANNEL).
    std::string stream_context_;
    // Preferred sid for binary talk audio (label hik-audio or first open channel).
    uint16_t talk_audio_sid_;
    bool talk_audio_sid_set_;
    // Set when RTC transport is disposing; late usrsctp callbacks must no-op.
    bool closed_;
    // Bulk transfer in progress (FLV over DC). Keep this object alive across srs_usleep yields.
    int busy_count_;
    // Transport destroyed while busy_; self-delete when busy reaches 0.
    bool orphan_;
    // Client FLV receive watermark (bytes) from {"cmd":"play_ack","got":N}.
    int64_t flv_ack_got_;
    // Nested depth while inside usrsctp recv callback (on_recv_sctp_data).
    // Must not call usrsctp_sendv re-entrantly → deadlock on assoc lock.
    int in_recv_callback_;
    // Depth while inside usrsctp_sendv (non-reentrant per instance).
    int in_usrsctp_send_;
    // Queued while unsafe to send; flushed after feed()/timer when idle.
    struct PendingSend {
        uint16_t sid_;
        std::string data_;
        bool as_string_;
        // true → PPID 50 DCEP control (ACK); false → string/binary data PPID.
        bool dcep_control_;
        PendingSend() : sid_(0), as_string_(true), dcep_control_(false)
        {
        }
    };
    std::vector<PendingSend> deferred_sends_;
    // App DC payloads deferred until usrsctp_conninput returns.
    // CRITICAL: never run handle_control_json / SDK / srs_usleep inside recv callback —
    // ST can switch to timer→flush→sendv while usrsctp still holds TCB lock → deadlock.
    struct PendingAppMsg {
        uint16_t sid_;
        std::string label_;
        std::string data_;
        bool is_binary_;
        PendingAppMsg() : sid_(0), is_binary_(false)
        {
        }
    };
    std::vector<PendingAppMsg> pending_app_msgs_;
    // SCTP→DTLS packets queued from on_send_sctp_data. NEVER write UDP/SSL inside
    // usrsctp (sendto may ST-yield while TCB locked → UDP feed deadlocks on TCB).
    std::vector<std::string> pending_dtls_out_;
    // Incoming DTLS/SCTP payloads if feed() while another usrsctp API is active.
    std::vector<std::string> pending_feeds_;

public:
    // dtls_writer must outlive this object (typically SrsSecurityTransport).
    SrsSctp(ISrsDtlsCallback *dtls_writer, ISrsSctpHandler *handler = NULL);
    virtual ~SrsSctp();

public:
    void set_handler(ISrsSctpHandler *h);
    void set_stream_context(const std::string &stream);
    std::string stream_context() const;
    // Detach DTLS writer and stop accepting send/feed (call before free transport).
    void close();
    bool is_closed() const;
    bool is_busy() const;
    // If busy, mark orphan instead of delete (caller must not freep).
    void mark_orphan();
    void acquire();
    void release();
    // True while this instance is registered for usrsctp send/recv callbacks.
    static bool is_live(SrsSctp *s);

    srs_error_t connect_peer();
    void feed(const char *buf, int nb_buf);
    // string=true → PPID 51; string=false → PPID 53 binary.
    srs_error_t send(uint16_t sid, const char *buf, int len, bool as_string = true);
    void broadcast(const char *buf, int len);

#ifdef SRS_HIKVISION
    // ISrsHikvisionTalkListener
public:
    virtual void on_talk_downlink(const std::string &talk_key, const char *data, int len);
    virtual srs_error_t dc_send_text(const std::string &s);
    virtual srs_error_t dc_send_binary(const char *data, int len);
    virtual void dc_acquire();
    virtual void dc_release();
    virtual void dc_note_play_ack(int64_t got);
    virtual int64_t dc_play_ack_got() const;
    virtual bool dc_alive() const;
#endif

    // usrsctp callbacks (public for C linkage).
    srs_error_t on_sctp_event(const struct sctp_rcvinfo &rcv, void *data, size_t len);
    srs_error_t on_sctp_data(const struct sctp_rcvinfo &rcv, void *data, size_t len);
    srs_error_t write_dtls(const char *data, int len);
    // Queue only (called from usrsctp output callback while locks held).
    void queue_dtls_out(const char *data, int len);
    // Enter/leave recv-callback scope (called from C on_recv_sctp_data).
    void enter_recv_callback();
    void leave_recv_callback();
    // Safe to call from any ST coroutine (DcPlayJob etc.): always queues, never usrsctp_sendv
    // re-entrantly. Flushed after DTLS feed and on SCTP timer tick.
    void queue_send(uint16_t sid, const char *buf, int len, bool as_string, bool dcep_control = false);
    // Drain deferred queue if not inside usrsctp (public for timer).
    void flush_deferred_sends();
    // Run Hikvision/control handlers queued during recv (must be outside usrsctp).
    void process_pending_app_messages();
    // Flush deferred DTLS UDP writes (must be outside usrsctp).
    void flush_dtls_out();
    // Drain pending_feeds_ then app msgs then deferred sends (outside usrsctp).
    void drain_after_usrsctp();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t on_data_channel_control(const struct sctp_rcvinfo &rcv, SrsBuffer *stream);
    srs_error_t on_data_channel_msg(const struct sctp_rcvinfo &rcv, SrsBuffer *stream);
    // Actually call usrsctp_sendv (never from inside recv callback / nested send).
    srs_error_t send_now(uint16_t sid, const char *buf, int len, bool as_string, bool dcep_control);
    bool usrsctp_send_safe() const;
    srs_error_t dispatch_app_message(uint16_t sid, const std::string &label, const char *data, int len,
                                     bool is_binary);
    void do_conninput(const char *buf, int nb_buf);
};

extern SrsSctpGlobalEnv *_srs_sctp_env;

#endif // SRS_SCTP

#endif
