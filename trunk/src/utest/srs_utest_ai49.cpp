//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai49.hpp>

#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_app_rtc_network.hpp>
#include <srs_app_rtc_sped.hpp>
#include <srs_app_statistic.hpp>
#include <srs_kernel_error.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_rtc_stun.hpp>
#include <srs_utest_ai11.hpp>
#include <srs_utest_ai23.hpp>
#include <srs_utest_ai32.hpp>
#include <srs_utest_ai37.hpp>
#include <srs_utest_manual_mock.hpp>

#include <algorithm>
#include <openssl/ssl.h>
#include <string.h>
#include <string>
#include <unistd.h>
#include <vector>

using namespace std;

// A DTLS 1.2 record of the content type, with a tag in its body so each datagram has
// its own bytes and CRC.
static string sped_dtls_datagram(uint8_t content_type, const string &tag)
{
    string datagram;
    datagram.push_back((char)content_type);
    datagram.push_back((char)0xfe);
    datagram.push_back((char)0xfd);
    datagram.append(tag);
    return datagram;
}

static string sped_handshake(const string &tag)
{
    return sped_dtls_datagram(22, tag);
}

static uint32_t sped_crc(const string &datagram)
{
    return srs_crc32_ieee(datagram.data(), (int)datagram.size());
}

static void sped_capture(SrsSpedController &sped, const string &datagram)
{
    sped.capture(datagram.data(), (int)datagram.size());
}

static vector<uint32_t> sped_acks(SrsSpedController &sped)
{
    vector<uint32_t> acks;
    EXPECT_TRUE(sped.acks_to_embed(BindingResponse, acks));
    return acks;
}

static string sped_next(SrsSpedController &sped)
{
    string data;
    EXPECT_TRUE(sped.data_to_embed(BindingResponse, data));
    return data;
}

// What a Binding request carries when the client sends neither SPED attribute.
static bool sped_receive_plain(SrsSpedController &sped)
{
    return sped.on_stun_received(false, "", false, vector<uint32_t>());
}

// What Chrome 158's first check carries: an empty DTLS-IN-STUN-ACK and no DATA.
static bool sped_receive_empty_ack(SrsSpedController &sped)
{
    return sped.on_stun_received(false, "", true, vector<uint32_t>());
}

static bool sped_receive_acks(SrsSpedController &sped, const vector<uint32_t> &acks)
{
    return sped.on_stun_received(false, "", true, acks);
}

VOID TEST(RtcSpedControllerTest, StateNames)
{
    EXPECT_STREQ("tentative", srs_sped_state_str(SrsSpedStateTentative));
    EXPECT_STREQ("confirmed", srs_sped_state_str(SrsSpedStateConfirmed));
    EXPECT_STREQ("pending", srs_sped_state_str(SrsSpedStatePending));
    EXPECT_STREQ("complete", srs_sped_state_str(SrsSpedStateComplete));
    EXPECT_STREQ("off", srs_sped_state_str(SrsSpedStateOff));
}

// After the answer, before any check: SPED is negotiated, nothing is captured yet, so
// a response has no DATA but still an empty ACK, which tells the peer SPED is on.
VOID TEST(RtcSpedControllerTest, NewSessionHasEmptyFlightAndEmptyAck)
{
    SrsSpedController sped;
    EXPECT_EQ(SrsSpedStateTentative, sped.state());

    string data;
    EXPECT_FALSE(sped.data_to_embed(BindingResponse, data));
    EXPECT_TRUE(data.empty());

    vector<uint32_t> acks;
    EXPECT_TRUE(sped.acks_to_embed(BindingResponse, acks));
    EXPECT_TRUE(acks.empty());
}

// Chrome 158 with the SPED field trial sends an empty ACK in its first check, and the
// ClientHello raw.
VOID TEST(RtcSpedControllerTest, FirstCheckWithEmptyAckConfirms)
{
    SrsSpedController sped;
    EXPECT_FALSE(sped_receive_empty_ack(sped));
    EXPECT_EQ(SrsSpedStateConfirmed, sped.state());

    string client_hello = sped_handshake("client-hello");
    sped.on_dtls_received(client_hello.data(), (int)client_hello.size());
    EXPECT_EQ(SrsSpedStateConfirmed, sped.state());

    vector<uint32_t> acks = sped_acks(sped);
    ASSERT_EQ(1u, acks.size());
    EXPECT_EQ(sped_crc(client_hello), acks[0]);
}

// A client that negotiated SPED in SDP but sends a first check with neither attribute
// does not do SPED, so DTLS goes raw only, as for a session without SPED.
VOID TEST(RtcSpedControllerTest, FirstCheckWithoutAttributesFallsBackToOff)
{
    SrsSpedController sped;
    EXPECT_FALSE(sped_receive_plain(sped));
    EXPECT_EQ(SrsSpedStateOff, sped.state());

    string client_hello = sped_handshake("client-hello");
    sped.on_dtls_received(client_hello.data(), (int)client_hello.size());
    sped_capture(sped, sped_handshake("server-hello"));
    sped.flush();

    string data;
    vector<uint32_t> acks;
    EXPECT_FALSE(sped.data_to_embed(BindingResponse, data));
    EXPECT_FALSE(sped.acks_to_embed(BindingResponse, acks));

    // A later check with attributes does not turn SPED back on.
    EXPECT_FALSE(sped.on_stun_received(true, client_hello, true, vector<uint32_t>()));
    EXPECT_EQ(SrsSpedStateOff, sped.state());

    sped.on_handshake_done(false, false);
    EXPECT_EQ(SrsSpedStateOff, sped.state());
    sped.on_srtp_received();
    EXPECT_EQ(SrsSpedStateOff, sped.state());
    EXPECT_FALSE(sped.data_to_embed(BindingResponse, data));
    EXPECT_FALSE(sped.acks_to_embed(BindingResponse, acks));
}

// A client that embeds its ClientHello in the first check, as the draft's example does.
VOID TEST(RtcSpedControllerTest, EmbeddedClientHelloIsDeliveredAndAcked)
{
    SrsSpedController sped;
    string client_hello = sped_handshake("client-hello");
    EXPECT_TRUE(sped.on_stun_received(true, client_hello, false, vector<uint32_t>()));
    EXPECT_EQ(SrsSpedStateConfirmed, sped.state());

    vector<uint32_t> acks = sped_acks(sped);
    ASSERT_EQ(1u, acks.size());
    EXPECT_EQ(sped_crc(client_hello), acks[0]);

    // The next check embeds the same cached ClientHello again, as the response was lost.
    // It is still delivered, so DTLS can drop the repeat, and ACKed only once.
    EXPECT_TRUE(sped.on_stun_received(true, client_hello, true, vector<uint32_t>()));
    acks = sped_acks(sped);
    ASSERT_EQ(1u, acks.size());
    EXPECT_EQ(sped_crc(client_hello), acks[0]);
}

// The draft drops a DATA value whose first byte is not DTLS (20 to 63).
VOID TEST(RtcSpedControllerTest, EmbeddedNonDtlsDataIsDropped)
{
    SrsSpedController sped;
    string rtp = sped_dtls_datagram(0x80, "rtp");
    EXPECT_FALSE(sped.on_stun_received(true, rtp, false, vector<uint32_t>()));
    EXPECT_EQ(SrsSpedStateConfirmed, sped.state());

    string stun = sped_dtls_datagram(0x00, "stun");
    EXPECT_FALSE(sped.on_stun_received(true, stun, false, vector<uint32_t>()));
    EXPECT_FALSE(sped.on_stun_received(true, "", false, vector<uint32_t>()));
    EXPECT_FALSE(sped.on_stun_received(true, sped_dtls_datagram(64, "x"), false, vector<uint32_t>()));
    EXPECT_FALSE(sped.on_stun_received(true, sped_dtls_datagram(19, "x"), false, vector<uint32_t>()));
    EXPECT_TRUE(sped.on_stun_received(true, sped_dtls_datagram(20, "ccs"), false, vector<uint32_t>()));
    EXPECT_TRUE(sped.on_stun_received(true, sped_dtls_datagram(63, "x"), false, vector<uint32_t>()));

    vector<uint32_t> acks = sped_acks(sped);
    ASSERT_EQ(2u, acks.size());
    EXPECT_EQ(sped_crc(sped_dtls_datagram(20, "ccs")), acks[0]);
    EXPECT_EQ(sped_crc(sped_dtls_datagram(63, "x")), acks[1]);

    // Raw non-DTLS is not ACKed either.
    sped.on_dtls_received(rtp.data(), (int)rtp.size());
    EXPECT_EQ(2u, sped_acks(sped).size());
}

// A fragmented server flight goes one datagram per response, round robin, until ACKed.
VOID TEST(RtcSpedControllerTest, FlightIsEmbeddedRoundRobin)
{
    SrsSpedController sped;
    EXPECT_FALSE(sped_receive_empty_ack(sped));

    string first = sped_handshake("server-flight-1");
    string second = sped_handshake("server-flight-2");
    sped_capture(sped, first);
    sped_capture(sped, second);
    sped.flush();

    EXPECT_EQ(first, sped_next(sped));
    EXPECT_EQ(second, sped_next(sped));
    EXPECT_EQ(first, sped_next(sped));
    EXPECT_EQ(second, sped_next(sped));
}

// A datagram DTLS writes after the flight was written out is a new flight, and the old
// one is no longer worth resending.
VOID TEST(RtcSpedControllerTest, NewFlightReplacesTheOld)
{
    SrsSpedController sped;
    EXPECT_FALSE(sped_receive_empty_ack(sped));

    string old_flight = sped_handshake("server-flight");
    sped_capture(sped, old_flight);
    sped.flush();
    EXPECT_EQ(old_flight, sped_next(sped));

    string new_flight = sped_handshake("server-finished");
    sped_capture(sped, new_flight);
    sped.flush();
    EXPECT_EQ(new_flight, sped_next(sped));
    EXPECT_EQ(new_flight, sped_next(sped));
}

// A DTLS 1.3 server resends on its timer the messages the client did not ACK in DTLS. While
// SPED still resends the flight, the copy goes raw only and the flight SPED resends stays, so
// the client's CRC ACKs keep pruning it. Once the client has ACKed every datagram in STUN but
// its DTLS still misses records, such as encrypted ones that came before the ServerHello, the
// timer's copy is the flight SPED resends.
VOID TEST(RtcSpedControllerTest, TimerRetransmissionReplacesOnlyAnAckedFlight)
{
    SrsSpedController sped;
    EXPECT_FALSE(sped_receive_empty_ack(sped));

    string first = sped_handshake("server-flight-1");
    string second = sped_handshake("server-flight-2");
    sped_capture(sped, first);
    sped_capture(sped, second);
    sped.flush();

    string copy1 = sped_handshake("server-flight-copy-1");
    string copy2 = sped_handshake("server-flight-copy-2");
    sped.capture_retransmission(copy1.data(), (int)copy1.size());
    sped.capture_retransmission(copy2.data(), (int)copy2.size());
    sped.flush();
    EXPECT_EQ(first, sped_next(sped));
    EXPECT_EQ(second, sped_next(sped));

    vector<uint32_t> acks;
    acks.push_back(sped_crc(first));
    EXPECT_FALSE(sped_receive_acks(sped, acks));
    sped.capture_retransmission(copy2.data(), (int)copy2.size());
    sped.flush();
    EXPECT_EQ(second, sped_next(sped));
    EXPECT_EQ(second, sped_next(sped));

    acks.clear();
    acks.push_back(sped_crc(second));
    EXPECT_FALSE(sped_receive_acks(sped, acks));
    string data;
    EXPECT_FALSE(sped.data_to_embed(BindingResponse, data));

    sped.capture_retransmission(copy1.data(), (int)copy1.size());
    sped.capture_retransmission(copy2.data(), (int)copy2.size());
    sped.flush();
    EXPECT_EQ(SrsSpedStateConfirmed, sped.state());
    EXPECT_EQ(copy1, sped_next(sped));
    EXPECT_EQ(copy2, sped_next(sped));

    // A later copy, before the client ACKs this one, goes raw only too.
    string copy3 = sped_handshake("server-flight-copy-3");
    sped.capture_retransmission(copy3.data(), (int)copy3.size());
    sped.flush();
    EXPECT_EQ(copy1, sped_next(sped));
    EXPECT_EQ(copy2, sped_next(sped));
}

// The peer ACKs by CRC, from any message, in any order; a CRC that matches nothing, or
// one already pruned, is ignored.
VOID TEST(RtcSpedControllerTest, AcksPruneTheFlightByCrc)
{
    SrsSpedController sped;
    EXPECT_FALSE(sped_receive_empty_ack(sped));

    string first = sped_handshake("server-flight-1");
    string second = sped_handshake("server-flight-2");
    sped_capture(sped, first);
    sped_capture(sped, second);
    sped.flush();

    vector<uint32_t> acks;
    acks.push_back(sped_crc(second));
    acks.push_back(0xdeadbeef);
    EXPECT_FALSE(sped_receive_acks(sped, acks));
    EXPECT_EQ(SrsSpedStateConfirmed, sped.state());
    EXPECT_EQ(first, sped_next(sped));
    EXPECT_EQ(first, sped_next(sped));

    // A duplicate ACK changes nothing.
    EXPECT_FALSE(sped_receive_acks(sped, acks));
    EXPECT_EQ(first, sped_next(sped));

    acks.clear();
    acks.push_back(sped_crc(first));
    acks.push_back(sped_crc(first));
    EXPECT_FALSE(sped_receive_acks(sped, acks));
    EXPECT_EQ(SrsSpedStateConfirmed, sped.state());

    string data;
    EXPECT_FALSE(sped.data_to_embed(BindingResponse, data));
    EXPECT_TRUE(sped_acks(sped).empty());
}

// The ACK list holds the 4 newest CRCs, the most an ACK attribute carries, with no
// duplicates.
VOID TEST(RtcSpedControllerTest, AckListKeepsTheFourNewest)
{
    SrsSpedController sped;
    EXPECT_FALSE(sped_receive_empty_ack(sped));

    vector<string> datagrams;
    for (int i = 0; i < 5; i++) {
        datagrams.push_back(sped_handshake(string("client-") + (char)('a' + i)));
    }

    EXPECT_TRUE(sped.on_stun_received(true, datagrams[0], true, vector<uint32_t>()));
    sped.on_dtls_received(datagrams[1].data(), (int)datagrams[1].size());
    EXPECT_TRUE(sped.on_stun_received(true, datagrams[2], true, vector<uint32_t>()));
    sped.on_dtls_received(datagrams[3].data(), (int)datagrams[3].size());

    // The same datagram, raw and embedded, is one ACK.
    EXPECT_TRUE(sped.on_stun_received(true, datagrams[3], true, vector<uint32_t>()));
    sped.on_dtls_received(datagrams[2].data(), (int)datagrams[2].size());

    vector<uint32_t> acks = sped_acks(sped);
    ASSERT_EQ(4u, acks.size());
    EXPECT_EQ(sped_crc(datagrams[0]), acks[0]);
    EXPECT_EQ(sped_crc(datagrams[3]), acks[3]);

    sped.on_dtls_received(datagrams[4].data(), (int)datagrams[4].size());
    acks = sped_acks(sped);
    ASSERT_EQ((size_t)kStunDtlsInStunMaxAcks, acks.size());
    EXPECT_EQ(sped_crc(datagrams[1]), acks[0]);
    EXPECT_EQ(sped_crc(datagrams[2]), acks[1]);
    EXPECT_EQ(sped_crc(datagrams[3]), acks[2]);
    EXPECT_EQ(sped_crc(datagrams[4]), acks[3]);
}

// SPED rides only in Binding requests and responses.
VOID TEST(RtcSpedControllerTest, EmbedsOnlyInBindingMessages)
{
    SrsSpedController sped;
    EXPECT_FALSE(sped_receive_empty_ack(sped));
    string server_flight = sped_handshake("server-flight");
    sped_capture(sped, server_flight);
    sped.flush();

    string data;
    vector<uint32_t> acks;
    EXPECT_FALSE(sped.data_to_embed(BindingErrorResponse, data));
    EXPECT_FALSE(sped.acks_to_embed(BindingErrorResponse, acks));

    EXPECT_TRUE(sped.data_to_embed(BindingRequest, data));
    EXPECT_EQ(server_flight, data);
    EXPECT_TRUE(sped.acks_to_embed(BindingRequest, acks));
}

// The whole DTLS 1.2 handshake with SRS as the server, every flight embedded, as the
// draft's example. The state and what SRS embeds are checked after every message.
VOID TEST(RtcSpedControllerTest, Dtls12ServerHandshakeEmbedded)
{
    SrsSpedController sped;
    string data;
    vector<uint32_t> acks;

    // Check 1: the ClientHello.
    string client_hello = sped_handshake("client-hello");
    EXPECT_TRUE(sped.on_stun_received(true, client_hello, true, vector<uint32_t>()));
    EXPECT_EQ(SrsSpedStateConfirmed, sped.state());

    // DTLS answers with flight 2 in one datagram, which response 1 embeds with the ACK.
    string server_flight = sped_handshake("server-hello-done");
    sped_capture(sped, server_flight);
    sped.flush();
    EXPECT_EQ(server_flight, sped_next(sped));
    acks = sped_acks(sped);
    ASSERT_EQ(1u, acks.size());
    EXPECT_EQ(sped_crc(client_hello), acks[0]);

    // Check 2: the client's flight 3, with the ACK of flight 2.
    string client_flight = sped_handshake("client-key-exchange-finished");
    acks.clear();
    acks.push_back(sped_crc(server_flight));
    EXPECT_TRUE(sped.on_stun_received(true, client_flight, true, acks));
    EXPECT_EQ(SrsSpedStateConfirmed, sped.state());
    EXPECT_FALSE(sped.data_to_embed(BindingResponse, data));

    // DTLS answers with CCS and Finished and is done. The server keeps them until ACKed.
    string server_finished = sped_dtls_datagram(20, "ccs-finished");
    sped_capture(sped, server_finished);
    sped.flush();
    sped.on_handshake_done(false, false);
    EXPECT_EQ(SrsSpedStatePending, sped.state());
    EXPECT_EQ(server_finished, sped_next(sped));
    acks = sped_acks(sped);
    ASSERT_EQ(2u, acks.size());
    EXPECT_EQ(sped_crc(client_flight), acks[1]);

    // Check 3: the client ACKs the Finished, so nothing is left to send.
    acks.clear();
    acks.push_back(sped_crc(server_flight));
    acks.push_back(sped_crc(server_finished));
    EXPECT_FALSE(sped_receive_acks(sped, acks));
    EXPECT_EQ(SrsSpedStateComplete, sped.state());
    EXPECT_FALSE(sped.data_to_embed(BindingResponse, data));
    EXPECT_FALSE(sped.acks_to_embed(BindingResponse, acks));

    // Later checks and stray copies change nothing.
    EXPECT_FALSE(sped.on_stun_received(true, client_flight, true, vector<uint32_t>()));
    sped_capture(sped, server_finished);
    sped.flush();
    EXPECT_EQ(SrsSpedStateComplete, sped.state());
    EXPECT_FALSE(sped.data_to_embed(BindingResponse, data));
}

// SRTP from the client proves it has the server's Finished, though its ACK was lost.
VOID TEST(RtcSpedControllerTest, SrtpWhilePendingCompletes)
{
    SrsSpedController sped;
    EXPECT_FALSE(sped_receive_empty_ack(sped));

    // SRTP before the handshake is done does not end SPED.
    sped.on_srtp_received();
    EXPECT_EQ(SrsSpedStateConfirmed, sped.state());

    sped_capture(sped, sped_dtls_datagram(20, "ccs-finished"));
    sped.flush();
    sped.on_handshake_done(false, false);
    EXPECT_EQ(SrsSpedStatePending, sped.state());

    sped.on_srtp_received();
    EXPECT_EQ(SrsSpedStateComplete, sped.state());

    string data;
    vector<uint32_t> acks;
    EXPECT_FALSE(sped.data_to_embed(BindingResponse, data));
    EXPECT_FALSE(sped.acks_to_embed(BindingResponse, acks));
}

// A client that is done stops sending the attributes.
VOID TEST(RtcSpedControllerTest, CheckWithoutAttributesWhilePendingCompletes)
{
    SrsSpedController sped;
    EXPECT_FALSE(sped_receive_empty_ack(sped));
    sped_capture(sped, sped_dtls_datagram(20, "ccs-finished"));
    sped.flush();
    sped.on_handshake_done(false, false);
    EXPECT_EQ(SrsSpedStatePending, sped.state());

    EXPECT_FALSE(sped_receive_plain(sped));
    EXPECT_EQ(SrsSpedStateComplete, sped.state());

    string data;
    EXPECT_FALSE(sped.data_to_embed(BindingResponse, data));
}

// When the handshake ends with its last flight dropped, nothing is left to embed, but the peer
// still waits for the ACK of the last datagram it sent.
VOID TEST(RtcSpedControllerTest, HandshakeDoneWithoutFlightStillAcks)
{
    SrsSpedController sped;
    EXPECT_FALSE(sped_receive_empty_ack(sped));

    string server_flight = sped_handshake("server-hello-finished");
    sped_capture(sped, server_flight);
    sped.flush();

    string client_finished = sped_dtls_datagram(0x2e, "client-finished");
    EXPECT_TRUE(sped.on_stun_received(true, client_finished, true, vector<uint32_t>()));
    sped.on_handshake_done(true, false);
    EXPECT_EQ(SrsSpedStatePending, sped.state());

    string data;
    EXPECT_FALSE(sped.data_to_embed(BindingResponse, data));
    vector<uint32_t> acks = sped_acks(sped);
    ASSERT_EQ(1u, acks.size());
    EXPECT_EQ(sped_crc(client_finished), acks[0]);

    // A check that only ACKs keeps SPED going, so the ACK of the Finished goes out again.
    acks.clear();
    acks.push_back(sped_crc(server_flight));
    EXPECT_FALSE(sped_receive_acks(sped, acks));
    EXPECT_EQ(SrsSpedStatePending, sped.state());
    EXPECT_EQ(1u, sped_acks(sped).size());

    sped.on_srtp_received();
    EXPECT_EQ(SrsSpedStateComplete, sped.state());
}

// The SPED attributes of one STUN message, as its sender's controller embeds them.
class SpedStunMessage
{
public:
    bool has_data_;
    string data_;
    bool has_acks_;
    vector<uint32_t> acks_;

public:
    SpedStunMessage()
    {
        has_data_ = false;
        has_acks_ = false;
    }
};

static SpedStunMessage sped_build(SrsSpedController *sped, SrsStunMessageType type)
{
    SpedStunMessage msg;
    msg.has_data_ = sped->data_to_embed(type, msg.data_);
    msg.has_acks_ = sped->acks_to_embed(type, msg.acks_);
    return msg;
}

// What the receiver of an authenticated STUN message does with its SPED attributes: the
// controller takes the ACKs and the DATA, and DTLS reads the DATA when it is DTLS.
static srs_error_t sped_deliver(SrsDtls &dtls, SpedStunMessage &msg)
{
    if (!dtls.sped()->on_stun_received(msg.has_data_, msg.data_, msg.has_acks_, msg.acks_)) {
        return srs_success;
    }
    return dtls.on_embedded_dtls(&msg.data_[0], (int)msg.data_.size());
}

static SpedStunMessage sped_request_with(const string &datagram)
{
    SpedStunMessage msg;
    msg.has_data_ = true;
    msg.data_ = datagram;
    msg.has_acks_ = true;
    return msg;
}

static srs_error_t sped_deliver_raw(SrsDtls &dtls, const vector<string> &datagrams)
{
    srs_error_t err = srs_success;

    for (int i = 0; i < (int)datagrams.size(); i++) {
        string datagram = datagrams[i];
        if ((err = dtls.on_dtls(&datagram[0], (int)datagram.size())) != srs_success) {
            return srs_error_wrap(err, "raw %d", i);
        }
    }

    return err;
}

static void sped_expect_srtp_keys_match(SrsDtls &client, SrsDtls &server)
{
    srs_error_t err;

    string client_recv, client_send, server_recv, server_send;
    HELPER_ASSERT_SUCCESS(client.get_srtp_key(client_recv, client_send));
    HELPER_ASSERT_SUCCESS(server.get_srtp_key(server_recv, server_send));
    EXPECT_EQ(30, (int)client_send.size());
    EXPECT_TRUE(client_send == server_recv);
    EXPECT_TRUE(client_recv == server_send);
}

// A client embeds its ClientHello in the first check. SRS, as the DTLS server, has started
// its handshake before it builds the response, so DTLS reads the ClientHello and writes the
// server flight: the flight is still sent raw, and the same datagrams are kept to embed in
// the response, with an ACK of the ClientHello.
VOID TEST(RtcSpedDtlsTest, EmbeddedClientHelloGivesServerFlightToEmbed)
{
    srs_error_t err;

    MockDtlsCallbackForHandshake ccb, scb;
    SrsDtls client(&ccb);
    SrsDtls server(&scb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
    HELPER_ASSERT_SUCCESS(server.initialize("passive", "dtls1.2"));
    server.enable_sped();
    SrsSpedController *sped = server.sped();
    ASSERT_TRUE(sped != NULL);
    EXPECT_EQ(SrsSpedStateTentative, sped->state());

    HELPER_ASSERT_SUCCESS(server.start_active_handshake());
    EXPECT_TRUE(scb.packets_.empty());
    EXPECT_TRUE(sped->flight_.empty());

    HELPER_ASSERT_SUCCESS(client.start_active_handshake());
    ASSERT_EQ(1, (int)ccb.packets_.size());
    string client_hello = ccb.packets_[0];

    SpedStunMessage request = sped_request_with(client_hello);
    HELPER_ASSERT_SUCCESS(sped_deliver(server, request));
    EXPECT_EQ(SrsSpedStateConfirmed, sped->state());
    EXPECT_FALSE(server.impl_->handshake_done_for_us_);

    // The flight starts with a handshake record(22) of type server_hello(2), sent raw.
    ASSERT_FALSE(scb.packets_.empty());
    ASSERT_GE((int)scb.packets_[0].size(), 14);
    EXPECT_EQ(22, (uint8_t)scb.packets_[0][0]);
    EXPECT_EQ(2, (uint8_t)scb.packets_[0][13]);
    EXPECT_TRUE(sped->flight_ == scb.packets_);

    SpedStunMessage response = sped_build(sped, BindingResponse);
    EXPECT_TRUE(response.has_data_);
    EXPECT_TRUE(response.data_ == scb.packets_[0]);
    ASSERT_TRUE(response.has_acks_);
    ASSERT_EQ(1, (int)response.acks_.size());
    EXPECT_EQ(sped_crc(client_hello), response.acks_[0]);
}

// Every raw DTLS datagram is lost, so the handshake completes only inside the checks and
// their responses: each side embeds its flight until the peer ACKs it, and once done, SRS
// keeps its CCS and Finished until the client ACKs them. Both sides use OpenSSL, the client
// with a controller of its own, and no socket.
VOID TEST(RtcSpedDtlsTest, HandshakeCompletesInsideStunWhenRawIsLost)
{
    srs_error_t err;

    MockDtlsCallbackForHandshake ccb, scb;
    SrsDtls client(&ccb);
    SrsDtls server(&scb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
    HELPER_ASSERT_SUCCESS(server.initialize("passive", "dtls1.2"));
    client.enable_sped();
    server.enable_sped();
    SrsSpedController *client_sped = client.sped();
    SrsSpedController *server_sped = server.sped();
    ASSERT_TRUE(client_sped != NULL);
    ASSERT_TRUE(server_sped != NULL);

    HELPER_ASSERT_SUCCESS(server.start_active_handshake());
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());
    ASSERT_EQ(1, (int)ccb.packets_.size());
    EXPECT_TRUE(client_sped->flight_ == ccb.packets_);
    ccb.packets_.clear();

    int rounds = 0;
    bool server_was_done = false;
    for (; rounds < 16 && server_sped->state() != SrsSpedStateComplete; rounds++) {
        SpedStunMessage request = sped_build(client_sped, BindingRequest);
        EXPECT_TRUE(request.has_acks_);
        HELPER_ASSERT_SUCCESS(sped_deliver(server, request));

        // SPED runs while the server waits for the client's flights, and waits for the ACK of
        // the server's last flight once the server is done.
        bool server_done = server.impl_->handshake_done_for_us_;
        if (server_sped->state() != SrsSpedStateComplete) {
            EXPECT_EQ(server_done ? SrsSpedStatePending : SrsSpedStateConfirmed, server_sped->state());
        } else {
            EXPECT_TRUE(server_was_done);
        }

        // The server has nothing left to embed only once it is complete.
        SpedStunMessage response = sped_build(server_sped, BindingResponse);
        EXPECT_EQ(server_sped->state() != SrsSpedStateComplete, response.has_acks_);
        EXPECT_EQ(server_sped->state() != SrsSpedStateComplete, response.has_data_);
        HELPER_ASSERT_SUCCESS(sped_deliver(client, response));

        server_was_done = server_done;
        ccb.packets_.clear();
        scb.packets_.clear();
    }

    EXPECT_LT(rounds, 16);
    EXPECT_EQ(SrsSpedStateComplete, server_sped->state());
    EXPECT_EQ(1, scb.handshake_done_count_);
    EXPECT_EQ(1, ccb.handshake_done_count_);
    EXPECT_TRUE(server_sped->flight_.empty());
    EXPECT_TRUE(server_sped->acks_.empty());

    // The client stops on the first response that carries no SPED attribute.
    EXPECT_EQ(SrsSpedStateComplete, client_sped->state());

    sped_expect_srtp_keys_match(client, server);
}

// Chrome sends each flight raw as well, and SRS ACKs raw DTLS as it ACKs embedded DTLS. Here
// the ClientHello arrives raw before any check carries a SPED attribute.
VOID TEST(RtcSpedDtlsTest, RawClientHelloIsAckedAndItsFlightKept)
{
    srs_error_t err;

    MockDtlsCallbackForHandshake ccb, scb;
    SrsDtls client(&ccb);
    SrsDtls server(&scb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
    HELPER_ASSERT_SUCCESS(server.initialize("passive", "dtls1.2"));
    server.enable_sped();
    SrsSpedController *sped = server.sped();
    ASSERT_TRUE(sped != NULL);

    HELPER_ASSERT_SUCCESS(server.start_active_handshake());
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());
    ASSERT_EQ(1, (int)ccb.packets_.size());
    string client_hello = ccb.packets_[0];

    HELPER_ASSERT_SUCCESS(sped_deliver_raw(server, ccb.packets_));
    EXPECT_EQ(SrsSpedStateTentative, sped->state());
    ASSERT_FALSE(scb.packets_.empty());
    EXPECT_TRUE(sped->flight_ == scb.packets_);

    // Chrome 158's first check carries an empty ACK, which confirms SPED, and its response
    // carries the server flight and the ACK of the raw ClientHello.
    EXPECT_FALSE(sped_receive_empty_ack(*sped));
    EXPECT_EQ(SrsSpedStateConfirmed, sped->state());

    SpedStunMessage response = sped_build(sped, BindingResponse);
    EXPECT_TRUE(response.has_data_);
    EXPECT_TRUE(response.data_ == scb.packets_[0]);
    ASSERT_EQ(1, (int)response.acks_.size());
    EXPECT_EQ(sped_crc(client_hello), response.acks_[0]);
}

// The ClientHello arrives twice, embedded and then raw, as a client that also sends each
// flight raw does. OpenSSL drops the copy during the handshake, so nothing new is sent, the
// flight to embed stays the same, and the ClientHello is ACKed once.
VOID TEST(RtcSpedDtlsTest, ClientHelloEmbeddedAndRawIsHandledOnce)
{
    srs_error_t err;

    MockDtlsCallbackForHandshake ccb, scb;
    SrsDtls client(&ccb);
    SrsDtls server(&scb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
    HELPER_ASSERT_SUCCESS(server.initialize("passive", "dtls1.2"));
    server.enable_sped();
    SrsSpedController *sped = server.sped();
    ASSERT_TRUE(sped != NULL);

    HELPER_ASSERT_SUCCESS(server.start_active_handshake());
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());
    ASSERT_EQ(1, (int)ccb.packets_.size());
    string client_hello = ccb.packets_[0];

    SpedStunMessage request = sped_request_with(client_hello);
    HELPER_ASSERT_SUCCESS(sped_deliver(server, request));
    ASSERT_FALSE(scb.packets_.empty());
    vector<string> server_flight = scb.packets_;
    EXPECT_TRUE(sped->flight_ == server_flight);
    scb.packets_.clear();

    HELPER_ASSERT_SUCCESS(sped_deliver_raw(server, ccb.packets_));
    EXPECT_TRUE(scb.packets_.empty());
    EXPECT_TRUE(sped->flight_ == server_flight);
    EXPECT_EQ(SrsSpedStateConfirmed, sped->state());

    vector<uint32_t> acks = sped_acks(*sped);
    ASSERT_EQ(1, (int)acks.size());
    EXPECT_EQ(sped_crc(client_hello), acks[0]);
}

// The client's last flight goes raw, as in the draft's section 5.2 example. SRS ACKs each of
// its datagrams in the next response, and keeps its CCS and Finished to embed until the
// client ACKs them.
VOID TEST(RtcSpedDtlsTest, RawClientFinishedIsAckedAndServerFinishedKept)
{
    srs_error_t err;

    MockDtlsCallbackForHandshake ccb, scb;
    SrsDtls client(&ccb);
    SrsDtls server(&scb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
    HELPER_ASSERT_SUCCESS(server.initialize("passive", "dtls1.2"));
    server.enable_sped();
    SrsSpedController *sped = server.sped();
    ASSERT_TRUE(sped != NULL);

    HELPER_ASSERT_SUCCESS(server.start_active_handshake());
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());
    ASSERT_EQ(1, (int)ccb.packets_.size());
    SpedStunMessage request = sped_request_with(ccb.packets_[0]);
    ccb.packets_.clear();
    HELPER_ASSERT_SUCCESS(sped_deliver(server, request));

    // The client reads the server flight raw, and sends its last flight raw.
    vector<string> server_flight = scb.packets_;
    scb.packets_.clear();
    HELPER_ASSERT_SUCCESS(sped_deliver_raw(client, server_flight));
    ASSERT_FALSE(ccb.packets_.empty());
    vector<string> client_flight = ccb.packets_;
    ccb.packets_.clear();

    HELPER_ASSERT_SUCCESS(sped_deliver_raw(server, client_flight));
    EXPECT_TRUE(server.impl_->handshake_done_for_us_);
    EXPECT_EQ(1, scb.handshake_done_count_);
    EXPECT_EQ(SrsSpedStatePending, sped->state());

    // The server's last flight is CCS and Finished. OpenSSL 1.1 puts a NewSessionTicket first for
    // a client that offers session tickets: a handshake record(22) of type new_session_ticket(4).
    // OpenSSL 4.1 sends no TLS 1.2 ticket when the server verifies the peer without a session id
    // context, as SRS does, so its flight starts with the CCS record(20).
    ASSERT_FALSE(scb.packets_.empty());
    ASSERT_GE((int)scb.packets_[0].size(), 14);
#if OPENSSL_VERSION_NUMBER >= 0x40100000L
    EXPECT_EQ(20, (uint8_t)scb.packets_[0][0]);
#else
    EXPECT_EQ(22, (uint8_t)scb.packets_[0][0]);
    EXPECT_EQ(4, (uint8_t)scb.packets_[0][13]);
#endif
    EXPECT_TRUE(sped->flight_ == scb.packets_);

    SpedStunMessage response = sped_build(sped, BindingResponse);
    EXPECT_TRUE(response.has_data_);
    EXPECT_TRUE(response.data_ == scb.packets_[0]);
    for (int i = 0; i < (int)client_flight.size(); i++) {
        uint32_t crc = sped_crc(client_flight[i]);
        EXPECT_TRUE(std::find(response.acks_.begin(), response.acks_.end(), crc) != response.acks_.end()) << i;
    }

    // The client reads the CCS and Finished raw, and ACKs them in its next check.
    vector<string> server_finished = scb.packets_;
    HELPER_ASSERT_SUCCESS(sped_deliver_raw(client, server_finished));
    EXPECT_EQ(1, ccb.handshake_done_count_);

    vector<uint32_t> acks;
    for (int i = 0; i < (int)server_finished.size(); i++) {
        acks.push_back(sped_crc(server_finished[i]));
    }
    EXPECT_FALSE(sped_receive_acks(*sped, acks));
    EXPECT_EQ(SrsSpedStateComplete, sped->state());
    EXPECT_TRUE(sped->flight_.empty());

    sped_expect_srtp_keys_match(client, server);
}

// A client that negotiated SPED but whose first check carries neither attribute does not do
// SPED, so the handshake goes raw, as without SPED, and nothing is kept to embed. This
// passes before the DTLS hooks exist, as it locks in the raw handshake.
VOID TEST(RtcSpedDtlsTest, FallbackToOffKeepsRawHandshake)
{
    srs_error_t err;

    MockDtlsCallbackForHandshake ccb, scb;
    SrsDtls client(&ccb);
    SrsDtls server(&scb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
    HELPER_ASSERT_SUCCESS(server.initialize("passive", "dtls1.2"));
    server.enable_sped();
    SrsSpedController *sped = server.sped();
    ASSERT_TRUE(sped != NULL);

    HELPER_ASSERT_SUCCESS(server.start_active_handshake());
    EXPECT_FALSE(sped_receive_plain(*sped));
    EXPECT_EQ(SrsSpedStateOff, sped->state());

    HELPER_ASSERT_SUCCESS(client.start_active_handshake());
    HELPER_ASSERT_SUCCESS(dtls_exchange(&client, &ccb, &server, &scb));
    EXPECT_EQ(1, ccb.handshake_done_count_);
    EXPECT_EQ(1, scb.handshake_done_count_);

    EXPECT_EQ(SrsSpedStateOff, sped->state());
    EXPECT_TRUE(sped->flight_.empty());
    EXPECT_TRUE(sped->acks_.empty());
    string data;
    EXPECT_FALSE(sped->data_to_embed(BindingResponse, data));

    sped_expect_srtp_keys_match(client, server);
}

// Without SPED negotiated, DTLS has no controller, before and after initialize, and the
// handshake is as before. This passes before the DTLS hooks exist, as it locks in the
// session without SPED.
VOID TEST(RtcSpedDtlsTest, WithoutSpedThereIsNoController)
{
    srs_error_t err;

    MockDtlsCallbackForHandshake ccb, scb;
    SrsDtls client(&ccb);
    SrsDtls server(&scb);
    EXPECT_TRUE(server.sped() == NULL);

    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
    HELPER_ASSERT_SUCCESS(server.initialize("passive", "dtls1.2"));
    EXPECT_TRUE(server.sped() == NULL);

    HELPER_ASSERT_SUCCESS(server.start_active_handshake());
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());
    HELPER_ASSERT_SUCCESS(dtls_exchange(&client, &ccb, &server, &scb));
    EXPECT_EQ(1, ccb.handshake_done_count_);
    EXPECT_EQ(1, scb.handshake_done_count_);
    EXPECT_TRUE(server.sped() == NULL);

    sped_expect_srtp_keys_match(client, server);
}

// The time left on the DTLS timer of a session, -1 when it is not running.
static srs_utime_t sped_dtls_timeout(SrsDtls *dtls)
{
    struct timeval tv;
    if (DTLSv1_get_timeout(dtls->impl_->dtls_, &tv) != 1) {
        return -1;
    }
    return tv.tv_sec * SRS_UTIME_SECONDS + tv.tv_usec;
}

// With SPED, DTLS writes each datagram small enough to embed in a STUN message with its
// attributes, as Chrome's 900-byte MTU with SPED does, so SPED can resend every datagram of
// a flight, whatever its certificate; without SPED, the MTU is the 1200 bytes of before.
VOID TEST(RtcSpedDtlsTest, SpedMtuEmbedsEveryDatagram)
{
    srs_error_t err;

    // OpenSSL tells the data MTU, the MTU without the record header and the cipher's
    // overhead, only once the handshake has chosen the cipher, the same for both.
    int data_mtu[2];
    for (int i = 0; i < 2; i++) {
        bool sped = i == 1;
        MockDtlsCallbackForHandshake ccb, scb;
        SrsDtls client(&ccb);
        SrsDtls server(&scb);
        HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
        HELPER_ASSERT_SUCCESS(server.initialize("passive", "dtls1.2"));
        if (sped) {
            server.enable_sped();
        }
        HELPER_ASSERT_SUCCESS(server.start_active_handshake());
        HELPER_ASSERT_SUCCESS(client.start_active_handshake());
        HELPER_ASSERT_SUCCESS(dtls_exchange(&client, &ccb, &server, &scb));
        ASSERT_EQ(1, scb.handshake_done_count_);
        data_mtu[i] = (int)DTLS_get_data_mtu(server.impl_->dtls_);
    }
    EXPECT_GT(data_mtu[0], kSpedDtlsMtu);
    EXPECT_LT(data_mtu[1], kSpedDtlsMtu);
    EXPECT_EQ(1200 - kSpedDtlsMtu, data_mtu[0] - data_mtu[1]);

    // A datagram of the MTU fits a response with a USERNAME of two 32-character ufrags and a
    // full ACK.
    SrsStunPacket response;
    response.set_message_type(BindingResponse);
    response.set_local_ufrag(string(32, 'l'));
    response.set_remote_ufrag(string(32, 'r'));
    response.set_dtls_in_stun_acks(vector<uint32_t>(kStunDtlsInStunMaxAcks, 1));
    EXPECT_TRUE(response.dtls_in_stun_data_fits(kSpedDtlsMtu));
}

// SPED resends the flight in the STUN messages until the peer ACKs it, so while SPED is on,
// the DTLS timer waits its longest, as the draft says DTLS's timers should be off; when SPED
// falls back to OFF, the timer starts at 50ms, as before.
VOID TEST(RtcSpedDtlsTest, TimerWaitsOnlyWhileSpedCarriesFlights)
{
    srs_error_t err;

    if (true) {
        MockDtlsCallbackForHandshake ccb, scb;
        SrsDtls client(&ccb);
        SrsDtls server(&scb);
        HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
        HELPER_ASSERT_SUCCESS(server.initialize("passive", "dtls1.2"));
        server.enable_sped();
        HELPER_ASSERT_SUCCESS(server.start_active_handshake());
        HELPER_ASSERT_SUCCESS(client.start_active_handshake());

        SpedStunMessage request = sped_request_with(ccb.packets_[0]);
        HELPER_ASSERT_SUCCESS(sped_deliver(server, request));
        EXPECT_EQ(SrsSpedStateConfirmed, server.sped()->state());
        ASSERT_FALSE(scb.packets_.empty());
        EXPECT_GT(sped_dtls_timeout(&server), 10 * SRS_UTIME_SECONDS);
    }

    if (true) {
        MockDtlsCallbackForHandshake ccb, scb;
        SrsDtls client(&ccb);
        SrsDtls server(&scb);
        HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
        HELPER_ASSERT_SUCCESS(server.initialize("passive", "dtls1.2"));
        server.enable_sped();
        HELPER_ASSERT_SUCCESS(server.start_active_handshake());
        EXPECT_FALSE(sped_receive_plain(*server.sped()));
        EXPECT_EQ(SrsSpedStateOff, server.sped()->state());
        HELPER_ASSERT_SUCCESS(client.start_active_handshake());

        HELPER_ASSERT_SUCCESS(sped_deliver_raw(server, ccb.packets_));
        ASSERT_FALSE(scb.packets_.empty());
        srs_utime_t timeout = sped_dtls_timeout(&server);
        EXPECT_GT(timeout, 0);
        EXPECT_LE(timeout, 50 * SRS_UTIME_MILLISECONDS);
    }
}

// A UDP socket that keeps each datagram SRS sends, so a test sees the raw DTLS and the
// STUN responses in the order they go out.
class MockUdpMuxSocketForSped : public MockUdpMuxSocket
{
public:
    vector<string> sent_;

public:
    virtual srs_error_t sendto(void *data, int size, srs_utime_t timeout)
    {
        sent_.push_back(string((const char *)data, size));
        return MockUdpMuxSocket::sendto(data, size, timeout);
    }
};

// An SRS session over UDP, with the real DTLS and SRTP transport, as the DTLS server. The
// peer's a=fingerprint is the SRS certificate, as the in-process client uses it too.
class SpedUdpSession
{
public:
    MockRtcConnectionForUdpNetwork conn_;
    MockEphemeralDelta delta_;
    MockUdpMuxSocketForSped socket_;
    MockResourceManagerForUdpNetwork manager_;
    SrsRtcUdpNetwork *network_;
    int checks_;

public:
    SpedUdpSession()
    {
        network_ = new SrsRtcUdpNetwork(&conn_, &delta_);
        network_->conn_manager_ = &manager_;
        checks_ = 0;
    }
    virtual ~SpedUdpSession()
    {
        // The mock sockets copy themselves as the cached addresses, so the network must not free them.
        network_->peer_addresses_.clear();
        network_->sendonly_skt_ = NULL;
        srs_freep(network_);
    }

public:
    srs_error_t initialize(bool sped, string version = "dtls1.2")
    {
        srs_error_t err = srs_success;

        SrsSessionConfig cfg;
        cfg.dtls_role_ = "passive";
        cfg.dtls_version_ = version;
        cfg.remote_fingerprint_algo_ = "sha-256";
        cfg.remote_fingerprint_ = _srs_rtc_dtls_certificate->get_fingerprint();
        cfg.sped_ = sped;
        if ((err = network_->initialize(&cfg, true, true)) != srs_success) {
            return srs_error_wrap(err, "init");
        }

        network_->sendonly_skt_ = &socket_;
        network_->set_state(SrsRtcNetworkStateWaitingStun);
        return err;
    }

    SrsSpedController *sped()
    {
        return network_->transport_->sped();
    }

    SrsDtls *dtls()
    {
        SrsSecurityTransport *transport = dynamic_cast<SrsSecurityTransport *>(network_->transport_);
        return dynamic_cast<SrsDtls *>(transport->dtls_);
    }

    // Send SRS an authenticated check with the SPED attributes, and get what SRS sent back:
    // the raw DTLS datagrams, and the one STUN response, which goes last.
    srs_error_t check(const SpedStunMessage &msg, vector<string> &raw, SrsStunPacket &response)
    {
        return check_on(NULL, msg, raw, response);
    }

    // The same check, from the candidate pair of the socket, or from the selected pair when it
    // is NULL. SRS answers on the pair of the check, and sends raw DTLS on the selected pair.
    srs_error_t check_on(MockUdpMuxSocketForSped *pair, const SpedStunMessage &msg, vector<string> &raw, SrsStunPacket &response)
    {
        srs_error_t err = srs_success;

        SrsStunPacket request;
        request.set_message_type(BindingRequest);
        request.set_local_ufrag("srsufrag");
        request.set_remote_ufrag("peerufrag");
        string tid = "sped-check00";
        tid[10] = (char)('0' + (checks_ / 10) % 10);
        tid[11] = (char)('0' + checks_ % 10);
        checks_++;
        request.set_transcation_id(tid);
        request.has_dtls_in_stun_data_ = msg.has_data_;
        request.dtls_in_stun_data_ = msg.data_;
        request.has_dtls_in_stun_ack_ = msg.has_acks_;
        request.dtls_in_stun_acks_ = msg.acks_;

        // The tests use at most two pairs, the session's socket and the check's.
        MockUdpMuxSocketForSped *from = pair ? pair : dynamic_cast<MockUdpMuxSocketForSped *>(network_->sendonly_skt_);
        socket_.sent_.clear();
        from->sent_.clear();

        char buf[20];
        memset(buf, 0, sizeof(buf));
        if (pair) {
            err = network_->on_stun(pair, &request, buf, sizeof(buf));
        } else {
            err = network_->on_stun(&request, buf, sizeof(buf));
        }
        if (err != srs_success) {
            return srs_error_wrap(err, "on stun");
        }

        if (from->sent_.empty()) {
            return srs_error_new(ERROR_RTC_STUN, "no response");
        }
        string last = from->sent_.back();
        raw.assign(from->sent_.begin(), from->sent_.end() - 1);
        // The raw DTLS goes on the selected pair, which may be the other one.
        if (from != &socket_) {
            raw.insert(raw.end(), socket_.sent_.begin(), socket_.sent_.end());
        }
        for (int i = 0; i < (int)raw.size(); i++) {
            string &datagram = raw[i];
            uint8_t first = (uint8_t)datagram[0];
            if (first < 20 || first > 63) {
                return srs_error_new(ERROR_RTC_STUN, "datagram %d is not DTLS, first=%u", i, first);
            }
        }

        // Decode into a new packet, as SRS and a client do, so no attribute is left from an earlier one.
        response = SrsStunPacket();
        if ((err = response.decode(last.data(), (int)last.size())) != srs_success) {
            return srs_error_wrap(err, "decode response");
        }
        if (!response.is_binding_response() || response.get_transcation_id() != tid) {
            return srs_error_new(ERROR_RTC_STUN, "not the response of the check");
        }
        return err;
    }
};

// The SPED attributes a client reads from a decoded response.
static SpedStunMessage sped_attributes_of(SrsStunPacket &response)
{
    SpedStunMessage msg;
    msg.has_data_ = response.has_dtls_in_stun_data();
    msg.data_ = response.get_dtls_in_stun_data();
    msg.has_acks_ = response.has_dtls_in_stun_ack();
    msg.acks_ = response.get_dtls_in_stun_acks();
    return msg;
}

// The client embeds its ClientHello in the first check, as SPED allows. SRS starts its DTLS
// handshake and feeds it the ClientHello before it builds the response, so the response
// carries the ACK of the ClientHello and the server flight. The flight also goes raw, before
// the response.
VOID TEST(RtcSpedUdpNetworkTest, EmbeddedClientHelloGetsServerFlightInResponse)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);
    EXPECT_EQ(SrsSpedStateTentative, sped->state());

    MockDtlsCallbackForHandshake ccb;
    SrsDtls client(&ccb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());
    ASSERT_EQ(1, (int)ccb.packets_.size());
    string client_hello = ccb.packets_[0];

    vector<string> raw;
    SrsStunPacket response;
    HELPER_ASSERT_SUCCESS(session.check(sped_request_with(client_hello), raw, response));
    EXPECT_EQ(SrsRtcNetworkStateDtls, session.network_->state_);
    EXPECT_EQ(SrsSpedStateConfirmed, sped->state());

    // The server flight starts with a handshake record(22) of type server_hello(2).
    ASSERT_EQ(1, (int)raw.size());
    ASSERT_GE((int)raw[0].size(), 14);
    EXPECT_EQ(22, (uint8_t)raw[0][0]);
    EXPECT_EQ(2, (uint8_t)raw[0][13]);

    ASSERT_TRUE(response.has_dtls_in_stun_ack());
    vector<uint32_t> acks = response.get_dtls_in_stun_acks();
    ASSERT_EQ(1, (int)acks.size());
    EXPECT_EQ(sped_crc(client_hello), acks[0]);
    ASSERT_TRUE(response.has_dtls_in_stun_data());
    EXPECT_TRUE(response.get_dtls_in_stun_data() == raw[0]);
}

// Every raw DTLS datagram is lost both ways, so the handshake runs only inside the checks and
// their responses. At each check SRS is in the state of the handshake so far, and once the
// client ACKs SRS's last flight, SPED is complete and the responses are as without SPED.
VOID TEST(RtcSpedUdpNetworkTest, HandshakeCompletesInsideStunWhenRawIsLost)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);

    MockDtlsCallbackForHandshake ccb;
    SrsDtls client(&ccb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
    client.enable_sped();
    SrsSpedController *client_sped = client.sped();
    ASSERT_TRUE(client_sped != NULL);
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());

    int rounds = 0;
    bool was_established = false;
    for (; rounds < 16 && sped->state() != SrsSpedStateComplete; rounds++) {
        vector<string> raw;
        SrsStunPacket response;
        HELPER_ASSERT_SUCCESS(session.check(sped_build(client_sped, BindingRequest), raw, response));

        // DTLS runs from the first check, and SRS waits for the ACK of its last flight
        // only after the handshake is done.
        bool established = session.network_->state_ == SrsRtcNetworkStateEstablished;
        EXPECT_TRUE(established || session.network_->state_ == SrsRtcNetworkStateDtls);
        if (sped->state() == SrsSpedStateComplete) {
            EXPECT_TRUE(was_established);
        } else {
            EXPECT_EQ(established ? SrsSpedStatePending : SrsSpedStateConfirmed, sped->state());
        }

        // A response carries SPED attributes until SPED is complete.
        bool complete = sped->state() == SrsSpedStateComplete;
        EXPECT_EQ(!complete, response.has_dtls_in_stun_ack());
        EXPECT_EQ(!complete, response.has_dtls_in_stun_data());

        SpedStunMessage msg = sped_attributes_of(response);
        HELPER_ASSERT_SUCCESS(sped_deliver(client, msg));
        was_established = established;
        ccb.packets_.clear();
    }

    EXPECT_LT(rounds, 16);
    EXPECT_EQ(SrsSpedStateComplete, sped->state());
    EXPECT_EQ(SrsRtcNetworkStateEstablished, session.network_->state_);
    EXPECT_EQ(1, ccb.handshake_done_count_);
    EXPECT_EQ(SrsSpedStateComplete, client_sped->state());

    // A later check gets the response it gets without SPED.
    vector<string> raw;
    SrsStunPacket response;
    HELPER_ASSERT_SUCCESS(session.check(sped_build(client_sped, BindingRequest), raw, response));
    EXPECT_TRUE(raw.empty());
    EXPECT_FALSE(response.has_dtls_in_stun_ack());
    EXPECT_FALSE(response.has_dtls_in_stun_data());
}

// A client that negotiated SPED but sends a first check with neither attribute does not do
// SPED: SRS answers as without SPED, and the handshake goes raw.
VOID TEST(RtcSpedUdpNetworkTest, CheckWithoutAttributesFallsBackToOff)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);

    vector<string> raw;
    SrsStunPacket response;
    HELPER_ASSERT_SUCCESS(session.check(SpedStunMessage(), raw, response));
    EXPECT_EQ(SrsSpedStateOff, sped->state());
    EXPECT_EQ(SrsRtcNetworkStateDtls, session.network_->state_);
    EXPECT_TRUE(raw.empty());
    EXPECT_FALSE(response.has_dtls_in_stun_ack());
    EXPECT_FALSE(response.has_dtls_in_stun_data());

    MockDtlsCallbackForHandshake ccb;
    SrsDtls client(&ccb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());
    ASSERT_EQ(1, (int)ccb.packets_.size());

    session.socket_.sent_.clear();
    string client_hello = ccb.packets_[0];
    HELPER_ASSERT_SUCCESS(session.network_->on_dtls(&client_hello[0], (int)client_hello.size()));
    ASSERT_EQ(1, (int)session.socket_.sent_.size());
    EXPECT_EQ(22, (uint8_t)session.socket_.sent_[0][0]);

    HELPER_ASSERT_SUCCESS(session.check(SpedStunMessage(), raw, response));
    EXPECT_TRUE(raw.empty());
    EXPECT_FALSE(response.has_dtls_in_stun_ack());
    EXPECT_FALSE(response.has_dtls_in_stun_data());
}

// Without SPED negotiated, a check is answered as before: the response goes first and has no
// SPED attribute, and an embedded ClientHello is not read, so DTLS waits for the raw one.
// This passes before the SPED path exists, as it locks in the session without SPED.
VOID TEST(RtcSpedUdpNetworkTest, WithoutSpedTheResponseIsUnchanged)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(false));
    EXPECT_TRUE(session.sped() == NULL);

    MockDtlsCallbackForHandshake ccb;
    SrsDtls client(&ccb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());
    ASSERT_EQ(1, (int)ccb.packets_.size());
    string client_hello = ccb.packets_[0];

    vector<string> raw;
    SrsStunPacket response;
    HELPER_ASSERT_SUCCESS(session.check(sped_request_with(client_hello), raw, response));
    EXPECT_EQ(SrsRtcNetworkStateDtls, session.network_->state_);
    EXPECT_TRUE(raw.empty());
    EXPECT_FALSE(response.has_dtls_in_stun_ack());
    EXPECT_FALSE(response.has_dtls_in_stun_data());

    session.socket_.sent_.clear();
    HELPER_ASSERT_SUCCESS(session.network_->on_dtls(&client_hello[0], (int)client_hello.size()));
    ASSERT_EQ(1, (int)session.socket_.sent_.size());
    EXPECT_EQ(22, (uint8_t)session.socket_.sent_[0][0]);
}

// A datagram that does not fit the response's STUN body is not embedded, and only goes raw,
// while the response still carries the ACK list.
VOID TEST(RtcSpedUdpNetworkTest, DatagramTooLargeIsNotEmbedded)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);

    SpedStunMessage empty_ack;
    empty_ack.has_acks_ = true;
    vector<string> raw;
    SrsStunPacket response;
    HELPER_ASSERT_SUCCESS(session.check(empty_ack, raw, response));
    EXPECT_EQ(SrsSpedStateConfirmed, sped->state());

    string big = sped_dtls_datagram(22, string(kStunSpedMaxBodySize, 'x'));
    sped->capture(big.data(), (int)big.size());
    sped->flush();

    HELPER_ASSERT_SUCCESS(session.check(empty_ack, raw, response));
    EXPECT_TRUE(response.has_dtls_in_stun_ack());
    EXPECT_FALSE(response.has_dtls_in_stun_data());
    EXPECT_EQ(SrsSpedStateConfirmed, sped->state());
}

// An SRS session over TCP, with the real DTLS and SRTP transport, as the DTLS server. Each
// packet SRS sends is one write of a 2-byte length and the packet, as RFC 4571 frames it.
class SpedTcpSession
{
public:
    MockRtcConnectionForUdpNetwork conn_;
    MockEphemeralDelta delta_;
    MockProtocolReadWriterForTcpNetwork socket_;
    SrsRtcTcpNetwork *network_;
    int checks_;

public:
    SpedTcpSession()
    {
        network_ = new SrsRtcTcpNetwork(&conn_, &delta_);
        checks_ = 0;
    }
    virtual ~SpedTcpSession()
    {
        network_->sendonly_skt_ = NULL;
        srs_freep(network_);
    }

public:
    srs_error_t initialize(bool sped, string version = "dtls1.2")
    {
        srs_error_t err = srs_success;

        SrsSessionConfig cfg;
        cfg.dtls_role_ = "passive";
        cfg.dtls_version_ = version;
        cfg.remote_fingerprint_algo_ = "sha-256";
        cfg.remote_fingerprint_ = _srs_rtc_dtls_certificate->get_fingerprint();
        cfg.sped_ = sped;
        if ((err = network_->initialize(&cfg, true, true)) != srs_success) {
            return srs_error_wrap(err, "init");
        }

        network_->update_sendonly_socket(&socket_);
        network_->set_peer_id("192.168.1.10", 10000);
        network_->set_state(SrsRtcNetworkStateWaitingStun);
        return err;
    }

    SrsSpedController *sped()
    {
        return network_->transport_->sped();
    }

    SrsDtls *dtls()
    {
        SrsSecurityTransport *transport = dynamic_cast<SrsSecurityTransport *>(network_->transport_);
        return dynamic_cast<SrsDtls *>(transport->dtls_);
    }

    // The packets SRS sent since the last clear, each without its 2-byte length, which must
    // match the packet.
    srs_error_t sent(vector<string> &packets)
    {
        packets.clear();
        for (int i = 0; i < (int)socket_.written_data_.size(); i++) {
            string &frame = socket_.written_data_[i];
            if (frame.size() < 2) {
                return srs_error_new(ERROR_RTC_STUN, "frame %d has no length", i);
            }
            int size = ((uint8_t)frame[0] << 8) | (uint8_t)frame[1];
            if (size != (int)frame.size() - 2) {
                return srs_error_new(ERROR_RTC_STUN, "frame %d length %d, body %d", i, size, (int)frame.size() - 2);
            }
            packets.push_back(frame.substr(2));
        }
        return srs_success;
    }

    // Send SRS an authenticated check with the SPED attributes, and get what SRS sent back:
    // the raw DTLS packets, and the one STUN response, which goes last.
    srs_error_t check(const SpedStunMessage &msg, vector<string> &raw, SrsStunPacket &response)
    {
        srs_error_t err = srs_success;

        SrsStunPacket request;
        request.set_message_type(BindingRequest);
        request.set_local_ufrag("srsufrag");
        request.set_remote_ufrag("peerufrag");
        string tid = "sped-tcpck00";
        tid[10] = (char)('0' + (checks_ / 10) % 10);
        tid[11] = (char)('0' + checks_ % 10);
        checks_++;
        request.set_transcation_id(tid);
        request.has_dtls_in_stun_data_ = msg.has_data_;
        request.dtls_in_stun_data_ = msg.data_;
        request.has_dtls_in_stun_ack_ = msg.has_acks_;
        request.dtls_in_stun_acks_ = msg.acks_;

        socket_.written_data_.clear();
        char buf[20];
        memset(buf, 0, sizeof(buf));
        if ((err = network_->on_stun(&request, buf, sizeof(buf))) != srs_success) {
            return srs_error_wrap(err, "on stun");
        }

        vector<string> packets;
        if ((err = sent(packets)) != srs_success) {
            return srs_error_wrap(err, "sent");
        }
        if (packets.empty()) {
            return srs_error_new(ERROR_RTC_STUN, "no response");
        }
        string last = packets.back();
        raw.assign(packets.begin(), packets.end() - 1);
        for (int i = 0; i < (int)raw.size(); i++) {
            string &packet = raw[i];
            uint8_t first = (uint8_t)packet[0];
            if (first < 20 || first > 63) {
                return srs_error_new(ERROR_RTC_STUN, "packet %d is not DTLS, first=%u", i, first);
            }
        }

        // Decode into a new packet, as SRS and a client do, so no attribute is left from an earlier one.
        response = SrsStunPacket();
        if ((err = response.decode(last.data(), (int)last.size())) != srs_success) {
            return srs_error_wrap(err, "decode response");
        }
        if (!response.is_binding_response() || response.get_transcation_id() != tid) {
            return srs_error_new(ERROR_RTC_STUN, "not the response of the check");
        }
        return err;
    }
};

// Over TCP, as over UDP, a ClientHello embedded in the first check gets the ACK of the
// ClientHello and the server flight in the response, and the flight also goes raw first, each
// in its own length-prefixed frame.
VOID TEST(RtcSpedTcpNetworkTest, EmbeddedClientHelloGetsServerFlightInResponse)
{
    srs_error_t err;

    SpedTcpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);
    EXPECT_EQ(SrsSpedStateTentative, sped->state());

    MockDtlsCallbackForHandshake ccb;
    SrsDtls client(&ccb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());
    ASSERT_EQ(1, (int)ccb.packets_.size());
    string client_hello = ccb.packets_[0];

    vector<string> raw;
    SrsStunPacket response;
    HELPER_ASSERT_SUCCESS(session.check(sped_request_with(client_hello), raw, response));
    EXPECT_EQ(SrsRtcNetworkStateDtls, session.network_->state_);
    EXPECT_EQ(SrsSpedStateConfirmed, sped->state());

    // The server flight starts with a handshake record(22) of type server_hello(2).
    ASSERT_EQ(1, (int)raw.size());
    ASSERT_GE((int)raw[0].size(), 14);
    EXPECT_EQ(22, (uint8_t)raw[0][0]);
    EXPECT_EQ(2, (uint8_t)raw[0][13]);

    ASSERT_TRUE(response.has_dtls_in_stun_ack());
    vector<uint32_t> acks = response.get_dtls_in_stun_acks();
    ASSERT_EQ(1, (int)acks.size());
    EXPECT_EQ(sped_crc(client_hello), acks[0]);
    ASSERT_TRUE(response.has_dtls_in_stun_data());
    EXPECT_TRUE(response.get_dtls_in_stun_data() == raw[0]);

    // The embedded ClientHello counts as received bytes, as a raw one does.
    EXPECT_EQ((int)client_hello.size(), session.delta_.in_bytes_);
}

// The raw DTLS packets never reach the client, so the handshake runs only inside the checks
// and their responses. At each check SRS is in the state of the handshake so far, and once the
// client ACKs SRS's last flight, SPED is complete and the responses are as without SPED.
VOID TEST(RtcSpedTcpNetworkTest, HandshakeCompletesInsideStunWhenRawIsLost)
{
    srs_error_t err;

    SpedTcpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);

    MockDtlsCallbackForHandshake ccb;
    SrsDtls client(&ccb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
    client.enable_sped();
    SrsSpedController *client_sped = client.sped();
    ASSERT_TRUE(client_sped != NULL);
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());

    int rounds = 0;
    bool was_established = false;
    for (; rounds < 16 && sped->state() != SrsSpedStateComplete; rounds++) {
        vector<string> raw;
        SrsStunPacket response;
        HELPER_ASSERT_SUCCESS(session.check(sped_build(client_sped, BindingRequest), raw, response));

        bool established = session.network_->state_ == SrsRtcNetworkStateEstablished;
        EXPECT_TRUE(established || session.network_->state_ == SrsRtcNetworkStateDtls);
        if (sped->state() == SrsSpedStateComplete) {
            EXPECT_TRUE(was_established);
        } else {
            EXPECT_EQ(established ? SrsSpedStatePending : SrsSpedStateConfirmed, sped->state());
        }

        bool complete = sped->state() == SrsSpedStateComplete;
        EXPECT_EQ(!complete, response.has_dtls_in_stun_ack());
        EXPECT_EQ(!complete, response.has_dtls_in_stun_data());

        SpedStunMessage msg = sped_attributes_of(response);
        HELPER_ASSERT_SUCCESS(sped_deliver(client, msg));
        was_established = established;
        ccb.packets_.clear();
    }

    EXPECT_LT(rounds, 16);
    EXPECT_EQ(SrsSpedStateComplete, sped->state());
    EXPECT_EQ(SrsRtcNetworkStateEstablished, session.network_->state_);
    EXPECT_EQ(1, ccb.handshake_done_count_);
    EXPECT_EQ(SrsSpedStateComplete, client_sped->state());

    vector<string> raw;
    SrsStunPacket response;
    HELPER_ASSERT_SUCCESS(session.check(sped_build(client_sped, BindingRequest), raw, response));
    EXPECT_TRUE(raw.empty());
    EXPECT_FALSE(response.has_dtls_in_stun_ack());
    EXPECT_FALSE(response.has_dtls_in_stun_data());
}

// A first check with neither attribute turns SPED off: SRS answers as without SPED, and the
// handshake goes raw over the TCP stream.
VOID TEST(RtcSpedTcpNetworkTest, CheckWithoutAttributesFallsBackToOff)
{
    srs_error_t err;

    SpedTcpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);

    vector<string> raw;
    SrsStunPacket response;
    HELPER_ASSERT_SUCCESS(session.check(SpedStunMessage(), raw, response));
    EXPECT_EQ(SrsSpedStateOff, sped->state());
    EXPECT_EQ(SrsRtcNetworkStateDtls, session.network_->state_);
    EXPECT_TRUE(raw.empty());
    EXPECT_FALSE(response.has_dtls_in_stun_ack());
    EXPECT_FALSE(response.has_dtls_in_stun_data());

    MockDtlsCallbackForHandshake ccb;
    SrsDtls client(&ccb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());
    ASSERT_EQ(1, (int)ccb.packets_.size());

    session.socket_.written_data_.clear();
    string client_hello = ccb.packets_[0];
    HELPER_ASSERT_SUCCESS(session.network_->on_dtls(&client_hello[0], (int)client_hello.size()));
    vector<string> packets;
    HELPER_ASSERT_SUCCESS(session.sent(packets));
    ASSERT_EQ(1, (int)packets.size());
    EXPECT_EQ(22, (uint8_t)packets[0][0]);

    HELPER_ASSERT_SUCCESS(session.check(SpedStunMessage(), raw, response));
    EXPECT_TRUE(raw.empty());
    EXPECT_FALSE(response.has_dtls_in_stun_ack());
    EXPECT_FALSE(response.has_dtls_in_stun_data());
}

// Without SPED negotiated, a TCP check is answered as before: the response goes first and has
// no SPED attribute, and an embedded ClientHello is not read. This passes before the TCP SPED
// path exists, as it locks in the session without SPED.
VOID TEST(RtcSpedTcpNetworkTest, WithoutSpedTheResponseIsUnchanged)
{
    srs_error_t err;

    SpedTcpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(false));
    EXPECT_TRUE(session.sped() == NULL);

    MockDtlsCallbackForHandshake ccb;
    SrsDtls client(&ccb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());
    ASSERT_EQ(1, (int)ccb.packets_.size());
    string client_hello = ccb.packets_[0];

    vector<string> raw;
    SrsStunPacket response;
    HELPER_ASSERT_SUCCESS(session.check(sped_request_with(client_hello), raw, response));
    EXPECT_EQ(SrsRtcNetworkStateDtls, session.network_->state_);
    EXPECT_TRUE(raw.empty());
    EXPECT_FALSE(response.has_dtls_in_stun_ack());
    EXPECT_FALSE(response.has_dtls_in_stun_data());
    EXPECT_EQ(0, session.delta_.in_bytes_);

    session.socket_.written_data_.clear();
    HELPER_ASSERT_SUCCESS(session.network_->on_dtls(&client_hello[0], (int)client_hello.size()));
    vector<string> packets;
    HELPER_ASSERT_SUCCESS(session.sent(packets));
    ASSERT_EQ(1, (int)packets.size());
    EXPECT_EQ(22, (uint8_t)packets[0][0]);
}

// A datagram that does not fit the response's STUN body is not embedded over TCP either,
// though a TCP frame could carry it, so both transports send the same STUN messages.
VOID TEST(RtcSpedTcpNetworkTest, DatagramTooLargeIsNotEmbedded)
{
    srs_error_t err;

    SpedTcpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);

    SpedStunMessage empty_ack;
    empty_ack.has_acks_ = true;
    vector<string> raw;
    SrsStunPacket response;
    HELPER_ASSERT_SUCCESS(session.check(empty_ack, raw, response));
    EXPECT_EQ(SrsSpedStateConfirmed, sped->state());

    string big = sped_dtls_datagram(22, string(kStunSpedMaxBodySize, 'x'));
    sped->capture(big.data(), (int)big.size());
    sped->flush();

    HELPER_ASSERT_SUCCESS(session.check(empty_ack, raw, response));
    EXPECT_TRUE(response.has_dtls_in_stun_ack());
    EXPECT_FALSE(response.has_dtls_in_stun_data());
    EXPECT_EQ(SrsSpedStateConfirmed, sped->state());
}

// Shrink the DTLS MTU, so OpenSSL splits a flight into more datagrams. 256 is the smallest
// MTU OpenSSL takes.
static srs_error_t sped_set_mtu(SrsDtls *dtls, int mtu)
{
    SSL *ssl = dtls->impl_->dtls_;
    if (!SSL_set_mtu(ssl, mtu) || !DTLS_set_link_mtu(ssl, mtu)) {
        return srs_error_new(ERROR_OpenSslCreateSSL, "set mtu %d", mtu);
    }
    return srs_success;
}

// A SPED client: an in-process OpenSSL client with a controller of its own, which embeds its
// flights in its checks and reads the responses. Its raw DTLS is lost unless a test hands it
// to SRS.
class SpedClient
{
public:
    MockDtlsCallbackForHandshake cb_;
    SrsDtls dtls_;

public:
    SpedClient() : dtls_(&cb_)
    {
    }

public:
    srs_error_t start(int mtu, string version = "dtls1.2", string server_name = "")
    {
        srs_error_t err = srs_success;

        if ((err = dtls_.initialize("active", version)) != srs_success) {
            return srs_error_wrap(err, "init");
        }
        if (!server_name.empty() && !SSL_set_tlsext_host_name(dtls_.impl_->dtls_, server_name.c_str())) {
            return srs_error_new(ERROR_OpenSslCreateSSL, "server name");
        }
        dtls_.enable_sped();
        if (mtu && (err = sped_set_mtu(&dtls_, mtu)) != srs_success) {
            return srs_error_wrap(err, "mtu");
        }
        if ((err = dtls_.start_active_handshake()) != srs_success) {
            return srs_error_wrap(err, "start");
        }
        return err;
    }

    SrsSpedController *sped()
    {
        return dtls_.sped();
    }

    bool done()
    {
        return cb_.handshake_done_count_ > 0;
    }

    SpedStunMessage request()
    {
        return sped_build(sped(), BindingRequest);
    }

    srs_error_t on_response(SrsStunPacket &response)
    {
        SpedStunMessage msg = sped_attributes_of(response);
        return sped_deliver(dtls_, msg);
    }
};

static bool sped_has(const vector<uint32_t> &acks, uint32_t crc)
{
    return std::find(acks.begin(), acks.end(), crc) != acks.end();
}

// Run the checks of a client whose raw DTLS is lost until SRS is done with DTLS and waits for
// the ACK of its last flight, and the client has read that flight.
template <typename T>
static srs_error_t sped_run_to_pending(T &session, SpedClient &client)
{
    srs_error_t err = srs_success;

    for (int i = 0; i < 16; i++) {
        vector<string> raw;
        SrsStunPacket response;
        if ((err = session.check(client.request(), raw, response)) != srs_success) {
            return srs_error_wrap(err, "check %d", i);
        }
        if ((err = client.on_response(response)) != srs_success) {
            return srs_error_wrap(err, "response %d", i);
        }
        if (session.sped()->state() == SrsSpedStatePending && client.done()) {
            return err;
        }
    }

    return srs_error_new(ERROR_RTC_DTLS, "not pending, state=%s", srs_sped_state_str(session.sped()->state()));
}

// An RTP packet with a 100-byte payload, in a buffer with room for the SRTP tag.
static int sped_rtp_packet(char *buf)
{
    memset(buf, 0, 1500);
    buf[0] = (char)0x80;
    buf[1] = 96;
    buf[3] = 1;
    buf[11] = 1;
    memset(buf + 12, 0xab, 100);
    return 112;
}

// An empty RTCP receiver report, in a buffer with room for the SRTCP index and tag.
static int sped_rtcp_packet(char *buf)
{
    memset(buf, 0, 1500);
    buf[0] = (char)0x80;
    buf[1] = (char)201;
    buf[3] = 1;
    buf[7] = 1;
    return 8;
}

// The response carrying the server flight is lost. The client has had no ACK of its
// ClientHello, so its next check, with a new transaction ID, embeds the same cached bytes.
// OpenSSL drops the copy, so SRS writes nothing new, and the response embeds the same server
// flight again, with the ACK, and the handshake goes on inside STUN.
VOID TEST(RtcSpedLossTest, LostResponseGetsTheSameServerFlightAgain)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);

    SpedClient client;
    HELPER_ASSERT_SUCCESS(client.start(0));
    ASSERT_EQ(1, (int)client.cb_.packets_.size());
    string client_hello = client.cb_.packets_[0];

    SpedStunMessage first = client.request();
    EXPECT_TRUE(first.data_ == client_hello);
    vector<string> raw;
    SrsStunPacket lost;
    HELPER_ASSERT_SUCCESS(session.check(first, raw, lost));
    ASSERT_EQ(1, (int)raw.size());
    vector<string> server_flight = raw;
    ASSERT_TRUE(lost.has_dtls_in_stun_data());
    EXPECT_TRUE(lost.get_dtls_in_stun_data() == server_flight[0]);
    EXPECT_EQ(SrsSpedStateConfirmed, sped->state());

    SpedStunMessage again = client.request();
    EXPECT_TRUE(again.data_ == client_hello);
    EXPECT_TRUE(again.has_acks_);
    EXPECT_TRUE(again.acks_.empty());

    SrsStunPacket response;
    HELPER_ASSERT_SUCCESS(session.check(again, raw, response));
    EXPECT_TRUE(raw.empty());
    EXPECT_EQ(SrsSpedStateConfirmed, sped->state());
    EXPECT_TRUE(sped->flight_ == server_flight);
    ASSERT_TRUE(response.has_dtls_in_stun_data());
    EXPECT_TRUE(response.get_dtls_in_stun_data() == server_flight[0]);
    vector<uint32_t> acks = response.get_dtls_in_stun_acks();
    ASSERT_EQ(1, (int)acks.size());
    EXPECT_EQ(sped_crc(client_hello), acks[0]);

    HELPER_ASSERT_SUCCESS(client.on_response(response));
    EXPECT_EQ(SrsSpedStateConfirmed, client.sped()->state());
    EXPECT_TRUE(sped_has(client.sped()->acks_, sped_crc(server_flight[0])));

    HELPER_ASSERT_SUCCESS(sped_run_to_pending(session, client));
    HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, response));
    EXPECT_EQ(SrsSpedStateComplete, sped->state());
    EXPECT_EQ(SrsRtcNetworkStateEstablished, session.network_->state_);
    sped_expect_srtp_keys_match(client.dtls_, *session.dtls());
}

// The check that embeds the client's last flight is lost, and so is its raw copy. SRS still
// waits in the handshake, and the client, with no ACK of that flight, embeds the same bytes in
// its next check, which completes SRS's handshake; that response ACKs the flight and embeds
// SRS's last flight.
VOID TEST(RtcSpedLossTest, LostClientFlightIsEmbeddedAgain)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);

    SpedClient client;
    HELPER_ASSERT_SUCCESS(client.start(0));

    vector<string> raw;
    SrsStunPacket response;
    HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, response));
    vector<string> server_flight = raw;
    client.cb_.packets_.clear();
    HELPER_ASSERT_SUCCESS(client.on_response(response));
    ASSERT_FALSE(client.cb_.packets_.empty());
    vector<string> client_flight = client.cb_.packets_;
    ASSERT_EQ(1, (int)client_flight.size());
    EXPECT_TRUE(client.sped()->flight_ == client_flight);

    SpedStunMessage lost = client.request();
    EXPECT_TRUE(lost.data_ == client_flight[0]);
    EXPECT_TRUE(sped_has(lost.acks_, sped_crc(server_flight[0])));
    EXPECT_EQ(SrsSpedStateConfirmed, sped->state());
    EXPECT_EQ(SrsRtcNetworkStateDtls, session.network_->state_);

    SpedStunMessage again = client.request();
    EXPECT_TRUE(again.data_ == client_flight[0]);
    HELPER_ASSERT_SUCCESS(session.check(again, raw, response));
    EXPECT_EQ(SrsRtcNetworkStateEstablished, session.network_->state_);
    EXPECT_EQ(SrsSpedStatePending, sped->state());
    ASSERT_FALSE(raw.empty());
    EXPECT_TRUE(sped->flight_ == raw);
    EXPECT_TRUE(sped_has(response.get_dtls_in_stun_acks(), sped_crc(client_flight[0])));
    ASSERT_TRUE(response.has_dtls_in_stun_data());
    EXPECT_TRUE(response.get_dtls_in_stun_data() == raw[0]);

    HELPER_ASSERT_SUCCESS(client.on_response(response));
    EXPECT_TRUE(client.done());
    HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, response));
    EXPECT_EQ(SrsSpedStateComplete, sped->state());
    sped_expect_srtp_keys_match(client.dtls_, *session.dtls());
}

// The check that embeds the client's last flight is lost, but its raw copy arrives, as a
// client that also sends each flight raw does. SRS completes its handshake from the raw copy
// and ACKs it in the next response, though that check embeds the flight once more.
VOID TEST(RtcSpedLossTest, LostEmbeddedClientFlightIsAckedFromItsRawCopy)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);

    SpedClient client;
    HELPER_ASSERT_SUCCESS(client.start(0));

    vector<string> raw;
    SrsStunPacket response;
    HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, response));
    client.cb_.packets_.clear();
    HELPER_ASSERT_SUCCESS(client.on_response(response));
    vector<string> client_flight = client.cb_.packets_;
    ASSERT_EQ(1, (int)client_flight.size());

    SpedStunMessage lost = client.request();
    EXPECT_TRUE(lost.data_ == client_flight[0]);

    session.socket_.sent_.clear();
    string raw_copy = client_flight[0];
    HELPER_ASSERT_SUCCESS(session.network_->on_dtls(&raw_copy[0], (int)raw_copy.size()));
    EXPECT_EQ(SrsRtcNetworkStateEstablished, session.network_->state_);
    EXPECT_EQ(SrsSpedStatePending, sped->state());
    EXPECT_TRUE(sped_has(sped->acks_, sped_crc(client_flight[0])));
    ASSERT_FALSE(session.socket_.sent_.empty());
    vector<string> server_finished = session.socket_.sent_;
    EXPECT_TRUE(sped->flight_ == server_finished);

    // The client has no ACK of its flight yet, so it embeds the same bytes again. OpenSSL, done,
    // drops the copy, whose record numbers it has seen, so it sends nothing new, and the response
    // embeds the last flight SRS kept.
    SpedStunMessage again = client.request();
    EXPECT_TRUE(again.data_ == client_flight[0]);
    HELPER_ASSERT_SUCCESS(session.check(again, raw, response));
    EXPECT_EQ(SrsSpedStatePending, sped->state());
    EXPECT_TRUE(sped_has(response.get_dtls_in_stun_acks(), sped_crc(client_flight[0])));
    EXPECT_TRUE(raw.empty());
    EXPECT_TRUE(sped->flight_ == server_finished);
    ASSERT_TRUE(response.has_dtls_in_stun_data());
    EXPECT_TRUE(response.get_dtls_in_stun_data() == server_finished[0]);

    HELPER_ASSERT_SUCCESS(client.on_response(response));
    EXPECT_TRUE(client.done());
    EXPECT_TRUE(client.sped()->flight_.empty());
    HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, response));
    EXPECT_EQ(SrsSpedStateComplete, sped->state());
    EXPECT_EQ(1, client.cb_.handshake_done_count_);
    sped_expect_srtp_keys_match(client.dtls_, *session.dtls());
}

// With a small DTLS MTU both sides split their flights, as Chrome splits its post-quantum
// ClientHello. The first fragment of the ClientHello makes no server flight, so its response
// carries only the ACK, as Chrome 158 sees live. Each side embeds its datagrams round robin,
// and with every raw datagram lost, the handshake still completes inside STUN. A long server name
// stands in for Chrome's post-quantum key share, so the DTLS 1.2 ClientHello takes two datagrams
// however few suites the OpenSSL build has.
VOID TEST(RtcSpedLossTest, FragmentedFlightsCompleteInsideStun)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);
    HELPER_ASSERT_SUCCESS(sped_set_mtu(session.dtls(), 256));

    SpedClient client;
    HELPER_ASSERT_SUCCESS(client.start(256, "dtls1.2", string(200, 'a') + ".example.com"));
    vector<string> client_hello = client.cb_.packets_;
    ASSERT_GE((int)client_hello.size(), 2);
    EXPECT_TRUE(client.sped()->flight_ == client_hello);

    vector<string> raw;
    SrsStunPacket response;
    HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, response));
    EXPECT_EQ(SrsSpedStateConfirmed, sped->state());
    EXPECT_TRUE(raw.empty());
    EXPECT_FALSE(response.has_dtls_in_stun_data());
    vector<uint32_t> acks = response.get_dtls_in_stun_acks();
    ASSERT_EQ(1, (int)acks.size());
    EXPECT_EQ(sped_crc(client_hello[0]), acks[0]);
    HELPER_ASSERT_SUCCESS(client.on_response(response));
    EXPECT_EQ((int)client_hello.size() - 1, (int)client.sped()->flight_.size());

    // Every datagram SRS embeds is one it wrote, and the server flight takes several.
    vector<string> server_sent;
    int server_flight_size = 0;
    int rounds = 0;
    for (; rounds < 32 && sped->state() != SrsSpedStateComplete; rounds++) {
        HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, response));
        server_sent.insert(server_sent.end(), raw.begin(), raw.end());
        if (server_flight_size == 0 && !raw.empty()) {
            server_flight_size = (int)raw.size();
        }

        EXPECT_LE((int)response.get_dtls_in_stun_acks().size(), kStunDtlsInStunMaxAcks);
        if (response.has_dtls_in_stun_data()) {
            string data = response.get_dtls_in_stun_data();
            EXPECT_TRUE(std::find(server_sent.begin(), server_sent.end(), data) != server_sent.end());
        }

        HELPER_ASSERT_SUCCESS(client.on_response(response));
    }

    EXPECT_LT(rounds, 32);
    EXPECT_GE(server_flight_size, 2);
    EXPECT_EQ(SrsSpedStateComplete, sped->state());
    EXPECT_EQ(SrsRtcNetworkStateEstablished, session.network_->state_);
    EXPECT_EQ(1, client.cb_.handshake_done_count_);
    sped_expect_srtp_keys_match(client.dtls_, *session.dtls());
}

// A client checks two candidate pairs of one session. The SPED state belongs to the one DTLS
// association, so a flight embedded on one pair is ACKed on the other: the response lost on
// the first pair is embedded again on the second, and SPED completes on whichever pair
// carries the last ACK. Each response goes back on the pair of its check.
VOID TEST(RtcSpedLossTest, TwoPairsShareOneSession)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);

    MockUdpMuxSocketForSped &pair_a = session.socket_;
    MockUdpMuxSocketForSped pair_b;
    pair_b.peer_ip_ = pair_a.peer_ip_;
    pair_b.peer_port_ = pair_a.peer_port_ + 1;
    pair_b.peer_id_ = "192.168.1.100:5001";
    ASSERT_NE(pair_a.peer_id_, pair_b.peer_id_);

    SpedClient client;
    HELPER_ASSERT_SUCCESS(client.start(0));
    string client_hello = client.cb_.packets_[0];

    vector<string> raw;
    SrsStunPacket lost;
    HELPER_ASSERT_SUCCESS(session.check_on(&pair_a, client.request(), raw, lost));
    ASSERT_TRUE(lost.has_dtls_in_stun_data());

    SrsStunPacket response;
    HELPER_ASSERT_SUCCESS(session.check_on(&pair_b, client.request(), raw, response));
    EXPECT_EQ(2, (int)session.network_->peer_addresses_.size());
    EXPECT_TRUE(session.sped() == sped);
    EXPECT_EQ(SrsSpedStateConfirmed, sped->state());
    EXPECT_TRUE(response.get_dtls_in_stun_data() == lost.get_dtls_in_stun_data());
    EXPECT_TRUE(sped_has(response.get_dtls_in_stun_acks(), sped_crc(client_hello)));
    HELPER_ASSERT_SUCCESS(client.on_response(response));

    HELPER_ASSERT_SUCCESS(session.check_on(&pair_a, client.request(), raw, response));
    EXPECT_EQ(SrsSpedStatePending, sped->state());
    EXPECT_EQ(SrsRtcNetworkStateEstablished, session.network_->state_);
    ASSERT_TRUE(response.has_dtls_in_stun_data());
    HELPER_ASSERT_SUCCESS(client.on_response(response));
    EXPECT_TRUE(client.done());

    HELPER_ASSERT_SUCCESS(session.check_on(&pair_b, client.request(), raw, response));
    EXPECT_EQ(SrsSpedStateComplete, sped->state());
    EXPECT_FALSE(response.has_dtls_in_stun_ack());
    EXPECT_FALSE(response.has_dtls_in_stun_data());
    sped_expect_srtp_keys_match(client.dtls_, *session.dtls());
}

// SRS is done and waits for the ACK of its last flight, but the client's next check is lost
// and SRTP comes first. Only a packet that decrypts proves the client finished DTLS: one that
// fails leaves SPED pending, and the first that decrypts completes it.
VOID TEST(RtcSpedLossTest, SrtpWhilePendingCompletes)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);

    SpedClient client;
    HELPER_ASSERT_SUCCESS(client.start(0));
    HELPER_ASSERT_SUCCESS(sped_run_to_pending(session, client));
    EXPECT_EQ(SrsRtcNetworkStateEstablished, session.network_->state_);
    EXPECT_FALSE(sped->flight_.empty());

    char buf[1500];
    int nb = sped_rtp_packet(buf);
    HELPER_EXPECT_FAILED(session.network_->on_rtp(buf, nb));
    EXPECT_FALSE(session.conn_.on_rtp_plaintext_called_);
    EXPECT_EQ(SrsSpedStatePending, sped->state());

    string recv_key, send_key;
    HELPER_ASSERT_SUCCESS(client.dtls_.get_srtp_key(recv_key, send_key));
    SrsSRTP srtp;
    HELPER_ASSERT_SUCCESS(srtp.initialize(recv_key, send_key));

    nb = sped_rtp_packet(buf);
    HELPER_ASSERT_SUCCESS(srtp.protect_rtp(buf, &nb));
    HELPER_ASSERT_SUCCESS(session.network_->on_rtp(buf, nb));
    EXPECT_TRUE(session.conn_.on_rtp_plaintext_called_);
    EXPECT_EQ(SrsSpedStateComplete, sped->state());
    EXPECT_TRUE(sped->flight_.empty());
    EXPECT_TRUE(sped->acks_.empty());

    // A later check gets the response it gets without SPED.
    vector<string> raw;
    SrsStunPacket response;
    HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, response));
    EXPECT_FALSE(response.has_dtls_in_stun_ack());
    EXPECT_FALSE(response.has_dtls_in_stun_data());
}

// The same over TCP, with SRTCP: a receiver report that decrypts completes SPED.
VOID TEST(RtcSpedLossTest, SrtcpWhilePendingCompletesOverTcp)
{
    srs_error_t err;

    SpedTcpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);

    SpedClient client;
    HELPER_ASSERT_SUCCESS(client.start(0));
    HELPER_ASSERT_SUCCESS(sped_run_to_pending(session, client));
    EXPECT_FALSE(sped->flight_.empty());

    string recv_key, send_key;
    HELPER_ASSERT_SUCCESS(client.dtls_.get_srtp_key(recv_key, send_key));
    SrsSRTP srtp;
    HELPER_ASSERT_SUCCESS(srtp.initialize(recv_key, send_key));

    char buf[1500];
    int nb = sped_rtcp_packet(buf);
    HELPER_ASSERT_SUCCESS(srtp.protect_rtcp(buf, &nb));
    HELPER_ASSERT_SUCCESS(session.network_->on_rtcp(buf, nb));
    EXPECT_TRUE(session.conn_.on_rtcp_called_);
    EXPECT_EQ(SrsSpedStateComplete, sped->state());
    EXPECT_TRUE(sped->flight_.empty());
}

// The response carrying the server flight is lost, and each check comes after the 50ms the
// DTLS timer started with before SPED. SPED resends the flight, so DTLS must not: the copy of
// the ClientHello makes SRS write nothing, and the client's next flight makes it write only
// its last flight, so what SPED embeds is the flight the client waits for, not a
// retransmission of the old one, which OpenSSL writes one datagram per message.
VOID TEST(RtcSpedLossTest, SlowChecksGetNoTimerRetransmission)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);

    SpedClient client;
    HELPER_ASSERT_SUCCESS(client.start(0));

    vector<string> raw;
    SrsStunPacket lost;
    HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, lost));
    ASSERT_EQ(1, (int)raw.size());
    vector<string> server_flight = raw;

    usleep(60 * 1000);
    SrsStunPacket response;
    HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, response));
    EXPECT_TRUE(raw.empty());
    EXPECT_TRUE(sped->flight_ == server_flight);
    ASSERT_TRUE(response.has_dtls_in_stun_data());
    EXPECT_TRUE(response.get_dtls_in_stun_data() == server_flight[0]);
    HELPER_ASSERT_SUCCESS(client.on_response(response));

    usleep(60 * 1000);
    HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, response));
    EXPECT_EQ(SrsSpedStatePending, sped->state());
    ASSERT_FALSE(raw.empty());
    EXPECT_TRUE(sped->flight_ == raw);
    for (int i = 0; i < (int)raw.size(); i++) {
        string &datagram = raw[i];
        bool server_hello = datagram.size() > 13 && (uint8_t)datagram[0] == 22 && (uint8_t)datagram[13] == 2;
        EXPECT_FALSE(server_hello) << "datagram " << i << " is a ServerHello again";
    }
    ASSERT_TRUE(response.has_dtls_in_stun_data());
    EXPECT_TRUE(response.get_dtls_in_stun_data() == raw[0]);

    HELPER_ASSERT_SUCCESS(client.on_response(response));
    EXPECT_TRUE(client.done());
    HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, response));
    EXPECT_EQ(SrsSpedStateComplete, sped->state());
    sped_expect_srtp_keys_match(client.dtls_, *session.dtls());
}

#if OPENSSL_VERSION_NUMBER >= 0x40100000L
// The messages one side's OpenSSL wrote, from its message callback: the record content type, and for a
// handshake message its type, so a test tells a DTLS 1.3 ACK record(26) from a NewSessionTicket(22, 4),
// though both are encrypted on the wire.
class SpedDtlsMessages
{
public:
    vector<int> content_types_;
    vector<int> handshake_types_;

public:
    static void callback(int write_p, int version, int content_type, const void *buf, size_t len, SSL *ssl, void *arg)
    {
        SpedDtlsMessages *messages = (SpedDtlsMessages *)arg;
        if (!write_p || content_type == SSL3_RT_HEADER || content_type == SSL3_RT_INNER_CONTENT_TYPE) {
            return;
        }
        messages->content_types_.push_back(content_type);
        messages->handshake_types_.push_back(content_type == SSL3_RT_HANDSHAKE && len > 0 ? ((const uint8_t *)buf)[0] : -1);
    }

    void attach(SrsDtls *dtls)
    {
        SSL_set_msg_callback(dtls->impl_->dtls_, callback);
        SSL_set_msg_callback_arg(dtls->impl_->dtls_, this);
    }

    void clear()
    {
        content_types_.clear();
        handshake_types_.clear();
    }

    bool wrote(int content_type, int handshake_type = -1)
    {
        for (int i = 0; i < (int)content_types_.size(); i++) {
            if (content_types_[i] == content_type && handshake_types_[i] == handshake_type) {
                return true;
            }
        }
        return false;
    }
};

// Whether a datagram starts with a DTLS 1.3 record in the unified header, 001CSLEE, which every
// encrypted record has: everything SRS sends after its ServerHello.
static bool sped_dtls13_encrypted(const string &datagram)
{
    return !datagram.empty() && ((uint8_t)datagram[0] & 0xe0) == 0x20;
}

static bool sped_dtls13_server_hello(const string &datagram)
{
    return datagram.size() > 13 && (uint8_t)datagram[0] == 22 && (uint8_t)datagram[13] == 2;
}

static bool sped_sent(const vector<string> &sent, const string &datagram)
{
    return std::find(sent.begin(), sent.end(), datagram) != sent.end();
}

// Sleep in ST, so SRS's DTLS timer coroutine runs, until the real time passed: ST's clock misses the
// time a test spends in usleep, so one ST sleep may end early.
static void sped_st_sleep(srs_utime_t duration)
{
    srs_utime_t start = srs_time_now_realtime();
    while (srs_time_now_realtime() - start < duration) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
}

// SPED on DTLS 1.3, with every raw datagram lost. The flights differ from DTLS 1.2: the client's
// ClientHello carries a post-quantum key share, so it takes two datagrams at SPED's MTU; SRS answers
// with ServerHello to Finished in one flight of several datagrams, all but the ServerHello encrypted;
// the client's Certificate, CertificateVerify and Finished end SRS's handshake, and SRS's last flight
// is the DTLS 1.3 ACK of them, which the client must get before it is done. SRS keeps that ACK to
// embed until the client ACKs it in STUN, as it keeps CCS and Finished on DTLS 1.2.
VOID TEST(RtcSpedDtls13Test, HandshakeCompletesInsideStunWhenRawIsLost)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true, "dtls1.3"));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);
    SpedDtlsMessages written;
    written.attach(session.dtls());

    SpedClient client;
    HELPER_ASSERT_SUCCESS(client.start(0, "dtls1.3"));
    vector<string> client_hello = client.cb_.packets_;
    EXPECT_EQ(2, (int)client_hello.size());
    EXPECT_TRUE(client.sped()->flight_ == client_hello);

    vector<string> server_sent;
    vector<string> server_flight;
    vector<string> server_last_flight;
    int rounds = 0;
    for (; rounds < 32 && sped->state() != SrsSpedStateComplete; rounds++) {
        bool server_was_done = session.dtls()->impl_->handshake_done_for_us_;
        written.clear();

        vector<string> raw;
        SrsStunPacket response;
        HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, response));
        server_sent.insert(server_sent.end(), raw.begin(), raw.end());

        bool server_done = session.dtls()->impl_->handshake_done_for_us_;
        if (!raw.empty() && server_flight.empty()) {
            server_flight = raw;
        }
        if (server_done && !server_was_done) {
            server_last_flight = raw;
            EXPECT_TRUE(written.wrote(SSL3_RT_ACK));
            EXPECT_FALSE(written.wrote(SSL3_RT_HANDSHAKE, SSL3_MT_NEWSESSION_TICKET));
            EXPECT_EQ(SrsSpedStatePending, sped->state());
            EXPECT_TRUE(sped->flight_ == raw);
            EXPECT_FALSE(client.done());
        }

        // SRS embeds only datagrams it wrote, and nothing once complete.
        if (sped->state() == SrsSpedStateComplete) {
            EXPECT_TRUE(server_was_done);
            EXPECT_FALSE(response.has_dtls_in_stun_ack());
            EXPECT_FALSE(response.has_dtls_in_stun_data());
        } else {
            EXPECT_EQ(server_done ? SrsSpedStatePending : SrsSpedStateConfirmed, sped->state());
            EXPECT_TRUE(response.has_dtls_in_stun_ack());
            EXPECT_LE((int)response.get_dtls_in_stun_acks().size(), kStunDtlsInStunMaxAcks);
        }
        if (response.has_dtls_in_stun_data()) {
            EXPECT_TRUE(sped_sent(server_sent, response.get_dtls_in_stun_data()));
        }

        HELPER_ASSERT_SUCCESS(client.on_response(response));
    }

    EXPECT_LT(rounds, 32);
    EXPECT_EQ(SrsSpedStateComplete, sped->state());
    EXPECT_EQ(SrsRtcNetworkStateEstablished, session.network_->state_);
    EXPECT_TRUE(client.done());
    EXPECT_EQ(DTLS1_3_VERSION, SSL_version(session.dtls()->impl_->dtls_));
    EXPECT_EQ(DTLS1_3_VERSION, SSL_version(client.dtls_.impl_->dtls_));

    // The first flight is the ServerHello, in the clear, then the encrypted rest, in more datagrams than one.
    ASSERT_GE((int)server_flight.size(), 2);
    EXPECT_TRUE(sped_dtls13_server_hello(server_flight[0]));
    EXPECT_TRUE(sped_dtls13_encrypted(server_flight.back()));
    ASSERT_EQ(1, (int)server_last_flight.size());
    EXPECT_TRUE(sped_dtls13_encrypted(server_last_flight[0]));
    sped_expect_srtp_keys_match(client.dtls_, *session.dtls());
}

// The same over TCP, each datagram in its own length-prefixed frame.
VOID TEST(RtcSpedDtls13Test, TcpHandshakeCompletesInsideStunWhenRawIsLost)
{
    srs_error_t err;

    SpedTcpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true, "dtls1.3"));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);

    SpedClient client;
    HELPER_ASSERT_SUCCESS(client.start(0, "dtls1.3"));

    vector<string> server_sent;
    int rounds = 0;
    for (; rounds < 32 && sped->state() != SrsSpedStateComplete; rounds++) {
        vector<string> raw;
        SrsStunPacket response;
        HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, response));
        server_sent.insert(server_sent.end(), raw.begin(), raw.end());
        if (response.has_dtls_in_stun_data()) {
            EXPECT_TRUE(sped_sent(server_sent, response.get_dtls_in_stun_data()));
        }
        HELPER_ASSERT_SUCCESS(client.on_response(response));
    }

    EXPECT_LT(rounds, 32);
    EXPECT_EQ(SrsSpedStateComplete, sped->state());
    EXPECT_EQ(SrsRtcNetworkStateEstablished, session.network_->state_);
    EXPECT_TRUE(client.done());
    EXPECT_EQ(DTLS1_3_VERSION, SSL_version(session.dtls()->impl_->dtls_));
    sped_expect_srtp_keys_match(client.dtls_, *session.dtls());
}

// SRS is the DTLS client for an offer with a=setup:passive, and the peer the DTLS server, which embeds
// its flights in its checks. On DTLS 1.3 SRS's handshake is done only once the peer's ACK of SRS's
// Finished arrives, so SRS has nothing left to resend and drops its last flight, then waits in PENDING
// until the peer's checks stop carrying SPED. Every raw datagram is lost.
VOID TEST(RtcSpedDtls13Test, SrsAsClientCompletesInsideStunWhenRawIsLost)
{
    srs_error_t err;

    MockDtlsCallbackForHandshake scb, pcb;
    SrsDtls srs(&scb);
    SrsDtls peer(&pcb);
    HELPER_ASSERT_SUCCESS(srs.initialize("active", "dtls1.3"));
    HELPER_ASSERT_SUCCESS(peer.initialize("passive", "dtls1.3"));
    srs.enable_sped();
    peer.enable_sped();
    SrsSpedController *srs_sped = srs.sped();
    SrsSpedController *peer_sped = peer.sped();

    HELPER_ASSERT_SUCCESS(peer.start_active_handshake());
    HELPER_ASSERT_SUCCESS(srs.start_active_handshake());
    EXPECT_TRUE(srs_sped->flight_ == scb.packets_);

    int rounds = 0;
    for (; rounds < 32 && srs_sped->state() != SrsSpedStateComplete; rounds++) {
        SpedStunMessage request = sped_build(peer_sped, BindingRequest);
        HELPER_ASSERT_SUCCESS(sped_deliver(srs, request));

        // SRS's handshake is done only on the peer's ACK, and then SRS keeps nothing to embed.
        bool srs_done = scb.handshake_done_count_ > 0;
        if (srs_sped->state() != SrsSpedStateComplete) {
            EXPECT_EQ(srs_done ? SrsSpedStatePending : SrsSpedStateConfirmed, srs_sped->state());
            if (srs_done) {
                EXPECT_TRUE(srs_sped->flight_.empty());
                EXPECT_TRUE(peer.impl_->handshake_done_for_us_);
            }
        }

        SpedStunMessage response = sped_build(srs_sped, BindingResponse);
        HELPER_ASSERT_SUCCESS(sped_deliver(peer, response));
        scb.packets_.clear();
        pcb.packets_.clear();
    }

    EXPECT_LT(rounds, 32);
    EXPECT_EQ(SrsSpedStateComplete, srs_sped->state());
    EXPECT_EQ(SrsSpedStateComplete, peer_sped->state());
    EXPECT_EQ(1, scb.handshake_done_count_);
    EXPECT_EQ(1, pcb.handshake_done_count_);
    EXPECT_EQ(DTLS1_3_VERSION, SSL_version(srs.impl_->dtls_));
    sped_expect_srtp_keys_match(srs, peer);
}

// The response carrying the first datagram of the server flight is lost, and the client's next check
// comes after the 50ms the DTLS timer starts with. The client has no ACK of the last ClientHello
// datagram, so it embeds the same bytes again. SRS writes nothing new for the copy, and its timer
// resends the flight raw only, so the flight SPED embeds stays the one the client is reading, and the
// next response embeds its next datagram, round robin.
VOID TEST(RtcSpedDtls13Test, LostResponseGetsTheSameServerFlightAgain)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true, "dtls1.3"));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);

    SpedClient client;
    HELPER_ASSERT_SUCCESS(client.start(0, "dtls1.3"));
    vector<string> client_hello = client.cb_.packets_;
    ASSERT_EQ(2, (int)client_hello.size());

    // The first datagram of the ClientHello makes no flight, so its response has only the ACK.
    vector<string> raw;
    SrsStunPacket response;
    HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, response));
    EXPECT_TRUE(raw.empty());
    EXPECT_FALSE(response.has_dtls_in_stun_data());
    HELPER_ASSERT_SUCCESS(client.on_response(response));
    EXPECT_EQ(1, (int)client.sped()->flight_.size());

    SpedStunMessage first = client.request();
    EXPECT_TRUE(first.data_ == client_hello[1]);
    SrsStunPacket lost;
    HELPER_ASSERT_SUCCESS(session.check(first, raw, lost));
    ASSERT_GE((int)raw.size(), 2);
    vector<string> server_flight = raw;
    EXPECT_TRUE(sped->flight_ == server_flight);
    ASSERT_TRUE(lost.has_dtls_in_stun_data());
    EXPECT_TRUE(lost.get_dtls_in_stun_data() == server_flight[0]);

    usleep(60 * 1000);
    SpedStunMessage again = client.request();
    EXPECT_TRUE(again.data_ == client_hello[1]);
    HELPER_ASSERT_SUCCESS(session.check(again, raw, response));
    ASSERT_FALSE(raw.empty());
    EXPECT_TRUE(sped_dtls13_server_hello(raw[0]));
    EXPECT_FALSE(sped_sent(server_flight, raw[0]));
    EXPECT_EQ(SrsSpedStateConfirmed, sped->state());
    EXPECT_TRUE(sped->flight_ == server_flight);
    EXPECT_TRUE(sped_has(response.get_dtls_in_stun_acks(), sped_crc(client_hello[1])));
    ASSERT_TRUE(response.has_dtls_in_stun_data());
    EXPECT_TRUE(response.get_dtls_in_stun_data() == server_flight[1]);
    HELPER_ASSERT_SUCCESS(client.on_response(response));

    HELPER_ASSERT_SUCCESS(sped_run_to_pending(session, client));
    HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, response));
    EXPECT_EQ(SrsSpedStateComplete, sped->state());
    EXPECT_EQ(SrsRtcNetworkStateEstablished, session.network_->state_);
    sped_expect_srtp_keys_match(client.dtls_, *session.dtls());
}

// The raw datagrams of SRS's flight after the first reach the client before it, and its DTLS drops what
// it cannot read yet, as BoringSSL drops encrypted records that come before the ServerHello; its SPED
// ACKs every datagram all the same, as Chrome's ACKs raw DTLS. So SRS's SPED has nothing left to resend
// while the client's DTLS misses most of the flight, and the client waits for SRS's timer. SRS resends
// on its DTLS timer, SPED embeds that copy, and the handshake completes inside STUN.
VOID TEST(RtcSpedDtls13Test, FlightAckedInStunButDroppedByDtlsIsResentOnTheTimer)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true, "dtls1.3"));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);

    SpedClient client;
    HELPER_ASSERT_SUCCESS(client.start(0, "dtls1.3"));

    vector<string> raw;
    SrsStunPacket response;
    HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, response));
    HELPER_ASSERT_SUCCESS(client.on_response(response));

    HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, response));
    ASSERT_GE((int)raw.size(), 2);
    vector<string> server_flight = raw;
    ASSERT_TRUE(response.has_dtls_in_stun_data());
    ASSERT_TRUE(response.get_dtls_in_stun_data() == server_flight[0]);
    for (int i = 1; i < (int)server_flight.size(); i++) {
        string &datagram = server_flight[i];
        client.sped()->on_dtls_received(datagram.data(), (int)datagram.size());
    }
    HELPER_ASSERT_SUCCESS(client.on_response(response));
    EXPECT_EQ(2, (int)client.cb_.packets_.size());

    // Every datagram is ACKed in STUN, so SPED has nothing to resend, and the client nothing to send.
    HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, response));
    EXPECT_TRUE(sped->flight_.empty());
    EXPECT_FALSE(response.has_dtls_in_stun_data());
    EXPECT_EQ(SrsSpedStateConfirmed, sped->state());
    HELPER_ASSERT_SUCCESS(client.on_response(response));
    EXPECT_FALSE(client.request().has_data_);

    // The timer starts at 50ms and doubles, so it fires at least once in this wait.
    sped_st_sleep(200 * SRS_UTIME_MILLISECONDS);
    EXPECT_FALSE(sped->flight_.empty());

    HELPER_ASSERT_SUCCESS(sped_run_to_pending(session, client));
    HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, response));
    EXPECT_EQ(SrsSpedStateComplete, sped->state());
    EXPECT_EQ(SrsRtcNetworkStateEstablished, session.network_->state_);
    sped_expect_srtp_keys_match(client.dtls_, *session.dtls());
}

// While SPED is on, a DTLS 1.3 server keeps its DTLS timer, which starts at 50ms, as its client may
// wait for it after an ACK in STUN or in DTLS; SRS as the DTLS client still waits its longest, as on
// DTLS 1.2.
VOID TEST(RtcSpedDtls13Test, ServerTimerRunsWhileSpedIsOn)
{
    srs_error_t err;

    MockDtlsCallbackForHandshake ccb, scb;
    SrsDtls client(&ccb);
    SrsDtls server(&scb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.3"));
    HELPER_ASSERT_SUCCESS(server.initialize("passive", "dtls1.3"));
    client.enable_sped();
    server.enable_sped();
    HELPER_ASSERT_SUCCESS(server.start_active_handshake());
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());
    EXPECT_GT(sped_dtls_timeout(&client), 10 * SRS_UTIME_SECONDS);

    for (int i = 0; i < (int)ccb.packets_.size(); i++) {
        SpedStunMessage request = sped_request_with(ccb.packets_[i]);
        HELPER_ASSERT_SUCCESS(sped_deliver(server, request));
    }
    EXPECT_EQ(SrsSpedStateConfirmed, server.sped()->state());
    ASSERT_FALSE(scb.packets_.empty());
    srs_utime_t timeout = sped_dtls_timeout(&server);
    EXPECT_GT(timeout, 0);
    EXPECT_LE(timeout, 50 * SRS_UTIME_MILLISECONDS);
}

// Run the checks of a client whose raw DTLS is lost until it has read the whole server flight and
// written its last flight, Certificate, CertificateVerify and Finished, which it returns.
static srs_error_t sped_run_to_client_flight(SpedUdpSession &session, SpedClient &client, vector<string> &client_flight)
{
    srs_error_t err = srs_success;

    for (int i = 0; i < 32; i++) {
        vector<string> raw;
        SrsStunPacket response;
        if ((err = session.check(client.request(), raw, response)) != srs_success) {
            return srs_error_wrap(err, "check %d", i);
        }
        client.cb_.packets_.clear();
        if ((err = client.on_response(response)) != srs_success) {
            return srs_error_wrap(err, "response %d", i);
        }
        if (!client.cb_.packets_.empty()) {
            client_flight = client.cb_.packets_;
            return err;
        }
    }

    return srs_error_new(ERROR_RTC_DTLS, "no client flight");
}

// The check that embeds the client's last flight is lost, and so is its raw copy. SRS still waits
// in the handshake, and the client, with no ACK of that flight, embeds it again until SRS has every
// datagram of it, which ends SRS's handshake; that response ACKs the flight and embeds SRS's ACK.
VOID TEST(RtcSpedDtls13Test, LostClientFlightIsEmbeddedAgain)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true, "dtls1.3"));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);

    SpedClient client;
    HELPER_ASSERT_SUCCESS(client.start(0, "dtls1.3"));
    vector<string> client_flight;
    HELPER_ASSERT_SUCCESS(sped_run_to_client_flight(session, client, client_flight));
    EXPECT_TRUE(client.sped()->flight_ == client_flight);
    for (int i = 0; i < (int)client_flight.size(); i++) {
        EXPECT_TRUE(sped_dtls13_encrypted(client_flight[i])) << i;
    }

    SpedStunMessage lost = client.request();
    EXPECT_TRUE(lost.data_ == client_flight[0]);
    EXPECT_EQ(SrsSpedStateConfirmed, sped->state());
    EXPECT_EQ(SrsRtcNetworkStateDtls, session.network_->state_);

    vector<string> raw;
    SrsStunPacket response;
    int rounds = 0;
    for (; rounds < 8 && sped->state() == SrsSpedStateConfirmed; rounds++) {
        SpedStunMessage again = client.request();
        EXPECT_TRUE(sped_sent(client_flight, again.data_));
        HELPER_ASSERT_SUCCESS(session.check(again, raw, response));
        if (sped->state() == SrsSpedStateConfirmed) {
            HELPER_ASSERT_SUCCESS(client.on_response(response));
        }
    }

    EXPECT_LT(rounds, 8);
    EXPECT_EQ(SrsRtcNetworkStateEstablished, session.network_->state_);
    EXPECT_EQ(SrsSpedStatePending, sped->state());
    ASSERT_FALSE(raw.empty());
    EXPECT_TRUE(sped->flight_ == raw);
    for (int i = 0; i < (int)client_flight.size(); i++) {
        EXPECT_TRUE(sped_has(response.get_dtls_in_stun_acks(), sped_crc(client_flight[i]))) << i;
    }
    ASSERT_TRUE(response.has_dtls_in_stun_data());
    EXPECT_TRUE(response.get_dtls_in_stun_data() == raw[0]);

    EXPECT_FALSE(client.done());
    HELPER_ASSERT_SUCCESS(client.on_response(response));
    EXPECT_TRUE(client.done());
    HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, response));
    EXPECT_EQ(SrsSpedStateComplete, sped->state());
    sped_expect_srtp_keys_match(client.dtls_, *session.dtls());
}

// The checks that embed the client's last flight are lost, but its raw copy arrives. SRS ends its
// handshake from the raw copy, ACKs it in STUN, and keeps its DTLS 1.3 ACK to embed. A later check
// still embeds the same bytes, whose record numbers OpenSSL has seen, so it drops them and writes
// nothing, and SPED embeds the ACK SRS kept, until the client ACKs it.
VOID TEST(RtcSpedDtls13Test, LostEmbeddedClientFlightIsAckedFromItsRawCopy)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true, "dtls1.3"));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);
    SpedDtlsMessages written;
    written.attach(session.dtls());

    SpedClient client;
    HELPER_ASSERT_SUCCESS(client.start(0, "dtls1.3"));
    vector<string> client_flight;
    HELPER_ASSERT_SUCCESS(sped_run_to_client_flight(session, client, client_flight));

    session.socket_.sent_.clear();
    written.clear();
    HELPER_ASSERT_SUCCESS(sped_deliver_raw(*session.dtls(), client_flight));
    EXPECT_EQ(SrsRtcNetworkStateEstablished, session.network_->state_);
    EXPECT_EQ(SrsSpedStatePending, sped->state());
    for (int i = 0; i < (int)client_flight.size(); i++) {
        EXPECT_TRUE(sped_has(sped->acks_, sped_crc(client_flight[i]))) << i;
    }
    EXPECT_TRUE(written.wrote(SSL3_RT_ACK));
    ASSERT_FALSE(session.socket_.sent_.empty());
    EXPECT_TRUE(sped->flight_ == session.socket_.sent_);

    vector<string> server_ack = sped->flight_;

    SpedStunMessage again = client.request();
    EXPECT_TRUE(sped_sent(client_flight, again.data_));
    written.clear();
    vector<string> raw;
    SrsStunPacket response;
    HELPER_ASSERT_SUCCESS(session.check(again, raw, response));
    EXPECT_EQ(SrsSpedStatePending, sped->state());
    EXPECT_TRUE(written.content_types_.empty());
    EXPECT_TRUE(raw.empty());
    EXPECT_TRUE(sped->flight_ == server_ack);
    ASSERT_TRUE(response.has_dtls_in_stun_data());
    EXPECT_TRUE(response.get_dtls_in_stun_data() == server_ack[0]);

    HELPER_ASSERT_SUCCESS(client.on_response(response));
    EXPECT_TRUE(client.done());
    EXPECT_TRUE(client.sped()->flight_.empty());
    HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, response));
    EXPECT_EQ(SrsSpedStateComplete, sped->state());
    EXPECT_EQ(1, client.cb_.handshake_done_count_);
    sped_expect_srtp_keys_match(client.dtls_, *session.dtls());
}

// At OpenSSL's smallest MTU, every DTLS 1.3 flight takes many datagrams, the post-quantum ClientHello
// most of all. SPED embeds them one per message, round robin, with at most 4 ACKs per message, and
// with every raw datagram lost, the handshake still completes inside STUN.
VOID TEST(RtcSpedDtls13Test, FragmentedFlightsCompleteInsideStun)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true, "dtls1.3"));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);
    HELPER_ASSERT_SUCCESS(sped_set_mtu(session.dtls(), 256));

    SpedClient client;
    HELPER_ASSERT_SUCCESS(client.start(256, "dtls1.3"));
    vector<string> client_hello = client.cb_.packets_;
    EXPECT_GE((int)client_hello.size(), 5);

    vector<string> server_sent;
    int server_flight_size = 0;
    int rounds = 0;
    for (; rounds < 128 && sped->state() != SrsSpedStateComplete; rounds++) {
        vector<string> raw;
        SrsStunPacket response;
        HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, response));
        server_sent.insert(server_sent.end(), raw.begin(), raw.end());
        if (server_flight_size == 0 && !raw.empty()) {
            server_flight_size = (int)raw.size();
        }

        EXPECT_LE((int)response.get_dtls_in_stun_acks().size(), kStunDtlsInStunMaxAcks);
        if (response.has_dtls_in_stun_data()) {
            EXPECT_TRUE(sped_sent(server_sent, response.get_dtls_in_stun_data()));
        }
        HELPER_ASSERT_SUCCESS(client.on_response(response));
    }

    EXPECT_LT(rounds, 128);
    EXPECT_GE(server_flight_size, 5);
    EXPECT_EQ(SrsSpedStateComplete, sped->state());
    EXPECT_EQ(SrsRtcNetworkStateEstablished, session.network_->state_);
    EXPECT_EQ(1, client.cb_.handshake_done_count_);
    sped_expect_srtp_keys_match(client.dtls_, *session.dtls());
}

// A client checks two candidate pairs of one session, and every response on the first pair is lost.
// The DTLS 1.3 flights SRS embeds belong to the one association, so the second pair carries them all.
VOID TEST(RtcSpedDtls13Test, TwoPairsShareOneSession)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true, "dtls1.3"));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);

    MockUdpMuxSocketForSped &pair_a = session.socket_;
    MockUdpMuxSocketForSped pair_b;
    pair_b.peer_ip_ = pair_a.peer_ip_;
    pair_b.peer_port_ = pair_a.peer_port_ + 1;
    pair_b.peer_id_ = "192.168.1.100:5001";
    ASSERT_NE(pair_a.peer_id_, pair_b.peer_id_);

    SpedClient client;
    HELPER_ASSERT_SUCCESS(client.start(0, "dtls1.3"));

    int rounds = 0;
    for (; rounds < 64 && sped->state() != SrsSpedStateComplete; rounds++) {
        vector<string> raw;
        SrsStunPacket response;
        bool on_a = rounds % 2 == 0;
        HELPER_ASSERT_SUCCESS(session.check_on(on_a ? &pair_a : &pair_b, client.request(), raw, response));
        EXPECT_TRUE(session.sped() == sped);
        if (!on_a) {
            HELPER_ASSERT_SUCCESS(client.on_response(response));
        }
    }

    EXPECT_LT(rounds, 64);
    EXPECT_EQ(2, (int)session.network_->peer_addresses_.size());
    EXPECT_EQ(SrsSpedStateComplete, sped->state());
    EXPECT_TRUE(client.done());
    sped_expect_srtp_keys_match(client.dtls_, *session.dtls());
}

// SRS is done and waits for the ACK of its DTLS 1.3 ACK, the client is done as it has read it, but
// the client's next check is lost and SRTP comes first. The first packet that decrypts completes SPED.
VOID TEST(RtcSpedDtls13Test, SrtpWhilePendingCompletes)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true, "dtls1.3"));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);

    SpedClient client;
    HELPER_ASSERT_SUCCESS(client.start(0, "dtls1.3"));
    HELPER_ASSERT_SUCCESS(sped_run_to_pending(session, client));
    EXPECT_EQ(SrsRtcNetworkStateEstablished, session.network_->state_);
    EXPECT_FALSE(sped->flight_.empty());

    string recv_key, send_key;
    HELPER_ASSERT_SUCCESS(client.dtls_.get_srtp_key(recv_key, send_key));
    SrsSRTP srtp;
    HELPER_ASSERT_SUCCESS(srtp.initialize(recv_key, send_key));

    char buf[1500];
    int nb = sped_rtp_packet(buf);
    HELPER_ASSERT_SUCCESS(srtp.protect_rtp(buf, &nb));
    HELPER_ASSERT_SUCCESS(session.network_->on_rtp(buf, nb));
    EXPECT_TRUE(session.conn_.on_rtp_plaintext_called_);
    EXPECT_EQ(SrsSpedStateComplete, sped->state());
    EXPECT_TRUE(sped->flight_.empty());
    EXPECT_TRUE(sped->acks_.empty());
}
#endif

// The SPED stats as the client API dumps them, with -2 for a field that is missing.
class SpedStats
{
public:
    string state_;
    int64_t embedded_sent_;
    int64_t embedded_recv_;
    int64_t raw_recv_;
    int64_t handshake_ms_;
};

static int64_t sped_json_integer(SrsJsonObject *obj, const char *name)
{
    SrsJsonAny *v = obj->get_property(name);
    return (v && v->is_integer()) ? v->to_integer() : -2;
}

static SpedStats sped_stats_of(SrsJsonObject *obj)
{
    SpedStats stats;
    SrsJsonAny *state = obj->get_property("state");
    stats.state_ = (state && state->is_string()) ? state->to_str() : "";
    stats.embedded_sent_ = sped_json_integer(obj, "embedded_sent");
    stats.embedded_recv_ = sped_json_integer(obj, "embedded_recv");
    stats.raw_recv_ = sped_json_integer(obj, "raw_recv");
    stats.handshake_ms_ = sped_json_integer(obj, "handshake_ms");
    return stats;
}

static SpedStats sped_stats(SrsSpedController *sped)
{
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
    sped->dumps(obj.get());
    return sped_stats_of(obj.get());
}

// A DTLS 1.2 server handshake, as SRS sees it: the stats count each datagram embedded in a
// response, each one received embedded or raw, and the time from the first check to the
// handshake done. They stop when SPED is complete, and later messages leave them as they are.
VOID TEST(RtcSpedStatsTest, CountsUntilCompleteWithHandshakeCost)
{
    MockClockForPithyPrint clk;
    clk.now_ = 100 * SRS_UTIME_MILLISECONDS;
    SrsSpedController sped;
    sped.clk_ = &clk;
    EXPECT_EQ(-1, sped.handshake_cost());

    SpedStats stats = sped_stats(&sped);
    EXPECT_STREQ("tentative", stats.state_.c_str());
    EXPECT_EQ(0, stats.embedded_sent_);
    EXPECT_EQ(0, stats.embedded_recv_);
    EXPECT_EQ(0, stats.raw_recv_);
    EXPECT_EQ(-1, stats.handshake_ms_);

    // The first check embeds the ClientHello, and the response the server flight.
    string client_hello = sped_handshake("client-hello");
    EXPECT_TRUE(sped.on_stun_received(true, client_hello, true, vector<uint32_t>()));
    string server_flight = sped_handshake("server-flight");
    sped_capture(sped, server_flight);
    sped.flush();
    EXPECT_TRUE(sped_next(sped) == server_flight);
    sped.on_data_embedded();

    // The client's flight 3 comes raw, and the handshake is done 60 ms after the first check.
    clk.now_ = 150 * SRS_UTIME_MILLISECONDS;
    string client_flight = sped_handshake("client-flight-3");
    sped.on_dtls_received(client_flight.data(), (int)client_flight.size());
    EXPECT_EQ(-1, sped.handshake_cost());

    clk.now_ = 160 * SRS_UTIME_MILLISECONDS;
    string server_finished = sped_dtls_datagram(20, "server-finished");
    sped_capture(sped, server_finished);
    sped.flush();
    sped.on_handshake_done(false, false);
    EXPECT_EQ(60 * SRS_UTIME_MILLISECONDS, sped.handshake_cost());

    // The next check has no ACK of the last flight, so it is embedded again.
    clk.now_ = 200 * SRS_UTIME_MILLISECONDS;
    EXPECT_FALSE(sped_receive_empty_ack(sped));
    EXPECT_TRUE(sped_next(sped) == server_finished);
    sped.on_data_embedded();

    stats = sped_stats(&sped);
    EXPECT_STREQ("pending", stats.state_.c_str());
    EXPECT_EQ(2, stats.embedded_sent_);
    EXPECT_EQ(1, stats.embedded_recv_);
    EXPECT_EQ(1, stats.raw_recv_);
    EXPECT_EQ(60, stats.handshake_ms_);

    // The ACK of the last flight completes SPED.
    vector<uint32_t> acks(1, sped_crc(server_finished));
    EXPECT_FALSE(sped_receive_acks(sped, acks));
    EXPECT_EQ(SrsSpedStateComplete, sped.state());

    clk.now_ = 300 * SRS_UTIME_MILLISECONDS;
    sped.on_dtls_received(client_flight.data(), (int)client_flight.size());
    sped.on_data_embedded();
    EXPECT_FALSE(sped.on_stun_received(true, client_hello, true, acks));

    stats = sped_stats(&sped);
    EXPECT_STREQ("complete", stats.state_.c_str());
    EXPECT_EQ(2, stats.embedded_sent_);
    EXPECT_EQ(1, stats.embedded_recv_);
    EXPECT_EQ(1, stats.raw_recv_);
    EXPECT_EQ(60, stats.handshake_ms_);
    EXPECT_EQ(60 * SRS_UTIME_MILLISECONDS, sped.handshake_cost());
}

// A client that does not do SPED in STUN: SPED is off from the first check, and the raw
// handshake that follows is neither counted nor timed.
VOID TEST(RtcSpedStatsTest, FallbackToOffHasNoCounts)
{
    MockClockForPithyPrint clk;
    clk.now_ = 100 * SRS_UTIME_MILLISECONDS;
    SrsSpedController sped;
    sped.clk_ = &clk;

    EXPECT_FALSE(sped_receive_plain(sped));
    EXPECT_EQ(SrsSpedStateOff, sped.state());

    clk.now_ = 150 * SRS_UTIME_MILLISECONDS;
    string client_hello = sped_handshake("client-hello");
    sped.on_dtls_received(client_hello.data(), (int)client_hello.size());
    sped.on_handshake_done(false, false);
    EXPECT_EQ(-1, sped.handshake_cost());

    SpedStats stats = sped_stats(&sped);
    EXPECT_STREQ("off", stats.state_.c_str());
    EXPECT_EQ(0, stats.embedded_sent_);
    EXPECT_EQ(0, stats.embedded_recv_);
    EXPECT_EQ(0, stats.raw_recv_);
    EXPECT_EQ(-1, stats.handshake_ms_);
}

// A whole handshake over UDP with every raw datagram lost, one check each 10 ms: the stats
// count each DATA in a response and each DTLS DATA in a check, and the handshake cost is the
// time from the first check to the check whose DATA completed SRS's handshake.
VOID TEST(RtcSpedStatsTest, UdpHandshakeInsideStunIsCounted)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);
    MockClockForPithyPrint clk;
    sped->clk_ = &clk;

    SpedClient client;
    HELPER_ASSERT_SUCCESS(client.start(0));

    int responses_with_data = 0;
    int checks_with_data = 0;
    int handshake_round = -1;
    int rounds = 0;
    for (; rounds < 16 && sped->state() != SrsSpedStateComplete; rounds++) {
        clk.now_ = (1000 + rounds * 10) * SRS_UTIME_MILLISECONDS;

        SpedStunMessage request = client.request();
        if (request.has_data_) {
            checks_with_data++;
        }

        vector<string> raw;
        SrsStunPacket response;
        HELPER_ASSERT_SUCCESS(session.check(request, raw, response));
        if (response.has_dtls_in_stun_data()) {
            responses_with_data++;
        }
        if (handshake_round < 0 && session.network_->state_ == SrsRtcNetworkStateEstablished) {
            handshake_round = rounds;
        }

        HELPER_ASSERT_SUCCESS(client.on_response(response));
    }
    EXPECT_EQ(SrsSpedStateComplete, sped->state());
    ASSERT_GT(handshake_round, 0);
    EXPECT_GT(responses_with_data, 0);
    EXPECT_GT(checks_with_data, 0);

    SpedStats stats = sped_stats(sped);
    EXPECT_STREQ("complete", stats.state_.c_str());
    EXPECT_EQ(responses_with_data, stats.embedded_sent_);
    EXPECT_EQ(checks_with_data, stats.embedded_recv_);
    EXPECT_EQ(0, stats.raw_recv_);
    EXPECT_EQ(handshake_round * 10, stats.handshake_ms_);
}

// A datagram too large to embed went raw only, so it is not counted as embedded.
VOID TEST(RtcSpedStatsTest, DatagramTooLargeIsNotCounted)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);

    SpedStunMessage empty_ack;
    empty_ack.has_acks_ = true;
    vector<string> raw;
    SrsStunPacket response;
    HELPER_ASSERT_SUCCESS(session.check(empty_ack, raw, response));

    string big = sped_dtls_datagram(22, string(kStunSpedMaxBodySize, 'x'));
    sped->capture(big.data(), (int)big.size());
    sped->flush();
    HELPER_ASSERT_SUCCESS(session.check(empty_ack, raw, response));
    EXPECT_FALSE(response.has_dtls_in_stun_data());
    EXPECT_EQ(0, sped_stats(sped).embedded_sent_);

    string small = sped_handshake("small");
    sped->capture(small.data(), (int)small.size());
    sped->flush();
    HELPER_ASSERT_SUCCESS(session.check(empty_ack, raw, response));
    EXPECT_TRUE(response.has_dtls_in_stun_data());
    EXPECT_EQ(1, sped_stats(sped).embedded_sent_);
}

// A session as the client API sees it, through the connection it registered the client with.
class MockSpedSessionForStatistic : public ISrsExpire, public ISrsSpedSource
{
public:
    SrsSpedController *sped_;

public:
    MockSpedSessionForStatistic()
    {
        sped_ = NULL;
    }
    virtual ~MockSpedSessionForStatistic()
    {
    }

public:
    virtual void expire()
    {
    }
    virtual SrsSpedController *sped()
    {
        return sped_;
    }
};

// The client API dumps an RTC client's SPED stats in "sped", only when its session has SPED.
VOID TEST(RtcSpedStatsTest, ClientApiDumpsTheSessionSped)
{
    srs_error_t err;

    SrsStatisticVhost vhost;
    vhost.id_ = "vid-sped";
    SrsStatisticStream stream;
    stream.assemble();
    stream.id_ = "sid-sped";
    stream.vhost_ = &vhost;

    SrsStatisticClient client;
    client.assemble();
    client.id_ = "cid-sped";
    client.type_ = SrsRtcConnPlay;
    client.req_ = new MockRequest("__defaultVhost__", "live", "livestream");
    client.stream_ = &stream;

    // Without a connection, or with one that is not an RTC session.
    if (true) {
        SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
        HELPER_ASSERT_SUCCESS(client.dumps(obj.get()));
        EXPECT_TRUE(obj->get_property("sped") == NULL);
    }

    MockSpedSessionForStatistic session;
    client.conn_ = &session;
    if (true) {
        SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
        HELPER_ASSERT_SUCCESS(client.dumps(obj.get()));
        EXPECT_TRUE(obj->get_property("sped") == NULL);
    }

    SrsSpedController sped;
    session.sped_ = &sped;
    EXPECT_FALSE(sped_receive_empty_ack(sped));
    if (true) {
        SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
        HELPER_ASSERT_SUCCESS(client.dumps(obj.get()));
        SrsJsonAny *prop = obj->get_property("sped");
        ASSERT_TRUE(prop != NULL);
        ASSERT_TRUE(prop->is_object());

        SpedStats stats = sped_stats_of(prop->to_object());
        EXPECT_STREQ("confirmed", stats.state_.c_str());
        EXPECT_EQ(0, stats.embedded_sent_);
        EXPECT_EQ(-1, stats.handshake_ms_);
    }

    client.conn_ = NULL;
}

// The session reports the SPED of the network its client uses: UDP until the client shows
// SPED over TCP. Without SPED, it reports none.
VOID TEST(RtcSpedStatsTest, SessionReportsTheNetworkTheClientUses)
{
    srs_error_t err;

    SrsSessionConfig cfg;
    cfg.dtls_role_ = "passive";
    cfg.dtls_version_ = "dtls1.2";
    cfg.remote_fingerprint_algo_ = "sha-256";
    cfg.remote_fingerprint_ = _srs_rtc_dtls_certificate->get_fingerprint();

    MockRtcAsyncTaskExecutor exec;
    SrsContextId cid;
    cid.set_value("sped-session");

    if (true) {
        SrsUniquePtr<SrsRtcConnection> conn(new SrsRtcConnection(&exec, cid));
        cfg.sped_ = false;
        HELPER_ASSERT_SUCCESS(conn->networks_->initialize(&cfg, true, true));
        EXPECT_TRUE(conn->sped() == NULL);
    }

    SrsUniquePtr<SrsRtcConnection> conn(new SrsRtcConnection(&exec, cid));
    cfg.sped_ = true;
    HELPER_ASSERT_SUCCESS(conn->networks_->initialize(&cfg, true, true));
    SrsSpedController *udp = conn->udp()->sped();
    SrsSpedController *tcp = conn->tcp()->sped();
    ASSERT_TRUE(udp != NULL);
    ASSERT_TRUE(tcp != NULL);
    EXPECT_TRUE(udp != tcp);
    EXPECT_TRUE(conn->sped() == udp);

    // The client checks over TCP, so its SPED runs there.
    EXPECT_FALSE(sped_receive_empty_ack(*tcp));
    EXPECT_TRUE(conn->sped() == tcp);

    // The session is what the client API reaches from the client's connection.
    ISrsExpire *expire = conn.get();
    ISrsSpedSource *source = dynamic_cast<ISrsSpedSource *>(expire);
    ASSERT_TRUE(source != NULL);
    EXPECT_TRUE(source->sped() == tcp);
}

// The bytes of a datagram whose first byte is the one RFC 9443 demultiplexes by.
static string sim_datagram(uint8_t first)
{
    string datagram(32, '\0');
    datagram[0] = (char)first;
    return datagram;
}

// The simulator loses the next STUN responses and the next DTLS datagrams it is armed for,
// each counted on its own, and lets every other packet, such as RTP, through.
VOID TEST(RtcHandshakeDropTest, LosesTheNextResponsesAndDtlsDatagrams)
{
    SrsRtcHandshakeDrop drop;
    string dtls = sim_datagram(22);
    string rtp = sim_datagram(0x80);
    string stun = sim_datagram(0x01);

    // Not armed, nothing is lost.
    EXPECT_FALSE(drop.drop_stun(100));
    EXPECT_FALSE(drop.drop_dtls(&dtls[0], dtls.size()));

    drop.arm(2, 1, 0);
    EXPECT_FALSE(drop.drop_dtls(&rtp[0], rtp.size()));
    EXPECT_FALSE(drop.drop_dtls(&stun[0], stun.size()));
    EXPECT_TRUE(drop.drop_dtls(&dtls[0], dtls.size()));
    EXPECT_FALSE(drop.drop_dtls(&dtls[0], dtls.size()));
    EXPECT_TRUE(drop.drop_stun(100));
    EXPECT_TRUE(drop.drop_stun(100));
    EXPECT_FALSE(drop.drop_stun(100));

    // No count, or a negative one, loses nothing.
    drop.arm(0, -1, -1);
    EXPECT_FALSE(drop.drop_stun(100));
    EXPECT_FALSE(drop.drop_dtls(&dtls[0], dtls.size()));
}

// A DTLS 1.2 record: the content type, the version, the epoch, a sequence number, and a body of the length.
static string sim_record(uint8_t content_type, uint16_t epoch, int length)
{
    string record(13 + length, '\0');
    record[0] = (char)content_type;
    record[1] = (char)0xfe;
    record[2] = (char)0xfd;
    record[3] = (char)(epoch >> 8);
    record[4] = (char)epoch;
    record[11] = (char)(length >> 8);
    record[12] = (char)length;
    return record;
}

// A DTLS 1.2 server's last flight is a ChangeCipherSpec and its Finished, the first record of the new epoch,
// with a NewSessionTicket before them when the client asked for one. The simulator loses the next datagrams that
// carry such a flight, so the client resends its own and SRS answers it again, and lets every other one through:
// the first flight, which is all epoch 0, an alert, media, STUN, and DTLS 1.3, whose encrypted records have no
// epoch field.
VOID TEST(RtcHandshakeDropTest, LosesTheNextLastFlights)
{
    SrsRtcHandshakeDrop drop;
    string first_flight = sim_record(22, 0, 100) + sim_record(22, 0, 12);
    string ccs_finished = sim_record(20, 0, 1) + sim_record(22, 1, 40);
    string ticket_ccs_finished = sim_record(22, 0, 200) + sim_record(20, 0, 1) + sim_record(22, 1, 40);
    string finished = sim_record(22, 1, 40);
    string alert = sim_record(21, 1, 26);
    string truncated = sim_record(22, 0, 100).substr(0, 50);
    string dtls13 = sim_datagram(0x2c);
    string rtp = sim_datagram(0x80);
    string stun = sim_datagram(0x01);

    // Not armed, nothing is lost.
    EXPECT_FALSE(drop.drop_dtls(&ccs_finished[0], ccs_finished.size()));

    drop.arm(0, 0, 3);
    EXPECT_FALSE(drop.drop_dtls(&first_flight[0], first_flight.size()));
    EXPECT_FALSE(drop.drop_dtls(&truncated[0], truncated.size()));
    EXPECT_FALSE(drop.drop_dtls(&dtls13[0], dtls13.size()));
    EXPECT_FALSE(drop.drop_dtls(&rtp[0], rtp.size()));
    EXPECT_FALSE(drop.drop_dtls(&stun[0], stun.size()));
    EXPECT_FALSE(drop.drop_dtls(&alert[0], alert.size()));
    EXPECT_TRUE(drop.drop_dtls(&ccs_finished[0], ccs_finished.size()));
    EXPECT_TRUE(drop.drop_dtls(&ticket_ccs_finished[0], ticket_ccs_finished.size()));
    EXPECT_TRUE(drop.drop_dtls(&finished[0], finished.size()));
    EXPECT_FALSE(drop.drop_dtls(&ccs_finished[0], ccs_finished.size()));

    // The DTLS count loses any DTLS datagram, and each count is used on its own.
    drop.arm(0, 1, 1);
    EXPECT_TRUE(drop.drop_dtls(&ccs_finished[0], ccs_finished.size()));
    EXPECT_TRUE(drop.drop_dtls(&first_flight[0], first_flight.size()));
    EXPECT_FALSE(drop.drop_dtls(&first_flight[0], first_flight.size()));
    EXPECT_FALSE(drop.drop_dtls(&ccs_finished[0], ccs_finished.size()));

    // A negative count loses nothing.
    drop.arm(0, 0, -1);
    EXPECT_FALSE(drop.drop_dtls(&ccs_finished[0], ccs_finished.size()));
}

// A session's UDP and TCP networks share its drops, which the session arms.
VOID TEST(RtcHandshakeDropTest, SessionArmsTheDropsOfBothNetworks)
{
    MockRtcAsyncTaskExecutor exec;
    SrsContextId cid;
    cid.set_value("sim-session");
    SrsUniquePtr<SrsRtcConnection> conn(new SrsRtcConnection(&exec, cid));

    SrsRtcNetworks *networks = dynamic_cast<SrsRtcNetworks *>(conn->networks_);
    ASSERT_TRUE(networks != NULL);
    ASSERT_TRUE(networks->drop_ != NULL);
    EXPECT_TRUE(dynamic_cast<SrsRtcUdpNetwork *>(networks->udp_)->drop_ == networks->drop_);
    EXPECT_TRUE(dynamic_cast<SrsRtcTcpNetwork *>(networks->tcp_)->drop_ == networks->drop_);

    string dtls = sim_datagram(22);
    conn->simulate_handshake_drop(1, 1, 0);
    EXPECT_TRUE(networks->drop_->drop_stun(100));
    EXPECT_FALSE(networks->drop_->drop_stun(100));
    EXPECT_TRUE(networks->drop_->drop_dtls(&dtls[0], dtls.size()));
    EXPECT_FALSE(networks->drop_->drop_dtls(&dtls[0], dtls.size()));

    string finished = sim_record(20, 0, 1) + sim_record(22, 1, 40);
    conn->simulate_handshake_drop(0, 0, 1);
    EXPECT_TRUE(networks->drop_->drop_dtls(&finished[0], finished.size()));
    EXPECT_FALSE(networks->drop_->drop_dtls(&finished[0], finished.size()));
}

// Without SPED, the simulator loses the first response and the first DTLS datagram SRS sends
// over UDP. The check still starts DTLS, and media is never lost.
VOID TEST(RtcHandshakeDropTest, UdpLosesResponseAndDtlsButNotMedia)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(false));
    SrsRtcHandshakeDrop drop;
    session.network_->set_handshake_drop(&drop);
    drop.arm(1, 1, 0);

    vector<string> raw;
    SrsStunPacket response;
    HELPER_EXPECT_FAILED(session.check(SpedStunMessage(), raw, response));
    EXPECT_TRUE(session.socket_.sent_.empty());
    EXPECT_EQ(SrsRtcNetworkStateDtls, session.network_->state_);

    // The next check gets its response.
    HELPER_ASSERT_SUCCESS(session.check(SpedStunMessage(), raw, response));
    EXPECT_TRUE(raw.empty());

    string rtp = sim_datagram(0x80);
    string dtls = sim_datagram(22);
    session.socket_.sent_.clear();
    HELPER_ASSERT_SUCCESS(session.network_->write(&rtp[0], rtp.size(), NULL));
    HELPER_ASSERT_SUCCESS(session.network_->write(&dtls[0], dtls.size(), NULL));
    HELPER_ASSERT_SUCCESS(session.network_->write(&dtls[0], dtls.size(), NULL));
    ASSERT_EQ(2, (int)session.socket_.sent_.size());
    EXPECT_TRUE(session.socket_.sent_[0] == rtp);
    EXPECT_TRUE(session.socket_.sent_[1] == dtls);
}

// With SPED, the simulator loses the first response and both raw flights of SRS, as a lossy
// network may. The client gets no answer to its first check, so it embeds its ClientHello again,
// and SRS embeds the same flight again: the handshake completes inside STUN, and a check of
// each round gets its response.
VOID TEST(RtcHandshakeDropTest, UdpSpedRecoversInsideStun)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);
    SrsRtcHandshakeDrop drop;
    session.network_->set_handshake_drop(&drop);
    drop.arm(1, 2, 0);

    MockDtlsCallbackForHandshake ccb;
    SrsDtls client(&ccb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
    client.enable_sped();
    SrsSpedController *client_sped = client.sped();
    ASSERT_TRUE(client_sped != NULL);
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());

    // The response and the raw server flight are lost, so the client gets nothing.
    vector<string> raw;
    SrsStunPacket response;
    HELPER_EXPECT_FAILED(session.check(sped_build(client_sped, BindingRequest), raw, response));
    EXPECT_TRUE(session.socket_.sent_.empty());
    EXPECT_EQ(SrsSpedStateConfirmed, sped->state());
    ccb.packets_.clear();

    int rounds = 0;
    for (; rounds < 16 && sped->state() != SrsSpedStateComplete; rounds++) {
        HELPER_ASSERT_SUCCESS(session.check(sped_build(client_sped, BindingRequest), raw, response));
        EXPECT_TRUE(raw.empty()) << rounds;
        SpedStunMessage msg = sped_attributes_of(response);
        HELPER_ASSERT_SUCCESS(sped_deliver(client, msg));
        ccb.packets_.clear();
    }

    EXPECT_LT(rounds, 16);
    EXPECT_EQ(SrsSpedStateComplete, sped->state());
    EXPECT_EQ(SrsRtcNetworkStateEstablished, session.network_->state_);
    EXPECT_EQ(1, ccb.handshake_done_count_);
    EXPECT_EQ(SrsSpedStateComplete, client_sped->state());
    EXPECT_FALSE(drop.drop_stun(100));
}

// Over TCP the simulator loses the same, without breaking the framing: the frames SRS still
// sends each have their length, and the handshake completes inside STUN.
VOID TEST(RtcHandshakeDropTest, TcpSpedRecoversInsideStun)
{
    srs_error_t err;

    SpedTcpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);
    SrsRtcHandshakeDrop drop;
    session.network_->set_handshake_drop(&drop);
    drop.arm(1, 2, 0);

    MockDtlsCallbackForHandshake ccb;
    SrsDtls client(&ccb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
    client.enable_sped();
    SrsSpedController *client_sped = client.sped();
    ASSERT_TRUE(client_sped != NULL);
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());

    vector<string> raw;
    SrsStunPacket response;
    HELPER_EXPECT_FAILED(session.check(sped_build(client_sped, BindingRequest), raw, response));
    EXPECT_TRUE(session.socket_.written_data_.empty());
    EXPECT_EQ(SrsSpedStateConfirmed, sped->state());
    ccb.packets_.clear();

    int rounds = 0;
    for (; rounds < 16 && sped->state() != SrsSpedStateComplete; rounds++) {
        HELPER_ASSERT_SUCCESS(session.check(sped_build(client_sped, BindingRequest), raw, response));
        EXPECT_TRUE(raw.empty()) << rounds;
        SpedStunMessage msg = sped_attributes_of(response);
        HELPER_ASSERT_SUCCESS(sped_deliver(client, msg));
        ccb.packets_.clear();
    }

    EXPECT_LT(rounds, 16);
    EXPECT_EQ(SrsSpedStateComplete, sped->state());
    EXPECT_EQ(SrsRtcNetworkStateEstablished, session.network_->state_);
    EXPECT_EQ(1, ccb.handshake_done_count_);
    EXPECT_EQ(SrsSpedStateComplete, client_sped->state());
}

// Whether a DTLS 1.2 datagram carries a ChangeCipherSpec record.
static bool sim_has_ccs(const string &datagram)
{
    for (size_t pos = 0; pos + 13 <= datagram.size();) {
        if ((uint8_t)datagram[pos] == 20) {
            return true;
        }
        pos += 13 + (((uint8_t)datagram[pos + 11] << 8) | (uint8_t)datagram[pos + 12]);
    }
    return false;
}

// Without SPED, SRS as the DTLS 1.2 server is done once it sends its last flight, CCS and Finished,
// half a round trip before the client. When that flight is lost, the client resends its own last
// flight on its timer, and SRS, already done, answers it with the same CCS and Finished again,
// which OpenSSL keeps for that. The client then completes with the same SRTP keys, and SRS starts
// the session's streams once, at its own handshake done, not again on the client's resend.
VOID TEST(RtcFinishedLossTest, LostLastFlightIsResentForTheClientsResend)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(false));
    SrsRtcHandshakeDrop drop;
    session.network_->set_handshake_drop(&drop);
    drop.arm(0, 0, 1);

    vector<string> raw;
    SrsStunPacket response;
    HELPER_ASSERT_SUCCESS(session.check(SpedStunMessage(), raw, response));
    EXPECT_EQ(SrsRtcNetworkStateDtls, session.network_->state_);

    MockDtlsCallbackForHandshake ccb;
    SrsDtls client(&ccb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());

    // The ClientHello gets the first flight.
    vector<string> client_hello = ccb.packets_;
    ccb.packets_.clear();
    session.socket_.sent_.clear();
    for (int i = 0; i < (int)client_hello.size(); i++) {
        HELPER_ASSERT_SUCCESS(session.network_->on_dtls(&client_hello[i][0], (int)client_hello[i].size()));
    }
    vector<string> first_flight = session.socket_.sent_;
    ASSERT_FALSE(first_flight.empty());
    HELPER_ASSERT_SUCCESS(sped_deliver_raw(client, first_flight));

    // The client's last flight makes SRS done, and SRS's last flight is lost.
    vector<string> client_flight = ccb.packets_;
    ccb.packets_.clear();
    ASSERT_FALSE(client_flight.empty());
    session.socket_.sent_.clear();
    for (int i = 0; i < (int)client_flight.size(); i++) {
        HELPER_ASSERT_SUCCESS(session.network_->on_dtls(&client_flight[i][0], (int)client_flight[i].size()));
    }
    EXPECT_TRUE(session.dtls()->impl_->handshake_done_for_us_);
    EXPECT_EQ(SrsRtcNetworkStateEstablished, session.network_->state_);
    EXPECT_EQ(1, session.conn_.handshake_done_count_);
    EXPECT_TRUE(session.socket_.sent_.empty());
    EXPECT_EQ(0, ccb.handshake_done_count_);

    // The client resends its last flight when its timer expires.
    srs_utime_t timeout = sped_dtls_timeout(&client);
    ASSERT_GT(timeout, 0);
    ASSERT_LE(timeout, 3 * SRS_UTIME_SECONDS);
    usleep(timeout + 5 * SRS_UTIME_MILLISECONDS);
    HELPER_ASSERT_SUCCESS(client.impl_->handle_timeout());
    vector<string> client_resend = ccb.packets_;
    ccb.packets_.clear();
    ASSERT_FALSE(client_resend.empty());

    // SRS answers the resend with its last flight at once, which the simulator lets through.
    session.socket_.sent_.clear();
    for (int i = 0; i < (int)client_resend.size(); i++) {
        HELPER_ASSERT_SUCCESS(session.network_->on_dtls(&client_resend[i][0], (int)client_resend[i].size()));
    }
    vector<string> last_flight = session.socket_.sent_;
    ASSERT_FALSE(last_flight.empty());
    int ccs = 0;
    for (int i = 0; i < (int)last_flight.size(); i++) {
        ccs += sim_has_ccs(last_flight[i]) ? 1 : 0;
    }
    EXPECT_EQ(1, ccs);

    HELPER_ASSERT_SUCCESS(sped_deliver_raw(client, last_flight));
    EXPECT_EQ(1, ccb.handshake_done_count_);
    EXPECT_EQ(1, session.conn_.handshake_done_count_);
    sped_expect_srtp_keys_match(client, *session.dtls());
}

#if OPENSSL_VERSION_NUMBER >= 0x40100000L // v4.1
// Whether the session set up its SRTP, which it does at its handshake done, before it starts its streams.
static bool sped_srtp_ready(SpedUdpSession &session)
{
    SrsSecurityTransport *transport = dynamic_cast<SrsSecurityTransport *>(session.network_->transport_);
    SrsSRTP *srtp = dynamic_cast<SrsSRTP *>(transport->srtp_);
    return transport->handshake_done_ || srtp->send_ctx_ || srtp->recv_ctx_;
}

// SRS as the DTLS 1.3 server is done only on the client's Finished, so a lost server flight leaves both sides
// waiting: SRS sets up no SRTP and starts no stream, however long the wait. SRS's own timer resends the flight,
// the client answers it with its Certificate, CertificateVerify and Finished, and only that makes SRS done, once,
// with the same SRTP keys. A copy of the client's flight changes nothing after that. The client has its keys when
// it sends its Finished; an OpenSSL client reports its handshake done only on SRS's ACK of it, which follows.
VOID TEST(RtcServerFlightLossTest, Dtls13LostServerFlightWaitsForTheClientsFinished)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(false, "dtls1.3"));
    vector<string> raw;
    SrsStunPacket response;
    HELPER_ASSERT_SUCCESS(session.check(SpedStunMessage(), raw, response));
    EXPECT_EQ(SrsRtcNetworkStateDtls, session.network_->state_);

    MockDtlsCallbackForHandshake ccb;
    SrsDtls client(&ccb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.3"));
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());

    // The ClientHello gets SRS's flight, ServerHello to Finished, and the whole flight is lost.
    vector<string> client_hello = ccb.packets_;
    ccb.packets_.clear();
    session.socket_.sent_.clear();
    for (int i = 0; i < (int)client_hello.size(); i++) {
        HELPER_ASSERT_SUCCESS(session.network_->on_dtls(&client_hello[i][0], (int)client_hello[i].size()));
    }
    vector<string> lost = session.socket_.sent_;
    session.socket_.sent_.clear();
    ASSERT_GE((int)lost.size(), 2);
    EXPECT_TRUE(sped_dtls13_server_hello(lost[0]));
    EXPECT_FALSE(session.dtls()->impl_->handshake_done_for_us_);
    EXPECT_FALSE(SSL_is_init_finished(session.dtls()->impl_->dtls_));
    EXPECT_FALSE(sped_srtp_ready(session));
    EXPECT_EQ(0, session.conn_.handshake_done_count_);

    // SRS's timer starts at 50ms and doubles, so it resends the whole flight in this wait, the ServerHello first.
    sped_st_sleep(200 * SRS_UTIME_MILLISECONDS);
    vector<string> resent = session.socket_.sent_;
    session.socket_.sent_.clear();
    ASSERT_GE((int)resent.size(), 2);
    EXPECT_TRUE(sped_dtls13_server_hello(resent[0]));
    EXPECT_FALSE(session.dtls()->impl_->handshake_done_for_us_);
    EXPECT_FALSE(sped_srtp_ready(session));
    EXPECT_EQ(0, session.conn_.handshake_done_count_);
    EXPECT_EQ(SrsRtcNetworkStateDtls, session.network_->state_);

    // The client reads the resent flight and sends its own.
    HELPER_ASSERT_SUCCESS(sped_deliver_raw(client, resent));
    vector<string> client_flight = ccb.packets_;
    ccb.packets_.clear();
    ASSERT_FALSE(client_flight.empty());
    EXPECT_FALSE(session.dtls()->impl_->handshake_done_for_us_);
    EXPECT_FALSE(sped_srtp_ready(session));
    EXPECT_EQ(0, session.conn_.handshake_done_count_);

    // The client's Finished makes SRS done, sets up SRTP and starts the streams, once, and SRS ACKs the flight.
    for (int i = 0; i < (int)client_flight.size(); i++) {
        HELPER_ASSERT_SUCCESS(session.network_->on_dtls(&client_flight[i][0], (int)client_flight[i].size()));
    }
    EXPECT_TRUE(session.dtls()->impl_->handshake_done_for_us_);
    EXPECT_TRUE(sped_srtp_ready(session));
    EXPECT_EQ(SrsRtcNetworkStateEstablished, session.network_->state_);
    EXPECT_EQ(1, session.conn_.handshake_done_count_);
    vector<string> ack = session.socket_.sent_;
    session.socket_.sent_.clear();
    ASSERT_FALSE(ack.empty());
    HELPER_ASSERT_SUCCESS(sped_deliver_raw(client, ack));
    EXPECT_EQ(1, ccb.handshake_done_count_);
    sped_expect_srtp_keys_match(client, *session.dtls());

    for (int i = 0; i < (int)client_flight.size(); i++) {
        HELPER_ASSERT_SUCCESS(session.network_->on_dtls(&client_flight[i][0], (int)client_flight[i].size()));
    }
    sped_st_sleep(100 * SRS_UTIME_MILLISECONDS);
    EXPECT_EQ(1, session.conn_.handshake_done_count_);
    EXPECT_EQ(1, ccb.handshake_done_count_);
}

// The same with SPED: the response that embeds the first datagram of SRS's flight is lost with the flight's raw
// copy, and so are the client's checks for a while. SRS's timer resends the flight raw, as the client ACKed none
// of it, and SPED keeps embedding the flight it has. The client reads the raw copy and writes its flight, which
// comes inside its checks, and only it makes SRS done, once. A DTLS 1.3 server waits for no proof after that,
// since its client sent its Finished first, so the players start at SRS's handshake done.
VOID TEST(RtcServerFlightLossTest, Dtls13SpedLostServerFlightWaitsForTheClientsFinished)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true, "dtls1.3"));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);

    SpedClient client;
    HELPER_ASSERT_SUCCESS(client.start(0, "dtls1.3"));
    ASSERT_EQ(2, (int)client.cb_.packets_.size());

    // The first datagram of the ClientHello makes no flight; the second does, and its response and the raw
    // flight are lost.
    vector<string> raw;
    SrsStunPacket response;
    HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, response));
    EXPECT_TRUE(raw.empty());
    HELPER_ASSERT_SUCCESS(client.on_response(response));

    SrsStunPacket lost;
    HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, lost));
    ASSERT_GE((int)raw.size(), 2);
    vector<string> server_flight = raw;
    ASSERT_TRUE(lost.has_dtls_in_stun_data());
    EXPECT_TRUE(sped->flight_ == server_flight);
    EXPECT_EQ(SrsSpedStateConfirmed, sped->state());
    EXPECT_FALSE(session.dtls()->impl_->handshake_done_for_us_);
    EXPECT_FALSE(sped_srtp_ready(session));
    EXPECT_EQ(0, session.conn_.handshake_done_count_);

    // No check arrives, and SRS's timer resends the flight raw only: SPED's flight stays the one it embeds.
    session.socket_.sent_.clear();
    sped_st_sleep(200 * SRS_UTIME_MILLISECONDS);
    vector<string> resent = session.socket_.sent_;
    ASSERT_GE((int)resent.size(), 2);
    EXPECT_TRUE(sped_dtls13_server_hello(resent[0]));
    EXPECT_FALSE(sped_sent(server_flight, resent[0]));
    EXPECT_TRUE(sped->flight_ == server_flight);
    EXPECT_EQ(SrsSpedStateConfirmed, sped->state());
    EXPECT_FALSE(session.dtls()->impl_->handshake_done_for_us_);
    EXPECT_FALSE(sped_srtp_ready(session));
    EXPECT_EQ(0, session.conn_.handshake_done_count_);

    // The client reads the raw copy and writes its flight.
    HELPER_ASSERT_SUCCESS(sped_deliver_raw(client.dtls_, resent));
    EXPECT_FALSE(client.cb_.packets_.empty());
    EXPECT_FALSE(session.dtls()->impl_->handshake_done_for_us_);
    EXPECT_FALSE(sped_srtp_ready(session));
    EXPECT_EQ(0, session.conn_.handshake_done_count_);

    // Its flight comes inside its checks, with its raw copy lost, and makes SRS done, once.
    HELPER_ASSERT_SUCCESS(sped_run_to_pending(session, client));
    EXPECT_TRUE(session.dtls()->impl_->handshake_done_for_us_);
    EXPECT_TRUE(sped_srtp_ready(session));
    EXPECT_EQ(1, session.conn_.handshake_done_count_);
    EXPECT_FALSE(sped->waits_for_peer());

    HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, response));
    EXPECT_EQ(SrsSpedStateComplete, sped->state());
    EXPECT_EQ(SrsRtcNetworkStateEstablished, session.network_->state_);
    EXPECT_EQ(1, session.conn_.handshake_done_count_);
    sped_expect_srtp_keys_match(client.dtls_, *session.dtls());
}
#endif

// A session that counts how often SPED told it SPED is complete.
class MockSpedHandler : public ISrsSpedHandler
{
public:
    int complete_count_;

public:
    MockSpedHandler()
    {
        complete_count_ = 0;
    }
    virtual void on_sped_complete()
    {
        complete_count_++;
    }
};

// On DTLS 1.2, SRS as DTLS server is done when it sends its last flight, CCS and Finished, half a round trip
// before the client has it, and a client drops SRTP until it is done. So SRS waits for the client's proof that it
// is done, and each proof ends the wait, once: the ACK of that flight, a check without SPED attributes, or an SRTP
// or SRTCP packet. A check that ACKs only other datagrams is no proof.
VOID TEST(RtcSpedPeerTest, EachProofOfTheClientEndsTheWaitOnce)
{
    for (int proof = 0; proof < 3; proof++) {
        SrsSpedController sped;
        MockSpedHandler handler;
        sped.set_handler(&handler);

        EXPECT_FALSE(sped_receive_empty_ack(sped));
        string server_flight = sped_handshake("server-hello-done");
        sped_capture(sped, server_flight);
        sped.flush();
        EXPECT_FALSE(sped.waits_for_peer()) << proof;

        string server_finished = sped_dtls_datagram(20, "ccs-finished");
        sped_capture(sped, server_finished);
        sped.flush();
        sped.on_handshake_done(false, true);
        EXPECT_EQ(SrsSpedStatePending, sped.state()) << proof;
        EXPECT_TRUE(sped.waits_for_peer()) << proof;

        vector<uint32_t> acks;
        acks.push_back(sped_crc(server_flight));
        EXPECT_FALSE(sped_receive_acks(sped, acks));
        EXPECT_TRUE(sped.waits_for_peer()) << proof;
        EXPECT_EQ(0, handler.complete_count_) << proof;

        if (proof == 0) {
            acks.push_back(sped_crc(server_finished));
            EXPECT_FALSE(sped_receive_acks(sped, acks));
        } else if (proof == 1) {
            EXPECT_FALSE(sped_receive_plain(sped));
        } else {
            sped.on_srtp_received();
        }
        EXPECT_EQ(SrsSpedStateComplete, sped.state()) << proof;
        EXPECT_FALSE(sped.waits_for_peer()) << proof;
        EXPECT_EQ(1, handler.complete_count_) << proof;

        acks.push_back(sped_crc(server_finished));
        EXPECT_FALSE(sped_receive_acks(sped, acks));
        EXPECT_FALSE(sped_receive_plain(sped));
        sped.on_srtp_received();
        EXPECT_EQ(1, handler.complete_count_) << proof;
    }
}

// SRS as DTLS client is done on the server's last flight, and SRS as DTLS 1.3 server on the client's Finished, so
// the peer is done first and SRS waits for nothing, though SPED still ends as before.
VOID TEST(RtcSpedPeerTest, PeerDoneFirstIsNotWaitedFor)
{
    SrsSpedController sped;
    MockSpedHandler handler;
    sped.set_handler(&handler);
    EXPECT_FALSE(sped_receive_empty_ack(sped));

    string last_flight = sped_dtls_datagram(0x2f, "ack-of-finished");
    sped_capture(sped, last_flight);
    sped.flush();
    sped.on_handshake_done(false, false);
    EXPECT_EQ(SrsSpedStatePending, sped.state());
    EXPECT_FALSE(sped.waits_for_peer());

    sped.on_srtp_received();
    EXPECT_EQ(SrsSpedStateComplete, sped.state());
    EXPECT_EQ(1, handler.complete_count_);
}

// A client that does not use SPED is never waited for, and no handler is told.
VOID TEST(RtcSpedPeerTest, OffIsNotWaitedFor)
{
    SrsSpedController sped;
    MockSpedHandler handler;
    sped.set_handler(&handler);
    EXPECT_FALSE(sped_receive_plain(sped));
    EXPECT_EQ(SrsSpedStateOff, sped.state());

    sped.on_handshake_done(false, true);
    EXPECT_FALSE(sped.waits_for_peer());
    sped.on_srtp_received();
    EXPECT_EQ(0, handler.complete_count_);
}

// SRS as DTLS 1.2 server, with the whole handshake inside STUN: at SRS's handshake done the client has not yet read
// SRS's last flight, so SRS waits for it; the client's next check ACKs that flight and ends the wait.
VOID TEST(RtcSpedPeerTest, Dtls12ServerWaitsForTheClientsAck)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);
    MockSpedHandler handler;
    sped->set_handler(&handler);

    SpedClient client;
    HELPER_ASSERT_SUCCESS(client.start(0));
    HELPER_ASSERT_SUCCESS(sped_run_to_pending(session, client));
    EXPECT_EQ(1, session.conn_.handshake_done_count_);
    EXPECT_TRUE(sped->waits_for_peer());
    EXPECT_EQ(0, handler.complete_count_);

    vector<string> raw;
    SrsStunPacket response;
    HELPER_ASSERT_SUCCESS(session.check(client.request(), raw, response));
    EXPECT_EQ(SrsSpedStateComplete, sped->state());
    EXPECT_FALSE(sped->waits_for_peer());
    EXPECT_EQ(1, handler.complete_count_);
}

// SRS as DTLS 1.2 client is done when it reads the server's last flight, after the server, so it waits for nothing.
VOID TEST(RtcSpedPeerTest, Dtls12ClientDoesNotWait)
{
    srs_error_t err;

    MockDtlsCallbackForHandshake scb, pcb;
    SrsDtls srs(&scb);
    SrsDtls peer(&pcb);
    HELPER_ASSERT_SUCCESS(srs.initialize("active", "dtls1.2"));
    HELPER_ASSERT_SUCCESS(peer.initialize("passive", "dtls1.2"));
    srs.enable_sped();
    peer.enable_sped();
    SrsSpedController *srs_sped = srs.sped();
    SrsSpedController *peer_sped = peer.sped();

    HELPER_ASSERT_SUCCESS(peer.start_active_handshake());
    HELPER_ASSERT_SUCCESS(srs.start_active_handshake());

    bool srs_done = false;
    for (int rounds = 0; rounds < 32; rounds++) {
        SpedStunMessage request = sped_build(peer_sped, BindingRequest);
        HELPER_ASSERT_SUCCESS(sped_deliver(srs, request));
        srs_done = scb.handshake_done_count_ > 0;
        if (srs_done) {
            break;
        }
        SpedStunMessage response = sped_build(srs_sped, BindingResponse);
        HELPER_ASSERT_SUCCESS(sped_deliver(peer, response));
        scb.packets_.clear();
        pcb.packets_.clear();
    }

    // The peer, the DTLS 1.2 server, was done first, and still waits for SRS's ACK of its last flight.
    ASSERT_TRUE(srs_done);
    EXPECT_EQ(1, pcb.handshake_done_count_);
    EXPECT_EQ(SrsSpedStatePending, srs_sped->state());
    EXPECT_FALSE(srs_sped->waits_for_peer());
    EXPECT_TRUE(peer_sped->waits_for_peer());
}

#if OPENSSL_VERSION_NUMBER >= 0x40100000L // v4.1
// SRS as DTLS 1.3 server is done only on the client's Finished, which the client sends once it is done, so SRS
// waits for nothing.
VOID TEST(RtcSpedPeerTest, Dtls13ServerDoesNotWait)
{
    srs_error_t err;

    SpedUdpSession session;
    HELPER_ASSERT_SUCCESS(session.initialize(true, "dtls1.3"));
    SrsSpedController *sped = session.sped();
    ASSERT_TRUE(sped != NULL);

    SpedClient client;
    HELPER_ASSERT_SUCCESS(client.start(0, "dtls1.3"));
    HELPER_ASSERT_SUCCESS(sped_run_to_pending(session, client));
    EXPECT_EQ(1, session.conn_.handshake_done_count_);
    EXPECT_FALSE(sped->waits_for_peer());
}
#endif
