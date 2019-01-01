/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2019 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <srs_app_ffmpeg.hpp>

#include <stdlib.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>

// for srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
#include <unistd.h>
#endif

#include <vector>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_process.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_utility.hpp>

#ifdef SRS_AUTO_FFMPEG_STUB

#define SRS_RTMP_ENCODER_COPY "copy"
#define SRS_RTMP_ENCODER_NO_VIDEO "vn"
#define SRS_RTMP_ENCODER_NO_AUDIO "an"
// only support libx264 encoder.
#define SRS_RTMP_ENCODER_VCODEC_LIBX264 "libx264"
#define SRS_RTMP_ENCODER_VCODEC_PNG "png"
// any aac encoder is ok which contains the aac,
// for example, libaacplus, aac, fdkaac
#define SRS_RTMP_ENCODER_ACODEC "aac"
#define SRS_RTMP_ENCODER_LIBAACPLUS "libaacplus"
#define SRS_RTMP_ENCODER_LIBFDKAAC "libfdk_aac"

SrsFFMPEG::SrsFFMPEG(std::string ffmpeg_bin)
{
    ffmpeg = ffmpeg_bin;
    
    vbitrate = 0;
    vfps = 0;
    vwidth = 0;
    vheight = 0;
    vthreads = 0;
    abitrate = 0;
    asample_rate = 0;
    achannels = 0;
    
    process = new SrsProcess();
}

SrsFFMPEG::~SrsFFMPEG()
{
    stop();
    
    srs_freep(process);
}

void SrsFFMPEG::set_iparams(string iparams)
{
    _iparams = iparams;
}

void SrsFFMPEG::set_oformat(string format)
{
    oformat = format;
}

string SrsFFMPEG::output()
{
    return _output;
}

srs_error_t SrsFFMPEG::initialize(string in, string out, string log)
{
    srs_error_t err = srs_success;
    
    input = in;
    _output = out;
    log_file = log;
    
    return err;
}

srs_error_t SrsFFMPEG::initialize_transcode(SrsConfDirective* engine)
{
    srs_error_t err = srs_success;
    
    perfile = _srs_config->get_engine_perfile(engine);
    iformat = _srs_config->get_engine_iformat(engine);
    vfilter = _srs_config->get_engine_vfilter(engine);
    vcodec = _srs_config->get_engine_vcodec(engine);
    vbitrate = _srs_config->get_engine_vbitrate(engine);
    vfps = _srs_config->get_engine_vfps(engine);
    vwidth = _srs_config->get_engine_vwidth(engine);
    vheight = _srs_config->get_engine_vheight(engine);
    vthreads = _srs_config->get_engine_vthreads(engine);
    vprofile = _srs_config->get_engine_vprofile(engine);
    vpreset = _srs_config->get_engine_vpreset(engine);
    vparams = _srs_config->get_engine_vparams(engine);
    acodec = _srs_config->get_engine_acodec(engine);
    abitrate = _srs_config->get_engine_abitrate(engine);
    asample_rate = _srs_config->get_engine_asample_rate(engine);
    achannels = _srs_config->get_engine_achannels(engine);
    aparams = _srs_config->get_engine_aparams(engine);
    oformat = _srs_config->get_engine_oformat(engine);
    
    // ensure the size is even.
    vwidth -= vwidth % 2;
    vheight -= vheight % 2;
    
    if (vcodec == SRS_RTMP_ENCODER_NO_VIDEO && acodec == SRS_RTMP_ENCODER_NO_AUDIO) {
        return srs_error_new(ERROR_ENCODER_VCODEC, "video and audio disabled");
    }
    
    if (vcodec != SRS_RTMP_ENCODER_COPY && vcodec != SRS_RTMP_ENCODER_NO_VIDEO && vcodec != SRS_RTMP_ENCODER_VCODEC_PNG) {
        if (vcodec != SRS_RTMP_ENCODER_VCODEC_LIBX264) {
            return srs_error_new(ERROR_ENCODER_VCODEC, "invalid vcodec, must be %s, actual %s", SRS_RTMP_ENCODER_VCODEC_LIBX264, vcodec.c_str());
        }
        if (vbitrate < 0) {
            return srs_error_new(ERROR_ENCODER_VBITRATE, "invalid vbitrate: %d", vbitrate);
        }
        if (vfps < 0) {
            return srs_error_new(ERROR_ENCODER_VFPS, "invalid vfps: %.2f", vfps);
        }
        if (vwidth < 0) {
            return srs_error_new(ERROR_ENCODER_VWIDTH, "invalid vwidth: %d", vwidth);
        }
        if (vheight < 0) {
            return srs_error_new(ERROR_ENCODER_VHEIGHT, "invalid vheight: %d", vheight);
        }
        if (vthreads < 0) {
            return srs_error_new(ERROR_ENCODER_VTHREADS, "invalid vthreads: %d", vthreads);
        }
        if (vprofile.empty()) {
            return srs_error_new(ERROR_ENCODER_VPROFILE, "invalid vprofile: %s", vprofile.c_str());
        }
        if (vpreset.empty()) {
            return srs_error_new(ERROR_ENCODER_VPRESET, "invalid vpreset: %s", vpreset.c_str());
        }
    }
    
    // @see, https://github.com/ossrs/srs/issues/145
    if (acodec == SRS_RTMP_ENCODER_LIBAACPLUS && acodec != SRS_RTMP_ENCODER_LIBFDKAAC) {
        if (abitrate != 0 && (abitrate < 16 || abitrate > 72)) {
            return srs_error_new(ERROR_ENCODER_ABITRATE, "invalid abitrate for aac: %d, must in [16, 72]", abitrate);
        }
    }
    
    if (acodec != SRS_RTMP_ENCODER_COPY && acodec != SRS_RTMP_ENCODER_NO_AUDIO) {
        if (abitrate < 0) {
            return srs_error_new(ERROR_ENCODER_ABITRATE, "invalid abitrate: %d", abitrate);
        }
        if (asample_rate < 0) {
            return srs_error_new(ERROR_ENCODER_ASAMPLE_RATE, "invalid sample rate: %d", asample_rate);
        }
        if (achannels != 0 && achannels != 1 && achannels != 2) {
            return srs_error_new(ERROR_ENCODER_ACHANNELS, "invalid achannels, must be 1 or 2, actual %d", achannels);
        }
    }
    if (_output.empty()) {
        return srs_error_new(ERROR_ENCODER_OUTPUT, "invalid empty output");
    }
    
    // for not rtmp input, donot append the iformat,
    // for example, "-f flv" before "-i udp://192.168.1.252:2222"
    // @see https://github.com/ossrs/srs/issues/290
    if (!srs_string_starts_with(input, "rtmp://")) {
        iformat = "";
    }
    
    return err;
}

srs_error_t SrsFFMPEG::initialize_copy()
{
    srs_error_t err = srs_success;
    
    vcodec = SRS_RTMP_ENCODER_COPY;
    acodec = SRS_RTMP_ENCODER_COPY;
    
    if (_output.empty()) {
        return srs_error_new(ERROR_ENCODER_OUTPUT, "invalid empty output");
    }
    
    return err;
}

srs_error_t SrsFFMPEG::start()
{
    srs_error_t err = srs_success;
    
    if (process->started()) {
        return err;
    }
    
    // the argv for process.
    params.clear();
    
    // argv[0], set to ffmpeg bin.
    // The  execv()  and  execvp() functions ....
    // The first argument, by convention, should point to
    // the filename associated  with  the file being executed.
    params.push_back(ffmpeg);
    
    // input params
    if (!_iparams.empty()) {
        params.push_back(_iparams);
    }
    
    // build the perfile
    if (!perfile.empty()) {
        std::vector<std::string>::iterator it;
        for (it = perfile.begin(); it != perfile.end(); ++it) {
            std::string p = *it;
            if (!p.empty()) {
                params.push_back(p);
            }
        }
    }
    
    // input.
    if (iformat != "off" && !iformat.empty()) {
        params.push_back("-f");
        params.push_back(iformat);
    }
    
    params.push_back("-i");
    params.push_back(input);
    
    // build the filter
    if (!vfilter.empty()) {
        std::vector<std::string>::iterator it;
        for (it = vfilter.begin(); it != vfilter.end(); ++it) {
            std::string p = *it;
            if (!p.empty()) {
                params.push_back(p);
            }
        }
    }
    
    // video specified.
    if (vcodec != SRS_RTMP_ENCODER_NO_VIDEO) {
        params.push_back("-vcodec");
        params.push_back(vcodec);
    } else {
        params.push_back("-vn");
    }
    
    // the codec params is disabled when copy
    if (vcodec != SRS_RTMP_ENCODER_COPY && vcodec != SRS_RTMP_ENCODER_NO_VIDEO) {
        if (vbitrate > 0) {
            params.push_back("-b:v");
            params.push_back(srs_int2str(vbitrate * 1000));
        }
        
        if (vfps > 0) {
            params.push_back("-r");
            params.push_back(srs_float2str(vfps));
        }
        
        if (vwidth > 0 && vheight > 0) {
            params.push_back("-s");
            params.push_back(srs_int2str(vwidth) + "x" + srs_int2str(vheight));
        }
        
        // TODO: add aspect if needed.
        if (vwidth > 0 && vheight > 0) {
            params.push_back("-aspect");
            params.push_back(srs_int2str(vwidth) + ":" + srs_int2str(vheight));
        }
        
        if (vthreads > 0) {
            params.push_back("-threads");
            params.push_back(srs_int2str(vthreads));
        }
        
        if (!vprofile.empty()) {
            params.push_back("-profile:v");
            params.push_back(vprofile);
        }
        
        if (!vpreset.empty()) {
            params.push_back("-preset");
            params.push_back(vpreset);
        }
        
        // vparams
        if (!vparams.empty()) {
            std::vector<std::string>::iterator it;
            for (it = vparams.begin(); it != vparams.end(); ++it) {
                std::string p = *it;
                if (!p.empty()) {
                    params.push_back(p);
                }
            }
        }
    }
    
    // audio specified.
    if (acodec != SRS_RTMP_ENCODER_NO_AUDIO) {
        params.push_back("-acodec");
        params.push_back(acodec);
    } else {
        params.push_back("-an");
    }
    
    // the codec params is disabled when copy
    if (acodec != SRS_RTMP_ENCODER_NO_AUDIO) {
        if (acodec != SRS_RTMP_ENCODER_COPY) {
            if (abitrate > 0) {
                params.push_back("-b:a");
                params.push_back(srs_int2str(abitrate * 1000));
            }
            
            if (asample_rate > 0) {
                params.push_back("-ar");
                params.push_back(srs_int2str(asample_rate));
            }
            
            if (achannels > 0) {
                params.push_back("-ac");
                params.push_back(srs_int2str(achannels));
            }
            
            // aparams
            std::vector<std::string>::iterator it;
            for (it = aparams.begin(); it != aparams.end(); ++it) {
                std::string p = *it;
                if (!p.empty()) {
                    params.push_back(p);
                }
            }
        } else {
            // for audio copy.
            for (int i = 0; i < (int)aparams.size();) {
                std::string pn = aparams[i++];
                
                // aparams, the adts to asc filter "-bsf:a aac_adtstoasc"
                if (pn == "-bsf:a" && i < (int)aparams.size()) {
                    std::string pv = aparams[i++];
                    if (pv == "aac_adtstoasc") {
                        params.push_back(pn);
                        params.push_back(pv);
                    }
                }
            }
        }
    }
    
    // output
    if (oformat != "off" && !oformat.empty()) {
        params.push_back("-f");
        params.push_back(oformat);
    }
    
    params.push_back("-y");
    params.push_back(_output);
    
    // when specified the log file.
    if (!log_file.empty()) {
        // stdout
        params.push_back("1");
        params.push_back(">");
        params.push_back(log_file);
        // stderr
        params.push_back("2");
        params.push_back(">");
        params.push_back(log_file);
    }
    
    // initialize the process.
    if ((err = process->initialize(ffmpeg, params)) != srs_success) {
        return srs_error_wrap(err, "init process");
    }
    
    return process->start();
}

srs_error_t SrsFFMPEG::cycle()
{
    return process->cycle();
}

void SrsFFMPEG::stop()
{
    process->stop();
}

void SrsFFMPEG::fast_stop()
{
    process->fast_stop();
}

#endif


