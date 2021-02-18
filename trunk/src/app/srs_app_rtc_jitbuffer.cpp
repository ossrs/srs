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

#include <srs_app_rtc_jitbuffer.hpp>

#include <srs_kernel_utility.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_st.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_file.hpp>


using namespace std;

// Use this rtt if no value has been reported.
static const int64_t kDefaultRtt = 200;

// Request a keyframe if no continuous frame has been received for this
// number of milliseconds and NACKs are disabled.
//static const int64_t kMaxDiscontinuousFramesTime = 1000;

typedef std::pair<uint32_t, SrsRtpFrameBuffer*> FrameListPair;

bool IsKeyFrame(FrameListPair pair)
{
    return pair.second->GetFrameType() == kVideoFrameKey;
}

bool HasNonEmptyState(FrameListPair pair)
{
    return pair.second->GetState() != kStateEmpty;
}

void FrameList::InsertFrame(SrsRtpFrameBuffer* frame)
{
    insert(rbegin().base(), FrameListPair(frame->GetTimeStamp(), frame));
}

SrsRtpFrameBuffer* FrameList::PopFrame(uint32_t timestamp)
{
    FrameList::iterator it = find(timestamp);

    if (it == end()) {
        return NULL;
    }

    SrsRtpFrameBuffer* frame = it->second;
    erase(it);
    return frame;
}

SrsRtpFrameBuffer* FrameList::Front() const
{
    return begin()->second;
}

SrsRtpFrameBuffer* FrameList::FrontNext() const
{
    FrameList::const_iterator it = begin();
    it++;

    if (it != end())
    {
        return it->second;
    } 

    return NULL;
}


SrsRtpFrameBuffer* FrameList::Back() const
{
    return rbegin()->second;
}

int FrameList::RecycleFramesUntilKeyFrame(FrameList::iterator* key_frame_it,
        UnorderedFrameList* free_frames)
{
    int drop_count = 0;
    FrameList::iterator it = begin();

    while (!empty()) {
        // Throw at least one frame.
        it->second->Reset();
        free_frames->push_back(it->second);
        erase(it++);
        ++drop_count;

        if (it != end() && it->second->GetFrameType() == kVideoFrameKey) {
            *key_frame_it = it;
            return drop_count;
        }
    }

    *key_frame_it = end();
    return drop_count;
}

void FrameList::CleanUpOldOrEmptyFrames(SrsRtpDecodingState* decoding_state, UnorderedFrameList* free_frames)
{
    while (!empty()) {
        SrsRtpFrameBuffer* oldest_frame = Front();
        bool remove_frame = false;

        if (oldest_frame->GetState() == kStateEmpty && size() > 1) {
            // This frame is empty, try to update the last decoded state and drop it
            // if successful.
            remove_frame = decoding_state->UpdateEmptyFrame(oldest_frame);
        } else {
            remove_frame = decoding_state->IsOldFrame(oldest_frame);
        }

        if (!remove_frame) {
            break;
        }

        free_frames->push_back(oldest_frame);
        erase(begin());
    }
}

void FrameList::Reset(UnorderedFrameList* free_frames)
{
    while (!empty()) {
        begin()->second->Reset();
        free_frames->push_back(begin()->second);
        erase(begin());
    }
}


VCMPacket::VCMPacket()
    :
    payloadType(0),
    timestamp(0),
    ntp_time_ms_(0),
    seqNum(0),
    dataPtr(NULL),
    sizeBytes(0),
    markerBit(false),
    frameType(kEmptyFrame),
    codec(kVideoCodecUnknown),
    isFirstPacket(false),
    completeNALU(kNaluUnset),
    insertStartCode(false),
    width(0),
    height(0)
    //codecSpecificHeader()
{
}


VCMPacket::VCMPacket(const uint8_t* ptr,
                     size_t size,
                     uint16_t seq,
                     uint32_t ts,
                     bool mBit,
                     H264PacketizationTypes type,
                     RtpVideoCodecTypes rtpType,
                     bool singlenual,
                     bool isfirst,
                     FrameType ftype) :
    payloadType(0),
    timestamp(ts),
    ntp_time_ms_(0),
    seqNum(seq),
    dataPtr(ptr),
    sizeBytes(size),
    markerBit(mBit),
    frameType(ftype),
    codec(kVideoCodecUnknown),
    isFirstPacket(false),
    completeNALU(kNaluComplete),
    insertStartCode(false),
    width(0),
    height(0),
    h264packetizationType(type)
    //codecSpecificHeader()
{
    CopyCodecSpecifics(rtpType, singlenual, isfirst);
}

void VCMPacket::Reset()
{
    payloadType = 0;
    timestamp = 0;
    ntp_time_ms_ = 0;
    seqNum = 0;
    dataPtr = NULL;
    sizeBytes = 0;
    markerBit = false;
    frameType = kEmptyFrame;
    codec = kVideoCodecUnknown;
    isFirstPacket = false;
    completeNALU = kNaluUnset;
    insertStartCode = false;
    width = 0;
    height = 0;
    //memset(&codecSpecificHeader, 0, sizeof(RTPVideoHeader));
}

void VCMPacket::CopyCodecSpecifics(RtpVideoCodecTypes codecType, bool H264single_nalu, bool firstPacket)
{
    switch (codecType) {
    case kRtpVideoVp8:

        // Handle all packets within a frame as depending on the previous packet
        // TODO(holmer): This should be changed to make fragments independent
        // when the VP8 RTP receiver supports fragments.
        if (isFirstPacket && markerBit) {
            completeNALU = kNaluComplete;
        } else if (isFirstPacket) {
            completeNALU = kNaluStart;
        } else if (markerBit) {
            completeNALU = kNaluEnd;
        } else {
            completeNALU = kNaluIncomplete;
        }

        codec = kVideoCodecVP8;
        return;

    case kRtpVideoH264:
        isFirstPacket = firstPacket;

        if (isFirstPacket) {
            insertStartCode = true;
        }

        if (H264single_nalu) {
            completeNALU = kNaluComplete;
        } else if (isFirstPacket) {
            completeNALU = kNaluStart;
        } else if (markerBit) {
            completeNALU = kNaluEnd;
        } else {
            completeNALU = kNaluIncomplete;
        }

        codec = kVideoCodecH264;
        return;
    case kRtpVideoPS:
        isFirstPacket = firstPacket;
        codec = kVideoCodecH264PS;
        return;

    case kRtpVideoVp9:
    case kRtpVideoGeneric:
    case kRtpVideoNone:
        codec = kVideoCodecUnknown;
        return;
    }
}

uint16_t BufferToUWord16(const uint8_t* dataBuffer)
{
    return (dataBuffer[0] << 8) | dataBuffer[1];
}

SrsRtpFrameBuffer::SrsRtpFrameBuffer()
{
    empty_seq_num_low_ = 0;
    empty_seq_num_high_ = 0;
    first_packet_seq_num_ = 0;
    last_packet_seq_num_ = 0;
    complete_ = false;
    decodable_ = false;
    timeStamp_ = 0;
    frame_type_ = kEmptyFrame;
    decode_error_mode_ = kNoErrors;
    _length = 0;
    _size = 0;
    _buffer = NULL;
}

SrsRtpFrameBuffer::~SrsRtpFrameBuffer()
{
    srs_freepa(_buffer);
}

void SrsRtpFrameBuffer::Reset()
{
    //session_nack_ = false;
    complete_ = false;
    decodable_ = false;
    frame_type_ = kVideoFrameDelta;
    packets_.clear();
    empty_seq_num_low_ = -1;
    empty_seq_num_high_ = -1;
    first_packet_seq_num_ = -1;
    last_packet_seq_num_ = -1;
    _length = 0;
}

size_t SrsRtpFrameBuffer::Length() const
{
    return _length;
}

SrsRtpFrameBufferEnum SrsRtpFrameBuffer::InsertPacket(const VCMPacket& packet, const FrameData& frame_data)
{
    if (packets_.size() == kMaxPacketsInSession) {
        srs_warn("RTP: jitbuffer Max number of packets per frame has been reached.");
        return kSizeError;
    }

    if (packets_.size() == 0){
        timeStamp_ = packet.timestamp;
    }

    uint32_t requiredSizeBytes = Length() + packet.sizeBytes;

    if (requiredSizeBytes == 0){
        srs_warn("RTP: jitbuffer requiredSizeBytes is zero ts:%u, seq:%u", packet.timestamp, packet.seqNum);
        return kSizeError; 
    }

    if (requiredSizeBytes >= _size) {
        const uint8_t* prevBuffer = _buffer;
        const uint32_t increments = requiredSizeBytes /
                                    kBufferIncStepSizeBytes +
                                    (requiredSizeBytes %
                                     kBufferIncStepSizeBytes > 0);
        const uint32_t newSize = _size +
                                 increments * kBufferIncStepSizeBytes;

        if (newSize > kMaxJBFrameSizeBytes) {
            srs_error("RTP: jitbuffer Failed to insert packet due to frame being too big.");
            return kSizeError;
        }

        VerifyAndAllocate(newSize);
        UpdateDataPointers(prevBuffer, _buffer);

        srs_trace("RTP: jitbuffer VerifyAndAllocate:newSize:%d, prevBuffer:%d, _buffer:%d", newSize, prevBuffer, _buffer);
    }

    // Find the position of this packet in the packet list in sequence number
    // order and insert it. Loop over the list in reverse order.
    ReversePacketIterator rit = packets_.rbegin();

    for (; rit != packets_.rend(); ++rit){
        if (LatestSequenceNumber(packet.seqNum, (*rit).seqNum) == packet.seqNum) {
            break;
        }
    }

    // Check for duplicate packets.
    if (rit != packets_.rend() &&
            (*rit).seqNum == packet.seqNum && (*rit).sizeBytes > 0) {
        return kDuplicatePacket;
    }

    // if ((packets_.size() == 0)&&
    //     (first_packet_seq_num_ == -1 ||
    //     IsNewerSequenceNumber(first_packet_seq_num_, packet.seqNum))) {
    //     first_packet_seq_num_ = packet.seqNum;
    // }
    
    // //TODO: not check marker, check a complete frame with timestamp
    // if (packet.markerBit &&
    //     (last_packet_seq_num_ == -1 ||
    //     IsNewerSequenceNumber(packet.seqNum, last_packet_seq_num_))) {
    //     last_packet_seq_num_ = packet.seqNum;
    // }

    if (packet.codec == kVideoCodecH264) {
        //   frame_type_ = packet.frameType;
        if (packet.isFirstPacket &&
                (first_packet_seq_num_ == -1 ||
                 IsNewerSequenceNumber(first_packet_seq_num_, packet.seqNum))) {
            first_packet_seq_num_ = packet.seqNum;
            frame_type_ = packet.frameType;
        }

        if (packet.markerBit &&
                (last_packet_seq_num_ == -1 ||
                 IsNewerSequenceNumber(packet.seqNum, last_packet_seq_num_))) {
            last_packet_seq_num_ = packet.seqNum;
        }
    } else if (packet.codec == kVideoCodecH264PS) {
        if (packet.isFirstPacket &&
                (first_packet_seq_num_ == -1 ||
                IsNewerSequenceNumber(first_packet_seq_num_, packet.seqNum))) {
            first_packet_seq_num_ = packet.seqNum;
            frame_type_ = packet.frameType;
        }
    
        //TODO: not check marker, check a complete frame with timestamp
        if (packet.markerBit &&
                (last_packet_seq_num_ == -1 ||
                IsNewerSequenceNumber(packet.seqNum, last_packet_seq_num_))) {
            last_packet_seq_num_ = packet.seqNum;
        }
    }else {
        srs_warn("RTP: jitbuffer no support codec type");
        return kFlushIndicator;
    }

    // The insert operation invalidates the iterator |rit|.
    PacketIterator packet_list_it = packets_.insert(rit.base(), packet);

    //size_t returnLength = (*packet_list_it).sizeBytes;
    size_t returnLength = InsertBuffer(_buffer, packet_list_it);
  
    // update length
    _length = Length() + static_cast<uint32_t>(returnLength);
    UpdateCompleteSession();
    
    if (decode_error_mode_ == kWithErrors) {
        decodable_ = true;
    } else if (decode_error_mode_ == kSelectiveErrors) {
        UpdateDecodableSession(frame_data);
    }

    if (complete()) {
        state_ = kStateComplete;
        return kCompleteSession;
    } else if (decodable()) {
        state_  = kStateDecodable;
        return kDecodableSession;
    } else if (!complete()) {
        state_ = kStateIncomplete;
        return kIncomplete;
    }

    return kIncomplete;
}

void SrsRtpFrameBuffer::VerifyAndAllocate(const uint32_t minimumSize)
{
    if (minimumSize > _size) {
        // create buffer of sufficient size
        uint8_t* newBuffer = new uint8_t[minimumSize];

        if (_buffer) {
            // copy old data
            memcpy(newBuffer, _buffer, _size);
            delete [] _buffer;
        }

        srs_info("RTP: jitbuffer VerifyAndAllocate oldbuffer=%d newbuffer=%d, minimumSize=%d, size=%d", 
                     _buffer, newBuffer, minimumSize, _size);

        _buffer = newBuffer;
        _size = minimumSize;
    }
}

void SrsRtpFrameBuffer::UpdateDataPointers(const uint8_t* old_base_ptr,
                                        const uint8_t* new_base_ptr)
{
    for (PacketIterator it = packets_.begin(); it != packets_.end(); ++it)
        if ((*it).dataPtr != NULL) {
            //assert(old_base_ptr != NULL && new_base_ptr != NULL);
            (*it).dataPtr = new_base_ptr + ((*it).dataPtr - old_base_ptr);
        }
}


size_t SrsRtpFrameBuffer::InsertBuffer(uint8_t* frame_buffer,
                                    PacketIterator packet_it)
{
    VCMPacket& packet = *packet_it;
    PacketIterator it;

    // Calculate the offset into the frame buffer for this packet.
    size_t offset = 0;

    for (it = packets_.begin(); it != packet_it; ++it) {
        offset += (*it).sizeBytes;
    }

    // Set the data pointer to pointing to the start of this packet in the
    // frame buffer.
    const uint8_t* packet_buffer = packet.dataPtr;
    packet.dataPtr = frame_buffer + offset;

    // We handle H.264 STAP-A packets in a special way as we need to remove the
    // two length bytes between each NAL unit, and potentially add start codes.
    const size_t kH264NALHeaderLengthInBytes = 1;
    const size_t kLengthFieldLength = 2;

    if (packet.codec == kVideoCodecH264 && 
            packet.h264packetizationType == kH264StapA
            /*packet.codecSpecificHeader.codecHeader.H264.stap_a*/) {

        size_t required_length = 0;
        size_t nalu_count = 0;
        const uint8_t* nalu_ptr = packet_buffer + kH264NALHeaderLengthInBytes;

        while (nalu_ptr < packet_buffer + packet.sizeBytes) {
            size_t length = BufferToUWord16(nalu_ptr);
            required_length += length;
            nalu_ptr += kLengthFieldLength + length;
            nalu_count++;

            srs_info("RTP: jitbuffer H264 stap_a length:%d, nalu_count=%d", length, nalu_count);
        }

        size_t total_len = required_length + nalu_count * (kLengthFieldLength) + kH264NALHeaderLengthInBytes;

        if (total_len > packet.sizeBytes) {
            srs_error("RTP: jitbuffer H264 required len %d is biger than packet len %d", total_len, packet.sizeBytes);
            return -1;
        }

        required_length += nalu_count * (packet.insertStartCode ? kH264StartCodeLengthBytes : 0);

        ShiftSubsequentPackets(packet_it, required_length);
        nalu_ptr = packet_buffer + kH264NALHeaderLengthInBytes;
        uint8_t* frame_buffer_ptr = frame_buffer + offset;
        required_length = 0;

        while (nalu_ptr < packet_buffer + packet.sizeBytes) {
            size_t length = BufferToUWord16(nalu_ptr);
            nalu_ptr += kLengthFieldLength;
            size_t naluLen = Insert(nalu_ptr,
                                    length,
                                    packet.insertStartCode,
                                    const_cast<uint8_t*>(frame_buffer_ptr));
            frame_buffer_ptr += naluLen;
            nalu_ptr += length;
            required_length += naluLen;
        }

        packet.sizeBytes = required_length;
        return packet.sizeBytes;
    }

    ShiftSubsequentPackets(
        packet_it,
        packet.sizeBytes +
        (packet.insertStartCode ? kH264StartCodeLengthBytes : 0));

    packet.sizeBytes = Insert(packet_buffer,
                              packet.sizeBytes,
                              packet.insertStartCode,
                              const_cast<uint8_t*>(packet.dataPtr));
    return packet.sizeBytes;
}



size_t SrsRtpFrameBuffer::Insert(const uint8_t* buffer,
                              size_t length,
                              bool insert_start_code,
                              uint8_t* frame_buffer)
{
    if (insert_start_code) {
        const unsigned char startCode[] = {0, 0, 0, 1};
        memcpy(frame_buffer, startCode, kH264StartCodeLengthBytes);
    }

    memcpy(frame_buffer + (insert_start_code ? kH264StartCodeLengthBytes : 0),
           buffer,
           length);
    length += (insert_start_code ? kH264StartCodeLengthBytes : 0);

    return length;
}


void SrsRtpFrameBuffer::ShiftSubsequentPackets(PacketIterator it,
        int steps_to_shift)
{
    ++it;

    if (it == packets_.end()) {
        return;
    }

    uint8_t* first_packet_ptr = const_cast<uint8_t*>((*it).dataPtr);
    int shift_length = 0;

    // Calculate the total move length and move the data pointers in advance.
    for (; it != packets_.end(); ++it) {
        shift_length += (*it).sizeBytes;

        if ((*it).dataPtr != NULL) {
            (*it).dataPtr += steps_to_shift;
        }
    }

    memmove(first_packet_ptr + steps_to_shift, first_packet_ptr, shift_length);
}

void SrsRtpFrameBuffer::UpdateCompleteSession()
{
    if (HaveFirstPacket() && HaveLastPacket()) {
        // Do we have all the packets in this session?
        bool complete_session = true;
        PacketIterator it = packets_.begin();
        PacketIterator prev_it = it;
        ++it;

        for (; it != packets_.end(); ++it) {
            if (!InSequence(it, prev_it)) {
                complete_session = false;
                break;
            }

            prev_it = it;
        }

        complete_ = complete_session;
    }
}

bool SrsRtpFrameBuffer::HaveFirstPacket() const
{
    return !packets_.empty() && (first_packet_seq_num_ != -1);
}

bool SrsRtpFrameBuffer::HaveLastPacket() const
{
    return !packets_.empty() && (last_packet_seq_num_ != -1);
}

bool SrsRtpFrameBuffer::InSequence(const PacketIterator& packet_it,
                                const PacketIterator& prev_packet_it)
{
    // If the two iterators are pointing to the same packet they are considered
    // to be in sequence.
    return (packet_it == prev_packet_it ||
            (static_cast<uint16_t>((*prev_packet_it).seqNum + 1) ==
             (*packet_it).seqNum));
}

void SrsRtpFrameBuffer::UpdateDecodableSession(const FrameData& frame_data)
{
    // Irrelevant if session is already complete or decodable
    if (complete_ || decodable_) {
        return;
    }

    // TODO(agalusza): Account for bursty loss.
    // TODO(agalusza): Refine these values to better approximate optimal ones.
    // Do not decode frames if the RTT is lower than this.
    const int64_t kRttThreshold = 100;
    // Do not decode frames if the number of packets is between these two
    // thresholds.
    const float kLowPacketPercentageThreshold = 0.2f;
    const float kHighPacketPercentageThreshold = 0.8f;

    if (frame_data.rtt_ms < kRttThreshold
            || !HaveFirstPacket()
            || (NumPackets() <= kHighPacketPercentageThreshold
                * frame_data.rolling_average_packets_per_frame
                && NumPackets() > kLowPacketPercentageThreshold
                * frame_data.rolling_average_packets_per_frame)) {
        return;
    }

    decodable_ = true;
}

bool SrsRtpFrameBuffer::complete() const
{
    return complete_;
}

bool SrsRtpFrameBuffer::decodable() const
{
    return decodable_;
}

int SrsRtpFrameBuffer::NumPackets() const
{
    return packets_.size();
}

uint32_t SrsRtpFrameBuffer::GetTimeStamp() const
{
    return timeStamp_;
}

FrameType SrsRtpFrameBuffer::GetFrameType() const
{
    return frame_type_;
}

SrsRtpFrameBufferStateEnum SrsRtpFrameBuffer::GetState() const
{
    return state_;
}

int32_t SrsRtpFrameBuffer::GetHighSeqNum() const
{
    if (packets_.empty()) {
        return empty_seq_num_high_;
    }

    if (empty_seq_num_high_ == -1) {
        return packets_.back().seqNum;
    }

    return LatestSequenceNumber(packets_.back().seqNum, empty_seq_num_high_);

}

int32_t SrsRtpFrameBuffer::GetLowSeqNum() const
{
    if (packets_.empty()) {
        return empty_seq_num_low_;
    }

    return packets_.front().seqNum;
}

const uint8_t* SrsRtpFrameBuffer::Buffer() const
{
    return _buffer;
}


void SrsRtpFrameBuffer::InformOfEmptyPacket(uint16_t seq_num)
{
    // Empty packets may be FEC or filler packets. They are sequential and
    // follow the data packets, therefore, we should only keep track of the high
    // and low sequence numbers and may assume that the packets in between are
    // empty packets belonging to the same frame (timestamp).
    if (empty_seq_num_high_ == -1) {
        empty_seq_num_high_ = seq_num;
    } else {
        empty_seq_num_high_ = LatestSequenceNumber(seq_num, empty_seq_num_high_);
    }

    if (empty_seq_num_low_ == -1 || IsNewerSequenceNumber(empty_seq_num_low_,
            seq_num)) {
        empty_seq_num_low_ = seq_num;
    }
}


size_t SrsRtpFrameBuffer::DeletePacketData(PacketIterator start, PacketIterator end)
{
    size_t bytes_to_delete = 0;  // The number of bytes to delete.
    PacketIterator packet_after_end = end;
    //++packet_after_end;

    // Get the number of bytes to delete.
    // Clear the size of these packets.
    for (PacketIterator it = start; it != packet_after_end; ++it) {
        bytes_to_delete += (*it).sizeBytes;
        (*it).sizeBytes = 0;
        (*it).dataPtr = NULL;
    }

    if (bytes_to_delete > 0) {
        ShiftSubsequentPackets(end, -static_cast<int>(bytes_to_delete));
    }
    
    return bytes_to_delete;
}

size_t SrsRtpFrameBuffer::MakeDecodable()
{
    size_t return_length = 0;

    if (packets_.empty()) {
        return 0;
    }

    PacketIterator begin = packets_.begin();
    PacketIterator end = packets_.end();
    return_length += DeletePacketData(begin, end);

    return return_length;
}

void SrsRtpFrameBuffer::PrepareForDecode(bool continuous)
{

    size_t bytes_removed = MakeDecodable();
    _length -= bytes_removed;

    // Transfer frame information to EncodedFrame and create any codec
    // specific information.
    //_frameType = ConvertFrameType(_sessionInfo.FrameType());
    //_completeFrame = _sessionInfo.complete();
    //_missingFrame = !continuous;
}



 bool SrsRtpFrameBuffer::DeletePacket(int &count)
 {
    return true;
 }


/////////////////////////////////////////////////////////////////////////////

SrsRtpDecodingState::SrsRtpDecodingState()
    : sequence_num_(0),
      time_stamp_(0),
      //picture_id_(kNoPictureId),
      //temporal_id_(kNoTemporalIdx),
      //tl0_pic_id_(kNoTl0PicIdx),
      full_sync_(true),
      in_initial_state_(true),
      m_firstPacket(false) {}

SrsRtpDecodingState::~SrsRtpDecodingState() {}

void SrsRtpDecodingState::Reset()
{
    // TODO(mikhal): Verify - not always would want to reset the sync
    sequence_num_ = 0;
    time_stamp_ = 0;
    //picture_id_ = kNoPictureId;
    //temporal_id_ = kNoTemporalIdx;
    //tl0_pic_id_ = kNoTl0PicIdx;
    full_sync_ = true;
    in_initial_state_ = true;
}

uint32_t SrsRtpDecodingState::time_stamp() const
{
    return time_stamp_;
}

uint16_t SrsRtpDecodingState::sequence_num() const
{
    return sequence_num_;
}

bool SrsRtpDecodingState::IsOldFrame(const SrsRtpFrameBuffer* frame) const
{
    //assert(frame != NULL);
    if (frame == NULL) {
        return false;
    }

    if (in_initial_state_) {
        return false;
    }

    return !IsNewerTimestamp(frame->GetTimeStamp(), time_stamp_);
}

bool SrsRtpDecodingState::IsOldPacket(const VCMPacket* packet)
{
    //assert(packet != NULL);
    if (packet == NULL) {
        return false;
    }

    if (in_initial_state_) {
        return false;
    }

    if (!m_firstPacket) {
        m_firstPacket = true;
        time_stamp_ = packet->timestamp - 1;
        return false;
    }

    return !IsNewerTimestamp(packet->timestamp, time_stamp_);
}

void SrsRtpDecodingState::SetState(const SrsRtpFrameBuffer* frame)
{
    //assert(frame != NULL && frame->GetHighSeqNum() >= 0);
    UpdateSyncState(frame);
    sequence_num_ = static_cast<uint16_t>(frame->GetHighSeqNum());
    time_stamp_ = frame->GetTimeStamp();
    in_initial_state_ = false;
}

void SrsRtpDecodingState::CopyFrom(const SrsRtpDecodingState& state)
{
    sequence_num_ = state.sequence_num_;
    time_stamp_ = state.time_stamp_;
    full_sync_ = state.full_sync_;
    in_initial_state_ = state.in_initial_state_;
}

bool SrsRtpDecodingState::UpdateEmptyFrame(const SrsRtpFrameBuffer* frame)
{
    bool empty_packet = frame->GetHighSeqNum() == frame->GetLowSeqNum();

    if (in_initial_state_ && empty_packet) {
        // Drop empty packets as long as we are in the initial state.
        return true;
    }

    if ((empty_packet && ContinuousSeqNum(frame->GetHighSeqNum())) ||
            ContinuousFrame(frame)) {
        // Continuous empty packets or continuous frames can be dropped if we
        // advance the sequence number.
        sequence_num_ = frame->GetHighSeqNum();
        time_stamp_ = frame->GetTimeStamp();
        return true;
    }

    return false;
}

void SrsRtpDecodingState::UpdateOldPacket(const VCMPacket* packet)
{
    //assert(packet != NULL);
    if (packet == NULL) {
        return; 
    }

    if (packet->timestamp == time_stamp_) {
        // Late packet belonging to the last decoded frame - make sure we update the
        // last decoded sequence number.
        sequence_num_ = LatestSequenceNumber(packet->seqNum, sequence_num_);
    }
}

void SrsRtpDecodingState::SetSeqNum(uint16_t new_seq_num)
{
    sequence_num_ = new_seq_num;
}

bool SrsRtpDecodingState::in_initial_state() const
{
    return in_initial_state_;
}

bool SrsRtpDecodingState::full_sync() const
{
    return full_sync_;
}

void SrsRtpDecodingState::UpdateSyncState(const SrsRtpFrameBuffer* frame)
{
    if (in_initial_state_) {
        return;
    }
}

bool SrsRtpDecodingState::ContinuousFrame(const SrsRtpFrameBuffer* frame) const
{
    // Check continuity based on the following hierarchy:
    // - Temporal layers (stop here if out of sync).
    // - Picture Id when available.
    // - Sequence numbers.
    // Return true when in initial state.
    // Note that when a method is not applicable it will return false.
    //assert(frame != NULL);
    if (frame == NULL) {
        return false;
    }

    // A key frame is always considered continuous as it doesn't refer to any
    // frames and therefore won't introduce any errors even if prior frames are
    // missing.
    if (frame->GetFrameType() == kVideoFrameKey) {
        return true;
    }

    // When in the initial state we always require a key frame to start decoding.
    if (in_initial_state_) {
        return false;
    }

    return ContinuousSeqNum(static_cast<uint16_t>(frame->GetLowSeqNum()));
}

bool SrsRtpDecodingState::ContinuousSeqNum(uint16_t seq_num) const
{
    return seq_num == static_cast<uint16_t>(sequence_num_ + 1);
}

SrsRtpTimeJitter::SrsRtpTimeJitter()
{
    delta = 0;
    previous_timestamp = 0;
    pts = 0;
}

SrsRtpTimeJitter::~SrsRtpTimeJitter()
{
}

int64_t SrsRtpTimeJitter::timestamp()
{
    return pts;
}

srs_error_t SrsRtpTimeJitter::correct(int64_t& ts)
{
    srs_error_t err = srs_success;
    
    if (previous_timestamp == 0) {
        previous_timestamp = ts;
    }
    
    delta = srs_max(0, (int)(ts - previous_timestamp));
    if (delta > 90000) {
        delta = 0;
    }

    previous_timestamp = ts;
    
    ts = pts + delta;
    pts = ts;
    
    return err;
}


void SrsRtpTimeJitter::reset()
{
    delta = 0;
    previous_timestamp = 0;
    pts = 0;
}

SrsRtpJitterBuffer::SrsRtpJitterBuffer(std::string key):
    key_(key),
    running_(false),
    max_number_of_frames_(kStartNumberOfFrames),
    free_frames_(),
    decodable_frames_(),
    incomplete_frames_(),
    last_decoded_state_(),
    first_packet_since_reset_(true),
    incoming_frame_rate_(0),
    incoming_frame_count_(0),
    time_last_incoming_frame_count_(0),
    incoming_bit_count_(0),
    incoming_bit_rate_(0),
    num_consecutive_old_packets_(0),
    num_packets_(0),
    num_packets_free_(0),
    num_duplicated_packets_(0),
    num_discarded_packets_(0),
    time_first_packet_ms_(0),
    //jitter_estimate_(clock),
    //inter_frame_delay_(clock_->TimeInMilliseconds()),
    rtt_ms_(kDefaultRtt),
    nack_mode_(kNoNack),
    low_rtt_nack_threshold_ms_(-1),
    high_rtt_nack_threshold_ms_(-1),
    missing_sequence_numbers_(SequenceNumberLessThan()),
    nack_seq_nums_(),
    max_nack_list_size_(0),
    max_packet_age_to_nack_(0),
    max_incomplete_time_ms_(0),
    decode_error_mode_(kNoErrors),
    average_packets_per_frame_(0.0f),
    frame_counter_(0),
    last_received_timestamp_(0),
    last_received_sequence_number_(0),
    first_packet_(0)
{
    for (int i = 0; i < kStartNumberOfFrames; i++) {
        free_frames_.push_back(new SrsRtpFrameBuffer());
    }

    wait_cond_t = srs_cond_new();
}

SrsRtpJitterBuffer::~SrsRtpJitterBuffer()
{
    for (UnorderedFrameList::iterator it = free_frames_.begin();
            it != free_frames_.end(); ++it) {
        delete *it;
    }

    for (FrameList::iterator it = incomplete_frames_.begin();
            it != incomplete_frames_.end(); ++it) {
        delete it->second;
    }

    for (FrameList::iterator it = decodable_frames_.begin();
            it != decodable_frames_.end(); ++it) {
        delete it->second;
    }
    
    srs_cond_destroy(wait_cond_t);
}

void SrsRtpJitterBuffer::SetDecodeErrorMode(SrsRtpDecodeErrorMode error_mode)
{
    decode_error_mode_ = error_mode;
}

void SrsRtpJitterBuffer::Flush()
{
    //CriticalSectionScoped cs(crit_sect_);
    decodable_frames_.Reset(&free_frames_);
    incomplete_frames_.Reset(&free_frames_);
    last_decoded_state_.Reset();  // TODO(mikhal): sync reset.
    //frame_event_->Reset();
    num_consecutive_old_packets_ = 0;
    // Also reset the jitter and delay estimates
    //jitter_estimate_.Reset();
    //inter_frame_delay_.Reset(clock_->TimeInMilliseconds());
    //waiting_for_completion_.frame_size = 0;
    //waiting_for_completion_.timestamp = 0;
    //waiting_for_completion_.latest_packet_time = -1;
    first_packet_since_reset_ = true;
    missing_sequence_numbers_.clear();
}

void SrsRtpJitterBuffer::ResetJittter()
{
    Flush();
}


SrsRtpFrameBufferEnum SrsRtpJitterBuffer::InsertPacket(uint16_t seq, uint32_t ts, bool maker, char *buf, int size,
        bool* retransmitted)
{
    bool isFirstPacketInFrame = IsFirstPacketInFrame(ts, seq);
   
    const VCMPacket packet((const uint8_t*)buf, size,
        seq, ts, maker,
        kH264SingleNalu, kRtpVideoPS, true, isFirstPacketInFrame, kVideoFrameDelta);
   
    ++num_packets_;

    if (num_packets_ == 1) {
        time_first_packet_ms_ =  srs_update_system_time();
    }

    //Does this packet belong to an old frame?
    // if (last_decoded_state_.IsOldPacket(&packet)) {
     
    //     //return kOldPacket;
    // }

    //num_consecutive_old_packets_ = 0;

    SrsRtpFrameBuffer* frame;
    FrameList* frame_list;

    const SrsRtpFrameBufferEnum error = GetFrameByRtpPacket(packet, &frame, &frame_list);

    if (error != kNoError) {
        return error;
    }


    //srs_utime_t now_ms =  srs_update_system_time();

    FrameData frame_data;
    frame_data.rtt_ms = 0; //rtt_ms_;
    frame_data.rolling_average_packets_per_frame = 25;//average_packets_per_frame_;

    SrsRtpFrameBufferEnum buffer_state = frame->InsertPacket(packet, frame_data);
    
    if (buffer_state > 0) {
        incoming_bit_count_ += packet.sizeBytes << 3;

        if (first_packet_since_reset_) {
            latest_received_sequence_number_ = packet.seqNum;
            first_packet_since_reset_ = false;
        } else {
            // if (IsPacketRetransmitted(packet)) {
            //     frame->IncrementNackCount();
            // }

            UpdateNackList(packet.seqNum);

            latest_received_sequence_number_ = LatestSequenceNumber(
                                                   latest_received_sequence_number_, packet.seqNum);
        }
    }

    // Is the frame already in the decodable list?
    bool continuous = IsContinuous(*frame);
    
    switch (buffer_state) {
    case kGeneralError:
    case kTimeStampError:
    case kSizeError: {
        free_frames_.push_back(frame);
        break;
    }

    case kCompleteSession: {
        //CountFrame(*frame);
        // if (previous_state != kStateDecodable &&
        //         previous_state != kStateComplete) {
        //     /*CountFrame(*frame);*/ //????????????????????�?? by ylr
        //     if (continuous) {
        //         // Signal that we have a complete session.
        //         frame_event_->Set();
        //     }
        // }
    }

    // Note: There is no break here - continuing to kDecodableSession.
    case kDecodableSession: {
        // *retransmitted = (frame->GetNackCount() > 0);

        if (true || continuous) {
            decodable_frames_.InsertFrame(frame);
            FindAndInsertContinuousFrames(*frame);
        } else {
            incomplete_frames_.InsertFrame(frame);

            // If NACKs are enabled, keyframes are triggered by |GetNackList|.
            // if (nack_mode_ == kNoNack && NonContinuousOrIncompleteDuration() >
            //         90 * kMaxDiscontinuousFramesTime) {
            //     return kFlushIndicator;
            // }
        }

        break;
    }

    case kIncomplete: {
        if (frame->GetState() == kStateEmpty &&
                last_decoded_state_.UpdateEmptyFrame(frame)) {
            free_frames_.push_back(frame);
            return kNoError;
        } else {
            incomplete_frames_.InsertFrame(frame);

            // If NACKs are enabled, keyframes are triggered by |GetNackList|.
            // if (nack_mode_ == kNoNack && NonContinuousOrIncompleteDuration() >
            //         90 * kMaxDiscontinuousFramesTime) {
            //     return kFlushIndicator;
            // }
        }

        break;
    }

    case kNoError:
    case kOutOfBoundsPacket:
    case kDuplicatePacket: {
        // Put back the frame where it came from.
        if (frame_list != NULL) {
            frame_list->InsertFrame(frame);
        } else {
            free_frames_.push_back(frame);
        }

        ++num_duplicated_packets_;
        break;
    }

    case kFlushIndicator:{
            free_frames_.push_back(frame);
        }
        return kFlushIndicator;

    default:
        assert(false);
    }

    return buffer_state;
}


SrsRtpFrameBufferEnum SrsRtpJitterBuffer::InsertPacket2(const SrsRtpPacket2 &pkt,
        bool* retransmitted)
{
    bool singlenual = false;
    H264PacketizationTypes packetyType = kH264SingleNalu;

    //char *buf = pkt.shared_msg->payload; //rtp packet data
    //int size = pkt.shared_msg->size; //rtp size

    //rtp header: total size - rtp payload size
    int rtp_header_size = pkt.shared_msg->size - pkt.payload->nb_bytes();
   
    char *rtp_payload_buf = pkt.shared_msg->payload + rtp_header_size;  //rtp payload data
    int rtp_payload_size = pkt.shared_msg->size - rtp_header_size;  //rtp payload size;

    bool is_first_packet_in_frame = false;
  
    if (rtp_payload_size == 0){ 
        //B0(1011):pad+ext, A0(1010):pad
        if ((pkt.shared_msg->payload[0] & 0xFF) == 0xB0 || (pkt.shared_msg->payload[0] & 0xFF) == 0xA0){
            //uint8_t padding_length = (uint8_t)(pkt.shared_msg->payload[pkt.shared_msg->size-1] & 0xFF);
            srs_info("RTP: jitbuffer padding packet ts:%u, seq:%u, payload size:%d, padding size:%d", 
                pkt.header.get_timestamp(),pkt.header.get_sequence(),
                rtp_payload_size, padding_length);
        }
        return kNoError;
    }
    
    SrsAvcNaluType nal_unit_type = SrsAvcNaluTypeReserved;
    FrameType frameType = kVideoFrameDelta;

    int8_t v = (uint8_t)pkt.nalu_type;
    if (v == kStapA) {
        singlenual = false;
        packetyType = kH264StapA;

        is_first_packet_in_frame = true;
        
        SrsRtpSTAPPayload *payload = (SrsRtpSTAPPayload*)pkt.payload;
        if (payload->get_sps() != NULL){
            nal_unit_type = SrsAvcNaluTypeSPS;
            frameType = kVideoFrameKey;
        }
    } else if (v == kFuA) {
        SrsRtpFUAPayload2 *payload = (SrsRtpFUAPayload2*)pkt.payload;
        int8_t nalu_byte0 = ((int8_t)payload->nri & 0xE0) | ((int8_t)payload->nalu_type & 0x1F);
        nal_unit_type = (SrsAvcNaluType)(nalu_byte0 & 0x1f);

        if (nal_unit_type == SrsAvcNaluTypeIDR){
            frameType = kVideoFrameKey;
        }
 
        if (payload->start){
            //xx xx ....
            //xx nalu ....
            rtp_payload_buf[1] = nalu_byte0;

            //nalu ....
            rtp_payload_buf = rtp_payload_buf + 1;
            rtp_payload_size = rtp_payload_size - 1;

            is_first_packet_in_frame = true;
        }else {
            //xx xx ....
            //....
            rtp_payload_buf = rtp_payload_buf + 2;
            rtp_payload_size = rtp_payload_size - 2;
        }

        singlenual = false;
        packetyType = kH264FuA;

    } else {
        singlenual = true;
        packetyType = kH264SingleNalu;
        is_first_packet_in_frame = true;
    }

    const VCMPacket packet((const uint8_t*)rtp_payload_buf, rtp_payload_size,
        pkt.header.get_sequence(), pkt.header.get_timestamp(), pkt.header.get_marker(), 
        packetyType, kRtpVideoH264, singlenual, is_first_packet_in_frame, frameType);

    ++num_packets_;

    if (num_packets_ == 1) {
        time_first_packet_ms_ =  srs_update_system_time();
    }

    //Does this packet belong to an old frame?
    // if (last_decoded_state_.IsOldPacket(&packet)) {
     
    //     //return kOldPacket;
    // }

    //num_consecutive_old_packets_ = 0;

    SrsRtpFrameBuffer* frame;
    FrameList* frame_list;

    const SrsRtpFrameBufferEnum error = GetFrameByRtpPacket(packet, &frame, &frame_list);

    if (error != kNoError) {
        return error;
    }


    //srs_utime_t now_ms =  srs_update_system_time();

    FrameData frame_data;
    frame_data.rtt_ms = 0; //rtt_ms_;
    frame_data.rolling_average_packets_per_frame = 25;//average_packets_per_frame_;

    SrsRtpFrameBufferEnum buffer_state = frame->InsertPacket(packet, frame_data);
    
    if (buffer_state > 0) {
        incoming_bit_count_ += packet.sizeBytes << 3;

        if (first_packet_since_reset_) {
            latest_received_sequence_number_ = packet.seqNum;
            first_packet_since_reset_ = false;
        } else {
            // if (IsPacketRetransmitted(packet)) {
            //     frame->IncrementNackCount();
            // }

            UpdateNackList(packet.seqNum);

            latest_received_sequence_number_ = LatestSequenceNumber(
                                                   latest_received_sequence_number_, packet.seqNum);
        }
    }

    // Is the frame already in the decodable list?
    bool continuous = IsContinuous(*frame);
    
    switch (buffer_state) {
    case kGeneralError:
    case kTimeStampError:
    case kSizeError: {
        free_frames_.push_back(frame);
        break;
    }

    case kCompleteSession: {
        //CountFrame(*frame);
        // if (previous_state != kStateDecodable &&
        //         previous_state != kStateComplete) {
        //     /*CountFrame(*frame);*/ //????????????????????�?? by ylr
        //     if (continuous) {
        //         // Signal that we have a complete session.
        //         frame_event_->Set();
        //     }
        // }
    }

    // Note: There is no break here - continuing to kDecodableSession.
    case kDecodableSession: {
        // *retransmitted = (frame->GetNackCount() > 0);

        if (true || continuous) {
            decodable_frames_.InsertFrame(frame);
            FindAndInsertContinuousFrames(*frame);
        } else {
            incomplete_frames_.InsertFrame(frame);

            // If NACKs are enabled, keyframes are triggered by |GetNackList|.
            // if (nack_mode_ == kNoNack && NonContinuousOrIncompleteDuration() >
            //         90 * kMaxDiscontinuousFramesTime) {
            //     return kFlushIndicator;
            // }
        }

        break;
    }

    case kIncomplete: {
        if (frame->GetState() == kStateEmpty &&
                last_decoded_state_.UpdateEmptyFrame(frame)) {
            free_frames_.push_back(frame);
            return kNoError;
        } else {
            incomplete_frames_.InsertFrame(frame);

            // If NACKs are enabled, keyframes are triggered by |GetNackList|.
            // if (nack_mode_ == kNoNack && NonContinuousOrIncompleteDuration() >
            //         90 * kMaxDiscontinuousFramesTime) {
            //     return kFlushIndicator;
            // }
        }

        break;
    }

    case kNoError:
    case kOutOfBoundsPacket:
    case kDuplicatePacket: {
        // Put back the frame where it came from.
        if (frame_list != NULL) {
            frame_list->InsertFrame(frame);
        } else {
            free_frames_.push_back(frame);
        }

        ++num_duplicated_packets_;
        break;
    }

    case kFlushIndicator:{
            free_frames_.push_back(frame);
        }
        return kFlushIndicator;

    default:
        assert(false);
    }

    return buffer_state;
}

// Gets frame to use for this timestamp. If no match, get empty frame.
SrsRtpFrameBufferEnum SrsRtpJitterBuffer::GetFrameByRtpPacket(const VCMPacket& packet,
        SrsRtpFrameBuffer** frame,
        FrameList** frame_list)
{
    *frame = incomplete_frames_.PopFrame(packet.timestamp);

    if (*frame != NULL) {
        *frame_list = &incomplete_frames_;
        return kNoError;
    }

    *frame = decodable_frames_.PopFrame(packet.timestamp);

    if (*frame != NULL) {
        *frame_list = &decodable_frames_;
        return kNoError;
    }

    *frame_list = NULL;
    // No match, return empty frame.
    *frame = GetEmptyFrame();

    if (*frame == NULL) {
        // No free frame! Try to reclaim some...
        bool found_key_frame = RecycleFramesUntilKeyFrame();
        *frame = GetEmptyFrame();
        assert(*frame);

        if (!found_key_frame) {
            free_frames_.push_back(*frame);
            return kFlushIndicator;
        }
    }

    (*frame)->Reset();
    return kNoError;
}

SrsRtpFrameBuffer* SrsRtpJitterBuffer::GetEmptyFrame()
{
    if (free_frames_.empty()) {
        if (!TryToIncreaseJitterBufferSize()) {
            return NULL;
        }
    }

    SrsRtpFrameBuffer* frame = free_frames_.front();
    free_frames_.pop_front();
    return frame;
}

bool SrsRtpJitterBuffer::TryToIncreaseJitterBufferSize()
{
    if (max_number_of_frames_ >= kMaxNumberOfFrames) {
        return false;
    }

    free_frames_.push_back(new SrsRtpFrameBuffer());
    ++max_number_of_frames_;
    return true;
}

// Recycle oldest frames up to a key frame, used if jitter buffer is completely
// full.
bool SrsRtpJitterBuffer::RecycleFramesUntilKeyFrame()
{
    // First release incomplete frames, and only release decodable frames if there
    // are no incomplete ones.
    FrameList::iterator key_frame_it;
    bool key_frame_found = false;
    int dropped_frames = 0;
    dropped_frames += incomplete_frames_.RecycleFramesUntilKeyFrame(
                          &key_frame_it, &free_frames_);
    key_frame_found = key_frame_it != incomplete_frames_.end();

    if (dropped_frames == 0) {
        dropped_frames += decodable_frames_.RecycleFramesUntilKeyFrame(
                              &key_frame_it, &free_frames_);
        key_frame_found = key_frame_it != decodable_frames_.end();
    }

    if (key_frame_found) {
        //LOG(LS_INFO) << "Found key frame while dropping frames.";
        // Reset last decoded state to make sure the next frame decoded is a key
        // frame, and start NACKing from here.
        last_decoded_state_.Reset();
        DropPacketsFromNackList(EstimatedLowSequenceNumber(*key_frame_it->second));
    } else if (decodable_frames_.empty()) {
        // All frames dropped. Reset the decoding state and clear missing sequence
        // numbers as we're starting fresh.
        last_decoded_state_.Reset();
        missing_sequence_numbers_.clear();
    }

    return key_frame_found;
}

bool SrsRtpJitterBuffer::IsContinuousInState(const SrsRtpFrameBuffer& frame,
        const SrsRtpDecodingState& decoding_state) const
{
    if (decode_error_mode_ == kWithErrors) {
         return true;
    }

    // Is this frame (complete or decodable) and continuous?
    // kStateDecodable will never be set when decode_error_mode_ is false
    // as SessionInfo determines this state based on the error mode (and frame
    // completeness).
    return (frame.GetState() == kStateComplete ||
            frame.GetState() == kStateDecodable) &&
           decoding_state.ContinuousFrame(&frame);
}

bool SrsRtpJitterBuffer::IsContinuous(const SrsRtpFrameBuffer& frame) const
{
    if (IsContinuousInState(frame, last_decoded_state_)) {
         return true;
    }

    SrsRtpDecodingState decoding_state;
    decoding_state.CopyFrom(last_decoded_state_);

    for (FrameList::const_iterator it = decodable_frames_.begin();
            it != decodable_frames_.end(); ++it)  {
        SrsRtpFrameBuffer* decodable_frame = it->second;

        if (IsNewerTimestamp(decodable_frame->GetTimeStamp(), frame.GetTimeStamp())) {
            break;
        }

        decoding_state.SetState(decodable_frame);

        if (IsContinuousInState(frame, decoding_state)) {
            return true;
        }
    }

    return false;
}

void SrsRtpJitterBuffer::FindAndInsertContinuousFrames(const SrsRtpFrameBuffer& new_frame)
{
    SrsRtpDecodingState decoding_state;
    decoding_state.CopyFrom(last_decoded_state_);
    decoding_state.SetState(&new_frame);

    // When temporal layers are available, we search for a complete or decodable
    // frame until we hit one of the following:
    // 1. Continuous base or sync layer.
    // 2. The end of the list was reached.
    for (FrameList::iterator it = incomplete_frames_.begin();
            it != incomplete_frames_.end();)  {
        SrsRtpFrameBuffer* frame = it->second;

        if (IsNewerTimestamp(new_frame.GetTimeStamp(), frame->GetTimeStamp())) {
            ++it;
            continue;
        }

        if (IsContinuousInState(*frame, decoding_state)) {
            decodable_frames_.InsertFrame(frame);
            incomplete_frames_.erase(it++);
            decoding_state.SetState(frame);
        } else {
            ++it;
        }
    }
}

// Must be called under the critical section |crit_sect_|.
void SrsRtpJitterBuffer::CleanUpOldOrEmptyFrames()
{
    decodable_frames_.CleanUpOldOrEmptyFrames(&last_decoded_state_,
            &free_frames_);
    incomplete_frames_.CleanUpOldOrEmptyFrames(&last_decoded_state_,
            &free_frames_);

    if (!last_decoded_state_.in_initial_state()) {
        DropPacketsFromNackList(last_decoded_state_.sequence_num());
    }
}

// Returns immediately or a |max_wait_time_ms| ms event hang waiting for a
// complete frame, |max_wait_time_ms| decided by caller.
bool SrsRtpJitterBuffer::NextCompleteTimestamp(uint32_t max_wait_time_ms, uint32_t* timestamp)
{
    // crit_sect_->Enter();

    // if (!running_) {
    //     crit_sect_->Leave();
    //     return false;
    // }

    CleanUpOldOrEmptyFrames();

    if (decodable_frames_.empty() ||
            decodable_frames_.Front()->GetState() != kStateComplete) {
        const int64_t end_wait_time_ms = srs_update_system_time() +
                                         max_wait_time_ms * SRS_UTIME_MILLISECONDS;
        int64_t wait_time_ms = max_wait_time_ms * SRS_UTIME_MILLISECONDS;

        while (wait_time_ms > 0) {
            int ret = srs_cond_timedwait(wait_cond_t, wait_time_ms);
            if (ret == 0) {
                // Finding oldest frame ready for decoder.
                CleanUpOldOrEmptyFrames();

                if (decodable_frames_.empty() ||
                        decodable_frames_.Front()->GetState() != kStateComplete) {
                    wait_time_ms = end_wait_time_ms - srs_update_system_time();
                } else {
                    break;
                }
            } else {
                break;
            }
        }

        // Inside |crit_sect_|.
    } else {
        // We already have a frame, reset the event.
        //frame_event_->Reset();
    }

    if (decodable_frames_.empty() ||
            decodable_frames_.Front()->GetState() != kStateComplete) {
        //crit_sect_->Leave();
        return false;
    }

    *timestamp = decodable_frames_.Front()->GetTimeStamp();
    //crit_sect_->Leave();
    return true;
}

bool SrsRtpJitterBuffer::NextMaybeIncompleteTimestamp(uint32_t* timestamp)
{
    if (decode_error_mode_ == kNoErrors) {
        srs_warn("RTP jitbuffer NextMaybeIncompleteTimestamp decode_error_mode_ %d", decode_error_mode_);
        // No point to continue, as we are not decoding with errors.
        return false;
    }

    CleanUpOldOrEmptyFrames();

    SrsRtpFrameBuffer* oldest_frame;

    if (decodable_frames_.empty()) {
        if (incomplete_frames_.size() <= 1) {
            return false;
        }

        oldest_frame = incomplete_frames_.Front();
        SrsRtpFrameBufferStateEnum oldest_frame_state = oldest_frame->GetState();

        SrsRtpFrameBuffer* next_frame;
        next_frame = incomplete_frames_.FrontNext();
    
        if (oldest_frame_state !=  kStateComplete && next_frame &&
         IsNewerSequenceNumber(next_frame->GetLowSeqNum(), oldest_frame->GetHighSeqNum()) &&
         next_frame->NumPackets() > 0 ) {
            oldest_frame_state = kStateComplete;
        }

        // Frame will only be removed from buffer if it is complete (or decodable).
        if (oldest_frame_state < kStateComplete) {
            int oldest_frame_hight_seq = oldest_frame->GetHighSeqNum();
            int next_frame_low_seq = next_frame->GetLowSeqNum();

            srs_warn("RTP: jitbuffer NextMaybeIncompleteTimestamp key(%s) incomplete oldest_frame (%u,%d)->(%u,%d)",
                    key_.c_str(), oldest_frame->GetTimeStamp(), oldest_frame_hight_seq, 
                    next_frame->GetTimeStamp(), next_frame_low_seq);
            return false;
        }
    } else {
        oldest_frame = decodable_frames_.Front();

        // If we have exactly one frame in the buffer, release it only if it is
        // complete. We know decodable_frames_ is  not empty due to the previous
        // check.
        if (decodable_frames_.size() == 1 && incomplete_frames_.empty()
                && oldest_frame->GetState() != kStateComplete) {
            return false;
        }
    }

    *timestamp = oldest_frame->GetTimeStamp();
    return true;
}

SrsRtpFrameBuffer* SrsRtpJitterBuffer::ExtractAndSetDecode(uint32_t timestamp)
{
    // Extract the frame with the desired timestamp.
    SrsRtpFrameBuffer* frame = decodable_frames_.PopFrame(timestamp);
    bool continuous = true;

    if (!frame) {
        frame = incomplete_frames_.PopFrame(timestamp);

        if (frame) {
            continuous = last_decoded_state_.ContinuousFrame(frame);
        } else {
            return NULL;
        }
    }

    // The state must be changed to decoding before cleaning up zero sized
    // frames to avoid empty frames being cleaned up and then given to the
    // decoder. Propagates the missing_frame bit.
    //frame->PrepareForDecode(continuous);

    // We have a frame - update the last decoded state and nack list.
    last_decoded_state_.SetState(frame);
    //DropPacketsFromNackList(last_decoded_state_.sequence_num());

    // if ((*frame).IsSessionComplete()) {
    //     //UpdateAveragePacketsPerFrame(frame->NumPackets());
    // }

    return frame;
}

// Release frame when done with decoding. Should never be used to release
// frames from within the jitter buffer.
void SrsRtpJitterBuffer::ReleaseFrame(SrsRtpFrameBuffer* frame)
{
    //CriticalSectionScoped cs(crit_sect_);
    //VCMFrameBuffer* frame_buffer = static_cast<VCMFrameBuffer*>(frame);
   
    if (frame) {
        free_frames_.push_back(frame);
    }
}

bool SrsRtpJitterBuffer::FoundFrame(uint32_t& time_stamp)
{
    
    bool found_frame = NextCompleteTimestamp(0, &time_stamp);

    if (!found_frame) {
        found_frame = NextMaybeIncompleteTimestamp(&time_stamp);
    }

    return found_frame;
}

bool SrsRtpJitterBuffer::GetFrame(char **buffer,  int &buf_len, int &size, bool &keyframe, const uint32_t time_stamp)
{
    SrsRtpFrameBuffer* frame = ExtractAndSetDecode(time_stamp);

    if (frame == NULL) {
        return false;
    }

    size = frame->Length();
    if (size <= 0){
        return false;
    }

    if (buffer == NULL){
        return false;
    }
   
    //verify and allocate a frame buffer, used to completed frame data
    if (buf_len < size || *buffer == NULL) {
        srs_freepa(*buffer);

        int resize = size + kBufferIncStepSizeBytes;
        *buffer = new char[resize];

        srs_trace("RTP: jitbuffer key=%s reallocate a frame buffer size(%d>%d) resize(%d)", 
            key_.c_str(), size, buf_len, resize);
            
        buf_len = resize;
    }

    const uint8_t *frame_buffer = frame->Buffer();
    memcpy(*buffer, frame_buffer, size);
    if (frame->GetFrameType() == kVideoFrameKey){
        keyframe = true;
    }

    frame->PrepareForDecode(false);
    ReleaseFrame(frame);
    return true;
}


SrsRtpFrameBuffer* SrsRtpJitterBuffer::NextFrame() const
{
    if (!decodable_frames_.empty()) {
        return decodable_frames_.Front();
    }

    if (!incomplete_frames_.empty()) {
        return incomplete_frames_.Front();
    }

    return NULL;
}

bool SrsRtpJitterBuffer::UpdateNackList(uint16_t sequence_number)
{
    if (nack_mode_ == kNoNack) {
        return true;
    }

    // Make sure we don't add packets which are already too old to be decoded.
    if (!last_decoded_state_.in_initial_state()) {
        latest_received_sequence_number_ = LatestSequenceNumber(
                                               latest_received_sequence_number_,
                                               last_decoded_state_.sequence_num());
    }

    if (IsNewerSequenceNumber(sequence_number,
                              latest_received_sequence_number_)) {
        // Push any missing sequence numbers to the NACK list.
        for (uint16_t i = latest_received_sequence_number_ + 1;
                IsNewerSequenceNumber(sequence_number, i); ++i) {
            missing_sequence_numbers_.insert(missing_sequence_numbers_.end(), i);
        }

        if (TooLargeNackList() && !HandleTooLargeNackList()) {
            srs_warn("RTP: jitbuffer key(%s) requesting key frame due to too large NACK list.",  key_.c_str());
            return false;
        }

        if (MissingTooOldPacket(sequence_number) &&
                !HandleTooOldPackets(sequence_number)) {
            srs_warn("RTP: jitbuffer key(%s) requesting key frame due to missing too old packets",  key_.c_str());
            return false;
        }
        
    } else {
        missing_sequence_numbers_.erase(sequence_number);
    }

    return true;
}

bool SrsRtpJitterBuffer::TooLargeNackList() const
{
    return missing_sequence_numbers_.size() > max_nack_list_size_;
}

bool SrsRtpJitterBuffer::HandleTooLargeNackList()
{
    // Recycle frames until the NACK list is small enough. It is likely cheaper to
    // request a key frame than to retransmit this many missing packets.
    srs_warn("RTP: jitbuffer NACK list has grown too large: %d > %d", 
                    missing_sequence_numbers_.size(), max_nack_list_size_);
    bool key_frame_found = false;

    while (TooLargeNackList()) {
        key_frame_found = RecycleFramesUntilKeyFrame();
    }

    return key_frame_found;
}

bool SrsRtpJitterBuffer::MissingTooOldPacket(uint16_t latest_sequence_number) const
{
    if (missing_sequence_numbers_.empty()) {
        return false;
    }

    const uint16_t age_of_oldest_missing_packet = latest_sequence_number -
            *missing_sequence_numbers_.begin();
    // Recycle frames if the NACK list contains too old sequence numbers as
    // the packets may have already been dropped by the sender.
    return age_of_oldest_missing_packet > max_packet_age_to_nack_;
}

bool SrsRtpJitterBuffer::HandleTooOldPackets(uint16_t latest_sequence_number)
{
    bool key_frame_found = false;
    const uint16_t age_of_oldest_missing_packet = latest_sequence_number -
            *missing_sequence_numbers_.begin();
    srs_warn("RTP: jitbuffer NACK list contains too old sequence numbers: %d > %d",
                      age_of_oldest_missing_packet,
                      max_packet_age_to_nack_);

    while (MissingTooOldPacket(latest_sequence_number)) {
        key_frame_found = RecycleFramesUntilKeyFrame();
    }

    return key_frame_found;
}

void SrsRtpJitterBuffer::DropPacketsFromNackList(uint16_t last_decoded_sequence_number)
{
    // Erase all sequence numbers from the NACK list which we won't need any
    // longer.
    missing_sequence_numbers_.erase(missing_sequence_numbers_.begin(),
                                    missing_sequence_numbers_.upper_bound(
                                        last_decoded_sequence_number));
}

void SrsRtpJitterBuffer::SetNackMode(SrsRtpNackMode mode,
                                  int64_t low_rtt_nack_threshold_ms,
                                  int64_t high_rtt_nack_threshold_ms)
{
    nack_mode_ = mode;

    if (mode == kNoNack) {
        missing_sequence_numbers_.clear();
    }

    assert(low_rtt_nack_threshold_ms >= -1 && high_rtt_nack_threshold_ms >= -1);
    assert(high_rtt_nack_threshold_ms == -1 || 
           low_rtt_nack_threshold_ms <= high_rtt_nack_threshold_ms);
    assert(low_rtt_nack_threshold_ms > -1 || high_rtt_nack_threshold_ms == -1);
    low_rtt_nack_threshold_ms_ = low_rtt_nack_threshold_ms;
    high_rtt_nack_threshold_ms_ = high_rtt_nack_threshold_ms;

    // Don't set a high start rtt if high_rtt_nack_threshold_ms_ is used, to not
    // disable NACK in hybrid mode.
    if (rtt_ms_ == kDefaultRtt && high_rtt_nack_threshold_ms_ != -1) {
        rtt_ms_ = 0;
    }

    // if (!WaitForRetransmissions()) {
    //     jitter_estimate_.ResetNackCount();
    // }
}

void SrsRtpJitterBuffer::SetNackSettings(size_t max_nack_list_size,
                                      int max_packet_age_to_nack,
                                      int max_incomplete_time_ms)
{
    assert(max_packet_age_to_nack >= 0);
    assert(max_incomplete_time_ms_ >= 0);
    max_nack_list_size_ = max_nack_list_size;
    max_packet_age_to_nack_ = max_packet_age_to_nack;
    max_incomplete_time_ms_ = max_incomplete_time_ms;
    nack_seq_nums_.resize(max_nack_list_size_);
}

SrsRtpNackMode SrsRtpJitterBuffer::nack_mode() const
{
    return nack_mode_;
}


int SrsRtpJitterBuffer::NonContinuousOrIncompleteDuration()
{
    if (incomplete_frames_.empty()) {
        return 0;
    }

    uint32_t start_timestamp = incomplete_frames_.Front()->GetTimeStamp();

    if (!decodable_frames_.empty()) {
        start_timestamp = decodable_frames_.Back()->GetTimeStamp();
    }

    return incomplete_frames_.Back()->GetTimeStamp() - start_timestamp;
}

uint16_t SrsRtpJitterBuffer::EstimatedLowSequenceNumber(const SrsRtpFrameBuffer& frame) const
{
    assert(frame.GetLowSeqNum() >= 0);

    if (frame.HaveFirstPacket()) {
        return frame.GetLowSeqNum();
    }

    // This estimate is not accurate if more than one packet with lower sequence
    // number is lost.
    return frame.GetLowSeqNum() - 1;
}

uint16_t* SrsRtpJitterBuffer::GetNackList(uint16_t* nack_list_size,
                                       bool* request_key_frame)
{
    //CriticalSectionScoped cs(crit_sect_);
    *request_key_frame = false;

    if (nack_mode_ == kNoNack) {
        *nack_list_size = 0;
        return NULL;
    }

    if (last_decoded_state_.in_initial_state()) {
        SrsRtpFrameBuffer* next_frame = NextFrame();
        const bool first_frame_is_key = next_frame &&
                                        next_frame->GetFrameType() == kVideoFrameKey &&
                                        next_frame->HaveFirstPacket();

        if (!first_frame_is_key) {
            bool have_non_empty_frame = decodable_frames_.end() != find_if(
                                            decodable_frames_.begin(), decodable_frames_.end(),
                                            HasNonEmptyState);

            if (!have_non_empty_frame) {
                have_non_empty_frame = incomplete_frames_.end() != find_if(
                                           incomplete_frames_.begin(), incomplete_frames_.end(),
                                           HasNonEmptyState);
            }

            bool found_key_frame = RecycleFramesUntilKeyFrame();

            if (!found_key_frame) {
                *request_key_frame = have_non_empty_frame;
                *nack_list_size = 0;
                return NULL;
            }
        }
    }

    if (TooLargeNackList()) {
        *request_key_frame = !HandleTooLargeNackList();
    }

    if (max_incomplete_time_ms_ > 0) {
        int non_continuous_incomplete_duration =
            NonContinuousOrIncompleteDuration();

        if (non_continuous_incomplete_duration > 90 * max_incomplete_time_ms_) {
            // LOG_F(LS_WARNING) << "Too long non-decodable duration: "
            //                   << non_continuous_incomplete_duration << " > "
            //                   << 90 * max_incomplete_time_ms_;
            FrameList::reverse_iterator rit = find_if(incomplete_frames_.rbegin(),
                                              incomplete_frames_.rend(), IsKeyFrame);

            if (rit == incomplete_frames_.rend()) {
                // Request a key frame if we don't have one already.
                *request_key_frame = true;
                *nack_list_size = 0;
                return NULL;
            } else {
                // Skip to the last key frame. If it's incomplete we will start
                // NACKing it.
                // Note that the estimated low sequence number is correct for VP8
                // streams because only the first packet of a key frame is marked.
                last_decoded_state_.Reset();
                DropPacketsFromNackList(EstimatedLowSequenceNumber(*rit->second));
            }
        }
    }

    unsigned int i = 0;
    SequenceNumberSet::iterator it = missing_sequence_numbers_.begin();

    for (; it != missing_sequence_numbers_.end(); ++it, ++i) {
        nack_seq_nums_[i] = *it;
    }

    *nack_list_size = i;
    return &nack_seq_nums_[0];
}

bool SrsRtpJitterBuffer::WaitForRetransmissions()
{
    if (nack_mode_ == kNoNack) {
        // NACK disabled -> don't wait for retransmissions.
        return false;
    }

    // Evaluate if the RTT is higher than |high_rtt_nack_threshold_ms_|, and in
    // that case we don't wait for retransmissions.
    if (high_rtt_nack_threshold_ms_ >= 0 &&
            rtt_ms_ >= high_rtt_nack_threshold_ms_) {
        return false;
    }

    return true;
}

bool SrsRtpJitterBuffer::IsPacketInOrder(uint16_t sequence_number)
{
    if (!first_packet_) {
        return true;
    }

    if (IsNewerSequenceNumber(sequence_number, last_received_sequence_number_)) {
        return true;
    }

    return false;
}

bool SrsRtpJitterBuffer::IsFirstPacketInFrame(uint32_t ts, uint16_t seq)
{
    bool is_first_packet_in_frame = false;
    bool in_order = IsPacketInOrder(seq);

    if (first_packet_){
        is_first_packet_in_frame =
            last_received_sequence_number_ + 1 == seq &&
            last_received_timestamp_ != ts;
    }else{
        is_first_packet_in_frame = true;
    }

    if (in_order) {
        first_packet_ = true;

        if (last_received_timestamp_ != ts) {
            last_received_timestamp_ = ts;
        }

        last_received_sequence_number_ = seq;
    }

    return is_first_packet_in_frame;
}
