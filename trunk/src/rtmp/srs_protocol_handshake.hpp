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

#ifndef SRS_RTMP_PROTOCOL_HANDSHKAE_HPP
#define SRS_RTMP_PROTOCOL_HANDSHKAE_HPP

/*
#include <srs_protocol_handshake.hpp>
*/

#include <srs_core.hpp>

class ISrsProtocolReaderWriter;
class SrsComplexHandshake;
class SrsHandshakeBytes;

#ifdef SRS_AUTO_SSL

namespace _srs_internal
{
    /**
    * the schema type.
    */
    enum srs_schema_type 
    {
        srs_schema_invalid = 2,
        
        /**
        * key-digest sequence
        */
        srs_schema0 = 0,
        
        /**
        * digest-key sequence
        * @remark, FMS requires the schema1(digest-key), or connect failed.
        */
        // 
        srs_schema1 = 1,
    };
    
    /**
    * 764bytes key structure
    *     random-data: (offset)bytes
    *     key-data: 128bytes
    *     random-data: (764-offset-128-4)bytes
    *     offset: 4bytes
    * @see also: http://blog.csdn.net/win_lin/article/details/13006803
    */
    class key_block
    {
    public:
        // (offset)bytes
        char* random0;
        int random0_size;
        
        // 128bytes
        char key[128];
        
        // (764-offset-128-4)bytes
        char* random1;
        int random1_size;
        
        // 4bytes
        int32_t offset;
    };
    
    /**
    * 764bytes digest structure
    *     offset: 4bytes
    *     random-data: (offset)bytes
    *     digest-data: 32bytes
    *     random-data: (764-4-offset-32)bytes
    * @see also: http://blog.csdn.net/win_lin/article/details/13006803
    */
    class digest_block
    {
    public:
        // 4bytes
        int32_t offset;
        
        // (offset)bytes
        char* random0;
        int random0_size;
        
        // 32bytes
        char digest[32];
        
        // (764-4-offset-32)bytes
        char* random1;
        int random1_size;
    };
    
    // the digest key generate size.
    #define __SRS_OpensslHashSize 512
    extern u_int8_t SrsGenuineFMSKey[];
    extern u_int8_t SrsGenuineFPKey[];
    int openssl_HMACsha256(const void* data, int data_size, const void* key, int key_size, void* digest);
    int openssl_generate_key(char* _private_key, char* _public_key, int32_t size);
    
    // calc the offset of key,
    // the key->offset cannot be used as the offset of key.
    int srs_key_block_get_offset(key_block* key);
    
    // create new key block data.
    // if created, user must free it by srs_key_block_free
    void srs_key_block_init(key_block* key);
    
    // parse key block from c1s1.
    // if created, user must free it by srs_key_block_free
    // @c1s1_key_bytes the key start bytes, maybe c1s1 or c1s1+764
    int srs_key_block_parse(key_block* key, char* c1s1_key_bytes);
    
    // free the block data create by 
    // srs_key_block_init or srs_key_block_parse
    void srs_key_block_free(key_block* key);
    
    // calc the offset of digest,
    // the key->offset cannot be used as the offset of digest.
    int srs_digest_block_get_offset(digest_block* digest);
    
    // create new digest block data.
    // if created, user must free it by srs_digest_block_free
    void srs_digest_block_init(digest_block* digest);

    // parse digest block from c1s1.
    // if created, user must free it by srs_digest_block_free
    // @c1s1_digest_bytes the digest start bytes, maybe c1s1 or c1s1+764
    int srs_digest_block_parse(digest_block* digest, char* c1s1_digest_bytes);
    
    // free the block data create by 
    // srs_digest_block_init or srs_digest_block_parse
    void srs_digest_block_free(digest_block* digest);
    
    /**
    * copy whole c1s1 to bytes.
    */
    void srs_schema0_copy_to(char* bytes, bool with_digest, 
        int32_t time, int32_t version, key_block* key, digest_block* digest);
    void srs_schema1_copy_to(char* bytes, bool with_digest, 
        int32_t time, int32_t version, digest_block* digest, key_block* key);
    
    /**
    * c1s1 is splited by digest:
    *     c1s1-part1: n bytes (time, version, key and digest-part1).
    *     digest-data: 32bytes
    *     c1s1-part2: (1536-n-32)bytes (digest-part2)
    * @return a new allocated bytes, user must free it.
    */
    char* srs_bytes_join_schema0(int32_t time, int32_t version, key_block* key, digest_block* digest);
    
    /**
    * c1s1 is splited by digest:
    *     c1s1-part1: n bytes (time, version and digest-part1).
    *     digest-data: 32bytes
    *     c1s1-part2: (1536-n-32)bytes (digest-part2 and key)
    * @return a new allocated bytes, user must free it.
    */
    char* srs_bytes_join_schema1(int32_t time, int32_t version, digest_block* digest, key_block* key);
    
    /**
    * c1s1 schema0
    *     time: 4bytes
    *     version: 4bytes
    *     key: 764bytes
    *     digest: 764bytes
    * c1s1 schema1
    *     time: 4bytes
    *     version: 4bytes
    *     digest: 764bytes
    *     key: 764bytes
    * @see also: http://blog.csdn.net/win_lin/article/details/13006803
    */
    class c1s1
    {
    public:
        union block {
            key_block key; 
            digest_block digest; 
        };
        
        // 4bytes
        int32_t time;
        // 4bytes
        int32_t version;
        // 764bytes
        // if schema0, use key
        // if schema1, use digest
        block block0;
        // 764bytes
        // if schema0, use digest
        // if schema1, use key
        block block1;
        
        // the logic schema
        srs_schema_type schema;
        
        c1s1();
        virtual ~c1s1();
        /**
        * get the digest key.
        */
        virtual char* get_digest();
        /**
        * copy to bytes.
        */
        virtual void dump(char* _c1s1);
        /**
        * server: parse the c1s1, discovery the key and digest by schema.
        * use the c1_validate_digest() to valid the digest of c1.
        */
        virtual int parse(char* _c1s1, srs_schema_type _schema);
        
        /**
        * client: create and sign c1 by schema.
        * sign the c1, generate the digest.
        *         calc_c1_digest(c1, schema) {
        *            get c1s1-joined from c1 by specified schema
        *            digest-data = HMACsha256(c1s1-joined, FPKey, 30)
        *            return digest-data;
        *        }
        *        random fill 1536bytes c1 // also fill the c1-128bytes-key
        *        time = time() // c1[0-3]
        *        version = [0x80, 0x00, 0x07, 0x02] // c1[4-7]
        *        schema = choose schema0 or schema1
        *        digest-data = calc_c1_digest(c1, schema)
        *        copy digest-data to c1
        */
        virtual int c1_create(srs_schema_type _schema);
        /**
        * server: validate the parsed c1 schema
        */
        virtual int c1_validate_digest(bool& is_valid);
        /**
        * server: create and sign the s1 from c1.
        */
        virtual int s1_create(c1s1* c1);
        /**
        * server: validate the parsed s1 schema
        */
        virtual int s1_validate_digest(bool& is_valid);
    private:
        virtual int calc_s1_digest(char*& digest);
        virtual int calc_c1_digest(char*& digest);
        virtual void destroy_blocks();
    };
    
    /**
    * the c2s2 complex handshake structure.
    * random-data: 1504bytes
    * digest-data: 32bytes
    * @see also: http://blog.csdn.net/win_lin/article/details/13006803
    */
    class c2s2
    {
    public:
        char random[1504];
        char digest[32];
        
        c2s2();
        virtual ~c2s2();
        
        /**
        * copy to bytes.
        */
        virtual void dump(char* _c2s2);
        /**
        * parse the c2s2
        */
        virtual void parse(char* _c2s2);
    
        /**
        * create c2.
        * random fill c2s2 1536 bytes
        * 
        * // client generate C2, or server valid C2
        * temp-key = HMACsha256(s1-digest, FPKey, 62)
        * c2-digest-data = HMACsha256(c2-random-data, temp-key, 32)
        */
        virtual int c2_create(c1s1* s1);
        
        /**
        * validate the c2 from client.
        */
        virtual int c2_validate(c1s1* s1, bool& is_valid);
        
        /**
        * create s2.
        * random fill c2s2 1536 bytes
        * 
        * // server generate S2, or client valid S2
        * temp-key = HMACsha256(c1-digest, FMSKey, 68)
        * s2-digest-data = HMACsha256(s2-random-data, temp-key, 32)
        */
        virtual int s2_create(c1s1* c1);
        
        /**
        * validate the s2 from server.
        */
        virtual int s2_validate(c1s1* c1, bool& is_valid);
    };
}

#endif

/**
* simple handshake.
* user can try complex handshake first, 
* rollback to simple handshake if error ERROR_RTMP_TRY_SIMPLE_HS
*/
class SrsSimpleHandshake
{
public:
    SrsSimpleHandshake();
    virtual ~SrsSimpleHandshake();
public:
    /**
    * simple handshake.
    */
    virtual int handshake_with_client(SrsHandshakeBytes* hs_bytes, ISrsProtocolReaderWriter* io);
    virtual int handshake_with_server(SrsHandshakeBytes* hs_bytes, ISrsProtocolReaderWriter* io);
};

/**
* rtmp complex handshake,
* @see also crtmp(crtmpserver) or librtmp,
* @see also: http://blog.csdn.net/win_lin/article/details/13006803
*/
class SrsComplexHandshake
{
public:
    SrsComplexHandshake();
    virtual ~SrsComplexHandshake();
public:
    /**
    * complex hanshake.
    * @return user must:
    *     continue connect app if success,
    *     try simple handshake if error is ERROR_RTMP_TRY_SIMPLE_HS,
    *     otherwise, disconnect
    */
    virtual int handshake_with_client(SrsHandshakeBytes* hs_bytes, ISrsProtocolReaderWriter* io);
    virtual int handshake_with_server(SrsHandshakeBytes* hs_bytes, ISrsProtocolReaderWriter* io);
};

#endif
