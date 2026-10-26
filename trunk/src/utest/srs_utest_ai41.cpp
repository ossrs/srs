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
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_http_client.hpp>
#include <srs_protocol_http_conn.hpp>
#include <srs_protocol_log.hpp>
#include <srs_protocol_rtc_stun.hpp>
#include <srs_protocol_rtmp_stack.hpp>
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
#include <sys/stat.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>
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
            if (::truncate(path_.c_str(), 0) != 0) {
                return srs_error_new(ERROR_SYSTEM_FILE_WRITE, "truncate");
            }
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
static std::string stun_controlling_binding_request(const std::string &username, const std::string &pwd, bool nominate)
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

// How far the test origin of an edge pull or a forwarder push goes before it stops.
enum RtmpConnOriginStep {
    // Not an RTMP server: an HTTP server answers the handshake with an HTTP error.
    RtmpConnOriginNotRtmp,
    // Refuses the app with an _error onStatus and closes, as an RTMP server does for an app
    // it does not serve.
    RtmpConnOriginRejectsApp,
    // Answers connect, then exits, as an origin that restarts at that moment.
    RtmpConnOriginExitsAfterConnect,
    // Answers createStream, then the connection is reset.
    RtmpConnOriginResetAfterCreateStream,
    // Serves the play or publish of the client.
    RtmpConnOriginServes,
};

// An RTMP origin on a random local port for one connection of an edge or a forwarder,
// which stops at the step the test picks.
class RtmpConnTestOrigin : public SrsHttpTestServer
{
public:
    RtmpConnOriginStep step_;
    string tc_url_;
    SrsRtmpConnType type_;
    string stream_;
    string video_;
    string error_;

public:
    RtmpConnTestOrigin(RtmpConnOriginStep step) : SrsHttpTestServer("")
    {
        step_ = step;
        type_ = SrsRtmpConnUnknown;
    }

public:
    virtual srs_error_t start()
    {
        srs_error_t err = SrsHttpTestServer::start();
        // Let State Threads run once, so its clock is fresh before the origin sets its
        // timeouts, as RawReplyTcpServer does.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        return err;
    }
    virtual srs_error_t do_cycle(srs_netfd_t cfd)
    {
        srs_error_t err = do_serve(cfd);
        if (err != srs_success) {
            error_ = srs_error_desc(err);
        }
        return err;
    }

private:
    srs_error_t do_serve(srs_netfd_t cfd)
    {
        srs_error_t err = srs_success;

        SrsStSocket skt(cfd);
        skt.set_recv_timeout(3 * SRS_UTIME_SECONDS);
        skt.set_send_timeout(3 * SRS_UTIME_SECONDS);

        if (step_ == RtmpConnOriginNotRtmp) {
            char buf[1537];
            if ((err = skt.read(buf, sizeof(buf), NULL)) != srs_success) {
                return srs_error_wrap(err, "read c0c1");
            }
            string res = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
            return skt.write((void *)res.data(), res.size(), NULL);
        }

        SrsRtmpServer rtmp(&skt);
        if ((err = rtmp.handshake()) != srs_success) {
            return srs_error_wrap(err, "handshake");
        }

        SrsRequest req;
        if ((err = rtmp.connect_app(&req)) != srs_success) {
            return srs_error_wrap(err, "connect app");
        }
        tc_url_ = req.tcUrl_;

        if (step_ == RtmpConnOriginRejectsApp) {
            rtmp.response_connect_reject(&req, "app not allowed");
            return err;
        }

        if ((err = rtmp.response_connect_app(&req, "127.0.0.1")) != srs_success) {
            return srs_error_wrap(err, "response connect app");
        }
        if (step_ == RtmpConnOriginExitsAfterConnect) {
            return err;
        }

        if (step_ == RtmpConnOriginResetAfterCreateStream) {
            SrsRtmpCommonMessage *msg_raw = NULL;
            SrsCreateStreamPacket *pkt_raw = NULL;
            if ((err = rtmp.expect_message<SrsCreateStreamPacket>(&msg_raw, &pkt_raw)) != srs_success) {
                return srs_error_wrap(err, "expect create stream");
            }
            SrsUniquePtr<SrsRtmpCommonMessage> msg(msg_raw);
            SrsUniquePtr<SrsCreateStreamPacket> pkt(pkt_raw);
            if ((err = rtmp.send_and_free_packet(new SrsCreateStreamResPacket(pkt->transaction_id_, 1), 0)) != srs_success) {
                return srs_error_wrap(err, "response create stream");
            }
            // Close with a reset, as a load balancer in front of the origin that drops the
            // connection does.
            struct linger lg;
            lg.l_onoff = 1;
            lg.l_linger = 0;
            setsockopt(srs_netfd_fileno(cfd), SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
            return err;
        }

        srs_utime_t duration = 0;
        if ((err = rtmp.identify_client(1, type_, stream_, duration)) != srs_success) {
            return srs_error_wrap(err, "identify client");
        }

        if (type_ == SrsRtmpConnPlay) {
            if ((err = rtmp.start_play(1)) != srs_success) {
                return srs_error_wrap(err, "start play");
            }

            SrsOnMetaDataPacket *meta = new SrsOnMetaDataPacket();
            meta->metadata_->set("width", SrsAmf0Any::number(1280));
            meta->metadata_->set("height", SrsAmf0Any::number(720));
            if ((err = rtmp.send_and_free_packet(meta, 1)) != srs_success) {
                return srs_error_wrap(err, "send metadata");
            }

            if ((err = rtmp.send_and_free_message(rtmp_conn_video_packet(), 1)) != srs_success) {
                return srs_error_wrap(err, "send video");
            }

            // Serve until the edge leaves.
            while (true) {
                SrsRtmpCommonMessage *msg = NULL;
                if ((err = rtmp.recv_message(&msg)) != srs_success) {
                    srs_freep(err);
                    return err;
                }
                srs_freep(msg);
            }
        }

        if ((err = rtmp.start_flash_publish(1)) != srs_success) {
            return srs_error_wrap(err, "start publish");
        }
        while (video_.empty()) {
            SrsRtmpCommonMessage *msg_raw = NULL;
            if ((err = rtmp.recv_message(&msg_raw)) != srs_success) {
                return srs_error_wrap(err, "recv media");
            }
            SrsUniquePtr<SrsRtmpCommonMessage> msg(msg_raw);
            if (msg->header_.is_video()) {
                video_.assign(msg->payload(), msg->size());
            }
        }

        return err;
    }

public:
    // An AVC key frame of 4 KB, larger than the 128-byte default chunk.
    static SrsMediaPacket *rtmp_conn_video_packet()
    {
        SrsMediaPacket *msg = new SrsMediaPacket();
        msg->timestamp_ = 40;
        msg->message_type_ = SrsFrameTypeVideo;
        char *data = new char[4096];
        for (int i = 0; i < 4096; i++) {
            data[i] = (char)(i % 251);
        }
        data[0] = 0x17;
        data[1] = 0x01;
        msg->wrap(data, 4096);
        return msg;
    }

    string rtmp_url(string stream)
    {
        return "rtmp://127.0.0.1:" + srs_strconv_format_int(get_port()) + "/live/" + stream;
    }
};

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
