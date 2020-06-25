/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRS_APP_RTC_DTLS_HPP
#define SRS_APP_RTC_DTLS_HPP

#include <srs_core.hpp>

#include <string>

class SrsRequest;

#include <openssl/ssl.h>

class SrsDtlsCertificate
{
private:
    std::string fingerprint;
    bool ecdsa_mode;
    X509* dtls_cert;
    EVP_PKEY* dtls_pkey;
    EC_KEY* eckey;
public:
    SrsDtlsCertificate();
    virtual ~SrsDtlsCertificate();
public:
    // Initialize DTLS certificate.
    srs_error_t initialize();
    // dtls_cert
    X509* get_cert();
    // public key
    EVP_PKEY* get_public_key();
    // ECDSA key
    EC_KEY* get_ecdsa_key();
    // certificate fingerprint
    std::string get_fingerprint();
    // whether is ecdsa
    bool is_ecdsa();
};

// @global config object.
extern SrsDtlsCertificate* _srs_rtc_dtls_certificate;

class ISrsDtlsCallback
{
public:
    ISrsDtlsCallback();
    virtual ~ISrsDtlsCallback();
public:
    // DTLS handshake done callback.
    virtual srs_error_t on_dtls_handshake_done() = 0;
    // DTLS receive application data callback.
    virtual srs_error_t on_dtls_application_data(const char* data, const int len) = 0;
    // DTLS write dtls data.
    virtual srs_error_t write_dtls_data(void* data, int size) = 0;
};

class SrsDtls
{
private:
    SSL_CTX* dtls_ctx;
    SSL* dtls;
    BIO* bio_in;
    BIO* bio_out;

    ISrsDtlsCallback* callback;

    bool handshake_done;
public:
    SrsDtls(ISrsDtlsCallback* callback);
    virtual ~SrsDtls();
public:
    srs_error_t initialize(SrsRequest* r);  
    srs_error_t do_handshake();
    srs_error_t on_dtls(char* data, int nb_data);
    srs_error_t export_keying_material(unsigned char *out, size_t olen, const char *label, size_t llen, const unsigned char *p, size_t plen, int use_context);
private:
    SSL_CTX* build_dtls_ctx();
    srs_error_t handshake();
};

#endif
