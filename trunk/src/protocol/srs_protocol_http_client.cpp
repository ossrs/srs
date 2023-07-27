//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_protocol_http_client.hpp>

#include <arpa/inet.h>
#include <sstream>
using namespace std;

#include <srs_protocol_kbps.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_core_autofree.hpp>
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

SrsSslClient::SrsSslClient(SrsTcpClient* tcp)
{
    transport = tcp;
    ssl_ctx = NULL;
    ssl = NULL;
}

SrsSslClient::~SrsSslClient()
{
    if (ssl) {
        // this function will free bio_in and bio_out
        SSL_free(ssl);
        ssl = NULL;
    }

    if (ssl_ctx) {
        SSL_CTX_free(ssl_ctx);
        ssl_ctx = NULL;
    }
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
srs_error_t SrsSslClient::handshake(const std::string& host)
{
    srs_error_t err = srs_success;

    // For HTTPS, try to connect over security transport.
#if (OPENSSL_VERSION_NUMBER < 0x10002000L) // v1.0.2
    ssl_ctx = SSL_CTX_new(TLS_method());
#else
    ssl_ctx = SSL_CTX_new(TLSv1_2_method());
#endif
    SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, srs_verify_callback);
    srs_assert(SSL_CTX_set_cipher_list(ssl_ctx, "ALL") == 1);

    // TODO: Setup callback, see SSL_set_ex_data and SSL_set_info_callback
    if ((ssl = SSL_new(ssl_ctx)) == NULL) {
        return srs_error_new(ERROR_HTTPS_HANDSHAKE, "SSL_new ssl");
    }

    if ((bio_in = BIO_new(BIO_s_mem())) == NULL) {
        return srs_error_new(ERROR_HTTPS_HANDSHAKE, "BIO_new in");
    }

    if ((bio_out = BIO_new(BIO_s_mem())) == NULL) {
        BIO_free(bio_in);
        return srs_error_new(ERROR_HTTPS_HANDSHAKE, "BIO_new out");
    }

    SSL_set_bio(ssl, bio_in, bio_out);

    // SSL setup active, as client role.
    SSL_set_connect_state(ssl);
    SSL_set_mode(ssl, SSL_MODE_ENABLE_PARTIAL_WRITE);
    // If the server address is not in IP address format, set the host in the Server Name Indication (SNI) field.
    if (!srs_check_ip_addr_valid(host)) {
        SSL_set_tlsext_host_name(ssl, host.c_str());
    }

    // Send ClientHello.
    int r0 = SSL_do_handshake(ssl); int r1 = SSL_get_error(ssl, r0); ERR_clear_error();
    if (r0 != -1 || r1 != SSL_ERROR_WANT_READ) {
        return srs_error_new(ERROR_HTTPS_HANDSHAKE, "handshake r0=%d, r1=%d", r0, r1);
    }

    uint8_t* data = NULL;
    int size = BIO_get_mem_data(bio_out, &data);
    if (!data || size <= 0) {
        return srs_error_new(ERROR_HTTPS_HANDSHAKE, "handshake data=%p, size=%d", data, size);
    }
    if ((err = transport->write(data, size, NULL)) != srs_success) {
        return srs_error_wrap(err, "handshake: write data=%p, size=%d", data, size);
    }
    if ((r0 = BIO_reset(bio_out)) != 1) {
        return srs_error_new(ERROR_HTTPS_HANDSHAKE, "BIO_reset r0=%d", r0);
    }

    srs_info("https: ClientHello done");

    // Receive ServerHello, Certificate, Server Key Exchange, Server Hello Done
    while (true) {
        char buf[512]; ssize_t nn = 0;
        if ((err = transport->read(buf, sizeof(buf), &nn)) != srs_success) {
            return srs_error_wrap(err, "handshake: read");
        }

        if ((r0 = BIO_write(bio_in, buf, nn)) <= 0) {
            // TODO: 0 or -1 maybe block, use BIO_should_retry to check.
            return srs_error_new(ERROR_HTTPS_HANDSHAKE, "BIO_write r0=%d, data=%p, size=%d", r0, buf, nn);
        }

        r0 = SSL_do_handshake(ssl); r1 = SSL_get_error(ssl, r0); ERR_clear_error();
        if (r0 != -1 || r1 != SSL_ERROR_WANT_READ) {
            return srs_error_new(ERROR_HTTPS_HANDSHAKE, "handshake r0=%d, r1=%d", r0, r1);
        }

        if ((size = BIO_get_mem_data(bio_out, &data)) > 0) {
            // OK, reset it for the next write.
            if ((r0 = BIO_reset(bio_in)) != 1) {
                return srs_error_new(ERROR_HTTPS_HANDSHAKE, "BIO_reset r0=%d", r0);
            }
            break;
        }
    }

    srs_info("https: ServerHello done");

    // Send Client Key Exchange, Change Cipher Spec, Encrypted Handshake Message
    if ((err = transport->write(data, size, NULL)) != srs_success) {
        return srs_error_wrap(err, "handshake: write data=%p, size=%d", data, size);
    }
    if ((r0 = BIO_reset(bio_out)) != 1) {
        return srs_error_new(ERROR_HTTPS_HANDSHAKE, "BIO_reset r0=%d", r0);
    }

    srs_info("https: Client done");

    // Receive New Session Ticket, Change Cipher Spec, Encrypted Handshake Message
    while (true) {
        char buf[128];
        ssize_t nn = 0;
        if ((err = transport->read(buf, sizeof(buf), &nn)) != srs_success) {
            return srs_error_wrap(err, "handshake: read");
        }

        if ((r0 = BIO_write(bio_in, buf, nn)) <= 0) {
            // TODO: 0 or -1 maybe block, use BIO_should_retry to check.
            return srs_error_new(ERROR_HTTPS_HANDSHAKE, "BIO_write r0=%d, data=%p, size=%d", r0, buf, nn);
        }

        r0 = SSL_do_handshake(ssl); r1 = SSL_get_error(ssl, r0); ERR_clear_error();
        if (r0 == 1 && r1 == SSL_ERROR_NONE) {
            break;
        }

        if (r0 != -1 || r1 != SSL_ERROR_WANT_READ) {
            return srs_error_new(ERROR_HTTPS_HANDSHAKE, "handshake r0=%d, r1=%d", r0, r1);
        }
    }

    srs_info("https: Server done");

    return err;
}
#pragma GCC diagnostic pop

srs_error_t SrsSslClient::read(void* plaintext, size_t nn_plaintext, ssize_t* nread)
{
    srs_error_t err = srs_success;

    while (true) {
        int r0 = SSL_read(ssl, plaintext, nn_plaintext); int r1 = SSL_get_error(ssl, r0); ERR_clear_error();
        int r2 = BIO_ctrl_pending(bio_in); int r3 = SSL_is_init_finished(ssl);

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
            char* cipher = new char[nn_cipher];
            SrsAutoFreeA(char, cipher);

            // Read the cipher from SSL.
            ssize_t nn = 0;
            if ((err = transport->read(cipher, nn_cipher, &nn)) != srs_success) {
                return srs_error_wrap(err, "https: read");
            }

            int r0 = BIO_write(bio_in, cipher, nn);
            if (r0 <= 0) {
                // TODO: 0 or -1 maybe block, use BIO_should_retry to check.
                return srs_error_new(ERROR_HTTPS_READ, "BIO_write r0=%d, cipher=%p, size=%d", r0, cipher, nn);
            }
            continue;
        }

        // Fail for error.
        if (r0 <= 0) {
            return srs_error_new(ERROR_HTTPS_READ, "SSL_read r0=%d, r1=%d, r2=%d, r3=%d",
                r0, r1, r2, r3);
        }
    }
}

srs_error_t SrsSslClient::write(void* plaintext, size_t nn_plaintext, ssize_t* nwrite)
{
    srs_error_t err = srs_success;

    for (char* p = (char*)plaintext; p < (char*)plaintext + nn_plaintext;) {
        int left = (int)nn_plaintext - (p - (char*)plaintext);
        int r0 = SSL_write(ssl, (const void*)p, left);
        int r1 = SSL_get_error(ssl, r0); ERR_clear_error();
        if (r0 <= 0) {
            return srs_error_new(ERROR_HTTPS_WRITE, "https: write data=%p, size=%d, r0=%d, r1=%d", p, left, r0, r1);
        }

        // Move p to the next writing position.
        p += r0;
        if (nwrite) {
            *nwrite += (ssize_t)r0;
        }

        uint8_t* data = NULL;
        int size = BIO_get_mem_data(bio_out, &data);
        if ((err = transport->write(data, size, NULL)) != srs_success) {
            return srs_error_wrap(err, "https: write data=%p, size=%d", data, size);
        }
        if ((r0 = BIO_reset(bio_out)) != 1) {
            return srs_error_new(ERROR_HTTPS_WRITE, "BIO_reset r0=%d", r0);
        }
    }

    return err;
}

SrsHttpClient::SrsHttpClient()
{
    transport = NULL;
    ssl_transport = NULL;
    kbps = new SrsNetworkKbps();
    parser = NULL;
    recv_timeout = timeout = SRS_UTIME_NO_TIMEOUT;
    port = 0;
}

SrsHttpClient::~SrsHttpClient()
{
    disconnect();

    srs_freep(kbps);
    srs_freep(parser);
}

srs_error_t SrsHttpClient::initialize(string schema, string h, int p, srs_utime_t tm)
{
    srs_error_t err = srs_success;
    
    srs_freep(parser);
    parser = new SrsHttpParser();
    
    if ((err = parser->initialize(HTTP_RESPONSE)) != srs_success) {
        return srs_error_wrap(err, "http: init parser");
    }
    
    // Always disconnect the transport.
    schema_ = schema;
    host = h;
    port = p;
    recv_timeout = timeout = tm;
    disconnect();
    
    // ep used for host in header.
    string ep = host;
    if (port > 0 && port != SRS_CONSTS_HTTP_DEFAULT_PORT) {
        ep += ":" + srs_int2str(port);
    }
    
    // Set default value for headers.
    headers["Host"] = ep;
    headers["Connection"] = "Keep-Alive";
    headers["User-Agent"] = RTMP_SIG_SRS_SERVER;
    headers["Content-Type"] = "application/json";
    
    return err;
}

SrsHttpClient* SrsHttpClient::set_header(string k, string v)
{
    headers[k] = v;
    
    return this;
}

srs_error_t SrsHttpClient::post(string path, string req, ISrsHttpMessage** ppmsg)
{
    *ppmsg = NULL;
    
    srs_error_t err = srs_success;
    
    // always set the content length.
    headers["Content-Length"] = srs_int2str(req.length());
    
    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "http: connect server");
    }
    
    if (path.size() == 0) {
        path = "/";
    }

    // TODO: FIXME: Use SrsHttpMessageWriter, never use stringstream and headers.
    // send POST request to uri
    // POST %s HTTP/1.1\r\nHost: %s\r\nContent-Length: %d\r\n\r\n%s
    std::stringstream ss;
    ss << "POST " << path << " " << "HTTP/1.1" << SRS_HTTP_CRLF;
    for (map<string, string>::iterator it = headers.begin(); it != headers.end(); ++it) {
        string key = it->first;
        string value = it->second;
        ss << key << ": " << value << SRS_HTTP_CRLF;
    }
    ss << SRS_HTTP_CRLF << req;

    std::string data = ss.str();
    if ((err = writer()->write((void*)data.c_str(), data.length(), NULL)) != srs_success) {
        // Disconnect the transport when channel error, reconnect for next operation.
        disconnect();
        return srs_error_wrap(err, "http: write");
    }
    
    ISrsHttpMessage* msg = NULL;
    if ((err = parser->parse_message(reader(), &msg)) != srs_success) {
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

srs_error_t SrsHttpClient::get(string path, string req, ISrsHttpMessage** ppmsg)
{
    *ppmsg = NULL;
    
    srs_error_t err = srs_success;
    
    // always set the content length.
    headers["Content-Length"] = srs_int2str(req.length());
    
    if ((err = connect()) != srs_success) {
        return srs_error_wrap(err, "http: connect server");
    }
    
    // send POST request to uri
    // GET %s HTTP/1.1\r\nHost: %s\r\nContent-Length: %d\r\n\r\n%s
    std::stringstream ss;
    ss << "GET " << path << " " << "HTTP/1.1" << SRS_HTTP_CRLF;
    for (map<string, string>::iterator it = headers.begin(); it != headers.end(); ++it) {
        string key = it->first;
        string value = it->second;
        ss << key << ": " << value << SRS_HTTP_CRLF;
    }
    ss << SRS_HTTP_CRLF << req;
    
    std::string data = ss.str();
    if ((err = writer()->write((void*)data.c_str(), data.length(), NULL)) != srs_success) {
        // Disconnect the transport when channel error, reconnect for next operation.
        disconnect();
        return srs_error_wrap(err, "http: write");
    }
    
    ISrsHttpMessage* msg = NULL;
    if ((err = parser->parse_message(reader(), &msg)) != srs_success) {
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
    recv_timeout = tm;
}

void SrsHttpClient::kbps_sample(const char* label, srs_utime_t age)
{
    kbps->sample();

    int sr = kbps->get_send_kbps();
    int sr30s = kbps->get_send_kbps_30s();
    int sr5m = kbps->get_send_kbps_5m();
    int rr = kbps->get_recv_kbps();
    int rr30s = kbps->get_recv_kbps_30s();
    int rr5m = kbps->get_recv_kbps_5m();

    srs_trace("<- %s time=%" PRId64 ", okbps=%d,%d,%d, ikbps=%d,%d,%d", label, srsu2ms(age), sr, sr30s, sr5m, rr, rr30s, rr5m);
}

void SrsHttpClient::disconnect()
{
    kbps->set_io(NULL, NULL);
    srs_freep(ssl_transport);
    srs_freep(transport);
}

srs_error_t SrsHttpClient::connect()
{
    srs_error_t err = srs_success;
    
    // When transport connected, ignore.
    if (transport) {
        return err;
    }
    
    transport = new SrsTcpClient(host, port, timeout);
    if ((err = transport->connect()) != srs_success) {
        disconnect();
        return srs_error_wrap(err, "http: tcp connect %s %s:%d to=%dms, rto=%dms",
            schema_.c_str(), host.c_str(), port, srsu2msi(timeout), srsu2msi(recv_timeout));
    }
    
    // Set the recv/send timeout in srs_utime_t.
    transport->set_recv_timeout(recv_timeout);
    transport->set_send_timeout(timeout);

    kbps->set_io(transport, transport);
    
    if (schema_ != "https") {
        return err;
    }

#if !defined(SRS_HTTPS)
    return srs_error_new(ERROR_HTTPS_NOT_SUPPORTED, "should configure with --https=on");
#else
    srs_assert(!ssl_transport);
    ssl_transport = new SrsSslClient(transport);

    srs_utime_t starttime = srs_update_system_time();

    if ((err = ssl_transport->handshake(host)) != srs_success) {
        disconnect();
        return srs_error_wrap(err, "http: ssl connect %s %s:%d to=%dms, rto=%dms",
            schema_.c_str(), host.c_str(), port, srsu2msi(timeout), srsu2msi(recv_timeout));
    }

    int cost = srsu2msi(srs_update_system_time() - starttime);
    srs_trace("https: connected to %s://%s:%d, cost=%dms", schema_.c_str(), host.c_str(), port, cost);

    return err;
#endif
}

ISrsStreamWriter* SrsHttpClient::writer()
{
    if (ssl_transport) {
        return ssl_transport;
    }
    return transport;
}

ISrsReader* SrsHttpClient::reader()
{
    if (ssl_transport) {
        return ssl_transport;
    }
    return transport;
}

