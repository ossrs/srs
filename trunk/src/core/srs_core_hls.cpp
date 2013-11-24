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

#include <srs_core_hls.hpp>

#include <srs_core_error.hpp>
#include <srs_core_codec.hpp>
#include <srs_core_amf0.hpp>
#include <srs_core_protocol.hpp>

SrsHLS::SrsHLS()
{
	codec = new SrsCodec();
	sample = new SrsCodecSample();
}

SrsHLS::~SrsHLS()
{
	srs_freep(codec);
	srs_freep(sample);
}

int SrsHLS::on_publish()
{
	int ret = ERROR_SUCCESS;
	return ret;
}

void SrsHLS::on_unpublish()
{
}

int SrsHLS::on_meta_data(SrsOnMetaDataPacket* metadata)
{
	int ret = ERROR_SUCCESS;

	if (!metadata || !metadata->metadata) {
		srs_trace("no metadata persent, hls ignored it.");
		return ret;
	}
	
	SrsAmf0Object* obj = metadata->metadata;
	if (obj->size() <= 0) {
		srs_trace("no metadata persent, hls ignored it.");
		return ret;
	}
	
	//	finger out the codec info from metadata if possible.
	SrsAmf0Any* prop = NULL;

	if ((prop = obj->get_property("duration")) != NULL && prop->is_number()) {
		codec->duration = (int)srs_amf0_convert<SrsAmf0Number>(prop)->value;
	}
	if ((prop = obj->get_property("width")) != NULL && prop->is_number()) {
		codec->width = (int)srs_amf0_convert<SrsAmf0Number>(prop)->value;
	}
	if ((prop = obj->get_property("height")) != NULL && prop->is_number()) {
		codec->height = (int)srs_amf0_convert<SrsAmf0Number>(prop)->value;
	}
	if ((prop = obj->get_property("framerate")) != NULL && prop->is_number()) {
		codec->frame_rate = (int)srs_amf0_convert<SrsAmf0Number>(prop)->value;
	}
	if ((prop = obj->get_property("videocodecid")) != NULL && prop->is_number()) {
		codec->video_codec_id = (int)srs_amf0_convert<SrsAmf0Number>(prop)->value;
	}
	if ((prop = obj->get_property("videodatarate")) != NULL && prop->is_number()) {
		codec->video_data_rate = (int)(1000 * srs_amf0_convert<SrsAmf0Number>(prop)->value);
	}
	
	if ((prop = obj->get_property("audiocodecid")) != NULL && prop->is_number()) {
		codec->audio_codec_id = (int)srs_amf0_convert<SrsAmf0Number>(prop)->value;
	}
	if ((prop = obj->get_property("audiodatarate")) != NULL && prop->is_number()) {
		codec->audio_data_rate = (int)(1000 * srs_amf0_convert<SrsAmf0Number>(prop)->value);
	}
	if ((prop = obj->get_property("audiosamplerate")) != NULL && prop->is_number()) {
		int sound_rate = (int)srs_amf0_convert<SrsAmf0Number>(prop)->value;
		if (sound_rate == 5512) {
			codec->sound_rate = SrsCodecAudioSampleRate5512;
		} else if (sound_rate == 11025) {
			codec->sound_rate = SrsCodecAudioSampleRate11025;
		} else if (sound_rate == 22050) {
			codec->sound_rate = SrsCodecAudioSampleRate22050;
		} else if (sound_rate == 44100) {
			codec->sound_rate = SrsCodecAudioSampleRate44100;
		} else {
			ret = ERROR_HLS_METADATA;
			srs_error("invalid sound_rate of metadata: %d, ret=%d", sound_rate, ret);
			return ret;
		}
	}
	if ((prop = obj->get_property("audiosamplesize")) != NULL && prop->is_number()) {
		int sound_size = (int)srs_amf0_convert<SrsAmf0Number>(prop)->value;
		if (sound_size == 16) {
			codec->sound_size = SrsCodecAudioSampleSize16bit;
		} else if (sound_size == 8) {
			codec->sound_size = SrsCodecAudioSampleSize8bit;
		} else {
			ret = ERROR_HLS_METADATA;
			srs_error("invalid sound_size of metadata: %d, ret=%d", sound_size, ret);
			return ret;
		}
	}
	if ((prop = obj->get_property("stereo")) != NULL && prop->is_number()) {
		if (srs_amf0_convert<SrsAmf0Boolean>(prop)->value) {
			codec->sound_type = SrsCodecAudioSoundTypeStereo;
		} else {
			codec->sound_type = SrsCodecAudioSoundTypeMono;
		}
	}
	
	return ret;
}

int SrsHLS::on_audio(SrsCommonMessage* audio)
{
	int ret = ERROR_SUCCESS;
	
	sample->clear();
	if ((ret = codec->audio_aac_demux(audio->payload, audio->size, sample)) != ERROR_SUCCESS) {
		return ret;
	}
	
	if (codec->audio_codec_id != SrsCodecAudioAAC) {
		return ret;
	}
	
	return ret;
}

int SrsHLS::on_video(SrsCommonMessage* video)
{
	int ret = ERROR_SUCCESS;
	
	sample->clear();
	if ((ret = codec->video_avc_demux(video->payload, video->size, sample)) != ERROR_SUCCESS) {
		return ret;
	}
	
	if (codec->video_codec_id != SrsCodecVideoAVC) {
		return ret;
	}
	
	return ret;
}

