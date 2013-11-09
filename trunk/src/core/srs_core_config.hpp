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

// conf node: enabled.
#define RTMP_VHOST_ENABLED "enabled"

class SrsFileBuffer
{
public:
	int fd;
	int line;
	// start of buffer.
	char* start;
	// end of buffer.
	char* end;
	// current consumed position.
	char* pos;
	// last available position.
	char* last;
	
	SrsFileBuffer();
	virtual ~SrsFileBuffer();
	virtual int open(const char* filename);
};

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
public:
	virtual int parse(const char* filename);
public:
	enum SrsDirectiveType{parse_file, parse_block};
	virtual int parse_conf(SrsFileBuffer* buffer, SrsDirectiveType type);
	virtual int read_token(SrsFileBuffer* buffer, std::vector<std::string>& args);
	virtual int refill_buffer(SrsFileBuffer* buffer, bool d_quoted, bool s_quoted, int startline, char*& pstart);
};

/**
* the config parser.
*/
class SrsConfig
{
private:
	bool show_help;
	bool show_version;
	std::string config_file;
	SrsConfDirective* root;
	std::vector<SrsReloadHandler*> subscribes;
public:
	SrsConfig();
	virtual ~SrsConfig();
public:
	virtual int reload();
	virtual void subscribe(SrsReloadHandler* handler);
	virtual void unsubscribe(SrsReloadHandler* handler);
public:
	virtual int parse_options(int argc, char** argv);
	virtual SrsConfDirective* get_vhost(std::string vhost);
	virtual SrsConfDirective* get_gop_cache(std::string vhost);
	virtual SrsConfDirective* get_refer(std::string vhost);
	virtual SrsConfDirective* get_refer_play(std::string vhost);
	virtual SrsConfDirective* get_refer_publish(std::string vhost);
	virtual SrsConfDirective* get_listen();
	virtual SrsConfDirective* get_chunk_size();
private:
	virtual int parse_file(const char* filename);
	virtual int parse_argv(int& i, char** argv);
	virtual void print_help(char** argv);
};

/**
* deep compare directive.
*/
bool srs_directive_equals(SrsConfDirective* a, SrsConfDirective* b);

// global config
extern SrsConfig* config;

#endif