//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_kernel_flv.hpp>

// for srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
#include <unistd.h>
#endif

#include <fcntl.h>
#include <sstream>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_rtc_rtp.hpp>

#include <srs_kernel_kbps.hpp>

SrsPps* _srs_pps_objs_msgs = NULL;

SrsMessageHeader::SrsMessageHeader()
{
    message_type = 0;
    payload_length = 0;
    timestamp_delta = 0;
    stream_id = 0;
    
    timestamp = 0;
    // we always use the connection chunk-id
    prefer_cid = RTMP_CID_OverConnection;
}

SrsMessageHeader::~SrsMessageHeader()
{
}

bool SrsMessageHeader::is_audio()
{
    return message_type == RTMP_MSG_AudioMessage;
}

bool SrsMessageHeader::is_video()
{
    return message_type == RTMP_MSG_VideoMessage;
}

bool SrsMessageHeader::is_amf0_command()
{
    return message_type == RTMP_MSG_AMF0CommandMessage;
}

bool SrsMessageHeader::is_amf0_data()
{
    return message_type == RTMP_MSG_AMF0DataMessage;
}

bool SrsMessageHeader::is_amf3_command()
{
    return message_type == RTMP_MSG_AMF3CommandMessage;
}

bool SrsMessageHeader::is_amf3_data()
{
    return message_type == RTMP_MSG_AMF3DataMessage;
}

bool SrsMessageHeader::is_window_ackledgement_size()
{
    return message_type == RTMP_MSG_WindowAcknowledgementSize;
}

bool SrsMessageHeader::is_ackledgement()
{
    return message_type == RTMP_MSG_Acknowledgement;
}

bool SrsMessageHeader::is_set_chunk_size()
{
    return message_type == RTMP_MSG_SetChunkSize;
}

bool SrsMessageHeader::is_user_control_message()
{
    return message_type == RTMP_MSG_UserControlMessage;
}

bool SrsMessageHeader::is_set_peer_bandwidth()
{
    return message_type == RTMP_MSG_SetPeerBandwidth;
}

bool SrsMessageHeader::is_aggregate()
{
    return message_type == RTMP_MSG_AggregateMessage;
}

void SrsMessageHeader::initialize_amf0_script(int size, int stream)
{
    message_type = RTMP_MSG_AMF0DataMessage;
    payload_length = (int32_t)size;
    timestamp_delta = (int32_t)0;
    timestamp = (int64_t)0;
    stream_id = (int32_t)stream;
    
    // amf0 script use connection2 chunk-id
    prefer_cid = RTMP_CID_OverConnection2;
}

void SrsMessageHeader::initialize_audio(int size, uint32_t time, int stream)
{
    message_type = RTMP_MSG_AudioMessage;
    payload_length = (int32_t)size;
    timestamp_delta = (int32_t)time;
    timestamp = (int64_t)time;
    stream_id = (int32_t)stream;
    
    // audio chunk-id
    prefer_cid = RTMP_CID_Audio;
}

void SrsMessageHeader::initialize_video(int size, uint32_t time, int stream)
{
    message_type = RTMP_MSG_VideoMessage;
    payload_length = (int32_t)size;
    timestamp_delta = (int32_t)time;
    timestamp = (int64_t)time;
    stream_id = (int32_t)stream;
    
    // video chunk-id
    prefer_cid = RTMP_CID_Video;
}

SrsCommonMessage::SrsCommonMessage()
{
    payload = NULL;
    size = 0;
}

SrsCommonMessage::~SrsCommonMessage()
{
    srs_freepa(payload);
}

void SrsCommonMessage::create_payload(int size)
{
    srs_freepa(payload);
    
    payload = new char[size];
    srs_verbose("create payload for RTMP message. size=%d", size);
}

srs_error_t SrsCommonMessage::create(SrsMessageHeader* pheader, char* body, int size)
{
    // drop previous payload.
    srs_freepa(payload);
    
    this->header = *pheader;
    this->payload = body;
    this->size = size;
    
    return srs_success;
}

SrsSharedMessageHeader::SrsSharedMessageHeader()
{
    payload_length = 0;
    message_type = 0;
    prefer_cid = 0;
}

SrsSharedMessageHeader::~SrsSharedMessageHeader()
{
}

SrsSharedPtrMessage::SrsSharedPtrPayload::SrsSharedPtrPayload()
{
    payload = NULL;
    size = 0;
    shared_count = 0;
}

SrsSharedPtrMessage::SrsSharedPtrPayload::~SrsSharedPtrPayload()
{
    srs_freepa(payload);
}

SrsSharedPtrMessage::SrsSharedPtrMessage() : timestamp(0), stream_id(0), size(0), payload(NULL)
{
    ptr = NULL;

    ++ _srs_pps_objs_msgs->sugar;
}

SrsSharedPtrMessage::~SrsSharedPtrMessage()
{
    if (ptr) {
        if (ptr->shared_count == 0) {
            srs_freep(ptr);
        } else {
            ptr->shared_count--;
        }
    }
}

srs_error_t SrsSharedPtrMessage::create(SrsCommonMessage* msg)
{
    srs_error_t err = srs_success;
    
    if ((err = create(&msg->header, msg->payload, msg->size)) != srs_success) {
        return srs_error_wrap(err, "create message");
    }
    
    // to prevent double free of payload:
    // initialize already attach the payload of msg,
    // detach the payload to transfer the owner to shared ptr.
    msg->payload = NULL;
    msg->size = 0;
    
    return err;
}

srs_error_t SrsSharedPtrMessage::create(SrsMessageHeader* pheader, char* payload, int size)
{
    srs_error_t err = srs_success;

    if (size < 0) {
        return srs_error_new(ERROR_RTMP_MESSAGE_CREATE, "create message size=%d", size);
    }

    srs_assert(!ptr);
    ptr = new SrsSharedPtrPayload();
    
    // direct attach the data.
    if (pheader) {
        ptr->header.message_type = pheader->message_type;
        ptr->header.payload_length = size;
        ptr->header.prefer_cid = pheader->prefer_cid;
        this->timestamp = pheader->timestamp;
        this->stream_id = pheader->stream_id;
    }
    ptr->payload = payload;
    ptr->size = size;
    
    // message can access it.
    this->payload = ptr->payload;
    this->size = ptr->size;
    
    return err;
}

void SrsSharedPtrMessage::wrap(char* payload, int size)
{
    srs_assert(!ptr);
    ptr = new SrsSharedPtrPayload();

    ptr->payload = payload;
    ptr->size = size;

    this->payload = ptr->payload;
    this->size = ptr->size;
}

int SrsSharedPtrMessage::count()
{
    return ptr? ptr->shared_count : 0;
}

bool SrsSharedPtrMessage::check(int stream_id)
{
    // Ignore error when message has no payload.
    if (!ptr) {
        return true;
    }

    // we donot use the complex basic header,
    // ensure the basic header is 1bytes.
    if (ptr->header.prefer_cid < 2 || ptr->header.prefer_cid > 63) {
        srs_info("change the chunk_id=%d to default=%d", ptr->header.prefer_cid, RTMP_CID_ProtocolControl);
        ptr->header.prefer_cid = RTMP_CID_ProtocolControl;
    }
    
    // we assume that the stream_id in a group must be the same.
    if (this->stream_id == stream_id) {
        return true;
    }
    this->stream_id = stream_id;
    
    return false;
}

bool SrsSharedPtrMessage::is_av()
{
    return ptr->header.message_type == RTMP_MSG_AudioMessage
    || ptr->header.message_type == RTMP_MSG_VideoMessage;
}

bool SrsSharedPtrMessage::is_audio()
{
    return ptr->header.message_type == RTMP_MSG_AudioMessage;
}

bool SrsSharedPtrMessage::is_video()
{
    return ptr->header.message_type == RTMP_MSG_VideoMessage;
}

int SrsSharedPtrMessage::chunk_header(char* cache, int nb_cache, bool c0)
{
    if (c0) {
        return srs_chunk_header_c0(ptr->header.prefer_cid, (uint32_t)timestamp,
            ptr->header.payload_length, ptr->header.message_type, stream_id, cache, nb_cache);
    } else {
        return srs_chunk_header_c3(ptr->header.prefer_cid, (uint32_t)timestamp,
            cache, nb_cache);
    }
}

SrsSharedPtrMessage* SrsSharedPtrMessage::copy()
{
    srs_assert(ptr);
    
    SrsSharedPtrMessage* copy = copy2();
    
    copy->timestamp = timestamp;
    copy->stream_id = stream_id;

    return copy;
}

SrsSharedPtrMessage* SrsSharedPtrMessage::copy2()
{
    SrsSharedPtrMessage* copy = new SrsSharedPtrMessage();

    // We got an object from cache, the ptr might exists, so unwrap it.
    //srs_assert(!copy->ptr);

    // Reference to this message instead.
    copy->ptr = ptr;
    ptr->shared_count++;

    copy->payload = ptr->payload;
    copy->size = ptr->size;

    return copy;
}

SrsFlvTransmuxer::SrsFlvTransmuxer()
{
    writer = NULL;

    drop_if_not_match_ = true;
    has_audio_ = true;
    has_video_ = true;
    nb_tag_headers = 0;
    tag_headers = NULL;
    nb_iovss_cache = 0;
    iovss_cache = NULL;
    nb_ppts = 0;
    ppts = NULL;
}

SrsFlvTransmuxer::~SrsFlvTransmuxer()
{
    srs_freepa(tag_headers);
    srs_freepa(iovss_cache);
    srs_freepa(ppts);
}

srs_error_t SrsFlvTransmuxer::initialize(ISrsWriter* fw)
{
    srs_assert(fw);
    writer = fw;
    return srs_success;
}

void SrsFlvTransmuxer::set_drop_if_not_match(bool v)
{
    drop_if_not_match_ = v;
}

bool SrsFlvTransmuxer::drop_if_not_match()
{
    return drop_if_not_match_;
}

srs_error_t SrsFlvTransmuxer::write_header(bool has_video, bool has_audio)
{
    srs_error_t err = srs_success;

    has_audio_ = has_audio;
    has_video_ = has_video;

    uint8_t av_flag = 0;
    av_flag += (has_audio? 4:0);
    av_flag += (has_video? 1:0);

    // 9bytes header and 4bytes first previous-tag-size
    char flv_header[] = {
        'F', 'L', 'V', // Signatures "FLV"
        (char)0x01, // File version (for example, 0x01 for FLV version 1)
        (char)av_flag, // 4, audio; 1, video; 5 audio+video.
        (char)0x00, (char)0x00, (char)0x00, (char)0x09 // DataOffset UI32 The length of this header in bytes
    };
    
    // flv specification should set the audio and video flag,
    // actually in practise, application generally ignore this flag,
    // so we generally set the audio/video to 0.
    
    // write 9bytes header.
    if ((err = write_header(flv_header)) != srs_success) {
        return srs_error_wrap(err, "write header");
    }
    
    return err;
}

srs_error_t SrsFlvTransmuxer::write_header(char flv_header[9])
{
    srs_error_t err = srs_success;
    
    // write data.
    if ((err = writer->write(flv_header, 9, NULL)) != srs_success) {
        return srs_error_wrap(err, "write flv header failed");
    }
    
    // previous tag size.
    char pts[] = { (char)0x00, (char)0x00, (char)0x00, (char)0x00 };
    if ((err = writer->write(pts, 4, NULL)) != srs_success) {
        return srs_error_wrap(err, "write pts");
    }
    
    return err;
}

srs_error_t SrsFlvTransmuxer::write_metadata(char type, char* data, int size)
{
    srs_error_t err = srs_success;
    
    if (size > 0) {
        cache_metadata(type, data, size, tag_header);
    }
    
    if ((err = write_tag(tag_header, sizeof(tag_header), data, size)) != srs_success) {
        return srs_error_wrap(err, "write tag");
    }
    
    return err;
}

srs_error_t SrsFlvTransmuxer::write_audio(int64_t timestamp, char* data, int size)
{
    srs_error_t err = srs_success;

    if (drop_if_not_match_ && !has_audio_) return err;
    
    if (size > 0) {
	    cache_audio(timestamp, data, size, tag_header);
    }
    
    if ((err = write_tag(tag_header, sizeof(tag_header), data, size)) != srs_success) {
        return srs_error_wrap(err, "write tag");
    }
    
    return err;
}

srs_error_t SrsFlvTransmuxer::write_video(int64_t timestamp, char* data, int size)
{
    srs_error_t err = srs_success;

    if (drop_if_not_match_ && !has_video_) return err;
    
    if (size > 0) {
	    cache_video(timestamp, data, size, tag_header);
    }
    
    if ((err = write_tag(tag_header, sizeof(tag_header), data, size)) != srs_success) {
        return srs_error_wrap(err, "write flv video tag failed");
    }
    
    return err;
}

int SrsFlvTransmuxer::size_tag(int data_size)
{
    srs_assert(data_size >= 0);
    return SRS_FLV_TAG_HEADER_SIZE + data_size + SRS_FLV_PREVIOUS_TAG_SIZE;
}

srs_error_t SrsFlvTransmuxer::write_tags(SrsSharedPtrMessage** msgs, int count)
{
    srs_error_t err = srs_success;
    
    // Do realloc the iovss if required.
    iovec* iovss = iovss_cache;
    do {
        int nn_might_iovss = 3 * count;
        if (nb_iovss_cache < nn_might_iovss) {
            srs_freepa(iovss_cache);

            nb_iovss_cache = nn_might_iovss;
            iovss = iovss_cache = new iovec[nn_might_iovss];
        }
    } while (false);
    
    // Do realloc the tag headers if required.
    char* cache = tag_headers;
    if (nb_tag_headers < count) {
        srs_freepa(tag_headers);
        
        nb_tag_headers = count;
        cache = tag_headers = new char[SRS_FLV_TAG_HEADER_SIZE * count];
    }
    
    // Do realloc the pts if required.
    char* pts = ppts;
    if (nb_ppts < count) {
        srs_freepa(ppts);
        
        nb_ppts = count;
        pts = ppts = new char[SRS_FLV_PREVIOUS_TAG_SIZE * count];
    }
    
    // Now all caches are ok, start to write all messages.
    iovec* iovs = iovss; int nn_real_iovss = 0;
    for (int i = 0; i < count; i++) {
        SrsSharedPtrMessage* msg = msgs[i];
        
        // Cache FLV packet header.
        if (msg->is_audio()) {
            if (drop_if_not_match_ && !has_audio_) continue; // Ignore audio packets if no audio stream.
            cache_audio(msg->timestamp, msg->payload, msg->size, cache);
        } else if (msg->is_video()) {
            if (drop_if_not_match_ && !has_video_) continue; // Ignore video packets if no video stream.
            cache_video(msg->timestamp, msg->payload, msg->size, cache);
        } else {
            cache_metadata(SrsFrameTypeScript, msg->payload, msg->size, cache);
        }
        
        // Cache FLV pts.
        cache_pts(SRS_FLV_TAG_HEADER_SIZE + msg->size, pts);
        
        // Set cache to iovec.
        iovs[0].iov_base = cache;
        iovs[0].iov_len = SRS_FLV_TAG_HEADER_SIZE;
        iovs[1].iov_base = msg->payload;
        iovs[1].iov_len = msg->size;
        iovs[2].iov_base = pts;
        iovs[2].iov_len = SRS_FLV_PREVIOUS_TAG_SIZE;
        
        // Move to next cache.
        cache += SRS_FLV_TAG_HEADER_SIZE;
        pts += SRS_FLV_PREVIOUS_TAG_SIZE;
        iovs += 3; nn_real_iovss += 3;
    }

    // Send out all data carried by iovec.
    if ((err = writer->writev(iovss, nn_real_iovss, NULL)) != srs_success) {
        return srs_error_wrap(err, "write flv tags failed");
    }
    
    return err;
}

void SrsFlvTransmuxer::cache_metadata(char type, char* data, int size, char* cache)
{
    srs_assert(data);
    
    // 11 bytes tag header
    /*char tag_header[] = {
     (char)type, // TagType UB [5], 18 = script data
     (char)0x00, (char)0x00, (char)0x00, // DataSize UI24 Length of the message.
     (char)0x00, (char)0x00, (char)0x00, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
     (char)0x00, // TimestampExtended UI8
     (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
     };*/

    SrsUniquePtr<SrsBuffer> tag_stream(new SrsBuffer(cache, 11));

    // write data size.
    tag_stream->write_1bytes(type);
    tag_stream->write_3bytes(size);
    tag_stream->write_3bytes(0x00);
    tag_stream->write_1bytes(0x00);
    tag_stream->write_3bytes(0x00);
}

void SrsFlvTransmuxer::cache_audio(int64_t timestamp, char* data, int size, char* cache)
{
    srs_assert(data);
    
    timestamp &= 0x7fffffff;
    
    // 11bytes tag header
    /*char tag_header[] = {
     (char)SrsFrameTypeAudio, // TagType UB [5], 8 = audio
     (char)0x00, (char)0x00, (char)0x00, // DataSize UI24 Length of the message.
     (char)0x00, (char)0x00, (char)0x00, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
     (char)0x00, // TimestampExtended UI8
     (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
     };*/

    SrsUniquePtr<SrsBuffer> tag_stream(new SrsBuffer(cache, 11));

    // write data size.
    tag_stream->write_1bytes(SrsFrameTypeAudio);
    tag_stream->write_3bytes(size);
    tag_stream->write_3bytes((int32_t)timestamp);
    // default to little-endian
    tag_stream->write_1bytes((timestamp >> 24) & 0xFF);
    tag_stream->write_3bytes(0x00);
}

void SrsFlvTransmuxer::cache_video(int64_t timestamp, char* data, int size, char* cache)
{
    srs_assert(data);
    
    timestamp &= 0x7fffffff;
    
    // 11bytes tag header
    /*char tag_header[] = {
     (char)SrsFrameTypeVideo, // TagType UB [5], 9 = video
     (char)0x00, (char)0x00, (char)0x00, // DataSize UI24 Length of the message.
     (char)0x00, (char)0x00, (char)0x00, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
     (char)0x00, // TimestampExtended UI8
     (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
     };*/

    SrsUniquePtr<SrsBuffer> tag_stream(new SrsBuffer(cache, 11));

    // write data size.
    tag_stream->write_1bytes(SrsFrameTypeVideo);
    tag_stream->write_3bytes(size);
    tag_stream->write_3bytes((int32_t)timestamp);
    // default to little-endian
    tag_stream->write_1bytes((timestamp >> 24) & 0xFF);
    tag_stream->write_3bytes(0x00);
}

void SrsFlvTransmuxer::cache_pts(int size, char* cache)
{
    SrsUniquePtr<SrsBuffer> tag_stream(new SrsBuffer(cache, 11));
    tag_stream->write_4bytes(size);
}

srs_error_t SrsFlvTransmuxer::write_tag(char* header, int header_size, char* tag, int tag_size)
{
    srs_error_t err = srs_success;
    
    // PreviousTagSizeN UI32 Size of last tag, including its header, in bytes.
    char pre_size[SRS_FLV_PREVIOUS_TAG_SIZE];
    cache_pts(tag_size + header_size, pre_size);
    
    iovec iovs[3];
    iovs[0].iov_base = header;
    iovs[0].iov_len = header_size;
    iovs[1].iov_base = tag;
    iovs[1].iov_len = tag_size;
    iovs[2].iov_base = pre_size;
    iovs[2].iov_len = SRS_FLV_PREVIOUS_TAG_SIZE;
    
    if ((err = writer->writev(iovs, 3, NULL)) != srs_success) {
        return srs_error_wrap(err, "write flv tag failed");
    }
    
    return err;
}

SrsFlvDecoder::SrsFlvDecoder()
{
    reader = NULL;
}

SrsFlvDecoder::~SrsFlvDecoder()
{
}

srs_error_t SrsFlvDecoder::initialize(ISrsReader* fr)
{
    srs_assert(fr);
    reader = fr;
    return srs_success;
}

srs_error_t SrsFlvDecoder::read_header(char header[9])
{
    srs_error_t err = srs_success;
    
    srs_assert(header);
    
    // TODO: FIXME: Should use readfully.
    if ((err = reader->read(header, 9, NULL)) != srs_success) {
        return srs_error_wrap(err, "read header");
    }
    
    char* h = header;
    if (h[0] != 'F' || h[1] != 'L' || h[2] != 'V') {
        return srs_error_new(ERROR_KERNEL_FLV_HEADER, "flv header must start with FLV");
    }
    
    return err;
}

srs_error_t SrsFlvDecoder::read_tag_header(char* ptype, int32_t* pdata_size, uint32_t* ptime)
{
    srs_error_t err = srs_success;
    
    srs_assert(ptype);
    srs_assert(pdata_size);
    srs_assert(ptime);
    
    char th[11]; // tag header
    
    // read tag header
    // TODO: FIXME: Should use readfully.
    if ((err = reader->read(th, 11, NULL)) != srs_success) {
        return srs_error_wrap(err, "read flv tag header failed");
    }
    
    // Reserved UB [2]
    // Filter UB [1]
    // TagType UB [5]
    *ptype = (th[0] & 0x1F);
    
    // DataSize UI24
    char* pp = (char*)pdata_size;
    pp[3] = 0;
    pp[2] = th[1];
    pp[1] = th[2];
    pp[0] = th[3];
    
    // Timestamp UI24
    pp = (char*)ptime;
    pp[2] = th[4];
    pp[1] = th[5];
    pp[0] = th[6];
    
    // TimestampExtended UI8
    pp[3] = th[7];
    
    return err;
}

srs_error_t SrsFlvDecoder::read_tag_data(char* data, int32_t size)
{
    srs_error_t err = srs_success;
    
    srs_assert(data);
    
    // TODO: FIXME: Should use readfully.
    if ((err = reader->read(data, size, NULL)) != srs_success) {
        return srs_error_wrap(err, "read flv tag header failed");
    }
    
    return err;
    
}

srs_error_t SrsFlvDecoder::read_previous_tag_size(char previous_tag_size[4])
{
    srs_error_t err = srs_success;
    
    srs_assert(previous_tag_size);
    
    // ignore 4bytes tag size.
    // TODO: FIXME: Should use readfully.
    if ((err = reader->read(previous_tag_size, 4, NULL)) != srs_success) {
        return srs_error_wrap(err, "read flv previous tag size failed");
    }
    
    return err;
}

SrsFlvVodStreamDecoder::SrsFlvVodStreamDecoder()
{
    reader = NULL;
}

SrsFlvVodStreamDecoder::~SrsFlvVodStreamDecoder()
{
}

srs_error_t SrsFlvVodStreamDecoder::initialize(ISrsReader* fr)
{
    srs_error_t err = srs_success;
    
    srs_assert(fr);
    reader = dynamic_cast<SrsFileReader*>(fr);
    if (!reader) {
        return srs_error_new(ERROR_EXPECT_FILE_IO, "stream is not file io");
    }
    
    if (!reader->is_open()) {
        return srs_error_new(ERROR_KERNEL_FLV_STREAM_CLOSED, "stream is not open for decoder");
    }
    
    return err;
}

srs_error_t SrsFlvVodStreamDecoder::read_header_ext(char header[13])
{
    srs_error_t err = srs_success;
    
    srs_assert(header);
    
    // @remark, always false, for sizeof(char[13]) equals to sizeof(char*)
    //srs_assert(13 == sizeof(header));
    
    // 9bytes header and 4bytes first previous-tag-size
    int size = 13;
    
    if ((err = reader->read(header, size, NULL)) != srs_success) {
        return srs_error_wrap(err, "read header");
    }
    
    return err;
}

srs_error_t SrsFlvVodStreamDecoder::read_sequence_header_summary(int64_t* pstart, int* psize)
{
    srs_error_t err = srs_success;
    
    srs_assert(pstart);
    srs_assert(psize);
    
    // simply, the first video/audio must be the sequence header.
    // and must be a sequence video and audio.
    
    // 11bytes tag header
    char tag_header[] = {
        (char)0x00, // TagType UB [5], 9 = video, 8 = audio, 18 = script data
        (char)0x00, (char)0x00, (char)0x00, // DataSize UI24 Length of the message.
        (char)0x00, (char)0x00, (char)0x00, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
        (char)0x00, // TimestampExtended UI8
        (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
    };
    
    // discovery the sequence header video and audio.
    // @remark, maybe no video or no audio.
    bool got_video = false;
    bool got_audio = false;
    // audio/video sequence and data offset.
    int64_t av_sequence_offset_start = -1;
    int64_t av_sequence_offset_end = -1;
    for (;;) {
        if ((err = reader->read(tag_header, SRS_FLV_TAG_HEADER_SIZE, NULL)) != srs_success) {
            return srs_error_wrap(err, "read tag header");
        }

        SrsUniquePtr<SrsBuffer> tag_stream(new SrsBuffer(tag_header, SRS_FLV_TAG_HEADER_SIZE));

        int8_t tag_type = tag_stream->read_1bytes();
        int32_t data_size = tag_stream->read_3bytes();
        
        bool is_video = tag_type == 0x09;
        bool is_audio = tag_type == 0x08;
        bool is_not_av = !is_video && !is_audio;
        if (is_not_av) {
            // skip body and tag size.
            reader->skip(data_size + SRS_FLV_PREVIOUS_TAG_SIZE);
            continue;
        }

        // if video duplicated, no audio
        if (is_video && got_video) {
            break;
        }
        // if audio duplicated, no video
        if (is_audio && got_audio) {
            break;
        }
        
        // video
        if (is_video) {
            srs_assert(!got_video);
            got_video = true;
            
            if (av_sequence_offset_start < 0) {
                av_sequence_offset_start = reader->tellg() - SRS_FLV_TAG_HEADER_SIZE;
            }
            av_sequence_offset_end = reader->tellg() + data_size + SRS_FLV_PREVIOUS_TAG_SIZE;
            reader->skip(data_size + SRS_FLV_PREVIOUS_TAG_SIZE);
        }
        
        // audio
        if (is_audio) {
            srs_assert(!got_audio);
            got_audio = true;

            if (av_sequence_offset_start < 0) {
                av_sequence_offset_start = reader->tellg() - SRS_FLV_TAG_HEADER_SIZE;
            }
            av_sequence_offset_end = reader->tellg() + data_size + SRS_FLV_PREVIOUS_TAG_SIZE;
            reader->skip(data_size + SRS_FLV_PREVIOUS_TAG_SIZE);
        }
        
        if (got_audio && got_video) {
            break;
        }
    }
    
    // seek to the sequence header start offset.
    if (av_sequence_offset_start > 0) {
        reader->seek2(av_sequence_offset_start);
        *pstart = av_sequence_offset_start;
        *psize = (int)(av_sequence_offset_end - av_sequence_offset_start);
    }
    
    return err;
}

srs_error_t SrsFlvVodStreamDecoder::seek2(int64_t offset)
{
    srs_error_t err = srs_success;
    
    if (offset >= reader->filesize()) {
        return srs_error_new(ERROR_SYSTEM_FILE_EOF, "flv fast decoder seek overflow file, size=%d, offset=%d", (int)reader->filesize(), (int)offset);
    }
    
    if (reader->seek2(offset) < 0) {
        return srs_error_new(ERROR_SYSTEM_FILE_SEEK, "flv fast decoder seek error, size=%d, offset=%d", (int)reader->filesize(), (int)offset);
    }
    
    return err;
}


