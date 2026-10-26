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

#include <string>

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
