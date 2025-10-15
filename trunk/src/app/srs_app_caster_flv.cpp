//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_caster_flv.hpp>

#include <algorithm>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_st.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_utility.hpp>

#define SRS_HTTP_FLV_STREAM_BUFFER 4096

ISrsHttpFlvListener::ISrsHttpFlvListener()
{
}

ISrsHttpFlvListener::~ISrsHttpFlvListener()
{
}

SrsHttpFlvListener::SrsHttpFlvListener()
{
    listener_ = new SrsTcpListener(this);
    caster_ = new SrsAppCasterFlv();

    config_ = _srs_config;
}

SrsHttpFlvListener::~SrsHttpFlvListener()
{
    srs_freep(caster_);
    srs_freep(listener_);

    config_ = NULL;
}

srs_error_t SrsHttpFlvListener::initialize(SrsConfDirective *c)
{
    srs_error_t err = srs_success;

    int port = config_->get_stream_caster_listen(c);
    if (port <= 0) {
        return srs_error_new(ERROR_STREAM_CASTER_PORT, "invalid port=%d", port);
    }

    listener_->set_endpoint(srs_net_address_any(), port);
    listener_->set_label("PUSH-FLV");

    if ((err = caster_->initialize(c)) != srs_success) {
        return srs_error_wrap(err, "init caster port=%d", port);
    }

    return err;
}

srs_error_t SrsHttpFlvListener::listen()
{
    srs_error_t err = srs_success;

    if ((err = listener_->listen()) != srs_success) {
        return srs_error_wrap(err, "listen");
    }

    return err;
}

void SrsHttpFlvListener::close()
{
    listener_->close();
}

srs_error_t SrsHttpFlvListener::on_tcp_client(ISrsListener *listener, srs_netfd_t stfd)
{
    srs_error_t err = caster_->on_tcp_client(listener, stfd);
    if (err != srs_success) {
        srs_warn("accept client failed, err is %s", srs_error_desc(err).c_str());
        srs_freep(err);
    }

    return err;
}

ISrsAppCasterFlv::ISrsAppCasterFlv()
{
}

ISrsAppCasterFlv::~ISrsAppCasterFlv()
{
}

SrsAppCasterFlv::SrsAppCasterFlv()
{
    http_mux_ = new SrsHttpServeMux();
    manager_ = new SrsResourceManager("CFLV");

    config_ = _srs_config;
}

SrsAppCasterFlv::~SrsAppCasterFlv()
{
    srs_freep(http_mux_);
    srs_freep(manager_);

    std::vector<ISrsConnection *>::iterator it;
    for (it = conns_.begin(); it != conns_.end(); ++it) {
        ISrsConnection *conn = *it;
        srs_freep(conn);
    }

    config_ = NULL;
}

srs_error_t SrsAppCasterFlv::initialize(SrsConfDirective *c)
{
    srs_error_t err = srs_success;

    output_ = config_->get_stream_caster_output(c);

    if ((err = http_mux_->handle("/", this)) != srs_success) {
        return srs_error_wrap(err, "handle root");
    }

    if ((err = manager_->start()) != srs_success) {
        return srs_error_wrap(err, "start manager");
    }

    return err;
}

srs_error_t SrsAppCasterFlv::on_tcp_client(ISrsListener *listener, srs_netfd_t stfd)
{
    srs_error_t err = srs_success;

    int fd = srs_netfd_fileno(stfd);
    string ip = srs_get_peer_ip(fd);
    int port = srs_get_peer_port(fd);

    if (ip.empty() && !config_->empty_ip_ok()) {
        srs_warn("empty ip for fd=%d", srs_netfd_fileno(stfd));
    }

    ISrsDynamicHttpConn *conn = new SrsDynamicHttpConn(this, stfd, http_mux_, ip, port);
    conns_.push_back(conn);

    if ((err = conn->start()) != srs_success) {
        return srs_error_wrap(err, "start tcp listener");
    }

    return err;
}

srs_error_t SrsAppCasterFlv::start()
{
    return manager_->start();
}

bool SrsAppCasterFlv::empty()
{
    return manager_->empty();
}

size_t SrsAppCasterFlv::size()
{
    return manager_->size();
}

void SrsAppCasterFlv::add(ISrsResource *conn, bool *exists)
{
    manager_->add(conn, exists);
}

void SrsAppCasterFlv::add_with_id(const std::string &id, ISrsResource *conn)
{
    manager_->add_with_id(id, conn);
}

void SrsAppCasterFlv::add_with_fast_id(uint64_t id, ISrsResource *conn)
{
    manager_->add_with_fast_id(id, conn);
}

void SrsAppCasterFlv::add_with_name(const std::string &name, ISrsResource *conn)
{
    manager_->add_with_name(name, conn);
}

ISrsResource *SrsAppCasterFlv::at(int index)
{
    return manager_->at(index);
}

ISrsResource *SrsAppCasterFlv::find_by_id(std::string id)
{
    return manager_->find_by_id(id);
}

ISrsResource *SrsAppCasterFlv::find_by_fast_id(uint64_t id)
{
    return manager_->find_by_fast_id(id);
}

ISrsResource *SrsAppCasterFlv::find_by_name(std::string name)
{
    return manager_->find_by_name(name);
}

void SrsAppCasterFlv::remove(ISrsResource *c)
{
    ISrsConnection *conn = dynamic_cast<ISrsConnection *>(c);

    std::vector<ISrsConnection *>::iterator it;
    if ((it = std::find(conns_.begin(), conns_.end(), conn)) != conns_.end()) {
        it = conns_.erase(it);
    }

    // fixbug: ISrsConnection for CasterFlv is not freed, which could cause memory leak
    // so, free conn which is not managed by SrsServer->conns;
    // @see: https://github.com/ossrs/srs/issues/826
    manager_->remove(c);
}

void SrsAppCasterFlv::subscribe(ISrsDisposingHandler *h)
{
    manager_->subscribe(h);
}

void SrsAppCasterFlv::unsubscribe(ISrsDisposingHandler *h)
{
    manager_->unsubscribe(h);
}

srs_error_t SrsAppCasterFlv::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    SrsHttpMessage *msg = dynamic_cast<SrsHttpMessage *>(r);
    SrsHttpConn *hconn = dynamic_cast<SrsHttpConn *>(msg->connection());
    SrsDynamicHttpConn *dconn = dynamic_cast<SrsDynamicHttpConn *>(hconn->handler());
    srs_assert(dconn);

    SrsPath path;
    std::string app = path.filepath_dir(r->path());
    app = srs_strings_trim_start(app, "/");

    std::string stream = path.filepath_base(r->path());
    stream = srs_strings_trim_start(stream, "/");

    std::string o = output_;
    if (!app.empty() && app != "/") {
        o = srs_strings_replace(o, "[app]", app);
    }
    o = srs_strings_replace(o, "[stream]", stream);

    // remove the extension.
    if (srs_strings_ends_with(o, ".flv")) {
        o = o.substr(0, o.length() - 4);
    }

    srs_error_t err = dconn->proxy(w, r, o);
    if (err != srs_success) {
        return srs_error_wrap(err, "proxy");
    }

    return err;
}

ISrsDynamicHttpConn::ISrsDynamicHttpConn()
{
}

ISrsDynamicHttpConn::~ISrsDynamicHttpConn()
{
}

SrsDynamicHttpConn::SrsDynamicHttpConn(ISrsResourceManager *cm, srs_netfd_t fd, SrsHttpServeMux *m, string cip, int cport)
{
    // Create a identify for this client.
    _srs_context->set_id(_srs_context->generate_id());

    manager_ = cm;
    sdk_ = NULL;
    pprint_ = SrsPithyPrint::create_caster();
    skt_ = new SrsTcpConnection(fd);
    conn_ = new SrsHttpConn(this, skt_, m, cip, cport);
    ip_ = cip;
    port_ = cport;

    config_ = _srs_config;
    app_factory_ = _srs_app_factory;
}

SrsDynamicHttpConn::~SrsDynamicHttpConn()
{
    srs_freep(conn_);
    srs_freep(skt_);
    srs_freep(sdk_);
    srs_freep(pprint_);

    config_ = NULL;
    app_factory_ = NULL;
}

srs_error_t SrsDynamicHttpConn::proxy(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string o)
{
    srs_error_t err = srs_success;

    output_ = o;
    srs_trace("flv: proxy %s:%d %s to %s", ip_.c_str(), port_, r->uri().c_str(), output_.c_str());

    ISrsHttpResponseReader *rr = r->body_reader();
    SrsHttpFileReader reader(rr);
    SrsFlvDecoder dec;

    if ((err = dec.initialize(&reader)) != srs_success) {
        return srs_error_wrap(err, "init decoder");
    }

    char header[9];
    if ((err = dec.read_header(header)) != srs_success) {
        return srs_error_wrap(err, "read header");
    }

    char pps[4];
    if ((err = dec.read_previous_tag_size(pps)) != srs_success) {
        return srs_error_wrap(err, "read pts");
    }

    err = do_proxy(rr, &dec);
    sdk_->close();

    return err;
}

srs_error_t SrsDynamicHttpConn::do_proxy(ISrsHttpResponseReader *rr, SrsFlvDecoder *dec)
{
    srs_error_t err = srs_success;

    srs_freep(sdk_);

    srs_utime_t cto = SRS_CONSTS_RTMP_TIMEOUT;
    srs_utime_t sto = SRS_CONSTS_RTMP_PULSE;
    sdk_ = app_factory_->create_rtmp_client(output_, cto, sto);

    if ((err = sdk_->connect()) != srs_success) {
        return srs_error_wrap(err, "connect %s failed, cto=%dms, sto=%dms.", output_.c_str(), srsu2msi(cto), srsu2msi(sto));
    }

    if ((err = sdk_->publish(SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE)) != srs_success) {
        return srs_error_wrap(err, "publish");
    }

    char pps[4];
    while (!rr->eof()) {
        pprint_->elapse();

        char type;
        int32_t size;
        uint32_t time;
        if ((err = dec->read_tag_header(&type, &size, &time)) != srs_success) {
            return srs_error_wrap(err, "read tag header");
        }

        char *data = new char[size];
        if ((err = dec->read_tag_data(data, size)) != srs_success) {
            srs_freepa(data);
            return srs_error_wrap(err, "read tag data");
        }

        SrsRtmpCommonMessage *cmsg = NULL;
        if ((err = srs_rtmp_create_msg(type, time, data, size, sdk_->sid(), &cmsg)) != srs_success) {
            return srs_error_wrap(err, "create message");
        }

        SrsMediaPacket *msg = new SrsMediaPacket();
        cmsg->to_msg(msg);
        srs_freep(cmsg);

        // TODO: FIXME: for post flv, reconnect when error.
        if ((err = sdk_->send_and_free_message(msg)) != srs_success) {
            return srs_error_wrap(err, "send message");
        }

        if (pprint_->can_print()) {
            srs_trace("flv: send msg %d age=%d, dts=%d, size=%d", type, pprint_->age(), time, size);
        }

        if ((err = dec->read_previous_tag_size(pps)) != srs_success) {
            return srs_error_wrap(err, "read pts");
        }
    }

    return err;
}

srs_error_t SrsDynamicHttpConn::on_start()
{
    return srs_success;
}

srs_error_t SrsDynamicHttpConn::on_http_message(ISrsHttpMessage *r, ISrsHttpResponseWriter *w)
{
    return srs_success;
}

srs_error_t SrsDynamicHttpConn::on_message_done(ISrsHttpMessage *r, ISrsHttpResponseWriter *w)
{
    return srs_success;
}

srs_error_t SrsDynamicHttpConn::on_conn_done(srs_error_t r0)
{
    // Because we use manager to manage this object,
    // not the http connection object, so we must remove it here.
    manager_->remove(this);

    return r0;
}

std::string SrsDynamicHttpConn::desc()
{
    return "DHttpConn";
}

std::string SrsDynamicHttpConn::remote_ip()
{
    return conn_->remote_ip();
}

const SrsContextId &SrsDynamicHttpConn::get_id()
{
    return conn_->get_id();
}

srs_error_t SrsDynamicHttpConn::start()
{
    srs_error_t err = srs_success;

    bool v = config_->get_http_stream_crossdomain();
    if ((err = conn_->set_crossdomain_enabled(v)) != srs_success) {
        return srs_error_wrap(err, "set cors=%d", v);
    }

    return conn_->start();
}

ISrsHttpFileReader::ISrsHttpFileReader()
{
}

ISrsHttpFileReader::~ISrsHttpFileReader()
{
}

SrsHttpFileReader::SrsHttpFileReader(ISrsHttpResponseReader *h)
{
    http_ = h;
    file_reader_ = new SrsFileReader();
}

SrsHttpFileReader::~SrsHttpFileReader()
{
    srs_freep(file_reader_);
}

srs_error_t SrsHttpFileReader::open(std::string /*file*/)
{
    return srs_success;
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

int64_t SrsHttpFileReader::seek2(int64_t offset)
{
    return offset;
}

int64_t SrsHttpFileReader::filesize()
{
    return 0;
}

srs_error_t SrsHttpFileReader::read(void *buf, size_t count, ssize_t *pnread)
{
    srs_error_t err = srs_success;

    if (http_->eof()) {
        return srs_error_new(ERROR_HTTP_REQUEST_EOF, "EOF");
    }

    int total_read = 0;
    while (total_read < (int)count) {
        ssize_t nread = 0;
        if ((err = http_->read((char *)buf + total_read, (int)(count - total_read), &nread)) != srs_success) {
            return srs_error_wrap(err, "read");
        }

        if (nread == 0) {
            err = srs_error_new(ERROR_HTTP_REQUEST_EOF, "EOF");
            break;
        }

        srs_assert(nread);
        total_read += (int)nread;
    }

    if (pnread) {
        *pnread = total_read;
    }

    return err;
}

srs_error_t SrsHttpFileReader::lseek(off_t offset, int whence, off_t *seeked)
{
    // TODO: FIXME: Use HTTP range for seek.
    return srs_success;
}
