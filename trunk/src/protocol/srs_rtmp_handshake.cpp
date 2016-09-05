/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(ossrs)

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <srs_rtmp_handshake.hpp>

#include <time.h>

#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_rtmp_io.hpp>
#include <srs_rtmp_utility.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_kernel_stream.hpp>

#ifdef SRS_AUTO_SSL

using namespace _srs_internal;

// for openssl_HMACsha256
#include <openssl/evp.h>
#include <openssl/hmac.h>
// for openssl_generate_key
#include <openssl/dh.h>

namespace _srs_internal
{
    // 68bytes FMS key which is used to sign the sever packet.
    u_int8_t SrsGenuineFMSKey[] = {
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
    u_int8_t SrsGenuineFPKey[] = {
        0x47, 0x65, 0x6E, 0x75, 0x69, 0x6E, 0x65, 0x20,
        0x41, 0x64, 0x6F, 0x62, 0x65, 0x20, 0x46, 0x6C,
        0x61, 0x73, 0x68, 0x20, 0x50, 0x6C, 0x61, 0x79,
        0x65, 0x72, 0x20, 0x30, 0x30, 0x31, // Genuine Adobe Flash Player 001
        0xF0, 0xEE, 0xC2, 0x4A, 0x80, 0x68, 0xBE, 0xE8,
        0x2E, 0x00, 0xD0, 0xD1, 0x02, 0x9E, 0x7E, 0x57,
        0x6E, 0xEC, 0x5D, 0x2D, 0x29, 0x80, 0x6F, 0xAB,
        0x93, 0xB8, 0xE6, 0x36, 0xCF, 0xEB, 0x31, 0xAE
    }; // 62
    
    int do_openssl_HMACsha256(HMAC_CTX* ctx, const void* data, int data_size, void* digest, unsigned int* digest_size) 
    {
        int ret = ERROR_SUCCESS;
        
        if (HMAC_Update(ctx, (unsigned char *) data, data_size) < 0) {
            ret = ERROR_OpenSslSha256Update;
            return ret;
        }
    
        if (HMAC_Final(ctx, (unsigned char *) digest, digest_size) < 0) {
            ret = ERROR_OpenSslSha256Final;
            return ret;
        }
        
        return ret;
    }
    /**
    * sha256 digest algorithm.
    * @param key the sha256 key, NULL to use EVP_Digest, for instance,
    *       hashlib.sha256(data).digest().
    */
    int openssl_HMACsha256(const void* key, int key_size, const void* data, int data_size, void* digest) 
    {
        int ret = ERROR_SUCCESS;
        
        unsigned int digest_size = 0;
        
        unsigned char* temp_key = (unsigned char*)key;
        unsigned char* temp_digest = (unsigned char*)digest;
        
        if (key == NULL) {
            // use data to digest.
            // @see ./crypto/sha/sha256t.c
            // @see ./crypto/evp/digest.c
            if (EVP_Digest(data, data_size, temp_digest, &digest_size, EVP_sha256(), NULL) < 0)
            {
                ret = ERROR_OpenSslSha256EvpDigest;
                return ret;
            }
        } else {
            // use key-data to digest.
            HMAC_CTX ctx;
            
            // @remark, if no key, use EVP_Digest to digest,
            // for instance, in python, hashlib.sha256(data).digest().
            HMAC_CTX_init(&ctx);
            if (HMAC_Init_ex(&ctx, temp_key, key_size, EVP_sha256(), NULL) < 0) {
                ret = ERROR_OpenSslSha256Init;
                return ret;
            }
            
            ret = do_openssl_HMACsha256(&ctx, data, data_size, temp_digest, &digest_size);
            HMAC_CTX_cleanup(&ctx);
            
            if (ret != ERROR_SUCCESS) {
                return ret;
            }
        }
        
        if (digest_size != 32) {
            ret = ERROR_OpenSslSha256DigestSize;
            return ret;
        }
        
        return ret;
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
            if (pdh->p != NULL) {
                BN_free(pdh->p);
                pdh->p = NULL;
            }
            if (pdh->g != NULL) {
                BN_free(pdh->g);
                pdh->g = NULL;
            }
            DH_free(pdh);
            pdh = NULL;
        }
    }
    
    int SrsDH::initialize(bool ensure_128bytes_public_key)
    {
        int ret = ERROR_SUCCESS;
        
        for (;;) {
            if ((ret = do_initialize()) != ERROR_SUCCESS) {
                return ret;
            }
            
            if (ensure_128bytes_public_key) {
                int32_t key_size = BN_num_bytes(pdh->pub_key);
                if (key_size != 128) {
                    srs_warn("regenerate 128B key, current=%dB", key_size);
                    continue;
                }
            }
            
            break;
        }
        
        return ret;
    }
    
    int SrsDH::copy_public_key(char* pkey, int32_t& pkey_size)
    {
        int ret = ERROR_SUCCESS;
        
        // copy public key to bytes.
        // sometimes, the key_size is 127, seems ok.
        int32_t key_size = BN_num_bytes(pdh->pub_key);
        srs_assert(key_size > 0);
        
        // maybe the key_size is 127, but dh will write all 128bytes pkey,
        // so, donot need to set/initialize the pkey.
        // @see https://github.com/ossrs/srs/issues/165
        key_size = BN_bn2bin(pdh->pub_key, (unsigned char*)pkey);
        srs_assert(key_size > 0);
        
        // output the size of public key.
        // @see https://github.com/ossrs/srs/issues/165
        srs_assert(key_size <= pkey_size);
        pkey_size = key_size;
        
        return ret;
    }
    
    int SrsDH::copy_shared_key(const char* ppkey, int32_t ppkey_size, char* skey, int32_t& skey_size)
    {
        int ret = ERROR_SUCCESS;
        
        BIGNUM* ppk = NULL;
        if ((ppk = BN_bin2bn((const unsigned char*)ppkey, ppkey_size, 0)) == NULL) {
            ret = ERROR_OpenSslGetPeerPublicKey;
            return ret;
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
            ret = ERROR_OpenSslComputeSharedKey;
        } else {
            skey_size = key_size;
        }
        
        if (ppk) {
            BN_free(ppk);
        }
        
        return ret;
    }
    
    int SrsDH::do_initialize()
    {
        int ret = ERROR_SUCCESS;
        
        int32_t bits_count = 1024;
        
        close();
        
        //1. Create the DH
        if ((pdh = DH_new()) == NULL) {
            ret = ERROR_OpenSslCreateDH; 
            return ret;
        }
    
        //2. Create his internal p and g
        if ((pdh->p = BN_new()) == NULL) {
            ret = ERROR_OpenSslCreateP; 
            return ret;
        }
        if ((pdh->g = BN_new()) == NULL) {
            ret = ERROR_OpenSslCreateG; 
            return ret;
        }
    
        //3. initialize p and g, @see ./test/ectest.c:260
        if (!BN_hex2bn(&pdh->p, RFC2409_PRIME_1024)) {
            ret = ERROR_OpenSslParseP1024; 
            return ret;
        }
        // @see ./test/bntest.c:1764
        if (!BN_set_word(pdh->g, 2)) {
            ret = ERROR_OpenSslSetG;
            return ret;
        }
    
        // 4. Set the key length
        pdh->length = bits_count;
    
        // 5. Generate private and public key
        // @see ./test/dhtest.c:152
        if (!DH_generate_key(pdh)) {
            ret = ERROR_OpenSslGenerateDHKeys;
            return ret;
        }
        
        return ret;
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
    
    int key_block::parse(SrsStream* stream)
    {
        int ret = ERROR_SUCCESS;
        
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
        
        return ret;
    }
    
    int key_block::calc_valid_offset()
    {
        int max_offset_size = 764 - 128 - 4;
        
        int valid_offset = 0;
        u_int8_t* pp = (u_int8_t*)&offset;
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

    int digest_block::parse(SrsStream* stream)
    {
        int ret = ERROR_SUCCESS;
        
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
        
        return ret;
    }
    
    int digest_block::calc_valid_offset()
    {
        int max_offset_size = 764 - 32 - 4;
        
        int valid_offset = 0;
        u_int8_t* pp = (u_int8_t*)&offset;
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
    
    int c1s1_strategy::dump(c1s1* owner, char* _c1s1, int size)
    {
        srs_assert(size == 1536);
        return copy_to(owner, _c1s1, size, true);
    }
    
    int c1s1_strategy::c1_create(c1s1* owner)
    {
        int ret = ERROR_SUCCESS;
        
        // generate digest
        char* c1_digest = NULL;
        
        if ((ret = calc_c1_digest(owner, c1_digest)) != ERROR_SUCCESS) {
            srs_error("sign c1 error, failed to calc digest. ret=%d", ret);
            return ret;
        }
        
        srs_assert(c1_digest != NULL);
        SrsAutoFreeA(char, c1_digest);
        
        memcpy(digest.digest, c1_digest, 32);
        
        return ret;
    }
    
    int c1s1_strategy::c1_validate_digest(c1s1* owner, bool& is_valid)
    {
        int ret = ERROR_SUCCESS;
        
        char* c1_digest = NULL;
        
        if ((ret = calc_c1_digest(owner, c1_digest)) != ERROR_SUCCESS) {
            srs_error("validate c1 error, failed to calc digest. ret=%d", ret);
            return ret;
        }
        
        srs_assert(c1_digest != NULL);
        SrsAutoFreeA(char, c1_digest);
        
        is_valid = srs_bytes_equals(digest.digest, c1_digest, 32);
        
        return ret;
    }
    
    int c1s1_strategy::s1_create(c1s1* owner, c1s1* c1)
    {
        int ret = ERROR_SUCCESS;

        SrsDH dh;
        
        // ensure generate 128bytes public key.
        if ((ret = dh.initialize(true)) != ERROR_SUCCESS) {
            return ret;
        }
        
        // directly generate the public key.
        // @see: https://github.com/ossrs/srs/issues/148
        int pkey_size = 128;
        if ((ret = dh.copy_shared_key(c1->get_key(), 128, key.key, pkey_size)) != ERROR_SUCCESS) {
            srs_error("calc s1 key failed. ret=%d", ret);
            return ret;
        }

        // altough the public key is always 128bytes, but the share key maybe not.
        // we just ignore the actual key size, but if need to use the key, must use the actual size.
        // TODO: FIXME: use the actual key size.
        //srs_assert(pkey_size == 128);
        srs_verbose("calc s1 key success.");
            
        char* s1_digest = NULL;
        if ((ret = calc_s1_digest(owner, s1_digest))  != ERROR_SUCCESS) {
            srs_error("calc s1 digest failed. ret=%d", ret);
            return ret;
        }
        srs_verbose("calc s1 digest success.");
        
        srs_assert(s1_digest != NULL);
        SrsAutoFreeA(char, s1_digest);
        
        memcpy(digest.digest, s1_digest, 32);
        srs_verbose("copy s1 key success.");
        
        return ret;
    }
    
    int c1s1_strategy::s1_validate_digest(c1s1* owner, bool& is_valid)
    {
        int ret = ERROR_SUCCESS;
        
        char* s1_digest = NULL;
        
        if ((ret = calc_s1_digest(owner, s1_digest)) != ERROR_SUCCESS) {
            srs_error("validate s1 error, failed to calc digest. ret=%d", ret);
            return ret;
        }
        
        srs_assert(s1_digest != NULL);
        SrsAutoFreeA(char, s1_digest);
        
        is_valid = srs_bytes_equals(digest.digest, s1_digest, 32);
        
        return ret;
    }
    
    int c1s1_strategy::calc_c1_digest(c1s1* owner, char*& c1_digest)
    {
        int ret = ERROR_SUCCESS;

        /**
        * c1s1 is splited by digest:
        *     c1s1-part1: n bytes (time, version, key and digest-part1).
        *     digest-data: 32bytes
        *     c1s1-part2: (1536-n-32)bytes (digest-part2)
        * @return a new allocated bytes, user must free it.
        */
        char* c1s1_joined_bytes = new char[1536 -32];
        SrsAutoFreeA(char, c1s1_joined_bytes);
        if ((ret = copy_to(owner, c1s1_joined_bytes, 1536 - 32, false)) != ERROR_SUCCESS) {
            return ret;
        }
        
        c1_digest = new char[SRS_OpensslHashSize];
        if ((ret = openssl_HMACsha256(SrsGenuineFPKey, 30, c1s1_joined_bytes, 1536 - 32, c1_digest)) != ERROR_SUCCESS) {
            srs_freepa(c1_digest);
            srs_error("calc digest for c1 failed. ret=%d", ret);
            return ret;
        }
        srs_verbose("digest calculated for c1");
        
        return ret;
    }
    
    int c1s1_strategy::calc_s1_digest(c1s1* owner, char*& s1_digest)
    {
        int ret = ERROR_SUCCESS;

        /**
        * c1s1 is splited by digest:
        *     c1s1-part1: n bytes (time, version, key and digest-part1).
        *     digest-data: 32bytes
        *     c1s1-part2: (1536-n-32)bytes (digest-part2)
        * @return a new allocated bytes, user must free it.
        */
        char* c1s1_joined_bytes = new char[1536 -32];
        SrsAutoFreeA(char, c1s1_joined_bytes);
        if ((ret = copy_to(owner, c1s1_joined_bytes, 1536 - 32, false)) != ERROR_SUCCESS) {
            return ret;
        }
        
        s1_digest = new char[SRS_OpensslHashSize];
        if ((ret = openssl_HMACsha256(SrsGenuineFMSKey, 36, c1s1_joined_bytes, 1536 - 32, s1_digest)) != ERROR_SUCCESS) {
            srs_freepa(s1_digest);
            srs_error("calc digest for s1 failed. ret=%d", ret);
            return ret;
        }
        srs_verbose("digest calculated for s1");

        return ret;
    }
    
    void c1s1_strategy::copy_time_version(SrsStream* stream, c1s1* owner)
    {
        srs_assert(stream->require(8));
        
        // 4bytes time
        stream->write_4bytes(owner->time);

        // 4bytes version
        stream->write_4bytes(owner->version);
    }
    void c1s1_strategy::copy_key(SrsStream* stream)
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
    void c1s1_strategy::copy_digest(SrsStream* stream, bool with_digest)
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
    
    int c1s1_strategy_schema0::parse(char* _c1s1, int size)
    {
        int ret = ERROR_SUCCESS;
        
        srs_assert(size == 1536);
        
        SrsStream stream;
        
        if ((ret = stream.initialize(_c1s1 + 8, 764)) != ERROR_SUCCESS) {
            return ret;
        }
        
        if ((ret = key.parse(&stream)) != ERROR_SUCCESS) {
            srs_error("parse the c1 key failed. ret=%d", ret);
            return ret;
        }
        
        if ((ret = stream.initialize(_c1s1 + 8 + 764, 764)) != ERROR_SUCCESS) {
            return ret;
        }

        if ((ret = digest.parse(&stream)) != ERROR_SUCCESS) {
            srs_error("parse the c1 digest failed. ret=%d", ret);
            return ret;
        }
        
        srs_verbose("parse c1 key-digest success");
        
        return ret;
    }
    
    int c1s1_strategy_schema0::copy_to(c1s1* owner, char* bytes, int size, bool with_digest)
    {
        int ret = ERROR_SUCCESS;
        
        if (with_digest) {
            srs_assert(size == 1536);
        } else {
            srs_assert(size == 1504);
        }
        
        SrsStream stream;
        
        if ((ret = stream.initialize(bytes, size)) != ERROR_SUCCESS) {
            return ret;
        }
        
        copy_time_version(&stream, owner);
        copy_key(&stream);
        copy_digest(&stream, with_digest);
        
        srs_assert(stream.empty());
        
        return ret;
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
    
    int c1s1_strategy_schema1::parse(char* _c1s1, int size)
    {
        int ret = ERROR_SUCCESS;
        
        srs_assert(size == 1536);
        
        SrsStream stream;
        
        if ((ret = stream.initialize(_c1s1 + 8, 764)) != ERROR_SUCCESS) {
            return ret;
        }

        if ((ret = digest.parse(&stream)) != ERROR_SUCCESS) {
            srs_error("parse the c1 digest failed. ret=%d", ret);
            return ret;
        }
        
        if ((ret = stream.initialize(_c1s1 + 8 + 764, 764)) != ERROR_SUCCESS) {
            return ret;
        }
        
        if ((ret = key.parse(&stream)) != ERROR_SUCCESS) {
            srs_error("parse the c1 key failed. ret=%d", ret);
            return ret;
        }
        
        srs_verbose("parse c1 digest-key success");
        
        return ret;
    }
    
    int c1s1_strategy_schema1::copy_to(c1s1* owner, char* bytes, int size, bool with_digest)
    {
        int ret = ERROR_SUCCESS;
        
        if (with_digest) {
            srs_assert(size == 1536);
        } else {
            srs_assert(size == 1504);
        }
        
        SrsStream stream;
        
        if ((ret = stream.initialize(bytes, size)) != ERROR_SUCCESS) {
            return ret;
        }
        
        copy_time_version(&stream, owner);
        copy_digest(&stream, with_digest);
        copy_key(&stream);
        
        srs_assert(stream.empty());
        
        return ret;
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
    
    int c1s1::dump(char* _c1s1, int size)
    {
        srs_assert(size == 1536);
        srs_assert(payload != NULL);
        return payload->dump(this, _c1s1, size);
    }
    
    int c1s1::parse(char* _c1s1, int size, srs_schema_type schema)
    {
        int ret = ERROR_SUCCESS;
        
        srs_assert(size == 1536);
        
        if (schema != srs_schema0 && schema != srs_schema1) {
            ret = ERROR_RTMP_CH_SCHEMA;
            srs_error("parse c1 failed. invalid schema=%d, ret=%d", schema, ret);
            return ret;
        }
        
        SrsStream stream;
        
        if ((ret = stream.initialize(_c1s1, size)) != ERROR_SUCCESS) {
            return ret;
        }
        
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
    
    int c1s1::c1_create(srs_schema_type schema)
    {
        int ret = ERROR_SUCCESS;
        
        if (schema != srs_schema0 && schema != srs_schema1) {
            ret = ERROR_RTMP_CH_SCHEMA;
            srs_error("create c1 failed. invalid schema=%d, ret=%d", schema, ret);
            return ret;
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
    
    int c1s1::c1_validate_digest(bool& is_valid)
    {
        is_valid = false;
        srs_assert(payload);
        return payload->c1_validate_digest(this, is_valid);
    }
    
    int c1s1::s1_create(c1s1* c1)
    {
        int ret = ERROR_SUCCESS;
        
        if (c1->schema() != srs_schema0 && c1->schema() != srs_schema1) {
            ret = ERROR_RTMP_CH_SCHEMA;
            srs_error("create s1 failed. invalid schema=%d, ret=%d", c1->schema(), ret);
            return ret;
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
    
    int c1s1::s1_validate_digest(bool& is_valid)
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
    
    int c2s2::dump(char* _c2s2, int size)
    {
        srs_assert(size == 1536);
        
        memcpy(_c2s2, random, 1504);
        memcpy(_c2s2 + 1504, digest, 32);
        
        return ERROR_SUCCESS;
    }
    
    int c2s2::parse(char* _c2s2, int size)
    {
        srs_assert(size == 1536);
        
        memcpy(random, _c2s2, 1504);
        memcpy(digest, _c2s2 + 1504, 32);
        
        return ERROR_SUCCESS;
    }
    
    int c2s2::c2_create(c1s1* s1)
    {
        int ret = ERROR_SUCCESS;
        
        char temp_key[SRS_OpensslHashSize];
        if ((ret = openssl_HMACsha256(SrsGenuineFPKey, 62, s1->get_digest(), 32, temp_key)) != ERROR_SUCCESS) {
            srs_error("create c2 temp key failed. ret=%d", ret);
            return ret;
        }
        srs_verbose("generate c2 temp key success.");
        
        char _digest[SRS_OpensslHashSize];
        if ((ret = openssl_HMACsha256(temp_key, 32, random, 1504, _digest)) != ERROR_SUCCESS) {
            srs_error("create c2 digest failed. ret=%d", ret);
            return ret;
        }
        srs_verbose("generate c2 digest success.");
        
        memcpy(digest, _digest, 32);
        
        return ret;
    }
    
    int c2s2::c2_validate(c1s1* s1, bool& is_valid)
    {
        is_valid = false;
        int ret = ERROR_SUCCESS;
        
        char temp_key[SRS_OpensslHashSize];
        if ((ret = openssl_HMACsha256(SrsGenuineFPKey, 62, s1->get_digest(), 32, temp_key)) != ERROR_SUCCESS) {
            srs_error("create c2 temp key failed. ret=%d", ret);
            return ret;
        }
        srs_verbose("generate c2 temp key success.");
        
        char _digest[SRS_OpensslHashSize];
        if ((ret = openssl_HMACsha256(temp_key, 32, random, 1504, _digest)) != ERROR_SUCCESS) {
            srs_error("create c2 digest failed. ret=%d", ret);
            return ret;
        }
        srs_verbose("generate c2 digest success.");
        
        is_valid = srs_bytes_equals(digest, _digest, 32);
        
        return ret;
    }
    
    int c2s2::s2_create(c1s1* c1)
    {
        int ret = ERROR_SUCCESS;
        
        char temp_key[SRS_OpensslHashSize];
        if ((ret = openssl_HMACsha256(SrsGenuineFMSKey, 68, c1->get_digest(), 32, temp_key)) != ERROR_SUCCESS) {
            srs_error("create s2 temp key failed. ret=%d", ret);
            return ret;
        }
        srs_verbose("generate s2 temp key success.");
        
        char _digest[SRS_OpensslHashSize];
        if ((ret = openssl_HMACsha256(temp_key, 32, random, 1504, _digest)) != ERROR_SUCCESS) {
            srs_error("create s2 digest failed. ret=%d", ret);
            return ret;
        }
        srs_verbose("generate s2 digest success.");
        
        memcpy(digest, _digest, 32);
        
        return ret;
    }
    
    int c2s2::s2_validate(c1s1* c1, bool& is_valid)
    {
        is_valid = false;
        int ret = ERROR_SUCCESS;
        
        char temp_key[SRS_OpensslHashSize];
        if ((ret = openssl_HMACsha256(SrsGenuineFMSKey, 68, c1->get_digest(), 32, temp_key)) != ERROR_SUCCESS) {
            srs_error("create s2 temp key failed. ret=%d", ret);
            return ret;
        }
        srs_verbose("generate s2 temp key success.");
        
        char _digest[SRS_OpensslHashSize];
        if ((ret = openssl_HMACsha256(temp_key, 32, random, 1504, _digest)) != ERROR_SUCCESS) {
            srs_error("create s2 digest failed. ret=%d", ret);
            return ret;
        }
        srs_verbose("generate s2 digest success.");
        
        is_valid = srs_bytes_equals(digest, _digest, 32);
        
        return ret;
    }
}

#endif

SrsSimpleHandshake::SrsSimpleHandshake()
{
}

SrsSimpleHandshake::~SrsSimpleHandshake()
{
}

int SrsSimpleHandshake::handshake_with_client(SrsHandshakeBytes* hs_bytes, ISrsProtocolReaderWriter* io)
{
    int ret = ERROR_SUCCESS;
    
    ssize_t nsize;
    
    if ((ret = hs_bytes->read_c0c1(io)) != ERROR_SUCCESS) {
        return ret;
    }

    // plain text required.
    if (hs_bytes->c0c1[0] != 0x03) {
        ret = ERROR_RTMP_PLAIN_REQUIRED;
        srs_warn("only support rtmp plain text. ret=%d", ret);
        return ret;
    }
    srs_verbose("check c0 success, required plain text.");
    
    if ((ret = hs_bytes->create_s0s1s2(hs_bytes->c0c1 + 1)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = io->write(hs_bytes->s0s1s2, 3073, &nsize)) != ERROR_SUCCESS) {
        srs_warn("simple handshake send s0s1s2 failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("simple handshake send s0s1s2 success.");
    
    if ((ret = hs_bytes->read_c2(io)) != ERROR_SUCCESS) {
        return ret;
    }
    
    srs_trace("simple handshake success.");
    
    return ret;
}

int SrsSimpleHandshake::handshake_with_server(SrsHandshakeBytes* hs_bytes, ISrsProtocolReaderWriter* io)
{
    int ret = ERROR_SUCCESS;
    
    ssize_t nsize;
    
    // simple handshake
    if ((ret = hs_bytes->create_c0c1()) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = io->write(hs_bytes->c0c1, 1537, &nsize)) != ERROR_SUCCESS) {
        srs_warn("write c0c1 failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("write c0c1 success.");
    
    if ((ret = hs_bytes->read_s0s1s2(io)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // plain text required.
    if (hs_bytes->s0s1s2[0] != 0x03) {
        ret = ERROR_RTMP_HANDSHAKE;
        srs_warn("handshake failed, plain text required. ret=%d", ret);
        return ret;
    }
    
    if ((ret = hs_bytes->create_c2()) != ERROR_SUCCESS) {
        return ret;
    }
    
    // for simple handshake, copy s1 to c2.
    // @see https://github.com/ossrs/srs/issues/418
    memcpy(hs_bytes->c2, hs_bytes->s0s1s2 + 1, 1536);
    
    if ((ret = io->write(hs_bytes->c2, 1536, &nsize)) != ERROR_SUCCESS) {
        srs_warn("simple handshake write c2 failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("simple handshake write c2 success.");
    
    srs_trace("simple handshake success.");
    
    return ret;
}

SrsComplexHandshake::SrsComplexHandshake()
{
}

SrsComplexHandshake::~SrsComplexHandshake()
{
}

#ifndef SRS_AUTO_SSL
int SrsComplexHandshake::handshake_with_client(SrsHandshakeBytes* /*hs_bytes*/, ISrsProtocolReaderWriter* /*io*/)
{
    srs_trace("directly use simple handshake for ssl disabled.");
    return ERROR_RTMP_TRY_SIMPLE_HS;
}
#else
int SrsComplexHandshake::handshake_with_client(SrsHandshakeBytes* hs_bytes, ISrsProtocolReaderWriter* io)
{
    int ret = ERROR_SUCCESS;

    ssize_t nsize;
    
    if ((ret = hs_bytes->read_c0c1(io)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // decode c1
    c1s1 c1;
    // try schema0.
    // @remark, use schema0 to make flash player happy.
    if ((ret = c1.parse(hs_bytes->c0c1 + 1, 1536, srs_schema0)) != ERROR_SUCCESS) {
        srs_error("parse c1 schema%d error. ret=%d", srs_schema0, ret);
        return ret;
    }
    // try schema1
    bool is_valid = false;
    if ((ret = c1.c1_validate_digest(is_valid)) != ERROR_SUCCESS || !is_valid) {
        srs_info("schema0 failed, try schema1.");
        if ((ret = c1.parse(hs_bytes->c0c1 + 1, 1536, srs_schema1)) != ERROR_SUCCESS) {
            srs_error("parse c1 schema%d error. ret=%d", srs_schema1, ret);
            return ret;
        }
        
        if ((ret = c1.c1_validate_digest(is_valid)) != ERROR_SUCCESS || !is_valid) {
            ret = ERROR_RTMP_TRY_SIMPLE_HS;
            srs_info("all schema valid failed, try simple handshake. ret=%d", ret);
            return ret;
        }
    } else {
        srs_info("schema0 is ok.");
    }
    srs_verbose("decode c1 success.");
    
    // encode s1
    c1s1 s1;
    if ((ret = s1.s1_create(&c1)) != ERROR_SUCCESS) {
        srs_error("create s1 from c1 failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("create s1 from c1 success.");
    // verify s1
    if ((ret = s1.s1_validate_digest(is_valid)) != ERROR_SUCCESS || !is_valid) {
        ret = ERROR_RTMP_TRY_SIMPLE_HS;
        srs_info("verify s1 failed, try simple handshake. ret=%d", ret);
        return ret;
    }
    srs_verbose("verify s1 success.");
    
    c2s2 s2;
    if ((ret = s2.s2_create(&c1)) != ERROR_SUCCESS) {
        srs_error("create s2 from c1 failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("create s2 from c1 success.");
    // verify s2
    if ((ret = s2.s2_validate(&c1, is_valid)) != ERROR_SUCCESS || !is_valid) {
        ret = ERROR_RTMP_TRY_SIMPLE_HS;
        srs_info("verify s2 failed, try simple handshake. ret=%d", ret);
        return ret;
    }
    srs_verbose("verify s2 success.");
    
    // sendout s0s1s2
    if ((ret = hs_bytes->create_s0s1s2()) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = s1.dump(hs_bytes->s0s1s2 + 1, 1536)) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = s2.dump(hs_bytes->s0s1s2 + 1537, 1536)) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = io->write(hs_bytes->s0s1s2, 3073, &nsize)) != ERROR_SUCCESS) {
        srs_warn("complex handshake send s0s1s2 failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("complex handshake send s0s1s2 success.");
    
    // recv c2
    if ((ret = hs_bytes->read_c2(io)) != ERROR_SUCCESS) {
        return ret;
    }
    c2s2 c2;
    if ((ret = c2.parse(hs_bytes->c2, 1536)) != ERROR_SUCCESS) {
        return ret;
    }
    srs_verbose("complex handshake read c2 success.");
    
    // verify c2
    // never verify c2, for ffmpeg will failed.
    // it's ok for flash.
    
    srs_trace("complex handshake success");
    
    return ret;
}
#endif

#ifndef SRS_AUTO_SSL
int SrsComplexHandshake::handshake_with_server(SrsHandshakeBytes* /*hs_bytes*/, ISrsProtocolReaderWriter* /*io*/)
{
    return ERROR_RTMP_TRY_SIMPLE_HS;
}
#else
int SrsComplexHandshake::handshake_with_server(SrsHandshakeBytes* hs_bytes, ISrsProtocolReaderWriter* io)
{
    int ret = ERROR_SUCCESS;

    ssize_t nsize;
    
    // complex handshake
    if ((ret = hs_bytes->create_c0c1()) != ERROR_SUCCESS) {
        return ret;
    }
    
    // sign c1
    c1s1 c1;
    // @remark, FMS requires the schema1(digest-key), or connect failed.
    if ((ret = c1.c1_create(srs_schema1)) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = c1.dump(hs_bytes->c0c1 + 1, 1536)) != ERROR_SUCCESS) {
        return ret;
    }

    // verify c1
    bool is_valid;
    if ((ret = c1.c1_validate_digest(is_valid)) != ERROR_SUCCESS || !is_valid) {
        ret = ERROR_RTMP_TRY_SIMPLE_HS;
        return ret;
    }
    
    if ((ret = io->write(hs_bytes->c0c1, 1537, &nsize)) != ERROR_SUCCESS) {
        srs_warn("write c0c1 failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("write c0c1 success.");
    
    // s0s1s2
    if ((ret = hs_bytes->read_s0s1s2(io)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // plain text required.
    if (hs_bytes->s0s1s2[0] != 0x03) {
        ret = ERROR_RTMP_HANDSHAKE;
        srs_warn("handshake failed, plain text required. ret=%d", ret);
        return ret;
    }
    
    // verify s1s2
    c1s1 s1;
    if ((ret = s1.parse(hs_bytes->s0s1s2 + 1, 1536, c1.schema())) != ERROR_SUCCESS) {
        return ret;
    }
    
    // never verify the s1,
    // for if forward to nginx-rtmp, verify s1 will failed,
    // TODO: FIXME: find the handshake schema of nginx-rtmp.
    
    // c2
    if ((ret = hs_bytes->create_c2()) != ERROR_SUCCESS) {
        return ret;
    }

    c2s2 c2;
    if ((ret = c2.c2_create(&s1)) != ERROR_SUCCESS) {
        return ret;
    }

    if ((ret = c2.dump(hs_bytes->c2, 1536)) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = io->write(hs_bytes->c2, 1536, &nsize)) != ERROR_SUCCESS) {
        srs_warn("complex handshake write c2 failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("complex handshake write c2 success.");
    
    srs_trace("complex handshake success.");
    
    return ret;
}
#endif


