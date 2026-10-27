//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai41.hpp>

#include <srs_app_http_stream.hpp>
#include <srs_app_log.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_security.hpp>
#include <srs_app_srt_source.hpp>
#include <srs_app_statistic.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_packet.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_http_client.hpp>
#include <srs_protocol_http_conn.hpp>
#include <srs_protocol_log.hpp>
#include <srs_protocol_rtc_stun.hpp>
#include <srs_protocol_rtmp_handshake.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_rtp.hpp>
#ifdef SRS_RTSP
#include <srs_protocol_rtsp_stack.hpp>
#endif
#include <srs_protocol_sdp.hpp>
#include <srs_utest_ai05.hpp>
#include <srs_utest_ai08.hpp>
#include <srs_utest_ai11.hpp>
#include <srs_utest_ai18.hpp>
#include <srs_utest_ai29.hpp>
#include <srs_utest_ai30.hpp>
#include <srs_utest_ai37.hpp>
#include <srs_utest_manual_config.hpp>
#include <srs_utest_manual_coworkers.hpp>
#include <srs_utest_manual_mock.hpp>
#include <srs_utest_manual_protocol.hpp>

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace std;
using namespace srs_internal;

extern srs_utime_t _srs_system_time_us_cache;
extern srs_mkdir_t _srs_mkdir_fn;

// The first read of the cached clock in a fresh process, such as a log line or a
// statistic written at startup before the server's clock timer has ticked, gets the
// real time instead of 0.
VOID TEST(KernelUtilityTest, CachedClockReadBeforeFirstTickGetsRealTime)
{
    srs_utime_t before = srs_time_now_realtime();
    ASSERT_GT(before, 0);

    _srs_system_time_us_cache = 0;
    srs_utime_t now = srs_time_now_cached();

    EXPECT_GE(now, before);
    EXPECT_EQ(now, _srs_system_time_us_cache);
    EXPECT_LT(now - before, 10 * SRS_UTIME_SECONDS);
}

// An HLS or DVR path whose app directory is a regular file, such as a file left at
// ./objs/nginx/html/live, cannot be created: the stream gets an error rather than
// writing into a missing directory.
VOID TEST(KernelUtilityTest, HlsDirUnderRegularFileFailsToCreate)
{
    srs_error_t err = srs_success;

    string file = srs_utest_tmp_dir() + "/srs_utest_ai41_live_" + srs_strconv_format_int(getpid());
    int fd = ::open(file.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    ASSERT_GE(fd, 0);
    ::close(fd);

    SrsPath path;
    err = path.mkdir_all(file + "/livestream/hls");
    EXPECT_EQ(ERROR_SYSTEM_CREATE_DIR, srs_error_code(err));
    srs_freep(err);

    EXPECT_FALSE(path.exists(file + "/livestream"));
    EXPECT_FALSE(path.is_dir(file));

    ::unlink(file.c_str());
}

// Another process creates the directory between the server's check and its mkdir.
static int ai41_mkdir_raced(const char *dir)
{
    srs_mkdir(dir);
    errno = EEXIST;
    return -1;
}

// Two servers (or an origin and its DVR tool) sharing one hls_path create the same
// stream directory at once: the one whose mkdir finds it already there goes on with
// the directory the other made.
VOID TEST(KernelUtilityTest, HlsDirCreatedByAnotherServerAtOnceIsUsed)
{
    srs_error_t err = srs_success;

    string parent = srs_utest_tmp_dir() + "/srs_utest_ai41_race_" + srs_strconv_format_int(getpid());
    string dir = parent + "/livestream";
    ASSERT_EQ(0, srs_mkdir(parent.c_str()));

    srs_mkdir_t old = _srs_mkdir_fn;
    _srs_mkdir_fn = ai41_mkdir_raced;
    SrsPath path;
    err = path.mkdir_all(dir);
    _srs_mkdir_fn = old;

    HELPER_EXPECT_SUCCESS(err);
    EXPECT_TRUE(path.is_dir(dir));

    ::rmdir(dir.c_str());
    ::rmdir(parent.c_str());
}

// An HTTP callback server that sends part of its response and then stalls makes the
// body read time out: the hook gets the timeout, not the bytes read so far as a body.
VOID TEST(KernelUtilityTest, HookResponseBodyTimesOutMidway)
{
    srs_error_t err = srs_success;

    string part(5000, 'x');
    MockSrsReader reader(part);
    reader.set_drained_error(srs_error_new(ERROR_SOCKET_TIMEOUT, "recv timeout"));

    string body;
    err = srs_io_readall(&reader, body);
    EXPECT_EQ(ERROR_SOCKET_TIMEOUT, srs_error_code(err));
    srs_freep(err);

    // Both reads of 4096 and 904 bytes were kept before the timeout.
    EXPECT_EQ(part.size(), body.size());
}

// A WebRTC video track of a WHIP publisher, with the profile-level-id of its SDP.
static SrsRtcTrackDescription *ai41_whip_avc_desc(string profile_level_id)
{
    SrsRtcTrackDescription *desc = new SrsRtcTrackDescription();
    desc->type_ = "video";
    desc->ssrc_ = 5678;
    SrsVideoPayload *media = new SrsVideoPayload(102, "H264", 90000);
    media->h264_param_.profile_level_id_ = profile_level_id;
    desc->media_ = media;
    return desc;
}

// An H.264 SPS of 768x320.
static uint8_t ai41_avc_sps_768x320[] = {
    0x67, 0x64, 0x00, 0x20, 0xac, 0xd9, 0x40, 0xc0, 0x29, 0xb0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01,
    0x00, 0x00, 0x03, 0x00, 0x32, 0x0f, 0x18, 0x31, 0x96};

// A WHIP publisher whose SDP has a profile-level-id that is not hex, such as
// "42z01f" from a broken client, still publishes: the codec is not listed from the
// SDP, and the stream is listed once its first SPS gives the codec and size.
VOID TEST(KernelUtilityTest, WhipSdpProfileLevelIdNotHexListsStreamFromSps)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsStatistic> stat(new SrsStatistic());
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "whip_bad_profile"));
    SrsUniquePtr<SrsRtcTrackDescription> desc(ai41_whip_avc_desc("42z01f"));
    MockRtcPacketReceiver receiver;
    SrsRtcVideoRecvTrack track(&receiver, desc.get(), false);

    SrsRtcFormat format;
    format.stat_ = stat.get();
    HELPER_EXPECT_SUCCESS(format.initialize(req.get()));

    SrsRtpPacket slice;
    slice.nalu_type_ = kFuA;
    HELPER_EXPECT_SUCCESS(format.on_rtp_packet(&track, &slice, false));
    EXPECT_TRUE(stat->find_stream_by_url(req->get_stream_url()) == NULL);

    SrsRtpPacket pkt;
    SrsRtpRawPayload *raw = new SrsRtpRawPayload();
    raw->sample_->bytes_ = (char *)ai41_avc_sps_768x320;
    raw->sample_->size_ = sizeof(ai41_avc_sps_768x320);
    pkt.nalu_type_ = SrsAvcNaluTypeSPS;
    pkt.set_payload(raw, SrsRtpPacketPayloadTypeRaw);
    HELPER_EXPECT_SUCCESS(format.on_rtp_packet(&track, &pkt, false));

    SrsStatisticStream *stream = stat->find_stream_by_url(req->get_stream_url());
    ASSERT_TRUE(stream != NULL);
    EXPECT_EQ(SrsVideoCodecIdAVC, stream->vcodec_);
    EXPECT_EQ(768, stream->width_);
    EXPECT_EQ(320, stream->height_);
}

// A Docker env with two spaces between the RTMP ports, such as
// SRS_RTMP_LISTEN="1935  19350", gives an empty listen between them, which the config
// check refuses by name when the server starts, rather than listening on a port 0.
VOID TEST(KernelUtilityTest, RtmpListenEnvWithDoubleSpaceIsRefused)
{
    srs_error_t err = srs_success;

    if (true) {
        MockSrsConfig conf;
        SrsSetEnvConfig(conf, listen, "SRS_RTMP_LISTEN", "1935  19350");
        err = conf.mock_parse(_MIN_OK_CONF);
        EXPECT_EQ(ERROR_SYSTEM_CONFIG_INVALID, srs_error_code(err));
        string desc = srs_error_desc(err);
        EXPECT_TRUE(desc.find("rtmp.listen= is invalid") != string::npos) << desc;
        srs_freep(err);
    }

    if (true) {
        MockSrsConfig conf;
        SrsSetEnvConfig(conf, listen, "SRS_RTMP_LISTEN", "1935 19350");
        HELPER_EXPECT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_EQ(2, (int)conf.get_listens().size());
    }
}

// A security rule with a slash but no prefix length, such as a typo
// "allow play 10.0.0.0/;", matches no client; the other rules still apply.
VOID TEST(KernelUtilityTest, SecurityAllowRuleWithEmptyPrefixLengthMatchesNoClient)
{
    srs_error_t err = srs_success;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost __defaultVhost__{security{enabled on; allow play 10.0.0.0/; allow play 192.168.1.0/24;}}"));

    SrsSecurity sec;
    sec.config_ = &conf;
    SrsRequest req;
    req.vhost_ = "__defaultVhost__";

    err = sec.check(SrsRtmpConnPlay, "10.0.0.5", &req);
    EXPECT_EQ(ERROR_SYSTEM_SECURITY_ALLOW, srs_error_code(err));
    srs_freep(err);

    HELPER_EXPECT_SUCCESS(sec.check(SrsRtmpConnPlay, "192.168.1.5", &req));
    sec.config_ = NULL;
}

// A security rule with an IPv4 prefix length over 32, such as "allow play 10.0.0.0/33;"
// or an IPv6-style "/64", must match no client, as the other malformed rules do. The
// mask is read from a table of 32 entries by the length, with no upper check, so the
// first client of the vhost reads past the table (ASan on Linux: global-buffer-overflow;
// the macOS build reads it unflagged, and the garbage mask happens to match no client).
VOID TEST(KernelUtilityTest, DISABLED_SecurityAllowRuleWithPrefixLengthOver32_ReadsPastMaskTable)
{
    srs_error_t err = srs_success;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost __defaultVhost__{security{enabled on; allow play 10.0.0.0/33; allow play 192.168.1.0/24;}}"));

    SrsSecurity sec;
    sec.config_ = &conf;
    SrsRequest req;
    req.vhost_ = "__defaultVhost__";

    err = sec.check(SrsRtmpConnPlay, "10.0.0.5", &req);
    EXPECT_EQ(ERROR_SYSTEM_SECURITY_ALLOW, srs_error_code(err));
    srs_freep(err);

    HELPER_EXPECT_SUCCESS(sec.check(SrsRtmpConnPlay, "192.168.1.5", &req));
    sec.config_ = NULL;
}

// An AMF0 Date of a script tag: the 0x0B marker, 8 bytes of milliseconds since the
// epoch, and a 2-byte time zone in minutes.
static void amf0_write_raw_date(SrsBuffer *b, int64_t ms_bits, int16_t time_zone)
{
    b->write_1bytes(0x0B);
    b->write_8bytes(ms_bits);
    b->write_2bytes(time_zone);
}

// A publisher that sends an FLV file's script tag as it is, such as a file indexed by
// flvtool2 or yamdi, puts a keyframes index (strict arrays of times and file positions)
// and a metadatadate Date in its @setDataFrame onMetaData ECMA array. The server copies
// the array into the metadata object, removes the duration, adds its own server info,
// and encodes it again for every player. A player must get the index and the date as
// the publisher sent them.
VOID TEST(ProtocolAmf0Test, PublisherMetadataWithKeyframesIndex_PlayerGetsArraysAndDate)
{
    srs_error_t err = srs_success;

    const double times[] = {0, 2.002, 4.004};
    const double positions[] = {13, 52310, 104998};
    const int64_t date_bits = 0x4278d3c8a0b10000LL;

    char buf[1024];
    SrsBuffer b(buf, sizeof(buf));
    HELPER_ASSERT_SUCCESS(srs_amf0_write_string(&b, "@setDataFrame"));
    HELPER_ASSERT_SUCCESS(srs_amf0_write_string(&b, "onMetaData"));
    b.write_1bytes(0x08); // ECMA array.
    b.write_4bytes(5);
    HELPER_ASSERT_SUCCESS(srs_amf0_write_utf8(&b, "duration"));
    HELPER_ASSERT_SUCCESS(srs_amf0_write_number(&b, 60.06));
    HELPER_ASSERT_SUCCESS(srs_amf0_write_utf8(&b, "width"));
    HELPER_ASSERT_SUCCESS(srs_amf0_write_number(&b, 1280));
    HELPER_ASSERT_SUCCESS(srs_amf0_write_utf8(&b, "height"));
    HELPER_ASSERT_SUCCESS(srs_amf0_write_number(&b, 720));
    HELPER_ASSERT_SUCCESS(srs_amf0_write_utf8(&b, "metadatadate"));
    amf0_write_raw_date(&b, date_bits, -480);
    HELPER_ASSERT_SUCCESS(srs_amf0_write_utf8(&b, "keyframes"));
    if (true) {
        SrsUniquePtr<SrsAmf0Object> keyframes(SrsAmf0Any::object());
        SrsAmf0StrictArray *t = SrsAmf0Any::strict_array();
        SrsAmf0StrictArray *p = SrsAmf0Any::strict_array();
        for (int i = 0; i < 3; i++) {
            t->append(SrsAmf0Any::number(times[i]));
            p->append(SrsAmf0Any::number(positions[i]));
        }
        keyframes->set("times", t);
        keyframes->set("filepositions", p);
        HELPER_ASSERT_SUCCESS(srs_amf0_write_any(&b, keyframes.get()));
    }
    b.write_2bytes(0x00); // ECMA array end.
    b.write_1bytes(0x09);

    // The server decodes the publisher's message.
    SrsOnMetaDataPacket published;
    SrsBuffer in(buf, b.pos());
    HELPER_ASSERT_SUCCESS(published.decode(&in));
    EXPECT_STREQ("onMetaData", published.name_.c_str());

    // The meta cache encodes it again for players.
    SrsMessageHeader header;
    header.message_type_ = RTMP_MSG_AMF0DataMessage;
    header.payload_length_ = b.pos();
    SrsMetaCache cache;
    bool updated = false;
    HELPER_ASSERT_SUCCESS(cache.update_data(&header, &published, updated));
    ASSERT_TRUE(updated);
    ASSERT_TRUE(cache.data() != NULL);

    // A player decodes what the server sends.
    SrsOnMetaDataPacket played;
    SrsBuffer out(cache.data()->payload(), cache.data()->size());
    HELPER_ASSERT_SUCCESS(played.decode(&out));
    SrsAmf0Object *meta = played.metadata_;

    EXPECT_TRUE(meta->get_property("duration") == NULL);
    ASSERT_TRUE(meta->ensure_property_string("server") != NULL);
    ASSERT_TRUE(meta->ensure_property_number("width") != NULL);
    EXPECT_EQ(1280, meta->ensure_property_number("width")->to_number());

    SrsAmf0Any *date = meta->get_property("metadatadate");
    ASSERT_TRUE(date != NULL);
    ASSERT_TRUE(date->is_date());
    EXPECT_EQ(date_bits, date->to_date());
    EXPECT_EQ(-480, date->to_date_time_zone());

    SrsAmf0Any *index = meta->get_property("keyframes");
    ASSERT_TRUE(index != NULL);
    ASSERT_TRUE(index->is_object());
    SrsAmf0Any *t = index->to_object()->get_property("times");
    SrsAmf0Any *p = index->to_object()->get_property("filepositions");
    ASSERT_TRUE(t != NULL && t->is_strict_array());
    ASSERT_TRUE(p != NULL && p->is_strict_array());
    ASSERT_EQ(3, t->to_strict_array()->count());
    ASSERT_EQ(3, p->to_strict_array()->count());
    for (int i = 0; i < 3; i++) {
        EXPECT_EQ(times[i], t->to_strict_array()->at(i)->to_number());
        EXPECT_EQ(positions[i], p->to_strict_array()->at(i)->to_number());
    }
}

// A broken encoder sends onMetaData whose message ends inside a Date: in its 8 bytes of
// milliseconds, or before its 2-byte time zone. The server must refuse the metadata as
// an AMF0 decode error, not read past the message.
VOID TEST(ProtocolAmf0Test, PublisherMetadataCutInsideDate_DecodeFails)
{
    srs_error_t err = srs_success;

    // The bytes after the Date marker that the message still holds.
    const int lefts[] = {5, 9};
    for (int i = 0; i < 2; i++) {
        char buf[128];
        SrsBuffer b(buf, sizeof(buf));
        HELPER_ASSERT_SUCCESS(srs_amf0_write_string(&b, "onMetaData"));
        b.write_1bytes(0x08); // ECMA array.
        b.write_4bytes(1);
        HELPER_ASSERT_SUCCESS(srs_amf0_write_utf8(&b, "metadatadate"));
        amf0_write_raw_date(&b, 0x4278d3c8a0b10000LL, 0);
        int size = b.pos() - 11 + 1 + lefts[i];

        SrsOnMetaDataPacket pkt;
        SrsBuffer in(buf, size);
        err = pkt.decode(&in);
        EXPECT_EQ(ERROR_RTMP_AMF0_DECODE, srs_error_code(err)) << "left=" << lefts[i];
        srs_freep(err);
    }
}

// The key and certificate of the HTTPS API, HTTPS stream and RTMPS server in the default
// configs.
static const char *tls_server_key = "conf/server.key";
static const char *tls_server_crt = "conf/server.crt";

// Write a new RSA private key, which is not the key of tls_server_crt, to a PEM file.
static bool tls_write_other_rsa_key(const string &path)
{
    EVP_PKEY *pkey = NULL;
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    bool ok = pctx && EVP_PKEY_keygen_init(pctx) == 1 && EVP_PKEY_CTX_set_rsa_keygen_bits(pctx, 2048) == 1 && EVP_PKEY_keygen(pctx, &pkey) == 1;
    EVP_PKEY_CTX_free(pctx);

    FILE *f = ok ? fopen(path.c_str(), "w") : NULL;
    ok = f && PEM_write_PrivateKey(f, pkey, NULL, NULL, 0, NULL, NULL) == 1;
    if (f) {
        fclose(f);
    }
    EVP_PKEY_free(pkey);
    return ok;
}

// A browser's ClientHello, and its key exchange flight, arrive in several TCP segments,
// such as a ClientHello over 1 KB with a post-quantum key share, or a slow mobile link.
// The server keeps reading until each flight is whole, completes the handshake, and reads
// the request, whose TLS record is split too.
VOID TEST(ProtocolConnTest, HttpsClientFlightsInSmallSegments_HandshakeCompletes)
{
    srs_error_t err = srs_success;

    TlsClientPeer client(false);
    client.segment_ = 50;
    SrsSslConnection ssl(&client);
    HELPER_ASSERT_SUCCESS(ssl.handshake(tls_server_key, tls_server_crt));
    // Both client flights took more than one read each.
    EXPECT_GT(client.nn_reads_, 4);

    string req = "GET /api/v1/versions HTTP/1.1\r\nHost: localhost:1990\r\n\r\n";
    client.send(req);
    EXPECT_TRUE(SSL_is_init_finished(client.ssl_));

    char buf[1024];
    ssize_t nread = 0;
    HELPER_ASSERT_SUCCESS(ssl.read(buf, sizeof(buf), &nread));
    EXPECT_EQ(req, string(buf, nread));
}

// A browser or curl that does not trust the server's self-signed certificate aborts the
// handshake with a TLS alert after the ServerHello. The server fails the handshake as a
// TLS error, and the connection ends.
VOID TEST(ProtocolConnTest, HttpsClientRejectsSelfSignedCert_HandshakeFails)
{
    srs_error_t err = srs_success;

    TlsClientPeer client(true);
    SrsSslConnection ssl(&client);
    err = ssl.handshake(tls_server_key, tls_server_crt);
    EXPECT_EQ(ERROR_TLS_HANDSHAKE, srs_error_code(err));
    srs_freep(err);

    EXPECT_NE(X509_V_OK, SSL_get_verify_result(client.ssl_));
    EXPECT_FALSE(SSL_is_init_finished(client.ssl_));
}

// A certificate monitor reads the server's certificate from the ServerHello and closes
// the TCP connection with no alert. The server's read for the client's key exchange
// fails as a client close.
VOID TEST(ProtocolConnTest, CertMonitorClosesAfterServerHello_HandshakeReadFails)
{
    srs_error_t err = srs_success;

    TlsClientPeer client(false);
    client.hangup_after_writes_ = 1;
    SrsSslConnection ssl(&client);
    err = ssl.handshake(tls_server_key, tls_server_crt);
    EXPECT_EQ(ERROR_SOCKET_READ, srs_error_code(err));
    EXPECT_TRUE(srs_is_client_gracefully_close(err));
    srs_freep(err);

    // The client got the ServerHello and stopped there.
    EXPECT_EQ(1, client.nn_writes_);
}

// A port scanner sends a ClientHello and resets the connection, or a client goes away
// before the server's last flight. The server's write fails as a client close, at
// either point.
VOID TEST(ProtocolConnTest, ClientResetsDuringHandshake_WriteFails)
{
    srs_error_t err = srs_success;

    // The server's first write is the ServerHello flight, the second its Finished.
    for (int fail_write = 1; fail_write <= 2; fail_write++) {
        TlsClientPeer client(false);
        client.fail_write_ = fail_write;
        SrsSslConnection ssl(&client);
        err = ssl.handshake(tls_server_key, tls_server_crt);
        EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err)) << "write=" << fail_write;
        EXPECT_TRUE(srs_is_client_gracefully_close(err));
        srs_freep(err);
        EXPECT_EQ(fail_write, client.nn_writes_);
    }
}

// The operator's https_api, http_server https or rtmps section names a key file that
// does not exist. Each client's handshake is refused before reading anything, as a key
// or certificate error.
VOID TEST(ProtocolConnTest, OperatorKeyFileMissing_HandshakeRefused)
{
    srs_error_t err = srs_success;

    TlsClientPeer client(false);
    SrsSslConnection ssl(&client);
    string key = srs_utest_tmp_dir() + "/srs_utest_ai41_no_such.key";
    err = ssl.handshake(key, tls_server_crt);
    EXPECT_EQ(ERROR_TLS_KEY_CRT, srs_error_code(err));
    srs_freep(err);

    EXPECT_EQ(0, client.nn_reads_);
}

// The operator renews the certificate but keeps the old key, so the key is not the
// certificate's. Each client's handshake is refused as a key or certificate error.
VOID TEST(ProtocolConnTest, OperatorKeyNotMatchingCert_HandshakeRefused)
{
    srs_error_t err = srs_success;

    string key = srs_utest_tmp_dir() + "/srs_utest_ai41_other_" + srs_strconv_format_int(getpid()) + ".key";
    ASSERT_TRUE(tls_write_other_rsa_key(key));

    TlsClientPeer client(false);
    SrsSslConnection ssl(&client);
    err = ssl.handshake(key, tls_server_crt);
    EXPECT_EQ(ERROR_TLS_KEY_CRT, srs_error_code(err));
    srs_freep(err);
    unlink(key.c_str());

    EXPECT_EQ(0, client.nn_reads_);
}

// An HTTPS client sends a request, then closes the connection with a TLS close_notify.
// The server reads the request, and its next read fails as a client close, so the HTTP
// connection ends quietly.
VOID TEST(ProtocolConnTest, HttpsClientSendsCloseNotify_ReadEndsAsClientClose)
{
    srs_error_t err = srs_success;

    TlsClientPeer client(false);
    SrsSslConnection ssl(&client);
    HELPER_ASSERT_SUCCESS(ssl.handshake(tls_server_key, tls_server_crt));

    string req = "GET /api/v1/summaries HTTP/1.1\r\nHost: localhost:1990\r\n\r\n";
    client.send(req);
    client.close_notify();

    char buf[1024];
    ssize_t nread = 0;
    HELPER_ASSERT_SUCCESS(ssl.read(buf, sizeof(buf), &nread));
    EXPECT_EQ(req, string(buf, nread));

    err = ssl.read(buf, sizeof(buf), &nread);
    EXPECT_EQ(ERROR_SOCKET_READ, srs_error_code(err));
    EXPECT_TRUE(srs_is_client_gracefully_close(err));
    srs_freep(err);
}

// An HTTPS client, such as a browser closing a preconnected socket, closes the TCP
// connection with no close_notify while the server waits for its request. The read fails
// as a client close.
VOID TEST(ProtocolConnTest, HttpsClientDropsTcpWithoutCloseNotify_ReadEndsAsClientClose)
{
    srs_error_t err = srs_success;

    TlsClientPeer client(false);
    SrsSslConnection ssl(&client);
    HELPER_ASSERT_SUCCESS(ssl.handshake(tls_server_key, tls_server_crt));
    client.hangup_after_writes_ = client.nn_writes_;

    char buf[1024];
    ssize_t nread = 0;
    err = ssl.read(buf, sizeof(buf), &nread);
    EXPECT_EQ(ERROR_SOCKET_READ, srs_error_code(err));
    EXPECT_TRUE(srs_is_client_gracefully_close(err));
    srs_freep(err);
}

// An RTMPS encoder completes TLS, then the RTMP handshake inside it: it writes C0 and C1
// as two TLS records, and C2 once it has S0S1S2. The server reads C0C1 and C2 whole over
// TLS and answers the 3073 bytes of S0S1S2.
VOID TEST(ProtocolConnTest, RtmpsEncoderHandshakeOverTls_Completes)
{
    srs_error_t err = srs_success;

    TlsClientPeer client(false);
    SrsSslConnection ssl(&client);
    HELPER_ASSERT_SUCCESS(ssl.handshake(tls_server_key, tls_server_crt));

    client.send(string("\x03", 1));
    client.send(string(1536, 0));
    client.replies_.push_back(make_pair((size_t)3073, string(1536, 0)));

    SrsRtmpServer rtmp(&ssl);
    HELPER_ASSERT_SUCCESS(rtmp.handshake());

    string got = client.received();
    ASSERT_EQ(3073u, got.size());
    EXPECT_EQ(0x03, got[0]);
    // The encoder's C2 was sent and read.
    EXPECT_TRUE(client.replies_.empty());
    EXPECT_EQ(0, (int)BIO_ctrl_pending(client.out_));
}

// An HTTPS-FLV player gets the live stream as a chunked response: the status line and
// headers, then the FLV header as one chunk written from an iovec. The player decrypts
// both.
VOID TEST(ProtocolConnTest, HttpsFlvPlayerGetsChunkedStream)
{
    srs_error_t err = srs_success;

    TlsClientPeer client(false);
    SrsSslConnection ssl(&client);
    HELPER_ASSERT_SUCCESS(ssl.handshake(tls_server_key, tls_server_crt));

    SrsHttpResponseWriter w(&ssl);
    w.header()->set_content_type("video/x-flv");
    w.write_header(SRS_CONSTS_HTTP_OK);

    // FLV header with audio and video, then PreviousTagSize0.
    char flv[] = {'F', 'L', 'V', 0x01, 0x05, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00};
    iovec iov[2];
    iov[0].iov_base = flv;
    iov[0].iov_len = 9;
    iov[1].iov_base = flv + 9;
    iov[1].iov_len = 4;
    HELPER_ASSERT_SUCCESS(w.writev(iov, 2, NULL));

    string got = client.received();
    EXPECT_EQ(0u, got.find("HTTP/1.1 200 OK\r\n"));
    EXPECT_NE(string::npos, got.find("Transfer-Encoding: chunked\r\n"));
    string chunk = string("\r\n\r\nd\r\n") + string(flv, sizeof(flv)) + "\r\n";
    EXPECT_NE(string::npos, got.find(chunk));
}

// An HTTPS-FLV player closes its connection while the server sends the stream. The
// server's TLS write fails as a client close, so the player's connection ends.
VOID TEST(ProtocolConnTest, HttpsFlvPlayerLeaves_WriteEndsAsClientClose)
{
    srs_error_t err = srs_success;

    TlsClientPeer client(false);
    SrsSslConnection ssl(&client);
    HELPER_ASSERT_SUCCESS(ssl.handshake(tls_server_key, tls_server_crt));
    client.fail_write_ = client.nn_writes_ + 1;

    SrsHttpResponseWriter w(&ssl);
    w.header()->set_content_type("video/x-flv");
    w.write_header(SRS_CONSTS_HTTP_OK);

    char flv[] = {'F', 'L', 'V', 0x01, 0x05, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00};
    iovec iov[1];
    iov[0].iov_base = flv;
    iov[0].iov_len = sizeof(flv);
    err = w.writev(iov, 1, NULL);
    EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
    EXPECT_TRUE(srs_is_client_gracefully_close(err));
    srs_freep(err);
}

// An HTTPS server on the other end of the server's HTTP client, such as an HTTP hook,
// heartbeat or HLS notify backend, or the origin of an HTTPS-FLV edge. It runs OpenSSL's
// server over memory BIOs: what the client writes is the server's input, and what the
// server sends is what the client reads. The server acts each time the client reads.
class TlsServerPeer : public ISrsProtocolReadWriter
{
public:
    SSL_CTX *ctx_;
    SSL *ssl_;
    // The bytes from the client, and the bytes to the client.
    BIO *in_;
    BIO *out_;
    // The client's write that fails, counted from 1, as a server gone; 0 for none.
    int fail_write_;
    // The server closes its TCP connection, with no TLS alert, once the client has
    // written this many times; -1 for never.
    int hangup_after_writes_;
    // A network device flips the last byte of the next record the server sends after
    // its handshake is done, such as its Finished or a response.
    bool corrupt_;
    bool corrupted_;
    // A network device flips the last byte of the client's next write, such as its request.
    bool corrupt_request_;
    int nn_writes_;
    // The plaintext the server got, and its reply once it has a whole request header.
    string received_;
    string response_;
    bool responded_;
    // Send a close_notify alert after the response, as a server closing the connection.
    bool close_after_response_;
    srs_utime_t rtm_;
    srs_utime_t stm_;
    int64_t nn_recv_;
    int64_t nn_send_;

public:
    TlsServerPeer()
    {
        ctx_ = SSL_CTX_new(TLS_server_method());
        SSL_CTX_use_certificate_chain_file(ctx_, tls_server_crt);
        SSL_CTX_use_PrivateKey_file(ctx_, tls_server_key, SSL_FILETYPE_PEM);
        ssl_ = SSL_new(ctx_);
        in_ = BIO_new(BIO_s_mem());
        out_ = BIO_new(BIO_s_mem());
        SSL_set_bio(ssl_, in_, out_);
        SSL_set_accept_state(ssl_);

        fail_write_ = 0;
        hangup_after_writes_ = -1;
        corrupt_ = corrupted_ = false;
        corrupt_request_ = false;
        nn_writes_ = 0;
        responded_ = false;
        close_after_response_ = false;
        rtm_ = stm_ = SRS_UTIME_NO_TIMEOUT;
        nn_recv_ = nn_send_ = 0;
    }
    virtual ~TlsServerPeer()
    {
        SSL_free(ssl_);
        SSL_CTX_free(ctx_);
    }

public:
    bool hung_up()
    {
        return hangup_after_writes_ >= 0 && nn_writes_ >= hangup_after_writes_;
    }
    // The SNI the client sent, empty if none.
    string servername()
    {
        const char *name = SSL_get_servername(ssl_, TLSEXT_NAMETYPE_host_name);
        return name ? name : "";
    }
    // Take what the client sent, and send what the server has to.
    void drive()
    {
        if (hung_up()) {
            return;
        }
        if (!SSL_is_init_finished(ssl_)) {
            SSL_do_handshake(ssl_);
            ERR_clear_error();
        }
        if (!SSL_is_init_finished(ssl_)) {
            return;
        }

        char buf[4096];
        int r0;
        while ((r0 = SSL_read(ssl_, buf, sizeof(buf))) > 0) {
            received_.append(buf, r0);
        }
        ERR_clear_error();

        if (!responded_ && !response_.empty() && received_.find("\r\n\r\n") != string::npos) {
            responded_ = true;
            SSL_write(ssl_, response_.data(), (int)response_.size());
            if (close_after_response_) {
                SSL_shutdown(ssl_);
                ERR_clear_error();
            }
        }

        char *data = NULL;
        long size = BIO_get_mem_data(out_, &data);
        if (corrupt_ && !corrupted_ && size > 0) {
            corrupted_ = true;
            data[size - 1] ^= 0xff;
        }
    }

public:
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread)
    {
        drive();

        int pending = hung_up() ? 0 : (int)BIO_ctrl_pending(out_);
        if (pending <= 0) {
            return srs_error_new(ERROR_SOCKET_READ, "EOF");
        }

        int n = srs_min(pending, (int)size);
        BIO_read(out_, buf, n);
        nn_recv_ += n;
        if (nread) {
            *nread = n;
        }
        return srs_success;
    }
    virtual srs_error_t read_fully(void *buf, size_t size, ssize_t *nread)
    {
        srs_error_t err = srs_success;
        size_t nb = 0;
        while (nb < size) {
            ssize_t n = 0;
            if ((err = read((char *)buf + nb, size - nb, &n)) != srs_success) {
                return err;
            }
            nb += n;
        }
        if (nread) {
            *nread = nb;
        }
        return err;
    }
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite)
    {
        nn_writes_++;
        if (nn_writes_ == fail_write_) {
            return srs_error_new(ERROR_SOCKET_WRITE, "server gone");
        }
        string bytes((char *)buf, size);
        if (corrupt_request_ && size > 0) {
            corrupt_request_ = false;
            bytes[size - 1] ^= 0xff;
        }
        BIO_write(in_, bytes.data(), (int)size);
        nn_send_ += size;
        if (nwrite) {
            *nwrite = size;
        }
        return srs_success;
    }
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t *nwrite)
    {
        srs_error_t err = srs_success;
        ssize_t total = 0;
        for (int i = 0; i < iov_size; i++) {
            if ((err = write(iov[i].iov_base, iov[i].iov_len, NULL)) != srs_success) {
                return err;
            }
            total += iov[i].iov_len;
        }
        if (nwrite) {
            *nwrite = total;
        }
        return err;
    }
    virtual void set_recv_timeout(srs_utime_t tm)
    {
        rtm_ = tm;
    }
    virtual srs_utime_t get_recv_timeout()
    {
        return rtm_;
    }
    virtual void set_send_timeout(srs_utime_t tm)
    {
        stm_ = tm;
    }
    virtual srs_utime_t get_send_timeout()
    {
        return stm_;
    }
    virtual int64_t get_recv_bytes()
    {
        return nn_recv_;
    }
    virtual int64_t get_send_bytes()
    {
        return nn_send_;
    }
};

static const char *https_hook_request = "POST /api/v1/streams HTTP/1.1\r\nHost: hooks.example.com\r\nContent-Length: 0\r\n\r\n";
static const char *https_hook_response = "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\n0";

// An HTTP hook, heartbeat or HLS notify URL names its backend by a domain name, such as
// https://hooks.example.com/api/v1/streams behind nginx or a CDN that serves many names
// on one address. The client sends the name as SNI, so the backend picks the right
// certificate; a backend named by its IP address gets no SNI.
VOID TEST(ProtocolHttpClientTest, HttpsHookByDomainName_SendsSni)
{
    srs_error_t err = srs_success;

    if (true) {
        TlsServerPeer server;
        server.response_ = https_hook_response;
        SrsSslClient ssl(&server);
        HELPER_ASSERT_SUCCESS(ssl.handshake("hooks.example.com"));
        EXPECT_EQ("hooks.example.com", server.servername());

        HELPER_ASSERT_SUCCESS(ssl.write((void *)https_hook_request, strlen(https_hook_request), NULL));

        char buf[1024];
        ssize_t nread = 0;
        HELPER_ASSERT_SUCCESS(ssl.read(buf, sizeof(buf), &nread));
        EXPECT_EQ(https_hook_request, server.received_);
        EXPECT_EQ(https_hook_response, string(buf, nread));
    }

    if (true) {
        TlsServerPeer server;
        SrsSslClient ssl(&server);
        HELPER_ASSERT_SUCCESS(ssl.handshake("10.0.0.8"));
        EXPECT_EQ("", server.servername());
    }
}

// The backend resets the connection while the client does its handshake: right after
// the TCP connect, such as a backend at its connection limit, or after its ServerHello,
// such as a proxy that refuses the client. The handshake fails with the write error.
VOID TEST(ProtocolHttpClientTest, HttpsBackendResetsDuringHandshake_WriteFails)
{
    srs_error_t err = srs_success;

    // The client's first write is its ClientHello, the second its key exchange flight.
    for (int fail_write = 1; fail_write <= 2; fail_write++) {
        TlsServerPeer server;
        server.fail_write_ = fail_write;
        SrsSslClient ssl(&server);
        err = ssl.handshake("hooks.example.com");
        EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err)) << "write=" << fail_write;
        srs_freep(err);
        EXPECT_EQ(fail_write, server.nn_writes_);
    }
}

// The backend closes the TCP connection after it got the client's key exchange, with no
// Finished, such as a backend that crashed or a load balancer that timed out. The
// client's read for the Finished fails, so the handshake fails.
VOID TEST(ProtocolHttpClientTest, HttpsBackendClosesBeforeFinished_HandshakeReadFails)
{
    srs_error_t err = srs_success;

    TlsServerPeer server;
    server.hangup_after_writes_ = 2;
    SrsSslClient ssl(&server);
    err = ssl.handshake("hooks.example.com");
    EXPECT_EQ(ERROR_SOCKET_READ, srs_error_code(err));
    srs_freep(err);

    // The server got both client flights and never finished.
    EXPECT_EQ(2, server.nn_writes_);
    EXPECT_FALSE(SSL_is_init_finished(server.ssl_));
}

// A network device corrupts the backend's Finished on the way. The client cannot verify
// it, so the handshake fails as a TLS error instead of trusting the connection.
VOID TEST(ProtocolHttpClientTest, HttpsBackendFinishedCorrupted_HandshakeFails)
{
    srs_error_t err = srs_success;

    TlsServerPeer server;
    server.corrupt_ = true;
    SrsSslClient ssl(&server);
    err = ssl.handshake("hooks.example.com");
    EXPECT_EQ(ERROR_TLS_HANDSHAKE, srs_error_code(err));
    srs_freep(err);

    // The server finished its side; only the client refused.
    EXPECT_TRUE(server.corrupted_);
    EXPECT_TRUE(SSL_is_init_finished(server.ssl_));
}

// A network device corrupts the backend's response record. The client cannot verify it,
// so the read fails as a TLS error and the hook gets no response.
VOID TEST(ProtocolHttpClientTest, HttpsHookResponseCorrupted_ReadFails)
{
    srs_error_t err = srs_success;

    TlsServerPeer server;
    server.response_ = https_hook_response;
    SrsSslClient ssl(&server);
    HELPER_ASSERT_SUCCESS(ssl.handshake("hooks.example.com"));
    server.corrupt_ = true;

    HELPER_ASSERT_SUCCESS(ssl.write((void *)https_hook_request, strlen(https_hook_request), NULL));

    char buf[1024];
    ssize_t nread = 0;
    err = ssl.read(buf, sizeof(buf), &nread);
    EXPECT_EQ(ERROR_TLS_READ, srs_error_code(err));
    srs_freep(err);
    EXPECT_TRUE(server.corrupted_);
    EXPECT_EQ(0, nread);
}

// The backend answers and closes its side with a close_notify alert, as a server with
// Connection: close does. The client reads the response, then its next read ends as a
// socket read error.
VOID TEST(ProtocolHttpClientTest, HttpsBackendSendsCloseNotify_ReadEnds)
{
    srs_error_t err = srs_success;

    TlsServerPeer server;
    server.response_ = https_hook_response;
    server.close_after_response_ = true;
    SrsSslClient ssl(&server);
    HELPER_ASSERT_SUCCESS(ssl.handshake("hooks.example.com"));
    HELPER_ASSERT_SUCCESS(ssl.write((void *)https_hook_request, strlen(https_hook_request), NULL));

    char buf[1024];
    ssize_t nread = 0;
    HELPER_ASSERT_SUCCESS(ssl.read(buf, sizeof(buf), &nread));
    EXPECT_EQ(https_hook_response, string(buf, nread));

    err = ssl.read(buf, sizeof(buf), &nread);
    EXPECT_EQ(ERROR_SOCKET_READ, srs_error_code(err));
    srs_freep(err);
}

// The backend resets the connection while the client sends its request after the
// handshake. The write fails with the socket error, so the hook fails.
VOID TEST(ProtocolHttpClientTest, HttpsBackendResetsDuringRequest_WriteFails)
{
    srs_error_t err = srs_success;

    TlsServerPeer server;
    SrsSslClient ssl(&server);
    HELPER_ASSERT_SUCCESS(ssl.handshake("hooks.example.com"));
    server.fail_write_ = server.nn_writes_ + 1;

    err = ssl.write((void *)https_hook_request, strlen(https_hook_request), NULL);
    EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
    srs_freep(err);

    // The server never got the request.
    server.drive();
    EXPECT_EQ("", server.received_);
}

// A network device corrupts the client's request on the way. The backend cannot verify
// the record, so it refuses it with a fatal bad_record_mac alert and the session is dead.
// The client's read of the response fails as a TLS error, and a retry of the request on
// the same connection fails at the write, rather than sending on the dead session.
VOID TEST(ProtocolHttpClientTest, HttpsBackendRefusesCorruptedRequest_RetryWriteFails)
{
    srs_error_t err = srs_success;

    TlsServerPeer server;
    server.response_ = https_hook_response;
    SrsSslClient ssl(&server);
    HELPER_ASSERT_SUCCESS(ssl.handshake("hooks.example.com"));
    server.corrupt_request_ = true;

    HELPER_ASSERT_SUCCESS(ssl.write((void *)https_hook_request, strlen(https_hook_request), NULL));

    char buf[1024];
    ssize_t nread = 0;
    err = ssl.read(buf, sizeof(buf), &nread);
    EXPECT_EQ(ERROR_TLS_READ, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(0, nread);

    // The server refused the request and never answered.
    EXPECT_EQ("", server.received_);
    EXPECT_FALSE(server.responded_);

    // The retry is not sent: the server gets no more bytes.
    int nn_writes = server.nn_writes_;
    err = ssl.write((void *)https_hook_request, strlen(https_hook_request), NULL);
    EXPECT_EQ(ERROR_TLS_WRITE, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(nn_writes, server.nn_writes_);
}

// A TCP server that reads the client's request, sends a fixed reply, which may be empty,
// and closes the connection.
class RawReplyTcpServer : public SrsHttpTestServer
{
public:
    string reply_;
    string request_;
    string error_;

public:
    RawReplyTcpServer(string reply) : SrsHttpTestServer("")
    {
        reply_ = reply;
    }

public:
    virtual srs_error_t start()
    {
        srs_error_t err = SrsHttpTestServer::start();
        // Let State Threads run once, so its clock is fresh before the server sets its
        // read timeout. After a long run of tests that never yield, the clock is seconds
        // behind and the timeout would fire at once.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        return err;
    }
    virtual srs_error_t do_cycle(srs_netfd_t cfd)
    {
        srs_error_t err = srs_success;

        SrsStSocket skt(cfd);
        skt.set_recv_timeout(1 * SRS_UTIME_SECONDS);
        skt.set_send_timeout(1 * SRS_UTIME_SECONDS);

        char buf[1024];
        ssize_t nn = 0;
        if ((err = skt.read(buf, sizeof(buf), &nn)) != srs_success) {
            error_ = srs_error_desc(err);
            return err;
        }
        request_.assign(buf, nn);

        if (!reply_.empty()) {
            return skt.write((void *)reply_.data(), reply_.size(), NULL);
        }
        return err;
    }
};

// The operator's on_publish hook URL names the port of a service that is not HTTP, such
// as an SSH server, by mistake. The client sends the request, cannot parse the reply as
// an HTTP response, and the hook fails.
VOID TEST(ProtocolHttpClientTest, HookUrlAtNonHttpService_PostFails)
{
    srs_error_t err = srs_success;

    RawReplyTcpServer server("SSH-2.0-OpenSSH_9.6\r\n");
    HELPER_ASSERT_SUCCESS(server.start());

    SrsHttpClient client;
    HELPER_ASSERT_SUCCESS(client.initialize("http", "127.0.0.1", server.get_port(), 3 * SRS_UTIME_SECONDS));

    ISrsHttpMessage *msg = NULL;
    err = client.post("/api/v1/streams", "{}", &msg);
    EXPECT_EQ(ERROR_HTTP_PARSE_HEADER, srs_error_code(err)) << srs_error_desc(err) << "\nserver: " << server.error_;
    srs_freep(err);
    EXPECT_TRUE(msg == NULL);

    EXPECT_EQ(0u, server.request_.find("POST /api/v1/streams HTTP/1.1\r\n")) << server.request_;
}

// The HLS notify backend, or the origin of an HTTPS-FLV edge, accepts the connection and
// closes it without a reply, such as a backend that crashed on the request. The client's
// GET fails reading the response.
VOID TEST(ProtocolHttpClientTest, NotifyBackendClosesWithoutReply_GetFails)
{
    srs_error_t err = srs_success;

    RawReplyTcpServer server("");
    HELPER_ASSERT_SUCCESS(server.start());

    SrsHttpClient client;
    HELPER_ASSERT_SUCCESS(client.initialize("http", "127.0.0.1", server.get_port(), 3 * SRS_UTIME_SECONDS));

    ISrsHttpMessage *msg = NULL;
    err = client.get("/live/livestream.ts", "", &msg);
    EXPECT_EQ(ERROR_SOCKET_READ, srs_error_code(err));
    srs_freep(err);
    EXPECT_TRUE(msg == NULL);

    EXPECT_EQ(0u, server.request_.find("GET /live/livestream.ts HTTP/1.1\r\n")) << server.request_;
}

// A TCP connection to a backend that resets it before the client sends anything.
class ResetBeforeRequestTcpClient : public SrsTcpClient
{
public:
    ResetBeforeRequestTcpClient() : SrsTcpClient("127.0.0.1", 8085, SRS_UTIME_NO_TIMEOUT)
    {
    }

public:
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite)
    {
        return srs_error_new(ERROR_SOCKET_WRITE, "connection reset by peer");
    }
};

// The backend accepts the TCP connection and resets it before the request is sent, such
// as a backend at its connection limit. The POST of a hook or heartbeat, and the GET of
// an HLS notify or edge pull, fail with the write error, and the client drops the
// connection, so it does not reuse a dead one.
VOID TEST(ProtocolHttpClientTest, BackendResetsBeforeRequest_ConnectionDropped)
{
    srs_error_t err = srs_success;

    for (int i = 0; i < 2; i++) {
        bool is_post = (i == 0);
        SrsHttpClient client;
        HELPER_ASSERT_SUCCESS(client.initialize("http", "127.0.0.1", 8085, 3 * SRS_UTIME_SECONDS));
        // The connection is already up, as connect() left it.
        client.transport_ = new ResetBeforeRequestTcpClient();

        ISrsHttpMessage *msg = NULL;
        if (is_post) {
            err = client.post("/api/v1/streams", "{}", &msg);
        } else {
            err = client.get("/live/livestream.ts", "", &msg);
        }
        EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err)) << (is_post ? "post" : "get");
        srs_freep(err);
        EXPECT_TRUE(msg == NULL);
        EXPECT_TRUE(client.transport_ == NULL);
    }
}

// Parse one HTTP request or response the way the HTTP server and client do, from the
// bytes a peer sent. The message reads its body from the parser's buffer, so the parser
// must outlive it.
static srs_error_t http_conn_parse(SrsHttpParser *hp, MockBufferIO *io, llhttp_type type, bool jsonp, SrsHttpMessage **pmsg)
{
    srs_error_t err = hp->initialize(type);
    if (err != srs_success) {
        return err;
    }
    hp->set_jsonp(jsonp);

    ISrsHttpMessage *msg = NULL;
    err = hp->parse_message(io, &msg);
    *pmsg = dynamic_cast<SrsHttpMessage *>(msg);
    return err;
}

// An HTTP-FLV player on the default vhost, which the server names __defaultVhost__. The
// request keeps the vhost for the source lookup, but its host is the address the player
// asked for, which the hooks and the stream stats report.
VOID TEST(ProtocolHttpConnTest, HttpFlvPlayerOnDefaultVhost_RequestHostIsHttpHost)
{
    srs_error_t err = srs_success;

    MockBufferIO io;
    io.append("GET /live/livestream.flv?token=abc HTTP/1.1\r\nHost: 192.168.1.10:8080\r\nUser-Agent: ffplay\r\n\r\n");
    SrsHttpParser hp;
    SrsHttpMessage *msg_raw = NULL;
    HELPER_ASSERT_SUCCESS(http_conn_parse(&hp, &io, HTTP_REQUEST, false, &msg_raw));
    SrsUniquePtr<SrsHttpMessage> msg(msg_raw);

    SrsUniquePtr<ISrsRequest> req(msg->to_request(SRS_CONSTS_RTMP_DEFAULT_VHOST));
    EXPECT_STREQ(SRS_CONSTS_RTMP_DEFAULT_VHOST, req->vhost_.c_str());
    EXPECT_STREQ("192.168.1.10", req->host_.c_str());
    EXPECT_STREQ("live", req->app_.c_str());
    EXPECT_STREQ("livestream", req->stream_.c_str());
    EXPECT_STREQ("?token=abc", req->param_.c_str());
    EXPECT_STREQ("flv", req->protocol_.c_str());
}

// The SRS console kicks a client off. A browser without CORS sends it as a JSONP GET with
// method=DELETE, and other API clients as a plain DELETE; both are a DELETE to the clients
// API, which kicks the client only for a DELETE.
VOID TEST(ProtocolHttpConnTest, ConsoleKicksClientWithJsonpDelete_MethodIsDelete)
{
    srs_error_t err = srs_success;

    if (true) {
        MockBufferIO io;
        io.append("GET /api/v1/clients/k3d1x?callback=jQuery_1&method=DELETE HTTP/1.1\r\nHost: 127.0.0.1:1985\r\n\r\n");
        SrsHttpParser hp;
        SrsHttpMessage *msg_raw = NULL;
        HELPER_ASSERT_SUCCESS(http_conn_parse(&hp, &io, HTTP_REQUEST, true, &msg_raw));
        SrsUniquePtr<SrsHttpMessage> msg(msg_raw);

        EXPECT_TRUE(msg->is_jsonp());
        EXPECT_TRUE(msg->is_http_delete());
        EXPECT_FALSE(msg->is_http_get());
        EXPECT_STREQ("DELETE", msg->method_str().c_str());
        EXPECT_STREQ("k3d1x", msg->parse_rest_id("/api/v1/clients/").c_str());
    }

    if (true) {
        MockBufferIO io;
        io.append("DELETE /api/v1/clients/k3d1x HTTP/1.1\r\nHost: 127.0.0.1:1985\r\n\r\n");
        SrsHttpParser hp;
        SrsHttpMessage *msg_raw = NULL;
        HELPER_ASSERT_SUCCESS(http_conn_parse(&hp, &io, HTTP_REQUEST, true, &msg_raw));
        SrsUniquePtr<SrsHttpMessage> msg(msg_raw);

        EXPECT_FALSE(msg->is_jsonp());
        EXPECT_TRUE(msg->is_http_delete());
        EXPECT_FALSE(msg->is_http_get());
    }
}

// A client asks for the server's options as a whole with "OPTIONS *" (the asterisk form of
// RFC 9112), as some HTTP tools and proxies probe a server. The request target has no
// leading slash, so the server puts one before it when it builds the URL from the Host.
VOID TEST(ProtocolHttpConnTest, ServerWideOptionsAsterisk_UrlBuiltFromHost)
{
    srs_error_t err = srs_success;

    MockBufferIO io;
    io.append("OPTIONS * HTTP/1.1\r\nHost: 192.168.1.10:8080\r\n\r\n");
    SrsHttpParser hp;
    SrsHttpMessage *msg_raw = NULL;
    HELPER_ASSERT_SUCCESS(http_conn_parse(&hp, &io, HTTP_REQUEST, false, &msg_raw));
    SrsUniquePtr<SrsHttpMessage> msg(msg_raw);

    EXPECT_TRUE(msg->is_http_options());
    EXPECT_STREQ("192.168.1.10", msg->host().c_str());
    EXPECT_EQ(8080, msg->port());
    EXPECT_STREQ("/*", msg->path().c_str());
    EXPECT_STREQ("http://192.168.1.10:8080/*", msg->url().c_str());
}

// A browser opens the SRS WHEP player page with the WHEP endpoint in its query, as the page's
// "?url=xxx to set the whole WHIP/WHEP url directly" (research/players/js/srs.page.js) asks:
// players/whep.html?url=http://host:1985/rtc/v1/whep/?app=live&stream=livestream. Browsers send
// ':' and '/' of a query as they are, so the server must still serve the page's path.
VOID TEST(ProtocolHttpConnTest, DISABLED_WhepPageWithEndpointUrlInQuery_ServesQueryPath)
{
    srs_error_t err = srs_success;

    MockBufferIO io;
    io.append("GET /players/whep.html?url=http://192.168.1.10:1985/rtc/v1/whep/?app=live&stream=livestream HTTP/1.1\r\n"
              "Host: 192.168.1.10:8080\r\n\r\n");
    SrsHttpParser hp;
    SrsHttpMessage *msg_raw = NULL;
    HELPER_ASSERT_SUCCESS(http_conn_parse(&hp, &io, HTTP_REQUEST, false, &msg_raw));
    SrsUniquePtr<SrsHttpMessage> msg(msg_raw);

    EXPECT_STREQ("/players/whep.html", msg->path().c_str());
    EXPECT_STREQ(".html", msg->ext().c_str());
    EXPECT_STREQ("192.168.1.10", msg->host().c_str());
    EXPECT_EQ(8080, msg->port());
    EXPECT_STREQ("http://192.168.1.10:1985/rtc/v1/whep/?app=live", msg->query_get("url").c_str());
}

// A hook backend answers a callback with an empty body and "Content-Length: 0", as many web
// frameworks do for a bare 200. The body reads as empty at once, without waiting on the
// keep-alive connection for bytes that never come.
VOID TEST(ProtocolHttpConnTest, HookBackendRepliesContentLengthZero_BodyEmpty)
{
    srs_error_t err = srs_success;

    MockBufferIO io;
    io.append("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: keep-alive\r\n\r\n");
    SrsHttpParser hp;
    SrsHttpMessage *msg_raw = NULL;
    HELPER_ASSERT_SUCCESS(http_conn_parse(&hp, &io, HTTP_RESPONSE, false, &msg_raw));
    SrsUniquePtr<SrsHttpMessage> msg(msg_raw);

    EXPECT_EQ(200, msg->status_code());
    EXPECT_EQ(0, msg->content_length());

    // The mock fails a read on an empty socket, so a read here would fail the body.
    string body;
    HELPER_EXPECT_SUCCESS(msg->body_read_all(body));
    EXPECT_TRUE(body.empty());
    EXPECT_TRUE(msg->body_reader()->eof());
}

// Read a chunked body the way the hook client and the HTTP-FLV edge do: read until EOF or an
// error, keeping what came before the error.
static srs_error_t http_conn_read_body(ISrsHttpMessage *msg, string &body)
{
    srs_error_t err = srs_success;
    ISrsHttpResponseReader *br = msg->body_reader();
    while (!br->eof()) {
        char buf[64];
        ssize_t nn = 0;
        if ((err = br->read(buf, sizeof(buf), &nn)) != srs_success) {
            return err;
        }
        body.append(buf, nn);
    }
    return err;
}

// A backend that declares a chunked body but sends a blank line where a chunk size must be,
// such as a broken proxy that drops the size line. The body is refused, not read as data.
VOID TEST(ProtocolHttpConnTest, ChunkedBodyWithEmptySizeLine_Refused)
{
    srs_error_t err = srs_success;

    MockBufferIO io;
    io.append("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n\r\nHello\r\n0\r\n\r\n");
    SrsHttpParser hp;
    SrsHttpMessage *msg_raw = NULL;
    HELPER_ASSERT_SUCCESS(http_conn_parse(&hp, &io, HTTP_RESPONSE, false, &msg_raw));
    SrsUniquePtr<SrsHttpMessage> msg(msg_raw);
    EXPECT_TRUE(msg->is_chunked());

    string body;
    err = http_conn_read_body(msg.get(), body);
    EXPECT_EQ(ERROR_HTTP_INVALID_CHUNK_HEADER, srs_error_code(err));
    srs_freep(err);
    EXPECT_TRUE(body.empty());
}

// A backend that declares a chunked body but sends the data with no chunk framing, or a
// negative size. Neither is a hex chunk size, so the body is refused.
VOID TEST(ProtocolHttpConnTest, ChunkedBodyWithNonHexSize_Refused)
{
    srs_error_t err = srs_success;

    const char *bodies[] = {"{\"code\":0}\r\n", "-5\r\nHello\r\n0\r\n\r\n"};
    for (int i = 0; i < 2; i++) {
        MockBufferIO io;
        io.append(string("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n") + bodies[i]);
        SrsHttpParser hp;
        SrsHttpMessage *msg_raw = NULL;
        HELPER_ASSERT_SUCCESS(http_conn_parse(&hp, &io, HTTP_RESPONSE, false, &msg_raw));
        SrsUniquePtr<SrsHttpMessage> msg(msg_raw);

        string body;
        err = http_conn_read_body(msg.get(), body);
        EXPECT_EQ(ERROR_HTTP_INVALID_CHUNK_HEADER, srs_error_code(err)) << bodies[i];
        srs_freep(err);
        EXPECT_TRUE(body.empty()) << bodies[i];
    }
}

// A backend sends a chunk with a chunk extension, "5;name=value", which RFC 9112 7.1.1 allows
// and tells a recipient to ignore when it does not know it. The body must read as the data.
VOID TEST(ProtocolHttpConnTest, DISABLED_ChunkWithExtension_BodyRefused)
{
    srs_error_t err = srs_success;

    MockBufferIO io;
    io.append("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5;name=value\r\nHello\r\n0\r\n\r\n");
    SrsHttpParser hp;
    SrsHttpMessage *msg_raw = NULL;
    HELPER_ASSERT_SUCCESS(http_conn_parse(&hp, &io, HTTP_RESPONSE, false, &msg_raw));
    SrsUniquePtr<SrsHttpMessage> msg(msg_raw);

    string body;
    HELPER_EXPECT_SUCCESS(http_conn_read_body(msg.get(), body));
    EXPECT_STREQ("Hello", body.c_str());
}

// An HTTP-FLV origin or a hook backend closes the connection inside a chunk, such as when it
// restarts. The bytes that came are given to the reader, then the read fails with the socket
// error instead of waiting for the rest of the chunk.
VOID TEST(ProtocolHttpConnTest, ChunkedPeerClosesInsideChunk_ReadFails)
{
    srs_error_t err = srs_success;

    MockBufferIO io;
    io.append("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0d\r\nHello");
    SrsHttpParser hp;
    SrsHttpMessage *msg_raw = NULL;
    HELPER_ASSERT_SUCCESS(http_conn_parse(&hp, &io, HTTP_RESPONSE, false, &msg_raw));
    SrsUniquePtr<SrsHttpMessage> msg(msg_raw);

    string body;
    err = http_conn_read_body(msg.get(), body);
    EXPECT_EQ(ERROR_SOCKET_READ, srs_error_code(err));
    srs_freep(err);
    EXPECT_STREQ("Hello", body.c_str());
}

// The peer sends a whole chunk and closes before the CRLF that ends it. The read fails on the
// missing CRLF instead of taking the cut chunk as complete.
VOID TEST(ProtocolHttpConnTest, ChunkedPeerClosesBeforeChunkEnd_ReadFails)
{
    srs_error_t err = srs_success;

    MockBufferIO io;
    io.append("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nHello");
    SrsHttpParser hp;
    SrsHttpMessage *msg_raw = NULL;
    HELPER_ASSERT_SUCCESS(http_conn_parse(&hp, &io, HTTP_RESPONSE, false, &msg_raw));
    SrsUniquePtr<SrsHttpMessage> msg(msg_raw);

    string body;
    err = http_conn_read_body(msg.get(), body);
    EXPECT_EQ(ERROR_SOCKET_READ, srs_error_code(err));
    srs_freep(err);
    EXPECT_FALSE(msg->body_reader()->eof());
}

// An HTTP-TS player (or .mp3 or .aac) leaves mid-stream. The live stream writes a 200 with no
// length, so each TS chunk goes out in chunked encoding; once the player is gone the next chunk
// fails with the socket error, and nothing more is sent.
VOID TEST(ProtocolHttpConnTest, HttpTsPlayerLeavesMidStream_ChunkWriteFails)
{
    srs_error_t err = srs_success;

    MockBufferIO io;
    SrsHttpResponseWriter w(&io);
    w.header()->set_content_type("video/MP2T");
    w.write_header(SRS_CONSTS_HTTP_OK);

    char ts[188];
    memset(ts, 0xff, sizeof(ts));
    ts[0] = 0x47;
    HELPER_ASSERT_SUCCESS(w.write(ts, sizeof(ts)));

    string out = HELPER_BUFFER2STR(&io.out_buffer);
    EXPECT_NE(string::npos, out.find("Transfer-Encoding: chunked\r\n"));
    EXPECT_NE(string::npos, out.find("\r\n\r\nbc\r\n\x47"));
    int sent = io.out_length();

    io.out_err = srs_error_new(ERROR_SOCKET_WRITE, "broken pipe");
    err = w.write(ts, sizeof(ts));
    EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(sent, io.out_length());
}

// An HTTP-FLV player leaves mid-stream. The FLV tags of each batch go out as one chunk; once
// the player is gone the next batch fails with the socket error.
VOID TEST(ProtocolHttpConnTest, HttpFlvPlayerLeavesMidStream_TagsWriteFails)
{
    srs_error_t err = srs_success;

    MockBufferIO io;
    SrsHttpResponseWriter w(&io);
    w.header()->set_content_type("video/x-flv");
    w.write_header(SRS_CONSTS_HTTP_OK);

    char tag[11 + 6 + 4];
    memset(tag, 0, sizeof(tag));
    tag[0] = SrsFrameTypeVideo;
    iovec iovs[2];
    iovs[0].iov_base = tag;
    iovs[0].iov_len = 11;
    iovs[1].iov_base = tag + 11;
    iovs[1].iov_len = 10;
    HELPER_ASSERT_SUCCESS(w.writev(iovs, 2, NULL));
    // The tags go out as one chunk: the size line, the two tag parts and the CRLF.
    string out = HELPER_BUFFER2STR(&io.out_buffer);
    int header_size = (int)out.find("\r\n\r\n") + 4;
    EXPECT_EQ(0, out.compare(header_size, 4, "15\r\n"));
    EXPECT_EQ(header_size + 4 + 21 + 2, io.out_length());
    int sent = io.out_length();

    io.out_err = srs_error_new(ERROR_SOCKET_WRITE, "broken pipe");
    err = w.writev(iovs, 2, NULL);
    EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(sent, io.out_length());
}

// An audio media packet with an AAC raw frame.
static SrsMediaPacket *http_conn_aac_packet(int64_t timestamp)
{
    SrsMediaPacket *msg = new SrsMediaPacket();
    msg->timestamp_ = timestamp;
    msg->message_type_ = SrsFrameTypeAudio;
    char *data = new char[8];
    memset(data, 0x55, 8);
    data[0] = (char)0xaf;
    data[1] = 0x01;
    msg->wrap(data, 8);
    return msg;
}

// An HTTP-FLV mount with has_audio off for a stream the publisher sends audio on too. With
// drop_if_not_match on, a batch of only audio frames is all dropped, so nothing is written:
// an empty chunk would be the "0" chunk that ends the chunked body, and the player would see
// the stream end.
VOID TEST(ProtocolHttpConnTest, HttpFlvAudioOnlyBatchOnVideoMount_NoChunkWritten)
{
    srs_error_t err = srs_success;

    MockBufferIO io;
    SrsHttpResponseWriter w(&io);
    w.header()->set_content_type("video/x-flv");
    w.write_header(SRS_CONSTS_HTTP_OK);

    SrsBufferWriter bw(&w);
    SrsFlvStreamEncoder enc;
    HELPER_ASSERT_SUCCESS(enc.initialize(&bw, NULL));
    enc.set_guess_has_av(false);
    enc.set_has_audio(false);
    enc.set_has_video(true);
    enc.set_drop_if_not_match(true);

    char avc[] = {0x17, 0x01, 0x00, 0x00, 0x00, 0x65};
    HELPER_ASSERT_SUCCESS(enc.write_video(0, avc, sizeof(avc)));
    int sent = io.out_length();
    ASSERT_GT(sent, 0);

    SrsMediaPacket *msgs[] = {http_conn_aac_packet(23), http_conn_aac_packet(46)};
    HELPER_EXPECT_SUCCESS(enc.write_tags(msgs, 2));
    EXPECT_EQ(sent, io.out_length());
    EXPECT_EQ(string::npos, HELPER_BUFFER2STR(&io.out_buffer).find("\r\n0\r\n\r\n"));

    for (int i = 0; i < 2; i++) {
        srs_freep(msgs[i]);
    }
}

// A client that has gone before the server answers, such as a player that gave up on a 404.
// The reply's header is the first write, so the error reply fails with the socket error.
VOID TEST(ProtocolHttpConnTest, ErrorReplyToClientGone_HeaderWriteFails)
{
    srs_error_t err = srs_success;

    MockBufferIO io;
    io.out_err = srs_error_new(ERROR_SOCKET_WRITE, "broken pipe");
    SrsHttpResponseWriter w(&io);

    err = srs_go_http_error(&w, SRS_CONSTS_HTTP_NotFound);
    EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(0, io.out_length());
}

// A browser sends a CORS preflight for an API call, and the page is closed before the answer.
// The preflight answer has no body, so its header goes out in final_request, which fails with
// the socket error; with the browser still there, it gets the 200 and the CORS headers.
VOID TEST(ProtocolHttpConnTest, CorsPreflightFromClosedPage_FinalRequestFails)
{
    srs_error_t err = srs_success;

    for (int i = 0; i < 2; i++) {
        bool page_closed = (i == 1);

        MockBufferIO in;
        in.append("OPTIONS /api/v1/streams/ HTTP/1.1\r\nHost: 192.168.1.10:1985\r\nOrigin: http://192.168.1.10:8080\r\n"
                  "Access-Control-Request-Method: GET\r\n\r\n");
        SrsHttpParser hp;
        SrsHttpMessage *msg_raw = NULL;
        HELPER_ASSERT_SUCCESS(http_conn_parse(&hp, &in, HTTP_REQUEST, false, &msg_raw));
        SrsUniquePtr<SrsHttpMessage> msg(msg_raw);

        SrsHttpCorsMux cors(NULL);
        HELPER_ASSERT_SUCCESS(cors.initialize(true));

        MockBufferIO io;
        if (page_closed) {
            io.out_err = srs_error_new(ERROR_SOCKET_WRITE, "broken pipe");
        }
        SrsHttpResponseWriter w(&io);
        err = cors.serve_http(&w, msg.get());

        if (page_closed) {
            EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
            srs_freep(err);
            EXPECT_EQ(0, io.out_length());
        } else {
            HELPER_EXPECT_SUCCESS(err);
            string out = HELPER_BUFFER2STR(&io.out_buffer);
            EXPECT_EQ(0u, out.find("HTTP/1.1 200 OK\r\n")) << out;
            EXPECT_NE(string::npos, out.find("Access-Control-Allow-Origin: *\r\n")) << out;
            EXPECT_NE(string::npos, out.find("Content-Length: 0\r\n")) << out;
        }
    }
}

// An API client whose Basic token is mangled: the last padding cut off or replaced, padding
// where a character must be, or bytes after the padding. Each is refused with a challenge, also
// the one whose token holds the right user and password before the trailing bytes.
VOID TEST(ProtocolHttpStackTest, ApiClientWithMangledBasicToken_Refused)
{
    srs_error_t err = srs_success;

    const char *tokens[] = {
        "YWRtaW46YQ=",       // "admin:a" with one of its two '=' cut off
        "YWRtaW46YQ=Q",      // "admin:a" with its second '=' replaced by a character
        "YWRtaW46Y===",      // padding at the second character of a quantum
        "YWRtaW46YWRtaW4=x", // "admin:admin", then a trailing byte
    };
    for (int i = 0; i < (int)(sizeof(tokens) / sizeof(tokens[0])); i++) {
        MockBufferIO in;
        in.append(string("GET /api/v1/streams/ HTTP/1.1\r\nHost: 192.168.1.10:1985\r\nAuthorization: Basic ") + tokens[i] + "\r\n\r\n");
        SrsHttpParser hp;
        SrsHttpMessage *msg_raw = NULL;
        HELPER_ASSERT_SUCCESS(http_conn_parse(&hp, &in, HTTP_REQUEST, false, &msg_raw));
        SrsUniquePtr<SrsHttpMessage> msg(msg_raw);

        SrsHttpBasicAuthenticator auth("admin", "admin");
        ASSERT_TRUE(auth.match(msg.get()));

        MockBufferIO io;
        SrsHttpResponseWriter w(&io);
        err = auth.authenticate(&w, msg.get());
        EXPECT_EQ(ERROR_BASE64_DECODE, srs_error_code(err)) << tokens[i];
        srs_freep(err);
        EXPECT_STREQ("Basic realm=\"SRS\"", w.header()->get("WWW-Authenticate").c_str()) << tokens[i];
    }
}

// A WHIP client on Node.js fetch sends its header names in lower case, as HTTP/2 does. The
// server looks up each header by its canonical name, so the names are stored that way.
VOID TEST(ProtocolHttpStackTest, WhipClientWithLowerCaseHeaderNames_HeadersFound)
{
    srs_error_t err = srs_success;

    MockBufferIO in;
    in.append("POST /rtc/v1/whip/?app=live&stream=livestream HTTP/1.1\r\nhost: 192.168.1.10:1985\r\n"
              "content-type: application/sdp\r\nx-forwarded-for: 10.0.0.8\r\ncontent-length: 4\r\n\r\nv=0\n");
    SrsHttpParser hp;
    SrsHttpMessage *msg_raw = NULL;
    HELPER_ASSERT_SUCCESS(http_conn_parse(&hp, &in, HTTP_REQUEST, false, &msg_raw));
    SrsUniquePtr<SrsHttpMessage> msg(msg_raw);

    EXPECT_STREQ("application/sdp", msg->header()->get("Content-Type").c_str());
    EXPECT_STREQ("10.0.0.8", msg->header()->get("X-Forwarded-For").c_str());
    EXPECT_EQ(4, msg->content_length());
    EXPECT_STREQ("192.168.1.10", msg->host().c_str());
    EXPECT_STREQ("livestream", msg->query_get("stream").c_str());
}

// Writes a file of size bytes in the utest tmp dir, and returns its path.
static string http_stack_write_file(string name, int size)
{
    string path = srs_utest_tmp_dir() + "/" + name;
    string data(size, 'F');
    SrsFileWriter fw;
    srs_error_t err = fw.open(path);
    if (err == srs_success) {
        err = fw.write((void *)data.data(), data.length(), NULL);
    }
    srs_freep(err);
    return path;
}

// A flv.js or Flash player that seeks to the start asks for the VOD file with start=0, and a
// player with a bad start parameter gets the same: the whole file, not a stream from an offset.
VOID TEST(ProtocolHttpStackTest, FlvVodPlayerSeeksToStart_WholeFileServed)
{
    srs_error_t err = srs_success;

    string name = "srs-utest-ai41-vod.flv";
    string path = http_stack_write_file(name, 100);

    const char *starts[] = {"0", "abc"};
    for (int i = 0; i < 2; i++) {
        SrsHttpMuxEntry e;
        e.pattern = "/";
        SrsHttpFileServer h(srs_utest_tmp_dir());
        h.entry_ = &e;

        SrsHttpMessage r(NULL, NULL);
        HELPER_ASSERT_SUCCESS(r.set_url("/" + name + "?start=" + starts[i], false));

        MockBufferIO io;
        SrsHttpResponseWriter w(&io);
        HELPER_EXPECT_SUCCESS(h.serve_http(&w, &r));

        string out = HELPER_BUFFER2STR(&io.out_buffer);
        EXPECT_EQ(0u, out.find("HTTP/1.1 200 OK\r\n")) << starts[i];
        EXPECT_NE(string::npos, out.find("Content-Length: 100\r\n")) << starts[i];
        EXPECT_NE(string::npos, out.find("Content-Type: video/x-flv\r\n")) << starts[i];
        EXPECT_EQ(string(100, 'F'), out.substr(out.find("\r\n\r\n") + 4)) << starts[i];
    }

    ::unlink(path.c_str());
}

// A player downloading a static file closes the connection: the first write fails and the
// download stops with the socket error. For an empty file, the header is the only write, in
// final_request, which fails the same way.
VOID TEST(ProtocolHttpStackTest, PlayerLeavesDuringFileDownload_ServeFails)
{
    srs_error_t err = srs_success;

    int sizes[] = {6000, 0};
    for (int i = 0; i < 2; i++) {
        string name = "srs-utest-ai41-download.mp4v";
        string path = http_stack_write_file(name, sizes[i]);

        SrsHttpMuxEntry e;
        e.pattern = "/";
        SrsHttpFileServer h(srs_utest_tmp_dir());
        h.entry_ = &e;

        SrsHttpMessage r(NULL, NULL);
        HELPER_ASSERT_SUCCESS(r.set_url("/" + name, false));

        MockBufferIO io;
        io.out_err = srs_error_new(ERROR_SOCKET_WRITE, "broken pipe");
        SrsHttpResponseWriter w(&io);
        err = h.serve_http(&w, &r);
        EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err)) << sizes[i];
        srs_freep(err);
        EXPECT_EQ(0, io.out_length());

        ::unlink(path.c_str());
    }
}

// Truncates the served file at the first write to the player, as an operator who copies a new
// file over the old one while a player downloads it.
class HttpStackTruncatingIO : public MockBufferIO
{
public:
    string path_;
    bool truncated_;

public:
    HttpStackTruncatingIO(string path) : path_(path), truncated_(false) {}

public:
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite)
    {
        if (!truncated_) {
            truncated_ = true;
            int fd = srs_file_open(path_.c_str(), O_WRONLY | O_TRUNC);
            if (fd < 0) {
                return srs_error_new(ERROR_SYSTEM_FILE_WRITE, "truncate");
            }
            srs_file_close(fd);
        }
        return MockBufferIO::write(buf, size, nwrite);
    }
};

// A static file is cut short while a player downloads it: the server read the first block
// before, and the next read hits the new end. The download stops with the EOF error, after the
// header that promised the old size and the first block; the server then closes the connection,
// so the player sees a short body.
VOID TEST(ProtocolHttpStackTest, FileTruncatedDuringDownload_DownloadStops)
{
    srs_error_t err = srs_success;

    string name = "srs-utest-ai41-truncated.mp4v";
    string path = http_stack_write_file(name, 6000);

    SrsHttpMuxEntry e;
    e.pattern = "/";
    SrsHttpFileServer h(srs_utest_tmp_dir());
    h.entry_ = &e;

    SrsHttpMessage r(NULL, NULL);
    HELPER_ASSERT_SUCCESS(r.set_url("/" + name, false));

    HttpStackTruncatingIO io(path);
    SrsHttpResponseWriter w(&io);
    err = h.serve_http(&w, &r);
    EXPECT_EQ(ERROR_SYSTEM_FILE_EOF, srs_error_code(err));
    srs_freep(err);

    string out = HELPER_BUFFER2STR(&io.out_buffer);
    EXPECT_EQ(0u, out.find("HTTP/1.1 200 OK\r\n"));
    EXPECT_NE(string::npos, out.find("Content-Length: 6000\r\n"));
    EXPECT_EQ(4096u, out.length() - (out.find("\r\n\r\n") + 4));

    ::unlink(path.c_str());
}

// An HLS segment is removed, by hls_dispose or the cleanup of old segments, after the server saw
// it exist but before it opens it. The mux returns the open error, and the player gets nothing.
VOID TEST(ProtocolHttpStackTest, SegmentRemovedBeforeOpen_MuxReturnsOpenError)
{
    srs_error_t err = srs_success;

    SrsHttpServeMux mux;
    HELPER_ASSERT_SUCCESS(mux.initialize());

    SrsHttpFileServer *h = new SrsHttpFileServer(srs_utest_tmp_dir());
    h->set_path(new MockSrsPathForHttpFileServer(false));
    HELPER_ASSERT_SUCCESS(mux.handle("/", h));

    SrsHttpMessage r(NULL, NULL);
    HELPER_ASSERT_SUCCESS(r.set_url("/live/srs-utest-ai41-removed-0.ts", false));

    MockBufferIO io;
    SrsHttpResponseWriter w(&io);
    err = mux.serve_http(&w, &r);
    EXPECT_EQ(ERROR_SYSTEM_FILE_OPENE, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(0, io.out_length());
}

// An encoder that puts a token as the user of its RTMP URL, such as rtmp://token@host/live: the
// token is not part of the host, so the vhost, app and stream are found as without it.
VOID TEST(ProtocolHttpStackTest, RtmpTcUrlWithUserOnly_HostWithoutUser)
{
    string schema, host, vhost, app, stream = "livestream", param;
    int port = 0;
    srs_net_url_parse_tcurl("rtmp://token@192.168.1.10/live", schema, host, vhost, app, stream, port, param);

    EXPECT_STREQ("rtmp", schema.c_str());
    EXPECT_STREQ("192.168.1.10", host.c_str());
    EXPECT_STREQ("192.168.1.10", vhost.c_str());
    EXPECT_STREQ("live", app.c_str());
    EXPECT_STREQ("livestream", stream.c_str());
    EXPECT_EQ(1935, port);

    SrsHttpUri uri;
    srs_error_t err = srs_success;
    HELPER_ASSERT_SUCCESS(uri.initialize("http://token@127.0.0.1:8085/api/v1/streams"));
    EXPECT_STREQ("token", uri.username().c_str());
    EXPECT_STREQ("", uri.password().c_str());
    EXPECT_STREQ("127.0.0.1", uri.get_host().c_str());
    EXPECT_EQ(8085, uri.get_port());
}

// An operator writes a hook URL with no host, such as on_publish http:///api/v1/streams. The
// hook refuses the URL instead of calling a host it made up.
VOID TEST(ProtocolHttpStackTest, HookUrlWithNoHost_Refused)
{
    const char *urls[] = {"http:///api/v1/streams", "http://token@/api/v1/streams"};
    for (int i = 0; i < 2; i++) {
        SrsHttpUri uri;
        srs_error_t err = uri.initialize(urls[i]);
        EXPECT_EQ(ERROR_HTTP_PARSE_URI, srs_error_code(err)) << urls[i];
        srs_freep(err);
    }
}

// Log a line the way the tagged SRS log macros (srs_trace2, srs_warn2) do, through a va_list.
static void protocol_log_tagged(SrsFileLog &log, SrsLogLevel level, const char *tag, const SrsContextId &cid, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log.log(level, tag, cid, fmt, args);
    va_end(args);
}

// A WebRTC player's track gives up on lost packets and the source asks the publisher for a key frame,
// logged with srs_trace2(TAG_MAYBE): the trace line names the session and the tag, with no errno,
// while a late TWCC timer, srs_warn2(TAG_LARGE_TIMER), also logs the errno before the tag.
VOID TEST(ProtocolLogTest, RtcNackTimeoutTraceTagged_NoErrnoBeforeTag)
{
    MockLogWriterForFileLog writer;
    SrsFileLog log;

    srs_freep(log.writer_);
    log.writer_ = &writer;
    log.log_to_file_tank_ = false;
    log.level_ = SrsLogLevelTrace;

    SrsContextId cid;
    cid.set_value("k3j9x2ab");

    errno = EAGAIN;
    protocol_log_tagged(log, SrsLogLevelTrace, TAG_MAYBE, cid, "RTC: NACK timeout=%u, request PLI, track=%s, ssrc=%u", 3, "video-0", 12345);
    ASSERT_EQ(1, writer.write_console_count_);
    string trace = writer.console_;
    EXPECT_TRUE(trace.find("[k3j9x2ab][MAYBE] RTC: NACK timeout=3, request PLI, track=video-0, ssrc=12345\n") != string::npos) << trace;
    EXPECT_EQ('[', trace[0]);

    char warn_head[64];
    snprintf(warn_head, sizeof(warn_head), "[k3j9x2ab][%d][LARGE_TIMER] twcc delay", EAGAIN);
    errno = EAGAIN;
    protocol_log_tagged(log, SrsLogLevelWarn, TAG_LARGE_TIMER, cid, "twcc delay %dms > 100ms, count=%u/%u", 150, 1, 10);
    ASSERT_EQ(2, writer.write_console_count_);
    EXPECT_TRUE(writer.console_.find(warn_head) != string::npos) << writer.console_;

    log.writer_ = NULL;
}

// An HLS player picks its own session with ?hls_ctx=xxx, and the server logs the session with that
// id. An id longer than the 64 KB log line (the HTTP reader takes a URL of 128 KB) makes the header
// overflow, so the line is dropped rather than written cut, and the next session logs as usual.
VOID TEST(ProtocolLogTest, HlsPlayerWithOversizedCtx_LogLineDropped)
{
    MockLogWriterForFileLog writer;
    SrsFileLog log;

    srs_freep(log.writer_);
    log.writer_ = &writer;
    log.log_to_file_tank_ = false;
    log.level_ = SrsLogLevelTrace;

    SrsContextId huge;
    huge.set_value(string(70000, 'h'));
    protocol_log_tagged(log, SrsLogLevelTrace, NULL, huge, "HLS: play stream=%s", "live/livestream");
    protocol_log_tagged(log, SrsLogLevelWarn, NULL, huge, "HLS: play stream=%s", "live/livestream");
    EXPECT_EQ(0, writer.write_console_count_);

    SrsContextId normal;
    normal.set_value("k3j9x2ab");
    protocol_log_tagged(log, SrsLogLevelTrace, NULL, normal, "HLS: play stream=%s", "live/livestream");
    ASSERT_EQ(1, writer.write_console_count_);
    EXPECT_TRUE(writer.console_.find("[k3j9x2ab] HLS: play stream=live/livestream\n") != string::npos) << writer.console_;

    log.writer_ = NULL;
}

// Feeds one PES of an SRT publisher to its frame builder, the way SrsSrtSource does for each
// message its TS context completes.
static srs_error_t raw_avc_srt_pes(SrsSrtFrameBuilder *builder, SrsTsStream stream, const string &payload)
{
    SrsTsChannel channel;
    channel.apply_ = (stream == SrsTsStreamAudioAAC) ? SrsTsPidApplyAudio : SrsTsPidApplyVideo;
    channel.stream_ = stream;

    SrsTsMessage msg(&channel, NULL);
    msg.sid_ = (stream == SrsTsStreamAudioAAC) ? SrsTsPESStreamIdAudioCommon : SrsTsPESStreamIdVideoCommon;
    msg.dts_ = msg.pts_ = 90000;
    msg.payload_->append(payload.data(), (int)payload.length());

    return builder->on_ts_message(&msg);
}

// An SRT publisher's HEVC key frame whose SPS is cut after its profile, tier and level, as from
// an encoder or a relay that truncates the parameter sets. The VPS parses, so the builder makes
// the sequence header and refuses it at the SPS, and no frame reaches the source.
VOID TEST(ProtocolRawAvcTest, SrtHevcKeyframeWithCutSps_SequenceHeaderRefused)
{
    srs_error_t err;

    MockFrameTarget target;
    SrsSrtFrameBuilder builder(&target);
    builder.assemble();
    MockRtcAsyncCallRequest req("test.vhost", "live", "hevc");
    HELPER_ASSERT_SUCCESS(builder.initialize(&req));

    const uint8_t vps[] = {0x40, 0x01, 0x0c, 0x01, 0xff, 0xff, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0x90, 0x00, 0x00, 0x03,
                           0x00, 0x00, 0x03, 0x00, 0x3d, 0x95, 0x98, 0x09};
    // The first 22 bytes of a 720p SPS: the NALU header, the ids and the profile, tier and level,
    // but none of the fields after them.
    const uint8_t sps[] = {0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03,
                           0x00, 0x3d, 0xa0, 0x02, 0x80, 0x80};
    const uint8_t pps[] = {0x44, 0x01, 0xc1, 0x72, 0xb4, 0x62, 0x40};
    const uint8_t idr[] = {0x26, 0x01, 0xaf, 0x06, 0xb8, 0x63, 0xef, 0x3a, 0x7f, 0x3c, 0x00, 0x01, 0x00, 0x80};
    string start_code("\x00\x00\x00\x01", 4);

    string au;
    au += start_code + string((const char *)vps, sizeof(vps));
    au += start_code + string((const char *)sps, sizeof(sps));
    au += start_code + string((const char *)pps, sizeof(pps));
    au += start_code + string((const char *)idr, sizeof(idr));

    err = raw_avc_srt_pes(&builder, SrsTsStreamVideoHEVC, au);
    ASSERT_TRUE(err != srs_success);
    EXPECT_TRUE(srs_error_desc(err).find("sps demux failed, len=22") != string::npos) << srs_error_desc(err);
    srs_freep(err);

    EXPECT_EQ(0, target.on_frame_count_);
}

// An SRT publisher's audio PES of two AAC frames, whose second ADTS header says 10 bytes but
// only 8 arrived, as when the encoder or a relay cuts the PES. The first frame goes to the
// source with its sequence header, and the cut frame is refused, not read past the PES.
VOID TEST(ProtocolRawAvcTest, SrtAacPesWithCutAdtsFrame_CutFrameRefused)
{
    srs_error_t err;

    MockFrameTarget target;
    SrsSrtFrameBuilder builder(&target);
    builder.assemble();
    MockRtcAsyncCallRequest req("test.vhost", "live", "aac");
    HELPER_ASSERT_SUCCESS(builder.initialize(&req));

    // AAC-LC, 44.1kHz, stereo, frame_length 10: a 7-byte ADTS header and 3 bytes of raw data.
    const uint8_t adts[] = {0xff, 0xf9, 0x50, 0x80, 0x01, 0x5f, 0xfc, 0xaa, 0xbb, 0xcc};
    string pes = string((const char *)adts, sizeof(adts)) + string((const char *)adts, 8);

    err = raw_avc_srt_pes(&builder, SrsTsStreamAudioAAC, pes);
    ASSERT_TRUE(err != srs_success);
    EXPECT_EQ(ERROR_AAC_ADTS_HEADER, srs_error_code(err)) << srs_error_desc(err);
    EXPECT_TRUE(srs_error_desc(err).find("requires 3 only 1 bytes") != string::npos) << srs_error_desc(err);
    srs_freep(err);

    // The sequence header and the first frame, both at the PES time.
    ASSERT_EQ(2, target.on_frame_count_);
    EXPECT_EQ(1000, target.timestamps_[0]);
    EXPECT_EQ(1000, target.timestamps_[1]);
}

static void stun_put_u16(std::string &b, uint16_t v)
{
    b.push_back((char)((v >> 8) & 0xff));
    b.push_back((char)(v & 0xff));
}

static void stun_put_u32(std::string &b, uint32_t v)
{
    stun_put_u16(b, (uint16_t)(v >> 16));
    stun_put_u16(b, (uint16_t)(v & 0xffff));
}

static void stun_put_attr(std::string &b, uint16_t type, const std::string &value)
{
    stun_put_u16(b, type);
    stun_put_u16(b, (uint16_t)value.size());
    b.append(value);
    if (value.size() % 4) {
        b.append(4 - value.size() % 4, '\0');
    }
}

// Set the message length in the header to the bytes after it plus the attribute about to be appended.
static void stun_set_length(std::string &b, int next_attr_size)
{
    int len = (int)b.size() - 20 + next_attr_size;
    b[2] = (char)((len >> 8) & 0xff);
    b[3] = (char)(len & 0xff);
}

// A binding request as Chrome sends it to an ice-lite server, which always makes the browser the controlling agent:
// USERNAME, ICE-CONTROLLING with its tie-breaker, PRIORITY and, on the check that nominates the pair, USE-CANDIDATE,
// then MESSAGE-INTEGRITY signed with the server's ICE password and FINGERPRINT, see RFC 8445 sections 7.1 and 8.1.1.
std::string stun_controlling_binding_request(const std::string &username, const std::string &pwd, bool nominate)
{
    std::string b;
    stun_put_u16(b, BindingRequest);
    stun_put_u16(b, 0);
    stun_put_u32(b, kStunMagicCookie);
    b.append("chrome-tid-1", 12);

    stun_put_attr(b, Username, username);
    stun_put_attr(b, IceControlling, std::string("\x5a\x13\x7c\x02\x9e\x41\xd8\x66", 8));
    std::string priority;
    stun_put_u32(priority, 0x6e7f1eff);
    stun_put_attr(b, Priority, priority);
    if (nominate) {
        stun_put_attr(b, UseCandidate, "");
    }

    stun_set_length(b, 24);
    unsigned char mac[20];
    unsigned int mac_len = 0;
    HMAC(EVP_sha1(), pwd.data(), (int)pwd.size(), (const unsigned char *)b.data(), b.size(), mac, &mac_len);
    stun_put_attr(b, MessageIntegrity, std::string((const char *)mac, mac_len));

    stun_set_length(b, 8);
    std::string crc;
    stun_put_u32(crc, srs_crc32_ieee(b.data(), (int)b.size()) ^ 0x5354554E);
    stun_put_attr(b, Fingerprint, crc);
    return b;
}

// A browser nominates the candidate pair with USE-CANDIDATE on a check sent as the controlling agent. The server,
// which is ice-lite, accepts it even with the strict role check on, since only a controlled peer is refused, and the
// session manager logs both flags for the pair.
VOID TEST(ProtocolRtcStunTest, ChromeNominatesPairAsControllingAgent_AcceptedUnderStrictCheck)
{
    srs_error_t err;

    // The ICE password of the answer, 32 random characters as the server makes it.
    SrsRand rand;
    const std::string pwd = rand.gen_str(32);

    // A check before the nomination carries no USE-CANDIDATE.
    std::string check = stun_controlling_binding_request("srvufrag:cliufrag", pwd, false);
    SrsStunPacket c;
    HELPER_ASSERT_SUCCESS(c.decode(check.data(), (int)check.size()));
    EXPECT_TRUE(c.get_ice_controlling());
    EXPECT_FALSE(c.get_use_candidate());

    std::string nominate = stun_controlling_binding_request("srvufrag:cliufrag", pwd, true);
    SrsStunPacket r;
    HELPER_ASSERT_SUCCESS(r.decode(nominate.data(), (int)nominate.size()));
    EXPECT_TRUE(r.is_binding_request());
    EXPECT_STREQ("srvufrag", r.get_local_ufrag().c_str());
    EXPECT_STREQ("cliufrag", r.get_remote_ufrag().c_str());
    EXPECT_TRUE(r.get_use_candidate());
    EXPECT_TRUE(r.get_ice_controlling());
    EXPECT_FALSE(r.get_ice_controlled());

    MockAppConfigForStunStrictCheck config;
    MockRtcAsyncTaskExecutor exec;
    SrsContextId cid;
    cid.set_value("test-rtc-stun-controlling");
    SrsUniquePtr<SrsRtcConnection> conn(new SrsRtcConnection(&exec, cid));
    conn->config_ = &config;
    SrsRequest *req = new SrsRequest();
    req->vhost_ = "__defaultVhost__";
    conn->req_ = req;
    conn->local_sdp_.session_info_.ice_pwd_ = pwd;

    std::string ice_pwd;
    HELPER_EXPECT_SUCCESS(conn->on_binding_request(&r, ice_pwd));
    EXPECT_STREQ(pwd.c_str(), ice_pwd.c_str());

    conn->config_ = NULL;
}

// Passes a STUN datagram from the address of a known session to the session manager, as the UDP listener does, and
// returns the error code, telling whether the session's network got the packet.
static int stun_session_manager_receive(std::string datagram, bool *network_got_it)
{
    MockResourceManagerForUpdateSessions conns;
    MockRtcNetworkForUdpNetwork network;
    MockRtcConnectionForUpdateSessions conn;
    conn.udp_network_ = &network;
    conns.fast_id_map_[12345] = &conn;

    SrsRtcSessionManager manager;
    manager.conn_manager_ = &conns;

    MockUdpMuxSocket skt;
    skt.fast_id_ = 12345;
    skt.peer_id_ = "192.168.1.10:50000";
    skt.data_ = (char *)datagram.data();
    skt.size_ = (int)datagram.size();

    srs_error_t err = manager.on_udp_packet(&skt);
    int code = srs_error_code(err);
    srs_freep(err);

    *network_got_it = network.on_stun_called_;
    manager.conn_manager_ = NULL;
    conn.udp_network_ = NULL;
    skt.data_ = NULL;
    return code;
}

// A binding request whose datagram does not hold the message its header counts, cut in transit or with bytes after it,
// is refused before it reaches the session, so it neither switches the session's address nor gets a response.
VOID TEST(ProtocolRtcStunTest, BindingRequestNotMatchingItsLength_RefusedBeforeSession)
{
    std::string request = stun_controlling_binding_request("srvufrag:cliufrag", "pwd", true);
    bool network_got_it = true;

    // The FINGERPRINT lost at the end.
    EXPECT_EQ(ERROR_RTC_STUN, stun_session_manager_receive(request.substr(0, request.size() - 8), &network_got_it));
    EXPECT_FALSE(network_got_it);

    // Four bytes after the message.
    network_got_it = true;
    EXPECT_EQ(ERROR_RTC_STUN, stun_session_manager_receive(request + std::string(4, '\0'), &network_got_it));
    EXPECT_FALSE(network_got_it);
}

// A binding request whose USERNAME says it is longer than the rest of the message is refused, rather than reading the
// username past the datagram.
VOID TEST(ProtocolRtcStunTest, AttributeLongerThanMessage_RefusedBeforeSession)
{
    std::string request = stun_controlling_binding_request("srvufrag:cliufrag", "pwd", true);
    // The USERNAME length, right after the 20-byte header and the attribute type.
    request[22] = 0x01;
    request[23] = 0x00;

    bool network_got_it = true;
    EXPECT_EQ(ERROR_RTC_STUN, stun_session_manager_receive(request, &network_got_it));
    EXPECT_FALSE(network_got_it);
}

// An edge pulls a stream with a token from its origin: it connects, plays the stream with
// the token in the stream name, then reads the origin's metadata and video as its ingester
// does, with the ingester's read timeout.
VOID TEST(ProtocolRtmpConnTest, EdgePullsFromOrigin_GetsMetadataAndVideo)
{
    srs_error_t err = srs_success;

    RtmpConnTestOrigin origin(RtmpConnOriginServes);
    HELPER_ASSERT_SUCCESS(origin.start());

    SrsBasicRtmpClient sdk(origin.rtmp_url("livestream?token=abc"), 3 * SRS_UTIME_SECONDS, 3 * SRS_UTIME_SECONDS);
    HELPER_ASSERT_SUCCESS(sdk.connect());

    // The edge passes the vhost in the tcUrl, so it plays without the vhost in the stream.
    string stream;
    HELPER_ASSERT_SUCCESS(sdk.play(60000, false, &stream));
    EXPECT_EQ("livestream?token=abc", stream);

    sdk.set_recv_timeout(3 * SRS_UTIME_SECONDS);

    double width = 0;
    string video;
    for (int i = 0; i < 16 && video.empty(); i++) {
        SrsRtmpCommonMessage *msg_raw = NULL;
        HELPER_ASSERT_SUCCESS(sdk.recv_message(&msg_raw));
        SrsUniquePtr<SrsRtmpCommonMessage> msg(msg_raw);

        if (msg->header_.is_amf0_data()) {
            SrsRtmpCommand *pkt_raw = NULL;
            HELPER_ASSERT_SUCCESS(sdk.decode_message(msg.get(), &pkt_raw));
            SrsUniquePtr<SrsRtmpCommand> pkt(pkt_raw);
            // The |RtmpSampleAccess of the play start is data too.
            SrsOnMetaDataPacket *meta = dynamic_cast<SrsOnMetaDataPacket *>(pkt.get());
            if (!meta) {
                continue;
            }
            SrsAmf0Any *prop = meta->metadata_->ensure_property_number("width");
            ASSERT_TRUE(prop != NULL);
            width = prop->to_number();
        } else if (msg->header_.is_video()) {
            video.assign(msg->payload(), msg->size());
        }
    }

    EXPECT_EQ(1280, width);
    SrsUniquePtr<SrsMediaPacket> sent(RtmpConnTestOrigin::rtmp_conn_video_packet());
    EXPECT_EQ(string(sent->payload(), sent->size()), video);

    EXPECT_EQ(SrsRtmpConnPlay, origin.type_);
    EXPECT_EQ("livestream?token=abc", origin.stream_);
    EXPECT_EQ("rtmp://127.0.0.1:" + srs_strconv_format_int(origin.get_port()) + "/live", origin.tc_url_);

    // The origin sends nothing more, so a read with the forwarder's short pulse times out,
    // and the caller goes on with its loop.
    sdk.set_recv_timeout(10 * SRS_UTIME_MILLISECONDS);
    SrsRtmpCommonMessage *msg = NULL;
    err = sdk.recv_message(&msg);
    EXPECT_EQ(ERROR_SOCKET_TIMEOUT, srs_error_code(err));
    srs_freep(err);
    EXPECT_TRUE(msg == NULL);
}

// A forwarder pushes a stream to its destination: it connects, publishes with the vhost's
// chunk size, and sends a key frame, which the destination gets whole.
VOID TEST(ProtocolRtmpConnTest, ForwarderPublishesToDestination_DestinationGetsVideo)
{
    srs_error_t err = srs_success;

    RtmpConnTestOrigin origin(RtmpConnOriginServes);
    HELPER_ASSERT_SUCCESS(origin.start());

    SrsBasicRtmpClient sdk(origin.rtmp_url("livestream"), 3 * SRS_UTIME_SECONDS, 3 * SRS_UTIME_SECONDS);
    HELPER_ASSERT_SUCCESS(sdk.connect());

    string stream;
    HELPER_ASSERT_SUCCESS(sdk.publish(60000, false, &stream));
    EXPECT_EQ("livestream", stream);

    HELPER_ASSERT_SUCCESS(sdk.send_and_free_message(RtmpConnTestOrigin::rtmp_conn_video_packet()));
    for (int i = 0; i < 100 && origin.video_.empty(); i++) {
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }

    EXPECT_EQ(SrsRtmpConnFlashPublish, origin.type_);
    EXPECT_EQ("livestream", origin.stream_);
    SrsUniquePtr<SrsMediaPacket> sent(RtmpConnTestOrigin::rtmp_conn_video_packet());
    EXPECT_EQ(string(sent->payload(), sent->size()), origin.video_) << origin.error_;
}

// The operator sets the edge's origin to the HTTP API port by mistake. The HTTP server
// answers the RTMP handshake with an HTTP error and closes, so the edge's connect fails at
// the handshake.
VOID TEST(ProtocolRtmpConnTest, EdgeOriginIsHttpPort_ConnectFailsAtHandshake)
{
    srs_error_t err = srs_success;

    RtmpConnTestOrigin origin(RtmpConnOriginNotRtmp);
    HELPER_ASSERT_SUCCESS(origin.start());

    SrsBasicRtmpClient sdk(origin.rtmp_url("livestream"), 3 * SRS_UTIME_SECONDS, 3 * SRS_UTIME_SECONDS);
    err = sdk.connect();
    ASSERT_TRUE(err != srs_success);
    string desc = srs_error_desc(err);
    srs_freep(err);
    EXPECT_NE(string::npos, desc.find("handshake")) << desc;
    EXPECT_EQ(string::npos, desc.find("connect app")) << desc;
}

// The origin refuses the app with an _error onStatus and closes, as an RTMP server does for
// an app it does not serve. The edge's connect fails at connect app.
VOID TEST(ProtocolRtmpConnTest, OriginRejectsApp_ConnectFailsAtConnectApp)
{
    srs_error_t err = srs_success;

    RtmpConnTestOrigin origin(RtmpConnOriginRejectsApp);
    HELPER_ASSERT_SUCCESS(origin.start());

    SrsBasicRtmpClient sdk(origin.rtmp_url("livestream"), 3 * SRS_UTIME_SECONDS, 3 * SRS_UTIME_SECONDS);
    err = sdk.connect();
    ASSERT_TRUE(err != srs_success);
    string desc = srs_error_desc(err);
    srs_freep(err);
    EXPECT_NE(string::npos, desc.find("connect app")) << desc;
    EXPECT_EQ(string::npos, desc.find("create stream")) << desc;

    EXPECT_EQ("rtmp://127.0.0.1:" + srs_strconv_format_int(origin.get_port()) + "/live", origin.tc_url_);
}

// The origin answers connect, then exits, as an origin that restarts at that moment. The
// edge's connect fails at createStream.
VOID TEST(ProtocolRtmpConnTest, OriginExitsAfterConnect_ConnectFailsAtCreateStream)
{
    srs_error_t err = srs_success;

    RtmpConnTestOrigin origin(RtmpConnOriginExitsAfterConnect);
    HELPER_ASSERT_SUCCESS(origin.start());

    SrsBasicRtmpClient sdk(origin.rtmp_url("livestream"), 3 * SRS_UTIME_SECONDS, 3 * SRS_UTIME_SECONDS);
    err = sdk.connect();
    ASSERT_TRUE(err != srs_success);
    string desc = srs_error_desc(err);
    srs_freep(err);
    EXPECT_NE(string::npos, desc.find("create stream")) << desc;
    EXPECT_EQ("", origin.error_);
}

// The connection to the origin is reset right after createStream, such as by a load
// balancer in front of the origin. The play of an edge and the publish of a forwarder fail
// writing their commands, and the caller retries.
VOID TEST(ProtocolRtmpConnTest, OriginResetAfterCreateStream_PlayAndPublishFail)
{
    srs_error_t err = srs_success;

    for (int i = 0; i < 2; i++) {
        bool is_play = (i == 0);

        RtmpConnTestOrigin origin(RtmpConnOriginResetAfterCreateStream);
        HELPER_ASSERT_SUCCESS(origin.start());

        SrsBasicRtmpClient sdk(origin.rtmp_url("livestream"), 3 * SRS_UTIME_SECONDS, 3 * SRS_UTIME_SECONDS);
        HELPER_ASSERT_SUCCESS(sdk.connect());

        // Let the reset reach the client.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);

        string stream;
        if (is_play) {
            err = sdk.play(60000, false, &stream);
        } else {
            err = sdk.publish(60000, false, &stream);
        }
        EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err)) << (is_play ? "play" : "publish") << ": " << srs_error_desc(err);
        srs_freep(err);
        EXPECT_EQ("livestream", stream);
    }
}

// The peer of an RTMP handshake, whose connection is reset after it took a given number of
// writes: a write after the reset fails, as on a TCP socket that got an RST.
class RtmpHandshakeResetIO : public MockBufferIO
{
public:
    int writes_left_;
    int writes_tried_;

public:
    RtmpHandshakeResetIO(int writes_left) : writes_left_(writes_left), writes_tried_(0) {}

public:
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite)
    {
        writes_tried_++;
        if (writes_left_ <= 0) {
            return srs_error_new(ERROR_SOCKET_WRITE, "connection reset by peer");
        }
        writes_left_--;
        return MockBufferIO::write(buf, size, nwrite);
    }
};

// The C0C1 of a client that does the simple handshake, as librtmp (OBS) sends for rtmp://:
// version 3, then the time, zero and random bytes, with no digest.
static string rtmp_hs_plain_c0c1()
{
    SrsHandshakeBytes bytes;
    srs_error_t err = bytes.create_c0c1();
    srs_assert(err == srs_success);
    return string(bytes.c0c1_, 1537);
}

// The C0C1 of a client that does the complex handshake, as FFmpeg and Flash send: version 3,
// then a C1 signed with the Flash Player key in the given schema. When zero_key is set, the
// client signs a C1 whose DH public key is all zeros.
static srs_error_t rtmp_hs_signed_c0c1(srs_schema_type schema, bool zero_key, SrsC1S1 &c1, string &c0c1)
{
    srs_error_t err = srs_success;

    if ((err = c1.c1_create(schema)) != srs_success) {
        return srs_error_wrap(err, "create c1");
    }

    if (zero_key) {
        memset(c1.get_key(), 0, 128);
        if ((err = c1.payload_->c1_create(&c1)) != srs_success) {
            return srs_error_wrap(err, "sign c1");
        }
    }

    char buf[1537];
    buf[0] = 0x03;
    if ((err = c1.dump(buf + 1, 1536)) != srs_success) {
        return srs_error_wrap(err, "dump c1");
    }
    c0c1.assign(buf, sizeof(buf));

    return err;
}

// An OBS publisher (simple handshake) gets S0S1S2 and leaves before it sends C2, such as when
// the user stops streaming at that moment. The server sent S0, an S1 that echoes the client's
// time, and an S2 that echoes C1 (#46), and the connection fails at reading C2.
VOID TEST(ProtocolRtmpHandshakeTest, ObsLeavesBeforeC2_ServerSentS0S1S2)
{
    srs_error_t err = srs_success;

    string c0c1 = rtmp_hs_plain_c0c1();
    MockBufferIO io;
    io.append(c0c1);

    SrsRtmpServer rtmp(&io);
    err = rtmp.handshake();
    EXPECT_EQ(ERROR_SOCKET_READ, srs_error_code(err));
    string desc = srs_error_desc(err);
    srs_freep(err);
    EXPECT_NE(string::npos, desc.find("simple handshake")) << desc;
    EXPECT_NE(string::npos, desc.find("read c2")) << desc;

    ASSERT_EQ(3073, io.out_length());
    string s0s1s2(io.out_buffer.bytes(), 3073);
    EXPECT_EQ(0x03, (uint8_t)s0s1s2[0]);
    EXPECT_EQ(c0c1.substr(1, 4), s0s1s2.substr(5, 4));
    EXPECT_EQ(c0c1.substr(1, 1536), s0s1s2.substr(1537, 1536));
}

// An OBS publisher (simple handshake) resets the connection right after its C0C1, such as an
// encoder that times out and reconnects. Writing S0S1S2 fails, and the server ends the
// connection with the write error.
VOID TEST(ProtocolRtmpHandshakeTest, ObsResetsAfterC0C1_ServerReplyFails)
{
    srs_error_t err = srs_success;

    RtmpHandshakeResetIO io(0);
    io.append(rtmp_hs_plain_c0c1());

    SrsRtmpServer rtmp(&io);
    err = rtmp.handshake();
    EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
    string desc = srs_error_desc(err);
    srs_freep(err);
    EXPECT_NE(string::npos, desc.find("simple handshake")) << desc;
    EXPECT_NE(string::npos, desc.find("write s0s1s2")) << desc;
    EXPECT_EQ(1, io.writes_tried_);
}

// A tool posts to the RTMP port by mistake, such as an HTTP API client set to port 1935. Its
// request is not an RTMP C0 (version 3) and carries no Flash digest, so the server refuses it
// as not plain RTMP and sends nothing back.
VOID TEST(ProtocolRtmpHandshakeTest, HttpPostToRtmpPort_RefusedWithoutReply)
{
    srs_error_t err = srs_success;

    string body = "{\"action\":\"on_publish\",\"stream\":\"" + string(2048, 'x') + "\"}";
    string request = "POST /api/v1/streams HTTP/1.1\r\nHost: 127.0.0.1:1935\r\nContent-Type: application/json\r\n";
    request += "Content-Length: " + srs_strconv_format_int(body.size()) + "\r\n\r\n" + body;

    MockBufferIO io;
    io.append(request);

    SrsRtmpServer rtmp(&io);
    err = rtmp.handshake();
    EXPECT_EQ(ERROR_RTMP_PLAIN_REQUIRED, srs_error_code(err));
    string desc = srs_error_desc(err);
    srs_freep(err);
    EXPECT_NE(string::npos, desc.find("version=50")) << desc;
    EXPECT_EQ(0, io.out_length());
}

// A Flash or FFmpeg publisher (complex handshake, either schema) gets S0S1S2 and leaves before
// it sends C2. The server sent S0, an S1 signed with the FMS key and an S2 that answers the
// client's digest, which a Flash client checks, and the connection fails at reading C2.
VOID TEST(ProtocolRtmpHandshakeTest, FlashLeavesBeforeC2_ServerSentSignedS1S2)
{
    srs_error_t err = srs_success;

    srs_schema_type schemas[] = {srs_schema0, srs_schema1};
    for (int i = 0; i < 2; i++) {
        SrsC1S1 c1;
        string c0c1;
        HELPER_ASSERT_SUCCESS(rtmp_hs_signed_c0c1(schemas[i], false, c1, c0c1));

        MockBufferIO io;
        io.append(c0c1);

        SrsRtmpServer rtmp(&io);
        err = rtmp.handshake();
        EXPECT_EQ(ERROR_SOCKET_READ, srs_error_code(err)) << "schema " << schemas[i];
        string desc = srs_error_desc(err);
        srs_freep(err);
        EXPECT_NE(string::npos, desc.find("complex handshake")) << desc;
        EXPECT_NE(string::npos, desc.find("read c2")) << desc;

        ASSERT_EQ(3073, io.out_length());
        char *s0s1s2 = io.out_buffer.bytes();
        EXPECT_EQ(0x03, (uint8_t)s0s1s2[0]);

        bool is_valid = false;
        SrsC1S1 s1;
        HELPER_ASSERT_SUCCESS(s1.parse(s0s1s2 + 1, 1536, c1.schema()));
        HELPER_ASSERT_SUCCESS(s1.s1_validate_digest(is_valid));
        EXPECT_TRUE(is_valid) << "schema " << schemas[i];

        is_valid = false;
        SrsC2S2 s2;
        HELPER_ASSERT_SUCCESS(s2.parse(s0s1s2 + 1537, 1536));
        HELPER_ASSERT_SUCCESS(s2.s2_validate(&c1, is_valid));
        EXPECT_TRUE(is_valid) << "schema " << schemas[i];
    }
}

// A Flash or FFmpeg publisher (complex handshake) resets the connection right after its C0C1.
// Writing S0S1S2 fails, and the server ends the connection with the write error; it does not
// try the simple handshake on the dead connection.
VOID TEST(ProtocolRtmpHandshakeTest, FlashResetsAfterC0C1_ServerReplyFails)
{
    srs_error_t err = srs_success;

    SrsC1S1 c1;
    string c0c1;
    HELPER_ASSERT_SUCCESS(rtmp_hs_signed_c0c1(srs_schema1, false, c1, c0c1));

    RtmpHandshakeResetIO io(0);
    io.append(c0c1);

    SrsRtmpServer rtmp(&io);
    err = rtmp.handshake();
    EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
    string desc = srs_error_desc(err);
    srs_freep(err);
    EXPECT_NE(string::npos, desc.find("complex handshake")) << desc;
    EXPECT_NE(string::npos, desc.find("write s0s1s2")) << desc;
    EXPECT_EQ(1, io.writes_tried_);
}

// A client signs its C1 with the Flash Player key but leaves the DH public key in it all
// zeros, which is not a valid DH public value. The server cannot compute the key of S1, so it
// closes the connection without a reply.
VOID TEST(ProtocolRtmpHandshakeTest, ClientSignsC1WithZeroKey_ClosedWithoutReply)
{
    srs_error_t err = srs_success;

    SrsC1S1 c1;
    string c0c1;
    HELPER_ASSERT_SUCCESS(rtmp_hs_signed_c0c1(srs_schema1, true, c1, c0c1));

    // The digest is valid, so the server takes the complex handshake.
    bool is_valid = false;
    HELPER_ASSERT_SUCCESS(c1.c1_validate_digest(is_valid));
    ASSERT_TRUE(is_valid);

    MockBufferIO io;
    io.append(c0c1);

    SrsRtmpServer rtmp(&io);
    err = rtmp.handshake();
    EXPECT_EQ(ERROR_OpenSslComputeSharedKey, srs_error_code(err));
    string desc = srs_error_desc(err);
    srs_freep(err);
    EXPECT_NE(string::npos, desc.find("create s1 from c1")) << desc;
    EXPECT_EQ(0, io.out_length());
}

// An edge or forwarder connects to an origin that resets the connection at once, such as an
// SRS origin over its max_connections. Writing C0C1 fails, and the client gets the write error
// to retry.
VOID TEST(ProtocolRtmpHandshakeTest, OriginResetsOnAccept_C0C1WriteFails)
{
    srs_error_t err = srs_success;

    RtmpHandshakeResetIO io(0);

    SrsRtmpClient rtmp(&io);
    err = rtmp.handshake();
    EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
    string desc = srs_error_desc(err);
    srs_freep(err);
    EXPECT_NE(string::npos, desc.find("write c0c1")) << desc;
    EXPECT_EQ(1, io.writes_tried_);
}

// The edge's origin is set to a port where FFmpeg serves MPEG-TS over TCP (tcp://...?listen).
// It sends TS packets as soon as the edge connects, so S0 is the TS sync byte 0x47, not
// version 3. The edge refuses the origin and sends no C2.
VOID TEST(ProtocolRtmpHandshakeTest, OriginPortServesMpegts_HandshakeRefused)
{
    srs_error_t err = srs_success;

    string ts;
    while (ts.size() < 3073) {
        string pkt(188, (char)0xff);
        pkt[0] = 0x47;
        pkt[1] = 0x1f;
        pkt[2] = (char)0xff;
        pkt[3] = 0x10;
        ts += pkt;
    }

    MockBufferIO io;
    io.append(ts);

    SrsRtmpClient rtmp(&io);
    err = rtmp.handshake();
    EXPECT_EQ(ERROR_RTMP_HANDSHAKE, srs_error_code(err));
    string desc = srs_error_desc(err);
    srs_freep(err);
    EXPECT_NE(string::npos, desc.find("version=47")) << desc;

    // Only C0C1 was sent.
    EXPECT_EQ(1537, io.out_length());
}

// The origin sends S0S1S2, then resets the connection, such as an origin restarting at that
// moment. Writing C2 fails, and the edge or forwarder gets the write error to retry.
VOID TEST(ProtocolRtmpHandshakeTest, OriginResetsAfterS0S1S2_C2WriteFails)
{
    srs_error_t err = srs_success;

    // An origin that answers with the simple handshake: S0, a random S1, and C1 echoed as S2.
    SrsHandshakeBytes origin;
    string c1(1536, 'c');
    HELPER_ASSERT_SUCCESS(origin.create_s0s1s2(c1.data()));

    RtmpHandshakeResetIO io(1);
    io.append(string(origin.s0s1s2_, 3073));

    SrsRtmpClient rtmp(&io);
    err = rtmp.handshake();
    EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
    string desc = srs_error_desc(err);
    srs_freep(err);
    EXPECT_NE(string::npos, desc.find("write c2")) << desc;

    // C0C1 was sent, and the whole S0S1S2 read.
    EXPECT_EQ(1537, io.out_length());
    EXPECT_EQ(0, io.length());
    EXPECT_EQ(2, io.writes_tried_);
}

// An AMF0 command message on chunk stream 3, as clients send connect, createStream and the like.
static string rtmp_stack_command(const RtmpStackAmf0 &c, uint32_t stream_id = 0)
{
    return rtmp_stack_chunks(3, RTMP_MSG_AMF0CommandMessage, stream_id, c.data_);
}

// The payload of a command as SRS's own encoder writes it, for a peer that sends a well-formed one.
static string rtmp_stack_payload(SrsRtmpCommand *pkt_raw)
{
    SrsUniquePtr<SrsRtmpCommand> pkt(pkt_raw);

    int size = 0;
    char *payload = NULL;
    srs_error_t err = pkt->encode(size, payload);
    srs_freep(err);

    string b(payload, size);
    srs_freepa(payload);
    return b;
}

// The server receives one message and decodes it, as SrsRtmpConn does with a client's command.
static srs_error_t rtmp_stack_recv_decode(const string &chunks, SrsRtmpCommand **ppkt)
{
    srs_error_t err = srs_success;

    MockBufferIO io;
    io.append(chunks);
    SrsProtocol proto(&io);

    SrsRtmpCommonMessage *msg_raw = NULL;
    if ((err = proto.recv_message(&msg_raw)) != srs_success) {
        return srs_error_wrap(err, "recv");
    }
    SrsUniquePtr<SrsRtmpCommonMessage> msg(msg_raw);

    return proto.decode_message(msg.get(), ppkt);
}

// Checks that err failed with the code and a description that holds want, then frees it.
static void rtmp_stack_expect_error(srs_error_t err, int code, const string &want, const string &label)
{
    EXPECT_TRUE(err != srs_success) << label;
    if (err == srs_success) {
        return;
    }
    string desc = srs_error_desc(err);
    EXPECT_EQ(code, srs_error_code(err)) << label << ": " << desc;
    EXPECT_NE(string::npos, desc.find(want)) << label << ": " << desc;
    srs_freep(err);
}

// The command name and transaction id of each message SRS wrote, read as the peer's RTMP stack
// reads them; a control message gives its type in the name, such as "type=4".
static vector<pair<string, double> > rtmp_stack_sent(MockBufferIO &io)
{
    vector<pair<string, double> > sent;

    MockBufferIO peer;
    peer.append(string(io.out_buffer.bytes(), io.out_buffer.length()));
    SrsProtocol proto(&peer);

    // The stack reads all the bytes at once, so read messages until it has none left.
    while (true) {
        SrsRtmpCommonMessage *msg_raw = NULL;
        srs_error_t err = proto.recv_message(&msg_raw);
        if (err != srs_success) {
            srs_freep(err);
            break;
        }
        SrsUniquePtr<SrsRtmpCommonMessage> msg(msg_raw);

        if (!msg->header_.is_amf0_command() && !msg->header_.is_amf0_data()) {
            sent.push_back(make_pair(srs_fmt_sprintf("type=%d", msg->header_.message_type_), 0.0));
            continue;
        }

        SrsBuffer b(msg->payload(), msg->size());
        string name;
        double tid = 0;
        srs_error_t r0 = srs_amf0_read_string(&b, name);
        srs_freep(r0);
        if (!b.empty()) {
            r0 = srs_amf0_read_number(&b, tid);
            srs_freep(r0);
        }
        sent.push_back(make_pair(name, tid));
    }

    return sent;
}

// A peer's connection drops inside a chunk header: after the first byte of a 3-byte basic header
// (a chunk stream id of 320 or more), or inside the 4-byte extended timestamp of a message whose
// timestamp is past 0xffffff, such as a publisher on air for more than 4.6 hours.
VOID TEST(ProtocolRtmpStackTest, PeerClosesInsideChunkHeader_ReadFails)
{
    if (true) {
        MockBufferIO io;
        io.append(string("\x01\x10", 2)); // fmt=0, cid=1 means a 3-byte basic header; one byte of two
        SrsProtocol proto(&io);

        SrsRtmpCommonMessage *msg = NULL;
        rtmp_stack_expect_error(proto.recv_message(&msg), ERROR_SOCKET_READ, "basic header requires 3 bytes", "basic header");
        EXPECT_TRUE(msg == NULL);
    }

    if (true) {
        uint8_t chunk[] = {
            0x04,             // fmt=0, cid=4
            0xff, 0xff, 0xff, // timestamp 0xffffff, so an extended timestamp follows
            0x00, 0x00, 0x04, // payload length
            0x08,             // audio
            0x01, 0x00, 0x00, 0x00,
            0x01, 0x00, // two bytes of the four of the extended timestamp
        };
        MockBufferIO io;
        io.append(chunk, sizeof(chunk));
        SrsProtocol proto(&io);

        SrsRtmpCommonMessage *msg = NULL;
        rtmp_stack_expect_error(proto.recv_message(&msg), ERROR_SOCKET_READ, "read 4 bytes ext timestamp", "ext timestamp");
        EXPECT_TRUE(msg == NULL);
    }
}

// A malformed peer starts a chunk stream with a fmt=2 chunk, which has no message length or type
// for the stream to take. RTMP allows only fmt=0 there (and SRS takes fmt=1 for librtmp's ping),
// so the connection is refused.
VOID TEST(ProtocolRtmpStackTest, FreshChunkStreamStartsWithFmt2_Refused)
{
    MockBufferIO io;
    io.append(string("\x83\x00\x00\x28", 4)); // fmt=2, cid=3, timestamp delta 40
    SrsProtocol proto(&io);

    SrsRtmpCommonMessage *msg = NULL;
    rtmp_stack_expect_error(proto.recv_message(&msg), ERROR_RTMP_CHUNK_START, "fresh chunk expect fmt=0, actual=2", "fmt=2");
    EXPECT_TRUE(msg == NULL);
}

// FFmpeg publishes its timecode as a 4-byte AMF0 data message that starts with 0x00 (#3803),
// which is no AMF0 data the server knows. It is ignored with no packet, and the publisher's next
// data message, its metadata, is decoded as usual.
VOID TEST(ProtocolRtmpStackTest, FfmpegTimecodeDataMessage_Ignored)
{
    srs_error_t err = srs_success;

    SrsRtmpCommand *pkt = NULL;
    string timecode = rtmp_stack_chunks(4, RTMP_MSG_AMF0DataMessage, 1, string("\x00\x00\x01\x00", 4));
    HELPER_EXPECT_SUCCESS(rtmp_stack_recv_decode(timecode, &pkt));
    EXPECT_TRUE(pkt == NULL);
    srs_freep(pkt);

    SrsAmf0Object *meta = SrsAmf0Any::object();
    meta->set("width", SrsAmf0Any::number(1280));
    string metadata = rtmp_stack_chunks(4, RTMP_MSG_AMF0DataMessage, 1, RtmpStackAmf0().str("@setDataFrame").str("onMetaData").obj(meta).data_);
    HELPER_EXPECT_SUCCESS(rtmp_stack_recv_decode(metadata, &pkt));
    SrsUniquePtr<SrsRtmpCommand> got(pkt);
    SrsOnMetaDataPacket *on_metadata = dynamic_cast<SrsOnMetaDataPacket *>(got.get());
    ASSERT_TRUE(on_metadata != NULL);
    EXPECT_EQ("onMetaData", on_metadata->name_);
    ASSERT_TRUE(on_metadata->metadata_->get_property("width") != NULL);
    EXPECT_EQ(1280, on_metadata->metadata_->get_property("width")->to_number());
}

// A publisher sets no acknowledgement window, so the server's in_ack_size (2500000 here) decides:
// after each half window received, the server acks the bytes received. On a long publish the
// count reaches 0xf0000000 (3.75 GB), and the sequence restarts from the bytes received since
// the last ack instead of overflowing.
VOID TEST(ProtocolRtmpStackTest, LongPublishPasses4GB_AckSequenceRestarts)
{
    srs_error_t err = srs_success;

    MockBufferIO io;
    SrsProtocol proto(&io);
    HELPER_EXPECT_SUCCESS(proto.set_in_window_ack_size(2500000));

    string audio = rtmp_stack_chunks(4, RTMP_MSG_AudioMessage, 1, string("\xaf\x01\x21\x10", 4));

    // Two GB into the session.
    io.rbytes = 0x80000000LL;
    io.append(audio);
    SrsRtmpCommonMessage *msg = NULL;
    HELPER_EXPECT_SUCCESS(proto.recv_message(&msg));
    srs_freep(msg);
    int64_t first = io.rbytes;

    // Almost 4 GB into the session.
    io.rbytes = 0xf8000000LL;
    io.append(audio);
    HELPER_EXPECT_SUCCESS(proto.recv_message(&msg));
    srs_freep(msg);
    int64_t second = io.rbytes;

    // The publisher reads two acks.
    MockBufferIO publisher;
    publisher.append(string(io.out_buffer.bytes(), io.out_buffer.length()));
    SrsProtocol peer(&publisher);

    uint32_t want[] = {(uint32_t)first, (uint32_t)(second - first)};
    for (int i = 0; i < 2; i++) {
        HELPER_ASSERT_SUCCESS(peer.recv_message(&msg));
        SrsUniquePtr<SrsRtmpCommonMessage> m(msg);
        SrsRtmpCommand *pkt_raw = NULL;
        HELPER_ASSERT_SUCCESS(peer.decode_message(m.get(), &pkt_raw));
        SrsUniquePtr<SrsRtmpCommand> pkt(pkt_raw);
        SrsAcknowledgementPacket *ack = dynamic_cast<SrsAcknowledgementPacket *>(pkt.get());
        ASSERT_TRUE(ack != NULL) << i;
        EXPECT_EQ(want[i], ack->sequence_number_) << i;
    }

    // And nothing else.
    rtmp_stack_expect_error(peer.recv_message(&msg), ERROR_SOCKET_READ, "read", "no third message");
}

// The server answers an ack or a ping the moment it receives the message that makes it due. When
// the peer's connection is already reset, that write fails, and receiving fails with it, so the
// connection ends at once.
VOID TEST(ProtocolRtmpStackTest, PeerGoneWhenAutoResponseDue_RecvFails)
{
    srs_error_t err = srs_success;

    // An ack is due after half the window.
    if (true) {
        MockBufferIO io;
        io.out_err = srs_error_new(ERROR_SOCKET_WRITE, "connection reset by peer");
        SrsProtocol proto(&io);
        HELPER_EXPECT_SUCCESS(proto.set_in_window_ack_size(2500000));

        io.rbytes = 2000000;
        io.append(rtmp_stack_chunks(4, RTMP_MSG_AudioMessage, 1, string("\xaf\x01\x21\x10", 4)));
        SrsRtmpCommonMessage *msg = NULL;
        rtmp_stack_expect_error(proto.recv_message(&msg), ERROR_SOCKET_WRITE, "send ack", "ack");
        EXPECT_TRUE(msg == NULL);
    }

    // A ping request from the peer, as FMS-style clients send to measure the round trip.
    if (true) {
        MockBufferIO io;
        io.out_err = srs_error_new(ERROR_SOCKET_WRITE, "connection reset by peer");
        SrsProtocol proto(&io);

        io.append(rtmp_stack_chunks(2, RTMP_MSG_UserControlMessage, 0, string("\x00\x06\x00\x00\x0d\x0f", 6)));
        SrsRtmpCommonMessage *msg = NULL;
        rtmp_stack_expect_error(proto.recv_message(&msg), ERROR_SOCKET_WRITE, "ping response", "ping");
        EXPECT_TRUE(msg == NULL);
    }
}

// The receive thread of a connection turns off the auto response, so an ack that falls due is
// queued and sent after the next message the server sends. When the connection is reset right
// after that message, the queued ack fails, and the send fails with it.
VOID TEST(ProtocolRtmpStackTest, QueuedAckFailsAfterSend_SendFails)
{
    srs_error_t err = srs_success;

    string audio = rtmp_stack_chunks(4, RTMP_MSG_AudioMessage, 1, string("\xaf\x01\x21\x10", 4));

    // After a command, such as the onStatus of an unpublish.
    if (true) {
        RtmpStackResetIO io(1);
        SrsProtocol proto(&io);
        proto.set_auto_response(false);
        HELPER_EXPECT_SUCCESS(proto.set_in_window_ack_size(2500000));

        io.rbytes = 2000000;
        io.append(audio);
        SrsRtmpCommonMessage *msg = NULL;
        HELPER_EXPECT_SUCCESS(proto.recv_message(&msg));
        srs_freep(msg);
        EXPECT_EQ(0, io.out_length()); // The ack is queued.

        rtmp_stack_expect_error(proto.send_and_free_packet(new SrsOnStatusCallPacket(), 1), ERROR_SOCKET_WRITE, "manual flush response", "command");
        EXPECT_EQ(2, io.writevs_tried_);
    }

    // After media, such as a batch of frames to a player.
    if (true) {
        RtmpStackResetIO io(1);
        SrsProtocol proto(&io);
        proto.set_auto_response(false);
        HELPER_EXPECT_SUCCESS(proto.set_in_window_ack_size(2500000));

        io.rbytes = 2000000;
        io.append(audio);
        SrsRtmpCommonMessage *msg = NULL;
        HELPER_EXPECT_SUCCESS(proto.recv_message(&msg));
        srs_freep(msg);

        rtmp_stack_expect_error(proto.send_and_free_message(http_conn_aac_packet(40), 1), ERROR_SOCKET_WRITE, "manual flush response", "media");
        EXPECT_EQ(2, io.writevs_tried_);
    }
}

// A malformed peer sends a protocol control message shorter than its fields. Each is refused,
// which ends the connection, rather than read past the message.
VOID TEST(ProtocolRtmpStackTest, PeerSendsControlMessagesCutShort_Refused)
{
    struct {
        const char *label;
        uint8_t type;
        const char *bytes;
        int size;
        int code;
    } cases[] = {
        {"set chunk size of 2 bytes", RTMP_MSG_SetChunkSize, "\x00\x00", 2, ERROR_RTMP_MESSAGE_DECODE},
        {"window ack size of 3 bytes", RTMP_MSG_WindowAcknowledgementSize, "\x00\x26\x25", 3, ERROR_RTMP_MESSAGE_DECODE},
        {"user control with no event type", RTMP_MSG_UserControlMessage, "\x00", 1, ERROR_RTMP_MESSAGE_DECODE},
        {"ping with 2 bytes of its timestamp", RTMP_MSG_UserControlMessage, "\x00\x06\x00\x00", 4, ERROR_RTMP_MESSAGE_DECODE},
        {"FMS event with no data", RTMP_MSG_UserControlMessage, "\x00\x1a", 2, ERROR_RTMP_MESSAGE_DECODE},
        // The code says encode, but it is the decode of a set buffer length with no length.
        {"set buffer length with no length", RTMP_MSG_UserControlMessage, "\x00\x03\x00\x00\x00\x01", 6, ERROR_RTMP_MESSAGE_ENCODE},
    };

    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        MockBufferIO io;
        io.append(rtmp_stack_chunks(2, cases[i].type, 0, string(cases[i].bytes, cases[i].size)));
        SrsProtocol proto(&io);

        SrsRtmpCommonMessage *msg = NULL;
        rtmp_stack_expect_error(proto.recv_message(&msg), cases[i].code, "decode message", cases[i].label);
        EXPECT_TRUE(msg == NULL) << cases[i].label;
        EXPECT_EQ(0, io.out_length()) << cases[i].label;
    }
}

// An edge or ingest pulls from an Adobe Media Server, which sends the FMS control event 0x1a with
// a 1-byte event data, not the 4 bytes of the other events. It is taken with its one byte, and
// the server sends nothing back for it.
VOID TEST(ProtocolRtmpStackTest, EdgePullsFromFmsWithEvent0_DecodedWithOneByte)
{
    srs_error_t err = srs_success;

    MockBufferIO io;
    io.append(rtmp_stack_chunks(2, RTMP_MSG_UserControlMessage, 0, string("\x00\x1a\x01", 3)));
    SrsRtmpClient client(&io);

    SrsRtmpCommonMessage *msg_raw = NULL;
    HELPER_ASSERT_SUCCESS(client.recv_message(&msg_raw));
    SrsUniquePtr<SrsRtmpCommonMessage> msg(msg_raw);

    SrsRtmpCommand *pkt_raw = NULL;
    HELPER_ASSERT_SUCCESS(client.decode_message(msg.get(), &pkt_raw));
    SrsUniquePtr<SrsRtmpCommand> pkt(pkt_raw);
    SrsUserControlPacket *event = dynamic_cast<SrsUserControlPacket *>(pkt.get());
    ASSERT_TRUE(event != NULL);
    EXPECT_EQ(SrsPCUCFmsEvent0, event->event_type_);
    EXPECT_EQ(1, event->event_data_);

    EXPECT_EQ(0, io.out_length());
}

// A vhost with chunk_size 128, the smallest allowed, and a 200 KB key frame: the frame takes about
// 1,600 chunks, more than the 1,024 the server's iovec array holds at first. The array grows, and
// the player gets the whole frame intact.
VOID TEST(ProtocolRtmpStackTest, PlayerKeyFrameOverIovCapacity_DeliveredIntact)
{
    srs_error_t err = srs_success;

    int size = 200 * 1024;
    string frame(size, '\0');
    frame[0] = 0x17; // AVC key frame
    frame[1] = 0x01; // NALU
    for (int i = 2; i < size; i++) {
        frame[i] = (char)(i % 251);
    }

    MockBufferIO io;
    SrsProtocol proto(&io);
    int iovs_before = proto.nb_out_iovs_;

    SrsMediaPacket *msg = new SrsMediaPacket();
    msg->timestamp_ = 40;
    msg->message_type_ = SrsFrameTypeVideo;
    char *data = new char[size];
    memcpy(data, frame.data(), size);
    msg->wrap(data, size);
    HELPER_ASSERT_SUCCESS(proto.send_and_free_message(msg, 1));
    EXPECT_GT(proto.nb_out_iovs_, iovs_before);

    // The player reads it at the same chunk size.
    MockBufferIO player;
    player.append(string(io.out_buffer.bytes(), io.out_buffer.length()));
    SrsProtocol peer(&player);
    SrsRtmpCommonMessage *got_raw = NULL;
    HELPER_ASSERT_SUCCESS(peer.recv_message(&got_raw));
    SrsUniquePtr<SrsRtmpCommonMessage> got(got_raw);
    EXPECT_TRUE(got->header_.is_video());
    EXPECT_EQ(40, got->header_.timestamp_);
    ASSERT_EQ(size, got->size());
    EXPECT_EQ(0, memcmp(frame.data(), got->payload(), size));
}

// A malformed or broken client sends a command cut short, or with a value of the wrong type in a
// field. The server refuses it at that field instead of reading past the message.
VOID TEST(ProtocolRtmpStackTest, ClientCommandsCutShort_Refused)
{
    SrsAmf0Object *connect_obj = SrsAmf0Any::object();
    connect_obj->set("app", SrsAmf0Any::str("live"));
    RtmpStackAmf0 connect = RtmpStackAmf0().str("connect").num(1).obj(connect_obj);

    struct {
        const char *label;
        RtmpStackAmf0 cmd;
        int code;
        const char *field;
    } cases[] = {
        {"connect with no transaction id", RtmpStackAmf0().str("connect"), ERROR_RTMP_AMF0_DECODE, "transaction_id"},
        // A Flash client whose objectEncoding is 3 puts an AMF3 value, marker 0x11, after the object.
        {"connect with AMF3 args", RtmpStackAmf0(connect).raw("\x11\x0a", 2), ERROR_RTMP_AMF0_INVALID, "args"},
        {"connect with args object cut", RtmpStackAmf0(connect).raw("\x03\x00\x05token", 8), ERROR_RTMP_AMF0_DECODE, "args"},
        {"createStream with no transaction id", RtmpStackAmf0().str("createStream"), ERROR_RTMP_AMF0_DECODE, "transaction_id"},
        {"closeStream with no transaction id", RtmpStackAmf0().str("closeStream"), ERROR_RTMP_AMF0_DECODE, "transaction_id"},
        {"FCPublish with no transaction id", RtmpStackAmf0().str("FCPublish"), ERROR_RTMP_AMF0_DECODE, "transaction_id"},
        {"releaseStream with no stream", RtmpStackAmf0().str("releaseStream").num(2).null(), ERROR_RTMP_AMF0_DECODE, "stream_name"},
        {"publish with no transaction id", RtmpStackAmf0().str("publish"), ERROR_RTMP_AMF0_DECODE, "transaction_id"},
        {"publish with no stream", RtmpStackAmf0().str("publish").num(5).null(), ERROR_RTMP_AMF0_DECODE, "stream_name"},
        {"publish with a number as type", RtmpStackAmf0().str("publish").num(5).null().str("livestream").num(0), ERROR_RTMP_AMF0_DECODE, "publish type"},
        {"pause with no transaction id", RtmpStackAmf0().str("pause"), ERROR_RTMP_AMF0_DECODE, "transaction_id"},
        {"pause with no flag", RtmpStackAmf0().str("pause").num(0).null(), ERROR_RTMP_AMF0_DECODE, "is_pause"},
        {"pause with no time", RtmpStackAmf0().str("pause").num(0).null().boolean(true), ERROR_RTMP_AMF0_DECODE, "time"},
        {"play with no transaction id", RtmpStackAmf0().str("play"), ERROR_RTMP_AMF0_DECODE, "transaction_id"},
        {"play with no stream", RtmpStackAmf0().str("play").num(4).null(), ERROR_RTMP_AMF0_DECODE, "stream_name"},
        {"play with a string as start", RtmpStackAmf0().str("play").num(4).null().str("livestream").str("0"), ERROR_RTMP_AMF0_DECODE, "start"},
        {"play with a string as duration", RtmpStackAmf0().str("play").num(4).null().str("livestream").num(-2).str("-1"), ERROR_RTMP_AMF0_DECODE, "duration"},
        {"play with reset cut", RtmpStackAmf0().str("play").num(4).null().str("livestream").num(-2).num(-1).raw("\x01", 1), ERROR_RTMP_AMF0_DECODE, "reset"},
        {"call with object cut", RtmpStackAmf0().str("FCSubscribe").num(3).raw("\x02\x00", 2), ERROR_RTMP_AMF0_DECODE, "command_object"},
        {"call with AMF3 args", RtmpStackAmf0().str("FCSubscribe").num(3).null().raw("\x11\x0a", 2), ERROR_RTMP_AMF0_INVALID, "discovery args"},
        {"call with args cut", RtmpStackAmf0().str("FCSubscribe").num(3).null().raw("\x00\x40", 2), ERROR_RTMP_AMF0_DECODE, "read args"},
    };

    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        SrsRtmpCommand *pkt = NULL;
        rtmp_stack_expect_error(rtmp_stack_recv_decode(rtmp_stack_command(cases[i].cmd, 1), &pkt), cases[i].code, cases[i].field, cases[i].label);
        EXPECT_TRUE(pkt == NULL) << cases[i].label;
        srs_freep(pkt);
    }
}

// A client passes a string after the connect object, such as a token, where RTMP puts optional
// user arguments as an object (#186). The connect is accepted and the string dropped, so the
// request has no args.
VOID TEST(ProtocolRtmpStackTest, ConnectWithStringArgs_ArgsDropped)
{
    srs_error_t err = srs_success;

    SrsAmf0Object *obj = SrsAmf0Any::object();
    obj->set("app", SrsAmf0Any::str("live"));
    obj->set("tcUrl", SrsAmf0Any::str("rtmp://127.0.0.1/live"));
    RtmpStackAmf0 connect = RtmpStackAmf0().str("connect").num(1).obj(obj).str("token=abc");

    MockBufferIO io;
    io.append(rtmp_stack_command(connect));
    SrsRtmpServer rtmp(&io);

    SrsRequest req;
    HELPER_ASSERT_SUCCESS(rtmp.connect_app(&req));
    EXPECT_EQ("rtmp://127.0.0.1/live", req.tcUrl_);
    EXPECT_EQ("live", req.app_);
    EXPECT_TRUE(req.args_ == NULL);
}

// A Flash player closes its NetStream, which sends closeStream(0, null) on its stream. The server
// decodes it as a close, which ends the play.
VOID TEST(ProtocolRtmpStackTest, FlashPlayerClosesStream_CloseStreamDecoded)
{
    srs_error_t err = srs_success;

    SrsRtmpCommand *pkt_raw = NULL;
    HELPER_ASSERT_SUCCESS(rtmp_stack_recv_decode(rtmp_stack_command(RtmpStackAmf0().str("closeStream").num(0).null(), 1), &pkt_raw));
    SrsUniquePtr<SrsRtmpCommand> pkt(pkt_raw);

    SrsCloseStreamPacket *close = dynamic_cast<SrsCloseStreamPacket *>(pkt.get());
    ASSERT_TRUE(close != NULL);
    EXPECT_EQ("closeStream", close->command_name_);
    EXPECT_EQ(0, close->transaction_id_);
}

// An encoder sends @setDataFrame onMetaData with no metadata after the name. It is taken as an
// empty metadata, not refused.
VOID TEST(ProtocolRtmpStackTest, PublisherMetadataWithEmptyBody_Accepted)
{
    srs_error_t err = srs_success;

    string metadata = rtmp_stack_chunks(4, RTMP_MSG_AMF0DataMessage, 1, RtmpStackAmf0().str("@setDataFrame").str("onMetaData").data_);
    SrsRtmpCommand *pkt_raw = NULL;
    HELPER_ASSERT_SUCCESS(rtmp_stack_recv_decode(metadata, &pkt_raw));
    SrsUniquePtr<SrsRtmpCommand> pkt(pkt_raw);

    SrsOnMetaDataPacket *on_metadata = dynamic_cast<SrsOnMetaDataPacket *>(pkt.get());
    ASSERT_TRUE(on_metadata != NULL);
    EXPECT_EQ("onMetaData", on_metadata->name_);
    EXPECT_EQ(0, on_metadata->metadata_->count());
}

// The connect reply of an origin, on chunk stream 3, as the edge or forwarder receives it.
static srs_error_t rtmp_stack_edge_connect(const RtmpStackAmf0 &reply, SrsServerInfo *si)
{
    MockBufferIO io;
    io.append(rtmp_stack_command(reply));
    SrsRtmpClient client(&io);

    SrsRequest req;
    req.tcUrl_ = "rtmp://127.0.0.1/live";
    return client.connect_app("live", req.tcUrl_, &req, false, si);
}

// An origin that refuses the app answers connect with _error, as nginx-rtmp and others do with
// NetConnection.Connect.Rejected, or sends a reply cut inside its properties. The edge's connect
// fails, so it does not go on to create a stream.
VOID TEST(ProtocolRtmpStackTest, OriginConnectReplyNotAResult_ConnectFails)
{
    SrsAmf0Object *info = SrsAmf0Any::object();
    info->set("level", SrsAmf0Any::str("error"));
    info->set("code", SrsAmf0Any::str("NetConnection.Connect.Rejected"));
    SrsServerInfo si;
    rtmp_stack_expect_error(rtmp_stack_edge_connect(RtmpStackAmf0().str("_error").num(1).null().obj(info), &si),
                            ERROR_RTMP_AMF0_DECODE, "command_name=_error", "_error");

    rtmp_stack_expect_error(rtmp_stack_edge_connect(RtmpStackAmf0().str("_result").num(1).raw("\x03\x00\x06""fmsVer", 9), &si),
                            ERROR_RTMP_AMF0_DECODE, "args", "props cut");
}

// A Red5 origin (1.0.6) answers connect with null where the properties object goes (#418). The
// null is ignored, and the edge connects with the status in the information object.
VOID TEST(ProtocolRtmpStackTest, Red5OriginConnectReplyWithNullProps_Connected)
{
    srs_error_t err = srs_success;

    SrsAmf0Object *info = SrsAmf0Any::object();
    info->set("level", SrsAmf0Any::str("status"));
    info->set("code", SrsAmf0Any::str("NetConnection.Connect.Success"));
    info->set("objectEncoding", SrsAmf0Any::number(0));

    SrsServerInfo si;
    HELPER_EXPECT_SUCCESS(rtmp_stack_edge_connect(RtmpStackAmf0().str("_result").num(1).null().obj(info), &si));
    EXPECT_EQ("", si.ip_); // Red5 sends no SRS server data.
}

// The origin answers createStream with _error, such as an origin at its stream limit, or with a
// _result that has no stream id. The edge's createStream fails instead of playing on a stream id
// the origin never gave.
VOID TEST(ProtocolRtmpStackTest, OriginRejectsCreateStream_CreateStreamFails)
{
    struct {
        const char *label;
        RtmpStackAmf0 reply;
        const char *want;
    } cases[] = {
        {"_error", RtmpStackAmf0().str("_error").num(2).null().obj(SrsAmf0Any::object()), "command_name=_error"},
        {"no stream id", RtmpStackAmf0().str("_result").num(2).null(), "stream_id"},
    };

    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        MockBufferIO io;
        io.append(rtmp_stack_command(cases[i].reply));
        SrsRtmpClient client(&io);

        int stream_id = -1;
        rtmp_stack_expect_error(client.create_stream(stream_id), ERROR_RTMP_AMF0_DECODE, cases[i].want, cases[i].label);
        EXPECT_EQ(-1, stream_id) << cases[i].label;
    }
}

// The origin resets the edge's or forwarder's connection at each message it sends while it
// connects, creates the stream and starts to play or publish, such as an origin restarting. The
// step fails with the write error, and nothing after it is sent.
VOID TEST(ProtocolRtmpStackTest, OriginResetsWhileEdgeConnects_EachSendFails)
{
    SrsRequest req;
    req.tcUrl_ = "rtmp://127.0.0.1/live";

    // connect, then the window ack size.
    for (int sent = 0; sent < 2; sent++) {
        RtmpStackResetIO io(sent);
        SrsRtmpClient client(&io);
        SrsServerInfo si;
        rtmp_stack_expect_error(client.connect_app("live", req.tcUrl_, &req, false, &si), ERROR_SOCKET_WRITE, "send packet", "connect");
        EXPECT_EQ(sent + 1, io.writevs_tried_);
    }

    if (true) {
        RtmpStackResetIO io(0);
        SrsRtmpClient client(&io);
        int stream_id = 0;
        rtmp_stack_expect_error(client.create_stream(stream_id), ERROR_SOCKET_WRITE, "send packet", "createStream");
    }

    // play, then the buffer length, then the chunk size of 60000.
    if (true) {
        RtmpStackResetIO io(1);
        SrsRtmpClient client(&io);
        rtmp_stack_expect_error(client.play("livestream", 1, 60000), ERROR_SOCKET_WRITE, "send set buffer length failed", "buffer length");
    }
    if (true) {
        RtmpStackResetIO io(2);
        SrsRtmpClient client(&io);
        rtmp_stack_expect_error(client.play("livestream", 1, 60000), ERROR_SOCKET_WRITE, "send set chunk size failed", "play chunk size");
    }

    // The chunk size of 60000, then publish.
    if (true) {
        RtmpStackResetIO io(1);
        SrsRtmpClient client(&io);
        rtmp_stack_expect_error(client.publish("livestream", 1, 60000), ERROR_SOCKET_WRITE, "send publish message failed", "publish");
    }
}

// One step of the server's replies to a client that connects, as SrsRtmpConn sends them.
static srs_error_t rtmp_stack_server_connect_step(SrsRtmpServer *rtmp, int step)
{
    SrsRequest req;
    req.tcUrl_ = "rtmp://127.0.0.1/live";
    bool accepted = false;

    switch (step) {
    case 0:
        return rtmp->connect_app(&req);
    case 1:
        return rtmp->set_window_ack_size(2500000);
    case 2:
        return rtmp->set_peer_bandwidth(2500000, 2);
    case 3:
        return rtmp->response_connect_app(&req, "127.0.0.1");
    case 4:
        return rtmp->on_bw_done();
    case 5:
        return rtmp->set_chunk_size(60000);
    case 6:
        return rtmp->start_publishing(1);
    default: {
        srs_error_t err = rtmp->redirect(&req, "rtmp://127.0.0.2/live/livestream", accepted);
        EXPECT_FALSE(accepted);
        return err;
    }
    }
}

// The client resets its connection at each step of the server's replies to its connect: before
// connect, or at the window ack size, peer bandwidth, connect result, onBWDone, chunk size,
// publish start or a redirect. Each step fails with the error, so the connection ends there.
VOID TEST(ProtocolRtmpStackTest, ClientResetsDuringConnectReplies_EachReplyFails)
{
    const char *wants[] = {
        "expect connect app response", "send ack", "send set peer bandwidth", "send connect app response",
        "send onBWDone", "send set chunk size", "send NetStream.Publish.Start", "send redirect/reject",
    };
    for (int i = 0; i < (int)(sizeof(wants) / sizeof(wants[0])); i++) {
        RtmpStackResetIO io(0);
        SrsRtmpServer rtmp(&io);
        int code = (i == 0) ? ERROR_SOCKET_READ : ERROR_SOCKET_WRITE;
        rtmp_stack_expect_error(rtmp_stack_server_connect_step(&rtmp, i), code, wants[i], wants[i]);
        EXPECT_EQ(0, io.out_length()) << wants[i];
    }

    // The reject is the last message to a client the server refuses; its failure is only logged.
    RtmpStackResetIO io(0);
    SrsRtmpServer rtmp(&io);
    SrsRequest req;
    rtmp.response_connect_reject(&req, "denied by hook");
    EXPECT_EQ(1, io.writevs_tried_);
    EXPECT_EQ(0, io.out_length());
}

// A player resets its connection at each message the server sends to start the play, or to pause
// and resume it. The step fails with the write error, and nothing after it is sent; with no reset
// the player gets every message in order.
VOID TEST(ProtocolRtmpStackTest, PlayerResetsDuringPlayStartOrPause_StepFails)
{
    srs_error_t err = srs_success;

    const char *play_steps[] = {"send StreamBegin", "send NetStream.Play.Reset", "send NetStream.Play.Start", "send |RtmpSampleAccess true"};
    for (int i = 0; i < 4; i++) {
        RtmpStackResetIO io(i);
        SrsRtmpServer rtmp(&io);
        rtmp_stack_expect_error(rtmp.start_play(1), ERROR_SOCKET_WRITE, play_steps[i], play_steps[i]);
        EXPECT_EQ(i + 1, io.writevs_tried_) << play_steps[i];
    }
    if (true) {
        RtmpStackResetIO io(4);
        SrsRtmpServer rtmp(&io);
        HELPER_EXPECT_SUCCESS(rtmp.start_play(1));
        vector<pair<string, double> > sent = rtmp_stack_sent(io);
        ASSERT_EQ(4u, sent.size());
        EXPECT_EQ("type=4", sent[0].first);
        EXPECT_EQ("onStatus", sent[1].first);
        EXPECT_EQ("onStatus", sent[2].first);
        EXPECT_EQ("|RtmpSampleAccess", sent[3].first);
    }

    const char *pause_steps[] = {"send NetStream.Pause.Notify", "send StreamEOF"};
    const char *unpause_steps[] = {"send NetStream.Unpause.Notify", "send StreamBegin"};
    for (int i = 0; i < 2; i++) {
        RtmpStackResetIO io(i);
        SrsRtmpServer rtmp(&io);
        rtmp_stack_expect_error(rtmp.on_play_client_pause(1, true), ERROR_SOCKET_WRITE, pause_steps[i], pause_steps[i]);

        RtmpStackResetIO io2(i);
        SrsRtmpServer rtmp2(&io2);
        rtmp_stack_expect_error(rtmp2.on_play_client_pause(1, false), ERROR_SOCKET_WRITE, unpause_steps[i], unpause_steps[i]);
    }
}

// The messages FFmpeg and OBS send to publish after releaseStream: FCPublish, createStream and
// publish, as the FMLE flow has them.
static string rtmp_stack_fmle_publish_messages(int count)
{
    SrsCreateStreamPacket *create_stream = new SrsCreateStreamPacket();
    create_stream->transaction_id_ = 4;
    SrsPublishPacket *publish = new SrsPublishPacket();
    publish->transaction_id_ = 5;
    publish->stream_name_ = "livestream";

    string payloads[] = {
        rtmp_stack_payload(SrsFMLEStartPacket::create_FC_publish("livestream")),
        rtmp_stack_payload(create_stream),
        rtmp_stack_payload(publish),
    };
    uint32_t stream_ids[] = {0, 0, 1};

    string b;
    for (int i = 0; i < count; i++) {
        b += rtmp_stack_chunks(3, RTMP_MSG_AMF0CommandMessage, stream_ids[i], payloads[i]);
    }
    return b;
}

// A publisher resets or closes its connection at each step of starting or stopping a publish:
// the FMLE flow of FFmpeg and OBS, the Haivision flow, and the unpublish. Each step fails with the
// error, so the server never takes the stream as published or sends what follows.
VOID TEST(ProtocolRtmpStackTest, PublisherResetsDuringPublishStartOrStop_StepFails)
{
    srs_error_t err = srs_success;

    // The client leaves before each of its messages.
    const char *recv_steps[] = {"recv FCPublish", "recv createStream", "recv publish"};
    for (int i = 0; i < 3; i++) {
        RtmpStackResetIO io(3);
        io.append(rtmp_stack_fmle_publish_messages(i));
        SrsRtmpServer rtmp(&io);
        rtmp_stack_expect_error(rtmp.start_fmle_publish(1), ERROR_SOCKET_READ, recv_steps[i], recv_steps[i]);
        EXPECT_EQ(i, io.writevs_tried_) << recv_steps[i];
    }

    // The client resets at each reply.
    const char *send_steps[] = {"send FCPublish response", "send createStream response", "send NetStream.Publish.Start"};
    for (int i = 0; i < 3; i++) {
        RtmpStackResetIO io(i);
        io.append(rtmp_stack_fmle_publish_messages(3));
        SrsRtmpServer rtmp(&io);
        rtmp_stack_expect_error(rtmp.start_fmle_publish(1), ERROR_SOCKET_WRITE, send_steps[i], send_steps[i]);
        EXPECT_EQ(i + 1, io.writevs_tried_) << send_steps[i];
    }
    if (true) {
        RtmpStackResetIO io(3);
        io.append(rtmp_stack_fmle_publish_messages(3));
        SrsRtmpServer rtmp(&io);
        HELPER_EXPECT_SUCCESS(rtmp.start_fmle_publish(1));
        vector<pair<string, double> > sent = rtmp_stack_sent(io);
        ASSERT_EQ(3u, sent.size());
        EXPECT_EQ(3, sent[0].second); // FCPublish
        EXPECT_EQ(4, sent[1].second); // createStream
        EXPECT_EQ("onFCPublish", sent[2].first);
    }

    // Haivision, which sends publish after its FCPublish was answered while identifying.
    if (true) {
        RtmpStackResetIO io(1);
        SrsRtmpServer rtmp(&io);
        rtmp_stack_expect_error(rtmp.start_haivision_publish(1), ERROR_SOCKET_READ, "recv publish", "haivision recv");
    }
    if (true) {
        SrsPublishPacket *publish = new SrsPublishPacket();
        publish->stream_name_ = "livestream";
        RtmpStackResetIO io(0);
        io.append(rtmp_stack_chunks(3, RTMP_MSG_AMF0CommandMessage, 1, rtmp_stack_payload(publish)));
        SrsRtmpServer rtmp(&io);
        rtmp_stack_expect_error(rtmp.start_haivision_publish(1), ERROR_SOCKET_WRITE, "send NetStream.Publish.Start", "haivision send");
    }

    // The unpublish replies to FCUnpublish.
    const char *unpublish_steps[] = {"send NetStream.unpublish.Success", "send FCUnpublish response", "send NetStream.Unpublish.Success"};
    for (int i = 0; i < 3; i++) {
        RtmpStackResetIO io(i);
        SrsRtmpServer rtmp(&io);
        rtmp_stack_expect_error(rtmp.fmle_unpublish(1, 6), ERROR_SOCKET_WRITE, unpublish_steps[i], unpublish_steps[i]);
        EXPECT_EQ(i + 1, io.writevs_tried_) << unpublish_steps[i];
    }
}

// The client resets its connection when the server answers its first command, while the server
// identifies whether it plays or publishes: releaseStream (FMLE), createStream, FCPublish after
// createStream (Haivision), or a _checkbw call. The identify fails with the write error.
VOID TEST(ProtocolRtmpStackTest, ClientResetsDuringIdentify_ResponseFails)
{
    string release_stream = rtmp_stack_chunks(3, RTMP_MSG_AMF0CommandMessage, 0, rtmp_stack_payload(SrsFMLEStartPacket::create_release_stream("livestream")));
    string create_stream = rtmp_stack_chunks(3, RTMP_MSG_AMF0CommandMessage, 0, rtmp_stack_payload(new SrsCreateStreamPacket()));
    string fc_publish = rtmp_stack_chunks(3, RTMP_MSG_AMF0CommandMessage, 0, rtmp_stack_payload(SrsFMLEStartPacket::create_FC_publish("livestream")));
    string checkbw = rtmp_stack_command(RtmpStackAmf0().str("_checkbw").num(2).null());

    struct {
        const char *label;
        string messages;
        int sent;
        const char *want;
    } cases[] = {
        {"releaseStream", release_stream, 0, "send releaseStream response"},
        {"createStream", create_stream, 0, "send createStream response"},
        {"Haivision FCPublish", create_stream + fc_publish, 1, "send FCPublish"},
        {"_checkbw", checkbw, 0, "response call"},
    };

    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        RtmpStackResetIO io(cases[i].sent);
        io.append(cases[i].messages);
        SrsRtmpServer rtmp(&io);

        SrsRtmpConnType type = SrsRtmpConnUnknown;
        string stream;
        srs_utime_t duration = 0;
        rtmp_stack_expect_error(rtmp.identify_client(1, type, stream, duration), ERROR_SOCKET_WRITE, cases[i].want, cases[i].label);
        EXPECT_EQ(cases[i].sent + 1, io.writevs_tried_) << cases[i].label;
    }
}

// The play of a client that sends a command the server does not handle before or after its
// createStream: librtmp with live=1 sends FCSubscribe before createStream, and FFmpeg sends
// getStreamLength between createStream and play. FCSubscribe gets a null _result, getStreamLength
// is ignored, and the client is identified as a player of its stream either way.
VOID TEST(ProtocolRtmpStackTest, PlayerSendsFcSubscribeOrGetStreamLength_IdentifiedAsPlayer)
{
    srs_error_t err = srs_success;

    SrsCreateStreamPacket *create_stream = new SrsCreateStreamPacket();
    create_stream->transaction_id_ = 4;
    string create = rtmp_stack_chunks(3, RTMP_MSG_AMF0CommandMessage, 0, rtmp_stack_payload(create_stream));
    SrsPlayPacket *play_pkt = new SrsPlayPacket();
    play_pkt->stream_name_ = "livestream";
    string play = rtmp_stack_chunks(8, RTMP_MSG_AMF0CommandMessage, 1, rtmp_stack_payload(play_pkt));

    // librtmp, live=1.
    if (true) {
        string fc_subscribe = rtmp_stack_command(RtmpStackAmf0().str("FCSubscribe").num(3).null().str("livestream"));
        MockBufferIO io;
        io.append(fc_subscribe + create + play);
        SrsRtmpServer rtmp(&io);

        SrsRtmpConnType type = SrsRtmpConnUnknown;
        string stream;
        srs_utime_t duration = 0;
        HELPER_ASSERT_SUCCESS(rtmp.identify_client(1, type, stream, duration));
        EXPECT_EQ(SrsRtmpConnPlay, type);
        EXPECT_EQ("livestream", stream);

        vector<pair<string, double> > sent = rtmp_stack_sent(io);
        ASSERT_EQ(2u, sent.size());
        EXPECT_EQ("_result", sent[0].first);
        EXPECT_EQ(3, sent[0].second); // FCSubscribe
        EXPECT_EQ("_result", sent[1].first);
        EXPECT_EQ(4, sent[1].second); // createStream
    }

    // FFmpeg.
    if (true) {
        string get_stream_length = rtmp_stack_command(RtmpStackAmf0().str("getStreamLength").num(5).null().str("livestream"), 1);
        MockBufferIO io;
        io.append(create + get_stream_length + play);
        SrsRtmpServer rtmp(&io);

        SrsRtmpConnType type = SrsRtmpConnUnknown;
        string stream;
        srs_utime_t duration = 0;
        HELPER_ASSERT_SUCCESS(rtmp.identify_client(1, type, stream, duration));
        EXPECT_EQ(SrsRtmpConnPlay, type);
        EXPECT_EQ("livestream", stream);

        vector<pair<string, double> > sent = rtmp_stack_sent(io);
        ASSERT_EQ(1u, sent.size());
        EXPECT_EQ(4, sent[0].second); // createStream only
    }
}

// A broken client sends createStream, then a play cut after its name. The identify fails at the
// play, after the createStream was answered, instead of waiting for another command.
VOID TEST(ProtocolRtmpStackTest, PlayCutShortAfterCreateStream_IdentifyFails)
{
    string create = rtmp_stack_chunks(3, RTMP_MSG_AMF0CommandMessage, 0, rtmp_stack_payload(new SrsCreateStreamPacket()));
    string play = rtmp_stack_command(RtmpStackAmf0().str("play"), 1);

    MockBufferIO io;
    io.append(create + play);
    SrsRtmpServer rtmp(&io);

    SrsRtmpConnType type = SrsRtmpConnUnknown;
    string stream;
    srs_utime_t duration = 0;
    rtmp_stack_expect_error(rtmp.identify_client(1, type, stream, duration), ERROR_RTMP_AMF0_DECODE, "decode identify", "play cut");
    EXPECT_EQ(1u, rtmp_stack_sent(io).size()); // The createStream result.
}

// A stream's source keeps the request of its first client. A later client of the stream that
// passed connect args, such as a token, updates the source's request, which then holds the
// later client's args instead of the first one's.
VOID TEST(ProtocolRtmpStackTest, LaterClientWithConnectArgs_SourceRequestTakesItsArgs)
{
    SrsRequest source_req;
    source_req.args_ = SrsAmf0Any::object();
    source_req.args_->set("token", SrsAmf0Any::str("first"));

    SrsRequest client_req;
    client_req.args_ = SrsAmf0Any::object();
    client_req.args_->set("token", SrsAmf0Any::str("second"));

    source_req.update_auth(&client_req);
    ASSERT_TRUE(source_req.args_ != NULL);
    ASSERT_TRUE(source_req.args_->get_property("token") != NULL);
    EXPECT_EQ("second", source_req.args_->get_property("token")->to_str());
    EXPECT_NE(source_req.args_, client_req.args_); // A copy, not shared.
}

// Packages the two slices of one frame, merged as with rtc_server.merge_nalus on, and returns the
// payload bytes each FU-A packet carries after its FU indicator and header.
static vector<int> rtp_merged_frame_fu_sizes(SrsVideoCodecId codec, vector<char> &slice0, vector<char> &slice1)
{
    vector<int> sizes;

    SrsFormat format;
    format.vcodec_ = new SrsVideoCodecConfig();
    format.vcodec_->id_ = codec;

    SrsRtpVideoBuilder builder;
    srs_error_t err = builder.initialize(&format, 0x1234, 96);
    EXPECT_TRUE(err == srs_success);
    srs_freep(err);

    SrsNaluSample s0(&slice0[0], (int)slice0.size());
    SrsNaluSample s1(&slice1[0], (int)slice1.size());
    vector<SrsNaluSample *> samples;
    samples.push_back(&s0);
    samples.push_back(&s1);

    SrsMediaPacket msg;
    msg.timestamp_ = 40;

    vector<SrsRtpPacket *> pkts;
    err = builder.package_nalus(&msg, samples, pkts);
    EXPECT_TRUE(err == srs_success);
    srs_freep(err);

    for (int i = 0; i < (int)pkts.size(); i++) {
        SrsRtpPacket *pkt = pkts[i];
        vector<SrsNaluSample *> *nalus = NULL;
        if (codec == SrsVideoCodecIdHEVC) {
            nalus = &static_cast<SrsRtpFUAPayloadHevc *>(pkt->payload())->nalus_;
        } else {
            nalus = &static_cast<SrsRtpFUAPayload *>(pkt->payload())->nalus_;
        }

        int size = 0;
        for (int j = 0; j < (int)nalus->size(); j++) {
            size += nalus->at(j)->size_;
        }
        sizes.push_back(size);
        srs_freep(pkt);
    }

    srs_freep(format.vcodec_);
    return sizes;
}

// With rtc_server.merge_nalus on, RTMP to WebRTC packs all slices of a frame into one NALU and
// cuts it into FU-A packets of up to 1200 bytes. When the merged NALU minus its header is a whole
// number of 1200-byte fragments, such as two slices of 1000 and 1398 bytes (2401 with the 3-byte
// separator), the frame must take exactly two fragments. The packet count is computed from the
// size with the header, so a third FU-A with the end bit and no data is sent.
VOID TEST(ProtocolRtpTest, DISABLED_MergedFrameOfWholeFragments_EmptyLastFuA)
{
    // H.264: two IDR slices, 1000 + 3 + 1398 = 2401 bytes, a 1-byte NALU header.
    if (true) {
        vector<char> slice0(1000, (char)0x88);
        vector<char> slice1(1398, (char)0x88);
        slice0[0] = slice1[0] = 0x65;

        vector<int> sizes = rtp_merged_frame_fu_sizes(SrsVideoCodecIdAVC, slice0, slice1);
        EXPECT_EQ(2, (int)sizes.size());
        for (int i = 0; i < (int)sizes.size(); i++) {
            EXPECT_GT(sizes[i], 0) << "FU-A " << i << " of " << sizes.size();
        }
    }

    // H.265: two TRAIL_R slices, 1000 + 3 + 1399 = 2402 bytes, a 2-byte NALU header.
    if (true) {
        vector<char> slice0(1000, (char)0x88);
        vector<char> slice1(1399, (char)0x88);
        slice0[0] = slice1[0] = 0x02;
        slice0[1] = slice1[1] = 0x01;

        vector<int> sizes = rtp_merged_frame_fu_sizes(SrsVideoCodecIdHEVC, slice0, slice1);
        EXPECT_EQ(2, (int)sizes.size());
        for (int i = 0; i < (int)sizes.size(); i++) {
            EXPECT_GT(sizes[i], 0) << "FU-A " << i << " of " << sizes.size();
        }
    }
}

#ifdef SRS_RTSP
// The TCP connection of an RTSP client, whose bytes arrive in the given segments: each read takes
// one segment, and a read after the last fails, as when the client closes the connection.
class RtspSegmentIO : public MockBufferIO
{
public:
    vector<string> segments_;

public:
    RtspSegmentIO *segment(const string &data)
    {
        segments_.push_back(data);
        return this;
    }

public:
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread)
    {
        if (segments_.empty()) {
            return srs_error_new(ERROR_SOCKET_READ, "connection closed by peer");
        }

        string data = segments_.front();
        segments_.erase(segments_.begin());
        srs_assert(data.size() <= size);
        memcpy(buf, data.data(), data.size());
        *nread = (ssize_t)data.size();
        return srs_success;
    }
};

// The 4-byte header of an interleaved frame, RFC 2326 10.12: '$', the channel and the length.
static string rtsp_interleaved_header(uint8_t channel, size_t size)
{
    string header;
    header.push_back('$');
    header.push_back((char)channel);
    header.push_back((char)((size >> 8) & 0xff));
    header.push_back((char)(size & 0xff));
    return header;
}

// The RTCP receiver report of a player on one track, RFC 3550 6.4.2: one report block about the
// server's SSRC, with the given interarrival jitter.
static string rtsp_rtcp_rr(uint32_t jitter)
{
    uint8_t rr[] = {
        0x81, 0xc9, 0x00, 0x07, // V=2, RC=1, RR, 7 words
        0x5a, 0x3c, 0x91, 0xe4, // SSRC of the player
        0x4b, 0x7e, 0x12, 0x9f, // SSRC of the server's track
        0x00, 0x00, 0x00, 0x00, // fraction and cumulative lost
        0x00, 0x00, 0x51, 0x6b, // extended highest sequence
        (uint8_t)(jitter >> 24), (uint8_t)(jitter >> 16), (uint8_t)(jitter >> 8), (uint8_t)jitter,
        0x00, 0x00, 0x00, 0x00, // LSR
        0x00, 0x00, 0x00, 0x00, // DLSR
    };
    return string((char *)rr, sizeof(rr));
}

// The SDES with the player's CNAME, which RFC 3550 6.1 puts after the RR in each compound packet.
static string rtsp_rtcp_sdes()
{
    uint8_t sdes[] = {
        0x81, 0xca, 0x00, 0x04,                         // V=2, SC=1, SDES, 4 words
        0x5a, 0x3c, 0x91, 0xe4,                         // SSRC of the player
        0x01, 0x08, 'r', 't', 's', 'p', 'p', 'l', 'a', 'y', // CNAME, 8 bytes
        0x00, 0x00,                                     // end of items, padding
    };
    return string((char *)sdes, sizeof(sdes));
}

// A player over TCP, such as VLC, sends its RTCP between requests on the RTSP connection. live555
// writes the 4-byte interleaved header and the packet with two send() calls, so the header often
// arrives alone. The parser must wait for the rest of the frame, skip it, and read the
// GET_PARAMETER keep-alive after it.
VOID TEST(ProtocolRtspStackTest, PlayerRtcpHeaderInOwnSegment_KeepAliveAfterItParsed)
{
    srs_error_t err = srs_success;

    string rtcp = rtsp_rtcp_rr(0x31) + rtsp_rtcp_sdes();
    ASSERT_EQ(52, (int)rtcp.size());

    RtspSegmentIO io;
    io.segment(rtsp_interleaved_header(1, rtcp.size()))
        ->segment(rtcp)
        ->segment("GET_PARAMETER rtsp://127.0.0.1:8554/live/livestream RTSP/1.0\r\nCSeq: 6\r\nSession: 7AB3C1D2\r\n\r\n");
    SrsRtspStack stack(&io);

    SrsRtspRequest *req = NULL;
    HELPER_ASSERT_SUCCESS(stack.recv_message(&req));
    SrsUniquePtr<SrsRtspRequest> req_uptr(req);
    EXPECT_STREQ("GET_PARAMETER", req->method_.c_str());
    EXPECT_STREQ("rtsp://127.0.0.1:8554/live/livestream", req->uri_.c_str());
    EXPECT_EQ(6, req->seq_);
    EXPECT_STREQ("7AB3C1D2", req->session_.c_str());
    EXPECT_TRUE(io.segments_.empty());
}

// A request longer than one TCP segment, such as a SETUP with a long URL, arrives cut at any byte,
// here inside the method, the version and the Transport value. The parser reads on until each
// token ends, and gets the same request as from one segment.
VOID TEST(ProtocolRtspStackTest, SetupSplitAcrossTcpSegments_ParsedWhole)
{
    srs_error_t err = srs_success;

    RtspSegmentIO io;
    io.segment("SET")
        ->segment("UP rtsp://127.0.0.1:8554/live/livestream/trackID=1 RT")
        ->segment("SP/1.0\r\nCSeq: 4\r\nTransport: RTP/AVP/TCP;uni")
        ->segment("cast;interleaved=2-3\r\nSession: 7AB3C1D2\r\n\r\n");
    SrsRtspStack stack(&io);

    SrsRtspRequest *req = NULL;
    HELPER_ASSERT_SUCCESS(stack.recv_message(&req));
    SrsUniquePtr<SrsRtspRequest> req_uptr(req);
    EXPECT_TRUE(req->is_setup());
    EXPECT_STREQ("RTSP/1.0", req->version_.c_str());
    EXPECT_EQ(4, req->seq_);
    EXPECT_EQ(1, req->stream_id_);
    EXPECT_STREQ("7AB3C1D2", req->session_.c_str());
    ASSERT_TRUE(req->transport_ != NULL);
    EXPECT_STREQ("TCP", req->transport_->lower_transport_.c_str());
    EXPECT_STREQ("unicast", req->transport_->cast_type_.c_str());
    EXPECT_EQ(2, req->transport_->interleaved_min_);
    EXPECT_EQ(3, req->transport_->interleaved_max_);
    EXPECT_TRUE(io.segments_.empty());
}

// A player using reduced-size RTCP (RFC 5506) sends a lone receiver report with no report block
// before it has received any RTP: 8 bytes, shorter than the 12 the RTCP check wants. The parser
// skips the interleaved frame whole, so the TEARDOWN after it parses.
VOID TEST(ProtocolRtspStackTest, PlayerReducedSizeRtcp_SkippedBeforeTeardown)
{
    srs_error_t err = srs_success;

    uint8_t rr[] = {0x80, 0xc9, 0x00, 0x01, 0x5a, 0x3c, 0x91, 0xe4};
    string rtcp((char *)rr, sizeof(rr));

    MockBufferIO io;
    string data = rtsp_interleaved_header(1, rtcp.size()) + rtcp +
                  "TEARDOWN rtsp://127.0.0.1:8554/live/livestream RTSP/1.0\r\nCSeq: 7\r\nSession: 7AB3C1D2\r\n\r\n";
    io.append(data);
    SrsRtspStack stack(&io);

    SrsRtspRequest *req = NULL;
    HELPER_ASSERT_SUCCESS(stack.recv_message(&req));
    SrsUniquePtr<SrsRtspRequest> req_uptr(req);
    EXPECT_TRUE(req->is_teardown());
    EXPECT_EQ(7, req->seq_);
    EXPECT_EQ(0, io.length());
}

// A SETUP whose Transport states the mode, as RFC 2326 12.39 allows (mode="PLAY", the default),
// before the interleaved channels. The mode is kept as sent, and the parameters after it still
// parse, so the server answers on the channels the client asked for.
VOID TEST(ProtocolRtspStackTest, SetupTransportWithMode_ChannelsAfterItParsed)
{
    srs_error_t err = srs_success;

    MockBufferIO io;
    io.append("SETUP rtsp://127.0.0.1:8554/live/livestream/trackID=0 RTSP/1.0\r\nCSeq: 3\r\n"
              "Transport: RTP/AVP/TCP;unicast;mode=\"PLAY\";interleaved=0-1\r\n\r\n");
    SrsRtspStack stack(&io);

    SrsRtspRequest *req = NULL;
    HELPER_ASSERT_SUCCESS(stack.recv_message(&req));
    SrsUniquePtr<SrsRtspRequest> req_uptr(req);
    ASSERT_TRUE(req->transport_ != NULL);
    EXPECT_STREQ("\"PLAY\"", req->transport_->mode_.c_str());
    EXPECT_EQ(0, req->transport_->interleaved_min_);
    EXPECT_EQ(1, req->transport_->interleaved_max_);

    // The answer, as SrsRtspConnection builds it from the request's transport.
    SrsRtspSetupResponse res((int)req->seq_);
    res.transport_->copy(req->transport_);
    res.session_ = "7AB3C1D2";
    res.ssrc_ = "1234";
    HELPER_ASSERT_SUCCESS(stack.send_message(&res));
    string out(io.out_buffer.bytes(), io.out_buffer.length());
    EXPECT_NE(string::npos, out.find("Transport: RTP/AVP/TCP;unicast;interleaved=0-1;ssrc=1234;mode=\"play\"\r\n")) << out;
}

// FFmpeg publishing with -f rtsp starts with an ANNOUNCE carrying its SDP. SRS only plays RTSP, so
// it answers 501, but it must read the request's type and body, so the SDP is not parsed as the
// next request.
VOID TEST(ProtocolRtspStackTest, FfmpegAnnounceWithSdp_TypeAndBodyRead)
{
    srs_error_t err = srs_success;

    string sdp = "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=No Name\r\nc=IN IP4 127.0.0.1\r\nt=0 0\r\n"
                 "a=tool:libavformat 61.7.100\r\nm=video 0 RTP/AVP 96\r\na=rtpmap:96 H264/90000\r\n"
                 "a=control:streamid=0\r\n";
    std::stringstream ss;
    ss << "ANNOUNCE rtsp://127.0.0.1:8554/live/livestream RTSP/1.0\r\n"
       << "Content-Type: application/sdp\r\n"
       << "CSeq: 2\r\n"
       << "User-Agent: Lavf61.7.100\r\n"
       << "Content-Length: " << sdp.length() << "\r\n\r\n"
       << sdp;

    MockBufferIO io;
    io.append(ss.str());
    SrsRtspStack stack(&io);

    SrsRtspRequest *req = NULL;
    HELPER_ASSERT_SUCCESS(stack.recv_message(&req));
    SrsUniquePtr<SrsRtspRequest> req_uptr(req);
    EXPECT_STREQ("ANNOUNCE", req->method_.c_str());
    EXPECT_STREQ("application/sdp", req->content_type_.c_str());
    EXPECT_EQ((long)sdp.length(), req->content_length_);
    EXPECT_STREQ("Lavf61.7.100", req->user_agent_.c_str());
    EXPECT_EQ(0, io.length());
}

// A player that closes its connection while it sends a request, at the version of the request line,
// inside a header name, or right after the name of each header SRS reads: the request fails with
// the read error and gives no request, so the connection ends.
VOID TEST(ProtocolRtspStackTest, PlayerClosesMidRequest_ReadErrorAndNoRequest)
{
    string requests[] = {
        "DESCRIBE rtsp://127.0.0.1:8554/live/livestream RTSP/1.0\r\nCSeq: 2\r\n"
        "User-Agent: LibVLC/3.0.20 (LIVE555 Streaming Media v2016.11.28)\r\nAccept: application/sdp\r\n"
        "x-Accept-Dynamic-Rate: 1\r\n\r\n",
        "SETUP rtsp://127.0.0.1:8554/live/livestream/trackID=0 RTSP/1.0\r\nCSeq: 3\r\n"
        "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\nSession: 7AB3C1D2\r\n\r\n",
        "PLAY rtsp://127.0.0.1:8554/live/livestream RTSP/1.0\r\nCSeq: 5\r\nSession: 7AB3C1D2\r\nRange: npt=0.000-\r\n\r\n",
        "ANNOUNCE rtsp://127.0.0.1:8554/live/livestream RTSP/1.0\r\nContent-Type: application/sdp\r\nCSeq: 2\r\n"
        "Content-Length: 4\r\n\r\nv=0\n",
    };

    for (int i = 0; i < (int)(sizeof(requests) / sizeof(string)); i++) {
        const string &request = requests[i];

        // The cuts: after "METHOD URI ", inside the first header name, and after each "Name: ".
        vector<size_t> cuts;
        size_t line_end = request.find("\r\n");
        cuts.push_back(request.rfind(' ', line_end) + 1);
        cuts.push_back(line_end + 2 + 2);
        for (size_t pos = request.find(": "); pos != string::npos; pos = request.find(": ", pos + 2)) {
            cuts.push_back(pos + 2);
        }

        for (int j = 0; j < (int)cuts.size(); j++) {
            MockBufferIO io;
            io.append(request.substr(0, cuts[j]));
            SrsRtspStack stack(&io);

            SrsRtspRequest *req = NULL;
            srs_error_t err = stack.recv_message(&req);
            EXPECT_EQ(ERROR_SOCKET_READ, srs_error_code(err)) << request.substr(0, cuts[j]);
            EXPECT_TRUE(req == NULL) << request.substr(0, cuts[j]);
            srs_freep(err);
            srs_freep(req);
        }
    }
}

// A player that has left when SRS answers its request: the write fails and the error is returned,
// so the connection ends instead of serving a closed socket.
VOID TEST(ProtocolRtspStackTest, PlayerLeftBeforeAnswer_WriteFails)
{
    MockBufferIO io;
    io.out_err = srs_error_new(ERROR_SOCKET_WRITE, "connection reset by peer");
    SrsRtspStack stack(&io);

    SrsRtspOptionsResponse res(1);
    srs_error_t err = stack.send_message(&res);
    EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(0, io.out_length());
}

// A player's RTCP over TCP arrives cut between two reads, and the bytes of the first read hold a
// space, CR or LF: a lone 32-byte RR (reduced-size RTCP, RFC 5506), whose length byte is 0x20, with
// its header alone as live555 writes it; an RR and SDES cut after a report block whose jitter is
// 32. The parser should wait for the rest of the frame and parse the GET_PARAMETER after it, but it
// scans the partial frame as a request, so the request fails and the connection ends.
VOID TEST(ProtocolRtspStackTest, DISABLED_PlayerRtcpCutAtDelimiterByte_ConnectionDropped)
{
    srs_error_t err = srs_success;

    string keepalive = "GET_PARAMETER rtsp://127.0.0.1:8554/live/livestream RTSP/1.0\r\nCSeq: 6\r\nSession: 7AB3C1D2\r\n\r\n";

    // A lone RR, its header in its own segment.
    if (true) {
        string rtcp = rtsp_rtcp_rr(0x31);
        RtspSegmentIO io;
        io.segment(rtsp_interleaved_header(1, rtcp.size()))->segment(rtcp)->segment(keepalive);
        SrsRtspStack stack(&io);

        SrsRtspRequest *req = NULL;
        HELPER_EXPECT_SUCCESS(stack.recv_message(&req));
        SrsUniquePtr<SrsRtspRequest> req_uptr(req);
        if (req) {
            EXPECT_STREQ("GET_PARAMETER", req->method_.c_str());
            EXPECT_EQ(6, req->seq_);
        }
    }

    // An RR and SDES, cut after the jitter of 32.
    if (true) {
        string frame = rtsp_interleaved_header(1, 52) + rtsp_rtcp_rr(32) + rtsp_rtcp_sdes();
        RtspSegmentIO io;
        io.segment(frame.substr(0, 28))->segment(frame.substr(28))->segment(keepalive);
        SrsRtspStack stack(&io);

        SrsRtspRequest *req = NULL;
        HELPER_EXPECT_SUCCESS(stack.recv_message(&req));
        SrsUniquePtr<SrsRtspRequest> req_uptr(req);
        if (req) {
            EXPECT_STREQ("GET_PARAMETER", req->method_.c_str());
            EXPECT_EQ(6, req->seq_);
        }
    }
}

// Header lines as RTSP takes them from HTTP/1.1 (RFC 2326 4.2, 12.1): no space after the colon, a
// list with a space in its value, such as Accept, and an empty value. Each request should parse,
// but the parser reads most values as one token up to a space and needs one, so each is refused
// and the connection ends.
VOID TEST(ProtocolRtspStackTest, DISABLED_HeaderWhitespaceAsHttpAllows_RequestRefused)
{
    srs_error_t err = srs_success;

    if (true) {
        MockBufferIO io;
        io.append("OPTIONS rtsp://127.0.0.1:8554/live/livestream RTSP/1.0\r\nCSeq:2\r\n\r\n");
        SrsRtspStack stack(&io);

        SrsRtspRequest *req = NULL;
        HELPER_EXPECT_SUCCESS(stack.recv_message(&req));
        SrsUniquePtr<SrsRtspRequest> req_uptr(req);
        if (req) {
            EXPECT_EQ(2, req->seq_);
        }
    }

    if (true) {
        MockBufferIO io;
        io.append("DESCRIBE rtsp://127.0.0.1:8554/live/livestream RTSP/1.0\r\nCSeq: 3\r\n"
                  "Accept: application/sdp, application/rtsl\r\n\r\n");
        SrsRtspStack stack(&io);

        SrsRtspRequest *req = NULL;
        HELPER_EXPECT_SUCCESS(stack.recv_message(&req));
        SrsUniquePtr<SrsRtspRequest> req_uptr(req);
        if (req) {
            EXPECT_STREQ("application/sdp, application/rtsl", req->accept_.c_str());
        }
    }

    if (true) {
        MockBufferIO io;
        io.append("OPTIONS rtsp://127.0.0.1:8554/live/livestream RTSP/1.0\r\nCSeq: 4\r\nUser-Agent: \r\n\r\n");
        SrsRtspStack stack(&io);

        SrsRtspRequest *req = NULL;
        HELPER_EXPECT_SUCCESS(stack.recv_message(&req));
        SrsUniquePtr<SrsRtspRequest> req_uptr(req);
        if (req) {
            EXPECT_EQ(4, req->seq_);
            EXPECT_STREQ("", req->user_agent_.c_str());
        }
    }
}

// A request longer than one TCP segment, cut between the CR and the LF of a header line. The parser
// skips the CR at the end of the first read, then takes the LF that starts the next read as the
// empty line that ends the headers: the request loses its later headers, and they are parsed as
// the next request, which fails and ends the connection.
VOID TEST(ProtocolRtspStackTest, DISABLED_RequestCutBetweenCrAndLf_HeadersAfterItLost)
{
    srs_error_t err = srs_success;

    RtspSegmentIO io;
    io.segment("DESCRIBE rtsp://127.0.0.1:8554/live/livestream RTSP/1.0\r\nCSeq: 2\r")
        ->segment("\nUser-Agent: LibVLC/3.0.20\r\nAccept: application/sdp\r\n\r\n");
    SrsRtspStack stack(&io);

    SrsRtspRequest *req = NULL;
    HELPER_ASSERT_SUCCESS(stack.recv_message(&req));
    SrsUniquePtr<SrsRtspRequest> req_uptr(req);
    EXPECT_EQ(2, req->seq_);
    EXPECT_STREQ("LibVLC/3.0.20", req->user_agent_.c_str());
    EXPECT_STREQ("application/sdp", req->accept_.c_str());

    // Nothing is left: the next read is the client closing.
    SrsRtspRequest *next = NULL;
    err = stack.recv_message(&next);
    EXPECT_EQ(ERROR_SOCKET_READ, srs_error_code(err));
    srs_freep(err);
    srs_freep(next);
}
#endif

// A WHIP offer as a browser publishes it: Opus audio and H.264 video, sendonly, with the
// transport-wide-cc extension. The caller gives the H.264 fmtp and may add session-level and
// video-level lines.
static string sdp_whip_offer(const string &h264_fmtp, const string &session_lines = "", const string &video_lines = "")
{
    return "v=0\r\n"
           "o=- 4611731400430051336 2 IN IP4 127.0.0.1\r\n"
           "s=-\r\n" +
           session_lines +
           "t=0 0\r\n"
           "a=group:BUNDLE 0 1\r\n"
           "a=msid-semantic: WMS\r\n"
           "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n"
           "c=IN IP4 0.0.0.0\r\n"
           "a=ice-ufrag:Vx3d\r\n"
           "a=ice-pwd:fA0Ttk8rMDpEgG4e5w3bU1xq\r\n"
           "a=fingerprint:sha-256 4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB:3B:45:7D:DD:61:8F:0C:5E:6B:0A:9A:B1\r\n"
           "a=setup:actpass\r\n"
           "a=mid:0\r\n"
           "a=extmap:3 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01\r\n"
           "a=sendonly\r\n"
           "a=msid:- a1\r\n"
           "a=rtcp-mux\r\n"
           "a=rtpmap:111 opus/48000/2\r\n"
           "a=rtcp-fb:111 transport-cc\r\n"
           "a=fmtp:111 minptime=10;useinbandfec=1\r\n"
           "a=ssrc:1001 cname:browser\r\n"
           "m=video 9 UDP/TLS/RTP/SAVPF 102\r\n"
           "c=IN IP4 0.0.0.0\r\n" +
           video_lines +
           "a=ice-ufrag:Vx3d\r\n"
           "a=ice-pwd:fA0Ttk8rMDpEgG4e5w3bU1xq\r\n"
           "a=fingerprint:sha-256 4A:AD:B9:B1:3F:82:18:3B:54:02:12:DF:3E:5D:49:6B:19:E5:7C:AB:3B:45:7D:DD:61:8F:0C:5E:6B:0A:9A:B1\r\n"
           "a=setup:actpass\r\n"
           "a=mid:1\r\n"
           "a=extmap:3 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01\r\n"
           "a=sendonly\r\n"
           "a=msid:- v1\r\n"
           "a=rtcp-mux\r\n"
           "a=rtcp-rsize\r\n"
           "a=rtpmap:102 H264/90000\r\n"
           "a=rtcp-fb:102 nack\r\n"
           "a=rtcp-fb:102 nack pli\r\n"
           "a=rtcp-fb:102 transport-cc\r\n"
           "a=fmtp:102 " +
           h264_fmtp + "\r\n"
                       "a=ssrc:2001 cname:browser\r\n";
}

// The server answers a WHIP publisher. The negotiator builds the answer's media, then the
// connection sets the ICE credentials, the DTLS fingerprint and role and the host candidates
// (rtc_server.protocol all: one UDP and one ICE-TCP candidate) on every media, as
// SrsRtcConnection::generate_local_sdp does, and the API encodes it. The browser reads its ICE
// password, fingerprint and DTLS role back from every media and connects to the candidates.
VOID TEST(ProtocolSdpTest, WhipAnswerCarriesIceFingerprintAndCandidates)
{
    srs_error_t err = srs_success;

    SrsRtcUserConfig ruc;
    ruc.req_->vhost_ = "__defaultVhost__";
    ruc.publish_ = true;
    ruc.remote_sdp_str_ = sdp_whip_offer("level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f");
    HELPER_ASSERT_SUCCESS(ruc.remote_sdp_.parse(ruc.remote_sdp_str_));

    SrsRtcPublisherNegotiator negotiator;
    SrsRtcSourceDescription stream_desc;
    HELPER_ASSERT_SUCCESS(negotiator.negotiate_publish_capability(&ruc, &stream_desc));

    SrsSdp local_sdp;
    HELPER_ASSERT_SUCCESS(negotiator.generate_publish_local_sdp(ruc.req_, local_sdp, &stream_desc, ruc.remote_sdp_.is_unified(), ruc.audio_before_video_));

    SrsRand rand;
    const string ufrag = rand.gen_str(8);
    const string pwd = rand.gen_str(32);
    const string fingerprint = "9C:1F:0E:3B:6A:2D:5E:44:81:07:C2:9A:B3:5F:6E:12:D4:70:8B:3C:2E:91:A6:05:7D:4B:E8:C1:36:0F:5A:22";
    local_sdp.set_ice_ufrag(ufrag);
    local_sdp.set_ice_pwd(pwd);
    local_sdp.set_fingerprint_algo("sha-256");
    local_sdp.set_fingerprint(fingerprint);
    local_sdp.add_candidate("udp", "192.168.1.10", 8000, "host");
    local_sdp.add_candidate("tcp", "192.168.1.10", 8000, "host");
    local_sdp.set_dtls_role("passive");

    std::ostringstream os;
    HELPER_ASSERT_SUCCESS(local_sdp.encode(os));
    string answer = os.str();

    // Each media carries both candidates, with the host type preference 126 and component 1:
    // (2^24)*126 + (2^8)*65535 + 255 = 2130706431. The ICE-TCP one is passive (RFC 6544): the
    // browser connects to the server.
    const string udp_candidate = "a=candidate:0 1 udp 2130706431 192.168.1.10 8000 typ host generation 0\r\n";
    const string tcp_candidate = "a=candidate:1 1 tcp 2130706431 192.168.1.10 8000 typ host tcptype passive\r\n";
    size_t video = answer.find("m=video");
    ASSERT_NE(string::npos, video) << answer;
    EXPECT_NE(string::npos, answer.find(udp_candidate)) << answer;
    EXPECT_LT(answer.find(udp_candidate), video) << answer;
    EXPECT_NE(string::npos, answer.find(udp_candidate, video)) << answer;
    EXPECT_NE(string::npos, answer.find(tcp_candidate, video)) << answer;
    EXPECT_NE(string::npos, answer.find("a=extmap:3 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01\r\n", video)) << answer;
    EXPECT_NE(string::npos, answer.find("a=recvonly\r\n", video)) << answer;

    // The browser parses the answer and finds the same credentials on each media.
    SrsSdp browser;
    HELPER_ASSERT_SUCCESS(browser.parse(answer));
    ASSERT_EQ(2, (int)browser.media_descs_.size());
    EXPECT_EQ(ufrag, browser.get_ice_ufrag());
    EXPECT_EQ(pwd, browser.get_ice_pwd());
    EXPECT_EQ("passive", browser.get_dtls_role());
    for (size_t i = 0; i < browser.media_descs_.size(); ++i) {
        SrsMediaDesc &desc = browser.media_descs_[i];
        EXPECT_EQ(ufrag, desc.session_info_.ice_ufrag_);
        EXPECT_EQ(pwd, desc.session_info_.ice_pwd_);
        EXPECT_EQ("sha-256", desc.session_info_.fingerprint_algo_);
        EXPECT_EQ(fingerprint, desc.session_info_.fingerprint_);
    }
    EXPECT_EQ("0", browser.media_descs_[0].mid_);
    EXPECT_EQ("1", browser.media_descs_[1].mid_);
}

// A publisher that caps its bitrate sends bandwidth lines (RFC 4566 5.8, b=AS or b=TIAS, as
// browsers write for a maxBitrate) and may name the session with an i= line. The server ignores
// them, and the attributes after a bandwidth line still belong to the video media.
VOID TEST(ProtocolSdpTest, PublisherOfferWithBandwidthLines_Accepted)
{
    srs_error_t err = srs_success;

    string offer = sdp_whip_offer("level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f",
                                  "i=WHIP publisher\r\nb=AS:3000\r\n", "b=AS:2500\r\nb=TIAS:2500000\r\n");
    SrsSdp sdp;
    HELPER_ASSERT_SUCCESS(sdp.parse(offer));

    ASSERT_EQ(2, (int)sdp.media_descs_.size());
    SrsMediaDesc &video = sdp.media_descs_[1];
    EXPECT_EQ("video", video.type_);
    EXPECT_EQ("1", video.mid_);
    EXPECT_TRUE(video.sendonly_);
    EXPECT_TRUE(video.rtcp_mux_);
    ASSERT_TRUE(video.find_media_with_payload_type(102) != NULL);
    EXPECT_EQ("H264", video.find_media_with_payload_type(102)->encoding_name_);
    ASSERT_EQ(1, (int)video.ssrc_infos_.size());
    EXPECT_EQ(2001u, video.ssrc_infos_[0].ssrc_);
    EXPECT_EQ("Vx3d", sdp.get_ice_ufrag());
}

// An offer whose rtpmap does not describe a payload of its m-line is refused with the SDP decode
// error: an application that munged the m-line to drop a codec but kept its rtpmap, and an rtpmap
// with no clock rate.
VOID TEST(ProtocolSdpTest, OfferWithRtpmapNotMatchingMLine_Refused)
{
    srs_error_t err = srs_success;

    // The app removed VP8 (96) from the m-line but kept its attributes.
    string munged = sdp_whip_offer("level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f", "",
                                   "a=rtpmap:96 VP8/90000\r\n");
    SrsSdp sdp;
    err = sdp.parse(munged);
    EXPECT_EQ(ERROR_RTC_SDP_DECODE, srs_error_code(err));
    EXPECT_NE(string::npos, srs_error_desc(err).find("can not find payload 96")) << srs_error_desc(err);
    srs_freep(err);

    // The rtpmap of the H.264 payload has no clock rate.
    string offer = sdp_whip_offer("level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f");
    size_t pos = offer.find("a=rtpmap:102 H264/90000");
    ASSERT_NE(string::npos, pos);
    offer.replace(pos, strlen("a=rtpmap:102 H264/90000"), "a=rtpmap:102 H264");
    SrsSdp sdp2;
    err = sdp2.parse(offer);
    EXPECT_EQ(ERROR_RTC_SDP_DECODE, srs_error_code(err));
    EXPECT_NE(string::npos, srs_error_desc(err).find("invalid rtpmap line=102 H264")) << srs_error_desc(err);
    srs_freep(err);
}

// A WHIP publisher whose H.264 fmtp leaves out level-asymmetry-allowed, an optional parameter
// whose default is 0 (RFC 6184 8.1), as GStreamer's payloader writes it. srs_parse_h264_fmtp
// refuses the fmtp, and the negotiator skips a payload it cannot parse instead of keeping it as a
// backup, so the publish is refused with no H.264 match. The same offer with
// level-asymmetry-allowed=0, which means the same, is accepted.
VOID TEST(ProtocolSdpTest, DISABLED_WhipPublisherH264WithoutLevelAsymmetry_PublishRefused)
{
    srs_error_t err = srs_success;

    if (true) {
        SrsRtcUserConfig ruc;
        ruc.req_->vhost_ = "__defaultVhost__";
        ruc.publish_ = true;
        HELPER_ASSERT_SUCCESS(ruc.remote_sdp_.parse(sdp_whip_offer("level-asymmetry-allowed=0;packetization-mode=1;profile-level-id=42e01f")));

        SrsRtcPublisherNegotiator negotiator;
        SrsRtcSourceDescription stream_desc;
        HELPER_ASSERT_SUCCESS(negotiator.negotiate_publish_capability(&ruc, &stream_desc));
        ASSERT_EQ(1, (int)stream_desc.video_track_descs_.size());
    }

    SrsRtcUserConfig ruc;
    ruc.req_->vhost_ = "__defaultVhost__";
    ruc.publish_ = true;
    HELPER_ASSERT_SUCCESS(ruc.remote_sdp_.parse(sdp_whip_offer("packetization-mode=1;profile-level-id=42e01f")));

    SrsRtcPublisherNegotiator negotiator;
    SrsRtcSourceDescription stream_desc;
    HELPER_EXPECT_SUCCESS(negotiator.negotiate_publish_capability(&ruc, &stream_desc));
    ASSERT_EQ(1, (int)stream_desc.video_track_descs_.size());
    EXPECT_EQ(102, (int)stream_desc.video_track_descs_[0]->media_->pt_);
}

// An offer whose ssrc-group, rtcp-fb or ssrc line ends right after its first word. The parser
// takes the rest of the line after that word, but the word read to the end of the line, so there
// is no rest. SrsGoApiRtcPublish and SrsGoApiRtcPlay parse the HTTP body before any hook, so any
// client of the WHIP or WHEP API can send it. The parser refuses the line, and the same lines with
// their values still parse.
VOID TEST(ProtocolSdpTest, OfferLineEndingAfterFirstWord_Refused)
{
    const char *lines[] = {
        "a=ssrc-group:FID",
        "a=rtcp-fb:102",
        "a=ssrc:2002 cname",
    };

    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i) {
        string offer = sdp_whip_offer("level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f") + lines[i] + "\r\n";

        SrsSdp sdp;
        srs_error_t err = srs_success;
        bool thrown = false;
        try {
            err = sdp.parse(offer);
        } catch (const std::exception &e) {
            thrown = true;
        }
        EXPECT_FALSE(thrown) << lines[i];
        EXPECT_EQ(ERROR_RTC_SDP_DECODE, srs_error_code(err)) << lines[i];
        srs_freep(err);
    }

    string offer = sdp_whip_offer("level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f") +
                   "a=ssrc-group:FID 2002 2003\r\n"
                   "a=rtcp-fb:102 goog-remb\r\n"
                   "a=ssrc:2002 cname:browser\r\n";
    srs_error_t err = srs_success;
    SrsSdp sdp;
    HELPER_ASSERT_SUCCESS(sdp.parse(offer));
    ASSERT_EQ(2, (int)sdp.media_descs_.size());
    SrsMediaDesc &video = sdp.media_descs_[1];
    ASSERT_EQ(1, (int)video.ssrc_groups_.size());
    EXPECT_STREQ("FID", video.ssrc_groups_[0].semantic_.c_str());
    ASSERT_EQ(2, (int)video.ssrc_groups_[0].ssrcs_.size());
    EXPECT_EQ(2003u, video.ssrc_groups_[0].ssrcs_[1]);
    SrsMediaPayloadType *payload = video.find_media_with_payload_type(102);
    ASSERT_TRUE(payload != NULL);
    EXPECT_STREQ("goog-remb", payload->rtcp_fb_.back().c_str());
    string cname;
    for (size_t i = 0; i < video.ssrc_infos_.size(); ++i) {
        if (video.ssrc_infos_[i].ssrc_ == 2002) {
            cname = video.ssrc_infos_[i].cname_;
        }
    }
    EXPECT_STREQ("browser", cname.c_str());
}
