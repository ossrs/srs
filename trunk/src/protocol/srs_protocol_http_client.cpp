//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_protocol_http_client.hpp>

#include <arpa/inet.h>
#include <sstream>
using namespace std;

#include <srs_core_autofree.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_kbps.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_http_conn.hpp>

// The return value of verify_callback controls the strategy of the further verification process. If verify_callback
// returns 0, the verification process is immediately stopped with "verification failed" state. If SSL_VERIFY_PEER is
// set, a verification failure alert is sent to the peer and the TLS/SSL handshake is terminated. If verify_callback
// returns 1, the verification process is continued. If verify_callback always returns 1, the TLS/SSL handshake will
// not be terminated with respect to verification failures and the connection will be established. The calling process
// can however retrieve the error code of the last verification error using SSL_get_verify_result(3) or by maintaining
// its own error storage managed by verify_callback.
// @see https://www.openssl.org/docs/man1.0.2/man3/SSL_CTX_set_verify.html
int srs_verify_callback(int preverify_ok, X509_STORE_CTX *ctx)
{
    // Always OK, we don't check the certificate of client,
    // because we allow client self-sign certificate.
    return 1;
}

SrsSslClient::SrsSslClient(SrsTcpClient *tcp)
{
    transport_ = tcp;
    ssl_ctx_ = NULL;
    ssl_ = NULL;
}

SrsSslClient::~SrsSslClient()
{
    if (ssl_) {
        // this function will free bio_in_ and bio_out_
        SSL_free(ssl_);
        ssl_ = NULL;
    }

    if (ssl_ctx_) {
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = NULL;
    }
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
srs_error_t SrsSslClient::handshake(const std::string &host)
{
    srs_error_t err = srs_success;

    // For HTTPS, try to connect over security transport.
#if (OPENSSL_VERSION_NUMBER < 0x10002000L) // v1.0.2
    ssl_ctx_ = SSL_CTX_new(TLS_method());
#else
    ssl_ctx_ = SSL_CTX_new(TLSv1_2_method());
#endif
    SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_PEER, srs_verify_callback);
    srs_assert(SSL_CTX_set_cipher_list(ssl_ctx_, "ALL") == 1);

    // TODO: Setup callback, see SSL_set_ex_data and SSL_set_info_callback
    if ((ssl_ = SSL_new(ssl_ctx_)) == NULL) {
        return srs_error_new(ERROR_TLS_HANDSHAKE, "SSL_new ssl");
    }

    if ((bio_in_ = BIO_new(BIO_s_mem())) == NULL) {
        return srs_error_new(ERROR_TLS_HANDSHAKE, "BIO_new in");
    }

    if ((bio_out_ = BIO_new(BIO_s_mem())) == NULL) {
        BIO_free(bio_in_);
        return srs_error_new(ERROR_TLS_HANDSHAKE, "BIO_new out");
    }

    SSL_set_bio(ssl_, bio_in_, bio_out_);

    // SSL setup active, as client role.
    SSL_set_connect_state(ssl_);
    SSL_set_mode(ssl_, SSL_MODE_ENABLE_PARTIAL_WRITE);
    // If the server address is not in IP address format, set the host in the Server Name Indication (SNI) field.
    if (!srs_net_is_valid_ip(host)) {
        SSL_set_tlsext_host_name(ssl_, host.c_str());
    }

    // Send ClientHello.
    int r0 = SSL_do_handshake(ssl_);
    int r1 = SSL_get_error(ssl_, r0);
    ERR_clear_error();
    if (r0 != -1 || r1 != SSL_ERROR_WANT_READ) {
        return srs_error_new(ERROR_TLS_HANDSHAKE, "handshake r0=%d, r1=%d", r0, r1);
    }

    uint8_t *data = NULL;
    int size = BIO_get_mem_data(bio_out_, &data);
    if (!data || size <= 0) {
        return srs_error_new(ERROR_TLS_HANDSHAKE, "handshake data=%p, size=%d", data, size);
    }
    if ((err = transport_->write(data, size, NULL)) != srs_success) {
        return srs_error_wrap(err, "handshake: write data=%p, size=%d", data, size);
    }
    if ((r0 = BIO_reset(bio_out_)) != 1) {
        return srs_error_new(ERROR_TLS_HANDSHAKE, "BIO_reset r0=%d", r0);
    }

    srs_info("tls: ClientHello done");

    // Receive ServerHello, Certificate, Server Key Exchange, Server Hello Done
    while (true) {
        char buf[512];
        ssize_t nn = 0;
        if ((err = transport_->read(buf, sizeof(buf), &nn)) != srs_success) {
            return srs_error_wrap(err, "handshake: read");
        }

        if ((r0 = BIO_write(bio_in_, buf, nn)) <= 0) {
            // TODO: 0 or -1 maybe block, use BIO_should_retry to check.
            return srs_error_new(ERROR_TLS_HANDSHAKE, "BIO_write r0=%d, data=%p, size=%d", r0, buf, nn);
        }

        r0 = SSL_do_handshake(ssl_);
        r1 = SSL_get_error(ssl_, r0);
        ERR_clear_error();
        if (r0 != -1 || r1 != SSL_ERROR_WANT_READ) {
            return srs_error_new(ERROR_TLS_HANDSHAKE, "handshake r0=%d, r1=%d", r0, r1);
        }

        if ((size = BIO_get_mem_data(bio_out_, &data)) > 0) {
            // OK, reset it for the next write.
            if ((r0 = BIO_reset(bio_in_)) != 1) {
                return srs_error_new(ERROR_TLS_HANDSHAKE, "BIO_reset r0=%d", r0);
            }
            break;
        }
    }

    srs_info("tls: ServerHello done");

    // Send Client Key Exchange, Change Cipher Spec, Encrypted Handshake Message
    if ((err = transport_->write(data, size, NULL)) != srs_success) {
        return srs_error_wrap(err, "handshake: write data=%p, size=%d", data, size);
    }
    if ((r0 = BIO_reset(bio_out_)) != 1) {
        return srs_error_new(ERROR_TLS_HANDSHAKE, "BIO_reset r0=%d", r0);
    }

    srs_info("tls: Client done");

    // Receive New Session Ticket, Change Cipher Spec, Encrypted Handshake Message
    while (true) {
        char buf[128];
        ssize_t nn = 0;
        if ((err = transport_->read(buf, sizeof(buf), &nn)) != srs_success) {
            return srs_error_wrap(err, "handshake: read");
        }

        if ((r0 = BIO_write(bio_in_, buf, nn)) <= 0) {
            // TODO: 0 or -1 maybe block, use BIO_should_retry to check.
            return srs_error_new(ERROR_TLS_HANDSHAKE, "BIO_write r0=%d, data=%p, size=%d", r0, buf, nn);
        }

        r0 = SSL_do_handshake(ssl_);
        r1 = SSL_get_error(ssl_, r0);
        ERR_clear_error();
        if (r0 == 1 && r1 == SSL_ERROR_NONE) {
            break;
        }

        if (r0 != -1 || r1 != SSL_ERROR_WANT_READ) {
            return srs_error_new(ERROR_TLS_HANDSHAKE, "handshake r0=%d, r1=%d", r0, r1);
        }
    }

    srs_info("tls: Server done");

    return err;
}
#pragma GCC diagnostic pop

srs_error_t SrsSslClient::read(void *plaintext, size_t nn_plaintext, ssize_t *nread)
{
    srs_error_t err = srs_success;

    while (true) {
        int r0 = SSL_read(ssl_, plaintext, nn_plaintext);
        int r1 = SSL_get_error(ssl_, r0);
        ERR_clear_error();
        int r2 = BIO_ctrl_pending(bio_in_);
        int r3 = SSL_is_init_finished(ssl_);

        // Peer gracefully close.
        if (r0 == 0 && r1 == SSL_ERROR_ZERO_RETURN) {
            return srs_error_new(ERROR_SOCKET_READ, "SSL_read r0=%d, r1=%d, r2=%d, r3=%d",
                                 r0, r1, r2, r3);
        }

        // OK, got data.
        if (r0 > 0) {
            srs_assert(r0 <= (int)nn_plaintext);
            if (nread) {
                *nread = r0;
            }
            return err;
        }

        // Need to read more data to feed SSL.
        if (r0 == -1 && r1 == SSL_ERROR_WANT_READ) {
            // TODO: Can we avoid copy?
            int nn_cipher = nn_plaintext;
            SrsUniquePtr<char[]> cipher(new char[nn_cipher]);

            // Read the cipher from SSL.
            ssize_t nn = 0;
            if ((err = transport_->read(cipher.get(), nn_cipher, &nn)) != srs_success) {
                return srs_error_wrap(err, "https: read");
            }

            int r0 = BIO_write(bio_in_, cipher.get(), nn);
            if (r0 <= 0) {
                // TODO: 0 or -1 maybe block, use BIO_should_retry to check.
                return srs_error_new(ERROR_TLS_READ, "BIO_write r0=%d, cipher=%p, size=%d", r0, cipher.get(), nn);
            }
            continue;
        }

        // Fail for error.
        if (r0 <= 0) {
            return srs_error_new(ERROR_TLS_READ, "SSL_read r0=%d, r1=%d, r2=%d, r3=%d",
                                 r0, r1, r2, r3);
        }
    }
}

srs_error_t SrsSslClient::write(void *plaintext, size_t nn_plaintext, ssize_t *nwrite)
{
    srs_error_t err = srs_success;

    for (char *p = (char *)plaintext; p < (char *)plaintext + nn_plaintext;) {
        int left = (int)nn_plaintext - (p - (char *)plaintext);
        int r0 = SSL_write(ssl_, (const void *)p, left);
        int r1 = SSL_get_error(ssl_, r0);
        ERR_clear_error();
        if (r0 <= 0) {
            return srs_error_new(ERROR_TLS_WRITE, "tls: write data=%p, size=%d, r0=%d, r1=%d", p, left, r0, r1);
        }

        // Move p to the next writing position.
        p += r0;
        if (nwrite) {
            *nwrite += (ssize_t)r0;
        }

        uint8_t *data = NULL;
        int size = BIO_get_mem_data(bio_out_, &data);
        if ((err = transport_->write(data, size, NULL)) != srs_success) {
            return srs_error_wrap(err, "tls: write data=%p, size=%d", data, size);
        }
        if ((r0 = BIO_reset(bio_out_)) != 1) {
            return srs_error_new(ERROR_TLS_WRITE, "BIO_reset r0=%d", r0);
        }
    }

    return err;
}

ISrsHttpClient::ISrsHttpClient()
{
}

ISrsHttpClient::~ISrsHttpClient()
{
}

SrsHttpClient::SrsHttpClient()
{
    transport_ = NULL;
    ssl_transport_ = NULL;
    kbps_ = new SrsNetworkKbps();
    parser_ = NULL;
    recv_timeout_ = timeout_ = SRS_UTIME_NO_TIMEOUT;
    port_ = 0;
}

SrsHttpClient::~SrsHttpClient()
{
    disconnect();

    srs_freep(kbps_);
    srs_freep(parser_);
}

srs_error_t SrsHttpClient::initialize(string schema, string h, int p, srs_utime_t tm)
{
    srs_error_t err = srs_success;

    srs_freep(parser_);
    parser_ = new SrsHttpParser();

    if ((err = parser_->initialize(HTTP_RESPONSE)) != srs_success) {
        return srs_error_wrap(err, "http: init parser");
    }

    // Always disconnect the transport.
    schema_ = schema;
    host_ = h;
    port_ = p;
    recv_timeout_ = timeout_ = tm;
    disconnect();

    // ep used for host in header.
    string ep = host_;
    if (port_ > 0 && port_ != SRS_CONSTS_HTTP_DEFAULT_PORT) {
        ep += ":" + srs_strconv_format_int(port_);
    }

    // Set default value for headers.
    headers_["Host"] = ep;
    headers_["Connection"] = "Keep-Alive";
    headers_["User-Agent"] = RTMP_SIG_SRS_SERVER;
    headers_["Content-Type"] = "application/json";

    return err;
}

SrsHttpClient *SrsHttpClient::set_header(string k, string v)
{
    headers_[k] = v;

    return this;
}

srs_error_t SrsHttpClient::post(string path, string req, ISrsHttpMessage **ppmsg)
{
    *ppmsg = NULL;

    srs_error_t err = srs_success;

    // always set the content length.
    headers_["Content-Length"] = srs_strconv_format_int(req.length());

    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "http: connect server");
    }

    if (path.size() == 0) {
        path = "/";
    }

    // TODO: FIXME: Use SrsHttpMessageWriter, never use stringstream and headers_.
    // send POST request to uri
    // POST %s HTTP/1.1\r\nHost: %s\r\nContent-Length: %d\r\n\r\n%s
    std::stringstream ss;
    ss << "POST " << path << " " << "HTTP/1.1" << SRS_HTTP_CRLF;
    for (map<string, string>::iterator it = headers_.begin(); it != headers_.end(); ++it) {
        string key = it->first;
        string value = it->second;
        ss << key << ": " << value << SRS_HTTP_CRLF;
    }
    ss << SRS_HTTP_CRLF << req;

    std::string data = ss.str();
    if ((err = writer()->write((void *)data.c_str(), data.length(), NULL)) != srs_success) {
        // Disconnect the transport when channel error, reconnect for next operation.
        disconnect();
        return srs_error_wrap(err, "http: write");
    }

    ISrsHttpMessage *msg = NULL;
    if ((err = parser_->parse_message(reader(), &msg)) != srs_success) {
        return srs_error_wrap(err, "http: parse response");
    }
    srs_assert(msg);

    if (ppmsg) {
        *ppmsg = msg;
    } else {
        srs_freep(msg);
    }

    return err;
}

srs_error_t SrsHttpClient::get(string path, string req, ISrsHttpMessage **ppmsg)
{
    *ppmsg = NULL;

    srs_error_t err = srs_success;

    // always set the content length.
    headers_["Content-Length"] = srs_strconv_format_int(req.length());

    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "http: connect server");
    }

    // send POST request to uri
    // GET %s HTTP/1.1\r\nHost: %s\r\nContent-Length: %d\r\n\r\n%s
    std::stringstream ss;
    ss << "GET " << path << " " << "HTTP/1.1" << SRS_HTTP_CRLF;
    for (map<string, string>::iterator it = headers_.begin(); it != headers_.end(); ++it) {
        string key = it->first;
        string value = it->second;
        ss << key << ": " << value << SRS_HTTP_CRLF;
    }
    ss << SRS_HTTP_CRLF << req;

    std::string data = ss.str();
    if ((err = writer()->write((void *)data.c_str(), data.length(), NULL)) != srs_success) {
        // Disconnect the transport when channel error, reconnect for next operation.
        disconnect();
        return srs_error_wrap(err, "http: write");
    }

    ISrsHttpMessage *msg = NULL;
    if ((err = parser_->parse_message(reader(), &msg)) != srs_success) {
        return srs_error_wrap(err, "http: parse response");
    }
    srs_assert(msg);

    if (ppmsg) {
        *ppmsg = msg;
    } else {
        srs_freep(msg);
    }

    return err;
}

void SrsHttpClient::set_recv_timeout(srs_utime_t tm)
{
    recv_timeout_ = tm;
}

void SrsHttpClient::kbps_sample(const char *label, srs_utime_t age)
{
    kbps_->sample();

    int sr = kbps_->get_send_kbps();
    int sr30s = kbps_->get_send_kbps_30s();
    int sr5m = kbps_->get_send_kbps_5m();
    int rr = kbps_->get_recv_kbps();
    int rr30s = kbps_->get_recv_kbps_30s();
    int rr5m = kbps_->get_recv_kbps_5m();

    srs_trace("<- %s time=%" PRId64 ", okbps=%d,%d,%d, ikbps=%d,%d,%d", label, srsu2ms(age), sr, sr30s, sr5m, rr, rr30s, rr5m);
}

void SrsHttpClient::disconnect()
{
    kbps_->set_io(NULL, NULL);
    srs_freep(ssl_transport_);
    srs_freep(transport_);
}

srs_error_t SrsHttpClient::connect()
{
    srs_error_t err = srs_success;

    // When transport connected, ignore.
    if (transport_) {
        return err;
    }

    transport_ = new SrsTcpClient(host_, port_, timeout_);
    if ((err = transport_->connect()) != srs_success) {
        disconnect();
        return srs_error_wrap(err, "http: tcp connect %s %s:%d to=%dms, rto=%dms",
                              schema_.c_str(), host_.c_str(), port_, srsu2msi(timeout_), srsu2msi(recv_timeout_));
    }

    // Set the recv/send timeout in srs_utime_t.
    transport_->set_recv_timeout(recv_timeout_);
    transport_->set_send_timeout(timeout_);

    kbps_->set_io(transport_, transport_);

    if (schema_ != "https") {
        return err;
    }

#if !defined(SRS_HTTPS)
    return srs_error_new(ERROR_HTTPS_NOT_SUPPORTED, "should configure with --https=on");
#else
    srs_assert(!ssl_transport_);
    ssl_transport_ = new SrsSslClient(transport_);

    srs_utime_t starttime = srs_time_now_realtime();

    if ((err = ssl_transport_->handshake(host_)) != srs_success) {
        disconnect();
        return srs_error_wrap(err, "http: ssl connect %s %s:%d to=%dms, rto=%dms",
                              schema_.c_str(), host_.c_str(), port_, srsu2msi(timeout_), srsu2msi(recv_timeout_));
    }

    int cost = srsu2msi(srs_time_now_realtime() - starttime);
    srs_trace("https: connected to %s://%s:%d, cost=%dms", schema_.c_str(), host_.c_str(), port_, cost);

    return err;
#endif
}

ISrsStreamWriter *SrsHttpClient::writer()
{
    if (ssl_transport_) {
        return ssl_transport_;
    }
    return transport_;
}

ISrsReader *SrsHttpClient::reader()
{
    if (ssl_transport_) {
        return ssl_transport_;
    }
    return transport_;
}
