/*
The MIT License (MIT)

Copyright (c) 2013-2017 SRS(ossrs)

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

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
#include <srs_core_mem_watch.hpp>

SrsMessageHeader::SrsMessageHeader()
{
    message_type = 0;
    payload_length = 0;
    timestamp_delta = 0;
    stream_id = 0;
    
    timestamp = 0;
    // we always use the connection chunk-id
    perfer_cid = RTMP_CID_OverConnection;
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
    perfer_cid = RTMP_CID_OverConnection2;
}

void SrsMessageHeader::initialize_audio(int size, uint32_t time, int stream)
{
    message_type = RTMP_MSG_AudioMessage;
    payload_length = (int32_t)size;
    timestamp_delta = (int32_t)time;
    timestamp = (int64_t)time;
    stream_id = (int32_t)stream;
    
    // audio chunk-id
    perfer_cid = RTMP_CID_Audio;
}

void SrsMessageHeader::initialize_video(int size, uint32_t time, int stream)
{
    message_type = RTMP_MSG_VideoMessage;
    payload_length = (int32_t)size;
    timestamp_delta = (int32_t)time;
    timestamp = (int64_t)time;
    stream_id = (int32_t)stream;
    
    // video chunk-id
    perfer_cid = RTMP_CID_Video;
}

SrsCommonMessage::SrsCommonMessage()
{
    payload = NULL;
    size = 0;
}

SrsCommonMessage::~SrsCommonMessage()
{
#ifdef SRS_AUTO_MEM_WATCH
    srs_memory_unwatch(payload);
#endif
    srs_freepa(payload);
}

void SrsCommonMessage::create_payload(int size)
{
    srs_freepa(payload);
    
    payload = new char[size];
    srs_verbose("create payload for RTMP message. size=%d", size);
    
#ifdef SRS_AUTO_MEM_WATCH
    srs_memory_watch(payload, "RTMP.msg.payload", size);
#endif
}

int SrsCommonMessage::create(SrsMessageHeader* pheader, char* body, int size)
{
    int ret = ERROR_SUCCESS;
    
    // drop previous payload.
    srs_freepa(payload);
    
    this->header = *pheader;
    this->payload = body;
    this->size = size;
    
    return ret;
}

SrsSharedPtrMessage::SrsSharedPtrPayload::SrsSharedPtrPayload()
{
    payload = NULL;
    size = 0;
    shared_count = 0;
}

SrsSharedPtrMessage::SrsSharedPtrPayload::~SrsSharedPtrPayload()
{
#ifdef SRS_AUTO_MEM_WATCH
    srs_memory_unwatch(payload);
#endif
    srs_freepa(payload);
}

SrsSharedPtrMessage::SrsSharedPtrMessage()
{
    ptr = NULL;
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

int SrsSharedPtrMessage::create(SrsCommonMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = create(&msg->header, msg->payload, msg->size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // to prevent double free of payload:
    // initialize already attach the payload of msg,
    // detach the payload to transfer the owner to shared ptr.
    msg->payload = NULL;
    msg->size = 0;
    
    return ret;
}

int SrsSharedPtrMessage::create(SrsMessageHeader* pheader, char* payload, int size)
{
    int ret = ERROR_SUCCESS;
    
    if (ptr) {
        ret = ERROR_SYSTEM_ASSERT_FAILED;
        srs_error("should not set the payload twice. ret=%d", ret);
        srs_assert(false);
        
        return ret;
    }
    
    ptr = new SrsSharedPtrPayload();
    
    // direct attach the data.
    if (pheader) {
        ptr->header.message_type = pheader->message_type;
        ptr->header.payload_length = size;
        ptr->header.perfer_cid = pheader->perfer_cid;
        this->timestamp = pheader->timestamp;
        this->stream_id = pheader->stream_id;
    }
    ptr->payload = payload;
    ptr->size = size;
    
    // message can access it.
    this->payload = ptr->payload;
    this->size = ptr->size;
    
    return ret;
}

int SrsSharedPtrMessage::count()
{
    srs_assert(ptr);
    return ptr->shared_count;
}

bool SrsSharedPtrMessage::check(int stream_id)
{
    // we donot use the complex basic header,
    // ensure the basic header is 1bytes.
    if (ptr->header.perfer_cid < 2) {
        srs_info("change the chunk_id=%d to default=%d",
            ptr->header.perfer_cid, RTMP_CID_ProtocolControl);
        ptr->header.perfer_cid = RTMP_CID_ProtocolControl;
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
        return srs_chunk_header_c0(
            ptr->header.perfer_cid, timestamp, ptr->header.payload_length,
            ptr->header.message_type, stream_id,
            cache, nb_cache);
    } else {
        return srs_chunk_header_c3(
            ptr->header.perfer_cid, timestamp,
            cache, nb_cache);
    }
}

SrsSharedPtrMessage* SrsSharedPtrMessage::copy()
{
    srs_assert(ptr);
    
    SrsSharedPtrMessage* copy = new SrsSharedPtrMessage();
    
    copy->ptr = ptr;
    ptr->shared_count++;
    
    copy->timestamp = timestamp;
    copy->stream_id = stream_id;
    copy->payload = ptr->payload;
    copy->size = ptr->size;
    
    return copy;
}

SrsFlvEncoder::SrsFlvEncoder()
{
    writer = NULL;
    tag_stream = new SrsBuffer();
    
#ifdef SRS_PERF_FAST_FLV_ENCODER
    nb_tag_headers = 0;
    tag_headers = NULL;
    nb_iovss_cache = 0;
    iovss_cache = NULL;
    nb_ppts = 0;
    ppts = NULL;
#endif
}

SrsFlvEncoder::~SrsFlvEncoder()
{
    srs_freep(tag_stream);
    
#ifdef SRS_PERF_FAST_FLV_ENCODER
    srs_freepa(tag_headers);
    srs_freepa(iovss_cache);
    srs_freepa(ppts);
#endif
}

int SrsFlvEncoder::initialize(ISrsWriter* fw)
{
    srs_assert(fw);
    writer = fw;
    return ERROR_SUCCESS;
}

int SrsFlvEncoder::write_header()
{
    int ret = ERROR_SUCCESS;
    
    // 9bytes header and 4bytes first previous-tag-size
    char flv_header[] = {
        'F', 'L', 'V', // Signatures "FLV"
        (char)0x01, // File version (for example, 0x01 for FLV version 1)
        (char)0x05, // 4, audio; 1, video; 5 audio+video.
        (char)0x00, (char)0x00, (char)0x00, (char)0x09 // DataOffset UI32 The length of this header in bytes
    };
    
    // flv specification should set the audio and video flag,
    // actually in practise, application generally ignore this flag,
    // so we generally set the audio/video to 0.
    
    // write 9bytes header.
    if ((ret = write_header(flv_header)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsFlvEncoder::write_header(char flv_header[9])
{
    int ret = ERROR_SUCCESS;
    
    // write data.
    if ((ret = writer->write(flv_header, 9, NULL)) != ERROR_SUCCESS) {
        srs_error("write flv header failed. ret=%d", ret);
        return ret;
    }
    
    // previous tag size.
    char pts[] = { (char)0x00, (char)0x00, (char)0x00, (char)0x00 };
    if ((ret = writer->write(pts, 4, NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsFlvEncoder::write_metadata(char type, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(data);
    
    if ((ret = write_metadata_to_cache(type, data, size, tag_header)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = write_tag(tag_header, sizeof(tag_header), data, size)) != ERROR_SUCCESS) {
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("write flv data tag failed. ret=%d", ret);
        }
        return ret;
    }
    
    return ret;
}

int SrsFlvEncoder::write_audio(int64_t timestamp, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(data);
    
    if ((ret = write_audio_to_cache(timestamp, data, size, tag_header)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = write_tag(tag_header, sizeof(tag_header), data, size)) != ERROR_SUCCESS) {
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("write flv audio tag failed. ret=%d", ret);
        }
        return ret;
    }
    
    return ret;
}

int SrsFlvEncoder::write_video(int64_t timestamp, char* data, int size)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(data);
    
    if ((ret = write_video_to_cache(timestamp, data, size, tag_header)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = write_tag(tag_header, sizeof(tag_header), data, size)) != ERROR_SUCCESS) {
        srs_error("write flv video tag failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsFlvEncoder::size_tag(int data_size)
{
    srs_assert(data_size >= 0);
    return SRS_FLV_TAG_HEADER_SIZE + data_size + SRS_FLV_PREVIOUS_TAG_SIZE;
}

#ifdef SRS_PERF_FAST_FLV_ENCODER
int SrsFlvEncoder::write_tags(SrsSharedPtrMessage** msgs, int count)
{
    int ret = ERROR_SUCCESS;
    
    // realloc the iovss.
    int nb_iovss = 3 * count;
    iovec* iovss = iovss_cache;
    if (nb_iovss_cache < nb_iovss) {
        srs_freepa(iovss_cache);
        
        nb_iovss_cache = nb_iovss;
        iovss = iovss_cache = new iovec[nb_iovss];
    }
    
    // realloc the tag headers.
    char* cache = tag_headers;
    if (nb_tag_headers < count) {
        srs_freepa(tag_headers);
        
        nb_tag_headers = count;
        cache = tag_headers = new char[SRS_FLV_TAG_HEADER_SIZE * count];
    }
    
    // realloc the pts.
    char* pts = ppts;
    if (nb_ppts < count) {
        srs_freepa(ppts);
        
        nb_ppts = count;
        pts = ppts = new char[SRS_FLV_PREVIOUS_TAG_SIZE * count];
    }
    
    // the cache is ok, write each messages.
    iovec* iovs = iovss;
    for (int i = 0; i < count; i++) {
        SrsSharedPtrMessage* msg = msgs[i];
        
        // cache all flv header.
        if (msg->is_audio()) {
            if ((ret = write_audio_to_cache(msg->timestamp, msg->payload, msg->size, cache)) != ERROR_SUCCESS) {
                return ret;
            }
        } else if (msg->is_video()) {
            if ((ret = write_video_to_cache(msg->timestamp, msg->payload, msg->size, cache)) != ERROR_SUCCESS) {
                return ret;
            }
        } else {
            if ((ret = write_metadata_to_cache(SrsCodecFlvTagScript, msg->payload, msg->size, cache)) != ERROR_SUCCESS) {
                return ret;
            }
        }
        
        // cache all pts.
        if ((ret = write_pts_to_cache(SRS_FLV_TAG_HEADER_SIZE + msg->size, pts)) != ERROR_SUCCESS) {
            return ret;
        }
        
        // all ioves.
        iovs[0].iov_base = cache;
        iovs[0].iov_len = SRS_FLV_TAG_HEADER_SIZE;
        iovs[1].iov_base = msg->payload;
        iovs[1].iov_len = msg->size;
        iovs[2].iov_base = pts;
        iovs[2].iov_len = SRS_FLV_PREVIOUS_TAG_SIZE;
        
        // move next.
        cache += SRS_FLV_TAG_HEADER_SIZE;
        pts += SRS_FLV_PREVIOUS_TAG_SIZE;
        iovs += 3;
    }
    
    if ((ret = writer->writev(iovss, nb_iovss, NULL)) != ERROR_SUCCESS) {
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("write flv tags failed. ret=%d", ret);
        }
        return ret;
    }
    
    return ret;
}
#endif

int SrsFlvEncoder::write_metadata_to_cache(char type, char* data, int size, char* cache)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(data);
    
    // 11 bytes tag header
    /*char tag_header[] = {
     (char)type, // TagType UB [5], 18 = script data
     (char)0x00, (char)0x00, (char)0x00, // DataSize UI24 Length of the message.
     (char)0x00, (char)0x00, (char)0x00, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
     (char)0x00, // TimestampExtended UI8
     (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
     };*/
    
    // write data size.
    if ((ret = tag_stream->initialize(cache, 11)) != ERROR_SUCCESS) {
        return ret;
    }
    tag_stream->write_1bytes(type);
    tag_stream->write_3bytes(size);
    tag_stream->write_3bytes(0x00);
    tag_stream->write_1bytes(0x00);
    tag_stream->write_3bytes(0x00);
    
    return ret;
}

int SrsFlvEncoder::write_audio_to_cache(int64_t timestamp, char* data, int size, char* cache)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(data);
    
    timestamp &= 0x7fffffff;
    
    // 11bytes tag header
    /*char tag_header[] = {
     (char)SrsCodecFlvTagAudio, // TagType UB [5], 8 = audio
     (char)0x00, (char)0x00, (char)0x00, // DataSize UI24 Length of the message.
     (char)0x00, (char)0x00, (char)0x00, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
     (char)0x00, // TimestampExtended UI8
     (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
     };*/
    
    // write data size.
    if ((ret = tag_stream->initialize(cache, 11)) != ERROR_SUCCESS) {
        return ret;
    }
    tag_stream->write_1bytes(SrsCodecFlvTagAudio);
    tag_stream->write_3bytes(size);
    tag_stream->write_3bytes((int32_t)timestamp);
    // default to little-endian
    tag_stream->write_1bytes((timestamp >> 24) & 0xFF);
    tag_stream->write_3bytes(0x00);
    
    return ret;
}

int SrsFlvEncoder::write_video_to_cache(int64_t timestamp, char* data, int size, char* cache)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(data);
    
    timestamp &= 0x7fffffff;
    
    // 11bytes tag header
    /*char tag_header[] = {
     (char)SrsCodecFlvTagVideo, // TagType UB [5], 9 = video
     (char)0x00, (char)0x00, (char)0x00, // DataSize UI24 Length of the message.
     (char)0x00, (char)0x00, (char)0x00, // Timestamp UI24 Time in milliseconds at which the data in this tag applies.
     (char)0x00, // TimestampExtended UI8
     (char)0x00, (char)0x00, (char)0x00, // StreamID UI24 Always 0.
     };*/
    
    // write data size.
    if ((ret = tag_stream->initialize(cache, 11)) != ERROR_SUCCESS) {
        return ret;
    }
    tag_stream->write_1bytes(SrsCodecFlvTagVideo);
    tag_stream->write_3bytes(size);
    tag_stream->write_3bytes((int32_t)timestamp);
    // default to little-endian
    tag_stream->write_1bytes((timestamp >> 24) & 0xFF);
    tag_stream->write_3bytes(0x00);
    
    return ret;
}

int SrsFlvEncoder::write_pts_to_cache(int size, char* cache)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = tag_stream->initialize(cache, SRS_FLV_PREVIOUS_TAG_SIZE)) != ERROR_SUCCESS) {
        return ret;
    }
    tag_stream->write_4bytes(size);
    
    return ret;
}

int SrsFlvEncoder::write_tag(char* header, int header_size, char* tag, int tag_size)
{
    int ret = ERROR_SUCCESS;
    
    // PreviousTagSizeN UI32 Size of last tag, including its header, in bytes.
    char pre_size[SRS_FLV_PREVIOUS_TAG_SIZE];
    if ((ret = write_pts_to_cache(tag_size + header_size, pre_size)) != ERROR_SUCCESS) {
        return ret;
    }
    
    iovec iovs[3];
    iovs[0].iov_base = header;
    iovs[0].iov_len = header_size;
    iovs[1].iov_base = tag;
    iovs[1].iov_len = tag_size;
    iovs[2].iov_base = pre_size;
    iovs[2].iov_len = SRS_FLV_PREVIOUS_TAG_SIZE;
    
    if ((ret = writer->writev(iovs, 3, NULL)) != ERROR_SUCCESS) {
        if (!srs_is_client_gracefully_close(ret)) {
            srs_error("write flv tag failed. ret=%d", ret);
        }
        return ret;
    }
    
    return ret;
}

SrsFlvDecoder::SrsFlvDecoder()
{
    reader = NULL;
    tag_stream = new SrsBuffer();
}

SrsFlvDecoder::~SrsFlvDecoder()
{
    srs_freep(tag_stream);
}

int SrsFlvDecoder::initialize(ISrsReader* fr)
{
    srs_assert(fr);
    reader = fr;
    return ERROR_SUCCESS;
}

int SrsFlvDecoder::read_header(char header[9])
{
    int ret = ERROR_SUCCESS;

    srs_assert(header);
    
    if ((ret = reader->read(header, 9, NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    
    char* h = header;
    if (h[0] != 'F' || h[1] != 'L' || h[2] != 'V') {
        ret = ERROR_KERNEL_FLV_HEADER;
        srs_warn("flv header must start with FLV. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsFlvDecoder::read_tag_header(char* ptype, int32_t* pdata_size, uint32_t* ptime)
{
    int ret = ERROR_SUCCESS;

    srs_assert(ptype);
    srs_assert(pdata_size);
    srs_assert(ptime);

    char th[11]; // tag header
    
    // read tag header
    if ((ret = reader->read(th, 11, NULL)) != ERROR_SUCCESS) {
        if (ret != ERROR_SYSTEM_FILE_EOF) {
            srs_error("read flv tag header failed. ret=%d", ret);
        }
        return ret;
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

    return ret;
}

int SrsFlvDecoder::read_tag_data(char* data, int32_t size)
{
    int ret = ERROR_SUCCESS;

    srs_assert(data);
    
    if ((ret = reader->read(data, size, NULL)) != ERROR_SUCCESS) {
        if (ret != ERROR_SYSTEM_FILE_EOF) {
            srs_error("read flv tag header failed. ret=%d", ret);
        }
        return ret;
    }
    
    return ret;

}

int SrsFlvDecoder::read_previous_tag_size(char previous_tag_size[4])
{
    int ret = ERROR_SUCCESS;

    srs_assert(previous_tag_size);
    
    // ignore 4bytes tag size.
    if ((ret = reader->read(previous_tag_size, 4, NULL)) != ERROR_SUCCESS) {
        if (ret != ERROR_SYSTEM_FILE_EOF) {
            srs_error("read flv previous tag size failed. ret=%d", ret);
        }
        return ret;
    }
    
    return ret;
}

SrsFlvVodStreamDecoder::SrsFlvVodStreamDecoder()
{
    reader = NULL;
    tag_stream = new SrsBuffer();
}

SrsFlvVodStreamDecoder::~SrsFlvVodStreamDecoder()
{
    srs_freep(tag_stream);
}

int SrsFlvVodStreamDecoder::initialize(ISrsReader* fr)
{
    int ret = ERROR_SUCCESS;
    
    srs_assert(fr);
    reader = dynamic_cast<SrsFileReader*>(fr);
    if (!reader) {
        ret = ERROR_EXPECT_FILE_IO;
        srs_error("stream is not file io. ret=%d", ret);
        return ret;
    }
    
    if (!reader->is_open()) {
        ret = ERROR_KERNEL_FLV_STREAM_CLOSED;
        srs_warn("stream is not open for decoder. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsFlvVodStreamDecoder::read_header_ext(char header[13])
{
    int ret = ERROR_SUCCESS;

    srs_assert(header);
    
    // @remark, always false, for sizeof(char[13]) equals to sizeof(char*)
    //srs_assert(13 == sizeof(header));
    
    // 9bytes header and 4bytes first previous-tag-size
    int size = 13;
    
    if ((ret = reader->read(header, size, NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsFlvVodStreamDecoder::read_sequence_header_summary(int64_t* pstart, int* psize)
{
    int ret = ERROR_SUCCESS;

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
        if ((ret = reader->read(tag_header, SRS_FLV_TAG_HEADER_SIZE, NULL)) != ERROR_SUCCESS) {
            return ret;
        }
        
        if ((ret = tag_stream->initialize(tag_header, SRS_FLV_TAG_HEADER_SIZE)) != ERROR_SUCCESS) {
            return ret;
        }
        
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
    }
    
    // seek to the sequence header start offset.
    if (av_sequence_offset_start > 0) {
        reader->lseek(av_sequence_offset_start);
        *pstart = av_sequence_offset_start;
        *psize = (int)(av_sequence_offset_end - av_sequence_offset_start);
    }
    
    return ret;
}

int SrsFlvVodStreamDecoder::lseek(int64_t offset)
{
    int ret = ERROR_SUCCESS;
    
    if (offset >= reader->filesize()) {
        ret = ERROR_SYSTEM_FILE_EOF;
        srs_warn("flv fast decoder seek overflow file, "
            "size=%"PRId64", offset=%"PRId64", ret=%d", 
            reader->filesize(), offset, ret);
        return ret;
    }
    
    if (reader->lseek(offset) < 0) {
        ret = ERROR_SYSTEM_FILE_SEEK;
        srs_warn("flv fast decoder seek error, "
            "size=%"PRId64", offset=%"PRId64", ret=%d", 
            reader->filesize(), offset, ret);
        return ret;
    }
    
    return ret;
}


