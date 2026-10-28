//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai43.hpp>

#include <srs_app_forward.hpp>
#include <srs_app_fragment.hpp>
#ifdef SRS_GB28181
#include <srs_app_gb28181.hpp>
#endif
#include <srs_app_factory.hpp>
#include <srs_app_heartbeat.hpp>
#include <srs_app_hls.hpp>
#include <srs_app_http_api.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_http_static.hpp>
#include <srs_app_http_stream.hpp>
#include <srs_app_edge.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_security.hpp>
#include <srs_app_st.hpp>
#include <srs_app_statistic.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_packet.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_kernel_resource.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_http_stack.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_st.hpp>
#include <srs_utest_ai13.hpp>
#include <srs_utest_ai14.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai16.hpp>
#include <srs_utest_ai19.hpp>
#include <srs_utest_ai23.hpp>
#include <srs_utest_ai34.hpp>
#include <srs_utest_ai36.hpp>
#include <srs_utest_ai40.hpp>
#include <srs_utest_ai42.hpp>
#include <srs_utest_manual_config.hpp>
#include <srs_utest_manual_coworkers.hpp>
#include <srs_utest_manual_http.hpp>
#include <srs_utest_manual_mock.hpp>
#include <srs_utest_manual_protocol.hpp>

#include <set>
#include <stdio.h>
#include <string>
#include <sys/stat.h>
#include <vector>
#ifndef _WIN32
#include <dirent.h>
#endif

using namespace std;

// State Threads cannot create a coroutine: the process is out of memory or over its address space limit.
static void *forward_st_thread_create_out_of_memory(void *(* /*start*/)(void *arg), void * /*arg*/, int /*joinable*/, int /*stack_size*/)
{
    return NULL;
}

// A publisher starts a stream on a vhost that forwards to another server. When no coroutine can be created for the
// forwarder, the publish fails with the ST error, naming the destination, instead of a forward that never runs. The
// forward shows in the stream API until the unpublish after the failure, which stops the forwarder safely and removes
// it.
VOID TEST(AppForwardTest, PublishFailsWhenNoCoroutineCanBeCreatedForForwarder)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<MockAppConfigForForwardHub> config(new MockAppConfigForForwardHub());
    SrsUniquePtr<MockHttpHooksForForwardHub> hooks(new MockHttpHooksForForwardHub());
    SrsUniquePtr<MockLiveSourceForOriginHub> source(new MockLiveSourceForOriginHub());
    SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "livestream"));

    std::vector<std::string> destinations;
    destinations.push_back("127.0.0.1:19350");
    config->set_forward_destinations(destinations);

    SrsUniquePtr<SrsOriginHub> hub(utest_forward_hub(source.get(), req.get(), config.get(), hooks.get()));

    _ST_THREAD_CREATE_PFN ov = _pfn_st_thread_create;
    _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)forward_st_thread_create_out_of_memory;
    err = hub->on_publish();
    _pfn_st_thread_create = ov;

    EXPECT_EQ(ERROR_ST_CREATE_CYCLE_THREAD, srs_error_code(err));
    string desc = srs_error_desc(err);
    srs_freep(err);
    EXPECT_NE(string::npos, desc.find("start thread")) << desc;
    EXPECT_NE(string::npos, desc.find("forward-to=127.0.0.1:19350")) << desc;

    ASSERT_EQ(1, (int)hub->forwarders_.size());
    SrsForwarder *forwarder = dynamic_cast<SrsForwarder *>(hub->forwarders_.at(0));
    ASSERT_TRUE(forwarder != NULL);
    string id = forwarder->id_;

    SrsStatisticForward *forward = _srs_stat->find_forward(id);
    ASSERT_TRUE(forward != NULL);
    EXPECT_STREQ("rtmp://127.0.0.1:19350/live/livestream", forward->url_.c_str());
    EXPECT_EQ(SrsForwardStateConnecting, forward->state_);

    hub->on_unpublish();
    EXPECT_EQ(0, (int)hub->forwarders_.size());
    EXPECT_TRUE(_srs_stat->find_forward(id) == NULL);
}

// The destination server is down: the forwarder logs the connect error, shows retrying in the stream API, and waits
// 3s before it connects again. An unpublish during that wait stops the forwarder at once, not after the wait, with no
// second connect, and removes it from the stream API.
VOID TEST(AppForwardTest, DestinationDown_RetriedUntilUnpublish)
{
    srs_error_t err;

    SrsStatistic stat;
    SrsUniquePtr<MockOriginHub> hub(new MockOriginHub());
    SrsUniquePtr<MockAppFactoryForForwarderCycle> factory(new MockAppFactoryForForwarderCycle());
    SrsUniquePtr<MockAppConfig> config(new MockAppConfig());
    SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "livestream"));

    MockRtmpClientForForwarder *sdk = new MockRtmpClientForForwarder();
    sdk->connect_error_ = srs_error_new(ERROR_SOCKET_CONNECT, "connection refused");
    factory->next_ = sdk;

    SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get(), req.get()));
    forwarder->stat_ = &stat;
    forwarder->app_factory_ = factory.get();
    forwarder->config_ = config.get();
    HELPER_EXPECT_SUCCESS(forwarder->initialize(req.get(), "127.0.0.1:19350"));

    MockLogForSignal log;
    ISrsLog *original_log = _srs_log;
    _srs_log = &log;

    err = forwarder->on_publish();
    // Let the forwarder connect, fail, and start its wait.
    for (int i = 0; i < 100 && sdk->connect_count_ == 0; i++) {
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    SrsStatisticForward *forward = stat.find_forward(forwarder->id_);
    SrsForwardState state = forward ? forward->state_ : SrsForwardStateConnecting;

    srs_utime_t starttime = srs_time_now_realtime();
    forwarder->on_unpublish();
    srs_utime_t elapsed = srs_time_now_realtime() - starttime;

    _srs_log = original_log;
    HELPER_EXPECT_SUCCESS(err);

    EXPECT_EQ(1, sdk->connect_count_);
    EXPECT_EQ(0, sdk->publish_count_);
    EXPECT_EQ(1, (int)factory->clients_.size());
    EXPECT_TRUE(forward != NULL);
    EXPECT_EQ(SrsForwardStateRetrying, state);
    EXPECT_LT(elapsed, 1 * SRS_UTIME_SECONDS);
    EXPECT_TRUE(stat.find_forward(forwarder->id_) == NULL);

    int retries = 0;
    for (int i = 0; i < (int)log.warns_.size(); i++) {
        const string &w = log.warns_[i];
        if (w.find("Forwarder: Ignore error") == 0 && w.find("connection refused") != string::npos) {
            retries++;
        }
    }
    EXPECT_EQ(1, retries);
}

// The destination accepts the publish, then resets the connection at the first packet the forwarder sends, the
// stream's metadata: the forward ends with the write error, and neither the sequence headers nor the queued media are
// sent.
VOID TEST(AppForwardTest, DestinationResetsAtMetadata_NothingElseSent)
{
    srs_error_t err;

    SrsUniquePtr<MockOriginHub> hub(new MockOriginHub());
    SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "livestream"));
    SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get(), req.get()));

    SrsMediaPacket *pkts[] = {utest_forward_metadata(0, 'm'), utest_forward_video_sh(0, 'V'), utest_forward_audio_sh(0, 'A'),
                              utest_forward_video_keyframe(40, 'k')};
    HELPER_EXPECT_SUCCESS(forwarder->on_meta_data(pkts[0]));
    HELPER_EXPECT_SUCCESS(forwarder->on_video(pkts[1]));
    HELPER_EXPECT_SUCCESS(forwarder->on_audio(pkts[2]));
    HELPER_EXPECT_SUCCESS(forwarder->on_video(pkts[3]));
    for (int i = 0; i < 4; i++) {
        srs_freep(pkts[i]);
    }

    MockRtmpClientForForwarder *sdk = new MockRtmpClientForForwarder();
    sdk->send_error_ = srs_error_new(ERROR_SOCKET_WRITE, "connection reset by peer");
    sdk->fail_send_at_ = 1;
    forwarder->sdk_ = sdk;
    srs_freep(forwarder->trd_);
    forwarder->trd_ = new MockCoroutineForForwarder(10);

    err = forwarder->forward();
    EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
    string desc = srs_error_desc(err);
    srs_freep(err);
    EXPECT_NE(string::npos, desc.find("send metadata")) << desc;

    EXPECT_EQ(1, sdk->send_count_);
    ASSERT_EQ(1, (int)sdk->sent_.size());
    EXPECT_EQ(SrsFrameTypeScript, sdk->sent_[0].type_);
    EXPECT_EQ('m', utest_forward_marker(sdk->sent_[0]));
    EXPECT_EQ(0, sdk->recv_count_);
    EXPECT_EQ(4, forwarder->queue_->size());
}

// A forward is running: each pithy print period the forwarder logs the push's kbps line with the number of messages it
// sent in that loop, including a loop with nothing to send while the publisher is idle.
VOID TEST(AppForwardTest, Forwarding_KbpsLineLoggedWithMessagesSent)
{
    srs_error_t err;

    PithyPrintAtOnceScope pithy(SrsPithyPrint::create_forwarder);

    SrsUniquePtr<MockOriginHub> hub(new MockOriginHub());
    SrsUniquePtr<MockAppFactoryForForwarderCycle> factory(new MockAppFactoryForForwarderCycle());
    SrsUniquePtr<MockAppConfig> config(new MockAppConfig());
    SrsUniquePtr<MockRequest> req(new MockRequest("__defaultVhost__", "live", "livestream"));

    SrsUniquePtr<SrsForwarder> forwarder(new SrsForwarder(hub.get(), req.get()));
    HELPER_EXPECT_SUCCESS(forwarder->initialize(req.get(), "127.0.0.1:19350"));
    utest_forward_wire(forwarder.get(), factory.get(), config.get(), 2);

    // The publisher sends a keyframe and an audio frame in the first loop, then nothing.
    MockRtmpClientForForwarder *sdk = new MockRtmpClientForForwarder();
    sdk->live_forwarder_ = forwarder.get();
    sdk->live_.push_back(utest_forward_batch(utest_forward_video_keyframe(100, 'K'), utest_forward_audio_raw(110, 'a')));
    factory->next_ = sdk;

    err = forwarder->do_cycle();
    EXPECT_EQ(ERROR_THREAD_DUMMY, srs_error_code(err));
    srs_freep(err);

    EXPECT_STREQ("Ka", utest_forward_markers(sdk->sent_).c_str());
    ASSERT_EQ(2, (int)sdk->kbps_labels_.size());
    EXPECT_EQ(SRS_CONSTS_LOG_FOWARDER, sdk->kbps_labels_.at(0));
    EXPECT_EQ(2, sdk->kbps_msgs_.at(0));
    EXPECT_EQ(SRS_CONSTS_LOG_FOWARDER, sdk->kbps_labels_.at(1));
    EXPECT_EQ(0, sdk->kbps_msgs_.at(1));
}

// A segment file of the path, of duration ms long, written to the disk unless it is gone.
static SrsFragment *fragment_utest_segment(std::string path, int64_t duration, bool gone)
{
    if (gone) {
        ::unlink(path.c_str());
    } else {
        FILE *fp = ::fopen(path.c_str(), "wb");
        if (fp) {
            ::fputs("ts", fp);
            ::fclose(fp);
        }
    }

    SrsFragment *fragment = new SrsFragment();
    fragment->set_path(path);
    fragment->append(1000);
    fragment->append(1000 + duration);
    return fragment;
}

static bool fragment_utest_file_exists(std::string path)
{
    return ::access(path.c_str(), F_OK) == 0;
}

// HLS (or DASH) with cleanup on deletes the segments that leave the window. When an expired segment file is already
// gone, such as removed by the operator's cleanup job or a sync to a CDN that moves the files, the delete fails with a
// warning, and the window still deletes the other expired files, frees every expired segment and keeps the live
// segments and their files.
VOID TEST(AppFragmentTest, CleanupExpiredSegmentAlreadyDeleted_OtherExpiredFilesStillDeleted)
{
    string gone = _srs_tmp_file_prefix + "fragment-cleanup-gone.ts";
    string old = _srs_tmp_file_prefix + "fragment-cleanup-old.ts";
    string live1 = _srs_tmp_file_prefix + "fragment-cleanup-live1.ts";
    string live2 = _srs_tmp_file_prefix + "fragment-cleanup-live2.ts";

    SrsUniquePtr<SrsFragmentWindow> window(new SrsFragmentWindow());
    window->append(fragment_utest_segment(gone, 4000, true));
    window->append(fragment_utest_segment(old, 4000, false));
    window->append(fragment_utest_segment(live1, 4000, false));
    window->append(fragment_utest_segment(live2, 4000, false));

    // A 5s window keeps the last two 4s segments; the first two expire.
    window->shrink(5 * SRS_UTIME_SECONDS);
    ASSERT_EQ(2, window->size());
    ASSERT_EQ(2, (int)window->expired_fragments_.size());

    window->clear_expired(true);
    EXPECT_EQ(0, (int)window->expired_fragments_.size());
    EXPECT_FALSE(fragment_utest_file_exists(old));

    EXPECT_EQ(2, window->size());
    EXPECT_EQ(live1, dynamic_cast<SrsFragment *>(window->at(0))->fullpath());
    EXPECT_EQ(live2, dynamic_cast<SrsFragment *>(window->at(1))->fullpath());
    EXPECT_TRUE(fragment_utest_file_exists(live1));
    EXPECT_TRUE(fragment_utest_file_exists(live2));

    ::unlink(live1.c_str());
    ::unlink(live2.c_str());
}

#ifdef SRS_GB28181
// The RTMP output of a GB28181 session, as the origin gets it: each message the muxer sends goes into the test's list,
// which outlives the client the muxer frees. The send numbered fail_at (1-based) fails, as an origin that closes the
// publish.
class GbUtestRtmpOrigin : public MockRtmpClient
{
public:
    std::vector<string> *sent_;
    int fail_at_;
    int nn_sends_;

public:
    GbUtestRtmpOrigin(std::vector<string> *sent, int fail_at = 0)
    {
        sent_ = sent;
        fail_at_ = fail_at;
        nn_sends_ = 0;
    }
    virtual ~GbUtestRtmpOrigin()
    {
    }

public:
    virtual srs_error_t send_and_free_message(SrsMediaPacket *msg)
    {
        nn_sends_++;
        if (nn_sends_ == fail_at_) {
            srs_freep(msg);
            return srs_error_new(ERROR_SOCKET_WRITE, "origin closed the publish");
        }

        sent_->push_back(string(msg->payload(), msg->size()));
        srs_freep(msg);
        return srs_success;
    }
};

// A GB28181 camera on the media connection of its session, after the SIP server reserved the session by the publish
// API and the camera's first packet bound it. The connection reads the camera's RTP over TCP (RFC 4571) from the io;
// the session's muxer publishes to the RTMP clients the test gives the factory.
class GbUtestCamera
{
public:
    MockAppFactoryForGbMuxer factory_;
    // The coroutines of the connection and of the session.
    MockCoroutineForForwarder owner_;
    MockCoroutineForForwarder session_owner_;
    SrsGbSession *session_;
    SrsGbMuxer *muxer_;
    SrsGbMediaTcpConn *conn_;
    MockBufferIO *io_;
    uint16_t seq_;

public:
    GbUtestCamera() : owner_(1000), session_owner_(1000)
    {
        seq_ = 100;

        session_ = new SrsGbSession();
        session_->assemble();
        session_->setup_owner(NULL, &session_owner_, NULL);
        // The publish API sets the device through the session's interface.
        ISrsGbSession *session = session_;
        session->device_id_ = "34020000001320000001";
        muxer_ = dynamic_cast<SrsGbMuxer *>(session_->muxer_);
        muxer_->setup("rtmp://127.0.0.1/live/[stream]");
        muxer_->app_factory_ = &factory_;

        conn_ = new SrsGbMediaTcpConn();
        conn_->assemble();
        io_ = new MockBufferIO();
        conn_->conn_ = io_;
        conn_->owner_coroutine_ = &owner_;
        conn_->session_ = session_;
    }
    ~GbUtestCamera()
    {
        conn_->owner_coroutine_ = NULL;
        conn_->session_ = NULL;
        srs_freep(conn_);
        srs_freep(session_);
    }

public:
    // The bytes as the payload of one RTP packet of the camera, framed by its 2 bytes of length.
    void send_rtp(const string &payload)
    {
        string rtp("\x80\x60", 2);
        rtp.push_back((char)(seq_ >> 8));
        rtp.push_back((char)(seq_ & 0xff));
        rtp.append(string("\x00\x01\x5f\x90\x0b\xeb\xc2\x02", 8));
        rtp.append(payload);
        seq_++;

        uint8_t length[2];
        length[0] = (uint8_t)(rtp.size() >> 8);
        length[1] = (uint8_t)(rtp.size() & 0xff);
        io_->append(length, 2);
        io_->append((uint8_t *)rtp.data(), (int)rtp.size());
    }
};

// The PSM of a camera: its video on 0xe0 and its audio on 0xc0, by stream type, 0 for none.
static string gb_utest_psm(uint8_t video_type, uint8_t audio_type)
{
    string entries;
    if (video_type) {
        entries.push_back((char)video_type);
        entries.append(string("\xe0\x00\x00", 3));
    }
    if (audio_type) {
        entries.push_back((char)audio_type);
        entries.append(string("\xc0\x00\x00", 3));
    }

    string body("\x80\x01\x00\x00\x00", 5);
    body.push_back((char)entries.size());
    return ps_pes(0xbc, body + entries + string(4, '\0'));
}

// A video (0xe0) or audio (0xc0) PES of a camera, with its PTS in 90kHz.
static string gb_utest_pes(uint8_t sid, uint32_t pts, const string &payload)
{
    string h("\x80\x80\x05", 3);
    h.push_back((char)(0x21 | ((pts >> 29) & 0x0e)));
    h.push_back((char)((pts >> 22) & 0xff));
    h.push_back((char)(((pts >> 14) & 0xfe) | 0x01));
    h.push_back((char)((pts >> 7) & 0xff));
    h.push_back((char)(((pts << 1) & 0xfe) | 0x01));
    return ps_pes(sid, h + payload);
}

static string gb_utest_annexb(const char *nalu, int size)
{
    return string("\x00\x00\x00\x01", 4) + string(nalu, size);
}

// An ADTS frame of 44.1kHz mono AAC: the 2-bit profile (1 for LC, 3 is reserved) and the bytes of raw data.
static string gb_utest_adts(uint8_t profile, int nb_raw)
{
    int length = 7 + nb_raw;
    string adts("\xff\xf1", 2);
    adts.push_back((char)((profile << 6) | (4 << 2)));
    adts.push_back((char)(0x40 | ((length >> 11) & 0x03)));
    adts.push_back((char)((length >> 3) & 0xff));
    adts.push_back((char)(((length & 0x07) << 5) | 0x1f));
    adts.push_back((char)0xfc);
    return adts + string(nb_raw, '\x21');
}

static const char gb_utest_h264_sps[] = {0x67, 0x42, 0x00, 0x1f, (char)0xac, (char)0xd9};
static const char gb_utest_h264_pps[] = {0x68, (char)0xeb, (char)0xe3, (char)0xcb};
static const char gb_utest_h264_idr[] = {0x65, (char)0x88, (char)0x84, 0x01};
static const char gb_utest_h264_p[] = {0x41, (char)0x9a, 0x02, 0x03};
static const char gb_utest_h265_vps[] = {0x40, 0x01, 0x0C, 0x01, (char)0xFF, (char)0xFF, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, (char)0x90,
                                         0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x5D, (char)0xAC, 0x09};
static const char gb_utest_h265_sps[] = {0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, (char)0x90, 0x00, 0x00, 0x03, 0x00,
                                         0x00, 0x03, 0x00, 0x5D, (char)0xA0, 0x02, (char)0x80, (char)0x80, 0x2D, 0x16, 0x59, 0x59,
                                         (char)0xA4, (char)0x93, 0x2B, (char)0xC0, 0x5A, 0x70, (char)0x80};
static const char gb_utest_h265_pps[] = {0x44, 0x01, (char)0xC1, 0x73, (char)0xD1, (char)0x89, 0x00};
static const char gb_utest_h265_idr[] = {0x26, 0x01, (char)0xaf, 0x06};

// The H.264 key frame of a camera, with its SPS and PPS, then a P frame.
static string gb_utest_h264_key()
{
    return gb_utest_annexb(gb_utest_h264_sps, sizeof(gb_utest_h264_sps)) + gb_utest_annexb(gb_utest_h264_pps, sizeof(gb_utest_h264_pps)) +
           gb_utest_annexb(gb_utest_h264_idr, sizeof(gb_utest_h264_idr));
}

static string gb_utest_h264_inter()
{
    return gb_utest_annexb(gb_utest_h264_p, sizeof(gb_utest_h264_p));
}

// The log lines that hold every text.
static int gb_utest_count(const std::vector<string> &lines, const string &a, const string &b = "")
{
    int n = 0;
    for (int i = 0; i < (int)lines.size(); i++) {
        const string &line = lines.at(i);
        if (line.find(a) != string::npos && line.find(b) != string::npos) {
            n++;
        }
    }
    return n;
}

// A GB28181 camera with G.711 A-law audio, common in GB28181, lists it in its PSM as 0x90. RTMP from GB28181 takes
// only AAC, so the muxer refuses each audio frame as not ADTS and the session ignores it with a warning; the video is
// published, its sequence header first, and the muxer logs what it sends.
VOID TEST(AppGb28181Test, CameraWithG711Audio_VideoPublishedAudioDropped)
{
    srs_error_t err = srs_success;

    MockLogForSignal log;
    PithyPrintAtOnceScope pprint(SrsPithyPrint::create_caster);
    std::vector<string> sent;
    GbUtestCamera camera;
    camera.factory_.clients_.push_back(new GbUtestRtmpOrigin(&sent));

    // A pack is handed on when the next one starts, so the third pack hands on the second.
    string g711(160, '\xd5');
    for (int i = 0; i < 3; i++) {
        uint32_t pts = 90000 + i * 3600;
        string pack = ps_pack_header();
        if (i == 0) {
            pack += ps_system_header() + gb_utest_psm(SrsTsStreamVideoH264, 0x90);
        }
        pack += gb_utest_pes(0xe0, pts, i == 0 ? gb_utest_h264_key() : gb_utest_h264_inter());
        pack += gb_utest_pes(0xc0, pts, g711);
        camera.send_rtp(pack);
    }

    ISrsLog *original = _srs_log;
    _srs_log = &log;
    err = camera.conn_->cycle();
    _srs_log = original;
    HELPER_EXPECT_SUCCESS(err);

    // The origin got the AVC sequence header, then the key frame.
    ASSERT_EQ(2, (int)sent.size());
    EXPECT_EQ(0x17, (uint8_t)sent[0][0]);
    EXPECT_EQ(SrsVideoAvcFrameTraitSequenceHeader, sent[0][1]);
    EXPECT_EQ(0x17, (uint8_t)sent[1][0]);
    EXPECT_EQ(SrsVideoAvcFrameTraitNALU, sent[1][1]);
    EXPECT_EQ(1, camera.factory_.created_);

    EXPECT_EQ(2, gb_utest_count(log.warns_, "Muxer: Ignore audio err", "not adts"));
    EXPECT_EQ(2, gb_utest_count(log.traces_, "Muxer: send msg V"));
    EXPECT_EQ(2u, camera.session_->media_packs_);
}

// The origin of the RTMP output is down, so each connect fails. The session ignores each pack's audio and video with a
// warning naming the connect, tries again at the next message, and goes on counting the camera's packs.
VOID TEST(AppGb28181Test, CameraWhenOriginRefusesConnect_MediaDroppedSessionGoesOn)
{
    srs_error_t err = srs_success;

    MockLogForSignal log;
    GbUtestCamera camera;
    for (int i = 0; i < 4; i++) {
        MockRtmpClient *client = new MockRtmpClient();
        client->connect_error_ = srs_error_new(ERROR_SOCKET_CONNECT, "origin down");
        camera.factory_.clients_.push_back(client);
    }

    for (int i = 0; i < 3; i++) {
        uint32_t pts = 90000 + i * 3600;
        string pack = ps_pack_header();
        if (i == 0) {
            pack += ps_system_header() + gb_utest_psm(SrsTsStreamVideoH264, SrsTsStreamAudioAAC);
        }
        pack += gb_utest_pes(0xe0, pts, i == 0 ? gb_utest_h264_key() : gb_utest_h264_inter());
        pack += gb_utest_pes(0xc0, pts, gb_utest_adts(1, 4));
        camera.send_rtp(pack);
    }

    ISrsLog *original = _srs_log;
    _srs_log = &log;
    err = camera.conn_->cycle();
    _srs_log = original;
    HELPER_EXPECT_SUCCESS(err);

    EXPECT_EQ(4, camera.factory_.created_);
    EXPECT_TRUE(camera.muxer_->sdk_ == NULL);
    EXPECT_EQ(2, gb_utest_count(log.warns_, "Muxer: Ignore audio err", "connect"));
    EXPECT_EQ(2, gb_utest_count(log.warns_, "Muxer: Ignore video err", "connect"));
    EXPECT_EQ(2u, camera.session_->media_packs_);
    EXPECT_EQ(4u, camera.session_->media_msgs_);
}

// An audio-only camera publishes AAC, and the origin closes the publish at the second message. The muxer closes its
// client and the session ignores that frame with a warning. The next frame connects again, and the new publish gets
// the AAC sequence header again, which an origin needs before the frames.
VOID TEST(AppGb28181Test, AudioOnlyCameraWhenOriginClosesPublish_NextPublishGetsSequenceHeader)
{
    srs_error_t err = srs_success;

    MockLogForSignal log;
    std::vector<string> first;
    std::vector<string> second;
    GbUtestCamera camera;
    camera.factory_.clients_.push_back(new GbUtestRtmpOrigin(&first, 2));
    camera.factory_.clients_.push_back(new GbUtestRtmpOrigin(&second));

    for (int i = 0; i < 4; i++) {
        string pack = ps_pack_header();
        if (i == 0) {
            pack += ps_system_header() + gb_utest_psm(0, SrsTsStreamAudioAAC);
        }
        pack += gb_utest_pes(0xc0, 90000 + i * 2090, gb_utest_adts(1, 4));
        camera.send_rtp(pack);
    }

    ISrsLog *original = _srs_log;
    _srs_log = &log;
    err = camera.conn_->cycle();
    _srs_log = original;
    HELPER_EXPECT_SUCCESS(err);

    // The first publish got the sequence header (AAC, 44.1kHz, 16-bit mono; LC, index 4, 1 channel), then closed at
    // the first frame.
    ASSERT_EQ(1, (int)first.size());
    EXPECT_EQ(string("\xae\x00\x12\x08", 4), first[0]);
    EXPECT_EQ(1, gb_utest_count(log.warns_, "Muxer: Ignore audio err", "origin closed the publish"));

    // The second publish got a sequence header of its own.
    EXPECT_EQ(2, camera.factory_.created_);
    int nn_sh = 0;
    for (int i = 0; i < (int)second.size(); i++) {
        if (second[i] == string("\xae\x00\x12\x08", 4)) {
            nn_sh++;
        }
    }
    EXPECT_EQ(1, nn_sh);
}

// A camera's ADTS headers are corrupt: one has the reserved profile 3, so no sequence header can be made and the frame
// is refused with a warning; one has a frame length of only its header, so it holds no audio and is skipped. The next
// good frame makes the sequence header, and the stream goes on.
VOID TEST(AppGb28181Test, CameraWithCorruptAdtsHeaders_FramesDroppedStreamGoesOn)
{
    srs_error_t err = srs_success;

    MockLogForSignal log;
    std::vector<string> sent;
    GbUtestCamera camera;
    camera.factory_.clients_.push_back(new GbUtestRtmpOrigin(&sent));

    string frames[] = {gb_utest_adts(3, 4), gb_utest_adts(1, 0), gb_utest_adts(1, 4), gb_utest_adts(1, 4)};
    for (int i = 0; i < 4; i++) {
        string pack = ps_pack_header();
        if (i == 0) {
            pack += ps_system_header() + gb_utest_psm(0, SrsTsStreamAudioAAC);
        }
        pack += gb_utest_pes(0xc0, 90000 + i * 2090, frames[i]);
        camera.send_rtp(pack);
    }

    ISrsLog *original = _srs_log;
    _srs_log = &log;
    err = camera.conn_->cycle();
    _srs_log = original;
    HELPER_EXPECT_SUCCESS(err);

    EXPECT_EQ(1, gb_utest_count(log.warns_, "Muxer: Ignore audio err", "invalid aac object"));
    EXPECT_EQ(1, gb_utest_count(log.warns_, "Muxer: Ignore audio err"));
    // The third frame's sequence header went once the frame after it was queued.
    ASSERT_EQ(1, (int)sent.size());
    EXPECT_EQ(string("\xae\x00\x12\x08", 4), sent[0]);
}

// An HEVC camera's first VPS is cut short, so the HEVC sequence header cannot be made: the key frame's video is
// refused with a warning and nothing is published. The next key frame carries a whole VPS, and the origin gets the
// sequence header in enhanced RTMP.
VOID TEST(AppGb28181Test, HevcCameraWithVpsCutShort_PublishesAtNextKeyFrame)
{
    srs_error_t err = srs_success;

    MockLogForSignal log;
    std::vector<string> sent;
    GbUtestCamera camera;
    camera.factory_.clients_.push_back(new GbUtestRtmpOrigin(&sent));

    string sps_pps = gb_utest_annexb(gb_utest_h265_sps, sizeof(gb_utest_h265_sps)) +
                     gb_utest_annexb(gb_utest_h265_pps, sizeof(gb_utest_h265_pps));
    string idr = gb_utest_annexb(gb_utest_h265_idr, sizeof(gb_utest_h265_idr));
    string videos[] = {
        gb_utest_annexb(gb_utest_h265_vps, 4) + sps_pps + idr,
        gb_utest_annexb(gb_utest_h265_vps, sizeof(gb_utest_h265_vps)) + sps_pps + idr,
        idr,
    };
    for (int i = 0; i < 3; i++) {
        string pack = ps_pack_header();
        if (i == 0) {
            pack += ps_system_header() + gb_utest_psm(SrsTsStreamVideoHEVC, 0);
        }
        pack += gb_utest_pes(0xe0, 90000 + i * 3600, videos[i]);
        camera.send_rtp(pack);
    }

    ISrsLog *original = _srs_log;
    _srs_log = &log;
    err = camera.conn_->cycle();
    _srs_log = original;
    HELPER_EXPECT_SUCCESS(err);

    EXPECT_EQ(1, gb_utest_count(log.warns_, "Muxer: Ignore video err", "hevc mux sequence header"));
    // The second key frame's sequence header, in enhanced RTMP: a key frame of the SequenceStart type, hvc1.
    ASSERT_EQ(1, (int)sent.size());
    EXPECT_EQ(0x90, (uint8_t)sent[0][0]);
    EXPECT_EQ(string("hvc1"), sent[0].substr(1, 4));
}

// A camera sends RTP whose payload is no PS, such as a camera set to send H.264 over RTP instead of PS. Each packet
// enters recover mode, which drops it and waits for a pack header. After 16 recovers the connection gives up, logs the
// error, and ends.
VOID TEST(AppGb28181Test, CameraSendsNoPs_ConnectionEndsAfterMaxRecover)
{
    srs_error_t err = srs_success;

    MockLogForSignal log;
    GbUtestCamera camera;
    for (int i = 0; i < 20; i++) {
        camera.send_rtp(string(gb_utest_h264_idr, sizeof(gb_utest_h264_idr)) + string(200, '\x5a'));
    }

    ISrsLog *original = _srs_log;
    _srs_log = &log;
    err = camera.conn_->cycle();
    _srs_log = original;
    HELPER_EXPECT_SUCCESS(err);

    // The 17th packet ends it; the 3 after it are never read.
    EXPECT_EQ(17, camera.owner_.pull_count_);
    EXPECT_EQ(1, gb_utest_count(log.errors_, "serve error", "exceed max recover"));
    EXPECT_EQ(0u, camera.session_->media_packs_);
    EXPECT_FALSE(camera.conn_->is_connected());
}

// A camera cuts its PS stream into RTP packets at any byte: one packet ends 10 bytes into the next pack header. The
// connection holds those bytes, joins them to the next packet, and no pack is lost. An RTP packet with no payload, as
// a keepalive (RFC 6263), carries nothing to decode.
VOID TEST(AppGb28181Test, CameraCutsPackHeaderAcrossRtpPackets_HeldForNextPacket)
{
    srs_error_t err = srs_success;

    MockLogForSignal log;
    std::vector<string> sent;
    GbUtestCamera camera;
    camera.factory_.clients_.push_back(new GbUtestRtmpOrigin(&sent));

    string packs[5];
    for (int i = 0; i < 5; i++) {
        packs[i] = ps_pack_header();
        if (i == 0) {
            packs[i] += ps_system_header() + gb_utest_psm(SrsTsStreamVideoH264, 0);
        }
        packs[i] += gb_utest_pes(0xe0, 90000 + i * 3600, i == 0 ? gb_utest_h264_key() : gb_utest_h264_inter());
    }

    camera.send_rtp(packs[0] + packs[1].substr(0, 10));
    camera.send_rtp(packs[1].substr(10));
    camera.send_rtp("");
    camera.send_rtp(packs[2]);
    camera.send_rtp(packs[3]);
    camera.send_rtp(packs[4]);

    ISrsLog *original = _srs_log;
    _srs_log = &log;
    err = camera.conn_->cycle();
    _srs_log = original;
    HELPER_EXPECT_SUCCESS(err);

    // Every pack but the last reached the session, none recovered, and the cut one was held once.
    EXPECT_EQ(4u, camera.session_->media_packs_);
    EXPECT_EQ(0u, camera.session_->media_recovered_);
    EXPECT_EQ(1u, camera.session_->media_reserved_);
    EXPECT_EQ(1, gb_utest_count(log.traces_, "PS: Reserved bytes for next loop", "left=10"));
    EXPECT_EQ(1, gb_utest_count(log.traces_, "PS: Consume reserved=10B"));

    // The origin got the sequence header, the key frame and the P frame of the cut pack, in order.
    ASSERT_EQ(4, (int)sent.size());
    EXPECT_EQ(SrsVideoAvcFrameTraitSequenceHeader, sent[0][1]);
    EXPECT_EQ(0x17, (uint8_t)sent[1][0]);
    EXPECT_EQ(0x27, (uint8_t)sent[2][0]);
    EXPECT_EQ(string(gb_utest_h264_p, sizeof(gb_utest_h264_p)), sent[2].substr(sent[2].size() - sizeof(gb_utest_h264_p)));
}

// A camera disconnects in the middle of an RTP packet. The connection ends as a closed client, drops the packet cut
// short and the pack it had not handed on, and is no longer connected.
VOID TEST(AppGb28181Test, CameraDisconnectsInsideRtpPacket_ConnectionEnds)
{
    srs_error_t err = srs_success;

    MockLogForSignal log;
    std::vector<string> sent;
    GbUtestCamera camera;
    camera.factory_.clients_.push_back(new GbUtestRtmpOrigin(&sent));

    for (int i = 0; i < 2; i++) {
        string pack = ps_pack_header();
        if (i == 0) {
            pack += ps_system_header() + gb_utest_psm(SrsTsStreamVideoH264, 0);
        }
        pack += gb_utest_pes(0xe0, 90000 + i * 3600, i == 0 ? gb_utest_h264_key() : gb_utest_h264_inter());
        camera.send_rtp(pack);
    }
    // The length says 200 bytes; only 50 came before the camera left.
    camera.io_->append(string("\x00\xc8", 2) + string(50, '\x80'));

    ISrsLog *original = _srs_log;
    _srs_log = &log;
    err = camera.conn_->cycle();
    _srs_log = original;
    HELPER_EXPECT_SUCCESS(err);

    EXPECT_EQ(1u, camera.session_->media_packs_);
    EXPECT_FALSE(camera.conn_->is_connected());
    EXPECT_EQ(1, gb_utest_count(log.warns_, "client disconnect peer"));
}

// A session that receives media prints its stats at the pithy print interval: how long it has lived, and the packs and
// messages of all its media connections and of the current one.
VOID TEST(AppGb28181Test, SessionPrintsMediaStatsAtPithyInterval)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsGbSession> session(new SrsGbSession());
    session->assemble();
    MockCoroutineForForwarder owner(1);
    session->setup_owner(NULL, &owner, &owner);
    // The media connected, so no wait for it times out.
    session->connecting_starttime_ = 0;
    session->ppp_->info_.interval_ = 0;

    MockGbMuxer *muxer = new MockGbMuxer();
    srs_freep(session->muxer_);
    session->muxer_ = muxer;

    // The camera's pack of one audio message.
    MockPackContext ctx;
    SrsPsPacket ps(NULL);
    SrsTsMessage audio;
    audio.sid_ = SrsTsPESStreamIdAudioCommon;
    std::vector<SrsTsMessage *> msgs;
    msgs.push_back(&audio);
    session->on_ps_pack(&ctx, &ps, msgs);

    MockLogForSignal log;
    ISrsLog *original = _srs_log;
    _srs_log = &log;
    err = session->do_cycle();
    _srs_log = original;
    EXPECT_EQ(ERROR_THREAD_DUMMY, srs_error_code(err));
    srs_freep(err);

    string media = srs_fmt_sprintf("media(id=%u, alive=0s, packs=1 ", ctx.media_id_);
    EXPECT_EQ(1, gb_utest_count(log.traces_, "Session: Alive=0s, packs=1, recover=0, reserved=0, msgs=1, drop=0", media));
}

// The session logs each state change with the device it serves, as the publish API set it through the session's
// interface, so an operator can tell which camera connected or dropped.
VOID TEST(AppGb28181Test, SessionStateChangeLog_NamesDevice)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsGbSession> session(new SrsGbSession());
    session->assemble();
    session->connecting_starttime_ = 0;
    // As SrsGoApiGbPublish::bind_session sets it.
    ISrsGbSession *raw_session = session.get();
    raw_session->device_id_ = "34020000001320000001";

    // The camera's media connected.
    SrsGbMediaTcpConn *media = dynamic_cast<SrsGbMediaTcpConn *>(session->media_.get());
    ASSERT_TRUE(media != NULL);
    media->connected_ = true;

    MockLogForSignal log;
    ISrsLog *original = _srs_log;
    _srs_log = &log;
    err = session->drive_state();
    _srs_log = original;
    HELPER_EXPECT_SUCCESS(err);

    EXPECT_EQ(SrsGbSessionStateEstablished, session->state_);
    EXPECT_EQ(1, gb_utest_count(log.traces_, "Session: Change device=34020000001320000001, state=Init->Established"));
}

// Another program, such as a second SRS, holds the GB28181 media port. The caster's listen fails to bind, so the
// server does not start with a GB port it cannot serve.
VOID TEST(AppGb28181Test, GbPortHeldByAnotherProgram_ListenFails)
{
    srs_error_t err = srs_success;

    int port = 0;
    HELPER_ASSERT_SUCCESS(srs_utest_free_port(SOCK_STREAM, &port));

    int other = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(other, 0);
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    ASSERT_EQ(0, ::bind(other, (sockaddr *)&addr, sizeof(addr)));
    ASSERT_EQ(0, ::listen(other, 1));

    MockAppConfigForGbListener config;
    config.stream_caster_listen_port_ = port;
    SrsUniquePtr<SrsGbListener> listener(new SrsGbListener(NULL));
    listener->config_ = &config;

    SrsConfDirective conf;
    conf.name_ = "stream_caster";
    conf.args_.push_back("gb28181");
    HELPER_EXPECT_SUCCESS(listener->initialize(&conf));

    err = listener->listen();
    EXPECT_EQ(ERROR_SOCKET_BIND, srs_error_code(err));
    srs_freep(err);

    ::close(other);
}

// The config has two gb28181 stream casters. The server sets up its one GB listener for each in turn; the second
// cannot mount the publish API, whose path the first already has, so the server does not start.
VOID TEST(AppGb28181Test, TwoGbStreamCasters_SecondPublishApiRefused)
{
    srs_error_t err = srs_success;

    MockAppConfigForGbListener config;
    config.stream_caster_listen_port_ = 9000;
    SrsHttpServeMux mux;
    MockApiServerOwnerForGbListener owner;
    owner.mux_ = &mux;

    SrsUniquePtr<SrsGbListener> listener(new SrsGbListener(&owner));
    listener->config_ = &config;
    MockIpListenerForGbListen *media = new MockIpListenerForGbListen();
    srs_freep(listener->media_listener_);
    listener->media_listener_ = media;

    SrsConfDirective first;
    first.name_ = "stream_caster";
    first.args_.push_back("gb28181");
    HELPER_EXPECT_SUCCESS(listener->initialize(&first));
    HELPER_EXPECT_SUCCESS(listener->listen());

    config.stream_caster_listen_port_ = 9001;
    SrsConfDirective second;
    second.name_ = "stream_caster";
    second.args_.push_back("gb28181");
    HELPER_EXPECT_SUCCESS(listener->initialize(&second));
    err = listener->listen();
    EXPECT_EQ(ERROR_HTTP_PATTERN_DUPLICATED, srs_error_code(err));
    srs_freep(err);

    listener->media_listener_ = NULL;
    srs_freep(media);
}

// The camera connects to the GB28181 media port. The listener makes a media connection for it, run by its own
// coroutine in the GB manager, which reads the camera's packets (here an RTCP report, dropped) until the camera leaves;
// the connection then leaves the manager and is freed.
VOID TEST(AppGb28181Test, CameraConnectsToGbPort_ServedUntilItLeaves)
{
    srs_error_t err = srs_success;

    StUtestTcpPair pair;
    HELPER_ASSERT_SUCCESS(pair.open());

    SrsResourceManager manager("GB");
    manager.assemble();
    SrsUniquePtr<SrsGbListener> listener(new SrsGbListener(NULL));
    listener->gb_manager_ = &manager;

    MockLogForSignal log;
    ISrsLog *original = _srs_log;
    _srs_log = &log;

    err = listener->on_tcp_client(listener->media_listener_, pair.take_server());
    EXPECT_EQ(1u, manager.size());

    // The camera sends an RTCP sender report, then leaves.
    string rtcp("\x00\x0c\x80\xc8\x00\x06\x00\x00\x04\xd2\x00\x00\x00\x00", 14);
    SrsStSocket camera(pair.client_);
    srs_error_t werr = camera.write((void *)rtcp.data(), rtcp.size(), NULL);
    srs_close_stfd(pair.client_);

    for (int i = 0; i < 100 && manager.zombies_.empty(); i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
    manager.clear();
    manager.clear();
    _srs_log = original;

    HELPER_EXPECT_SUCCESS(err);
    HELPER_EXPECT_SUCCESS(werr);
    EXPECT_EQ(1, gb_utest_count(log.warns_, "PS: Drop RTCP packets nn=1"));
    EXPECT_TRUE(manager.empty());
}

// State Threads cannot create a coroutine: the process is out of memory or over its address space limit.
static void *gb_st_thread_create_out_of_memory(void *(* /*start*/)(void *arg), void * /*arg*/, int /*joinable*/, int /*stack_size*/)
{
    return NULL;
}

// The camera connects while no coroutine can be created for its media connection. The listener fails with the ST
// error, the connection leaves the GB manager, and the camera's socket is closed.
VOID TEST(AppGb28181Test, CameraConnectsWhenNoCoroutineCanBeCreated_SocketClosed)
{
    srs_error_t err = srs_success;

    StUtestTcpPair pair;
    HELPER_ASSERT_SUCCESS(pair.open());

    SrsResourceManager manager("GB");
    manager.assemble();
    SrsUniquePtr<SrsGbListener> listener(new SrsGbListener(NULL));
    listener->gb_manager_ = &manager;

    _ST_THREAD_CREATE_PFN ov = _pfn_st_thread_create;
    _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)gb_st_thread_create_out_of_memory;
    err = listener->on_tcp_client(listener->media_listener_, pair.take_server());
    _pfn_st_thread_create = ov;

    EXPECT_EQ(ERROR_ST_CREATE_CYCLE_THREAD, srs_error_code(err));
    srs_freep(err);

    manager.clear();
    EXPECT_TRUE(manager.empty());

    // The camera reads the end of the connection.
    char b = 0;
    EXPECT_EQ(0, (int)srs_read(pair.client_, &b, 1, 1 * SRS_UTIME_SECONDS));
}

// The answer of the publish API to a request body, as the SIP server reads it.
static string gb_utest_publish(SrsGoApiGbPublish *api, const string &body, srs_error_t body_error = srs_success)
{
    MockResponseWriter w;
    MockHttpMessageForGbPublish r;
    r.body_content_ = body;
    r.body_error_ = body_error;

    srs_error_t err = api->serve_http(&w, &r);
    if (err != srs_success) {
        string desc = srs_error_desc(err);
        srs_freep(err);
        return desc;
    }
    return string(w.io.out_buffer.bytes(), w.io.out_buffer.length());
}

static string gb_utest_code(int code)
{
    return "\"code\":" + srs_strconv_format_int(code);
}

// The SIP server's publish requests that cannot reserve a session: its connection closes before the body ends, the body
// is no JSON object, or the device id or SSRC is missing or not a string. Each answers its error code and makes no
// session.
VOID TEST(AppGb28181Test, SipServerBadPublishRequests_RefusedWithCode)
{
    MockResourceManagerForGbPublish manager;
    MockAppFactoryForGbPublish factory;
    MockAppConfigForGbListener config;
    SrsConfDirective conf;
    conf.name_ = "stream_caster";
    SrsGoApiGbPublish api(&conf);
    api.config_ = &config;
    api.gb_manager_ = &manager;
    api.app_factory_ = &factory;

    string answer = gb_utest_publish(&api, "", srs_error_new(ERROR_SOCKET_READ, "sip server closed"));
    EXPECT_NE(string::npos, answer.find(gb_utest_code(ERROR_SOCKET_READ))) << answer;

    const char *bodies[] = {
        "id=34020000001320000001&ssrc=1234567890",
        "[\"34020000001320000001\", \"1234567890\"]",
        "{\"ssrc\":\"1234567890\"}",
        "{\"id\":34020000001320000001,\"ssrc\":\"1234567890\"}",
        "{\"id\":\"34020000001320000001\"}",
        "{\"id\":\"34020000001320000001\",\"ssrc\":1234567890}",
    };
    for (int i = 0; i < (int)(sizeof(bodies) / sizeof(bodies[0])); i++) {
        answer = gb_utest_publish(&api, bodies[i]);
        EXPECT_NE(string::npos, answer.find(gb_utest_code(ERROR_HTTP_DATA_INVALID))) << bodies[i] << ": " << answer;
    }

    EXPECT_TRUE(manager.id_map_.empty());
    EXPECT_TRUE(manager.fast_id_map_.empty());

    api.config_ = NULL;
    api.gb_manager_ = NULL;
    api.app_factory_ = NULL;
}

// The SIP server reserves a session for a camera by the publish API. While the session lives, the same device, or
// another device with the same SSRC, is refused as busy. When the session ends, the GB manager logs it by its
// description and frees it. A reservation whose coroutine cannot be created is refused and released at once, so the
// SIP server's retry succeeds.
VOID TEST(AppGb28181Test, SipServerReservesSession_BusyUntilItEnds)
{
    SrsResourceManager manager("GB", true);
    manager.assemble();
    MockAppConfigForGbListener config;
    config.stream_caster_listen_port_ = 9000;
    SrsConfDirective conf;
    conf.name_ = "stream_caster";
    SrsGoApiGbPublish api(&conf);
    api.config_ = &config;
    api.gb_manager_ = &manager;

    MockLogForSignal log;
    ISrsLog *original = _srs_log;
    _srs_log = &log;

    string ok = gb_utest_publish(&api, "{\"id\":\"34020000001320000001\",\"ssrc\":\"1234567890\"}");
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    string same_device = gb_utest_publish(&api, "{\"id\":\"34020000001320000001\",\"ssrc\":\"1234567891\"}");
    string same_ssrc = gb_utest_publish(&api, "{\"id\":\"34020000001320000002\",\"ssrc\":\"1234567890\"}");

    // The session ends, so its executor leaves the manager, then the session.
    SrsSharedResource<ISrsGbSession> *wrapper = dynamic_cast<SrsSharedResource<ISrsGbSession> *>(manager.find_by_id("34020000001320000001"));
    SrsGbSession *session = wrapper ? dynamic_cast<SrsGbSession *>(wrapper->get()) : NULL;
    if (session) {
        session->owner_coroutine_->interrupt();
    }
    for (int i = 0; i < 100 && manager.zombies_.empty(); i++) {
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }
    manager.clear();
    manager.clear();
    bool ended = manager.empty();
    int nn_before_dispose = gb_utest_count(log.traces_, "GB: before dispose resource(GBS)");
    int nn_disposing = gb_utest_count(log.traces_, "GB: disposing #0 resource(GBS)");

    // No coroutine for the next reservation of the device.
    _ST_THREAD_CREATE_PFN ov = _pfn_st_thread_create;
    _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)gb_st_thread_create_out_of_memory;
    string no_coroutine = gb_utest_publish(&api, "{\"id\":\"34020000001320000001\",\"ssrc\":\"1234567890\"}");
    _pfn_st_thread_create = ov;
    manager.clear();
    bool released = manager.empty() && !manager.find_by_id("34020000001320000001") && !manager.find_by_fast_id(1234567890);

    // The SIP server retries.
    string retry = gb_utest_publish(&api, "{\"id\":\"34020000001320000001\",\"ssrc\":\"1234567890\"}");
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    wrapper = dynamic_cast<SrsSharedResource<ISrsGbSession> *>(manager.find_by_id("34020000001320000001"));
    session = wrapper ? dynamic_cast<SrsGbSession *>(wrapper->get()) : NULL;
    if (session) {
        session->owner_coroutine_->interrupt();
    }
    for (int i = 0; i < 100 && manager.zombies_.empty(); i++) {
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }
    manager.clear();
    manager.clear();
    _srs_log = original;

    EXPECT_NE(string::npos, ok.find(gb_utest_code(ERROR_SUCCESS))) << ok;
    EXPECT_NE(string::npos, ok.find("\"port\":9000")) << ok;
    EXPECT_NE(string::npos, same_device.find(gb_utest_code(ERROR_SYSTEM_STREAM_BUSY))) << same_device;
    EXPECT_NE(string::npos, same_ssrc.find(gb_utest_code(ERROR_SYSTEM_STREAM_BUSY))) << same_ssrc;
    EXPECT_TRUE(session != NULL);
    EXPECT_TRUE(ended);
    // The executor and the shared resource each log the session by its description.
    EXPECT_EQ(2, nn_before_dispose);
    EXPECT_EQ(2, nn_disposing);

    EXPECT_NE(string::npos, no_coroutine.find(gb_utest_code(ERROR_ST_CREATE_CYCLE_THREAD))) << no_coroutine;
    EXPECT_TRUE(released);
    EXPECT_NE(string::npos, retry.find(gb_utest_code(ERROR_SUCCESS))) << retry;
    EXPECT_TRUE(manager.empty());

    api.config_ = NULL;
    api.gb_manager_ = NULL;
}
#endif

// A collector that answers the heartbeat with a whole JSON body.
static const char *heartbeat_utest_ok = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n{\"code\":0}";

// Runs one heartbeat tick of the server, which reads the heartbeat section of this config text, posting with the
// server's own HTTP client and statistic. Returns the warnings the tick logged.
static vector<string> heartbeat_utest_tick(string section, srs_error_t *perr)
{
    MockSrsConfig conf;
    *perr = conf.mock_parse(string(_MIN_OK_CONF) + "heartbeat { enabled on; " + section + " }");
    if (*perr != srs_success) {
        return vector<string>();
    }

    SrsHttpHeartbeat heartbeat;
    heartbeat.config_ = &conf;

    MockLogForSignal log;
    ISrsLog *original = _srs_log;
    _srs_log = &log;
    heartbeat.heartbeat();
    _srs_log = original;

    heartbeat.config_ = NULL;
    return log.warns_;
}

// The log line, such as a warning, that holds this text, or "" when no line does.
static string utest_log_find(const vector<string> &warns, string text)
{
    for (int i = 0; i < (int)warns.size(); i++) {
        if (warns[i].find(text) != string::npos) {
            return warns[i];
        }
    }
    return "";
}

// A collector that needs a bearer token: with auth enabled, each tick posts the server's report to the url's path
// with the configured token in the Authorization header, and the device id the operator named, and logs no warning.
VOID TEST(AppHeartbeatTest, CollectorWithBearerAuth_TickPostsTokenAndDevice)
{
    srs_error_t err;

    HttpUtestOneReplyServer collector(heartbeat_utest_ok);
    HELPER_ASSERT_SUCCESS(collector.start());

    vector<string> warns = heartbeat_utest_tick("url http://" + collector.server() + "/api/v1/servers; device_id srs-utest-01;"
                                                "auth { enabled on; type bearer; token t1; }",
                                                &err);
    HELPER_ASSERT_SUCCESS(err);

    EXPECT_STREQ("", utest_log_find(warns, "heartbeat err").c_str());
    string req = collector.request_;
    EXPECT_EQ(0, (int)req.find("POST /api/v1/servers HTTP/1.1\r\n")) << req;
    EXPECT_NE(string::npos, req.find("\r\nAuthorization: Bearer t1\r\n")) << req;
    EXPECT_NE(string::npos, req.find("\"device_id\":\"srs-utest-01\"")) << req;
}

// An operator writes the collector's token but leaves out "enabled on": auth is off, so the tick posts with no
// Authorization header, and the collector, not the server, refuses it.
VOID TEST(AppHeartbeatTest, CollectorAuthWithoutEnabled_TickPostsNoToken)
{
    srs_error_t err;

    HttpUtestOneReplyServer collector(heartbeat_utest_ok);
    HELPER_ASSERT_SUCCESS(collector.start());

    vector<string> warns = heartbeat_utest_tick("url http://" + collector.server() + "/api/v1/servers;"
                                                "auth { type bearer; token t1; }",
                                                &err);
    HELPER_ASSERT_SUCCESS(err);

    EXPECT_STREQ("", utest_log_find(warns, "heartbeat err").c_str());
    string req = collector.request_;
    EXPECT_EQ(0, (int)req.find("POST /api/v1/servers HTTP/1.1\r\n")) << req;
    EXPECT_EQ(string::npos, req.find("Authorization")) << req;
}

// The collector is down: the tick cannot post, logs a warning naming the url, and returns, so the server keeps
// running and tries again at the next tick.
VOID TEST(AppHeartbeatTest, CollectorDown_TickWarnsWithUrl)
{
    srs_error_t err;

    // Never started, so nothing listens on its port.
    HttpUtestOneReplyServer down("");
    string url = "http://" + down.server() + "/api/v1/servers";

    vector<string> warns = heartbeat_utest_tick("url " + url + ";", &err);
    HELPER_ASSERT_SUCCESS(err);

    string warn = utest_log_find(warns, "heartbeat err");
    EXPECT_NE(string::npos, warn.find("http post hartbeart uri failed. url=" + url)) << warn;
}

// An operator leaves the schema out of the heartbeat url. The config loads, but each tick logs that the url cannot be
// parsed and posts nothing, not even to the host and port the url names.
VOID TEST(AppHeartbeatTest, HeartbeatUrlWithoutSchema_TickWarnsAndPostsNothing)
{
    srs_error_t err;

    HttpUtestOneReplyServer collector(heartbeat_utest_ok);
    HELPER_ASSERT_SUCCESS(collector.start());
    string url = collector.server() + "/api/v1/servers";

    vector<string> warns = heartbeat_utest_tick("url " + url + ";", &err);
    HELPER_ASSERT_SUCCESS(err);
    // Give the collector a chance to accept, had the tick connected.
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    string warn = utest_log_find(warns, "heartbeat err");
    EXPECT_NE(string::npos, warn.find("http uri parse hartbeart url failed. url=" + url)) << warn;
    EXPECT_NE(string::npos, warn.find("no schema")) << warn;
    EXPECT_TRUE(collector.request_.empty()) << collector.request_;
}

// The collector answers the post but closes before its whole body is sent: the tick fails reading the body and logs
// it.
VOID TEST(AppHeartbeatTest, CollectorClosesMidBody_TickWarnsReadBody)
{
    srs_error_t err;

    HttpUtestOneReplyServer collector("HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n{\"code\"");
    HELPER_ASSERT_SUCCESS(collector.start());

    vector<string> warns = heartbeat_utest_tick("url http://" + collector.server() + "/api/v1/servers;", &err);
    HELPER_ASSERT_SUCCESS(err);

    EXPECT_EQ(0, (int)collector.request_.find("POST /api/v1/servers HTTP/1.1\r\n")) << collector.request_;
    string warn = utest_log_find(warns, "heartbeat err");
    EXPECT_NE(string::npos, warn.find("read body")) << warn;
}

// Removes a dir and everything in it, as an operator's cleanup job does.
void hls_utest_remove_all(const string &dir)
{
#ifdef _WIN32
    WIN32_FIND_DATAA e;
    HANDLE d = ::FindFirstFileA((dir + "/*").c_str(), &e);
    if (d == INVALID_HANDLE_VALUE) {
        ::unlink(dir.c_str());
        return;
    }

    do {
        string name = e.cFileName;
        if (name != "." && name != "..") {
            hls_utest_remove_all(dir + "/" + name);
        }
    } while (::FindNextFileA(d, &e));
    ::FindClose(d);
    ::rmdir(dir.c_str());
#else
    DIR *d = opendir(dir.c_str());
    if (!d) {
        ::unlink(dir.c_str());
        return;
    }

    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        string name = e->d_name;
        if (name != "." && name != "..") {
            hls_utest_remove_all(dir + "/" + name);
        }
    }
    closedir(d);
    ::rmdir(dir.c_str());
#endif
}

string hls_utest_read_file(const string &path)
{
    FILE *fp = fopen(path.c_str(), "rb");
    if (!fp) {
        return "";
    }

    string content;
    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) {
        content.append(buf, n);
    }
    fclose(fp);
    return content;
}

void hls_utest_write_file(const string &path, const string &content)
{
    FILE *fp = fopen(path.c_str(), "wb");
    if (fp) {
        fwrite(content.data(), 1, content.length(), fp);
        fclose(fp);
    }
}

int hls_utest_count(const string &s, const string &sub)
{
    int n = 0;
    for (size_t pos = s.find(sub); pos != string::npos; pos = s.find(sub, pos + sub.length())) {
        n++;
    }
    return n;
}

bool hls_volume_read_only_ = false;
bool hls_volume_full_ = false;
string hls_volume_read_only_at_;
string hls_volume_full_at_;
bool hls_volume_unreadable_ = false;
bool hls_volume_bad_sectors_ = false;

// Creates no file for writing once the volume is read-only. Opening the file the volume fails at for writing makes it
// read-only, so that open fails, or full, so its writes fail.
FILE *hls_utest_fopen(const char *path, const char *mode)
{
    bool writing = (mode[0] == 'w' || mode[0] == 'a');
    if (writing && !hls_volume_read_only_at_.empty() && srs_strings_ends_with(path, hls_volume_read_only_at_)) {
        hls_volume_read_only_ = true;
    }
    if (writing && !hls_volume_full_at_.empty() && srs_strings_ends_with(path, hls_volume_full_at_)) {
        hls_volume_full_ = true;
    }

    if (hls_volume_read_only_ && writing) {
        errno = EROFS;
        return NULL;
    }
    return ::fopen(path, mode);
}

// Fails every write once the disk is full.
size_t hls_utest_fwrite(const void *ptr, size_t size, size_t nitems, FILE *stream)
{
    if (hls_volume_full_) {
        errno = ENOSPC;
        return 0;
    }
    return ::fwrite(ptr, size, nitems, stream);
}

// Creates no dir once the volume is read-only.
int hls_utest_mkdir(const char *path)
{
    if (hls_volume_read_only_) {
        errno = EROFS;
        return -1;
    }
    return srs_mkdir(path);
}

// Opens no file for reading once its permissions deny the server.
int hls_utest_open(const char *path, int oflag, ...)
{
    if (hls_volume_unreadable_) {
        errno = EACCES;
        return -1;
    }
    return srs_file_open(path, oflag);
}

// Reads nothing once the disk has bad sectors.
ssize_t hls_utest_read(int fd, void *buf, size_t count)
{
    if (hls_volume_bad_sectors_) {
        errno = EIO;
        return -1;
    }
    return srs_file_read(fd, buf, count);
}

// A publisher streams H.264 and AAC for 10 seconds to a vhost with HLS segments of 1s and a window of 3s. A key frame
// each second cuts a segment. The playlist lists the last segments that cover the window, the first with the sequence
// number of its file, and the server removes the older segment files, so the disk use stays bounded on a long publish.
// The unpublish drops the segment too short to play, and the dispose leaves nothing.
VOID TEST(AppHlsTest, LongPublishSlidesWindowAndRemovesOldSegments)
{
    srs_error_t err;

    HlsUtestStream s("ai43-hls-long");
    HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1; hls_window 3;"));
    HELPER_ASSERT_SUCCESS(s.sequence_headers());
    HELPER_ASSERT_SUCCESS(s.av(0, 10000));

    string m3u8 = s.m3u8();
    EXPECT_NE(string::npos, m3u8.find("#EXT-X-VERSION:3\n")) << m3u8;
    EXPECT_NE(string::npos, m3u8.find("#EXT-X-MEDIA-SEQUENCE:6\n")) << m3u8;
    EXPECT_NE(string::npos, m3u8.find("#EXT-X-TARGETDURATION:1\n")) << m3u8;
    EXPECT_EQ(4, hls_utest_count(m3u8, "#EXTINF:1.000, no desc\n")) << m3u8;
    for (int i = 6; i < 10; i++) {
        string ts = "livestream-" + srs_strconv_format_int(i) + ".ts";
        EXPECT_NE(string::npos, m3u8.find("\n" + ts + "\n")) << m3u8;
        EXPECT_TRUE(s.exists(ts)) << ts;
    }
    for (int i = 0; i < 5; i++) {
        EXPECT_FALSE(s.exists("livestream-" + srs_strconv_format_int(i) + ".ts")) << i;
    }
    EXPECT_TRUE(s.exists("livestream-10.ts.tmp"));

    s.hls_->on_unpublish();
    EXPECT_FALSE(s.exists("livestream-10.ts.tmp"));
    EXPECT_FALSE(s.exists("livestream-10.ts"));

    s.hls_->dispose();
    EXPECT_FALSE(s.exists("livestream.m3u8"));
    EXPECT_EQ(0, ::rmdir(s.path("").c_str()));
}

// The server restarts while a stream is live, and the publisher publishes again. The playlist the last run wrote is
// recovered: its segments stay listed, the discontinuity before the first one kept, and the new segments follow with
// the next sequence numbers, so players of the old playlist go on.
VOID TEST(AppHlsTest, RepublishAfterServerRestart_LastRunSegmentsRecovered)
{
    srs_error_t err;

    HlsUtestStream s("ai43-hls-restart");
    SrsPath p;
    HELPER_ASSERT_SUCCESS(p.mkdir_all(s.path("")));
    hls_utest_write_file(s.path("livestream.m3u8"), "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-MEDIA-SEQUENCE:7\n#EXT-X-TARGETDURATION:1\n"
                                                    "#EXT-X-DISCONTINUITY\n#EXTINF:1.000, no desc\nlivestream-7.ts\n"
                                                    "#EXTINF:1.000, no desc\nlivestream-8.ts\n");

    HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1;"));
    HELPER_ASSERT_SUCCESS(s.sequence_headers());
    HELPER_ASSERT_SUCCESS(s.av(0, 1000));

    string m3u8 = s.m3u8();
    EXPECT_NE(string::npos, m3u8.find("#EXT-X-MEDIA-SEQUENCE:7\n#EXT-X-TARGETDURATION:1\n#EXT-X-DISCONTINUITY\n"
                                      "#EXTINF:1.000, no desc\nlivestream-7.ts\n#EXTINF:1.000, no desc\nlivestream-8.ts\n"
                                      "#EXT-X-DISCONTINUITY\n#EXTINF:1.000, no desc\nlivestream-9.ts\n"))
        << m3u8;
    EXPECT_TRUE(s.exists("livestream-9.ts"));
}

// The operator moves a stream from an FFmpeg packager (an event playlist with independent segments, so of version 6)
// to SRS, on the same HLS dir, and the packager's last write of the playlist was cut in its last entry. The server
// warns about the version, recovers the whole entries, drops the cut one, and goes on from them.
VOID TEST(AppHlsTest, RepublishOverFfmpegPackagerPlaylist_CutEntryDropped)
{
    srs_error_t err;

    HlsUtestStream s("ai43-hls-ffmpeg");
    SrsPath p;
    HELPER_ASSERT_SUCCESS(p.mkdir_all(s.path("")));
    hls_utest_write_file(s.path("livestream.m3u8"), "#EXTM3U\n#EXT-X-VERSION:6\n#EXT-X-TARGETDURATION:1\n#EXT-X-MEDIA-SEQUENCE:3\n"
                                                    "#EXT-X-PLAYLIST-TYPE:EVENT\n#EXT-X-INDEPENDENT-SEGMENTS\n"
                                                    "#EXTINF:1.000000,\nlivestream-3.ts\n#EXTINF:1.000000,");

    MockLogForSignal log;
    ISrsLog *original = _srs_log;
    _srs_log = &log;
    err = s.publish("hls_fragment 1;");
    _srs_log = original;
    HELPER_ASSERT_SUCCESS(err);

    EXPECT_STREQ("m3u8 3.0 required, actual is #EXT-X-VERSION:6", utest_log_find(log.warns_, "m3u8 3.0").c_str());
    EXPECT_NE(string::npos, utest_log_find(log.warns_, "ts entry unexpected eof").find("inf=#EXTINF:1.000000,"));

    HELPER_ASSERT_SUCCESS(s.sequence_headers());
    HELPER_ASSERT_SUCCESS(s.av(0, 1000));

    string m3u8 = s.m3u8();
    EXPECT_NE(string::npos, m3u8.find("#EXT-X-MEDIA-SEQUENCE:3\n#EXT-X-TARGETDURATION:1\n#EXTINF:1.000, no desc\nlivestream-3.ts\n"
                                      "#EXT-X-DISCONTINUITY\n#EXTINF:1.000, no desc\nlivestream-4.ts\n"))
        << m3u8;
    EXPECT_EQ(2, hls_utest_count(m3u8, "#EXTINF:")) << m3u8;
}

// The publisher drops and publishes again to the same stream, before the server cleans it up. Its segments are in the
// window and in the playlist the server recovers, so each is listed once, and the new segments follow them.
VOID TEST(AppHlsTest, PublisherReconnects_SegmentsListedOnce)
{
    srs_error_t err;

    HlsUtestStream s("ai43-hls-reconnect");
    HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1;"));
    HELPER_ASSERT_SUCCESS(s.sequence_headers());
    HELPER_ASSERT_SUCCESS(s.av(0, 2000));
    s.hls_->on_unpublish();

    HELPER_ASSERT_SUCCESS(s.hls_->on_publish());
    HELPER_ASSERT_SUCCESS(s.sequence_headers());
    HELPER_ASSERT_SUCCESS(s.av(3000, 4000));

    string m3u8 = s.m3u8();
    EXPECT_NE(string::npos, m3u8.find("#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-TARGETDURATION:1\n#EXT-X-DISCONTINUITY\n"
                                      "#EXTINF:1.000, no desc\nlivestream-0.ts\n#EXTINF:1.000, no desc\nlivestream-1.ts\n"
                                      "#EXT-X-DISCONTINUITY\n#EXTINF:1.000, no desc\nlivestream-2.ts\n"))
        << m3u8;
    EXPECT_EQ(3, hls_utest_count(m3u8, "#EXTINF:")) << m3u8;
}

// The server lost power right after it renamed a new playlist over the old one, before the file system wrote its data,
// so an empty playlist is left. When the publisher publishes again, the server warns and starts a new playlist, as with
// no playlist at all, which replaces the empty one.
VOID TEST(AppHlsTest, EmptyPlaylistLeftByPowerLoss_NewPlaylistStarted)
{
    srs_error_t err;

    HlsUtestStream s("ai43-hls-empty");
    SrsPath p;
    HELPER_ASSERT_SUCCESS(p.mkdir_all(s.path("")));
    hls_utest_write_file(s.path("livestream.m3u8"), "");

    MockLogForSignal log;
    ISrsLog *original = _srs_log;
    _srs_log = &log;
    err = s.publish("hls_fragment 1;");
    _srs_log = original;
    HELPER_ASSERT_SUCCESS(err);
    EXPECT_NE(string::npos, utest_log_find(log.warns_, "empty m3u8").find("livestream.m3u8"));

    HELPER_ASSERT_SUCCESS(s.sequence_headers());
    HELPER_ASSERT_SUCCESS(s.av(0, 2000));

    string m3u8 = s.m3u8();
    EXPECT_NE(string::npos, m3u8.find("#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-TARGETDURATION:1\n#EXT-X-DISCONTINUITY\n"
                                      "#EXTINF:1.000, no desc\nlivestream-0.ts\n#EXTINF:1.000, no desc\nlivestream-1.ts\n"))
        << m3u8;
    EXPECT_EQ(2, hls_utest_count(m3u8, "#EXTINF:")) << m3u8;
}

// The operator sets hls_path under a regular file, such as a typo naming another service's file. HLS can not make the
// stream's dir, so the publish fails, for TS and for fMP4, and the file is not touched.
VOID TEST(AppHlsTest, HlsPathUnderRegularFile_PublishRefused)
{
    srs_error_t err;

    const char *options[] = {"", "hls_use_fmp4 on;"};
    for (int i = 0; i < 2; i++) {
        HlsUtestStream s("ai43-hls-under-file");
        hls_utest_write_file(s.home_, "html");

        err = s.publish(options[i]);
        EXPECT_EQ(ERROR_SYSTEM_CREATE_DIR, srs_error_code(err)) << options[i];
        EXPECT_NE(string::npos, srs_error_desc(err).find("hls: update config")) << srs_error_desc(err);
        srs_freep(err);
        EXPECT_STREQ("html", hls_utest_read_file(s.home_).c_str());
    }
}

// The HLS volume was remounted read-only after an I/O error, and a publisher publishes a new TS stream to an app other
// streams publish to, so the app's dir is there. With the segments beside the playlist, the first segment can not be
// created; with each stream's segments in a dir of its own, such as hls_ts_file [app]/[stream]/[seq].ts, that dir can
// not be made. The publish fails either way, leaving nothing.
VOID TEST(AppHlsTest, ReadOnlyVolumeAtPublish_TsPublishRefused)
{
    srs_error_t err;

    const char *options[] = {"", "hls_ts_file [app]/[stream]/[seq].ts;"};
    for (int i = 0; i < 2; i++) {
        HlsUtestVolumeScope volume;
        HlsUtestStream s("ai43-hls-ro-publish");
        SrsPath p;
        HELPER_ASSERT_SUCCESS(p.mkdir_all(s.path("")));
        volume.read_only();

        err = s.publish(options[i]);
        EXPECT_EQ(i == 0 ? ERROR_SYSTEM_FILE_OPENE : ERROR_SYSTEM_CREATE_DIR, srs_error_code(err)) << options[i];
        EXPECT_NE(string::npos, srs_error_desc(err).find("hls: segment open")) << srs_error_desc(err);
        srs_freep(err);
        EXPECT_FALSE(s.exists("livestream-0.ts.tmp"));
        EXPECT_FALSE(s.exists("livestream"));
    }
}

// The HLS volume is remounted read-only while a publisher streams. At the next key frame, the segment is finished, but
// neither the playlist nor the next segment can be created, so the frame fails, and the hub stops HLS for the stream:
// the segments listed before stay listed, and the unpublish warns that it could not finish.
VOID TEST(AppHlsTest, ReadOnlyVolumeMidStream_PlaylistKeepsEarlierSegments)
{
    srs_error_t err;

    HlsUtestVolumeScope volume;
    HlsUtestStream s("ai43-hls-ro-mid");
    HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1;"));
    HELPER_ASSERT_SUCCESS(s.sequence_headers());
    HELPER_ASSERT_SUCCESS(s.av(0, 1900));

    volume.read_only();
    err = s.video(2000, flv_utest_avc_idr, sizeof(flv_utest_avc_idr));
    EXPECT_EQ(ERROR_SYSTEM_FILE_OPENE, srs_error_code(err));
    EXPECT_NE(string::npos, srs_error_desc(err).find("hls: reap segment")) << srs_error_desc(err);
    srs_freep(err);

    MockLogForSignal log;
    ISrsLog *original = _srs_log;
    _srs_log = &log;
    s.hls_->on_unpublish();
    _srs_log = original;
    EXPECT_NE(string::npos, utest_log_find(log.warns_, "hls: ignore unpublish failed").find("segment close"));

    string m3u8 = s.m3u8();
    EXPECT_NE(string::npos, m3u8.find("\nlivestream-0.ts\n")) << m3u8;
    EXPECT_EQ(1, hls_utest_count(m3u8, "#EXTINF:")) << m3u8;
    EXPECT_TRUE(s.exists("livestream-1.ts"));
}

// The disk fills while a publisher streams, right before a key frame. The segment is finished, but the playlist can
// not be written, so the frame fails with the playlist's write error and the hub stops HLS for the stream; no playlist
// lists a segment, and the unpublish leaves no temporary file.
VOID TEST(AppHlsTest, DiskFullAtKeyFrame_PlaylistNotWritten)
{
    srs_error_t err;

    HlsUtestVolumeScope volume;
    HlsUtestStream s("ai43-hls-full-key");
    HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1;"));
    HELPER_ASSERT_SUCCESS(s.sequence_headers());
    HELPER_ASSERT_SUCCESS(s.av(0, 900));

    volume.fill();
    err = s.video(1000, flv_utest_avc_idr, sizeof(flv_utest_avc_idr));
    EXPECT_EQ(ERROR_SYSTEM_FILE_WRITE, srs_error_code(err));
    EXPECT_NE(string::npos, srs_error_desc(err).find("hls: reap segment")) << srs_error_desc(err);
    EXPECT_NE(string::npos, srs_error_desc(err).find("hls: refresh m3u8")) << srs_error_desc(err);
    srs_freep(err);

    s.hls_->on_unpublish();
    EXPECT_FALSE(s.exists("livestream.m3u8"));
    EXPECT_FALSE(s.exists("livestream.m3u8.temp"));
    EXPECT_FALSE(s.exists("livestream-1.ts.tmp"));
}

// The disk fills while a publisher streams, inside a segment. The next audio or video frame can not be written, so it
// fails and the hub stops HLS for the stream. For a radio station that publishes AAC only, the frame that cuts a
// segment fails too, as the audio that starts the next segment can not be written.
VOID TEST(AppHlsTest, DiskFullInsideSegment_NextFrameFails)
{
    srs_error_t err;

    for (int i = 0; i < 2; i++) {
        HlsUtestVolumeScope volume;
        HlsUtestStream s("ai43-hls-full-mid");
        HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1;"));
        HELPER_ASSERT_SUCCESS(s.sequence_headers());
        HELPER_ASSERT_SUCCESS(s.av(0, 400));

        volume.fill();
        if (i == 0) {
            err = s.audio(500, flv_utest_aac_frame, sizeof(flv_utest_aac_frame));
            EXPECT_NE(string::npos, srs_error_desc(err).find("hls: flush audio")) << srs_error_desc(err);
        } else {
            err = s.video(500, flv_utest_avc_inter, sizeof(flv_utest_avc_inter));
            EXPECT_NE(string::npos, srs_error_desc(err).find("hls: flush video")) << srs_error_desc(err);
        }
        EXPECT_EQ(ERROR_SYSTEM_FILE_WRITE, srs_error_code(err)) << i;
        srs_freep(err);
    }

    if (true) {
        HlsUtestVolumeScope volume;
        HlsUtestStream s("ai43-hls-full-radio");
        HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1;"));
        HELPER_ASSERT_SUCCESS(s.audio(0, flv_utest_aac_sh, sizeof(flv_utest_aac_sh)));
        // Pure audio is written in runs of at least 0.72s: the runs start at 0, 759 and 1518ms, and the frame at
        // 2277ms starts the run past 2.1s, which cuts the segment.
        for (int ts = 0; ts <= 2254; ts += 23) {
            HELPER_ASSERT_SUCCESS(s.audio(ts, flv_utest_aac_frame, sizeof(flv_utest_aac_frame)));
        }

        volume.fill();
        err = s.audio(2277, flv_utest_aac_frame, sizeof(flv_utest_aac_frame));
        EXPECT_EQ(ERROR_SYSTEM_FILE_WRITE, srs_error_code(err));
        EXPECT_NE(string::npos, srs_error_desc(err).find("hls: reap segment")) << srs_error_desc(err);
        srs_freep(err);
    }
}

#ifndef _WIN32 // Windows deletes no open file.
// A cleanup job of the operator deletes the stream's HLS dir while the publisher streams. The segment being written is
// gone with its dir, so the key frame that ends it can not finish it: the frame fails and the hub stops HLS for the
// stream, rather than list a segment players can not get. For TS and for fMP4.
VOID TEST(AppHlsTest, CleanupJobDeletesStreamDir_SegmentNotListed)
{
    srs_error_t err;

    const char *options[] = {"hls_fragment 1;", "hls_fragment 1; hls_use_fmp4 on;"};
    for (int i = 0; i < 2; i++) {
        HlsUtestStream s("ai43-hls-deleted");
        HELPER_ASSERT_SUCCESS(s.publish(options[i]));
        HELPER_ASSERT_SUCCESS(s.sequence_headers());
        HELPER_ASSERT_SUCCESS(s.av(0, 1900));

        hls_utest_remove_all(s.home_);
        err = s.video(2000, flv_utest_avc_idr, sizeof(flv_utest_avc_idr));
        EXPECT_EQ(ERROR_SYSTEM_FRAGMENT_RENAME, srs_error_code(err)) << options[i];
        srs_freep(err);
        EXPECT_FALSE(s.exists("livestream.m3u8")) << options[i];
    }
}
#endif

// The operator names the playlist as the dir of the segments, such as hls_m3u8_file [app]/[stream] with hls_ts_file
// [app]/[stream]/[seq].ts. The playlist can not be renamed over the dir, so the key frame that ends the first segment
// fails with the rename error and the hub stops HLS for the stream, rather than go on with no playlist; the temporary
// playlist is removed. For TS and for fMP4.
VOID TEST(AppHlsTest, PlaylistNamedAsSegmentDir_RenameErrorReturned)
{
    srs_error_t err;

    const char *options[] = {"hls_fragment 1; hls_m3u8_file [app]/[stream]; hls_ts_file [app]/[stream]/[seq].ts;",
                             "hls_fragment 1; hls_use_fmp4 on; hls_m3u8_file [app]/[stream]; hls_fmp4_file [app]/[stream]/[seq].m4s;"};
    for (int i = 0; i < 2; i++) {
        HlsUtestStream s("ai43-hls-m3u8-dir");
        HELPER_ASSERT_SUCCESS(s.publish(options[i]));
        HELPER_ASSERT_SUCCESS(s.sequence_headers());

        err = s.av(0, 2000);
        EXPECT_EQ(ERROR_HLS_WRITE_FAILED, srs_error_code(err)) << options[i];
        EXPECT_NE(string::npos, srs_error_desc(err).find("rename m3u8 file failed")) << srs_error_desc(err);
        srs_freep(err);
        EXPECT_FALSE(s.exists("livestream.temp")) << options[i];
    }
}

// A publisher streams H.264 and AAC for 10 seconds to a vhost with fMP4 HLS segments of 1s and a window of 3s. The
// playlist maps the init file the sequence headers wrote, and lists the last segments, each in the HLS dir; the older
// segment files are removed, and the dispose leaves nothing.
VOID TEST(AppHlsTest, Fmp4LongPublishSlidesWindowAndRemovesOldSegments)
{
    srs_error_t err;

    HlsUtestStream s("ai43-hls-fmp4-long");
    HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1; hls_window 3; hls_use_fmp4 on;"));
    HELPER_ASSERT_SUCCESS(s.sequence_headers());
    EXPECT_TRUE(s.exists("livestream/init.mp4"));
    HELPER_ASSERT_SUCCESS(s.av(0, 10000));

    string m3u8 = s.m3u8();
    EXPECT_NE(string::npos, m3u8.find("#EXT-X-VERSION:7\n")) << m3u8;
    EXPECT_NE(string::npos, m3u8.find("#EXT-X-MAP:URI=\"livestream/init.mp4\"\n")) << m3u8;
    int listed = 0;
    for (int i = 0; i < 10; i++) {
        string m4s = "livestream-" + srs_strconv_format_int(i) + ".m4s";
        if (m3u8.find("\n" + m4s + "\n") != string::npos) {
            listed++;
            EXPECT_TRUE(s.exists(m4s)) << m4s;
        }
    }
    EXPECT_GE(listed, 2) << m3u8;
    EXPECT_FALSE(s.exists("livestream-0.m4s"));

    s.hls_->on_unpublish();
    s.hls_->dispose();
    EXPECT_FALSE(s.exists("livestream.m3u8"));
    EXPECT_FALSE(s.exists("livestream/init.mp4"));
}

// A publisher streams to a vhost with fMP4 HLS segments of 1s, with a key frame each second, as it does with TS.
// The segment's duration counts the key frame before the cut is checked, so each key frame cuts a segment of 1s, as
// TS does: the playlist lists the last four 1s segments, and the target duration is 1s.
VOID TEST(AppHlsTest, Fmp4SegmentOfOneGop_CutAtEachKeyFrame)
{
    srs_error_t err;

    HlsUtestStream s("ai43-hls-fmp4-gop");
    HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1; hls_window 3; hls_use_fmp4 on;"));
    HELPER_ASSERT_SUCCESS(s.sequence_headers());
    HELPER_ASSERT_SUCCESS(s.av(0, 10000));

    string m3u8 = s.m3u8();
    EXPECT_NE(string::npos, m3u8.find("#EXT-X-MEDIA-SEQUENCE:6\n")) << m3u8;
    EXPECT_NE(string::npos, m3u8.find("#EXT-X-TARGETDURATION:1\n")) << m3u8;
    EXPECT_EQ(4, hls_utest_count(m3u8, "#EXTINF:1.000, no desc\n")) << m3u8;
    for (int i = 6; i < 10; i++) {
        string m4s = "livestream-" + srs_strconv_format_int(i) + ".m4s";
        EXPECT_NE(string::npos, m3u8.find("\n" + m4s + "\n")) << m3u8;
    }
}

// A radio station publishes AAC only, a frame each 100ms, to a vhost with fMP4 HLS segments of 1s. The frame that
// brings a segment to hls_aof_ratio (2.1) times the fragment cuts it, as TS does, so 8.5s of audio gives four
// segments of 2.1s, cut at 2.1s, 4.2s, 6.3s and 8.4s, not a frame later each time.
VOID TEST(AppHlsTest, Fmp4AudioOnlyStream_CutAtFrameReachingDuration)
{
    srs_error_t err;

    HlsUtestStream s("ai43-hls-fmp4-audio-cut");
    HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1; hls_use_fmp4 on;"));
    HELPER_ASSERT_SUCCESS(s.audio(0, flv_utest_aac_sh, sizeof(flv_utest_aac_sh)));
    for (int ts = 0; ts <= 8500; ts += 100) {
        HELPER_ASSERT_SUCCESS(s.audio(ts, flv_utest_aac_frame, sizeof(flv_utest_aac_frame)));
    }

    string m3u8 = s.m3u8();
    EXPECT_EQ(4, hls_utest_count(m3u8, "#EXTINF:2.100, no desc\n")) << m3u8;
    EXPECT_EQ(4, hls_utest_count(m3u8, "#EXTINF:")) << m3u8;
    EXPECT_NE(string::npos, m3u8.find("\nlivestream-3.m4s\n")) << m3u8;
}

// The EXTINF durations of a playlist, in ms.
vector<int> hls_utest_extinf_ms(const string &m3u8)
{
    vector<int> durations;
    for (size_t pos = m3u8.find("#EXTINF:"); pos != string::npos; pos = m3u8.find("#EXTINF:", pos + 1)) {
        durations.push_back((int)(::atof(m3u8.c_str() + pos + 8) * 1000 + 0.5));
    }
    return durations;
}

// A radio station publishes AAC only, a frame each 23ms, to a vhost with fMP4 HLS segments of 1s. With no video to
// cut at key frames, a segment is cut once it passes hls_aof_ratio (2.1) times the fragment, and the init file holds
// the audio track only. An empty AAC sequence header the encoder sends before its audio is ready writes no init file.
VOID TEST(AppHlsTest, Fmp4AudioOnlyStream_SegmentsCutByDuration)
{
    srs_error_t err;

    HlsUtestStream s("ai43-hls-fmp4-audio");
    HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1; hls_use_fmp4 on;"));

    const uint8_t empty_sh[] = {0xaf, 0x00};
    HELPER_ASSERT_SUCCESS(s.audio(0, empty_sh, sizeof(empty_sh)));
    EXPECT_FALSE(s.exists("livestream/init.mp4"));

    HELPER_ASSERT_SUCCESS(s.audio(0, flv_utest_aac_sh, sizeof(flv_utest_aac_sh)));
    EXPECT_TRUE(s.exists("livestream/init.mp4"));
    for (int ts = 0; ts <= 7000; ts += 23) {
        HELPER_ASSERT_SUCCESS(s.audio(ts, flv_utest_aac_frame, sizeof(flv_utest_aac_frame)));
    }

    string m3u8 = s.m3u8();
    vector<int> durations = hls_utest_extinf_ms(m3u8);
    ASSERT_GE((int)durations.size(), 2) << m3u8;
    for (int i = 0; i < (int)durations.size(); i++) {
        EXPECT_GE(durations[i], 2100) << m3u8;
        EXPECT_LT(durations[i], 2100 + 23) << m3u8;
    }
    EXPECT_TRUE(s.exists("livestream-0.m4s"));
    EXPECT_TRUE(s.exists("livestream-1.m4s"));
}

// The fMP4 HLS volume fails while a publisher sends its sequence headers: it is remounted read-only, or the disk fills.
// The init file of the video sequence header, of both tracks when the audio one follows, or of the audio track of an
// audio-only stream, can not be written, so the sequence header fails and the hub stops HLS for the stream; no init
// file is left for players. The stream was published before, so its dir is there.
VOID TEST(AppHlsTest, Fmp4VolumeFailsAtSequenceHeader_InitFileNotWritten)
{
    srs_error_t err;

    for (int full = 0; full < 2; full++) {
        for (int track = 0; track < 3; track++) {
            HlsUtestVolumeScope volume;
            HlsUtestStream s("ai43-hls-fmp4-init");
            SrsPath p;
            HELPER_ASSERT_SUCCESS(p.mkdir_all(s.path("livestream")));
            HELPER_ASSERT_SUCCESS(s.publish("hls_use_fmp4 on;"));

            // The video sequence header, then the audio one, or the audio one only.
            if (track == 1) {
                HELPER_ASSERT_SUCCESS(s.video(0, flv_utest_avc_sh, sizeof(flv_utest_avc_sh)));
                ::unlink(s.path("livestream/init.mp4").c_str());
            }
            if (full) {
                volume.fill();
            } else {
                volume.read_only();
            }
            if (track == 0) {
                err = s.video(0, flv_utest_avc_sh, sizeof(flv_utest_avc_sh));
            } else {
                err = s.audio(0, flv_utest_aac_sh, sizeof(flv_utest_aac_sh));
            }

            EXPECT_EQ(full ? ERROR_SYSTEM_FILE_WRITE : ERROR_SYSTEM_FILE_OPENE, srs_error_code(err)) << full << track;
            EXPECT_NE(string::npos, srs_error_desc(err).find("write init mp4")) << srs_error_desc(err);
            srs_freep(err);
            EXPECT_FALSE(s.exists("livestream/init.mp4")) << full << track;
        }
    }
}

// The fMP4 HLS volume is remounted read-only after the publisher's sequence headers. Its first frame, video or audio,
// can not open a segment, so the frame fails and the hub stops HLS for the stream, with no segment left.
VOID TEST(AppHlsTest, Fmp4ReadOnlyVolumeAtFirstFrame_SegmentNotOpened)
{
    srs_error_t err;

    for (int i = 0; i < 2; i++) {
        HlsUtestVolumeScope volume;
        HlsUtestStream s("ai43-hls-fmp4-ro-frame");
        HELPER_ASSERT_SUCCESS(s.publish("hls_use_fmp4 on;"));
        HELPER_ASSERT_SUCCESS(s.sequence_headers());

        volume.read_only();
        if (i == 0) {
            err = s.video(0, flv_utest_avc_idr, sizeof(flv_utest_avc_idr));
        } else {
            err = s.audio(0, flv_utest_aac_frame, sizeof(flv_utest_aac_frame));
        }
        EXPECT_EQ(ERROR_SYSTEM_FILE_OPENE, srs_error_code(err)) << i;
        EXPECT_NE(string::npos, srs_error_desc(err).find("open hls m4s segment tmp file")) << srs_error_desc(err);
        srs_freep(err);
        EXPECT_FALSE(s.exists("livestream-0.m4s.tmp"));
    }
}

// The disk fills while a publisher streams fMP4 HLS, right before the key frame that ends a segment. The segment can
// not be written, so the frame fails and the hub stops HLS for the stream: the segment is not listed, and the
// unpublish, which can not write the segment either, warns.
VOID TEST(AppHlsTest, Fmp4DiskFullAtKeyFrame_SegmentNotListed)
{
    srs_error_t err;

    HlsUtestVolumeScope volume;
    HlsUtestStream s("ai43-hls-fmp4-full");
    HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1; hls_use_fmp4 on;"));
    HELPER_ASSERT_SUCCESS(s.sequence_headers());
    HELPER_ASSERT_SUCCESS(s.av(0, 900));

    volume.fill();
    err = s.video(1000, flv_utest_avc_idr, sizeof(flv_utest_avc_idr));
    EXPECT_EQ(ERROR_SYSTEM_FILE_WRITE, srs_error_code(err));
    EXPECT_NE(string::npos, srs_error_desc(err).find("m4s flush encoder")) << srs_error_desc(err);
    srs_freep(err);
    EXPECT_FALSE(s.exists("livestream.m3u8"));
    EXPECT_FALSE(s.exists("livestream-0.m4s"));

    MockLogForSignal log;
    ISrsLog *original = _srs_log;
    _srs_log = &log;
    s.hls_->on_unpublish();
    _srs_log = original;
    EXPECT_NE(string::npos, utest_log_find(log.warns_, "hls: ignore unpublish failed").find("m4s flush encoder"));
}

// The fMP4 HLS volume is remounted read-only while a publisher streams. The key frame that ends a segment finishes it,
// but the playlist can not be created, so the frame fails and the hub stops HLS for the stream; the playlist players
// have still lists the earlier segments.
VOID TEST(AppHlsTest, Fmp4ReadOnlyVolumeAtKeyFrame_PlaylistKeepsEarlierSegments)
{
    srs_error_t err;

    HlsUtestVolumeScope volume;
    HlsUtestStream s("ai43-hls-fmp4-ro-key");
    HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1; hls_use_fmp4 on;"));
    HELPER_ASSERT_SUCCESS(s.sequence_headers());
    HELPER_ASSERT_SUCCESS(s.av(0, 1900));
    string m3u8 = s.m3u8();
    ASSERT_EQ(1, hls_utest_count(m3u8, "#EXTINF:")) << m3u8;

    volume.read_only();
    err = s.video(2000, flv_utest_avc_idr, sizeof(flv_utest_avc_idr));
    EXPECT_EQ(ERROR_SYSTEM_FILE_OPENE, srs_error_code(err));
    EXPECT_NE(string::npos, srs_error_desc(err).find("refresh m3u8")) << srs_error_desc(err);
    srs_freep(err);
    EXPECT_STREQ(m3u8.c_str(), s.m3u8().c_str());
}

// An idle fMP4 publisher is disposed after a cleanup job of the operator deleted the stream's HLS dir: the segment
// being written is gone, so its temporary file can not be removed; the dispose warns and goes on.
VOID TEST(AppHlsTest, Fmp4IdlePublisherDisposedAfterCleanupJob_WarnsTmpFileGone)
{
    srs_error_t err;

    HlsUtestStream s("ai43-hls-fmp4-idle");
    HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1; hls_use_fmp4 on;"));
    HELPER_ASSERT_SUCCESS(s.sequence_headers());
    HELPER_ASSERT_SUCCESS(s.av(0, 500));
    hls_utest_remove_all(s.home_);

    MockLogForSignal log;
    ISrsLog *original = _srs_log;
    _srs_log = &log;
    s.hls_->dispose();
    _srs_log = original;
    EXPECT_NE(string::npos, utest_log_find(log.warns_, "Unlink tmp ts failed").find("livestream-0.m4s.tmp"));
}

// The fMP4 HLS volume is remounted read-only before an idle publisher is disposed. The playlist and the init file can
// not be removed: the dispose warns for each and still tries the init file after the playlist fails.
VOID TEST(AppHlsTest, Fmp4DisposeOnReadOnlyVolume_WarnsAndTriesEachFile)
{
    srs_error_t err;

    HlsUtestReadOnlyFactory factory;
    HlsUtestStream s("ai43-hls-fmp4-ro-dispose");
    s.factory_ = &factory;
    HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1; hls_use_fmp4 on;"));
    HELPER_ASSERT_SUCCESS(s.sequence_headers());
    HELPER_ASSERT_SUCCESS(s.av(0, 2000));
    s.hls_->on_unpublish();
    ASSERT_TRUE(s.exists("livestream.m3u8"));

    MockLogForSignal log;
    ISrsLog *original = _srs_log;
    _srs_log = &log;
    s.hls_->dispose();
    _srs_log = original;

    EXPECT_STRNE("", utest_log_find(log.warns_, "dispose: ignore remove m3u8 failed").c_str());
    EXPECT_STRNE("", utest_log_find(log.warns_, "dispose: ignore remove init mp4 failed").c_str());
    EXPECT_TRUE(s.exists("livestream.m3u8"));
    EXPECT_TRUE(s.exists("livestream/init.mp4"));
    ASSERT_EQ(2, (int)factory.unlinks_.size());
    EXPECT_STREQ(s.path("livestream.m3u8").c_str(), factory.unlinks_[0].c_str());
    EXPECT_STREQ(s.path("livestream/init.mp4").c_str(), factory.unlinks_[1].c_str());
}

// The keys are served by a key server that needs a token, so hls_key_url holds a query. The playlist's key URI puts
// the key file's path before the query, so the key server gets both.
VOID TEST(AppHlsTest, KeyUrlWithToken_KeyUriKeepsQuery)
{
    srs_error_t err;

    HlsUtestStream s("ai43-hls-key-token");
    HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1; hls_keys on; hls_key_url https://keys.example.com/?token=abc;"));
    HELPER_ASSERT_SUCCESS(s.sequence_headers());
    HELPER_ASSERT_SUCCESS(s.av(0, 1000));

    string m3u8 = s.m3u8();
    EXPECT_NE(string::npos, m3u8.find("#EXT-X-KEY:METHOD=AES-128,URI=\"https://keys.example.com/live/livestream-0.key?token=abc\",IV=0x"))
        << m3u8;
    EXPECT_TRUE(s.exists("livestream-0.key"));
}

// The segments are served by a CDN, so hls_entry_prefix names it, with no slash at its end. Each segment's URI is
// under the prefix and the playlist's dir, as the CDN maps the HLS dir.
VOID TEST(AppHlsTest, EntryPrefixWithoutSlash_SegmentUriUnderPrefixAndAppDir)
{
    srs_error_t err;

    HlsUtestStream s("ai43-hls-prefix");
    HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1; hls_entry_prefix http://cdn.example.com;"));
    HELPER_ASSERT_SUCCESS(s.sequence_headers());
    HELPER_ASSERT_SUCCESS(s.av(0, 1000));

    string m3u8 = s.m3u8();
    EXPECT_NE(string::npos, m3u8.find("\nhttp://cdn.example.com/live/livestream-0.ts\n")) << m3u8;
    EXPECT_TRUE(s.exists("livestream-0.ts"));
}

// With hls_ts_floor on, the segments are named by the floor of the time over the fragment, one more each segment. A
// publisher pushes a file faster than real time, so the names run ahead of the clock, and each segment is made longer
// by 30% of a fragment per fragment ahead. Once the names are more than 20 fragments ahead, the server warns and names
// the next segment by the clock again. The mux log shows how far ahead the names are. For TS and for fMP4.
VOID TEST(AppHlsTest, TsFloorPublisherFasterThanRealTime_NamesJumpBackToClock)
{
    srs_error_t err;

    const char *options[] = {"hls_ts_file [app]/[stream]-[timestamp].ts;",
                             "hls_use_fmp4 on; hls_fmp4_file [app]/[stream]-[timestamp].m4s;"};
    for (int i = 0; i < 2; i++) {
        PithyPrintAtOnceScope print(SrsPithyPrint::create_hls);
        HlsUtestStream s("ai43-hls-floor");
        HELPER_ASSERT_SUCCESS(s.publish(string("hls_fragment 1; hls_window 3; hls_ts_floor on; ") + options[i]));
        HELPER_ASSERT_SUCCESS(s.sequence_headers());

        MockLogForSignal log;
        ISrsLog *original = _srs_log;
        _srs_log = &log;
        err = s.av(0, 200000);
        _srs_log = original;
        HELPER_ASSERT_SUCCESS(err);

        EXPECT_STRNE("", utest_log_find(log.warns_, "hls: jmp for ts deviation").c_str()) << options[i];
        EXPECT_STRNE("", utest_log_find(log.traces_, ", dva=20p").c_str()) << options[i];

        string m3u8 = s.m3u8();
        size_t pos = m3u8.find("\nlivestream-");
        ASSERT_NE(string::npos, pos) << m3u8;
        string name = m3u8.substr(pos + 1, m3u8.find("\n", pos + 1) - pos - 1);
        EXPECT_TRUE(s.exists(name)) << name;
    }
}

// A radio station publishes AAC only, with hls_dts_directly off, so the server times the audio by its samples, not by
// the FLV timestamps rounded to ms. Its frames of 2048 samples at 44.1kHz come 46 or 47ms apart; each is 46.44ms of
// samples. Pure audio is written in runs of at least 0.72s, and the segment is cut when a run starts past
// hls_aof_ratio (2.1) times the fragment: the run at frame 51, at 51 x 2048 / 44100 = 2.368s, where the FLV timestamps
// would give 2.371s.
VOID TEST(AppHlsTest, PureAudioWithoutDtsDirectly_TimedBySamples)
{
    srs_error_t err;

    HlsUtestStream s("ai43-hls-audio");
    HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1; hls_dts_directly off;"));
    HELPER_ASSERT_SUCCESS(s.audio(0, flv_utest_aac_sh, sizeof(flv_utest_aac_sh)));
    int64_t ts = 0;
    for (int i = 0; i < 100; i++) {
        HELPER_ASSERT_SUCCESS(s.audio(ts, flv_utest_aac_frame, sizeof(flv_utest_aac_frame)));
        ts += (i % 2) ? 47 : 46;
    }

    string m3u8 = s.m3u8();
    vector<int> durations = hls_utest_extinf_ms(m3u8);
    ASSERT_GE((int)durations.size(), 1) << m3u8;
    EXPECT_EQ(2368, durations[0]) << m3u8;
    EXPECT_TRUE(s.exists("livestream-0.ts"));
}

// The operator changes the HLS fragment and reloads while a publisher streams. The next frame, audio or video, restarts
// HLS with the new config and asks the hub for the sequence headers again, so the next segments are cut by it. When the
// new config names an hls_path under a regular file, the restart fails, and so does the frame.
VOID TEST(AppHlsTest, ReloadWhileStreaming_NextFrameRestartsWithNewConfig)
{
    srs_error_t err;

    HlsUtestStream s("ai43-hls-reload");
    HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1;"));
    HELPER_ASSERT_SUCCESS(s.sequence_headers());
    HELPER_ASSERT_SUCCESS(s.av(0, 1000));
    EXPECT_NE(string::npos, s.m3u8().find("#EXT-X-TARGETDURATION:1\n"));

    MockSrsConfig conf2;
    HELPER_ASSERT_SUCCESS(s.load(conf2, "hls_fragment 2;"));
    s.use(&conf2);
    s.hls_->async_reload();
    HELPER_ASSERT_SUCCESS(s.audio(1100, flv_utest_aac_frame, sizeof(flv_utest_aac_frame)));
    EXPECT_EQ(1, s.on_hls_request_sh_count_);
    HELPER_ASSERT_SUCCESS(s.av(2000, 6000));
    EXPECT_NE(string::npos, s.m3u8().find("#EXT-X-TARGETDURATION:2\n")) << s.m3u8();

    MockSrsConfig conf3;
    HELPER_ASSERT_SUCCESS(s.load(conf3, "hls_fragment 1;"));
    s.use(&conf3);
    s.hls_->async_reload();
    HELPER_ASSERT_SUCCESS(s.video(6100, flv_utest_avc_inter, sizeof(flv_utest_avc_inter)));
    EXPECT_EQ(2, s.on_hls_request_sh_count_);

    string file = _srs_tmp_file_prefix + "ai43-hls-reload-file";
    MockFileRemover remover(file);
    hls_utest_write_file(file, "html");
    MockSrsConfig conf4;
    HELPER_ASSERT_SUCCESS(conf4.mock_parse(string(_MIN_OK_CONF) + "vhost __defaultVhost__ { hls { enabled on; hls_path " + file + "/hls; } }"));
    s.use(&conf4);
    s.hls_->async_reload();
    err = s.video(6200, flv_utest_avc_inter, sizeof(flv_utest_avc_inter));
    EXPECT_EQ(ERROR_SYSTEM_CREATE_DIR, srs_error_code(err));
    EXPECT_NE(string::npos, srs_error_desc(err).find("hls publish failed")) << srs_error_desc(err);
    srs_freep(err);
    EXPECT_EQ(2, s.on_hls_request_sh_count_);
}

// The operator reloads while a publisher streams fMP4 HLS, and the disk has filled. The next frame restarts HLS, but
// the init file of the sequence headers the hub feeds again can not be written, so the frame fails.
VOID TEST(AppHlsTest, Fmp4ReloadOnFullDisk_SequenceHeadersFail)
{
    srs_error_t err;

    HlsUtestVolumeScope volume;
    HlsUtestStream s("ai43-hls-fmp4-reload");
    HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1; hls_use_fmp4 on;"));
    HELPER_ASSERT_SUCCESS(s.sequence_headers());
    HELPER_ASSERT_SUCCESS(s.av(0, 500));

    volume.fill();
    s.hls_->async_reload();
    err = s.audio(600, flv_utest_aac_frame, sizeof(flv_utest_aac_frame));
    EXPECT_EQ(ERROR_SYSTEM_FILE_WRITE, srs_error_code(err));
    EXPECT_NE(string::npos, srs_error_desc(err).find("hls request sh")) << srs_error_desc(err);
    srs_freep(err);
    EXPECT_EQ(1, s.on_hls_request_sh_count_);
}

// An idle publisher keeps its HLS files when the dispose is off (hls_dispose 0), or while a reload is pending; past
// hls_dispose, the cycle disposes its HLS once, not again at the next timeouts. A vhost with HLS off gets no HLS.
VOID TEST(AppHlsTest, IdlePublisherDispose_OnlyWhenOnAndNothingPending)
{
    srs_error_t err;

    if (true) {
        HlsUtestStream s("ai43-hls-dispose-off");
        HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1; hls_dispose 0;"));
        HELPER_ASSERT_SUCCESS(s.sequence_headers());
        HELPER_ASSERT_SUCCESS(s.av(0, 1000));

        s.hls_->last_update_time_ = 1;
        HELPER_EXPECT_SUCCESS(s.hls_->cycle());
        EXPECT_TRUE(s.exists("livestream.m3u8"));
        EXPECT_TRUE(s.hls_->enabled_);
    }

    if (true) {
        HlsUtestStream s("ai43-hls-dispose-reload");
        HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1; hls_dispose 1;"));
        HELPER_ASSERT_SUCCESS(s.sequence_headers());
        HELPER_ASSERT_SUCCESS(s.av(0, 1000));

        s.hls_->async_reload();
        s.hls_->last_update_time_ = 1;
        HELPER_EXPECT_SUCCESS(s.hls_->cycle());
        EXPECT_TRUE(s.exists("livestream.m3u8"));
        EXPECT_TRUE(s.hls_->enabled_);
    }

    if (true) {
        HlsUtestStream s("ai43-hls-dispose-once");
        HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1; hls_dispose 1;"));
        HELPER_ASSERT_SUCCESS(s.sequence_headers());
        HELPER_ASSERT_SUCCESS(s.av(0, 1000));

        s.hls_->last_update_time_ = 1;
        HELPER_EXPECT_SUCCESS(s.hls_->cycle());
        EXPECT_FALSE(s.exists("livestream.m3u8"));
        EXPECT_FALSE(s.hls_->enabled_);
        EXPECT_TRUE(s.hls_->suspended_);

        // The next timeout disposes nothing: a playlist written to the dir since, such as by another server on a shared
        // volume, stays.
        hls_utest_write_file(s.path("livestream.m3u8"), "#EXTM3U\n");
        s.hls_->last_update_time_ = 1;
        HELPER_EXPECT_SUCCESS(s.hls_->cycle());
        EXPECT_TRUE(s.exists("livestream.m3u8"));
    }

    if (true) {
        HlsUtestStream s("ai43-hls-off");
        HELPER_ASSERT_SUCCESS(s.publish("enabled off;"));
        HELPER_ASSERT_SUCCESS(s.sequence_headers());
        HELPER_ASSERT_SUCCESS(s.av(0, 1000));
        EXPECT_FALSE(s.hls_->enabled_);
        EXPECT_FALSE(s.exists("livestream.m3u8"));
    }
}

// The process is out of memory when a publisher publishes, so no coroutine can be created for the HLS callbacks. The
// publish fails with the ST error, for TS and for fMP4.
VOID TEST(AppHlsTest, PublishWhenNoCoroutineCanBeCreated_Refused)
{
    srs_error_t err;

    const char *options[] = {"", "hls_use_fmp4 on;"};
    for (int i = 0; i < 2; i++) {
        HlsUtestStream s("ai43-hls-no-coroutine");

        _ST_THREAD_CREATE_PFN ov = _pfn_st_thread_create;
        _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)forward_st_thread_create_out_of_memory;
        err = s.publish(options[i]);
        _pfn_st_thread_create = ov;

        EXPECT_EQ(ERROR_ST_CREATE_CYCLE_THREAD, srs_error_code(err)) << options[i];
        EXPECT_NE(string::npos, srs_error_desc(err).find("muxer publish")) << srs_error_desc(err);
        srs_freep(err);
        EXPECT_FALSE(s.hls_->enabled_);
    }
}

// An idle publisher's HLS was disposed, and when it sends media again, the volume has been remounted read-only or the
// disk has filled. HLS can not resume: for TS the first segment can not be opened, for fMP4 the init file of the
// sequence headers the hub feeds again can not be written. The media fails, and the hub stops HLS for the stream.
VOID TEST(AppHlsTest, IdlePublisherResumesOnFailedVolume_ResumeFails)
{
    srs_error_t err;

    if (true) {
        HlsUtestVolumeScope volume;
        HlsUtestStream s("ai43-hls-resume-ts");
        HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1; hls_dispose 1;"));
        HELPER_ASSERT_SUCCESS(s.sequence_headers());
        HELPER_ASSERT_SUCCESS(s.av(0, 1000));
        s.hls_->last_update_time_ = 1;
        HELPER_ASSERT_SUCCESS(s.hls_->cycle());

        volume.read_only();
        err = s.audio(1100, flv_utest_aac_frame, sizeof(flv_utest_aac_frame));
        EXPECT_EQ(ERROR_SYSTEM_FILE_OPENE, srs_error_code(err));
        EXPECT_NE(string::npos, srs_error_desc(err).find("hls: resume")) << srs_error_desc(err);
        srs_freep(err);
        EXPECT_EQ(0, s.on_hls_request_sh_count_);
    }

    if (true) {
        HlsUtestVolumeScope volume;
        HlsUtestStream s("ai43-hls-resume-fmp4");
        HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1; hls_dispose 1; hls_use_fmp4 on;"));
        HELPER_ASSERT_SUCCESS(s.sequence_headers());
        HELPER_ASSERT_SUCCESS(s.av(0, 1000));
        s.hls_->last_update_time_ = 1;
        HELPER_ASSERT_SUCCESS(s.hls_->cycle());
        EXPECT_FALSE(s.exists("livestream/init.mp4"));

        volume.fill();
        err = s.video(1100, flv_utest_avc_inter, sizeof(flv_utest_avc_inter));
        EXPECT_EQ(ERROR_SYSTEM_FILE_WRITE, srs_error_code(err));
        EXPECT_NE(string::npos, srs_error_desc(err).find("hls request sh")) << srs_error_desc(err);
        srs_freep(err);
        EXPECT_EQ(1, s.on_hls_request_sh_count_);
        EXPECT_FALSE(s.exists("livestream/init.mp4"));
    }
}

// A publisher sends a video info frame, such as the start of a seek, between its frames. HLS writes nothing of it.
VOID TEST(AppHlsTest, VideoInfoFrame_NotWrittenToSegment)
{
    srs_error_t err;

    HlsUtestStream s("ai43-hls-info");
    HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1;"));
    HELPER_ASSERT_SUCCESS(s.sequence_headers());
    HELPER_ASSERT_SUCCESS(s.av(0, 500));

    string before = hls_utest_read_file(s.path("livestream-0.ts.tmp"));
    const uint8_t info[] = {0x57, 0x00};
    HELPER_ASSERT_SUCCESS(s.video(550, info, sizeof(info)));
    EXPECT_EQ(before.length(), hls_utest_read_file(s.path("livestream-0.ts.tmp")).length());
}

// The disk is full when a publisher publishes with HLS encryption on. The key of the first segment can not be
// written, so the TS publish fails; for fMP4, whose first segment opens at the first frame, that frame fails.
VOID TEST(AppHlsTest, KeysOnFullDisk_KeyNotWritten)
{
    srs_error_t err;

    if (true) {
        HlsUtestVolumeScope volume;
        HlsUtestStream s("ai43-hls-key-full-ts");
        volume.fill();
        err = s.publish("hls_keys on;");
        EXPECT_EQ(ERROR_SYSTEM_FILE_WRITE, srs_error_code(err));
        EXPECT_NE(string::npos, srs_error_desc(err).find("write hls key")) << srs_error_desc(err);
        srs_freep(err);
        EXPECT_FALSE(s.exists("livestream-0.ts.tmp"));
    }

    if (true) {
        HlsUtestVolumeScope volume;
        HlsUtestStream s("ai43-hls-key-full-fmp4");
        HELPER_ASSERT_SUCCESS(s.publish("hls_keys on; hls_use_fmp4 on;"));
        HELPER_ASSERT_SUCCESS(s.sequence_headers());
        volume.fill();
        err = s.video(0, flv_utest_avc_idr, sizeof(flv_utest_avc_idr));
        EXPECT_EQ(ERROR_SYSTEM_FILE_WRITE, srs_error_code(err));
        EXPECT_NE(string::npos, srs_error_desc(err).find("write hls key")) << srs_error_desc(err);
        srs_freep(err);
        EXPECT_FALSE(s.exists("livestream-0.m4s.tmp"));
    }
}

// The operator turns the HTTP hooks on for on_publish only. A segment's on_hls and on_hls_notify callbacks have no
// backend to call, so they call nothing; with an on_hls_notify backend that is down, the callback fails naming it.
VOID TEST(AppHlsTest, HlsCallbacksByHooksConfig)
{
    srs_error_t err;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.mock_parse(string(_MIN_OK_CONF) + "vhost __defaultVhost__ { http_hooks { enabled on; "
                                                                 "on_publish http://127.0.0.1:8085/api/v1/streams; } }"));
    MockSrsConfig conf2;
    HELPER_ASSERT_SUCCESS(conf2.mock_parse(string(_MIN_OK_CONF) + "vhost __defaultVhost__ { http_hooks { enabled on; "
                                                                  "on_hls_notify http://127.0.0.1:8085/[app]/[stream]/[ts_url]; } }"));
    MockHttpHooksForHlsNotify hooks;
    MockSrsRequest req("__defaultVhost__", "live", "livestream");

    SrsDvrAsyncCallOnHls on_hls(SrsContextId(), &req, "./objs/nginx/html/live/livestream-0.ts", "livestream-0.ts",
                                "./objs/nginx/html/live/livestream.m3u8", "live/livestream.m3u8", 0, 10 * SRS_UTIME_SECONDS);
    on_hls.config_ = &conf;
    on_hls.hooks_ = &hooks;
    HELPER_EXPECT_SUCCESS(on_hls.call());
    EXPECT_EQ(0, (int)hooks.on_hls_ts_urls_.size());

    SrsDvrAsyncCallOnHlsNotify notify(SrsContextId(), &req, "livestream-0.ts");
    notify.config_ = &conf;
    notify.hooks_ = &hooks;
    HELPER_EXPECT_SUCCESS(notify.call());
    EXPECT_EQ(0, hooks.on_hls_notify_count_);

    srs_error_t down = srs_error_new(ERROR_SOCKET_CONNECT, "connect refused");
    hooks.set_on_hls_notify_error(down);
    srs_freep(down);
    notify.config_ = &conf2;
    err = notify.call();
    EXPECT_EQ(ERROR_SOCKET_CONNECT, srs_error_code(err));
    EXPECT_NE(string::npos, srs_error_desc(err).find("callback on_hls_notify http://127.0.0.1:8085/[app]/[stream]/[ts_url]"))
        << srs_error_desc(err);
    srs_freep(err);
    EXPECT_EQ(1, hooks.on_hls_notify_count_);

    on_hls.config_ = NULL;
    on_hls.hooks_ = NULL;
    notify.config_ = NULL;
    notify.hooks_ = NULL;
}

// The socket of an HTTP API client that closes its connection after the first writes of the reply: every write from
// the given one on fails, as a send to a peer that sent a FIN or RST does.
class HttpApiUtestLeavingClientIO : public MockBufferIO
{
public:
    // The write that fails first, from 1, and the writes made so far.
    int fail_at_;
    int nb_writes_;

public:
    HttpApiUtestLeavingClientIO(int fail_at)
    {
        fail_at_ = fail_at;
        nb_writes_ = 0;
    }
    virtual ~HttpApiUtestLeavingClientIO()
    {
    }

public:
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite)
    {
        if (++nb_writes_ >= fail_at_) {
            return srs_error_new(ERROR_SOCKET_WRITE, "peer reset");
        }
        return MockBufferIO::write(buf, size, nwrite);
    }
};

// The HTTP API of a server, mounted at the paths SrsServer mounts, over a real stream registry that the tests fill as
// publishers and players come and go.
class HttpApiUtestServer
{
public:
    MockAppConfig config_;
    SrsStatistic stat_;
    SrsHttpServeMux mux_;

public:
    HttpApiUtestServer()
    {
        stat_.config_ = &config_;
        stat_.server_id_ = "vid-utest-server";

        SrsGoApiVersion *version = new SrsGoApiVersion();
        version->stat_ = &stat_;
        mux_.handle("/api/v1/versions", version);

        SrsGoApiVhosts *vhosts = new SrsGoApiVhosts();
        vhosts->stat_ = &stat_;
        mux_.handle("/api/v1/vhosts/", vhosts);

        SrsGoApiStreams *streams = new SrsGoApiStreams();
        streams->stat_ = &stat_;
        mux_.handle("/api/v1/streams/", streams);

        SrsGoApiClients *clients = new SrsGoApiClients();
        clients->stat_ = &stat_;
        mux_.handle("/api/v1/clients/", clients);

        mux_.handle("/api/v1/tests/errors", new SrsGoApiError());
        mux_.handle("error.srs.com/api/v1/tests/errors", new SrsGoApiError());
    }
    virtual ~HttpApiUtestServer()
    {
    }

public:
    // A request of the method to the url, as the HTTP API connection parses it, with JSONP allowed as the API server
    // allows it.
    srs_error_t serve(llhttp_method_t method, string url, MockBufferIO *io)
    {
        srs_error_t err = srs_success;

        SrsHttpMessage r(NULL, NULL);
        r.set_basic(HTTP_REQUEST, method, (llhttp_status_t)200, -1);
        if ((err = r.set_url(url, true)) != srs_success) {
            return srs_error_wrap(err, "url");
        }

        SrsHttpResponseWriter w(io);
        return mux_.serve_http(&w, &r);
    }
};

// The status line of an HTTP reply the API wrote.
static string http_api_utest_status(MockBufferIO &io)
{
    string reply = string(io.out_buffer.bytes(), io.out_buffer.length());
    return reply.substr(0, reply.find("\r\n"));
}

// The body of an HTTP reply the API wrote.
static string http_api_utest_body(MockBufferIO &io)
{
    string reply = string(io.out_buffer.bytes(), io.out_buffer.length());
    size_t pos = reply.find("\r\n\r\n");
    return pos == string::npos ? "" : reply.substr(pos + 4);
}

// The JSON body of an HTTP reply the API wrote, or NULL if it is not a JSON object.
static SrsJsonObject *http_api_utest_json(MockBufferIO &io)
{
    SrsJsonAny *json = SrsJsonAny::loads(http_api_utest_body(io));
    if (json && !json->is_object()) {
        srs_freep(json);
    }
    return (SrsJsonObject *)json;
}

// The code of the JSON reply, or -1 if it has none.
static int http_api_utest_code(MockBufferIO &io)
{
    SrsUniquePtr<SrsJsonObject> obj(http_api_utest_json(io));
    SrsJsonAny *code = obj.get() ? obj->get_property("code") : NULL;
    return (code && code->is_integer()) ? (int)code->to_integer() : -1;
}

// A dashboard lists the streams of a server where two encoders publish, a page at a time, then opens one of them.
// The list counts every stream in its total, while the page holds only the streams asked for.
VOID TEST(AppHttpApiTest, DashboardListsStreamsByPage_TotalCountsAll)
{
    srs_error_t err;

    HttpApiUtestServer server;
    MockRequest a("__defaultVhost__", "live", "camera1");
    MockRequest b("__defaultVhost__", "live", "camera2");
    MockExpire conn_a, conn_b;
    HELPER_EXPECT_SUCCESS(server.stat_.on_client("cid-pub-1", &a, &conn_a, SrsRtmpConnFMLEPublish));
    server.stat_.on_stream_publish(&a, "cid-pub-1");
    HELPER_EXPECT_SUCCESS(server.stat_.on_client("cid-pub-2", &b, &conn_b, SrsRtmpConnFMLEPublish));
    server.stat_.on_stream_publish(&b, "cid-pub-2");

    // The first page of one stream.
    MockBufferIO page;
    HELPER_ASSERT_SUCCESS(server.serve(HTTP_GET, "http://127.0.0.1:1985/api/v1/streams/?start=0&count=1", &page));
    SrsUniquePtr<SrsJsonObject> obj(http_api_utest_json(page));
    ASSERT_TRUE(obj.get() != NULL) << http_api_utest_body(page);
    EXPECT_EQ(0, obj->get_property("code")->to_integer());
    EXPECT_EQ(2, obj->get_property("total")->to_integer());
    EXPECT_STREQ("vid-utest-server", obj->get_property("server")->to_str().c_str());
    SrsJsonAny *streams = obj->get_property("streams");
    ASSERT_TRUE(streams != NULL && streams->is_array());
    ASSERT_EQ(1, ((SrsJsonArray *)streams)->count());

    // All streams, then the one the dashboard opens, by the id the list gave.
    MockBufferIO all;
    HELPER_ASSERT_SUCCESS(server.serve(HTTP_GET, "http://127.0.0.1:1985/api/v1/streams/", &all));
    SrsUniquePtr<SrsJsonObject> obj2(http_api_utest_json(all));
    ASSERT_TRUE(obj2.get() != NULL);
    EXPECT_EQ(2, obj2->get_property("total")->to_integer());
    SrsJsonArray *list = (SrsJsonArray *)obj2->get_property("streams");
    ASSERT_EQ(2, list->count());
    SrsJsonObject *first = list->at(0)->to_object();
    string sid = first->get_property("id")->to_str();
    string name = first->get_property("name")->to_str();

    MockBufferIO one;
    HELPER_ASSERT_SUCCESS(server.serve(HTTP_GET, "http://127.0.0.1:1985/api/v1/streams/" + sid, &one));
    SrsUniquePtr<SrsJsonObject> obj3(http_api_utest_json(one));
    ASSERT_TRUE(obj3.get() != NULL);
    SrsJsonAny *stream = obj3->get_property("stream");
    ASSERT_TRUE(stream != NULL && stream->is_object());
    EXPECT_STREQ(sid.c_str(), stream->to_object()->get_property("id")->to_str().c_str());
    EXPECT_STREQ(name.c_str(), stream->to_object()->get_property("name")->to_str().c_str());
    EXPECT_TRUE(stream->to_object()->get_property("publish")->to_object()->get_property("active")->to_boolean());
}

// A dashboard holds the id of a stream whose encoder then stops: once the publisher leaves, the stream is gone from the
// registry, and the next query of its id answers stream not found, not a stale stream.
VOID TEST(AppHttpApiTest, StreamOpenedAfterEncoderLeft_NotFound)
{
    srs_error_t err;

    HttpApiUtestServer server;
    MockRequest req("__defaultVhost__", "live", "livestream");
    MockExpire conn;
    HELPER_EXPECT_SUCCESS(server.stat_.on_client("cid-pub", &req, &conn, SrsRtmpConnFMLEPublish));
    server.stat_.on_stream_publish(&req, "cid-pub");
    SrsStatisticStream *stream = server.stat_.find_stream_by_url(req.get_stream_url());
    ASSERT_TRUE(stream != NULL);
    string sid = stream->id_;

    server.stat_.on_stream_close(&req);
    server.stat_.on_disconnect("cid-pub", srs_success);

    MockBufferIO io;
    HELPER_ASSERT_SUCCESS(server.serve(HTTP_GET, "http://127.0.0.1:1985/api/v1/streams/" + sid, &io));
    EXPECT_STREQ("HTTP/1.1 200 OK", http_api_utest_status(io).c_str());
    EXPECT_EQ(ERROR_RTMP_STREAM_NOT_FOUND, http_api_utest_code(io)) << http_api_utest_body(io);

    // A player that left is not found either.
    MockBufferIO client;
    HELPER_ASSERT_SUCCESS(server.serve(HTTP_GET, "http://127.0.0.1:1985/api/v1/clients/cid-pub", &client));
    EXPECT_EQ(ERROR_RTMP_CLIENT_NOT_FOUND, http_api_utest_code(client)) << http_api_utest_body(client);
}

// A dashboard opens a vhost by the id the vhost list gave: the reply has its name and the streams and clients on it.
// An id the server does not know, such as one kept from before a restart (ids are random per run), answers vhost not
// found.
VOID TEST(AppHttpApiTest, VhostOpenedById_UnknownIdNotFound)
{
    srs_error_t err;

    HttpApiUtestServer server;
    MockRequest req("ossrs.net", "live", "livestream");
    MockExpire publisher, player;
    HELPER_EXPECT_SUCCESS(server.stat_.on_client("cid-pub", &req, &publisher, SrsRtmpConnFMLEPublish));
    server.stat_.on_stream_publish(&req, "cid-pub");
    HELPER_EXPECT_SUCCESS(server.stat_.on_client("cid-play", &req, &player, SrsRtmpConnPlay));
    SrsStatisticVhost *vhost = server.stat_.find_vhost_by_name("ossrs.net");
    ASSERT_TRUE(vhost != NULL);
    vhost->config_ = &server.config_;

    MockBufferIO io;
    HELPER_ASSERT_SUCCESS(server.serve(HTTP_GET, "http://127.0.0.1:1985/api/v1/vhosts/" + vhost->id_, &io));
    SrsUniquePtr<SrsJsonObject> obj(http_api_utest_json(io));
    ASSERT_TRUE(obj.get() != NULL) << http_api_utest_body(io);
    EXPECT_EQ(0, obj->get_property("code")->to_integer());
    SrsJsonAny *data = obj->get_property("vhost");
    ASSERT_TRUE(data != NULL && data->is_object());
    EXPECT_STREQ(vhost->id_.c_str(), data->to_object()->get_property("id")->to_str().c_str());
    EXPECT_STREQ("ossrs.net", data->to_object()->get_property("name")->to_str().c_str());
    EXPECT_EQ(1, data->to_object()->get_property("streams")->to_integer());
    EXPECT_EQ(2, data->to_object()->get_property("clients")->to_integer());

    MockBufferIO unknown;
    HELPER_ASSERT_SUCCESS(server.serve(HTTP_GET, "http://127.0.0.1:1985/api/v1/vhosts/vid-0from0restart", &unknown));
    EXPECT_EQ(ERROR_RTMP_VHOST_NOT_FOUND, http_api_utest_code(unknown)) << http_api_utest_body(unknown);
}

// The vhost, stream and client APIs only read, besides kicking a client with DELETE: a client that POSTs or PUTs to
// them, such as a script that tries to change a stream, gets 405 Method Not Allowed and nothing changes.
VOID TEST(AppHttpApiTest, WriteMethodsToReadOnlyApis_MethodNotAllowed)
{
    srs_error_t err;

    HttpApiUtestServer server;
    MockRequest req("__defaultVhost__", "live", "livestream");
    MockExpire conn;
    HELPER_EXPECT_SUCCESS(server.stat_.on_client("cid-play", &req, &conn, SrsRtmpConnPlay));

    MockBufferIO vhosts;
    HELPER_ASSERT_SUCCESS(server.serve(HTTP_POST, "http://127.0.0.1:1985/api/v1/vhosts/", &vhosts));
    EXPECT_STREQ("HTTP/1.1 405 Method Not Allowed", http_api_utest_status(vhosts).c_str());

    MockBufferIO streams;
    HELPER_ASSERT_SUCCESS(server.serve(HTTP_PUT, "http://127.0.0.1:1985/api/v1/streams/", &streams));
    EXPECT_STREQ("HTTP/1.1 405 Method Not Allowed", http_api_utest_status(streams).c_str());

    MockBufferIO clients;
    HELPER_ASSERT_SUCCESS(server.serve(HTTP_PUT, "http://127.0.0.1:1985/api/v1/clients/cid-play", &clients));
    EXPECT_STREQ("HTTP/1.1 405 Method Not Allowed", http_api_utest_status(clients).c_str());

    EXPECT_FALSE(conn.expired_);
    EXPECT_TRUE(server.stat_.find_client("cid-play") != NULL);
}

// An operator kicks an RTMP player with DELETE on its client id: the player's connection is expired, so its coroutine
// ends and closes the socket, and the API answers success. A DELETE with no client id, such as a script whose id
// variable was empty, kicks nobody and answers client not found.
VOID TEST(AppHttpApiTest, OperatorKicksRtmpPlayer_ConnectionExpired)
{
    srs_error_t err;

    HttpApiUtestServer server;
    MockRequest req("__defaultVhost__", "live", "livestream");
    MockExpire player, other;
    HELPER_EXPECT_SUCCESS(server.stat_.on_client("cid-play", &req, &player, SrsRtmpConnPlay));
    HELPER_EXPECT_SUCCESS(server.stat_.on_client("cid-other", &req, &other, SrsRtmpConnPlay));

    MockBufferIO io;
    HELPER_ASSERT_SUCCESS(server.serve(HTTP_DELETE, "http://127.0.0.1:1985/api/v1/clients/cid-play", &io));
    EXPECT_STREQ("HTTP/1.1 200 OK", http_api_utest_status(io).c_str());
    EXPECT_EQ(0, http_api_utest_code(io)) << http_api_utest_body(io);
    EXPECT_TRUE(player.expired_);
    EXPECT_FALSE(other.expired_);

    MockBufferIO empty;
    HELPER_ASSERT_SUCCESS(server.serve(HTTP_DELETE, "http://127.0.0.1:1985/api/v1/clients/", &empty));
    EXPECT_EQ(ERROR_RTMP_CLIENT_NOT_FOUND, http_api_utest_code(empty)) << http_api_utest_body(empty);
    EXPECT_FALSE(other.expired_);
}

// An HLS viewer is listed as a client, but it has no connection of its own to close: each playlist and segment is a
// separate HTTP request. Kicking it is refused with code 400 and an error log, and the viewer stays listed.
VOID TEST(AppHttpApiTest, OperatorKicksHlsViewer_RefusedNoConnection)
{
    srs_error_t err;

    HttpApiUtestServer server;
    MockRequest req("__defaultVhost__", "live", "livestream");
    HELPER_EXPECT_SUCCESS(server.stat_.on_client("cid-hls", &req, NULL, SrsHlsPlay));

    MockLogForSignal log;
    ISrsLog *original_log = _srs_log;
    _srs_log = &log;

    MockBufferIO io;
    err = server.serve(HTTP_DELETE, "http://127.0.0.1:1985/api/v1/clients/cid-hls", &io);

    _srs_log = original_log;
    HELPER_ASSERT_SUCCESS(err);

    EXPECT_EQ(SRS_CONSTS_HTTP_BadRequest, http_api_utest_code(io)) << http_api_utest_body(io);
    EXPECT_TRUE(server.stat_.find_client("cid-hls") != NULL);
    EXPECT_STRNE("", utest_log_find(log.errors_, "kickoff client id=cid-hls error").c_str());
}

// The old console talks JSONP: it sends every call as a GET with a callback and the real method in the query. Its
// kick of a player expires the connection as a DELETE does, and the reply is wrapped in the callback, so the page's
// script tag runs it.
VOID TEST(AppHttpApiTest, JsonpConsoleKicksPlayer_ReplyWrappedInCallback)
{
    srs_error_t err;

    HttpApiUtestServer server;
    MockRequest req("__defaultVhost__", "live", "livestream");
    MockExpire player;
    HELPER_EXPECT_SUCCESS(server.stat_.on_client("cid-play", &req, &player, SrsRtmpConnPlay));

    MockBufferIO io;
    HELPER_ASSERT_SUCCESS(server.serve(HTTP_GET, "http://127.0.0.1:1985/api/v1/clients/cid-play?callback=onKick&method=DELETE", &io));
    EXPECT_TRUE(player.expired_);

    string body = http_api_utest_body(io);
    ASSERT_GT(body.length(), 8u) << body;
    EXPECT_EQ(0u, body.find("onKick({")) << body;
    EXPECT_EQ(')', body.at(body.length() - 1)) << body;
    EXPECT_NE(string::npos, body.find("\"code\":0")) << body;
}

// An API client, such as a monitor that polls with a short timeout, closes its connection while the reply is sent.
// At whichever write the socket fails, the handler stops writing and returns the error, so the connection ends;
// nothing after the failed write is sent. For JSONP the reply goes out in parts: the callback, "(", the data and ")".
VOID TEST(AppHttpApiTest, ClientLeavesMidReply_WriteErrorEndsReply)
{
    srs_error_t err;

    HttpApiUtestServer server;

    // JSON: the header, then the body in one write.
    for (int fail_at = 1; fail_at <= 2; fail_at++) {
        HttpApiUtestLeavingClientIO io(fail_at);
        err = server.serve(HTTP_GET, "http://127.0.0.1:1985/api/v1/versions", &io);
        EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err)) << fail_at;
        EXPECT_NE(string::npos, srs_error_desc(err).find("write json")) << fail_at << ": " << srs_error_desc(err);
        srs_freep(err);
        EXPECT_EQ(fail_at, io.nb_writes_);
    }

    // JSONP: the header with the callback, then "(", the data and ")".
    const char *parts[] = {"write jsonp callback", "write jsonp callback", "write jsonp left token", "write jsonp data", "write jsonp right token"};
    for (int fail_at = 1; fail_at <= 5; fail_at++) {
        HttpApiUtestLeavingClientIO io(fail_at);
        err = server.serve(HTTP_GET, "http://127.0.0.1:1985/api/v1/versions?callback=onVersion", &io);
        EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err)) << fail_at;
        EXPECT_NE(string::npos, srs_error_desc(err).find(parts[fail_at - 1])) << fail_at << ": " << srs_error_desc(err);
        srs_freep(err);
        EXPECT_EQ(fail_at, io.nb_writes_);
    }

    // The client stays: the whole JSONP reply is written.
    HttpApiUtestLeavingClientIO io(100);
    HELPER_ASSERT_SUCCESS(server.serve(HTTP_GET, "http://127.0.0.1:1985/api/v1/versions?callback=onVersion", &io));
    EXPECT_EQ(5, io.nb_writes_);
    string body = http_api_utest_body(io);
    EXPECT_EQ(0u, body.find("onVersion({")) << body;
    EXPECT_EQ(')', body.at(body.length() - 1)) << body;
}

// The error test endpoint, /api/v1/tests/errors, always answers code 100, for a client to check how it handles an API
// error; the HTTP vhost error.srs.com serves its own copy at the same path.
VOID TEST(AppHttpApiTest, ErrorTestEndpoint_AnswersCode100)
{
    srs_error_t err;

    HttpApiUtestServer server;

    MockBufferIO io;
    HELPER_ASSERT_SUCCESS(server.serve(HTTP_GET, "http://127.0.0.1:1985/api/v1/tests/errors", &io));
    EXPECT_STREQ("HTTP/1.1 200 OK", http_api_utest_status(io).c_str());
    EXPECT_EQ(100, http_api_utest_code(io)) << http_api_utest_body(io);

    MockBufferIO vhost;
    HELPER_ASSERT_SUCCESS(server.serve(HTTP_GET, "http://error.srs.com:1985/api/v1/tests/errors", &vhost));
    EXPECT_EQ(100, http_api_utest_code(vhost)) << http_api_utest_body(vhost);
}

// With the RAW API enabled, an rpc the server does not have, such as a typo of reload or a missing rpc, is refused
// with ERROR_SYSTEM_CONFIG_RAW, and no signal is raised.
VOID TEST(AppHttpApiTest, RawApiUnknownRpc_RefusedNoSignal)
{
    srs_error_t err;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.mock_parse(string(_MIN_OK_CONF) + "http_api { enabled on; raw_api { enabled on; allow_reload on; } }"));

    MockSignalHandler handler;
    SrsHttpServeMux mux;
    SrsGoApiRaw *raw = new SrsGoApiRaw(&handler);
    raw->config_ = &conf;
    raw->assemble();
    HELPER_ASSERT_SUCCESS(mux.handle("/api/v1/raw", raw));

    const char *urls[] = {"http://127.0.0.1:1985/api/v1/raw?rpc=relaod", "http://127.0.0.1:1985/api/v1/raw"};
    for (int i = 0; i < 2; i++) {
        SrsHttpMessage r(NULL, NULL);
        HELPER_ASSERT_SUCCESS(r.set_url(urls[i], true));
        MockBufferIO io;
        SrsHttpResponseWriter w(&io);
        HELPER_ASSERT_SUCCESS(mux.serve_http(&w, &r));
        EXPECT_EQ(ERROR_SYSTEM_CONFIG_RAW, http_api_utest_code(io)) << urls[i] << ": " << http_api_utest_body(io);
    }
    EXPECT_EQ(0, handler.signal_count_);

    // The same server reloads for the right rpc.
    SrsHttpMessage r(NULL, NULL);
    HELPER_ASSERT_SUCCESS(r.set_url("http://127.0.0.1:1985/api/v1/raw?rpc=reload", true));
    MockBufferIO io;
    SrsHttpResponseWriter w(&io);
    HELPER_ASSERT_SUCCESS(mux.serve_http(&w, &r));
    EXPECT_EQ(0, http_api_utest_code(io)) << http_api_utest_body(io);
    EXPECT_EQ(SRS_SIGNAL_RELOAD, handler.signal_received_);
}

// The server mounts /metrics on the HTTP API whether the exporter is on or not. A Prometheus that scrapes it with the
// exporter off gets a JSON error, ERROR_EXPORTER_DISABLED, not metrics.
VOID TEST(AppHttpApiTest, MetricsScrapedWithExporterOff_Refused)
{
    srs_error_t err;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.mock_parse(string(_MIN_OK_CONF) + "http_api { enabled on; } exporter { enabled off; }"));

    SrsStatistic stat;
    stat.config_ = &conf;
    SrsHttpServeMux mux;
    SrsGoApiMetrics *metrics = new SrsGoApiMetrics();
    metrics->stat_ = &stat;
    metrics->config_ = &conf;
    metrics->assemble();
    HELPER_ASSERT_SUCCESS(mux.handle("/metrics", metrics));

    SrsHttpMessage r(NULL, NULL);
    HELPER_ASSERT_SUCCESS(r.set_url("http://127.0.0.1:1985/metrics", true));
    MockBufferIO io;
    SrsHttpResponseWriter w(&io);
    HELPER_ASSERT_SUCCESS(mux.serve_http(&w, &r));
    EXPECT_EQ(ERROR_EXPORTER_DISABLED, http_api_utest_code(io)) << http_api_utest_body(io);
    EXPECT_EQ(string::npos, http_api_utest_body(io).find("srs_build_info"));
}

// The coroutine of an HTTP connection: a kick (expire) interrupts it, and from then on each pull fails, as for the
// server's coroutine.
class HttpConnUtestCoroutine : public MockCoroutineForCycle
{
public:
    virtual void interrupt()
    {
        if (pull_error_ == srs_success) {
            pull_error_ = srs_error_new(ERROR_THREAD_INTERRUPED, "interrupted");
        }
    }
};

// A client of the HTTP API listener, whose socket holds the requests it sends, served by the server's HTTP connection
// and its owner, with the API mounted as the server mounts it.
class HttpConnUtestClient
{
public:
    MockSrsConfig conf_;
    MockResourceManagerForHttpxConn manager_;
    HttpApiUtestServer api_;
    MockBufferIO *io_;
    SrsHttpxConn *connx_;
    SrsHttpConn *conn_;

public:
    HttpConnUtestClient(string requests)
    {
        io_ = new MockBufferIO();
        io_->append(requests);

        connx_ = new SrsHttpxConn(&manager_, io_, &api_.mux_, "127.0.0.1", 50000, "", "");
        connx_->config_ = &conf_;
        connx_->set_api_listener(true);

        conn_ = new SrsHttpConn(connx_, io_, &api_.mux_, "127.0.0.1", 50000);
        conn_->config_ = &conf_;
        conn_->trd_ = new HttpConnUtestCoroutine();
        connx_->conn_ = conn_;
    }
    virtual ~HttpConnUtestClient()
    {
        conn_->config_ = NULL;
        connx_->config_ = NULL;
        srs_freep(connx_);
    }

public:
    // Starts the connection as the server does when it accepts the client, by this http_api section.
    srs_error_t start(string http_api)
    {
        srs_error_t err = srs_success;
        if ((err = conf_.mock_parse(string(_MIN_OK_CONF) + "http_api { enabled on; " + http_api + " }")) != srs_success) {
            return srs_error_wrap(err, "config");
        }
        return connx_->start();
    }
    // Runs the connection's coroutine until the client has nothing more to send, and returns what it was sent.
    string serve()
    {
        srs_error_t err = conn_->cycle();
        EXPECT_TRUE(err == srs_success);
        srs_freep(err);
        return string(io_->out_buffer.bytes(), io_->out_buffer.length());
    }
};

// The status lines of the HTTP replies in what a client was sent, in order.
static vector<string> http_conn_utest_statuses(string replies)
{
    vector<string> statuses;
    for (size_t pos = replies.find("HTTP/1.1 "); pos != string::npos; pos = replies.find("HTTP/1.1 ", pos + 1)) {
        statuses.push_back(replies.substr(pos, replies.find("\r\n", pos) - pos));
    }
    return statuses;
}

// The operator protects the HTTP API with basic auth. A dashboard on one keep-alive connection that sends no
// credentials, or a wrong password, is refused with 401 and the Basic challenge, so a browser prompts for them; with
// the right user and password it is served. A path outside /api/, such as a stream, is not protected: it gets the
// mux's 404, not a 401.
VOID TEST(AppHttpConnTest, ApiBasicAuth_OnlyRightCredentialsServed)
{
    srs_error_t err;

    // admin:secret and admin:wrong in base64.
    HttpConnUtestClient client(
        "GET /api/v1/versions HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"
        "GET /api/v1/versions HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Basic YWRtaW46d3Jvbmc=\r\n\r\n"
        "GET /api/v1/versions HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Basic YWRtaW46c2VjcmV0\r\n\r\n"
        "GET /live/livestream.flv HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
    HELPER_ASSERT_SUCCESS(client.start("auth { enabled on; type basic; username admin; password secret; }"));

    string replies = client.serve();
    vector<string> statuses = http_conn_utest_statuses(replies);
    ASSERT_EQ(4, (int)statuses.size()) << replies;
    EXPECT_STREQ("HTTP/1.1 401 Unauthorized", statuses[0].c_str());
    EXPECT_STREQ("HTTP/1.1 401 Unauthorized", statuses[1].c_str());
    EXPECT_STREQ("HTTP/1.1 200 OK", statuses[2].c_str());
    EXPECT_STREQ("HTTP/1.1 404 Not Found", statuses[3].c_str());
    EXPECT_NE(string::npos, replies.find("WWW-Authenticate: Basic realm=\"SRS\"\r\n")) << replies;
    EXPECT_NE(string::npos, replies.find("\"version\":\"" RTMP_SIG_SRS_VERSION "\"")) << replies;

    // The client left after its requests, so the connection ends and removes itself.
    EXPECT_TRUE(client.manager_.remove_called_);
}

// The operator protects the HTTP API with a bearer token, and the WebRTC signaling too (rtc_bearer_enabled). A script
// with no token is refused with 401 and the Bearer challenge, at the API and at a WHIP publish; with the token it is
// served. With rtc_bearer_enabled off, the signaling is not protected: the WHIP path gets past auth to the mux.
VOID TEST(AppHttpConnTest, ApiBearerAuth_TokenGuardsApiAndRtcSignaling)
{
    srs_error_t err;

    string requests = "GET /api/v1/versions HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n"
                      "GET /api/v1/versions HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer t1\r\n\r\n"
                      "POST /rtc/v1/whip/?app=live&stream=livestream HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\n\r\n";

    if (true) {
        HttpConnUtestClient client(requests);
        HELPER_ASSERT_SUCCESS(client.start("auth { enabled on; type bearer; token t1; rtc_bearer_enabled on; }"));

        string replies = client.serve();
        vector<string> statuses = http_conn_utest_statuses(replies);
        ASSERT_EQ(3, (int)statuses.size()) << replies;
        EXPECT_STREQ("HTTP/1.1 401 Unauthorized", statuses[0].c_str());
        EXPECT_STREQ("HTTP/1.1 200 OK", statuses[1].c_str());
        EXPECT_STREQ("HTTP/1.1 401 Unauthorized", statuses[2].c_str());
        EXPECT_NE(string::npos, replies.find("WWW-Authenticate: Bearer\r\n")) << replies;
    }

    if (true) {
        HttpConnUtestClient client(requests);
        HELPER_ASSERT_SUCCESS(client.start("auth { enabled on; type bearer; token t1; }"));

        string replies = client.serve();
        vector<string> statuses = http_conn_utest_statuses(replies);
        ASSERT_EQ(3, (int)statuses.size()) << replies;
        EXPECT_STREQ("HTTP/1.1 401 Unauthorized", statuses[0].c_str());
        EXPECT_STREQ("HTTP/1.1 200 OK", statuses[1].c_str());
        EXPECT_STREQ("HTTP/1.1 404 Not Found", statuses[2].c_str());
    }
}

// The process is out of memory when the server accepts an HTTP client, so no coroutine can be created for it. The
// start fails with the ST error, and the server closes the client.
VOID TEST(AppHttpConnTest, ClientWhenNoCoroutineCanBeCreated_Refused)
{
    HttpConnUtestClient client("GET /api/v1/versions HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
    srs_freep(client.conn_->trd_);
    client.conn_->assemble();

    _ST_THREAD_CREATE_PFN ov = _pfn_st_thread_create;
    _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)forward_st_thread_create_out_of_memory;
    srs_error_t err = client.start("");
    _pfn_st_thread_create = ov;

    EXPECT_EQ(ERROR_ST_CREATE_CYCLE_THREAD, srs_error_code(err));
    EXPECT_NE(string::npos, srs_error_desc(err).find("coroutine")) << srs_error_desc(err);
    srs_freep(err);
    EXPECT_EQ(0, client.io_->out_length());
}

// The live stream handler of an HTTP-FLV viewer, whose publisher has unpublished: it ends the viewer with stream EOF,
// as SrsLiveStream does once its entry is disabled.
class HttpConnUtestUnpublishedStream : public ISrsHttpHandler
{
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
    {
        return srs_error_new(ERROR_HTTP_STREAM_EOF, "Stream EOF");
    }
};

// Each way an HTTP connection ends is logged by its cause, and none is an error of the coroutine: a client that leaves
// is a warning, an HTTP-FLV viewer ended by its publisher's unpublish is a server disconnect warning, and a client that
// sends a request that is not HTTP, such as a scanner, is an error log naming the parse error.
VOID TEST(AppHttpConnTest, ConnectionEnd_LoggedByCause)
{
    srs_error_t err;

    const char *requests[] = {
        "GET /api/v1/versions HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n",
        "GET /live/livestream.flv HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n",
        "\x16\x03\x01\x02\x00\x01\x00\x01\xfc\x03\x03\r\n\r\n",
    };
    for (int i = 0; i < 3; i++) {
        HttpConnUtestClient client(requests[i]);
        HELPER_ASSERT_SUCCESS(client.api_.mux_.handle("/live/livestream.flv", new HttpConnUtestUnpublishedStream()));
        HELPER_ASSERT_SUCCESS(client.start(""));

        MockLogForSignal log;
        ISrsLog *original = _srs_log;
        _srs_log = &log;
        client.serve();
        _srs_log = original;

        if (i == 0) {
            EXPECT_NE("", utest_log_find(log.warns_, "client disconnect peer. ret=1007"));
            EXPECT_EQ(0, log.error_count_);
        } else if (i == 1) {
            EXPECT_NE("", utest_log_find(log.warns_, "server disconnect. ret=4040"));
            EXPECT_EQ(0, log.error_count_);
        } else {
            EXPECT_NE("", utest_log_find(log.errors_, "serve error"));
            EXPECT_NE("", utest_log_find(log.errors_, "parse message"));
            EXPECT_EQ(0, log.warn_count_);
        }
        EXPECT_TRUE(client.manager_.remove_called_) << i;
    }
}

// The socket of an HTTP-FLV viewer that sends nothing after its request: each read of the recv thread times out,
// and at the given read the operator kicks the viewer, as the API's DELETE does from another coroutine.
class HttpConnUtestIdleViewerSocket : public MockSslConnection
{
public:
    int kick_at_;
    ISrsExpire *kick_;

public:
    HttpConnUtestIdleViewerSocket()
    {
        kick_at_ = 0;
        kick_ = NULL;
    }

public:
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread)
    {
        if (++read_count_ == kick_at_ && kick_) {
            kick_->expire();
        }
        return srs_error_new(ERROR_SOCKET_TIMEOUT, "timeout");
    }
};

// The recv thread of an idle HTTP-FLV viewer reads its socket with a 3s timeout, so a kick is seen within 3s: it reads
// again at each timeout, and after the operator kicks the viewer, it stops at the next timeout with the interrupt. An
// HTTPS-FLV viewer is read through TLS, never from the raw socket under it.
VOID TEST(AppHttpConnTest, IdleFlvViewerKicked_RecvThreadStopsAtNextTimeout)
{
    srs_error_t err;

    for (int https = 0; https < 2; https++) {
        HttpConnUtestIdleViewerSocket *raw = new HttpConnUtestIdleViewerSocket();
        HttpConnUtestIdleViewerSocket *tls = https ? new HttpConnUtestIdleViewerSocket() : NULL;
        HttpConnUtestIdleViewerSocket *skt = https ? tls : raw;

        SrsUniquePtr<SrsHttpxConn> connx(new SrsHttpxConn(NULL, raw, NULL, "127.0.0.1", 50000, https ? "/key.pem" : "", https ? "/cert.pem" : ""));
        connx->ssl_ = tls;
        SrsHttpConn *conn = new SrsHttpConn(connx.get(), skt, NULL, "127.0.0.1", 50000);
        conn->trd_ = new HttpConnUtestCoroutine();
        connx->conn_ = conn;

        skt->kick_at_ = 3;
        skt->kick_ = conn;

        err = connx->pop_message(NULL);
        EXPECT_EQ(ERROR_THREAD_INTERRUPED, srs_error_code(err)) << https;
        srs_freep(err);

        EXPECT_EQ(3, skt->read_count_) << https;
        EXPECT_EQ(3 * SRS_UTIME_SECONDS, skt->get_recv_timeout()) << https;
        if (https) {
            EXPECT_EQ(0, raw->read_count_);
        }
    }
}

// The server's own HTTP hooks, posting with its HTTP client, over a real statistic, as the RTMP connection and the
// DVR and HLS workers call them.
class HooksUtestHooks
{
public:
    SrsAppFactory factory_;
    SrsStatistic stat_;
    MockSrsConfig config_;
    SrsHttpHooks hooks_;
    MockRequest req_;

public:
    HooksUtestHooks() : req_("__defaultVhost__", "live", "livestream")
    {
        hooks_.factory_ = &factory_;
        hooks_.stat_ = &stat_;
        hooks_.config_ = &config_;
        req_.ip_ = "192.168.1.100";
        req_.tcUrl_ = "rtmp://127.0.0.1/live";
    }
    ~HooksUtestHooks()
    {
        hooks_.factory_ = NULL;
        hooks_.stat_ = NULL;
        hooks_.config_ = NULL;
    }
};

// A hook server's whole answer that allows the client, as the SRS docs ask.
static string hooks_utest_ok()
{
    return mock_http_response(200, "{\"code\":0}");
}

// The body of the POST a hook server got.
static string hooks_utest_body(const string &request)
{
    size_t pos = request.find("\r\n\r\n");
    return pos == string::npos ? "" : request.substr(pos + 4);
}

// A publisher and a player of live/livestream are in the HTTP API's stream list when their hooks run, as the RTMP
// connection adds each client before its on_publish or on_play hook, and takes the stream snapshot before it leaves.
// Each callback names the stream by the id the API lists, so the hook server can look it up, and the play and stop
// callbacks carry the stream's client count.
VOID TEST(AppHttpHooksTest, HooksOfAListedStream_CarryItsApiStreamId)
{
    srs_error_t err;

    HooksUtestHooks h;
    HELPER_ASSERT_SUCCESS(h.stat_.on_client("hooks-publisher", &h.req_, NULL, SrsRtmpConnFMLEPublish));
    HELPER_ASSERT_SUCCESS(h.stat_.on_client("hooks-player", &h.req_, NULL, SrsRtmpConnPlay));
    SrsStatisticStream *stream = h.stat_.find_stream_by_url(h.req_.get_stream_url());
    ASSERT_TRUE(stream != NULL);
    string stream_id = "\"stream_id\":\"" + stream->id_ + "\"";
    SrsStatisticStreamSnapshot snapshot(&h.stat_, &h.req_);

    const char *actions[] = {"on_publish", "on_play", "on_unpublish", "on_stop", "on_dvr", "on_hls"};
    for (int i = 0; i < (int)(sizeof(actions) / sizeof(actions[0])); i++) {
        string action = actions[i];
        HttpUtestOneReplyServer server(hooks_utest_ok());
        HELPER_ASSERT_SUCCESS(server.start());
        string url = "http://" + server.server() + "/api/v1/streams";

        MockLogForSignal log;
        ISrsLog *original = _srs_log;
        _srs_log = &log;
        err = srs_success;
        if (action == "on_publish") {
            err = h.hooks_.on_publish(url, &h.req_, SrsRtmpConnFMLEPublish);
        } else if (action == "on_play") {
            err = h.hooks_.on_play(url, &h.req_, SrsRtmpConnPlay);
        } else if (action == "on_unpublish") {
            h.hooks_.on_unpublish(url, &h.req_, SrsRtmpConnFMLEPublish, snapshot);
        } else if (action == "on_stop") {
            h.hooks_.on_stop(url, &h.req_, SrsRtmpConnPlay, snapshot);
        } else if (action == "on_dvr") {
            err = h.hooks_.on_dvr(SrsContextId(), url, &h.req_, "./objs/nginx/html/live/livestream.flv", snapshot);
        } else {
            err = h.hooks_.on_hls(SrsContextId(), url, &h.req_, "./objs/nginx/html/live/livestream-0.ts", "livestream-0.ts",
                                  "./objs/nginx/html/live/livestream.m3u8", "live/livestream.m3u8", 0, 10 * SRS_UTIME_SECONDS);
        }
        _srs_log = original;
        HELPER_EXPECT_SUCCESS(err);
        EXPECT_TRUE(log.warns_.empty()) << action;

        string body = hooks_utest_body(server.request_);
        EXPECT_NE(string::npos, body.find("\"action\":\"" + action + "\"")) << body;
        EXPECT_NE(string::npos, body.find(stream_id)) << body;
        if (action == "on_play" || action == "on_stop") {
            EXPECT_NE(string::npos, body.find("\"clients\":2")) << body;
        }
    }
}

// An operator leaves the schema out of the on_connect url. The config loads, but each client is refused at connect
// with the url parse error, and nothing is posted, not even to the host and port the url names.
VOID TEST(AppHttpHooksTest, HookUrlWithoutSchema_ConnectRefusedAndNothingPosted)
{
    srs_error_t err;

    HooksUtestHooks h;
    HttpUtestOneReplyServer server(hooks_utest_ok());
    HELPER_ASSERT_SUCCESS(server.start());
    string url = server.server() + "/api/v1/clients";

    err = h.hooks_.on_connect(url, &h.req_);
    // Give the server a chance to accept, had the hook connected.
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    EXPECT_EQ(ERROR_HTTP_PARSE_URI, srs_error_code(err));
    string desc = srs_error_desc(err);
    srs_freep(err);
    EXPECT_NE(string::npos, desc.find("on_connect failed")) << desc;
    EXPECT_NE(string::npos, desc.find("url=" + url)) << desc;
    EXPECT_TRUE(server.request_.empty()) << server.request_;
}

// The hook server answers the publisher's on_publish, but closes before its whole body is sent: the publish is
// refused with the body read error, as an allow cannot be told from a deny.
VOID TEST(AppHttpHooksTest, HookServerClosesMidBody_PublishRefused)
{
    srs_error_t err;

    HooksUtestHooks h;
    HttpUtestOneReplyServer server("HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n{\"co");
    HELPER_ASSERT_SUCCESS(server.start());

    err = h.hooks_.on_publish("http://" + server.server() + "/api/v1/streams", &h.req_, SrsRtmpConnFMLEPublish);
    EXPECT_TRUE(err != srs_success);
    string desc = srs_error_desc(err);
    srs_freep(err);
    EXPECT_NE(string::npos, desc.find("on_publish failed")) << desc;
    EXPECT_NE(string::npos, desc.find("body read")) << desc;
    EXPECT_NE(string::npos, hooks_utest_body(server.request_).find("\"action\":\"on_publish\"")) << server.request_;
}

// An auth server denies a player with HTTP 403: the play is refused with the status, and the error names the url and
// the hook server's answer.
VOID TEST(AppHttpHooksTest, AuthServerDeniesPlayer_PlayRefused)
{
    srs_error_t err;

    HooksUtestHooks h;
    HttpUtestOneReplyServer server(mock_http_response(403, "denied"));
    HELPER_ASSERT_SUCCESS(server.start());
    string url = "http://" + server.server() + "/api/v1/sessions";

    err = h.hooks_.on_play(url, &h.req_, SrsRtmpConnPlay);
    EXPECT_EQ(ERROR_HTTP_STATUS_INVALID, srs_error_code(err));
    string desc = srs_error_desc(err);
    srs_freep(err);
    EXPECT_NE(string::npos, desc.find("on_play failed")) << desc;
    EXPECT_NE(string::npos, desc.find("url=" + url)) << desc;
    EXPECT_NE(string::npos, desc.find("response=denied")) << desc;
}

// The hook server is down when a client leaves: on_close, on_unpublish and on_stop cannot post, and each logs a
// warning naming the url and goes on, as the client is gone anyway.
VOID TEST(AppHttpHooksTest, HookServerDownWhenClientsLeave_CloseUnpublishStopOnlyWarn)
{
    HooksUtestHooks h;
    // Never started, so nothing listens on its port.
    HttpUtestOneReplyServer down("");
    string url = "http://" + down.server() + "/api/v1/clients";
    SrsStatisticStreamSnapshot snapshot(&h.stat_, &h.req_);

    MockLogForSignal log;
    ISrsLog *original = _srs_log;
    _srs_log = &log;
    h.hooks_.on_close(url, &h.req_, 1024, 2048);
    h.hooks_.on_unpublish(url, &h.req_, SrsRtmpConnFMLEPublish, snapshot);
    h.hooks_.on_stop(url, &h.req_, SrsRtmpConnPlay, snapshot);
    _srs_log = original;

    const char *actions[] = {"on_close", "on_unpublish", "on_stop"};
    for (int i = 0; i < 3; i++) {
        string warn = utest_log_find(log.warns_, string("http: ignore ") + actions[i] + " failed");
        EXPECT_NE(string::npos, warn.find("url=" + url)) << actions[i] << ": " << warn;
        EXPECT_NE(string::npos, warn.find("ret=" + srs_strconv_format_int(ERROR_ST_CONNECT))) << actions[i] << ": " << warn;
    }
}

// The hook server is down when a DVR file or an HLS segment is done: on_dvr and on_hls fail with the url, so the
// async worker that called them logs it.
VOID TEST(AppHttpHooksTest, HookServerDownWhenFileDone_DvrAndHlsCallbacksFail)
{
    srs_error_t err;

    HooksUtestHooks h;
    HttpUtestOneReplyServer down("");
    string url = "http://" + down.server() + "/api/v1/dvrs";
    SrsStatisticStreamSnapshot snapshot(&h.stat_, &h.req_);

    err = h.hooks_.on_dvr(SrsContextId(), url, &h.req_, "./objs/nginx/html/live/livestream.flv", snapshot);
    EXPECT_EQ(ERROR_ST_CONNECT, srs_error_code(err));
    string desc = srs_error_desc(err);
    srs_freep(err);
    EXPECT_NE(string::npos, desc.find("http post on_dvr uri failed")) << desc;
    EXPECT_NE(string::npos, desc.find("url=" + url)) << desc;

    err = h.hooks_.on_hls(SrsContextId(), url, &h.req_, "./objs/nginx/html/live/livestream-0.ts", "livestream-0.ts",
                          "./objs/nginx/html/live/livestream.m3u8", "live/livestream.m3u8", 0, 10 * SRS_UTIME_SECONDS);
    EXPECT_EQ(ERROR_ST_CONNECT, srs_error_code(err));
    desc = srs_error_desc(err);
    srs_freep(err);
    EXPECT_NE(string::npos, desc.find("http: post " + url)) << desc;
    EXPECT_NE(string::npos, desc.find("\"action\":\"on_hls\"")) << desc;
}

// With hls_entry_prefix set to a CDN, each segment's url in the playlist is absolute, and on_hls_notify fetches the
// segment from that url itself instead of the configured one, so the CDN caches it before players ask.
VOID TEST(AppHttpHooksTest, HlsNotifyWithCdnEntryPrefix_FetchesSegmentFromCdn)
{
    srs_error_t err;

    HooksUtestHooks h;
    HttpUtestOneReplyServer cdn(mock_http_response(200, "0123456789"));
    HELPER_ASSERT_SUCCESS(cdn.start());
    // The configured notify url, which nothing listens on.
    HttpUtestOneReplyServer down("");
    string ts_url = "http://" + cdn.server() + "/live/livestream-0.ts";

    MockLogForSignal log;
    ISrsLog *original = _srs_log;
    _srs_log = &log;
    err = h.hooks_.on_hls_notify(SrsContextId(), "http://" + down.server() + "/api/v1/hls/[app]/[stream]/[ts_url]", &h.req_, ts_url, 64);
    _srs_log = original;
    HELPER_EXPECT_SUCCESS(err);

    EXPECT_EQ(0, (int)cdn.request_.find("GET /live/livestream-0.ts HTTP/1.1\r\n")) << cdn.request_;
    string trace = utest_log_find(log.traces_, "on_hls_notify success");
    EXPECT_NE(string::npos, trace.find("url=" + ts_url)) << trace;
    EXPECT_NE(string::npos, trace.find("code=200")) << trace;
    EXPECT_NE(string::npos, trace.find("read=10B, err=Success")) << trace;
}

// The CDN closes in the middle of the segment the notify reads: the read stops at the error, which is logged with
// the bytes read, and the notify still succeeds, as a notify failure must not stop HLS.
VOID TEST(AppHttpHooksTest, HlsNotifyCdnClosesMidSegment_ReadStopsAndIgnored)
{
    srs_error_t err;

    HooksUtestHooks h;
    HttpUtestOneReplyServer cdn("HTTP/1.1 200 OK\r\nContent-Length: 100\r\n\r\n0123456789");
    HELPER_ASSERT_SUCCESS(cdn.start());

    MockLogForSignal log;
    ISrsLog *original = _srs_log;
    _srs_log = &log;
    err = h.hooks_.on_hls_notify(SrsContextId(), "http://" + cdn.server() + "/live/[ts_url]", &h.req_, "livestream-0.ts", 64);
    _srs_log = original;
    HELPER_EXPECT_SUCCESS(err);

    EXPECT_EQ(0, (int)cdn.request_.find("GET /live/livestream-0.ts HTTP/1.1\r\n")) << cdn.request_;
    string trace = utest_log_find(log.traces_, "on_hls_notify success");
    EXPECT_NE(string::npos, trace.find("read=10B")) << trace;
    EXPECT_NE(string::npos, trace.find("err=code=")) << trace;
}

// An operator leaves the schema out of the on_hls_notify url: each notify fails with the url parse error, so the
// async worker logs it, and nothing is fetched.
VOID TEST(AppHttpHooksTest, HlsNotifyUrlWithoutSchema_NotifyFails)
{
    srs_error_t err;

    HooksUtestHooks h;
    HttpUtestOneReplyServer cdn(mock_http_response(200, "0123456789"));
    HELPER_ASSERT_SUCCESS(cdn.start());

    err = h.hooks_.on_hls_notify(SrsContextId(), cdn.server() + "/live/[stream]/[ts_url]", &h.req_, "livestream-0.ts", 64);
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    EXPECT_EQ(ERROR_HTTP_PARSE_URI, srs_error_code(err));
    string desc = srs_error_desc(err);
    srs_freep(err);
    EXPECT_NE(string::npos, desc.find("http: init url=" + cdn.server() + "/live/livestream/livestream-0.ts")) << desc;
    EXPECT_TRUE(cdn.request_.empty()) << cdn.request_;
}

// An edge's player asks an origin of a cluster for a stream it does not have, so the origin asks its coworkers,
// through the server's own HTTP hooks, which origin has it, and redirects the player there.
class HooksUtestClusterOrigin
{
public:
    HooksUtestHooks h_;
    MockAppConfigForRtmpConnCluster config_;
    MockRtmpServerForRedirect *rtmp_;
    SrsUniquePtr<SrsRtmpConn> conn_;

public:
    HooksUtestClusterOrigin() : conn_(new SrsRtmpConn(new MockRtmpTransportForDoCycle(), "192.168.1.100", 1935))
    {
        conn_->config_ = &config_;
        conn_->assemble();
        conn_->hooks_ = &h_.hooks_;
        rtmp_ = new MockRtmpServerForRedirect();
        srs_freep(conn_->rtmp_);
        conn_->rtmp_ = rtmp_;

        ISrsRequest *req = conn_->info_->req_;
        req->vhost_ = "__defaultVhost__";
        req->host_ = "127.0.0.1";
        req->app_ = "live";
        req->stream_ = "livestream";
    }
    ~HooksUtestClusterOrigin()
    {
        conn_->hooks_ = NULL;
        conn_->config_ = NULL;
    }

public:
    srs_error_t redirect()
    {
        SrsSharedPtr<SrsLiveSource> source(new MockLiveSource());
        return conn_->redirect_to_origin_cluster(source);
    }
};

// The answer of an SRS coworker's cluster API, with the origin it found or null, as SrsGoApiClusters writes it.
static string hooks_utest_cluster_answer(string origin)
{
    string query = "{\"ip\":\"127.0.0.1\",\"vhost\":\"__defaultVhost__\",\"app\":\"live\",\"stream\":\"livestream\"}";
    return mock_http_response(200, "{\"code\":0,\"data\":{\"query\":" + query + ",\"origin\":" + origin + "}}");
}

// Of three coworkers, the first is down and the second does not have the stream, so it answers no origin: both are
// skipped, and the player is redirected to the origin the third names.
VOID TEST(AppHttpHooksTest, ClusterCoworkerDownOrWithoutStream_RedirectsToTheOneWithIt)
{
    srs_error_t err;

    HttpUtestOneReplyServer down("");
    HttpUtestOneReplyServer without(hooks_utest_cluster_answer("null"));
    HELPER_ASSERT_SUCCESS(without.start());
    HttpUtestOneReplyServer with(hooks_utest_cluster_answer("{\"ip\":\"10.0.0.3\",\"port\":19350,\"vhost\":\"__defaultVhost__\","
                                                            "\"api\":\"10.0.0.3:1985\",\"routers\":[\"10.0.0.3:1985\"]}"));
    HELPER_ASSERT_SUCCESS(with.start());

    HooksUtestClusterOrigin o;
    o.config_.coworkers_.push_back(down.server());
    o.config_.coworkers_.push_back(without.server());
    o.config_.coworkers_.push_back(with.server());

    err = o.redirect();
    EXPECT_EQ(ERROR_CONTROL_REDIRECT, srs_error_code(err));
    srs_freep(err);

    EXPECT_EQ(0, (int)without.request_.find("POST /api/v1/clusters?vhost=__defaultVhost__&ip=127.0.0.1&app=live&stream=livestream&coworker=" + without.server() + " HTTP/1.1\r\n")) << without.request_;
    EXPECT_EQ(0, (int)with.request_.find("POST /api/v1/clusters?")) << with.request_;
    ASSERT_EQ(1, (int)o.rtmp_->redirect_urls_.size());
    EXPECT_NE(string::npos, o.rtmp_->redirect_urls_[0].find("10.0.0.3:19350")) << o.rtmp_->redirect_urls_[0];
}

// An operator lists the HTTP hooks server as a coworker by mistake. It answers each discovery as a hook, with "0" or
// {"code":0}, which names no origin, so no coworker can be used and the player is refused with the discovery error.
VOID TEST(AppHttpHooksTest, ClusterCoworkerIsHookServer_NoOriginFound)
{
    srs_error_t err;

    HttpUtestOneReplyServer plain(mock_http_response(200, "0"));
    HELPER_ASSERT_SUCCESS(plain.start());
    HttpUtestOneReplyServer json(hooks_utest_ok());
    HELPER_ASSERT_SUCCESS(json.start());

    HooksUtestClusterOrigin o;
    o.config_.coworkers_.push_back(plain.server());
    o.config_.coworkers_.push_back(json.server());

    err = o.redirect();
    EXPECT_EQ(ERROR_OCLUSTER_DISCOVER, srs_error_code(err));
    string desc = srs_error_desc(err);
    srs_freep(err);
    EXPECT_NE(string::npos, desc.find("discover coworkers")) << desc;
    EXPECT_NE(string::npos, desc.find("parse data {\"code\":0}")) << desc;

    EXPECT_EQ(0, (int)plain.request_.find("POST /api/v1/clusters?")) << plain.request_;
    EXPECT_EQ(0, (int)json.request_.find("POST /api/v1/clusters?")) << json.request_;
    EXPECT_TRUE(o.rtmp_->redirect_urls_.empty());
}

// The HTTP file server SrsHttpStaticServer mounts at / for the default vhost, serving the files of a temporary dir with
// the vhost's config text. Its HLS sessions are counted by the server's stream registry, which the HTTP API lists and
// kicks clients from.
class StaticUtestVod
{
public:
    string dir_;
    MockSrsConfig conf_;
    HttpApiUtestServer api_;
    SrsHttpMuxEntry entry_;
    SrsVodStream *stream_;

public:
    StaticUtestVod()
    {
        dir_ = "/tmp/srs-utest-static-" + srs_strconv_format_int(getpid());
        hls_utest_remove_all(dir_);
        srs_mkdir(dir_.c_str());
        srs_mkdir((dir_ + "/live").c_str());

        entry_.pattern = "/";
        stream_ = new SrsVodStream(dir_);
        stream_->entry_ = &entry_;
        stream_->config_ = &conf_;
        stream_->hls_.config_ = &conf_;
        stream_->hls_.stat_ = &api_.stat_;
        stream_->hls_.shared_timer_ = NULL;

        SrsSecurity *security = new SrsSecurity();
        security->config_ = &conf_;
        srs_freep(stream_->hls_.security_);
        stream_->hls_.security_ = security;
    }
    virtual ~StaticUtestVod()
    {
        stream_->config_ = NULL;
        stream_->hls_.config_ = NULL;
        stream_->hls_.stat_ = NULL;
        srs_freep(stream_);
        hls_utest_remove_all(dir_);
    }

public:
    // Loads the config of the default vhost, by the text of its body.
    srs_error_t initialize(string vhost)
    {
        return conf_.mock_parse(string(_MIN_OK_CONF) + "vhost __defaultVhost__ { " + vhost + " }");
    }
    // Writes a file at the path under the HTTP dir, such as /live/livestream.m3u8.
    void write(string path, string content)
    {
        hls_utest_write_file(dir_ + path, content);
    }
    // A player asks for the url on a connection whose socket is io.
    srs_error_t get(string url, MockBufferIO *io, string range = "")
    {
        srs_error_t err = srs_success;

        SrsHttpMessage r(NULL, NULL);
        SrsHttpHeader header;
        header.set("Host", "127.0.0.1:8080");
        if (!range.empty()) {
            header.set("Range", range);
        }
        r.set_header(&header, false);
        if ((err = r.set_url(url, false)) != srs_success) {
            return srs_error_wrap(err, "url");
        }

        SrsHttpResponseWriter w(io);
        return stream_->serve_http(&w, &r);
    }
    // The HLS session the server keeps for a ctx, or NULL.
    SrsHlsVirtualConn *session(string ctx)
    {
        std::map<std::string, SrsHlsVirtualConn *>::iterator it = stream_->hls_.map_ctx_info_.find(ctx);
        return it == stream_->hls_.map_ctx_info_.end() ? NULL : it->second;
    }
};

// The session id (hls_ctx) a master playlist names, or "" when it names none.
static string static_utest_ctx(const string &m3u8)
{
    size_t pos = m3u8.find("hls_ctx=");
    if (pos == string::npos) {
        return "";
    }
    pos += 8;
    return m3u8.substr(pos, m3u8.find_first_of("&\n", pos) - pos);
}

// The value of a header of the HTTP reply in io, or "".
static string static_utest_header(MockBufferIO &io, string name)
{
    string reply = string(io.out_buffer.bytes(), io.out_buffer.length());
    string head = reply.substr(0, reply.find("\r\n\r\n"));
    size_t pos = head.find("\r\n" + name + ": ");
    if (pos == string::npos) {
        return "";
    }
    pos += name.length() + 4;
    return head.substr(pos, head.find("\r\n", pos) - pos);
}

// An FLV tag of the type, 8 audio, 9 video or 18 script, with its data and previous tag size.
static string static_utest_flv_tag(uint8_t type, const string &data)
{
    int size = (int)data.size();
    char h[11] = {(char)type, (char)(size >> 16), (char)(size >> 8), (char)size, 0, 0, 0, 0, 0, 0, 0};
    int prev = 11 + size;
    char p[4] = {(char)(prev >> 24), (char)(prev >> 16), (char)(prev >> 8), (char)prev};
    return string(h, 11) + data + string(p, 4);
}

// The FLV header and the first previous tag size, of a file with audio and video.
static string static_utest_flv_header()
{
    const char h[13] = {'F', 'L', 'V', 0x01, 0x05, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00};
    return string(h, 13);
}

static const char *static_utest_m3u8 = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:10\n#EXTINF:10.000,\nlivestream-0.ts\n";

// An operator keeps HLS sessions for the API and hooks, but turns hls_ts_ctx off so the segments are plain files a CDN
// can cache. The player gets the master playlist of its session, then each refresh of its playlist is the file on disk,
// with the segment URIs as written, and the viewer stays listed.
VOID TEST(AppHttpStaticTest, HlsTsCtxOff_SessionPlaylistServedAsFile)
{
    srs_error_t err;

    StaticUtestVod vod;
    HELPER_ASSERT_SUCCESS(vod.initialize("hls { enabled on; hls_ctx on; hls_ts_ctx off; }"));
    vod.write("/live/livestream.m3u8", static_utest_m3u8);

    MockBufferIO master;
    HELPER_ASSERT_SUCCESS(vod.get("/live/livestream.m3u8", &master));
    string ctx = static_utest_ctx(http_api_utest_body(master));
    ASSERT_EQ(8, (int)ctx.length()) << http_api_utest_body(master);

    for (int i = 0; i < 2; i++) {
        MockBufferIO media;
        HELPER_ASSERT_SUCCESS(vod.get("/live/livestream.m3u8?hls_ctx=" + ctx, &media));
        EXPECT_STREQ("HTTP/1.1 200 OK", http_api_utest_status(media).c_str());
        EXPECT_STREQ(static_utest_m3u8, http_api_utest_body(media).c_str());
        EXPECT_TRUE(vod.api_.stat_.find_client(ctx) != NULL);
    }
}

// With hls_ts_ctx off, a player that has played longer than two windows refreshes its playlist, served as a file. The
// refresh keeps its session alive, so the HLS timer keeps the session and the viewer in the API, and the next refresh
// gets the media playlist again, not a master playlist of a new session.
VOID TEST(AppHttpStaticTest, HlsTsCtxOffPlayerKeepsRefreshing_SessionKept)
{
    srs_error_t err;

    StaticUtestVod vod;
    HELPER_ASSERT_SUCCESS(vod.initialize("hls { enabled on; hls_window 10; hls_ts_ctx off; }"));
    vod.write("/live/livestream.m3u8", static_utest_m3u8);

    MockBufferIO master;
    HELPER_ASSERT_SUCCESS(vod.get("/live/livestream.m3u8", &master));
    string ctx = static_utest_ctx(http_api_utest_body(master));
    ASSERT_TRUE(vod.session(ctx) != NULL);

    // The player started three windows ago, and refreshes its playlist now.
    vod.session(ctx)->request_time_ -= 30 * SRS_UTIME_SECONDS;
    MockBufferIO media;
    HELPER_ASSERT_SUCCESS(vod.get("/live/livestream.m3u8?hls_ctx=" + ctx, &media));
    EXPECT_STREQ(static_utest_m3u8, http_api_utest_body(media).c_str());

    // The 5s timer keeps a session refreshed within two windows.
    HELPER_EXPECT_SUCCESS(vod.stream_->hls_.on_timer(5 * SRS_UTIME_SECONDS));
    EXPECT_TRUE(vod.session(ctx) != NULL);
    EXPECT_TRUE(vod.api_.stat_.find_client(ctx) != NULL);

    MockBufferIO next;
    HELPER_ASSERT_SUCCESS(vod.get("/live/livestream.m3u8?hls_ctx=" + ctx, &next));
    EXPECT_STREQ(static_utest_m3u8, http_api_utest_body(next).c_str());
}

// An operator kicks an HLS viewer with the HTTP API, by the session id the API lists. The viewer leaves the API at
// once, and its next playlist refresh is refused with 404, so the player stops instead of starting a new session.
VOID TEST(AppHttpStaticTest, HlsViewerKickedByApi_PlaylistRefreshNotFound)
{
    srs_error_t err;

    StaticUtestVod vod;
    HELPER_ASSERT_SUCCESS(vod.initialize("hls { enabled on; }"));
    vod.write("/live/livestream.m3u8", static_utest_m3u8);

    MockBufferIO master;
    HELPER_ASSERT_SUCCESS(vod.get("/live/livestream.m3u8", &master));
    string ctx = static_utest_ctx(http_api_utest_body(master));
    ASSERT_TRUE(vod.session(ctx) != NULL);
    ASSERT_TRUE(vod.api_.stat_.find_client(ctx) != NULL);
    // The server has one stream registry, which the session leaves when kicked.
    vod.session(ctx)->stat_ = &vod.api_.stat_;

    MockBufferIO kick;
    HELPER_ASSERT_SUCCESS(vod.api_.serve(HTTP_DELETE, "/api/v1/clients/" + ctx, &kick));
    EXPECT_EQ(0, http_api_utest_code(kick)) << http_api_utest_body(kick);
    EXPECT_TRUE(vod.api_.stat_.find_client(ctx) == NULL);

    for (int i = 0; i < 2; i++) {
        MockBufferIO media;
        HELPER_ASSERT_SUCCESS(vod.get("/live/livestream.m3u8?hls_ctx=" + ctx, &media));
        EXPECT_STREQ("HTTP/1.1 404 Not Found", http_api_utest_status(media).c_str());
        EXPECT_STREQ(("HLS stream " + ctx + " is EOF").c_str(), http_api_utest_body(media).c_str());
        EXPECT_TRUE(vod.api_.stat_.find_client(ctx) == NULL);
    }
}

// The same kick on a vhost with hls_ts_ctx off, where the playlist refresh of a session is served as a file: the
// kicked viewer's refresh is still refused with 404, so the kick stops the player as it does with hls_ts_ctx on.
VOID TEST(AppHttpStaticTest, HlsTsCtxOffViewerKickedByApi_PlaylistRefreshNotFound)
{
    srs_error_t err;

    StaticUtestVod vod;
    HELPER_ASSERT_SUCCESS(vod.initialize("hls { enabled on; hls_ts_ctx off; }"));
    vod.write("/live/livestream.m3u8", static_utest_m3u8);

    MockBufferIO master;
    HELPER_ASSERT_SUCCESS(vod.get("/live/livestream.m3u8", &master));
    string ctx = static_utest_ctx(http_api_utest_body(master));
    ASSERT_TRUE(vod.session(ctx) != NULL);
    vod.session(ctx)->stat_ = &vod.api_.stat_;

    MockBufferIO kick;
    HELPER_ASSERT_SUCCESS(vod.api_.serve(HTTP_DELETE, "/api/v1/clients/" + ctx, &kick));
    EXPECT_EQ(0, http_api_utest_code(kick)) << http_api_utest_body(kick);

    MockBufferIO media;
    HELPER_ASSERT_SUCCESS(vod.get("/live/livestream.m3u8?hls_ctx=" + ctx, &media));
    EXPECT_STREQ("HTTP/1.1 404 Not Found", http_api_utest_status(media).c_str());
    EXPECT_STREQ(("HLS stream " + ctx + " is EOF").c_str(), http_api_utest_body(media).c_str());
}

// A player with an auth token in its URL: the master playlist names the media playlist with the session id and the
// token, so each refresh carries the token, and the refreshed playlist names the segments with the session id.
VOID TEST(AppHttpStaticTest, HlsPlayerWithToken_MasterPlaylistKeepsToken)
{
    srs_error_t err;

    StaticUtestVod vod;
    HELPER_ASSERT_SUCCESS(vod.initialize("hls { enabled on; }"));
    vod.write("/live/livestream.m3u8", static_utest_m3u8);

    MockBufferIO master;
    HELPER_ASSERT_SUCCESS(vod.get("/live/livestream.m3u8?token=abc", &master));
    string ctx = static_utest_ctx(http_api_utest_body(master));
    ASSERT_EQ(8, (int)ctx.length()) << http_api_utest_body(master);
    string media_url = "/live/livestream.m3u8?hls_ctx=" + ctx + "&token=abc";
    EXPECT_STREQ(("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1,AVERAGE-BANDWIDTH=1\n" + media_url + "\n").c_str(),
                 http_api_utest_body(master).c_str());

    MockBufferIO media;
    HELPER_ASSERT_SUCCESS(vod.get(media_url, &media));
    EXPECT_NE(string::npos, http_api_utest_body(media).find("\nlivestream-0.ts?hls_ctx=" + ctx + "\n")) << http_api_utest_body(media);
    EXPECT_EQ(1, (int)vod.api_.stat_.clients_.size());
}

// An operator behind a reverse proxy that serves the server under another path, such as /hls/live/livestream.m3u8,
// turns on hls_master_m3u8_path_relative: the master playlist names the media playlist by its file name, which the
// player resolves under the proxy's path.
VOID TEST(AppHttpStaticTest, HlsMasterPathRelative_MediaPlaylistNamedByFileName)
{
    srs_error_t err;

    StaticUtestVod vod;
    HELPER_ASSERT_SUCCESS(vod.initialize("hls { enabled on; hls_master_m3u8_path_relative on; }"));
    vod.write("/live/livestream.m3u8", static_utest_m3u8);

    MockBufferIO master;
    HELPER_ASSERT_SUCCESS(vod.get("/live/livestream.m3u8", &master));
    string ctx = static_utest_ctx(http_api_utest_body(master));
    EXPECT_STREQ(("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1,AVERAGE-BANDWIDTH=1\nlivestream.m3u8?hls_ctx=" + ctx + "\n").c_str(),
                 http_api_utest_body(master).c_str());
}

// A player that closes its connection before it gets the master playlist, such as a page closed at once: the request
// fails at the write, the viewer is dropped from the API and no session is kept for it.
VOID TEST(AppHttpStaticTest, HlsViewerLeavesBeforeMasterPlaylist_NoSessionKept)
{
    srs_error_t err;

    for (int fail_at = 1; fail_at <= 2; fail_at++) {
        StaticUtestVod vod;
        HELPER_ASSERT_SUCCESS(vod.initialize("hls { enabled on; }"));
        vod.write("/live/livestream.m3u8", static_utest_m3u8);

        HttpApiUtestLeavingClientIO io(fail_at);
        err = vod.get("/live/livestream.m3u8", &io);
        EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err)) << fail_at;
        string desc = srs_error_desc(err);
        srs_freep(err);
        EXPECT_NE(string::npos, desc.find("write  bytes=")) << desc;

        EXPECT_TRUE(vod.stream_->hls_.map_ctx_info_.empty()) << fail_at;
        EXPECT_TRUE(vod.api_.stat_.clients_.empty()) << fail_at;
    }
}

// A VOD playlist made by another packager and served from the HTTP dir, whose segment URIs carry a query such as a
// cache-busting version: the playlist of each session puts the session id before that query, so a segment request
// carries both, for TS and for fMP4 segments.
VOID TEST(AppHttpStaticTest, HlsPlaylistWithSegmentQuery_SessionIdBeforeQuery)
{
    srs_error_t err;

    StaticUtestVod vod;
    HELPER_ASSERT_SUCCESS(vod.initialize("hls { enabled on; }"));
    vod.write("/live/movie.m3u8", "#EXTM3U\n#EXTINF:10.000,\nmovie-0.ts?v=2\n#EXTINF:10.000,\nmovie-1.ts?v=2\n#EXT-X-ENDLIST\n");
    vod.write("/live/show.m3u8", "#EXTM3U\n#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:10.000,\nshow-0.m4s?v=2\n#EXT-X-ENDLIST\n");

    MockBufferIO movie_master;
    HELPER_ASSERT_SUCCESS(vod.get("/live/movie.m3u8", &movie_master));
    string movie = static_utest_ctx(http_api_utest_body(movie_master));
    MockBufferIO movie_media;
    HELPER_ASSERT_SUCCESS(vod.get("/live/movie.m3u8?hls_ctx=" + movie, &movie_media));
    EXPECT_STREQ(("#EXTM3U\n#EXTINF:10.000,\nmovie-0.ts?hls_ctx=" + movie + "&v=2\n#EXTINF:10.000,\nmovie-1.ts?hls_ctx=" + movie + "&v=2\n#EXT-X-ENDLIST\n").c_str(),
                 http_api_utest_body(movie_media).c_str());

    MockBufferIO show_master;
    HELPER_ASSERT_SUCCESS(vod.get("/live/show.m3u8", &show_master));
    string show = static_utest_ctx(http_api_utest_body(show_master));
    MockBufferIO show_media;
    HELPER_ASSERT_SUCCESS(vod.get("/live/show.m3u8?hls_ctx=" + show, &show_media));
    EXPECT_NE(string::npos, http_api_utest_body(show_media).find("\nshow-0.m4s?hls_ctx=" + show + "&v=2\n")) << http_api_utest_body(show_media);
    EXPECT_NE(movie, show);
}

// A player seeks in an FLV recording that has no audio or video tag, such as a DVR file of a publish that sent only
// metadata: there is no sequence header to send first, so the request fails at the tags and nothing is sent.
VOID TEST(AppHttpStaticTest, FlvWithoutMediaSeeked_NothingServed)
{
    srs_error_t err;

    StaticUtestVod vod;
    HELPER_ASSERT_SUCCESS(vod.initialize(""));
    vod.write("/live/meta.flv", static_utest_flv_header() + static_utest_flv_tag(18, string(20, 'm')));

    MockBufferIO io;
    err = vod.get("/live/meta.flv?start=13", &io);
    EXPECT_EQ(ERROR_SYSTEM_FILE_EOF, srs_error_code(err));
    string desc = srs_error_desc(err);
    srs_freep(err);
    EXPECT_NE(string::npos, desc.find("ffd read sps")) << desc;
    EXPECT_EQ(0, io.out_buffer.length());
}

// A player seeks in an FLV recording: it gets the FLV header and the sequence headers, then the tags from the start
// it asked for. A player that seeks again closes this connection, so serving stops at the write that fails, whether
// the header, the sequence headers or the tags, and nothing more is sent.
VOID TEST(AppHttpStaticTest, FlvSeekViewerLeaves_ServingStopsAtFailedWrite)
{
    srs_error_t err;

    string sh = static_utest_flv_tag(8, string("\xaf\x00\x12\x10", 4)) + static_utest_flv_tag(9, string("\x17\x00\x00\x00\x00", 5));
    string frame0 = static_utest_flv_tag(9, string("\x17\x01\x00\x00\x00\x65\x88", 7));
    string frame1 = static_utest_flv_tag(9, string("\x27\x01\x00\x00\x00\x41\x9a", 7));
    string file = static_utest_flv_header() + sh + frame0 + frame1;
    int start = (int)(file.size() - frame1.size());

    StaticUtestVod vod;
    HELPER_ASSERT_SUCCESS(vod.initialize(""));
    vod.write("/live/record.flv", file);
    string url = "/live/record.flv?start=" + srs_strconv_format_int(start);

    MockBufferIO all;
    HELPER_ASSERT_SUCCESS(vod.get(url, &all));
    EXPECT_STREQ("HTTP/1.1 200 OK", http_api_utest_status(all).c_str());
    string body = static_utest_flv_header() + sh + frame1;
    EXPECT_STREQ(srs_strconv_format_int(body.size()).c_str(), static_utest_header(all, "Content-Length").c_str());
    EXPECT_TRUE(body == http_api_utest_body(all));

    const char *stages[] = {"write flv header", "write flv header", "write sequence", "read flv="};
    for (int fail_at = 1; fail_at <= 4; fail_at++) {
        HttpApiUtestLeavingClientIO io(fail_at);
        err = vod.get(url, &io);
        EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err)) << fail_at;
        string desc = srs_error_desc(err);
        srs_freep(err);
        EXPECT_NE(string::npos, desc.find(stages[fail_at - 1])) << fail_at << ": " << desc;
        EXPECT_EQ(fail_at, io.nb_writes_) << fail_at;
    }
}

// An FLV file that ends inside its sequence header tags, such as a recording cut short, asked for with a start: the
// sequence headers the tags claim are not all in the file, so the request is refused before any byte is sent, as for
// a file with no sequence header, and the player gets no bytes the file does not have.
VOID TEST(AppHttpStaticTest, FlvCutInsideSequenceHeaders_Refused)
{
    srs_error_t err;

    string audio = static_utest_flv_tag(8, string("\xaf\x00\x12\x10", 4));
    char video[11] = {9, 0x00, 0x00, 100, 0, 0, 0, 0, 0, 0, 0};
    string file = static_utest_flv_header() + audio + string(video, 11) + string(10, 'v');
    int start = (int)file.size() - 10;

    StaticUtestVod vod;
    HELPER_ASSERT_SUCCESS(vod.initialize(""));
    vod.write("/live/cut.flv", file);

    MockBufferIO io;
    err = vod.get("/live/cut.flv?start=" + srs_strconv_format_int(start), &io);
    EXPECT_EQ(ERROR_HTTP_REMUX_SEQUENCE_HEADER, srs_error_code(err));
    string desc = srs_error_desc(err);
    srs_freep(err);
    EXPECT_NE(string::npos, desc.find("past file end")) << desc;
    EXPECT_EQ(0, io.out_buffer.length());
}

// A player asks for a range of an MP4 recording, as it does to seek: it gets 206 with the bytes of the range. A player
// that seeks again closes this connection, so serving stops at the write that fails.
VOID TEST(AppHttpStaticTest, Mp4RangeViewerLeaves_ServingStops)
{
    srs_error_t err;

    string file;
    for (int i = 0; i < 100; i++) {
        file.push_back((char)('a' + i % 26));
    }

    StaticUtestVod vod;
    HELPER_ASSERT_SUCCESS(vod.initialize(""));
    vod.write("/live/record.mp4", file);

    MockBufferIO all;
    HELPER_ASSERT_SUCCESS(vod.get("/live/record.mp4", &all, "bytes=10-59"));
    EXPECT_STREQ("HTTP/1.1 206 Partial Content", http_api_utest_status(all).c_str());
    EXPECT_STREQ("bytes 10-59/100", static_utest_header(all, "Content-Range").c_str());
    EXPECT_STREQ(file.substr(10, 50).c_str(), http_api_utest_body(all).c_str());

    for (int fail_at = 1; fail_at <= 2; fail_at++) {
        HttpApiUtestLeavingClientIO io(fail_at);
        err = vod.get("/live/record.mp4", &io, "bytes=10-59");
        EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err)) << fail_at;
        string desc = srs_error_desc(err);
        srs_freep(err);
        EXPECT_NE(string::npos, desc.find("read mp4=")) << desc;
        EXPECT_EQ(fail_at, io.nb_writes_) << fail_at;
    }
}

// The media of an encoder as FLV tags: H.264 and AAC, or MP3 frames whose last byte tells them apart.
static const uint8_t hstream_avc_sh[] = {
    0x17, 0x00, 0x00, 0x00, 0x00, 0x01, 0x64, 0x00, 0x20, 0xff, 0xe1, 0x00, 0x19, 0x67, 0x64, 0x00, 0x20, 0xac,
    0xd9, 0x40, 0xc0, 0x29, 0xb0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00, 0x32, 0x0f, 0x18,
    0x31, 0x96, 0x01, 0x00, 0x05, 0x68, 0xeb, 0xec, 0xb2, 0x2c};
static const uint8_t hstream_avc_idr[] = {0x17, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x65, 0x88, 0x84, 0x00, 0x10};
static const uint8_t hstream_aac_sh[] = {0xaf, 0x00, 0x12, 0x10};
static const uint8_t hstream_aac_frame[] = {0xaf, 0x01, 0x21, 0x00};
static const uint8_t hstream_mp3_frame_a[] = {0x2f, 0xff, 0xfb, 0x90, 0x11};
static const uint8_t hstream_mp3_frame_b[] = {0x2f, 0xff, 0xfb, 0x90, 0x22};

// A message the encoder publishes: 'a' audio, 'v' video or 'm' its onMetaData, at a timestamp in ms.
struct HttpStreamUtestFrame {
    char type_;
    int64_t timestamp_;
    string data_;
};
typedef vector<HttpStreamUtestFrame> HttpStreamUtestBatch;

static HttpStreamUtestFrame hstream_frame(char type, int64_t timestamp, const uint8_t *data, int size)
{
    HttpStreamUtestFrame f;
    f.type_ = type;
    f.timestamp_ = timestamp;
    f.data_ = string((const char *)data, size);
    return f;
}
#define HSTREAM_FRAME(type, timestamp, data) hstream_frame(type, timestamp, data, (int)sizeof(data))

// The default vhost remuxing its RTMP streams to HTTP by the given http_remux options, with mw_latency 0, as a
// low-latency config sets, so an HTTP player waits 10ms between its polls of the stream.
#define HSTREAM_VHOST(remux) "vhost __defaultVhost__ { play { mw_latency 0; } http_remux { enabled on; " remux " } } "

// The request of an RTMP encoder that publishes a stream, as the RTMP connection parses it.
class HttpStreamUtestRequest : public SrsRequest
{
public:
    HttpStreamUtestRequest(string vhost, string app, string stream)
    {
        vhost_ = vhost;
        app_ = app;
        stream_ = stream;
        host_ = "127.0.0.1";
        port_ = 1935;
        schema_ = "rtmp";
        tcUrl_ = "rtmp://127.0.0.1/" + app;
    }
};

// The HTTP server of an SRS that remuxes its RTMP streams to HTTP, made as SrsServer makes it, with the config loaded
// from the test's text, and its own live sources and stream API registry for the scope of a test. An encoder publishes
// and unpublishes through its live source, which asks the server to mount and unmount its HTTP stream, as SrsServer
// does, and its messages go into the live source.
class HttpStreamUtestServer : public ISrsLiveSourceHandler
{
public:
    MockSrsConfig conf_;
    SrsStatistic stat_;
    SrsLiveSourceManager sources_;
    SrsHttpServer *server_;
    HttpStreamUtestRequest req_;

private:
    SrsConfig *saved_config_;
    SrsLiveSourceManager *saved_sources_;
    SrsStatistic *saved_stat_;

public:
    HttpStreamUtestServer() : req_("__defaultVhost__", "live", "livestream")
    {
        server_ = NULL;
        stat_.config_ = &conf_;
        stat_.server_id_ = "vid-utest-server";

        saved_config_ = _srs_config;
        saved_sources_ = _srs_sources;
        saved_stat_ = _srs_stat;
        _srs_config = &conf_;
        _srs_sources = &sources_;
        _srs_stat = &stat_;
    }
    virtual ~HttpStreamUtestServer()
    {
        srs_freep(server_);
        _srs_config = saved_config_;
        _srs_sources = saved_sources_;
        _srs_stat = saved_stat_;
    }

public:
    srs_error_t start(string vhosts)
    {
        srs_error_t err = srs_success;
        if ((err = conf_.mock_parse(string(_MIN_OK_CONF) + vhosts)) != srs_success) {
            return srs_error_wrap(err, "config");
        }

        server_ = new SrsHttpServer();
        server_->assemble();
        return server_->initialize();
    }
    SrsHttpStreamServer *streams()
    {
        return dynamic_cast<SrsHttpStreamServer *>(server_->http_stream_);
    }
    // The encoder of the request publishes, by default the encoder of req_.
    srs_error_t publish(ISrsRequest *r = NULL)
    {
        return source(r)->on_publish();
    }
    // The encoder unpublishes, or a refused publish is released, as the RTMP connection releases it. The teardown of
    // the HTTP stream starts.
    void unpublish(ISrsRequest *r = NULL)
    {
        source(r)->on_unpublish();
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }
    // The server mounts the HTTP stream of a publish, as the source's on_publish asks it.
    srs_error_t mount(ISrsRequest *r)
    {
        return server_->http_mount(r);
    }
    // The server unmounts an HTTP stream, as the source's on_unpublish asks it, or a stream that its player mounted, as
    // on an edge, and the teardown starts.
    void unmount(ISrsRequest *r)
    {
        server_->http_unmount(r);
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }
    SrsSharedPtr<SrsLiveSource> source(ISrsRequest *r = NULL)
    {
        SrsSharedPtr<SrsLiveSource> source;
        srs_error_t err = sources_.fetch_or_create(r ? r : &req_, source);
        EXPECT_TRUE(err == srs_success) << srs_error_desc(err);
        srs_freep(err);
        source->handler_ = this;
        return source;
    }
    // Interface ISrsLiveSourceHandler, as SrsServer.
    virtual srs_error_t on_publish(ISrsRequest *r)
    {
        return server_->http_mount(r);
    }
    virtual void on_unpublish(ISrsRequest *r)
    {
        // A source still publishing when the test ends unpublishes as it is freed, after the server.
        if (server_) {
            server_->http_unmount(r);
        }
    }
    // The encoder's messages, in order, as the RTMP connection passes them to the live source.
    srs_error_t send(const HttpStreamUtestBatch &batch)
    {
        srs_error_t err = srs_success;

        SrsSharedPtr<SrsLiveSource> s = source();
        for (int i = 0; i < (int)batch.size(); i++) {
            const HttpStreamUtestFrame &f = batch.at(i);

            SrsRtmpCommonMessage msg;
            if (f.type_ == 'm') {
                SrsOnMetaDataPacket meta;
                meta.metadata_->set("width", SrsAmf0Any::number(768));
                meta.metadata_->set("height", SrsAmf0Any::number(320));
                msg.header_.initialize_amf0_script(0, 1);
                msg.header_.timestamp_ = f.timestamp_;
                err = s->on_meta_data(&msg, &meta);
            } else {
                int size = (int)f.data_.size();
                if (f.type_ == 'a') {
                    msg.header_.initialize_audio(size, (uint32_t)f.timestamp_, 1);
                } else {
                    msg.header_.initialize_video(size, (uint32_t)f.timestamp_, 1);
                }
                msg.create_payload(size);
                memcpy(msg.payload(), f.data_.data(), size);
                err = (f.type_ == 'a') ? s->on_audio(&msg) : s->on_video(&msg);
            }

            if (err != srs_success) {
                return srs_error_wrap(err, "message %d", i);
            }
        }

        return err;
    }
    // Finds the handler of a GET of the url, as the server does for each HTTP request before the static files.
    srs_error_t match(string url, ISrsHttpHandler **ph)
    {
        srs_error_t err = srs_success;

        SrsHttpMessage r(NULL, NULL);
        r.set_basic(HTTP_REQUEST, HTTP_GET, (llhttp_status_t)200, -1);
        if ((err = r.set_url(url, false)) != srs_success) {
            return srs_error_wrap(err, "url");
        }

        *ph = NULL;
        return streams()->mux()->find_handler(&r, ph);
    }
    // The live stream handler mounted for the stream url, or NULL.
    ISrsHttpHandler *mounted(string stream_url)
    {
        std::map<std::string, SrsLiveEntry *>::iterator it = streams()->streamHandlers_.find(stream_url);
        return (it == streams()->streamHandlers_.end()) ? NULL : it->second->stream_;
    }
};

// The coroutine of an HTTP player's connection, with the context id the server gives it, so the stream API lists the
// player by the id its connection ends with.
class HttpStreamUtestCoroutine : public HttpConnUtestCoroutine
{
public:
    SrsContextId cid_;

public:
    HttpStreamUtestCoroutine()
    {
        cid_ = _srs_context->generate_id();
    }
    virtual const SrsContextId &cid()
    {
        return cid_;
    }
};

// The socket of an HTTP player: it holds the request the player sent. Once the server has read it, the player sends
// nothing more, so each read of the server's recv thread waits, as for its 3s timeout, while the encoder publishes the
// next of its batches. When no batch is left, the player closes the connection.
class HttpStreamUtestPlayerSocket : public MockBufferIO
{
public:
    HttpStreamUtestServer *server_;
    vector<HttpStreamUtestBatch> batches_;
    int nb_waits_;

public:
    HttpStreamUtestPlayerSocket(HttpStreamUtestServer *server)
    {
        server_ = server;
        nb_waits_ = 0;
    }
    virtual ~HttpStreamUtestPlayerSocket()
    {
    }

public:
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread)
    {
        if (in_buffer.length() > 0) {
            return MockBufferIO::read(buf, size, nread);
        }

        if (!on_wait(nb_waits_++)) {
            return srs_error_new(ERROR_SOCKET_READ, "player closed");
        }

        srs_usleep(50 * SRS_UTIME_MILLISECONDS);
        return srs_error_new(ERROR_SOCKET_TIMEOUT, "timeout");
    }
    // The player waits for the n-th time, from 0. Returns false when the player closes its connection instead.
    virtual bool on_wait(int n)
    {
        if (n >= (int)batches_.size()) {
            return false;
        }

        srs_error_t err = server_->send(batches_.at(n));
        EXPECT_TRUE(err == srs_success) << srs_error_desc(err);
        srs_freep(err);
        return true;
    }
};

// An HTTP player of the server: a client of the HTTP server listener that asks for one path, served by the server's
// HTTP connection and its owner.
class HttpStreamUtestPlayer
{
public:
    MockResourceManagerForHttpxConn manager_;
    HttpStreamUtestPlayerSocket *io_;
    HttpStreamUtestCoroutine *trd_;
    SrsHttpxConn *connx_;
    SrsHttpConn *conn_;

public:
    HttpStreamUtestPlayer(HttpStreamUtestServer *server, string path, HttpStreamUtestPlayerSocket *io = NULL)
    {
        io_ = io ? io : new HttpStreamUtestPlayerSocket(server);
        io_->append("GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1:8080\r\n\r\n");

        connx_ = new SrsHttpxConn(&manager_, io_, server->server_, "127.0.0.1", 50000, "", "");
        conn_ = new SrsHttpConn(connx_, io_, server->server_, "127.0.0.1", 50000);
        trd_ = new HttpStreamUtestCoroutine();
        conn_->trd_ = trd_;
        connx_->conn_ = conn_;
    }
    virtual ~HttpStreamUtestPlayer()
    {
        srs_freep(connx_);
    }

public:
    // The player connects and plays until it closes or the server ends its connection. Returns what it was sent.
    string play()
    {
        srs_error_t err = connx_->start();
        EXPECT_TRUE(err == srs_success) << srs_error_desc(err);
        srs_freep(err);

        // The connection's coroutine runs with the connection's context id, as the server's does.
        SrsContextId saved = _srs_context->get_id();
        _srs_context->set_id(trd_->cid_);
        err = conn_->cycle();
        _srs_context->set_id(saved);
        srs_freep(err);

        return string(io_->out_buffer.bytes(), io_->out_buffer.length());
    }
};

// The status line of the reply to an HTTP player, or empty if it got none.
static string hstream_status(const string &reply)
{
    return reply.substr(0, reply.find("\r\n"));
}

// The body of the chunked reply to an HTTP player.
static string hstream_body(const string &reply)
{
    size_t pos = reply.find("\r\n\r\n");
    if (pos == string::npos) {
        return "";
    }

    string body;
    for (pos += 4; pos < reply.size();) {
        size_t eol = reply.find("\r\n", pos);
        if (eol == string::npos) {
            break;
        }
        int size = (int)strtol(reply.substr(pos, eol - pos).c_str(), NULL, 16);
        if (size <= 0) {
            break;
        }
        body += reply.substr(eol + 2, size);
        pos = eol + 2 + size + 2;
    }
    return body;
}

// The tag types of an HTTP-FLV body in order, after its FLV header: A audio, V video, S script.
static string hstream_flv_tags(const string &flv)
{
    string types;
    for (size_t pos = 9 + 4; pos + 11 <= flv.size();) {
        uint8_t type = (uint8_t)flv.at(pos);
        int size = ((uint8_t)flv.at(pos + 1) << 16) | ((uint8_t)flv.at(pos + 2) << 8) | (uint8_t)flv.at(pos + 3);
        types.push_back(type == 8 ? 'A' : (type == 9 ? 'V' : 'S'));
        pos += 11 + size + 4;
    }
    return types;
}

// An H.264 and AAC stream with a GOP in the cache: the sequence headers, a keyframe and an audio frame.
static HttpStreamUtestBatch hstream_gop()
{
    HttpStreamUtestBatch gop;
    gop.push_back(HSTREAM_FRAME('v', 0, hstream_avc_sh));
    gop.push_back(HSTREAM_FRAME('a', 0, hstream_aac_sh));
    gop.push_back(HSTREAM_FRAME('v', 0, hstream_avc_idr));
    gop.push_back(HSTREAM_FRAME('a', 23, hstream_aac_frame));
    return gop;
}

// An HTTP-TS player joins a stream with a GOP in the cache. It gets the GOP at once as a transport stream: TS packets
// of 188 bytes, the PAT first, then the PMT and the video and audio. When it leaves, the stream API drops it and the
// source has no consumer left for it.
VOID TEST(AppHttpStreamTest, TsPlayerJoinsStream_GetsGopAsTransportStream)
{
    srs_error_t err;

    HttpStreamUtestServer server;
    HELPER_ASSERT_SUCCESS(server.start(HSTREAM_VHOST("mount [vhost]/[app]/[stream].ts;")));
    HELPER_ASSERT_SUCCESS(server.publish());
    HELPER_ASSERT_SUCCESS(server.send(hstream_gop()));

    if (true) {
        HttpStreamUtestPlayer player(&server, "/live/livestream.ts");
        string reply = player.play();
        EXPECT_STREQ("HTTP/1.1 200 OK", hstream_status(reply).c_str());
        EXPECT_NE(string::npos, reply.find("Content-Type: video/MP2T\r\n")) << reply;

        string ts = hstream_body(reply);
        ASSERT_GT((int)ts.size(), 0);
        EXPECT_EQ(0, (int)ts.size() % 188);

        vector<int> pids;
        for (size_t i = 0; i + 188 <= ts.size(); i += 188) {
            EXPECT_EQ(0x47, (uint8_t)ts.at(i)) << i;
            pids.push_back((((uint8_t)ts.at(i + 1) & 0x1f) << 8) | (uint8_t)ts.at(i + 2));
        }
        ASSERT_GE((int)pids.size(), 4);
        EXPECT_EQ(0, pids.at(0));
        std::set<int> unique(pids.begin(), pids.end());
        EXPECT_GE((int)unique.size(), 4);

        EXPECT_TRUE(player.manager_.remove_called_);
    }

    EXPECT_TRUE(server.stat_.clients_.empty());
    EXPECT_TRUE(server.source()->consumers_.empty());
    server.unpublish();
}

// An HTTP .aac player of an audio stream on a vhost with fast_cache: the stream's cache plays it from the publish on,
// so a player that joins later gets the last seconds of audio at once, as ADTS frames, not only the audio after it
// joined.
VOID TEST(AppHttpStreamTest, AacPlayerWithFastCache_GetsRecentAudioAtJoin)
{
    srs_error_t err;

    HttpStreamUtestServer server;
    HELPER_ASSERT_SUCCESS(server.start(HSTREAM_VHOST("fast_cache 30; mount [vhost]/[app]/[stream].aac;")));
    HELPER_ASSERT_SUCCESS(server.publish());

    // The cache plays the stream once its coroutine runs.
    srs_usleep(10 * SRS_UTIME_MILLISECONDS);

    HttpStreamUtestBatch audio;
    audio.push_back(HSTREAM_FRAME('a', 0, hstream_aac_sh));
    audio.push_back(HSTREAM_FRAME('a', 0, hstream_aac_frame));
    audio.push_back(HSTREAM_FRAME('a', 23, hstream_aac_frame));
    audio.push_back(HSTREAM_FRAME('a', 46, hstream_aac_frame));
    HELPER_ASSERT_SUCCESS(server.send(audio));

    // The cache takes the audio at its next poll of the stream, every 500ms.
    srs_usleep(600 * SRS_UTIME_MILLISECONDS);

    if (true) {
        HttpStreamUtestPlayer player(&server, "/live/livestream.aac");
        string reply = player.play();
        EXPECT_STREQ("HTTP/1.1 200 OK", hstream_status(reply).c_str());
        EXPECT_NE(string::npos, reply.find("Content-Type: audio/x-aac\r\n")) << reply;

        // Three ADTS frames, each the 7-byte header and the 2 raw bytes.
        string aac = hstream_body(reply);
        ASSERT_EQ(27, (int)aac.size());
        for (int i = 0; i < 27; i += 9) {
            EXPECT_EQ(0xff, (uint8_t)aac.at(i)) << i;
            EXPECT_EQ(0xf0, (uint8_t)aac.at(i + 1) & 0xf0) << i;
            EXPECT_EQ(string("\x21\x00", 2), aac.substr(i + 7, 2)) << i;
        }
    }

    // The teardown stops the cache.
    server.unpublish();
    EXPECT_TRUE(server.streams()->streamHandlers_.empty());
    EXPECT_TRUE(server.source()->consumers_.empty());
}

// An HTTP .mp3 player of an MP3 stream on a vhost without fast_cache gets the ID3 header, then only the audio published
// after it joined: an MP3 stream has no sequence header, and audio has no GOP cache.
VOID TEST(AppHttpStreamTest, Mp3PlayerWithoutFastCache_GetsOnlyAudioAfterJoin)
{
    srs_error_t err;

    HttpStreamUtestServer server;
    HELPER_ASSERT_SUCCESS(server.start(HSTREAM_VHOST("mount [vhost]/[app]/[stream].mp3;")));
    HELPER_ASSERT_SUCCESS(server.publish());

    HttpStreamUtestBatch before;
    before.push_back(HSTREAM_FRAME('a', 0, hstream_mp3_frame_a));
    HELPER_ASSERT_SUCCESS(server.send(before));

    HttpStreamUtestPlayerSocket *io = new HttpStreamUtestPlayerSocket(&server);
    HttpStreamUtestBatch after;
    after.push_back(HSTREAM_FRAME('a', 26, hstream_mp3_frame_b));
    io->batches_.push_back(after);

    HttpStreamUtestPlayer player(&server, "/live/livestream.mp3", io);
    string reply = player.play();
    EXPECT_STREQ("HTTP/1.1 200 OK", hstream_status(reply).c_str());
    EXPECT_NE(string::npos, reply.find("Content-Type: audio/mpeg\r\n")) << reply;

    // The 20-byte ID3 header, then the frame without its FLV tag byte.
    string mp3 = hstream_body(reply);
    ASSERT_EQ(24, (int)mp3.size());
    EXPECT_EQ("ID3", mp3.substr(0, 3));
    EXPECT_EQ(string("\xff\xfb\x90\x22", 4), mp3.substr(20));

    server.unpublish();
}

// A player of each format closes its connection before the server writes it anything: the write of the FLV header, the
// ID3 header, the first TS packets or the first ADTS frame fails. The server ends the session at that write: nothing
// is sent, the stream API drops the player, and the source has no consumer left for it.
VOID TEST(AppHttpStreamTest, PlayerGoneAtFirstWrite_SessionEnds)
{
    srs_error_t err;

    const char *exts[] = {"flv", "mp3", "ts", "aac"};
    for (int i = 0; i < 4; i++) {
        string ext = exts[i];

        HttpStreamUtestServer server;
        HELPER_ASSERT_SUCCESS(server.start(HSTREAM_VHOST("mount [vhost]/[app]/[stream]." + ext + ";")));
        HELPER_ASSERT_SUCCESS(server.publish());
        if (ext == "mp3") {
            HttpStreamUtestBatch audio;
            audio.push_back(HSTREAM_FRAME('a', 0, hstream_mp3_frame_a));
            HELPER_ASSERT_SUCCESS(server.send(audio));
        } else {
            HELPER_ASSERT_SUCCESS(server.send(hstream_gop()));
        }

        // An .aac player gets no GOP, only the audio after it joined.
        HttpStreamUtestPlayerSocket *io = new HttpStreamUtestPlayerSocket(&server);
        io->out_err = srs_error_new(ERROR_SOCKET_WRITE, "peer reset");
        HttpStreamUtestBatch audio;
        audio.push_back(HSTREAM_FRAME('a', 46, hstream_aac_frame));
        io->batches_.push_back(audio);

        if (true) {
            HttpStreamUtestPlayer player(&server, "/live/livestream." + ext, io);
            string reply = player.play();
            EXPECT_TRUE(reply.empty()) << ext;
            EXPECT_TRUE(player.manager_.remove_called_) << ext;
        }

        EXPECT_TRUE(server.stat_.clients_.empty()) << ext;
        EXPECT_TRUE(server.source()->consumers_.empty()) << ext;
        server.unpublish();
    }
}

// An HTTP-FLV player waits for a stream before its encoder starts. The encoder sends its onMetaData first, then the
// sequence headers and audio frames before its first video frame. The FLV header waits for the first batch with media,
// and the player guesses the stream from it (https://github.com/ossrs/srs/issues/939): audio frames and no video frame
// is an audio-only stream, so the header flags audio only, and the video tags are dropped as they do not match it.
VOID TEST(AppHttpStreamTest, FlvPlayerBeforeEncoder_HeaderGuessedFromFirstMedia)
{
    srs_error_t err;

    HttpStreamUtestServer server;
    HELPER_ASSERT_SUCCESS(server.start(HSTREAM_VHOST("mount [vhost]/[app]/[stream].flv;")));
    HELPER_ASSERT_SUCCESS(server.publish());

    HttpStreamUtestPlayerSocket *io = new HttpStreamUtestPlayerSocket(&server);
    HttpStreamUtestBatch metadata;
    metadata.push_back(hstream_frame('m', 0, NULL, 0));
    io->batches_.push_back(metadata);

    HttpStreamUtestBatch media;
    media.push_back(HSTREAM_FRAME('v', 0, hstream_avc_sh));
    media.push_back(HSTREAM_FRAME('a', 0, hstream_aac_sh));
    media.push_back(HSTREAM_FRAME('a', 0, hstream_aac_frame));
    media.push_back(HSTREAM_FRAME('a', 23, hstream_aac_frame));
    io->batches_.push_back(media);

    HttpStreamUtestPlayer player(&server, "/live/livestream.flv", io);
    string reply = player.play();
    EXPECT_STREQ("HTTP/1.1 200 OK", hstream_status(reply).c_str());

    string flv = hstream_body(reply);
    ASSERT_GE((int)flv.size(), 13);
    EXPECT_EQ("FLV", flv.substr(0, 3));
    EXPECT_EQ(0x04, (uint8_t)flv.at(4));

    // The audio sequence header and frames follow the header; the video sequence header is dropped.
    string tags = hstream_flv_tags(flv);
    ASSERT_GE((int)tags.size(), 3) << tags;
    EXPECT_EQ("AAA", tags.substr(tags.size() - 3)) << tags;
    EXPECT_EQ(string::npos, tags.find('V')) << tags;

    server.unpublish();
}

// The first HTTP player of a stream that is not mounted yet, as on an edge, is matched by the vhost's mount template,
// which mounts the stream. A request the template does not match is left to the static files: no extension, a vhost
// that is disabled or has no http_remux, another extension than the mount's, a path shorter than the mount, or a longer
// one when the mount has no [app]. A mounted stream is then matched by its own mount.
VOID TEST(AppHttpStreamTest, PlayerOfUnmountedStream_MatchedByMountTemplate)
{
    srs_error_t err;

    HttpStreamUtestServer server;
    HELPER_ASSERT_SUCCESS(server.start(
        "vhost __defaultVhost__ { http_remux { enabled on; mount [vhost]/[app]/[stream].flv; } } "
        "vhost ts.vhost { http_remux { enabled on; mount [vhost]/[app]/[stream].ts; } } "
        "vhost mp3.vhost { http_remux { enabled on; mount [vhost]/[app]/[stream].mp3; } } "
        "vhost aac.vhost { http_remux { enabled on; mount [vhost]/[app]/[stream].aac; } } "
        "vhost off.vhost { enabled off; http_remux { enabled on; mount [vhost]/[app]/[stream].flv; } } "
        "vhost plain.vhost { } "
        "vhost one.vhost { http_remux { enabled on; mount [vhost]/live/[stream].flv; } } "));

    const char *unmatched[] = {
        "http://127.0.0.1/live/livestream",
        "http://off.vhost/live/livestream.flv",
        "http://plain.vhost/live/livestream.flv",
        "http://127.0.0.1/live/livestream.ts",
        "http://ts.vhost/live/livestream.flv",
        "http://mp3.vhost/live/livestream.flv",
        "http://aac.vhost/live/livestream.flv",
        "http://127.0.0.1/livestream.flv",
        "http://one.vhost/live/sub/livestream.flv",
    };
    for (int i = 0; i < (int)(sizeof(unmatched) / sizeof(unmatched[0])); i++) {
        ISrsHttpHandler *h = NULL;
        HELPER_ASSERT_SUCCESS(server.match(unmatched[i], &h));
        EXPECT_TRUE(h->is_not_found()) << unmatched[i];
    }
    EXPECT_TRUE(server.streams()->streamHandlers_.empty());

    HttpStreamUtestRequest flv("__defaultVhost__", "live", "livestream");
    HttpStreamUtestRequest ts("ts.vhost", "live", "livestream");
    HttpStreamUtestRequest one("one.vhost", "live", "livestream");

    // The vhosts' mounts first, for a mount of the default vhost has no host, so it matches the path on any vhost.
    ISrsHttpHandler *h = NULL;
    HELPER_ASSERT_SUCCESS(server.match("http://ts.vhost/live/livestream.ts", &h));
    EXPECT_TRUE(h != NULL && h == server.mounted(ts.get_stream_url()));
    HELPER_ASSERT_SUCCESS(server.match("http://one.vhost/live/livestream.flv", &h));
    EXPECT_TRUE(h != NULL && h == server.mounted(one.get_stream_url()));
    HELPER_ASSERT_SUCCESS(server.match("http://127.0.0.1/live/livestream.flv", &h));
    EXPECT_TRUE(h != NULL && h == server.mounted(flv.get_stream_url()));
    EXPECT_EQ(3, (int)server.streams()->streamHandlers_.size());

    // The next player of the stream is matched by the stream's own mount, with no new mount.
    ISrsHttpHandler *again = NULL;
    HELPER_ASSERT_SUCCESS(server.match("http://127.0.0.1/live/livestream.flv", &again));
    EXPECT_TRUE(again == server.mounted(flv.get_stream_url()));
    EXPECT_EQ(3, (int)server.streams()->streamHandlers_.size());

    server.unmount(&flv);
    server.unmount(&ts);
    server.unmount(&one);
    EXPECT_TRUE(server.streams()->streamHandlers_.empty());
}

// The socket of an HTTP-FLV player that still plays when its encoder unpublishes: at its first wait the encoder
// unpublishes, and the teardown of the stream, which waits for this player to leave, kicks it. Meanwhile, new players
// ask for the stream, before and after the operator disables http_remux and reloads.
class HttpStreamUtestTeardownPlayerSocket : public HttpStreamUtestPlayerSocket
{
public:
    srs_error_t new_player_err_;
    srs_error_t after_reload_err_;

public:
    HttpStreamUtestTeardownPlayerSocket(HttpStreamUtestServer *server) : HttpStreamUtestPlayerSocket(server)
    {
        new_player_err_ = srs_success;
        after_reload_err_ = srs_success;
    }
    virtual ~HttpStreamUtestTeardownPlayerSocket()
    {
        srs_freep(new_player_err_);
        srs_freep(after_reload_err_);
    }

public:
    virtual bool on_wait(int n)
    {
        if (n > 0) {
            return false;
        }

        server_->unpublish();

        ISrsHttpHandler *h = NULL;
        new_player_err_ = server_->match("http://127.0.0.1/live/livestream.flv", &h);

        srs_error_t err = server_->conf_.mock_parse(string(_MIN_OK_CONF) + "vhost __defaultVhost__ { http_remux { enabled off; } }");
        EXPECT_TRUE(err == srs_success) << srs_error_desc(err);
        srs_freep(err);
        after_reload_err_ = server_->match("http://127.0.0.1/live/livestream.flv", &h);
        return true;
    }
};

// An encoder unpublishes while an HTTP-FLV player still plays its stream. The teardown kicks the player and waits for
// it to leave. A new player of the stream meanwhile is refused, as the stream is disposing; after the operator disables
// http_remux and reloads, it is refused as disabled. Once the player left, the stream is unmounted.
VOID TEST(AppHttpStreamTest, NewPlayerDuringTeardown_Refused)
{
    srs_error_t err;

    HttpStreamUtestServer server;
    HELPER_ASSERT_SUCCESS(server.start(HSTREAM_VHOST("mount [vhost]/[app]/[stream].flv;")));
    HELPER_ASSERT_SUCCESS(server.publish());
    HELPER_ASSERT_SUCCESS(server.send(hstream_gop()));

    HttpStreamUtestTeardownPlayerSocket *io = new HttpStreamUtestTeardownPlayerSocket(&server);
    if (true) {
        HttpStreamUtestPlayer player(&server, "/live/livestream.flv", io);
        string reply = player.play();
        EXPECT_STREQ("HTTP/1.1 200 OK", hstream_status(reply).c_str());
        EXPECT_EQ("FLV", hstream_body(reply).substr(0, 3));

        EXPECT_EQ(ERROR_STREAM_DISPOSING, srs_error_code(io->new_player_err_)) << srs_error_desc(io->new_player_err_);
        EXPECT_EQ(ERROR_HTTP_DYNAMIC_MATCH, srs_error_code(io->after_reload_err_)) << srs_error_desc(io->after_reload_err_);
    }

    // The teardown checks every 100ms whether its player left.
    srs_usleep(150 * SRS_UTIME_MILLISECONDS);
    EXPECT_TRUE(server.streams()->streamHandlers_.empty());
}

// The socket of an HTTP-FLV player that still plays when its encoder unpublishes: at its first wait the encoder
// unpublishes, then publishes again at once, which is refused while the teardown waits for this player, and the refused
// publish is released, as the RTMP connection releases it.
class HttpStreamUtestRepublishPlayerSocket : public HttpStreamUtestPlayerSocket
{
public:
    srs_error_t republish_err_;
    int nb_teardowns_;

public:
    HttpStreamUtestRepublishPlayerSocket(HttpStreamUtestServer *server) : HttpStreamUtestPlayerSocket(server)
    {
        republish_err_ = srs_success;
        nb_teardowns_ = 0;
    }
    virtual ~HttpStreamUtestRepublishPlayerSocket()
    {
        srs_freep(republish_err_);
    }

public:
    virtual bool on_wait(int n)
    {
        if (n > 0) {
            return false;
        }

        server_->unpublish();
        republish_err_ = server_->publish();
        server_->unpublish();
        nb_teardowns_ = (int)server_->streams()->destroyers_.size();
        return true;
    }
};

// An encoder reconnects while an HTTP-FLV player of its stream has not left yet. The publish is refused while the
// teardown of the stream waits for the player, and the release of the refused publish starts no second teardown. Once
// the player left, the encoder publishes again; a later unpublish frees the teardowns that are done.
VOID TEST(AppHttpStreamTest, EncoderRepublishesDuringTeardown_RefusedThenPublishesAgain)
{
    srs_error_t err;

    HttpStreamUtestServer server;
    HELPER_ASSERT_SUCCESS(server.start(HSTREAM_VHOST("mount [vhost]/[app]/[stream].flv;")));
    HELPER_ASSERT_SUCCESS(server.publish());
    HELPER_ASSERT_SUCCESS(server.send(hstream_gop()));

    HttpStreamUtestRepublishPlayerSocket *io = new HttpStreamUtestRepublishPlayerSocket(&server);
    if (true) {
        HttpStreamUtestPlayer player(&server, "/live/livestream.flv", io);
        player.play();
        EXPECT_EQ(ERROR_STREAM_DISPOSING, srs_error_code(io->republish_err_)) << srs_error_desc(io->republish_err_);
        EXPECT_EQ(1, io->nb_teardowns_);
    }

    srs_usleep(150 * SRS_UTIME_MILLISECONDS);
    EXPECT_TRUE(server.streams()->streamHandlers_.empty());

    // The encoder publishes again, and unpublishes: the teardown that is done is freed.
    HELPER_EXPECT_SUCCESS(server.publish());
    server.unpublish();
    EXPECT_EQ(1, (int)server.streams()->destroyers_.size());
    EXPECT_TRUE(server.streams()->streamHandlers_.empty());
}

// The operator mounts http_remux with an extension SRS cannot remux to, such as .mp4. The config is accepted and the
// stream is mounted when it is published, but its player is refused: the connection is closed with no reply, and the
// stream API drops it. On an edge, a player of a stream that is not mounted yet is not matched by such a mount.
VOID TEST(AppHttpStreamTest, MountWithUnsupportedExtension_PlayerRefused)
{
    srs_error_t err;

    HttpStreamUtestServer server;
    HELPER_ASSERT_SUCCESS(server.start(HSTREAM_VHOST("mount [vhost]/[app]/[stream].mp4;")));
    HELPER_ASSERT_SUCCESS(server.publish());
    HELPER_ASSERT_SUCCESS(server.send(hstream_gop()));

    if (true) {
        HttpStreamUtestPlayer player(&server, "/live/livestream.mp4");
        string reply = player.play();
        EXPECT_TRUE(reply.empty()) << reply;
        EXPECT_TRUE(player.manager_.remove_called_);
    }
    EXPECT_TRUE(server.stat_.clients_.empty());
    EXPECT_TRUE(server.source()->consumers_.empty());

    ISrsHttpHandler *h = NULL;
    HELPER_ASSERT_SUCCESS(server.match("http://127.0.0.1/live/other.mp4", &h));
    EXPECT_TRUE(h->is_not_found());

    server.unpublish();
}

// The last player of an edge stream left, and the edge is stopping its pull from the origin, when a new HTTP-FLV player
// arrives. The edge refuses the new player while it stops: its connection is closed with no reply, and the stream API
// drops it.
VOID TEST(AppHttpStreamTest, EdgePlayerWhileEdgeStopsPull_Refused)
{
    srs_error_t err;

    HttpStreamUtestServer server;
    HELPER_ASSERT_SUCCESS(server.start("vhost __defaultVhost__ { cluster { mode remote; origin 127.0.0.1:19350; } "
                                       "http_remux { enabled on; mount [vhost]/[app]/[stream].flv; } } "));

    SrsSharedPtr<SrsLiveSource> source = server.source();
    source->play_edge_->state_ = SrsEdgeStateIngestStopping;

    if (true) {
        HttpStreamUtestPlayer player(&server, "/live/livestream.flv");
        string reply = player.play();
        EXPECT_TRUE(reply.empty()) << reply;
        EXPECT_TRUE(player.manager_.remove_called_);
    }
    EXPECT_TRUE(server.stat_.clients_.empty());
    EXPECT_TRUE(source->consumers_.empty());

    // The player's request mounted the stream, as an edge mounts on demand.
    EXPECT_TRUE(server.mounted(server.req_.get_stream_url()) != NULL);
    source->play_edge_->state_ = SrsEdgeStateInit;
    server.unmount(&server.req_);
}

// The process is out of memory, so no coroutine can be created. The mount of a publish on a vhost with fast_cache
// fails, as its stream cache cannot start, and the release of the refused publish unmounts the stream. A player that
// arrives is refused, as its recv thread cannot start: nothing is sent, and the stream API drops it.
VOID TEST(AppHttpStreamTest, NoCoroutineCanBeCreated_PublishAndPlayerRefused)
{
    srs_error_t err;

    if (true) {
        HttpStreamUtestServer server;
        HELPER_ASSERT_SUCCESS(server.start(HSTREAM_VHOST("fast_cache 30; mount [vhost]/[app]/[stream].aac;")));

        // The mount the source asks for, after its hub started.
        _ST_THREAD_CREATE_PFN ov = _pfn_st_thread_create;
        _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)forward_st_thread_create_out_of_memory;
        err = server.mount(&server.req_);
        _pfn_st_thread_create = ov;

        EXPECT_EQ(ERROR_ST_CREATE_CYCLE_THREAD, srs_error_code(err));
        EXPECT_NE(string::npos, srs_error_desc(err).find("start stream cache failed")) << srs_error_desc(err);
        srs_freep(err);

        server.unmount(&server.req_);
        EXPECT_TRUE(server.streams()->streamHandlers_.empty());
    }

    if (true) {
        HttpStreamUtestServer server;
        HELPER_ASSERT_SUCCESS(server.start(HSTREAM_VHOST("mount [vhost]/[app]/[stream].flv;")));
        HELPER_ASSERT_SUCCESS(server.publish());
        HELPER_ASSERT_SUCCESS(server.send(hstream_gop()));

        if (true) {
            HttpStreamUtestPlayer player(&server, "/live/livestream.flv");

            _ST_THREAD_CREATE_PFN ov = _pfn_st_thread_create;
            _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)forward_st_thread_create_out_of_memory;
            string reply = player.play();
            _pfn_st_thread_create = ov;

            EXPECT_TRUE(reply.empty()) << reply;
            EXPECT_TRUE(player.manager_.remove_called_);
        }
        EXPECT_TRUE(server.stat_.clients_.empty());
        EXPECT_TRUE(server.source()->consumers_.empty());
        server.unpublish();
    }
}

// The mount of http_remux has no [app], so the same stream name in two apps maps to one mount. The second encoder's
// publish is refused, as the mount is taken, and the first stream's players are still served at the mount.
VOID TEST(AppHttpStreamTest, TwoStreamsOnOneMount_SecondPublishRefused)
{
    srs_error_t err;

    HttpStreamUtestServer server;
    HELPER_ASSERT_SUCCESS(server.start(HSTREAM_VHOST("mount [vhost]/[stream].flv;")));
    HELPER_ASSERT_SUCCESS(server.publish());

    // The mount the source of the second publish asks for.
    HttpStreamUtestRequest game("__defaultVhost__", "game", "livestream");
    err = server.mount(&game);
    EXPECT_EQ(ERROR_HTTP_PATTERN_DUPLICATED, srs_error_code(err)) << srs_error_desc(err);
    srs_freep(err);

    ISrsHttpHandler *h = NULL;
    HELPER_ASSERT_SUCCESS(server.match("http://127.0.0.1/livestream.flv", &h));
    EXPECT_TRUE(h != NULL && h == server.mounted(server.req_.get_stream_url()));

    server.unpublish();
}

// The same as above, then the refused publish is released, as the RTMP connection releases a publish the source
// refused: the source's unpublish unmounts the second stream. The refused mount left nothing to tear down, so the
// first stream's players are still served at the mount until the first stream unpublishes, and then the mount is
// free for the second stream's next publish.
VOID TEST(AppHttpStreamTest, RefusedPublishOnSharedMount_FirstStreamStillServed)
{
    srs_error_t err;

    HttpStreamUtestServer server;
    HELPER_ASSERT_SUCCESS(server.start(HSTREAM_VHOST("mount [vhost]/[stream].flv;")));
    HELPER_ASSERT_SUCCESS(server.mount(&server.req_));

    HttpStreamUtestRequest game("__defaultVhost__", "game", "livestream");
    err = server.mount(&game);
    EXPECT_EQ(ERROR_HTTP_PATTERN_DUPLICATED, srs_error_code(err)) << srs_error_desc(err);
    srs_freep(err);
    EXPECT_TRUE(server.mounted(game.get_stream_url()) == NULL);
    server.unmount(&game);

    ISrsHttpHandler *h = NULL;
    HELPER_ASSERT_SUCCESS(server.match("http://127.0.0.1/livestream.flv", &h));
    EXPECT_TRUE(h != NULL && h == server.mounted(server.req_.get_stream_url()));

    // The first stream unpublishes, and its teardown frees the mount.
    server.unmount(&server.req_);
    EXPECT_TRUE(server.mounted(server.req_.get_stream_url()) == NULL);

    // The second stream publishes again, and is served at the mount.
    HELPER_ASSERT_SUCCESS(server.mount(&game));
    HELPER_ASSERT_SUCCESS(server.match("http://127.0.0.1/livestream.flv", &h));
    EXPECT_TRUE(h != NULL && h == server.mounted(game.get_stream_url()));
    server.unmount(&game);
}

// The process is out of memory when an encoder unpublishes, so the teardown of its HTTP stream cannot start. The stream
// stays disposing, though no teardown runs, so when memory is back, the encoder's next publish should mount the stream
// again; instead, every publish of the stream is refused as disposing until the server restarts.
VOID TEST(AppHttpStreamTest, DISABLED_UnpublishWhenNoCoroutineCanBeCreated_StreamNeverPublishesAgain)
{
    srs_error_t err;

    HttpStreamUtestServer server;
    HELPER_ASSERT_SUCCESS(server.start(HSTREAM_VHOST("mount [vhost]/[app]/[stream].flv;")));
    HELPER_ASSERT_SUCCESS(server.mount(&server.req_));

    // The unmount the source's unpublish asks for.
    _ST_THREAD_CREATE_PFN ov = _pfn_st_thread_create;
    _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)forward_st_thread_create_out_of_memory;
    server.unmount(&server.req_);
    _pfn_st_thread_create = ov;

    // The mount the source's next publish asks for.
    srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    HELPER_EXPECT_SUCCESS(server.mount(&server.req_));
}
