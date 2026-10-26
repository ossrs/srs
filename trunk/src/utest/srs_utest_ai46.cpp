//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai46.hpp>

#include <srs_app_rtc_codec.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_srt_listener.hpp>
#include <srs_app_srt_server.hpp>
#include <srs_app_srt_source.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_stream_bridge.hpp>
#include <srs_app_listener.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_rtmp_msg_array.hpp>
#include <srs_protocol_sdp.hpp>
#include <srs_protocol_srt.hpp>
#include <srs_protocol_st.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_utest_ai08.hpp>
#include <srs_utest_ai14.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai24.hpp>
#include <srs_utest_ai32.hpp>
#include <srs_utest_ai37.hpp>
#include <srs_utest_ai40.hpp>
#include <srs_utest_ai42.hpp>
#include <srs_utest_manual_config.hpp>
#include <srs_utest_manual_mock.hpp>

#ifndef _WIN32
#include <fcntl.h>
#include <net/if.h>
#include <unistd.h>
#endif

#include <map>
#include <set>
#include <vector>
using namespace std;

// The server's SRT acceptor listening on loopback as `srt_server { listen }` makes it, with the listener and its accept
// coroutine from the real factory, handing each caller to a server that keeps the accepted sockets.
class SrtListenerUtestServer
{
public:
    MockSrsConfig conf_;
    MockSrtClientHandlerForAcceptor server_;
    SrsSrtAcceptor *acceptor_;
    int port_;

public:
    SrtListenerUtestServer()
    {
        acceptor_ = new SrsSrtAcceptor(&server_);
        port_ = 0;
    }
    virtual ~SrtListenerUtestServer()
    {
        // Stop accepting first, then close what the server was handed, as a server that quits does.
        srs_freep(acceptor_);
        for (int i = 0; i < (int)server_.fds_.size(); i++) {
            srs_srt_close(server_.fds_[i]);
        }
    }

public:
    srs_error_t listen(string ip)
    {
        srs_error_t err = srs_success;

        if ((err = srs_utest_free_port(SOCK_DGRAM, &port_)) != srs_success) {
            return srs_error_wrap(err, "port");
        }
        if ((err = conf_.mock_parse(_MIN_OK_CONF "srt_server { enabled on; listen 10080; }")) != srs_success) {
            return srs_error_wrap(err, "config");
        }
        acceptor_->config_ = &conf_;

        return acceptor_->listen(ip, port_);
    }

    // Wait until the server was handed n callers, as long as a caller's handshake may take on loopback.
    void wait_for_callers(int n)
    {
        for (int i = 0; i < 300 && (int)server_.fds_.size() < n; i++) {
            srs_usleep(10 * SRS_UTIME_MILLISECONDS);
        }
    }

    string streamid_of(int index)
    {
        string streamid;
        srs_error_t err = srs_srt_get_streamid(server_.fds_[index], streamid);
        srs_freep(err);
        return streamid;
    }
};

// An SRT caller such as ffmpeg or OBS, with its stream id, connecting to the server's SRT port.
class SrtListenerUtestCaller
{
public:
    SrsSrtSocket *skt_;

public:
    SrtListenerUtestCaller()
    {
        skt_ = NULL;
    }
    virtual ~SrtListenerUtestCaller()
    {
        srs_freep(skt_);
    }

public:
    srs_error_t connect(int port, string streamid)
    {
        srs_error_t err = srs_success;

        srs_srt_t fd = srs_srt_socket_invalid();
        if ((err = srs_srt_socket_with_default_option(&fd)) != srs_success) {
            return srs_error_wrap(err, "socket");
        }
        skt_ = new SrsSrtSocket(_srs_srt_eventloop->poller(), fd);
        skt_->set_send_timeout(3 * SRS_UTIME_SECONDS);
        skt_->set_recv_timeout(3 * SRS_UTIME_SECONDS);
        if ((err = srs_srt_set_streamid(fd, streamid)) != srs_success) {
            return srs_error_wrap(err, "streamid");
        }
        return skt_->connect("127.0.0.1", port);
    }
};

// State Threads returns NULL when it cannot map the stack of a new coroutine, such as when the process is out of
// memory or over its address space limit.
static void *srt_listener_utest_st_thread_create_out_of_memory(void *(* /*start*/)(void *arg), void * /*arg*/, int /*joinable*/, int /*stack_size*/)
{
    return NULL;
}

// An encoder and then a player connect to the server's SRT port. The listener accepts each one and hands its socket to
// the server, which reads the caller's stream id from it to know the stream and whether it publishes or plays.
VOID TEST(AppSrtListenerTest, EncoderThenPlayerConnect_EachHandedToServer)
{
    srs_error_t err;

    SrtListenerUtestServer s;
    HELPER_ASSERT_SUCCESS(s.listen("127.0.0.1"));

    SrtListenerUtestCaller encoder;
    HELPER_ASSERT_SUCCESS(encoder.connect(s.port_, "#!::r=live/livestream,m=publish"));
    s.wait_for_callers(1);
    ASSERT_EQ(1, (int)s.server_.fds_.size());
    EXPECT_STREQ("#!::r=live/livestream,m=publish", s.streamid_of(0).c_str());

    SrtListenerUtestCaller player;
    HELPER_ASSERT_SUCCESS(player.connect(s.port_, "#!::r=live/livestream,m=request"));
    s.wait_for_callers(2);
    ASSERT_EQ(2, (int)s.server_.fds_.size());
    EXPECT_STREQ("#!::r=live/livestream,m=request", s.streamid_of(1).c_str());

    // Each caller gets its own socket, apart from the listening one.
    EXPECT_NE(s.server_.fds_[0], s.server_.fds_[1]);
    EXPECT_NE(s.acceptor_->listener_->fd(), s.server_.fds_[0]);
    EXPECT_NE(s.acceptor_->listener_->fd(), s.server_.fds_[1]);
}

// The server refuses a caller, such as one over max_connections, and closes its socket. The listener goes on, so the
// next caller to the port is still accepted and handed to the server.
VOID TEST(AppSrtListenerTest, ServerRefusesCaller_NextCallerStillHanded)
{
    srs_error_t err;

    SrtListenerUtestServer s;
    HELPER_ASSERT_SUCCESS(s.listen("127.0.0.1"));
    s.server_.accept_error_ = srs_error_new(ERROR_EXCEED_CONNECTIONS, "exceed max connections");

    SrtListenerUtestCaller refused;
    HELPER_ASSERT_SUCCESS(refused.connect(s.port_, "#!::r=live/refused,m=publish"));
    s.wait_for_callers(1);
    ASSERT_EQ(1, (int)s.server_.fds_.size());
    srs_srt_close(s.server_.fds_[0]);
    s.server_.fds_.clear();
    srs_freep(s.server_.accept_error_);

    SrtListenerUtestCaller next;
    HELPER_ASSERT_SUCCESS(next.connect(s.port_, "#!::r=live/next,m=publish"));
    s.wait_for_callers(1);
    ASSERT_EQ(1, (int)s.server_.fds_.size());
    EXPECT_STREQ("#!::r=live/next,m=publish", s.streamid_of(0).c_str());
}

// The config listens at an address the host does not have, such as a config moved from another machine
// (192.0.2.1 is a documentation address, never assigned). The bind fails, so the server fails to start with the
// error instead of running with no SRT port.
VOID TEST(AppSrtListenerTest, ListenAtAddressNotOnHost_ServerFailsToStart)
{
    srs_error_t err;

    SrtListenerUtestServer s;
    err = s.listen("192.0.2.1");

    EXPECT_EQ(ERROR_SOCKET_BIND, srs_error_code(err));
    EXPECT_TRUE(srs_error_desc(err).find("srs_srt_listen") != string::npos);
    srs_freep(err);

    // No accept loop is started for the port.
    SrsSrtListener *listener = dynamic_cast<SrsSrtListener *>(s.acceptor_->listener_);
    ASSERT_TRUE(listener != NULL);
    EXPECT_TRUE(NULL == listener->srt_skt_);
}

// No coroutine can be created for the listener's accept loop, as when the process is out of memory at startup. The
// server fails to start with the ST error instead of running with an SRT port nobody accepts on.
VOID TEST(AppSrtListenerTest, NoCoroutineForAcceptLoop_ServerFailsToStart)
{
    srs_error_t err;

    SrtListenerUtestServer s;

    _ST_THREAD_CREATE_PFN ov = _pfn_st_thread_create;
    _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)srt_listener_utest_st_thread_create_out_of_memory;
    err = s.listen("127.0.0.1");
    _pfn_st_thread_create = ov;

    EXPECT_EQ(ERROR_ST_CREATE_CYCLE_THREAD, srs_error_code(err));
    EXPECT_TRUE(srs_error_desc(err).find("start coroutine") != string::npos);
    srs_freep(err);
}

// The server starts its SRT event loop, which polls every SRT socket, before it listens on any port. When no coroutine
// can be created for that loop, as when the process is out of memory at startup, the start fails with the ST error, so
// the server quits instead of running with SRT sockets nobody polls.
VOID TEST(AppSrtServerTest, NoCoroutineForEventLoop_ServerFailsToStart)
{
    srs_error_t err;

    SrsSrtEventLoop loop;
    HELPER_ASSERT_SUCCESS(loop.initialize());
    EXPECT_TRUE(loop.poller() != NULL);

    _ST_THREAD_CREATE_PFN ov = _pfn_st_thread_create;
    _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)srt_listener_utest_st_thread_create_out_of_memory;
    err = loop.start();
    _pfn_st_thread_create = ov;

    EXPECT_EQ(ERROR_ST_CREATE_CYCLE_THREAD, srs_error_code(err));
    EXPECT_TRUE(srs_error_desc(err).find("start coroutine") != string::npos);
    srs_freep(err);
}

// H.264 320x240 baseline parameter sets, an IDR slice and a P slice, as an SRT encoder sends them.
static const uint8_t srt_source_utest_sps[] = {0x67, 0x42, 0xc0, 0x0d, 0xd9, 0x01, 0x41, 0xfb, 0x01, 0x10, 0x00, 0x00,
                                               0x03, 0x00, 0x10, 0x00, 0x00, 0x03, 0x03, 0x20, 0xf1, 0x42, 0xa4, 0x80};
static const uint8_t srt_source_utest_pps[] = {0x68, 0xcb, 0x83, 0xcb, 0x20};
static const uint8_t srt_source_utest_idr[] = {0x65, 0x88, 0x84, 0x00, 0x10, 0x11, 0x22, 0x33};
static const uint8_t srt_source_utest_p[] = {0x41, 0x9a, 0x21, 0x6c, 0x42, 0x11, 0x22, 0x33};

// HEVC 720p parameter sets, an IDR_W_RADL slice and a TRAIL_R slice.
static const uint8_t srt_source_utest_vps[] = {0x40, 0x01, 0x0c, 0x01, 0xff, 0xff, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00,
                                               0x90, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x3d, 0x95, 0x98, 0x09};
static const uint8_t srt_source_utest_hevc_sps[] = {0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0x90, 0x00, 0x00, 0x03, 0x00,
                                                    0x00, 0x03, 0x00, 0x3d, 0xa0, 0x02, 0x80, 0x80, 0x2d, 0x16, 0x59, 0x59, 0xa4, 0x93,
                                                    0x2b, 0xc0, 0x40, 0x40, 0x00, 0x00, 0xfa, 0x40, 0x00, 0x17, 0x70, 0x02};
static const uint8_t srt_source_utest_hevc_pps[] = {0x44, 0x01, 0xc1, 0x72, 0xb4, 0x62, 0x40};
static const uint8_t srt_source_utest_hevc_idr[] = {0x26, 0x01, 0xaf, 0x06, 0xb8, 0x63, 0xef, 0x3a, 0x7f, 0x3c, 0x00, 0x01, 0x00, 0x80};
static const uint8_t srt_source_utest_hevc_p[] = {0x02, 0x01, 0xd0, 0x09, 0x7e, 0x11, 0x22, 0x33};

// An ADTS frame of AAC-LC, 44.1kHz, stereo: a 7-byte header and 8 bytes of raw data.
static const uint8_t srt_source_utest_adts[] = {0xff, 0xf1, 0x50, 0x80, 0x01, 0xff, 0xfc, 0x21, 0x11, 0x45, 0x00, 0x14, 0x50, 0x01, 0x46};

static string srt_source_utest_nalu(const uint8_t *nalu, int size)
{
    return string("\x00\x00\x00\x01", 4) + string((const char *)nalu, size);
}

static string srt_source_utest_h264_key()
{
    return srt_source_utest_nalu(srt_source_utest_sps, sizeof(srt_source_utest_sps)) +
           srt_source_utest_nalu(srt_source_utest_pps, sizeof(srt_source_utest_pps)) +
           srt_source_utest_nalu(srt_source_utest_idr, sizeof(srt_source_utest_idr));
}

static string srt_source_utest_h264_p()
{
    return srt_source_utest_nalu(srt_source_utest_p, sizeof(srt_source_utest_p));
}

static string srt_source_utest_hevc_params()
{
    return srt_source_utest_nalu(srt_source_utest_vps, sizeof(srt_source_utest_vps)) +
           srt_source_utest_nalu(srt_source_utest_hevc_sps, sizeof(srt_source_utest_hevc_sps)) +
           srt_source_utest_nalu(srt_source_utest_hevc_pps, sizeof(srt_source_utest_hevc_pps));
}

static string srt_source_utest_adts_frame()
{
    return string((const char *)srt_source_utest_adts, sizeof(srt_source_utest_adts));
}

// The RTMP side of an SRT stream on a vhost with `srt_to_rtmp on`, the live source, keeping each FLV tag the frame builder
// gives it. While refuse_ is set it refuses every tag, as the live source does for a tag its format cannot parse.
class SrtSourceUtestRtmpSide : public ISrsFrameTarget
{
public:
    vector<string> tags_;
    vector<int64_t> times_;
    bool refuse_;

public:
    SrtSourceUtestRtmpSide()
    {
        refuse_ = false;
    }
    virtual ~SrtSourceUtestRtmpSide()
    {
    }

public:
    virtual srs_error_t on_frame(SrsMediaPacket *frame)
    {
        if (refuse_) {
            return srs_error_new(ERROR_HLS_DECODE_ERROR, "format consume");
        }
        tags_.push_back(string(frame->payload(), frame->size()));
        times_.push_back(frame->timestamp_);
        return srs_success;
    }
};

// A TS writer keeping the TS packets an encoder muxes.
class SrtSourceUtestTsWriter : public ISrsStreamWriter
{
public:
    string data_;

public:
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite)
    {
        data_.append((const char *)buf, size);
        if (nwrite) {
            *nwrite = (ssize_t)size;
        }
        return srs_success;
    }
};

// An SRT encoder publishing a stream to the server's real SrsSrtSource, which parses the codecs for the stream API into
// a real statistic, and converts it to RTMP through the real SrsSrtBridge and frame builder (srt_to_rtmp on). The
// encoder muxes each PES into TS as FFmpeg or OBS does, and sends the TS in SRT packets of 7 TS packets.
class SrtSourceUtestStream
{
public:
    SrsStatistic stat_;
    SrsRequest req_;
    MockLogForSignal log_;
    SrtSourceUtestRtmpSide rtmp_;
    SrsSharedPtr<SrsSrtSource> source_;
    SrsSrtBridge *bridge_;
    SrsTsContext ts_;
    SrtSourceUtestTsWriter writer_;
    SrsVideoCodecId vcodec_;
    SrsAudioCodecId acodec_;

public:
    SrtSourceUtestStream(string stream, SrsVideoCodecId vcodec = SrsVideoCodecIdAVC, SrsAudioCodecId acodec = SrsAudioCodecIdAAC)
    {
        req_.vhost_ = "__defaultVhost__";
        req_.app_ = "live";
        req_.stream_ = stream;
        vcodec_ = vcodec;
        acodec_ = acodec;
        bridge_ = NULL;
    }
    virtual ~SrtSourceUtestStream()
    {
        if (source_.get()) {
            source_->on_unpublish();
            source_->stat_ = NULL;
            dynamic_cast<SrsSrtFormat *>(source_->format_)->stat_ = NULL;
        }
    }

public:
    srs_error_t publish()
    {
        srs_error_t err = srs_success;

        source_ = SrsSharedPtr<SrsSrtSource>(new SrsSrtSource());
        source_->assemble();
        source_->stat_ = &stat_;
        format()->stat_ = &stat_;
        if ((err = source_->initialize(&req_)) != srs_success) {
            return srs_error_wrap(err, "source");
        }

        bridge_ = new SrsSrtBridge(_srs_app_factory);
        bridge_->assemble();
        bridge_->frame_builder_->frame_target_ = &rtmp_;
        if ((err = bridge_->initialize(&req_)) != srs_success) {
            return srs_error_wrap(err, "bridge");
        }
        source_->set_bridge(bridge_);

        return source_->on_publish();
    }

    SrsSrtFormat *format()
    {
        return dynamic_cast<SrsSrtFormat *>(source_->format_);
    }

    SrsStatisticStream *api_stream()
    {
        return stat_.find_stream_by_url(req_.get_stream_url());
    }

    // Mux a video PES of annexb NALUs, with its dts and pts in 90kHz.
    srs_error_t video(int64_t dts, int64_t pts, const string &annexb)
    {
        SrsTsMessage msg;
        msg.sid_ = SrsTsPESStreamIdVideoCommon;
        msg.dts_ = dts;
        msg.pts_ = pts;
        msg.payload_->append(annexb.data(), (int)annexb.length());
        return ts_.encode(&writer_, &msg, vcodec_, acodec_);
    }

    // Mux an audio PES of ADTS frames, with the PES stream id the encoder gives it, 0xc0 by default.
    srs_error_t audio(int64_t pts, const string &adts, uint8_t sid = 0xc0)
    {
        srs_error_t err = srs_success;

        size_t start = writer_.data_.size();
        SrsTsMessage msg;
        msg.sid_ = SrsTsPESStreamIdAudioCommon;
        msg.dts_ = msg.pts_ = pts;
        msg.payload_->append(adts.data(), (int)adts.length());
        if ((err = ts_.encode(&writer_, &msg, vcodec_, acodec_)) != srs_success) {
            return err;
        }

        // The muxer writes 0xc0; set the stream id in the PES header of the new packets, as another muxer writes it.
        string pes_start("\x00\x00\x01\xc0", 4);
        size_t pos = writer_.data_.find(pes_start, start);
        if (pos != string::npos) {
            writer_.data_[pos + 3] = (char)sid;
        }
        return err;
    }

    // Send the TS muxed so far in SRT packets of 7 TS packets, with the server's log kept.
    srs_error_t send()
    {
        srs_error_t err = srs_success;

        ISrsLog *original = _srs_log;
        _srs_log = &log_;

        const int nb_srt = 7 * SRS_TS_PACKET_SIZE;
        for (size_t off = 0; off < writer_.data_.size(); off += nb_srt) {
            int size = (int)srs_min((size_t)nb_srt, writer_.data_.size() - off);
            SrsUniquePtr<SrsSrtPacket> pkt(new SrsSrtPacket());
            pkt->wrap((char *)writer_.data_.data() + off, size);
            if ((err = source_->on_srt_packet(pkt.get())) != srs_success) {
                break;
            }
        }
        writer_.data_.clear();

        _srs_log = original;
        return err;
    }

    // Whether any warning the server logged has the text.
    bool warned(const string &text)
    {
        for (int i = 0; i < (int)log_.warns_.size(); i++) {
            if (log_.warns_[i].find(text) != string::npos) {
                return true;
            }
        }
        return false;
    }
};

// The first byte and the AVC or AAC packet type of each FLV tag, such as "17 00" for an AVC sequence header.
static string srt_source_utest_kinds(const vector<string> &tags)
{
    string kinds;
    for (int i = 0; i < (int)tags.size(); i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%s%02x %02x", i ? "," : "", (uint8_t)tags[i][0], (uint8_t)tags[i][1]);
        kinds += buf;
    }
    return kinds;
}

// An H.264 encoder sends a key frame with its SPS and PPS, AAC audio, then P frames. The RTMP side gets the AVC
// sequence header, the key frame, the AAC sequence header and frame, and each P frame as an inter frame, at its time,
// and the stream API reports both codecs.
VOID TEST(AppSrtSourceTest, H264KeyFrameThenPFrames_PFramesConvertedAsInterFrames)
{
    srs_error_t err;

    SrtSourceUtestStream s("h264-gop");
    HELPER_ASSERT_SUCCESS(s.publish());

    HELPER_ASSERT_SUCCESS(s.video(90000, 90000, srt_source_utest_h264_key()));
    HELPER_ASSERT_SUCCESS(s.audio(90000, srt_source_utest_adts_frame()));
    HELPER_ASSERT_SUCCESS(s.video(93600, 93600, srt_source_utest_h264_p()));
    HELPER_ASSERT_SUCCESS(s.video(97200, 97200, srt_source_utest_h264_p()));
    HELPER_ASSERT_SUCCESS(s.send());

    EXPECT_STREQ("17 00,17 01,af 00,af 01,27 01,27 01", srt_source_utest_kinds(s.rtmp_.tags_).c_str());
    ASSERT_EQ(6, (int)s.rtmp_.times_.size());
    EXPECT_EQ(1000, s.rtmp_.times_[0]);
    EXPECT_EQ(1000, s.rtmp_.times_[1]);
    EXPECT_EQ(1040, s.rtmp_.times_[4]);
    EXPECT_EQ(1080, s.rtmp_.times_[5]);

    // The P frame keeps its NALU, after the 5-byte tag header and the 4-byte NALU size.
    EXPECT_EQ(string((const char *)srt_source_utest_p, sizeof(srt_source_utest_p)), s.rtmp_.tags_[4].substr(9));

    SrsStatisticStream *stream = s.api_stream();
    ASSERT_TRUE(stream != NULL);
    EXPECT_EQ(SrsVideoCodecIdAVC, stream->vcodec_);
    EXPECT_EQ(320, stream->width_);
    EXPECT_EQ(240, stream->height_);
    EXPECT_EQ(SrsAudioCodecIdAAC, stream->acodec_);
}

// FFmpeg muxes AAC into TS with many frames in one PES (https://github.com/ossrs/srs/issues/3164). Each frame of the PES
// goes to RTMP at its own time, 1024 samples apart, and the server warns of the latency at most once a pithy print
// interval, not for every PES.
VOID TEST(AppSrtSourceTest, TenAacFramesInOnePes_EachFrameTimedAndLatencyWarnedOnce)
{
    srs_error_t err;

    SrtSourceUtestStream s("aac-merged");
    HELPER_ASSERT_SUCCESS(s.publish());

    MockClockForPithyPrint clock;
    clock.now_ = 100 * SRS_UTIME_SECONDS;
    SrsAlonePithyPrint *pp = s.bridge_->frame_builder_->pp_audio_duration_;
    pp->clk_ = &clock;
    pp->previous_tick_ = clock.now_;

    string pes;
    for (int i = 0; i < 10; i++) {
        pes += srt_source_utest_adts_frame();
    }

    // The first PES comes within the interval: no warning yet.
    HELPER_ASSERT_SUCCESS(s.audio(90000, pes));
    HELPER_ASSERT_SUCCESS(s.send());
    EXPECT_FALSE(s.warned("audio duration="));

    // After the pithy print interval, the next PES warns, and one right after it does not.
    clock.now_ += 10 * SRS_UTIME_SECONDS;
    HELPER_ASSERT_SUCCESS(s.audio(90000 + 90 * 232, pes));
    HELPER_ASSERT_SUCCESS(s.audio(90000 + 90 * 464, pes));
    HELPER_ASSERT_SUCCESS(s.send());
    int nb_warns = 0;
    for (int i = 0; i < (int)s.log_.warns_.size(); i++) {
        if (s.log_.warns_[i].find("audio duration=230ms too large, audio frames=10") != string::npos) {
            nb_warns++;
        }
    }
    EXPECT_EQ(1, nb_warns);

    // The sequence header, then 30 frames, each 1024 samples at 44.1kHz (23.2ms) after the one before in its PES.
    ASSERT_EQ(31, (int)s.rtmp_.times_.size());
    EXPECT_EQ(0x00, (uint8_t)s.rtmp_.tags_[0][1]);
    EXPECT_EQ(1000, s.rtmp_.times_[1]);
    EXPECT_EQ(1023, s.rtmp_.times_[2]);
    EXPECT_EQ(1046, s.rtmp_.times_[3]);
    EXPECT_EQ(1208, s.rtmp_.times_[10]);
    EXPECT_EQ(1232, s.rtmp_.times_[11]);
}

// An encoder that marks its AAC as private stream 1 (PES stream id 0xbd), as some hardware encoders do
// (https://github.com/ossrs/srs/issues/740). The audio is still converted to RTMP as AAC.
VOID TEST(AppSrtSourceTest, AacAsPrivateStream1_ConvertedAsAudio)
{
    srs_error_t err;

    SrtSourceUtestStream s("private-stream-1");
    HELPER_ASSERT_SUCCESS(s.publish());

    HELPER_ASSERT_SUCCESS(s.video(90000, 90000, srt_source_utest_h264_key()));
    HELPER_ASSERT_SUCCESS(s.audio(90000, srt_source_utest_adts_frame(), 0xbd));
    HELPER_ASSERT_SUCCESS(s.send());

    EXPECT_STREQ("17 00,17 01,af 00,af 01", srt_source_utest_kinds(s.rtmp_.tags_).c_str());
    EXPECT_EQ(string((const char *)srt_source_utest_adts + 7, 8), s.rtmp_.tags_[3].substr(2));
}

// A muxer numbers its audio stream 1 (PES stream id 0xc1), as for the second audio of a source with two languages. The
// server converts only stream 0, so the audio is not converted to RTMP, and the video goes on.
VOID TEST(AppSrtSourceTest, AudioStreamNumberOne_NotConvertedVideoGoesOn)
{
    srs_error_t err;

    SrtSourceUtestStream s("audio-stream-1");
    HELPER_ASSERT_SUCCESS(s.publish());

    HELPER_ASSERT_SUCCESS(s.video(90000, 90000, srt_source_utest_h264_key()));
    HELPER_ASSERT_SUCCESS(s.audio(90000, srt_source_utest_adts_frame(), 0xc1));
    HELPER_ASSERT_SUCCESS(s.video(93600, 93600, srt_source_utest_h264_p()));
    HELPER_ASSERT_SUCCESS(s.send());

    EXPECT_STREQ("17 00,17 01,27 01", srt_source_utest_kinds(s.rtmp_.tags_).c_str());
    EXPECT_TRUE(s.warned("unsupported stream format, sid=0xc1(A-1)"));
}

// An encoder sends MP3 audio in TS, such as FFmpeg with libmp3lame. The SRT to RTMP conversion takes only AAC, so the
// audio is dropped with a warning, and the H.264 video is still converted.
VOID TEST(AppSrtSourceTest, Mp3Audio_DroppedVideoGoesOn)
{
    srs_error_t err;

    SrtSourceUtestStream s("mp3", SrsVideoCodecIdAVC, SrsAudioCodecIdMP3);
    HELPER_ASSERT_SUCCESS(s.publish());

    // An MPEG-1 Layer 3 frame header, 128kbps, 44.1kHz, and some of its data.
    const uint8_t mp3[] = {0xff, 0xfb, 0x90, 0x64, 0x00, 0x0f, 0xf0, 0x00, 0x00, 0x69, 0x00, 0x00};
    HELPER_ASSERT_SUCCESS(s.video(90000, 90000, srt_source_utest_h264_key()));
    HELPER_ASSERT_SUCCESS(s.audio(90000, string((const char *)mp3, sizeof(mp3))));
    HELPER_ASSERT_SUCCESS(s.video(93600, 93600, srt_source_utest_h264_p()));
    HELPER_ASSERT_SUCCESS(s.send());

    EXPECT_STREQ("17 00,17 01,27 01", srt_source_utest_kinds(s.rtmp_.tags_).c_str());
    EXPECT_TRUE(s.warned("unsupported stream codec=3"));
}

// An encoder puts the video in TS with length prefixes (AVCC or HVCC, as in MP4 or FLV) instead of annexb start codes,
// as a broken remuxer does. Each such PES is dropped with a warning, and once the encoder sends annexb, its key frame
// is converted and the stream API gets the codec.
VOID TEST(AppSrtSourceTest, VideoWithLengthPrefixes_DroppedUntilAnnexB)
{
    srs_error_t err;

    if (true) {
        SrtSourceUtestStream s("avcc");
        HELPER_ASSERT_SUCCESS(s.publish());

        string avcc = string("\x00\x00\x00\x08", 4) + string((const char *)srt_source_utest_idr, sizeof(srt_source_utest_idr));
        HELPER_ASSERT_SUCCESS(s.video(90000, 90000, avcc));
        HELPER_ASSERT_SUCCESS(s.send());
        EXPECT_TRUE(s.rtmp_.tags_.empty());
        EXPECT_TRUE(s.warned("demux annexb"));
        EXPECT_FALSE(s.format()->video_codec_reported_);

        HELPER_ASSERT_SUCCESS(s.video(93600, 93600, srt_source_utest_h264_key()));
        HELPER_ASSERT_SUCCESS(s.send());
        EXPECT_STREQ("17 00,17 01", srt_source_utest_kinds(s.rtmp_.tags_).c_str());
        EXPECT_TRUE(s.format()->video_codec_reported_);
        EXPECT_EQ(320, s.api_stream()->width_);
    }

    if (true) {
        SrtSourceUtestStream s("hvcc", SrsVideoCodecIdHEVC);
        HELPER_ASSERT_SUCCESS(s.publish());

        string hvcc = string("\x00\x00\x00\x0e", 4) + string((const char *)srt_source_utest_hevc_idr, sizeof(srt_source_utest_hevc_idr));
        HELPER_ASSERT_SUCCESS(s.video(90000, 90000, hvcc));
        HELPER_ASSERT_SUCCESS(s.send());
        EXPECT_TRUE(s.rtmp_.tags_.empty());
        EXPECT_TRUE(s.warned("demux hevc annexb"));

        HELPER_ASSERT_SUCCESS(s.video(93600, 93600, srt_source_utest_hevc_params() + srt_source_utest_nalu(srt_source_utest_hevc_idr, sizeof(srt_source_utest_hevc_idr))));
        HELPER_ASSERT_SUCCESS(s.send());
        EXPECT_STREQ("90 68,93 68", srt_source_utest_kinds(s.rtmp_.tags_).c_str());
    }
}

// An encoder writes two start codes in a row, an empty NALU, at the start or end of its PES or before a slice. The server
// skips the empty NALU instead of reading its type, so the H.264 and HEVC frames are converted whole and the stream API
// gets the codec.
VOID TEST(AppSrtSourceTest, TwoStartCodesInARow_EmptyNaluSkipped)
{
    srs_error_t err;

    string start_code("\x00\x00\x00\x01", 4);

    if (true) {
        SrtSourceUtestStream s("h264-empty-nalu");
        HELPER_ASSERT_SUCCESS(s.publish());

        string au = start_code + srt_source_utest_nalu(srt_source_utest_sps, sizeof(srt_source_utest_sps)) +
                    srt_source_utest_nalu(srt_source_utest_pps, sizeof(srt_source_utest_pps)) + start_code +
                    srt_source_utest_nalu(srt_source_utest_idr, sizeof(srt_source_utest_idr)) + start_code;
        HELPER_ASSERT_SUCCESS(s.video(90000, 90000, au));
        HELPER_ASSERT_SUCCESS(s.send());

        EXPECT_STREQ("17 00,17 01", srt_source_utest_kinds(s.rtmp_.tags_).c_str());
        // One NALU, the IDR slice, after the 5-byte tag header and its 4-byte size.
        EXPECT_EQ(5 + 4 + (int)sizeof(srt_source_utest_idr), (int)s.rtmp_.tags_[1].size());
        EXPECT_TRUE(s.format()->video_codec_reported_);
        EXPECT_EQ(240, s.api_stream()->height_);
    }

    if (true) {
        SrtSourceUtestStream s("hevc-empty-nalu", SrsVideoCodecIdHEVC);
        HELPER_ASSERT_SUCCESS(s.publish());

        string au = srt_source_utest_hevc_params() + start_code + srt_source_utest_nalu(srt_source_utest_hevc_idr, sizeof(srt_source_utest_hevc_idr)) + start_code;
        HELPER_ASSERT_SUCCESS(s.video(90000, 90000, au));
        HELPER_ASSERT_SUCCESS(s.send());

        EXPECT_STREQ("90 68,93 68", srt_source_utest_kinds(s.rtmp_.tags_).c_str());
        EXPECT_EQ(5 + 4 + (int)sizeof(srt_source_utest_hevc_idr), (int)s.rtmp_.tags_[1].size());
    }
}

// An encoder sends an SPS cut to 3 bytes, as over a link that cuts the PES, then a new PPS with a key frame. The server
// keeps no SPS to mux a sequence header with, so it refuses that key frame instead of aborting, and converts the stream
// again from the next key frame with a whole SPS.
VOID TEST(AppSrtSourceTest, CutSpsThenNewPps_KeyFrameRefusedUntilWholeSps)
{
    srs_error_t err;

    SrtSourceUtestStream s("cut-sps");
    HELPER_ASSERT_SUCCESS(s.publish());

    const uint8_t pps[] = {0x68, 0xce, 0x38, 0x80};
    string au = srt_source_utest_nalu(srt_source_utest_sps, 3) + srt_source_utest_nalu(pps, sizeof(pps)) +
                srt_source_utest_nalu(srt_source_utest_idr, sizeof(srt_source_utest_idr));
    HELPER_ASSERT_SUCCESS(s.video(90000, 90000, au));
    HELPER_ASSERT_SUCCESS(s.send());
    EXPECT_TRUE(s.rtmp_.tags_.empty());
    EXPECT_TRUE(s.warned("sps or pps empty"));

    HELPER_ASSERT_SUCCESS(s.video(93600, 93600, srt_source_utest_h264_key()));
    HELPER_ASSERT_SUCCESS(s.send());
    EXPECT_STREQ("17 00,17 01", srt_source_utest_kinds(s.rtmp_.tags_).c_str());
}

// An encoder sends the parameter sets in a PES of their own before each key frame's PES, as some hardware encoders do.
// The RTMP side gets the sequence header from the first PES and the frame from the second, for H.264 and for HEVC.
VOID TEST(AppSrtSourceTest, ParameterSetsInOwnPes_HeaderThenFrame)
{
    srs_error_t err;

    if (true) {
        SrtSourceUtestStream s("h264-params-pes");
        HELPER_ASSERT_SUCCESS(s.publish());

        string params = srt_source_utest_nalu(srt_source_utest_sps, sizeof(srt_source_utest_sps)) +
                        srt_source_utest_nalu(srt_source_utest_pps, sizeof(srt_source_utest_pps));
        HELPER_ASSERT_SUCCESS(s.video(90000, 90000, params));
        HELPER_ASSERT_SUCCESS(s.video(90000, 90000, srt_source_utest_nalu(srt_source_utest_idr, sizeof(srt_source_utest_idr))));
        HELPER_ASSERT_SUCCESS(s.send());

        EXPECT_STREQ("17 00,17 01", srt_source_utest_kinds(s.rtmp_.tags_).c_str());
        // The PES of only parameter sets has no frame to convert; the server says so and goes on.
        EXPECT_TRUE(s.warned("empty frame"));
    }

    if (true) {
        SrtSourceUtestStream s("hevc-params-pes", SrsVideoCodecIdHEVC);
        HELPER_ASSERT_SUCCESS(s.publish());

        HELPER_ASSERT_SUCCESS(s.video(90000, 90000, srt_source_utest_hevc_params()));
        HELPER_ASSERT_SUCCESS(s.video(90000, 90000, srt_source_utest_nalu(srt_source_utest_hevc_idr, sizeof(srt_source_utest_hevc_idr))));
        HELPER_ASSERT_SUCCESS(s.video(93600, 93600, srt_source_utest_nalu(srt_source_utest_hevc_p, sizeof(srt_source_utest_hevc_p))));
        HELPER_ASSERT_SUCCESS(s.send());

        EXPECT_STREQ("90 68,93 68,a3 68", srt_source_utest_kinds(s.rtmp_.tags_).c_str());
        EXPECT_FALSE(s.warned("empty frame"));
    }
}

// The live source refuses the tags of an SRT stream for a while, as it does for a tag its format cannot parse. Each
// refused sequence header and frame is dropped with a warning naming it, and the frames that come after the live source
// takes tags again are converted, for H.264 with AAC and for HEVC.
VOID TEST(AppSrtSourceTest, LiveSourceRefusesTags_DroppedThenConversionGoesOn)
{
    srs_error_t err;

    if (true) {
        SrtSourceUtestStream s("refused-h264");
        HELPER_ASSERT_SUCCESS(s.publish());

        s.rtmp_.refuse_ = true;
        HELPER_ASSERT_SUCCESS(s.video(90000, 90000, srt_source_utest_h264_key()));
        HELPER_ASSERT_SUCCESS(s.audio(90000, srt_source_utest_adts_frame()));
        HELPER_ASSERT_SUCCESS(s.video(93600, 93600, srt_source_utest_h264_p()));
        HELPER_ASSERT_SUCCESS(s.audio(92000, srt_source_utest_adts_frame()));
        HELPER_ASSERT_SUCCESS(s.send());
        EXPECT_TRUE(s.rtmp_.tags_.empty());
        EXPECT_TRUE(s.warned("srt to rtmp sps/pps"));
        EXPECT_TRUE(s.warned("srt ts video to rtmp"));
        EXPECT_TRUE(s.warned("audio sh"));
        EXPECT_TRUE(s.warned("audio frame"));

        s.rtmp_.refuse_ = false;
        HELPER_ASSERT_SUCCESS(s.video(97200, 97200, srt_source_utest_h264_p()));
        HELPER_ASSERT_SUCCESS(s.audio(94000, srt_source_utest_adts_frame()));
        HELPER_ASSERT_SUCCESS(s.send());
        EXPECT_STREQ("27 01,af 01", srt_source_utest_kinds(s.rtmp_.tags_).c_str());
    }

    if (true) {
        SrtSourceUtestStream s("refused-hevc", SrsVideoCodecIdHEVC);
        HELPER_ASSERT_SUCCESS(s.publish());

        s.rtmp_.refuse_ = true;
        HELPER_ASSERT_SUCCESS(s.video(90000, 90000, srt_source_utest_hevc_params() + srt_source_utest_nalu(srt_source_utest_hevc_idr, sizeof(srt_source_utest_hevc_idr))));
        HELPER_ASSERT_SUCCESS(s.video(93600, 93600, srt_source_utest_nalu(srt_source_utest_hevc_p, sizeof(srt_source_utest_hevc_p))));
        HELPER_ASSERT_SUCCESS(s.send());
        EXPECT_TRUE(s.rtmp_.tags_.empty());
        EXPECT_TRUE(s.warned("srt to rtmp vps/sps/pps"));
        EXPECT_TRUE(s.warned("srt ts hevc video to rtmp"));

        s.rtmp_.refuse_ = false;
        HELPER_ASSERT_SUCCESS(s.video(97200, 97200, srt_source_utest_nalu(srt_source_utest_hevc_p, sizeof(srt_source_utest_hevc_p))));
        HELPER_ASSERT_SUCCESS(s.send());
        EXPECT_STREQ("a3 68", srt_source_utest_kinds(s.rtmp_.tags_).c_str());
    }
}

// An encoder's audio PES that is not ADTS, such as LATM, then one whose ADTS header has the reserved profile, as from a
// corrupt header. Neither is converted to RTMP nor reported to the stream API; the first valid ADTS PES is both.
VOID TEST(AppSrtSourceTest, AudioNotAdtsOrReservedProfile_DroppedUntilValidAdts)
{
    srs_error_t err;

    SrtSourceUtestStream s("bad-adts");
    HELPER_ASSERT_SUCCESS(s.publish());

    // A LATM AudioMuxElement starts with the 11-bit sync word 0x2b7.
    const uint8_t latm[] = {0x56, 0xe0, 0x1b, 0x20, 0x00, 0x11, 0x90, 0x00, 0x21, 0x11};
    HELPER_ASSERT_SUCCESS(s.audio(90000, string((const char *)latm, sizeof(latm))));
    HELPER_ASSERT_SUCCESS(s.send());
    EXPECT_TRUE(s.warned("not adts"));

    string reserved = srt_source_utest_adts_frame();
    reserved[2] = (char)0xd0; // profile 3
    HELPER_ASSERT_SUCCESS(s.audio(92000, reserved));
    HELPER_ASSERT_SUCCESS(s.send());
    EXPECT_TRUE(s.warned("invalid aac object"));
    EXPECT_TRUE(s.rtmp_.tags_.empty());
    EXPECT_FALSE(s.format()->audio_codec_reported_);

    HELPER_ASSERT_SUCCESS(s.audio(94000, srt_source_utest_adts_frame()));
    HELPER_ASSERT_SUCCESS(s.send());
    EXPECT_STREQ("af 00,af 01", srt_source_utest_kinds(s.rtmp_.tags_).c_str());
    EXPECT_TRUE(s.format()->audio_codec_reported_);
    EXPECT_EQ(SrsAudioCodecIdAAC, s.api_stream()->acodec_);
}

// An encoder's audio PES starts with an ADTS header of no raw data (frame_length 7), as a cut or padding frame. The
// server skips it: the next frame of the PES is converted at the PES time, and the stream API gets the codec from the
// next PES.
VOID TEST(AppSrtSourceTest, AdtsHeaderWithNoData_SkippedNextFrameConverted)
{
    srs_error_t err;

    SrtSourceUtestStream s("empty-adts");
    HELPER_ASSERT_SUCCESS(s.publish());

    const uint8_t empty[] = {0xff, 0xf1, 0x50, 0x80, 0x00, 0xff, 0xfc};
    HELPER_ASSERT_SUCCESS(s.audio(90000, string((const char *)empty, sizeof(empty)) + srt_source_utest_adts_frame()));
    HELPER_ASSERT_SUCCESS(s.send());
    EXPECT_STREQ("af 00,af 01", srt_source_utest_kinds(s.rtmp_.tags_).c_str());
    EXPECT_EQ(1000, s.rtmp_.times_[1]);
    EXPECT_FALSE(s.format()->audio_codec_reported_);

    HELPER_ASSERT_SUCCESS(s.audio(92000, srt_source_utest_adts_frame()));
    HELPER_ASSERT_SUCCESS(s.send());
    EXPECT_TRUE(s.format()->audio_codec_reported_);
}

// An encoder that the server joins mid GOP, or that starts with audio: P frames and AAC come before the first key frame.
// The stream API gets the audio codec from the first AAC, and the video codec only from the key frame's SPS and PPS.
VOID TEST(AppSrtSourceTest, AudioBeforeFirstKeyFrame_EachCodecReportedWhenFound)
{
    srs_error_t err;

    SrtSourceUtestStream s("mid-gop");
    HELPER_ASSERT_SUCCESS(s.publish());

    HELPER_ASSERT_SUCCESS(s.audio(90000, srt_source_utest_adts_frame()));
    HELPER_ASSERT_SUCCESS(s.video(90000, 90000, srt_source_utest_h264_p()));
    HELPER_ASSERT_SUCCESS(s.audio(92000, srt_source_utest_adts_frame()));
    HELPER_ASSERT_SUCCESS(s.video(93600, 93600, srt_source_utest_h264_p()));
    HELPER_ASSERT_SUCCESS(s.send());
    EXPECT_TRUE(s.format()->audio_codec_reported_);
    EXPECT_FALSE(s.format()->video_codec_reported_);
    EXPECT_TRUE(s.api_stream()->has_audio_);
    EXPECT_FALSE(s.api_stream()->has_video_);

    HELPER_ASSERT_SUCCESS(s.video(97200, 97200, srt_source_utest_h264_key()));
    HELPER_ASSERT_SUCCESS(s.send());
    EXPECT_TRUE(s.format()->video_codec_reported_);
    EXPECT_TRUE(s.api_stream()->has_video_);
    EXPECT_EQ(320, s.api_stream()->width_);
}

// An HEVC encoder with AAC. The server does not parse HEVC parameter sets for the stream API yet, so it stops looking
// for the video codec at the first HEVC PES and reports only the audio codec, then counts the frames of both from the
// TS headers.
VOID TEST(AppSrtSourceTest, HevcWithAac_AudioReportedAndFramesCounted)
{
    srs_error_t err;

    SrtSourceUtestStream s("hevc-aac", SrsVideoCodecIdHEVC);
    HELPER_ASSERT_SUCCESS(s.publish());

    HELPER_ASSERT_SUCCESS(s.video(90000, 90000, srt_source_utest_hevc_params() + srt_source_utest_nalu(srt_source_utest_hevc_idr, sizeof(srt_source_utest_hevc_idr))));
    HELPER_ASSERT_SUCCESS(s.audio(90000, srt_source_utest_adts_frame()));
    HELPER_ASSERT_SUCCESS(s.video(93600, 93600, srt_source_utest_nalu(srt_source_utest_hevc_p, sizeof(srt_source_utest_hevc_p))));
    HELPER_ASSERT_SUCCESS(s.send());

    EXPECT_TRUE(s.format()->video_codec_reported_);
    EXPECT_TRUE(s.format()->audio_codec_reported_);
    EXPECT_TRUE(s.api_stream()->has_audio_);
    EXPECT_FALSE(s.api_stream()->has_video_);
    EXPECT_EQ(2, s.format()->nn_video_frames_);
    EXPECT_EQ(1, s.format()->nn_audio_frames_);
}

// FFmpeg sends an SDT (service description, PID 0x11) table in its TS, which starts a payload unit as each PES does.
// The server counts only the PES of the audio and video PIDs as frames, not the SDT packets.
VOID TEST(AppSrtSourceTest, FfmpegSdtPackets_NotCountedAsFrames)
{
    srs_error_t err;

    SrtSourceUtestStream s("sdt");
    HELPER_ASSERT_SUCCESS(s.publish());

    string sdt(SRS_TS_PACKET_SIZE, (char)0xff);
    const uint8_t sdt_head[] = {0x47, 0x40, 0x11, 0x10, 0x00, 0x42, 0xf0, 0x25, 0x00, 0x01, 0xc1, 0x00, 0x00};
    sdt.replace(0, sizeof(sdt_head), (const char *)sdt_head, sizeof(sdt_head));

    HELPER_ASSERT_SUCCESS(s.video(90000, 90000, srt_source_utest_h264_key()));
    HELPER_ASSERT_SUCCESS(s.audio(90000, srt_source_utest_adts_frame()));
    s.writer_.data_ += sdt;
    HELPER_ASSERT_SUCCESS(s.video(93600, 93600, srt_source_utest_h264_p()));
    s.writer_.data_ += sdt;
    HELPER_ASSERT_SUCCESS(s.send());

    EXPECT_EQ(2, s.format()->nn_video_frames_);
    EXPECT_EQ(1, s.format()->nn_audio_frames_);
    EXPECT_STREQ("17 00,17 01,af 00,af 01,27 01", srt_source_utest_kinds(s.rtmp_.tags_).c_str());
}

// An encoder's key frame whose SPS is cut after its level, as over a lossy link, then the next key frame with a whole
// SPS. The stream API gets no video codec from the cut SPS, and gets 320x240 from the next key frame.
VOID TEST(AppSrtSourceTest, KeyFrameWithCutSps_VideoReportedFromNextKeyFrame)
{
    srs_error_t err;

    SrtSourceUtestStream s("cut-sps-api");
    HELPER_ASSERT_SUCCESS(s.publish());

    string cut = srt_source_utest_nalu(srt_source_utest_sps, 4) + srt_source_utest_nalu(srt_source_utest_pps, sizeof(srt_source_utest_pps)) +
                 srt_source_utest_nalu(srt_source_utest_idr, sizeof(srt_source_utest_idr));
    HELPER_ASSERT_SUCCESS(s.video(90000, 90000, cut));
    HELPER_ASSERT_SUCCESS(s.send());
    EXPECT_FALSE(s.format()->video_codec_reported_);
    EXPECT_TRUE(s.warned("parse video codec"));

    HELPER_ASSERT_SUCCESS(s.video(93600, 93600, srt_source_utest_h264_key()));
    HELPER_ASSERT_SUCCESS(s.send());
    EXPECT_TRUE(s.format()->video_codec_reported_);
    EXPECT_EQ(320, s.api_stream()->width_);
    EXPECT_EQ(240, s.api_stream()->height_);
}

// As above, but with a P frame between the key frame of the cut SPS and the next key frame, as in any GOP. The stream API
// should get 320x240 from the next key frame.
VOID TEST(AppSrtSourceTest, DISABLED_KeyFrameWithCutSpsThenPFrame_VideoReportedAs0x0)
{
    srs_error_t err;

    SrtSourceUtestStream s("cut-sps-gop");
    HELPER_ASSERT_SUCCESS(s.publish());

    string cut = srt_source_utest_nalu(srt_source_utest_sps, 4) + srt_source_utest_nalu(srt_source_utest_pps, sizeof(srt_source_utest_pps)) +
                 srt_source_utest_nalu(srt_source_utest_idr, sizeof(srt_source_utest_idr));
    HELPER_ASSERT_SUCCESS(s.video(90000, 90000, cut));
    HELPER_ASSERT_SUCCESS(s.video(93600, 93600, srt_source_utest_h264_p()));
    HELPER_ASSERT_SUCCESS(s.video(97200, 97200, srt_source_utest_h264_key()));
    HELPER_ASSERT_SUCCESS(s.send());

    ASSERT_TRUE(s.api_stream() != NULL);
    EXPECT_EQ(320, s.api_stream()->width_);
    EXPECT_EQ(240, s.api_stream()->height_);
}

// An SRT encoder leaves a stream with no player. The source stays for 3s, so an encoder that reconnects finds it, and
// the 3s check of the source manager frees it after that, while a stream a player waits on stays until 3s after the
// player gives up.
VOID TEST(AppSrtSourceTest, EncoderLeftWithNoPlayer_SourceFreedAfterThreeSeconds)
{
    srs_error_t err;

    MockClockForPithyPrint clock;
    clock.now_ = 10 * SRS_UTIME_SECONDS;
    SrsStatistic stat;

    SrsSrtSourceManager manager;
    SrsRequest req;
    req.vhost_ = "__defaultVhost__";
    req.app_ = "live";
    req.stream_ = "left";

    SrsSharedPtr<SrsSrtSource> source;
    HELPER_ASSERT_SUCCESS(manager.fetch_or_create(&req, source));
    source->clk_ = &clock;
    source->stat_ = &stat;
    HELPER_ASSERT_SUCCESS(source->on_publish());
    source->on_unpublish();

    // A player waits on another stream, which nobody publishes.
    SrsRequest waited_req;
    waited_req.vhost_ = "__defaultVhost__";
    waited_req.app_ = "live";
    waited_req.stream_ = "waited";
    SrsSharedPtr<SrsSrtSource> waited;
    HELPER_ASSERT_SUCCESS(manager.fetch_or_create(&waited_req, waited));
    waited->clk_ = &clock;
    ISrsSrtConsumer *player = NULL;
    HELPER_ASSERT_SUCCESS(waited->create_consumer(player));

    // Just under 3s after the encoder left, the source is kept.
    clock.now_ = 12 * SRS_UTIME_SECONDS + 999 * SRS_UTIME_MILLISECONDS;
    HELPER_EXPECT_SUCCESS(manager.notify(0, 3 * SRS_UTIME_SECONDS, clock.now_));
    EXPECT_EQ(2, (int)manager.pool_.size());

    // At 3s, it is freed, and the stream with the player stays.
    clock.now_ = 13 * SRS_UTIME_SECONDS;
    HELPER_EXPECT_SUCCESS(manager.notify(0, 3 * SRS_UTIME_SECONDS, clock.now_));
    EXPECT_EQ(1, (int)manager.pool_.size());
    EXPECT_TRUE(manager.pool_.find(waited_req.get_stream_url()) != manager.pool_.end());

    // The player gives up, and the stream it waited on, never published, is freed 3s later.
    srs_freep(player);
    clock.now_ = 15 * SRS_UTIME_SECONDS + 999 * SRS_UTIME_MILLISECONDS;
    HELPER_EXPECT_SUCCESS(manager.notify(0, 3 * SRS_UTIME_SECONDS, clock.now_));
    EXPECT_EQ(1, (int)manager.pool_.size());
    clock.now_ = 16 * SRS_UTIME_SECONDS;
    HELPER_EXPECT_SUCCESS(manager.notify(0, 3 * SRS_UTIME_SECONDS, clock.now_));
    EXPECT_TRUE(manager.pool_.empty());

    // An encoder that comes now gets a new source.
    SrsSharedPtr<SrsSrtSource> fresh;
    HELPER_ASSERT_SUCCESS(manager.fetch_or_create(&req, fresh));
    EXPECT_TRUE(fresh.get() != source.get());

    source->stat_ = NULL;
}

// The server starts the SRT source manager, whose timer frees the sources of gone streams. When no coroutine can be
// created for that timer, as when the process is out of memory at startup, the start fails with the ST error, so the
// server quits instead of keeping every stream's source forever.
VOID TEST(AppSrtSourceTest, NoCoroutineForSourcesTimer_ServerFailsToStart)
{
    srs_error_t err;

    SrsSrtSourceManager manager;
    manager.assemble();

    _ST_THREAD_CREATE_PFN ov = _pfn_st_thread_create;
    _pfn_st_thread_create = (_ST_THREAD_CREATE_PFN)srt_listener_utest_st_thread_create_out_of_memory;
    err = manager.initialize();
    _pfn_st_thread_create = ov;

    EXPECT_EQ(ERROR_ST_CREATE_CYCLE_THREAD, srs_error_code(err));
    EXPECT_TRUE(srs_error_desc(err).find("timer") != string::npos);
    srs_freep(err);
}

// The video of a stream as the stream API lists it, from the server's registry, or NULL if the stream or its video is
// not listed.
static SrsJsonObject *statistic_utest_listed_video(SrsJsonArray *streams, string name)
{
    for (int i = 0; i < streams->count(); i++) {
        SrsJsonObject *stream = streams->at(i)->to_object();
        SrsJsonAny *stream_name = stream->get_property("name");
        if (!stream_name || stream_name->to_str() != name) {
            continue;
        }

        SrsJsonAny *video = stream->get_property("video");
        return (video && video->is_object()) ? video->to_object() : NULL;
    }
    return NULL;
}

// WHIP publishers of VP9 and of AV1, which SRS relays to WebRTC players without parsing the bitstream: the stream API
// lists each stream's video codec as the SDP named it, with profile and level null (the SDP gives none SRS reads) and
// no size, for the console and for clients that read the API.
VOID TEST(AppStatisticTest, WhipVp9AndAv1Publishers_ListedWithoutProfileOrLevel)
{
    srs_error_t err;

    SrsUniquePtr<SrsStatistic> stat(new SrsStatistic());
    const char *codecs[] = {"VP9", "AV1"};
    const char *names[] = {"whip_vp9", "whip_av1"};

    for (int i = 0; i < 2; i++) {
        MockRequest req("__defaultVhost__", "live", names[i]);

        SrsRtcTrackDescription desc;
        desc.type_ = "video";
        desc.ssrc_ = 5678 + i;
        desc.media_ = new SrsVideoPayload(98 + i, codecs[i], 90000);
        MockRtcPacketReceiver receiver;
        SrsRtcVideoRecvTrack track(&receiver, &desc, false);

        SrsRtcFormat format;
        format.stat_ = stat.get();
        HELPER_ASSERT_SUCCESS(format.initialize(&req));

        // The first video packet of the publisher, a frame of the relayed codec.
        SrsRtpPacket frame;
        HELPER_EXPECT_SUCCESS(format.on_rtp_packet(&track, &frame, false));
    }

    SrsUniquePtr<SrsJsonArray> streams(SrsJsonAny::array());
    HELPER_ASSERT_SUCCESS(stat->dumps_streams(streams.get(), 0, 10));
    ASSERT_EQ(2, streams->count());

    for (int i = 0; i < 2; i++) {
        SrsJsonObject *video = statistic_utest_listed_video(streams.get(), names[i]);
        ASSERT_TRUE(video != NULL) << names[i];

        SrsJsonAny *codec = video->get_property("codec");
        ASSERT_TRUE(codec != NULL && codec->is_string()) << names[i];
        EXPECT_STREQ(codecs[i], codec->to_str().c_str());

        SrsJsonAny *profile = video->get_property("profile");
        ASSERT_TRUE(profile != NULL) << names[i];
        EXPECT_TRUE(profile->is_null()) << names[i];
        SrsJsonAny *level = video->get_property("level");
        ASSERT_TRUE(level != NULL) << names[i];
        EXPECT_TRUE(level->is_null()) << names[i];

        EXPECT_EQ(0, video->get_property("width")->to_integer()) << names[i];
        EXPECT_EQ(0, video->get_property("height")->to_integer()) << names[i];
    }
}

#ifdef SRS_FFMPEG_FIT
// Makes the origin hub of a live stream as the server does, reading the test config, so it forwards as the vhost says;
// its HLS, DASH, DVR, transcoder and exec do nothing.
class StreamBridgeUtestHubFactory : public SrsAppFactory
{
public:
    ISrsAppConfig *config_;
    ISrsStatistic *stat_;

public:
    StreamBridgeUtestHubFactory()
    {
        config_ = NULL;
        stat_ = NULL;
    }

public:
    virtual ISrsOriginHub *create_origin_hub()
    {
        SrsOriginHub *hub = new SrsOriginHub();
        hub->config_ = config_;
        hub->stat_ = stat_;

        srs_freep(hub->hls_);
        hub->hls_ = new MockHlsForOriginHub();
        srs_freep(hub->dash_);
        hub->dash_ = new MockDashForOriginHub();
        srs_freep(hub->dvr_);
        hub->dvr_ = new MockDvrForOriginHub();
        srs_freep(hub->encoder_);
        hub->encoder_ = new MockMediaEncoderForOriginHub();
        srs_freep(hub->ng_exec_);
        hub->ng_exec_ = new MockNgExecForOriginHub();

        hub->assemble();
        return hub;
    }
};

// A stream of the server converted between RTMP and WebRTC: the real SrsLiveSource of its RTMP side and the real
// SrsRtcSource of its WebRTC side, with the vhost from config text and a real statistic, joined by the bridge the
// encoder's connection makes: SrsRtcBridge for a WHIP publisher (rtc_to_rtmp), SrsRtmpBridge for an RTMP encoder
// (rtmp_to_rtc), SrsSrtBridge for an SRT encoder (srt_to_rtmp and rtmp_to_rtc) with its real SrsSrtSource. Players of
// both sides take what the stream gets.
class StreamBridgeUtestStream
{
public:
    MockSrsConfig conf_;
    SrsStatistic stat_;
    MockLiveSourceHandler handler_;
    MockClockForPithyPrint clock_;
    StreamBridgeUtestHubFactory factory_;
    SrsRequest req_;
    SrsSharedPtr<SrsLiveSource> live_;
    SrsSharedPtr<SrsRtcSource> rtc_;
    SrsSharedPtr<SrsSrtSource> srt_;
    SrsTsContext ts_;
    MockLogForSignal log_;
    vector<SrsLiveConsumer *> rtmp_players_;
    vector<SrsRtcConsumer *> rtc_players_;

public:
    StreamBridgeUtestStream()
    {
        req_.vhost_ = "__defaultVhost__";
        req_.app_ = "live";
        req_.stream_ = "livestream";
        clock_.now_ = 100 * SRS_UTIME_SECONDS;
    }
    virtual ~StreamBridgeUtestStream()
    {
        for (int i = 0; i < (int)rtmp_players_.size(); i++) {
            SrsLiveConsumer *player = rtmp_players_.at(i);
            srs_freep(player);
        }
        for (int i = 0; i < (int)rtc_players_.size(); i++) {
            SrsRtcConsumer *player = rtc_players_.at(i);
            srs_freep(player);
        }
    }

public:
    // Creates both sources of the stream, as the source managers do.
    srs_error_t open(string text)
    {
        srs_error_t err = srs_success;

        if ((err = conf_.mock_parse(string(_MIN_OK_CONF) + text)) != srs_success) {
            return srs_error_wrap(err, "parse");
        }
        factory_.config_ = &conf_;
        factory_.stat_ = &stat_;

        SrsLiveSource *live = new SrsLiveSource();
        live->config_ = &conf_;
        live->stat_ = &stat_;
        live->handler_ = &handler_;
        live->app_factory_ = &factory_;
        live->clk_ = &clock_;
        live_ = SrsSharedPtr<SrsLiveSource>(live);
        live->assemble();
        if ((err = live->initialize(live_, &req_)) != srs_success) {
            return srs_error_wrap(err, "live source");
        }

        SrsRtcSource *rtc = new SrsRtcSource();
        rtc->config_ = &conf_;
        rtc->stat_ = &stat_;
        rtc_ = SrsSharedPtr<SrsRtcSource>(rtc);
        if ((err = rtc->initialize(&req_)) != srs_success) {
            return srs_error_wrap(err, "rtc source");
        }

        return err;
    }
    // A WHIP publisher of Opus and H.264 starts to send, after DTLS, as SrsRtcPublishStream::initialize and start do.
    srs_error_t whip_publish()
    {
        srs_error_t err = srs_success;

        SrsRtcSourceDescription desc;
        SrsRtcTrackDescription *audio = new SrsRtcTrackDescription();
        audio->type_ = "audio";
        audio->id_ = "audio-whip";
        audio->ssrc_ = 1234;
        audio->media_ = new SrsAudioPayload(111, "opus", 48000, 2);
        desc.audio_track_desc_ = audio;
        SrsRtcTrackDescription *video = new SrsRtcTrackDescription();
        video->type_ = "video";
        video->id_ = "video-whip";
        video->ssrc_ = 5678;
        video->media_ = new SrsVideoPayload(106, "H264", 90000);
        desc.video_track_descs_.push_back(video);
        rtc_->set_stream_desc(&desc);

        SrsRtcBridge *bridge = new SrsRtcBridge(_srs_app_factory);
        live_->set_cache(false);
        bridge->enable_rtc2rtmp(live_);
        if ((err = bridge->initialize(&req_)) != srs_success) {
            srs_freep(bridge);
            return srs_error_wrap(err, "bridge");
        }
        rtc_->set_bridge(bridge);

        return rtc_->on_publish();
    }
    // An Opus packet of the WHIP publisher, as its audio track gives it to the source.
    srs_error_t whip_opus(uint16_t seq, int64_t avsync_time, const string &opus)
    {
        SrsRtpPacket pkt;
        pkt.header_.set_ssrc(1234);
        pkt.header_.set_sequence(seq);
        pkt.header_.set_timestamp((uint32_t)(avsync_time * 48));
        pkt.header_.set_payload_type(111);
        pkt.frame_type_ = SrsFrameTypeAudio;
        pkt.set_avsync_time(avsync_time);

        SrsRtpRawPayload *raw = new SrsRtpRawPayload();
        raw->payload_ = pkt.wrap((int)opus.size());
        raw->nn_payload_ = (int)opus.size();
        memcpy(raw->payload_, opus.data(), opus.size());
        pkt.set_payload(raw, SrsRtpPacketPayloadTypeRaw);

        return rtc_->on_rtp(&pkt);
    }
    // An RTMP encoder starts to publish, as SrsRtmpConn::acquire_publish does on a vhost with rtmp_to_rtc.
    srs_error_t rtmp_publish()
    {
        srs_error_t err = srs_success;

        SrsRtmpBridge *bridge = new SrsRtmpBridge(_srs_app_factory);
        bridge->enable_rtmp2rtc(rtc_);
        if ((err = bridge->initialize(&req_)) != srs_success) {
            srs_freep(bridge);
            return srs_error_wrap(err, "bridge");
        }
        live_->set_bridge(bridge);

        return live_->on_publish();
    }
    srs_error_t rtmp_video(int64_t timestamp, const uint8_t *data, int size)
    {
        SrsRtmpCommonMessage msg;
        msg.header_.initialize_video(size, (uint32_t)timestamp, 1);
        msg.create_payload(size);
        memcpy(msg.payload(), data, size);
        return live_->on_video(&msg);
    }
    srs_error_t rtmp_audio(int64_t timestamp, const uint8_t *data, int size)
    {
        SrsRtmpCommonMessage msg;
        msg.header_.initialize_audio(size, (uint32_t)timestamp, 1);
        msg.create_payload(size);
        memcpy(msg.payload(), data, size);
        return live_->on_audio(&msg);
    }
    // An SRT encoder starts to publish, as SrsMpegtsSrtConn::acquire_publish does on a vhost with srt_to_rtmp and
    // rtmp_to_rtc: the bridge converts the stream to both RTMP and WebRTC.
    srs_error_t srt_publish()
    {
        srs_error_t err = srs_success;

        srt_ = SrsSharedPtr<SrsSrtSource>(new SrsSrtSource());
        srt_->assemble();
        srt_->stat_ = &stat_;
        dynamic_cast<SrsSrtFormat *>(srt_->format_)->stat_ = &stat_;
        if ((err = srt_->initialize(&req_)) != srs_success) {
            return srs_error_wrap(err, "srt source");
        }

        SrsSrtBridge *bridge = new SrsSrtBridge(_srs_app_factory);
        bridge->assemble();
        bridge->enable_srt2rtmp(live_);
        bridge->enable_srt2rtc(rtc_);
        if ((err = bridge->initialize(&req_)) != srs_success) {
            srs_freep(bridge);
            return srs_error_wrap(err, "bridge");
        }
        srt_->set_bridge(bridge);

        return srt_->on_publish();
    }
    // The SRT encoder sends a PES of one ADTS frame at the time in ms, muxed in TS as FFmpeg or OBS does, in SRT packets
    // of 7 TS packets, with the server's log kept.
    srs_error_t srt_audio(int64_t timestamp, const string &adts)
    {
        srs_error_t err = srs_success;

        SrsTsMessage msg;
        msg.sid_ = SrsTsPESStreamIdAudioCommon;
        msg.dts_ = msg.pts_ = timestamp * 90;
        msg.payload_->append(adts.data(), (int)adts.length());
        SrtSourceUtestTsWriter writer;
        if ((err = ts_.encode(&writer, &msg, SrsVideoCodecIdAVC, SrsAudioCodecIdAAC)) != srs_success) {
            return srs_error_wrap(err, "mux");
        }

        ISrsLog *original = _srs_log;
        _srs_log = &log_;

        const int nb_srt = 7 * SRS_TS_PACKET_SIZE;
        for (size_t off = 0; off < writer.data_.size(); off += nb_srt) {
            int size = (int)srs_min((size_t)nb_srt, writer.data_.size() - off);
            SrsUniquePtr<SrsSrtPacket> pkt(new SrsSrtPacket());
            pkt->wrap((char *)writer.data_.data() + off, size);
            if ((err = srt_->on_srt_packet(pkt.get())) != srs_success) {
                break;
            }
        }

        _srs_log = original;
        return err;
    }
    // How many warnings the server logged with the text.
    int warned(const string &text)
    {
        int n = 0;
        for (int i = 0; i < (int)log_.warns_.size(); i++) {
            if (log_.warns_[i].find(text) != string::npos) {
                n++;
            }
        }
        return n;
    }
    // An RTMP player joins, as SrsRtmpConn::playing does.
    SrsLiveConsumer *rtmp_play()
    {
        SrsLiveConsumer *player = NULL;
        srs_error_t err = live_->create_consumer(player);
        EXPECT_TRUE(err == srs_success) << srs_error_desc(err);
        srs_freep(err);
        if (!player) {
            return NULL;
        }
        rtmp_players_.push_back(player);

        err = live_->consumer_dumps(player, true, true, true);
        EXPECT_TRUE(err == srs_success) << srs_error_desc(err);
        srs_freep(err);
        return player;
    }
    // A WebRTC player joins, as SrsRtcPlayStream does.
    SrsRtcConsumer *rtc_play()
    {
        ISrsRtcConsumer *player = NULL;
        srs_error_t err = rtc_->create_consumer(player);
        EXPECT_TRUE(err == srs_success) << srs_error_desc(err);
        srs_freep(err);
        if (!player) {
            return NULL;
        }
        SrsRtcConsumer *consumer = dynamic_cast<SrsRtcConsumer *>(player);
        rtc_players_.push_back(consumer);

        err = rtc_->consumer_dumps(player);
        EXPECT_TRUE(err == srs_success) << srs_error_desc(err);
        srs_freep(err);
        return consumer;
    }
};

// What the RTMP player got since it last read: A and V for the sequence headers, a and v for frames.
static string stream_bridge_utest_rtmp_got(SrsLiveConsumer *player)
{
    SrsMessageArray msgs(1024);
    int count = 0;
    srs_error_t err = player->dump_packets(&msgs, count);
    EXPECT_TRUE(err == srs_success) << srs_error_desc(err);
    srs_freep(err);

    string got;
    for (int i = 0; i < count; i++) {
        SrsMediaPacket *msg = msgs.msgs_[i];
        if (msg->is_audio()) {
            got += SrsFlvAudio::sh(msg->payload(), msg->size()) ? "A" : "a";
        } else if (msg->is_video()) {
            got += SrsFlvVideo::sh(msg->payload(), msg->size()) ? "V" : "v";
        }
    }
    msgs.free(count);
    return got;
}

// The number of packets the WebRTC player got since it last read.
static int stream_bridge_utest_rtc_got(SrsRtcConsumer *player)
{
    int n = 0;
    for (;;) {
        SrsRtpPacket *pkt = NULL;
        srs_error_t err = player->dump_packet(&pkt);
        EXPECT_TRUE(err == srs_success) << srs_error_desc(err);
        srs_freep(err);
        if (!pkt) {
            break;
        }
        srs_freep(pkt);
        n++;
    }
    return n;
}

// Opus frames of 20ms, as a browser sends them, from n AAC frames transcoded as the RTMP to WebRTC bridge does.
static void stream_bridge_utest_opus(int n, vector<string> &opus)
{
    srs_error_t err = srs_success;

    vector<string> aac;
    encode_aac_adts_frames(n, aac);

    SrsAudioTranscoder transcoder;
    HELPER_ASSERT_SUCCESS(transcoder.initialize(SrsAudioCodecIdAAC, SrsAudioCodecIdOpus, 2, 48000, 48000));
    for (int i = 0; i < (int)aac.size(); i++) {
        SrsParsedAudioPacket frame;
        frame.dts_ = (int64_t)i * 1024 * 1000 / 44100;
        frame.cts_ = 0;
        HELPER_ASSERT_SUCCESS(frame.add_sample((char *)aac[i].data(), (int)aac[i].size()));

        vector<SrsParsedAudioPacket *> outs;
        HELPER_ASSERT_SUCCESS(transcoder.transcode(&frame, outs));
        for (int j = 0; j < (int)outs.size(); j++) {
            SrsParsedAudioPacket *out = outs.at(j);
            opus.push_back(string(out->samples_[0].bytes_, out->samples_[0].size_));
        }
        transcoder.free_frames(outs);
    }
}

// A WHIP publisher to a vhost with `rtc_to_rtmp on` that also forwards to an rtmps:// URL, such as YouTube's RTMPS
// ingest, which forward does not support: the RTMP side refuses the publish as not supported, so the session fails
// after DTLS. When the session ends, both sides of the stream are released, so a publisher can take it again.
VOID TEST(AppStreamBridgeTest, WhipToRtmpForwardingToRtmps_RefusedThenStreamReleased)
{
    srs_error_t err;

    StreamBridgeUtestStream s;
    HELPER_ASSERT_SUCCESS(s.open("vhost __defaultVhost__ { rtc { enabled on; rtc_to_rtmp on; } "
                                 "forward { enabled on; destination rtmps://a.rtmps.example.com/live2; } }"));
    EXPECT_TRUE(s.live_->can_publish(false));
    EXPECT_TRUE(s.rtc_->can_publish());

    err = s.whip_publish();
    EXPECT_EQ(ERROR_NOT_SUPPORTED, srs_error_code(err));
    EXPECT_NE(string::npos, srs_error_desc(err).find("rtmp target publish")) << srs_error_desc(err);
    EXPECT_NE(string::npos, srs_error_desc(err).find("forward does not support RTMPS")) << srs_error_desc(err);
    srs_freep(err);

    // The server's publish handler, which mounts the HTTP stream, is never reached, and the stream is not published.
    EXPECT_EQ(0, s.handler_.on_publish_count_);
    EXPECT_FALSE(s.live_->can_publish(false));
    EXPECT_FALSE(s.rtc_->can_publish());

    // The session ends, as SrsRtcPublishStream's destructor unpublishes the source.
    s.rtc_->on_unpublish();
    EXPECT_TRUE(s.live_->can_publish(false));
    EXPECT_TRUE(s.rtc_->can_publish());
    EXPECT_EQ(1, s.handler_.on_unpublish_count_);
}

// A WHIP publisher to a vhost with `rtc_to_rtmp on` sends one Opus packet the decoder refuses, such as one whose frame
// count is 0 from a broken encoder: WebRTC players get it as published, the packet fails to convert, which the server
// logs, and the next packets are converted again, so RTMP players lose only that packet's audio.
VOID TEST(AppStreamBridgeTest, WhipToRtmpCorruptOpusPacket_OnlyThatPacketLost)
{
    srs_error_t err;

    vector<string> opus;
    stream_bridge_utest_opus(50, opus);
    ASSERT_GE((int)opus.size(), 40);

    StreamBridgeUtestStream s;
    HELPER_ASSERT_SUCCESS(s.open("vhost __defaultVhost__ { rtc { enabled on; rtc_to_rtmp on; } }"));
    HELPER_ASSERT_SUCCESS(s.whip_publish());
    SrsLiveConsumer *rtmp_player = s.rtmp_play();
    SrsRtcConsumer *rtc_player = s.rtc_play();
    ASSERT_TRUE(rtmp_player != NULL);
    ASSERT_TRUE(rtc_player != NULL);

    for (int i = 0; i < 20; i++) {
        HELPER_EXPECT_SUCCESS(s.whip_opus(100 + i, 1000 + i * 20, opus.at(i)));
    }
    string before = stream_bridge_utest_rtmp_got(rtmp_player);
    ASSERT_GT(before.size(), 10u) << before;
    EXPECT_EQ('A', before.at(0)) << before;
    EXPECT_EQ(string(before.size() - 1, 'a'), before.substr(1));
    EXPECT_EQ(20, stream_bridge_utest_rtc_got(rtc_player));

    // The TOC byte of a 20ms CELT frame with code 3, then a frame count of 0, which RFC 6716 forbids.
    err = s.whip_opus(120, 1000 + 20 * 20, string("\xfb\x00", 2));
    EXPECT_EQ(ERROR_RTC_RTP_MUXER, srs_error_code(err));
    EXPECT_NE(string::npos, srs_error_desc(err).find("frame builder on rtp")) << srs_error_desc(err);
    srs_freep(err);
    EXPECT_EQ(1, stream_bridge_utest_rtc_got(rtc_player));

    for (int i = 21; i < 40; i++) {
        HELPER_EXPECT_SUCCESS(s.whip_opus(100 + i, 1000 + i * 20, opus.at(i)));
    }
    string after = stream_bridge_utest_rtmp_got(rtmp_player);
    EXPECT_GT(after.size(), 10u) << after;
    EXPECT_EQ(string(after.size(), 'a'), after);
    EXPECT_EQ(19, stream_bridge_utest_rtc_got(rtc_player));

    s.rtc_->on_unpublish();
}

// The RTMP audio message of an ADTS frame: the FLV audio tag header of AAC, then the raw frame without its ADTS header.
static string stream_bridge_utest_flv_aac(const string &adts)
{
    return string("\xaf\x01", 2) + adts.substr(7);
}

// An ADTS frame that keeps its 7-byte header, with garbage after it, which the AAC decoder refuses.
static string stream_bridge_utest_corrupt(const string &adts)
{
    string corrupt = adts;
    for (int i = 7; i < (int)corrupt.size(); i++) {
        corrupt[i] = (char)(0xff - i * 7);
    }
    return corrupt;
}

// An SRT encoder to a vhost with `srt_to_rtmp on` and `rtmp_to_rtc on` sends one corrupt AAC frame, its ADTS header
// right but its data garbage: RTMP players get every frame as sent, the WebRTC side fails to transcode only that frame,
// which the server logs, and the next frames give Opus again, so WebRTC players lose only that frame and the encoder
// stays.
VOID TEST(AppStreamBridgeTest, SrtToRtmpAndRtcCorruptAacFrame_OnlyWebrtcLosesThatFrame)
{
    srs_error_t err;

    vector<string> aac;
    encode_aac_adts_frames(30, aac);
    ASSERT_EQ(30, (int)aac.size());

    StreamBridgeUtestStream s;
    HELPER_ASSERT_SUCCESS(s.open("vhost __defaultVhost__ { srt { enabled on; srt_to_rtmp on; } "
                                 "rtc { enabled on; rtmp_to_rtc on; } }"));
    HELPER_ASSERT_SUCCESS(s.srt_publish());
    SrsLiveConsumer *rtmp_player = s.rtmp_play();
    SrsRtcConsumer *rtc_player = s.rtc_play();
    ASSERT_TRUE(rtmp_player != NULL);
    ASSERT_TRUE(rtc_player != NULL);

    for (int i = 0; i < 10; i++) {
        HELPER_EXPECT_SUCCESS(s.srt_audio(i * 1024 * 1000 / 44100, aac.at(i)));
    }
    EXPECT_EQ("A" + string(10, 'a'), stream_bridge_utest_rtmp_got(rtmp_player));
    EXPECT_GT(stream_bridge_utest_rtc_got(rtc_player), 5);
    EXPECT_EQ(0, s.warned("rtp builder on frame"));

    HELPER_EXPECT_SUCCESS(s.srt_audio(10 * 1024 * 1000 / 44100, stream_bridge_utest_corrupt(aac.at(10))));
    EXPECT_EQ("a", stream_bridge_utest_rtmp_got(rtmp_player));
    EXPECT_EQ(0, stream_bridge_utest_rtc_got(rtc_player));
    EXPECT_EQ(1, s.warned("rtp builder on frame"));

    for (int i = 11; i < 30; i++) {
        HELPER_EXPECT_SUCCESS(s.srt_audio(i * 1024 * 1000 / 44100, aac.at(i)));
    }
    EXPECT_EQ(string(19, 'a'), stream_bridge_utest_rtmp_got(rtmp_player));
    EXPECT_GT(stream_bridge_utest_rtc_got(rtc_player), 15);
    EXPECT_EQ(1, s.warned("rtp builder on frame"));

    s.srt_->on_unpublish();
}

// An RTMP encoder to a vhost with `rtmp_to_rtc on` sends one corrupt AAC frame, its ADTS data garbage. As for SRT, the
// WebRTC players should lose only that frame, and the encoder should stay. The WebRTC builder instead returns the
// decoder's error, the live source returns it before the RTMP players get the frame, and SrsRtmpConn ends the publish
// on it, so one bad frame disconnects the encoder (B62).
VOID TEST(AppStreamBridgeTest, DISABLED_RtmpToRtcCorruptAacFrame_PublisherDisconnected)
{
    srs_error_t err;

    vector<string> aac;
    encode_aac_adts_frames(20, aac);
    ASSERT_EQ(20, (int)aac.size());

    StreamBridgeUtestStream s;
    HELPER_ASSERT_SUCCESS(s.open("vhost __defaultVhost__ { rtc { enabled on; rtmp_to_rtc on; } }"));
    HELPER_ASSERT_SUCCESS(s.rtmp_publish());
    SrsLiveConsumer *rtmp_player = s.rtmp_play();
    SrsRtcConsumer *rtc_player = s.rtc_play();
    ASSERT_TRUE(rtmp_player != NULL);
    ASSERT_TRUE(rtc_player != NULL);

    HELPER_ASSERT_SUCCESS(s.rtmp_audio(0, flv_utest_aac_sh, sizeof(flv_utest_aac_sh)));
    for (int i = 0; i < 20; i++) {
        string frame = stream_bridge_utest_flv_aac(i == 10 ? stream_bridge_utest_corrupt(aac.at(i)) : aac.at(i));
        HELPER_EXPECT_SUCCESS(s.rtmp_audio(i * 1024 * 1000 / 44100, (const uint8_t *)frame.data(), (int)frame.size()));
    }
    EXPECT_EQ("A" + string(20, 'a'), stream_bridge_utest_rtmp_got(rtmp_player));
    EXPECT_GT(stream_bridge_utest_rtc_got(rtc_player), 15);

    s.live_->on_unpublish();
}

// An HEVC RTMP encoder that sends its VPS, SPS and PPS in band, in each key frame, with a sequence header that holds
// none, as ISO/IEC 14496-15 allows for hev1, to a vhost with `rtmp_to_rtc on`. The live source takes both, and the
// WebRTC player should get the key frame's NALUs. The WebRTC builder instead fails the key frame, "vps/sps/pps
// empty", and the live source returns it, so the encoder is disconnected at its first key frame (B61, as B57 for RTSP).
VOID TEST(AppStreamBridgeTest, DISABLED_RtmpHevcParameterSetsInBandToRtc_PublisherDisconnected)
{
    srs_error_t err;

    StreamBridgeUtestStream s;
    HELPER_ASSERT_SUCCESS(s.open("vhost __defaultVhost__ { rtc { enabled on; rtmp_to_rtc on; } }"));
    HELPER_ASSERT_SUCCESS(s.rtmp_publish());
    SrsRtcConsumer *player = s.rtc_play();
    ASSERT_TRUE(player != NULL);

    // The HEVCDecoderConfigurationRecord: version 1, Main profile, level 3.1, 4-byte NALU sizes, and no arrays.
    const uint8_t sh[] = {0x1c, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x60, 0x00, 0x00, 0x00, 0x90, 0x00, 0x00, 0x00,
                          0x00, 0x00, 0x5d, 0xf0, 0x00, 0xfc, 0xfd, 0xf8, 0xf8, 0x00, 0x00, 0x0f, 0x00};
    HELPER_ASSERT_SUCCESS(s.rtmp_video(0, sh, sizeof(sh)));

    string tag;
    HELPER_ASSERT_SUCCESS(mp4_hevc_sh(1, tag));
    SrsFormat format;
    HELPER_ASSERT_SUCCESS(format.initialize());
    HELPER_ASSERT_SUCCESS(format.on_video(0, (char *)tag.data(), (int)tag.size()));

    // The key frame: the VPS, SPS and PPS, then the IDR, each with its size.
    string frame("\x1c\x01\x00\x00\x00", 5);
    vector<SrsHevcHvccNalu> &arrays = format.vcodec_->hevc_dec_conf_record_.nalu_vec_;
    for (int i = 0; i < (int)arrays.size(); i++) {
        const vector<uint8_t> &nalu = arrays.at(i).nal_data_vec_.at(0).nal_unit_data_;
        frame += string(3, '\0') + (char)nalu.size() + string((const char *)&nalu[0], nalu.size());
    }
    frame += string("\x00\x00\x00\x05\x26\x01\xaf\x00\x10", 9);

    HELPER_EXPECT_SUCCESS(s.rtmp_video(40, (const uint8_t *)frame.data(), (int)frame.size()));
    EXPECT_EQ(4, stream_bridge_utest_rtc_got(player));

    s.live_->on_unpublish();
}
#endif

// The server's view of each TCP client it accepts, as SrsServer::do_on_tcp_client takes it: the client's address and
// port, for the security check and the log.
class UtilityUtestPeerOfClient : public ISrsTcpHandler
{
public:
    string ip_;
    int port_;

public:
    UtilityUtestPeerOfClient()
    {
        port_ = 0;
    }
    virtual ~UtilityUtestPeerOfClient()
    {
    }

public:
    virtual srs_error_t on_tcp_client(ISrsListener *listener, srs_netfd_t stfd)
    {
        int fd = srs_netfd_fileno(stfd);
        ip_ = srs_get_peer_ip(fd);
        port_ = srs_get_peer_port(fd);
        srs_close_stfd(stfd);
        return srs_success;
    }
};

// A client connecting over IPv6 to a server listening on an IPv6 address: the server has the client's address and port.
VOID TEST(AppUtilityTest, Ipv6ClientConnects_ServerHasItsPort)
{
    srs_error_t err;

    int port = srs_utest_random_port();
    UtilityUtestPeerOfClient server;
    SrsTcpListener listener(&server);
    listener.set_endpoint("::1", port);
    HELPER_ASSERT_SUCCESS(listener.listen());

    srs_netfd_t stfd = NULL;
    HELPER_ASSERT_SUCCESS(srs_tcp_connect("::1", port, 1 * SRS_UTIME_SECONDS, &stfd));
    SrsTcpConnection client(stfd);

    for (int i = 0; i < 100 && !server.port_; i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
    EXPECT_STREQ("::1", server.ip_.c_str());
    EXPECT_EQ(srs_get_local_port(srs_netfd_fileno(stfd)), server.port_);
}

#if !defined(SRS_OSX) && !defined(_WIN32) // Linux only: the disk, UDP and network device stats read /proc.
// The block devices of the host, one per line of /proc/diskstats: the name and the reads completed, as the kernel counts.
static map<string, unsigned long long> utility_utest_disk_reads()
{
    map<string, unsigned long long> disks;

    FILE *f = fopen("/proc/diskstats", "r");
    if (!f) {
        return disks;
    }

    char buf[1024];
    while (fgets(buf, sizeof(buf), f)) {
        unsigned int major = 0, minor = 0;
        char name[32] = {0};
        unsigned long long reads = 0;
        if (sscanf(buf, "%u %u %31s %llu", &major, &minor, name, &reads) == 4) {
            disks[name] = reads;
        }
    }
    fclose(f);

    return disks;
}

static unsigned long long utility_utest_sum(const map<string, unsigned long long> &disks)
{
    unsigned long long sum = 0;
    for (map<string, unsigned long long>::const_iterator it = disks.begin(); it != disks.end(); ++it) {
        unsigned long long reads = it->second;
        sum += reads;
    }
    return sum;
}

// A 4MB file written and synced, then dropped from the page cache and read back, so the disk is written and read, as by
// a DVR writing a recording and a player reading an old one.
static void utility_utest_disk_write_and_read(const string &path)
{
    string data(4 * 1024 * 1024, 'x');

    int fd = ::open(path.c_str(), O_CREAT | O_TRUNC | O_RDWR, 0644);
    ASSERT_GT(fd, 0);
    ASSERT_EQ((ssize_t)data.size(), ::write(fd, data.data(), data.size()));
    ASSERT_EQ(0, ::fsync(fd));
    ASSERT_EQ(0, ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED));

    ASSERT_EQ(0, ::lseek(fd, 0, SEEK_SET));
    string back(data.size(), '\0');
    ASSERT_EQ((ssize_t)back.size(), ::read(fd, &back[0], back.size()));
    EXPECT_TRUE(back == data);

    ::close(fd);
    ::unlink(path.c_str());
}

static SrsJsonObject *utility_utest_summaries_system(SrsJsonObject *obj)
{
    srs_api_dump_summaries(_srs_config, obj);
    SrsJsonObject *data = (SrsJsonObject *)obj->get_property("data");
    return (SrsJsonObject *)data->get_property("system");
}

// The host's disks listed in `stats { disk }`, written and read between two samples of the server's disk timer: the
// summaries API reports a read rate and a busy percent, and the counters are the sum of the listed disks.
VOID TEST(AppUtilityTest, StatsDisksWrittenAndReadBetweenSamples_ApiReportsReadRateAndBusy)
{
    srs_error_t err;

    map<string, unsigned long long> disks = utility_utest_disk_reads();
    ASSERT_FALSE(disks.empty());

    string names;
    for (map<string, unsigned long long>::iterator it = disks.begin(); it != disks.end(); ++it) {
        string name = it->first;
        names += " " + name;
    }

    FactoryUtestServerConfig server;
    HELPER_ASSERT_SUCCESS(server.load("stats { disk" + names + "; }"));

    srs_update_disk_stat();

    utility_utest_disk_write_and_read(_srs_tmp_file_prefix + "ai46-disk-stat.data");
    srs_usleep(50 * SRS_UTIME_MILLISECONDS);

    unsigned long long reads_before = utility_utest_sum(utility_utest_disk_reads());
    srs_update_disk_stat();
    unsigned long long reads_after = utility_utest_sum(utility_utest_disk_reads());

    SrsDiskStat *stat = srs_get_disk_stat();
    EXPECT_TRUE(stat->ok_);
    EXPECT_LE(reads_before, (unsigned long long)stat->rd_ios_);
    EXPECT_GE(reads_after, (unsigned long long)stat->rd_ios_);
    EXPECT_GT(stat->wr_ios_, 0u);
    EXPECT_GT(stat->ticks_, 0u);

    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
    SrsJsonObject *sys = utility_utest_summaries_system(obj.get());
    EXPECT_GT(sys->get_property("disk_read_KBps")->to_integer(), 0);
    EXPECT_GT(sys->get_property("disk_busy_percent")->to_number(), 0);
}

// A disk listed in `stats { disk }` that the host does not have, such as sda on a host of vda: nothing is counted and the
// disk is not busy, while the stat is still sampled.
VOID TEST(AppUtilityTest, StatsDiskTheHostHasNot_CountsNothing)
{
    srs_error_t err;

    FactoryUtestServerConfig server;
    HELPER_ASSERT_SUCCESS(server.load("stats { disk utestnodisk0; }"));

    srs_update_disk_stat();
    srs_update_disk_stat();

    SrsDiskStat *stat = srs_get_disk_stat();
    EXPECT_TRUE(stat->ok_);
    EXPECT_EQ(0u, stat->rd_ios_);
    EXPECT_EQ(0u, stat->wr_ios_);
    EXPECT_EQ(0u, stat->ticks_);
    EXPECT_EQ(0, stat->busy_);
}

// A UDP socket with the smallest receive buffer that reads nothing, as a WebRTC server falling behind, and a peer sending
// 1KB datagrams to it on loopback: the kernel drops what the buffer can't hold. Returns the datagrams dropped.
static int utility_utest_udp_overflow(int n)
{
    int rfd = ::socket(AF_INET, SOCK_DGRAM, 0);
    int size = 1;
    ::setsockopt(rfd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t addrlen = sizeof(addr);
    ::bind(rfd, (sockaddr *)&addr, sizeof(addr));
    ::getsockname(rfd, (sockaddr *)&addr, &addrlen);

    int sfd = ::socket(AF_INET, SOCK_DGRAM, 0);
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    int sent = 0;
    for (int i = 0; i < n; i++) {
        if (::sendto(sfd, buf, sizeof(buf), 0, (sockaddr *)&addr, sizeof(addr)) == (ssize_t)sizeof(buf)) {
            sent++;
        }
    }

    int kept = 0;
    while (::recv(rfd, buf, sizeof(buf), MSG_DONTWAIT) > 0) {
        kept++;
    }

    ::close(sfd);
    ::close(rfd);
    return sent - kept;
}

// The host dropping UDP datagrams for a full receive buffer, then dropping more before the server's next sample of
// /proc/net/snmp: the stat has the drops since the last sample, which the RTC server logs as loss, and none once the
// drops stop.
VOID TEST(AppUtilityTest, HostDropsUdpForFullReceiveBuffer_DropsSinceLastSample)
{
    ASSERT_GT(utility_utest_udp_overflow(100), 0);
    srs_update_udp_snmp_statistic();
    SrsSnmpUdpStat *stat = srs_get_udp_snmp_stat();
    EXPECT_TRUE(stat->ok_);
    EXPECT_GT(stat->rcv_buf_errors_, 0u);

    int dropped = utility_utest_udp_overflow(100);
    ASSERT_GT(dropped, 0);
    srs_update_udp_snmp_statistic();
    EXPECT_EQ(dropped, srs_get_udp_snmp_stat()->rcv_buf_errors_delta_);

    srs_update_udp_snmp_statistic();
    EXPECT_EQ(0, srs_get_udp_snmp_stat()->rcv_buf_errors_delta_);
}

// The first interface of the host that /proc/net/dev lists and that has an IPv4 address other than the loopback, such as
// eth0, or empty if none.
static string utility_utest_host_interface()
{
    set<string> named;
    ifaddrs *ifap = NULL;
    if (srs_getifaddrs(&ifap) != 0) {
        return "";
    }
    for (ifaddrs *p = ifap; p; p = p->ifa_next) {
        if (p->ifa_addr && p->ifa_addr->sa_family == AF_INET && !(p->ifa_flags & IFF_LOOPBACK)) {
            named.insert(p->ifa_name);
        }
    }
    srs_freeifaddrs(ifap);

    SrsNetworkDevices *devices = srs_get_network_devices();
    for (int i = 0; i < srs_get_network_devices_count(); i++) {
        SrsNetworkDevices &device = devices[i];
        if (named.find(device.name_) != named.end()) {
            return device.name_;
        }
    }
    return "";
}

// The internet flag of each interface, as the server discovers the host's own addresses at startup.
static void utility_utest_discover_host_ips()
{
    ifaddrs *ifap = NULL;
    if (srs_getifaddrs(&ifap) != 0) {
        return;
    }

    SrsProtocolUtility utility;
    vector<SrsIPAddress *> ips;
    srs_discover_local_ips(&utility, ifap, ips);
    srs_freeifaddrs(ifap);

    for (int i = 0; i < (int)ips.size(); i++) {
        SrsIPAddress *ip = ips[i];
        srs_freep(ip);
    }
}

// A host whose interface has a public address, such as a cloud server's eth0: the summaries API counts that interface's
// bytes as internet traffic, and the other interfaces', the loopback too, as intranet traffic.
VOID TEST(AppUtilityTest, InterfaceWithPublicAddress_BytesCountedAsInternet)
{
    srs_update_network_devices();
    string ifname = utility_utest_host_interface();
    ASSERT_FALSE(ifname.empty());

    // The server discovers the interface with a public address.
    UtilityIfaceList ifs;
    ifs.add(ifname.c_str(), IFF_UP | IFF_RUNNING, "203.0.113.10");
    SrsProtocolUtility utility;
    vector<SrsIPAddress *> ips;
    srs_discover_local_ips(&utility, ifs.head(), ips);
    ASSERT_EQ(1, (int)ips.size());
    EXPECT_TRUE(ips[0]->is_internet_);
    srs_freep(ips[0]);

    srs_update_network_devices();
    int64_t internet_recv = 0, internet_send = 0, intranet_recv = 0, intranet_send = 0;
    SrsNetworkDevices *devices = srs_get_network_devices();
    for (int i = 0; i < srs_get_network_devices_count(); i++) {
        SrsNetworkDevices &device = devices[i];
        if (ifname == device.name_) {
            internet_recv += device.rbytes_;
            internet_send += device.sbytes_;
        } else {
            intranet_recv += device.rbytes_;
            intranet_send += device.sbytes_;
        }
    }

    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
    SrsJsonObject *sys = utility_utest_summaries_system(obj.get());

    // Restore the interface's flag from the host's own addresses, before any assertion can return.
    utility_utest_discover_host_ips();

    EXPECT_GT(internet_recv, 0);
    EXPECT_EQ(internet_recv, sys->get_property("net_recv_bytes")->to_integer());
    EXPECT_EQ(internet_send, sys->get_property("net_send_bytes")->to_integer());
    EXPECT_EQ(intranet_recv, sys->get_property("net_recvi_bytes")->to_integer());
    EXPECT_EQ(intranet_send, sys->get_property("net_sendi_bytes")->to_integer());
}

// The interfaces of a host as /proc/net/dev lists them: the name before the colon, without the padding.
static vector<string> utility_utest_proc_interfaces()
{
    vector<string> names;

    FILE *f = fopen("/proc/net/dev", "r");
    if (!f) {
        return names;
    }

    char buf[1024];
    for (int i = 0; fgets(buf, sizeof(buf), f); i++) {
        char *colon = strchr(buf, ':');
        if (i < 2 || !colon) {
            continue;
        }
        string name(buf, colon - buf);
        names.push_back(name.substr(name.find_first_not_of(' ')));
    }
    fclose(f);

    return names;
}

// A host with an interface named longer than 6 chars, such as enp0s31f6 on a bare-metal server, or gretap0 and ip6tnl0 in
// a Docker container: the server lists it by a cut name with no bytes, so a public interface of such a name counts no
// internet traffic. Bug: SrsNetworkDevices keeps 6 chars of the name, and the parse stops at the cut.
VOID TEST(AppUtilityTest, DISABLED_InterfaceNamedLongerThan6Chars_NotListed)
{
    vector<string> names = utility_utest_proc_interfaces();
    ASSERT_FALSE(names.empty());

    srs_update_network_devices();
    set<string> listed;
    SrsNetworkDevices *devices = srs_get_network_devices();
    for (int i = 0; i < srs_get_network_devices_count(); i++) {
        SrsNetworkDevices &device = devices[i];
        listed.insert(device.name_);
    }

    for (int i = 0; i < (int)names.size() && i < 16; i++) {
        string name = names[i];
        EXPECT_TRUE(listed.find(name) != listed.end()) << "interface " << name << " not listed";
    }
}
#endif

// A client connects to the RTMP port and resets the connection at once, as a load balancer's TCP health probe or a
// port scanner does, before the server sets TCP_NODELAY and the send buffer of the accepted socket. macOS refuses
// both options on a reset socket, so the server gets a socket option error; Linux keeps the options. Either way the
// first read of the handshake fails, so the server drops the connection.
VOID TEST(ProtocolConnTest, ClientResetsBeforeSocketOptions_ConnectionDropped)
{
    srs_error_t err = srs_success;

    StUtestTcpPair pair;
    HELPER_ASSERT_SUCCESS(pair.open());

    SrsTcpConnection conn(pair.take_server());
    conn.set_recv_timeout(3 * SRS_UTIME_SECONDS);

    // The client closes with a reset, before the server reads anything.
    struct linger l;
    l.l_onoff = 1;
    l.l_linger = 0;
    EXPECT_EQ(0, setsockopt(srs_netfd_fileno(pair.client_), SOL_SOCKET, SO_LINGER, (const char *)&l, sizeof(l)));
    srs_close_stfd(pair.client_);
    srs_usleep(10 * SRS_UTIME_MILLISECONDS);

    srs_error_t nodelay_err = conn.set_tcp_nodelay(true);
    srs_error_t sndbuf_err = conn.set_socket_buffer(350 * SRS_UTIME_MILLISECONDS);
#ifdef SRS_OSX
    EXPECT_EQ(ERROR_SOCKET_NO_NODELAY, srs_error_code(nodelay_err));
    EXPECT_EQ(ERROR_SOCKET_SNDBUF, srs_error_code(sndbuf_err));
#else
    HELPER_EXPECT_SUCCESS(nodelay_err);
    HELPER_EXPECT_SUCCESS(sndbuf_err);
    nodelay_err = sndbuf_err = srs_success;
#endif
    srs_freep(nodelay_err);
    srs_freep(sndbuf_err);

    // The RTMP handshake reads C0C1 first, and gets the reset.
    char c0c1[1537];
    ssize_t nread = 0;
    err = conn.read_fully(c0c1, sizeof(c0c1), &nread);
    ASSERT_TRUE(err != srs_success);
    EXPECT_NE(ERROR_SOCKET_TIMEOUT, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(0, conn.get_recv_bytes());
}
