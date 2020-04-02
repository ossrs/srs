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

#include <srtp2/srtp.h>

SrsDtls* SrsDtls::_instance = NULL;

SrsDtls::SrsDtls()
{
}

SrsDtls::~SrsDtls()
{
}

SrsDtls* SrsDtls::instance()
{
    if (!_instance) {
        _instance = new SrsDtls();
        _instance->init();
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

void SrsDtls::init()
{
    // srtp init first
    srs_assert(srtp_init() == 0);

    // init dtls context
	EVP_PKEY* dtls_private_key = EVP_PKEY_new();
    srs_assert(dtls_private_key);

    RSA* rsa = RSA_new();
    srs_assert(rsa);

    BIGNUM* exponent = BN_new();
    srs_assert(exponent);

    BN_set_word(exponent, RSA_F4);

	const std::string& aor = "www.hw.com";
	int expire_day = 365;
	int private_key_len = 1024;

    RSA_generate_key_ex(rsa, private_key_len, exponent, NULL);

    srs_assert(EVP_PKEY_set1_RSA(dtls_private_key, rsa) == 1);

    X509* dtls_cert = X509_new();
    srs_assert(dtls_cert);

    X509_NAME* subject = X509_NAME_new();
    srs_assert(subject);

    int serial = rand();
    ASN1_INTEGER_set(X509_get_serialNumber(dtls_cert), serial);

    X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC, (unsigned char *) aor.data(), aor.size(), -1, 0);

    X509_set_issuer_name(dtls_cert, subject);
    X509_set_subject_name(dtls_cert, subject);

    const long cert_duration = 60*60*24*expire_day;

    X509_gmtime_adj(X509_get_notBefore(dtls_cert), 0);
    X509_gmtime_adj(X509_get_notAfter(dtls_cert), cert_duration);

    srs_assert(X509_set_pubkey(dtls_cert, dtls_private_key) == 1);

    srs_assert(X509_sign(dtls_cert, dtls_private_key, EVP_sha1()) != 0);

    // cleanup
    RSA_free(rsa);
    BN_free(exponent);
    X509_NAME_free(subject);

    dtls_ctx = SSL_CTX_new(DTLS_method());
	srs_assert(SSL_CTX_use_certificate(dtls_ctx, dtls_cert) == 1);

    srs_assert(SSL_CTX_use_PrivateKey(dtls_ctx, dtls_private_key) == 1);
    srs_assert(SSL_CTX_set_cipher_list(dtls_ctx, "ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH") == 1);
    srs_assert(SSL_CTX_set_tlsext_use_srtp(dtls_ctx, "SRTP_AES128_CM_SHA1_80") == 0);

    // Server will send Certificate Request.
    // @see https://www.openssl.org/docs/man1.0.2/man3/SSL_CTX_set_verify.html
    // TODO: FIXME: Config it, default to off to make the packet smaller.
    SSL_CTX_set_verify(dtls_ctx, SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE, verify_callback);

    // The depth count is "level 0:peer certificate", "level 1: CA certificate",
    // "level 2: higher level CA certificate", and so on.
    // @see https://www.openssl.org/docs/man1.0.2/man3/SSL_CTX_set_verify.html
    SSL_CTX_set_verify_depth (dtls_ctx, 4);

    SSL_CTX_set_read_ahead(dtls_ctx, 1);

    // dtls fingerprint
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
