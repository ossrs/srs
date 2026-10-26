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
#include <srs_app_rtmp_source.hpp>
#include <srs_app_st.hpp>
#include <srs_app_statistic.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_kernel_resource.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_http_stack.hpp>
#include <srs_protocol_st.hpp>
#include <srs_utest_ai14.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai23.hpp>
#include <srs_utest_ai34.hpp>
#include <srs_utest_ai40.hpp>
#include <srs_utest_ai42.hpp>
#include <srs_utest_manual_http.hpp>
#include <srs_utest_manual_mock.hpp>
#include <srs_utest_manual_protocol.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

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

// The session logs each state change with the device it serves, as the publish API set it. The device is set through
// the session's interface, whose device_id_ the session's own member of the same name hides, so the log names no
// device.
VOID TEST(AppGb28181Test, DISABLED_SessionStateChangeLog_DeviceIdEmpty)
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
