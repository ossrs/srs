//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai47.hpp>

#include <srs_app_dash.hpp>
#include <srs_app_edge.hpp>
#include <srs_app_gb28181.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_http_static.hpp>
#include <srs_app_ingest.hpp>
#include <srs_app_listener.hpp>
#include <srs_app_rtc_api.hpp>
#include <srs_app_rtc_codec.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_network.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_server.hpp>
#include <srs_app_srt_conn.hpp>
#include <srs_app_statistic.hpp>
#include <srs_kernel_balance.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_mp4.hpp>
#include <srs_kernel_packet.hpp>
#include <srs_kernel_resource.hpp>
#include <srs_kernel_rtc_queue.hpp>
#include <srs_kernel_rtc_rtcp.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_format.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_rtc_stun.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_rtp.hpp>
#include <srs_protocol_sdp.hpp>
#include <srs_protocol_st.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_utest_ai07.hpp>
#include <srs_utest_ai08.hpp>
#include <srs_utest_ai09.hpp>
#include <srs_utest_ai12.hpp>
#include <srs_utest_ai13.hpp>
#include <srs_utest_ai14.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai16.hpp>
#include <srs_utest_ai17.hpp>
#include <srs_utest_ai18.hpp>
#include <srs_utest_ai23.hpp>
#include <srs_utest_ai25.hpp>
#include <srs_utest_ai34.hpp>
#include <srs_utest_ai36.hpp>
#include <srs_utest_ai37.hpp>
#include <srs_utest_ai38.hpp>
#include <srs_utest_ai39.hpp>
#include <srs_utest_ai41.hpp>
#include <srs_utest_manual_config.hpp>
#include <srs_utest_manual_http.hpp>
#include <srs_utest_manual_kernel.hpp>
#include <srs_utest_manual_mock.hpp>
#include <srs_utest_manual_protocol.hpp>

#include <errno.h>
#include <math.h>
#include <openssl/buffer.h>
#include <openssl/ssl.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef _WIN32
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <vector>

using namespace std;

extern srs_fopen_t _srs_fopen_fn;
extern srs_error_t aac_raw_append_adts_header(SrsMediaPacket *shared_audio, SrsFormat *format, char **pbuf, int *pnn_buf);

// Each test here shows one memory bug, a leak, double free, use-after-free or overflow, a crash or a stall on server input,
// under a sanitizer or by a failed assertion. A test of an unfixed bug is disabled so the suite stays green; run it
// with --gtest_also_run_disabled_tests and a filter. When the bug is fixed, the test is enabled.
//
// A test that checks in a child process with EXPECT_EXIT ends the child with _exit, not exit: exit runs static
// destructors, such as libsrt's, which lock mutexes a thread of the parent may have held at the fork, and hang.

// An HEVC PPS from a publisher's sequence header whose scaling_list_data() ends right after an Exp-Golomb
// code: the next scaling_list_pred_mode_flag is read with read_bit() and no require_bits(), so the bit buffer
// asserts on the empty stream and the server aborts. The parse should return an error instead.
VOID TEST(MemoryAuditTest, SrsHevcScalingListData_TruncatedPredModeFlagAborts)
{
    UtestBitWriter b;
    b.u(0x44, 8); // nal_unit_type 34, PPS
    b.u(0x01, 8); // nuh_layer_id 0, nuh_temporal_id_plus1 1
    b.ue(0);      // pps_pic_parameter_set_id
    b.ue(0);      // pps_seq_parameter_set_id
    b.u(0, 7);    // dependent_slice_segments_enabled_flag to cabac_init_present_flag
    b.ue(0);      // num_ref_idx_l0_default_active_minus1
    b.ue(0);      // num_ref_idx_l1_default_active_minus1
    b.se(0);      // init_qp_minus26
    b.u(0, 3);    // constrained_intra_pred_flag, transform_skip_enabled_flag, cu_qp_delta_enabled_flag
    b.se(0);      // pps_cb_qp_offset
    b.se(0);      // pps_cr_qp_offset
    b.u(0, 6);    // pps_slice_chroma_qp_offsets_present_flag to entropy_coding_sync_enabled_flag, no tiles
    b.u(0, 2);    // pps_loop_filter_across_slices_enabled_flag, deblocking_filter_control_present_flag
    b.u(1, 1);    // pps_scaling_list_data_present_flag
    // Three 4x4 matrices that predict, then the data ends on a byte boundary.
    for (int i = 0; i < 3; i++) {
        b.u(0, 1); // scaling_list_pred_mode_flag
        b.ue(0);   // scaling_list_pred_matrix_id_delta
    }
    ASSERT_EQ(0, b.size() % 8);
    std::vector<char> data = b.bytes();

    // Parse in a child process, which exits 0 only when the parse returns instead of aborting.
    EXPECT_EXIT(
        {
            SrsFormat format;
            srs_error_t err = format.initialize();
            if (err == srs_success) {
                SrsBuffer stream(&data[0], (int)data.size());
                err = format.hevc_demux_pps(&stream);
            }
            srs_freep(err);
            _exit(0);
        },
        ::testing::ExitedWithCode(0), "");
}

// An HEVC VPS NALU from a publisher's sequence header that holds only its first header byte: the parse reads
// that byte behind require(1), then skips nuh_layer_id and nuh_temporal_id_plus1 with skip(1) and no require(),
// so the buffer asserts on the end and the server aborts. The parse should return an error instead.
VOID TEST(MemoryAuditTest, SrsHevcRbspVps_OneByteNaluAborts)
{
    char data[] = {0x40}; // forbidden_zero_bit 0, nal_unit_type 32, VPS; the second header byte is cut

    // Parse in a child process, which exits 0 only when the parse returns instead of aborting.
    EXPECT_EXIT(
        {
            SrsFormat format;
            srs_error_t err = format.initialize();
            if (err == srs_success) {
                SrsBuffer stream(data, (int)sizeof(data));
                err = format.hevc_demux_vps(&stream);
            }
            srs_freep(err);
            _exit(0);
        },
        ::testing::ExitedWithCode(0), "");
}

// An HEVC SPS NALU from a publisher's sequence header that holds only its first header byte: like the VPS, the
// parse reads that byte behind require(1), then skips the second header byte with skip(1) and no require(), so
// the buffer asserts on the end and the server aborts. The parse should return an error instead.
VOID TEST(MemoryAuditTest, SrsHevcRbspSps_OneByteNaluAborts)
{
    char data[] = {0x42}; // forbidden_zero_bit 0, nal_unit_type 33, SPS; the second header byte is cut

    // Parse in a child process, which exits 0 only when the parse returns instead of aborting.
    EXPECT_EXIT(
        {
            SrsFormat format;
            srs_error_t err = format.initialize();
            if (err == srs_success) {
                SrsBuffer stream(data, (int)sizeof(data));
                err = format.hevc_demux_sps(&stream);
            }
            srs_freep(err);
            _exit(0);
        },
        ::testing::ExitedWithCode(0), "");
}

// An HEVC PPS NALU from a publisher's sequence header that holds only its first header byte: like the VPS and
// SPS, the parse reads that byte behind require(1), then skips the second header byte with skip(1) and no
// require(), so the buffer asserts on the end and the server aborts. The parse should return an error instead.
VOID TEST(MemoryAuditTest, SrsHevcRbspPps_OneByteNaluAborts)
{
    char data[] = {0x44}; // forbidden_zero_bit 0, nal_unit_type 34, PPS; the second header byte is cut

    // Parse in a child process, which exits 0 only when the parse returns instead of aborting.
    EXPECT_EXIT(
        {
            SrsFormat format;
            srs_error_t err = format.initialize();
            if (err == srs_success) {
                SrsBuffer stream(data, (int)sizeof(data));
                err = format.hevc_demux_pps(&stream);
            }
            srs_freep(err);
            _exit(0);
        },
        ::testing::ExitedWithCode(0), "");
}

// An HEVC PPS from a publisher's sequence header with tiles on and no uniform spacing: the parse resizes
// column_width_minus1 and row_height_minus1 to the ue(v) tile counts from the stream with no bound, so a few
// bytes make the server allocate and zero up to 8 GB per vector, and the vectors stay in the PPS table after
// the parse fails. A level 6.2 stream has at most 20 tile columns (H.265 Table A.8), so the parse should refuse
// a larger count before it allocates.
VOID TEST(MemoryAuditTest, SrsHevcRbspPps_TileCountAllocatesUnbounded)
{
    UtestBitWriter b;
    b.u(0x44, 8);         // nal_unit_type 34, PPS
    b.u(0x01, 8);         // nuh_layer_id 0, nuh_temporal_id_plus1 1
    b.ue(0);              // pps_pic_parameter_set_id
    b.ue(0);              // pps_seq_parameter_set_id
    b.u(0, 7);            // dependent_slice_segments_enabled_flag to cabac_init_present_flag
    b.ue(0);              // num_ref_idx_l0_default_active_minus1
    b.ue(0);              // num_ref_idx_l1_default_active_minus1
    b.se(0);              // init_qp_minus26
    b.u(0, 3);            // constrained_intra_pred_flag, transform_skip_enabled_flag, cu_qp_delta_enabled_flag
    b.se(0);              // pps_cb_qp_offset
    b.se(0);              // pps_cr_qp_offset
    b.u(0, 4);            // pps_slice_chroma_qp_offsets_present_flag to transquant_bypass_enabled_flag
    b.u(1, 1);            // tiles_enabled_flag
    b.u(0, 1);            // entropy_coding_sync_enabled_flag
    b.ue((1 << 24) - 1);  // num_tile_columns_minus1, 16M columns, 64 MB of uint32_t
    b.ue(0);              // num_tile_rows_minus1
    b.u(0, 1);            // uniform_spacing_flag, so the widths follow; the data ends here
    std::vector<char> data = b.bytes();
    EXPECT_GT(16, (int)data.size());

    srs_error_t err = srs_success;

    SrsFormat format;
    HELPER_EXPECT_SUCCESS(format.initialize());

    SrsBuffer stream(&data[0], (int)data.size());
    HELPER_EXPECT_FAILED(format.hevc_demux_pps(&stream));

    SrsHevcRbspPps *pps = &format.vcodec_->hevc_dec_conf_record_.pps_table_[0];
    EXPECT_GE(20u, pps->column_width_minus1_.size());
}

#if defined(__APPLE__) || defined(__linux__)
// A file on a full disk: stdio buffers the writes, and each flush of the buffer to the disk fails with ENOSPC.
// Seeking the disk itself works, so only the flush fails.
#ifdef __APPLE__
static int memory_audit_full_disk_write(void *cookie, const char *buf, int size)
{
    errno = ENOSPC;
    return -1;
}

static fpos_t memory_audit_full_disk_seek(void *cookie, fpos_t offset, int whence)
{
    return offset;
}

static FILE *memory_audit_full_disk_fopen(const char *path, const char *mode)
{
    return funopen(NULL, NULL, memory_audit_full_disk_write, memory_audit_full_disk_seek, NULL);
}
#else
static ssize_t memory_audit_full_disk_write(void *cookie, const char *buf, size_t size)
{
    errno = ENOSPC;
    return -1;
}

static int memory_audit_full_disk_seek(void *cookie, off64_t *offset, int whence)
{
    return 0;
}

static FILE *memory_audit_full_disk_fopen(const char *path, const char *mode)
{
    cookie_io_functions_t io = {NULL, memory_audit_full_disk_write, memory_audit_full_disk_seek, NULL};
    return fopencookie(NULL, mode, io);
}
#endif

// A DVR FLV file whose disk fills while the last frames are still in the 64 KB stdio buffer: on close, the
// segmenter updates the metadata with seek2, whose fseek flushes the buffer first. The flush fails, so fseek
// returns -1, seek2 asserts on it and the server aborts. seek2 should not abort on a failed seek.
VOID TEST(MemoryAuditTest, SrsFileWriter_SeekOnFullDiskAborts)
{
    // Write and seek in a child process, which exits 0 only when the seek returns instead of aborting.
    EXPECT_EXIT(
        {
            _srs_fopen_fn = memory_audit_full_disk_fopen;

            SrsFileWriter fw;
            srs_error_t err = fw.open("memory-audit-full-disk.flv");
            if (err == srs_success) {
                err = fw.set_iobuf_size(65536);
            }

            // The frame fits in the buffer, so the write succeeds though the disk is full.
            char frame[16] = {0};
            if (err == srs_success) {
                err = fw.write(frame, sizeof(frame), NULL);
            }
            if (err != srs_success) {
                srs_freep(err);
                _exit(1);
            }

            // The seek returns its error, which a full disk gives, instead of aborting.
            err = fw.seek2(0);
            int code = srs_error_code(err);
            srs_freep(err);
            _exit(code == ERROR_SYSTEM_FILE_SEEK ? 0 : 1);
        },
        ::testing::ExitedWithCode(0), "");
}
#endif

// Feeds one FLV audio tag to the MP4 encoder the way SrsDvrMp4Segmenter::encode_audio does.
static srs_error_t memory_audit_mp4_dvr_audio(SrsMp4Encoder *enc, SrsFormat *fmt, uint32_t dts, const char *tag, int size)
{
    srs_error_t err = srs_success;

    if ((err = fmt->on_audio(dts, (char *)tag, size)) != srs_success) {
        return srs_error_wrap(err, "demux audio");
    }

    SrsAudioAacFrameTrait ct = fmt->audio_->aac_packet_type_;
    if (ct == SrsAudioAacFrameTraitSequenceHeader) {
        enc->set_audio_codec(fmt->acodec_->id_, fmt->acodec_->sound_rate_, fmt->acodec_->sound_size_, fmt->acodec_->sound_type_);
    }

    return enc->write_sample(fmt, SrsMp4HandlerTypeSOUN, 0x00, ct, dts, dts, (uint8_t *)fmt->raw_, (uint32_t)fmt->nb_raw_);
}

// A publisher sends an AAC sequence header with no AudioSpecificConfig, only the two FLV audio bytes, then AAC frames,
// into a DVR MP4 session. The format accepts both, so the encoder keeps an empty ASC and counts the frames. When the
// DVR closes the file, the encoder builds the esds with a decoder specific info of zero bytes, and the descriptor's
// encode asserts its payload length is positive, so the server aborts. The flush should return an error instead.
VOID TEST(MemoryAuditTest, SrsMp4BaseDescriptor_EmptyAscAbortsDvrFlush)
{
    const char empty_sh[] = {(char)0xaf, 0x00};
    const char frame[] = {(char)0xaf, 0x01, 0x21, 0x11, 0x45, 0x00};

    // Record and flush in a child process, which exits 0 only when the flush returns instead of aborting.
    EXPECT_EXIT(
        {
            MockSrsFileWriter fw;
            SrsMp4Encoder enc;
            SrsFormat fmt;
            srs_error_t err = fmt.initialize();
            if (err == srs_success) {
                err = enc.initialize(&fw);
            }
            if (err == srs_success) {
                err = memory_audit_mp4_dvr_audio(&enc, &fmt, 0, empty_sh, (int)sizeof(empty_sh));
            }
            if (err == srs_success) {
                err = memory_audit_mp4_dvr_audio(&enc, &fmt, 0, frame, (int)sizeof(frame));
            }
            if (err == srs_success) {
                err = memory_audit_mp4_dvr_audio(&enc, &fmt, 23, frame, (int)sizeof(frame));
            }
            if (err != srs_success) {
                srs_freep(err);
                _exit(1);
            }

            err = enc.flush();
            srs_freep(err);
            _exit(0);
        },
        ::testing::ExitedWithCode(0), "");
}

// A WHIP publisher's HEVC RTP packet whose payload is one byte, the first header byte of an SPS NALU: the
// video track decodes it as a raw payload of type SPS, and SrsRtcFormat, which lists the picture size of each
// new SPS for the stream statistics, hands it to SrsFormat::hevc_demux_sps. Like a one-byte SPS in an RTMP
// sequence header, the parse reads that byte behind require(1), then skips the second header byte with skip(1)
// and no require(), so the buffer asserts on the end and the server aborts. One packet does it, with no
// sequence header. The packet should be refused instead.
VOID TEST(MemoryAuditTest, SrsFormat_WhipOneByteHevcSpsAborts)
{
    char data[] = {0x42}; // forbidden_zero_bit 0, nal_unit_type 33, SPS; the second header byte is cut

    // Run in a child process, which exits 0 only when the packet is handled instead of aborting.
    EXPECT_EXIT(
        {
            SrsUniquePtr<SrsStatistic> stat(new SrsStatistic());
            SrsUniquePtr<MockRequest> req(new MockRequest("test.vhost", "live", "whip_hevc"));

            // A WHIP HEVC video track, as negotiated from the publisher's offer.
            SrsUniquePtr<SrsRtcTrackDescription> desc(new SrsRtcTrackDescription());
            desc->type_ = "video";
            desc->ssrc_ = 5678;
            SrsVideoPayload *media = new SrsVideoPayload(49, "H265", 90000);
            media->h265_param_.profile_id_ = "1";
            media->h265_param_.level_id_ = "93";
            desc->media_ = media;
            MockRtcPacketReceiver receiver;
            SrsRtcVideoRecvTrack track(&receiver, desc.get(), false);

            SrsRtcFormat format;
            format.stat_ = stat.get();
            srs_error_t err = format.initialize(req.get());

            // The packet as SrsRtcVideoRecvTrack::on_before_decode_payload decodes it: a raw payload of type SPS.
            if (err == srs_success) {
                SrsRtpPacket pkt;
                SrsRtpRawPayload *raw = new SrsRtpRawPayload();
                raw->sample_->bytes_ = data;
                raw->sample_->size_ = (int)sizeof(data);
                pkt.nalu_type_ = SrsHevcNaluTypeParse(data[0]);
                pkt.set_payload(raw, SrsRtpPacketPayloadTypeRaw);
                err = format.on_rtp_packet(&track, &pkt, false);
            }
            srs_freep(err);
            _exit(0);
        },
        ::testing::ExitedWithCode(0), "");
}

// A GB28181 device sends an RTP packet over the media TCP connection whose PS pack has a system header with
// header_length 0. SrsPsPacket::decode_system checks the stream holds the length, then reads the 6 fixed fields
// (rate bound, audio and video bounds, flags) from a sub-buffer of header_length bytes with no require(), so the
// buffer asserts on the 3-byte rate bound and the server aborts. Any header_length below 6 does it. The pack
// should be refused, so the context enters its recover mode instead.
VOID TEST(MemoryAuditTest, SrsPsPacket_ShortSystemHeaderAborts)
{
    // RTP header: V=2, PT=96, seq=1, ts=0, SSRC=0x0beb5135.
    string raw = string("\x80\x60\x00\x01\x00\x00\x00\x00\x0b\xeb\x51\x35", 12);
    // The pack header of a GB28181 camera: 14 bytes and 6 stuffing bytes.
    raw += string("\x00\x00\x01\xba\x44\x68\x6e\x4c\x94\x01\x01\x30\x13\xfe\xff\xff\x00\x00\xa0\x05", 20);
    // The system header with header_length 0, then bytes so the pack is above the integrity check's 32 bytes.
    raw += string("\x00\x00\x01\xbb\x00\x00", 6);
    raw += string(12, '\xff');

    // Run in a child process, which exits 0 only when the packet is handled instead of aborting.
    EXPECT_EXIT(
        {
            MockPsHandler handler;
            SrsRecoverablePsContext context;
            context.ctx_->set_detect_ps_integrity(true);

            // As SrsGbMediaTcpConn::do_cycle parses one RTP packet, with no reserved bytes.
            SrsBuffer b((char *)raw.data(), (int)raw.length());
            srs_error_t err = context.decode_rtp(&b, 0, &handler);
            srs_freep(err);
            _exit(0);
        },
        ::testing::ExitedWithCode(0), "");
}

// A WebRTC publisher sends video RTP in runs of 16 packets and skips one sequence number each time, so each lost
// number is 17 after the one before. SrsRtcRecvTrack::on_nack adds every lost number to the NACK list, which allows
// 665 for video, and SrsRtcConnection::check_send_nacks asks for all of them in one SrsRtcpNack, encoded into a
// 1500-byte buffer. SrsRtcpNack::encode checks the buffer only for its own nb_bytes(), the same 1500, then writes one
// 4-byte PID and BLP pair for every number more than 16 after the last pair's PID: 400 lost numbers need 1612 bytes,
// and the 2-byte write past the end asserts, so the server aborts. The NACK should be split or cut to the buffer.
VOID TEST(MemoryAuditTest, SrsRtcpNack_SparseLossesOverflowEncodeAborts)
{
    // Run in a child process, which exits 0 only when the NACK encode returns instead of aborting.
    EXPECT_EXIT(
        {
            MockRtcPacketReceiver receiver;
            MockCircuitBreaker breaker;
            breaker.hybrid_high_water_level_ = false;

            SrsRtcTrackDescription desc;
            desc.type_ = "video";
            desc.id_ = "video-track";
            desc.ssrc_ = 0x2345;
            desc.is_active_ = true;
            SrsUniquePtr<SrsRtcVideoRecvTrack> track(new SrsRtcVideoRecvTrack(&receiver, &desc, false));
            track->circuit_breaker_ = &breaker;

            // 400 runs of 16 packets, each run followed by one lost sequence number, and a packet after the last.
            uint16_t seq = 1000;
            for (int i = 0; i <= 400; i++) {
                for (int j = 0; j < (i < 400 ? 16 : 1); j++) {
                    SrsRtpPacket pkt;
                    pkt.header_.set_ssrc(0x2345);
                    pkt.header_.set_sequence(seq++);
                    SrsRtpPacket *p = &pkt;
                    srs_error_t err = track->on_nack(&p);
                    srs_freep(err);
                }
                seq++;
            }
            if (track->nack_receiver_->queue_.size() != 400) {
                _exit(2);
            }

            // Past the first NACK interval of 10ms, as the publisher's NACK timer finds them. A forked child has
            // no ST event loop to sleep in, so it waits on the clock.
            srs_utime_t start = srs_time_now_realtime();
            while (srs_time_now_realtime() - start < 30 * SRS_UTIME_MILLISECONDS) {
            }

            // As SrsRtcConnection::check_send_nacks builds and encodes the NACK.
            SrsRtcpNack nack(0x1234);
            nack.set_media_ssrc(0x2345);
            uint32_t timeout_nacks = 0;
            track->nack_receiver_->get_nack_seqs(nack, timeout_nacks);
            if (nack.get_lost_sns().size() != 400) {
                _exit(3);
            }

            char buf[kRtcpPacketSize];
            SrsBuffer stream(buf, sizeof(buf));
            srs_error_t err = nack.encode(&stream);
            srs_freep(err);
            _exit(0);
        },
        ::testing::ExitedWithCode(0), "");
}

// Builds a WebRTC player's generic NACK for the media SSRC: one PID and BLP pair for every 17 sequence numbers from
// the first, each BLP with all 16 bits set, so the packet names 17 numbers for every 4 bytes of FCI.
static std::vector<char> memory_audit_full_bitmask_nack(uint32_t media_ssrc, uint16_t first, int nn_fci)
{
    int nb = 12 + 4 * nn_fci;
    std::vector<char> bytes(nb);
    SrsBuffer w(&bytes[0], nb);
    w.write_1bytes(0x81); // V=2, FMT=1, generic NACK
    w.write_1bytes(SrsRtcpType_rtpfb);
    w.write_2bytes((uint16_t)(2 + nn_fci));
    w.write_4bytes(0x1234);
    w.write_4bytes(media_ssrc);
    for (int i = 0; i < nn_fci; i++) {
        w.write_2bytes((uint16_t)(first + i * 17));
        w.write_2bytes((int16_t)0xffff);
    }
    return bytes;
}

// A WebRTC player sends generic NACKs whose BLP bitmasks have every bit set. SrsRtcpNack::decode expanded each 4-byte
// PID and BLP pair into 17 inserts in its sequence set, bounded only by the packet: an RTCP packet over TCP, or a
// fragmented UDP datagram, holds 16380 pairs, 278460 inserts, and SrsRtcSendTrack::on_recv_nack sent every cached
// packet named again. So one 65532-byte NACK took the one ST thread far longer than a packet should, and a 248-byte
// NACK resent the whole 1000-packet video cache. The decode keeps at most kRtcpNackMaxLostSns numbers.
VOID TEST(MemoryAuditTest, SrsRtcpNack_BitmaskExpansionResendsWholeCache)
{
    srs_error_t err = srs_success;

    MockRtcPacketSender sender;
    SrsRtcTrackDescription desc;
    desc.type_ = "video";
    desc.ssrc_ = 5678;
    desc.is_active_ = true;
    SrsRtcVideoSendTrack track(&sender, &desc);
    track.assemble();

    // The player's track sends and caches 1000 packets, its whole video cache.
    uint16_t first = 0;
    for (int i = 0; i < 1000; i++) {
        SrsRtpPacket *pkt = new SrsRtpPacket();
        pkt->header_.set_sequence(5000 + i);
        pkt->header_.set_timestamp(90000 + i * 3000);
        HELPER_EXPECT_SUCCESS(track.on_rtp(pkt));
        if (i == 0) {
            first = pkt->header_.get_sequence();
        }
        HELPER_EXPECT_SUCCESS(track.on_nack(&pkt));
        srs_freep(pkt);
    }
    sender.send_packet_count_ = 0;
    sender.sent_packets_.clear();

    // One NACK of 16380 pairs, as SrsRtcpCompound::decode and SrsRtcPlayStream::on_rtcp_nack handle it.
    if (true) {
        std::vector<char> bytes = memory_audit_full_bitmask_nack(5678, first, 16380);
        srs_utime_t starttime = srs_time_now_realtime();

        SrsRtcpNack nack;
        SrsBuffer r(&bytes[0], (int)bytes.size());
        HELPER_ASSERT_SUCCESS(nack.decode(&r));
        std::vector<uint16_t> seqs = nack.get_lost_sns();
        HELPER_ASSERT_SUCCESS(track.on_recv_nack(seqs));

        srs_utime_t elapsed = srs_time_now_realtime() - starttime;
        EXPECT_GT(20 * SRS_UTIME_MILLISECONDS, elapsed);
        EXPECT_EQ(kRtcpNackMaxLostSns, (int)seqs.size());
        EXPECT_TRUE(r.empty());
    }
    sender.send_packet_count_ = 0;
    sender.sent_packets_.clear();

    // A 248-byte NACK of 59 pairs, which names the whole cache.
    if (true) {
        std::vector<char> bytes = memory_audit_full_bitmask_nack(5678, first, 59);
        SrsRtcpNack nack;
        SrsBuffer r(&bytes[0], (int)bytes.size());
        HELPER_ASSERT_SUCCESS(nack.decode(&r));
        std::vector<uint16_t> seqs = nack.get_lost_sns();
        HELPER_ASSERT_SUCCESS(track.on_recv_nack(seqs));
    }
    EXPECT_EQ(kRtcpNackMaxLostSns, sender.send_packet_count_);
}

// Decodes the RTCP bytes as SrsRtcConnection::on_rtcp does and gives each packet to the publisher, as
// SrsRtcConnection::dispatch_rtcp does for an XR.
static srs_error_t memory_audit_publisher_on_rtcp(SrsRtcPublishStream *publisher, const std::string &bytes)
{
    srs_error_t err = srs_success;

    SrsBuffer b((char *)bytes.data(), (int)bytes.size());
    SrsRtcpCompound compound;
    if ((err = compound.decode(&b)) != srs_success) {
        return srs_error_wrap(err, "decode");
    }

    SrsRtcpCommon *rtcp = NULL;
    while ((rtcp = compound.get_next_rtcp()) != NULL) {
        err = publisher->on_rtcp(rtcp);
        srs_freep(rtcp);
        if (err != srs_success) {
            return srs_error_wrap(err, "on rtcp");
        }
    }
    return err;
}

// A WHIP publisher sends an RTCP XR from its media SSRC, which SrsRtcConnection::dispatch_rtcp gives to its publish
// stream, with a DLRR report block (block type 5) of block length 1, so the block holds only 4 bytes of the 12-byte
// sub-block. SrsRtcPublishStream::on_rtcp_xr checks the block against the rest of the compound packet, not against
// the sub-blocks it reads, then reads one whole 12-byte sub-block for every started 12 bytes of the block. When the
// XR is the last packet, the 8 bytes past the end assert in SrsBuffer::read_4bytes and the server aborts. Any DLRR
// block whose length is not 1 plus a multiple of 3 words reads past it the same way. The parser should refuse a
// block that is not a whole number of sub-blocks, and stay within the XR packet.
VOID TEST(MemoryAuditTest, SrsRtcpXr_ShortDlrrBlockAbortsPublisher)
{
    // An XR with one whole DLRR sub-block: header, SSRC, block header with block length 3, SSRC, LRR, DLRR.
    std::string whole = std::string("\x80\xcf\x00\x05\x00\x00\x12\x34", 8);
    whole += std::string("\x05\x00\x00\x03\x00\x00\x56\x78\x00\x00\x00\x00\x00\x00\x00\x00", 16);
    // An XR with a DLRR block of block length 1: header, SSRC, block header, then only the sub-block's SSRC.
    std::string cut = std::string("\x80\xcf\x00\x03\x00\x00\x12\x34", 8);
    cut += std::string("\x05\x00\x00\x01\x00\x00\x56\x78", 8);

    // Run in a child process, which exits 0 only when the XR is handled instead of aborting.
    EXPECT_EXIT(
        {
            MockRtcAsyncTaskExecutor exec;
            MockExpire expire;
            MockRtcPacketReceiver receiver;
            SrsContextId cid;
            cid.set_value("memory-audit-xr-publisher");
            SrsUniquePtr<SrsRtcPublishStream> publisher(new SrsRtcPublishStream(&exec, &expire, &receiver, cid));

            // The whole sub-block is handled.
            srs_error_t err = memory_audit_publisher_on_rtcp(publisher.get(), whole);
            if (err != srs_success) {
                srs_freep(err);
                _exit(2);
            }

            err = memory_audit_publisher_on_rtcp(publisher.get(), cut);
            srs_freep(err);
            _exit(0);
        },
        ::testing::ExitedWithCode(0), "");
}

// A PES header whose extension has the pack_header_field flag but no PES_private_data flag. After reading the 1-byte
// pack_field_length, SrsMpegPES::decode adjusts its earlier byte count with "- 16 - 1 + pack_field_length", which
// takes off the 16 private data bytes even when there are none, so it checks 16 bytes too few. A pack_field_length
// of 180 with 173 bytes left passes the check for 164, then read_bytes asserts and the server aborts. An SRT
// publisher, a UDP MPEG-TS caster sender, or a GB28181 device (SrsPsPacket decodes its PES the same way) can send it.
// The PES should be refused, since the pack field does not fit.
VOID TEST(MemoryAuditTest, SrsMpegPES_PackFieldWithoutPrivateDataAborts)
{
    const int pid = 0x100;

    char p[SRS_TS_PACKET_SIZE];
    memset(p, 0xFF, SRS_TS_PACKET_SIZE);
    // TS header: unit start, payload only.
    const uint8_t ts[4] = {0x47, (uint8_t)(0x40 | (pid >> 8)), (uint8_t)(pid & 0xFF), 0x10};
    memcpy(p, ts, sizeof(ts));
    // PES: start code, video stream id, PES_packet_length 0, then the flags with only PES_extension_flag,
    // PES_header_data_length 182, the extension flags with only pack_header_field_flag, and pack_field_length 180.
    const uint8_t pes[11] = {0x00, 0x00, 0x01, 0xE0, 0x00, 0x00, 0x80, 0x01, 182, 0x4E, 180};
    memcpy(p + 4, pes, sizeof(pes));
    ASSERT_EQ(173, SRS_TS_PACKET_SIZE - 4 - (int)sizeof(pes));

    // Decode in a child process, which exits 0 only when the packet is handled instead of aborting.
    EXPECT_EXIT(
        {
            SrsTsContext ctx;
            ctx.set(pid, SrsTsPidApplyVideo, SrsTsStreamVideoH264);
            MockTsHandler handler;

            // As SrsMpegtsOverUdp and SrsSrtFrameBuilder decode one 188-byte TS packet.
            SrsBuffer stream(p, SRS_TS_PACKET_SIZE);
            srs_error_t err = ctx.decode(&stream, &handler);
            srs_freep(err);
            _exit(0);
        },
        ::testing::ExitedWithCode(0), "");
}

// A WHIP, WHEP or RTC API offer is parsed before any hook, and a GB28181 y= line in a media section becomes two
// a=ssrc lines built by srs_fmt_sprintf. That helper formats into a static 8192-byte buffer and returns an empty
// string when the result does not fit, so a y= value of about 4 KB gives an empty cname line, and
// SrsMediaDesc::parse_line takes substr(2) of it, which throws std::out_of_range. The server has no handler, so
// one HTTP POST aborts the whole server. A short y= value parses; the long one should be refused instead.
VOID TEST(MemoryAuditTest, SrsFmtSprintf_WhipLongGbSsrcThrows)
{
    std::string head = "v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 96\r\ny=";

    if (true) {
        SrsSdp sdp;
        srs_error_t err = srs_success;
        EXPECT_NO_THROW(err = sdp.parse(head + "0100008888\r\n"));
        HELPER_EXPECT_SUCCESS(err);
    }

    if (true) {
        SrsSdp sdp;
        srs_error_t err = srs_success;
        EXPECT_NO_THROW(err = sdp.parse(head + std::string(4096, '1') + "\r\n"));
        srs_freep(err);
    }
}

// The forward backend answers the on_forward hook with {"data":{"urls":[...]}}; on_forward_backend() walks the
// SrsJsonArray by count() and at() and calls to_str() on each entry without checking it is a string, so an entry
// that is a number, null, object or array makes to_str() assert and the server aborts on publish. The hook should
// skip or refuse the entry instead.
VOID TEST(MemoryAuditTest, SrsJsonArray_ForwardBackendNonStringUrlAborts)
{
    // Call the hook in a child process, which exits 0 only when the hook returns instead of aborting.
    EXPECT_EXIT(
        {
            MockAppFactoryForForwardBackend factory;
            MockStatisticForHooks stat;
            SrsHttpHooks hooks;
            hooks.factory_ = &factory;
            hooks.stat_ = &stat;
            hooks.config_ = NULL;

            factory.exchange_.status_code_ = 200;
            factory.exchange_.response_ = "{\"code\":0,\"data\":{\"urls\":[\"rtmp://127.0.0.1/live/a\",123]}}";

            MockRequest req("__defaultVhost__", "live", "livestream");
            std::vector<std::string> urls;
            srs_error_t err = hooks.on_forward_backend("http://127.0.0.1:8085/api/v1/forward", &req, urls);
            srs_freep(err);
            _exit(0);
        },
        ::testing::ExitedWithCode(0), "");
}

// A WHIP H.264 publisher, with RTC to RTMP on, sends a STAP-A of a one-byte SPS (only the NALU header 0x67) and a
// PPS. The frame builder takes it as a keyframe, and do_packet_sequence_header_avc() hands the SPS to
// SrsRawH264Stream::mux_sequence_header(), which asserts the SPS has the 4 bytes of the type, profile and level, so
// the server aborts. The SRT, MPEG-TS over UDP and GB28181 paths get the SPS from sps_demux(), which drops one
// shorter than 4 bytes; the RTC path has no such check. The short SPS should be refused instead.
VOID TEST(MemoryAuditTest, SrsRawH264Stream_WhipOneByteSpsAborts)
{
    char sps_data[] = {0x67};                               // nal_unit_type 7, SPS; profile and level are cut
    char pps_data[] = {0x68, (char)0xce, 0x3c, (char)0x80}; // nal_unit_type 8, PPS

    // Run in a child process, which exits 0 only when the packet is handled instead of aborting.
    EXPECT_EXIT(
        {
            MockRtcFrameTarget target;
            SrsRtcFrameBuilder builder(_srs_app_factory, &target);
            MockRequest req("test.vhost", "live", "whip_avc");
            srs_error_t err = builder.initialize(&req, SrsAudioCodecIdAAC, SrsVideoCodecIdAVC);

            // The packet as SrsRtcVideoRecvTrack::on_before_decode_payload decodes a STAP-A.
            if (err == srs_success) {
                SrsRtpPacket pkt;
                pkt.header_.set_ssrc(5678);
                pkt.header_.set_sequence(100);
                pkt.header_.set_timestamp(90000);
                pkt.frame_type_ = SrsFrameTypeVideo;
                pkt.set_avsync_time(1000);
                pkt.nalu_type_ = kStapA;

                SrsRtpSTAPPayload *stap = new SrsRtpSTAPPayload();
                stap->nalus_.push_back(new SrsNaluSample(sps_data, (int)sizeof(sps_data)));
                stap->nalus_.push_back(new SrsNaluSample(pps_data, (int)sizeof(pps_data)));
                pkt.set_payload(stap, SrsRtpPacketPayloadTypeSTAP);

                err = builder.on_rtp(&pkt);
            }
            srs_freep(err);
            _exit(0);
        },
        ::testing::ExitedWithCode(0), "");
}

// Feed one WHIP HEVC aggregation packet of a VPS, an SPS and a PPS to a frame builder with RTC to RTMP on, as
// SrsRtcVideoRecvTrack::on_before_decode_payload decodes it, then exit 0 if the builder returns.
static void memory_audit_whip_hevc_vps_sps_pps(char *vps_data, int nb_vps)
{
    char sps_data[] = {0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, (char)0x90, 0x00, 0x00, 0x03, 0x00,
                       0x00, 0x03, 0x00, 0x3d, (char)0xa0, 0x02, (char)0x80, (char)0x80, 0x2d, 0x16, 0x59, 0x59,
                       (char)0xa4, (char)0x93, 0x2b, (char)0xc0, 0x40, 0x40, 0x00, 0x00, (char)0xfa, 0x40, 0x00,
                       0x17, 0x70, 0x02};
    char pps_data[] = {0x44, 0x01, (char)0xc1, 0x72, (char)0xb4, 0x62, 0x40};

    MockRtcFrameTarget target;
    SrsRtcFrameBuilder builder(_srs_app_factory, &target);
    MockRequest req("test.vhost", "live", "whip_hevc");
    srs_error_t err = builder.initialize(&req, SrsAudioCodecIdAAC, SrsVideoCodecIdHEVC);

    if (err == srs_success) {
        SrsRtpPacket pkt;
        pkt.header_.set_ssrc(5678);
        pkt.header_.set_sequence(100);
        pkt.header_.set_timestamp(90000);
        pkt.frame_type_ = SrsFrameTypeVideo;
        pkt.set_avsync_time(1000);
        pkt.nalu_type_ = kStapHevc;

        SrsRtpSTAPPayloadHevc *stap = new SrsRtpSTAPPayloadHevc();
        stap->nalus_.push_back(new SrsNaluSample(vps_data, nb_vps));
        stap->nalus_.push_back(new SrsNaluSample(sps_data, (int)sizeof(sps_data)));
        stap->nalus_.push_back(new SrsNaluSample(pps_data, (int)sizeof(pps_data)));
        pkt.set_payload(stap, SrsRtpPacketPayloadTypeSTAPHevc);

        err = builder.on_rtp(&pkt);
    }
    srs_freep(err);
    _exit(0);
}

// A WHIP HEVC publisher, with RTC to RTMP on, sends an aggregation packet of a one-byte VPS (only the first NALU
// header byte 0x40), an SPS and a PPS. SrsRtcFormat only parses the SPS, so the VPS goes unchecked to
// do_packet_sequence_header_hevc(), which hands it to SrsRawHEVCStream::mux_sequence_header(). That parses it with
// SrsFormat::hevc_demux_vps(), which reads one byte behind require(1) and then skips the second header byte with no
// check, so SrsBuffer::skip() asserts and the server aborts. SRT to RTMP and GB28181 reach the same call with a
// one-byte VPS from vps_demux(), which keeps any non-empty VPS. The short VPS should be refused instead.
VOID TEST(MemoryAuditTest, SrsRawHEVCStream_WhipOneByteVpsAborts)
{
    // The two-byte VPS fails to parse and the builder returns its error.
    char vps2[] = {0x40, 0x01};
    EXPECT_EXIT(memory_audit_whip_hevc_vps_sps_pps(vps2, (int)sizeof(vps2)), ::testing::ExitedWithCode(0), "");

    // The one-byte VPS aborts in a child process, where exit 0 is expected.
    char vps1[] = {0x40};
    EXPECT_EXIT(memory_audit_whip_hevc_vps_sps_pps(vps1, (int)sizeof(vps1)), ::testing::ExitedWithCode(0), "");
}

// Decode a STUN binding request whose USERNAME is the server's ufrag, a colon and nb_remote bytes of the client's
// ufrag, and answer it as the UDP network of a session that accepted it, then exit 0 if the network returns.
static void memory_audit_stun_binding_long_username(int nb_remote)
{
    string username = "srvufrag:" + string(nb_remote, 'x');
    int nb_attr = 4 + (int)username.size() + ((username.size() % 4) ? 4 - (username.size() % 4) : 0);

    vector<char> data(20 + nb_attr, 0);
    SrsBuffer b(&data[0], (int)data.size());
    b.write_2bytes(BindingRequest);
    b.write_2bytes(nb_attr);
    b.write_4bytes(kStunMagicCookie);
    b.write_string("012345678901");
    b.write_2bytes(Username);
    b.write_2bytes((int16_t)username.size());
    b.write_string(username);

    SrsStunPacket ping;
    srs_error_t err = ping.decode(&data[0], (int)data.size());

    if (err == srs_success) {
        MockRtcConnectionForUdpNetwork conn;
        MockEphemeralDelta delta;
        SrsRtcUdpNetwork network(&conn, &delta);
        SrsSessionConfig cfg;
        cfg.dtls_role_ = "passive";
        cfg.dtls_version_ = "auto";
        err = network.initialize(&cfg, false, false);
        if (err == srs_success) {
            network.sendonly_skt_ = new MockUdpMuxSocket();
            err = network.on_stun(&ping, &data[0], (int)data.size());
        }
    }
    srs_freep(err);
    _exit(0);
}

// A WebRTC client sends a binding request with a USERNAME of 1,433 bytes or more, signed with the ICE password it got
// in the answer. The session is found by the username or, for a later request, by the client's address, so the long
// name is not compared with the session's. SrsRtcUdpNetwork::on_binding_request() swaps the two ufrags into the
// response, and SrsStunPacket::encode_binding_response() writes the USERNAME into a 1,460-byte buffer in
// encode_username() and the whole response into the caller's 1,500-byte buffer with no size check, so SrsBuffer
// asserts and the server aborts. The response should be refused, or the username checked, instead.
VOID TEST(MemoryAuditTest, SrsStunPacket_LongUsernameResponseAborts)
{
    // A 1,432-byte username, 9 bytes of the server's ufrag and colon plus 1,423 of the client's, fills the 1,500-byte
    // response exactly and returns.
    EXPECT_EXIT(memory_audit_stun_binding_long_username(1423), ::testing::ExitedWithCode(0), "");

    // One more byte overflows the response buffer and aborts in a child process, where exit 0 is expected.
    EXPECT_EXIT(memory_audit_stun_binding_long_username(1424), ::testing::ExitedWithCode(0), "");
}

// An RTMP edge follows an origin's redirect, and an HTTP-FLV edge an origin's 302 Location, to the host and
// port in that URL. The URL parser takes any run of digits as the port, so a port of eight digits reaches
// srs_tcp_connect, which prints it into an 8-byte buffer and asserts the text fits, and the server aborts.
// The connect should return an error instead.
VOID TEST(MemoryAuditTest, SrsTcpConnect_LongRedirectPortAborts)
{
    std::string schema, host, vhost, app, stream, param;
    int port = SRS_CONSTS_RTMP_DEFAULT_PORT;
    srs_net_url_parse_tcurl("rtmp://127.0.0.1:12345678/live", schema, host, vhost, app, stream, port, param);
    ASSERT_STREQ("127.0.0.1", host.c_str());
    ASSERT_EQ(12345678, port);

    // Connect in a child process, which exits 0 only when the connect returns instead of aborting.
    EXPECT_EXIT(
        {
            srs_netfd_t stfd = NULL;
            srs_error_t err = srs_tcp_connect(host, port, 100 * SRS_UTIME_MILLISECONDS, &stfd);
            srs_close_stfd(stfd);
            int code = srs_error_code(err);
            srs_freep(err);
            _exit(code == ERROR_SYSTEM_PORT_INVALID ? 0 : 1);
        },
        ::testing::ExitedWithCode(0), "");
}

// A config value and the env var that overrides it, refused or taken by the config check.
struct MemoryAuditConfigValue {
    const char *conf_;
    const char *env_;
    const char *value_;
};

// Check that the config check refuses the value in the config file and in its env var, with an error that names it.
static void memory_audit_config_refused(const MemoryAuditConfigValue &v)
{
    if (true) {
        string c = string(_MIN_OK_CONF) + "vhost test.com{" + v.conf_ + " " + v.value_ + ";}}";
        MockSrsConfig conf;
        srs_error_t err = conf.mock_parse(c);
        EXPECT_TRUE(err != srs_success) << c;
        EXPECT_NE(string::npos, srs_error_desc(err).find(v.value_)) << c;
        srs_freep(err);
    }

    if (true) {
        MockSrsConfig conf;
        SrsSetEnvConfig(conf, value, v.env_, v.value_);
        srs_error_t err = conf.mock_parse(_MIN_OK_CONF);
        EXPECT_TRUE(err != srs_success) << v.env_ << "=" << v.value_;
        EXPECT_NE(string::npos, srs_error_desc(err).find(string(v.env_) + "=" + v.value_)) << v.env_;
        srs_freep(err);
    }
}

// Check that the config check takes the value in the config file and in its env var.
static void memory_audit_config_taken(const MemoryAuditConfigValue &v)
{
    srs_error_t err;

    if (true) {
        string c = string(_MIN_OK_CONF) + "vhost test.com{" + v.conf_ + " " + v.value_ + ";}}";
        MockSrsConfig conf;
        HELPER_EXPECT_SUCCESS(conf.mock_parse(c));
    }

    if (true) {
        MockSrsConfig conf;
        SrsSetEnvConfig(conf, value, v.env_, v.value_);
        HELPER_EXPECT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
    }
}

// The MPD writer lists the last window of segments from index "size - window", so with a "dash_window_size" of 0
// that index is one past the last segment, and with a negative window further: std::vector::at throws out_of_range,
// nothing catches it, and the server aborts as soon as a stream publishes to the vhost. The config check refuses a
// window below 1, from the config file or the env, so the writer gets a valid window.
VOID TEST(MemoryAuditTest, SrsMpdWriter_ZeroWindowSizeReadsPastSegments)
{
    MemoryAuditConfigValue bad[] = {
        {"dash{dash_window_size", "SRS_VHOST_DASH_DASH_WINDOW_SIZE", "0"},
        {"dash{dash_window_size", "SRS_VHOST_DASH_DASH_WINDOW_SIZE", "-1"},
    };
    for (int i = 0; i < (int)(sizeof(bad) / sizeof(bad[0])); i++) {
        memory_audit_config_refused(bad[i]);
    }

    MemoryAuditConfigValue good = {"dash{dash_window_size", "SRS_VHOST_DASH_DASH_WINDOW_SIZE", "1"};
    memory_audit_config_taken(good);
}

// Every DASH segment asks the MPD writer for its file name, which asserts the fragment is not 0, so a "dash_fragment"
// of 0, or one so small, such as 0.0000001, that its cast to microseconds is 0, aborts the server on the first audio
// or video frame a stream publishes to the vhost. The config check refuses a fragment that is not above 0 as a
// srs_utime_t, from the config file or the env.
VOID TEST(MemoryAuditTest, SrsMpdWriter_ZeroFragmentAborts)
{
    MemoryAuditConfigValue bad[] = {
        {"dash{dash_fragment", "SRS_VHOST_DASH_DASH_FRAGMENT", "0"},
        {"dash{dash_fragment", "SRS_VHOST_DASH_DASH_FRAGMENT", "0.0000001"},
    };
    for (int i = 0; i < (int)(sizeof(bad) / sizeof(bad[0])); i++) {
        memory_audit_config_refused(bad[i]);
    }

    MemoryAuditConfigValue good = {"dash{dash_fragment", "SRS_VHOST_DASH_DASH_FRAGMENT", "0.001"};
    memory_audit_config_taken(good);
}

// With on_hls_notify configured, each new HLS segment makes SrsHttpHooks::on_hls_notify() GET the segment from the
// CDN and read at most hls_nb_notify bytes of it into a buffer of srs_min(nb_notify, 4096) bytes, so a negative
// hls_nb_notify makes the buffer new char[negative], which throws std::bad_array_new_length that nothing catches,
// and the server aborts at the first segment. The config check refuses a negative hls_nb_notify, from the config
// file or the env; 0 reads nothing and is taken.
VOID TEST(MemoryAuditTest, SrsHttpHooks_HlsNotifyNegativeSizeAborts)
{
    MemoryAuditConfigValue bad = {"hls{hls_nb_notify", "SRS_VHOST_HLS_HLS_NB_NOTIFY", "-1"};
    memory_audit_config_refused(bad);

    MemoryAuditConfigValue good = {"hls{hls_nb_notify", "SRS_VHOST_HLS_HLS_NB_NOTIFY", "0"};
    memory_audit_config_taken(good);
}

// Mock HTTP hooks whose on_play stands for the switch to a hook server, where SrsHttpStreamDestroy::call() samples
// whether the HTTP stream it tears down still has viewers.
class MemoryAuditLiveStreamHooks : public MockHttpHooksForLiveStream
{
public:
    SrsLiveStream *stream_;
    // How many on_play calls ran while the stream was disabled and still alive.
    int alive_while_disabled_;

public:
    MemoryAuditLiveStreamHooks()
    {
        stream_ = NULL;
        alive_while_disabled_ = 0;
    }

public:
    virtual srs_error_t on_play(std::string url, ISrsRequest *req, SrsRtmpConnType type)
    {
        if (!stream_->entry_->enabled && stream_->alive()) {
            alive_while_disabled_++;
        }
        return MockHttpHooksForLiveStream::on_play(url, req, type);
    }
};

// When a stream unpublishes, SrsHttpStreamDestroy::call() disables the mount entry, expires the current viewers, then
// samples stream->alive() every 100 ms and asserts after 3000 samples, 300 s, if it is still alive. But the stream
// stays mounted while it waits, and SrsLiveStream::serve_http() adds every new viewer to viewers_ before
// serve_http_impl() runs the on_play hook, which checks entry_->enabled only after the hook returns. So each player
// that connects during the teardown, such as every player of a popular stream retrying after it ended, keeps the stream
// alive for the whole round trip to the hook server, and is never expired. When such players overlap every sample for
// 300 s, the srs_assert(false) aborts the server; until then the publisher cannot publish the stream again, since
// http_mount() refuses a disposing entry.
VOID TEST(MemoryAuditTest, SrsLiveStream_ViewerDuringTeardownKeepsStreamAliveAcrossHook)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<MockHttpMessageForLiveStream> message(new MockHttpMessageForLiveStream());
    MockResponseWriter writer;
    MockRequest request("test.vhost", "live", "stream1");
    MockBufferCache cache;
    MockStatisticForLiveStream stat;
    MockSecurity security;

    MockAppConfigForLiveStreamHooks config;
    config.http_hooks_enabled_ = true;
    config.on_play_directive_ = new SrsConfDirective();
    config.on_play_directive_->name_ = "on_play";
    config.on_play_directive_->args_.push_back("http://127.0.0.1:8085/api/v1/play");

    SrsLiveStream stream(&request, &cache);
    MemoryAuditLiveStreamHooks hooks;
    hooks.stream_ = &stream;
    stream.stat_ = &stat;
    srs_freep(stream.security_);
    stream.security_ = &security;
    stream.config_ = &config;
    stream.hooks_ = &hooks;

    // The teardown has started: the entry is disabled and the viewers at that moment are expired.
    stream.entry_ = new SrsHttpMuxEntry();
    stream.entry_->enabled = false;
    stream.expire();

    // A player connects now, and is refused only after its on_play hook.
    err = stream.serve_http(&writer, message.get());
    EXPECT_EQ(ERROR_RTMP_STREAM_NOT_FOUND, srs_error_code(err));
    srs_freep(err);
    EXPECT_TRUE(stream.viewers_.empty());

    // The teardown samples alive() during that hook and sees a viewer, so it keeps waiting. A refusal before the
    // hook, or a viewer not counted once the entry is disabled, would both pass.
    EXPECT_EQ(0, hooks.alive_while_disabled_);

    stream.stat_ = NULL;
    stream.security_ = NULL;
    stream.config_ = NULL;
    stream.hooks_ = NULL;
    srs_freep(stream.entry_);
}

// SrsIngester::initialize_ffmpeg() asserts that rtmp.listen has an endpoint, to fill [port] in the engine output.
// The config check accepts a config without rtmp.listen, such as a WebRTC or HTTP-only server, and an ingest that
// pushes to another server needs no local RTMP port. So a config with an enabled ingest and no rtmp.listen passes
// the check, and the server aborts at startup in SrsServer::ingest(). The parse should return an error, or build the
// ingester when the output does not use [port].
VOID TEST(MemoryAuditTest, SrsIngester_NoRtmpListenAborts)
{
    const char *ingest_conf = "vhost __defaultVhost__ {"
                              "    ingest livestream {"
                              "        enabled on;"
                              "        input { type file; url ./doc/source.flv; }"
                              "        ffmpeg ./objs/ffmpeg/bin/ffmpeg;"
                              "        engine { enabled off; output rtmp://192.168.1.10/live/livestream; }"
                              "    }"
                              "}";

    // Control: with rtmp.listen, then without it. Parse the ingesters in a child process, which exits 0 only when
    // the parse returns instead of aborting.
    const char *rtmp_confs[] = {_MIN_OK_CONF, ""};
    for (int i = 0; i < (int)(sizeof(rtmp_confs) / sizeof(rtmp_confs[0])); i++) {
        std::string conf_text = std::string(rtmp_confs[i]) + ingest_conf;
        EXPECT_EXIT(
            {
                MockSrsConfig conf;
                srs_error_t err = conf.mock_parse(conf_text);
                if (err != srs_success) {
                    // The config check refused it, so the server would not start: not this bug.
                    srs_freep(err);
                    _exit(2);
                }

                SrsIngester ingester;
                ingester.config_ = &conf;
                err = ingester.parse();
                srs_freep(err);
                _exit(0);
            },
            ::testing::ExitedWithCode(0), "")
            << conf_text;
    }
}

#ifdef SRS_FFMPEG_FIT
// Encode n frames of a 440Hz tone as raw AAC-LC at 44.1kHz with the given channels, the payloads of RTMP audio tags.
static void memory_audit_encode_raw_aac(int channels, int n, vector<string> &frames)
{
    const AVCodec *codec = avcodec_find_encoder_by_name("aac");
    ASSERT_TRUE(codec != NULL);

    AVCodecContext *enc = avcodec_alloc_context3(codec);
    ASSERT_TRUE(enc != NULL);
    enc->sample_rate = 44100;
    enc->channels = channels;
    enc->channel_layout = av_get_default_channel_layout(channels);
    enc->sample_fmt = AV_SAMPLE_FMT_FLTP;
    enc->bit_rate = 64000;
    enc->time_base.num = 1;
    enc->time_base.den = 44100;
    ASSERT_GE(avcodec_open2(enc, codec, NULL), 0);

    AVFrame *frame = av_frame_alloc();
    frame->format = enc->sample_fmt;
    frame->nb_samples = enc->frame_size;
    frame->channel_layout = enc->channel_layout;
    ASSERT_GE(av_frame_get_buffer(frame, 0), 0);
    AVPacket *pkt = av_packet_alloc();

    int64_t pts = 0;
    for (int i = 0; (int)frames.size() < n && i < n + 16; i++) {
        ASSERT_GE(av_frame_make_writable(frame), 0);
        for (int ch = 0; ch < channels; ch++) {
            float *samples = (float *)frame->data[ch];
            for (int s = 0; s < frame->nb_samples; s++) {
                samples[s] = 0.3f * sinf(2.0f * 3.14159265f * 440.0f * (float)(pts + s) / 44100.0f);
            }
        }
        frame->pts = pts;
        pts += frame->nb_samples;
        ASSERT_GE(avcodec_send_frame(enc, frame), 0);

        while (avcodec_receive_packet(enc, pkt) >= 0) {
            frames.push_back(string((char *)pkt->data, pkt->size));
            av_packet_unref(pkt);
        }
    }

    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&enc);
    ASSERT_EQ(n, (int)frames.size());
}

// Feed an RTMP audio tag, the FLV audio header 0xaf (AAC, 44.1kHz, 16 bits, stereo) and the AAC packet type, to the
// RTMP to WebRTC bridge, as the live source does for each message of a publisher.
static srs_error_t memory_audit_rtmp_aac(SrsRtcRtpBuilder *builder, char aac_packet_type, const string &data, int64_t timestamp)
{
    int size = 2 + (int)data.size();
    char *payload = new char[size];
    payload[0] = (char)0xaf;
    payload[1] = aac_packet_type;
    memcpy(payload + 2, data.data(), data.size());

    SrsMediaPacket msg;
    msg.message_type_ = SrsFrameTypeAudio;
    msg.timestamp_ = timestamp;
    msg.wrap(payload, size);
    return builder->on_frame(&msg);
}

// An RTMP publisher to a vhost with `rtmp_to_rtc on` whose AAC goes from stereo to mono, such as an encoder whose
// audio source is switched, sends a new sequence header for 1 channel. SrsRtcRtpBuilder keeps its transcoder, since
// the codec is still AAC, and builds the ADTS header of each frame from the new sequence header, so FFmpeg's AAC
// decoder outputs mono frames. SrsAudioTranscoder made its resampler once, from the first decoded frame, for 2 input
// channels, so swr_convert() reads the second channel's plane from the mono frame's data[1], which is NULL, and the
// server crashes. A frame whose channels, layout or rate differ from the resampler's should rebuild it, or be refused.
VOID TEST(MemoryAuditTest, SrsAudioTranscoder_StereoToMonoAacCrashes)
{
    srs_error_t err;

    vector<string> stereo, mono;
    memory_audit_encode_raw_aac(2, 10, stereo);
    memory_audit_encode_raw_aac(1, 10, mono);

    SrsSharedPtr<SrsRtcSource> rtc_source(new SrsRtcSource());
    SrsUniquePtr<MockStreamBridgeRequest> req(new MockStreamBridgeRequest());
    HELPER_ASSERT_SUCCESS(rtc_source->initialize(req.get()));

    MockRtpTarget rtp_target;
    SrsRtcRtpBuilder builder(_srs_app_factory, &rtp_target, rtc_source);
    builder.assemble();
    HELPER_ASSERT_SUCCESS(builder.initialize(req.get()));

    // AAC-LC 44.1kHz stereo: the sequence header, then frames, transcoded to Opus for the WebRTC players.
    int64_t timestamp = 0;
    HELPER_ASSERT_SUCCESS(memory_audit_rtmp_aac(&builder, 0x00, string("\x12\x10", 2), timestamp));
    for (int i = 0; i < (int)stereo.size(); i++, timestamp += 23) {
        HELPER_ASSERT_SUCCESS(memory_audit_rtmp_aac(&builder, 0x01, stereo[i], timestamp));
    }
    ASSERT_GT(rtp_target.on_rtp_count_, 0);

    // Control: a new stereo sequence header and more stereo frames transcode as before.
    HELPER_ASSERT_SUCCESS(memory_audit_rtmp_aac(&builder, 0x00, string("\x12\x10", 2), timestamp));
    for (int i = 0; i < (int)stereo.size(); i++, timestamp += 23) {
        HELPER_ASSERT_SUCCESS(memory_audit_rtmp_aac(&builder, 0x01, stereo[i], timestamp));
    }

    // AAC-LC 44.1kHz mono: the sequence header, then frames. The first one the decoder outputs crashes the resampler.
    HELPER_ASSERT_SUCCESS(memory_audit_rtmp_aac(&builder, 0x00, string("\x12\x08", 2), timestamp));
    for (int i = 0; i < (int)mono.size(); i++, timestamp += 23) {
        err = memory_audit_rtmp_aac(&builder, 0x01, mono[i], timestamp);
        srs_freep(err);
    }
}
#endif

#ifndef _WIN32 // Windows deletes no open file.
// A DASH video segment whose tmp file is gone when it ends, as when the operator removes the stream's dir or a cleanup
// job deletes it. The reap flushes the segment encoder, frees the file writer, then fails to rename the tmp file. The
// controller returns the error, and the hub stops DASH with on_unpublish, which reaps the same segment again: its
// encoder still holds the freed writer and writes the sidx through it, a heap-use-after-free in
// SrsMp4M2tsSegmentEncoder::flush. The encoder should not keep a writer its owner freed.
VOID TEST(MemoryAuditTest, SrsMp4M2tsSegmentEncoder_ReapAfterFailedRenameUsesFreedWriter)
{
    srs_error_t err;

    // Open the segment as SrsFragmentedMp4::initialize does, and write one key frame to it.
    SrsUniquePtr<SrsFragmentedMp4> fragment(new SrsFragmentedMp4());
    fragment->set_path(_srs_tmp_file_prefix + "memory-audit-dash-video-0.m4s");
    HELPER_ASSERT_SUCCESS(fragment->fw_->open(fragment->tmppath()));
    HELPER_ASSERT_SUCCESS(fragment->enc_->initialize(fragment->fw_, 0, 0, 1));

    uint8_t frame[] = {0x00, 0x00, 0x00, 0x04, 0x65, 0x88, 0x84, 0x00};
    HELPER_ASSERT_SUCCESS(fragment->enc_->write_sample(SrsMp4HandlerTypeVIDE, SrsVideoAvcFrameTypeKeyFrame, 0, 0, frame, sizeof(frame)));
    fragment->append(0);
    fragment->append(1000);

    // The tmp file goes away before the segment ends.
    ::unlink(fragment->tmppath().c_str());

    // The controller's reap on the next key frame flushes, closes the writer, and fails to rename.
    uint64_t dts = 1000;
    err = fragment->reap(dts);
    EXPECT_EQ(ERROR_SYSTEM_FRAGMENT_RENAME, srs_error_code(err));
    srs_freep(err);

    // The hub's on_unpublish reaps the same segment again, which fails on the closed writer.
    err = fragment->reap(dts);
    EXPECT_TRUE(err != srs_success);
    srs_freep(err);
}
#endif

// An edge that pulls HTTP-FLV from its origin turns each FLV tag into a message, an audio tag of size 0 too, and
// SrsLiveSource::on_audio, unlike on_video and SrsProtocol, does not drop the empty one. SrsFormat::on_audio returns
// success on it before initialize, so the parsed audio packet keeps the previous frame's samples, which point into
// that frame's payload, freed once its last copy is sent. The source, its hub (HLS, DASH, DVR) and the RTMP to RTC
// bridge then read those samples as the current frame: aac_raw_append_adts_header copies them, a heap-use-after-free.
VOID TEST(MemoryAuditTest, SrsParsedPacket_EmptyAudioKeepsFreedSamples)
{
    srs_error_t err;

    SrsRtmpFormat format;
    HELPER_ASSERT_SUCCESS(format.initialize());

    // AAC LC 44.1 kHz stereo sequence header, then one raw frame, each in its own payload.
    char sh[] = {(char)0xaf, 0x00, 0x12, 0x10};
    SrsUniquePtr<SrsMediaPacket> sh_pkt(new SrsMediaPacket());
    sh_pkt->message_type_ = SrsFrameTypeAudio;
    char *sh_payload = new char[sizeof(sh)];
    memcpy(sh_payload, sh, sizeof(sh));
    sh_pkt->wrap(sh_payload, sizeof(sh));
    HELPER_ASSERT_SUCCESS(format.on_audio(sh_pkt.get()));

    char raw[] = {(char)0xaf, 0x01, 0x21, 0x10, 0x04, 0x60, (char)0x8c, 0x1c};
    SrsMediaPacket *raw_pkt = new SrsMediaPacket();
    raw_pkt->message_type_ = SrsFrameTypeAudio;
    char *raw_payload = new char[sizeof(raw)];
    memcpy(raw_payload, raw, sizeof(raw));
    raw_pkt->wrap(raw_payload, sizeof(raw));
    HELPER_ASSERT_SUCCESS(format.on_audio(raw_pkt));
    ASSERT_EQ(1, format.audio_->nb_samples_);

    // The frame is sent and its last copy freed, which frees the payload the sample points into.
    srs_freep(raw_pkt);

    // The empty audio tag, made as SrsEdgeFlvUpstream::recv_message makes it.
    SrsRtmpCommonMessage *empty_raw = NULL;
    HELPER_ASSERT_SUCCESS(srs_rtmp_create_msg(SrsFrameTypeAudio, 46, new char[0], 0, 1, &empty_raw));
    SrsUniquePtr<SrsRtmpCommonMessage> empty(empty_raw);
    SrsMediaPacket empty_pkt;
    empty->to_msg(&empty_pkt);

    HELPER_EXPECT_SUCCESS(format.on_audio(&empty_pkt));
    // The empty message has no sample, and the packet no longer holds the freed frame's.
    EXPECT_EQ(0, format.audio_->nb_samples_);
    EXPECT_TRUE(format.raw_ == NULL);
    EXPECT_EQ(0, format.nb_raw_);

    // The RTMP to RTC bridge drops the message for no sample, instead of reading the freed payload.
    char *buf = NULL;
    int nn_buf = 0;
    HELPER_EXPECT_SUCCESS(aac_raw_append_adts_header(&empty_pkt, &format, &buf, &nn_buf));
    EXPECT_TRUE(buf == NULL);
    srs_freepa(buf);
}

// Sends the datagram from the client to the server's UDP port, and receives it on the server's socket, as the RTC
// server's UDP listener coroutine does before it dispatches the packet.
static srs_error_t memory_audit_udp_deliver(srs_netfd_t client, int server_port, SrsUdpMuxSocket *skt, const std::string &datagram)
{
    sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(server_port);
    to.sin_addr.s_addr = inet_addr("127.0.0.1");

    int n = srs_sendto(client, (void *)datagram.data(), (int)datagram.size(), (sockaddr *)&to, sizeof(to), SRS_UTIME_NO_TIMEOUT);
    if (n != (int)datagram.size()) {
        return srs_error_new(ERROR_SOCKET_WRITE, "sendto %d", n);
    }

    n = skt->recvfrom(SRS_UTIME_SECONDS);
    if (n != (int)datagram.size()) {
        return srs_error_new(ERROR_SOCKET_READ, "recvfrom %d", n);
    }
    return srs_success;
}

// A WHIP or WHEP client deletes its session, which removes it from the RTC server's resource manager, while its ICE
// checks still come in. remove() only queues the session and signals the manager coroutine; until that coroutine
// runs, find_by_fast_id and find_by_name still return the session, and the UDP dispatcher does not check
// is_disposing(). The UDP listener coroutine already holds the next check, so it runs before the manager, finds the
// removed session and answers the check on the session's sendonly socket. That socket yields on every 21st send, the
// manager frees the session in do_clear, and SrsRtcUdpNetwork::on_binding_request then reads its freed state_. The
// dispatcher should skip a disposing session, or the manager should not free a session in use.
VOID TEST(MemoryAuditTest, SrsResourceManager_RemovedRtcSessionFreedDuringStunResponse)
{
    srs_error_t err;

    // The session switches this coroutine to its context; keep ours for the tests that run after.
    SrsContextRestore(_srs_context->get_id());

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server { enabled on; listen 8000; candidate 127.0.0.1; }"));

    // The RTC server's resource manager, with its own coroutine as the server starts it, waiting for a removal.
    SrsResourceManager manager("utest-rtc");
    manager.assemble();
    HELPER_ASSERT_SUCCESS(manager.start());
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    SrsRtcSessionManager sessions;
    sessions.conn_manager_ = &manager;
    sessions.config_ = &conf;

    // A session whose answer was sent, found by the username of its ICE checks, as create_rtc_session makes it. The
    // manager owns and frees it.
    std::string username = "srvufrag:cliufrag";
    std::string answer_ice("utest-answer-ice-1234567");
    MockRtcAsyncTaskExecutor exec;
    MockRtcAsyncCallRequest req("__defaultVhost__", "live", "livestream");
    SrsRtcConnection *session = new SrsRtcConnection(&exec, SrsContextId());
    session->conn_manager_ = &manager;
    session->config_ = &conf;
    session->assemble();
    session->local_sdp_.session_info_.ice_pwd_.assign(answer_ice);
    HELPER_ASSERT_SUCCESS(session->initialize(&req, false, false, username));
    manager.add_with_name(username, session);

    SrsRtcUdpNetwork *network = dynamic_cast<SrsRtcUdpNetwork *>(session->udp());
    network->conn_manager_ = &manager;
    network->set_state(SrsRtcNetworkStateEstablished);

    // The server's UDP port, and the client's address.
    int port = 0;
    srs_netfd_t server_fd = NULL;
    HELPER_ASSERT_SUCCESS(srs_utest_udp_listen("127.0.0.1", &port, &server_fd));
    SrsUdpMuxSocket *skt = new SrsUdpMuxSocket(server_fd);
    srs_netfd_t client_fd = NULL;
    HELPER_ASSERT_SUCCESS(srs_udp_listen("127.0.0.1", 0, &client_fd));

    // The client's checks so far, each answered on the session's sendonly socket for the client's address: 20 sends,
    // so the next send on it yields.
    std::string check = stun_controlling_binding_request(username, answer_ice, true);
    for (int i = 0; i < 20; i++) {
        HELPER_ASSERT_SUCCESS(memory_audit_udp_deliver(client_fd, port, skt, check));
        HELPER_ASSERT_SUCCESS(sessions.on_udp_packet(skt));
    }

    // The next check is ready on the listener when the client's DELETE removes the session.
    HELPER_ASSERT_SUCCESS(memory_audit_udp_deliver(client_fd, port, skt, check));
    manager.remove(session);
    EXPECT_TRUE(session == manager.find_by_name(username));

    // The listener coroutine dispatches the check before the manager runs.
    err = sessions.on_udp_packet(skt);
    srs_freep(err);

    // The manager has freed the session.
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    EXPECT_TRUE(manager.empty());
    EXPECT_TRUE(NULL == manager.find_by_name(username));

    srs_freep(skt);
    srs_close_stfd(client_fd);
    srs_close_stfd(server_fd);
}

// A WebRTC publisher's TWCC sequence numbers come in any order within one 100ms feedback interval; the server parses
// them before SRTP. SrsRtcpTWCC keeps them in a std::set ordered by SrsSeqCompareLess, a circular comparison that is
// not a strict weak order once the numbers span more than half the circle (a < b < c < a), so the tree can lose its
// order and find() can miss a number it holds. When a feedback fills the datagram, do_encode saves the next number as
// next_base_sn_, and the next feedback looks it up with find() and dereferences the result with no end() check: a
// read through the end iterator, then of the received-time map's end node. Each burst below is 800 packets, 8000
// packets a second, with seqs from a fixed generator; a few of them leave next_base_sn_ that find() cannot see.
VOID TEST(MemoryAuditTest, SrsRtcpTWCC_NextBaseSeqMissedByCircularSet)
{
    int nn_splits = 0;
    int nn_failed = 0;
    for (uint32_t seed = 1; seed <= 200; seed++) {
        SrsRtcpTWCC twcc(0x2001);
        twcc.set_media_ssrc(0xb001);

        srs_utime_t t = 640 * SRS_UTIME_MILLISECONDS;
        uint32_t x = seed;
        for (int i = 0; i < 800; i++) {
            x = x * 1103515245u + 12345u;
            srs_error_t err = twcc.recv_packet((uint16_t)(x >> 16), t);
            srs_freep(err); // A repeated seq is refused, as the server does.
            t += 125;
        }

        // The feedback loop of SrsRtcPublishStream::send_periodic_twcc, one datagram each.
        for (int i = 0; i < 1024 && twcc.need_feedback(); ++i) {
            char pkt[kMaxUDPDataSize];
            SrsBuffer buffer(pkt, sizeof(pkt));
            // A feedback that cannot find its next base fails, and drops the rest of this round.
            srs_error_t err = twcc.encode(&buffer);
            if (err != srs_success) {
                EXPECT_EQ(ERROR_RTC_RTCP, srs_error_code(err));
                srs_freep(err);
                nn_failed++;
                break;
            }

            if (!twcc.has_next_base_sn_) {
                break;
            }
            nn_splits++;
        }
    }

    EXPECT_LT(0, nn_splits);
    EXPECT_LT(0, nn_failed);
}

// A network that encrypts like SrsSRTP::protect_rtp: with the AES-CM HMAC-SHA1-80 policy SrsSRTP::initialize sets,
// libsrtp's srtp_protect appends a 10-byte auth tag in place right after the packet and trusts the caller to leave
// room for it. libsrtp is not built with the sanitizer, so the test writes the tag itself where ASan can see it.
class MemoryAuditSrtpTrailerNetwork : public MockRtcNetwork
{
public:
    virtual srs_error_t protect_rtp(void *packet, int *nb_cipher)
    {
        memset((char *)packet + *nb_cipher, 0xab, 10);
        *nb_cipher += 10;
        return srs_success;
    }
};

// A WHIP publisher sends an RTP packet whose plaintext after SRTP decryption is 1500 bytes. The UDP read buffer is
// 64 KB and SrsRtpPacket::wrap keeps any size, so the publisher's packet is decoded whole, and SrsRtcSource::on_rtp
// gives each player a copy. SrsRtcConnection::do_send_packet encodes the copy into its 1500-byte (kRtpPacketSize)
// heap buffer; SrsRtpPacket::encode checks only that the header, payload and padding fit, which they do exactly, then
// protect_rtp appends the 10-byte SRTP auth tag past the end of the buffer, a heap overflow driven by the publisher.
// Any plaintext from 1491 to 1500 bytes overflows; one over 1500 fails the encode instead. A player with RTX answers
// a NACK with 2 more bytes, so a 1489-byte packet overflows there, and 1490 bytes fit a TCP publisher's frame. The
// send buffer should keep room for the SRTP trailer, or the encode should refuse a packet that leaves none.
VOID TEST(MemoryAuditTest, SrsRtpPacket_FullSizePacketOverflowsSrtpTrailer)
{
    srs_error_t err;

    // The publisher's plaintext: a 12-byte header, version 2, no padding, no extension, then 1488 bytes of payload.
    char plaintext[kRtpPacketSize];
    memset(plaintext, 0x42, sizeof(plaintext));
    SrsBuffer hb(plaintext, kRtpHeaderFixedSize);
    hb.write_1bytes(0x80);
    hb.write_1bytes(96);
    hb.write_2bytes(1000);
    hb.write_4bytes(90000);
    hb.write_4bytes(0x12345678);

    // Decode it as SrsRtcPublishStream::on_rtp_plaintext does, and copy it as the source does for each player.
    SrsUniquePtr<SrsRtpPacket> pkt(new SrsRtpPacket());
    char *p = pkt->wrap(plaintext, sizeof(plaintext));
    SrsBuffer buf(p, sizeof(plaintext));
    HELPER_ASSERT_SUCCESS(pkt->decode(&buf));
    SrsUniquePtr<SrsRtpPacket> cp(pkt->copy());
    EXPECT_EQ(kRtpPacketSize, (int)cp->nb_bytes());

    MockRtcAsyncTaskExecutor exec;
    SrsContextId cid;
    cid.set_value("memory-audit-srtp-trailer");
    MemoryAuditSrtpTrailerNetwork network;
    network.is_established_ = true;
    MockRtcNetworksForConnection networks(&network);

    SrsUniquePtr<SrsRtcConnection> conn(new SrsRtcConnection(&exec, cid));
    srs_freep(conn->networks_);
    conn->networks_ = &networks;

    // The player's send encodes the copy and encrypts it in place.
    err = conn->do_send_packet(cp.get());
    conn->networks_ = NULL;
    HELPER_EXPECT_SUCCESS(err);
    EXPECT_EQ(1, network.write_count_);
}

// Build a 23-byte RTP packet, the smallest srs_rtp_fast_parse_twcc accepts: the 12-byte header with the extension
// bit, a one-byte header extension (0xBEDE) of one 4-byte word, and 3 payload bytes. The heap block is exactly the
// packet, like the end of the UDP or TCP read buffer when the datagram or frame fills it.
static char *memory_audit_build_twcc_rtp(const uint8_t ext[4])
{
    const uint8_t rtp[23] = {
        0x90, 0x60, 0x12, 0x34,             // V=2, X=1, CC=0, PT=96, seq=0x1234
        0x00, 0x00, 0x00, 0x01,             // timestamp
        0x12, 0x34, 0x56, 0x78,             // SSRC
        0xBE, 0xDE, 0x00, 0x01,             // one-byte header extension, length 1 word
        ext[0], ext[1], ext[2], ext[3],     // the extension elements
        0x00, 0x00, 0x00                    // payload
    };
    char *buf = new char[sizeof(rtp)];
    memcpy(buf, rtp, sizeof(rtp));
    return buf;
}

// The publisher reads the TWCC sequence number from every packet before SRTP unprotect, so with TWCC negotiated
// any packet from the peer's address reaches srs_rtp_fast_parse_twcc. Its element walk skips a non-TWCC element
// by its length and subtracts that length from the uint16_t extension_length without checking it fits: an element
// claiming 16 bytes in a 4-byte extension wraps the count to 65523, and the walk goes on reading past the packet
// until a byte names the TWCC id (then it reads two more bytes as the sequence number) or memory ends.
VOID TEST(MemoryAuditTest, SrsRtpFastParseTwcc_ElementPastExtensionReadsOutOfBounds)
{
    srs_error_t err;
    uint8_t twcc_id = 3;

    // A TWCC element (id 3, L=1) in the extension parses in bounds.
    if (true) {
        const uint8_t ext[4] = {0x31, 0xAB, 0xCD, 0x00};
        SrsUniquePtr<char[]> buf(memory_audit_build_twcc_rtp(ext));
        uint16_t twcc_sn = 0;
        HELPER_ASSERT_SUCCESS(srs_rtp_fast_parse_twcc(buf.get(), 23, twcc_id, twcc_sn));
        EXPECT_EQ(0xABCD, twcc_sn);
    }

    // An element of id 1 claiming 16 bytes (L=15) in the 4-byte extension is refused, not skipped past the packet.
    if (true) {
        const uint8_t ext[4] = {0x1F, 0x00, 0x00, 0x00};
        SrsUniquePtr<char[]> buf(memory_audit_build_twcc_rtp(ext));
        uint16_t twcc_sn = 0;
        HELPER_EXPECT_FAILED(srs_rtp_fast_parse_twcc(buf.get(), 23, twcc_id, twcc_sn));
    }
}

// Records the deepest stack address reached when mkdir is called, and creates nothing.
static char *memory_audit_mkdir_lowest_stack = NULL;
static int memory_audit_mkdir_calls = 0;
static int memory_audit_mock_mkdir(const char *path)
{
    char marker = 0;
    char *addr = &marker;
    if (!memory_audit_mkdir_lowest_stack || addr < memory_audit_mkdir_lowest_stack) {
        memory_audit_mkdir_lowest_stack = addr;
    }
    memory_audit_mkdir_calls++;
    return 0;
}

// HLS and DASH create the m3u8 dir by SrsPath::mkdir_all on publish, and the dir holds the client's app and
// stream, which may contain slashes and run to 64 KB in an RTMP publish, or longer in an HTTP or WHIP URL. The
// helper recurses once per missing parent before the first mkdir, and stat fails for a path over PATH_MAX, so
// the depth is the number of slashes. The publish runs on an ST coroutine with a 128 KB or 256 KB stack, so a
// stream name of 4000 short dirs runs past the coroutine stack into its guard page and crashes the server. Each
// level also holds two copies of its whole prefix on the heap. The depth should be bounded.
VOID TEST(MemoryAuditTest, SrsDoCreateDirRecursively_DeepStreamOverflowsCoroutineStack)
{
    srs_error_t err = srs_success;

    const int nb_dirs = 4000;
    std::string dir = "/srs-memory-audit-missing/live";
    for (int i = 0; i < nb_dirs; i++) {
        dir += "/a";
    }
    ASSERT_FALSE(SrsPath().exists("/srs-memory-audit-missing"));

    extern srs_mkdir_t _srs_mkdir_fn;
    srs_mkdir_t old = _srs_mkdir_fn;
    _srs_mkdir_fn = memory_audit_mock_mkdir;
    memory_audit_mkdir_lowest_stack = NULL;
    memory_audit_mkdir_calls = 0;

    char top = 0;
    err = SrsPath().mkdir_all(dir);
    _srs_mkdir_fn = old;
    HELPER_EXPECT_SUCCESS(err);

    EXPECT_EQ(nb_dirs + 2, memory_audit_mkdir_calls);
    ASSERT_TRUE(memory_audit_mkdir_lowest_stack != NULL);
    int depth = (int)(&top - memory_audit_mkdir_lowest_stack);
    EXPECT_GT(128 * 1024, depth);
}

// An RTMP ping request on chunk stream 2, fmt 0: a 6-byte user control message, event 6 with a 4-byte timestamp.
static void memory_audit_append_rtmp_ping(MockBufferIO *io, uint8_t ts)
{
    uint8_t ping[] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x04, 0x00, 0x00, 0x00, 0x00,
                      0x00, 0x06, 0x00, 0x00, 0x00, ts};
    io->append(ping, sizeof(ping));
}

// A player's socket whose write switches to the receive coroutine, as st_writev does when the player's TCP window is
// full: on the chosen write, the queue receive thread reads the one message the player sent meanwhile. It reads only
// one, because SrsQueueRecvThread waits until the play coroutine pumps it.
class MemoryAuditPingDuringWriteIO : public MockBufferIO
{
public:
    SrsProtocol *proto_;
    int nb_writes_;
    int switch_at_;

public:
    MemoryAuditPingDuringWriteIO()
    {
        proto_ = NULL;
        nb_writes_ = 0;
        switch_at_ = 0;
    }

public:
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t *nwrite)
    {
        if (++nb_writes_ == switch_at_ && proto_) {
            SrsRtmpCommonMessage *msg = NULL;
            srs_error_t err = proto_->recv_message(&msg);
            srs_freep(err);
            srs_freep(msg);
        }
        return MockBufferIO::writev(iov, iov_size, nwrite);
    }
};

// During RTMP play, SrsQueueRecvThread turns the auto response off, so the receive coroutine queues the
// acknowledgement and the ping response for a player's ping in manual_response_queue_, and the play coroutine flushes
// the queue after it sends media. SrsProtocol::manual_response_flush() keeps a vector iterator across
// do_send_and_free_packet(), whose write switches coroutines when the player's socket is full. A player that set a
// small acknowledgement window and sends a ping meanwhile makes the receive coroutine push two more responses, the
// full vector grows into a new buffer, and the flush then compares and reads its iterator into the freed one: a
// heap-use-after-free, and the stale pointer it reads is a response already sent and freed.
VOID TEST(MemoryAuditTest, SrsProtocol_PingDuringManualFlushUsesFreedQueue)
{
    srs_error_t err = srs_success;

    MemoryAuditPingDuringWriteIO io;
    SrsProtocol proto(&io);
    proto.set_auto_response(false);

    // The player sets a 1-byte acknowledgement window, so every message it sends is acknowledged, then pings twice.
    uint8_t ack_size[] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x05, 0x00, 0x00, 0x00, 0x00,
                          0x00, 0x00, 0x00, 0x01};
    io.append(ack_size, sizeof(ack_size));
    memory_audit_append_rtmp_ping(&io, 1);
    memory_audit_append_rtmp_ping(&io, 2);

    // The window and the first ping arrive before the flush: an acknowledgement and a ping response fill the queue.
    for (int i = 0; i < 2; i++) {
        SrsRtmpCommonMessage *msg_raw = NULL;
        HELPER_ASSERT_SUCCESS(proto.recv_message(&msg_raw));
        SrsUniquePtr<SrsRtmpCommonMessage> msg(msg_raw);
    }
    ASSERT_EQ(2, (int)proto.manual_response_queue_.size());
    ASSERT_EQ(2, (int)proto.manual_response_queue_.capacity());

    // The play coroutine sends a video message, the first write, then flushes; the second ping arrives while the
    // first response, the second write, waits for the player's socket.
    io.proto_ = &proto;
    io.switch_at_ = 2;

    SrsMediaPacket *video = new SrsMediaPacket();
    video->message_type_ = SrsFrameTypeVideo;
    char *payload = new char[16];
    memset(payload, 0, 16);
    video->wrap(payload, 16);

    HELPER_EXPECT_SUCCESS(proto.send_and_free_messages(&video, 1, 1));

    EXPECT_EQ(0, (int)proto.manual_response_queue_.size());
}

// SrsCallPacket::decode() leaves arguments_ NULL when a call ends after its command object, such as an origin's
// onBWDone(0, null), and SrsProtocol decodes every AMF0 command it does not know as such a call. An edge pulling RTMP
// passes every command of its origin to SrsEdgeIngester::process_publish_message(), which looks for an RTMP 302 in the
// call's arguments and calls call->arguments_->is_object() with no NULL check: an origin's call without arguments
// crashes the edge on a NULL dereference. The edge should skip a call whose arguments_ is NULL.
VOID TEST(MemoryAuditTest, SrsCallPacket_CallWithoutArgumentsCrashesEdge)
{
    srs_error_t err = srs_success;

    // The origin's onBWDone(0, null): a command name, a transaction id and a null command object, no arguments.
    uint8_t bytes[] = {0x02, 0x00, 0x08, 'o', 'n', 'B', 'W', 'D', 'o', 'n', 'e', 0x00, 0x00, 0x00,
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05};
    SrsUniquePtr<SrsRtmpCommonMessage> msg(new SrsRtmpCommonMessage());
    msg->header_.message_type_ = RTMP_MSG_AMF0CommandMessage;
    msg->header_.payload_length_ = sizeof(bytes);
    msg->create_payload(sizeof(bytes));
    memcpy(msg->payload(), bytes, sizeof(bytes));

    // The edge's RTMP client decodes it as a call without arguments.
    MockBufferIO io;
    SrsProtocol proto(&io);
    SrsRtmpCommand *pkt_raw = NULL;
    HELPER_ASSERT_SUCCESS(proto.decode_message(msg.get(), &pkt_raw));
    SrsCallPacket *call = dynamic_cast<SrsCallPacket *>(pkt_raw);
    ASSERT_TRUE(call != NULL);
    EXPECT_TRUE(call->arguments_ == NULL);

    MockEdgeIngesterFixture f(100);
    MockEdgeUpstreamForIngesterCycle *upstream = new MockEdgeUpstreamForIngesterCycle();
    upstream->factory_ = &f.factory_;
    srs_freep(f.ingester_->upstream_);
    f.ingester_->upstream_ = upstream;
    upstream->packets_.push_back(call);

    // The edge should ignore a call that is no redirect.
    std::string redirect;
    HELPER_EXPECT_SUCCESS(f.ingester_->process_publish_message(msg.get(), redirect));
    EXPECT_TRUE(redirect.empty());
}

// An RTMP publisher's AVC sequence header has a 25-byte SPS and a 1455-byte PPS. SrsFormat::avc_demux_sps_pps
// keeps a PPS of any size up to 64 KB, and SrsRtpVideoBuilder::package_stap_a puts the SPS and PPS of every IDR in
// one STAP-A packet with no size check and no fragmentation, so the packet is 12 + 1 + 2 + 25 + 2 + 1455 = 1497
// bytes. SrsRtcRtpBuilder::on_video hands it to the source, and each WebRTC player's do_send_packet encodes it into
// the 1500-byte send buffer and appends the 10-byte SRTP tag past the end (B21). So RTMP-made packets are not all
// under kRtpMaxPayloadSize: any parameter sets whose STAP-A is 1491 to 1500 bytes overflow, and a larger one fails
// the encode of every key frame's STAP-A. HEVC VPS, SPS and PPS of up to 64 KB each make the same STAP.
VOID TEST(MemoryAuditTest, SrsRtpVideoBuilder_LargeParameterSetsOverflowSrtpTrailer)
{
    srs_error_t err;

    // The FLV AVC sequence header: a real 25-byte High profile SPS, then one PPS of 1455 bytes.
    const int nb_pps = 1455;
    uint8_t head[] = {
        0x17, 0x00, 0x00, 0x00, 0x00, 0x01, 0x64, 0x00, 0x20, 0xff, 0xe1, 0x00, 0x19, 0x67, 0x64, 0x00, 0x20,
        0xac, 0xd9, 0x40, 0xc0, 0x29, 0xb0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00,
        0x32, 0x0f, 0x18, 0x31, 0x96, 0x01};
    std::vector<char> sh(head, head + sizeof(head));
    sh.push_back((char)(nb_pps >> 8));
    sh.push_back((char)(nb_pps & 0xff));
    sh.push_back((char)0x68);
    sh.resize(sh.size() + nb_pps - 1, (char)0xeb);

    SrsFormat format;
    HELPER_ASSERT_SUCCESS(format.initialize());
    HELPER_ASSERT_SUCCESS(format.on_video(0, &sh[0], (int)sh.size()));
    ASSERT_TRUE(format.vcodec_ != NULL);
    EXPECT_EQ(nb_pps, (int)format.vcodec_->pictureParameterSetNALUnit_.size());

    // Package the STAP-A of the next IDR as SrsRtcRtpBuilder::on_video does.
    SrsRtpVideoBuilder builder;
    HELPER_ASSERT_SUCCESS(builder.initialize(&format, 0x12345678, 96));
    SrsMediaPacket msg;
    msg.timestamp_ = 40;
    SrsUniquePtr<SrsRtpPacket> pkt(new SrsRtpPacket());
    HELPER_ASSERT_SUCCESS(builder.package_stap_a(&msg, pkt.get()));
    ASSERT_TRUE(pkt->payload() != NULL);
    EXPECT_EQ(1497, (int)pkt->nb_bytes());

    // The source gives each player a copy, and the player's send encodes and encrypts it in place.
    SrsUniquePtr<SrsRtpPacket> cp(pkt->copy());

    MockRtcAsyncTaskExecutor exec;
    SrsContextId cid;
    cid.set_value("memory-audit-stap-trailer");
    MemoryAuditSrtpTrailerNetwork network;
    network.is_established_ = true;
    MockRtcNetworksForConnection networks(&network);

    SrsUniquePtr<SrsRtcConnection> conn(new SrsRtcConnection(&exec, cid));
    srs_freep(conn->networks_);
    conn->networks_ = &networks;

    err = conn->do_send_packet(cp.get());
    conn->networks_ = NULL;
    HELPER_EXPECT_SUCCESS(err);
    EXPECT_EQ(1, network.write_count_);
}

// A WHIP offer may carry an ssrc-group line with a single SSRC, such as "a=ssrc-group:FEC 2001". The SDP
// parser accepts any group of one or more SSRCs, but the publish negotiation reads the second SSRC of every
// FEC group, and of every FID group when the track has RTX, without checking the group size. That reads
// just past the one-SSRC vector the parser built; the utest build's standard library bounds checks abort
// on the read, where a server build takes the bytes after the vector as the FEC SSRC.
VOID TEST(MemoryAuditTest, SrsSSRCGroup_OneSsrcFecGroupReadsPastVector)
{
    srs_error_t err;

    SrsUniquePtr<SrsRtcPublisherNegotiator> negotiator(new SrsRtcPublisherNegotiator());
    SrsUniquePtr<MockRtcConnectionRequest> request(new MockRtcConnectionRequest("test.vhost", "live", "stream1"));

    SrsUniquePtr<SrsRtcUserConfig> ruc(new SrsRtcUserConfig());
    srs_freep(ruc->req_);
    ruc->req_ = request->copy();
    ruc->publish_ = true;
    ruc->dtls_ = true;
    ruc->srtp_ = true;
    ruc->audio_before_video_ = true;
    ruc->remote_sdp_str_ =
        "v=0\r\n"
        "o=- 123456789 2 IN IP4 127.0.0.1\r\n"
        "s=-\r\n"
        "t=0 0\r\n"
        "a=group:BUNDLE 0\r\n"
        "a=msid-semantic: WMS stream\r\n"
        "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=rtcp:9 IN IP4 0.0.0.0\r\n"
        "a=ice-ufrag:test\r\n"
        "a=ice-pwd:testpassword\r\n"
        "a=fingerprint:sha-256 AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99\r\n"
        "a=setup:actpass\r\n"
        "a=mid:0\r\n"
        "a=sendonly\r\n"
        "a=rtcp-mux\r\n"
        "a=rtpmap:96 H264/90000\r\n"
        "a=fmtp:96 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f\r\n"
        "a=ssrc-group:FEC 2001\r\n"
        "a=ssrc:2001 cname:test-video\r\n"
        "a=ssrc:2001 msid:stream video\r\n";
    HELPER_EXPECT_SUCCESS(ruc->remote_sdp_.parse(ruc->remote_sdp_str_));
    ASSERT_EQ(1, (int)ruc->remote_sdp_.media_descs_.size());
    ASSERT_EQ(1, (int)ruc->remote_sdp_.media_descs_[0].ssrc_groups_.size());
    ASSERT_EQ(1, (int)ruc->remote_sdp_.media_descs_[0].ssrc_groups_[0].ssrcs_.size());

    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    HELPER_EXPECT_SUCCESS(negotiator->negotiate_publish_capability(ruc.get(), stream_desc.get()));
    ASSERT_EQ(1, (int)stream_desc->video_track_descs_.size());

    // The group names no FEC SSRC, so the track has none.
    EXPECT_EQ(0, (int)stream_desc->video_track_descs_[0]->fec_ssrc_);
}

// An HTTP-FLV edge follows an origin's 302 Location by parsing it into a local schema, host and port, and then
// connects to them. The port is a local int with no initial value, and the URL parser returns early on a URL it
// refuses, such as a Location without "://", leaving every output as it was. So the edge connects to an empty
// host and schema at whatever port the stack held, three times, instead of failing the redirect. A port of
// eight or more digits there aborts the server in srs_tcp_connect, like an origin's long redirect port.
VOID TEST(MemoryAuditTest, SrsNetUrlParseTcurl_EdgeRedirectUninitializedPort)
{
    // The parser leaves the outputs as they were when it refuses the URL.
    std::string schema, host, vhost, app, stream = "stream2.flv", param;
    int port = 12345678;
    srs_net_url_parse_tcurl("10.0.0.2:8081/live2", schema, host, vhost, app, stream, port, param);
    EXPECT_EQ(12345678, port);
    EXPECT_TRUE(host.empty());

    MockAppConfigForEdgeIngester config;
    config.origin_ = new SrsConfDirective();
    config.origin_->name_ = "origin";
    config.origin_->args_.push_back("127.0.0.1:8080");
    MockAppFactoryForEdgeIngester factory;
    factory.location_ = "10.0.0.2:8081/live2/stream2.flv";

    SrsUniquePtr<SrsEdgeFlvUpstream> upstream(new SrsEdgeFlvUpstream("http"));
    upstream->config_ = &config;
    upstream->app_factory_ = &factory;

    MockSrsRequest req("test.vhost", "live", "livestream");
    SrsLbRoundRobin lb;
    srs_error_t err = upstream->connect(&req, &lb);
    srs_freep(err);

    // The edge should refuse the redirect after the first request, not connect to the unparsed Location.
    ASSERT_LE(1, (int)factory.http_hosts_.size());
    EXPECT_EQ("127.0.0.1", factory.http_hosts_.at(0));
    EXPECT_EQ(8080, factory.http_ports_.at(0));
    EXPECT_EQ(1, (int)factory.http_hosts_.size());
    for (int i = 1; i < (int)factory.http_hosts_.size(); i++) {
        EXPECT_FALSE(factory.http_hosts_.at(i).empty()) << i << " port=" << factory.http_ports_.at(i);
        EXPECT_FALSE(factory.http_schemas_.at(i).empty()) << i;
    }

    upstream->config_ = NULL;
    upstream->app_factory_ = NULL;
}

// The config check warns when a chunk size is outside 128 to 65536, so a start, a "-t" test or a reload of a
// config with such a chunk size should log a warning and go on. But the warning prints the int chunk size with
// "%s", so the logger reads the number as a string pointer, and the server crashes at that address. The global
// warning has it for "rtmp { chunk_size }" or SRS_VHOST_CHUNK_SIZE, the vhost warning for "vhost { chunk_size }",
// where it prints the global chunk size instead of the vhost's.
VOID TEST(MemoryAuditTest, SrsConfig_ChunkSizeWarningReadsIntAsString)
{
    // Check each config in a child process, which exits 0 only when the check returns instead of crashing.
    const char *confs[] = {
        "rtmp {chunk_size 100;}",
        "vhost v {chunk_size 70000;}",
    };
    for (int i = 0; i < (int)(sizeof(confs) / sizeof(confs[0])); i++) {
        std::string conf_text = confs[i];
        EXPECT_EXIT(
            {
                // Format the warnings as the server's file log does; the default empty log drops them.
                ISrsLog *old_log = _srs_log;
                MockLogForSignal log;
                _srs_log = &log;

                MockSrsConfig conf;
                srs_error_t err = conf.mock_parse(conf_text);
                srs_freep(err);

                _srs_log = old_log;
                _exit(log.warn_count_ > 0 ? 0 : 1);
            },
            ::testing::ExitedWithCode(0), "")
            << conf_text;
    }
}

// An SRS 4 GB28181 config puts "host" directly in "stream_caster". The transform moves it to "sip { candidate; }",
// but it creates "sip" by a push_back into the stream caster's children while it still holds an iterator into
// them, then erases "host" by that iterator. When "sip" is missing and the children fill the vector's capacity,
// the push_back moves them to a new buffer, so the erase works on the freed one. Four children fill it, so a
// start or reload of this config reads and writes freed memory.
VOID TEST(MemoryAuditTest, SrsConfigTransformVhost_GbHostToSipUsesStaleIterator)
{
    MockSrsConfig conf;
    srs_error_t err = conf.mock_parse("stream_caster {enabled on; caster gb28181; listen 9000; host 192.168.1.10;}");
    srs_freep(err);
}

// SrsHlsSegment's constructor leaves iv_ unset; only config_cipher() fills it, and the muxer calls that only for the
// segment it opens. With hls_keys and hls_recover (on by default) both on, a republish recovers the window from the
// m3u8 on disk: do_recover_hls() makes a segment per #EXTINF entry and skips the #EXT-X-KEY lines, so every recovered
// segment keeps the 16 uninitialized bytes of its heap block. The next m3u8 refresh writes them, hex encoded, as the
// IV of each recovered segment at a key boundary: the server's heap bytes go into a public playlist, and a player
// decrypting with that IV gets garbage instead of the segment the previous publish encrypted with the original IV.
VOID TEST(MemoryAuditTest, SrsHlsSegment_RecoveredSegmentWritesUninitializedIv)
{
    srs_error_t err;

    // The m3u8 the previous publish wrote with hls_keys on and hls_fragments_per_key 1, each segment with its own IV.
    std::string m3u8_content =
        "#EXTM3U\n"
        "#EXT-X-VERSION:3\n"
        "#EXT-X-TARGETDURATION:10\n"
        "#EXT-X-MEDIA-SEQUENCE:100\n"
        "#EXT-X-KEY:METHOD=AES-128,URI=\"test-100.key\",IV=0x0102030405060708090a0b0c0d0e0f10\n"
        "#EXTINF:10.000,\n"
        "test-100.ts\n"
        "#EXT-X-KEY:METHOD=AES-128,URI=\"test-101.key\",IV=0x1112131415161718191a1b1c1d1e1f20\n"
        "#EXTINF:10.000,\n"
        "test-101.ts\n";

    // The muxer's recover_hls() takes the reader into its own SrsUniquePtr.
    MockSrsFileReader *mock_reader = new MockSrsFileReader(m3u8_content.c_str(), m3u8_content.length());
    SrsUniquePtr<MockAppFactory> mock_factory(new MockAppFactory());
    mock_factory->real_reader_ = mock_reader;

    SrsUniquePtr<SrsHlsMuxer> muxer(new SrsHlsMuxer());
    HELPER_EXPECT_SUCCESS(muxer->initialize());

    MockHlsRequest mock_request_template("__defaultVhost__", "live", "test");
    muxer->req_ = mock_request_template.copy();
    muxer->m3u8_ = "/tmp/live/test.m3u8";
    muxer->m3u8_url_ = "live/test.m3u8";
    muxer->m3u8_dir_ = "/tmp/live";
    muxer->hls_path_ = "/tmp";
    muxer->sequence_no_ = 0;
    muxer->latest_acodec_ = SrsAudioCodecIdAAC;
    muxer->latest_vcodec_ = SrsVideoCodecIdAVC;
    muxer->hls_keys_ = true;
    muxer->hls_fragments_per_key_ = 1;
    muxer->hls_key_file_ = "[stream]-[seq].key";
    muxer->hls_key_file_path_ = "/tmp/live";
    muxer->app_factory_ = mock_factory.get();
    muxer->writer_ = new MockSrsFileWriter();

    HELPER_EXPECT_SUCCESS(muxer->do_recover_hls());
    ASSERT_EQ(2, muxer->segments_->size());

    // Refresh the playlist entry of each recovered segment, as refresh_m3u8() does after the next segment closes.
    std::stringstream ss;
    for (int i = 0; i < muxer->segments_->size(); i++) {
        SrsHlsSegment *seg = dynamic_cast<SrsHlsSegment *>(muxer->segments_->at(i));
        ASSERT_TRUE(seg != NULL);
        HELPER_EXPECT_SUCCESS(muxer->do_refresh_m3u8_segment(seg, ss));
    }
    std::string m3u8 = ss.str();

    // Each recovered segment lists its key with the IV it was encrypted with, which the refresh writes in uppercase.
    EXPECT_NE(std::string::npos, m3u8.find("URI=\"test-100.key\""));
    EXPECT_NE(std::string::npos, m3u8.find("IV=0x0102030405060708090A0B0C0D0E0F10")) << m3u8;
    EXPECT_NE(std::string::npos, m3u8.find("IV=0x1112131415161718191A1B1C1D1E1F20")) << m3u8;

    muxer->app_factory_ = NULL;
    srs_freep(muxer->writer_);
    mock_factory->real_reader_ = NULL;
}

// SrsHlsM4sSegment's constructor leaves iv_ unset; only config_cipher() fills it, and the fMP4 muxer calls that only
// while hls_keys is on. The muxer, and its window of segments, lives as long as the source, across publishes, and each
// publish rereads the config. So a stream publishes with hls_keys off, the operator turns hls_keys on and reloads, and
// the encoder publishes again within the source's cleanup delay: the window still holds the segments of the first
// publish, never encrypted and with 16 uninitialized bytes each. The next m3u8 refresh writes, for each of them at a
// key boundary, a SAMPLE-AES key line whose IV is those bytes: the server's heap bytes go into a public playlist, and a
// player decrypts segments that were never encrypted.
VOID TEST(MemoryAuditTest, SrsHlsM4sSegment_UnencryptedSegmentWritesUninitializedIv)
{
    srs_error_t err;

    MockSrsFileWriter fw;
    MockHlsRequest mock_request("__defaultVhost__", "live", "test");

    SrsUniquePtr<SrsHlsFmp4Muxer> muxer(new SrsHlsFmp4Muxer());
    muxer->req_ = mock_request.copy();
    muxer->m3u8_dir_ = "/tmp/live";
    muxer->hls_key_file_ = "[stream]-[seq].key";
    muxer->hls_key_file_path_ = "/tmp/live";

    // The first publish, with hls_keys off: segment_open() makes the segment and write_hls_key() configures no cipher.
    SrsHlsM4sSegment *segment = new SrsHlsM4sSegment(&fw);
    segment->sequence_no_ = 100;
    segment->set_path("/tmp/live/test-100.m4s");
    muxer->segments_->append(segment);

    // The next publish after the reload: update_config() reads hls_keys on, and the window keeps the old segment.
    muxer->hls_keys_ = true;
    muxer->hls_fragments_per_key_ = 1;

    // Refresh the playlist entry of the old segment, as refresh_m3u8() does after the next segment closes.
    std::stringstream ss;
    HELPER_EXPECT_SUCCESS(muxer->do_refresh_m3u8_segment(segment, ss));
    std::string m3u8 = ss.str();

    // The segment was never encrypted, so its entry must not name a key, nor carry the bytes its heap block held as IV.
    EXPECT_EQ(std::string::npos, m3u8.find("#EXT-X-KEY")) << m3u8;
}

// The TS muxer, like the fMP4 one, lives as long as the source, across publishes: update_config() rereads hls_keys at
// each publish and replaces the writer, but keeps the window. So a stream publishes with hls_keys off, the operator
// turns hls_keys on and reloads, and the encoder publishes again within the source's cleanup delay: the window still
// holds the first publish's segments, never encrypted, each with the 16 uninitialized bytes SrsHlsSegment's constructor
// leaves in iv_. do_refresh_m3u8_segment() looks only at the muxer's hls_keys_, so each m3u8 refresh writes, for every
// one of them at a key boundary, an AES-128 key line whose IV is those bytes, before a segment that was never
// encrypted, and names a key file the muxer never wrote.
VOID TEST(MemoryAuditTest, SrsHlsMuxer_UnencryptedSegmentWritesUninitializedIv)
{
    srs_error_t err;

    MockHlsRequest req("__defaultVhost__", "live", "test");
    SrsUniquePtr<SrsHlsMuxer> muxer(new SrsHlsMuxer());

    // The first publish, with hls_keys off.
    HELPER_ASSERT_SUCCESS(muxer->update_config(&req, "", "./objs/nginx/html", "[app]/[stream].m3u8", "[app]/[stream]-[seq].ts",
                                               10 * SRS_UTIME_SECONDS, 60 * SRS_UTIME_SECONDS, false, 2.1, true, true, false, 1,
                                               "[stream]-[seq].key", "./objs/nginx/html", ""));

    // A segment the first publish closed: segment_open() configured no cipher, and do_segment_close2() freed its
    // muxer and moved it to the window.
    SrsHlsSegment *segment = new SrsHlsSegment(muxer->context_, SrsAudioCodecIdAAC, SrsVideoCodecIdAVC, muxer->writer_);
    segment->sequence_no_ = 100;
    segment->set_path("./objs/nginx/html/live/test-100.ts");
    segment->uri_ = "test-100.ts";
    segment->append(0);
    segment->append(10 * 1000);
    srs_freep(segment->tscw_);
    muxer->segments_->append(segment);

    // The next publish after the reload, with hls_keys on: the window keeps the old segment.
    HELPER_ASSERT_SUCCESS(muxer->update_config(&req, "", "./objs/nginx/html", "[app]/[stream].m3u8", "[app]/[stream]-[seq].ts",
                                               10 * SRS_UTIME_SECONDS, 60 * SRS_UTIME_SECONDS, false, 2.1, true, true, true, 1,
                                               "[stream]-[seq].key", "./objs/nginx/html", ""));
    ASSERT_EQ(1, muxer->segments_->size());

    // Refresh the playlist entry of the old segment, as refresh_m3u8() does after the next segment closes.
    std::stringstream ss;
    HELPER_EXPECT_SUCCESS(muxer->do_refresh_m3u8_segment(segment, ss));
    std::string m3u8 = ss.str();

    // The segment was never encrypted, so its entry must not name a key, nor carry the bytes its heap block held as IV.
    EXPECT_EQ(std::string::npos, m3u8.find("#EXT-X-KEY")) << m3u8;
}

// An HTTPS-FLV player that renegotiates TLS while the server is writing to it. The stream coroutine's
// SrsSslConnection::write() is blocked in the socket write of the TLS record it took from the output BIO,
// and the client's ClientHello wakes the HTTP recv coroutine, which reads the same SSL in pop_message().
class MemoryAuditRenegotiatingTlsPeer : public TlsClientPeer
{
public:
    SrsHttpxConn *conn_;
    // Start the renegotiation during the next server write.
    bool armed_;
    size_t last_write_;
    // The output BIO's buffer when the server's write reached the socket, and after the recv coroutine read.
    char *data_at_write_;
    char *data_after_read_;

public:
    MemoryAuditRenegotiatingTlsPeer() : TlsClientPeer(false)
    {
        conn_ = NULL;
        armed_ = false;
        last_write_ = 0;
        data_at_write_ = data_after_read_ = NULL;
    }

public:
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite)
    {
        last_write_ = size;
        if (armed_) {
            armed_ = false;

            BUF_MEM *bm = NULL;
            BIO_get_mem_ptr(dynamic_cast<SrsSslConnection *>(conn_->ssl_)->bio_out_, &bm);
            data_at_write_ = bm->data;

            // The client starts a renegotiation while the server's write waits in the socket.
            SSL_renegotiate(ssl_);
            SSL_do_handshake(ssl_);
            ERR_clear_error();

            // The recv coroutine reads the ClientHello, and the server answers with its flight, until it waits
            // for the client's next flight.
            srs_error_t r0 = conn_->pop_message(NULL);
            srs_freep(r0);

            BIO_get_mem_ptr(dynamic_cast<SrsSslConnection *>(conn_->ssl_)->bio_out_, &bm);
            data_after_read_ = bm->data;
        }

        // The socket write goes on with the record it was given.
        return TlsClientPeer::write(buf, size, nwrite);
    }
};

// SrsHttpxConn::pop_message() reads the connection's TLS socket from the HTTP recv coroutine, while the
// HTTP-FLV stream coroutine writes to the same SrsSslConnection. The write hands the transport a pointer into
// the output memory BIO, and the socket write can wait for the client. A client that renegotiates then has
// the recv coroutine's SSL_read() append the server's handshake flight to that BIO; when the flight does not
// fit, OpenSSL reallocates the buffer and frees the old one, so the waiting write sends freed heap memory to
// the client. The BIO_reset() after the write then drops the flight, too.
VOID TEST(MemoryAuditTest, SrsHttpxConn_HttpsRenegotiationFreesTheWriteBuffer)
{
    srs_error_t err = srs_success;

    MemoryAuditRenegotiatingTlsPeer *client = new MemoryAuditRenegotiatingTlsPeer();
    SrsUniquePtr<SrsHttpxConn> conn(new SrsHttpxConn(NULL, client, NULL, "127.0.0.1", 8088, "conf/server.key", "conf/server.crt"));
    conn->assemble();
    client->conn_ = conn.get();

    // The TLS handshake, as on_start() does for an HTTPS connection.
    HELPER_ASSERT_SUCCESS(conn->on_start());
    client->drive();
    ASSERT_TRUE(SSL_is_init_finished(client->ssl_));

    // A small write tells the TLS record overhead.
    string probe(100, 'x');
    HELPER_ASSERT_SUCCESS(conn->ssl_->write((void *)probe.data(), probe.size(), NULL));
    int overhead = (int)client->last_write_ - (int)probe.size();
    ASSERT_GT(overhead, 0);

    // A response record just smaller than the output BIO's buffer, such as an FLV tag, so it is written without
    // growing the buffer, and the renegotiation flight does not fit after it.
    BUF_MEM *bm = NULL;
    BIO_get_mem_ptr(dynamic_cast<SrsSslConnection *>(conn->ssl_)->bio_out_, &bm);
    int payload = (int)bm->max - overhead - 16;
    ASSERT_GT(payload, 0);
    ASSERT_LE(payload, 16384);

    string tag(payload, 'f');
    client->armed_ = true;
    HELPER_EXPECT_SUCCESS(conn->ssl_->write((void *)tag.data(), tag.size(), NULL));

    // The record the socket write sends must still be the output BIO's buffer.
    EXPECT_TRUE(client->data_at_write_ != NULL);
    EXPECT_TRUE(client->data_at_write_ == client->data_after_read_);
}

// Polls, as any other coroutine of the server that is ready in the same round would run, and once the connection
// starts freeing the play stream, publishes the stream at every switch of that teardown, as a new WHIP or RTMP
// publisher does.
class MemoryAuditPublishDuringPlayTeardown : public ISrsCoroutineHandler
{
public:
    SrsSharedPtr<SrsRtcSource> source_;
    bool tearing_down_;
    bool play_freed_;
    bool published_;

public:
    MemoryAuditPublishDuringPlayTeardown() : tearing_down_(false), play_freed_(false), published_(false) {}
    virtual ~MemoryAuditPublishDuringPlayTeardown() {}

public:
    virtual srs_error_t cycle()
    {
        srs_error_t err = srs_success;
        for (int i = 0; i < 1000 && !play_freed_; i++) {
            if (tearing_down_) {
                published_ = true;
                if ((err = source_->on_publish()) != srs_success) {
                    return err;
                }
            }
            srs_thread_yield();
        }
        return err;
    }
};

// SrsRtcConnection frees its players without stopping them first, so ~SrsRtcPlayStream frees trd_, which interrupts
// the sender coroutine and joins it. Until the sender runs, its consumer is still on the source with the play stream
// as handler. The join yields, so a publisher of the same stream that is ready in that round runs
// SrsRtcSource::on_publish, which calls on_stream_change of every consumer, and the play stream's on_stream_change
// calls pli_worker_->request_keyframe, which must not be freed before the sender quits. A player that leaves, by a WHEP DELETE,
// a timeout or an API kick, while a publisher starts the stream it watched takes down the whole server.
VOID TEST(MemoryAuditTest, SrsRtcPlayStream_PublishDuringTeardownUsesFreedPliWorker)
{
    srs_error_t err = srs_success;

    MockAppConfig config;
    MockRtcSourceManager sources;
    MockAppStatistic stat;
    MockRtcAsyncCallRequest req("test.vhost", "live", "stream1");
    MockRtcAsyncTaskExecutor exec;
    MockExpire expire;
    MockRtcPacketSender sender;
    MockRtcTrackDescriptionFactory factory;
    config.mw_msgs_ = 0;

    SrsContextId cid;
    cid.set_value("memory-audit-play-teardown");
    SrsRtcPlayStream *play = new SrsRtcPlayStream(&exec, &expire, &sender, cid);
    play->config_ = &config;
    play->rtc_sources_ = &sources;
    play->stat_ = &stat;
    play->shared_timer_ = NULL;

    std::map<uint32_t, SrsRtcTrackDescription *> sub_relations = factory.create_audio_video_tracks();
    HELPER_ASSERT_SUCCESS(play->initialize(&req, sub_relations));
    for (std::map<uint32_t, SrsRtcTrackDescription *>::iterator it = sub_relations.begin(); it != sub_relations.end(); ++it) {
        SrsRtcTrackDescription *desc = it->second;
        srs_freep(desc);
    }
    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(factory.create_stream_description());
    play->source_->set_stream_desc(stream_desc.get());

    // The sender coroutine makes its consumer and waits for packets.
    HELPER_ASSERT_SUCCESS(play->start());
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    EXPECT_EQ(1, (int)play->source_->consumers_.size());

    // The publisher coroutine is ready when the connection frees the player.
    MemoryAuditPublishDuringPlayTeardown publisher;
    publisher.source_ = play->source_;
    SrsUniquePtr<SrsSTCoroutine> trd(new SrsSTCoroutine("publisher", &publisher));
    HELPER_ASSERT_SUCCESS(trd->start());

    publisher.tearing_down_ = true;
    srs_freep(play);
    publisher.play_freed_ = true;
    srs_freep(exec.last_task_);

    // With the bug, the publish crashes in on_stream_change before this.
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    EXPECT_TRUE(publisher.published_);
}

// A network that encrypts like SrsSRTP::protect_rtcp: with the AES-CM HMAC-SHA1-80 policy SrsSRTP::initialize sets,
// libsrtp's srtp_protect_rtcp appends a 4-byte E flag and SRTCP index and a 10-byte auth tag in place right after the
// packet, and trusts the caller to leave room for them. libsrtp is not built with the sanitizer, so the test writes
// the 14 bytes itself where ASan can see them.
class MemoryAuditSrtcpTrailerNetwork : public MockRtcNetwork
{
public:
    virtual srs_error_t protect_rtcp(void *packet, int *nb_cipher)
    {
        memset((char *)packet + *nb_cipher, 0xab, 14);
        *nb_cipher += 14;
        return srs_success;
    }
};

// A WebRTC publisher sends video RTP in runs of 16 packets and skips one sequence number each time, as for B17, but
// loses 370 numbers, not 373 or more. SrsRtcConnection::check_send_nacks asks for all of them in one SrsRtcpNack,
// whose 370 PID and BLP pairs encode to 12 + 4 * 370 = 1492 bytes, within the 1500-byte stack buffer, so the encode
// passes. SrsRtcConnection::send_rtcp then encrypts the packet in place, and the 14-byte SRTCP trailer lands past the
// buffer: a stack overflow of 6 bytes driven by the publisher. Any NACK of 1487 to 1500 bytes overflows. send_rtcp
// takes no buffer size, so every caller has to leave room for the trailer, and the NACK buffer does not.
VOID TEST(MemoryAuditTest, SrsRtcConnection_NackFillingBufferOverflowsSrtcpTrailer)
{
    MockRtcPacketReceiver receiver;
    MockCircuitBreaker breaker;
    breaker.hybrid_high_water_level_ = false;

    SrsRtcTrackDescription desc;
    desc.type_ = "video";
    desc.id_ = "video-track";
    desc.ssrc_ = 0x2345;
    desc.is_active_ = true;
    SrsUniquePtr<SrsRtcVideoRecvTrack> track(new SrsRtcVideoRecvTrack(&receiver, &desc, false));
    track->circuit_breaker_ = &breaker;

    // 370 runs of 16 packets, each run followed by one lost sequence number, and a packet after the last.
    uint16_t seq = 1000;
    for (int i = 0; i <= 370; i++) {
        for (int j = 0; j < (i < 370 ? 16 : 1); j++) {
            SrsRtpPacket pkt;
            pkt.header_.set_ssrc(0x2345);
            pkt.header_.set_sequence(seq++);
            SrsRtpPacket *p = &pkt;
            srs_error_t err = track->on_nack(&p);
            srs_freep(err);
        }
        seq++;
    }
    EXPECT_EQ(370, (int)track->nack_receiver_->queue_.size());

    // Past the first NACK interval of 10ms, as the connection's NACK timer finds them.
    srs_usleep(30 * SRS_UTIME_MILLISECONDS);
    srs_time_now_realtime();

    MockRtcAsyncTaskExecutor exec;
    SrsContextId cid;
    cid.set_value("memory-audit-srtcp-trailer-nack");
    MemoryAuditSrtcpTrailerNetwork network;
    network.is_established_ = true;
    MockRtcNetworksForConnection networks(&network);
    MockRtcBlackholeForSecurityTransport blackhole;

    SrsUniquePtr<SrsRtcConnection> conn(new SrsRtcConnection(&exec, cid));
    srs_freep(conn->networks_);
    conn->networks_ = &networks;
    conn->blackhole_ = &blackhole;
    conn->circuit_breaker_ = &breaker;

    // The NACK timer's check of the publisher's video track, which encodes the NACK and sends it with send_rtcp.
    uint32_t sent_nacks = 0, timeout_nacks = 0;
    conn->check_send_nacks(track->nack_receiver_, 0x2345, sent_nacks, timeout_nacks);
    conn->networks_ = NULL;
    conn->blackhole_ = NULL;
    conn->circuit_breaker_ = NULL;

    EXPECT_EQ(1, network.write_count_);
    EXPECT_EQ(1492, blackhole.last_len_);
}

// A WebRTC publisher with TWCC on sends packets whose TWCC sequence numbers run against their arrival, each 250us or
// more before the one numbered just below it, so every receive delta is negative and takes 2 bytes. When the 100ms
// TWCC timer comes late, as it does on a loaded server ("twcc delay" warning), one feedback holds about 720 such
// packets. SrsRtcPublishStream::send_periodic_twcc encodes it into a 1472-byte (kMaxUDPDataSize) stack buffer, and
// SrsRtcpTWCC::do_encode fills it to within a few bytes of the end, leaving no room for the 14-byte SRTCP trailer that
// SrsRtcConnection::send_rtcp then adds in place: a stack overflow.
VOID TEST(MemoryAuditTest, SrsRtcConnection_TwccFillingBufferOverflowsSrtcpTrailer)
{
    srs_error_t err;

    MockRtcAsyncTaskExecutor exec;
    SrsContextId cid;
    cid.set_value("memory-audit-srtcp-trailer-twcc");
    MemoryAuditSrtcpTrailerNetwork network;
    network.is_established_ = true;
    MockRtcNetworksForConnection networks(&network);
    MockRtcBlackholeForSecurityTransport blackhole;

    SrsUniquePtr<SrsRtcConnection> conn(new SrsRtcConnection(&exec, cid));
    srs_freep(conn->networks_);
    conn->networks_ = &networks;
    conn->blackhole_ = &blackhole;

    MockRtcExpire expire;
    SrsUniquePtr<SrsRtcPublishStream> publisher(new SrsRtcPublishStream(&exec, &expire, conn.get(), cid));

    // As SrsRtcPublishStream::on_twcc records them: 800 packets, each received 300us before the one numbered below it.
    srs_utime_t now = srs_time_now_realtime();
    for (int i = 0; i < 800; i++) {
        HELPER_EXPECT_SUCCESS(publisher->rtcp_twcc_->recv_packet((uint16_t)(1000 + i), now - i * 300));
    }

    // The TWCC timer's send, which encodes the feedback and sends it with send_rtcp.
    err = publisher->send_periodic_twcc();
    srs_freep(err);
    conn->networks_ = NULL;
    conn->blackhole_ = NULL;

    EXPECT_LE(1, network.write_count_);
}

// A DTLS callback whose next write switches to other coroutines after it is sent, as SrsUdpMuxSocket::sendto does on
// every 21st send of a session, or as a TCP send does on a full buffer.
class MemoryAuditDtlsYieldingWriteCallback : public MockDtlsCallbackForHandshake
{
public:
    bool yield_next_;
    bool yielding_;

public:
    MemoryAuditDtlsYieldingWriteCallback()
    {
        yield_next_ = false;
        yielding_ = false;
    }
    virtual ~MemoryAuditDtlsYieldingWriteCallback()
    {
    }

public:
    virtual srs_error_t write_dtls_data(void *data, int size)
    {
        srs_error_t err = MockDtlsCallbackForHandshake::write_dtls_data(data, size);
        if (yield_next_) {
            yield_next_ = false;
            yielding_ = true;
            srs_usleep(300 * SRS_UTIME_MILLISECONDS);
            yielding_ = false;
        }
        return err;
    }
};

// Hand each packet to the DTLS object, in order.
static srs_error_t memory_audit_dtls_deliver(SrsDtls *dtls, std::vector<std::string> &packets)
{
    srs_error_t err = srs_success;

    std::vector<std::string> pkts;
    pkts.swap(packets);
    for (int i = 0; i < (int)pkts.size(); i++) {
        std::string &pkt = pkts[i];
        if ((err = dtls->on_dtls(&pkt[0], (int)pkt.size())) != srs_success) {
            return err;
        }
    }

    return err;
}

// B83: SrsDtlsClientImpl::cycle retransmits the last flight by DTLSv1_handle_timeout, and OpenSSL sends it from its
// handshake buffer BIO through the output BIO callback into the network send, which can switch coroutines. If the
// peer's Finished is read meanwhile, SSL_read ends the handshake and frees that buffer BIO, and stop_arq joins the ARQ
// coroutine, which returns into SrsDtlsImpl::write_dtls_data and OpenSSL's flush with the freed buffer.
VOID TEST(MemoryAuditTest, SrsDtlsClientImpl_FinishedReadDuringArqSendFreesItsBuffer)
{
    srs_error_t err;

    MemoryAuditDtlsYieldingWriteCallback ccb;
    MockDtlsCallbackForHandshake scb;
    SrsDtls client(&ccb);
    SrsDtls server(&scb);
    HELPER_ASSERT_SUCCESS(client.initialize("active", "dtls1.2"));
    HELPER_ASSERT_SUCCESS(server.initialize("passive", "dtls1.2"));
    HELPER_ASSERT_SUCCESS(server.start_active_handshake());
    HELPER_ASSERT_SUCCESS(client.start_active_handshake());

    // ClientHello, the server's flight, then the client's last flight, which the server answers with its Finished.
    HELPER_ASSERT_SUCCESS(memory_audit_dtls_deliver(&server, ccb.packets_));
    HELPER_ASSERT_SUCCESS(memory_audit_dtls_deliver(&client, scb.packets_));
    ASSERT_FALSE(ccb.packets_.empty());
    HELPER_ASSERT_SUCCESS(memory_audit_dtls_deliver(&server, ccb.packets_));
    ASSERT_TRUE(server.impl_->handshake_done_for_us_);
    ASSERT_FALSE(client.impl_->handshake_done_for_us_);
    std::vector<std::string> finished;
    finished.swap(scb.packets_);
    ASSERT_FALSE(finished.empty());

    // The server's Finished is late, so the ARQ coroutine retransmits the client's last flight, and its send switches.
    ccb.yield_next_ = true;
    for (int i = 0; i < 100 && !ccb.yielding_; i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
    ASSERT_TRUE(ccb.yielding_);

    // The receive coroutine reads the server's Finished, which ends the handshake and stops the ARQ coroutine, and
    // ASan reports the ARQ coroutine reading the freed buffer in SrsDtlsImpl::write_dtls_data.
    HELPER_EXPECT_SUCCESS(memory_audit_dtls_deliver(&client, finished));
    EXPECT_TRUE(client.impl_->handshake_done_for_us_);
}

// A STUN binding request with only a USERNAME attribute, which SrsRtcTcpConn::handshake looks the session up by.
static std::string memory_audit_stun_binding_request(const std::string &username)
{
    int padded = ((int)username.size() + 3) / 4 * 4;
    int message_size = 4 + padded;

    std::string stun("\x00\x01", 2);
    stun.push_back((char)((message_size >> 8) & 0xff));
    stun.push_back((char)(message_size & 0xff));
    stun.append("\x21\x12\xa4\x42", 4);
    stun.append("012345678901", 12);
    stun.append("\x00\x06", 2);
    stun.push_back((char)((username.size() >> 8) & 0xff));
    stun.push_back((char)(username.size() & 0xff));
    stun += username;
    stun.append(padded - username.size(), '\0');
    return stun;
}

// Frame a WebRTC over TCP packet with its two-byte length, RFC 4571, and send it from the client.
static srs_error_t memory_audit_rtc_tcp_send(SrsStSocket *client, const std::string &pkt)
{
    std::string frame;
    frame.push_back((char)((pkt.size() >> 8) & 0xff));
    frame.push_back((char)(pkt.size() & 0xff));
    frame += pkt;
    return client->write((void *)frame.data(), frame.size(), NULL);
}

// The server's TCP socket, whose sends wait once stalled_ is set, as a send to a client that stopped reading waits
// for room in the full buffer, until the coroutine is interrupted.
class MemoryAuditStallingTcpSocket : public ISrsProtocolReadWriter
{
public:
    SrsTcpConnection *skt_;
    bool stalled_;
    bool waiting_;

public:
    MemoryAuditStallingTcpSocket(SrsTcpConnection *skt)
    {
        skt_ = skt;
        stalled_ = false;
        waiting_ = false;
    }
    virtual ~MemoryAuditStallingTcpSocket()
    {
        srs_freep(skt_);
    }

public:
    virtual void set_recv_timeout(srs_utime_t tm) { skt_->set_recv_timeout(tm); }
    virtual srs_utime_t get_recv_timeout() { return skt_->get_recv_timeout(); }
    virtual void set_send_timeout(srs_utime_t tm) { skt_->set_send_timeout(tm); }
    virtual srs_utime_t get_send_timeout() { return skt_->get_send_timeout(); }
    virtual int64_t get_recv_bytes() { return skt_->get_recv_bytes(); }
    virtual int64_t get_send_bytes() { return skt_->get_send_bytes(); }
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread) { return skt_->read(buf, size, nread); }
    virtual srs_error_t read_fully(void *buf, size_t size, ssize_t *nread) { return skt_->read_fully(buf, size, nread); }

public:
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite)
    {
        if (stalled_) {
            return wait();
        }
        return skt_->write(buf, size, nwrite);
    }
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t *nwrite)
    {
        if (stalled_) {
            return wait();
        }
        return skt_->writev(iov, iov_size, nwrite);
    }

private:
    srs_error_t wait()
    {
        waiting_ = true;
        int r0 = srs_usleep(SRS_UTIME_NO_TIMEOUT);
        waiting_ = false;
        return srs_error_new(ERROR_SOCKET_WRITE, "write r0=%d", r0);
    }
};

// B87: SrsRtcTcpConn handles each packet on its own coroutine by calling into the session's TCP network, and a DTLS
// packet goes into OpenSSL, whose output callback sends on the TCP socket and waits there while the client's buffer
// is full. The session can be freed meanwhile, such as by its timeout, because the waiting coroutine reads no more
// packets. ~SrsRtcTcpNetwork only clears the conn's session pointer and interrupts its coroutine, then frees the DTLS
// transport and the SSL, so the interrupted send returns into OpenSSL and SrsDtlsImpl::do_on_dtls on freed memory.
VOID TEST(MemoryAuditTest, SrsRtcTcpConn_SessionFreedDuringDtlsSendUsesFreedDtls)
{
    srs_error_t err;

    int port = 0;
    srs_netfd_t lfd = NULL;
    HELPER_ASSERT_SUCCESS(srs_utest_tcp_listen("127.0.0.1", &port, &lfd));

    // A WebRTC client over TCP.
    srs_netfd_t client_fd = NULL;
    HELPER_ASSERT_SUCCESS(srs_tcp_connect("127.0.0.1", port, 1 * SRS_UTIME_SECONDS, &client_fd));
    srs_netfd_t server_fd = srs_accept(lfd, NULL, NULL, 1 * SRS_UTIME_SECONDS);
    ASSERT_TRUE(server_fd != NULL);
    SrsStSocket client(client_fd);

    // The session, found by the binding request's username, and its TCP network with the DTLS server transport.
    MockResourceManagerForTcpConnHandshake manager;
    MockAppStatistic stat;
    MockRtcConnectionForTcpConnHandshake session;
    MockEphemeralDelta delta;
    SrsRtcTcpNetwork *network = new SrsRtcTcpNetwork(&session, &delta);
    SrsSessionConfig cfg;
    cfg.dtls_role_ = "passive";
    cfg.dtls_version_ = "dtls1.2";
    HELPER_ASSERT_SUCCESS(network->initialize(&cfg, true, true));
    session.tcp_network_ = network;
    manager.session_to_return_ = &session;

    // The TCP conn, its shared wrapper and its executor coroutine, as SrsServer::do_on_tcp_client makes them.
    MemoryAuditStallingTcpSocket *skt = new MemoryAuditStallingTcpSocket(new SrsTcpConnection(server_fd));
    SrsRtcTcpConn *conn = new SrsRtcTcpConn(skt, "127.0.0.1", port);
    conn->assemble();
    conn->conn_manager_ = &manager;
    conn->stat_ = &stat;
    SrsSharedResource<ISrsRtcTcpConn> *wrapper = new SrsSharedResource<ISrsRtcTcpConn>(conn);
    SrsExecutorCoroutine *executor = new SrsExecutorCoroutine(&manager, wrapper, conn, conn);
    executor->assemble();
    conn->setup_owner(wrapper, executor, executor);
    HELPER_ASSERT_SUCCESS(executor->start());

    // The binding request binds the conn to the session's network.
    HELPER_ASSERT_SUCCESS(memory_audit_rtc_tcp_send(&client, memory_audit_stun_binding_request("test:session")));
    for (int i = 0; i < 100 && network->owner().get() != conn; i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
    ASSERT_TRUE(network->owner().get() == conn);

    // The client stops reading and its buffer fills, then it sends its ClientHello. The conn's coroutine hands it to
    // OpenSSL, whose server flight waits in the send.
    skt->stalled_ = true;
    MockDtlsCallbackForHandshake ccb;
    SrsDtls dtls_client(&ccb);
    HELPER_ASSERT_SUCCESS(dtls_client.initialize("active", "dtls1.2"));
    HELPER_ASSERT_SUCCESS(dtls_client.start_active_handshake());
    ASSERT_FALSE(ccb.packets_.empty());
    for (int i = 0; i < (int)ccb.packets_.size(); i++) {
        HELPER_ASSERT_SUCCESS(memory_audit_rtc_tcp_send(&client, ccb.packets_[i]));
    }
    for (int i = 0; i < 100 && !skt->waiting_; i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
    ASSERT_TRUE(skt->waiting_);
    EXPECT_GT(delta.in_bytes_, 0);

    // The session is freed, as the resource manager does when it times out, which frees its TCP network. The conn's
    // coroutine is interrupted and returns into the freed DTLS objects, and ASan reports a heap-use-after-free.
    session.tcp_network_ = NULL;
    manager.session_to_return_ = NULL;
    srs_freep(network);
    srs_usleep(100 * SRS_UTIME_MILLISECONDS);

    srs_freep(executor);
    srs_freep(wrapper);
    srs_close_stfd(client_fd);
    srs_close_stfd(lfd);
}

// A resource of another client, which counts how many times it is freed.
class MemoryAuditCountedResource : public ISrsResource
{
public:
    SrsContextId cid_;
    int *nn_freed_;

public:
    MemoryAuditCountedResource(int *nn_freed)
    {
        nn_freed_ = nn_freed;
    }
    virtual ~MemoryAuditCountedResource()
    {
        (*nn_freed_)++;
    }

public:
    virtual const SrsContextId &get_id() { return cid_; }
    virtual std::string desc() { return "MemoryAuditCountedResource"; }
};

// The TCP conn of a session, whose coroutine is in a send of the session, such as a NACK resend of a player, when
// the session is freed. interrupt() waits for the coroutine to leave the send, and meanwhile another client closes,
// which removes its resource again. It notes how many players the session still had when it was stopped.
class MemoryAuditTcpConnInSend : public ISrsRtcTcpConn
{
public:
    SrsContextId cid_;
    SrsRtcConnection *session_;
    ISrsResourceManager *manager_;
    ISrsResource *closing_;
    bool interrupted_;
    int nn_players_;

public:
    MemoryAuditTcpConnInSend(SrsRtcConnection *session, ISrsResourceManager *manager, ISrsResource *closing)
    {
        session_ = session;
        manager_ = manager;
        closing_ = closing;
        interrupted_ = false;
        nn_players_ = -1;
    }
    virtual ~MemoryAuditTcpConnInSend()
    {
    }

public:
    virtual const SrsContextId &get_id() { return cid_; }
    virtual std::string desc() { return "MemoryAuditTcpConnInSend"; }
    virtual std::string remote_ip() { return "127.0.0.1"; }
    virtual srs_error_t cycle() { return srs_success; }
    virtual void on_executor_done(ISrsInterruptable *executor) {}

public:
    virtual void interrupt()
    {
        // Once stopped, the coroutine handles no more packets, so a later interrupt returns at once.
        if (interrupted_) {
            return;
        }
        interrupted_ = true;
        nn_players_ = (int)session_->players_.size();

        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        manager_->remove(closing_);
    }
};

// B96, B103: when the RTC server frees a WebRTC over TCP session, it must stop the session's TCP conn before it frees
// any part of the session, because the conn's coroutine may be in a send of a player, such as a NACK resend, and
// returns into it. The wait for that coroutine must also happen where the resource manager allows a coroutine
// switch, so that another client of the same batch, removed again meanwhile by its own conn, is freed once.
VOID TEST(MemoryAuditTest, SrsRtcConnection_TcpConnStoppedBeforeSessionFreed)
{
    srs_error_t err;

    // The session switches this coroutine to its context; keep ours for the tests that run after.
    SrsContextRestore(_srs_context->get_id());

    // The RTC server's resource manager, with its own coroutine as the server starts it.
    SrsResourceManager manager("utest-rtc");
    manager.assemble();
    HELPER_ASSERT_SUCCESS(manager.start());
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    // A WHEP session over TCP with one player, which the manager owns and frees.
    MockRtcAsyncTaskExecutor exec;
    MockRtcPacketSender sender;
    SrsRtcConnection *session = new SrsRtcConnection(&exec, SrsContextId());
    session->conn_manager_ = &manager;
    session->assemble();
    session->players_["/live/livestream"] = new SrsRtcPlayStream(&exec, session, &sender, SrsContextId());
    manager.add(session);

    // Another client of the server.
    int nn_freed = 0;
    MemoryAuditCountedResource *other = new MemoryAuditCountedResource(&nn_freed);
    manager.add(other);

    // The session's TCP conn, in a send of the session.
    MemoryAuditTcpConnInSend *conn = new MemoryAuditTcpConnInSend(session, &manager, other);
    SrsSharedResource<ISrsRtcTcpConn> owner(conn);
    ISrsRtcTcpNetwork *network = dynamic_cast<ISrsRtcTcpNetwork *>(session->tcp());
    ASSERT_TRUE(network != NULL);
    network->set_owner(owner);

    // Both time out in the same tick, and the manager frees them.
    manager.remove(session);
    manager.remove(other);
    for (int i = 0; i < 100 && !manager.empty(); i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }

    // The conn was stopped while the session was whole, and the other client was freed once.
    EXPECT_TRUE(conn->interrupted_);
    EXPECT_EQ(1, conn->nn_players_);
    EXPECT_EQ(1, nn_freed);
    EXPECT_TRUE(manager.empty());
}

// Feed a WHIP H.264 publisher's stream to a frame builder with RTC to RTMP on: one IDR packet, then count packets of
// a one-byte NALU (only the header, as SrsRtcVideoRecvTrack decodes a raw NALU), each with the marker bit and its
// own timestamp, so each is a whole frame of no payload. Return how many frames the builder sent, or -1 on an error.
static int memory_audit_whip_empty_frames(int count)
{
    char idr_data[] = {0x65, (char)0x88, (char)0x84, 0x00, 0x33};
    char empty_data[] = {0x41};

    MockRtcFrameTarget target;
    SrsRtcFrameBuilder builder(_srs_app_factory, &target);
    MockRequest req("test.vhost", "live", "whip_empty");
    srs_error_t err = builder.initialize(&req, SrsAudioCodecIdAAC, SrsVideoCodecIdAVC);

    for (int i = 0; err == srs_success && i <= count; i++) {
        SrsRtpPacket pkt;
        pkt.header_.set_ssrc(5678);
        pkt.header_.set_sequence((uint16_t)(1000 + i));
        pkt.header_.set_timestamp(90000 + i * 3000);
        pkt.header_.set_marker(true);
        pkt.frame_type_ = SrsFrameTypeVideo;
        pkt.set_avsync_time(1000 + i * 33);

        SrsRtpRawPayload *raw = new SrsRtpRawPayload();
        if (i == 0) {
            pkt.nalu_type_ = SrsAvcNaluTypeIDR;
            raw->payload_ = idr_data;
            raw->nn_payload_ = (int)sizeof(idr_data);
        } else {
            pkt.nalu_type_ = SrsAvcNaluTypeNonIDR;
            raw->payload_ = empty_data;
            raw->nn_payload_ = (int)sizeof(empty_data);
        }
        pkt.set_payload(raw, SrsRtpPacketPayloadTypeRaw);

        err = builder.on_rtp(&pkt);
    }

    if (err != srs_success) {
        srs_freep(err);
        return -1;
    }
    return target.on_frame_count_;
}

// B89: a frame of no payload is never taken out of the video cache, so its slot stays in use. After 512 such frames,
// every slot of the cache is in use, and the slot of the next sequence number holds a packet 512 numbers older.
// SrsRtcFrameBuilderVideoFrameDetector::detect_next_frame() asks the cache for the next frame from there, and
// find_next_lost_sn() takes the stale slot as present (is_slot_in_use() and get_rtp_timestamp() ignore its sn_), so
// it reports a one-packet frame whose packet get_packet() does not find. packet_video_rtmp() finds no payload in it
// and calls itself for the frame after, which is again a stale slot, so it recurses without an end and overflows the
// stack, crashing the server. ASan reports a stack-overflow in packet_video_rtmp(). A frame of no payload should be
// freed out of the cache, find_next_lost_sn() should check sn_, and the next frame should be found by a loop.
VOID TEST(MemoryAuditTest, SrsRtcFrameBuilderVideoFrameDetector_EmptyFramesRecurseWithoutEnd)
{
    // Control: 500 empty frames leave a free slot, and the builder sends only the IDR frame.
    EXPECT_EQ(1, memory_audit_whip_empty_frames(500));

    // At the 512th empty frame, the builder recurses through the stale slots until the stack overflows.
    EXPECT_EQ(1, memory_audit_whip_empty_frames(600));
}


// A frame target that records how far down the stack each frame is sent from.
class MemoryAuditStackDepthTarget : public MockRtcFrameTarget
{
public:
    uintptr_t lowest_;
    uintptr_t highest_;

public:
    MemoryAuditStackDepthTarget() : lowest_(0), highest_(0) {}
    virtual ~MemoryAuditStackDepthTarget() {}

public:
    virtual srs_error_t on_frame(SrsMediaPacket *frame)
    {
        char probe = 0;
        uintptr_t v = (uintptr_t)&probe;
        if (!lowest_ || v < lowest_) {
            lowest_ = v;
        }
        if (v > highest_) {
            highest_ = v;
        }
        return MockRtcFrameTarget::on_frame(frame);
    }
};

// A WHIP H.264 publisher's stream with one packet held back: an IDR packet, a lost packet, then count one-packet
// frames of a two-byte NALU, each with the marker bit and its own timestamp, then the lost packet last. The builder
// keeps every frame after the lost packet in its cache until the lost one comes, and then sends them all at once.
// Runs on its own coroutine, as the server's WebRTC connections do.
class MemoryAuditWhipHeldFrames : public ISrsCoroutineHandler
{
public:
    int count_;
    int sent_;
    int stack_used_;
    bool done_;

public:
    MemoryAuditWhipHeldFrames(int count) : count_(count), sent_(-1), stack_used_(0), done_(false) {}
    virtual ~MemoryAuditWhipHeldFrames() {}

public:
    virtual srs_error_t cycle()
    {
        char idr_data[] = {0x65, (char)0x88, (char)0x84, 0x00, 0x33};
        char frame_data[] = {0x41, (char)0x9a};

        MemoryAuditStackDepthTarget target;
        SrsRtcFrameBuilder builder(_srs_app_factory, &target);
        MockRequest req("test.vhost", "live", "whip_held");
        srs_error_t err = builder.initialize(&req, SrsAudioCodecIdAAC, SrsVideoCodecIdAVC);

        // The IDR is 0, the lost packet is 1, the held frames are 2 to count + 1, and the lost packet comes last.
        for (int i = 0; err == srs_success && i <= count_ + 2; i++) {
            int n = (i == count_ + 2) ? 1 : i;
            if (n == 1 && i == 1) {
                continue;
            }

            SrsRtpPacket pkt;
            pkt.header_.set_ssrc(5678);
            pkt.header_.set_sequence((uint16_t)(1000 + n));
            pkt.header_.set_timestamp(90000 + n * 3000);
            pkt.header_.set_marker(true);
            pkt.frame_type_ = SrsFrameTypeVideo;
            pkt.set_avsync_time(1000 + n * 33);

            SrsRtpRawPayload *raw = new SrsRtpRawPayload();
            if (n == 0) {
                pkt.nalu_type_ = SrsAvcNaluTypeIDR;
                raw->payload_ = idr_data;
                raw->nn_payload_ = (int)sizeof(idr_data);
            } else {
                pkt.nalu_type_ = SrsAvcNaluTypeNonIDR;
                raw->payload_ = frame_data;
                raw->nn_payload_ = (int)sizeof(frame_data);
            }
            pkt.set_payload(raw, SrsRtpPacketPayloadTypeRaw);

            err = builder.on_rtp(&pkt);
        }

        if (err != srs_success) {
            srs_freep(err);
        } else {
            sent_ = target.on_frame_count_;
            stack_used_ = (int)(target.highest_ - target.lowest_);
        }
        done_ = true;
        return srs_success;
    }
};

// Run the held frames on a coroutine with a 1 MB stack, so a deep recursion is measured, not overflowed. Return the
// frames sent, or -1 on an error, and set stack_used to the stack the frames after the first one took.
static int memory_audit_whip_held_frames(int count, int &stack_used)
{
    MemoryAuditWhipHeldFrames feeder(count);

    SrsUniquePtr<SrsSTCoroutine> trd(new SrsSTCoroutine("held", &feeder));
    trd->set_stack_size(1 << 20);
    srs_error_t err = trd->start();
    if (err != srs_success) {
        srs_freep(err);
        return -1;
    }

    for (int i = 0; i < 1000 && !feeder.done_; i++) {
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }
    stack_used = feeder.stack_used_;
    return feeder.sent_;
}

// B89: SrsRtcFrameBuilder::packet_video_rtmp() sends a frame and then calls itself for the next frame in the cache,
// so it recurses once per frame that is ready. When one packet is lost, every frame after it waits in the 512-slot
// cache, and the lost packet's arrival sends them all in one recursion, up to 510 frames deep. Each level takes 320
// bytes of stack in a GCC -O0 build, 528 in a clang -O0 build and about 1.6 KB under ASan, so 500 frames need 160 KB
// or more, while the executor coroutine of a WebRTC TCP connection has the default 128 KB. ST allocates coroutine
// stacks with malloc (MALLOC_STACK), where its red zones cannot be protected, so the frames are written over the heap
// below the stack. Any WHIP publisher reaches it by holding one packet back. The next frame should be found by a
// loop, not a recursion.
VOID TEST(MemoryAuditTest, SrsRtcFrameBuilder_HeldFramesRecurseOverCoroutineStack)
{
    // The stack of a WebRTC TCP connection's coroutine, ST_DEFAULT_STACK_SIZE.
    const int coroutine_stack = 128 * 1024;

    // Control: 50 held frames are sent after the IDR and the lost packet, well within the stack.
    int stack_used = 0;
    EXPECT_EQ(52, memory_audit_whip_held_frames(50, stack_used));
    EXPECT_LT(stack_used, coroutine_stack);

    // 500 held frames, which fit in the cache, are sent in one recursion that needs more than the whole stack.
    EXPECT_EQ(502, memory_audit_whip_held_frames(500, stack_used));
    EXPECT_LT(stack_used, coroutine_stack);
}

// A WHIP publisher's IDR frame loses its middle packet, and more than 512 later packets come before the resend, so the
// slot of the IDR's last packet now holds a later packet. The cache took that slot for an earlier packet of the IDR
// frame and failed the resend as a cache overflow, which the publisher's connection returns as an error: over TCP, it
// ends the session. A valid publisher's late resend should be taken, or its frame dropped, with no error.
VOID TEST(MemoryAuditTest, SrsRtcFrameBuilder_LateResendAfterCacheWrapFailsPublisher)
{
    srs_error_t err = srs_success;

    char idr_data[] = {0x65, (char)0x88, (char)0x84, 0x00, 0x33};
    char frame_data[] = {0x41, (char)0x9a};

    MockRtcFrameTarget target;
    SrsRtcFrameBuilder builder(_srs_app_factory, &target);
    MockRequest req("test.vhost", "live", "whip_late_resend");
    HELPER_EXPECT_SUCCESS(builder.initialize(&req, SrsAudioCodecIdAAC, SrsVideoCodecIdAVC));

    // The IDR frame is packets 0 to 2 with one timestamp and the marker on 2; packet 1 is lost. Then 600 one-packet
    // frames, 3 to 602, and the resend of packet 1 last.
    const int nb_frames = 600;
    int nb_failed = 0;
    for (int i = 0; i <= nb_frames + 3; i++) {
        int n = (i == nb_frames + 3) ? 1 : i;
        if (n == 1 && i == 1) {
            continue;
        }

        SrsRtpPacket pkt;
        pkt.header_.set_ssrc(5678);
        pkt.header_.set_sequence((uint16_t)(1000 + n));
        pkt.header_.set_timestamp(n <= 2 ? 90000 : 90000 + n * 3000);
        pkt.header_.set_marker(n != 0 && n != 1);
        pkt.frame_type_ = SrsFrameTypeVideo;
        pkt.set_avsync_time(n <= 2 ? 1000 : 1000 + n * 33);

        SrsRtpRawPayload *raw = new SrsRtpRawPayload();
        pkt.nalu_type_ = (n <= 2) ? SrsAvcNaluTypeIDR : SrsAvcNaluTypeNonIDR;
        raw->payload_ = (n <= 2) ? idr_data : frame_data;
        raw->nn_payload_ = (n <= 2) ? (int)sizeof(idr_data) : (int)sizeof(frame_data);
        pkt.set_payload(raw, SrsRtpPacketPayloadTypeRaw);

        if ((err = builder.on_rtp(&pkt)) != srs_success) {
            nb_failed++;
            srs_freep(err);
        }
    }

    EXPECT_EQ(0, nb_failed);
}

// An SRT player sends no data, so each read of its recv coroutine times out after peer_idle_timeout. The
// coroutine keeps going on ERROR_SRT_TIMEOUT but never frees that error, so every idle period of every SRT
// player leaks one error. Under LeakSanitizer, each timeout below is a direct leak from SrsCplxError::create.
VOID TEST(MemoryAuditTest, SrsCplxError_SrtRecvTimeoutLeaks)
{
    srs_error_t err;

    MockSrtConnection *conn = new MockSrtConnection();
    conn->simulate_timeout_ = true;
    SrsUniquePtr<SrsSrtRecvThread> recv_thread(new SrsSrtRecvThread(conn));

    // The coroutine lets two reads time out, then quits.
    MockSrtCoroutine *trd = new MockSrtCoroutine();
    trd->pull_error_ = srs_error_new(ERROR_THREAD_INTERRUPED, "thread interrupted");
    srs_freep(recv_thread->trd_);
    recv_thread->trd_ = trd;

    HELPER_EXPECT_FAILED(recv_thread->cycle());
    EXPECT_EQ(3, trd->pull_count_);

    recv_thread->trd_ = NULL;
    srs_freep(trd);
    srs_freep(conn);
}

// A WHIP offer whose audio payload has no fmtp, such as Opus without parameters or any PCMU or PCMA
// payload, makes SrsAudioPayload::set_opus_param_desc return an error, which the publish negotiation ignores
// and never frees. Every such publish leaks one error. Under LeakSanitizer, it is a direct leak from
// SrsCplxError::create.
VOID TEST(MemoryAuditTest, SrsCplxError_OpusParamErrorLeaks)
{
    srs_error_t err;

    SrsUniquePtr<SrsRtcPublisherNegotiator> negotiator(new SrsRtcPublisherNegotiator());
    SrsUniquePtr<MockRtcConnectionRequest> request(new MockRtcConnectionRequest("test.vhost", "live", "stream1"));

    SrsUniquePtr<SrsRtcUserConfig> ruc(new SrsRtcUserConfig());
    srs_freep(ruc->req_);
    ruc->req_ = request->copy();
    ruc->publish_ = true;
    ruc->dtls_ = true;
    ruc->srtp_ = true;
    ruc->audio_before_video_ = true;
    ruc->remote_sdp_str_ =
        "v=0\r\n"
        "o=- 123456789 2 IN IP4 127.0.0.1\r\n"
        "s=-\r\n"
        "t=0 0\r\n"
        "a=group:BUNDLE 0 1\r\n"
        "a=msid-semantic: WMS stream\r\n"
        "m=audio 9 UDP/TLS/RTP/SAVPF 0\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=rtcp:9 IN IP4 0.0.0.0\r\n"
        "a=ice-ufrag:test\r\n"
        "a=ice-pwd:testpassword\r\n"
        "a=fingerprint:sha-256 AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99\r\n"
        "a=setup:actpass\r\n"
        "a=mid:0\r\n"
        "a=sendonly\r\n"
        "a=rtcp-mux\r\n"
        "a=rtpmap:0 PCMU/8000\r\n"
        "a=ssrc:1001 cname:test-audio\r\n"
        "a=ssrc:1001 msid:stream audio\r\n"
        "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=rtcp:9 IN IP4 0.0.0.0\r\n"
        "a=ice-ufrag:test\r\n"
        "a=ice-pwd:testpassword\r\n"
        "a=fingerprint:sha-256 AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99\r\n"
        "a=setup:actpass\r\n"
        "a=mid:1\r\n"
        "a=sendonly\r\n"
        "a=rtcp-mux\r\n"
        "a=rtpmap:96 H264/90000\r\n"
        "a=fmtp:96 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f\r\n"
        "a=ssrc:2001 cname:test-video\r\n"
        "a=ssrc:2001 msid:stream video\r\n";
    HELPER_EXPECT_SUCCESS(ruc->remote_sdp_.parse(ruc->remote_sdp_str_));

    SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());
    HELPER_EXPECT_SUCCESS(negotiator->negotiate_publish_capability(ruc.get(), stream_desc.get()));
    ASSERT_TRUE(stream_desc->audio_track_desc_ != NULL);
}

// An RTMP player asks an origin cluster for a stream it does not have. The origin asks each coworker in
// turn, and when one fails but is not the last, it moves on without freeing that error, so every such play
// leaks one error per failed coworker. Under LeakSanitizer, it is a direct leak from SrsCplxError::copy in
// the mock, which stands for the hooks' own error; the mock frees its own list.
VOID TEST(MemoryAuditTest, SrsCplxError_CoworkerDiscoverErrorLeaks)
{
    srs_error_t err;

    MockAppConfigForRtmpConnCluster config;
    config.coworkers_.push_back("10.0.0.1:1985");
    config.coworkers_.push_back("10.0.0.2:1985");
    MockHttpHooksForCoworkers hooks;
    hooks.hosts_.push_back("");
    hooks.ports_.push_back(0);
    hooks.hosts_.push_back("10.0.0.2");
    hooks.ports_.push_back(19350);
    hooks.errors_.push_back(srs_error_new(ERROR_HTTP_DVR_NO_TAEGET, "mock not found"));
    hooks.errors_.push_back(srs_success);

    SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(new MockRtmpTransportForDoCycle(), "192.168.1.100", 1935));
    conn->config_ = &config;
    conn->assemble();
    conn->hooks_ = &hooks;
    MockRtmpServerForRedirect *rtmp = new MockRtmpServerForRedirect();
    srs_freep(conn->rtmp_);
    conn->rtmp_ = rtmp;

    ISrsRequest *req = conn->info_->req_;
    req->vhost_ = "__defaultVhost__";
    req->host_ = "127.0.0.1";
    req->app_ = "live";
    req->stream_ = "livestream";

    // The first coworker fails, the second one has the stream.
    SrsSharedPtr<SrsLiveSource> source(new MockLiveSource());
    err = conn->redirect_to_origin_cluster(source);
    EXPECT_EQ(ERROR_CONTROL_REDIRECT, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(2, (int)hooks.urls_.size());

    conn->hooks_ = NULL;
    conn->config_ = NULL;
}

// An edge pulls HTTP-FLV from an origin that stops right after a tag's data, before its 4-byte previous tag size, as
// when the origin crashes or a proxy cuts the connection. The upstream allocates the tag data and reads it, then fails
// at the previous tag size and returns without freeing the data, so every such cut leaks one tag, up to 16 MB. Under
// LeakSanitizer, it is a direct leak from SrsEdgeFlvUpstream::recv_message.
VOID TEST(MemoryAuditTest, SrsFlvDecoder_EdgeTagCutBeforePreviousTagSizeLeaks)
{
    srs_error_t err;

    // A video tag header of 1024 bytes of data, then the data, and no previous tag size.
    string flv;
    flv.append("\x09\x00\x04\x00", 4);
    flv.append("\x00\x00\x28\x00", 4);
    flv.append("\x00\x00\x00", 3);
    flv.append(1024, '\x17');
    MockSrsFileReader reader(flv.data(), (int)flv.size());

    SrsUniquePtr<SrsEdgeFlvUpstream> upstream(new SrsEdgeFlvUpstream("http"));
    SrsFlvDecoder *decoder = new SrsFlvDecoder();
    HELPER_ASSERT_SUCCESS(decoder->initialize(&reader));
    upstream->decoder_ = decoder;

    SrsRtmpCommonMessage *msg = NULL;
    err = upstream->recv_message(&msg);
    EXPECT_TRUE(msg == NULL);
    EXPECT_EQ(ERROR_SYSTEM_FILE_EOF, srs_error_code(err));
    EXPECT_NE(string::npos, srs_error_desc(err).find("read pts"));
    srs_freep(err);
    srs_freep(msg);
}

// The static file server answers a directory without its slash, such as GET /players, with this handler's redirect,
// and the HTTP mux does the same for every pattern registered with a trailing slash. A client that resets the
// connection right after its request makes the response write fail. The handler ignores the errors of write and
// final_request, so a failed write leaves a heap error that is never freed, and the handler reports success. Any
// client can repeat the request, leaking an error chain each time.
VOID TEST(MemoryAuditTest, SrsHttpRedirectHandler_WriteErrorLeaks)
{
    srs_error_t err = srs_success;

    SrsHttpRedirectHandler h("/players/", SRS_CONSTS_HTTP_MovedPermanently);

    MockResponseWriter w;
    w.io.out_err = srs_error_new(ERROR_SOCKET_WRITE, "connection reset");

    SrsHttpMessage r(NULL, NULL);
    HELPER_ASSERT_SUCCESS(r.set_url("/players", false));

    // The write failed, so the handler returns its error.
    err = h.serve_http(&w, &r);
    EXPECT_TRUE(err != srs_success);
    srs_freep(err);
}

// The JSON play and publish APIs parse the request body with SrsJsonAny::loads, which returns a new value the caller
// owns. When the body is valid JSON but not an object, such as [1,2], both handlers return the error without freeing
// the parsed value. Any HTTP client can POST such a body to /rtc/v1/play/ or /rtc/v1/publish/ and leak the whole
// parsed tree on each request.
VOID TEST(MemoryAuditTest, SrsJsonAny_RtcApiNonObjectBodyLeaks)
{
    srs_error_t err = srs_success;

    if (true) {
        SrsGoApiRtcPlay api(NULL);
        MockResponseWriter w;
        MockHttpMessageForRtcApi r;
        r.body_content_ = "[1,2]";

        HELPER_EXPECT_SUCCESS(api.serve_http(&w, &r));
        string response = string(w.io.out_buffer.bytes(), w.io.out_buffer.length());
        EXPECT_TRUE(response.find("\"code\":400") != string::npos) << response;
    }

    if (true) {
        SrsGoApiRtcPublish api(NULL);
        MockResponseWriter w;
        MockHttpMessageForRtcApi r;
        r.body_content_ = "[1,2]";

        HELPER_EXPECT_SUCCESS(api.serve_http(&w, &r));
        string response = string(w.io.out_buffer.bytes(), w.io.out_buffer.length());
        EXPECT_TRUE(response.find("\"code\":400") != string::npos) << response;
    }
}

// SrsEdgeFlvUpstream::decode_message() hands an onMetaData or @setDataFrame packet to the caller through *ppacket before
// it decodes it, and returns the decode error with the packet still there. SrsEdgeIngester::process_publish_message()
// takes ownership only on success, as SrsProtocol::decode_message() works, which frees the packet on an error. So an
// origin's FLV script tag that names @setDataFrame or onMetaData and is cut or malformed after the name leaks the packet
// and its metadata object, once per pull; the edge then pulls again. The upstream should free the packet on an error
// and set *ppacket only on success.
VOID TEST(MemoryAuditTest, SrsEdgeFlvUpstream_BadMetadataLeaksPacket)
{
    srs_error_t err = srs_success;

    // The origin's script tag: @setDataFrame with no name or metadata after it.
    uint8_t bytes[] = {0x02, 0x00, 0x0d, '@', 's', 'e', 't', 'D', 'a', 't', 'a', 'F', 'r', 'a', 'm', 'e'};
    char *data = new char[sizeof(bytes)];
    memcpy(data, bytes, sizeof(bytes));

    // The message as SrsEdgeFlvUpstream::recv_message() makes it from an FLV script tag.
    SrsRtmpCommonMessage *msg_raw = NULL;
    HELPER_ASSERT_SUCCESS(srs_rtmp_create_msg(SrsFrameTypeScript, 0, data, sizeof(bytes), 1, &msg_raw));
    SrsUniquePtr<SrsRtmpCommonMessage> msg(msg_raw);
    EXPECT_TRUE(msg->header_.is_amf0_data());

    MockEdgeIngesterFixture f(100);
    srs_freep(f.ingester_->upstream_);
    f.ingester_->upstream_ = new SrsEdgeFlvUpstream("http");

    // The edge refuses the tag, and decode_message() frees the SrsOnMetaDataPacket it made.
    std::string redirect;
    err = f.ingester_->process_publish_message(msg.get(), redirect);
    EXPECT_TRUE(err != srs_success);
    srs_freep(err);
}

// With hls_ctx and hls_ts_ctx on (the default), a viewer that refreshes the m3u8 of its session gets a playlist that
// SrsHlsStream::serve_exists_session() rebuilds with the ctx in each segment URL. It writes the body with w->write()
// and ignores the result, so when the socket write fails, such as a viewer that resets the connection after its
// request, the error is dropped and never freed. The final_request() that follows flushes nothing more and succeeds,
// so the session goes on as served. Any viewer with a session can repeat it, one leaked error per refresh.
VOID TEST(MemoryAuditTest, SrsHlsStream_ExistingSessionWriteErrorLeaks)
{
    srs_error_t err = srs_success;

    SrsHlsStream hls;
    MockFileReaderFactoryForHlsStream factory("#EXTM3U\n#EXTINF:10.000,\nstream1-0.ts\n");

    SrsHttpMessage r(NULL, NULL);
    HELPER_ASSERT_SUCCESS(r.set_url("/live/stream1.m3u8?hls_ctx=abcdefgh", false));

    MockResponseWriter w;
    w.io.out_err = srs_error_new(ERROR_SOCKET_WRITE, "connection reset");

    // The write failed, so the session returns its error.
    err = hls.serve_exists_session(&w, &r, &factory, "./objs/nginx/html/live/stream1.m3u8");
    EXPECT_TRUE(err != srs_success);
    srs_freep(err);
}

// A WHEP or RTC play offer may carry more than one m-line of a kind, such as a client that adds two video
// transceivers; SrsGoApiRtcPlay::check_remote_sdp checks only the type, rtcp-mux and direction of each. For every
// m-line, SrsRtcPlayerNegotiator::negotiate_play_capability copies every source track of the negotiated codec and
// inserts the copy keyed by the publisher's SSRC. The second video m-line copies the same source track again, the
// insert finds that SSRC in the map and keeps the first copy, and the second is held by nothing. The caller frees only
// what is in the map, so each such play request leaks a track description per extra m-line.
VOID TEST(MemoryAuditTest, SrsRtcPlayerNegotiator_DuplicateMlineLeaksTrackCopy)
{
    srs_error_t err;

    MockRtcSourceManager sources;
    MockAppConfig config;
    MockRtcSSRCGenerator ssrc_generator;
    SrsRtcPlayerNegotiator negotiator;
    negotiator.config_ = &config;
    negotiator.rtc_sources_ = &sources;
    negotiator.ssrc_generator_ = &ssrc_generator;

    // A stream published with one audio and one video track, as by a WHIP publisher.
    SrsSharedPtr<SrsRtcSource> source(new MockRtcSource());
    sources.mock_source_ = source;
    if (true) {
        SrsUniquePtr<SrsRtcSourceDescription> stream_desc(new SrsRtcSourceDescription());

        SrsRtcTrackDescription *audio_track = new SrsRtcTrackDescription();
        stream_desc->audio_track_desc_ = audio_track;
        audio_track->type_ = "audio";
        audio_track->id_ = "audio-publisher";
        audio_track->ssrc_ = 3001;
        audio_track->direction_ = "recvonly";
        audio_track->media_ = new SrsAudioPayload(111, "opus", 48000, 2);

        SrsRtcTrackDescription *video_track = new SrsRtcTrackDescription();
        stream_desc->video_track_descs_.push_back(video_track);
        video_track->type_ = "video";
        video_track->id_ = "video-publisher";
        video_track->ssrc_ = 3002;
        video_track->direction_ = "recvonly";
        video_track->media_ = new SrsVideoPayload(106, "H264", 90000);

        source->set_stream_desc(stream_desc.get());
    }

    // A play offer of one audio and two video m-lines.
    std::string offer =
        "v=0\r\n"
        "o=- 4611731400430051339 2 IN IP4 127.0.0.1\r\n"
        "s=-\r\n"
        "t=0 0\r\n"
        "a=group:BUNDLE 0 1 2\r\n"
        "a=msid-semantic: WMS\r\n"
        "m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=ice-ufrag:test1234\r\n"
        "a=ice-pwd:testpassword1234567890\r\n"
        "a=setup:actpass\r\n"
        "a=mid:0\r\n"
        "a=recvonly\r\n"
        "a=rtcp-mux\r\n"
        "a=rtpmap:111 opus/48000/2\r\n"
        "m=video 9 UDP/TLS/RTP/SAVPF 106\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=setup:actpass\r\n"
        "a=mid:1\r\n"
        "a=recvonly\r\n"
        "a=rtcp-mux\r\n"
        "a=rtpmap:106 H264/90000\r\n"
        "a=fmtp:106 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f\r\n"
        "m=video 9 UDP/TLS/RTP/SAVPF 106\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=setup:actpass\r\n"
        "a=mid:2\r\n"
        "a=recvonly\r\n"
        "a=rtcp-mux\r\n"
        "a=rtpmap:106 H264/90000\r\n"
        "a=fmtp:106 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f\r\n";

    SrsRtcUserConfig ruc;
    srs_freep(ruc.req_);
    ruc.req_ = new MockRtcAsyncCallRequest("test.vhost", "live", "stream1");
    ruc.publish_ = false;
    HELPER_ASSERT_SUCCESS(ruc.remote_sdp_.parse(offer));

    std::map<uint32_t, SrsRtcTrackDescription *> sub_relations;
    HELPER_EXPECT_SUCCESS(negotiator.negotiate_play_capability(&ruc, sub_relations));

    // Each source track is copied once, though two m-lines ask for video, so the map holds every copy.
    EXPECT_EQ(2, ssrc_generator.count_);
    EXPECT_EQ(2, (int)sub_relations.size());

    // As SrsRtcConnection::add_player does, free what is in the map; nothing else is left to leak.
    for (std::map<uint32_t, SrsRtcTrackDescription *>::iterator it = sub_relations.begin(); it != sub_relations.end(); ++it) {
        SrsRtcTrackDescription *track = it->second;
        srs_freep(track);
    }
}

// The transport of an RTMP player whose connection the client reset after it sent play. macOS refuses SO_SNDBUF on a
// reset socket, so setting the send buffer fails, as ProtocolConnTest.ClientResetsBeforeSocketOptions_ConnectionDropped
// shows on a real socket.
class MemoryAuditResetPlayerTransport : public MockRtmpTransportForDoCycle
{
public:
    int set_socket_buffer_count_;

public:
    MemoryAuditResetPlayerTransport()
    {
        set_socket_buffer_count_ = 0;
    }
    virtual ~MemoryAuditResetPlayerTransport()
    {
    }

public:
    virtual srs_error_t set_socket_buffer(srs_utime_t buffer_v)
    {
        set_socket_buffer_count_++;
        return srs_error_new(ERROR_SOCKET_SNDBUF, "setsockopt fd=%d, r0=%d", 0, 0);
    }
};

// B90: SrsRtmpConn::do_playing() sets the player's socket send buffer and drops the error it returns, so each player
// whose socket refuses SO_SNDBUF leaks one error. A client that sends play and then resets the connection reaches it
// on macOS, once per play. The play stops here at the first thread check; under LeakSanitizer, the error from the
// mock's set_socket_buffer under do_playing is a direct leak.
VOID TEST(MemoryAuditTest, SrsRtmpConn_SocketBufferErrorLeaks)
{
    srs_error_t err;

    MockAppConfigForRtmpConn config;
    MemoryAuditResetPlayerTransport *transport = new MemoryAuditResetPlayerTransport();
    SrsUniquePtr<SrsRtmpConn> conn(new SrsRtmpConn(transport, "192.168.1.100", 1935));
    conn->config_ = &config;
    conn->assemble();

    // The play coroutine is asked to quit at the first check in the play loop.
    MockCoroutineForRtmpConn trd;
    trd.pull_count_ = 1;
    trd.pull_error_ = srs_error_new(ERROR_THREAD_INTERRUPED, "mock interrupted");
    ISrsCoroutine *conn_trd = conn->trd_;
    conn->trd_ = &trd;

    ISrsRequest *req = conn->info_->req_;
    req->vhost_ = "__defaultVhost__";
    req->app_ = "live";
    req->stream_ = "livestream";

    SrsSharedPtr<SrsLiveSource> source(new MockLiveSourceForQueue());
    SrsUniquePtr<MockLiveConsumerForPlayControl> consumer(new MockLiveConsumerForPlayControl(source.get()));

    // The receive thread is not reached before the thread check.
    err = conn->do_playing(source, consumer.get(), NULL);
    EXPECT_EQ(ERROR_THREAD_INTERRUPED, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(1, transport->set_socket_buffer_count_);

    conn->trd_ = conn_trd;
    conn->config_ = NULL;
}

// Runs the RTMPS transport's TLS handshake, the first thing SrsRtmpConn::do_cycle does for a new client.
class MemoryAuditRtmpsHandshake : public ISrsCoroutineHandler
{
public:
    SrsRtmpsTransport *transport_;
    bool done_;

public:
    MemoryAuditRtmpsHandshake(SrsRtmpsTransport *t) : transport_(t), done_(false) {}
    virtual ~MemoryAuditRtmpsHandshake() {}

public:
    virtual srs_error_t cycle()
    {
        srs_error_t err = transport_->handshake();
        srs_freep(err);
        done_ = true;
        return srs_success;
    }
};

// An RTMPS client starts with the TLS handshake, which reads the ClientHello with the same 30 s timeout the RTMP
// handshake has, so a client that connects and sends nothing, or vanishes without a FIN, is dropped as a plain RTMP
// client is, and does not hold its connection, coroutine, fd and TLS state.
VOID TEST(MemoryAuditTest, SrsSslConnection_RtmpsIdleClientNeverTimesOut)
{
    srs_error_t err;

    int port = 0;
    srs_netfd_t lfd = NULL;
    HELPER_ASSERT_SUCCESS(srs_utest_tcp_listen("127.0.0.1", &port, &lfd));

    // The client connects and sends nothing.
    srs_netfd_t stfd = NULL;
    HELPER_ASSERT_SUCCESS(srs_tcp_connect("127.0.0.1", port, 1 * SRS_UTIME_SECONDS, &stfd));
    SrsTcpConnection *idle = new SrsTcpConnection(stfd);

    srs_netfd_t cfd = srs_accept(lfd, NULL, NULL, 1 * SRS_UTIME_SECONDS);
    ASSERT_TRUE(cfd != NULL);

    // The server builds the transport as SrsServer::do_on_tcp_client does for the RTMPS listener.
    SrsUniquePtr<SrsRtmpsTransport> transport(new SrsRtmpsTransport(cfd));
    MemoryAuditRtmpsHandshake handshake(transport.get());
    SrsUniquePtr<SrsSTCoroutine> trd(new SrsSTCoroutine("rtmps", &handshake));
    HELPER_ASSERT_SUCCESS(trd->start());

    // The handshake has loaded the certificate and waits for the ClientHello, within the RTMP timeout.
    srs_usleep(200 * SRS_UTIME_MILLISECONDS);
    EXPECT_FALSE(handshake.done_);
    EXPECT_EQ(SRS_CONSTS_RTMP_TIMEOUT, transport->io()->get_recv_timeout());
    EXPECT_EQ(SRS_CONSTS_RTMP_TIMEOUT, transport->io()->get_send_timeout());

    // The client leaving ends it at once.
    srs_freep(idle);
    for (int i = 0; i < 100 && !handshake.done_; i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
    EXPECT_TRUE(handshake.done_);

    trd->stop();
    srs_close_stfd(lfd);
}

// Sends one packet after another over a WebRTC TCP network until a send fails or max_writes_ are sent, as a player's
// send coroutine sends media, or as the shared timer sends an RTCP report.
class MemoryAuditTcpNetworkSender : public ISrsCoroutineHandler
{
public:
    SrsRtcTcpNetwork *network_;
    int size_;
    int max_writes_;
    int nb_writes_;
    bool done_;

public:
    MemoryAuditTcpNetworkSender(SrsRtcTcpNetwork *network, int size, int max_writes)
    {
        network_ = network;
        size_ = size;
        max_writes_ = max_writes;
        nb_writes_ = 0;
        done_ = false;
    }
    virtual ~MemoryAuditTcpNetworkSender() {}

public:
    virtual srs_error_t cycle()
    {
        srs_error_t err = srs_success;

        char buf[1500];
        memset(buf, 0, sizeof(buf));
        while (nb_writes_ < max_writes_) {
            if ((err = network_->write(buf, size_, NULL)) != srs_success) {
                break;
            }
            nb_writes_++;
        }

        done_ = true;
        return err;
    }
};

// When a WebRTC client over TCP keeps its connection open and stops reading, a send that finds the buffer full
// gives up after the send timeout, and drops the socket, because it may have left part of a packet on the stream. Every
// later send to that client, such as the server-wide shared timer's next RTCP report, fails at once, so no coroutine
// waits on the client, and the session ends when it times out.
VOID TEST(MemoryAuditTest, SrsRtcTcpNetwork_SendToStalledPeerNeverReturns)
{
    srs_error_t err;

    int port = 0;
    srs_netfd_t lfd = NULL;
    HELPER_ASSERT_SUCCESS(srs_utest_tcp_listen("127.0.0.1", &port, &lfd));

    // The client connects and never reads.
    srs_netfd_t client = NULL;
    HELPER_ASSERT_SUCCESS(srs_tcp_connect("127.0.0.1", port, 1 * SRS_UTIME_SECONDS, &client));
    srs_netfd_t server_fd = srs_accept(lfd, NULL, NULL, 1 * SRS_UTIME_SECONDS);
    ASSERT_TRUE(server_fd != NULL);

    // The TCP connection's socket, as the server makes it for a WebRTC client on the TCP port.
    SrsUniquePtr<SrsTcpConnection> skt(new SrsTcpConnection(server_fd));

    MockRtcConnectionForUdpNetwork conn;
    MockEphemeralDelta delta;
    SrsRtcTcpNetwork network(&conn, &delta);
    network.update_sendonly_socket(skt.get());

    // The player's media fills the client's receive buffer and the server's send buffer, then its send waits.
    MemoryAuditTcpNetworkSender media(&network, 1200, 1000000);
    SrsUniquePtr<SrsSTCoroutine> media_trd(new SrsSTCoroutine("media", &media));
    HELPER_ASSERT_SUCCESS(media_trd->start());

    int last_writes = -1;
    for (int i = 0; i < 200 && media.nb_writes_ != last_writes; i++) {
        last_writes = media.nb_writes_;
        srs_usleep(50 * SRS_UTIME_MILLISECONDS);
    }
    ASSERT_GT(media.nb_writes_, 0);

    // The waiting send gives up after the send timeout.
    for (int i = 0; i < 300 && !media.done_; i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
    EXPECT_TRUE(media.done_);

    // The shared timer's report to the client fails at once, without a wait.
    MemoryAuditTcpNetworkSender report(&network, 28, 1000000);
    SrsUniquePtr<SrsSTCoroutine> report_trd(new SrsSTCoroutine("report", &report));
    HELPER_ASSERT_SUCCESS(report_trd->start());
    srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    EXPECT_TRUE(report.done_);
    EXPECT_EQ(0, report.nb_writes_);

    // Reset the connection, so the client's end goes away without a read.
    struct linger lg;
    lg.l_onoff = 1;
    lg.l_linger = 0;
    setsockopt(srs_netfd_fileno(client), SOL_SOCKET, SO_LINGER, (const char *)&lg, sizeof(lg));
    srs_close_stfd(client);
    report_trd->stop();
    media_trd->stop();

    network.update_sendonly_socket(NULL);
    srs_close_stfd(lfd);
}

// A WebRTC publisher jumps its RTP sequence by 32767, the most the forward check accepts. SrsRtcRecvTrack::on_nack
// asks the ring buffer for the gap, and SrsRtpNackForReceiver::insert added every missing seq to its map, 32766 nodes,
// before check_queue_size saw the list was over its limit and cleared it. The clear also resets the ring buffer, so
// the next packet starts it again and the one after can jump again: every two packets cost 32766 map inserts and
// frees on the one ST thread. The list should stop at its limit.
VOID TEST(MemoryAuditTest, SrsRtpNackForReceiver_SequenceJumpInsertsWholeGap)
{
    srs_error_t err = srs_success;

    // The list never holds more than its limit, whatever gap it is given.
    if (true) {
        SrsRtpRingBuffer rtp(1000);
        SrsRtpNackForReceiver nack(&rtp, 1000 * 2 / 3);
        nack.insert(100, 100 + 32767);
        EXPECT_EQ(1000 * 2 / 3, (int)nack.queue_.size());

        // The list is full, so it is cleared.
        nack.check_queue_size();
        EXPECT_TRUE(nack.queue_.empty());

        // A gap under the limit is kept whole.
        nack.insert(100, 110);
        EXPECT_EQ(10, (int)nack.queue_.size());
    }

    MockRtcPacketReceiver receiver;
    MockCircuitBreaker breaker;
    breaker.hybrid_high_water_level_ = false;

    SrsRtcTrackDescription desc;
    desc.type_ = "video";
    desc.id_ = "video-track";
    desc.ssrc_ = 0x2345;
    desc.is_active_ = true;
    SrsUniquePtr<SrsRtcVideoRecvTrack> track(new SrsRtcVideoRecvTrack(&receiver, &desc, false));
    track->circuit_breaker_ = &breaker;

    // Through the track: 100 pairs, a packet that starts the ring buffer, then one 32767 after it.
    for (int i = 0; i < 100; i++) {
        uint16_t seqs[] = {(uint16_t)(i * 7), (uint16_t)(i * 7 + 32767)};
        for (int j = 0; j < 2; j++) {
            SrsRtpPacket pkt;
            pkt.header_.set_ssrc(0x2345);
            pkt.header_.set_sequence(seqs[j]);
            SrsRtpPacket *p = &pkt;
            HELPER_ASSERT_SUCCESS(track->on_nack(&p));
        }
    }

    // Each jump fills the list to its limit, which is then cleared, so the list is empty.
    EXPECT_TRUE(track->nack_receiver_->queue_.empty());
}

// When WebRTC over TCP shares the HTTP port, the server peeks at each new client's first bytes to tell WebRTC from
// HTTP. The peek waits for the bytes with no recv timeout, so it used to hold the listener's own coroutine: a client
// that connects and sends fewer bytes, or none, such as a browser's speculative preconnect, kept every later client
// from being taken. The peek should run in the client's own coroutine.
VOID TEST(MemoryAuditTest, SrsBufferedReadWriter_IdleClientBlocksSharedHttpPort)
{
    srs_error_t err;

    SrsResourceManager manager("utest-shared-port-idle");
    manager.assemble();
    HELPER_ASSERT_SUCCESS(manager.start());

    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->assemble();
    server->conn_manager_ = &manager;
    server->reuse_rtc_over_server_ = true;

    MockServerTcpAcceptor acceptor(server.get(), server->http_listener_);
    SrsUniquePtr<SrsTcpListener> listener(new SrsTcpListener(&acceptor));
    int port = srs_utest_random_port();
    listener->set_endpoint("127.0.0.1", port);
    HELPER_ASSERT_SUCCESS(listener->listen());

    // The idle client sends 4 bytes, fewer than the peek needs, and then waits. Its peek is counted as a connection.
    srs_netfd_t stfd = NULL;
    HELPER_ASSERT_SUCCESS(srs_tcp_connect("127.0.0.1", port, 1 * SRS_UTIME_SECONDS, &stfd));
    SrsTcpConnection *idle = new SrsTcpConnection(stfd);
    HELPER_ASSERT_SUCCESS(idle->write((void *)"GET ", 4, NULL));
    for (int i = 0; i < 100 && manager.empty(); i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
    EXPECT_EQ(1, (int)manager.size());
    EXPECT_TRUE(acceptor.accepted_);
    EXPECT_TRUE(acceptor.error_.empty());

    // A normal HTTP client comes after it and sends its whole request line.
    stfd = NULL;
    HELPER_ASSERT_SUCCESS(srs_tcp_connect("127.0.0.1", port, 1 * SRS_UTIME_SECONDS, &stfd));
    SrsTcpConnection *web = new SrsTcpConnection(stfd);
    const char http[] = "GET / HTTP/1.1\r\n";
    HELPER_ASSERT_SUCCESS(web->write((void *)http, sizeof(http) - 1, NULL));

    // The server takes the HTTP client while the idle one still waits: its peek and the HTTP connection.
    int nn_http = 0;
    for (int i = 0; i < 100 && nn_http == 0; i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
        for (int j = 0; j < (int)manager.size(); j++) {
            nn_http += dynamic_cast<SrsHttpxConn *>(manager.at(j)) ? 1 : 0;
        }
    }
    EXPECT_EQ(1, nn_http);
    for (int i = 0; i < 100 && manager.size() != 2; i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
    EXPECT_EQ(2, (int)manager.size());

    // When the idle client leaves, its peek fails and only the HTTP connection is left.
    srs_freep(idle);
    for (int i = 0; i < 100 && manager.size() != 1; i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
    EXPECT_EQ(1, (int)manager.size());
    EXPECT_TRUE(dynamic_cast<SrsHttpxConn *>(manager.at(0)) != NULL);

    // The HTTP client leaves too, and its connection ends.
    srs_freep(web);
    for (int i = 0; i < 100 && !manager.empty(); i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
    EXPECT_TRUE(manager.empty());

    server->conn_manager_ = NULL;
}

// A listener coroutine whose pull() succeeds a fixed number of times, then stops the accept loop.
class MemoryAuditCountedCoroutine : public ISrsCoroutine
{
public:
    int nb_pulls_;
    int max_pulls_;
    SrsContextId cid_;

public:
    MemoryAuditCountedCoroutine(int max_pulls) : nb_pulls_(0), max_pulls_(max_pulls) {}
    virtual ~MemoryAuditCountedCoroutine() {}

public:
    virtual srs_error_t start() { return srs_success; }
    virtual void stop() {}
    virtual void interrupt() {}
    virtual srs_error_t pull()
    {
        if (++nb_pulls_ > max_pulls_) {
            return srs_error_new(ERROR_THREAD_INTERRUPED, "stop");
        }
        return srs_success;
    }
    virtual const SrsContextId &cid() { return cid_; }
    virtual void set_cid(const SrsContextId &cid) { cid_ = cid; }
};

// Any other coroutine of the server, such as a connection that would close its fd.
class MemoryAuditProbeCoroutine : public ISrsCoroutineHandler
{
public:
    bool ran_;

public:
    MemoryAuditProbeCoroutine() : ran_(false) {}
    virtual ~MemoryAuditProbeCoroutine() {}

public:
    virtual srs_error_t cycle()
    {
        ran_ = true;
        return srs_success;
    }
};

// When the process is out of fds, accept fails with EMFILE right away and ST returns NULL without waiting.
// SrsTcpListener::cycle() logged and ignored the error and called accept again at once, so the loop never yielded
// while a client waited: no other coroutine ran, none closed an fd, and the server spun at 100% CPU. Linux leaves the
// client in the backlog on EMFILE, so the spin never ended and the whole server hung; macOS drops that client, so it
// spun as long as new clients kept coming. Every TCP listener runs this loop. It should wait after an accept error.
#ifndef _WIN32 // POSIX only: RLIMIT_NOFILE
VOID TEST(MemoryAuditTest, SrsTcpListener_AcceptErrorSpinsWithoutYield)
{
    srs_error_t err = srs_success;

    int port = 0;
    srs_netfd_t lfd = NULL;
    HELPER_ASSERT_SUCCESS(srs_utest_tcp_listen("127.0.0.1", &port, &lfd));

    // Clients wait in the backlog, more than the failed accepts below, since macOS drops one per EMFILE.
    const int nb_loops = 5;
    std::vector<srs_netfd_t> clients;
    int max_fd = srs_netfd_fileno(lfd);
    for (int i = 0; i < nb_loops + 10; i++) {
        srs_netfd_t client = NULL;
        HELPER_ASSERT_SUCCESS(srs_tcp_connect("127.0.0.1", port, 1 * SRS_UTIME_SECONDS, &client));
        clients.push_back(client);
        max_fd = srs_max(max_fd, srs_netfd_fileno(client));
    }

    SrsUniquePtr<SrsTcpListener> listener(new SrsTcpListener(NULL));
    listener->lfd_ = lfd;
    srs_freep(listener->trd_);
    listener->trd_ = new MemoryAuditCountedCoroutine(nb_loops);

    // Control: the probe runs as soon as the test coroutine yields.
    if (true) {
        MemoryAuditProbeCoroutine probe;
        SrsUniquePtr<SrsSTCoroutine> trd(new SrsSTCoroutine("probe", &probe));
        HELPER_ASSERT_SUCCESS(trd->start());
        EXPECT_FALSE(probe.ran_);
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        EXPECT_TRUE(probe.ran_);
    }

    // Run the process out of fds: lower the soft limit and fill every free fd number under it.
    struct rlimit saved;
    ASSERT_EQ(0, getrlimit(RLIMIT_NOFILE, &saved));
    struct rlimit lowered = saved;
    lowered.rlim_cur = max_fd + 1;
    ASSERT_EQ(0, setrlimit(RLIMIT_NOFILE, &lowered));
    std::vector<int> fillers;
    for (int fd = dup(0); fd >= 0; fd = dup(0)) {
        fillers.push_back(fd);
    }
    int dup_errno = errno;

    // Precondition: accept fails.
    err = listener->do_cycle();
    bool accept_failed = (err != srs_success);
    srs_freep(err);

    // Then the accept loop runs nb_loops times, while the probe waits to run.
    MemoryAuditProbeCoroutine probe;
    SrsUniquePtr<SrsSTCoroutine> trd(new SrsSTCoroutine("probe", &probe));
    HELPER_ASSERT_SUCCESS(trd->start());
    err = listener->cycle();
    srs_freep(err);
    bool ran_during_accept_loop = probe.ran_;

    for (int i = 0; i < (int)fillers.size(); i++) {
        ::close(fillers[i]);
    }
    ASSERT_EQ(0, setrlimit(RLIMIT_NOFILE, &saved));

    EXPECT_EQ(EMFILE, dup_errno);
    EXPECT_TRUE(accept_failed);
    EXPECT_LT(nb_loops, ((MemoryAuditCountedCoroutine *)listener->trd_)->nb_pulls_);

    // The loop waits after each failed accept, so other coroutines run and can close their fds.
    EXPECT_TRUE(ran_during_accept_loop);

    for (int i = 0; i < (int)clients.size(); i++) {
        srs_close_stfd(clients[i]);
    }
}
#endif

// Closing a TCP listener stops its coroutine, which interrupts the accept. The accept then fails like an accept out of
// fds, and the listener waited after it before it saw the stop, so every listener's close took that wait. A graceful
// quit closes the listeners one by one, so each port stayed open that much longer than the one before. A listener
// should close at once.
VOID TEST(MemoryAuditTest, SrsTcpListener_CloseWaitsAfterInterruptedAccept)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsTcpListener> listener(new SrsTcpListener(NULL));
    listener->set_endpoint("127.0.0.1", srs_utest_random_port());
    HELPER_ASSERT_SUCCESS(listener->listen());

    // The listener's coroutine waits in accept.
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    srs_utime_t starttime = srs_time_now_realtime();
    listener->close();
    srs_utime_t duration = srs_time_now_realtime() - starttime;

    EXPECT_LT(duration, 50 * SRS_UTIME_MILLISECONDS);
}
