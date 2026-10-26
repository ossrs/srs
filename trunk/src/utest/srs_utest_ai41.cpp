//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai41.hpp>

#include <srs_app_rtc_source.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_security.hpp>
#include <srs_app_statistic.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_http_conn.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_sdp.hpp>
#include <srs_utest_ai05.hpp>
#include <srs_utest_manual_config.hpp>
#include <srs_utest_manual_coworkers.hpp>
#include <srs_utest_manual_mock.hpp>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

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
    ::mkdir(dir, 0755);
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
    ASSERT_EQ(0, ::mkdir(parent.c_str(), 0755));

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

// A TLS client on the other end of an HTTPS or RTMPS connection, such as a browser or an
// encoder. It runs OpenSSL's client over memory BIOs: what the server writes is the
// client's input, and what the client sends is what the server reads. The client acts
// each time the server reads, as a peer answers what it got.
class TlsClientPeer : public ISrsProtocolReadWriter
{
public:
    SSL_CTX *ctx_;
    SSL *ssl_;
    // The bytes from the server, and the bytes to the server.
    BIO *in_;
    BIO *out_;
    // The most bytes one server read gets, as a TCP segment; 0 for no limit.
    int segment_;
    // The server's write that fails, counted from 1, as a client gone; 0 for none.
    int fail_write_;
    // The client closes its TCP connection, with no TLS alert, once the server has
    // written this many times; -1 for never.
    int hangup_after_writes_;
    int nn_writes_;
    int nn_reads_;
    // The plaintext the client got from the server.
    string received_;
    // What the client sends once it has got this much plaintext.
    vector<pair<size_t, string> > replies_;
    srs_utime_t rtm_;
    srs_utime_t stm_;
    int64_t nn_recv_;
    int64_t nn_send_;

public:
    TlsClientPeer(bool verify_server)
    {
        ctx_ = SSL_CTX_new(TLS_client_method());
        SSL_CTX_set_verify(ctx_, verify_server ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, NULL);
        ssl_ = SSL_new(ctx_);
        in_ = BIO_new(BIO_s_mem());
        out_ = BIO_new(BIO_s_mem());
        SSL_set_bio(ssl_, in_, out_);
        SSL_set_connect_state(ssl_);

        segment_ = 0;
        fail_write_ = 0;
        hangup_after_writes_ = -1;
        nn_writes_ = 0;
        nn_reads_ = 0;
        rtm_ = stm_ = SRS_UTIME_NO_TIMEOUT;
        nn_recv_ = nn_send_ = 0;
    }
    virtual ~TlsClientPeer()
    {
        SSL_free(ssl_);
        SSL_CTX_free(ctx_);
    }

public:
    bool hung_up()
    {
        return hangup_after_writes_ >= 0 && nn_writes_ >= hangup_after_writes_;
    }
    // Take what the server sent, and send what the client has to.
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

        while (!replies_.empty() && replies_.front().first <= received_.size()) {
            string data = replies_.front().second;
            replies_.erase(replies_.begin());
            SSL_write(ssl_, data.data(), (int)data.size());
        }
    }
    void send(const string &data)
    {
        drive();
        SSL_write(ssl_, data.data(), (int)data.size());
    }
    void close_notify()
    {
        drive();
        SSL_shutdown(ssl_);
        ERR_clear_error();
    }
    string received()
    {
        drive();
        return received_;
    }

public:
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread)
    {
        nn_reads_++;
        drive();

        int pending = hung_up() ? 0 : (int)BIO_ctrl_pending(out_);
        if (pending <= 0) {
            return srs_error_new(ERROR_SOCKET_READ, "EOF");
        }

        int n = srs_min(pending, (int)size);
        if (segment_ > 0) {
            n = srs_min(n, segment_);
        }
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
            return srs_error_new(ERROR_SOCKET_WRITE, "client gone");
        }
        BIO_write(in_, buf, (int)size);
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
