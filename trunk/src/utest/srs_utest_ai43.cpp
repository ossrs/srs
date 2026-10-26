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
#include <srs_app_rtmp_source.hpp>
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
#include <srs_protocol_http_stack.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_st.hpp>
#include <srs_utest_ai13.hpp>
#include <srs_utest_ai14.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai16.hpp>
#include <srs_utest_ai23.hpp>
#include <srs_utest_ai34.hpp>
#include <srs_utest_ai40.hpp>
#include <srs_utest_ai42.hpp>
#include <srs_utest_manual_config.hpp>
#include <srs_utest_manual_coworkers.hpp>
#include <srs_utest_manual_http.hpp>
#include <srs_utest_manual_mock.hpp>
#include <srs_utest_manual_protocol.hpp>

#include <arpa/inet.h>
#include <dirent.h>
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

extern srs_fopen_t _srs_fopen_fn;
extern srs_fwrite_t _srs_fwrite_fn;

// Removes a dir and everything in it, as an operator's cleanup job does.
static void hls_utest_remove_all(const string &dir)
{
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
}

static string hls_utest_read_file(const string &path)
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

static void hls_utest_write_file(const string &path, const string &content)
{
    FILE *fp = fopen(path.c_str(), "wb");
    if (fp) {
        fwrite(content.data(), 1, content.length(), fp);
        fclose(fp);
    }
}

static int hls_utest_count(const string &s, const string &sub)
{
    int n = 0;
    for (size_t pos = s.find(sub); pos != string::npos; pos = s.find(sub, pos + sub.length())) {
        n++;
    }
    return n;
}

static bool hls_volume_read_only_ = false;
static bool hls_volume_full_ = false;

// Creates no file for writing once the volume is read-only.
static FILE *hls_utest_fopen(const char *path, const char *mode)
{
    if (hls_volume_read_only_ && (mode[0] == 'w' || mode[0] == 'a')) {
        errno = EROFS;
        return NULL;
    }
    return ::fopen(path, mode);
}

// Fails every write once the disk is full.
static size_t hls_utest_fwrite(const void *ptr, size_t size, size_t nitems, FILE *stream)
{
    if (hls_volume_full_) {
        errno = ENOSPC;
        return 0;
    }
    return ::fwrite(ptr, size, nitems, stream);
}

// The volume HLS writes to, which a test remounts read-only or fills.
class HlsUtestVolumeScope
{
public:
    srs_fopen_t fopen_fn_;
    srs_fwrite_t fwrite_fn_;

public:
    HlsUtestVolumeScope()
    {
        fopen_fn_ = _srs_fopen_fn;
        fwrite_fn_ = _srs_fwrite_fn;
        _srs_fopen_fn = hls_utest_fopen;
        _srs_fwrite_fn = hls_utest_fwrite;
        hls_volume_read_only_ = hls_volume_full_ = false;
    }
    virtual ~HlsUtestVolumeScope()
    {
        _srs_fopen_fn = fopen_fn_;
        _srs_fwrite_fn = fwrite_fn_;
        hls_volume_read_only_ = hls_volume_full_ = false;
    }
    void read_only()
    {
        hls_volume_read_only_ = true;
    }
    void fill()
    {
        hls_volume_full_ = true;
    }
};

// A path that fails every delete, as on a volume remounted read-only, and notes each path it was asked to delete.
class HlsUtestReadOnlyPath : public SrsPath
{
public:
    vector<string> *unlinks_;

public:
    HlsUtestReadOnlyPath(vector<string> *unlinks)
    {
        unlinks_ = unlinks;
    }
    virtual srs_error_t unlink(std::string path)
    {
        unlinks_->push_back(path);
        return srs_error_new(ERROR_SYSTEM_FILE_UNLINK, "unlink %s, read-only file system", path.c_str());
    }
};

// The server's factory, but its paths fail every delete.
class HlsUtestReadOnlyFactory : public SrsAppFactory
{
public:
    vector<string> unlinks_;

public:
    virtual SrsPath *create_path()
    {
        return new HlsUtestReadOnlyPath(&unlinks_);
    }
};

// A stream published with HLS on, its files under home, fed the packets of an RTMP publisher as the origin hub feeds
// them: each packet is parsed by the stream's format first. As the hub, it caches the sequence headers and feeds them
// again when HLS asks, after a reload or when a disposed HLS resumes.
class HlsUtestStream : public MockOriginHub
{
public:
    string home_;
    MockSrsConfig conf_;
    MockSrsRequest req_;
    SrsHls *hls_;
    SrsFormat format_;
    SrsMediaPacket *vsh_;
    SrsFormat vsh_format_;
    SrsMediaPacket *ash_;
    SrsFormat ash_format_;
    // The factory of the muxer's files and paths, or NULL for the server's.
    ISrsAppFactory *factory_;

public:
    HlsUtestStream(string name) : req_("__defaultVhost__", "live", "livestream")
    {
        home_ = _srs_tmp_file_prefix + name;
        hls_utest_remove_all(home_);
        hls_ = new SrsHls();
        hls_->assemble();
        vsh_ = ash_ = NULL;
        factory_ = NULL;
    }
    virtual ~HlsUtestStream()
    {
        srs_freep(hls_);
        srs_freep(vsh_);
        srs_freep(ash_);
        hls_utest_remove_all(home_);
    }

public:
    // Loads the config of the vhost, whose hls section has these options, and hls_path under home. HLS is on, unless
    // the options turn it off.
    srs_error_t load(MockSrsConfig &conf, string options)
    {
        return conf.mock_parse(string(_MIN_OK_CONF) + "vhost __defaultVhost__ { hls { " + options + " hls_path " + home_ + "; enabled on; } }");
    }
    // Points HLS, its controller and muxer at this config, and the muxer at the test's factory, if any.
    void use(ISrsAppConfig *conf)
    {
        hls_->config_ = conf;
        SrsHlsController *ts = dynamic_cast<SrsHlsController *>(hls_->controller_);
        if (ts) {
            ts->config_ = conf;
            SrsHlsMuxer *muxer = dynamic_cast<SrsHlsMuxer *>(ts->muxer_);
            muxer->config_ = conf;
            if (factory_) {
                muxer->app_factory_ = factory_;
            }
        }
        SrsHlsMp4Controller *mp4 = dynamic_cast<SrsHlsMp4Controller *>(hls_->controller_);
        if (mp4) {
            mp4->config_ = conf;
            mp4->muxer_->config_ = conf;
            if (factory_) {
                mp4->muxer_->app_factory_ = factory_;
            }
        }
    }
    srs_error_t publish(string options)
    {
        srs_error_t err = srs_success;

        if ((err = load(conf_, options)) != srs_success) {
            return srs_error_wrap(err, "config");
        }
        if ((err = format_.initialize()) != srs_success) {
            return srs_error_wrap(err, "format");
        }
        if ((err = vsh_format_.initialize()) != srs_success) {
            return srs_error_wrap(err, "vsh format");
        }
        if ((err = ash_format_.initialize()) != srs_success) {
            return srs_error_wrap(err, "ash format");
        }

        hls_->config_ = &conf_;
        if ((err = hls_->initialize(this, &req_)) != srs_success) {
            return srs_error_wrap(err, "initialize");
        }
        use(&conf_);

        return hls_->on_publish();
    }
    srs_error_t video(int64_t ts, const uint8_t *data, int size)
    {
        srs_error_t err = srs_success;

        SrsMediaPacket msg;
        char *payload = new char[size];
        memcpy(payload, data, size);
        msg.wrap(payload, size);
        msg.timestamp_ = ts;
        msg.message_type_ = SrsFrameTypeVideo;

        if ((err = format_.on_video(ts, msg.payload(), msg.size())) != srs_success) {
            return srs_error_wrap(err, "format");
        }
        if (format_.is_avc_sequence_header()) {
            srs_freep(vsh_);
            vsh_ = msg.copy();
            if ((err = vsh_format_.on_video(ts, vsh_->payload(), vsh_->size())) != srs_success) {
                return srs_error_wrap(err, "vsh format");
            }
        }

        return hls_->on_video(&msg, &format_);
    }
    srs_error_t audio(int64_t ts, const uint8_t *data, int size)
    {
        srs_error_t err = srs_success;

        SrsMediaPacket msg;
        char *payload = new char[size];
        memcpy(payload, data, size);
        msg.wrap(payload, size);
        msg.timestamp_ = ts;
        msg.message_type_ = SrsFrameTypeAudio;

        if ((err = format_.on_audio(ts, msg.payload(), msg.size())) != srs_success) {
            return srs_error_wrap(err, "format");
        }
        if (format_.is_aac_sequence_header()) {
            srs_freep(ash_);
            ash_ = msg.copy();
            if ((err = ash_format_.on_audio(ts, ash_->payload(), ash_->size())) != srs_success) {
                return srs_error_wrap(err, "ash format");
            }
        }

        return hls_->on_audio(&msg, &format_);
    }
    // Sends the sequence headers of both tracks.
    srs_error_t sequence_headers()
    {
        srs_error_t err = srs_success;

        if ((err = video(0, flv_utest_avc_sh, sizeof(flv_utest_avc_sh))) != srs_success) {
            return srs_error_wrap(err, "avc sh");
        }
        if ((err = audio(0, flv_utest_aac_sh, sizeof(flv_utest_aac_sh))) != srs_success) {
            return srs_error_wrap(err, "aac sh");
        }
        return err;
    }
    // Sends the media from one time to another, in ms: 10 video and 10 audio frames a second, a key frame each second.
    srs_error_t av(int64_t from, int64_t to)
    {
        srs_error_t err = srs_success;

        for (int64_t ts = from; ts <= to; ts += 100) {
            const uint8_t *frame = (ts % 1000) == 0 ? flv_utest_avc_idr : flv_utest_avc_inter;
            if ((err = video(ts, frame, sizeof(flv_utest_avc_idr))) != srs_success) {
                return srs_error_wrap(err, "video %d", (int)ts);
            }
            if ((err = audio(ts, flv_utest_aac_frame, sizeof(flv_utest_aac_frame))) != srs_success) {
                return srs_error_wrap(err, "audio %d", (int)ts);
            }
        }
        return err;
    }
    // On HLS's request, feeds the cached sequence headers again, as SrsOriginHub does.
    virtual srs_error_t on_hls_request_sh()
    {
        srs_error_t err = srs_success;

        on_hls_request_sh_count_++;
        if (on_hls_request_sh_error_) {
            return srs_error_copy(on_hls_request_sh_error_);
        }

        if (vsh_ && (err = hls_->on_video(vsh_, &vsh_format_)) != srs_success) {
            return srs_error_wrap(err, "hls video sh");
        }
        if (ash_ && (err = hls_->on_audio(ash_, &ash_format_)) != srs_success) {
            return srs_error_wrap(err, "hls audio sh");
        }
        return err;
    }

public:
    string path(string name)
    {
        return home_ + "/live/" + name;
    }
    bool exists(string name)
    {
        SrsPath p;
        return p.exists(path(name));
    }
    string m3u8()
    {
        return hls_utest_read_file(path("livestream.m3u8"));
    }
};

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
// so an empty playlist is left. When the publisher publishes again, the server should start a new playlist, as with no
// playlist at all; it refuses the publish, with an error of code 0, until an operator removes the file.
VOID TEST(AppHlsTest, DISABLED_EmptyPlaylistLeftByPowerLoss_PublishRefused)
{
    srs_error_t err;

    HlsUtestStream s("ai43-hls-empty");
    SrsPath p;
    HELPER_ASSERT_SUCCESS(p.mkdir_all(s.path("")));
    hls_utest_write_file(s.path("livestream.m3u8"), "");

    HELPER_EXPECT_SUCCESS(s.publish("hls_fragment 1;"));
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

// The HLS volume was remounted read-only after an I/O error, and a publisher publishes to a TS stream. The first
// segment can not be created, so the publish fails.
VOID TEST(AppHlsTest, ReadOnlyVolumeAtPublish_TsPublishRefused)
{
    srs_error_t err;

    HlsUtestVolumeScope volume;
    HlsUtestStream s("ai43-hls-ro-publish");
    volume.read_only();

    err = s.publish("");
    EXPECT_EQ(ERROR_SYSTEM_FILE_OPENE, srs_error_code(err));
    EXPECT_NE(string::npos, srs_error_desc(err).find("hls: segment open")) << srs_error_desc(err);
    srs_freep(err);
    EXPECT_FALSE(s.exists("livestream-0.ts.tmp"));
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

// The disk fills while a publisher streams, right before a key frame. The segment is finished, but neither the
// playlist nor the next segment can be written, so the frame fails and the hub stops HLS for the stream; no playlist
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
// [app]/[stream]/[seq].ts. The playlist can not be renamed over the dir, so the segment close should fail and the hub
// stop HLS for the stream; the rename error is lost instead, and the stream goes on with no playlist and no warning.
VOID TEST(AppHlsTest, DISABLED_PlaylistNamedAsSegmentDir_RenameErrorLost)
{
    srs_error_t err;

    const char *options[] = {"hls_fragment 1; hls_m3u8_file [app]/[stream]; hls_ts_file [app]/[stream]/[seq].ts;",
                             "hls_fragment 1; hls_use_fmp4 on; hls_m3u8_file [app]/[stream]; hls_fmp4_file [app]/[stream]/[seq].m4s;"};
    for (int i = 0; i < 2; i++) {
        HlsUtestStream s("ai43-hls-m3u8-dir");
        HELPER_ASSERT_SUCCESS(s.publish(options[i]));
        HELPER_ASSERT_SUCCESS(s.sequence_headers());
        HELPER_ASSERT_SUCCESS(s.av(0, 900));

        err = s.video(1000, flv_utest_avc_idr, sizeof(flv_utest_avc_idr));
        EXPECT_EQ(ERROR_HLS_WRITE_FAILED, srs_error_code(err)) << options[i];
        EXPECT_NE(string::npos, srs_error_desc(err).find("rename m3u8 file failed")) << srs_error_desc(err);
        srs_freep(err);
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
// Each key frame should cut a segment of 1s, as TS does; but the fMP4 muxer checks the segment's duration before it
// adds the key frame, which is then 0.9s, so it cuts each second key frame, and lists 2s segments as 1.9s.
VOID TEST(AppHlsTest, DISABLED_Fmp4SegmentOfOneGop_CutAtSecondKeyFrame)
{
    srs_error_t err;

    HlsUtestStream s("ai43-hls-fmp4-gop");
    HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1; hls_window 3; hls_use_fmp4 on;"));
    HELPER_ASSERT_SUCCESS(s.sequence_headers());
    HELPER_ASSERT_SUCCESS(s.av(0, 10000));

    string m3u8 = s.m3u8();
    EXPECT_NE(string::npos, m3u8.find("#EXT-X-MEDIA-SEQUENCE:6\n")) << m3u8;
    EXPECT_EQ(4, hls_utest_count(m3u8, "#EXTINF:1.000, no desc\n")) << m3u8;
}

// The EXTINF durations of a playlist, in ms.
static vector<int> hls_utest_extinf_ms(const string &m3u8)
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
// file is left for players.
VOID TEST(AppHlsTest, Fmp4VolumeFailsAtSequenceHeader_InitFileNotWritten)
{
    srs_error_t err;

    for (int full = 0; full < 2; full++) {
        for (int track = 0; track < 3; track++) {
            HlsUtestVolumeScope volume;
            HlsUtestStream s("ai43-hls-fmp4-init");
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
    HELPER_ASSERT_SUCCESS(s.av(0, 1900));

    volume.fill();
    err = s.video(2000, flv_utest_avc_idr, sizeof(flv_utest_avc_idr));
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
    HELPER_ASSERT_SUCCESS(s.av(0, 3900));
    string m3u8 = s.m3u8();
    ASSERT_EQ(1, hls_utest_count(m3u8, "#EXTINF:")) << m3u8;

    volume.read_only();
    err = s.video(4000, flv_utest_avc_idr, sizeof(flv_utest_avc_idr));
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
