//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_protocol_conn.hpp>

#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_io.hpp>

#include <algorithm>
#include <netinet/in.h>
#include <netinet/tcp.h>
using namespace std;

ISrsConnection::ISrsConnection()
{
}

ISrsConnection::~ISrsConnection()
{
}

ISrsExpire::ISrsExpire()
{
}

ISrsExpire::~ISrsExpire()
{
}

SrsTcpConnection::SrsTcpConnection(srs_netfd_t c)
{
    stfd_ = c;
    skt_ = new SrsStSocket(c);
}

SrsTcpConnection::~SrsTcpConnection()
{
    srs_freep(skt_);
    srs_close_stfd(stfd_);
}

srs_error_t SrsTcpConnection::set_tcp_nodelay(bool v)
{
    srs_error_t err = srs_success;

    int r0 = 0;
    socklen_t nb_v = sizeof(int);
    int fd = srs_netfd_fileno(stfd_);

    int ov = 0;
    if ((r0 = getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &ov, &nb_v)) != 0) {
        return srs_error_new(ERROR_SOCKET_NO_NODELAY, "getsockopt fd=%d, r0=%d", fd, r0);
    }

#ifndef SRS_PERF_TCP_NODELAY
    srs_warn("ignore TCP_NODELAY, fd=%d, ov=%d", fd, ov);
    return err;
#endif

    int iv = (v ? 1 : 0);
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
    int fd = srs_netfd_fileno(stfd_);
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
    skt_->set_recv_timeout(tm);
}

srs_utime_t SrsTcpConnection::get_recv_timeout()
{
    return skt_->get_recv_timeout();
}

srs_error_t SrsTcpConnection::read_fully(void *buf, size_t size, ssize_t *nread)
{
    return skt_->read_fully(buf, size, nread);
}

int64_t SrsTcpConnection::get_recv_bytes()
{
    return skt_->get_recv_bytes();
}

int64_t SrsTcpConnection::get_send_bytes()
{
    return skt_->get_send_bytes();
}

srs_error_t SrsTcpConnection::read(void *buf, size_t size, ssize_t *nread)
{
    return skt_->read(buf, size, nread);
}

void SrsTcpConnection::set_send_timeout(srs_utime_t tm)
{
    skt_->set_send_timeout(tm);
}

srs_utime_t SrsTcpConnection::get_send_timeout()
{
    return skt_->get_send_timeout();
}

srs_error_t SrsTcpConnection::write(void *buf, size_t size, ssize_t *nwrite)
{
    return skt_->write(buf, size, nwrite);
}

srs_error_t SrsTcpConnection::writev(const iovec *iov, int iov_size, ssize_t *nwrite)
{
    return skt_->writev(iov, iov_size, nwrite);
}

SrsBufferedReadWriter::SrsBufferedReadWriter(ISrsProtocolReadWriter *io)
{
    io_ = io;
    buf_ = NULL;
}

SrsBufferedReadWriter::~SrsBufferedReadWriter()
{
    srs_freep(buf_);
}

srs_error_t SrsBufferedReadWriter::peek(char *buf, int *size)
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

srs_error_t SrsBufferedReadWriter::read(void *buf, size_t size, ssize_t *nread)
{
    if (!buf_ || buf_->empty()) {
        return io_->read(buf, size, nread);
    }

    int nn = srs_min(buf_->left(), (int)size);
    *nread = nn;

    if (nn) {
        buf_->read_bytes((char *)buf, nn);
    }
    return srs_success;
}

srs_error_t SrsBufferedReadWriter::read_fully(void *buf, size_t size, ssize_t *nread)
{
    if (!buf_ || buf_->empty()) {
        return io_->read_fully(buf, size, nread);
    }

    int nn = srs_min(buf_->left(), (int)size);
    if (nn) {
        buf_->read_bytes((char *)buf, nn);
    }

    int left = size - nn;
    *nread = size;

    if (left) {
        return io_->read_fully((char *)buf + nn, left, NULL);
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

srs_error_t SrsBufferedReadWriter::write(void *buf, size_t size, ssize_t *nwrite)
{
    return io_->write(buf, size, nwrite);
}

srs_error_t SrsBufferedReadWriter::writev(const iovec *iov, int iov_size, ssize_t *nwrite)
{
    return io_->writev(iov, iov_size, nwrite);
}

ISrsSslConnection::ISrsSslConnection()
{
}

ISrsSslConnection::~ISrsSslConnection()
{
}

SrsSslConnection::SrsSslConnection(ISrsProtocolReadWriter *c)
{
    transport_ = c;
    ssl_ctx_ = NULL;
    ssl_ = NULL;
}

SrsSslConnection::~SrsSslConnection()
{
    if (ssl_) {
        // this function will free bio_in_ and bio_out_
        SSL_free(ssl_);
        ssl_ = NULL;
    }

    if (ssl_ctx_) {
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = NULL;
    }
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
srs_error_t SrsSslConnection::handshake(string key_file, string crt_file)
{
    srs_error_t err = srs_success;

    // For HTTPS, try to connect over security transport.
#if (OPENSSL_VERSION_NUMBER < 0x10002000L) // v1.0.2
    ssl_ctx_ = SSL_CTX_new(TLS_method());
#else
    ssl_ctx_ = SSL_CTX_new(TLSv1_2_method());
#endif
    SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_NONE, NULL);
    srs_assert(SSL_CTX_set_cipher_list(ssl_ctx_, "ALL") == 1);

    // TODO: Setup callback, see SSL_set_ex_data and SSL_set_info_callback
    if ((ssl_ = SSL_new(ssl_ctx_)) == NULL) {
        return srs_error_new(ERROR_TLS_HANDSHAKE, "SSL_new ssl");
    }

    if ((bio_in_ = BIO_new(BIO_s_mem())) == NULL) {
        return srs_error_new(ERROR_TLS_HANDSHAKE, "BIO_new in");
    }

    if ((bio_out_ = BIO_new(BIO_s_mem())) == NULL) {
        BIO_free(bio_in_);
        return srs_error_new(ERROR_TLS_HANDSHAKE, "BIO_new out");
    }

    SSL_set_bio(ssl_, bio_in_, bio_out_);

    // SSL setup active, as server role.
    SSL_set_accept_state(ssl_);
    SSL_set_mode(ssl_, SSL_MODE_ENABLE_PARTIAL_WRITE);

    uint8_t *data = NULL;
    int r0, r1, size;

    // Setup the key and cert file for server.
    if ((r0 = SSL_use_certificate_chain_file(ssl_, crt_file.c_str())) != 1) {
        return srs_error_new(ERROR_TLS_KEY_CRT, "use cert %s", crt_file.c_str());
    }

    if ((r0 = SSL_use_RSAPrivateKey_file(ssl_, key_file.c_str(), SSL_FILETYPE_PEM)) != 1) {
        return srs_error_new(ERROR_TLS_KEY_CRT, "use key %s", key_file.c_str());
    }

    if ((r0 = SSL_check_private_key(ssl_)) != 1) {
        return srs_error_new(ERROR_TLS_KEY_CRT, "check key %s with cert %s",
                             key_file.c_str(), crt_file.c_str());
    }
    srs_info("ssl: use key %s and cert %s", key_file.c_str(), crt_file.c_str());

    // Receive ClientHello
    while (true) {
        char buf[1024];
        ssize_t nn = 0;
        if ((err = transport_->read(buf, sizeof(buf), &nn)) != srs_success) {
            return srs_error_wrap(err, "handshake: read");
        }

        if ((r0 = BIO_write(bio_in_, buf, nn)) <= 0) {
            // TODO: 0 or -1 maybe block, use BIO_should_retry to check.
            return srs_error_new(ERROR_TLS_HANDSHAKE, "BIO_write r0=%d, data=%p, size=%d", r0, buf, nn);
        }

        r0 = SSL_do_handshake(ssl_);
        r1 = SSL_get_error(ssl_, r0);
        ERR_clear_error();
        if (r0 != -1 || r1 != SSL_ERROR_WANT_READ) {
            return srs_error_new(ERROR_TLS_HANDSHAKE, "handshake r0=%d, r1=%d", r0, r1);
        }

        if ((size = BIO_get_mem_data(bio_out_, &data)) > 0) {
            // OK, reset it for the next write.
            if ((r0 = BIO_reset(bio_in_)) != 1) {
                return srs_error_new(ERROR_TLS_HANDSHAKE, "BIO_reset r0=%d", r0);
            }
            break;
        }
    }

    srs_info("tls: ClientHello done");

    // Send ServerHello, Certificate, Server Key Exchange, Server Hello Done
    size = BIO_get_mem_data(bio_out_, &data);
    if (!data || size <= 0) {
        return srs_error_new(ERROR_TLS_HANDSHAKE, "handshake data=%p, size=%d", data, size);
    }
    if ((err = transport_->write(data, size, NULL)) != srs_success) {
        return srs_error_wrap(err, "handshake: write data=%p, size=%d", data, size);
    }
    if ((r0 = BIO_reset(bio_out_)) != 1) {
        return srs_error_new(ERROR_TLS_HANDSHAKE, "BIO_reset r0=%d", r0);
    }

    srs_info("tls: ServerHello done");

    // Receive Client Key Exchange, Change Cipher Spec, Encrypted Handshake Message
    while (true) {
        char buf[1024];
        ssize_t nn = 0;
        if ((err = transport_->read(buf, sizeof(buf), &nn)) != srs_success) {
            return srs_error_wrap(err, "handshake: read");
        }

        if ((r0 = BIO_write(bio_in_, buf, nn)) <= 0) {
            // TODO: 0 or -1 maybe block, use BIO_should_retry to check.
            return srs_error_new(ERROR_TLS_HANDSHAKE, "BIO_write r0=%d, data=%p, size=%d", r0, buf, nn);
        }

        r0 = SSL_do_handshake(ssl_);
        r1 = SSL_get_error(ssl_, r0);
        ERR_clear_error();
        if (r0 == 1 && r1 == SSL_ERROR_NONE) {
            break;
        }

        if (r0 != -1 || r1 != SSL_ERROR_WANT_READ) {
            return srs_error_new(ERROR_TLS_HANDSHAKE, "handshake r0=%d, r1=%d", r0, r1);
        }

        if ((size = BIO_get_mem_data(bio_out_, &data)) > 0) {
            // OK, reset it for the next write.
            if ((r0 = BIO_reset(bio_in_)) != 1) {
                return srs_error_new(ERROR_TLS_HANDSHAKE, "BIO_reset r0=%d", r0);
            }
            break;
        }
    }

    srs_info("tls: Client done");

    // Send New Session Ticket, Change Cipher Spec, Encrypted Handshake Message
    size = BIO_get_mem_data(bio_out_, &data);
    if (!data || size <= 0) {
        return srs_error_new(ERROR_TLS_HANDSHAKE, "handshake data=%p, size=%d", data, size);
    }
    if ((err = transport_->write(data, size, NULL)) != srs_success) {
        return srs_error_wrap(err, "handshake: write data=%p, size=%d", data, size);
    }
    if ((r0 = BIO_reset(bio_out_)) != 1) {
        return srs_error_new(ERROR_TLS_HANDSHAKE, "BIO_reset r0=%d", r0);
    }

    srs_info("tls: Server done");

    return err;
}
#pragma GCC diagnostic pop

void SrsSslConnection::set_recv_timeout(srs_utime_t tm)
{
    transport_->set_recv_timeout(tm);
}

srs_utime_t SrsSslConnection::get_recv_timeout()
{
    return transport_->get_recv_timeout();
}

srs_error_t SrsSslConnection::read_fully(void *buf, size_t size, ssize_t *nread)
{
    srs_error_t err = srs_success;
    ssize_t nb = 0;
    void *p = buf;
    while ((size_t)nb < size) {
        ssize_t once_nb = 0;
        if ((err = read((char *)p + nb, size - nb, &once_nb)) != srs_success) {
            return srs_error_wrap(err, "tls: read");
        }
        nb += once_nb;
    }

    if (nread) {
        *nread = nb;
    }

    return err;
}

int64_t SrsSslConnection::get_recv_bytes()
{
    return transport_->get_recv_bytes();
}

int64_t SrsSslConnection::get_send_bytes()
{
    return transport_->get_send_bytes();
}

srs_error_t SrsSslConnection::read(void *plaintext, size_t nn_plaintext, ssize_t *nread)
{
    srs_error_t err = srs_success;

    while (true) {
        int r0 = SSL_read(ssl_, plaintext, nn_plaintext);
        int r1 = SSL_get_error(ssl_, r0);
        ERR_clear_error();
        int r2 = BIO_ctrl_pending(bio_in_);
        int r3 = SSL_is_init_finished(ssl_);

        // Peer gracefully close.
        if (r0 == 0 && r1 == SSL_ERROR_ZERO_RETURN) {
            return srs_error_new(ERROR_SOCKET_READ, "SSL_read r0=%d, r1=%d, r2=%d, r3=%d",
                                 r0, r1, r2, r3);
        }

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
            SrsUniquePtr<char[]> cipher(new char[nn_cipher]);

            // Read the cipher from SSL.
            ssize_t nn = 0;
            if ((err = transport_->read(cipher.get(), nn_cipher, &nn)) != srs_success) {
                return srs_error_wrap(err, "tls: read");
            }

            int r0 = BIO_write(bio_in_, cipher.get(), nn);
            if (r0 <= 0) {
                // TODO: 0 or -1 maybe block, use BIO_should_retry to check.
                return srs_error_new(ERROR_TLS_READ, "BIO_write r0=%d, cipher=%p, size=%d", r0, cipher.get(), nn);
            }
            continue;
        }

        // Fail for error.
        if (r0 <= 0) {
            return srs_error_new(ERROR_TLS_READ, "SSL_read r0=%d, r1=%d, r2=%d, r3=%d",
                                 r0, r1, r2, r3);
        }
    }
}

void SrsSslConnection::set_send_timeout(srs_utime_t tm)
{
    transport_->set_send_timeout(tm);
}

srs_utime_t SrsSslConnection::get_send_timeout()
{
    return transport_->get_send_timeout();
}

srs_error_t SrsSslConnection::write(void *plaintext, size_t nn_plaintext, ssize_t *nwrite)
{
    srs_error_t err = srs_success;

    for (char *p = (char *)plaintext; p < (char *)plaintext + nn_plaintext;) {
        int left = (int)nn_plaintext - (p - (char *)plaintext);
        int r0 = SSL_write(ssl_, (const void *)p, left);
        int r1 = SSL_get_error(ssl_, r0);
        ERR_clear_error();
        if (r0 <= 0) {
            return srs_error_new(ERROR_TLS_WRITE, "tls: write data=%p, size=%d, r0=%d, r1=%d", p, left, r0, r1);
        }

        // Move p to the next writing position.
        p += r0;
        if (nwrite) {
            *nwrite += (ssize_t)r0;
        }

        uint8_t *data = NULL;
        int size = BIO_get_mem_data(bio_out_, &data);
        if ((err = transport_->write(data, size, NULL)) != srs_success) {
            return srs_error_wrap(err, "tls: write data=%p, size=%d", data, size);
        }
        if ((r0 = BIO_reset(bio_out_)) != 1) {
            return srs_error_new(ERROR_TLS_WRITE, "BIO_reset r0=%d", r0);
        }
    }

    return err;
}

srs_error_t SrsSslConnection::writev(const iovec *iov, int iov_size, ssize_t *nwrite)
{
    srs_error_t err = srs_success;

    for (int i = 0; i < iov_size; i++) {
        const iovec *p = iov + i;
        if ((err = write((void *)p->iov_base, (size_t)p->iov_len, nwrite)) != srs_success) {
            return srs_error_wrap(err, "write iov #%d base=%p, size=%d", i, p->iov_base, p->iov_len);
        }
    }

    return err;
}
