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

#include <srs_core_encoder.hpp>

#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>

#include <algorithm>

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_core_config.hpp>
#include <srs_core_rtmp.hpp>
#include <srs_core_pithy_print.hpp>
#include <srs_core_protocol.hpp>

#ifdef SRS_FFMPEG

#define SRS_ENCODER_COPY	"copy"
#define SRS_ENCODER_VCODEC 	"libx264"
#define SRS_ENCODER_ACODEC 	"libaacplus"

// for encoder to detect the dead loop
static std::vector<std::string> _transcoded_url;

SrsFFMPEG::SrsFFMPEG(std::string ffmpeg_bin)
{
	started			= false;
	pid				= -1;
	ffmpeg 			= ffmpeg_bin;
	
	vbitrate 		= 0;
	vfps 			= 0;
	vwidth 			= 0;
	vheight 		= 0;
	vthreads 		= 0;
	abitrate 		= 0;
	asample_rate 	= 0;
	achannels 		= 0;
	
	log_fd = -1;
}

SrsFFMPEG::~SrsFFMPEG()
{
	stop();
}

int SrsFFMPEG::initialize(SrsRequest* req, SrsConfDirective* engine)
{
	int ret = ERROR_SUCCESS;
	
	config->get_engine_vfilter(engine, vfilter);
	vcodec 			= config->get_engine_vcodec(engine);
	vbitrate 		= config->get_engine_vbitrate(engine);
	vfps 			= config->get_engine_vfps(engine);
	vwidth 			= config->get_engine_vwidth(engine);
	vheight 		= config->get_engine_vheight(engine);
	vthreads 		= config->get_engine_vthreads(engine);
	vprofile 		= config->get_engine_vprofile(engine);
	vpreset 		= config->get_engine_vpreset(engine);
	config->get_engine_vparams(engine, vparams);
	acodec 			= config->get_engine_acodec(engine);
	abitrate 		= config->get_engine_abitrate(engine);
	asample_rate 	= config->get_engine_asample_rate(engine);
	achannels 		= config->get_engine_achannels(engine);
	config->get_engine_aparams(engine, aparams);
	output 			= config->get_engine_output(engine);
	
	// ensure the size is even.
	vwidth -= vwidth % 2;
	vheight -= vheight % 2;

	// input stream, from local.
	// ie. rtmp://127.0.0.1:1935/live/livestream
	input = "rtmp://127.0.0.1:";
	input += req->port;
	input += "/";
	input += req->app;
	input += "?vhost=";
	input += req->vhost;
	input += "/";
	input += req->stream;
	
	// output stream, to other/self server
	// ie. rtmp://127.0.0.1:1935/live/livestream_sd
	output = srs_replace(output, "[vhost]", req->vhost);
	output = srs_replace(output, "[port]", req->port);
	output = srs_replace(output, "[app]", req->app);
	output = srs_replace(output, "[stream]", req->stream);
	output = srs_replace(output, "[engine]", engine->arg0());
	
	// write ffmpeg info to log file.
	log_file = config->get_log_dir();
	log_file += "/";
	log_file += "encoder";
	log_file += "-";
	log_file += req->vhost;
	log_file += "-";
	log_file += req->app;
	log_file += "-";
	log_file += req->stream;
	log_file += ".log";

	// important: loop check, donot transcode again.
	std::vector<std::string>::iterator it;
	it = std::find(_transcoded_url.begin(), _transcoded_url.end(), input);
	if (it != _transcoded_url.end()) {
		ret = ERROR_ENCODER_LOOP;
		srs_info("detect a loop cycle, input=%s, output=%s, ignore it. ret=%d",
			input.c_str(), output.c_str(), ret);
		return ret;
	}
	_transcoded_url.push_back(output);
	
	if (vcodec != SRS_ENCODER_COPY) {
		if (vcodec != SRS_ENCODER_VCODEC) {
			ret = ERROR_ENCODER_VCODEC;
			srs_error("invalid vcodec, must be %s, actual %s, ret=%d",
				SRS_ENCODER_VCODEC, vcodec.c_str(), ret);
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
	
	if (acodec != SRS_ENCODER_COPY) {
		if (acodec != SRS_ENCODER_ACODEC) {
			ret = ERROR_ENCODER_ACODEC;
			srs_error("invalid acodec, must be %s, actual %s, ret=%d",
				SRS_ENCODER_ACODEC, acodec.c_str(), ret);
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
	if (output.empty()) {
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
	params.push_back("-vcodec");
	params.push_back(vcodec);
	
	// the codec params is disabled when copy
	if (vcodec != SRS_ENCODER_COPY) {
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
	params.push_back("-acodec");
	params.push_back(acodec);
	
	// the codec params is disabled when copy
	if (acodec != SRS_ENCODER_COPY) {
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
	params.push_back(output);

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
		// 		1) SIGHUP	 2) SIGINT	 3) SIGQUIT
		// other signals, directly exit(123), for example:
		//		9) SIGKILL	15) SIGTERM
		int status = 0;
		if (waitpid(pid, &status, 0) < 0) {
			srs_warn("wait the encoder quit failed, ignored. pid=%d", pid);
		}
		
		srs_trace("stop the encoder success. pid=%d", pid);
		pid = -1;
	}
	
	std::vector<std::string>::iterator it;
	it = std::find(_transcoded_url.begin(), _transcoded_url.end(), output);
	if (it != _transcoded_url.end()) {
		_transcoded_url.erase(it);
	}
}

SrsEncoder::SrsEncoder()
{
	pthread = new SrsThread(this, SRS_ENCODER_SLEEP_US);
	pithy_print = new SrsPithyPrint(SRS_STAGE_ENCODER);
}

SrsEncoder::~SrsEncoder()
{
	on_unpublish();
	
	srs_freep(pthread);
}

int SrsEncoder::on_publish(SrsRequest* req)
{
	int ret = ERROR_SUCCESS;

	ret = parse_scope_engines(req);
	
	// ignore the loop encoder
	if (ret == ERROR_ENCODER_LOOP) {
		clear_engines();
		ret = ERROR_SUCCESS;
	}
	
	// return for error or no engine.
	if (ret != ERROR_SUCCESS || ffmpegs.empty()) {
		return ret;
	}
    
    // start thread to run all encoding engines.
    if ((ret = pthread->start()) != ERROR_SUCCESS) {
        srs_error("st_thread_create failed. ret=%d", ret);
        return ret;
    }
    
	return ret;
}

void SrsEncoder::on_unpublish()
{
	pthread->stop();
	clear_engines();
}

int SrsEncoder::cycle()
{
	int ret = ERROR_SUCCESS;
	
	std::vector<SrsFFMPEG*>::iterator it;
	for (it = ffmpegs.begin(); it != ffmpegs.end(); ++it) {
		SrsFFMPEG* ffmpeg = *it;
		
		// start all ffmpegs.
		if ((ret = ffmpeg->start()) != ERROR_SUCCESS) {
			srs_error("ffmpeg start failed. ret=%d", ret);
			return ret;
		}

		// check ffmpeg status.
		if ((ret = ffmpeg->cycle()) != ERROR_SUCCESS) {
			srs_error("ffmpeg cycle failed. ret=%d", ret);
			return ret;
		}
	}

	// pithy print
	encoder();
	pithy_print->elapse(SRS_ENCODER_SLEEP_US / 1000);
	
	return ret;
}

void SrsEncoder::on_leave_loop()
{
	// kill ffmpeg when finished and it alive
	std::vector<SrsFFMPEG*>::iterator it;

	for (it = ffmpegs.begin(); it != ffmpegs.end(); ++it) {
		SrsFFMPEG* ffmpeg = *it;
		ffmpeg->stop();
	}
}

void SrsEncoder::clear_engines()
{
	std::vector<SrsFFMPEG*>::iterator it;
	
	for (it = ffmpegs.begin(); it != ffmpegs.end(); ++it) {
		SrsFFMPEG* ffmpeg = *it;
		srs_freep(ffmpeg);
	}

	ffmpegs.clear();
}

SrsFFMPEG* SrsEncoder::at(int index)
{
	return ffmpegs[index];
}

int SrsEncoder::parse_scope_engines(SrsRequest* req)
{
	int ret = ERROR_SUCCESS;
	
	// parse all transcode engines.
	SrsConfDirective* conf = NULL;
	
	// parse vhost scope engines
	std::string scope = "";
	if ((conf = config->get_transcode(req->vhost, scope)) != NULL) {
		if ((ret = parse_transcode(req, conf)) != ERROR_SUCCESS) {
			srs_error("parse vhost scope=%s transcode engines failed. "
				"ret=%d", scope.c_str(), ret);
			return ret;
		}
	}
	// parse app scope engines
	scope = req->app;
	if ((conf = config->get_transcode(req->vhost, scope)) != NULL) {
		if ((ret = parse_transcode(req, conf)) != ERROR_SUCCESS) {
			srs_error("parse app scope=%s transcode engines failed. "
				"ret=%d", scope.c_str(), ret);
			return ret;
		}
	}
	// parse stream scope engines
	scope += "/";
	scope += req->stream;
	if ((conf = config->get_transcode(req->vhost, scope)) != NULL) {
		if ((ret = parse_transcode(req, conf)) != ERROR_SUCCESS) {
			srs_error("parse stream scope=%s transcode engines failed. "
				"ret=%d", scope.c_str(), ret);
			return ret;
		}
	}
	
	return ret;
}

int SrsEncoder::parse_transcode(SrsRequest* req, SrsConfDirective* conf)
{
	int ret = ERROR_SUCCESS;
	
	srs_assert(conf);
	
	// enabled
	if (!config->get_transcode_enabled(conf)) {
		srs_trace("ignore the disabled transcode: %s", 
			conf->arg0().c_str());
		return ret;
	}
	
	// ffmpeg
	std::string ffmpeg_bin = config->get_transcode_ffmpeg(conf);
	if (ffmpeg_bin.empty()) {
		srs_trace("ignore the empty ffmpeg transcode: %s", 
			conf->arg0().c_str());
		return ret;
	}
	
	// get all engines.
	std::vector<SrsConfDirective*> engines;
	config->get_transcode_engines(conf, engines);
	if (engines.empty()) {
		srs_trace("ignore the empty transcode engine: %s", 
			conf->arg0().c_str());
		return ret;
	}
	
	// create engine
	for (int i = 0; i < (int)engines.size(); i++) {
		SrsConfDirective* engine = engines[i];
		if (!config->get_engine_enabled(engine)) {
			srs_trace("ignore the diabled transcode engine: %s %s", 
				conf->arg0().c_str(), engine->arg0().c_str());
			continue;
		}
		
		SrsFFMPEG* ffmpeg = new SrsFFMPEG(ffmpeg_bin);
		
		if ((ret = ffmpeg->initialize(req, engine)) != ERROR_SUCCESS) {
			srs_freep(ffmpeg);
			
			// if got a loop, donot transcode the whole stream.
			if (ret == ERROR_ENCODER_LOOP) {
				break;
			}
			
			srs_error("invalid transcode engine: %s %s", 
				conf->arg0().c_str(), engine->arg0().c_str());
			return ret;
		}

		ffmpegs.push_back(ffmpeg);
	}
	
	return ret;
}

void SrsEncoder::encoder()
{
	// reportable
	if (pithy_print->can_print()) {
		// TODO: FIXME: show more info.
		srs_trace("-> time=%"PRId64", encoders=%d", pithy_print->get_age(), (int)ffmpegs.size());
	}
}

#endif

