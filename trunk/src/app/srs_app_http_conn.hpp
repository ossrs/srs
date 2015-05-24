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

#ifndef SRS_APP_HTTP_CONN_HPP
#define SRS_APP_HTTP_CONN_HPP

/*
#include <srs_app_http_conn.hpp>
*/

#include <srs_core.hpp>

#ifdef SRS_AUTO_HTTP_PARSER
#include <http_parser.h>
#endif

#if defined(SRS_AUTO_HTTP_PARSER) || defined(SRS_AUTO_HTTP_SERVER)

#include <map>
#include <string>
#include <vector>

#include <srs_app_st.hpp>
#include <srs_http_stack.hpp>
#include <srs_app_reload.hpp>
#include <srs_kernel_file.hpp>
#include <srs_app_thread.hpp>
#include <srs_app_conn.hpp>

class SrsServer;
class SrsSource;
class SrsRequest;
class SrsConsumer;
class SrsStSocket;
class SrsTsEncoder;
class SrsAacEncoder;
class SrsMp3Encoder;
class SrsFlvEncoder;
class SrsHttpParser;
class ISrsHttpMessage;
class SrsHttpHandler;
class SrsMessageQueue;
class SrsSharedPtrMessage;
class SrsRequest;
class SrsFastBuffer;
class SrsHttpUri;
class SrsConnection;
class SrsHttpMessage;

#endif

#ifdef SRS_AUTO_HTTP_PARSER

// for HTTP FLV, each video/audio packet is send by 3 iovs,
// while each iov is send by 4 sub iovs, that is needs 3 chunk header,
// suppose each header is 16 length, 3*16=48 is ok.
// that is, 512 can used for 16 iovs to send.
#define SRS_HTTP_HEADER_CACHE_SIZE 512

/**
 * response writer use st socket
 */
class SrsHttpResponseWriter : public ISrsHttpResponseWriter
{
private:
    SrsStSocket* skt;
    SrsHttpHeader* hdr;
    char header_cache[SRS_HTTP_HEADER_CACHE_SIZE];
private:
    // reply header has been (logically) written
    bool header_wrote;
    // status code passed to WriteHeader
    int status;
private:
    // explicitly-declared Content-Length; or -1
    int64_t content_length;
    // number of bytes written in body
    int64_t written;
private:
    // wroteHeader tells whether the header's been written to "the
    // wire" (or rather: w.conn.buf). this is unlike
    // (*response).wroteHeader, which tells only whether it was
    // logically written.
    bool header_sent;
public:
    SrsHttpResponseWriter(SrsStSocket* io);
    virtual ~SrsHttpResponseWriter();
public:
    virtual int final_request();
    virtual SrsHttpHeader* header();
    virtual int write(char* data, int size);
    virtual int writev(iovec* iov, int iovcnt, ssize_t* pnwrite);
    virtual void write_header(int code);
    virtual int send_header(char* data, int size);
};

/**
 * response reader use st socket.
 */
class SrsHttpResponseReader : virtual public ISrsHttpResponseReader
{
private:
    SrsStSocket* skt;
    SrsHttpMessage* owner;
    SrsFastBuffer* buffer;
    bool is_eof;
    // the left bytes in chunk.
    int nb_left_chunk;
    // the number of bytes of current chunk.
    int nb_chunk;
    // already read total bytes.
    int64_t nb_total_read;
public:
    SrsHttpResponseReader(SrsHttpMessage* msg, SrsStSocket* io);
    virtual ~SrsHttpResponseReader();
public:
    /**
     * initialize the response reader with buffer.
     */
    virtual int initialize(SrsFastBuffer* buffer);
    // interface ISrsHttpResponseReader
public:
    virtual bool eof();
    virtual int read(char* data, int nb_data, int* nb_read);
private:
    virtual int read_chunked(char* data, int nb_data, int* nb_read);
    virtual int read_specified(char* data, int nb_data, int* nb_read);
};

// for http header.
typedef std::pair<std::string, std::string> SrsHttpHeaderField;

// A Request represents an HTTP request received by a server
// or to be sent by a client.
//
// The field semantics differ slightly between client and server
// usage. In addition to the notes on the fields below, see the
// documentation for Request.Write and RoundTripper.
/**
 * the http message, request or response.
 */
class SrsHttpMessage : public ISrsHttpMessage
{
private:
    /**
     * parsed url.
     */
    std::string _url;
    /**
     * the extension of file, for example, .flv
     */
    std::string _ext;
    /**
     * parsed http header.
     */
    http_parser _header;
    /**
     * body object, reader object.
     * @remark, user can get body in string by get_body().
     */
    SrsHttpResponseReader* _body;
    /**
     * whether the body is chunked.
     */
    bool chunked;
    /**
     * whether the request indicates should keep alive
     * for the http connection.
     */
    bool keep_alive;
    /**
     * uri parser
     */
    SrsHttpUri* _uri;
    /**
     * use a buffer to read and send ts file.
     */
    // TODO: FIXME: remove it.
    char* _http_ts_send_buffer;
    // http headers
    std::vector<SrsHttpHeaderField> _headers;
    // the query map
    std::map<std::string, std::string> _query;
    // the transport connection, can be NULL.
    SrsConnection* conn;
public:
    SrsHttpMessage(SrsStSocket* io, SrsConnection* c);
    virtual ~SrsHttpMessage();
public:
    /**
     * set the original messages, then update the message.
     */
    virtual int update(std::string url, http_parser* header,
                       SrsFastBuffer* body, std::vector<SrsHttpHeaderField>& headers
                       );
private:
    virtual SrsConnection* connection();
public:
    virtual u_int8_t method();
    virtual u_int16_t status_code();
    /**
     * method helpers.
     */
    virtual std::string method_str();
    virtual bool is_http_get();
    virtual bool is_http_put();
    virtual bool is_http_post();
    virtual bool is_http_delete();
    virtual bool is_http_options();
    /**
     * whether body is chunked encoding, for reader only.
     */
    virtual bool is_chunked();
    /**
     * whether should keep the connection alive.
     */
    virtual bool is_keep_alive();
    /**
     * the uri contains the host and path.
     */
    virtual std::string uri();
    /**
     * the url maybe the path.
     */
    virtual std::string url();
    virtual std::string host();
    virtual std::string path();
    virtual std::string ext();
public:
    /**
     * read body to string.
     * @remark for small http body.
     */
    virtual int body_read_all(std::string& body);
    /**
     * get the body reader, to read one by one.
     * @remark when body is very large, or chunked, use this.
     */
    virtual ISrsHttpResponseReader* body_reader();
    /**
     * the content length, -1 for chunked or not set.
     */
    virtual int64_t content_length();
    /**
     * get the param in query string,
     * for instance, query is "start=100&end=200",
     * then query_get("start") is "100", and query_get("end") is "200"
     */
    virtual std::string query_get(std::string key);
    /**
     * get the headers.
     */
    virtual int request_header_count();
    virtual std::string request_header_key_at(int index);
    virtual std::string request_header_value_at(int index);
    virtual std::string get_request_header(std::string name);
public:
    /**
     * convert the http message to a request.
     * @remark user must free the return request.
     */
    virtual SrsRequest* to_request(std::string vhost);
};

/**
 * wrapper for http-parser,
 * provides HTTP message originted service.
 */
class SrsHttpParser
{
private:
    http_parser_settings settings;
    http_parser parser;
    // the global parse buffer.
    SrsFastBuffer* buffer;
private:
    // http parse data, reset before parse message.
    bool expect_field_name;
    std::string field_name;
    std::string field_value;
    SrsHttpParseState state;
    http_parser header;
    std::string url;
    std::vector<SrsHttpHeaderField> headers;
    int header_parsed;
public:
    SrsHttpParser();
    virtual ~SrsHttpParser();
public:
    /**
     * initialize the http parser with specified type,
     * one parser can only parse request or response messages.
     */
    virtual int initialize(enum http_parser_type type);
    /**
     * always parse a http message,
     * that is, the *ppmsg always NOT-NULL when return success.
     * or error and *ppmsg must be NULL.
     * @remark, if success, *ppmsg always NOT-NULL, *ppmsg always is_complete().
     */
    virtual int parse_message(SrsStSocket* skt, SrsConnection* conn, ISrsHttpMessage** ppmsg);
private:
    /**
     * parse the HTTP message to member field: msg.
     */
    virtual int parse_message_imp(SrsStSocket* skt);
private:
    static int on_message_begin(http_parser* parser);
    static int on_headers_complete(http_parser* parser);
    static int on_message_complete(http_parser* parser);
    static int on_url(http_parser* parser, const char* at, size_t length);
    static int on_header_field(http_parser* parser, const char* at, size_t length);
    static int on_header_value(http_parser* parser, const char* at, size_t length);
    static int on_body(http_parser* parser, const char* at, size_t length);
};

/**
 * used to resolve the http uri.
 */
class SrsHttpUri
{
private:
    std::string url;
    std::string schema;
    std::string host;
    int port;
    std::string path;
    std::string query;
public:
    SrsHttpUri();
    virtual ~SrsHttpUri();
public:
    /**
     * initialize the http uri.
     */
    virtual int initialize(std::string _url);
public:
    virtual const char* get_url();
    virtual const char* get_schema();
    virtual const char* get_host();
    virtual int get_port();
    virtual const char* get_path();
    virtual const char* get_query();
private:
    /**
     * get the parsed url field.
     * @return return empty string if not set.
     */
    virtual std::string get_uri_field(std::string uri, http_parser_url* hp_u, http_parser_url_fields field);
};

#endif

#ifdef SRS_AUTO_HTTP_SERVER

/**
* the flv vod stream supports flv?start=offset-bytes.
* for example, http://server/file.flv?start=10240
* server will write flv header and sequence header, 
* then seek(10240) and response flv tag data.
*/
class SrsVodStream : public SrsHttpFileServer
{
public:
    SrsVodStream(std::string root_dir);
    virtual ~SrsVodStream();
protected:
    virtual int serve_flv_stream(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string fullpath, int offset);
    virtual int serve_mp4_stream(ISrsHttpResponseWriter* w, ISrsHttpMessage* r, std::string fullpath, int start, int end);
};

/**
* for the srs http stream cache, 
* for example, the audio stream cache to make android(weixin) happy.
* we start a thread to shrink the queue.
*/
class SrsStreamCache : public ISrsEndlessThreadHandler
{
private:
    SrsMessageQueue* queue;
    SrsSource* source;
    SrsRequest* req;
    SrsEndlessThread* pthread;
public:
    SrsStreamCache(SrsSource* s, SrsRequest* r);
    virtual ~SrsStreamCache();
public:
    virtual int start();
    virtual int dump_cache(SrsConsumer* consumer);
// interface ISrsEndlessThreadHandler.
public:
    virtual int cycle();
};

/**
* the stream encoder in some codec, for example, flv or aac.
*/
class ISrsStreamEncoder
{
public:
    ISrsStreamEncoder();
    virtual ~ISrsStreamEncoder();
public:
    /**
    * initialize the encoder with file writer(to http response) and stream cache.
    * @param w the writer to write to http response.
    * @param c the stream cache for audio stream fast startup.
    */
    virtual int initialize(SrsFileWriter* w, SrsStreamCache* c) = 0;
    /**
    * write rtmp video/audio/metadata.
    */
    virtual int write_audio(int64_t timestamp, char* data, int size) = 0;
    virtual int write_video(int64_t timestamp, char* data, int size) = 0;
    virtual int write_metadata(int64_t timestamp, char* data, int size) = 0;
public:
    /**
    * for some stream, for example, mp3 and aac, the audio stream,
    * we use large gop cache in encoder, for the gop cache of SrsSource is ignore audio.
    * @return true to use gop cache of encoder; otherwise, use SrsSource.
    */
    virtual bool has_cache() = 0;
    /**
    * dumps the cache of encoder to consumer.
    */
    virtual int dump_cache(SrsConsumer* consumer) = 0;
};

/**
* the flv stream encoder, remux rtmp stream to flv stream.
*/
class SrsFlvStreamEncoder : public ISrsStreamEncoder
{
private:
    SrsFlvEncoder* enc;
public:
    SrsFlvStreamEncoder();
    virtual ~SrsFlvStreamEncoder();
public:
    virtual int initialize(SrsFileWriter* w, SrsStreamCache* c);
    virtual int write_audio(int64_t timestamp, char* data, int size);
    virtual int write_video(int64_t timestamp, char* data, int size);
    virtual int write_metadata(int64_t timestamp, char* data, int size);
public:
    virtual bool has_cache();
    virtual int dump_cache(SrsConsumer* consumer);
};

/**
* the ts stream encoder, remux rtmp stream to ts stream.
*/
class SrsTsStreamEncoder : public ISrsStreamEncoder
{
private:
    SrsTsEncoder* enc;
public:
    SrsTsStreamEncoder();
    virtual ~SrsTsStreamEncoder();
public:
    virtual int initialize(SrsFileWriter* w, SrsStreamCache* c);
    virtual int write_audio(int64_t timestamp, char* data, int size);
    virtual int write_video(int64_t timestamp, char* data, int size);
    virtual int write_metadata(int64_t timestamp, char* data, int size);
public:
    virtual bool has_cache();
    virtual int dump_cache(SrsConsumer* consumer);
};

/**
* the aac stream encoder, remux rtmp stream to aac stream.
*/
class SrsAacStreamEncoder : public ISrsStreamEncoder
{
private:
    SrsAacEncoder* enc;
    SrsStreamCache* cache;
public:
    SrsAacStreamEncoder();
    virtual ~SrsAacStreamEncoder();
public:
    virtual int initialize(SrsFileWriter* w, SrsStreamCache* c);
    virtual int write_audio(int64_t timestamp, char* data, int size);
    virtual int write_video(int64_t timestamp, char* data, int size);
    virtual int write_metadata(int64_t timestamp, char* data, int size);
public:
    virtual bool has_cache();
    virtual int dump_cache(SrsConsumer* consumer);
};

/**
* the mp3 stream encoder, remux rtmp stream to mp3 stream.
*/
class SrsMp3StreamEncoder : public ISrsStreamEncoder
{
private:
    SrsMp3Encoder* enc;
    SrsStreamCache* cache;
public:
    SrsMp3StreamEncoder();
    virtual ~SrsMp3StreamEncoder();
public:
    virtual int initialize(SrsFileWriter* w, SrsStreamCache* c);
    virtual int write_audio(int64_t timestamp, char* data, int size);
    virtual int write_video(int64_t timestamp, char* data, int size);
    virtual int write_metadata(int64_t timestamp, char* data, int size);
public:
    virtual bool has_cache();
    virtual int dump_cache(SrsConsumer* consumer);
};

/**
* write stream to http response direclty.
*/
class SrsStreamWriter : public SrsFileWriter
{
private:
    ISrsHttpResponseWriter* writer;
public:
    SrsStreamWriter(ISrsHttpResponseWriter* w);
    virtual ~SrsStreamWriter();
public:
    virtual int open(std::string file);
    virtual void close();
public:
    virtual bool is_open();
    virtual int64_t tellg();
public:
    virtual int write(void* buf, size_t count, ssize_t* pnwrite);
    virtual int writev(iovec* iov, int iovcnt, ssize_t* pnwrite);
};

/**
* the flv live stream supports access rtmp in flv over http.
* srs will remux rtmp to flv streaming.
*/
class SrsLiveStream : public ISrsHttpHandler
{
private:
    SrsRequest* req;
    SrsSource* source;
    SrsStreamCache* cache;
public:
    SrsLiveStream(SrsSource* s, SrsRequest* r, SrsStreamCache* c);
    virtual ~SrsLiveStream();
public:
    virtual int serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
private:
    virtual int streaming_send_messages(ISrsStreamEncoder* enc, SrsSharedPtrMessage** msgs, int nb_msgs);
};

/**
* the srs live entry
*/
struct SrsLiveEntry
{
private:
    bool _is_flv;
    bool _is_ts;
    bool _is_aac;
    bool _is_mp3;
public:
    // for template, the mount contains variables.
    // for concrete stream, the mount is url to access.
    std::string mount;
    // whether hstrs(http stream trigger rtmp source)
    bool hstrs;
    
    SrsLiveStream* stream;
    SrsStreamCache* cache;
    
    SrsLiveEntry(std::string m, bool h);
    
    bool is_flv();
    bool is_ts();
    bool is_mp3();
    bool is_aac();
};

/**
* the m3u8 stream handler.
*/
class SrsHlsM3u8Stream : public ISrsHttpHandler
{
private:
    std::string m3u8;
public:
    SrsHlsM3u8Stream();
    virtual ~SrsHlsM3u8Stream();
public:
    virtual void set_m3u8(std::string v);
public:
    virtual int serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

/**
* the ts stream handler.
*/
class SrsHlsTsStream : public ISrsHttpHandler
{
private:
    std::string ts;
public:
    SrsHlsTsStream();
    virtual ~SrsHlsTsStream();
public:
    virtual void set_ts(std::string v);
public:
    virtual int serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

/**
* the srs hls entry.
*/
struct SrsHlsEntry
{
    // for template, the mount contains variables.
    // for concrete stream, the mount is url to access.
    std::string mount;

    // key: the m3u8/ts file path.
    // value: the http handler.
    std::map<std::string, ISrsHttpHandler*> streams;
    
    SrsHlsEntry();
};

/**
* the http server instance,
* serve http static file, flv vod stream and flv live stream.
*/
class SrsHttpServer : virtual public ISrsReloadHandler
    , virtual public ISrsHttpMatchHijacker
{
private:
    SrsServer* server;
public:
    SrsHttpServeMux mux;
    // the http live streaming template, to create streams.
    std::map<std::string, SrsLiveEntry*> tflvs;
    // the http live streaming streams, crote by template.
    std::map<std::string, SrsLiveEntry*> sflvs;
    // the hls live streaming template, to create streams.
    std::map<std::string, SrsHlsEntry*> thls;
    // the hls live streaming streams, crote by template.
    std::map<std::string, SrsHlsEntry*> shls;
public:
    SrsHttpServer(SrsServer* svr);
    virtual ~SrsHttpServer();
public:
    virtual int initialize();
// http flv/ts/mp3/aac stream
public:
    virtual int http_mount(SrsSource* s, SrsRequest* r);
    virtual void http_unmount(SrsSource* s, SrsRequest* r);
// hls stream
public:
    virtual int mount_hls(SrsRequest* r);
    virtual int hls_update_m3u8(SrsRequest* r, std::string m3u8);
    virtual int hls_update_ts(SrsRequest* r, std::string uri, std::string ts);
    virtual void unmount_hls(SrsRequest* r);
// interface ISrsReloadHandler.
public:
    virtual int on_reload_vhost_http_updated();
    virtual int on_reload_vhost_http_remux_updated();
    virtual int on_reload_vhost_hls(std::string vhost);
// interface ISrsHttpMatchHijacker
public:
    virtual int hijack(ISrsHttpMessage* request, ISrsHttpHandler** ph);
private:
    virtual int initialize_static_file();
    virtual int initialize_flv_streaming();
    virtual int initialize_hls_streaming();
};

class SrsHttpConn : public SrsConnection
{
private:
    SrsHttpParser* parser;
    SrsHttpServeMux* http_mux;
public:
    SrsHttpConn(IConnectionManager* cm, st_netfd_t fd, SrsHttpServeMux* m);
    virtual ~SrsHttpConn();
// interface IKbpsDelta
public:
    virtual void resample();
    virtual int64_t get_send_bytes_delta();
    virtual int64_t get_recv_bytes_delta();
    virtual void cleanup();
protected:
    virtual int do_cycle();
protected:
    // when got http message,
    // for the static service or api, discard any body.
    // for the stream caster, for instance, http flv streaming, may discard the flv header or not.
    virtual int on_got_http_message(ISrsHttpMessage* msg) = 0;
private:
    virtual int process_request(ISrsHttpResponseWriter* w, ISrsHttpMessage* r);
};

class SrsStaticHttpConn : public SrsHttpConn
{
public:
    SrsStaticHttpConn(IConnectionManager* cm, st_netfd_t fd, SrsHttpServeMux* m);
    virtual ~SrsStaticHttpConn();
public:
    virtual int on_got_http_message(ISrsHttpMessage* msg);
};

#endif

#endif

