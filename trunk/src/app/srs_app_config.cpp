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

#include <srs_app_config.hpp>

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
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_protocol_utility.hpp>
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

class SrsFileBuffer
{
private:
    // last available position.
    char* last;
    // end of buffer.
    char* end;
    // start of buffer.
    char* start;
public:
    // current consumed position.
    char* pos;
    // current parsed line.
    int line;
    
    SrsFileBuffer();
    virtual ~SrsFileBuffer();
    virtual int fullfill(const char* filename);
    virtual bool empty();
};

SrsFileBuffer::SrsFileBuffer()
{
    line = 0;

    pos = last = start = NULL;
    end = start;
}

SrsFileBuffer::~SrsFileBuffer()
{
    srs_freepa(start);
}

int SrsFileBuffer::fullfill(const char* filename)
{
    int ret = ERROR_SUCCESS;
    
    int fd = -1;
    int nread = 0;
    int filesize = 0;
    
    if ((fd = ::open(filename, O_RDONLY, 0)) < 0) {
        ret = ERROR_SYSTEM_CONFIG_INVALID;
        srs_error("open conf file error. ret=%d", ret);
        goto finish;
    }
    
    if ((filesize = FILE_SIZE(fd) - FILE_OFFSET(fd)) <= 0) {
        ret = ERROR_SYSTEM_CONFIG_EOF;
        srs_error("read conf file error. ret=%d", ret);
        goto finish;
    }

    srs_freepa(start);
    pos = last = start = new char[filesize];
    end = start + filesize;
    
    if ((nread = read(fd, start, filesize)) != filesize) {
        ret = ERROR_SYSTEM_CONFIG_INVALID;
        srs_error("read file read error. expect %d, actual %d bytes, ret=%d", 
            filesize, nread, ret);
        goto finish;
    }
    
    line = 1;
    
finish:
    if (fd > 0) {
        ::close(fd);
    }
    
    return ret;
}

bool SrsFileBuffer::empty()
{
    return pos >= end;
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

string SrsConfDirective::arg0()
{
    if (args.size() > 0) {
        return args.at(0);
    }
    
    return "";
}

string SrsConfDirective::arg1()
{
    if (args.size() > 1) {
        return args.at(1);
    }
    
    return "";
}

string SrsConfDirective::arg2()
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

SrsConfDirective* SrsConfDirective::get(string _name)
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

SrsConfDirective* SrsConfDirective::get(string _name, string _arg0)
{
    std::vector<SrsConfDirective*>::iterator it;
    for (it = directives.begin(); it != directives.end(); ++it) {
        SrsConfDirective* directive = *it;
        if (directive->name == _name && directive->arg0() == _arg0) {
            return directive;
        }
    }
    
    return NULL;
}

int SrsConfDirective::parse(const char* filename)
{
    int ret = ERROR_SUCCESS;
    
    SrsFileBuffer buffer;
    
    if ((ret = buffer.fullfill(filename)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return parse_conf(&buffer, parse_file);
}

// see: ngx_conf_parse
int SrsConfDirective::parse_conf(SrsFileBuffer* buffer, SrsDirectiveType type)
{
    int ret = ERROR_SUCCESS;
    
    while (true) {
        std::vector<string> args;
        ret = read_token(buffer, args);
        
        /**
        * ret maybe:
        * ERROR_SYSTEM_CONFIG_INVALID         error.
        * ERROR_SYSTEM_CONFIG_DIRECTIVE        directive terminated by ';' found
        * ERROR_SYSTEM_CONFIG_BLOCK_START    token terminated by '{' found
        * ERROR_SYSTEM_CONFIG_BLOCK_END        the '}' found
        * ERROR_SYSTEM_CONFIG_EOF            the config file is done
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
int SrsConfDirective::read_token(SrsFileBuffer* buffer, std::vector<string>& args)
{
    int ret = ERROR_SUCCESS;

    char* pstart = buffer->pos;

    bool sharp_comment = false;
    
    bool d_quoted = false;
    bool s_quoted = false;
    
    bool need_space = false;
    bool last_space = true;
    
    while (true) {
        if (buffer->empty()) {
            ret = ERROR_SYSTEM_CONFIG_EOF;
            
            if (!args.empty() || !last_space) {
                srs_error("line %d: unexpected end of file, expecting ; or \"}\"", buffer->line);
                return ERROR_SYSTEM_CONFIG_INVALID;
            }
            srs_trace("config parsed EOF");
            
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
                
                string word_str = word;
                if (!word_str.empty()) {
                    args.push_back(word_str);
                }
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
    std::vector<ISrsReloadHandler*>::iterator it;

    // merge config: listen
    if (!srs_directive_equals(root->get("listen"), old_root->get("listen"))) {
        for (it = subscribes.begin(); it != subscribes.end(); ++it) {
            ISrsReloadHandler* subscribe = *it;
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
            ISrsReloadHandler* subscribe = *it;
            if ((ret = subscribe->on_reload_pithy_print()) != ERROR_SUCCESS) {
                srs_error("notify subscribes pithy_print listen failed. ret=%d", ret);
                return ret;
            }
        }
        srs_trace("reload pithy_print success.");
    }

    // merge config: vhost added, directly supported.
        
    // merge config: vhost removed/disabled/modified.
    for (int i = 0; i < (int)old_root->directives.size(); i++) {
        SrsConfDirective* old_vhost = old_root->at(i);
        // only process vhost directives.
        if (old_vhost->name != "vhost") {
            continue;
        }
        
        std::string vhost = old_vhost->arg0();

        SrsConfDirective* new_vhost = root->get("vhost", vhost);
        // ignore if absolutely equal
        if (new_vhost && srs_directive_equals(old_vhost, new_vhost)) {
            srs_trace("vhost %s absolutely equal, ignore.", vhost.c_str());
            continue;
        }
        // ignore if enable the new vhost when old vhost is disabled.
        if (get_vhost_enabled(new_vhost) && !get_vhost_enabled(old_vhost)) {
            srs_trace("vhost %s disabled=>enabled, ignore.", vhost.c_str());
            continue;
        }
        // ignore if both old and new vhost are disabled.
        if (!get_vhost_enabled(new_vhost) && !get_vhost_enabled(old_vhost)) {
            srs_trace("vhost %s disabled=>disabled, ignore.", vhost.c_str());
            continue;
        }

        // merge config: vhost removed/disabled.
        if (!get_vhost_enabled(new_vhost) && get_vhost_enabled(old_vhost)) {
            srs_trace("vhost %s disabled, reload it.", vhost.c_str());
            for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                ISrsReloadHandler* subscribe = *it;
                if ((ret = subscribe->on_reload_vhost_removed(vhost)) != ERROR_SUCCESS) {
                    srs_error("notify subscribes pithy_print remove "
                        "vhost %s failed. ret=%d", vhost.c_str(), ret);
                    return ret;
                }
            }
            srs_trace("reload remove vhost %s success.", vhost.c_str());
        }
        
        // merge config: vhost modified.
        srs_trace("vhost %s modified, reload its detail.", vhost.c_str());
        if (get_vhost_enabled(new_vhost) && get_vhost_enabled(old_vhost)) {
            // gop_cache
            if (!srs_directive_equals(new_vhost->get("gop_cache"), old_vhost->get("gop_cache"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((ret = subscribe->on_reload_gop_cache(vhost)) != ERROR_SUCCESS) {
                        srs_error("vhost %s notify subscribes gop_cache failed. ret=%d", vhost.c_str(), ret);
                        return ret;
                    }
                }
                srs_trace("vhost %s reload gop_cache success.", vhost.c_str());
            }
            // queue_length
            if (!srs_directive_equals(new_vhost->get("queue_length"), old_vhost->get("queue_length"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((ret = subscribe->on_reload_queue_length(vhost)) != ERROR_SUCCESS) {
                        srs_error("vhost %s notify subscribes queue_length failed. ret=%d", vhost.c_str(), ret);
                        return ret;
                    }
                }
                srs_trace("vhost %s reload queue_length success.", vhost.c_str());
            }
            // forward
            if (!srs_directive_equals(new_vhost->get("forward"), old_vhost->get("forward"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((ret = subscribe->on_reload_forward(vhost)) != ERROR_SUCCESS) {
                        srs_error("vhost %s notify subscribes forward failed. ret=%d", vhost.c_str(), ret);
                        return ret;
                    }
                }
                srs_trace("vhost %s reload forward success.", vhost.c_str());
            }
            // hls
            if (!srs_directive_equals(new_vhost->get("hls"), old_vhost->get("hls"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((ret = subscribe->on_reload_hls(vhost)) != ERROR_SUCCESS) {
                        srs_error("vhost %s notify subscribes hls failed. ret=%d", vhost.c_str(), ret);
                        return ret;
                    }
                }
                srs_trace("vhost %s reload hls success.", vhost.c_str());
            }
            // transcode
            if (!srs_directive_equals(new_vhost->get("transcode"), old_vhost->get("transcode"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((ret = subscribe->on_reload_transcode(vhost)) != ERROR_SUCCESS) {
                        srs_error("vhost %s notify subscribes transcode failed. ret=%d", vhost.c_str(), ret);
                        return ret;
                    }
                }
                srs_trace("vhost %s reload transcode success.", vhost.c_str());
            }
            // TODO: suppor reload hls/forward/ffmpeg/http
            continue;
        }
        srs_warn("invalid reload path, enabled old: %d, new: %d", 
            get_vhost_enabled(old_vhost), get_vhost_enabled(new_vhost));
    }
    
    return ret;
}

void SrsConfig::subscribe(ISrsReloadHandler* handler)
{
    std::vector<ISrsReloadHandler*>::iterator it;
    
    it = std::find(subscribes.begin(), subscribes.end(), handler);
    if (it != subscribes.end()) {
        return;
    }
    
    subscribes.push_back(handler);
}

void SrsConfig::unsubscribe(ISrsReloadHandler* handler)
{
    std::vector<ISrsReloadHandler*>::iterator it;
    
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
        fprintf(stderr, "%s\n", RTMP_SIG_SRS_VERSION);
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
    // check rtmp port specified by directive listen.
    if ((conf = get_listen()) == NULL || conf->args.size() == 0) {
        ret = ERROR_SYSTEM_CONFIG_INVALID;
        srs_error("line %d: conf error, "
            "directive \"listen\" is empty, ret=%d", (conf? conf->conf_line:0), ret);
        return ret;
    }
    
    // TODO: check the hls.
    // TODO: check forward.
    // TODO: check ffmpeg.
    // TODO: check http.
    // TODO: check pid.
    
    // check log
    std::string log_filename = this->get_srs_log_file();
    if (get_srs_log_tank_file() && log_filename.empty()) {
        ret = ERROR_SYSTEM_CONFIG_INVALID;
        srs_error("must specifies the file to write log to. ret=%d", ret);
        return ret;
    }
    if (get_srs_log_tank_file()) {
        srs_trace("log file is %s", log_filename.c_str());
    } else {
        srs_trace("write log to console");
    }
    
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
    printf(
        RTMP_SIG_SRS_NAME" "RTMP_SIG_SRS_VERSION" "RTMP_SIG_SRS_COPYRIGHT"\n" 
        "Primary Authors: "RTMP_SIG_SRS_PRIMARY_AUTHROS"\n"
        "Build: "SRS_BUILD_DATE" Configuration:"SRS_CONFIGURE"\n"
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

SrsConfDirective* SrsConfig::get_vhost(string vhost)
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

SrsConfDirective* SrsConfig::get_vhost_on_connect(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) { 
        return NULL;
    }
    
    conf = conf->get("http_hooks");
    if (!conf) {
        return NULL;
    }
    
    SrsConfDirective* enabled = conf->get("enabled");
    if (!enabled || enabled->arg0() != "on") {
        return NULL;
    }
    
    return conf->get("on_connect");
}

SrsConfDirective* SrsConfig::get_vhost_on_close(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) { 
        return NULL;
    }
    
    conf = conf->get("http_hooks");
    if (!conf) {
        return NULL;
    }
    
    SrsConfDirective* enabled = conf->get("enabled");
    if (!enabled || enabled->arg0() != "on") {
        return NULL;
    }
    
    return conf->get("on_close");
}

SrsConfDirective* SrsConfig::get_vhost_on_publish(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) { 
        return NULL;
    }
    
    conf = conf->get("http_hooks");
    if (!conf) {
        return NULL;
    }
    
    SrsConfDirective* enabled = conf->get("enabled");
    if (!enabled || enabled->arg0() != "on") {
        return NULL;
    }
    
    return conf->get("on_publish");
}

SrsConfDirective* SrsConfig::get_vhost_on_unpublish(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) { 
        return NULL;
    }
    
    conf = conf->get("http_hooks");
    if (!conf) {
        return NULL;
    }
    
    SrsConfDirective* enabled = conf->get("enabled");
    if (!enabled || enabled->arg0() != "on") {
        return NULL;
    }
    
    return conf->get("on_unpublish");
}

SrsConfDirective* SrsConfig::get_vhost_on_play(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) { 
        return NULL;
    }
    
    conf = conf->get("http_hooks");
    if (!conf) {
        return NULL;
    }
    
    SrsConfDirective* enabled = conf->get("enabled");
    if (!enabled || enabled->arg0() != "on") {
        return NULL;
    }
    
    return conf->get("on_play");
}

SrsConfDirective* SrsConfig::get_vhost_on_stop(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) { 
        return NULL;
    }
    
    conf = conf->get("http_hooks");
    if (!conf) {
        return NULL;
    }
    
    SrsConfDirective* enabled = conf->get("enabled");
    if (!enabled || enabled->arg0() != "on") {
        return NULL;
    }
    
    return conf->get("on_stop");
}

bool SrsConfig::get_vhost_enabled(string vhost)
{
    SrsConfDirective* vhost_conf = get_vhost(vhost);
    
    return get_vhost_enabled(vhost_conf);
}

bool SrsConfig::get_vhost_enabled(SrsConfDirective* vhost)
{
    if (!vhost) {
        return false;
    }
    
    SrsConfDirective* conf = vhost->get("enabled");
    if (!conf) {
        return true;
    }
    
    if (conf->arg0() == "off") {
        return false;
    }
    
    return true;
}

SrsConfDirective* SrsConfig::get_transcode(string vhost, string scope)
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

string SrsConfig::get_transcode_ffmpeg(SrsConfDirective* transcode)
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

string SrsConfig::get_engine_vcodec(SrsConfDirective* engine)
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

string SrsConfig::get_engine_vprofile(SrsConfDirective* engine)
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

string SrsConfig::get_engine_vpreset(SrsConfDirective* engine)
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

void SrsConfig::get_engine_vparams(SrsConfDirective* engine, std::vector<string>& vparams)
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

void SrsConfig::get_engine_vfilter(SrsConfDirective* engine, std::vector<string>& vfilter)
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

string SrsConfig::get_engine_acodec(SrsConfDirective* engine)
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

void SrsConfig::get_engine_aparams(SrsConfDirective* engine, std::vector<string>& aparams)
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

string SrsConfig::get_engine_output(SrsConfDirective* engine)
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

string SrsConfig::get_ffmpeg_log_dir()
{
    srs_assert(root);
    
    SrsConfDirective* conf = root->get("ff_log_dir");
    if (!conf || conf->arg0().empty()) {
        return "./objs/logs";
    }
    
    return conf->arg0();
}

string SrsConfig::get_srs_log_file()
{
    srs_assert(root);
    
    SrsConfDirective* conf = root->get("srs_log_file");
    if (!conf || conf->arg0().empty()) {
        return "./objs/srs.log";
    }
    
    return conf->arg0();
}

string SrsConfig::get_srs_log_level()
{
    srs_assert(root);
    
    SrsConfDirective* conf = root->get("srs_log_level");
    if (!conf || conf->arg0().empty()) {
        return "trace";
    }
    
    return conf->arg0();
}

bool SrsConfig::get_srs_log_tank_file()
{
    srs_assert(root);
    
    SrsConfDirective* conf = root->get("srs_log_tank");
    if (conf && conf->arg0() == "console") {
        return false;
    }
    
    return true;
}

bool SrsConfig::get_deamon()
{
    srs_assert(root);
    
    SrsConfDirective* conf = root->get("daemon");
    if (conf && conf->arg0() == "off") {
        return false;
    }
    
    return true;
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

bool SrsConfig::get_gop_cache(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) {
        return true;
    }
    
    conf = conf->get("gop_cache");
    if (conf && conf->arg0() == "off") {
        return false;
    }
    
    return true;
}

bool SrsConfig::get_atc(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) {
        return true;
    }
    
    conf = conf->get("atc");
    if (conf && conf->arg0() == "on") {
        return true;
    }
    
    return false;
}

double SrsConfig::get_queue_length(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) {
        return SRS_CONF_DEFAULT_QUEUE_LENGTH;
    }
    
    conf = conf->get("queue_length");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_QUEUE_LENGTH;
    }
    
    return ::atoi(conf->arg0().c_str());
}

SrsConfDirective* SrsConfig::get_forward(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) {
        return NULL;
    }
    
    return conf->get("forward");
}

SrsConfDirective* SrsConfig::get_hls(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) {
        return NULL;
    }
    
    return conf->get("hls");
}

bool SrsConfig::get_hls_enabled(string vhost)
{
    SrsConfDirective* hls = get_hls(vhost);
    
    if (!hls) {
        return false;
    }
    
    SrsConfDirective* conf = hls->get("enabled");
    
    if (!conf) {
        return false;
    }
    
    if (conf->arg0() == "on") {
        return true;
    }
    
    return false;
}

string SrsConfig::get_hls_path(string vhost)
{
    SrsConfDirective* hls = get_hls(vhost);
    
    if (!hls) {
        return SRS_CONF_DEFAULT_HLS_PATH;
    }
    
    SrsConfDirective* conf = hls->get("hls_path");
    
    if (!conf) {
        return SRS_CONF_DEFAULT_HLS_PATH;
    }

    return conf->arg0();
}

double SrsConfig::get_hls_fragment(string vhost)
{
    SrsConfDirective* hls = get_hls(vhost);
    
    if (!hls) {
        return SRS_CONF_DEFAULT_HLS_FRAGMENT;
    }
    
    SrsConfDirective* conf = hls->get("hls_fragment");
    
    if (!conf) {
        return SRS_CONF_DEFAULT_HLS_FRAGMENT;
    }

    return ::atof(conf->arg0().c_str());
}

double SrsConfig::get_hls_window(string vhost)
{
    SrsConfDirective* hls = get_hls(vhost);
    
    if (!hls) {
        return SRS_CONF_DEFAULT_HLS_WINDOW;
    }
    
    SrsConfDirective* conf = hls->get("hls_window");
    
    if (!conf) {
        return SRS_CONF_DEFAULT_HLS_WINDOW;
    }

    return ::atof(conf->arg0().c_str());
}

SrsConfDirective* SrsConfig::get_http_api()
{
    return root->get("http_api");
}

bool SrsConfig::get_http_api_enabled()
{
    SrsConfDirective* conf = get_http_api();
    
    if (!conf) {
        return false;
    }
    
    conf = conf->get("enabled");
    if (conf && conf->arg0() == "on") {
        return true;
    }
    
    return false;
}

int SrsConfig::get_http_api_listen()
{
    SrsConfDirective* conf = get_http_api();
    
    if (conf) {
        conf = conf->get("listen");
        
        if (conf && !conf->arg0().empty()) {
            return ::atoi(conf->arg0().c_str());
        }
    }
    
    return 1985;
}

SrsConfDirective* SrsConfig::get_http_stream()
{
    return root->get("http_stream");
}

bool SrsConfig::get_http_stream_enabled()
{
    SrsConfDirective* conf = get_http_stream();
    
    if (!conf) {
        return false;
    }
    
    conf = conf->get("enabled");
    
    if (conf && conf->arg0() == "on") {
        return true;
    }
    
    return false;
}

int SrsConfig::get_http_stream_listen()
{
    SrsConfDirective* conf = get_http_stream();
    
    if (conf) {
        conf = conf->get("listen");
        
        if (conf && !conf->arg0().empty()) {
            return ::atoi(conf->arg0().c_str());
        }
    }
    
    return 8080;
}

SrsConfDirective* SrsConfig::get_refer(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) {
        return NULL;
    }
    
    return conf->get("refer");
}

SrsConfDirective* SrsConfig::get_refer_play(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) {
        return NULL;
    }
    
    return conf->get("refer_play");
}

SrsConfDirective* SrsConfig::get_refer_publish(string vhost)
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

string SrsConfig::get_pid_file()
{
    SrsConfDirective* conf = root->get("pid");
    
    if (!conf) {
        return SRS_CONF_DEFAULT_PID_FILE;
    }
    
    return conf->arg0();
}

int SrsConfig::get_chunk_size(const std::string &vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) {
        return SRS_CONF_DEFAULT_CHUNK_SIZE;
    }

    conf = conf->get("chunk_size");
    if (!conf) {
        // vhost does not specify the chunk size,
        // use the global instead.
        conf = root->get("chunk_size");
        if (!conf) {
            return SRS_CONF_DEFAULT_CHUNK_SIZE;
        }
        
        return ::atoi(conf->arg0().c_str());
    }

    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_pithy_print_publish()
{
    SrsConfDirective* pithy = root->get("pithy_print");
    if (!pithy) {
        return SRS_STAGE_PUBLISH_USER_INTERVAL_MS;
    }
    
    pithy = pithy->get("publish");
    if (!pithy) {
        return SRS_STAGE_PUBLISH_USER_INTERVAL_MS;
    }
    
    return ::atoi(pithy->arg0().c_str());
}

int SrsConfig::get_pithy_print_forwarder()
{
    SrsConfDirective* pithy = root->get("pithy_print");
    if (!pithy) {
        return SRS_STAGE_FORWARDER_INTERVAL_MS;
    }
    
    pithy = pithy->get("forwarder");
    if (!pithy) {
        return SRS_STAGE_FORWARDER_INTERVAL_MS;
    }
    
    return ::atoi(pithy->arg0().c_str());
}

int SrsConfig::get_pithy_print_hls()
{
    SrsConfDirective* pithy = root->get("pithy_print");
    if (!pithy) {
        return SRS_STAGE_HLS_INTERVAL_MS;
    }
    
    pithy = pithy->get("hls");
    if (!pithy) {
        return SRS_STAGE_HLS_INTERVAL_MS;
    }
    
    return ::atoi(pithy->arg0().c_str());
}

bool SrsConfig::get_bw_check_enabled(const string &vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) {
        return false;
    }

    conf = conf->get("bandcheck");
    if (!conf) {
        return false;
    }

    conf = conf->get("enabled");
    if (!conf || conf->arg0() != "on") {
        return false;
    }

    return true;
}

string SrsConfig::get_bw_check_key(const string &vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) {
        return "";
    }

    conf = conf->get("bandcheck");
    if (!conf) {
        return "";
    }
    
    conf = conf->get("key");
    if (!conf) {
        return "";
    }

    return conf->arg0();
}

int SrsConfig::get_bw_check_interval_ms(const string &vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) {
        return SRS_CONF_DEFAULT_BANDWIDTH_INTERVAL;
    }

    conf = conf->get("bandcheck");
    if (!conf) {
        return SRS_CONF_DEFAULT_BANDWIDTH_INTERVAL;
    }
    
    conf = conf->get("interval_ms");
    if (!conf) {
        return SRS_CONF_DEFAULT_BANDWIDTH_INTERVAL;
    }

    return ::atoi(conf->arg0().c_str()) * 1000;
}

int SrsConfig::get_bw_check_limit_kbps(const string &vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) {
        return SRS_CONF_DEFAULT_BANDWIDTH_LIMIT_KBPS;
    }

    conf = conf->get("bandcheck");
    if (!conf) {
        return SRS_CONF_DEFAULT_BANDWIDTH_LIMIT_KBPS;
    }
    
    conf = conf->get("limit_kbps");
    if (!conf) {
        return SRS_CONF_DEFAULT_BANDWIDTH_LIMIT_KBPS;
    }

    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_pithy_print_encoder()
{
    SrsConfDirective* pithy = root->get("encoder");
    if (!pithy) {
        return SRS_STAGE_ENCODER_INTERVAL_MS;
    }
    
    pithy = pithy->get("forwarder");
    if (!pithy) {
        return SRS_STAGE_ENCODER_INTERVAL_MS;
    }
    
    return ::atoi(pithy->arg0().c_str());
}

int SrsConfig::get_pithy_print_play()
{
    SrsConfDirective* pithy = root->get("pithy_print");
    if (!pithy) {
        return SRS_STAGE_PLAY_USER_INTERVAL_MS;
    }
    
    pithy = pithy->get("play");
    if (!pithy) {
        return SRS_STAGE_PLAY_USER_INTERVAL_MS;
    }
    
    return ::atoi(pithy->arg0().c_str());
}

bool srs_directive_equals(SrsConfDirective* a, SrsConfDirective* b)
{
    // both NULL, equal.
    if (!a && !b) {
        return true;
    }
    
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
