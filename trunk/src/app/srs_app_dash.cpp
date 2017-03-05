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

#include <srs_app_dash.hpp>

#include <srs_kernel_error.hpp>
#include <srs_app_source.hpp>
#include <srs_app_config.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_file.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_mp4.hpp>

#include <sstream>
using namespace std;

SrsFragmentedMp4::SrsFragmentedMp4()
{
}

SrsFragmentedMp4::~SrsFragmentedMp4()
{
}

SrsMpdWriter::SrsMpdWriter()
{
    req = NULL;
    timeshit = update_period = fragment = 0;
    last_update_mpd = -1;
}

SrsMpdWriter::~SrsMpdWriter()
{
}

int SrsMpdWriter::initialize(SrsRequest* r)
{
    int ret = ERROR_SUCCESS;
    
    req = r;
    fragment = _srs_config->get_dash_fragment(r->vhost);
    update_period = _srs_config->get_dash_update_period(r->vhost);
    timeshit = _srs_config->get_dash_timeshift(r->vhost);
    home = _srs_config->get_dash_path(r->vhost);
    mpd_file = _srs_config->get_dash_mpd_file(r->vhost);
    
    return ret;
}

int SrsMpdWriter::write(SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    // MPD is not expired?
    if (last_update_mpd != -1 && srs_get_system_time_ms() - last_update_mpd < update_period) {
        return ret;
    }
    last_update_mpd = srs_get_system_time_ms();
    
    string mpd_path = srs_path_build_stream(mpd_file, req->vhost, req->app, req->stream);
    string full_path = home + "/" + mpd_path;
    string full_home = srs_path_dirname(full_path);
    
    if ((ret = srs_create_dir_recursively(full_home)) != ERROR_SUCCESS) {
        srs_error("DASH: Create MPD home failed, home=%s, ret=%d", full_home.c_str(), ret);
        return ret;
    }
    
    stringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"utf-8\"?>" << endl
        << "<MPD profiles=\"urn:mpeg:dash:profile:isoff-live:2011,http://dashif.org/guidelines/dash-if-simple\" " << endl
        << "    ns1:schemaLocation=\"urn:mpeg:dash:schema:mpd:2011 DASH-MPD.xsd\" " << endl
        << "    xmlns=\"urn:mpeg:dash:schema:mpd:2011\" xmlns:ns1=\"http://www.w3.org/2001/XMLSchema-instance\" " << endl
        << "    type=\"dynamic\" minimumUpdatePeriod=\"PT" << update_period / 1000 << "S\" " << endl
        << "    timeShiftBufferDepth=\"PT" << timeshit / 1000 << "S\" availabilityStartTime=\"1970-01-01T00:00:00Z\" " << endl
        << "    maxSegmentDuration=\"PT" << fragment / 1000 << "S\" minBufferTime=\"PT" << fragment / 1000 << "S\" >" << endl
        << "    <BaseURL>" << req->stream << "/" << "</BaseURL>" << endl
        << "    <Period start=\"PT0S\">" << endl;
    if (format->acodec) {
        ss  << "        <AdaptationSet mimeType=\"audio/mp4\" segmentAlignment=\"true\" startWithSAP=\"1\">" << endl;
        ss  << "            <SegmentTemplate duration=\"" << fragment / 1000 << "\" "
                                << "initialization=\"$RepresentationID$-init.mp4\" "
                                << "media=\"$RepresentationID$-$Number$.m4s\" />" << endl;
        ss  << "            <Representation id=\"audio\" bandwidth=\"48000\" codecs=\"mp4a.40.2\" />" << endl;
        ss  << "        </AdaptationSet>" << endl;
    }
    if (format->vcodec) {
        int w = format->vcodec->width;
        int h = format->vcodec->height;
        ss  << "        <AdaptationSet mimeType=\"video/mp4\" segmentAlignment=\"true\" startWithSAP=\"1\">" << endl;
        ss  << "            <SegmentTemplate duration=\"" << fragment / 1000 << "\" "
                                << "initialization=\"$RepresentationID$-init.mp4\" "
                                << "media=\"$RepresentationID$-$Number$.m4s\" />" << endl;
        ss  << "            <Representation id=\"video\" bandwidth=\"800000\" codecs=\"avc1.64001e\" "
                                << "width=\"" << w << "\" height=\"" << h << "\"/>" << endl;
        ss  << "        </AdaptationSet>" << endl;
    }
    ss  << "    </Period>" << endl
        << "</MPD>" << endl;
    
    SrsFileWriter* fw = new SrsFileWriter();
    SrsAutoFree(SrsFileWriter, fw);
    
    string full_path_tmp = full_path + ".tmp";
    if ((ret = fw->open(full_path_tmp)) != ERROR_SUCCESS) {
        srs_error("DASH: Open MPD file=%s failed, ret=%d", full_path_tmp.c_str(), ret);
        return ret;
    }
    
    string content = ss.str();
    if ((ret = fw->write((void*)content.data(), content.length(), NULL)) != ERROR_SUCCESS) {
        srs_error("DASH: Write MPD file=%s failed, ret=%d", full_path.c_str(), ret);
        return ret;
    }
    
    if (::rename(full_path_tmp.c_str(), full_path.c_str()) < 0) {
        ret = ERROR_DASH_WRITE_FAILED;
        srs_error("DASH: Rename %s to %s failed, ret=%d", full_path_tmp.c_str(), full_path.c_str(), ret);
        return ret;
    }
    
    srs_trace("DASH: Refresh MPD success, size=%dB, file=%s", content.length(), full_path.c_str());
    
    return ret;
}

SrsDashController::SrsDashController()
{
    req = NULL;
    video_tack_id = 2;
    audio_track_id = 1;
    mpd = new SrsMpdWriter();
}

SrsDashController::~SrsDashController()
{
    srs_freep(mpd);
}

int SrsDashController::initialize(SrsRequest* r)
{
    int ret = ERROR_SUCCESS;
    
    req = r;
    home = _srs_config->get_dash_path(r->vhost);
    
    if ((ret = mpd->initialize(r)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDashController::on_audio(SrsSharedPtrMessage* shared_audio, SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    if (format->is_aac_sequence_header()) {
        return refresh_init_mp4(shared_audio, format);
    }
    
    if ((ret = refresh_mpd(format)) != ERROR_SUCCESS) {
        srs_error("DASH: Refresh the MPD failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsDashController::on_video(SrsSharedPtrMessage* shared_video, SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    if (format->is_avc_sequence_header()) {
        return refresh_init_mp4(shared_video, format);
    }
    
    if ((ret = refresh_mpd(format)) != ERROR_SUCCESS) {
        srs_error("DASH: Refresh the MPD failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsDashController::refresh_mpd(SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    // TODO: FIXME: Support pure audio streaming.
    if (!format->acodec || !format->vcodec) {
        return ret;
    }
    
    if ((ret = mpd->write(format)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsDashController::refresh_init_mp4(SrsSharedPtrMessage* msg, SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    if (msg->size <= 0 || (msg->is_video() && !format->vcodec->avc_extra_size)
        || (msg->is_audio() && !format->acodec->aac_extra_size)) {
        srs_warn("DASH: Ignore empty sequence header.");
        return ret;
    }
    
    string full_home = home + "/" + req->app + "/" + req->stream;
    if ((ret = srs_create_dir_recursively(full_home)) != ERROR_SUCCESS) {
        srs_error("DASH: Create media home failed, home=%s, ret=%d", full_home.c_str(), ret);
        return ret;
    }
    
    std::string path = full_home;
    if (msg->is_video()) {
        path += "/video-init.mp4";
    } else {
        path += "/audio-init.mp4";
    }
    
    SrsFileWriter* fw = new SrsFileWriter();
    SrsAutoFree(SrsFileWriter, fw);
    
    string path_tmp = path + ".tmp";
    if ((ret = fw->open(path_tmp)) != ERROR_SUCCESS) {
        srs_error("DASH: Open media failed, path=%s, ret=%d", path_tmp.c_str(), ret);
        return ret;
    }
    
    // Write ftyp box.
    SrsMp4FileTypeBox* ftyp = new SrsMp4FileTypeBox();
    SrsAutoFree(SrsMp4FileTypeBox, ftyp);
    if (true) {
        ftyp->major_brand = SrsMp4BoxBrandISO5;
        ftyp->minor_version = 0;
        ftyp->set_compatible_brands(SrsMp4BoxBrandISOM, SrsMp4BoxBrandISO5, SrsMp4BoxBrandDASH, SrsMp4BoxBrandMP42);
    }
    
    // Write moov.
    SrsMp4MovieBox* moov = new SrsMp4MovieBox();
    SrsAutoFree(SrsMp4MovieBox, moov);
    if (true) {
        SrsMp4MovieHeaderBox* mvhd = new SrsMp4MovieHeaderBox();
        moov->set_mvhd(mvhd);
        
        mvhd->timescale = 1000; // Use tbn ms.
        mvhd->duration_in_tbn = 0;
        mvhd->next_track_ID = (msg->is_video()? video_tack_id : audio_track_id);
        
        if (msg->is_video()) {
            SrsMp4TrackBox* trak = new SrsMp4TrackBox();
            moov->add_trak(trak);
            
            SrsMp4TrackHeaderBox* tkhd = new SrsMp4TrackHeaderBox();
            trak->set_tkhd(tkhd);
            
            tkhd->track_ID = mvhd->next_track_ID++;
            tkhd->duration = 0;
            tkhd->width = (format->vcodec->width << 16);
            tkhd->height = (format->vcodec->height << 16);
            
            SrsMp4MediaBox* mdia = new SrsMp4MediaBox();
            trak->set_mdia(mdia);
            
            SrsMp4MediaHeaderBox* mdhd = new SrsMp4MediaHeaderBox();
            mdia->set_mdhd(mdhd);
            
            mdhd->timescale = 1000;
            mdhd->duration = 0;
            mdhd->set_language0('u');
            mdhd->set_language1('n');
            mdhd->set_language2('d');
            
            SrsMp4HandlerReferenceBox* hdlr = new SrsMp4HandlerReferenceBox();
            mdia->set_hdlr(hdlr);
            
            hdlr->handler_type = SrsMp4HandlerTypeVIDE;
            hdlr->name = "VideoHandler";
            
            SrsMp4MediaInformationBox* minf = new SrsMp4MediaInformationBox();
            mdia->set_minf(minf);
            
            SrsMp4VideoMeidaHeaderBox* vmhd = new SrsMp4VideoMeidaHeaderBox();
            minf->set_vmhd(vmhd);
            
            SrsMp4DataInformationBox* dinf = new SrsMp4DataInformationBox();
            minf->set_dinf(dinf);
            
            SrsMp4DataReferenceBox* dref = new SrsMp4DataReferenceBox();
            dinf->set_dref(dref);
            
            SrsMp4DataEntryBox* url = new SrsMp4DataEntryUrlBox();
            dref->append(url);
            
            SrsMp4SampleTableBox* stbl = new SrsMp4SampleTableBox();
            minf->set_stbl(stbl);
            
            SrsMp4SampleDescriptionBox* stsd = new SrsMp4SampleDescriptionBox();
            stbl->set_stsd(stsd);
            
            SrsMp4VisualSampleEntry* avc1 = new SrsMp4VisualSampleEntry();
            stsd->append(avc1);
            
            avc1->width = format->vcodec->width;
            avc1->height = format->vcodec->height;
            
            SrsMp4AvccBox* avcC = new SrsMp4AvccBox();
            avc1->set_avcC(avcC);
            
            avcC->nb_config = format->vcodec->avc_extra_size;
            avcC->avc_config = new uint8_t[format->vcodec->avc_extra_size];
            memcpy(avcC->avc_config, format->vcodec->avc_extra_data, format->vcodec->avc_extra_size);
            
            SrsMp4DecodingTime2SampleBox* stts = new SrsMp4DecodingTime2SampleBox();
            stbl->set_stts(stts);
            
            SrsMp4Sample2ChunkBox* stsc = new SrsMp4Sample2ChunkBox();
            stbl->set_stsc(stsc);
            
            SrsMp4SampleSizeBox* stsz = new SrsMp4SampleSizeBox();
            stbl->set_stsz(stsz);
            
            SrsMp4ChunkOffsetBox* stco = new SrsMp4ChunkOffsetBox();
            stbl->set_stco(stco);
            
            SrsMp4MovieExtendsBox* mvex = new SrsMp4MovieExtendsBox();
            moov->set_mvex(mvex);
            
            SrsMp4TrackExtendsBox* trex = new SrsMp4TrackExtendsBox();
            mvex->set_trex(trex);
            
            trex->track_ID = video_tack_id;
            trex->default_sample_description_index = 1;
        } else {
            SrsMp4TrackBox* trak = new SrsMp4TrackBox();
            moov->add_trak(trak);
            
            SrsMp4TrackHeaderBox* tkhd = new SrsMp4TrackHeaderBox();
            tkhd->volume = 0x0100;
            trak->set_tkhd(tkhd);
            
            tkhd->track_ID = mvhd->next_track_ID++;
            tkhd->duration = 0;
            
            SrsMp4MediaBox* mdia = new SrsMp4MediaBox();
            trak->set_mdia(mdia);
            
            SrsMp4MediaHeaderBox* mdhd = new SrsMp4MediaHeaderBox();
            mdia->set_mdhd(mdhd);
            
            mdhd->timescale = 1000;
            mdhd->duration = 0;
            mdhd->set_language0('u');
            mdhd->set_language1('n');
            mdhd->set_language2('d');
            
            SrsMp4HandlerReferenceBox* hdlr = new SrsMp4HandlerReferenceBox();
            mdia->set_hdlr(hdlr);
            
            hdlr->handler_type = SrsMp4HandlerTypeSOUN;
            hdlr->name = "SoundHandler";
            
            SrsMp4MediaInformationBox* minf = new SrsMp4MediaInformationBox();
            mdia->set_minf(minf);
            
            SrsMp4SoundMeidaHeaderBox* smhd = new SrsMp4SoundMeidaHeaderBox();
            minf->set_smhd(smhd);
            
            SrsMp4DataInformationBox* dinf = new SrsMp4DataInformationBox();
            minf->set_dinf(dinf);
            
            SrsMp4DataReferenceBox* dref = new SrsMp4DataReferenceBox();
            dinf->set_dref(dref);
            
            SrsMp4DataEntryBox* url = new SrsMp4DataEntryUrlBox();
            dref->append(url);
            
            SrsMp4SampleTableBox* stbl = new SrsMp4SampleTableBox();
            minf->set_stbl(stbl);
            
            SrsMp4SampleDescriptionBox* stsd = new SrsMp4SampleDescriptionBox();
            stbl->set_stsd(stsd);
            
            SrsMp4AudioSampleEntry* mp4a = new SrsMp4AudioSampleEntry();
            mp4a->samplerate = uint32_t(srs_flv_srates[format->acodec->sound_rate]) << 16;
            if (format->acodec->sound_size == SrsAudioSampleBits16bit) {
                mp4a->samplesize = 16;
            } else {
                mp4a->samplesize = 8;
            }
            if (format->acodec->sound_type == SrsAudioChannelsStereo) {
                mp4a->channelcount = 2;
            } else {
                mp4a->channelcount = 1;
            }
            stsd->append(mp4a);
            
            SrsMp4EsdsBox* esds = new SrsMp4EsdsBox();
            mp4a->set_esds(esds);
            
            SrsMp4ES_Descriptor* es = esds->es;
            es->ES_ID = 0x02;
            
            SrsMp4DecoderConfigDescriptor& desc = es->decConfigDescr;
            desc.objectTypeIndication = SrsMp4ObjectTypeAac;
            desc.streamType = SrsMp4StreamTypeAudioStream;
            srs_freep(desc.decSpecificInfo);
            
            SrsMp4DecoderSpecificInfo* asc = new SrsMp4DecoderSpecificInfo();
            desc.decSpecificInfo = asc;
            asc->nb_asc = format->acodec->aac_extra_size;
            asc->asc = new uint8_t[format->acodec->aac_extra_size];
            memcpy(asc->asc, format->acodec->aac_extra_data, format->acodec->aac_extra_size);
            
            SrsMp4DecodingTime2SampleBox* stts = new SrsMp4DecodingTime2SampleBox();
            stbl->set_stts(stts);
            
            SrsMp4Sample2ChunkBox* stsc = new SrsMp4Sample2ChunkBox();
            stbl->set_stsc(stsc);
            
            SrsMp4SampleSizeBox* stsz = new SrsMp4SampleSizeBox();
            stbl->set_stsz(stsz);
            
            SrsMp4ChunkOffsetBox* stco = new SrsMp4ChunkOffsetBox();
            stbl->set_stco(stco);
            
            SrsMp4MovieExtendsBox* mvex = new SrsMp4MovieExtendsBox();
            moov->set_mvex(mvex);
            
            SrsMp4TrackExtendsBox* trex = new SrsMp4TrackExtendsBox();
            mvex->set_trex(trex);
            
            trex->track_ID = audio_track_id;
            trex->default_sample_description_index = 1;
        }
    }
    
    int nb_data = ftyp->nb_bytes() + moov->nb_bytes();
    uint8_t* data = new uint8_t[nb_data];
    SrsAutoFreeA(uint8_t, data);
    
    SrsBuffer* buffer = new SrsBuffer();
    SrsAutoFree(SrsBuffer, buffer);
    if ((ret = buffer->initialize((char*)data, nb_data)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = ftyp->encode(buffer)) != ERROR_SUCCESS) {
        return ret;
    }
    if ((ret = moov->encode(buffer)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = fw->write(data, nb_data, NULL)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if (::rename(path_tmp.c_str(), path.c_str()) < 0) {
        ret = ERROR_DASH_WRITE_FAILED;
        srs_error("DASH: Rename %s to %s failed, ret=%d", path_tmp.c_str(), path.c_str(), ret);
        return ret;
    }
    
    srs_trace("DASH: Refresh media success, file=%s", path.c_str());
    
    return ret;
}

SrsDash::SrsDash()
{
    hub = NULL;
    req = NULL;
    controller = new SrsDashController();
    
    enabled = false;
}

SrsDash::~SrsDash()
{
    srs_freep(controller);
}

int SrsDash::initialize(SrsOriginHub* h, SrsRequest* r)
{
    int ret = ERROR_SUCCESS;
    
    hub = h;
    req = r;
    
    if ((ret = controller->initialize(req)) != ERROR_SUCCESS) {
        srs_error("DASH: Initialize controller failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsDash::on_publish()
{
    int ret = ERROR_SUCCESS;
    
    // Prevent duplicated publish.
    if (enabled) {
        return ret;
    }
    
    if (!_srs_config->get_dash_enabled(req->vhost)) {
        return ret;
    }
    enabled = true;
    
    return ret;
}

int SrsDash::on_audio(SrsSharedPtrMessage* shared_audio, SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    if (!enabled) {
        return ret;
    }
    
    if ((ret = controller->on_audio(shared_audio, format)) != ERROR_SUCCESS) {
        srs_error("DASH: Consume audio failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsDash::on_video(SrsSharedPtrMessage* shared_video, SrsFormat* format)
{
    int ret = ERROR_SUCCESS;
    
    if (!enabled) {
        return ret;
    }
    
    if ((ret = controller->on_video(shared_video, format)) != ERROR_SUCCESS) {
        srs_error("DASH: Consume video failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

void SrsDash::on_unpublish()
{
    // Prevent duplicated unpublish.
    if (!enabled) {
        return;
    }
    
    enabled = false;
}

