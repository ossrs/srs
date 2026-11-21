//
// Copyright (c) 2013-2026 The SRS Authors
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

class ISrsRequest;
class ISrsAppConfig;
class ISrsRand;

class ISrsDtlsCertificate
{
public:
    ISrsDtlsCertificate();
    virtual ~ISrsDtlsCertificate();

public:
    virtual srs_error_t initialize() = 0;
    virtual X509 *get_cert() = 0;
    virtual EVP_PKEY *get_public_key() = 0;
    virtual EC_KEY *get_ecdsa_key() = 0;
    virtual std::string get_fingerprint() = 0;
    virtual bool is_ecdsa() = 0;
};

class SrsDtlsCertificate : public ISrsDtlsCertificate
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsRand *rand_;

    std::string fingerprint_;
    bool ecdsa_mode_;
    X509 *dtls_cert_;
    EVP_PKEY *dtls_pkey_;
    EC_KEY *eckey_;

public:
    SrsDtlsCertificate();
    virtual ~SrsDtlsCertificate();

public:
    srs_error_t initialize();
    X509 *get_cert();
    EVP_PKEY *get_public_key();
    EC_KEY *get_ecdsa_key();
    std::string get_fingerprint();
    bool is_ecdsa();
};

extern SrsDtlsCertificate *_srs_rtc_dtls_certificate;

// The DTLS client sends the first handshake packet.
enum SrsDtlsRole {
    SrsDtlsRoleClient,
    SrsDtlsRoleServer
};

// WebRTC ignores DTLS 1.0 and accepts only DTLS 1.2, and removes DTLS 1.0 support in M84 or later.
// TODO(https://bugs.webrtc.org/10261).
enum SrsDtlsVersion {
    SrsDtlsVersionAuto = -1,
    SrsDtlsVersion1_0,
    SrsDtlsVersion1_2
};

extern SSL_CTX *srs_build_dtls_ctx(ISrsDtlsCertificate *certificate, SrsDtlsVersion version, std::string role);

class ISrsDtlsCallback
{
public:
    ISrsDtlsCallback();
    virtual ~ISrsDtlsCallback();

public:
    virtual srs_error_t on_dtls_handshake_done() = 0;
    virtual srs_error_t on_dtls_application_data(const char *data, const int len) = 0;
    virtual srs_error_t write_dtls_data(void *data, int size) = 0;
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc) = 0;
};

enum SrsDtlsState {
    SrsDtlsStateInit,
    SrsDtlsStateClientDone,
};

class SrsDtlsImpl
{
// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    SSL_CTX *dtls_ctx_;
    SSL *dtls_;
    BIO *bio_in_;
    BIO *bio_out_;
    ISrsDtlsCallback *callback_;
    ISrsDtlsCertificate *dtls_certificate_;
    SrsDtlsVersion version_;

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    // The peer may not be done yet, so its DTLS packets still need handling after this is set.
    bool handshake_done_for_us_;
    int nn_arq_packets_;
    uint8_t last_handshake_type_;
    uint8_t last_content_type_;
    // The records OpenSSL wrote, sent after it returns, because a send may switch coroutines.
    std::vector<std::string> out_records_;

public:
    SrsDtlsImpl(ISrsDtlsCallback *callback);
    virtual ~SrsDtlsImpl();

public:
    // Called by the OpenSSL BIO callback, a C function, to queue each DTLS record it writes.
    void queue_dtls_data(void *data, int size);

public:
    virtual srs_error_t initialize(std::string version, std::string role);
    virtual srs_error_t start_active_handshake();
    virtual srs_error_t on_dtls(char *data, int nb_data);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    srs_error_t do_on_dtls(char *data, int nb_data);
    void flush_dtls_data();
    srs_error_t write_dtls_data(void *data, int size);
    void state_trace(uint8_t *data, int length, bool incoming, int r0);

public:
    srs_error_t get_srtp_key(std::string &recv_key, std::string &send_key);
    srs_error_t get_peer_fingerprint(std::string algo, std::string &fingerprint);
    void callback_by_ssl(std::string type, std::string desc);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual srs_error_t on_handshake_done() = 0;
    virtual bool is_dtls_client() = 0;
    virtual srs_error_t start_arq() = 0;
};

class SrsDtlsClientImpl : public SrsDtlsImpl, public ISrsCoroutineHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsCoroutine *trd_;
    SrsDtlsState state_;
    int arq_max_retry_;

public:
    SrsDtlsClientImpl(ISrsDtlsCallback *callback);
    virtual ~SrsDtlsClientImpl();

public:
    virtual srs_error_t initialize(std::string version, std::string role);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual srs_error_t on_handshake_done();
    virtual bool is_dtls_client();

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    srs_error_t start_arq();
    void stop_arq();

public:
    virtual srs_error_t cycle();
};

class SrsDtlsServerImpl : public SrsDtlsImpl
{
public:
    SrsDtlsServerImpl(ISrsDtlsCallback *callback);
    virtual ~SrsDtlsServerImpl();

public:
    virtual srs_error_t initialize(std::string version, std::string role);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual srs_error_t on_handshake_done();
    virtual bool is_dtls_client();
    srs_error_t start_arq();
};

class SrsDtlsEmptyImpl : public SrsDtlsImpl
{
public:
    SrsDtlsEmptyImpl();
    virtual ~SrsDtlsEmptyImpl();

public:
    virtual srs_error_t initialize(std::string version, std::string role);
    virtual srs_error_t on_dtls(char *data, int nb_data);

public:
    srs_error_t get_srtp_key(std::string &recv_key, std::string &send_key);
    void callback_by_ssl(std::string type, std::string desc);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    virtual srs_error_t on_handshake_done();
    virtual bool is_dtls_client();
    virtual srs_error_t start_arq();
};

class ISrsDtls
{
public:
    ISrsDtls();
    virtual ~ISrsDtls();

public:
    virtual srs_error_t initialize(std::string role, std::string version) = 0;
    virtual srs_error_t start_active_handshake() = 0;
    virtual srs_error_t on_dtls(char *data, int nb_data) = 0;
    virtual srs_error_t get_srtp_key(std::string &recv_key, std::string &send_key) = 0;
    virtual srs_error_t get_peer_fingerprint(std::string algo, std::string &fingerprint) = 0;
};

class SrsDtls : public ISrsDtls
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsDtlsImpl *impl_;
    ISrsDtlsCallback *callback_;

public:
    SrsDtls(ISrsDtlsCallback *callback);
    virtual ~SrsDtls();

public:
    srs_error_t initialize(std::string role, std::string version);

public:
    srs_error_t start_active_handshake();
    srs_error_t on_dtls(char *data, int nb_data);

public:
    srs_error_t get_srtp_key(std::string &recv_key, std::string &send_key);
    srs_error_t get_peer_fingerprint(std::string algo, std::string &fingerprint);
};

class ISrsSRTP
{
public:
    ISrsSRTP();
    virtual ~ISrsSRTP();

public:
    virtual srs_error_t initialize(std::string recv_key, std::string send_key) = 0;
    virtual srs_error_t protect_rtp(void *packet, int *nb_cipher) = 0;
    virtual srs_error_t protect_rtcp(void *packet, int *nb_cipher) = 0;
    virtual srs_error_t unprotect_rtp(void *packet, int *nb_plaintext) = 0;
    virtual srs_error_t unprotect_rtcp(void *packet, int *nb_plaintext) = 0;
};

class SrsSRTP : public ISrsSRTP
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srtp_t recv_ctx_;
    srtp_t send_ctx_;

public:
    SrsSRTP();
    virtual ~SrsSRTP();

public:
    srs_error_t initialize(std::string recv_key, std::string send_key);

public:
    srs_error_t protect_rtp(void *packet, int *nb_cipher);
    srs_error_t protect_rtcp(void *packet, int *nb_cipher);
    srs_error_t unprotect_rtp(void *packet, int *nb_plaintext);
    srs_error_t unprotect_rtcp(void *packet, int *nb_plaintext);
};

#endif
