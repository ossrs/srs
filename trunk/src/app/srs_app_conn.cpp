//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_app_conn.hpp>

#include <netinet/tcp.h>
#include <algorithm>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_log.hpp>
#include <srs_app_log.hpp>
#include <srs_app_config.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_protocol_kbps.hpp>

SrsPps* _srs_pps_ids = NULL;
SrsPps* _srs_pps_fids = NULL;
SrsPps* _srs_pps_fids_level0 = NULL;
SrsPps* _srs_pps_dispose = NULL;

ISrsDisposingHandler::ISrsDisposingHandler()
{
}

ISrsDisposingHandler::~ISrsDisposingHandler()
{
}

SrsResourceManager::SrsResourceManager(const std::string& label, bool verbose)
{
    verbose_ = verbose;
    label_ = label;
    cond = srs_cond_new();
    trd = NULL;
    p_disposing_ = NULL;
    removing_ = false;

    nn_level0_cache_ = 100000;
    conns_level0_cache_ = new SrsResourceFastIdItem[nn_level0_cache_];
}

SrsResourceManager::~SrsResourceManager()
{
    if (trd) {
        srs_cond_signal(cond);
        trd->stop();

        srs_freep(trd);
        srs_cond_destroy(cond);
    }

    clear();

    // Free all objects not in zombies.
    std::vector<ISrsResource*>::iterator it;
    for (it = conns_.begin(); it != conns_.end(); ++it) {
        ISrsResource* resource = *it;
        srs_freep(resource);
    }

    srs_freepa(conns_level0_cache_);
}

srs_error_t SrsResourceManager::start()
{
    srs_error_t err = srs_success;

    cid_ = _srs_context->generate_id();
    trd = new SrsSTCoroutine("manager", this, cid_);

    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "conn manager");
    }

    return err;
}

bool SrsResourceManager::empty()
{
    return conns_.empty();
}

size_t SrsResourceManager::size()
{
    return conns_.size();
}

srs_error_t SrsResourceManager::cycle()
{
    srs_error_t err = srs_success;

    srs_trace("%s: connection manager run, conns=%d", label_.c_str(), (int)conns_.size());

    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "conn manager");
        }

        // Clear all zombies, because we may switch context and lost signal
        // when we clear zombie connection.
        while (!zombies_.empty()) {
            clear();
        }

        srs_cond_wait(cond);
    }

    return err;
}

void SrsResourceManager::add(ISrsResource* conn, bool* exists)
{
    if (std::find(conns_.begin(), conns_.end(), conn) == conns_.end()) {
        conns_.push_back(conn);
    } else {
        if (exists) {
            *exists = true;
        }
    }
}

void SrsResourceManager::add_with_id(const std::string& id, ISrsResource* conn)
{
    add(conn);
    conns_id_[id] = conn;
}

void SrsResourceManager::add_with_fast_id(uint64_t id, ISrsResource* conn)
{
    bool exists = false;
    add(conn, &exists);
    conns_fast_id_[id] = conn;

    if (exists) {
        return;
    }

    // For new resource, build the level-0 cache for fast-id.
    SrsResourceFastIdItem* item = &conns_level0_cache_[(id | id>>32) % nn_level0_cache_];

    // Ignore if exits item.
    if (item->fast_id && item->fast_id == id) {
        return;
    }

    // Fresh one, create the item.
    if (!item->fast_id) {
        item->fast_id = id;
        item->impl = conn;
        item->nn_collisions = 1;
        item->available = true;
    }

    // Collision, increase the collisions.
    if (item->fast_id != id) {
        item->nn_collisions++;
        item->available = false;
    }
}

void SrsResourceManager::add_with_name(const std::string& name, ISrsResource* conn)
{
    add(conn);
    conns_name_[name] = conn;
}

ISrsResource* SrsResourceManager::at(int index)
{
    return (index < (int)conns_.size())? conns_.at(index) : NULL;
}

ISrsResource* SrsResourceManager::find_by_id(std::string id)
{
    ++_srs_pps_ids->sugar;
    map<string, ISrsResource*>::iterator it = conns_id_.find(id);
    return (it != conns_id_.end())? it->second : NULL;
}

ISrsResource* SrsResourceManager::find_by_fast_id(uint64_t id)
{
    SrsResourceFastIdItem* item = &conns_level0_cache_[(id | id>>32) % nn_level0_cache_];
    if (item->available && item->fast_id == id) {
        ++_srs_pps_fids_level0->sugar;
        return item->impl;
    }

    ++_srs_pps_fids->sugar;
    map<uint64_t, ISrsResource*>::iterator it = conns_fast_id_.find(id);
    return (it != conns_fast_id_.end())? it->second : NULL;
}

ISrsResource* SrsResourceManager::find_by_name(std::string name)
{
    ++_srs_pps_ids->sugar;
    map<string, ISrsResource*>::iterator it = conns_name_.find(name);
    return (it != conns_name_.end())? it->second : NULL;
}

void SrsResourceManager::subscribe(ISrsDisposingHandler* h)
{
    if (std::find(handlers_.begin(), handlers_.end(), h) == handlers_.end()) {
        handlers_.push_back(h);
    }

    // Restore the handler from unsubscribing handlers.
    vector<ISrsDisposingHandler*>::iterator it;
    if ((it = std::find(unsubs_.begin(), unsubs_.end(), h)) != unsubs_.end()) {
        it = unsubs_.erase(it);
    }
}

void SrsResourceManager::unsubscribe(ISrsDisposingHandler* h)
{
    vector<ISrsDisposingHandler*>::iterator it = find(handlers_.begin(), handlers_.end(), h);
    if (it != handlers_.end()) {
        it = handlers_.erase(it);
    }

    // Put it to the unsubscribing handlers.
    if (std::find(unsubs_.begin(), unsubs_.end(), h) == unsubs_.end()) {
        unsubs_.push_back(h);
    }
}

void SrsResourceManager::remove(ISrsResource* c)
{
    SrsContextRestore(_srs_context->get_id());

    removing_ = true;
    do_remove(c);
    removing_ = false;
}

void SrsResourceManager::do_remove(ISrsResource* c)
{
    bool in_zombie = false;
    bool in_disposing = false;
    check_remove(c, in_zombie, in_disposing);
    bool ignored = in_zombie || in_disposing;

    if (verbose_) {
        _srs_context->set_id(c->get_id());
        srs_trace("%s: before dispose resource(%s)(%p), conns=%d, zombies=%d, ign=%d, inz=%d, ind=%d",
            label_.c_str(), c->desc().c_str(), c, (int)conns_.size(), (int)zombies_.size(), ignored,
            in_zombie, in_disposing);
    }
    if (ignored) {
        return;
    }

    // Push to zombies, we will free it in another coroutine.
    zombies_.push_back(c);

    // We should copy all handlers, because it may change during callback.
    vector<ISrsDisposingHandler*> handlers = handlers_;

    // Notify other handlers to handle the before-dispose event.
    for (int i = 0; i < (int)handlers.size(); i++) {
        ISrsDisposingHandler* h = handlers.at(i);

        // Ignore if handler is unsubscribing.
        if (!unsubs_.empty() && std::find(unsubs_.begin(), unsubs_.end(), h) != unsubs_.end()) {
            srs_warn2(TAG_RESOURCE_UNSUB, "%s: ignore before-dispose resource(%s)(%p) for %p, conns=%d",
                label_.c_str(), c->desc().c_str(), c, h, (int)conns_.size());
            continue;
        }

        h->on_before_dispose(c);
    }

    // Notify the coroutine to free it.
    srs_cond_signal(cond);
}

void SrsResourceManager::check_remove(ISrsResource* c, bool& in_zombie, bool& in_disposing)
{
    // Only notify when not removed(in zombies_).
    vector<ISrsResource*>::iterator it = std::find(zombies_.begin(), zombies_.end(), c);
    if (it != zombies_.end()) {
        in_zombie = true;
    }

    // Also ignore when we are disposing it.
    if (p_disposing_) {
        it = std::find(p_disposing_->begin(), p_disposing_->end(), c);
        if (it != p_disposing_->end()) {
            in_disposing = true;
        }
    }
}

void SrsResourceManager::clear()
{
    if (zombies_.empty()) {
        return;
    }

    SrsContextRestore(cid_);
    if (verbose_) {
        srs_trace("%s: clear zombies=%d resources, conns=%d, removing=%d, unsubs=%d",
            label_.c_str(), (int)zombies_.size(), (int)conns_.size(), removing_, (int)unsubs_.size());
    }

    // Clear all unsubscribing handlers, if not removing any resource.
    if (!removing_ && !unsubs_.empty()) {
        vector<ISrsDisposingHandler*>().swap(unsubs_);
    }

    do_clear();
}

void SrsResourceManager::do_clear()
{
    // To prevent thread switch when delete connection,
    // we copy all connections then free one by one.
    vector<ISrsResource*> copy;
    copy.swap(zombies_);
    p_disposing_ = &copy;

    for (int i = 0; i < (int)copy.size(); i++) {
        ISrsResource* conn = copy.at(i);

        if (verbose_) {
            _srs_context->set_id(conn->get_id());
            srs_trace("%s: disposing #%d resource(%s)(%p), conns=%d, disposing=%d, zombies=%d", label_.c_str(),
                i, conn->desc().c_str(), conn, (int)conns_.size(), (int)copy.size(), (int)zombies_.size());
        }

        ++_srs_pps_dispose->sugar;

        dispose(conn);
    }

    // Reset it for it points to a local object.
    // @remark We must set the disposing to NULL to avoid reusing address,
    // because the context might switch.
    p_disposing_ = NULL;

    // We should free the resources when finished all disposing callbacks,
    // which might cause context switch and reuse the freed addresses.
    for (int i = 0; i < (int)copy.size(); i++) {
        ISrsResource* conn = copy.at(i);
        srs_freep(conn);
    }
}

void SrsResourceManager::dispose(ISrsResource* c)
{
    for (map<string, ISrsResource*>::iterator it = conns_name_.begin(); it != conns_name_.end();) {
        if (c != it->second) {
            ++it;
        } else {
            // Use C++98 style: https://stackoverflow.com/a/4636230
            conns_name_.erase(it++);
        }
    }

    for (map<string, ISrsResource*>::iterator it = conns_id_.begin(); it != conns_id_.end();) {
        if (c != it->second) {
            ++it;
        } else {
            // Use C++98 style: https://stackoverflow.com/a/4636230
            conns_id_.erase(it++);
        }
    }

    for (map<uint64_t, ISrsResource*>::iterator it = conns_fast_id_.begin(); it != conns_fast_id_.end();) {
        if (c != it->second) {
            ++it;
        } else {
            // Update the level-0 cache for fast-id.
            uint64_t id = it->first;
            SrsResourceFastIdItem* item = &conns_level0_cache_[(id | id>>32) % nn_level0_cache_];
            item->nn_collisions--;
            if (!item->nn_collisions) {
                item->fast_id = 0;
                item->available = false;
            }

            // Use C++98 style: https://stackoverflow.com/a/4636230
            conns_fast_id_.erase(it++);
        }
    }

    vector<ISrsResource*>::iterator it = std::find(conns_.begin(), conns_.end(), c);
    if (it != conns_.end()) {
        it = conns_.erase(it);
    }

    // We should copy all handlers, because it may change during callback.
    vector<ISrsDisposingHandler*> handlers = handlers_;

    // Notify other handlers to handle the disposing event.
    for (int i = 0; i < (int)handlers.size(); i++) {
        ISrsDisposingHandler* h = handlers.at(i);

        // Ignore if handler is unsubscribing.
        if (!unsubs_.empty() && std::find(unsubs_.begin(), unsubs_.end(), h) != unsubs_.end()) {
            srs_warn2(TAG_RESOURCE_UNSUB, "%s: ignore disposing resource(%s)(%p) for %p, conns=%d",
                label_.c_str(), c->desc().c_str(), c, h, (int)conns_.size());
            continue;
        }

        h->on_disposing(c);
    }
}

SrsLazySweepGc::SrsLazySweepGc()
{
}

SrsLazySweepGc::~SrsLazySweepGc()
{
}

srs_error_t SrsLazySweepGc::start()
{
    srs_error_t err = srs_success;
    return err;
}

void SrsLazySweepGc::remove(SrsLazyObject* c)
{
    // TODO: FIXME: MUST lazy sweep.
    srs_freep(c);
}

ISrsLazyGc* _srs_gc = NULL;

ISrsExpire::ISrsExpire()
{
}

ISrsExpire::~ISrsExpire()
{
}

SrsTcpConnection::SrsTcpConnection(srs_netfd_t c)
{
    stfd = c;
    skt = new SrsStSocket(c);
}

SrsTcpConnection::~SrsTcpConnection()
{
    srs_freep(skt);
    srs_close_stfd(stfd);
}

srs_error_t SrsTcpConnection::set_tcp_nodelay(bool v)
{
    srs_error_t err = srs_success;

    int r0 = 0;
    socklen_t nb_v = sizeof(int);
    int fd = srs_netfd_fileno(stfd);

    int ov = 0;
    if ((r0 = getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &ov, &nb_v)) != 0) {
        return srs_error_new(ERROR_SOCKET_NO_NODELAY, "getsockopt fd=%d, r0=%d", fd, r0);
    }

#ifndef SRS_PERF_TCP_NODELAY
    srs_warn("ignore TCP_NODELAY, fd=%d, ov=%d", fd, ov);
    return err;
#endif

    int iv = (v? 1:0);
    if ((r0 = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &iv, nb_v)) != 0) {
        return srs_error_new(ERROR_SOCKET_NO_NODELAY, "setsockopt fd=%d, r0=%d", fd, r0);
    }
    if ((r0 = getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &iv, &nb_v)) != 0) {
        return srs_error_new(ERROR_SOCKET_NO_NODELAY, "getsockopt fd=%d, r0=%d", fd, r0);
    }

    srs_trace("set fd=%d TCP_NODELAY %d=>%d", fd, ov, iv);

    return err;
}

srs_error_t SrsTcpConnection::set_socket_buffer(srs_utime_t buffer_v)
{
    srs_error_t err = srs_success;

    int r0 = 0;
    int fd = srs_netfd_fileno(stfd);
    socklen_t nb_v = sizeof(int);

    int ov = 0;
    if ((r0 = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &ov, &nb_v)) != 0) {
        return srs_error_new(ERROR_SOCKET_SNDBUF, "getsockopt fd=%d, r0=%d", fd, r0);
    }

#ifndef SRS_PERF_MW_SO_SNDBUF
    srs_warn("ignore SO_SNDBUF, fd=%d, ov=%d", fd, ov);
    return err;
#endif

    // the bytes:
    //      4KB=4096, 8KB=8192, 16KB=16384, 32KB=32768, 64KB=65536,
    //      128KB=131072, 256KB=262144, 512KB=524288
    // the buffer should set to sleep*kbps/8,
    // for example, your system delivery stream in 1000kbps,
    // sleep 800ms for small bytes, the buffer should set to:
    //      800*1000/8=100000B(about 128KB).
    // other examples:
    //      2000*3000/8=750000B(about 732KB).
    //      2000*5000/8=1250000B(about 1220KB).
    int kbps = 4000;
    int iv = srsu2ms(buffer_v) * kbps / 8;

    // socket send buffer, system will double it.
    iv = iv / 2;

    // override the send buffer by macro.
#ifdef SRS_PERF_SO_SNDBUF_SIZE
    iv = SRS_PERF_SO_SNDBUF_SIZE / 2;
#endif

    // set the socket send buffer when required larger buffer
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &iv, nb_v) < 0) {
        return srs_error_new(ERROR_SOCKET_SNDBUF, "setsockopt fd=%d, r0=%d", fd, r0);
    }
    if ((r0 = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &iv, &nb_v)) != 0) {
        return srs_error_new(ERROR_SOCKET_SNDBUF, "getsockopt fd=%d, r0=%d", fd, r0);
    }

    srs_trace("set fd=%d, SO_SNDBUF=%d=>%d, buffer=%dms", fd, ov, iv, srsu2ms(buffer_v));

    return err;
}

void SrsTcpConnection::set_recv_timeout(srs_utime_t tm)
{
    skt->set_recv_timeout(tm);
}

srs_utime_t SrsTcpConnection::get_recv_timeout()
{
    return skt->get_recv_timeout();
}

srs_error_t SrsTcpConnection::read_fully(void* buf, size_t size, ssize_t* nread)
{
    return skt->read_fully(buf, size, nread);
}

int64_t SrsTcpConnection::get_recv_bytes()
{
    return skt->get_recv_bytes();
}

int64_t SrsTcpConnection::get_send_bytes()
{
    return skt->get_send_bytes();
}

srs_error_t SrsTcpConnection::read(void* buf, size_t size, ssize_t* nread)
{
    return skt->read(buf, size, nread);
}

void SrsTcpConnection::set_send_timeout(srs_utime_t tm)
{
    skt->set_send_timeout(tm);
}

srs_utime_t SrsTcpConnection::get_send_timeout()
{
    return skt->get_send_timeout();
}

srs_error_t SrsTcpConnection::write(void* buf, size_t size, ssize_t* nwrite)
{
    return skt->write(buf, size, nwrite);
}

srs_error_t SrsTcpConnection::writev(const iovec *iov, int iov_size, ssize_t* nwrite)
{
    return skt->writev(iov, iov_size, nwrite);
}

SrsBufferedReadWriter::SrsBufferedReadWriter(ISrsProtocolReadWriter* io)
{
    io_ = io;
    buf_ = NULL;
}

SrsBufferedReadWriter::~SrsBufferedReadWriter()
{
    srs_freep(buf_);
}

srs_error_t SrsBufferedReadWriter::peek(char* buf, int* size)
{
    srs_error_t err = srs_success;

    if ((err = reload_buffer()) != srs_success) {
        return srs_error_wrap(err, "reload buffer");
    }

    int nn = srs_min(buf_->left(), *size);
    *size = nn;

    if (nn) {
        memcpy(buf, buf_->head(), nn);
    }

    return err;
}

srs_error_t SrsBufferedReadWriter::reload_buffer()
{
    srs_error_t err = srs_success;

    if (buf_ && !buf_->empty()) {
        return err;
    }

    // We use read_fully to always full fill the cache, to avoid peeking failed.
    ssize_t nread = 0;
    if ((err = io_->read_fully(cache_, sizeof(cache_), &nread)) != srs_success) {
        return srs_error_wrap(err, "read");
    }

    srs_freep(buf_);
    buf_ = new SrsBuffer(cache_, nread);

    return err;
}

srs_error_t SrsBufferedReadWriter::read(void* buf, size_t size, ssize_t* nread)
{
    if (!buf_ || buf_->empty()) {
        return io_->read(buf, size, nread);
    }

    int nn = srs_min(buf_->left(), (int)size);
    *nread = nn;

    if (nn) {
        buf_->read_bytes((char*)buf, nn);
    }
    return srs_success;
}

srs_error_t SrsBufferedReadWriter::read_fully(void* buf, size_t size, ssize_t* nread)
{
    if (!buf_ || buf_->empty()) {
        return io_->read_fully(buf, size, nread);
    }

    int nn = srs_min(buf_->left(), (int)size);
    if (nn) {
        buf_->read_bytes((char*)buf, nn);
    }

    int left = size - nn;
    *nread = size;

    if (left) {
        return io_->read_fully((char*)buf + nn, left, NULL);
    }
    return srs_success;
}

void SrsBufferedReadWriter::set_recv_timeout(srs_utime_t tm)
{
    return io_->set_recv_timeout(tm);
}

srs_utime_t SrsBufferedReadWriter::get_recv_timeout()
{
    return io_->get_recv_timeout();
}

int64_t SrsBufferedReadWriter::get_recv_bytes()
{
    return io_->get_recv_bytes();
}

int64_t SrsBufferedReadWriter::get_send_bytes()
{
    return io_->get_send_bytes();
}

void SrsBufferedReadWriter::set_send_timeout(srs_utime_t tm)
{
    return io_->set_send_timeout(tm);
}

srs_utime_t SrsBufferedReadWriter::get_send_timeout()
{
    return io_->get_send_timeout();
}

srs_error_t SrsBufferedReadWriter::write(void* buf, size_t size, ssize_t* nwrite)
{
    return io_->write(buf, size, nwrite);
}

srs_error_t SrsBufferedReadWriter::writev(const iovec *iov, int iov_size, ssize_t* nwrite)
{
    return io_->writev(iov, iov_size, nwrite);
}

SrsSslConnection::SrsSslConnection(ISrsProtocolReadWriter* c)
{
    transport = c;
    ssl_ctx = NULL;
    ssl = NULL;
}

SrsSslConnection::~SrsSslConnection()
{
    if (ssl) {
        // this function will free bio_in and bio_out
        SSL_free(ssl);
        ssl = NULL;
    }

    if (ssl_ctx) {
        SSL_CTX_free(ssl_ctx);
        ssl_ctx = NULL;
    }
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
srs_error_t SrsSslConnection::handshake(string key_file, string crt_file)
{
    srs_error_t err = srs_success;

    // For HTTPS, try to connect over security transport.
#if (OPENSSL_VERSION_NUMBER < 0x10002000L) // v1.0.2
    ssl_ctx = SSL_CTX_new(TLS_method());
#else
    ssl_ctx = SSL_CTX_new(TLSv1_2_method());
#endif
    SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_NONE, NULL);
    srs_assert(SSL_CTX_set_cipher_list(ssl_ctx, "ALL") == 1);

    // TODO: Setup callback, see SSL_set_ex_data and SSL_set_info_callback
    if ((ssl = SSL_new(ssl_ctx)) == NULL) {
        return srs_error_new(ERROR_HTTPS_HANDSHAKE, "SSL_new ssl");
    }

    if ((bio_in = BIO_new(BIO_s_mem())) == NULL) {
        return srs_error_new(ERROR_HTTPS_HANDSHAKE, "BIO_new in");
    }

    if ((bio_out = BIO_new(BIO_s_mem())) == NULL) {
        BIO_free(bio_in);
        return srs_error_new(ERROR_HTTPS_HANDSHAKE, "BIO_new out");
    }

    SSL_set_bio(ssl, bio_in, bio_out);

    // SSL setup active, as server role.
    SSL_set_accept_state(ssl);
    SSL_set_mode(ssl, SSL_MODE_ENABLE_PARTIAL_WRITE);

    uint8_t* data = NULL;
    int r0, r1, size;

    // Setup the key and cert file for server.
    if ((r0 = SSL_use_certificate_file(ssl, crt_file.c_str(), SSL_FILETYPE_PEM)) != 1) {
        return srs_error_new(ERROR_HTTPS_KEY_CRT, "use cert %s", crt_file.c_str());
    }

    if ((r0 = SSL_use_RSAPrivateKey_file(ssl, key_file.c_str(), SSL_FILETYPE_PEM)) != 1) {
        return srs_error_new(ERROR_HTTPS_KEY_CRT, "use key %s", key_file.c_str());
    }

    if ((r0 = SSL_check_private_key(ssl)) != 1) {
        return srs_error_new(ERROR_HTTPS_KEY_CRT, "check key %s with cert %s",
            key_file.c_str(), crt_file.c_str());
    }
    srs_info("ssl: use key %s and cert %s", key_file.c_str(), crt_file.c_str());

    // Receive ClientHello
    while (true) {
        char buf[1024]; ssize_t nn = 0;
        if ((err = transport->read(buf, sizeof(buf), &nn)) != srs_success) {
            return srs_error_wrap(err, "handshake: read");
        }

        if ((r0 = BIO_write(bio_in, buf, nn)) <= 0) {
            // TODO: 0 or -1 maybe block, use BIO_should_retry to check.
            return srs_error_new(ERROR_HTTPS_HANDSHAKE, "BIO_write r0=%d, data=%p, size=%d", r0, buf, nn);
        }

        r0 = SSL_do_handshake(ssl); r1 = SSL_get_error(ssl, r0); ERR_clear_error();
        if (r0 != -1 || r1 != SSL_ERROR_WANT_READ) {
            return srs_error_new(ERROR_HTTPS_HANDSHAKE, "handshake r0=%d, r1=%d", r0, r1);
        }

        if ((size = BIO_get_mem_data(bio_out, &data)) > 0) {
            // OK, reset it for the next write.
            if ((r0 = BIO_reset(bio_in)) != 1) {
                return srs_error_new(ERROR_HTTPS_HANDSHAKE, "BIO_reset r0=%d", r0);
            }
            break;
        }
    }

    srs_info("https: ClientHello done");

    // Send ServerHello, Certificate, Server Key Exchange, Server Hello Done
    size = BIO_get_mem_data(bio_out, &data);
    if (!data || size <= 0) {
        return srs_error_new(ERROR_HTTPS_HANDSHAKE, "handshake data=%p, size=%d", data, size);
    }
    if ((err = transport->write(data, size, NULL)) != srs_success) {
        return srs_error_wrap(err, "handshake: write data=%p, size=%d", data, size);
    }
    if ((r0 = BIO_reset(bio_out)) != 1) {
        return srs_error_new(ERROR_HTTPS_HANDSHAKE, "BIO_reset r0=%d", r0);
    }

    srs_info("https: ServerHello done");

    // Receive Client Key Exchange, Change Cipher Spec, Encrypted Handshake Message
    while (true) {
        char buf[1024]; ssize_t nn = 0;
        if ((err = transport->read(buf, sizeof(buf), &nn)) != srs_success) {
            return srs_error_wrap(err, "handshake: read");
        }

        if ((r0 = BIO_write(bio_in, buf, nn)) <= 0) {
            // TODO: 0 or -1 maybe block, use BIO_should_retry to check.
            return srs_error_new(ERROR_HTTPS_HANDSHAKE, "BIO_write r0=%d, data=%p, size=%d", r0, buf, nn);
        }

        r0 = SSL_do_handshake(ssl); r1 = SSL_get_error(ssl, r0); ERR_clear_error();
        if (r0 == 1 && r1 == SSL_ERROR_NONE) {
            break;
        }

        if (r0 != -1 || r1 != SSL_ERROR_WANT_READ) {
            return srs_error_new(ERROR_HTTPS_HANDSHAKE, "handshake r0=%d, r1=%d", r0, r1);
        }

        if ((size = BIO_get_mem_data(bio_out, &data)) > 0) {
            // OK, reset it for the next write.
            if ((r0 = BIO_reset(bio_in)) != 1) {
                return srs_error_new(ERROR_HTTPS_HANDSHAKE, "BIO_reset r0=%d", r0);
            }
            break;
        }
    }

    srs_info("https: Client done");

    // Send New Session Ticket, Change Cipher Spec, Encrypted Handshake Message
    size = BIO_get_mem_data(bio_out, &data);
    if (!data || size <= 0) {
        return srs_error_new(ERROR_HTTPS_HANDSHAKE, "handshake data=%p, size=%d", data, size);
    }
    if ((err = transport->write(data, size, NULL)) != srs_success) {
        return srs_error_wrap(err, "handshake: write data=%p, size=%d", data, size);
    }
    if ((r0 = BIO_reset(bio_out)) != 1) {
        return srs_error_new(ERROR_HTTPS_HANDSHAKE, "BIO_reset r0=%d", r0);
    }

    srs_info("https: Server done");

    return err;
}
#pragma GCC diagnostic pop

void SrsSslConnection::set_recv_timeout(srs_utime_t tm)
{
    transport->set_recv_timeout(tm);
}

srs_utime_t SrsSslConnection::get_recv_timeout()
{
    return transport->get_recv_timeout();
}

srs_error_t SrsSslConnection::read_fully(void* buf, size_t size, ssize_t* nread)
{
    return transport->read_fully(buf, size, nread);
}

int64_t SrsSslConnection::get_recv_bytes()
{
    return transport->get_recv_bytes();
}

int64_t SrsSslConnection::get_send_bytes()
{
    return transport->get_send_bytes();
}

srs_error_t SrsSslConnection::read(void* plaintext, size_t nn_plaintext, ssize_t* nread)
{
    srs_error_t err = srs_success;

    while (true) {
        int r0 = SSL_read(ssl, plaintext, nn_plaintext); int r1 = SSL_get_error(ssl, r0); ERR_clear_error();
        int r2 = BIO_ctrl_pending(bio_in); int r3 = SSL_is_init_finished(ssl);

        // OK, got data.
        if (r0 > 0) {
            srs_assert(r0 <= (int)nn_plaintext);
            if (nread) {
                *nread = r0;
            }
            return err;
        }

        // Need to read more data to feed SSL.
        if (r0 == -1 && r1 == SSL_ERROR_WANT_READ) {
            // TODO: Can we avoid copy?
            int nn_cipher = nn_plaintext;
            char* cipher = new char[nn_cipher];
            SrsAutoFreeA(char, cipher);

            // Read the cipher from SSL.
            ssize_t nn = 0;
            if ((err = transport->read(cipher, nn_cipher, &nn)) != srs_success) {
                return srs_error_wrap(err, "https: read");
            }

            int r0 = BIO_write(bio_in, cipher, nn);
            if (r0 <= 0) {
                // TODO: 0 or -1 maybe block, use BIO_should_retry to check.
                return srs_error_new(ERROR_HTTPS_READ, "BIO_write r0=%d, cipher=%p, size=%d", r0, cipher, nn);
            }
            continue;
        }

        // Fail for error.
        if (r0 <= 0) {
            return srs_error_new(ERROR_HTTPS_READ, "SSL_read r0=%d, r1=%d, r2=%d, r3=%d",
                r0, r1, r2, r3);
        }
    }
}

void SrsSslConnection::set_send_timeout(srs_utime_t tm)
{
    transport->set_send_timeout(tm);
}

srs_utime_t SrsSslConnection::get_send_timeout()
{
    return transport->get_send_timeout();
}

srs_error_t SrsSslConnection::write(void* plaintext, size_t nn_plaintext, ssize_t* nwrite)
{
    srs_error_t err = srs_success;

    for (char* p = (char*)plaintext; p < (char*)plaintext + nn_plaintext;) {
        int left = (int)nn_plaintext - (p - (char*)plaintext);
        int r0 = SSL_write(ssl, (const void*)p, left);
        int r1 = SSL_get_error(ssl, r0); ERR_clear_error();
        if (r0 <= 0) {
            return srs_error_new(ERROR_HTTPS_WRITE, "https: write data=%p, size=%d, r0=%d, r1=%d", p, left, r0, r1);
        }

        // Move p to the next writing position.
        p += r0;
        if (nwrite) {
            *nwrite += (ssize_t)r0;
        }

        uint8_t* data = NULL;
        int size = BIO_get_mem_data(bio_out, &data);
        if ((err = transport->write(data, size, NULL)) != srs_success) {
            return srs_error_wrap(err, "https: write data=%p, size=%d", data, size);
        }
        if ((r0 = BIO_reset(bio_out)) != 1) {
            return srs_error_new(ERROR_HTTPS_WRITE, "BIO_reset r0=%d", r0);
        }
    }

    return err;
}

srs_error_t SrsSslConnection::writev(const iovec *iov, int iov_size, ssize_t* nwrite)
{
    srs_error_t err = srs_success;

    for (int i = 0; i < iov_size; i++) {
        const iovec* p = iov + i;
        if ((err = write((void*)p->iov_base, (size_t)p->iov_len, nwrite)) != srs_success) {
            return srs_error_wrap(err, "write iov #%d base=%p, size=%d", i, p->iov_base, p->iov_len);
        }
    }

    return err;
}

