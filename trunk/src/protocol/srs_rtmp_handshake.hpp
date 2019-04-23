/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2019 Winlin
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

#ifndef SRS_PROTOCOL_HANDSHAKE_HPP
#define SRS_PROTOCOL_HANDSHAKE_HPP

#include <srs_core.hpp>

class ISrsProtocolReadWriter;
class SrsComplexHandshake;
class SrsHandshakeBytes;
class SrsBuffer;

// For openssl.
#include <openssl/hmac.h>

namespace _srs_internal
{
    // The digest key generate size.
    #define SRS_OpensslHashSize 512
    extern uint8_t SrsGenuineFMSKey[];
    extern uint8_t SrsGenuineFPKey[];
    srs_error_t openssl_HMACsha256(const void* key, int key_size, const void* data, int data_size, void* digest);
    srs_error_t openssl_generate_key(char* public_key, int32_t size);
    
    // The DH wrapper.
    class SrsDH
    {
    private:
        DH* pdh;
    public:
        SrsDH();
        virtual ~SrsDH();
    private:
        virtual void close();
    public:
        // Initialize dh, generate the public and private key.
        // @param ensure_128bytes_public_key whether ensure public key is 128bytes,
        //       sometimes openssl generate 127bytes public key.
        //       default to false to donot ensure.
        virtual srs_error_t initialize(bool ensure_128bytes_public_key = false);
        // Copy the public key.
        // @param pkey the bytes to copy the public key.
        // @param pkey_size the max public key size, output the actual public key size.
        //       user should never ignore this size.
        // @remark, when ensure_128bytes_public_key, the size always 128.
        virtual srs_error_t copy_public_key(char* pkey, int32_t& pkey_size);
        // Generate and copy the shared key.
        // Generate the shared key with peer public key.
        // @param ppkey peer public key.
        // @param ppkey_size the size of ppkey.
        // @param skey the computed shared key.
        // @param skey_size the max shared key size, output the actual shared key size.
        //       user should never ignore this size.
        virtual srs_error_t copy_shared_key(const char* ppkey, int32_t ppkey_size, char* skey, int32_t& skey_size);
    private:
        virtual srs_error_t do_initialize();
    };
    // The schema type.
    enum srs_schema_type
    {
        srs_schema_invalid = 2,
        
        // The key-digest sequence
        srs_schema0 = 0,
        
        // The digest-key sequence
        // @remark, FMS requires the schema1(digest-key), or connect failed.
        //
        srs_schema1 = 1,
    };
    
    // The 764bytes key structure
    //     random-data: (offset)bytes
    //     key-data: 128bytes
    //     random-data: (764-offset-128-4)bytes
    //     offset: 4bytes
    // @see also: http://blog.csdn.net/win_lin/article/details/13006803
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
    public:
        key_block();
        virtual ~key_block();
    public:
        // Parse key block from c1s1.
        // if created, user must free it by srs_key_block_free
        // @stream contains c1s1_key_bytes the key start bytes
        srs_error_t parse(SrsBuffer* stream);
    private:
        // Calculate the offset of key,
        // The key->offset cannot be used as the offset of key.
        int calc_valid_offset();
    };
    
    // The 764bytes digest structure
    //     offset: 4bytes
    //     random-data: (offset)bytes
    //     digest-data: 32bytes
    //     random-data: (764-4-offset-32)bytes
    // @see also: http://blog.csdn.net/win_lin/article/details/13006803
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
    public:
        digest_block();
        virtual ~digest_block();
    public:
        // Parse digest block from c1s1.
        // if created, user must free it by srs_digest_block_free
        // @stream contains c1s1_digest_bytes the digest start bytes
        srs_error_t parse(SrsBuffer* stream);
    private:
        // Calculate the offset of digest,
        // The key->offset cannot be used as the offset of digest.
        int calc_valid_offset();
    };
    
    class c1s1;
    
    // The c1s1 strategy, use schema0 or schema1.
    // The template method class to defines common behaviors,
    // while the concrete class to implements in schema0 or schema1.
    class c1s1_strategy
    {
    protected:
        key_block key;
        digest_block digest;
    public:
        c1s1_strategy();
        virtual ~c1s1_strategy();
    public:
        // Get the scema.
        virtual srs_schema_type schema() = 0;
        // Get the digest.
        virtual char* get_digest();
        // Get the key.
        virtual char* get_key();
        // Copy to bytes.
        // @param size must be 1536.
        virtual srs_error_t dump(c1s1* owner, char* _c1s1, int size);
        // For server: parse the c1s1, discovery the key and digest by schema.
        // use the c1_validate_digest() to valid the digest of c1.
        virtual srs_error_t parse(char* _c1s1, int size) = 0;
    public:
        // For client: create and sign c1 by schema.
        // sign the c1, generate the digest.
        //         calc_c1_digest(c1, schema) {
        //            get c1s1-joined from c1 by specified schema
        //            digest-data = HMACsha256(c1s1-joined, FPKey, 30)
        //            return digest-data;
        //        }
        //        random fill 1536bytes c1 // also fill the c1-128bytes-key
        //        time = time() // c1[0-3]
        //        version = [0x80, 0x00, 0x07, 0x02] // c1[4-7]
        //        schema = choose schema0 or schema1
        //        digest-data = calc_c1_digest(c1, schema)
        //        copy digest-data to c1
        virtual srs_error_t c1_create(c1s1* owner);
        // For server:  validate the parsed c1 schema
        virtual srs_error_t c1_validate_digest(c1s1* owner, bool& is_valid);
        // For server:  create and sign the s1 from c1.
        //       // decode c1 try schema0 then schema1
        //       c1-digest-data = get-c1-digest-data(schema0)
        //       if c1-digest-data equals to calc_c1_digest(c1, schema0) {
        //           c1-key-data = get-c1-key-data(schema0)
        //           schema = schema0
        //       } else {
        //           c1-digest-data = get-c1-digest-data(schema1)
        //           if c1-digest-data not equals to calc_c1_digest(c1, schema1) {
        //               switch to simple handshake.
        //               return
        //           }
        //           c1-key-data = get-c1-key-data(schema1)
        //           schema = schema1
        //       }
        //
        //       // Generate s1
        //       random fill 1536bytes s1
        //       time = time() // c1[0-3]
        //       version = [0x04, 0x05, 0x00, 0x01] // s1[4-7]
        //       s1-key-data=shared_key=DH_compute_key(peer_pub_key=c1-key-data)
        //       get c1s1-joined by specified schema
        //       s1-digest-data = HMACsha256(c1s1-joined, FMSKey, 36)
        //       copy s1-digest-data and s1-key-data to s1.
        // @param c1, to get the peer_pub_key of client.
        virtual srs_error_t s1_create(c1s1* owner, c1s1* c1);
        // For server:  validate the parsed s1 schema
        virtual srs_error_t s1_validate_digest(c1s1* owner, bool& is_valid);
    public:
        // Calculate the digest for c1
        virtual srs_error_t calc_c1_digest(c1s1* owner, char*& c1_digest);
        // Calculate the digest for s1
        virtual srs_error_t calc_s1_digest(c1s1* owner, char*& s1_digest);
        // Copy whole c1s1 to bytes.
        // @param size must always be 1536 with digest, and 1504 without digest.
        virtual srs_error_t copy_to(c1s1* owner, char* bytes, int size, bool with_digest) = 0;
        // Copy time and version to stream.
        virtual void copy_time_version(SrsBuffer* stream, c1s1* owner);
        // Copy key to stream.
        virtual void copy_key(SrsBuffer* stream);
        // Copy digest to stream.
        virtual void copy_digest(SrsBuffer* stream, bool with_digest);
    };
    
    // The c1s1 schema0
    //     key: 764bytes
    //     digest: 764bytes
    class c1s1_strategy_schema0 : public c1s1_strategy
    {
    public:
        c1s1_strategy_schema0();
        virtual ~c1s1_strategy_schema0();
    public:
        virtual srs_schema_type schema();
        virtual srs_error_t parse(char* _c1s1, int size);
    public:
        virtual srs_error_t copy_to(c1s1* owner, char* bytes, int size, bool with_digest);
    };
    
    // The c1s1 schema1
    //     digest: 764bytes
    //     key: 764bytes
    class c1s1_strategy_schema1 : public c1s1_strategy
    {
    public:
        c1s1_strategy_schema1();
        virtual ~c1s1_strategy_schema1();
    public:
        virtual srs_schema_type schema();
        virtual srs_error_t parse(char* _c1s1, int size);
    public:
        virtual srs_error_t copy_to(c1s1* owner, char* bytes, int size, bool with_digest);
    };
    
    // The c1s1 schema0
    //     time: 4bytes
    //     version: 4bytes
    //     key: 764bytes
    //     digest: 764bytes
    // The c1s1 schema1
    //     time: 4bytes
    //     version: 4bytes
    //     digest: 764bytes
    //     key: 764bytes
    // @see also: http://blog.csdn.net/win_lin/article/details/13006803
    class c1s1
    {
    public:
        // 4bytes
        int32_t time;
        // 4bytes
        int32_t version;
        // 764bytes+764bytes
        c1s1_strategy* payload;
    public:
        c1s1();
        virtual ~c1s1();
    public:
        // Get the scema.
        virtual srs_schema_type schema();
        // Get the digest key.
        virtual char* get_digest();
        // Get the key.
        virtual char* get_key();
    public:
        // Copy to bytes.
        // @param size, must always be 1536.
        virtual srs_error_t dump(char* _c1s1, int size);
        // For server:  parse the c1s1, discovery the key and digest by schema.
        // @param size, must always be 1536.
        // use the c1_validate_digest() to valid the digest of c1.
        // use the s1_validate_digest() to valid the digest of s1.
        virtual srs_error_t parse(char* _c1s1, int size, srs_schema_type _schema);
    public:
        // For client:  create and sign c1 by schema.
        // sign the c1, generate the digest.
        //         calc_c1_digest(c1, schema) {
        //            get c1s1-joined from c1 by specified schema
        //            digest-data = HMACsha256(c1s1-joined, FPKey, 30)
        //            return digest-data;
        //        }
        //        random fill 1536bytes c1 // also fill the c1-128bytes-key
        //        time = time() // c1[0-3]
        //        version = [0x80, 0x00, 0x07, 0x02] // c1[4-7]
        //        schema = choose schema0 or schema1
        //        digest-data = calc_c1_digest(c1, schema)
        //        copy digest-data to c1
        virtual srs_error_t c1_create(srs_schema_type _schema);
        // For server:  validate the parsed c1 schema
        virtual srs_error_t c1_validate_digest(bool& is_valid);
    public:
        // For server:  create and sign the s1 from c1.
        //       // decode c1 try schema0 then schema1
        //       c1-digest-data = get-c1-digest-data(schema0)
        //       if c1-digest-data equals to calc_c1_digest(c1, schema0) {
        //           c1-key-data = get-c1-key-data(schema0)
        //           schema = schema0
        //       } else {
        //           c1-digest-data = get-c1-digest-data(schema1)
        //           if c1-digest-data not equals to calc_c1_digest(c1, schema1) {
        //               switch to simple handshake.
        //               return
        //           }
        //           c1-key-data = get-c1-key-data(schema1)
        //           schema = schema1
        //       }
        //
        //       // Generate s1
        //       random fill 1536bytes s1
        //       time = time() // c1[0-3]
        //       version = [0x04, 0x05, 0x00, 0x01] // s1[4-7]
        //       s1-key-data=shared_key=DH_compute_key(peer_pub_key=c1-key-data)
        //       get c1s1-joined by specified schema
        //       s1-digest-data = HMACsha256(c1s1-joined, FMSKey, 36)
        //       copy s1-digest-data and s1-key-data to s1.
        virtual srs_error_t s1_create(c1s1* c1);
        // For server:  validate the parsed s1 schema
        virtual srs_error_t s1_validate_digest(bool& is_valid);
    };
    
    // The c2s2 complex handshake structure.
    // random-data: 1504bytes
    // digest-data: 32bytes
    // @see also: http://blog.csdn.net/win_lin/article/details/13006803
    class c2s2
    {
    public:
        char random[1504];
        char digest[32];
    public:
        c2s2();
        virtual ~c2s2();
    public:
        // Copy to bytes.
        // @param size, must always be 1536.
        virtual srs_error_t dump(char* _c2s2, int size);
        // Parse the c2s2
        // @param size, must always be 1536.
        virtual srs_error_t parse(char* _c2s2, int size);
    public:
        // Create c2.
        // random fill c2s2 1536 bytes
        //
        // // client generate C2, or server valid C2
        // temp-key = HMACsha256(s1-digest, FPKey, 62)
        // c2-digest-data = HMACsha256(c2-random-data, temp-key, 32)
        virtual srs_error_t c2_create(c1s1* s1);
        
        // Validate the c2 from client.
        virtual srs_error_t c2_validate(c1s1* s1, bool& is_valid);
    public:
        // Create s2.
        // random fill c2s2 1536 bytes
        //
        // For server generate S2, or client valid S2
        // temp-key = HMACsha256(c1-digest, FMSKey, 68)
        // s2-digest-data = HMACsha256(s2-random-data, temp-key, 32)
        virtual srs_error_t s2_create(c1s1* c1);
        
        // Validate the s2 from server.
        virtual srs_error_t s2_validate(c1s1* c1, bool& is_valid);
    };
}

// Simple handshake.
// user can try complex handshake first,
// rollback to simple handshake if error ERROR_RTMP_TRY_SIMPLE_HS
class SrsSimpleHandshake
{
public:
    SrsSimpleHandshake();
    virtual ~SrsSimpleHandshake();
public:
    // Simple handshake.
    virtual srs_error_t handshake_with_client(SrsHandshakeBytes* hs_bytes, ISrsProtocolReadWriter* io);
    virtual srs_error_t handshake_with_server(SrsHandshakeBytes* hs_bytes, ISrsProtocolReadWriter* io);
};

// Complex handshake,
// @see also crtmp(crtmpserver) or librtmp,
// @see also: http://blog.csdn.net/win_lin/article/details/13006803
class SrsComplexHandshake
{
public:
    SrsComplexHandshake();
    virtual ~SrsComplexHandshake();
public:
    // Complex hanshake.
    // @return user must:
    //     continue connect app if success,
    //     try simple handshake if error is ERROR_RTMP_TRY_SIMPLE_HS,
    //     otherwise, disconnect
    virtual srs_error_t handshake_with_client(SrsHandshakeBytes* hs_bytes, ISrsProtocolReadWriter* io);
    virtual srs_error_t handshake_with_server(SrsHandshakeBytes* hs_bytes, ISrsProtocolReadWriter* io);
};

#endif
