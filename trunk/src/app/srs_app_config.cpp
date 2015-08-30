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

#include <srs_app_config.hpp>

#include <unistd.h>
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
#include <srs_rtmp_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_source.hpp>
#include <srs_kernel_file.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_performance.hpp>
#include <srs_kernel_file.hpp>
#include <srs_rtmp_amf0.hpp>
#include <srs_app_statistic.hpp>

using namespace _srs_internal;

#define SRS_WIKI_URL_LOG "https://github.com/simple-rtmp-server/srs/wiki/v1_CN_SrsLog"

// when user config an invalid value, macros to perfer true or false.
#define SRS_CONF_PERFER_FALSE(conf_arg) conf_arg == "on"
#define SRS_CONF_PERFER_TRUE(conf_arg) conf_arg != "off"

// default config file.
#define SRS_CONF_DEFAULT_COFNIG_FILE "conf/srs.conf"

// '\n'
#define SRS_LF (char)SRS_CONSTS_LF

// '\r'
#define SRS_CR (char)SRS_CONSTS_CR

// dumps the engine to amf0 object.
int srs_config_dumps_engine(SrsConfDirective* dir, SrsAmf0Object* engine);

bool is_common_space(char ch)
{
    return (ch == ' ' || ch == '\t' || ch == SRS_CR || ch == SRS_LF);
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

SrsConfDirective* SrsConfDirective::get_or_create(string n)
{
    SrsConfDirective* conf = get(n);
    
    if (!conf) {
        conf = new SrsConfDirective();
        conf->name = n;
        directives.push_back(conf);
    }
    
    return conf;
}

SrsConfDirective* SrsConfDirective::get_or_create(string n, string a0)
{
    SrsConfDirective* conf = get_or_create(n);
    
    if (conf->arg0() == a0) {
        return conf;
    }
    
    // update a0.
    if (!conf->args.empty()) {
        conf->args.erase(conf->args.begin());
    }
    
    conf->args.insert(conf->args.begin(), a0);
    
    return conf;
}

bool SrsConfDirective::is_vhost()
{
    return name == "vhost";
}

bool SrsConfDirective::is_stream_caster()
{
    return name == "stream_caster";
}

int SrsConfDirective::parse(SrsConfigBuffer* buffer)
{
    return parse_conf(buffer, parse_file);
}

int SrsConfDirective::persistence(SrsFileWriter* writer, int level)
{
    int ret = ERROR_SUCCESS;
    
    static char SPACE = SRS_CONSTS_SP;
    static char SEMICOLON = SRS_CONSTS_SE;
    static char LF = SRS_CONSTS_LF;
    static char LB = SRS_CONSTS_LB;
    static char RB = SRS_CONSTS_RB;
    static const char* INDENT = "    ";
    
    // for level0 directive, only contains sub directives.
    if (level > 0) {
        // indent by (level - 1) * 4 space.
        for (int i = 0; i < level - 1; i++) {
            if ((ret = writer->write((char*)INDENT, 4, NULL)) != ERROR_SUCCESS) {
                return ret;
            }
        }
        
        // directive name.
        if ((ret = writer->write((char*)name.c_str(), (int)name.length(), NULL)) != ERROR_SUCCESS) {
            return ret;
        }
        if (!args.empty() && (ret = writer->write((char*)&SPACE, 1, NULL)) != ERROR_SUCCESS) {
            return ret;
        }
        
        // directive args.
        for (int i = 0; i < (int)args.size(); i++) {
            std::string& arg = args.at(i);
            if ((ret = writer->write((char*)arg.c_str(), (int)arg.length(), NULL)) != ERROR_SUCCESS) {
                return ret;
            }
            if (i < (int)args.size() - 1 && (ret = writer->write((char*)&SPACE, 1, NULL)) != ERROR_SUCCESS) {
                return ret;
            }
        }
        
        // native directive, without sub directives.
        if (directives.empty()) {
            if ((ret = writer->write((char*)&SEMICOLON, 1, NULL)) != ERROR_SUCCESS) {
                return ret;
            }
        }
    }
    
    // persistence all sub directives.
    if (level > 0) {
        if (!directives.empty()) {
            if ((ret = writer->write((char*)&SPACE, 1, NULL)) != ERROR_SUCCESS) {
                return ret;
            }
            if ((ret = writer->write((char*)&LB, 1, NULL)) != ERROR_SUCCESS) {
                return ret;
            }
        }
        
        if ((ret = writer->write((char*)&LF, 1, NULL)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    for (int i = 0; i < (int)directives.size(); i++) {
        SrsConfDirective* dir = directives.at(i);
        if ((ret = dir->persistence(writer, level + 1)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    if (level > 0 && !directives.empty()) {
        // indent by (level - 1) * 4 space.
        for (int i = 0; i < level - 1; i++) {
            if ((ret = writer->write((char*)INDENT, 4, NULL)) != ERROR_SUCCESS) {
                return ret;
            }
        }
        
        if ((ret = writer->write((char*)&RB, 1, NULL)) != ERROR_SUCCESS) {
            return ret;
        }
        
        if ((ret = writer->write((char*)&LF, 1, NULL)) != ERROR_SUCCESS) {
            return ret;
        }
    }
    
    
    return ret;
}

SrsAmf0StrictArray* SrsConfDirective::dumps_args()
{
    SrsAmf0StrictArray* arr = SrsAmf0Any::strict_array();
    for (int i = 0; i < (int)args.size(); i++) {
        string arg = args.at(i);
        arr->append(SrsAmf0Any::str(arg.c_str()));
    }
    return arr;
}

SrsAmf0Any* SrsConfDirective::dumps_arg0_to_str()
{
    return SrsAmf0Any::str(arg0().c_str());
}

SrsAmf0Any* SrsConfDirective::dumps_arg0_to_number()
{
    return SrsAmf0Any::number(::atof(arg0().c_str()));
}

SrsAmf0Any* SrsConfDirective::dumps_arg0_to_boolean()
{
    return SrsAmf0Any::boolean(arg0() == "on");
}

// see: ngx_conf_parse
int SrsConfDirective::parse_conf(SrsConfigBuffer* buffer, SrsDirectiveType type)
{
    int ret = ERROR_SUCCESS;
    
    while (true) {
        std::vector<string> args;
        int line_start = 0;
        ret = read_token(buffer, args, line_start);
        
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
                srs_error("line %d: unexpected \"}\", ret=%d", buffer->line, ret);
                return ret;
            }
            return ERROR_SUCCESS;
        }
        if (ret == ERROR_SYSTEM_CONFIG_EOF) {
            if (type == parse_block) {
                srs_error("line %d: unexpected end of file, expecting \"}\", ret=%d", conf_line, ret);
                return ret;
            }
            return ERROR_SUCCESS;
        }
        
        if (args.empty()) {
            ret = ERROR_SYSTEM_CONFIG_INVALID;
            srs_error("line %d: empty directive. ret=%d", conf_line, ret);
            return ret;
        }
        
        // build directive tree.
        SrsConfDirective* directive = new SrsConfDirective();

        directive->conf_line = line_start;
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
int SrsConfDirective::read_token(SrsConfigBuffer* buffer, vector<string>& args, int& line_start)
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
        
        if (ch == SRS_LF) {
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
            if (line_start == 0) {
                line_start = buffer->line;
            }
            
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
                int len = (int)(buffer->pos - pstart);
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
    dolphin = false;
    
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

bool SrsConfig::is_dolphin()
{
    return dolphin;
}

void SrsConfig::set_config_directive(SrsConfDirective* parent, string dir, string value)
{
    SrsConfDirective* d = parent->get(dir);
    
    if (!d) {
        d = new SrsConfDirective();
        if (!dir.empty()) {
            d->name = dir;
        }
        parent->directives.push_back(d);
    }
    
    d->args.clear();
    if (!value.empty()) {
        d->args.push_back(value);
    }
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
    
    // transform config to compatible with previous style of config.
    if ((ret = srs_config_transform_vhost(conf.root)) != ERROR_SUCCESS) {
        srs_error("transform config failed. ret=%d", ret);
        return ret;
    }

    if ((ret = conf.check_config()) != ERROR_SUCCESS) {
        srs_error("ignore config reloader check config failed. ret=%d", ret);
        ret = ERROR_SUCCESS;
        return ret;
    }
    
    return reload_conf(&conf);
}

int SrsConfig::reload_vhost(SrsConfDirective* old_root)
{
    int ret = ERROR_SUCCESS;
    
    // merge config.
    std::vector<ISrsReloadHandler*>::iterator it;
    
    // following directly support reload.
    //      origin, token_traverse, vhost, debug_srs_upnode
    
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
        
        // cluster.mode, never supports reload.
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
            
            // chunk_size, only one per vhost.
            if (!srs_directive_equals(new_vhost->get("chunk_size"), old_vhost->get("chunk_size"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((ret = subscribe->on_reload_vhost_chunk_size(vhost)) != ERROR_SUCCESS) {
                        srs_error("vhost %s notify subscribes chunk_size failed. ret=%d", vhost.c_str(), ret);
                        return ret;
                    }
                }
                srs_trace("vhost %s reload chunk_size success.", vhost.c_str());
            }
            
            // tcp_nodelay, only one per vhost
            if (!srs_directive_equals(new_vhost->get("tcp_nodelay"), old_vhost->get("tcp_nodelay"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((ret = subscribe->on_reload_vhost_tcp_nodelay(vhost)) != ERROR_SUCCESS) {
                        srs_error("vhost %s notify subscribes tcp_nodelay failed. ret=%d", vhost.c_str(), ret);
                        return ret;
                    }
                }
                srs_trace("vhost %s reload tcp_nodelay success.", vhost.c_str());
            }
            
            // min_latency, only one per vhost
            if (!srs_directive_equals(new_vhost->get("min_latency"), old_vhost->get("min_latency"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((ret = subscribe->on_reload_vhost_realtime(vhost)) != ERROR_SUCCESS) {
                        srs_error("vhost %s notify subscribes min_latency failed. ret=%d", vhost.c_str(), ret);
                        return ret;
                    }
                }
                srs_trace("vhost %s reload min_latency success.", vhost.c_str());
            }
            
            // play, only one per vhost
            if (!srs_directive_equals(new_vhost->get("play"), old_vhost->get("play"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((ret = subscribe->on_reload_vhost_play(vhost)) != ERROR_SUCCESS) {
                        srs_error("vhost %s notify subscribes play failed. ret=%d", vhost.c_str(), ret);
                        return ret;
                    }
                }
                srs_trace("vhost %s reload play success.", vhost.c_str());
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
            // @remark, the hls_on_error directly support reload.
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

            // hds reload
            if (!srs_directive_equals(new_vhost->get("hds"), old_vhost->get("hds"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((ret = subscribe->on_reload_vhost_hds(vhost)) != ERROR_SUCCESS) {
                        srs_error("vhost %s notify subscribes hds failed. ret=%d", vhost.c_str(), ret);
                        return ret;
                    }
                }
                srs_trace("vhost %s reload hds success.", vhost.c_str());
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
                srs_trace("vhost %s reload dvr success.", vhost.c_str());
            }
            
            // exec, only one per vhost
            if (!srs_directive_equals(new_vhost->get("exec"), old_vhost->get("exec"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((ret = subscribe->on_reload_vhost_exec(vhost)) != ERROR_SUCCESS) {
                        srs_error("vhost %s notify subscribes exec failed. ret=%d", vhost.c_str(), ret);
                        return ret;
                    }
                }
                srs_trace("vhost %s reload exec success.", vhost.c_str());
            }
            
            // publish, only one per vhost
            if (!srs_directive_equals(new_vhost->get("publish"), old_vhost->get("publish"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((ret = subscribe->on_reload_vhost_publish(vhost)) != ERROR_SUCCESS) {
                        srs_error("vhost %s notify subscribes publish failed. ret=%d", vhost.c_str(), ret);
                        return ret;
                    }
                }
                srs_trace("vhost %s reload publish success.", vhost.c_str());
            }
            
            // http_static, only one per vhost.
            if (!srs_directive_equals(new_vhost->get("http_static"), old_vhost->get("http_static"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((ret = subscribe->on_reload_vhost_http_updated()) != ERROR_SUCCESS) {
                        srs_error("vhost %s notify subscribes http_static failed. ret=%d", vhost.c_str(), ret);
                        return ret;
                    }
                }
                srs_trace("vhost %s reload http_static success.", vhost.c_str());
            }
            
            // http_remux, only one per vhost.
            if (!srs_directive_equals(new_vhost->get("http_remux"), old_vhost->get("http_remux"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((ret = subscribe->on_reload_vhost_http_remux_updated(vhost)) != ERROR_SUCCESS) {
                        srs_error("vhost %s notify subscribes http_remux failed. ret=%d", vhost.c_str(), ret);
                        return ret;
                    }
                }
                srs_trace("vhost %s reload http_remux success.", vhost.c_str());
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
        srs_trace("ignore reload vhost, enabled old: %d, new: %d",
            get_vhost_enabled(old_vhost), get_vhost_enabled(new_vhost));
    }
    
    return ret;
}

int SrsConfig::reload_conf(SrsConfig* conf)
{
    int ret = ERROR_SUCCESS;
    
    SrsConfDirective* old_root = root;
    SrsAutoFree(SrsConfDirective, old_root);
    
    root = conf->root;
    conf->root = NULL;
    
    // merge config.
    std::vector<ISrsReloadHandler*>::iterator it;

    // never support reload:
    //      daemon
    //
    // always support reload without additional code:
    //      chunk_size, ff_log_dir,
    //      bandcheck, http_hooks, heartbeat,
    //      security
    
    // merge config: max_connections
    if (!srs_directive_equals(root->get("max_connections"), old_root->get("max_connections"))) {
        for (it = subscribes.begin(); it != subscribes.end(); ++it) {
            ISrsReloadHandler* subscribe = *it;
            if ((ret = subscribe->on_reload_max_conns()) != ERROR_SUCCESS) {
                srs_error("notify subscribes reload max_connections failed. ret=%d", ret);
                return ret;
            }
        }
        srs_trace("reload max_connections success.");
    }

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
    
    // merge config: pithy_print_ms
    if (!srs_directive_equals(root->get("pithy_print_ms"), old_root->get("pithy_print_ms"))) {
        for (it = subscribes.begin(); it != subscribes.end(); ++it) {
            ISrsReloadHandler* subscribe = *it;
            if ((ret = subscribe->on_reload_pithy_print()) != ERROR_SUCCESS) {
                srs_error("notify subscribes pithy_print_ms listen failed. ret=%d", ret);
                return ret;
            }
        }
        srs_trace("reload pithy_print_ms success.");
    }
    
    // merge config: http_api
    if ((ret = reload_http_api(old_root)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // merge config: http_stream
    if ((ret = reload_http_stream(old_root)) != ERROR_SUCCESS) {
        return ret;
    }

    // TODO: FIXME: support reload stream_caster.

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
        
        if (!srs_directive_equals(old_http_api->get("crossdomain"), new_http_api->get("crossdomain"))) {
            for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                ISrsReloadHandler* subscribe = *it;
                if ((ret = subscribe->on_reload_http_api_crossdomain()) != ERROR_SUCCESS) {
                    srs_error("notify subscribes http_api crossdomain modified failed. ret=%d", ret);
                    return ret;
                }
            }
        }
        srs_trace("reload crossdomain modified http_api success.");
        
        if (!srs_directive_equals(old_http_api->get("raw_api"), new_http_api->get("raw_api"))) {
            for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                ISrsReloadHandler* subscribe = *it;
                if ((ret = subscribe->on_reload_http_api_raw_api()) != ERROR_SUCCESS) {
                    srs_error("notify subscribes http_api raw_api modified failed. ret=%d", ret);
                    return ret;
                }
            }
        }
        srs_trace("reload raw_api modified http_api success.");
        
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
    
    SrsConfDirective* new_http_stream = root->get("http_server");
    SrsConfDirective* old_http_stream = old_root->get("http_server");

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
        SrsConfDirective* new_ingester = new_vhost->get("ingest", ingest_id);
        
        // ENABLED => DISABLED
        if (get_ingest_enabled(old_ingester) && !get_ingest_enabled(new_ingester)) {
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
    }
    
    // for added ingesters, start them.
    for (int i = 0; i < (int)new_ingesters.size(); i++) {
        SrsConfDirective* new_ingester = new_ingesters.at(i);
        std::string ingest_id = new_ingester->arg0();
        SrsConfDirective* old_ingester = old_vhost->get("ingest", ingest_id);
        
        // DISABLED => ENABLED
        if (!get_ingest_enabled(old_ingester) && get_ingest_enabled(new_ingester)) {
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
    }

    // for updated ingesters, restart them.
    for (int i = 0; i < (int)new_ingesters.size(); i++) {
        SrsConfDirective* new_ingester = new_ingesters.at(i);
        std::string ingest_id = new_ingester->arg0();
        SrsConfDirective* old_ingester = old_vhost->get("ingest", ingest_id);
        
        // ENABLED => ENABLED
        if (get_ingest_enabled(old_ingester) && get_ingest_enabled(new_ingester)) {
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
    
    // transform config to compatible with previous style of config.
    if ((ret = srs_config_transform_vhost(root)) != ERROR_SUCCESS) {
        srs_error("transform config failed. ret=%d", ret);
        return ret;
    }
    
    if (test_conf) {
        // the parse_file never check the config,
        // we check it when user requires check config file.
        if (ret == ERROR_SUCCESS) {
            ret = check_config();
        }

        if (ret == ERROR_SUCCESS) {
            srs_trace("config file is ok");
            exit(0);
        } else {
            srs_error("config file is invalid");
            exit(ret);
        }
    }

    ////////////////////////////////////////////////////////////////////////
    // check log name and level
    ////////////////////////////////////////////////////////////////////////
    if (true) {
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
    }
    
    return ret;
}

int SrsConfig::persistence()
{
    int ret = ERROR_SUCCESS;
    
    // write to a tmp file, then mv to the config.
    std::string path = config_file + ".tmp";
    
    // open the tmp file for persistence
    SrsFileWriter fw;
    if ((ret = fw.open(path)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // persistence root directive to writer.
    if ((ret = root->persistence(&fw, 0)) != ERROR_SUCCESS) {
        ::unlink(path.c_str());
        return ret;
    }
    
    // rename the config file.
    if (::rename(path.c_str(), config_file.c_str()) < 0) {
        ::unlink(path.c_str());
        
        ret = ERROR_SYSTEM_CONFIG_PERSISTENCE;
        srs_error("rename config from %s to %s failed. ret=%d", path.c_str(), config_file.c_str(), ret);
        return ret;
    }
    
    return ret;
}

int SrsConfig::global_to_json(SrsAmf0Object* obj)
{
    int ret = ERROR_SUCCESS;
    
    for (int i = 0; i < (int)root->directives.size(); i++) {
        SrsConfDirective* dir = root->directives.at(i);
        if (dir->is_vhost()) {
            continue;
        }
        
        if (dir->name == "listen") {
            obj->set(dir->name, dir->dumps_args());
        } else if (dir->name == "pid") {
            obj->set(dir->name, dir->dumps_arg0_to_str());
        } else if (dir->name == "chunk_size") {
            obj->set(dir->name, dir->dumps_arg0_to_number());
        } else if (dir->name == "ff_log_dir") {
            obj->set(dir->name, dir->dumps_arg0_to_str());
        } else if (dir->name == "srs_log_tank") {
            obj->set(dir->name, dir->dumps_arg0_to_str());
        } else if (dir->name == "srs_log_level") {
            obj->set(dir->name, dir->dumps_arg0_to_str());
        } else if (dir->name == "srs_log_file") {
            obj->set(dir->name, dir->dumps_arg0_to_str());
        } else if (dir->name == "max_connections") {
            obj->set(dir->name, dir->dumps_arg0_to_number());
        } else if (dir->name == "daemon") {
            obj->set(dir->name, dir->dumps_arg0_to_boolean());
        } else if (dir->name == "utc_time") {
            obj->set(dir->name, dir->dumps_arg0_to_boolean());
        } else if (dir->name == "pithy_print_ms") {
            obj->set(dir->name, dir->dumps_arg0_to_number());
        } else if (dir->name == "heartbeat") {
            SrsAmf0Object* sobj = SrsAmf0Any::object();
            for (int j = 0; j < (int)dir->directives.size(); j++) {
                SrsConfDirective* sdir = dir->directives.at(j);
                if (sdir->name == "enabled") {
                    sobj->set(sdir->name, sdir->dumps_arg0_to_boolean());
                } else if (sdir->name == "interval") {
                    sobj->set(sdir->name, sdir->dumps_arg0_to_number());
                } else if (sdir->name == "url") {
                    sobj->set(sdir->name, sdir->dumps_arg0_to_str());
                } else if (sdir->name == "device_id") {
                    sobj->set(sdir->name, sdir->dumps_arg0_to_str());
                } else if (sdir->name == "summaries") {
                    sobj->set(sdir->name, sdir->dumps_arg0_to_boolean());
                }
            }
            obj->set(dir->name, sobj);
        } else if (dir->name == "stats") {
            SrsAmf0Object* sobj = SrsAmf0Any::object();
            for (int j = 0; j < (int)dir->directives.size(); j++) {
                SrsConfDirective* sdir = dir->directives.at(j);
                if (sdir->name == "network") {
                    sobj->set(sdir->name, sdir->dumps_arg0_to_number());
                } else if (sdir->name == "disk") {
                    sobj->set(sdir->name, sdir->dumps_args());
                }
            }
            obj->set(dir->name, sobj);
        } else if (dir->name == "http_api") {
            SrsAmf0Object* sobj = SrsAmf0Any::object();
            for (int j = 0; j < (int)dir->directives.size(); j++) {
                SrsConfDirective* sdir = dir->directives.at(j);
                if (sdir->name == "enabled") {
                    sobj->set(sdir->name, sdir->dumps_arg0_to_boolean());
                } else if (sdir->name == "listen") {
                    sobj->set(sdir->name, sdir->dumps_arg0_to_str());
                } else if (sdir->name == "crossdomain") {
                    sobj->set(sdir->name, sdir->dumps_arg0_to_boolean());
                } else if (sdir->name == "raw_api") {
                    SrsAmf0Object* ssobj = SrsAmf0Any::object();
                    sobj->set(sdir->name, ssobj);
                    
                    for (int j = 0; j < (int)sdir->directives.size(); j++) {
                        SrsConfDirective* ssdir = sdir->directives.at(j);
                        if (ssdir->name == "enabled") {
                            ssobj->set(ssdir->name, ssdir->dumps_arg0_to_boolean());
                        } else if (ssdir->name == "allow_reload") {
                            ssobj->set(ssdir->name, ssdir->dumps_arg0_to_boolean());
                        } else if (ssdir->name == "allow_query") {
                            ssobj->set(ssdir->name, ssdir->dumps_arg0_to_boolean());
                        } else if (ssdir->name == "allow_update") {
                            ssobj->set(ssdir->name, ssdir->dumps_arg0_to_boolean());
                        }
                    }
                }
            }
            obj->set(dir->name, sobj);
        } else if (dir->name == "http_server") {
            SrsAmf0Object* sobj = SrsAmf0Any::object();
            for (int j = 0; j < (int)dir->directives.size(); j++) {
                SrsConfDirective* sdir = dir->directives.at(j);
                if (sdir->name == "enabled") {
                    sobj->set(sdir->name, sdir->dumps_arg0_to_boolean());
                } else if (sdir->name == "listen") {
                    sobj->set(sdir->name, sdir->dumps_arg0_to_str());
                } else if (sdir->name == "dir") {
                    sobj->set(sdir->name, sdir->dumps_arg0_to_str());
                }
            }
            obj->set(dir->name, sobj);
        } else if (dir->name == "stream_caster") {
            SrsAmf0Object* sobj = SrsAmf0Any::object();
            for (int j = 0; j < (int)dir->directives.size(); j++) {
                SrsConfDirective* sdir = dir->directives.at(j);
                if (sdir->name == "enabled") {
                    sobj->set(sdir->name, sdir->dumps_arg0_to_boolean());
                } else if (sdir->name == "caster") {
                    sobj->set(sdir->name, sdir->dumps_arg0_to_str());
                } else if (sdir->name == "output") {
                    sobj->set(sdir->name, sdir->dumps_arg0_to_str());
                } else if (sdir->name == "listen") {
                    sobj->set(sdir->name, sdir->dumps_arg0_to_str());
                } else if (sdir->name == "rtp_port_min") {
                    sobj->set(sdir->name, sdir->dumps_arg0_to_number());
                } else if (sdir->name == "rtp_port_max") {
                    sobj->set(sdir->name, sdir->dumps_arg0_to_number());
                }
            }
            obj->set(dir->name, sobj);
        } else {
            continue;
        }
    }
    
    SrsAmf0Object* sobjs = SrsAmf0Any::object();
    int nb_vhosts = 0;
    
    SrsStatistic* stat = SrsStatistic::instance();
    for (int i = 0; i < (int)root->directives.size(); i++) {
        SrsConfDirective* dir = root->directives.at(i);
        if (!dir->is_vhost()) {
            continue;
        }
        
        nb_vhosts++;
        SrsAmf0Object* sobj = SrsAmf0Any::object();
        sobjs->set(dir->arg0(), sobj);
        
        SrsStatisticVhost* svhost = stat->find_vhost(dir->arg0());
        sobj->set("id", SrsAmf0Any::number(svhost? (double)svhost->id : 0));
        sobj->set("name", dir->dumps_arg0_to_str());
        
        if (get_vhost_enabled(dir->name)) {
            sobj->set("enabled", SrsAmf0Any::boolean(true));
        }
        if (get_dvr_enabled(dir->name)) {
            sobj->set("dvr", SrsAmf0Any::boolean(true));
        }
        if (get_vhost_http_enabled(dir->name)) {
            sobj->set("http_static", SrsAmf0Any::boolean(true));
        }
        if (get_vhost_http_remux_enabled(dir->name)) {
            sobj->set("http_remux", SrsAmf0Any::boolean(true));
        }
        if (get_hls_enabled(dir->name)) {
            sobj->set("hls", SrsAmf0Any::boolean(true));
        }
        if (get_hds_enabled(dir->name)) {
            sobj->set("hds", SrsAmf0Any::boolean(true));
        }
        if (get_vhost_http_hooks(dir->name)) {
            sobj->set("http_hooks", SrsAmf0Any::boolean(true));
        }
        if (get_exec_enabled(dir->name)) {
            sobj->set("exec", SrsAmf0Any::boolean(true));
        }
        if (get_bw_check_enabled(dir->name)) {
            sobj->set("bandcheck", SrsAmf0Any::boolean(true));
        }
        if (!get_vhost_is_edge(dir->name)) {
            sobj->set("origin", SrsAmf0Any::boolean(true));
        }
        if (get_forward_enabled(dir->name)) {
            sobj->set("forward", SrsAmf0Any::boolean(true));
        }
        
        if (get_security_enabled(dir->name)) {
            sobj->set("security", SrsAmf0Any::boolean(true));
        }
        if (get_refer_enabled(dir->name)) {
            sobj->set("refer", SrsAmf0Any::boolean(true));
        }
        
        if (get_mr_enabled(dir->name)) {
            sobj->set("mr", SrsAmf0Any::boolean(true));
        }
        if (get_realtime_enabled(dir->name)) {
            sobj->set("min_latency", SrsAmf0Any::boolean(true));
        }
        if (get_gop_cache(dir->name)) {
            sobj->set("gop_cache", SrsAmf0Any::boolean(true));
        }
        if (get_tcp_nodelay(dir->name)) {
            sobj->set("tcp_nodelay", SrsAmf0Any::boolean(true));
        }
        
        if (get_mix_correct(dir->name)) {
            sobj->set("mix_correct", SrsAmf0Any::boolean(true));
        }
        if (get_time_jitter(dir->name) != SrsRtmpJitterAlgorithmOFF) {
            sobj->set("time_jitter", SrsAmf0Any::boolean(true));
        }
        if (get_atc(dir->name)) {
            sobj->set("atc", SrsAmf0Any::boolean(true));
        }
        
        bool has_transcode = false;
        for (int j = 0; !has_transcode && j < (int)dir->directives.size(); j++) {
            SrsConfDirective* sdir = dir->directives.at(j);
            if (sdir->name != "transcode") {
                continue;
            }
            
            if (!get_transcode_enabled(sdir)) {
                continue;
            }
            
            for (int k = 0; !has_transcode && k < (int)sdir->directives.size(); k++) {
                SrsConfDirective* ssdir = sdir->directives.at(k);
                if (ssdir->name != "engine") {
                    continue;
                }
                
                if (get_engine_enabled(ssdir)) {
                    has_transcode = true;
                    break;
                }
            }
        }
        if (has_transcode) {
            sobj->set("transcode", SrsAmf0Any::boolean(has_transcode));
        }
        
        bool has_ingest = false;
        for (int j = 0; !has_ingest && j < (int)dir->directives.size(); j++) {
            SrsConfDirective* sdir = dir->directives.at(j);
            if (sdir->name != "ingest") {
                continue;
            }
            
            if (get_ingest_enabled(sdir)) {
                has_ingest = true;
                break;
            }
        }
        if (has_ingest) {
            sobj->set("ingest", SrsAmf0Any::boolean(has_ingest));
        }
    }
    
    obj->set("nb_vhosts", SrsAmf0Any::number(nb_vhosts));
    obj->set("vhosts", sobjs);
    
    return ret;
}

int SrsConfig::vhost_to_json(SrsConfDirective* vhost, SrsAmf0Object* obj)
{
    int ret = ERROR_SUCCESS;
    
    SrsConfDirective* dir = NULL;
    
    // always present in vhost.
    SrsStatistic* stat = SrsStatistic::instance();
    
    SrsStatisticVhost* svhost = stat->find_vhost(vhost->arg0());
    obj->set("id", SrsAmf0Any::number(svhost? (double)svhost->id : 0));
    
    obj->set("name", vhost->dumps_arg0_to_str());
    obj->set("enabled", SrsAmf0Any::boolean(get_vhost_enabled(vhost)));
    
    // vhost scope configs.
    if ((dir = vhost->get("chunk_size")) != NULL) {
        obj->set("chunk_size", dir->dumps_arg0_to_number());
    }
    if ((dir = vhost->get("min_latency")) != NULL) {
        obj->set("min_latency", dir->dumps_arg0_to_boolean());
    }
    if ((dir = vhost->get("tcp_nodelay")) != NULL) {
        obj->set("tcp_nodelay", dir->dumps_arg0_to_boolean());
    }
    
    // cluster.
    if ((dir = vhost->get("cluster")) != NULL) {
        SrsAmf0Object* cluster = SrsAmf0Any::object();
        obj->set("cluster", cluster);
        
        for (int i = 0; i < (int)dir->directives.size(); i++) {
            SrsConfDirective* sdir = dir->directives.at(i);
            
            if (sdir->name == "mode") {
                cluster->set("mode", sdir->dumps_arg0_to_str());
            } else if (sdir->name == "origin") {
                cluster->set("origin", sdir->dumps_arg0_to_str());
            } else if (sdir->name == "token_traverse") {
                cluster->set("token_traverse", sdir->dumps_arg0_to_boolean());
            } else if (sdir->name == "vhost") {
                cluster->set("vhost", sdir->dumps_arg0_to_str());
            } else if (sdir->name == "debug_srs_upnode") {
                cluster->set("debug_srs_upnode", sdir->dumps_arg0_to_boolean());
            }
        }
    }
    
    // forward
    if ((dir = vhost->get("forward")) != NULL) {
        SrsAmf0Object* forward = SrsAmf0Any::object();
        obj->set("forward", forward);
        
        forward->set("enabled", SrsAmf0Any::boolean(get_forward_enabled(vhost->name)));
        
        for (int i = 0; i < (int)dir->directives.size(); i++) {
            SrsConfDirective* sdir = dir->directives.at(i);
            
            if (sdir->name == "destination") {
                forward->set("destination", sdir->dumps_args());
            }
        }
    }
    
    // play
    if ((dir = vhost->get("play")) != NULL) {
        SrsAmf0Object* play = SrsAmf0Any::object();
        obj->set("play", play);
        
        for (int i = 0; i < (int)dir->directives.size(); i++) {
            SrsConfDirective* sdir = dir->directives.at(i);
            
            if (sdir->name == "time_jitter") {
                play->set("time_jitter", sdir->dumps_arg0_to_str());
            } else if (sdir->name == "mix_correct") {
                play->set("mix_correct", sdir->dumps_arg0_to_boolean());
            } else if (sdir->name == "atc") {
                play->set("atc", sdir->dumps_arg0_to_boolean());
            } else if (sdir->name == "atc_auto") {
                play->set("atc_auto", sdir->dumps_arg0_to_boolean());
            } else if (sdir->name == "mw_latency") {
                play->set("mw_latency", sdir->dumps_arg0_to_number());
            } else if (sdir->name == "gop_cache") {
                play->set("gop_cache", sdir->dumps_arg0_to_boolean());
            } else if (sdir->name == "queue_length") {
                play->set("queue_length", sdir->dumps_arg0_to_number());
            } else if (sdir->name == "reduce_sequence_header") {
                play->set("reduce_sequence_header", sdir->dumps_arg0_to_boolean());
            } else if (sdir->name == "send_min_interval") {
                play->set("send_min_interval", sdir->dumps_arg0_to_number());
            }
        }
    }
    
    // publish
    if ((dir = vhost->get("publish")) != NULL) {
        SrsAmf0Object* publish = SrsAmf0Any::object();
        obj->set("publish", publish);
        
        for (int i = 0; i < (int)dir->directives.size(); i++) {
            SrsConfDirective* sdir = dir->directives.at(i);
            
            if (sdir->name == "mr") {
                publish->set("mr", sdir->dumps_arg0_to_boolean());
            } else if (sdir->name == "mr_latency") {
                publish->set("mr_latency", sdir->dumps_arg0_to_number());
            } else if (sdir->name == "firstpkt_timeout") {
                publish->set("firstpkt_timeout", sdir->dumps_arg0_to_number());
            } else if (sdir->name == "normal_timeout") {
                publish->set("normal_timeout", sdir->dumps_arg0_to_number());
            }
        }
    }
    
    // refer
    if ((dir = vhost->get("refer")) != NULL) {
        SrsAmf0Object* refer = SrsAmf0Any::object();
        obj->set("refer", refer);
        
        refer->set("enabled", SrsAmf0Any::boolean(get_refer_enabled(vhost->name)));
        
        for (int i = 0; i < (int)dir->directives.size(); i++) {
            SrsConfDirective* sdir = dir->directives.at(i);
            
            if (sdir->name == "all") {
                refer->set("all", sdir->dumps_args());
            } else if (sdir->name == "publish") {
                refer->set("publish", sdir->dumps_args());
            } else if (sdir->name == "play") {
                refer->set("play", sdir->dumps_args());
            }
        }
    }
    
    // bandcheck
    if ((dir = vhost->get("bandcheck")) != NULL) {
        SrsAmf0Object* bandcheck = SrsAmf0Any::object();
        obj->set("bandcheck", bandcheck);
        
        bandcheck->set("enabled", SrsAmf0Any::boolean(get_bw_check_enabled(vhost->name)));
        
        for (int i = 0; i < (int)dir->directives.size(); i++) {
            SrsConfDirective* sdir = dir->directives.at(i);
            
            if (sdir->name == "key") {
                bandcheck->set("key", sdir->dumps_arg0_to_str());
            } else if (sdir->name == "interval") {
                bandcheck->set("interval", sdir->dumps_arg0_to_number());
            } else if (sdir->name == "limit_kbps") {
                bandcheck->set("limit_kbps", sdir->dumps_arg0_to_number());
            }
        }
    }
    
    // security
    if ((dir = vhost->get("security")) != NULL) {
        SrsAmf0Object* security = SrsAmf0Any::object();
        obj->set("security", security);
        
        security->set("enabled", SrsAmf0Any::boolean(get_security_enabled(vhost->name)));
        
        SrsAmf0StrictArray* allows = SrsAmf0Any::strict_array();
        security->set("allows", allows);
        
        SrsAmf0StrictArray* denies = SrsAmf0Any::strict_array();
        security->set("denies", denies);
        
        for (int i = 0; i < (int)dir->directives.size(); i++) {
            SrsConfDirective* sdir = dir->directives.at(i);
            
            if (sdir->name == "allow") {
                SrsAmf0Object* allow = SrsAmf0Any::object();
                allow->set("action", SrsAmf0Any::str(sdir->name.c_str()));
                allow->set("method", SrsAmf0Any::str(sdir->arg0().c_str()));
                allow->set("entry", SrsAmf0Any::str(sdir->arg1().c_str()));
                allows->append(allow);
            } else if (sdir->name == "deny") {
                SrsAmf0Object* deny = SrsAmf0Any::object();
                deny->set("action", SrsAmf0Any::str(sdir->name.c_str()));
                deny->set("method", SrsAmf0Any::str(sdir->arg0().c_str()));
                deny->set("entry", SrsAmf0Any::str(sdir->arg1().c_str()));
                denies->append(deny);
            }
        }
    }
    
    // http_static
    if ((dir = vhost->get("http_static")) != NULL) {
        SrsAmf0Object* http_static = SrsAmf0Any::object();
        obj->set("http_static", http_static);
        
        http_static->set("enabled", SrsAmf0Any::boolean(get_vhost_http_enabled(vhost->name)));
        
        for (int i = 0; i < (int)dir->directives.size(); i++) {
            SrsConfDirective* sdir = dir->directives.at(i);
            
            if (sdir->name == "mount") {
                http_static->set("mount", sdir->dumps_arg0_to_str());
            } else if (sdir->name == "dir") {
                http_static->set("dir", sdir->dumps_arg0_to_str());
            }
        }
    }
    
    // http_remux
    if ((dir = vhost->get("http_remux")) != NULL) {
        SrsAmf0Object* http_remux = SrsAmf0Any::object();
        obj->set("http_remux", http_remux);
        
        http_remux->set("enabled", SrsAmf0Any::boolean(get_vhost_http_remux_enabled(vhost->name)));
        
        for (int i = 0; i < (int)dir->directives.size(); i++) {
            SrsConfDirective* sdir = dir->directives.at(i);
            
            if (sdir->name == "fast_cache") {
                http_remux->set("fast_cache", sdir->dumps_arg0_to_number());
            } else if (sdir->name == "mount") {
                http_remux->set("mount", sdir->dumps_arg0_to_str());
            } else if (sdir->name == "hstrs") {
                http_remux->set("hstrs", sdir->dumps_arg0_to_boolean());
            }
        }
    }
    
    // http_hooks
    if ((dir = vhost->get("http_hooks")) != NULL) {
        SrsAmf0Object* http_hooks = SrsAmf0Any::object();
        obj->set("http_hooks", http_hooks);
        
        http_hooks->set("enabled", SrsAmf0Any::boolean(get_vhost_http_hooks_enabled(vhost->name)));
        
        for (int i = 0; i < (int)dir->directives.size(); i++) {
            SrsConfDirective* sdir = dir->directives.at(i);
            
            if (sdir->name == "on_connect") {
                http_hooks->set("on_connect", sdir->dumps_args());
            } else if (sdir->name == "on_close") {
                http_hooks->set("on_close", sdir->dumps_args());
            } else if (sdir->name == "on_publish") {
                http_hooks->set("on_publish", sdir->dumps_args());
            } else if (sdir->name == "on_unpublish") {
                http_hooks->set("on_unpublish", sdir->dumps_args());
            } else if (sdir->name == "on_play") {
                http_hooks->set("on_play", sdir->dumps_args());
            } else if (sdir->name == "on_stop") {
                http_hooks->set("on_stop", sdir->dumps_args());
            } else if (sdir->name == "on_dvr") {
                http_hooks->set("on_dvr", sdir->dumps_args());
            } else if (sdir->name == "on_hls") {
                http_hooks->set("on_hls", sdir->dumps_args());
            } else if (sdir->name == "on_hls_notify") {
                http_hooks->set("on_hls_notify", sdir->dumps_arg0_to_str());
            }
        }
    }
    
    // hls
    if ((dir = vhost->get("hls")) != NULL) {
        SrsAmf0Object* hls = SrsAmf0Any::object();
        obj->set("hls", hls);
        
        hls->set("enabled", SrsAmf0Any::boolean(get_hls_enabled(vhost->name)));
        
        for (int i = 0; i < (int)dir->directives.size(); i++) {
            SrsConfDirective* sdir = dir->directives.at(i);
            
            if (sdir->name == "hls_fragment") {
                hls->set("hls_fragment", sdir->dumps_arg0_to_number());
            } else if (sdir->name == "hls_td_ratio") {
                hls->set("hls_td_ratio", sdir->dumps_arg0_to_number());
            } else if (sdir->name == "hls_aof_ratio") {
                hls->set("hls_aof_ratio", sdir->dumps_arg0_to_number());
            } else if (sdir->name == "hls_window") {
                hls->set("hls_window", sdir->dumps_arg0_to_number());
            } else if (sdir->name == "hls_on_error") {
                hls->set("hls_on_error", sdir->dumps_arg0_to_str());
            } else if (sdir->name == "hls_storage") {
                hls->set("hls_storage", sdir->dumps_arg0_to_str());
            } else if (sdir->name == "hls_path") {
                hls->set("hls_path", sdir->dumps_arg0_to_str());
            } else if (sdir->name == "hls_m3u8_file") {
                hls->set("hls_m3u8_file", sdir->dumps_arg0_to_str());
            } else if (sdir->name == "hls_ts_file") {
                hls->set("hls_ts_file", sdir->dumps_arg0_to_str());
            } else if (sdir->name == "hls_ts_floor") {
                hls->set("hls_ts_floor", sdir->dumps_arg0_to_boolean());
            } else if (sdir->name == "hls_entry_prefix") {
                hls->set("hls_entry_prefix", sdir->dumps_arg0_to_str());
            } else if (sdir->name == "hls_mount") {
                hls->set("hls_mount", sdir->dumps_arg0_to_str());
            } else if (sdir->name == "hls_acodec") {
                hls->set("hls_acodec", sdir->dumps_arg0_to_str());
            } else if (sdir->name == "hls_vcodec") {
                hls->set("hls_vcodec", sdir->dumps_arg0_to_str());
            } else if (sdir->name == "hls_cleanup") {
                hls->set("hls_cleanup", sdir->dumps_arg0_to_boolean());
            } else if (sdir->name == "hls_dispose") {
                hls->set("hls_dispose", sdir->dumps_arg0_to_number());
            } else if (sdir->name == "hls_nb_notify") {
                hls->set("hls_nb_notify", sdir->dumps_arg0_to_number());
            } else if (sdir->name == "hls_wait_keyframe") {
                hls->set("hls_wait_keyframe", sdir->dumps_arg0_to_boolean());
            }
        }
    }
    
    // hds
    if ((dir = vhost->get("hds")) != NULL) {
        SrsAmf0Object* hds = SrsAmf0Any::object();
        obj->set("hds", hds);
        
        hds->set("enabled", SrsAmf0Any::boolean(get_hds_enabled(vhost->name)));
        
        for (int i = 0; i < (int)dir->directives.size(); i++) {
            SrsConfDirective* sdir = dir->directives.at(i);
            
            if (sdir->name == "hds_fragment") {
                hds->set("hds_fragment", sdir->dumps_arg0_to_number());
            } else if (sdir->name == "hds_window") {
                hds->set("hds_window", sdir->dumps_arg0_to_number());
            } else if (sdir->name == "hds_path") {
                hds->set("hds_path", sdir->dumps_arg0_to_str());
            }
        }
    }
    
    // dvr
    if ((dir = vhost->get("dvr")) != NULL) {
        SrsAmf0Object* dvr = SrsAmf0Any::object();
        obj->set("dvr", dvr);
        
        dvr->set("enabled", SrsAmf0Any::boolean(get_dvr_enabled(vhost->name)));
        
        for (int i = 0; i < (int)dir->directives.size(); i++) {
            SrsConfDirective* sdir = dir->directives.at(i);
            
            if (sdir->name == "dvr_plan") {
                dvr->set("dvr_plan", sdir->dumps_arg0_to_str());
            } else if (sdir->name == "dvr_path") {
                dvr->set("dvr_path", sdir->dumps_arg0_to_str());
            } else if (sdir->name == "dvr_duration") {
                dvr->set("dvr_duration", sdir->dumps_arg0_to_number());
            } else if (sdir->name == "dvr_wait_keyframe") {
                dvr->set("dvr_wait_keyframe", sdir->dumps_arg0_to_boolean());
            } else if (sdir->name == "time_jitter") {
                dvr->set("time_jitter", sdir->dumps_arg0_to_str());
            }
        }
    }
    
    // exec
    if ((dir = vhost->get("exec")) != NULL) {
        SrsAmf0Object* ng_exec = SrsAmf0Any::object();
        obj->set("exec", ng_exec);
        
        ng_exec->set("enabled", SrsAmf0Any::boolean(get_exec_enabled(vhost->name)));
        
        for (int i = 0; i < (int)dir->directives.size(); i++) {
            SrsConfDirective* sdir = dir->directives.at(i);
            
            if (sdir->name == "publish") {
                ng_exec->set("publish", sdir->dumps_args());
            }
        }
    }
    
    // ingest
    SrsAmf0StrictArray* ingests = NULL;
    for (int i = 0; i < (int)vhost->directives.size(); i++) {
        dir = vhost->directives.at(i);
        if (dir->name != "ingest") {
            continue;
        }
        
        if (!ingests) {
            ingests = SrsAmf0Any::strict_array();
            obj->set("ingests", ingests);
        }
        
        SrsAmf0Object* ingest = SrsAmf0Any::object();
        ingest->set("id", dir->dumps_arg0_to_str());
        ingest->set("enabled", SrsAmf0Any::boolean(get_ingest_enabled(dir)));
        ingests->append(ingest);
        
        for (int j = 0; j < (int)dir->directives.size(); j++) {
            SrsConfDirective* sdir = dir->directives.at(j);
            
            if (sdir->name == "input") {
                SrsAmf0Object* input = SrsAmf0Any::object();
                ingest->set("input", input);
                
                SrsConfDirective* type = sdir->get("type");
                if (type) {
                    input->set("type", type->dumps_arg0_to_str());
                }
                
                SrsConfDirective* url = sdir->get("url");
                if (url) {
                    input->set("url", url->dumps_arg0_to_str());
                }
            } else if (sdir->name == "ffmpeg") {
                ingest->set("ffmpeg", sdir->dumps_arg0_to_str());
            } else if (sdir->name == "engine") {
                SrsAmf0Object* engine = SrsAmf0Any::object();
                ingest->set("engine", engine);
                
                if ((ret = srs_config_dumps_engine(sdir, engine)) != ERROR_SUCCESS) {
                    return ret;
                }
            }
        }
    }
    
    // transcode
    SrsAmf0StrictArray* transcodes = NULL;
    for (int i = 0; i < (int)vhost->directives.size(); i++) {
        dir = vhost->directives.at(i);
        if (dir->name != "transcode") {
            continue;
        }
        
        if (!transcodes) {
            transcodes = SrsAmf0Any::strict_array();
            obj->set("transcodes", transcodes);
        }
        
        SrsAmf0Object* transcode = SrsAmf0Any::object();
        transcodes->append(transcode);
        
        transcode->set("apply", dir->dumps_arg0_to_str());
        transcode->set("enabled", SrsAmf0Any::boolean(get_transcode_enabled(dir)));
        
        SrsAmf0StrictArray* engines = SrsAmf0Any::strict_array();
        transcode->set("engines", engines);
        
        for (int i = 0; i < (int)dir->directives.size(); i++) {
            SrsConfDirective* sdir = dir->directives.at(i);
            
            if (sdir->name == "ffmpeg") {
                transcode->set("ffmpeg", sdir->dumps_arg0_to_str());
            } else if (sdir->name == "engine") {
                SrsAmf0Object* engine = SrsAmf0Any::object();
                engines->append(engine);
                
                if ((ret = srs_config_dumps_engine(sdir, engine)) != ERROR_SUCCESS) {
                    return ret;
                }
            }
        }
    }
    
    return ret;
}

int SrsConfig::raw_to_json(SrsAmf0Object* obj)
{
    int ret = ERROR_SUCCESS;
    
    SrsAmf0Object* sobj = SrsAmf0Any::object();
    obj->set("http_api", sobj);
    
    sobj->set("enabled", SrsAmf0Any::boolean(get_http_api_enabled()));
    sobj->set("listen", SrsAmf0Any::str(get_http_api_listen().c_str()));
    sobj->set("crossdomain", SrsAmf0Any::boolean(get_http_api_crossdomain()));
    
    SrsAmf0Object* ssobj = SrsAmf0Any::object();
    sobj->set("raw_api", ssobj);
    
    ssobj->set("enabled", SrsAmf0Any::boolean(get_raw_api()));
    ssobj->set("allow_reload", SrsAmf0Any::boolean(get_raw_api_allow_reload()));
    ssobj->set("allow_query", SrsAmf0Any::boolean(get_raw_api_allow_query()));
    ssobj->set("allow_update", SrsAmf0Any::boolean(get_raw_api_allow_update()));
    
    return ret;
}

string SrsConfig::config()
{
    return config_file;
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
            case 'p':
                dolphin = true;
                if (*p) {
                    dolphin_rtmp_port = p;
                    continue;
                }
                if (argv[++i]) {
                    dolphin_rtmp_port = argv[i];
                    continue;
                }
                ret = ERROR_SYSTEM_CONFIG_INVALID;
                srs_error("option \"-p\" requires params, ret=%d", ret);
                return ret;
            case 'x':
                dolphin = true;
                if (*p) {
                    dolphin_http_port = p;
                    continue;
                }
                if (argv[++i]) {
                    dolphin_http_port = argv[i];
                    continue;
                }
                ret = ERROR_SYSTEM_CONFIG_INVALID;
                srs_error("option \"-x\" requires params, ret=%d", ret);
                return ret;
            case 'v':
            case 'V':
                show_help = false;
                show_version = true;
                break;
            case 'c':
                show_help = false;
                if (*p) {
                    config_file = p;
                    continue;
                }
                if (argv[++i]) {
                    config_file = argv[i];
                    continue;
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
        "License: "RTMP_SIG_SRS_LICENSE"\n"
        "Primary: "RTMP_SIG_SRS_PRIMARY"\n"
        "Authors: "RTMP_SIG_SRS_AUTHROS"\n"
        "Build: "SRS_AUTO_BUILD_DATE" Configuration:"SRS_AUTO_USER_CONFIGURE"\n"
        "Features:"SRS_AUTO_CONFIGURE"\n""\n"
        "Usage: %s [-h?vV] [[-t] -c <filename>]\n" 
        "\n"
        "Options:\n"
        "   -?, -h              : show this help and exit(0)\n"
        "   -v, -V              : show version and exit(0)\n"
        "   -t                  : test configuration file, exit(error_code).\n"
        "   -c filename         : use configuration file for SRS\n"
        "For srs-dolphin:\n"
        "   -p  rtmp-port       : the rtmp port to listen.\n"
        "   -x  http-port       : the http port to listen.\n"
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
    
    SrsConfigBuffer buffer;
    
    if ((ret = buffer.fullfill(config_file.c_str())) != ERROR_SUCCESS) {
        return ret;
    }
    
    return parse_buffer(&buffer);
}

int SrsConfig::check_config()
{
    int ret = ERROR_SUCCESS;

    srs_trace("srs checking config...");

    ////////////////////////////////////////////////////////////////////////
    // check empty
    ////////////////////////////////////////////////////////////////////////
    if (root->directives.size() == 0) {
        ret = ERROR_SYSTEM_CONFIG_INVALID;
        srs_error("conf is empty, ret=%d", ret);
        return ret;
    }
    
    ////////////////////////////////////////////////////////////////////////
    // check root directives.
    ////////////////////////////////////////////////////////////////////////
    for (int i = 0; i < (int)root->directives.size(); i++) {
        SrsConfDirective* conf = root->at(i);
        std::string n = conf->name;
        if (n != "listen" && n != "pid" && n != "chunk_size" && n != "ff_log_dir" 
            && n != "srs_log_tank" && n != "srs_log_level" && n != "srs_log_file"
            && n != "max_connections" && n != "daemon" && n != "heartbeat"
            && n != "http_api" && n != "stats" && n != "vhost" && n != "pithy_print_ms"
            && n != "http_server" && n != "stream_caster"
            && n != "utc_time"
        ) {
            ret = ERROR_SYSTEM_CONFIG_INVALID;
            srs_error("unsupported directive %s, ret=%d", n.c_str(), ret);
            return ret;
        }
    }
    if (true) {
        SrsConfDirective* conf = root->get("http_api");
        for (int i = 0; conf && i < (int)conf->directives.size(); i++) {
            SrsConfDirective* obj = conf->at(i);
            string n = obj->name;
            if (n != "enabled" && n != "listen" && n != "crossdomain" && n != "raw_api") {
                ret = ERROR_SYSTEM_CONFIG_INVALID;
                srs_error("unsupported http_api directive %s, ret=%d", n.c_str(), ret);
                return ret;
            }
            
            if (n == "raw_api") {
                for (int j = 0; j < (int)obj->directives.size(); j++) {
                    string m = obj->at(j)->name;
                    if (m != "enabled" && m != "allow_reload" && m != "allow_query" && m != "allow_update") {
                        ret = ERROR_SYSTEM_CONFIG_INVALID;
                        srs_error("unsupported http_api.raw_api directive %s, ret=%d", m.c_str(), ret);
                        return ret;
                    }
                }
            }
        }
    }
    if (true) {
        SrsConfDirective* conf = root->get("http_server");
        for (int i = 0; conf && i < (int)conf->directives.size(); i++) {
            string n = conf->at(i)->name;
            if (n != "enabled" && n != "listen" && n != "dir") {
                ret = ERROR_SYSTEM_CONFIG_INVALID;
                srs_error("unsupported http_stream directive %s, ret=%d", n.c_str(), ret);
                return ret;
            }
        }
    }
    if (true) {
        SrsConfDirective* conf = get_heartbeart();
        for (int i = 0; conf && i < (int)conf->directives.size(); i++) {
            string n = conf->at(i)->name;
            if (n != "enabled" && n != "interval" && n != "url"
                && n != "device_id" && n != "summaries"
                ) {
                ret = ERROR_SYSTEM_CONFIG_INVALID;
                srs_error("unsupported heartbeat directive %s, ret=%d", n.c_str(), ret);
                return ret;
            }
        }
    }
    if (true) {
        SrsConfDirective* conf = get_stats();
        for (int i = 0; conf && i < (int)conf->directives.size(); i++) {
            string n = conf->at(i)->name;
            if (n != "network" && n != "disk") {
                ret = ERROR_SYSTEM_CONFIG_INVALID;
                srs_error("unsupported stats directive %s, ret=%d", n.c_str(), ret);
                return ret;
            }
        }
    }
    
    
    ////////////////////////////////////////////////////////////////////////
    // check listen for rtmp.
    ////////////////////////////////////////////////////////////////////////
    if (true) {
        vector<string> listens = get_listens();
        if (listens.size() <= 0) {
            ret = ERROR_SYSTEM_CONFIG_INVALID;
            srs_error("directive \"listen\" is empty, ret=%d", ret);
            return ret;
        }
        for (int i = 0; i < (int)listens.size(); i++) {
            string port = listens[i];
            if (port.empty() || ::atoi(port.c_str()) <= 0) {
                ret = ERROR_SYSTEM_CONFIG_INVALID;
                srs_error("directive listen invalid, port=%s, ret=%d", port.c_str(), ret);
                return ret;
            }
        }
    }
    
    ////////////////////////////////////////////////////////////////////////
    // check max connections
    ////////////////////////////////////////////////////////////////////////
    if (get_max_connections() <= 0) {
        ret = ERROR_SYSTEM_CONFIG_INVALID;
        srs_error("directive max_connections invalid, max_connections=%d, ret=%d", get_max_connections(), ret);
        return ret;
    }
    
    // check max connections of system limits
    if (true) {
        int nb_consumed_fds = (int)get_listens().size();
        if (!get_http_api_listen().empty()) {
            nb_consumed_fds++;
        }
        if (!get_http_stream_listen().empty()) {
            nb_consumed_fds++;
        }
        if (get_log_tank_file()) {
            nb_consumed_fds++;
        }
        // 0, 1, 2 for stdin, stdout and stderr.
        nb_consumed_fds += 3;
        
        int nb_connections = get_max_connections();
        int nb_total = nb_connections + nb_consumed_fds;
        
        int max_open_files = (int)sysconf(_SC_OPEN_MAX);
        int nb_canbe = max_open_files - nb_consumed_fds - 1;

        // for each play connections, we open a pipe(2fds) to convert SrsConsumver to io,
        // refine performance, @see: https://github.com/simple-rtmp-server/srs/issues/194
        if (nb_total >= max_open_files) {
            ret = ERROR_SYSTEM_CONFIG_INVALID;
            srs_error("invalid max_connections=%d, required=%d, system limit to %d, "
                "total=%d(max_connections=%d, nb_consumed_fds=%d), ret=%d. "
                "you can change max_connections from %d to %d, or "
                "you can login as root and set the limit: ulimit -HSn %d", 
                nb_connections, nb_total + 1, max_open_files, 
                nb_total, nb_connections, nb_consumed_fds,
                ret, nb_connections, nb_canbe, nb_total + 1);
            return ret;
        }
    }
    
    ////////////////////////////////////////////////////////////////////////
    // check heartbeat
    ////////////////////////////////////////////////////////////////////////
    if (get_heartbeat_interval() <= 0) {
        ret = ERROR_SYSTEM_CONFIG_INVALID;
        srs_error("directive heartbeat interval invalid, interval=%"PRId64", ret=%d", 
            get_heartbeat_interval(), ret);
        return ret;
    }
    
    ////////////////////////////////////////////////////////////////////////
    // check stats
    ////////////////////////////////////////////////////////////////////////
    if (get_stats_network() < 0) {
        ret = ERROR_SYSTEM_CONFIG_INVALID;
        srs_error("directive stats network invalid, network=%d, ret=%d", 
            get_stats_network(), ret);
        return ret;
    }
    if (true) {
        vector<std::string> ips = srs_get_local_ipv4_ips();
        int index = get_stats_network();
        if (index >= (int)ips.size()) {
            ret = ERROR_SYSTEM_CONFIG_INVALID;
            srs_error("stats network invalid, total local ip count=%d, index=%d, ret=%d",
                (int)ips.size(), index, ret);
            return ret;
        }
        srs_warn("stats network use index=%d, ip=%s", index, ips.at(index).c_str());
    }
    if (true) {
        SrsConfDirective* conf = get_stats_disk_device();
        if (conf == NULL || (int)conf->args.size() <= 0) {
            srs_warn("stats disk not configed, disk iops disabled.");
        } else {
            string disks;
            for (int i = 0; i < (int)conf->args.size(); i++) {
                disks += conf->args.at(i);
                disks += " ";
            }
            srs_warn("stats disk list: %s", disks.c_str());
        }
    }
    
    ////////////////////////////////////////////////////////////////////////
    // check http api
    ////////////////////////////////////////////////////////////////////////
    if (get_http_api_listen().empty()) {
        ret = ERROR_SYSTEM_CONFIG_INVALID;
        srs_error("directive http_api listen invalid, listen=%s, ret=%d",
            get_http_api_listen().c_str(), ret);
        return ret;
    }
    
    ////////////////////////////////////////////////////////////////////////
    // check http stream
    ////////////////////////////////////////////////////////////////////////
    if (get_http_stream_listen().empty()) {
        ret = ERROR_SYSTEM_CONFIG_INVALID;
        srs_error("directive http_stream listen invalid, listen=%s, ret=%d",
            get_http_stream_listen().c_str(), ret);
        return ret;
    }
    ////////////////////////////////////////////////////////////////////////
    // check log name and level
    ////////////////////////////////////////////////////////////////////////
    if (true) {
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
    }
    
    ////////////////////////////////////////////////////////////////////////
    // check features
    ////////////////////////////////////////////////////////////////////////
#ifndef SRS_AUTO_HTTP_SERVER
    if (get_http_stream_enabled()) {
        srs_warn("http_stream is disabled by configure");
    }
#endif
#ifndef SRS_AUTO_HTTP_API
    if (get_http_api_enabled()) {
        srs_warn("http_api is disabled by configure");
    }
#endif

    vector<SrsConfDirective*> stream_casters = get_stream_casters();
    for (int n = 0; n < (int)stream_casters.size(); n++) {
        SrsConfDirective* stream_caster = stream_casters[n];
        for (int i = 0; stream_caster && i < (int)stream_caster->directives.size(); i++) {
            SrsConfDirective* conf = stream_caster->at(i);
            string n = conf->name;
            if (n != "enabled" && n != "caster" && n != "output"
                && n != "listen" && n != "rtp_port_min" && n != "rtp_port_max"
                ) {
                ret = ERROR_SYSTEM_CONFIG_INVALID;
                srs_error("unsupported stream_caster directive %s, ret=%d", n.c_str(), ret);
                return ret;
            }
        }
    }

    vector<SrsConfDirective*> vhosts;
    get_vhosts(vhosts);
    for (int n = 0; n < (int)vhosts.size(); n++) {
        SrsConfDirective* vhost = vhosts[n];
        for (int i = 0; vhost && i < (int)vhost->directives.size(); i++) {
            SrsConfDirective* conf = vhost->at(i);
            string n = conf->name;
            if (n != "enabled" && n != "chunk_size" && n != "min_latency" && n != "tcp_nodelay"
                && n != "dvr" && n != "ingest" && n != "hls" && n != "http_hooks"
                && n != "refer" && n != "forward" && n != "transcode" && n != "bandcheck"
                && n != "play" && n != "publish" && n != "cluster"
                && n != "security" && n != "http_remux"
                && n != "http_static" && n != "hds" && n != "exec"
            ) {
                ret = ERROR_SYSTEM_CONFIG_INVALID;
                srs_error("unsupported vhost directive %s, ret=%d", n.c_str(), ret);
                return ret;
            }
            // for each sub directives of vhost.
            if (n == "dvr") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name.c_str();
                    if (m != "enabled" && m != "dvr_path" && m != "dvr_plan"
                        && m != "dvr_duration" && m != "dvr_wait_keyframe" && m != "time_jitter"
                    ) {
                        ret = ERROR_SYSTEM_CONFIG_INVALID;
                        srs_error("unsupported vhost dvr directive %s, ret=%d", m.c_str(), ret);
                        return ret;
                    }
                }
            } else if (n == "refer") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name.c_str();
                    if (m != "enabled" && m != "all" && m != "publish" && m != "play") {
                        ret = ERROR_SYSTEM_CONFIG_INVALID;
                        srs_error("unsupported vhost refer directive %s, ret=%d", m.c_str(), ret);
                        return ret;
                    }
                }
            } else if (n == "exec") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name.c_str();
                    if (m != "enabled" && m != "publish") {
                        ret = ERROR_SYSTEM_CONFIG_INVALID;
                        srs_error("unsupported vhost exec directive %s, ret=%d", m.c_str(), ret);
                        return ret;
                    }
                }
            } else if (n == "play") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name.c_str();
                    if (m != "time_jitter" && m != "mix_correct" && m != "atc" && m != "atc_auto" && m != "mw_latency"
                        && m != "gop_cache" && m != "queue_length" && m != "send_min_interval" && m != "reduce_sequence_header"
                    ) {
                        ret = ERROR_SYSTEM_CONFIG_INVALID;
                        srs_error("unsupported vhost play directive %s, ret=%d", m.c_str(), ret);
                        return ret;
                    }
                }
            } else if (n == "cluster") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name.c_str();
                    if (m != "mode" && m != "origin" && m != "token_traverse" && m != "vhost" && m != "debug_srs_upnode") {
                        ret = ERROR_SYSTEM_CONFIG_INVALID;
                        srs_error("unsupported vhost cluster directive %s, ret=%d", m.c_str(), ret);
                        return ret;
                    }
                }
            } else if (n == "publish") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name.c_str();
                    if (m != "mr" && m != "mr_latency" && m != "firstpkt_timeout" && m != "normal_timeout") {
                        ret = ERROR_SYSTEM_CONFIG_INVALID;
                        srs_error("unsupported vhost publish directive %s, ret=%d", m.c_str(), ret);
                        return ret;
                    }
                }
            } else if (n == "ingest") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name.c_str();
                    if (m != "enabled" && m != "input" && m != "ffmpeg" && m != "engine") {
                        ret = ERROR_SYSTEM_CONFIG_INVALID;
                        srs_error("unsupported vhost ingest directive %s, ret=%d", m.c_str(), ret);
                        return ret;
                    }
                }
            } else if (n == "http_static") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name.c_str();
                    if (m != "enabled" && m != "mount" && m != "dir") {
                        ret = ERROR_SYSTEM_CONFIG_INVALID;
                        srs_error("unsupported vhost http directive %s, ret=%d", m.c_str(), ret);
                        return ret;
                    }
                }
            } else if (n == "http_remux") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name.c_str();
                    if (m != "enabled" && m != "mount" && m != "fast_cache" && m != "hstrs") {
                        ret = ERROR_SYSTEM_CONFIG_INVALID;
                        srs_error("unsupported vhost http_remux directive %s, ret=%d", m.c_str(), ret);
                        return ret;
                    }
                }
            } else if (n == "hls") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name.c_str();
                    if (m != "enabled" && m != "hls_entry_prefix" && m != "hls_path" && m != "hls_fragment" && m != "hls_window" && m != "hls_on_error"
                        && m != "hls_storage" && m != "hls_mount" && m != "hls_td_ratio" && m != "hls_aof_ratio" && m != "hls_acodec" && m != "hls_vcodec"
                        && m != "hls_m3u8_file" && m != "hls_ts_file" && m != "hls_ts_floor" && m != "hls_cleanup" && m != "hls_nb_notify"
                        && m != "hls_wait_keyframe" && m != "hls_dispose"
                        ) {
                        ret = ERROR_SYSTEM_CONFIG_INVALID;
                        srs_error("unsupported vhost hls directive %s, ret=%d", m.c_str(), ret);
                        return ret;
                    }
                }
            } else if (n == "http_hooks") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name.c_str();
                    if (m != "enabled" && m != "on_connect" && m != "on_close" && m != "on_publish"
                        && m != "on_unpublish" && m != "on_play" && m != "on_stop"
                        && m != "on_dvr" && m != "on_hls" && m != "on_hls_notify"
                        ) {
                        ret = ERROR_SYSTEM_CONFIG_INVALID;
                        srs_error("unsupported vhost http_hooks directive %s, ret=%d", m.c_str(), ret);
                        return ret;
                    }
                }
            } else if (n == "forward") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name.c_str();
                    if (m != "enabled" && m != "destination") {
                        ret = ERROR_SYSTEM_CONFIG_INVALID;
                        srs_error("unsupported vhost forward directive %s, ret=%d", m.c_str(), ret);
                        return ret;
                    }
                }
            } else if (n == "security") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    SrsConfDirective* security = conf->at(j);
                    string m = security->name.c_str();
                    if (m != "enabled" && m != "deny" && m != "allow") {
                        ret = ERROR_SYSTEM_CONFIG_INVALID;
                        srs_error("unsupported vhost security directive %s, ret=%d", m.c_str(), ret);
                        return ret;
                    }
                }
            } else if (n == "transcode") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    SrsConfDirective* trans = conf->at(j);
                    string m = trans->name.c_str();
                    if (m != "enabled" && m != "ffmpeg" && m != "engine") {
                        ret = ERROR_SYSTEM_CONFIG_INVALID;
                        srs_error("unsupported vhost transcode directive %s, ret=%d", m.c_str(), ret);
                        return ret;
                    }
                    if (m == "engine") {
                        for (int k = 0; k < (int)trans->directives.size(); k++) {
                            string e = trans->at(k)->name;
                            if (e != "enabled" && e != "vfilter" && e != "vcodec"
                                && e != "vbitrate" && e != "vfps" && e != "vwidth" && e != "vheight"
                                && e != "vthreads" && e != "vprofile" && e != "vpreset" && e != "vparams"
                                && e != "acodec" && e != "abitrate" && e != "asample_rate" && e != "achannels"
                                && e != "aparams" && e != "output"
                                && e != "iformat" && e != "oformat"
                                ) {
                                ret = ERROR_SYSTEM_CONFIG_INVALID;
                                srs_error("unsupported vhost transcode engine directive %s, ret=%d", e.c_str(), ret);
                                return ret;
                            }
                        }
                    }
                }
            } else if (n == "bandcheck") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name.c_str();
                    if (m != "enabled" && m != "key" && m != "interval" && m != "limit_kbps") {
                        ret = ERROR_SYSTEM_CONFIG_INVALID;
                        srs_error("unsupported vhost bandcheck directive %s, ret=%d", m.c_str(), ret);
                        return ret;
                    }
                }
            }
        }
    }
    // check ingest id unique.
    for (int i = 0; i < (int)vhosts.size(); i++) {
        SrsConfDirective* vhost = vhosts[i];
        std::vector<std::string> ids;

        for (int j = 0; j < (int)vhost->directives.size(); j++) {
            SrsConfDirective* conf = vhost->at(j);
            if (conf->name != "ingest") {
                continue;
            }

            std::string id = conf->arg0();
            for (int k = 0; k < (int)ids.size(); k++) {
                if (id == ids.at(k)) {
                    ret = ERROR_SYSTEM_CONFIG_INVALID;
                    srs_error("directive \"ingest\" id duplicated, vhost=%s, id=%s, ret=%d",
                              vhost->name.c_str(), id.c_str(), ret);
                    return ret;
                }
            }
            ids.push_back(id);
        }
    }
    
    ////////////////////////////////////////////////////////////////////////
    // check chunk size
    ////////////////////////////////////////////////////////////////////////
    if (get_global_chunk_size() < SRS_CONSTS_RTMP_MIN_CHUNK_SIZE 
        || get_global_chunk_size() > SRS_CONSTS_RTMP_MAX_CHUNK_SIZE
    ) {
        ret = ERROR_SYSTEM_CONFIG_INVALID;
        srs_error("directive chunk_size invalid, chunk_size=%d, must in [%d, %d], ret=%d", 
            get_global_chunk_size(), SRS_CONSTS_RTMP_MIN_CHUNK_SIZE, 
            SRS_CONSTS_RTMP_MAX_CHUNK_SIZE, ret);
        return ret;
    }
    for (int i = 0; i < (int)vhosts.size(); i++) {
        SrsConfDirective* vhost = vhosts[i];
        if (get_chunk_size(vhost->arg0()) < SRS_CONSTS_RTMP_MIN_CHUNK_SIZE 
            || get_chunk_size(vhost->arg0()) > SRS_CONSTS_RTMP_MAX_CHUNK_SIZE
        ) {
            ret = ERROR_SYSTEM_CONFIG_INVALID;
            srs_error("directive vhost %s chunk_size invalid, chunk_size=%d, must in [%d, %d], ret=%d", 
                vhost->arg0().c_str(), get_chunk_size(vhost->arg0()), SRS_CONSTS_RTMP_MIN_CHUNK_SIZE, 
                SRS_CONSTS_RTMP_MAX_CHUNK_SIZE, ret);
            return ret;
        }
    }
    for (int i = 0; i < (int)vhosts.size(); i++) {
        SrsConfDirective* vhost = vhosts[i];
        srs_assert(vhost != NULL);
#ifndef SRS_AUTO_DVR
        if (get_dvr_enabled(vhost->arg0())) {
            srs_warn("dvr of vhost %s is disabled by configure", vhost->arg0().c_str());
        }
#endif
#ifndef SRS_AUTO_HLS
        if (get_hls_enabled(vhost->arg0())) {
            srs_warn("hls of vhost %s is disabled by configure", vhost->arg0().c_str());
        }
#endif
#ifndef SRS_AUTO_HTTP_CALLBACK
        if (get_vhost_http_hooks_enabled(vhost->arg0())) {
            srs_warn("http_hooks of vhost %s is disabled by configure", vhost->arg0().c_str());
        }
#endif
#ifndef SRS_AUTO_TRANSCODE
        if (get_transcode_enabled(get_transcode(vhost->arg0(), ""))) {
            srs_warn("transcode of vhost %s is disabled by configure", vhost->arg0().c_str());
        }
#endif
#ifndef SRS_AUTO_INGEST
        vector<SrsConfDirective*> ingesters = get_ingesters(vhost->arg0());
        for (int j = 0; j < (int)ingesters.size(); j++) {
            SrsConfDirective* ingest = ingesters[j];
            if (get_ingest_enabled(ingest)) {
                srs_warn("ingest %s of vhost %s is disabled by configure", 
                    ingest->arg0().c_str(), vhost->arg0().c_str()
                );
            }
        }
#endif
        // TODO: FIXME: required http server when hls storage is ram or both.
    }
    
    return ret;
}

int SrsConfig::parse_buffer(SrsConfigBuffer* buffer)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = root->parse(buffer)) != ERROR_SUCCESS) {
        return ret;
    }
    
    // mock by dolphin mode.
    // for the dolphin will start srs with specified params.
    if (dolphin) {
        // for RTMP.
        set_config_directive(root, "listen", dolphin_rtmp_port);
        
        // for HTTP
        set_config_directive(root, "http_server", "");
        SrsConfDirective* http_server = root->get("http_server");
        set_config_directive(http_server, "enabled", "on");
        set_config_directive(http_server, "listen", dolphin_http_port);
        
        // others.
        set_config_directive(root, "daemon", "off");
        set_config_directive(root, "srs_log_tank", "console");
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
    SrsConfDirective* conf = root->get("daemon");
    if (!conf || conf->arg0().empty()) {
        return true;
    }
    
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

SrsConfDirective* SrsConfig::get_root()
{
    return root;
}

int SrsConfig::get_max_connections()
{
    static int DEFAULT = 1000;
    
    SrsConfDirective* conf = root->get("max_connections");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

vector<string> SrsConfig::get_listens()
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
    static string DEFAULT = "./objs/srs.pid";
    
    SrsConfDirective* conf = root->get("pid");
    
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

int SrsConfig::get_pithy_print_ms()
{
    static int DEFAULT = 10000;
    
    SrsConfDirective* conf = root->get("pithy_print_ms");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

bool SrsConfig::get_utc_time()
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = root->get("utc_time");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

vector<SrsConfDirective*> SrsConfig::get_stream_casters()
{
    srs_assert(root);
    
    std::vector<SrsConfDirective*> stream_casters;
    
    for (int i = 0; i < (int)root->directives.size(); i++) {
        SrsConfDirective* conf = root->at(i);
        
        if (!conf->is_stream_caster()) {
            continue;
        }
        
        stream_casters.push_back(conf);
    }
    
    return stream_casters;
}

bool SrsConfig::get_stream_caster_enabled(SrsConfDirective* conf)
{
    static bool DEFAULT = false;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_stream_caster_engine(SrsConfDirective* conf)
{
    static string DEFAULT = "";
    
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("caster");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return conf->arg0();
}

string SrsConfig::get_stream_caster_output(SrsConfDirective* conf)
{
    static string DEFAULT = "";
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("output");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

int SrsConfig::get_stream_caster_listen(SrsConfDirective* conf)
{
    static int DEFAULT = 0;
    
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("listen");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_stream_caster_rtp_port_min(SrsConfDirective* conf)
{
    static int DEFAULT = 0;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("rtp_port_min");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_stream_caster_rtp_port_max(SrsConfDirective* conf)
{
    static int DEFAULT = 0;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("rtp_port_max");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
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

void SrsConfig::get_vhosts(vector<SrsConfDirective*>& vhosts)
{
    srs_assert(root);

    for (int i = 0; i < (int)root->directives.size(); i++) {
        SrsConfDirective* conf = root->at(i);

        if (!conf->is_vhost()) {
            continue;
        }

        vhosts.push_back(conf);
    }
}

bool SrsConfig::get_vhost_enabled(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    
    return get_vhost_enabled(conf);
}

bool SrsConfig::get_vhost_enabled(SrsConfDirective* conf)
{
    static bool DEFAULT = true;
    
    // false for NULL vhost.
    if (!conf) {
        return false;
    }
    
    // perfer true for exists one.
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

bool SrsConfig::get_gop_cache(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return SRS_PERF_GOP_CACHE;
    }
    
    conf = conf->get("play");
    if (!conf || conf->arg0().empty()) {
        return SRS_PERF_GOP_CACHE;
    }
    
    conf = conf->get("gop_cache");
    if (!conf || conf->arg0().empty()) {
        return SRS_PERF_GOP_CACHE;
    }
    
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

bool SrsConfig::get_debug_srs_upnode(string vhost)
{
    static bool DEFAULT = true;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("cluster");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    conf = conf->get("debug_srs_upnode");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

bool SrsConfig::get_atc(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("play");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    conf = conf->get("atc");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

bool SrsConfig::get_atc_auto(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("play");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    conf = conf->get("atc_auto");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

int SrsConfig::get_time_jitter(string vhost)
{
    static string DEFAULT = "full";
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return _srs_time_jitter_string2int(DEFAULT);
    }
    
    conf = conf->get("play");
    if (!conf || conf->arg0().empty()) {
        return _srs_time_jitter_string2int(DEFAULT);
    }
    
    conf = conf->get("time_jitter");
    if (!conf || conf->arg0().empty()) {
        return _srs_time_jitter_string2int(DEFAULT);
    }
    
    return _srs_time_jitter_string2int(conf->arg0());
}

bool SrsConfig::get_mix_correct(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("play");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    conf = conf->get("mix_correct");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

double SrsConfig::get_queue_length(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return SRS_PERF_PLAY_QUEUE;
    }
    
    conf = conf->get("play");
    if (!conf || conf->arg0().empty()) {
        return SRS_PERF_GOP_CACHE;
    }
    
    conf = conf->get("queue_length");
    if (!conf || conf->arg0().empty()) {
        return SRS_PERF_PLAY_QUEUE;
    }
    
    return ::atoi(conf->arg0().c_str());
}

bool SrsConfig::get_refer_enabled(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("refer");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

SrsConfDirective* SrsConfig::get_refer_all(string vhost)
{
    static SrsConfDirective* DEFAULT = NULL;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("refer");
    if (!conf) {
        return DEFAULT;
    }
    
    return conf->get("all");
}

SrsConfDirective* SrsConfig::get_refer_play(string vhost)
{
    static SrsConfDirective* DEFAULT = NULL;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("refer");
    if (!conf) {
        return DEFAULT;
    }
    
    return conf->get("play");
}

SrsConfDirective* SrsConfig::get_refer_publish(string vhost)
{
    static SrsConfDirective* DEFAULT = NULL;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("refer");
    if (!conf) {
        return DEFAULT;
    }
    
    return conf->get("publish");
}

int SrsConfig::get_chunk_size(string vhost)
{
    if (vhost.empty()) {
        return get_global_chunk_size();
    }
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        // vhost does not specify the chunk size,
        // use the global instead.
        return get_global_chunk_size();
    }

    conf = conf->get("chunk_size");
    if (!conf || conf->arg0().empty()) {
        // vhost does not specify the chunk size,
        // use the global instead.
        return get_global_chunk_size();
    }

    return ::atoi(conf->arg0().c_str());
}

bool SrsConfig::get_mr_enabled(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return SRS_PERF_MR_ENABLED;
    }

    conf = conf->get("publish");
    if (!conf) {
        return SRS_PERF_MR_ENABLED;
    }

    conf = conf->get("mr");
    if (!conf || conf->arg0().empty()) {
        return SRS_PERF_MR_ENABLED;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

int SrsConfig::get_mr_sleep_ms(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return SRS_PERF_MR_SLEEP;
    }

    conf = conf->get("publish");
    if (!conf) {
        return SRS_PERF_MR_SLEEP;
    }

    conf = conf->get("mr_latency");
    if (!conf || conf->arg0().empty()) {
        return SRS_PERF_MR_SLEEP;
    }

    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_mw_sleep_ms(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return SRS_PERF_MW_SLEEP;
    }
    
    conf = conf->get("play");
    if (!conf || conf->arg0().empty()) {
        return SRS_PERF_MW_SLEEP;
    }

    conf = conf->get("mw_latency");
    if (!conf || conf->arg0().empty()) {
        return SRS_PERF_MW_SLEEP;
    }

    return ::atoi(conf->arg0().c_str());
}

bool SrsConfig::get_realtime_enabled(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return SRS_PERF_MIN_LATENCY_ENABLED;
    }

    conf = conf->get("min_latency");
    if (!conf || conf->arg0().empty()) {
        return SRS_PERF_MIN_LATENCY_ENABLED;
    }

    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

bool SrsConfig::get_tcp_nodelay(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("tcp_nodelay");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

double SrsConfig::get_send_min_interval(string vhost)
{
    static double DEFAULT = 0.0;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("play");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    conf = conf->get("send_min_interval");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atof(conf->arg0().c_str());
}

bool SrsConfig::get_reduce_sequence_header(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("play");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    conf = conf->get("reduce_sequence_header");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

int SrsConfig::get_publish_1stpkt_timeout(string vhost)
{
    // when no msg recevied for publisher, use larger timeout.
    static int DEFAULT = 20000;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("publish");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("firstpkt_timeout");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_publish_normal_timeout(string vhost)
{
    // the timeout for publish recv.
    // we must use more smaller timeout, for the recv never know the status
    // of underlayer socket.
    static int DEFAULT = 5000;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("publish");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("normal_timeout");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_global_chunk_size()
{
    SrsConfDirective* conf = root->get("chunk_size");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONSTS_RTMP_SRS_CHUNK_SIZE;
    }
    
    return ::atoi(conf->arg0().c_str());
}

bool SrsConfig::get_forward_enabled(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("forward");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

SrsConfDirective* SrsConfig::get_forwards(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return NULL;
    }
    
    conf = conf->get("forward");
    if (!conf) {
        return NULL;
    }
    
    return conf->get("destination");
}

SrsConfDirective* SrsConfig::get_vhost_http_hooks(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) { 
        return NULL;
    }
    
    return conf->get("http_hooks");
}

bool SrsConfig::get_vhost_http_hooks_enabled(string vhost)
{
    static bool DEFAULT = false;

    SrsConfDirective* conf = get_vhost_http_hooks(vhost);
    if (!conf) { 
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

SrsConfDirective* SrsConfig::get_vhost_on_connect(string vhost)
{
    SrsConfDirective* conf = get_vhost_http_hooks(vhost);
    if (!conf) { 
        return NULL;
    }
    
    return conf->get("on_connect");
}

SrsConfDirective* SrsConfig::get_vhost_on_close(string vhost)
{
    SrsConfDirective* conf = get_vhost_http_hooks(vhost);
    if (!conf) { 
        return NULL;
    }
    
    return conf->get("on_close");
}

SrsConfDirective* SrsConfig::get_vhost_on_publish(string vhost)
{
    SrsConfDirective* conf = get_vhost_http_hooks(vhost);
    if (!conf) { 
        return NULL;
    }
    
    return conf->get("on_publish");
}

SrsConfDirective* SrsConfig::get_vhost_on_unpublish(string vhost)
{
    SrsConfDirective* conf = get_vhost_http_hooks(vhost);
    if (!conf) { 
        return NULL;
    }
    
    return conf->get("on_unpublish");
}

SrsConfDirective* SrsConfig::get_vhost_on_play(string vhost)
{
    SrsConfDirective* conf = get_vhost_http_hooks(vhost);
    if (!conf) { 
        return NULL;
    }
    
    return conf->get("on_play");
}

SrsConfDirective* SrsConfig::get_vhost_on_stop(string vhost)
{
    SrsConfDirective* conf = get_vhost_http_hooks(vhost);
    if (!conf) { 
        return NULL;
    }
    
    return conf->get("on_stop");
}

SrsConfDirective* SrsConfig::get_vhost_on_dvr(string vhost)
{
    SrsConfDirective* conf = get_vhost_http_hooks(vhost);
    if (!conf) {
        return NULL;
    }
    
    return conf->get("on_dvr");
}

SrsConfDirective* SrsConfig::get_vhost_on_hls(string vhost)
{
    SrsConfDirective* conf = get_vhost_http_hooks(vhost);
    if (!conf) {
        return NULL;
    }
    
    return conf->get("on_hls");
}

SrsConfDirective* SrsConfig::get_vhost_on_hls_notify(string vhost)
{
    SrsConfDirective* conf = get_vhost_http_hooks(vhost);
    if (!conf) {
        return NULL;
    }
    
    return conf->get("on_hls_notify");
}

bool SrsConfig::get_bw_check_enabled(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("bandcheck");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_bw_check_key(string vhost)
{
    static string DEFAULT = "";
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("bandcheck");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("key");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return conf->arg0();
}

int SrsConfig::get_bw_check_interval_ms(string vhost)
{
    static int DEFAULT = 30 * 1000;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("bandcheck");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("interval");
    if (!conf) {
        return DEFAULT;
    }

    return (int)(::atof(conf->arg0().c_str()) * 1000);
}

int SrsConfig::get_bw_check_limit_kbps(string vhost)
{
    static int DEFAULT = 1000;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("bandcheck");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("limit_kbps");
    if (!conf) {
        return DEFAULT;
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
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = vhost;
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("cluster");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    conf = conf->get("mode");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return "remote" == conf->arg0();
}

SrsConfDirective* SrsConfig::get_vhost_edge_origin(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return NULL;
    }
    
    conf = conf->get("cluster");
    if (!conf || conf->arg0().empty()) {
        return NULL;
    }
    
    return conf->get("origin");
}

bool SrsConfig::get_vhost_edge_token_traverse(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("cluster");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    conf = conf->get("token_traverse");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_vhost_edge_transform_vhost(string vhost)
{
    static string DEFAULT = "[vhost]";
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("cluster");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    conf = conf->get("vhost");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

bool SrsConfig::get_security_enabled(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    SrsConfDirective* security = conf->get("security");
    if (!security) {
        return DEFAULT;
    }
    
    conf = security->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

SrsConfDirective* SrsConfig::get_security_rules(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return NULL;
    }
    
    return conf->get("security");
}

SrsConfDirective* SrsConfig::get_transcode(string vhost, string scope)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return NULL;
    }
    
    conf = conf->get("transcode");
    if (!conf || conf->arg0() != scope) {
        return NULL;
    }
    
    return conf;
}

bool SrsConfig::get_transcode_enabled(SrsConfDirective* conf)
{
    static bool DEFAULT = false;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_transcode_ffmpeg(SrsConfDirective* conf)
{
    static string DEFAULT = "";
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("ffmpeg");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

vector<SrsConfDirective*> SrsConfig::get_transcode_engines(SrsConfDirective* conf)
{
    vector<SrsConfDirective*> engines;
    
    if (!conf) {
        return engines;
    }
    
    for (int i = 0; i < (int)conf->directives.size(); i++) {
        SrsConfDirective* engine = conf->directives[i];
        
        if (engine->name == "engine") {
            engines.push_back(engine);
        }
    }
    
    return engines;
}

bool SrsConfig::get_engine_enabled(SrsConfDirective* conf)
{
    static bool DEFAULT = false;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_engine_iformat(SrsConfDirective* conf)
{
    static string DEFAULT = "flv";
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("iformat");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

vector<string> SrsConfig::get_engine_vfilter(SrsConfDirective* conf)
{
    vector<string> vfilter;
    
    if (!conf) {
        return vfilter;
    }
    
    conf = conf->get("vfilter");
    if (!conf) {
        return vfilter;
    }
    
    for (int i = 0; i < (int)conf->directives.size(); i++) {
        SrsConfDirective* filter = conf->directives[i];
        if (!filter) {
            continue;
        }
        
        vfilter.push_back("-" + filter->name);
        vfilter.push_back(filter->arg0());
    }
    
    return vfilter;
}

string SrsConfig::get_engine_vcodec(SrsConfDirective* conf)
{
    static string DEFAULT = "";
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("vcodec");
    if (!conf) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

int SrsConfig::get_engine_vbitrate(SrsConfDirective* conf)
{
    static int DEFAULT = 0;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("vbitrate");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

double SrsConfig::get_engine_vfps(SrsConfDirective* conf)
{
    static double DEFAULT = 0;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("vfps");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atof(conf->arg0().c_str());
}

int SrsConfig::get_engine_vwidth(SrsConfDirective* conf)
{
    static int DEFAULT = 0;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("vwidth");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_engine_vheight(SrsConfDirective* conf)
{
    static int DEFAULT = 0;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("vheight");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_engine_vthreads(SrsConfDirective* conf)
{
    static int DEFAULT = 1;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("vthreads");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

string SrsConfig::get_engine_vprofile(SrsConfDirective* conf)
{
    static string DEFAULT = "";
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("vprofile");
    if (!conf) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_engine_vpreset(SrsConfDirective* conf)
{
    static string DEFAULT = "";
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("vpreset");
    if (!conf) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

vector<string> SrsConfig::get_engine_vparams(SrsConfDirective* conf)
{
    vector<string> vparams;

    if (!conf) {
        return vparams;
    }
    
    conf = conf->get("vparams");
    if (!conf) {
        return vparams;
    }
    
    for (int i = 0; i < (int)conf->directives.size(); i++) {
        SrsConfDirective* filter = conf->directives[i];
        if (!filter) {
            continue;
        }
        
        vparams.push_back("-" + filter->name);
        vparams.push_back(filter->arg0());
    }
    
    return vparams;
}

string SrsConfig::get_engine_acodec(SrsConfDirective* conf)
{
    static string DEFAULT = "";
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("acodec");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

int SrsConfig::get_engine_abitrate(SrsConfDirective* conf)
{
    static int DEFAULT = 0;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("abitrate");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_engine_asample_rate(SrsConfDirective* conf)
{
    static int DEFAULT = 0;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("asample_rate");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_engine_achannels(SrsConfDirective* conf)
{
    static int DEFAULT = 0;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("achannels");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

vector<string> SrsConfig::get_engine_aparams(SrsConfDirective* conf)
{
    vector<string> aparams;
    
    if (!conf) {
        return aparams;
    }
    
    conf = conf->get("aparams");
    if (!conf) {
        return aparams;
    }
    
    for (int i = 0; i < (int)conf->directives.size(); i++) {
        SrsConfDirective* filter = conf->directives[i];
        if (!filter) {
            continue;
        }
        
        aparams.push_back("-" + filter->name);
        aparams.push_back(filter->arg0());
    }
    
    return aparams;
}

string SrsConfig::get_engine_oformat(SrsConfDirective* conf)
{
    static string DEFAULT = "flv";
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("oformat");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_engine_output(SrsConfDirective* conf)
{
    static string DEFAULT = "";
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("output");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

SrsConfDirective* SrsConfig::get_exec(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return NULL;
    }
    
    return conf->get("exec");
}

bool SrsConfig::get_exec_enabled(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_exec(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

vector<SrsConfDirective*> SrsConfig::get_exec_publishs(string vhost)
{
    vector<SrsConfDirective*> eps;
    
    SrsConfDirective* conf = get_exec(vhost);
    if (!conf) {
        return eps;
    }
    
    for (int i = 0; i < (int)conf->directives.size(); i++) {
        SrsConfDirective* ep = conf->at(i);
        if (ep->name == "publish") {
            eps.push_back(ep);
        }
    }
    
    return eps;
}

vector<SrsConfDirective*> SrsConfig::get_ingesters(string vhost)
{
    vector<SrsConfDirective*> ingeters;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return ingeters;
    }
    
    for (int i = 0; i < (int)conf->directives.size(); i++) {
        SrsConfDirective* ingester = conf->directives[i];
        
        if (ingester->name == "ingest") {
            ingeters.push_back(ingester);
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
    
    return conf->get("ingest", ingest_id);
}

bool SrsConfig::get_ingest_enabled(SrsConfDirective* conf)
{
    static bool DEFAULT = false;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_ingest_ffmpeg(SrsConfDirective* conf)
{
    static string DEFAULT = "";
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("ffmpeg");
    if (!conf) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_ingest_input_type(SrsConfDirective* conf)
{
    static string DEFAULT = "file";
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("input");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("type");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_ingest_input_url(SrsConfDirective* conf)
{
    static string DEFAULT = "";
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("input");
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("url");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

bool SrsConfig::get_log_tank_file()
{
    static bool DEFAULT = true;
    
    SrsConfDirective* conf = root->get("srs_log_tank");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0() != "console";
}

string SrsConfig::get_log_level()
{
    static string DEFAULT = "trace";
    
    SrsConfDirective* conf = root->get("srs_log_level");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_log_file()
{
    static string DEFAULT = "./objs/srs.log";
    
    SrsConfDirective* conf = root->get("srs_log_file");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
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
    static string DEFAULT = "./objs";
    
    SrsConfDirective* conf = root->get("ff_log_dir");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
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
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_hls(vhost);
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_hls_entry_prefix(string vhost)
{
    static string DEFAULT = "";
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_entry_prefix");
    if (!conf) {
        return DEFAULT;
    }

    return conf->arg0();
}

string SrsConfig::get_hls_path(string vhost)
{
    static string DEFAULT = "./objs/nginx/html";
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_path");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_hls_m3u8_file(string vhost)
{
    static string DEFAULT = "[app]/[stream].m3u8";
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_m3u8_file");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_hls_ts_file(string vhost)
{
    static string DEFAULT = "[app]/[stream]-[seq].ts";
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_ts_file");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

bool SrsConfig::get_hls_ts_floor(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_ts_floor");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

double SrsConfig::get_hls_fragment(string vhost)
{
    static double DEFAULT = 10;
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_fragment");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return ::atof(conf->arg0().c_str());
}

double SrsConfig::get_hls_td_ratio(string vhost)
{
    static double DEFAULT = 1.5;
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_td_ratio");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atof(conf->arg0().c_str());
}

double SrsConfig::get_hls_aof_ratio(string vhost)
{
    static double DEFAULT = 2.0;
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_aof_ratio");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atof(conf->arg0().c_str());
}

double SrsConfig::get_hls_window(string vhost)
{
    static double DEFAULT = 60;
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_window");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atof(conf->arg0().c_str());
}

string SrsConfig::get_hls_on_error(string vhost)
{
    static string DEFAULT = "ignore";
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_on_error");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return conf->arg0();
}

string SrsConfig::get_hls_storage(string vhost)
{
    static string DEFAULT = "disk";
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_storage");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_hls_mount(string vhost)
{
    static string DEFAULT = "[vhost]/[app]/[stream].m3u8";
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_mount");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_hls_acodec(string vhost)
{
    static string DEFAULT = "aac";
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_acodec");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_hls_vcodec(string vhost)
{
    static string DEFAULT = "h264";
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_vcodec");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

int SrsConfig::get_vhost_hls_nb_notify(string vhost)
{
    static int DEFAULT = 64;
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_nb_notify");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

bool SrsConfig::get_hls_cleanup(string vhost)
{
    static bool DEFAULT = true;
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_cleanup");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

int SrsConfig::get_hls_dispose(string vhost)
{
    static int DEFAULT = 0;
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_dispose");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

bool SrsConfig::get_hls_wait_keyframe(string vhost)
{
    static bool DEFAULT = true;
    
    SrsConfDirective* conf = get_hls(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hls_wait_keyframe");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

SrsConfDirective *SrsConfig::get_hds(const string &vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) {
        return NULL;
    }

    return conf->get("hds");
}

bool SrsConfig::get_hds_enabled(const string &vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_hds(vhost);
    if (!conf) {
        return DEFAULT;
    }

    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_hds_path(const string &vhost)
{
    static string DEFAULT = "./objs/nginx/html";
    
    SrsConfDirective* conf = get_hds(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hds_path");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

double SrsConfig::get_hds_fragment(const string &vhost)
{
    static double DEFAULT = 10;
    
    SrsConfDirective* conf = get_hds(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hds_fragment");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atof(conf->arg0().c_str());
}

double SrsConfig::get_hds_window(const string &vhost)
{
    static double DEFAULT = 60;
    
    SrsConfDirective* conf = get_hds(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hds_window");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
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
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_dvr(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_dvr_path(string vhost)
{
    static string DEFAULT = "./objs/nginx/html/[app]/[stream].[timestamp].flv";
    
    SrsConfDirective* conf = get_dvr(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("dvr_path");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_dvr_plan(string vhost)
{
    static string DEFAULT = "session";
    
    SrsConfDirective* conf = get_dvr(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("dvr_plan");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

int SrsConfig::get_dvr_duration(string vhost)
{
    static int DEFAULT = 30;
    
    SrsConfDirective* conf = get_dvr(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("dvr_duration");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

bool SrsConfig::get_dvr_wait_keyframe(string vhost)
{
    static bool DEFAULT = true;
    
    SrsConfDirective* conf = get_dvr(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("dvr_wait_keyframe");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

int SrsConfig::get_dvr_time_jitter(string vhost)
{
    static string DEFAULT = "full";
    
    SrsConfDirective* conf = get_dvr(vhost);
    
    if (!conf) {
        return _srs_time_jitter_string2int(DEFAULT);
    }
    
    conf = conf->get("time_jitter");
    if (!conf || conf->arg0().empty()) {
        return _srs_time_jitter_string2int(DEFAULT);
    }
    
    return _srs_time_jitter_string2int(conf->arg0());
}

bool SrsConfig::get_http_api_enabled()
{
    SrsConfDirective* conf = root->get("http_api");
    return get_http_api_enabled(conf);
}

bool SrsConfig::get_http_api_enabled(SrsConfDirective* conf)
{
    static bool DEFAULT = false;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_http_api_listen()
{
    static string DEFAULT = "1985";
    
    SrsConfDirective* conf = root->get("http_api");
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("listen");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }

    return conf->arg0();
}

bool SrsConfig::get_http_api_crossdomain()
{
    static bool DEFAULT = true;
    
    SrsConfDirective* conf = root->get("http_api");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("crossdomain");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

bool SrsConfig::get_raw_api()
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = root->get("http_api");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("raw_api");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

bool SrsConfig::get_raw_api_allow_reload()
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = root->get("http_api");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("raw_api");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("allow_reload");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

bool SrsConfig::get_raw_api_allow_query()
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = root->get("http_api");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("raw_api");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("allow_query");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

bool SrsConfig::get_raw_api_allow_update()
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = root->get("http_api");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("raw_api");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("allow_update");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

bool SrsConfig::get_http_stream_enabled()
{
    SrsConfDirective* conf = root->get("http_server");
    return get_http_stream_enabled(conf);
}

bool SrsConfig::get_http_stream_enabled(SrsConfDirective* conf)
{
    static bool DEFAULT = false;
    
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_http_stream_listen()
{
    static string DEFAULT = "8080";
    
    SrsConfDirective* conf = root->get("http_server");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("listen");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_http_stream_dir()
{
    static string DEFAULT = "./objs/nginx/html";
    
    SrsConfDirective* conf = root->get("http_server");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("dir");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

bool SrsConfig::get_vhost_http_enabled(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("http_static");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_vhost_http_mount(string vhost)
{
    static string DEFAULT = "[vhost]/";
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("http_static");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("mount");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_vhost_http_dir(string vhost)
{
    static string DEFAULT = "./objs/nginx/html";
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("http_static");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("dir");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

bool SrsConfig::get_vhost_http_remux_enabled(string vhost)
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("http_remux");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

double SrsConfig::get_vhost_http_remux_fast_cache(string vhost)
{
    static double DEFAULT = 0;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("http_remux");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("fast_cache");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atof(conf->arg0().c_str());
}

string SrsConfig::get_vhost_http_remux_mount(string vhost)
{
    static string DEFAULT = "[vhost]/[app]/[stream].flv";
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("http_remux");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("mount");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

bool SrsConfig::get_vhost_http_remux_hstrs(string vhost)
{
    // the HSTRS must default to false for origin.
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("http_remux");
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("hstrs");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

SrsConfDirective* SrsConfig::get_heartbeart()
{
    return root->get("heartbeat");
}

bool SrsConfig::get_heartbeat_enabled()
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_heartbeart();
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

int64_t SrsConfig::get_heartbeat_interval()
{
    static int64_t DEFAULT = (int64_t)(9.9 * 1000);
    
    SrsConfDirective* conf = get_heartbeart();
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("interval");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return (int64_t)(::atof(conf->arg0().c_str()) * 1000);
}

string SrsConfig::get_heartbeat_url()
{
    static string DEFAULT = "http://"SRS_CONSTS_LOCALHOST":8085/api/v1/servers";
    
    SrsConfDirective* conf = get_heartbeart();
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("url");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_heartbeat_device_id()
{
    static string DEFAULT = "";
    
    SrsConfDirective* conf = get_heartbeart();
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("device_id");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

bool SrsConfig::get_heartbeat_summaries()
{
    static bool DEFAULT = false;
    
    SrsConfDirective* conf = get_heartbeart();
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("summaries");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

SrsConfDirective* SrsConfig::get_stats()
{
    return root->get("stats");
}

int SrsConfig::get_stats_network()
{
    static int DEFAULT = 0;
    
    SrsConfDirective* conf = get_stats();
    if (!conf) {
        return DEFAULT;
    }
    
    conf = conf->get("network");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

SrsConfDirective* SrsConfig::get_stats_disk_device()
{
    SrsConfDirective* conf = get_stats();
    if (!conf) {
        return NULL;
    }
    
    conf = conf->get("disk");
    if (!conf || conf->args.size() == 0) {
        return NULL;
    }
    
    return conf;
}

namespace _srs_internal
{
    SrsConfigBuffer::SrsConfigBuffer()
    {
        line = 1;
    
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

bool srs_config_hls_is_on_error_ignore(string strategy)
{
    return strategy == "ignore";
}

bool srs_config_hls_is_on_error_continue(string strategy)
{
    return strategy == "continue";
}

bool srs_config_ingest_is_file(string type)
{
    return type == "file";
}

bool srs_config_ingest_is_stream(string type)
{
    return type == "stream";
}

bool srs_config_dvr_is_plan_segment(string plan)
{
    return plan == "segment";
}

bool srs_config_dvr_is_plan_session(string plan)
{
    return plan == "session";
}

bool srs_config_dvr_is_plan_append(string plan)
{
    return plan == "append";
}

bool srs_stream_caster_is_udp(string caster)
{
    return caster == "mpegts_over_udp";
}

bool srs_stream_caster_is_rtsp(string caster)
{
    return caster == "rtsp";
}

bool srs_stream_caster_is_flv(string caster)
{
    return caster == "flv";
}

int srs_config_transform_vhost(SrsConfDirective* root)
{
    int ret = ERROR_SUCCESS;
    
    for (int i = 0; i < (int)root->directives.size(); i++) {
        SrsConfDirective* dir = root->directives.at(i);
        
        // SRS2.0, rename global http_stream to http_server.
        //  SRS1:
        //      http_stream {}
        //  SRS2+:
        //      http_server {}
        if (dir->name == "http_stream") {
            dir->name = "http_server";
            continue;
        }
        
        if (!dir->is_vhost()) {
            continue;
        }
        
        // for each directive of vhost.
        std::vector<SrsConfDirective*>::iterator it;
        for (it = dir->directives.begin(); it != dir->directives.end();) {
            SrsConfDirective* conf = *it;
            string n = conf->name;
            
            // SRS2.0, rename vhost http to http_static
            //  SRS1:
            //      vhost { http {} }
            //  SRS2+:
            //      vhost { http_static {} }
            if (n == "http") {
                conf->name = "http_static";
                ++it;
                continue;
            }
            
            // SRS3.0, change the refer style
            //  SRS1/2:
            //      vhost { refer; refer_play; refer_publish; }
            //  SRS3+:
            //      vhost { refer { enabled; all; play; publish; } }
            if ((n == "refer" && conf->directives.empty()) || n == "refer_play" || n == "refer_publish") {
                // remove the old one first, for name duplicated.
                it = dir->directives.erase(it);
                
                SrsConfDirective* refer = dir->get_or_create("refer");
                refer->get_or_create("enabled", "on");
                if (n == "refer") {
                    SrsConfDirective* all = refer->get_or_create("all");
                    all->args = conf->args;
                } else if (n == "play") {
                    SrsConfDirective* play = refer->get_or_create("play");
                    play->args = conf->args;
                } else if (n == "publish") {
                    SrsConfDirective* publish = refer->get_or_create("publish");
                    publish->args = conf->args;
                }
                
                // remove the old directive.
                srs_freep(conf);
                continue;
            }
            
            // SRS3.0, change the mr style
            //  SRS2:
            //      vhost { mr { enabled; latency; } }
            //  SRS3+:
            //      vhost { publish { mr; mr_latency; } }
            if (n == "mr") {
                it = dir->directives.erase(it);
                
                SrsConfDirective* publish = dir->get_or_create("publish");
                
                SrsConfDirective* enabled = conf->get("enabled");
                if (enabled) {
                    SrsConfDirective* mr = publish->get_or_create("mr");
                    mr->args = enabled->args;
                }
                
                SrsConfDirective* latency = conf->get("latency");
                if (latency) {
                    SrsConfDirective* mr_latency = publish->get_or_create("mr_latency");
                    mr_latency->args = latency->args;
                }
                
                srs_freep(conf);
                continue;
            }
            
            // SRS3.0, change the publish_1stpkt_timeout
            //  SRS2:
            //      vhost { publish_1stpkt_timeout; }
            //  SRS3+:
            //      vhost { publish { firstpkt_timeout; } }
            if (n == "publish_1stpkt_timeout") {
                it = dir->directives.erase(it);
                
                SrsConfDirective* publish = dir->get_or_create("publish");
                
                SrsConfDirective* firstpkt_timeout = publish->get_or_create("firstpkt_timeout");
                firstpkt_timeout->args = conf->args;
                
                srs_freep(conf);
                continue;
            }
            
            // SRS3.0, change the publish_normal_timeout
            //  SRS2:
            //      vhost { publish_normal_timeout; }
            //  SRS3+:
            //      vhost { publish { normal_timeout; } }
            if (n == "publish_normal_timeout") {
                it = dir->directives.erase(it);
                
                SrsConfDirective* publish = dir->get_or_create("publish");
                
                SrsConfDirective* normal_timeout = publish->get_or_create("normal_timeout");
                normal_timeout->args = conf->args;
                
                srs_freep(conf);
                continue;
            }
            
            // SRS3.0, change the folowing like a shadow:
            //      time_jitter, mix_correct, atc, atc_auto, mw_latency, gop_cache, queue_length
            //  SRS1/2:
            //      vhost { shadow; }
            //  SRS3+:
            //      vhost { play { shadow; } }
            if (n == "time_jitter" || n == "mix_correct" || n == "atc" || n == "atc_auto"
                || n == "mw_latency" || n == "gop_cache" || n == "queue_length" || n == "send_min_interval"
                || n == "reduce_sequence_header"
            ) {
                it = dir->directives.erase(it);
                
                SrsConfDirective* play = dir->get_or_create("play");
                SrsConfDirective* shadow = play->get_or_create(conf->name);
                shadow->args = conf->args;
                
                srs_freep(conf);
                continue;
            }
            
            // SRS3.0, change the forward.
            //  SRS1/2:
            //      vhost { forward; }
            //  SRS3+:
            //      vhost { forward { enabled; destination; } }
            if (n == "forward" && conf->directives.empty()) {
                conf->get_or_create("enabled", "on");
                
                SrsConfDirective* destination = conf->get_or_create("destination");
                destination->args = conf->args;
                conf->args.clear();
                
                ++it;
                continue;
            }
            
            // SRS3.0, change the folowing like a shadow:
            //      mode, origin, token_traverse, vhost, debug_srs_upnode
            //  SRS1/2:
            //      vhost { shadow; }
            //  SRS3+:
            //      vhost { cluster { shadow; } }
            if (n == "mode" || n == "origin" || n == "token_traverse" || n == "vhost" || n == "debug_srs_upnode") {
                it = dir->directives.erase(it);
                
                SrsConfDirective* cluster = dir->get_or_create("cluster");
                SrsConfDirective* shadow = cluster->get_or_create(conf->name);
                shadow->args = conf->args;
                
                srs_freep(conf);
                continue;
            }
            
            ++it;
        }
    }
    
    return ret;
}

int srs_config_dumps_engine(SrsConfDirective* dir, SrsAmf0Object* engine)
{
    int ret = ERROR_SUCCESS;
    
    SrsConfDirective* conf = NULL;
    
    engine->set("id", dir->dumps_arg0_to_str());
    engine->set("enabled", SrsAmf0Any::boolean(_srs_config->get_engine_enabled(dir)));
    
    if ((conf = dir->get("iformat")) != NULL) {
        engine->set("iformat", conf->dumps_arg0_to_str());
    }
    
    if ((conf = dir->get("vfilter")) != NULL) {
        SrsAmf0Object* vfilter = SrsAmf0Any::object();
        engine->set("vfilter", vfilter);
        
        for (int i = 0; i < (int)conf->directives.size(); i++) {
            SrsConfDirective* sdir = conf->directives.at(i);
            vfilter->set(sdir->name, sdir->dumps_arg0_to_str());
        }
    }
    
    if ((conf = dir->get("vcodec")) != NULL) {
        engine->set("vcodec", conf->dumps_arg0_to_str());
    }
    
    if ((conf = dir->get("vbitrate")) != NULL) {
        engine->set("vbitrate", conf->dumps_arg0_to_number());
    }
    
    if ((conf = dir->get("vfps")) != NULL) {
        engine->set("vfps", conf->dumps_arg0_to_number());
    }
    
    if ((conf = dir->get("vwidth")) != NULL) {
        engine->set("vwidth", conf->dumps_arg0_to_number());
    }
    
    if ((conf = dir->get("vheight")) != NULL) {
        engine->set("vheight", conf->dumps_arg0_to_number());
    }
    
    if ((conf = dir->get("vthreads")) != NULL) {
        engine->set("vthreads", conf->dumps_arg0_to_number());
    }
    
    if ((conf = dir->get("vprofile")) != NULL) {
        engine->set("vprofile", conf->dumps_arg0_to_str());
    }
    
    if ((conf = dir->get("vpreset")) != NULL) {
        engine->set("vpreset", conf->dumps_arg0_to_str());
    }
    
    if ((conf = dir->get("vparams")) != NULL) {
        SrsAmf0Object* vparams = SrsAmf0Any::object();
        engine->set("vparams", vparams);
        
        for (int i = 0; i < (int)conf->directives.size(); i++) {
            SrsConfDirective* sdir = conf->directives.at(i);
            vparams->set(sdir->name, sdir->dumps_arg0_to_str());
        }
    }
    
    if ((conf = dir->get("acodec")) != NULL) {
        engine->set("acodec", conf->dumps_arg0_to_str());
    }
    
    if ((conf = dir->get("abitrate")) != NULL) {
        engine->set("abitrate", conf->dumps_arg0_to_number());
    }
    
    if ((conf = dir->get("asample_rate")) != NULL) {
        engine->set("asample_rate", conf->dumps_arg0_to_number());
    }
    
    if ((conf = dir->get("achannels")) != NULL) {
        engine->set("achannels", conf->dumps_arg0_to_number());
    }
    
    if ((conf = dir->get("aparams")) != NULL) {
        SrsAmf0Object* aparams = SrsAmf0Any::object();
        engine->set("aparams", aparams);
        
        for (int i = 0; i < (int)conf->directives.size(); i++) {
            SrsConfDirective* sdir = conf->directives.at(i);
            aparams->set(sdir->name, sdir->dumps_arg0_to_str());
        }
    }
    
    if ((conf = dir->get("oformat")) != NULL) {
        engine->set("oformat", conf->dumps_arg0_to_str());
    }
    
    if ((conf = dir->get("output")) != NULL) {
        engine->set("output", conf->dumps_arg0_to_str());
    }
    
    return ret;
}

