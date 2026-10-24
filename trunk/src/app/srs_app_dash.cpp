//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_dash.hpp>

#include <srs_app_config.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_mp4.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_rtmp_stack.hpp>

#include <sstream>
#include <stdlib.h>

using namespace std;

string srs_time_to_utc_format_str(srs_utime_t u)
{
    time_t s = srsu2s(u);
    struct tm t;
    srs_assert(srs_gmtime_r(&s, &t) != NULL);

    char print_buf[256];
    size_t ret = strftime(print_buf, sizeof(print_buf), "%Y-%m-%dT%H:%M:%SZ", &t);

    return std::string(print_buf, ret);
}

ISrsInitMp4::ISrsInitMp4()
{
}

ISrsInitMp4::~ISrsInitMp4()
{
}

SrsInitMp4::SrsInitMp4()
{
    fw_ = new SrsFileWriter();
    init_ = new SrsMp4M2tsInitEncoder();
    fragment_ = new SrsFragment();
}

SrsInitMp4::~SrsInitMp4()
{
    srs_freep(init_);
    srs_freep(fw_);
    srs_freep(fragment_);
}

srs_error_t SrsInitMp4::write(SrsFormat *format, bool video, int tid)
{
    srs_error_t err = srs_success;

    string path_tmp = fragment_->tmppath();
    if ((err = fw_->open(path_tmp)) != srs_success) {
        return srs_error_wrap(err, "Open init mp4 failed, path=%s", path_tmp.c_str());
    }

    if ((err = init_->initialize(fw_)) != srs_success) {
        return srs_error_wrap(err, "init");
    }

    if ((err = init_->write(format, video, tid)) != srs_success) {
        return srs_error_wrap(err, "write init");
    }

    return err;
}

void SrsInitMp4::set_path(std::string v)
{
    fragment_->set_path(v);
}

std::string SrsInitMp4::tmppath()
{
    return fragment_->tmppath();
}

srs_error_t SrsInitMp4::rename()
{
    // Windows renames no open file.
    fw_->close();

    return fragment_->rename();
}

void SrsInitMp4::append(int64_t dts)
{
    fragment_->append(dts);
}

srs_error_t SrsInitMp4::create_dir()
{
    return fragment_->create_dir();
}

void SrsInitMp4::set_number(uint64_t n)
{
    fragment_->set_number(n);
}

uint64_t SrsInitMp4::number()
{
    return fragment_->number();
}

srs_utime_t SrsInitMp4::duration()
{
    return fragment_->duration();
}

srs_error_t SrsInitMp4::unlink_tmpfile()
{
    return fragment_->unlink_tmpfile();
}

srs_utime_t SrsInitMp4::get_start_dts()
{
    return fragment_->get_start_dts();
}

srs_error_t SrsInitMp4::unlink_file()
{
    return fragment_->unlink_file();
}

ISrsFragmentedMp4::ISrsFragmentedMp4()
{
}

ISrsFragmentedMp4::~ISrsFragmentedMp4()
{
}

SrsFragmentedMp4::SrsFragmentedMp4()
{
    fw_ = new SrsFileWriter();
    enc_ = new SrsMp4M2tsSegmentEncoder();
    fragment_ = new SrsFragment();

    config_ = _srs_config;
}

SrsFragmentedMp4::~SrsFragmentedMp4()
{
    srs_freep(enc_);
    srs_freep(fw_);
    srs_freep(fragment_);

    config_ = NULL;
}

srs_error_t SrsFragmentedMp4::initialize(ISrsRequest *r, bool video, int64_t time, ISrsMpdWriter *mpd, uint32_t tid)
{
    srs_error_t err = srs_success;

    string file_home;
    string file_name;
    int64_t sequence_number;
    if ((err = mpd->get_fragment(video, file_home, file_name, time, sequence_number)) != srs_success) {
        return srs_error_wrap(err, "get fragment, seq=%u, home=%s, file=%s",
                              (uint32_t)sequence_number, file_home.c_str(), file_name.c_str());
    }

    string home = config_->get_dash_path(r->vhost_);
    fragment_->set_path(home + "/" + file_home + "/" + file_name);
    // Set number of the fragment, use in mpd SegmentTemplate@startNumber later.
    fragment_->set_number(sequence_number);

    if ((err = fragment_->create_dir()) != srs_success) {
        return srs_error_wrap(err, "create dir");
    }

    string path_tmp = fragment_->tmppath();
    if ((err = fw_->open(path_tmp)) != srs_success) {
        return srs_error_wrap(err, "Open fmp4 failed, path=%s", path_tmp.c_str());
    }

    if ((err = enc_->initialize(fw_, (uint32_t)sequence_number, time, tid)) != srs_success) {
        return srs_error_wrap(err, "init encoder, seq=%u, time=%" PRId64 ", tid=%u", (uint32_t)sequence_number, time, tid);
    }

    return err;
}

srs_error_t SrsFragmentedMp4::write(SrsMediaPacket *shared_msg, SrsFormat *format)
{
    srs_error_t err = srs_success;

    if (shared_msg->is_audio()) {
        uint8_t *sample = (uint8_t *)format->raw_;
        uint32_t nb_sample = (uint32_t)format->nb_raw_;

        uint32_t dts = (uint32_t)shared_msg->timestamp_;
        err = enc_->write_sample(SrsMp4HandlerTypeSOUN, 0x00, dts, dts, sample, nb_sample);
    } else if (shared_msg->is_video()) {
        SrsVideoAvcFrameType frame_type = format->video_->frame_type_;
        uint32_t cts = (uint32_t)format->video_->cts_;

        uint32_t dts = (uint32_t)shared_msg->timestamp_;
        uint32_t pts = dts + cts;

        uint8_t *sample = (uint8_t *)format->raw_;
        uint32_t nb_sample = (uint32_t)format->nb_raw_;
        err = enc_->write_sample(SrsMp4HandlerTypeVIDE, frame_type, dts, pts, sample, nb_sample);
    } else {
        return err;
    }

    fragment_->append(shared_msg->timestamp_);

    return err;
}

srs_error_t SrsFragmentedMp4::reap(uint64_t &dts)
{
    srs_error_t err = srs_success;

    if ((err = enc_->flush(dts)) != srs_success) {
        return srs_error_wrap(err, "Flush encoder failed");
    }

    srs_freep(fw_);

    if ((err = fragment_->rename()) != srs_success) {
        return srs_error_wrap(err, "rename");
    }

    return err;
}

void SrsFragmentedMp4::set_path(std::string v)
{
    fragment_->set_path(v);
}

std::string SrsFragmentedMp4::tmppath()
{
    return fragment_->tmppath();
}

srs_error_t SrsFragmentedMp4::rename()
{
    return fragment_->rename();
}

void SrsFragmentedMp4::append(int64_t dts)
{
    fragment_->append(dts);
}

srs_error_t SrsFragmentedMp4::create_dir()
{
    return fragment_->create_dir();
}

void SrsFragmentedMp4::set_number(uint64_t n)
{
    fragment_->set_number(n);
}

srs_utime_t SrsFragmentedMp4::duration()
{
    return fragment_->duration();
}

srs_error_t SrsFragmentedMp4::unlink_tmpfile()
{
    // Windows removes no open file.
    srs_freep(fw_);

    return fragment_->unlink_tmpfile();
}

uint64_t SrsFragmentedMp4::number()
{
    return fragment_->number();
}

srs_utime_t SrsFragmentedMp4::get_start_dts()
{
    return fragment_->get_start_dts();
}

srs_error_t SrsFragmentedMp4::unlink_file()
{
    return fragment_->unlink_file();
}

ISrsMpdWriter::ISrsMpdWriter()
{
}

ISrsMpdWriter::~ISrsMpdWriter()
{
}

// The codecs of the video representation, RFC 6381 for H.264, ISO/IEC 14496-15 Annex E
// for HEVC, whose sample entry in the init file is hev1.
static string srs_dash_video_codecs(SrsVideoCodecConfig *vcodec)
{
    if (vcodec->id_ == SrsVideoCodecIdHEVC) {
        SrsHevcDecoderConfigurationRecord *r = &vcodec->hevc_dec_conf_record_;

        string space;
        if (r->general_profile_space_ > 0) {
            space = string(1, (char)('A' + r->general_profile_space_ - 1));
        }

        // The 32 compatibility flags in reverse bit order.
        uint32_t flags = 0;
        for (int i = 0; i < 32; i++) {
            if (r->general_profile_compatibility_flags_ & (1u << i)) {
                flags |= 1u << (31 - i);
            }
        }

        string codecs = srs_fmt_sprintf("hev1.%s%d.%X.%c%d", space.c_str(), r->general_profile_idc_, flags,
                                        r->general_tier_flag_ ? 'H' : 'L', r->general_level_idc_);

        // The 6 constraint bytes, without the trailing zero bytes.
        uint8_t bytes[6];
        int nn_bytes = 0;
        for (int i = 0; i < 6; i++) {
            bytes[i] = (uint8_t)(r->general_constraint_indicator_flags_ >> (8 * (5 - i)));
            if (bytes[i]) {
                nn_bytes = i + 1;
            }
        }
        for (int i = 0; i < nn_bytes; i++) {
            codecs += srs_fmt_sprintf(".%X", bytes[i]);
        }
        return codecs;
    }

    // The profile, compatibility and level bytes of the avcC.
    if (vcodec->id_ == SrsVideoCodecIdAVC && vcodec->avc_extra_data_.size() >= 4) {
        uint8_t *p = (uint8_t *)&vcodec->avc_extra_data_[0];
        return srs_fmt_sprintf("avc1.%02x%02x%02x", p[1], p[2], p[3]);
    }

    return "avc1.64001e";
}

// The codecs of the audio representation, RFC 6381: mp4a.40 and the AAC object type, or for
// MP3 mp4a and the object type of its esds in hex, as the init file writes it.
static string srs_dash_audio_codecs(SrsAudioCodecConfig *acodec)
{
    if (acodec->id_ == SrsAudioCodecIdMP3) {
        return srs_fmt_sprintf("mp4a.%02X", srs_mp4_audio_object_type(acodec->id_, acodec->sound_rate_));
    }
    if (acodec->id_ == SrsAudioCodecIdAAC && acodec->aac_object_ != SrsAacObjectTypeReserved) {
        return srs_fmt_sprintf("mp4a.40.%d", acodec->aac_object_);
    }
    return "mp4a.40.2";
}

// The dir of a stream's segments and init files, relative to dash_path: the stream's
// name under the dir of the MPD, which the MPD's BaseURL points to.
static string srs_dash_fragment_home(const string &mpd_file, ISrsRequest *r)
{
    SrsPath path;
    string mpd_path = srs_path_build_stream(mpd_file, r->vhost_, r->app_, r->stream_);
    return path.filepath_dir(mpd_path) + "/" + r->stream_;
}

SrsMpdWriter::SrsMpdWriter()
{
    req_ = NULL;
    timeshit_ = update_period_ = fragment_ = 0;

    window_size_ = 0;
    availability_start_time_ = 0;

    video_number_ = 0;
    audio_number_ = 0;

    config_ = _srs_config;
    app_factory_ = _srs_app_factory;
}

SrsMpdWriter::~SrsMpdWriter()
{
    config_ = NULL;
    app_factory_ = NULL;
}

// LCOV_EXCL_START
void SrsMpdWriter::dispose()
{
    // The MPD file write wrote, and no other; a stream that never wrote one has no dash_path
    // to build its path from.
    if (mpd_path_.empty()) {
        return;
    }

    SrsPath path;
    srs_error_t err = path.unlink(mpd_path_);
    if (err != srs_success) {
        srs_warn("ignore remove mpd failed, %s, %s", mpd_path_.c_str(), srs_error_desc(err).c_str());
        srs_freep(err);
    }
    mpd_path_ = "";
}
// LCOV_EXCL_STOP

// CRITICAL: This method is called AFTER the source has been added to the source pool
// in the fetch_or_create pattern (see PR 4449).
//
// IMPORTANT: All field initialization in this method MUST NOT cause coroutine context switches.
// This prevents the race condition where multiple coroutines could create duplicate sources
// for the same stream when context switches occurred during initialization.
srs_error_t SrsMpdWriter::initialize(ISrsRequest *r)
{
    req_ = r;
    return srs_success;
}

srs_error_t SrsMpdWriter::on_publish()
{
    ISrsRequest *r = req_;

    fragment_ = config_->get_dash_fragment(r->vhost_);
    update_period_ = config_->get_dash_update_period(r->vhost_);
    timeshit_ = config_->get_dash_timeshift(r->vhost_);
    home_ = config_->get_dash_path(r->vhost_);
    mpd_file_ = config_->get_dash_mpd_file(r->vhost_);

    fragment_home_ = srs_dash_fragment_home(mpd_file_, req_);
    window_size_ = config_->get_dash_window_size(r->vhost_);

    srs_trace("DASH: Config fragment=%dms, period=%dms, window=%d, timeshit=%dms, home=%s, mpd=%s",
              srsu2msi(fragment_), srsu2msi(update_period_), window_size_, srsu2msi(timeshit_), home_.c_str(), mpd_file_.c_str());

    return srs_success;
}

// LCOV_EXCL_START
void SrsMpdWriter::on_unpublish()
{
}

srs_error_t SrsMpdWriter::write(SrsFormat *format, ISrsFragmentWindow *afragments, ISrsFragmentWindow *vfragments)
{
    srs_error_t err = srs_success;

    // A stream with no video or no audio lists the one track it has.
    if (afragments->empty() && vfragments->empty()) {
        return err;
    }

    SrsPath path;
    string mpd_path = srs_path_build_stream(mpd_file_, req_->vhost_, req_->app_, req_->stream_);
    string full_path = home_ + "/" + mpd_path;
    string full_home = path.filepath_dir(full_path);

    fragment_home_ = srs_dash_fragment_home(mpd_file_, req_);

    if ((err = path.mkdir_all(full_home)) != srs_success) {
        return srs_error_wrap(err, "Create MPD home failed, home=%s", full_home.c_str());
    }

    srs_utime_t last_fragment = 0;
    if (!vfragments->empty()) {
        last_fragment = vfragments->at(vfragments->size() - 1)->duration();
    }
    if (!afragments->empty()) {
        last_fragment = srs_max(last_fragment, afragments->at(afragments->size() - 1)->duration());
    }
    double last_duration = srsu2ms(last_fragment) / 1000.0;

    stringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"utf-8\"?>" << endl
       << "<MPD profiles=\"urn:mpeg:dash:profile:isoff-live:2011,http://dashif.org/guidelines/dash-if-simple\" " << endl
       << "    ns1:schemaLocation=\"urn:mpeg:dash:schema:mpd:2011 DASH-MPD.xsd\" " << endl
       << "    xmlns=\"urn:mpeg:dash:schema:mpd:2011\" xmlns:ns1=\"http://www.w3.org/2001/XMLSchema-instance\" " << endl
       << "    type=\"dynamic\" " << endl
       << "    minimumUpdatePeriod=\"PT" << srs_fmt_sprintf("%.3f", srsu2ms(update_period_) / 1000.0) << "S\" " << endl
       << "    timeShiftBufferDepth=\"PT" << srs_fmt_sprintf("%.3f", last_duration * window_size_) << "S\" " << endl
       << "    availabilityStartTime=\"" << srs_time_to_utc_format_str(availability_start_time_) << "\" " << endl
       << "    publishTime=\"" << srs_time_to_utc_format_str(srs_time_now_cached()) << "\" " << endl
       << "    minBufferTime=\"PT" << srs_fmt_sprintf("%.3f", 2 * last_duration) << "S\" >" << endl;

    ss << "    <BaseURL>" << req_->stream_ << "/" << "</BaseURL>" << endl;

    ss << "    <Period start=\"PT0S\">" << endl;

    if (format->acodec_ && !afragments->empty()) {
        int start_index = srs_max(0, afragments->size() - window_size_);
        ss << "        <AdaptationSet mimeType=\"audio/mp4\" segmentAlignment=\"true\" startWithSAP=\"1\">" << endl;
        ss << "            <Representation id=\"audio\" bandwidth=\"48000\" codecs=\"" << srs_dash_audio_codecs(format->acodec_) << "\">" << endl;
        ss << "                <SegmentTemplate initialization=\"$RepresentationID$-init.mp4\" "
           << "media=\"$RepresentationID$-$Number$.m4s\" "
           << "startNumber=\"" << afragments->at(start_index)->number() << "\" "
           << "timescale=\"1000\">" << endl;
        ss << "                    <SegmentTimeline>" << endl;
        for (int i = start_index; i < afragments->size(); ++i) {
            ss << "                        <S t=\"" << srsu2ms(afragments->at(i)->get_start_dts()) << "\" "
               << "d=\"" << srsu2ms(afragments->at(i)->duration()) << "\" />" << endl;
        }
        ss << "                    </SegmentTimeline>" << endl;
        ss << "                </SegmentTemplate>" << endl;
        ss << "            </Representation>" << endl;
        ss << "        </AdaptationSet>" << endl;
    }

    if (format->vcodec_ && !vfragments->empty()) {
        int start_index = srs_max(0, vfragments->size() - window_size_);
        int w = format->vcodec_->width_;
        int h = format->vcodec_->height_;
        ss << "        <AdaptationSet mimeType=\"video/mp4\" segmentAlignment=\"true\" startWithSAP=\"1\">" << endl;
        ss << "            <Representation id=\"video\" bandwidth=\"800000\" codecs=\"" << srs_dash_video_codecs(format->vcodec_) << "\" " << "width=\"" << w << "\" height=\"" << h << "\">" << endl;
        ss << "                <SegmentTemplate initialization=\"$RepresentationID$-init.mp4\" "
           << "media=\"$RepresentationID$-$Number$.m4s\" "
           << "startNumber=\"" << vfragments->at(start_index)->number() << "\" "
           << "timescale=\"1000\">" << endl;
        ss << "                    <SegmentTimeline>" << endl;
        for (int i = start_index; i < vfragments->size(); ++i) {
            ss << "                        <S t=\"" << srsu2ms(vfragments->at(i)->get_start_dts()) << "\" "
               << "d=\"" << srsu2ms(vfragments->at(i)->duration()) << "\" />" << endl;
        }
        ss << "                    </SegmentTimeline>" << endl;
        ss << "                </SegmentTemplate>" << endl;
        ss << "            </Representation>" << endl;
        ss << "        </AdaptationSet>" << endl;
    }
    ss << "    </Period>" << endl;
    ss << "</MPD>" << endl;

    SrsUniquePtr<ISrsFileWriter> fw(app_factory_->create_file_writer());

    string full_path_tmp = full_path + ".tmp";
    if ((err = fw->open(full_path_tmp)) != srs_success) {
        return srs_error_wrap(err, "Open MPD file=%s failed", full_path_tmp.c_str());
    }

    string content = ss.str();
    if ((err = fw->write((void *)content.data(), content.length(), NULL)) != srs_success) {
        return srs_error_wrap(err, "Write MPD file=%s failed", full_path.c_str());
    }
    // Windows renames no open file.
    fw->close();

    if (srs_rename(full_path_tmp.c_str(), full_path.c_str()) < 0) {
        return srs_error_new(ERROR_DASH_WRITE_FAILED, "Rename %s to %s failed", full_path_tmp.c_str(), full_path.c_str());
    }
    mpd_path_ = full_path;

    srs_trace("DASH: Refresh MPD success, size=%dB, file=%s", content.length(), full_path.c_str());

    return err;
}
// LCOV_EXCL_STOP

srs_error_t SrsMpdWriter::get_fragment(bool video, std::string &home, std::string &file_name, int64_t time, int64_t &sn)
{
    srs_error_t err = srs_success;

    home = fragment_home_;

    // We name the segment as advanced N segments, because when we are generating segment at the current time,
    // the player may also request the current segment.
    srs_assert(fragment_);

    if (video) {
        sn = video_number_++;
        file_name = "video-" + srs_strconv_format_int(sn) + ".m4s";
    } else {
        sn = audio_number_++;
        file_name = "audio-" + srs_strconv_format_int(sn) + ".m4s";
    }

    return err;
}

void SrsMpdWriter::set_availability_start_time(srs_utime_t t)
{
    availability_start_time_ = t;
}

srs_utime_t SrsMpdWriter::get_availability_start_time()
{
    return availability_start_time_;
}

ISrsDashController::ISrsDashController()
{
}

ISrsDashController::~ISrsDashController()
{
}

SrsDashController::SrsDashController()
{
    req_ = NULL;
    format_ = NULL;
    // trackid start from 1, because some player will check if track id is greater than 0
    video_track_id_ = 1;
    audio_track_id_ = 2;
    mpd_ = new SrsMpdWriter();
    vcurrent_ = acurrent_ = NULL;
    vfragments_ = new SrsFragmentWindow();
    afragments_ = new SrsFragmentWindow();
    audio_dts_ = video_dts_ = 0;
    first_dts_ = -1;
    video_reaped_ = false;
    fragment_ = 0;

    app_factory_ = _srs_app_factory;
    config_ = _srs_config;
}

SrsDashController::~SrsDashController()
{
    srs_freep(mpd_);
    srs_freep(vcurrent_);
    srs_freep(acurrent_);
    srs_freep(vfragments_);
    srs_freep(afragments_);

    app_factory_ = NULL;
    config_ = NULL;
}

// LCOV_EXCL_START
void SrsDashController::dispose()
{
    srs_error_t err = srs_success;

    vfragments_->dispose();
    afragments_->dispose();

    if (vcurrent_ && (err = vcurrent_->unlink_tmpfile()) != srs_success) {
        srs_warn("Unlink tmp video m4s failed %s", srs_error_desc(err).c_str());
        srs_freep(err);
    }

    if (acurrent_ && (err = acurrent_->unlink_tmpfile()) != srs_success) {
        srs_warn("Unlink tmp audio m4s failed %s", srs_error_desc(err).c_str());
        srs_freep(err);
    }

    mpd_->dispose();

    // The init files refresh_init_mp4 wrote, and no other; a stream that never wrote one has
    // no dash_path to build its path from.
    SrsPath path;
    string *init_files[] = {&video_init_file_, &audio_init_file_};
    for (int i = 0; i < 2; i++) {
        string &init_file = *init_files[i];
        if (!init_file.empty() && path.exists(init_file) && (err = path.unlink(init_file)) != srs_success) {
            srs_warn("Unlink init mp4 failed %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
        init_file = "";
    }

    srs_trace("gracefully dispose dash %s", req_ ? req_->get_stream_url().c_str() : "");
}
// LCOV_EXCL_STOP

// CRITICAL: This method is called AFTER the source has been added to the source pool
// in the fetch_or_create pattern (see PR 4449).
//
// IMPORTANT: All field initialization in this method MUST NOT cause coroutine context switches.
// This prevents the race condition where multiple coroutines could create duplicate sources
// for the same stream when context switches occurred during initialization.
srs_error_t SrsDashController::initialize(ISrsRequest *r)
{
    srs_error_t err = srs_success;

    req_ = r;

    if ((err = mpd_->initialize(r)) != srs_success) {
        return srs_error_wrap(err, "mpd");
    }

    return err;
}

srs_error_t SrsDashController::on_publish()
{
    srs_error_t err = srs_success;

    ISrsRequest *r = req_;

    fragment_ = config_->get_dash_fragment(r->vhost_);
    home_ = config_->get_dash_path(r->vhost_);

    if ((err = mpd_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "mpd");
    }

    srs_freep(vcurrent_);
    srs_freep(vfragments_);
    vfragments_ = app_factory_->create_fragment_window();

    srs_freep(acurrent_);
    srs_freep(afragments_);
    afragments_ = app_factory_->create_fragment_window();

    audio_dts_ = 0;
    video_dts_ = 0;
    first_dts_ = -1;
    video_reaped_ = false;

    return err;
}

// Remove the file of a reaped segment that no window lists, and free the segment.
static void srs_dash_unlink_unlisted(ISrsFragmentedMp4 *&fragment)
{
    srs_error_t err = srs_success;

    if ((err = fragment->unlink_file()) != srs_success) {
        srs_warn("Unlink unlisted m4s failed %s", srs_error_desc(err).c_str());
        srs_freep(err);
    }

    srs_freep(fragment);
}

// LCOV_EXCL_START
void SrsDashController::on_unpublish()
{
    mpd_->on_unpublish();

    srs_error_t err = srs_success;

    bool video_reaped = false;
    if (vcurrent_ && (err = vcurrent_->reap(video_dts_)) != srs_success) {
        srs_warn("reap video dts=%" PRId64 " err %s", video_dts_, srs_error_desc(err).c_str());
        srs_freep(err);
    } else if (vcurrent_) {
        video_reaped = true;
    }

    if (vcurrent_ && vcurrent_->duration()) {
        vfragments_->append(vcurrent_);
        vcurrent_ = NULL;
    } else if (video_reaped) {
        // A segment of one frame, such as a keyframe that ends the publish, has no duration,
        // so the MPD never lists it; remove the file the reap renamed it to.
        srs_dash_unlink_unlisted(vcurrent_);
    }

    bool audio_reaped = false;
    if (acurrent_ && (err = acurrent_->reap(audio_dts_)) != srs_success) {
        srs_warn("reap audio dts=%" PRId64 " err %s", audio_dts_, srs_error_desc(err).c_str());
        srs_freep(err);
    } else if (acurrent_) {
        audio_reaped = true;
    }

    if (acurrent_ && acurrent_->duration() > 0) {
        afragments_->append(acurrent_);
        acurrent_ = NULL;
    } else if (audio_reaped) {
        srs_dash_unlink_unlisted(acurrent_);
    }

    if ((err = refresh_mpd(format_)) != srs_success) {
        srs_warn("Refresh the MPD failed, err=%s", srs_error_desc(err).c_str());
        srs_freep(err);
    }
}
// LCOV_EXCL_STOP

srs_error_t SrsDashController::on_audio(SrsMediaPacket *shared_audio, SrsFormat *format)
{
    srs_error_t err = srs_success;

    format_ = format;

    // MP3 has no sequence header: its first frame stands for one, and writes the init file.
    if (format->is_aac_sequence_header() || format->is_mp3_sequence_header()) {
        return refresh_init_mp4(shared_audio, format);
    }

    audio_dts_ = shared_audio->timestamp_;

    if (!acurrent_) {
        acurrent_ = app_factory_->create_fragmented_mp4();

        if ((err = acurrent_->initialize(req_, false, audio_dts_ * SRS_UTIME_MILLISECONDS, mpd_, audio_track_id_)) != srs_success) {
            return srs_error_wrap(err, "Initialize the audio fragment failed");
        }
    }

    if (first_dts_ == -1) {
        first_dts_ = audio_dts_;
        mpd_->set_availability_start_time(srs_time_now_cached() - first_dts_ * SRS_UTIME_MILLISECONDS);
    }

    // The video is reaped, audio must be reaped right now to align the timestamp of video; while
    // the publish has sent no video, the audio is reaped by its own duration.
    if (video_reaped_ || (!vcurrent_ && vfragments_->empty() && acurrent_->duration() >= fragment_)) {
        video_reaped_ = false;
        // Append current timestamp to calculate right duration.
        acurrent_->append(shared_audio->timestamp_);
        if ((err = acurrent_->reap(audio_dts_)) != srs_success) {
            return srs_error_wrap(err, "reap current");
        }

        afragments_->append(acurrent_);
        acurrent_ = app_factory_->create_fragmented_mp4();

        if ((err = acurrent_->initialize(req_, false, audio_dts_ * SRS_UTIME_MILLISECONDS, mpd_, audio_track_id_)) != srs_success) {
            return srs_error_wrap(err, "Initialize the audio fragment failed");
        }

        if ((err = refresh_mpd(format)) != srs_success) {
            return srs_error_wrap(err, "Refresh the MPD failed");
        }
    }

    if ((err = acurrent_->write(shared_audio, format)) != srs_success) {
        return srs_error_wrap(err, "Write audio to fragment failed");
    }

    srs_utime_t fragment = config_->get_dash_fragment(req_->vhost_);
    int window_size = config_->get_dash_window_size(req_->vhost_);
    int dash_window = 2 * window_size * fragment;
    if (afragments_->size() > window_size) {
        int w = 0;
        for (int i = afragments_->size() - window_size; i < afragments_->size(); ++i) {
            w += afragments_->at(i)->duration();
        }
        dash_window = srs_max(dash_window, w);

        // shrink the segments.
        afragments_->shrink(dash_window);
    }

    bool dash_cleanup = config_->get_dash_cleanup(req_->vhost_);
    // remove the m4s file.
    afragments_->clear_expired(dash_cleanup);

    return err;
}

srs_error_t SrsDashController::on_video(SrsMediaPacket *shared_video, SrsFormat *format)
{
    srs_error_t err = srs_success;

    format_ = format;

    // Ignore the info frame, such as the colorInfo metadata of enhanced RTMP: it holds no
    // picture and does not set the sample, so writing it writes stale bytes. HLS skips it too.
    if (format->video_ && format->video_->frame_type_ == SrsVideoAvcFrameTypeVideoInfoFrame) {
        return err;
    }

    if (format->is_avc_sequence_header()) {
        return refresh_init_mp4(shared_video, format);
    }

    video_dts_ = shared_video->timestamp_;

    if (!vcurrent_) {
        vcurrent_ = app_factory_->create_fragmented_mp4();

        if ((err = vcurrent_->initialize(req_, true, video_dts_ * SRS_UTIME_MILLISECONDS, mpd_, video_track_id_)) != srs_success) {
            return srs_error_wrap(err, "Initialize the video fragment failed");
        }
    }

    if (first_dts_ == -1) {
        first_dts_ = video_dts_;
        mpd_->set_availability_start_time(srs_time_now_cached() - first_dts_ * SRS_UTIME_MILLISECONDS);
    }

    bool reopen = format->video_->frame_type_ == SrsVideoAvcFrameTypeKeyFrame && vcurrent_->duration() >= fragment_;
    if (reopen) {
        // Append current timestamp to calculate right duration.
        vcurrent_->append(shared_video->timestamp_);
        if ((err = vcurrent_->reap(video_dts_)) != srs_success) {
            return srs_error_wrap(err, "reap current");
        }

        // Mark the video has reaped, audio will reaped when recv next frame.
        video_reaped_ = true;

        vfragments_->append(vcurrent_);
        vcurrent_ = app_factory_->create_fragmented_mp4();

        if ((err = vcurrent_->initialize(req_, true, video_dts_ * SRS_UTIME_MILLISECONDS, mpd_, video_track_id_)) != srs_success) {
            return srs_error_wrap(err, "Initialize the video fragment failed");
        }

        if ((err = refresh_mpd(format)) != srs_success) {
            return srs_error_wrap(err, "Refresh the MPD failed");
        }
    }

    if ((err = vcurrent_->write(shared_video, format)) != srs_success) {
        return srs_error_wrap(err, "Write video to fragment failed");
    }

    srs_utime_t fragment = config_->get_dash_fragment(req_->vhost_);
    int window_size = config_->get_dash_window_size(req_->vhost_);
    int dash_window = 2 * window_size * fragment;
    if (vfragments_->size() > window_size) {
        int w = 0;
        for (int i = vfragments_->size() - window_size; i < vfragments_->size(); ++i) {
            w += vfragments_->at(i)->duration();
        }
        dash_window = srs_max(dash_window, w);

        // shrink the segments.
        vfragments_->shrink(dash_window);
    }

    bool dash_cleanup = config_->get_dash_cleanup(req_->vhost_);
    // remove the m4s file.
    vfragments_->clear_expired(dash_cleanup);

    return err;
}

srs_error_t SrsDashController::refresh_mpd(SrsFormat *format)
{
    srs_error_t err = srs_success;

    if (!format) {
        return err;
    }

    // A stream of both tracks waits until each has a segment, so that its first MPD lists
    // both; a track this publish has not sent is not waited for.
    bool has_video = vcurrent_ || !vfragments_->empty();
    bool has_audio = acurrent_ || !afragments_->empty();
    if ((has_video && vfragments_->empty()) || (has_audio && afragments_->empty())) {
        return err;
    }

    if ((err = mpd_->write(format, afragments_, vfragments_)) != srs_success) {
        return srs_error_wrap(err, "write mpd");
    }

    return err;
}

srs_error_t SrsDashController::refresh_init_mp4(SrsMediaPacket *msg, SrsFormat *format)
{
    srs_error_t err = srs_success;

    // Only AAC has a config to wait for; MP3 has none.
    bool audio_ok = msg->is_audio() && (format->acodec_->id_ == SrsAudioCodecIdMP3 || format->acodec_->is_aac_codec_ok());
    if (msg->size() <= 0 || (msg->is_video() && !format->vcodec_->is_avc_codec_ok()) || (msg->is_audio() && !audio_ok)) {
        srs_warn("DASH: Ignore empty sequence header.");
        return err;
    }

    SrsPath path_util;
    // Beside the segments, where the MPD's BaseURL points the player.
    string full_home = home_ + "/" + srs_dash_fragment_home(config_->get_dash_mpd_file(req_->vhost_), req_);
    if ((err = path_util.mkdir_all(full_home)) != srs_success) {
        return srs_error_wrap(err, "Create media home failed, home=%s", full_home.c_str());
    }

    std::string path = full_home;
    if (msg->is_video()) {
        path += "/video-init.mp4";
    } else {
        path += "/audio-init.mp4";
    }

    SrsUniquePtr<ISrsInitMp4> init_mp4(app_factory_->create_init_mp4());

    init_mp4->set_path(path);

    int tid = msg->is_video() ? video_track_id_ : audio_track_id_;
    if ((err = init_mp4->write(format, msg->is_video(), tid)) != srs_success) {
        return srs_error_wrap(err, "write init");
    }

    if ((err = init_mp4->rename()) != srs_success) {
        return srs_error_wrap(err, "rename init");
    }

    if (msg->is_video()) {
        video_init_file_ = path;
    } else {
        audio_init_file_ = path;
    }

    srs_trace("DASH: Refresh media type=%s, file=%s", (msg->is_video() ? "video" : "audio"), path.c_str());

    return err;
}

ISrsDash::ISrsDash()
{
}

ISrsDash::~ISrsDash()
{
}

SrsDash::SrsDash()
{
    hub_ = NULL;
    req_ = NULL;
    controller_ = new SrsDashController();

    enabled_ = false;
    disposable_ = false;
    disposed_publishing_ = false;
    last_update_time_ = 0;

    config_ = _srs_config;
}

SrsDash::~SrsDash()
{
    srs_freep(controller_);

    config_ = NULL;
}

void SrsDash::dispose()
{
    // Call on_unpublish only if still enabled to avoid duplicate calls.
    if (enabled_) {
        on_unpublish();
    }

    // Ignore when dash_dispose disabled.
    srs_utime_t dash_dispose = config_->get_dash_dispose(req_->vhost_);
    if (!dash_dispose) {
        return;
    }

    // Always dispose files when dash_dispose timeout occurs, even if already unpublished.
    controller_->dispose();
}

srs_error_t SrsDash::cycle()
{
    srs_error_t err = srs_success;

    if (last_update_time_ <= 0) {
        last_update_time_ = srs_time_now_cached();
    }

    if (!req_) {
        return err;
    }

    srs_utime_t dash_dispose = config_->get_dash_dispose(req_->vhost_);
    if (dash_dispose <= 0) {
        return err;
    }
    if (srs_time_now_cached() - last_update_time_ <= dash_dispose) {
        return err;
    }
    last_update_time_ = srs_time_now_cached();

    if (!disposable_) {
        return err;
    }
    disposable_ = false;

    srs_trace("dash cycle to dispose dash %s, timeout=%dms", req_->get_stream_url().c_str(), dash_dispose);
    bool publishing = enabled_;
    dispose();

    // An idle publisher is still published, so its next media starts DASH again.
    disposed_publishing_ = publishing;

    return err;
}

srs_error_t SrsDash::resume()
{
    srs_error_t err = srs_success;

    disposed_publishing_ = false;
    srs_trace("dash resume %s, media after the dispose", req_->get_stream_url().c_str());

    if ((err = on_publish()) != srs_success) {
        return srs_error_wrap(err, "dash publish");
    }

    // The sequence headers came before the dispose, so feed them again.
    if ((err = hub_->on_dash_request_sh()) != srs_success) {
        return srs_error_wrap(err, "dash request sh");
    }

    return err;
}

srs_utime_t SrsDash::cleanup_delay()
{
    // We use larger timeout to cleanup the HLS, after disposed it if required.
    return config_->get_dash_dispose(req_->vhost_) * 1.1;
}

// CRITICAL: This method is called AFTER the source has been added to the source pool
// in the fetch_or_create pattern (see PR 4449).
//
// IMPORTANT: All field initialization in this method MUST NOT cause coroutine context switches.
// This prevents the race condition where multiple coroutines could create duplicate sources
// for the same stream when context switches occurred during initialization.
srs_error_t SrsDash::initialize(ISrsOriginHub *h, ISrsRequest *r)
{
    srs_error_t err = srs_success;

    hub_ = h;
    req_ = r;

    if ((err = controller_->initialize(req_)) != srs_success) {
        return srs_error_wrap(err, "controller");
    }

    return err;
}

srs_error_t SrsDash::on_publish()
{
    srs_error_t err = srs_success;

    // Prevent duplicated publish.
    if (enabled_) {
        return err;
    }

    if (!config_->get_dash_enabled(req_->vhost_)) {
        return err;
    }
    enabled_ = true;

    // update the dash time, for dash_dispose.
    last_update_time_ = srs_time_now_cached();
    disposed_publishing_ = false;

    if ((err = controller_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "controller");
    }

    // ok, the dash can be dispose, or need to be dispose.
    disposable_ = true;

    return err;
}

srs_error_t SrsDash::on_audio(SrsMediaPacket *shared_audio, SrsFormat *format)
{
    srs_error_t err = srs_success;

    if (disposed_publishing_ && (err = resume()) != srs_success) {
        return srs_error_wrap(err, "dash: resume");
    }

    if (!enabled_) {
        return err;
    }

    if (!format->acodec_) {
        return err;
    }

    // update the dash time, for dash_dispose.
    last_update_time_ = srs_time_now_cached();

    if ((err = controller_->on_audio(shared_audio, format)) != srs_success) {
        return srs_error_wrap(err, "Consume audio failed");
    }

    return err;
}

srs_error_t SrsDash::on_video(SrsMediaPacket *shared_video, SrsFormat *format)
{
    srs_error_t err = srs_success;

    if (disposed_publishing_ && (err = resume()) != srs_success) {
        return srs_error_wrap(err, "dash: resume");
    }

    if (!enabled_) {
        return err;
    }

    if (!format->vcodec_) {
        return err;
    }

    // update the dash time, for dash_dispose.
    last_update_time_ = srs_time_now_cached();

    if ((err = controller_->on_video(shared_video, format)) != srs_success) {
        return srs_error_wrap(err, "Consume video failed");
    }

    return err;
}

void SrsDash::on_unpublish()
{
    // The stream is unpublished, so no media resumes a disposed DASH.
    disposed_publishing_ = false;

    // Prevent duplicated unpublish.
    if (!enabled_) {
        return;
    }

    enabled_ = false;

    controller_->on_unpublish();
}
