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
#include <srs_app_source.hpp>
#include <srs_kernel_file.hpp>

#define SRS_WIKI_URL_LOG "https://github.com/winlinvip/simple-rtmp-server/wiki/SrsLog"

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
    srs_assert(index < (int)directives.size());
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

bool SrsConfDirective::is_vhost()
{
    return name == "vhost";
}

int SrsConfDirective::parse(_srs_internal::SrsConfigBuffer* buffer)
{
    return parse_conf(buffer, parse_file);
}

// see: ngx_conf_parse
int SrsConfDirective::parse_conf(_srs_internal::SrsConfigBuffer* buffer, SrsDirectiveType type)
{
    int ret = ERROR_SUCCESS;
    
    while (true) {
        std::vector<string> args;
        
        ret = read_token(buffer, args);
        
        /**
        * ret maybe:
        * ERROR_SYSTEM_CONFIG_INVALID           error.
        * ERROR_SYSTEM_CONFIG_DIRECTIVE         directive terminated by ';' found
        * ERROR_SYSTEM_CONFIG_BLOCK_START       token terminated by '{' found
        * ERROR_SYSTEM_CONFIG_BLOCK_END         the '}' found
        * ERROR_SYSTEM_CONFIG_EOF               the config file is done
        */
        if (ret == ERROR_SYSTEM_CONFIG_INVALID) {
            return ret;
        }
        if (ret == ERROR_SYSTEM_CONFIG_BLOCK_END) {
            if (type != parse_block) {
                srs_error("line %d: unexpected \"}\", ret=%d", buffer->line + 1, ret);
                return ret;
            }
            return ERROR_SUCCESS;
        }
        if (ret == ERROR_SYSTEM_CONFIG_EOF) {
            if (type == parse_block) {
                srs_error("line %d: unexpected end of file, expecting \"}\", ret=%d", conf_line + 1, ret);
                return ret;
            }
            return ERROR_SUCCESS;
        }
        
        if (args.empty()) {
            ret = ERROR_SYSTEM_CONFIG_INVALID;
            srs_error("line %d: empty directive. ret=%d", conf_line + 1, ret);
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
int SrsConfDirective::read_token(_srs_internal::SrsConfigBuffer* buffer, vector<string>& args)
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
            srs_trace("config parse complete");
            
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
            srs_error("line %d: unexpected '%c'", buffer->line + 1, ch);
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
                        srs_error("line %d: unexpected ';'", buffer->line + 1);
                        return ERROR_SYSTEM_CONFIG_INVALID;
                    }
                    return ERROR_SYSTEM_CONFIG_DIRECTIVE;
                case '{':
                    if (args.size() == 0) {
                        srs_error("line %d: unexpected '{'", buffer->line + 1);
                        return ERROR_SYSTEM_CONFIG_INVALID;
                    }
                    return ERROR_SYSTEM_CONFIG_BLOCK_START;
                case '}':
                    if (args.size() != 0) {
                        srs_error("line %d: unexpected '}'", buffer->line + 1);
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
                char* aword = new char[len];
                memcpy(aword, pstart, len);
                aword[len - 1] = 0;
                
                string word_str = aword;
                if (!word_str.empty()) {
                    args.push_back(word_str);
                }
                srs_freep(aword);
                
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
    test_conf = false;
    
    root = new SrsConfDirective();
    root->conf_line = 0;
    root->name = "root";
}

SrsConfig::~SrsConfig()
{
    srs_freep(root);
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

int SrsConfig::reload()
{
    int ret = ERROR_SUCCESS;

    SrsConfig conf;
    if ((ret = conf.parse_file(config_file.c_str())) != ERROR_SUCCESS) {
        srs_error("ignore config reloader parse file failed. ret=%d", ret);
        ret = ERROR_SUCCESS;
        return ret;
    }
    srs_info("config reloader parse file success.");
    
    SrsConfDirective* old_root = root;
    SrsAutoFree(SrsConfDirective, old_root);
    
    root = conf.root;
    conf.root = NULL;
    
    // merge config.
    std::vector<ISrsReloadHandler*>::iterator it;

    // never support reload:
    //      daemon
    //
    // always support reload without additional code:
    //      chunk_size, ff_log_dir, max_connections,
    //      bandcheck, http_hooks, heartbeat, 
    //      token_traverse

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
    
    // merge config: pid
    if (!srs_directive_equals(root->get("pid"), old_root->get("pid"))) {
        for (it = subscribes.begin(); it != subscribes.end(); ++it) {
            ISrsReloadHandler* subscribe = *it;
            if ((ret = subscribe->on_reload_pid()) != ERROR_SUCCESS) {
                srs_error("notify subscribes reload pid failed. ret=%d", ret);
                return ret;
            }
        }
        srs_trace("reload pid success.");
    }
    
    // merge config: srs_log_tank
    if (!srs_directive_equals(root->get("srs_log_tank"), old_root->get("srs_log_tank"))) {
        for (it = subscribes.begin(); it != subscribes.end(); ++it) {
            ISrsReloadHandler* subscribe = *it;
            if ((ret = subscribe->on_reload_log_tank()) != ERROR_SUCCESS) {
                srs_error("notify subscribes reload srs_log_tank failed. ret=%d", ret);
                return ret;
            }
        }
        srs_trace("reload srs_log_tank success.");
    }
    
    // merge config: srs_log_level
    if (!srs_directive_equals(root->get("srs_log_level"), old_root->get("srs_log_level"))) {
        for (it = subscribes.begin(); it != subscribes.end(); ++it) {
            ISrsReloadHandler* subscribe = *it;
            if ((ret = subscribe->on_reload_log_level()) != ERROR_SUCCESS) {
                srs_error("notify subscribes reload srs_log_level failed. ret=%d", ret);
                return ret;
            }
        }
        srs_trace("reload srs_log_level success.");
    }
    
    // merge config: srs_log_file
    if (!srs_directive_equals(root->get("srs_log_file"), old_root->get("srs_log_file"))) {
        for (it = subscribes.begin(); it != subscribes.end(); ++it) {
            ISrsReloadHandler* subscribe = *it;
            if ((ret = subscribe->on_reload_log_file()) != ERROR_SUCCESS) {
                srs_error("notify subscribes reload srs_log_file failed. ret=%d", ret);
                return ret;
            }
        }
        srs_trace("reload srs_log_file success.");
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
    
    // merge config: http_api
    if ((ret = reload_http_api(old_root)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // merge config: http_stream
    if ((ret = reload_http_stream(old_root)) != ERROR_SUCCESS) {
        return ret;
    }

    // merge config: vhost
    if ((ret = reload_vhost(old_root)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsConfig::reload_http_api(SrsConfDirective* old_root)
{
    int ret = ERROR_SUCCESS;
    
    // merge config.
    std::vector<ISrsReloadHandler*>::iterator it;
    
    // state graph
    //      old_http_api    new_http_api
    //      DISABLED    =>  ENABLED
    //      ENABLED     =>  DISABLED
    //      ENABLED     =>  ENABLED (modified)
    
    SrsConfDirective* new_http_api = root->get("http_api");
    SrsConfDirective* old_http_api = old_root->get("http_api");

    // DISABLED    =>      ENABLED
    if (!get_http_api_enabled(old_http_api) && get_http_api_enabled(new_http_api)) {
        for (it = subscribes.begin(); it != subscribes.end(); ++it) {
            ISrsReloadHandler* subscribe = *it;
            if ((ret = subscribe->on_reload_http_api_enabled()) != ERROR_SUCCESS) {
                srs_error("notify subscribes http_api disabled=>enabled failed. ret=%d", ret);
                return ret;
            }
        }
        srs_trace("reload disabled=>enabled http_api success.");
        
        return ret;
    }

    // ENABLED     =>      DISABLED
    if (get_http_api_enabled(old_http_api) && !get_http_api_enabled(new_http_api)) {
        for (it = subscribes.begin(); it != subscribes.end(); ++it) {
            ISrsReloadHandler* subscribe = *it;
            if ((ret = subscribe->on_reload_http_api_disabled()) != ERROR_SUCCESS) {
                srs_error("notify subscribes http_api enabled=>disabled failed. ret=%d", ret);
                return ret;
            }
        }
        srs_trace("reload enabled=>disabled http_api success.");
        
        return ret;
    }
    
    //      ENABLED     =>  ENABLED (modified)
    if (get_http_api_enabled(old_http_api) && get_http_api_enabled(new_http_api)
        && !srs_directive_equals(old_http_api, new_http_api)
    ) {
        for (it = subscribes.begin(); it != subscribes.end(); ++it) {
            ISrsReloadHandler* subscribe = *it;
            if ((ret = subscribe->on_reload_http_api_enabled()) != ERROR_SUCCESS) {
                srs_error("notify subscribes http_api enabled modified failed. ret=%d", ret);
                return ret;
            }
        }
        srs_trace("reload enabled modified http_api success.");
        
        return ret;
    }
    
    srs_trace("reload http_api not changed success.");
    
    return ret;
}

int SrsConfig::reload_http_stream(SrsConfDirective* old_root)
{
    int ret = ERROR_SUCCESS;
    
    // merge config.
    std::vector<ISrsReloadHandler*>::iterator it;
    
    // state graph
    //      old_http_stream     new_http_stream
    //      DISABLED    =>      ENABLED
    //      ENABLED     =>      DISABLED
    //      ENABLED     =>      ENABLED (modified)
    
    SrsConfDirective* new_http_stream = root->get("http_stream");
    SrsConfDirective* old_http_stream = old_root->get("http_stream");

    // DISABLED    =>      ENABLED
    if (!get_http_stream_enabled(old_http_stream) && get_http_stream_enabled(new_http_stream)) {
        for (it = subscribes.begin(); it != subscribes.end(); ++it) {
            ISrsReloadHandler* subscribe = *it;
            if ((ret = subscribe->on_reload_http_stream_enabled()) != ERROR_SUCCESS) {
                srs_error("notify subscribes http_stream disabled=>enabled failed. ret=%d", ret);
                return ret;
            }
        }
        srs_trace("reload disabled=>enabled http_stream success.");
        
        return ret;
    }

    // ENABLED     =>      DISABLED
    if (get_http_stream_enabled(old_http_stream) && !get_http_stream_enabled(new_http_stream)) {
        for (it = subscribes.begin(); it != subscribes.end(); ++it) {
            ISrsReloadHandler* subscribe = *it;
            if ((ret = subscribe->on_reload_http_stream_disabled()) != ERROR_SUCCESS) {
                srs_error("notify subscribes http_stream enabled=>disabled failed. ret=%d", ret);
                return ret;
            }
        }
        srs_trace("reload enabled=>disabled http_stream success.");
        
        return ret;
    }
    
    //      ENABLED     =>  ENABLED (modified)
    if (get_http_stream_enabled(old_http_stream) && get_http_stream_enabled(new_http_stream)
        && !srs_directive_equals(old_http_stream, new_http_stream)
    ) {
        for (it = subscribes.begin(); it != subscribes.end(); ++it) {
            ISrsReloadHandler* subscribe = *it;
            if ((ret = subscribe->on_reload_http_stream_updated()) != ERROR_SUCCESS) {
                srs_error("notify subscribes http_stream enabled modified failed. ret=%d", ret);
                return ret;
            }
        }
        srs_trace("reload enabled modified http_stream success.");
        
        return ret;
    }
    
    srs_trace("reload http_stream not changed success.");
    
    return ret;
}

int SrsConfig::reload_vhost(SrsConfDirective* old_root)
{
    int ret = ERROR_SUCCESS;
    
    // merge config.
    std::vector<ISrsReloadHandler*>::iterator it;
    
    // state graph
    //      old_vhost       new_vhost
    //      DISABLED    =>  ENABLED
    //      ENABLED     =>  DISABLED
    //      ENABLED     =>  ENABLED (modified)

    // collect all vhost names
    std::vector<std::string> vhosts;
    for (int i = 0; i < (int)root->directives.size(); i++) {
        SrsConfDirective* vhost = root->at(i);
        if (vhost->name != "vhost") {
            continue;
        }
        vhosts.push_back(vhost->arg0());
    }
    for (int i = 0; i < (int)old_root->directives.size(); i++) {
        SrsConfDirective* vhost = old_root->at(i);
        if (vhost->name != "vhost") {
            continue;
        }
        if (root->get("vhost", vhost->arg0())) {
            continue;
        }
        vhosts.push_back(vhost->arg0());
    }
    
    // process each vhost
    for (int i = 0; i < (int)vhosts.size(); i++) {
        std::string vhost = vhosts.at(i);

        SrsConfDirective* old_vhost = old_root->get("vhost", vhost);
        SrsConfDirective* new_vhost = root->get("vhost", vhost);
        
        //      DISABLED    =>  ENABLED
        if (!get_vhost_enabled(old_vhost) && get_vhost_enabled(new_vhost)) {
            srs_trace("vhost %s added, reload it.", vhost.c_str());
            for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                ISrsReloadHandler* subscribe = *it;
                if ((ret = subscribe->on_reload_vhost_added(vhost)) != ERROR_SUCCESS) {
                    srs_error("notify subscribes added "
                        "vhost %s failed. ret=%d", vhost.c_str(), ret);
                    return ret;
                }
            }
            srs_trace("reload new vhost %s success.", vhost.c_str());
            continue;
        }
        
        //      ENABLED     =>  DISABLED
        if (get_vhost_enabled(old_vhost) && !get_vhost_enabled(new_vhost)) {
            srs_trace("vhost %s removed, reload it.", vhost.c_str());
            for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                ISrsReloadHandler* subscribe = *it;
                if ((ret = subscribe->on_reload_vhost_removed(vhost)) != ERROR_SUCCESS) {
                    srs_error("notify subscribes removed "
                        "vhost %s failed. ret=%d", vhost.c_str(), ret);
                    return ret;
                }
            }
            srs_trace("reload removed vhost %s success.", vhost.c_str());
            continue;
        }
        
        // mode, never supports reload.
        // first, for the origin and edge role change is too complex.
        // second, the vhosts in origin device group normally are all origin,
        //      they never change to edge sometimes.
        // third, the origin or upnode device can always be restart,
        //      edge will retry and the users connected to edge are ok.
        // it's ok to add or remove edge/origin vhost.
        if (get_vhost_is_edge(old_vhost) != get_vhost_is_edge(new_vhost)) {
            ret = ERROR_RTMP_EDGE_RELOAD;
            srs_error("reload never supports mode changed. ret=%d", ret);
            return ret;
        }
    
        //      ENABLED     =>  ENABLED (modified)
        if (get_vhost_enabled(new_vhost) && get_vhost_enabled(old_vhost)) {
            srs_trace("vhost %s maybe modified, reload its detail.", vhost.c_str());
            // atc, only one per vhost
            if (!srs_directive_equals(new_vhost->get("atc"), old_vhost->get("atc"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((ret = subscribe->on_reload_vhost_atc(vhost)) != ERROR_SUCCESS) {
                        srs_error("vhost %s notify subscribes atc failed. ret=%d", vhost.c_str(), ret);
                        return ret;
                    }
                }
                srs_trace("vhost %s reload atc success.", vhost.c_str());
            }
            // gop_cache, only one per vhost
            if (!srs_directive_equals(new_vhost->get("gop_cache"), old_vhost->get("gop_cache"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((ret = subscribe->on_reload_vhost_gop_cache(vhost)) != ERROR_SUCCESS) {
                        srs_error("vhost %s notify subscribes gop_cache failed. ret=%d", vhost.c_str(), ret);
                        return ret;
                    }
                }
                srs_trace("vhost %s reload gop_cache success.", vhost.c_str());
            }
            // queue_length, only one per vhost
            if (!srs_directive_equals(new_vhost->get("queue_length"), old_vhost->get("queue_length"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((ret = subscribe->on_reload_vhost_queue_length(vhost)) != ERROR_SUCCESS) {
                        srs_error("vhost %s notify subscribes queue_length failed. ret=%d", vhost.c_str(), ret);
                        return ret;
                    }
                }
                srs_trace("vhost %s reload queue_length success.", vhost.c_str());
            }
            // time_jitter, only one per vhost
            if (!srs_directive_equals(new_vhost->get("time_jitter"), old_vhost->get("time_jitter"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((ret = subscribe->on_reload_vhost_time_jitter(vhost)) != ERROR_SUCCESS) {
                        srs_error("vhost %s notify subscribes time_jitter failed. ret=%d", vhost.c_str(), ret);
                        return ret;
                    }
                }
                srs_trace("vhost %s reload time_jitter success.", vhost.c_str());
            }
            // forward, only one per vhost
            if (!srs_directive_equals(new_vhost->get("forward"), old_vhost->get("forward"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((ret = subscribe->on_reload_vhost_forward(vhost)) != ERROR_SUCCESS) {
                        srs_error("vhost %s notify subscribes forward failed. ret=%d", vhost.c_str(), ret);
                        return ret;
                    }
                }
                srs_trace("vhost %s reload forward success.", vhost.c_str());
            }
            // hls, only one per vhost
            if (!srs_directive_equals(new_vhost->get("hls"), old_vhost->get("hls"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((ret = subscribe->on_reload_vhost_hls(vhost)) != ERROR_SUCCESS) {
                        srs_error("vhost %s notify subscribes hls failed. ret=%d", vhost.c_str(), ret);
                        return ret;
                    }
                }
                srs_trace("vhost %s reload hls success.", vhost.c_str());
            }
            // dvr, only one per vhost
            if (!srs_directive_equals(new_vhost->get("dvr"), old_vhost->get("dvr"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((ret = subscribe->on_reload_vhost_dvr(vhost)) != ERROR_SUCCESS) {
                        srs_error("vhost %s notify subscribes dvr failed. ret=%d", vhost.c_str(), ret);
                        return ret;
                    }
                }
                srs_trace("vhost %s reload hls success.", vhost.c_str());
            }
            // http, only one per vhost.
            if (!srs_directive_equals(new_vhost->get("http"), old_vhost->get("http"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((ret = subscribe->on_reload_vhost_http_updated()) != ERROR_SUCCESS) {
                        srs_error("vhost %s notify subscribes http failed. ret=%d", vhost.c_str(), ret);
                        return ret;
                    }
                }
                srs_trace("vhost %s reload http success.", vhost.c_str());
            }
            // transcode, many per vhost.
            if ((ret = reload_transcode(new_vhost, old_vhost)) != ERROR_SUCCESS) {
                return ret;
            }
            // ingest, many per vhost.
            if ((ret = reload_ingest(new_vhost, old_vhost)) != ERROR_SUCCESS) {
                return ret;
            }
            continue;
        }
        srs_trace("igreno reload vhost, enabled old: %d, new: %d", 
            get_vhost_enabled(old_vhost), get_vhost_enabled(new_vhost));
    }
    
    return ret;
}

int SrsConfig::reload_transcode(SrsConfDirective* new_vhost, SrsConfDirective* old_vhost)
{
    int ret = ERROR_SUCCESS;
    
    std::vector<SrsConfDirective*> old_transcoders;
    for (int i = 0; i < (int)old_vhost->directives.size(); i++) {
        SrsConfDirective* conf = old_vhost->at(i);
        if (conf->name == "transcode") {
            old_transcoders.push_back(conf);
        }
    }
    
    std::vector<SrsConfDirective*> new_transcoders;
    for (int i = 0; i < (int)new_vhost->directives.size(); i++) {
        SrsConfDirective* conf = new_vhost->at(i);
        if (conf->name == "transcode") {
            new_transcoders.push_back(conf);
        }
    }
    
    std::vector<ISrsReloadHandler*>::iterator it;
    
    std::string vhost = new_vhost->arg0();
    
    // to be simple:
    // whatever, once tiny changed of transcode, 
    // restart all ffmpeg of vhost.
    bool changed = false;
    
    // discovery the removed ffmpeg.
    for (int i = 0; !changed && i < (int)old_transcoders.size(); i++) {
        SrsConfDirective* old_transcoder = old_transcoders.at(i);
        std::string transcoder_id = old_transcoder->arg0();
        
        // if transcoder exists in new vhost, not removed, ignore.
        if (new_vhost->get("transcode", transcoder_id)) {
            continue;
        }
        
        changed = true;
    }
    
    // discovery the added ffmpeg.
    for (int i = 0; !changed && i < (int)new_transcoders.size(); i++) {
        SrsConfDirective* new_transcoder = new_transcoders.at(i);
        std::string transcoder_id = new_transcoder->arg0();
        
        // if transcoder exists in old vhost, not added, ignore.
        if (old_vhost->get("transcode", transcoder_id)) {
            continue;
        }
        
        changed = true;
    }
    
    // for updated transcoders, restart them.
    for (int i = 0; !changed && i < (int)new_transcoders.size(); i++) {
        SrsConfDirective* new_transcoder = new_transcoders.at(i);
        std::string transcoder_id = new_transcoder->arg0();
        SrsConfDirective* old_transcoder = old_vhost->get("transcode", transcoder_id);
        srs_assert(old_transcoder);
        
        if (srs_directive_equals(new_transcoder, old_transcoder)) {
            continue;
        }
        
        changed = true;
    }
    
    // transcode, many per vhost
    if (changed) {
        for (it = subscribes.begin(); it != subscribes.end(); ++it) {
            ISrsReloadHandler* subscribe = *it;
            if ((ret = subscribe->on_reload_vhost_transcode(vhost)) != ERROR_SUCCESS) {
                srs_error("vhost %s notify subscribes transcode failed. ret=%d", vhost.c_str(), ret);
                return ret;
            }
        }
        srs_trace("vhost %s reload transcode success.", vhost.c_str());
    }
    
    return ret;
}

int SrsConfig::reload_ingest(SrsConfDirective* new_vhost, SrsConfDirective* old_vhost)
{
    int ret = ERROR_SUCCESS;
    
    std::vector<SrsConfDirective*> old_ingesters;
    for (int i = 0; i < (int)old_vhost->directives.size(); i++) {
        SrsConfDirective* conf = old_vhost->at(i);
        if (conf->name == "ingest") {
            old_ingesters.push_back(conf);
        }
    }
    
    std::vector<SrsConfDirective*> new_ingesters;
    for (int i = 0; i < (int)new_vhost->directives.size(); i++) {
        SrsConfDirective* conf = new_vhost->at(i);
        if (conf->name == "ingest") {
            new_ingesters.push_back(conf);
        }
    }
    
    std::vector<ISrsReloadHandler*>::iterator it;
    
    std::string vhost = new_vhost->arg0();
    
    // for removed ingesters, stop them.
    for (int i = 0; i < (int)old_ingesters.size(); i++) {
        SrsConfDirective* old_ingester = old_ingesters.at(i);
        std::string ingest_id = old_ingester->arg0();
        
        // if ingester exists in new vhost, not removed, ignore.
        if (new_vhost->get("ingest", ingest_id)) {
            continue;
        }

        // notice handler ingester removed.
        for (it = subscribes.begin(); it != subscribes.end(); ++it) {
            ISrsReloadHandler* subscribe = *it;
            if ((ret = subscribe->on_reload_ingest_removed(vhost, ingest_id)) != ERROR_SUCCESS) {
                srs_error("vhost %s notify subscribes ingest=%s removed failed. ret=%d", 
                    vhost.c_str(), ingest_id.c_str(), ret);
                return ret;
            }
        }
        srs_trace("vhost %s reload ingest=%s removed success.", vhost.c_str(), ingest_id.c_str());
    }
    
    // for added ingesters, start them.
    for (int i = 0; i < (int)new_ingesters.size(); i++) {
        SrsConfDirective* new_ingester = new_ingesters.at(i);
        std::string ingest_id = new_ingester->arg0();
        
        // if ingester exists in old vhost, not added, ignore.
        if (old_vhost->get("ingest", ingest_id)) {
            continue;
        }

        // notice handler ingester removed.
        for (it = subscribes.begin(); it != subscribes.end(); ++it) {
            ISrsReloadHandler* subscribe = *it;
            if ((ret = subscribe->on_reload_ingest_added(vhost, ingest_id)) != ERROR_SUCCESS) {
                srs_error("vhost %s notify subscribes ingest=%s added failed. ret=%d", 
                    vhost.c_str(), ingest_id.c_str(), ret);
                return ret;
            }
        }
        srs_trace("vhost %s reload ingest=%s added success.", vhost.c_str(), ingest_id.c_str());
    }

    // for updated ingesters, restart them.
    for (int i = 0; i < (int)new_ingesters.size(); i++) {
        SrsConfDirective* new_ingester = new_ingesters.at(i);
        std::string ingest_id = new_ingester->arg0();
        SrsConfDirective* old_ingester = old_vhost->get("ingest", ingest_id);
        
        // ignore the added ingester.
        if (!old_ingester) {
            continue;
        }
        
        if (srs_directive_equals(new_ingester, old_ingester)) {
            continue;
        }

        // notice handler ingester removed.
        for (it = subscribes.begin(); it != subscribes.end(); ++it) {
            ISrsReloadHandler* subscribe = *it;
            if ((ret = subscribe->on_reload_ingest_updated(vhost, ingest_id)) != ERROR_SUCCESS) {
                srs_error("vhost %s notify subscribes ingest=%s updated failed. ret=%d", 
                    vhost.c_str(), ingest_id.c_str(), ret);
                return ret;
            }
        }
        srs_trace("vhost %s reload ingest=%s updated success.", vhost.c_str(), ingest_id.c_str());
    }

    srs_trace("ingest not changed for vhost=%s", vhost.c_str());
    
    return ret;
}

// see: ngx_get_options
int SrsConfig::parse_options(int argc, char** argv)
{
    int ret = ERROR_SUCCESS;
    
    // argv
    for (int i = 0; i < argc; i++) {
        _argv.append(argv[i]);
        
        if (i < argc - 1) {
            _argv.append(" ");
        }
    }
    
    // cwd
    char cwd[256];
    getcwd(cwd, sizeof(cwd));
    _cwd = cwd;
    
    // config
    show_help = true;
    for (int i = 1; i < argc; i++) {
        if ((ret = parse_argv(i, argv)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    if (show_help) {
        print_help(argv);
        exit(0);
    }
    
    if (show_version) {
        fprintf(stderr, "%s\n", RTMP_SIG_SRS_VERSION);
        exit(0);
    }
    
    if (config_file.empty()) {
        ret = ERROR_SYSTEM_CONFIG_INVALID;
        srs_error("config file not specified, see help: %s -h, ret=%d", argv[0], ret);
        return ret;
    }

    ret = parse_file(config_file.c_str());
    
    if (test_conf) {
        if (ret == ERROR_SUCCESS) {
            srs_trace("config file is ok");
            exit(0);
        } else {
            srs_error("config file is invalid");
            exit(ret);
        }
    }
    
    return ret;
}

int SrsConfig::parse_argv(int& i, char** argv)
{
    int ret = ERROR_SUCCESS;
    
    char* p = argv[i];
        
    if (*p++ != '-') {
        show_help = true;
        return ret;
    }
    
    while (*p) {
        switch (*p++) {
            case '?':
            case 'h':
                show_help = true;
                break;
            case 't':
                show_help = false;
                test_conf = true;
                break;
            case 'v':
            case 'V':
                show_help = false;
                show_version = true;
                break;
            case 'c':
                show_help = false;
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
        "license: "RTMP_SIG_SRS_LICENSE"\n"
        "Primary Authors: "RTMP_SIG_SRS_PRIMARY_AUTHROS"\n"
        "Build: "SRS_AUTO_BUILD_DATE" Configuration:"SRS_AUTO_USER_CONFIGURE"\n"
        "Features:"SRS_AUTO_CONFIGURE"\n""\n"
        "Usage: %s [-h?vV] [[-t] -c <filename>]\n" 
        "\n"
        "Options:\n"
        "   -?, -h              : show this help and exit(0)\n"
        "   -v, -V              : show version and exit(0)\n"
        "   -t                  : test configuration file, exit(error_code).\n"
        "   -c filename         : use configuration file for SRS\n"
        "\n"
        RTMP_SIG_SRS_WEB"\n"
        RTMP_SIG_SRS_URL"\n"
        "Email: "RTMP_SIG_SRS_EMAIL"\n"
        "\n"
        "For example:\n"
        "   %s -v\n"
        "   %s -t -c "SRS_CONF_DEFAULT_COFNIG_FILE"\n"
        "   %s -c "SRS_CONF_DEFAULT_COFNIG_FILE"\n",
        argv[0], argv[0], argv[0], argv[0]);
}

int SrsConfig::parse_file(const char* filename)
{
    int ret = ERROR_SUCCESS;
    
    config_file = filename;
    
    if (config_file.empty()) {
        return ERROR_SYSTEM_CONFIG_INVALID;
    }
    
    _srs_internal::SrsConfigBuffer buffer;
    
    if ((ret = buffer.fullfill(config_file.c_str())) != ERROR_SUCCESS) {
        return ret;
    }
    
    return parse_buffer(&buffer);
}

int SrsConfig::parse_buffer(_srs_internal::SrsConfigBuffer* buffer)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = root->parse(buffer)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // check root directives.
    for (int i = 0; i < (int)root->directives.size(); i++) {
        SrsConfDirective* conf = root->at(i);
        std::string n = conf->name;
        if (n != "listen" && n != "pid" && n != "chunk_size" && n != "ff_log_dir" 
            && n != "srs_log_tank" && n != "srs_log_level" && n != "srs_log_file"
            && n != "max_connections" && n != "daemon" && n != "heartbeat"
            && n != "http_api" && n != "http_stream" && n != "vhost"
            && n != "pithy_print") 
        {
            ret = ERROR_SYSTEM_CONFIG_INVALID;
            srs_error("unsupported directive %s, ret=%d", n.c_str(), ret);
            return ret;
        }
    }
    
    // check rtmp port specified by directive listen.
    if (_srs_config->get_listen().size() <= 0) {
        ret = ERROR_SYSTEM_CONFIG_INVALID;
        srs_error("directive \"listen\" is empty, ret=%d", ret);
        return ret;
    }
    
    // TODO: FIXME: check others.
    
    // check log
    std::string log_filename = this->get_log_file();
    if (get_log_tank_file() && log_filename.empty()) {
        ret = ERROR_SYSTEM_CONFIG_INVALID;
        srs_error("must specifies the file to write log to. ret=%d", ret);
        return ret;
    }
    if (get_log_tank_file()) {
        srs_trace("write log to file %s", log_filename.c_str());
        srs_trace("you can: tailf %s", log_filename.c_str());
        srs_trace("@see: %s", SRS_WIKI_URL_LOG);
    } else {
        srs_trace("write log to console");
    }
    
    return ret;
}

string SrsConfig::cwd()
{
    return _cwd;
}

string SrsConfig::argv()
{
    return _argv;
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

SrsConfDirective* SrsConfig::get_root()
{
    return root;
}

int SrsConfig::get_max_connections()
{
    srs_assert(root);
    
    SrsConfDirective* conf = root->get("max_connections");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_MAX_CONNECTIONS;
    }
    
    return ::atoi(conf->arg0().c_str());
}

vector<string> SrsConfig::get_listen()
{
    std::vector<string> ports;
    
    SrsConfDirective* conf = root->get("listen");
    if (!conf) {
        return ports;
    }
    
    for (int i = 0; i < (int)conf->args.size(); i++) {
        ports.push_back(conf->args.at(i));
    }
    
    return ports;
}

string SrsConfig::get_pid_file()
{
    SrsConfDirective* conf = root->get("pid");
    
    if (!conf) {
        return SRS_CONF_DEFAULT_PID_FILE;
    }
    
    return conf->arg0();
}

int SrsConfig::get_pithy_print_publish()
{
    SrsConfDirective* pithy = root->get("pithy_print");
    if (!pithy) {
        return SRS_CONF_DEFAULT_STAGE_PUBLISH_USER_INTERVAL_MS;
    }
    
    pithy = pithy->get("publish");
    if (!pithy) {
        return SRS_CONF_DEFAULT_STAGE_PUBLISH_USER_INTERVAL_MS;
    }
    
    return ::atoi(pithy->arg0().c_str());
}

int SrsConfig::get_pithy_print_forwarder()
{
    SrsConfDirective* pithy = root->get("pithy_print");
    if (!pithy) {
        return SRS_CONF_DEFAULT_STAGE_FORWARDER_INTERVAL_MS;
    }
    
    pithy = pithy->get("forwarder");
    if (!pithy) {
        return SRS_CONF_DEFAULT_STAGE_FORWARDER_INTERVAL_MS;
    }
    
    return ::atoi(pithy->arg0().c_str());
}

int SrsConfig::get_pithy_print_encoder()
{
    SrsConfDirective* pithy = root->get("pithy_print");
    if (!pithy) {
        return SRS_CONF_DEFAULT_STAGE_ENCODER_INTERVAL_MS;
    }
    
    pithy = pithy->get("encoder");
    if (!pithy) {
        return SRS_CONF_DEFAULT_STAGE_ENCODER_INTERVAL_MS;
    }
    
    return ::atoi(pithy->arg0().c_str());
}

int SrsConfig::get_pithy_print_ingester()
{
    SrsConfDirective* pithy = root->get("pithy_print");
    if (!pithy) {
        return SRS_CONF_DEFAULT_STAGE_INGESTER_INTERVAL_MS;
    }
    
    pithy = pithy->get("ingester");
    if (!pithy) {
        return SRS_CONF_DEFAULT_STAGE_INGESTER_INTERVAL_MS;
    }
    
    return ::atoi(pithy->arg0().c_str());
}

int SrsConfig::get_pithy_print_hls()
{
    SrsConfDirective* pithy = root->get("pithy_print");
    if (!pithy) {
        return SRS_CONF_DEFAULT_STAGE_HLS_INTERVAL_MS;
    }
    
    pithy = pithy->get("hls");
    if (!pithy) {
        return SRS_CONF_DEFAULT_STAGE_HLS_INTERVAL_MS;
    }
    
    return ::atoi(pithy->arg0().c_str());
}

int SrsConfig::get_pithy_print_play()
{
    SrsConfDirective* pithy = root->get("pithy_print");
    if (!pithy) {
        return SRS_CONF_DEFAULT_STAGE_PLAY_USER_INTERVAL_MS;
    }
    
    pithy = pithy->get("play");
    if (!pithy) {
        return SRS_CONF_DEFAULT_STAGE_PLAY_USER_INTERVAL_MS;
    }
    
    return ::atoi(pithy->arg0().c_str());
}

int SrsConfig::get_pithy_print_edge()
{
    SrsConfDirective* pithy = root->get("pithy_print");
    if (!pithy) {
        return SRS_CONF_DEFAULT_STAGE_EDGE_INTERVAL_MS;
    }
    
    pithy = pithy->get("edge");
    if (!pithy) {
        return SRS_CONF_DEFAULT_STAGE_EDGE_INTERVAL_MS;
    }
    
    return ::atoi(pithy->arg0().c_str());
}

SrsConfDirective* SrsConfig::get_vhost(string vhost)
{
    srs_assert(root);
    
    for (int i = 0; i < (int)root->directives.size(); i++) {
        SrsConfDirective* conf = root->at(i);
        
        if (!conf->is_vhost()) {
            continue;
        }
        
        if (conf->arg0() == vhost) {
            return conf;
        }
    }
    
    if (vhost != SRS_CONSTS_RTMP_DEFAULT_VHOST) {
        return get_vhost(SRS_CONSTS_RTMP_DEFAULT_VHOST);
    }
    
    return NULL;
}

vector<SrsConfDirective*> SrsConfig::get_vhosts()
{
    srs_assert(root);
    
    std::vector<SrsConfDirective*> vhosts;
    
    for (int i = 0; i < (int)root->directives.size(); i++) {
        SrsConfDirective* conf = root->at(i);
        
        if (!conf->is_vhost()) {
            continue;
        }
        
        vhosts.push_back(conf);
    }
    
    return vhosts;
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
        return false;
    }
    
    conf = conf->get("atc");
    if (conf && conf->arg0() == "on") {
        return true;
    }
    
    return false;
}

bool SrsConfig::get_atc_auto(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) {
        return true;
    }
    
    conf = conf->get("atc_auto");
    if (conf && conf->arg0() == "off") {
        return false;
    }
    
    return true;
}

int SrsConfig::get_time_jitter(string vhost)
{
    SrsConfDirective* dvr = get_vhost(vhost);
    
    std::string time_jitter = SRS_CONF_DEFAULT_TIME_JITTER;
    
    if (dvr) {
        SrsConfDirective* conf = dvr->get("time_jitter");
    
        if (conf) {
            time_jitter = conf->arg0();
        }
    }
    
    return _srs_time_jitter_string2int(time_jitter);
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

int SrsConfig::get_chunk_size(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) {
        return SRS_CONSTS_RTMP_SRS_CHUNK_SIZE;
    }

    conf = conf->get("chunk_size");
    if (!conf) {
        // vhost does not specify the chunk size,
        // use the global instead.
        conf = root->get("chunk_size");
        if (!conf) {
            return SRS_CONSTS_RTMP_SRS_CHUNK_SIZE;
        }
        
        return ::atoi(conf->arg0().c_str());
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

SrsConfDirective* SrsConfig::get_vhost_on_dvr_hss_reap_flv(string vhost)
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
    
    return conf->get("on_dvr_hss_reap_flv");
}

bool SrsConfig::get_bw_check_enabled(string vhost)
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

string SrsConfig::get_bw_check_key(string vhost)
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

int SrsConfig::get_bw_check_interval_ms(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) {
        return SRS_CONF_DEFAULT_BANDWIDTH_INTERVAL * 1000;
    }

    conf = conf->get("bandcheck");
    if (!conf) {
        return SRS_CONF_DEFAULT_BANDWIDTH_INTERVAL * 1000;
    }
    
    conf = conf->get("interval");
    if (!conf) {
        return SRS_CONF_DEFAULT_BANDWIDTH_INTERVAL * 1000;
    }

    return ::atof(conf->arg0().c_str()) * 1000;
}

int SrsConfig::get_bw_check_limit_kbps(string vhost)
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

bool SrsConfig::get_vhost_is_edge(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    return get_vhost_is_edge(conf);
}

bool SrsConfig::get_vhost_is_edge(SrsConfDirective* vhost)
{
    SrsConfDirective* conf = vhost;
    
    if (!conf) {
        return false;
    }
    
    conf = conf->get("mode");
    if (!conf || conf->arg0() != "remote") {
        return false;
    }
    
    return true;
}

SrsConfDirective* SrsConfig::get_vhost_edge_origin(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    
    if (!conf) {
        return NULL;
    }
    
    return conf->get("origin");
}

bool SrsConfig::get_vhost_edge_token_traverse(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    
    if (!conf) {
        return false;
    }
    
    conf = conf->get("token_traverse");
    if (!conf || conf->arg0() != "on") {
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

vector<SrsConfDirective*> SrsConfig::get_transcode_engines(SrsConfDirective* transcode)
{
    vector<SrsConfDirective*> engines;
    
    if (!transcode) {
        return engines;
    }
    
    for (int i = 0; i < (int)transcode->directives.size(); i++) {
        SrsConfDirective* conf = transcode->directives[i];
        
        if (conf->name == "engine") {
            engines.push_back(conf);
        }
    }
    
    return engines;
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

string SrsConfig::get_engine_iformat(SrsConfDirective* engine)
{
    if (!engine) {
        return SRS_CONF_DEFAULT_TRANSCODE_IFORMAT;
    }
    
    SrsConfDirective* conf = engine->get("iformat");
    if (!conf) {
        return SRS_CONF_DEFAULT_TRANSCODE_IFORMAT;
    }
    
    return conf->arg0();
}

vector<string> SrsConfig::get_engine_vfilter(SrsConfDirective* engine)
{
    vector<string> vfilter;
    
    if (!engine) {
        return vfilter;
    }
    
    SrsConfDirective* conf = engine->get("vfilter");
    if (!conf) {
        return vfilter;
    }
    
    for (int i = 0; i < (int)conf->directives.size(); i++) {
        SrsConfDirective* p = conf->directives[i];
        if (!p) {
            continue;
        }
        
        vfilter.push_back("-" + p->name);
        vfilter.push_back(p->arg0());
    }
    
    return vfilter;
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

vector<string> SrsConfig::get_engine_vparams(SrsConfDirective* engine)
{
    vector<string> vparams;

    if (!engine) {
        return vparams;
    }
    
    SrsConfDirective* conf = engine->get("vparams");
    if (!conf) {
        return vparams;
    }
    
    for (int i = 0; i < (int)conf->directives.size(); i++) {
        SrsConfDirective* p = conf->directives[i];
        if (!p) {
            continue;
        }
        
        vparams.push_back("-" + p->name);
        vparams.push_back(p->arg0());
    }
    
    return vparams;
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

vector<string> SrsConfig::get_engine_aparams(SrsConfDirective* engine)
{
    vector<string> aparams;
    
    if (!engine) {
        return aparams;
    }
    
    SrsConfDirective* conf = engine->get("aparams");
    if (!conf) {
        return aparams;
    }
    
    for (int i = 0; i < (int)conf->directives.size(); i++) {
        SrsConfDirective* p = conf->directives[i];
        if (!p) {
            continue;
        }
        
        aparams.push_back("-" + p->name);
        aparams.push_back(p->arg0());
    }
    
    return aparams;
}

string SrsConfig::get_engine_oformat(SrsConfDirective* engine)
{
    if (!engine) {
        return SRS_CONF_DEFAULT_TRANSCODE_OFORMAT;
    }
    
    SrsConfDirective* conf = engine->get("oformat");
    if (!conf) {
        return SRS_CONF_DEFAULT_TRANSCODE_OFORMAT;
    }
    
    return conf->arg0();
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

vector<SrsConfDirective*> SrsConfig::get_ingesters(string vhost)
{
    vector<SrsConfDirective*> ingeters;
    
    SrsConfDirective* vhost_conf = get_vhost(vhost);
    if (!vhost_conf) {
        return ingeters;
    }
    
    for (int i = 0; i < (int)vhost_conf->directives.size(); i++) {
        SrsConfDirective* conf = vhost_conf->directives[i];
        
        if (conf->name == "ingest") {
            ingeters.push_back(conf);
        }
    }
    
    return ingeters;
}

SrsConfDirective* SrsConfig::get_ingest_by_id(string vhost, string ingest_id)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return NULL;
    }
    
    conf = conf->get("ingest", ingest_id);
    return conf;
}

bool SrsConfig::get_ingest_enabled(SrsConfDirective* ingest)
{
    SrsConfDirective* conf = ingest->get("enabled");
    
    if (!conf || conf->arg0() != "on") {
        return false;
    }
    
    return true;
}

string SrsConfig::get_ingest_ffmpeg(SrsConfDirective* ingest)
{
    SrsConfDirective* conf = ingest->get("ffmpeg");
    
    if (!conf) {
        return "";
    }
    
    return conf->arg0();
}

string SrsConfig::get_ingest_input_type(SrsConfDirective* ingest)
{
    SrsConfDirective* conf = ingest->get("input");
    
    if (!conf) {
        return SRS_CONF_DEFAULT_INGEST_TYPE_FILE;
    }

    conf = conf->get("type");
    
    if (!conf) {
        return SRS_CONF_DEFAULT_INGEST_TYPE_FILE;
    }
    
    return conf->arg0();
}

string SrsConfig::get_ingest_input_url(SrsConfDirective* ingest)
{
    SrsConfDirective* conf = ingest->get("input");
    
    if (!conf) {
        return "";
    }

    conf = conf->get("url");
    
    if (!conf) {
        return "";
    }
    
    return conf->arg0();
}

string SrsConfig::get_log_file()
{
    srs_assert(root);
    
    SrsConfDirective* conf = root->get("srs_log_file");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_LOG_FILE;
    }
    
    return conf->arg0();
}

bool SrsConfig::get_ffmpeg_log_enabled()
{
    string log = get_ffmpeg_log_dir();
    return log != SRS_CONSTS_NULL_FILE;
}

string SrsConfig::get_ffmpeg_log_dir()
{
    srs_assert(root);
    
    SrsConfDirective* conf = root->get("ff_log_dir");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_FF_LOG_DIR;
    }
    
    return conf->arg0();
}

string SrsConfig::get_log_level()
{
    srs_assert(root);
    
    SrsConfDirective* conf = root->get("srs_log_level");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_LOG_LEVEL;
    }
    
    return conf->arg0();
}

bool SrsConfig::get_log_tank_file()
{
    srs_assert(root);
    
    SrsConfDirective* conf = root->get("srs_log_tank");
    if (conf && conf->arg0() == SRS_CONF_DEFAULT_LOG_TANK_CONSOLE) {
        return false;
    }
    
    return true;
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

SrsConfDirective* SrsConfig::get_dvr(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) {
        return NULL;
    }
    
    return conf->get("dvr");
}

bool SrsConfig::get_dvr_enabled(string vhost)
{
    SrsConfDirective* dvr = get_dvr(vhost);
    
    if (!dvr) {
        return false;
    }
    
    SrsConfDirective* conf = dvr->get("enabled");
    
    if (!conf) {
        return false;
    }
    
    if (conf->arg0() == "on") {
        return true;
    }
    
    return false;
}

string SrsConfig::get_dvr_path(string vhost)
{
    SrsConfDirective* dvr = get_dvr(vhost);
    
    if (!dvr) {
        return SRS_CONF_DEFAULT_DVR_PATH;
    }
    
    SrsConfDirective* conf = dvr->get("dvr_path");
    
    if (!conf) {
        return SRS_CONF_DEFAULT_DVR_PATH;
    }
    
    return conf->arg0();
}

string SrsConfig::get_dvr_plan(string vhost)
{
    SrsConfDirective* dvr = get_dvr(vhost);
    
    if (!dvr) {
        return SRS_CONF_DEFAULT_DVR_PLAN;
    }
    
    SrsConfDirective* conf = dvr->get("dvr_plan");
    
    if (!conf) {
        return SRS_CONF_DEFAULT_DVR_PLAN;
    }
    
    return conf->arg0();
}

int SrsConfig::get_dvr_duration(string vhost)
{
    SrsConfDirective* dvr = get_dvr(vhost);
    
    if (!dvr) {
        return SRS_CONF_DEFAULT_DVR_DURATION;
    }
    
    SrsConfDirective* conf = dvr->get("dvr_duration");
    
    if (!conf) {
        return SRS_CONF_DEFAULT_DVR_DURATION;
    }
    
    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_dvr_time_jitter(string vhost)
{
    SrsConfDirective* dvr = get_dvr(vhost);
    
    std::string time_jitter = SRS_CONF_DEFAULT_TIME_JITTER;
    
    if (dvr) {
        SrsConfDirective* conf = dvr->get("time_jitter");
    
        if (conf) {
            time_jitter = conf->arg0();
        }
    }
    
    return _srs_time_jitter_string2int(time_jitter);
}

bool SrsConfig::get_http_api_enabled()
{
    SrsConfDirective* conf = get_http_api();
    return get_http_api_enabled(conf);
}

SrsConfDirective* SrsConfig::get_http_api()
{
    return root->get("http_api");
}

bool SrsConfig::get_http_api_enabled(SrsConfDirective* conf)
{
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
    
    if (!conf) {
        return SRS_CONF_DEFAULT_HTTP_API_PORT;
    }
    
    conf = conf->get("listen");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_HTTP_API_PORT;
    }

    return ::atoi(conf->arg0().c_str());
}

bool SrsConfig::get_http_stream_enabled()
{
    SrsConfDirective* conf = get_http_stream();
    return get_http_stream_enabled(conf);
}

SrsConfDirective* SrsConfig::get_http_stream()
{
    return root->get("http_stream");
}

bool SrsConfig::get_http_stream_enabled(SrsConfDirective* conf)
{
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
    
    if (!conf) {
        return SRS_CONF_DEFAULT_HTTP_STREAM_PORT;
    }
    
    conf = conf->get("listen");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_HTTP_STREAM_PORT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

string SrsConfig::get_http_stream_dir()
{
    SrsConfDirective* conf = get_http_stream();
    if (!conf) {
        return SRS_CONF_DEFAULT_HTTP_DIR;
    }
    
    conf = conf->get("dir");
    if (!conf) {
        return SRS_CONF_DEFAULT_HTTP_DIR;
    }
    
    if (conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_HTTP_DIR;
    }
    
    return conf->arg0();
}

bool SrsConfig::get_vhost_http_enabled(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return false;
    }
    
    conf = conf->get("http");
    if (!conf) {
        return false;
    }
    
    conf = conf->get("enabled");
    if (!conf) {
        return false;
    }
    
    if (conf->arg0() == "on") {
        return true;
    }
    
    return false;
}

string SrsConfig::get_vhost_http_mount(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return SRS_CONF_DEFAULT_HTTP_MOUNT;
    }
    
    conf = conf->get("http");
    if (!conf) {
        return SRS_CONF_DEFAULT_HTTP_MOUNT;
    }
    
    conf = conf->get("mount");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_HTTP_MOUNT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_vhost_http_dir(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return SRS_CONF_DEFAULT_HTTP_DIR;
    }
    
    conf = conf->get("http");
    if (!conf) {
        return SRS_CONF_DEFAULT_HTTP_DIR;
    }
    
    conf = conf->get("dir");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_HTTP_DIR;
    }
    
    return conf->arg0();
}

SrsConfDirective* SrsConfig::get_heartbeart()
{
    return root->get("heartbeat");
}

bool SrsConfig::get_heartbeat_enabled()
{
    SrsConfDirective* conf = get_heartbeart();
    
    if (!conf) {
        return SRS_CONF_DEFAULT_HTTP_HEAETBEAT_ENABLED;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0() != "on") {
        return SRS_CONF_DEFAULT_HTTP_HEAETBEAT_ENABLED;
    }
    
    return true;
}

int64_t SrsConfig::get_heartbeat_interval()
{
    SrsConfDirective* conf = get_heartbeart();
    
    if (!conf) {
        return (int64_t)(SRS_CONF_DEFAULT_HTTP_HEAETBEAT_INTERVAL * 1000);
    }
    conf = conf->get("interval");
    if (!conf || conf->arg0().empty()) {
        return (int64_t)(SRS_CONF_DEFAULT_HTTP_HEAETBEAT_INTERVAL * 1000);
    }
    
    return (int64_t)(::atof(conf->arg0().c_str()) * 1000);
}

string SrsConfig::get_heartbeat_url()
{
    SrsConfDirective* conf = get_heartbeart();
    
    if (!conf) {
        return SRS_CONF_DEFAULT_HTTP_HEAETBEAT_URL;
    }
    
    conf = conf->get("url");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_HTTP_HEAETBEAT_URL;
    }
    
    return conf->arg0();
}

string SrsConfig::get_heartbeat_device_id()
{
    SrsConfDirective* conf = get_heartbeart();
    
    if (!conf) {
        return "";
    }
    
    conf = conf->get("device_id");
    if (!conf || conf->arg0().empty()) {
        return "";
    }
    
    return conf->arg0();
}

int SrsConfig::get_heartbeat_device_index()
{
    SrsConfDirective* conf = get_heartbeart();
    
    if (!conf) {
        return SRS_CONF_DEFAULT_HTTP_HEAETBEAT_INDEX;
    }
    
    conf = conf->get("device_index");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_HTTP_HEAETBEAT_INDEX;
    }
    
    return ::atoi(conf->arg0().c_str());
}

bool SrsConfig::get_heartbeat_summaries()
{
    SrsConfDirective* conf = get_heartbeart();
    
    if (!conf) {
        return SRS_CONF_DEFAULT_HTTP_HEAETBEAT_SUMMARIES;
    }
    
    conf = conf->get("summaries");
    if (!conf || conf->arg0() != "on") {
        return SRS_CONF_DEFAULT_HTTP_HEAETBEAT_SUMMARIES;
    }
    
    return true;
}

namespace _srs_internal
{
    SrsConfigBuffer::SrsConfigBuffer()
    {
        line = 0;
    
        pos = last = start = NULL;
        end = start;
    }
    
    SrsConfigBuffer::~SrsConfigBuffer()
    {
        srs_freep(start);
    }
    
    int SrsConfigBuffer::fullfill(const char* filename)
    {
        int ret = ERROR_SUCCESS;
        
        SrsFileReader reader;
        
        // open file reader.
        if ((ret = reader.open(filename)) != ERROR_SUCCESS) {
            srs_error("open conf file error. ret=%d", ret);
            return ret;
        }
        
        // read all.
        int filesize = (int)reader.filesize();
        
        // create buffer
        srs_freep(start);
        pos = last = start = new char[filesize];
        end = start + filesize;
        
        // read total content from file.
        ssize_t nread = 0;
        if ((ret = reader.read(start, filesize, &nread)) != ERROR_SUCCESS) {
            srs_error("read file read error. expect %d, actual %d bytes, ret=%d", 
                filesize, nread, ret);
            return ret;
        }
        
        return ret;
    }
    
    bool SrsConfigBuffer::empty()
    {
        return pos >= end;
    }
};

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
