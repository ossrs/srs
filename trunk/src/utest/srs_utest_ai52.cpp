//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai52.hpp>

#include <srs_app_config.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_core.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_mp4.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_rtmp_handshake.hpp>
#include <srs_utest_ai11.hpp>
#include <srs_utest_ai37.hpp>
#include <srs_utest_manual_config.hpp>
#include <srs_utest_manual_kernel.hpp>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/opensslv.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#if OPENSSL_VERSION_NUMBER >= 0x40100000L
#include <openssl/core_names.h>
#include <openssl/dtls1.h>
#include <openssl/kdf.h>
#endif

#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

using namespace std;

// A peer with no version limit of its own, so it offers the newest DTLS its OpenSSL has: DTLS 1.3
// on OpenSSL 4.1, as a browser does. It uses the same certificate as SRS, since only the version matters.
static SSL_CTX *utest_dtls_newest_ctx(ISrsDtlsCertificate *certificate, bool client)
{
    SSL_CTX *ctx = SSL_CTX_new(client ? DTLS_client_method() : DTLS_server_method());
    SSL_CTX_use_certificate(ctx, certificate->get_cert());
    SSL_CTX_use_PrivateKey(ctx, certificate->get_public_key());
    return ctx;
}

// SRS compiles against the headers of one OpenSSL and links the libraries of the same one. A build that
// mixes them, such as a cached 1.1.1l library with 4.1 headers, fails here before any TLS code runs.
VOID TEST(OpenSslBuildTest, HeadersMatchTheLinkedLibrary)
{
    EXPECT_EQ((unsigned long)OPENSSL_VERSION_NUMBER, OpenSSL_version_num());
}

// With --ssl-4=on, SRS builds the bundled OpenSSL 4.1, the first OpenSSL with DTLS 1.3. Other builds,
// the bundled 1.1.1l or a system OpenSSL, have no fixed version to check.
VOID TEST(OpenSslBuildTest, BundledOpenSsl4HasDtls13)
{
#ifdef SRS_SSL_4
    EXPECT_EQ(4, OPENSSL_VERSION_MAJOR);
    EXPECT_EQ(1, OPENSSL_VERSION_MINOR);
    EXPECT_EQ(0xfefc, DTLS1_3_VERSION);

    SSL_CTX *ctx = SSL_CTX_new(DTLS_method());
    ASSERT_TRUE(ctx != NULL);
    EXPECT_EQ(1, SSL_CTX_set_max_proto_version(ctx, DTLS1_3_VERSION));
    EXPECT_EQ(DTLS1_3_VERSION, SSL_CTX_get_max_proto_version(ctx));
    SSL_CTX_free(ctx);
#else
    SUCCEED();
#endif
}

// A peer that stops at DTLS 1.2, as a browser without DTLS 1.3 does.
static SSL_CTX *utest_dtls12_ctx(ISrsDtlsCertificate *certificate, bool client)
{
    SSL_CTX *ctx = utest_dtls_newest_ctx(certificate, client);
    SSL_CTX_set_max_proto_version(ctx, DTLS1_2_VERSION);
    return ctx;
}

// The newest DTLS version dtls_version auto agrees on with a peer that has every version: DTLS 1.3 from
// OpenSSL 4.1, DTLS 1.2 before it.
static int utest_dtls_auto_newest()
{
#if OPENSSL_VERSION_NUMBER >= 0x40100000L
    return DTLS1_3_VERSION;
#else
    return DTLS1_2_VERSION;
#endif
}

// dtls_version auto agrees on the newest version both sides have, as a server and as a client: DTLS 1.3
// with a peer that offers it on OpenSSL 4.1 or newer, else DTLS 1.2, and DTLS 1.2 with a peer that stops
// there, so a browser without DTLS 1.3 still connects.
VOID TEST(OpenSslBuildTest, DtlsAutoAgreesOnTheNewestVersionOfBoth)
{
    srs_error_t err = srs_success;

    MockDtlsCertificateForDtlsCtx certificate(true);
    HELPER_ASSERT_SUCCESS(certificate.initialize());

    for (int peer12 = 0; peer12 < 2; peer12++) {
        int want = peer12 ? DTLS1_2_VERSION : utest_dtls_auto_newest();

        SSL_CTX *peer_client = peer12 ? utest_dtls12_ctx(&certificate, true) : utest_dtls_newest_ctx(&certificate, true);
        SSL_CTX *srs_server = srs_build_dtls_ctx(&certificate, SrsDtlsVersionAuto, "passive");
        ASSERT_TRUE(peer_client != NULL);
        ASSERT_TRUE(srs_server != NULL);
        EXPECT_EQ(want, utest_dtls_handshake(peer_client, srs_server)) << "peer12=" << peer12;
        SSL_CTX_free(peer_client);
        SSL_CTX_free(srs_server);

        SSL_CTX *srs_client = srs_build_dtls_ctx(&certificate, SrsDtlsVersionAuto, "active");
        SSL_CTX *peer_server = peer12 ? utest_dtls12_ctx(&certificate, false) : utest_dtls_newest_ctx(&certificate, false);
        ASSERT_TRUE(srs_client != NULL);
        ASSERT_TRUE(peer_server != NULL);
        EXPECT_EQ(want, utest_dtls_handshake(srs_client, peer_server)) << "peer12=" << peer12;
        SSL_CTX_free(srs_client);
        SSL_CTX_free(peer_server);
    }
}

// DTLS 1.3 is there exactly from OpenSSL 4.1, so the bundled OpenSSL 4.1 of --ssl-4=on has it, and the
// bundled 1.1.1l does not.
VOID TEST(Dtls13Test, SupportedFromOpenSsl41)
{
    EXPECT_EQ(OPENSSL_VERSION_NUMBER >= 0x40100000L, srs_dtls_version13_supported());
#ifdef SRS_SSL_4
    EXPECT_TRUE(srs_dtls_version13_supported());
#endif
}

// dtls_version dtls1.3 pins DTLS 1.3, as a server and as a client: a peer with every version agrees on
// it, and a peer that stops at DTLS 1.2 fails the handshake. Builds without DTLS 1.3 refuse the value in
// the config check; should one reach the context, it takes auto's versions.
VOID TEST(Dtls13Test, PinnedDtls13RefusesAnOlderPeer)
{
    srs_error_t err = srs_success;

    MockDtlsCertificateForDtlsCtx certificate(true);
    HELPER_ASSERT_SUCCESS(certificate.initialize());

    for (int peer12 = 0; peer12 < 2; peer12++) {
#if OPENSSL_VERSION_NUMBER >= 0x40100000L
        int want = peer12 ? 0 : DTLS1_3_VERSION;
#else
        int want = DTLS1_2_VERSION;
#endif

        SSL_CTX *peer_client = peer12 ? utest_dtls12_ctx(&certificate, true) : utest_dtls_newest_ctx(&certificate, true);
        SSL_CTX *srs_server = srs_build_dtls_ctx(&certificate, SrsDtlsVersion1_3, "passive");
        ASSERT_TRUE(peer_client != NULL);
        ASSERT_TRUE(srs_server != NULL);
        EXPECT_EQ(want, utest_dtls_handshake(peer_client, srs_server)) << "peer12=" << peer12;
        SSL_CTX_free(peer_client);
        SSL_CTX_free(srs_server);

        SSL_CTX *srs_client = srs_build_dtls_ctx(&certificate, SrsDtlsVersion1_3, "active");
        SSL_CTX *peer_server = peer12 ? utest_dtls12_ctx(&certificate, false) : utest_dtls_newest_ctx(&certificate, false);
        ASSERT_TRUE(srs_client != NULL);
        ASSERT_TRUE(peer_server != NULL);
        EXPECT_EQ(want, utest_dtls_handshake(srs_client, peer_server)) << "peer12=" << peer12;
        SSL_CTX_free(srs_client);
        SSL_CTX_free(peer_server);
    }
}

// The session's DTLS takes dtls1.3 from the config and builds its context with only DTLS 1.3; auto
// reaches up to DTLS 1.3 where the build has it.
VOID TEST(Dtls13Test, InitializeSelectsDtls13)
{
    srs_error_t err = srs_success;

    MockDtlsCertificateForDtlsCtx certificate(true);
    HELPER_ASSERT_SUCCESS(certificate.initialize());

    if (true) {
        SrsDtlsServerImpl impl(NULL);
        impl.dtls_certificate_ = &certificate;
        HELPER_EXPECT_SUCCESS(impl.initialize("dtls1.3", "passive"));
        EXPECT_EQ(SrsDtlsVersion1_3, impl.version_);
        ASSERT_TRUE(impl.dtls_ctx_ != NULL);
#if OPENSSL_VERSION_NUMBER >= 0x40100000L
        EXPECT_EQ(DTLS1_3_VERSION, SSL_CTX_get_min_proto_version(impl.dtls_ctx_));
        EXPECT_EQ(DTLS1_3_VERSION, SSL_CTX_get_max_proto_version(impl.dtls_ctx_));
#endif
        impl.dtls_certificate_ = NULL;
    }

    if (true) {
        SrsDtlsServerImpl impl(NULL);
        impl.dtls_certificate_ = &certificate;
        HELPER_EXPECT_SUCCESS(impl.initialize("auto", "passive"));
        EXPECT_EQ(SrsDtlsVersionAuto, impl.version_);
        ASSERT_TRUE(impl.dtls_ctx_ != NULL);
#if OPENSSL_VERSION_NUMBER >= 0x40100000L
        EXPECT_EQ(DTLS1_3_VERSION, SSL_CTX_get_max_proto_version(impl.dtls_ctx_));
#endif
        impl.dtls_certificate_ = NULL;
    }
}

// The config takes dtls_version dtls1.3 only in a build with DTLS 1.3, from a vhost or from the env;
// other builds refuse it at the config check, so SRS never starts with a version it cannot use.
VOID TEST(Dtls13Test, ConfigTakesDtls13OnlyWithOpenSsl41)
{
    string c = string(_MIN_OK_CONF) + "vhost test.com{rtc{enabled on; dtls_version dtls1.3;}}";

    if (true) {
        MockSrsConfig conf;
        srs_error_t err = conf.mock_parse(c);
#if OPENSSL_VERSION_NUMBER >= 0x40100000L
        HELPER_EXPECT_SUCCESS(err);
        EXPECT_STREQ("dtls1.3", conf.get_rtc_dtls_version("test.com").c_str());
#else
        EXPECT_TRUE(err != srs_success) << c;
        EXPECT_NE(string::npos, srs_error_desc(err).find("dtls_version=dtls1.3")) << srs_error_desc(err);
        srs_freep(err);
#endif
    }

    if (true) {
        MockSrsConfig conf;
        SrsSetEnvConfig(conf, dtls_version, "SRS_VHOST_RTC_DTLS_VERSION", "dtls1.3");
        srs_error_t err = conf.mock_parse(_MIN_OK_CONF);
#if OPENSSL_VERSION_NUMBER >= 0x40100000L
        HELPER_EXPECT_SUCCESS(err);
        EXPECT_STREQ("dtls1.3", conf.get_rtc_dtls_version("__defaultVhost__").c_str());
#else
        EXPECT_TRUE(err != srs_success);
        EXPECT_NE(string::npos, srs_error_desc(err).find("SRS_VHOST_RTC_DTLS_VERSION=dtls1.3")) << srs_error_desc(err);
        srs_freep(err);
#endif
    }
}

// The versions every build has stay accepted, from a vhost and from the env.
VOID TEST(Dtls13Test, ConfigTakesTheOtherVersionsInEveryBuild)
{
    srs_error_t err = srs_success;

    const char *versions[] = {"auto", "dtls1.0", "dtls1.2"};
    for (int i = 0; i < (int)(sizeof(versions) / sizeof(versions[0])); i++) {
        string version = versions[i];

        if (true) {
            MockSrsConfig conf;
            HELPER_EXPECT_SUCCESS(conf.mock_parse(string(_MIN_OK_CONF) + "vhost test.com{rtc{enabled on; dtls_version " + version + ";}}"));
            EXPECT_EQ(version, conf.get_rtc_dtls_version("test.com"));
        }

        if (true) {
            MockSrsConfig conf;
            SrsSetEnvConfig(conf, dtls_version, "SRS_VHOST_RTC_DTLS_VERSION", version);
            HELPER_EXPECT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
            EXPECT_EQ(version, conf.get_rtc_dtls_version("__defaultVhost__"));
        }
    }
}

// dtls_version dtls1.2 and dtls1.0 pin that version against a peer that offers newer ones. OpenSSL 4
// has no version-specific methods, so SRS pins the version on the flexible one.
VOID TEST(OpenSslBuildTest, DtlsPinnedVersionsHoldWithANewerPeer)
{
    srs_error_t err = srs_success;

    MockDtlsCertificateForDtlsCtx certificate(true);
    HELPER_ASSERT_SUCCESS(certificate.initialize());

    SSL_CTX *peer_client = utest_dtls_newest_ctx(&certificate, true);
    SSL_CTX *srs_server = srs_build_dtls_ctx(&certificate, SrsDtlsVersion1_2, "passive");
    ASSERT_TRUE(peer_client != NULL);
    ASSERT_TRUE(srs_server != NULL);
    EXPECT_EQ(DTLS1_2_VERSION, utest_dtls_handshake(peer_client, srs_server));
    SSL_CTX_free(peer_client);
    SSL_CTX_free(srs_server);

    // OpenSSL 3 and newer allow DTLS 1.0 only at security level 0, which SRS sets for dtls1.0; the
    // peer needs it too, and a cipher list without the suites level 1 would refuse.
    peer_client = utest_dtls_newest_ctx(&certificate, true);
    SSL_CTX_set_security_level(peer_client, 0);
    SSL_CTX_set_cipher_list(peer_client, "ALL:!aNULL:!eNULL:!LOW:!MD5");
    srs_server = srs_build_dtls_ctx(&certificate, SrsDtlsVersion1_0, "passive");
    ASSERT_TRUE(srs_server != NULL);
    EXPECT_EQ(DTLS1_VERSION, utest_dtls_handshake(peer_client, srs_server));
    SSL_CTX_free(peer_client);
    SSL_CTX_free(srs_server);

    SSL_CTX *srs_client = srs_build_dtls_ctx(&certificate, SrsDtlsVersion1_2, "active");
    SSL_CTX *peer_server = utest_dtls_newest_ctx(&certificate, false);
    ASSERT_TRUE(srs_client != NULL);
    ASSERT_TRUE(peer_server != NULL);
    EXPECT_EQ(DTLS1_2_VERSION, utest_dtls_handshake(srs_client, peer_server));
    SSL_CTX_free(srs_client);
    SSL_CTX_free(peer_server);
}

// The CBC-AES128 example of NIST SP 800-38A, F.2.1: four plaintext blocks and their ciphertext.
static const unsigned char utest_nist_key[16] = {
    0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
static const unsigned char utest_nist_iv[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};
static const unsigned char utest_nist_plain[64] = {
    0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
    0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c, 0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51,
    0x30, 0xc8, 0x1c, 0x46, 0xa3, 0x5c, 0xe4, 0x11, 0xe5, 0xfb, 0xc1, 0x19, 0x1a, 0x0a, 0x52, 0xef,
    0xf6, 0x9f, 0x24, 0x45, 0xdf, 0x4f, 0x9b, 0x17, 0xad, 0x2b, 0x41, 0x7b, 0xe6, 0x6c, 0x37, 0x10};
static const unsigned char utest_nist_cipher[64] = {
    0x76, 0x49, 0xab, 0xac, 0x81, 0x19, 0xb2, 0x46, 0xce, 0xe9, 0x8e, 0x9b, 0x12, 0xe9, 0x19, 0x7d,
    0x50, 0x86, 0xcb, 0x9b, 0x50, 0x72, 0x19, 0xee, 0x95, 0xdb, 0x11, 0x3a, 0x91, 0x76, 0x78, 0xb2,
    0x73, 0xbe, 0xd6, 0xb8, 0xe3, 0xc1, 0x74, 0x3b, 0x71, 0x16, 0xe6, 0x9e, 0x22, 0x22, 0x95, 0x16,
    0x3f, 0xf1, 0xca, 0xa1, 0x68, 0x1f, 0xac, 0x09, 0x12, 0x0e, 0xca, 0x30, 0x75, 0x86, 0xe1, 0xa7};

static string utest_hex(const void *data, int size)
{
    string s;
    const unsigned char *p = (const unsigned char *)data;
    for (int i = 0; i < size; i++) {
        char b[3];
        snprintf(b, sizeof(b), "%02x", p[i]);
        s += b;
    }
    return s;
}

// The RTMP complex handshake signs its packets by HMAC-SHA256, and digests them by plain SHA-256 with
// no key. Both match the published vectors: RFC 4231 test case 2, and the SHA-256 of "abc" (FIPS 180-2).
VOID TEST(OpenSslApiTest, RtmpHmacSha256MatchesRfc4231)
{
    srs_error_t err = srs_success;

    const char *key = "Jefe";
    const char *data = "what do ya want for nothing?";
    char digest[32];
    HELPER_EXPECT_SUCCESS(srs_internal::openssl_HMACsha256(key, 4, data, (int)strlen(data), digest));
    EXPECT_EQ("5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843", utest_hex(digest, 32));

    HELPER_EXPECT_SUCCESS(srs_internal::openssl_HMACsha256(NULL, 0, "abc", 3, digest));
    EXPECT_EQ("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", utest_hex(digest, 32));
}

// A Flash publisher and the server each make a key pair in the 1024-bit group of RFC 2409 and
// exchange the public keys in C1 and S1: both get the same shared key. A client that sends a public
// key of 1, which would give it a known shared key, is refused.
VOID TEST(OpenSslApiTest, RtmpDhPeersAgreeAndABadPeerKeyIsRefused)
{
    srs_error_t err = srs_success;

    srs_internal::SrsDH server;
    srs_internal::SrsDH client;
    HELPER_ASSERT_SUCCESS(server.initialize(true));
    HELPER_ASSERT_SUCCESS(client.initialize(true));

    char server_pub[128];
    int32_t server_pub_size = sizeof(server_pub);
    HELPER_ASSERT_SUCCESS(server.copy_public_key(server_pub, server_pub_size));
    EXPECT_EQ(128, server_pub_size);
    char client_pub[128];
    int32_t client_pub_size = sizeof(client_pub);
    HELPER_ASSERT_SUCCESS(client.copy_public_key(client_pub, client_pub_size));
    EXPECT_EQ(128, client_pub_size);
    EXPECT_FALSE(srs_bytes_equal(server_pub, client_pub, 128));

    char server_shared[128];
    int32_t server_shared_size = sizeof(server_shared);
    HELPER_ASSERT_SUCCESS(server.copy_shared_key(client_pub, client_pub_size, server_shared, server_shared_size));
    char client_shared[128];
    int32_t client_shared_size = sizeof(client_shared);
    HELPER_ASSERT_SUCCESS(client.copy_shared_key(server_pub, server_pub_size, client_shared, client_shared_size));
    ASSERT_EQ(server_shared_size, client_shared_size);
    EXPECT_GT(server_shared_size, 100);
    EXPECT_TRUE(srs_bytes_equal(server_shared, client_shared, server_shared_size));

    char one[1] = {0x01};
    char shared[128];
    int32_t shared_size = sizeof(shared);
    HELPER_EXPECT_FAILED(server.copy_shared_key(one, 1, shared, shared_size));
}

// HLS with hls_keys on encrypts each TS segment by AES-128-CBC from the key and IV of its key file, as
// the METHOD=AES-128 of RFC 8216 says, which a player decrypts with any AES library. The segment
// starts with the NIST plaintext, so its first 64 bytes are the NIST ciphertext; the 4 packets are 47
// whole blocks, so close adds a block of padding.
VOID TEST(OpenSslApiTest, HlsAes128SegmentMatchesNist)
{
    srs_error_t err = srs_success;

    unsigned char key[16];
    unsigned char iv[16];
    memcpy(key, utest_nist_key, 16);
    memcpy(iv, utest_nist_iv, 16);

    unsigned char packets[SRS_TS_PACKET_SIZE * 4];
    memset(packets, 0xff, sizeof(packets));
    memcpy(packets, utest_nist_plain, sizeof(utest_nist_plain));

    string path = srs_utest_tmp_dir() + "/srs_utest_ai52_hls_aes_" + srs_strconv_format_int(getpid()) + ".ts";
    if (true) {
        SrsEncFileWriter writer;
        HELPER_ASSERT_SUCCESS(writer.config_cipher(key, iv));
        HELPER_ASSERT_SUCCESS(writer.open(path));
        for (int i = 0; i < 4; i++) {
            HELPER_EXPECT_SUCCESS(writer.write(packets + i * SRS_TS_PACKET_SIZE, SRS_TS_PACKET_SIZE, NULL));
        }
        writer.close();
    }

    string cipher;
    if (true) {
        FILE *fp = fopen(path.c_str(), "rb");
        ASSERT_TRUE(fp != NULL);
        char b[4096];
        size_t n;
        while ((n = fread(b, 1, sizeof(b), fp)) > 0) {
            cipher.append(b, n);
        }
        fclose(fp);
        unlink(path.c_str());
    }

    ASSERT_EQ(sizeof(packets) + 16, cipher.size());
    EXPECT_EQ(utest_hex(utest_nist_cipher, 64), utest_hex(cipher.data(), 64));
}

// HLS with fMP4 segments and hls_keys on encrypts the samples by cbcs (ISO/IEC 23001-7): AES-128-CBC
// from the constant IV over every whole block of an AAC sample, and over 1 block of each 10 of an AVC
// slice after its first 32 bytes, chained from one encrypted block to the next. With the NIST key, IV
// and plaintext blocks in those places, the encrypted blocks are the NIST ciphertext.
VOID TEST(OpenSslApiTest, Fmp4CbcsSamplesMatchNist)
{
    srs_error_t err = srs_success;

    MockSrsFileWriter fw;
    SrsFmp4SegmentEncoder encoder;
    HELPER_ASSERT_SUCCESS(encoder.initialize(&fw, 0, 0, 1, 2));

    unsigned char key[16];
    unsigned char iv[16];
    memcpy(key, utest_nist_key, 16);
    memcpy(iv, utest_nist_iv, 16);
    HELPER_ASSERT_SUCCESS(encoder.config_cipher(key, iv));
    encoder.config_video_cipher(SrsVideoCodecIdAVC, 4);

    // An IDR slice of 208 bytes after its 4-byte length: 32 clear bytes, then 11 blocks, the first and
    // the last of them encrypted.
    uint8_t video[4 + 208];
    memset(video, 0x11, sizeof(video));
    uint8_t idr[] = {0x00, 0x00, 0x00, 0xd0, 0x65};
    memcpy(video, idr, sizeof(idr));
    memcpy(video + 36, utest_nist_plain, 16);
    memcpy(video + 196, utest_nist_plain + 16, 16);
    HELPER_ASSERT_SUCCESS(encoder.write_sample(SrsMp4HandlerTypeVIDE, SrsVideoAvcFrameTypeKeyFrame, 1000, 1000, video, sizeof(video)));

    // Four whole blocks, then 5 bytes left clear.
    uint8_t audio[64 + 5];
    memcpy(audio, utest_nist_plain, 64);
    memset(audio + 64, 0x22, 5);
    HELPER_ASSERT_SUCCESS(encoder.write_sample(SrsMp4HandlerTypeSOUN, 0x00, 1000, 1000, audio, sizeof(audio)));

    HELPER_ASSERT_SUCCESS(encoder.flush(2000));
    string content = fw.str();
    ASSERT_GT(content.size(), sizeof(video) + sizeof(audio));

    string got_video = content.substr(content.size() - sizeof(audio) - sizeof(video), sizeof(video));
    EXPECT_EQ(utest_hex(video, 36), utest_hex(got_video.data(), 36));
    EXPECT_EQ(utest_hex(utest_nist_cipher, 16), utest_hex(got_video.data() + 36, 16));
    EXPECT_EQ(utest_hex(video + 52, 144), utest_hex(got_video.data() + 52, 144));
    EXPECT_EQ(utest_hex(utest_nist_cipher + 16, 16), utest_hex(got_video.data() + 196, 16));

    string got_audio = content.substr(content.size() - sizeof(audio));
    EXPECT_EQ(utest_hex(utest_nist_cipher, 64), utest_hex(got_audio.data(), 64));
    EXPECT_EQ(utest_hex(audio + 64, 5), utest_hex(got_audio.data() + 64, 5));
}

// The certificate SRS makes for WebRTC is self-signed by its key, valid from now for 365 days, with a
// P-256 key by default or an RSA key with rtc_server.ecdsa off. OpenSSL 3.2 and newer refuse an RSA key
// below 2048 bits at their default security level, so the key is 2048 bits there.
VOID TEST(OpenSslApiTest, DtlsCertificateIsSelfSignedForAYear)
{
    srs_error_t err = srs_success;

    for (int ecdsa = 0; ecdsa < 2; ecdsa++) {
        MockAppConfigForDtlsCertificate config;
        config.ecdsa_ = (ecdsa == 1);

        SrsDtlsCertificate cert;
        cert.config_ = &config;
        HELPER_ASSERT_SUCCESS(cert.initialize());

        X509 *x509 = cert.get_cert();
        EVP_PKEY *pkey = cert.get_public_key();
        ASSERT_TRUE(x509 != NULL);
        ASSERT_TRUE(pkey != NULL);
        if (ecdsa) {
            EXPECT_EQ(EVP_PKEY_EC, EVP_PKEY_id(pkey));
            EXPECT_EQ(256, EVP_PKEY_bits(pkey));
        } else {
            EXPECT_EQ(EVP_PKEY_RSA, EVP_PKEY_id(pkey));
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
            EXPECT_EQ(2048, EVP_PKEY_bits(pkey));
#else
            EXPECT_EQ(1024, EVP_PKEY_bits(pkey));
#endif
        }
        EXPECT_EQ(1, X509_verify(x509, pkey)) << "ecdsa=" << ecdsa;

        // From the start of the validity to now, which a NULL time stands for.
        int days = 0;
        int secs = 0;
        EXPECT_EQ(1, ASN1_TIME_diff(&days, &secs, X509_get0_notBefore(x509), NULL));
        EXPECT_TRUE(days == 0 && secs >= 0 && secs < 60) << "ecdsa=" << ecdsa << ", days=" << days << ", secs=" << secs;

        EXPECT_EQ(1, ASN1_TIME_diff(&days, &secs, X509_get0_notBefore(x509), X509_get0_notAfter(x509)));
        EXPECT_EQ(365, days) << "ecdsa=" << ecdsa;
        EXPECT_EQ(0, secs) << "ecdsa=" << ecdsa;

        cert.config_ = NULL;
    }
}

// A browser's DTLS, with its own certificate and dtls_version auto, so it offers every version its OpenSSL
// has, as Chrome offers DTLS 1.3 and 1.2.
static srs_error_t utest_dtls13_peer_initialize(SrsDtls *peer, ISrsDtlsCertificate *certificate, string role)
{
    srs_freep(peer->impl_);
    if (role == "active") {
        peer->impl_ = new SrsDtlsClientImpl(peer->callback_);
    } else {
        peer->impl_ = new SrsDtlsServerImpl(peer->callback_);
    }
    peer->impl_->dtls_certificate_ = certificate;
    return peer->impl_->initialize("auto", role);
}

// Hand the records between the peer and the SRS transport, until neither side has anything more to send.
static srs_error_t utest_dtls13_exchange_with_transport(SrsDtls *peer, MockDtlsCallbackForHandshake *pcb, SrsSecurityTransport *srs, MockRtcNetwork *network)
{
    srs_error_t err = srs_success;

    for (int round = 0; round < 16; round++) {
        if (pcb->packets_.empty() && network->packets_.empty()) {
            return err;
        }

        vector<string> to_srs;
        to_srs.swap(pcb->packets_);
        for (int i = 0; i < (int)to_srs.size(); i++) {
            string &pkt = to_srs[i];
            if ((err = srs->on_dtls(&pkt[0], (int)pkt.size())) != srs_success) {
                return srs_error_wrap(err, "srs");
            }
        }

        vector<string> to_peer;
        to_peer.swap(network->packets_);
        for (int i = 0; i < (int)to_peer.size(); i++) {
            string &pkt = to_peer[i];
            if ((err = peer->on_dtls(&pkt[0], (int)pkt.size())) != srs_success) {
                return srs_error_wrap(err, "peer");
            }
        }
    }

    return srs_error_new(ERROR_RTC_DTLS, "DTLS exchange does not end");
}

// A browser that offers DTLS 1.3 and 1.2 sets up a session with SRS, which takes dtls1.3 or auto, as the DTLS
// server (a=setup:actpass offers) and as the client (a=setup:passive offers). On OpenSSL 4.1 both agree on
// DTLS 1.3; the use_srtp extension still picks SRTP_AES128_CM_HMAC_SHA1_80, the only profile SRS offers,
// and the EXTRACTOR-dtls_srtp exporter gives each side the keys the other sends with, so media flows both
// ways, see RFC 9147 section 4.1 and RFC 5764 section 4.2. Builds without DTLS 1.3 agree on DTLS 1.2.
VOID TEST(Dtls13HandshakeTest, SessionAgreesOnSrtpProfileAndKeys)
{
    srs_error_t err = srs_success;

    const char *versions[] = {"dtls1.3", "auto"};
    for (int i = 0; i < (int)(sizeof(versions) / sizeof(versions[0])); i++) {
        string version = versions[i];

        for (int srs_passive = 0; srs_passive < 2; srs_passive++) {
            string srs_role = srs_passive ? "passive" : "active";
            string peer_role = srs_passive ? "active" : "passive";

            MockDtlsCallbackForHandshake scb, pcb;
            SrsDtls srs(&scb);
            SrsDtls peer(&pcb);
            HELPER_ASSERT_SUCCESS(srs.initialize(srs_role, version));
            HELPER_ASSERT_SUCCESS(peer.initialize(peer_role, "auto"));

            SrsDtls *client = srs_passive ? &peer : &srs;
            MockDtlsCallbackForHandshake *ccb = srs_passive ? &pcb : &scb;
            SrsDtls *server = srs_passive ? &srs : &peer;
            MockDtlsCallbackForHandshake *svcb = srs_passive ? &scb : &pcb;
            HELPER_ASSERT_SUCCESS(server->start_active_handshake());
            HELPER_ASSERT_SUCCESS(client->start_active_handshake());
            HELPER_ASSERT_SUCCESS(dtls_exchange(client, ccb, server, svcb));

            EXPECT_EQ(1, scb.handshake_done_count_) << version << " " << srs_role;
            EXPECT_EQ(1, pcb.handshake_done_count_) << version << " " << srs_role;
            EXPECT_TRUE(scb.alerts_.empty()) << version << " " << srs_role;
            EXPECT_TRUE(pcb.alerts_.empty()) << version << " " << srs_role;
            EXPECT_EQ(utest_dtls_auto_newest(), SSL_version(srs.impl_->dtls_)) << version << " " << srs_role;
            EXPECT_EQ(utest_dtls_auto_newest(), SSL_version(peer.impl_->dtls_)) << version << " " << srs_role;

            SRTP_PROTECTION_PROFILE *srs_profile = SSL_get_selected_srtp_profile(srs.impl_->dtls_);
            SRTP_PROTECTION_PROFILE *peer_profile = SSL_get_selected_srtp_profile(peer.impl_->dtls_);
            ASSERT_TRUE(srs_profile != NULL) << version << " " << srs_role;
            ASSERT_TRUE(peer_profile != NULL) << version << " " << srs_role;
            EXPECT_EQ((unsigned long)SRTP_AES128_CM_SHA1_80, srs_profile->id) << version << " " << srs_role;
            EXPECT_EQ((unsigned long)SRTP_AES128_CM_SHA1_80, peer_profile->id) << version << " " << srs_role;

            string srs_recv, srs_send, peer_recv, peer_send;
            HELPER_ASSERT_SUCCESS(srs.get_srtp_key(srs_recv, srs_send));
            HELPER_ASSERT_SUCCESS(peer.get_srtp_key(peer_recv, peer_send));
            EXPECT_EQ(30, (int)srs_send.size());
            EXPECT_EQ(30, (int)srs_recv.size());
            EXPECT_TRUE(srs_send == peer_recv) << version << " " << srs_role;
            EXPECT_TRUE(srs_recv == peer_send) << version << " " << srs_role;
            EXPECT_TRUE(srs_send != srs_recv) << version << " " << srs_role;

            SrsSRTP srs_srtp, peer_srtp;
            HELPER_ASSERT_SUCCESS(srs_srtp.initialize(srs_recv, srs_send));
            HELPER_ASSERT_SUCCESS(peer_srtp.initialize(peer_recv, peer_send));
            expect_srtp_round_trip(&srs_srtp, &peer_srtp, 1);
            expect_srtp_round_trip(&peer_srtp, &srs_srtp, 1);
        }
    }
}

// RFC 8842 section 5.1 holds on DTLS 1.3 too: SRS completes a session only with the certificate whose
// a=fingerprint the offer has. On DTLS 1.3 the server gets the client's certificate in the client's last
// flight, encrypted, and the client gets the server's in the server's first flight; SRS checks it at its
// handshake end in both roles, and refuses someone on the media path with their own certificate.
VOID TEST(Dtls13HandshakeTest, PeerCertificateIsCheckedAgainstTheOffer)
{
    srs_error_t err = srs_success;

    MockDtlsCertificateForDtlsCtx browser_certificate(true);
    HELPER_ASSERT_SUCCESS(browser_certificate.initialize());
    MockDtlsCertificateForDtlsCtx attacker_certificate(true);
    HELPER_ASSERT_SUCCESS(attacker_certificate.initialize());
    ASSERT_STRNE(browser_certificate.get_fingerprint().c_str(), attacker_certificate.get_fingerprint().c_str());

    for (int srs_passive = 0; srs_passive < 2; srs_passive++) {
        for (int attacker = 0; attacker < 2; attacker++) {
            string srs_role = srs_passive ? "passive" : "active";

            MockRtcNetwork network;
            SrsSecurityTransport srs(&network);
            SrsSessionConfig cfg;
            cfg.dtls_role_ = srs_role;
            cfg.dtls_version_ = "dtls1.3";
            cfg.remote_fingerprint_algo_ = "sha-256";
            cfg.remote_fingerprint_ = browser_certificate.get_fingerprint();
            HELPER_ASSERT_SUCCESS(srs.initialize(&cfg));

            MockDtlsCallbackForHandshake pcb;
            SrsDtls peer(&pcb);
            ISrsDtlsCertificate *certificate = attacker ? &attacker_certificate : &browser_certificate;
            HELPER_ASSERT_SUCCESS(utest_dtls13_peer_initialize(&peer, certificate, srs_passive ? "active" : "passive"));

            if (srs_passive) {
                HELPER_ASSERT_SUCCESS(srs.start_active_handshake());
                HELPER_ASSERT_SUCCESS(peer.start_active_handshake());
            } else {
                HELPER_ASSERT_SUCCESS(peer.start_active_handshake());
                HELPER_ASSERT_SUCCESS(srs.start_active_handshake());
            }
            err = utest_dtls13_exchange_with_transport(&peer, &pcb, &srs, &network);

            SrsDtls *srs_dtls = dynamic_cast<SrsDtls *>(srs.dtls_);
            ASSERT_TRUE(srs_dtls != NULL);
            EXPECT_EQ(utest_dtls_auto_newest(), SSL_version(srs_dtls->impl_->dtls_)) << srs_role << " attacker=" << attacker;
            if (attacker) {
                EXPECT_TRUE(err != srs_success) << srs_role;
                EXPECT_FALSE(srs.handshake_done_) << srs_role;
                EXPECT_EQ(0, network.on_dtls_handshake_done_count_) << srs_role;
            } else {
                EXPECT_TRUE(err == srs_success) << srs_role << " " << srs_error_desc(err);
                EXPECT_TRUE(srs.handshake_done_) << srs_role;
                EXPECT_EQ(1, network.on_dtls_handshake_done_count_) << srs_role;
                EXPECT_EQ(1, pcb.handshake_done_count_) << srs_role;
            }
            srs_freep(err);
        }
    }
}

// Count each ClientHello an SSL writes, from OpenSSL's message callback, which sees each handshake
// message whole, before it is split into records.
static void utest_dtls_count_client_hello(int write_p, int version, int content_type, const void *buf, size_t len, SSL *ssl, void *arg)
{
    if (write_p && content_type == SSL3_RT_HANDSHAKE && len > 0 && ((const uint8_t *)buf)[0] == SSL3_MT_CLIENT_HELLO) {
        (*(int *)arg)++;
    }
}

// Run a handshake between a client and a server context, and count the ClientHellos the client sent: a
// second one answers a HelloRetryRequest. The version both agreed on is put in version, 0 if the handshake
// failed, and the name of the key exchange group in group.
static int utest_dtls_client_hellos(SSL_CTX *client_ctx, SSL_CTX *server_ctx, int &version, string &group)
{
    SSL *client = SSL_new(client_ctx);
    SSL *server = SSL_new(server_ctx);
    BIO *client_in = BIO_new(BIO_s_mem());
    BIO *client_out = BIO_new(BIO_s_mem());
    BIO *server_in = BIO_new(BIO_s_mem());
    BIO *server_out = BIO_new(BIO_s_mem());
    SSL_set_bio(client, client_in, client_out);
    SSL_set_bio(server, server_in, server_out);
    SSL_set_connect_state(client);
    SSL_set_accept_state(server);

    SSL *peers[] = {client, server};
    for (int i = 0; i < 2; i++) {
        SSL_set_options(peers[i], SSL_OP_NO_QUERY_MTU);
        SSL_set_mtu(peers[i], 1200);
        DTLS_set_link_mtu(peers[i], 1200);
    }

    int client_hellos = 0;
    SSL_set_msg_callback(client, utest_dtls_count_client_hello);
    SSL_set_msg_callback_arg(client, &client_hellos);

    char buf[4096];
    int nn = 0;
    for (int i = 0; i < 16 && (!SSL_is_init_finished(client) || !SSL_is_init_finished(server)); i++) {
        SSL_do_handshake(client);
        while ((nn = BIO_read(client_out, buf, sizeof(buf))) > 0) {
            BIO_write(server_in, buf, nn);
        }

        SSL_do_handshake(server);
        while ((nn = BIO_read(server_out, buf, sizeof(buf))) > 0) {
            BIO_write(client_in, buf, nn);
        }
    }

    version = 0;
    if (SSL_is_init_finished(client) && SSL_is_init_finished(server) && SSL_version(client) == SSL_version(server)) {
        version = SSL_version(client);
    }
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
    const char *name = SSL_group_to_name(client, SSL_get_negotiated_group(client));
    group = name ? name : "";
#endif

    SSL_free(client);
    SSL_free(server);
    return client_hellos;
}

// A browser guesses the key exchange group and sends its key_share in its first ClientHello: Chrome 158
// shares X25519MLKEM768 and X25519, as an OpenSSL 4.1 client with its default groups does. SRS must take
// one of them, or it answers with a HelloRetryRequest, and the browser's second ClientHello costs one more
// round trip, so DTLS 1.3 would take as long as DTLS 1.2, see RFC 8446 section 4.1.4. This holds with SRS's
// ECDSA certificate, the default, and its RSA one. DTLS 1.2 has no key_share and SRS no cookie exchange, so
// a browser sends one ClientHello in every build.
VOID TEST(Dtls13HandshakeTest, BrowserKeyShareNeedsNoHelloRetry)
{
    srs_error_t err = srs_success;

    for (int ecdsa = 0; ecdsa < 2; ecdsa++) {
        MockDtlsCertificateForDtlsCtx certificate(ecdsa == 1);
        HELPER_ASSERT_SUCCESS(certificate.initialize());

        SSL_CTX *browser = utest_dtls_newest_ctx(&certificate, true);
        SSL_CTX *srs_server = srs_build_dtls_ctx(&certificate, SrsDtlsVersionAuto, "passive");
        ASSERT_TRUE(browser != NULL);
        ASSERT_TRUE(srs_server != NULL);

        int version = 0;
        string group;
        EXPECT_EQ(1, utest_dtls_client_hellos(browser, srs_server, version, group)) << "ecdsa=" << ecdsa << ", group=" << group;
        EXPECT_EQ(utest_dtls_auto_newest(), version) << "ecdsa=" << ecdsa << ", group=" << group;

        SSL_CTX_free(browser);
        SSL_CTX_free(srs_server);
    }
}

#if OPENSSL_VERSION_NUMBER >= 0x40100000L
// The handshake traffic secrets each SSL logs, by SSL and keylog label, in hex.
static map<pair<const SSL *, string>, string> utest_dtls13_secrets;

static void utest_dtls13_keylog(const SSL *ssl, const char *line)
{
    string s = line;
    size_t label_end = s.find(' ');
    size_t secret_start = s.rfind(' ');
    if (label_end == string::npos || secret_start == label_end) {
        return;
    }
    utest_dtls13_secrets[make_pair(ssl, s.substr(0, label_end))] = s.substr(secret_start + 1);
}

static string utest_dtls13_secret(SSL *ssl, string label)
{
    string hex = utest_dtls13_secrets[make_pair((const SSL *)ssl, label)];
    string secret;
    for (int i = 0; i + 1 < (int)hex.size(); i += 2) {
        secret.push_back((char)strtol(hex.substr(i, 2).c_str(), NULL, 16));
    }
    return secret;
}

// HKDF-Expand-Label of TLS 1.3 with the "dtls13" label prefix of DTLS 1.3, for TLS_AES_128_GCM_SHA256,
// see RFC 8446 section 7.1 and RFC 9147 section 5.9.
static string utest_dtls13_expand_label(const string &secret, string label, int size)
{
    string info;
    info.push_back((char)(size >> 8));
    info.push_back((char)size);
    string full = "dtls13" + label;
    info.push_back((char)full.size());
    info += full;
    info.push_back(0);

    int mode = EVP_KDF_HKDF_MODE_EXPAND_ONLY;
    OSSL_PARAM params[5];
    params[0] = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, (char *)"SHA256", 0);
    params[1] = OSSL_PARAM_construct_int(OSSL_KDF_PARAM_MODE, &mode);
    params[2] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, (void *)secret.data(), secret.size());
    params[3] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO, (void *)info.data(), info.size());
    params[4] = OSSL_PARAM_construct_end();

    string out(size, 0);
    EVP_KDF *kdf = EVP_KDF_fetch(NULL, "HKDF", NULL);
    EVP_KDF_CTX *ctx = EVP_KDF_CTX_new(kdf);
    int r = EVP_KDF_derive(ctx, (unsigned char *)&out[0], size, params);
    EVP_KDF_CTX_free(ctx);
    EVP_KDF_free(kdf);
    return r > 0 ? out : "";
}

// The record number mask of RFC 9147 section 4.2.3, from the first 16 bytes of the ciphertext.
static string utest_dtls13_sn_mask(const string &secret, const char *ciphertext)
{
    string key = utest_dtls13_expand_label(secret, "sn", 16);
    unsigned char mask[16];
    int n = 0;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_128_ecb(), NULL, (const unsigned char *)key.data(), NULL);
    EVP_CIPHER_CTX_set_padding(ctx, 0);
    EVP_EncryptUpdate(ctx, mask, &n, (const unsigned char *)ciphertext, 16);
    EVP_CIPHER_CTX_free(ctx);
    return string((char *)mask, sizeof(mask));
}

// The record numbers (epoch, sequence) of the records in a datagram the server sent: the hellos in the
// clear in epoch 0, and the encrypted records of the handshake epoch 2, whose unified header has a masked
// 16-bit sequence number, which is the whole number this early in the epoch.
static vector<pair<uint64_t, uint64_t> > utest_dtls13_record_numbers(const string &datagram, const string &server_secret)
{
    vector<pair<uint64_t, uint64_t> > records;
    const uint8_t *p = (const uint8_t *)datagram.data();
    int size = (int)datagram.size();

    for (int pos = 0; pos < size;) {
        if ((p[pos] & DTLS13_UNI_HDR_FIX_BITS_MASK) == DTLS13_UNI_HDR_FIX_BITS) {
            int header = 1 + ((p[pos] & DTLS13_UNI_HDR_SEQ_BIT) ? 2 : 1) + ((p[pos] & DTLS13_UNI_HDR_LEN_BIT) ? 2 : 0);
            int len = size - pos - header;
            if (p[pos] & DTLS13_UNI_HDR_LEN_BIT) {
                len = (p[pos + header - 2] << 8) | p[pos + header - 1];
            }
            if (server_secret.empty() || !(p[pos] & DTLS13_UNI_HDR_SEQ_BIT) || len < 16 || pos + header + len > size) {
                break;
            }
            string mask = utest_dtls13_sn_mask(server_secret, datagram.data() + pos + header);
            uint64_t seq = (((p[pos + 1] ^ (uint8_t)mask[0]) << 8) | (p[pos + 2] ^ (uint8_t)mask[1]));
            records.push_back(make_pair((uint64_t)(p[pos] & 0x03), seq));
            pos += header + len;
            continue;
        }

        if (pos + 13 > size) {
            break;
        }
        uint64_t epoch = (p[pos + 3] << 8) | p[pos + 4];
        uint64_t seq = 0;
        for (int i = 5; i < 11; i++) {
            seq = (seq << 8) | p[pos + i];
        }
        records.push_back(make_pair(epoch, seq));
        pos += 13 + ((p[pos + 11] << 8) | p[pos + 12]);
    }

    return records;
}

// An ACK record of the given record numbers, as a browser sends it in the handshake epoch 2 with the
// TLS_AES_128_GCM_SHA256 keys of its handshake traffic secret, see RFC 9147 sections 4 and 7.
static string utest_dtls13_ack_record(const string &client_secret, const vector<pair<uint64_t, uint64_t> > &records, uint16_t seq)
{
    string key = utest_dtls13_expand_label(client_secret, "key", 16);
    string iv = utest_dtls13_expand_label(client_secret, "iv", 12);

    string plain;
    int list_size = (int)records.size() * 16;
    plain.push_back((char)(list_size >> 8));
    plain.push_back((char)list_size);
    for (int i = 0; i < (int)records.size(); i++) {
        for (int j = 56; j >= 0; j -= 8) {
            plain.push_back((char)(records[i].first >> j));
        }
        for (int j = 56; j >= 0; j -= 8) {
            plain.push_back((char)(records[i].second >> j));
        }
    }
    plain.push_back((char)SSL3_RT_ACK);

    int length = (int)plain.size() + 16;
    unsigned char header[5] = {DTLS13_UNI_HDR_FIX_BITS | DTLS13_UNI_HDR_SEQ_BIT | DTLS13_UNI_HDR_LEN_BIT | 0x02,
                               (unsigned char)(seq >> 8), (unsigned char)seq, (unsigned char)(length >> 8), (unsigned char)length};
    unsigned char nonce[12];
    memcpy(nonce, iv.data(), sizeof(nonce));
    nonce[10] ^= (unsigned char)(seq >> 8);
    nonce[11] ^= (unsigned char)seq;

    string ciphertext(length, 0);
    int n = 0;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, (const unsigned char *)key.data(), nonce);
    EVP_EncryptUpdate(ctx, NULL, &n, header, sizeof(header));
    EVP_EncryptUpdate(ctx, (unsigned char *)&ciphertext[0], &n, (const unsigned char *)plain.data(), (int)plain.size());
    EVP_EncryptFinal_ex(ctx, (unsigned char *)&ciphertext[n], &n);
    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, &ciphertext[plain.size()]);
    EVP_CIPHER_CTX_free(ctx);

    string mask = utest_dtls13_sn_mask(client_secret, ciphertext.data());
    header[1] ^= (unsigned char)mask[0];
    header[2] ^= (unsigned char)mask[1];
    return string((char *)header, sizeof(header)) + ciphertext;
}

// A browser's DTLS with dtls_version auto, as the DTLS client, which logs its handshake secrets and
// takes TLS_AES_128_GCM_SHA256, Chrome's suite, so a test can write the ACK records Chrome writes.
static srs_error_t utest_dtls13_browser_start(SrsDtls *srs, MockDtlsCallbackForHandshake *scb, SrsDtls *browser, MockDtlsCallbackForHandshake *bcb)
{
    srs_error_t err = srs_success;

    if ((err = srs->initialize("passive", "dtls1.3")) != srs_success) {
        return srs_error_wrap(err, "srs");
    }
    if ((err = browser->initialize("active", "auto")) != srs_success) {
        return srs_error_wrap(err, "browser");
    }
    SSL_CTX_set_keylog_callback(browser->impl_->dtls_ctx_, utest_dtls13_keylog);
    if (SSL_set_ciphersuites(browser->impl_->dtls_, "TLS_AES_128_GCM_SHA256") != 1) {
        return srs_error_new(ERROR_RTC_DTLS, "ciphersuites");
    }
    if ((err = srs->start_active_handshake()) != srs_success) {
        return srs_error_wrap(err, "srs start");
    }
    if ((err = browser->start_active_handshake()) != srs_success) {
        return srs_error_wrap(err, "browser start");
    }

    vector<string> hello;
    hello.swap(bcb->packets_);
    for (int i = 0; i < (int)hello.size(); i++) {
        if ((err = srs->on_dtls(&hello[i][0], (int)hello[i].size())) != srs_success) {
            return srs_error_wrap(err, "client hello");
        }
    }
    return err;
}

// Sleep in ST, so SRS's DTLS timer coroutine runs, until the real time passed: ST's clock misses the
// time a test spends in usleep, so one ST sleep may end early.
static void utest_dtls13_st_sleep(srs_utime_t duration)
{
    srs_utime_t start = srs_time_now_realtime();
    while (srs_time_now_realtime() - start < duration) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
}
#endif

// A DTLS 1.3 client that got only part of the server's first flight may ACK what it got, so the server
// resends only the rest (RFC 9147 sections 7.1 and 7.2). Chrome does, after a lost datagram of SRS's
// flight, with SPED or without it. SRS must take the ACK, still wait for the client's flight, and complete
// the handshake once the rest arrives. OpenSSL 4.1.0 refused the ACK with an unexpected_message alert in
// epoch 3, which the client cannot read, so the session hung (openssl/openssl#33242). An OpenSSL client
// never ACKs a server's handshake flight, so the test writes the ACK record Chrome writes.
VOID TEST(Dtls13LossTest, BrowserAckOfAPartialFirstFlightKeepsTheHandshake)
{
#if OPENSSL_VERSION_NUMBER >= 0x40100000L
    srs_error_t err = srs_success;

    MockDtlsCallbackForHandshake scb, bcb;
    SrsDtls srs(&scb);
    SrsDtls browser(&bcb);
    HELPER_ASSERT_SUCCESS(utest_dtls13_browser_start(&srs, &scb, &browser, &bcb));

    // SRS's first flight is the ServerHello, then the encrypted EncryptedExtensions to Finished.
    vector<string> flight;
    flight.swap(scb.packets_);
    ASSERT_GE((int)flight.size(), 2);

    // Only the first datagram arrives: the browser has the ServerHello, so its handshake keys, and waits.
    HELPER_ASSERT_SUCCESS(browser.on_dtls(&flight[0][0], (int)flight[0].size()));
    EXPECT_TRUE(bcb.packets_.empty());
    string client_secret = utest_dtls13_secret(browser.impl_->dtls_, "CLIENT_HANDSHAKE_TRAFFIC_SECRET");
    ASSERT_EQ(32, (int)client_secret.size());

    // The ServerHello goes in the clear, so its record numbers need no key.
    vector<pair<uint64_t, uint64_t> > received = utest_dtls13_record_numbers(flight[0], "");
    ASSERT_FALSE(received.empty());
    EXPECT_EQ(0, (int)received[0].first);
    string ack = utest_dtls13_ack_record(client_secret, received, 16);
    HELPER_EXPECT_SUCCESS(srs.on_dtls(&ack[0], (int)ack.size()));
    EXPECT_TRUE(scb.alerts_.empty());
    EXPECT_EQ(0, scb.handshake_done_count_);

    // The rest of the flight arrives, and the browser answers with its own.
    for (int i = 1; i < (int)flight.size(); i++) {
        HELPER_ASSERT_SUCCESS(browser.on_dtls(&flight[i][0], (int)flight[i].size()));
    }
    EXPECT_FALSE(bcb.packets_.empty());
    HELPER_ASSERT_SUCCESS(dtls_exchange(&browser, &bcb, &srs, &scb));

    EXPECT_EQ(1, scb.handshake_done_count_);
    EXPECT_EQ(1, bcb.handshake_done_count_);
    EXPECT_TRUE(scb.alerts_.empty());
    EXPECT_TRUE(bcb.alerts_.empty());
    EXPECT_EQ(DTLS1_3_VERSION, SSL_version(srs.impl_->dtls_));
#endif
}

// The same partial ACK, but the rest of SRS's flight is lost too. A DTLS 1.3 client that ACKed part of a
// flight waits for the server to resend the rest on the server's timer (RFC 9147 section 7.2): Chrome sent
// nothing for 10s after its ACK. So SRS resends on its own timer, and stops once its handshake is done.
VOID TEST(Dtls13LossTest, ServerResendsTheRestOfItsFlightOnItsTimer)
{
#if OPENSSL_VERSION_NUMBER >= 0x40100000L
    srs_error_t err = srs_success;

    MockDtlsCallbackForHandshake scb, bcb;
    SrsDtls srs(&scb);
    SrsDtls browser(&bcb);
    HELPER_ASSERT_SUCCESS(utest_dtls13_browser_start(&srs, &scb, &browser, &bcb));

    vector<string> flight;
    flight.swap(scb.packets_);
    ASSERT_GE((int)flight.size(), 2);

    HELPER_ASSERT_SUCCESS(browser.on_dtls(&flight[0][0], (int)flight[0].size()));
    string client_secret = utest_dtls13_secret(browser.impl_->dtls_, "CLIENT_HANDSHAKE_TRAFFIC_SECRET");
    ASSERT_EQ(32, (int)client_secret.size());
    string ack = utest_dtls13_ack_record(client_secret, utest_dtls13_record_numbers(flight[0], ""), 16);
    HELPER_EXPECT_SUCCESS(srs.on_dtls(&ack[0], (int)ack.size()));
    EXPECT_TRUE(scb.packets_.empty());

    // The timer starts at 50ms and doubles, so it fires at least once in this wait.
    utest_dtls13_st_sleep(200 * SRS_UTIME_MILLISECONDS);
    vector<string> resent;
    resent.swap(scb.packets_);
    ASSERT_FALSE(resent.empty());
    EXPECT_TRUE(scb.alerts_.empty());
    // Only the encrypted rest, in the unified header 001CSLEE, and not the ServerHello the browser ACKed.
    for (int i = 0; i < (int)resent.size(); i++) {
        string &datagram = resent[i];
        EXPECT_EQ(0x20, (uint8_t)datagram[0] & 0xe0) << "datagram " << i;
    }

    bcb.packets_.clear();
    for (int i = 0; i < (int)resent.size(); i++) {
        HELPER_ASSERT_SUCCESS(browser.on_dtls(&resent[i][0], (int)resent[i].size()));
    }
    EXPECT_FALSE(bcb.packets_.empty());
    HELPER_ASSERT_SUCCESS(dtls_exchange(&browser, &bcb, &srs, &scb));
    EXPECT_EQ(1, scb.handshake_done_count_);
    EXPECT_EQ(1, bcb.handshake_done_count_);
    EXPECT_TRUE(scb.alerts_.empty());
    EXPECT_TRUE(bcb.alerts_.empty());

    scb.packets_.clear();
    utest_dtls13_st_sleep(200 * SRS_UTIME_MILLISECONDS);
    EXPECT_TRUE(scb.packets_.empty());
#endif
}

// A DTLS 1.3 client may also ACK the server's whole first flight before its own flight is out, such as
// while it makes its CertificateVerify. That ACK says nothing of the client's flight: SRS must still wait
// for the client's Certificate, CertificateVerify and Finished, and be done only after them, since its
// fingerprint check needs the client's certificate. OpenSSL 4.1.0 refused this ACK too.
VOID TEST(Dtls13LossTest, BrowserAckOfTheWholeFlightBeforeItsOwnKeepsTheHandshake)
{
#if OPENSSL_VERSION_NUMBER >= 0x40100000L
    srs_error_t err = srs_success;

    MockDtlsCallbackForHandshake scb, bcb;
    SrsDtls srs(&scb);
    SrsDtls browser(&bcb);
    HELPER_ASSERT_SUCCESS(utest_dtls13_browser_start(&srs, &scb, &browser, &bcb));

    vector<string> flight;
    flight.swap(scb.packets_);
    ASSERT_GE((int)flight.size(), 2);
    for (int i = 0; i < (int)flight.size(); i++) {
        HELPER_ASSERT_SUCCESS(browser.on_dtls(&flight[i][0], (int)flight[i].size()));
    }

    // The browser's flight is held back, and its ACK of every record of SRS's flight goes first.
    vector<string> browser_flight;
    browser_flight.swap(bcb.packets_);
    ASSERT_FALSE(browser_flight.empty());
    string client_secret = utest_dtls13_secret(browser.impl_->dtls_, "CLIENT_HANDSHAKE_TRAFFIC_SECRET");
    string server_secret = utest_dtls13_secret(browser.impl_->dtls_, "SERVER_HANDSHAKE_TRAFFIC_SECRET");
    ASSERT_EQ(32, (int)client_secret.size());
    ASSERT_EQ(32, (int)server_secret.size());

    vector<pair<uint64_t, uint64_t> > received;
    for (int i = 0; i < (int)flight.size(); i++) {
        vector<pair<uint64_t, uint64_t> > records = utest_dtls13_record_numbers(flight[i], server_secret);
        received.insert(received.end(), records.begin(), records.end());
    }
    ASSERT_GE((int)received.size(), 2);
    string ack = utest_dtls13_ack_record(client_secret, received, 16);
    HELPER_EXPECT_SUCCESS(srs.on_dtls(&ack[0], (int)ack.size()));
    EXPECT_TRUE(scb.alerts_.empty());
    EXPECT_EQ(0, scb.handshake_done_count_);
    EXPECT_FALSE(SSL_is_init_finished(srs.impl_->dtls_));

    for (int i = 0; i < (int)browser_flight.size(); i++) {
        HELPER_ASSERT_SUCCESS(srs.on_dtls(&browser_flight[i][0], (int)browser_flight[i].size()));
    }
    EXPECT_EQ(1, scb.handshake_done_count_);
    HELPER_ASSERT_SUCCESS(dtls_exchange(&browser, &bcb, &srs, &scb));

    EXPECT_EQ(1, scb.handshake_done_count_);
    EXPECT_EQ(1, bcb.handshake_done_count_);
    EXPECT_TRUE(scb.alerts_.empty());
    EXPECT_TRUE(bcb.alerts_.empty());
#endif
}

// What a DTLS handshake between two contexts agreed on, read from the client once both are done.
struct UtestDtlsAgreed {
    int version_;
    std::string cipher_;
    std::string group_;
    std::string srtp_;
};

static UtestDtlsAgreed utest_dtls_agree(SSL_CTX *client_ctx, SSL_CTX *server_ctx)
{
    UtestDtlsAgreed agreed;
    agreed.version_ = 0;

    SSL *client = SSL_new(client_ctx);
    SSL *server = SSL_new(server_ctx);
    BIO *client_in = BIO_new(BIO_s_mem());
    BIO *client_out = BIO_new(BIO_s_mem());
    BIO *server_in = BIO_new(BIO_s_mem());
    BIO *server_out = BIO_new(BIO_s_mem());
    SSL_set_bio(client, client_in, client_out);
    SSL_set_bio(server, server_in, server_out);
    SSL_set_connect_state(client);
    SSL_set_accept_state(server);

    SSL *peers[] = {client, server};
    for (int i = 0; i < 2; i++) {
        SSL_set_options(peers[i], SSL_OP_NO_QUERY_MTU);
        SSL_set_mtu(peers[i], 1200);
        DTLS_set_link_mtu(peers[i], 1200);
    }

    char buf[4096];
    for (int i = 0; i < 32 && (!SSL_is_init_finished(client) || !SSL_is_init_finished(server)); i++) {
        SSL_do_handshake(client);
        for (int nn = 0; (nn = BIO_read(client_out, buf, sizeof(buf))) > 0;) {
            BIO_write(server_in, buf, nn);
        }
        SSL_do_handshake(server);
        for (int nn = 0; (nn = BIO_read(server_out, buf, sizeof(buf))) > 0;) {
            BIO_write(client_in, buf, nn);
        }
    }

    if (SSL_is_init_finished(client) && SSL_is_init_finished(server) && SSL_version(client) == SSL_version(server)) {
        agreed.version_ = SSL_version(client);
        agreed.cipher_ = SSL_CIPHER_get_name(SSL_get_current_cipher(client));
        SRTP_PROTECTION_PROFILE *profile = SSL_get_selected_srtp_profile(client);
        agreed.srtp_ = profile ? profile->name : "";
#if OPENSSL_VERSION_NUMBER >= 0x30200000L
        const char *group = SSL_get0_group_name(client);
        agreed.group_ = group ? group : "";
#endif
    }

    SSL_free(client);
    SSL_free(server);
    return agreed;
}

// A browser's DTLS client, limited to one DTLS 1.2 cipher suite, which offers SRTP as WebRTC does.
static SSL_CTX *utest_dtls12_browser_ctx(ISrsDtlsCertificate *certificate, const char *suite)
{
    SSL_CTX *ctx = utest_dtls12_ctx(certificate, true);
    if (SSL_CTX_set_cipher_list(ctx, suite) != 1 || SSL_CTX_set_tlsext_use_srtp(ctx, "SRTP_AES128_CM_SHA1_80") != 0) {
        SSL_CTX_free(ctx);
        return NULL;
    }
    return ctx;
}

// A browser offers these DTLS 1.2 suites for an ECDSA and an RSA certificate: AES-GCM and ChaCha20-Poly1305
// AEADs, and AES-CBC with SHA-1 for older stacks. SRS's DTLS server takes each one alone, with either kind of
// certificate SRS makes, and picks SRTP_AES128_CM_HMAC_SHA1_80 for media. The bundled OpenSSL must keep them
// whatever features its build turns off.
VOID TEST(OpenSslAlgorithmTest, Dtls12SuitesWithEcdsaAndRsaCertificates)
{
    srs_error_t err = srs_success;

    const char *ecdsa_suites[] = {"ECDHE-ECDSA-AES128-GCM-SHA256", "ECDHE-ECDSA-AES256-GCM-SHA384",
                                  "ECDHE-ECDSA-CHACHA20-POLY1305", "ECDHE-ECDSA-AES128-SHA", "ECDHE-ECDSA-AES256-SHA"};
    const char *rsa_suites[] = {"ECDHE-RSA-AES128-GCM-SHA256", "ECDHE-RSA-AES256-GCM-SHA384",
                                "ECDHE-RSA-CHACHA20-POLY1305", "ECDHE-RSA-AES128-SHA", "ECDHE-RSA-AES256-SHA"};

    for (int ecdsa = 0; ecdsa < 2; ecdsa++) {
        MockDtlsCertificateForDtlsCtx certificate(ecdsa == 1);
        HELPER_ASSERT_SUCCESS(certificate.initialize());
        const char **suites = ecdsa ? ecdsa_suites : rsa_suites;

        for (int i = 0; i < 5; i++) {
            SSL_CTX *browser = utest_dtls12_browser_ctx(&certificate, suites[i]);
            SSL_CTX *srs = srs_build_dtls_ctx(&certificate, SrsDtlsVersion1_2, "passive");
            ASSERT_TRUE(browser != NULL) << suites[i];
            ASSERT_TRUE(srs != NULL) << suites[i];

            UtestDtlsAgreed agreed = utest_dtls_agree(browser, srs);
            EXPECT_EQ(DTLS1_2_VERSION, agreed.version_) << suites[i];
            EXPECT_EQ(suites[i], agreed.cipher_);
            EXPECT_EQ("SRTP_AES128_CM_SHA1_80", agreed.srtp_) << suites[i];

            SSL_CTX_free(browser);
            SSL_CTX_free(srs);
        }
    }
}

// The key exchange groups browsers share: X25519MLKEM768 (post-quantum, DTLS 1.3 only), X25519 and P-256.
// On OpenSSL 4.1 SRS's DTLS 1.3 server takes each one alone, with each DTLS 1.3 cipher suite and either
// certificate, and DTLS 1.2 still takes X25519. On DTLS 1.2 the client's groups also name the curves its
// ECDSA certificate may use (RFC 8422 section 5.1.1), so a browser lists P-256 after X25519 there. Builds before
// OpenSSL 4.1 have no DTLS 1.3, and SRS limits an ECDSA context there to the P curves, so they only take P-256
// on DTLS 1.2.
VOID TEST(OpenSslAlgorithmTest, KeyExchangeGroupsAndDtls13Suites)
{
    srs_error_t err = srs_success;

    for (int ecdsa = 0; ecdsa < 2; ecdsa++) {
        MockDtlsCertificateForDtlsCtx certificate(ecdsa == 1);
        HELPER_ASSERT_SUCCESS(certificate.initialize());

        if (true) {
            SSL_CTX *browser = utest_dtls12_browser_ctx(&certificate, "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256");
            ASSERT_TRUE(browser != NULL);
            EXPECT_EQ(1, SSL_CTX_set1_groups_list(browser, "P-256"));
            SSL_CTX *srs = srs_build_dtls_ctx(&certificate, SrsDtlsVersion1_2, "passive");
            ASSERT_TRUE(srs != NULL);
            UtestDtlsAgreed agreed = utest_dtls_agree(browser, srs);
            EXPECT_EQ(DTLS1_2_VERSION, agreed.version_) << "ecdsa=" << ecdsa;
#if OPENSSL_VERSION_NUMBER >= 0x30200000L
            EXPECT_EQ("secp256r1", agreed.group_) << "ecdsa=" << ecdsa;
#endif
            SSL_CTX_free(browser);
            SSL_CTX_free(srs);
        }

#if OPENSSL_VERSION_NUMBER >= 0x40100000L
        if (true) {
            SSL_CTX *browser = utest_dtls12_browser_ctx(&certificate, "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256");
            ASSERT_TRUE(browser != NULL);
            EXPECT_EQ(1, SSL_CTX_set1_groups_list(browser, "X25519:P-256"));
            SSL_CTX *srs = srs_build_dtls_ctx(&certificate, SrsDtlsVersion1_2, "passive");
            ASSERT_TRUE(srs != NULL);
            UtestDtlsAgreed agreed = utest_dtls_agree(browser, srs);
            EXPECT_EQ(DTLS1_2_VERSION, agreed.version_) << "ecdsa=" << ecdsa;
            EXPECT_EQ("x25519", agreed.group_) << "ecdsa=" << ecdsa;
            SSL_CTX_free(browser);
            SSL_CTX_free(srs);
        }

        const char *groups[] = {"X25519MLKEM768", "X25519", "P-256"};
        const char *group_names[] = {"X25519MLKEM768", "x25519", "secp256r1"};
        const char *suites[] = {"TLS_AES_128_GCM_SHA256", "TLS_AES_256_GCM_SHA384", "TLS_CHACHA20_POLY1305_SHA256"};
        for (int g = 0; g < 3; g++) {
            for (int s = 0; s < 3; s++) {
                SSL_CTX *browser = utest_dtls_newest_ctx(&certificate, true);
                ASSERT_TRUE(browser != NULL);
                EXPECT_EQ(1, SSL_CTX_set1_groups_list(browser, groups[g])) << groups[g];
                EXPECT_EQ(1, SSL_CTX_set_ciphersuites(browser, suites[s])) << suites[s];
                EXPECT_EQ(0, SSL_CTX_set_tlsext_use_srtp(browser, "SRTP_AES128_CM_SHA1_80"));
                SSL_CTX *srs = srs_build_dtls_ctx(&certificate, SrsDtlsVersion1_3, "passive");
                ASSERT_TRUE(srs != NULL);

                UtestDtlsAgreed agreed = utest_dtls_agree(browser, srs);
                EXPECT_EQ(DTLS1_3_VERSION, agreed.version_) << groups[g] << " " << suites[s] << " ecdsa=" << ecdsa;
                EXPECT_EQ(suites[s], agreed.cipher_) << groups[g] << " ecdsa=" << ecdsa;
                EXPECT_EQ(group_names[g], agreed.group_) << suites[s] << " ecdsa=" << ecdsa;
                EXPECT_EQ("SRTP_AES128_CM_SHA1_80", agreed.srtp_) << groups[g] << " " << suites[s];

                SSL_CTX_free(browser);
                SSL_CTX_free(srs);
            }
        }
#endif
    }
}

static string utest_unhex(const char *hex)
{
    string s;
    for (int i = 0; hex[i] && hex[i + 1]; i += 2) {
        char b[3] = {hex[i], hex[i + 1], 0};
        s.push_back((char)strtol(b, NULL, 16));
    }
    return s;
}

// One EVP cipher pass over the input, with an AEAD tag out when tag is not NULL. Returns the output in hex,
// or an empty string when OpenSSL refuses a step.
static string utest_evp_encrypt(const EVP_CIPHER *cipher, const string &key, const string &iv, const string &in, string *tag)
{
    string out;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    EVP_CIPHER_CTX_set_flags(ctx, EVP_CIPHER_CTX_FLAG_WRAP_ALLOW);
    unsigned char buf[256];
    int n = 0, m = 0;
    if (cipher && EVP_EncryptInit_ex(ctx, cipher, NULL, (const unsigned char *)key.data(), iv.empty() ? NULL : (const unsigned char *)iv.data()) == 1 &&
        EVP_CIPHER_CTX_set_padding(ctx, 0) == 1 &&
        EVP_EncryptUpdate(ctx, buf, &n, (const unsigned char *)in.data(), (int)in.size()) == 1 &&
        EVP_EncryptFinal_ex(ctx, buf + n, &m) == 1) {
        out = utest_hex(buf, n + m);
        if (tag) {
            unsigned char t[16];
            *tag = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, t) == 1 ? utest_hex(t, 16) : "";
        }
    }
    EVP_CIPHER_CTX_free(ctx);
    return out;
}

// The primitives libsrtp and libsrt take from OpenSSL, besides those SRS calls itself and the tests above
// check, match their published vectors:
//   - SRTP_AES128_CM_HMAC_SHA1_80 and SRT encrypt by AES-CTR and authenticate SRTP by HMAC-SHA1; SRT also
//     has AES-GCM, as do the SRTP AEAD profiles (NIST SP 800-38A F.5.1 and F.5.5, RFC 2202 case 2, the GCM
//     spec's test cases 2 and 14).
//   - SRT derives its key from the passphrase by PBKDF2-HMAC-SHA1, wraps it by AES key wrap, and has AES-ECB
//     for its counter blocks (RFC 6070 case 1, RFC 3394 4.1, FIPS-197 C.1).
//   - The STUN MESSAGE-INTEGRITY of a long-term credential is keyed by MD5 (RFC 1321's "abc").
VOID TEST(OpenSslAlgorithmTest, PrimitivesOfSrtpSrtAndStunMatchTheirVectors)
{
    string ctr_iv = utest_unhex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    string ctr_plain = utest_unhex("6bc1bee22e409f96e93d7e117393172a");
    EXPECT_EQ("874d6191b620e3261bef6864990db6ce",
              utest_evp_encrypt(EVP_aes_128_ctr(), utest_unhex("2b7e151628aed2a6abf7158809cf4f3c"), ctr_iv, ctr_plain, NULL));
    EXPECT_EQ("601ec313775789a5b7a7f504bbf3d228",
              utest_evp_encrypt(EVP_aes_256_ctr(), utest_unhex("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4"), ctr_iv, ctr_plain, NULL));

    string tag;
    EXPECT_EQ("0388dace60b6a392f328c2b971b2fe78", utest_evp_encrypt(EVP_aes_128_gcm(), string(16, 0), string(12, 0), string(16, 0), &tag));
    EXPECT_EQ("ab6e47d42cec13bdf53a67b21257bddf", tag);
    EXPECT_EQ("cea7403d4d606b6e074ec5d3baf39d18", utest_evp_encrypt(EVP_aes_256_gcm(), string(32, 0), string(12, 0), string(16, 0), &tag));
    EXPECT_EQ("d0d1c8a799996bf0265b98b5d48ab919", tag);

    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int md_size = 0;
    const char *data = "what do ya want for nothing?";
    ASSERT_TRUE(HMAC(EVP_sha1(), "Jefe", 4, (const unsigned char *)data, strlen(data), md, &md_size) != NULL);
    EXPECT_EQ("effcdf6ae5eb2fa2d27416d5f184df9c259a7c79", utest_hex(md, md_size));

    unsigned char derived[20];
    ASSERT_EQ(1, PKCS5_PBKDF2_HMAC_SHA1("password", 8, (const unsigned char *)"salt", 4, 1, sizeof(derived), derived));
    EXPECT_EQ("0c60c80f961f0e71f3a9b524af6012062fe037a6", utest_hex(derived, sizeof(derived)));

    EXPECT_EQ("1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5",
              utest_evp_encrypt(EVP_aes_128_wrap(), utest_unhex("000102030405060708090a0b0c0d0e0f"), "", utest_unhex("00112233445566778899aabbccddeeff"), NULL));
    EXPECT_EQ("69c4e0d86a7b0430d8cdb78070b4c55a",
              utest_evp_encrypt(EVP_aes_128_ecb(), utest_unhex("000102030405060708090a0b0c0d0e0f"), "", utest_unhex("00112233445566778899aabbccddeeff"), NULL));

    ASSERT_EQ(1, EVP_Digest("abc", 3, md, &md_size, EVP_md5(), NULL));
    EXPECT_EQ("900150983cd24fb0d6963f7d28e17f72", utest_hex(md, md_size));
}
