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

#include <srs_app_caster_flv.hpp>

#ifdef SRS_AUTO_STREAM_CASTER

#include <algorithm>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_http.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_rtmp_sdk.hpp>
#include <srs_rtmp_utility.hpp>
#include <srs_app_st_socket.hpp>
#include <srs_app_utility.hpp>
#include <srs_rtmp_amf0.hpp>
#include <srs_kernel_utility.hpp>

#define SRS_HTTP_FLV_STREAM_BUFFER 4096

SrsAppCasterFlv::SrsAppCasterFlv(SrsConfDirective* c)
{
    http_mux = new SrsHttpServeMux();
    output = _srs_config->get_stream_caster_output(c);
}

SrsAppCasterFlv::~SrsAppCasterFlv()
{
}

int SrsAppCasterFlv::initialize()
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = http_mux->handle("/", this)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsAppCasterFlv::on_tcp_client(st_netfd_t stfd)
{
    int ret = ERROR_SUCCESS;
    
    SrsHttpConn* conn = new SrsDynamicHttpConn(this, stfd, http_mux);
    conns.push_back(conn);
    
    if ((ret = conn->start()) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

void SrsAppCasterFlv::remove(SrsConnection* c)
{
    std::vector<SrsHttpConn*>::iterator it;
    if ((it = std::find(conns.begin(), conns.end(), c)) != conns.end()) {
        conns.erase(it);
    }
}

int SrsAppCasterFlv::serve_http(ISrsHttpResponseWriter* w, SrsHttpMessage* r)
{
    SrsDynamicHttpConn* conn = dynamic_cast<SrsDynamicHttpConn*>(r->connection());
    srs_assert(conn);
    
    std::string app = srs_path_dirname(r->path());
    std::string stream = srs_path_basename(r->path());
    
    std::string o = output;
    if (!app.empty() && app != "/") {
        o = srs_string_replace(o, "[app]", app);
    }
    o = srs_string_replace(o, "[stream]", stream);
    
    // remove the extension.
    if (srs_string_ends_with(o, ".flv")) {
        o = o.substr(0, o.length() - 4);
    }
    
    return conn->proxy(w, r, o);
}

SrsDynamicHttpConn::SrsDynamicHttpConn(IConnectionManager* cm, st_netfd_t fd, SrsHttpServeMux* m)
    : SrsHttpConn(cm, fd, m)
{
    
    req = NULL;
    io = NULL;
    client = NULL;
    stfd = NULL;
    stream_id = 0;
}

SrsDynamicHttpConn::~SrsDynamicHttpConn()
{
}

int SrsDynamicHttpConn::on_got_http_message(SrsHttpMessage* msg)
{
    int ret = ERROR_SUCCESS;
    return ret;
}

int SrsDynamicHttpConn::proxy(ISrsHttpResponseWriter* w, SrsHttpMessage* r, std::string o)
{
    int ret = ERROR_SUCCESS;
    
    output = o;
    srs_trace("flv: proxy %s to %s", r->uri().c_str(), output.c_str());
    
    char* buffer = new char[SRS_HTTP_FLV_STREAM_BUFFER];
    SrsAutoFree(char, buffer);
    
    ISrsHttpResponseReader* rr = r->body_reader();
    SrsHttpFileReader reader(rr);
    SrsFlvDecoder dec;
    
    if ((ret = dec.initialize(&reader)) != ERROR_SUCCESS) {
        return ret;
    }
    
    while (!rr->eof()) {
        int nb_read = 0;
        if ((ret = rr->read(buffer, SRS_HTTP_FLV_STREAM_BUFFER, &nb_read)) != ERROR_SUCCESS) {
            return ret;
        }
        //srs_trace("flv: read %dB from %s", nb_read, r->path().c_str());
    }
    
    return ret;
}

int SrsDynamicHttpConn::connect()
{
    int ret = ERROR_SUCCESS;
    
    // when ok, ignore.
    // TODO: FIXME: should reconnect when disconnected.
    if (io || client) {
        return ret;
    }
    
    // parse uri
    if (!req) {
        req = new SrsRequest();
        
        size_t pos = string::npos;
        string uri = req->tcUrl = output;
        
        // tcUrl, stream
        if ((pos = uri.rfind("/")) != string::npos) {
            req->stream = uri.substr(pos + 1);
            req->tcUrl = uri = uri.substr(0, pos);
        }
        
        srs_discovery_tc_url(req->tcUrl,
                             req->schema, req->host, req->vhost, req->app, req->port,
                             req->param);
    }
    
    // connect host.
    if ((ret = srs_socket_connect(req->host, ::atoi(req->port.c_str()), ST_UTIME_NO_TIMEOUT, &stfd)) != ERROR_SUCCESS) {
        srs_error("mpegts: connect server %s:%s failed. ret=%d", req->host.c_str(), req->port.c_str(), ret);
        return ret;
    }
    io = new SrsStSocket(stfd);
    client = new SrsRtmpClient(io);
    
    client->set_recv_timeout(SRS_CONSTS_RTMP_RECV_TIMEOUT_US);
    client->set_send_timeout(SRS_CONSTS_RTMP_SEND_TIMEOUT_US);
    
    // connect to vhost/app
    if ((ret = client->handshake()) != ERROR_SUCCESS) {
        srs_error("mpegts: handshake with server failed. ret=%d", ret);
        return ret;
    }
    if ((ret = connect_app(req->host, req->port)) != ERROR_SUCCESS) {
        srs_error("mpegts: connect with server failed. ret=%d", ret);
        return ret;
    }
    if ((ret = client->create_stream(stream_id)) != ERROR_SUCCESS) {
        srs_error("mpegts: connect with server failed, stream_id=%d. ret=%d", stream_id, ret);
        return ret;
    }
    
    // publish.
    if ((ret = client->publish(req->stream, stream_id)) != ERROR_SUCCESS) {
        srs_error("mpegts: publish failed, stream=%s, stream_id=%d. ret=%d",
                  req->stream.c_str(), stream_id, ret);
        return ret;
    }
    
    return ret;
}

// TODO: FIXME: refine the connect_app.
int SrsDynamicHttpConn::connect_app(string ep_server, string ep_port)
{
    int ret = ERROR_SUCCESS;
    
    // args of request takes the srs info.
    if (req->args == NULL) {
        req->args = SrsAmf0Any::object();
    }
    
    // notify server the edge identity,
    // @see https://github.com/simple-rtmp-server/srs/issues/147
    SrsAmf0Object* data = req->args;
    data->set("srs_sig", SrsAmf0Any::str(RTMP_SIG_SRS_KEY));
    data->set("srs_server", SrsAmf0Any::str(RTMP_SIG_SRS_KEY" "RTMP_SIG_SRS_VERSION" ("RTMP_SIG_SRS_URL_SHORT")"));
    data->set("srs_license", SrsAmf0Any::str(RTMP_SIG_SRS_LICENSE));
    data->set("srs_role", SrsAmf0Any::str(RTMP_SIG_SRS_ROLE));
    data->set("srs_url", SrsAmf0Any::str(RTMP_SIG_SRS_URL));
    data->set("srs_version", SrsAmf0Any::str(RTMP_SIG_SRS_VERSION));
    data->set("srs_site", SrsAmf0Any::str(RTMP_SIG_SRS_WEB));
    data->set("srs_email", SrsAmf0Any::str(RTMP_SIG_SRS_EMAIL));
    data->set("srs_copyright", SrsAmf0Any::str(RTMP_SIG_SRS_COPYRIGHT));
    data->set("srs_primary", SrsAmf0Any::str(RTMP_SIG_SRS_PRIMARY));
    data->set("srs_authors", SrsAmf0Any::str(RTMP_SIG_SRS_AUTHROS));
    // for edge to directly get the id of client.
    data->set("srs_pid", SrsAmf0Any::number(getpid()));
    data->set("srs_id", SrsAmf0Any::number(_srs_context->get_id()));
    
    // local ip of edge
    std::vector<std::string> ips = srs_get_local_ipv4_ips();
    assert(_srs_config->get_stats_network() < (int)ips.size());
    std::string local_ip = ips[_srs_config->get_stats_network()];
    data->set("srs_server_ip", SrsAmf0Any::str(local_ip.c_str()));
    
    // generate the tcUrl
    std::string param = "";
    std::string tc_url = srs_generate_tc_url(ep_server, req->vhost, req->app, ep_port, param);
    
    // upnode server identity will show in the connect_app of client.
    // @see https://github.com/simple-rtmp-server/srs/issues/160
    // the debug_srs_upnode is config in vhost and default to true.
    bool debug_srs_upnode = _srs_config->get_debug_srs_upnode(req->vhost);
    if ((ret = client->connect_app(req->app, tc_url, req, debug_srs_upnode)) != ERROR_SUCCESS) {
        srs_error("mpegts: connect with server failed, tcUrl=%s, dsu=%d. ret=%d",
                  tc_url.c_str(), debug_srs_upnode, ret);
        return ret;
    }
    
    return ret;
}

void SrsDynamicHttpConn::close()
{
    srs_freep(client);
    srs_freep(io);
    srs_freep(req);
    srs_close_stfd(stfd);
}

SrsHttpFileReader::SrsHttpFileReader(ISrsHttpResponseReader* h)
{
    http = h;
}

SrsHttpFileReader::~SrsHttpFileReader()
{
}

int SrsHttpFileReader::open(std::string /*file*/)
{
    return ERROR_SUCCESS;
}

void SrsHttpFileReader::close()
{
}

bool SrsHttpFileReader::is_open()
{
    return true;
}

int64_t SrsHttpFileReader::tellg()
{
    return 0;
}

void SrsHttpFileReader::skip(int64_t /*size*/)
{
}

int64_t SrsHttpFileReader::lseek(int64_t offset)
{
    return offset;
}

int64_t SrsHttpFileReader::filesize()
{
    return 0;
}

int SrsHttpFileReader::read(void* buf, size_t count, ssize_t* pnread)
{
    int ret = ERROR_SUCCESS;
    
    if (http->eof()) {
        ret = ERROR_HTTP_REQUEST_EOF;
        srs_error("flv: encoder EOF. ret=%d", ret);
        return ret;
    }
    
    int nread = 0;
    if ((ret = http->read((char*)buf, (int)count, &nread)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if (pnread) {
        *pnread = nread;
    }
    
    return ret;
}

#endif
