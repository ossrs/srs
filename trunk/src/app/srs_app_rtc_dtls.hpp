//
// Copyright (c) 2013-2025 The SRS Authors
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

// The interface for DTLS certificate.
class ISrsDtlsCertificate
{
public:
    ISrsDtlsCertificate();
    virtual ~ISrsDtlsCertificate();

public:
    virtual srs_error_t initialize() = 0;
    virtual std::string get_fingerprint() = 0;
};

// The DTLS certificate.
class SrsDtlsCertificate : public ISrsDtlsCertificate
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::string fingerprint_;
    bool ecdsa_mode_;
    X509 *dtls_cert_;
    EVP_PKEY *dtls_pkey_;
    EC_KEY *eckey_;

public:
    SrsDtlsCertificate();
    virtual ~SrsDtlsCertificate();

public:
    // Initialize DTLS certificate.
    srs_error_t initialize();
    // dtls_cert
    X509 *get_cert();
    // public key
    EVP_PKEY *get_public_key();
    // ECDSA key
    EC_KEY *get_ecdsa_key();
    // certificate fingerprint
    std::string get_fingerprint();
    // whether is ecdsa
    bool is_ecdsa();
};

// @global config object.
extern SrsDtlsCertificate *_srs_rtc_dtls_certificate;

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
    virtual srs_error_t on_dtls_application_data(const char *data, const int len) = 0;
    // DTLS write dtls data.
    virtual srs_error_t write_dtls_data(void *data, int size) = 0;
    // Callback when DTLS Alert message.
    virtual srs_error_t on_dtls_alert(std::string type, std::string desc) = 0;
};

// The state for DTLS client.
enum SrsDtlsState {
    SrsDtlsStateInit,       // Start.
    SrsDtlsStateClientDone, // Done.
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
    // @remark: dtls_version_ default value is SrsDtlsVersionAuto.
    SrsDtlsVersion version_;

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    // Whether the handshake is done, for us only.
    // @remark For us only, means peer maybe not done, we also need to handle the DTLS packet.
    bool handshake_done_for_us_;
    // The stat for ARQ packets.
    int nn_arq_packets_;
    uint8_t last_handshake_type_;
    uint8_t last_content_type_;

public:
    SrsDtlsImpl(ISrsDtlsCallback *callback);
    virtual ~SrsDtlsImpl();

public:
    // Internal API for sending DTLS packets.
    srs_error_t write_dtls_data(void *data, int size);

public:
    virtual srs_error_t initialize(std::string version, std::string role);
    virtual srs_error_t start_active_handshake();
    virtual srs_error_t on_dtls(char *data, int nb_data);

// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    srs_error_t do_on_dtls(char *data, int nb_data);
    void state_trace(uint8_t *data, int length, bool incoming, int r0);

public:
    srs_error_t get_srtp_key(std::string &recv_key, std::string &send_key);
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
    // ARQ thread, for role active(DTLS client).
    // @note If passive(DTLS server), the ARQ is driven by DTLS client.
    ISrsCoroutine *trd_;
    // The DTLS-client state to drive the ARQ thread.
    SrsDtlsState state_;
    // The max ARQ retry.
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

// The interface for DTLS.
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
};

// The DTLS transport.
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
    // Start DTLS handshake mechanism.
    srs_error_t start_active_handshake();
    // When got DTLS packet, may handshake packets or application data.
    // @remark When we are passive(DTLS server), we start handshake when got DTLS packet.
    srs_error_t on_dtls(char *data, int nb_data);

public:
    srs_error_t get_srtp_key(std::string &recv_key, std::string &send_key);
};

// The interface for SRTP.
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

// The SRTP transport.
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
    // Intialize srtp context with recv_key and send_key.
    srs_error_t initialize(std::string recv_key, std::string send_key);

public:
    srs_error_t protect_rtp(void *packet, int *nb_cipher);
    srs_error_t protect_rtcp(void *packet, int *nb_cipher);
    srs_error_t unprotect_rtp(void *packet, int *nb_plaintext);
    srs_error_t unprotect_rtcp(void *packet, int *nb_plaintext);
};

#endif
