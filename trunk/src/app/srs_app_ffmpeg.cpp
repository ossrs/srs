/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(ossrs)

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
#include <srs_app_utility.hpp>

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
    started            = false;
    fast_stopped       = false;
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
}

SrsFFMPEG::~SrsFFMPEG()
{
    stop();
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
    
    // @see, https://github.com/ossrs/srs/issues/145
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
    // @see https://github.com/ossrs/srs/issues/290
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
            snprintf(tmp, sizeof(tmp), "%d", vbitrate * 1000);
            params.push_back(tmp);
        }
        
        if (vfps > 0) {
            params.push_back("-r");
            snprintf(tmp, sizeof(tmp), "%.2f", vfps);
            params.push_back(tmp);
        }
        
        if (vwidth > 0 && vheight > 0) {
            params.push_back("-s");
            snprintf(tmp, sizeof(tmp), "%dx%d", vwidth, vheight);
            params.push_back(tmp);
        }
        
        // TODO: add aspect if needed.
        if (vwidth > 0 && vheight > 0) {
            params.push_back("-aspect");
            snprintf(tmp, sizeof(tmp), "%d:%d", vwidth, vheight);
            params.push_back(tmp);
        }
        
        if (vthreads > 0) {
            params.push_back("-threads");
            snprintf(tmp, sizeof(tmp), "%d", vthreads);
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
                snprintf(tmp, sizeof(tmp), "%d", abitrate * 1000);
                params.push_back(tmp);
            }
            
            if (asample_rate > 0) {
                params.push_back("-ar");
                snprintf(tmp, sizeof(tmp), "%d", asample_rate);
                params.push_back(tmp);
            }
            
            if (achannels > 0) {
                params.push_back("-ac");
                snprintf(tmp, sizeof(tmp), "%d", achannels);
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

    std::string cli;
    if (true) {
        for (int i = 0; i < (int)params.size(); i++) {
            std::string ffp = params[i];
            cli += ffp;
            if (i < (int)params.size() - 1) {
                cli += " ";
            }
        }
        srs_trace("start ffmpeg, log: %s, params: %s", log_file.c_str(), cli.c_str());
    }
    
    // for log
    int cid = _srs_context->get_id();
    
    // TODO: fork or vfork?
    if ((pid = fork()) < 0) {
        ret = ERROR_ENCODER_FORK;
        srs_error("vfork process failed. ret=%d", ret);
        return ret;
    }
    
    // child process: ffmpeg encoder engine.
    if (pid == 0) {
        // ignore the SIGINT and SIGTERM
        signal(SIGINT, SIG_IGN);
        signal(SIGTERM, SIG_IGN);
        
        // redirect logs to file.
        int log_fd = -1;
        int flags = O_CREAT|O_WRONLY|O_APPEND;
        mode_t mode = S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH;
        if ((log_fd = ::open(log_file.c_str(), flags, mode)) < 0) {
            ret = ERROR_ENCODER_OPEN;
            srs_error("open encoder file %s failed. ret=%d", log_file.c_str(), ret);
            exit(ret);
        }
        
        // log basic info
        if (true) {
            char buf[4096];
            int pos = 0;
            pos += snprintf(buf + pos, sizeof(buf) - pos, "\n");
            pos += snprintf(buf + pos, sizeof(buf) - pos, "ffmpeg cid=%d\n", cid);
            pos += snprintf(buf + pos, sizeof(buf) - pos, "log=%s\n", log_file.c_str());
            pos += snprintf(buf + pos, sizeof(buf) - pos, "params: %s\n", cli.c_str());
            ::write(log_fd, buf, pos);
        }
        
        // dup to stdout and stderr.
        if (dup2(log_fd, STDOUT_FILENO) < 0) {
            ret = ERROR_ENCODER_DUP2;
            srs_error("dup2 encoder file failed. ret=%d", ret);
            exit(ret);
        }
        if (dup2(log_fd, STDERR_FILENO) < 0) {
            ret = ERROR_ENCODER_DUP2;
            srs_error("dup2 encoder file failed. ret=%d", ret);
            exit(ret);
        }
        
        // close log fd
        ::close(log_fd);
        // close other fds
        // TODO: do in right way.
        for (int i = 3; i < 1024; i++) {
            ::close(i);
        }
        
        // memory leak in child process, it's ok.
        char** charpv_params = new char*[params.size() + 1];
        for (int i = 0; i < (int)params.size(); i++) {
            std::string& p = params[i];
            charpv_params[i] = (char*)p.data();
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
    
    // ffmpeg is prepare to stop, donot cycle.
    if (fast_stopped) {
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
    if (!started) {
        return;
    }
    
    // kill the ffmpeg,
    // when rewind, upstream will stop publish(unpublish),
    // unpublish event will stop all ffmpeg encoders,
    // then publish will start all ffmpeg encoders.
    int ret = srs_kill_forced(pid);
    if (ret != ERROR_SUCCESS) {
        srs_warn("ignore kill the encoder failed, pid=%d. ret=%d", pid, ret);
        return;
    }
    
    // terminated, set started to false to stop the cycle.
    started = false;
}

void SrsFFMPEG::fast_stop()
{
    int ret = ERROR_SUCCESS;
    
    if (!started) {
        return;
    }
    
    if (pid <= 0) {
        return;
    }
    
    if (kill(pid, SIGTERM) < 0) {
        ret = ERROR_SYSTEM_KILL;
        srs_warn("ignore fast stop ffmpeg failed, pid=%d. ret=%d", pid, ret);
        return;
    }
    
    return;
}

#endif


