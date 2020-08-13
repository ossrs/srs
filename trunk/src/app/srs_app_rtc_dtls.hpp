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
#include <srtp2/srtp.h>

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

// @remark: play the role of DTLS_CLIENT, will send handshake
// packet first.
enum SrsDtlsRole {
    SrsDtlsRoleClient,
    SrsDtlsRoleServer
};

// @remark: DTLS_10 will all be ignored, and only DTLS1_2 will be accepted,
// DTLS_10 Support will be completely removed in M84 or later.
// TODO(https://bugs.webrtc.org/10261).
enum SrsDtlsVersion {
    SrsDtlsVersionAuto = -1,
    SrsDtlsVersion1_0,
    SrsDtlsVersion1_2
};

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

    // @remark: dtls_role_ default value is DTLS_SERVER.
    SrsDtlsRole role_;
    // @remark: dtls_version_ default value is SrsDtlsVersionAuto.
    SrsDtlsVersion version_;
public:
    SrsDtls(ISrsDtlsCallback* callback);
    virtual ~SrsDtls();
public:
    srs_error_t initialize(std::string role, std::string version);
    // As DTLS client, start handshake actively, send the ClientHello packet.
    srs_error_t start_active_handshake();
    // When got DTLS packet, may handshake packets or application data.
    // @remark When we are passive(DTLS server), we start handshake when got DTLS packet.
    srs_error_t on_dtls(char* data, int nb_data);
private:
    srs_error_t do_on_dtls(char* data, int nb_data);
    void trace(uint8_t* data, int length, bool incoming);
public:
    srs_error_t get_srtp_key(std::string& recv_key, std::string& send_key);
private:
    SSL_CTX* build_dtls_ctx();
    srs_error_t do_handshake();
};

class SrsSRTP
{
private:
    srtp_t recv_ctx_;
    srtp_t send_ctx_;
public:
    SrsSRTP();
    virtual ~SrsSRTP();
public:
    // Intialize srtp context with recv_key and send_key.
    srs_error_t initialize(std::string recv_key, std::string send_key);
public:
    // Encrypt the input plaintext to output cipher with nb_cipher bytes.
    // @remark Note that the nb_cipher is the size of input plaintext, and 
    // it also is the length of output cipher when return.
    srs_error_t protect_rtp(const char* plaintext, char* cipher, int& nb_cipher);
    srs_error_t protect_rtcp(const char* plaintext, char* cipher, int& nb_cipher);
    // Encrypt the input rtp_hdr with *len_ptr bytes.
    // @remark the input plaintext and out cipher reuse rtp_hdr.
    srs_error_t protect_rtp2(void* rtp_hdr, int* len_ptr);
    // Decrypt the input cipher to output cipher with nb_cipher bytes.
    // @remark Note that the nb_plaintext is the size of input cipher, and 
    // it also is the length of output plaintext when return.
    srs_error_t unprotect_rtp(const char* cipher, char* plaintext, int& nb_plaintext);
    srs_error_t unprotect_rtcp(const char* cipher, char* plaintext, int& nb_plaintext);
};

#endif
