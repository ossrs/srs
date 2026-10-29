//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI41_HPP
#define SRS_UTEST_AI41_HPP

/*
#include <srs_utest_ai41.hpp>
*/
#include <srs_utest.hpp>

#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_st.hpp>
#include <srs_utest_manual_protocol.hpp>


#include <string>
#include <vector>

// How far the test origin of an edge pull or a forwarder push goes before it stops.
enum RtmpConnOriginStep {
    // Not an RTMP server: an HTTP server answers the handshake with an HTTP error.
    RtmpConnOriginNotRtmp,
    // Refuses the app with an _error onStatus and closes, as an RTMP server does for an app
    // it does not serve.
    RtmpConnOriginRejectsApp,
    // Answers connect, then exits, as an origin that restarts at that moment.
    RtmpConnOriginExitsAfterConnect,
    // Answers createStream, then the connection is reset.
    RtmpConnOriginResetAfterCreateStream,
    // Serves the play or publish of the client.
    RtmpConnOriginServes,
    // Serves the play with the metadata and a key frame, then closes its sending side, as an
    // origin whose publisher stopped.
    RtmpConnOriginServesThenCloses,
};

// An RTMP origin on a random local port for one connection of an edge or a forwarder,
// which stops at the step the test picks.
class RtmpConnTestOrigin : public SrsHttpTestServer
{
public:
    RtmpConnOriginStep step_;
    std::string tc_url_;
    SrsRtmpConnType type_;
    std::string stream_;
    std::string video_;
    std::string error_;
    // For RtmpConnOriginResetAfterCreateStream, how long the origin waits after its
    // createStream response before the reset.
    srs_utime_t reset_delay_;

public:
    RtmpConnTestOrigin(RtmpConnOriginStep step) : SrsHttpTestServer("")
    {
        step_ = step;
        type_ = SrsRtmpConnUnknown;
        reset_delay_ = 0;
    }

public:
    virtual srs_error_t start()
    {
        srs_error_t err = SrsHttpTestServer::start();
        // Let State Threads run once, so its clock is fresh before the origin sets its
        // timeouts, as RawReplyTcpServer does.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        return err;
    }
    virtual srs_error_t do_cycle(srs_netfd_t cfd)
    {
        srs_error_t err = do_serve(cfd);
        if (err != srs_success) {
            error_ = srs_error_desc(err);
        }
        return err;
    }

private:
    srs_error_t do_serve(srs_netfd_t cfd)
    {
        srs_error_t err = srs_success;

        SrsStSocket skt(cfd);
        skt.set_recv_timeout(3 * SRS_UTIME_SECONDS);
        skt.set_send_timeout(3 * SRS_UTIME_SECONDS);

        if (step_ == RtmpConnOriginNotRtmp) {
            char buf[1537];
            if ((err = skt.read(buf, sizeof(buf), NULL)) != srs_success) {
                return srs_error_wrap(err, "read c0c1");
            }
            std::string res = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
            return skt.write((void *)res.data(), res.size(), NULL);
        }

        SrsRtmpServer rtmp(&skt);
        if ((err = rtmp.handshake()) != srs_success) {
            return srs_error_wrap(err, "handshake");
        }

        SrsRequest req;
        if ((err = rtmp.connect_app(&req)) != srs_success) {
            return srs_error_wrap(err, "connect app");
        }
        tc_url_ = req.tcUrl_;

        if (step_ == RtmpConnOriginRejectsApp) {
            rtmp.response_connect_reject(&req, "app not allowed");
            return err;
        }

        if ((err = rtmp.response_connect_app(&req, "127.0.0.1")) != srs_success) {
            return srs_error_wrap(err, "response connect app");
        }
        if (step_ == RtmpConnOriginExitsAfterConnect) {
            return err;
        }

        if (step_ == RtmpConnOriginResetAfterCreateStream) {
            SrsRtmpCommonMessage *msg_raw = NULL;
            SrsCreateStreamPacket *pkt_raw = NULL;
            if ((err = rtmp.expect_message<SrsCreateStreamPacket>(&msg_raw, &pkt_raw)) != srs_success) {
                return srs_error_wrap(err, "expect create stream");
            }
            SrsUniquePtr<SrsRtmpCommonMessage> msg(msg_raw);
            SrsUniquePtr<SrsCreateStreamPacket> pkt(pkt_raw);
            if ((err = rtmp.send_and_free_packet(new SrsCreateStreamResPacket(pkt->transaction_id_, 1), 0)) != srs_success) {
                return srs_error_wrap(err, "response create stream");
            }
            if (reset_delay_ > 0) {
                srs_usleep(reset_delay_);
            }

            // Close with a reset, as a load balancer in front of the origin that drops the
            // connection does.
            struct linger lg;
            lg.l_onoff = 1;
            lg.l_linger = 0;
            srs_setsockopt(srs_netfd_fileno(cfd), SOL_SOCKET, SO_LINGER, &lg, sizeof(lg));
            return err;
        }

        srs_utime_t duration = 0;
        if ((err = rtmp.identify_client(1, type_, stream_, duration)) != srs_success) {
            return srs_error_wrap(err, "identify client");
        }

        if (type_ == SrsRtmpConnPlay) {
            if ((err = rtmp.start_play(1)) != srs_success) {
                return srs_error_wrap(err, "start play");
            }

            SrsOnMetaDataPacket *meta = new SrsOnMetaDataPacket();
            meta->metadata_->set("width", SrsAmf0Any::number(1280));
            meta->metadata_->set("height", SrsAmf0Any::number(720));
            if ((err = rtmp.send_and_free_packet(meta, 1)) != srs_success) {
                return srs_error_wrap(err, "send metadata");
            }

            if ((err = rtmp.send_and_free_message(rtmp_conn_video_packet(), 1)) != srs_success) {
                return srs_error_wrap(err, "send video");
            }

            // Close the sending side only, and keep reading what the edge sends: a close with unread data in the
            // receive buffer resets the connection on Linux, and the reset drops what the edge has not read yet.
            if (step_ == RtmpConnOriginServesThenCloses) {
                shutdown(srs_netfd_fileno(cfd), SHUT_WR);
            }

            // Serve until the edge leaves.
            while (true) {
                SrsRtmpCommonMessage *msg = NULL;
                if ((err = rtmp.recv_message(&msg)) != srs_success) {
                    srs_freep(err);
                    return err;
                }
                srs_freep(msg);
            }
        }

        if ((err = rtmp.start_flash_publish(1)) != srs_success) {
            return srs_error_wrap(err, "start publish");
        }
        while (video_.empty()) {
            SrsRtmpCommonMessage *msg_raw = NULL;
            if ((err = rtmp.recv_message(&msg_raw)) != srs_success) {
                return srs_error_wrap(err, "recv media");
            }
            SrsUniquePtr<SrsRtmpCommonMessage> msg(msg_raw);
            if (msg->header_.is_video()) {
                video_.assign(msg->payload(), msg->size());
            }
        }

        return err;
    }

public:
    // An AVC key frame of 4 KB, larger than the 128-byte default chunk.
    static SrsMediaPacket *rtmp_conn_video_packet()
    {
        SrsMediaPacket *msg = new SrsMediaPacket();
        msg->timestamp_ = 40;
        msg->message_type_ = SrsFrameTypeVideo;
        char *data = new char[4096];
        for (int i = 0; i < 4096; i++) {
            data[i] = (char)(i % 251);
        }
        data[0] = 0x17;
        data[1] = 0x01;
        msg->wrap(data, 4096);
        return msg;
    }

    std::string rtmp_url(std::string stream)
    {
        return "rtmp://127.0.0.1:" + srs_strconv_format_int(get_port()) + "/live/" + stream;
    }
};

// An AMF0 command or data payload written field by field, as a peer builds one. A peer can stop
// at any field, or put a value of the wrong type in it.
class RtmpStackAmf0
{
public:
    std::string data_;

public:
    RtmpStackAmf0 &str(const std::string &v)
    {
        std::vector<char> b(SrsAmf0Size::str(v));
        SrsBuffer s(&b[0], (int)b.size());
        srs_error_t err = srs_amf0_write_string(&s, v);
        srs_freep(err);
        data_.append(&b[0], b.size());
        return *this;
    }
    RtmpStackAmf0 &num(double v)
    {
        std::vector<char> b(SrsAmf0Size::number());
        SrsBuffer s(&b[0], (int)b.size());
        srs_error_t err = srs_amf0_write_number(&s, v);
        srs_freep(err);
        data_.append(&b[0], b.size());
        return *this;
    }
    RtmpStackAmf0 &boolean(bool v)
    {
        std::vector<char> b(SrsAmf0Size::boolean());
        SrsBuffer s(&b[0], (int)b.size());
        srs_error_t err = srs_amf0_write_boolean(&s, v);
        srs_freep(err);
        data_.append(&b[0], b.size());
        return *this;
    }
    RtmpStackAmf0 &null()
    {
        char b[1];
        SrsBuffer s(b, 1);
        srs_error_t err = srs_amf0_write_null(&s);
        srs_freep(err);
        data_.append(b, 1);
        return *this;
    }
    // Writes the object and frees it.
    RtmpStackAmf0 &obj(SrsAmf0Object *v)
    {
        SrsUniquePtr<SrsAmf0Object> o(v);
        std::vector<char> b(SrsAmf0Size::object(o.get()));
        SrsBuffer s(&b[0], (int)b.size());
        srs_error_t err = o->write(&s);
        srs_freep(err);
        data_.append(&b[0], b.size());
        return *this;
    }
    RtmpStackAmf0 &raw(const char *bytes, int size)
    {
        data_.append(bytes, size);
        return *this;
    }
};

// The chunks of one RTMP message as a peer sends them at the default chunk size of 128: a fmt=0
// header on chunk stream cid, then a 1-byte fmt=3 header before each further 128 bytes.
static std::string rtmp_stack_chunks(int cid, uint8_t type, uint32_t stream_id, const std::string &payload)
{
    int size = (int)payload.size();

    std::string b;
    b.push_back((char)cid);
    b.append(3, '\0'); // timestamp
    b.push_back((char)(size >> 16));
    b.push_back((char)(size >> 8));
    b.push_back((char)size);
    b.push_back((char)type);
    for (int i = 0; i < 4; i++) {
        b.push_back((char)(stream_id >> (8 * i))); // little endian
    }

    for (int pos = 0; pos < size; pos += 128) {
        if (pos > 0) {
            b.push_back((char)(0xc0 | cid));
        }
        b.append(payload.substr(pos, 128));
    }
    return b;
}

// A TLS client on the other end of an HTTPS or RTMPS connection, such as a browser or an
// encoder. It runs OpenSSL's client over memory BIOs: what the server writes is the
// client's input, and what the client sends is what the server reads. The client acts
// each time the server reads, as a peer answers what it got.
class TlsClientPeer : public ISrsProtocolReadWriter
{
public:
    SSL_CTX *ctx_;
    SSL *ssl_;
    // The bytes from the server, and the bytes to the server.
    BIO *in_;
    BIO *out_;
    // The most bytes one server read gets, as a TCP segment; 0 for no limit.
    int segment_;
    // The server's write that fails, counted from 1, as a client gone; 0 for none.
    int fail_write_;
    // The client closes its TCP connection, with no TLS alert, once the server has
    // written this many times; -1 for never.
    int hangup_after_writes_;
    int nn_writes_;
    int nn_reads_;
    // The plaintext the client got from the server.
    std::string received_;
    // What the client sends once it has got this much plaintext.
    std::vector<std::pair<size_t, std::string> > replies_;
    srs_utime_t rtm_;
    srs_utime_t stm_;
    int64_t nn_recv_;
    int64_t nn_send_;

public:
    TlsClientPeer(bool verify_server)
    {
        ctx_ = SSL_CTX_new(TLS_client_method());
        SSL_CTX_set_verify(ctx_, verify_server ? SSL_VERIFY_PEER : SSL_VERIFY_NONE, NULL);
        ssl_ = SSL_new(ctx_);
        in_ = BIO_new(BIO_s_mem());
        out_ = BIO_new(BIO_s_mem());
        SSL_set_bio(ssl_, in_, out_);
        SSL_set_connect_state(ssl_);

        segment_ = 0;
        fail_write_ = 0;
        hangup_after_writes_ = -1;
        nn_writes_ = 0;
        nn_reads_ = 0;
        rtm_ = stm_ = SRS_UTIME_NO_TIMEOUT;
        nn_recv_ = nn_send_ = 0;
    }
    virtual ~TlsClientPeer()
    {
        SSL_free(ssl_);
        SSL_CTX_free(ctx_);
    }

public:
    bool hung_up()
    {
        return hangup_after_writes_ >= 0 && nn_writes_ >= hangup_after_writes_;
    }
    // Take what the server sent, and send what the client has to.
    void drive()
    {
        if (hung_up()) {
            return;
        }
        if (!SSL_is_init_finished(ssl_)) {
            SSL_do_handshake(ssl_);
            ERR_clear_error();
        }
        if (!SSL_is_init_finished(ssl_)) {
            return;
        }

        char buf[4096];
        int r0;
        while ((r0 = SSL_read(ssl_, buf, sizeof(buf))) > 0) {
            received_.append(buf, r0);
        }
        ERR_clear_error();

        while (!replies_.empty() && replies_.front().first <= received_.size()) {
            std::string data = replies_.front().second;
            replies_.erase(replies_.begin());
            SSL_write(ssl_, data.data(), (int)data.size());
        }
    }
    void send(const std::string &data)
    {
        drive();
        SSL_write(ssl_, data.data(), (int)data.size());
    }
    void close_notify()
    {
        drive();
        SSL_shutdown(ssl_);
        ERR_clear_error();
    }
    std::string received()
    {
        drive();
        return received_;
    }

public:
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread)
    {
        nn_reads_++;
        drive();

        int pending = hung_up() ? 0 : (int)BIO_ctrl_pending(out_);
        if (pending <= 0) {
            return srs_error_new(ERROR_SOCKET_READ, "EOF");
        }

        int n = srs_min(pending, (int)size);
        if (segment_ > 0) {
            n = srs_min(n, segment_);
        }
        BIO_read(out_, buf, n);
        nn_recv_ += n;
        if (nread) {
            *nread = n;
        }
        return srs_success;
    }
    virtual srs_error_t read_fully(void *buf, size_t size, ssize_t *nread)
    {
        srs_error_t err = srs_success;
        size_t nb = 0;
        while (nb < size) {
            ssize_t n = 0;
            if ((err = read((char *)buf + nb, size - nb, &n)) != srs_success) {
                return err;
            }
            nb += n;
        }
        if (nread) {
            *nread = nb;
        }
        return err;
    }
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite)
    {
        nn_writes_++;
        if (nn_writes_ == fail_write_) {
            return srs_error_new(ERROR_SOCKET_WRITE, "client gone");
        }
        BIO_write(in_, buf, (int)size);
        nn_send_ += size;
        if (nwrite) {
            *nwrite = size;
        }
        return srs_success;
    }
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t *nwrite)
    {
        srs_error_t err = srs_success;
        ssize_t total = 0;
        for (int i = 0; i < iov_size; i++) {
            if ((err = write(iov[i].iov_base, iov[i].iov_len, NULL)) != srs_success) {
                return err;
            }
            total += iov[i].iov_len;
        }
        if (nwrite) {
            *nwrite = total;
        }
        return err;
    }
    virtual void set_recv_timeout(srs_utime_t tm)
    {
        rtm_ = tm;
    }
    virtual srs_utime_t get_recv_timeout()
    {
        return rtm_;
    }
    virtual void set_send_timeout(srs_utime_t tm)
    {
        stm_ = tm;
    }
    virtual srs_utime_t get_send_timeout()
    {
        return stm_;
    }
    virtual int64_t get_recv_bytes()
    {
        return nn_recv_;
    }
    virtual int64_t get_send_bytes()
    {
        return nn_send_;
    }
};

// The peer of an RTMP connection, whose connection is reset after it took a given number of
// messages: SrsProtocol writes each message with one writev, and a writev after the reset fails.
class RtmpStackResetIO : public MockBufferIO
{
public:
    int writevs_left_;
    int writevs_tried_;

public:
    RtmpStackResetIO(int writevs_left) : writevs_left_(writevs_left), writevs_tried_(0) {}

public:
    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t *nwrite)
    {
        writevs_tried_++;
        if (writevs_left_ <= 0) {
            return srs_error_new(ERROR_SOCKET_WRITE, "connection reset by peer");
        }
        writevs_left_--;
        return MockBufferIO::writev(iov, iov_size, nwrite);
    }
};

// A binding request as Chrome sends it to an ice-lite server, as the controlling agent, signed with the server's ICE
// password; with `nominate`, it carries USE-CANDIDATE.
extern std::string stun_controlling_binding_request(const std::string &username, const std::string &pwd, bool nominate);

#endif
