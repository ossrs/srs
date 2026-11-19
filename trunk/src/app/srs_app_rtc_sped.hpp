//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_RTC_SPED_HPP
#define SRS_APP_RTC_SPED_HPP

#include <srs_core.hpp>

#include <srs_protocol_rtc_stun.hpp>

#include <stdint.h>
#include <string>
#include <vector>

class ISrsClock;
class SrsJsonObject;

// The state of SPED (STUN Protocol for Embedding DTLS) for one session, as
// Chrome names it, see https://www.ietf.org/archive/id/draft-hancke-webrtc-sped-01.html
enum SrsSpedState {
    // Negotiated in SDP, but the peer has not yet shown it in STUN.
    SrsSpedStateTentative = 0,
    // The peer sends SPED attributes, and the DTLS handshake is in progress.
    SrsSpedStateConfirmed,
    // The DTLS handshake is done, and the last flight may still wait for an ACK.
    SrsSpedStatePending,
    SrsSpedStateComplete,
    // The peer does not use SPED, so DTLS goes raw only.
    SrsSpedStateOff,
};

extern const char *srs_sped_state_str(SrsSpedState state);

// The DTLS MTU with SPED, as Chrome's field trial sets it, so every datagram DTLS writes fits
// a STUN message with its attributes, within kStunSpedMaxBodySize.
const int kSpedDtlsMtu = 900;

// The session that starts its streams when SPED is complete.
class ISrsSpedHandler
{
public:
    ISrsSpedHandler();
    virtual ~ISrsSpedHandler();

public:
    // SPED is complete: the peer proved it finished DTLS, or SPED needs nothing more of it.
    virtual void on_sped_complete() = 0;
};

// The SPED controller keeps the DTLS datagrams to embed in STUN messages, and the
// CRCs of the DTLS datagrams received, to ACK. It has no network or DTLS code: the
// owner feeds it the datagrams DTLS writes and the attributes STUN carries.
class SrsSpedController
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsSpedState state_;
    // L1: the current flight's datagrams not yet ACKed, with their CRCs.
    std::vector<std::string> flight_;
    std::vector<uint32_t> flight_crcs_;
    // The next datagram of L1 to embed, round robin.
    size_t next_;
    // Whether the current flight is written out, so the next capture starts a new one.
    bool flight_done_;
    // L2: the CRCs of the DTLS datagrams received, oldest first.
    std::vector<uint32_t> acks_;
    // Whether the peer had not finished DTLS when this side did.
    bool wait_for_peer_;
    ISrsSpedHandler *handler_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsClock *clk_;
    // The DTLS datagrams embedded in the messages sent, the DTLS datagrams received embedded,
    // and the DTLS datagrams received raw, until SPED stops.
    int nn_embedded_sent_;
    int nn_embedded_recv_;
    int nn_raw_recv_;
    // When the first STUN message came, and when the DTLS handshake was done, 0 before.
    srs_utime_t first_stun_at_;
    srs_utime_t handshake_at_;

public:
    SrsSpedController();
    virtual ~SrsSpedController();

public:
    SrsSpedState state();
    // Set the handler told when SPED is complete, NULL for none.
    void set_handler(ISrsSpedHandler *handler);
    // Capture a datagram DTLS writes. The first capture after flush() starts a new flight.
    void capture(const char *data, int size);
    // Capture a datagram DTLS resends on its timer. It starts a new flight only when the peer
    // has ACKed every datagram of the current one, which SPED is still resending otherwise.
    void capture_retransmission(const char *data, int size);
    // Mark the current flight as written out.
    void flush();
    // Get the next datagram to embed in a message of the type, round robin.
    // Return false when there is nothing to embed.
    bool data_to_embed(SrsStunMessageType type, std::string &data);
    // Report the datagram from data_to_embed() put in a message, which it is only when it fits.
    void on_data_embedded();
    // Get the CRCs to ACK in a message of the type. The list may be empty, which still
    // tells the peer SPED is on. Return false when no ACK attribute is to be sent.
    bool acks_to_embed(SrsStunMessageType type, std::vector<uint32_t> &acks);
    // Process the SPED attributes of a received, authenticated STUN message. Return true
    // when the embedded data is a DTLS datagram that the owner must feed to DTLS.
    bool on_stun_received(bool has_data, const std::string &data, bool has_acks, const std::vector<uint32_t> &acks);
    // Report a DTLS datagram received raw, outside STUN, so it is ACKed as well.
    void on_dtls_received(const char *data, int size);
    // Report the DTLS handshake done. When clear_flight is true, the last flight needs no
    // ACK, so it is dropped. When wait_for_peer is true, the peer is not done yet, and
    // waits_for_peer() is true until SPED is complete.
    void on_handshake_done(bool clear_flight, bool wait_for_peer);
    // Report an SRTP or SRTCP packet received, which proves the peer finished DTLS.
    void on_srtp_received();

public:
    // The time from the first STUN message to the DTLS handshake done, -1 before it is done.
    srs_utime_t handshake_cost();
    // Dump the state and the counts, for the client API.
    void dumps(SrsJsonObject *obj);
    // Whether SPED still embeds and ACKs: not yet complete, and not off.
    bool is_active();
    // Whether the handshake is done, and the peer has not yet proved it is done too.
    bool waits_for_peer();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool is_binding(SrsStunMessageType type);
    void prune(const std::vector<uint32_t> &acks);
    void ack(const char *data, int size);
    void clear_flight();
    void stop(SrsSpedState state);
};

// A session that reports its SPED controller, for the client API.
class ISrsSpedSource
{
public:
    ISrsSpedSource();
    virtual ~ISrsSpedSource();

public:
    // Get the SPED controller of the network the client uses, NULL without SPED.
    virtual SrsSpedController *sped() = 0;
};

#endif
