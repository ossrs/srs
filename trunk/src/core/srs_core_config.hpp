/*
The MIT License (MIT)

Copyright (c) 2013 winlin
Copyright (c) 2013 wenjiegit

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

#ifndef SRS_CORE_CONIFG_HPP
#define SRS_CORE_CONIFG_HPP

/*
#include <srs_core_config.hpp>
*/
#include <srs_core.hpp>

#include <vector>
#include <string>

#include <srs_core_reload.hpp>

// default vhost for rtmp
#define RTMP_VHOST_DEFAULT "__defaultVhost__"

#define SRS_LOCALHOST "127.0.0.1"
#define RTMP_DEFAULT_PORT 1935
#define RTMP_DEFAULT_PORTS "1935"

#define SRS_CONF_DEFAULT_HLS_PATH "./objs/nginx/html"
#define SRS_CONF_DEFAULT_HLS_FRAGMENT 10
#define SRS_CONF_DEFAULT_HLS_WINDOW 60
// in ms, for HLS aac sync time.
#define SRS_CONF_DEFAULT_AAC_SYNC 100
// in ms, for HLS aac flush the audio
#define SRS_CONF_DEFAULT_AAC_DELAY 300
// in seconds, the live queue length.
#define SRS_CONF_DEFAULT_QUEUE_LENGTH 30
// in seconds, the paused queue length.
#define SRS_CONF_DEFAULT_PAUSED_LENGTH 10

#define SRS_CONF_DEFAULT_CHUNK_SIZE 4096

#define SRS_STAGE_PLAY_USER_INTERVAL_MS 1300
#define SRS_STAGE_PUBLISH_USER_INTERVAL_MS 1100
#define SRS_STAGE_FORWARDER_INTERVAL_MS 2000
#define SRS_STAGE_ENCODER_INTERVAL_MS 2000
#define SRS_STAGE_HLS_INTERVAL_MS 2000

class SrsFileBuffer;

class SrsConfDirective
{
public:
	int conf_line;
	std::string name;
	std::vector<std::string> args;
	std::vector<SrsConfDirective*> directives;
public:
	SrsConfDirective();
	virtual ~SrsConfDirective();
	std::string arg0();
	std::string arg1(); 
	std::string arg2();
	SrsConfDirective* at(int index);
	SrsConfDirective* get(std::string _name);
	SrsConfDirective* get(std::string _name, std::string _arg0);
public:
	virtual int parse(const char* filename);
public:
	enum SrsDirectiveType{parse_file, parse_block};
	virtual int parse_conf(SrsFileBuffer* buffer, SrsDirectiveType type);
	virtual int read_token(SrsFileBuffer* buffer, std::vector<std::string>& args);
};

/**
* the config parser.
* for the config supports reload, so never keep the reference cross st-thread,
* that is, never save the SrsConfDirective* get by any api of config,
* for it maybe free in the reload st-thread cycle.
* you can keep it before st-thread switch, or simply never keep it.
*/
class SrsConfig
{
private:
	bool show_help;
	bool show_version;
	std::string config_file;
	SrsConfDirective* root;
	std::vector<ISrsReloadHandler*> subscribes;
public:
	SrsConfig();
	virtual ~SrsConfig();
public:
	virtual int reload();
	virtual void subscribe(ISrsReloadHandler* handler);
	virtual void unsubscribe(ISrsReloadHandler* handler);
public:
	virtual int parse_options(int argc, char** argv);

private:
	virtual int parse_file(const char* filename);
	virtual int parse_argv(int& i, char** argv);
	virtual void print_help(char** argv);
public:
	virtual SrsConfDirective* 	get_vhost(std::string vhost);
	virtual bool    		  	get_vhost_enabled(std::string vhost);
	virtual bool    		  	get_vhost_enabled(SrsConfDirective* vhost);
	virtual SrsConfDirective* 	get_vhost_on_connect(std::string vhost);
	virtual SrsConfDirective* 	get_vhost_on_close(std::string vhost);
	virtual SrsConfDirective* 	get_vhost_on_publish(std::string vhost);
	virtual SrsConfDirective* 	get_vhost_on_unpublish(std::string vhost);
	virtual SrsConfDirective* 	get_vhost_on_play(std::string vhost);
	virtual SrsConfDirective* 	get_vhost_on_stop(std::string vhost);
	virtual SrsConfDirective* 	get_transcode(std::string vhost, std::string scope);
	virtual bool    		  	get_transcode_enabled(SrsConfDirective* transcode);
	virtual std::string			get_transcode_ffmpeg(SrsConfDirective* transcode);
	virtual void				get_transcode_engines(SrsConfDirective* transcode, std::vector<SrsConfDirective*>& engines);
	virtual bool				get_engine_enabled(SrsConfDirective* engine);
	virtual std::string			get_engine_vcodec(SrsConfDirective* engine);
	virtual int					get_engine_vbitrate(SrsConfDirective* engine);
	virtual double				get_engine_vfps(SrsConfDirective* engine);
	virtual int					get_engine_vwidth(SrsConfDirective* engine);
	virtual int					get_engine_vheight(SrsConfDirective* engine);
	virtual int					get_engine_vthreads(SrsConfDirective* engine);
	virtual std::string			get_engine_vprofile(SrsConfDirective* engine);
	virtual std::string			get_engine_vpreset(SrsConfDirective* engine);
	virtual void				get_engine_vparams(SrsConfDirective* engine, std::vector<std::string>& vparams);
	virtual void				get_engine_vfilter(SrsConfDirective* engine, std::vector<std::string>& vfilter);
	virtual std::string			get_engine_acodec(SrsConfDirective* engine);
	virtual int					get_engine_abitrate(SrsConfDirective* engine);
	virtual int					get_engine_asample_rate(SrsConfDirective* engine);
	virtual int					get_engine_achannels(SrsConfDirective* engine);
	virtual void				get_engine_aparams(SrsConfDirective* engine, std::vector<std::string>& aparams);
	virtual std::string			get_engine_output(SrsConfDirective* engine);
	virtual std::string			get_log_dir();
	virtual int					get_max_connections();
	virtual bool				get_gop_cache(std::string vhost);
	virtual double				get_queue_length(std::string vhost);
	virtual SrsConfDirective*	get_forward(std::string vhost);
private:
	virtual SrsConfDirective*	get_hls(std::string vhost);
public:
	virtual bool				get_hls_enabled(std::string vhost);
	virtual std::string			get_hls_path(std::string vhost);
	virtual double				get_hls_fragment(std::string vhost);
	virtual double				get_hls_window(std::string vhost);
	virtual SrsConfDirective*	get_refer(std::string vhost);
	virtual SrsConfDirective*	get_refer_play(std::string vhost);
	virtual SrsConfDirective*	get_refer_publish(std::string vhost);
	virtual SrsConfDirective*	get_listen();
	virtual int					get_chunk_size();
    virtual int					get_chunk_size(const std::string& vhost);
	virtual int					get_pithy_print_publish();
	virtual int					get_pithy_print_forwarder();
	virtual int					get_pithy_print_encoder();
	virtual int					get_pithy_print_hls();
	virtual int					get_pithy_print_play();
    virtual bool                get_bw_check_enabled(const std::string &vhost, const std::string &key);
    virtual void                get_bw_check_settings(const std::string &vhost, int64_t &interval_ms, int &limit_kbps);
};

/**
* deep compare directive.
*/
bool srs_directive_equals(SrsConfDirective* a, SrsConfDirective* b);

// global config
extern SrsConfig* config;

#endif
