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
}

SrsHLS::~SrsHLS()
{
	srs_freep(codec);
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
		return ret;
	}
	
	SrsAmf0Object* obj = metadata->metadata;
	if (obj->size() <= 0) {
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
		codec->sample_rate = (int)srs_amf0_convert<SrsAmf0Number>(prop)->value;
	}
	if ((prop = obj->get_property("audiosamplesize")) != NULL && prop->is_number()) {
		codec->sample_size = (int)srs_amf0_convert<SrsAmf0Number>(prop)->value;
		if (codec->sample_size == 16) {
			codec->sample_size = 2;
		} else {
			codec->sample_size = 1;
		}
	}
	if ((prop = obj->get_property("stereo")) != NULL && prop->is_number()) {
		if (srs_amf0_convert<SrsAmf0Boolean>(prop)->value) {
			codec->audio_channels = 2;
		} else {
			codec->audio_channels = 1;
		}
	}
	
	return ret;
}

int SrsHLS::on_audio(SrsCommonMessage* audio)
{
	int ret = ERROR_SUCCESS;
	
	if ((ret = codec->parse_av_codec(false, audio->payload, audio->size)) != ERROR_SUCCESS) {
		return ret;
	}
	
	return ret;
}

int SrsHLS::on_video(SrsCommonMessage* video)
{
	int ret = ERROR_SUCCESS;
	
	if ((ret = codec->parse_av_codec(true, video->payload, video->size)) != ERROR_SUCCESS) {
		return ret;
	}
	
	if (codec->video_codec_id != SrsCodecVideoAVC) {
		return ret;
	}
	
	return ret;
}

