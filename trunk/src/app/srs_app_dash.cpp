//
// Copyright (c) 2013-2025 The SRS Authors
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
#include <unistd.h>

using namespace std;

string srs_time_to_utc_format_str(srs_utime_t u)
{
    time_t s = srsu2s(u);
    struct tm t;
    srs_assert(gmtime_r(&s, &t) != NULL);

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

void SrsMpdWriter::dispose()
{
    if (req_) {
        string mpd_path = srs_path_build_stream(mpd_file_, req_->vhost_, req_->app_, req_->stream_);
        string full_path = home_ + "/" + mpd_path;
        SrsPath path;
        srs_error_t err = path.unlink(full_path);
        if (err != srs_success) {
            srs_warn("ignore remove mpd failed, %s, %s", full_path.c_str(), srs_error_desc(err).c_str());
            srs_freep(err);
        }
    }
}

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

    SrsPath path;
    string mpd_path = srs_path_build_stream(mpd_file_, req_->vhost_, req_->app_, req_->stream_);
    fragment_home_ = path.filepath_dir(mpd_path) + "/" + req_->stream_;
    window_size_ = config_->get_dash_window_size(r->vhost_);

    srs_trace("DASH: Config fragment=%dms, period=%dms, window=%d, timeshit=%dms, home=%s, mpd=%s",
              srsu2msi(fragment_), srsu2msi(update_period_), window_size_, srsu2msi(timeshit_), home_.c_str(), mpd_file_.c_str());

    return srs_success;
}

void SrsMpdWriter::on_unpublish()
{
}

srs_error_t SrsMpdWriter::write(SrsFormat *format, ISrsFragmentWindow *afragments, ISrsFragmentWindow *vfragments)
{
    srs_error_t err = srs_success;

    // TODO: FIXME: pure audio/video support.
    if (afragments->empty() || vfragments->empty()) {
        return err;
    }

    SrsPath path;
    string mpd_path = srs_path_build_stream(mpd_file_, req_->vhost_, req_->app_, req_->stream_);
    string full_path = home_ + "/" + mpd_path;
    string full_home = path.filepath_dir(full_path);

    fragment_home_ = path.filepath_dir(mpd_path) + "/" + req_->stream_;

    if ((err = path.mkdir_all(full_home)) != srs_success) {
        return srs_error_wrap(err, "Create MPD home failed, home=%s", full_home.c_str());
    }

    double last_duration = srsu2s(srs_max(vfragments->at(vfragments->size() - 1)->duration(), afragments->at(afragments->size() - 1)->duration()));

    stringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"utf-8\"?>" << endl
       << "<MPD profiles=\"urn:mpeg:dash:profile:isoff-live:2011,http://dashif.org/guidelines/dash-if-simple\" " << endl
       << "    ns1:schemaLocation=\"urn:mpeg:dash:schema:mpd:2011 DASH-MPD.xsd\" " << endl
       << "    xmlns=\"urn:mpeg:dash:schema:mpd:2011\" xmlns:ns1=\"http://www.w3.org/2001/XMLSchema-instance\" " << endl
       << "    type=\"dynamic\" " << endl
       << "    minimumUpdatePeriod=\"PT" << srs_fmt_sprintf("%.3f", srsu2s(update_period_)) << "S\" " << endl
       << "    timeShiftBufferDepth=\"PT" << srs_fmt_sprintf("%.3f", last_duration * window_size_) << "S\" " << endl
       << "    availabilityStartTime=\"" << srs_time_to_utc_format_str(availability_start_time_) << "\" " << endl
       << "    publishTime=\"" << srs_time_to_utc_format_str(srs_time_now_cached()) << "\" " << endl
       << "    minBufferTime=\"PT" << srs_fmt_sprintf("%.3f", 2 * last_duration) << "S\" >" << endl;

    ss << "    <BaseURL>" << req_->stream_ << "/" << "</BaseURL>" << endl;

    ss << "    <Period start=\"PT0S\">" << endl;

    if (format->acodec_ && !afragments->empty()) {
        int start_index = srs_max(0, afragments->size() - window_size_);
        ss << "        <AdaptationSet mimeType=\"audio/mp4\" segmentAlignment=\"true\" startWithSAP=\"1\">" << endl;
        ss << "            <Representation id=\"audio\" bandwidth=\"48000\" codecs=\"mp4a.40.2\">" << endl;
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
        ss << "            <Representation id=\"video\" bandwidth=\"800000\" codecs=\"avc1.64001e\" " << "width=\"" << w << "\" height=\"" << h << "\">" << endl;
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

    if (::rename(full_path_tmp.c_str(), full_path.c_str()) < 0) {
        return srs_error_new(ERROR_DASH_WRITE_FAILED, "Rename %s to %s failed", full_path_tmp.c_str(), full_path.c_str());
    }

    srs_trace("DASH: Refresh MPD success, size=%dB, file=%s", content.length(), full_path.c_str());

    return err;
}

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

    srs_trace("gracefully dispose dash %s", req_ ? req_->get_stream_url().c_str() : "");
}

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

void SrsDashController::on_unpublish()
{
    mpd_->on_unpublish();

    srs_error_t err = srs_success;

    if (vcurrent_ && (err = vcurrent_->reap(video_dts_)) != srs_success) {
        srs_warn("reap video dts=%" PRId64 " err %s", video_dts_, srs_error_desc(err).c_str());
        srs_freep(err);
    }

    if (vcurrent_ && vcurrent_->duration()) {
        vfragments_->append(vcurrent_);
        vcurrent_ = NULL;
    }

    if (acurrent_ && (err = acurrent_->reap(audio_dts_)) != srs_success) {
        srs_warn("reap audio dts=%" PRId64 " err %s", audio_dts_, srs_error_desc(err).c_str());
        srs_freep(err);
    }

    if (acurrent_ && acurrent_->duration() > 0) {
        afragments_->append(acurrent_);
        acurrent_ = NULL;
    }

    if ((err = refresh_mpd(format_)) != srs_success) {
        srs_warn("Refresh the MPD failed, err=%s", srs_error_desc(err).c_str());
        srs_freep(err);
    }
}

srs_error_t SrsDashController::on_audio(SrsMediaPacket *shared_audio, SrsFormat *format)
{
    srs_error_t err = srs_success;

    format_ = format;

    if (format->is_aac_sequence_header()) {
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

    // TODO: FIXME: Support pure audio streaming.
    if (video_reaped_) {
        // The video is reaped, audio must be reaped right now to align the timestamp of video.
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

    // TODO: FIXME: Support pure audio streaming.
    if (!format || !format->acodec_ || !format->vcodec_) {
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

    if (msg->size() <= 0 || (msg->is_video() && !format->vcodec_->is_avc_codec_ok()) || (msg->is_audio() && !format->acodec_->is_aac_codec_ok())) {
        srs_warn("DASH: Ignore empty sequence header.");
        return err;
    }

    SrsPath path_util;
    string full_home = home_ + "/" + req_->app_ + "/" + req_->stream_;
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
    // We disabled the reload, so DASH will not be enabled by reloading.
    // As a result, if DASH is disabled, we don't need to dispose.
    if (!enabled_) {
        return;
    }

    on_unpublish();

    // Ignore when dash_dispose disabled.
    srs_utime_t dash_dispose = config_->get_dash_dispose(req_->vhost_);
    if (!dash_dispose) {
        return;
    }

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
    dispose();

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
    // Prevent duplicated unpublish.
    if (!enabled_) {
        return;
    }

    enabled_ = false;

    controller_->on_unpublish();
}
