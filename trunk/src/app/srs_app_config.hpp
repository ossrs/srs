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

#define SRS_LOCALHOST "127.0.0.1"
#define SRS_CONF_DEFAULT_PID_FILE "./objs/srs.pid"
#define SRS_DEFAULT_CONF "conf/srs.conf"

#define SRS_CONF_DEFAULT_MAX_CONNECTIONS 12345
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
#define SRS_CONF_DEFAULT_TIME_JITTER "full"
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
#define SRS_CONF_DEFAULT_HTTP_HEAETBEAT_INDEX 0
#define SRS_CONF_DEFAULT_HTTP_HEAETBEAT_SUMMARIES false

#define SRS_STAGE_PLAY_USER_INTERVAL_MS 10000
#define SRS_STAGE_PUBLISH_USER_INTERVAL_MS 10000
#define SRS_STAGE_FORWARDER_INTERVAL_MS 10000
#define SRS_STAGE_ENCODER_INTERVAL_MS 10000
#define SRS_STAGE_INGESTER_INTERVAL_MS 10000
#define SRS_STAGE_HLS_INTERVAL_MS 10000
#define SRS_STAGE_EDGE_INTERVAL_MS 10000

#define SRS_AUTO_INGEST_TYPE_FILE "file"
#define SRS_AUTO_INGEST_TYPE_STREAM "stream"

namespace _srs_internal
{
    class SrsConfigBuffer;
};

/**
* the config directive.
* the config file is a group of directives,
* all directive has name, args and child-directives.
* for example, the following config text:
        vhost vhost.ossrs.net {
            enabled         on;
            ingest livestream {
                enabled      on;
                ffmpeg       /bin/ffmpeg;
            }
        }
* will be parsed to:
*       SrsConfDirective: name="vhost", arg0="vhost.ossrs.net", child-directives=[
*           SrsConfDirective: name="enabled", arg0="on", child-directives=[]
*           SrsConfDirective: name="ingest", arg0="livestream", child-directives=[
*               SrsConfDirective: name="enabled", arg0="on", child-directives=[]
*               SrsConfDirective: name="ffmpeg", arg0="/bin/ffmpeg", child-directives=[]
*           ]
*       ]
*/
class SrsConfDirective
{
public:
    /**
    * the line of config file in which the directive from
    */
    int conf_line;
    /**
    * the name of directive, for example, the following config text:
    *       enabled     on;
    * will be parsed to a directive, its name is "enalbed"
    */
    std::string name;
    /**
    * the args of directive, for example, the following config text:
    *       listen      1935 1936;
    * will be parsed to a directive, its args is ["1935", "1936"].
    */
    std::vector<std::string> args;
    /**
    * the child directives, for example, the following config text:
    *       vhost vhost.ossrs.net {
    *           enabled         on;
    *       }
    * will be parsed to a directive, its directives is a vector contains 
    * a directive, which is:
    *       name:"enalbed", args:["on"], directives:[]
    * 
    * @remark, the directives can contains directives.
    */
    std::vector<SrsConfDirective*> directives;
public:
    SrsConfDirective();
    virtual ~SrsConfDirective();
// args
public:
    /**
    * get the args0,1,2, if user want to get more args,
    * directly use the args.at(index).
    */
    virtual std::string arg0();
    virtual std::string arg1();
    virtual std::string arg2();
// directives
public:
    /**
    * get the directive by index.
    * @remark, assert the index<directives.size().
    */
    virtual SrsConfDirective* at(int index);
    /**
    * get the directive by name, return the first match.
    */
    virtual SrsConfDirective* get(std::string _name);
    /**
    * get the directive by name and its arg0, return the first match.
    */
    virtual SrsConfDirective* get(std::string _name, std::string _arg0);
// help utilities
public:
    /**
    * whether current directive is vhost.
    */
    virtual bool is_vhost();
// parse utilities
public:
    /**
    * parse config directive from file buffer.
    */
    virtual int parse(_srs_internal::SrsConfigBuffer* buffer);
// private parse.
private:
    /**
    * the directive parsing type.
    */
    enum SrsDirectiveType {
        /**
        * the root directives, parsing file.
        */
        parse_file, 
        /**
        * for each direcitve, parsing text block.
        */
        parse_block
    };
    /**
    * parse the conf from buffer. the work flow:
    * 1. read a token(directive args and a ret flag), 
    * 2. initialize the directive by args, args[0] is name, args[1-N] is args of directive,
    * 3. if ret flag indicates there are child-directives, read_conf(directive, block) recursively.
    */
    virtual int parse_conf(_srs_internal::SrsConfigBuffer* buffer, SrsDirectiveType type);
    /**
    * read a token from buffer.
    * a token, is the directive args and a flag indicates whether has child-directives.
    * @param args, the output directive args, the first is the directive name, left is the args.
    * @return, an error code indicates error or has child-directives.
    */
    virtual int read_token(_srs_internal::SrsConfigBuffer* buffer, std::vector<std::string>& args);
};

/**
* the config service provider.
* for the config supports reload, so never keep the reference cross st-thread,
* that is, never save the SrsConfDirective* get by any api of config,
* for it maybe free in the reload st-thread cycle.
* you can keep it before st-thread switch, or simply never keep it.
*/
class SrsConfig
{
// user command
private:
    /**
    * whether show help and exit.
    */
    bool show_help;
    /**
    * whether test config file and exit.
    */
    bool test_conf;
    /**
    * whether show SRS version and exit.
    */
    bool show_version;
// global env variables.
private:
    /**
    * the user parameters, the argc and argv.
    * the argv is " ".join(argv), where argv is from main(argc, argv).
    */
    std::string _argv;
    /**
    * current working directory.
    */
    std::string _cwd;
// config section
private:
    /**
    * the last parsed config file.
    * if reload, reload the config file.
    */
    std::string config_file;
    /**
    * the directive root.
    */
    SrsConfDirective* root;
// reload section
private:
    /**
    * the reload subscribers, when reload, callback all handlers.
    */
    std::vector<ISrsReloadHandler*> subscribes;
public:
    SrsConfig();
    virtual ~SrsConfig();
// reload
public:
    /**
    * for reload handler to register itself,
    * when config service do the reload, callback the handler.
    */
    virtual void subscribe(ISrsReloadHandler* handler);
    /**
    * for reload handler to unregister itself.
    */
    virtual void unsubscribe(ISrsReloadHandler* handler);
    /**
    * reload the config file.
    * @remark, user can test the config before reload it.
    */
    virtual int reload();
private:
    /**
    * reload the http_api section of config.
    */
    virtual int reload_http_api(SrsConfDirective* old_root);
    /**
    * reload the http_stream section of config.
    */
    virtual int reload_http_stream(SrsConfDirective* old_root);
    /**
    * reload the vhost section of config.
    */
    virtual int reload_vhost(SrsConfDirective* old_root);
    /**
    * reload the transcode section of vhost of config.
    */
    virtual int reload_transcode(SrsConfDirective* new_vhost, SrsConfDirective* old_vhost);
    /**
    * reload the ingest section of vhost of config.
    */
    virtual int reload_ingest(SrsConfDirective* new_vhost, SrsConfDirective* old_vhost);
// parse options and file
public:
    /**
    * parse the cli, the main(argc,argv) function.
    */
    virtual int parse_options(int argc, char** argv);
private:
    /**
    * parse each argv.
    */
    virtual int parse_argv(int& i, char** argv);
    /**
    * print help and exit.
    */
    virtual void print_help(char** argv);
    /**
    * parse the config file, which is specified by cli.
    */
    virtual int parse_file(const char* filename);
protected:
    /**
    * parse config from the buffer.
    * @param buffer, the config buffer, user must delete it.
    * @remark, protected for the utest to override with mock.
    */
    virtual int parse_buffer(_srs_internal::SrsConfigBuffer* buffer);
// global env
public:
    /**
    * get the current work directory.
    */
    virtual std::string         cwd();
    /**
    * get the cli, the main(argc,argv), program start command.
    */
    virtual std::string         argv();
// global section
public:
    /**
    * get the directive root, corresponding to the config file.
    * the root directive, no name and args, contains directives.
    * all directive parsed can retrieve from root.
    */
    virtual SrsConfDirective*   get_root();
    /**
    * get the deamon config.
    * if true, SRS will run in deamon mode, fork and fork to reap the 
    * grand-child process to init process.
    */
    virtual bool                get_deamon();
    /**
    * get the max connections limit of system.
    * if exceed the max connection, SRS will disconnect the connection.
    * @remark, linux will limit the connections of each process, 
    *       for example, when you need SRS to service 10000+ connections,
    *       user must use "ulimit -HSn 10000" and config the max connections
    *       of SRS.
    */
    virtual int                 get_max_connections();
    /**
    * get the listen port of SRS.
    * user can specifies multiple listen ports,
    * each args of directive is a listen port.
    */
    virtual std::vector<std::string>        get_listen();
    /**
    * get the pid file path.
    * the pid file is used to save the pid of SRS,
    * use file lock to prevent multiple SRS starting.
    * @remark, if user need to run multiple SRS instance,
    *       for example, to start multiple SRS for multiple CPUs,
    *       user can use different pid file for each process.
    */
    virtual std::string         get_pid_file();
    /**
    * get the pithy print interval for publish, in ms,
    * the publish(flash/FMLE) message print.
    */
    virtual int                 get_pithy_print_publish();
    /**
    * get the pithy print interval for forwarder, in ms,
    * the forwarder message print, for SRS forward stream to other servers.
    */
    virtual int                 get_pithy_print_forwarder();
    /**
    * get the pithy print interval for encoder, in ms,
    * the encoder message print, for FFMPEG transcoder.
    */
    virtual int                 get_pithy_print_encoder();
    /**
    * get the pithy print interval for ingester, in ms,
    * the ingest used FFMPEG, or your tools, to read and transcode other stream 
    * to RTMP to SRS.
    */
    virtual int                 get_pithy_print_ingester();
    /**
    * get the pithy print interval for HLS, in ms,
    * the HLS used for IOS/android/PC, SRS will mux RTMP to HLS.
    */
    virtual int                 get_pithy_print_hls();
    /**
    * get the pithy print interval for Play, in ms,
    * the play is client or edge playing RTMP stream
    */
    virtual int                 get_pithy_print_play();
    /**
    * get the pithy print interval for edge, in ms,
    * the edge will get stream from upnode.
    */
    virtual int                 get_pithy_print_edge();
// vhost specified section
public:
    /**
    * get the vhost directive by vhost name.
    * @param vhost, the name of vhost to get.
    */
    virtual SrsConfDirective*   get_vhost(std::string vhost);
    /**
    * get all vhosts in config file.
    */
    virtual std::vector<SrsConfDirective*>  get_vhosts();
    /**
    * 
    */
    virtual bool                get_vhost_enabled(std::string vhost);
    /**
    * 
    */
    virtual bool                get_vhost_enabled(SrsConfDirective* vhost);
    /**
    * 
    */
    virtual SrsConfDirective*   get_vhost_on_connect(std::string vhost);
    /**
    * 
    */
    virtual SrsConfDirective*   get_vhost_on_close(std::string vhost);
    /**
    * 
    */
    virtual SrsConfDirective*   get_vhost_on_publish(std::string vhost);
    /**
    * 
    */
    virtual SrsConfDirective*   get_vhost_on_unpublish(std::string vhost);
    /**
    * 
    */
    virtual SrsConfDirective*   get_vhost_on_play(std::string vhost);
    /**
    * 
    */
    virtual SrsConfDirective*   get_vhost_on_stop(std::string vhost);
    /**
    * 
    */
    virtual SrsConfDirective*   get_vhost_on_dvr_hss_reap_flv(std::string vhost);
    /**
    * 
    */
    virtual bool                get_gop_cache(std::string vhost);
    /**
    * 
    */
    virtual bool                get_atc(std::string vhost);
    /**
    * 
    */
    virtual bool                get_atc_auto(std::string vhost);
    /**
    * 
    */
    virtual int                 get_time_jitter(std::string vhost);
    /**
    * 
    */
    virtual double              get_queue_length(std::string vhost);
    /**
    * 
    */
    virtual SrsConfDirective*   get_forward(std::string vhost);
    /**
    * 
    */
    virtual SrsConfDirective*   get_refer(std::string vhost);
    /**
    * 
    */
    virtual SrsConfDirective*   get_refer_play(std::string vhost);
    /**
    * 
    */
    virtual SrsConfDirective*   get_refer_publish(std::string vhost);
    /**
    * 
    */
    virtual int                 get_chunk_size(const std::string& vhost);
// bwct(bandwidth check tool) section
public:
    /**
    * 
    */
    virtual bool                get_bw_check_enabled(const std::string& vhost);
    /**
    * 
    */
    virtual std::string         get_bw_check_key(const std::string& vhost);
    /**
    * 
    */
    virtual int                 get_bw_check_interval_ms(const std::string& vhost);
    /**
    * 
    */
    virtual int                 get_bw_check_limit_kbps(const std::string& vhost);
// vhost edge section
public:
    /**
    * 
    */
    virtual bool                get_vhost_is_edge(std::string vhost);
    /**
    * 
    */
    virtual bool                get_vhost_is_edge(SrsConfDirective* vhost);
    /**
    * 
    */
    virtual SrsConfDirective*   get_vhost_edge_origin(std::string vhost);
    /**
    * 
    */
    virtual bool                get_vhost_edge_token_traverse(std::string vhost);
// vhost transcode section
public:
    /**
    * 
    */
    virtual SrsConfDirective*   get_transcode(std::string vhost, std::string scope);
    /**
    * 
    */
    virtual bool                get_transcode_enabled(SrsConfDirective* transcode);
    /**
    * 
    */
    virtual std::string         get_transcode_ffmpeg(SrsConfDirective* transcode);
    /**
    * 
    */
    virtual void                get_transcode_engines(SrsConfDirective* transcode, std::vector<SrsConfDirective*>& engines);
    /**
    * 
    */
    virtual bool                get_engine_enabled(SrsConfDirective* engine);
    /**
    * 
    */
    virtual std::string         get_engine_vcodec(SrsConfDirective* engine);
    /**
    * 
    */
    virtual int                 get_engine_vbitrate(SrsConfDirective* engine);
    /**
    * 
    */
    virtual double              get_engine_vfps(SrsConfDirective* engine);
    /**
    * 
    */
    virtual int                 get_engine_vwidth(SrsConfDirective* engine);
    /**
    * 
    */
    virtual int                 get_engine_vheight(SrsConfDirective* engine);
    /**
    * 
    */
    virtual int                 get_engine_vthreads(SrsConfDirective* engine);
    /**
    * 
    */
    virtual std::string         get_engine_vprofile(SrsConfDirective* engine);
    /**
    * 
    */
    virtual std::string         get_engine_vpreset(SrsConfDirective* engine);
    /**
    * 
    */
    virtual void                get_engine_vparams(SrsConfDirective* engine, std::vector<std::string>& vparams);
    /**
    * 
    */
    virtual void                get_engine_vfilter(SrsConfDirective* engine, std::vector<std::string>& vfilter);
    /**
    * 
    */
    virtual std::string         get_engine_acodec(SrsConfDirective* engine);
    /**
    * 
    */
    virtual int                 get_engine_abitrate(SrsConfDirective* engine);
    /**
    * 
    */
    virtual int                 get_engine_asample_rate(SrsConfDirective* engine);
    /**
    * 
    */
    virtual int                 get_engine_achannels(SrsConfDirective* engine);
    /**
    * 
    */
    virtual void                get_engine_aparams(SrsConfDirective* engine, std::vector<std::string>& aparams);
    /**
    * 
    */
    virtual std::string         get_engine_output(SrsConfDirective* engine);
// ingest section
public:
    /**
    * 
    */
    virtual void                get_ingesters(std::string vhost, std::vector<SrsConfDirective*>& ingeters);
    /**
    * 
    */
    virtual SrsConfDirective*   get_ingest_by_id(std::string vhost, std::string ingest_id);
    /**
    * 
    */
    virtual bool                get_ingest_enabled(SrsConfDirective* ingest);
    /**
    * 
    */
    virtual std::string         get_ingest_ffmpeg(SrsConfDirective* ingest);
    /**
    * 
    */
    virtual std::string         get_ingest_input_type(SrsConfDirective* ingest);
    /**
    * 
    */
    virtual std::string         get_ingest_input_url(SrsConfDirective* ingest);
// log section
public:
    /**
    * 
    */
    virtual bool                get_log_tank_file();
    /**
    * 
    */
    virtual std::string         get_log_level();
    /**
    * 
    */
    virtual std::string         get_log_file();
    /**
    * 
    */
    virtual bool                get_ffmpeg_log_enabled();
    /**
    * 
    */
    virtual std::string         get_ffmpeg_log_dir();
// hls section
private:
    /**
    * 
    */
    virtual SrsConfDirective*   get_hls(std::string vhost);
public:
    /**
    * 
    */
    virtual bool                get_hls_enabled(std::string vhost);
    /**
    * 
    */
    virtual std::string         get_hls_path(std::string vhost);
    /**
    * 
    */
    virtual double              get_hls_fragment(std::string vhost);
    /**
    * 
    */
    virtual double              get_hls_window(std::string vhost);
// dvr section
private:
    /**
    * 
    */
    virtual SrsConfDirective*   get_dvr(std::string vhost);
public:
    /**
    * 
    */
    virtual bool                get_dvr_enabled(std::string vhost);
    /**
    * 
    */
    virtual std::string         get_dvr_path(std::string vhost);
    /**
    * 
    */
    virtual std::string         get_dvr_plan(std::string vhost);
    /**
    * 
    */
    virtual int                 get_dvr_duration(std::string vhost);
    /**
    * 
    */
    virtual int                 get_dvr_time_jitter(std::string vhost);
// http api section
private:
    /**
    * 
    */
    virtual SrsConfDirective*   get_http_api();
public:
    /**
    * 
    */
    virtual bool                get_http_api_enabled();
    /**
    * 
    */
    virtual bool                get_http_api_enabled(SrsConfDirective* conf);
    /**
    * 
    */
    virtual int                 get_http_api_listen();
// http stream section
private:
    /**
    * 
    */
    virtual SrsConfDirective*   get_http_stream();
public:
    /**
    * 
    */
    virtual bool                get_http_stream_enabled();
    /**
    * 
    */
    virtual bool                get_http_stream_enabled(SrsConfDirective* conf);
    /**
    * 
    */
    virtual int                 get_http_stream_listen();
    /**
    * 
    */
    virtual std::string         get_http_stream_dir();
public:
    /**
    * 
    */
    virtual bool                get_vhost_http_enabled(std::string vhost);
    /**
    * 
    */
    virtual std::string         get_vhost_http_mount(std::string vhost);
    /**
    * 
    */
    virtual std::string         get_vhost_http_dir(std::string vhost);
// http heartbeart section
private:
    /**
    * 
    */
    virtual SrsConfDirective*   get_heartbeart();
public:
    /**
    * 
    */
    virtual bool                get_heartbeat_enabled();
    /**
    * 
    */
    virtual int64_t             get_heartbeat_interval();
    /**
    * 
    */
    virtual std::string         get_heartbeat_url();
    /**
    * 
    */
    virtual std::string         get_heartbeat_device_id();
    /**
    * 
    */
    virtual int                 get_heartbeat_device_index();
    /**
    * 
    */
    virtual bool                get_heartbeat_summaries();
};

namespace _srs_internal
{
    /**
    * the buffer of config content.
    */
    class SrsConfigBuffer
    {
    protected:
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
    public:
        SrsConfigBuffer();
        virtual ~SrsConfigBuffer();
    public:
        /**
        * fullfill the buffer with content of file specified by filename.
        */
        virtual int fullfill(const char* filename);
        /**
        * whether buffer is empty.
        */
        virtual bool empty();
    };
};

/**
* deep compare directive.
*/
bool srs_directive_equals(SrsConfDirective* a, SrsConfDirective* b);

// global config
extern SrsConfig* _srs_config;

#endif