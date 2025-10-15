//
// Copyright (c) 2013-2025 The SRS Authors
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

// Indicates whether to enable debugging for NACK. If enabled, the specified PT(109)
// video packet will always be dropped. You can use this option to verify the NACK
// logic. Note that you should restart SRS after each test, as a global variable
// controls the debugging.
#ifdef SRS_DEBUG_NACK_DROP
#define SRS_NACK_DEBUG_DROP_ENABLED
#endif
#define SRS_NACK_DEBUG_DROP_PACKET_PT 109
#define SRS_NACK_DEBUG_DROP_PACKET_N 3

class SrsRtpPacket;
class SrsMemoryBlock;

// The RTP packet max size, should never exceed this size.
const int kRtpPacketSize = 1500;

// The RTP payload max size, reserved some paddings for SRTP as such:
//      kRtpPacketSize = kRtpMaxPayloadSize + paddings
// For example, if kRtpPacketSize is 1500, recommend to set kRtpMaxPayloadSize to 1400,
// which reserves 100 bytes for SRTP or paddings.
// otherwise, the kRtpPacketSize must less than MTU, in webrtc source code,
// the rtp max size is assigned by kVideoMtu = 1200.
// so we set kRtpMaxPayloadSize = 1200.
// see @doc https://groups.google.com/g/discuss-webrtc/c/gH5ysR3SoZI
const int kRtpMaxPayloadSize = kRtpPacketSize - 300;

const int kRtpHeaderFixedSize = 12;
const uint8_t kRtpMarker = 0x80;

// H.264 nalu header type mask.
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
const uint8_t kStart = 0x80; // Fu-header start bit
const uint8_t kEnd = 0x40;   // Fu-header end bit

class SrsBuffer;
class SrsRtpRawPayload;
class SrsRtpFUAPayload2;
class SrsRtpExtensionTypes;

// Fast parse the SSRC from RTP packet. Return 0 if invalid.
uint32_t srs_rtp_fast_parse_ssrc(char *buf, int size);
uint16_t srs_rtp_fast_parse_seq(char *buf, int size);
uint8_t srs_rtp_fast_parse_pt(char *buf, int size);
srs_error_t srs_rtp_fast_parse_twcc(char *buf, int size, uint8_t twcc_id, uint16_t &twcc_sn);

// The "distance" between two uint16 number, for example:
//      distance(prev_value=3, value=5) is (int16_t)(uint16_t)((uint16_t)3-(uint16_t)5) is -2
//      distance(prev_value=3, value=65534) is (int16_t)(uint16_t)((uint16_t)3-(uint16_t)65534) is 5
//      distance(prev_value=65532, value=65534) is (int16_t)(uint16_t)((uint16_t)65532-(uint16_t)65534) is -2
// For RTP sequence, it's only uint16 and may flip back, so 3 maybe 3+0xffff.
// @remark Note that srs_rtp_seq_distance(0, 32768)>0 is TRUE by https://mp.weixin.qq.com/s/JZTInmlB9FUWXBQw_7NYqg
//      but for WebRTC jitter buffer it's FALSE and we follow it.
// @remark For srs_rtp_seq_distance(32768, 0)>0, it's FALSE definitely.
inline int16_t srs_rtp_seq_distance(const uint16_t &prev_value, const uint16_t &value)
{
    return (int16_t)(value - prev_value);
}
inline int32_t srs_rtp_ts_distance(const uint32_t &prev_value, const uint32_t &value)
{
    return (int32_t)(value - prev_value);
}

// For map to compare the sequence of RTP.
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

// Note that the extensions should never extends from any class, for performance.
class SrsRtpExtensionTwcc // : public ISrsCodec
{
    bool has_twcc_;
    uint8_t id_;
    uint16_t sn_;

public:
    SrsRtpExtensionTwcc();
    virtual ~SrsRtpExtensionTwcc();

public:
    inline bool exists() { return has_twcc_; } // SrsRtpExtensionTwcc::exists
    uint8_t get_id();
    void set_id(uint8_t id);
    uint16_t get_sn();
    void set_sn(uint16_t sn);

public:
    // ISrsCodec
    virtual srs_error_t decode(SrsBuffer *buf);
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual uint64_t nb_bytes();
};

// Note that the extensions should never extends from any class, for performance.
class SrsRtpExtensionOneByte // : public ISrsCodec
{
    bool has_ext_;
    int id_;
    uint8_t value_;

public:
    SrsRtpExtensionOneByte();
    virtual ~SrsRtpExtensionOneByte();

public:
    inline bool exists() { return has_ext_; } // SrsRtpExtensionOneByte::exists
    int get_id() { return id_; }
    uint8_t get_value() { return value_; }
    void set_id(int id);
    void set_value(uint8_t value);

public:
    // ISrsCodec
    virtual srs_error_t decode(SrsBuffer *buf);
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual uint64_t nb_bytes() { return 2; };
};

// Note that the extensions should never extends from any class, for performance.
class SrsRtpExtensions // : public ISrsCodec
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    bool has_ext_;
    // by default, twcc isnot decoded. Because it is decoded by fast function(srs_rtp_fast_parse_twcc)
    bool decode_twcc_extension_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The extension types is used to decode the packet, which is reference to
    // the types in publish stream.
    SrsRtpExtensionTypes *types_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsRtpExtensionTwcc twcc_;
    SrsRtpExtensionOneByte audio_level_;

public:
    SrsRtpExtensions();
    virtual ~SrsRtpExtensions();

public:
    void enable_twcc_decode() { decode_twcc_extension_ = true; } // SrsRtpExtensions::enable_twcc_decode
    inline bool exists() { return has_ext_; }                    // SrsRtpExtensions::exists
    void set_types_(SrsRtpExtensionTypes *types);
    srs_error_t get_twcc_sequence_number(uint16_t &twcc_sn);
    srs_error_t set_twcc_sequence_number(uint8_t id, uint16_t sn);
    srs_error_t get_audio_level(uint8_t &level);
    srs_error_t set_audio_level(int id, uint8_t level);
    // ISrsCodec
public:
    virtual srs_error_t decode(SrsBuffer *buf);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t decode_0xbede(SrsBuffer *buf);

public:
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual uint64_t nb_bytes();
};

// Note that the header should never extends from any class, for performance.
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
    void enable_twcc_decode() { extensions_.enable_twcc_decode(); } // SrsRtpHeader::enable_twcc_decode
    void set_marker(bool v);
    bool get_marker() const;
    void set_payload_type(uint8_t v);
    uint8_t get_payload_type() const;
    void set_sequence(uint16_t v);
    uint16_t get_sequence() const;
    void set_timestamp(uint32_t v);
    uint32_t get_timestamp() const;
    void set_ssrc(uint32_t v);
    inline uint32_t get_ssrc() const { return ssrc_; } // SrsRtpHeader::get_ssrc
    void set_padding(uint8_t v);
    uint8_t get_padding() const;
    void set_extensions(SrsRtpExtensionTypes *extmap);
    void ignore_padding(bool v);
    srs_error_t get_twcc_sequence_number(uint16_t &twcc_sn);
    srs_error_t set_twcc_sequence_number(uint8_t id, uint16_t sn);
};

// The common payload interface for RTP packet.
class ISrsRtpPayloader : public ISrsCodec
{
public:
    ISrsRtpPayloader();
    virtual ~ISrsRtpPayloader();

public:
    virtual ISrsRtpPayloader *copy() = 0;
};

// The payload type, for performance to avoid dynamic cast.
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
    // We don't know the actual payload, so we depends on external handler.
    virtual void on_before_decode_payload(SrsRtpPacket *pkt, SrsBuffer *buf, ISrsRtpPayloader **ppayload, SrsRtpPacketPayloadType *ppt) = 0;
};

// The RTP packet with cached shared message.
class SrsRtpPacket
{
    // RTP packet fields.
public:
    SrsRtpHeader header_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRtpPayloader *payload_;
    SrsRtpPacketPayloadType payload_type_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The original shared memory block, all RTP packets can refer to its data.
    // Note that the size of shared memory block, is not the packet size, it's a larger aligned buffer.
    // @remark Note that it may point to the whole RTP packet(for RTP parser, which decode RTP packet from buffer),
    //      and it may point to the RTP payload(for RTMP to RTP, which build RTP header and payload).
    SrsSharedPtr<SrsMemoryBlock>
        shared_buffer_;
    // The size of RTP packet or RTP payload.
    int actual_buffer_size_;
    // Helper fields.
public:
    // The first byte as nalu type, for video decoder only.
    uint8_t nalu_type_;
    // The frame type, for RTMP bridge or SFU source.
    SrsFrameType frame_type_;
    // Fast cache for performance.
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // The cached payload size for packet.
    int cached_payload_size_;
    // The helper handler for decoder, use RAW payload if NULL.
    ISrsRtpPacketDecodeHandler *decode_handler_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    int64_t avsync_time_;

public:
    SrsRtpPacket();
    virtual ~SrsRtpPacket();

public:
    // Wrap buffer to shared_message, which is managed by us.
    char *wrap(int size);
    char *wrap(char *data, int size);
    // Wrap the shared memory block, we copy it.
    char *wrap(SrsSharedPtr<SrsMemoryBlock> block);
    // Copy the RTP packet.
    virtual SrsRtpPacket *copy();

public:
    // Parse the TWCC extension, ignore by default.
    void enable_twcc_decode() { header_.enable_twcc_decode(); } // SrsRtpPacket::enable_twcc_decode
    // Get and set the payload of packet.
    // @remark Note that return NULL if no payload.
    void set_payload(ISrsRtpPayloader *p, SrsRtpPacketPayloadType pt)
    {
        payload_ = p;
        payload_type_ = pt;
    }
    ISrsRtpPayloader *payload() { return payload_; }
    // Set the padding of RTP packet.
    void set_padding(int size);
    // Increase the padding of RTP packet.
    void add_padding(int size);
    // Set the decode handler.
    void set_decode_handler(ISrsRtpPacketDecodeHandler *h);
    // Whether the packet is Audio packet.
    bool is_audio();
    // Set RTP header extensions for encoding or decoding header extension
    void set_extension_types(SrsRtpExtensionTypes *v);
    // interface ISrsEncoder
public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual srs_error_t decode(SrsBuffer *buf);

public:
    bool is_keyframe(SrsVideoCodecId codec_id);
    // Get and set the packet sync time in milliseconds.
    void set_avsync_time(int64_t avsync_time) { avsync_time_ = avsync_time; }
    int64_t get_avsync_time() const { return avsync_time_; }
};

// Single payload data.
class SrsRtpRawPayload : public ISrsRtpPayloader
{
public:
    // The RAW payload, directly point to the shared memory.
    // @remark We only refer to the memory, user must free its bytes.
    char *payload_;
    int nn_payload_;

public:
    // Use the whole RAW RTP payload as a sample.
    SrsNaluSample *sample_;

public:
    SrsRtpRawPayload();
    virtual ~SrsRtpRawPayload();
    // interface ISrsRtpPayloader
public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual srs_error_t decode(SrsBuffer *buf);
    virtual ISrsRtpPayloader *copy();
};

// Multiple NALUs, automatically insert 001 between NALUs.
class SrsRtpRawNALUs : public ISrsRtpPayloader
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // We will manage the samples, but the sample itself point to the shared memory.
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
    // We will manage the returned samples, if user want to manage it, please copy it.
    srs_error_t read_samples(std::vector<SrsNaluSample *> &samples, int packet_size);
    // interface ISrsRtpPayloader
public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual srs_error_t decode(SrsBuffer *buf);
    virtual ISrsRtpPayloader *copy();
};

// STAP-A, for multiple NALUs.
class SrsRtpSTAPPayload : public ISrsRtpPayloader
{
public:
    // The NRI in NALU type.
    SrsAvcNaluType nri_;
    // The NALU samples, we will manage the samples.
    // @remark We only refer to the memory, user must free its bytes.
    std::vector<SrsNaluSample *> nalus_;

public:
    SrsRtpSTAPPayload();
    virtual ~SrsRtpSTAPPayload();

public:
    SrsNaluSample *get_sps();
    SrsNaluSample *get_pps();
    SrsNaluSample *get_idr();
    // interface ISrsRtpPayloader
public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual srs_error_t decode(SrsBuffer *buf);
    virtual ISrsRtpPayloader *copy();
};

// FU-A, for one NALU with multiple fragments.
// With more than one payload.
class SrsRtpFUAPayload : public ISrsRtpPayloader
{
public:
    // The NRI in NALU type.
    SrsAvcNaluType nri_;
    // The FUA header.
    bool start_;
    bool end_;
    SrsAvcNaluType nalu_type_;
    // The NALU samples, we manage the samples.
    // @remark We only refer to the memory, user must free its bytes.
    std::vector<SrsNaluSample *> nalus_;

public:
    SrsRtpFUAPayload();
    virtual ~SrsRtpFUAPayload();
    // interface ISrsRtpPayloader
public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual srs_error_t decode(SrsBuffer *buf);
    virtual ISrsRtpPayloader *copy();
};

// FU-A, for one NALU with multiple fragments.
// With only one payload.
class SrsRtpFUAPayload2 : public ISrsRtpPayloader
{
public:
    // The NRI in NALU type.
    SrsAvcNaluType nri_;
    // The FUA header.
    bool start_;
    bool end_;
    SrsAvcNaluType nalu_type_;
    // The payload and size,
    char *payload_;
    int size_;

public:
    SrsRtpFUAPayload2();
    virtual ~SrsRtpFUAPayload2();
    // interface ISrsRtpPayloader
public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual srs_error_t decode(SrsBuffer *buf);
    virtual ISrsRtpPayloader *copy();
};

class SrsRtpSTAPPayloadHevc : public ISrsRtpPayloader
{
public:
    // The NALU samples, we will manage the samples.
    // @remark We only refer to the memory, user must free its bytes.
    std::vector<SrsNaluSample *> nalus_;

public:
    SrsRtpSTAPPayloadHevc();
    virtual ~SrsRtpSTAPPayloadHevc();

public:
    SrsNaluSample *get_vps();
    SrsNaluSample *get_sps();
    SrsNaluSample *get_pps();
    SrsNaluSample *get_idr();
    // interface ISrsRtpPayloader
public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual srs_error_t decode(SrsBuffer *buf);
    virtual ISrsRtpPayloader *copy();
};

// FU, for one NALU with multiple fragments.
// With more than one payload for HEVC.
class SrsRtpFUAPayloadHevc : public ISrsRtpPayloader
{
public:
    // The FUA header.
    bool start_;
    bool end_;
    SrsHevcNaluType nalu_type_;
    // The NALU samples, we manage the samples.
    // @remark We only refer to the memory, user must free its bytes.
    std::vector<SrsNaluSample *> nalus_;

public:
    SrsRtpFUAPayloadHevc();
    virtual ~SrsRtpFUAPayloadHevc();
    // interface ISrsRtpPayloader
public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer *buf);
    virtual srs_error_t decode(SrsBuffer *buf);
    virtual ISrsRtpPayloader *copy();
};

// FU, for one NALU with multiple fragments.
// With only one payload for HEVC.
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
