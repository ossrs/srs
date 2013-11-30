/*
The MIT License (MIT)

Copyright (c) 2013 winlin

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

#ifndef SRS_CORE_ENCODER_HPP
#define SRS_CORE_ENCODER_HPP

/*
#include <srs_core_encoder.hpp>
*/
#include <srs_core.hpp>

#include <string>
#include <vector>

#include <st.h>

class SrsConfDirective;

/**
* a transcode engine: ffmepg,
* used to transcode a stream to another.
*/
class SrsFFMPEG
{
private:
	bool started;
	pid_t pid;
private:
	std::string 				ffmpeg;
	std::string 				vcodec;
	int 						vbitrate;
	double 						vfps;
	int 						vwidth;
	int 						vheight;
	int 						vthreads;
	std::string 				vprofile;
	std::string 				vpreset;
	std::string 				vparams;
	std::string 				acodec;
	int 						abitrate;
	int 						asample_rate;
	int 						achannels;
	std::string					aparams;
	std::string 				output;
	std::string 				input;
public:
	SrsFFMPEG(std::string ffmpeg_bin);
	virtual ~SrsFFMPEG();
public:
	virtual int initialize(std::string vhost, std::string port, std::string app, std::string stream, SrsConfDirective* engine);
	virtual int start();
	virtual void stop();
};

/**
* the encoder for a stream,
* may use multiple ffmpegs to transcode the specified stream.
*/
class SrsEncoder
{
private:
	std::string vhost;
	std::string port;
	std::string app;
	std::string stream;
private:
	std::vector<SrsFFMPEG*> ffmpegs;
private:
	st_thread_t tid;
	bool loop;
public:
	SrsEncoder();
	virtual ~SrsEncoder();
public:
	virtual int on_publish(std::string vhost, std::string port, std::string app, std::string stream);
	virtual void on_unpublish();
private:
	virtual int parse_scope_engines();
	virtual void clear_engines();
	virtual SrsFFMPEG* at(int index);
	virtual int parse_transcode(SrsConfDirective* conf);
	virtual int cycle();
	virtual void encoder_cycle();
	static void* encoder_thread(void* arg);
};

#endif
