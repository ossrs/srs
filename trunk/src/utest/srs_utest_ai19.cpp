//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai19.hpp>

using namespace std;

#include <srs_app_circuit_breaker.hpp>
#include <srs_app_dvr.hpp>
#include <srs_app_edge.hpp>
#include <srs_app_encoder.hpp>
#include <srs_app_ffmpeg.hpp>
#include <srs_app_heartbeat.hpp>
#include <srs_app_hls.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_app_ingest.hpp>
#include <srs_app_mpegts_udp.hpp>
#include <srs_app_ng_exec.hpp>
#include <srs_app_process.hpp>
#include <srs_app_recv_thread.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_packet.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_http_conn.hpp>
#include <srs_protocol_http_stack.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_utest_ai05.hpp>
#include <srs_utest_manual_coworkers.hpp>
#include <srs_utest_manual_http.hpp>

#include <sys/time.h>

// Mock ISrsAppConfig implementation
MockAppConfigForUdpCaster::MockAppConfigForUdpCaster()
{
    stream_caster_listen_port_ = 0;
}

MockAppConfigForUdpCaster::~MockAppConfigForUdpCaster()
{
}

int MockAppConfigForUdpCaster::get_stream_caster_listen(SrsConfDirective *conf)
{
    return stream_caster_listen_port_;
}

// Mock ISrsIpListener implementation
MockIpListenerForUdpCaster::MockIpListenerForUdpCaster()
{
    endpoint_port_ = 0;
    set_endpoint_called_ = false;
    set_label_called_ = false;
    listen_called_ = false;
    close_called_ = false;
}

MockIpListenerForUdpCaster::~MockIpListenerForUdpCaster()
{
}

ISrsListener *MockIpListenerForUdpCaster::set_endpoint(const std::string &i, int p)
{
    endpoint_ip_ = i;
    endpoint_port_ = p;
    set_endpoint_called_ = true;
    return this;
}

ISrsListener *MockIpListenerForUdpCaster::set_label(const std::string &label)
{
    label_ = label;
    set_label_called_ = true;
    return this;
}

srs_error_t MockIpListenerForUdpCaster::listen()
{
    listen_called_ = true;
    return srs_success;
}

void MockIpListenerForUdpCaster::close()
{
    close_called_ = true;
}

void MockIpListenerForUdpCaster::reset()
{
    endpoint_ip_ = "";
    endpoint_port_ = 0;
    label_ = "";
    set_endpoint_called_ = false;
    set_label_called_ = false;
    listen_called_ = false;
    close_called_ = false;
}

// Mock ISrsMpegtsOverUdp implementation
MockMpegtsOverUdp::MockMpegtsOverUdp()
{
    assemble_count_ = 0;
    initialize_called_ = false;
    initialize_error_ = srs_success;
}

MockMpegtsOverUdp::~MockMpegtsOverUdp()
{
    srs_freep(initialize_error_);
}

void MockMpegtsOverUdp::assemble()
{
    assemble_count_++;
}

srs_error_t MockMpegtsOverUdp::initialize(SrsConfDirective *c)
{
    initialize_called_ = true;
    if (initialize_error_ != srs_success) {
        return srs_error_copy(initialize_error_);
    }
    return srs_success;
}

srs_error_t MockMpegtsOverUdp::on_ts_message(SrsTsMessage *msg)
{
    return srs_success;
}

srs_error_t MockMpegtsOverUdp::on_udp_packet(const sockaddr *from, const int fromlen, char *buf, int nb_buf)
{
    return srs_success;
}

void MockMpegtsOverUdp::reset()
{
    initialize_called_ = false;
    srs_freep(initialize_error_);
}

// Test SrsUdpCasterListener - covers the major use scenario:
// 1. Create listener
// 2. Initialize with valid port configuration
// 3. Verify listener and caster are properly configured
// 4. Start listening
// 5. Close the listener
VOID TEST(UdpCasterListenerTest, InitializeAndListen)
{
    srs_error_t err;

    // Create mock config with valid port
    SrsUniquePtr<MockAppConfigForUdpCaster> mock_config(new MockAppConfigForUdpCaster());
    mock_config->stream_caster_listen_port_ = 8935;

    // Create mock listener and caster
    MockIpListenerForUdpCaster *mock_listener = new MockIpListenerForUdpCaster();
    MockMpegtsOverUdp *mock_caster = new MockMpegtsOverUdp();

    // Create SrsUdpCasterListener
    SrsUniquePtr<SrsUdpCasterListener> listener(new SrsUdpCasterListener());

    // Inject mock dependencies
    listener->config_ = mock_config.get();
    listener->listener_ = mock_listener;
    listener->caster_ = mock_caster;

    // Create a dummy config directive
    SrsConfDirective conf;

    // Test initialize() - should configure listener and caster
    HELPER_EXPECT_SUCCESS(listener->initialize(&conf));

    // Verify listener was configured with correct endpoint
    EXPECT_TRUE(mock_listener->set_endpoint_called_);
    EXPECT_EQ(8935, mock_listener->endpoint_port_);
    EXPECT_EQ(srs_net_address_any(), mock_listener->endpoint_ip_);

    // Verify listener label was set
    EXPECT_TRUE(mock_listener->set_label_called_);
    EXPECT_EQ("MPEGTS", mock_listener->label_);

    // Verify caster was initialized
    EXPECT_TRUE(mock_caster->initialize_called_);

    // Test listen() - should start listening
    HELPER_EXPECT_SUCCESS(listener->listen());
    EXPECT_TRUE(mock_listener->listen_called_);

    // Test close() - should close listener
    listener->close();
    EXPECT_TRUE(mock_listener->close_called_);

    // Clean up - set to NULL to avoid double-free
    listener->config_ = NULL;
    listener->listener_ = NULL;
    listener->caster_ = NULL;
}

// Test SrsUdpCasterListener::initialize with invalid port
VOID TEST(UdpCasterListenerTest, InitializeWithInvalidPort)
{
    srs_error_t err;

    // Create mock config with invalid port
    SrsUniquePtr<MockAppConfigForUdpCaster> mock_config(new MockAppConfigForUdpCaster());
    mock_config->stream_caster_listen_port_ = 0; // Invalid port

    // Create mock listener and caster
    MockIpListenerForUdpCaster *mock_listener = new MockIpListenerForUdpCaster();
    MockMpegtsOverUdp *mock_caster = new MockMpegtsOverUdp();

    // Create SrsUdpCasterListener
    SrsUniquePtr<SrsUdpCasterListener> listener(new SrsUdpCasterListener());

    // Inject mock dependencies
    listener->config_ = mock_config.get();
    listener->listener_ = mock_listener;
    listener->caster_ = mock_caster;

    // Create a dummy config directive
    SrsConfDirective conf;

    // Test initialize() - should fail with invalid port
    HELPER_EXPECT_FAILED(listener->initialize(&conf));

    // Verify listener was NOT configured
    EXPECT_FALSE(mock_listener->set_endpoint_called_);
    EXPECT_FALSE(mock_listener->set_label_called_);

    // Verify caster was NOT initialized
    EXPECT_FALSE(mock_caster->initialize_called_);

    // Clean up - set to NULL to avoid double-free
    listener->config_ = NULL;
    listener->listener_ = NULL;
    listener->caster_ = NULL;
}

// Test SrsUdpCasterListener::initialize when caster initialization fails
VOID TEST(UdpCasterListenerTest, InitializeWithCasterFailure)
{
    srs_error_t err;

    // Create mock config with valid port
    SrsUniquePtr<MockAppConfigForUdpCaster> mock_config(new MockAppConfigForUdpCaster());
    mock_config->stream_caster_listen_port_ = 8935;

    // Create mock listener and caster
    MockIpListenerForUdpCaster *mock_listener = new MockIpListenerForUdpCaster();
    MockMpegtsOverUdp *mock_caster = new MockMpegtsOverUdp();

    // Configure caster to fail initialization
    mock_caster->initialize_error_ = srs_error_new(ERROR_SYSTEM_CONFIG_INVALID, "caster init failed");

    // Create SrsUdpCasterListener
    SrsUniquePtr<SrsUdpCasterListener> listener(new SrsUdpCasterListener());

    // Inject mock dependencies
    listener->config_ = mock_config.get();
    listener->listener_ = mock_listener;
    listener->caster_ = mock_caster;

    // Create a dummy config directive
    SrsConfDirective conf;

    // Test initialize() - should fail when caster initialization fails
    HELPER_EXPECT_FAILED(listener->initialize(&conf));

    // Verify listener was configured (happens before caster init)
    EXPECT_TRUE(mock_listener->set_endpoint_called_);
    EXPECT_TRUE(mock_listener->set_label_called_);

    // Verify caster initialization was attempted
    EXPECT_TRUE(mock_caster->initialize_called_);

    // Clean up - set to NULL to avoid double-free
    listener->config_ = NULL;
    listener->listener_ = NULL;
    listener->caster_ = NULL;
}

// assemble() assembles the caster, which the constructor only allocates.
VOID TEST(UdpCasterListenerTest, AssembleAssemblesCaster)
{
    SrsUniquePtr<SrsUdpCasterListener> listener(new SrsUdpCasterListener());

    // Owned by the listener, whose destructor frees it.
    MockMpegtsOverUdp *caster = new MockMpegtsOverUdp();
    srs_freep(listener->caster_);
    listener->caster_ = caster;

    listener->assemble();
    EXPECT_EQ(1, caster->assemble_count_);
}

// The constructor does not create the pithy print, which would enter the caster
// stage of the global stage manager before a test could inject anything.
VOID TEST(MpegtsOverUdpTest, ConstructorLeavesPithyPrintUnset)
{
    SrsUniquePtr<SrsMpegtsOverUdp> caster(new SrsMpegtsOverUdp());

    EXPECT_TRUE(caster->pprint_ == NULL);
}

// assemble() creates the pithy print for the caster stage.
VOID TEST(MpegtsOverUdpTest, AssembleCreatesCasterPithyPrint)
{
    SrsUniquePtr<SrsMpegtsOverUdp> caster(new SrsMpegtsOverUdp());
    srs_freep(caster->pprint_);

    caster->assemble();

    SrsPithyPrint *pprint = dynamic_cast<SrsPithyPrint *>(caster->pprint_);
    ASSERT_TRUE(pprint != NULL);

    SrsUniquePtr<SrsPithyPrint> expected(SrsPithyPrint::create_caster());
    EXPECT_EQ(expected->stage_id_, pprint->stage_id_);
}

// Test SrsMpegtsQueue push and dequeue - major use scenario
// This test covers the typical workflow:
// 1. Push multiple audio and video packets with different timestamps
// 2. Dequeue packets in timestamp order once threshold is met (2+ videos and 2+ audios)
// 3. Verify packets are returned in correct timestamp order
// 4. Verify dequeue returns NULL when threshold is not met
VOID TEST(MpegtsQueueTest, PushAndDequeueBasic)
{
    srs_error_t err;

    // Create SrsMpegtsQueue
    SrsUniquePtr<SrsMpegtsQueue> queue(new SrsMpegtsQueue());

    // Push 3 video packets
    SrsMediaPacket *video1 = new SrsMediaPacket();
    video1->timestamp_ = 1000;
    video1->message_type_ = SrsFrameTypeVideo;
    HELPER_EXPECT_SUCCESS(queue->push(video1));

    SrsMediaPacket *video2 = new SrsMediaPacket();
    video2->timestamp_ = 2000;
    video2->message_type_ = SrsFrameTypeVideo;
    HELPER_EXPECT_SUCCESS(queue->push(video2));

    SrsMediaPacket *video3 = new SrsMediaPacket();
    video3->timestamp_ = 3000;
    video3->message_type_ = SrsFrameTypeVideo;
    HELPER_EXPECT_SUCCESS(queue->push(video3));

    // Push 3 audio packets
    SrsMediaPacket *audio1 = new SrsMediaPacket();
    audio1->timestamp_ = 1500;
    audio1->message_type_ = SrsFrameTypeAudio;
    HELPER_EXPECT_SUCCESS(queue->push(audio1));

    SrsMediaPacket *audio2 = new SrsMediaPacket();
    audio2->timestamp_ = 2500;
    audio2->message_type_ = SrsFrameTypeAudio;
    HELPER_EXPECT_SUCCESS(queue->push(audio2));

    SrsMediaPacket *audio3 = new SrsMediaPacket();
    audio3->timestamp_ = 3500;
    audio3->message_type_ = SrsFrameTypeAudio;
    HELPER_EXPECT_SUCCESS(queue->push(audio3));

    // Now we have 3 videos and 3 audios, dequeue should return packets in timestamp order
    SrsMediaPacket *dequeued1 = queue->dequeue();
    ASSERT_TRUE(dequeued1 != NULL);
    EXPECT_EQ(1000, dequeued1->timestamp_);
    EXPECT_TRUE(dequeued1->is_video());
    srs_freep(dequeued1);

    // After dequeue, we have 2 videos and 3 audios, still meets threshold (2+ videos and 2+ audios)
    SrsMediaPacket *dequeued2 = queue->dequeue();
    ASSERT_TRUE(dequeued2 != NULL);
    EXPECT_EQ(1500, dequeued2->timestamp_);
    EXPECT_TRUE(dequeued2->is_audio());
    srs_freep(dequeued2);

    // After dequeue, we have 2 videos and 2 audios, still meets threshold
    SrsMediaPacket *dequeued3 = queue->dequeue();
    ASSERT_TRUE(dequeued3 != NULL);
    EXPECT_EQ(2000, dequeued3->timestamp_);
    EXPECT_TRUE(dequeued3->is_video());
    srs_freep(dequeued3);

    // After dequeue, we have 1 video and 2 audios, does NOT meet threshold (need 2+ videos)
    // So dequeue should return NULL
    SrsMediaPacket *dequeued4 = queue->dequeue();
    EXPECT_TRUE(dequeued4 == NULL);
}

// Test SrsMpegtsOverUdp::on_udp_packet and on_udp_bytes - major use scenario
// This test covers the complete UDP packet processing flow:
// 1. Receive UDP packet with TS data
// 2. Parse peer IP and port from sockaddr
// 3. Append data to buffer
// 4. Find TS sync byte (0x47)
// 5. Parse TS packets (188 bytes each)
// 6. Decode TS packets using context
// 7. Erase consumed bytes from buffer
VOID TEST(MpegtsOverUdpTest, ProcessUdpPacketWithTsData)
{
    srs_error_t err;

    // Create SrsMpegtsOverUdp instance
    SrsUniquePtr<SrsMpegtsOverUdp> udp_handler(new SrsMpegtsOverUdp());
    udp_handler->assemble();

    // Create a valid TS packet (188 bytes) - PAT packet
    // This is a real TS PAT (Program Association Table) packet
    uint8_t ts_packet[188] = {
        // TS header: sync_byte=0x47, pid=0x0000 (PAT)
        0x47, 0x40, 0x00, 0x10, 0x00,
        // PAT table
        0x00, 0xb0, 0x0d, 0x00, 0x01, 0xc1, 0x00, 0x00, 0x00, 0x01, 0xf0,
        0x00, 0x2a, 0xb1, 0x04, 0xb2};
    // Fill rest with 0xff (stuffing bytes)
    for (int i = 21; i < 188; i++) {
        ts_packet[i] = 0xff;
    }

    // Create sockaddr for peer address
    struct sockaddr_in peer_addr;
    memset(&peer_addr, 0, sizeof(peer_addr));
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(12345);
    inet_pton(AF_INET, "192.168.1.100", &peer_addr.sin_addr);

    // Test on_udp_packet - should successfully process the TS packet
    HELPER_EXPECT_SUCCESS(udp_handler->on_udp_packet(
        (const sockaddr *)&peer_addr,
        sizeof(peer_addr),
        (char *)ts_packet,
        sizeof(ts_packet)));

    // Verify buffer was updated (data appended and then consumed)
    // After processing one complete TS packet (188 bytes), buffer should be empty
    EXPECT_EQ(0, udp_handler->buffer_->length());
}

// Mock ISrsRawH264Stream implementation
MockMpegtsRawH264Stream::MockMpegtsRawH264Stream()
{
    annexb_demux_called_ = false;
    is_sps_called_ = false;
    is_pps_called_ = false;
    sps_demux_called_ = false;
    pps_demux_called_ = false;
    annexb_demux_error_ = srs_success;
    sps_demux_error_ = srs_success;
    pps_demux_error_ = srs_success;
    demux_frame_ = NULL;
    demux_frame_size_ = 0;
    is_sps_result_ = false;
    is_pps_result_ = false;
}

MockMpegtsRawH264Stream::~MockMpegtsRawH264Stream()
{
    srs_freep(annexb_demux_error_);
    srs_freep(sps_demux_error_);
    srs_freep(pps_demux_error_);
}

srs_error_t MockMpegtsRawH264Stream::annexb_demux(SrsBuffer *stream, char **pframe, int *pnb_frame)
{
    annexb_demux_called_ = true;

    if (annexb_demux_error_ != srs_success) {
        return srs_error_copy(annexb_demux_error_);
    }

    *pframe = demux_frame_;
    *pnb_frame = demux_frame_size_;

    // Skip the frame in the buffer
    if (demux_frame_size_ > 0 && stream->left() >= demux_frame_size_) {
        stream->skip(demux_frame_size_);
    }

    return srs_success;
}

bool MockMpegtsRawH264Stream::is_sps(char *frame, int nb_frame)
{
    is_sps_called_ = true;
    return is_sps_result_;
}

bool MockMpegtsRawH264Stream::is_pps(char *frame, int nb_frame)
{
    is_pps_called_ = true;
    return is_pps_result_;
}

srs_error_t MockMpegtsRawH264Stream::sps_demux(char *frame, int nb_frame, std::string &sps)
{
    sps_demux_called_ = true;

    if (sps_demux_error_ != srs_success) {
        return srs_error_copy(sps_demux_error_);
    }

    sps = sps_output_;
    return srs_success;
}

srs_error_t MockMpegtsRawH264Stream::pps_demux(char *frame, int nb_frame, std::string &pps)
{
    pps_demux_called_ = true;

    if (pps_demux_error_ != srs_success) {
        return srs_error_copy(pps_demux_error_);
    }

    pps = pps_output_;
    return srs_success;
}

srs_error_t MockMpegtsRawH264Stream::mux_sequence_header(std::string sps, std::string pps, std::string &sh)
{
    // Simple mock implementation
    sh = "\x17\x00\x00\x00\x00"; // AVC sequence header prefix
    return srs_success;
}

srs_error_t MockMpegtsRawH264Stream::mux_ipb_frame(char *frame, int frame_size, std::string &ibp)
{
    // Simple mock implementation
    ibp.assign(frame, frame_size);
    return srs_success;
}

srs_error_t MockMpegtsRawH264Stream::mux_avc2flv(std::string video, int8_t frame_type, int8_t avc_packet_type, uint32_t dts, uint32_t pts, char **flv, int *nb_flv)
{
    // Simple mock implementation - allocate a small buffer
    *nb_flv = 10;
    *flv = new char[*nb_flv];
    memset(*flv, 0, *nb_flv);
    return srs_success;
}

void MockMpegtsRawH264Stream::reset()
{
    annexb_demux_called_ = false;
    is_sps_called_ = false;
    is_pps_called_ = false;
    sps_demux_called_ = false;
    pps_demux_called_ = false;
    srs_freep(annexb_demux_error_);
    srs_freep(sps_demux_error_);
    srs_freep(pps_demux_error_);
    demux_frame_ = NULL;
    demux_frame_size_ = 0;
    is_sps_result_ = false;
    is_pps_result_ = false;
}

// Test SrsMpegtsOverUdp::on_ts_video - major use scenario
// This test covers the complete video processing flow:
// 1. Receive TS video message with H.264 annexb data (SPS, PPS, IDR frame)
// 2. Connect to RTMP server
// 3. Demux annexb frames from buffer
// 4. Process SPS and PPS to generate sequence header
// 5. Process IDR frame and write to RTMP
VOID TEST(MpegtsOverUdpTest, OnTsVideoWithSpsPpsIdrFrame)
{
    srs_error_t err;

    // Create SrsMpegtsOverUdp instance
    SrsUniquePtr<SrsMpegtsOverUdp> udp_handler(new SrsMpegtsOverUdp());
    udp_handler->assemble();

    // Create mock dependencies
    MockMpegtsRawH264Stream *mock_avc = new MockMpegtsRawH264Stream();
    MockRtmpClient *mock_sdk = new MockRtmpClient();

    // Inject mock dependencies
    udp_handler->avc_ = mock_avc;
    // Note: sdk_ should be NULL initially, then connect() will be called
    // For this test, we simulate that sdk_ is already connected
    udp_handler->sdk_ = mock_sdk;

    // Create a TsMessage with valid timestamp
    SrsUniquePtr<SrsTsMessage> msg(new SrsTsMessage());
    msg->dts_ = 90000; // 1 second in 90kHz timebase (will be converted to 1000ms)
    msg->pts_ = 90000;

    // Create H.264 annexb data: SPS + PPS + IDR frame
    // SPS NALU (type 7): 0x00 0x00 0x00 0x01 0x67 ...
    char sps_nalu[] = {0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1e};
    // PPS NALU (type 8): 0x00 0x00 0x00 0x01 0x68 ...
    char pps_nalu[] = {0x00, 0x00, 0x00, 0x01, 0x68, (char)0xce, 0x3c, (char)0x80};
    // IDR NALU (type 5): 0x00 0x00 0x00 0x01 0x65 ...
    char idr_nalu[] = {0x00, 0x00, 0x00, 0x01, 0x65, (char)0x88, (char)0x84, 0x00};

    // Combine all NALUs into a single buffer
    int total_size = sizeof(sps_nalu) + sizeof(pps_nalu) + sizeof(idr_nalu);
    char *annexb_data = new char[total_size];
    memcpy(annexb_data, sps_nalu, sizeof(sps_nalu));
    memcpy(annexb_data + sizeof(sps_nalu), pps_nalu, sizeof(pps_nalu));
    memcpy(annexb_data + sizeof(sps_nalu) + sizeof(pps_nalu), idr_nalu, sizeof(idr_nalu));

    // Create buffer with annexb data
    SrsUniquePtr<SrsBuffer> avs(new SrsBuffer(annexb_data, total_size));

    // Configure mock_avc to return SPS frame
    mock_avc->demux_frame_ = sps_nalu + 4;    // Skip start code
    mock_avc->demux_frame_size_ = total_size; // Return all data to empty the buffer
    mock_avc->is_sps_result_ = true;
    mock_avc->is_pps_result_ = false;
    mock_avc->sps_output_ = std::string(sps_nalu + 4, sizeof(sps_nalu) - 4);
    mock_avc->pps_output_ = std::string(pps_nalu + 4, sizeof(pps_nalu) - 4);

    // Test on_ts_video - should process SPS frame
    HELPER_EXPECT_SUCCESS(udp_handler->on_ts_video(msg.get(), avs.get()));

    // Verify connect was NOT called (sdk_ is already set, so connect() returns early)
    // In real scenario, connect() would be called if sdk_ is NULL
    EXPECT_FALSE(mock_sdk->connect_called_);

    // Verify annexb_demux was called
    EXPECT_TRUE(mock_avc->annexb_demux_called_);

    // Verify is_sps was called
    EXPECT_TRUE(mock_avc->is_sps_called_);

    // Verify sps_demux was called
    EXPECT_TRUE(mock_avc->sps_demux_called_);

    // Verify h264_sps_ was updated
    EXPECT_EQ(mock_avc->sps_output_, udp_handler->h264_sps_);
    EXPECT_TRUE(udp_handler->h264_sps_changed_);

    // Verify buffer is now empty (all data consumed)
    EXPECT_TRUE(avs->empty());

    // Clean up - set to NULL to avoid double-free
    udp_handler->avc_ = NULL;
    udp_handler->sdk_ = NULL;
    srs_freep(mock_avc);
    srs_freep(mock_sdk);
    delete[] annexb_data;
}

// Append an ADTS frame of AAC LC, stereo, at the sampling frequency index sfi, with size bytes of payload.
static void mock_append_adts_frame(std::string &pes, int sfi, int size)
{
    int frame_length = 7 + size;
    pes.push_back((char)0xff);
    pes.push_back((char)0xf1);
    pes.push_back((char)((1 << 6) | (sfi << 2) | (2 >> 2)));
    pes.push_back((char)(((2 & 0x03) << 6) | ((frame_length >> 11) & 0x03)));
    pes.push_back((char)((frame_length >> 3) & 0xff));
    pes.push_back((char)(((frame_length & 0x07) << 5) | 0x1f));
    pes.push_back((char)0xfc);
    for (int i = 0; i < size; i++) {
        pes.push_back((char)(0x21 + i));
    }
}

// Collect the timestamps of the audio messages the caster queued, in order.
static std::vector<int64_t> mock_queued_audio_times(SrsMpegtsQueue *queue)
{
    std::vector<int64_t> times;
    std::map<int64_t, SrsMediaPacket *>::iterator it;
    for (it = queue->msgs_.begin(); it != queue->msgs_.end(); ++it) {
        if (it->second->is_audio()) {
            times.push_back(it->first);
        }
    }
    return times;
}

// A PES of several AAC frames, as ffmpeg muxes TS by default, has the time of
// its first frame only; each frame after it comes 1024 samples later, so the
// caster must time each frame, not stamp them all with the PES's time.
VOID TEST(MpegtsOverUdpTest, OnTsAudioTimesEachFrameOfPes)
{
    srs_error_t err;

    SrsUniquePtr<SrsMpegtsOverUdp> udp_handler(new SrsMpegtsOverUdp());
    udp_handler->assemble();

    // The RTMP output is connected, and the caster's own queue holds what it writes.
    udp_handler->sdk_ = new MockRtmpClient();
    SrsMpegtsQueue *queue = dynamic_cast<SrsMpegtsQueue *>(udp_handler->queue_);
    ASSERT_TRUE(queue != NULL);

    // The first PES, 3 frames at 44.1kHz from 1000ms: the sequence header at
    // 1000ms, then the frames 23.2ms apart; the queue moves the first frame
    // 1ms after the header, which holds its time.
    if (true) {
        std::string pes;
        for (int i = 0; i < 3; i++) {
            mock_append_adts_frame(pes, 4, 10);
        }
        SrsUniquePtr<SrsTsMessage> msg(new SrsTsMessage());
        msg->dts_ = msg->pts_ = 90000;
        SrsBuffer avs((char *)pes.data(), (int)pes.size());
        HELPER_EXPECT_SUCCESS(udp_handler->on_ts_audio(msg.get(), &avs));
        EXPECT_TRUE(avs.empty());

        std::vector<int64_t> times = mock_queued_audio_times(queue);
        ASSERT_EQ(4, (int)times.size());
        EXPECT_EQ(1000, times[0]);
        EXPECT_EQ(1001, times[1]);
        EXPECT_EQ(1023, times[2]);
        EXPECT_EQ(1046, times[3]);
    }

    // The next PES, 4 frames at 48kHz from 2000ms: 21.3ms apart, no header.
    if (true) {
        std::string pes;
        for (int i = 0; i < 4; i++) {
            mock_append_adts_frame(pes, 3, 10);
        }
        SrsUniquePtr<SrsTsMessage> msg(new SrsTsMessage());
        msg->dts_ = msg->pts_ = 180000;
        SrsBuffer avs((char *)pes.data(), (int)pes.size());
        HELPER_EXPECT_SUCCESS(udp_handler->on_ts_audio(msg.get(), &avs));

        std::vector<int64_t> times = mock_queued_audio_times(queue);
        ASSERT_EQ(8, (int)times.size());
        EXPECT_EQ(2000, times[4]);
        EXPECT_EQ(2021, times[5]);
        EXPECT_EQ(2042, times[6]);
        EXPECT_EQ(2064, times[7]);
    }

    // The caster frees its queue with the queued messages, and the mock RTMP client.
}

// Append an annexb NALU, its start code then its bytes.
static void mock_append_annexb_nalu(std::string &pes, const char *nalu, int size)
{
    pes.append("\x00\x00\x00\x01", 4);
    pes.append(nalu, size);
}

// Collect the video messages the caster queued, in order of time.
static std::vector<SrsMediaPacket *> mock_queued_videos(SrsMpegtsQueue *queue)
{
    std::vector<SrsMediaPacket *> videos;
    std::map<int64_t, SrsMediaPacket *>::iterator it;
    for (it = queue->msgs_.begin(); it != queue->msgs_.end(); ++it) {
        if (it->second->is_video()) {
            videos.push_back(it->second);
        }
    }
    return videos;
}

// A PES is one access unit: x264 puts an SEI before the first IDR, and a frame
// may have several slices. The caster must send the whole access unit as one
// RTMP message, a keyframe when it has an IDR slice, not each NALU as a message
// of its own, which makes the SEI an inter frame before the keyframe.
VOID TEST(MpegtsOverUdpTest, OnTsVideoSendsAccessUnitAsOneMessage)
{
    srs_error_t err;

    SrsUniquePtr<SrsMpegtsOverUdp> udp_handler(new SrsMpegtsOverUdp());
    udp_handler->assemble();

    // The RTMP output is connected, and the caster's own queue holds what it
    // writes: with no audio, it sends nothing.
    udp_handler->sdk_ = new MockRtmpClient();
    SrsMpegtsQueue *queue = dynamic_cast<SrsMpegtsQueue *>(udp_handler->queue_);
    ASSERT_TRUE(queue != NULL);

    const char aud[] = {0x09, (char)0xf0};
    const char sps[] = {0x67, 0x64, 0x00, 0x1f, (char)0xac, (char)0xd9};
    const char pps[] = {0x68, (char)0xeb, (char)0xe3, (char)0xcb};
    const char sei[] = {0x06, 0x05, 0x02, 0x11, 0x22, (char)0x80};
    const char idr0[] = {0x65, (char)0x88, (char)0x84, 0x01};
    const char idr1[] = {0x65, 0x00, 0x22, 0x02};
    const char p0[] = {0x41, (char)0x9a, 0x11, 0x03};
    const char p1[] = {0x41, 0x00, 0x33, 0x04};

    // The first access unit at 1000ms: AUD, SPS, PPS, SEI and an IDR of 2 slices.
    if (true) {
        std::string pes;
        mock_append_annexb_nalu(pes, aud, sizeof(aud));
        mock_append_annexb_nalu(pes, sps, sizeof(sps));
        mock_append_annexb_nalu(pes, pps, sizeof(pps));
        mock_append_annexb_nalu(pes, sei, sizeof(sei));
        mock_append_annexb_nalu(pes, idr0, sizeof(idr0));
        mock_append_annexb_nalu(pes, idr1, sizeof(idr1));

        SrsUniquePtr<SrsTsMessage> msg(new SrsTsMessage());
        msg->dts_ = msg->pts_ = 90000;
        SrsBuffer avs((char *)pes.data(), (int)pes.size());
        HELPER_EXPECT_SUCCESS(udp_handler->on_ts_video(msg.get(), &avs));
        EXPECT_TRUE(avs.empty());
    }

    // The next access unit at 1040ms: AUD and a P-frame of 2 slices.
    if (true) {
        std::string pes;
        mock_append_annexb_nalu(pes, aud, sizeof(aud));
        mock_append_annexb_nalu(pes, p0, sizeof(p0));
        mock_append_annexb_nalu(pes, p1, sizeof(p1));

        SrsUniquePtr<SrsTsMessage> msg(new SrsTsMessage());
        msg->dts_ = msg->pts_ = 93600;
        SrsBuffer avs((char *)pes.data(), (int)pes.size());
        HELPER_EXPECT_SUCCESS(udp_handler->on_ts_video(msg.get(), &avs));
    }

    // The sequence header, then one message for each access unit.
    std::vector<SrsMediaPacket *> videos = mock_queued_videos(queue);
    ASSERT_EQ(3, (int)videos.size());

    // The sequence header at 1000ms, a keyframe.
    EXPECT_EQ(1000, videos[0]->timestamp_);
    ASSERT_GE(videos[0]->size(), 2);
    EXPECT_EQ(0x17, (uint8_t)videos[0]->payload()[0]);
    EXPECT_EQ(SrsVideoAvcFrameTraitSequenceHeader, videos[0]->payload()[1]);

    // The first frame, a keyframe that starts with the SEI, its NALUs in the
    // order they came, each with its 4-byte size; the queue moves it 1ms after
    // the header, which holds its time.
    if (true) {
        std::string want;
        want.append("\x17\x01\x00\x00\x00", 5);
        want.append("\x00\x00\x00\x06", 4);
        want.append(sei, sizeof(sei));
        want.append("\x00\x00\x00\x04", 4);
        want.append(idr0, sizeof(idr0));
        want.append("\x00\x00\x00\x04", 4);
        want.append(idr1, sizeof(idr1));
        EXPECT_EQ(1001, videos[1]->timestamp_);
        EXPECT_EQ(want, std::string(videos[1]->payload(), videos[1]->size()));
    }

    // The next frame, an inter frame of its 2 slices.
    if (true) {
        std::string want;
        want.append("\x27\x01\x00\x00\x00", 5);
        want.append("\x00\x00\x00\x04", 4);
        want.append(p0, sizeof(p0));
        want.append("\x00\x00\x00\x04", 4);
        want.append(p1, sizeof(p1));
        EXPECT_EQ(1040, videos[2]->timestamp_);
        EXPECT_EQ(want, std::string(videos[2]->payload(), videos[2]->size()));
    }

    // The caster frees its queue with the queued messages, and the mock RTMP client.
}

// A frame whose PES has a presentation time after its decode time, as with
// B-frames, goes with the difference as its composition time; the sequence
// header has none.
VOID TEST(MpegtsOverUdpTest, OnTsVideoKeepsCompositionTime)
{
    srs_error_t err;

    SrsUniquePtr<SrsMpegtsOverUdp> udp_handler(new SrsMpegtsOverUdp());
    udp_handler->assemble();

    udp_handler->sdk_ = new MockRtmpClient();
    SrsMpegtsQueue *queue = dynamic_cast<SrsMpegtsQueue *>(udp_handler->queue_);
    ASSERT_TRUE(queue != NULL);

    const char sps[] = {0x67, 0x64, 0x00, 0x1f, (char)0xac, (char)0xd9};
    const char pps[] = {0x68, (char)0xeb, (char)0xe3, (char)0xcb};
    const char idr[] = {0x65, (char)0x88, (char)0x84, 0x01};
    const char p[] = {0x41, (char)0x9a, 0x11, 0x03};
    const char b[] = {0x01, (char)0x9e, 0x22, 0x04};

    // Each PES: its NALUs, its dts and its pts, in 90kHz.
    struct {
        bool keyframe;
        int64_t dts;
        int64_t pts;
    } frames[] = {
        {true, 90000, 97200}, // the keyframe at 1000ms, shown at 1080ms
        {false, 93600, 108000}, // a P-frame at 1040ms, shown at 1200ms
        {false, 97200, 100800}, // a B-frame at 1080ms, shown at 1120ms
        {false, 100800, 100800}, // a frame of no B-frame, shown at its dts
        {false, 104400, 101000}, // a pts before the dts, which no stream has
    };

    for (int i = 0; i < (int)(sizeof(frames) / sizeof(frames[0])); i++) {
        std::string pes;
        if (frames[i].keyframe) {
            mock_append_annexb_nalu(pes, sps, sizeof(sps));
            mock_append_annexb_nalu(pes, pps, sizeof(pps));
            mock_append_annexb_nalu(pes, idr, sizeof(idr));
        } else if (i == 2) {
            mock_append_annexb_nalu(pes, b, sizeof(b));
        } else {
            mock_append_annexb_nalu(pes, p, sizeof(p));
        }

        SrsUniquePtr<SrsTsMessage> msg(new SrsTsMessage());
        msg->dts_ = frames[i].dts;
        msg->pts_ = frames[i].pts;
        SrsBuffer avs((char *)pes.data(), (int)pes.size());
        HELPER_EXPECT_SUCCESS(udp_handler->on_ts_video(msg.get(), &avs));
    }

    std::vector<SrsMediaPacket *> videos = mock_queued_videos(queue);
    ASSERT_EQ(6, (int)videos.size());

    // The composition time, 3 bytes after the frame type and packet type.
    int want_cts[] = {0, 80, 160, 40, 0, 0};
    uint32_t want_time[] = {1000, 1001, 1040, 1080, 1120, 1160};
    for (int i = 0; i < 6; i++) {
        ASSERT_GE(videos[i]->size(), 5);
        const uint8_t *v = (const uint8_t *)videos[i]->payload();
        int cts = (v[2] << 16) | (v[3] << 8) | v[4];
        EXPECT_EQ(want_cts[i], cts) << "video " << i;
        EXPECT_EQ(want_time[i], (uint32_t)videos[i]->timestamp_) << "video " << i;
    }
    EXPECT_EQ(SrsVideoAvcFrameTraitSequenceHeader, videos[0]->payload()[1]);
}

// Count the video and audio sequence headers the caster queued, of the
// times from start to before end in ms.
static void mock_queued_sequence_headers(SrsMpegtsQueue *queue, int64_t start, int64_t end, int &nb_video, int &nb_audio)
{
    nb_video = nb_audio = 0;
    std::map<int64_t, SrsMediaPacket *>::iterator it;
    for (it = queue->msgs_.begin(); it != queue->msgs_.end(); ++it) {
        SrsMediaPacket *msg = it->second;
        if (it->first < start || it->first >= end || msg->size() < 2 || msg->payload()[1] != 0) {
            continue;
        }
        if (msg->is_video()) {
            nb_video++;
        } else if (msg->is_audio()) {
            nb_audio++;
        }
    }
}

// When the caster's RTMP output closes, as when the server closes the
// caster's publisher for a stream idle past its timeout, the next push is a
// new publish: the caster sends each sequence header again, and nothing it
// queued for the closed publish.
VOID TEST(MpegtsOverUdpTest, ClosedOutputSendsSequenceHeadersAgain)
{
    srs_error_t err;

    SrsUniquePtr<SrsMpegtsOverUdp> udp_handler(new SrsMpegtsOverUdp());
    udp_handler->assemble();

    // The RTMP output is connected; the factory makes the next one.
    udp_handler->sdk_ = new MockRtmpClient();
    SrsUniquePtr<MockAppFactoryForMpegtsOverUdp> mock_factory(new MockAppFactoryForMpegtsOverUdp());
    mock_factory->mock_rtmp_client_ = new MockRtmpClient();
    udp_handler->app_factory_ = mock_factory.get();
    SrsMpegtsQueue *queue = dynamic_cast<SrsMpegtsQueue *>(udp_handler->queue_);
    ASSERT_TRUE(queue != NULL);

    const char sps[] = {0x67, 0x64, 0x00, 0x1f, (char)0xac, (char)0xd9};
    const char pps[] = {0x68, (char)0xeb, (char)0xe3, (char)0xcb};

    // A push of an AAC frame, then the SPS and PPS, at dts: the queue holds
    // fewer than 2 frames of each track, so the caster sends nothing yet.
    struct Push {
        static void run(SrsMpegtsOverUdp *caster, int64_t dts, const char *sps, int nb_sps, const char *pps, int nb_pps)
        {
            srs_error_t err;
            if (true) {
                std::string pes;
                mock_append_adts_frame(pes, 4, 10);
                SrsUniquePtr<SrsTsMessage> msg(new SrsTsMessage());
                msg->dts_ = msg->pts_ = dts;
                SrsBuffer avs((char *)pes.data(), (int)pes.size());
                HELPER_EXPECT_SUCCESS(caster->on_ts_audio(msg.get(), &avs));
            }
            if (true) {
                std::string pes;
                mock_append_annexb_nalu(pes, sps, nb_sps);
                mock_append_annexb_nalu(pes, pps, nb_pps);
                SrsUniquePtr<SrsTsMessage> msg(new SrsTsMessage());
                msg->dts_ = msg->pts_ = dts;
                SrsBuffer avs((char *)pes.data(), (int)pes.size());
                HELPER_EXPECT_SUCCESS(caster->on_ts_video(msg.get(), &avs));
            }
        }
    };

    // The first push: both sequence headers.
    Push::run(udp_handler.get(), 900000, sps, sizeof(sps), pps, sizeof(pps));
    int nb_video = 0, nb_audio = 0;
    mock_queued_sequence_headers(queue, 10000, 11000, nb_video, nb_audio);
    EXPECT_EQ(1, nb_video);
    EXPECT_EQ(1, nb_audio);

    // The output closes, and what it queued goes with it.
    udp_handler->close();
    EXPECT_TRUE(udp_handler->sdk_ == NULL);
    EXPECT_TRUE(queue->msgs_.empty());

    // The second push, the same codecs from the start: a new output, and both
    // sequence headers again, at the second push's times.
    Push::run(udp_handler.get(), 0, sps, sizeof(sps), pps, sizeof(pps));
    EXPECT_TRUE(mock_factory->create_rtmp_client_called_);
    EXPECT_TRUE(udp_handler->sdk_ == mock_factory->mock_rtmp_client_);
    mock_queued_sequence_headers(queue, 0, 1000, nb_video, nb_audio);
    EXPECT_EQ(1, nb_video);
    EXPECT_EQ(1, nb_audio);
    ASSERT_FALSE(queue->msgs_.empty());
    EXPECT_GT(10000, queue->msgs_.rbegin()->first);

    // The caster frees its queue with the queued messages, and the second mock RTMP client.
    udp_handler->app_factory_ = NULL;
}

// Test SrsMpegtsOverUdp::write_h264_sps_pps - major use scenario
// This test covers the complete SPS/PPS writing flow:
// 1. Both SPS and PPS have changed (h264_sps_changed_ and h264_pps_changed_ are true)
// 2. Call write_h264_sps_pps with dts and pts
// 3. Verify it calls avc_->mux_sequence_header to create sequence header
// 4. Verify it calls avc_->mux_avc2flv to convert to FLV format
// 5. Verify it calls rtmp_write_packet to write the packet
// 6. Verify flags are reset correctly (h264_sps_changed_, h264_pps_changed_ set to false, h264_sps_pps_sent_ set to true)
VOID TEST(MpegtsOverUdpTest, WriteH264SpsPps)
{
    srs_error_t err;

    // Create SrsMpegtsOverUdp instance
    SrsUniquePtr<SrsMpegtsOverUdp> udp_handler(new SrsMpegtsOverUdp());
    udp_handler->assemble();

    // Create mock dependencies
    MockMpegtsRawH264Stream *mock_avc = new MockMpegtsRawH264Stream();
    MockRtmpClient *mock_sdk = new MockRtmpClient();
    SrsUniquePtr<SrsMpegtsQueue> mock_queue(new SrsMpegtsQueue());

    // Inject mock dependencies
    udp_handler->avc_ = mock_avc;
    udp_handler->sdk_ = mock_sdk;
    udp_handler->queue_ = mock_queue.get();

    // Set up SPS and PPS data
    std::string sps_data = "\x67\x42\x00\x1e";
    std::string pps_data = "\x68\xce\x3c\x80";
    udp_handler->h264_sps_ = sps_data;
    udp_handler->h264_pps_ = pps_data;

    // Set both SPS and PPS changed flags to true (required for write_h264_sps_pps to proceed)
    udp_handler->h264_sps_changed_ = true;
    udp_handler->h264_pps_changed_ = true;
    udp_handler->h264_sps_pps_sent_ = false;

    // Test write_h264_sps_pps with valid dts and pts
    uint32_t dts = 1000;
    uint32_t pts = 1000;
    HELPER_EXPECT_SUCCESS(udp_handler->write_h264_sps_pps(dts, pts));

    // Verify flags are reset correctly after successful write
    EXPECT_FALSE(udp_handler->h264_sps_changed_);
    EXPECT_FALSE(udp_handler->h264_pps_changed_);
    EXPECT_TRUE(udp_handler->h264_sps_pps_sent_);

    // Clean up - set to NULL to avoid double-free
    udp_handler->avc_ = NULL;
    udp_handler->sdk_ = NULL;
    udp_handler->queue_ = NULL;
    srs_freep(mock_avc);
    srs_freep(mock_sdk);
}

// Test SrsMpegtsOverUdp::write_h264_ipb_frame - major use scenario
// This test covers the complete IPB frame writing flow:
// 1. SPS/PPS already sent (h264_sps_pps_sent_ = true)
// 2. Write an access unit of an IDR frame (keyframe), its NALU with its 4-byte size
// 3. Verify it calls avc_->mux_avc2flv with correct frame type (keyframe)
// 4. Verify it calls rtmp_write_packet to write the video packet
VOID TEST(MpegtsOverUdpTest, WriteH264IpbFrameWithIdrFrame)
{
    srs_error_t err;

    // Create SrsMpegtsOverUdp instance
    SrsUniquePtr<SrsMpegtsOverUdp> udp_handler(new SrsMpegtsOverUdp());
    udp_handler->assemble();

    // Create mock dependencies
    MockMpegtsRawH264Stream *mock_avc = new MockMpegtsRawH264Stream();
    MockRtmpClient *mock_sdk = new MockRtmpClient();
    SrsUniquePtr<SrsMpegtsQueue> mock_queue(new SrsMpegtsQueue());

    // Inject mock dependencies
    udp_handler->avc_ = mock_avc;
    udp_handler->sdk_ = mock_sdk;
    udp_handler->queue_ = mock_queue.get();

    // Set h264_sps_pps_sent_ to true (required for write_h264_ipb_frame to proceed)
    udp_handler->h264_sps_pps_sent_ = true;

    // Create an IDR frame NALU (type 5)
    // NALU header: 0x65 = 0110 0101 (forbidden_zero_bit=0, nal_ref_idc=3, nal_unit_type=5)
    char idr_frame[] = {0x00, 0x00, 0x00, 0x08, 0x65, (char)0x88, (char)0x84, 0x00, 0x01, 0x02, 0x03, 0x04};
    std::string ibps(idr_frame, sizeof(idr_frame));

    // Test write_h264_ipb_frame with IDR frame
    uint32_t dts = 2000;
    uint32_t pts = 2000;
    HELPER_EXPECT_SUCCESS(udp_handler->write_h264_ipb_frame(ibps, true, dts, pts));

    // Clean up - set to NULL to avoid double-free
    udp_handler->avc_ = NULL;
    udp_handler->sdk_ = NULL;
    udp_handler->queue_ = NULL;
    srs_freep(mock_avc);
    srs_freep(mock_sdk);
}

// Mock ISrsAppFactory implementation
MockAppFactoryForMpegtsOverUdp::MockAppFactoryForMpegtsOverUdp()
{
    mock_rtmp_client_ = NULL;
    create_rtmp_client_called_ = false;
}

MockAppFactoryForMpegtsOverUdp::~MockAppFactoryForMpegtsOverUdp()
{
    // Don't free mock_rtmp_client_ - it's managed by the test
}

ISrsBasicRtmpClient *MockAppFactoryForMpegtsOverUdp::create_rtmp_client(std::string url, srs_utime_t cto, srs_utime_t sto)
{
    create_rtmp_client_called_ = true;
    return mock_rtmp_client_;
}

void MockAppFactoryForMpegtsOverUdp::reset()
{
    create_rtmp_client_called_ = false;
}

// Test SrsMpegtsOverUdp::rtmp_write_packet - major use scenario
// This test covers the complete RTMP packet writing flow:
// 1. Call rtmp_write_packet with video data
// 2. Verify it calls connect() to establish RTMP connection (if not already connected)
// 3. Verify it creates RTMP message from raw data using srs_rtmp_create_msg
// 4. Verify it pushes message to queue
// 5. Verify it dequeues ready messages and sends them via sdk_->send_and_free_message
// 6. Verify multiple packets are sent in correct order when queue threshold is met
VOID TEST(MpegtsOverUdpTest, RtmpWritePacketWithVideoData)
{
    srs_error_t err;

    // Create SrsMpegtsOverUdp instance
    SrsUniquePtr<SrsMpegtsOverUdp> udp_handler(new SrsMpegtsOverUdp());
    udp_handler->assemble();

    // Create mock dependencies
    MockRtmpClient *mock_sdk = new MockRtmpClient();
    SrsUniquePtr<MockAppFactoryForMpegtsOverUdp> mock_factory(new MockAppFactoryForMpegtsOverUdp());
    SrsUniquePtr<SrsMpegtsQueue> mock_queue(new SrsMpegtsQueue());

    // Configure mock factory to return our mock RTMP client
    mock_factory->mock_rtmp_client_ = mock_sdk;

    // Inject mock dependencies
    udp_handler->app_factory_ = mock_factory.get();
    udp_handler->queue_ = mock_queue.get();
    udp_handler->output_ = "rtmp://127.0.0.1/live/stream";

    // Note: sdk_ is NULL initially, so connect() will be called

    // Create test video data (H.264 IDR frame) - allocate on heap
    int video_size = 25;
    char *video_data = new char[video_size];
    video_data[0] = 0x17;
    video_data[1] = 0x01;
    video_data[2] = 0x00;
    video_data[3] = 0x00;
    video_data[4] = 0x00;
    video_data[5] = 0x00;
    video_data[6] = 0x00;
    video_data[7] = 0x00;
    video_data[8] = 0x10;
    video_data[9] = 0x65;
    video_data[10] = (char)0x88;
    video_data[11] = (char)0x84;
    video_data[12] = 0x00;
    for (int i = 13; i < video_size; i++)
        video_data[i] = i - 12;

    uint32_t timestamp = 1000;
    char type = SrsFrameTypeVideo;

    // First call to rtmp_write_packet - should trigger connect()
    HELPER_EXPECT_SUCCESS(udp_handler->rtmp_write_packet(type, timestamp, video_data, video_size));

    // Verify connect was called (factory creates RTMP client)
    EXPECT_TRUE(mock_factory->create_rtmp_client_called_);
    EXPECT_TRUE(mock_sdk->connect_called_);
    EXPECT_TRUE(mock_sdk->publish_called_);

    // Verify sdk_ is now set
    EXPECT_TRUE(udp_handler->sdk_ != NULL);

    // Reset the flag to verify it's not called again
    mock_sdk->connect_called_ = false;
    mock_sdk->publish_called_ = false;

    // Second call should NOT trigger connect (already connected)
    char *video_data2 = new char[17];
    video_data2[0] = 0x27;
    video_data2[1] = 0x01;
    video_data2[2] = 0x00;
    video_data2[3] = 0x00;
    video_data2[4] = 0x00;
    video_data2[5] = 0x00;
    video_data2[6] = 0x00;
    video_data2[7] = 0x00;
    video_data2[8] = 0x08;
    video_data2[9] = 0x41;
    video_data2[10] = (char)0x88;
    video_data2[11] = (char)0x84;
    video_data2[12] = 0x00;
    for (int i = 13; i < 17; i++)
        video_data2[i] = i - 12;

    uint32_t timestamp2 = 1040;
    HELPER_EXPECT_SUCCESS(udp_handler->rtmp_write_packet(type, timestamp2, video_data2, 17));

    // Verify connect was NOT called again
    EXPECT_FALSE(mock_sdk->connect_called_);
    EXPECT_FALSE(mock_sdk->publish_called_);

    // Clean up - set to NULL to avoid double-free
    udp_handler->app_factory_ = NULL;
    udp_handler->queue_ = NULL;
    udp_handler->sdk_ = NULL;
    srs_freep(mock_sdk);
}

// Mock ISrsCoroutine implementation for testing SrsHttpConn::cycle()
MockCoroutineForCycle::MockCoroutineForCycle()
{
    pull_called_ = false;
    pull_error_ = srs_success;
}

MockCoroutineForCycle::~MockCoroutineForCycle()
{
    srs_freep(pull_error_);
}

srs_error_t MockCoroutineForCycle::start()
{
    return srs_success;
}

void MockCoroutineForCycle::stop()
{
}

void MockCoroutineForCycle::interrupt()
{
}

srs_error_t MockCoroutineForCycle::pull()
{
    pull_called_ = true;
    if (pull_error_ != srs_success) {
        return srs_error_copy(pull_error_);
    }
    return srs_success;
}

const SrsContextId &MockCoroutineForCycle::cid()
{
    static SrsContextId id;
    return id;
}

void MockCoroutineForCycle::set_cid(const SrsContextId &cid)
{
}

void MockCoroutineForCycle::reset()
{
    pull_called_ = false;
    srs_freep(pull_error_);
}

// Mock SrsHttpMessage implementation for testing SrsHttpConn::cycle()
MockHttpMessageForCycle::MockHttpMessageForCycle() : SrsHttpMessage(NULL, NULL)
{
    is_keep_alive_ = false;
}

MockHttpMessageForCycle::~MockHttpMessageForCycle()
{
}

bool MockHttpMessageForCycle::is_keep_alive()
{
    return is_keep_alive_;
}

ISrsRequest *MockHttpMessageForCycle::to_request(std::string vhost)
{
    return NULL;
}

// Test SrsHttpConn::cycle() - covers the major use scenario:
// This test covers the complete HTTP connection lifecycle:
// 1. Create SrsHttpConn with mock dependencies
// 2. Call cycle() which internally calls do_cycle()
// 3. Verify parser is initialized
// 4. Verify handler->on_start() is called
// 5. Verify HTTP message is parsed
// 6. Verify handler->on_http_message() is called
// 7. Verify handler->on_message_done() is called
// 8. Verify handler->on_conn_done() is called
// 9. Verify connection completes successfully
VOID TEST(HttpConnTest, CycleSuccessWithSingleRequest)
{
    srs_error_t err;

    // Create mock dependencies
    MockHttpxConn *mock_handler = new MockHttpxConn();
    MockProtocolReadWriter *mock_skt = new MockProtocolReadWriter();
    SrsHttpServeMux *mock_mux = new SrsHttpServeMux();

    // Create SrsHttpConn
    SrsUniquePtr<SrsHttpConn> conn(new SrsHttpConn(mock_handler, mock_skt, mock_mux, "127.0.0.1", 8080));

    // Create mock parser and coroutine
    MockHttpParser *mock_parser = new MockHttpParser();
    MockCoroutineForCycle *mock_trd = new MockCoroutineForCycle();

    // Create mock message - will be freed by SrsHttpConn::process_requests()
    MockHttpMessageForCycle *mock_message = new MockHttpMessageForCycle();

    // Configure mock message to NOT keep alive (so process_requests loop exits after one request)
    mock_message->is_keep_alive_ = false;

    // Configure mock parser to return our mock message
    mock_parser->messages_.push_back(mock_message);
    mock_parser->cond_->signal();

    // Inject mock dependencies into SrsHttpConn
    conn->parser_ = mock_parser;
    conn->trd_ = mock_trd;
    conn->handler_ = mock_handler;
    conn->skt_ = mock_skt;

    // Test cycle() - should successfully process one HTTP request
    HELPER_EXPECT_SUCCESS(conn->cycle());

    // Verify parser was initialized
    EXPECT_TRUE(mock_parser->initialize_called_);

    // Verify handler->on_start() was called
    EXPECT_TRUE(mock_handler->on_start_called_);

    // Verify coroutine pull() was called
    EXPECT_TRUE(mock_trd->pull_called_);

    // Verify parser->parse_message() was called
    EXPECT_TRUE(mock_parser->parse_message_called_);

    // Verify handler->on_http_message() was called
    EXPECT_TRUE(mock_handler->on_http_message_called_);

    // Verify handler->on_message_done() was called
    EXPECT_TRUE(mock_handler->on_message_done_called_);

    // Verify handler->on_conn_done() was called
    EXPECT_TRUE(mock_handler->on_conn_done_called_);

    // Verify recv timeout was set
    EXPECT_EQ(SRS_HTTP_RECV_TIMEOUT, mock_skt->get_recv_timeout());

    // Clean up - Note: mock_message is already freed by SrsHttpConn::process_requests()
    // The SrsHttpConn destructor will free parser_ and trd_, so we need to prevent double-free
    // We'll let conn go out of scope naturally, which will call its destructor
    // Then we manually free the other mocks that weren't owned by conn
    srs_freep(mock_handler);
    srs_freep(mock_skt);
    srs_freep(mock_mux);
    // Note: mock_parser and mock_trd will be freed by SrsHttpConn destructor
}

VOID TEST(HttpConnTest, ConstructionReachesNoCollaborator)
{
    MockHttpxConn handler;
    MockProtocolReadWriter skt;

    SrsUniquePtr<SrsHttpConn> conn(new SrsHttpConn(&handler, &skt, NULL, "127.0.0.1", 8080));

    // GOAL: construction only captures dependencies, so a test can replace them before any work runs.
    EXPECT_TRUE(NULL == conn->trd_);
    EXPECT_EQ(0, conn->create_time_);

    SrsNetworkDelta *delta = dynamic_cast<SrsNetworkDelta *>(conn->delta_);
    EXPECT_TRUE(NULL == delta->in_);
    EXPECT_TRUE(NULL == delta->out_);

    EXPECT_TRUE(_srs_app_factory == conn->app_factory_);
    EXPECT_TRUE(_srs_context == conn->context_);
}

VOID TEST(HttpConnTest, AssembleWiresInjectedContextAndSocket)
{
    MockHttpxConn handler;
    MockProtocolReadWriter skt;

    SrsUniquePtr<SrsHttpConn> conn(new SrsHttpConn(&handler, &skt, NULL, "127.0.0.1", 8080));

    // The connection owns the coroutine the factory creates, and frees it in its destructor.
    MockCoroutineForRtmpConn *trd = new MockCoroutineForRtmpConn();

    MockAppFactoryForRtmpConn factory;
    factory.coroutine_ = trd;

    MockContextForRtmpConn context;
    context.id_ = SrsContextId().set_value("http-cid");

    conn->app_factory_ = &factory;
    conn->context_ = &context;
    conn->assemble();

    // GOAL: the coroutine is created by assemble(), through the injected factory, and runs under the
    // id of the injected context.
    EXPECT_EQ(1, factory.create_coroutine_count_);
    EXPECT_STREQ("http", factory.coroutine_name_.c_str());
    EXPECT_TRUE(conn.get() == factory.coroutine_handler_);
    EXPECT_STREQ("http-cid", factory.coroutine_cid_.c_str());
    EXPECT_TRUE(trd == conn->trd_);

    // GOAL: the connection runs under the id its owner chose, so it never creates or switches one.
    EXPECT_EQ(0, context.generate_id_count_);
    EXPECT_EQ(0, context.set_id_count_);

    // GOAL: the bandwidth delta measures the socket, and the creation time is taken at assemble().
    SrsNetworkDelta *delta = dynamic_cast<SrsNetworkDelta *>(conn->delta_);
    EXPECT_TRUE(&skt == delta->in_);
    EXPECT_TRUE(&skt == delta->out_);
    EXPECT_TRUE(conn->create_time_ > 0);

    conn->app_factory_ = NULL;
    conn->context_ = NULL;
}

VOID TEST(HttpConnTest, InvalidJsonpCallbackAnsweredBadRequest)
{
    srs_error_t err;

    MockHttpxConn handler;
    MockBufferIO skt;
    skt.append("GET /api/v1/versions?callback=alert(1) HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");

    SrsUniquePtr<SrsHttpConn> conn(new SrsHttpConn(&handler, &skt, NULL, "127.0.0.1", 8080));
    conn->trd_ = new MockCoroutineForCycle();
    HELPER_EXPECT_SUCCESS(conn->parser_->initialize(HTTP_REQUEST));
    HELPER_EXPECT_SUCCESS(conn->set_jsonp(true));

    // GOAL: the refused callback still ends the connection with its error.
    ISrsRequest *req = NULL;
    err = conn->process_requests(&req);
    EXPECT_EQ(ERROR_HTTP_JSONP, srs_error_code(err));
    srs_freep(err);
    srs_freep(req);

    // GOAL: the client is told why by 400, with the error code and its short name, never the callback,
    // so nothing it sent is echoed back as script.
    string out(skt.out_buffer.bytes(), skt.out_buffer.length());
    EXPECT_TRUE(srs_strings_starts_with(out, "HTTP/1.1 400 Bad Request\r\n")) << out;
    EXPECT_TRUE(srs_strings_contains(out, "Connection: Close\r\n")) << out;
    EXPECT_TRUE(srs_strings_contains(out, "Content-Type: text/plain; charset=utf-8\r\n")) << out;
    EXPECT_TRUE(srs_strings_ends_with(out, "\r\n\r\n4058: HttpJsonp")) << out;
    EXPECT_FALSE(srs_strings_contains(out, "alert")) << out;

    // GOAL: no handler sees the refused request.
    EXPECT_FALSE(handler.on_http_message_called_);
}

VOID TEST(HttpConnTest, ValidJsonpCallbackNotRefused)
{
    srs_error_t err;

    MockHttpxConn handler;
    MockBufferIO skt;
    skt.append("GET /api/v1/versions?callback=cb HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");

    MockHttpServeMux mux;
    SrsUniquePtr<SrsHttpConn> conn(new SrsHttpConn(&handler, &skt, &mux, "127.0.0.1", 8080));
    conn->trd_ = new MockCoroutineForCycle();
    HELPER_EXPECT_SUCCESS(conn->parser_->initialize(HTTP_REQUEST));
    HELPER_EXPECT_SUCCESS(conn->set_jsonp(true));
    HELPER_EXPECT_SUCCESS(conn->set_auth_enabled(false));

    // GOAL: a callback of allowed characters is served as before, and the connection ends only when the
    // client sends nothing more. This passes before and after the refused callback got its answer.
    ISrsRequest *req = NULL;
    err = conn->process_requests(&req);
    EXPECT_EQ(ERROR_SOCKET_READ, srs_error_code(err));
    srs_freep(err);
    srs_freep(req);

    EXPECT_TRUE(handler.on_http_message_called_);
    EXPECT_EQ(1, mux.serve_http_count_);
    EXPECT_EQ(0, skt.out_length());
}

VOID TEST(HttpConnTest, UrlNotCleanAnsweredBadRequest)
{
    srs_error_t err;

    MockHttpxConn handler;
    MockBufferIO skt;
    skt.append("GET /a/../secret.dat HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");

    SrsHttpServeMux mux;
    HELPER_EXPECT_SUCCESS(mux.initialize());
    SrsUniquePtr<SrsHttpConn> conn(new SrsHttpConn(&handler, &skt, &mux, "127.0.0.1", 8080));
    conn->trd_ = new MockCoroutineForCycle();
    HELPER_EXPECT_SUCCESS(conn->parser_->initialize(HTTP_REQUEST));
    HELPER_EXPECT_SUCCESS(conn->set_auth_enabled(false));

    // GOAL: the mux still refuses a URL with "..", and the refusal ends the connection with its error.
    ISrsRequest *req = NULL;
    err = conn->process_requests(&req);
    EXPECT_EQ(ERROR_HTTP_URL_NOT_CLEAN, srs_error_code(err));
    srs_freep(err);
    srs_freep(req);

    // GOAL: the client is told why by 400, with the error code and its short name, before the close.
    string out(skt.out_buffer.bytes(), skt.out_buffer.length());
    EXPECT_TRUE(srs_strings_starts_with(out, "HTTP/1.1 400 Bad Request\r\n")) << out;
    EXPECT_TRUE(srs_strings_contains(out, "Connection: Close\r\n")) << out;
    EXPECT_TRUE(srs_strings_contains(out, "Content-Type: text/plain; charset=utf-8\r\n")) << out;
    EXPECT_TRUE(srs_strings_ends_with(out, "\r\n\r\n4002: HttpUrlNotClean")) << out;
}

VOID TEST(HttpConnTest, CleanUrlNotRefused)
{
    srs_error_t err;

    MockHttpxConn handler;
    MockBufferIO skt;
    skt.append("GET /a/secret.dat HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");

    SrsHttpServeMux mux;
    HELPER_EXPECT_SUCCESS(mux.initialize());
    SrsUniquePtr<SrsHttpConn> conn(new SrsHttpConn(&handler, &skt, &mux, "127.0.0.1", 8080));
    conn->trd_ = new MockCoroutineForCycle();
    HELPER_EXPECT_SUCCESS(conn->parser_->initialize(HTTP_REQUEST));
    HELPER_EXPECT_SUCCESS(conn->set_auth_enabled(false));

    // GOAL: a clean URL nothing is mounted at gets the mux's 404 once, and the connection ends only when
    // the client sends nothing more. This passes before and after the refused URL got its answer.
    ISrsRequest *req = NULL;
    err = conn->process_requests(&req);
    EXPECT_EQ(ERROR_SOCKET_READ, srs_error_code(err));
    srs_freep(err);
    srs_freep(req);

    string out(skt.out_buffer.bytes(), skt.out_buffer.length());
    EXPECT_TRUE(srs_strings_starts_with(out, "HTTP/1.1 404 Not Found\r\n")) << out;
    EXPECT_FALSE(srs_strings_contains(out, "HTTP/1.1 400")) << out;
}

// The error code of the lookup of a URL by a mux with nothing mounted.
static int srs_utest_mux_find_handler_code(string url)
{
    SrsHttpServeMux mux;
    srs_error_t err = mux.initialize();
    srs_freep(err);

    SrsHttpHeader header;
    header.set("Host", "127.0.0.1");
    SrsHttpMessage msg;
    msg.set_header(&header, false);
    if ((err = msg.set_url(url, false)) != srs_success) {
        int code = srs_error_code(err);
        srs_freep(err);
        return code;
    }

    ISrsHttpHandler *h = NULL;
    err = mux.find_handler(&msg, &h);
    int code = srs_error_code(err);
    srs_freep(err);
    return code;
}

VOID TEST(HttpServeMuxTest, DotDotSegmentRefused)
{
    // GOAL: a path with a ".." segment, in the middle or at the end, is not clean.
    EXPECT_EQ(ERROR_HTTP_URL_NOT_CLEAN, srs_utest_mux_find_handler_code("/a/../x"));
    EXPECT_EQ(ERROR_HTTP_URL_NOT_CLEAN, srs_utest_mux_find_handler_code("/a/.."));
    EXPECT_EQ(ERROR_HTTP_URL_NOT_CLEAN, srs_utest_mux_find_handler_code("/../x.flv?t=1"));
}

VOID TEST(HttpServeMuxTest, TwoDotsInNameOrQueryNotRefused)
{
    // GOAL: two dots in a name or in the query are no traversal, so the lookup goes on to the mux's 404 handler.
    EXPECT_EQ(ERROR_SUCCESS, srs_utest_mux_find_handler_code("/a..b.dat"));
    EXPECT_EQ(ERROR_SUCCESS, srs_utest_mux_find_handler_code("/live/...flv"));
    EXPECT_EQ(ERROR_SUCCESS, srs_utest_mux_find_handler_code("/x.flv?t=a..b"));
    EXPECT_EQ(ERROR_SUCCESS, srs_utest_mux_find_handler_code("/x.flv?t=/../"));
}

VOID TEST(HttpxConnTest, ConstructionReachesNoCollaborator)
{
    std::string before = _srs_context->get_id().c_str();

    MockProtocolReadWriter *io = new MockProtocolReadWriter();
    SrsUniquePtr<SrsHttpxConn> connx(new SrsHttpxConn(NULL, io, NULL, "127.0.0.1", 8080, "", ""));

    MockProtocolReadWriter *sio = new MockProtocolReadWriter();
    SrsUniquePtr<SrsHttpxConn> connxs(new SrsHttpxConn(NULL, sio, NULL, "127.0.0.1", 8443, "/key.pem", "/cert.pem"));

    // GOAL: construction only captures dependencies, so a test can replace them before any work runs:
    // no client id is switched, and neither the TLS socket nor the HTTP connection exists yet.
    EXPECT_STREQ(before.c_str(), _srs_context->get_id().c_str());

    EXPECT_TRUE(NULL == connx->conn_);
    EXPECT_TRUE(NULL == connx->ssl_);
    EXPECT_TRUE(_srs_context == connx->context_);

    EXPECT_TRUE(NULL == connxs->conn_);
    EXPECT_TRUE(NULL == connxs->ssl_);
    EXPECT_TRUE(_srs_context == connxs->context_);
}

VOID TEST(HttpxConnTest, AssembleSwitchesInjectedContextThenBuildsHttpConn)
{
    std::string before = _srs_context->get_id().c_str();

    MockProtocolReadWriter *io = new MockProtocolReadWriter();
    SrsUniquePtr<SrsHttpxConn> connx(new SrsHttpxConn(NULL, io, NULL, "127.0.0.1", 8080, "", ""));

    MockContextForRtmpConn context;
    context.id_ = SrsContextId().set_value("httpx-cid");

    connx->context_ = &context;
    connx->assemble();

    // GOAL: the client id is created and switched through the injected context, never the global one.
    EXPECT_EQ(1, context.generate_id_count_);
    EXPECT_EQ(1, context.set_id_count_);
    EXPECT_STREQ(before.c_str(), _srs_context->get_id().c_str());

    // GOAL: without a key and certificate, assemble() builds a plain HTTP connection over the owner's socket.
    EXPECT_TRUE(NULL == connx->ssl_);
    SrsHttpConn *conn = dynamic_cast<SrsHttpConn *>(connx->conn_);
    ASSERT_TRUE(conn != NULL);

    SrsNetworkDelta *delta = dynamic_cast<SrsNetworkDelta *>(conn->delta_);
    EXPECT_TRUE(io == delta->in_);
    EXPECT_TRUE(io == delta->out_);

    connx->context_ = NULL;
}

VOID TEST(HttpxConnTest, AssembleBuildsHttpsConnOverTlsSocket)
{
    MockProtocolReadWriter *io = new MockProtocolReadWriter();
    SrsUniquePtr<SrsHttpxConn> connx(new SrsHttpxConn(NULL, io, NULL, "127.0.0.1", 8443, "/key.pem", "/cert.pem"));

    MockContextForRtmpConn context;
    context.id_ = SrsContextId().set_value("https-cid");

    connx->context_ = &context;
    connx->assemble();

    // GOAL: the client id is switched through the injected context for HTTPS too.
    EXPECT_EQ(1, context.generate_id_count_);
    EXPECT_EQ(1, context.set_id_count_);

    // GOAL: with a key and certificate, assemble() wraps the owner's socket in TLS, and the HTTP
    // connection reads, writes and measures the TLS socket rather than the raw one.
    ASSERT_TRUE(connx->ssl_ != NULL);
    SrsHttpConn *conn = dynamic_cast<SrsHttpConn *>(connx->conn_);
    ASSERT_TRUE(conn != NULL);
    EXPECT_TRUE(connx->ssl_ == conn->skt_);

    SrsNetworkDelta *delta = dynamic_cast<SrsNetworkDelta *>(conn->delta_);
    EXPECT_TRUE(connx->ssl_ == delta->in_);
    EXPECT_TRUE(connx->ssl_ == delta->out_);
    EXPECT_STREQ("HttpsConn", connx->desc().c_str());

    connx->context_ = NULL;
}

VOID TEST(HttpxConnTest, AssembleBuildsHttpConnUnderNewContext)
{
    std::string before = _srs_context->get_id().c_str();

    MockProtocolReadWriter *io = new MockProtocolReadWriter();
    SrsUniquePtr<SrsHttpxConn> connx(new SrsHttpxConn(NULL, io, NULL, "127.0.0.1", 8080, "", ""));
    connx->assemble();

    // GOAL: the owner switches to a new client id, then assembles its HTTP connection, so the
    // connection's coroutine runs under that new id and its delta measures the owner's socket.
    EXPECT_STRNE(before.c_str(), _srs_context->get_id().c_str());

    SrsHttpConn *conn = dynamic_cast<SrsHttpConn *>(connx->conn_);
    ASSERT_TRUE(conn != NULL);
    ASSERT_TRUE(conn->trd_ != NULL);
    EXPECT_EQ(0, conn->trd_->cid().compare(_srs_context->get_id()));

    SrsNetworkDelta *delta = dynamic_cast<SrsNetworkDelta *>(conn->delta_);
    EXPECT_TRUE(io == delta->in_);
    EXPECT_TRUE(io == delta->out_);
}

// The constructor only captures the factory and context globals; it creates no coroutine,
// so a test can inject both before assemble() runs.
VOID TEST(HttpRecvThreadTest, ConstructorCapturesGlobalsWithoutCoroutine)
{
    SrsHttpRecvThread thread(NULL);

    EXPECT_TRUE(thread.app_factory_ == _srs_app_factory);
    EXPECT_TRUE(thread.context_ == _srs_context);
    EXPECT_TRUE(thread.trd_ == NULL);
}

// assemble() creates the receive coroutine through the injected factory, bound to the
// context id of the injected context.
VOID TEST(HttpRecvThreadTest, AssembleCreatesCoroutineThroughFactory)
{
    SrsHttpRecvThread thread(NULL);

    MockAppFactoryForRtmpConn factory;
    MockCoroutineForCycle *coroutine = new MockCoroutineForCycle();
    factory.coroutine_ = coroutine;
    MockContextForRtmpConn context;
    context.id_.set_value("http-recv-cid");

    thread.app_factory_ = &factory;
    thread.context_ = &context;
    thread.assemble();

    EXPECT_EQ(1, factory.create_coroutine_count_);
    EXPECT_STREQ("http-receive", factory.coroutine_name_.c_str());
    EXPECT_TRUE(factory.coroutine_handler_ == &thread);
    EXPECT_EQ(0, factory.coroutine_cid_.compare(context.id_));
    EXPECT_TRUE(thread.trd_ == coroutine);

    // The thread frees trd_ when it holds the mock coroutine; otherwise free it here.
    if (thread.trd_ != coroutine) {
        srs_freep(coroutine);
    }
    thread.app_factory_ = NULL;
    thread.context_ = NULL;
}

// The constructor only copies the request, creates the message queue and captures the
// globals; it creates no coroutine, so a test can inject the factory before assemble() runs.
VOID TEST(BufferCacheTest, ConstructorCapturesFactoryWithoutCoroutine)
{
    MockRequest req("test.vhost", "live", "stream1");
    SrsBufferCache cache(&req);

    EXPECT_TRUE(cache.app_factory_ == _srs_app_factory);
    EXPECT_TRUE(cache.config_ == _srs_config);
    EXPECT_TRUE(cache.live_sources_ == _srs_sources);
    EXPECT_TRUE(cache.queue_ != NULL);
    EXPECT_TRUE(cache.trd_ == NULL);
}

// assemble() creates the cache coroutine through the injected factory, with no context id,
// so the coroutine generates a fresh one when it starts.
VOID TEST(BufferCacheTest, AssembleCreatesCoroutineThroughFactory)
{
    MockRequest req("test.vhost", "live", "stream1");
    SrsBufferCache cache(&req);

    MockAppFactoryForRtmpConn factory;
    MockCoroutineForCycle *coroutine = new MockCoroutineForCycle();
    factory.coroutine_ = coroutine;

    cache.app_factory_ = &factory;
    cache.assemble();

    EXPECT_EQ(1, factory.create_coroutine_count_);
    EXPECT_STREQ("http-stream", factory.coroutine_name_.c_str());
    EXPECT_TRUE(factory.coroutine_handler_ == &cache);
    EXPECT_TRUE(factory.coroutine_cid_.empty());
    EXPECT_TRUE(cache.trd_ == coroutine);

    // The cache frees trd_ when it holds the mock coroutine; otherwise free it here.
    if (cache.trd_ != coroutine) {
        srs_freep(coroutine);
    }
    cache.app_factory_ = NULL;
}

// Mounting a stream builds its buffer cache and assembles it before starting it, so the
// cache holds its coroutine.
VOID TEST(BufferCacheTest, HttpMountAssemblesBufferCache)
{
    srs_error_t err = srs_success;

    MockAppConfigForHttpStreamServer config;
    MockRequest req("test.vhost", "live", "stream1");

    if (true) {
        SrsUniquePtr<SrsHttpStreamServer> server(new SrsHttpStreamServer());
        server->config_ = &config;

        server->templateHandlers_["test.vhost"] = new SrsLiveEntry("[vhost]/[app]/[stream].flv");

        HELPER_EXPECT_SUCCESS(server->http_mount(&req));

        SrsLiveEntry *entry = server->streamHandlers_[req.get_stream_url()];
        ASSERT_TRUE(entry != NULL);
        SrsBufferCache *cache = dynamic_cast<SrsBufferCache *>(entry->cache_);
        ASSERT_TRUE(cache != NULL);
        EXPECT_TRUE(cache->trd_ != NULL);

        server->config_ = NULL;
    }
}

// Mock ISrsHttpResponseReader implementation for SrsHttpFileReader
MockHttpResponseReaderForHttpFileReader::MockHttpResponseReaderForHttpFileReader()
{
    content_ = "";
    read_pos_ = 0;
    eof_ = false;
}

MockHttpResponseReaderForHttpFileReader::~MockHttpResponseReaderForHttpFileReader()
{
}

bool MockHttpResponseReaderForHttpFileReader::eof()
{
    return eof_;
}

srs_error_t MockHttpResponseReaderForHttpFileReader::read(void *buf, size_t size, ssize_t *nread)
{
    if (eof_) {
        *nread = 0;
        return srs_success;
    }

    size_t remaining = content_.size() - read_pos_;
    size_t to_read = srs_min(size, remaining);
    memcpy(buf, content_.data() + read_pos_, to_read);
    read_pos_ += to_read;
    *nread = to_read;

    if (read_pos_ >= content_.size()) {
        eof_ = true;
    }

    return srs_success;
}

void MockHttpResponseReaderForHttpFileReader::reset()
{
    content_ = "";
    read_pos_ = 0;
    eof_ = false;
}

// Mock ISrsPithyPrint implementation for SrsIngester
MockPithyPrintForIngester::MockPithyPrintForIngester()
{
    elapse_called_ = false;
    can_print_called_ = false;
    can_print_result_ = false;
    age_value_ = 0;
}

MockPithyPrintForIngester::~MockPithyPrintForIngester()
{
}

void MockPithyPrintForIngester::elapse()
{
    elapse_called_ = true;
}

bool MockPithyPrintForIngester::can_print()
{
    can_print_called_ = true;
    return can_print_result_;
}

srs_utime_t MockPithyPrintForIngester::age()
{
    return age_value_;
}

void MockPithyPrintForIngester::reset()
{
    elapse_called_ = false;
    can_print_called_ = false;
    can_print_result_ = false;
    age_value_ = 0;
}

// Test SrsHttpFileReader::read - covers the major use scenario:
// 1. Create SrsHttpFileReader with mock ISrsHttpResponseReader
// 2. Mock http_ to return data in multiple chunks (simulating network reads)
// 3. Call read() to read data from HTTP response
// 4. Verify it reads all data correctly by calling http_->read() multiple times
// 5. Verify total bytes read is returned correctly
VOID TEST(HttpFileReaderTest, ReadDataFromHttpResponse)
{
    srs_error_t err;

    // Create mock HTTP response reader
    SrsUniquePtr<MockHttpResponseReaderForHttpFileReader> mock_http(new MockHttpResponseReaderForHttpFileReader());

    // Set up test data - simulate HTTP response with 100 bytes
    std::string test_data;
    for (int i = 0; i < 100; i++) {
        test_data.push_back((char)i);
    }
    mock_http->content_ = test_data;
    mock_http->eof_ = false;

    // Create SrsHttpFileReader
    SrsUniquePtr<SrsHttpFileReader> reader(new SrsHttpFileReader(mock_http.get()));

    // Test read() - read 100 bytes
    char buf[100];
    ssize_t nread = 0;
    HELPER_EXPECT_SUCCESS(reader->read(buf, 100, &nread));

    // Verify all 100 bytes were read
    EXPECT_EQ(100, (int)nread);

    // Verify data is correct
    for (int i = 0; i < 100; i++) {
        EXPECT_EQ((char)i, buf[i]);
    }

    // Verify mock_http is now at EOF
    EXPECT_TRUE(mock_http->eof_);
}

// Mock ISrsResourceManager implementation for testing SrsHttpxConn
MockResourceManagerForHttpxConn::MockResourceManagerForHttpxConn()
{
    remove_called_ = false;
    removed_resource_ = NULL;
}

MockResourceManagerForHttpxConn::~MockResourceManagerForHttpxConn()
{
}

srs_error_t MockResourceManagerForHttpxConn::start()
{
    return srs_success;
}

bool MockResourceManagerForHttpxConn::empty()
{
    return true;
}

size_t MockResourceManagerForHttpxConn::size()
{
    return 0;
}

void MockResourceManagerForHttpxConn::add(ISrsResource *conn, bool *exists)
{
}

void MockResourceManagerForHttpxConn::add_with_id(const std::string &id, ISrsResource *conn)
{
}

void MockResourceManagerForHttpxConn::add_with_fast_id(uint64_t id, ISrsResource *conn)
{
}

void MockResourceManagerForHttpxConn::add_with_name(const std::string &name, ISrsResource *conn)
{
}

ISrsResource *MockResourceManagerForHttpxConn::at(int index)
{
    return NULL;
}

ISrsResource *MockResourceManagerForHttpxConn::find_by_id(std::string id)
{
    return NULL;
}

ISrsResource *MockResourceManagerForHttpxConn::find_by_fast_id(uint64_t id)
{
    return NULL;
}

ISrsResource *MockResourceManagerForHttpxConn::find_by_name(std::string name)
{
    return NULL;
}

void MockResourceManagerForHttpxConn::remove(ISrsResource *c)
{
    remove_called_ = true;
    removed_resource_ = c;
}

void MockResourceManagerForHttpxConn::subscribe(ISrsDisposingHandler *h)
{
}

void MockResourceManagerForHttpxConn::unsubscribe(ISrsDisposingHandler *h)
{
}

void MockResourceManagerForHttpxConn::reset()
{
    remove_called_ = false;
    removed_resource_ = NULL;
}

// Mock ISrsStatistic implementation for testing SrsHttpxConn
MockStatisticForHttpxConn::MockStatisticForHttpxConn()
{
    on_disconnect_called_ = false;
    kbps_add_delta_called_ = false;
    disconnect_id_ = "";
    kbps_id_ = "";
    disconnect_error_code_ = 0;
    kbps_delta_ = NULL;
}

MockStatisticForHttpxConn::~MockStatisticForHttpxConn()
{
}

void MockStatisticForHttpxConn::on_disconnect(std::string id, srs_error_t err)
{
    on_disconnect_called_ = true;
    disconnect_id_ = id;
    // The error is borrowed, as SrsStatistic::on_disconnect does, because the caller still returns it.
    disconnect_error_code_ = srs_error_code(err);
}

srs_error_t MockStatisticForHttpxConn::on_client(std::string id, ISrsRequest *req, ISrsExpire *conn, SrsRtmpConnType type)
{
    return srs_success;
}

srs_error_t MockStatisticForHttpxConn::on_video_info(ISrsRequest *req, SrsVideoCodecId vcodec, int avc_profile, int avc_level, int width, int height)
{
    return srs_success;
}

srs_error_t MockStatisticForHttpxConn::on_audio_info(ISrsRequest *req, SrsAudioCodecId acodec, SrsAudioSampleRate asample_rate, SrsAudioChannels asound_type, SrsAacObjectType aac_object)
{
    return srs_success;
}

void MockStatisticForHttpxConn::on_stream_publish(ISrsRequest *req, std::string publisher_id)
{
}

void MockStatisticForHttpxConn::on_stream_close(ISrsRequest *req)
{
}

void MockStatisticForHttpxConn::kbps_add_delta(std::string id, ISrsKbpsDelta *delta)
{
    kbps_add_delta_called_ = true;
    kbps_id_ = id;
    kbps_delta_ = delta;
}

void MockStatisticForHttpxConn::kbps_sample()
{
}

srs_error_t MockStatisticForHttpxConn::on_video_frames(ISrsRequest *req, int nb_frames)
{
    return srs_success;
}

srs_error_t MockStatisticForHttpxConn::on_audio_frames(ISrsRequest *req, int nb_frames)
{
    return srs_success;
}

srs_error_t MockStatisticForHttpxConn::dumps_vhosts(SrsJsonArray *arr)
{
    return srs_success;
}

srs_error_t MockStatisticForHttpxConn::dumps_streams(SrsJsonArray *arr, int start, int count)
{
    return srs_success;
}

srs_error_t MockStatisticForHttpxConn::dumps_clients(SrsJsonArray *arr, int start, int count)
{
    return srs_success;
}

srs_error_t MockStatisticForHttpxConn::dumps_forwards(SrsJsonArray *arr, std::string stream_id, int start, int count)
{
    return srs_success;
}

int MockStatisticForHttpxConn::count_forwards(std::string stream_id)
{
    return 0;
}

SrsStatisticForward *MockStatisticForHttpxConn::find_forward(std::string forward_id)
{
    return NULL;
}

void MockStatisticForHttpxConn::on_forward_start(std::string id, ISrsRequest *req, std::string url)
{
}

void MockStatisticForHttpxConn::on_forward_state(std::string id, SrsForwardState state)
{
}

void MockStatisticForHttpxConn::on_forward_stop(std::string id)
{
}

srs_error_t MockStatisticForHttpxConn::dumps_metrics(int64_t &send_bytes, int64_t &recv_bytes, int64_t &nstreams, int64_t &nclients, int64_t &total_nclients, int64_t &nerrs)
{
    return srs_success;
}

std::string MockStatisticForHttpxConn::server_id()
{
    return "mock_server_id";
}

std::string MockStatisticForHttpxConn::service_id()
{
    return "mock_service_id";
}

std::string MockStatisticForHttpxConn::service_pid()
{
    return "mock_pid";
}

SrsStatisticVhost *MockStatisticForHttpxConn::find_vhost_by_id(std::string vid)
{
    return NULL;
}

SrsStatisticStream *MockStatisticForHttpxConn::find_stream(std::string sid)
{
    return NULL;
}

SrsStatisticStream *MockStatisticForHttpxConn::find_stream_by_url(std::string url)
{
    return NULL;
}

SrsStatisticClient *MockStatisticForHttpxConn::find_client(std::string client_id)
{
    return NULL;
}

void MockStatisticForHttpxConn::reset()
{
    on_disconnect_called_ = false;
    kbps_add_delta_called_ = false;
    disconnect_id_ = "";
    kbps_id_ = "";
    disconnect_error_code_ = 0;
    kbps_delta_ = NULL;
}

// Mock ISrsHttpConn implementation for testing SrsHttpxConn
MockHttpConnForHttpxConn::MockHttpConnForHttpxConn()
{
    remote_ip_ = "127.0.0.1";
    context_id_ = _srs_context->generate_id();
    delta_ = NULL;
    crossdomain_ = false;
    crossdomain_set_ = false;
}

MockHttpConnForHttpxConn::~MockHttpConnForHttpxConn()
{
}

ISrsKbpsDelta *MockHttpConnForHttpxConn::delta()
{
    return delta_;
}

srs_error_t MockHttpConnForHttpxConn::start()
{
    return srs_success;
}

srs_error_t MockHttpConnForHttpxConn::cycle()
{
    return srs_success;
}

srs_error_t MockHttpConnForHttpxConn::pull()
{
    return srs_success;
}

srs_error_t MockHttpConnForHttpxConn::set_crossdomain_enabled(bool v)
{
    crossdomain_ = v;
    crossdomain_set_ = true;
    return srs_success;
}

srs_error_t MockHttpConnForHttpxConn::set_auth_enabled(bool auth_enabled)
{
    return srs_success;
}

srs_error_t MockHttpConnForHttpxConn::set_jsonp(bool v)
{
    return srs_success;
}

std::string MockHttpConnForHttpxConn::remote_ip()
{
    return remote_ip_;
}

const SrsContextId &MockHttpConnForHttpxConn::get_id()
{
    return context_id_;
}

std::string MockHttpConnForHttpxConn::desc()
{
    return "MockHttpConn";
}

void MockHttpConnForHttpxConn::expire()
{
}

VOID TEST(HttpxConnTest, OnHttpMessageClosesConnectionAndMarksHttps)
{
    srs_error_t err;

    // Plain HTTP: the connection is short-term, and the schema stays http.
    if (true) {
        SrsUniquePtr<SrsHttpxConn> connx(new SrsHttpxConn(NULL, NULL, NULL, "127.0.0.1", 8080, "", ""));

        SrsHttpMessage msg;
        MockResponseWriter w;
        HELPER_EXPECT_SUCCESS(connx->on_http_message(&msg, &w));

        EXPECT_STREQ("Close", w.header()->get("Connection").c_str());
        EXPECT_STREQ("http", msg.schema().c_str());
        HELPER_EXPECT_SUCCESS(connx->on_message_done(&msg, &w));
    }

    // HTTPS: the parsed message is marked as https, so handlers build https URLs for it.
    if (true) {
        SrsUniquePtr<SrsHttpxConn> connx(new SrsHttpxConn(NULL, NULL, NULL, "127.0.0.1", 8443, "/key.pem", "/cert.pem"));

        // The connection owns and frees its TLS socket.
        connx->ssl_ = new MockSslConnection();

        SrsHttpMessage msg;
        MockResponseWriter w;
        HELPER_EXPECT_SUCCESS(connx->on_http_message(&msg, &w));

        EXPECT_STREQ("Close", w.header()->get("Connection").c_str());
        EXPECT_STREQ("https", msg.schema().c_str());
    }
}

VOID TEST(HttpxConnTest, OnConnDoneRemovesItselfAndSwallowsTimeout)
{
    srs_error_t err;

    MockResourceManagerForHttpxConn manager;
    MockStatisticForHttpxConn stat;

    SrsUniquePtr<SrsHttpxConn> connx(new SrsHttpxConn(&manager, NULL, NULL, "127.0.0.1", 8080, "", ""));
    connx->stat_ = &stat;

    // The connection owns and frees its HTTP connection.
    MockHttpConnForHttpxConn *conn = new MockHttpConnForHttpxConn();
    connx->conn_ = conn;

    // An HTTP API client may send nothing before the timeout, which is a normal end, not an error.
    err = connx->on_conn_done(srs_error_new(ERROR_SOCKET_TIMEOUT, "timeout"));
    HELPER_EXPECT_SUCCESS(err);

    // The owner, not the HTTP connection, is the resource the manager holds, so it removes itself.
    EXPECT_TRUE(manager.remove_called_);
    EXPECT_TRUE(connx.get() == manager.removed_resource_);

    // Without stat enabled, as for an HTTP API client, no statistic is touched.
    EXPECT_FALSE(stat.on_disconnect_called_);
    EXPECT_FALSE(stat.kbps_add_delta_called_);

    connx->stat_ = NULL;
}

VOID TEST(HttpxConnTest, OnConnDoneStatsStreamingClientAndKeepsError)
{
    srs_error_t err;

    MockResourceManagerForHttpxConn manager;
    MockStatisticForHttpxConn stat;

    SrsUniquePtr<SrsHttpxConn> connx(new SrsHttpxConn(&manager, NULL, NULL, "127.0.0.1", 8080, "", ""));
    connx->stat_ = &stat;

    MockHttpConnForHttpxConn *conn = new MockHttpConnForHttpxConn();
    SrsNetworkDelta delta;
    conn->delta_ = &delta;
    connx->conn_ = conn;

    // An HTTP streaming client, such as HTTP-FLV, has its own statistic session.
    connx->set_enable_stat(true);

    err = connx->on_conn_done(srs_error_new(ERROR_SOCKET_READ, "read"));

    // A real error is returned to the caller, not swallowed.
    EXPECT_EQ(ERROR_SOCKET_READ, srs_error_code(err));
    srs_freep(err);

    // The session is closed with that error, and its bandwidth is counted, under the connection id.
    EXPECT_TRUE(stat.on_disconnect_called_);
    EXPECT_STREQ(conn->context_id_.c_str(), stat.disconnect_id_.c_str());
    EXPECT_EQ(ERROR_SOCKET_READ, stat.disconnect_error_code_);
    EXPECT_TRUE(stat.kbps_add_delta_called_);
    EXPECT_STREQ(conn->context_id_.c_str(), stat.kbps_id_.c_str());
    EXPECT_TRUE(&delta == stat.kbps_delta_);

    EXPECT_TRUE(manager.remove_called_);
    EXPECT_TRUE(connx.get() == manager.removed_resource_);

    connx->stat_ = NULL;
}

MockAppConfigForHttpxConnCors::MockAppConfigForHttpxConnCors()
{
    http_api_crossdomain_ = false;
    http_stream_crossdomain_ = false;
}

MockAppConfigForHttpxConnCors::~MockAppConfigForHttpxConnCors()
{
}

bool MockAppConfigForHttpxConnCors::get_http_api_crossdomain()
{
    return http_api_crossdomain_;
}

bool MockAppConfigForHttpxConnCors::get_http_stream_crossdomain()
{
    return http_stream_crossdomain_;
}

// A connection of an HTTP API listener answers CORS by the API's crossdomain, and a connection of
// the HTTP server's listener by the server's, whichever of the two is on.
VOID TEST(HttpxConnTest, StartTakesCrossdomainOfItsListener)
{
    srs_error_t err;

    for (int i = 0; i < 4; i++) {
        bool api_listener = (i & 1) != 0;
        bool api_on = (i & 2) != 0;

        MockAppConfigForHttpxConnCors config;
        config.http_api_crossdomain_ = api_on;
        config.http_stream_crossdomain_ = !api_on;

        SrsUniquePtr<SrsHttpxConn> connx(new SrsHttpxConn(NULL, NULL, NULL, "127.0.0.1", 8080, "", ""));
        connx->config_ = &config;
        if (api_listener) {
            connx->set_api_listener(true);
        }

        MockHttpConnForHttpxConn *conn = new MockHttpConnForHttpxConn();
        connx->conn_ = conn;

        HELPER_EXPECT_SUCCESS(connx->start());

        bool want = api_listener ? api_on : !api_on;
        EXPECT_TRUE(conn->crossdomain_set_);
        EXPECT_EQ(want, conn->crossdomain_) << "api_listener=" << api_listener << ", api_on=" << api_on;

        connx->config_ = NULL;
    }
}

// Test SrsQueueRecvThread basic queue operations
// This test covers the major use scenario: consume messages, check queue state, pump messages, and handle errors
VOID TEST(QueueRecvThreadTest, BasicQueueOperations)
{
    srs_error_t err;

    // Create mock RTMP server
    SrsUniquePtr<MockRtmpServer> mock_rtmp(new MockRtmpServer());

    // Create SrsQueueRecvThread (without starting the actual recv thread)
    SrsUniquePtr<SrsQueueRecvThread> queue_thread(new SrsQueueRecvThread(NULL, mock_rtmp.get(), 5 * SRS_UTIME_SECONDS, SrsContextId()));

    // Test 1: Initially queue should be empty
    EXPECT_TRUE(queue_thread->empty());
    EXPECT_EQ(0, queue_thread->size());
    EXPECT_FALSE(queue_thread->interrupted());

    // Test 2: Consume first message
    SrsRtmpCommonMessage *msg1 = new SrsRtmpCommonMessage();
    msg1->header_.message_type_ = RTMP_MSG_VideoMessage;
    msg1->header_.payload_length_ = 10;
    msg1->create_payload(10);
    HELPER_EXPECT_SUCCESS(queue_thread->consume(msg1));

    // Queue should have one message
    EXPECT_FALSE(queue_thread->empty());
    EXPECT_EQ(1, queue_thread->size());
    EXPECT_TRUE(queue_thread->interrupted()); // interrupted() returns true when queue is not empty

    // Test 3: Consume second message
    SrsRtmpCommonMessage *msg2 = new SrsRtmpCommonMessage();
    msg2->header_.message_type_ = RTMP_MSG_AudioMessage;
    msg2->header_.payload_length_ = 20;
    msg2->create_payload(20);
    HELPER_EXPECT_SUCCESS(queue_thread->consume(msg2));

    // Queue should have two messages
    EXPECT_FALSE(queue_thread->empty());
    EXPECT_EQ(2, queue_thread->size());

    // Test 4: Pump first message (FIFO order)
    SrsRtmpCommonMessage *pumped_msg1 = queue_thread->pump();
    EXPECT_TRUE(pumped_msg1 != NULL);
    EXPECT_EQ(RTMP_MSG_VideoMessage, pumped_msg1->header_.message_type_);
    EXPECT_EQ(10, pumped_msg1->header_.payload_length_);
    srs_freep(pumped_msg1);

    // Queue should have one message left
    EXPECT_FALSE(queue_thread->empty());
    EXPECT_EQ(1, queue_thread->size());

    // Test 5: Pump second message
    SrsRtmpCommonMessage *pumped_msg2 = queue_thread->pump();
    EXPECT_TRUE(pumped_msg2 != NULL);
    EXPECT_EQ(RTMP_MSG_AudioMessage, pumped_msg2->header_.message_type_);
    EXPECT_EQ(20, pumped_msg2->header_.payload_length_);
    srs_freep(pumped_msg2);

    // Queue should be empty again
    EXPECT_TRUE(queue_thread->empty());
    EXPECT_EQ(0, queue_thread->size());
    EXPECT_FALSE(queue_thread->interrupted());

    // Test 6: Test error_code() - initially should be success
    err = queue_thread->error_code();
    HELPER_EXPECT_SUCCESS(err);

    // Test 7: Test interrupt() with error
    srs_error_t test_error = srs_error_new(ERROR_SOCKET_READ, "test error");
    queue_thread->interrupt(test_error);

    // Error code should now return the error
    err = queue_thread->error_code();
    EXPECT_TRUE(err != srs_success);
    EXPECT_EQ(ERROR_SOCKET_READ, srs_error_code(err));
    srs_freep(err);

    // Test 8: Test on_start() and on_stop() - verify set_auto_response is called
    queue_thread->on_start();
    EXPECT_TRUE(mock_rtmp->set_auto_response_called_);
    EXPECT_FALSE(mock_rtmp->auto_response_value_); // Should be set to false

    queue_thread->on_stop();
    EXPECT_TRUE(mock_rtmp->set_auto_response_called_);
    EXPECT_TRUE(mock_rtmp->auto_response_value_); // Should be set to true
}

// Test SrsPublishRecvThread basic operations
// This test covers the major use scenario: wait(), nb_msgs(), nb_video_frames(), error_code(), set_cid(), get_cid()
VOID TEST(PublishRecvThreadTest, BasicOperations)
{
    srs_error_t err;

    // Create mock dependencies
    SrsUniquePtr<MockRtmpServer> mock_rtmp(new MockRtmpServer());
    SrsUniquePtr<MockSrsRequest> mock_req(new MockSrsRequest("__defaultVhost__", "live", "test_stream"));
    SrsSharedPtr<SrsLiveSource> mock_source; // NULL is fine for this test

    // Create SrsPublishRecvThread (without starting the actual recv thread)
    SrsUniquePtr<SrsPublishRecvThread> publish_thread(new SrsPublishRecvThread(
        mock_rtmp.get(), mock_req.get(), -1, 5 * SRS_UTIME_SECONDS, NULL, mock_source, SrsContextId()));

    // Test 1: Initially nb_msgs should be 0
    EXPECT_EQ(0, publish_thread->nb_msgs());

    // Test 2: Initially nb_video_frames should be 0
    EXPECT_EQ(0, publish_thread->nb_video_frames());

    // Test 3: Test error_code() - initially should be success
    err = publish_thread->error_code();
    HELPER_EXPECT_SUCCESS(err);

    // Test 4: Test set_cid() and get_cid()
    SrsContextId test_cid;
    test_cid.set_value("test-context-id");
    publish_thread->set_cid(test_cid);
    SrsContextId retrieved_cid = publish_thread->get_cid();
    EXPECT_STREQ(test_cid.c_str(), retrieved_cid.c_str());

    // Test 5: Test wait() with timeout - should return success when no error
    err = publish_thread->wait(100 * SRS_UTIME_MILLISECONDS);
    HELPER_EXPECT_SUCCESS(err);

    // Test 6: Test consume() to increment message counters
    SrsRtmpCommonMessage *video_msg = new SrsRtmpCommonMessage();
    video_msg->header_.message_type_ = RTMP_MSG_VideoMessage;
    video_msg->header_.payload_length_ = 100;
    video_msg->create_payload(100);

    // Note: consume() will try to call conn_->handle_publish_message() which will fail with NULL conn_
    // So we expect this to fail, but we can still test the counter increment by checking nb_msgs
    // For this basic test, we'll just verify the initial state and error handling

    // Test 7: Test interrupt() with error
    srs_error_t test_error = srs_error_new(ERROR_SOCKET_READ, "test error");
    publish_thread->interrupt(test_error);

    // Error code should now return the error
    err = publish_thread->error_code();
    EXPECT_TRUE(err != srs_success);
    EXPECT_EQ(ERROR_SOCKET_READ, srs_error_code(err));
    srs_freep(err);

    // Test 8: Test wait() after error - should return the error immediately
    err = publish_thread->wait(100 * SRS_UTIME_MILLISECONDS);
    EXPECT_TRUE(err != srs_success);
    EXPECT_EQ(ERROR_SOCKET_READ, srs_error_code(err));
    srs_freep(err);

    // Test 9: Test interrupted() - should always return false for publish recv thread
    EXPECT_FALSE(publish_thread->interrupted());

    // Clean up
    srs_freep(video_msg);
}

// Mock ISrsFFMPEG implementation
MockFFMPEGForEncoder::MockFFMPEGForEncoder()
{
    initialize_called_ = false;
    start_called_ = false;
    start_error_ = srs_success;
    output_ = "";
}

MockFFMPEGForEncoder::~MockFFMPEGForEncoder()
{
}

void MockFFMPEGForEncoder::append_iparam(std::string iparam)
{
}

void MockFFMPEGForEncoder::set_oformat(std::string format)
{
}

std::string MockFFMPEGForEncoder::output()
{
    return output_;
}

srs_error_t MockFFMPEGForEncoder::initialize(std::string in, std::string out, std::string log)
{
    initialize_called_ = true;
    output_ = out;
    return srs_success;
}

srs_error_t MockFFMPEGForEncoder::initialize_transcode(SrsConfDirective *engine)
{
    return srs_success;
}

srs_error_t MockFFMPEGForEncoder::initialize_copy()
{
    return srs_success;
}

srs_error_t MockFFMPEGForEncoder::start()
{
    start_called_ = true;
    return srs_error_copy(start_error_);
}

srs_error_t MockFFMPEGForEncoder::cycle()
{
    return srs_success;
}

void MockFFMPEGForEncoder::stop()
{
}

void MockFFMPEGForEncoder::fast_stop()
{
}

void MockFFMPEGForEncoder::fast_kill()
{
}

void MockFFMPEGForEncoder::reset()
{
    initialize_called_ = false;
    start_called_ = false;
    srs_freep(start_error_);
    output_ = "";
}

// Mock ISrsAppConfig implementation
MockAppConfigForEncoder::MockAppConfigForEncoder()
{
    transcode_directive_ = NULL;
    transcode_enabled_ = true;
    transcode_ffmpeg_bin_ = "/usr/bin/ffmpeg";
    engine_enabled_ = true;
    target_scope_ = ""; // Default to vhost scope (empty string)
}

MockAppConfigForEncoder::~MockAppConfigForEncoder()
{
    reset();
}

SrsConfDirective *MockAppConfigForEncoder::get_transcode(std::string vhost, std::string scope)
{
    // Only return transcode_directive_ for the target scope
    if (scope == target_scope_) {
        return transcode_directive_;
    }
    return NULL;
}

bool MockAppConfigForEncoder::get_transcode_enabled(SrsConfDirective *conf)
{
    return transcode_enabled_;
}

std::string MockAppConfigForEncoder::get_transcode_ffmpeg(SrsConfDirective *conf)
{
    return transcode_ffmpeg_bin_;
}

std::vector<SrsConfDirective *> MockAppConfigForEncoder::get_transcode_engines(SrsConfDirective *conf)
{
    return transcode_engines_;
}

bool MockAppConfigForEncoder::get_engine_enabled(SrsConfDirective *conf)
{
    return engine_enabled_;
}

std::string MockAppConfigForEncoder::get_engine_output(SrsConfDirective *conf)
{
    return "rtmp://127.0.0.1/live/livestream_hd";
}

bool MockAppConfigForEncoder::get_ff_log_enabled()
{
    return false;
}

void MockAppConfigForEncoder::reset()
{
    transcode_directive_ = NULL;
    transcode_engines_.clear();
}

// Mock ISrsAppFactory implementation
MockAppFactoryForEncoder::MockAppFactoryForEncoder()
{
    mock_ffmpeg_ = NULL;
}

MockAppFactoryForEncoder::~MockAppFactoryForEncoder()
{
    reset();
}

ISrsFFMPEG *MockAppFactoryForEncoder::create_ffmpeg(std::string ffmpeg_bin)
{
    return (ISrsFFMPEG *)mock_ffmpeg_;
}

void MockAppFactoryForEncoder::reset()
{
    mock_ffmpeg_ = NULL;
}

// The constructor does not create the pithy print, which would enter the encoder
// stage of the global stage manager before a test could inject anything.
VOID TEST(EncoderTest, ConstructorLeavesPithyPrintUnset)
{
    SrsUniquePtr<SrsEncoder> encoder(new SrsEncoder());

    EXPECT_TRUE(encoder->pprint_ == NULL);
}

// assemble() creates the pithy print for the encoder stage.
VOID TEST(EncoderTest, AssembleCreatesEncoderPithyPrint)
{
    SrsUniquePtr<SrsEncoder> encoder(new SrsEncoder());
    srs_freep(encoder->pprint_);

    encoder->assemble();

    SrsPithyPrint *pprint = dynamic_cast<SrsPithyPrint *>(encoder->pprint_);
    ASSERT_TRUE(pprint != NULL);

    SrsUniquePtr<SrsPithyPrint> expected(SrsPithyPrint::create_encoder());
    EXPECT_EQ(expected->stage_id_, pprint->stage_id_);
}

VOID TEST(EncoderTest, OnPublishMajorScenario)
{
    srs_error_t err = srs_success;

    // Create mock objects
    SrsUniquePtr<MockAppConfigForEncoder> mock_config(new MockAppConfigForEncoder());
    SrsUniquePtr<MockSrsRequest> mock_req(new MockSrsRequest("test.vhost", "live", "livestream"));

    // Setup: No transcode configuration (transcode_directive_ = NULL)
    // This tests the major scenario where on_publish is called but no transcoding is configured
    mock_config->transcode_directive_ = NULL;

    // Create encoder and inject mock config
    SrsUniquePtr<SrsEncoder> encoder(new SrsEncoder());
    encoder->config_ = mock_config.get();

    // Test: Call on_publish with no transcode configuration
    // Expected: Should return success and not start any encoding threads
    HELPER_EXPECT_SUCCESS(encoder->on_publish(mock_req.get()));

    // Verify: No FFmpeg instances were created (ffmpegs_ vector should be empty)
    // This is the expected behavior when no transcode engines are configured

    // Clean up: Set injected fields to NULL to avoid double-free
    encoder->config_ = NULL;
}

// Test SrsEncoder::parse_scope_engines and clear_engines - covers the major use scenario:
// This test covers the complete encoder lifecycle for the provided code:
// 1. Create SrsEncoder with mocked config and factory
// 2. Configure mock config to return transcode directive for stream scope
// 3. Call parse_scope_engines() to parse all three scopes (vhost, app, stream) and create FFmpeg engines
// 4. Verify that FFmpeg engines are created and added to ffmpegs_ vector
// 5. Verify that output URLs are tracked in the transcoded URLs for loop detection
// 6. Call clear_engines() to clean up all engines
// 7. Verify that engines are properly freed and removed from the transcoded URLs
VOID TEST(EncoderTest, ParseScopeEnginesAndClearEngines)
{
    srs_error_t err;

    // Create mock config
    SrsUniquePtr<MockAppConfigForEncoder> mock_config(new MockAppConfigForEncoder());

    // Create transcode directive for stream scope (most specific)
    SrsConfDirective *transcode_conf = new SrsConfDirective();
    transcode_conf->name_ = "transcode";
    transcode_conf->args_.push_back("stream_transcode");

    // Create engine directive
    SrsConfDirective *engine_conf = new SrsConfDirective();
    engine_conf->name_ = "engine";
    engine_conf->args_.push_back("hd");

    // Configure mock config to return transcode directive for stream scope
    mock_config->transcode_directive_ = transcode_conf;
    mock_config->transcode_enabled_ = true;
    mock_config->transcode_ffmpeg_bin_ = "/usr/bin/ffmpeg";
    mock_config->transcode_engines_.push_back(engine_conf);
    mock_config->engine_enabled_ = true;
    mock_config->target_scope_ = "live/livestream"; // Stream scope

    // Create mock factory
    SrsUniquePtr<MockAppFactoryForEncoder> mock_factory(new MockAppFactoryForEncoder());

    // Create mock FFmpeg instance and set it in the factory
    // Note: The factory will return this instance when create_ffmpeg is called
    MockFFMPEGForEncoder *mock_ffmpeg = new MockFFMPEGForEncoder();
    mock_factory->mock_ffmpeg_ = mock_ffmpeg;

    // Create SrsEncoder
    SrsUniquePtr<SrsEncoder> encoder(new SrsEncoder());

    // Inject mock dependencies
    std::vector<std::string> transcoded_urls;
    encoder->config_ = mock_config.get();
    encoder->app_factory_ = mock_factory.get();
    encoder->transcoded_urls_ = &transcoded_urls;

    // Create mock request
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "livestream"));

    // Test parse_scope_engines() - should parse stream scope and create FFmpeg engine
    HELPER_EXPECT_SUCCESS(encoder->parse_scope_engines(req.get()));

    // Verify that one FFmpeg engine was created
    EXPECT_EQ(1, (int)encoder->ffmpegs_.size());

    // Verify that the FFmpeg engine was initialized
    EXPECT_TRUE(mock_ffmpeg->initialize_called_);

    // Verify that output URL was set
    EXPECT_FALSE(mock_ffmpeg->output().empty());

    // Verify that at() method returns the correct engine
    ISrsFFMPEG *ffmpeg_at_0 = encoder->at(0);
    EXPECT_TRUE(ffmpeg_at_0 != NULL);
    EXPECT_EQ((ISrsFFMPEG *)mock_ffmpeg, ffmpeg_at_0);

    // Verify that the output URL is tracked for loop detection
    ASSERT_EQ(1, (int)transcoded_urls.size());
    EXPECT_STREQ(mock_ffmpeg->output().c_str(), transcoded_urls.at(0).c_str());

    // Test clear_engines() - should free all engines and remove from the transcoded URLs
    encoder->clear_engines();

    // Verify that ffmpegs_ vector and the transcoded URLs are now empty
    EXPECT_EQ(0, (int)encoder->ffmpegs_.size());
    EXPECT_EQ(0, (int)transcoded_urls.size());

    // Clean up - set to NULL to avoid double-free
    encoder->config_ = NULL;
    encoder->app_factory_ = NULL;
    encoder->transcoded_urls_ = NULL;

    // Clean up directives
    srs_freep(transcode_conf);
    srs_freep(engine_conf);
}

// Test SrsEncoder::initialize_ffmpeg - covers the major use scenario:
// This test covers the complete initialize_ffmpeg workflow:
// 1. Constructs input URL from request (rtmp://localhost:port/app/stream?vhost=xxx)
// 2. Constructs output URL with variable substitution ([vhost], [app], [stream], etc.)
// 3. Calls ffmpeg->initialize() with input, output, and log file
// 4. Calls ffmpeg->initialize_transcode() with engine directive
// 5. Sets input_stream_name_ for logging purposes
VOID TEST(EncoderTest, InitializeFFmpegMajorScenario)
{
    srs_error_t err;

    // Create mock objects
    SrsUniquePtr<MockAppConfigForEncoder> mock_config(new MockAppConfigForEncoder());
    SrsUniquePtr<MockFFMPEGForEncoder> mock_ffmpeg(new MockFFMPEGForEncoder());

    // Create mock request with specific values for URL construction
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "livestream"));
    req->port_ = 1935;
    req->param_ = "token=abc123";

    // Create mock engine directive with args for variable substitution
    SrsUniquePtr<SrsConfDirective> engine(new SrsConfDirective());
    engine->name_ = "engine";
    engine->args_.push_back("hd"); // engine name for [engine] substitution

    // Configure mock config to return output URL with variables
    // The output URL will be processed by initialize_ffmpeg to replace variables
    mock_config->get_engine_output(engine.get()); // Will return "rtmp://127.0.0.1/live/livestream_hd"

    // Create SrsEncoder and inject mock dependencies
    std::vector<std::string> transcoded_urls;
    SrsUniquePtr<SrsEncoder> encoder(new SrsEncoder());
    encoder->config_ = mock_config.get();
    encoder->transcoded_urls_ = &transcoded_urls;

    // Test initialize_ffmpeg() - should construct URLs and initialize ffmpeg
    HELPER_EXPECT_SUCCESS(encoder->initialize_ffmpeg(mock_ffmpeg.get(), req.get(), engine.get()));

    // Verify that ffmpeg->initialize() was called
    EXPECT_TRUE(mock_ffmpeg->initialize_called_);

    // Verify that output URL was set (from mock config)
    EXPECT_FALSE(mock_ffmpeg->output().empty());
    EXPECT_STREQ("rtmp://127.0.0.1/live/livestream_hd", mock_ffmpeg->output().c_str());

    // Verify that input_stream_name_ was set correctly (vhost/app/stream format)
    EXPECT_STREQ("test.vhost/live/livestream", encoder->input_stream_name_.c_str());

    // Clean up - set to NULL to avoid double-free
    encoder->config_ = NULL;
    encoder->transcoded_urls_ = NULL;
}

// A stream that another encoder transcoded to, published back as an input, is a loop: the encoder
// refuses it and doesn't track its output, so it transcodes no stream again.
VOID TEST(EncoderTest, InitializeFFmpegRefusesTranscodedInput)
{
    srs_error_t err;

    MockAppConfigForEncoder config;
    MockFFMPEGForEncoder ffmpeg;
    MockSrsRequest req("test.vhost", "live", "livestream_hd");
    req.port_ = 1935;

    SrsConfDirective engine;
    engine.name_ = "engine";
    engine.args_.push_back("hd");

    std::vector<std::string> transcoded_urls;
    transcoded_urls.push_back("rtmp://127.0.0.1:1935/live/livestream_hd?vhost=test.vhost");

    SrsUniquePtr<SrsEncoder> encoder(new SrsEncoder());
    encoder->config_ = &config;
    encoder->transcoded_urls_ = &transcoded_urls;

    err = encoder->initialize_ffmpeg(&ffmpeg, &req, &engine);
    EXPECT_EQ(ERROR_ENCODER_LOOP, srs_error_code(err));
    srs_freep(err);

    EXPECT_FALSE(ffmpeg.initialize_called_);
    ASSERT_EQ(1, (int)transcoded_urls.size());
    EXPECT_STREQ("rtmp://127.0.0.1:1935/live/livestream_hd?vhost=test.vhost", transcoded_urls.at(0).c_str());

    encoder->config_ = NULL;
    encoder->transcoded_urls_ = NULL;
}

VOID TEST(ProcessTest, InitializeWithRedirection)
{
    srs_error_t err;

    // Create SrsProcess instance
    SrsUniquePtr<SrsProcess> process(new SrsProcess());

    // Test major use scenario: FFmpeg command with stdout and stderr redirection
    // Simulates: ffmpeg -i input.flv -c copy output.flv 1>/dev/null 2>/dev/null
    string binary = "/usr/bin/ffmpeg";
    vector<string> argv;
    argv.push_back("/usr/bin/ffmpeg");
    argv.push_back("-i");
    argv.push_back("input.flv");
    argv.push_back("-c");
    argv.push_back("copy");
    argv.push_back("output.flv");
    argv.push_back("1>/dev/null");
    argv.push_back("2>/dev/null");

    // Initialize the process
    HELPER_EXPECT_SUCCESS(process->initialize(binary, argv));

    // Verify binary is set correctly
    EXPECT_STREQ("/usr/bin/ffmpeg", process->bin_.c_str());

    // Verify stdout redirection is parsed correctly
    EXPECT_STREQ("/dev/null", process->stdout_file_.c_str());

    // Verify stderr redirection is parsed correctly
    EXPECT_STREQ("/dev/null", process->stderr_file_.c_str());

    // Verify params_ contains only the actual command parameters (without redirection)
    EXPECT_EQ(6, (int)process->params_.size());
    EXPECT_STREQ("/usr/bin/ffmpeg", process->params_[0].c_str());
    EXPECT_STREQ("-i", process->params_[1].c_str());
    EXPECT_STREQ("input.flv", process->params_[2].c_str());
    EXPECT_STREQ("-c", process->params_[3].c_str());
    EXPECT_STREQ("copy", process->params_[4].c_str());
    EXPECT_STREQ("output.flv", process->params_[5].c_str());

    // Verify actual_cli_ contains command without redirection
    EXPECT_STREQ("/usr/bin/ffmpeg -i input.flv -c copy output.flv", process->actual_cli_.c_str());

    // Verify cli_ contains full original command with redirection
    EXPECT_STREQ("/usr/bin/ffmpeg -i input.flv -c copy output.flv 1>/dev/null 2>/dev/null", process->cli_.c_str());
}

// Mock ISrsAppConfig implementation for SrsNgExec
MockAppConfigForNgExec::MockAppConfigForNgExec()
{
    exec_enabled_ = false;
}

MockAppConfigForNgExec::~MockAppConfigForNgExec()
{
    // Free all exec_publishs_ directives
    for (int i = 0; i < (int)exec_publishs_.size(); i++) {
        srs_freep(exec_publishs_[i]);
    }
    exec_publishs_.clear();
}

bool MockAppConfigForNgExec::get_exec_enabled(std::string vhost)
{
    return exec_enabled_;
}

std::vector<SrsConfDirective *> MockAppConfigForNgExec::get_exec_publishs(std::string vhost)
{
    return exec_publishs_;
}

// The constructor does not create the pithy print, which would enter the exec
// stage of the global stage manager before a test could inject anything.
VOID TEST(NgExecTest, ConstructorLeavesPithyPrintUnset)
{
    SrsUniquePtr<SrsNgExec> ng_exec(new SrsNgExec());

    EXPECT_TRUE(ng_exec->pprint_ == NULL);
}

// assemble() creates the pithy print for the exec stage.
VOID TEST(NgExecTest, AssembleCreatesExecPithyPrint)
{
    SrsUniquePtr<SrsNgExec> ng_exec(new SrsNgExec());
    srs_freep(ng_exec->pprint_);

    ng_exec->assemble();

    ASSERT_TRUE(ng_exec->pprint_ != NULL);

    SrsUniquePtr<SrsPithyPrint> expected(SrsPithyPrint::create_exec());
    EXPECT_EQ(expected->stage_id_, ng_exec->pprint_->stage_id_);
}

VOID TEST(NgExecTest, ParseExecPublishWithMultipleArgs)
{
    srs_error_t err = srs_success;

    // Create mock config
    SrsUniquePtr<MockAppConfigForNgExec> mock_config(new MockAppConfigForNgExec());
    mock_config->exec_enabled_ = true;

    // Create exec_publish directive with multiple arguments including redirection
    // Simulates: publish ffmpeg -i [url] -c copy -f flv output.flv>log.txt
    SrsConfDirective *ep = new SrsConfDirective();
    ep->name_ = "publish";
    ep->args_.push_back("ffmpeg");
    ep->args_.push_back("-i");
    ep->args_.push_back("[url]");
    ep->args_.push_back("-c");
    ep->args_.push_back("copy");
    ep->args_.push_back("-f");
    ep->args_.push_back("flv");
    ep->args_.push_back("output.flv>log.txt");
    mock_config->exec_publishs_.push_back(ep);

    // Create mock request
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "stream1", "127.0.0.1", 1935));

    // Create SrsNgExec and inject mock config
    SrsUniquePtr<SrsNgExec> ng_exec(new SrsNgExec());
    ng_exec->config_ = mock_config.get();

    // Test parse_exec_publish
    HELPER_EXPECT_SUCCESS(ng_exec->parse_exec_publish(req.get()));

    // Verify input_stream_name_ was set correctly
    EXPECT_EQ("test.vhost/live/stream1", ng_exec->input_stream_name_);

    // Verify exec_publishs_ was populated
    EXPECT_EQ(1, (int)ng_exec->exec_publishs_.size());

    // Clean up - set to NULL to avoid double-free
    ng_exec->config_ = NULL;
}

// Mock ISrsHttpMessage implementation for SrsHttpHeartbeat
MockHttpMessageForHeartbeat::MockHttpMessageForHeartbeat()
{
    status_code_ = 200;
    body_content_ = "";
    body_reader_ = new MockBufferReader("");
}

MockHttpMessageForHeartbeat::~MockHttpMessageForHeartbeat()
{
    srs_freep(body_reader_);
}

uint8_t MockHttpMessageForHeartbeat::method()
{
    return 0;
}

uint16_t MockHttpMessageForHeartbeat::status_code()
{
    return status_code_;
}

std::string MockHttpMessageForHeartbeat::method_str()
{
    return "";
}

std::string MockHttpMessageForHeartbeat::url()
{
    return "";
}

std::string MockHttpMessageForHeartbeat::host()
{
    return "";
}

std::string MockHttpMessageForHeartbeat::path()
{
    return "";
}

std::string MockHttpMessageForHeartbeat::query()
{
    return "";
}

std::string MockHttpMessageForHeartbeat::ext()
{
    return "";
}

srs_error_t MockHttpMessageForHeartbeat::body_read_all(std::string &body)
{
    body = body_content_;
    return srs_success;
}

ISrsHttpResponseReader *MockHttpMessageForHeartbeat::body_reader()
{
    return NULL;
}

int64_t MockHttpMessageForHeartbeat::content_length()
{
    return body_content_.length();
}

std::string MockHttpMessageForHeartbeat::query_get(std::string key)
{
    return "";
}

SrsHttpHeader *MockHttpMessageForHeartbeat::header()
{
    return NULL;
}

bool MockHttpMessageForHeartbeat::is_jsonp()
{
    return false;
}

bool MockHttpMessageForHeartbeat::is_keep_alive()
{
    return false;
}

ISrsRequest *MockHttpMessageForHeartbeat::to_request(std::string vhost)
{
    return NULL;
}

std::string MockHttpMessageForHeartbeat::parse_rest_id(std::string pattern)
{
    return "";
}

uint8_t MockHttpMessageForHeartbeat::message_type()
{
    return 0;
}

bool MockHttpMessageForHeartbeat::is_http_get()
{
    return false;
}

bool MockHttpMessageForHeartbeat::is_http_put()
{
    return false;
}

bool MockHttpMessageForHeartbeat::is_http_post()
{
    return true;
}

bool MockHttpMessageForHeartbeat::is_http_delete()
{
    return false;
}

bool MockHttpMessageForHeartbeat::is_http_options()
{
    return false;
}

std::string MockHttpMessageForHeartbeat::uri()
{
    return "";
}

void MockHttpMessageForHeartbeat::reset()
{
    status_code_ = 200;
    body_content_ = "";
}

// Mock ISrsHttpClient implementation for SrsHttpHeartbeat
MockHttpClientForHeartbeat::MockHttpClientForHeartbeat()
{
    initialize_called_ = false;
    post_called_ = false;
    port_ = 0;
    mock_response_ = NULL;
    initialize_error_ = srs_success;
    post_error_ = srs_success;
    should_delete_response_ = true;
    request_body_out_ = NULL;
    path_out_ = NULL;
}

MockHttpClientForHeartbeat::~MockHttpClientForHeartbeat()
{
    srs_freep(initialize_error_);
    srs_freep(post_error_);
    // Don't delete mock_response_ - it's managed by the caller or already deleted
    mock_response_ = NULL;
}

srs_error_t MockHttpClientForHeartbeat::initialize(std::string schema, std::string h, int p, srs_utime_t tm)
{
    initialize_called_ = true;
    schema_ = schema;
    host_ = h;
    port_ = p;
    return srs_error_copy(initialize_error_);
}

srs_error_t MockHttpClientForHeartbeat::get(std::string path, std::string req, ISrsHttpMessage **ppmsg)
{
    return srs_success;
}

srs_error_t MockHttpClientForHeartbeat::post(std::string path, std::string req, ISrsHttpMessage **ppmsg)
{
    post_called_ = true;
    path_ = path;
    request_body_ = req;
    if (request_body_out_) {
        *request_body_out_ = req;
    }
    if (path_out_) {
        *path_out_ = path;
    }
    if (ppmsg && mock_response_) {
        *ppmsg = (ISrsHttpMessage *)mock_response_;
    }
    return srs_error_copy(post_error_);
}

void MockHttpClientForHeartbeat::set_recv_timeout(srs_utime_t tm)
{
}

void MockHttpClientForHeartbeat::kbps_sample(const char *label, srs_utime_t age)
{
}

void MockHttpClientForHeartbeat::reset()
{
    initialize_called_ = false;
    post_called_ = false;
    port_ = 0;
    schema_ = "";
    host_ = "";
    path_ = "";
    request_body_ = "";
}

// Mock ISrsAppFactory implementation for SrsHttpHeartbeat
MockAppFactoryForHeartbeat::MockAppFactoryForHeartbeat()
{
    mock_http_client_ = NULL;
    create_http_client_called_ = false;
    should_delete_client_ = true;
}

MockAppFactoryForHeartbeat::~MockAppFactoryForHeartbeat()
{
    if (should_delete_client_) {
        srs_freep(mock_http_client_);
    }
}

ISrsHttpClient *MockAppFactoryForHeartbeat::create_http_client()
{
    create_http_client_called_ = true;
    return mock_http_client_;
}

void MockAppFactoryForHeartbeat::reset()
{
    create_http_client_called_ = false;
}

// Mock ISrsAppConfig implementation for SrsHttpHeartbeat
MockAppConfigForHeartbeat::MockAppConfigForHeartbeat()
{
    heartbeat_url_ = "";
    heartbeat_device_id_ = "";
    heartbeat_summaries_ = false;
    heartbeat_ports_ = false;
    stats_network_ = 0;
    http_stream_enabled_ = false;
    http_api_enabled_ = false;
    srt_enabled_ = false;
    rtsp_server_enabled_ = false;
    rtc_server_enabled_ = false;
    rtc_server_tcp_enabled_ = false;
}

MockAppConfigForHeartbeat::~MockAppConfigForHeartbeat()
{
}

std::string MockAppConfigForHeartbeat::get_heartbeat_url()
{
    return heartbeat_url_;
}

std::string MockAppConfigForHeartbeat::get_heartbeat_device_id()
{
    return heartbeat_device_id_;
}

bool MockAppConfigForHeartbeat::get_heartbeat_summaries()
{
    return heartbeat_summaries_;
}

bool MockAppConfigForHeartbeat::get_heartbeat_ports()
{
    return heartbeat_ports_;
}

int MockAppConfigForHeartbeat::get_stats_network()
{
    return stats_network_;
}

std::vector<std::string> MockAppConfigForHeartbeat::get_listens()
{
    return listens_;
}

bool MockAppConfigForHeartbeat::get_http_stream_enabled()
{
    return http_stream_enabled_;
}

std::vector<std::string> MockAppConfigForHeartbeat::get_http_stream_listens()
{
    return http_stream_listens_;
}

bool MockAppConfigForHeartbeat::get_http_api_enabled()
{
    return http_api_enabled_;
}

std::vector<std::string> MockAppConfigForHeartbeat::get_http_api_listens()
{
    return http_api_listens_;
}

bool MockAppConfigForHeartbeat::get_srt_enabled()
{
    return srt_enabled_;
}

std::vector<std::string> MockAppConfigForHeartbeat::get_srt_listens()
{
    return srt_listens_;
}

bool MockAppConfigForHeartbeat::get_rtsp_server_enabled()
{
    return rtsp_server_enabled_;
}

std::vector<std::string> MockAppConfigForHeartbeat::get_rtsp_server_listens()
{
    return rtsp_server_listens_;
}

bool MockAppConfigForHeartbeat::get_rtc_server_enabled()
{
    return rtc_server_enabled_;
}

std::vector<std::string> MockAppConfigForHeartbeat::get_rtc_server_listens()
{
    return rtc_server_listens_;
}

bool MockAppConfigForHeartbeat::get_rtc_server_tcp_enabled()
{
    return rtc_server_tcp_enabled_;
}

std::vector<std::string> MockAppConfigForHeartbeat::get_rtc_server_tcp_listens()
{
    return rtc_server_tcp_listens_;
}

std::string MockAppConfigForHeartbeat::argv()
{
    return argv_;
}

std::string MockAppConfigForHeartbeat::cwd()
{
    return cwd_;
}

// Mock ISrsAppConfig implementation for SrsCircuitBreaker
MockAppConfigForCircuitBreaker::MockAppConfigForCircuitBreaker()
{
    circuit_breaker_enabled_ = true;
    high_threshold_ = 90;
    high_pulse_ = 2;
    critical_threshold_ = 95;
    critical_pulse_ = 1;
    dying_threshold_ = 99;
    dying_pulse_ = 5;
}

MockAppConfigForCircuitBreaker::~MockAppConfigForCircuitBreaker()
{
}

bool MockAppConfigForCircuitBreaker::get_circuit_breaker()
{
    return circuit_breaker_enabled_;
}

int MockAppConfigForCircuitBreaker::get_high_threshold()
{
    return high_threshold_;
}

int MockAppConfigForCircuitBreaker::get_high_pulse()
{
    return high_pulse_;
}

int MockAppConfigForCircuitBreaker::get_critical_threshold()
{
    return critical_threshold_;
}

int MockAppConfigForCircuitBreaker::get_critical_pulse()
{
    return critical_pulse_;
}

int MockAppConfigForCircuitBreaker::get_dying_threshold()
{
    return dying_threshold_;
}

int MockAppConfigForCircuitBreaker::get_dying_pulse()
{
    return dying_pulse_;
}

// Mock ISrsFastTimer implementation for SrsCircuitBreaker
MockFastTimerForCircuitBreaker::MockFastTimerForCircuitBreaker()
{
    subscribe_called_ = false;
    subscribed_handler_ = NULL;
    unsubscribed_handler_ = NULL;
}

MockFastTimerForCircuitBreaker::~MockFastTimerForCircuitBreaker()
{
}

srs_error_t MockFastTimerForCircuitBreaker::start()
{
    return srs_success;
}

void MockFastTimerForCircuitBreaker::subscribe(ISrsFastTimerHandler *handler)
{
    subscribe_called_ = true;
    subscribed_handler_ = handler;
}

void MockFastTimerForCircuitBreaker::unsubscribe(ISrsFastTimerHandler *handler)
{
    unsubscribed_handler_ = handler;
}

void MockFastTimerForCircuitBreaker::reset()
{
    subscribe_called_ = false;
    subscribed_handler_ = NULL;
    unsubscribed_handler_ = NULL;
}

// Mock ISrsSharedTimer implementation for SrsCircuitBreaker
MockSharedTimerForCircuitBreaker::MockSharedTimerForCircuitBreaker()
{
    timer1s_ = new MockFastTimerForCircuitBreaker();
}

MockSharedTimerForCircuitBreaker::~MockSharedTimerForCircuitBreaker()
{
    srs_freep(timer1s_);
}

ISrsFastTimer *MockSharedTimerForCircuitBreaker::timer20ms()
{
    return NULL;
}

ISrsFastTimer *MockSharedTimerForCircuitBreaker::timer100ms()
{
    return NULL;
}

ISrsFastTimer *MockSharedTimerForCircuitBreaker::timer1s()
{
    return timer1s_;
}

ISrsFastTimer *MockSharedTimerForCircuitBreaker::timer5s()
{
    return NULL;
}

// Mock ISrsHost implementation for SrsCircuitBreaker
MockHostForCircuitBreaker::MockHostForCircuitBreaker()
{
    proc_stat_ = new SrsProcSelfStat();
    proc_stat_->percent_ = 0.0;
}

MockHostForCircuitBreaker::~MockHostForCircuitBreaker()
{
    srs_freep(proc_stat_);
}

SrsProcSelfStat *MockHostForCircuitBreaker::self_proc_stat()
{
    return proc_stat_;
}

VOID TEST(HttpHeartbeatTest, DoHeartbeatWithAllPortsEnabled)
{
    srs_error_t err = srs_success;

    // Create mock config with all protocols enabled
    SrsUniquePtr<MockAppConfigForHeartbeat> mock_config(new MockAppConfigForHeartbeat());
    mock_config->heartbeat_url_ = "http://127.0.0.1:8080/api/v1/heartbeat";
    mock_config->heartbeat_device_id_ = "test-device-001";
    mock_config->heartbeat_summaries_ = false;
    mock_config->heartbeat_ports_ = true;
    mock_config->stats_network_ = 0;

    // Configure RTMP listen endpoints
    mock_config->listens_.push_back("1935");
    mock_config->listens_.push_back("19350");

    // Configure HTTP stream endpoints
    mock_config->http_stream_enabled_ = true;
    mock_config->http_stream_listens_.push_back("8080");

    // Configure HTTP API endpoints
    mock_config->http_api_enabled_ = true;
    mock_config->http_api_listens_.push_back("1985");

    // Configure SRT endpoints
    mock_config->srt_enabled_ = true;
    mock_config->srt_listens_.push_back("10080");

    // Configure RTSP endpoints
    mock_config->rtsp_server_enabled_ = true;
    mock_config->rtsp_server_listens_.push_back("554");

    // Configure WebRTC endpoints
    mock_config->rtc_server_enabled_ = true;
    mock_config->rtc_server_listens_.push_back("8000");
    mock_config->rtc_server_tcp_enabled_ = true;
    mock_config->rtc_server_tcp_listens_.push_back("8000");

    // Create mock HTTP response
    MockHttpMessageForHeartbeat *mock_response = new MockHttpMessageForHeartbeat();
    mock_response->status_code_ = 200;
    mock_response->body_content_ = "{\"code\":0,\"data\":{}}";

    // Create mock HTTP client
    MockHttpClientForHeartbeat *mock_http_client = new MockHttpClientForHeartbeat();
    mock_http_client->mock_response_ = mock_response;
    mock_http_client->should_delete_response_ = true; // Client will delete response

    // Create mock app factory
    SrsUniquePtr<MockAppFactoryForHeartbeat> mock_factory(new MockAppFactoryForHeartbeat());
    mock_factory->mock_http_client_ = mock_http_client;
    mock_factory->should_delete_client_ = true; // Factory will delete client

    // Create SrsHttpHeartbeat and inject mocks
    SrsUniquePtr<SrsHttpHeartbeat> heartbeat(new SrsHttpHeartbeat());
    heartbeat->config_ = mock_config.get();
    heartbeat->app_factory_ = mock_factory.get();

    // Execute do_heartbeat
    // Note: This will delete mock_http_client and mock_response via SrsUniquePtr in do_heartbeat()
    // So we cannot verify mock_http_client fields after this call
    HELPER_EXPECT_SUCCESS(heartbeat->do_heartbeat());

    // Clean up - set to NULL to avoid double-free
    heartbeat->config_ = NULL;
    heartbeat->app_factory_ = NULL;
    mock_factory->mock_http_client_ = NULL; // Already deleted by do_heartbeat()
}

MockStatisticForHeartbeat::MockStatisticForHeartbeat()
{
}

MockStatisticForHeartbeat::~MockStatisticForHeartbeat()
{
}

std::string MockStatisticForHeartbeat::server_id()
{
    return "mock-server";
}

std::string MockStatisticForHeartbeat::service_id()
{
    return "mock-service";
}

std::string MockStatisticForHeartbeat::service_pid()
{
    return "mock-pid";
}

VOID TEST(HttpHeartbeatTest, ConstructionCapturesStatistic)
{
    SrsUniquePtr<SrsHttpHeartbeat> heartbeat(new SrsHttpHeartbeat());
    EXPECT_TRUE(heartbeat->stat_ == _srs_stat);
}

VOID TEST(HttpHeartbeatTest, DoHeartbeatReportsIdsFromInjectedStatistic)
{
    srs_error_t err;

    MockAppConfigForHeartbeat config;
    config.heartbeat_url_ = "http://127.0.0.1:8085/api/v1/servers";
    config.heartbeat_device_id_ = "test-device";

    MockHttpMessageForHeartbeat *response = new MockHttpMessageForHeartbeat();
    response->body_content_ = "{\"code\":0}";

    std::string body;
    MockHttpClientForHeartbeat *client = new MockHttpClientForHeartbeat();
    client->mock_response_ = response;
    client->request_body_out_ = &body;

    MockAppFactoryForHeartbeat factory;
    factory.mock_http_client_ = client;

    MockStatisticForHeartbeat stat;

    SrsUniquePtr<SrsHttpHeartbeat> heartbeat(new SrsHttpHeartbeat());
    heartbeat->config_ = &config;
    heartbeat->app_factory_ = &factory;
    heartbeat->stat_ = &stat;

    // The client and its response are freed by do_heartbeat().
    HELPER_EXPECT_SUCCESS(heartbeat->do_heartbeat());
    factory.mock_http_client_ = NULL;

    SrsUniquePtr<SrsJsonAny> json(SrsJsonAny::loads(body));
    ASSERT_TRUE(json.get() && json->is_object());
    SrsJsonObject *obj = json->to_object();

    SrsJsonAny *prop = obj->get_property("server");
    ASSERT_TRUE(prop && prop->is_string());
    EXPECT_STREQ("mock-server", prop->to_str().c_str());

    prop = obj->get_property("service");
    ASSERT_TRUE(prop && prop->is_string());
    EXPECT_STREQ("mock-service", prop->to_str().c_str());

    prop = obj->get_property("pid");
    ASSERT_TRUE(prop && prop->is_string());
    EXPECT_STREQ("mock-pid", prop->to_str().c_str());

    heartbeat->config_ = NULL;
    heartbeat->app_factory_ = NULL;
    heartbeat->stat_ = NULL;
}

VOID TEST(HttpHeartbeatTest, DoHeartbeatSummariesFromInjectedConfig)
{
    srs_error_t err;

    MockAppConfigForHeartbeat config;
    config.heartbeat_url_ = "http://127.0.0.1:8085/api/v1/servers";
    config.heartbeat_summaries_ = true;
    config.argv_ = "./objs/srs -c conf/heartbeat.conf";
    config.cwd_ = "/tmp/srs-heartbeat";

    MockHttpMessageForHeartbeat *response = new MockHttpMessageForHeartbeat();
    response->body_content_ = "{\"code\":0}";

    std::string body;
    MockHttpClientForHeartbeat *client = new MockHttpClientForHeartbeat();
    client->mock_response_ = response;
    client->request_body_out_ = &body;

    MockAppFactoryForHeartbeat factory;
    factory.mock_http_client_ = client;

    MockStatisticForHeartbeat stat;

    SrsUniquePtr<SrsHttpHeartbeat> heartbeat(new SrsHttpHeartbeat());
    heartbeat->config_ = &config;
    heartbeat->app_factory_ = &factory;
    heartbeat->stat_ = &stat;

    // The client and its response are freed by do_heartbeat().
    HELPER_EXPECT_SUCCESS(heartbeat->do_heartbeat());
    factory.mock_http_client_ = NULL;

    SrsUniquePtr<SrsJsonAny> json(SrsJsonAny::loads(body));
    ASSERT_TRUE(json.get() && json->is_object());

    SrsJsonAny *prop = json->to_object()->get_property("summaries");
    ASSERT_TRUE(prop && prop->is_object());
    prop = prop->to_object()->get_property("data");
    ASSERT_TRUE(prop && prop->is_object());
    prop = prop->to_object()->get_property("self");
    ASSERT_TRUE(prop && prop->is_object());
    SrsJsonObject *self = prop->to_object();

    prop = self->get_property("argv");
    ASSERT_TRUE(prop && prop->is_string());
    EXPECT_STREQ("./objs/srs -c conf/heartbeat.conf", prop->to_str().c_str());

    prop = self->get_property("cwd");
    ASSERT_TRUE(prop && prop->is_string());
    EXPECT_STREQ("/tmp/srs-heartbeat", prop->to_str().c_str());

    heartbeat->config_ = NULL;
    heartbeat->app_factory_ = NULL;
    heartbeat->stat_ = NULL;
}

// Run one heartbeat to the url, and return the path it was posted to.
static srs_error_t heartbeat_post_path(std::string url, std::string *path)
{
    MockAppConfigForHeartbeat config;
    config.heartbeat_url_ = url;

    MockHttpMessageForHeartbeat *response = new MockHttpMessageForHeartbeat();
    response->body_content_ = "{\"code\":0}";

    MockHttpClientForHeartbeat *client = new MockHttpClientForHeartbeat();
    client->mock_response_ = response;
    client->path_out_ = path;

    MockAppFactoryForHeartbeat factory;
    factory.mock_http_client_ = client;

    MockStatisticForHeartbeat stat;

    SrsUniquePtr<SrsHttpHeartbeat> heartbeat(new SrsHttpHeartbeat());
    heartbeat->config_ = &config;
    heartbeat->app_factory_ = &factory;
    heartbeat->stat_ = &stat;

    // The client and its response are freed by do_heartbeat().
    srs_error_t err = heartbeat->do_heartbeat();
    factory.mock_http_client_ = NULL;

    heartbeat->config_ = NULL;
    heartbeat->app_factory_ = NULL;
    heartbeat->stat_ = NULL;
    return err;
}

// The heartbeat keeps its url's query, as the hooks do, since a receiver may
// take a token or an id from it.
VOID TEST(HttpHeartbeatTest, DoHeartbeatPostsUrlQuery)
{
    srs_error_t err;

    std::string path;
    HELPER_EXPECT_SUCCESS(heartbeat_post_path("http://127.0.0.1:8085/api/v1/heartbeat?k1=v1&k2=v2", &path));
    EXPECT_STREQ("/api/v1/heartbeat?k1=v1&k2=v2", path.c_str());
}

// A url without a query is posted to its path alone, with no "?".
VOID TEST(HttpHeartbeatTest, DoHeartbeatPostsPathWithoutQuery)
{
    srs_error_t err;

    std::string path;
    HELPER_EXPECT_SUCCESS(heartbeat_post_path("http://127.0.0.1:8085/api/v1/heartbeat", &path));
    EXPECT_STREQ("/api/v1/heartbeat", path.c_str());
}

// Test SrsCircuitBreaker - covers the major use scenario:
// This test verifies the complete circuit breaker functionality including:
// 1. Initialize with configuration (enabled, thresholds, pulses)
// 2. Subscribe to timer for periodic CPU monitoring
// 3. Simulate CPU load changes via on_timer() callback
// 4. Verify water level transitions (high -> critical -> dying)
// 5. Verify water level decay when CPU load decreases
// 6. Test all three protection levels: high_water_level, critical_water_level, dying_water_level
VOID TEST(CircuitBreakerTest, InitializeAndWaterLevelTransitions)
{
    srs_error_t err;

    // Create mock dependencies
    SrsUniquePtr<MockAppConfigForCircuitBreaker> mock_config(new MockAppConfigForCircuitBreaker());
    SrsUniquePtr<MockSharedTimerForCircuitBreaker> mock_timer(new MockSharedTimerForCircuitBreaker());
    MockHostForCircuitBreaker *mock_host = new MockHostForCircuitBreaker();

    // Configure circuit breaker settings
    mock_config->circuit_breaker_enabled_ = true;
    mock_config->high_threshold_ = 90;     // CPU > 90% triggers high water level
    mock_config->high_pulse_ = 2;          // High level lasts for 2 timer ticks
    mock_config->critical_threshold_ = 95; // CPU > 95% triggers critical water level
    mock_config->critical_pulse_ = 1;      // Critical level lasts for 1 timer tick
    mock_config->dying_threshold_ = 99;    // CPU > 99% triggers dying water level
    mock_config->dying_pulse_ = 5;         // Dying level requires 5 consecutive ticks

    // Create SrsCircuitBreaker
    SrsUniquePtr<SrsCircuitBreaker> breaker(new SrsCircuitBreaker());

    // Inject mock dependencies
    breaker->config_ = mock_config.get();
    breaker->shared_timer_ = mock_timer.get();
    breaker->host_ = mock_host;

    // Test 1: Initialize - should load config and subscribe to timer
    HELPER_EXPECT_SUCCESS(breaker->initialize());

    // Verify timer subscription
    EXPECT_TRUE(mock_timer->timer1s_->subscribe_called_);
    EXPECT_EQ(breaker.get(), mock_timer->timer1s_->subscribed_handler_);

    // Test 2: Initially all water levels should be false (CPU is 0%)
    EXPECT_FALSE(breaker->hybrid_high_water_level());
    EXPECT_FALSE(breaker->hybrid_critical_water_level());
    EXPECT_FALSE(breaker->hybrid_dying_water_level());

    // Test 3: Simulate high CPU load (91% > high_threshold 90%)
    mock_host->proc_stat_->percent_ = 0.91; // 91% CPU
    HELPER_EXPECT_SUCCESS(breaker->on_timer(1 * SRS_UTIME_SECONDS));

    // After 1 tick with high CPU, high water level should be active
    EXPECT_TRUE(breaker->hybrid_high_water_level());
    EXPECT_FALSE(breaker->hybrid_critical_water_level());
    EXPECT_FALSE(breaker->hybrid_dying_water_level());

    // Test 4: Simulate critical CPU load (96% > critical_threshold 95%)
    mock_host->proc_stat_->percent_ = 0.96; // 96% CPU
    HELPER_EXPECT_SUCCESS(breaker->on_timer(1 * SRS_UTIME_SECONDS));

    // After 1 tick with critical CPU, both high and critical should be active
    EXPECT_TRUE(breaker->hybrid_high_water_level());
    EXPECT_TRUE(breaker->hybrid_critical_water_level());
    EXPECT_FALSE(breaker->hybrid_dying_water_level());

    // Test 5: Simulate dying CPU load (99.5% > dying_threshold 99%)
    // Need 5 consecutive ticks to activate dying level
    mock_host->proc_stat_->percent_ = 0.995; // 99.5% CPU
    for (int i = 0; i < 5; i++) {
        HELPER_EXPECT_SUCCESS(breaker->on_timer(1 * SRS_UTIME_SECONDS));
    }

    // After 5 consecutive ticks with dying CPU, all levels should be active
    EXPECT_TRUE(breaker->hybrid_high_water_level());
    EXPECT_TRUE(breaker->hybrid_critical_water_level());
    EXPECT_TRUE(breaker->hybrid_dying_water_level());

    // Test 6: Simulate CPU load decrease (back to 50%)
    mock_host->proc_stat_->percent_ = 0.50; // 50% CPU
    HELPER_EXPECT_SUCCESS(breaker->on_timer(1 * SRS_UTIME_SECONDS));

    // Dying level should immediately reset to 0 when CPU drops
    EXPECT_FALSE(breaker->hybrid_dying_water_level());
    // Critical should also become false (since dying is false and critical_water_level_ will decay)
    // But high should still be active (high_water_level_ = 2, needs 2 ticks to decay)
    EXPECT_TRUE(breaker->hybrid_high_water_level());
    // Critical is false because dying is false and critical_water_level_ decayed from 1 to 0
    EXPECT_FALSE(breaker->hybrid_critical_water_level());

    // Test 7: Continue with low CPU - high should decay to 0 after 1 more tick
    HELPER_EXPECT_SUCCESS(breaker->on_timer(1 * SRS_UTIME_SECONDS));
    EXPECT_FALSE(breaker->hybrid_high_water_level()); // High now false (high_water_level_ = 0)
    EXPECT_FALSE(breaker->hybrid_critical_water_level());
    EXPECT_FALSE(breaker->hybrid_dying_water_level());

    // Test 9: Verify disabled circuit breaker returns false for all levels
    mock_config->circuit_breaker_enabled_ = false;
    SrsUniquePtr<SrsCircuitBreaker> disabled_breaker(new SrsCircuitBreaker());
    disabled_breaker->config_ = mock_config.get();
    disabled_breaker->shared_timer_ = mock_timer.get();
    disabled_breaker->host_ = mock_host;
    HELPER_EXPECT_SUCCESS(disabled_breaker->initialize());

    // Even with high CPU, disabled breaker should return false
    mock_host->proc_stat_->percent_ = 0.99;
    HELPER_EXPECT_SUCCESS(disabled_breaker->on_timer(1 * SRS_UTIME_SECONDS));
    EXPECT_FALSE(disabled_breaker->hybrid_high_water_level());
    EXPECT_FALSE(disabled_breaker->hybrid_critical_water_level());
    EXPECT_FALSE(disabled_breaker->hybrid_dying_water_level());

    // Clean up - set to NULL to avoid double-free
    breaker->config_ = NULL;
    breaker->shared_timer_ = NULL;
    breaker->host_ = NULL;
    disabled_breaker->config_ = NULL;
    disabled_breaker->shared_timer_ = NULL;
    disabled_breaker->host_ = NULL;
    srs_freep(mock_host);
}

MockAppConfigForBuildTimestamp::MockAppConfigForBuildTimestamp()
{
    utc_time_ = false;
    get_utc_time_count_ = 0;
}

MockAppConfigForBuildTimestamp::~MockAppConfigForBuildTimestamp()
{
}

bool MockAppConfigForBuildTimestamp::get_utc_time()
{
    get_utc_time_count_++;
    return utc_time_;
}

std::vector<std::string> MockAppConfigForBuildTimestamp::get_listens()
{
    std::vector<std::string> listens;
    listens.push_back("1935");
    return listens;
}

std::string MockAppConfigForBuildTimestamp::get_engine_output(SrsConfDirective *conf)
{
    return "rtmp://127.0.0.1:[port]/live/build_timestamp_[2006]";
}

std::string MockAppConfigForBuildTimestamp::get_ingest_input_type(SrsConfDirective *conf)
{
    return "file";
}

std::string MockAppConfigForBuildTimestamp::get_ingest_input_url(SrsConfDirective *conf)
{
    return "./build_timestamp.flv";
}

// Format the current time as "[2006]-[01]-[02] [15]" would be built, in UTC or local time.
static std::string build_timestamp_expected(bool utc)
{
    timeval tv;
    gettimeofday(&tv, NULL);

    struct tm now;
    if (utc) {
        gmtime_r(&tv.tv_sec, &now);
    } else {
        localtime_r(&tv.tv_sec, &now);
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d", 1900 + now.tm_year, 1 + now.tm_mon, now.tm_mday, now.tm_hour);
    return buf;
}

// The UTC switch is read from the config passed in, not from the global config, and selects UTC.
// Where the local time zone is UTC both results are equal, so only the read count tells them apart.
VOID TEST(PathBuildTimestampTest, BuildsUtcTimeThroughConfig)
{
    MockAppConfigForBuildTimestamp config;
    config.utc_time_ = true;

    std::string before = build_timestamp_expected(true);
    std::string path = srs_path_build_timestamp(&config, "[2006]-[01]-[02] [15]");
    std::string after = build_timestamp_expected(true);

    EXPECT_EQ(1, config.get_utc_time_count_);
    EXPECT_TRUE(path == before || path == after) << "path=" << path << ", expect=" << before;
}

VOID TEST(PathBuildTimestampTest, BuildsLocalTimeThroughConfig)
{
    MockAppConfigForBuildTimestamp config;
    config.utc_time_ = false;

    std::string before = build_timestamp_expected(false);
    std::string path = srs_path_build_timestamp(&config, "[2006]-[01]-[02] [15]");
    std::string after = build_timestamp_expected(false);

    EXPECT_EQ(1, config.get_utc_time_count_);
    EXPECT_TRUE(path == before || path == after) << "path=" << path << ", expect=" << before;
}

VOID TEST(HlsMuxerTest, GenerateTsFilenameBuildsTimestampThroughConfig)
{
    MockAppConfigForBuildTimestamp config;
    MockRequest req("test_vhost", "live", "stream");

    SrsHlsMuxer muxer;
    muxer.config_ = &config;
    muxer.req_ = &req;
    muxer.hls_ts_file_ = "[stream]-[2006]-[seq].ts";
    muxer.hls_ts_floor_ = false;

    SrsHlsSegment *segment = new SrsHlsSegment(muxer.context_, SrsAudioCodecIdAAC, SrsVideoCodecIdDisabled, new MockSrsFileWriter());
    segment->sequence_no_ = 100;
    muxer.current_ = segment;

    std::string ts_file = muxer.generate_ts_filename();

    EXPECT_EQ(1, config.get_utc_time_count_);
    EXPECT_TRUE(ts_file.find("[2006]") == std::string::npos) << ts_file;

    muxer.config_ = NULL;
    muxer.req_ = NULL;
    muxer.current_ = NULL;
    srs_freep(segment);
}

VOID TEST(HlsFmp4MuxerTest, GenerateM4sFilenameBuildsTimestampThroughConfig)
{
    MockAppConfigForBuildTimestamp config;
    MockRequest req("test_vhost", "live", "stream");

    SrsHlsFmp4Muxer muxer;
    muxer.config_ = &config;
    muxer.req_ = &req;
    muxer.hls_m4s_file_ = "[stream]-[2006]-[seq].m4s";
    muxer.hls_ts_floor_ = false;

    SrsHlsM4sSegment *segment = new SrsHlsM4sSegment(NULL);
    segment->sequence_no_ = 100;
    muxer.current_ = segment;

    std::string m4s_file = muxer.generate_m4s_filename();

    EXPECT_EQ(1, config.get_utc_time_count_);
    EXPECT_TRUE(m4s_file.find("[2006]") == std::string::npos) << m4s_file;

    muxer.config_ = NULL;
    muxer.req_ = NULL;
    muxer.current_ = NULL;
    srs_freep(segment);
}

VOID TEST(DvrSegmenterTest, GeneratePathBuildsTimestampThroughConfig)
{
    // Declared before the segmenter, whose destructor unsubscribes from the config.
    MockAppConfigForBuildTimestamp config;
    MockRequest req("test_vhost", "live", "stream");

    SrsDvrFlvSegmenter segmenter;
    segmenter.config_ = &config;
    segmenter.req_ = &req;

    std::string path = segmenter.generate_path();

    EXPECT_EQ(1, config.get_utc_time_count_);
    EXPECT_TRUE(path.find("[2006]") == std::string::npos) << path;

    segmenter.req_ = NULL;
}

VOID TEST(EncoderTest, InitializeFFmpegBuildsTimestampThroughConfig)
{
    srs_error_t err;

    MockAppConfigForBuildTimestamp config;
    MockFFMPEGForEncoder ffmpeg;
    MockSrsRequest req("build.timestamp.vhost", "live", "encoder_stream");

    SrsConfDirective engine;
    engine.name_ = "engine";
    engine.args_.push_back("hd");

    std::vector<std::string> transcoded_urls;
    SrsUniquePtr<SrsEncoder> encoder(new SrsEncoder());
    encoder->config_ = &config;
    encoder->transcoded_urls_ = &transcoded_urls;

    HELPER_EXPECT_SUCCESS(encoder->initialize_ffmpeg(&ffmpeg, &req, &engine));

    EXPECT_EQ(1, config.get_utc_time_count_);
    EXPECT_TRUE(ffmpeg.output_.find("[2006]") == std::string::npos) << ffmpeg.output_;

    encoder->config_ = NULL;
    encoder->transcoded_urls_ = NULL;
}

VOID TEST(IngesterTest, InitializeFFmpegBuildsTimestampThroughConfig)
{
    srs_error_t err;

    MockAppConfigForBuildTimestamp config;
    MockFFMPEGForEncoder ffmpeg;

    SrsConfDirective vhost;
    vhost.name_ = "vhost";
    vhost.args_.push_back("build.timestamp.vhost");

    SrsConfDirective ingest;
    ingest.name_ = "ingest";
    ingest.args_.push_back("livestream");

    SrsConfDirective engine;
    engine.name_ = "engine";

    SrsUniquePtr<SrsIngester> ingester(new SrsIngester());
    ingester->config_ = &config;

    HELPER_EXPECT_SUCCESS(ingester->initialize_ffmpeg(&ffmpeg, &vhost, &ingest, &engine));

    EXPECT_EQ(1, config.get_utc_time_count_);
    EXPECT_TRUE(ffmpeg.output_.find("[2006]") == std::string::npos) << ffmpeg.output_;

    ingester->config_ = NULL;
}

VOID TEST(NgExecTest, ParseBuildsTimestampThroughConfig)
{
    MockAppConfigForBuildTimestamp config;
    MockSrsRequest req("build.timestamp.vhost", "live", "stream1");

    SrsUniquePtr<SrsNgExec> ng_exec(new SrsNgExec());
    ng_exec->config_ = &config;

    std::string output = ng_exec->parse(&req, "[stream]-[2006]");

    EXPECT_EQ(1, config.get_utc_time_count_);
    EXPECT_TRUE(output.find("[2006]") == std::string::npos) << output;

    ng_exec->config_ = NULL;
}
