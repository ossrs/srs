/*
The MIT License (MIT)

Copyright (c) 2013-2020 Winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#include <srs_utest_rtc.hpp>

#include <srs_kernel_error.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_rtc_queue.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_kernel_codec.hpp>

#include <vector>
using namespace std;

extern SSL_CTX* srs_build_dtls_ctx(SrsDtlsVersion version);

class MockDtls
{
public:
    SSL_CTX* dtls_ctx;
    SSL* dtls;
    BIO* bio_in;
    BIO* bio_out;
    ISrsDtlsCallback* callback_;
    bool handshake_done_for_us;
    SrsDtlsRole role_;
    SrsDtlsVersion version_;
public:
    MockDtls(ISrsDtlsCallback* callback);
    virtual ~MockDtls();
    srs_error_t initialize(std::string role, std::string version);
    srs_error_t start_active_handshake();
    srs_error_t on_dtls(char* data, int nb_data);
    srs_error_t do_handshake();
};

MockDtls::MockDtls(ISrsDtlsCallback* callback)
{
    dtls_ctx = NULL;
    dtls = NULL;

    callback_ = callback;
    handshake_done_for_us = false;

    role_ = SrsDtlsRoleServer;
    version_ = SrsDtlsVersionAuto;
}

MockDtls::~MockDtls()
{
    if (dtls_ctx) {
        SSL_CTX_free(dtls_ctx);
        dtls_ctx = NULL;
    }

    if (dtls) {
        SSL_free(dtls);
        dtls = NULL;
    }
}

srs_error_t MockDtls::initialize(std::string role, std::string version)
{
    role_ = SrsDtlsRoleServer;
    if (role == "active") {
        role_ = SrsDtlsRoleClient;
    }

    if (version == "dtls1.0") {
        version_ = SrsDtlsVersion1_0;
    } else if (version == "dtls1.2") {
        version_ = SrsDtlsVersion1_2;
    } else {
        version_ = SrsDtlsVersionAuto;
    }

    dtls_ctx = srs_build_dtls_ctx(version_);
    dtls = SSL_new(dtls_ctx);
    srs_assert(dtls);

    if (role_ == SrsDtlsRoleClient) {
        SSL_set_connect_state(dtls);
        SSL_set_max_send_fragment(dtls, kRtpPacketSize);
    } else {
        SSL_set_accept_state(dtls);
    }

    bio_in = BIO_new(BIO_s_mem());
    srs_assert(bio_in);

    bio_out = BIO_new(BIO_s_mem());
    srs_assert(bio_out);

    SSL_set_bio(dtls, bio_in, bio_out);
    return srs_success;
}

srs_error_t MockDtls::start_active_handshake()
{
    if (role_ == SrsDtlsRoleClient) {
        return do_handshake();
    }
    return srs_success;
}

srs_error_t MockDtls::on_dtls(char* data, int nb_data)
{
    srs_error_t err = srs_success;

    srs_assert(BIO_reset(bio_in) == 1);
    srs_assert(BIO_reset(bio_out) == 1);
    srs_assert(BIO_write(bio_in, data, nb_data) > 0);

    if ((err = do_handshake()) != srs_success) {
        return srs_error_wrap(err, "do handshake");
    }

    while (BIO_ctrl_pending(bio_in) > 0) {
        char buf[8092];
        int nb = SSL_read(dtls, buf, sizeof(buf));
        if (nb <= 0) {
            continue;
        }

        if ((err = callback_->on_dtls_application_data(buf, nb)) != srs_success) {
            return srs_error_wrap(err, "on DTLS data, size=%u", nb);
        }
    }

    return err;
}

srs_error_t MockDtls::do_handshake()
{
    srs_error_t err = srs_success;

    int r0 = SSL_do_handshake(dtls);
    int r1 = SSL_get_error(dtls, r0);
    if (r0 < 0 && (r1 != SSL_ERROR_NONE && r1 != SSL_ERROR_WANT_READ && r1 != SSL_ERROR_WANT_WRITE)) {
        return srs_error_new(ERROR_RTC_DTLS, "handshake r0=%d, r1=%d", r0, r1);
    }
    if (r1 == SSL_ERROR_NONE) {
        handshake_done_for_us = true;
    }

    uint8_t* data = NULL;
    int size = BIO_get_mem_data(bio_out, &data);

    if (size > 0 && (err = callback_->write_dtls_data(data, size)) != srs_success) {
        return srs_error_wrap(err, "dtls send size=%u", size);
    }

    if (handshake_done_for_us) {
        return callback_->on_dtls_handshake_done();
    }

    return err;
}

class MockDtlsCallback : virtual public ISrsDtlsCallback, virtual public ISrsCoroutineHandler
{
public:
    SrsDtls* peer;
    MockDtls* peer2;
    SrsCoroutine* trd;
    srs_error_t r0;
    bool done;
    std::vector<SrsSample> samples;
public:
    int nn_client_hello_lost;
    int nn_server_hello_lost;
    int nn_certificate_lost;
    int nn_new_session_lost;
    int nn_change_cipher_lost;
public:
    // client -> server
    int nn_client_hello;
    // server -> client
    int nn_server_hello;
    // client -> server
    int nn_certificate;
    // server -> client
    int nn_new_session;
    int nn_change_cipher;
public:
    MockDtlsCallback();
    virtual ~MockDtlsCallback();
    virtual srs_error_t on_dtls_handshake_done();
    virtual srs_error_t on_dtls_application_data(const char* data, const int len);
    virtual srs_error_t write_dtls_data(void* data, int size);
    virtual srs_error_t cycle();
};

MockDtlsCallback::MockDtlsCallback()
{
    peer = NULL;
    peer2 = NULL;
    r0 = srs_success;
    done = false;
    trd = new SrsSTCoroutine("mock", this);
    srs_assert(trd->start() == srs_success);

    nn_client_hello_lost = 0;
    nn_server_hello_lost = 0;
    nn_certificate_lost = 0;
    nn_new_session_lost = 0;
    nn_change_cipher_lost = 0;

    nn_client_hello = 0;
    nn_server_hello = 0;
    nn_certificate = 0;
    nn_new_session = 0;
    nn_change_cipher = 0;
}

MockDtlsCallback::~MockDtlsCallback()
{
    srs_freep(trd);
    srs_freep(r0);
    for (vector<SrsSample>::iterator it = samples.begin(); it != samples.end(); ++it) {
        delete[] it->bytes;
    }
}

srs_error_t MockDtlsCallback::on_dtls_handshake_done()
{
    done = true;
    return srs_success;
}

srs_error_t MockDtlsCallback::on_dtls_application_data(const char* data, const int len)
{
    return srs_success;
}

srs_error_t MockDtlsCallback::write_dtls_data(void* data, int size)
{
    int nn_lost = 0;
    if (true) {
        uint8_t content_type = 0;
        if (size >= 1) {
            content_type = (uint8_t)((uint8_t*)data)[0];
        }

        uint8_t handshake_type = 0;
        if (size >= 14) {
            handshake_type = (uint8_t)((uint8_t*)data)[13];
        }

        if (content_type == 22) {
            if (handshake_type == 1) {
                nn_lost = nn_client_hello_lost--;
                nn_client_hello++;
            } else if (handshake_type == 2) {
                nn_lost = nn_server_hello_lost--;
                nn_server_hello++;
            } else if (handshake_type == 11) {
                nn_lost = nn_certificate_lost--;
                nn_certificate++;
            } else if (handshake_type == 4) {
                nn_lost = nn_new_session_lost--;
                nn_new_session++;
            }
        } else if (content_type == 20) {
            nn_lost = nn_change_cipher_lost--;
            nn_change_cipher++;
        }
    }

    // Simulate to drop packet.
    if (nn_lost > 0) {
        return srs_success;
    }

    // Send out it.
    char* cp = new char[size];
    memcpy(cp, data, size);

    samples.push_back(SrsSample((char*)cp, size));

    return srs_success;
}

srs_error_t MockDtlsCallback::cycle()
{
    srs_error_t err = srs_success;

    while (err == srs_success) {
        if ((err = trd->pull()) != srs_success) {
            break;
        }

        if (samples.empty()) {
            srs_usleep(0);
            continue;
        }

        SrsSample p = samples.at(0);
        samples.erase(samples.begin());

        if (peer) {
            err = peer->on_dtls((char*)p.bytes, p.size);
        } else if (peer2) {
            err = peer2->on_dtls((char*)p.bytes, p.size);
        }

        srs_freepa(p.bytes);
    }

    // Copy it for utest to check it.
    r0 = srs_error_copy(err);

    return err;
}

// Wait for mock io to done, try to switch to coroutine many times.
void mock_wait_dtls_io_done(int count = 100, int interval = 0)
{
    for (int i = 0; i < count; i++) {
        srs_usleep(interval * SRS_UTIME_MILLISECONDS);
    }
}

// To avoid the crash when peer or peer2 is freed before io.
class MockBridgeDtlsIO
{
private:
    MockDtlsCallback* io_;
public:
    MockBridgeDtlsIO(MockDtlsCallback* io, SrsDtls* peer, MockDtls* peer2) {
        io_ = io;
        io->peer = peer;
        io->peer2 = peer2;
    }
    virtual ~MockBridgeDtlsIO() {
        io_->peer = NULL;
        io_->peer2 = NULL;
    }
};

struct DTLSServerFlowCase
{
    int id;

    string ClientVersion;
    string ServerVersion;

    bool ClientDone;
    bool ServerDone;

    bool ClientError;
    bool ServerError;
};

std::ostream& operator<< (std::ostream& stream, const DTLSServerFlowCase& c)
{
    stream << "Case #" << c.id
        << ", client(" << c.ClientVersion << ",done=" << c.ClientDone << ",err=" << c.ClientError << ")"
        << ", server(" << c.ServerVersion << ",done=" << c.ServerDone << ",err=" << c.ServerError << ")";
    return stream;
}

VOID TEST(KernelRTCTest, DTLSClientARQTest)
{
    srs_error_t err = srs_success;

    // No ARQ, check the number of packets.
    if (true) {
        MockDtlsCallback cio; SrsDtls client(&cio);
        MockDtlsCallback sio; SrsDtls server(&sio);
        cio.peer = &server; sio.peer = &client;
        HELPER_EXPECT_SUCCESS(client.initialize("active", "dtls1.0"));
        HELPER_EXPECT_SUCCESS(server.initialize("passive", "dtls1.0"));

        HELPER_EXPECT_SUCCESS(client.start_active_handshake());
        mock_wait_dtls_io_done(30, 1);

        EXPECT_TRUE(sio.r0 == srs_success);
        EXPECT_TRUE(cio.r0 == srs_success);

        EXPECT_TRUE(cio.done);
        EXPECT_TRUE(sio.done);

        EXPECT_EQ(1, cio.nn_client_hello);
        EXPECT_EQ(1, sio.nn_server_hello);
        EXPECT_EQ(1, cio.nn_certificate);
        EXPECT_EQ(1, sio.nn_new_session);
        EXPECT_EQ(0, sio.nn_change_cipher);
    }

    // ClientHello lost, client retransmit the ClientHello.
    if (true) {
        MockDtlsCallback cio; SrsDtls client(&cio);
        MockDtlsCallback sio; SrsDtls server(&sio);
        MockBridgeDtlsIO b0(&cio, &server, NULL); MockBridgeDtlsIO b1(&sio, &client, NULL);
        HELPER_EXPECT_SUCCESS(client.initialize("active", "dtls1.0"));
        HELPER_EXPECT_SUCCESS(server.initialize("passive", "dtls1.0"));

        // Use very short interval for utest.
        dynamic_cast<SrsDtlsClientImpl*>(client.impl)->arq_first = 1 * SRS_UTIME_MILLISECONDS;
        dynamic_cast<SrsDtlsClientImpl*>(client.impl)->arq_interval = 1 * SRS_UTIME_MILLISECONDS;

        // Lost 2 packets, total packets should be 3.
        // Note that only one server hello.
        cio.nn_client_hello_lost = 2;

        HELPER_EXPECT_SUCCESS(client.start_active_handshake());
        mock_wait_dtls_io_done(10, 3);

        EXPECT_TRUE(sio.r0 == srs_success);
        EXPECT_TRUE(cio.r0 == srs_success);

        EXPECT_TRUE(cio.done);
        EXPECT_TRUE(sio.done);

        EXPECT_EQ(3, cio.nn_client_hello);
        EXPECT_EQ(1, sio.nn_server_hello);
        EXPECT_EQ(1, cio.nn_certificate);
        EXPECT_EQ(1, sio.nn_new_session);
        EXPECT_EQ(0, sio.nn_change_cipher);
    }

    // Certificate lost, client retransmit the Certificate.
    if (true) {
        MockDtlsCallback cio; SrsDtls client(&cio);
        MockDtlsCallback sio; SrsDtls server(&sio);
        MockBridgeDtlsIO b0(&cio, &server, NULL); MockBridgeDtlsIO b1(&sio, &client, NULL);
        HELPER_EXPECT_SUCCESS(client.initialize("active", "dtls1.0"));
        HELPER_EXPECT_SUCCESS(server.initialize("passive", "dtls1.0"));

        // Use very short interval for utest.
        dynamic_cast<SrsDtlsClientImpl*>(client.impl)->arq_first = 1 * SRS_UTIME_MILLISECONDS;
        dynamic_cast<SrsDtlsClientImpl*>(client.impl)->arq_interval = 1 * SRS_UTIME_MILLISECONDS;

        // Lost 2 packets, total packets should be 3.
        // Note that only one server NewSessionTicket.
        cio.nn_certificate_lost = 2;

        HELPER_EXPECT_SUCCESS(client.start_active_handshake());
        mock_wait_dtls_io_done(10, 3);

        EXPECT_TRUE(sio.r0 == srs_success);
        EXPECT_TRUE(cio.r0 == srs_success);

        EXPECT_TRUE(cio.done);
        EXPECT_TRUE(sio.done);

        EXPECT_EQ(1, cio.nn_client_hello);
        EXPECT_EQ(1, sio.nn_server_hello);
        EXPECT_EQ(3, cio.nn_certificate);
        EXPECT_EQ(1, sio.nn_new_session);
        EXPECT_EQ(0, sio.nn_change_cipher);
    }
}

VOID TEST(KernelRTCTest, DTLSServerARQTest)
{
    srs_error_t err = srs_success;

    // No ARQ, check the number of packets.
    if (true) {
        MockDtlsCallback cio; SrsDtls client(&cio);
        MockDtlsCallback sio; SrsDtls server(&sio);
        cio.peer = &server; sio.peer = &client;
        HELPER_EXPECT_SUCCESS(client.initialize("active", "dtls1.0"));
        HELPER_EXPECT_SUCCESS(server.initialize("passive", "dtls1.0"));

        HELPER_EXPECT_SUCCESS(client.start_active_handshake());
        mock_wait_dtls_io_done(30, 1);

        EXPECT_TRUE(sio.r0 == srs_success);
        EXPECT_TRUE(cio.r0 == srs_success);

        EXPECT_TRUE(cio.done);
        EXPECT_TRUE(sio.done);

        EXPECT_EQ(1, cio.nn_client_hello);
        EXPECT_EQ(1, sio.nn_server_hello);
        EXPECT_EQ(1, cio.nn_certificate);
        EXPECT_EQ(1, sio.nn_new_session);
        EXPECT_EQ(0, sio.nn_change_cipher);
    }

    // ServerHello lost, client retransmit the ClientHello.
    if (true) {
        MockDtlsCallback cio; SrsDtls client(&cio);
        MockDtlsCallback sio; SrsDtls server(&sio);
        MockBridgeDtlsIO b0(&cio, &server, NULL); MockBridgeDtlsIO b1(&sio, &client, NULL);
        HELPER_EXPECT_SUCCESS(client.initialize("active", "dtls1.0"));
        HELPER_EXPECT_SUCCESS(server.initialize("passive", "dtls1.0"));

        // Use very short interval for utest.
        dynamic_cast<SrsDtlsClientImpl*>(client.impl)->arq_first = 1 * SRS_UTIME_MILLISECONDS;
        dynamic_cast<SrsDtlsClientImpl*>(client.impl)->arq_interval = 1 * SRS_UTIME_MILLISECONDS;

        // Lost 2 packets, total packets should be 3.
        sio.nn_server_hello_lost = 2;

        HELPER_EXPECT_SUCCESS(client.start_active_handshake());
        mock_wait_dtls_io_done(10, 3);

        EXPECT_TRUE(sio.r0 == srs_success);
        EXPECT_TRUE(cio.r0 == srs_success);

        EXPECT_TRUE(cio.done);
        EXPECT_TRUE(sio.done);

        EXPECT_EQ(3, cio.nn_client_hello);
        EXPECT_EQ(3, sio.nn_server_hello);
        EXPECT_EQ(1, cio.nn_certificate);
        EXPECT_EQ(1, sio.nn_new_session);
        EXPECT_EQ(0, sio.nn_change_cipher);
    }

    // NewSessionTicket lost, client retransmit the Certificate.
    if (true) {
        MockDtlsCallback cio; SrsDtls client(&cio);
        MockDtlsCallback sio; SrsDtls server(&sio);
        MockBridgeDtlsIO b0(&cio, &server, NULL); MockBridgeDtlsIO b1(&sio, &client, NULL);
        HELPER_EXPECT_SUCCESS(client.initialize("active", "dtls1.0"));
        HELPER_EXPECT_SUCCESS(server.initialize("passive", "dtls1.0"));

        // Use very short interval for utest.
        dynamic_cast<SrsDtlsClientImpl*>(client.impl)->arq_first = 1 * SRS_UTIME_MILLISECONDS;
        dynamic_cast<SrsDtlsClientImpl*>(client.impl)->arq_interval = 1 * SRS_UTIME_MILLISECONDS;

        // Lost 2 packets, total packets should be 3.
        sio.nn_new_session_lost = 2;

        HELPER_EXPECT_SUCCESS(client.start_active_handshake());
        mock_wait_dtls_io_done(10, 3);

        EXPECT_TRUE(sio.r0 == srs_success);
        EXPECT_TRUE(cio.r0 == srs_success);

        EXPECT_TRUE(cio.done);
        EXPECT_TRUE(sio.done);

        EXPECT_EQ(1, cio.nn_client_hello);
        EXPECT_EQ(1, sio.nn_server_hello);
        EXPECT_EQ(3, cio.nn_certificate);
        EXPECT_EQ(3, sio.nn_new_session);
        EXPECT_EQ(0, sio.nn_change_cipher);
    }
}

VOID TEST(KernelRTCTest, DTLSClientFlowTest)
{
    srs_error_t err = srs_success;

    DTLSServerFlowCase cases[] = {
        // OK, Client, Server: DTLS v1.0
        {0, "dtls1.0", "dtls1.0", true, true, false, false},
        // OK, Client, Server: DTLS v1.2
        {1, "dtls1.2", "dtls1.2", true, true, false, false},
        // OK, Client: DTLS v1.0, Server: DTLS auto(v1.0 or v1.2).
        {2, "dtls1.0", "auto", true, true, false, false},
        // OK, Client: DTLS v1.2, Server: DTLS auto(v1.0 or v1.2).
        {3, "dtls1.2", "auto", true, true, false, false},
        // OK, Client: DTLS auto(v1.0 or v1.2), Server: DTLS v1.0
        {4, "auto", "dtls1.0", true, true, false, false},
        // OK, Client: DTLS auto(v1.0 or v1.2), Server: DTLS v1.0
        {5, "auto", "dtls1.2", true, true, false, false},
        // Fail, Client: DTLS v1.0, Server: DTLS v1.2
        {6, "dtls1.0", "dtls1.2", false, false, false, true},
        // Fail, Client: DTLS v1.2, Server: DTLS v1.0
        {7, "dtls1.2", "dtls1.0", false, false, true, false},
    };

    for (int i = 0; i < (int)(sizeof(cases) / sizeof(DTLSServerFlowCase)); i++) {
        DTLSServerFlowCase c = cases[i];

        MockDtlsCallback cio; SrsDtls client(&cio);
        MockDtlsCallback sio; MockDtls server(&sio);
        MockBridgeDtlsIO b0(&cio, NULL, &server); MockBridgeDtlsIO b1(&sio, &client, NULL);
        HELPER_EXPECT_SUCCESS(client.initialize("active", c.ClientVersion)) << c;
        HELPER_EXPECT_SUCCESS(server.initialize("passive", c.ServerVersion)) << c;

        HELPER_EXPECT_SUCCESS(client.start_active_handshake()) << c;
        mock_wait_dtls_io_done();

        // Note that the cio error is generated from server, vice versa.
        EXPECT_EQ(c.ClientError, sio.r0 != srs_success) << c;
        EXPECT_EQ(c.ServerError, cio.r0 != srs_success) << c;

        EXPECT_EQ(c.ClientDone, cio.done) << c;
        EXPECT_EQ(c.ServerDone, sio.done) << c;
    }
}

VOID TEST(KernelRTCTest, DTLSServerFlowTest)
{
    srs_error_t err = srs_success;

    DTLSServerFlowCase cases[] = {
        // OK, Client, Server: DTLS v1.0
        {0, "dtls1.0", "dtls1.0", true, true, false, false},
        // OK, Client, Server: DTLS v1.2
        {1, "dtls1.2", "dtls1.2", true, true, false, false},
        // OK, Client: DTLS v1.0, Server: DTLS auto(v1.0 or v1.2).
        {2, "dtls1.0", "auto", true, true, false, false},
        // OK, Client: DTLS v1.2, Server: DTLS auto(v1.0 or v1.2).
        {3, "dtls1.2", "auto", true, true, false, false},
        // OK, Client: DTLS auto(v1.0 or v1.2), Server: DTLS v1.0
        {4, "auto", "dtls1.0", true, true, false, false},
        // OK, Client: DTLS auto(v1.0 or v1.2), Server: DTLS v1.0
        {5, "auto", "dtls1.2", true, true, false, false},
        // Fail, Client: DTLS v1.0, Server: DTLS v1.2
        {6, "dtls1.0", "dtls1.2", false, false, false, true},
        // Fail, Client: DTLS v1.2, Server: DTLS v1.0
        {7, "dtls1.2", "dtls1.0", false, false, true, false},
    };

    for (int i = 0; i < (int)(sizeof(cases) / sizeof(DTLSServerFlowCase)); i++) {
        DTLSServerFlowCase c = cases[i];

        MockDtlsCallback cio; MockDtls client(&cio);
        MockDtlsCallback sio; SrsDtls server(&sio);
        MockBridgeDtlsIO b0(&cio, &server, NULL); MockBridgeDtlsIO b1(&sio, NULL, &client);
        HELPER_EXPECT_SUCCESS(client.initialize("active", c.ClientVersion)) << c;
        HELPER_EXPECT_SUCCESS(server.initialize("passive", c.ServerVersion)) << c;

        HELPER_EXPECT_SUCCESS(client.start_active_handshake()) << c;
        mock_wait_dtls_io_done();

        // Note that the cio error is generated from server, vice versa.
        EXPECT_EQ(c.ClientError, sio.r0 != srs_success) << c;
        EXPECT_EQ(c.ServerError, cio.r0 != srs_success) << c;

        EXPECT_EQ(c.ClientDone, cio.done) << c;
        EXPECT_EQ(c.ServerDone, sio.done) << c;
    }
}

VOID TEST(KernelRTCTest, SequenceCompare)
{
    if (true) {
        EXPECT_EQ(0, srs_rtp_seq_distance(0, 0));
        EXPECT_EQ(0, srs_rtp_seq_distance(1, 1));
        EXPECT_EQ(0, srs_rtp_seq_distance(3, 3));

        EXPECT_EQ(1, srs_rtp_seq_distance(0, 1));
        EXPECT_EQ(-1, srs_rtp_seq_distance(1, 0));
        EXPECT_EQ(1, srs_rtp_seq_distance(65535, 0));
    }

    if (true) {
        EXPECT_FALSE(srs_rtp_seq_distance(1, 1) > 0);
        EXPECT_TRUE(srs_rtp_seq_distance(65534, 65535) > 0);
        EXPECT_TRUE(srs_rtp_seq_distance(0, 1) > 0);
        EXPECT_TRUE(srs_rtp_seq_distance(255, 256) > 0);

        EXPECT_TRUE(srs_rtp_seq_distance(65535, 0) > 0);
        EXPECT_TRUE(srs_rtp_seq_distance(65280, 0) > 0);
        EXPECT_TRUE(srs_rtp_seq_distance(65535, 255) > 0);
        EXPECT_TRUE(srs_rtp_seq_distance(65280, 255) > 0);

        EXPECT_FALSE(srs_rtp_seq_distance(0, 65535) > 0);
        EXPECT_FALSE(srs_rtp_seq_distance(0, 65280) > 0);
        EXPECT_FALSE(srs_rtp_seq_distance(255, 65535) > 0);
        EXPECT_FALSE(srs_rtp_seq_distance(255, 65280) > 0);

        // Note that srs_rtp_seq_distance(0, 32768)>0 is TRUE by https://mp.weixin.qq.com/s/JZTInmlB9FUWXBQw_7NYqg
        //      but for WebRTC jitter buffer it's FALSE and we follow it.
        EXPECT_FALSE(srs_rtp_seq_distance(0, 32768) > 0);
        // It's FALSE definitely.
        EXPECT_FALSE(srs_rtp_seq_distance(32768, 0) > 0);
    }

    if (true) {
        EXPECT_FALSE(srs_seq_is_newer(1, 1));
        EXPECT_TRUE(srs_seq_is_newer(65535, 65534));
        EXPECT_TRUE(srs_seq_is_newer(1, 0));
        EXPECT_TRUE(srs_seq_is_newer(256, 255));

        EXPECT_TRUE(srs_seq_is_newer(0, 65535));
        EXPECT_TRUE(srs_seq_is_newer(0, 65280));
        EXPECT_TRUE(srs_seq_is_newer(255, 65535));
        EXPECT_TRUE(srs_seq_is_newer(255, 65280));

        EXPECT_FALSE(srs_seq_is_newer(65535, 0));
        EXPECT_FALSE(srs_seq_is_newer(65280, 0));
        EXPECT_FALSE(srs_seq_is_newer(65535, 255));
        EXPECT_FALSE(srs_seq_is_newer(65280, 255));

        EXPECT_FALSE(srs_seq_is_newer(32768, 0));
        EXPECT_FALSE(srs_seq_is_newer(0, 32768));
    }

    if (true) {
        EXPECT_FALSE(srs_seq_distance(1, 1) > 0);
        EXPECT_TRUE(srs_seq_distance(65535, 65534) > 0);
        EXPECT_TRUE(srs_seq_distance(1, 0) > 0);
        EXPECT_TRUE(srs_seq_distance(256, 255) > 0);

        EXPECT_TRUE(srs_seq_distance(0, 65535) > 0);
        EXPECT_TRUE(srs_seq_distance(0, 65280) > 0);
        EXPECT_TRUE(srs_seq_distance(255, 65535) > 0);
        EXPECT_TRUE(srs_seq_distance(255, 65280) > 0);

        EXPECT_FALSE(srs_seq_distance(65535, 0) > 0);
        EXPECT_FALSE(srs_seq_distance(65280, 0) > 0);
        EXPECT_FALSE(srs_seq_distance(65535, 255) > 0);
        EXPECT_FALSE(srs_seq_distance(65280, 255) > 0);

        EXPECT_FALSE(srs_seq_distance(32768, 0) > 0);
        EXPECT_FALSE(srs_seq_distance(0, 32768) > 0);
    }

    if (true) {
        EXPECT_FALSE(srs_seq_is_rollback(1, 1));
        EXPECT_FALSE(srs_seq_is_rollback(65535, 65534));
        EXPECT_FALSE(srs_seq_is_rollback(1, 0));
        EXPECT_FALSE(srs_seq_is_rollback(256, 255));

        EXPECT_TRUE(srs_seq_is_rollback(0, 65535));
        EXPECT_TRUE(srs_seq_is_rollback(0, 65280));
        EXPECT_TRUE(srs_seq_is_rollback(255, 65535));
        EXPECT_TRUE(srs_seq_is_rollback(255, 65280));

        EXPECT_FALSE(srs_seq_is_rollback(65535, 0));
        EXPECT_FALSE(srs_seq_is_rollback(65280, 0));
        EXPECT_FALSE(srs_seq_is_rollback(65535, 255));
        EXPECT_FALSE(srs_seq_is_rollback(65280, 255));

        EXPECT_FALSE(srs_seq_is_rollback(32768, 0));
        EXPECT_FALSE(srs_seq_is_rollback(0, 32768));
    }
}

VOID TEST(KernelRTCTest, DecodeHeaderWithPadding)
{
    srs_error_t err = srs_success;

    // RTP packet cipher with padding.
    uint8_t data[] = {
        0xb0, 0x66, 0x0a, 0x97, 0x7e, 0x32, 0x10, 0xee, 0x7d, 0xe6, 0xd0, 0xe6, 0xbe, 0xde, 0x00, 0x01, 0x31, 0x00, 0x16, 0x00, 0x25, 0xcd, 0xef, 0xce, 0xd7, 0x24, 0x57, 0xd9, 0x3c, 0xfd, 0x0f, 0x77, 0xea, 0x89, 0x61, 0xcb, 0x67, 0xa1, 0x65, 0x4a, 0x7d, 0x1f, 0x10, 0x4e, 0xed, 0x5e, 0x74, 0xe8, 0x7e, 0xce, 0x4d, 0xcf, 0xd5, 0x58, 0xd1, 0x2c, 0x30, 0xf1, 0x26, 0x62, 0xd3, 0x0c, 0x6a, 0x48,
        0x29, 0x83, 0xd2, 0x3d, 0x30, 0xa1, 0x7c, 0x6f, 0xa1, 0x5c, 0x9f, 0x08, 0x43, 0x50, 0x34, 0x2b, 0x3c, 0xa1, 0xf0, 0xb0, 0xe2, 0x0e, 0xc8, 0xf9, 0x79, 0x06, 0x51, 0xfe, 0xbb, 0x13, 0x54, 0x3e, 0xb4, 0x37, 0x91, 0x96, 0x94, 0xb7, 0x61, 0x2e, 0x97, 0x09, 0xb8, 0x27, 0x10, 0x6a, 0x2e, 0xe0, 0x62, 0xe4, 0x37, 0x41, 0xab, 0x4f, 0xbf, 0x06, 0x0a, 0x89, 0x80, 0x18, 0x0d, 0x6e, 0x0a, 0xd1,
        0x9f, 0xf1, 0xdd, 0x12, 0xbd, 0x1a, 0x70, 0x72, 0x33, 0xcc, 0xaa, 0x82, 0xdf, 0x92, 0x90, 0x45, 0xda, 0x3e, 0x88, 0x1c, 0x63, 0x83, 0xbc, 0xc8, 0xff, 0xfd, 0x64, 0xe3, 0xd4, 0x68, 0xe6, 0xc8, 0xdc, 0x81, 0x72, 0x5f, 0x38, 0x5b, 0xab, 0x63, 0x7b, 0x96, 0x03, 0x03, 0x54, 0xc5, 0xe6, 0x35, 0xf6, 0x86, 0xcc, 0xac, 0x74, 0xb0, 0xf4, 0x07, 0x9e, 0x19, 0x30, 0x4f, 0x90, 0xd6, 0xdb, 0x8b,
        0x0d, 0xcb, 0x76, 0x71, 0x55, 0xc7, 0x4a, 0x6e, 0x1b, 0xb4, 0x42, 0xf4, 0xae, 0x81, 0x17, 0x08, 0xb7, 0x50, 0x61, 0x5a, 0x42, 0xde, 0x1f, 0xf3, 0xfd, 0xe2, 0x30, 0xff, 0xb7, 0x07, 0xdd, 0x4b, 0xb1, 0x00, 0xd9, 0x6c, 0x43, 0xa0, 0x9a, 0xfa, 0xbb, 0xec, 0xdf, 0x51, 0xce, 0x33, 0x79, 0x4b, 0xa7, 0x02, 0xf3, 0x96, 0x62, 0x42, 0x25, 0x28, 0x85, 0xa7, 0xe7, 0xd1, 0xd3, 0xf3,
    };

    // If not plaintext, the padding in body is invalid,
    // so it will fail if decoding the header with padding.
    if (true) {
        SrsBuffer b((char*)data, sizeof(data)); SrsRtpHeader h;
        HELPER_EXPECT_FAILED(h.decode(&b));
    }

    // Should ok if ignore padding.
    if (true) {
        SrsBuffer b((char*)data, sizeof(data)); SrsRtpHeader h;
        h.ignore_padding(true);
        HELPER_EXPECT_SUCCESS(h.decode(&b));
    }
}

VOID TEST(KernelRTCTest, DumpsHexToString)
{
    if (true) {
        EXPECT_STREQ("", srs_string_dumps_hex(NULL, 0).c_str());
    }

    if (true) {
        uint8_t data[] = {0, 0, 0, 0};
        EXPECT_STREQ("00 00 00 00", srs_string_dumps_hex((const char*)data, sizeof(data)).c_str());
    }

    if (true) {
        uint8_t data[] = {0, 1, 2, 3};
        EXPECT_STREQ("00 01 02 03", srs_string_dumps_hex((const char*)data, sizeof(data)).c_str());
    }

    if (true) {
        uint8_t data[] = {0xa, 3, 0xf, 3};
        EXPECT_STREQ("0a 03 0f 03", srs_string_dumps_hex((const char*)data, sizeof(data)).c_str());
    }

    if (true) {
        uint8_t data[] = {0xa, 3, 0xf, 3};
        EXPECT_STREQ("0a,03,0f,03", srs_string_dumps_hex((const char*)data, sizeof(data), INT_MAX, ',', 0, 0).c_str());
        EXPECT_STREQ("0a030f03", srs_string_dumps_hex((const char*)data, sizeof(data), INT_MAX, '\0', 0, 0).c_str());
        EXPECT_STREQ("0a,03,\n0f,03", srs_string_dumps_hex((const char*)data, sizeof(data), INT_MAX, ',', 2, '\n').c_str());
        EXPECT_STREQ("0a,03,0f,03", srs_string_dumps_hex((const char*)data, sizeof(data), INT_MAX, ',', 2,'\0').c_str());
    }

    if (true) {
        uint8_t data[] = {0xa, 3, 0xf};
        EXPECT_STREQ("0a 03", srs_string_dumps_hex((const char*)data, sizeof(data), 2).c_str());
    }
}

VOID TEST(KernelRTCTest, NACKFetchRTPPacket)
{
    SrsRtcConnection s(NULL, SrsContextId());
    SrsRtcPlayStream play(&s, SrsContextId());

    SrsRtcTrackDescription ds;
    SrsRtcVideoSendTrack *track = new SrsRtcVideoSendTrack(&s, &ds);

    // The RTP queue will free the packet.
    if (true) {
        SrsRtpPacket2* pkt = new SrsRtpPacket2();
        pkt->header.set_sequence(100);
        track->rtp_queue_->set(pkt->header.get_sequence(), pkt);
    }

    // If sequence not match, packet not found.
    if (true) {
        SrsRtpPacket2* pkt = track->fetch_rtp_packet(10);
        EXPECT_TRUE(pkt == NULL);
    }

    // The sequence matched, we got the packet.
    if (true) {
        SrsRtpPacket2* pkt = track->fetch_rtp_packet(100);
        EXPECT_TRUE(pkt != NULL);
    }

    // NACK special case.
    if (true) {
        // The sequence is the "same", 1100%1000 is 100,
        // so we can also get it from the RTP queue.
        SrsRtpPacket2* pkt = track->rtp_queue_->at(1100);
        EXPECT_TRUE(pkt != NULL);

        // But the track requires exactly match, so it returns NULL.
        pkt = track->fetch_rtp_packet(1100);
        EXPECT_TRUE(pkt == NULL);
    }
}

extern bool srs_is_stun(const uint8_t* data, size_t size);
extern bool srs_is_dtls(const uint8_t* data, size_t len);
extern bool srs_is_rtp_or_rtcp(const uint8_t* data, size_t len);
extern bool srs_is_rtcp(const uint8_t* data, size_t len);

#define mock_arr_push(arr, elem) arr.push_back(vector<uint8_t>(elem, elem + sizeof(elem)))

VOID TEST(KernelRTCTest, TestPacketType)
{
    // DTLS packet.
    vector< vector<uint8_t> > dtlss;
    if (true) { uint8_t data[13] = {20}; mock_arr_push(dtlss, data); } // change_cipher_spec(20)
    if (true) { uint8_t data[13] = {21}; mock_arr_push(dtlss, data); } // alert(21)
    if (true) { uint8_t data[13] = {22}; mock_arr_push(dtlss, data); } // handshake(22)
    if (true) { uint8_t data[13] = {23}; mock_arr_push(dtlss, data); } // application_data(23)
    for (int i = 0; i < (int)dtlss.size(); i++) {
        vector<uint8_t> elem = dtlss.at(i);
        EXPECT_TRUE(srs_is_dtls(&elem[0], (size_t)elem.size()));
    }

    for (int i = 0; i < (int)dtlss.size(); i++) {
        vector<uint8_t> elem = dtlss.at(i);
        EXPECT_FALSE(srs_is_dtls(&elem[0], 1));

        // All DTLS should not be other packets.
        EXPECT_FALSE(srs_is_stun(&elem[0], (size_t)elem.size()));
        EXPECT_TRUE(srs_is_dtls(&elem[0], (size_t)elem.size()));
        EXPECT_FALSE(srs_is_rtp_or_rtcp(&elem[0], (size_t)elem.size()));
        EXPECT_FALSE(srs_is_rtcp(&elem[0], (size_t)elem.size()));
    }

    // STUN packet.
    vector< vector<uint8_t> > stuns;
    if (true) { uint8_t data[1] = {0}; mock_arr_push(stuns, data); } // binding request.
    if (true) { uint8_t data[1] = {1}; mock_arr_push(stuns, data); } // binding success response.
    for (int i = 0; i < (int)stuns.size(); i++) {
        vector<uint8_t> elem = stuns.at(i);
        EXPECT_TRUE(srs_is_stun(&elem[0], (size_t)elem.size()));
    }

    for (int i = 0; i < (int)stuns.size(); i++) {
        vector<uint8_t> elem = stuns.at(i);
        EXPECT_FALSE(srs_is_stun(&elem[0], 0));

        // All STUN should not be other packets.
        EXPECT_TRUE(srs_is_stun(&elem[0], (size_t)elem.size()));
        EXPECT_FALSE(srs_is_dtls(&elem[0], (size_t)elem.size()));
        EXPECT_FALSE(srs_is_rtp_or_rtcp(&elem[0], (size_t)elem.size()));
        EXPECT_FALSE(srs_is_rtcp(&elem[0], (size_t)elem.size()));
    }

    // RTCP packet.
    vector< vector<uint8_t> > rtcps;
    if (true) { uint8_t data[12] = {0x80, 192}; mock_arr_push(rtcps, data); }
    if (true) { uint8_t data[12] = {0x80, 200}; mock_arr_push(rtcps, data); } // SR
    if (true) { uint8_t data[12] = {0x80, 201}; mock_arr_push(rtcps, data); } // RR
    if (true) { uint8_t data[12] = {0x80, 202}; mock_arr_push(rtcps, data); } // SDES
    if (true) { uint8_t data[12] = {0x80, 203}; mock_arr_push(rtcps, data); } // BYE
    if (true) { uint8_t data[12] = {0x80, 204}; mock_arr_push(rtcps, data); } // APP
    if (true) { uint8_t data[12] = {0x80, 223}; mock_arr_push(rtcps, data); }
    for (int i = 0; i < (int)rtcps.size(); i++) {
        vector<uint8_t> elem = rtcps.at(i);
        EXPECT_TRUE(srs_is_rtcp(&elem[0], (size_t)elem.size()));
    }

    for (int i = 0; i < (int)rtcps.size(); i++) {
        vector<uint8_t> elem = rtcps.at(i);
        EXPECT_FALSE(srs_is_rtcp(&elem[0], 2));

        // All RTCP should not be other packets.
        EXPECT_FALSE(srs_is_stun(&elem[0], (size_t)elem.size()));
        EXPECT_FALSE(srs_is_dtls(&elem[0], (size_t)elem.size()));
        EXPECT_TRUE(srs_is_rtp_or_rtcp(&elem[0], (size_t)elem.size()));
        EXPECT_TRUE(srs_is_rtcp(&elem[0], (size_t)elem.size()));
    }

    // RTP packet.
    vector< vector<uint8_t> > rtps;
    if (true) { uint8_t data[12] = {0x80, 96}; mock_arr_push(rtps, data); }
    if (true) { uint8_t data[12] = {0x80, 127}; mock_arr_push(rtps, data); }
    if (true) { uint8_t data[12] = {0x80, 224}; mock_arr_push(rtps, data); }
    if (true) { uint8_t data[12] = {0x80, 255}; mock_arr_push(rtps, data); }
    for (int i = 0; i < (int)rtps.size(); i++) {
        vector<uint8_t> elem = rtps.at(i);
        EXPECT_TRUE(srs_is_rtp_or_rtcp(&elem[0], (size_t)elem.size()));
        EXPECT_FALSE(srs_is_rtcp(&elem[0], (size_t)elem.size()));
    }

    for (int i = 0; i < (int)rtps.size(); i++) {
        vector<uint8_t> elem = rtps.at(i);
        EXPECT_FALSE(srs_is_rtp_or_rtcp(&elem[0], 2));

        // All RTP should not be other packets.
        EXPECT_FALSE(srs_is_stun(&elem[0], (size_t)elem.size()));
        EXPECT_FALSE(srs_is_dtls(&elem[0], (size_t)elem.size()));
        EXPECT_TRUE(srs_is_rtp_or_rtcp(&elem[0], (size_t)elem.size()));
        EXPECT_FALSE(srs_is_rtcp(&elem[0], (size_t)elem.size()));
    }
}

VOID TEST(KernelRTCTest, DefaultTrackStatus)
{
    // By default, track is disabled.
    if (true) {
        SrsRtcTrackDescription td;

        // The track must default to disable, that is, the active is false.
        EXPECT_FALSE(td.is_active_);
    }

    // Enable it by player.
    if (true) {
        SrsRtcConnection s(NULL, SrsContextId()); SrsRtcPlayStream play(&s, SrsContextId());
        SrsRtcAudioSendTrack* audio; SrsRtcVideoSendTrack *video;

        if (true) {
            SrsRtcTrackDescription ds; ds.type_ = "audio"; ds.id_ = "NSNWOn19NDn12o8nNeji2"; ds.ssrc_ = 100;
            play.audio_tracks_[ds.ssrc_] = audio = new SrsRtcAudioSendTrack(&s, &ds);
        }
        if (true) {
            SrsRtcTrackDescription ds; ds.type_ = "video"; ds.id_ = "VMo22nfLDn122nfnDNL2"; ds.ssrc_ = 200;
            play.video_tracks_[ds.ssrc_] = video = new SrsRtcVideoSendTrack(&s, &ds);
        }
        EXPECT_FALSE(audio->get_track_status());
        EXPECT_FALSE(video->get_track_status());

        play.set_all_tracks_status(true);
        EXPECT_TRUE(audio->get_track_status());
        EXPECT_TRUE(video->get_track_status());
    }

    // Enable it by publisher.
    if (true) {
        SrsRtcConnection s(NULL, SrsContextId()); SrsRtcPublishStream publish(&s);
        SrsRtcAudioRecvTrack* audio; SrsRtcVideoRecvTrack *video;

        if (true) {
            SrsRtcTrackDescription ds; ds.type_ = "audio"; ds.id_ = "NSNWOn19NDn12o8nNeji2"; ds.ssrc_ = 100;
            audio = new SrsRtcAudioRecvTrack(&s, &ds); publish.audio_tracks_.push_back(audio);
        }
        if (true) {
            SrsRtcTrackDescription ds; ds.type_ = "video"; ds.id_ = "VMo22nfLDn122nfnDNL2"; ds.ssrc_ = 200;
            video = new SrsRtcVideoRecvTrack(&s, &ds); publish.video_tracks_.push_back(video);
        }
        EXPECT_FALSE(audio->get_track_status());
        EXPECT_FALSE(video->get_track_status());

        publish.set_all_tracks_status(true);
        EXPECT_TRUE(audio->get_track_status());
        EXPECT_TRUE(video->get_track_status());
    }
}

