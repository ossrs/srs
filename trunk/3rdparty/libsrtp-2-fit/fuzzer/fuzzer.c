/* By Guido Vranken <guidovranken@gmail.com> --
 * https://guidovranken.wordpress.com/ */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <limits.h>
#include "srtp.h"
#include "srtp_priv.h"
#include "ekt.h"
#include "fuzzer.h"
#include "mt19937.h"
#include "testmem.h"

/* Global variables */
static bool g_no_align = false; /* Can be enabled with --no_align */
static bool g_post_init =
    false; /* Set to true once past initialization phase */
static bool g_write_input = false;

#ifdef FUZZ_32BIT
#include <sys/mman.h>
static bool g_no_mmap = false; /* Can be enabled with --no_mmap */
static void *g_mmap_allocation =
    NULL; /* Keeps current mmap() allocation address */
static size_t g_mmap_allocation_size =
    0; /* Keeps current mmap() allocation size */
#endif

/* Custom allocator functions */

static void *fuzz_alloc(const size_t size, const bool do_zero)
{
    void *ret = NULL;
#ifdef FUZZ_32BIT
    bool do_malloc = true;
#endif
    bool do_mmap, mmap_high = true;

    if (size == 0) {
        size_t ret;
        /* Allocations of size 0 are not illegal, but are a bad practice, since
         * writing just a single byte to this region constitutes undefined
         * behavior per the C spec. glibc will return a small, valid memory
         * region
         * whereas OpenBSD will crash upon writing to it.
         * Intentionally return a pointer to an invalid page to detect
         * unsound code efficiently.
         * fuzz_free is aware of this pointer range and will not attempt
         * to free()/munmap() it.
         */
        ret = 0x01 + (fuzz_mt19937_get() % 1024);
        return (void *)ret;
    }

    /* Don't do mmap()-based allocations during initialization */
    if (g_post_init == true) {
        /* Even extract these values if --no_mmap is specified.
         * This keeps the PRNG output stream consistent across
         * fuzzer configurations.
         */
        do_mmap = (fuzz_mt19937_get() % 64) == 0 ? true : false;
        if (do_mmap == true) {
            mmap_high = (fuzz_mt19937_get() % 2) == 0 ? true : false;
        }
    } else {
        do_mmap = false;
    }

#ifdef FUZZ_32BIT
    /* g_mmap_allocation must be NULL because we only support a single
     * concurrent mmap allocation at a time
     */
    if (g_mmap_allocation == NULL && g_no_mmap == false && do_mmap == true) {
        void *mmap_address;
        if (mmap_high == true) {
            mmap_address = (void *)0xFFFF0000;
        } else {
            mmap_address = (void *)0x00010000;
        }
        g_mmap_allocation_size = size;

        ret = mmap(mmap_address, g_mmap_allocation_size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (ret == MAP_FAILED) {
            /* That's okay -- just return NULL to the caller */

            ret = NULL;

            /* Reset this for the sake of cleanliness */
            g_mmap_allocation_size = 0;
        }
        /* ret not being MAP_FAILED does not mean that ret is the requested
         * address (mmap_address). That's okay. We're not going to perform
         * a munmap() on it and call malloc() instead. It won't gain us
         * anything.
         */

        g_mmap_allocation = ret;
        do_malloc = false;
    }

    if (do_malloc == true)
#endif
    {
        ret = malloc(size);
    }

    /* Mimic calloc() if so requested */
    if (ret != NULL && do_zero) {
        memset(ret, 0, size);
    }

    return ret;
}

/* Internal allocations by this fuzzer must on one hand (sometimes)
 * receive memory from mmap(), but on the other hand these requests for
 * memory may not fail. By calling this function, the allocation is
 * guaranteed to succeed; it first tries with fuzz_alloc(), which may
 * fail if it uses mmap(), and if that is the case, memory is allocated
 * via the libc allocator (malloc, calloc) which should always succeed */
static void *fuzz_alloc_succeed(const size_t size, const bool do_zero)
{
    void *ret = fuzz_alloc(size, do_zero);
    if (ret == NULL) {
        if (do_zero == false) {
            ret = malloc(size);
        } else {
            ret = calloc(1, size);
        }
    }

    return ret;
}

void *fuzz_calloc(const size_t nmemb, const size_t size)
{
    /* We must be past srtp_init() to prevent that that function fails */
    if (g_post_init == true) {
        /* Fail 1 in 64 allocations on average to test whether the library
         * can deal with this properly.
         */
        if ((fuzz_mt19937_get() % 64) == 0) {
            return NULL;
        }
    }

    return fuzz_alloc(nmemb * size, true);
}

static bool fuzz_is_special_pointer(void *ptr)
{
    /* Special, invalid pointers introduced when code attempted
     * to do size = 0 allocations.
     */
    if ((size_t)ptr >= 0x01 && (size_t)ptr < (0x01 + 1024)) {
        return true;
    } else {
        return false;
    }
}

void fuzz_free(void *ptr)
{
    if (fuzz_is_special_pointer(ptr) == true) {
        return;
    }

#ifdef FUZZ_32BIT
    if (g_post_init == true && ptr != NULL && ptr == g_mmap_allocation) {
        if (munmap(g_mmap_allocation, g_mmap_allocation_size) == -1) {
            /* Shouldn't happen */
            abort();
        }
        g_mmap_allocation = NULL;
    } else
#endif
    {
        free(ptr);
    }
}

static srtp_err_status_t fuzz_srtp_protect(srtp_t srtp_sender,
                                           void *hdr,
                                           int *len,
                                           uint8_t use_mki,
                                           unsigned int mki)
{
    return srtp_protect(srtp_sender, hdr, len);
}

static srtp_err_status_t fuzz_srtp_unprotect(srtp_t srtp_sender,
                                             void *hdr,
                                             int *len,
                                             uint8_t use_mki,
                                             unsigned int mki)
{
    return srtp_unprotect(srtp_sender, hdr, len);
}

static srtp_err_status_t fuzz_srtp_protect_rtcp(srtp_t srtp_sender,
                                                void *hdr,
                                                int *len,
                                                uint8_t use_mki,
                                                unsigned int mki)
{
    return srtp_protect_rtcp(srtp_sender, hdr, len);
}

static srtp_err_status_t fuzz_srtp_unprotect_rtcp(srtp_t srtp_sender,
                                                  void *hdr,
                                                  int *len,
                                                  uint8_t use_mki,
                                                  unsigned int mki)
{
    return srtp_unprotect_rtcp(srtp_sender, hdr, len);
}

static srtp_err_status_t fuzz_srtp_protect_mki(srtp_t srtp_sender,
                                               void *hdr,
                                               int *len,
                                               uint8_t use_mki,
                                               unsigned int mki)
{
    return srtp_protect_mki(srtp_sender, hdr, len, use_mki, mki);
}

static srtp_err_status_t fuzz_srtp_protect_rtcp_mki(srtp_t srtp_sender,
                                                    void *hdr,
                                                    int *len,
                                                    uint8_t use_mki,
                                                    unsigned int mki)
{
    return srtp_protect_rtcp_mki(srtp_sender, hdr, len, use_mki, mki);
}

static srtp_err_status_t fuzz_srtp_unprotect_mki(srtp_t srtp_sender,
                                                 void *hdr,
                                                 int *len,
                                                 uint8_t use_mki,
                                                 unsigned int mki)
{
    return srtp_unprotect_mki(srtp_sender, hdr, len, use_mki);
}

static srtp_err_status_t fuzz_srtp_unprotect_rtcp_mki(srtp_t srtp_sender,
                                                      void *hdr,
                                                      int *len,
                                                      uint8_t use_mki,
                                                      unsigned int mki)
{
    return srtp_unprotect_rtcp_mki(srtp_sender, hdr, len, use_mki);
}

/* Get protect length functions */

static srtp_err_status_t fuzz_srtp_get_protect_length(const srtp_t srtp_ctx,
                                                      uint8_t use_mki,
                                                      unsigned int mki,
                                                      uint32_t *length)
{
    return srtp_get_protect_trailer_length(srtp_ctx, 0, 0, length);
}

static srtp_err_status_t fuzz_srtp_get_protect_rtcp_length(
    const srtp_t srtp_ctx,
    uint8_t use_mki,
    unsigned int mki,
    uint32_t *length)
{
    return srtp_get_protect_rtcp_trailer_length(srtp_ctx, 0, 0, length);
}

static srtp_err_status_t fuzz_srtp_get_protect_mki_length(const srtp_t srtp_ctx,
                                                          uint8_t use_mki,
                                                          unsigned int mki,
                                                          uint32_t *length)
{
    return srtp_get_protect_trailer_length(srtp_ctx, use_mki, mki, length);
}

static srtp_err_status_t fuzz_srtp_get_protect_rtcp_mki_length(
    const srtp_t srtp_ctx,
    uint8_t use_mki,
    unsigned int mki,
    uint32_t *length)
{
    return srtp_get_protect_rtcp_trailer_length(srtp_ctx, use_mki, mki, length);
}

static uint8_t *extract_key(const uint8_t **data,
                            size_t *size,
                            const size_t key_size)
{
    uint8_t *ret;
    if (*size < key_size) {
        return NULL;
    }

    ret = fuzz_alloc_succeed(key_size, false);
    EXTRACT(ret, *data, *size, key_size);

    return ret;
}

static srtp_master_key_t *extract_master_key(const uint8_t **data,
                                             size_t *size,
                                             const size_t key_size,
                                             bool simulate,
                                             bool *success)
{
    srtp_master_key_t *ret = NULL;
    uint16_t mki_id_size;

    if (simulate == true) {
        *success = false;
    }

    EXTRACT_IF(&mki_id_size, *data, *size, sizeof(mki_id_size));

    if (*size < key_size + mki_id_size) {
        goto end;
    }

    if (simulate == true) {
        *data += key_size + mki_id_size;
        *size -= key_size + mki_id_size;
        *success = true;
        goto end;
    }

    ret = fuzz_alloc_succeed(sizeof(srtp_master_key_t), false);
    ret->key = fuzz_alloc_succeed(key_size, false);

    ret->mki_id = fuzz_alloc_succeed(mki_id_size, false);

    EXTRACT(ret->key, *data, *size, key_size);
    EXTRACT(ret->mki_id, *data, *size, mki_id_size);
    ret->mki_size = mki_id_size;
end:
    return ret;
}

static srtp_master_key_t **extract_master_keys(const uint8_t **data,
                                               size_t *size,
                                               const size_t key_size,
                                               unsigned long *num_master_keys)
{
    const uint8_t *data_orig = *data;
    size_t size_orig = *size;
    size_t i = 0;

    srtp_master_key_t **ret = NULL;

    *num_master_keys = 0;

    /* First pass -- dry run, determine how many keys we want and can extract */
    while (1) {
        uint8_t do_extract_master_key;
        bool success;
        if (*size < sizeof(do_extract_master_key)) {
            goto next;
        }
        EXTRACT(&do_extract_master_key, *data, *size,
                sizeof(do_extract_master_key));

        /* Decide whether to extract another key */
        if ((do_extract_master_key % 2) == 0) {
            break;
        }

        extract_master_key(data, size, key_size, true, &success);

        if (success == false) {
            break;
        }

        (*num_master_keys)++;
    }

next:
    *data = data_orig;
    *size = size_orig;

    /* Allocate array of pointers */
    ret = fuzz_alloc_succeed(*num_master_keys * sizeof(srtp_master_key_t *),
                             false);

    /* Second pass -- perform the actual extractions */
    for (i = 0; i < *num_master_keys; i++) {
        uint8_t do_extract_master_key;
        EXTRACT_IF(&do_extract_master_key, *data, *size,
                   sizeof(do_extract_master_key));

        if ((do_extract_master_key % 2) == 0) {
            break;
        }

        ret[i] = extract_master_key(data, size, key_size, false, NULL);

        if (ret[i] == NULL) {
            /* Shouldn't happen */
            abort();
        }
    }

end:
    return ret;
}

static srtp_ekt_policy_t extract_ekt_policy(const uint8_t **data, size_t *size)
{
    srtp_ekt_policy_t ret = NULL;
    struct {
        srtp_ekt_spi_t spi;
        uint8_t key[16];

    } params;

    EXTRACT_IF(&params, *data, *size, sizeof(params));

    ret = fuzz_alloc_succeed(sizeof(struct srtp_ekt_policy_ctx_t), false);

    ret->spi = params.spi;

    /* The only supported cipher type */
    ret->ekt_cipher_type = SRTP_EKT_CIPHER_AES_128_ECB;

    ret->ekt_key = fuzz_alloc_succeed(sizeof(params.key), false);
    memcpy(ret->ekt_key, params.key, sizeof(params.key));

    ret->next_ekt_policy = NULL;

end:
    return ret;
}

static srtp_policy_t *extract_policy(const uint8_t **data, size_t *size)
{
    srtp_policy_t *policy = NULL;
    struct {
        uint8_t srtp_crypto_policy_func;
        uint64_t window_size;
        uint8_t allow_repeat_tx;
        uint8_t ssrc_type;
        uint32_t ssrc_value;
        uint8_t num_xtn_hdr;
        uint8_t with_ekt;
        srtp_ekt_spi_t ekt_spi;
        uint8_t do_extract_key;
        uint8_t do_extract_master_keys;
    } params;

    EXTRACT_IF(&params, *data, *size, sizeof(params));

    params.srtp_crypto_policy_func %= sizeof(fuzz_srtp_crypto_policies) /
                                      sizeof(fuzz_srtp_crypto_policies[0]);
    params.allow_repeat_tx %= 2;
    params.ssrc_type %=
        sizeof(fuzz_ssrc_type_map) / sizeof(fuzz_ssrc_type_map[0]);
    params.with_ekt %= 2;

    policy = fuzz_alloc_succeed(sizeof(*policy), true);

    fuzz_srtp_crypto_policies[params.srtp_crypto_policy_func]
        .crypto_policy_func(&policy->rtp);
    fuzz_srtp_crypto_policies[params.srtp_crypto_policy_func]
        .crypto_policy_func(&policy->rtcp);

    if (policy->rtp.cipher_key_len > MAX_KEY_LEN) {
        /* Shouldn't happen */
        abort();
    }

    policy->ssrc.type = fuzz_ssrc_type_map[params.ssrc_type].srtp_ssrc_type;
    policy->ssrc.value = params.ssrc_value;

    if ((params.do_extract_key % 2) == 0) {
        policy->key = extract_key(data, size, policy->rtp.cipher_key_len);

        if (policy->key == NULL) {
            fuzz_free(policy);
            return NULL;
        }
    }

    if (params.num_xtn_hdr != 0) {
        const size_t xtn_hdr_size = params.num_xtn_hdr * sizeof(int);
        if (*size < xtn_hdr_size) {
            fuzz_free(policy->key);
            fuzz_free(policy);
            return NULL;
        }
        policy->enc_xtn_hdr = fuzz_alloc_succeed(xtn_hdr_size, false);
        EXTRACT(policy->enc_xtn_hdr, *data, *size, xtn_hdr_size);
        policy->enc_xtn_hdr_count = params.num_xtn_hdr;
    }

    if ((params.do_extract_master_keys % 2) == 0) {
        policy->keys = extract_master_keys(
            data, size, policy->rtp.cipher_key_len, &policy->num_master_keys);
        if (policy->keys == NULL) {
            fuzz_free(policy->key);
            fuzz_free(policy->enc_xtn_hdr);
            fuzz_free(policy);
            return NULL;
        }
    }

    if (params.with_ekt) {
        policy->ekt = extract_ekt_policy(data, size);
    }

    policy->window_size = params.window_size;
    policy->allow_repeat_tx = params.allow_repeat_tx;
    policy->next = NULL;

end:
    return policy;
}

static srtp_policy_t *extract_policies(const uint8_t **data, size_t *size)
{
    srtp_policy_t *curpolicy = NULL, *policy_chain = NULL;

    curpolicy = extract_policy(data, size);
    if (curpolicy == NULL) {
        return NULL;
    }

    policy_chain = curpolicy;

    while (1) {
        uint8_t do_extract_policy;
        EXTRACT_IF(&do_extract_policy, *data, *size, sizeof(do_extract_policy));

        /* Decide whether to extract another policy */
        if ((do_extract_policy % 2) == 0) {
            break;
        }

        curpolicy->next = extract_policy(data, size);
        if (curpolicy->next == NULL) {
            break;
        }
        curpolicy = curpolicy->next;
    }

end:
    return policy_chain;
}

static uint32_t *extract_remove_stream_ssrc(const uint8_t **data,
                                            size_t *size,
                                            uint8_t *num_remove_stream)
{
    uint32_t *ret = NULL;
    uint8_t _num_remove_stream;
    size_t total_size;

    *num_remove_stream = 0;

    EXTRACT_IF(&_num_remove_stream, *data, *size, sizeof(_num_remove_stream));

    if (_num_remove_stream == 0) {
        goto end;
    }

    total_size = _num_remove_stream * sizeof(uint32_t);

    if (*size < total_size) {
        goto end;
    }

    ret = fuzz_alloc_succeed(total_size, false);
    EXTRACT(ret, *data, *size, total_size);

    *num_remove_stream = _num_remove_stream;

end:
    return ret;
}

static uint32_t *extract_set_roc(const uint8_t **data,
                                 size_t *size,
                                 uint8_t *num_set_roc)
{
    uint32_t *ret = NULL;
    uint8_t _num_set_roc;
    size_t total_size;

    *num_set_roc = 0;
    EXTRACT_IF(&_num_set_roc, *data, *size, sizeof(_num_set_roc));
    if (_num_set_roc == 0) {
        goto end;
    }

    /* Tuples of 2 uint32_t's */
    total_size = _num_set_roc * sizeof(uint32_t) * 2;

    if (*size < total_size) {
        goto end;
    }

    ret = fuzz_alloc_succeed(total_size, false);
    EXTRACT(ret, *data, *size, total_size);

    *num_set_roc = _num_set_roc;

end:
    return ret;
}

static void free_policies(srtp_policy_t *curpolicy)
{
    size_t i;
    while (curpolicy) {
        srtp_policy_t *next = curpolicy->next;

        fuzz_free(curpolicy->key);

        for (i = 0; i < curpolicy->num_master_keys; i++) {
            fuzz_free(curpolicy->keys[i]->key);
            fuzz_free(curpolicy->keys[i]->mki_id);
            fuzz_free(curpolicy->keys[i]);
        }

        fuzz_free(curpolicy->keys);
        fuzz_free(curpolicy->enc_xtn_hdr);

        if (curpolicy->ekt) {
            fuzz_free(curpolicy->ekt->ekt_key);
            fuzz_free(curpolicy->ekt);
        }

        fuzz_free(curpolicy);

        curpolicy = next;
    }
}

static uint8_t *run_srtp_func(const srtp_t srtp_ctx,
                              const uint8_t **data,
                              size_t *size)
{
    uint8_t *ret = NULL;
    uint8_t *copy = NULL, *copy_2 = NULL;

    struct {
        uint16_t size;
        uint8_t srtp_func;
        uint8_t use_mki;
        uint32_t mki;
        uint8_t stretch;
    } params_1;

    struct {
        uint8_t srtp_func;
        uint8_t use_mki;
        uint32_t mki;
    } params_2;
    int ret_size;

    EXTRACT_IF(&params_1, *data, *size, sizeof(params_1));
    params_1.srtp_func %= sizeof(srtp_funcs) / sizeof(srtp_funcs[0]);
    params_1.use_mki %= 2;

    if (*size < params_1.size) {
        goto end;
    }

    /* Enforce 4 byte alignment */
    if (g_no_align == false) {
        params_1.size -= params_1.size % 4;
    }

    if (params_1.size == 0) {
        goto end;
    }

    ret_size = params_1.size;
    if (srtp_funcs[params_1.srtp_func].protect == true) {
        /* Intentionally not initialized to trigger MemorySanitizer, if
         * applicable */
        uint32_t alloc_size;

        if (srtp_funcs[params_1.srtp_func].get_length(
                srtp_ctx, params_1.use_mki, params_1.mki, &alloc_size) !=
            srtp_err_status_ok) {
            goto end;
        }

        copy = fuzz_alloc_succeed(ret_size + alloc_size, false);
    } else {
        copy = fuzz_alloc_succeed(ret_size, false);
    }

    EXTRACT(copy, *data, *size, params_1.size);

    if (srtp_funcs[params_1.srtp_func].srtp_func(
            srtp_ctx, copy, &ret_size, params_1.use_mki, params_1.mki) !=
        srtp_err_status_ok) {
        fuzz_free(copy);
        goto end;
    }
    // fuzz_free(copy);

    fuzz_testmem(copy, ret_size);

    ret = copy;

    EXTRACT_IF(&params_2, *data, *size, sizeof(params_2));
    params_2.srtp_func %= sizeof(srtp_funcs) / sizeof(srtp_funcs[0]);
    params_2.use_mki %= 2;

    if (ret_size == 0) {
        goto end;
    }

    if (srtp_funcs[params_2.srtp_func].protect == true) {
        /* Intentionally not initialized to trigger MemorySanitizer, if
         * applicable */
        uint32_t alloc_size;

        if (srtp_funcs[params_2.srtp_func].get_length(
                srtp_ctx, params_2.use_mki, params_2.mki, &alloc_size) !=
            srtp_err_status_ok) {
            goto end;
        }

        copy_2 = fuzz_alloc_succeed(ret_size + alloc_size, false);
    } else {
        copy_2 = fuzz_alloc_succeed(ret_size, false);
    }

    memcpy(copy_2, copy, ret_size);
    fuzz_free(copy);
    copy = copy_2;

    if (srtp_funcs[params_2.srtp_func].srtp_func(
            srtp_ctx, copy, &ret_size, params_2.use_mki, params_2.mki) !=
        srtp_err_status_ok) {
        fuzz_free(copy);
        ret = NULL;
        goto end;
    }

    fuzz_testmem(copy, ret_size);

    ret = copy;

end:
    return ret;
}

void fuzz_srtp_event_handler(srtp_event_data_t *data)
{
    fuzz_testmem(data, sizeof(srtp_event_data_t));
    if (data->session != NULL) {
        fuzz_testmem(data->session, sizeof(*data->session));
    }
}

static void fuzz_write_input(const uint8_t *data, size_t size)
{
    FILE *fp = fopen("input.bin", "wb");

    if (fp == NULL) {
        /* Shouldn't happen */
        abort();
    }

    if (size != 0 && fwrite(data, size, 1, fp) != 1) {
        printf("Cannot write\n");
        /* Shouldn't happen */
        abort();
    }

    fclose(fp);
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    char **_argv = *argv;
    int i;
    bool no_custom_event_handler = false;

    if (srtp_init() != srtp_err_status_ok) {
        /* Shouldn't happen */
        abort();
    }

    for (i = 0; i < *argc; i++) {
        if (strcmp("--no_align", _argv[i]) == 0) {
            g_no_align = true;
        } else if (strcmp("--no_custom_event_handler", _argv[i]) == 0) {
            no_custom_event_handler = true;
        } else if (strcmp("--write_input", _argv[i]) == 0) {
            g_write_input = true;
        }
#ifdef FUZZ_32BIT
        else if (strcmp("--no_mmap", _argv[i]) == 0) {
            g_no_mmap = true;
        }
#endif
        else if (strncmp("--", _argv[i], 2) == 0) {
            printf("Invalid argument: %s\n", _argv[i]);
            exit(0);
        }
    }

    if (no_custom_event_handler == false) {
        if (srtp_install_event_handler(fuzz_srtp_event_handler) !=
            srtp_err_status_ok) {
            /* Shouldn't happen */
            abort();
        }
    }

    /* Fully initialized -- past this point, simulated allocation failures
     * are allowed to occur */
    g_post_init = true;

    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint8_t num_remove_stream;
    uint32_t *remove_stream_ssrc = NULL;
    uint8_t num_set_roc;
    uint32_t *set_roc = NULL;
    srtp_t srtp_ctx = NULL;
    srtp_policy_t *policy_chain = NULL, *policy_chain_2 = NULL;
    uint32_t randseed;
    static bool firstrun = true;

    if (firstrun == true) {
        /* TODO version check etc and send it to MSAN */
    }

#ifdef FUZZ_32BIT
    /* Free the mmap allocation made during the previous iteration, if
     * applicable */
    fuzz_free(g_mmap_allocation);
#endif

    if (g_write_input == true) {
        fuzz_write_input(data, size);
    }

    EXTRACT_IF(&randseed, data, size, sizeof(randseed));
    fuzz_mt19937_init(randseed);
    srand(randseed);

    /* policy_chain is used to initialize the srtp context with */
    if ((policy_chain = extract_policies(&data, &size)) == NULL) {
        goto end;
    }
    /* policy_chain_2 is used as an argument to srtp_update later on */
    if ((policy_chain_2 = extract_policies(&data, &size)) == NULL) {
        goto end;
    }

    /* Create context */
    if (srtp_create(&srtp_ctx, policy_chain) != srtp_err_status_ok) {
        goto end;
    }

    // free_policies(policy_chain);
    // policy_chain = NULL;

    /* Don't check for NULL result -- no extractions is fine */
    remove_stream_ssrc =
        extract_remove_stream_ssrc(&data, &size, &num_remove_stream);

    /* Don't check for NULL result -- no extractions is fine */
    set_roc = extract_set_roc(&data, &size, &num_set_roc);

    {
        uint8_t *ret;
        int i = 0, j = 0;

        while ((ret = run_srtp_func(srtp_ctx, &data, &size)) != NULL) {
            fuzz_free(ret);

            /* Keep removing streams until the set of SSRCs extracted from the
             * fuzzer input is exhausted */
            if (i < num_remove_stream) {
                if (srtp_remove_stream(srtp_ctx, remove_stream_ssrc[i]) !=
                    srtp_err_status_ok) {
                    goto end;
                }
                i++;
            }

            /* Keep setting and getting ROCs until the set of SSRC/ROC tuples
             * extracted from the fuzzer input is exhausted */
            if (j < num_set_roc * 2) {
                uint32_t roc;
                if (srtp_set_stream_roc(srtp_ctx, set_roc[j], set_roc[j + 1]) !=
                    srtp_err_status_ok) {
                    goto end;
                }
                if (srtp_get_stream_roc(srtp_ctx, set_roc[j + 1], &roc) !=
                    srtp_err_status_ok) {
                    goto end;
                }
                j += 2;
            }

            if (policy_chain_2 != NULL) {
                /* TODO srtp_update(srtp_ctx, policy_chain_2); */

                /* Discard after using once */
                free_policies(policy_chain_2);
                policy_chain_2 = NULL;
            }
        }
    }

end:
    free_policies(policy_chain);
    free_policies(policy_chain_2);
    fuzz_free(remove_stream_ssrc);
    fuzz_free(set_roc);
    if (srtp_ctx != NULL) {
        srtp_dealloc(srtp_ctx);
    }
    fuzz_mt19937_destroy();

    return 0;
}
