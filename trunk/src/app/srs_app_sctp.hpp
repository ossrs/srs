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

public:
    // dtls_writer must outlive this object (typically SrsSecurityTransport).
    SrsSctp(ISrsDtlsCallback *dtls_writer, ISrsSctpHandler *handler = NULL);
    virtual ~SrsSctp();

public:
    void set_handler(ISrsSctpHandler *h);
    void set_stream_context(const std::string &stream);
    std::string stream_context() const;

    srs_error_t connect_peer();
    void feed(const char *buf, int nb_buf);
    // string=true → PPID 51; string=false → PPID 53 binary.
    srs_error_t send(uint16_t sid, const char *buf, int len, bool as_string = true);
    void broadcast(const char *buf, int len);

#ifdef SRS_HIKVISION
    // ISrsHikvisionTalkListener
public:
    virtual void on_talk_downlink(const std::string &talk_key, const char *data, int len);
#endif

    // usrsctp callbacks (public for C linkage).
    srs_error_t on_sctp_event(const struct sctp_rcvinfo &rcv, void *data, size_t len);
    srs_error_t on_sctp_data(const struct sctp_rcvinfo &rcv, void *data, size_t len);
    srs_error_t write_dtls(const char *data, int len);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t on_data_channel_control(const struct sctp_rcvinfo &rcv, SrsBuffer *stream);
    srs_error_t on_data_channel_msg(const struct sctp_rcvinfo &rcv, SrsBuffer *stream);
};

extern SrsSctpGlobalEnv *_srs_sctp_env;

#endif // SRS_SCTP

#endif
