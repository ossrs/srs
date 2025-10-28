//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest.hpp>

#include <srs_app_config.hpp>
#include <srs_app_log.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_app_server.hpp>
#include <srs_app_st.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_protocol_st.hpp>

#include <string>
using namespace std;

#include <sys/mman.h>
#include <sys/types.h>

#include <srs_app_factory.hpp>
#include <srs_app_srt_server.hpp>
#include <srt/srt.h>

// For RTMP test server
#include <srs_protocol_conn.hpp>
#include <srs_protocol_rtmp_conn.hpp>
#include <srs_protocol_rtmp_stack.hpp>

// For TCP test server and client
#include <srs_app_listener.hpp>
#include <srs_app_rtc_codec.hpp>
#include <srs_app_st.hpp>

// Temporary disk config.
std::string _srs_tmp_file_prefix = "/tmp/srs-utest-";
// Temporary network config.
std::string _srs_tmp_host = "127.0.0.1";
int _srs_tmp_port = 11935;
srs_utime_t _srs_tmp_timeout = (100 * SRS_UTIME_MILLISECONDS);

// kernel module.
ISrsLog *_srs_log = NULL;
ISrsContext *_srs_context = NULL;
// app module.
SrsConfig *_srs_config = NULL;
bool _srs_in_docker = false;
bool _srs_config_by_env = false;

// @global kernel factory.
ISrsAppFactory *_srs_app_factory = new SrsAppFactory();
ISrsKernelFactory *_srs_kernel_factory = _srs_app_factory;

#ifdef SRS_FFMPEG_FIT
// Register FFmpeg log callback funciton.
SrsFFmpegLogHelper _srs_ffmpeg_log_helper;
#endif

// The binary name of SRS.
const char *_srs_binary = NULL;

#include <srs_app_st.hpp>

static void srs_srt_utest_null_log_handler(void *opaque, int level, const char *file, int line,
                                           const char *area, const char *message)
{
    // srt null log handler, do no print any log.
}

// Initialize global settings.
srs_error_t prepare_main()
{
    srs_error_t err = srs_success;

    // Root global objects, should be created before any other global objects.
    _srs_log = new SrsFileLog();
    _srs_context = new SrsThreadContext();
    _srs_config = new SrsConfig();

    // For background context id.
    _srs_context->set_id(_srs_context->generate_id());

    if ((err = srs_global_initialize()) != srs_success) {
        return srs_error_wrap(err, "init global");
    }

    _srs_server = new SrsServer();

    srs_freep(_srs_log);
    _srs_log = new MockEmptyLog(SrsLogLevelError);

    if ((err = _srs_rtc_dtls_certificate->initialize()) != srs_success) {
        return srs_error_wrap(err, "rtc dtls certificate initialize");
    }

    srs_freep(_srs_context);
    _srs_context = new SrsThreadContext();

    if ((err = srs_srt_log_initialize()) != srs_success) {
        return srs_error_wrap(err, "srt log initialize");
    }

#ifdef SRS_FFMPEG_FIT
    // Disable FFmpeg detail log in utest.
    _srs_ffmpeg_log_helper.disable_ffmpeg_log();
#endif

    // Prevent the output of srt logs in utest.
    srt_setloghandler(NULL, srs_srt_utest_null_log_handler);
    // Set SRT log level to FATAL to suppress ERROR and WARNING logs in unit tests.
    // LOG_CRIT (2) is the highest level that suppresses most logs.
    srt_setloglevel(LOG_CRIT);

    _srt_eventloop = new SrsSrtEventLoop();
    if ((err = _srt_eventloop->initialize()) != srs_success) {
        return srs_error_wrap(err, "srt poller initialize");
    }
    if ((err = _srt_eventloop->start()) != srs_success) {
        return srs_error_wrap(err, "srt poller start");
    }

    return err;
}

// We could do something in the main of utest.
// Copy from gtest-1.6.0/src/gtest_main.cc
GTEST_API_ int main(int argc, char **argv)
{
    srs_error_t err = srs_success;

    _srs_binary = argv[0];

    if ((err = prepare_main()) != srs_success) {
        fprintf(stderr, "Failed, %s\n", srs_error_desc(err).c_str());

        int ret = srs_error_code(err);
        srs_freep(err);
        return ret;
    }

    testing::InitGoogleTest(&argc, argv);
    int r0 = RUN_ALL_TESTS();

    return r0;
}

MockEmptyLog::MockEmptyLog(SrsLogLevel l)
{
    level_ = l;
}

MockEmptyLog::~MockEmptyLog()
{
}

void srs_bytes_print(char *pa, int size)
{
    for (int i = 0; i < size; i++) {
        char v = pa[i];
        printf("%#x ", v);
    }
    printf("\n");
}

// basic test and samples.
VOID TEST(SampleTest, FastSampleInt64Test)
{
    EXPECT_EQ(1, (int)sizeof(int8_t));
    EXPECT_EQ(2, (int)sizeof(int16_t));
    EXPECT_EQ(4, (int)sizeof(int32_t));
    EXPECT_EQ(8, (int)sizeof(int64_t));
}

VOID TEST(SampleTest, FastSampleMacrosTest)
{
    EXPECT_TRUE(1);
    EXPECT_FALSE(0);

    EXPECT_EQ(1, 1); // ==
    EXPECT_NE(1, 2); // !=
    EXPECT_LE(1, 2); // <=
    EXPECT_LT(1, 2); // <
    EXPECT_GE(2, 1); // >=
    EXPECT_GT(2, 1); // >

    EXPECT_STREQ("winlin", "winlin");
    EXPECT_STRNE("winlin", "srs");
    EXPECT_STRCASEEQ("winlin", "Winlin");
    EXPECT_STRCASENE("winlin", "srs");

    EXPECT_FLOAT_EQ(1.0, 1.000000000000001);
    EXPECT_DOUBLE_EQ(1.0, 1.0000000000000001);
    EXPECT_NEAR(10, 15, 5);
}

VOID TEST(SampleTest, StringEQTest)
{
    string str = "100";
    EXPECT_TRUE("100" == str);
    EXPECT_EQ("100", str);
    EXPECT_STREQ("100", str.c_str());
}

class MockSrsContextId
{
public:
    MockSrsContextId()
    {
        bind_ = NULL;
    }
    MockSrsContextId(const MockSrsContextId &cp)
    {
        bind_ = NULL;
        if (cp.bind_) {
            bind_ = cp.bind_->copy();
        }
    }
    MockSrsContextId &operator=(const MockSrsContextId &cp)
    {
        srs_freep(bind_);
        if (cp.bind_) {
            bind_ = cp.bind_->copy();
        }
        return *this;
    }
    virtual ~MockSrsContextId()
    {
        srs_freep(bind_);
    }

public:
    MockSrsContextId *copy() const
    {
        MockSrsContextId *cp = new MockSrsContextId();
        if (bind_) {
            cp->bind_ = bind_->copy();
        }
        return cp;
    }

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    MockSrsContextId *bind_;
};

VOID TEST(SampleTest, ContextTest)
{
    MockSrsContextId cid;
    cid.bind_ = new MockSrsContextId();

    static std::map<int, MockSrsContextId> cache;
    cache[0] = cid;
    cache[0] = cid;
}

MockProtectedBuffer::MockProtectedBuffer() : raw_memory_(NULL), size_(0), data_(NULL)
{
}

MockProtectedBuffer::~MockProtectedBuffer()
{
    if (size_ && raw_memory_) {
        long page_size = sysconf(_SC_PAGESIZE);
        munmap(raw_memory_, page_size * 2);
    }
}

int MockProtectedBuffer::alloc(int size)
{
    srs_assert(!raw_memory_);

    long page_size = sysconf(_SC_PAGESIZE);
    if (size >= page_size)
        return -1;

    char *data = (char *)mmap(NULL, page_size * 2, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (data == MAP_FAILED) {
        return -1;
    }

    size_ = size;
    raw_memory_ = data;
    data_ = data + page_size - size;

    int r0 = mprotect(data + page_size, page_size, PROT_NONE);
    if (r0 < 0) {
        return r0;
    }

    return 0;
}

SrsCoroutineChan::SrsCoroutineChan()
{
    trd_ = NULL;
    lock_ = srs_mutex_new();
}

SrsCoroutineChan::~SrsCoroutineChan()
{
    srs_mutex_destroy(lock_);
}

SrsCoroutineChan &SrsCoroutineChan::push(void *value)
{
    SrsLocker(&lock_);

    args_.push_back(value);
    return *this;
}

void *SrsCoroutineChan::pop()
{
    SrsLocker(&lock_);

    void *arg = *args_.begin();
    args_.erase(args_.begin());
    return arg;
}

SrsCoroutineChan *SrsCoroutineChan::copy()
{
    SrsLocker(&lock_);

    SrsCoroutineChan *cp = new SrsCoroutineChan();
    cp->args_ = args_;
    cp->trd_ = trd_;
    return cp;
}

extern string mock_http_response(int status, string content);

SrsHttpTestServer::SrsHttpTestServer(string response_body) : response_body_(response_body)
{
    trd_ = new SrsSTCoroutine("http-test", this);
    fd_ = NULL;
    ip_ = "127.0.0.1";
    // Generate random port in range [30000, 60000]
    SrsRand rand;
    port_ = rand.integer(30000, 60000);
}

SrsHttpTestServer::~SrsHttpTestServer()
{
    close();
    srs_freep(trd_);
    srs_close_stfd(fd_);
}

srs_error_t SrsHttpTestServer::start()
{
    srs_error_t err = srs_success;

    // Retry up to 3 times with different random ports if listen fails
    for (int retry = 0; retry < 3; retry++) {
        if ((err = srs_tcp_listen(ip_, port_, &fd_)) == srs_success) {
            break;
        }

        // If this is not the last retry, generate a new random port and try again
        if (retry < 2) {
            srs_freep(err);
            SrsRand rand;
            port_ = rand.integer(30000, 60000);
            srs_trace("HTTP test server listen failed on %s:%d, retry %d with new port %d",
                      ip_.c_str(), port_, retry + 1, port_);
        }
    }

    if (err != srs_success) {
        return srs_error_wrap(err, "listen %s:%d after 3 retries", ip_.c_str(), port_);
    }

    return trd_->start();
}

void SrsHttpTestServer::close()
{
    if (trd_) {
        trd_->stop();
    }
    srs_close_stfd(fd_);
}

string SrsHttpTestServer::url()
{
    return "http://" + ip_ + ":" + srs_strconv_format_int(port_);
}

int SrsHttpTestServer::get_port()
{
    return port_;
}

srs_error_t SrsHttpTestServer::cycle()
{
    srs_error_t err = srs_success;

    srs_netfd_t cfd = srs_accept(fd_, NULL, NULL, SRS_UTIME_NO_TIMEOUT);
    if (cfd == NULL) {
        return err;
    }

    err = do_cycle(cfd);
    srs_close_stfd(cfd);
    srs_freep(err);

    return err;
}

srs_error_t SrsHttpTestServer::do_cycle(srs_netfd_t cfd)
{
    srs_error_t err = srs_success;

    SrsStSocket skt(cfd);
    skt.set_recv_timeout(1 * SRS_UTIME_SECONDS);
    skt.set_send_timeout(1 * SRS_UTIME_SECONDS);

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return err;
        }

        char buf[1024];
        if ((err = skt.read(buf, 1024, NULL)) != srs_success) {
            return err;
        }

        // Generate proper HTTP response
        string res = mock_http_response(200, response_body_);
        if ((err = skt.write((char *)res.data(), (int)res.length(), NULL)) != srs_success) {
            return err;
        }
    }

    return err;
}

SrsHttpsTestServer::SrsHttpsTestServer(string response_body, string key_file, string cert_file)
    : response_body_(response_body), ssl_key_file_(key_file), ssl_cert_file_(cert_file)
{
    trd_ = new SrsFastCoroutine("https-test", this);
    fd_ = NULL;
    ip_ = "127.0.0.1";
    // Generate random port in range [30000, 60000]
    SrsRand rand;
    port_ = rand.integer(30000, 60000);
}

SrsHttpsTestServer::~SrsHttpsTestServer()
{
    close();
    srs_freep(trd_);
}

srs_error_t SrsHttpsTestServer::start()
{
    srs_error_t err = srs_success;

    // Retry up to 3 times with different random ports if listen fails
    for (int retry = 0; retry < 3; retry++) {
        if ((err = srs_tcp_listen(ip_, port_, &fd_)) == srs_success) {
            break;
        }

        // If this is not the last retry, generate a new random port and try again
        if (retry < 2) {
            srs_freep(err);
            SrsRand rand;
            port_ = rand.integer(30000, 60000);
            srs_trace("HTTPS test server listen failed on %s:%d, retry %d with new port %d",
                      ip_.c_str(), port_, retry + 1, port_);
        }
    }

    if (err != srs_success) {
        return srs_error_wrap(err, "listen %s:%d after 3 retries", ip_.c_str(), port_);
    }

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start coroutine");
    }

    return err;
}

void SrsHttpsTestServer::close()
{
    if (trd_) {
        trd_->stop();
    }
    if (fd_) {
        srs_close_stfd(fd_);
        fd_ = NULL;
    }
}

string SrsHttpsTestServer::url()
{
    return "https://" + ip_ + ":" + srs_strconv_format_int(port_);
}

int SrsHttpsTestServer::get_port()
{
    return port_;
}

srs_error_t SrsHttpsTestServer::cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "pull");
        }

        srs_netfd_t client_fd = srs_accept(fd_, NULL, NULL, SRS_UTIME_NO_TIMEOUT);
        if (client_fd == NULL) {
            return srs_error_new(ERROR_SOCKET_ACCEPT, "accept failed");
        }

        if ((err = handle_client(client_fd)) != srs_success) {
            srs_warn("handle client failed, err=%s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
    }

    return err;
}

srs_error_t SrsHttpsTestServer::handle_client(srs_netfd_t client_fd)
{
    srs_error_t err = srs_success;

    SrsStSocket *skt = new SrsStSocket(client_fd);
    SrsUniquePtr<SrsStSocket> skt_uptr(skt);

    // Create SSL connection
    SrsSslConnection *ssl = new SrsSslConnection(skt);
    SrsUniquePtr<SrsSslConnection> ssl_uptr(ssl);

    // Perform SSL handshake
    if ((err = ssl->handshake(ssl_key_file_, ssl_cert_file_)) != srs_success) {
        return srs_error_wrap(err, "ssl handshake");
    }

    // Read HTTP request (simplified - just read some data)
    char buf[4096];
    ssize_t nread = 0;
    if ((err = ssl->read(buf, sizeof(buf), &nread)) != srs_success) {
        return srs_error_wrap(err, "read request");
    }

    // Send HTTP response
    string response = mock_http_response(200, response_body_);
    if ((err = ssl->write((void *)response.data(), response.length(), NULL)) != srs_success) {
        return srs_error_wrap(err, "write response");
    }

    return err;
}

SrsRtmpTestServer::SrsRtmpTestServer(string app, string stream) : app_(app), stream_(stream)
{
    trd_ = new SrsSTCoroutine("rtmp-test", this);
    fd_ = NULL;
    ip_ = "127.0.0.1";
    enable_publish_ = true;
    enable_play_ = true;
    // Generate random port in range [30000, 60000]
    SrsRand rand;
    port_ = rand.integer(30000, 60000);
}

SrsRtmpTestServer::~SrsRtmpTestServer()
{
    close();
    srs_freep(trd_);
    srs_close_stfd(fd_);
}

srs_error_t SrsRtmpTestServer::start()
{
    srs_error_t err = srs_success;

    // Retry up to 3 times with different random ports if listen fails
    for (int retry = 0; retry < 3; retry++) {
        if ((err = srs_tcp_listen(ip_, port_, &fd_)) == srs_success) {
            break;
        }

        // If this is not the last retry, generate a new random port and try again
        if (retry < 2) {
            srs_freep(err);
            SrsRand rand;
            port_ = rand.integer(30000, 60000);
            srs_trace("RTMP test server listen failed on %s:%d, retry %d with new port %d",
                      ip_.c_str(), port_, retry + 1, port_);
        }
    }

    if (err != srs_success) {
        return srs_error_wrap(err, "listen %s:%d after 3 retries", ip_.c_str(), port_);
    }

    return trd_->start();
}

void SrsRtmpTestServer::close()
{
    if (trd_) {
        trd_->stop();
    }
    srs_close_stfd(fd_);
}

string SrsRtmpTestServer::url()
{
    return "rtmp://" + ip_ + ":" + srs_strconv_format_int(port_) + "/" + app_ + "/" + stream_;
}

int SrsRtmpTestServer::get_port()
{
    return port_;
}

void SrsRtmpTestServer::enable_publish(bool v)
{
    enable_publish_ = v;
}

void SrsRtmpTestServer::enable_play(bool v)
{
    enable_play_ = v;
}

srs_error_t SrsRtmpTestServer::cycle()
{
    srs_error_t err = srs_success;

    srs_netfd_t cfd = srs_accept(fd_, NULL, NULL, SRS_UTIME_NO_TIMEOUT);
    if (cfd == NULL) {
        return err;
    }

    err = do_cycle(cfd);
    srs_close_stfd(cfd);
    srs_freep(err);

    return err;
}

srs_error_t SrsRtmpTestServer::do_cycle(srs_netfd_t cfd)
{
    return handle_rtmp_client(cfd);
}

srs_error_t SrsRtmpTestServer::handle_rtmp_client(srs_netfd_t cfd)
{
    srs_error_t err = srs_success;

    SrsStSocket skt(cfd);
    skt.set_recv_timeout(5 * SRS_UTIME_SECONDS);
    skt.set_send_timeout(5 * SRS_UTIME_SECONDS);

    // Create RTMP server to handle the client
    SrsRtmpServer rtmp(&skt);

    // Perform RTMP handshake
    if ((err = rtmp.handshake()) != srs_success) {
        return srs_error_wrap(err, "rtmp handshake");
    }

    // Handle connect app
    SrsRequest req;
    if ((err = rtmp.connect_app(&req)) != srs_success) {
        return srs_error_wrap(err, "rtmp connect app");
    }

    // Respond to connect app
    if ((err = rtmp.response_connect_app(&req, ip_.c_str())) != srs_success) {
        return srs_error_wrap(err, "rtmp response connect app");
    }

    // Set window ack size
    if ((err = rtmp.set_window_ack_size(2500000)) != srs_success) {
        return srs_error_wrap(err, "rtmp set window ack size");
    }

    // Set peer bandwidth
    if ((err = rtmp.set_peer_bandwidth(2500000, 2)) != srs_success) {
        return srs_error_wrap(err, "rtmp set peer bandwidth");
    }

    // Send onBWDone
    if ((err = rtmp.on_bw_done()) != srs_success) {
        return srs_error_wrap(err, "rtmp on bw done");
    }

    // Identify client (play or publish)
    int stream_id = 1; // Use a fixed stream ID for testing
    SrsRtmpConnType type = SrsRtmpConnUnknown;
    string stream_name;
    srs_utime_t duration = 0;

    if ((err = rtmp.identify_client(stream_id, type, stream_name, duration)) != srs_success) {
        return srs_error_wrap(err, "rtmp identify client");
    }

    // Set chunk size
    if ((err = rtmp.set_chunk_size(4096)) != srs_success) {
        return srs_error_wrap(err, "rtmp set chunk size");
    }

    // Handle based on client type
    if (srs_client_type_is_publish(type)) {
        if (!enable_publish_) {
            return srs_error_new(ERROR_RTMP_ACCESS_DENIED, "publish not enabled");
        }
        // For publish, we just accept it and don't send any response
        // The client will start sending media data
    } else {
        if (!enable_play_) {
            return srs_error_new(ERROR_RTMP_ACCESS_DENIED, "play not enabled");
        }
        // For play, send play start response
        if ((err = rtmp.start_play(stream_id)) != srs_success) {
            return srs_error_wrap(err, "rtmp start play");
        }
    }

    return err;
}

SrsTestTcpServer::SrsTestTcpServer(string ip)
{
    trd_ = NULL;
    listener_ = NULL;
    conn_ = NULL;
    ip_ = ip;
    // Generate random port in range [30000, 60000]
    SrsRand rand;
    port_ = 30000 + (rand.integer() % (60000 - 30000 + 1));
}

SrsTestTcpServer::~SrsTestTcpServer()
{
    close();
    srs_freep(conn_);
}

srs_error_t SrsTestTcpServer::start()
{
    srs_error_t err = srs_success;

    listener_ = new SrsTcpListener(this);
    listener_->set_endpoint(ip_, port_);

    if ((err = listener_->listen()) != srs_success) {
        return srs_error_wrap(err, "tcp listen %s:%d", ip_.c_str(), port_);
    }

    // Get the actual port that was assigned
    port_ = listener_->port();

    trd_ = new SrsSTCoroutine("tcp-test", this);
    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start tcp test server");
    }

    return err;
}

void SrsTestTcpServer::close()
{
    if (listener_) {
        listener_->close();
        srs_freep(listener_);
    }

    if (trd_) {
        trd_->stop();
        srs_freep(trd_);
    }
}

int SrsTestTcpServer::get_port()
{
    return port_;
}

SrsTcpConnection *SrsTestTcpServer::get_connection()
{
    return conn_;
}

srs_error_t SrsTestTcpServer::cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "tcp test server");
        }

        // Just wait for connections, the listener handles them via on_tcp_client
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }

    return err;
}

srs_error_t SrsTestTcpServer::on_tcp_client(ISrsListener *listener, srs_netfd_t stfd)
{
    srs_freep(conn_);
    conn_ = new SrsTcpConnection(stfd);
    return srs_success;
}

SrsTestTcpClient::SrsTestTcpClient(string host, int port, srs_utime_t timeout)
{
    client_ = NULL;
    conn_ = NULL;
    host_ = host;
    port_ = port;
    timeout_ = timeout;
}

SrsTestTcpClient::~SrsTestTcpClient()
{
    close();
}

srs_error_t SrsTestTcpClient::connect()
{
    srs_error_t err = srs_success;

    close(); // Close any existing connection

    client_ = new SrsTcpClient(host_, port_, timeout_);
    if ((err = client_->connect()) != srs_success) {
        return srs_error_wrap(err, "tcp client connect %s:%d", host_.c_str(), port_);
    }

    // Create SrsTcpConnection from the connected client
    // We need to get the file descriptor from the client
    // Since SrsTcpClient doesn't expose the fd directly, we'll create a new connection
    srs_netfd_t stfd = NULL;
    if ((err = srs_tcp_connect(host_, port_, timeout_, &stfd)) != srs_success) {
        return srs_error_wrap(err, "tcp connect for connection %s:%d", host_.c_str(), port_);
    }

    conn_ = new SrsTcpConnection(stfd);

    return err;
}

void SrsTestTcpClient::close()
{
    srs_freep(client_);
    srs_freep(conn_);
}

SrsTcpConnection *SrsTestTcpClient::get_connection()
{
    return conn_;
}

srs_error_t SrsTestTcpClient::write(void *buf, size_t size, ssize_t *nwrite)
{
    if (!client_) {
        return srs_error_new(ERROR_SOCKET_WRITE, "client not connected");
    }
    return client_->write(buf, size, nwrite);
}

srs_error_t SrsTestTcpClient::read(void *buf, size_t size, ssize_t *nread)
{
    if (!client_) {
        return srs_error_new(ERROR_SOCKET_READ, "client not connected");
    }
    return client_->read(buf, size, nread);
}

SrsUdpTestServer::SrsUdpTestServer(string host)
{
    host_ = host;
    lfd_ = NULL;
    trd_ = NULL;
    socket_ = NULL;
    started_ = false;
    // Generate random port in range [30000, 60000]
    SrsRand rand;
    port_ = 30000 + (rand.integer() % (60000 - 30000 + 1));
}

SrsUdpTestServer::~SrsUdpTestServer()
{
    stop();
    srs_freep(socket_);
    srs_close_stfd(lfd_);
}

srs_error_t SrsUdpTestServer::start()
{
    srs_error_t err = srs_success;

    if (started_) {
        return err;
    }

    // Create UDP socket - retry up to 3 times with different random ports if listen fails
    for (int retry = 0; retry < 3; retry++) {
        if ((err = srs_udp_listen(host_, port_, &lfd_)) == srs_success) {
            break;
        }

        // If this is not the last retry, generate a new random port and try again
        if (retry < 2) {
            srs_freep(err);
            SrsRand rand;
            port_ = 30000 + (rand.integer() % (60000 - 30000 + 1));
            srs_trace("UDP test server listen failed on %s:%d, retry %d with new port %d",
                      host_.c_str(), port_, retry + 1, port_);
        }
    }

    if (err != srs_success) {
        return srs_error_wrap(err, "udp listen %s:%d after 3 retries", host_.c_str(), port_);
    }

    // Get the actual port that was assigned
    int actual_fd = srs_netfd_fileno(lfd_);
    sockaddr_storage addr;
    socklen_t addrlen = sizeof(addr);
    if (getsockname(actual_fd, (sockaddr *)&addr, &addrlen) == 0) {
        if (addr.ss_family == AF_INET) {
            port_ = ntohs(((sockaddr_in *)&addr)->sin_port);
        } else if (addr.ss_family == AF_INET6) {
            port_ = ntohs(((sockaddr_in6 *)&addr)->sin6_port);
        }
    }

    // Create socket wrapper
    socket_ = new SrsStSocket(lfd_);

    // Start coroutine to handle packets
    trd_ = new SrsSTCoroutine("udp-test", this);
    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start udp test server");
    }

    started_ = true;
    return err;
}

void SrsUdpTestServer::stop()
{
    started_ = false;
    if (trd_) {
        trd_->stop();
        srs_freep(trd_);
    }
}

int SrsUdpTestServer::get_port()
{
    return port_;
}

SrsStSocket *SrsUdpTestServer::get_socket()
{
    return socket_;
}

srs_error_t SrsUdpTestServer::cycle()
{
    srs_error_t err = srs_success;

    while (started_) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "udp test server");
        }

        // Simple echo server - receive and echo back using recvfrom/sendto
        char buf[1024];
        sockaddr_storage from;
        int fromlen = sizeof(from);

        ssize_t nread = srs_recvfrom(lfd_, buf, sizeof(buf), (sockaddr *)&from, &fromlen, 10 * SRS_UTIME_MILLISECONDS);
        if (nread <= 0) {
            continue; // Timeout or error, continue
        }

        // Echo back the data
        ssize_t nwrite = srs_sendto(lfd_, buf, nread, (sockaddr *)&from, fromlen, 10 * SRS_UTIME_MILLISECONDS);
        if (nwrite <= 0) {
            continue; // Error sending, continue
        }
    }

    return err;
}

SrsUdpTestClient::SrsUdpTestClient(string host, int port, srs_utime_t timeout)
{
    host_ = host;
    port_ = port;
    timeout_ = timeout;
    stfd_ = NULL;
    socket_ = NULL;
    memset(&server_addr_, 0, sizeof(server_addr_));
    server_addrlen_ = 0;
}

SrsUdpTestClient::~SrsUdpTestClient()
{
    close();
}

srs_error_t SrsUdpTestClient::connect()
{
    srs_error_t err = srs_success;

    close();

    // Create UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1) {
        return srs_error_new(ERROR_SOCKET_CREATE, "create udp socket");
    }

    stfd_ = srs_netfd_open_socket(sock);
    if (stfd_ == NULL) {
        ::close(sock);
        return srs_error_new(ERROR_ST_OPEN_SOCKET, "open udp socket");
    }

    // Setup server address
    sockaddr_in *addr = (sockaddr_in *)&server_addr_;
    addr->sin_family = AF_INET;
    addr->sin_port = htons(port_);
    if (inet_pton(AF_INET, host_.c_str(), &addr->sin_addr) <= 0) {
        close();
        return srs_error_new(ERROR_SYSTEM_IP_INVALID, "invalid ip %s", host_.c_str());
    }
    server_addrlen_ = sizeof(sockaddr_in);

    // Create socket wrapper
    socket_ = new SrsStSocket(stfd_);
    socket_->set_recv_timeout(timeout_);
    socket_->set_send_timeout(timeout_);

    return err;
}

void SrsUdpTestClient::close()
{
    srs_freep(socket_);
    srs_close_stfd(stfd_);
}

SrsStSocket *SrsUdpTestClient::get_socket()
{
    return socket_;
}

srs_error_t SrsUdpTestClient::sendto(void *buf, size_t size, ssize_t *nwrite)
{
    if (!stfd_) {
        return srs_error_new(ERROR_SOCKET_WRITE, "udp client not connected");
    }

    ssize_t nb_write = srs_sendto(stfd_, buf, size, (sockaddr *)&server_addr_, server_addrlen_, timeout_);
    if (nb_write <= 0) {
        if (nb_write < 0 && errno == ETIME) {
            return srs_error_new(ERROR_SOCKET_TIMEOUT, "sendto timeout %d ms", srsu2msi(timeout_));
        }
        return srs_error_new(ERROR_SOCKET_WRITE, "sendto failed");
    }

    if (nwrite) {
        *nwrite = nb_write;
    }
    return srs_success;
}

srs_error_t SrsUdpTestClient::recvfrom(void *buf, size_t size, ssize_t *nread)
{
    if (!stfd_) {
        return srs_error_new(ERROR_SOCKET_READ, "udp client not connected");
    }

    sockaddr_storage from;
    int fromlen = sizeof(from);
    ssize_t nb_read = srs_recvfrom(stfd_, buf, size, (sockaddr *)&from, &fromlen, timeout_);
    if (nb_read <= 0) {
        if (nb_read < 0 && errno == ETIME) {
            return srs_error_new(ERROR_SOCKET_TIMEOUT, "recvfrom timeout %d ms", srsu2msi(timeout_));
        }
        return srs_error_new(ERROR_SOCKET_READ, "recvfrom failed");
    }

    if (nread) {
        *nread = nb_read;
    }
    return srs_success;
}
