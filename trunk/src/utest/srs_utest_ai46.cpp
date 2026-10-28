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
#include <srs_app_ng_exec.hpp>
#include <srs_app_server.hpp>
#include <srs_app_process.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_rtmp_handshake.hpp>
#include <srs_protocol_rtmp_msg_array.hpp>
#include <srs_protocol_sdp.hpp>
#include <srs_protocol_srt.hpp>
#include <srs_protocol_st.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_utest_ai08.hpp>
#include <srs_utest_ai14.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai18.hpp>
#include <srs_utest_ai24.hpp>
#include <srs_utest_ai32.hpp>
#include <srs_utest_ai37.hpp>
#include <srs_utest_ai38.hpp>
#include <srs_utest_ai40.hpp>
#include <srs_utest_ai42.hpp>
#include <srs_utest_ai43.hpp>
#include <srs_utest_ai44.hpp>
#include <srs_utest_manual_config.hpp>
#include <srs_utest_manual_mock.hpp>

#ifndef _WIN32
#include <fcntl.h>
#include <net/if.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#include <windows.h>

#include <shellapi.h>
#include <ws2tcpip.h>
#pragma comment(lib, "shell32.lib")
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

// As above, but with a P frame between the key frame of the cut SPS and the next key frame, as in any GOP. The P frame
// reports nothing, and the stream API gets 320x240 from the next key frame.
VOID TEST(AppSrtSourceTest, KeyFrameWithCutSpsThenPFrame_VideoReportedFromNextKeyFrame)
{
    srs_error_t err;

    SrtSourceUtestStream s("cut-sps-gop");
    HELPER_ASSERT_SUCCESS(s.publish());

    string cut = srt_source_utest_nalu(srt_source_utest_sps, 4) + srt_source_utest_nalu(srt_source_utest_pps, sizeof(srt_source_utest_pps)) +
                 srt_source_utest_nalu(srt_source_utest_idr, sizeof(srt_source_utest_idr));
    HELPER_ASSERT_SUCCESS(s.video(90000, 90000, cut));
    HELPER_ASSERT_SUCCESS(s.video(93600, 93600, srt_source_utest_h264_p()));
    HELPER_ASSERT_SUCCESS(s.send());
    EXPECT_FALSE(s.format()->video_codec_reported_);

    HELPER_ASSERT_SUCCESS(s.video(97200, 97200, srt_source_utest_h264_key()));
    HELPER_ASSERT_SUCCESS(s.send());
    EXPECT_TRUE(s.format()->video_codec_reported_);

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
// its DASH, DVR, transcoder and exec do nothing. Its HLS does nothing too, unless real_hls_ asks for the server's TS
// HLS, which writes where the vhost says.
class StreamBridgeUtestHubFactory : public SrsAppFactory
{
public:
    ISrsAppConfig *config_;
    ISrsStatistic *stat_;
    bool real_hls_;

public:
    StreamBridgeUtestHubFactory()
    {
        config_ = NULL;
        stat_ = NULL;
        real_hls_ = false;
    }

public:
    virtual ISrsOriginHub *create_origin_hub()
    {
        SrsOriginHub *hub = new SrsOriginHub();
        hub->config_ = config_;
        hub->stat_ = stat_;

        srs_freep(hub->hls_);
        if (real_hls_) {
            SrsHls *hls = new SrsHls();
            hls->assemble();
            hls->config_ = config_;
            SrsHlsController *controller = new SrsHlsController();
            controller->config_ = config_;
            dynamic_cast<SrsHlsMuxer *>(controller->muxer_)->config_ = config_;
            hls->controller_ = controller;
            hub->hls_ = hls;
        } else {
            hub->hls_ = new MockHlsForOriginHub();
        }
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

// An SRT encoder of a radio station starts to publish, with HLS on under home with `hls_on_error disconnect`, besides
// `srt_to_rtmp on` and `rtmp_to_rtc on`.
static srs_error_t stream_bridge_utest_hls_radio(StreamBridgeUtestStream &s, const string &home)
{
    srs_error_t err = srs_success;

    s.factory_.real_hls_ = true;
    if ((err = s.open("vhost __defaultVhost__ { srt { enabled on; srt_to_rtmp on; } rtc { enabled on; rtmp_to_rtc on; } "
                      "hls { enabled on; hls_fragment 1; hls_on_error disconnect; hls_path " +
                      home + "; } }")) != srs_success) {
        return srs_error_wrap(err, "open");
    }

    return s.srt_publish();
}

// An SRT radio station converted to RTMP and WebRTC, with HLS on, when the HLS disk fills. The live source refuses
// each frame that HLS can not write, as for an RTMP encoder, before any player gets it: from the first frame HLS fails
// on, the RTMP and the WebRTC players get none of the frames, not only the HLS players. AAC is written to HLS in runs,
// so the frames until the next run still reach the players.
VOID TEST(AppStreamBridgeTest, SrtToRtmpAndRtcHlsDiskFull_PlayersGetNoFrameHlsFailed)
{
    srs_error_t err;

    vector<string> aac;
    encode_aac_adts_frames(90, aac);
    ASSERT_EQ(90, (int)aac.size());

    string home = _srs_tmp_file_prefix + "ai46-bridge-hls-full";
    hls_utest_remove_all(home);
    if (true) {
        HlsUtestVolumeScope volume;
        StreamBridgeUtestStream s;
        HELPER_ASSERT_SUCCESS(stream_bridge_utest_hls_radio(s, home));
        SrsLiveConsumer *rtmp_player = s.rtmp_play();
        SrsRtcConsumer *rtc_player = s.rtc_play();
        ASSERT_TRUE(rtmp_player != NULL);
        ASSERT_TRUE(rtc_player != NULL);

        for (int i = 0; i < 50; i++) {
            HELPER_ASSERT_SUCCESS(s.srt_audio(i * 1024 * 1000 / 44100, aac.at(i)));
        }
        EXPECT_EQ("A" + string(50, 'a'), stream_bridge_utest_rtmp_got(rtmp_player));
        EXPECT_GT(stream_bridge_utest_rtc_got(rtc_player), 40);

        // Each frame reaches both players until HLS fails on one; from then on, none does.
        volume.fill();
        int failed_at = -1;
        for (int i = 50; i < 90; i++) {
            err = s.srt_audio(i * 1024 * 1000 / 44100, aac.at(i));
            srs_freep(err);
            string rtmp_got = stream_bridge_utest_rtmp_got(rtmp_player);
            int rtc_got = stream_bridge_utest_rtc_got(rtc_player);
            if (failed_at < 0 && rtmp_got.empty()) {
                failed_at = i;
            }
            if (failed_at < 0) {
                EXPECT_EQ("a", rtmp_got) << i;
                EXPECT_GT(rtc_got, 0) << i;
            } else {
                EXPECT_EQ("", rtmp_got) << i;
                EXPECT_EQ(0, rtc_got) << i;
            }
        }
        EXPECT_GT(failed_at, 50);
        EXPECT_LT(failed_at, 80);

        s.srt_->on_unpublish();
    }
    hls_utest_remove_all(home);
}

// The same SRT radio station, with `hls_on_error disconnect`, when the HLS disk fills. As for an RTMP encoder, the
// frame HLS fails on should end the publish, so the encoder republishes, as the config asks. The SRT frame builder
// instead logs the live source's error as a bad TS packet and goes on, so the encoder stays while no player gets a
// frame, and the server logs a warning per TS packet until the disk has room (B65).
VOID TEST(AppStreamBridgeTest, DISABLED_SrtHlsDiskFullWithOnErrorDisconnect_EncoderNotDisconnected)
{
    srs_error_t err;

    vector<string> aac;
    encode_aac_adts_frames(90, aac);
    ASSERT_EQ(90, (int)aac.size());

    string home = _srs_tmp_file_prefix + "ai46-bridge-hls-full-dis";
    hls_utest_remove_all(home);
    if (true) {
        HlsUtestVolumeScope volume;
        StreamBridgeUtestStream s;
        HELPER_ASSERT_SUCCESS(stream_bridge_utest_hls_radio(s, home));
        for (int i = 0; i < 50; i++) {
            HELPER_ASSERT_SUCCESS(s.srt_audio(i * 1024 * 1000 / 44100, aac.at(i)));
        }

        volume.fill();
        int code = ERROR_SUCCESS;
        for (int i = 50; i < 90 && code == ERROR_SUCCESS; i++) {
            err = s.srt_audio(i * 1024 * 1000 / 44100, aac.at(i));
            code = srs_error_code(err);
            srs_freep(err);
        }
        EXPECT_EQ(ERROR_SYSTEM_FILE_WRITE, code);
        EXPECT_EQ(0, s.warned("rtmp target on frame"));

        s.srt_->on_unpublish();
    }
    hls_utest_remove_all(home);
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
// WebRTC builder has no parameter set to send in a STAP, so the player gets the key frame's four NALUs as they are,
// and the encoder stays.
VOID TEST(AppStreamBridgeTest, RtmpHevcParameterSetsInBandToRtc_KeyFrameNalusSent)
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

    // In the build dir, on the checkout's disk: /tmp can be a tmpfs, as on Ubuntu 25.10 and later, which no disk counts.
    utility_utest_disk_write_and_read("./objs/srs-utest-ai46-disk-stat.data");
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

// Sends n 1KB datagrams from a UDP socket with the smallest send buffer to the address, without waiting. Returns the sends
// that failed for a full send buffer.
static int utility_utest_udp_send_full(const sockaddr_in &addr, int n)
{
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    int size = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));

    char buf[1024];
    memset(buf, 0, sizeof(buf));
    int failed = 0;
    for (int i = 0; i < n; i++) {
        if (::sendto(fd, buf, sizeof(buf), MSG_DONTWAIT, (sockaddr *)&addr, sizeof(addr)) < 0 && errno == EAGAIN) {
            failed++;
        }
    }

    ::close(fd);
    return failed;
}

// A WebRTC server sending to a browser on its LAN that went away, such as a laptop that left the Wi-Fi: no host answers ARP
// for the address, so the kernel holds the datagrams while it asks, the small send buffer fills, and the next sends fail.
// The peer is the last addresses of the subnet of the host's first IPv4 interface other than the loopback, such as
// 172.17.255.254 in Docker, the first one no host answers. Returns the sends that failed, or 0 if every address answered.
static int utility_utest_udp_send_to_gone_lan_peer(int n)
{
    ifaddrs *ifap = NULL;
    if (srs_getifaddrs(&ifap) != 0) {
        return 0;
    }

    int failed = 0;
    for (ifaddrs *p = ifap; p && !failed; p = p->ifa_next) {
        if (!p->ifa_addr || !p->ifa_netmask || p->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if ((p->ifa_flags & IFF_LOOPBACK) || !(p->ifa_flags & IFF_UP)) {
            continue;
        }

        uint32_t self = ntohl(((sockaddr_in *)p->ifa_addr)->sin_addr.s_addr);
        uint32_t mask = ntohl(((sockaddr_in *)p->ifa_netmask)->sin_addr.s_addr);
        uint32_t broadcast = (self & mask) | ~mask;
        for (uint32_t i = 1; i <= 3 && i < ~mask && !failed; i++) {
            uint32_t peer = broadcast - i;
            if (peer == self) {
                continue;
            }

            sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(9);
            addr.sin_addr.s_addr = htonl(peer);
            failed = utility_utest_udp_send_full(addr, n);
        }
    }

    srs_freeifaddrs(ifap);
    return failed;
}

// The host failing UDP sends for a full send buffer, as a WebRTC server sending to a LAN peer that went away, then failing
// more before the server's next sample of /proc/net/snmp: the stat has the send buffer errors since the last sample, which
// the RTC server logs as loss, and none once the errors stop.
VOID TEST(AppUtilityTest, LanPeerGoneFillsSendBuffer_SendErrorsSinceLastSample)
{
    ASSERT_GT(utility_utest_udp_send_to_gone_lan_peer(100), 0);
    srs_update_udp_snmp_statistic();
    SrsSnmpUdpStat *stat = srs_get_udp_snmp_stat();
    EXPECT_TRUE(stat->ok_);
    EXPECT_GT(stat->snd_buf_errors_, 0u);

    int failed = utility_utest_udp_send_to_gone_lan_peer(100);
    ASSERT_GT(failed, 0);
    srs_update_udp_snmp_statistic();
    EXPECT_EQ(failed, srs_get_udp_snmp_stat()->snd_buf_errors_delta_);

    srs_update_udp_snmp_statistic();
    EXPECT_EQ(0, srs_get_udp_snmp_stat()->snd_buf_errors_delta_);
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

// The interfaces of a host as /proc/net/dev lists them: the name before the colon, without the padding, and the bytes
// received, the first number after it.
static vector<string> utility_utest_proc_interfaces(map<string, unsigned long long> &rbytes)
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
        name = name.substr(name.find_first_not_of(' '));
        names.push_back(name);
        rbytes[name] = strtoull(colon + 1, NULL, 10);
    }
    fclose(f);

    return names;
}

// A host with an interface named longer than 6 chars, such as enp0s31f6 on a bare-metal server, or gretap0 and ip6tnl0 in
// a Docker container: the server lists it by its whole name, up to the 15 chars Linux allows, with its bytes, so a
// public interface of such a name counts its internet traffic.
VOID TEST(AppUtilityTest, InterfaceNamedLongerThan6Chars_Listed)
{
    map<string, unsigned long long> before;
    vector<string> names = utility_utest_proc_interfaces(before);
    ASSERT_FALSE(names.empty());

    srs_update_network_devices();
    map<string, unsigned long long> after;
    utility_utest_proc_interfaces(after);

    map<string, unsigned long long> listed;
    SrsNetworkDevices *devices = srs_get_network_devices();
    for (int i = 0; i < srs_get_network_devices_count(); i++) {
        SrsNetworkDevices &device = devices[i];
        listed[device.name_] = device.rbytes_;
    }

    for (int i = 0; i < (int)names.size() && i < 16; i++) {
        string name = names[i];
        ASSERT_TRUE(listed.find(name) != listed.end()) << "interface " << name << " not listed";

        // The bytes are read at the update, between the two reads of the test.
        EXPECT_LE(before[name], listed[name]) << "interface " << name;
        EXPECT_GE(after[name], listed[name]) << "interface " << name;
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

// A DH key pair for the RTMP complex handshake whose first key is one OpenSSL gives about 1 in 256 times: the public
// key 2^x mod p has a leading zero byte, so it is 127 bytes. The keys after it are random, as OpenSSL makes them.
class RtmpHandshakeShortFirstKeyDH : public srs_internal::SrsDH
{
public:
    int nn_keys_;
    int first_key_size_;

public:
    RtmpHandshakeShortFirstKeyDH()
    {
        nn_keys_ = 0;
        first_key_size_ = 0;
    }
    virtual ~RtmpHandshakeShortFirstKeyDH()
    {
    }

public:
    virtual srs_error_t do_initialize()
    {
        srs_error_t err = srs_internal::SrsDH::do_initialize();
        if (err != srs_success || nn_keys_++ > 0) {
            return err;
        }

        // A 1023-bit private key, as OpenSSL makes for the 1024-bit RFC 2409 prime, with a 127-byte public key.
        BIGNUM *priv = NULL;
        BN_hex2bn(&priv, "4C6A44610B1E94452244EF6D67EC70DF938279A9749E1116548A68E19998D0FE"
                         "5386CB9047FF145B20BCC539A053F71BAAB3463EA4F887505C25BF60EDCD6E16"
                         "1FB5AC2628FE1F8121C5D60E679EF6A22DA2A8520D21E35F0E34CCD0AA95AB1A"
                         "45A50FF6A273CE0A551339A51A061B9B534B7601A227ADBC9B9C88AB7271B86B");
        const BIGNUM *p = NULL;
        const BIGNUM *g = NULL;
        DH_get0_pqg(pdh, &p, NULL, &g);

        BIGNUM *pub = BN_new();
        BN_CTX *ctx = BN_CTX_new();
        BN_mod_exp(pub, g, priv, p, ctx);
        BN_CTX_free(ctx);

        DH_set0_key(pdh, pub, priv);
        first_key_size_ = BN_num_bytes(pub);
        return err;
    }
};

// A Flash or FFmpeg publisher does the complex handshake, and the server makes a DH key pair for S1. When OpenSSL
// gives a public key with a leading zero byte, 127 bytes, the server drops it and makes another, so the key it sends
// fills all 128 bytes of the S1 key field.
VOID TEST(ProtocolRtmpHandshakeTest, DhPublicKeyWithLeadingZeroByte_ServerMakesAnother)
{
    srs_error_t err = srs_success;

    RtmpHandshakeShortFirstKeyDH dh;
    HELPER_ASSERT_SUCCESS(dh.initialize(true));

    // The first key was the short one, so the server made a second.
    EXPECT_EQ(127, dh.first_key_size_);
    EXPECT_GE(dh.nn_keys_, 2);

    char pkey[128];
    int32_t pkey_size = sizeof(pkey);
    HELPER_EXPECT_SUCCESS(dh.copy_public_key(pkey, pkey_size));
    EXPECT_EQ(128, pkey_size);

    // The new key works for the handshake: the client and the server get the same shared key from each other's keys.
    srs_internal::SrsDH client;
    HELPER_ASSERT_SUCCESS(client.initialize(true));
    char ckey[128];
    int32_t ckey_size = sizeof(ckey);
    HELPER_EXPECT_SUCCESS(client.copy_public_key(ckey, ckey_size));

    char server_shared[128];
    int32_t server_shared_size = sizeof(server_shared);
    HELPER_EXPECT_SUCCESS(dh.copy_shared_key(ckey, ckey_size, server_shared, server_shared_size));
    char client_shared[128];
    int32_t client_shared_size = sizeof(client_shared);
    HELPER_EXPECT_SUCCESS(client.copy_shared_key(pkey, pkey_size, client_shared, client_shared_size));
    ASSERT_EQ(server_shared_size, client_shared_size);
    EXPECT_TRUE(srs_bytes_equal(server_shared, client_shared, server_shared_size));
}

// The HLS volume is remounted read-only after an I/O error right as the server finishes a segment at a key frame: the
// segment and the playlist that lists it are written, but the next segment can not be created. The frame fails and
// the hub stops HLS for the stream; players have the playlist with the finished segment. For TS and for fMP4.
VOID TEST(AppHlsTest, ReadOnlyVolumeAfterSegmentFinished_NextSegmentNotOpened)
{
    srs_error_t err;

    if (true) {
        HlsUtestVolumeScope volume;
        HlsUtestStream s("ai46-hls-ro-next");
        HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1;"));
        HELPER_ASSERT_SUCCESS(s.sequence_headers());
        HELPER_ASSERT_SUCCESS(s.av(0, 900));

        volume.read_only_at("livestream-1.ts.tmp");
        err = s.video(1000, flv_utest_avc_idr, sizeof(flv_utest_avc_idr));
        EXPECT_EQ(ERROR_SYSTEM_FILE_OPENE, srs_error_code(err));
        EXPECT_NE(string::npos, srs_error_desc(err).find("hls: segment open")) << srs_error_desc(err);
        srs_freep(err);

        string m3u8 = s.m3u8();
        EXPECT_NE(string::npos, m3u8.find("\nlivestream-0.ts\n")) << m3u8;
        EXPECT_EQ(1, hls_utest_count(m3u8, "#EXTINF:")) << m3u8;
        EXPECT_TRUE(s.exists("livestream-0.ts"));
        EXPECT_FALSE(s.exists("livestream-1.ts.tmp"));
    }

    if (true) {
        HlsUtestVolumeScope volume;
        HlsUtestStream s("ai46-hls-fmp4-ro-next");
        HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1; hls_use_fmp4 on;"));
        HELPER_ASSERT_SUCCESS(s.sequence_headers());
        HELPER_ASSERT_SUCCESS(s.av(0, 900));

        volume.read_only_at("livestream-1.m4s.tmp");
        err = s.video(1000, flv_utest_avc_idr, sizeof(flv_utest_avc_idr));
        EXPECT_EQ(ERROR_SYSTEM_FILE_OPENE, srs_error_code(err));
        EXPECT_NE(string::npos, srs_error_desc(err).find("open segment")) << srs_error_desc(err);
        srs_freep(err);

        string m3u8 = s.m3u8();
        EXPECT_NE(string::npos, m3u8.find("\nlivestream-0.m4s\n")) << m3u8;
        EXPECT_EQ(1, hls_utest_count(m3u8, "#EXTINF:")) << m3u8;
        EXPECT_TRUE(s.exists("livestream-0.m4s"));
        EXPECT_FALSE(s.exists("livestream-1.m4s.tmp"));
    }
}

// A radio station publishes AAC only, a frame each 23ms, to a vhost with fMP4 HLS segments of 1s, so a segment is cut
// once it passes 2.1s. The fMP4 segment keeps its samples in memory until it is cut. When the disk fills before the
// cut, the segment can not be written at the cut; when the volume is remounted read-only right after the cut wrote the
// segment and the playlist, the next segment can not be created. Either way the frame fails and the hub stops HLS for
// the stream.
VOID TEST(AppHlsTest, Fmp4RadioVolumeFailsAtSegmentCut_FrameFails)
{
    srs_error_t err;

    for (int full = 0; full < 2; full++) {
        HlsUtestVolumeScope volume;
        HlsUtestStream s("ai46-hls-fmp4-radio");
        HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1; hls_use_fmp4 on;"));
        HELPER_ASSERT_SUCCESS(s.audio(0, flv_utest_aac_sh, sizeof(flv_utest_aac_sh)));
        int64_t ts = 0;
        for (; ts <= 2070; ts += 23) {
            HELPER_ASSERT_SUCCESS(s.audio(ts, flv_utest_aac_frame, sizeof(flv_utest_aac_frame)));
        }

        if (full) {
            volume.fill();
        } else {
            volume.read_only_at("livestream-1.m4s.tmp");
        }
        // The frames before the cut are kept in memory, so the first one to fail is the one that cuts the segment.
        err = srs_success;
        for (; err == srs_success && ts <= 2300; ts += 23) {
            err = s.audio(ts, flv_utest_aac_frame, sizeof(flv_utest_aac_frame));
        }
        EXPECT_EQ(full ? ERROR_SYSTEM_FILE_WRITE : ERROR_SYSTEM_FILE_OPENE, srs_error_code(err)) << full;
        EXPECT_NE(string::npos, srs_error_desc(err).find(full ? "segment close" : "open segment")) << srs_error_desc(err);
        srs_freep(err);
        EXPECT_GE(ts, 2100) << full;

        if (full) {
            EXPECT_FALSE(s.exists("livestream.m3u8"));
            EXPECT_FALSE(s.exists("livestream-0.m4s"));
        } else {
            string m3u8 = s.m3u8();
            EXPECT_NE(string::npos, m3u8.find("\nlivestream-0.m4s\n")) << m3u8;
            EXPECT_TRUE(s.exists("livestream-0.m4s"));
            EXPECT_FALSE(s.exists("livestream-1.m4s.tmp"));
        }
    }
}

// The disk fills while a publisher streams fMP4 HLS, right after a key frame's segment is written, at the playlist.
// The new playlist can not be written, so players keep the old one, which lists the earlier segments only, and no
// temporary playlist is left. The frame should fail too, but B38 loses the error, so the test does not check it.
VOID TEST(AppHlsTest, Fmp4DiskFillsAtPlaylist_PlayersKeepOldPlaylist)
{
    srs_error_t err;

    HlsUtestVolumeScope volume;
    HlsUtestStream s("ai46-hls-fmp4-full-m3u8");
    HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1; hls_use_fmp4 on;"));
    HELPER_ASSERT_SUCCESS(s.sequence_headers());
    HELPER_ASSERT_SUCCESS(s.av(0, 1900));
    string m3u8 = s.m3u8();
    ASSERT_EQ(1, hls_utest_count(m3u8, "#EXTINF:")) << m3u8;

    volume.fill_at("livestream.m3u8.temp");
    err = s.video(2000, flv_utest_avc_idr, sizeof(flv_utest_avc_idr));
    srs_freep(err);

    EXPECT_STREQ(m3u8.c_str(), s.m3u8().c_str());
    EXPECT_FALSE(s.exists("livestream.m3u8.temp"));
}

// The fMP4 HLS volume is remounted read-only after an I/O error while a new stream starts, and the stream needs a dir
// of its own that is not there yet. With the default init file in the stream's dir, hls_init_file
// [app]/[stream]/init.mp4, the publisher's first sequence header can not make that dir for the init file. With the init
// file beside the playlist and the segments in the stream's dir, such as hls_init_file [app]/[stream]-init.mp4 with
// hls_fmp4_file [app]/[stream]/[seq].m4s, the init file is written before the volume fails, and the first frame can not
// make the dir for its segment. Either way the media fails and the hub stops HLS for the stream.
VOID TEST(AppHlsTest, Fmp4ReadOnlyVolumeAtNewStreamDir_MediaFails)
{
    srs_error_t err;

    if (true) {
        HlsUtestVolumeScope volume;
        HlsUtestStream s("ai46-hls-fmp4-ro-init-dir");
        HELPER_ASSERT_SUCCESS(s.publish("hls_use_fmp4 on;"));

        volume.read_only();
        err = s.video(0, flv_utest_avc_sh, sizeof(flv_utest_avc_sh));
        EXPECT_EQ(ERROR_SYSTEM_CREATE_DIR, srs_error_code(err));
        EXPECT_NE(string::npos, srs_error_desc(err).find("Create init mp4 dir failed")) << srs_error_desc(err);
        srs_freep(err);
        EXPECT_FALSE(s.exists("livestream"));
    }

    if (true) {
        HlsUtestVolumeScope volume;
        HlsUtestStream s("ai46-hls-fmp4-ro-dir");
        HELPER_ASSERT_SUCCESS(s.publish("hls_use_fmp4 on; hls_init_file [app]/[stream]-init.mp4; hls_fmp4_file [app]/[stream]/[seq].m4s;"));
        HELPER_ASSERT_SUCCESS(s.sequence_headers());
        EXPECT_TRUE(s.exists("livestream-init.mp4"));

        volume.read_only();
        err = s.video(0, flv_utest_avc_idr, sizeof(flv_utest_avc_idr));
        EXPECT_EQ(ERROR_SYSTEM_CREATE_DIR, srs_error_code(err));
        EXPECT_NE(string::npos, srs_error_desc(err).find("create hls m4s segment dir")) << srs_error_desc(err);
        srs_freep(err);
        EXPECT_FALSE(s.exists("livestream"));
    }
}

// The server restarts while a stream is live, and the publisher publishes again, but the playlist the last run wrote
// can not be read: it was written by a packager running as another user, with no read permission for the server, or
// the disk has bad sectors under it. The server can not recover the stream's segments, so it refuses the publish
// with the error, rather than overwrite a playlist it could not read; the playlist is not touched.
VOID TEST(AppHlsTest, UnreadablePlaylistAtRepublish_PublishRefused)
{
    srs_error_t err;

    for (int bad_sectors = 0; bad_sectors < 2; bad_sectors++) {
        HlsUtestVolumeScope volume;
        HlsUtestStream s("ai46-hls-unreadable");
        SrsPath p;
        HELPER_ASSERT_SUCCESS(p.mkdir_all(s.path("")));
        string playlist = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-MEDIA-SEQUENCE:7\n#EXT-X-TARGETDURATION:1\n"
                          "#EXTINF:1.000, no desc\nlivestream-7.ts\n";
        hls_utest_write_file(s.path("livestream.m3u8"), playlist);

        if (bad_sectors) {
            volume.bad_sectors();
        } else {
            volume.unreadable();
        }
        err = s.publish("hls_fragment 1;");
        EXPECT_EQ(bad_sectors ? ERROR_SYSTEM_FILE_READ : ERROR_SYSTEM_FILE_OPENE, srs_error_code(err)) << bad_sectors;
        EXPECT_NE(string::npos, srs_error_desc(err).find(bad_sectors ? "read data" : "open file")) << srs_error_desc(err);
        EXPECT_NE(string::npos, srs_error_desc(err).find("hls: recover stream")) << srs_error_desc(err);
        srs_freep(err);
        EXPECT_STREQ(playlist.c_str(), hls_utest_read_file(s.path("livestream.m3u8")).c_str());
        EXPECT_FALSE(s.exists("livestream-8.ts.tmp"));
    }
}

// The PTS of each PES of the stream id, such as 0xc0 for audio or 0xe0 for video, in a TS file, in order, in 90kHz
// ticks.
static vector<int64_t> hls_utest_pes_pts(const string &ts, uint8_t sid)
{
    vector<int64_t> pts;
    for (size_t pos = 0; pos + 188 <= ts.size(); pos += 188) {
        const uint8_t *p = (const uint8_t *)ts.data() + pos;
        // Only a packet that starts a PES has its header, after the adaptation field, if any.
        int afc = (p[3] >> 4) & 0x03;
        if (p[0] != 0x47 || !(p[1] & 0x40) || afc == 2) {
            continue;
        }
        int offset = (afc == 3) ? 5 + p[4] : 4;
        if (offset + 14 > 188) {
            continue;
        }
        const uint8_t *pes = p + offset;
        if (pes[0] != 0x00 || pes[1] != 0x00 || pes[2] != 0x01 || pes[3] != sid || !(pes[7] & 0x80)) {
            continue;
        }
        const uint8_t *b = pes + 9;
        pts.push_back((((int64_t)b[0] >> 1) & 0x07) << 30 | (int64_t)b[1] << 22 | ((int64_t)b[2] >> 1) << 15 | (int64_t)b[3] << 7 | (b[4] >> 1));
    }
    return pts;
}

// The segment files the playlist lists, in order.
static vector<string> hls_utest_listed_files(HlsUtestStream &s)
{
    vector<string> files;
    vector<string> lines = srs_strings_split(s.m3u8(), "\n");
    for (int i = 0; i < (int)lines.size(); i++) {
        string line = lines.at(i);
        if (srs_strings_ends_with(line, ".ts")) {
            files.push_back(s.path(line));
        }
    }
    return files;
}

// A publisher sends H.264 and AAC to a vhost that times audio by the guessed samples of each frame
// (hls_dts_directly off), with HLS segments of 1s. Its encoder restarts its clocks in the second segment, without
// publishing again, so the timestamps of both tracks go back to 0. The video in the segments follows the new clock, and
// so does the audio: the guessed samples restart with the clock, so the audio goes back to 0 with the video, once,
// then goes on by the guessed samples of each frame, as from the start.
VOID TEST(AppHlsTest, EncoderRestartsClocksWithoutDtsDirectly_AudioRestartsWithVideo)
{
    srs_error_t err;

    HlsUtestStream s("ai46-hls-audio-reset");
    HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1; hls_dts_directly off;"));
    HELPER_ASSERT_SUCCESS(s.sequence_headers());
    HELPER_ASSERT_SUCCESS(s.av(0, 1500));
    HELPER_ASSERT_SUCCESS(s.av(0, 3500));

    vector<string> files = hls_utest_listed_files(s);
    ASSERT_EQ(4, (int)files.size()) << s.m3u8();
    vector<int64_t> audio, video;
    for (int i = 0; i < (int)files.size(); i++) {
        string body = hls_utest_read_file(files.at(i));
        vector<int64_t> a = hls_utest_pes_pts(body, 0xc0);
        vector<int64_t> v = hls_utest_pes_pts(body, 0xe0);
        audio.insert(audio.end(), a.begin(), a.end());
        video.insert(video.end(), v.begin(), v.end());
    }
    // A frame each 100ms of both tracks, 16 before the restart and 30 after it in the listed segments.
    ASSERT_EQ(46, (int)audio.size());
    ASSERT_EQ(46, (int)video.size());

    for (int i = 1; i < 46; i++) {
        if (i == 16) {
            EXPECT_EQ(0, video[i]);
            EXPECT_EQ(0, audio[i]);
        } else {
            EXPECT_EQ(9000, video[i] - video[i - 1]) << i;
            // Each AAC frame 100ms after the last is guessed as 4096 samples of 44.1kHz, 8359.18 ticks.
            EXPECT_NEAR(8359, audio[i] - audio[i - 1], 1) << i;
        }
    }
}

// A publisher sends H.264 and AAC to a vhost with HLS segments of 1s, and its encoder restarts its clocks in the second
// segment, without publishing again, so the timestamps go back to 0. That segment holds the 0.6s before the restart
// and the 1s after it, but it is listed as 1s: the segment's duration is measured from its least timestamp, so the
// time before the restart is lost. Players are told a shorter segment than they get. Each segment should be listed
// as at least the frames it holds, which holds for a fix that measures the time of both clocks or that cuts the
// segment at the restart.
VOID TEST(AppHlsTest, DISABLED_EncoderRestartsClocksMidSegment_SegmentListedShort)
{
    srs_error_t err;

    HlsUtestStream s("ai46-hls-clock-restart");
    HELPER_ASSERT_SUCCESS(s.publish("hls_fragment 1;"));
    HELPER_ASSERT_SUCCESS(s.sequence_headers());
    HELPER_ASSERT_SUCCESS(s.av(0, 1500));
    HELPER_ASSERT_SUCCESS(s.av(0, 3500));

    string m3u8 = s.m3u8();
    vector<int> durations = hls_utest_extinf_ms(m3u8);
    vector<string> files = hls_utest_listed_files(s);
    ASSERT_EQ(files.size(), durations.size()) << m3u8;
    for (int i = 0; i < (int)files.size(); i++) {
        // A video frame each 100ms: a segment of n frames lasts at least (n - 1) * 100ms.
        int nb_frames = (int)hls_utest_pes_pts(hls_utest_read_file(files.at(i)), 0xe0).size();
        EXPECT_GE(durations.at(i), (nb_frames - 1) * 100) << files.at(i) << "\n" << m3u8;
    }
}

// A slow HTTP callback server for the on_hls hook, which takes a while to answer each segment, and notes the sequence
// number of each segment it is called for.
class HlsUtestSlowOnHls : public MockHttpHooks
{
public:
    vector<int> seqs_;

public:
    virtual srs_error_t on_hls(SrsContextId cid, std::string url, ISrsRequest *req, std::string file, std::string ts_url,
                               std::string m3u8, std::string m3u8_url, int sn, srs_utime_t duration)
    {
        seqs_.push_back(sn);
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
        return srs_success;
    }
};

// The server quits, by a SIGTERM, while a stream unpublishes: its timer runs the HLS cycle of each stream, then
// disposes every stream.
class HlsUtestServerQuit : public ISrsCoroutineHandler
{
public:
    HlsUtestStream *s_;
    srs_error_t cycle_err_;
    bool m3u8_at_cycle_;
    bool done_;
    SrsSTCoroutine *trd_;

public:
    HlsUtestServerQuit(HlsUtestStream *s)
    {
        s_ = s;
        cycle_err_ = srs_success;
        m3u8_at_cycle_ = false;
        done_ = false;
        trd_ = new SrsSTCoroutine("hls-quit", this);
    }
    virtual ~HlsUtestServerQuit()
    {
        srs_freep(trd_);
        srs_freep(cycle_err_);
    }

public:
    virtual srs_error_t cycle()
    {
        cycle_err_ = s_->hls_->cycle();
        m3u8_at_cycle_ = s_->exists("livestream.m3u8");
        s_->hls_->dispose();
        done_ = true;
        return srs_success;
    }
};

// A publisher unpublishes a stream whose segments call a slow on_hls callback server: the unpublish waits for the
// callbacks of the segments. Meanwhile the server quits: its timer runs the HLS cycle, which waits for the unpublish
// rather than dispose the stream, though the stream is past hls_dispose, and the quit disposes the stream, which skips
// the unpublish already running and removes the stream's HLS files. Each segment is called back once, and the
// unpublish ends after the callbacks.
VOID TEST(AppHlsTest, ServerQuitsWhileUnpublishWaitsOnHlsCallbacks_UnpublishedOnce)
{
    srs_error_t err;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.mock_parse(string(_MIN_OK_CONF) + "vhost __defaultVhost__ { http_hooks { enabled on; "
                                                                 "on_hls http://127.0.0.1:8085/api/v1/hls; } }"));
    HlsUtestSlowOnHls hooks;
    SrsConfig *saved_config = _srs_config;
    ISrsHttpHooks *saved_hooks = _srs_hooks;
    _srs_config = &conf;
    _srs_hooks = &hooks;

    if (true) {
        HlsUtestStream s("ai46-hls-quit");
        HELPER_EXPECT_SUCCESS(s.publish("hls_fragment 1; hls_dispose 1;"));
        HELPER_EXPECT_SUCCESS(s.sequence_headers());
        HELPER_EXPECT_SUCCESS(s.av(0, 2500));
        EXPECT_TRUE(hooks.seqs_.empty());

        // The stream is past hls_dispose since its last frame, so the timer would dispose it but for the unpublish.
        s.hls_->last_update_time_ = 1;
        HlsUtestServerQuit quit(&s);
        HELPER_EXPECT_SUCCESS(quit.trd_->start());
        s.hls_->on_unpublish();

        EXPECT_TRUE(quit.done_);
        HELPER_EXPECT_SUCCESS(srs_error_copy(quit.cycle_err_));
        EXPECT_TRUE(quit.m3u8_at_cycle_);
        EXPECT_FALSE(s.exists("livestream.m3u8"));
        EXPECT_FALSE(s.exists("livestream-0.ts"));

        EXPECT_EQ(3, (int)hooks.seqs_.size());
        for (int i = 0; i < (int)hooks.seqs_.size(); i++) {
            EXPECT_EQ(i, hooks.seqs_[i]);
        }
    }

    _srs_config = saved_config;
    _srs_hooks = saved_hooks;
}

// The WebRTC server's UDP port answers each browser at the address its packets came from. One browser's network goes
// away, so the host has no route from the port's interface to that address any more: the send to it fails with a
// socket write error, which the session's UDP network returns. The port is shared by every session, so the next send
// to another browser still reaches it.
VOID TEST(AppListenerTest, BrowserAddressUnreachable_SendFailsOtherBrowsersStillServed)
{
    srs_error_t err;

    int port = 0;
    srs_netfd_t server_fd = NULL;
    HELPER_ASSERT_SUCCESS(srs_utest_udp_listen("127.0.0.1", &port, &server_fd));
    SrsUniquePtr<SrsUdpMuxSocket> skt(new SrsUdpMuxSocket(server_fd));

    srs_netfd_t other_fd = NULL;
    HELPER_ASSERT_SUCCESS(srs_udp_listen("127.0.0.1", 0, &other_fd));
    int other_port = srs_get_local_port(srs_netfd_fileno(other_fd));

    // The gone browser, at an address of the documentation range (TEST-NET-1), with no route from loopback.
    sockaddr_in *gone = (sockaddr_in *)&skt->from_;
    memset(gone, 0, sizeof(sockaddr_in));
    gone->sin_family = AF_INET;
    gone->sin_port = htons(9);
    gone->sin_addr.s_addr = inet_addr("192.0.2.1");
    skt->fromlen_ = sizeof(sockaddr_in);

    string answer = "stun-binding-response";
    err = skt->sendto((void *)answer.data(), (int)answer.size(), SRS_UTIME_NO_TIMEOUT);
    EXPECT_EQ(ERROR_SOCKET_WRITE, srs_error_code(err));
    srs_freep(err);

    // The other browser, still on the network.
    sockaddr_in *other = (sockaddr_in *)&skt->from_;
    memset(other, 0, sizeof(sockaddr_in));
    other->sin_family = AF_INET;
    other->sin_port = htons(other_port);
    other->sin_addr.s_addr = inet_addr("127.0.0.1");
    HELPER_EXPECT_SUCCESS(skt->sendto((void *)answer.data(), (int)answer.size(), SRS_UTIME_NO_TIMEOUT));

    char buf[1500];
    sockaddr_in from;
    int fromlen = sizeof(from);
    int n = srs_recvfrom(other_fd, buf, sizeof(buf), (sockaddr *)&from, &fromlen, SRS_UTIME_SECONDS);
    ASSERT_EQ((int)answer.size(), n);
    EXPECT_EQ(answer, string(buf, n));
    EXPECT_EQ(port, (int)ntohs(from.sin_port));

    srs_close_stfd(other_fd);
    srs_close_stfd(server_fd);
}

// The time of an exec's retry loop: it records each wait, as the ingester's mock does, and yields for only 1ms, so the
// loop retries at once.
class NgExecUtestTime : public MockTimeForIngester
{
public:
    virtual void usleep(srs_utime_t duration)
    {
        MockTimeForIngester::usleep(duration);
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }
};

// The factory of an exec: its commands are processes that never fork, each failing to start with the fork error of the
// moment, and its retry loop waits on a recorded time.
class NgExecUtestFactory : public SrsAppFactory
{
public:
    std::vector<MockProcessForFFMPEG *> processes_;
    NgExecUtestTime *time_;
    // The error each new process starts with, owned by the factory.
    srs_error_t fork_error_;

public:
    NgExecUtestFactory()
    {
        time_ = NULL;
        fork_error_ = srs_success;
    }
    virtual ~NgExecUtestFactory()
    {
        srs_freep(fork_error_);
    }

public:
    virtual ISrsProcess *create_process()
    {
        MockProcessForFFMPEG *process = new MockProcessForFFMPEG();
        process->start_error_ = srs_error_copy(fork_error_);
        processes_.push_back(process);
        return process;
    }
    virtual ISrsTime *create_time()
    {
        time_ = new NgExecUtestTime();
        return time_;
    }
};

// A vhost with two exec commands, and the process table is full when an encoder publishes: the first fork fails. The
// exec logs the error and keeps its coroutine, waits 3s, and tries again, the second command too; once processes exit
// and the fork works, both commands run, and the exec logs them for the stream.
VOID TEST(AppNgExecTest, ProcessTableFull_ForkFailsThenRetriedUntilCommandsRun)
{
    srs_error_t err;

    ListenerUtestLog log;
    PithyPrintAtOnceScope pithy(SrsPithyPrint::create_exec);

    MockSrsConfig conf;
    string text = "vhost __defaultVhost__ { exec { enabled on; publish /bin/echo [app]/[stream]; publish /bin/sleep 30; } }";
    HELPER_ASSERT_SUCCESS(conf.mock_parse(string(_MIN_OK_CONF) + text));

    NgExecUtestFactory factory;
    factory.fork_error_ = srs_error_new(ERROR_ENCODER_FORK, "vfork process failed, cli=/bin/echo live/livestream");

    SrsUniquePtr<SrsNgExec> exec(new SrsNgExec());
    exec->config_ = &conf;
    exec->app_factory_ = &factory;
    exec->assemble();

    MockSrsRequest req("__defaultVhost__", "live", "livestream");
    HELPER_ASSERT_SUCCESS(exec->on_publish(&req));
    ASSERT_EQ(2, (int)factory.processes_.size());
    MockProcessForFFMPEG *echo = factory.processes_[0];
    MockProcessForFFMPEG *sleeper = factory.processes_[1];
    EXPECT_EQ("/bin/echo", echo->binary_);
    EXPECT_EQ("/bin/sleep", sleeper->binary_);

    // The fork fails at each try: the exec warns, waits 3s and retries; the second command waits for the first.
    for (int i = 0; i < 100 && echo->start_count_ < 2; i++) {
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }
    ASSERT_GE(echo->start_count_, 2);
    EXPECT_EQ(0, sleeper->start_count_);
    EXPECT_FALSE(echo->started());
    EXPECT_GE(log.warns("EXEC: Ignore error"), 2);
    EXPECT_GE(log.warns("process start"), 2);
    EXPECT_GE(log.warns("vfork process failed"), 2);
    ASSERT_TRUE(factory.time_ != NULL);
    EXPECT_GE(factory.time_->usleep_count_, 2);
    EXPECT_EQ(3 * SRS_UTIME_SECONDS, factory.time_->last_usleep_duration_);
    EXPECT_TRUE(log.last_trace("-> EXE ").empty());

    // Processes exit and the fork works again: the next try runs both commands and logs them.
    srs_freep(echo->start_error_);
    srs_freep(sleeper->start_error_);
    int warned = log.warns("EXEC: Ignore error");
    for (int i = 0; i < 100 && log.last_trace("-> EXE ").empty(); i++) {
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }
    EXPECT_TRUE(echo->started());
    EXPECT_TRUE(sleeper->started());
    EXPECT_EQ(warned, log.warns("EXEC: Ignore error"));
    string line = log.last_trace("-> EXE ");
    EXPECT_NE(string::npos, line.find("publish=2, input=__defaultVhost__/live/livestream")) << line;

    // The encoder stops: the exec holds no command.
    exec->on_unpublish();
    EXPECT_TRUE(exec->exec_publishs_.empty());
}

// The main loop's wait between cycles: it records each wait, and the signals an operator sends while the server waits,
// one per wait, reach the server as the signal manager hands them over.
class ServerUtestTime : public ISrsTime
{
public:
    SrsServer *server_;
    std::vector<srs_utime_t> *waits_;
    std::vector<int> signals_;

public:
    ServerUtestTime(SrsServer *server, std::vector<srs_utime_t> *waits)
    {
        server_ = server;
        waits_ = waits;
    }
    virtual ~ServerUtestTime()
    {
    }

public:
    virtual void usleep(srs_utime_t duration)
    {
        waits_->push_back(duration);
        if (waits_->size() <= signals_.size()) {
            server_->on_signal(signals_.at(waits_->size() - 1));
        }
    }
};

// The factory of a server whose main loop waits on a recorded time.
class ServerUtestFactory : public SrsAppFactory
{
public:
    SrsServer *server_;
    std::vector<srs_utime_t> waits_;
    std::vector<int> signals_;

public:
    ServerUtestFactory(SrsServer *server)
    {
        server_ = server;
    }
    virtual ~ServerUtestFactory()
    {
    }

public:
    virtual ISrsTime *create_time()
    {
        ServerUtestTime *time = new ServerUtestTime(server_, &waits_);
        time->signals_ = signals_;
        return time;
    }
};

// A running server with no signal cycles once a second. The operator edits the config and sends SIGHUP while the
// server waits: the next cycle reloads it, and the server goes on. Then SIGTERM: the next cycle sees it and the main
// loop ends, so the server stops.
VOID TEST(ServerCycleTest, ReloadThenQuitWhileWaiting_LoopCyclesEachSecondUntilQuit)
{
    srs_error_t err;

    MockAppConfigForDo2Cycle config;
    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->assemble();
    server->config_ = &config;

    ServerUtestFactory factory(server.get());
    factory.signals_.push_back(SRS_SIGNAL_RELOAD);
    factory.signals_.push_back(SRS_SIGNAL_FAST_QUIT);
    server->app_factory_ = &factory;

    server->signal_fast_quit_ = false;
    server->signal_gracefully_quit_ = false;
    HELPER_EXPECT_SUCCESS(server->do_cycle());

    // Two waits of a second: the reload came in the first, the quit in the second.
    ASSERT_EQ(2, (int)factory.waits_.size());
    EXPECT_EQ(1 * SRS_UTIME_SECONDS, factory.waits_.at(0));
    EXPECT_EQ(1 * SRS_UTIME_SECONDS, factory.waits_.at(1));

    // The reload ran once, in the cycle after the first wait, and the server went on to wait again.
    EXPECT_EQ(1, config.reload_count_);
    EXPECT_FALSE(server->signal_reload_);
    EXPECT_TRUE(server->signal_fast_quit_);
    EXPECT_FALSE(server->signal_gracefully_quit_);

    server->app_factory_ = NULL;
    server->config_ = NULL;
}

#ifndef _WIN32 // POSIX only: the death test forks, and abort raises SIGABRT
// The operator asks for a core dump with SIGABRT (`kill -ABRT`, or the raw API's signal): the server aborts at once,
// so the system writes the core of the running process.
VOID TEST(ServerSignalDeathTest, AbortSignal_ServerAbortsForCoreDump)
{
    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->assemble();

    EXPECT_EXIT(server->on_signal(SRS_SIGNAL_ASSERT_ABORT), ::testing::KilledBySignal(SIGABRT), "");

    // The test process got no quit or reload from the signal.
    EXPECT_FALSE(server->signal_fast_quit_);
    EXPECT_FALSE(server->signal_gracefully_quit_);
    EXPECT_FALSE(server->signal_reload_);
}

// The file size limit of the process during a test, put back at the end, with SIGXFSZ ignored, so a write past the
// limit fails with EFBIG instead of killing the process.
class ServerUtestFileSizeLimit
{
public:
    struct rlimit old_;
    void (*old_handler_)(int);

public:
    ServerUtestFileSizeLimit(rlim_t limit)
    {
        old_handler_ = ::signal(SIGXFSZ, SIG_IGN);
        ::getrlimit(RLIMIT_FSIZE, &old_);
        struct rlimit r = old_;
        r.rlim_cur = limit;
        ::setrlimit(RLIMIT_FSIZE, &r);
    }
    virtual ~ServerUtestFileSizeLimit()
    {
        ::setrlimit(RLIMIT_FSIZE, &old_);
        ::signal(SIGXFSZ, old_handler_);
    }
};

// SRS starts with no room to write the pid: its file size limit is 0 (`ulimit -f 0`, or `LimitFSIZE=0` in a systemd
// unit), as a full quota gives. The pid file opens and locks, but writing the pid fails, so the server refuses to
// start, and the pid file holds no pid.
VOID TEST(PidFileLockerTest, NoRoomToWritePid_ServerRefusesToStart)
{
    srs_error_t err;

    string path = _srs_tmp_file_prefix + "ai46-pid-no-room";
    ::unlink(path.c_str());
    MockAppConfigForPidFile config(path);

    // The pid file gets the lowest free fd.
    int fd = ::open("/dev/null", O_RDONLY);
    ASSERT_GE(fd, 0);
    ::close(fd);

    if (true) {
        ServerUtestFileSizeLimit limit(0);

        SrsPidFileLocker locker;
        locker.config_ = &config;
        err = locker.acquire();
        EXPECT_EQ(ERROR_SYSTEM_PID_WRITE_FILE, srs_error_code(err));
        srs_freep(err);
        EXPECT_EQ(-1, locker.pid_fd_);
        locker.config_ = NULL;
    }

    // The pid file is there, empty.
    struct stat st;
    ASSERT_EQ(0, ::stat(path.c_str(), &st));
    EXPECT_EQ(0, (int)st.st_size);

    // The failed start leaves the file open; the process exits next, which closes it. Close it here as that exit does.
    EXPECT_NE(-1, ::fcntl(fd, F_GETFD));
    ::close(fd);

    // With room again, the next start writes its pid.
    if (true) {
        SrsPidFileLocker locker;
        locker.config_ = &config;
        HELPER_EXPECT_SUCCESS(locker.acquire());
        EXPECT_NE(-1, locker.pid_fd_);
        locker.config_ = NULL;
    }
    ASSERT_EQ(0, ::stat(path.c_str(), &st));
    EXPECT_EQ((int)srs_strconv_format_int(getpid()).length(), (int)st.st_size);

    ::unlink(path.c_str());
}
#endif

#ifdef _WIN32 // Windows only: the Windows half of the platform shims; Linux and macOS run the POSIX half.
static string win_tmp_file(const char *name)
{
    return _srs_tmp_file_prefix + "ai46-win-" + name;
}

static void win_write_file(const string &path, const string &data)
{
    FILE *fp = ::fopen(path.c_str(), "wb");
    ASSERT_TRUE(fp != NULL);
    ASSERT_EQ(data.size(), ::fwrite(data.data(), 1, data.size(), fp));
    ::fclose(fp);
}

static string win_read_file(const string &path)
{
    string data;
    FILE *fp = ::fopen(path.c_str(), "rb");
    if (!fp) {
        return data;
    }

    char buf[256];
    size_t n;
    while ((n = ::fread(buf, 1, sizeof(buf), fp)) > 0) {
        data.append(buf, n);
    }
    ::fclose(fp);
    return data;
}

// A program of Windows by its full path, as a config names FFmpeg: srs_process_spawn does not search PATH.
static string win_system_exe(const char *name)
{
    char dir[MAX_PATH];
    UINT n = ::GetSystemDirectoryA(dir, sizeof(dir));
    return string(dir, n) + "\\" + name;
}

// Start cmd.exe with the command as SRS starts an engine, wait for it, and return its exit code, or -1 when it
// did not start.
static int win_spawn_cmd(const string &command, const string &out, const string &err)
{
    string cmd = win_system_exe("cmd.exe");
    const char *argv[] = {cmd.c_str(), "/c", command.c_str(), NULL};
    pid_t pid = srs_process_spawn(cmd.c_str(), argv, out.c_str(), err.c_str(), "cid", "info\n");
    if (pid < 0) {
        return -1;
    }

    int status = -1;
    if (srs_waitpid(pid, &status, 0) != pid) {
        return -1;
    }
    return status;
}

// An engine writes its output and its errors to the two log files of its config, and its exit code reaches SRS.
VOID TEST(WindowsPlatformTest, EngineOutputAndErrorsGoToTheirLogs)
{
    string out = win_tmp_file("spawn-out");
    string err = win_tmp_file("spawn-err");
    ::unlink(out.c_str());
    ::unlink(err.c_str());

    EXPECT_EQ(3, win_spawn_cmd("echo out& 1>&2 echo err& exit 3", out, err));

    string got = win_read_file(out);
    EXPECT_TRUE(srs_strings_ends_with(got, "out\r\n")) << got;
    EXPECT_EQ("err\r\n", win_read_file(err));

    ::unlink(out.c_str());
    ::unlink(err.c_str());
}

// The log starts with the banner that names SRS, the context and the engine, before anything the engine writes;
// with no error log, the errors go to SRS's own stderr.
VOID TEST(WindowsPlatformTest, EngineLogStartsWithTheBanner)
{
    string out = win_tmp_file("spawn-banner");
    ::unlink(out.c_str());

    string cmd = win_system_exe("cmd.exe");
    const char *argv[] = {cmd.c_str(), "/c", "echo out", NULL};
    pid_t pid = srs_process_spawn(cmd.c_str(), argv, out.c_str(), "", "cid", "info\n");
    ASSERT_GT(pid, 0);
    int status = -1;
    ASSERT_EQ(pid, srs_waitpid(pid, &status, 0));
    EXPECT_EQ(0, status);

    string got = win_read_file(out);
    string head = "\nprocess ppid=" + srs_strconv_format_int(::GetCurrentProcessId()) + ", cid=cid, pid=" +
                  srs_strconv_format_int(pid) + ", in=0, out=1, err=2\ninfo\n";
    EXPECT_EQ(head + "out\r\n", got);

    ::unlink(out.c_str());
}

static string win_wide_to_utf8(const wchar_t *s)
{
    int n = ::WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    if (n <= 1) {
        return string();
    }
    string v(n - 1, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, s, -1, &v[0], n, NULL, NULL);
    return v;
}

// An engine gets each argument as the config wrote it, with spaces, quotes, backslashes and empty ones, such as an
// output under Program Files. cmd.exe echoes its command line as it got it, which the C runtime of an engine such
// as FFmpeg splits as CommandLineToArgvW does.
VOID TEST(WindowsPlatformTest, EngineGetsTheArgsAsIs)
{
    string out = win_tmp_file("spawn-args");
    ::unlink(out.c_str());

    vector<string> args;
    args.push_back("a b");
    args.push_back("c\"d");
    args.push_back("e\\");
    args.push_back("");
    args.push_back("f\\\\\"g h");
    args.push_back("C:\\Program Files\\ffmpeg\\");

    string cmd = win_system_exe("cmd.exe");
    vector<const char *> argv;
    argv.push_back(cmd.c_str());
    argv.push_back("/c");
    argv.push_back("echo");
    for (int i = 0; i < (int)args.size(); i++) {
        argv.push_back(args[i].c_str());
    }
    argv.push_back(NULL);

    pid_t pid = srs_process_spawn(cmd.c_str(), &argv[0], out.c_str(), "", "cid", "");
    ASSERT_GT(pid, 0);
    int status = -1;
    ASSERT_EQ(pid, srs_waitpid(pid, &status, 0));
    EXPECT_EQ(0, status);

    // The echoed line follows the banner, which ends with the empty info.
    string got = win_read_file(out);
    size_t pos = got.find("err=2\n");
    ASSERT_NE(string::npos, pos) << got;
    string line = got.substr(pos + 6);
    ASSERT_TRUE(srs_strings_ends_with(line, "\r\n")) << line;
    line = "x " + line.substr(0, line.size() - 2);

    wstring wline(line.begin(), line.end());
    int n = 0;
    LPWSTR *parsed = ::CommandLineToArgvW(wline.c_str(), &n);
    ASSERT_TRUE(parsed != NULL);
    EXPECT_EQ((int)args.size() + 1, n) << line;
    for (int i = 0; i < (int)args.size() && i + 1 < n; i++) {
        EXPECT_EQ(args[i], win_wide_to_utf8(parsed[i + 1])) << line;
    }
    ::LocalFree(parsed);

    ::unlink(out.c_str());
}

// The log of an engine that ran before keeps its lines; the next run appends, as a log does.
VOID TEST(WindowsPlatformTest, EngineAppendsToItsLog)
{
    string err = win_tmp_file("spawn-append");
    win_write_file(err, "old\r\n");

    EXPECT_EQ(0, win_spawn_cmd("1>&2 echo new", "/dev/null", err));

    EXPECT_EQ("old\r\nnew\r\n", win_read_file(err));

    ::unlink(err.c_str());
}

// One file for both logs, as ff_log_dir gives an engine, keeps the lines of both in order, none written over.
VOID TEST(WindowsPlatformTest, EngineSharesOneLogForBothOutputs)
{
    string out = win_tmp_file("spawn-both");
    ::unlink(out.c_str());

    EXPECT_EQ(0, win_spawn_cmd("echo out& 1>&2 echo err", out, out));

    string got = win_read_file(out);
    EXPECT_TRUE(srs_strings_ends_with(got, "out\r\nerr\r\n")) << got;

    ::unlink(out.c_str());
}

// An engine that reads its input, such as sort, ends at once on the null device, where SRS's console would hold it.
VOID TEST(WindowsPlatformTest, EngineHasNoInput)
{
    string err = win_tmp_file("spawn-stdin");
    ::unlink(err.c_str());

    EXPECT_EQ(0, win_spawn_cmd("sort& 1>&2 echo done", "/dev/null", err));

    EXPECT_EQ("done\r\n", win_read_file(err));

    ::unlink(err.c_str());
}

// With ff_log_dir set to /dev/null, as the default config does, the engine writes to the null device.
VOID TEST(WindowsPlatformTest, EngineLogsToTheNullDevice)
{
    EXPECT_EQ(0, win_spawn_cmd("echo out& 1>&2 echo err", "/dev/null", "/dev/null"));
}

// A config from Linux names the engine without .exe, such as ./objs/ffmpeg/bin/ffmpeg; it still starts.
VOID TEST(WindowsPlatformTest, EngineNamedWithoutExeStarts)
{
    string cmd = win_system_exe("cmd");
    const char *argv[] = {cmd.c_str(), "/c", "exit 5", NULL};
    pid_t pid = srs_process_spawn(cmd.c_str(), argv, "/dev/null", "/dev/null", "cid", "");
    ASSERT_GT(pid, 0);

    int status = -1;
    EXPECT_EQ(pid, srs_waitpid(pid, &status, 0));
    EXPECT_EQ(5, status);
}

// An engine not installed at the path of the config does not start, and SRS learns why.
VOID TEST(WindowsPlatformTest, EngineNotInstalledFails)
{
    string bin = win_tmp_file("no-such-ffmpeg");
    const char *argv[] = {bin.c_str(), "-i", "in.flv", NULL};

    errno = 0;
    EXPECT_EQ(-1, srs_process_spawn(bin.c_str(), argv, "/dev/null", "/dev/null", "cid", ""));
    EXPECT_EQ(ENOENT, errno);
}

// An engine whose log dir is missing does not start, as SRS cannot open its log.
VOID TEST(WindowsPlatformTest, EngineLogDirMissingFails)
{
    string cmd = win_system_exe("cmd.exe");
    string log = win_tmp_file("no-such-dir/ffmpeg.log");
    const char *argv[] = {cmd.c_str(), "/c", "exit 0", NULL};

    errno = 0;
    EXPECT_EQ(-1, srs_process_spawn(cmd.c_str(), argv, log.c_str(), "/dev/null", "cid", ""));
    EXPECT_EQ(ENOENT, errno);
}

// An engine is reaped once; after that SRS has no child by that pid to signal or wait for.
VOID TEST(WindowsPlatformTest, EngineIsReapedOnce)
{
    string cmd = win_system_exe("cmd.exe");
    const char *argv[] = {cmd.c_str(), "/c", "exit 0", NULL};
    pid_t pid = srs_process_spawn(cmd.c_str(), argv, "/dev/null", "/dev/null", "cid", "");
    ASSERT_GT(pid, 0);

    int status = -1;
    EXPECT_EQ(pid, srs_waitpid(pid, &status, 0));

    errno = 0;
    EXPECT_EQ(-1, srs_waitpid(pid, &status, WNOHANG));
    EXPECT_EQ(ECHILD, errno);
    errno = 0;
    EXPECT_EQ(-1, srs_kill(pid, SIGTERM));
    EXPECT_EQ(ESRCH, errno);
}

// SRS sees a running engine as not quit, kills it, and reaps it with the exit code of a killed process.
VOID TEST(WindowsPlatformTest, KilledEngineIsReaped)
{
    string ping = win_system_exe("PING.EXE");
    const char *argv[] = {ping.c_str(), "-n", "30", "127.0.0.1", NULL};
    pid_t pid = srs_process_spawn(ping.c_str(), argv, "/dev/null", "/dev/null", "cid", "");
    ASSERT_GT(pid, 0);

    int status = -1;
    EXPECT_EQ(0, srs_waitpid(pid, &status, WNOHANG));

    EXPECT_EQ(0, srs_kill(pid, SIGKILL));
    EXPECT_EQ(pid, srs_waitpid(pid, &status, 0));
    EXPECT_EQ(128 + SIGKILL, status);
}

// An engine that quit on its own, such as FFmpeg at the end of its input, still takes the stop signal, and SRS
// reaps it with its own exit code.
VOID TEST(WindowsPlatformTest, EngineThatQuitTakesTheSignal)
{
    string cmd = win_system_exe("cmd.exe");
    const char *argv[] = {cmd.c_str(), "/c", "exit 4", NULL};
    pid_t pid = srs_process_spawn(cmd.c_str(), argv, "/dev/null", "/dev/null", "cid", "");
    ASSERT_GT(pid, 0);

    HANDLE h = ::OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
    ASSERT_TRUE(h != NULL);
    EXPECT_EQ(WAIT_OBJECT_0, ::WaitForSingleObject(h, 10000));
    ::CloseHandle(h);

    EXPECT_EQ(0, srs_kill(pid, SIGTERM));
    int status = -1;
    EXPECT_EQ(pid, srs_waitpid(pid, &status, 0));
    EXPECT_EQ(4, status);
}

// An unpublish stops an engine that keeps running: SRS asks it to quit, then ends it, and reaps it.
VOID TEST(WindowsPlatformTest, StopEndsARunningEngine)
{
    srs_error_t err;

    string ping = win_system_exe("PING.EXE");
    vector<string> argv;
    argv.push_back(ping);
    argv.push_back("-n");
    argv.push_back("30");
    argv.push_back("127.0.0.1");
    argv.push_back("1>/dev/null");

    SrsUniquePtr<SrsProcess> process(new SrsProcess());
    HELPER_ASSERT_SUCCESS(process->initialize(ping, argv));
    HELPER_ASSERT_SUCCESS(process->start());
    pid_t pid = process->get_pid();
    EXPECT_GT(pid, 0);

    HELPER_EXPECT_SUCCESS(process->cycle());
    EXPECT_TRUE(process->started());

    process->stop();
    EXPECT_FALSE(process->started());

    int status = -1;
    errno = 0;
    EXPECT_EQ(-1, srs_waitpid(pid, &status, WNOHANG));
    EXPECT_EQ(ECHILD, errno);
}

// A second SRS with the same pid file refuses to start, and the pid of the first stays in the file. A Windows lock
// belongs to the handle, so a second handle in this process plays the second SRS.
VOID TEST(WindowsPlatformTest, SecondServerWithSamePidFileRefused)
{
    srs_error_t err;

    string path = win_tmp_file("pid");
    ::unlink(path.c_str());
    MockAppConfigForPidFile config(path);

    if (true) {
        SrsPidFileLocker first;
        first.config_ = &config;
        HELPER_EXPECT_SUCCESS(first.acquire());

        SrsPidFileLocker second;
        second.config_ = &config;
        err = second.acquire();
        EXPECT_EQ(ERROR_SYSTEM_PID_ALREADY_RUNNING, srs_error_code(err));
        srs_freep(err);
        EXPECT_EQ(-1, second.pid_fd_);

        EXPECT_EQ(srs_strconv_format_int(::GetCurrentProcessId()), win_read_file(path));
        first.config_ = NULL;
        second.config_ = NULL;
    }

    ::unlink(path.c_str());
}

// A program that reads the playlist without letting others delete it, such as a web server serving it, keeps
// SRS from replacing it until it closes the file.
VOID TEST(WindowsPlatformTest, PlaylistHeldByAReaderNotReplaced)
{
    string m3u8 = win_tmp_file("live.m3u8");
    string temp = win_tmp_file("live.m3u8.temp");
    win_write_file(m3u8, "#EXTM3U\n#old\n");
    win_write_file(temp, "#EXTM3U\n#new\n");

    FILE *reader = ::fopen(m3u8.c_str(), "rb");
    ASSERT_TRUE(reader != NULL);
    errno = 0;
    EXPECT_EQ(-1, srs_rename(temp.c_str(), m3u8.c_str()));
    EXPECT_EQ(EACCES, errno);
    ::fclose(reader);

    EXPECT_EQ("#EXTM3U\n#old\n", win_read_file(m3u8));
    EXPECT_EQ(0, srs_rename(temp.c_str(), m3u8.c_str()));
    EXPECT_EQ("#EXTM3U\n#new\n", win_read_file(m3u8));

    ::unlink(m3u8.c_str());
}

// A hook or an origin at [::1] that is down refuses at once, as at 127.0.0.1; Windows would retry the SYN for
// about 2 s.
VOID TEST(WindowsPlatformTest, Ipv6LoopbackConnectRefusedAtOnce)
{
    srs_error_t err;

    int port = 0;
    HELPER_ASSERT_SUCCESS(srs_utest_free_port(SOCK_STREAM, &port));

    srs_netfd_t stfd = NULL;
    srs_utime_t starttime = srs_time_now_realtime();
    err = srs_tcp_connect("::1", port, 3 * SRS_UTIME_SECONDS, &stfd);
    srs_utime_t elapsed = srs_time_now_realtime() - starttime;

    EXPECT_EQ(ERROR_ST_CONNECT, srs_error_code(err));
    srs_freep(err);
    EXPECT_TRUE(stfd == NULL);
    EXPECT_LT(elapsed, 500 * SRS_UTIME_MILLISECONDS);
}

// A listen on an address this host does not have, such as a config copied from another server, fails with the
// errno SRS logs for it.
VOID TEST(WindowsPlatformTest, ListenOnAnAddressNotOnThisHostFails)
{
    srs_error_t err;

    int types[] = {SOCK_STREAM, SOCK_DGRAM};
    for (int i = 0; i < 2; i++) {
        int port = 0;
        HELPER_ASSERT_SUCCESS(srs_utest_free_port(types[i], &port));

        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(0xc0000201); // 192.0.2.1, TEST-NET-1

        srs_socket_t fd = ::socket(AF_INET, types[i], 0);
        ASSERT_NE(SRS_INVALID_SOCKET, fd);
        errno = 0;
        EXPECT_EQ(-1, srs_bind(fd, (sockaddr *)&addr, sizeof(addr)));
        EXPECT_EQ(EADDRNOTAVAIL, errno);
        srs_close_socket(fd);
    }
}

// The summaries API names the OS: Windows, its version and build, and the host name.
VOID TEST(WindowsPlatformTest, UnameNamesWindows)
{
    utsname name;
    ASSERT_EQ(0, srs_uname(&name));

    EXPECT_STREQ("Windows", name.sysname);
    EXPECT_STREQ("x86_64", name.machine);
    EXPECT_STRNE("", name.nodename);
    // Windows 10 and 11, and the servers since 2016, are all version 10.0, told apart by the build.
    EXPECT_STREQ("10", name.release);
    string version = name.version;
    ASSERT_TRUE(srs_strings_starts_with(version, "10.0.")) << version;
    EXPECT_GE(::atoi(version.substr(5).c_str()), 10240) << version;
}

// The summaries API gives the uptime of the host, from its boot.
VOID TEST(WindowsPlatformTest, PlatformInfoGivesTheHostUptime)
{
    srs_update_platform_info();
    double uptime = srs_get_platform_info()->os_uptime_;
    double expect = (double)::GetTickCount64() / 1000;

    EXPECT_GT(uptime, 0);
    EXPECT_NEAR(expect, uptime, 2);
}
#endif
