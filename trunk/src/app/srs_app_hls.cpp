//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_hls.hpp>

#include <algorithm>
#include <fcntl.h>
#include <math.h>
#include <sstream>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
using namespace std;

#include <openssl/rand.h>
#include <srs_app_config.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_mp4.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_kernel_ts.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_format.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_stream.hpp>

// drop the segment when duration of ts too small.
// TODO: FIXME: Refine to time unit.
#define SRS_HLS_SEGMENT_MIN_DURATION (100 * SRS_UTIME_MILLISECONDS)

// fragment plus the deviation percent.
#define SRS_HLS_FLOOR_REAP_PERCENT 0.3
// reset the piece id when deviation overflow this.
#define SRS_JUMP_WHEN_PIECE_DEVIATION 20

SrsHlsSegment::SrsHlsSegment(SrsTsContext *c, SrsAudioCodecId ac, SrsVideoCodecId vc, ISrsFileWriter *w)
{
    sequence_no_ = 0;
    writer_ = w;
    tscw_ = new SrsTsContextWriter(writer_, c, ac, vc);
}

SrsHlsSegment::~SrsHlsSegment()
{
    srs_freep(tscw_);
}

void SrsHlsSegment::config_cipher(unsigned char *key, unsigned char *iv)
{
    memcpy(this->iv_, iv, 16);

    SrsEncFileWriter *fw = dynamic_cast<SrsEncFileWriter *>(writer_);
    srs_assert(fw);
    fw->config_cipher(key, iv);
}

// LCOV_EXCL_START
srs_error_t SrsHlsSegment::rename()
{
    if (true) {
        std::stringstream ss;
        ss << srsu2msi(duration());
        uri_ = srs_strings_replace(uri_, "[duration]", ss.str());
    }

    return SrsFragment::rename();
}
// LCOV_EXCL_STOP

SrsInitMp4Segment::SrsInitMp4Segment(ISrsFileWriter *fw)
{
    fw_ = fw;
    const_iv_size_ = 0;
}

SrsInitMp4Segment::~SrsInitMp4Segment()
{
    fw_->close();
}

srs_error_t SrsInitMp4Segment::config_cipher(unsigned char *kid, unsigned char *const_iv, uint8_t const_iv_size)
{
    if (const_iv_size != 8 && const_iv_size != 16) {
        return srs_error_new(ERROR_MP4_BOX_STRING, "invalidate const_iv_size=%d", const_iv_size);
    }

    memcpy(kid_, kid, 16);
    memcpy(const_iv_, const_iv, const_iv_size);
    const_iv_size_ = const_iv_size;

    // CBCS encryption: For example, 1 encrypt block, 9 skip blocks (10% encryption)
    init_.config_encryption(1, 9, kid_, const_iv, const_iv_size);

    return srs_success;
}

srs_error_t SrsInitMp4Segment::write(SrsFormat *format, int v_tid, int a_tid)
{
    srs_error_t err = srs_success;

    if ((err = init_encoder()) != srs_success) {
        return srs_error_wrap(err, "init encoder");
    }

    if ((err = init_.write(format, v_tid, a_tid)) != srs_success) {
        return srs_error_wrap(err, "write init");
    }

    return err;
}

srs_error_t SrsInitMp4Segment::write_video_only(SrsFormat *format, int v_tid)
{
    srs_error_t err = srs_success;

    if ((err = init_encoder()) != srs_success) {
        return srs_error_wrap(err, "init encoder");
    }

    if ((err = init_.write(format, true, v_tid)) != srs_success) {
        return srs_error_wrap(err, "write init");
    }

    return err;
}

srs_error_t SrsInitMp4Segment::write_audio_only(SrsFormat *format, int a_tid)
{
    srs_error_t err = srs_success;

    if ((err = init_encoder()) != srs_success) {
        return srs_error_wrap(err, "init encoder");
    }

    if ((err = init_.write(format, false, a_tid)) != srs_success) {
        return srs_error_wrap(err, "write init");
    }

    return err;
}

srs_error_t SrsInitMp4Segment::init_encoder()
{
    srs_error_t err = srs_success;

    srs_assert(!fullpath().empty());

    string path_tmp = tmppath();
    if ((err = fw_->open(path_tmp)) != srs_success) {
        return srs_error_wrap(err, "Open init mp4 failed, path=%s", path_tmp.c_str());
    }

    if ((err = init_.initialize(fw_)) != srs_success) {
        return srs_error_wrap(err, "init");
    }

    return err;
}

SrsHlsM4sSegment::SrsHlsM4sSegment(ISrsFileWriter *fw)
{
    fw_ = fw;
}

SrsHlsM4sSegment::~SrsHlsM4sSegment()
{
}

srs_error_t SrsHlsM4sSegment::initialize(int64_t time, uint32_t v_tid, uint32_t a_tid, int sequence_number, std::string m4s_path)
{
    srs_error_t err = srs_success;

    set_path(m4s_path);

    set_number(sequence_number);
    if ((err = create_dir()) != srs_success) {
        return srs_error_wrap(err, "create hls m4s segment dir.");
    }

    if ((err = fw_->open(tmppath())) != srs_success) {
        return srs_error_wrap(err, "open hls m4s segment tmp file.");
    }

    if ((err = enc_.initialize(fw_, sequence_number, time, v_tid, a_tid)) != srs_success) {
        return srs_error_wrap(err, "initialize SrsFmp4SegmentEncoder");
    }

    return err;
}

void SrsHlsM4sSegment::config_cipher(unsigned char *key, unsigned char *iv)
{
    // TODO: set key and iv to mp4 box
    enc_.config_cipher(key, iv);
    memcpy(this->iv_, iv, 16);
}

srs_error_t SrsHlsM4sSegment::write(SrsMediaPacket *shared_msg, SrsFormat *format)
{
    srs_error_t err = srs_success;

    if (shared_msg->is_audio()) {
        uint8_t *sample = (uint8_t *)format->raw_;
        uint32_t nb_sample = (uint32_t)format->nb_raw_;

        uint32_t dts = (uint32_t)shared_msg->timestamp_;
        if ((err = enc_.write_sample(SrsMp4HandlerTypeSOUN, 0x00, dts, dts, sample, nb_sample)) != srs_success) {
            return srs_error_wrap(err, "m4s segment write audio sample");
        }
    } else if (shared_msg->is_video()) {
        SrsVideoAvcFrameType frame_type = format->video_->frame_type_;
        uint32_t cts = (uint32_t)format->video_->cts_;

        uint32_t dts = (uint32_t)shared_msg->timestamp_;
        uint32_t pts = dts + cts;

        uint8_t *sample = (uint8_t *)format->raw_;
        uint32_t nb_sample = (uint32_t)format->nb_raw_;
        if ((err = enc_.write_sample(SrsMp4HandlerTypeVIDE, frame_type, dts, pts, sample, nb_sample)) != srs_success) {
            return srs_error_wrap(err, "m4s segment write video sample");
        }
    } else {
        srs_trace("the sample m4s segment write is neither video nor audio sample.");
        return err;
    }

    append(shared_msg->timestamp_);

    return err;
}

srs_error_t SrsHlsM4sSegment::reap(uint64_t dts)
{
    srs_error_t err = srs_success;

    if ((err = enc_.flush(dts)) != srs_success) {
        return srs_error_wrap(err, "m4s flush encoder.");
    }

    fw_->close();

    if ((err = rename()) != srs_success) {
        return srs_error_wrap(err, "m4s segment rename.");
    }

    return err;
}

SrsDvrAsyncCallOnHls::SrsDvrAsyncCallOnHls(SrsContextId c, ISrsRequest *r, string p, string t, string m, string mu, int s, srs_utime_t d)
{
    req_ = r->copy();
    cid_ = c;
    path_ = p;
    ts_url_ = t;
    m3u8_ = m;
    m3u8_url_ = mu;
    seq_no_ = s;
    duration_ = d;

    config_ = _srs_config;
    hooks_ = _srs_hooks;
}

SrsDvrAsyncCallOnHls::~SrsDvrAsyncCallOnHls()
{
    srs_freep(req_);

    config_ = NULL;
    hooks_ = NULL;
}

srs_error_t SrsDvrAsyncCallOnHls::call()
{
    srs_error_t err = srs_success;

    if (!config_->get_vhost_http_hooks_enabled(req_->vhost_)) {
        return err;
    }

    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;

    if (true) {
        SrsConfDirective *conf = config_->get_vhost_on_hls(req_->vhost_);

        if (!conf) {
            return err;
        }

        hooks = conf->args_;
    }

    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((err = hooks_->on_hls(cid_, url, req_, path_, ts_url_, m3u8_, m3u8_url_, seq_no_, duration_)) != srs_success) {
            return srs_error_wrap(err, "callback on_hls %s", url.c_str());
        }
    }

    return err;
}

string SrsDvrAsyncCallOnHls::to_string()
{
    return "on_hls: " + path_;
}

SrsDvrAsyncCallOnHlsNotify::SrsDvrAsyncCallOnHlsNotify(SrsContextId c, ISrsRequest *r, string u)
{
    cid_ = c;
    req_ = r->copy();
    ts_url_ = u;

    config_ = _srs_config;
    hooks_ = _srs_hooks;
}

SrsDvrAsyncCallOnHlsNotify::~SrsDvrAsyncCallOnHlsNotify()
{
    srs_freep(req_);

    config_ = NULL;
    hooks_ = NULL;
}

srs_error_t SrsDvrAsyncCallOnHlsNotify::call()
{
    srs_error_t err = srs_success;

    if (!config_->get_vhost_http_hooks_enabled(req_->vhost_)) {
        return err;
    }

    // the http hooks will cause context switch,
    // so we must copy all hooks for the on_connect may freed.
    // @see https://github.com/ossrs/srs/issues/475
    vector<string> hooks;

    if (true) {
        SrsConfDirective *conf = config_->get_vhost_on_hls_notify(req_->vhost_);

        if (!conf) {
            return err;
        }

        hooks = conf->args_;
    }

    int nb_notify = config_->get_vhost_hls_nb_notify(req_->vhost_);
    for (int i = 0; i < (int)hooks.size(); i++) {
        std::string url = hooks.at(i);
        if ((err = hooks_->on_hls_notify(cid_, url, req_, ts_url_, nb_notify)) != srs_success) {
            return srs_error_wrap(err, "callback on_hls_notify %s", url.c_str());
        }
    }

    return err;
}

string SrsDvrAsyncCallOnHlsNotify::to_string()
{
    return "on_hls_notify: " + ts_url_;
}

SrsHlsFmp4Muxer::SrsHlsFmp4Muxer()
{
    req_ = NULL;
    hls_fragment_ = hls_window_ = 0;
    hls_aof_ratio_ = 1.0;
    deviation_ts_ = 0;
    hls_cleanup_ = true;
    hls_wait_keyframe_ = true;
    previous_floor_ts_ = 0;
    accept_floor_ts_ = 0;
    hls_ts_floor_ = false;
    max_td_ = 0;
    writer_ = NULL;
    sequence_no_ = 0;
    current_ = NULL;
    hls_keys_ = false;
    hls_fragments_per_key_ = 0;
    async_ = new SrsAsyncCallWorker();
    segments_ = new SrsFragmentWindow();
    latest_acodec_ = SrsAudioCodecIdForbidden;
    latest_vcodec_ = SrsVideoCodecIdForbidden;
    video_track_id_ = 0;
    audio_track_id_ = 0;
    init_mp4_ready_ = false;
    video_dts_ = 0;

    memset(key_, 0, 16);
    memset(iv_, 0, 16);

    config_ = _srs_config;
    app_factory_ = _srs_app_factory;
}

SrsHlsFmp4Muxer::~SrsHlsFmp4Muxer()
{
    srs_freep(segments_);
    srs_freep(current_);
    srs_freep(req_);
    srs_freep(async_);
    srs_freep(writer_);

    config_ = NULL;
    app_factory_ = NULL;
}

void SrsHlsFmp4Muxer::dispose()
{
    srs_error_t err = srs_success;

    segments_->dispose();

    if (current_) {
        if ((err = current_->unlink_tmpfile()) != srs_success) {
            srs_warn("Unlink tmp ts failed %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
        srs_freep(current_);
    }

    SrsUniquePtr<SrsPath> path(app_factory_->create_path());
    if (path->exists(m3u8_)) {
        if ((err = path->unlink(m3u8_)) != srs_success) {
            srs_warn("dispose: ignore remove m3u8 failed, %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
    }

    srs_trace("gracefully dispose hls %s", req_ ? req_->get_stream_url().c_str() : "");
}

int SrsHlsFmp4Muxer::sequence_no()
{
    return sequence_no_;
}

std::string SrsHlsFmp4Muxer::m4s_url()
{
    return current_ ? current_->uri_ : "";
}

srs_utime_t SrsHlsFmp4Muxer::duration()
{
    return current_ ? current_->duration() : 0;
}

int SrsHlsFmp4Muxer::deviation()
{
    // no floor, no deviation.
    if (!hls_ts_floor_) {
        return 0;
    }

    return deviation_ts_;
}

SrsAudioCodecId SrsHlsFmp4Muxer::latest_acodec()
{
    return latest_acodec_;
}

void SrsHlsFmp4Muxer::set_latest_acodec(SrsAudioCodecId v)
{
    latest_acodec_ = v;
}

SrsVideoCodecId SrsHlsFmp4Muxer::latest_vcodec()
{
    return latest_vcodec_;
}

void SrsHlsFmp4Muxer::set_latest_vcodec(SrsVideoCodecId v)
{
    latest_vcodec_ = v;
}

// CRITICAL: This method is called AFTER the source has been added to the source pool
// in the fetch_or_create pattern (see PR 4449).
//
// IMPORTANT: All field initialization in this method MUST NOT cause coroutine context switches.
// This prevents the race condition where multiple coroutines could create duplicate sources
// for the same stream when context switches occurred during initialization.
srs_error_t SrsHlsFmp4Muxer::initialize(int v_tid, int a_tid)
{
    video_track_id_ = v_tid;
    audio_track_id_ = a_tid;

    return srs_success;
}

srs_error_t SrsHlsFmp4Muxer::on_publish(ISrsRequest *req)
{
    srs_error_t err = srs_success;

    if ((err = async_->start()) != srs_success) {
        return srs_error_wrap(err, "async start");
    }

    return err;
}

srs_error_t SrsHlsFmp4Muxer::write_init_mp4(SrsFormat *format, bool has_video, bool has_audio)
{
    srs_error_t err = srs_success;

    std::string vhost = req_->vhost_;
    std::string stream = req_->stream_;
    std::string app = req_->app_;

    // Get init.mp4 file template from configuration
    std::string init_file = config_->get_hls_init_file(vhost);
    init_file = srs_path_build_stream(init_file, vhost, app, stream);

    std::string hls_path = config_->get_hls_path(vhost);
    std::string filepath = hls_path + "/" + init_file;

    // Create directory for the init file
    SrsPath path;
    std::string init_dir = path.filepath_dir(filepath);
    if ((err = path.mkdir_all(init_dir)) != srs_success) {
        return srs_error_wrap(err, "Create init mp4 dir failed, dir=%s", init_dir.c_str());
    }

    SrsUniquePtr<SrsInitMp4Segment> init_mp4(new SrsInitMp4Segment(writer_));

    init_mp4->set_path(filepath);

    if (hls_keys_) {
        init_mp4->config_cipher(kid_, iv_, 16);
    }

    if (has_video && has_audio) {
        if ((err = init_mp4->write(format, video_track_id_, audio_track_id_)) != srs_success) {
            return srs_error_wrap(err, "write hls init.mp4 with audio and video");
        }
    } else if (has_video) {
        if ((err = init_mp4->write_video_only(format, video_track_id_)) != srs_success) {
            return srs_error_wrap(err, "write hls init.mp4 with video only");
        }
    } else if (has_audio) {
        if ((err = init_mp4->write_audio_only(format, audio_track_id_)) != srs_success) {
            return srs_error_wrap(err, "write hls init.mp4 with audio only");
        }
    } else {
        return srs_error_new(ERROR_HLS_WRITE_FAILED, "no video and no audio sequence header");
    }

    if ((err = init_mp4->rename()) != srs_success) {
        return srs_error_wrap(err, "rename hls init.mp4");
    }

    // the ts url, relative or absolute url.
    // TODO: FIXME: Use url and path manager.
    std::string mp4_path = init_mp4->fullpath();
    if (srs_strings_starts_with(mp4_path, m3u8_dir_)) {
        mp4_path = mp4_path.substr(m3u8_dir_.length());
    }
    while (srs_strings_starts_with(mp4_path, "/")) {
        mp4_path = mp4_path.substr(1);
    }

    string init_mp4_uri = hls_entry_prefix_;
    if (!hls_entry_prefix_.empty() && !srs_strings_ends_with(hls_entry_prefix_, "/")) {
        init_mp4_uri += "/";

        // add the http dir to uri.
        string http_dir = path.filepath_dir(m3u8_url_);
        if (!http_dir.empty()) {
            init_mp4_uri += http_dir + "/";
        }
    }
    init_mp4_uri += mp4_path;

    // Convert to relative URI for m3u8 playlist.
    // TODO: Need to resolve the relative URI from m3u8 and init file.
    init_mp4_uri_ = path.filepath_base(init_file);

    // use async to call the http hooks, for it will cause thread switch.
    if ((err = async_->execute(new SrsDvrAsyncCallOnHls(_srs_context->get_id(), req_, init_mp4->fullpath(),
                                                        init_mp4_uri, m3u8_, m3u8_url_, 0, 0))) != srs_success) {
        return srs_error_wrap(err, "segment close");
    }

    // use async to call the http hooks, for it will cause thread switch.
    if ((err = async_->execute(new SrsDvrAsyncCallOnHlsNotify(_srs_context->get_id(), req_, init_mp4_uri))) != srs_success) {
        return srs_error_wrap(err, "segment close");
    }

    init_mp4_ready_ = true;
    return err;
}

srs_error_t SrsHlsFmp4Muxer::write_audio(SrsMediaPacket *shared_audio, SrsFormat *format)
{
    srs_error_t err = srs_success;

    if (!current_) {
        if ((err = segment_open(shared_audio->timestamp_ * SRS_UTIME_MILLISECONDS)) != srs_success) {
            return srs_error_wrap(err, "open segment");
        }
    }

    if (current_->duration() >= hls_fragment_) {
        if ((err = segment_close()) != srs_success) {
            return srs_error_wrap(err, "segment close");
        }

        if ((err = segment_open(shared_audio->timestamp_ * SRS_UTIME_MILLISECONDS)) != srs_success) {
            return srs_error_wrap(err, "open segment");
        }
    }

    current_->write(shared_audio, format);
    return err;
}

srs_error_t SrsHlsFmp4Muxer::write_video(SrsMediaPacket *shared_video, SrsFormat *format)
{
    srs_error_t err = srs_success;

    video_dts_ = shared_video->timestamp_;

    if (!current_) {
        if ((err = segment_open(shared_video->timestamp_ * SRS_UTIME_MILLISECONDS)) != srs_success) {
            return srs_error_wrap(err, "open segment");
        }
    }

    bool reopen = current_->duration() >= hls_fragment_;
    if (reopen) {
        if ((err = segment_close()) != srs_success) {
            return srs_error_wrap(err, "segment close");
        }

        if ((err = segment_open(shared_video->timestamp_ * SRS_UTIME_MILLISECONDS)) != srs_success) {
            return srs_error_wrap(err, "open segment");
        }
    }

    current_->write(shared_video, format);

    return err;
}

srs_error_t SrsHlsFmp4Muxer::on_unpublish()
{
    async_->stop();
    return srs_success;
}

srs_error_t SrsHlsFmp4Muxer::update_config(ISrsRequest *r)
{
    srs_error_t err = srs_success;

    srs_freep(req_);
    req_ = r->copy();

    std::string vhost = req_->vhost_;
    std::string stream = req_->stream_;
    std::string app = req_->app_;

    hls_fragment_ = config_->get_hls_fragment(vhost);
    double hls_td_ratio = config_->get_hls_td_ratio(vhost);
    hls_window_ = config_->get_hls_window(vhost);

    // get the hls m3u8 ts list entry prefix config
    hls_entry_prefix_ = config_->get_hls_entry_prefix(vhost);
    // get the hls path config
    hls_path_ = config_->get_hls_path(vhost);
    m3u8_url_ = config_->get_hls_m3u8_file(vhost);
    hls_m4s_file_ = config_->get_hls_fmp4_file(vhost);
    hls_cleanup_ = config_->get_hls_cleanup(vhost);
    hls_wait_keyframe_ = config_->get_hls_wait_keyframe(vhost);
    // the audio overflow, for pure audio to reap segment.
    hls_aof_ratio_ = config_->get_hls_aof_ratio(vhost);
    // whether use floor(timestamp/hls_fragment) for variable timestamp
    hls_ts_floor_ = config_->get_hls_ts_floor(vhost);

    hls_keys_ = config_->get_hls_keys(vhost);
    hls_fragments_per_key_ = config_->get_hls_fragments_per_key(vhost);
    hls_key_file_ = config_->get_hls_key_file(vhost);
    hls_key_file_path_ = config_->get_hls_key_file_path(vhost);
    hls_key_url_ = config_->get_hls_key_url(vhost);

    previous_floor_ts_ = 0;
    accept_floor_ts_ = 0;
    deviation_ts_ = 0;

    // generate the m3u8 dir and path.
    m3u8_url_ = srs_path_build_stream(m3u8_url_, vhost, app, stream);
    m3u8_ = hls_path_ + "/" + m3u8_url_;

    // when update config, reset the history target duration.
    max_td_ = hls_fragment_ * hls_td_ratio;

    // create m3u8 dir once.
    if ((err = create_directories()) != srs_success) {
        return srs_error_wrap(err, "create dir");
    }

    writer_ = app_factory_->create_file_writer();

    return err;
}

// LCOV_EXCL_START
srs_error_t SrsHlsFmp4Muxer::create_directories()
{
    srs_error_t err = srs_success;

    SrsPath path;
    m3u8_dir_ = path.filepath_dir(m3u8_);
    if ((err = path.mkdir_all(m3u8_dir_)) != srs_success) {
        return srs_error_wrap(err, "create dir");
    }

    if (hls_keys_ && (hls_path_ != hls_key_file_path_)) {
        string key_file = srs_path_build_stream(hls_key_file_, req_->vhost_, req_->app_, req_->stream_);
        string key_url = hls_key_file_path_ + "/" + key_file;
        string key_dir = path.filepath_dir(key_url);
        if ((err = path.mkdir_all(key_dir)) != srs_success) {
            return srs_error_wrap(err, "create dir");
        }
    }

    return err;
}
// LCOV_EXCL_STOP

srs_error_t SrsHlsFmp4Muxer::segment_open(srs_utime_t basetime)
{
    srs_error_t err = srs_success;

    if (current_) {
        srs_warn("ignore the segment open, for segment is already open.");
        return err;
    }

    // new segment.
    current_ = new SrsHlsM4sSegment(writer_);
    current_->sequence_no_ = sequence_no_++;

    if ((err = write_hls_key()) != srs_success) {
        return srs_error_wrap(err, "write hls key");
    }

    // generate filename.
    std::string m4s_file = generate_m4s_filename();

    std::string m4s_path = hls_path_ + "/" + m4s_file;
    current_->set_path(m4s_path);

    // the ts url, relative or absolute url.
    // TODO: FIXME: Use url and path manager.
    std::string m4s_url = current_->fullpath();
    if (srs_strings_starts_with(m4s_url, m3u8_dir_)) {
        m4s_url = m4s_url.substr(m3u8_dir_.length());
    }
    while (srs_strings_starts_with(m4s_url, "/")) {
        m4s_url = m4s_url.substr(1);
    }

    current_->uri_ += hls_entry_prefix_;
    if (!hls_entry_prefix_.empty() && !srs_strings_ends_with(hls_entry_prefix_, "/")) {
        current_->uri_ += "/";

        // add the http dir to uri.
        SrsPath path;
        string http_dir = path.filepath_dir(m3u8_url_);
        if (!http_dir.empty()) {
            current_->uri_ += http_dir + "/";
        }
    }
    current_->uri_ += m4s_url;

    current_->initialize(basetime, video_track_id_, audio_track_id_, sequence_no_, m4s_path);

    return err;
}

std::string SrsHlsFmp4Muxer::generate_m4s_filename()
{

    std::string m4s_file = hls_m4s_file_;

    m4s_file = srs_path_build_stream(m4s_file, req_->vhost_, req_->app_, req_->stream_);

    if (hls_ts_floor_) {
        // accept the floor ts for the first piece.
        int64_t current_floor_ts = srs_time_now_realtime() / hls_fragment_;
        if (!accept_floor_ts_) {
            accept_floor_ts_ = current_floor_ts - 1;
        } else {
            accept_floor_ts_++;
        }

        // jump when deviation more than 10p
        if (accept_floor_ts_ - current_floor_ts > SRS_JUMP_WHEN_PIECE_DEVIATION) {
            srs_warn("hls: jmp for ts deviation, current=%" PRId64 ", accept=%" PRId64, current_floor_ts, accept_floor_ts_);
            accept_floor_ts_ = current_floor_ts - 1;
        }

        // when reap ts, adjust the deviation.
        deviation_ts_ = (int)(accept_floor_ts_ - current_floor_ts);

        // dup/jmp detect for ts in floor mode.
        if (previous_floor_ts_ && previous_floor_ts_ != current_floor_ts - 1) {
            srs_warn("hls: dup/jmp ts, previous=%" PRId64 ", current=%" PRId64 ", accept=%" PRId64 ", deviation=%d",
                     previous_floor_ts_, current_floor_ts, accept_floor_ts_, deviation_ts_);
        }
        previous_floor_ts_ = current_floor_ts;

        // we always ensure the piece is increase one by one.
        std::stringstream ts_floor;
        ts_floor << accept_floor_ts_;
        m4s_file = srs_strings_replace(m4s_file, "[timestamp]", ts_floor.str());

        // TODO: FIMXE: we must use the accept ts floor time to generate the hour variable.
        m4s_file = srs_path_build_timestamp(m4s_file);
    } else {
        m4s_file = srs_path_build_timestamp(m4s_file);
    }

    if (true) {
        std::stringstream ss;
        ss << current_->sequence_no_;
        m4s_file = srs_strings_replace(m4s_file, "[seq]", ss.str());
    }

    return m4s_file;
}

bool SrsHlsFmp4Muxer::is_segment_open()
{
    return current_ != NULL;
}

srs_error_t SrsHlsFmp4Muxer::on_sequence_header()
{
    return srs_success;
}

bool SrsHlsFmp4Muxer::is_segment_overflow()
{
    // If segment is not open, it cannot overflow.
    if (!is_segment_open()) {
        return false;
    }

    // to prevent very small segment.
    if (current_->duration() < 2 * SRS_HLS_SEGMENT_MIN_DURATION) {
        return false;
    }

    // Use N% deviation, to smoother.
    srs_utime_t deviation = hls_ts_floor_ ? SRS_HLS_FLOOR_REAP_PERCENT * deviation_ts_ * hls_fragment_ : 0;

    // Keep in mind that we use max_td for the base duration, not the hls_fragment. To calculate
    // max_td, multiply hls_fragment by hls_td_ratio.
    return current_->duration() >= max_td_ + deviation;
}

bool SrsHlsFmp4Muxer::wait_keyframe()
{
    return hls_wait_keyframe_;
}

bool SrsHlsFmp4Muxer::is_segment_absolutely_overflow()
{
    // If segment is not open, it cannot overflow.
    if (!is_segment_open()) {
        return false;
    }

    // to prevent very small segment.
    if (current_->duration() < 2 * SRS_HLS_SEGMENT_MIN_DURATION) {
        return false;
    }

    // use N% deviation, to smoother.
    srs_utime_t deviation = hls_ts_floor_ ? SRS_HLS_FLOOR_REAP_PERCENT * deviation_ts_ * hls_fragment_ : 0;
    return current_->duration() >= hls_aof_ratio_ * hls_fragment_ + deviation;
}

void SrsHlsFmp4Muxer::update_duration(uint64_t dts)
{
    // If segment is not open, ignore the update event.
    if (!is_segment_open()) {
        return;
    }
    current_->append(dts / 90);
}

srs_error_t SrsHlsFmp4Muxer::segment_close()
{
    srs_error_t err = do_segment_close();

    return err;
}

srs_error_t SrsHlsFmp4Muxer::do_segment_close()
{
    srs_error_t err = srs_success;

    if (!current_) {
        srs_warn("ignore the segment close, for segment is not open.");
        return err;
    }

    if ((err = current_->reap(video_dts_)) != srs_success) {
        return srs_error_wrap(err, "reap segment");
    }

    // use async to call the http hooks, for it will cause thread switch.
    if ((err = async_->execute(new SrsDvrAsyncCallOnHls(_srs_context->get_id(), req_, current_->fullpath(),
                                                        current_->uri_, m3u8_, m3u8_url_, current_->sequence_no_, current_->duration()))) != srs_success) {
        return srs_error_wrap(err, "segment close");
    }

    // use async to call the http hooks, for it will cause thread switch.
    if ((err = async_->execute(new SrsDvrAsyncCallOnHlsNotify(_srs_context->get_id(), req_, current_->uri_))) != srs_success) {
        return srs_error_wrap(err, "segment close");
    }

    segments_->append(current_);
    current_ = NULL;

    // shrink the segments.
    segments_->shrink(hls_window_);

    // refresh the m3u8, donot contains the removed ts
    if ((err = refresh_m3u8()) != srs_success) {
        return srs_error_wrap(err, "refresh m3u8");
    }

    // remove the ts file.
    segments_->clear_expired(hls_cleanup_);

    return err;
}

srs_error_t SrsHlsFmp4Muxer::write_hls_key()
{
    srs_error_t err = srs_success;

    if (hls_keys_ && current_->sequence_no_ % hls_fragments_per_key_ == 0) {
        if (RAND_bytes(key_, 16) < 0) {
            return srs_error_wrap(err, "rand key failed.");
        }
        if (RAND_bytes(kid_, 16) < 0) {
            return srs_error_wrap(err, "rand kid failed.");
        }
        if (RAND_bytes(iv_, 16) < 0) {
            return srs_error_wrap(err, "rand iv failed.");
        }

        string key_file = srs_path_build_stream(hls_key_file_, req_->vhost_, req_->app_, req_->stream_);
        key_file = srs_strings_replace(key_file, "[seq]", srs_strconv_format_int(current_->sequence_no_));
        string key_url = hls_key_file_path_ + "/" + key_file;

        SrsUniquePtr<ISrsFileWriter> fw(app_factory_->create_file_writer());
        if ((err = fw->open(key_url)) != srs_success) {
            return srs_error_wrap(err, "open file %s", key_url.c_str());
        }

        err = fw->write(key_, 16, NULL);
        fw->close();

        if (err != srs_success) {
            return srs_error_wrap(err, "write key");
        }
    }

    if (hls_keys_) {
        current_->config_cipher(key_, iv_);
    }

    return err;
}

// LCOV_EXCL_START
srs_error_t SrsHlsFmp4Muxer::refresh_m3u8()
{
    srs_error_t err = srs_success;

    // no segments, also no m3u8, return.
    if (segments_->empty()) {
        return err;
    }

    std::string temp_m3u8 = m3u8_ + ".temp";
    if ((err = do_refresh_m3u8(temp_m3u8)) == srs_success) {
        if (rename(temp_m3u8.c_str(), m3u8_.c_str()) < 0) {
            err = srs_error_new(ERROR_HLS_WRITE_FAILED, "hls: rename m3u8 file failed. %s => %s", temp_m3u8.c_str(), m3u8_.c_str());
        }
    }

    // remove the temp file.
    SrsUniquePtr<SrsPath> path(app_factory_->create_path());
    if (path->exists(temp_m3u8)) {
        if ((err = path->unlink(temp_m3u8)) != srs_success) {
            srs_warn("refresh: ignore remove m3u8 failed, %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
    }

    return err;
}
// LCOV_EXCL_STOP

srs_error_t SrsHlsFmp4Muxer::do_refresh_m3u8(std::string m3u8_file)
{
    srs_error_t err = srs_success;

    // no segments, return.
    if (segments_->empty()) {
        return err;
    }

    SrsUniquePtr<ISrsFileWriter> writer(app_factory_->create_file_writer());
    if ((err = writer->open(m3u8_file)) != srs_success) {
        return srs_error_wrap(err, "hls: open m3u8 file %s", m3u8_file.c_str());
    }

    // #EXTM3U\n
    // #EXT-X-VERSION:3\n
    std::stringstream ss;
    ss << "#EXTM3U" << SRS_CONSTS_LF;
    // TODO: for fmp4 set #EXT-X-VERSION:7, need support tag #EXT-X-MAP:URI="init.mp4", which
    // at least version:5
    // DOC: https://developer.apple.com/documentation/http-live-streaming/about-the-ext-x-version-tag
    ss << "#EXT-X-VERSION:7" << SRS_CONSTS_LF;

    // #EXT-X-MEDIA-SEQUENCE:4294967295\n
    SrsHlsM4sSegment *first = dynamic_cast<SrsHlsM4sSegment *>(segments_->first());
    if (first == NULL) {
        return srs_error_new(ERROR_HLS_WRITE_FAILED, "segments cast");
    }

    ss << "#EXT-X-MEDIA-SEQUENCE:" << first->sequence_no_ << SRS_CONSTS_LF;

    // #EXT-X-TARGETDURATION:4294967295\n
    /**
     * @see hls-m3u8-draft-pantos-http-live-streaming-12.pdf, page 25
     * The Media Playlist file MUST contain an EXT-X-TARGETDURATION tag.
     * Its value MUST be equal to or greater than the EXTINF duration of any
     * media segment that appears or will appear in the Playlist file,
     * rounded to the nearest integer. Its value MUST NOT change. A
     * typical target duration is 10 seconds.
     */
    srs_utime_t max_duration = segments_->max_duration();
    int target_duration = (int)ceil(srsu2msi(srs_max(max_duration, max_td_)) / 1000.0);

    ss << "#EXT-X-TARGETDURATION:" << target_duration << SRS_CONSTS_LF;

    // TODO: add #EXT-X-MAP:URI="init.mp4" for fmp4
    ss << "#EXT-X-MAP:URI=\"" << init_mp4_uri_ << "\"" << SRS_CONSTS_LF;

    // write all segments
    for (int i = 0; i < segments_->size(); i++) {
        SrsHlsM4sSegment *segment = dynamic_cast<SrsHlsM4sSegment *>(segments_->at(i));
        if ((err = do_refresh_m3u8_segment(segment, ss)) != srs_success) {
            return srs_error_wrap(err, "hls: refresh m3u8 segment");
        }
    }

    // write m3u8 to writer.
    std::string m3u8 = ss.str();
    if ((err = writer->write((char *)m3u8.c_str(), (int)m3u8.length(), NULL)) != srs_success) {
        return srs_error_wrap(err, "hls: write m3u8");
    }

    return err;
}

srs_error_t SrsHlsFmp4Muxer::do_refresh_m3u8_segment(SrsHlsM4sSegment *segment, std::stringstream &ss)
{
    srs_error_t err = srs_success;

    if (segment->is_sequence_header()) {
        // #EXT-X-DISCONTINUITY\n
        ss << "#EXT-X-DISCONTINUITY" << SRS_CONSTS_LF;
    }

    if (hls_keys_ && ((segment->sequence_no_ % hls_fragments_per_key_) == 0)) {
        char hexiv[33];
        srs_hex_encode_to_string(hexiv, segment->iv_, 16);
        hexiv[32] = '\0';

        string key_file = srs_path_build_stream(hls_key_file_, req_->vhost_, req_->app_, req_->stream_);
        key_file = srs_strings_replace(key_file, "[seq]", srs_strconv_format_int(segment->sequence_no_));

        string key_path = key_file;
        // if key_url is not set,only use the file name
        if (!hls_key_url_.empty()) {
            key_path = hls_key_url_ + key_file;
        }

        ss << "#EXT-X-KEY:METHOD=SAMPLE-AES,URI=" << "\"" << key_path << "\",IV=0x" << hexiv << SRS_CONSTS_LF;
    }

    // "#EXTINF:4294967295.208,\n"
    ss.precision(3);
    ss.setf(std::ios::fixed, std::ios::floatfield);
    ss << "#EXTINF:" << srsu2msi(segment->duration()) / 1000.0 << ", no desc" << SRS_CONSTS_LF;

    // {file name}\n
    // TODO get segment name in relative path.
    SrsPath path;
    std::string seg_uri = segment->fullpath();
    if (true) {
        std::stringstream stemp;
        stemp << srsu2msi(segment->duration());
        seg_uri = srs_strings_replace(seg_uri, "[duration]", stemp.str());
    }
    // ss << segment->uri << SRS_CONSTS_LF;
    ss << path.filepath_base(seg_uri) << SRS_CONSTS_LF;
    
    return err;
}

ISrsHlsMuxer::ISrsHlsMuxer()
{
}

ISrsHlsMuxer::~ISrsHlsMuxer()
{
}

SrsHlsMuxer::SrsHlsMuxer()
{
    req_ = NULL;
    hls_fragment_ = hls_window_ = 0;
    hls_aof_ratio_ = 1.0;
    deviation_ts_ = 0;
    hls_cleanup_ = true;
    hls_wait_keyframe_ = true;
    previous_floor_ts_ = 0;
    accept_floor_ts_ = 0;
    hls_ts_floor_ = false;
    max_td_ = 0;
    writer_ = NULL;
    sequence_no_ = 0;
    current_ = NULL;
    hls_keys_ = false;
    hls_fragments_per_key_ = 0;
    async_ = new SrsAsyncCallWorker();
    context_ = new SrsTsContext();
    segments_ = new SrsFragmentWindow();
    latest_acodec_ = SrsAudioCodecIdForbidden;
    latest_vcodec_ = SrsVideoCodecIdForbidden;

    memset(key_, 0, 16);
    memset(iv_, 0, 16);

    config_ = _srs_config;
    app_factory_ = _srs_app_factory;
}

SrsHlsMuxer::~SrsHlsMuxer()
{
    srs_freep(segments_);
    srs_freep(current_);
    srs_freep(req_);
    srs_freep(async_);
    srs_freep(context_);
    srs_freep(writer_);

    config_ = NULL;
    app_factory_ = NULL;
}

// LCOV_EXCL_START
void SrsHlsMuxer::dispose()
{
    srs_error_t err = srs_success;

    segments_->dispose();

    if (current_) {
        if ((err = current_->unlink_tmpfile()) != srs_success) {
            srs_warn("Unlink tmp ts failed %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
        srs_freep(current_);
    }

    SrsUniquePtr<SrsPath> path(app_factory_->create_path());
    if (path->exists(m3u8_)) {
        if ((err = path->unlink(m3u8_)) != srs_success) {
            srs_warn("dispose: ignore remove m3u8 failed, %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
    }

    srs_trace("gracefully dispose hls %s", req_ ? req_->get_stream_url().c_str() : "");
}
// LCOV_EXCL_STOP

int SrsHlsMuxer::sequence_no()
{
    return sequence_no_;
}

string SrsHlsMuxer::ts_url()
{
    return current_ ? current_->uri_ : "";
}

srs_utime_t SrsHlsMuxer::duration()
{
    return current_ ? current_->duration() : 0;
}

int SrsHlsMuxer::deviation()
{
    // no floor, no deviation.
    if (!hls_ts_floor_) {
        return 0;
    }

    return deviation_ts_;
}

SrsAudioCodecId SrsHlsMuxer::latest_acodec()
{
    // If current context writer exists, we query from it.
    if (current_ && current_->tscw_)
        return current_->tscw_->acodec();

    // Get the configured or updated config.
    return latest_acodec_;
}

void SrsHlsMuxer::set_latest_acodec(SrsAudioCodecId v)
{
    // Refresh the codec in context writer for current segment.
    if (current_ && current_->tscw_)
        current_->tscw_->set_acodec(v);

    // Refresh the codec for future segments.
    latest_acodec_ = v;
}

SrsVideoCodecId SrsHlsMuxer::latest_vcodec()
{
    // If current context writer exists, we query from it.
    if (current_ && current_->tscw_)
        return current_->tscw_->vcodec();

    // Get the configured or updated config.
    return latest_vcodec_;
}

void SrsHlsMuxer::set_latest_vcodec(SrsVideoCodecId v)
{
    // Refresh the codec in context writer for current segment.
    if (current_ && current_->tscw_)
        current_->tscw_->set_vcodec(v);

    // Refresh the codec for future segments.
    latest_vcodec_ = v;
}

// CRITICAL: This method is called AFTER the source has been added to the source pool
// in the fetch_or_create pattern (see PR 4449).
//
// IMPORTANT: All field initialization in this method MUST NOT cause coroutine context switches.
// This prevents the race condition where multiple coroutines could create duplicate sources
// for the same stream when context switches occurred during initialization.
srs_error_t SrsHlsMuxer::initialize()
{
    return srs_success;
}

srs_error_t SrsHlsMuxer::on_publish(ISrsRequest *req)
{
    srs_error_t err = srs_success;

    if ((err = async_->start()) != srs_success) {
        return srs_error_wrap(err, "async start");
    }

    return err;
}

srs_error_t SrsHlsMuxer::on_unpublish()
{
    async_->stop();
    return srs_success;
}

srs_error_t SrsHlsMuxer::update_config(ISrsRequest *r, string entry_prefix,
                                       string path, string m3u8_file, string ts_file, srs_utime_t fragment, srs_utime_t window,
                                       bool ts_floor, double aof_ratio, bool cleanup, bool wait_keyframe, bool keys,
                                       int fragments_per_key, string key_file, string key_file_path, string key_url)
{
    srs_error_t err = srs_success;

    srs_freep(req_);
    req_ = r->copy();

    hls_entry_prefix_ = entry_prefix;
    hls_path_ = path;
    hls_ts_file_ = ts_file;
    hls_fragment_ = fragment;
    hls_aof_ratio_ = aof_ratio;
    hls_ts_floor_ = ts_floor;
    hls_cleanup_ = cleanup;
    hls_wait_keyframe_ = wait_keyframe;
    previous_floor_ts_ = 0;
    accept_floor_ts_ = 0;
    hls_window_ = window;
    deviation_ts_ = 0;

    hls_keys_ = keys;
    hls_fragments_per_key_ = fragments_per_key;
    hls_key_file_ = key_file;
    hls_key_file_path_ = key_file_path;
    hls_key_url_ = key_url;

    // generate the m3u8 dir and path.
    m3u8_url_ = srs_path_build_stream(m3u8_file, req_->vhost_, req_->app_, req_->stream_);
    m3u8_ = path + "/" + m3u8_url_;

    // when update config, reset the history target duration.
    max_td_ = fragment * config_->get_hls_td_ratio(r->vhost_);

    if ((err = create_directories()) != srs_success) {
        return srs_error_wrap(err, "create dir");
    }

    if (hls_keys_) {
        writer_ = app_factory_->create_enc_file_writer();
    } else {
        writer_ = app_factory_->create_file_writer();
    }

    return err;
}

// LCOV_EXCL_START
srs_error_t SrsHlsMuxer::create_directories()
{
    srs_error_t err = srs_success;

    // create m3u8 dir once.
    SrsPath path_util;
    m3u8_dir_ = path_util.filepath_dir(m3u8_);
    if ((err = path_util.mkdir_all(m3u8_dir_)) != srs_success) {
        return srs_error_wrap(err, "create dir");
    }

    if (hls_keys_ && (hls_path_ != hls_key_file_path_)) {
        string key_file = srs_path_build_stream(hls_key_file_, req_->vhost_, req_->app_, req_->stream_);
        string key_url = hls_key_file_path_ + "/" + key_file;
        string key_dir = path_util.filepath_dir(key_url);
        if ((err = path_util.mkdir_all(key_dir)) != srs_success) {
            return srs_error_wrap(err, "create dir");
        }
    }

    return err;
}
// LCOV_EXCL_STOP

srs_error_t SrsHlsMuxer::recover_hls()
{
    srs_error_t err = srs_success;

    // exist the m3u8 file.
    SrsUniquePtr<SrsPath> path(app_factory_->create_path());
    if (!path->exists(m3u8_)) {
        return err;
    }

    return do_recover_hls();
}

srs_error_t SrsHlsMuxer::do_recover_hls()
{
    srs_error_t err = srs_success;

    srs_trace("hls: recover stream m3u8=%s, m3u8_url=%s, hls_path=%s",
              m3u8_.c_str(), m3u8_url_.c_str(), hls_path_.c_str());

    // read whole m3u8 file content as a string
    SrsUniquePtr<ISrsFileReader> fr(app_factory_->create_file_reader());
    if ((err = fr->open(m3u8_)) != srs_success) {
        return srs_error_wrap(err, "open file");
    }

    std::string body;
    if ((err = srs_io_readall(fr.get(), body)) != srs_success) {
        return srs_error_wrap(err, "read data");
    }
    if (body.empty()) {
        return srs_error_wrap(err, "read empty m3u8");
    }

    bool discon = false;

    std::string ptl;
    while (!body.empty()) {
        size_t pos = string::npos;

        std::string line;
        if ((pos = body.find("\n")) != string::npos) {
            line = body.substr(0, pos);
            body = body.substr(pos + 1);
        } else {
            line = body;
            body = "";
        }

        line = srs_strings_replace(line, "\r", "");
        line = srs_strings_replace(line, " ", "");

        // #EXT-X-VERSION:3
        // the version must be 3.0
        if (srs_strings_starts_with(line, "#EXT-X-VERSION:")) {
            if (!srs_strings_ends_with(line, ":3")) {
                srs_warn("m3u8 3.0 required, actual is %s", line.c_str());
            }
            continue;
        }

        // #EXT-X-PLAYLIST-TYPE:VOD
        // the playlist type, vod or nothing.
        if (srs_strings_starts_with(line, "#EXT-X-PLAYLIST-TYPE:")) {
            ptl = line;
            continue;
        }

        // #EXT-X-MEDIA-SEQUENCE:4294967295
        // the media sequence no.
        if (srs_strings_starts_with(line, "#EXT-X-MEDIA-SEQUENCE:")) {
            sequence_no_ = ::atof(line.substr(string("#EXT-X-MEDIA-SEQUENCE:").length()).c_str());
        }

        // #EXT-X-DISCONTINUITY
        // the discontinuity tag.
        if (srs_strings_starts_with(line, "#EXT-X-DISCONTINUITY")) {
            discon = true;
        }

        // #EXTINF:11.401,
        // livestream-5.ts
        // parse each ts entry, expect current line is inf.
        if (!srs_strings_starts_with(line, "#EXTINF:")) {
            continue;
        }

        // expect next line is url.
        std::string ts_url;
        if ((pos = body.find("\n")) != string::npos) {
            ts_url = body.substr(0, pos);
            body = body.substr(pos + 1);
        } else {
            srs_warn("ts entry unexpected eof, inf=%s", line.c_str());
            break;
        }

        // parse the ts duration.
        line = line.substr(string("#EXTINF:").length());
        if ((pos = line.find(",")) != string::npos) {
            line = line.substr(0, pos);
        }

        double ts_duration = ::atof(line.c_str());

        // Only create new segment if it doesn't already exist
        if (!segment_exists(ts_url)) {
            // load the default acodec, use the same logic as segment_open().
            SrsAudioCodecId default_acodec = SrsAudioCodecIdDisabled;

            // Now that we know the latest audio codec in stream, use it.
            if (latest_acodec_ != SrsAudioCodecIdForbidden)
                default_acodec = latest_acodec_;

            // load the default vcodec, use the same logic as segment_open().
            SrsVideoCodecId default_vcodec = SrsVideoCodecIdDisabled;

            // Now that we know the latest video codec in stream, use it.
            if (latest_vcodec_ != SrsVideoCodecIdForbidden)
                default_vcodec = latest_vcodec_;

            // new segment.
            SrsHlsSegment *seg = new SrsHlsSegment(context_, default_acodec, default_vcodec, writer_);
            seg->sequence_no_ = sequence_no_++;
            seg->set_path(hls_path_ + "/" + req_->app_ + "/" + ts_url);
            seg->uri_ = ts_url;
            seg->set_sequence_header(discon);

            seg->append(0);
            seg->append(ts_duration * 1000);

            segments_->append(seg);
        } else {
            // Segment already exists, just increment sequence number to maintain consistency
            sequence_no_++;
        }

        discon = false;
    }

    return err;
}

bool SrsHlsMuxer::segment_exists(const std::string &ts_url)
{
    for (int i = 0; i < segments_->size(); i++) {
        SrsHlsSegment *existing_seg = dynamic_cast<SrsHlsSegment *>(segments_->at(i));
        if (existing_seg && existing_seg->uri_ == ts_url) {
            return true;
        }
    }
    return false;
}

srs_error_t SrsHlsMuxer::segment_open()
{
    srs_error_t err = srs_success;

    if (current_) {
        srs_warn("ignore the segment open, for segment is already open.");
        return err;
    }

    // when segment open, the current segment must be NULL.
    srs_assert(!current_);

    // load the default acodec from config.
    SrsAudioCodecId default_acodec = SrsAudioCodecIdDisabled;

    // Now that we know the latest audio codec in stream, use it.
    if (latest_acodec_ != SrsAudioCodecIdForbidden)
        default_acodec = latest_acodec_;

    // load the default vcodec from config.
    SrsVideoCodecId default_vcodec = SrsVideoCodecIdDisabled;

    // Now that we know the latest video codec in stream, use it.
    if (latest_vcodec_ != SrsVideoCodecIdForbidden)
        default_vcodec = latest_vcodec_;

    // new segment.
    current_ = new SrsHlsSegment(context_, default_acodec, default_vcodec, writer_);
    current_->sequence_no_ = sequence_no_++;

    if ((err = write_hls_key()) != srs_success) {
        return srs_error_wrap(err, "write hls key");
    }

    // generate filename.
    std::string ts_file = generate_ts_filename();
    current_->set_path(hls_path_ + "/" + ts_file);

    // the ts url, relative or absolute url.
    // TODO: FIXME: Use url and path manager.
    std::string ts_url = current_->fullpath();
    if (srs_strings_starts_with(ts_url, m3u8_dir_)) {
        ts_url = ts_url.substr(m3u8_dir_.length());
    }
    while (srs_strings_starts_with(ts_url, "/")) {
        ts_url = ts_url.substr(1);
    }
    current_->uri_ += hls_entry_prefix_;
    if (!hls_entry_prefix_.empty() && !srs_strings_ends_with(hls_entry_prefix_, "/")) {
        current_->uri_ += "/";

        // add the http dir to uri.
        SrsPath path;
        string http_dir = path.filepath_dir(m3u8_url_);
        if (!http_dir.empty()) {
            current_->uri_ += http_dir + "/";
        }
    }
    current_->uri_ += ts_url;

    // create dir recursively for hls.
    if ((err = current_->create_dir()) != srs_success) {
        return srs_error_wrap(err, "create dir");
    }

    // open temp ts file.
    std::string tmp_file = current_->tmppath();
    if ((err = current_->writer_->open(tmp_file)) != srs_success) {
        return srs_error_wrap(err, "open hls muxer");
    }

    // reset the context for a new ts start.
    context_->reset();

    return err;
}

string SrsHlsMuxer::generate_ts_filename()
{
    std::string ts_file = hls_ts_file_;

    ts_file = srs_path_build_stream(ts_file, req_->vhost_, req_->app_, req_->stream_);

    if (hls_ts_floor_) {
        // accept the floor ts for the first piece.
        int64_t current_floor_ts = srs_time_now_realtime() / hls_fragment_;
        if (!accept_floor_ts_) {
            accept_floor_ts_ = current_floor_ts - 1;
        } else {
            accept_floor_ts_++;
        }

        // jump when deviation more than 10p
        if (accept_floor_ts_ - current_floor_ts > SRS_JUMP_WHEN_PIECE_DEVIATION) {
            srs_warn("hls: jmp for ts deviation, current=%" PRId64 ", accept=%" PRId64, current_floor_ts, accept_floor_ts_);
            accept_floor_ts_ = current_floor_ts - 1;
        }

        // when reap ts, adjust the deviation.
        deviation_ts_ = (int)(accept_floor_ts_ - current_floor_ts);

        // dup/jmp detect for ts in floor mode.
        if (previous_floor_ts_ && previous_floor_ts_ != current_floor_ts - 1) {
            srs_warn("hls: dup/jmp ts, previous=%" PRId64 ", current=%" PRId64 ", accept=%" PRId64 ", deviation=%d",
                     previous_floor_ts_, current_floor_ts, accept_floor_ts_, deviation_ts_);
        }
        previous_floor_ts_ = current_floor_ts;

        // we always ensure the piece is increase one by one.
        std::stringstream ts_floor;
        ts_floor << accept_floor_ts_;
        ts_file = srs_strings_replace(ts_file, "[timestamp]", ts_floor.str());

        // TODO: FIMXE: we must use the accept ts floor time to generate the hour variable.
        ts_file = srs_path_build_timestamp(ts_file);
    } else {
        ts_file = srs_path_build_timestamp(ts_file);
    }

    if (true) {
        std::stringstream ss;
        ss << current_->sequence_no_;
        ts_file = srs_strings_replace(ts_file, "[seq]", ss.str());
    }

    return ts_file;
}

srs_error_t SrsHlsMuxer::on_sequence_header()
{
    srs_error_t err = srs_success;

    // If segment is not open, ignore the sequence header event.
    if (!is_segment_open()) {
        srs_warn("sequence header ignored, for segment is not open.");
        return err;
    }

    // set the current segment to sequence header,
    // when close the segement, it will write a discontinuity to m3u8 file.
    current_->set_sequence_header(true);

    return err;
}

bool SrsHlsMuxer::is_segment_open()
{
    return current_ != NULL;
}

bool SrsHlsMuxer::is_segment_overflow()
{
    // If segment is not open, it cannot overflow.
    if (!is_segment_open()) {
        return false;
    }

    // to prevent very small segment.
    if (current_->duration() < 2 * SRS_HLS_SEGMENT_MIN_DURATION) {
        return false;
    }

    // Use N% deviation, to smoother.
    srs_utime_t deviation = hls_ts_floor_ ? SRS_HLS_FLOOR_REAP_PERCENT * deviation_ts_ * hls_fragment_ : 0;

    // Keep in mind that we use max_td for the base duration, not the hls_fragment. To calculate
    // max_td, multiply hls_fragment by hls_td_ratio.
    return current_->duration() >= max_td_ + deviation;
}

bool SrsHlsMuxer::wait_keyframe()
{
    return hls_wait_keyframe_;
}

bool SrsHlsMuxer::is_segment_absolutely_overflow()
{
    // If segment is not open, it cannot overflow.
    if (!is_segment_open()) {
        return false;
    }

    // to prevent very small segment.
    if (current_->duration() < 2 * SRS_HLS_SEGMENT_MIN_DURATION) {
        return false;
    }

    // use N% deviation, to smoother.
    srs_utime_t deviation = hls_ts_floor_ ? SRS_HLS_FLOOR_REAP_PERCENT * deviation_ts_ * hls_fragment_ : 0;
    return current_->duration() >= hls_aof_ratio_ * hls_fragment_ + deviation;
}

bool SrsHlsMuxer::pure_audio()
{
    return current_ && current_->tscw_ && current_->tscw_->vcodec() == SrsVideoCodecIdDisabled;
}

srs_error_t SrsHlsMuxer::flush_audio(SrsTsMessageCache *cache)
{
    srs_error_t err = srs_success;

    // if current is NULL, segment is not open, ignore the flush event.
    if (!current_) {
        srs_warn("flush audio ignored, for segment is not open.");
        return err;
    }

    if (!cache->audio_ || cache->audio_->payload_->length() <= 0) {
        return err;
    }

    // update the duration of segment.
    update_duration(cache->audio_->dts_);

    if ((err = current_->tscw_->write_audio(cache->audio_)) != srs_success) {
        return srs_error_wrap(err, "hls: write audio");
    }

    // write success, clear and free the msg
    srs_freep(cache->audio_);

    return err;
}

srs_error_t SrsHlsMuxer::flush_video(SrsTsMessageCache *cache)
{
    srs_error_t err = srs_success;

    // if current is NULL, segment is not open, ignore the flush event.
    if (!current_) {
        srs_warn("flush video ignored, for segment is not open.");
        return err;
    }

    if (!cache->video_ || cache->video_->payload_->length() <= 0) {
        return err;
    }

    srs_assert(current_);

    // update the duration of segment.
    update_duration(cache->video_->dts_);

    if ((err = current_->tscw_->write_video(cache->video_)) != srs_success) {
        return srs_error_wrap(err, "hls: write video");
    }

    // write success, clear and free the msg
    srs_freep(cache->video_);

    return err;
}

void SrsHlsMuxer::update_duration(uint64_t dts)
{
    // If segment is not open, ignore the update event.
    if (!is_segment_open()) {
        return;
    }
    current_->append(dts / 90);
}

srs_error_t SrsHlsMuxer::segment_close()
{
    srs_error_t err = do_segment_close();

    // We always cleanup current segment.
    srs_freep(current_);

    return err;
}

srs_error_t SrsHlsMuxer::do_segment_close()
{
    srs_error_t err = srs_success;

    if (!current_) {
        srs_warn("ignore the segment close, for segment is not open.");
        return err;
    }

    if ((err = do_segment_close2()) != srs_success) {
        return srs_error_wrap(err, "hls: do segment close");
    }

    // shrink the segments.
    segments_->shrink(hls_window_);

    // refresh the m3u8, donot contains the removed ts
    err = refresh_m3u8();

    // remove the ts file.
    segments_->clear_expired(hls_cleanup_);

    // check ret of refresh m3u8
    if (err != srs_success) {
        return srs_error_wrap(err, "hls: refresh m3u8");
    }

    return err;
}

srs_error_t SrsHlsMuxer::do_segment_close2()
{
    srs_error_t err = srs_success;

    // when close current segment, the current segment must not be NULL.
    srs_assert(current_);

    // We should always close the underlayer writer.
    if (current_ && current_->writer_) {
        current_->writer_->close();
    }

    // valid, add to segments if segment duration is ok
    // when too small, it maybe not enough data to play.
    // when too large, it maybe timestamp corrupt.
    // make the segment more acceptable, when in [min, max_td * 3], it's ok.
    bool matchMinDuration = current_->duration() >= SRS_HLS_SEGMENT_MIN_DURATION;
    bool matchMaxDuration = current_->duration() <= max_td_ * 3 * 1000;
    if (matchMinDuration && matchMaxDuration) {
        // rename from tmp to real path
        if ((err = current_->rename()) != srs_success) {
            return srs_error_wrap(err, "rename");
        }

        // use async to call the http hooks, for it will cause thread switch.
        if ((err = async_->execute(new SrsDvrAsyncCallOnHls(_srs_context->get_id(), req_, current_->fullpath(),
                                                            current_->uri_, m3u8_, m3u8_url_, current_->sequence_no_, current_->duration()))) != srs_success) {
            return srs_error_wrap(err, "segment close");
        }

        // use async to call the http hooks, for it will cause thread switch.
        if ((err = async_->execute(new SrsDvrAsyncCallOnHlsNotify(_srs_context->get_id(), req_, current_->uri_))) != srs_success) {
            return srs_error_wrap(err, "segment close");
        }

        // close the muxer of finished segment.
        srs_freep(current_->tscw_);

        segments_->append(current_);
        current_ = NULL;
    } else {
        // reuse current segment index.
        sequence_no_--;

        srs_trace("Drop ts segment, sequence_no=%d, uri=%s, duration=%dms",
                  current_->sequence_no_, current_->uri_.c_str(), srsu2msi(current_->duration()));

        // rename from tmp to real path
        if ((err = current_->unlink_tmpfile()) != srs_success) {
            return srs_error_wrap(err, "rename");
        }
    }

    return err;
}

srs_error_t SrsHlsMuxer::write_hls_key()
{
    srs_error_t err = srs_success;

    if (hls_keys_ && current_->sequence_no_ % hls_fragments_per_key_ == 0) {
        if (RAND_bytes(key_, 16) < 0) {
            return srs_error_wrap(err, "rand key failed.");
        }
        if (RAND_bytes(iv_, 16) < 0) {
            return srs_error_wrap(err, "rand iv failed.");
        }

        string key_file = srs_path_build_stream(hls_key_file_, req_->vhost_, req_->app_, req_->stream_);
        key_file = srs_strings_replace(key_file, "[seq]", srs_strconv_format_int(current_->sequence_no_));
        string key_url = hls_key_file_path_ + "/" + key_file;

        SrsUniquePtr<ISrsFileWriter> fw(app_factory_->create_file_writer());
        if ((err = fw->open(key_url)) != srs_success) {
            return srs_error_wrap(err, "open file %s", key_url.c_str());
        }

        err = fw->write(key_, 16, NULL);
        fw->close();

        if (err != srs_success) {
            return srs_error_wrap(err, "write key");
        }
    }

    if (hls_keys_) {
        current_->config_cipher(key_, iv_);
    }

    return err;
}

// LCOV_EXCL_START
srs_error_t SrsHlsMuxer::refresh_m3u8()
{
    srs_error_t err = srs_success;

    // no segments, also no m3u8, return.
    if (segments_->empty()) {
        return err;
    }

    std::string temp_m3u8 = m3u8_ + ".temp";
    if ((err = do_refresh_m3u8(temp_m3u8)) == srs_success) {
        if (rename(temp_m3u8.c_str(), m3u8_.c_str()) < 0) {
            err = srs_error_new(ERROR_HLS_WRITE_FAILED, "hls: rename m3u8 file failed. %s => %s", temp_m3u8.c_str(), m3u8_.c_str());
        }
    }

    // remove the temp file.
    SrsUniquePtr<SrsPath> path(app_factory_->create_path());
    if (path->exists(temp_m3u8)) {
        if ((err = path->unlink(temp_m3u8)) != srs_success) {
            srs_warn("refresh: ignore remove m3u8 failed, %s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
    }

    return err;
}
// LCOV_EXCL_STOP

srs_error_t SrsHlsMuxer::do_refresh_m3u8(string m3u8_file)
{
    srs_error_t err = srs_success;

    // no segments, return.
    if (segments_->empty()) {
        return err;
    }

    SrsUniquePtr<ISrsFileWriter> writer(app_factory_->create_file_writer());
    if ((err = writer->open(m3u8_file)) != srs_success) {
        return srs_error_wrap(err, "hls: open m3u8 file %s", m3u8_file.c_str());
    }

    // #EXTM3U\n
    // #EXT-X-VERSION:3\n
    std::stringstream ss;
    ss << "#EXTM3U" << SRS_CONSTS_LF;
    // TODO: for fmp4 set #EXT-X-VERSION:7, need support tag #EXT-X-MAP:URI="init.mp4", which
    // at least version:5
    // DOC: https://developer.apple.com/documentation/http-live-streaming/about-the-ext-x-version-tag
    ss << "#EXT-X-VERSION:3" << SRS_CONSTS_LF;

    // #EXT-X-MEDIA-SEQUENCE:4294967295\n
    SrsHlsSegment *first = dynamic_cast<SrsHlsSegment *>(segments_->first());
    if (first == NULL) {
        return srs_error_new(ERROR_HLS_WRITE_FAILED, "segments cast");
    }

    ss << "#EXT-X-MEDIA-SEQUENCE:" << first->sequence_no_ << SRS_CONSTS_LF;

    // #EXT-X-TARGETDURATION:4294967295\n
    /**
     * @see hls-m3u8-draft-pantos-http-live-streaming-12.pdf, page 25
     * The Media Playlist file MUST contain an EXT-X-TARGETDURATION tag.
     * Its value MUST be equal to or greater than the EXTINF duration of any
     * media segment that appears or will appear in the Playlist file,
     * rounded to the nearest integer. Its value MUST NOT change. A
     * typical target duration is 10 seconds.
     */
    srs_utime_t max_duration = segments_->max_duration();
    int target_duration = (int)ceil(srsu2msi(srs_max(max_duration, max_td_)) / 1000.0);

    ss << "#EXT-X-TARGETDURATION:" << target_duration << SRS_CONSTS_LF;

    // TODO: add #EXT-X-MAP:URI="init.mp4" for fmp4

    // write all segments
    for (int i = 0; i < segments_->size(); i++) {
        SrsHlsSegment *segment = dynamic_cast<SrsHlsSegment *>(segments_->at(i));
        if ((err = do_refresh_m3u8_segment(segment, ss)) != srs_success) {
            return srs_error_wrap(err, "hls: refresh m3u8 segment");
        }
    }

    // write m3u8 to writer.
    std::string m3u8 = ss.str();
    if ((err = writer->write((char *)m3u8.c_str(), (int)m3u8.length(), NULL)) != srs_success) {
        return srs_error_wrap(err, "hls: write m3u8");
    }

    return err;
}

srs_error_t SrsHlsMuxer::do_refresh_m3u8_segment(SrsHlsSegment *segment, std::stringstream &ss)
{
    srs_error_t err = srs_success;

    if (segment->is_sequence_header()) {
        // #EXT-X-DISCONTINUITY\n
        ss << "#EXT-X-DISCONTINUITY" << SRS_CONSTS_LF;
    }

    if (hls_keys_ && ((segment->sequence_no_ % hls_fragments_per_key_) == 0)) {
        char hexiv[33];
        srs_hex_encode_to_string(hexiv, segment->iv_, 16);
        hexiv[32] = '\0';

        string key_file = srs_path_build_stream(hls_key_file_, req_->vhost_, req_->app_, req_->stream_);
        key_file = srs_strings_replace(key_file, "[seq]", srs_strconv_format_int(segment->sequence_no_));

        string key_path = key_file;
        // if key_url is not set,only use the file name
        if (!hls_key_url_.empty()) {
            key_path = hls_key_url_ + key_file;
        }

        ss << "#EXT-X-KEY:METHOD=AES-128,URI=" << "\"" << key_path << "\",IV=0x" << hexiv << SRS_CONSTS_LF;
    }

    // "#EXTINF:4294967295.208,\n"
    ss.precision(3);
    ss.setf(std::ios::fixed, std::ios::floatfield);
    ss << "#EXTINF:" << srsu2msi(segment->duration()) / 1000.0 << ", no desc" << SRS_CONSTS_LF;

    // {file name}\n
    std::string seg_uri = segment->uri_;
    if (true) {
        std::stringstream stemp;
        stemp << srsu2msi(segment->duration());
        seg_uri = srs_strings_replace(seg_uri, "[duration]", stemp.str());
    }
    // ss << segment->uri << SRS_CONSTS_LF;
    ss << seg_uri << SRS_CONSTS_LF;

    return err;
}

ISrsHlsController::ISrsHlsController()
{
}

ISrsHlsController::~ISrsHlsController()
{
}

SrsHlsController::SrsHlsController()
{
    tsmc_ = new SrsTsMessageCache();
    muxer_ = new SrsHlsMuxer();

    hls_dts_directly_ = false;
    previous_audio_dts_ = 0;
    aac_samples_ = 0;

    config_ = _srs_config;
}

SrsHlsController::~SrsHlsController()
{
    srs_freep(muxer_);
    srs_freep(tsmc_);

    config_ = NULL;
}

// CRITICAL: This method is called AFTER the source has been added to the source pool
// in the fetch_or_create pattern (see PR 4449).
//
// IMPORTANT: All field initialization in this method MUST NOT cause coroutine context switches.
// This prevents the race condition where multiple coroutines could create duplicate sources
// for the same stream when context switches occurred during initialization.
srs_error_t SrsHlsController::initialize()
{
    srs_error_t err = muxer_->initialize();
    if (err != srs_success) {
        return srs_error_wrap(err, "hls muxer initialize");
    }
    return srs_success;
}

void SrsHlsController::dispose()
{
    muxer_->dispose();
}

int SrsHlsController::sequence_no()
{
    return muxer_->sequence_no();
}

string SrsHlsController::ts_url()
{
    return muxer_->ts_url();
}

srs_utime_t SrsHlsController::duration()
{
    return muxer_->duration();
}

int SrsHlsController::deviation()
{
    return muxer_->deviation();
}

srs_error_t SrsHlsController::on_publish(ISrsRequest *req)
{
    srs_error_t err = srs_success;

    std::string vhost = req->vhost_;
    std::string stream = req->stream_;
    std::string app = req->app_;

    srs_utime_t hls_fragment = config_->get_hls_fragment(vhost);
    double hls_td_ratio = config_->get_hls_td_ratio(vhost);
    srs_utime_t hls_window = config_->get_hls_window(vhost);

    // get the hls m3u8 ts list entry prefix config
    std::string entry_prefix = config_->get_hls_entry_prefix(vhost);
    // get the hls path config
    std::string path = config_->get_hls_path(vhost);
    std::string m3u8_file = config_->get_hls_m3u8_file(vhost);
    std::string ts_file = config_->get_hls_ts_file(vhost);
    bool cleanup = config_->get_hls_cleanup(vhost);
    bool wait_keyframe = config_->get_hls_wait_keyframe(vhost);
    // the audio overflow, for pure audio to reap segment.
    double hls_aof_ratio = config_->get_hls_aof_ratio(vhost);
    // whether use floor(timestamp/hls_fragment) for variable timestamp
    bool ts_floor = config_->get_hls_ts_floor(vhost);
    // the seconds to dispose the hls.
    srs_utime_t hls_dispose = config_->get_hls_dispose(vhost);

    bool hls_keys = config_->get_hls_keys(vhost);
    int hls_fragments_per_key = config_->get_hls_fragments_per_key(vhost);
    string hls_key_file = config_->get_hls_key_file(vhost);
    string hls_key_file_path = config_->get_hls_key_file_path(vhost);
    string hls_key_url = config_->get_hls_key_url(vhost);

    // TODO: FIXME: support load exists m3u8, to recover publish stream.
    // for the HLS donot requires the EXT-X-MEDIA-SEQUENCE be monotonically increase.
    bool recover = config_->get_hls_recover(vhost);

    if ((err = muxer_->on_publish(req)) != srs_success) {
        return srs_error_wrap(err, "muxer publish");
    }

    if ((err = muxer_->update_config(req, entry_prefix, path, m3u8_file, ts_file, hls_fragment,
                                     hls_window, ts_floor, hls_aof_ratio, cleanup, wait_keyframe, hls_keys, hls_fragments_per_key,
                                     hls_key_file, hls_key_file_path, hls_key_url)) != srs_success) {
        return srs_error_wrap(err, "hls: update config");
    }

    if (recover && (err = muxer_->recover_hls()) != srs_success) {
        return srs_error_wrap(err, "hls: recover stream");
    }

    if ((err = muxer_->segment_open()) != srs_success) {
        return srs_error_wrap(err, "hls: segment open");
    }

    // This config item is used in SrsHls, we just log its value here.
    // If enabled, directly turn FLV timestamp to TS DTS.
    // @remark It'll be reloaded automatically, because the origin hub will republish while reloading.
    hls_dts_directly_ = config_->get_vhost_hls_dts_directly(req->vhost_);

    srs_trace("hls: win=%dms, frag=%dms, prefix=%s, path=%s, m3u8=%s, ts=%s, tdr=%.2f, aof=%.2f, floor=%d, clean=%d, waitk=%d, dispose=%dms, dts_directly=%d",
              srsu2msi(hls_window), srsu2msi(hls_fragment), entry_prefix.c_str(), path.c_str(), m3u8_file.c_str(), ts_file.c_str(),
              hls_td_ratio, hls_aof_ratio, ts_floor, cleanup, wait_keyframe, srsu2msi(hls_dispose), hls_dts_directly_);

    return err;
}

srs_error_t SrsHlsController::on_unpublish()
{
    srs_error_t err = srs_success;

    if ((err = muxer_->flush_audio(tsmc_)) != srs_success) {
        return srs_error_wrap(err, "hls: flush audio");
    }

    if ((err = muxer_->segment_close()) != srs_success) {
        return srs_error_wrap(err, "hls: segment close");
    }

    if ((err = muxer_->on_unpublish()) != srs_success) {
        return srs_error_wrap(err, "muxer unpublish");
    }

    return err;
}

// LCOV_EXCL_START
srs_error_t SrsHlsController::on_sequence_header(SrsMediaPacket *msg, SrsFormat *format)
{
    // TODO: support discontinuity for the same stream
    // currently we reap and insert discontinity when encoder republish,
    // but actually, event when stream is not republish, the
    // sequence header may change, for example,
    // ffmpeg ingest a external rtmp stream and push to srs,
    // when the sequence header changed, the stream is not republish.
    return muxer_->on_sequence_header();
}
// LCOV_EXCL_STOP

srs_error_t SrsHlsController::write_audio(SrsMediaPacket *shared_audio, SrsFormat *format)
{
    srs_error_t err = srs_success;
    SrsParsedAudioPacket *frame = format->audio_;

    // Reset the aac samples counter when DTS jitter.
    if (previous_audio_dts_ > shared_audio->timestamp_) {
        previous_audio_dts_ = shared_audio->timestamp_;
        aac_samples_ = 0;
    }

    // The diff duration in ms between two FLV audio packets.
    int diff = ::abs((int)(shared_audio->timestamp_ - previous_audio_dts_));
    previous_audio_dts_ = shared_audio->timestamp_;

    // Guess the number of samples for each AAC frame.
    // If samples is 1024, the sample-rate is 8000HZ, the diff should be 1024/8000s=128ms.
    // If samples is 1024, the sample-rate is 44100HZ, the diff should be 1024/44100s=23ms.
    // If samples is 2048, the sample-rate is 44100HZ, the diff should be 2048/44100s=46ms.
    int nb_samples_per_frame = 0;
    int guessNumberOfSamples = diff * srs_flv_srates[format->acodec_->sound_rate_] / 1000;
    if (guessNumberOfSamples > 0) {
        if (guessNumberOfSamples < 960) {
            nb_samples_per_frame = 960;
        } else if (guessNumberOfSamples < 1536) {
            nb_samples_per_frame = 1024;
        } else if (guessNumberOfSamples < 3072) {
            nb_samples_per_frame = 2048;
        } else {
            nb_samples_per_frame = 4096;
        }
    }

    // Recalc the DTS by the samples of AAC.
    aac_samples_ += nb_samples_per_frame;
    int64_t dts = 90000 * aac_samples_ / srs_flv_srates[format->acodec_->sound_rate_];

    // If directly turn FLV timestamp, overwrite the guessed DTS.
    // @doc https://github.com/ossrs/srs/issues/1506#issuecomment-562063095
    if (hls_dts_directly_) {
        dts = shared_audio->timestamp_ * 90;
    }

    // Refresh the codec ASAP.
    if (muxer_->latest_acodec() != frame->acodec()->id_) {
        srs_trace("HLS: Switch audio codec %d(%s) to %d(%s)", muxer_->latest_acodec(), srs_audio_codec_id2str(muxer_->latest_acodec()).c_str(),
                  frame->acodec()->id_, srs_audio_codec_id2str(frame->acodec()->id_).c_str());
        muxer_->set_latest_acodec(frame->acodec()->id_);
    }

    // write audio to cache.
    if ((err = tsmc_->cache_audio(frame, dts)) != srs_success) {
        return srs_error_wrap(err, "hls: cache audio");
    }

    // First, update the duration of the segment, as we might reap the segment. The duration should
    // cover from the first frame to the last frame.
    muxer_->update_duration(tsmc_->audio_->dts_);

    // reap when current source is pure audio.
    // it maybe changed when stream info changed,
    // for example, pure audio when start, audio/video when publishing,
    // pure audio again for audio disabled.
    // so we reap event when the audio incoming when segment overflow.
    // we use absolutely overflow of segment to make jwplayer/ffplay happy
    if (tsmc_->audio_ && muxer_->is_segment_absolutely_overflow()) {
        if ((err = reap_segment()) != srs_success) {
            return srs_error_wrap(err, "hls: reap segment");
        }
    }

    // for pure audio, aggregate some frame to one.
    // TODO: FIXME: Check whether it's necessary.
    if (muxer_->pure_audio() && tsmc_->audio_) {
        if (dts - tsmc_->audio_->start_pts_ < SRS_CONSTS_HLS_PURE_AUDIO_AGGREGATE) {
            return err;
        }
    }

    // directly write the audio frame by frame to ts,
    // it's ok for the hls overload, or maybe cause the audio corrupt,
    // which introduced by aggregate the audios to a big one.
    // @see https://github.com/ossrs/srs/issues/512
    if ((err = muxer_->flush_audio(tsmc_)) != srs_success) {
        return srs_error_wrap(err, "hls: flush audio");
    }

    return err;
}

srs_error_t SrsHlsController::write_video(SrsMediaPacket *shared_video, SrsFormat *format)
{
    srs_error_t err = srs_success;
    SrsParsedVideoPacket *frame = format->video_;
    int64_t dts = shared_video->timestamp_ * 90;

    // Refresh the codec ASAP.
    if (muxer_->latest_vcodec() != frame->vcodec()->id_) {
        srs_trace("HLS: Switch video codec %d(%s) to %d(%s)", muxer_->latest_vcodec(), srs_video_codec_id2str(muxer_->latest_vcodec()).c_str(),
                  frame->vcodec()->id_, srs_video_codec_id2str(frame->vcodec()->id_).c_str());
        muxer_->set_latest_vcodec(frame->vcodec()->id_);
    }

    // write video to cache.
    if ((err = tsmc_->cache_video(frame, dts)) != srs_success) {
        return srs_error_wrap(err, "hls: cache video");
    }

    // First, update the duration of the segment, as we might reap the segment. The duration should
    // cover from the first frame to the last frame.
    muxer_->update_duration(tsmc_->video_->dts_);

    // when segment overflow, reap if possible.
    if (muxer_->is_segment_overflow()) {
        // do reap ts if any of:
        //      a. wait keyframe and got keyframe.
        //      b. always reap when not wait keyframe.
        if (!muxer_->wait_keyframe() || frame->frame_type_ == SrsVideoAvcFrameTypeKeyFrame) {
            // reap the segment, which will also flush the video.
            if ((err = reap_segment()) != srs_success) {
                return srs_error_wrap(err, "hls: reap segment");
            }
        }
    }

    // flush video when got one
    if ((err = muxer_->flush_video(tsmc_)) != srs_success) {
        return srs_error_wrap(err, "hls: flush video");
    }

    return err;
}

srs_error_t SrsHlsController::reap_segment()
{
    srs_error_t err = srs_success;

    // TODO: flush audio before or after segment?
    // TODO: fresh segment begin with audio or video?

    // close current ts.
    if ((err = muxer_->segment_close()) != srs_success) {
        // When close segment error, we must reopen it for next packet to write.
        srs_error_t r0 = muxer_->segment_open();
        if (r0 != srs_success) {
            srs_warn("close segment err %s", srs_error_desc(r0).c_str());
            srs_freep(r0);
        }

        return srs_error_wrap(err, "hls: segment close");
    }

    // open new ts.
    if ((err = muxer_->segment_open()) != srs_success) {
        return srs_error_wrap(err, "hls: segment open");
    }

    // segment open, flush video first.
    if ((err = muxer_->flush_video(tsmc_)) != srs_success) {
        return srs_error_wrap(err, "hls: flush video");
    }

    // segment open, flush the audio.
    // @see: ngx_rtmp_hls_open_fragment
    /* start fragment with audio to make iPhone happy */
    if ((err = muxer_->flush_audio(tsmc_)) != srs_success) {
        return srs_error_wrap(err, "hls: flush audio");
    }

    return err;
}

SrsHlsMp4Controller::SrsHlsMp4Controller()
{
    has_video_sh_ = false;
    has_audio_sh_ = false;

    video_track_id_ = 1;
    audio_track_id_ = 2;

    audio_dts_ = 0;
    video_dts_ = 0;

    req_ = NULL;
    muxer_ = new SrsHlsFmp4Muxer();

    config_ = _srs_config;
}

SrsHlsMp4Controller::~SrsHlsMp4Controller()
{
    srs_freep(muxer_);

    config_ = NULL;
}

// CRITICAL: This method is called AFTER the source has been added to the source pool
// in the fetch_or_create pattern (see PR 4449).
//
// IMPORTANT: All field initialization in this method MUST NOT cause coroutine context switches.
// This prevents the race condition where multiple coroutines could create duplicate sources
// for the same stream when context switches occurred during initialization.
srs_error_t SrsHlsMp4Controller::initialize()
{
    srs_error_t err = srs_success;
    if ((err = muxer_->initialize(video_track_id_, audio_track_id_)) != srs_success) {
        return srs_error_wrap(err, "initialize SrsHlsFmp4Muxer");
    }

    return err;
}

void SrsHlsMp4Controller::dispose()
{
    muxer_->dispose();
}

srs_error_t SrsHlsMp4Controller::on_publish(ISrsRequest *req)
{
    srs_error_t err = srs_success;

    req_ = req;
    std::string vhost = req->vhost_;
    std::string stream = req->stream_;
    std::string app = req->app_;

    // get the hls m3u8 ts list entry prefix config
    std::string entry_prefix = config_->get_hls_entry_prefix(vhost);
    // get the hls path config
    std::string path = config_->get_hls_path(vhost);
    std::string m3u8_file = config_->get_hls_m3u8_file(vhost);
    std::string ts_file = config_->get_hls_ts_file(vhost);

    if ((err = muxer_->on_publish(req)) != srs_success) {
        return srs_error_wrap(err, "muxer publish");
    }

    if ((err = muxer_->update_config(req)) != srs_success) {
        return srs_error_wrap(err, "hls: update config");
    }

    return err;
}

srs_error_t SrsHlsMp4Controller::on_unpublish()
{
    srs_error_t err = srs_success;
    req_ = NULL;

    if ((err = muxer_->segment_close()) != srs_success) {
        return srs_error_wrap(err, "hls: segment close");
    }

    if ((err = muxer_->on_unpublish()) != srs_success) {
        return srs_error_wrap(err, "muxer unpublish");
    }

    return err;
}

srs_error_t SrsHlsMp4Controller::write_audio(SrsMediaPacket *shared_audio, SrsFormat *format)
{
    srs_error_t err = srs_success;
    SrsParsedAudioPacket *frame = format->audio_;

    // Ignore audio sequence header
    if (format->is_aac_sequence_header() || format->is_mp3_sequence_header()) {
        return err;
    }

    // Refresh the codec ASAP.
    if (muxer_->latest_acodec() != frame->acodec()->id_) {
        srs_trace("HLS: Switch audio codec %d(%s) to %d(%s)", muxer_->latest_acodec(), srs_audio_codec_id2str(muxer_->latest_acodec()).c_str(),
                  frame->acodec()->id_, srs_audio_codec_id2str(frame->acodec()->id_).c_str());
        muxer_->set_latest_acodec(frame->acodec()->id_);
    }

    audio_dts_ = shared_audio->timestamp_;

    if ((err = muxer_->write_audio(shared_audio, format)) != srs_success) {
        return srs_error_wrap(err, "write audio");
    }

    return err;
}

srs_error_t SrsHlsMp4Controller::write_video(SrsMediaPacket *shared_video, SrsFormat *format)
{
    srs_error_t err = srs_success;
    SrsParsedVideoPacket *frame = format->video_;

    // Refresh the codec ASAP.
    if (muxer_->latest_vcodec() != frame->vcodec()->id_) {
        srs_trace("HLS: Switch video codec %d(%s) to %d(%s)", muxer_->latest_vcodec(), srs_video_codec_id2str(muxer_->latest_vcodec()).c_str(),
                  frame->vcodec()->id_, srs_video_codec_id2str(frame->vcodec()->id_).c_str());
        muxer_->set_latest_vcodec(frame->vcodec()->id_);
    }

    video_dts_ = shared_video->timestamp_;

    if ((err = muxer_->write_video(shared_video, format)) != srs_success) {
        return srs_error_wrap(err, "write video");
    }

    return err;
}

srs_error_t SrsHlsMp4Controller::on_sequence_header(SrsMediaPacket *msg, SrsFormat *format)
{
    srs_error_t err = srs_success;

    if (req_ == NULL) {
        return srs_error_new(ERROR_HLS_NO_STREAM, "no req yet");
    }

    if (msg->is_video()) {
        has_video_sh_ = true;
    }

    if (msg->is_audio()) {
        if (format->acodec_->aac_extra_data_.size() == 0) {
            srs_trace("the audio codec's aac extra data is empty");
            return err;
        }

        has_audio_sh_ = true;
    }

    if ((err = muxer_->write_init_mp4(format, has_video_sh_, has_audio_sh_)) != srs_success) {
        return srs_error_wrap(err, "write init mp4");
    }

    return err;
}

int SrsHlsMp4Controller::sequence_no()
{
    return muxer_->sequence_no();
}

std::string SrsHlsMp4Controller::ts_url()
{
    return muxer_->m4s_url();
}

srs_utime_t SrsHlsMp4Controller::duration()
{
    return muxer_->duration();
}

int SrsHlsMp4Controller::deviation()
{
    return muxer_->deviation();
}

ISrsHls::ISrsHls()
{
}

ISrsHls::~ISrsHls()
{
}

SrsHls::SrsHls()
{
    req_ = NULL;
    hub_ = NULL;

    enabled_ = false;
    disposable_ = false;
    unpublishing_ = false;
    async_reload_ = reloading_ = false;
    last_update_time_ = 0;

    jitter_ = new SrsRtmpJitter();
    // TODO: replace NULL by a dummy ISrsHlsController
    controller_ = NULL;

    pprint_ = SrsPithyPrint::create_hls();

    config_ = _srs_config;
}

SrsHls::~SrsHls()
{
    srs_freep(jitter_);
    srs_freep(controller_);
    srs_freep(pprint_);

    config_ = NULL;
}

void SrsHls::async_reload()
{
    async_reload_ = true;
}

srs_error_t SrsHls::reload()
{
    srs_error_t err = srs_success;

    // Ignore if not active.
    if (!enabled_)
        return err;

    int reloading = 0, reloaded = 0, refreshed = 0;
    err = do_reload(&reloading, &reloaded, &refreshed);
    srs_trace("async reload hls %s, reloading=%d, reloaded=%d, refreshed=%d",
              req_->get_stream_url().c_str(), reloading, reloaded, refreshed);

    return err;
}

srs_error_t SrsHls::do_reload(int *reloading, int *reloaded, int *refreshed)
{
    srs_error_t err = srs_success;

    if (!async_reload_ || reloading_)
        return err;
    reloading_ = true;
    *reloading = 1;

    on_unpublish();
    if ((err = on_publish()) != srs_success) {
        return srs_error_wrap(err, "hls publish failed");
    }
    *reloaded = 1;

    // Before feed the sequence header, must reset the reloading.
    reloading_ = false;
    async_reload_ = false;

    // After reloading, we must request the sequence header again.
    if ((err = hub_->on_hls_request_sh()) != srs_success) {
        return srs_error_wrap(err, "hls request sh");
    }
    *refreshed = 1;

    return err;
}

// LCOV_EXCL_START
void SrsHls::dispose()
{
    // We disabled the reload, so HLS will not be enabled by reloading.
    // As a result, if HLS is disabled, we don't need to dispose.
    if (!enabled_) {
        return;
    }

    on_unpublish();

    // Ignore when hls_dispose disabled.
    // @see https://github.com/ossrs/srs/issues/865
    srs_utime_t hls_dispose = config_->get_hls_dispose(req_->vhost_);
    if (!hls_dispose) {
        return;
    }

    controller_->dispose();
}
// LCOV_EXCL_STOP

srs_error_t SrsHls::cycle()
{
    srs_error_t err = srs_success;

    if (last_update_time_ <= 0) {
        last_update_time_ = srs_time_now_cached();
    }

    if (!req_) {
        return err;
    }

    // When unpublishing, we must wait for it done.
    if (unpublishing_)
        return err;

    // When reloading, we must wait for it done.
    if (async_reload_)
        return err;

    // If not unpublishing and not reloading, try to dispose HLS stream.
    srs_utime_t hls_dispose = config_->get_hls_dispose(req_->vhost_);
    if (hls_dispose <= 0) {
        return err;
    }
    if (srs_time_now_cached() - last_update_time_ <= hls_dispose) {
        return err;
    }
    last_update_time_ = srs_time_now_cached();

    if (!disposable_) {
        return err;
    }
    disposable_ = false;

    srs_trace("hls cycle to dispose hls %s, timeout=%dms", req_->get_stream_url().c_str(), hls_dispose);
    dispose();

    return err;
}

srs_utime_t SrsHls::cleanup_delay()
{
    // We use larger timeout to cleanup the HLS, after disposed it if required.
    return config_->get_hls_dispose(req_->vhost_) * 1.1;
}

// CRITICAL: This method is called AFTER the source has been added to the source pool
// in the fetch_or_create pattern (see PR 4449).
//
// IMPORTANT: All field initialization in this method MUST NOT cause coroutine context switches.
// This prevents the race condition where multiple coroutines could create duplicate sources
// for the same stream when context switches occurred during initialization.
srs_error_t SrsHls::initialize(ISrsOriginHub *h, ISrsRequest *r)
{
    srs_error_t err = srs_success;

    hub_ = h;
    req_ = r;

    bool is_fmp4_enabled = config_->get_hls_use_fmp4(r->vhost_);

    if (!controller_) {
        if (is_fmp4_enabled) {
            controller_ = new SrsHlsMp4Controller();
        } else {
            controller_ = new SrsHlsController();
        }
    }

    if ((err = controller_->initialize()) != srs_success) {
        return srs_error_wrap(err, "controller initialize");
    }

    return err;
}

srs_error_t SrsHls::on_publish()
{
    srs_error_t err = srs_success;

    // update the hls time, for hls_dispose.
    last_update_time_ = srs_time_now_cached();

    // support multiple publish.
    if (enabled_) {
        return err;
    }

    if (!config_->get_hls_enabled(req_->vhost_)) {
        return err;
    }

    if ((err = controller_->on_publish(req_)) != srs_success) {
        return srs_error_wrap(err, "hls: on publish");
    }

    // if enabled, open the muxer.
    enabled_ = true;
    // Reset the unpublishing state.
    unpublishing_ = false;

    // ok, the hls can be dispose, or need to be dispose.
    disposable_ = true;

    return err;
}

void SrsHls::on_unpublish()
{
    srs_error_t err = srs_success;

    // support multiple unpublish.
    if (!enabled_) {
        return;
    }

    // During unpublishing, there maybe callback that switch to other coroutines.
    if (unpublishing_)
        return;
    unpublishing_ = true;

    if ((err = controller_->on_unpublish()) != srs_success) {
        srs_warn("hls: ignore unpublish failed %s", srs_error_desc(err).c_str());
        srs_freep(err);
    }

    enabled_ = false;
    unpublishing_ = false;
}

srs_error_t SrsHls::on_audio(SrsMediaPacket *shared_audio, SrsFormat *format)
{
    srs_error_t err = srs_success;

    // If not able to transmux to HLS, ignore.
    if (!enabled_ || unpublishing_)
        return err;
    if (async_reload_)
        return reload();

    // Ignore if no format->acodec, it means the codec is not parsed, or unknown codec.
    // @issue https://github.com/ossrs/srs/issues/1506#issuecomment-562079474
    // TODO: format->acodec is always not-nil, remove this check.
    if (!format->acodec_) {
        return err;
    }

    // update the hls time, for hls_dispose.
    last_update_time_ = srs_time_now_cached();

    SrsUniquePtr<SrsMediaPacket> audio(shared_audio->copy());

    // ts support audio codec: aac/mp3
    SrsAudioCodecId acodec = format->acodec_->id_;
    if (acodec != SrsAudioCodecIdAAC && acodec != SrsAudioCodecIdMP3) {
        return err;
    }

    // ignore sequence header
    srs_assert(format->audio_);
    // TODO: verify mp3 play by HLS.
    if (format->is_aac_sequence_header() || format->is_mp3_sequence_header()) {
        return controller_->on_sequence_header(audio.get(), format);
    }

    // TODO: FIXME: config the jitter of HLS.
    if ((err = jitter_->correct(audio.get(), SrsRtmpJitterAlgorithmOFF)) != srs_success) {
        return srs_error_wrap(err, "hls: jitter");
    }

    if ((err = controller_->write_audio(audio.get(), format)) != srs_success) {
        return srs_error_wrap(err, "hls: write audio");
    }

    return err;
}

srs_error_t SrsHls::on_video(SrsMediaPacket *shared_video, SrsFormat *format)
{
    srs_error_t err = srs_success;

    // If not able to transmux to HLS, ignore.
    if (!enabled_ || unpublishing_)
        return err;
    if (async_reload_)
        return reload();

    // Ignore if no format->vcodec, it means the codec is not parsed, or unknown codec.
    // @issue https://github.com/ossrs/srs/issues/1506#issuecomment-562079474
    if (!format->vcodec_) {
        return err;
    }

    // update the hls time, for hls_dispose.
    last_update_time_ = srs_time_now_cached();

    SrsUniquePtr<SrsMediaPacket> video(shared_video->copy());

    // ignore info frame,
    // @see https://github.com/ossrs/srs/issues/288#issuecomment-69863909
    srs_assert(format->video_);
    if (format->video_->frame_type_ == SrsVideoAvcFrameTypeVideoInfoFrame) {
        return err;
    }

    srs_assert(format->vcodec_);
    if (format->vcodec_->id_ != SrsVideoCodecIdAVC && format->vcodec_->id_ != SrsVideoCodecIdHEVC) {
        return err;
    }

    // ignore sequence header avc and hevc
    // is avc|hevc|av1 sequence header check, but av1 packet already ignored above. so it's ok to use
    // below method.
    if (format->is_avc_sequence_header()) {
        return controller_->on_sequence_header(video.get(), format);
    }

    // TODO: FIXME: config the jitter of HLS.
    if ((err = jitter_->correct(video.get(), SrsRtmpJitterAlgorithmOFF)) != srs_success) {
        return srs_error_wrap(err, "hls: jitter");
    }

    if ((err = controller_->write_video(video.get(), format)) != srs_success) {
        return srs_error_wrap(err, "hls: write video");
    }

    // pithy print message.
    hls_show_mux_log();

    return err;
}

// LCOV_EXCL_START
void SrsHls::hls_show_mux_log()
{
    pprint_->elapse();

    if (!pprint_->can_print()) {
        return;
    }

    // the run time is not equals to stream time,
    // @see: https://github.com/ossrs/srs/issues/81#issuecomment-48100994
    // it's ok.
    srs_trace("-> " SRS_CONSTS_LOG_HLS " time=%" PRId64 "ms, sno=%d, ts=%s, dur=%dms, dva=%dp",
              pprint_->age(), controller_->sequence_no(), controller_->ts_url().c_str(),
              srsu2msi(controller_->duration()), controller_->deviation());
}
// LCOV_EXCL_STOP

