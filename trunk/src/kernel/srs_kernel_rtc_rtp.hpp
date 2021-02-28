/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRS_KERNEL_RTC_RTP_HPP
#define SRS_KERNEL_RTC_RTP_HPP

#include <srs_core.hpp>

#include <srs_kernel_buffer.hpp>
#include <srs_kernel_codec.hpp>

#include <string>
#include <list>
#include <vector>

class SrsRtpPacket2;

// The RTP packet max size, should never exceed this size.
const int kRtpPacketSize        = 1500;

const int kRtpHeaderFixedSize   = 12;
const uint8_t kRtpMarker        = 0x80;

// H.264 nalu header type mask.
const uint8_t kNalTypeMask      = 0x1F;

// @see: https://tools.ietf.org/html/rfc6184#section-5.2
const uint8_t kStapA            = 24;

// @see: https://tools.ietf.org/html/rfc6184#section-5.2
const uint8_t kFuA              = 28;

// @see: https://tools.ietf.org/html/rfc6184#section-5.8
const uint8_t kStart            = 0x80; // Fu-header start bit
const uint8_t kEnd              = 0x40; // Fu-header end bit


class SrsBuffer;
class SrsRtpRawPayload;
class SrsRtpFUAPayload2;
class SrsSharedPtrMessage;
class SrsRtpExtensionTypes;

// Fast parse the SSRC from RTP packet. Return 0 if invalid.
uint32_t srs_rtp_fast_parse_ssrc(char* buf, int size);
uint8_t srs_rtp_fast_parse_pt(char* buf, int size);
srs_error_t srs_rtp_fast_parse_twcc(char* buf, int size, SrsRtpExtensionTypes* types, uint16_t& twcc_sn);

// The "distance" between two uint16 number, for example:
//      distance(prev_value=3, value=5) === (int16_t)(uint16_t)((uint16_t)3-(uint16_t)5) === -2
//      distance(prev_value=3, value=65534) === (int16_t)(uint16_t)((uint16_t)3-(uint16_t)65534) === 5
//      distance(prev_value=65532, value=65534) === (int16_t)(uint16_t)((uint16_t)65532-(uint16_t)65534) === -2
// For RTP sequence, it's only uint16 and may flip back, so 3 maybe 3+0xffff.
// @remark Note that srs_rtp_seq_distance(0, 32768)>0 is TRUE by https://mp.weixin.qq.com/s/JZTInmlB9FUWXBQw_7NYqg
//      but for WebRTC jitter buffer it's FALSE and we follow it.
// @remark For srs_rtp_seq_distance(32768, 0)>0, it's FALSE definitely.
inline int16_t srs_rtp_seq_distance(const uint16_t& prev_value, const uint16_t& value)
{
    return (int16_t)(value - prev_value);
}

// For map to compare the sequence of RTP.
struct SrsSeqCompareLess {
    bool operator()(const uint16_t& pre_value, const uint16_t& value) const {
        return srs_rtp_seq_distance(pre_value, value) > 0;
    }
};

bool srs_seq_is_newer(uint16_t value, uint16_t pre_value);
bool srs_seq_is_rollback(uint16_t value, uint16_t pre_value);
int32_t srs_seq_distance(uint16_t value, uint16_t pre_value);

enum SrsRtpExtensionType
{
    kRtpExtensionNone,
    kRtpExtensionTransportSequenceNumber,
    kRtpExtensionAudioLevel,
    kRtpExtensionNumberOfExtensions  // Must be the last entity in the enum.
};

const std::string kAudioLevelUri = "urn:ietf:params:rtp-hdrext:ssrc-audio-level";

struct SrsExtensionInfo
{
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
private:
    bool register_id(int id, SrsRtpExtensionType type, std::string uri);
private:
    uint8_t ids_[kRtpExtensionNumberOfExtensions];
};

// Note that the extensions should never extends from any class, for performance.
class SrsRtpExtensionTwcc// : public ISrsCodec
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
    virtual srs_error_t decode(SrsBuffer* buf);
    virtual srs_error_t encode(SrsBuffer* buf);
    virtual uint64_t nb_bytes();
};

// Note that the extensions should never extends from any class, for performance.
class SrsRtpExtensionOneByte// : public ISrsCodec
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
    virtual srs_error_t decode(SrsBuffer* buf);
    virtual srs_error_t encode(SrsBuffer* buf);
    virtual uint64_t nb_bytes() { return 2; };
};

// Note that the extensions should never extends from any class, for performance.
class SrsRtpExtensions// : public ISrsCodec
{
private:
    bool has_ext_;
private:
    // The extension types is used to decode the packet, which is reference to
    // the types in publish stream.
    SrsRtpExtensionTypes* types_;
private:
    SrsRtpExtensionTwcc twcc_;
    SrsRtpExtensionOneByte audio_level_;
public:
    SrsRtpExtensions();
    virtual ~SrsRtpExtensions();
public:
    inline bool exists() { return has_ext_; } // SrsRtpExtensions::exists
    void set_types_(SrsRtpExtensionTypes* types);
    srs_error_t get_twcc_sequence_number(uint16_t& twcc_sn);
    srs_error_t set_twcc_sequence_number(uint8_t id, uint16_t sn);
    srs_error_t get_audio_level(uint8_t& level);
    srs_error_t set_audio_level(int id, uint8_t level);
// ISrsCodec
public:
    virtual srs_error_t decode(SrsBuffer* buf);
private:
    srs_error_t decode_0xbede(SrsBuffer* buf);
public:
    virtual srs_error_t encode(SrsBuffer* buf);
    virtual uint64_t nb_bytes();
};

// Note that the header should never extends from any class, for performance.
class SrsRtpHeader// : public ISrsCodec
{
private:
    uint8_t padding_length;
    uint8_t cc;
    bool marker;
    uint8_t payload_type;
    uint16_t sequence;
    uint32_t timestamp;
    uint32_t ssrc;
    uint32_t csrc[15];
    SrsRtpExtensions extensions_;
    bool ignore_padding_;
public:
    SrsRtpHeader();
    virtual ~SrsRtpHeader();
public:
    virtual srs_error_t decode(SrsBuffer* buf);
private:
    srs_error_t parse_extensions(SrsBuffer* buf);
public:
    virtual srs_error_t encode(SrsBuffer* buf);
    virtual uint64_t nb_bytes();
public:
    void set_marker(bool v);
    bool get_marker() const;
    void set_payload_type(uint8_t v);
    uint8_t get_payload_type() const;
    void set_sequence(uint16_t v);
    uint16_t get_sequence() const;
    void set_timestamp(uint32_t v);
    uint32_t get_timestamp() const;
    void set_ssrc(uint32_t v);
    inline uint32_t get_ssrc() const { return ssrc; } // SrsRtpHeader::get_ssrc
    void set_padding(uint8_t v);
    uint8_t get_padding() const;
    void set_extensions(SrsRtpExtensionTypes* extmap);
    void ignore_padding(bool v);
    srs_error_t get_twcc_sequence_number(uint16_t& twcc_sn);
    srs_error_t set_twcc_sequence_number(uint8_t id, uint16_t sn);
};

// The common payload interface for RTP packet.
class ISrsRtpPayloader : public ISrsCodec
{
public:
    ISrsRtpPayloader();
    virtual ~ISrsRtpPayloader();
public:
    virtual ISrsRtpPayloader* copy() = 0;
};

// The payload type, for performance to avoid dynamic cast.
enum SrsRtpPacketPayloadType
{
    SrsRtpPacketPayloadTypeRaw,
    SrsRtpPacketPayloadTypeFUA2,
    SrsRtpPacketPayloadTypeFUA,
    SrsRtpPacketPayloadTypeNALU,
    SrsRtpPacketPayloadTypeSTAP,
    SrsRtpPacketPayloadTypeUnknown,
};

class ISrsRtpPacketDecodeHandler
{
public:
    ISrsRtpPacketDecodeHandler();
    virtual ~ISrsRtpPacketDecodeHandler();
public:
    // We don't know the actual payload, so we depends on external handler.
    virtual void on_before_decode_payload(SrsRtpPacket2* pkt, SrsBuffer* buf, ISrsRtpPayloader** ppayload, SrsRtpPacketPayloadType* ppt) = 0;
};

// The RTP packet with cached shared message.
class SrsRtpPacket2
{
// RTP packet fields.
public:
    SrsRtpHeader header;
private:
    ISrsRtpPayloader* payload_;
    SrsRtpPacketPayloadType payload_type_;
private:
    // The original shared message, all RTP packets can refer to its data.
    // Note that the size of shared msg, is not the packet size, it's a larger aligned buffer.
    SrsSharedPtrMessage* shared_buffer_;
    // The size of original packet.
    int actual_buffer_size_;
// Helper fields.
public:
    // The first byte as nalu type, for video decoder only.
    SrsAvcNaluType nalu_type;
    // The frame type, for RTMP bridger or SFU source.
    SrsFrameType frame_type;
// Fast cache for performance.
private:
    // The cached payload size for packet.
    int cached_payload_size;
    // The helper handler for decoder, use RAW payload if NULL.
    ISrsRtpPacketDecodeHandler* decode_handler;
public:
    SrsRtpPacket2();
    virtual ~SrsRtpPacket2();
public:
    // User MUST reset the packet if got from cache,
    // except copy(we will assign the header and copy payload).
    void reset();
private:
    void recycle_payload();
    void recycle_shared_buffer();
public:
    // Recycle the object to reuse it.
    virtual bool recycle();
    // Wrap buffer to shared_message, which is managed by us.
    char* wrap(int size);
    char* wrap(char* data, int size);
    // Wrap the shared message, we copy it.
    char* wrap(SrsSharedPtrMessage* msg);
    // Copy the RTP packet.
    virtual SrsRtpPacket2* copy();
public:
    // Get and set the payload of packet.
    void set_payload(ISrsRtpPayloader* p, SrsRtpPacketPayloadType pt) { payload_ = p; payload_type_ = pt; }
    ISrsRtpPayloader* payload() { return payload_; }
    // Set the padding of RTP packet.
    void set_padding(int size);
    // Increase the padding of RTP packet.
    void add_padding(int size);
    // Set the decode handler.
    void set_decode_handler(ISrsRtpPacketDecodeHandler* h);
    // Whether the packet is Audio packet.
    bool is_audio();
    // Set RTP header extensions for encoding or decoding header extension
    void set_extension_types(SrsRtpExtensionTypes* v);
// interface ISrsEncoder
public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer* buf);
    virtual srs_error_t decode(SrsBuffer* buf);
};

// For object cache manager to stat the object dropped.
#include <srs_kernel_kbps.hpp>
extern SrsPps* _srs_pps_objs_drop;

// The RTP packet or message cache manager.
template<typename T>
class SrsRtpObjectCacheManager
{
private:
    bool enabled_;
    std::vector<T*> cache_objs_;
    size_t capacity_;
    size_t object_size_;
public:
    // SrsRtpObjectCacheManager::SrsRtpObjectCacheManager
    SrsRtpObjectCacheManager(size_t size_of_object) {
        enabled_ = false;
        capacity_ = 0;
        object_size_ = size_of_object;
    }
    // SrsRtpObjectCacheManager::~SrsRtpObjectCacheManager
    virtual ~SrsRtpObjectCacheManager() {
        typedef typename std::vector<T*>::iterator iterator;
        for (iterator it = cache_objs_.begin(); it != cache_objs_.end(); ++it) {
            T* obj = *it;
            srs_freep(obj);
        }
    }
public:
    // Setup the object cache, shrink if capacity changed.
    // SrsRtpObjectCacheManager::setup
    void setup(bool v, uint64_t memory) {
        enabled_ = v;
        capacity_ = (size_t)(memory / object_size_);


        if (!enabled_) {
            capacity_ = 0;
        }

        // Shrink the cache.
        while (cache_objs_.size() > capacity_) {
            T* obj = cache_objs_.back();
            cache_objs_.pop_back();
            srs_freep(obj);
        }
    }
    // Get the status of object cache.
    // SrsRtpObjectCacheManager::enabled
    inline bool enabled() {
        return enabled_;
    }
    // SrsRtpObjectCacheManager::size
    int size() {
        return (int)cache_objs_.size();
    }
    // SrsRtpObjectCacheManager::capacity
    int capacity() {
        return (int)capacity_;
    }
    // Try to allocate from cache, create new object if no cache.
    // SrsRtpObjectCacheManager::allocate
    T* allocate() {
        if (!enabled_ || cache_objs_.empty()) {
            return new T();
        }

        T* obj = cache_objs_.back();
        cache_objs_.pop_back();

        return obj;
    }
    // Recycle the object to cache.
    // @remark User can directly free the packet.
    // SrsRtpObjectCacheManager::recycle
    void recycle(T* p) {
        // The p may be NULL, because srs_freep(NULL) is ok.
        if (!p) {
            return;
        }

        // If disabled, drop the object.
        if (!enabled_) {
            srs_freep(p);
            return;
        }

        // If recycle the object fail, drop the cached object.
        if (!p->recycle()) {
            srs_freep(p);
            return;
        }

        // If exceed the capacity, drop the object.
        if (cache_objs_.size() > capacity_) {
            ++_srs_pps_objs_drop->sugar;

            srs_freep(p);
            return;
        }

        // Recycle it.
        cache_objs_.push_back(p);
    }
};

// Single payload data.
class SrsRtpRawPayload : public ISrsRtpPayloader
{
public:
    // The RAW payload, directly point to the shared memory.
    // @remark We only refer to the memory, user must free its bytes.
    char* payload;
    int nn_payload;
public:
    SrsRtpRawPayload();
    virtual ~SrsRtpRawPayload();
public:
    bool recycle() { return true; }
// interface ISrsRtpPayloader
public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer* buf);
    virtual srs_error_t decode(SrsBuffer* buf);
    virtual ISrsRtpPayloader* copy();
};

// Multiple NALUs, automatically insert 001 between NALUs.
class SrsRtpRawNALUs : public ISrsRtpPayloader
{
private:
    // We will manage the samples, but the sample itself point to the shared memory.
    std::vector<SrsSample*> nalus;
    int nn_bytes;
    int cursor;
public:
    SrsRtpRawNALUs();
    virtual ~SrsRtpRawNALUs();
public:
    void push_back(SrsSample* sample);
public:
    uint8_t skip_first_byte();
    // We will manage the returned samples, if user want to manage it, please copy it.
    srs_error_t read_samples(std::vector<SrsSample*>& samples, int packet_size);
// interface ISrsRtpPayloader
public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer* buf);
    virtual srs_error_t decode(SrsBuffer* buf);
    virtual ISrsRtpPayloader* copy();
};

// STAP-A, for multiple NALUs.
class SrsRtpSTAPPayload : public ISrsRtpPayloader
{
public:
    // The NRI in NALU type.
    SrsAvcNaluType nri;
    // The NALU samples, we will manage the samples.
    // @remark We only refer to the memory, user must free its bytes.
    std::vector<SrsSample*> nalus;
public:
    SrsRtpSTAPPayload();
    virtual ~SrsRtpSTAPPayload();
public:
    SrsSample* get_sps();
    SrsSample* get_pps();
// interface ISrsRtpPayloader
public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer* buf);
    virtual srs_error_t decode(SrsBuffer* buf);
    virtual ISrsRtpPayloader* copy();
};

// FU-A, for one NALU with multiple fragments.
// With more than one payload.
class SrsRtpFUAPayload : public ISrsRtpPayloader
{
public:
    // The NRI in NALU type.
    SrsAvcNaluType nri;
    // The FUA header.
    bool start;
    bool end;
    SrsAvcNaluType nalu_type;
    // The NALU samples, we manage the samples.
    // @remark We only refer to the memory, user must free its bytes.
    std::vector<SrsSample*> nalus;
public:
    SrsRtpFUAPayload();
    virtual ~SrsRtpFUAPayload();
// interface ISrsRtpPayloader
public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer* buf);
    virtual srs_error_t decode(SrsBuffer* buf);
    virtual ISrsRtpPayloader* copy();
};

// FU-A, for one NALU with multiple fragments.
// With only one payload.
class SrsRtpFUAPayload2 : public ISrsRtpPayloader
{
public:
    // The NRI in NALU type.
    SrsAvcNaluType nri;
    // The FUA header.
    bool start;
    bool end;
    SrsAvcNaluType nalu_type;
    // The payload and size,
    char* payload;
    int size;
public:
    SrsRtpFUAPayload2();
    virtual ~SrsRtpFUAPayload2();
public:
    bool recycle() { return true; }
// interface ISrsRtpPayloader
public:
    virtual uint64_t nb_bytes();
    virtual srs_error_t encode(SrsBuffer* buf);
    virtual srs_error_t decode(SrsBuffer* buf);
    virtual ISrsRtpPayloader* copy();
};

// For RTP packets cache.
extern SrsRtpObjectCacheManager<SrsRtpPacket2>* _srs_rtp_cache;
extern SrsRtpObjectCacheManager<SrsRtpRawPayload>* _srs_rtp_raw_cache;
extern SrsRtpObjectCacheManager<SrsRtpFUAPayload2>* _srs_rtp_fua_cache;

// For shared message cache, with payload.
extern SrsRtpObjectCacheManager<SrsSharedPtrMessage>* _srs_rtp_msg_cache_buffers;
// For shared message cache, without payload.
// Note that user must unwrap the shared message, before recycle it.
extern SrsRtpObjectCacheManager<SrsSharedPtrMessage>* _srs_rtp_msg_cache_objs;

#endif
