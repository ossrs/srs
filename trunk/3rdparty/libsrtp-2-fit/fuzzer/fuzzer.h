#define MAX_KEY_LEN 46
#define EXTRACT(dest, src, srcsize, copysize)                                  \
    {                                                                          \
        memcpy((dest), (src), (copysize));                                     \
        (src) += (copysize);                                                   \
        (srcsize) -= (copysize);                                               \
    }

/* Extract data if src contains sufficient bytes, otherwise go to end */
#define EXTRACT_IF(dest, src, srcsize, copysize)                               \
    {                                                                          \
        if ((srcsize) < (copysize)) {                                          \
            goto end;                                                          \
        } else {                                                               \
            EXTRACT((dest), (src), (srcsize), (copysize));                     \
        }                                                                      \
    }
#include <stdint.h>
#if UINTPTR_MAX == 0xffffffff
#define FUZZ_32BIT
#elif UINTPTR_MAX == 0xffffffffffffffff
#else
#error "Cannot detect word size"
#endif

typedef srtp_err_status_t (
    *fuzz_srtp_func)(srtp_t, void *, int *, uint8_t, unsigned int);
typedef void (*fuzz_srtp_crypto_policy_func)(srtp_crypto_policy_t *);
typedef srtp_err_status_t (*fuzz_srtp_get_length_func)(const srtp_t,
                                                       uint8_t,
                                                       unsigned int,
                                                       uint32_t *);

struct fuzz_srtp_params {
    uint8_t srtp_func;
    uint8_t srtp_crypto_policy_func;
    uint16_t window_size;
    uint8_t allow_repeat_tx;
    uint8_t ssrc_type;
    unsigned int ssrc_value;
    uint8_t key[MAX_KEY_LEN];
    uint8_t mki;
};

static srtp_err_status_t fuzz_srtp_protect(srtp_t srtp_sender,
                                           void *hdr,
                                           int *len,
                                           uint8_t use_mki,
                                           unsigned int mki);
static srtp_err_status_t fuzz_srtp_unprotect(srtp_t srtp_sender,
                                             void *hdr,
                                             int *len,
                                             uint8_t use_mki,
                                             unsigned int mki);
static srtp_err_status_t fuzz_srtp_protect_rtcp(srtp_t srtp_sender,
                                                void *hdr,
                                                int *len,
                                                uint8_t use_mki,
                                                unsigned int mki);
static srtp_err_status_t fuzz_srtp_unprotect_rtcp(srtp_t srtp_sender,
                                                  void *hdr,
                                                  int *len,
                                                  uint8_t use_mki,
                                                  unsigned int mki);
static srtp_err_status_t fuzz_srtp_protect_mki(srtp_t srtp_sender,
                                               void *hdr,
                                               int *len,
                                               uint8_t use_mki,
                                               unsigned int mki);
static srtp_err_status_t fuzz_srtp_protect_rtcp_mki(srtp_t srtp_sender,
                                                    void *hdr,
                                                    int *len,
                                                    uint8_t use_mki,
                                                    unsigned int mki);
static srtp_err_status_t fuzz_srtp_unprotect_mki(srtp_t srtp_sender,
                                                 void *hdr,
                                                 int *len,
                                                 uint8_t use_mki,
                                                 unsigned int mki);
static srtp_err_status_t fuzz_srtp_unprotect_rtcp_mki(srtp_t srtp_sender,
                                                      void *hdr,
                                                      int *len,
                                                      uint8_t use_mki,
                                                      unsigned int mki);

static srtp_err_status_t fuzz_srtp_get_protect_length(const srtp_t srtp_ctx,
                                                      uint8_t use_mki,
                                                      unsigned int mki,
                                                      uint32_t *length);
static srtp_err_status_t fuzz_srtp_get_protect_mki_length(const srtp_t srtp_ctx,
                                                          uint8_t use_mki,
                                                          unsigned int mki,
                                                          uint32_t *length);
static srtp_err_status_t fuzz_srtp_get_protect_rtcp_length(
    const srtp_t srtp_ctx,
    uint8_t use_mki,
    unsigned int mki,
    uint32_t *length);
static srtp_err_status_t fuzz_srtp_get_protect_rtcp_mki_length(
    const srtp_t srtp_ctx,
    uint8_t use_mki,
    unsigned int mki,
    uint32_t *length);

struct fuzz_srtp_func_ext {
    fuzz_srtp_func srtp_func;
    bool protect;
    fuzz_srtp_get_length_func get_length;
};

const struct fuzz_srtp_func_ext srtp_funcs[] = {
    { fuzz_srtp_protect, true, fuzz_srtp_get_protect_length },
    { fuzz_srtp_unprotect, false, NULL },
    { fuzz_srtp_protect_rtcp, true, fuzz_srtp_get_protect_rtcp_length },
    { fuzz_srtp_unprotect_rtcp, false, NULL },
    { fuzz_srtp_protect_mki, true, fuzz_srtp_get_protect_mki_length },
    { fuzz_srtp_unprotect_mki, false, NULL },
    { fuzz_srtp_protect_rtcp_mki, true, fuzz_srtp_get_protect_rtcp_mki_length },
    { fuzz_srtp_unprotect_rtcp_mki, false, NULL }
};

struct fuzz_srtp_crypto_policy_func_ext {
    fuzz_srtp_crypto_policy_func crypto_policy_func;
    const char *name;
};

const struct fuzz_srtp_crypto_policy_func_ext fuzz_srtp_crypto_policies[] = {
    { srtp_crypto_policy_set_rtp_default, "" },
    { srtp_crypto_policy_set_rtcp_default, "" },
    { srtp_crypto_policy_set_aes_cm_128_hmac_sha1_32,
      "srtp_crypto_policy_set_aes_cm_128_hmac_sha1_32" },
    { srtp_crypto_policy_set_aes_cm_128_null_auth,
      "srtp_crypto_policy_set_aes_cm_128_null_auth" },
    { srtp_crypto_policy_set_aes_cm_256_hmac_sha1_32,
      "srtp_crypto_policy_set_aes_cm_256_hmac_sha1_32" },
    { srtp_crypto_policy_set_aes_cm_256_hmac_sha1_80,
      "srtp_crypto_policy_set_aes_cm_256_hmac_sha1_80" },
    { srtp_crypto_policy_set_aes_cm_256_null_auth,
      "srtp_crypto_policy_set_aes_cm_256_null_auth" },
    { srtp_crypto_policy_set_null_cipher_hmac_null,
      "srtp_crypto_policy_set_null_cipher_hmac_null" },
    { srtp_crypto_policy_set_null_cipher_hmac_sha1_80,
      "srtp_crypto_policy_set_null_cipher_hmac_sha1_80" },
#ifdef OPENSSL
    { srtp_crypto_policy_set_aes_cm_192_hmac_sha1_32,
      "srtp_crypto_policy_set_aes_cm_192_hmac_sha1_32" },
    { srtp_crypto_policy_set_aes_cm_192_hmac_sha1_80,
      "srtp_crypto_policy_set_aes_cm_192_hmac_sha1_80" },
    { srtp_crypto_policy_set_aes_cm_192_null_auth,
      "srtp_crypto_policy_set_aes_cm_192_null_auth" },
    { srtp_crypto_policy_set_aes_gcm_128_16_auth,
      "srtp_crypto_policy_set_aes_gcm_128_16_auth" },
    { srtp_crypto_policy_set_aes_gcm_128_8_auth,
      "srtp_crypto_policy_set_aes_gcm_128_8_auth" },
    { srtp_crypto_policy_set_aes_gcm_128_8_only_auth,
      "srtp_crypto_policy_set_aes_gcm_128_8_only_auth" },
    { srtp_crypto_policy_set_aes_gcm_256_16_auth,
      "srtp_crypto_policy_set_aes_gcm_256_16_auth" },
    { srtp_crypto_policy_set_aes_gcm_256_8_auth,
      "srtp_crypto_policy_set_aes_gcm_256_8_auth" },
    { srtp_crypto_policy_set_aes_gcm_256_8_only_auth,
      "srtp_crypto_policy_set_aes_gcm_256_8_only_auth" },
#endif
};

struct fuzz_srtp_ssrc_type_ext {
    srtp_ssrc_type_t srtp_ssrc_type;
    const char *name;
};

const struct fuzz_srtp_ssrc_type_ext fuzz_ssrc_type_map[] = {
    { ssrc_undefined, "ssrc_undefined" },
    { ssrc_specific, "ssrc_specific" },
    { ssrc_any_inbound, "ssrc_any_inbound" },
    { ssrc_any_outbound, "ssrc_any_outbound" },
};
