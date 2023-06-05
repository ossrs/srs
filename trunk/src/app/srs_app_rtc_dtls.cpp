//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_rtc_dtls.hpp>

using namespace std;

#include <string.h>

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_config.hpp>
#include <srs_core_autofree.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_app_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_utility.hpp>

#include <srtp2/srtp.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

// to avoid dtls negotiate failed, set max fragment size 1200.
// @see https://github.com/ossrs/srs/issues/2415
const int DTLS_FRAGMENT_MAX_SIZE = 1200;

// Defined in HTTP/HTTPS client.
extern int srs_verify_callback(int preverify_ok, X509_STORE_CTX *ctx);

// Setup the openssl timeout for DTLS packet.
// @see https://www.openssl.org/docs/man1.1.1/man3/DTLS_set_timer_cb.html
//
// Use step timeout for ARQ, [50, 100, 200, 400, 800, 1600, 3200, 6400, 12800, 25600, 51200] in ms,
// then total timeout is sum([50, 100, 200, 400, 800, 1600, 3200, 6400, 12800, 25600, 51200]) = 102350ms.
//
// @remark The connection might be closed for timeout in about 30s by default, which stop the DTLS ARQ.
unsigned int dtls_timer_cb(SSL* dtls, unsigned int previous_us)
{
    SrsDtlsImpl* dtls_impl = (SrsDtlsImpl*)SSL_get_ex_data(dtls, 0);
    srs_assert(dtls_impl);

    // Double the timeout in us. Note that it can be 0.
    unsigned int timeout_us = previous_us * 2;

    // limit the timeout in [50ms, 30s].
    timeout_us = srs_max(timeout_us, 50 * 1000);
    timeout_us = srs_min(timeout_us, 30 * 1000 * 1000);
    srs_info("DTLS: ARQ timer cb timeout=%ums, previous=%ums", timeout_us/1000, previous_us/1000);

    return timeout_us;
}

// Print the information of SSL, DTLS alert as such.
void ssl_on_info(const SSL* dtls, int where, int ret)
{
    SrsDtlsImpl* dtls_impl = (SrsDtlsImpl*)SSL_get_ex_data(dtls, 0);
    srs_assert(dtls_impl);

    const char* method;
    int w = where& ~SSL_ST_MASK;
    if (w & SSL_ST_CONNECT) {
        method = "SSL_connect";
    } else if (w & SSL_ST_ACCEPT) {
        method = "SSL_accept";
    } else {
        method = "undefined";
    }

    int r1 = SSL_get_error(dtls, ret); ERR_clear_error();
    if (where & SSL_CB_LOOP) {
        srs_info("DTLS: method=%s state=%s(%s), where=%d, ret=%d, r1=%d", method, SSL_state_string(dtls),
            SSL_state_string_long(dtls), where, ret, r1);
    } else if (where & SSL_CB_ALERT) {
        method = (where & SSL_CB_READ) ? "read":"write";

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

        // Notify the DTLS to handle the ALERT message, which maybe means media connection disconnect.
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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
SSL_CTX* srs_build_dtls_ctx(SrsDtlsVersion version, std::string role)
{
    SSL_CTX* dtls_ctx;
#if OPENSSL_VERSION_NUMBER < 0x10002000L // v1.0.2
    dtls_ctx = SSL_CTX_new(DTLSv1_method());
#else
    if (version == SrsDtlsVersion1_0) {
        if (role == "active") {
            dtls_ctx = SSL_CTX_new(DTLSv1_client_method());
        } else {
            dtls_ctx = SSL_CTX_new(DTLSv1_server_method());
        }
    } else if (version == SrsDtlsVersion1_2) {
        if (role == "active") {
            dtls_ctx = SSL_CTX_new(DTLS_client_method());
        } else {
            dtls_ctx = SSL_CTX_new(DTLS_server_method());
        }
    } else {
        // SrsDtlsVersionAuto, use version-flexible DTLS methods
        dtls_ctx = SSL_CTX_new(DTLS_method());
    }
#endif

    if (_srs_rtc_dtls_certificate->is_ecdsa()) { // By ECDSA, https://stackoverflow.com/a/6006898
#if OPENSSL_VERSION_NUMBER >= 0x10002000L // v1.0.2
        // For ECDSA, we could set the curves list.
        // @see https://www.openssl.org/docs/man1.0.2/man3/SSL_CTX_set1_curves_list.html
        SSL_CTX_set1_curves_list(dtls_ctx, "P-521:P-384:P-256");
#endif

        // For openssl <1.1, we must set the ECDH manually.
        // @see https://stackoverrun.com/cn/q/10791887
#if OPENSSL_VERSION_NUMBER < 0x10100000L // v1.1.x
    #if OPENSSL_VERSION_NUMBER < 0x10002000L // v1.0.2
        SSL_CTX_set_tmp_ecdh(dtls_ctx, _srs_rtc_dtls_certificate->get_ecdsa_key());
    #else
        SSL_CTX_set_ecdh_auto(dtls_ctx, 1);
    #endif
#endif
    }

    // Setup DTLS context.
    if (true) {
        // We use "ALL", while you can use "DEFAULT" means "ALL:!EXPORT:!LOW:!aNULL:!eNULL:!SSLv2"
        // @see https://www.openssl.org/docs/man1.0.2/man1/ciphers.html
        srs_assert(SSL_CTX_set_cipher_list(dtls_ctx, "ALL") == 1);

        // Setup the certificate.
        srs_assert(SSL_CTX_use_certificate(dtls_ctx, _srs_rtc_dtls_certificate->get_cert()) == 1);
        srs_assert(SSL_CTX_use_PrivateKey(dtls_ctx, _srs_rtc_dtls_certificate->get_public_key()) == 1);

        // Server will send Certificate Request.
        // @see https://www.openssl.org/docs/man1.0.2/man3/SSL_CTX_set_verify.html
        // TODO: FIXME: Config it, default to off to make the packet smaller.
        SSL_CTX_set_verify(dtls_ctx, SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE, srs_verify_callback);
        // The depth count is "level 0:peer certificate", "level 1: CA certificate",
        // "level 2: higher level CA certificate", and so on.
        // @see https://www.openssl.org/docs/man1.0.2/man3/SSL_CTX_set_verify.html
        SSL_CTX_set_verify_depth(dtls_ctx, 4);

        // Whether we should read as many input bytes as possible (for non-blocking reads) or not.
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

SrsDtlsCertificate::SrsDtlsCertificate()
{
    ecdsa_mode = true;
    dtls_cert = NULL;
    dtls_pkey = NULL;
    eckey = NULL;
}

SrsDtlsCertificate::~SrsDtlsCertificate()
{
    if (eckey) {
        EC_KEY_free(eckey);
    }

    if (dtls_pkey) {
        EVP_PKEY_free(dtls_pkey);
    }

    if (dtls_cert) {
        X509_free(dtls_cert);
    }
}

srs_error_t SrsDtlsCertificate::initialize()
{
    srs_error_t err = srs_success;

    // Initialize once.
    if (dtls_cert) {
        return err;
    }

#if OPENSSL_VERSION_NUMBER < 0x10100000L // v1.1.x
    // Initialize SSL library by registering algorithms
    // The SSL_library_init() and OpenSSL_add_ssl_algorithms() functions were deprecated in OpenSSL 1.1.0 by OPENSSL_init_ssl().
    // @see https://www.openssl.org/docs/man1.1.0/man3/OpenSSL_add_ssl_algorithms.html
    // @see https://web.archive.org/web/20150806185102/http://sctp.fh-muenster.de:80/dtls/dtls_udp_echo.c
    OpenSSL_add_ssl_algorithms();
#else
    // As of version 1.1.0 OpenSSL will automatically allocate all resources that it needs so no explicit
    // initialisation is required. Similarly it will also automatically deinitialise as required.
    // @see https://www.openssl.org/docs/man1.1.0/man3/OPENSSL_init_ssl.html
    // OPENSSL_init_ssl();
#endif

    // Initialize SRTP first.
    srs_assert(srtp_init() == 0);

    // Whether use ECDSA certificate.
    ecdsa_mode = _srs_config->get_rtc_server_ecdsa();

    // Create keys by RSA or ECDSA.
    dtls_pkey = EVP_PKEY_new();
    srs_assert(dtls_pkey);
    if (!ecdsa_mode) { // By RSA
        RSA* rsa = RSA_new();
        srs_assert(rsa);

        // Initialize the big-number for private key.
        BIGNUM* exponent = BN_new();
        srs_assert(exponent);
        BN_set_word(exponent, RSA_F4);

        // Generates a key pair and stores it in the RSA structure provided in rsa.
        // @see https://www.openssl.org/docs/man1.0.2/man3/RSA_generate_key_ex.html
        int key_bits = 1024;
        RSA_generate_key_ex(rsa, key_bits, exponent, NULL);

        // @see https://www.openssl.org/docs/man1.1.0/man3/EVP_PKEY_type.html
        srs_assert(EVP_PKEY_set1_RSA(dtls_pkey, rsa) == 1);

        RSA_free(rsa);
        BN_free(exponent);
    }
    if (ecdsa_mode) { // By ECDSA, https://stackoverflow.com/a/6006898
        eckey = EC_KEY_new();
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
        EC_GROUP* ecgroup = EC_GROUP_new_by_curve_name(NID_X9_62_prime256v1);
        //EC_GROUP* ecgroup = EC_GROUP_new_by_curve_name(NID_secp384r1);
        srs_assert(ecgroup);
#if OPENSSL_VERSION_NUMBER < 0x10100000L // v1.1.x
        // For openssl 1.0, we must set the group parameters, so that cert is ok.
        // @see https://github.com/monero-project/monero/blob/master/contrib/epee/src/net_ssl.cpp#L225
        EC_GROUP_set_asn1_flag(ecgroup, OPENSSL_EC_NAMED_CURVE);
#endif

        srs_assert(EC_KEY_set_group(eckey, ecgroup) == 1);
        srs_assert(EC_KEY_generate_key(eckey) == 1);

        // @see https://www.openssl.org/docs/man1.1.0/man3/EVP_PKEY_type.html
        srs_assert(EVP_PKEY_set1_EC_KEY(dtls_pkey, eckey) == 1);

        EC_GROUP_free(ecgroup);
    }

    // Create certificate, from previous generated pkey.
    // TODO: Support ECDSA certificate.
    dtls_cert = X509_new();
    srs_assert(dtls_cert);
    if (true) {
        X509_NAME* subject = X509_NAME_new();
        srs_assert(subject);

        int serial = (int)srs_random();
        ASN1_INTEGER_set(X509_get_serialNumber(dtls_cert), serial);

        const std::string& aor = RTMP_SIG_SRS_DOMAIN;
        X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC, (unsigned char *) aor.data(), aor.size(), -1, 0);

        X509_set_issuer_name(dtls_cert, subject);
        X509_set_subject_name(dtls_cert, subject);

        int expire_day = 365;
        const long cert_duration = 60*60*24*expire_day;

        X509_gmtime_adj(X509_get_notBefore(dtls_cert), 0);
        X509_gmtime_adj(X509_get_notAfter(dtls_cert), cert_duration);

        X509_set_version(dtls_cert, 2);
        srs_assert(X509_set_pubkey(dtls_cert, dtls_pkey) == 1);
        srs_assert(X509_sign(dtls_cert, dtls_pkey, EVP_sha1()) != 0);

        X509_NAME_free(subject);
    }

    // Show DTLS fingerprint
    if (true) {
        unsigned char md[EVP_MAX_MD_SIZE];
        unsigned int n = 0;

        // TODO: FIXME: Unused variable.
        /*int r = */X509_digest(dtls_cert, EVP_sha256(), md, &n);

        char* fp = new char[3 * n];
        SrsAutoFreeA(char, fp);
        char *p = fp;

        for (unsigned int i = 0; i < n; i++, ++p) {
            int nb = snprintf(p, 3, "%02X", md[i]);
            srs_assert(nb > 0 && nb < (3 * n - (p - fp)));
            p += nb;

            if(i < (n-1)) {
                *p = ':';
            } else {
                *p = '\0';
            }
        }

        fingerprint.assign(fp, strlen(fp));
        srs_trace("fingerprint=%s", fingerprint.c_str());
    }

    return err;
}

X509* SrsDtlsCertificate::get_cert()
{
    return dtls_cert;
}

EVP_PKEY* SrsDtlsCertificate::get_public_key()
{
    return dtls_pkey;
}
    
EC_KEY* SrsDtlsCertificate::get_ecdsa_key() 
{
    return eckey;
}
    
std::string SrsDtlsCertificate::get_fingerprint()
{
    return fingerprint;
}

bool SrsDtlsCertificate::is_ecdsa()
{
    return ecdsa_mode;
}

ISrsDtlsCallback::ISrsDtlsCallback()
{
}

ISrsDtlsCallback::~ISrsDtlsCallback()
{
}

SrsDtlsImpl::SrsDtlsImpl(ISrsDtlsCallback* callback)
{
    dtls_ctx = NULL;
    dtls = NULL;
    bio_in = NULL;
    bio_out = NULL;

    callback_ = callback;
    handshake_done_for_us = false;
    nn_arq_packets = 0;
    last_handshake_type = 0;
    last_content_type = 0;

    version_ = SrsDtlsVersionAuto;
}

SrsDtlsImpl::~SrsDtlsImpl()
{
    if (!handshake_done_for_us) {
        srs_warn2(TAG_DTLS_HANG, "DTLS: Hang, done=%u, version=%d, arq=%u", handshake_done_for_us,
            version_, nn_arq_packets);
    }

    if (dtls_ctx) {
        SSL_CTX_free(dtls_ctx);
        dtls_ctx = NULL;
    }

    if (dtls) {
        // this function will free bio_in and bio_out
        SSL_free(dtls);
        dtls = NULL;
    }
}

long srs_dtls_bio_out_callback(BIO* bio, int cmd, const char* argp, int argi, long argl, long ret)
{
    long r0 = (BIO_CB_RETURN & cmd) ? ret : 1;
    if (cmd == BIO_CB_WRITE && argp && argi > 0) {
        SrsDtlsImpl* dtls = (SrsDtlsImpl*)BIO_get_callback_arg(bio);
        srs_error_t err = dtls->write_dtls_data((void*)argp, argi);
        if (err != srs_success) {
            srs_warn("ignore err %s", srs_error_desc(err).c_str());
        }
        srs_freep(err);
    }
    return r0;
}

srs_error_t SrsDtlsImpl::write_dtls_data(void* data, int size)
{
    srs_error_t err = srs_success;

    if (size > 0 && (err = callback_->write_dtls_data(data, size)) != srs_success) {
        return srs_error_wrap(err, "dtls send size=%u, data=[%s]", size,
            srs_string_dumps_hex((char*)data, size, 32).c_str());
    }

    // change_cipher_spec(20), alert(21), handshake(22), application_data(23)
    // @see https://tools.ietf.org/html/rfc2246#section-6.2.1
    uint8_t content_type = size >= 1 ? ((uint8_t*)data)[0] : 0;
    uint8_t handshake_type = size >= 14 ? ((uint8_t*)data)[13] : 0;
    if (content_type && handshake_type && last_content_type == content_type && last_handshake_type == handshake_type) {
        nn_arq_packets++;
    }
    last_content_type = content_type;
    last_handshake_type = handshake_type;

    // Logging when got SSL original data.
    state_trace((uint8_t*)data, size, false, 0);

    return err;
}

srs_error_t SrsDtlsImpl::initialize(std::string version, std::string role)
{
    srs_error_t err = srs_success;

    if (version == "dtls1.0") {
        version_ = SrsDtlsVersion1_0;
    } else if (version == "dtls1.2") {
        version_ = SrsDtlsVersion1_2;
    } else {
        version_ = SrsDtlsVersionAuto;
    }

    dtls_ctx = srs_build_dtls_ctx(version_, role);

    if ((dtls = SSL_new(dtls_ctx)) == NULL) {
        return srs_error_new(ERROR_OpenSslCreateSSL, "SSL_new dtls");
    }

    SSL_set_ex_data(dtls, 0, this);
    SSL_set_info_callback(dtls, ssl_on_info);

    // We have set the MTU to fragment the DTLS packet. It is important to note that the packet is split
    // to ensure that each handshake packet is smaller than the MTU.
    // @see https://stackoverflow.com/questions/62413602/openssl-server-packets-get-fragmented-into-270-bytes-per-packet
    SSL_set_options(dtls, SSL_OP_NO_QUERY_MTU);
    SSL_set_mtu(dtls, DTLS_FRAGMENT_MAX_SIZE);
    // See https://github.com/versatica/mediasoup/pull/217
    DTLS_set_link_mtu(dtls, DTLS_FRAGMENT_MAX_SIZE);

    // @see https://linux.die.net/man/3/openssl_version_number
    //                MM NN FF PP S
    // 0x1010102fL = 0x1 01 01 02 fL            // 1.1.1b release
    //   MM(major) = 0x1                        // 1.*
    //     NN(minor) = 0x01                     // 1.1.*
    //          FF(fix) = 0x01                  // 1.1.1*
    //     PP(patch) = 'a' + 0x02 - 1 = 'b'     // 1.1.1b *
    //              S(status) = 0xf = release   // 1.1.1b release
    // @note Status 0 for development, 1 to e for betas 1 to 14, and f for release.
#if OPENSSL_VERSION_NUMBER >= 0x1010102fL // 1.1.1b
    DTLS_set_timer_cb(dtls, dtls_timer_cb);
#endif

    // Setup memory BIO.
    if ((bio_in = BIO_new(BIO_s_mem())) == NULL) {
        return srs_error_new(ERROR_OpenSslBIONew, "BIO_new in");
    }

    if ((bio_out = BIO_new(BIO_s_mem())) == NULL) {
        BIO_free(bio_in);
        return srs_error_new(ERROR_OpenSslBIONew, "BIO_new out");
    }

    // Please be aware that it is necessary to use a callback to obtain the packet to be written out. It is
    // imperative that BIO_get_mem_data is not used to retrieve the packet, as it returns all the bytes that
    // need to be sent out.
    // For example, if MTU is set to 1200, and we got two DTLS packets to sendout:
    //      ServerHello, 95bytes.
    //      Certificate, 1105+143=1248bytes.
    // If use BIO_get_mem_data, it will return 95+1248=1343bytes, which is larger than MTU 1200.
    // If use callback, it will return two UDP packets:
    //      ServerHello+Certificate(Frament) = 95+1105=1200bytes.
    //      Certificate(Fragment) = 143bytes.
    // Note that there should be more packets in real world, like ServerKeyExchange, CertificateRequest,
    // and ServerHelloDone. Here we just use two packets for example.
    BIO_set_callback(bio_out, srs_dtls_bio_out_callback);
    BIO_set_callback_arg(bio_out, (char*)this);

    SSL_set_bio(dtls, bio_in, bio_out);

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
    int r0 = SSL_do_handshake(dtls);
    int r1 = SSL_get_error(dtls, r0); ERR_clear_error();
    // Fatal SSL error, for example, no available suite when peer is DTLS 1.0 while we are DTLS 1.2.
    if (r0 < 0 && (r1 != SSL_ERROR_NONE && r1 != SSL_ERROR_WANT_READ && r1 != SSL_ERROR_WANT_WRITE)) {
        return srs_error_new(ERROR_RTC_DTLS, "handshake r0=%d, r1=%d", r0, r1);
    }

    if ((err = start_arq()) != srs_success) {
        return srs_error_wrap(err, "start arq");
    }

    return err;
}

srs_error_t SrsDtlsImpl::on_dtls(char* data, int nb_data)
{
    srs_error_t err = srs_success;

    if ((err = do_on_dtls(data, nb_data)) != srs_success) {
        return srs_error_wrap(err, "on_dtls size=%u, data=[%s]", nb_data,
            srs_string_dumps_hex(data, nb_data, 32).c_str());
    }

    return err;
}

srs_error_t SrsDtlsImpl::do_on_dtls(char* data, int nb_data)
{
    srs_error_t err = srs_success;

    // When already done, only for us, we still got message from client,
    // it might be our response is lost, or application data.
    if (handshake_done_for_us) {
        srs_trace("DTLS: After done, got %d bytes", nb_data);
    }

    // Feed the received DTLS packets to BIO; we will consume them later.
    int r0 = 0;
    if ((r0 = BIO_write(bio_in, data, nb_data)) <= 0) {
        // TODO: 0 or -1 maybe block, use BIO_should_retry to check.
        return srs_error_new(ERROR_OpenSslBIOWrite, "BIO_write r0=%d", r0);
    }
    state_trace((uint8_t*)data, nb_data, true, r0);

    // If there is data available in bio_in, use SSL_read to allow SSL to process it.
    // We limit the MTU to 1200 for DTLS handshake, which ensures that the buffer is large enough for reading.
    // TODO: FIXME: DTLS application messages, such as DataChannel messages, may exceed 1500 bytes, but they should be
    //  fragmented. This fragmentation should be done at the application level. However, I'm not certain about this
    //  and will leave it to the developer who is responsible for developing the DataChannel.
    char buf[kRtpPacketSize];
    r0 = SSL_read(dtls, buf, sizeof(buf));
    int r1 = SSL_get_error(dtls, r0); ERR_clear_error();
    if (r0 <= 0) {
        if (r1 != SSL_ERROR_WANT_READ && r1 != SSL_ERROR_WANT_WRITE && r1 != SSL_ERROR_ZERO_RETURN) {
            return srs_error_new(ERROR_RTC_DTLS, "DTLS: read r0=%d, r1=%d, done=%d", r0, r1, handshake_done_for_us);
        }
    } else {
        srs_trace("DTLS: read r0=%d, r1=%d, padding=%d, done=%d, data=[%s]",
            r0, r1, BIO_ctrl_pending(bio_in), handshake_done_for_us, srs_string_dumps_hex(buf, r0, 32).c_str());

        if ((err = callback_->on_dtls_application_data(buf, r0)) != srs_success) {
            return srs_error_wrap(err, "on DTLS data, done=%d, r1=%d, size=%u, data=[%s]", handshake_done_for_us,
                r1, r0, srs_string_dumps_hex(buf, r0, 32).c_str());
        }
    }

    // Check whether the DTLS is completed.
    if (!handshake_done_for_us && SSL_is_init_finished(dtls) == 1) {
        handshake_done_for_us = true;
        if (((err = on_handshake_done()) != srs_success)) {
            return srs_error_wrap(err, "done");
        }
    }

    return err;
}

void SrsDtlsImpl::state_trace(uint8_t* data, int length, bool incoming, int r0)
{
    // change_cipher_spec(20), alert(21), handshake(22), application_data(23)
    // @see https://tools.ietf.org/html/rfc2246#section-6.2.1
    uint8_t content_type = 0;
    if (length >= 1) {
        content_type = (uint8_t)data[0];
    }

    uint16_t size = 0;
    if (length >= 13) {
        size = uint16_t(data[11])<<8 | uint16_t(data[12]);
    }

    uint8_t handshake_type = 0;
    if (length >= 14) {
        handshake_type = (uint8_t)data[13];
    }

    srs_trace("DTLS: State %s %s, done=%u, arq=%u, r0=%d, len=%u, cnt=%u, size=%u, hs=%u",
        (is_dtls_client()? "Active":"Passive"), (incoming? "RECV":"SEND"), handshake_done_for_us,
        nn_arq_packets, r0, length, content_type, size, handshake_type);
}

const int SRTP_MASTER_KEY_KEY_LEN = 16;
const int SRTP_MASTER_KEY_SALT_LEN = 14;
srs_error_t SrsDtlsImpl::get_srtp_key(std::string& recv_key, std::string& send_key)
{
    srs_error_t err = srs_success;

    unsigned char material[SRTP_MASTER_KEY_LEN * 2] = {0};  // client(SRTP_MASTER_KEY_KEY_LEN + SRTP_MASTER_KEY_SALT_LEN) + server
    static const string dtls_srtp_lable = "EXTRACTOR-dtls_srtp";
    if (!SSL_export_keying_material(dtls, material, sizeof(material), dtls_srtp_lable.c_str(), dtls_srtp_lable.size(), NULL, 0, 0)) {
        return srs_error_new(ERROR_RTC_SRTP_INIT, "SSL export key r0=%lu", ERR_get_error());
    }

    size_t offset = 0;

    std::string client_master_key(reinterpret_cast<char*>(material), SRTP_MASTER_KEY_KEY_LEN);
    offset += SRTP_MASTER_KEY_KEY_LEN;
    std::string server_master_key(reinterpret_cast<char*>(material + offset), SRTP_MASTER_KEY_KEY_LEN);
    offset += SRTP_MASTER_KEY_KEY_LEN;
    std::string client_master_salt(reinterpret_cast<char*>(material + offset), SRTP_MASTER_KEY_SALT_LEN);
    offset += SRTP_MASTER_KEY_SALT_LEN;
    std::string server_master_salt(reinterpret_cast<char*>(material + offset), SRTP_MASTER_KEY_SALT_LEN);

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

SrsDtlsClientImpl::SrsDtlsClientImpl(ISrsDtlsCallback* callback) : SrsDtlsImpl(callback)
{
    trd = NULL;
    state_ = SrsDtlsStateInit;

    // the max dtls retry num is 12 in openssl.
    arq_max_retry = 12 * 2; // Max ARQ limit shared for ClientHello and Certificate.
}

SrsDtlsClientImpl::~SrsDtlsClientImpl()
{
    srs_freep(trd);
}

srs_error_t SrsDtlsClientImpl::initialize(std::string version, std::string role)
{
    srs_error_t err = srs_success;

    if ((err = SrsDtlsImpl::initialize(version, role)) != srs_success) {
        return err;
    }

    // Dtls setup active, as client role.
    SSL_set_connect_state(dtls);

    return err;
}

srs_error_t SrsDtlsClientImpl::on_handshake_done()
{
    srs_error_t err = srs_success;

    // Ignore if done.
    if (state_ == SrsDtlsStateClientDone) {
        return err;
    }

    // Change to done state.
    state_ = SrsDtlsStateClientDone;

    // When handshake done, stop the ARQ.
    stop_arq();

    // Notify connection the DTLS is done.
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

    // Dispose the previous ARQ thread.
    srs_freep(trd);
    trd = new SrsSTCoroutine("dtls", this, _srs_context->get_id());

    // We should start the ARQ thread for DTLS client.
    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "arq start");
    }

    return err;
}

void SrsDtlsClientImpl::stop_arq()
{
    srs_freep(trd);
}

// The timeout is set by dtls_timer_cb.
srs_error_t SrsDtlsClientImpl::cycle()
{
    srs_error_t err = srs_success;

    // Limit the max retry for ARQ, to avoid infinite loop.
    // Note that we set the timeout to [50, 100, 200, 400, 800, 1600, 3200, 6400, 12800, 25600, 51200] in ms
    // by dtls_timer_cb, but the actual timeout is limit to 1s:
    //      50ms, 100ms, 200ms, 400ms, 800ms, (1000ms,600ms), (200ms,1000ms,1000ms,1000ms),
    //      (400ms,1000ms,1000ms,1000ms,1000ms,1000ms,1000ms), ...
    // So when the max ARQ limit to 12 times, the max loop is about 103.
    // @remark We change the max sleep to 100ms, so we limit about (103*10)/2=500.
    const int max_loop = 512;

    int arq_count = 0;
    for (int i = 0; arq_count < arq_max_retry && i < max_loop; i++) {
        // We ignore any error for ARQ thread.
        if ((err = trd->pull()) != srs_success) {
            srs_freep(err);
            return err;
        }

        // If done, should stop ARQ.
        if (handshake_done_for_us) {
            return err;
        }

        // If there is a timeout in progress, it sets *out to the time remaining
        // and returns one. Otherwise, it returns zero.
        timeval to = {0};
        int r0 = DTLSv1_get_timeout(dtls, &to);
        srs_utime_t timeout = r0 == 1 ? to.tv_sec + to.tv_usec : 0;

        // There is timeout to wait, so we should wait, because there is no packet in openssl.
        if (timeout > 0) {
            // Sleeping for an excessively long period of time may result in a significant slowdown
            // of the ARQ, even if the timeout is reset by the receipt of a packet. To ensure timely
            // response, it is recommended to decrease the timeout duration and increase the frequency
            // of checks.
            timeout = srs_min(timeout, 100 * SRS_UTIME_MILLISECONDS);
            srs_info("DTLS: ARQ wait timeout=%dms, to=%dms, r0=%d", srsu2msi(timeout), srsu2msi(to.tv_sec + to.tv_usec), r0);
            srs_usleep(timeout);
            continue;
        }

        // DTLSv1_handle_timeout is called when a DTLS handshake timeout expires. If no timeout
        // had expired, it returns 0. Otherwise, it retransmits the previous flight of handshake
        // messages and returns 1. If too many timeouts had expired without progress or an error
        // occurs, it returns -1.
        r0 = DTLSv1_handle_timeout(dtls);
        if (r0 != 0 && r0 != 1) {
            int r1 = SSL_get_error(dtls, r0); ERR_clear_error();
            return srs_error_new(ERROR_RTC_DTLS, "ARQ r0=%d, r1=%d", r0, r1);
        }
    }

    return err;
}

SrsDtlsServerImpl::SrsDtlsServerImpl(ISrsDtlsCallback* callback) : SrsDtlsImpl(callback)
{
}

SrsDtlsServerImpl::~SrsDtlsServerImpl()
{
}

srs_error_t SrsDtlsServerImpl::initialize(std::string version, std::string role)
{
    srs_error_t err = srs_success;

    if ((err = SrsDtlsImpl::initialize(version, role)) != srs_success) {
        return err;
    }

    // Dtls setup passive, as server role.
    SSL_set_accept_state(dtls);

    return err;
}

srs_error_t SrsDtlsServerImpl::on_handshake_done()
{
    srs_error_t err = srs_success;

    // Notify connection the DTLS is done.
    if (((err = callback_->on_dtls_handshake_done()) != srs_success)) {
        return srs_error_wrap(err, "dtls done");
    }

    return err;
}

bool SrsDtlsServerImpl::is_dtls_client()
{
    return false;
}

srs_error_t SrsDtlsServerImpl::start_arq()
{
    // We do not initiate ARQ for the DTLS server since it is controlled by the DTLS client.
    return srs_success;
}

SrsDtlsEmptyImpl::SrsDtlsEmptyImpl() : SrsDtlsImpl(NULL)
{
    handshake_done_for_us = true;
}

SrsDtlsEmptyImpl::~SrsDtlsEmptyImpl()
{
}

srs_error_t SrsDtlsEmptyImpl::initialize(std::string version, std::string role)
{
    return srs_success;
}

srs_error_t SrsDtlsEmptyImpl::on_dtls(char* data, int nb_data)
{
    return srs_success;
}

srs_error_t SrsDtlsEmptyImpl::get_srtp_key(std::string& recv_key, std::string& send_key)
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

SrsDtls::SrsDtls(ISrsDtlsCallback* callback)
{
    callback_ = callback;
    impl = new SrsDtlsEmptyImpl();
}

SrsDtls::~SrsDtls()
{
    srs_freep(impl);
}

srs_error_t SrsDtls::initialize(std::string role, std::string version)
{
    srs_freep(impl);
    if (role == "active") {
        impl = new SrsDtlsClientImpl(callback_);
    } else {
        impl = new SrsDtlsServerImpl(callback_);
    }

    return impl->initialize(version, role);
}

srs_error_t SrsDtls::start_active_handshake()
{
    return impl->start_active_handshake();
}

srs_error_t SrsDtls::on_dtls(char* data, int nb_data)
{
    return impl->on_dtls(data, nb_data);
}

srs_error_t SrsDtls::get_srtp_key(std::string& recv_key, std::string& send_key)
{
    return impl->get_srtp_key(recv_key, send_key);
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
    bzero(&policy, sizeof(policy));

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

    // init recv context
    policy.ssrc.type = ssrc_any_inbound;
    uint8_t *rkey = new uint8_t[recv_key.size()];
    SrsAutoFreeA(uint8_t, rkey);
    memcpy(rkey, recv_key.data(), recv_key.size());
    policy.key = rkey;

    srtp_err_status_t r0 = srtp_err_status_ok;
    if ((r0 = srtp_create(&recv_ctx_, &policy)) != srtp_err_status_ok) {
        return srs_error_new(ERROR_RTC_SRTP_INIT, "srtp create r0=%u", r0);
    }

    policy.ssrc.type = ssrc_any_outbound;
    uint8_t *skey = new uint8_t[send_key.size()];
    SrsAutoFreeA(uint8_t, skey);
    memcpy(skey, send_key.data(), send_key.size());
    policy.key = skey;

    if ((r0 = srtp_create(&send_ctx_, &policy)) != srtp_err_status_ok) {
        return srs_error_new(ERROR_RTC_SRTP_INIT, "srtp create r0=%u", r0);
    }

    return err;
}

srs_error_t SrsSRTP::protect_rtp(void* packet, int* nb_cipher)
{
    srs_error_t err = srs_success;

    // If DTLS/SRTP is not ready, fail.
    if (!send_ctx_) {
        return srs_error_new(ERROR_RTC_SRTP_PROTECT, "not ready");
    }

    srtp_err_status_t r0 = srtp_err_status_ok;
    if ((r0 = srtp_protect(send_ctx_, packet, nb_cipher)) != srtp_err_status_ok) {
        return srs_error_new(ERROR_RTC_SRTP_PROTECT, "rtp protect r0=%u", r0);
    }

    return err;
}

srs_error_t SrsSRTP::protect_rtcp(void* packet, int* nb_cipher)
{
    srs_error_t err = srs_success;

    // If DTLS/SRTP is not ready, fail.
    if (!send_ctx_) {
        return srs_error_new(ERROR_RTC_SRTP_PROTECT, "not ready");
    }

    srtp_err_status_t r0 = srtp_err_status_ok;
    if ((r0 = srtp_protect_rtcp(send_ctx_, packet, nb_cipher)) != srtp_err_status_ok) {
        return srs_error_new(ERROR_RTC_SRTP_PROTECT, "rtcp protect r0=%u", r0);
    }

    return err;
}

srs_error_t SrsSRTP::unprotect_rtp(void* packet, int* nb_plaintext)
{
    srs_error_t err = srs_success;

    // If DTLS/SRTP is not ready, fail.
    if (!recv_ctx_) {
        return srs_error_new(ERROR_RTC_SRTP_UNPROTECT, "not ready");
    }

    srtp_err_status_t r0 = srtp_err_status_ok;
    if ((r0 = srtp_unprotect(recv_ctx_, packet, nb_plaintext)) != srtp_err_status_ok) {
        return srs_error_new(ERROR_RTC_SRTP_UNPROTECT, "rtp unprotect r0=%u", r0);
    }

    return err;
}

srs_error_t SrsSRTP::unprotect_rtcp(void* packet, int* nb_plaintext)
{
    srs_error_t err = srs_success;

    // If DTLS/SRTP is not ready, fail.
    if (!recv_ctx_) {
        return srs_error_new(ERROR_RTC_SRTP_UNPROTECT, "not ready");
    }

    srtp_err_status_t r0 = srtp_err_status_ok;
    if ((r0 = srtp_unprotect_rtcp(recv_ctx_, packet, nb_plaintext)) != srtp_err_status_ok) {
        return srs_error_new(ERROR_RTC_SRTP_UNPROTECT, "rtcp unprotect r0=%u", r0);
    }

    return err;
}

