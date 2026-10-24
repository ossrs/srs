#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <stdio.h>

int main()
{
    SSL_CTX *ctx = SSL_CTX_new(DTLS_method());
    EVP_PKEY *pkey = EVP_EC_gen("prime256v1");
    X509 *cert = X509_new();
    if (!ctx || !pkey || !cert) {
        printf("DTLS context or key failed\n");
        return 1;
    }

    X509_NAME *name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char *)"ossrs.net", -1, -1, 0);
    X509_set_issuer_name(cert, name);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    X509_gmtime_adj(X509_getm_notBefore(cert), 0);
    X509_gmtime_adj(X509_getm_notAfter(cert), 365 * 24 * 3600);
    X509_set_version(cert, 2);
    if (X509_set_pubkey(cert, pkey) != 1 || X509_sign(cert, pkey, EVP_sha256()) == 0) {
        printf("self-signed certificate failed\n");
        return 1;
    }
    if (SSL_CTX_use_certificate(ctx, cert) != 1 || SSL_CTX_use_PrivateKey(ctx, pkey) != 1 ||
        SSL_CTX_check_private_key(ctx) != 1) {
        printf("DTLS context rejects the certificate\n");
        return 1;
    }

    X509_free(cert);
    EVP_PKEY_free(pkey);
    SSL_CTX_free(ctx);
    printf("%s DTLS context with a self-signed certificate\n", OpenSSL_version(OPENSSL_VERSION));
    return 0;
}
