/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 Lixin
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

#ifndef SRS_APP_RTC_RTP_JITBUFFER_HPP
#define SRS_APP_RTC_RTP_JITBUFFER_HPP

#include <srs_core.hpp>

#include <algorithm>
#include <string>
#include <vector>
#include <queue>
#include <map>
#include <list>
#include <set>

#include <srs_app_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_kernel_flv.hpp>

class SrsPsRtpPacket;
class SrsRtpFrameBuffer;
class SrsRtpDecodingState;
class SrsGb28181RtmpMuxer;
class VCMPacket;
class SrsRtpPacket2;

///jittbuffer

enum FrameType {
    kEmptyFrame            = 0,
    kAudioFrameSpeech      = 1,
    kAudioFrameCN          = 2,
    kVideoFrameKey         = 3,    // independent frame
    kVideoFrameDelta       = 4,    // depends on the previus frame
    kVideoFrameGolden      = 5,    // depends on a old known previus frame
    kVideoFrameAltRef      = 6
};

// Used to indicate which decode with errors mode should be used.
enum SrsRtpDecodeErrorMode {
    kNoErrors,                // Never decode with errors. Video will freeze
    // if nack is disabled.
    kSelectiveErrors,         // Frames that are determined decodable in
    // VCMSessionInfo may be decoded with missing
    // packets. As not all incomplete frames will be
    // decodable, video will freeze if nack is disabled.
    kWithErrors               // Release frames as needed. Errors may be
    // introduced as some encoded frames may not be
    // complete.
};

// Used to estimate rolling average of packets per frame.
static const float kFastConvergeMultiplier = 0.4f;
static const float kNormalConvergeMultiplier = 0.2f;

enum { kMaxNumberOfFrames     = 300 };
enum { kStartNumberOfFrames   = 6 };
enum { kMaxVideoDelayMs       = 10000 };
enum { kPacketsPerFrameMultiplier = 5 };
enum { kFastConvergeThreshold = 5};

enum SrsRtpJitterBufferEnum {
    kMaxConsecutiveOldFrames        = 60,
    kMaxConsecutiveOldPackets       = 300,
    kMaxPacketsInSession            = 800,
    kBufferIncStepSizeBytes         = 30000,   // >20 packets.
    kMaxJBFrameSizeBytes            = 4000000  // sanity don't go above 4Mbyte.
};

enum SrsRtpFrameBufferEnum {
    kOutOfBoundsPacket    = -7,
    kNotInitialized       = -6,
    kOldPacket            = -5,
    kGeneralError         = -4,
    kFlushIndicator       = -3,   // Indicator that a flush has occurred.
    kTimeStampError       = -2,
    kSizeError            = -1,
    kNoError              = 0,
    kIncomplete           = 1,    // Frame incomplete.
    kCompleteSession      = 3,    // at least one layer in the frame complete.
    kDecodableSession     = 4,    // Frame incomplete, but ready to be decoded
    kDuplicatePacket      = 5     // We're receiving a duplicate packet.
};

enum SrsRtpFrameBufferStateEnum {
    kStateEmpty,              // frame popped by the RTP receiver
    kStateIncomplete,         // frame that have one or more packet(s) stored
    kStateComplete,           // frame that have all packets
    kStateDecodable           // Hybrid mode - frame can be decoded
};

enum SrsRtpNackMode {
    kNack,
    kNoNack
};

// Used to indicate if a received packet contain a complete NALU (or equivalent)
enum VCMNaluCompleteness {
    kNaluUnset = 0,       // Packet has not been filled.
    kNaluComplete = 1,    // Packet can be decoded as is.
    kNaluStart,           // Packet contain beginning of NALU
    kNaluIncomplete,      // Packet is not beginning or end of NALU
    kNaluEnd,             // Packet is the end of a NALU
};

enum RtpVideoCodecTypes {
    kRtpVideoNone,
    kRtpVideoGeneric,
    kRtpVideoVp8,
    kRtpVideoVp9,
    kRtpVideoH264,
    kRtpVideoPS
};


// Video codec types
enum VideoCodecType {
    kVideoCodecVP8,
    kVideoCodecVP9,
    kVideoCodecH264,
    kVideoCodecH264SVC,
    kVideoCodecI420,
    kVideoCodecRED,
    kVideoCodecULPFEC,
    kVideoCodecGeneric,
    kVideoCodecH264PS,
    kVideoCodecUnknown
};

// The packetization types that we support: single, aggregated, and fragmented.
enum H264PacketizationTypes {
    kH264SingleNalu,  // This packet contains a single NAL unit.
    kH264StapA,       // This packet contains STAP-A (single time
    // aggregation) packets. If this packet has an
    // associated NAL unit type, it'll be for the
    // first such aggregated packet.
    kH264FuA,         // This packet contains a FU-A (fragmentation
    // unit) packet, meaning it is a part of a frame
    // that was too large to fit into a single packet.
};

enum { kH264StartCodeLengthBytes = 4};

// Used to pass data from jitter buffer to session info.
// This data is then used in determining whether a frame is decodable.
struct FrameData {
    int64_t rtt_ms;
    float rolling_average_packets_per_frame;
};

inline bool IsNewerSequenceNumber(uint16_t sequence_number,
                                  uint16_t prev_sequence_number)
{
    return sequence_number != prev_sequence_number &&
           static_cast<uint16_t>(sequence_number - prev_sequence_number) < 0x8000;
}

inline bool IsNewerTimestamp(uint32_t timestamp, uint32_t prev_timestamp)
{
    return timestamp != prev_timestamp &&
           static_cast<uint32_t>(timestamp - prev_timestamp) < 0x80000000;
}

inline uint16_t LatestSequenceNumber(uint16_t sequence_number1,
                                     uint16_t sequence_number2)
{
    return IsNewerSequenceNumber(sequence_number1, sequence_number2)
           ? sequence_number1
           : sequence_number2;
}

inline uint32_t LatestTimestamp(uint32_t timestamp1, uint32_t timestamp2)
{
    return IsNewerTimestamp(timestamp1, timestamp2) ? timestamp1 : timestamp2;
}

typedef std::list<SrsRtpFrameBuffer*> UnorderedFrameList;

class TimestampLessThan {
public:
    bool operator() (const uint32_t& timestamp1,
                     const uint32_t& timestamp2) const
    {
        return IsNewerTimestamp(timestamp2, timestamp1);
    }
};

class FrameList
    : public std::map<uint32_t, SrsRtpFrameBuffer*, TimestampLessThan> {
public:
    void InsertFrame(SrsRtpFrameBuffer* frame);
    SrsRtpFrameBuffer* PopFrame(uint32_t timestamp);
    SrsRtpFrameBuffer* Front() const;
    SrsRtpFrameBuffer* FrontNext() const;
    SrsRtpFrameBuffer* Back() const;
    int RecycleFramesUntilKeyFrame(FrameList::iterator* key_frame_it,
                                   UnorderedFrameList* free_frames);
    void CleanUpOldOrEmptyFrames(SrsRtpDecodingState* decoding_state, UnorderedFrameList* free_frames);
    void Reset(UnorderedFrameList* free_frames);
};


class VCMPacket {
public:
    VCMPacket();
    VCMPacket(const uint8_t* ptr,
              size_t size,
              uint16_t seqNum,
              uint32_t timestamp,
              bool markerBit,
              H264PacketizationTypes type,
              RtpVideoCodecTypes rtpType,
              bool singlenual,
              bool isfirst,
              FrameType ftype
              );

    void Reset();

    uint8_t           payloadType;
    uint32_t          timestamp;
    // NTP time of the capture time in local timebase in milliseconds.
    int64_t ntp_time_ms_;
    uint16_t          seqNum;
    const uint8_t*    dataPtr;
    size_t          sizeBytes;
    bool            markerBit;

    FrameType               frameType;
    VideoCodecType  codec;

    bool isFirstPacket;                 // Is this first packet in a frame.
    VCMNaluCompleteness completeNALU;   // Default is kNaluIncomplete.
    bool insertStartCode;               // True if a start code should be inserted before this
    // packet.
    int width;
    int height;
    //RTPVideoHeader codecSpecificHeader;
    

    //H264 header
    H264PacketizationTypes h264packetizationType;
    bool  h264singleNalu;
  
public:
    void CopyCodecSpecifics(RtpVideoCodecTypes codecType, bool H264single_nalu, bool firstPacket);
};

class SrsRtpFrameBuffer {
public:
    SrsRtpFrameBuffer();
    virtual ~SrsRtpFrameBuffer();

public:
    SrsRtpFrameBufferEnum InsertPacket(const VCMPacket& packet,  const FrameData& frame_data);
    void UpdateCompleteSession();
    void UpdateDecodableSession(const FrameData& frame_data);
    bool HaveFirstPacket() const;
    bool HaveLastPacket() const;
    void Reset();
   
    uint32_t GetTimeStamp() const;
    FrameType GetFrameType() const;
    SrsRtpFrameBufferStateEnum GetState() const;

    int32_t GetHighSeqNum() const;
    int32_t GetLowSeqNum() const;
    size_t Length() const;
    const uint8_t* Buffer() const;

    int NumPackets() const;   
    void InformOfEmptyPacket(uint16_t seq_num);
  
    bool complete() const;
    bool decodable() const;

    bool DeletePacket(int &count);
    void PrepareForDecode(bool continuous);
   
private:

    typedef std::list<VCMPacket> PacketList;
    typedef PacketList::iterator PacketIterator;
    typedef PacketList::const_iterator PacketIteratorConst;
    typedef PacketList::reverse_iterator ReversePacketIterator;

    bool InSequence(const PacketIterator& packet_it,
                                const PacketIterator& prev_packet_it);

    size_t InsertBuffer(uint8_t* frame_buffer, PacketIterator packet_it);
    size_t Insert(const uint8_t* buffer, size_t length, bool insert_start_code, uint8_t* frame_buffer);
    void ShiftSubsequentPackets(PacketIterator it,  int steps_to_shift);
    void VerifyAndAllocate(const uint32_t minimumSize);
    void UpdateDataPointers(const uint8_t* old_base_ptr, const uint8_t* new_base_ptr);
    size_t DeletePacketData(PacketIterator start, PacketIterator end);
    size_t MakeDecodable();


    PacketList packets_;
    int empty_seq_num_low_;
    int empty_seq_num_high_;

    int first_packet_seq_num_;
    int last_packet_seq_num_;

    bool complete_;
    bool decodable_;

    uint32_t timeStamp_;
    FrameType frame_type_;

    SrsRtpDecodeErrorMode decode_error_mode_;
    SrsRtpFrameBufferStateEnum state_;
   
    //uint16_t             nackCount_;
    //int64_t              latestPacketTimeMs_;

    // The payload.
    uint8_t* _buffer;
    size_t   _size;
    size_t _length;
};

class SrsRtpDecodingState {
public:
    SrsRtpDecodingState();
    ~SrsRtpDecodingState();
    // Check for old frame
    bool IsOldFrame(const SrsRtpFrameBuffer* frame) const;
    // Check for old packet
    bool IsOldPacket(const VCMPacket* packet);
    // Check for frame continuity based on current decoded state. Use best method
    // possible, i.e. temporal info, picture ID or sequence number.
    bool ContinuousFrame(const SrsRtpFrameBuffer* frame) const;
    void SetState(const SrsRtpFrameBuffer* frame);
    void CopyFrom(const SrsRtpDecodingState& state);
    bool UpdateEmptyFrame(const SrsRtpFrameBuffer* frame);
    // Update the sequence number if the timestamp matches current state and the
    // sequence number is higher than the current one. This accounts for packets
    // arriving late.
    void UpdateOldPacket(const VCMPacket* packet);
    void SetSeqNum(uint16_t new_seq_num);
    void Reset();
    uint32_t time_stamp() const;
    uint16_t sequence_num() const;
    // Return true if at initial state.
    bool in_initial_state() const;
    // Return true when sync is on - decode all layers.
    bool full_sync() const;

private:
    void UpdateSyncState(const SrsRtpFrameBuffer* frame);
    // Designated continuity functions
    //bool ContinuousPictureId(int picture_id) const;
    bool ContinuousSeqNum(uint16_t seq_num) const;
    //bool ContinuousLayer(int temporal_id, int tl0_pic_id) const;
    //bool UsingPictureId(const SrsRtpFrameBuffer* frame) const;

    // Keep state of last decoded frame.
    // TODO(mikhal/stefan): create designated classes to handle these types.
    uint16_t    sequence_num_;
    uint32_t    time_stamp_;
    bool        full_sync_;  // Sync flag when temporal layers are used.
    bool        in_initial_state_;

    bool        m_firstPacket;
};

// The time jitter correct for rtp.
class SrsRtpTimeJitter
{
private:
    int64_t previous_timestamp;
    int64_t pts;
    int delta;
public:
    SrsRtpTimeJitter();
    virtual ~SrsRtpTimeJitter();
public:
    int64_t timestamp();
    srs_error_t correct(int64_t& ts);
    void reset();
};

class SrsRtpJitterBuffer
{
public:
    SrsRtpJitterBuffer(std::string key);
    virtual ~SrsRtpJitterBuffer();

public:
    srs_error_t start();
    void Reset();
    SrsRtpFrameBufferEnum InsertPacket2(const SrsRtpPacket2 &pkt, bool* retransmitted);
    SrsRtpFrameBufferEnum InsertPacket(uint16_t seq, uint32_t ts, bool maker, char *buf, int size,
        bool* retransmitted);
    void ReleaseFrame(SrsRtpFrameBuffer* frame);
    bool FoundFrame(uint32_t& time_stamp);
    bool GetFrame(char **buffer, int &buf_len, int &size, bool &keyframe, const uint32_t time_stamp);
    void SetDecodeErrorMode(SrsRtpDecodeErrorMode error_mode);
    void SetNackMode(SrsRtpNackMode mode,int64_t low_rtt_nack_threshold_ms,
                                    int64_t high_rtt_nack_threshold_ms);
    void SetNackSettings(size_t max_nack_list_size,int max_packet_age_to_nack, 
                                    int max_incomplete_time_ms);
    uint16_t* GetNackList(uint16_t* nack_list_size, bool* request_key_frame);
    void Flush();
    void ResetJittter();

    bool isFirstKeyFrame;
private:

    SrsRtpFrameBufferEnum GetFrameByRtpPacket(const VCMPacket& packet, SrsRtpFrameBuffer** frame,
                              FrameList** frame_list);
    SrsRtpFrameBuffer* GetEmptyFrame();
    bool NextCompleteTimestamp(uint32_t max_wait_time_ms, uint32_t* timestamp);
    bool NextMaybeIncompleteTimestamp(uint32_t* timestamp);
    SrsRtpFrameBuffer* ExtractAndSetDecode(uint32_t timestamp);
    SrsRtpFrameBuffer* NextFrame() const;
  

    bool TryToIncreaseJitterBufferSize();
    bool RecycleFramesUntilKeyFrame();
    bool IsContinuous(const SrsRtpFrameBuffer& frame) const;
    bool IsContinuousInState(const SrsRtpFrameBuffer& frame,
        const SrsRtpDecodingState& decoding_state) const;
    void FindAndInsertContinuousFrames(const SrsRtpFrameBuffer& new_frame);
    void CleanUpOldOrEmptyFrames();

    //nack
    bool UpdateNackList(uint16_t sequence_number);
    bool TooLargeNackList() const;
    bool HandleTooLargeNackList();
    bool MissingTooOldPacket(uint16_t latest_sequence_number) const;
    bool HandleTooOldPackets(uint16_t latest_sequence_number);
    void DropPacketsFromNackList(uint16_t last_decoded_sequence_number);
    SrsRtpNackMode nack_mode() const;
    int NonContinuousOrIncompleteDuration();
    uint16_t EstimatedLowSequenceNumber(const SrsRtpFrameBuffer& frame) const;
    bool WaitForRetransmissions();

    bool IsPacketInOrder(uint16_t sequence_number);
    bool IsFirstPacketInFrame(uint32_t ts, uint16_t seq);
   
private:
    class SequenceNumberLessThan {
    public:
        bool operator() (const uint16_t& sequence_number1,
                         const uint16_t& sequence_number2) const
        {
            return IsNewerSequenceNumber(sequence_number2, sequence_number1);
        }
    };
    typedef std::set<uint16_t, SequenceNumberLessThan> SequenceNumberSet;
    
    std::string key_;

    srs_cond_t wait_cond_t;
    // If we are running (have started) or not.
    bool running_;
    // Number of allocated frames.
    int max_number_of_frames_;
    UnorderedFrameList free_frames_;
    FrameList decodable_frames_;
    FrameList incomplete_frames_;
    SrsRtpDecodingState last_decoded_state_;
    bool first_packet_since_reset_;

    // Statistics.
    //VCMReceiveStatisticsCallback* stats_callback_ GUARDED_BY(crit_sect_);
    // Frame counts for each type (key, delta, ...)
    //FrameCounts receive_statistics_;
    // Latest calculated frame rates of incoming stream.
    unsigned int incoming_frame_rate_;
    unsigned int incoming_frame_count_;
    int64_t time_last_incoming_frame_count_;
    unsigned int incoming_bit_count_;
    unsigned int incoming_bit_rate_;
    // Number of frames in a row that have been too old.
    int num_consecutive_old_frames_;
    // Number of packets in a row that have been too old.
    int num_consecutive_old_packets_;
    // Number of packets received.
    int num_packets_;
    int num_packets_free_;
    // Number of duplicated packets received.
    int num_duplicated_packets_;
    // Number of packets discarded by the jitter buffer.
    int num_discarded_packets_;
    // Time when first packet is received.
    int64_t time_first_packet_ms_;

    // Jitter estimation.
    // Filter for estimating jitter.
    //VCMJitterEstimator jitter_estimate_;
    // Calculates network delays used for jitter calculations.
    //VCMInterFrameDelay inter_frame_delay_;
    //VCMJitterSample waiting_for_completion_;
    int64_t rtt_ms_;

    // NACK and retransmissions.
    SrsRtpNackMode nack_mode_;
    int64_t low_rtt_nack_threshold_ms_;
    int64_t high_rtt_nack_threshold_ms_;
    // Holds the internal NACK list (the missing sequence numbers).
    SequenceNumberSet missing_sequence_numbers_;
    uint16_t latest_received_sequence_number_;
    std::vector<uint16_t> nack_seq_nums_;
    size_t max_nack_list_size_;
    int max_packet_age_to_nack_;  // Measured in sequence numbers.
    int max_incomplete_time_ms_;

    SrsRtpDecodeErrorMode decode_error_mode_;
    // Estimated rolling average of packets per frame
    float average_packets_per_frame_;
    // average_packets_per_frame converges fast if we have fewer than this many
    // frames.
    int frame_counter_;

    uint32_t last_received_timestamp_;
    uint16_t last_received_sequence_number_;
    bool first_packet_;
    
};

#endif

