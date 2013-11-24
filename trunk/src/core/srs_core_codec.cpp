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

#include <srs_core_error.hpp>
#include <srs_core_stream.hpp>

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
	aac_sample_rate	 		= 0;
	sample_rate		 		= 0;
	sample_size		 		= 0;
	audio_channels	 		= 0;
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

int SrsCodec::parse_av_codec(bool is_video, int8_t* data, int size)
{
	int ret = ERROR_SUCCESS;
	
	if (!data || size <= 0) {
		return ret;
	}
	
	if ((ret = stream->initialize((char*)data, size)) != ERROR_SUCCESS) {
		return ret;
	}
	
	if (is_video) {
		if (!stream->require(1)) {
			return ret;
		}
		
		int8_t frame_type = stream->read_1bytes();
		int8_t codec_id = frame_type & 0x0f;
		frame_type = (frame_type >> 4) & 0x0f;
		
		video_codec_id = codec_id;
		if (codec_id != SrsCodecVideoAVC) {
			return ret;
		}
		
		if (!stream->require(4)) {
			return ret;
		}
		int8_t avc_packet_type = stream->read_1bytes();
		int32_t composition_time = stream->read_3bytes();
		
		// 5.2.4.1.1 Syntax
		if (avc_packet_type == SrsCodecVideoAVCTypeSequenceHeader) {
		}
	} else {
	}
	
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
