//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai37.hpp>

#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_network.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_stream_token.hpp>
#include <srs_core_platform.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_rtc_rtcp.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_rtc_stun.hpp>
#include <srs_protocol_st.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_utest_ai11.hpp>
#include <srs_utest_ai12.hpp>
#include <srs_utest_ai17.hpp>
#include <srs_utest_manual_http.hpp>
#include <srs_utest_manual_protocol.hpp>

#include <srt/srt.h>
#include <string.h>

#include <set>

using namespace std;

extern std::vector<SrsIPAddress *> _srs_system_ips;
extern std::string _srs_public_internet_address;
extern std::string _srs_system_hostname;
extern std::set<std::string> discover_candidates(ISrsProtocolUtility *utility, ISrsAppConfig *config, SrsRtcUserConfig *ruc);
extern srs_error_t api_server_as_candidates(ISrsAppConfig *config, std::string api, std::set<std::string> &candidate_ips);

MockDtlsCallbackForHandshake::MockDtlsCallbackForHandshake()
{
    handshake_done_count_ = 0;
    handshake_done_error_ = srs_success;
    app_data_error_ = srs_success;
    write_error_ = srs_success;
    alert_error_ = srs_success;
}

MockDtlsCallbackForHandshake::~MockDtlsCallbackForHandshake()
{
    srs_freep(handshake_done_error_);
    srs_freep(app_data_error_);
    srs_freep(write_error_);
    srs_freep(alert_error_);
}

srs_error_t MockDtlsCallbackForHandshake::on_dtls_handshake_done()
{
    handshake_done_count_++;
    return srs_error_copy(handshake_done_error_);
}

srs_error_t MockDtlsCallbackForHandshake::on_dtls_application_data(const char *data, const int len)
{
    app_data_.push_back(string(data, len));
    return srs_error_copy(app_data_error_);
}

srs_error_t MockDtlsCallbackForHandshake::write_dtls_data(void *data, int size)
{
    packets_.push_back(string((char *)data, size));
    return srs_error_copy(write_error_);
}

srs_error_t MockDtlsCallbackForHandshake::on_dtls_alert(std::string type, std::string desc)
{
    alerts_.push_back(make_pair(type, desc));
    return srs_error_copy(alert_error_);
}

MockRtcConnectionForCreateSession::MockRtcConnectionForCreateSession()
{
    add_publisher_error_ = srs_success;
    add_player_error_ = srs_success;
    generate_local_sdp_error_ = srs_success;
    initialize_error_ = srs_success;
    generated_username_ = "srvufrag:cliufrag";
    last_tracks_publish_ = false;
    last_tracks_status_ = false;
    last_init_dtls_ = false;
    last_init_srtp_ = false;
    destroyed_ = NULL;
}

MockRtcConnectionForCreateSession::~MockRtcConnectionForCreateSession()
{
    srs_freep(add_publisher_error_);
    srs_freep(add_player_error_);
    srs_freep(generate_local_sdp_error_);
    srs_freep(initialize_error_);
    if (destroyed_) {
        *destroyed_ = true;
    }
}

srs_error_t MockRtcConnectionForCreateSession::add_publisher(SrsRtcUserConfig *ruc, SrsSdp &local_sdp)
{
    calls_.push_back("add_publisher");
    return srs_error_copy(add_publisher_error_);
}

srs_error_t MockRtcConnectionForCreateSession::add_player(SrsRtcUserConfig *ruc, SrsSdp &local_sdp)
{
    calls_.push_back("add_player");
    return srs_error_copy(add_player_error_);
}

void MockRtcConnectionForCreateSession::set_all_tracks_status(std::string stream_uri, bool is_publish, bool status)
{
    calls_.push_back("set_all_tracks_status");
    last_tracks_stream_ = stream_uri;
    last_tracks_publish_ = is_publish;
    last_tracks_status_ = status;
}

void MockRtcConnectionForCreateSession::set_remote_sdp(const SrsSdp &sdp)
{
    calls_.push_back("set_remote_sdp");
}

void MockRtcConnectionForCreateSession::set_local_sdp(const SrsSdp &sdp)
{
    calls_.push_back("set_local_sdp");
}

void MockRtcConnectionForCreateSession::set_state_as_waiting_stun()
{
    calls_.push_back("set_state_as_waiting_stun");
}

srs_error_t MockRtcConnectionForCreateSession::initialize(ISrsRequest *r, bool dtls, bool srtp, std::string username)
{
    calls_.push_back("initialize");
    last_init_dtls_ = dtls;
    last_init_srtp_ = srtp;
    last_init_username_ = username;
    return srs_error_copy(initialize_error_);
}

void MockRtcConnectionForCreateSession::set_publish_token(SrsSharedPtr<ISrsStreamPublishToken> publish_token)
{
    calls_.push_back("set_publish_token");
    publish_token_ = publish_token;
}

srs_error_t MockRtcConnectionForCreateSession::generate_local_sdp(SrsRtcUserConfig *ruc, SrsSdp &local_sdp, std::string &username)
{
    calls_.push_back("generate_local_sdp");
    username = generated_username_;
    return srs_error_copy(generate_local_sdp_error_);
}

MockAppFactoryForCreateSession::MockAppFactoryForCreateSession()
{
    connection_ = NULL;
    create_rtc_connection_count_ = 0;
}

MockAppFactoryForCreateSession::~MockAppFactoryForCreateSession()
{
}

ISrsRtcConnection *MockAppFactoryForCreateSession::create_rtc_connection(ISrsExecRtcAsyncTask *exec, const SrsContextId &cid)
{
    create_rtc_connection_count_++;
    return connection_;
}

MockAsyncCallTaskForSessionManager::MockAsyncCallTaskForSessionManager(int *calls)
{
    calls_ = calls;
}

MockAsyncCallTaskForSessionManager::~MockAsyncCallTaskForSessionManager()
{
}

srs_error_t MockAsyncCallTaskForSessionManager::call()
{
    (*calls_)++;
    return srs_success;
}

std::string MockAsyncCallTaskForSessionManager::to_string()
{
    return "mock-task";
}

MockContextIdSetterForTcpConnCycle::MockContextIdSetterForTcpConnCycle()
{
    set_cid_count_ = 0;
}

MockContextIdSetterForTcpConnCycle::~MockContextIdSetterForTcpConnCycle()
{
}

void MockContextIdSetterForTcpConnCycle::set_cid(const SrsContextId &cid)
{
    set_cid_count_++;
    cid_ = cid;
}

MockRtcConnectionForTcpConnCycle::MockRtcConnectionForTcpConnCycle()
{
    tcp_network_ = NULL;
    expire_count_ = 0;
}

MockRtcConnectionForTcpConnCycle::~MockRtcConnectionForTcpConnCycle()
{
    tcp_network_ = NULL;
}

ISrsRtcNetwork *MockRtcConnectionForTcpConnCycle::tcp()
{
    return tcp_network_;
}

void MockRtcConnectionForTcpConnCycle::expire()
{
    expire_count_++;
}

MockSrtClientHandlerForAcceptor::MockSrtClientHandlerForAcceptor()
{
    accept_error_ = srs_success;
}

MockSrtClientHandlerForAcceptor::~MockSrtClientHandlerForAcceptor()
{
    srs_freep(accept_error_);
}

srs_error_t MockSrtClientHandlerForAcceptor::accept_srt_client(srs_srt_t srt_fd)
{
    fds_.push_back(srt_fd);
    return srs_error_copy(accept_error_);
}

MockSrtSocketWaiter::MockSrtSocketWaiter(SrsSrtSocket *skt, bool readable)
{
    skt_ = skt;
    readable_ = readable;
    done_ = false;
    err_ = srs_success;
}

MockSrtSocketWaiter::~MockSrtSocketWaiter()
{
    srs_freep(err_);
}

srs_error_t MockSrtSocketWaiter::cycle()
{
    err_ = readable_ ? skt_->wait_readable() : skt_->wait_writeable();
    done_ = true;
    return srs_success;
}

MockRtcPlayForJsonApi::MockRtcPlayForJsonApi(ISrsRtcApiServer *server) : SrsGoApiRtcPlay(server)
{
    serve_count_ = 0;
    serve_error_ = srs_success;
    publish_ = dtls_ = srtp_ = false;
}

MockRtcPlayForJsonApi::~MockRtcPlayForJsonApi()
{
    srs_freep(serve_error_);
}

srs_error_t MockRtcPlayForJsonApi::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, SrsRtcUserConfig *ruc)
{
    serve_count_++;
    ip_ = ruc->req_->ip_;
    vhost_ = ruc->req_->vhost_;
    app_ = ruc->req_->app_;
    stream_ = ruc->req_->stream_;
    api_ = ruc->api_;
    eip_ = ruc->eip_;
    vcodec_ = ruc->vcodec_;
    acodec_ = ruc->acodec_;
    publish_ = ruc->publish_;
    dtls_ = ruc->dtls_;
    srtp_ = ruc->srtp_;

    ruc->local_sdp_str_ = "answer-sdp";
    ruc->session_id_ = "sid-1";
    return srs_error_copy(serve_error_);
}

MockRtcPublishForJsonApi::MockRtcPublishForJsonApi(ISrsRtcApiServer *server) : SrsGoApiRtcPublish(server)
{
    serve_count_ = 0;
    serve_error_ = srs_success;
    publish_ = dtls_ = srtp_ = false;
}

MockRtcPublishForJsonApi::~MockRtcPublishForJsonApi()
{
    srs_freep(serve_error_);
}

srs_error_t MockRtcPublishForJsonApi::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, SrsRtcUserConfig *ruc)
{
    serve_count_++;
    ip_ = ruc->req_->ip_;
    vhost_ = ruc->req_->vhost_;
    param_ = ruc->req_->param_;
    api_ = ruc->api_;
    eip_ = ruc->eip_;
    vcodec_ = ruc->vcodec_;
    acodec_ = ruc->acodec_;
    publish_ = ruc->publish_;
    dtls_ = ruc->dtls_;
    srtp_ = ruc->srtp_;

    ruc->local_sdp_str_ = "answer-sdp";
    ruc->session_id_ = "sid-2";
    return srs_error_copy(serve_error_);
}

// Hand the records each side sent to the other side, until neither side has anything more to send.
srs_error_t dtls_exchange(SrsDtls *client, MockDtlsCallbackForHandshake *ccb, SrsDtls *server, MockDtlsCallbackForHandshake *scb)
{
    srs_error_t err = srs_success;

    for (int round = 0; round < 16; round++) {
        if (ccb->packets_.empty() && scb->packets_.empty()) {
            return err;
        }

        vector<string> to_server;
        to_server.swap(ccb->packets_);
        for (int i = 0; i < (int)to_server.size(); i++) {
            string &pkt = to_server[i];
            if ((err = server->on_dtls(&pkt[0], (int)pkt.size())) != srs_success) {
                return srs_error_wrap(err, "server");
            }
        }

        vector<string> to_client;
        to_client.swap(scb->packets_);
        for (int i = 0; i < (int)to_client.size(); i++) {
            string &pkt = to_client[i];
            if ((err = client->on_dtls(&pkt[0], (int)pkt.size())) != srs_success) {
                return srs_error_wrap(err, "client");
            }
        }
    }

    return srs_error_new(ERROR_RTC_DTLS, "DTLS exchange does not end");
}

// An RTP packet with a 12 bytes header and a payload, in a buffer large enough for the SRTP auth tag.
static int build_rtp_packet(char *buf, uint16_t seq, int payload_size)
{
    memset(buf, 0, 1500);
    buf[0] = (char)0x80;
    buf[1] = 96;
    buf[2] = (char)(seq >> 8);
    buf[3] = (char)(seq & 0xff);
    buf[7] = 0x10;
    buf[8] = 0x11;
    buf[9] = 0x22;
    buf[10] = 0x33;
    buf[11] = 0x44;
    for (int i = 0; i < payload_size; i++) {
        buf[12 + i] = (char)('a' + i % 26);
    }
    return 12 + payload_size;
}

// An RTCP receiver report without report blocks.
static int build_rtcp_rr(char *buf)
{
    memset(buf, 0, 1500);
    buf[0] = (char)0x80;
    buf[1] = (char)201;
    buf[3] = 1;
    buf[4] = 0x11;
    buf[5] = 0x22;
    buf[6] = 0x33;
    buf[7] = 0x44;
    return 8;
}

// Protect an RTP and an RTCP packet by one side and unprotect them by the other.
void expect_srtp_round_trip(ISrsSRTP *sender, ISrsSRTP *receiver, uint16_t seq)
{
    srs_error_t err;

    uint32_t storage[400];
    char *buf = (char *)storage;
    char plain[1500];

    int nb = build_rtp_packet(buf, seq, 20);
    memcpy(plain, buf, nb);
    HELPER_EXPECT_SUCCESS(sender->protect_rtp(buf, &nb));
    EXPECT_EQ(12 + 20 + 10, nb);
    EXPECT_NE(0, memcmp(plain + 12, buf + 12, 20));
    HELPER_EXPECT_SUCCESS(receiver->unprotect_rtp(buf, &nb));
    EXPECT_EQ(12 + 20, nb);
    EXPECT_EQ(0, memcmp(plain, buf, nb));

    nb = build_rtcp_rr(buf);
    memcpy(plain, buf, nb);
    HELPER_EXPECT_SUCCESS(sender->protect_rtcp(buf, &nb));
    EXPECT_EQ(8 + 4 + 10, nb);
    HELPER_EXPECT_SUCCESS(receiver->unprotect_rtcp(buf, &nb));
    EXPECT_EQ(8, nb);
    EXPECT_EQ(0, memcmp(plain, buf, nb));
}

// A DTLS client and server complete the handshake over the records they pass to each other, then derive SRTP keys
// that match: what one side sends with, the other receives with, see RFC 5764 section 4.2.
VOID TEST(DtlsHandshakeTest, ClientAndServerAgreeOnSrtpKeys)
{
    srs_error_t err;

    MockDtlsCallbackForHandshake ccb, scb;
    SrsDtls client(&ccb);
    SrsDtls server(&scb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
    HELPER_ASSERT_SUCCESS(server.initialize("passive", "dtls1.2"));

    // The passive side waits for the ClientHello.
    HELPER_ASSERT_SUCCESS(server.start_active_handshake());
    EXPECT_TRUE(scb.packets_.empty());

    // The active side sends the ClientHello, a handshake record(22) of type client_hello(1).
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());
    ASSERT_FALSE(ccb.packets_.empty());
    ASSERT_GE((int)ccb.packets_[0].size(), 14);
    EXPECT_EQ(22, (uint8_t)ccb.packets_[0][0]);
    EXPECT_EQ(1, (uint8_t)ccb.packets_[0][13]);

    HELPER_ASSERT_SUCCESS(dtls_exchange(&client, &ccb, &server, &scb));

    EXPECT_EQ(1, ccb.handshake_done_count_);
    EXPECT_EQ(1, scb.handshake_done_count_);
    EXPECT_TRUE(client.impl_->handshake_done_for_us_);
    EXPECT_TRUE(server.impl_->handshake_done_for_us_);
    EXPECT_TRUE(ccb.alerts_.empty());
    EXPECT_TRUE(scb.alerts_.empty());

    // Once done, the client stops its ARQ, and a second done is ignored.
    SrsDtlsClientImpl *client_impl = dynamic_cast<SrsDtlsClientImpl *>(client.impl_);
    ASSERT_TRUE(client_impl != NULL);
    EXPECT_TRUE(client_impl->trd_ == NULL);
    EXPECT_EQ(SrsDtlsStateClientDone, client_impl->state_);
    HELPER_EXPECT_SUCCESS(client_impl->on_handshake_done());
    EXPECT_EQ(1, ccb.handshake_done_count_);

    string client_recv, client_send, server_recv, server_send;
    HELPER_ASSERT_SUCCESS(client.get_srtp_key(client_recv, client_send));
    HELPER_ASSERT_SUCCESS(server.get_srtp_key(server_recv, server_send));

    // Each key is the 16 bytes master key and the 14 bytes master salt.
    EXPECT_EQ(30, (int)client_send.size());
    EXPECT_EQ(30, (int)client_recv.size());
    EXPECT_TRUE(client_send == server_recv);
    EXPECT_TRUE(client_recv == server_send);
    EXPECT_TRUE(client_send != client_recv);

    // Media the client protects, the server unprotects, and the other way round.
    SrsSRTP client_srtp, server_srtp;
    HELPER_ASSERT_SUCCESS(client_srtp.initialize(client_recv, client_send));
    HELPER_ASSERT_SUCCESS(server_srtp.initialize(server_recv, server_send));
    expect_srtp_round_trip(&client_srtp, &server_srtp, 1);
    expect_srtp_round_trip(&server_srtp, &client_srtp, 1);
}

// After the handshake, application data such as a DataChannel message reaches the peer's callback, and a close_notify
// alert reaches both sides as a warning, see RFC 6347 and RFC 5246 section 7.2.1.
VOID TEST(DtlsHandshakeTest, ApplicationDataAndCloseNotifyAfterHandshake)
{
    srs_error_t err;

    MockDtlsCallbackForHandshake ccb, scb;
    SrsDtls client(&ccb);
    SrsDtls server(&scb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
    HELPER_ASSERT_SUCCESS(server.initialize("passive", "dtls1.2"));
    HELPER_ASSERT_SUCCESS(server.start_active_handshake());
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());
    HELPER_ASSERT_SUCCESS(dtls_exchange(&client, &ccb, &server, &scb));

    ASSERT_EQ(5, SSL_write(client.impl_->dtls_, "hello", 5));
    ASSERT_EQ(1, (int)ccb.packets_.size());
    // An application_data record(23).
    EXPECT_EQ(23, (uint8_t)ccb.packets_[0][0]);
    HELPER_ASSERT_SUCCESS(dtls_exchange(&client, &ccb, &server, &scb));
    ASSERT_EQ(1, (int)scb.app_data_.size());
    EXPECT_STREQ("hello", scb.app_data_[0].c_str());

    // The connection fails the record its handler rejects.
    scb.app_data_error_ = srs_error_new(ERROR_RTC_DTLS, "mock app data error");
    ASSERT_EQ(5, SSL_write(client.impl_->dtls_, "world", 5));
    HELPER_EXPECT_FAILED(dtls_exchange(&client, &ccb, &server, &scb));
    ASSERT_EQ(2, (int)scb.app_data_.size());
    EXPECT_STREQ("world", scb.app_data_[1].c_str());

    // The client closes the DTLS session.
    SSL_shutdown(client.impl_->dtls_);
    ASSERT_EQ(1, (int)ccb.alerts_.size());
    EXPECT_STREQ("warning", ccb.alerts_[0].first.c_str());
    EXPECT_STREQ("CN", ccb.alerts_[0].second.c_str());

    HELPER_EXPECT_SUCCESS(dtls_exchange(&client, &ccb, &server, &scb));
    ASSERT_FALSE(scb.alerts_.empty());
    EXPECT_STREQ("warning", scb.alerts_[0].first.c_str());
    EXPECT_STREQ("CN", scb.alerts_[0].second.c_str());
}

// The handshake fails at the side whose handler rejects the done event: the server after the client's Finished, and
// the client after the server's Finished.
VOID TEST(DtlsHandshakeTest, HandshakeDoneErrorFailsTheHandshake)
{
    srs_error_t err;

    if (true) {
        MockDtlsCallbackForHandshake ccb, scb;
        scb.handshake_done_error_ = srs_error_new(ERROR_RTC_DTLS, "mock server done error");
        SrsDtls client(&ccb);
        SrsDtls server(&scb);
        HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
        HELPER_ASSERT_SUCCESS(server.initialize("passive", "dtls1.2"));
        HELPER_ASSERT_SUCCESS(client.start_active_handshake());

        err = dtls_exchange(&client, &ccb, &server, &scb);
        EXPECT_EQ(ERROR_RTC_DTLS, srs_error_code(err));
        srs_freep(err);
        EXPECT_EQ(1, scb.handshake_done_count_);
        EXPECT_EQ(0, ccb.handshake_done_count_);
    }

    if (true) {
        MockDtlsCallbackForHandshake ccb, scb;
        ccb.handshake_done_error_ = srs_error_new(ERROR_RTC_DTLS, "mock client done error");
        SrsDtls client(&ccb);
        SrsDtls server(&scb);
        HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
        HELPER_ASSERT_SUCCESS(server.initialize("passive", "dtls1.2"));
        HELPER_ASSERT_SUCCESS(client.start_active_handshake());

        err = dtls_exchange(&client, &ccb, &server, &scb);
        EXPECT_EQ(ERROR_RTC_DTLS, srs_error_code(err));
        srs_freep(err);
        EXPECT_EQ(1, scb.handshake_done_count_);
        EXPECT_EQ(1, ccb.handshake_done_count_);
    }
}

// Replace every cipher suite of a ClientHello record by TLS_NULL_WITH_NULL_NULL, which no server accepts. The
// ClientHello is sent in epoch 0, without a MAC, so the server parses the changed record.
static void ai37_clear_client_hello_ciphers(string &pkt)
{
    // The record header is 13 bytes and the handshake header 12, then the version and the random.
    size_t p = 13 + 12 + 2 + 32;
    p += 1 + (uint8_t)pkt[p];
    p += 1 + (uint8_t)pkt[p];
    size_t nn = ((uint8_t)pkt[p] << 8) | (uint8_t)pkt[p + 1];
    p += 2;
    for (size_t i = 0; i < nn && p + i < pkt.size(); i++) {
        pkt[p + i] = 0;
    }
}

// A ClientHello without a cipher suite the server supports fails the handshake: the server sends a fatal alert, and
// the client that reads it reports it, see RFC 5246 section 7.4.1.3.
VOID TEST(DtlsHandshakeTest, NoSharedCipherSendsFatalAlert)
{
    srs_error_t err;

    MockDtlsCallbackForHandshake ccb, scb;
    SrsDtls client(&ccb);
    SrsDtls server(&scb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
    HELPER_ASSERT_SUCCESS(server.initialize("passive", "dtls1.2"));
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());
    ASSERT_EQ(1, (int)ccb.packets_.size());

    string hello = ccb.packets_[0];
    ccb.packets_.clear();
    ai37_clear_client_hello_ciphers(hello);

    HELPER_EXPECT_FAILED(server.on_dtls(&hello[0], (int)hello.size()));
    EXPECT_EQ(0, scb.handshake_done_count_);
    ASSERT_FALSE(scb.alerts_.empty());
    EXPECT_STREQ("fatal", scb.alerts_[0].first.c_str());

    // The server answered with its alert record(21).
    ASSERT_EQ(1, (int)scb.packets_.size());
    EXPECT_EQ(21, (uint8_t)scb.packets_[0][0]);

    // The client reports the alert to its handler, which closes the session.
    err = client.on_dtls(&scb.packets_[0][0], (int)scb.packets_[0].size());
    srs_freep(err);
    ASSERT_FALSE(ccb.alerts_.empty());
    EXPECT_STREQ("fatal", ccb.alerts_[0].first.c_str());
    EXPECT_EQ(0, ccb.handshake_done_count_);
}

// When no answer comes, the client retransmits the ClientHello by its ARQ coroutine, with the timer of
// dtls_timer_cb starting at 50ms.
VOID TEST(DtlsHandshakeTest, ClientRetransmitsClientHelloWithoutAnswer)
{
    srs_error_t err;

    MockDtlsCallbackForHandshake ccb;
    SrsDtls client(&ccb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());
    ASSERT_EQ(1, (int)ccb.packets_.size());

    srs_usleep(300 * SRS_UTIME_MILLISECONDS);

    ASSERT_GE((int)ccb.packets_.size(), 2);
    for (int i = 0; i < (int)ccb.packets_.size(); i++) {
        EXPECT_EQ(22, (uint8_t)ccb.packets_[i][0]);
        EXPECT_EQ(1, (uint8_t)ccb.packets_[i][13]);
    }
    EXPECT_EQ((int)ccb.packets_.size() - 1, client.impl_->nn_arq_packets_);
    EXPECT_EQ(0, ccb.handshake_done_count_);
}

// A record the transport fails to send is reported to the caller, while the handshake ignores it and lets the ARQ
// send it again.
VOID TEST(DtlsHandshakeTest, WriteErrorIsReportedNotFatal)
{
    srs_error_t err;

    MockDtlsCallbackForHandshake ccb;
    ccb.write_error_ = srs_error_new(ERROR_SOCKET_WRITE, "mock write error");
    SrsDtls client(&ccb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
    HELPER_EXPECT_SUCCESS(client.start_active_handshake());
    EXPECT_EQ(1, (int)ccb.packets_.size());

    char record[14] = {22};
    err = client.impl_->write_dtls_data(record, sizeof(record));
    EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(2, (int)ccb.packets_.size());

    // An empty write sends nothing.
    HELPER_EXPECT_SUCCESS(client.impl_->write_dtls_data(record, 0));
    EXPECT_EQ(2, (int)ccb.packets_.size());
}

// Before initialize, the DTLS object has no session: it drops packets, and it is passive and already done.
VOID TEST(DtlsHandshakeTest, EmptyImplBeforeInitialize)
{
    srs_error_t err;

    MockDtlsCallbackForHandshake cb;
    SrsDtls dtls(&cb);
    char data[13] = {22};
    HELPER_EXPECT_SUCCESS(dtls.on_dtls(data, sizeof(data)));
    EXPECT_TRUE(cb.packets_.empty());
    EXPECT_TRUE(cb.app_data_.empty());

    SrsDtlsEmptyImpl impl;
    HELPER_EXPECT_SUCCESS(impl.initialize("dtls1.2", "active"));
    EXPECT_TRUE(impl.dtls_ == NULL);
    HELPER_EXPECT_SUCCESS(impl.on_dtls(data, sizeof(data)));
    EXPECT_TRUE(impl.handshake_done_for_us_);
    EXPECT_FALSE(impl.is_dtls_client());
    HELPER_EXPECT_SUCCESS(impl.start_arq());
    HELPER_EXPECT_SUCCESS(impl.on_handshake_done());

    string recv_key = "r", send_key = "s";
    HELPER_EXPECT_SUCCESS(impl.get_srtp_key(recv_key, send_key));
    EXPECT_STREQ("r", recv_key.c_str());
    EXPECT_STREQ("s", send_key.c_str());

    // No callback to notify.
    impl.callback_by_ssl("warning", "CN");
}

// An alert the handler fails to process does not fail the DTLS session.
VOID TEST(DtlsHandshakeTest, AlertHandlerErrorIsIgnored)
{
    srs_error_t err;

    MockDtlsCallbackForHandshake cb;
    cb.alert_error_ = srs_error_new(ERROR_RTC_DTLS, "mock alert error");
    SrsDtlsServerImpl impl(&cb);
    HELPER_ASSERT_SUCCESS(impl.initialize("dtls1.2", "passive"));

    impl.callback_by_ssl("fatal", "HF");
    ASSERT_EQ(1, (int)cb.alerts_.size());
    EXPECT_STREQ("fatal", cb.alerts_[0].first.c_str());
    EXPECT_STREQ("HF", cb.alerts_[0].second.c_str());
}

// SRTP with swapped keys: what one side protects, the other side unprotects, and a replayed or modified packet is
// rejected, see RFC 3711 section 3.3.
VOID TEST(SrtpTest, ProtectUnprotectWithSwappedKeys)
{
    srs_error_t err;

    string k1(30, '1'), k2(30, '2');
    SrsSRTP a, b;
    HELPER_ASSERT_SUCCESS(a.initialize(k2, k1));
    HELPER_ASSERT_SUCCESS(b.initialize(k1, k2));

    expect_srtp_round_trip(&a, &b, 100);
    expect_srtp_round_trip(&b, &a, 200);

    uint32_t storage[400];
    char *buf = (char *)storage;
    uint32_t replay_storage[400];
    char *replay = (char *)replay_storage;

    // Replay of a packet already received.
    int nb = build_rtp_packet(buf, 101, 20);
    HELPER_ASSERT_SUCCESS(a.protect_rtp(buf, &nb));
    memcpy(replay, buf, nb);
    int nb_replay = nb;
    HELPER_EXPECT_SUCCESS(b.unprotect_rtp(buf, &nb));
    err = b.unprotect_rtp(replay, &nb_replay);
    EXPECT_EQ(ERROR_RTC_SRTP_UNPROTECT, srs_error_code(err));
    srs_freep(err);

    // Modified payload fails the auth tag.
    nb = build_rtp_packet(buf, 102, 20);
    HELPER_ASSERT_SUCCESS(a.protect_rtp(buf, &nb));
    buf[20] ^= 0x01;
    err = b.unprotect_rtp(buf, &nb);
    EXPECT_EQ(ERROR_RTC_SRTP_UNPROTECT, srs_error_code(err));
    srs_freep(err);

    nb = build_rtcp_rr(buf);
    HELPER_ASSERT_SUCCESS(a.protect_rtcp(buf, &nb));
    buf[5] ^= 0x01;
    err = b.unprotect_rtcp(buf, &nb);
    EXPECT_EQ(ERROR_RTC_SRTP_UNPROTECT, srs_error_code(err));
    srs_freep(err);

    // A packet shorter than its header.
    nb = 4;
    err = a.protect_rtp(buf, &nb);
    EXPECT_EQ(ERROR_RTC_SRTP_PROTECT, srs_error_code(err));
    srs_freep(err);

    nb = 4;
    err = a.protect_rtcp(buf, &nb);
    EXPECT_EQ(ERROR_RTC_SRTP_PROTECT, srs_error_code(err));
    srs_freep(err);
}

// Before the DTLS handshake initializes it, SRTP refuses to protect or unprotect.
VOID TEST(SrtpTest, NotReadyBeforeInitialize)
{
    srs_error_t err;

    SrsSRTP srtp;
    char buf[1500];
    int nb = build_rtcp_rr(buf);

    err = srtp.protect_rtp(buf, &nb);
    EXPECT_EQ(ERROR_RTC_SRTP_PROTECT, srs_error_code(err));
    srs_freep(err);

    err = srtp.protect_rtcp(buf, &nb);
    EXPECT_EQ(ERROR_RTC_SRTP_PROTECT, srs_error_code(err));
    srs_freep(err);

    err = srtp.unprotect_rtp(buf, &nb);
    EXPECT_EQ(ERROR_RTC_SRTP_UNPROTECT, srs_error_code(err));
    srs_freep(err);

    err = srtp.unprotect_rtcp(buf, &nb);
    EXPECT_EQ(ERROR_RTC_SRTP_UNPROTECT, srs_error_code(err));
    srs_freep(err);

    EXPECT_EQ(8, nb);
}

// The session manager with mocks for every collaborator a session creation goes through.
class RtcCreateSessionEnv
{
public:
    MockResourceManagerForUpdateSessions conns_;
    MockStreamPublishTokenManager tokens_;
    MockRtcSourceManager sources_;
    MockAppFactoryForCreateSession factory_;
    SrsRtcSessionManager manager_;
    SrsRtcUserConfig ruc_;
    SrsSdp local_sdp_;

public:
    RtcCreateSessionEnv(bool publish)
    {
        manager_.conn_manager_ = &conns_;
        manager_.stream_publish_tokens_ = &tokens_;
        manager_.rtc_sources_ = &sources_;
        manager_.app_factory_ = &factory_;

        ruc_.req_->vhost_ = "__defaultVhost__";
        ruc_.req_->app_ = "live";
        ruc_.req_->stream_ = "livestream";
        ruc_.publish_ = publish;
        ruc_.dtls_ = true;
        ruc_.srtp_ = false;
    }
    ~RtcCreateSessionEnv()
    {
        manager_.conn_manager_ = NULL;
        manager_.stream_publish_tokens_ = NULL;
        manager_.rtc_sources_ = NULL;
        manager_.app_factory_ = NULL;
    }
};

// A publisher session negotiates its tracks, enables them, generates the answer, waits for STUN, and is registered by
// its ICE username, holding the publish token of the stream.
VOID TEST(RtcSessionManagerTest, CreatePublisherSession)
{
    srs_error_t err;

    RtcCreateSessionEnv env(true);
    MockRtcConnectionForCreateSession *conn = new MockRtcConnectionForCreateSession();
    env.factory_.connection_ = conn;

    ISrsRtcConnection *session = NULL;
    HELPER_ASSERT_SUCCESS(env.manager_.create_rtc_session(&env.ruc_, env.local_sdp_, &session));
    EXPECT_EQ((ISrsRtcConnection *)conn, session);

    ASSERT_EQ(8, (int)conn->calls_.size());
    EXPECT_STREQ("add_publisher", conn->calls_[0].c_str());
    EXPECT_STREQ("set_all_tracks_status", conn->calls_[1].c_str());
    EXPECT_STREQ("generate_local_sdp", conn->calls_[2].c_str());
    EXPECT_STREQ("set_remote_sdp", conn->calls_[3].c_str());
    EXPECT_STREQ("set_local_sdp", conn->calls_[4].c_str());
    EXPECT_STREQ("set_state_as_waiting_stun", conn->calls_[5].c_str());
    EXPECT_STREQ("initialize", conn->calls_[6].c_str());
    EXPECT_STREQ("set_publish_token", conn->calls_[7].c_str());

    EXPECT_STREQ(env.ruc_.req_->get_stream_url().c_str(), conn->last_tracks_stream_.c_str());
    EXPECT_TRUE(conn->last_tracks_publish_);
    EXPECT_TRUE(conn->last_tracks_status_);
    EXPECT_TRUE(conn->last_init_dtls_);
    EXPECT_FALSE(conn->last_init_srtp_);
    EXPECT_STREQ("srvufrag:cliufrag", conn->last_init_username_.c_str());

    EXPECT_EQ(1, env.tokens_.acquire_token_count_);
    EXPECT_TRUE(conn->publish_token_.get() != NULL);
    EXPECT_EQ(1, env.sources_.fetch_or_create_count_);
    EXPECT_EQ((ISrsResource *)conn, env.conns_.name_map_["srvufrag:cliufrag"]);

    srs_freep(conn);
}

// A player session takes no publish token.
VOID TEST(RtcSessionManagerTest, CreatePlayerSession)
{
    srs_error_t err;

    RtcCreateSessionEnv env(false);
    MockRtcConnectionForCreateSession *conn = new MockRtcConnectionForCreateSession();
    env.factory_.connection_ = conn;

    ISrsRtcConnection *session = NULL;
    HELPER_ASSERT_SUCCESS(env.manager_.create_rtc_session(&env.ruc_, env.local_sdp_, &session));
    EXPECT_EQ((ISrsRtcConnection *)conn, session);

    ASSERT_EQ(7, (int)conn->calls_.size());
    EXPECT_STREQ("add_player", conn->calls_[0].c_str());
    EXPECT_STREQ("initialize", conn->calls_[6].c_str());
    EXPECT_FALSE(conn->last_tracks_publish_);
    EXPECT_TRUE(conn->last_tracks_status_);

    EXPECT_EQ(0, env.tokens_.acquire_token_count_);
    EXPECT_TRUE(conn->publish_token_.get() == NULL);
    EXPECT_EQ((ISrsResource *)conn, env.conns_.name_map_["srvufrag:cliufrag"]);

    srs_freep(conn);
}

// A second publisher of a stream that is being published is refused, and releases the token it took.
VOID TEST(RtcSessionManagerTest, CreatePublisherSessionWhenStreamBusy)
{
    srs_error_t err;

    RtcCreateSessionEnv env(true);
    SrsStreamPublishToken *token = new SrsStreamPublishToken(env.ruc_.req_->get_stream_url(), &env.tokens_);
    token->acquired_ = true;
    env.tokens_.token_to_return_ = token;
    env.sources_.mock_source_->set_stream_created();

    ISrsRtcConnection *session = NULL;
    err = env.manager_.create_rtc_session(&env.ruc_, env.local_sdp_, &session);
    EXPECT_EQ(ERROR_RTC_SOURCE_BUSY, srs_error_code(err));
    srs_freep(err);

    EXPECT_TRUE(session == NULL);
    EXPECT_EQ(0, env.factory_.create_rtc_connection_count_);
    EXPECT_EQ(1, env.tokens_.release_token_count_);
    EXPECT_TRUE(env.conns_.name_map_.empty());
}

// A publisher that cannot take the token, or whose source cannot be created, gets no session.
VOID TEST(RtcSessionManagerTest, CreateSessionFailsBeforeConnection)
{
    srs_error_t err;

    if (true) {
        RtcCreateSessionEnv env(true);
        srs_error_t token_error = srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "mock token busy");
        env.tokens_.set_acquire_token_error(token_error);
        srs_freep(token_error);

        ISrsRtcConnection *session = NULL;
        err = env.manager_.create_rtc_session(&env.ruc_, env.local_sdp_, &session);
        EXPECT_EQ(ERROR_SYSTEM_STREAM_BUSY, srs_error_code(err));
        srs_freep(err);
        EXPECT_EQ(0, env.sources_.fetch_or_create_count_);
        EXPECT_EQ(0, env.factory_.create_rtc_connection_count_);
    }

    if (true) {
        RtcCreateSessionEnv env(false);
        srs_error_t source_error = srs_error_new(ERROR_RTC_SOURCE_BUSY, "mock source error");
        env.sources_.set_fetch_or_create_error(source_error);
        srs_freep(source_error);

        ISrsRtcConnection *session = NULL;
        err = env.manager_.create_rtc_session(&env.ruc_, env.local_sdp_, &session);
        EXPECT_EQ(ERROR_RTC_SOURCE_BUSY, srs_error_code(err));
        srs_freep(err);
        EXPECT_EQ(0, env.factory_.create_rtc_connection_count_);
    }
}

// A session that fails while it is set up is freed and never registered, whichever step fails.
VOID TEST(RtcSessionManagerTest, CreateSessionFreesFailedConnection)
{
    srs_error_t err;

    for (int step = 0; step < 4; step++) {
        RtcCreateSessionEnv env(step != 1);
        MockRtcConnectionForCreateSession *conn = new MockRtcConnectionForCreateSession();
        bool destroyed = false;
        conn->destroyed_ = &destroyed;
        env.factory_.connection_ = conn;

        srs_error_t mock_error = srs_error_new(ERROR_RTC_SDP_EXCHANGE, "mock step %d error", step);
        if (step == 0) {
            conn->add_publisher_error_ = mock_error;
        } else if (step == 1) {
            conn->add_player_error_ = mock_error;
        } else if (step == 2) {
            conn->generate_local_sdp_error_ = mock_error;
        } else {
            conn->initialize_error_ = mock_error;
        }

        ISrsRtcConnection *session = NULL;
        err = env.manager_.create_rtc_session(&env.ruc_, env.local_sdp_, &session);
        EXPECT_EQ(ERROR_RTC_SDP_EXCHANGE, srs_error_code(err)) << "step " << step;
        srs_freep(err);

        EXPECT_TRUE(session == NULL) << "step " << step;
        EXPECT_TRUE(destroyed) << "step " << step;
        EXPECT_TRUE(env.conns_.name_map_.empty()) << "step " << step;
        EXPECT_TRUE(env.conns_.resources_.empty()) << "step " << step;
    }
}

// A task handed to the session manager runs on its async worker.
VOID TEST(RtcSessionManagerTest, ExecRtcAsyncWorkRunsTask)
{
    srs_error_t err;

    SrsRtcSessionManager manager;
    HELPER_ASSERT_SUCCESS(manager.initialize());

    int calls = 0;
    HELPER_EXPECT_SUCCESS(manager.exec_rtc_async_work(new MockAsyncCallTaskForSessionManager(&calls)));

    for (int i = 0; i < 100 && calls == 0; i++) {
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }
    EXPECT_EQ(1, calls);
}

// The UDP packets of a session go to its network by kind: RTP, RTCP, DTLS and STUN responses. RTP is the hot path,
// which switches to the session context only on error.
VOID TEST(RtcSessionManagerTest, OnUdpPacketDispatchesByKind)
{
    srs_error_t err;

    MockResourceManagerForUpdateSessions conns;
    MockRtcNetworkForUdpNetwork network;
    MockRtcConnectionForUpdateSessions conn;
    conn.udp_network_ = &network;
    conns.fast_id_map_[12345] = &conn;

    SrsRtcSessionManager manager;
    manager.conn_manager_ = &conns;

    MockUdpMuxSocket skt;
    skt.fast_id_ = 12345;
    skt.peer_id_ = "127.0.0.1:50000";

    char pkt[32];

    // RTP.
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = (char)0x80;
    pkt[1] = 96;
    skt.data_ = pkt;
    skt.size_ = 20;
    HELPER_EXPECT_SUCCESS(manager.on_udp_packet(&skt));
    EXPECT_TRUE(network.on_rtp_called_);
    EXPECT_TRUE(conn.alive_called_);
    EXPECT_FALSE(conn.switch_to_context_called_);

    // RTP the network rejects.
    network.on_rtp_error_ = srs_error_new(ERROR_RTC_RTP, "mock rtp error");
    err = manager.on_udp_packet(&skt);
    EXPECT_EQ(ERROR_RTC_RTP, srs_error_code(err));
    srs_freep(err);
    EXPECT_TRUE(conn.switch_to_context_called_);

    // RTCP, a sender report(200).
    conn.switch_to_context_called_ = false;
    pkt[1] = (char)200;
    HELPER_EXPECT_SUCCESS(manager.on_udp_packet(&skt));
    EXPECT_TRUE(network.on_rtcp_called_);
    EXPECT_TRUE(conn.switch_to_context_called_);

    // DTLS, a handshake record(22).
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 22;
    HELPER_EXPECT_SUCCESS(manager.on_udp_packet(&skt));
    EXPECT_TRUE(network.on_dtls_called_);

    // STUN binding success response, which is not a binding request.
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x01;
    pkt[1] = 0x01;
    pkt[4] = 0x21;
    pkt[5] = 0x12;
    pkt[6] = (char)0xa4;
    pkt[7] = 0x42;
    HELPER_EXPECT_SUCCESS(manager.on_udp_packet(&skt));
    EXPECT_TRUE(network.on_stun_called_);

    // Neither RTP, RTCP, DTLS nor STUN.
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x70;
    err = manager.on_udp_packet(&skt);
    EXPECT_EQ(ERROR_RTC_UDP, srs_error_code(err));
    srs_freep(err);

    manager.conn_manager_ = NULL;
    conn.udp_network_ = NULL;
    skt.data_ = NULL;
}

// A STUN packet that does not decode, or a binding request for no session, is refused.
VOID TEST(RtcSessionManagerTest, OnUdpPacketRefusesBadStun)
{
    srs_error_t err;

    MockResourceManagerForUpdateSessions conns;
    SrsRtcSessionManager manager;
    manager.conn_manager_ = &conns;

    MockUdpMuxSocket skt;
    skt.fast_id_ = 12345;
    skt.peer_id_ = "127.0.0.1:50000";

    char pkt[20];
    memset(pkt, 0, sizeof(pkt));
    pkt[1] = 0x01;
    pkt[4] = 0x21;
    pkt[5] = 0x12;
    pkt[6] = (char)0xa4;
    pkt[7] = 0x42;
    skt.data_ = pkt;

    // Shorter than the STUN header.
    skt.size_ = 10;
    err = manager.on_udp_packet(&skt);
    EXPECT_EQ(ERROR_RTC_STUN, srs_error_code(err));
    srs_freep(err);

    // A binding request, with no session by username or address.
    skt.size_ = 20;
    err = manager.on_udp_packet(&skt);
    EXPECT_EQ(ERROR_RTC_STUN, srs_error_code(err));
    srs_freep(err);

    manager.conn_manager_ = NULL;
    skt.data_ = NULL;
}

// The black hole copies each RTC packet to its UDP address, for capture by a tool such as Wireshark.
VOID TEST(RtcBlackholeTest, CopiesPacketToAddress)
{
    srs_error_t err;

    srs_netfd_t lfd = NULL;
    int port = 0;
    HELPER_ASSERT_SUCCESS(srs_utest_udp_listen("127.0.0.1", &port, &lfd));

    MockAppConfigForRtcBlackhole config;
    config.black_hole_ = true;
    config.black_hole_addr_ = "127.0.0.1:" + srs_strconv_format_int(port);

    if (true) {
        SrsRtcBlackhole blackhole;
        blackhole.config_ = &config;
        HELPER_EXPECT_SUCCESS(blackhole.initialize());
        EXPECT_TRUE(blackhole.blackhole_);

        char data[] = "rtc-packet";
        blackhole.sendto(data, sizeof(data));

        char buf[64];
        sockaddr_in from;
        int fromlen = sizeof(from);
        int n = srs_recvfrom(lfd, buf, sizeof(buf), (sockaddr *)&from, &fromlen, 1 * SRS_UTIME_SECONDS);
        EXPECT_EQ((int)sizeof(data), n);
        if (n == (int)sizeof(data)) {
            EXPECT_EQ(0, memcmp(data, buf, n));
        }

        blackhole.config_ = NULL;
    }

    srs_close_stfd(lfd);
}

// Without auto detection, the first IPv4 address is the candidate, and an IPv6 only host uses its first address.
VOID TEST(RtcServerTest, DiscoverCandidatesWithoutBestMatch)
{
    MockAppConfigForDiscoverCandidates config;
    config.rtc_server_candidates_ = "*";
    config.use_auto_detect_network_ip_ = false;

    SrsRtcUserConfig ruc;

    if (true) {
        MockProtocolUtility utility;
        utility.add_ip("fe80::1", "eth0", false, false, false);
        utility.add_ip("192.168.1.100", "eth0", true, false, false);
        utility.add_ip("10.0.0.1", "eth1", true, false, false);

        set<string> candidates = discover_candidates(&utility, &config, &ruc);
        ASSERT_EQ(1, (int)candidates.size());
        EXPECT_STREQ("192.168.1.100", candidates.begin()->c_str());
    }

    if (true) {
        MockProtocolUtility utility;
        utility.add_ip("fe80::1", "eth0", false, false, false);
        utility.add_ip("fe80::2", "eth1", false, false, false);

        set<string> candidates = discover_candidates(&utility, &config, &ruc);
        ASSERT_EQ(1, (int)candidates.size());
        EXPECT_STREQ("fe80::1", candidates.begin()->c_str());
    }
}

// A resolved API host is a candidate, unless it resolves to an address such as "::".
VOID TEST(ApiServerAsCandidatesTest, ResolvesHostWhichIsNotIPv4)
{
    srs_error_t err;

    MockAppConfig config;
    config.set_api_as_candidates(true);
    config.set_keep_api_domain(false);
    config.set_resolve_api_domain(true);

    set<string> candidates;
    HELPER_EXPECT_SUCCESS(api_server_as_candidates(&config, "0:0:0:0:0:0:0:2", candidates));
    ASSERT_EQ(1, (int)candidates.size());
    EXPECT_STREQ("::2", candidates.begin()->c_str());

    candidates.clear();
    HELPER_EXPECT_SUCCESS(api_server_as_candidates(&config, "0:0:0:0:0:0:0:0", candidates));
    EXPECT_TRUE(candidates.empty());
}

// A binding request from a peer in the ice-controlled role is refused when the strict check is on, because an
// ice-lite server must be controlled, see RFC 8445 section 6.1.1.
VOID TEST(SrsRtcConnectionTest, OnBindingRequestRefusesIceControlledPeerWhenStrict)
{
    srs_error_t err;

    MockAppConfigForStunStrictCheck config;
    MockRtcAsyncTaskExecutor exec;
    SrsContextId cid;
    cid.set_value("test-rtc-connection-strict-stun");
    SrsUniquePtr<SrsRtcConnection> conn(new SrsRtcConnection(&exec, cid));
    conn->config_ = &config;

    SrsRequest *req = new SrsRequest();
    req->vhost_ = "__defaultVhost__";
    conn->req_ = req;
    conn->local_sdp_.session_info_.ice_pwd_ = "local-ice-pwd";

    SrsStunPacket r;
    r.ice_controlled_ = true;
    string ice_pwd;
    err = conn->on_binding_request(&r, ice_pwd);
    EXPECT_EQ(ERROR_RTC_STUN, srs_error_code(err));
    srs_freep(err);
    EXPECT_TRUE(ice_pwd.empty());

    conn->config_ = NULL;
}

// A session is alive while binding requests arrive within its timeout.
VOID TEST(SrsRtcConnectionTest, AliveUntilSessionTimeout)
{
    MockRtcAsyncTaskExecutor exec;
    SrsContextId cid;
    cid.set_value("test-rtc-connection-alive");
    SrsUniquePtr<SrsRtcConnection> conn(new SrsRtcConnection(&exec, cid));

    conn->session_timeout_ = 30 * SRS_UTIME_SECONDS;
    conn->last_stun_time_ = 0;
    EXPECT_FALSE(conn->is_alive());

    conn->alive();
    EXPECT_EQ(srs_time_now_cached(), conn->last_stun_time_);
    EXPECT_TRUE(conn->is_alive());

    conn->session_timeout_ = 0;
    EXPECT_FALSE(conn->is_alive());

    EXPECT_TRUE(conn->tcp() != NULL);
    EXPECT_TRUE(conn->tcp() != conn->udp());
}

// An RTCP packet that the network fails to write fails the send.
VOID TEST(SrsRtcConnectionTest, SendRtcpFailsWhenWriteFails)
{
    srs_error_t err;

    MockRtcAsyncTaskExecutor exec;
    SrsContextId cid;
    cid.set_value("test-rtc-connection-send-rtcp-write");

    MockRtcNetwork network;
    MockRtcNetworksForConnection networks(&network);
    MockRtcBlackholeForSecurityTransport blackhole;

    SrsUniquePtr<SrsRtcConnection> conn(new SrsRtcConnection(&exec, cid));
    srs_freep(conn->networks_);
    conn->networks_ = &networks;
    conn->blackhole_ = &blackhole;

    srs_error_t write_error = srs_error_new(ERROR_SOCKET_WRITE, "mock write error");
    network.set_write_error(write_error);
    srs_freep(write_error);

    char data[] = {(char)0x80, (char)0xc8, 0x00, 0x06, 0x12, 0x34, 0x56, 0x78};
    err = conn->send_rtcp(data, sizeof(data));
    EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(1, network.protect_rtcp_count_);
    EXPECT_EQ(1, network.write_count_);

    conn->networks_ = NULL;
    conn->blackhole_ = NULL;
}

// A PLI from the player asks the publisher of that video for a keyframe. The other payload specific feedbacks are
// accepted and ignored, and an unknown one is an error, see RFC 4585 section 6.3.
VOID TEST(RtcPlayStreamTest, PayloadSpecificFeedbackRequestsKeyframe)
{
    srs_error_t err;

    MockAppConfig config;
    MockRtcSourceManager sources;
    MockAppStatistic stat;
    MockRtcAsyncCallRequest req("test.vhost", "live", "stream1");
    MockRtcAsyncTaskExecutor exec;
    MockExpire expire;
    MockRtcPacketSender sender;

    SrsContextId cid;
    cid.set_value("test-play-stream-psfb");
    SrsUniquePtr<SrsRtcPlayStream> play(new SrsRtcPlayStream(&exec, &expire, &sender, cid));
    play->config_ = &config;
    play->rtc_sources_ = &sources;
    play->stat_ = &stat;

    SrsRtcTrackDescription *video_desc = new SrsRtcTrackDescription();
    video_desc->type_ = "video";
    video_desc->id_ = "video-track-id";
    video_desc->ssrc_ = 67890;
    video_desc->is_active_ = true;
    std::map<uint32_t, SrsRtcTrackDescription *> sub_relations;
    sub_relations[67890] = video_desc;
    HELPER_ASSERT_SUCCESS(play->initialize(&req, sub_relations));
    srs_freep(video_desc);

    MockRtcPliWorker *pli_worker = new MockRtcPliWorker(play.get());
    srs_freep(play->pli_worker_);
    play->pli_worker_ = pli_worker;

    EXPECT_EQ(0, play->context_id().compare(cid));

    SrsRtcpFbCommon fb;
    fb.header_.rc = kPLI;
    fb.set_media_ssrc(67890);
    HELPER_EXPECT_SUCCESS(play->on_rtcp_ps_feedback(&fb));
    EXPECT_EQ(1, pli_worker->request_keyframe_count_);
    EXPECT_TRUE(pli_worker->has_keyframe_request(67890, cid));

    // A PLI for a video this player does not send.
    fb.set_media_ssrc(11111);
    HELPER_EXPECT_SUCCESS(play->on_rtcp_ps_feedback(&fb));
    EXPECT_EQ(1, pli_worker->request_keyframe_count_);

    uint8_t ignored[] = {kSLI, kRPSI, kAFB};
    for (int i = 0; i < (int)sizeof(ignored); i++) {
        fb.header_.rc = ignored[i];
        fb.set_media_ssrc(67890);
        HELPER_EXPECT_SUCCESS(play->on_rtcp_ps_feedback(&fb));
    }
    EXPECT_EQ(1, pli_worker->request_keyframe_count_);

    fb.header_.rc = 7;
    err = play->on_rtcp_ps_feedback(&fb);
    EXPECT_EQ(ERROR_RTC_RTCP, srs_error_code(err));
    srs_freep(err);

    play->config_ = NULL;
    play->rtc_sources_ = NULL;
    play->stat_ = NULL;
}

// A publish stream and its unpublish task carry the context of the publisher.
VOID TEST(RtcPublishStreamTest, ContextIdOfPublisher)
{
    MockRtcAsyncTaskExecutor exec;
    MockExpire expire;
    MockRtcPacketReceiver receiver;
    SrsContextId cid;
    cid.set_value("test-publish-stream-cid");

    SrsUniquePtr<SrsRtcPublishStream> publish(new SrsRtcPublishStream(&exec, &expire, &receiver, cid));
    EXPECT_EQ(0, publish->context_id().compare(cid));

    MockRtcAsyncCallRequest req("test.vhost", "live", "stream1");
    SrsStatisticStreamSnapshot snapshot;
    SrsRtcAsyncCallOnUnpublish task(cid, &req, snapshot);
    EXPECT_TRUE(task.to_string().empty());
    EXPECT_EQ(0, task.cid_.compare(cid));
}

// A minimal SDP that parses.
#define AI37_SDP "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n"

static string ai37_rtc_api_body(bool with_streamurl, bool with_clientip)
{
    SrsUniquePtr<SrsJsonObject> body(SrsJsonAny::object());
    body->set("sdp", SrsJsonAny::str(AI37_SDP));
    if (with_streamurl) {
        body->set("streamurl", SrsJsonAny::str("webrtc://test.vhost/live/livestream"));
    }
    if (with_clientip) {
        body->set("clientip", SrsJsonAny::str("192.168.1.100"));
    }
    body->set("api", SrsJsonAny::str("http://127.0.0.1:1985/rtc/v1/play/"));
    body->set("tid", SrsJsonAny::str("tid-1"));
    return body->dumps();
}

static SrsConfDirective *ai37_default_vhost()
{
    SrsConfDirective *vhost = new SrsConfDirective();
    vhost->name_ = "vhost";
    vhost->args_.push_back("__defaultVhost__");
    return vhost;
}

// The deprecated JSON play API takes the client IP from the proxy header when the body has none, resolves the vhost by
// config, and takes encrypt, dtls, eip and codecs from the query.
VOID TEST(GoApiRtcPlayTest, JsonApiResolvesRequest)
{
    srs_error_t err;

    MockAppConfig config;
    config.default_vhost_ = ai37_default_vhost();
    MockStatisticForRtcApi stat;
    stat.server_id_ = "server-1";
    MockRtcApiServerForPlay server;

    MockRtcPlayForJsonApi api(&server);
    api.config_ = &config;
    api.stat_ = &stat;

    MockResponseWriter writer;
    MockHttpMessageForRtcApi message;
    message.body_content_ = ai37_rtc_api_body(true, false);
    message.mock_conn_->remote_ip_ = "10.0.0.1";
    message.header()->set("X-Forwarded-For", "1.2.3.4, 10.0.0.1");
    message.query_params_["encrypt"] = "false";
    message.query_params_["dtls"] = "false";
    message.query_params_["eip"] = "203.0.113.1";
    message.query_params_["codec"] = "h264";
    message.query_params_["acodec"] = "opus";

    HELPER_EXPECT_SUCCESS(api.serve_http(&writer, &message));
    EXPECT_EQ(1, api.serve_count_);
    EXPECT_STREQ("1.2.3.4", api.ip_.c_str());
    EXPECT_STREQ("__defaultVhost__", api.vhost_.c_str());
    EXPECT_STREQ("live", api.app_.c_str());
    EXPECT_STREQ("livestream", api.stream_.c_str());
    EXPECT_STREQ("http://127.0.0.1:1985/rtc/v1/play/", api.api_.c_str());
    EXPECT_STREQ("203.0.113.1", api.eip_.c_str());
    EXPECT_STREQ("h264", api.vcodec_.c_str());
    EXPECT_STREQ("opus", api.acodec_.c_str());
    EXPECT_FALSE(api.publish_);
    EXPECT_FALSE(api.dtls_);
    EXPECT_FALSE(api.srtp_);

    string response = string(writer.io.out_buffer.bytes(), writer.io.out_buffer.length());
    EXPECT_TRUE(response.find("\"code\":0") != string::npos) << response;
    EXPECT_TRUE(response.find("\"sdp\":\"answer-sdp\"") != string::npos) << response;
    EXPECT_TRUE(response.find("\"sessionid\":\"sid-1\"") != string::npos) << response;
    EXPECT_TRUE(response.find("\"server\":\"server-1\"") != string::npos) << response;

    // Without a proxy header, the client IP is the peer of the connection, and SRTP follows the config.
    MockResponseWriter writer2;
    MockHttpMessageForRtcApi message2;
    message2.body_content_ = ai37_rtc_api_body(true, false);
    message2.mock_conn_->remote_ip_ = "10.0.0.1";
    message2.query_params_["encrypt"] = "true";
    HELPER_EXPECT_SUCCESS(api.serve_http(&writer2, &message2));
    EXPECT_EQ(2, api.serve_count_);
    EXPECT_STREQ("10.0.0.1", api.ip_.c_str());
    EXPECT_TRUE(api.dtls_);
    EXPECT_TRUE(api.srtp_);

    api.config_ = NULL;
    api.stat_ = NULL;
}

// Every request the deprecated JSON play API cannot serve answers code 400.
VOID TEST(GoApiRtcPlayTest, JsonApiRefusesBadRequest)
{
    srs_error_t err;

    MockAppConfig config;
    MockRtcApiServerForPlay server;
    MockRtcPlayForJsonApi api(&server);
    api.config_ = &config;

    for (int i = 0; i < 5; i++) {
        MockResponseWriter writer;
        MockHttpMessageForRtcApi message;
        message.body_content_ = ai37_rtc_api_body(true, true);
        if (i == 0) {
            message.body_read_all_error_ = srs_error_new(ERROR_SOCKET_READ, "mock read error");
        } else if (i == 1) {
            message.body_content_ = "not-json";
        } else if (i == 2) {
            message.body_content_ = ai37_rtc_api_body(false, true);
        } else if (i == 3) {
            SrsUniquePtr<SrsJsonObject> body(SrsJsonAny::object());
            body->set("sdp", SrsJsonAny::str("garbage"));
            body->set("streamurl", SrsJsonAny::str("webrtc://test.vhost/live/livestream"));
            message.body_content_ = body->dumps();
        } else {
            api.serve_error_ = srs_error_new(ERROR_RTC_API_BODY, "mock serve error");
        }

        HELPER_EXPECT_SUCCESS(api.serve_http(&writer, &message));
        string response = string(writer.io.out_buffer.bytes(), writer.io.out_buffer.length());
        EXPECT_TRUE(response.find("\"code\":400") != string::npos) << "case " << i << ": " << response;
        EXPECT_EQ(i == 4 ? 1 : 0, api.serve_count_) << "case " << i;
    }

    api.config_ = NULL;
}

// The deprecated JSON publish API resolves the request as the play API does, marks it as a WebRTC upstream, and
// refuses what it cannot serve with code 400.
VOID TEST(GoApiRtcPublishTest, JsonApiResolvesAndRefuses)
{
    srs_error_t err;

    MockAppConfig config;
    config.default_vhost_ = ai37_default_vhost();
    MockStatisticForRtcApi stat;
    MockRtcApiServerForPlay server;

    MockRtcPublishForJsonApi api(&server);
    api.config_ = &config;
    api.stat_ = &stat;

    if (true) {
        MockResponseWriter writer;
        MockHttpMessageForRtcApi message;
        message.body_content_ = ai37_rtc_api_body(true, false);
        message.mock_conn_->remote_ip_ = "10.0.0.1";
        message.header()->set("X-Real-IP", "5.6.7.8");
        message.query_params_["encrypt"] = "false";
        message.query_params_["candidate"] = "203.0.113.2";
        message.query_params_["vcodec"] = "h265";

        HELPER_EXPECT_SUCCESS(api.serve_http(&writer, &message));
        EXPECT_EQ(1, api.serve_count_);
        EXPECT_STREQ("5.6.7.8", api.ip_.c_str());
        EXPECT_STREQ("__defaultVhost__", api.vhost_.c_str());
        EXPECT_TRUE(api.param_.find("upstream=rtc") != string::npos) << api.param_;
        EXPECT_STREQ("http://127.0.0.1:1985/rtc/v1/play/", api.api_.c_str());
        EXPECT_STREQ("203.0.113.2", api.eip_.c_str());
        EXPECT_STREQ("h265", api.vcodec_.c_str());
        EXPECT_TRUE(api.publish_);
        EXPECT_TRUE(api.dtls_);
        EXPECT_FALSE(api.srtp_);

        string response = string(writer.io.out_buffer.bytes(), writer.io.out_buffer.length());
        EXPECT_TRUE(response.find("\"sessionid\":\"sid-2\"") != string::npos) << response;
    }

    for (int i = 0; i < 5; i++) {
        MockResponseWriter writer;
        MockHttpMessageForRtcApi message;
        message.body_content_ = ai37_rtc_api_body(true, true);
        if (i == 0) {
            message.body_read_all_error_ = srs_error_new(ERROR_SOCKET_READ, "mock read error");
        } else if (i == 1) {
            message.body_content_ = "[1,2]";
        } else if (i == 2) {
            message.body_content_ = ai37_rtc_api_body(false, true);
        } else if (i == 3) {
            SrsUniquePtr<SrsJsonObject> body(SrsJsonAny::object());
            body->set("sdp", SrsJsonAny::str("garbage"));
            body->set("streamurl", SrsJsonAny::str("webrtc://test.vhost/live/livestream"));
            message.body_content_ = body->dumps();
        } else {
            api.serve_error_ = srs_error_new(ERROR_RTC_API_BODY, "mock serve error");
        }

        HELPER_EXPECT_SUCCESS(api.serve_http(&writer, &message));
        string response = string(writer.io.out_buffer.bytes(), writer.io.out_buffer.length());
        EXPECT_TRUE(response.find("\"code\":400") != string::npos) << "case " << i << ": " << response;
        EXPECT_EQ(i == 4 ? 2 : 1, api.serve_count_) << "case " << i;
    }

    api.config_ = NULL;
    api.stat_ = NULL;
}

// Each SRT option the acceptor sets on its listener reads back from libsrt.
VOID TEST(SrtOptionsTest, SetsEachOptionOnSocket)
{
    srs_error_t err;

    srs_srt_t fd = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(srs_srt_socket_with_default_option(&fd));

    SrsSrtOptions opts;

    HELPER_EXPECT_SUCCESS(opts.set_maxbw(fd, 1000000));
    int64_t maxbw = 0;
    HELPER_EXPECT_SUCCESS(srs_srt_get_maxbw(fd, maxbw));
    EXPECT_EQ(1000000, maxbw);

    HELPER_EXPECT_SUCCESS(opts.set_mss(fd, 1400));
    int mss = 0;
    HELPER_EXPECT_SUCCESS(srs_srt_get_mss(fd, mss));
    EXPECT_EQ(1400, mss);

    HELPER_EXPECT_SUCCESS(opts.set_payload_size(fd, 1316));
    int payload_size = 0;
    HELPER_EXPECT_SUCCESS(srs_srt_get_payload_size(fd, payload_size));
    EXPECT_EQ(1316, payload_size);

    HELPER_EXPECT_SUCCESS(opts.set_connect_timeout(fd, 4000));
    int connect_timeout = 0;
    HELPER_EXPECT_SUCCESS(srs_srt_get_connect_timeout(fd, connect_timeout));
    EXPECT_EQ(4000, connect_timeout);

    HELPER_EXPECT_SUCCESS(opts.set_peer_idle_timeout(fd, 8000));
    int peer_idle_timeout = 0;
    HELPER_EXPECT_SUCCESS(srs_srt_get_peer_idle_timeout(fd, peer_idle_timeout));
    EXPECT_EQ(8000, peer_idle_timeout);

    HELPER_EXPECT_SUCCESS(opts.set_tsbpdmode(fd, false));
    bool tsbpdmode = true;
    HELPER_EXPECT_SUCCESS(srs_srt_get_tsbpdmode(fd, tsbpdmode));
    EXPECT_FALSE(tsbpdmode);

    HELPER_EXPECT_SUCCESS(opts.set_sndbuf(fd, 2 * 1024 * 1024));
    int sndbuf = 0;
    HELPER_EXPECT_SUCCESS(srs_srt_get_sndbuf(fd, sndbuf));
    EXPECT_GT(sndbuf, 0);

    HELPER_EXPECT_SUCCESS(opts.set_rcvbuf(fd, 2 * 1024 * 1024));
    int rcvbuf = 0;
    HELPER_EXPECT_SUCCESS(srs_srt_get_rcvbuf(fd, rcvbuf));
    EXPECT_GT(rcvbuf, 0);

    HELPER_EXPECT_SUCCESS(opts.set_tlpktdrop(fd, false));
    bool tlpktdrop = true;
    HELPER_EXPECT_SUCCESS(srs_srt_get_tlpktdrop(fd, tlpktdrop));
    EXPECT_FALSE(tlpktdrop);

    HELPER_EXPECT_SUCCESS(opts.set_latency(fd, 200));
    int latency = 0;
    HELPER_EXPECT_SUCCESS(srs_srt_get_latency(fd, latency));
    EXPECT_EQ(200, latency);

    HELPER_EXPECT_SUCCESS(opts.set_rcv_latency(fd, 300));
    int rcv_latency = 0;
    HELPER_EXPECT_SUCCESS(srs_srt_get_rcv_latency(fd, rcv_latency));
    EXPECT_EQ(300, rcv_latency);

    HELPER_EXPECT_SUCCESS(opts.set_peer_latency(fd, 400));
    int peer_latency = 0;
    HELPER_EXPECT_SUCCESS(srs_srt_get_peer_latency(fd, peer_latency));
    EXPECT_EQ(400, peer_latency);

    HELPER_EXPECT_SUCCESS(opts.set_passphrase(fd, "0123456789abcdef"));
    HELPER_EXPECT_SUCCESS(opts.set_pbkeylen(fd, 16));

    HELPER_EXPECT_SUCCESS(srs_srt_close(fd));
}

// A socket in error fails a wait at once, without subscribing to the poller.
VOID TEST(SrtSocketTest, WaitFailsWhenSocketHasError)
{
    srs_error_t err;

    MockSrtPollerForSrtConnection poller;
    SrsSrtSocket skt(&poller, srs_srt_socket_invalid());
    skt.has_error_ = 1;

    err = skt.wait_readable();
    EXPECT_EQ(ERROR_SRT_IO, srs_error_code(err));
    srs_freep(err);

    err = skt.wait_writeable();
    EXPECT_EQ(ERROR_SRT_IO, srs_error_code(err));
    srs_freep(err);

    EXPECT_EQ(0, poller.add_socket_count_);
    EXPECT_EQ(0, poller.mod_socket_count_);
}

// A wait fails when the poller cannot subscribe the socket.
VOID TEST(SrtSocketTest, WaitFailsWhenPollerRefusesSocket)
{
    srs_error_t err;

    if (true) {
        MockSrtPollerForSrtConnection poller;
        poller.add_socket_error_ = srs_error_new(ERROR_SRT_EPOLL, "mock add error");
        SrsSrtSocket skt(&poller, srs_srt_socket_invalid());

        err = skt.wait_readable();
        EXPECT_EQ(ERROR_SRT_EPOLL, srs_error_code(err));
        srs_freep(err);
        EXPECT_EQ(1, poller.add_socket_count_);
    }

    if (true) {
        MockSrtPollerForSrtConnection poller;
        poller.add_socket_error_ = srs_error_new(ERROR_SRT_EPOLL, "mock add error");
        SrsSrtSocket skt(&poller, srs_srt_socket_invalid());

        err = skt.wait_writeable();
        EXPECT_EQ(ERROR_SRT_EPOLL, srs_error_code(err));
        srs_freep(err);
        EXPECT_EQ(1, poller.add_socket_count_);
    }
}

// A wait that times out reports the timeout, even when the poller then fails to unsubscribe the event.
VOID TEST(SrtSocketTest, WaitTimeoutIgnoresUnsubscribeError)
{
    srs_error_t err;

    MockSrtPollerForSrtConnection poller;
    poller.mod_socket_error_ = srs_error_new(ERROR_SRT_EPOLL, "mock mod error");
    SrsSrtSocket skt(&poller, srs_srt_socket_invalid());
    skt.recv_timeout_ = 1 * SRS_UTIME_MILLISECONDS;
    skt.send_timeout_ = 1 * SRS_UTIME_MILLISECONDS;

    err = skt.wait_readable();
    EXPECT_EQ(ERROR_SRT_TIMEOUT, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(1, poller.add_socket_count_);
    EXPECT_EQ(1, poller.mod_socket_count_);

    MockSrtPollerForSrtConnection poller2;
    poller2.mod_socket_error_ = srs_error_new(ERROR_SRT_EPOLL, "mock mod error");
    SrsSrtSocket skt2(&poller2, srs_srt_socket_invalid());
    skt2.send_timeout_ = 1 * SRS_UTIME_MILLISECONDS;

    err = skt2.wait_writeable();
    EXPECT_EQ(ERROR_SRT_TIMEOUT, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(1, poller2.add_socket_count_);
    EXPECT_EQ(1, poller2.mod_socket_count_);
}

// A coroutine waiting on the socket returns when it is interrupted, or when the poller reports an error on the socket.
VOID TEST(SrtSocketTest, WaitEndsByInterruptOrError)
{
    srs_error_t err;

    for (int i = 0; i < 2; i++) {
        bool readable = (i == 0);

        if (true) {
            MockSrtPollerForSrtConnection poller;
            SrsSrtSocket skt(&poller, srs_srt_socket_invalid());
            skt.recv_timeout_ = skt.send_timeout_ = 10 * SRS_UTIME_SECONDS;

            MockSrtSocketWaiter waiter(&skt, readable);
            SrsSTCoroutine trd("srt-wait", &waiter);
            HELPER_ASSERT_SUCCESS(trd.start());
            srs_usleep(1 * SRS_UTIME_MILLISECONDS);
            EXPECT_FALSE(waiter.done_);

            trd.stop();
            EXPECT_TRUE(waiter.done_);
            EXPECT_EQ(ERROR_SRT_INTERRUPT, srs_error_code(waiter.err_)) << "readable=" << readable;
        }

        if (true) {
            MockSrtPollerForSrtConnection poller;
            SrsSrtSocket skt(&poller, srs_srt_socket_invalid());
            skt.recv_timeout_ = skt.send_timeout_ = 10 * SRS_UTIME_SECONDS;

            MockSrtSocketWaiter waiter(&skt, readable);
            SrsSTCoroutine trd("srt-wait", &waiter);
            HELPER_ASSERT_SUCCESS(trd.start());
            srs_usleep(1 * SRS_UTIME_MILLISECONDS);
            EXPECT_FALSE(waiter.done_);

            skt.notify_error();
            for (int j = 0; j < 100 && !waiter.done_; j++) {
                srs_usleep(1 * SRS_UTIME_MILLISECONDS);
            }
            EXPECT_TRUE(waiter.done_);
            EXPECT_EQ(ERROR_SRT_IO, srs_error_code(waiter.err_)) << "readable=" << readable;
            trd.stop();
        }
    }
}

// The socket subscribes an event once, and unsubscribes it once, removing the socket from the poller with its last
// event.
VOID TEST(SrtSocketTest, EnableAndDisableEventOnce)
{
    srs_error_t err;

    MockSrtPollerForSrtConnection poller;
    if (true) {
        SrsSrtSocket skt(&poller, srs_srt_socket_invalid());

        HELPER_EXPECT_SUCCESS(skt.enable_event(SRT_EPOLL_IN));
        HELPER_EXPECT_SUCCESS(skt.enable_event(SRT_EPOLL_IN));
        EXPECT_EQ(1, poller.add_socket_count_);
        EXPECT_EQ(0, poller.mod_socket_count_);
        EXPECT_EQ(SRT_EPOLL_IN, skt.events_);

        HELPER_EXPECT_SUCCESS(skt.disable_event(SRT_EPOLL_IN));
        HELPER_EXPECT_SUCCESS(skt.disable_event(SRT_EPOLL_IN));
        EXPECT_EQ(1, poller.del_socket_count_);
        EXPECT_EQ(0, skt.events_);
    }

    // And the socket removes itself when closed.
    EXPECT_EQ(2, poller.del_socket_count_);
}

// The socket is closed even when the poller fails to remove it.
VOID TEST(SrtSocketTest, CloseIgnoresPollerRemoveError)
{
    MockSrtPollerForSrtConnection poller;
    poller.del_socket_error_ = srs_error_new(ERROR_SRT_EPOLL, "mock del error");
    if (true) {
        SrsSrtSocket skt(&poller, srs_srt_socket_invalid());
    }
    EXPECT_EQ(1, poller.del_socket_count_);
}

// Connect, receive and send fail on a socket libsrt does not know, without waiting.
VOID TEST(SrtSocketTest, IoFailsOnInvalidSocket)
{
    srs_error_t err;

    MockSrtPollerForSrtConnection poller;
    SrsSrtSocket skt(&poller, srs_srt_socket_invalid());

    err = skt.connect("127.0.0.1", srs_utest_random_port());
    EXPECT_EQ(ERROR_SRT_IO, srs_error_code(err));
    srs_freep(err);

    char buf[16] = {0};
    ssize_t n = 0;
    err = skt.recvmsg(buf, sizeof(buf), &n);
    EXPECT_EQ(ERROR_SRT_IO, srs_error_code(err));
    srs_freep(err);

    err = skt.sendmsg(buf, sizeof(buf), &n);
    EXPECT_EQ(ERROR_SRT_IO, srs_error_code(err));
    srs_freep(err);

    EXPECT_EQ(0, n);
    EXPECT_EQ(0, poller.add_socket_count_);
}

// The SRT event loop keeps polling when a poll fails, and stops with its poller when freed.
VOID TEST(SrtEventLoopTest, KeepsPollingWhenPollFails)
{
    srs_error_t err;

    MockSrtPollerForSrtConnection *poller = new MockSrtPollerForSrtConnection();
    poller->wait_error_ = srs_error_new(ERROR_SRT_EPOLL, "mock wait error");

    if (true) {
        SrsSrtEventLoop loop;
        loop.srt_poller_ = poller;
        EXPECT_EQ((ISrsSrtPoller *)poller, loop.poller());
        HELPER_ASSERT_SUCCESS(loop.start());

        for (int i = 0; i < 200 && poller->wait_count_ < 2; i++) {
            srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        }
        EXPECT_GE(poller->wait_count_, 2);
    }
}

// The acceptor hands each SRT client to its handler, and keeps accepting when the handler refuses one.
VOID TEST(SrtAcceptorTest, HandsClientToHandler)
{
    srs_error_t err;

    MockSrtClientHandlerForAcceptor handler;
    SrsSrtAcceptor acceptor(&handler);

    HELPER_EXPECT_SUCCESS(acceptor.on_srt_client(100));
    ASSERT_EQ(1, (int)handler.fds_.size());
    EXPECT_EQ(100, handler.fds_[0]);

    handler.accept_error_ = srs_error_new(ERROR_SRT_CONN, "mock accept error");
    HELPER_EXPECT_SUCCESS(acceptor.on_srt_client(101));
    ASSERT_EQ(2, (int)handler.fds_.size());
    EXPECT_EQ(101, handler.fds_[1]);
}

// The hostname is the system one, and is cached.
VOID TEST(ProtocolUtilityTest, SystemHostname)
{
    SrsProtocolUtility utility;

    string saved = _srs_system_hostname;
    _srs_system_hostname = "";

    char buf[256];
    memset(buf, 0, sizeof(buf));
    ASSERT_EQ(0, gethostname(buf, sizeof(buf)));
    EXPECT_STREQ(buf, utility.system_hostname().c_str());
    EXPECT_STREQ(buf, _srs_system_hostname.c_str());

    _srs_system_hostname = "cached-host";
    EXPECT_STREQ("cached-host", utility.system_hostname().c_str());

    _srs_system_hostname = saved;
}

// With only a loopback address, or only an IPv6 address when IPv4 is asked, the first address is the public one.
VOID TEST(ProtocolUtilityTest, PublicAddressFallsBackToFirstAddress)
{
    SrsProtocolUtility utility;

    vector<SrsIPAddress *> saved_ips;
    saved_ips.swap(_srs_system_ips);
    string saved_address = _srs_public_internet_address;

    SrsIPAddress lo;
    lo.ifname_ = "lo";
    lo.ip_ = "127.0.0.1";
    lo.is_ipv4_ = true;
    lo.is_internet_ = false;
    lo.is_loopback_ = true;

    SrsIPAddress v6;
    v6.ifname_ = "eth0";
    v6.ip_ = "fd00::1";
    v6.is_ipv4_ = false;
    v6.is_internet_ = false;
    v6.is_loopback_ = false;

    _srs_system_ips.push_back(&lo);
    _srs_public_internet_address = "";
    EXPECT_STREQ("127.0.0.1", utility.public_internet_address(true).c_str());
    EXPECT_STREQ("127.0.0.1", _srs_public_internet_address.c_str());

    _srs_system_ips.clear();
    _srs_system_ips.push_back(&v6);
    _srs_public_internet_address = "";
    EXPECT_STREQ("fd00::1", utility.public_internet_address(true).c_str());

    _srs_system_ips.clear();
    _srs_system_ips.swap(saved_ips);
    _srs_public_internet_address = saved_address;
}

// IPv6 multicast is not an internet address, while a global unicast one is.
VOID TEST(ProtocolUtilityTest, IPv6MulticastIsNotInternet)
{
    SrsProtocolUtility utility;

    sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;

    ASSERT_EQ(1, inet_pton(AF_INET6, "ff02::1", &addr.sin6_addr));
    EXPECT_FALSE(utility.is_internet((sockaddr *)&addr));

    ASSERT_EQ(1, inet_pton(AF_INET6, "ff0e::1", &addr.sin6_addr));
    EXPECT_FALSE(utility.is_internet((sockaddr *)&addr));

    ASSERT_EQ(1, inet_pton(AF_INET6, "2001:4860:4860::8888", &addr.sin6_addr));
    EXPECT_TRUE(utility.is_internet((sockaddr *)&addr));
}

// Run the cycle of a WebRTC over TCP connection whose socket fails the read with the given error, or reads the given
// bytes, and return the result of the cycle.
static srs_error_t ai37_rtc_tcp_conn_cycle(srs_error_t read_error, string in, MockAppStatistic *stat, ISrsRtcConnection *session)
{
    MockBufferIO *io = new MockBufferIO();
    io->in_err = read_error;
    if (!in.empty()) {
        io->append(in);
    }

    MockContextIdSetterForTcpConnCycle cid_setter;
    SrsUniquePtr<SrsRtcTcpConn> conn(new SrsRtcTcpConn(io, "127.0.0.1", 50000));
    conn->assemble();
    conn->stat_ = stat;
    conn->setup_owner(NULL, NULL, &cid_setter);
    conn->session_ = session;

    srs_error_t err = conn->cycle();
    EXPECT_EQ(1, cid_setter.set_cid_count_);

    conn->stat_ = NULL;
    conn->session_ = NULL;
    return err;
}

// A WebRTC over TCP connection ends without an error whatever closed it: a timeout, a close with a message, the server
// or the client, or a packet it cannot serve. Each end is counted as a disconnect.
VOID TEST(RtcTcpConnTest, CycleEndsQuietlyWhateverClosedIt)
{
    srs_error_t err;

    int codes[] = {ERROR_SOCKET_TIMEOUT, ERROR_SUCCESS, ERROR_HTTP_STREAM_EOF, ERROR_SOCKET_READ};
    for (int i = 0; i < (int)(sizeof(codes) / sizeof(int)); i++) {
        MockAppStatistic stat;
        srs_error_t read_error = srs_error_new(codes[i], "mock read error");
        HELPER_EXPECT_SUCCESS(ai37_rtc_tcp_conn_cycle(read_error, "", &stat, NULL));
        EXPECT_EQ(1, stat.on_disconnect_count_) << "code " << codes[i];
    }

    // The first packet must be a STUN binding request, not RTP.
    MockAppStatistic stat;
    string rtp("\x00\x0c\x80\x60", 4);
    rtp.append(10, '\0');
    HELPER_EXPECT_SUCCESS(ai37_rtc_tcp_conn_cycle(srs_success, rtp, &stat, NULL));
    EXPECT_EQ(1, stat.on_disconnect_count_);
}

// When the TCP transport of an established session closes, the session expires, because the client used this
// transport. A session not established on it does not expire, as the client may still use UDP.
VOID TEST(RtcTcpConnTest, CycleExpiresEstablishedSession)
{
    srs_error_t err;

    if (true) {
        MockAppStatistic stat;
        MockRtcNetwork network;
        network.set_established(true);
        MockRtcConnectionForTcpConnCycle session;
        session.tcp_network_ = &network;

        HELPER_EXPECT_SUCCESS(ai37_rtc_tcp_conn_cycle(srs_error_new(ERROR_SOCKET_READ, "mock close"), "", &stat, &session));
        EXPECT_EQ(1, session.expire_count_);
    }

    if (true) {
        MockAppStatistic stat;
        MockRtcNetwork network;
        network.set_established(false);
        MockRtcConnectionForTcpConnCycle session;
        session.tcp_network_ = &network;

        HELPER_EXPECT_SUCCESS(ai37_rtc_tcp_conn_cycle(srs_error_new(ERROR_SOCKET_READ, "mock close"), "", &stat, &session));
        EXPECT_EQ(0, session.expire_count_);
    }
}
