//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai46.hpp>

#include <srs_app_srt_listener.hpp>
#include <srs_app_srt_server.hpp>
#include <srs_app_srt_source.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_stream_bridge.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_protocol_srt.hpp>
#include <srs_protocol_st.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai32.hpp>
#include <srs_utest_ai37.hpp>
#include <srs_utest_manual_config.hpp>

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
