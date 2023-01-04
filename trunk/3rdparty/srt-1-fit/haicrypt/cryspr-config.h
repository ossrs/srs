#ifndef INC_SRT_CRYSPR_CONFIG_H
#define INC_SRT_CRYSPR_CONFIG_H

// Size of the single block for encryption.
// This might need tweaking for particular implementation library.
#define CRYSPR_AESBLKSZ 16              /* 128-bit */

#if defined(USE_OPENSSL)
#include "cryspr-openssl.h"
#define cryspr4SRT()  crysprOpenSSL()
#define CRYSPR_IMPL_DESC "OpenSSL-AES"
#elif defined(USE_OPENSSL_EVP)
#include "cryspr-openssl-evp.h"
#define cryspr4SRT()  crysprOpenSSL_EVP()
#define CRYSPR_IMPL_DESC "OpenSSL-EVP"
#elif defined(USE_GNUTLS)
#include "cryspr-gnutls.h"
#define cryspr4SRT()  crysprGnuTLS()
#define CRYSPR_IMPL_DESC "GnuTLS"
#elif defined(USE_MBEDTLS)
#include "cryspr-mbedtls.h"
#define cryspr4SRT() crysprMbedtls()
#define CRYSPR_IMPL_DESC "MbedTLS"
#else
#error Cryspr implementation not selected. Please define USE_* + OPENSSL/GNUTLS/MBEDTLS.
#define CRYSPR_IMPL_DESC "No Cipher"
#endif


#endif
