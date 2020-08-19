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
#include <vector>

#include <openssl/ssl.h>
#include <srtp2/srtp.h>

#include <srs_app_st.hpp>

class SrsRequest;

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

// The state for DTLS client.
enum SrsDtlsState {
    SrsDtlsStateInit, // Start.
    SrsDtlsStateClientHello, // Should start ARQ thread.
    SrsDtlsStateServerHello, // We are in the first ARQ state.
    SrsDtlsStateClientCertificate, // Should start ARQ thread again.
    SrsDtlsStateServerDone, // We are in the second ARQ state.
    SrsDtlsStateClientDone, // Done.
};

class SrsDtlsImpl
{
protected:
    SSL_CTX* dtls_ctx;
    SSL* dtls;
    BIO* bio_in;
    BIO* bio_out;
    ISrsDtlsCallback* callback_;
    // @remark: dtls_version_ default value is SrsDtlsVersionAuto.
    SrsDtlsVersion version_;
protected:
    // Whether the handhshake is done, for us only.
    // @remark For us only, means peer maybe not done, we also need to handle the DTLS packet.
    bool handshake_done_for_us;
    // DTLS packet cache, only last out-going packet.
    uint8_t* last_outgoing_packet_cache;
    int nn_last_outgoing_packet;
public:
    SrsDtlsImpl(ISrsDtlsCallback* callback);
    virtual ~SrsDtlsImpl();
public:
    virtual srs_error_t initialize(std::string version);
    virtual srs_error_t start_active_handshake() = 0;
    virtual srs_error_t on_dtls(char* data, int nb_data);
protected:
    srs_error_t do_on_dtls(char* data, int nb_data);
    srs_error_t do_handshake();
    void state_trace(uint8_t* data, int length, bool incoming, int r0, int r1, bool cache, bool arq);
public:
    srs_error_t get_srtp_key(std::string& recv_key, std::string& send_key);
protected:
    virtual void on_ssl_out_data(uint8_t*& data, int& size, bool& cached) = 0;
    virtual srs_error_t on_final_out_data(uint8_t* data, int size) = 0;
    virtual srs_error_t on_handshake_done() = 0;
    virtual bool is_dtls_client() = 0;
};

class SrsDtlsClientImpl : virtual public SrsDtlsImpl, virtual public ISrsCoroutineHandler
{
private:
    // ARQ thread, for role active(DTLS client).
    // @note If passive(DTLS server), the ARQ is driven by DTLS client.
    SrsCoroutine* trd;
    // The DTLS-client state to drive the ARQ thread.
    SrsDtlsState state_;
    // The timeout for ARQ.
    srs_utime_t arq_first;
    srs_utime_t arq_interval;
public:
    SrsDtlsClientImpl(ISrsDtlsCallback* callback);
    virtual ~SrsDtlsClientImpl();
public:
    virtual srs_error_t initialize(std::string version);
    virtual srs_error_t start_active_handshake();
    virtual srs_error_t on_dtls(char* data, int nb_data);
protected:
    virtual void on_ssl_out_data(uint8_t*& data, int& size, bool& cached);
    virtual srs_error_t on_final_out_data(uint8_t* data, int size);
    virtual srs_error_t on_handshake_done();
    virtual bool is_dtls_client();
private:
    srs_error_t start_arq();
    void stop_arq();
public:
    virtual srs_error_t cycle();
};

class SrsDtlsServerImpl : public SrsDtlsImpl
{
public:
    SrsDtlsServerImpl(ISrsDtlsCallback* callback);
    virtual ~SrsDtlsServerImpl();
public:
    virtual srs_error_t initialize(std::string version);
    virtual srs_error_t start_active_handshake();
protected:
    virtual void on_ssl_out_data(uint8_t*& data, int& size, bool& cached);
    virtual srs_error_t on_final_out_data(uint8_t* data, int size);
    virtual srs_error_t on_handshake_done();
    virtual bool is_dtls_client();
};

class SrsDtls
{
private:
    SrsDtlsImpl* impl;
    ISrsDtlsCallback* callback_;
public:
    SrsDtls(ISrsDtlsCallback* callback);
    virtual ~SrsDtls();
public:
    srs_error_t initialize(std::string role, std::string version);
public:
    // As DTLS client, start handshake actively, send the ClientHello packet.
    srs_error_t start_active_handshake();
    // When got DTLS packet, may handshake packets or application data.
    // @remark When we are passive(DTLS server), we start handshake when got DTLS packet.
    srs_error_t on_dtls(char* data, int nb_data);
public:
    srs_error_t get_srtp_key(std::string& recv_key, std::string& send_key);
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
