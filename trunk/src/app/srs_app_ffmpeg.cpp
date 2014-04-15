/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

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

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_protocol_rtmp.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_protocol_rtmp_stack.hpp>

#ifdef SRS_AUTO_FFMPEG

#define SRS_RTMP_ENCODER_COPY    "copy"
#define SRS_RTMP_ENCODER_NO_VIDEO    "vn"
#define SRS_RTMP_ENCODER_NO_AUDIO    "an"
// only support libx264 encoder.
#define SRS_RTMP_ENCODER_VCODEC     "libx264"
// any aac encoder is ok which contains the aac,
// for example, libaacplus, aac, fdkaac
#define SRS_RTMP_ENCODER_ACODEC     "aac"

SrsFFMPEG::SrsFFMPEG(std::string ffmpeg_bin)
{
    started            = false;
    pid                = -1;
    ffmpeg             = ffmpeg_bin;
    
    vbitrate         = 0;
    vfps             = 0;
    vwidth             = 0;
    vheight         = 0;
    vthreads         = 0;
    abitrate         = 0;
    asample_rate     = 0;
    achannels         = 0;
    
    log_fd = -1;
}

SrsFFMPEG::~SrsFFMPEG()
{
    stop();
}

void SrsFFMPEG::set_iparams(string iparams)
{
    _iparams = iparams;
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
    
    _srs_config->get_engine_vfilter(engine, vfilter);
    vcodec             = _srs_config->get_engine_vcodec(engine);
    vbitrate         = _srs_config->get_engine_vbitrate(engine);
    vfps             = _srs_config->get_engine_vfps(engine);
    vwidth             = _srs_config->get_engine_vwidth(engine);
    vheight         = _srs_config->get_engine_vheight(engine);
    vthreads         = _srs_config->get_engine_vthreads(engine);
    vprofile         = _srs_config->get_engine_vprofile(engine);
    vpreset         = _srs_config->get_engine_vpreset(engine);
    _srs_config->get_engine_vparams(engine, vparams);
    acodec             = _srs_config->get_engine_acodec(engine);
    abitrate         = _srs_config->get_engine_abitrate(engine);
    asample_rate     = _srs_config->get_engine_asample_rate(engine);
    achannels         = _srs_config->get_engine_achannels(engine);
    _srs_config->get_engine_aparams(engine, aparams);
    
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
        if (vbitrate <= 0) {
            ret = ERROR_ENCODER_VBITRATE;
            srs_error("invalid vbitrate: %d, ret=%d", vbitrate, ret);
            return ret;
        }
        if (vfps <= 0) {
            ret = ERROR_ENCODER_VFPS;
            srs_error("invalid vfps: %.2f, ret=%d", vfps, ret);
            return ret;
        }
        if (vwidth <= 0) {
            ret = ERROR_ENCODER_VWIDTH;
            srs_error("invalid vwidth: %d, ret=%d", vwidth, ret);
            return ret;
        }
        if (vheight <= 0) {
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
    
    if (acodec != SRS_RTMP_ENCODER_COPY && acodec != SRS_RTMP_ENCODER_NO_AUDIO) {
        if (acodec.find(SRS_RTMP_ENCODER_ACODEC) == std::string::npos) {
            ret = ERROR_ENCODER_ACODEC;
            srs_error("invalid acodec, must be %s, actual %s, ret=%d",
                SRS_RTMP_ENCODER_ACODEC, acodec.c_str(), ret);
            return ret;
        }
        if (abitrate <= 0) {
            ret = ERROR_ENCODER_ABITRATE;
            srs_error("invalid abitrate: %d, ret=%d", 
                abitrate, ret);
            return ret;
        }
        if (asample_rate <= 0) {
            ret = ERROR_ENCODER_ASAMPLE_RATE;
            srs_error("invalid sample rate: %d, ret=%d", 
                asample_rate, ret);
            return ret;
        }
        if (achannels != 1 && achannels != 2) {
            ret = ERROR_ENCODER_ACHANNELS;
            srs_error("invalid achannels, must be 1 or 2, actual %d, ret=%d", 
                achannels, ret);
            return ret;
        }
    }
    if (_output.empty()) {
        ret = ERROR_ENCODER_OUTPUT;
        srs_error("invalid empty output, ret=%d", ret);
        return ret;
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
    
    if (started) {
        return ret;
    }
    
    // prepare exec params
    char tmp[256];
    std::vector<std::string> params;
    
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
    params.push_back("-f");
    params.push_back("flv");
    
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
        params.push_back("-b:v");
        snprintf(tmp, sizeof(tmp), "%d", vbitrate * 1000);
        params.push_back(tmp);
        
        params.push_back("-r");
        snprintf(tmp, sizeof(tmp), "%.2f", vfps);
        params.push_back(tmp);
        
        params.push_back("-s");
        snprintf(tmp, sizeof(tmp), "%dx%d", vwidth, vheight);
        params.push_back(tmp);
        
        // TODO: add aspect if needed.
        params.push_back("-aspect");
        snprintf(tmp, sizeof(tmp), "%d:%d", vwidth, vheight);
        params.push_back(tmp);
        
        params.push_back("-threads");
        snprintf(tmp, sizeof(tmp), "%d", vthreads);
        params.push_back(tmp);
        
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
    if (acodec != SRS_RTMP_ENCODER_COPY && acodec != SRS_RTMP_ENCODER_NO_AUDIO) {
        params.push_back("-b:a");
        snprintf(tmp, sizeof(tmp), "%d", abitrate * 1000);
        params.push_back(tmp);
        
        params.push_back("-ar");
        snprintf(tmp, sizeof(tmp), "%d", asample_rate);
        params.push_back(tmp);
        
        params.push_back("-ac");
        snprintf(tmp, sizeof(tmp), "%d", achannels);
        params.push_back(tmp);
        
        // aparams
        if (!aparams.empty()) {
            std::vector<std::string>::iterator it;
            for (it = aparams.begin(); it != aparams.end(); ++it) {
                std::string p = *it;
                if (!p.empty()) {
                    params.push_back(p);
                }
            }
        }
    }

    // output
    params.push_back("-f");
    params.push_back("flv");
    
    params.push_back("-y");
    params.push_back(_output);

    if (true) {
        int pparam_size = 8 * 1024;
        char* pparam = new char[pparam_size];
        char* p = pparam;
        char* last = pparam + pparam_size;
        for (int i = 0; i < (int)params.size(); i++) {
            std::string ffp = params[i];
            snprintf(p, last - p, "%s ", ffp.c_str());
            p += ffp.length() + 1;
        }
        srs_trace("start transcoder, log: %s, params: %s", 
            log_file.c_str(), pparam);
        srs_freepa(pparam);
    }
    
    // TODO: fork or vfork?
    if ((pid = fork()) < 0) {
        ret = ERROR_ENCODER_FORK;
        srs_error("vfork process failed. ret=%d", ret);
        return ret;
    }
    
    // child process: ffmpeg encoder engine.
    if (pid == 0) {
        // redirect logs to file.
        int flags = O_CREAT|O_WRONLY|O_APPEND;
        mode_t mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH;
        if ((log_fd = ::open(log_file.c_str(), flags, mode)) < 0) {
            ret = ERROR_ENCODER_OPEN;
            srs_error("open encoder file %s failed. ret=%d", log_file.c_str(), ret);
            return ret;
        }
        if (dup2(log_fd, STDOUT_FILENO) < 0) {
            ret = ERROR_ENCODER_DUP2;
            srs_error("dup2 encoder file failed. ret=%d", ret);
            return ret;
        }
        if (dup2(log_fd, STDERR_FILENO) < 0) {
            ret = ERROR_ENCODER_DUP2;
            srs_error("dup2 encoder file failed. ret=%d", ret);
            return ret;
        }
        // close other fds
        // TODO: do in right way.
        for (int i = 3; i < 1024; i++) {
            ::close(i);
        }
        
        // memory leak in child process, it's ok.
        char** charpv_params = new char*[params.size() + 1];
        for (int i = 0; i < (int)params.size(); i++) {
            std::string p = params[i];
            charpv_params[i] = (char*)p.c_str();
        }
        // EOF: NULL
        charpv_params[params.size()] = NULL;
        
        // TODO: execv or execvp
        ret = execv(ffmpeg.c_str(), charpv_params);
        if (ret < 0) {
            fprintf(stderr, "fork ffmpeg failed, errno=%d(%s)", 
                errno, strerror(errno));
        }
        exit(ret);
    }

    // parent.
    if (pid > 0) {
        started = true;
        srs_trace("vfored ffmpeg encoder engine, pid=%d", pid);
        return ret;
    }
    
    return ret;
}

int SrsFFMPEG::cycle()
{
    int ret = ERROR_SUCCESS;
    
    if (!started) {
        return ret;
    }
    
    int status = 0;
    pid_t p = waitpid(pid, &status, WNOHANG);
    
    if (p < 0) {
        ret = ERROR_SYSTEM_WAITPID;
        srs_error("transcode waitpid failed, pid=%d, ret=%d", pid, ret);
        return ret;
    }
    
    if (p == 0) {
        srs_info("transcode process pid=%d is running.", pid);
        return ret;
    }
    
    srs_trace("transcode process pid=%d terminate, restart it.", pid);
    started = false;
    
    return ret;
}

void SrsFFMPEG::stop()
{
    if (log_fd > 0) {
        ::close(log_fd);
        log_fd = -1;
    }
    
    if (!started) {
        return;
    }
    
    // kill the ffmpeg,
    // when rewind, upstream will stop publish(unpublish),
    // unpublish event will stop all ffmpeg encoders,
    // then publish will start all ffmpeg encoders.
    if (pid > 0) {
        if (kill(pid, SIGKILL) < 0) {
            srs_warn("kill the encoder failed, ignored. pid=%d", pid);
        }
        
        // wait for the ffmpeg to quit.
        // ffmpeg will gracefully quit if signal is:
        //         1) SIGHUP     2) SIGINT     3) SIGQUIT
        // other signals, directly exit(123), for example:
        //        9) SIGKILL    15) SIGTERM
        int status = 0;
        if (waitpid(pid, &status, 0) < 0) {
            srs_warn("wait the encoder quit failed, ignored. pid=%d", pid);
        }
        
        srs_trace("stop the encoder success. pid=%d", pid);
        pid = -1;
    }
}

#endif

