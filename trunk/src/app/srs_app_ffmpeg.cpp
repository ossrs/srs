/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(simple-rtmp-server)

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

#include <srs_app_ffmpeg.hpp>

#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>

#include <vector>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_app_utility.hpp>
#include <srs_app_process.hpp>
#include <srs_core_autofree.hpp>

#ifdef SRS_AUTO_FFMPEG_STUB

#define SRS_RTMP_ENCODER_COPY           "copy"
#define SRS_RTMP_ENCODER_NO_VIDEO       "vn"
#define SRS_RTMP_ENCODER_NO_AUDIO       "an"
// only support libx264 encoder.
#define SRS_RTMP_ENCODER_VCODEC         "libx264"
// any aac encoder is ok which contains the aac,
// for example, libaacplus, aac, fdkaac
#define SRS_RTMP_ENCODER_ACODEC         "aac"
#define SRS_RTMP_ENCODER_LIBAACPLUS     "libaacplus"
#define SRS_RTMP_ENCODER_LIBFDKAAC      "libfdk_aac"

SrsFFMPEG::SrsFFMPEG(std::string ffmpeg_bin)
{
    ffmpeg             = ffmpeg_bin;
    
    vbitrate         = 0;
    vfps             = 0;
    vwidth             = 0;
    vheight         = 0;
    vthreads         = 0;
    abitrate         = 0;
    asample_rate     = 0;
    achannels         = 0;
    
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

int SrsFFMPEG::initialize(string in, string out, string log)
{
    int ret = ERROR_SUCCESS;
    
    input = in;
    _output = out;
    log_file = log;
    
    return ret;
}

int SrsFFMPEG::initialize_transcode(SrsConfDirective* engine)
{
    int ret = ERROR_SUCCESS;
    
    iformat             = _srs_config->get_engine_iformat(engine);
    vfilter             = _srs_config->get_engine_vfilter(engine);
    vcodec              = _srs_config->get_engine_vcodec(engine);
    vbitrate            = _srs_config->get_engine_vbitrate(engine);
    vfps                = _srs_config->get_engine_vfps(engine);
    vwidth              = _srs_config->get_engine_vwidth(engine);
    vheight             = _srs_config->get_engine_vheight(engine);
    vthreads            = _srs_config->get_engine_vthreads(engine);
    vprofile            = _srs_config->get_engine_vprofile(engine);
    vpreset             = _srs_config->get_engine_vpreset(engine);
    vparams             = _srs_config->get_engine_vparams(engine);
    acodec              = _srs_config->get_engine_acodec(engine);
    abitrate            = _srs_config->get_engine_abitrate(engine);
    asample_rate        = _srs_config->get_engine_asample_rate(engine);
    achannels           = _srs_config->get_engine_achannels(engine);
    aparams             = _srs_config->get_engine_aparams(engine);
    oformat             = _srs_config->get_engine_oformat(engine);
    
    // ensure the size is even.
    vwidth -= vwidth % 2;
    vheight -= vheight % 2;
    
    if (vcodec == SRS_RTMP_ENCODER_NO_VIDEO && acodec == SRS_RTMP_ENCODER_NO_AUDIO) {
        ret = ERROR_ENCODER_VCODEC;
        srs_warn("video and audio disabled. ret=%d", ret);
        return ret;
    }
    
    if (vcodec != SRS_RTMP_ENCODER_COPY && vcodec != SRS_RTMP_ENCODER_NO_VIDEO) {
        if (vcodec != SRS_RTMP_ENCODER_VCODEC) {
            ret = ERROR_ENCODER_VCODEC;
            srs_error("invalid vcodec, must be %s, actual %s, ret=%d",
                SRS_RTMP_ENCODER_VCODEC, vcodec.c_str(), ret);
            return ret;
        }
        if (vbitrate < 0) {
            ret = ERROR_ENCODER_VBITRATE;
            srs_error("invalid vbitrate: %d, ret=%d", vbitrate, ret);
            return ret;
        }
        if (vfps < 0) {
            ret = ERROR_ENCODER_VFPS;
            srs_error("invalid vfps: %.2f, ret=%d", vfps, ret);
            return ret;
        }
        if (vwidth < 0) {
            ret = ERROR_ENCODER_VWIDTH;
            srs_error("invalid vwidth: %d, ret=%d", vwidth, ret);
            return ret;
        }
        if (vheight < 0) {
            ret = ERROR_ENCODER_VHEIGHT;
            srs_error("invalid vheight: %d, ret=%d", vheight, ret);
            return ret;
        }
        if (vthreads < 0) {
            ret = ERROR_ENCODER_VTHREADS;
            srs_error("invalid vthreads: %d, ret=%d", vthreads, ret);
            return ret;
        }
        if (vprofile.empty()) {
            ret = ERROR_ENCODER_VPROFILE;
            srs_error("invalid vprofile: %s, ret=%d", vprofile.c_str(), ret);
            return ret;
        }
        if (vpreset.empty()) {
            ret = ERROR_ENCODER_VPRESET;
            srs_error("invalid vpreset: %s, ret=%d", vpreset.c_str(), ret);
            return ret;
        }
    }
    
    // @see, https://github.com/simple-rtmp-server/srs/issues/145
    if (acodec == SRS_RTMP_ENCODER_LIBAACPLUS && acodec != SRS_RTMP_ENCODER_LIBFDKAAC) {
        if (abitrate != 0 && (abitrate < 16 || abitrate > 72)) {
            ret = ERROR_ENCODER_ABITRATE;
            srs_error("invalid abitrate for aac: %d, must in [16, 72], ret=%d", abitrate, ret);
            return ret;
        }
    }
    
    if (acodec != SRS_RTMP_ENCODER_COPY && acodec != SRS_RTMP_ENCODER_NO_AUDIO) {
        if (abitrate < 0) {
            ret = ERROR_ENCODER_ABITRATE;
            srs_error("invalid abitrate: %d, ret=%d", abitrate, ret);
            return ret;
        }
        if (asample_rate < 0) {
            ret = ERROR_ENCODER_ASAMPLE_RATE;
            srs_error("invalid sample rate: %d, ret=%d", asample_rate, ret);
            return ret;
        }
        if (achannels != 0 && achannels != 1 && achannels != 2) {
            ret = ERROR_ENCODER_ACHANNELS;
            srs_error("invalid achannels, must be 1 or 2, actual %d, ret=%d", achannels, ret);
            return ret;
        }
    }
    if (_output.empty()) {
        ret = ERROR_ENCODER_OUTPUT;
        srs_error("invalid empty output, ret=%d", ret);
        return ret;
    }
    
    // for not rtmp input, donot append the iformat,
    // for example, "-f flv" before "-i udp://192.168.1.252:2222"
    // @see https://github.com/simple-rtmp-server/srs/issues/290
    if (input.find("rtmp://") != 0) {
        iformat = "";
    }
    
    return ret;
}

int SrsFFMPEG::initialize_copy()
{
    int ret = ERROR_SUCCESS;
    
    vcodec = SRS_RTMP_ENCODER_COPY;
    acodec = SRS_RTMP_ENCODER_COPY;

    if (_output.empty()) {
        ret = ERROR_ENCODER_OUTPUT;
        srs_error("invalid empty output, ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsFFMPEG::start()
{
    int ret = ERROR_SUCCESS;
    
    if (process->started()) {
        return ret;
    }
    
    // prepare exec params
    // @remark we should never use stack variable, use heap to alloc to make lldb happy.
    #define SRS_TMP_SIZE 512
    char* tmp = new char[SRS_TMP_SIZE];
    SrsAutoFree(char, tmp);
    
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
            snprintf(tmp, SRS_TMP_SIZE, "%d", vbitrate * 1000);
            params.push_back(tmp);
        }
        
        if (vfps > 0) {
            params.push_back("-r");
            snprintf(tmp, SRS_TMP_SIZE, "%.2f", vfps);
            params.push_back(tmp);
        }
        
        if (vwidth > 0 && vheight > 0) {
            params.push_back("-s");
            snprintf(tmp, SRS_TMP_SIZE, "%dx%d", vwidth, vheight);
            params.push_back(tmp);
        }
        
        // TODO: add aspect if needed.
        if (vwidth > 0 && vheight > 0) {
            params.push_back("-aspect");
            snprintf(tmp, SRS_TMP_SIZE, "%d:%d", vwidth, vheight);
            params.push_back(tmp);
        }
        
        if (vthreads > 0) {
            params.push_back("-threads");
            snprintf(tmp, SRS_TMP_SIZE, "%d", vthreads);
            params.push_back(tmp);
        }
        
        params.push_back("-profile:v");
        params.push_back(vprofile);
        
        params.push_back("-preset");
        params.push_back(vpreset);
        
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
                snprintf(tmp, SRS_TMP_SIZE, "%d", abitrate * 1000);
                params.push_back(tmp);
            }
            
            if (asample_rate > 0) {
                params.push_back("-ar");
                snprintf(tmp, SRS_TMP_SIZE, "%d", asample_rate);
                params.push_back(tmp);
            }
            
            if (achannels > 0) {
                params.push_back("-ac");
                snprintf(tmp, SRS_TMP_SIZE, "%d", achannels);
                params.push_back(tmp);
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
    if (false && !log_file.empty()) {
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
    if ((ret = process->initialize(ffmpeg, params)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return process->start();
}

int SrsFFMPEG::cycle()
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


