//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_app6.hpp>

using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_app_rtc_network.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_srt_source.hpp>
#include <srs_kernel_error.hpp>
#include <srs_protocol_sdp.hpp>
#include <srs_utest_app2.hpp>

// Mock ISrsResourceManager implementation
MockResourceManagerForBindSession::MockResourceManagerForBindSession()
{
    session_to_return_ = NULL;
}

MockResourceManagerForBindSession::~MockResourceManagerForBindSession()
{
}

srs_error_t MockResourceManagerForBindSession::start()
{
    return srs_success;
}

bool MockResourceManagerForBindSession::empty()
{
    return true;
}

size_t MockResourceManagerForBindSession::size()
{
    return 0;
}

void MockResourceManagerForBindSession::add(ISrsResource *conn, bool *exists)
{
}

void MockResourceManagerForBindSession::add_with_id(const std::string &id, ISrsResource *conn)
{
}

void MockResourceManagerForBindSession::add_with_fast_id(uint64_t id, ISrsResource *conn)
{
}

void MockResourceManagerForBindSession::add_with_name(const std::string & /*name*/, ISrsResource * /*conn*/)
{
}

ISrsResource *MockResourceManagerForBindSession::at(int index)
{
    return NULL;
}

ISrsResource *MockResourceManagerForBindSession::find_by_id(std::string id)
{
    return NULL;
}

ISrsResource *MockResourceManagerForBindSession::find_by_fast_id(uint64_t id)
{
    return session_to_return_;
}

ISrsResource *MockResourceManagerForBindSession::find_by_name(std::string /*name*/)
{
    return NULL;
}

void MockResourceManagerForBindSession::remove(ISrsResource *c)
{
}

void MockResourceManagerForBindSession::subscribe(ISrsDisposingHandler *h)
{
}

void MockResourceManagerForBindSession::unsubscribe(ISrsDisposingHandler *h)
{
}

void MockResourceManagerForBindSession::reset()
{
    session_to_return_ = NULL;
}

// Mock ISrsEphemeralDelta implementation
MockEphemeralDelta::MockEphemeralDelta()
{
    in_bytes_ = 0;
    out_bytes_ = 0;
}

MockEphemeralDelta::~MockEphemeralDelta()
{
}

void MockEphemeralDelta::add_delta(int64_t in, int64_t out)
{
    in_bytes_ += in;
    out_bytes_ += out;
}

void MockEphemeralDelta::remark(int64_t *in, int64_t *out)
{
    if (in)
        *in = in_bytes_;
    if (out)
        *out = out_bytes_;
    in_bytes_ = 0;
    out_bytes_ = 0;
}

void MockEphemeralDelta::reset()
{
    in_bytes_ = 0;
    out_bytes_ = 0;
}

// Mock ISrsUdpMuxSocket implementation
MockUdpMuxSocket::MockUdpMuxSocket()
{
    sendto_error_ = srs_success;
    sendto_called_count_ = 0;
    last_sendto_size_ = 0;
    peer_ip_ = "192.168.1.100";
    peer_port_ = 5000;
    peer_id_ = "192.168.1.100:5000";
    fast_id_ = 0;
    data_ = NULL;
    size_ = 0;
}

MockUdpMuxSocket::~MockUdpMuxSocket()
{
    srs_freep(sendto_error_);
    data_ = NULL;
}

srs_error_t MockUdpMuxSocket::sendto(void *data, int size, srs_utime_t timeout)
{
    sendto_called_count_++;
    last_sendto_size_ = size;
    return srs_error_copy(sendto_error_);
}

std::string MockUdpMuxSocket::get_peer_ip() const
{
    return peer_ip_;
}

int MockUdpMuxSocket::get_peer_port() const
{
    return peer_port_;
}

std::string MockUdpMuxSocket::peer_id()
{
    return peer_id_;
}

uint64_t MockUdpMuxSocket::fast_id()
{
    return fast_id_;
}

SrsUdpMuxSocket *MockUdpMuxSocket::copy_sendonly()
{
    // Return self for testing purposes - in real implementation this creates a copy
    return (SrsUdpMuxSocket *)this;
}

int MockUdpMuxSocket::recvfrom(srs_utime_t timeout)
{
    // Mock implementation - return the size of data received
    return size_;
}

char *MockUdpMuxSocket::data()
{
    // Mock implementation - return the data buffer
    return data_;
}

int MockUdpMuxSocket::size()
{
    // Mock implementation - return the size of data
    return size_;
}

void MockUdpMuxSocket::reset()
{
    srs_freep(sendto_error_);
    sendto_called_count_ = 0;
    last_sendto_size_ = 0;
}

void MockUdpMuxSocket::set_sendto_error(srs_error_t err)
{
    srs_freep(sendto_error_);
    sendto_error_ = srs_error_copy(err);
}

// Mock DTLS implementation
MockDtls::MockDtls()
{
    initialize_error_ = srs_success;
    start_active_handshake_error_ = srs_success;
    on_dtls_error_ = srs_success;
    get_srtp_key_error_ = srs_success;

    reset();
}

MockDtls::~MockDtls()
{
    srs_freep(initialize_error_);
    srs_freep(start_active_handshake_error_);
    srs_freep(on_dtls_error_);
    srs_freep(get_srtp_key_error_);
}

void MockDtls::reset()
{
    srs_freep(initialize_error_);
    srs_freep(start_active_handshake_error_);
    srs_freep(on_dtls_error_);
    srs_freep(get_srtp_key_error_);

    initialize_error_ = srs_success;
    start_active_handshake_error_ = srs_success;
    on_dtls_error_ = srs_success;
    get_srtp_key_error_ = srs_success;

    last_role_ = "";
    last_version_ = "";
    recv_key_ = "";
    send_key_ = "";

    initialize_count_ = 0;
    start_active_handshake_count_ = 0;
    on_dtls_count_ = 0;
    get_srtp_key_count_ = 0;
}

srs_error_t MockDtls::initialize(std::string role, std::string version)
{
    initialize_count_++;
    last_role_ = role;
    last_version_ = version;
    return srs_error_copy(initialize_error_);
}

srs_error_t MockDtls::start_active_handshake()
{
    start_active_handshake_count_++;
    return srs_error_copy(start_active_handshake_error_);
}

srs_error_t MockDtls::on_dtls(char *data, int nb_data)
{
    on_dtls_count_++;
    return srs_error_copy(on_dtls_error_);
}

srs_error_t MockDtls::get_srtp_key(std::string &recv_key, std::string &send_key)
{
    get_srtp_key_count_++;
    if (get_srtp_key_error_ != srs_success) {
        return srs_error_copy(get_srtp_key_error_);
    }
    recv_key = recv_key_;
    send_key = send_key_;
    return srs_success;
}

void MockDtls::set_initialize_error(srs_error_t err)
{
    srs_freep(initialize_error_);
    initialize_error_ = srs_error_copy(err);
}

void MockDtls::set_start_active_handshake_error(srs_error_t err)
{
    srs_freep(start_active_handshake_error_);
    start_active_handshake_error_ = srs_error_copy(err);
}

void MockDtls::set_on_dtls_error(srs_error_t err)
{
    srs_freep(on_dtls_error_);
    on_dtls_error_ = srs_error_copy(err);
}

void MockDtls::set_get_srtp_key_error(srs_error_t err)
{
    srs_freep(get_srtp_key_error_);
    get_srtp_key_error_ = srs_error_copy(err);
}

void MockDtls::set_srtp_keys(const std::string &recv_key, const std::string &send_key)
{
    recv_key_ = recv_key;
    send_key_ = send_key;
}

// Mock RTC Network implementation
MockRtcNetwork::MockRtcNetwork()
{
    on_dtls_handshake_done_error_ = srs_success;
    on_dtls_alert_error_ = srs_success;
    protect_rtp_error_ = srs_success;
    protect_rtcp_error_ = srs_success;
    write_error_ = srs_success;

    reset();
}

MockRtcNetwork::~MockRtcNetwork()
{
    srs_freep(on_dtls_handshake_done_error_);
    srs_freep(on_dtls_alert_error_);
    srs_freep(protect_rtp_error_);
    srs_freep(protect_rtcp_error_);
    srs_freep(write_error_);
}

void MockRtcNetwork::reset()
{
    srs_freep(on_dtls_handshake_done_error_);
    srs_freep(on_dtls_alert_error_);
    srs_freep(protect_rtp_error_);
    srs_freep(protect_rtcp_error_);
    srs_freep(write_error_);

    on_dtls_handshake_done_error_ = srs_success;
    on_dtls_alert_error_ = srs_success;
    protect_rtp_error_ = srs_success;
    protect_rtcp_error_ = srs_success;
    write_error_ = srs_success;

    last_alert_type_ = "";
    last_alert_desc_ = "";

    on_dtls_handshake_done_count_ = 0;
    on_dtls_alert_count_ = 0;
    protect_rtp_count_ = 0;
    protect_rtcp_count_ = 0;
    write_count_ = 0;
    is_established_ = true;
}

srs_error_t MockRtcNetwork::initialize(SrsSessionConfig *cfg, bool dtls, bool srtp)
{
    return srs_success;
}

void MockRtcNetwork::set_state(SrsRtcNetworkState state)
{
}

srs_error_t MockRtcNetwork::on_dtls_handshake_done()
{
    on_dtls_handshake_done_count_++;
    return srs_error_copy(on_dtls_handshake_done_error_);
}

srs_error_t MockRtcNetwork::on_dtls_alert(std::string type, std::string desc)
{
    on_dtls_alert_count_++;
    last_alert_type_ = type;
    last_alert_desc_ = desc;
    return srs_error_copy(on_dtls_alert_error_);
}

srs_error_t MockRtcNetwork::on_dtls(char *data, int nb_data)
{
    return srs_success;
}

srs_error_t MockRtcNetwork::protect_rtp(void *packet, int *nb_cipher)
{
    protect_rtp_count_++;
    return srs_error_copy(protect_rtp_error_);
}

srs_error_t MockRtcNetwork::protect_rtcp(void *packet, int *nb_cipher)
{
    protect_rtcp_count_++;
    return srs_error_copy(protect_rtcp_error_);
}

srs_error_t MockRtcNetwork::on_stun(SrsStunPacket *r, char *data, int nb_data)
{
    return srs_success;
}

srs_error_t MockRtcNetwork::on_rtp(char *data, int nb_data)
{
    return srs_success;
}

srs_error_t MockRtcNetwork::on_rtcp(char *data, int nb_data)
{
    return srs_success;
}

bool MockRtcNetwork::is_establelished()
{
    return is_established_;
}

srs_error_t MockRtcNetwork::write(void *buf, size_t size, ssize_t *nwrite)
{
    write_count_++;
    if (nwrite) {
        *nwrite = (ssize_t)size;
    }
    return srs_error_copy(write_error_);
}

void MockRtcNetwork::set_on_dtls_handshake_done_error(srs_error_t err)
{
    srs_freep(on_dtls_handshake_done_error_);
    on_dtls_handshake_done_error_ = srs_error_copy(err);
}

void MockRtcNetwork::set_on_dtls_alert_error(srs_error_t err)
{
    srs_freep(on_dtls_alert_error_);
    on_dtls_alert_error_ = srs_error_copy(err);
}

void MockRtcNetwork::set_protect_rtp_error(srs_error_t err)
{
    srs_freep(protect_rtp_error_);
    protect_rtp_error_ = srs_error_copy(err);
}

void MockRtcNetwork::set_protect_rtcp_error(srs_error_t err)
{
    srs_freep(protect_rtcp_error_);
    protect_rtcp_error_ = srs_error_copy(err);
}

void MockRtcNetwork::set_write_error(srs_error_t err)
{
    srs_freep(write_error_);
    write_error_ = srs_error_copy(err);
}

void MockRtcNetwork::set_established(bool established)
{
    is_established_ = established;
}

// Mock SRTP implementation
MockSrtp::MockSrtp()
{
    initialize_error_ = srs_success;
    protect_rtp_error_ = srs_success;
    protect_rtcp_error_ = srs_success;
    unprotect_rtp_error_ = srs_success;
    unprotect_rtcp_error_ = srs_success;

    reset();
}

MockSrtp::~MockSrtp()
{
    srs_freep(initialize_error_);
    srs_freep(protect_rtp_error_);
    srs_freep(protect_rtcp_error_);
    srs_freep(unprotect_rtp_error_);
    srs_freep(unprotect_rtcp_error_);
}

void MockSrtp::reset()
{
    srs_freep(initialize_error_);
    srs_freep(protect_rtp_error_);
    srs_freep(protect_rtcp_error_);
    srs_freep(unprotect_rtp_error_);
    srs_freep(unprotect_rtcp_error_);

    initialize_error_ = srs_success;
    protect_rtp_error_ = srs_success;
    protect_rtcp_error_ = srs_success;
    unprotect_rtp_error_ = srs_success;
    unprotect_rtcp_error_ = srs_success;

    last_recv_key_ = "";
    last_send_key_ = "";

    initialize_count_ = 0;
    protect_rtp_count_ = 0;
    protect_rtcp_count_ = 0;
    unprotect_rtp_count_ = 0;
    unprotect_rtcp_count_ = 0;
}

srs_error_t MockSrtp::initialize(std::string recv_key, std::string send_key)
{
    initialize_count_++;
    last_recv_key_ = recv_key;
    last_send_key_ = send_key;
    return srs_error_copy(initialize_error_);
}

srs_error_t MockSrtp::protect_rtp(void *packet, int *nb_cipher)
{
    protect_rtp_count_++;
    return srs_error_copy(protect_rtp_error_);
}

srs_error_t MockSrtp::protect_rtcp(void *packet, int *nb_cipher)
{
    protect_rtcp_count_++;
    return srs_error_copy(protect_rtcp_error_);
}

srs_error_t MockSrtp::unprotect_rtp(void *packet, int *nb_plaintext)
{
    unprotect_rtp_count_++;
    return srs_error_copy(unprotect_rtp_error_);
}

srs_error_t MockSrtp::unprotect_rtcp(void *packet, int *nb_plaintext)
{
    unprotect_rtcp_count_++;
    return srs_error_copy(unprotect_rtcp_error_);
}

void MockSrtp::set_initialize_error(srs_error_t err)
{
    srs_freep(initialize_error_);
    initialize_error_ = srs_error_copy(err);
}

void MockSrtp::set_protect_rtp_error(srs_error_t err)
{
    srs_freep(protect_rtp_error_);
    protect_rtp_error_ = srs_error_copy(err);
}

void MockSrtp::set_protect_rtcp_error(srs_error_t err)
{
    srs_freep(protect_rtcp_error_);
    protect_rtcp_error_ = srs_error_copy(err);
}

void MockSrtp::set_unprotect_rtp_error(srs_error_t err)
{
    srs_freep(unprotect_rtp_error_);
    unprotect_rtp_error_ = srs_error_copy(err);
}

void MockSrtp::set_unprotect_rtcp_error(srs_error_t err)
{
    srs_freep(unprotect_rtcp_error_);
    unprotect_rtcp_error_ = srs_error_copy(err);
}

// Custom SrsSecurityTransport for testing that allows injecting mock DTLS and SRTP
class TestableSecurityTransport : public SrsSecurityTransport
{
public:
    TestableSecurityTransport(ISrsRtcNetwork *s, MockDtls *mock_dtls, MockSrtp *mock_srtp = NULL) : SrsSecurityTransport(s)
    {
        // Replace the real DTLS with our mock
        srs_freep(dtls_);
        dtls_ = mock_dtls;

        // Replace the real SRTP with our mock if provided
        if (mock_srtp) {
            srs_freep(srtp_);
            srtp_ = mock_srtp;
        }
    }

    virtual ~TestableSecurityTransport()
    {
        // Don't free the mock DTLS and SRTP, they're managed by the test
        dtls_ = NULL;
        srtp_ = NULL;
    }
};

VOID TEST(SecurityTransportTest, InitializeSuccess)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls));

    SrsUniquePtr<SrsSessionConfig> cfg(new SrsSessionConfig());
    cfg->dtls_role_ = "active";
    cfg->dtls_version_ = "1.2";

    HELPER_EXPECT_SUCCESS(transport->initialize(cfg.get()));

    // Verify DTLS initialize was called with correct parameters
    EXPECT_EQ(1, mock_dtls.initialize_count_);
    EXPECT_STREQ("active", mock_dtls.last_role_.c_str());
    EXPECT_STREQ("1.2", mock_dtls.last_version_.c_str());
}

VOID TEST(SecurityTransportTest, InitializeFailure)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls));

    // Set up mock to return error
    srs_error_t mock_error = srs_error_new(ERROR_RTC_DTLS, "mock dtls initialize error");
    mock_dtls.set_initialize_error(mock_error);
    srs_freep(mock_error);

    SrsUniquePtr<SrsSessionConfig> cfg(new SrsSessionConfig());
    cfg->dtls_role_ = "passive";
    cfg->dtls_version_ = "auto";

    HELPER_EXPECT_FAILED(transport->initialize(cfg.get()));

    // Verify DTLS initialize was called with correct parameters
    EXPECT_EQ(1, mock_dtls.initialize_count_);
    EXPECT_STREQ("passive", mock_dtls.last_role_.c_str());
    EXPECT_STREQ("auto", mock_dtls.last_version_.c_str());
}

VOID TEST(SecurityTransportTest, StartActiveHandshakeSuccess)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls));

    HELPER_EXPECT_SUCCESS(transport->start_active_handshake());

    // Verify DTLS start_active_handshake was called
    EXPECT_EQ(1, mock_dtls.start_active_handshake_count_);
}

VOID TEST(SecurityTransportTest, StartActiveHandshakeFailure)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls));

    // Set up mock to return error
    srs_error_t mock_error = srs_error_new(ERROR_RTC_DTLS, "mock dtls handshake error");
    mock_dtls.set_start_active_handshake_error(mock_error);
    srs_freep(mock_error);

    HELPER_EXPECT_FAILED(transport->start_active_handshake());

    // Verify DTLS start_active_handshake was called
    EXPECT_EQ(1, mock_dtls.start_active_handshake_count_);
}

VOID TEST(SecurityTransportTest, InitializeWithDifferentRoles)
{
    srs_error_t err;

    // Test with active role
    if (true) {
        MockRtcNetwork mock_network;
        MockDtls mock_dtls;
        SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls));

        SrsUniquePtr<SrsSessionConfig> cfg(new SrsSessionConfig());
        cfg->dtls_role_ = "active";
        cfg->dtls_version_ = "1.2";

        HELPER_EXPECT_SUCCESS(transport->initialize(cfg.get()));
        EXPECT_STREQ("active", mock_dtls.last_role_.c_str());
        EXPECT_STREQ("1.2", mock_dtls.last_version_.c_str());
    }

    // Test with passive role
    if (true) {
        MockRtcNetwork mock_network;
        MockDtls mock_dtls;
        SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls));

        SrsUniquePtr<SrsSessionConfig> cfg(new SrsSessionConfig());
        cfg->dtls_role_ = "passive";
        cfg->dtls_version_ = "auto";

        HELPER_EXPECT_SUCCESS(transport->initialize(cfg.get()));
        EXPECT_STREQ("passive", mock_dtls.last_role_.c_str());
        EXPECT_STREQ("auto", mock_dtls.last_version_.c_str());
    }
}

VOID TEST(SecurityTransportTest, InitializeWithEmptyRoleAndVersion)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls));

    SrsUniquePtr<SrsSessionConfig> cfg(new SrsSessionConfig());
    cfg->dtls_role_ = "";
    cfg->dtls_version_ = "";

    HELPER_EXPECT_SUCCESS(transport->initialize(cfg.get()));

    // Verify DTLS initialize was called with empty parameters
    EXPECT_EQ(1, mock_dtls.initialize_count_);
    EXPECT_STREQ("", mock_dtls.last_role_.c_str());
    EXPECT_STREQ("", mock_dtls.last_version_.c_str());
}

VOID TEST(SecurityTransportTest, MultipleInitializeCalls)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls));

    SrsUniquePtr<SrsSessionConfig> cfg(new SrsSessionConfig());
    cfg->dtls_role_ = "active";
    cfg->dtls_version_ = "1.2";

    // First initialize call
    HELPER_EXPECT_SUCCESS(transport->initialize(cfg.get()));
    EXPECT_EQ(1, mock_dtls.initialize_count_);

    // Second initialize call
    cfg->dtls_role_ = "passive";
    cfg->dtls_version_ = "auto";
    HELPER_EXPECT_SUCCESS(transport->initialize(cfg.get()));
    EXPECT_EQ(2, mock_dtls.initialize_count_);
    EXPECT_STREQ("passive", mock_dtls.last_role_.c_str());
    EXPECT_STREQ("auto", mock_dtls.last_version_.c_str());
}

VOID TEST(SecurityTransportTest, MultipleHandshakeCalls)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls));

    // First handshake call
    HELPER_EXPECT_SUCCESS(transport->start_active_handshake());
    EXPECT_EQ(1, mock_dtls.start_active_handshake_count_);

    // Second handshake call
    HELPER_EXPECT_SUCCESS(transport->start_active_handshake());
    EXPECT_EQ(2, mock_dtls.start_active_handshake_count_);
}

VOID TEST(SecurityTransportTest, InitializeAndHandshakeSequence)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls));

    // Initialize first
    SrsUniquePtr<SrsSessionConfig> cfg(new SrsSessionConfig());
    cfg->dtls_role_ = "active";
    cfg->dtls_version_ = "1.2";

    HELPER_EXPECT_SUCCESS(transport->initialize(cfg.get()));
    EXPECT_EQ(1, mock_dtls.initialize_count_);
    EXPECT_EQ(0, mock_dtls.start_active_handshake_count_);

    // Then start handshake
    HELPER_EXPECT_SUCCESS(transport->start_active_handshake());
    EXPECT_EQ(1, mock_dtls.initialize_count_);
    EXPECT_EQ(1, mock_dtls.start_active_handshake_count_);
}

VOID TEST(SecurityTransportTest, HandshakeWithoutInitialize)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls));

    // Try handshake without initialize - should still work as DTLS handles it
    HELPER_EXPECT_SUCCESS(transport->start_active_handshake());
    EXPECT_EQ(0, mock_dtls.initialize_count_);
    EXPECT_EQ(1, mock_dtls.start_active_handshake_count_);
}

VOID TEST(SecurityTransportTest, WriteDtlsDataSuccess)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls));

    char test_data[] = "test dtls data";
    int data_size = strlen(test_data);

    HELPER_EXPECT_SUCCESS(transport->write_dtls_data(test_data, data_size));

    // Verify network write was called
    EXPECT_EQ(1, mock_network.write_count_);
}

VOID TEST(SecurityTransportTest, WriteDtlsDataZeroSize)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls));

    char test_data[] = "test dtls data";

    // Write with zero size should return success without calling network write
    HELPER_EXPECT_SUCCESS(transport->write_dtls_data(test_data, 0));

    // Verify network write was NOT called
    EXPECT_EQ(0, mock_network.write_count_);
}

VOID TEST(SecurityTransportTest, WriteDtlsDataNetworkError)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls));

    // Set up mock to return error
    srs_error_t mock_error = srs_error_new(ERROR_SOCKET_WRITE, "mock network write error");
    mock_network.set_write_error(mock_error);
    srs_freep(mock_error);

    char test_data[] = "test dtls data";
    int data_size = strlen(test_data);

    HELPER_EXPECT_FAILED(transport->write_dtls_data(test_data, data_size));

    // Verify network write was called
    EXPECT_EQ(1, mock_network.write_count_);
}

VOID TEST(SecurityTransportTest, OnDtlsSuccess)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls));

    char test_data[] = "dtls handshake data";
    int data_size = strlen(test_data);

    HELPER_EXPECT_SUCCESS(transport->on_dtls(test_data, data_size));

    // Verify DTLS on_dtls was called
    EXPECT_EQ(1, mock_dtls.on_dtls_count_);
}

VOID TEST(SecurityTransportTest, OnDtlsError)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls));

    // Set up mock to return error
    srs_error_t mock_error = srs_error_new(ERROR_RTC_DTLS, "mock dtls processing error");
    mock_dtls.set_on_dtls_error(mock_error);
    srs_freep(mock_error);

    char test_data[] = "dtls handshake data";
    int data_size = strlen(test_data);

    HELPER_EXPECT_FAILED(transport->on_dtls(test_data, data_size));

    // Verify DTLS on_dtls was called
    EXPECT_EQ(1, mock_dtls.on_dtls_count_);
}

VOID TEST(SecurityTransportTest, OnDtlsAlertSuccess)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls));

    std::string alert_type = "warning";
    std::string alert_desc = "close_notify";

    HELPER_EXPECT_SUCCESS(transport->on_dtls_alert(alert_type, alert_desc));

    // Verify network on_dtls_alert was called with correct parameters
    EXPECT_EQ(1, mock_network.on_dtls_alert_count_);
    EXPECT_STREQ("warning", mock_network.last_alert_type_.c_str());
    EXPECT_STREQ("close_notify", mock_network.last_alert_desc_.c_str());
}

VOID TEST(SecurityTransportTest, OnDtlsAlertError)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls));

    // Set up mock to return error
    srs_error_t mock_error = srs_error_new(ERROR_RTC_DTLS, "mock dtls alert error");
    mock_network.set_on_dtls_alert_error(mock_error);
    srs_freep(mock_error);

    std::string alert_type = "fatal";
    std::string alert_desc = "handshake_failure";

    HELPER_EXPECT_FAILED(transport->on_dtls_alert(alert_type, alert_desc));

    // Verify network on_dtls_alert was called with correct parameters
    EXPECT_EQ(1, mock_network.on_dtls_alert_count_);
    EXPECT_STREQ("fatal", mock_network.last_alert_type_.c_str());
    EXPECT_STREQ("handshake_failure", mock_network.last_alert_desc_.c_str());
}

VOID TEST(SecurityTransportTest, OnDtlsHandshakeDoneSuccess)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls));

    // Set up mock DTLS to return valid SRTP keys (30 bytes each: 16 key + 14 salt)
    std::string recv_key(30, 'r'); // 30 bytes of 'r'
    std::string send_key(30, 's'); // 30 bytes of 's'
    mock_dtls.set_srtp_keys(recv_key, send_key);

    HELPER_EXPECT_SUCCESS(transport->on_dtls_handshake_done());

    // Verify DTLS get_srtp_key was called
    EXPECT_EQ(1, mock_dtls.get_srtp_key_count_);
    // Verify network on_dtls_handshake_done was called
    EXPECT_EQ(1, mock_network.on_dtls_handshake_done_count_);
}

VOID TEST(SecurityTransportTest, OnDtlsHandshakeDoneAlreadyDone)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls));

    // Set up mock DTLS to return valid SRTP keys (30 bytes each: 16 key + 14 salt)
    std::string recv_key(30, 'r'); // 30 bytes of 'r'
    std::string send_key(30, 's'); // 30 bytes of 's'
    mock_dtls.set_srtp_keys(recv_key, send_key);

    // First call should succeed
    HELPER_EXPECT_SUCCESS(transport->on_dtls_handshake_done());
    EXPECT_EQ(1, mock_dtls.get_srtp_key_count_);
    EXPECT_EQ(1, mock_network.on_dtls_handshake_done_count_);

    // Second call should return success immediately without calling DTLS/network
    HELPER_EXPECT_SUCCESS(transport->on_dtls_handshake_done());
    EXPECT_EQ(1, mock_dtls.get_srtp_key_count_);              // Should not increase
    EXPECT_EQ(1, mock_network.on_dtls_handshake_done_count_); // Should not increase
}

VOID TEST(SecurityTransportTest, OnDtlsHandshakeDoneSrtpInitError)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls));

    // Set up mock DTLS to return error when getting SRTP keys
    srs_error_t mock_error = srs_error_new(ERROR_RTC_SRTP_INIT, "mock srtp key error");
    mock_dtls.set_get_srtp_key_error(mock_error);
    srs_freep(mock_error);

    HELPER_EXPECT_FAILED(transport->on_dtls_handshake_done());

    // Verify DTLS get_srtp_key was called
    EXPECT_EQ(1, mock_dtls.get_srtp_key_count_);
    // Verify network on_dtls_handshake_done was NOT called due to SRTP init failure
    EXPECT_EQ(0, mock_network.on_dtls_handshake_done_count_);
}

VOID TEST(SecurityTransportTest, OnDtlsHandshakeDoneNetworkError)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls));

    // Set up mock DTLS to return valid SRTP keys (30 bytes each: 16 key + 14 salt)
    std::string recv_key(30, 'r'); // 30 bytes of 'r'
    std::string send_key(30, 's'); // 30 bytes of 's'
    mock_dtls.set_srtp_keys(recv_key, send_key);

    // Set up mock network to return error
    srs_error_t mock_error = srs_error_new(ERROR_RTC_DTLS, "mock network handshake done error");
    mock_network.set_on_dtls_handshake_done_error(mock_error);
    srs_freep(mock_error);

    HELPER_EXPECT_FAILED(transport->on_dtls_handshake_done());

    // Verify DTLS get_srtp_key was called
    EXPECT_EQ(1, mock_dtls.get_srtp_key_count_);
    // Verify network on_dtls_handshake_done was called
    EXPECT_EQ(1, mock_network.on_dtls_handshake_done_count_);
}

VOID TEST(SecurityTransportTest, OnDtlsApplicationDataSuccess)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls));

    char test_data[] = "application data";
    int data_size = strlen(test_data);

    HELPER_EXPECT_SUCCESS(transport->on_dtls_application_data(test_data, data_size));

    // This method currently just returns success without doing anything
    // TODO: When SCTP protocol support is added, this test should verify SCTP processing
}

VOID TEST(SecurityTransportTest, SrtpInitializeSuccess)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    MockSrtp mock_srtp;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls, &mock_srtp));

    // Set up mock DTLS to return valid SRTP keys (30 bytes each: 16 key + 14 salt)
    std::string recv_key(30, 'r'); // 30 bytes of 'r'
    std::string send_key(30, 's'); // 30 bytes of 's'
    mock_dtls.set_srtp_keys(recv_key, send_key);

    HELPER_EXPECT_SUCCESS(transport->srtp_initialize());

    // Verify DTLS get_srtp_key was called
    EXPECT_EQ(1, mock_dtls.get_srtp_key_count_);
    // Verify SRTP initialize was called with correct keys
    EXPECT_EQ(1, mock_srtp.initialize_count_);
    EXPECT_STREQ(recv_key.c_str(), mock_srtp.last_recv_key_.c_str());
    EXPECT_STREQ(send_key.c_str(), mock_srtp.last_send_key_.c_str());
}

VOID TEST(SecurityTransportTest, SrtpInitializeDtlsKeyError)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    MockSrtp mock_srtp;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls, &mock_srtp));

    // Set up mock DTLS to return error when getting SRTP keys
    srs_error_t mock_error = srs_error_new(ERROR_RTC_SRTP_INIT, "mock dtls get srtp key error");
    mock_dtls.set_get_srtp_key_error(mock_error);
    srs_freep(mock_error);

    HELPER_EXPECT_FAILED(transport->srtp_initialize());

    // Verify DTLS get_srtp_key was called
    EXPECT_EQ(1, mock_dtls.get_srtp_key_count_);
    // Verify SRTP initialize was NOT called due to DTLS error
    EXPECT_EQ(0, mock_srtp.initialize_count_);
}

VOID TEST(SecurityTransportTest, SrtpInitializeSrtpError)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    MockSrtp mock_srtp;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls, &mock_srtp));

    // Set up mock DTLS to return valid SRTP keys
    std::string recv_key(30, 'r');
    std::string send_key(30, 's');
    mock_dtls.set_srtp_keys(recv_key, send_key);

    // Set up mock SRTP to return error
    srs_error_t mock_error = srs_error_new(ERROR_RTC_SRTP_INIT, "mock srtp initialize error");
    mock_srtp.set_initialize_error(mock_error);
    srs_freep(mock_error);

    HELPER_EXPECT_FAILED(transport->srtp_initialize());

    // Verify DTLS get_srtp_key was called
    EXPECT_EQ(1, mock_dtls.get_srtp_key_count_);
    // Verify SRTP initialize was called
    EXPECT_EQ(1, mock_srtp.initialize_count_);
}

VOID TEST(SecurityTransportTest, ProtectRtpSuccess)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    MockSrtp mock_srtp;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls, &mock_srtp));

    char test_packet[100] = {0};
    int nb_cipher = sizeof(test_packet);

    HELPER_EXPECT_SUCCESS(transport->protect_rtp(test_packet, &nb_cipher));

    // Verify SRTP protect_rtp was called
    EXPECT_EQ(1, mock_srtp.protect_rtp_count_);
}

VOID TEST(SecurityTransportTest, ProtectRtpError)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    MockSrtp mock_srtp;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls, &mock_srtp));

    // Set up mock SRTP to return error
    srs_error_t mock_error = srs_error_new(ERROR_RTC_SRTP_PROTECT, "mock srtp protect rtp error");
    mock_srtp.set_protect_rtp_error(mock_error);
    srs_freep(mock_error);

    char test_packet[100] = {0};
    int nb_cipher = sizeof(test_packet);

    HELPER_EXPECT_FAILED(transport->protect_rtp(test_packet, &nb_cipher));

    // Verify SRTP protect_rtp was called
    EXPECT_EQ(1, mock_srtp.protect_rtp_count_);
}

VOID TEST(SecurityTransportTest, ProtectRtcpSuccess)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    MockSrtp mock_srtp;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls, &mock_srtp));

    char test_packet[100] = {0};
    int nb_cipher = sizeof(test_packet);

    HELPER_EXPECT_SUCCESS(transport->protect_rtcp(test_packet, &nb_cipher));

    // Verify SRTP protect_rtcp was called
    EXPECT_EQ(1, mock_srtp.protect_rtcp_count_);
}

VOID TEST(SecurityTransportTest, ProtectRtcpError)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    MockSrtp mock_srtp;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls, &mock_srtp));

    // Set up mock SRTP to return error
    srs_error_t mock_error = srs_error_new(ERROR_RTC_SRTP_PROTECT, "mock srtp protect rtcp error");
    mock_srtp.set_protect_rtcp_error(mock_error);
    srs_freep(mock_error);

    char test_packet[100] = {0};
    int nb_cipher = sizeof(test_packet);

    HELPER_EXPECT_FAILED(transport->protect_rtcp(test_packet, &nb_cipher));

    // Verify SRTP protect_rtcp was called
    EXPECT_EQ(1, mock_srtp.protect_rtcp_count_);
}

VOID TEST(SecurityTransportTest, UnprotectRtpSuccess)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    MockSrtp mock_srtp;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls, &mock_srtp));

    char test_packet[100] = {0};
    int nb_plaintext = sizeof(test_packet);

    HELPER_EXPECT_SUCCESS(transport->unprotect_rtp(test_packet, &nb_plaintext));

    // Verify SRTP unprotect_rtp was called
    EXPECT_EQ(1, mock_srtp.unprotect_rtp_count_);
}

VOID TEST(SecurityTransportTest, UnprotectRtpError)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    MockSrtp mock_srtp;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls, &mock_srtp));

    // Set up mock SRTP to return error
    srs_error_t mock_error = srs_error_new(ERROR_RTC_SRTP_UNPROTECT, "mock srtp unprotect rtp error");
    mock_srtp.set_unprotect_rtp_error(mock_error);
    srs_freep(mock_error);

    char test_packet[100] = {0};
    int nb_plaintext = sizeof(test_packet);

    HELPER_EXPECT_FAILED(transport->unprotect_rtp(test_packet, &nb_plaintext));

    // Verify SRTP unprotect_rtp was called
    EXPECT_EQ(1, mock_srtp.unprotect_rtp_count_);
}

VOID TEST(SecurityTransportTest, UnprotectRtcpSuccess)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    MockSrtp mock_srtp;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls, &mock_srtp));

    char test_packet[100] = {0};
    int nb_plaintext = sizeof(test_packet);

    HELPER_EXPECT_SUCCESS(transport->unprotect_rtcp(test_packet, &nb_plaintext));

    // Verify SRTP unprotect_rtcp was called
    EXPECT_EQ(1, mock_srtp.unprotect_rtcp_count_);
}

VOID TEST(SecurityTransportTest, UnprotectRtcpError)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    MockSrtp mock_srtp;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls, &mock_srtp));

    // Set up mock SRTP to return error
    srs_error_t mock_error = srs_error_new(ERROR_RTC_SRTP_UNPROTECT, "mock srtp unprotect rtcp error");
    mock_srtp.set_unprotect_rtcp_error(mock_error);
    srs_freep(mock_error);

    char test_packet[100] = {0};
    int nb_plaintext = sizeof(test_packet);

    HELPER_EXPECT_FAILED(transport->unprotect_rtcp(test_packet, &nb_plaintext));

    // Verify SRTP unprotect_rtcp was called
    EXPECT_EQ(1, mock_srtp.unprotect_rtcp_count_);
}

VOID TEST(SecurityTransportTest, ProtectUnprotectWithNullPointers)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    MockSrtp mock_srtp;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls, &mock_srtp));

    int nb_cipher = 100;
    int nb_plaintext = 100;

    // Test with NULL packet pointer - should still call SRTP methods
    HELPER_EXPECT_SUCCESS(transport->protect_rtp(NULL, &nb_cipher));
    EXPECT_EQ(1, mock_srtp.protect_rtp_count_);

    HELPER_EXPECT_SUCCESS(transport->protect_rtcp(NULL, &nb_cipher));
    EXPECT_EQ(1, mock_srtp.protect_rtcp_count_);

    HELPER_EXPECT_SUCCESS(transport->unprotect_rtp(NULL, &nb_plaintext));
    EXPECT_EQ(1, mock_srtp.unprotect_rtp_count_);

    HELPER_EXPECT_SUCCESS(transport->unprotect_rtcp(NULL, &nb_plaintext));
    EXPECT_EQ(1, mock_srtp.unprotect_rtcp_count_);
}

VOID TEST(SecurityTransportTest, MultipleProtectUnprotectCalls)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    MockSrtp mock_srtp;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls, &mock_srtp));

    char test_packet[100] = {0};
    int nb_cipher = sizeof(test_packet);
    int nb_plaintext = sizeof(test_packet);

    // Multiple protect_rtp calls
    HELPER_EXPECT_SUCCESS(transport->protect_rtp(test_packet, &nb_cipher));
    HELPER_EXPECT_SUCCESS(transport->protect_rtp(test_packet, &nb_cipher));
    EXPECT_EQ(2, mock_srtp.protect_rtp_count_);

    // Multiple protect_rtcp calls
    HELPER_EXPECT_SUCCESS(transport->protect_rtcp(test_packet, &nb_cipher));
    HELPER_EXPECT_SUCCESS(transport->protect_rtcp(test_packet, &nb_cipher));
    EXPECT_EQ(2, mock_srtp.protect_rtcp_count_);

    // Multiple unprotect_rtp calls
    HELPER_EXPECT_SUCCESS(transport->unprotect_rtp(test_packet, &nb_plaintext));
    HELPER_EXPECT_SUCCESS(transport->unprotect_rtp(test_packet, &nb_plaintext));
    EXPECT_EQ(2, mock_srtp.unprotect_rtp_count_);

    // Multiple unprotect_rtcp calls
    HELPER_EXPECT_SUCCESS(transport->unprotect_rtcp(test_packet, &nb_plaintext));
    HELPER_EXPECT_SUCCESS(transport->unprotect_rtcp(test_packet, &nb_plaintext));
    EXPECT_EQ(2, mock_srtp.unprotect_rtcp_count_);
}

VOID TEST(SecurityTransportTest, SrtpInitializeWithEmptyKeys)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    MockDtls mock_dtls;
    MockSrtp mock_srtp;
    SrsUniquePtr<TestableSecurityTransport> transport(new TestableSecurityTransport(&mock_network, &mock_dtls, &mock_srtp));

    // Set up mock DTLS to return empty SRTP keys
    std::string recv_key = "";
    std::string send_key = "";
    mock_dtls.set_srtp_keys(recv_key, send_key);

    HELPER_EXPECT_SUCCESS(transport->srtp_initialize());

    // Verify DTLS get_srtp_key was called
    EXPECT_EQ(1, mock_dtls.get_srtp_key_count_);
    // Verify SRTP initialize was called with empty keys
    EXPECT_EQ(1, mock_srtp.initialize_count_);
    EXPECT_STREQ("", mock_srtp.last_recv_key_.c_str());
    EXPECT_STREQ("", mock_srtp.last_send_key_.c_str());
}

// Tests for SrsSemiSecurityTransport
VOID TEST(SemiSecurityTransportTest, ConstructorAndDestructor)
{
    MockRtcNetwork mock_network;
    SrsSemiSecurityTransport *transport = new SrsSemiSecurityTransport(&mock_network);

    // Constructor should succeed
    EXPECT_TRUE(transport != NULL);

    // Destructor should not crash
    srs_freep(transport);
}

VOID TEST(SemiSecurityTransportTest, ProtectRtpAlwaysSuccess)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    SrsUniquePtr<SrsSemiSecurityTransport> transport(new SrsSemiSecurityTransport(&mock_network));

    char test_packet[100] = {0};
    int nb_cipher = sizeof(test_packet);

    // protect_rtp should always return success without doing anything
    HELPER_EXPECT_SUCCESS(transport->protect_rtp(test_packet, &nb_cipher));

    // Test with NULL packet
    HELPER_EXPECT_SUCCESS(transport->protect_rtp(NULL, &nb_cipher));

    // Test with NULL size pointer
    HELPER_EXPECT_SUCCESS(transport->protect_rtp(test_packet, NULL));

    // Test with both NULL
    HELPER_EXPECT_SUCCESS(transport->protect_rtp(NULL, NULL));
}

VOID TEST(SemiSecurityTransportTest, ProtectRtcpAlwaysSuccess)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    SrsUniquePtr<SrsSemiSecurityTransport> transport(new SrsSemiSecurityTransport(&mock_network));

    char test_packet[100] = {0};
    int nb_cipher = sizeof(test_packet);

    // protect_rtcp should always return success without doing anything
    HELPER_EXPECT_SUCCESS(transport->protect_rtcp(test_packet, &nb_cipher));

    // Test with NULL packet
    HELPER_EXPECT_SUCCESS(transport->protect_rtcp(NULL, &nb_cipher));

    // Test with NULL size pointer
    HELPER_EXPECT_SUCCESS(transport->protect_rtcp(test_packet, NULL));

    // Test with both NULL
    HELPER_EXPECT_SUCCESS(transport->protect_rtcp(NULL, NULL));
}

VOID TEST(SemiSecurityTransportTest, UnprotectRtpAlwaysSuccess)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    SrsUniquePtr<SrsSemiSecurityTransport> transport(new SrsSemiSecurityTransport(&mock_network));

    char test_packet[100] = {0};
    int nb_plaintext = sizeof(test_packet);

    // unprotect_rtp should always return success without doing anything
    HELPER_EXPECT_SUCCESS(transport->unprotect_rtp(test_packet, &nb_plaintext));

    // Test with NULL packet
    HELPER_EXPECT_SUCCESS(transport->unprotect_rtp(NULL, &nb_plaintext));

    // Test with NULL size pointer
    HELPER_EXPECT_SUCCESS(transport->unprotect_rtp(test_packet, NULL));

    // Test with both NULL
    HELPER_EXPECT_SUCCESS(transport->unprotect_rtp(NULL, NULL));
}

VOID TEST(SemiSecurityTransportTest, UnprotectRtcpAlwaysSuccess)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    SrsUniquePtr<SrsSemiSecurityTransport> transport(new SrsSemiSecurityTransport(&mock_network));

    char test_packet[100] = {0};
    int nb_plaintext = sizeof(test_packet);

    // unprotect_rtcp should always return success without doing anything
    HELPER_EXPECT_SUCCESS(transport->unprotect_rtcp(test_packet, &nb_plaintext));

    // Test with NULL packet
    HELPER_EXPECT_SUCCESS(transport->unprotect_rtcp(NULL, &nb_plaintext));

    // Test with NULL size pointer
    HELPER_EXPECT_SUCCESS(transport->unprotect_rtcp(test_packet, NULL));

    // Test with both NULL
    HELPER_EXPECT_SUCCESS(transport->unprotect_rtcp(NULL, NULL));
}

VOID TEST(SemiSecurityTransportTest, MultipleProtectUnprotectCalls)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    SrsUniquePtr<SrsSemiSecurityTransport> transport(new SrsSemiSecurityTransport(&mock_network));

    char test_packet[100] = {0};
    int nb_cipher = sizeof(test_packet);
    int nb_plaintext = sizeof(test_packet);

    // Multiple calls should all succeed
    for (int i = 0; i < 5; i++) {
        HELPER_EXPECT_SUCCESS(transport->protect_rtp(test_packet, &nb_cipher));
        HELPER_EXPECT_SUCCESS(transport->protect_rtcp(test_packet, &nb_cipher));
        HELPER_EXPECT_SUCCESS(transport->unprotect_rtp(test_packet, &nb_plaintext));
        HELPER_EXPECT_SUCCESS(transport->unprotect_rtcp(test_packet, &nb_plaintext));
    }
}

VOID TEST(SemiSecurityTransportTest, InheritedMethodsFromSecurityTransport)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    SrsUniquePtr<SrsSemiSecurityTransport> transport(new SrsSemiSecurityTransport(&mock_network));

    // Test inherited initialize method
    SrsUniquePtr<SrsSessionConfig> cfg(new SrsSessionConfig());
    cfg->dtls_role_ = "active";
    cfg->dtls_version_ = "1.2";

    // This should work since it inherits from SrsSecurityTransport
    // Note: This will use real DTLS, so we expect it to work
    HELPER_EXPECT_SUCCESS(transport->initialize(cfg.get()));
}

// Tests for SrsPlaintextTransport
VOID TEST(PlaintextTransportTest, ConstructorAndDestructor)
{
    MockRtcNetwork mock_network;
    SrsPlaintextTransport *transport = new SrsPlaintextTransport(&mock_network);

    // Constructor should succeed
    EXPECT_TRUE(transport != NULL);

    // Destructor should not crash
    srs_freep(transport);
}

VOID TEST(PlaintextTransportTest, InitializeAlwaysSuccess)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    SrsUniquePtr<SrsPlaintextTransport> transport(new SrsPlaintextTransport(&mock_network));

    // initialize should always return success without doing anything
    SrsUniquePtr<SrsSessionConfig> cfg(new SrsSessionConfig());
    cfg->dtls_role_ = "active";
    cfg->dtls_version_ = "1.2";

    HELPER_EXPECT_SUCCESS(transport->initialize(cfg.get()));

    // Test with different config values
    cfg->dtls_role_ = "passive";
    cfg->dtls_version_ = "auto";
    HELPER_EXPECT_SUCCESS(transport->initialize(cfg.get()));

    // Test with empty config values
    cfg->dtls_role_ = "";
    cfg->dtls_version_ = "";
    HELPER_EXPECT_SUCCESS(transport->initialize(cfg.get()));

    // Test with NULL config
    HELPER_EXPECT_SUCCESS(transport->initialize(NULL));
}

VOID TEST(PlaintextTransportTest, StartActiveHandshakeCallsNetworkHandshakeDone)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    SrsUniquePtr<SrsPlaintextTransport> transport(new SrsPlaintextTransport(&mock_network));

    // start_active_handshake should call network->on_dtls_handshake_done()
    HELPER_EXPECT_SUCCESS(transport->start_active_handshake());

    // Verify network on_dtls_handshake_done was called
    EXPECT_EQ(1, mock_network.on_dtls_handshake_done_count_);
}

VOID TEST(PlaintextTransportTest, StartActiveHandshakeNetworkError)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    SrsUniquePtr<SrsPlaintextTransport> transport(new SrsPlaintextTransport(&mock_network));

    // Set up mock network to return error
    srs_error_t mock_error = srs_error_new(ERROR_RTC_DTLS, "mock network handshake done error");
    mock_network.set_on_dtls_handshake_done_error(mock_error);
    srs_freep(mock_error);

    // start_active_handshake should propagate the network error
    HELPER_EXPECT_FAILED(transport->start_active_handshake());

    // Verify network on_dtls_handshake_done was called
    EXPECT_EQ(1, mock_network.on_dtls_handshake_done_count_);
}

VOID TEST(PlaintextTransportTest, OnDtlsAlwaysSuccess)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    SrsUniquePtr<SrsPlaintextTransport> transport(new SrsPlaintextTransport(&mock_network));

    char test_data[] = "dtls handshake data";
    int data_size = strlen(test_data);

    // on_dtls should always return success without doing anything
    HELPER_EXPECT_SUCCESS(transport->on_dtls(test_data, data_size));

    // Test with NULL data
    HELPER_EXPECT_SUCCESS(transport->on_dtls(NULL, data_size));

    // Test with zero size
    HELPER_EXPECT_SUCCESS(transport->on_dtls(test_data, 0));

    // Test with both NULL and zero
    HELPER_EXPECT_SUCCESS(transport->on_dtls(NULL, 0));
}

VOID TEST(PlaintextTransportTest, OnDtlsAlertAlwaysSuccess)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    SrsUniquePtr<SrsPlaintextTransport> transport(new SrsPlaintextTransport(&mock_network));

    // on_dtls_alert should always return success without doing anything
    HELPER_EXPECT_SUCCESS(transport->on_dtls_alert("warning", "close_notify"));
    HELPER_EXPECT_SUCCESS(transport->on_dtls_alert("fatal", "handshake_failure"));
    HELPER_EXPECT_SUCCESS(transport->on_dtls_alert("", ""));
}

VOID TEST(PlaintextTransportTest, OnDtlsHandshakeDoneCallsNetwork)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    SrsUniquePtr<SrsPlaintextTransport> transport(new SrsPlaintextTransport(&mock_network));

    // on_dtls_handshake_done should call network->on_dtls_handshake_done()
    HELPER_EXPECT_SUCCESS(transport->on_dtls_handshake_done());

    // Verify network on_dtls_handshake_done was called
    EXPECT_EQ(1, mock_network.on_dtls_handshake_done_count_);
}

VOID TEST(PlaintextTransportTest, OnDtlsHandshakeDoneNetworkError)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    SrsUniquePtr<SrsPlaintextTransport> transport(new SrsPlaintextTransport(&mock_network));

    // Set up mock network to return error
    srs_error_t mock_error = srs_error_new(ERROR_RTC_DTLS, "mock network handshake done error");
    mock_network.set_on_dtls_handshake_done_error(mock_error);
    srs_freep(mock_error);

    // on_dtls_handshake_done should propagate the network error
    HELPER_EXPECT_FAILED(transport->on_dtls_handshake_done());

    // Verify network on_dtls_handshake_done was called
    EXPECT_EQ(1, mock_network.on_dtls_handshake_done_count_);
}

VOID TEST(PlaintextTransportTest, OnDtlsApplicationDataAlwaysSuccess)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    SrsUniquePtr<SrsPlaintextTransport> transport(new SrsPlaintextTransport(&mock_network));

    char test_data[] = "application data";
    int data_size = strlen(test_data);

    // on_dtls_application_data should always return success without doing anything
    HELPER_EXPECT_SUCCESS(transport->on_dtls_application_data(test_data, data_size));

    // Test with NULL data
    HELPER_EXPECT_SUCCESS(transport->on_dtls_application_data(NULL, data_size));

    // Test with zero size
    HELPER_EXPECT_SUCCESS(transport->on_dtls_application_data(test_data, 0));

    // Test with both NULL and zero
    HELPER_EXPECT_SUCCESS(transport->on_dtls_application_data(NULL, 0));
}

VOID TEST(PlaintextTransportTest, WriteDtlsDataAlwaysSuccess)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    SrsUniquePtr<SrsPlaintextTransport> transport(new SrsPlaintextTransport(&mock_network));

    char test_data[] = "dtls data to write";
    int data_size = strlen(test_data);

    // write_dtls_data should always return success without doing anything
    HELPER_EXPECT_SUCCESS(transport->write_dtls_data(test_data, data_size));

    // Test with NULL data
    HELPER_EXPECT_SUCCESS(transport->write_dtls_data(NULL, data_size));

    // Test with zero size
    HELPER_EXPECT_SUCCESS(transport->write_dtls_data(test_data, 0));

    // Test with both NULL and zero
    HELPER_EXPECT_SUCCESS(transport->write_dtls_data(NULL, 0));
}

VOID TEST(PlaintextTransportTest, ProtectRtpAlwaysSuccess)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    SrsUniquePtr<SrsPlaintextTransport> transport(new SrsPlaintextTransport(&mock_network));

    char test_packet[100] = {0};
    int nb_cipher = sizeof(test_packet);

    // protect_rtp should always return success without doing anything
    HELPER_EXPECT_SUCCESS(transport->protect_rtp(test_packet, &nb_cipher));

    // Test with NULL packet
    HELPER_EXPECT_SUCCESS(transport->protect_rtp(NULL, &nb_cipher));

    // Test with NULL size pointer
    HELPER_EXPECT_SUCCESS(transport->protect_rtp(test_packet, NULL));

    // Test with both NULL
    HELPER_EXPECT_SUCCESS(transport->protect_rtp(NULL, NULL));
}

VOID TEST(PlaintextTransportTest, ProtectRtcpAlwaysSuccess)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    SrsUniquePtr<SrsPlaintextTransport> transport(new SrsPlaintextTransport(&mock_network));

    char test_packet[100] = {0};
    int nb_cipher = sizeof(test_packet);

    // protect_rtcp should always return success without doing anything
    HELPER_EXPECT_SUCCESS(transport->protect_rtcp(test_packet, &nb_cipher));

    // Test with NULL packet
    HELPER_EXPECT_SUCCESS(transport->protect_rtcp(NULL, &nb_cipher));

    // Test with NULL size pointer
    HELPER_EXPECT_SUCCESS(transport->protect_rtcp(test_packet, NULL));

    // Test with both NULL
    HELPER_EXPECT_SUCCESS(transport->protect_rtcp(NULL, NULL));
}

VOID TEST(PlaintextTransportTest, UnprotectRtpAlwaysSuccess)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    SrsUniquePtr<SrsPlaintextTransport> transport(new SrsPlaintextTransport(&mock_network));

    char test_packet[100] = {0};
    int nb_plaintext = sizeof(test_packet);

    // unprotect_rtp should always return success without doing anything
    HELPER_EXPECT_SUCCESS(transport->unprotect_rtp(test_packet, &nb_plaintext));

    // Test with NULL packet
    HELPER_EXPECT_SUCCESS(transport->unprotect_rtp(NULL, &nb_plaintext));

    // Test with NULL size pointer
    HELPER_EXPECT_SUCCESS(transport->unprotect_rtp(test_packet, NULL));

    // Test with both NULL
    HELPER_EXPECT_SUCCESS(transport->unprotect_rtp(NULL, NULL));
}

VOID TEST(PlaintextTransportTest, UnprotectRtcpAlwaysSuccess)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    SrsUniquePtr<SrsPlaintextTransport> transport(new SrsPlaintextTransport(&mock_network));

    char test_packet[100] = {0};
    int nb_plaintext = sizeof(test_packet);

    // unprotect_rtcp should always return success without doing anything
    HELPER_EXPECT_SUCCESS(transport->unprotect_rtcp(test_packet, &nb_plaintext));

    // Test with NULL packet
    HELPER_EXPECT_SUCCESS(transport->unprotect_rtcp(NULL, &nb_plaintext));

    // Test with NULL size pointer
    HELPER_EXPECT_SUCCESS(transport->unprotect_rtcp(test_packet, NULL));

    // Test with both NULL
    HELPER_EXPECT_SUCCESS(transport->unprotect_rtcp(NULL, NULL));
}

VOID TEST(PlaintextTransportTest, MultipleProtectUnprotectCalls)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    SrsUniquePtr<SrsPlaintextTransport> transport(new SrsPlaintextTransport(&mock_network));

    char test_packet[100] = {0};
    int nb_cipher = sizeof(test_packet);
    int nb_plaintext = sizeof(test_packet);

    // Multiple calls should all succeed
    for (int i = 0; i < 5; i++) {
        HELPER_EXPECT_SUCCESS(transport->protect_rtp(test_packet, &nb_cipher));
        HELPER_EXPECT_SUCCESS(transport->protect_rtcp(test_packet, &nb_cipher));
        HELPER_EXPECT_SUCCESS(transport->unprotect_rtp(test_packet, &nb_plaintext));
        HELPER_EXPECT_SUCCESS(transport->unprotect_rtcp(test_packet, &nb_plaintext));
    }
}

VOID TEST(RtcPublishStreamTest, SendRtcpXrRrtr)
{
    srs_error_t err;

    MockRtcAsyncTaskExecutor mock_exec;
    MockRtcExpire mock_expire;
    MockRtcPacketReceiver mock_receiver;
    SrsUniquePtr<SrsRtcPublishStream> publish_stream(new SrsRtcPublishStream(&mock_exec, &mock_expire, &mock_receiver, SrsContextId()));

    // Create audio track
    SrsRtcTrackDescription *audio_desc = new SrsRtcTrackDescription();
    audio_desc->type_ = "audio";
    audio_desc->id_ = "audio_track_1";
    audio_desc->ssrc_ = 12345;
    audio_desc->is_active_ = true;
    SrsRtcAudioRecvTrack *audio_track = new SrsRtcAudioRecvTrack(&mock_receiver, audio_desc);
    publish_stream->audio_tracks_.push_back(audio_track);

    // Create video track
    SrsRtcTrackDescription *video_desc = new SrsRtcTrackDescription();
    video_desc->type_ = "video";
    video_desc->id_ = "video_track_1";
    video_desc->ssrc_ = 67890;
    video_desc->is_active_ = true;
    SrsRtcVideoRecvTrack *video_track = new SrsRtcVideoRecvTrack(&mock_receiver, video_desc);
    publish_stream->video_tracks_.push_back(video_track);

    // Test successful case
    HELPER_EXPECT_SUCCESS(publish_stream->send_rtcp_xr_rrtr());

    // Verify that send_rtcp_xr_rrtr was called for both tracks (2 times total)
    EXPECT_EQ(2, mock_receiver.send_rtcp_xr_rrtr_count_);
}

// Mock RTC expire implementation
MockRtcExpire::MockRtcExpire()
{
    expired_ = false;
}

MockRtcExpire::~MockRtcExpire()
{
}

void MockRtcExpire::expire()
{
    expired_ = true;
}

void MockRtcExpire::reset()
{
    expired_ = false;
}

VOID TEST(RtcPublishStreamTest, SendRtcpRrSuccess)
{
    srs_error_t err;

    // Create mock objects
    MockRtcAsyncTaskExecutor mock_exec;
    MockRtcExpire mock_expire;
    MockRtcPacketReceiver mock_receiver;
    SrsContextId cid;

    // Create publish stream
    SrsUniquePtr<SrsRtcPublishStream> publish_stream(new SrsRtcPublishStream(&mock_exec, &mock_expire, &mock_receiver, cid));

    // Create mock track descriptions
    SrsUniquePtr<SrsRtcTrackDescription> video_track_desc(new SrsRtcTrackDescription());
    video_track_desc->type_ = "video";
    video_track_desc->id_ = "video_track_1";
    video_track_desc->ssrc_ = 12345;

    SrsUniquePtr<SrsRtcTrackDescription> audio_track_desc(new SrsRtcTrackDescription());
    audio_track_desc->type_ = "audio";
    audio_track_desc->id_ = "audio_track_1";
    audio_track_desc->ssrc_ = 67890;

    // Create video and audio recv tracks
    SrsRtcVideoRecvTrack *video_track = new SrsRtcVideoRecvTrack(&mock_receiver, video_track_desc.get());
    SrsRtcAudioRecvTrack *audio_track = new SrsRtcAudioRecvTrack(&mock_receiver, audio_track_desc.get());

    // Add tracks to publish stream (using private member access)
    // The publish stream will take ownership and free them in destructor
    publish_stream->video_tracks_.push_back(video_track);
    publish_stream->audio_tracks_.push_back(audio_track);

    // Reset receiver count before test
    mock_receiver.reset();

    // Test send_rtcp_rr - should succeed when all tracks succeed
    HELPER_EXPECT_SUCCESS(publish_stream->send_rtcp_rr());

    // Verify that send_rtcp_rr was called on both tracks (2 calls total)
    EXPECT_EQ(2, mock_receiver.send_rtcp_rr_count_);
}

VOID TEST(RtcPublishStreamTest, SendRtcpRrVideoTrackError)
{
    srs_error_t err;

    // Create mock objects
    MockRtcAsyncTaskExecutor mock_exec;
    MockRtcExpire mock_expire;
    MockRtcPacketReceiver mock_receiver;
    SrsContextId cid;

    // Create publish stream
    SrsUniquePtr<SrsRtcPublishStream> publish_stream(new SrsRtcPublishStream(&mock_exec, &mock_expire, &mock_receiver, cid));

    // Create mock track descriptions
    SrsUniquePtr<SrsRtcTrackDescription> video_track_desc(new SrsRtcTrackDescription());
    video_track_desc->type_ = "video";
    video_track_desc->id_ = "video_track_1";
    video_track_desc->ssrc_ = 12345;

    // Create video recv track
    SrsRtcVideoRecvTrack *video_track = new SrsRtcVideoRecvTrack(&mock_receiver, video_track_desc.get());

    // Add track to publish stream (only video track to simplify)
    publish_stream->video_tracks_.push_back(video_track);

    // Reset receiver count before test
    mock_receiver.reset();

    // Set receiver to return error after reset
    srs_error_t mock_error = srs_error_new(ERROR_RTC_RTCP, "mock rtcp rr error");
    mock_receiver.set_send_rtcp_rr_error(mock_error);

    // Test send_rtcp_rr - should fail when receiver returns error
    HELPER_EXPECT_FAILED(publish_stream->send_rtcp_rr());

    // Verify that send_rtcp_rr was called once
    EXPECT_EQ(1, mock_receiver.send_rtcp_rr_count_);
}

VOID TEST(RtcPublishStreamTest, SendRtcpRrNoTracks)
{
    srs_error_t err;

    // Create mock objects
    MockRtcAsyncTaskExecutor mock_exec;
    MockRtcExpire mock_expire;
    MockRtcPacketReceiver mock_receiver;
    SrsContextId cid;

    // Create publish stream
    SrsUniquePtr<SrsRtcPublishStream> publish_stream(new SrsRtcPublishStream(&mock_exec, &mock_expire, &mock_receiver, cid));

    // Test send_rtcp_rr with no tracks - should succeed (empty loops)
    HELPER_EXPECT_SUCCESS(publish_stream->send_rtcp_rr());
}

VOID TEST(RtcPublishStreamTest, SendRtcpRrMultipleTracks)
{
    srs_error_t err;

    // Create mock objects
    MockRtcAsyncTaskExecutor mock_exec;
    MockRtcExpire mock_expire;
    MockRtcPacketReceiver mock_receiver;
    SrsContextId cid;

    // Create publish stream
    SrsUniquePtr<SrsRtcPublishStream> publish_stream(new SrsRtcPublishStream(&mock_exec, &mock_expire, &mock_receiver, cid));

    // Create multiple video track descriptions
    SrsUniquePtr<SrsRtcTrackDescription> video_track_desc1(new SrsRtcTrackDescription());
    video_track_desc1->type_ = "video";
    video_track_desc1->id_ = "video_track_1";
    video_track_desc1->ssrc_ = 12345;

    SrsUniquePtr<SrsRtcTrackDescription> video_track_desc2(new SrsRtcTrackDescription());
    video_track_desc2->type_ = "video";
    video_track_desc2->id_ = "video_track_2";
    video_track_desc2->ssrc_ = 12346;

    // Create multiple audio track descriptions
    SrsUniquePtr<SrsRtcTrackDescription> audio_track_desc1(new SrsRtcTrackDescription());
    audio_track_desc1->type_ = "audio";
    audio_track_desc1->id_ = "audio_track_1";
    audio_track_desc1->ssrc_ = 67890;

    SrsUniquePtr<SrsRtcTrackDescription> audio_track_desc2(new SrsRtcTrackDescription());
    audio_track_desc2->type_ = "audio";
    audio_track_desc2->id_ = "audio_track_2";
    audio_track_desc2->ssrc_ = 67891;

    // Create tracks
    SrsRtcVideoRecvTrack *video_track1 = new SrsRtcVideoRecvTrack(&mock_receiver, video_track_desc1.get());
    SrsRtcVideoRecvTrack *video_track2 = new SrsRtcVideoRecvTrack(&mock_receiver, video_track_desc2.get());
    SrsRtcAudioRecvTrack *audio_track1 = new SrsRtcAudioRecvTrack(&mock_receiver, audio_track_desc1.get());
    SrsRtcAudioRecvTrack *audio_track2 = new SrsRtcAudioRecvTrack(&mock_receiver, audio_track_desc2.get());

    // Add tracks to publish stream
    publish_stream->video_tracks_.push_back(video_track1);
    publish_stream->video_tracks_.push_back(video_track2);
    publish_stream->audio_tracks_.push_back(audio_track1);
    publish_stream->audio_tracks_.push_back(audio_track2);

    // Reset receiver count before test
    mock_receiver.reset();

    // Test send_rtcp_rr - should succeed when all tracks succeed
    HELPER_EXPECT_SUCCESS(publish_stream->send_rtcp_rr());

    // Verify that send_rtcp_rr was called on all tracks (4 calls total)
    EXPECT_EQ(4, mock_receiver.send_rtcp_rr_count_);
}

VOID TEST(PlaintextTransportTest, MultipleHandshakeCalls)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    SrsUniquePtr<SrsPlaintextTransport> transport(new SrsPlaintextTransport(&mock_network));

    // Multiple handshake calls should all succeed and call network each time
    HELPER_EXPECT_SUCCESS(transport->start_active_handshake());
    EXPECT_EQ(1, mock_network.on_dtls_handshake_done_count_);

    HELPER_EXPECT_SUCCESS(transport->start_active_handshake());
    EXPECT_EQ(2, mock_network.on_dtls_handshake_done_count_);

    HELPER_EXPECT_SUCCESS(transport->on_dtls_handshake_done());
    EXPECT_EQ(3, mock_network.on_dtls_handshake_done_count_);
}

VOID TEST(PlaintextTransportTest, InitializeAndHandshakeSequence)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    SrsUniquePtr<SrsPlaintextTransport> transport(new SrsPlaintextTransport(&mock_network));

    // Initialize first
    SrsUniquePtr<SrsSessionConfig> cfg(new SrsSessionConfig());
    cfg->dtls_role_ = "active";
    cfg->dtls_version_ = "1.2";

    HELPER_EXPECT_SUCCESS(transport->initialize(cfg.get()));
    EXPECT_EQ(0, mock_network.on_dtls_handshake_done_count_);

    // Then start handshake
    HELPER_EXPECT_SUCCESS(transport->start_active_handshake());
    EXPECT_EQ(1, mock_network.on_dtls_handshake_done_count_);
}

VOID TEST(PlaintextTransportTest, HandshakeWithoutInitialize)
{
    srs_error_t err;

    MockRtcNetwork mock_network;
    SrsUniquePtr<SrsPlaintextTransport> transport(new SrsPlaintextTransport(&mock_network));

    // Handshake without initialize should still work
    HELPER_EXPECT_SUCCESS(transport->start_active_handshake());
    EXPECT_EQ(1, mock_network.on_dtls_handshake_done_count_);
}

// Mock PLI Worker Handler implementation
MockRtcPliWorkerHandler::MockRtcPliWorkerHandler()
{
    do_request_keyframe_error_ = srs_success;
    do_request_keyframe_count_ = 0;
}

MockRtcPliWorkerHandler::~MockRtcPliWorkerHandler()
{
    srs_freep(do_request_keyframe_error_);
}

srs_error_t MockRtcPliWorkerHandler::do_request_keyframe(uint32_t ssrc, SrsContextId cid)
{
    do_request_keyframe_count_++;
    keyframe_requests_.push_back(std::make_pair(ssrc, cid));

    if (do_request_keyframe_error_ != srs_success) {
        return srs_error_copy(do_request_keyframe_error_);
    }

    return srs_success;
}

void MockRtcPliWorkerHandler::reset()
{
    srs_freep(do_request_keyframe_error_);
    do_request_keyframe_error_ = srs_success;
    do_request_keyframe_count_ = 0;
    keyframe_requests_.clear();
}

void MockRtcPliWorkerHandler::set_do_request_keyframe_error(srs_error_t err)
{
    srs_freep(do_request_keyframe_error_);
    do_request_keyframe_error_ = srs_error_copy(err);
}

bool MockRtcPliWorkerHandler::has_keyframe_request(uint32_t ssrc, const SrsContextId &cid)
{
    for (size_t i = 0; i < keyframe_requests_.size(); i++) {
        if (keyframe_requests_[i].first == ssrc) {
            // Compare the string values of the context IDs
            std::string req_cid_str = keyframe_requests_[i].second.c_str();
            std::string target_cid_str = cid.c_str();
            if (req_cid_str == target_cid_str) {
                return true;
            }
        }
    }
    return false;
}

int MockRtcPliWorkerHandler::get_keyframe_request_count()
{
    return do_request_keyframe_count_;
}

MockRtcPliWorker::MockRtcPliWorker(ISrsRtcPliWorkerHandler *h) : SrsRtcPliWorker(h)
{
    request_keyframe_count_ = 0;
}

MockRtcPliWorker::~MockRtcPliWorker()
{
}

void MockRtcPliWorker::request_keyframe(uint32_t ssrc, SrsContextId cid)
{
    request_keyframe_count_++;
    keyframe_requests_.push_back(std::make_pair(ssrc, cid));
    // Don't call the parent implementation to avoid actual PLI processing
}

void MockRtcPliWorker::reset()
{
    request_keyframe_count_ = 0;
    keyframe_requests_.clear();
}

bool MockRtcPliWorker::has_keyframe_request(uint32_t ssrc, const SrsContextId &cid)
{
    for (std::vector<std::pair<uint32_t, SrsContextId> >::iterator it = keyframe_requests_.begin(); it != keyframe_requests_.end(); ++it) {
        if (it->first == ssrc && it->second.compare(cid) == 0) {
            return true;
        }
    }
    return false;
}

int MockRtcPliWorker::get_keyframe_request_count()
{
    return request_keyframe_count_;
}

VOID TEST(RtcPliWorkerTest, BasicFunctionality)
{
    srs_error_t err;

    MockRtcPliWorkerHandler mock_handler;
    SrsUniquePtr<SrsRtcPliWorker> worker(new SrsRtcPliWorker(&mock_handler));

    // Test starting the worker
    HELPER_EXPECT_SUCCESS(worker->start());

    // Request multiple keyframes with different SSRCs
    uint32_t ssrc1 = 12345;
    uint32_t ssrc2 = 67890;

    SrsContextId cid1;
    SrsContextId cid2;
    cid1.set_value("test-cid-001");
    cid2.set_value("test-cid-002");

    worker->request_keyframe(ssrc1, cid1);
    worker->request_keyframe(ssrc2, cid2);

    // Give the worker time to process requests
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // Verify both handlers were called
    EXPECT_EQ(2, mock_handler.get_keyframe_request_count());
    EXPECT_TRUE(mock_handler.has_keyframe_request(ssrc1, cid1));
    EXPECT_TRUE(mock_handler.has_keyframe_request(ssrc2, cid2));
}

VOID TEST(RtcPliWorkerTest, ErrorHandling)
{
    srs_error_t err;

    MockRtcPliWorkerHandler mock_handler;
    SrsUniquePtr<SrsRtcPliWorker> worker(new SrsRtcPliWorker(&mock_handler));

    // Test starting the worker
    HELPER_EXPECT_SUCCESS(worker->start());

    // Set up the mock handler to return an error
    srs_error_t test_error = srs_error_new(ERROR_RTC_RTCP, "test PLI error");
    mock_handler.set_do_request_keyframe_error(test_error);
    srs_freep(test_error);

    // Request a keyframe that should trigger the error
    uint32_t ssrc = 12345;
    SrsContextId cid;
    cid.set_value("test-cid-error");

    worker->request_keyframe(ssrc, cid);

    // Give the worker time to process the request and handle the error
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // Verify the handler was called despite the error
    EXPECT_EQ(1, mock_handler.get_keyframe_request_count());
    EXPECT_TRUE(mock_handler.has_keyframe_request(ssrc, cid));

    // Test that the worker continues to work after handling the error
    mock_handler.reset();

    // Request another keyframe with no error
    uint32_t ssrc2 = 67890;
    SrsContextId cid2;
    cid2.set_value("test-cid-success");

    worker->request_keyframe(ssrc2, cid2);

    // Give the worker time to process the successful request
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // Verify the worker recovered and processed the successful request
    EXPECT_EQ(1, mock_handler.get_keyframe_request_count());
    EXPECT_TRUE(mock_handler.has_keyframe_request(ssrc2, cid2));
}

// Mock HTTP hooks implementation
MockHttpHooks::MockHttpHooks()
{
    on_stop_count_ = 0;
    on_unpublish_count_ = 0;
}

MockHttpHooks::~MockHttpHooks()
{
    clear_calls();
}

srs_error_t MockHttpHooks::on_connect(std::string url, ISrsRequest *req)
{
    return srs_success;
}

void MockHttpHooks::on_close(std::string url, ISrsRequest *req, int64_t send_bytes, int64_t recv_bytes)
{
}

srs_error_t MockHttpHooks::on_publish(std::string url, ISrsRequest *req)
{
    return srs_success;
}

void MockHttpHooks::on_unpublish(std::string url, ISrsRequest *req)
{
    on_unpublish_count_++;
    on_unpublish_calls_.push_back(std::make_pair(url, req));
}

srs_error_t MockHttpHooks::on_play(std::string url, ISrsRequest *req)
{
    return srs_success;
}

void MockHttpHooks::on_stop(std::string url, ISrsRequest *req)
{
    on_stop_count_++;
    on_stop_calls_.push_back(std::make_pair(url, req));
}

srs_error_t MockHttpHooks::on_dvr(SrsContextId cid, std::string url, ISrsRequest *req, std::string file)
{
    return srs_success;
}

srs_error_t MockHttpHooks::on_hls(SrsContextId cid, std::string url, ISrsRequest *req, std::string file, std::string ts_url,
                                  std::string m3u8, std::string m3u8_url, int sn, srs_utime_t duration)
{
    return srs_success;
}

srs_error_t MockHttpHooks::on_hls_notify(SrsContextId cid, std::string url, ISrsRequest *req, std::string ts_url, int nb_notify)
{
    return srs_success;
}

srs_error_t MockHttpHooks::discover_co_workers(std::string url, std::string &host, int &port)
{
    return srs_success;
}

srs_error_t MockHttpHooks::on_forward_backend(std::string url, ISrsRequest *req, std::vector<std::string> &rtmp_urls)
{
    return srs_success;
}

void MockHttpHooks::clear_calls()
{
    on_stop_calls_.clear();
    on_stop_count_ = 0;
    on_unpublish_calls_.clear();
    on_unpublish_count_ = 0;
}

// Mock context implementation
MockContext::MockContext()
{
    current_id_.set_value("mock-context-id");
    get_id_result_.set_value("mock-get-id");
}

MockContext::~MockContext()
{
}

SrsContextId MockContext::generate_id()
{
    SrsContextId id;
    return id.set_value("generated-id");
}

const SrsContextId &MockContext::get_id()
{
    return get_id_result_;
}

const SrsContextId &MockContext::set_id(const SrsContextId &v)
{
    current_id_ = v;
    return current_id_;
}

void MockContext::set_current_id(const SrsContextId &id)
{
    current_id_ = id;
    get_id_result_ = id;
}

// Mock app config implementation
MockAppConfig::MockAppConfig()
{
    http_hooks_enabled_ = true;
    on_stop_directive_ = NULL;
    on_unpublish_directive_ = NULL;
    rtc_nack_enabled_ = true;
    rtc_nack_no_copy_ = false;
    rtc_drop_for_pt_ = 0;
    rtc_twcc_enabled_ = true;
    srt_enabled_ = false;
    rtc_to_rtmp_ = false;
    dash_dispose_ = 0;
    dash_enabled_ = false;
    api_as_candidates_ = true;
    resolve_api_domain_ = true;
    keep_api_domain_ = false;
}

MockAppConfig::~MockAppConfig()
{
    clear_on_stop_directive();
    clear_on_unpublish_directive();
}

srs_utime_t MockAppConfig::get_pithy_print()
{
    return 10 * SRS_UTIME_SECONDS;
}

std::string MockAppConfig::get_default_app_name()
{
    return "live";
}

void MockAppConfig::subscribe(ISrsReloadHandler *handler)
{
    // Do nothing in mock
}

void MockAppConfig::unsubscribe(ISrsReloadHandler *handler)
{
    // Do nothing in mock
}

bool MockAppConfig::get_vhost_http_hooks_enabled(std::string vhost)
{
    return http_hooks_enabled_;
}

SrsConfDirective *MockAppConfig::get_vhost_on_stop(std::string vhost)
{
    return on_stop_directive_;
}

SrsConfDirective *MockAppConfig::get_vhost_on_unpublish(std::string vhost)
{
    return on_unpublish_directive_;
}

SrsConfDirective *MockAppConfig::get_vhost_on_dvr(std::string vhost)
{
    return NULL;
}

bool MockAppConfig::get_rtc_nack_enabled(std::string vhost)
{
    return rtc_nack_enabled_;
}

bool MockAppConfig::get_rtc_nack_no_copy(std::string vhost)
{
    return rtc_nack_no_copy_;
}

bool MockAppConfig::get_realtime_enabled(std::string vhost, bool is_rtc)
{
    return true;
}

int MockAppConfig::get_mw_msgs(std::string vhost, bool is_realtime, bool is_rtc)
{
    return 8;
}

int MockAppConfig::get_rtc_drop_for_pt(std::string vhost)
{
    return rtc_drop_for_pt_;
}

bool MockAppConfig::get_rtc_twcc_enabled(std::string vhost)
{
    return rtc_twcc_enabled_;
}

bool MockAppConfig::get_srt_enabled()
{
    return srt_enabled_;
}

bool MockAppConfig::get_srt_enabled(std::string vhost)
{
    return srt_enabled_;
}

std::string MockAppConfig::get_srt_default_streamid()
{
    return "#!::r=live/livestream,m=request";
}

bool MockAppConfig::get_srt_to_rtmp(std::string vhost)
{
    return true;
}

bool MockAppConfig::get_rtc_to_rtmp(std::string vhost)
{
    return rtc_to_rtmp_;
}

srs_utime_t MockAppConfig::get_rtc_stun_timeout(std::string vhost)
{
    return 30 * SRS_UTIME_SECONDS; // Default 30 seconds timeout
}

bool MockAppConfig::get_rtc_stun_strict_check(std::string vhost)
{
    return false; // Default to non-strict mode
}

std::string MockAppConfig::get_rtc_dtls_role(std::string vhost)
{
    return "passive"; // Default DTLS role
}

std::string MockAppConfig::get_rtc_dtls_version(std::string vhost)
{
    return "auto"; // Default DTLS version
}

SrsConfDirective *MockAppConfig::get_vhost_on_hls(std::string vhost)
{
    return NULL;
}

SrsConfDirective *MockAppConfig::get_vhost_on_hls_notify(std::string vhost)
{
    return NULL;
}

bool MockAppConfig::get_hls_enabled(std::string vhost)
{
    return false;
}

bool MockAppConfig::get_hls_enabled(SrsConfDirective *vhost)
{
    return false;
}

bool MockAppConfig::get_hls_use_fmp4(std::string vhost)
{
    return false;
}

std::string MockAppConfig::get_hls_entry_prefix(std::string vhost)
{
    return "";
}

std::string MockAppConfig::get_hls_path(std::string vhost)
{
    return "./objs/nginx/html";
}

std::string MockAppConfig::get_hls_m3u8_file(std::string vhost)
{
    return "[app]/[stream].m3u8";
}

std::string MockAppConfig::get_hls_ts_file(std::string vhost)
{
    return "[app]/[stream]-[seq].ts";
}

std::string MockAppConfig::get_hls_fmp4_file(std::string vhost)
{
    return "[app]/[stream]-[seq].m4s";
}

std::string MockAppConfig::get_hls_init_file(std::string vhost)
{
    return "[app]/[stream]/init.mp4";
}

bool MockAppConfig::get_hls_ts_floor(std::string vhost)
{
    return false;
}

srs_utime_t MockAppConfig::get_hls_fragment(std::string vhost)
{
    return 10 * SRS_UTIME_SECONDS;
}

double MockAppConfig::get_hls_td_ratio(std::string vhost)
{
    return 1.5;
}

double MockAppConfig::get_hls_aof_ratio(std::string vhost)
{
    return 2.0;
}

srs_utime_t MockAppConfig::get_hls_window(std::string vhost)
{
    return 60 * SRS_UTIME_SECONDS;
}

std::string MockAppConfig::get_hls_on_error(std::string vhost)
{
    return "continue";
}

bool MockAppConfig::get_hls_cleanup(std::string vhost)
{
    return true;
}

srs_utime_t MockAppConfig::get_hls_dispose(std::string vhost)
{
    return 120 * SRS_UTIME_SECONDS;
}

bool MockAppConfig::get_hls_wait_keyframe(std::string vhost)
{
    return true;
}

bool MockAppConfig::get_hls_keys(std::string vhost)
{
    return false;
}

int MockAppConfig::get_hls_fragments_per_key(std::string vhost)
{
    return 5;
}

std::string MockAppConfig::get_hls_key_file(std::string vhost)
{
    return "[app]/[stream]-[seq].key";
}

std::string MockAppConfig::get_hls_key_file_path(std::string vhost)
{
    return "./objs/nginx/html";
}

std::string MockAppConfig::get_hls_key_url(std::string vhost)
{
    return "";
}

int MockAppConfig::get_vhost_hls_nb_notify(std::string vhost)
{
    return 64;
}

bool MockAppConfig::get_vhost_hls_dts_directly(std::string vhost)
{
    return true;
}

bool MockAppConfig::get_hls_ctx_enabled(std::string vhost)
{
    return true;
}

bool MockAppConfig::get_hls_ts_ctx_enabled(std::string vhost)
{
    return true;
}

bool MockAppConfig::get_hls_recover(std::string vhost)
{
    return true;
}

bool MockAppConfig::get_forward_enabled(std::string vhost)
{
    return false;
}

SrsConfDirective *MockAppConfig::get_forwards(std::string vhost)
{
    return NULL;
}

srs_utime_t MockAppConfig::get_queue_length(std::string vhost)
{
    return 30 * SRS_UTIME_SECONDS;
}

SrsConfDirective *MockAppConfig::get_forward_backend(std::string vhost)
{
    return NULL;
}

bool MockAppConfig::get_atc(std::string vhost)
{
    return false;
}

int MockAppConfig::get_time_jitter(std::string vhost)
{
    return SrsRtmpJitterAlgorithmFULL;
}

bool MockAppConfig::get_mix_correct(std::string vhost)
{
    return false;
}

bool MockAppConfig::try_annexb_first(std::string vhost)
{
    return true;
}

bool MockAppConfig::get_vhost_is_edge(std::string vhost)
{
    return false;
}

bool MockAppConfig::get_atc_auto(std::string vhost)
{
    return false;
}

bool MockAppConfig::get_reduce_sequence_header(std::string vhost)
{
    return false;
}

bool MockAppConfig::get_parse_sps(std::string vhost)
{
    return true;
}

void MockAppConfig::set_http_hooks_enabled(bool enabled)
{
    http_hooks_enabled_ = enabled;
}

void MockAppConfig::set_on_stop_urls(const std::vector<std::string> &urls)
{
    clear_on_stop_directive();
    if (!urls.empty()) {
        on_stop_directive_ = new SrsConfDirective();
        on_stop_directive_->name_ = "on_stop";
        on_stop_directive_->args_ = urls;
    }
}

void MockAppConfig::clear_on_stop_directive()
{
    srs_freep(on_stop_directive_);
}

void MockAppConfig::set_on_unpublish_urls(const std::vector<std::string> &urls)
{
    clear_on_unpublish_directive();
    if (!urls.empty()) {
        on_unpublish_directive_ = new SrsConfDirective();
        on_unpublish_directive_->name_ = "on_unpublish";
        on_unpublish_directive_->args_ = urls;
    }
}

void MockAppConfig::clear_on_unpublish_directive()
{
    srs_freep(on_unpublish_directive_);
}

void MockAppConfig::set_rtc_nack_enabled(bool enabled)
{
    rtc_nack_enabled_ = enabled;
}

void MockAppConfig::set_rtc_nack_no_copy(bool no_copy)
{
    rtc_nack_no_copy_ = no_copy;
}

void MockAppConfig::set_rtc_drop_for_pt(int pt)
{
    rtc_drop_for_pt_ = pt;
}

void MockAppConfig::set_rtc_twcc_enabled(bool enabled)
{
    rtc_twcc_enabled_ = enabled;
}

void MockAppConfig::set_srt_enabled(bool enabled)
{
    srt_enabled_ = enabled;
}

void MockAppConfig::set_rtc_to_rtmp(bool enabled)
{
    rtc_to_rtmp_ = enabled;
}

void MockAppConfig::set_api_as_candidates(bool enabled)
{
    api_as_candidates_ = enabled;
}

void MockAppConfig::set_resolve_api_domain(bool enabled)
{
    resolve_api_domain_ = enabled;
}

void MockAppConfig::set_keep_api_domain(bool enabled)
{
    keep_api_domain_ = enabled;
}

// Mock request implementation
MockRtcAsyncCallRequest::MockRtcAsyncCallRequest(std::string vhost, std::string app, std::string stream)
{
    vhost_ = vhost;
    app_ = app;
    stream_ = stream;
}

MockRtcAsyncCallRequest::~MockRtcAsyncCallRequest()
{
}

ISrsRequest *MockRtcAsyncCallRequest::copy()
{
    MockRtcAsyncCallRequest *cp = new MockRtcAsyncCallRequest(vhost_, app_, stream_);
    return cp;
}

std::string MockRtcAsyncCallRequest::get_stream_url()
{
    return "rtmp://" + vhost_ + "/" + app_ + "/" + stream_;
}

void MockRtcAsyncCallRequest::update_auth(ISrsRequest *req)
{
}

void MockRtcAsyncCallRequest::strip()
{
}

ISrsRequest *MockRtcAsyncCallRequest::as_http()
{
    return this;
}

// Mock RTC source manager implementation
MockRtcSourceManager::MockRtcSourceManager()
{
    initialize_error_ = srs_success;
    fetch_or_create_error_ = srs_success;
    initialize_count_ = 0;
    fetch_or_create_count_ = 0;
    mock_source_ = SrsSharedPtr<SrsRtcSource>(new SrsRtcSource());
}

MockRtcSourceManager::~MockRtcSourceManager()
{
    srs_freep(initialize_error_);
    srs_freep(fetch_or_create_error_);
}

srs_error_t MockRtcSourceManager::initialize()
{
    initialize_count_++;
    return srs_error_copy(initialize_error_);
}

srs_error_t MockRtcSourceManager::fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsRtcSource> &pps)
{
    fetch_or_create_count_++;
    if (fetch_or_create_error_ != srs_success) {
        return srs_error_copy(fetch_or_create_error_);
    }
    pps = mock_source_;
    return srs_success;
}

SrsSharedPtr<SrsRtcSource> MockRtcSourceManager::fetch(ISrsRequest *r)
{
    return mock_source_;
}

void MockRtcSourceManager::set_initialize_error(srs_error_t err)
{
    srs_freep(initialize_error_);
    initialize_error_ = srs_error_copy(err);
}

void MockRtcSourceManager::set_fetch_or_create_error(srs_error_t err)
{
    srs_freep(fetch_or_create_error_);
    fetch_or_create_error_ = srs_error_copy(err);
}

void MockRtcSourceManager::reset()
{
    srs_freep(initialize_error_);
    srs_freep(fetch_or_create_error_);
    initialize_error_ = srs_success;
    fetch_or_create_error_ = srs_success;
    initialize_count_ = 0;
    fetch_or_create_count_ = 0;
}

// Mock statistic implementation
MockRtcStatistic::MockRtcStatistic()
{
    on_client_error_ = srs_success;
    on_client_count_ = 0;
    on_disconnect_count_ = 0;
    last_client_id_ = "";
    last_client_req_ = NULL;
    last_client_conn_ = NULL;
    last_client_type_ = SrsRtmpConnUnknown;
}

MockRtcStatistic::~MockRtcStatistic()
{
    srs_freep(on_client_error_);
}

void MockRtcStatistic::on_disconnect(std::string id, srs_error_t err)
{
    on_disconnect_count_++;
}

srs_error_t MockRtcStatistic::on_client(std::string id, ISrsRequest *req, ISrsExpire *conn, SrsRtmpConnType type)
{
    on_client_count_++;
    last_client_id_ = id;
    last_client_req_ = req;
    last_client_conn_ = conn;
    last_client_type_ = type;
    return srs_error_copy(on_client_error_);
}

void MockRtcStatistic::set_on_client_error(srs_error_t err)
{
    srs_freep(on_client_error_);
    on_client_error_ = srs_error_copy(err);
}

void MockRtcStatistic::reset()
{
    srs_freep(on_client_error_);
    on_client_error_ = srs_success;
    on_client_count_ = 0;
    on_disconnect_count_ = 0;
    last_client_id_ = "";
    last_client_req_ = NULL;
    last_client_conn_ = NULL;
    last_client_type_ = SrsRtmpConnUnknown;
}

srs_error_t MockRtcStatistic::on_video_info(ISrsRequest *req, SrsVideoCodecId vcodec, int avc_profile, int avc_level, int width, int height)
{
    return srs_success;
}

srs_error_t MockRtcStatistic::on_audio_info(ISrsRequest *req, SrsAudioCodecId acodec, SrsAudioSampleRate asample_rate, SrsAudioChannels asound_type, SrsAacObjectType aac_object)
{
    return srs_success;
}

void MockRtcStatistic::on_stream_publish(ISrsRequest *req, std::string publisher_id)
{
    // Do nothing in mock
}

void MockRtcStatistic::on_stream_close(ISrsRequest *req)
{
    // Do nothing in mock
}

void MockRtcStatistic::kbps_add_delta(std::string id, ISrsKbpsDelta *delta)
{
    // Do nothing in mock
}

void MockRtcStatistic::kbps_sample()
{
    // Do nothing in mock
}

srs_error_t MockRtcStatistic::on_video_frames(ISrsRequest *req, int nb_frames)
{
    return srs_success;
}

std::string MockRtcStatistic::server_id()
{
    return "mock_server_id";
}

std::string MockRtcStatistic::service_id()
{
    return "mock_service_id";
}

std::string MockRtcStatistic::service_pid()
{
    return "mock_pid";
}

SrsStatisticVhost *MockRtcStatistic::find_vhost_by_id(std::string vid)
{
    return NULL;
}

SrsStatisticStream *MockRtcStatistic::find_stream(std::string sid)
{
    return NULL;
}

SrsStatisticStream *MockRtcStatistic::find_stream_by_url(std::string url)
{
    return NULL;
}

SrsStatisticClient *MockRtcStatistic::find_client(std::string client_id)
{
    return NULL;
}

srs_error_t MockRtcStatistic::dumps_vhosts(SrsJsonArray *arr)
{
    return srs_success;
}

srs_error_t MockRtcStatistic::dumps_streams(SrsJsonArray *arr, int start, int count)
{
    return srs_success;
}

srs_error_t MockRtcStatistic::dumps_clients(SrsJsonArray *arr, int start, int count)
{
    return srs_success;
}

srs_error_t MockRtcStatistic::dumps_metrics(int64_t &send_bytes, int64_t &recv_bytes, int64_t &nstreams, int64_t &nclients, int64_t &total_nclients, int64_t &nerrs)
{
    send_bytes = 0;
    recv_bytes = 0;
    nstreams = 0;
    nclients = 0;
    total_nclients = 0;
    nerrs = 0;
    return srs_success;
}

// Unit tests for SrsRtcAsyncCallOnStop::call()
VOID TEST(RtcAsyncCallOnStopTest, CallWithHttpHooksDisabled)
{
    srs_error_t err;

    // Create mock objects
    MockAppConfig mock_config;
    MockHttpHooks mock_hooks;
    MockContext mock_context;
    MockRtcAsyncCallRequest mock_request("test.vhost", "live", "stream1");

    // Disable HTTP hooks
    mock_config.set_http_hooks_enabled(false);

    // Create SrsRtcAsyncCallOnStop with mocked dependencies
    SrsContextId cid;
    cid.set_value("test-context-id");

    SrsUniquePtr<SrsRtcAsyncCallOnStop> async_call(new SrsRtcAsyncCallOnStop(cid, &mock_request));

    // Replace the dependencies with mocks (private members are accessible due to #define private public)
    async_call->hooks_ = &mock_hooks;
    async_call->context_ = &mock_context;
    async_call->config_ = &mock_config;

    // Call should succeed but not invoke hooks since they're disabled
    HELPER_EXPECT_SUCCESS(async_call->call());

    // Verify no hooks were called
    EXPECT_EQ(0, mock_hooks.on_stop_count_);
    EXPECT_EQ(0, (int)mock_hooks.on_stop_calls_.size());
}

VOID TEST(RtcAsyncCallOnStopTest, CallWithNoOnStopConfiguration)
{
    srs_error_t err;

    // Create mock objects
    MockAppConfig mock_config;
    MockHttpHooks mock_hooks;
    MockContext mock_context;
    MockRtcAsyncCallRequest mock_request("test.vhost", "live", "stream1");

    // Enable HTTP hooks but don't configure on_stop URLs
    mock_config.set_http_hooks_enabled(true);
    // on_stop_directive_ remains NULL

    // Create SrsRtcAsyncCallOnStop with mocked dependencies
    SrsContextId cid;
    cid.set_value("test-context-id");

    SrsUniquePtr<SrsRtcAsyncCallOnStop> async_call(new SrsRtcAsyncCallOnStop(cid, &mock_request));

    // Replace the dependencies with mocks
    async_call->hooks_ = &mock_hooks;
    async_call->context_ = &mock_context;
    async_call->config_ = &mock_config;

    // Call should succeed but not invoke hooks since no URLs are configured
    HELPER_EXPECT_SUCCESS(async_call->call());

    // Verify no hooks were called
    EXPECT_EQ(0, mock_hooks.on_stop_count_);
    EXPECT_EQ(0, (int)mock_hooks.on_stop_calls_.size());
}

VOID TEST(RtcAsyncCallOnStopTest, CallWithSingleOnStopUrl)
{
    srs_error_t err;

    // Create mock objects
    MockAppConfig mock_config;
    MockHttpHooks mock_hooks;
    MockContext mock_context;
    MockRtcAsyncCallRequest mock_request("test.vhost", "live", "stream1");

    // Enable HTTP hooks and configure single on_stop URL
    mock_config.set_http_hooks_enabled(true);
    std::vector<std::string> urls;
    urls.push_back("http://callback.server.com/on_stop");
    mock_config.set_on_stop_urls(urls);

    // Create SrsRtcAsyncCallOnStop with mocked dependencies
    SrsContextId cid;
    cid.set_value("test-context-id");

    SrsUniquePtr<SrsRtcAsyncCallOnStop> async_call(new SrsRtcAsyncCallOnStop(cid, &mock_request));

    // Replace the dependencies with mocks
    async_call->hooks_ = &mock_hooks;
    async_call->context_ = &mock_context;
    async_call->config_ = &mock_config;

    // Call should succeed and invoke hooks
    HELPER_EXPECT_SUCCESS(async_call->call());

    // Verify hooks were called once
    EXPECT_EQ(1, mock_hooks.on_stop_count_);
    EXPECT_EQ(1, (int)mock_hooks.on_stop_calls_.size());
    EXPECT_STREQ("http://callback.server.com/on_stop", mock_hooks.on_stop_calls_[0].first.c_str());
    // Note: The request pointer will be different because SrsRtcAsyncCallOnStop creates a copy
    EXPECT_TRUE(mock_hooks.on_stop_calls_[0].second != NULL);
}

VOID TEST(RtcAsyncCallOnStopTest, CallWithMultipleOnStopUrls)
{
    srs_error_t err;

    // Create mock objects
    MockAppConfig mock_config;
    MockHttpHooks mock_hooks;
    MockContext mock_context;
    MockRtcAsyncCallRequest mock_request("test.vhost", "live", "stream1");

    // Enable HTTP hooks and configure multiple on_stop URLs
    mock_config.set_http_hooks_enabled(true);
    std::vector<std::string> urls;
    urls.push_back("http://callback1.server.com/on_stop");
    urls.push_back("http://callback2.server.com/on_stop");
    urls.push_back("http://callback3.server.com/on_stop");
    mock_config.set_on_stop_urls(urls);

    // Create SrsRtcAsyncCallOnStop with mocked dependencies
    SrsContextId cid;
    cid.set_value("test-context-id");

    SrsUniquePtr<SrsRtcAsyncCallOnStop> async_call(new SrsRtcAsyncCallOnStop(cid, &mock_request));

    // Replace the dependencies with mocks
    async_call->hooks_ = &mock_hooks;
    async_call->context_ = &mock_context;
    async_call->config_ = &mock_config;

    // Call should succeed and invoke all hooks
    HELPER_EXPECT_SUCCESS(async_call->call());

    // Verify all hooks were called
    EXPECT_EQ(3, mock_hooks.on_stop_count_);
    EXPECT_EQ(3, (int)mock_hooks.on_stop_calls_.size());
    EXPECT_STREQ("http://callback1.server.com/on_stop", mock_hooks.on_stop_calls_[0].first.c_str());
    EXPECT_STREQ("http://callback2.server.com/on_stop", mock_hooks.on_stop_calls_[1].first.c_str());
    EXPECT_STREQ("http://callback3.server.com/on_stop", mock_hooks.on_stop_calls_[2].first.c_str());

    // All calls should use the same request object (but different from original due to copy)
    for (int i = 0; i < 3; i++) {
        // Note: The request pointer will be different because SrsRtcAsyncCallOnStop creates a copy
        EXPECT_TRUE(mock_hooks.on_stop_calls_[i].second != NULL);
    }
}

VOID TEST(RtcAsyncCallOnStopTest, CallWithContextSwitching)
{
    srs_error_t err;

    // Create mock objects
    MockAppConfig mock_config;
    MockHttpHooks mock_hooks;
    MockContext mock_context;
    MockRtcAsyncCallRequest mock_request("test.vhost", "live", "stream1");

    // Enable HTTP hooks and configure on_stop URL
    mock_config.set_http_hooks_enabled(true);
    std::vector<std::string> urls;
    urls.push_back("http://callback.server.com/on_stop");
    mock_config.set_on_stop_urls(urls);

    // Set up context IDs
    SrsContextId original_cid;
    original_cid.set_value("original-context-id");
    SrsContextId new_cid;
    new_cid.set_value("new-context-id");

    // Set the mock context to return the original context ID initially
    mock_context.set_current_id(original_cid);

    // Create SrsRtcAsyncCallOnStop with the new context ID
    SrsUniquePtr<SrsRtcAsyncCallOnStop> async_call(new SrsRtcAsyncCallOnStop(new_cid, &mock_request));

    // Replace the dependencies with mocks
    async_call->hooks_ = &mock_hooks;
    async_call->context_ = &mock_context;
    async_call->config_ = &mock_config;

    // Call should succeed and perform context switching
    HELPER_EXPECT_SUCCESS(async_call->call());

    // Verify hooks were called
    EXPECT_EQ(1, mock_hooks.on_stop_count_);
    EXPECT_EQ(1, (int)mock_hooks.on_stop_calls_.size());

    // Verify context was switched to the new context ID
    EXPECT_EQ(0, mock_context.current_id_.compare(new_cid));
}

VOID TEST(RtcPlayStreamTest, InitializeSuccess)
{
    srs_error_t err;

    // Create mock objects
    MockAppConfig mock_config;
    MockRtcSourceManager mock_rtc_sources;
    MockRtcStatistic mock_stat;
    MockRtcAsyncCallRequest mock_request("test.vhost", "live", "stream1");
    MockRtcAsyncTaskExecutor mock_async_executor;
    MockExpire mock_expire;
    MockRtcPacketSender mock_packet_sender;

    // Create RTC play stream with mock interfaces
    SrsContextId cid;
    cid.set_value("test-play-stream-cid");
    SrsUniquePtr<SrsRtcPlayStream> play_stream(new SrsRtcPlayStream(&mock_async_executor, &mock_expire, &mock_packet_sender, cid));

    // Mock the dependencies by setting the private members
    play_stream->config_ = &mock_config;
    play_stream->rtc_sources_ = &mock_rtc_sources;
    play_stream->stat_ = &mock_stat;

    // Create track descriptions for testing
    std::map<uint32_t, SrsRtcTrackDescription *> sub_relations;

    // Create audio track description
    SrsRtcTrackDescription *audio_desc = new SrsRtcTrackDescription();
    audio_desc->type_ = "audio";
    audio_desc->id_ = "audio-track-id";
    audio_desc->ssrc_ = 12345;
    audio_desc->is_active_ = true;
    sub_relations[12345] = audio_desc;

    // Create video track description
    SrsRtcTrackDescription *video_desc = new SrsRtcTrackDescription();
    video_desc->type_ = "video";
    video_desc->id_ = "video-track-id";
    video_desc->ssrc_ = 67890;
    video_desc->is_active_ = true;
    sub_relations[67890] = video_desc;

    // Initialize should succeed
    HELPER_EXPECT_SUCCESS(play_stream->initialize(&mock_request, sub_relations));

    // Verify stat->on_client was called
    EXPECT_EQ(1, mock_stat.on_client_count_);
    EXPECT_STREQ(cid.c_str(), mock_stat.last_client_id_.c_str());
    EXPECT_EQ(SrsRtcConnPlay, mock_stat.last_client_type_);

    // Verify rtc_sources->fetch_or_create was called
    EXPECT_EQ(1, mock_rtc_sources.fetch_or_create_count_);

    // Verify tracks were created
    EXPECT_EQ(1, (int)play_stream->audio_tracks_.size());
    EXPECT_EQ(1, (int)play_stream->video_tracks_.size());
    EXPECT_TRUE(play_stream->audio_tracks_.find(12345) != play_stream->audio_tracks_.end());
    EXPECT_TRUE(play_stream->video_tracks_.find(67890) != play_stream->video_tracks_.end());

    // Verify NACK configuration was applied
    EXPECT_TRUE(play_stream->nack_enabled_);
    EXPECT_FALSE(play_stream->nack_no_copy_);

    // Verify tracks have NACK configuration applied
    SrsRtcAudioSendTrack *audio_track = play_stream->audio_tracks_[12345];
    SrsRtcVideoSendTrack *video_track = play_stream->video_tracks_[67890];
    EXPECT_TRUE(audio_track != NULL);
    EXPECT_TRUE(video_track != NULL);

    // Clean up track descriptions
    srs_freep(audio_desc);
    srs_freep(video_desc);
}

VOID TEST(RtcPlayStreamTest, OnStreamChangeSuccess)
{
    srs_error_t err;

    // Create mock objects
    MockAppConfig mock_config;
    MockRtcSourceManager mock_rtc_sources;
    MockRtcStatistic mock_stat;
    MockRtcAsyncCallRequest mock_request("test.vhost", "live", "stream1");
    MockRtcAsyncTaskExecutor mock_async_executor;
    MockExpire mock_expire;
    MockRtcPacketSender mock_packet_sender;

    // Create RTC play stream with mock interfaces
    SrsContextId cid;
    cid.set_value("test-stream-change-cid");
    SrsUniquePtr<SrsRtcPlayStream> play_stream(new SrsRtcPlayStream(&mock_async_executor, &mock_expire, &mock_packet_sender, cid));

    // Mock the dependencies by setting the private members
    play_stream->config_ = &mock_config;
    play_stream->rtc_sources_ = &mock_rtc_sources;
    play_stream->stat_ = &mock_stat;

    // Create mock PLI worker and replace the real one
    MockRtcPliWorker *mock_pli_worker = new MockRtcPliWorker(play_stream.get());
    srs_freep(play_stream->pli_worker_); // Free the original PLI worker
    play_stream->pli_worker_ = mock_pli_worker;

    // Create initial track descriptions for testing
    std::map<uint32_t, SrsRtcTrackDescription *> sub_relations;

    // Create audio track description
    SrsRtcTrackDescription *audio_desc = new SrsRtcTrackDescription();
    audio_desc->type_ = "audio";
    audio_desc->id_ = "audio-track-id";
    audio_desc->ssrc_ = 12345;
    audio_desc->is_active_ = true;
    audio_desc->media_ = new SrsCodecPayload();
    audio_desc->media_->pt_ = 111;
    audio_desc->media_->pt_of_publisher_ = 111;
    sub_relations[12345] = audio_desc;

    // Create video track description
    SrsRtcTrackDescription *video_desc = new SrsRtcTrackDescription();
    video_desc->type_ = "video";
    video_desc->id_ = "video-track-id";
    video_desc->ssrc_ = 67890;
    video_desc->is_active_ = true;
    video_desc->media_ = new SrsCodecPayload();
    video_desc->media_->pt_ = 96;
    video_desc->media_->pt_of_publisher_ = 96;
    sub_relations[67890] = video_desc;

    // Initialize the play stream first
    HELPER_EXPECT_SUCCESS(play_stream->initialize(&mock_request, sub_relations));

    // Reset PLI worker to clear any initialization requests
    mock_pli_worker->reset();

    // Create source description for stream change
    SrsRtcSourceDescription *source_desc = new SrsRtcSourceDescription();

    // Create new audio track description with different payload type
    SrsRtcTrackDescription *new_audio_desc = new SrsRtcTrackDescription();
    new_audio_desc->type_ = "audio";
    new_audio_desc->id_ = "new-audio-track-id";
    new_audio_desc->ssrc_ = 54321; // Different SSRC
    new_audio_desc->is_active_ = true;
    new_audio_desc->media_ = new SrsCodecPayload();
    new_audio_desc->media_->pt_ = 112; // Different payload type
    new_audio_desc->media_->pt_of_publisher_ = 112;
    source_desc->audio_track_desc_ = new_audio_desc;

    // Create new video track description with different payload type
    SrsRtcTrackDescription *new_video_desc = new SrsRtcTrackDescription();
    new_video_desc->type_ = "video";
    new_video_desc->id_ = "new-video-track-id";
    new_video_desc->ssrc_ = 98765; // Different SSRC
    new_video_desc->is_active_ = true;
    new_video_desc->media_ = new SrsCodecPayload();
    new_video_desc->media_->pt_ = 97; // Different payload type
    new_video_desc->media_->pt_of_publisher_ = 97;
    source_desc->video_track_descs_.push_back(new_video_desc);

    // Call on_stream_change
    play_stream->on_stream_change(source_desc);

    // Verify PLI worker received keyframe requests
    EXPECT_EQ(2, mock_pli_worker->get_keyframe_request_count());
    EXPECT_TRUE(mock_pli_worker->has_keyframe_request(54321, cid)); // Audio SSRC
    EXPECT_TRUE(mock_pli_worker->has_keyframe_request(98765, cid)); // Video SSRC

    // Verify audio track was updated with new SSRC and payload type
    EXPECT_EQ(1, (int)play_stream->audio_tracks_.size());
    EXPECT_TRUE(play_stream->audio_tracks_.find(54321) != play_stream->audio_tracks_.end());
    EXPECT_TRUE(play_stream->audio_tracks_.find(12345) == play_stream->audio_tracks_.end());
    SrsRtcAudioSendTrack *updated_audio_track = play_stream->audio_tracks_[54321];
    EXPECT_TRUE(updated_audio_track != NULL);
    EXPECT_EQ(112, updated_audio_track->track_desc_->media_->pt_of_publisher_);

    // Verify video track was updated with new SSRC and payload type
    EXPECT_EQ(1, (int)play_stream->video_tracks_.size());
    EXPECT_TRUE(play_stream->video_tracks_.find(98765) != play_stream->video_tracks_.end());
    EXPECT_TRUE(play_stream->video_tracks_.find(67890) == play_stream->video_tracks_.end());
    SrsRtcVideoSendTrack *updated_video_track = play_stream->video_tracks_[98765];
    EXPECT_TRUE(updated_video_track != NULL);
    EXPECT_EQ(97, updated_video_track->track_desc_->media_->pt_of_publisher_);

    // Clean up
    srs_freep(audio_desc);
    srs_freep(video_desc);
    srs_freep(source_desc);
}

// Mock RTC async task executor implementation
MockRtcAsyncTaskExecutor::MockRtcAsyncTaskExecutor()
{
    exec_error_ = srs_success;
    exec_count_ = 0;
    last_task_ = NULL;
}

MockRtcAsyncTaskExecutor::~MockRtcAsyncTaskExecutor()
{
}

srs_error_t MockRtcAsyncTaskExecutor::exec_rtc_async_work(ISrsAsyncCallTask *t)
{
    exec_count_++;
    last_task_ = t;
    return exec_error_;
}

void MockRtcAsyncTaskExecutor::set_exec_error(srs_error_t err)
{
    exec_error_ = err;
}

void MockRtcAsyncTaskExecutor::reset()
{
    exec_error_ = srs_success;
    exec_count_ = 0;
    last_task_ = NULL;
}

// Mock RTC packet sender implementation
MockRtcPacketSender::MockRtcPacketSender()
{
    send_packet_error_ = srs_success;
    send_packet_count_ = 0;
    last_sent_packet_ = NULL;
}

MockRtcPacketSender::~MockRtcPacketSender()
{
}

srs_error_t MockRtcPacketSender::do_send_packet(SrsRtpPacket *pkt)
{
    send_packet_count_++;
    last_sent_packet_ = pkt;
    return send_packet_error_;
}

void MockRtcPacketSender::set_send_packet_error(srs_error_t err)
{
    send_packet_error_ = err;
}

void MockRtcPacketSender::reset()
{
    send_packet_error_ = srs_success;
    send_packet_count_ = 0;
    last_sent_packet_ = NULL;
}

// Mock RTC send track implementation
MockRtcSendTrack::MockRtcSendTrack(ISrsRtcPacketSender *sender, SrsRtcTrackDescription *track_desc, bool is_audio)
    : SrsRtcSendTrack(sender, track_desc, is_audio)
{
    on_rtp_error_ = srs_success;
    on_nack_error_ = srs_success;
    on_rtp_count_ = 0;
    on_nack_count_ = 0;
    last_rtp_packet_ = NULL;
    last_nack_packet_ = NULL;
    nack_set_to_null_ = false;
}

MockRtcSendTrack::~MockRtcSendTrack()
{
}

srs_error_t MockRtcSendTrack::on_rtp(SrsRtpPacket *pkt)
{
    on_rtp_count_++;
    last_rtp_packet_ = pkt;
    return on_rtp_error_;
}

srs_error_t MockRtcSendTrack::on_rtcp(SrsRtpPacket *pkt)
{
    return srs_success;
}

srs_error_t MockRtcSendTrack::on_nack(SrsRtpPacket **ppkt)
{
    on_nack_count_++;
    last_nack_packet_ = ppkt;
    if (nack_set_to_null_ && ppkt && *ppkt) {
        *ppkt = NULL;
    }
    return on_nack_error_;
}

void MockRtcSendTrack::set_on_rtp_error(srs_error_t err)
{
    on_rtp_error_ = err;
}

void MockRtcSendTrack::set_on_nack_error(srs_error_t err)
{
    on_nack_error_ = err;
}

void MockRtcSendTrack::set_nack_set_to_null(bool v)
{
    nack_set_to_null_ = v;
}

void MockRtcSendTrack::reset()
{
    on_rtp_error_ = srs_success;
    on_nack_error_ = srs_success;
    on_rtp_count_ = 0;
    on_nack_count_ = 0;
    last_rtp_packet_ = NULL;
    last_nack_packet_ = NULL;
    nack_set_to_null_ = false;
}

VOID TEST(RtcPlayStreamTest, SendPacketBasic)
{
    srs_error_t err;

    // Create mock objects
    MockAppConfig mock_config;
    MockRtcSourceManager mock_rtc_sources;
    MockRtcStatistic mock_stat;
    MockRtcAsyncCallRequest mock_request("test.vhost", "live", "stream1");
    MockRtcAsyncTaskExecutor mock_async_executor;
    MockExpire mock_expire;
    MockRtcPacketSender mock_packet_sender;

    // Create RTC play stream with mock interfaces
    SrsContextId cid;
    cid.set_value("test-send-packet-cid");
    SrsUniquePtr<SrsRtcPlayStream> play_stream(new SrsRtcPlayStream(&mock_async_executor, &mock_expire, &mock_packet_sender, cid));

    // Mock the dependencies by setting the private members
    play_stream->config_ = &mock_config;
    play_stream->rtc_sources_ = &mock_rtc_sources;
    play_stream->stat_ = &mock_stat;

    // Create track descriptions for audio and video
    SrsRtcTrackDescription *audio_desc = new SrsRtcTrackDescription();
    audio_desc->type_ = "audio";
    audio_desc->media_ = new SrsAudioPayload(111, "opus", 48000, 2);
    audio_desc->ssrc_ = 12345;

    SrsRtcTrackDescription *video_desc = new SrsRtcTrackDescription();
    video_desc->type_ = "video";
    video_desc->media_ = new SrsVideoPayload(97, "H264", 90000);
    video_desc->ssrc_ = 67890;

    // Create mock send tracks
    MockRtcSendTrack *mock_audio_track = new MockRtcSendTrack(&mock_packet_sender, audio_desc, true);
    MockRtcSendTrack *mock_video_track = new MockRtcSendTrack(&mock_packet_sender, video_desc, false);

    // Add tracks to play stream's collections
    play_stream->audio_tracks_[12345] = (SrsRtcAudioSendTrack *)mock_audio_track;
    play_stream->video_tracks_[67890] = (SrsRtcVideoSendTrack *)mock_video_track;

    // Test 1: Send audio packet
    SrsRtpPacket *audio_pkt = new SrsRtpPacket();
    audio_pkt->header_.set_ssrc(12345);
    audio_pkt->header_.set_sequence(100);
    audio_pkt->header_.set_payload_type(111);
    audio_pkt->frame_type_ = SrsFrameTypeAudio; // Set frame type for is_audio() check

    HELPER_EXPECT_SUCCESS(play_stream->send_packet(audio_pkt));

    // Verify audio track was called
    EXPECT_EQ(1, mock_audio_track->on_rtp_count_);
    EXPECT_EQ(audio_pkt, mock_audio_track->last_rtp_packet_);
    EXPECT_EQ(0, mock_video_track->on_rtp_count_);

    // Test 2: Send video packet
    SrsRtpPacket *video_pkt = new SrsRtpPacket();
    video_pkt->header_.set_ssrc(67890);
    video_pkt->header_.set_sequence(200);
    video_pkt->header_.set_payload_type(97);
    video_pkt->frame_type_ = SrsFrameTypeVideo; // Set frame type for is_audio() check

    HELPER_EXPECT_SUCCESS(play_stream->send_packet(video_pkt));

    // Verify video track was called
    EXPECT_EQ(1, mock_video_track->on_rtp_count_);
    EXPECT_EQ(video_pkt, mock_video_track->last_rtp_packet_);
    EXPECT_EQ(1, mock_audio_track->on_rtp_count_); // Should remain 1

    // Test 3: Test cache functionality - send same audio packet again
    SrsRtpPacket *audio_pkt2 = new SrsRtpPacket();
    audio_pkt2->header_.set_ssrc(12345);
    audio_pkt2->header_.set_sequence(101);
    audio_pkt2->header_.set_payload_type(111);
    audio_pkt2->frame_type_ = SrsFrameTypeAudio;

    HELPER_EXPECT_SUCCESS(play_stream->send_packet(audio_pkt2));

    // Verify cache was used (track should be called again)
    EXPECT_EQ(2, mock_audio_track->on_rtp_count_);
    EXPECT_EQ(audio_pkt2, mock_audio_track->last_rtp_packet_);

    // Test 4: Test unknown SSRC (should be ignored)
    SrsRtpPacket *unknown_pkt = new SrsRtpPacket();
    unknown_pkt->header_.set_ssrc(99999);
    unknown_pkt->header_.set_sequence(300);
    unknown_pkt->header_.set_payload_type(96);
    unknown_pkt->frame_type_ = SrsFrameTypeVideo;

    HELPER_EXPECT_SUCCESS(play_stream->send_packet(unknown_pkt));

    // Verify no tracks were called for unknown SSRC
    EXPECT_EQ(2, mock_audio_track->on_rtp_count_); // Should remain 2
    EXPECT_EQ(1, mock_video_track->on_rtp_count_); // Should remain 1

    // Clear the track references from play_stream before cleanup to avoid double-free
    play_stream->audio_tracks_.clear();
    play_stream->video_tracks_.clear();

    // Clean up
    srs_freep(audio_pkt);
    srs_freep(video_pkt);
    srs_freep(audio_pkt2);

    srs_freep(unknown_pkt);
    srs_freep(mock_audio_track);
    srs_freep(mock_video_track);
    srs_freep(audio_desc);
    srs_freep(video_desc);
}

// Note: NACK functionality test would require more complex setup
// including proper track initialization and NACK buffer management.
// The basic send_packet functionality is covered by SendPacketBasic test.

// Mock RTCP classes implementations
MockRtcpCommon::MockRtcpCommon(uint8_t type)
{
    mock_type_ = type;
}

MockRtcpCommon::~MockRtcpCommon()
{
}

uint8_t MockRtcpCommon::type() const
{
    return mock_type_;
}

MockRtcpRR::MockRtcpRR(uint32_t sender_ssrc) : SrsRtcpRR(sender_ssrc)
{
}

MockRtcpRR::~MockRtcpRR()
{
}

MockRtcpNack::MockRtcpNack(uint32_t sender_ssrc) : SrsRtcpNack(sender_ssrc)
{
}

MockRtcpNack::~MockRtcpNack()
{
}

MockRtcpFbCommon::MockRtcpFbCommon(uint8_t rc)
{
    mock_rc_ = rc;
}

MockRtcpFbCommon::~MockRtcpFbCommon()
{
}

uint8_t MockRtcpFbCommon::get_rc() const
{
    return mock_rc_;
}

MockRtcpXr::MockRtcpXr(uint32_t ssrc) : SrsRtcpXr(ssrc)
{
}

MockRtcpXr::~MockRtcpXr()
{
}

// Mock RTC send track with NACK response capability for testing on_rtcp_nack - implementation
MockRtcSendTrackForNack::MockRtcSendTrackForNack(ISrsRtcPacketSender *sender, SrsRtcTrackDescription *track_desc, bool is_audio, uint32_t ssrc)
    : SrsRtcSendTrack(sender, track_desc, is_audio)
{
    on_recv_nack_error_ = srs_success;
    on_recv_nack_count_ = 0;
    test_ssrc_ = ssrc;
    track_enabled_ = true;
}

MockRtcSendTrackForNack::~MockRtcSendTrackForNack()
{
}

srs_error_t MockRtcSendTrackForNack::on_rtp(SrsRtpPacket *pkt)
{
    return srs_success;
}

srs_error_t MockRtcSendTrackForNack::on_rtcp(SrsRtpPacket *pkt)
{
    return srs_success;
}

srs_error_t MockRtcSendTrackForNack::on_recv_nack(const std::vector<uint16_t> &lost_seqs)
{
    on_recv_nack_count_++;
    last_lost_seqs_ = lost_seqs;
    return on_recv_nack_error_;
}

bool MockRtcSendTrackForNack::has_ssrc(uint32_t ssrc)
{
    return ssrc == test_ssrc_;
}

bool MockRtcSendTrackForNack::get_track_status()
{
    return track_desc_->is_active_;
}

void MockRtcSendTrackForNack::set_track_enabled(bool enabled)
{
    track_desc_->is_active_ = enabled;
}

void MockRtcSendTrackForNack::set_on_recv_nack_error(srs_error_t err)
{
    on_recv_nack_error_ = err;
}

void MockRtcSendTrackForNack::reset()
{
    on_recv_nack_error_ = srs_success;
    on_recv_nack_count_ = 0;
    last_lost_seqs_.clear();
}

VOID TEST(RtcPlayStreamTest, OnRtcpDispatch)
{
    srs_error_t err;

    // Create mock objects
    MockAppConfig mock_config;
    MockRtcSourceManager mock_source_manager;
    MockRtcStatistic mock_stat;
    MockRtcAsyncTaskExecutor mock_executor;
    MockRtcPacketSender mock_sender;
    MockExpire mock_expire;

    // Set up mock config
    mock_config.set_http_hooks_enabled(false);

    // Create a mock request
    MockRtcAsyncCallRequest mock_request("__defaultVhost__", "live", "test");

    // Create SrsRtcPlayStream
    SrsUniquePtr<SrsRtcPlayStream> play_stream(new SrsRtcPlayStream(&mock_executor, &mock_expire, &mock_sender, _srs_context->get_id()));

    // Test case 1: RTCP RR packet
    if (true) {
        MockRtcpRR rr_packet(0x12345678);
        HELPER_EXPECT_SUCCESS(play_stream->on_rtcp(&rr_packet));
    }

    // Test case 2: RTCP NACK packet (rtpfb type)
    if (true) {
        MockRtcpNack nack_packet(0x87654321);
        HELPER_EXPECT_SUCCESS(play_stream->on_rtcp(&nack_packet));
    }

    // Test case 3: RTCP PS feedback packet (psfb type)
    if (true) {
        MockRtcpFbCommon psfb_packet(kPLI); // PLI feedback
        HELPER_EXPECT_SUCCESS(play_stream->on_rtcp(&psfb_packet));
    }

    // Test case 4: RTCP XR packet
    if (true) {
        MockRtcpXr xr_packet(0xABCDEF00);
        HELPER_EXPECT_SUCCESS(play_stream->on_rtcp(&xr_packet));
    }

    // Test case 5: RTCP BYE packet (should return success)
    if (true) {
        MockRtcpCommon bye_packet(SrsRtcpType_bye);
        HELPER_EXPECT_SUCCESS(play_stream->on_rtcp(&bye_packet));
    }

    // Test case 6: Unknown RTCP type (should return error)
    if (true) {
        MockRtcpCommon unknown_packet(255); // Invalid type
        HELPER_EXPECT_FAILED(play_stream->on_rtcp(&unknown_packet));
    }
}

VOID TEST(RtcPlayStreamTest, OnRtcpNack)
{
    srs_error_t err;

    // Create mock objects
    MockAppConfig mock_config;
    MockRtcSourceManager mock_source_manager;
    MockRtcStatistic mock_stat;
    MockRtcAsyncTaskExecutor mock_executor;
    MockRtcPacketSender mock_sender;
    MockExpire mock_expire;

    // Set up mock config
    mock_config.set_http_hooks_enabled(false);

    // Create SrsRtcPlayStream
    SrsUniquePtr<SrsRtcPlayStream> play_stream(new SrsRtcPlayStream(&mock_executor, &mock_expire, &mock_sender, _srs_context->get_id()));

    // Test case 1: NACK disabled - should succeed but not process NACK
    if (true) {
        // Disable NACK in play stream (simulate nack_enabled_ = false)
        play_stream->nack_enabled_ = false;

        MockRtcpNack nack_packet(0x12345678);
        nack_packet.set_media_ssrc(0x87654321);
        nack_packet.add_lost_sn(100);
        nack_packet.add_lost_sn(102);
        nack_packet.add_lost_sn(105);

        HELPER_EXPECT_SUCCESS(play_stream->on_rtcp_nack(&nack_packet));
    }

    // Test case 2: NACK enabled but no matching track - should return error
    if (true) {
        play_stream->nack_enabled_ = true;

        MockRtcpNack nack_packet(0x12345678);
        nack_packet.set_media_ssrc(0x99999999); // SSRC that doesn't match any track
        nack_packet.add_lost_sn(200);

        HELPER_EXPECT_FAILED(play_stream->on_rtcp_nack(&nack_packet));
    }

    // Test case 3: NACK enabled with matching audio track - should succeed
    if (true) {
        play_stream->nack_enabled_ = true;

        // Create track descriptions
        SrsRtcTrackDescription *audio_desc = new SrsRtcTrackDescription();
        audio_desc->type_ = "audio";
        audio_desc->ssrc_ = 0x11111111;
        audio_desc->is_active_ = true;

        // Create mock audio track
        MockRtcSendTrackForNack *mock_audio_track = new MockRtcSendTrackForNack(&mock_sender, audio_desc, true, 0x11111111);
        mock_audio_track->set_track_enabled(true);

        // Add track to play stream
        play_stream->audio_tracks_[0x11111111] = (SrsRtcAudioSendTrack *)mock_audio_track;

        // Create NACK packet for audio track
        MockRtcpNack nack_packet(0x12345678);
        nack_packet.set_media_ssrc(0x11111111);
        nack_packet.add_lost_sn(300);
        nack_packet.add_lost_sn(301);

        HELPER_EXPECT_SUCCESS(play_stream->on_rtcp_nack(&nack_packet));

        // Verify the track received the NACK
        EXPECT_EQ(1, mock_audio_track->on_recv_nack_count_);
        EXPECT_EQ(2, mock_audio_track->last_lost_seqs_.size());
        EXPECT_EQ(300, mock_audio_track->last_lost_seqs_[0]);
        EXPECT_EQ(301, mock_audio_track->last_lost_seqs_[1]);

        // Clear track from play stream before freeing
        play_stream->audio_tracks_.clear();
        srs_freep(mock_audio_track);
        srs_freep(audio_desc);
    }

    // Test case 4: NACK enabled with matching video track - should succeed
    if (true) {
        play_stream->nack_enabled_ = true;

        // Create track descriptions
        SrsRtcTrackDescription *video_desc = new SrsRtcTrackDescription();
        video_desc->type_ = "video";
        video_desc->ssrc_ = 0x22222222;
        video_desc->is_active_ = true;

        // Create mock video track
        MockRtcSendTrackForNack *mock_video_track = new MockRtcSendTrackForNack(&mock_sender, video_desc, false, 0x22222222);
        mock_video_track->set_track_enabled(true);

        // Add track to play stream
        play_stream->video_tracks_[0x22222222] = (SrsRtcVideoSendTrack *)mock_video_track;

        // Create NACK packet for video track
        MockRtcpNack nack_packet(0x12345678);
        nack_packet.set_media_ssrc(0x22222222);
        nack_packet.add_lost_sn(400);
        nack_packet.add_lost_sn(402);
        nack_packet.add_lost_sn(405);

        HELPER_EXPECT_SUCCESS(play_stream->on_rtcp_nack(&nack_packet));

        // Verify the track received the NACK
        EXPECT_EQ(1, mock_video_track->on_recv_nack_count_);
        EXPECT_EQ(3, mock_video_track->last_lost_seqs_.size());
        EXPECT_EQ(400, mock_video_track->last_lost_seqs_[0]);
        EXPECT_EQ(402, mock_video_track->last_lost_seqs_[1]);
        EXPECT_EQ(405, mock_video_track->last_lost_seqs_[2]);

        // Clear track from play stream before freeing
        play_stream->video_tracks_.clear();
        srs_freep(mock_video_track);
        srs_freep(video_desc);
    }

    // Test case 5: NACK with disabled track - should not find track
    if (true) {
        play_stream->nack_enabled_ = true;

        // Create track descriptions
        SrsRtcTrackDescription *audio_desc = new SrsRtcTrackDescription();
        audio_desc->type_ = "audio";
        audio_desc->ssrc_ = 0x33333333;
        audio_desc->is_active_ = true;

        // Create mock audio track but disable it
        MockRtcSendTrackForNack *mock_audio_track = new MockRtcSendTrackForNack(&mock_sender, audio_desc, true, 0x33333333);
        mock_audio_track->set_track_enabled(false); // Disabled track

        // Add track to play stream
        play_stream->audio_tracks_[0x33333333] = (SrsRtcAudioSendTrack *)mock_audio_track;

        // Create NACK packet for disabled track
        MockRtcpNack nack_packet(0x12345678);
        nack_packet.set_media_ssrc(0x33333333);
        nack_packet.add_lost_sn(500);

        HELPER_EXPECT_FAILED(play_stream->on_rtcp_nack(&nack_packet));

        // Verify the track did not receive the NACK
        EXPECT_EQ(0, mock_audio_track->on_recv_nack_count_);

        // Clear track from play stream before freeing
        play_stream->audio_tracks_.clear();
        srs_freep(mock_audio_track);
        srs_freep(audio_desc);
    }

    // Test case 6: NACK with track error - should propagate error
    if (true) {
        play_stream->nack_enabled_ = true;

        // Create track descriptions
        SrsRtcTrackDescription *video_desc = new SrsRtcTrackDescription();
        video_desc->type_ = "video";
        video_desc->ssrc_ = 0x44444444;
        video_desc->is_active_ = true;

        // Create mock video track that returns error
        MockRtcSendTrackForNack *mock_video_track = new MockRtcSendTrackForNack(&mock_sender, video_desc, false, 0x44444444);
        mock_video_track->set_track_enabled(true);
        mock_video_track->set_on_recv_nack_error(srs_error_new(ERROR_RTC_STUN, "mock nack error"));

        // Add track to play stream
        play_stream->video_tracks_[0x44444444] = (SrsRtcVideoSendTrack *)mock_video_track;

        // Create NACK packet for error track
        MockRtcpNack nack_packet(0x12345678);
        nack_packet.set_media_ssrc(0x44444444);
        nack_packet.add_lost_sn(600);

        HELPER_EXPECT_FAILED(play_stream->on_rtcp_nack(&nack_packet));

        // Verify the track received the NACK but returned error
        EXPECT_EQ(1, mock_video_track->on_recv_nack_count_);

        // Clear track from play stream before freeing
        play_stream->video_tracks_.clear();
        srs_freep(mock_video_track);
        srs_freep(video_desc);
    }
}

VOID TEST(RtcPlayStreamTest, DoRequestKeyframe)
{
    srs_error_t err;

    // Create mock objects
    MockAppConfig mock_config;
    MockRtcSourceManager mock_rtc_sources;
    MockRtcStatistic mock_stat;
    MockRtcAsyncCallRequest mock_request("test.vhost", "live", "stream1");
    MockRtcAsyncTaskExecutor mock_async_executor;
    MockExpire mock_expire;
    MockRtcPacketSender mock_packet_sender;

    // Create RTC play stream with mock interfaces
    SrsContextId cid;
    cid.set_value("test-do-request-keyframe-cid");
    SrsUniquePtr<SrsRtcPlayStream> play_stream(new SrsRtcPlayStream(&mock_async_executor, &mock_expire, &mock_packet_sender, cid));

    // Mock the dependencies by setting the private members
    play_stream->config_ = &mock_config;
    play_stream->rtc_sources_ = &mock_rtc_sources;
    play_stream->stat_ = &mock_stat;

    // Initialize the play stream
    std::map<uint32_t, SrsRtcTrackDescription *> sub_relations;
    HELPER_EXPECT_SUCCESS(play_stream->initialize(&mock_request, sub_relations));

    // Test 1: No publisher stream - should return success without doing anything
    {
        uint32_t test_ssrc = 12345;
        SrsContextId test_cid;
        test_cid.set_value("test-subscriber-cid");

        HELPER_EXPECT_SUCCESS(play_stream->do_request_keyframe(test_ssrc, test_cid));
    }

    // Test 2: With publisher stream - should call request_keyframe on publisher
    {
        // Create mock publisher stream
        MockRtcPublishStream *mock_publisher = new MockRtcPublishStream();
        SrsContextId publisher_cid;
        publisher_cid.set_value("test-publisher-cid");
        mock_publisher->set_context_id(publisher_cid);

        // Set the publisher stream in the source
        play_stream->source_->set_publish_stream(mock_publisher);

        uint32_t test_ssrc = 67890;
        SrsContextId test_cid;
        test_cid.set_value("test-subscriber-cid-2");

        // Call do_request_keyframe
        HELPER_EXPECT_SUCCESS(play_stream->do_request_keyframe(test_ssrc, test_cid));

        // Verify that request_keyframe was called on the publisher
        EXPECT_EQ(1, mock_publisher->request_keyframe_count_);
        EXPECT_EQ(test_ssrc, mock_publisher->last_keyframe_ssrc_);
        EXPECT_EQ(0, test_cid.compare(mock_publisher->last_keyframe_cid_));

        // Clean up - the source will handle the publisher cleanup
        play_stream->source_->set_publish_stream(NULL);
        srs_freep(mock_publisher);
    }
}

// Mock RTCP sender implementation
MockRtcRtcpSender::MockRtcRtcpSender()
{
    is_sender_started_ = true;
    is_sender_twcc_enabled_ = false;
    send_rtcp_rr_error_ = srs_success;
    send_rtcp_xr_rrtr_error_ = srs_success;
    send_periodic_twcc_error_ = srs_success;
    send_rtcp_rr_count_ = 0;
    send_rtcp_xr_rrtr_count_ = 0;
    send_periodic_twcc_count_ = 0;
}

MockRtcRtcpSender::~MockRtcRtcpSender()
{
    srs_freep(send_rtcp_rr_error_);
    srs_freep(send_rtcp_xr_rrtr_error_);
    srs_freep(send_periodic_twcc_error_);
}

bool MockRtcRtcpSender::is_sender_started()
{
    return is_sender_started_;
}

srs_error_t MockRtcRtcpSender::send_rtcp_rr()
{
    send_rtcp_rr_count_++;
    return srs_error_copy(send_rtcp_rr_error_);
}

srs_error_t MockRtcRtcpSender::send_rtcp_xr_rrtr()
{
    send_rtcp_xr_rrtr_count_++;
    return srs_error_copy(send_rtcp_xr_rrtr_error_);
}

bool MockRtcRtcpSender::is_sender_twcc_enabled()
{
    return is_sender_twcc_enabled_;
}

srs_error_t MockRtcRtcpSender::send_periodic_twcc()
{
    send_periodic_twcc_count_++;
    return srs_error_copy(send_periodic_twcc_error_);
}

void MockRtcRtcpSender::set_sender_started(bool started)
{
    is_sender_started_ = started;
}

void MockRtcRtcpSender::set_sender_twcc_enabled(bool enabled)
{
    is_sender_twcc_enabled_ = enabled;
}

void MockRtcRtcpSender::set_send_rtcp_rr_error(srs_error_t err)
{
    srs_freep(send_rtcp_rr_error_);
    send_rtcp_rr_error_ = srs_error_copy(err);
}

void MockRtcRtcpSender::set_send_rtcp_xr_rrtr_error(srs_error_t err)
{
    srs_freep(send_rtcp_xr_rrtr_error_);
    send_rtcp_xr_rrtr_error_ = srs_error_copy(err);
}

void MockRtcRtcpSender::set_send_periodic_twcc_error(srs_error_t err)
{
    srs_freep(send_periodic_twcc_error_);
    send_periodic_twcc_error_ = srs_error_copy(err);
}

void MockRtcRtcpSender::reset()
{
    is_sender_started_ = true;
    is_sender_twcc_enabled_ = false;
    srs_freep(send_rtcp_rr_error_);
    srs_freep(send_rtcp_xr_rrtr_error_);
    srs_freep(send_periodic_twcc_error_);
    send_rtcp_rr_error_ = srs_success;
    send_rtcp_xr_rrtr_error_ = srs_success;
    send_periodic_twcc_error_ = srs_success;
    send_rtcp_rr_count_ = 0;
    send_rtcp_xr_rrtr_count_ = 0;
    send_periodic_twcc_count_ = 0;
}

// Mock RTC packet receiver implementation
MockRtcPacketReceiver::MockRtcPacketReceiver()
{
    send_rtcp_rr_error_ = srs_success;
    send_rtcp_xr_rrtr_error_ = srs_success;
    send_rtcp_error_ = srs_success;
    send_rtcp_fb_pli_error_ = srs_success;
    send_rtcp_rr_count_ = 0;
    send_rtcp_xr_rrtr_count_ = 0;
    send_rtcp_count_ = 0;
    send_rtcp_fb_pli_count_ = 0;
    check_send_nacks_count_ = 0;
}

MockRtcPacketReceiver::~MockRtcPacketReceiver()
{
}

srs_error_t MockRtcPacketReceiver::send_rtcp_rr(uint32_t ssrc, SrsRtpRingBuffer *rtp_queue, const uint64_t &last_send_systime, const SrsNtp &last_send_ntp)
{
    send_rtcp_rr_count_++;
    return send_rtcp_rr_error_;
}

srs_error_t MockRtcPacketReceiver::send_rtcp_xr_rrtr(uint32_t ssrc)
{
    send_rtcp_xr_rrtr_count_++;
    return send_rtcp_xr_rrtr_error_;
}

void MockRtcPacketReceiver::check_send_nacks(SrsRtpNackForReceiver *nack, uint32_t ssrc, uint32_t &sent_nacks, uint32_t &timeout_nacks)
{
    check_send_nacks_count_++;
    sent_nacks = 0;
    timeout_nacks = 0;
}

srs_error_t MockRtcPacketReceiver::send_rtcp(char *data, int nb_data)
{
    send_rtcp_count_++;
    return send_rtcp_error_;
}

srs_error_t MockRtcPacketReceiver::send_rtcp_fb_pli(uint32_t ssrc, const SrsContextId &cid_of_subscriber)
{
    send_rtcp_fb_pli_count_++;
    return send_rtcp_fb_pli_error_;
}

void MockRtcPacketReceiver::set_send_rtcp_rr_error(srs_error_t err)
{
    send_rtcp_rr_error_ = err;
}

void MockRtcPacketReceiver::set_send_rtcp_xr_rrtr_error(srs_error_t err)
{
    send_rtcp_xr_rrtr_error_ = err;
}

void MockRtcPacketReceiver::set_send_rtcp_error(srs_error_t err)
{
    send_rtcp_error_ = err;
}

void MockRtcPacketReceiver::set_send_rtcp_fb_pli_error(srs_error_t err)
{
    send_rtcp_fb_pli_error_ = err;
}

void MockRtcPacketReceiver::reset()
{
    send_rtcp_rr_error_ = srs_success;
    send_rtcp_xr_rrtr_error_ = srs_success;
    send_rtcp_error_ = srs_success;
    send_rtcp_fb_pli_error_ = srs_success;
    send_rtcp_rr_count_ = 0;
    send_rtcp_xr_rrtr_count_ = 0;
    send_rtcp_count_ = 0;
    send_rtcp_fb_pli_count_ = 0;
    check_send_nacks_count_ = 0;
}

VOID TEST(SrsRtcPublishRtcpTimerTest, OnTimer)
{
    srs_error_t err;

    // Create mock RTCP sender
    MockRtcRtcpSender *mock_sender = new MockRtcRtcpSender();

    // Create timer with mock sender
    SrsUniquePtr<SrsRtcPublishRtcpTimer> timer(new SrsRtcPublishRtcpTimer(mock_sender));

    // Test 1: Normal operation - sender started, both RTCP calls succeed
    {
        mock_sender->reset();
        mock_sender->set_sender_started(true);

        HELPER_EXPECT_SUCCESS(timer->on_timer(100 * SRS_UTIME_MILLISECONDS));

        // Verify both RTCP methods were called
        EXPECT_EQ(1, mock_sender->send_rtcp_rr_count_);
        EXPECT_EQ(1, mock_sender->send_rtcp_xr_rrtr_count_);
    }

    // Test 2: Sender not started - should return success but not call RTCP methods
    {
        mock_sender->reset();
        mock_sender->set_sender_started(false);

        HELPER_EXPECT_SUCCESS(timer->on_timer(100 * SRS_UTIME_MILLISECONDS));

        // Verify RTCP methods were not called
        EXPECT_EQ(0, mock_sender->send_rtcp_rr_count_);
        EXPECT_EQ(0, mock_sender->send_rtcp_xr_rrtr_count_);
    }

    // Test 3: send_rtcp_rr fails - should continue and call send_rtcp_xr_rrtr
    {
        mock_sender->reset();
        mock_sender->set_sender_started(true);
        mock_sender->set_send_rtcp_rr_error(srs_error_new(ERROR_RTC_RTP_MUXER, "mock rr error"));

        HELPER_EXPECT_SUCCESS(timer->on_timer(100 * SRS_UTIME_MILLISECONDS));

        // Verify both methods were called despite RR error
        EXPECT_EQ(1, mock_sender->send_rtcp_rr_count_);
        EXPECT_EQ(1, mock_sender->send_rtcp_xr_rrtr_count_);
    }

    // Test 4: send_rtcp_xr_rrtr fails - should still return success
    {
        mock_sender->reset();
        mock_sender->set_sender_started(true);
        mock_sender->set_send_rtcp_xr_rrtr_error(srs_error_new(ERROR_RTC_RTP_MUXER, "mock xr error"));

        HELPER_EXPECT_SUCCESS(timer->on_timer(100 * SRS_UTIME_MILLISECONDS));

        // Verify both methods were called
        EXPECT_EQ(1, mock_sender->send_rtcp_rr_count_);
        EXPECT_EQ(1, mock_sender->send_rtcp_xr_rrtr_count_);
    }

    // Test 5: Both RTCP methods fail - should still return success
    {
        mock_sender->reset();
        mock_sender->set_sender_started(true);
        mock_sender->set_send_rtcp_rr_error(srs_error_new(ERROR_RTC_RTP_MUXER, "mock rr error"));
        mock_sender->set_send_rtcp_xr_rrtr_error(srs_error_new(ERROR_RTC_RTP_MUXER, "mock xr error"));

        HELPER_EXPECT_SUCCESS(timer->on_timer(100 * SRS_UTIME_MILLISECONDS));

        // Verify both methods were called despite errors
        EXPECT_EQ(1, mock_sender->send_rtcp_rr_count_);
        EXPECT_EQ(1, mock_sender->send_rtcp_xr_rrtr_count_);
    }

    srs_freep(mock_sender);
}

VOID TEST(SrsRtcPublishTwccTimerTest, OnTimer)
{
    srs_error_t err;

    // Create mock RTCP sender
    MockRtcRtcpSender *mock_sender = new MockRtcRtcpSender();

    // Create timer with mock sender
    SrsUniquePtr<SrsRtcPublishTwccTimer> timer(new SrsRtcPublishTwccTimer(mock_sender));

    // Test 1: Normal operation - sender started, TWCC enabled, circuit breaker not critical
    if (true) {
        mock_sender->reset();
        mock_sender->set_sender_started(true);
        mock_sender->set_sender_twcc_enabled(true);

        // Mock circuit breaker to not be in critical state
        MockCircuitBreaker mock_circuit_breaker;
        mock_circuit_breaker.hybrid_critical_water_level_ = false;
        ISrsCircuitBreaker *original_circuit_breaker = _srs_circuit_breaker;
        _srs_circuit_breaker = &mock_circuit_breaker;

        HELPER_EXPECT_SUCCESS(timer->on_timer(100 * SRS_UTIME_MILLISECONDS));

        // Verify send_periodic_twcc was called
        EXPECT_EQ(1, mock_sender->send_periodic_twcc_count_);

        // Restore original circuit breaker
        _srs_circuit_breaker = original_circuit_breaker;
    }

    // Test 2: Sender not started - should return early without calling TWCC methods
    if (true) {
        mock_sender->reset();
        mock_sender->set_sender_started(false);
        mock_sender->set_sender_twcc_enabled(true);

        HELPER_EXPECT_SUCCESS(timer->on_timer(100 * SRS_UTIME_MILLISECONDS));

        // Verify no TWCC methods were called
        EXPECT_EQ(0, mock_sender->send_periodic_twcc_count_);
    }

    // Test 3: Sender started but TWCC disabled - should return early without calling send_periodic_twcc
    if (true) {
        mock_sender->reset();
        mock_sender->set_sender_started(true);
        mock_sender->set_sender_twcc_enabled(false);

        HELPER_EXPECT_SUCCESS(timer->on_timer(100 * SRS_UTIME_MILLISECONDS));

        // Verify send_periodic_twcc was not called
        EXPECT_EQ(0, mock_sender->send_periodic_twcc_count_);
    }

    // Test 4: Circuit breaker in critical state - should return early without calling send_periodic_twcc
    if (true) {
        // Mock circuit breaker to be in critical state - must be set BEFORE creating timer
        MockCircuitBreaker mock_circuit_breaker;
        mock_circuit_breaker.hybrid_critical_water_level_ = true;
        ISrsCircuitBreaker *original_circuit_breaker = _srs_circuit_breaker;
        _srs_circuit_breaker = &mock_circuit_breaker;

        // Create new timer with the mock circuit breaker
        SrsUniquePtr<SrsRtcPublishTwccTimer> timer_with_mock_cb(new SrsRtcPublishTwccTimer(mock_sender));

        mock_sender->reset();
        mock_sender->set_sender_started(true);
        mock_sender->set_sender_twcc_enabled(true);

        HELPER_EXPECT_SUCCESS(timer_with_mock_cb->on_timer(100 * SRS_UTIME_MILLISECONDS));

        // Verify send_periodic_twcc was not called due to circuit breaker
        EXPECT_EQ(0, mock_sender->send_periodic_twcc_count_);

        // Restore original circuit breaker
        _srs_circuit_breaker = original_circuit_breaker;
    }

    // Test 5: send_periodic_twcc returns error - should handle error gracefully and continue
    if (true) {
        mock_sender->reset();
        mock_sender->set_sender_started(true);
        mock_sender->set_sender_twcc_enabled(true);
        mock_sender->set_send_periodic_twcc_error(srs_error_new(ERROR_RTC_RTP_MUXER, "mock twcc error"));

        // Mock circuit breaker to not be in critical state
        MockCircuitBreaker mock_circuit_breaker;
        mock_circuit_breaker.hybrid_critical_water_level_ = false;
        ISrsCircuitBreaker *original_circuit_breaker = _srs_circuit_breaker;
        _srs_circuit_breaker = &mock_circuit_breaker;

        // Should still return success even when send_periodic_twcc fails
        HELPER_EXPECT_SUCCESS(timer->on_timer(100 * SRS_UTIME_MILLISECONDS));

        // Verify send_periodic_twcc was called despite error
        EXPECT_EQ(1, mock_sender->send_periodic_twcc_count_);

        // Restore original circuit breaker
        _srs_circuit_breaker = original_circuit_breaker;
    }

    srs_freep(mock_sender);
}

// Unit tests for SrsRtcAsyncCallOnUnpublish::call()
VOID TEST(RtcAsyncCallOnUnpublishTest, CallWithHttpHooksDisabled)
{
    srs_error_t err;

    // Create mock objects
    MockAppConfig mock_config;
    MockHttpHooks mock_hooks;
    MockRtcAsyncCallRequest mock_request("test.vhost", "live", "stream1");

    // Disable HTTP hooks
    mock_config.set_http_hooks_enabled(false);

    // Create SrsRtcAsyncCallOnUnpublish with mocked dependencies
    SrsContextId cid;
    cid.set_value("test-context-id");

    SrsUniquePtr<SrsRtcAsyncCallOnUnpublish> async_call(new SrsRtcAsyncCallOnUnpublish(cid, &mock_request));

    // Replace the dependencies with mocks (private members are accessible due to #define private public)
    async_call->hooks_ = &mock_hooks;
    async_call->config_ = &mock_config;

    // Call should succeed but not invoke hooks since they're disabled
    HELPER_EXPECT_SUCCESS(async_call->call());

    // Verify no hooks were called
    EXPECT_EQ(0, mock_hooks.on_unpublish_count_);
    EXPECT_EQ(0, (int)mock_hooks.on_unpublish_calls_.size());
}

VOID TEST(RtcAsyncCallOnUnpublishTest, CallWithNoOnUnpublishConfiguration)
{
    srs_error_t err;

    // Create mock objects
    MockAppConfig mock_config;
    MockHttpHooks mock_hooks;
    MockRtcAsyncCallRequest mock_request("test.vhost", "live", "stream1");

    // Enable HTTP hooks but don't configure on_unpublish URLs
    mock_config.set_http_hooks_enabled(true);
    // on_unpublish_directive_ remains NULL

    // Create SrsRtcAsyncCallOnUnpublish with mocked dependencies
    SrsContextId cid;
    cid.set_value("test-context-id");

    SrsUniquePtr<SrsRtcAsyncCallOnUnpublish> async_call(new SrsRtcAsyncCallOnUnpublish(cid, &mock_request));

    // Replace the dependencies with mocks
    async_call->hooks_ = &mock_hooks;
    async_call->config_ = &mock_config;

    // Call should succeed but not invoke hooks since no URLs are configured
    HELPER_EXPECT_SUCCESS(async_call->call());

    // Verify no hooks were called
    EXPECT_EQ(0, mock_hooks.on_unpublish_count_);
    EXPECT_EQ(0, (int)mock_hooks.on_unpublish_calls_.size());
}

VOID TEST(RtcAsyncCallOnUnpublishTest, CallWithSingleOnUnpublishUrl)
{
    srs_error_t err;

    // Create mock objects
    MockAppConfig mock_config;
    MockHttpHooks mock_hooks;
    MockRtcAsyncCallRequest mock_request("test.vhost", "live", "stream1");

    // Enable HTTP hooks and configure single on_unpublish URL
    mock_config.set_http_hooks_enabled(true);
    std::vector<std::string> urls;
    urls.push_back("http://callback.server.com/on_unpublish");
    mock_config.set_on_unpublish_urls(urls);

    // Create SrsRtcAsyncCallOnUnpublish with mocked dependencies
    SrsContextId cid;
    cid.set_value("test-context-id");

    SrsUniquePtr<SrsRtcAsyncCallOnUnpublish> async_call(new SrsRtcAsyncCallOnUnpublish(cid, &mock_request));

    // Replace the dependencies with mocks
    async_call->hooks_ = &mock_hooks;
    async_call->config_ = &mock_config;

    // Call should succeed and invoke hooks
    HELPER_EXPECT_SUCCESS(async_call->call());

    // Verify hooks were called once
    EXPECT_EQ(1, mock_hooks.on_unpublish_count_);
    EXPECT_EQ(1, (int)mock_hooks.on_unpublish_calls_.size());
    EXPECT_STREQ("http://callback.server.com/on_unpublish", mock_hooks.on_unpublish_calls_[0].first.c_str());
    // Note: The request pointer will be different because SrsRtcAsyncCallOnUnpublish creates a copy
    EXPECT_TRUE(mock_hooks.on_unpublish_calls_[0].second != NULL);
}

VOID TEST(RtcAsyncCallOnUnpublishTest, CallWithMultipleOnUnpublishUrls)
{
    srs_error_t err;

    // Create mock objects
    MockAppConfig mock_config;
    MockHttpHooks mock_hooks;
    MockRtcAsyncCallRequest mock_request("test.vhost", "live", "stream1");

    // Enable HTTP hooks and configure multiple on_unpublish URLs
    mock_config.set_http_hooks_enabled(true);
    std::vector<std::string> urls;
    urls.push_back("http://callback1.server.com/on_unpublish");
    urls.push_back("http://callback2.server.com/on_unpublish");
    urls.push_back("http://callback3.server.com/on_unpublish");
    mock_config.set_on_unpublish_urls(urls);

    // Create SrsRtcAsyncCallOnUnpublish with mocked dependencies
    SrsContextId cid;
    cid.set_value("test-context-id");

    SrsUniquePtr<SrsRtcAsyncCallOnUnpublish> async_call(new SrsRtcAsyncCallOnUnpublish(cid, &mock_request));

    // Replace the dependencies with mocks
    async_call->hooks_ = &mock_hooks;
    async_call->config_ = &mock_config;

    // Call should succeed and invoke all hooks
    HELPER_EXPECT_SUCCESS(async_call->call());

    // Verify all hooks were called
    EXPECT_EQ(3, mock_hooks.on_unpublish_count_);
    EXPECT_EQ(3, (int)mock_hooks.on_unpublish_calls_.size());
    EXPECT_STREQ("http://callback1.server.com/on_unpublish", mock_hooks.on_unpublish_calls_[0].first.c_str());
    EXPECT_STREQ("http://callback2.server.com/on_unpublish", mock_hooks.on_unpublish_calls_[1].first.c_str());
    EXPECT_STREQ("http://callback3.server.com/on_unpublish", mock_hooks.on_unpublish_calls_[2].first.c_str());

    // All calls should use the same request object (but different from original due to copy)
    for (int i = 0; i < 3; i++) {
        // Note: The request pointer will be different because SrsRtcAsyncCallOnUnpublish creates a copy
        EXPECT_TRUE(mock_hooks.on_unpublish_calls_[i].second != NULL);
    }
}

VOID TEST(RtcAsyncCallOnUnpublishTest, CallWithContextSwitching)
{
    srs_error_t err;

    // Create mock objects
    MockAppConfig mock_config;
    MockHttpHooks mock_hooks;
    MockRtcAsyncCallRequest mock_request("test.vhost", "live", "stream1");

    // Enable HTTP hooks and configure on_unpublish URL
    mock_config.set_http_hooks_enabled(true);
    std::vector<std::string> urls;
    urls.push_back("http://callback.server.com/on_unpublish");
    mock_config.set_on_unpublish_urls(urls);

    // Set up context IDs
    SrsContextId original_cid;
    original_cid.set_value("original-context-id");
    SrsContextId new_cid;
    new_cid.set_value("new-context-id");

    // Create SrsRtcAsyncCallOnUnpublish with the new context ID
    SrsUniquePtr<SrsRtcAsyncCallOnUnpublish> async_call(new SrsRtcAsyncCallOnUnpublish(new_cid, &mock_request));

    // Replace the dependencies with mocks
    async_call->hooks_ = &mock_hooks;
    async_call->config_ = &mock_config;

    // Call should succeed and perform context switching
    HELPER_EXPECT_SUCCESS(async_call->call());

    // Verify hooks were called
    EXPECT_EQ(1, mock_hooks.on_unpublish_count_);
    EXPECT_EQ(1, (int)mock_hooks.on_unpublish_calls_.size());

    // Note: Context switching verification is not as straightforward for SrsRtcAsyncCallOnUnpublish
    // because it uses _srs_context directly instead of a member variable like SrsRtcAsyncCallOnStop
}

// Mock live source manager implementation
MockLiveSourceManager::MockLiveSourceManager()
{
    fetch_or_create_error_ = srs_success;
    fetch_or_create_count_ = 0;
    can_publish_ = true;

    // Create a mock live source
    mock_source_ = SrsSharedPtr<SrsLiveSource>(new MockLiveSource());
}

MockLiveSourceManager::~MockLiveSourceManager()
{
    srs_freep(fetch_or_create_error_);
}

srs_error_t MockLiveSourceManager::fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsLiveSource> &pps)
{
    fetch_or_create_count_++;
    if (fetch_or_create_error_ != srs_success) {
        return srs_error_copy(fetch_or_create_error_);
    }
    pps = mock_source_;
    return srs_success;
}

SrsSharedPtr<SrsLiveSource> MockLiveSourceManager::fetch(ISrsRequest *r)
{
    return mock_source_;
}

void MockLiveSourceManager::dispose()
{
    // Mock implementation - no-op for testing
}

srs_error_t MockLiveSourceManager::initialize()
{
    // Mock implementation - always succeeds
    return srs_success;
}

void MockLiveSourceManager::set_fetch_or_create_error(srs_error_t err)
{
    srs_freep(fetch_or_create_error_);
    fetch_or_create_error_ = srs_error_copy(err);
}

void MockLiveSourceManager::set_can_publish(bool can_publish)
{
    can_publish_ = can_publish;
    if (mock_source_.get()) {
        MockLiveSource *mock_live_source = dynamic_cast<MockLiveSource *>(mock_source_.get());
        if (mock_live_source) {
            mock_live_source->set_can_publish(can_publish);
        }
    }
}

void MockLiveSourceManager::reset()
{
    srs_freep(fetch_or_create_error_);
    fetch_or_create_error_ = srs_success;
    fetch_or_create_count_ = 0;
    can_publish_ = true;
}

// Mock live source implementation
MockLiveSource::MockLiveSource()
{
    can_publish_result_ = true;
}

MockLiveSource::~MockLiveSource()
{
}

bool MockLiveSource::can_publish(bool is_edge)
{
    return can_publish_result_;
}

void MockLiveSource::set_can_publish(bool can_publish)
{
    can_publish_result_ = can_publish;
}

srs_error_t MockLiveSource::on_publish()
{
    // Mock implementation - just return success
    return srs_success;
}

srs_error_t MockLiveSource::on_edge_start_publish()
{
    // Mock implementation - just return success
    return srs_success;
}

// Mock SRT source implementation
MockSrtSource::MockSrtSource()
{
    can_publish_result_ = true;
}

MockSrtSource::~MockSrtSource()
{
}

bool MockSrtSource::can_publish()
{
    return can_publish_result_;
}

void MockSrtSource::set_can_publish(bool can_publish)
{
    can_publish_result_ = can_publish;
}

// Mock SRT source manager implementation
MockSrtSourceManager::MockSrtSourceManager()
{
    initialize_error_ = srs_success;
    fetch_or_create_error_ = srs_success;
    initialize_count_ = 0;
    fetch_or_create_count_ = 0;
    can_publish_ = true;

    // Create a mock SRT source
    mock_source_ = SrsSharedPtr<SrsSrtSource>(new MockSrtSource());
}

MockSrtSourceManager::~MockSrtSourceManager()
{
    srs_freep(initialize_error_);
    srs_freep(fetch_or_create_error_);
}

srs_error_t MockSrtSourceManager::initialize()
{
    initialize_count_++;
    return srs_error_copy(initialize_error_);
}

srs_error_t MockSrtSourceManager::fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsSrtSource> &pps)
{
    fetch_or_create_count_++;
    if (fetch_or_create_error_ != srs_success) {
        return srs_error_copy(fetch_or_create_error_);
    }
    pps = mock_source_;
    return srs_success;
}

SrsSharedPtr<SrsSrtSource> MockSrtSourceManager::fetch(ISrsRequest *r)
{
    return mock_source_;
}

void MockSrtSourceManager::set_initialize_error(srs_error_t err)
{
    srs_freep(initialize_error_);
    initialize_error_ = srs_error_copy(err);
}

void MockSrtSourceManager::set_fetch_or_create_error(srs_error_t err)
{
    srs_freep(fetch_or_create_error_);
    fetch_or_create_error_ = srs_error_copy(err);
}

void MockSrtSourceManager::set_can_publish(bool can_publish)
{
    can_publish_ = can_publish;
    if (mock_source_.get()) {
        MockSrtSource *mock_srt_source = dynamic_cast<MockSrtSource *>(mock_source_.get());
        if (mock_srt_source) {
            mock_srt_source->set_can_publish(can_publish);
        }
    }
}

void MockSrtSourceManager::reset()
{
    srs_freep(initialize_error_);
    srs_freep(fetch_or_create_error_);
    initialize_error_ = srs_success;
    fetch_or_create_error_ = srs_success;
    initialize_count_ = 0;
    fetch_or_create_count_ = 0;
    can_publish_ = true;
}

VOID TEST(RtcPublishStreamTest, Initialize)
{
    srs_error_t err;

    // Create mock objects
    MockRtcStatistic mock_stat;
    MockAppConfig mock_config;
    MockRtcSourceManager mock_rtc_sources;
    MockLiveSourceManager mock_live_sources;
    MockSrtSourceManager mock_srt_sources;
    MockRtcPacketReceiver mock_receiver;
    MockRtcAsyncCallRequest mock_request("test.vhost", "live", "stream1");
    MockRtcAsyncTaskExecutor mock_exec;
    MockExpire mock_expire;

    // Create SrsRtcPublishStream with mock dependencies
    SrsContextId cid;
    cid.set_value("test-publish-stream-id");

    SrsUniquePtr<SrsRtcPublishStream> publish_stream(new SrsRtcPublishStream(&mock_exec, &mock_expire, &mock_receiver, cid));

    // Set mock dependencies
    publish_stream->stat_ = &mock_stat;
    publish_stream->config_ = &mock_config;
    publish_stream->rtc_sources_ = &mock_rtc_sources;
    publish_stream->live_sources_ = &mock_live_sources;
    publish_stream->srt_sources_ = &mock_srt_sources;

    // Create stream description with audio and video tracks
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());

    // Add audio track description
    stream_desc->audio_track_desc_ = new SrsRtcTrackDescription();
    stream_desc->audio_track_desc_->type_ = "audio";
    stream_desc->audio_track_desc_->id_ = "audio-track-1";
    stream_desc->audio_track_desc_->ssrc_ = 12345;

    // Add video track description with TWCC extension
    SrsRtcTrackDescription *video_track = new SrsRtcTrackDescription();
    video_track->type_ = "video";
    video_track->id_ = "video-track-1";
    video_track->ssrc_ = 67890;
    video_track->add_rtp_extension_desc(1, "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01");
    stream_desc->video_track_descs_.push_back(video_track);

    // Configure mock config
    mock_config.set_rtc_nack_enabled(true);
    mock_config.set_rtc_nack_no_copy(false);
    mock_config.set_rtc_drop_for_pt(0);
    mock_config.set_rtc_twcc_enabled(true);
    mock_config.set_srt_enabled(true);
    mock_config.set_rtc_to_rtmp(true);

    // Test successful initialization
    HELPER_EXPECT_SUCCESS(publish_stream->initialize(&mock_request, stream_desc.get()));

    // Verify mock calls
    EXPECT_EQ(1, mock_stat.on_client_count_);
    EXPECT_EQ(1, mock_rtc_sources.fetch_or_create_count_);
    EXPECT_EQ(1, mock_live_sources.fetch_or_create_count_);
    EXPECT_EQ(1, mock_srt_sources.fetch_or_create_count_);
}

VOID TEST(SrsRtcPublishStreamTest, OnTwccSuccess)
{
    srs_error_t err;

    // Create mock objects
    MockRtcAsyncTaskExecutor mock_exec;
    MockRtcExpire mock_expire;
    MockRtcPacketReceiver mock_receiver;
    SrsContextId cid;
    cid.set_value("test-twcc-stream-id");

    // Create SrsRtcPublishStream with mock dependencies
    SrsUniquePtr<SrsRtcPublishStream> publish_stream(new SrsRtcPublishStream(&mock_exec, &mock_expire, &mock_receiver, cid));

    // Test on_twcc with a typical sequence number
    uint16_t test_sn = 12345;
    HELPER_EXPECT_SUCCESS(publish_stream->on_twcc(test_sn));

    // Test on_twcc with different sequence numbers to verify it handles multiple packets
    HELPER_EXPECT_SUCCESS(publish_stream->on_twcc(12346));
    HELPER_EXPECT_SUCCESS(publish_stream->on_twcc(12347));

    // Test duplicate sequence number should fail
    HELPER_EXPECT_FAILED(publish_stream->on_twcc(12345));
}
