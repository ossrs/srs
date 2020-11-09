//
// Copyright (c) 2013-2021 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

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
    // Callback when DTLS Alert message.
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc) = 0;
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
    // Whether the handshake is done, for us only.
    // @remark For us only, means peer maybe not done, we also need to handle the DTLS packet.
    bool handshake_done_for_us;
    // The stat for ARQ packets.
    int nn_arq_packets;
public:
    SrsDtlsImpl(ISrsDtlsCallback* callback);
    virtual ~SrsDtlsImpl();
public:
    virtual srs_error_t initialize(std::string version, std::string role);
    virtual srs_error_t start_active_handshake() = 0;
    virtual bool should_reset_timer() = 0;
    virtual srs_error_t on_dtls(char* data, int nb_data);
protected:
    srs_error_t do_on_dtls(char* data, int nb_data);
    srs_error_t do_handshake();
    void state_trace(uint8_t* data, int length, bool incoming, int r0, int r1, bool arq);
public:
    srs_error_t get_srtp_key(std::string& recv_key, std::string& send_key);
    void callback_by_ssl(std::string type, std::string desc);
protected:
    virtual srs_error_t on_final_out_data(uint8_t* data, int size) = 0;
    virtual srs_error_t on_handshake_done() = 0;
    virtual bool is_dtls_client() = 0;
public:
    srs_error_t send(const char* data, const int len);
};

class SrsDtlsClientImpl : public SrsDtlsImpl, public ISrsCoroutineHandler
{
private:
    // ARQ thread, for role active(DTLS client).
    // @note If passive(DTLS server), the ARQ is driven by DTLS client.
    SrsCoroutine* trd;
    // The DTLS-client state to drive the ARQ thread.
    SrsDtlsState state_;
    // The max ARQ retry.
    int arq_max_retry;
    // Should we reset the timer?
    // It's true when init, or in state ServerHello.
    bool reset_timer_;
public:
    SrsDtlsClientImpl(ISrsDtlsCallback* callback);
    virtual ~SrsDtlsClientImpl();
public:
    virtual srs_error_t initialize(std::string version, std::string role);
    virtual srs_error_t start_active_handshake();
    virtual bool should_reset_timer();
protected:
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
    virtual srs_error_t initialize(std::string version, std::string role);
    virtual srs_error_t start_active_handshake();
    virtual bool should_reset_timer();
protected:
    virtual srs_error_t on_final_out_data(uint8_t* data, int size);
    virtual srs_error_t on_handshake_done();
    virtual bool is_dtls_client();
};

class SrsDtlsEmptyImpl : public SrsDtlsImpl
{
public:
    SrsDtlsEmptyImpl();
    virtual ~SrsDtlsEmptyImpl();
public:
    virtual srs_error_t initialize(std::string version, std::string role);
    virtual srs_error_t start_active_handshake();
    virtual bool should_reset_timer();
    virtual srs_error_t on_dtls(char* data, int nb_data);
public:
    srs_error_t get_srtp_key(std::string& recv_key, std::string& send_key);
    void callback_by_ssl(std::string type, std::string desc);
protected:
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
public:
    // Encrypt by DTLS and sendout, for example, DataChannel to send data.
    srs_error_t send(const char* data, const int len);
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
    srs_error_t protect_rtp(void* packet, int* nb_cipher);
    srs_error_t protect_rtcp(void* packet, int* nb_cipher);
    srs_error_t unprotect_rtp(void* packet, int* nb_plaintext);
    srs_error_t unprotect_rtcp(void* packet, int* nb_plaintext);
};

#endif
