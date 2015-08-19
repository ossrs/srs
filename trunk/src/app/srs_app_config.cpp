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

using namespace _srs_internal;

#define SRS_WIKI_URL_LOG "https://github.com/simple-rtmp-server/srs/wiki/v1_CN_SrsLog"

// when user config an invalid value, macros to perfer true or false.
#define SRS_CONF_PERFER_FALSE(conf_arg) conf_arg == "on"
#define SRS_CONF_PERFER_TRUE(conf_arg) conf_arg != "off"

///////////////////////////////////////////////////////////
// default consts values
///////////////////////////////////////////////////////////
#define SRS_CONF_DEFAULT_PID_FILE "./objs/srs.pid"
#define SRS_CONF_DEFAULT_LOG_FILE "./objs/srs.log"
#define SRS_CONF_DEFAULT_LOG_LEVEL "trace"
#define SRS_CONF_DEFAULT_LOG_TANK_CONSOLE "console"
#define SRS_CONF_DEFAULT_COFNIG_FILE "conf/srs.conf"
#define SRS_CONF_DEFAULT_FF_LOG_DIR "./objs"
#define SRS_CONF_DEFAULT_UTC_TIME false

#define SRS_CONF_DEFAULT_MAX_CONNECTIONS 1000
#define SRS_CONF_DEFAULT_HLS_PATH "./objs/nginx/html"
#define SRS_CONF_DEFAULT_HLS_M3U8_FILE "[app]/[stream].m3u8"
#define SRS_CONF_DEFAULT_HLS_TS_FILE "[app]/[stream]-[seq].ts"
#define SRS_CONF_DEFAULT_HLS_TS_FLOOR false
#define SRS_CONF_DEFAULT_HLS_FRAGMENT 10
#define SRS_CONF_DEFAULT_HLS_TD_RATIO 1.5
#define SRS_CONF_DEFAULT_HLS_AOF_RATIO 2.0
#define SRS_CONF_DEFAULT_HLS_WINDOW 60
#define SRS_CONF_DEFAULT_HLS_ON_ERROR_IGNORE "ignore"
#define SRS_CONF_DEFAULT_HLS_ON_ERROR_DISCONNECT "disconnect"
#define SRS_CONF_DEFAULT_HLS_ON_ERROR_CONTINUE "continue"
#define SRS_CONF_DEFAULT_HLS_ON_ERROR SRS_CONF_DEFAULT_HLS_ON_ERROR_IGNORE
#define SRS_CONF_DEFAULT_HLS_STORAGE "disk"
#define SRS_CONF_DEFAULT_HLS_MOUNT "[vhost]/[app]/[stream].m3u8"
#define SRS_CONF_DEFAULT_HLS_ACODEC "aac"
#define SRS_CONF_DEFAULT_HLS_VCODEC "h264"
#define SRS_CONF_DEFAULT_HLS_CLEANUP true
#define SRS_CONF_DEFAULT_HLS_WAIT_KEYFRAME true
#define SRS_CONF_DEFAULT_HLS_NB_NOTIFY 64
#define SRS_CONF_DEFAULT_DVR_PATH "./objs/nginx/html/[app]/[stream].[timestamp].flv"
#define SRS_CONF_DEFAULT_DVR_PLAN_SESSION "session"
#define SRS_CONF_DEFAULT_DVR_PLAN_SEGMENT "segment"
#define SRS_CONF_DEFAULT_DVR_PLAN_APPEND "append"
#define SRS_CONF_DEFAULT_DVR_PLAN SRS_CONF_DEFAULT_DVR_PLAN_SESSION
#define SRS_CONF_DEFAULT_DVR_DURATION 30
#define SRS_CONF_DEFAULT_TIME_JITTER "full"
#define SRS_CONF_DEFAULT_ATC_AUTO true
#define SRS_CONF_DEFAULT_MIX_CORRECT false
// in seconds, the paused queue length.
#define SRS_CONF_DEFAULT_PAUSED_LENGTH 10
// the interval in seconds for bandwidth check
#define SRS_CONF_DEFAULT_BANDWIDTH_INTERVAL 30
// the interval in seconds for bandwidth check
#define SRS_CONF_DEFAULT_BANDWIDTH_LIMIT_KBPS 1000

#define SRS_CONF_DEFAULT_HTTP_MOUNT "[vhost]/"
#define SRS_CONF_DEFAULT_HTTP_REMUX_MOUNT "[vhost]/[app]/[stream].flv"
#define SRS_CONF_DEFAULT_HTTP_DIR SRS_CONF_DEFAULT_HLS_PATH
#define SRS_CONF_DEFAULT_HTTP_AUDIO_FAST_CACHE 0

#define SRS_CONF_DEFAULT_HTTP_STREAM_PORT "8080"
#define SRS_CONF_DEFAULT_HTTP_API_PORT "1985"
#define SRS_CONF_DEFAULT_HTTP_API_CROSSDOMAIN true

#define SRS_CONF_DEFAULT_HTTP_HEAETBEAT_ENABLED false
#define SRS_CONF_DEFAULT_HTTP_HEAETBEAT_INTERVAL 9.9
#define SRS_CONF_DEFAULT_HTTP_HEAETBEAT_URL "http://"SRS_CONSTS_LOCALHOST":8085/api/v1/servers"
#define SRS_CONF_DEFAULT_HTTP_HEAETBEAT_SUMMARIES false

#define SRS_CONF_DEFAULT_SECURITY_ENABLED false

#define SRS_CONF_DEFAULT_STREAM_CASTER_ENABLED false
#define SRS_CONF_DEFAULT_STREAM_CASTER_MPEGTS_OVER_UDP "mpegts_over_udp"
#define SRS_CONF_DEFAULT_STREAM_CASTER_RTSP "rtsp"
#define SRS_CONF_DEFAULT_STREAM_CASTER_FLV "flv"

#define SRS_CONF_DEFAULT_STATS_NETWORK_DEVICE_INDEX 0

#define SRS_CONF_DEFAULT_PITHY_PRINT_MS 10000

#define SRS_CONF_DEFAULT_INGEST_TYPE_FILE "file"
#define SRS_CONF_DEFAULT_INGEST_TYPE_STREAM "stream"

#define SRS_CONF_DEFAULT_TRANSCODE_IFORMAT "flv"
#define SRS_CONF_DEFAULT_TRANSCODE_OFORMAT "flv"

#define SRS_CONF_DEFAULT_EDGE_TOKEN_TRAVERSE false
#define SRS_CONF_DEFAULT_EDGE_TRANSFORM_VHOST "[vhost]"

// hds default value
#define SRS_CONF_DEFAULT_HDS_PATH       "./objs/nginx/html"
#define SRS_CONF_DEFAULT_HDS_WINDOW     (60)
#define SRS_CONF_DEFAULT_HDS_FRAGMENT   (10)

// '\n'
#define SRS_LF (char)SRS_CONSTS_LF

// '\r'
#define SRS_CR (char)SRS_CONSTS_CR

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
            // mix_correct, only one per vhost
            if (!srs_directive_equals(new_vhost->get("mix_correct"), old_vhost->get("mix_correct"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((ret = subscribe->on_reload_vhost_mix_correct(vhost)) != ERROR_SUCCESS) {
                        srs_error("vhost %s notify subscribes mix_correct failed. ret=%d", vhost.c_str(), ret);
                        return ret;
                    }
                }
                srs_trace("vhost %s reload mix_correct success.", vhost.c_str());
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
                srs_trace("vhost %s reload hlsdvrsuccess.", vhost.c_str());
            }
            // mr, only one per vhost
            if (!srs_directive_equals(new_vhost->get("mr"), old_vhost->get("mr"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((ret = subscribe->on_reload_vhost_mr(vhost)) != ERROR_SUCCESS) {
                        srs_error("vhost %s notify subscribes mr failed. ret=%d", vhost.c_str(), ret);
                        return ret;
                    }
                }
                srs_trace("vhost %s reload mr success.", vhost.c_str());
            }
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
            // mw, only one per vhost
            if (!srs_directive_equals(new_vhost->get("mw_latency"), old_vhost->get("mw_latency"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((ret = subscribe->on_reload_vhost_mw(vhost)) != ERROR_SUCCESS) {
                        srs_error("vhost %s notify subscribes mw failed. ret=%d", vhost.c_str(), ret);
                        return ret;
                    }
                }
                srs_trace("vhost %s reload mw success.", vhost.c_str());
            }
            // smi(send_min_interval), only one per vhost
            if (!srs_directive_equals(new_vhost->get("send_min_interval"), old_vhost->get("send_min_interval"))) {
                for (it = subscribes.begin(); it != subscribes.end(); ++it) {
                    ISrsReloadHandler* subscribe = *it;
                    if ((ret = subscribe->on_reload_vhost_smi(vhost)) != ERROR_SUCCESS) {
                        srs_error("vhost %s notify subscribes smi failed. ret=%d", vhost.c_str(), ret);
                        return ret;
                    }
                }
                srs_trace("vhost %s reload smi success.", vhost.c_str());
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
            // http_static, only one per vhost.
            // @remark, http_static introduced as alias of http.
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
    //      token_traverse, debug_srs_upnode,
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
    // http_stream rename to http_server in SRS2.
    if (!new_http_stream) {
        new_http_stream = root->get("http_server");
    }

    SrsConfDirective* old_http_stream = old_root->get("http_stream");
    // http_stream rename to http_server in SRS2.
    if (!old_http_stream) {
        old_http_stream = root->get("http_server");
    }

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
            && n != "http_stream" && n != "http_server" && n != "stream_caster"
            && n != "utc_time"
        ) {
            ret = ERROR_SYSTEM_CONFIG_INVALID;
            srs_error("unsupported directive %s, ret=%d", n.c_str(), ret);
            return ret;
        }
    }
    if (true) {
        SrsConfDirective* conf = get_http_api();
        for (int i = 0; conf && i < (int)conf->directives.size(); i++) {
            string n = conf->at(i)->name;
            if (n != "enabled" && n != "listen" && n != "crossdomain") {
                ret = ERROR_SYSTEM_CONFIG_INVALID;
                srs_error("unsupported http_api directive %s, ret=%d", n.c_str(), ret);
                return ret;
            }
        }
    }
    if (true) {
        SrsConfDirective* conf = get_http_stream();
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
            if (n != "enabled" && n != "chunk_size"
                && n != "mode" && n != "origin" && n != "token_traverse" && n != "vhost"
                && n != "dvr" && n != "ingest" && n != "hls" && n != "http_hooks"
                && n != "gop_cache" && n != "queue_length"
                && n != "refer" && n != "refer_publish" && n != "refer_play"
                && n != "forward" && n != "transcode" && n != "bandcheck"
                && n != "time_jitter" && n != "mix_correct"
                && n != "atc" && n != "atc_auto"
                && n != "debug_srs_upnode"
                && n != "mr" && n != "mw_latency" && n != "min_latency"
                && n != "tcp_nodelay" && n != "send_min_interval" && n != "reduce_sequence_header"
                && n != "security" && n != "http_remux"
                && n != "http" && n != "http_static"
                && n != "hds"
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
            } else if (n == "mr") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name.c_str();
                    if (m != "enabled" && m != "latency"
                        ) {
                        ret = ERROR_SYSTEM_CONFIG_INVALID;
                        srs_error("unsupported vhost mr directive %s, ret=%d", m.c_str(), ret);
                        return ret;
                    }
                }
            } else if (n == "ingest") {
                for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name.c_str();
                    if (m != "enabled" && m != "input" && m != "ffmpeg"
                        && m != "engine"
                        ) {
                        ret = ERROR_SYSTEM_CONFIG_INVALID;
                        srs_error("unsupported vhost ingest directive %s, ret=%d", m.c_str(), ret);
                        return ret;
                    }
                }
            } else if (n == "http" || n == "http_static") {
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
                // TODO: FIXME: implements it.
                /*for (int j = 0; j < (int)conf->directives.size(); j++) {
                    string m = conf->at(j)->name.c_str();
                    if (m != "enabled" && m != "vhost" && m != "refer") {
                        ret = ERROR_SYSTEM_CONFIG_INVALID;
                        srs_error("unsupported vhost forward directive %s, ret=%d", m.c_str(), ret);
                        return ret;
                    }
                }*/
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
    srs_assert(root);
    
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
    srs_assert(root);
    
    SrsConfDirective* conf = root->get("max_connections");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_MAX_CONNECTIONS;
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
    SrsConfDirective* conf = root->get("pid");
    
    if (!conf) {
        return SRS_CONF_DEFAULT_PID_FILE;
    }
    
    return conf->arg0();
}

int SrsConfig::get_pithy_print_ms()
{
    SrsConfDirective* pithy = root->get("pithy_print_ms");
    if (!pithy || pithy->arg0().empty()) {
        return SRS_CONF_DEFAULT_PITHY_PRINT_MS;
    }
    
    return ::atoi(pithy->arg0().c_str());
}

bool SrsConfig::get_utc_time()
{
    SrsConfDirective* conf = root->get("utc_time");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_UTC_TIME;
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

bool SrsConfig::get_stream_caster_enabled(SrsConfDirective* sc)
{
    srs_assert(sc);

    SrsConfDirective* conf = sc->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_STREAM_CASTER_ENABLED;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_stream_caster_engine(SrsConfDirective* sc)
{
    srs_assert(sc);

    SrsConfDirective* conf = sc->get("caster");
    if (!conf || conf->arg0().empty()) {
        return "";
    }

    return conf->arg0();
}

string SrsConfig::get_stream_caster_output(SrsConfDirective* sc)
{
    srs_assert(sc);

    SrsConfDirective* conf = sc->get("output");
    if (!conf || conf->arg0().empty()) {
        return "";
    }

    return conf->arg0();
}

int SrsConfig::get_stream_caster_listen(SrsConfDirective* sc)
{
    srs_assert(sc);

    SrsConfDirective* conf = sc->get("listen");
    if (!conf || conf->arg0().empty()) {
        return 0;
    }

    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_stream_caster_rtp_port_min(SrsConfDirective* sc)
{
    srs_assert(sc);

    SrsConfDirective* conf = sc->get("rtp_port_min");
    if (!conf || conf->arg0().empty()) {
        return 0;
    }

    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_stream_caster_rtp_port_max(SrsConfDirective* sc)
{
    srs_assert(sc);

    SrsConfDirective* conf = sc->get("rtp_port_max");
    if (!conf || conf->arg0().empty()) {
        return 0;
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
    SrsConfDirective* vhost_conf = get_vhost(vhost);
    
    return get_vhost_enabled(vhost_conf);
}

bool SrsConfig::get_vhost_enabled(SrsConfDirective* vhost)
{
    if (!vhost) {
        return false;
    }
    
    SrsConfDirective* conf = vhost->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return true;
    }
    
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

bool SrsConfig::get_gop_cache(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) {
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
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) {
        return true;
    }
    
    conf = conf->get("debug_srs_upnode");
    if (!conf || conf->arg0().empty()) {
        return true;
    }
    
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

bool SrsConfig::get_atc(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) {
        return false;
    }
    
    conf = conf->get("atc");
    if (!conf || conf->arg0().empty()) {
        return false;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

bool SrsConfig::get_atc_auto(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) {
        return SRS_CONF_DEFAULT_ATC_AUTO;
    }
    
    conf = conf->get("atc_auto");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_ATC_AUTO;
    }
    
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

int SrsConfig::get_time_jitter(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    
    std::string time_jitter = SRS_CONF_DEFAULT_TIME_JITTER;
    
    if (conf) {
        conf = conf->get("time_jitter");
    
        if (conf && !conf->arg0().empty()) {
            time_jitter = conf->arg0();
        }
    }
    
    return _srs_time_jitter_string2int(time_jitter);
}

bool SrsConfig::get_mix_correct(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    
    if (!conf) {
        return SRS_CONF_DEFAULT_MIX_CORRECT;
    }
    
    conf = conf->get("mix_correct");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_MIX_CORRECT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

double SrsConfig::get_queue_length(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) {
        return SRS_PERF_PLAY_QUEUE;
    }
    
    conf = conf->get("queue_length");
    if (!conf || conf->arg0().empty()) {
        return SRS_PERF_PLAY_QUEUE;
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

    conf = conf->get("mr");
    if (!conf) {
        return SRS_PERF_MR_ENABLED;
    }

    conf = conf->get("enabled");
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

    conf = conf->get("mr");
    if (!conf) {
        return SRS_PERF_MR_SLEEP;
    }

    conf = conf->get("latency");
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
    
    conf = conf->get("reduce_sequence_header");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

int SrsConfig::get_global_chunk_size()
{
    SrsConfDirective* conf = root->get("chunk_size");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONSTS_RTMP_SRS_CHUNK_SIZE;
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
    SrsConfDirective* conf = get_vhost_http_hooks(vhost);

    if (!conf) { 
        return false;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return false;
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
    SrsConfDirective* conf = get_vhost(vhost);

    if (!conf) {
        return false;
    }

    conf = conf->get("bandcheck");
    if (!conf) {
        return false;
    }

    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return false;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
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

    return (int)(::atof(conf->arg0().c_str()) * 1000);
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
    static bool DEFAULT = false;
    SrsConfDirective* conf = vhost;

    if (!conf) {
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
    
    return conf->get("origin");
}

bool SrsConfig::get_vhost_edge_token_traverse(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    
    if (!conf) {
        return SRS_CONF_DEFAULT_EDGE_TOKEN_TRAVERSE;
    }
    
    conf = conf->get("token_traverse");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_EDGE_TOKEN_TRAVERSE;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_vhost_edge_transform_vhost(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    
    if (!conf) {
        return SRS_CONF_DEFAULT_EDGE_TRANSFORM_VHOST;
    }
    
    conf = conf->get("vhost");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_EDGE_TRANSFORM_VHOST;
    }
    
    return conf->arg0();
}

bool SrsConfig::get_security_enabled(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    
    if (!conf) {
        return SRS_CONF_DEFAULT_SECURITY_ENABLED;
    }
    
    SrsConfDirective* security = conf->get("security");
    if (!security) {
        return SRS_CONF_DEFAULT_SECURITY_ENABLED;
    }
    
    conf = security->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_SECURITY_ENABLED;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

SrsConfDirective* SrsConfig::get_security_rules(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    
    if (!conf) {
        return NULL;
    }
    
    SrsConfDirective* security = conf->get("security");
    if (!security) {
        return NULL;
    }
    
    return security;
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
    if (!conf || conf->arg0().empty()) {
        return false;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
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
    if (!conf || conf->arg0().empty()) {
        return false;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
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
    static string DEFAULT = "";
    
    if (!engine) {
        return DEFAULT;
    }
    
    SrsConfDirective* conf = engine->get("vcodec");
    if (!conf) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

int SrsConfig::get_engine_vbitrate(SrsConfDirective* engine)
{
    static int DEFAULT = 0;
    
    if (!engine) {
        return DEFAULT;
    }
    
    SrsConfDirective* conf = engine->get("vbitrate");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

double SrsConfig::get_engine_vfps(SrsConfDirective* engine)
{
    static double DEFAULT = 0;
    
    if (!engine) {
        return DEFAULT;
    }
    
    SrsConfDirective* conf = engine->get("vfps");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atof(conf->arg0().c_str());
}

int SrsConfig::get_engine_vwidth(SrsConfDirective* engine)
{
    static int DEFAULT = 0;
    
    if (!engine) {
        return DEFAULT;
    }
    
    SrsConfDirective* conf = engine->get("vwidth");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_engine_vheight(SrsConfDirective* engine)
{
    static int DEFAULT = 0;
    
    if (!engine) {
        return DEFAULT;
    }
    
    SrsConfDirective* conf = engine->get("vheight");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_engine_vthreads(SrsConfDirective* engine)
{
    static int DEFAULT = 1;
    
    if (!engine) {
        return DEFAULT;
    }
    
    SrsConfDirective* conf = engine->get("vthreads");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
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
    static string DEFAULT = "";
    
    if (!engine) {
        return DEFAULT;
    }
    
    SrsConfDirective* conf = engine->get("acodec");
    if (!conf) {
        return DEFAULT;
    }
    
    return conf->arg0();
}

int SrsConfig::get_engine_abitrate(SrsConfDirective* engine)
{
    static int DEFAULT = 0;
    
    if (!engine) {
        return DEFAULT;
    }
    
    SrsConfDirective* conf = engine->get("abitrate");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_engine_asample_rate(SrsConfDirective* engine)
{
    static int DEFAULT = 0;
    
    if (!engine) {
        return DEFAULT;
    }
    
    SrsConfDirective* conf = engine->get("asample_rate");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
    }
    
    return ::atoi(conf->arg0().c_str());
}

int SrsConfig::get_engine_achannels(SrsConfDirective* engine)
{
    static int DEFAULT = 0;
    
    if (!engine) {
        return DEFAULT;
    }
    
    SrsConfDirective* conf = engine->get("achannels");
    if (!conf || conf->arg0().empty()) {
        return DEFAULT;
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
    if (!ingest) {
        return false;
    }
    
    SrsConfDirective* conf = ingest->get("enabled");
    
    if (!conf || conf->arg0().empty()) {
        return false;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_ingest_ffmpeg(SrsConfDirective* ingest)
{
    if (!ingest) {
        return "";
    }
    
    SrsConfDirective* conf = ingest->get("ffmpeg");
    
    if (!conf) {
        return "";
    }
    
    return conf->arg0();
}

string SrsConfig::get_ingest_input_type(SrsConfDirective* ingest)
{
    if (!ingest) {
        return SRS_CONF_DEFAULT_INGEST_TYPE_FILE;
    }
    
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
    if (!ingest) {
        return "";
    }
    
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

bool SrsConfig::get_log_tank_file()
{
    srs_assert(root);
    
    SrsConfDirective* conf = root->get("srs_log_tank");
    if (!conf || conf->arg0().empty()) {
        return true;
    }
    
    return conf->arg0() != SRS_CONF_DEFAULT_LOG_TANK_CONSOLE;
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
    
    if (!conf || conf->arg0().empty()) {
        return false;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_hls_entry_prefix(string vhost)
{
    SrsConfDirective* hls = get_hls(vhost);
    
    if (!hls) {
        return "";
    }
    
    SrsConfDirective* conf = hls->get("hls_entry_prefix");
    
    if (!conf) {
        return "";
    }

    return conf->arg0();
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

string SrsConfig::get_hls_m3u8_file(string vhost)
{
    SrsConfDirective* hls = get_hls(vhost);
    
    if (!hls) {
        return SRS_CONF_DEFAULT_HLS_M3U8_FILE;
    }
    
    SrsConfDirective* conf = hls->get("hls_m3u8_file");
    
    if (!conf) {
        return SRS_CONF_DEFAULT_HLS_M3U8_FILE;
    }
    
    return conf->arg0();
}

string SrsConfig::get_hls_ts_file(string vhost)
{
    SrsConfDirective* hls = get_hls(vhost);
    
    if (!hls) {
        return SRS_CONF_DEFAULT_HLS_TS_FILE;
    }
    
    SrsConfDirective* conf = hls->get("hls_ts_file");
    
    if (!conf) {
        return SRS_CONF_DEFAULT_HLS_TS_FILE;
    }
    
    return conf->arg0();
}

bool SrsConfig::get_hls_ts_floor(string vhost)
{
    SrsConfDirective* hls = get_hls(vhost);
    
    if (!hls) {
        return SRS_CONF_DEFAULT_HLS_TS_FLOOR;
    }
    
    SrsConfDirective* conf = hls->get("hls_ts_floor");
    
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_HLS_TS_FLOOR;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
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

double SrsConfig::get_hls_td_ratio(string vhost)
{
    SrsConfDirective* hls = get_hls(vhost);
    
    if (!hls) {
        return SRS_CONF_DEFAULT_HLS_TD_RATIO;
    }
    
    SrsConfDirective* conf = hls->get("hls_td_ratio");
    
    if (!conf) {
        return SRS_CONF_DEFAULT_HLS_TD_RATIO;
    }
    
    return ::atof(conf->arg0().c_str());
}

double SrsConfig::get_hls_aof_ratio(string vhost)
{
    SrsConfDirective* hls = get_hls(vhost);
    
    if (!hls) {
        return SRS_CONF_DEFAULT_HLS_AOF_RATIO;
    }
    
    SrsConfDirective* conf = hls->get("hls_aof_ratio");
    
    if (!conf) {
        return SRS_CONF_DEFAULT_HLS_AOF_RATIO;
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

string SrsConfig::get_hls_on_error(string vhost)
{
    SrsConfDirective* hls = get_hls(vhost);
    
    if (!hls) {
        return SRS_CONF_DEFAULT_HLS_ON_ERROR;
    }
    
    SrsConfDirective* conf = hls->get("hls_on_error");
    
    if (!conf) {
        return SRS_CONF_DEFAULT_HLS_ON_ERROR;
    }

    return conf->arg0();
}

string SrsConfig::get_hls_storage(string vhost)
{
    SrsConfDirective* hls = get_hls(vhost);
    
    if (!hls) {
        return SRS_CONF_DEFAULT_HLS_STORAGE;
    }
    
    SrsConfDirective* conf = hls->get("hls_storage");
    
    if (!conf) {
        return SRS_CONF_DEFAULT_HLS_STORAGE;
    }

    return conf->arg0();
}

string SrsConfig::get_hls_mount(string vhost)
{
    SrsConfDirective* hls = get_hls(vhost);
    
    if (!hls) {
        return SRS_CONF_DEFAULT_HLS_MOUNT;
    }
    
    SrsConfDirective* conf = hls->get("hls_mount");
    
    if (!conf) {
        return SRS_CONF_DEFAULT_HLS_MOUNT;
    }

    return conf->arg0();
}

string SrsConfig::get_hls_acodec(string vhost)
{
    SrsConfDirective* hls = get_hls(vhost);
    
    if (!hls) {
        return SRS_CONF_DEFAULT_HLS_ACODEC;
    }
    
    SrsConfDirective* conf = hls->get("hls_acodec");
    
    if (!conf) {
        return SRS_CONF_DEFAULT_HLS_ACODEC;
    }

    return conf->arg0();
}

string SrsConfig::get_hls_vcodec(string vhost)
{
    SrsConfDirective* hls = get_hls(vhost);
    
    if (!hls) {
        return SRS_CONF_DEFAULT_HLS_VCODEC;
    }
    
    SrsConfDirective* conf = hls->get("hls_vcodec");
    
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_HLS_VCODEC;
    }

    return conf->arg0();
}

int SrsConfig::get_vhost_hls_nb_notify(string vhost)
{
    SrsConfDirective* conf = get_hls(vhost);
    
    if (!conf) {
        return SRS_CONF_DEFAULT_HLS_NB_NOTIFY;
    }
    
    conf = conf->get("hls_nb_notify");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_HLS_NB_NOTIFY;
    }
    
    return ::atoi(conf->arg0().c_str());
}

bool SrsConfig::get_hls_cleanup(string vhost)
{
    SrsConfDirective* hls = get_hls(vhost);
    
    if (!hls) {
        return SRS_CONF_DEFAULT_HLS_CLEANUP;
    }
    
    SrsConfDirective* conf = hls->get("hls_cleanup");
    
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_HLS_CLEANUP;
    }
    
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

int SrsConfig::get_hls_dispose(string vhost)
{
    SrsConfDirective* conf = get_hls(vhost);
    
    int DEFAULT = 0;
    
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
    SrsConfDirective* hls = get_hls(vhost);
    
    if (!hls) {
        return SRS_CONF_DEFAULT_HLS_WAIT_KEYFRAME;
    }
    
    SrsConfDirective* conf = hls->get("hls_wait_keyframe");
    
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_HLS_WAIT_KEYFRAME;
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
    SrsConfDirective* hds = get_hds(vhost);

    if (!hds) {
        return false;
    }

    SrsConfDirective* conf = hds->get("enabled");

    if (!conf || conf->arg0().empty()) {
        return false;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_hds_path(const string &vhost)
{
    SrsConfDirective* hds = get_hds(vhost);

    if (!hds) {
        return SRS_CONF_DEFAULT_HDS_PATH;
    }

    SrsConfDirective* conf = hds->get("hds_path");

    if (!conf) {
        return SRS_CONF_DEFAULT_HDS_PATH;
    }

    return conf->arg0();
}

double SrsConfig::get_hds_fragment(const string &vhost)
{
    SrsConfDirective* hds = get_hds(vhost);

    if (!hds) {
        return SRS_CONF_DEFAULT_HDS_FRAGMENT;
    }

    SrsConfDirective* conf = hds->get("hds_fragment");

    if (!conf) {
        return SRS_CONF_DEFAULT_HDS_FRAGMENT;
    }

    return ::atof(conf->arg0().c_str());
}

double SrsConfig::get_hds_window(const string &vhost)
{
    SrsConfDirective* hds = get_hds(vhost);

    if (!hds) {
        return SRS_CONF_DEFAULT_HDS_WINDOW;
    }

    SrsConfDirective* conf = hds->get("hds_window");

    if (!conf) {
        return SRS_CONF_DEFAULT_HDS_WINDOW;
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
    
    if (!conf || conf->arg0().empty()) {
        return false;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
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

bool SrsConfig::get_dvr_wait_keyframe(string vhost)
{
    SrsConfDirective* dvr = get_dvr(vhost);
    
    if (!dvr) {
        return true;
    }
    
    SrsConfDirective* conf = dvr->get("dvr_wait_keyframe");
    
    if (!conf || conf->arg0().empty()) {
        return true;
    }
    
    return SRS_CONF_PERFER_TRUE(conf->arg0());
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
    if (!conf || conf->arg0().empty()) {
        return false;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_http_api_listen()
{
    SrsConfDirective* conf = get_http_api();
    
    if (!conf) {
        return SRS_CONF_DEFAULT_HTTP_API_PORT;
    }
    
    conf = conf->get("listen");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_HTTP_API_PORT;
    }

    return conf->arg0();
}

bool SrsConfig::get_http_api_crossdomain()
{
    SrsConfDirective* conf = get_http_api();
    
    if (!conf) {
        return SRS_CONF_DEFAULT_HTTP_API_CROSSDOMAIN;
    }
    
    conf = conf->get("crossdomain");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_HTTP_API_CROSSDOMAIN;
    }
    
    return SRS_CONF_PERFER_TRUE(conf->arg0());
}

bool SrsConfig::get_http_stream_enabled()
{
    SrsConfDirective* conf = get_http_stream();
    return get_http_stream_enabled(conf);
}

SrsConfDirective* SrsConfig::get_http_stream()
{
    SrsConfDirective* conf = root->get("http_stream");
    // http_stream renamed to http_server in SRS2.
    if (!conf) {
        conf = root->get("http_server");
    }

    return conf;
}

bool SrsConfig::get_http_stream_enabled(SrsConfDirective* conf)
{
    if (!conf) {
        return false;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return false;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_http_stream_listen()
{
    SrsConfDirective* conf = get_http_stream();
    
    if (!conf) {
        return SRS_CONF_DEFAULT_HTTP_STREAM_PORT;
    }
    
    conf = conf->get("listen");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_HTTP_STREAM_PORT;
    }
    
    return conf->arg0();
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
    SrsConfDirective* vconf = get_vhost(vhost);
    if (!vconf) {
        return false;
    }
    
    SrsConfDirective* conf = vconf->get("http");
    if (!conf) {
        conf = vconf->get("http_static");
    }
    
    if (!conf) {
        return false;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return false;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

string SrsConfig::get_vhost_http_mount(string vhost)
{
    SrsConfDirective* vconf = get_vhost(vhost);
    if (!vconf) {
        return SRS_CONF_DEFAULT_HTTP_MOUNT;
    }
    
    SrsConfDirective* conf = vconf->get("http");
    if (!conf) {
        conf = vconf->get("http_static");
        if (!conf) {
            return SRS_CONF_DEFAULT_HTTP_MOUNT;
        }
    }
    
    conf = conf->get("mount");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_HTTP_MOUNT;
    }
    
    return conf->arg0();
}

string SrsConfig::get_vhost_http_dir(string vhost)
{
    SrsConfDirective* vconf = get_vhost(vhost);
    if (!vconf) {
        return SRS_CONF_DEFAULT_HTTP_DIR;
    }
    
    SrsConfDirective* conf = vconf->get("http");
    if (!conf) {
        conf = vconf->get("http_static");
        if (!conf) {
            return SRS_CONF_DEFAULT_HTTP_DIR;
        }
    }
    
    conf = conf->get("dir");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_HTTP_DIR;
    }
    
    return conf->arg0();
}

bool SrsConfig::get_vhost_http_remux_enabled(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return false;
    }
    
    conf = conf->get("http_remux");
    if (!conf) {
        return false;
    }
    
    conf = conf->get("enabled");
    if (!conf || conf->arg0().empty()) {
        return false;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

double SrsConfig::get_vhost_http_remux_fast_cache(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return SRS_CONF_DEFAULT_HTTP_AUDIO_FAST_CACHE;
    }
    
    conf = conf->get("http_remux");
    if (!conf) {
        return SRS_CONF_DEFAULT_HTTP_AUDIO_FAST_CACHE;
    }
    
    conf = conf->get("fast_cache");
    if (!conf) {
        return SRS_CONF_DEFAULT_HTTP_AUDIO_FAST_CACHE;
    }
    
    if (conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_HTTP_AUDIO_FAST_CACHE;
    }
    
    return ::atof(conf->arg0().c_str());
}

string SrsConfig::get_vhost_http_remux_mount(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return SRS_CONF_DEFAULT_HTTP_REMUX_MOUNT;
    }
    
    conf = conf->get("http_remux");
    if (!conf) {
        return SRS_CONF_DEFAULT_HTTP_REMUX_MOUNT;
    }
    
    conf = conf->get("mount");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_HTTP_REMUX_MOUNT;
    }
    
    return conf->arg0();
}

bool SrsConfig::get_vhost_http_remux_hstrs(string vhost)
{
    SrsConfDirective* conf = get_vhost(vhost);
    if (!conf) {
        return false;
    }
    
    conf = conf->get("http_remux");
    if (!conf) {
        return false;
    }
    
    conf = conf->get("hstrs");
    if (!conf || conf->arg0().empty()) {
        return false;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
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
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_HTTP_HEAETBEAT_ENABLED;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
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

bool SrsConfig::get_heartbeat_summaries()
{
    SrsConfDirective* conf = get_heartbeart();
    
    if (!conf) {
        return SRS_CONF_DEFAULT_HTTP_HEAETBEAT_SUMMARIES;
    }
    
    conf = conf->get("summaries");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_HTTP_HEAETBEAT_SUMMARIES;
    }
    
    return SRS_CONF_PERFER_FALSE(conf->arg0());
}

SrsConfDirective* SrsConfig::get_stats()
{
    return root->get("stats");
}

int SrsConfig::get_stats_network()
{
    SrsConfDirective* conf = get_stats();
    
    if (!conf) {
        return SRS_CONF_DEFAULT_STATS_NETWORK_DEVICE_INDEX;
    }
    
    conf = conf->get("network");
    if (!conf || conf->arg0().empty()) {
        return SRS_CONF_DEFAULT_STATS_NETWORK_DEVICE_INDEX;
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
    return strategy == SRS_CONF_DEFAULT_HLS_ON_ERROR_IGNORE;
}

bool srs_config_hls_is_on_error_continue(string strategy)
{
    return strategy == SRS_CONF_DEFAULT_HLS_ON_ERROR_CONTINUE;
}

bool srs_config_ingest_is_file(string type)
{
    return type == SRS_CONF_DEFAULT_INGEST_TYPE_FILE;
}

bool srs_config_ingest_is_stream(string type)
{
    return type == SRS_CONF_DEFAULT_INGEST_TYPE_STREAM;
}

bool srs_config_dvr_is_plan_segment(string plan)
{
    return plan == SRS_CONF_DEFAULT_DVR_PLAN_SEGMENT;
}

bool srs_config_dvr_is_plan_session(string plan)
{
    return plan == SRS_CONF_DEFAULT_DVR_PLAN_SESSION;
}

bool srs_config_dvr_is_plan_append(string plan)
{
    return plan == SRS_CONF_DEFAULT_DVR_PLAN_APPEND;
}

bool srs_stream_caster_is_udp(string caster)
{
    return caster == SRS_CONF_DEFAULT_STREAM_CASTER_MPEGTS_OVER_UDP;
}

bool srs_stream_caster_is_rtsp(string caster)
{
    return caster == SRS_CONF_DEFAULT_STREAM_CASTER_RTSP;
}

bool srs_stream_caster_is_flv(string caster)
{
    return caster == SRS_CONF_DEFAULT_STREAM_CASTER_FLV;
}
