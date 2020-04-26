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

#include <srs_app_dtls.hpp>

using namespace std;

#include <string.h>

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_config.hpp>

#include <srtp2/srtp.h>
#include <openssl/ssl.h>

SrsDtls* SrsDtls::_instance = NULL;

SrsDtls::SrsDtls()
{
    dtls_ctx = NULL;
}

SrsDtls::~SrsDtls()
{
    SSL_CTX_free(dtls_ctx);
}

SrsDtls* SrsDtls::instance()
{
    if (!_instance) {
        _instance = new SrsDtls();
    }   
    return _instance;
}

// The return value of verify_callback controls the strategy of the further verification process. If verify_callback
// returns 0, the verification process is immediately stopped with "verification failed" state. If SSL_VERIFY_PEER is
// set, a verification failure alert is sent to the peer and the TLS/SSL handshake is terminated. If verify_callback
// returns 1, the verification process is continued. If verify_callback always returns 1, the TLS/SSL handshake will
// not be terminated with respect to verification failures and the connection will be established. The calling process
// can however retrieve the error code of the last verification error using SSL_get_verify_result(3) or by maintaining
// its own error storage managed by verify_callback.
// @see https://www.openssl.org/docs/man1.0.2/man3/SSL_CTX_set_verify.html
static int verify_callback(int preverify_ok, X509_STORE_CTX *ctx)
{
    // Always OK, we don't check the certificate of client,
    // because we allow client self-sign certificate.
    return 1;
}

srs_error_t SrsDtls::init(const SrsRequest& req)
{
    srs_error_t err = srs_success;

    // Initialize once.
    if (dtls_ctx) {
        return err;
    }

#if OPENSSL_VERSION_NUMBER < 0x10100000L // v1.1.x
    // Initialize SSL library by registering algorithms
    // The SSL_library_init() and OpenSSL_add_ssl_algorithms() functions were deprecated in OpenSSL 1.1.0 by OPENSSL_init_ssl().
    // @see https://www.openssl.org/docs/man1.1.0/man3/OpenSSL_add_ssl_algorithms.html
    // @see https://web.archive.org/web/20150806185102/http://sctp.fh-muenster.de:80/dtls/dtls_udp_echo.c
    OpenSSL_add_ssl_algorithms();
#endif

#if OPENSSL_VERSION_NUMBER < 0x10002000L // v1.0.2
    dtls_ctx = SSL_CTX_new(DTLSv1_method());
#else
    //dtls_ctx = SSL_CTX_new(DTLS_method());
    dtls_ctx = SSL_CTX_new(DTLSv1_method());
    //dtls_ctx = SSL_CTX_new(DTLSv1_2_method());
#endif

    // Initialize SRTP first.
    srs_assert(srtp_init() == 0);

    // Whether use ECDSA certificate.
    bool is_ecdsa = _srs_config->get_rtc_server_ecdsa();

    // Create keys by RSA or ECDSA.
    EVP_PKEY* dtls_pkey = EVP_PKEY_new();
    srs_assert(dtls_pkey);
    if (!is_ecdsa) { // By RSA
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
    if (is_ecdsa) { // By ECDSA, https://stackoverflow.com/a/6006898
        EC_KEY* eckey = EC_KEY_new();
        srs_assert(eckey);

#if OPENSSL_VERSION_NUMBER >= 0x10002000L // v1.0.2
        // For ECDSA, we could set the curves list.
        // @see https://www.openssl.org/docs/man1.0.2/man3/SSL_CTX_set1_curves_list.html
        SSL_CTX_set1_curves_list(dtls_ctx, "P-521:P-384:P-256");
#endif
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

        // For openssl <1.1, we must set the ECDH manually.
        // @see https://stackoverrun.com/cn/q/10791887
#if OPENSSL_VERSION_NUMBER < 0x10100000L // v1.1.x
    #if OPENSSL_VERSION_NUMBER < 0x10002000L // v1.0.2
        SSL_CTX_set_tmp_ecdh(dtls_ctx, eckey);
    #else
        SSL_CTX_set_ecdh_auto(dtls_ctx, 1);
    #endif
#endif
        // @see https://www.openssl.org/docs/man1.1.0/man3/EVP_PKEY_type.html
        srs_assert(EVP_PKEY_set1_EC_KEY(dtls_pkey, eckey) == 1);

        EC_GROUP_free(ecgroup);
        EC_KEY_free(eckey);
    }

    // Create certificate, from previous generated pkey.
    // TODO: Support ECDSA certificate.
    X509* dtls_cert = X509_new();
    srs_assert(dtls_cert);
    if (true) {
        X509_NAME* subject = X509_NAME_new();
        srs_assert(subject);

        int serial = rand();
        ASN1_INTEGER_set(X509_get_serialNumber(dtls_cert), serial);

        const std::string& aor = "ossrs.net";
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

    // Setup DTLS context.
    if (true) {
        // We use "ALL", while you can use "DEFAULT" means "ALL:!EXPORT:!LOW:!aNULL:!eNULL:!SSLv2"
        // @see https://www.openssl.org/docs/man1.0.2/man1/ciphers.html
        srs_assert(SSL_CTX_set_cipher_list(dtls_ctx, "ALL") == 1);

        // Setup the certificate.
        srs_assert(SSL_CTX_use_certificate(dtls_ctx, dtls_cert) == 1);
        srs_assert(SSL_CTX_use_PrivateKey(dtls_ctx, dtls_pkey) == 1);

        // Server will send Certificate Request.
        // @see https://www.openssl.org/docs/man1.0.2/man3/SSL_CTX_set_verify.html
        // TODO: FIXME: Config it, default to off to make the packet smaller.
        SSL_CTX_set_verify(dtls_ctx, SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE, verify_callback);
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

    // Show DTLS fingerprint
    if (true) {
        char fp[100] = {0};
        char *p = fp;
        unsigned char md[EVP_MAX_MD_SIZE];
        unsigned int n = 0;

        // TODO: FIXME: Unused variable.
        /*int r = */X509_digest(dtls_cert, EVP_sha256(), md, &n);

        for (unsigned int i = 0; i < n; i++, ++p) {
            sprintf(p, "%02X", md[i]);
            p += 2;

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
