/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

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

#include <srs_protocol_handshake.hpp>

#include <time.h>

#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_protocol_io.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_protocol_rtmp.hpp>
#include <srs_kernel_stream.hpp>

#ifdef SRS_AUTO_SSL

using namespace _srs_internal;

// for openssl_HMACsha256
#include <openssl/evp.h>
#include <openssl/hmac.h>
// for __openssl_generate_key
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
    
    int openssl_HMACsha256(const void* data, int data_size, const void* key, int key_size, void* digest) 
    {
        HMAC_CTX ctx;
        
        HMAC_CTX_init(&ctx);
        HMAC_Init_ex(&ctx, (unsigned char*) key, key_size, EVP_sha256(), NULL);
        HMAC_Update(&ctx, (unsigned char *) data, data_size);
    
        unsigned int digest_size;
        HMAC_Final(&ctx, (unsigned char *) digest, &digest_size);
        
        HMAC_CTX_cleanup(&ctx);
        
        if (digest_size != 32) {
            return ERROR_OpenSslSha256DigestSize;
        }
        
        return ERROR_SUCCESS;
    }
    
    #define RFC2409_PRIME_1024 \
            "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1" \
            "29024E088A67CC74020BBEA63B139B22514A08798E3404DD" \
            "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245" \
            "E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED" \
            "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE65381" \
            "FFFFFFFFFFFFFFFF"
    int __openssl_generate_key(
        u_int8_t* _private_key, u_int8_t* _public_key, int32_t& size,
        DH*& pdh, int32_t& bits_count, u_int8_t*& shared_key, int32_t& shared_key_length, BIGNUM*& peer_public_key
    ){
        int ret = ERROR_SUCCESS;
    
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
    
        //3. initialize p, g and key length
        if (BN_hex2bn(&pdh->p, RFC2409_PRIME_1024) == 0) {
            ret = ERROR_OpenSslParseP1024; 
            return ret;
        }
        if (BN_set_word(pdh->g, 2) != 1) {
            ret = ERROR_OpenSslSetG;
            return ret;
        }
    
        //4. Set the key length
        pdh->length = bits_count;
    
        //5. Generate private and public key
        if (DH_generate_key(pdh) != 1) {
            ret = ERROR_OpenSslGenerateDHKeys;
            return ret;
        }
    
        // CreateSharedKey
        if (pdh == NULL) {
            ret = ERROR_OpenSslGenerateDHKeys;
            return ret;
        }
    
        if (shared_key_length != 0 || shared_key != NULL) {
            ret = ERROR_OpenSslShareKeyComputed;
            return ret;
        }
    
        shared_key_length = DH_size(pdh);
        if (shared_key_length <= 0 || shared_key_length > 1024) {
            ret = ERROR_OpenSslGetSharedKeySize;
            return ret;
        }
        shared_key = new u_int8_t[shared_key_length];
        memset(shared_key, 0, shared_key_length);
    
        peer_public_key = BN_bin2bn(_private_key, size, 0);
        if (peer_public_key == NULL) {
            ret = ERROR_OpenSslGetPeerPublicKey;
            return ret;
        }
    
        if (DH_compute_key(shared_key, peer_public_key, pdh) == -1) {
            ret = ERROR_OpenSslComputeSharedKey;
            return ret;
        }
    
        // CopyPublicKey
        if (pdh == NULL) {
            ret = ERROR_OpenSslComputeSharedKey;
            return ret;
        }
        
        int32_t keySize = BN_num_bytes(pdh->pub_key);
        if ((keySize <= 0) || (size <= 0) || (keySize > size)) {
            //("CopyPublicKey failed due to either invalid DH state or invalid call"); return ret;
            ret = ERROR_OpenSslInvalidDHState; 
            return ret;
        }
    
        if (BN_bn2bin(pdh->pub_key, _public_key) != keySize) {
            //("Unable to copy key"); return ret;
            ret = ERROR_OpenSslCopyKey; 
            return ret;
        }
        
        return ret;
    }
    int openssl_generate_key(char* _private_key, char* _public_key, int32_t size)
    {
        int ret = ERROR_SUCCESS;
    
        // Initialize
        DH* pdh = NULL;
        int32_t bits_count = 1024; 
        u_int8_t* shared_key = NULL;
        int32_t shared_key_length = 0;
        BIGNUM* peer_public_key = NULL;
        
        ret = __openssl_generate_key(
            (u_int8_t*)_private_key, (u_int8_t*)_public_key, size,
            pdh, bits_count, shared_key, shared_key_length, peer_public_key
        );
        
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
    
        if (shared_key != NULL) {
            delete[] shared_key;
            shared_key = NULL;
        }
    
        if (peer_public_key != NULL) {
            BN_free(peer_public_key);
            peer_public_key = NULL;
        }
    
        return ret;
    }
    
    // read/write stream using SrsStream.
    void __srs_stream_write_4bytes(char* pp, int32_t value) 
    {
        static SrsStream stream;
        
        int ret = stream.initialize(pp, 4);
        srs_assert(ret == ERROR_SUCCESS);
        
        stream.write_4bytes(value);
    }
    int32_t __srs_stream_read_4bytes(char* pp)
    {
        static SrsStream stream;
        
        int ret = stream.initialize(pp, 4);
        srs_assert(ret == ERROR_SUCCESS);
        
        return stream.read_4bytes();
    }
    
    // calc the offset of key,
    // the key->offset cannot be used as the offset of key.
    int srs_key_block_get_offset(key_block* key)
    {
        int max_offset_size = 764 - 128 - 4;
        
        int offset = 0;
        u_int8_t* pp = (u_int8_t*)&key->offset;
        offset += *pp++;
        offset += *pp++;
        offset += *pp++;
        offset += *pp++;
    
        return offset % max_offset_size;
    }
    
    // create new key block data.
    // if created, user must free it by srs_key_block_free
    void srs_key_block_init(key_block* key)
    {
        key->offset = (int32_t)rand();
        key->random0 = NULL;
        key->random1 = NULL;
        
        int offset = srs_key_block_get_offset(key);
        srs_assert(offset >= 0);
        
        key->random0_size = offset;
        if (key->random0_size > 0) {
            key->random0 = new char[key->random0_size];
            srs_random_generate(key->random0, key->random0_size);
        }
        
        srs_random_generate(key->key, sizeof(key->key));
        
        key->random1_size = 764 - offset - 128 - 4;
        if (key->random1_size > 0) {
            key->random1 = new char[key->random1_size];
            srs_random_generate(key->random1, key->random1_size);
        }
    }
    
    // parse key block from c1s1.
    // if created, user must free it by srs_key_block_free
    // @c1s1_key_bytes the key start bytes, maybe c1s1 or c1s1+764
    int srs_key_block_parse(key_block* key, char* c1s1_key_bytes)
    {
        int ret = ERROR_SUCCESS;
    
        char* pp = c1s1_key_bytes + 764;
        
        pp -= sizeof(int32_t);
        key->offset = __srs_stream_read_4bytes(pp);
        
        key->random0 = NULL;
        key->random1 = NULL;
        
        int offset = srs_key_block_get_offset(key);
        srs_assert(offset >= 0);
        
        pp = c1s1_key_bytes;
        key->random0_size = offset;
        if (key->random0_size > 0) {
            key->random0 = new char[key->random0_size];
            memcpy(key->random0, pp, key->random0_size);
        }
        pp += key->random0_size;
        
        memcpy(key->key, pp, sizeof(key->key));
        pp += sizeof(key->key);
        
        key->random1_size = 764 - offset - 128 - 4;
        if (key->random1_size > 0) {
            key->random1 = new char[key->random1_size];
            memcpy(key->random1, pp, key->random1_size);
        }
        
        return ret;
    }
    
    // free the block data create by 
    // srs_key_block_init or srs_key_block_parse
    void srs_key_block_free(key_block* key)
    {
        if (key->random0) {
            srs_freep(key->random0);
        }
        if (key->random1) {
            srs_freep(key->random1);
        }
    }
    
    // calc the offset of digest,
    // the key->offset cannot be used as the offset of digest.
    int srs_digest_block_get_offset(digest_block* digest)
    {
        int max_offset_size = 764 - 32 - 4;
        
        int offset = 0;
        u_int8_t* pp = (u_int8_t*)&digest->offset;
        offset += *pp++;
        offset += *pp++;
        offset += *pp++;
        offset += *pp++;
    
        return offset % max_offset_size;
    }
    
    // create new digest block data.
    // if created, user must free it by srs_digest_block_free
    void srs_digest_block_init(digest_block* digest)
    {
        digest->offset = (int32_t)rand();
        digest->random0 = NULL;
        digest->random1 = NULL;
        
        int offset = srs_digest_block_get_offset(digest);
        srs_assert(offset >= 0);
        
        digest->random0_size = offset;
        if (digest->random0_size > 0) {
            digest->random0 = new char[digest->random0_size];
            srs_random_generate(digest->random0, digest->random0_size);
        }
        
        srs_random_generate(digest->digest, sizeof(digest->digest));
        
        digest->random1_size = 764 - 4 - offset - 32;
        if (digest->random1_size > 0) {
            digest->random1 = new char[digest->random1_size];
            srs_random_generate(digest->random1, digest->random1_size);
        }
    }

    // parse digest block from c1s1.
    // if created, user must free it by srs_digest_block_free
    // @c1s1_digest_bytes the digest start bytes, maybe c1s1 or c1s1+764
    int srs_digest_block_parse(digest_block* digest, char* c1s1_digest_bytes)
    {
        int ret = ERROR_SUCCESS;
    
        char* pp = c1s1_digest_bytes;
        
        digest->offset = __srs_stream_read_4bytes(pp);
        pp += sizeof(int32_t);
        
        digest->random0 = NULL;
        digest->random1 = NULL;
        
        int offset = srs_digest_block_get_offset(digest);
        srs_assert(offset >= 0);
        
        digest->random0_size = offset;
        if (digest->random0_size > 0) {
            digest->random0 = new char[digest->random0_size];
            memcpy(digest->random0, pp, digest->random0_size);
        }
        pp += digest->random0_size;
        
        memcpy(digest->digest, pp, sizeof(digest->digest));
        pp += sizeof(digest->digest);
        
        digest->random1_size = 764 - 4 - offset - 32;
        if (digest->random1_size > 0) {
            digest->random1 = new char[digest->random1_size];
            memcpy(digest->random1, pp, digest->random1_size);
        }
        
        return ret;
    }
    
    // free the block data create by 
    // srs_digest_block_init or srs_digest_block_parse
    void srs_digest_block_free(digest_block* digest)
    {
        if (digest->random0) {
            srs_freep(digest->random0);
        }
        if (digest->random1) {
            srs_freep(digest->random1);
        }
    }
    
    void __srs_time_copy_to(char*& pp, int32_t time)
    {
        // 4bytes time
        __srs_stream_write_4bytes(pp, time);
        pp += 4;
    }
    void __srs_version_copy_to(char*& pp, int32_t version)
    {
        // 4bytes version
        __srs_stream_write_4bytes(pp, version);
        pp += 4;
    }
    void __srs_key_copy_to(char*& pp, key_block* key)
    {
        // 764bytes key block
        if (key->random0_size > 0) {
            memcpy(pp, key->random0, key->random0_size);
        }
        pp += key->random0_size;
        
        memcpy(pp, key->key, sizeof(key->key));
        pp += sizeof(key->key);
        
        if (key->random1_size > 0) {
            memcpy(pp, key->random1, key->random1_size);
        }
        pp += key->random1_size;
        
        __srs_stream_write_4bytes(pp, key->offset);
        pp += 4;
    }
    void __srs_digest_copy_to(char*& pp, digest_block* digest, bool with_digest)
    {
        // 732bytes digest block without the 32bytes digest-data
        // nbytes digest block part1
        __srs_stream_write_4bytes(pp, digest->offset);
        pp += 4;
        
        // digest random padding.
        if (digest->random0_size > 0) {
            memcpy(pp, digest->random0, digest->random0_size);
        }
        pp += digest->random0_size;
        
        // digest
        if (with_digest) {
            memcpy(pp, digest->digest, 32);
            pp += 32;
        }
        
        // nbytes digest block part2
        if (digest->random1_size > 0) {
            memcpy(pp, digest->random1, digest->random1_size);
        }
        pp += digest->random1_size;
    }
    
    /**
    * copy whole c1s1 to bytes.
    */
    void srs_schema0_copy_to(char* bytes, bool with_digest, 
        int32_t time, int32_t version, key_block* key, digest_block* digest)
    {
        char* pp = bytes;
    
        __srs_time_copy_to(pp, time);
        __srs_version_copy_to(pp, version);
        __srs_key_copy_to(pp, key);
        __srs_digest_copy_to(pp, digest, with_digest);
        
        if (with_digest) {
            srs_assert(pp - bytes == 1536);
        } else {
            srs_assert(pp - bytes == 1536 - 32);
        }
    }
    void srs_schema1_copy_to(char* bytes, bool with_digest, 
        int32_t time, int32_t version, digest_block* digest, key_block* key)
    {
        char* pp = bytes;
    
        __srs_time_copy_to(pp, time);
        __srs_version_copy_to(pp, version);
        __srs_digest_copy_to(pp, digest, with_digest);
        __srs_key_copy_to(pp, key);
        
        if (with_digest) {
            srs_assert(pp - bytes == 1536);
        } else {
            srs_assert(pp - bytes == 1536 - 32);
        }
    }
    
    /**
    * c1s1 is splited by digest:
    *     c1s1-part1: n bytes (time, version, key and digest-part1).
    *     digest-data: 32bytes
    *     c1s1-part2: (1536-n-32)bytes (digest-part2)
    */
    char* srs_bytes_join_schema0(int32_t time, int32_t version, key_block* key, digest_block* digest)
    {
        char* bytes = new char[1536 -32];
        
        srs_schema0_copy_to(bytes, false, time, version, key, digest);
        
        return bytes;
    }
    
    /**
    * c1s1 is splited by digest:
    *     c1s1-part1: n bytes (time, version and digest-part1).
    *     digest-data: 32bytes
    *     c1s1-part2: (1536-n-32)bytes (digest-part2 and key)
    */
    char* srs_bytes_join_schema1(int32_t time, int32_t version, digest_block* digest, key_block* key)
    {
        char* bytes = new char[1536 -32];
        
        srs_schema1_copy_to(bytes, false, time, version, digest, key);
        
        return bytes;
    }
    
    c2s2::c2s2()
    {
        srs_random_generate(random, 1504);
        srs_random_generate(digest, 32);
    }
    
    c2s2::~c2s2()
    {
    }
    
    void c2s2::dump(char* _c2s2)
    {
        memcpy(_c2s2, random, 1504);
        memcpy(_c2s2 + 1504, digest, 32);
    }
    
    void c2s2::parse(char* _c2s2)
    {
        memcpy(random, _c2s2, 1504);
        memcpy(digest, _c2s2 + 1504, 32);
    }
    
    int c2s2::c2_create(c1s1* s1)
    {
        int ret = ERROR_SUCCESS;
        
        char temp_key[__SRS_OpensslHashSize];
        if ((ret = openssl_HMACsha256(s1->get_digest(), 32, SrsGenuineFPKey, 62, temp_key)) != ERROR_SUCCESS) {
            srs_error("create c2 temp key failed. ret=%d", ret);
            return ret;
        }
        srs_verbose("generate c2 temp key success.");
        
        char _digest[__SRS_OpensslHashSize];
        if ((ret = openssl_HMACsha256(random, 1504, temp_key, 32, _digest)) != ERROR_SUCCESS) {
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
        
        char temp_key[__SRS_OpensslHashSize];
        if ((ret = openssl_HMACsha256(s1->get_digest(), 32, SrsGenuineFPKey, 62, temp_key)) != ERROR_SUCCESS) {
            srs_error("create c2 temp key failed. ret=%d", ret);
            return ret;
        }
        srs_verbose("generate c2 temp key success.");
        
        char _digest[__SRS_OpensslHashSize];
        if ((ret = openssl_HMACsha256(random, 1504, temp_key, 32, _digest)) != ERROR_SUCCESS) {
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
        
        char temp_key[__SRS_OpensslHashSize];
        if ((ret = openssl_HMACsha256(c1->get_digest(), 32, SrsGenuineFMSKey, 68, temp_key)) != ERROR_SUCCESS) {
            srs_error("create s2 temp key failed. ret=%d", ret);
            return ret;
        }
        srs_verbose("generate s2 temp key success.");
        
        char _digest[__SRS_OpensslHashSize];
        if ((ret = openssl_HMACsha256(random, 1504, temp_key, 32, _digest)) != ERROR_SUCCESS) {
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
        
        char temp_key[__SRS_OpensslHashSize];
        if ((ret = openssl_HMACsha256(c1->get_digest(), 32, SrsGenuineFMSKey, 68, temp_key)) != ERROR_SUCCESS) {
            srs_error("create s2 temp key failed. ret=%d", ret);
            return ret;
        }
        srs_verbose("generate s2 temp key success.");
        
        char _digest[__SRS_OpensslHashSize];
        if ((ret = openssl_HMACsha256(random, 1504, temp_key, 32, _digest)) != ERROR_SUCCESS) {
            srs_error("create s2 digest failed. ret=%d", ret);
            return ret;
        }
        srs_verbose("generate s2 digest success.");
        
        is_valid = srs_bytes_equals(digest, _digest, 32);
        
        return ret;
    }
    
    // TODO: FIXME: move to the right position.
    c1s1::c1s1()
    {
        schema = srs_schema_invalid;
    }
    c1s1::~c1s1()
    {
        destroy_blocks();
    }
    
    char* c1s1::get_digest()
    {
        srs_assert(schema != srs_schema_invalid);
        
        if (schema == srs_schema0) {
            return block1.digest.digest;
        } else {
            return block0.digest.digest;
        }
    }
    
    void c1s1::dump(char* _c1s1)
    {
        srs_assert(schema != srs_schema_invalid);
        
        if (schema == srs_schema0) {
            srs_schema0_copy_to(_c1s1, true, time, version, &block0.key, &block1.digest);
        } else {
            srs_schema1_copy_to(_c1s1, true, time, version, &block0.digest, &block1.key);
        }
    }
    
    int c1s1::parse(char* _c1s1, srs_schema_type _schema)
    {
        int ret = ERROR_SUCCESS;
        
        if (_schema == srs_schema_invalid) {
            ret = ERROR_RTMP_CH_SCHEMA;
            srs_error("parse c1 failed. invalid schema=%d, ret=%d", _schema, ret);
            return ret;
        }
        
        destroy_blocks();
        
        
        time = __srs_stream_read_4bytes(_c1s1);
        version = __srs_stream_read_4bytes(_c1s1 + 4); // client c1 version
        
        if (_schema == srs_schema0) {
            if ((ret = srs_key_block_parse(&block0.key, _c1s1 + 8)) != ERROR_SUCCESS) {
                srs_error("parse the c1 key failed. ret=%d", ret);
                return ret;
            }
            if ((ret = srs_digest_block_parse(&block1.digest, _c1s1 + 8 + 764)) != ERROR_SUCCESS) {
                srs_error("parse the c1 digest failed. ret=%d", ret);
                return ret;
            }
            srs_verbose("parse c1 key-digest success");
        } else if (_schema == srs_schema1) {
            if ((ret = srs_digest_block_parse(&block0.digest, _c1s1 + 8)) != ERROR_SUCCESS) {
                srs_error("parse the c1 key failed. ret=%d", ret);
                return ret;
            }
            if ((ret = srs_key_block_parse(&block1.key, _c1s1 + 8 + 764)) != ERROR_SUCCESS) {
                srs_error("parse the c1 digest failed. ret=%d", ret);
                return ret;
            }
            srs_verbose("parse c1 digest-key success");
        } else {
            ret = ERROR_RTMP_CH_SCHEMA;
            srs_error("parse c1 failed. invalid schema=%d, ret=%d", _schema, ret);
            return ret;
        }
        
        schema = _schema;
        
        return ret;
    }
    
    int c1s1::c1_create(srs_schema_type _schema)
    {
        int ret = ERROR_SUCCESS;
        
        if (_schema == srs_schema_invalid) {
            ret = ERROR_RTMP_CH_SCHEMA;
            srs_error("create c1 failed. invalid schema=%d, ret=%d", _schema, ret);
            return ret;
        }
        
        destroy_blocks();
        
        // client c1 time and version
        time = ::time(NULL);
        version = 0x80000702; // client c1 version
        
        // generate signature by schema
        if (_schema == srs_schema0) {
            srs_key_block_init(&block0.key);
            srs_digest_block_init(&block1.digest);
        } else {
            srs_digest_block_init(&block0.digest);
            srs_key_block_init(&block1.key);
        }
        
        schema = _schema;
        
        // generate digest
        char* digest = NULL;
        
        if ((ret = calc_c1_digest(digest)) != ERROR_SUCCESS) {
            srs_error("sign c1 error, failed to calc digest. ret=%d", ret);
            return ret;
        }
        
        srs_assert(digest != NULL);
        SrsAutoFree(char, digest);
        
        if (schema == srs_schema0) {
            memcpy(block1.digest.digest, digest, 32);
        } else {
            memcpy(block0.digest.digest, digest, 32);
        }
        
        return ret;
    }
    
    int c1s1::c1_validate_digest(bool& is_valid)
    {
        is_valid = false;
        int ret = ERROR_SUCCESS;
        
        char* c1_digest = NULL;
        
        if ((ret = calc_c1_digest(c1_digest)) != ERROR_SUCCESS) {
            srs_error("validate c1 error, failed to calc digest. ret=%d", ret);
            return ret;
        }
        
        srs_assert(c1_digest != NULL);
        SrsAutoFree(char, c1_digest);
        
        if (schema == srs_schema0) {
            is_valid = srs_bytes_equals(block1.digest.digest, c1_digest, 32);
        } else {
            is_valid = srs_bytes_equals(block0.digest.digest, c1_digest, 32);
        }
        
        return ret;
    }
    
    int c1s1::s1_validate_digest(bool& is_valid)
    {
        is_valid = false;
        int ret = ERROR_SUCCESS;
        
        char* s1_digest = NULL;
        
        if ((ret = calc_s1_digest(s1_digest)) != ERROR_SUCCESS) {
            srs_error("validate s1 error, failed to calc digest. ret=%d", ret);
            return ret;
        }
        
        srs_assert(s1_digest != NULL);
        SrsAutoFree(char, s1_digest);
        
        if (schema == srs_schema0) {
            is_valid = srs_bytes_equals(block1.digest.digest, s1_digest, 32);
        } else {
            is_valid = srs_bytes_equals(block0.digest.digest, s1_digest, 32);
        }
        
        return ret;
    }
    
    int c1s1::s1_create(c1s1* c1)
    {
        int ret = ERROR_SUCCESS;
        
        if (c1->schema == srs_schema_invalid) {
            ret = ERROR_RTMP_CH_SCHEMA;
            srs_error("create s1 failed. invalid schema=%d, ret=%d", c1->schema, ret);
            return ret;
        }
        
        destroy_blocks();
        schema = c1->schema;
        
        time = ::time(NULL);
        version = 0x01000504; // server s1 version
        
        if (schema == srs_schema0) {
            srs_key_block_init(&block0.key);
            srs_digest_block_init(&block1.digest);
        } else {
            srs_digest_block_init(&block0.digest);
            srs_key_block_init(&block1.key);
        }
        
        if (schema == srs_schema0) {
            if ((ret = openssl_generate_key(c1->block0.key.key, block0.key.key, 128)) != ERROR_SUCCESS) {
                srs_error("calc s1 key failed. ret=%d", ret);
                return ret;
            }
        } else {
            if ((ret = openssl_generate_key(c1->block1.key.key, block1.key.key, 128)) != ERROR_SUCCESS) {
                srs_error("calc s1 key failed. ret=%d", ret);
                return ret;
            }
        }
        srs_verbose("calc s1 key success.");
            
        char* s1_digest = NULL;
        if ((ret = calc_s1_digest(s1_digest))  != ERROR_SUCCESS) {
            srs_error("calc s1 digest failed. ret=%d", ret);
            return ret;
        }
        srs_verbose("calc s1 digest success.");
        
        srs_assert(s1_digest != NULL);
        SrsAutoFree(char, s1_digest);
        
        if (schema == srs_schema0) {
            memcpy(block1.digest.digest, s1_digest, 32);
        } else {
            memcpy(block0.digest.digest, s1_digest, 32);
        }
        srs_verbose("copy s1 key success.");
        
        return ret;
    }
    
    int c1s1::calc_s1_digest(char*& digest)
    {
        int ret = ERROR_SUCCESS;
        
        srs_assert(schema == srs_schema0 || schema == srs_schema1);
        
        char* c1s1_joined_bytes = NULL;
    
        if (schema == srs_schema0) {
            c1s1_joined_bytes = srs_bytes_join_schema0(time, version, &block0.key, &block1.digest);
        } else {
            c1s1_joined_bytes = srs_bytes_join_schema1(time, version, &block0.digest, &block1.key);
        }
        
        srs_assert(c1s1_joined_bytes != NULL);
        SrsAutoFree(char, c1s1_joined_bytes);
        
        digest = new char[__SRS_OpensslHashSize];
        if ((ret = openssl_HMACsha256(c1s1_joined_bytes, 1536 - 32, SrsGenuineFMSKey, 36, digest)) != ERROR_SUCCESS) {
            srs_error("calc digest for s1 failed. ret=%d", ret);
            return ret;
        }
        srs_verbose("digest calculated for s1");
        
        return ret;
    }
    
    int c1s1::calc_c1_digest(char*& digest)
    {
        int ret = ERROR_SUCCESS;
        
        srs_assert(schema == srs_schema0 || schema == srs_schema1);
        
        char* c1s1_joined_bytes = NULL;
    
        if (schema == srs_schema0) {
            c1s1_joined_bytes = srs_bytes_join_schema0(time, version, &block0.key, &block1.digest);
        } else {
            c1s1_joined_bytes = srs_bytes_join_schema1(time, version, &block0.digest, &block1.key);
        }
        
        srs_assert(c1s1_joined_bytes != NULL);
        SrsAutoFree(char, c1s1_joined_bytes);
        
        digest = new char[__SRS_OpensslHashSize];
        if ((ret = openssl_HMACsha256(c1s1_joined_bytes, 1536 - 32, SrsGenuineFPKey, 30, digest)) != ERROR_SUCCESS) {
            srs_error("calc digest for c1 failed. ret=%d", ret);
            return ret;
        }
        srs_verbose("digest calculated for c1");
        
        return ret;
    }
    
    void c1s1::destroy_blocks()
    {
        if (schema == srs_schema_invalid) {
            return;
        }
        
        if (schema == srs_schema0) {
            srs_key_block_free(&block0.key);
            srs_digest_block_free(&block1.digest);
        } else {
            srs_digest_block_free(&block0.digest);
            srs_key_block_free(&block1.key);
        }
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
    if ((ret = c1.parse(hs_bytes->c0c1 + 1, srs_schema0)) != ERROR_SUCCESS) {
        srs_error("parse c1 schema%d error. ret=%d", srs_schema0, ret);
        return ret;
    }
    // try schema1
    bool is_valid = false;
    if ((ret = c1.c1_validate_digest(is_valid)) != ERROR_SUCCESS || !is_valid) {
        if ((ret = c1.parse(hs_bytes->c0c1 + 1, srs_schema1)) != ERROR_SUCCESS) {
            srs_error("parse c1 schema%d error. ret=%d", srs_schema1, ret);
            return ret;
        }
        
        if ((ret = c1.c1_validate_digest(is_valid)) != ERROR_SUCCESS || !is_valid) {
            ret = ERROR_RTMP_TRY_SIMPLE_HS;
            srs_info("all schema valid failed, try simple handshake. ret=%d", ret);
            return ret;
        }
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
    s1.dump(hs_bytes->s0s1s2 + 1);
    s2.dump(hs_bytes->s0s1s2 + 1537);
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
    c2.parse(hs_bytes->c2);
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
    c1.dump(hs_bytes->c0c1 + 1);
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
    if ((ret = s1.parse(hs_bytes->s0s1s2 + 1, c1.schema)) != ERROR_SUCCESS) {
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
    c2.dump(hs_bytes->c2);
    if ((ret = io->write(hs_bytes->c2, 1536, &nsize)) != ERROR_SUCCESS) {
        srs_warn("complex handshake write c2 failed. ret=%d", ret);
        return ret;
    }
    srs_verbose("complex handshake write c2 success.");
    
    srs_trace("complex handshake success.");
    
    return ret;
}
#endif


