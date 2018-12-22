/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2018 Winlin
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

#include <srs_rtmp_handshake.hpp>

#include <time.h>

#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_protocol_io.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_utility.hpp>

using namespace _srs_internal;

// for openssl_HMACsha256
#include <openssl/evp.h>
#include <openssl/hmac.h>
// for openssl_generate_key
#include <openssl/dh.h>

#if OPENSSL_VERSION_NUMBER < 0x10100000L

static HMAC_CTX *HMAC_CTX_new(void)
{
    HMAC_CTX *ctx = (HMAC_CTX *)malloc(sizeof(*ctx));
    if (ctx != NULL) {
        HMAC_CTX_init(ctx);
    }
    return ctx;
}

static void HMAC_CTX_free(HMAC_CTX *ctx)
{
    if (ctx != NULL) {
        HMAC_CTX_cleanup(ctx);
        free(ctx);
    }
}

static void DH_get0_key(const DH *dh, const BIGNUM **pub_key, const BIGNUM **priv_key)
{
    if (pub_key != NULL) {
        *pub_key = dh->pub_key;
    }
    if (priv_key != NULL) {
        *priv_key = dh->priv_key;
    }
}

static int DH_set0_pqg(DH *dh, BIGNUM *p, BIGNUM *q, BIGNUM *g)
{
    /* If the fields p and g in d are NULL, the corresponding input
     * parameters MUST be non-NULL.  q may remain NULL.
     */
    if ((dh->p == NULL && p == NULL)
        || (dh->g == NULL && g == NULL))
        return 0;
    
    if (p != NULL) {
        BN_free(dh->p);
        dh->p = p;
    }
    if (q != NULL) {
        BN_free(dh->q);
        dh->q = q;
    }
    if (g != NULL) {
        BN_free(dh->g);
        dh->g = g;
    }
    
    if (q != NULL) {
        dh->length = BN_num_bits(q);
    }
    
    return 1;
}

static int DH_set_length(DH *dh, long length)
{
    dh->length = length;
    return 1;
}

namespace _srs_internal
{
    // 68bytes FMS key which is used to sign the sever packet.
    uint8_t SrsGenuineFMSKey[] = {
        0x47, 0x65, 0x6e, 0x75, 0x69, 0x6e, 0x65, 0x20,
        0x41, 0x64, 0x6f, 0x62, 0x65, 0x20, 0x46, 0x6c,
        0x61, 0x73, 0x68, 0x20, 0x4d, 0x65, 0x64, 0x69,
        0x61, 0x20, 0x53, 0x65, 0x72, 0x76, 0x65, 0x72,
        0x20, 0x30, 0x30, 0x31, // Genuine Adobe Flash Media Server 001
        0xf0, 0xee, 0xc2, 0x4a, 0x80, 0x68, 0xbe, 0xe8,
        0x2e, 0x00, 0xd0, 0xd1, 0x02, 0x9e, 0x7e, 0x57,
        0x6e, 0xec, 0x5d, 0x2d, 0x29, 0x80, 0x6f, 0xab,
        0x93, 0xb8, 0xe6, 0x36, 0xcf, 0xeb, 0x31, 0xae
    }; // 68
    
    // 62bytes FP key which is used to sign the client packet.
    uint8_t SrsGenuineFPKey[] = {
        0x47, 0x65, 0x6E, 0x75, 0x69, 0x6E, 0x65, 0x20,
        0x41, 0x64, 0x6F, 0x62, 0x65, 0x20, 0x46, 0x6C,
        0x61, 0x73, 0x68, 0x20, 0x50, 0x6C, 0x61, 0x79,
        0x65, 0x72, 0x20, 0x30, 0x30, 0x31, // Genuine Adobe Flash Player 001
        0xF0, 0xEE, 0xC2, 0x4A, 0x80, 0x68, 0xBE, 0xE8,
        0x2E, 0x00, 0xD0, 0xD1, 0x02, 0x9E, 0x7E, 0x57,
        0x6E, 0xEC, 0x5D, 0x2D, 0x29, 0x80, 0x6F, 0xAB,
        0x93, 0xB8, 0xE6, 0x36, 0xCF, 0xEB, 0x31, 0xAE
    }; // 62
    
    srs_error_t do_openssl_HMACsha256(HMAC_CTX* ctx, const void* data, int data_size, void* digest, unsigned int* digest_size)
    {
        srs_error_t err = srs_success;
        
        if (HMAC_Update(ctx, (unsigned char *) data, data_size) < 0) {
            return srs_error_new(ERROR_OpenSslSha256Update, "hmac update");
        }
        
        if (HMAC_Final(ctx, (unsigned char *) digest, digest_size) < 0) {
            return srs_error_new(ERROR_OpenSslSha256Final, "hmac final");
        }
        
        return err;
    }
    /**
     * sha256 digest algorithm.
     * @param key the sha256 key, NULL to use EVP_Digest, for instance,
     *       hashlib.sha256(data).digest().
     */
    srs_error_t openssl_HMACsha256(const void* key, int key_size, const void* data, int data_size, void* digest)
    {
        srs_error_t err = srs_success;
        
        unsigned int digest_size = 0;
        
        unsigned char* temp_key = (unsigned char*)key;
        unsigned char* temp_digest = (unsigned char*)digest;
        
        if (key == NULL) {
            // use data to digest.
            // @see ./crypto/sha/sha256t.c
            // @see ./crypto/evp/digest.c
            if (EVP_Digest(data, data_size, temp_digest, &digest_size, EVP_sha256(), NULL) < 0) {
                return srs_error_new(ERROR_OpenSslSha256EvpDigest, "evp digest");
            }
        } else {
            // use key-data to digest.
            HMAC_CTX *ctx = HMAC_CTX_new();
            if (ctx == NULL) {
                return srs_error_new(ERROR_OpenSslCreateHMAC, "hmac new");
            }
            // @remark, if no key, use EVP_Digest to digest,
            // for instance, in python, hashlib.sha256(data).digest().
            if (HMAC_Init_ex(ctx, temp_key, key_size, EVP_sha256(), NULL) < 0) {
                HMAC_CTX_free(ctx);
                return srs_error_new(ERROR_OpenSslSha256Init, "hmac init");
            }
            
            err = do_openssl_HMACsha256(ctx, data, data_size, temp_digest, &digest_size);
            HMAC_CTX_free(ctx);
            
            if (err != srs_success) {
                return srs_error_wrap(err, "hmac sha256");
            }
        }
        
        if (digest_size != 32) {
            return srs_error_new(ERROR_OpenSslSha256DigestSize, "digest size %d", digest_size);
        }
        
        return err;
    }
    
#define RFC2409_PRIME_1024 \
"FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1" \
"29024E088A67CC74020BBEA63B139B22514A08798E3404DD" \
"EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245" \
"E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED" \
"EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE65381" \
"FFFFFFFFFFFFFFFF"
    
    SrsDH::SrsDH()
    {
        pdh = NULL;
    }
    
    SrsDH::~SrsDH()
    {
        close();
    }
    
    void SrsDH::close()
    {
        if (pdh != NULL) {
            DH_free(pdh);
            pdh = NULL;
        }
    }
    
    srs_error_t SrsDH::initialize(bool ensure_128bytes_public_key)
    {
        srs_error_t err = srs_success;
        
        for (;;) {
            if ((err = do_initialize()) != srs_success) {
                return srs_error_wrap(err, "init");
            }
            
            if (ensure_128bytes_public_key) {
                const BIGNUM *pub_key = NULL;
                DH_get0_key(pdh, &pub_key, NULL);
                int32_t key_size = BN_num_bytes(pub_key);
                if (key_size != 128) {
                    srs_warn("regenerate 128B key, current=%dB", key_size);
                    continue;
                }
            }
            
            break;
        }
        
        return err;
    }
    
    srs_error_t SrsDH::copy_public_key(char* pkey, int32_t& pkey_size)
    {
        srs_error_t err = srs_success;
        
        // copy public key to bytes.
        // sometimes, the key_size is 127, seems ok.
        const BIGNUM *pub_key = NULL;
        DH_get0_key(pdh, &pub_key, NULL);
        int32_t key_size = BN_num_bytes(pub_key);
        srs_assert(key_size > 0);
        
        // maybe the key_size is 127, but dh will write all 128bytes pkey,
        // so, donot need to set/initialize the pkey.
        // @see https://github.com/ossrs/srs/issues/165
        key_size = BN_bn2bin(pub_key, (unsigned char*)pkey);
        srs_assert(key_size > 0);
        
        // output the size of public key.
        // @see https://github.com/ossrs/srs/issues/165
        srs_assert(key_size <= pkey_size);
        pkey_size = key_size;
        
        return err;
    }
    
    srs_error_t SrsDH::copy_shared_key(const char* ppkey, int32_t ppkey_size, char* skey, int32_t& skey_size)
    {
        srs_error_t err = srs_success;
        
        BIGNUM* ppk = NULL;
        if ((ppk = BN_bin2bn((const unsigned char*)ppkey, ppkey_size, 0)) == NULL) {
            return srs_error_new(ERROR_OpenSslGetPeerPublicKey, "bin2bn");
        }
        
        // if failed, donot return, do cleanup, @see ./test/dhtest.c:168
        // maybe the key_size is 127, but dh will write all 128bytes skey,
        // so, donot need to set/initialize the skey.
        // @see https://github.com/ossrs/srs/issues/165
        int32_t key_size = DH_compute_key((unsigned char*)skey, ppk, pdh);
        
        if (key_size < ppkey_size) {
            srs_warn("shared key size=%d, ppk_size=%d", key_size, ppkey_size);
        }
        
        if (key_size < 0 || key_size > skey_size) {
            err = srs_error_new(ERROR_OpenSslComputeSharedKey, "key size %d", key_size);
        } else {
            skey_size = key_size;
        }
        
        if (ppk) {
            BN_free(ppk);
        }
        
        return err;
    }
    
    srs_error_t SrsDH::do_initialize()
    {
        srs_error_t err = srs_success;
        
        int32_t bits_count = 1024;
        
        close();
        
        //1. Create the DH
        if ((pdh = DH_new()) == NULL) {
            return srs_error_new(ERROR_OpenSslCreateDH, "dh new");
        }
        
        //2. Create his internal p and g
        BIGNUM *p, *g;
        if ((p = BN_new()) == NULL) {
            return srs_error_new(ERROR_OpenSslCreateP, "dh new");
        }
        if ((g = BN_new()) == NULL) {
            BN_free(p);
            return srs_error_new(ERROR_OpenSslCreateG, "bn new");
        }
        DH_set0_pqg(pdh, p, NULL, g);
        
        //3. initialize p and g, @see ./test/ectest.c:260
        if (!BN_hex2bn(&p, RFC2409_PRIME_1024)) {
            return srs_error_new(ERROR_OpenSslParseP1024, "hex2bn");
        }
        // @see ./test/bntest.c:1764
        if (!BN_set_word(g, 2)) {
            return srs_error_new(ERROR_OpenSslSetG, "set word");
        }
        
        // 4. Set the key length
        DH_set_length(pdh, bits_count);
        
        // 5. Generate private and public key
        // @see ./test/dhtest.c:152
        if (!DH_generate_key(pdh)) {
            return srs_error_new(ERROR_OpenSslGenerateDHKeys, "dh generate key");
        }
        
        return err;
    }
    
    key_block::key_block()
    {
        offset = (int32_t)rand();
        random0 = NULL;
        random1 = NULL;
        
        int valid_offset = calc_valid_offset();
        srs_assert(valid_offset >= 0);
        
        random0_size = valid_offset;
        if (random0_size > 0) {
            random0 = new char[random0_size];
            srs_random_generate(random0, random0_size);
            snprintf(random0, random0_size, "%s", RTMP_SIG_SRS_HANDSHAKE);
        }
        
        srs_random_generate(key, sizeof(key));
        
        random1_size = 764 - valid_offset - 128 - 4;
        if (random1_size > 0) {
            random1 = new char[random1_size];
            srs_random_generate(random1, random1_size);
            snprintf(random1, random1_size, "%s", RTMP_SIG_SRS_HANDSHAKE);
        }
    }
    
    key_block::~key_block()
    {
        srs_freepa(random0);
        srs_freepa(random1);
    }
    
    srs_error_t key_block::parse(SrsBuffer* stream)
    {
        srs_error_t err = srs_success;
        
        // the key must be 764 bytes.
        srs_assert(stream->require(764));
        
        // read the last offset first, 760-763
        stream->skip(764 - sizeof(int32_t));
        offset = stream->read_4bytes();
        
        // reset stream to read others.
        stream->skip(-764);
        
        int valid_offset = calc_valid_offset();
        srs_assert(valid_offset >= 0);
        
        random0_size = valid_offset;
        if (random0_size > 0) {
            srs_freepa(random0);
            random0 = new char[random0_size];
            stream->read_bytes(random0, random0_size);
        }
        
        stream->read_bytes(key, 128);
        
        random1_size = 764 - valid_offset - 128 - 4;
        if (random1_size > 0) {
            srs_freepa(random1);
            random1 = new char[random1_size];
            stream->read_bytes(random1, random1_size);
        }
        
        return err;
    }
    
    int key_block::calc_valid_offset()
    {
        int max_offset_size = 764 - 128 - 4;
        
        int valid_offset = 0;
        uint8_t* pp = (uint8_t*)&offset;
        valid_offset += *pp++;
        valid_offset += *pp++;
        valid_offset += *pp++;
        valid_offset += *pp++;
        
        return valid_offset % max_offset_size;
    }
    
    digest_block::digest_block()
    {
        offset = (int32_t)rand();
        random0 = NULL;
        random1 = NULL;
        
        int valid_offset = calc_valid_offset();
        srs_assert(valid_offset >= 0);
        
        random0_size = valid_offset;
        if (random0_size > 0) {
            random0 = new char[random0_size];
            srs_random_generate(random0, random0_size);
            snprintf(random0, random0_size, "%s", RTMP_SIG_SRS_HANDSHAKE);
        }
        
        srs_random_generate(digest, sizeof(digest));
        
        random1_size = 764 - 4 - valid_offset - 32;
        if (random1_size > 0) {
            random1 = new char[random1_size];
            srs_random_generate(random1, random1_size);
            snprintf(random1, random1_size, "%s", RTMP_SIG_SRS_HANDSHAKE);
        }
    }
    
    digest_block::~digest_block()
    {
        srs_freepa(random0);
        srs_freepa(random1);
    }
    
    srs_error_t digest_block::parse(SrsBuffer* stream)
    {
        srs_error_t err = srs_success;
        
        // the digest must be 764 bytes.
        srs_assert(stream->require(764));
        
        offset = stream->read_4bytes();
        
        int valid_offset = calc_valid_offset();
        srs_assert(valid_offset >= 0);
        
        random0_size = valid_offset;
        if (random0_size > 0) {
            srs_freepa(random0);
            random0 = new char[random0_size];
            stream->read_bytes(random0, random0_size);
        }
        
        stream->read_bytes(digest, 32);
        
        random1_size = 764 - 4 - valid_offset - 32;
        if (random1_size > 0) {
            srs_freepa(random1);
            random1 = new char[random1_size];
            stream->read_bytes(random1, random1_size);
        }
        
        return err;
    }
    
    int digest_block::calc_valid_offset()
    {
        int max_offset_size = 764 - 32 - 4;
        
        int valid_offset = 0;
        uint8_t* pp = (uint8_t*)&offset;
        valid_offset += *pp++;
        valid_offset += *pp++;
        valid_offset += *pp++;
        valid_offset += *pp++;
        
        return valid_offset % max_offset_size;
    }
    
    c1s1_strategy::c1s1_strategy()
    {
    }
    
    c1s1_strategy::~c1s1_strategy()
    {
    }
    
    char* c1s1_strategy::get_digest()
    {
        return digest.digest;
    }
    
    char* c1s1_strategy::get_key()
    {
        return key.key;
    }
    
    srs_error_t c1s1_strategy::dump(c1s1* owner, char* _c1s1, int size)
    {
        srs_assert(size == 1536);
        return copy_to(owner, _c1s1, size, true);
    }
    
    srs_error_t c1s1_strategy::c1_create(c1s1* owner)
    {
        srs_error_t err = srs_success;
        
        // generate digest
        char* c1_digest = NULL;
        
        if ((err = calc_c1_digest(owner, c1_digest)) != srs_success) {
            return srs_error_wrap(err, "sign c1");
        }
        
        srs_assert(c1_digest != NULL);
        SrsAutoFreeA(char, c1_digest);
        
        memcpy(digest.digest, c1_digest, 32);
        
        return err;
    }
    
    srs_error_t c1s1_strategy::c1_validate_digest(c1s1* owner, bool& is_valid)
    {
        srs_error_t err = srs_success;
        
        char* c1_digest = NULL;
        
        if ((err = calc_c1_digest(owner, c1_digest)) != srs_success) {
            return srs_error_wrap(err, "validate c1");
        }
        
        srs_assert(c1_digest != NULL);
        SrsAutoFreeA(char, c1_digest);
        
        is_valid = srs_bytes_equals(digest.digest, c1_digest, 32);
        
        return err;
    }
    
    srs_error_t c1s1_strategy::s1_create(c1s1* owner, c1s1* c1)
    {
        srs_error_t err = srs_success;
        
        SrsDH dh;
        
        // ensure generate 128bytes public key.
        if ((err = dh.initialize(true)) != srs_success) {
            return srs_error_wrap(err, "dh init");
        }
        
        // directly generate the public key.
        // @see: https://github.com/ossrs/srs/issues/148
        int pkey_size = 128;
        if ((err = dh.copy_shared_key(c1->get_key(), 128, key.key, pkey_size)) != srs_success) {
            return srs_error_wrap(err, "copy shared key");
        }
        
        // although the public key is always 128bytes, but the share key maybe not.
        // we just ignore the actual key size, but if need to use the key, must use the actual size.
        // TODO: FIXME: use the actual key size.
        //srs_assert(pkey_size == 128);
        
        char* s1_digest = NULL;
        if ((err = calc_s1_digest(owner, s1_digest))  != srs_success) {
            return srs_error_wrap(err, "calc s1 digest");
        }
        
        srs_assert(s1_digest != NULL);
        SrsAutoFreeA(char, s1_digest);
        
        memcpy(digest.digest, s1_digest, 32);
        
        return err;
    }
    
    srs_error_t c1s1_strategy::s1_validate_digest(c1s1* owner, bool& is_valid)
    {
        srs_error_t err = srs_success;
        
        char* s1_digest = NULL;
        
        if ((err = calc_s1_digest(owner, s1_digest)) != srs_success) {
            return srs_error_wrap(err, "validate s1");
        }
        
        srs_assert(s1_digest != NULL);
        SrsAutoFreeA(char, s1_digest);
        
        is_valid = srs_bytes_equals(digest.digest, s1_digest, 32);
        
        return err;
    }
    
    srs_error_t c1s1_strategy::calc_c1_digest(c1s1* owner, char*& c1_digest)
    {
        srs_error_t err = srs_success;
        
        /**
         * c1s1 is splited by digest:
         *     c1s1-part1: n bytes (time, version, key and digest-part1).
         *     digest-data: 32bytes
         *     c1s1-part2: (1536-n-32)bytes (digest-part2)
         * @return a new allocated bytes, user must free it.
         */
        char* c1s1_joined_bytes = new char[1536 -32];
        SrsAutoFreeA(char, c1s1_joined_bytes);
        if ((err = copy_to(owner, c1s1_joined_bytes, 1536 - 32, false)) != srs_success) {
            return srs_error_wrap(err, "copy bytes");
        }
        
        c1_digest = new char[SRS_OpensslHashSize];
        if ((err = openssl_HMACsha256(SrsGenuineFPKey, 30, c1s1_joined_bytes, 1536 - 32, c1_digest)) != srs_success) {
            srs_freepa(c1_digest);
            return srs_error_wrap(err, "calc c1 digest");
        }
        
        return err;
    }
    
    srs_error_t c1s1_strategy::calc_s1_digest(c1s1* owner, char*& s1_digest)
    {
        srs_error_t err = srs_success;
        
        /**
         * c1s1 is splited by digest:
         *     c1s1-part1: n bytes (time, version, key and digest-part1).
         *     digest-data: 32bytes
         *     c1s1-part2: (1536-n-32)bytes (digest-part2)
         * @return a new allocated bytes, user must free it.
         */
        char* c1s1_joined_bytes = new char[1536 -32];
        SrsAutoFreeA(char, c1s1_joined_bytes);
        if ((err = copy_to(owner, c1s1_joined_bytes, 1536 - 32, false)) != srs_success) {
            return srs_error_wrap(err, "copy bytes");
        }
        
        s1_digest = new char[SRS_OpensslHashSize];
        if ((err = openssl_HMACsha256(SrsGenuineFMSKey, 36, c1s1_joined_bytes, 1536 - 32, s1_digest)) != srs_success) {
            srs_freepa(s1_digest);
            return srs_error_wrap(err, "calc s1 digest");
        }
        
        return err;
    }
    
    void c1s1_strategy::copy_time_version(SrsBuffer* stream, c1s1* owner)
    {
        srs_assert(stream->require(8));
        
        // 4bytes time
        stream->write_4bytes(owner->time);
        
        // 4bytes version
        stream->write_4bytes(owner->version);
    }
    void c1s1_strategy::copy_key(SrsBuffer* stream)
    {
        srs_assert(key.random0_size >= 0);
        srs_assert(key.random1_size >= 0);
        
        int total = key.random0_size + 128 + key.random1_size + 4;
        srs_assert(stream->require(total));
        
        // 764bytes key block
        if (key.random0_size > 0) {
            stream->write_bytes(key.random0, key.random0_size);
        }
        
        stream->write_bytes(key.key, 128);
        
        if (key.random1_size > 0) {
            stream->write_bytes(key.random1, key.random1_size);
        }
        
        stream->write_4bytes(key.offset);
    }
    void c1s1_strategy::copy_digest(SrsBuffer* stream, bool with_digest)
    {
        srs_assert(key.random0_size >= 0);
        srs_assert(key.random1_size >= 0);
        
        int total = 4 + digest.random0_size + digest.random1_size;
        if (with_digest) {
            total += 32;
        }
        srs_assert(stream->require(total));
        
        // 732bytes digest block without the 32bytes digest-data
        // nbytes digest block part1
        stream->write_4bytes(digest.offset);
        
        // digest random padding.
        if (digest.random0_size > 0) {
            stream->write_bytes(digest.random0, digest.random0_size);
        }
        
        // digest
        if (with_digest) {
            stream->write_bytes(digest.digest, 32);
        }
        
        // nbytes digest block part2
        if (digest.random1_size > 0) {
            stream->write_bytes(digest.random1, digest.random1_size);
        }
    }
    
    c1s1_strategy_schema0::c1s1_strategy_schema0()
    {
    }
    
    c1s1_strategy_schema0::~c1s1_strategy_schema0()
    {
    }
    
    srs_schema_type c1s1_strategy_schema0::schema()
    {
        return srs_schema0;
    }
    
    srs_error_t c1s1_strategy_schema0::parse(char* _c1s1, int size)
    {
        srs_error_t err = srs_success;
        
        srs_assert(size == 1536);
        
        if (true) {
            SrsBuffer stream(_c1s1 + 8, 764);
            
            if ((err = key.parse(&stream)) != srs_success) {
                return srs_error_wrap(err, "parse the c1 key");
            }
        }
        
        if (true) {
            SrsBuffer stream(_c1s1 + 8 + 764, 764);
        
            if ((err = digest.parse(&stream)) != srs_success) {
                return srs_error_wrap(err, "parse the c1 digest");
            }
        }
        
        return err;
    }
    
    srs_error_t c1s1_strategy_schema0::copy_to(c1s1* owner, char* bytes, int size, bool with_digest)
    {
        srs_error_t err = srs_success;
        
        if (with_digest) {
            srs_assert(size == 1536);
        } else {
            srs_assert(size == 1504);
        }
        
        SrsBuffer stream(bytes, size);
        
        copy_time_version(&stream, owner);
        copy_key(&stream);
        copy_digest(&stream, with_digest);
        
        srs_assert(stream.empty());
        
        return err;
    }
    
    c1s1_strategy_schema1::c1s1_strategy_schema1()
    {
    }
    
    c1s1_strategy_schema1::~c1s1_strategy_schema1()
    {
    }
    
    srs_schema_type c1s1_strategy_schema1::schema()
    {
        return srs_schema1;
    }
    
    srs_error_t c1s1_strategy_schema1::parse(char* _c1s1, int size)
    {
        srs_error_t err = srs_success;
        
        srs_assert(size == 1536);
        
        if (true) {
            SrsBuffer stream(_c1s1 + 8, 764);
            
            if ((err = digest.parse(&stream)) != srs_success) {
                return srs_error_wrap(err, "parse c1 digest");
            }
        }
        
        if (true) {
            SrsBuffer stream(_c1s1 + 8 + 764, 764);
            
            if ((err = key.parse(&stream)) != srs_success) {
                return srs_error_wrap(err, "parse c1 key");
            }
        }
        
        return err;
    }
    
    srs_error_t c1s1_strategy_schema1::copy_to(c1s1* owner, char* bytes, int size, bool with_digest)
    {
        srs_error_t err = srs_success;
        
        if (with_digest) {
            srs_assert(size == 1536);
        } else {
            srs_assert(size == 1504);
        }
        
        SrsBuffer stream(bytes, size);
        
        copy_time_version(&stream, owner);
        copy_digest(&stream, with_digest);
        copy_key(&stream);
        
        srs_assert(stream.empty());
        
        return err;
    }
    
    c1s1::c1s1()
    {
        payload = NULL;
    }
    c1s1::~c1s1()
    {
        srs_freep(payload);
    }
    
    srs_schema_type c1s1::schema()
    {
        srs_assert(payload != NULL);
        return payload->schema();
    }
    
    char* c1s1::get_digest()
    {
        srs_assert(payload != NULL);
        return payload->get_digest();
    }
    
    char* c1s1::get_key()
    {
        srs_assert(payload != NULL);
        return payload->get_key();
    }
    
    srs_error_t c1s1::dump(char* _c1s1, int size)
    {
        srs_assert(size == 1536);
        srs_assert(payload != NULL);
        return payload->dump(this, _c1s1, size);
    }
    
    srs_error_t c1s1::parse(char* _c1s1, int size, srs_schema_type schema)
    {
        srs_assert(size == 1536);
        
        if (schema != srs_schema0 && schema != srs_schema1) {
            return srs_error_new(ERROR_RTMP_CH_SCHEMA, "parse c1 failed. invalid schema=%d", schema);
        }
        
        SrsBuffer stream(_c1s1, size);
        
        time = stream.read_4bytes();
        version = stream.read_4bytes(); // client c1 version
        
        srs_freep(payload);
        if (schema == srs_schema0) {
            payload = new c1s1_strategy_schema0();
        } else {
            payload = new c1s1_strategy_schema1();
        }
        
        return payload->parse(_c1s1, size);
    }
    
    srs_error_t c1s1::c1_create(srs_schema_type schema)
    {
        if (schema != srs_schema0 && schema != srs_schema1) {
            return srs_error_new(ERROR_RTMP_CH_SCHEMA, "create c1 failed. invalid schema=%d", schema);
        }
        
        // client c1 time and version
        time = (int32_t)::time(NULL);
        version = 0x80000702; // client c1 version
        
        // generate signature by schema
        srs_freep(payload);
        if (schema == srs_schema0) {
            payload = new c1s1_strategy_schema0();
        } else {
            payload = new c1s1_strategy_schema1();
        }
        
        return payload->c1_create(this);
    }
    
    srs_error_t c1s1::c1_validate_digest(bool& is_valid)
    {
        is_valid = false;
        srs_assert(payload);
        return payload->c1_validate_digest(this, is_valid);
    }
    
    srs_error_t c1s1::s1_create(c1s1* c1)
    {
        if (c1->schema() != srs_schema0 && c1->schema() != srs_schema1) {
            return srs_error_new(ERROR_RTMP_CH_SCHEMA, "create s1 failed. invalid schema=%d", c1->schema());
        }
        
        time = ::time(NULL);
        version = 0x01000504; // server s1 version
        
        srs_freep(payload);
        if (c1->schema() == srs_schema0) {
            payload = new c1s1_strategy_schema0();
        } else {
            payload = new c1s1_strategy_schema1();
        }
        
        return payload->s1_create(this, c1);
    }
    
    srs_error_t c1s1::s1_validate_digest(bool& is_valid)
    {
        is_valid = false;
        srs_assert(payload);
        return payload->s1_validate_digest(this, is_valid);
    }
    
    c2s2::c2s2()
    {
        srs_random_generate(random, 1504);
        
        int size = snprintf(random, 1504, "%s", RTMP_SIG_SRS_HANDSHAKE);
        srs_assert(++size < 1504);
        snprintf(random + 1504 - size, size, "%s", RTMP_SIG_SRS_HANDSHAKE);
        
        srs_random_generate(digest, 32);
    }
    
    c2s2::~c2s2()
    {
    }
    
    srs_error_t c2s2::dump(char* _c2s2, int size)
    {
        srs_assert(size == 1536);
        
        memcpy(_c2s2, random, 1504);
        memcpy(_c2s2 + 1504, digest, 32);
        
        return srs_success;
    }
    
    srs_error_t c2s2::parse(char* _c2s2, int size)
    {
        srs_assert(size == 1536);
        
        memcpy(random, _c2s2, 1504);
        memcpy(digest, _c2s2 + 1504, 32);
        
        return srs_success;
    }
    
    srs_error_t c2s2::c2_create(c1s1* s1)
    {
        srs_error_t err = srs_success;
        
        char temp_key[SRS_OpensslHashSize];
        if ((err = openssl_HMACsha256(SrsGenuineFPKey, 62, s1->get_digest(), 32, temp_key)) != srs_success) {
            return srs_error_wrap(err, "create c2 temp key");
        }
        
        char _digest[SRS_OpensslHashSize];
        if ((err = openssl_HMACsha256(temp_key, 32, random, 1504, _digest)) != srs_success) {
            return srs_error_wrap(err, "create c2 digest");
        }
        
        memcpy(digest, _digest, 32);
        
        return err;
    }
    
    srs_error_t c2s2::c2_validate(c1s1* s1, bool& is_valid)
    {
        is_valid = false;
        srs_error_t err = srs_success;
        
        char temp_key[SRS_OpensslHashSize];
        if ((err = openssl_HMACsha256(SrsGenuineFPKey, 62, s1->get_digest(), 32, temp_key)) != srs_success) {
            return srs_error_wrap(err, "create c2 temp key");
        }
        
        char _digest[SRS_OpensslHashSize];
        if ((err = openssl_HMACsha256(temp_key, 32, random, 1504, _digest)) != srs_success) {
            return srs_error_wrap(err, "create c2 digest");
        }
        
        is_valid = srs_bytes_equals(digest, _digest, 32);
        
        return err;
    }
    
    srs_error_t c2s2::s2_create(c1s1* c1)
    {
        srs_error_t err = srs_success;
        
        char temp_key[SRS_OpensslHashSize];
        if ((err = openssl_HMACsha256(SrsGenuineFMSKey, 68, c1->get_digest(), 32, temp_key)) != srs_success) {
            return srs_error_wrap(err, "create s2 temp key");
        }
        
        char _digest[SRS_OpensslHashSize];
        if ((err = openssl_HMACsha256(temp_key, 32, random, 1504, _digest)) != srs_success) {
            return srs_error_wrap(err, "create s2 digest");
        }
        
        memcpy(digest, _digest, 32);
        
        return err;
    }
    
    srs_error_t c2s2::s2_validate(c1s1* c1, bool& is_valid)
    {
        is_valid = false;
        srs_error_t err = srs_success;
        
        char temp_key[SRS_OpensslHashSize];
        if ((err = openssl_HMACsha256(SrsGenuineFMSKey, 68, c1->get_digest(), 32, temp_key)) != srs_success) {
            return srs_error_wrap(err, "create s2 temp key");
        }
        
        char _digest[SRS_OpensslHashSize];
        if ((err = openssl_HMACsha256(temp_key, 32, random, 1504, _digest)) != srs_success) {
            return srs_error_wrap(err, "create s2 digest");
        }
        
        is_valid = srs_bytes_equals(digest, _digest, 32);
        
        return err;
    }
}

#endif

SrsSimpleHandshake::SrsSimpleHandshake()
{
}

SrsSimpleHandshake::~SrsSimpleHandshake()
{
}

srs_error_t SrsSimpleHandshake::handshake_with_client(SrsHandshakeBytes* hs_bytes, ISrsProtocolReaderWriter* io)
{
    srs_error_t err = srs_success;
    
    ssize_t nsize;
    
    if ((err = hs_bytes->read_c0c1(io)) != srs_success) {
        return srs_error_wrap(err, "read c0c1");
    }
    
    // plain text required.
    if (hs_bytes->c0c1[0] != 0x03) {
        return srs_error_new(ERROR_RTMP_PLAIN_REQUIRED, "only support rtmp plain text, version=%X", (uint8_t)hs_bytes->c0c1[0]);
    }
    
    if ((err = hs_bytes->create_s0s1s2(hs_bytes->c0c1 + 1)) != srs_success) {
        return srs_error_wrap(err, "create s0s1s2");
    }
    
    if ((err = io->write(hs_bytes->s0s1s2, 3073, &nsize)) != srs_success) {
        return srs_error_wrap(err, "write s0s1s2");
    }
    
    if ((err = hs_bytes->read_c2(io)) != srs_success) {
        return srs_error_wrap(err, "read c2");
    }
    
    srs_trace("simple handshake success.");
    
    return err;
}

srs_error_t SrsSimpleHandshake::handshake_with_server(SrsHandshakeBytes* hs_bytes, ISrsProtocolReaderWriter* io)
{
    srs_error_t err = srs_success;
    
    ssize_t nsize;
    
    // simple handshake
    if ((err = hs_bytes->create_c0c1()) != srs_success) {
        return srs_error_wrap(err, "create c0c1");
    }
    
    if ((err = io->write(hs_bytes->c0c1, 1537, &nsize)) != srs_success) {
        return srs_error_wrap(err, "write c0c1");
    }
    
    if ((err = hs_bytes->read_s0s1s2(io)) != srs_success) {
        return srs_error_wrap(err, "read s0s1s2");
    }
    
    // plain text required.
    if (hs_bytes->s0s1s2[0] != 0x03) {
        return srs_error_new(ERROR_RTMP_HANDSHAKE, "handshake failed, plain text required, version=%X", (uint8_t)hs_bytes->s0s1s2[0]);
    }
    
    if ((err = hs_bytes->create_c2()) != srs_success) {
        return srs_error_wrap(err, "create c2");
    }
    
    // for simple handshake, copy s1 to c2.
    // @see https://github.com/ossrs/srs/issues/418
    memcpy(hs_bytes->c2, hs_bytes->s0s1s2 + 1, 1536);
    
    if ((err = io->write(hs_bytes->c2, 1536, &nsize)) != srs_success) {
        return srs_error_wrap(err, "write c2");
    }
    
    srs_trace("simple handshake success.");
    
    return err;
}

SrsComplexHandshake::SrsComplexHandshake()
{
}

SrsComplexHandshake::~SrsComplexHandshake()
{
}

srs_error_t SrsComplexHandshake::handshake_with_client(SrsHandshakeBytes* hs_bytes, ISrsProtocolReaderWriter* io)
{
    srs_error_t err = srs_success;
    
    ssize_t nsize;
    
    if ((err = hs_bytes->read_c0c1(io)) != srs_success) {
        return srs_error_wrap(err, "read c0c1");
    }
    
    // decode c1
    c1s1 c1;
    // try schema0.
    // @remark, use schema0 to make flash player happy.
    if ((err = c1.parse(hs_bytes->c0c1 + 1, 1536, srs_schema0)) != srs_success) {
        return srs_error_wrap(err, "parse c1, schema=%d", srs_schema0);
    }
    // try schema1
    bool is_valid = false;
    if ((err = c1.c1_validate_digest(is_valid)) != srs_success || !is_valid) {
        srs_freep(err);
        
        if ((err = c1.parse(hs_bytes->c0c1 + 1, 1536, srs_schema1)) != srs_success) {
            return srs_error_wrap(err, "parse c0c1, schame=%d", srs_schema1);
        }
        
        if ((err = c1.c1_validate_digest(is_valid)) != srs_success || !is_valid) {
            srs_freep(err);
            return srs_error_new(ERROR_RTMP_TRY_SIMPLE_HS, "all schema valid failed, try simple handshake");
        }
    }
    
    // encode s1
    c1s1 s1;
    if ((err = s1.s1_create(&c1)) != srs_success) {
        return srs_error_wrap(err, "create s1 from c1");
    }
    // verify s1
    if ((err = s1.s1_validate_digest(is_valid)) != srs_success || !is_valid) {
        srs_freep(err);
        return srs_error_new(ERROR_RTMP_TRY_SIMPLE_HS, "verify s1 failed, try simple handshake");
    }
    
    c2s2 s2;
    if ((err = s2.s2_create(&c1)) != srs_success) {
        return srs_error_wrap(err, "create s2 from c1");
    }
    // verify s2
    if ((err = s2.s2_validate(&c1, is_valid)) != srs_success || !is_valid) {
        return srs_error_new(ERROR_RTMP_TRY_SIMPLE_HS, "verify s2 failed, try simple handshake");
    }
    
    // sendout s0s1s2
    if ((err = hs_bytes->create_s0s1s2()) != srs_success) {
        return srs_error_wrap(err, "create s0s1s2");
    }
    if ((err = s1.dump(hs_bytes->s0s1s2 + 1, 1536)) != srs_success) {
        return srs_error_wrap(err, "dump s1");
    }
    if ((err = s2.dump(hs_bytes->s0s1s2 + 1537, 1536)) != srs_success) {
        return srs_error_wrap(err, "dump s2");
    }
    if ((err = io->write(hs_bytes->s0s1s2, 3073, &nsize)) != srs_success) {
        return srs_error_wrap(err, "write s0s1s2");
    }
    
    // recv c2
    if ((err = hs_bytes->read_c2(io)) != srs_success) {
        return srs_error_wrap(err, "read c2");
    }
    c2s2 c2;
    if ((err = c2.parse(hs_bytes->c2, 1536)) != srs_success) {
        return srs_error_wrap(err, "parse c2");
    }
    
    // verify c2
    // never verify c2, for ffmpeg will failed.
    // it's ok for flash.
    
    srs_trace("complex handshake success");
    
    return err;
}

srs_error_t SrsComplexHandshake::handshake_with_server(SrsHandshakeBytes* hs_bytes, ISrsProtocolReaderWriter* io)
{
    srs_error_t err = srs_success;
    
    ssize_t nsize;
    
    // complex handshake
    if ((err = hs_bytes->create_c0c1()) != srs_success) {
        return srs_error_wrap(err, "create c0c1");
    }
    
    // sign c1
    c1s1 c1;
    // @remark, FMS requires the schema1(digest-key), or connect failed.
    if ((err = c1.c1_create(srs_schema1)) != srs_success) {
        return srs_error_wrap(err, "create c1");
    }
    if ((err = c1.dump(hs_bytes->c0c1 + 1, 1536)) != srs_success) {
        return srs_error_wrap(err, "dump c1");
    }
    
    // verify c1
    bool is_valid;
    if ((err = c1.c1_validate_digest(is_valid)) != srs_success || !is_valid) {
        srs_freep(err);
        return srs_error_new(ERROR_RTMP_TRY_SIMPLE_HS, "try simple handshake");
    }
    
    if ((err = io->write(hs_bytes->c0c1, 1537, &nsize)) != srs_success) {
        return srs_error_wrap(err, "write c0c1");
    }
    
    // s0s1s2
    if ((err = hs_bytes->read_s0s1s2(io)) != srs_success) {
        return srs_error_wrap(err, "read s0s1s2");
    }
    
    // plain text required.
    if (hs_bytes->s0s1s2[0] != 0x03) {
        return srs_error_new(ERROR_RTMP_HANDSHAKE,  "handshake failed, plain text required, version=%X", (uint8_t)hs_bytes->s0s1s2[0]);
    }
    
    // verify s1s2
    c1s1 s1;
    if ((err = s1.parse(hs_bytes->s0s1s2 + 1, 1536, c1.schema())) != srs_success) {
        return srs_error_wrap(err, "parse s1");
    }
    
    // never verify the s1,
    // for if forward to nginx-rtmp, verify s1 will failed,
    // TODO: FIXME: find the handshake schema of nginx-rtmp.
    
    // c2
    if ((err = hs_bytes->create_c2()) != srs_success) {
        return srs_error_wrap(err, "create c2");
    }
    
    c2s2 c2;
    if ((err = c2.c2_create(&s1)) != srs_success) {
        return srs_error_wrap(err, "create c2");
    }
    
    if ((err = c2.dump(hs_bytes->c2, 1536)) != srs_success) {
        return srs_error_wrap(err, "dump c2");
    }
    if ((err = io->write(hs_bytes->c2, 1536, &nsize)) != srs_success) {
        return srs_error_wrap(err, "write c2");
    }
    
    srs_trace("complex handshake success.");
    
    return err;
}

