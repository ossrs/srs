/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

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

#ifndef SRS_APP_CONFIG_HPP
#define SRS_APP_CONFIG_HPP

/*
#include <srs_app_config.hpp>
*/
#include <srs_core.hpp>

#include <vector>
#include <string>

#include <srs_app_reload.hpp>

///////////////////////////////////////////////////////////
// default consts values
///////////////////////////////////////////////////////////
#define SRS_CONF_DEFAULT_PID_FILE "./objs/srs.pid"
#define SRS_CONF_DEFAULT_LOG_FILE "./objs/srs.log"
#define SRS_CONF_DEFAULT_LOG_LEVEL "trace"
#define SRS_CONF_DEFAULT_LOG_TANK_CONSOLE "console"
#define SRS_CONF_DEFAULT_COFNIG_FILE "conf/srs.conf"
#define SRS_CONF_DEFAULT_FF_LOG_DIR "./objs"

#define SRS_CONF_DEFAULT_MAX_CONNECTIONS 1000
#define SRS_CONF_DEFAULT_HLS_PATH "./objs/nginx/html"
#define SRS_CONF_DEFAULT_HLS_M3U8_FILE "[app]/[stream].m3u8"
#define SRS_CONF_DEFAULT_HLS_TS_FILE "[app]/[stream]-[seq].ts"
#define SRS_CONF_DEFAULT_HLS_TS_FLOOR "off"
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
#define SRS_CONF_DEFAULT_DVR_PATH "./objs/nginx/html/[app]/[stream].[timestamp].flv"
#define SRS_CONF_DEFAULT_DVR_PLAN_SESSION "session"
#define SRS_CONF_DEFAULT_DVR_PLAN_SEGMENT "segment"
#define SRS_CONF_DEFAULT_DVR_PLAN_APPEND "append"
#define SRS_CONF_DEFAULT_DVR_PLAN SRS_CONF_DEFAULT_DVR_PLAN_SESSION
#define SRS_CONF_DEFAULT_DVR_DURATION 30
#define SRS_CONF_DEFAULT_TIME_JITTER "full"
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

#define SRS_CONF_DEFAULT_STATS_NETWORK_DEVICE_INDEX 0

#define SRS_CONF_DEFAULT_PITHY_PRINT_MS 10000

#define SRS_CONF_DEFAULT_INGEST_TYPE_FILE "file"
#define SRS_CONF_DEFAULT_INGEST_TYPE_STREAM "stream"

#define SRS_CONF_DEFAULT_TRANSCODE_IFORMAT "flv"
#define SRS_CONF_DEFAULT_TRANSCODE_OFORMAT "flv"

// hds default value
#define SRS_CONF_DEFAULT_HDS_PATH       "./objs/nginx/html"
#define SRS_CONF_DEFAULT_HDS_WINDOW     (60)
#define SRS_CONF_DEFAULT_HDS_FRAGMENT   (10)

namespace _srs_internal
{
    class SrsConfigBuffer;
}

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
* @remark, allow empty directive, for example: "dir0 {}"
* @remark, don't allow empty name, for example: ";" or "{dir0 arg0;}
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
    /**
    * whether current directive is stream_caster.
    */
    virtual bool is_stream_caster();
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
    * @param line_start, the actual start line of directive.
    * @return, an error code indicates error or has child-directives.
    */
    virtual int read_token(_srs_internal::SrsConfigBuffer* buffer, std::vector<std::string>& args, int& line_start);
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
protected:
    /**
    * reload from the config.
    * @remark, use protected for the utest to override with mock.
    */
    virtual int reload_conf(SrsConfig* conf);
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
    /**
    * get the config file path.
    */
    virtual std::string config();
private:
    /**
    * parse each argv.
    */
    virtual int parse_argv(int& i, char** argv);
    /**
    * print help and exit.
    */
    virtual void print_help(char** argv);
public:
    /**
    * parse the config file, which is specified by cli.
    */
    virtual int parse_file(const char* filename);
    /**
    * check the parsed config.
    */
    virtual int check_config();
protected:
    /**
    * parse config from the buffer.
    * @param buffer, the config buffer, user must delete it.
    * @remark, use protected for the utest to override with mock.
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
    virtual std::vector<std::string>        get_listens();
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
    * get pithy print pulse ms,
    * for example, all rtmp connections only print one message
    * every this interval in ms.
    */
    virtual int                 get_pithy_print_ms();
// stream_caster section
public:
    /**
    * get all stream_caster in config file.
    */
    virtual std::vector<SrsConfDirective*>  get_stream_casters();
    /**
    * get whether the specified stream_caster is enabled.
    */
    virtual bool                get_stream_caster_enabled(SrsConfDirective* sc);
    /**
    * get the engine of stream_caster, the caster config.
    */
    virtual std::string         get_stream_caster_engine(SrsConfDirective* sc);
    /**
    * get the output rtmp url of stream_caster, the output config.
    */
    virtual std::string         get_stream_caster_output(SrsConfDirective* sc);
    /**
    * get the listen port of stream caster.
    */
    virtual int                 get_stream_caster_listen(SrsConfDirective* sc);
    /**
    * get the min udp port for rtp of stream caster rtsp.
    */
    virtual int                 get_stream_caster_rtp_port_min(SrsConfDirective* sc);
    /**
    * get the max udp port for rtp of stream caster rtsp.
    */
    virtual int                 get_stream_caster_rtp_port_max(SrsConfDirective* sc);
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
    * whether vhost is enabled
    * @param vhost, the vhost name.
    * @return true when vhost is ok; otherwise, false.
    */
    virtual bool                get_vhost_enabled(std::string vhost);
    /**
    * whether vhost is enabled
    * @param vhost, the vhost directive.
    * @return true when vhost is ok; otherwise, false.
    */
    virtual bool                get_vhost_enabled(SrsConfDirective* vhost);
    /**
    * whether gop_cache is enabled of vhost.
    * gop_cache used to cache last gop, for client to fast startup.
    * @return true when gop_cache is ok; otherwise, false.
    * @remark, default true.
    */
    virtual bool                get_gop_cache(std::string vhost);
    /**
    * whether debug_srs_upnode is enabled of vhost.
    * debug_srs_upnode is very important feature for tracable log,
    * but some server, for instance, flussonic donot support it.
    * @see https://github.com/winlinvip/simple-rtmp-server/issues/160
    * @return true when debug_srs_upnode is ok; otherwise, false.
    * @remark, default true.
    */
    virtual bool                get_debug_srs_upnode(std::string vhost);
    /**
    * whether atc is enabled of vhost.
    * atc always use encoder timestamp, SRS never adjust the time.
    * @return true when atc is ok; otherwise, false.
    * @remark, default false.
    */
    virtual bool                get_atc(std::string vhost);
    /**
    * whether atc_auto is enabled of vhost.
    * atc_auto used to auto enable atc, when metadata specified the bravo_atc.
    * @return true when atc_auto is ok; otherwise, false.
    * @remark, default true.
    */
    virtual bool                get_atc_auto(std::string vhost);
    /**
    * get the time_jitter algorithm.
    * @return the time_jitter algorithm, defined in SrsRtmpJitterAlgorithm.
    * @remark, default full.
    */
    virtual int                 get_time_jitter(std::string vhost);
    /**
    * get the cache queue length, in seconds.
    * when exceed the queue length, drop packet util I frame.
    * @remark, default 10.
    */
    virtual double              get_queue_length(std::string vhost);
    /**
    * get the refer antisuck directive.
    * each args of directive is a refer config.
    * when the client refer(pageUrl) not match the refer config,
    * SRS will reject the connection.
    * @remark, default NULL.
    */
    virtual SrsConfDirective*   get_refer(std::string vhost);
    /**
    * get the play refer, refer for play clients.
    * @remark, default NULL.
    */
    virtual SrsConfDirective*   get_refer_play(std::string vhost);
    /**
    * get the publish refer, refer for publish clients.
    * @remark, default NULL.
    */
    virtual SrsConfDirective*   get_refer_publish(std::string vhost);
    /**
    * get the chunk size of vhost.
    * @param vhost, the vhost to get the chunk size. use global if not specified.
    *       empty string to get the global.
    * @remark, default 60000.
    */
    virtual int                 get_chunk_size(std::string vhost);
    /**
    * whether mr is enabled for vhost.
    * @param vhost, the vhost to get the mr.
    */
    virtual bool                get_mr_enabled(std::string vhost);
    /**
    * get the mr sleep time in ms for vhost.
    * @param vhost, the vhost to get the mr sleep time.
    */
    // TODO: FIXME: add utest for mr config.
    virtual int                 get_mr_sleep_ms(std::string vhost);
    /**
    * get the mw sleep time in ms for vhost.
    * @param vhost, the vhost to get the mw sleep time.
    */
    // TODO: FIXME: add utest for mw config.
    virtual int                 get_mw_sleep_ms(std::string vhost);
    /**
    * whether min latency mode enabled.
    * @param vhost, the vhost to get the min_latency.
    */
    // TODO: FIXME: add utest for min_latency.
    virtual bool                get_realtime_enabled(std::string vhost);
private:
    /**
    * get the global chunk size.
    */
    virtual int                 get_global_chunk_size();
// forward section
public:
    /**
    * get the forward directive of vhost.
    */
    virtual SrsConfDirective*   get_forward(std::string vhost);
// http_hooks section
private:
    /**
    * get the http_hooks directive of vhost.
    */
    virtual SrsConfDirective*   get_vhost_http_hooks(std::string vhost);
public:
    /**
    * whether vhost http-hooks enabled.
    * @remark, if not enabled, donot callback all http hooks.
    */
    virtual bool                get_vhost_http_hooks_enabled(std::string vhost);
    /**
    * get the on_connect callbacks of vhost.
    * @return the on_connect callback directive, the args is the url to callback.
    */
    virtual SrsConfDirective*   get_vhost_on_connect(std::string vhost);
    /**
    * get the on_close callbacks of vhost.
    * @return the on_close callback directive, the args is the url to callback.
    */
    virtual SrsConfDirective*   get_vhost_on_close(std::string vhost);
    /**
    * get the on_publish callbacks of vhost.
    * @return the on_publish callback directive, the args is the url to callback.
    */
    virtual SrsConfDirective*   get_vhost_on_publish(std::string vhost);
    /**
    * get the on_unpublish callbacks of vhost.
    * @return the on_unpublish callback directive, the args is the url to callback.
    */
    virtual SrsConfDirective*   get_vhost_on_unpublish(std::string vhost);
    /**
    * get the on_play callbacks of vhost.
    * @return the on_play callback directive, the args is the url to callback.
    */
    virtual SrsConfDirective*   get_vhost_on_play(std::string vhost);
    /**
    * get the on_stop callbacks of vhost.
    * @return the on_stop callback directive, the args is the url to callback.
    */
    virtual SrsConfDirective*   get_vhost_on_stop(std::string vhost);
    /**
     * get the on_dvr callbacks of vhost.
     * @return the on_dvr callback directive, the args is the url to callback.
     */
    virtual SrsConfDirective*   get_vhost_on_dvr(std::string vhost);
    /**
     * get the on_hls callbacks of vhost.
     * @return the on_hls callback directive, the args is the url to callback.
     */
    virtual SrsConfDirective*   get_vhost_on_hls(std::string vhost);
// bwct(bandwidth check tool) section
public:
    /**
    * whether bw check enabled for vhost.
    * if enabled, serve all clients with bandwidth check services.
    * oterwise, serve all cleints with stream.
    */
    virtual bool                get_bw_check_enabled(std::string vhost);
    /**
    * the key of server, if client key mot match, reject.
    */
    virtual std::string         get_bw_check_key(std::string vhost);
    /**
    * the check interval, in ms.
    * if the client request check in very short time(in the interval),
    * SRS will reject client.
    * @remark this is used to prevent the bandwidth check attack.
    */
    virtual int                 get_bw_check_interval_ms(std::string vhost);
    /**
    * the max kbps that user can test,
    * if exceed the kbps, server will slowdown the send-recv.
    * @remark this is used to protect the service bandwidth.
    */
    virtual int                 get_bw_check_limit_kbps(std::string vhost);
// vhost edge section
public:
    /**
    * whether vhost is edge mode.
    * for edge, publish client will be proxyed to upnode,
    * for edge, play client will share a connection to get stream from upnode.
    */
    virtual bool                get_vhost_is_edge(std::string vhost);
    /**
    * whether vhost is edge mode.
    * for edge, publish client will be proxyed to upnode,
    * for edge, play client will share a connection to get stream from upnode.
    */
    virtual bool                get_vhost_is_edge(SrsConfDirective* vhost);
    /**
    * get the origin config of edge,
    * specifies the origin ip address, port.
    */
    virtual SrsConfDirective*   get_vhost_edge_origin(std::string vhost);
    /**
    * whether edge token tranverse is enabled,
    * if true, edge will send connect origin to verfy the token of client.
    * for example, we verify all clients on the origin FMS by server-side as,
    * all clients connected to edge must be tranverse to origin to verify.
    */
    virtual bool                get_vhost_edge_token_traverse(std::string vhost);
// vhost security section
public:
    /**
    * whether the secrity of vhost enabled.
    */
    virtual bool                get_security_enabled(std::string vhost);
    /**
    * get the security rules.
    */
    virtual SrsConfDirective*   get_security_rules(std::string vhost);
// vhost transcode section
public:
    /**
    * get the transcode directive of vhost in specified scope.
    * @param vhost, the vhost name to get the transcode directive.
    * @param scope, the scope, empty to get all. for example, user can transcode
    *       the app scope stream, by config with app:
    *                   transcode live {...}
    *       when the scope is "live", this directive is matched.
    *       the scope can be: empty for all, app, app/stream.
    * @remark, please see the samples of full.conf, the app.transcode.srs.com
    *       and stream.transcode.srs.com.
    */
    virtual SrsConfDirective*   get_transcode(std::string vhost, std::string scope);
    /**
    * whether the transcode directive is enabled.
    */
    virtual bool                get_transcode_enabled(SrsConfDirective* transcode);
    /**
    * get the ffmpeg tool path of transcode.
    */
    virtual std::string         get_transcode_ffmpeg(SrsConfDirective* transcode);
    /**
    * get the engines of transcode.
    */
    virtual std::vector<SrsConfDirective*>      get_transcode_engines(SrsConfDirective* transcode);
    /**
    * whether the engine is enabled.
    */
    virtual bool                get_engine_enabled(SrsConfDirective* engine);
    /**
    * get the iformat of engine
    */
    virtual std::string         get_engine_iformat(SrsConfDirective* engine);
    /**
    * get the vfilter of engine,
    * the video filter set before the vcodec of FFMPEG.
    */
    virtual std::vector<std::string> get_engine_vfilter(SrsConfDirective* engine);
    /**
    * get the vcodec of engine,
    * the codec of video, copy or libx264
    */
    virtual std::string         get_engine_vcodec(SrsConfDirective* engine);
    /**
    * get the vbitrate of engine,
    * the bitrate in kbps of video, for example, 800kbps
    */
    virtual int                 get_engine_vbitrate(SrsConfDirective* engine);
    /**
    * get the vfps of engine.
    * the video fps, for example, 25fps
    */
    virtual double              get_engine_vfps(SrsConfDirective* engine);
    /**
    * get the vwidth of engine,
    * the video width, for example, 1024
    */
    virtual int                 get_engine_vwidth(SrsConfDirective* engine);
    /**
    * get the vheight of engine,
    * the video height, for example, 576
    */
    virtual int                 get_engine_vheight(SrsConfDirective* engine);
    /**
    * get the vthreads of engine,
    * the video transcode libx264 threads, for instance, 8
    */
    virtual int                 get_engine_vthreads(SrsConfDirective* engine);
    /**
    * get the vprofile of engine,
    * the libx264 profile, can be high,main,baseline
    */
    virtual std::string         get_engine_vprofile(SrsConfDirective* engine);
    /**
    * get the vpreset of engine,
    * the libx264 preset, can be ultrafast,superfast,veryfast,faster,fast,medium,slow,slower,veryslow,placebo
    */
    virtual std::string         get_engine_vpreset(SrsConfDirective* engine);
    /**
    * get the additional video params.
    */
    virtual std::vector<std::string> get_engine_vparams(SrsConfDirective* engine);
    /**
    * get the acodec of engine,
    * the audio codec can be copy or libaacplus
    */
    virtual std::string         get_engine_acodec(SrsConfDirective* engine);
    /**
    * get the abitrate of engine,
    * the audio bitrate in kbps, for instance, 64kbps.
    */
    virtual int                 get_engine_abitrate(SrsConfDirective* engine);
    /**
    * get the asample_rate of engine,
    * the audio sample_rate, for instance, 44100HZ
    */
    virtual int                 get_engine_asample_rate(SrsConfDirective* engine);
    /**
    * get the achannels of engine,
    * the audio channel, for instance, 1 for mono, 2 for stereo.
    */
    virtual int                 get_engine_achannels(SrsConfDirective* engine);
    /**
    * get the aparams of engine,
    * the audio additional params.
    */
    virtual std::vector<std::string> get_engine_aparams(SrsConfDirective* engine);
    /**
    * get the oformat of engine
    */
    virtual std::string         get_engine_oformat(SrsConfDirective* engine);
    /**
    * get the output of engine, for example, rtmp://localhost/live/livestream,
    * @remark, we will use some variable, for instance, [vhost] to substitude with vhost.
    */
    virtual std::string         get_engine_output(SrsConfDirective* engine);
// vhost ingest section
public:
    /**
    * get the ingest directives of vhost.
    */
    virtual std::vector<SrsConfDirective*> get_ingesters(std::string vhost);
    /**
    * get specified ingest.
    */
    virtual SrsConfDirective*   get_ingest_by_id(std::string vhost, std::string ingest_id);
    /**
    * whether ingest is enalbed.
    */
    virtual bool                get_ingest_enabled(SrsConfDirective* ingest);
    /**
    * get the ingest ffmpeg tool
    */
    virtual std::string         get_ingest_ffmpeg(SrsConfDirective* ingest);
    /**
    * get the ingest input type, file or stream.
    */
    virtual std::string         get_ingest_input_type(SrsConfDirective* ingest);
    /**
    * get the ingest input url.
    */
    virtual std::string         get_ingest_input_url(SrsConfDirective* ingest);
// log section
public:
    /**
    * whether log to file.
    */
    virtual bool                get_log_tank_file();
    /**
    * get the log level.
    */
    virtual std::string         get_log_level();
    /**
    * get the log file path.
    */
    virtual std::string         get_log_file();
    /**
    * whether ffmpeg log enabled
    */
    virtual bool                get_ffmpeg_log_enabled();
    /**
    * the ffmpeg log dir.
    * @remark, /dev/null to disable it.
    */
    virtual std::string         get_ffmpeg_log_dir();
// hls section
private:
    /**
    * get the hls directive of vhost.
    */
    virtual SrsConfDirective*   get_hls(std::string vhost);
public:
    /**
    * whether HLS is enabled.
    */
    virtual bool                get_hls_enabled(std::string vhost);
    /**
    * get the HLS m3u8 list ts segment entry prefix info.
    */
    virtual std::string         get_hls_entry_prefix(std::string vhost);
    /**
     * get the HLS ts/m3u8 file store path.
     */
    virtual std::string         get_hls_path(std::string vhost);
    /**
     * get the HLS m3u8 file path template.
     */
    virtual std::string         get_hls_m3u8_file(std::string vhost);
    /**
     * get the HLS ts file path template.
     */
    virtual std::string         get_hls_ts_file(std::string vhost);
    /**
     * whether enable the floor(timestamp/hls_fragment) for variable timestamp.
     */
    virtual bool                get_hls_ts_floor(std::string vhost);
    /**
    * get the hls fragment time, in seconds.
    */
    virtual double              get_hls_fragment(std::string vhost);
    /**
    * get the hls td(target duration) ratio.
    */
    virtual double              get_hls_td_ratio(std::string vhost);
    /**
     * get the hls aof(audio overflow) ratio.
     */
    virtual double              get_hls_aof_ratio(std::string vhost);
    /**
    * get the hls window time, in seconds.
    * a window is a set of ts, the ts collection in m3u8.
    * @remark SRS will delete the ts exceed the window.
    */
    virtual double              get_hls_window(std::string vhost);
    /**
    * get the hls hls_on_error config.
    * the ignore will ignore error and disable hls.
    * the disconnect will disconnect publish connection.
    * @see https://github.com/winlinvip/simple-rtmp-server/issues/264
    */
    virtual std::string         get_hls_on_error(std::string vhost);
    /**
    * get the HLS storage type.
    */
    virtual std::string         get_hls_storage(std::string vhost);
    /**
    * get the HLS mount url for HTTP server.
    */
    virtual std::string         get_hls_mount(std::string vhost);
    /**
    * get the HLS default audio codec.
    */
    virtual std::string         get_hls_acodec(std::string vhost);
    /**
    * get the HLS default video codec.
    */
    virtual std::string         get_hls_vcodec(std::string vhost);

    // hds section
private:
    /**
    * get the hds directive of vhost.
    */
    virtual SrsConfDirective*   get_hds(const std::string &vhost);
public:
    /**
    * whether HDS is enabled.
    */
    virtual bool                get_hds_enabled(const std::string &vhost);
    /**
    * get the HDS file store path.
    */
    virtual std::string         get_hds_path(const std::string &vhost);
    /**
    * get the hds fragment time, in seconds.
    */
    virtual double              get_hds_fragment(const std::string &vhost);
    /**
    * get the hds window time, in seconds.
    * a window is a set of hds fragments.
    */
    virtual double              get_hds_window(const std::string &vhost);

// dvr section
private:
    /**
    * get the dvr directive.
    */
    virtual SrsConfDirective*   get_dvr(std::string vhost);
public:
    /**
    * whether dvr is enabled.
    */
    virtual bool                get_dvr_enabled(std::string vhost);
    /**
    * get the dvr path, the flv file to save in.
    */
    virtual std::string         get_dvr_path(std::string vhost);
    /**
    * get the plan of dvr, how to reap the flv file.
    */
    virtual std::string         get_dvr_plan(std::string vhost);
    /**
    * get the duration of dvr flv.
    */
    virtual int                 get_dvr_duration(std::string vhost);
    /**
    * whether wait keyframe to reap segment.
    */
    virtual bool                get_dvr_wait_keyframe(std::string vhost);
    /**
    * get the time_jitter algorithm for dvr.
    */
    virtual int                 get_dvr_time_jitter(std::string vhost);
// http api section
private:
    /**
    * get the http api directive.
    */
    virtual SrsConfDirective*   get_http_api();
    /**
    * whether http api enabled
    */
    virtual bool                get_http_api_enabled(SrsConfDirective* conf);
public:
    /**
    * whether http api enabled.
    */
    virtual bool                get_http_api_enabled();
    /**
    * get the http api listen port.
    */
    virtual std::string         get_http_api_listen();
    /**
    * whether enable crossdomain for http api.
    */
    virtual bool                get_http_api_crossdomain();
// http stream section
private:
    /**
    * get the http stream directive.
    */
    virtual SrsConfDirective*   get_http_stream();
    /**
    * whether http stream enabled.
    */
    virtual bool                get_http_stream_enabled(SrsConfDirective* conf);
public:
    /**
    * whether http stream enabled.
    */
    virtual bool                get_http_stream_enabled();
    /**
    * get the http stream listen port.
    */
    virtual std::string         get_http_stream_listen();
    /**
    * get the http stream root dir.
    */
    virtual std::string         get_http_stream_dir();
public:
    /**
    * get whether vhost enabled http stream
    */
    virtual bool                get_vhost_http_enabled(std::string vhost);
    /**
    * get the http mount point for vhost.
    * for example, http://vhost/live/livestream
    */
    virtual std::string         get_vhost_http_mount(std::string vhost);
    /**
    * get the http dir for vhost.
    * the path on disk for mount root of http vhost.
    */
    virtual std::string         get_vhost_http_dir(std::string vhost);
// flv live streaming section
public:
    /**
    * get whether vhost enabled http flv live stream
    */
    virtual bool                get_vhost_http_remux_enabled(std::string vhost);
    /**
    * get the fast cache duration for http audio live stream.
    */
    virtual double              get_vhost_http_remux_fast_cache(std::string vhost);
    /**
    * get the http flv live stream mount point for vhost.
    * used to generate the flv stream mount path.
    */
    virtual std::string         get_vhost_http_remux_mount(std::string vhost);
    /**
    * get whether the hstrs(http stream trigger rtmp source) enabled.
    */
    virtual bool                get_vhost_http_remux_hstrs(std::string vhost);
// http heartbeart section
private:
    /**
    * get the heartbeat directive.
    */
    virtual SrsConfDirective*   get_heartbeart();
public:
    /**
    * whether heartbeat enabled.
    */
    virtual bool                get_heartbeat_enabled();
    /**
    * get the heartbeat interval, in ms.
    */
    virtual int64_t             get_heartbeat_interval();
    /**
    * get the heartbeat report url.
    */
    virtual std::string         get_heartbeat_url();
    /**
    * get the device id of heartbeat, to report to server.
    */
    virtual std::string         get_heartbeat_device_id();
    /**
    * whether report with summaries of http api: /api/v1/summaries.
    */
    virtual bool                get_heartbeat_summaries();
// stats section
private:
    /**
    * get the stats directive.
    */
    virtual SrsConfDirective*   get_stats();
public:
    /**
    * get the network device index, used to retrieve the ip of device,
    * for heartbeat to report to server, or to get the local ip.
    * for example, 0 means the eth0 maybe.
    */
    virtual int                 get_stats_network();
    /**
    * get the disk stat device name list.
    * the device name configed in args of directive.
    * @return the disk device name to stat. NULL if not configed.
    */
    virtual SrsConfDirective*   get_stats_disk_device();
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

