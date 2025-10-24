//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_PUBLIC_SHARED_HPP
#define SRS_UTEST_PUBLIC_SHARED_HPP

// Before define the private/protected, we must include some system header files.
// Or it may fail with:
//      redeclared with different access struct __xfer_bufptrs
// @see https://stackoverflow.com/questions/47839718/sstream-redeclared-with-public-access-compiler-error
#include "gtest/gtest.h"

// Note: The #define private public and #define protected public are now handled in srs_core.hpp
// when SRS_FORCE_PUBLIC4UTEST is enabled (automatically enabled when --utest=on is specified).
// This ensures consistent class layout between production code and utest code with AddressSanitizer.

/*
#include <srs_utest.hpp>
*/
#include <srs_core.hpp>

#include <string>
using namespace std;

#include <srs_app_log.hpp>
#include <srs_kernel_stream.hpp>

// Include headers for TCP test classes
#include <srs_app_listener.hpp>
#include <srs_protocol_conn.hpp>

// Include headers for all the mocks.
#include <srs_utest_manual_mock.hpp>

// we add an empty macro for upp to show the smart tips.
#define VOID

// Temporary disk config.
extern std::string _srs_tmp_file_prefix;
// Temporary network config.
extern std::string _srs_tmp_host;
extern int _srs_tmp_port;
extern srs_utime_t _srs_tmp_timeout;

// For errors.
// @remark we directly delete the err, because we allow user to append message if fail.
#define HELPER_EXPECT_SUCCESS(x)                                \
    if ((err = x) != srs_success)                               \
        fprintf(stderr, "err %s", srs_error_desc(err).c_str()); \
    if (err != srs_success)                                     \
        delete err;                                             \
    EXPECT_TRUE(srs_success == err)
#define HELPER_EXPECT_FAILED(x)   \
    if ((err = x) != srs_success) \
        delete err;               \
    EXPECT_TRUE(srs_success != err)

// For errors, assert.
// @remark we directly delete the err, because we allow user to append message if fail.
#define HELPER_ASSERT_SUCCESS(x)                                \
    if ((err = x) != srs_success)                               \
        fprintf(stderr, "err %s", srs_error_desc(err).c_str()); \
    if (err != srs_success)                                     \
        delete err;                                             \
    ASSERT_TRUE(srs_success == err)
#define HELPER_ASSERT_FAILED(x)   \
    if ((err = x) != srs_success) \
        delete err;               \
    ASSERT_TRUE(srs_success != err)

// For init array data.
#define HELPER_ARRAY_INIT(buf, sz, val)        \
    for (int _iii = 0; _iii < (int)sz; _iii++) \
    (buf)[_iii] = val

// Dump simple stream to string.
#define HELPER_BUFFER2STR(io) \
    string((const char *)(io)->bytes(), (size_t)(io)->length())

// Covert uint8_t array to string.
#define HELPER_ARR2STR(arr, size) \
    string((char *)(arr), (int)size)

// the asserts of gtest:
//    * {ASSERT|EXPECT}_EQ(expected, actual): Tests that expected == actual
//    * {ASSERT|EXPECT}_NE(v1, v2):           Tests that v1 != v2
//    * {ASSERT|EXPECT}_LT(v1, v2):           Tests that v1 < v2
//    * {ASSERT|EXPECT}_LE(v1, v2):           Tests that v1 <= v2
//    * {ASSERT|EXPECT}_GT(v1, v2):           Tests that v1 > v2
//    * {ASSERT|EXPECT}_GE(v1, v2):           Tests that v1 >= v2
//    * {ASSERT|EXPECT}_STREQ(s1, s2):     Tests that s1 == s2
//    * {ASSERT|EXPECT}_STRNE(s1, s2):     Tests that s1 != s2
//    * {ASSERT|EXPECT}_STRCASEEQ(s1, s2): Tests that s1 == s2, ignoring case
//    * {ASSERT|EXPECT}_STRCASENE(s1, s2): Tests that s1 != s2, ignoring case
//    * {ASSERT|EXPECT}_FLOAT_EQ(expected, actual): Tests that two float values are almost equal.
//    * {ASSERT|EXPECT}_DOUBLE_EQ(expected, actual): Tests that two double values are almost equal.
//    * {ASSERT|EXPECT}_NEAR(v1, v2, abs_error): Tests that v1 and v2 are within the given distance to each other.

// print the bytes.
void srs_bytes_print(char *pa, int size);

class MockEmptyLog : public SrsFileLog
{
public:
    MockEmptyLog(SrsLogLevel l);
    virtual ~MockEmptyLog();
};

// To test the memory corruption, we protect the memory by mprotect.
//          MockProtectedBuffer buffer;
//          if (buffer.alloc(8)) { EXPECT_TRUE(false); return; }
// Crash when write beyond the data:
//          buffer.data_[0] = 0; // OK
//          buffer.data_[7] = 0; // OK
//          buffer.data_[8] = 0; // Crash
// Crash when read beyond the data:
//          char v = buffer.data_[0]; // OK
//          char v = buffer.data_[7]; // OK
//          char v = buffer.data_[8]; // Crash
// @remark The size of memory to allocate, should smaller than page size, generally 4096 bytes.
class MockProtectedBuffer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    char *raw_memory_;

public:
    int size_;
    // Should use this as data.
    char *data_;

public:
    MockProtectedBuffer();
    virtual ~MockProtectedBuffer();
    // Return 0 for success.
    int alloc(int size);
};

// The chan for anonymous coroutine to share variables.
// The chan never free the args, you must manage the memory.
class SrsCoroutineChan
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    std::vector<void *> args_;
    srs_mutex_t lock_;

public:
    // The thread to run the coroutine.
    ISrsCoroutine *trd_;

public:
    SrsCoroutineChan();
    virtual ~SrsCoroutineChan();

public:
    SrsCoroutineChan &push(void *value);
    void *pop();
    SrsCoroutineChan *copy();
};

// A helper to create a anonymous coroutine like goroutine in Go.
// * The context is used to share variables between coroutines.
// * The id is used to identify the coroutine.
// * The code_block is the code to run in the coroutine.
//
// The correct way is to avoid the block, unless you intend to do it,
// so you should create in the same scope, and use id to distinguish them.
// For example:
//        SrsCoroutineChan ctx;
//
//        SRS_COROUTINE_GO_IMPL(&ctx, coroutine1, {
//            srs_usleep(1000 * SRS_UTIME_MILLISECONDS);
//        });
//
//        SRS_COROUTINE_GO_IMPL(&ctx, coroutine2, {
//            srs_usleep(1000 * SRS_UTIME_MILLISECONDS);
//        });
//
//        // It won't wait for the coroutine to terminate.
//        // So you will expect to run to here immediately.
//
// CAUTION: Note that if use a block to run the coroutine, it will
// stop and wait for the coroutine to terminate. So it maybe crash
// for the current thread is interrupted and stopping, such as the
// ctx.pop() will crash for requiring a lock on a stopping thread.
// For example:
//        SrsCoroutineChan ctx;
//
//        // Generally we SHOULD NOT do this, unless you intend to.
//        if (true) {
//            SRS_COROUTINE_GO_IMPL(&ctx, coroutine, {
//                srs_usleep(1000 * SRS_UTIME_MILLISECONDS);
//            });
//        }
//        if (true) {
//            SRS_COROUTINE_GO_IMPL(&ctx, coroutine, {
//                srs_usleep(1000 * SRS_UTIME_MILLISECONDS);
//            });
//        }
//
//        // The coroutine will be stopped and wait for it to terminate.
//        // So maybe it won't execute all your code there.
//
// Warning: Donot use this macro unless you don't need to debug the code block,
// because it's impossible to debug it. Accordingly, you should use it when the
// code block is very simple.
#define SRS_COROUTINE_GO_IMPL(context, id, code_block)                \
    class AnonymousCoroutineHandler##id : public ISrsCoroutineHandler \
    {                                                                 \
    public:                                                           \
        SrsCoroutineChan *ctx_;                                       \
                                                                      \
    public:                                                           \
        AnonymousCoroutineHandler##id() : ctx_(NULL) {}               \
        ~AnonymousCoroutineHandler##id() { srs_freep(ctx_); }         \
        void set_ctx(SrsCoroutineChan *c) { ctx_ = c->copy(); }       \
        virtual srs_error_t cycle()                                   \
        {                                                             \
            SrsCoroutineChan &ctx = *ctx_;                            \
            (void)ctx;                                                \
            code_block;                                               \
            return srs_success;                                       \
        }                                                             \
    };                                                                \
    AnonymousCoroutineHandler##id handler##id;                        \
    SrsSTCoroutine st##id("anonymous", &handler##id);                 \
    (context)->trd_ = &st##id;                                        \
    handler##id.set_ctx(context);                                     \
    srs_error_t err_coroutine##id = st##id.start();                   \
    srs_assert(err_coroutine##id == srs_success)

// A helper to create a anonymous coroutine like goroutine in Go.
// For example:
//        SRS_COROUTINE_GO({
//            srs_usleep(1 * SRS_UTIME_MILLISECONDS);
//        });
#define SRS_COROUTINE_GO(code_block) \
    SrsCoroutineChan context##id;    \
    SRS_COROUTINE_GO_IMPL(&context##id, coroutine0, code_block)

// A helper to create a anonymous coroutine like goroutine in Go.
// With the id, it allows you to create multiple coroutines.
// For example:
//        SRS_COROUTINE_GO2(coroutine1, {
//            srs_usleep(1 * SRS_UTIME_MILLISECONDS);
//        });
//        SRS_COROUTINE_GO2(coroutine2, {
//            srs_usleep(1 * SRS_UTIME_MILLISECONDS);
//        });
#define SRS_COROUTINE_GO2(id, code_block) \
    SrsCoroutineChan context##id;         \
    SRS_COROUTINE_GO_IMPL(&context##id, id, code_block)

// A helper to create a anonymous coroutine like goroutine in Go.
// With the context, it allows you to share variables between coroutines.
// For example:
//        SrsCoroutineChan ctx;
//        ctx.push(1);
//        SRS_COROUTINE_GO_CTX(&ctx, {
//            int v = (int)ctx.pop();
//            srs_usleep(v * SRS_UTIME_MILLISECONDS);
//        });
#define SRS_COROUTINE_GO_CTX(ctx, code_block) \
    SRS_COROUTINE_GO_IMPL(ctx, coroutine0, code_block)

// A helper to create a anonymous coroutine like goroutine in Go.
// With the context and id, it allows you to create multiple coroutines.
// For example:
//        SrsCoroutineChan ctx;
//        ctx.push(1);
//        SRS_COROUTINE_GO_CTX2(&ctx, coroutine1, {
//            int v = (int)ctx.pop();
//            srs_usleep(v * SRS_UTIME_MILLISECONDS);
//        });
//        SRS_COROUTINE_GO_CTX2(&ctx, coroutine2, {
//            int v = (int)ctx.pop();
//            srs_usleep(v * SRS_UTIME_MILLISECONDS);
//        });
#define SRS_COROUTINE_GO_CTX2(ctx, id, code_block) \
    SRS_COROUTINE_GO_IMPL(ctx, id, code_block)

// Simple HTTP test server similar to Go's httptest.NewServer
// This is a simplified version that uses the raw socket approach like MockOnCycleThread4
// but with proper HTTP response formatting
class SrsHttpTestServer : public ISrsCoroutineHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsCoroutine *trd_;
    srs_netfd_t fd_;
    string response_body_;
    string ip_;
    int port_;

public:
    SrsHttpTestServer(string response_body);
    virtual ~SrsHttpTestServer();

public:
    virtual srs_error_t start();
    virtual void close();
    virtual string url();
    virtual int get_port();

    // Interface ISrsCoroutineHandler
public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_cycle(srs_netfd_t cfd);
};

// Simple HTTPS test server similar to Go's httptest.NewServer but with SSL support
class SrsHttpsTestServer : public ISrsCoroutineHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsCoroutine *trd_;
    srs_netfd_t fd_;
    string response_body_;
    string ssl_key_file_;
    string ssl_cert_file_;
    string ip_;
    int port_;

public:
    SrsHttpsTestServer(string response_body, string key_file = "./conf/server.key", string cert_file = "./conf/server.crt");
    virtual ~SrsHttpsTestServer();
    virtual srs_error_t start();
    virtual void close();
    virtual string url();
    virtual int get_port();

    // Interface ISrsCoroutineHandler
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_error_t handle_client(srs_netfd_t client_fd);
};

// Simple RTMP test server similar to SrsHttpTestServer but for RTMP protocol
// This server handles basic RTMP handshake and connect app operations
class SrsRtmpTestServer : public ISrsCoroutineHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsCoroutine *trd_;
    srs_netfd_t fd_;
    string app_;
    string stream_;
    string ip_;
    int port_;
    bool enable_publish_;
    bool enable_play_;

public:
    SrsRtmpTestServer(string app = "live", string stream = "test");
    virtual ~SrsRtmpTestServer();

public:
    virtual srs_error_t start();
    virtual void close();
    virtual string url();
    virtual int get_port();
    virtual void enable_publish(bool v = true);
    virtual void enable_play(bool v = true);

    // Interface ISrsCoroutineHandler
public:
    virtual srs_error_t cycle();

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t do_cycle(srs_netfd_t cfd);
    virtual srs_error_t handle_rtmp_client(srs_netfd_t cfd);
};

// Test TCP server for testing SrsTcpConnection
class SrsTestTcpServer : public ISrsCoroutineHandler, public ISrsTcpHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsCoroutine *trd_;
    SrsTcpListener *listener_;
    string ip_;
    int port_;
    SrsTcpConnection *conn_;

public:
    SrsTestTcpServer(string ip = "127.0.0.1");
    virtual ~SrsTestTcpServer();

public:
    virtual srs_error_t start();
    virtual void close();
    virtual int get_port();
    virtual SrsTcpConnection *get_connection();

    // Interface ISrsCoroutineHandler
public:
    virtual srs_error_t cycle();

    // Interface ISrsTcpHandler
public:
    virtual srs_error_t on_tcp_client(ISrsListener *listener, srs_netfd_t stfd);
};

// Test TCP client for testing SrsTcpConnection
class SrsTestTcpClient
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    SrsTcpClient *client_;
    SrsTcpConnection *conn_;
    string host_;
    int port_;
    srs_utime_t timeout_;

public:
    SrsTestTcpClient(string host, int port, srs_utime_t timeout = 1 * SRS_UTIME_SECONDS);
    virtual ~SrsTestTcpClient();

public:
    virtual srs_error_t connect();
    virtual void close();
    virtual SrsTcpConnection *get_connection();
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite);
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread);
};

// Test UDP server for testing UDP socket communication
class SrsUdpTestServer : public ISrsCoroutineHandler
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_netfd_t lfd_;
    ISrsCoroutine *trd_;
    SrsStSocket *socket_;
    string host_;
    int port_;
    bool started_;

public:
    SrsUdpTestServer(string host);
    virtual ~SrsUdpTestServer();

public:
    virtual srs_error_t start();
    virtual void stop();
    virtual int get_port();
    virtual SrsStSocket *get_socket();

public:
    virtual srs_error_t cycle();
};

// Test UDP client for testing UDP socket communication
class SrsUdpTestClient
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_netfd_t stfd_;
    SrsStSocket *socket_;
    string host_;
    int port_;
    srs_utime_t timeout_;
    sockaddr_storage server_addr_;
    int server_addrlen_;

public:
    SrsUdpTestClient(string host, int port, srs_utime_t timeout = 1 * SRS_UTIME_SECONDS);
    virtual ~SrsUdpTestClient();

public:
    virtual srs_error_t connect();
    virtual void close();
    virtual SrsStSocket *get_socket();
    virtual srs_error_t sendto(void *buf, size_t size, ssize_t *nwrite);
    virtual srs_error_t recvfrom(void *buf, size_t size, ssize_t *nread);
};

#endif
