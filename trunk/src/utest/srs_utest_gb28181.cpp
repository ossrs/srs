//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//
#include <srs_utest_gb28181.hpp>

#include <sstream>
using namespace std;

#include <srs_protocol_http_stack.hpp>
#include <srs_protocol_http_conn.hpp>
#include <srs_utest_protocol.hpp>
#include <srs_protocol_json.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_file.hpp>
#include <srs_utest_kernel.hpp>
#include <srs_app_http_static.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_gb28181.hpp>
#include <srs_app_rtc_sdp.hpp>
#include <srs_kernel_rtc_rtp.hpp>

extern void srs_sip_parse_address(const std::string& address, std::string& user, std::string& host);

VOID TEST(ProtocolGbSipTest, SipParseAddress)
{
    if (true) {
        string user, host;
        srs_sip_parse_address("", user, host);
        EXPECT_STREQ("", user.c_str());
        EXPECT_STREQ("", host.c_str());
    }

    if (true) {
        string user, host;
        srs_sip_parse_address("sip:", user, host);
        EXPECT_STREQ("", user.c_str());
        EXPECT_STREQ("", host.c_str());
    }

    if (true) {
        string user, host;
        srs_sip_parse_address("sip:bob", user, host);
        EXPECT_STREQ("bob", user.c_str());
        EXPECT_STREQ("", host.c_str());
    }

    if (true) {
        string user, host;
        srs_sip_parse_address("sip:bob@", user, host);
        EXPECT_STREQ("bob", user.c_str());
        EXPECT_STREQ("", host.c_str());
    }

    if (true) {
        string user, host;
        srs_sip_parse_address("sip:bob@host.com", user, host);
        EXPECT_STREQ("bob", user.c_str());
        EXPECT_STREQ("host.com", host.c_str());
    }

    if (true) {
        string user, host;
        srs_sip_parse_address("Bob <bob@host.com>", user, host);
        EXPECT_STREQ("bob", user.c_str());
        EXPECT_STREQ("host.com", host.c_str());
    }

    if (true) {
        string user, host;
        srs_sip_parse_address("Bob <sip:bob@host.com>", user, host);
        EXPECT_STREQ("bob", user.c_str());
        EXPECT_STREQ("host.com", host.c_str());
    }
}

VOID TEST(ProtocolGbSipTest, SipViaBranchMagic)
{
    srs_error_t err = srs_success;

    // See https://www.ietf.org/rfc/rfc3261.html#section-24.2
    MockMSegmentsReader r;
    r.in_bytes.push_back("REGISTER sip:registrar.biloxi.com SIP/2.0\r\n");
    r.in_bytes.push_back("Via: SIP/2.0/UDP bobspc.biloxi.com:5060;branch=xxxx\r\n");
    r.in_bytes.push_back("To: Bob <sip:bob@biloxi.com>\r\n");
    r.in_bytes.push_back("From: Bob <sip:bob@biloxi.com>;tag=456248\r\n");
    r.in_bytes.push_back("Call-ID: 843817637684230@998sdasdh09\r\n");
    r.in_bytes.push_back("CSeq: 1826 REGISTER\r\n");
    r.in_bytes.push_back("Content-Length: 0\r\n");
    r.in_bytes.push_back("\r\n");

    SrsHttpParser p;
    HELPER_ASSERT_SUCCESS(p.initialize(HTTP_BOTH));

    ISrsHttpMessage* msg = NULL;
    SrsAutoFree(ISrsHttpMessage, msg);
    HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));

    SrsSipMessage smsg;
    HELPER_ASSERT_FAILED(smsg.parse(msg));
}

VOID TEST(ProtocolGbSipTest, SipRegisterRequest)
{
    srs_error_t err = srs_success;

    // See https://www.ietf.org/rfc/rfc3261.html#section-24.1
    MockMSegmentsReader source;
    source.in_bytes.push_back("REGISTER sip:registrar.biloxi.com SIP/2.0\r\n");
    source.in_bytes.push_back("Via: SIP/2.0/UDP bobspc.biloxi.com:5060;branch=z9hG4bKnashds7\r\n");
    source.in_bytes.push_back("Max-Forwards: 70\r\n");
    source.in_bytes.push_back("To: Bob <sip:bob@biloxi.com>\r\n");
    source.in_bytes.push_back("From: Bob <sip:bob@biloxi.com>;tag=456248\r\n");
    source.in_bytes.push_back("Call-ID: 843817637684230@998sdasdh09\r\n");
    source.in_bytes.push_back("CSeq: 1826 REGISTER\r\n");
    source.in_bytes.push_back("Contact: <sip:bob@192.0.2.4>\r\n");
    source.in_bytes.push_back("Expires: 7200\r\n");
    source.in_bytes.push_back("Content-Length: 0\r\n");
    source.in_bytes.push_back("\r\n");

    if (true) {
        MockMSegmentsReader r = source;

        SrsHttpParser p;
        HELPER_ASSERT_SUCCESS(p.initialize(HTTP_REQUEST));

        ISrsHttpMessage* msg = NULL;
        SrsAutoFree(ISrsHttpMessage, msg);
        HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));
        EXPECT_EQ(HTTP_REGISTER, msg->method());
        EXPECT_STREQ("/sip:registrar.biloxi.com", msg->path().c_str());
        EXPECT_STREQ("SIP/2.0/UDP bobspc.biloxi.com:5060;branch=z9hG4bKnashds7", msg->header()->get("Via").c_str());
        EXPECT_STREQ("70", msg->header()->get("Max-Forwards").c_str());
        EXPECT_STREQ("Bob <sip:bob@biloxi.com>", msg->header()->get("To").c_str());
        EXPECT_STREQ("Bob <sip:bob@biloxi.com>;tag=456248", msg->header()->get("From").c_str());
        EXPECT_STREQ("843817637684230@998sdasdh09", msg->header()->get("Call-ID").c_str());
        EXPECT_STREQ("1826 REGISTER", msg->header()->get("CSeq").c_str());
        EXPECT_STREQ("<sip:bob@192.0.2.4>", msg->header()->get("Contact").c_str());
        EXPECT_STREQ("7200", msg->header()->get("Expires").c_str());
        EXPECT_EQ(0, msg->content_length());

        SrsSipMessage smsg;
        HELPER_ASSERT_SUCCESS(smsg.parse(msg));
        EXPECT_EQ(HTTP_REQUEST, smsg.type_);
        EXPECT_EQ(HTTP_REGISTER, smsg.method_);
        EXPECT_STREQ("sip:registrar.biloxi.com", smsg.request_uri_.c_str());
        EXPECT_STREQ("SIP/2.0/UDP bobspc.biloxi.com:5060;branch=z9hG4bKnashds7", smsg.via_.c_str());
        EXPECT_STREQ("UDP", smsg.via_transport_.c_str());
        EXPECT_STREQ("bobspc.biloxi.com:5060", smsg.via_send_by_.c_str());
        EXPECT_STREQ("bobspc.biloxi.com", smsg.via_send_by_address_.c_str());
        EXPECT_EQ(5060, smsg.via_send_by_port_);
        EXPECT_STREQ("branch=z9hG4bKnashds7", smsg.via_branch_.c_str());
        EXPECT_STREQ("Bob <sip:bob@biloxi.com>", smsg.to_.c_str());
        EXPECT_STREQ("Bob <sip:bob@biloxi.com>", smsg.to_address_.c_str());
        EXPECT_STREQ("", smsg.to_tag_.c_str());
        EXPECT_STREQ("Bob <sip:bob@biloxi.com>;tag=456248", smsg.from_.c_str());
        EXPECT_STREQ("Bob <sip:bob@biloxi.com>", smsg.from_address_.c_str());
        EXPECT_STREQ("tag=456248", smsg.from_tag_.c_str());
        EXPECT_STREQ("843817637684230@998sdasdh09", smsg.call_id_.c_str());
        EXPECT_STREQ("<sip:bob@192.0.2.4>", smsg.contact_.c_str());
        EXPECT_STREQ("bob", smsg.contact_user_.c_str());
        EXPECT_STREQ("192.0.2.4", smsg.contact_host_.c_str());
        EXPECT_STREQ("192.0.2.4", smsg.contact_host_address_.c_str());
        EXPECT_EQ(5060, smsg.contact_host_port_);
        EXPECT_STREQ("1826 REGISTER", smsg.cseq_.c_str());
        EXPECT_EQ((uint32_t)1826, smsg.cseq_number_);
        EXPECT_EQ((uint32_t)7200, smsg.expires_);
        EXPECT_EQ((uint32_t)70, smsg.max_forwards_);
    }

    // Parse in HTTP_REQUEST mode.
    if (true) {
        MockMSegmentsReader r = source;

        SrsHttpParser p;
        HELPER_ASSERT_SUCCESS(p.initialize(HTTP_REQUEST));

        ISrsHttpMessage* msg = NULL;
        SrsAutoFree(ISrsHttpMessage, msg);
        HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));
        EXPECT_EQ(HTTP_REQUEST, (http_parser_type)msg->message_type());
        EXPECT_EQ(HTTP_REGISTER, msg->method());
        EXPECT_STREQ("/sip:registrar.biloxi.com", msg->path().c_str());
    }

    // Parse in HTTP_BOTH mode.
    if (true) {
        MockMSegmentsReader r = source;

        SrsHttpParser p;
        HELPER_ASSERT_SUCCESS(p.initialize(HTTP_BOTH));

        ISrsHttpMessage* msg = NULL;
        SrsAutoFree(ISrsHttpMessage, msg);
        HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));
        EXPECT_EQ(HTTP_REQUEST, (http_parser_type)msg->message_type());
        EXPECT_EQ(HTTP_REGISTER, msg->method());
        EXPECT_STREQ("/sip:registrar.biloxi.com", msg->path().c_str());
    }
}

VOID TEST(ProtocolGbSipTest, SipRegisterResponse)
{
    srs_error_t err = srs_success;

    // See https://www.ietf.org/rfc/rfc3261.html#section-24.1
    MockMSegmentsReader source;
    source.in_bytes.push_back("SIP/2.0 200 OK\r\n");
    source.in_bytes.push_back("Via: SIP/2.0/UDP bobspc.biloxi.com:5060;branch=z9hG4bKnashds7;received=192.0.2.4\r\n");
    source.in_bytes.push_back("To: Bob <sip:bob@biloxi.com>;tag=2493k59kd\r\n");
    source.in_bytes.push_back("From: Bob <sip:bob@biloxi.com>;tag=456248\r\n");
    source.in_bytes.push_back("Call-ID: 843817637684230@998sdasdh09\r\n");
    source.in_bytes.push_back("CSeq: 1826 REGISTER\r\n");
    source.in_bytes.push_back("Contact: <sip:bob@192.0.2.4>\r\n");
    source.in_bytes.push_back("Expires: 7200\r\n");
    source.in_bytes.push_back("Content-Length: 0\r\n");
    source.in_bytes.push_back("\r\n");

    if (true) {
        MockMSegmentsReader r = source;

        SrsHttpParser p;
        HELPER_ASSERT_SUCCESS(p.initialize(HTTP_RESPONSE));

        ISrsHttpMessage* msg = NULL;
        SrsAutoFree(ISrsHttpMessage, msg);
        HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));
        EXPECT_EQ(HTTP_RESPONSE, (http_parser_type) msg->message_type());
        EXPECT_EQ(200, msg->status_code());
        EXPECT_STREQ("SIP/2.0/UDP bobspc.biloxi.com:5060;branch=z9hG4bKnashds7;received=192.0.2.4",
                     msg->header()->get("Via").c_str());
        EXPECT_STREQ("Bob <sip:bob@biloxi.com>;tag=2493k59kd", msg->header()->get("To").c_str());
        EXPECT_STREQ("Bob <sip:bob@biloxi.com>;tag=456248", msg->header()->get("From").c_str());
        EXPECT_STREQ("843817637684230@998sdasdh09", msg->header()->get("Call-ID").c_str());
        EXPECT_STREQ("1826 REGISTER", msg->header()->get("CSeq").c_str());
        EXPECT_STREQ("<sip:bob@192.0.2.4>", msg->header()->get("Contact").c_str());
        EXPECT_STREQ("7200", msg->header()->get("Expires").c_str());
        EXPECT_EQ(0, msg->content_length());

        SrsSipMessage smsg;
        HELPER_ASSERT_SUCCESS(smsg.parse(msg));
        EXPECT_EQ(HTTP_RESPONSE, smsg.type_);
        EXPECT_EQ(HTTP_STATUS_OK, smsg.status_);
        EXPECT_STREQ("SIP/2.0/UDP bobspc.biloxi.com:5060;branch=z9hG4bKnashds7;received=192.0.2.4", smsg.via_.c_str());
        EXPECT_STREQ("UDP", smsg.via_transport_.c_str());
        EXPECT_STREQ("bobspc.biloxi.com:5060", smsg.via_send_by_.c_str());
        EXPECT_STREQ("bobspc.biloxi.com", smsg.via_send_by_address_.c_str());
        EXPECT_EQ(5060, smsg.via_send_by_port_);
        EXPECT_STREQ("branch=z9hG4bKnashds7", smsg.via_branch_.c_str());
        EXPECT_STREQ("Bob <sip:bob@biloxi.com>;tag=2493k59kd", smsg.to_.c_str());
        EXPECT_STREQ("Bob <sip:bob@biloxi.com>", smsg.to_address_.c_str());
        EXPECT_STREQ("tag=2493k59kd", smsg.to_tag_.c_str());
        EXPECT_STREQ("Bob <sip:bob@biloxi.com>;tag=456248", smsg.from_.c_str());
        EXPECT_STREQ("Bob <sip:bob@biloxi.com>", smsg.from_address_.c_str());
        EXPECT_STREQ("tag=456248", smsg.from_tag_.c_str());
        EXPECT_STREQ("843817637684230@998sdasdh09", smsg.call_id_.c_str());
        EXPECT_STREQ("<sip:bob@192.0.2.4>", smsg.contact_.c_str());
        EXPECT_STREQ("bob", smsg.contact_user_.c_str());
        EXPECT_STREQ("192.0.2.4", smsg.contact_host_.c_str());
        EXPECT_STREQ("192.0.2.4", smsg.contact_host_address_.c_str());
        EXPECT_EQ(5060, smsg.contact_host_port_);
        EXPECT_STREQ("1826 REGISTER", smsg.cseq_.c_str());
        EXPECT_EQ((uint32_t)1826, smsg.cseq_number_);
    }

    // Parse in HTTP_RESPONSE mode.
    if (true) {
        MockMSegmentsReader r = source;

        SrsHttpParser p;
        HELPER_ASSERT_SUCCESS(p.initialize(HTTP_RESPONSE));

        ISrsHttpMessage* msg = NULL;
        SrsAutoFree(ISrsHttpMessage, msg);
        HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));
        EXPECT_EQ(HTTP_RESPONSE, (http_parser_type)msg->message_type());
        EXPECT_EQ(200, msg->status_code());
    }

    // Parse in HTTP_BOTH mode.
    if (true) {
        MockMSegmentsReader r = source;

        SrsHttpParser p;
        HELPER_ASSERT_SUCCESS(p.initialize(HTTP_BOTH));

        ISrsHttpMessage* msg = NULL;
        SrsAutoFree(ISrsHttpMessage, msg);
        HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));
        EXPECT_EQ(HTTP_RESPONSE, (http_parser_type)msg->message_type());
        EXPECT_EQ(200, msg->status_code());
    }
}

VOID TEST(ProtocolGbSipTest, SipSessionUacInviteRequest)
{
    srs_error_t err = srs_success;

    // See https://www.ietf.org/rfc/rfc3261.html#section-24.2
    MockMSegmentsReader r;
    r.in_bytes.push_back("INVITE sip:bob@biloxi.com SIP/2.0\r\n");
    r.in_bytes.push_back("Via: SIP/2.0/UDP pc33.atlanta.com;branch=z9hG4bKnashds8\r\n");
    r.in_bytes.push_back("Max-Forwards: 70\r\n");
    r.in_bytes.push_back("To: Bob <sip:bob@biloxi.com>\r\n");
    r.in_bytes.push_back("From: Alice <sip:alice@atlanta.com>;tag=1928301774\r\n");
    r.in_bytes.push_back("Call-ID: a84b4c76e66710\r\n");
    r.in_bytes.push_back("CSeq: 314159 INVITE\r\n");
    r.in_bytes.push_back("Contact: <sip:alice@pc33.atlanta.com>\r\n");
    r.in_bytes.push_back("Content-Type: application/sdp\r\n");
    r.in_bytes.push_back("Content-Length: 142\r\n");
    r.in_bytes.push_back("\r\n");
    r.in_bytes.push_back(string(142, 'x'));

    SrsHttpParser p;
    HELPER_ASSERT_SUCCESS(p.initialize(HTTP_BOTH));

    ISrsHttpMessage* msg = NULL;
    SrsAutoFree(ISrsHttpMessage, msg);
    HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));
    EXPECT_EQ(HTTP_INVITE, msg->method());
    EXPECT_STREQ("/sip:bob@biloxi.com", msg->path().c_str());
    EXPECT_STREQ("SIP/2.0/UDP pc33.atlanta.com;branch=z9hG4bKnashds8", msg->header()->get("Via").c_str());
    EXPECT_STREQ("70", msg->header()->get("Max-Forwards").c_str());
    EXPECT_STREQ("Bob <sip:bob@biloxi.com>", msg->header()->get("To").c_str());
    EXPECT_STREQ("Alice <sip:alice@atlanta.com>;tag=1928301774", msg->header()->get("From").c_str());
    EXPECT_STREQ("a84b4c76e66710", msg->header()->get("Call-ID").c_str());
    EXPECT_STREQ("314159 INVITE", msg->header()->get("CSeq").c_str());
    EXPECT_STREQ("<sip:alice@pc33.atlanta.com>", msg->header()->get("Contact").c_str());
    EXPECT_EQ(142, msg->content_length());

    SrsSipMessage smsg;
    HELPER_ASSERT_SUCCESS(smsg.parse(msg));
    EXPECT_EQ(HTTP_REQUEST, smsg.type_);
    EXPECT_EQ(HTTP_INVITE, smsg.method_);
    EXPECT_STREQ("sip:bob@biloxi.com", smsg.request_uri_.c_str());
    EXPECT_STREQ("SIP/2.0/UDP pc33.atlanta.com;branch=z9hG4bKnashds8", smsg.via_.c_str());
    EXPECT_STREQ("UDP", smsg.via_transport_.c_str());
    EXPECT_STREQ("pc33.atlanta.com", smsg.via_send_by_.c_str());
    EXPECT_STREQ("pc33.atlanta.com", smsg.via_send_by_address_.c_str());
    EXPECT_EQ(5060, smsg.via_send_by_port_);
    EXPECT_STREQ("branch=z9hG4bKnashds8", smsg.via_branch_.c_str());
    EXPECT_STREQ("Bob <sip:bob@biloxi.com>", smsg.to_.c_str());
    EXPECT_STREQ("Bob <sip:bob@biloxi.com>", smsg.to_address_.c_str());
    EXPECT_STREQ("", smsg.to_tag_.c_str());
    EXPECT_STREQ("Alice <sip:alice@atlanta.com>;tag=1928301774", smsg.from_.c_str());
    EXPECT_STREQ("Alice <sip:alice@atlanta.com>", smsg.from_address_.c_str());
    EXPECT_STREQ("tag=1928301774", smsg.from_tag_.c_str());
    EXPECT_STREQ("a84b4c76e66710", smsg.call_id_.c_str());
    EXPECT_STREQ("<sip:alice@pc33.atlanta.com>", smsg.contact_.c_str());
    EXPECT_STREQ("alice", smsg.contact_user_.c_str());
    EXPECT_STREQ("pc33.atlanta.com", smsg.contact_host_.c_str());
    EXPECT_STREQ("pc33.atlanta.com", smsg.contact_host_address_.c_str());
    EXPECT_EQ(5060, smsg.contact_host_port_);
    EXPECT_STREQ("314159 INVITE", smsg.cseq_.c_str());
    EXPECT_EQ((uint32_t)314159, smsg.cseq_number_);
    EXPECT_EQ((uint32_t)70, smsg.max_forwards_);
    EXPECT_EQ((size_t)142, smsg.body_.length());
}

VOID TEST(ProtocolGbSipTest, SipSessionUasTryingResponse)
{
    srs_error_t err = srs_success;

    // See https://www.ietf.org/rfc/rfc3261.html#section-24.2
    MockMSegmentsReader r;
    r.in_bytes.push_back("SIP/2.0 100 Trying\r\n");
    r.in_bytes.push_back("Via: SIP/2.0/UDP pc33.atlanta.com;branch=z9hG4bKnashds8;received=192.0.2.1\r\n");
    r.in_bytes.push_back("To: Bob <sip:bob@biloxi.com>\r\n");
    r.in_bytes.push_back("From: Alice <sip:alice@atlanta.com>;tag=1928301774\r\n");
    r.in_bytes.push_back("Call-ID: a84b4c76e66710\r\n");
    r.in_bytes.push_back("CSeq: 314159 INVITE\r\n");
    r.in_bytes.push_back("Content-Length: 0\r\n");
    r.in_bytes.push_back("\r\n");

    SrsHttpParser p;
    HELPER_ASSERT_SUCCESS(p.initialize(HTTP_RESPONSE));

    ISrsHttpMessage* msg = NULL;
    SrsAutoFree(ISrsHttpMessage, msg);
    HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));
    EXPECT_EQ(HTTP_RESPONSE, (http_parser_type) msg->message_type());
    EXPECT_EQ(100, msg->status_code());
    EXPECT_STREQ("SIP/2.0/UDP pc33.atlanta.com;branch=z9hG4bKnashds8;received=192.0.2.1", msg->header()->get("Via").c_str());
    EXPECT_STREQ("Bob <sip:bob@biloxi.com>", msg->header()->get("To").c_str());
    EXPECT_STREQ("Alice <sip:alice@atlanta.com>;tag=1928301774", msg->header()->get("From").c_str());
    EXPECT_STREQ("a84b4c76e66710", msg->header()->get("Call-ID").c_str());
    EXPECT_STREQ("314159 INVITE", msg->header()->get("CSeq").c_str());
    EXPECT_EQ(0, msg->content_length());

    SrsSipMessage smsg;
    HELPER_ASSERT_SUCCESS(smsg.parse(msg));
    EXPECT_EQ(HTTP_RESPONSE, smsg.type_);
    EXPECT_EQ(100, smsg.status_);
    EXPECT_STREQ("SIP/2.0/UDP pc33.atlanta.com;branch=z9hG4bKnashds8;received=192.0.2.1", smsg.via_.c_str());
    EXPECT_STREQ("UDP", smsg.via_transport_.c_str());
    EXPECT_STREQ("pc33.atlanta.com", smsg.via_send_by_.c_str());
    EXPECT_STREQ("pc33.atlanta.com", smsg.via_send_by_address_.c_str());
    EXPECT_EQ(5060, smsg.via_send_by_port_);
    EXPECT_STREQ("branch=z9hG4bKnashds8", smsg.via_branch_.c_str());
    EXPECT_STREQ("Bob <sip:bob@biloxi.com>", smsg.to_.c_str());
    EXPECT_STREQ("Bob <sip:bob@biloxi.com>", smsg.to_address_.c_str());
    EXPECT_STREQ("", smsg.to_tag_.c_str());
    EXPECT_STREQ("Alice <sip:alice@atlanta.com>;tag=1928301774", smsg.from_.c_str());
    EXPECT_STREQ("Alice <sip:alice@atlanta.com>", smsg.from_address_.c_str());
    EXPECT_STREQ("tag=1928301774", smsg.from_tag_.c_str());
    EXPECT_STREQ("a84b4c76e66710", smsg.call_id_.c_str());
    EXPECT_STREQ("314159 INVITE", smsg.cseq_.c_str());
    EXPECT_EQ((uint32_t)314159, smsg.cseq_number_);
}

VOID TEST(ProtocolGbSipTest, SipSessionUas200OkResponse)
{
    srs_error_t err = srs_success;

    // See https://www.ietf.org/rfc/rfc3261.html#section-24.2
    MockMSegmentsReader r;
    r.in_bytes.push_back("SIP/2.0 200 OK\r\n");
    r.in_bytes.push_back("Via: SIP/2.0/UDP server10.biloxi.com;branch=z9hG4bK4b43c2ff8.1;received=192.0.2.3\r\n");
    r.in_bytes.push_back("Via: SIP/2.0/UDP bigbox3.site3.atlanta.com;branch=z9hG4bK77ef4c2312983.1;received=192.0.2.2\r\n");
    r.in_bytes.push_back("Via: SIP/2.0/UDP pc33.atlanta.com;branch=z9hG4bKnashds8;received=192.0.2.1\r\n");
    r.in_bytes.push_back("To: Bob <sip:bob@biloxi.com>;tag=a6c85cf\r\n");
    r.in_bytes.push_back("From: Alice <sip:alice@atlanta.com>;tag=1928301774\r\n");
    r.in_bytes.push_back("Call-ID: a84b4c76e66710\r\n");
    r.in_bytes.push_back("CSeq: 314159 INVITE\r\n");
    r.in_bytes.push_back("Contact: <sip:bob@192.0.2.4>\r\n");
    r.in_bytes.push_back("Content-Type: application/sdp\r\n");
    r.in_bytes.push_back("Content-Length: 131\r\n");
    r.in_bytes.push_back("\r\n");
    r.in_bytes.push_back(string(131, 'x'));

    SrsHttpParser p;
    HELPER_ASSERT_SUCCESS(p.initialize(HTTP_RESPONSE));

    ISrsHttpMessage* msg = NULL;
    SrsAutoFree(ISrsHttpMessage, msg);
    HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));
    EXPECT_EQ(HTTP_RESPONSE, (http_parser_type) msg->message_type());
    EXPECT_EQ(200, msg->status_code());
    EXPECT_STREQ("SIP/2.0/UDP pc33.atlanta.com;branch=z9hG4bKnashds8;received=192.0.2.1", msg->header()->get("Via").c_str());
    EXPECT_STREQ("Bob <sip:bob@biloxi.com>;tag=a6c85cf", msg->header()->get("To").c_str());
    EXPECT_STREQ("Alice <sip:alice@atlanta.com>;tag=1928301774", msg->header()->get("From").c_str());
    EXPECT_STREQ("<sip:bob@192.0.2.4>", msg->header()->get("Contact").c_str());
    EXPECT_STREQ("a84b4c76e66710", msg->header()->get("Call-ID").c_str());
    EXPECT_STREQ("314159 INVITE", msg->header()->get("CSeq").c_str());
    EXPECT_EQ(131, msg->content_length());

    SrsSipMessage smsg;
    HELPER_ASSERT_SUCCESS(smsg.parse(msg));
    EXPECT_EQ(HTTP_RESPONSE, smsg.type_);
    EXPECT_EQ(200, smsg.status_);
    EXPECT_STREQ("SIP/2.0/UDP pc33.atlanta.com;branch=z9hG4bKnashds8;received=192.0.2.1", smsg.via_.c_str());
    EXPECT_STREQ("UDP", smsg.via_transport_.c_str());
    EXPECT_STREQ("pc33.atlanta.com", smsg.via_send_by_.c_str());
    EXPECT_STREQ("pc33.atlanta.com", smsg.via_send_by_address_.c_str());
    EXPECT_EQ(5060, smsg.via_send_by_port_);
    EXPECT_STREQ("branch=z9hG4bKnashds8", smsg.via_branch_.c_str());
    EXPECT_STREQ("Bob <sip:bob@biloxi.com>;tag=a6c85cf", smsg.to_.c_str());
    EXPECT_STREQ("Bob <sip:bob@biloxi.com>", smsg.to_address_.c_str());
    EXPECT_STREQ("tag=a6c85cf", smsg.to_tag_.c_str());
    EXPECT_STREQ("Alice <sip:alice@atlanta.com>;tag=1928301774", smsg.from_.c_str());
    EXPECT_STREQ("Alice <sip:alice@atlanta.com>", smsg.from_address_.c_str());
    EXPECT_STREQ("tag=1928301774", smsg.from_tag_.c_str());
    EXPECT_STREQ("<sip:bob@192.0.2.4>", smsg.contact_.c_str());
    EXPECT_STREQ("bob", smsg.contact_user_.c_str());
    EXPECT_STREQ("192.0.2.4", smsg.contact_host_.c_str());
    EXPECT_STREQ("192.0.2.4", smsg.contact_host_address_.c_str());
    EXPECT_EQ(5060, smsg.contact_host_port_);
    EXPECT_STREQ("a84b4c76e66710", smsg.call_id_.c_str());
    EXPECT_STREQ("314159 INVITE", smsg.cseq_.c_str());
    EXPECT_EQ((uint32_t)314159, smsg.cseq_number_);
    EXPECT_EQ((size_t)131, smsg.body_.length());
}

VOID TEST(ProtocolGbSipTest, SipSessionUacAckRequest)
{
    srs_error_t err = srs_success;

    // See https://www.ietf.org/rfc/rfc3261.html#section-24.2
    MockMSegmentsReader r;
    r.in_bytes.push_back("ACK sip:bob@192.0.2.4 SIP/2.0\r\n");
    r.in_bytes.push_back("Via: SIP/2.0/UDP pc33.atlanta.com;branch=z9hG4bKnashds9\r\n");
    r.in_bytes.push_back("Max-Forwards: 70\r\n");
    r.in_bytes.push_back("To: Bob <sip:bob@biloxi.com>;tag=a6c85cf\r\n");
    r.in_bytes.push_back("From: Alice <sip:alice@atlanta.com>;tag=1928301774\r\n");
    r.in_bytes.push_back("Call-ID: a84b4c76e66710\r\n");
    r.in_bytes.push_back("CSeq: 314159 ACK\r\n");
    r.in_bytes.push_back("Content-Length: 0\r\n");
    r.in_bytes.push_back("\r\n");

    SrsHttpParser p;
    HELPER_ASSERT_SUCCESS(p.initialize(HTTP_BOTH));

    ISrsHttpMessage* msg = NULL;
    SrsAutoFree(ISrsHttpMessage, msg);
    HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));
    EXPECT_EQ(HTTP_ACK, msg->method());
    EXPECT_STREQ("/sip:bob@192.0.2.4", msg->path().c_str());
    EXPECT_STREQ("SIP/2.0/UDP pc33.atlanta.com;branch=z9hG4bKnashds9", msg->header()->get("Via").c_str());
    EXPECT_STREQ("70", msg->header()->get("Max-Forwards").c_str());
    EXPECT_STREQ("Bob <sip:bob@biloxi.com>;tag=a6c85cf", msg->header()->get("To").c_str());
    EXPECT_STREQ("Alice <sip:alice@atlanta.com>;tag=1928301774", msg->header()->get("From").c_str());
    EXPECT_STREQ("a84b4c76e66710", msg->header()->get("Call-ID").c_str());
    EXPECT_STREQ("314159 ACK", msg->header()->get("CSeq").c_str());
    EXPECT_EQ(0, msg->content_length());

    SrsSipMessage smsg;
    HELPER_ASSERT_SUCCESS(smsg.parse(msg));
    EXPECT_EQ(HTTP_REQUEST, smsg.type_);
    EXPECT_EQ(HTTP_ACK, smsg.method_);
    EXPECT_STREQ("sip:bob@192.0.2.4", smsg.request_uri_.c_str());
    EXPECT_STREQ("SIP/2.0/UDP pc33.atlanta.com;branch=z9hG4bKnashds9", smsg.via_.c_str());
    EXPECT_STREQ("UDP", smsg.via_transport_.c_str());
    EXPECT_STREQ("pc33.atlanta.com", smsg.via_send_by_.c_str());
    EXPECT_STREQ("pc33.atlanta.com", smsg.via_send_by_address_.c_str());
    EXPECT_EQ(5060, smsg.via_send_by_port_);
    EXPECT_STREQ("branch=z9hG4bKnashds9", smsg.via_branch_.c_str());
    EXPECT_STREQ("Bob <sip:bob@biloxi.com>;tag=a6c85cf", smsg.to_.c_str());
    EXPECT_STREQ("Bob <sip:bob@biloxi.com>", smsg.to_address_.c_str());
    EXPECT_STREQ("tag=a6c85cf", smsg.to_tag_.c_str());
    EXPECT_STREQ("Alice <sip:alice@atlanta.com>;tag=1928301774", smsg.from_.c_str());
    EXPECT_STREQ("Alice <sip:alice@atlanta.com>", smsg.from_address_.c_str());
    EXPECT_STREQ("tag=1928301774", smsg.from_tag_.c_str());
    EXPECT_STREQ("a84b4c76e66710", smsg.call_id_.c_str());
    EXPECT_STREQ("314159 ACK", smsg.cseq_.c_str());
    EXPECT_EQ((uint32_t)314159, smsg.cseq_number_);
    EXPECT_EQ((uint32_t)70, smsg.max_forwards_);
}

VOID TEST(ProtocolGbSipTest, SipSessionUacByeRequest)
{
    srs_error_t err = srs_success;

    // See https://www.ietf.org/rfc/rfc3261.html#section-24.2
    MockMSegmentsReader r;
    r.in_bytes.push_back("BYE sip:alice@pc33.atlanta.com SIP/2.0\r\n");
    r.in_bytes.push_back("Via: SIP/2.0/UDP 192.0.2.4;branch=z9hG4bKnashds10\r\n");
    r.in_bytes.push_back("Max-Forwards: 70\r\n");
    r.in_bytes.push_back("From: Bob <sip:bob@biloxi.com>;tag=a6c85cf\r\n");
    r.in_bytes.push_back("To: Alice <sip:alice@atlanta.com>;tag=1928301774\r\n");
    r.in_bytes.push_back("Call-ID: a84b4c76e66710\r\n");
    r.in_bytes.push_back("CSeq: 231 BYE\r\n");
    r.in_bytes.push_back("Content-Length: 0\r\n");
    r.in_bytes.push_back("\r\n");

    SrsHttpParser p;
    HELPER_ASSERT_SUCCESS(p.initialize(HTTP_BOTH));

    ISrsHttpMessage* msg = NULL;
    SrsAutoFree(ISrsHttpMessage, msg);
    HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));
    EXPECT_EQ(HTTP_BYE, msg->method());
    EXPECT_STREQ("/sip:alice@pc33.atlanta.com", msg->path().c_str());
    EXPECT_STREQ("SIP/2.0/UDP 192.0.2.4;branch=z9hG4bKnashds10", msg->header()->get("Via").c_str());
    EXPECT_STREQ("70", msg->header()->get("Max-Forwards").c_str());
    EXPECT_STREQ("Alice <sip:alice@atlanta.com>;tag=1928301774", msg->header()->get("To").c_str());
    EXPECT_STREQ("Bob <sip:bob@biloxi.com>;tag=a6c85cf", msg->header()->get("From").c_str());
    EXPECT_STREQ("a84b4c76e66710", msg->header()->get("Call-ID").c_str());
    EXPECT_STREQ("231 BYE", msg->header()->get("CSeq").c_str());
    EXPECT_EQ(0, msg->content_length());

    SrsSipMessage smsg;
    HELPER_ASSERT_SUCCESS(smsg.parse(msg));
    EXPECT_EQ(HTTP_REQUEST, smsg.type_);
    EXPECT_EQ(HTTP_BYE, smsg.method_);
    EXPECT_STREQ("sip:alice@pc33.atlanta.com", smsg.request_uri_.c_str());
    EXPECT_STREQ("SIP/2.0/UDP 192.0.2.4;branch=z9hG4bKnashds10", smsg.via_.c_str());
    EXPECT_STREQ("UDP", smsg.via_transport_.c_str());
    EXPECT_STREQ("192.0.2.4", smsg.via_send_by_.c_str());
    EXPECT_STREQ("192.0.2.4", smsg.via_send_by_address_.c_str());
    EXPECT_EQ(5060, smsg.via_send_by_port_);
    EXPECT_STREQ("branch=z9hG4bKnashds10", smsg.via_branch_.c_str());
    EXPECT_STREQ("Alice <sip:alice@atlanta.com>;tag=1928301774", smsg.to_.c_str());
    EXPECT_STREQ("Alice <sip:alice@atlanta.com>", smsg.to_address_.c_str());
    EXPECT_STREQ("tag=1928301774", smsg.to_tag_.c_str());
    EXPECT_STREQ("Bob <sip:bob@biloxi.com>;tag=a6c85cf", smsg.from_.c_str());
    EXPECT_STREQ("Bob <sip:bob@biloxi.com>", smsg.from_address_.c_str());
    EXPECT_STREQ("tag=a6c85cf", smsg.from_tag_.c_str());
    EXPECT_STREQ("a84b4c76e66710", smsg.call_id_.c_str());
    EXPECT_STREQ("231 BYE", smsg.cseq_.c_str());
    EXPECT_EQ((uint32_t)231, smsg.cseq_number_);
    EXPECT_EQ((uint32_t)70, smsg.max_forwards_);
}

VOID TEST(ProtocolGbSipTest, SipRegisterExpires)
{
    srs_error_t err = srs_success;

    if (true) {
        MockMSegmentsReader r;
        r.in_bytes.push_back("REGISTER sip:registrar.biloxi.com SIP/2.0\r\n");
        r.in_bytes.push_back("Via: SIP/2.0/UDP bobspc.biloxi.com:5060;branch=z9hG4bKnashds7\r\n");
        r.in_bytes.push_back("To: Bob <sip:bob@biloxi.com>\r\n");
        r.in_bytes.push_back("From: Bob <sip:bob@biloxi.com>;tag=456248\r\n");
        r.in_bytes.push_back("Call-ID: 843817637684230@998sdasdh09\r\n");
        r.in_bytes.push_back("CSeq: 1826 REGISTER\r\n");
        r.in_bytes.push_back("Expires: 7200\r\n");
        r.in_bytes.push_back("Content-Length: 0\r\n");
        r.in_bytes.push_back("\r\n");

        SrsHttpParser p;
        HELPER_ASSERT_SUCCESS(p.initialize(HTTP_REQUEST));

        ISrsHttpMessage* msg = NULL;
        SrsAutoFree(ISrsHttpMessage, msg);
        HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));

        SrsSipMessage smsg;
        HELPER_ASSERT_SUCCESS(smsg.parse(msg));
        EXPECT_EQ(HTTP_REQUEST, smsg.type_);
        EXPECT_EQ(HTTP_REGISTER, smsg.method_);
        EXPECT_EQ((uint32_t)7200, smsg.expires_);
    }

    if (true) {
        MockMSegmentsReader r;
        r.in_bytes.push_back("REGISTER sip:registrar.biloxi.com SIP/2.0\r\n");
        r.in_bytes.push_back("Via: SIP/2.0/UDP bobspc.biloxi.com:5060;branch=z9hG4bKnashds7\r\n");
        r.in_bytes.push_back("To: Bob <sip:bob@biloxi.com>\r\n");
        r.in_bytes.push_back("From: Bob <sip:bob@biloxi.com>;tag=456248\r\n");
        r.in_bytes.push_back("Call-ID: 843817637684230@998sdasdh09\r\n");
        r.in_bytes.push_back("CSeq: 1826 REGISTER\r\n");
        r.in_bytes.push_back("Expires: 0\r\n");
        r.in_bytes.push_back("Content-Length: 0\r\n");
        r.in_bytes.push_back("\r\n");

        SrsHttpParser p;
        HELPER_ASSERT_SUCCESS(p.initialize(HTTP_REQUEST));

        ISrsHttpMessage* msg = NULL;
        SrsAutoFree(ISrsHttpMessage, msg);
        HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));

        SrsSipMessage smsg;
        HELPER_ASSERT_SUCCESS(smsg.parse(msg));
        EXPECT_EQ(HTTP_REQUEST, smsg.type_);
        EXPECT_EQ(HTTP_REGISTER, smsg.method_);
        EXPECT_EQ((uint32_t)0, smsg.expires_);
    }
}

VOID TEST(ProtocolGbSipTest, SipSmallMessagesInOneBuffer)
{
    srs_error_t err = srs_success;

    // See https://www.ietf.org/rfc/rfc3261.html#section-24.2
    MockMSegmentsReader r;
    r.in_bytes.push_back(
        "SIP/2.0 100 Trying\r\n"
        "Via: SIP/2.0/TCP 192.168.3.85:5060;rport=5060;branch=z9hG4bK95108j;received=127.0.0.1\r\n"
        "From: <sip:34020000002000000001@3402000000>;tag=SRS6600q9p4\r\n"
        "To: <sip:34020000001772715371@3402000000>\r\n"
        "Call-ID: x220zl3805088272\r\n"
        "CSeq: 831 INVITE\r\n"
        "Content-Length: 0\r\n"
        "\r\n"
        "SIP/2.0 200 OK\r\n"
        "Via: SIP/2.0/TCP 192.168.3.85:5060;rport=5060;branch=z9hG4bK95108j;received=127.0.0.1\r\n"
        "From: <sip:34020000002000000001@3402000000>;tag=SRS6600q9p4\r\n"
        "To: <sip:34020000001772715371@3402000000>\r\n"
        "Call-ID: x220zl3805088272\r\n"
        "CSeq: 831 INVITE\r\n"
        "Content-Length: 0\r\n"
        "\r\n"
    );

    SrsHttpParser p;
    HELPER_ASSERT_SUCCESS(p.initialize(HTTP_BOTH));

    if (true) {
        ISrsHttpMessage* msg = NULL;
        SrsAutoFree(ISrsHttpMessage, msg);
        HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));

        SrsSipMessage smsg;
        HELPER_ASSERT_SUCCESS(smsg.parse(msg));
        EXPECT_EQ(HTTP_RESPONSE, smsg.type_);
        EXPECT_EQ(100, smsg.status_);
    }

    if (true) {
        ISrsHttpMessage* msg = NULL;
        SrsAutoFree(ISrsHttpMessage, msg);
        HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));

        SrsSipMessage smsg;
        HELPER_ASSERT_SUCCESS(smsg.parse(msg));
        EXPECT_EQ(HTTP_RESPONSE, smsg.type_);
        EXPECT_EQ(200, smsg.status_);
    }
}

VOID TEST(ProtocolGbSipTest, SipSmallMessagesWithBody)
{
    srs_error_t err = srs_success;

    // See https://www.ietf.org/rfc/rfc3261.html#section-24.2
    MockMSegmentsReader r;
    r.in_bytes.push_back(
        "SIP/2.0 100 Trying\r\n"
        "Via: SIP/2.0/TCP 192.168.3.85:5060;rport=5060;branch=z9hG4bK95108j;received=127.0.0.1\r\n"
        "From: <sip:34020000002000000001@3402000000>;tag=SRS6600q9p4\r\n"
        "To: <sip:34020000001772715371@3402000000>\r\n"
        "Call-ID: x220zl3805088272\r\n"
        "CSeq: 831 INVITE\r\n"
        "Content-Length: 10\r\n"
        "\r\n"
        "HelloWorld"
        "SIP/2.0 200 OK\r\n"
        "Via: SIP/2.0/TCP 192.168.3.85:5060;rport=5060;branch=z9hG4bK95108j;received=127.0.0.1\r\n"
        "From: <sip:34020000002000000001@3402000000>;tag=SRS6600q9p4\r\n"
        "To: <sip:34020000001772715371@3402000000>\r\n"
        "Call-ID: x220zl3805088272\r\n"
        "CSeq: 831 INVITE\r\n"
        "Content-Length: 11\r\n"
        "\r\n"
        "HelloServer"
    );

    SrsHttpParser p;
    HELPER_ASSERT_SUCCESS(p.initialize(HTTP_BOTH));

    if (true) {
        ISrsHttpMessage* msg = NULL;
        SrsAutoFree(ISrsHttpMessage, msg);
        HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));

        SrsSipMessage smsg;
        HELPER_ASSERT_SUCCESS(smsg.parse(msg));
        EXPECT_EQ(HTTP_RESPONSE, smsg.type_);
        EXPECT_EQ(100, smsg.status_);
        EXPECT_STREQ("HelloWorld", smsg.body_.c_str());
    }

    if (true) {
        ISrsHttpMessage* msg = NULL;
        SrsAutoFree(ISrsHttpMessage, msg);
        HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));

        SrsSipMessage smsg;
        HELPER_ASSERT_SUCCESS(smsg.parse(msg));
        EXPECT_EQ(HTTP_RESPONSE, smsg.type_);
        EXPECT_EQ(200, smsg.status_);
        EXPECT_STREQ("HelloServer", smsg.body_.c_str());
    }
}

VOID TEST(ProtocolGbSipTest, SipStandardOfferDecode)
{
    srs_error_t err = srs_success;

    string str = \
        "v=0\r\n" \
        "o=64010600002020000001 0 0 IN IP4 172.20.16.3\r\n" \
        "s=Play\r\n" \
        "c=IN IP4 172.20.16.3\r\n" \
        "t=0 0\r\n" \
        "m=video 6000 RTP/AVP 96 98 97\r\n" \
        "a=recvonly\r\n" \
        "a=rtpmap:96 PS/90000\r\n" \
        "a=rtpmap:98 H264/90000\r\n" \
        "a=rtpmap:97 MPEG4/90000\r\n";
    SrsSdp o;
    HELPER_ASSERT_SUCCESS(o.parse(str));
    EXPECT_STREQ("0", o.version_.c_str());
    EXPECT_STREQ("64010600002020000001", o.username_.c_str());
    EXPECT_STREQ("0", o.session_id_.c_str());
    EXPECT_STREQ("0", o.session_version_.c_str());
    EXPECT_STREQ("IN", o.nettype_.c_str());
    EXPECT_STREQ("IP4", o.addrtype_.c_str());
    EXPECT_STREQ("172.20.16.3", o.unicast_address_.c_str());
    EXPECT_STREQ("Play", o.session_name_.c_str());
    EXPECT_EQ(0, o.start_time_);
    EXPECT_EQ(0, o.end_time_);

    ASSERT_EQ((size_t)1, o.media_descs_.size());
    const SrsMediaDesc& m = o.media_descs_.at(0);
    EXPECT_STREQ("video", m.type_.c_str());
    EXPECT_EQ(6000, m.port_);
    EXPECT_STREQ("RTP/AVP", m.protos_.c_str());
    EXPECT_EQ((size_t)3, m.payload_types_.size());

    const SrsMediaPayloadType& ps = m.payload_types_.at(0);
    EXPECT_EQ(96, ps.payload_type_);
    EXPECT_STREQ("PS", ps.encoding_name_.c_str());
    EXPECT_EQ(90000, ps.clock_rate_);

    const SrsMediaPayloadType& h264 = m.payload_types_.at(1);
    EXPECT_EQ(98, h264.payload_type_);
    EXPECT_STREQ("H264", h264.encoding_name_.c_str());
    EXPECT_EQ(90000, h264.clock_rate_);

    const SrsMediaPayloadType& mpeg4 = m.payload_types_.at(2);
    EXPECT_EQ(97, mpeg4.payload_type_);
    EXPECT_STREQ("MPEG4", mpeg4.encoding_name_.c_str());
    EXPECT_EQ(90000, mpeg4.clock_rate_);
}

VOID TEST(ProtocolGbSipTest, SipStandardOfferEncode)
{
    srs_error_t err = srs_success;

    SrsSdp o;
    o.version_ = "0";
    o.username_ = "64010600002020000001";
    o.session_id_ = "0";
    o.session_version_ = "0";
    o.nettype_ = "IN";
    o.addrtype_ = "IP4";
    o.unicast_address_ = "172.20.16.3";
    o.session_name_ = "Play";
    o.start_time_ = 0;
    o.end_time_ = 0;
    o.ice_lite_ = ""; // Disable this line.
    o.connection_ = "c=IN IP4 172.20.16.3"; // Session level connection.

    o.media_descs_.push_back(SrsMediaDesc("video"));
    SrsMediaDesc& m = o.media_descs_.at(0);
    m.port_ = 6000;
    m.protos_ = "RTP/AVP";
    m.connection_ = ""; // Disable media level connection.
    m.recvonly_ = true;

    m.payload_types_.push_back(SrsMediaPayloadType(96));
    SrsMediaPayloadType& ps = m.payload_types_.at(0);
    ps.encoding_name_ = "PS";
    ps.clock_rate_ = 90000;

    m.payload_types_.push_back(SrsMediaPayloadType(98));
    SrsMediaPayloadType& h264 = m.payload_types_.at(1);
    h264.encoding_name_ = "H264";
    h264.clock_rate_ = 90000;

    m.payload_types_.push_back(SrsMediaPayloadType(97));
    SrsMediaPayloadType& mpeg4 = m.payload_types_.at(2);
    mpeg4.encoding_name_ = "MPEG4";
    mpeg4.clock_rate_ = 90000;

    ostringstream os;
    HELPER_ASSERT_SUCCESS(o.encode(os));
    string ostr = os.str();
    string str = \
        "v=0\r\n" \
        "o=64010600002020000001 0 0 IN IP4 172.20.16.3\r\n" \
        "s=Play\r\n" \
        "c=IN IP4 172.20.16.3\r\n" \
        "t=0 0\r\n" \
        "m=video 6000 RTP/AVP 96 98 97\r\n" \
        "a=recvonly\r\n" \
        "a=rtpmap:96 PS/90000\r\n" \
        "a=rtpmap:98 H264/90000\r\n" \
        "a=rtpmap:97 MPEG4/90000\r\n";
    EXPECT_STREQ(ostr.c_str(), str.c_str());
}

VOID TEST(ProtocolGbSipTest, SipGb28181OfferDecode)
{
    srs_error_t err = srs_success;

    string str = \
        "v=0\r\n" \
        "o=64010600002020000001 0 0 IN IP4 172.20.16.3\r\n" \
        "s=Play\r\n" \
        "c=IN IP4 172.20.16.3\r\n" \
        "t=0 0\r\n" \
        "m=video 6000 RTP/AVP 96 98 97\r\n" \
        "a=recvonly\r\n" \
        "a=rtpmap:96 PS/90000\r\n" \
        "a=rtpmap:98 H264/90000\r\n" \
        "a=rtpmap:97 MPEG4/90000\r\n" \
        "y=0100008888\r\n";
    SrsSdp o;
    HELPER_ASSERT_SUCCESS(o.parse(str));
    EXPECT_STREQ("0", o.version_.c_str());
    EXPECT_STREQ("64010600002020000001", o.username_.c_str());
    EXPECT_STREQ("0", o.session_id_.c_str());
    EXPECT_STREQ("0", o.session_version_.c_str());
    EXPECT_STREQ("IN", o.nettype_.c_str());
    EXPECT_STREQ("IP4", o.addrtype_.c_str());
    EXPECT_STREQ("172.20.16.3", o.unicast_address_.c_str());
    EXPECT_STREQ("Play", o.session_name_.c_str());
    EXPECT_EQ(0, o.start_time_);
    EXPECT_EQ(0, o.end_time_);

    ASSERT_EQ((size_t)1, o.media_descs_.size());
    const SrsMediaDesc& m = o.media_descs_.at(0);
    EXPECT_STREQ("video", m.type_.c_str());
    EXPECT_EQ(6000, m.port_);
    EXPECT_STREQ("RTP/AVP", m.protos_.c_str());
    EXPECT_EQ((size_t)3, m.payload_types_.size());
    ASSERT_EQ((size_t)1, m.ssrc_infos_.size());

    const SrsSSRCInfo& ssrc = m.ssrc_infos_.at(0);
    EXPECT_EQ((uint32_t)100008888, ssrc.ssrc_);
    EXPECT_STREQ("0100008888", ssrc.cname_.c_str());
    EXPECT_STREQ("gb28181", ssrc.label_.c_str());

    const SrsMediaPayloadType& ps = m.payload_types_.at(0);
    EXPECT_EQ(96, ps.payload_type_);
    EXPECT_STREQ("PS", ps.encoding_name_.c_str());
    EXPECT_EQ(90000, ps.clock_rate_);

    const SrsMediaPayloadType& h264 = m.payload_types_.at(1);
    EXPECT_EQ(98, h264.payload_type_);
    EXPECT_STREQ("H264", h264.encoding_name_.c_str());
    EXPECT_EQ(90000, h264.clock_rate_);

    const SrsMediaPayloadType& mpeg4 = m.payload_types_.at(2);
    EXPECT_EQ(97, mpeg4.payload_type_);
    EXPECT_STREQ("MPEG4", mpeg4.encoding_name_.c_str());
    EXPECT_EQ(90000, mpeg4.clock_rate_);
}

VOID TEST(ProtocolGbSipTest, SipGb28181OfferEncode)
{
    srs_error_t err = srs_success;

    SrsSdp o;
    o.version_ = "0";
    o.username_ = "64010600002020000001";
    o.session_id_ = "0";
    o.session_version_ = "0";
    o.nettype_ = "IN";
    o.addrtype_ = "IP4";
    o.unicast_address_ = "172.20.16.3";
    o.session_name_ = "Play";
    o.start_time_ = 0;
    o.end_time_ = 0;
    o.ice_lite_ = ""; // Disable this line.
    o.connection_ = "c=IN IP4 172.20.16.3"; // Session level connection.

    o.media_descs_.push_back(SrsMediaDesc("video"));
    SrsMediaDesc& m = o.media_descs_.at(0);
    m.port_ = 6000;
    m.protos_ = "RTP/AVP";
    m.connection_ = ""; // Disable media level connection.
    m.recvonly_ = true;

    m.payload_types_.push_back(SrsMediaPayloadType(96));
    SrsMediaPayloadType& ps = m.payload_types_.at(0);
    ps.encoding_name_ = "PS";
    ps.clock_rate_ = 90000;

    m.payload_types_.push_back(SrsMediaPayloadType(98));
    SrsMediaPayloadType& h264 = m.payload_types_.at(1);
    h264.encoding_name_ = "H264";
    h264.clock_rate_ = 90000;

    m.payload_types_.push_back(SrsMediaPayloadType(97));
    SrsMediaPayloadType& mpeg4 = m.payload_types_.at(2);
    mpeg4.encoding_name_ = "MPEG4";
    mpeg4.clock_rate_ = 90000;

    m.ssrc_infos_.push_back(SrsSSRCInfo());
    SrsSSRCInfo& ssrc = m.ssrc_infos_.at(0);
    ssrc.ssrc_ = 100008888;
    ssrc.cname_ = "0100008888";
    ssrc.label_ = "gb28181";

    ostringstream os;
    HELPER_ASSERT_SUCCESS(o.encode(os));
    string ostr = os.str();
    string str = \
        "v=0\r\n" \
        "o=64010600002020000001 0 0 IN IP4 172.20.16.3\r\n" \
        "s=Play\r\n" \
        "c=IN IP4 172.20.16.3\r\n" \
        "t=0 0\r\n" \
        "m=video 6000 RTP/AVP 96 98 97\r\n" \
        "a=recvonly\r\n" \
        "a=rtpmap:96 PS/90000\r\n" \
        "a=rtpmap:98 H264/90000\r\n" \
        "a=rtpmap:97 MPEG4/90000\r\n" \
        "y=0100008888\r\n";
    EXPECT_STREQ(ostr.c_str(), str.c_str());
}

VOID TEST(ProtocolGbSipTest, GbRegisterRequest)
{
    srs_error_t err = srs_success;

    // See https://www.ietf.org/rfc/rfc3261.html#section-24.1
    MockMSegmentsReader r;
    r.in_bytes.push_back("REGISTER sip:34020000002000000001@3402000000 SIP/2.0\r\n");
    r.in_bytes.push_back("Via: SIP/2.0/UDP 192.168.3.99:5060;rport;branch=z9hG4bK442003727\r\n");
    r.in_bytes.push_back("From: <sip:34020000001320000001@3402000000>;tag=307202390\r\n");
    r.in_bytes.push_back("To: <sip:34020000001320000001@3402000000>\r\n");
    r.in_bytes.push_back("Call-ID: 393945367\r\n");
    r.in_bytes.push_back("CSeq: 1 REGISTER\r\n");
    r.in_bytes.push_back("Contact: <sip:34020000001320000001@192.168.3.99:5060>\r\n");
    r.in_bytes.push_back("Max-Forwards: 70\r\n");
    r.in_bytes.push_back("User-Agent: IP Camera\r\n");
    r.in_bytes.push_back("Expires: 3600\r\n");
    r.in_bytes.push_back("Content-Length: 0\r\n");
    r.in_bytes.push_back("\r\n");

    SrsHttpParser p;
    HELPER_ASSERT_SUCCESS(p.initialize(HTTP_REQUEST));

    ISrsHttpMessage* msg = NULL;
    SrsAutoFree(ISrsHttpMessage, msg);
    HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));
    EXPECT_EQ(HTTP_REGISTER, msg->method());
    EXPECT_EQ(0, msg->content_length());

    SrsSipMessage smsg;
    HELPER_ASSERT_SUCCESS(smsg.parse(msg));
    EXPECT_EQ(HTTP_REQUEST, smsg.type_);
    EXPECT_EQ(HTTP_REGISTER, smsg.method_);
    EXPECT_STREQ("sip:34020000002000000001@3402000000", smsg.request_uri_.c_str());
    EXPECT_STREQ("SIP/2.0/UDP 192.168.3.99:5060;rport;branch=z9hG4bK442003727", smsg.via_.c_str());
    EXPECT_STREQ("UDP", smsg.via_transport_.c_str());
    EXPECT_STREQ("192.168.3.99:5060", smsg.via_send_by_.c_str());
    EXPECT_STREQ("192.168.3.99", smsg.via_send_by_address_.c_str());
    EXPECT_EQ(5060, smsg.via_send_by_port_);
    EXPECT_STREQ("branch=z9hG4bK442003727", smsg.via_branch_.c_str());
    EXPECT_STREQ("<sip:34020000001320000001@3402000000>;tag=307202390", smsg.from_.c_str());
    EXPECT_STREQ("<sip:34020000001320000001@3402000000>", smsg.from_address_.c_str());
    EXPECT_STREQ("34020000001320000001", smsg.from_address_user_.c_str());
    EXPECT_STREQ("3402000000", smsg.from_address_host_.c_str());
    EXPECT_STREQ("tag=307202390", smsg.from_tag_.c_str());
    EXPECT_STREQ("<sip:34020000001320000001@3402000000>", smsg.to_.c_str());
    EXPECT_STREQ("<sip:34020000001320000001@3402000000>", smsg.to_address_.c_str());
    EXPECT_STREQ("34020000001320000001", smsg.to_address_user_.c_str());
    EXPECT_STREQ("3402000000", smsg.to_address_host_.c_str());
    EXPECT_STREQ("", smsg.to_tag_.c_str());
    EXPECT_STREQ("393945367", smsg.call_id_.c_str());
    EXPECT_STREQ("1 REGISTER", smsg.cseq_.c_str());
    EXPECT_EQ((uint32_t)1, smsg.cseq_number_);
    EXPECT_STREQ("<sip:34020000001320000001@192.168.3.99:5060>", smsg.contact_.c_str());
    EXPECT_STREQ("34020000001320000001", smsg.contact_user_.c_str());
    EXPECT_STREQ("192.168.3.99:5060", smsg.contact_host_.c_str());
    EXPECT_STREQ("192.168.3.99", smsg.contact_host_address_.c_str());
    EXPECT_EQ(5060, smsg.contact_host_port_);
    EXPECT_EQ((uint32_t)70, smsg.max_forwards_);
    EXPECT_EQ((uint32_t)3600, smsg.expires_);

    EXPECT_STREQ(smsg.from_address_.c_str(), smsg.to_address_.c_str());
}

VOID TEST(ProtocolGbSipTest, GbRegisterResponse)
{
    srs_error_t err = srs_success;

    // See https://www.ietf.org/rfc/rfc3261.html#section-24.1
    MockMSegmentsReader r;
    r.in_bytes.push_back("SIP/2.0 200 OK\r\n");
    r.in_bytes.push_back("Via: SIP/2.0/UDP 192.168.3.99:5060;rport;branch=z9hG4bK442003727\r\n");
    r.in_bytes.push_back("From: <sip:34020000001320000001@3402000000>;tag=307202390\r\n");
    r.in_bytes.push_back("To: <sip:34020000001320000001@3402000000>\r\n");
    r.in_bytes.push_back("Call-ID: 393945367\r\n");
    r.in_bytes.push_back("CSeq: 1 REGISTER\r\n");
    r.in_bytes.push_back("Contact: <sip:34020000001320000001@192.168.3.99:5060>\r\n");
    r.in_bytes.push_back("User-Agent: SRS/5.0.65(Bee)\r\n");
    r.in_bytes.push_back("Expires: 3600\r\n");
    r.in_bytes.push_back("Content-Length: 0\r\n");
    r.in_bytes.push_back("\r\n");

    SrsHttpParser p;
    HELPER_ASSERT_SUCCESS(p.initialize(HTTP_RESPONSE));

    ISrsHttpMessage* msg = NULL;
    SrsAutoFree(ISrsHttpMessage, msg);
    HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));
    EXPECT_EQ(HTTP_RESPONSE, (http_parser_type) msg->message_type());
    EXPECT_EQ(200, msg->status_code());
    EXPECT_EQ(0, msg->content_length());

    SrsSipMessage smsg;
    HELPER_ASSERT_SUCCESS(smsg.parse(msg));
    EXPECT_EQ(HTTP_RESPONSE, smsg.type_);
    EXPECT_EQ(HTTP_STATUS_OK, smsg.status_);
    EXPECT_STREQ("SIP/2.0/UDP 192.168.3.99:5060;rport;branch=z9hG4bK442003727", smsg.via_.c_str());
    EXPECT_STREQ("UDP", smsg.via_transport_.c_str());
    EXPECT_STREQ("192.168.3.99:5060", smsg.via_send_by_.c_str());
    EXPECT_STREQ("192.168.3.99", smsg.via_send_by_address_.c_str());
    EXPECT_EQ(5060, smsg.via_send_by_port_);
    EXPECT_STREQ("branch=z9hG4bK442003727", smsg.via_branch_.c_str());
    EXPECT_STREQ("<sip:34020000001320000001@3402000000>;tag=307202390", smsg.from_.c_str());
    EXPECT_STREQ("<sip:34020000001320000001@3402000000>", smsg.from_address_.c_str());
    EXPECT_STREQ("34020000001320000001", smsg.from_address_user_.c_str());
    EXPECT_STREQ("3402000000", smsg.from_address_host_.c_str());
    EXPECT_STREQ("tag=307202390", smsg.from_tag_.c_str());
    EXPECT_STREQ("<sip:34020000001320000001@3402000000>", smsg.to_.c_str());
    EXPECT_STREQ("<sip:34020000001320000001@3402000000>", smsg.to_address_.c_str());
    EXPECT_STREQ("34020000001320000001", smsg.to_address_user_.c_str());
    EXPECT_STREQ("3402000000", smsg.to_address_host_.c_str());
    EXPECT_STREQ("", smsg.to_tag_.c_str());
    EXPECT_STREQ("393945367", smsg.call_id_.c_str());
    EXPECT_STREQ("1 REGISTER", smsg.cseq_.c_str());
    EXPECT_EQ((uint32_t)1, smsg.cseq_number_);
    EXPECT_STREQ("<sip:34020000001320000001@192.168.3.99:5060>", smsg.contact_.c_str());
    EXPECT_STREQ("34020000001320000001", smsg.contact_user_.c_str());
    EXPECT_STREQ("192.168.3.99:5060", smsg.contact_host_.c_str());
    EXPECT_STREQ("192.168.3.99", smsg.contact_host_address_.c_str());
    EXPECT_EQ(5060, smsg.contact_host_port_);
    EXPECT_EQ((uint32_t)3600, smsg.expires_);
}

VOID TEST(ProtocolGbSipTest, GbInviteRequest)
{
    srs_error_t err = srs_success;

    string sdp =
        "v=0\r\n"
        "o=34020000001320000001 0 0 IN IP4 192.168.3.82\r\n"
        "s=Play\r\n"
        "c=IN IP4 192.168.3.82\r\n"
        "t=0 0\r\n"
        "m=video 9000 TCP/RTP/AVP 96\r\n"
        "a=recvonly\r\n"
        "a=rtpmap:96 PS/90000\r\n"
        "y=0200007585\r\n";

    MockMSegmentsReader r;
    r.in_bytes.push_back("INVITE sip:34020000001320000001@3402000000 SIP/2.0\r\n");
    r.in_bytes.push_back("Via: SIP/2.0/TCP 192.168.3.82:5060;rport;branch=z9hG4bK0l31rx\r\n");
    r.in_bytes.push_back("From: <sip:34020000002000000001@3402000000>;tag=SRSk1er282t\r\n");
    r.in_bytes.push_back("To: <sip:34020000001320000001@3402000000>\r\n");
    r.in_bytes.push_back("CSeq: 854 INVITE\r\n");
    r.in_bytes.push_back("Call-ID: 854k7337207yxpfj\r\n");
    r.in_bytes.push_back("Content-Type: application/sdp\r\n");
    r.in_bytes.push_back("Contact: <sip:34020000002000000001@3402000000>\r\n");
    r.in_bytes.push_back("Max-Forwards: 70\r\n");
    r.in_bytes.push_back("Subject: 34020000001320000001:0200007585,34020000002000000001:0\r\n");
    r.in_bytes.push_back("Server: SRS/5.0.65(Bee)\r\n");
    r.in_bytes.push_back(srs_fmt("Content-Length: %d\r\n", sdp.length()));
    r.in_bytes.push_back("\r\n");
    r.in_bytes.push_back(sdp);

    SrsHttpParser p;
    HELPER_ASSERT_SUCCESS(p.initialize(HTTP_BOTH));

    ISrsHttpMessage* msg = NULL;
    SrsAutoFree(ISrsHttpMessage, msg);
    HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));

    SrsSipMessage smsg;
    HELPER_ASSERT_SUCCESS(smsg.parse(msg));
    EXPECT_EQ(HTTP_REQUEST, smsg.type_);
    EXPECT_EQ(HTTP_INVITE, smsg.method_);
    EXPECT_STREQ("sip:34020000001320000001@3402000000", smsg.request_uri_.c_str());
    EXPECT_STREQ("SIP/2.0/TCP 192.168.3.82:5060;rport;branch=z9hG4bK0l31rx", smsg.via_.c_str());
    EXPECT_STREQ("TCP", smsg.via_transport_.c_str());
    EXPECT_STREQ("192.168.3.82:5060", smsg.via_send_by_.c_str());
    EXPECT_STREQ("192.168.3.82", smsg.via_send_by_address_.c_str());
    EXPECT_EQ(5060, smsg.via_send_by_port_);
    EXPECT_STREQ("branch=z9hG4bK0l31rx", smsg.via_branch_.c_str());
    EXPECT_STREQ("<sip:34020000002000000001@3402000000>;tag=SRSk1er282t", smsg.from_.c_str());
    EXPECT_STREQ("<sip:34020000002000000001@3402000000>", smsg.from_address_.c_str());
    EXPECT_STREQ("34020000002000000001", smsg.from_address_user_.c_str());
    EXPECT_STREQ("3402000000", smsg.from_address_host_.c_str());
    EXPECT_STREQ("tag=SRSk1er282t", smsg.from_tag_.c_str());
    EXPECT_STREQ("<sip:34020000001320000001@3402000000>", smsg.to_.c_str());
    EXPECT_STREQ("<sip:34020000001320000001@3402000000>", smsg.to_address_.c_str());
    EXPECT_STREQ("34020000001320000001", smsg.to_address_user_.c_str());
    EXPECT_STREQ("3402000000", smsg.to_address_host_.c_str());
    EXPECT_STREQ("", smsg.to_tag_.c_str());
    EXPECT_STREQ("854 INVITE", smsg.cseq_.c_str());
    EXPECT_EQ((uint32_t)854, smsg.cseq_number_);
    EXPECT_STREQ("854k7337207yxpfj", smsg.call_id_.c_str());
    EXPECT_STREQ("<sip:34020000002000000001@3402000000>", smsg.contact_.c_str());
    EXPECT_STREQ("34020000002000000001", smsg.contact_user_.c_str());
    EXPECT_STREQ("3402000000", smsg.contact_host_.c_str());
    EXPECT_STREQ("3402000000", smsg.contact_host_address_.c_str());
    EXPECT_EQ(5060, smsg.contact_host_port_);
    EXPECT_EQ((uint32_t)70, smsg.max_forwards_);
    EXPECT_STREQ("34020000001320000001:0200007585,34020000002000000001:0", smsg.subject_.c_str());
    EXPECT_STREQ(sdp.c_str(), smsg.body_.c_str());
}

VOID TEST(ProtocolGbSipTest, GbTringResponse)
{
    srs_error_t err = srs_success;

    // See https://www.ietf.org/rfc/rfc3261.html#section-24.1
    MockMSegmentsReader r;
    r.in_bytes.push_back("SIP/2.0 100 Trying\r\n");
    r.in_bytes.push_back("Via: SIP/2.0/TCP 192.168.3.82:5060;rport=5060;branch=z9hG4bK0l31rx\r\n");
    r.in_bytes.push_back("From: <sip:34020000002000000001@3402000000>;tag=SRSk1er282t\r\n");
    r.in_bytes.push_back("To: <sip:34020000001320000001@3402000000>\r\n");
    r.in_bytes.push_back("Call-ID: 854k7337207yxpfj\r\n");
    r.in_bytes.push_back("CSeq: 854 INVITE\r\n");
    r.in_bytes.push_back("User-Agent: IP Camera\r\n");
    r.in_bytes.push_back("Content-Length: 0\r\n");
    r.in_bytes.push_back("\r\n");

    SrsHttpParser p;
    HELPER_ASSERT_SUCCESS(p.initialize(HTTP_RESPONSE));

    ISrsHttpMessage* msg = NULL;
    SrsAutoFree(ISrsHttpMessage, msg);
    HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));

    SrsSipMessage smsg;
    HELPER_ASSERT_SUCCESS(smsg.parse(msg));
    EXPECT_EQ(HTTP_RESPONSE, smsg.type_);
    EXPECT_EQ(100, smsg.status_);
    EXPECT_STREQ("SIP/2.0/TCP 192.168.3.82:5060;rport=5060;branch=z9hG4bK0l31rx", smsg.via_.c_str());
    EXPECT_STREQ("TCP", smsg.via_transport_.c_str());
    EXPECT_STREQ("192.168.3.82:5060", smsg.via_send_by_.c_str());
    EXPECT_STREQ("192.168.3.82", smsg.via_send_by_address_.c_str());
    EXPECT_EQ(5060, smsg.via_send_by_port_);
    EXPECT_STREQ("branch=z9hG4bK0l31rx", smsg.via_branch_.c_str());
    EXPECT_STREQ("<sip:34020000002000000001@3402000000>;tag=SRSk1er282t", smsg.from_.c_str());
    EXPECT_STREQ("<sip:34020000002000000001@3402000000>", smsg.from_address_.c_str());
    EXPECT_STREQ("34020000002000000001", smsg.from_address_user_.c_str());
    EXPECT_STREQ("3402000000", smsg.from_address_host_.c_str());
    EXPECT_STREQ("tag=SRSk1er282t", smsg.from_tag_.c_str());
    EXPECT_STREQ("<sip:34020000001320000001@3402000000>", smsg.to_.c_str());
    EXPECT_STREQ("<sip:34020000001320000001@3402000000>", smsg.to_address_.c_str());
    EXPECT_STREQ("34020000001320000001", smsg.to_address_user_.c_str());
    EXPECT_STREQ("3402000000", smsg.to_address_host_.c_str());
    EXPECT_STREQ("", smsg.to_tag_.c_str());
    EXPECT_STREQ("854 INVITE", smsg.cseq_.c_str());
    EXPECT_EQ((uint32_t)854, smsg.cseq_number_);
    EXPECT_STREQ("854k7337207yxpfj", smsg.call_id_.c_str());
    EXPECT_EQ((size_t)0, smsg.body_.length());
}

VOID TEST(ProtocolGbSipTest, Gb200OkResponse)
{
    srs_error_t err = srs_success;

    string sdp =
        "v=0\r\n"
        "o=34020000001320000001 1941 1941 IN IP4 192.168.3.99\r\n"
        "s=Play\r\n"
        "c=IN IP4 192.168.3.99\r\n"
        "t=0 0\r\n"
        "m=video 15060 TCP/RTP/AVP 96\r\n"
        "a=setup:active\r\n"
        "a=sendonly\r\n"
        "a=rtpmap:96 PS/90000\r\n"
        "a=filesize:0\r\n"
        "y=0200007585\r\n";

    // See https://www.ietf.org/rfc/rfc3261.html#section-24.1
    MockMSegmentsReader r;
    r.in_bytes.push_back("SIP/2.0 200 OK\r\n");
    r.in_bytes.push_back("Via: SIP/2.0/TCP 192.168.3.82:5060;rport=5060;branch=z9hG4bK0l31rx\r\n");
    r.in_bytes.push_back("From: <sip:34020000002000000001@3402000000>;tag=SRSk1er282t\r\n");
    r.in_bytes.push_back("To: <sip:34020000001320000001@3402000000>\r\n");
    r.in_bytes.push_back("Call-ID: 854k7337207yxpfj\r\n");
    r.in_bytes.push_back("CSeq: 854 INVITE\r\n");
    r.in_bytes.push_back("Contact: <sip:34020000001320000001@192.168.3.99:5060>\r\n");
    r.in_bytes.push_back("Content-Type: application/sdp\r\n");
    r.in_bytes.push_back("User-Agent: IP Camera\r\n");
    r.in_bytes.push_back(srs_fmt("Content-Length: %d\r\n", sdp.length()));
    r.in_bytes.push_back("\r\n");
    r.in_bytes.push_back(sdp);

    SrsHttpParser p;
    HELPER_ASSERT_SUCCESS(p.initialize(HTTP_RESPONSE));

    ISrsHttpMessage* msg = NULL;
    SrsAutoFree(ISrsHttpMessage, msg);
    HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));

    SrsSipMessage smsg;
    HELPER_ASSERT_SUCCESS(smsg.parse(msg));
    EXPECT_EQ(HTTP_RESPONSE, smsg.type_);
    EXPECT_EQ(200, smsg.status_);
    EXPECT_STREQ("SIP/2.0/TCP 192.168.3.82:5060;rport=5060;branch=z9hG4bK0l31rx", smsg.via_.c_str());
    EXPECT_STREQ("TCP", smsg.via_transport_.c_str());
    EXPECT_STREQ("192.168.3.82:5060", smsg.via_send_by_.c_str());
    EXPECT_STREQ("192.168.3.82", smsg.via_send_by_address_.c_str());
    EXPECT_EQ(5060, smsg.via_send_by_port_);
    EXPECT_STREQ("branch=z9hG4bK0l31rx", smsg.via_branch_.c_str());
    EXPECT_STREQ("<sip:34020000002000000001@3402000000>;tag=SRSk1er282t", smsg.from_.c_str());
    EXPECT_STREQ("<sip:34020000002000000001@3402000000>", smsg.from_address_.c_str());
    EXPECT_STREQ("34020000002000000001", smsg.from_address_user_.c_str());
    EXPECT_STREQ("3402000000", smsg.from_address_host_.c_str());
    EXPECT_STREQ("tag=SRSk1er282t", smsg.from_tag_.c_str());
    EXPECT_STREQ("<sip:34020000001320000001@3402000000>", smsg.to_.c_str());
    EXPECT_STREQ("<sip:34020000001320000001@3402000000>", smsg.to_address_.c_str());
    EXPECT_STREQ("34020000001320000001", smsg.to_address_user_.c_str());
    EXPECT_STREQ("3402000000", smsg.to_address_host_.c_str());
    EXPECT_STREQ("", smsg.to_tag_.c_str());
    EXPECT_STREQ("854 INVITE", smsg.cseq_.c_str());
    EXPECT_EQ((uint32_t)854, smsg.cseq_number_);
    EXPECT_STREQ("854k7337207yxpfj", smsg.call_id_.c_str());
    EXPECT_STREQ("<sip:34020000001320000001@192.168.3.99:5060>", smsg.contact_.c_str());
    EXPECT_STREQ("34020000001320000001", smsg.contact_user_.c_str());
    EXPECT_STREQ("192.168.3.99:5060", smsg.contact_host_.c_str());
    EXPECT_STREQ("192.168.3.99", smsg.contact_host_address_.c_str());
    EXPECT_EQ(5060, smsg.contact_host_port_);
    EXPECT_STREQ(sdp.c_str(), smsg.body_.c_str());
}

VOID TEST(ProtocolGbSipTest, GbAckRequest)
{
    srs_error_t err = srs_success;

    MockMSegmentsReader r;
    r.in_bytes.push_back("ACK sip:34020000001320000001@3402000000 SIP/2.0\r\n");
    r.in_bytes.push_back("Via: SIP/2.0/TCP 192.168.3.82:5060;rport;branch=z9hG4bK9k30b6\r\n");
    r.in_bytes.push_back("From: <sip:34020000002000000001@3402000000>;tag=SRSk1er282t\r\n");
    r.in_bytes.push_back("To: <sip:34020000001320000001@3402000000>\r\n");
    r.in_bytes.push_back("CSeq: 854 ACK\r\n");
    r.in_bytes.push_back("Call-ID: 854k7337207yxpfj\r\n");
    r.in_bytes.push_back("Max-Forwards: 70\r\n");
    r.in_bytes.push_back("Server: SRS/5.0.65(Bee)\r\n");
    r.in_bytes.push_back("Content-Length: 0\r\n");
    r.in_bytes.push_back("\r\n");

    SrsHttpParser p;
    HELPER_ASSERT_SUCCESS(p.initialize(HTTP_BOTH));

    ISrsHttpMessage* msg = NULL;
    SrsAutoFree(ISrsHttpMessage, msg);
    HELPER_ASSERT_SUCCESS(p.parse_message(&r, &msg));

    SrsSipMessage smsg;
    HELPER_ASSERT_SUCCESS(smsg.parse(msg));
    EXPECT_EQ(HTTP_REQUEST, smsg.type_);
    EXPECT_EQ(HTTP_ACK, smsg.method_);
    EXPECT_STREQ("sip:34020000001320000001@3402000000", smsg.request_uri_.c_str());
    EXPECT_STREQ("SIP/2.0/TCP 192.168.3.82:5060;rport;branch=z9hG4bK9k30b6", smsg.via_.c_str());
    EXPECT_STREQ("TCP", smsg.via_transport_.c_str());
    EXPECT_STREQ("192.168.3.82:5060", smsg.via_send_by_.c_str());
    EXPECT_STREQ("192.168.3.82", smsg.via_send_by_address_.c_str());
    EXPECT_EQ(5060, smsg.via_send_by_port_);
    EXPECT_STREQ("branch=z9hG4bK9k30b6", smsg.via_branch_.c_str());
    EXPECT_STREQ("<sip:34020000002000000001@3402000000>;tag=SRSk1er282t", smsg.from_.c_str());
    EXPECT_STREQ("<sip:34020000002000000001@3402000000>", smsg.from_address_.c_str());
    EXPECT_STREQ("34020000002000000001", smsg.from_address_user_.c_str());
    EXPECT_STREQ("3402000000", smsg.from_address_host_.c_str());
    EXPECT_STREQ("tag=SRSk1er282t", smsg.from_tag_.c_str());
    EXPECT_STREQ("<sip:34020000001320000001@3402000000>", smsg.to_.c_str());
    EXPECT_STREQ("<sip:34020000001320000001@3402000000>", smsg.to_address_.c_str());
    EXPECT_STREQ("34020000001320000001", smsg.to_address_user_.c_str());
    EXPECT_STREQ("3402000000", smsg.to_address_host_.c_str());
    EXPECT_STREQ("", smsg.to_tag_.c_str());
    EXPECT_STREQ("854 ACK", smsg.cseq_.c_str());
    EXPECT_EQ((uint32_t)854, smsg.cseq_number_);
    EXPECT_STREQ("854k7337207yxpfj", smsg.call_id_.c_str());
    EXPECT_EQ((uint32_t)70, smsg.max_forwards_);
    EXPECT_EQ((size_t)0, smsg.body_.length());
}

VOID TEST(KernelPSTest, PsPacketDecodePartialPesHeader)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    // A PES packet with complete header, but without enough data.
    string raw = string("\x00\x00\x01\xc0\x00\x82\x8c\x80", 8);
    SrsBuffer b((char*)raw.data(), raw.length());

    // Should be success, for recover mode.
    HELPER_ASSERT_SUCCESS(context.decode(&b, handler.clear()));
    EXPECT_EQ((size_t)0, handler.msgs_.size());
    EXPECT_EQ(1, context.recover_);
}

VOID TEST(KernelPSTest, PsPacketDecodePartialPesHeader2)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    // Ignore if PS header is not integrity.
    context.ctx_.set_detect_ps_integrity(true);

    // A PES packet with complete header, but without enough data.
    string raw = string("\x00\x00\x01\xc0\x00\x82\x8c\x80", 8);
    SrsBuffer b((char*)raw.data(), raw.length());

    // Should be success, for recover mode.
    HELPER_ASSERT_SUCCESS(context.decode(&b, handler.clear()));
    EXPECT_EQ((size_t)0, handler.msgs_.size());
    EXPECT_EQ(0, context.recover_);

    // Ignored for not enough bytes.
    EXPECT_EQ(0, b.pos());
}

VOID TEST(KernelPSTest, PsPacketDecodeInvalidPesHeader)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    // A packet with invalid PES packet.
    string raw = string("\x00\x02\x00\x17\x00\x01\x80\x01", 8);
    SrsBuffer b((char*)raw.data(), raw.length());

    // Should be success, for recover mode.
    HELPER_ASSERT_SUCCESS(context.decode(&b, handler.clear()));
    EXPECT_EQ((size_t)0, handler.msgs_.size());
    EXPECT_EQ(1, context.recover_);
}

VOID TEST(KernelPSTest, PsPacketDecodeInvalidRtp)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    // A packet with invalid RTP packet.
    string raw = string("x80\x01", 2);
    SrsBuffer b((char*)raw.data(), raw.length());

    // Should be success, for recover mode.
    HELPER_ASSERT_SUCCESS(context.decode_rtp(&b, 0, handler.clear()));
    EXPECT_EQ((size_t)0, handler.msgs_.size());
    EXPECT_EQ(1, context.recover_);
}

VOID TEST(KernelPSTest, PsPacketDecodeRecover)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    if (true) {
        // A PES packet with complete header, but without enough data.
        string raw = string("\x00\x00\x01\xc0\x00\x82\x8c\x80", 8);
        SrsBuffer b((char*)raw.data(), raw.length());

        // Should be success, for recover mode.
        HELPER_ASSERT_SUCCESS(context.decode(&b, handler.clear()));
        EXPECT_EQ((size_t)0, handler.msgs_.size());
        EXPECT_EQ(1, context.recover_);
    }

    if (true) {
        // Continous data, but should be dropped for recover mode.
        string raw(136 - 8, 'x');
        SrsBuffer b((char*)raw.data(), raw.length());

        // Should be success, for recover mode.
        HELPER_ASSERT_SUCCESS(context.decode(&b, handler.clear()));
        EXPECT_EQ((size_t)0, handler.msgs_.size());
        EXPECT_EQ(2, context.recover_);
    }

    if (true) {
        // New PES packet with header, but should be dropped for recover mode.
        string raw = string("\x00\x00\x01\xc0\x00\x82\x8c\x80", 8) + string(136 - 8, 'x');
        SrsBuffer b((char*)raw.data(), raw.length());

        // Should be success, for recover mode.
        HELPER_ASSERT_SUCCESS(context.decode(&b, handler.clear()));
        EXPECT_EQ((size_t)0, handler.msgs_.size());
        EXPECT_EQ(3, context.recover_);
    }

    if (true) {
        // New pack header, should be ok and quit recover mode.
        string raw = string("\x00\x00\x01\xba\x44\x68\x6e\x4c\x94\x01\x01\x30\x13\xfe\xff\xff\x00\x00\xa0\x05", 20)
                     + string("\x00\x00\x01\xc0\x00\x82\x8c\x80\x09\x21\x1a\x1b\xa3\x51\xff\xff\xff\xf8", 18) + string(118, 'x');
        SrsBuffer b((char*)raw.data(), raw.length());

        // Should be success, for recover mode.
        HELPER_ASSERT_SUCCESS(context.decode(&b, handler.clear()));
        EXPECT_EQ((size_t)1, handler.msgs_.size());
        EXPECT_EQ(0, context.recover_);
    }
}

VOID TEST(KernelPSTest, PsRecoverLimit)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    // A packet with invalid RTP packet.
    for (int i = 0; i < 16; i++) {
        string raw = string("x80\x01", 2);
        SrsBuffer b((char*) raw.data(), raw.length());

        // Should be success, for recover mode.
        HELPER_ASSERT_SUCCESS(context.decode_rtp(&b, 0, handler.clear()));
        EXPECT_EQ((size_t)0, handler.msgs_.size());
        EXPECT_EQ(i+1, context.recover_);
    }

    // The last time, should fail.
    string raw = string("x80\x01", 2);
    SrsBuffer b((char*) raw.data(), raw.length());

    // Should be fail, because exceed max recover limit.
    HELPER_ASSERT_FAILED(context.decode_rtp(&b, 0, handler.clear()));
    EXPECT_EQ((size_t)0, handler.msgs_.size());
    EXPECT_EQ(17, context.recover_);
}

VOID TEST(KernelPSTest, PsRecoverLimit2)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    // A packet with invalid PES packet.
    for (int i = 0; i < 16; i++) {
        string raw = string("\x00\x02\x00\x17\x00\x01\x80\x01", 8);
        SrsBuffer b((char*)raw.data(), raw.length());

        // Should be success, for recover mode.
        HELPER_ASSERT_SUCCESS(context.decode_rtp(&b, 0, handler.clear()));
        EXPECT_EQ((size_t)0, handler.msgs_.size());
        EXPECT_EQ(i+1, context.recover_);
    }

    // The last time, should fail.
    string raw = string("\x00\x02\x00\x17\x00\x01\x80\x01", 8);
    SrsBuffer b((char*)raw.data(), raw.length());

    // Should be fail, because exceed max recover limit.
    HELPER_ASSERT_FAILED(context.decode_rtp(&b, 0, handler.clear()));
    EXPECT_EQ((size_t)0, handler.msgs_.size());
    EXPECT_EQ(17, context.recover_);
}

VOID TEST(KernelPSTest, PsNoRecoverLargeLength)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    // A packet with large RTP packet.
    string raw = string(1501, 'x');
    SrsBuffer b((char*)raw.data(), raw.length());

    // Should be success, for recover mode.
    HELPER_ASSERT_FAILED(context.decode_rtp(&b, 0, &handler));
    EXPECT_EQ((size_t)0, handler.msgs_.size());
    EXPECT_EQ(1, context.recover_);
}

VOID TEST(KernelPSTest, PsSkipUtilPack)
{
    if (true) {
        string raws[] = {
            string("\x00\x00\x01\xba", 4),
            string("\xaa\x00\x00\x01\xba", 5),
            string("\x00\x00\x00\x01\xba", 5),
            string("\x01\x00\x00\x01\xba", 5),
            string("\xaa\xaa\x00\x00\x01\xba", 6),
            string("\x00\x00\x00\x00\x01\xba", 6),
            string("\x00\x01\x00\x00\x01\xba", 6),
            string("\x01\x00\x00\x00\x01\xba", 6),
            string("\xaa\xaa\xaa\x00\x00\x01\xba", 7),
            string("\x00\x00\x00\x00\x00\x01\xba", 7),
            string("\x00\x00\x01\x00\x00\x01\xba", 7),
            string("\x00\x01\x00\x00\x00\x01\xba", 7),
            string("\x01\x00\x00\x00\x00\x01\xba", 7),
            string("\xaa\xaa\xaa\xaa\x00\x00\x01\xba", 8),
            string("\x00\x00\x00\x00\x00\x00\x01\xba", 8),
            string("\x00\x00\x00\x01\x00\x00\x01\xba", 8),
            string("\x00\x00\x01\x00\x00\x00\x01\xba", 8),
            string("\x00\x01\x00\x00\x00\x00\x01\xba", 8),
            string("\x01\x00\x00\x00\x00\x00\x01\xba", 8),
        };
        for (int i = 0; i < (int)(sizeof(raws) / sizeof(string)); i++) {
            string raw = raws[i];
            SrsBuffer b((char*) raw.data(), raw.length());
            EXPECT_TRUE(srs_skip_util_pack(&b));
        }
    }

    if (true) {
        string raws[] = {
            string(8, '\x00') + string(4, '\xaa'),
            string(7, '\x00') + string(4, '\xaa'),
            string(6, '\x00') + string(4, '\xaa'),
            string(5, '\x00') + string(4, '\xaa'),
            string(4, '\x00') + string(4, '\xaa'),
            string(3, '\x00') + string(4, '\xaa'),
            string(2, '\x00') + string(4, '\xaa'),
            string(1, '\x00') + string(4, '\xaa'),
            string(1, '\x00') + string(3, '\xaa'),
            string(1, '\x00') + string(2, '\xaa'),
            string(1, '\x00') + string(1, '\xaa'),
            string(1, '\x00'),
            string(8, '\x00'), string(8, '\x01'), string(8, '\xaa'),
            string(7, '\x00'), string(7, '\x01'), string(7, '\xaa'),
            string(6, '\x00'), string(6, '\x01'), string(6, '\xaa'),
            string(5, '\x00'), string(5, '\x01'), string(5, '\xaa'),
            string(4, '\x00'), string(4, '\x01'), string(4, '\xaa'),
            string(3, '\x00'), string(3, '\x01'), string(3, '\xaa'),
            string(2, '\x00'), string(2, '\x01'), string(2, '\xaa'),
            string(1, '\x00'), string(1, '\x01'), string(1, '\xaa'),
        };
        for (int i = 0; i < (int)(sizeof(raws) / sizeof(string)); i++) {
            string raw = raws[i];
            SrsBuffer b((char*) raw.data(), raw.length());
            EXPECT_FALSE(srs_skip_util_pack(&b));
        }
    }

    if (true) {
        SrsBuffer b(NULL, 0);
        EXPECT_FALSE(srs_skip_util_pack(&b));
    }
}

VOID TEST(KernelPSTest, PsPacketDecodeRegularMessage)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    // PT=DynamicRTP-Type-96, SSRC=0xBEBD135, Seq=31916, Time=95652000
    SrsRtpPacket rtp;
    string raw = string(
        "\x80\x60\x7c\xac\x05\xb3\x88\xa0\x0b\xeb\xd1\x35\x00\x00\x01\xc0" \
        "\x00\x6e\x8c\x80\x07\x25\x8a\x6d\xa9\xfd\xff\xf8\xff\xf9\x50\x40" \
        "\x0c\x9f\xfc\x01\x3a\x2e\x98\x28\x18\x0a\x09\x84\x81\x60\xc0\x50" \
        "\x2a\x12\x13\x05\x02\x22\x00\x88\x4c\x40\x11\x09\x85\x02\x61\x10" \
        "\xa8\x40\x00\x00\x00\x1f\xa6\x8d\xef\x03\xca\xf0\x63\x7f\x02\xe2" \
        "\x1d\x7f\xbf\x3e\x22\xbe\x3d\xf7\xa2\x7c\xba\xe6\xc8\xfb\x35\x9f" \
        "\xd1\xa2\xc4\xaa\xc5\x3d\xf6\x67\xfd\xc6\x39\x06\x9f\x9e\xdf\x9b" \
        "\x10\xd7\x4f\x59\xfd\xef\xea\xee\xc8\x4c\x40\xe5\xd9\xed\x00\x1c", 128);
    SrsBuffer b2((char*) raw.data(), raw.length());
    HELPER_ASSERT_SUCCESS(rtp.decode(&b2));

    SrsRtpRawPayload* rtp_raw = dynamic_cast<SrsRtpRawPayload*>(rtp.payload());
    SrsBuffer b((char*)rtp_raw->payload, rtp_raw->nn_payload);

    // Should be success, for recover mode.
    HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
    ASSERT_EQ((size_t)1, handler.msgs_.size());
    EXPECT_EQ(0, context.recover_);

    SrsTsMessage* m = handler.msgs_.front();
    EXPECT_EQ(SrsTsPESStreamIdAudioCommon, m->sid);
    EXPECT_EQ(100, m->PES_packet_length);
}

VOID TEST(KernelPSTest, PsPacketDecodeRegularMessage2)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    // PT=DynamicRTP-Type-96, SSRC=0xBEBD135, Seq=31916, Time=95652000
    SrsRtpPacket rtp;
    string raw = string(
        "\x80\x60\x7c\xac\x05\xb3\x88\xa0\x0b\xeb\xd1\x35\x00\x00\x01\xc0" \
        "\x00\x6e\x8c\x80\x07\x25\x8a\x6d\xa9\xfd\xff\xf8\xff\xf9\x50\x40" \
        "\x0c\x9f\xfc\x01\x3a\x2e\x98\x28\x18\x0a\x09\x84\x81\x60\xc0\x50" \
        "\x2a\x12\x13\x05\x02\x22\x00\x88\x4c\x40\x11\x09\x85\x02\x61\x10" \
        "\xa8\x40\x00\x00\x00\x1f\xa6\x8d\xef\x03\xca\xf0\x63\x7f\x02\xe2" \
        "\x1d\x7f\xbf\x3e\x22\xbe\x3d\xf7\xa2\x7c\xba\xe6\xc8\xfb\x35\x9f" \
        "\xd1\xa2\xc4\xaa\xc5\x3d\xf6\x67\xfd\xc6\x39\x06\x9f\x9e\xdf\x9b" \
        "\x10\xd7\x4f\x59\xfd\xef\xea\xee\xc8\x4c\x40\xe5\xd9\xed\x00\x1c", 128);
    SrsBuffer b((char*) raw.data(), raw.length());

    // Should be success, for recover mode.
    HELPER_ASSERT_SUCCESS(context.decode_rtp(&b, 0, &handler));
    ASSERT_EQ((size_t)1, handler.msgs_.size());
    EXPECT_EQ(0, context.recover_);

    SrsTsMessage* m = handler.msgs_.front();
    EXPECT_EQ(SrsTsPESStreamIdAudioCommon, m->sid);
    EXPECT_EQ(100, m->PES_packet_length);
}

VOID TEST(KernelPSTest, PsPacketDecodeRegularMessage3)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    // PT=DynamicRTP-Type-96, SSRC=0xBEBD135, Seq=31916, Time=95652000
    SrsRtpPacket rtp;
    string raw = string(
        "\x00\x00\x01\xc0\x80\x60\x7c\xac\x05\xb3\x88\xa0\x0b\xeb\xd1\x35" \
        "\x00\x6e\x8c\x80\x07\x25\x8a\x6d\xa9\xfd\xff\xf8\xff\xf9\x50\x40" \
        "\x0c\x9f\xfc\x01\x3a\x2e\x98\x28\x18\x0a\x09\x84\x81\x60\xc0\x50" \
        "\x2a\x12\x13\x05\x02\x22\x00\x88\x4c\x40\x11\x09\x85\x02\x61\x10" \
        "\xa8\x40\x00\x00\x00\x1f\xa6\x8d\xef\x03\xca\xf0\x63\x7f\x02\xe2" \
        "\x1d\x7f\xbf\x3e\x22\xbe\x3d\xf7\xa2\x7c\xba\xe6\xc8\xfb\x35\x9f" \
        "\xd1\xa2\xc4\xaa\xc5\x3d\xf6\x67\xfd\xc6\x39\x06\x9f\x9e\xdf\x9b" \
        "\x10\xd7\x4f\x59\xfd\xef\xea\xee\xc8\x4c\x40\xe5\xd9\xed\x00\x1c", 128);
    SrsBuffer b((char*) raw.data(), raw.length());

    // Should be success, for recover mode.
    HELPER_ASSERT_SUCCESS(context.decode_rtp(&b, 4, &handler));
    ASSERT_EQ((size_t)1, handler.msgs_.size());
    EXPECT_EQ(0, context.recover_);

    SrsTsMessage* m = handler.msgs_.front();
    EXPECT_EQ(SrsTsPESStreamIdAudioCommon, m->sid);
    EXPECT_EQ(100, m->PES_packet_length);
}

VOID TEST(KernelPSTest, PsPacketDecodeInvalidStartCode)
{
    srs_error_t err = srs_success;

    // PS: Enter recover=1, seq=31914, ts=95648400, pt=96, pack=31813, pack-msgs=6, sopm=31913,
    // bytes=[00 02 00 17 00 01 80 01], pos=0, left=96 for err
    // code=4048(GbPsHeader)(Invalid PS header for GB28181) : decode pack : decode : Invalid PS stream 0 0x2 0
    //
    // thread [2378][ha430859]: decode() [src/app/srs_app_gb28181.cpp:1631][errno=35]
    // thread [2378][ha430859]: decode() [src/kernel/srs_kernel_ps.cpp:76][errno=35]
    // thread [2378][ha430859]: do_decode() [src/kernel/srs_kernel_ps.cpp:145][errno=35]

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    // PT=DynamicRTP-Type-96, SSRC=0xBEBD135, Seq=31813, Time=95648400
    if (true) {
        string raw = string(
            "\x00\x00\x01\xba" \
            "\x56\x29\xb6\x04\xd4\x01\x09\xc3\x47\xfe\xff\xff\x01\x78\x46\xc7" \
            "\x00\x00\x01\xbb\x00\x12\x84\xe1\xa3\x04\xe1\x7f\xe0\xe0\x80\xc0" \
            "\xc0\x08\xbd\xe0\x80\xbf\xe0\x80\x00\x00\x01\xbc\x00\x5e\xe0\xff" \
            "\x00\x24\x40\x0e\x48\x4b\x01\x00\x16\x9f\x21\xb6\x6b\x77\x00\xff" \
            "\xff\xff\x41\x12\x48\x4b\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09" \
            "\x0a\x0b\x0c\x0d\x0e\x0f\x00\x30\x1b\xe0\x00\x1c\x42\x0e\x07\x10" \
            "\x10\xea\x0a\x00\x05\xa0\x11\x30\x00\x00\x1c\x21\x2a\x0a\x7f\xff" \
            "\x00\x00\x07\x08\x1f\xfe\x40\xb4\x0f\xc0\x00\x0c\x43\x0a\x00\x90" \
            "\xfe\x02\xb1\x13\x01\xf4\x03\xff\x51\x2b\xe5\x9b\x00\x00\x01\xe0" \
            "\x00\x2a\x8c\x80\x0a\x25\x8a\x6d\x81\x35\xff\xff\xff\xff\xfc\x00" \
            "\x00\x00\x01\x67\x4d\x00\x32\x9d\xa8\x0a\x00\x2d\x69\xb8\x08\x08" \
            "\x0a\x00\x00\x03\x00\x02\x00\x00\x03\x00\x65\x08\x00\x00\x01\xe0" \
            "\x00\x0e\x8c\x00\x03\xff\xff\xfc\x00\x00\x00\x01\x68\xee\x3c\x80" \
            "\x00\x00\x01\xe0\x00\x0e\x8c\x00\x02\xff\xfc\x00\x00\x00\x01\x06" \
            "\xe5\x01\xfb\x80\x00\x00\x01\xe0\xff\xc6\x8c\x00\x03\xff\xff\xfd" \
            "\x00\x00\x00\x01\x65\xb8\x00\x00\x03\x02\x98\x24\x13\xff\xf2\x82", 1400-1140) + string(1140, 'x');
        SrsBuffer b((char*) raw.data(), raw.length());

        // Should be success, for recover mode.
        HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
        ASSERT_EQ((size_t)3, handler.msgs_.size());
        EXPECT_EQ(0, context.recover_);

        SrsTsMessage* last = context.ctx_.last_;
        ASSERT_EQ(65472, last->PES_packet_length);
        ASSERT_EQ(1156, last->payload->length());
    }

    // Seq 31814 to 31858, 45*1400=63000, left is 65472-1156-63000=1316 bytes in next packet(seq=31859).
    for (int i = 0; i <= 31858 - 31814; i++) {
        string raw(1400, 'x');
        SrsBuffer b((char*) raw.data(), raw.length());

        HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
        ASSERT_EQ((size_t)3, handler.msgs_.size()); // We don't clear handler, so there must be 3 messages.
        EXPECT_EQ(0, context.recover_);

        SrsTsMessage* last = context.ctx_.last_;
        ASSERT_EQ(65472, last->PES_packet_length);
        ASSERT_EQ(1156 + 1400 * (i + 1), last->payload->length());
    }
    if (true) {
        SrsTsMessage* last = context.ctx_.last_;
        ASSERT_EQ(65472, last->PES_packet_length);
        ASSERT_EQ(64156, last->payload->length());
    }

    // PT=DynamicRTP-Type-96, SSRC=0xBEBD135, Seq=31859, Time=95648400 [TCP segment of a reassembled PDU]
    if (true) {
        string raw = string(1312, 'x') + string(
            "\x1a\x67\xe4\x00" /* last 1312+4=1316 bytes for previous video frame */ \
            "\x00\x00\x01\xe0\xff\xc6\x88\x00\x03\xff\xff\xff" \
            "\x7a\xc3\x59\x8e\x08\x09\x39\x7d\x56\xa5\x97\x2e\xf5\xc6\x7e\x2c" \
            "\xb8\xd3\x7f\x4b\x57\x6a\xba\x7a\x75\xd0\xb9\x95\x19\x61\x13\xd5" \
            "\x21\x8c\x88\x62\x62\x4c\xa8\x3c\x0e\x2e\xe6\x2b\x3d\xf0\x9a\x8e" \
            "\xb3\xbc\xe1\xe7\x52\x79\x4b\x14\xa9\x8e\xf0\x78\x38\xf4\xb6\x27" \
            "\x62\x4f\x97\x89\x87\xc8\x8f\x6c", 88);
        SrsBuffer b((char*) raw.data(), raw.length());

        // Should be success, for recover mode.
        HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
        ASSERT_EQ((size_t)4, handler.msgs_.size());
        EXPECT_EQ(0, context.recover_);

        SrsTsMessage* last = context.ctx_.last_;
        ASSERT_EQ(65472, last->PES_packet_length);
        ASSERT_EQ(72, last->payload->length());
    }

    // Seq 31860 to 31905, 46*1400=64400, left is 65472-72-64400=1000 bytes in next packet(seq=31906).
    for (int i = 0; i <= 31905 - 31860; i++) {
        string raw(1400, 'x');
        SrsBuffer b((char*) raw.data(), raw.length());

        HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
        ASSERT_EQ((size_t)4, handler.msgs_.size()); // We don't clear handler, so there must be 4 messages.
        EXPECT_EQ(0, context.recover_);

        SrsTsMessage* last = context.ctx_.last_;
        ASSERT_EQ(65472, last->PES_packet_length);
        ASSERT_EQ(72 + 1400 * (i + 1), last->payload->length());
    }
    if (true) {
        SrsTsMessage* last = context.ctx_.last_;
        ASSERT_EQ(65472, last->PES_packet_length);
        ASSERT_EQ(64472, last->payload->length());
    }

    // PT=DynamicRTP-Type-96, SSRC=0xBEBD135, Seq=31906, Time=95648400 [TCP segment of a reassembled PDU]
    if (true) {
        string raw = string(992, 'x') + string(
            "\x28\xa9\x68\x46\x6f\xaf\x11\x9e" /* last 992+8=1000 bytes for previous video frame */ \
            "\x00\x00\x01\xe0\x27\xc2\x88\x00" \
            "\x03\xff\xff\xfa\x05\xcb\xbc\x6f\x7b\x70\x13\xbc\xc1\xc8\x9a\x7d" \
            "\x13\x09\x6d\x17\x78\xb7\xaf\x95\x23\xa6\x25\x40\xc0\xdf\x8b\x7e", 48) + string(360, 'x');
        SrsBuffer b((char*) raw.data(), raw.length());

        // Should be success, for recover mode.
        HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
        ASSERT_EQ((size_t)5, handler.msgs_.size());
        EXPECT_EQ(0, context.recover_);

        SrsTsMessage* last = context.ctx_.last_;
        ASSERT_EQ(10172, last->PES_packet_length);
        ASSERT_EQ(388, last->payload->length());
    }

    // Seq 31907 to 31912, 6*1400=8400, left is 10172-388-8400=1384 bytes in next packet(seq=31913).
    for (int i = 0; i <= 31912 - 31907; i++) {
        string raw(1400, 'x');
        SrsBuffer b((char*) raw.data(), raw.length());

        HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
        ASSERT_EQ((size_t)5, handler.msgs_.size()); // We don't clear handler, so there must be 5 messages.
        EXPECT_EQ(0, context.recover_);

        SrsTsMessage* last = context.ctx_.last_;
        ASSERT_EQ(10172, last->PES_packet_length);
        ASSERT_EQ(388 + 1400 * (i + 1), last->payload->length());
    }
    if (true) {
        SrsTsMessage* last = context.ctx_.last_;
        ASSERT_EQ(10172, last->PES_packet_length);
        ASSERT_EQ(8788, last->payload->length());
    }

    // PT=DynamicRTP-Type-96, SSRC=0xBEBD135, Seq=31913, Time=95648400
    if (true) {
        string raw = string(1376, 'x') + string(
            "\x02\xf0\x42\x42\x74\xe3\x1c\x20" /* last 1376+8=1384 bytes for previous video frame */ \
            "\x00\x00\x01\xbd\x00\x6a\x8c\x80" \
            "\x07\x25\x8a\x6d\x81\x35\xff\xf8", 24);
        SrsBuffer b((char*) raw.data(), raw.length());

        // Should be success, for recover mode.
        HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
        ASSERT_EQ((size_t)6, handler.msgs_.size());
        EXPECT_EQ(0, context.recover_);

        SrsTsMessage* last = context.ctx_.last_;
        ASSERT_EQ(96, last->PES_packet_length);
        ASSERT_EQ(0, last->payload->length());
    }

    // Seq 31914, 96 bytes
    if (true) {
        string raw = string(
            "\x00\x02\x00\x17\x00\x01\x80\x01\x78\xff\x46\xc7\xe0\xf1\xf0\x50" \
            "\x49\x6c\x65\xc2\x19\x2b\xae\x38\xd1\xa7\x08\x00\x82\x60\x16\x39" \
            "\xa6\x6b\xa7\x03\x8e\x8d\xff\x3c\xe2\xa9\x80\xac\x09\x06\x60\xc9" \
            "\x12\x0f\xb2\xf7\xb7\x40\x3b\x49\xb8\x75\x6b\x70\x2c\x03\xb4\x1a" \
            "\x12\x0f\xb2\xf7\xb7\x40\x3b\x49\xb8\x75\x6b\x70\x2c\x03\xb4\x1a" \
            "\xa2\x15\x7a\x9e\x83\x7a\xee\xb1\xd6\x64\xdf\x7e\x11\x9c\xb9\xe9", 96);
        SrsBuffer b((char*) raw.data(), raw.length());

        // Should be success, for recover mode.
        HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
        ASSERT_EQ((size_t)6, handler.msgs_.size()); // Private Stream is dropped, so there should be still 6 messages.
        EXPECT_EQ(0, context.recover_);
    }
}

VOID TEST(KernelPSTest, PsPacketDecodePartialPayload)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    if (true) {
        string raw = string(
            "\x00\x00\x01\xbd\x00\x6a" /* PES header */ \
            "\x8c\x80\x07\x25\x8a\x6d\x81\x35\xff\xf8" /* PES header data */, 16);
        SrsBuffer b((char*) raw.data(), raw.length());

        // Should be success, for recover mode.
        HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
        ASSERT_EQ((size_t)0, handler.msgs_.size()); // Drop Private Stream message.
        EXPECT_EQ(0, context.recover_);
    }

    if (true) {
        string raw = string(
            /* Bellow is PES packet payload, 96 bytes */ \
            "\x00\x02\x00\x17\x00\x01\x80\x01\x78\xff\x46\xc7\xe0\xf1\xf0\x50" \
            "\x49\x6c\x65\xc2\x19\x2b\xae\x38\xd1\xa7\x08\x00\x82\x60\x16\x39" \
            "\xa6\x6b\xa7\x03\x8e\x8d\xff\x3c\xe2\xa9\x80\xac\x09\x06\x60\xc9" \
            "\x12\x0f\xb2\xf7\xb7\x40\x3b\x49\xb8\x75\x6b\x70\x2c\x03\xb4\x1a" \
            "\x12\x0f\xb2\xf7\xb7\x40\x3b\x49\xb8\x75\x6b\x70\x2c\x03\xb4\x1a" \
            "\xa2\x15\x7a\x9e\x83\x7a\xee\xb1\xd6\x64\xdf\x7e\x11\x9c\xb9\xe9", 96);
        SrsBuffer b((char*) raw.data(), raw.length());

        // Should be success, for recover mode.
        HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
        ASSERT_EQ((size_t)0, handler.msgs_.size()); // Drop Private Stream message.
        EXPECT_EQ(0, context.recover_);
    }
}

VOID TEST(KernelPSTest, PsPacketDecodePrivateStream)
{
    srs_error_t err = srs_success;

    MockPsHandler handler;
    SrsRecoverablePsContext context;

    string raw = string(
        "\x00\x00\x01\xbd\x00\x6a" /* PES header */ \
        "\x8c\x80\x07\x25\x8a\x6d\x81\x35\xff\xf8" /* PES header data */ \
        /* Bellow is PES packet payload, 96 bytes */ \
        "\x00\x02\x00\x17\x00\x01\x80\x01\x78\xff\x46\xc7\xe0\xf1\xf0\x50" \
        "\x49\x6c\x65\xc2\x19\x2b\xae\x38\xd1\xa7\x08\x00\x82\x60\x16\x39" \
        "\xa6\x6b\xa7\x03\x8e\x8d\xff\x3c\xe2\xa9\x80\xac\x09\x06\x60\xc9" \
        "\x12\x0f\xb2\xf7\xb7\x40\x3b\x49\xb8\x75\x6b\x70\x2c\x03\xb4\x1a" \
        "\x12\x0f\xb2\xf7\xb7\x40\x3b\x49\xb8\x75\x6b\x70\x2c\x03\xb4\x1a" \
        "\xa2\x15\x7a\x9e\x83\x7a\xee\xb1\xd6\x64\xdf\x7e\x11\x9c\xb9\xe9", 16+96);
    SrsBuffer b((char*) raw.data(), raw.length());

    // Should be success, for recover mode.
    HELPER_ASSERT_SUCCESS(context.decode(&b, &handler));
    ASSERT_EQ((size_t)0, handler.msgs_.size()); // Drop Private Stream message.
    EXPECT_EQ(0, context.recover_);
}

