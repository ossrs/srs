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

#ifndef SRS_APP_CONIFG_HPP
#define SRS_APP_CONIFG_HPP

/*
#include <srs_app_config.hpp>
*/
#include <srs_core.hpp>

#include <vector>
#include <string>

#include <srs_app_reload.hpp>

// default vhost for rtmp
#define RTMP_VHOST_DEFAULT "__defaultVhost__"

#define SRS_LOCALHOST "127.0.0.1"
#define SRS_CONF_DEFAULT_PID_FILE "./objs/srs.pid"
#define SRS_DEFAULT_CONF "conf/srs.conf"

#define SRS_CONF_DEFAULT_HLS_PATH "./objs/nginx/html"
#define SRS_CONF_DEFAULT_HLS_FRAGMENT 10
#define SRS_CONF_DEFAULT_HLS_WINDOW 60
#define SRS_CONF_DEFAULT_DVR_PATH "./objs/nginx/html"
#define SRS_CONF_DEFAULT_DVR_PLAN_SESSION "session"
#define SRS_CONF_DEFAULT_DVR_PLAN_SEGMENT "segment"
// chnvideo hss
#define SRS_CONF_DEFAULT_DVR_PLAN_HSS "hss"
#define SRS_CONF_DEFAULT_DVR_PLAN SRS_CONF_DEFAULT_DVR_PLAN_SESSION
#define SRS_CONF_DEFAULT_DVR_DURATION 30
// in ms, for HLS aac sync time.
#define SRS_CONF_DEFAULT_AAC_SYNC 100
// in ms, for HLS aac flush the audio
#define SRS_CONF_DEFAULT_AAC_DELAY 300
// in seconds, the live queue length.
#define SRS_CONF_DEFAULT_QUEUE_LENGTH 30
// in seconds, the paused queue length.
#define SRS_CONF_DEFAULT_PAUSED_LENGTH 10
// the interval in seconds for bandwidth check
#define SRS_CONF_DEFAULT_BANDWIDTH_INTERVAL 30
// the interval in seconds for bandwidth check
#define SRS_CONF_DEFAULT_BANDWIDTH_LIMIT_KBPS 1000

#define SRS_CONF_DEFAULT_HTTP_MOUNT "/"
#define SRS_CONF_DEFAULT_HTTP_DIR SRS_CONF_DEFAULT_HLS_PATH

#define SRS_CONF_DEFAULT_HTTP_STREAM_PORT 8080
#define SRS_CONF_DEFAULT_HTTP_API_PORT 1985

#define SRS_CONF_DEFAULT_HTTP_HEAETBEAT_ENABLED false
#define SRS_CONF_DEFAULT_HTTP_HEAETBEAT_INTERVAL 9.9
#define SRS_CONF_DEFAULT_HTTP_HEAETBEAT_URL "http://127.0.0.1:8085/api/v1/servers"

#define SRS_STAGE_PLAY_USER_INTERVAL_MS 10000
#define SRS_STAGE_PUBLISH_USER_INTERVAL_MS 10000
#define SRS_STAGE_FORWARDER_INTERVAL_MS 10000
#define SRS_STAGE_ENCODER_INTERVAL_MS 10000
#define SRS_STAGE_INGESTER_INTERVAL_MS 10000
#define SRS_STAGE_HLS_INTERVAL_MS 10000
#define SRS_STAGE_EDGE_INTERVAL_MS 10000

#define SRS_AUTO_INGEST_TYPE_FILE "file"
#define SRS_AUTO_INGEST_TYPE_STREAM "stream"

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
    void set_arg0(std::string value);
    SrsConfDirective* at(int index);
    SrsConfDirective* get(std::string _name);
    SrsConfDirective* get(std::string _name, std::string _arg0);
public:
    virtual int parse(const char* filename);
public:
    enum SrsDirectiveType{parse_file, parse_block};
    virtual int parse_conf(SrsFileBuffer* buffer, SrsDirectiveType type);
    virtual int read_token(SrsFileBuffer* buffer, std::vector<std::string>& args);
public:
    virtual bool is_vhost();
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
    bool test_conf;
    bool show_version;
    std::string config_file;
    std::string _argv;
    std::string _cwd;
    SrsConfDirective* root;
    std::vector<ISrsReloadHandler*> subscribes;
public:
    SrsConfig();
    virtual ~SrsConfig();
// reload
public:
    virtual void subscribe(ISrsReloadHandler* handler);
    virtual void unsubscribe(ISrsReloadHandler* handler);
    virtual int reload();
private:
    virtual SrsConfDirective* get_or_create(SrsConfDirective* node, std::string name);
public:
    /**
    * dynamic set the config, for instance, for http api to set,
    * @return ture if config changed and need to reload.
    */
    virtual bool set_log_file(std::string file);
    virtual bool set_log_tank(std::string tank);
    virtual bool set_log_level(std::string level);
public:
    virtual int force_reload_log_file();
    virtual int force_reload_log_tank();
    virtual int force_reload_log_level();
private:
    virtual int reload_http_api(SrsConfDirective* old_root);
    virtual int reload_http_stream(SrsConfDirective* old_root);
    virtual int reload_vhost(SrsConfDirective* old_root);
    virtual int reload_transcode(SrsConfDirective* new_vhost, SrsConfDirective* old_vhost);
    virtual int reload_ingest(SrsConfDirective* new_vhost, SrsConfDirective* old_vhost);
// parse options and file
public:
    virtual int parse_options(int argc, char** argv);
private:
    virtual int parse_file(const char* filename);
    virtual int parse_argv(int& i, char** argv);
    virtual void print_help(char** argv);
public:
    virtual std::string         cwd();
    virtual std::string         argv();
// global section
public:
    virtual SrsConfDirective*   get_root();
    virtual bool                get_deamon();
    virtual int                 get_max_connections();
    virtual SrsConfDirective*   get_listen();
    virtual std::string         get_pid_file();
    virtual int                 get_pithy_print_publish();
    virtual int                 get_pithy_print_forwarder();
    virtual int                 get_pithy_print_encoder();
    virtual int                 get_pithy_print_ingester();
    virtual int                 get_pithy_print_hls();
    virtual int                 get_pithy_print_play();
    virtual int                 get_pithy_print_edge();
// vhost specified section
public:
    virtual SrsConfDirective*   get_vhost(std::string vhost);
    virtual void                get_vhosts(std::vector<SrsConfDirective*>& vhosts);
    virtual bool                get_vhost_enabled(std::string vhost);
    virtual bool                get_vhost_enabled(SrsConfDirective* vhost);
    virtual SrsConfDirective*   get_vhost_on_connect(std::string vhost);
    virtual SrsConfDirective*   get_vhost_on_close(std::string vhost);
    virtual SrsConfDirective*   get_vhost_on_publish(std::string vhost);
    virtual SrsConfDirective*   get_vhost_on_unpublish(std::string vhost);
    virtual SrsConfDirective*   get_vhost_on_play(std::string vhost);
    virtual SrsConfDirective*   get_vhost_on_stop(std::string vhost);
    virtual SrsConfDirective*   get_vhost_on_dvr_hss_reap_flv(std::string vhost);
    virtual bool                get_gop_cache(std::string vhost);
    virtual bool                get_atc(std::string vhost);
    virtual bool                get_atc_auto(std::string vhost);
    virtual double              get_queue_length(std::string vhost);
    virtual SrsConfDirective*   get_forward(std::string vhost);
    virtual SrsConfDirective*   get_refer(std::string vhost);
    virtual SrsConfDirective*   get_refer_play(std::string vhost);
    virtual SrsConfDirective*   get_refer_publish(std::string vhost);
    virtual int                 get_chunk_size(const std::string& vhost);
// bwct(bandwidth check tool) section
public:
    virtual bool                get_bw_check_enabled(const std::string& vhost);
    virtual std::string         get_bw_check_key(const std::string& vhost);
    virtual int                 get_bw_check_interval_ms(const std::string& vhost);
    virtual int                 get_bw_check_limit_kbps(const std::string& vhost);
// vhost edge section
public:
    virtual bool                get_vhost_is_edge(std::string vhost);
    virtual bool                get_vhost_is_edge(SrsConfDirective* vhost);
    virtual SrsConfDirective*   get_vhost_edge_origin(std::string vhost);
// vhost transcode section
public:
    virtual SrsConfDirective*   get_transcode(std::string vhost, std::string scope);
    virtual bool                get_transcode_enabled(SrsConfDirective* transcode);
    virtual std::string         get_transcode_ffmpeg(SrsConfDirective* transcode);
    virtual void                get_transcode_engines(SrsConfDirective* transcode, std::vector<SrsConfDirective*>& engines);
    virtual bool                get_engine_enabled(SrsConfDirective* engine);
    virtual std::string         get_engine_vcodec(SrsConfDirective* engine);
    virtual int                 get_engine_vbitrate(SrsConfDirective* engine);
    virtual double              get_engine_vfps(SrsConfDirective* engine);
    virtual int                 get_engine_vwidth(SrsConfDirective* engine);
    virtual int                 get_engine_vheight(SrsConfDirective* engine);
    virtual int                 get_engine_vthreads(SrsConfDirective* engine);
    virtual std::string         get_engine_vprofile(SrsConfDirective* engine);
    virtual std::string         get_engine_vpreset(SrsConfDirective* engine);
    virtual void                get_engine_vparams(SrsConfDirective* engine, std::vector<std::string>& vparams);
    virtual void                get_engine_vfilter(SrsConfDirective* engine, std::vector<std::string>& vfilter);
    virtual std::string         get_engine_acodec(SrsConfDirective* engine);
    virtual int                 get_engine_abitrate(SrsConfDirective* engine);
    virtual int                 get_engine_asample_rate(SrsConfDirective* engine);
    virtual int                 get_engine_achannels(SrsConfDirective* engine);
    virtual void                get_engine_aparams(SrsConfDirective* engine, std::vector<std::string>& aparams);
    virtual std::string         get_engine_output(SrsConfDirective* engine);
// ingest section
public:
    virtual void                get_ingesters(std::string vhost, std::vector<SrsConfDirective*>& ingeters);
    virtual SrsConfDirective*   get_ingest_by_id(std::string vhost, std::string ingest_id);
    virtual bool                get_ingest_enabled(SrsConfDirective* ingest);
    virtual std::string         get_ingest_ffmpeg(SrsConfDirective* ingest);
    virtual std::string         get_ingest_input_type(SrsConfDirective* ingest);
    virtual std::string         get_ingest_input_url(SrsConfDirective* ingest);
// log section
public:
    virtual bool                get_log_tank_file();
    virtual std::string         get_log_level();
    virtual std::string         get_log_file();
    virtual std::string         get_ffmpeg_log_dir();
// hls section
private:
    virtual SrsConfDirective*   get_hls(std::string vhost);
public:
    virtual bool                get_hls_enabled(std::string vhost);
    virtual std::string         get_hls_path(std::string vhost);
    virtual double              get_hls_fragment(std::string vhost);
    virtual double              get_hls_window(std::string vhost);
// dvr section
private:
    virtual SrsConfDirective*   get_dvr(std::string vhost);
public:
    virtual bool                get_dvr_enabled(std::string vhost);
    virtual std::string         get_dvr_path(std::string vhost);
    virtual std::string         get_dvr_plan(std::string vhost);
    virtual int                 get_dvr_duration(std::string vhost);
// http api section
private:
    virtual SrsConfDirective*   get_http_api();
public:
    virtual bool                get_http_api_enabled();
    virtual bool                get_http_api_enabled(SrsConfDirective* conf);
    virtual int                 get_http_api_listen();
// http stream section
private:
    virtual SrsConfDirective*   get_http_stream();
public:
    virtual bool                get_http_stream_enabled();
    virtual bool                get_http_stream_enabled(SrsConfDirective* conf);
    virtual int                 get_http_stream_listen();
    virtual std::string         get_http_stream_dir();
public:
    virtual bool                get_vhost_http_enabled(std::string vhost);
    virtual std::string         get_vhost_http_mount(std::string vhost);
    virtual std::string         get_vhost_http_dir(std::string vhost);
// http heartbeart section
private:
    virtual SrsConfDirective*   get_heartbeart();
public:
    virtual bool                get_heartbeat_enabled();
    virtual int64_t             get_heartbeat_interval();
    virtual std::string         get_heartbeat_url();
    virtual std::string         get_heartbeat_device_id();
};

/**
* deep compare directive.
*/
bool srs_directive_equals(SrsConfDirective* a, SrsConfDirective* b);

// global config
extern SrsConfig* _srs_config;

#endif