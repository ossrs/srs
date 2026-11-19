//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_HANDSHAKE_HPP
#define SRS_PROTOCOL_HANDSHAKE_HPP

#include <srs_core.hpp>
#include <srs_kernel_utility.hpp>

class ISrsProtocolReadWriter;
class SrsComplexHandshake;
class SrsHandshakeBytes;
class SrsBuffer;

#include <openssl/hmac.h>

namespace srs_internal
{
#define SRS_OpensslHashSize 512
extern uint8_t SrsGenuineFMSKey[];
extern uint8_t SrsGenuineFPKey[];
srs_error_t openssl_HMACsha256(const void *key, int key_size, const void *data, int data_size, void *digest);
srs_error_t openssl_generate_key(char *public_key, int32_t size);

class SrsDH
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    EVP_PKEY *pkey_;
#else
    DH *pdh;
#endif

public:
    SrsDH();
    virtual ~SrsDH();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual void close();

public:
    // OpenSSL sometimes generates a 127-byte public key.
    virtual srs_error_t initialize(bool ensure_128bytes_public_key = false);
    // The pkey_size is the buffer size in and the key size out. Never ignore the size out.
    virtual srs_error_t copy_public_key(char *pkey, int32_t &pkey_size);
    // The skey_size is the buffer size in and the key size out. Never ignore the size out.
    virtual srs_error_t copy_shared_key(const char *ppkey, int32_t ppkey_size, char *skey, int32_t &skey_size);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_initialize();
};
enum srs_schema_type {
    srs_schema_invalid = 2,

    // The key block, then the digest block.
    srs_schema0 = 0,

    // The digest block, then the key block.
    srs_schema1 = 1,
};

// The 764bytes key structure
//     random-data: (offset)bytes
//     key-data: 128bytes
//     random-data: (764-offset-128-4)bytes
//     offset: 4bytes
// @see also: http://blog.csdn.net/win_lin/article/details/13006803
class SrsKeyBlock
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRand *rand_;

public:
    char *random0_;
    int random0_size_;

    char key_[128];

    char *random1_;
    int random1_size_;

    int32_t offset_;

public:
    SrsKeyBlock();
    virtual ~SrsKeyBlock();

public:
    void assemble(); // Not in the constructor, to avoid calling functions there.

public:
    srs_error_t parse(SrsBuffer *stream);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The offset_ field is not the offset of the key; the offset is computed from its bytes.
    int
    calc_valid_offset();
};

// The 764bytes digest structure
//     offset: 4bytes
//     random-data: (offset)bytes
//     digest-data: 32bytes
//     random-data: (764-4-offset-32)bytes
// @see also: http://blog.csdn.net/win_lin/article/details/13006803
class SrsDigestBlock
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRand *rand_;

public:
    int32_t offset_;

    char *random0_;
    int random0_size_;

    char digest_[32];

    char *random1_;
    int random1_size_;

public:
    SrsDigestBlock();
    virtual ~SrsDigestBlock();

public:
    void assemble(); // Not in the constructor, to avoid calling functions there.

public:
    srs_error_t parse(SrsBuffer *stream);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The offset_ field is not the offset of the digest; the offset is computed from its bytes.
    int
    calc_valid_offset();
};

class SrsC1S1;

class SrsC1S1Strategy
{
// clang-format off
SRS_DECLARE_PROTECTED: // clang-format on
    SrsKeyBlock key_;
    SrsDigestBlock digest_;

public:
    SrsC1S1Strategy();
    virtual ~SrsC1S1Strategy();

public:
    void assemble(); // Not in the constructor, to avoid calling functions there.

public:
    virtual srs_schema_type schema() = 0;
    virtual char *get_digest();
    virtual char *get_key();
    virtual srs_error_t dump(SrsC1S1 *owner, char *c1s1, int size);
    virtual srs_error_t parse(char *c1s1, int size) = 0;

public:
    virtual srs_error_t c1_create(SrsC1S1 *owner);
    virtual srs_error_t c1_validate_digest(SrsC1S1 *owner, bool &is_valid);
    virtual srs_error_t s1_create(SrsC1S1 *owner, SrsC1S1 *c1);
    virtual srs_error_t s1_validate_digest(SrsC1S1 *owner, bool &is_valid);

public:
    virtual srs_error_t calc_c1_digest(SrsC1S1 *owner, char *&c1_digest);
    virtual srs_error_t calc_s1_digest(SrsC1S1 *owner, char *&s1_digest);
    virtual srs_error_t copy_to(SrsC1S1 *owner, char *bytes, int size, bool with_digest) = 0;
    virtual void copy_time_version(SrsBuffer *stream, SrsC1S1 *owner);
    virtual void copy_key(SrsBuffer *stream);
    virtual void copy_digest(SrsBuffer *stream, bool with_digest);
};

class SrsC1S1StrategySchema0 : public SrsC1S1Strategy
{
public:
    SrsC1S1StrategySchema0();
    virtual ~SrsC1S1StrategySchema0();

public:
    virtual srs_schema_type schema();
    virtual srs_error_t parse(char *c1s1, int size);

public:
    virtual srs_error_t copy_to(SrsC1S1 *owner, char *bytes, int size, bool with_digest);
};

class SrsC1S1StrategySchema1 : public SrsC1S1Strategy
{
public:
    SrsC1S1StrategySchema1();
    virtual ~SrsC1S1StrategySchema1();

public:
    virtual srs_schema_type schema();
    virtual srs_error_t parse(char *c1s1, int size);

public:
    virtual srs_error_t copy_to(SrsC1S1 *owner, char *bytes, int size, bool with_digest);
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
class SrsC1S1
{
public:
    int32_t time_;
    int32_t version_;
    SrsC1S1Strategy *payload_;

public:
    SrsC1S1();
    virtual ~SrsC1S1();

public:
    virtual srs_schema_type schema();
    virtual char *get_digest();
    virtual char *get_key();

public:
    virtual srs_error_t dump(char *c1s1, int size);
    // Does not check the digest; use c1_validate_digest() or s1_validate_digest() for that.
    virtual srs_error_t parse(char *c1s1, int size, srs_schema_type schema);

public:
    virtual srs_error_t c1_create(srs_schema_type schema);
    virtual srs_error_t c1_validate_digest(bool &is_valid);

public:
    virtual srs_error_t s1_create(SrsC1S1 *c1);
    virtual srs_error_t s1_validate_digest(bool &is_valid);
};

// @see also: http://blog.csdn.net/win_lin/article/details/13006803
class SrsC2S2
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRand *rand_;

public:
    char random_[1504];
    char digest_[32];

public:
    SrsC2S2();
    virtual ~SrsC2S2();

public:
    void assemble(); // Not in the constructor, to avoid calling functions there.

public:
    virtual srs_error_t dump(char *c2s2, int size);
    virtual srs_error_t parse(char *c2s2, int size);

public:
    virtual srs_error_t c2_create(SrsC1S1 *s1);

    virtual srs_error_t c2_validate(SrsC1S1 *s1, bool &is_valid);

public:
    virtual srs_error_t s2_create(SrsC1S1 *c1);

    virtual srs_error_t s2_validate(SrsC1S1 *c1, bool &is_valid);
};
} // namespace srs_internal

class SrsSimpleHandshake
{
public:
    SrsSimpleHandshake();
    virtual ~SrsSimpleHandshake();

public:
    virtual srs_error_t handshake_with_client(SrsHandshakeBytes *hs_bytes, ISrsProtocolReadWriter *io);
    virtual srs_error_t handshake_with_server(SrsHandshakeBytes *hs_bytes, ISrsProtocolReadWriter *io);
};

// @see also crtmp(crtmpserver) or librtmp,
// @see also: http://blog.csdn.net/win_lin/article/details/13006803
class SrsComplexHandshake
{
public:
    SrsComplexHandshake();
    virtual ~SrsComplexHandshake();

public:
    // On ERROR_RTMP_TRY_SIMPLE_HS, try the simple handshake; on other errors, disconnect.
    virtual srs_error_t handshake_with_client(SrsHandshakeBytes *hs_bytes, ISrsProtocolReadWriter *io);
    virtual srs_error_t handshake_with_server(SrsHandshakeBytes *hs_bytes, ISrsProtocolReadWriter *io);
};

#endif
