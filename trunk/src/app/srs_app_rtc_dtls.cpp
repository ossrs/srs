//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_rtc_dtls.hpp>

using namespace std;

#include <string.h>

#include <srs_app_config.hpp>
#include <srs_app_log.hpp>
#include <srs_app_rtc_sped.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_utility.hpp>

#include <openssl/err.h>
#include <openssl/rsa.h>
#include <openssl/ssl.h>
#include <srtp2/srtp.h>

// Larger DTLS fragments made the handshake fail.
// @see https://github.com/ossrs/srs/issues/2415
const int DTLS_FRAGMENT_MAX_SIZE = 1200;

// Defined in HTTP/HTTPS client.
extern int srs_verify_callback(int preverify_ok, X509_STORE_CTX *ctx);

// The certificate digest in the format of the SDP a=fingerprint, such as AB:CD:01, see RFC 8122 section 5.
string srs_dtls_fingerprint(X509 *cert, const EVP_MD *md)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int n = 0;
    if (X509_digest(cert, md, digest, &n) != 1) {
        return "";
    }

    string fingerprint;
    for (unsigned int i = 0; i < n; i++) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02X", digest[i]);
        if (i > 0) {
            fingerprint += ":";
        }
        fingerprint += hex;
    }
    return fingerprint;
}

// The hash functions of RFC 8122 section 5, except md2 and md5, which it says not to use.
const EVP_MD *srs_dtls_fingerprint_md(string algo)
{
    if (!strcasecmp(algo.c_str(), "sha-1")) {
        return EVP_sha1();
    } else if (!strcasecmp(algo.c_str(), "sha-224")) {
        return EVP_sha224();
    } else if (!strcasecmp(algo.c_str(), "sha-256")) {
        return EVP_sha256();
    } else if (!strcasecmp(algo.c_str(), "sha-384")) {
        return EVP_sha384();
    } else if (!strcasecmp(algo.c_str(), "sha-512")) {
        return EVP_sha512();
    }
    return NULL;
}

// @see https://www.openssl.org/docs/man1.1.1/man3/DTLS_set_timer_cb.html
//
// The session times out in 30s by default, which stops the DTLS ARQ.
unsigned int dtls_timer_cb(SSL *dtls, unsigned int previous_us)
{
    SrsDtlsImpl *dtls_impl = (SrsDtlsImpl *)SSL_get_ex_data(dtls, 0);
    srs_assert(dtls_impl);

    // While SPED is on, it resends the flight in the STUN messages until the peer ACKs it, so the timer waits its
    // longest, as draft-hancke-webrtc-sped says DTLS timers should be off: a retransmission on this timer would
    // replace the flight SPED resends with a copy in more datagrams, which the peer needs all of.
    //
    // Not for a DTLS 1.3 server: a client that ACKed part of the flight waits for the server's timer
    // to resend the rest (RFC 9147 section 7.2), and so does a client that SPED says has every datagram,
    // since BoringSSL drops encrypted records that come before the ServerHello.
    const unsigned int max_us = 30 * 1000 * 1000;
    SrsSpedController *sped = dtls_impl->sped();
    if (sped && sped->is_active() && !dtls_impl->is_dtls13_server()) {
        return max_us;
    }

    // The previous_us can be 0.
    unsigned int timeout_us = previous_us * 2;

    timeout_us = srs_max(timeout_us, 50 * 1000);
    timeout_us = srs_min(timeout_us, max_us);
    srs_info("DTLS: ARQ timer cb timeout=%ums, previous=%ums", timeout_us / 1000, previous_us / 1000);

    return timeout_us;
}

void ssl_on_info(const SSL *dtls, int where, int ret)
{
    SrsDtlsImpl *dtls_impl = (SrsDtlsImpl *)SSL_get_ex_data(dtls, 0);
    srs_assert(dtls_impl);

    const char *method;
    int w = where & ~SSL_ST_MASK;
    if (w & SSL_ST_CONNECT) {
        method = "SSL_connect";
    } else if (w & SSL_ST_ACCEPT) {
        method = "SSL_accept";
    } else {
        method = "undefined";
    }

    int r1 = SSL_get_error(dtls, ret);
    ERR_clear_error();
    if (where & SSL_CB_LOOP) {
        srs_info("DTLS: method=%s state=%s(%s), where=%d, ret=%d, r1=%d", method, SSL_state_string(dtls),
                 SSL_state_string_long(dtls), where, ret, r1);
    } else if (where & SSL_CB_ALERT) {
        method = (where & SSL_CB_READ) ? "read" : "write";

        // @see https://www.openssl.org/docs/man1.0.2/man3/SSL_alert_type_string_long.html
        string alert_type = SSL_alert_type_string_long(ret);
        string alert_desc = SSL_alert_desc_string(ret);

        if (alert_type == "warning" && alert_desc == "CN") {
            srs_warn("DTLS: SSL3 alert method=%s type=%s, desc=%s(%s), where=%d, ret=%d, r1=%d", method, alert_type.c_str(),
                     alert_desc.c_str(), SSL_alert_desc_string_long(ret), where, ret, r1);
        } else {
            srs_error("DTLS: SSL3 alert method=%s type=%s, desc=%s(%s), where=%d, ret=%d, r1=%d", method, alert_type.c_str(),
                      alert_desc.c_str(), SSL_alert_desc_string_long(ret), where, ret, r1);
        }

        // An alert may mean the peer closed the media connection.
        dtls_impl->callback_by_ssl(alert_type, alert_desc);
    } else if (where & SSL_CB_EXIT) {
        if (ret == 0) {
            srs_warn("DTLS: Fail method=%s state=%s(%s), where=%d, ret=%d, r1=%d", method, SSL_state_string(dtls),
                     SSL_state_string_long(dtls), where, ret, r1);
        } else if (ret < 0) {
            if (r1 != SSL_ERROR_NONE && r1 != SSL_ERROR_WANT_READ && r1 != SSL_ERROR_WANT_WRITE) {
                srs_error("DTLS: Error method=%s state=%s(%s), where=%d, ret=%d, r1=%d", method, SSL_state_string(dtls),
                          SSL_state_string_long(dtls), where, ret, r1);
            } else {
                srs_info("DTLS: Error method=%s state=%s(%s), where=%d, ret=%d, r1=%d", method, SSL_state_string(dtls),
                         SSL_state_string_long(dtls), where, ret, r1);
            }
        }
    }
}

bool srs_dtls_version13_supported()
{
#if OPENSSL_VERSION_NUMBER >= 0x40100000L // v4.1
    return true;
#else
    return false;
#endif
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
SSL_CTX *srs_build_dtls_ctx(ISrsDtlsCertificate *certificate, SrsDtlsVersion version, std::string role)
{
    SSL_CTX *dtls_ctx;
#if OPENSSL_VERSION_NUMBER < 0x10002000L // v1.0.2
    dtls_ctx = SSL_CTX_new(DTLSv1_method());
#elif OPENSSL_VERSION_NUMBER >= 0x30000000L // v3.0
    // OpenSSL 3 deprecates the version-specific methods and OpenSSL 4 has none, so pin the version on the flexible ones.
    // The newest version SRS takes is DTLS 1.3 from OpenSSL 4.1, else DTLS 1.2.
    int newest = DTLS1_2_VERSION;
    int pinned = 0;
    if (version == SrsDtlsVersion1_0) {
        pinned = DTLS1_VERSION;
    } else if (version == SrsDtlsVersion1_2) {
        pinned = DTLS1_2_VERSION;
    }
#if OPENSSL_VERSION_NUMBER >= 0x40100000L // v4.1
    newest = DTLS1_3_VERSION;
    if (version == SrsDtlsVersion1_3) {
        pinned = DTLS1_3_VERSION;
    }
#endif
    if (pinned) {
        if (role == "active") {
            dtls_ctx = SSL_CTX_new(DTLS_client_method());
        } else {
            dtls_ctx = SSL_CTX_new(DTLS_server_method());
        }
        SSL_CTX_set_min_proto_version(dtls_ctx, pinned);
        SSL_CTX_set_max_proto_version(dtls_ctx, pinned);
    } else {
        // The version-flexible DTLS method, up to the newest version SRS takes.
        dtls_ctx = SSL_CTX_new(DTLS_method());
        SSL_CTX_set_max_proto_version(dtls_ctx, newest);
    }
#else
    if (version == SrsDtlsVersion1_0) {
        if (role == "active") {
            dtls_ctx = SSL_CTX_new(DTLSv1_client_method());
        } else {
            dtls_ctx = SSL_CTX_new(DTLSv1_server_method());
        }
    } else if (version == SrsDtlsVersion1_2) {
        if (role == "active") {
            dtls_ctx = SSL_CTX_new(DTLSv1_2_client_method());
        } else {
            dtls_ctx = SSL_CTX_new(DTLSv1_2_server_method());
        }
    } else {
        // The version-flexible DTLS method.
        dtls_ctx = SSL_CTX_new(DTLS_method());
    }
#endif

    // "DEFAULT" would mean "ALL:!EXPORT:!LOW:!aNULL:!eNULL:!SSLv2".
    // @see https://www.openssl.org/docs/man1.0.2/man1/ciphers.html
    const char *ciphers = "ALL";
#if OPENSSL_VERSION_NUMBER >= 0x30000000L // v3.0
    // OpenSSL 3 allows DTLS 1.0 only at security level 0, so an explicit dtls1.0 would fail every handshake.
    // Level 0 also drops the level 1 suite checks, so the list leaves out what they refuse.
    if (version == SrsDtlsVersion1_0) {
        SSL_CTX_set_security_level(dtls_ctx, 0);
        ciphers = "ALL:!aNULL:!eNULL:!LOW:!MD5";
    }
#endif

    if (certificate->is_ecdsa()) { // By ECDSA, https://stackoverflow.com/a/6006898
#if OPENSSL_VERSION_NUMBER >= 0x40100000L // v4.1
        // The P curves list below would also limit the DTLS 1.3 key_share. Browsers share X25519MLKEM768 and
        // X25519, so every handshake would get a HelloRetryRequest and cost a round trip. OpenSSL's default
        // groups take both, and still have the P curves for older peers.
#elif OPENSSL_VERSION_NUMBER >= 0x10002000L // v1.0.2
        // @see https://www.openssl.org/docs/man1.0.2/man3/SSL_CTX_set1_curves_list.html
        SSL_CTX_set1_curves_list(dtls_ctx, "P-521:P-384:P-256");
#endif

        // OpenSSL before 1.1 needs the ECDH set by hand.
        // @see https://stackoverrun.com/cn/q/10791887
#if OPENSSL_VERSION_NUMBER < 0x10100000L // v1.1.x
#if OPENSSL_VERSION_NUMBER < 0x10002000L // v1.0.2
        EC_KEY *eckey = EVP_PKEY_get1_EC_KEY(certificate->get_public_key());
        SSL_CTX_set_tmp_ecdh(dtls_ctx, eckey);
        EC_KEY_free(eckey);
#else
        SSL_CTX_set_ecdh_auto(dtls_ctx, 1);
#endif
#endif
    }

    if (true) {
        srs_assert(SSL_CTX_set_cipher_list(dtls_ctx, ciphers) == 1);

        srs_assert(SSL_CTX_use_certificate(dtls_ctx, certificate->get_cert()) == 1);
        srs_assert(SSL_CTX_use_PrivateKey(dtls_ctx, certificate->get_public_key()) == 1);

        // Server will send Certificate Request.
        // @see https://www.openssl.org/docs/man1.0.2/man3/SSL_CTX_set_verify.html
        // TODO: FIXME: Config it, default to off to make the packet smaller.
        SSL_CTX_set_verify(dtls_ctx, SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE, srs_verify_callback);
        // The depth count is "level 0:peer certificate", "level 1: CA certificate",
        // "level 2: higher level CA certificate", and so on.
        // @see https://www.openssl.org/docs/man1.0.2/man3/SSL_CTX_set_verify.html
        SSL_CTX_set_verify_depth(dtls_ctx, 4);

        // @see https://www.openssl.org/docs/man1.0.2/man3/SSL_CTX_set_read_ahead.html
        SSL_CTX_set_read_ahead(dtls_ctx, 1);

        // TODO: Maybe we can use SRTP-GCM in future.
        // @see https://bugs.chromium.org/p/chromium/issues/detail?id=713701
        // @see https://groups.google.com/forum/#!topic/discuss-webrtc/PvCbWSetVAQ
        // @remark Only support SRTP_AES128_CM_SHA1_80, please read ssl/d1_srtp.c
        srs_assert(SSL_CTX_set_tlsext_use_srtp(dtls_ctx, "SRTP_AES128_CM_SHA1_80") == 0);
    }

    return dtls_ctx;
}
#pragma GCC diagnostic pop

ISrsDtlsCertificate::ISrsDtlsCertificate()
{
}

ISrsDtlsCertificate::~ISrsDtlsCertificate()
{
}

SrsDtlsCertificate::SrsDtlsCertificate()
{
    config_ = _srs_config;
    rand_ = new SrsRand();

    ecdsa_mode_ = true;
    dtls_cert_ = NULL;
    dtls_pkey_ = NULL;
}

SrsDtlsCertificate::~SrsDtlsCertificate()
{
    if (dtls_pkey_) {
        EVP_PKEY_free(dtls_pkey_);
    }

    if (dtls_cert_) {
        X509_free(dtls_cert_);
    }

    srs_freep(rand_);
    config_ = NULL;
}

srs_error_t SrsDtlsCertificate::initialize()
{
    srs_error_t err = srs_success;

    if (dtls_cert_) {
        return err;
    }

    ecdsa_mode_ = config_->get_rtc_server_ecdsa();

#if OPENSSL_VERSION_NUMBER >= 0x30000000L // v3.0
    if (ecdsa_mode_) { // By ECDSA, https://stackoverflow.com/a/6006898
        dtls_pkey_ = EVP_EC_gen("P-256");
    } else {
        // OpenSSL 3.2+ defaults to security level 2, which refuses an RSA key below 2048 bits.
        dtls_pkey_ = EVP_RSA_gen(2048);
    }
    srs_assert(dtls_pkey_);
#else
    dtls_pkey_ = EVP_PKEY_new();
    srs_assert(dtls_pkey_);
    if (!ecdsa_mode_) {
        RSA *rsa = RSA_new();
        srs_assert(rsa);

        BIGNUM *exponent = BN_new();
        srs_assert(exponent);
        BN_set_word(exponent, RSA_F4);

        // @see https://www.openssl.org/docs/man1.0.2/man3/RSA_generate_key_ex.html
        RSA_generate_key_ex(rsa, 1024, exponent, NULL);

        // @see https://www.openssl.org/docs/man1.1.0/man3/EVP_PKEY_type.html
        srs_assert(EVP_PKEY_set1_RSA(dtls_pkey_, rsa) == 1);

        RSA_free(rsa);
        BN_free(exponent);
    }
    if (ecdsa_mode_) { // By ECDSA, https://stackoverflow.com/a/6006898
        EC_KEY *eckey = EC_KEY_new();
        srs_assert(eckey);

        // Should use the curves in ClientHello.supported_groups
        // For example:
        //      Supported Group: x25519 (0x001d)
        //      Supported Group: secp256r1 (0x0017)
        //      Supported Group: secp384r1 (0x0018)
        // @remark The curve NID_secp256k1 is not secp256r1, k1 != r1.
        // TODO: FIXME: Parse ClientHello and choose the curve.
        // Note that secp256r1 in openssl is called NID_X9_62_prime256v1, not NID_secp256k1
        // @see https://stackoverflow.com/questions/41950056/openssl1-1-0-b-is-not-support-secp256r1openssl-ecparam-list-curves
        EC_GROUP *ecgroup = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
        // EC_GROUP* ecgroup = EC_GROUP_new_by_curve_name(NID_secp384r1);
        srs_assert(ecgroup);
#if OPENSSL_VERSION_NUMBER < 0x10100000L // v1.1.x
        // OpenSSL 1.0 needs the group parameters set, or the certificate does not work.
        // @see https://github.com/monero-project/monero/blob/master/contrib/epee/src/net_ssl.cpp#L225
        EC_GROUP_set_asn1_flag(ecgroup, OPENSSL_EC_NAMED_CURVE);
#endif

        srs_assert(EC_KEY_set_group(eckey, ecgroup) == 1);
        srs_assert(EC_KEY_generate_key(eckey) == 1);

        // @see https://www.openssl.org/docs/man1.1.0/man3/EVP_PKEY_type.html
        srs_assert(EVP_PKEY_set1_EC_KEY(dtls_pkey_, eckey) == 1);

        EC_KEY_free(eckey);
        EC_GROUP_free(ecgroup);
    }
#endif

    // TODO: Support ECDSA certificate.
    dtls_cert_ = X509_new();
    srs_assert(dtls_cert_);
    if (true) {
        X509_NAME *subject = X509_NAME_new();
        srs_assert(subject);

        int serial = (int)rand_->integer();
        ASN1_INTEGER_set(X509_get_serialNumber(dtls_cert_), serial);

        const std::string &aor = RTMP_SIG_SRS_DOMAIN;
        X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC, (unsigned char *)aor.data(), aor.size(), -1, 0);

        X509_set_issuer_name(dtls_cert_, subject);
        X509_set_subject_name(dtls_cert_, subject);

        int expire_day = 365;
        const long cert_duration = 60 * 60 * 24 * expire_day;

#if OPENSSL_VERSION_NUMBER >= 0x30000000L // v3.0
        X509_gmtime_adj(X509_getm_notBefore(dtls_cert_), 0);
        X509_gmtime_adj(X509_getm_notAfter(dtls_cert_), cert_duration);
#else
        X509_gmtime_adj(X509_get_notBefore(dtls_cert_), 0);
        X509_gmtime_adj(X509_get_notAfter(dtls_cert_), cert_duration);
#endif

        X509_set_version(dtls_cert_, 2);
        srs_assert(X509_set_pubkey(dtls_cert_, dtls_pkey_) == 1);
        srs_assert(X509_sign(dtls_cert_, dtls_pkey_, EVP_sha1()) != 0);

        X509_NAME_free(subject);
    }

    fingerprint_ = srs_dtls_fingerprint(dtls_cert_, EVP_sha256());
    srs_trace("fingerprint=%s", fingerprint_.c_str());

    return err;
}

X509 *SrsDtlsCertificate::get_cert()
{
    return dtls_cert_;
}

EVP_PKEY *SrsDtlsCertificate::get_public_key()
{
    return dtls_pkey_;
}

std::string SrsDtlsCertificate::get_fingerprint()
{
    return fingerprint_;
}

bool SrsDtlsCertificate::is_ecdsa()
{
    return ecdsa_mode_;
}

ISrsDtlsCallback::ISrsDtlsCallback()
{
}

ISrsDtlsCallback::~ISrsDtlsCallback()
{
}

SrsDtlsImpl::SrsDtlsImpl(ISrsDtlsCallback *callback)
{
    dtls_ctx_ = NULL;
    dtls_ = NULL;
    bio_in_ = NULL;
    bio_out_ = NULL;

    callback_ = callback;
    handshake_done_for_us_ = false;
    nn_arq_packets_ = 0;
    last_handshake_type_ = 0;
    last_content_type_ = 0;
    sped_ = NULL;
    retransmitting_ = false;

    version_ = SrsDtlsVersionAuto;

    dtls_certificate_ = _srs_rtc_dtls_certificate;
}

SrsDtlsImpl::~SrsDtlsImpl()
{
    if (!handshake_done_for_us_) {
        srs_warn2(TAG_DTLS_HANG, "DTLS: Hang, done=%u, version=%d, arq=%u", handshake_done_for_us_,
                  version_, nn_arq_packets_);
    }

    if (dtls_ctx_) {
        SSL_CTX_free(dtls_ctx_);
        dtls_ctx_ = NULL;
    }

    if (dtls_) {
        // SSL_free also frees bio_in_ and bio_out_.
        SSL_free(dtls_);
        dtls_ = NULL;
    }

    srs_freep(sped_);
    dtls_certificate_ = NULL;
}

#if OPENSSL_VERSION_NUMBER >= 0x30000000L // v3.0
long srs_dtls_bio_out_callback(BIO *bio, int cmd, const char *argp, size_t len, int argi, long argl, int ret, size_t *processed)
{
    long r0 = (BIO_CB_RETURN & cmd) ? ret : 1;
    if (cmd == BIO_CB_WRITE && argp && len > 0) {
        SrsDtlsImpl *dtls = (SrsDtlsImpl *)BIO_get_callback_arg(bio);
        dtls->queue_dtls_data((void *)argp, (int)len);
    }
    return r0;
}
#else
long srs_dtls_bio_out_callback(BIO *bio, int cmd, const char *argp, int argi, long argl, long ret)
{
    long r0 = (BIO_CB_RETURN & cmd) ? ret : 1;
    if (cmd == BIO_CB_WRITE && argp && argi > 0) {
        SrsDtlsImpl *dtls = (SrsDtlsImpl *)BIO_get_callback_arg(bio);
        dtls->queue_dtls_data((void *)argp, argi);
    }
    return r0;
}
#endif

void SrsDtlsImpl::queue_dtls_data(void *data, int size)
{
    // Never send from inside OpenSSL: the send may switch to a coroutine that calls OpenSSL on the same SSL, or ends
    // the handshake and frees the buffer of this record, so copy it and send it once OpenSSL returns.
    out_records_.push_back(std::string((char *)data, size));

    if (sped_ && retransmitting_) {
        sped_->capture_retransmission((const char *)data, size);
    } else if (sped_) {
        sped_->capture((const char *)data, size);
    }
}

void SrsDtlsImpl::flush_dtls_data()
{
    // OpenSSL writes a whole flight in one call, so what it wrote before this flush is one flight.
    if (sped_) {
        sped_->flush();
    }

    // Take each record before its send, because another coroutine may flush while the send switches.
    while (!out_records_.empty()) {
        std::string record = out_records_.front();
        out_records_.erase(out_records_.begin());

        srs_error_t err = write_dtls_data((void *)record.data(), (int)record.size());
        if (err != srs_success) {
            srs_warn("ignore err %s", srs_error_desc(err).c_str());
        }
        srs_freep(err);
    }
}

srs_error_t SrsDtlsImpl::write_dtls_data(void *data, int size)
{
    srs_error_t err = srs_success;

    if (size > 0 && (err = callback_->write_dtls_data(data, size)) != srs_success) {
        return srs_error_wrap(err, "dtls send size=%u, data=[%s]", size,
                              srs_strings_dumps_hex((char *)data, size, 32).c_str());
    }

    // change_cipher_spec(20), alert(21), handshake(22), application_data(23)
    // @see https://tools.ietf.org/html/rfc2246#section-6.2.1
    uint8_t content_type = size >= 1 ? ((uint8_t *)data)[0] : 0;
    uint8_t handshake_type = size >= 14 ? ((uint8_t *)data)[13] : 0;
    if (content_type && handshake_type && last_content_type_ == content_type && last_handshake_type_ == handshake_type) {
        nn_arq_packets_++;
    }
    last_content_type_ = content_type;
    last_handshake_type_ = handshake_type;

    state_trace((uint8_t *)data, size, false, 0);

    return err;
}

srs_error_t SrsDtlsImpl::initialize(std::string version, std::string role)
{
    srs_error_t err = srs_success;

    if (version == "dtls1.0") {
        version_ = SrsDtlsVersion1_0;
    } else if (version == "dtls1.2") {
        version_ = SrsDtlsVersion1_2;
    } else if (version == "dtls1.3") {
        version_ = SrsDtlsVersion1_3;
    } else {
        version_ = SrsDtlsVersionAuto;
    }

    dtls_ctx_ = srs_build_dtls_ctx(dtls_certificate_, version_, role);

    if ((dtls_ = SSL_new(dtls_ctx_)) == NULL) {
        return srs_error_new(ERROR_OpenSslCreateSSL, "SSL_new dtls");
    }

    SSL_set_ex_data(dtls_, 0, this);
    SSL_set_info_callback(dtls_, ssl_on_info);

    // OpenSSL splits each handshake packet to fit this MTU.
    // @see https://stackoverflow.com/questions/62413602/openssl-server-packets-get-fragmented-into-270-bytes-per-packet
    SSL_set_options(dtls_, SSL_OP_NO_QUERY_MTU);
    SSL_set_mtu(dtls_, DTLS_FRAGMENT_MAX_SIZE);
    // See https://github.com/versatica/mediasoup/pull/217
    DTLS_set_link_mtu(dtls_, DTLS_FRAGMENT_MAX_SIZE);

    // @see https://linux.die.net/man/3/openssl_version_number
#if OPENSSL_VERSION_NUMBER >= 0x1010102fL // 1.1.1b
    DTLS_set_timer_cb(dtls_, dtls_timer_cb);
#endif

    if ((bio_in_ = BIO_new(BIO_s_mem())) == NULL) {
        return srs_error_new(ERROR_OpenSslBIONew, "BIO_new in");
    }

    if ((bio_out_ = BIO_new(BIO_s_mem())) == NULL) {
        BIO_free(bio_in_);
        return srs_error_new(ERROR_OpenSslBIONew, "BIO_new out");
    }

    // Take each packet in the BIO callback, not with BIO_get_mem_data, which returns all the bytes to send at once.
    // For example, with MTU 1200 and two DTLS packets to send:
    //      ServerHello, 95bytes.
    //      Certificate, 1105+143=1248bytes.
    // BIO_get_mem_data returns 95+1248=1343bytes, larger than the MTU.
    // The callback gets two UDP packets:
    //      ServerHello+Certificate(Fragment) = 95+1105=1200bytes.
    //      Certificate(Fragment) = 143bytes.
    // A real handshake has more packets, such as ServerKeyExchange, CertificateRequest and ServerHelloDone.
#if OPENSSL_VERSION_NUMBER >= 0x30000000L // v3.0
    BIO_set_callback_ex(bio_out_, srs_dtls_bio_out_callback);
#else
    BIO_set_callback(bio_out_, srs_dtls_bio_out_callback);
#endif
    BIO_set_callback_arg(bio_out_, (char *)this);

    SSL_set_bio(dtls_, bio_in_, bio_out_);

    return err;
}

srs_error_t SrsDtlsImpl::start_active_handshake()
{
    srs_error_t err = srs_success;

    // During initialization, we only need to call SSL_do_handshake once because SSL_read consumes
    // the handshake message if the handshake is incomplete.
    // To simplify maintenance, we initiate the handshake for both the DTLS server and client after
    // sending out the ICE response in the start_active_handshake function. It's worth noting that
    // although the DTLS server may receive the ClientHello immediately after sending out the ICE
    // response, this shouldn't be an issue as the handshake function is called before any DTLS
    // packets are received.
    int r0 = SSL_do_handshake(dtls_);
    int r1 = SSL_get_error(dtls_, r0);
    ERR_clear_error();

    // Send what OpenSSL wrote here: not before SSL_get_error and ERR_clear_error, because a send may switch to a
    // coroutine whose OpenSSL call changes this thread's error queue; and not after the check below, because on a
    // fatal error the records hold the alert the peer must get.
    flush_dtls_data();

    // Fatal SSL error, for example, no available suite when peer is DTLS 1.0 while we are DTLS 1.2.
    if (r0 < 0 && (r1 != SSL_ERROR_NONE && r1 != SSL_ERROR_WANT_READ && r1 != SSL_ERROR_WANT_WRITE)) {
        return srs_error_new(ERROR_RTC_DTLS, "handshake r0=%d, r1=%d", r0, r1);
    }

    if ((err = start_arq()) != srs_success) {
        return srs_error_wrap(err, "start arq");
    }

    return err;
}

srs_error_t SrsDtlsImpl::on_dtls(char *data, int nb_data)
{
    srs_error_t err = srs_success;

    if (sped_) {
        sped_->on_dtls_received(data, nb_data);
    }

    if ((err = do_on_dtls(data, nb_data)) != srs_success) {
        return srs_error_wrap(err, "on_dtls size=%u, data=[%s]", nb_data,
                              srs_strings_dumps_hex(data, nb_data, 32).c_str());
    }

    return err;
}

void SrsDtlsImpl::enable_sped()
{
    if (!sped_) {
        sped_ = new SrsSpedController();
    }

    // SPED resends a datagram only if it fits a STUN message, so write each one small enough.
    SSL_set_mtu(dtls_, kSpedDtlsMtu);
    DTLS_set_link_mtu(dtls_, kSpedDtlsMtu);
}

SrsSpedController *SrsDtlsImpl::sped()
{
    return sped_;
}

srs_error_t SrsDtlsImpl::on_embedded_dtls(char *data, int nb_data)
{
    srs_error_t err = srs_success;

    if ((err = do_on_dtls(data, nb_data)) != srs_success) {
        return srs_error_wrap(err, "on_embedded_dtls size=%u, data=[%s]", nb_data,
                              srs_strings_dumps_hex(data, nb_data, 32).c_str());
    }

    return err;
}

bool SrsDtlsImpl::is_dtls13_server()
{
#if OPENSSL_VERSION_NUMBER >= 0x40100000L // v4.1
    return !is_dtls_client() && SSL_version(dtls_) == DTLS1_3_VERSION;
#else
    return false;
#endif
}

srs_error_t SrsDtlsImpl::do_on_dtls(char *data, int nb_data)
{
    srs_error_t err = srs_success;

    // After the handshake is done for us, the peer may resend because our response was lost, or send application data.
    if (handshake_done_for_us_) {
        srs_trace("DTLS: After done, got %d bytes", nb_data);
    }

    // SSL_read resends the last flight first when the timer expired, and what it writes then would go out
    // with the records of the read, as one flight to SPED.
    if (sped_ && (err = handle_timeout()) != srs_success) {
        return srs_error_wrap(err, "timeout");
    }

    int r0 = 0;
    if ((r0 = BIO_write(bio_in_, data, nb_data)) <= 0) {
        // TODO: 0 or -1 maybe block, use BIO_should_retry to check.
        return srs_error_new(ERROR_OpenSslBIOWrite, "BIO_write r0=%d", r0);
    }
    state_trace((uint8_t *)data, nb_data, true, r0);

    // The handshake MTU is 1200, so this buffer is large enough.
    // TODO: FIXME: DTLS application messages, such as DataChannel messages, may exceed 1500 bytes, but they should be
    //  fragmented. This fragmentation should be done at the application level. However, I'm not certain about this
    //  and will leave it to the developer who is responsible for developing the DataChannel.
    char buf[kRtpPacketSize];
    r0 = SSL_read(dtls_, buf, sizeof(buf));
    int r1 = SSL_get_error(dtls_, r0);
    ERR_clear_error();

    // Send what OpenSSL wrote here: not before SSL_get_error and ERR_clear_error, because a send may switch to a
    // coroutine whose OpenSSL call changes this thread's error queue; and not after the check below, because on a
    // fatal error the records hold the alert the peer must get.
    flush_dtls_data();

    if (r0 <= 0) {
        if (r1 != SSL_ERROR_WANT_READ && r1 != SSL_ERROR_WANT_WRITE && r1 != SSL_ERROR_ZERO_RETURN) {
            return srs_error_new(ERROR_RTC_DTLS, "DTLS: read r0=%d, r1=%d, done=%d", r0, r1, handshake_done_for_us_);
        }
    } else {
        srs_trace("DTLS: read r0=%d, r1=%d, padding=%d, done=%d, data=[%s]",
                  r0, r1, BIO_ctrl_pending(bio_in_), handshake_done_for_us_, srs_strings_dumps_hex(buf, r0, 32).c_str());

        if ((err = callback_->on_dtls_application_data(buf, r0)) != srs_success) {
            return srs_error_wrap(err, "on DTLS data, done=%d, r1=%d, size=%u, data=[%s]", handshake_done_for_us_,
                                  r1, r0, srs_strings_dumps_hex(buf, r0, 32).c_str());
        }
    }

    if (!handshake_done_for_us_ && SSL_is_init_finished(dtls_) == 1) {
        handshake_done_for_us_ = true;

        // The server keeps its last flight until the client ACKs it: CCS and Finished in DTLS 1.2, and in
        // DTLS 1.3 the ACK record of the client's Finished, which the client waits for. The client's last
        // flight needs no resend once it is done: in DTLS 1.2 the server's Finished answers it, and in
        // DTLS 1.3 the client is done only on the server's ACK of it. A DTLS 1.2 server is done when it sends
        // its last flight, before the client has it, while a DTLS 1.3 server is done only on the client's
        // Finished, which the client sends once it is done.
        if (sped_) {
            sped_->on_handshake_done(is_dtls_client(), !is_dtls_client() && !is_dtls13_server());
        }

        if (((err = on_handshake_done()) != srs_success)) {
            return srs_error_wrap(err, "done");
        }
    }

    if ((err = on_dtls_read()) != srs_success) {
        return srs_error_wrap(err, "read");
    }

    return err;
}

srs_error_t SrsDtlsImpl::on_dtls_read()
{
    return srs_success;
}

srs_error_t SrsDtlsImpl::handle_timeout()
{
    srs_error_t err = srs_success;

    // DTLSv1_handle_timeout returns 0 when no timer expired, 1 after it resends the previous flight, and -1
    // after too many timeouts without progress or on an error.
    retransmitting_ = true;
    int r0 = DTLSv1_handle_timeout(dtls_);
    int r1 = SSL_get_error(dtls_, r0);
    ERR_clear_error();
    retransmitting_ = false;

    // Send what OpenSSL wrote here: not before SSL_get_error and ERR_clear_error, because a send may switch to a
    // coroutine whose OpenSSL call changes this thread's error queue; and not after the check below, because on
    // an error the records may hold an alert the peer must get.
    flush_dtls_data();

    if (r0 != 0 && r0 != 1) {
        return srs_error_new(ERROR_RTC_DTLS, "ARQ r0=%d, r1=%d", r0, r1);
    }

    return err;
}

srs_error_t SrsDtlsImpl::get_peer_fingerprint(std::string algo, std::string &fingerprint)
{
    srs_error_t err = srs_success;

    const EVP_MD *md = srs_dtls_fingerprint_md(algo);
    if (!md) {
        return srs_error_new(ERROR_RTC_DTLS, "unsupported fingerprint algo %s", algo.c_str());
    }

#if OPENSSL_VERSION_NUMBER >= 0x30000000L // v3.0
    X509 *cert = SSL_get1_peer_certificate(dtls_);
#else
    X509 *cert = SSL_get_peer_certificate(dtls_);
#endif
    if (!cert) {
        return srs_error_new(ERROR_RTC_DTLS, "no peer certificate");
    }

    fingerprint = srs_dtls_fingerprint(cert, md);
    X509_free(cert);

    return err;
}

void SrsDtlsImpl::state_trace(uint8_t *data, int length, bool incoming, int r0)
{
    // change_cipher_spec(20), alert(21), handshake(22), application_data(23)
    // @see https://tools.ietf.org/html/rfc2246#section-6.2.1
    uint8_t content_type = 0;
    if (length >= 1) {
        content_type = (uint8_t)data[0];
    }

    uint16_t size = 0;
    if (length >= 13) {
        size = uint16_t(data[11]) << 8 | uint16_t(data[12]);
    }

    uint8_t handshake_type = 0;
    if (length >= 14) {
        handshake_type = (uint8_t)data[13];
    }

    srs_trace("DTLS: State %s %s, done=%u, arq=%u, r0=%d, len=%u, cnt=%u, size=%u, hs=%u",
              (is_dtls_client() ? "Active" : "Passive"), (incoming ? "RECV" : "SEND"), handshake_done_for_us_,
              nn_arq_packets_, r0, length, content_type, size, handshake_type);
}

const int SRTP_MASTER_KEY_KEY_LEN = 16;
const int SRTP_MASTER_KEY_SALT_LEN = 14;
srs_error_t SrsDtlsImpl::get_srtp_key(std::string &recv_key, std::string &send_key)
{
    srs_error_t err = srs_success;

    unsigned char material[SRTP_MASTER_KEY_LEN * 2] = {0}; // SRTP_MASTER_KEY_LEN is the key plus the salt, for the client and the server.
    static const string dtls_srtp_lable = "EXTRACTOR-dtls_srtp";
    if (!SSL_export_keying_material(dtls_, material, sizeof(material), dtls_srtp_lable.c_str(), dtls_srtp_lable.size(), NULL, 0, 0)) {
        return srs_error_new(ERROR_RTC_SRTP_INIT, "SSL export key r0=%lu", ERR_get_error());
    }

    size_t offset = 0;

    std::string client_master_key(reinterpret_cast<char *>(material), SRTP_MASTER_KEY_KEY_LEN);
    offset += SRTP_MASTER_KEY_KEY_LEN;
    std::string server_master_key(reinterpret_cast<char *>(material + offset), SRTP_MASTER_KEY_KEY_LEN);
    offset += SRTP_MASTER_KEY_KEY_LEN;
    std::string client_master_salt(reinterpret_cast<char *>(material + offset), SRTP_MASTER_KEY_SALT_LEN);
    offset += SRTP_MASTER_KEY_SALT_LEN;
    std::string server_master_salt(reinterpret_cast<char *>(material + offset), SRTP_MASTER_KEY_SALT_LEN);

    if (is_dtls_client()) {
        recv_key = server_master_key + server_master_salt;
        send_key = client_master_key + client_master_salt;
    } else {
        recv_key = client_master_key + client_master_salt;
        send_key = server_master_key + server_master_salt;
    }

    return err;
}

void SrsDtlsImpl::callback_by_ssl(std::string type, std::string desc)
{
    srs_error_t err = srs_success;
    if ((err = callback_->on_dtls_alert(type, desc)) != srs_success) {
        srs_warn2(TAG_DTLS_ALERT, "DTLS: handler alert err %s", srs_error_desc(err).c_str());
        srs_freep(err);
    }
}

SrsDtlsClientImpl::SrsDtlsClientImpl(ISrsDtlsCallback *callback) : SrsDtlsImpl(callback)
{
    trd_ = NULL;
    state_ = SrsDtlsStateInit;

    // OpenSSL retries DTLS at most 12 times.
    arq_max_retry_ = 12 * 2; // Max ARQ limit shared for ClientHello and Certificate.
}

SrsDtlsClientImpl::~SrsDtlsClientImpl()
{
    srs_freep(trd_);
}

srs_error_t SrsDtlsClientImpl::initialize(std::string version, std::string role)
{
    srs_error_t err = srs_success;

    if ((err = SrsDtlsImpl::initialize(version, role)) != srs_success) {
        return err;
    }

    SSL_set_connect_state(dtls_);

    return err;
}

srs_error_t SrsDtlsClientImpl::on_handshake_done()
{
    srs_error_t err = srs_success;

    if (state_ == SrsDtlsStateClientDone) {
        return err;
    }

    state_ = SrsDtlsStateClientDone;

    stop_arq();

    if (((err = callback_->on_dtls_handshake_done()) != srs_success)) {
        return srs_error_wrap(err, "dtls done");
    }

    return err;
}

bool SrsDtlsClientImpl::is_dtls_client()
{
    return true;
}

srs_error_t SrsDtlsClientImpl::start_arq()
{
    srs_error_t err = srs_success;

    srs_freep(trd_);
    trd_ = new SrsSTCoroutine("dtls", this, _srs_context->get_id());

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "arq start");
    }

    return err;
}

void SrsDtlsClientImpl::stop_arq()
{
    srs_freep(trd_);
}

// The timeout is set by dtls_timer_cb.
srs_error_t SrsDtlsClientImpl::cycle()
{
    srs_error_t err = srs_success;

    // The OpenSSL timeouts from dtls_timer_cb start at 50ms and double, but each wait below is at most 100ms,
    // so 12 ARQs take about 500 loops.
    const int max_loop = 512;

    int arq_count = 0;
    for (int i = 0; arq_count < arq_max_retry_ && i < max_loop; i++) {
        if ((err = trd_->pull()) != srs_success) {
            srs_freep(err);
            return err;
        }

        if (handshake_done_for_us_) {
            return err;
        }

        // DTLSv1_get_timeout returns 1 and sets the time left when a timer runs, or returns 0.
        timeval to = {0};
        int r0 = DTLSv1_get_timeout(dtls_, &to);
        srs_utime_t timeout = r0 == 1 ? to.tv_sec + to.tv_usec : 0;

        if (timeout > 0) {
            // A long sleep slows the ARQ, even when a received packet resets the timer.
            timeout = srs_min(timeout, 100 * SRS_UTIME_MILLISECONDS);
            srs_info("DTLS: ARQ wait timeout=%dms, to=%dms, r0=%d", srsu2msi(timeout), srsu2msi(to.tv_sec + to.tv_usec), r0);
            srs_usleep(timeout);
            continue;
        }

        if ((err = handle_timeout()) != srs_success) {
            return err;
        }
    }

    return err;
}

SrsDtlsServerImpl::SrsDtlsServerImpl(ISrsDtlsCallback *callback) : SrsDtlsImpl(callback)
{
    trd_ = NULL;
}

SrsDtlsServerImpl::~SrsDtlsServerImpl()
{
    srs_freep(trd_);
}

srs_error_t SrsDtlsServerImpl::initialize(std::string version, std::string role)
{
    srs_error_t err = srs_success;

    if ((err = SrsDtlsImpl::initialize(version, role)) != srs_success) {
        return err;
    }

    SSL_set_accept_state(dtls_);

    return err;
}

srs_error_t SrsDtlsServerImpl::on_handshake_done()
{
    srs_error_t err = srs_success;

    srs_freep(trd_);

    if (((err = callback_->on_dtls_handshake_done()) != srs_success)) {
        return srs_error_wrap(err, "dtls done");
    }

    return err;
}

bool SrsDtlsServerImpl::is_dtls_client()
{
    return false;
}

// A DTLS 1.2 client drives the ARQ: it resends its flight on its timer, and the read resends the server's.
// A DTLS 1.3 client that ACKed part of the server's flight resends nothing, and waits for the server's timer
// to resend the rest, see RFC 9147 section 7.2.
srs_error_t SrsDtlsServerImpl::on_dtls_read()
{
    srs_error_t err = srs_success;

    timeval to = {0};
    if (trd_ || handshake_done_for_us_ || !is_dtls13_server() || DTLSv1_get_timeout(dtls_, &to) != 1) {
        return err;
    }

    trd_ = new SrsSTCoroutine("dtls", this, _srs_context->get_id());
    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "arq start");
    }

    return err;
}

srs_error_t SrsDtlsServerImpl::start_arq()
{
    return srs_success;
}

srs_error_t SrsDtlsServerImpl::cycle()
{
    srs_error_t err = srs_success;

    while (!handshake_done_for_us_) {
        if ((err = trd_->pull()) != srs_success) {
            srs_freep(err);
            return err;
        }

        // No timer runs once the client ACKed the whole flight, until a new flight.
        timeval to = {0};
        srs_utime_t timeout = 100 * SRS_UTIME_MILLISECONDS;
        if (DTLSv1_get_timeout(dtls_, &to) == 1) {
            timeout = srs_min(timeout, to.tv_sec * SRS_UTIME_SECONDS + to.tv_usec);
        }

        if (timeout > 0) {
            srs_usleep(timeout);
            continue;
        }

        if ((err = handle_timeout()) != srs_success) {
            return srs_error_wrap(err, "arq");
        }
    }

    return err;
}

SrsDtlsEmptyImpl::SrsDtlsEmptyImpl() : SrsDtlsImpl(NULL)
{
    handshake_done_for_us_ = true;
}

SrsDtlsEmptyImpl::~SrsDtlsEmptyImpl()
{
}

srs_error_t SrsDtlsEmptyImpl::initialize(std::string version, std::string role)
{
    return srs_success;
}

srs_error_t SrsDtlsEmptyImpl::on_dtls(char *data, int nb_data)
{
    return srs_success;
}

srs_error_t SrsDtlsEmptyImpl::get_srtp_key(std::string &recv_key, std::string &send_key)
{
    return srs_success;
}

void SrsDtlsEmptyImpl::callback_by_ssl(std::string type, std::string desc)
{
}

srs_error_t SrsDtlsEmptyImpl::on_handshake_done()
{
    return srs_success;
}

bool SrsDtlsEmptyImpl::is_dtls_client()
{
    return false;
}

srs_error_t SrsDtlsEmptyImpl::start_arq()
{
    return srs_success;
}

ISrsDtls::ISrsDtls()
{
}

ISrsDtls::~ISrsDtls()
{
}

SrsDtls::SrsDtls(ISrsDtlsCallback *callback)
{
    callback_ = callback;
    impl_ = new SrsDtlsEmptyImpl();
}

SrsDtls::~SrsDtls()
{
    srs_freep(impl_);
}

srs_error_t SrsDtls::initialize(std::string role, std::string version)
{
    srs_freep(impl_);
    if (role == "active") {
        impl_ = new SrsDtlsClientImpl(callback_);
    } else {
        impl_ = new SrsDtlsServerImpl(callback_);
    }

    return impl_->initialize(version, role);
}

srs_error_t SrsDtls::start_active_handshake()
{
    return impl_->start_active_handshake();
}

srs_error_t SrsDtls::on_dtls(char *data, int nb_data)
{
    return impl_->on_dtls(data, nb_data);
}

void SrsDtls::enable_sped()
{
    impl_->enable_sped();
}

SrsSpedController *SrsDtls::sped()
{
    return impl_->sped();
}

srs_error_t SrsDtls::on_embedded_dtls(char *data, int nb_data)
{
    return impl_->on_embedded_dtls(data, nb_data);
}

srs_error_t SrsDtls::get_srtp_key(std::string &recv_key, std::string &send_key)
{
    return impl_->get_srtp_key(recv_key, send_key);
}

srs_error_t SrsDtls::get_peer_fingerprint(std::string algo, std::string &fingerprint)
{
    return impl_->get_peer_fingerprint(algo, fingerprint);
}

ISrsSRTP::ISrsSRTP()
{
}

ISrsSRTP::~ISrsSRTP()
{
}

SrsSRTP::SrsSRTP()
{
    recv_ctx_ = NULL;
    send_ctx_ = NULL;
}

SrsSRTP::~SrsSRTP()
{
    if (recv_ctx_) {
        srtp_dealloc(recv_ctx_);
    }

    if (send_ctx_) {
        srtp_dealloc(send_ctx_);
    }
}

srs_error_t SrsSRTP::initialize(string recv_key, std::string send_key)
{
    srs_error_t err = srs_success;

    srtp_policy_t policy;
    memset(&policy, 0, sizeof(policy));

    // TODO: Maybe we can use SRTP-GCM in future.
    // @see https://bugs.chromium.org/p/chromium/issues/detail?id=713701
    // @see https://groups.google.com/forum/#!topic/discuss-webrtc/PvCbWSetVAQ
    srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtp);
    srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtcp);

    policy.ssrc.value = 0;
    // TODO: adjust window_size
    policy.window_size = 8192;
    policy.allow_repeat_tx = 1;
    policy.next = NULL;

    policy.ssrc.type = ssrc_any_inbound;
    SrsUniquePtr<uint8_t[]> rkey(new uint8_t[recv_key.size()]);
    memcpy(rkey.get(), recv_key.data(), recv_key.size());
    policy.key = rkey.get();

    srtp_err_status_t r0 = srtp_err_status_ok;
    if ((r0 = srtp_create(&recv_ctx_, &policy)) != srtp_err_status_ok) {
        return srs_error_new(ERROR_RTC_SRTP_INIT, "srtp create r0=%u", r0);
    }

    policy.ssrc.type = ssrc_any_outbound;
    SrsUniquePtr<uint8_t[]> skey(new uint8_t[send_key.size()]);
    memcpy(skey.get(), send_key.data(), send_key.size());
    policy.key = skey.get();

    if ((r0 = srtp_create(&send_ctx_, &policy)) != srtp_err_status_ok) {
        return srs_error_new(ERROR_RTC_SRTP_INIT, "srtp create r0=%u", r0);
    }

    return err;
}

srs_error_t SrsSRTP::protect_rtp(void *packet, int *nb_cipher)
{
    srs_error_t err = srs_success;

    if (!send_ctx_) {
        return srs_error_new(ERROR_RTC_SRTP_PROTECT, "not ready");
    }

    srtp_err_status_t r0 = srtp_err_status_ok;
    if ((r0 = srtp_protect(send_ctx_, packet, nb_cipher)) != srtp_err_status_ok) {
        return srs_error_new(ERROR_RTC_SRTP_PROTECT, "rtp protect r0=%u", r0);
    }

    return err;
}

srs_error_t SrsSRTP::protect_rtcp(void *packet, int *nb_cipher)
{
    srs_error_t err = srs_success;

    if (!send_ctx_) {
        return srs_error_new(ERROR_RTC_SRTP_PROTECT, "not ready");
    }

    srtp_err_status_t r0 = srtp_err_status_ok;
    if ((r0 = srtp_protect_rtcp(send_ctx_, packet, nb_cipher)) != srtp_err_status_ok) {
        return srs_error_new(ERROR_RTC_SRTP_PROTECT, "rtcp protect r0=%u", r0);
    }

    return err;
}

srs_error_t SrsSRTP::unprotect_rtp(void *packet, int *nb_plaintext)
{
    srs_error_t err = srs_success;

    if (!recv_ctx_) {
        return srs_error_new(ERROR_RTC_SRTP_UNPROTECT, "not ready");
    }

    srtp_err_status_t r0 = srtp_err_status_ok;
    if ((r0 = srtp_unprotect(recv_ctx_, packet, nb_plaintext)) != srtp_err_status_ok) {
        return srs_error_new(ERROR_RTC_SRTP_UNPROTECT, "rtp unprotect r0=%u", r0);
    }

    return err;
}

srs_error_t SrsSRTP::unprotect_rtcp(void *packet, int *nb_plaintext)
{
    srs_error_t err = srs_success;

    if (!recv_ctx_) {
        return srs_error_new(ERROR_RTC_SRTP_UNPROTECT, "not ready");
    }

    srtp_err_status_t r0 = srtp_err_status_ok;
    if ((r0 = srtp_unprotect_rtcp(recv_ctx_, packet, nb_plaintext)) != srtp_err_status_ok) {
        return srs_error_new(ERROR_RTC_SRTP_UNPROTECT, "rtcp unprotect r0=%u", r0);
    }

    return err;
}
