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

SrsCodec::SrsCodec()
{
}

SrsCodec::~SrsCodec()
{
}

bool SrsCodec::video_is_sequence_header(int8_t* data, int size)
{
	// E.4.3.1 VIDEODATA
	// Frame Type UB [4]
	// Type of video frame. The following values are defined:
	// 	1 = key frame (for AVC, a seekable frame)
	// 	2 = inter frame (for AVC, a non-seekable frame)
	// 	3 = disposable inter frame (H.263 only)
	// 	4 = generated key frame (reserved for server use only)
	// 	5 = video info/command frame
	//
	// AVCPacketType IF CodecID == 7 UI8
	// The following values are defined:
	// 	0 = AVC sequence header
	// 	1 = AVC NALU
	// 	2 = AVC end of sequence (lower level NALU sequence ender is
	// 		not required or supported)
	
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
	
	return frame_type == 1 && avc_packet_type == 0;
}

bool SrsCodec::audio_is_sequence_header(int8_t* data, int size)
{
	// AACPacketType IF SoundFormat == 10 UI8
	// The following values are defined:
	// 	0 = AAC sequence header
	// 	1 = AAC raw
	
	// sequence header only for aac
	if (!audio_is_aac(data, size)) {
		return false;
	}
	
	// 2bytes required.
	if (size < 2) {
		return false;
	}
	
	char aac_packet_type = *(char*)(data + 1);
	
	return aac_packet_type == 0;
}

bool SrsCodec::video_is_h264(int8_t* data, int size)
{
	// E.4.3.1 VIDEODATA
	// CodecID UB [4]
	// Codec Identifier. The following values are defined:
	// 	2 = Sorenson H.263
	// 	3 = Screen video
	// 	4 = On2 VP6
	// 	5 = On2 VP6 with alpha channel
	// 	6 = Screen video version 2
	// 	7 = AVC
	
	// 1bytes required.
	if (size < 1) {
		return false;
	}

	char codec_id = *(char*)data;
	codec_id = codec_id & 0x0F;
	
	return codec_id == 7;
}

bool SrsCodec::audio_is_aac(int8_t* data, int size)
{
	// SoundFormat UB [4] 
	// Format of SoundData. The following values are defined:
	// 	0 = Linear PCM, platform endian
	// 	1 = ADPCM
	// 	2 = MP3
	// 	3 = Linear PCM, little endian
	// 	4 = Nellymoser 16 kHz mono
	// 	5 = Nellymoser 8 kHz mono
	// 	6 = Nellymoser
	// 	7 = G.711 A-law logarithmic PCM
	// 	8 = G.711 mu-law logarithmic PCM
	// 	9 = reserved
	// 	10 = AAC
	// 	11 = Speex
	// 	14 = MP3 8 kHz
	// 	15 = Device-specific sound
	// Formats 7, 8, 14, and 15 are reserved.
	// AAC is supported in Flash Player 9,0,115,0 and higher.
	// Speex is supported in Flash Player 10 and higher.
	
	// 1bytes required.
	if (size < 1) {
		return false;
	}
	
	char sound_format = *(char*)data;
	sound_format = (sound_format >> 4) & 0x0F;
	
	return sound_format == 10;
}
