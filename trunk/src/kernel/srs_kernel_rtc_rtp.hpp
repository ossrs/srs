//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_RTC_RTP_HPP
#define SRS_KERNEL_RTC_RTP_HPP

#include <srs_core.hpp>

#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_packet.hpp>

#include <list>
#include <string>
#include <vector>

// Drops one received video packet of PT 109, the third one, to verify the NACK logic. Restart SRS after each test,
// since a static counter controls the drop.
#ifdef SRS_DEBUG_NACK_DROP
#define SRS_NACK_DEBUG_DROP_ENABLED
#endif
#define SRS_NACK_DEBUG_DROP_PACKET_PT 109
#define SRS_NACK_DEBUG_DROP_PACKET_N 3

// Whether to log every NACK request and every retransmission, plain or RTX, packet by packet, to verify the NACK and
// RTX logic against the network simulator or the debug drop above. Never enabled in a release build.
#if defined(SRS_SIMULATOR) || defined(SRS_DEBUG_NACK_DROP)
#define SRS_NACK_DEBUG_LOG_ENABLED
#endif

class SrsRtpPacket;
class SrsMemoryBlock;

const int kRtpPacketSize = 1500;

// Leaves room for SRTP and padding, and stays below the MTU: WebRTC sets its RTP max size to kVideoMtu = 1200.
// See @doc https://groups.google.com/g/discuss-webrtc/c/gH5ysR3SoZI
const int kRtpMaxPayloadSize = kRtpPacketSize - 300;

const int kRtpHeaderFixedSize = 12;
const uint8_t kRtpMarker = 0x80;

// The H.264 NALU header type mask.
const uint8_t kNalTypeMask = 0x1F;

// @see: https://tools.ietf.org/html/rfc6184#section-5.2
const uint8_t kStapA = 24;
// @see: https://tools.ietf.org/html/rfc6184#section-5.2
const uint8_t kFuA = 28;

// @see: https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.2
const uint8_t kStapHevc = 48;
// @see: https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.3
const uint8_t kFuHevc = 49;

// @see: https://tools.ietf.org/html/rfc6184#section-5.8
const uint8_t kStart = 0x80; // FU header start bit
const uint8_t kEnd = 0x40;   // FU header end bit

class SrsBuffer;
class SrsRtpRawPayload;
class SrsRtpFUAPayload2;
class SrsRtpExtensionTypes;

// Each returns 0 for a packet too short to hold the field.
uint32_t srs_rtp_fast_parse_ssrc(char *buf, int size);
uint16_t srs_rtp_fast_parse_seq(char *buf, int size);
uint8_t srs_rtp_fast_parse_pt(char *buf, int size);
srs_error_t srs_rtp_fast_parse_twcc(char *buf, int size, uint8_t twcc_id, uint16_t &twcc_sn);

// The RTP sequence is a uint16 that wraps.
// @remark srs_rtp_seq_distance(0, 32768)>0 is TRUE by https://mp.weixin.qq.com/s/JZTInmlB9FUWXBQw_7NYqg
//      but FALSE in the WebRTC jitter buffer, which this follows.
inline int16_t srs_rtp_seq_distance(const uint16_t &prev_value, const uint16_t &value)
{
    return (int16_t)(value - prev_value);
}
inline int32_t srs_rtp_ts_distance(const uint32_t &prev_value, const uint32_t &value)
{
    return (int32_t)(value - prev_value);
}

struct SrsSeqCompareLess {
    bool operator()(const uint16_t &pre_value, const uint16_t &value) const
    {
        return srs_rtp_seq_distance(pre_value, value) > 0;
    }
};

bool srs_seq_is_newer(uint16_t value, uint16_t pre_value);
bool srs_seq_is_rollback(uint16_t value, uint16_t pre_value);
int32_t srs_seq_distance(uint16_t value, uint16_t pre_value);

enum SrsRtpExtensionType {
    kRtpExtensionNone,
    kRtpExtensionTransportSequenceNumber,
    kRtpExtensionAudioLevel,
    kRtpExtensionNumberOfExtensions // Must be the last entity in the enum.
};

const std::string kAudioLevelUri = "urn:ietf:params:rtp-hdrext:ssrc-audio-level";

struct SrsExtensionInfo {
    SrsRtpExtensionType type;
    std::string uri;
};

const SrsExtensionInfo kExtensions[] = {
    {kRtpExtensionTransportSequenceNumber, std::string("http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01")},
    {kRtpExtensionAudioLevel, kAudioLevelUri},
};

class SrsRtpExtensionTypes
{
public:
    static const SrsRtpExtensionType kInvalidType = kRtpExtensionNone;
    static const int kInvalidId = 0;

public:
    bool register_by_uri(int id, std::string uri);
    SrsRtpExtensionType get_type(int id) const;

public:
    SrsRtpExtensionTypes();
    virtual ~SrsRtpExtensionTypes();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool register_id(int id, SrsRtpExtensionType type, std::string uri);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    uint8_t ids_[kRtpExtensionNumberOfExtensions];
};

// The extensions extend no class, for performance.
class SrsRtpExtensionTwcc // : public ISrsCodec
{
    bool has_twcc_;
    uint8_t id_;
    uint16_t sn_;

public:
    SrsRtpExtensionTwcc();
    virtual ~SrsRtpExtensionTwcc();

public:
    inline bool exists() { return has_twcc_; }
    uint8_t get_id();
    void set_id(uint8_t id);
    uint16_t get_sn();
    void set_sn(uint16_t sn);

public:
    virtual srs_error_t decode(SrsBuffer *buf);
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual uint64_t nb_bytes();
};

// The extensions extend no class, for performance.
class SrsRtpExtensionOneByte // : public ISrsCodec
{
    bool has_ext_;
    int id_;
    uint8_t value_;

public:
    SrsRtpExtensionOneByte();
    virtual ~SrsRtpExtensionOneByte();

public:
    inline bool exists() { return has_ext_; }
    int get_id() { return id_; }
    uint8_t get_value() { return value_; }
    void set_id(int id);
    void set_value(uint8_t value);

public:
    virtual srs_error_t decode(SrsBuffer *buf);
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual uint64_t nb_bytes() { return 2; };
};

// The extensions extend no class, for performance.
class SrsRtpExtensions // : public ISrsCodec
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool has_ext_;
    // Off by default, since srs_rtp_fast_parse_twcc reads the TWCC sequence number.
    bool decode_twcc_extension_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Refers to the extension types of the publish stream.
    SrsRtpExtensionTypes *types_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsRtpExtensionTwcc twcc_;
    SrsRtpExtensionOneByte audio_level_;

public:
    SrsRtpExtensions();
    virtual ~SrsRtpExtensions();

public:
    void enable_twcc_decode() { decode_twcc_extension_ = true; }
    inline bool exists() { return has_ext_; }
    void set_types_(SrsRtpExtensionTypes *types);
    srs_error_t get_twcc_sequence_number(uint16_t &twcc_sn);
    srs_error_t set_twcc_sequence_number(uint8_t id, uint16_t sn);
    srs_error_t get_audio_level(uint8_t &level);
    srs_error_t set_audio_level(int id, uint8_t level);

public:
    virtual srs_error_t decode(SrsBuffer *buf);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t decode_0xbede(SrsBuffer *buf);

public:
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual uint64_t nb_bytes();
};

// The header extends no class, for performance.
class SrsRtpHeader // : public ISrsCodec
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    uint8_t padding_length_;
    uint8_t cc_;
    bool marker_;
    uint8_t payload_type_;
    uint16_t sequence_;
    uint32_t timestamp_;
    uint32_t ssrc_;
    uint32_t csrc_[15];
    SrsRtpExtensions extensions_;
    bool ignore_padding_;

public:
    SrsRtpHeader();
    virtual ~SrsRtpHeader();

public:
    virtual srs_error_t decode(SrsBuffer *buf);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t parse_extensions(SrsBuffer *buf);

public:
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual uint64_t nb_bytes();

public:
    void enable_twcc_decode() { extensions_.enable_twcc_decode(); }
    void set_marker(bool v);
    bool get_marker() const;
    void set_payload_type(uint8_t v);
    uint8_t get_payload_type() const;
    void set_sequence(uint16_t v);
    uint16_t get_sequence() const;
    void set_timestamp(uint32_t v);
    uint32_t get_timestamp() const;
    void set_ssrc(uint32_t v);
    inline uint32_t get_ssrc() const { return ssrc_; }
    void set_padding(uint8_t v);
    uint8_t get_padding() const;
    void set_extensions(SrsRtpExtensionTypes *extmap);
    void ignore_padding(bool v);
    srs_error_t get_twcc_sequence_number(uint16_t &twcc_sn);
    srs_error_t set_twcc_sequence_number(uint8_t id, uint16_t sn);
};

class ISrsRtpPayloader : public ISrsCodec
{
public:
    ISrsRtpPayloader();
    virtual ~ISrsRtpPayloader();

public:
    virtual ISrsRtpPayloader *copy() = 0;
};

// Avoids dynamic_cast on the payload, for performance.
enum SrsRtpPacketPayloadType {
    SrsRtpPacketPayloadTypeRaw,
    SrsRtpPacketPayloadTypeFUA2,
    SrsRtpPacketPayloadTypeFUAHevc2,
    SrsRtpPacketPayloadTypeFUA,
    SrsRtpPacketPayloadTypeFUAHevc,
    SrsRtpPacketPayloadTypeNALU,
    SrsRtpPacketPayloadTypeSTAP,
    SrsRtpPacketPayloadTypeSTAPHevc,
    SrsRtpPacketPayloadTypeUnknown,
};

class ISrsRtpPacketDecodeHandler
{
public:
    ISrsRtpPacketDecodeHandler();
    virtual ~ISrsRtpPacketDecodeHandler();

public:
    // The packet does not know its payload format, so the handler picks the payloader.
    virtual void on_before_decode_payload(SrsRtpPacket *pkt, SrsBuffer *buf, ISrsRtpPayloader **ppayload, SrsRtpPacketPayloadType *ppt) = 0;
};

class SrsRtpPacket
{
public:
    SrsRtpHeader header_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtpPayloader *payload_;
    SrsRtpPacketPayloadType payload_type_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Copies of the packet share this block, which may be larger than the packet. It holds the whole RTP packet when
    // parsed from a buffer, or only the payload when built from RTMP.
    SrsSharedPtr<SrsMemoryBlock>
        shared_buffer_;
    int actual_buffer_size_;

public:
    // The NALU type of the first payload byte, for video only.
    uint8_t nalu_type_;
    // For the RTMP bridge or SFU source.
    SrsFrameType frame_type_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int cached_payload_size_;
    ISrsRtpPacketDecodeHandler *decode_handler_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int64_t avsync_time_;

public:
    SrsRtpPacket();
    virtual ~SrsRtpPacket();

public:
    char *wrap(int size);
    char *wrap(char *data, int size);
    char *wrap(SrsSharedPtr<SrsMemoryBlock> block);
    virtual SrsRtpPacket *copy();

public:
    void enable_twcc_decode() { header_.enable_twcc_decode(); }
    void set_payload(ISrsRtpPayloader *p, SrsRtpPacketPayloadType pt)
    {
        payload_ = p;
        payload_type_ = pt;
    }
    ISrsRtpPayloader *payload() { return payload_; }
    void set_padding(int size);
    void add_padding(int size);
    void set_decode_handler(ISrsRtpPacketDecodeHandler *h);
    bool is_audio();
    void set_extension_types(SrsRtpExtensionTypes *v);

public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual srs_error_t decode(SrsBuffer *buf);

public:
    bool is_keyframe(SrsVideoCodecId codec_id);
    // In milliseconds.
    void set_avsync_time(int64_t avsync_time) { avsync_time_ = avsync_time; }
    int64_t get_avsync_time() const { return avsync_time_; }
};

class SrsRtpRawPayload : public ISrsRtpPayloader
{
public:
    // Points into the packet's shared buffer, which owns the bytes.
    char *payload_;
    int nn_payload_;

public:
    // The whole payload as one sample.
    SrsNaluSample *sample_;

public:
    SrsRtpRawPayload();
    virtual ~SrsRtpRawPayload();

public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual srs_error_t decode(SrsBuffer *buf);
    virtual ISrsRtpPayloader *copy();
};

// The RFC 4588 RTX payload: the two-byte original sequence number (OSN) followed by the original payload. The payload
// is owned: the sender stores a copy of the cached payload, so the cached packet is never mutated, and decode creates a
// raw payload. Like every payloader, the payload references the media bytes of its packet and never copies them.
class SrsRtpRtxPayload : public ISrsRtpPayloader
{
public:
    uint16_t osn_;
    ISrsRtpPayloader *payload_;

public:
    SrsRtpRtxPayload();
    virtual ~SrsRtpRtxPayload();

public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual srs_error_t decode(SrsBuffer *buf);
    virtual ISrsRtpPayloader *copy();
};

class SrsRtpRawNALUs : public ISrsRtpPayloader
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // Owns the samples, but not the bytes they point to.
    std::vector<SrsNaluSample *>
        nalus_;
    int nn_bytes_;
    int cursor_;

public:
    SrsRtpRawNALUs();
    virtual ~SrsRtpRawNALUs();

public:
    void push_back(SrsNaluSample *sample);

public:
    uint8_t skip_bytes(int count);
    // The caller owns the returned samples, but not the bytes they point to.
    srs_error_t read_samples(std::vector<SrsNaluSample *> &samples, int packet_size);

public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual srs_error_t decode(SrsBuffer *buf);
    virtual ISrsRtpPayloader *copy();
};

// STAP-A.
class SrsRtpSTAPPayload : public ISrsRtpPayloader
{
public:
    SrsAvcNaluType nri_;
    // Owns the samples, but not the bytes they point to.
    std::vector<SrsNaluSample *> nalus_;

public:
    SrsRtpSTAPPayload();
    virtual ~SrsRtpSTAPPayload();

public:
    SrsNaluSample *get_sps();
    SrsNaluSample *get_pps();
    SrsNaluSample *get_idr();

public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual srs_error_t decode(SrsBuffer *buf);
    virtual ISrsRtpPayloader *copy();
};

// FU-A, a fragment of one NALU.
class SrsRtpFUAPayload : public ISrsRtpPayloader
{
public:
    SrsAvcNaluType nri_;
    // The FU header.
    bool start_;
    bool end_;
    SrsAvcNaluType nalu_type_;
    // Owns the samples, but not the bytes they point to.
    std::vector<SrsNaluSample *> nalus_;

public:
    SrsRtpFUAPayload();
    virtual ~SrsRtpFUAPayload();

public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual srs_error_t decode(SrsBuffer *buf);
    virtual ISrsRtpPayloader *copy();
};

// FU-A, a fragment of one NALU.
class SrsRtpFUAPayload2 : public ISrsRtpPayloader
{
public:
    SrsAvcNaluType nri_;
    // The FU header.
    bool start_;
    bool end_;
    SrsAvcNaluType nalu_type_;
    char *payload_;
    int size_;

public:
    SrsRtpFUAPayload2();
    virtual ~SrsRtpFUAPayload2();

public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual srs_error_t decode(SrsBuffer *buf);
    virtual ISrsRtpPayloader *copy();
};

class SrsRtpSTAPPayloadHevc : public ISrsRtpPayloader
{
public:
    // Owns the samples, but not the bytes they point to.
    std::vector<SrsNaluSample *> nalus_;

public:
    SrsRtpSTAPPayloadHevc();
    virtual ~SrsRtpSTAPPayloadHevc();

public:
    SrsNaluSample *get_vps();
    SrsNaluSample *get_sps();
    SrsNaluSample *get_pps();
    SrsNaluSample *get_idr();

public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual srs_error_t decode(SrsBuffer *buf);
    virtual ISrsRtpPayloader *copy();
};

// HEVC FU, a fragment of one NALU.
class SrsRtpFUAPayloadHevc : public ISrsRtpPayloader
{
public:
    // The FU header.
    bool start_;
    bool end_;
    SrsHevcNaluType nalu_type_;
    // Owns the samples, but not the bytes they point to.
    std::vector<SrsNaluSample *> nalus_;

public:
    SrsRtpFUAPayloadHevc();
    virtual ~SrsRtpFUAPayloadHevc();

public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual srs_error_t decode(SrsBuffer *buf);
    virtual ISrsRtpPayloader *copy();
};

// HEVC FU, a fragment of one NALU.
class SrsRtpFUAPayloadHevc2 : public ISrsRtpPayloader
{
public:
    bool start_;
    bool end_;
    SrsHevcNaluType nalu_type_;
    char *payload_;
    int size_;

public:
    SrsRtpFUAPayloadHevc2();
    virtual ~SrsRtpFUAPayloadHevc2();

public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual srs_error_t decode(SrsBuffer *buf);
    virtual ISrsRtpPayloader *copy();
};

#endif
