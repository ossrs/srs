#ifndef INC__CRYSPR_CONFIG_H
#define INC__CRYSPR_CONFIG_H

// Size of the single block for encryption.
// This might need tweaking for particular implementation library.
#define CRYSPR_AESBLKSZ 16              /* 128-bit */

#if defined(USE_OPENSSL)
#include "cryspr-openssl.h"
#define cryspr4SRT()  crysprOpenSSL()
#elif defined(USE_GNUTLS)
#include "cryspr-gnutls.h"
#define cryspr4SRT()  crysprGnuTLS()
#elif defined(USE_MBEDTLS)
#include "cryspr-mbedtls.h"
#define cryspr4SRT() crysprMbedtls()
#else
#error Cryspr implementation not selected. Please define USE_* + OPENSSL/GNUTLS/MBEDTLS.
#endif


#endif
