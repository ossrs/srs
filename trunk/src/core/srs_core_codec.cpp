/*
The MIT License (MIT)

Copyright (c) 2013 winlin

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

#include <srs_core_codec.hpp>

#include <string.h>

#include <srs_core_error.hpp>
#include <srs_core_stream.hpp>
#include <srs_core_log.hpp>

SrsCodec::SrsCodec()
{
	width 					= 0;
	height 					= 0;
	duration 				= 0;
	frame_rate 				= 0;
	video_data_rate 		= 0;
	video_codec_id 			= 0;
	audio_data_rate 		= 0;
	audio_codec_id 			= 0;
	sound_rate		 		= 0;
	sound_size		 		= 0;
	sound_type	 			= 0;
	profile 				= 0;
	level 					= 0;
	avc_extra_size 			= 0;
	avc_extra_data 			= NULL;
	aac_extra_size 			= 0;
	aac_extra_data 			= NULL;

	stream = new SrsStream();
}

SrsCodec::~SrsCodec()
{
	srs_freepa(avc_extra_data);
	srs_freepa(aac_extra_data);

	srs_freep(stream);
}

int SrsCodec::parse_audio_codec(int8_t* data, int size)
{
	int ret = ERROR_SUCCESS;
	
	if (!data || size <= 0) {
		srs_trace("no audio present, hls ignore it.");
		return ret;
	}
	
	if ((ret = stream->initialize((char*)data, size)) != ERROR_SUCCESS) {
		return ret;
	}

	// audio decode
	if (!stream->require(1)) {
		ret = ERROR_HLS_DECODE_ERROR;
		srs_error("hls decode audio sound_format failed. ret=%d", ret);
		return ret;
	}
	
	int8_t sound_format = stream->read_1bytes();
	
	sound_type = sound_format & 0x01;
	sound_size = (sound_format >> 1) & 0x01;
	sound_rate = (sound_format >> 2) & 0x01;
	sound_format = (sound_format >> 4) & 0x0f;
	
	audio_codec_id = sound_format;
	
	// only support aac
	if (audio_codec_id != SrsCodecAudioAAC) {
		ret = ERROR_HLS_DECODE_ERROR;
		srs_error("hls only support audio aac codec. ret=%d", ret);
		return ret;
	}

	if (!stream->require(1)) {
		ret = ERROR_HLS_DECODE_ERROR;
		srs_error("hls decode audio aac_packet_type failed. ret=%d", ret);
		return ret;
	}
	
	int8_t aac_packet_type = stream->read_1bytes();
	
	if (aac_packet_type == SrsCodecAudioTypeSequenceHeader) {
		// AudioSpecificConfig
		// 1.6.2.1 AudioSpecificConfig, in aac-mp4a-format-ISO_IEC_14496-3+2001.pdf, page 33.
		aac_extra_size = size - stream->pos();
		if (aac_extra_size > 0) {
			srs_freepa(aac_extra_data);
			aac_extra_data = new char[aac_extra_size];
			memcpy(aac_extra_data, data + stream->pos(), aac_extra_size);
		}
	} else if (aac_packet_type == SrsCodecAudioTypeRawData) {
		// Raw AAC frame data in UI8 []
	} else {
		// ignored.
	}
	
	srs_info("audio decoded, type=%d, codec=%d, asize=%d, rate=%d, format=%d, size=%d", 
		sound_type, audio_codec_id, sound_size, sound_rate, sound_format, size);
	
	return ret;
}

int SrsCodec::parse_video_codec(int8_t* data, int size)
{
	int ret = ERROR_SUCCESS;
	
	if (!data || size <= 0) {
		srs_trace("no video present, hls ignore it.");
		return ret;
	}
	
	if ((ret = stream->initialize((char*)data, size)) != ERROR_SUCCESS) {
		return ret;
	}

	// video decode
	if (!stream->require(1)) {
		ret = ERROR_HLS_DECODE_ERROR;
		srs_error("hls decode video frame_type failed. ret=%d", ret);
		return ret;
	}
	
	int8_t frame_type = stream->read_1bytes();
	int8_t codec_id = frame_type & 0x0f;
	frame_type = (frame_type >> 4) & 0x0f;
	
	video_codec_id = codec_id;
	// only support h.264/avc
	if (codec_id != SrsCodecVideoAVC) {
		ret = ERROR_HLS_DECODE_ERROR;
		srs_error("hls only support video h.264/avc codec. ret=%d", ret);
		return ret;
	}
	
	if (!stream->require(4)) {
		ret = ERROR_HLS_DECODE_ERROR;
		srs_error("hls decode video avc_packet_type failed. ret=%d", ret);
		return ret;
	}
	int8_t avc_packet_type = stream->read_1bytes();
	int32_t composition_time = stream->read_3bytes();
	
	// avoid warning, used it future.
	(void)composition_time;
	
	if (avc_packet_type == SrsCodecVideoAVCTypeSequenceHeader) {
		// AVCDecoderConfigurationRecord
		// 5.2.4.1.1 Syntax, H.264-AVC-ISO_IEC_14496-15.pdf, page 16
		avc_extra_size = size - stream->pos();
		if (avc_extra_size > 0) {
			srs_freepa(avc_extra_data);
			avc_extra_data = new char[avc_extra_size];
			memcpy(avc_extra_data, data + stream->pos(), avc_extra_size);
		}
	} else if (avc_packet_type == SrsCodecVideoAVCTypeNALU){
		// One or more NALUs (Full frames are required)
		// 5.3.4.2.1 Syntax, H.264-AVC-ISO_IEC_14496-15.pdf, page 20
	} else {
		// ignored.
	}
	
	srs_info("video decoded, type=%d, codec=%d, avc=%d, time=%d, size=%d", 
		frame_type, video_codec_id, avc_packet_type, composition_time, size);
	
	return ret;
}

bool SrsCodec::video_is_keyframe(int8_t* data, int size)
{
	// 2bytes required.
	if (size < 1) {
		return false;
	}

	char frame_type = *(char*)data;
	frame_type = (frame_type >> 4) & 0x0F;
	
	return frame_type == SrsCodecVideoAVCFrameKeyFrame;
}

bool SrsCodec::video_is_sequence_header(int8_t* data, int size)
{
	// sequence header only for h264
	if (!video_is_h264(data, size)) {
		return false;
	}
	
	// 2bytes required.
	if (size < 2) {
		return false;
	}

	char frame_type = *(char*)data;
	frame_type = (frame_type >> 4) & 0x0F;

	char avc_packet_type = *(char*)(data + 1);
	
	return frame_type == SrsCodecVideoAVCFrameKeyFrame 
		&& avc_packet_type == SrsCodecVideoAVCTypeSequenceHeader;
}

bool SrsCodec::audio_is_sequence_header(int8_t* data, int size)
{
	// sequence header only for aac
	if (!audio_is_aac(data, size)) {
		return false;
	}
	
	// 2bytes required.
	if (size < 2) {
		return false;
	}
	
	char aac_packet_type = *(char*)(data + 1);
	
	return aac_packet_type == SrsCodecAudioTypeSequenceHeader;
}

bool SrsCodec::video_is_h264(int8_t* data, int size)
{
	// 1bytes required.
	if (size < 1) {
		return false;
	}

	char codec_id = *(char*)data;
	codec_id = codec_id & 0x0F;
	
	return codec_id == SrsCodecVideoAVC;
}

bool SrsCodec::audio_is_aac(int8_t* data, int size)
{
	// 1bytes required.
	if (size < 1) {
		return false;
	}
	
	char sound_format = *(char*)data;
	sound_format = (sound_format >> 4) & 0x0F;
	
	return sound_format == SrsCodecAudioAAC;
}
