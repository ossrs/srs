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

#include <srs_core_config.hpp>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
// file operations.
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <vector>
#include <algorithm>

#include <srs_core_error.hpp>
#include <srs_core_log.hpp>
#include <srs_core_autofree.hpp>

#define FILE_OFFSET(fd) lseek(fd, 0, SEEK_CUR)

int64_t FILE_SIZE(int fd)
{
	int64_t pre = FILE_OFFSET(fd);
	int64_t pos = lseek(fd, 0, SEEK_END);
	lseek(fd, pre, SEEK_SET);
	return pos;
}

#define LF (char)0x0a
#define CR (char)0x0d

bool is_common_space(char ch)
{
	return (ch == ' ' || ch == '\t' || ch == CR || ch == LF);
}

#define CONF_BUFFER_SIZE 1024 * 1024

SrsFileBuffer::SrsFileBuffer()
{
	fd = -1;
	line = 0;

	pos = last = start = new char[CONF_BUFFER_SIZE];
	end = start + CONF_BUFFER_SIZE;
}

SrsFileBuffer::~SrsFileBuffer()
{
	if (fd > 0) {
		close(fd);
	}
	srs_freepa(start);
}

int SrsFileBuffer::open(const char* filename)
{
	assert(fd == -1);
	
	if ((fd = ::open(filename, O_RDONLY, 0)) < 0) {
		srs_error("open conf file error. errno=%d(%s)", errno, strerror(errno));
		return ERROR_SYSTEM_CONFIG_INVALID;
	}
	
	line = 1;
	
	return ERROR_SUCCESS;
}

SrsConfDirective::SrsConfDirective()
{
}

SrsConfDirective::~SrsConfDirective()
{
	std::vector<SrsConfDirective*>::iterator it;
	for (it = directives.begin(); it != directives.end(); ++it) {
		SrsConfDirective* directive = *it;
		srs_freep(directive);
	}
	directives.clear();
}

std::string SrsConfDirective::arg0()
{
	if (args.size() > 0) {
		return args.at(0);
	}
	
	return "";
}

std::string SrsConfDirective::arg1()
{
	if (args.size() > 1) {
		return args.at(1);
	}
	
	return "";
}

std::string SrsConfDirective::arg2()
{
	if (args.size() > 2) {
		return args.at(2);
	}
	
	return "";
}

SrsConfDirective* SrsConfDirective::at(int index)
{
	return directives.at(index);
}

SrsConfDirective* SrsConfDirective::get(std::string _name)
{
	std::vector<SrsConfDirective*>::iterator it;
	for (it = directives.begin(); it != directives.end(); ++it) {
		SrsConfDirective* directive = *it;
		if (directive->name == _name) {
			return directive;
		}
	}
	
	return NULL;
}

int SrsConfDirective::parse(const char* filename)
{
	int ret = ERROR_SUCCESS;
	
	SrsFileBuffer buffer;
	
	if ((ret = buffer.open(filename)) != ERROR_SUCCESS) {
		return ret;
	}
	
	return parse_conf(&buffer, parse_file);
}

// see: ngx_conf_parse
int SrsConfDirective::parse_conf(SrsFileBuffer* buffer, SrsDirectiveType type)
{
	int ret = ERROR_SUCCESS;
	
	while (true) {
		std::vector<std::string> args;
		ret = read_token(buffer, args);
		
		/**
		* ret maybe:
		* ERROR_SYSTEM_CONFIG_INVALID 		error.
		* ERROR_SYSTEM_CONFIG_DIRECTIVE		directive terminated by ';' found
		* ERROR_SYSTEM_CONFIG_BLOCK_START	token terminated by '{' found
		* ERROR_SYSTEM_CONFIG_BLOCK_END		the '}' found
		* ERROR_SYSTEM_CONFIG_EOF			the config file is done
		*/
		if (ret == ERROR_SYSTEM_CONFIG_INVALID) {
			return ret;
		}
		if (ret == ERROR_SYSTEM_CONFIG_BLOCK_END) {
			if (type != parse_block) {
				srs_error("line %d: unexpected \"}\"", buffer->line);
				return ret;
			}
			return ERROR_SUCCESS;
		}
		if (ret == ERROR_SYSTEM_CONFIG_EOF) {
			if (type == parse_block) {
				srs_error("line %d: unexpected end of file, expecting \"}\"", buffer->line);
				return ret;
			}
			return ERROR_SUCCESS;
		}
		
		if (args.empty()) {
			srs_error("line %d: empty directive.", buffer->line);
			return ret;
		}
		
		// build directive tree.
		SrsConfDirective* directive = new SrsConfDirective();

		directive->conf_line = buffer->line;
		directive->name = args[0];
		args.erase(args.begin());
		directive->args.swap(args);
		
		directives.push_back(directive);
		
		if (ret == ERROR_SYSTEM_CONFIG_BLOCK_START) {
			if ((ret = directive->parse_conf(buffer, parse_block)) != ERROR_SUCCESS) {
				return ret;
			}
		}
	}
	
	return ret;
}

// see: ngx_conf_read_token
int SrsConfDirective::read_token(SrsFileBuffer* buffer, std::vector<std::string>& args)
{
	int ret = ERROR_SUCCESS;

	char* pstart = buffer->pos;
	int startline = buffer->line;

	bool sharp_comment = false;
	
	bool d_quoted = false;
	bool s_quoted = false;
	
	bool need_space = false;
	bool last_space = true;
	
	while (true) {
		if ((ret = refill_buffer(buffer, d_quoted, s_quoted, startline, pstart)) != ERROR_SUCCESS) {
			if (!args.empty() || !last_space) {
				srs_error("line %d: unexpected end of file, expecting ; or \"}\"", buffer->line);
				return ERROR_SYSTEM_CONFIG_INVALID;
			}
			return ret;
		}
		
		char ch = *buffer->pos++;
		
		if (ch == LF) {
			buffer->line++;
			sharp_comment = false;
		}
		
		if (sharp_comment) {
			continue;
		}
		
		if (need_space) {
			if (is_common_space(ch)) {
				last_space = true;
				need_space = false;
				continue;
			}
			if (ch == ';') {
				return ERROR_SYSTEM_CONFIG_DIRECTIVE;
			}
			if (ch == '{') {
				return ERROR_SYSTEM_CONFIG_BLOCK_START;
			}
			srs_error("line %d: unexpected '%c'", buffer->line, ch);
			return ERROR_SYSTEM_CONFIG_INVALID; 
		}
		
		// last charecter is space.
		if (last_space) {
			if (is_common_space(ch)) {
				continue;
			}
			pstart = buffer->pos - 1;
			startline = buffer->line;
			switch (ch) {
				case ';':
					if (args.size() == 0) {
						srs_error("line %d: unexpected ';'", buffer->line);
						return ERROR_SYSTEM_CONFIG_INVALID;
					}
					return ERROR_SYSTEM_CONFIG_DIRECTIVE;
				case '{':
					if (args.size() == 0) {
						srs_error("line %d: unexpected '{'", buffer->line);
						return ERROR_SYSTEM_CONFIG_INVALID;
					}
					return ERROR_SYSTEM_CONFIG_BLOCK_START;
				case '}':
					if (args.size() != 0) {
						srs_error("line %d: unexpected '}'", buffer->line);
						return ERROR_SYSTEM_CONFIG_INVALID;
					}
					return ERROR_SYSTEM_CONFIG_BLOCK_END;
				case '#':
					sharp_comment = 1;
					continue;
				case '"':
					pstart++;
					d_quoted = true;
					last_space = 0;
					continue;
				case '\'':
					pstart++;
					s_quoted = true;
					last_space = 0;
					continue;
				default:
					last_space = 0;
					continue;
			}
		} else {
		// last charecter is not space
			bool found = false;
			if (d_quoted) {
				if (ch == '"') {
					d_quoted = false;
					need_space = true;
					found = true;
				}
			} else if (s_quoted) {
				if (ch == '\'') {
					s_quoted = false;
					need_space = true;
					found = true;
				}
			} else if (is_common_space(ch) || ch == ';' || ch == '{') {
				last_space = true;
				found = 1;
			}
			
			if (found) {
				int len = buffer->pos - pstart;
				char* word = new char[len];
				memcpy(word, pstart, len);
				word[len - 1] = 0;
				
				args.push_back(word);
				srs_freepa(word);
				
				if (ch == ';') {
					return ERROR_SYSTEM_CONFIG_DIRECTIVE;
				}
				if (ch == '{') {
					return ERROR_SYSTEM_CONFIG_BLOCK_START;
				}
			}
		}
	}
	
	return ret;
}

int SrsConfDirective::refill_buffer(SrsFileBuffer* buffer, bool d_quoted, bool s_quoted, int startline, char*& pstart)
{
	int ret = ERROR_SUCCESS;
	
	if (buffer->pos < buffer->last) {
		return ret;
	}
	
	int size = FILE_SIZE(buffer->fd) - FILE_OFFSET(buffer->fd);
	if (size > CONF_BUFFER_SIZE) {
		ret = ERROR_SYSTEM_CONFIG_TOO_LARGE;
		srs_error("config file too large, max=%d, actual=%d, ret=%d",
			CONF_BUFFER_SIZE, size, ret);
		return ret;
	}
	
	if (size <= 0) {
		return ERROR_SYSTEM_CONFIG_EOF;
	}
	
	int len = buffer->pos - buffer->start;
	if (len >= CONF_BUFFER_SIZE) {
		buffer->line = startline;
		
		if (!d_quoted && !s_quoted) {
			srs_error("line %d: too long parameter \"%*s...\" started", 
				buffer->line, 10, buffer->start);
			
		} else {
			srs_error("line %d: too long parameter, "
				"probably missing terminating '%c' character", buffer->line, d_quoted? '"':'\'');
		}
		return ERROR_SYSTEM_CONFIG_INVALID;
	}
	
	if (len) {
		memmove(buffer->start, pstart, len);
	}
	
	size = srs_min(size, buffer->end - (buffer->start + len));
	int n = read(buffer->fd, buffer->start + len, size);
	if (n != size) {
		srs_error("read file read error. expect %d, actual %d bytes.", size, n);
		return ERROR_SYSTEM_CONFIG_INVALID;
	}
	
	buffer->pos = buffer->start + len;
	buffer->last = buffer->pos + n;
	pstart = buffer->start;
	
	return ret;
}

SrsConfig* config = new SrsConfig();

SrsConfig::SrsConfig()
{
	show_help = false;
	show_version = false;
	
	root = new SrsConfDirective();
	root->conf_line = 0;
	root->name = "root";
}

SrsConfig::~SrsConfig()
{
	srs_freep(root);
}

int SrsConfig::reload()
{
	int ret = ERROR_SUCCESS;

	SrsConfig conf;
	if ((ret = conf.parse_file(config_file.c_str())) != ERROR_SUCCESS) {
		srs_error("config reloader parse file failed. ret=%d", ret);
		return ret;
	}
	srs_info("config reloader parse file success.");
	
	// store current root to old_root,
	// and reap the root from conf to current root.
	SrsConfDirective* old_root = root;
	SrsAutoFree(SrsConfDirective, old_root, false);
	
	root = conf.root;
	conf.root = NULL;
	
	// merge config.
	std::vector<SrsReloadHandler*>::iterator it;

	// merge config: listen
	if (!srs_directive_equals(root->get("listen"), old_root->get("listen"))) {
		for (it = subscribes.begin(); it != subscribes.end(); ++it) {
			SrsReloadHandler* subscribe = *it;
			if ((ret = subscribe->on_reload_listen()) != ERROR_SUCCESS) {
				srs_error("notify subscribes reload listen failed. ret=%d", ret);
				return ret;
			}
		}
		srs_trace("reload listen success.");
	}
	// merge config: pithy_print
	if (!srs_directive_equals(root->get("pithy_print"), old_root->get("pithy_print"))) {
		for (it = subscribes.begin(); it != subscribes.end(); ++it) {
			SrsReloadHandler* subscribe = *it;
			if ((ret = subscribe->on_reload_pithy_print()) != ERROR_SUCCESS) {
				srs_error("notify subscribes pithy_print listen failed. ret=%d", ret);
				return ret;
			}
		}
		srs_trace("reload pithy_print success.");
	}
	
	return ret;
}

void SrsConfig::subscribe(SrsReloadHandler* handler)
{
	std::vector<SrsReloadHandler*>::iterator it;
	
	it = std::find(subscribes.begin(), subscribes.end(), handler);
	if (it != subscribes.end()) {
		return;
	}
	
	subscribes.push_back(handler);
}

void SrsConfig::unsubscribe(SrsReloadHandler* handler)
{
	std::vector<SrsReloadHandler*>::iterator it;
	
	it = std::find(subscribes.begin(), subscribes.end(), handler);
	if (it == subscribes.end()) {
		return;
	}
	
	subscribes.erase(it);
}

// see: ngx_get_options
int SrsConfig::parse_options(int argc, char** argv)
{
	int ret = ERROR_SUCCESS;
	
	for (int i = 1; i < argc; i++) {
		if ((ret = parse_argv(i, argv)) != ERROR_SUCCESS) {
			return ret;
		}
	}
	
	if (show_help) {
		print_help(argv);
	}
	
	if (show_version) {
		printf("%s\n", RTMP_SIG_SRS_VERSION);
	}
	
	if (show_help || show_version) {
		exit(0);
	}
	
	if (config_file.empty()) {
		ret = ERROR_SYSTEM_CONFIG_INVALID;
		srs_error("config file not specified, see help: %s -h, ret=%d", argv[0], ret);
		return ret;
	}

	return parse_file(config_file.c_str());
}

SrsConfDirective* SrsConfig::get_vhost(std::string vhost)
{
	srs_assert(root);
	
	for (int i = 0; i < (int)root->directives.size(); i++) {
		SrsConfDirective* conf = root->at(i);
		
		if (conf->name != "vhost") {
			continue;
		}
		
		if (conf->arg0() == vhost) {
			return conf;
		}
	}
	
	if (vhost != RTMP_VHOST_DEFAULT) {
		return get_vhost(RTMP_VHOST_DEFAULT);
	}
	
	return NULL;
}

bool SrsConfig::get_vhost_enabled(std::string vhost)
{
	SrsConfDirective* vhost_conf = get_vhost(vhost);

	if (!vhost_conf) {
		return true;
	}
	
	SrsConfDirective* conf = vhost_conf->get("enabled");
	if (!conf) {
		return true;
	}
	
	if (conf->arg0() == "off") {
		return false;
	}
	
	return true;
}

SrsConfDirective* SrsConfig::get_transcode(std::string vhost, std::string scope)
{
	SrsConfDirective* conf = get_vhost(vhost);

	if (!conf) {
		return NULL;
	}
	
	SrsConfDirective* transcode = conf->get("transcode");
	if (!transcode) {
		return NULL;
	}
	
	if (transcode->arg0() == scope) {
		return transcode;
	}
	
	return NULL;
}

bool SrsConfig::get_transcode_enabled(SrsConfDirective* transcode)
{
	if (!transcode) {
		return false;
	}
	
	SrsConfDirective* conf = transcode->get("enabled");
	if (!conf || conf->arg0() != "on") {
		return false;
	}
	
	return true;
}

std::string SrsConfig::get_transcode_ffmpeg(SrsConfDirective* transcode)
{
	if (!transcode) {
		return "";
	}
	
	SrsConfDirective* conf = transcode->get("ffmpeg");
	if (!conf) {
		return "";
	}
	
	return conf->arg0();
}

void SrsConfig::get_transcode_engines(SrsConfDirective* transcode, std::vector<SrsConfDirective*>& engines)
{
	if (!transcode) {
		return;
	}
	
	for (int i = 0; i < (int)transcode->directives.size(); i++) {
		SrsConfDirective* conf = transcode->directives[i];
		
		if (conf->name == "engine") {
			engines.push_back(conf);
		}
	}
	
	return;
}

bool SrsConfig::get_engine_enabled(SrsConfDirective* engine)
{
	if (!engine) {
		return false;
	}
	
	SrsConfDirective* conf = engine->get("enabled");
	if (!conf || conf->arg0() != "on") {
		return false;
	}
	
	return true;
}

std::string SrsConfig::get_engine_vcodec(SrsConfDirective* engine)
{
	if (!engine) {
		return "";
	}
	
	SrsConfDirective* conf = engine->get("vcodec");
	if (!conf) {
		return "";
	}
	
	return conf->arg0();
}

int SrsConfig::get_engine_vbitrate(SrsConfDirective* engine)
{
	if (!engine) {
		return 0;
	}
	
	SrsConfDirective* conf = engine->get("vbitrate");
	if (!conf) {
		return 0;
	}
	
	return ::atoi(conf->arg0().c_str());
}

double SrsConfig::get_engine_vfps(SrsConfDirective* engine)
{
	if (!engine) {
		return 0;
	}
	
	SrsConfDirective* conf = engine->get("vfps");
	if (!conf) {
		return 0;
	}
	
	return ::atof(conf->arg0().c_str());
}

int SrsConfig::get_engine_vwidth(SrsConfDirective* engine)
{
	if (!engine) {
		return 0;
	}
	
	SrsConfDirective* conf = engine->get("vwidth");
	if (!conf) {
		return 0;
	}
	
	return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_engine_vheight(SrsConfDirective* engine)
{
	if (!engine) {
		return 0;
	}
	
	SrsConfDirective* conf = engine->get("vheight");
	if (!conf) {
		return 0;
	}
	
	return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_engine_vthreads(SrsConfDirective* engine)
{
	if (!engine) {
		return 0;
	}
	
	SrsConfDirective* conf = engine->get("vthreads");
	if (!conf) {
		return 0;
	}
	
	return ::atoi(conf->arg0().c_str());
}

std::string SrsConfig::get_engine_vprofile(SrsConfDirective* engine)
{
	if (!engine) {
		return "";
	}
	
	SrsConfDirective* conf = engine->get("vprofile");
	if (!conf) {
		return "";
	}
	
	return conf->arg0();
}

std::string SrsConfig::get_engine_vpreset(SrsConfDirective* engine)
{
	if (!engine) {
		return "";
	}
	
	SrsConfDirective* conf = engine->get("vpreset");
	if (!conf) {
		return "";
	}
	
	return conf->arg0();
}

void SrsConfig::get_engine_vparams(SrsConfDirective* engine, std::vector<std::string>& vparams)
{
	if (!engine) {
		return;
	}
	
	SrsConfDirective* conf = engine->get("vparams");
	if (!conf) {
		return;
	}
	
	for (int i = 0; i < (int)conf->directives.size(); i++) {
		SrsConfDirective* p = conf->directives[i];
		if (!p) {
			continue;
		}
		
		vparams.push_back("-" + p->name);
		vparams.push_back(p->arg0());
	}
}

void SrsConfig::get_engine_vfilter(SrsConfDirective* engine, std::vector<std::string>& vfilter)
{
	if (!engine) {
		return;
	}
	
	SrsConfDirective* conf = engine->get("vfilter");
	if (!conf) {
		return;
	}
	
	for (int i = 0; i < (int)conf->directives.size(); i++) {
		SrsConfDirective* p = conf->directives[i];
		if (!p) {
			continue;
		}
		
		vfilter.push_back("-" + p->name);
		vfilter.push_back(p->arg0());
	}
}

std::string SrsConfig::get_engine_acodec(SrsConfDirective* engine)
{
	if (!engine) {
		return "";
	}
	
	SrsConfDirective* conf = engine->get("acodec");
	if (!conf) {
		return "";
	}
	
	return conf->arg0();
}

int SrsConfig::get_engine_abitrate(SrsConfDirective* engine)
{
	if (!engine) {
		return 0;
	}
	
	SrsConfDirective* conf = engine->get("abitrate");
	if (!conf) {
		return 0;
	}
	
	return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_engine_asample_rate(SrsConfDirective* engine)
{
	if (!engine) {
		return 0;
	}
	
	SrsConfDirective* conf = engine->get("asample_rate");
	if (!conf) {
		return 0;
	}
	
	return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_engine_achannels(SrsConfDirective* engine)
{
	if (!engine) {
		return 0;
	}
	
	SrsConfDirective* conf = engine->get("achannels");
	if (!conf) {
		return 0;
	}
	
	return ::atoi(conf->arg0().c_str());
}

void SrsConfig::get_engine_aparams(SrsConfDirective* engine, std::vector<std::string>& aparams)
{
	if (!engine) {
		return;
	}
	
	SrsConfDirective* conf = engine->get("aparams");
	if (!conf) {
		return;
	}
	
	for (int i = 0; i < (int)conf->directives.size(); i++) {
		SrsConfDirective* p = conf->directives[i];
		if (!p) {
			continue;
		}
		
		aparams.push_back("-" + p->name);
		aparams.push_back(p->arg0());
	}
}

std::string SrsConfig::get_engine_output(SrsConfDirective* engine)
{
	if (!engine) {
		return "";
	}
	
	SrsConfDirective* conf = engine->get("output");
	if (!conf) {
		return "";
	}
	
	return conf->arg0();
}

std::string SrsConfig::get_log_dir()
{
	srs_assert(root);
	
	SrsConfDirective* conf = root->get("log_dir");
	if (!conf || conf->arg0().empty()) {
		return "./objs/logs";
	}
	
	return conf->arg0();
}

int SrsConfig::get_max_connections()
{
	srs_assert(root);
	
	SrsConfDirective* conf = root->get("max_connections");
	if (!conf || conf->arg0().empty()) {
		return 2000;
	}
	
	return ::atoi(conf->arg0().c_str());
}

SrsConfDirective* SrsConfig::get_gop_cache(std::string vhost)
{
	SrsConfDirective* conf = get_vhost(vhost);

	if (!conf) {
		return NULL;
	}
	
	return conf->get("gop_cache");
}

SrsConfDirective* SrsConfig::get_forward(std::string vhost)
{
	SrsConfDirective* conf = get_vhost(vhost);

	if (!conf) {
		return NULL;
	}
	
	return conf->get("forward");
}

SrsConfDirective* SrsConfig::get_hls(std::string vhost)
{
	SrsConfDirective* conf = get_vhost(vhost);

	if (!conf) {
		return NULL;
	}
	
	return conf->get("hls");
}

bool SrsConfig::get_hls_enabled(std::string vhost)
{
	SrsConfDirective* hls = get_hls(vhost);
	
	if (!hls) {
		return true;
	}
	
	if (hls->arg0() == "off") {
		return false;
	}
	
	return true;
}

SrsConfDirective* SrsConfig::get_hls_path(std::string vhost)
{
	SrsConfDirective* conf = get_vhost(vhost);

	if (!conf) {
		return NULL;
	}
	
	return conf->get("hls_path");
}

SrsConfDirective* SrsConfig::get_hls_fragment(std::string vhost)
{
	SrsConfDirective* conf = get_vhost(vhost);

	if (!conf) {
		return NULL;
	}
	
	return conf->get("hls_fragment");
}

SrsConfDirective* SrsConfig::get_hls_window(std::string vhost)
{
	SrsConfDirective* conf = get_vhost(vhost);

	if (!conf) {
		return NULL;
	}
	
	return conf->get("hls_window");
}

SrsConfDirective* SrsConfig::get_refer(std::string vhost)
{
	SrsConfDirective* conf = get_vhost(vhost);

	if (!conf) {
		return NULL;
	}
	
	return conf->get("refer");
}

SrsConfDirective* SrsConfig::get_refer_play(std::string vhost)
{
	SrsConfDirective* conf = get_vhost(vhost);

	if (!conf) {
		return NULL;
	}
	
	return conf->get("refer_play");
}

SrsConfDirective* SrsConfig::get_refer_publish(std::string vhost)
{
	SrsConfDirective* conf = get_vhost(vhost);

	if (!conf) {
		return NULL;
	}
	
	return conf->get("refer_publish");
}

SrsConfDirective* SrsConfig::get_listen()
{
	return root->get("listen");
}

SrsConfDirective* SrsConfig::get_chunk_size()
{
	return root->get("chunk_size");
}

SrsConfDirective* SrsConfig::get_pithy_print_publish()
{
	SrsConfDirective* pithy = root->get("pithy_print");
	if (!pithy) {
		return NULL;
	}
	
	return pithy->get("publish");
}

SrsConfDirective* SrsConfig::get_pithy_print_forwarder()
{
	SrsConfDirective* pithy = root->get("pithy_print");
	if (!pithy) {
		return NULL;
	}
	
	return pithy->get("forwarder");
}

SrsConfDirective* SrsConfig::get_pithy_print_hls()
{
	SrsConfDirective* pithy = root->get("pithy_print");
	if (!pithy) {
		return NULL;
	}
	
	return pithy->get("hls");
}

SrsConfDirective* SrsConfig::get_pithy_print_encoder()
{
	SrsConfDirective* pithy = root->get("encoder");
	if (!pithy) {
		return NULL;
	}
	
	return pithy->get("forwarder");
}

SrsConfDirective* SrsConfig::get_pithy_print_play()
{
	SrsConfDirective* pithy = root->get("pithy_print");
	if (!pithy) {
		return NULL;
	}
	
	return pithy->get("play");
}

int SrsConfig::parse_file(const char* filename)
{
	int ret = ERROR_SUCCESS;
	
	config_file = filename;
	
	if (config_file.empty()) {
		return ERROR_SYSTEM_CONFIG_INVALID;
	}
	
	if ((ret = root->parse(config_file.c_str())) != ERROR_SUCCESS) {
		return ret;
	}
	
	SrsConfDirective* conf = NULL;
	if ((conf = get_listen()) == NULL || conf->args.size() == 0) {
		ret = ERROR_SYSTEM_CONFIG_INVALID;
		srs_error("line %d: conf error, "
			"directive \"listen\" is empty, ret=%d", (conf? conf->conf_line:0), ret);
		return ret;
	}
	// TODO: check the hls.
	// TODO: check other config.
	// TODO: check hls.
	// TODO: check ssl.
	// TODO: check ffmpeg.
	
	return ret;
}

int SrsConfig::parse_argv(int& i, char** argv)
{
	int ret = ERROR_SUCCESS;

	char* p = argv[i];
		
	if (*p++ != '-') {
		ret = ERROR_SYSTEM_CONFIG_INVALID;
		srs_error("invalid options(index=%d, value=%s), "
			"must starts with -, see help: %s -h, ret=%d", i, argv[i], argv[0], ret);
		return ret;
	}
	
	while (*p) {
		switch (*p++) {
			case '?':
			case 'h':
				show_help = true;
				break;
			case 'v':
			case 'V':
				show_version = true;
				break;
			case 'c':
				if (*p) {
					config_file = p;
					return ret;
				}
				if (argv[++i]) {
					config_file = argv[i];
					return ret;
				}
				ret = ERROR_SYSTEM_CONFIG_INVALID;
				srs_error("option \"-c\" requires parameter, ret=%d", ret);
				return ret;
			default:
				ret = ERROR_SYSTEM_CONFIG_INVALID;
				srs_error("invalid option: \"%c\", see help: %s -h, ret=%d", *(p - 1), argv[0], ret);
				return ret;
		}
	}
	
	return ret;
}

void SrsConfig::print_help(char** argv)
{
	printf(RTMP_SIG_SRS_NAME" "RTMP_SIG_SRS_VERSION
		" Copyright (c) 2013 winlin\n"
		"Contributors: "RTMP_SIG_SRS_CONTRIBUTOR"\n"
		"Configuration: "SRS_CONFIGURE"\n"
		"Usage: %s [-h?vV] [-c <filename>]\n" 
		"\n"
		"Options:\n"
		"   -?-h            : show help\n"
		"   -v-V            : show version and exit\n"
		"   -c filename     : set configuration file\n"
		"\n"
		RTMP_SIG_SRS_WEB"\n"
		RTMP_SIG_SRS_URL"\n"
		"Email: "RTMP_SIG_SRS_EMAIL"\n"
		"\n",
		argv[0]);
}

bool srs_directive_equals(SrsConfDirective* a, SrsConfDirective* b)
{
	if (!a || !b) {
		return false;
	}
	
	if (a->name != b->name) {
		return false;
	}
	
	if (a->args.size() != b->args.size()) {
		return false;
	}
	
	for (int i = 0; i < (int)a->args.size(); i++) {
		if (a->args.at(i) != b->args.at(i)) {
			return false;
		}
	}
	
	if (a->directives.size() != b->directives.size()) {
		return false;
	}
	
	for (int i = 0; i < (int)a->directives.size(); i++) {
		SrsConfDirective* a0 = a->at(i);
		SrsConfDirective* b0 = b->at(i);
		
		if (!srs_directive_equals(a0, b0)) {
			return false;
		}
	}
	
	return true;
}

