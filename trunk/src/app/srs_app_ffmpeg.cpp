//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_ffmpeg.hpp>

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>

// for srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
#include <unistd.h>
#endif

#include <vector>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_process.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>

#ifdef SRS_FFMPEG_STUB

#define SRS_RTMP_ENCODER_COPY "copy"
#define SRS_RTMP_ENCODER_NO_VIDEO "vn"
#define SRS_RTMP_ENCODER_NO_AUDIO "an"
// only support encoder: h264, h265 and other variants like libx264 etc.
#define SRS_RTMP_ENCODER_VCODEC_H264 "264"
#define SRS_RTMP_ENCODER_VCODEC_H265 "265"
#define SRS_RTMP_ENCODER_VCODEC_HEVC "hevc"
#define SRS_RTMP_ENCODER_VCODEC_PNG "png"
// any aac encoder is ok which contains the aac,
// for example, libaacplus, aac, fdkaac
#define SRS_RTMP_ENCODER_ACODEC "aac"
#define SRS_RTMP_ENCODER_LIBAACPLUS "libaacplus"
#define SRS_RTMP_ENCODER_LIBFDKAAC "libfdk_aac"

ISrsFFMPEG::ISrsFFMPEG()
{
}

ISrsFFMPEG::~ISrsFFMPEG()
{
}

SrsFFMPEG::SrsFFMPEG(std::string ffmpeg_bin)
{
    ffmpeg_ = ffmpeg_bin;

    vbitrate_ = 0;
    vfps_ = 0;
    vwidth_ = 0;
    vheight_ = 0;
    vthreads_ = 0;
    abitrate_ = 0;
    asample_rate_ = 0;
    achannels_ = 0;

    process_ = new SrsProcess();

    config_ = _srs_config;
}

SrsFFMPEG::~SrsFFMPEG()
{
    stop();

    srs_freep(process_);

    config_ = NULL;
}

void SrsFFMPEG::append_iparam(string iparam)
{
    iparams_.push_back(iparam);
}

void SrsFFMPEG::set_oformat(string format)
{
    oformat_ = format;
}

string SrsFFMPEG::output()
{
    return output_;
}

srs_error_t SrsFFMPEG::initialize(string in, string out, string log)
{
    srs_error_t err = srs_success;

    input_ = in;
    output_ = out;
    log_file_ = log;

    return err;
}

srs_error_t SrsFFMPEG::initialize_transcode(SrsConfDirective *engine)
{
    srs_error_t err = srs_success;

    perfile_ = config_->get_engine_perfile(engine);
    iformat_ = config_->get_engine_iformat(engine);
    vfilter_ = config_->get_engine_vfilter(engine);
    vcodec_ = config_->get_engine_vcodec(engine);
    vbitrate_ = config_->get_engine_vbitrate(engine);
    vfps_ = config_->get_engine_vfps(engine);
    vwidth_ = config_->get_engine_vwidth(engine);
    vheight_ = config_->get_engine_vheight(engine);
    vthreads_ = config_->get_engine_vthreads(engine);
    vprofile_ = config_->get_engine_vprofile(engine);
    vpreset_ = config_->get_engine_vpreset(engine);
    vparams_ = config_->get_engine_vparams(engine);
    acodec_ = config_->get_engine_acodec(engine);
    abitrate_ = config_->get_engine_abitrate(engine);
    asample_rate_ = config_->get_engine_asample_rate(engine);
    achannels_ = config_->get_engine_achannels(engine);
    aparams_ = config_->get_engine_aparams(engine);
    oformat_ = config_->get_engine_oformat(engine);

    // ensure the size is even.
    vwidth_ -= vwidth_ % 2;
    vheight_ -= vheight_ % 2;

    if (vcodec_ == SRS_RTMP_ENCODER_NO_VIDEO && acodec_ == SRS_RTMP_ENCODER_NO_AUDIO) {
        return srs_error_new(ERROR_ENCODER_VCODEC, "video and audio disabled");
    }

    if (vcodec_ != SRS_RTMP_ENCODER_COPY && vcodec_ != SRS_RTMP_ENCODER_NO_VIDEO && vcodec_ != SRS_RTMP_ENCODER_VCODEC_PNG) {
        if (vcodec_.find(SRS_RTMP_ENCODER_VCODEC_H264) != string::npos && vcodec_.find(SRS_RTMP_ENCODER_VCODEC_H265) != string::npos && vcodec_.find(SRS_RTMP_ENCODER_VCODEC_HEVC) != string::npos) {
            return srs_error_new(ERROR_ENCODER_VCODEC, "invalid vcodec, must be h264, h265 or one of their variants, actual %s", vcodec_.c_str());
        }
        if (vbitrate_ < 0) {
            return srs_error_new(ERROR_ENCODER_VBITRATE, "invalid vbitrate: %d", vbitrate_);
        }
        if (vfps_ < 0) {
            return srs_error_new(ERROR_ENCODER_VFPS, "invalid vfps: %.2f", vfps_);
        }
        if (vwidth_ < 0) {
            return srs_error_new(ERROR_ENCODER_VWIDTH, "invalid vwidth: %d", vwidth_);
        }
        if (vheight_ < 0) {
            return srs_error_new(ERROR_ENCODER_VHEIGHT, "invalid vheight: %d", vheight_);
        }
        if (vthreads_ < 0) {
            return srs_error_new(ERROR_ENCODER_VTHREADS, "invalid vthreads: %d", vthreads_);
        }
        if (vprofile_.empty()) {
            return srs_error_new(ERROR_ENCODER_VPROFILE, "invalid vprofile: %s", vprofile_.c_str());
        }
        if (vpreset_.empty()) {
            return srs_error_new(ERROR_ENCODER_VPRESET, "invalid vpreset: %s", vpreset_.c_str());
        }
    }

    // @see, https://github.com/ossrs/srs/issues/145
    if (acodec_ == SRS_RTMP_ENCODER_LIBAACPLUS && acodec_ != SRS_RTMP_ENCODER_LIBFDKAAC) {
        if (abitrate_ != 0 && (abitrate_ < 16 || abitrate_ > 72)) {
            return srs_error_new(ERROR_ENCODER_ABITRATE, "invalid abitrate for aac: %d, must in [16, 72]", abitrate_);
        }
    }

    if (acodec_ != SRS_RTMP_ENCODER_COPY && acodec_ != SRS_RTMP_ENCODER_NO_AUDIO) {
        if (abitrate_ < 0) {
            return srs_error_new(ERROR_ENCODER_ABITRATE, "invalid abitrate: %d", abitrate_);
        }
        if (asample_rate_ < 0) {
            return srs_error_new(ERROR_ENCODER_ASAMPLE_RATE, "invalid sample rate: %d", asample_rate_);
        }
        if (achannels_ != 0 && achannels_ != 1 && achannels_ != 2) {
            return srs_error_new(ERROR_ENCODER_ACHANNELS, "invalid achannels, must be 1 or 2, actual %d", achannels_);
        }
    }
    if (output_.empty()) {
        return srs_error_new(ERROR_ENCODER_OUTPUT, "invalid empty output");
    }

    // for not rtmp input, donot append the iformat,
    // for example, "-f flv" before "-i udp://192.168.1.252:2222"
    if (!srs_strings_starts_with(input_, "rtmp://")) {
        iformat_ = "";
    }

    return err;
}

srs_error_t SrsFFMPEG::initialize_copy()
{
    srs_error_t err = srs_success;

    vcodec_ = SRS_RTMP_ENCODER_COPY;
    acodec_ = SRS_RTMP_ENCODER_COPY;

    if (output_.empty()) {
        return srs_error_new(ERROR_ENCODER_OUTPUT, "invalid empty output");
    }

    return err;
}

srs_error_t SrsFFMPEG::start()
{
    srs_error_t err = srs_success;

    if (process_->started()) {
        return err;
    }

    // the argv for process.
    params_.clear();

    // argv[0], set to ffmpeg bin.
    // The  execv()  and  execvp() functions ....
    // The first argument, by convention, should point to
    // the filename associated  with  the file being executed.
    params_.push_back(ffmpeg_);

    // input params
    for (int i = 0; i < (int)iparams_.size(); i++) {
        string iparam = iparams_.at(i);
        if (!iparam.empty()) {
            params_.push_back(iparam);
        }
    }

    // build the perfile
    if (!perfile_.empty()) {
        std::vector<std::string>::iterator it;
        for (it = perfile_.begin(); it != perfile_.end(); ++it) {
            std::string p = *it;
            if (!p.empty()) {
                params_.push_back(p);
            }
        }
    }

    // input.
    if (iformat_ != "off" && !iformat_.empty()) {
        params_.push_back("-f");
        params_.push_back(iformat_);
    }

    params_.push_back("-i");
    params_.push_back(input_);

    // build the filter
    if (!vfilter_.empty()) {
        std::vector<std::string>::iterator it;
        for (it = vfilter_.begin(); it != vfilter_.end(); ++it) {
            std::string p = *it;
            if (!p.empty()) {
                params_.push_back(p);
            }
        }
    }

    // video specified.
    if (vcodec_ != SRS_RTMP_ENCODER_NO_VIDEO) {
        params_.push_back("-vcodec");
        params_.push_back(vcodec_);
    } else {
        params_.push_back("-vn");
    }

    // the codec params is disabled when copy
    if (vcodec_ != SRS_RTMP_ENCODER_COPY && vcodec_ != SRS_RTMP_ENCODER_NO_VIDEO) {
        if (vbitrate_ > 0) {
            params_.push_back("-b:v");
            params_.push_back(srs_strconv_format_int(vbitrate_ * 1000));
        }

        if (vfps_ > 0) {
            params_.push_back("-r");
            params_.push_back(srs_strconv_format_float(vfps_));
        }

        if (vwidth_ > 0 && vheight_ > 0) {
            params_.push_back("-s");
            params_.push_back(srs_strconv_format_int(vwidth_) + "x" + srs_strconv_format_int(vheight_));
        }

        // TODO: add aspect if needed.
        if (vwidth_ > 0 && vheight_ > 0) {
            params_.push_back("-aspect");
            params_.push_back(srs_strconv_format_int(vwidth_) + ":" + srs_strconv_format_int(vheight_));
        }

        if (vthreads_ > 0) {
            params_.push_back("-threads");
            params_.push_back(srs_strconv_format_int(vthreads_));
        }

        if (!vprofile_.empty()) {
            params_.push_back("-profile:v");
            params_.push_back(vprofile_);
        }

        if (!vpreset_.empty()) {
            params_.push_back("-preset");
            params_.push_back(vpreset_);
        }

        // vparams
        if (!vparams_.empty()) {
            std::vector<std::string>::iterator it;
            for (it = vparams_.begin(); it != vparams_.end(); ++it) {
                std::string p = *it;
                if (!p.empty()) {
                    params_.push_back(p);
                }
            }
        }
    }

    // audio specified.
    if (acodec_ != SRS_RTMP_ENCODER_NO_AUDIO) {
        params_.push_back("-acodec");
        params_.push_back(acodec_);
    } else {
        params_.push_back("-an");
    }

    // the codec params is disabled when copy
    if (acodec_ != SRS_RTMP_ENCODER_NO_AUDIO) {
        if (acodec_ != SRS_RTMP_ENCODER_COPY) {
            if (abitrate_ > 0) {
                params_.push_back("-b:a");
                params_.push_back(srs_strconv_format_int(abitrate_ * 1000));
            }

            if (asample_rate_ > 0) {
                params_.push_back("-ar");
                params_.push_back(srs_strconv_format_int(asample_rate_));
            }

            if (achannels_ > 0) {
                params_.push_back("-ac");
                params_.push_back(srs_strconv_format_int(achannels_));
            }

            // aparams
            std::vector<std::string>::iterator it;
            for (it = aparams_.begin(); it != aparams_.end(); ++it) {
                std::string p = *it;
                if (!p.empty()) {
                    params_.push_back(p);
                }
            }
        } else {
            // for audio copy.
            for (int i = 0; i < (int)aparams_.size();) {
                std::string pn = aparams_[i++];

                // aparams, the adts to asc filter "-bsf:a aac_adtstoasc"
                if (pn == "-bsf:a" && i < (int)aparams_.size()) {
                    std::string pv = aparams_[i++];
                    if (pv == "aac_adtstoasc") {
                        params_.push_back(pn);
                        params_.push_back(pv);
                    }
                }
            }
        }
    }

    // output
    if (oformat_ != "off" && !oformat_.empty()) {
        params_.push_back("-f");
        params_.push_back(oformat_);
    }

    params_.push_back("-y");
    params_.push_back(output_);

    // when specified the log file.
    if (!log_file_.empty()) {
        // stdout
        params_.push_back("1");
        params_.push_back(">");
        params_.push_back(log_file_);
        // stderr
        params_.push_back("2");
        params_.push_back(">");
        params_.push_back(log_file_);
    }

    // initialize the process.
    if ((err = process_->initialize(ffmpeg_, params_)) != srs_success) {
        return srs_error_wrap(err, "init process");
    }

    return process_->start();
}

srs_error_t SrsFFMPEG::cycle()
{
    return process_->cycle();
}

void SrsFFMPEG::stop()
{
    process_->stop();
}

void SrsFFMPEG::fast_stop()
{
    process_->fast_stop();
}

void SrsFFMPEG::fast_kill()
{
    process_->fast_kill();
}

#endif
