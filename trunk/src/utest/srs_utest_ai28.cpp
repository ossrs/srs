//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai28.hpp>

#include <srs_app_srt_server.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>

#include <sstream>
using namespace std;

// The acceptor passes this address and port to the listener it creates. The listener is a mock, so nothing binds it;
// the address is from the TEST-NET-1 documentation range.
static const char *kAcceptorIp = "192.0.2.1";
static const int kAcceptorPort = 10080;
static const srs_srt_t kListenerFd = 7;

template <typename T>
static string to_str(T v)
{
    stringstream ss;
    ss << v;
    return ss.str();
}

MockAppConfigForSrtAcceptor::MockAppConfigForSrtAcceptor()
{
    maxbw_ = 1000000;
    mss_ = 1400;
    tsbpdmode_ = false;
    latency_ = 200;
    recv_latency_ = 300;
    peer_latency_ = 400;
    tlpktdrop_ = false;
    conntimeout_ = 5 * SRS_UTIME_SECONDS;
    peeridletimeout_ = 7 * SRS_UTIME_SECONDS;
    sendbuf_ = 100000;
    recvbuf_ = 200000;
    payloadsize_ = 1456;
    pbkeylen_ = 0;
}

MockAppConfigForSrtAcceptor::~MockAppConfigForSrtAcceptor()
{
}

MockSrtListenerForSrtAcceptor::MockSrtListenerForSrtAcceptor(vector<string> *calls)
{
    calls_ = calls;
    fd_ = kListenerFd;
    create_socket_error_ = srs_success;
    listen_error_ = srs_success;
}

MockSrtListenerForSrtAcceptor::~MockSrtListenerForSrtAcceptor()
{
    srs_freep(create_socket_error_);
    srs_freep(listen_error_);
}

srs_srt_t MockSrtListenerForSrtAcceptor::fd()
{
    return fd_;
}

srs_error_t MockSrtListenerForSrtAcceptor::create_socket()
{
    calls_->push_back("create_socket");
    return srs_error_copy(create_socket_error_);
}

srs_error_t MockSrtListenerForSrtAcceptor::listen()
{
    calls_->push_back("listen");
    return srs_error_copy(listen_error_);
}

MockSrtOptionsForSrtAcceptor::MockSrtOptionsForSrtAcceptor(vector<string> *calls)
{
    calls_ = calls;
}

MockSrtOptionsForSrtAcceptor::~MockSrtOptionsForSrtAcceptor()
{
}

srs_error_t MockSrtOptionsForSrtAcceptor::record(srs_srt_t srt_fd, string name, string value)
{
    calls_->push_back(name + "=" + value);
    fds_.push_back(srt_fd);

    if (name == fail_option_) {
        return srs_error_new(ERROR_SOCKET_LISTEN, "mock set %s", name.c_str());
    }
    return srs_success;
}

srs_error_t MockSrtOptionsForSrtAcceptor::set_maxbw(srs_srt_t srt_fd, int64_t maxbw)
{
    return record(srt_fd, "maxbw", to_str(maxbw));
}

srs_error_t MockSrtOptionsForSrtAcceptor::set_mss(srs_srt_t srt_fd, int mss)
{
    return record(srt_fd, "mss", to_str(mss));
}

srs_error_t MockSrtOptionsForSrtAcceptor::set_payload_size(srs_srt_t srt_fd, int payload_size)
{
    return record(srt_fd, "payload_size", to_str(payload_size));
}

srs_error_t MockSrtOptionsForSrtAcceptor::set_connect_timeout(srs_srt_t srt_fd, int timeout)
{
    return record(srt_fd, "connect_timeout", to_str(timeout));
}

srs_error_t MockSrtOptionsForSrtAcceptor::set_peer_idle_timeout(srs_srt_t srt_fd, int timeout)
{
    return record(srt_fd, "peer_idle_timeout", to_str(timeout));
}

srs_error_t MockSrtOptionsForSrtAcceptor::set_tsbpdmode(srs_srt_t srt_fd, bool tsbpdmode)
{
    return record(srt_fd, "tsbpdmode", to_str(tsbpdmode));
}

srs_error_t MockSrtOptionsForSrtAcceptor::set_sndbuf(srs_srt_t srt_fd, int sndbuf)
{
    return record(srt_fd, "sndbuf", to_str(sndbuf));
}

srs_error_t MockSrtOptionsForSrtAcceptor::set_rcvbuf(srs_srt_t srt_fd, int rcvbuf)
{
    return record(srt_fd, "rcvbuf", to_str(rcvbuf));
}

srs_error_t MockSrtOptionsForSrtAcceptor::set_tlpktdrop(srs_srt_t srt_fd, bool tlpktdrop)
{
    return record(srt_fd, "tlpktdrop", to_str(tlpktdrop));
}

srs_error_t MockSrtOptionsForSrtAcceptor::set_latency(srs_srt_t srt_fd, int latency)
{
    return record(srt_fd, "latency", to_str(latency));
}

srs_error_t MockSrtOptionsForSrtAcceptor::set_rcv_latency(srs_srt_t srt_fd, int rcv_latency)
{
    return record(srt_fd, "rcv_latency", to_str(rcv_latency));
}

srs_error_t MockSrtOptionsForSrtAcceptor::set_peer_latency(srs_srt_t srt_fd, int peer_latency)
{
    return record(srt_fd, "peer_latency", to_str(peer_latency));
}

srs_error_t MockSrtOptionsForSrtAcceptor::set_passphrase(srs_srt_t srt_fd, const string &passphrase)
{
    return record(srt_fd, "passphrase", passphrase);
}

srs_error_t MockSrtOptionsForSrtAcceptor::set_pbkeylen(srs_srt_t srt_fd, int pbkeylen)
{
    return record(srt_fd, "pbkeylen", to_str(pbkeylen));
}

MockAppFactoryForSrtAcceptor::MockAppFactoryForSrtAcceptor(MockSrtListenerForSrtAcceptor *listener)
{
    listener_ = listener;
    listener_created_ = false;
    create_srt_listener_count_ = 0;
    handler_ = NULL;
    port_ = 0;
}

MockAppFactoryForSrtAcceptor::~MockAppFactoryForSrtAcceptor()
{
    if (!listener_created_) {
        srs_freep(listener_);
    }
}

ISrsSrtListener *MockAppFactoryForSrtAcceptor::create_srt_listener(ISrsSrtHandler *handler, string ip, int port)
{
    create_srt_listener_count_++;
    handler_ = handler;
    ip_ = ip;
    port_ = port;

    listener_created_ = true;
    return listener_;
}

// The options the acceptor must set, in order, for the given config, as the options mock records them.
static vector<string> expected_options(MockAppConfigForSrtAcceptor *config)
{
    vector<string> opts;
    opts.push_back("maxbw=" + to_str(config->maxbw_));
    opts.push_back("mss=" + to_str(config->mss_));
    opts.push_back("tsbpdmode=" + to_str(config->tsbpdmode_));
    opts.push_back("latency=" + to_str(config->latency_));
    opts.push_back("rcv_latency=" + to_str(config->recv_latency_));
    opts.push_back("peer_latency=" + to_str(config->peer_latency_));
    opts.push_back("tlpktdrop=" + to_str(config->tlpktdrop_));
    opts.push_back("connect_timeout=" + to_str(srsu2msi(config->conntimeout_)));
    opts.push_back("peer_idle_timeout=" + to_str(srsu2msi(config->peeridletimeout_)));
    opts.push_back("sndbuf=" + to_str(config->sendbuf_));
    opts.push_back("rcvbuf=" + to_str(config->recvbuf_));
    opts.push_back("payload_size=" + to_str(config->payloadsize_));
    if (!config->passphrase_.empty()) {
        opts.push_back("passphrase=" + config->passphrase_);
        opts.push_back("pbkeylen=" + to_str(config->pbkeylen_));
    }
    return opts;
}

// Replace every collaborator of the acceptor with a mock. The options object is owned by the acceptor, so free it
// before the replacement; release_mocks() must run before the acceptor is destroyed.
static void inject_mocks(SrsSrtAcceptor *acceptor, ISrsAppConfig *config, ISrsAppFactory *factory, ISrsSrtOptions *options)
{
    acceptor->config_ = config;
    acceptor->app_factory_ = factory;
    srs_freep(acceptor->srt_options_);
    acceptor->srt_options_ = options;
}

static void release_mocks(SrsSrtAcceptor *acceptor)
{
    acceptor->config_ = NULL;
    acceptor->app_factory_ = NULL;
    acceptor->srt_options_ = NULL;
}

// The acceptor creates its listener through the factory, with itself as the handler and the given address.
VOID TEST(SrtAcceptorTest, ListenCreatesListenerThroughFactory)
{
    vector<string> calls;
    MockAppConfigForSrtAcceptor config;
    MockSrtOptionsForSrtAcceptor options(&calls);
    MockAppFactoryForSrtAcceptor factory(new MockSrtListenerForSrtAcceptor(&calls));

    SrsSrtAcceptor acceptor(NULL);
    inject_mocks(&acceptor, &config, &factory, &options);

    srs_error_t err = acceptor.listen(kAcceptorIp, kAcceptorPort);
    EXPECT_TRUE(err == srs_success) << srs_error_summary(err);
    srs_freep(err);

    EXPECT_EQ(1, factory.create_srt_listener_count_);
    EXPECT_TRUE(factory.handler_ == &acceptor);
    EXPECT_STREQ(kAcceptorIp, factory.ip_.c_str());
    EXPECT_EQ(kAcceptorPort, factory.port_);

    release_mocks(&acceptor);
}

// Every option is set from config on the listener's fd, after the socket is created and before it listens. Without a
// passphrase, neither the passphrase nor the key length is set.
VOID TEST(SrtAcceptorTest, ListenSetsEveryOptionBetweenCreateAndListen)
{
    vector<string> calls;
    MockAppConfigForSrtAcceptor config;
    MockSrtOptionsForSrtAcceptor options(&calls);
    MockAppFactoryForSrtAcceptor factory(new MockSrtListenerForSrtAcceptor(&calls));

    SrsSrtAcceptor acceptor(NULL);
    inject_mocks(&acceptor, &config, &factory, &options);

    srs_error_t err = acceptor.listen(kAcceptorIp, kAcceptorPort);
    EXPECT_TRUE(err == srs_success) << srs_error_summary(err);
    srs_freep(err);

    vector<string> expected;
    expected.push_back("create_socket");
    vector<string> opts = expected_options(&config);
    expected.insert(expected.end(), opts.begin(), opts.end());
    expected.push_back("listen");
    EXPECT_EQ(expected, calls);

    EXPECT_EQ(opts.size(), options.fds_.size());
    for (int i = 0; i < (int)options.fds_.size(); i++) {
        EXPECT_EQ(kListenerFd, options.fds_[i]) << "option " << i;
    }

    release_mocks(&acceptor);
}

// A configured passphrase is set, followed by the key length, as the last options before listening.
VOID TEST(SrtAcceptorTest, ListenSetsPassphraseAndKeyLengthWhenConfigured)
{
    vector<string> calls;
    MockAppConfigForSrtAcceptor config;
    config.passphrase_ = "0123456789abcdef";
    config.pbkeylen_ = 16;
    MockSrtOptionsForSrtAcceptor options(&calls);
    MockAppFactoryForSrtAcceptor factory(new MockSrtListenerForSrtAcceptor(&calls));

    SrsSrtAcceptor acceptor(NULL);
    inject_mocks(&acceptor, &config, &factory, &options);

    srs_error_t err = acceptor.listen(kAcceptorIp, kAcceptorPort);
    EXPECT_TRUE(err == srs_success) << srs_error_summary(err);
    srs_freep(err);

    ASSERT_EQ(16, (int)calls.size());
    EXPECT_STREQ("passphrase=0123456789abcdef", calls[13].c_str());
    EXPECT_STREQ("pbkeylen=16", calls[14].c_str());
    EXPECT_STREQ("listen", calls[15].c_str());

    release_mocks(&acceptor);
}

// When the socket cannot be created, the error is returned and no option is set and nothing listens.
VOID TEST(SrtAcceptorTest, ListenFailsWhenCreateSocketFails)
{
    vector<string> calls;
    MockAppConfigForSrtAcceptor config;
    MockSrtOptionsForSrtAcceptor options(&calls);
    MockSrtListenerForSrtAcceptor *listener = new MockSrtListenerForSrtAcceptor(&calls);
    listener->create_socket_error_ = srs_error_new(ERROR_SOCKET_CREATE, "mock create");
    MockAppFactoryForSrtAcceptor factory(listener);

    SrsSrtAcceptor acceptor(NULL);
    inject_mocks(&acceptor, &config, &factory, &options);

    srs_error_t err = acceptor.listen(kAcceptorIp, kAcceptorPort);
    EXPECT_EQ(ERROR_SOCKET_CREATE, srs_error_code(err));
    srs_freep(err);

    vector<string> expected;
    expected.push_back("create_socket");
    EXPECT_EQ(expected, calls);

    release_mocks(&acceptor);
}

// Each option can fail. The acceptor returns that option's error and stops: no later option is set and nothing listens.
VOID TEST(SrtAcceptorTest, ListenStopsAtTheFirstFailedOption)
{
    MockAppConfigForSrtAcceptor config;
    config.passphrase_ = "0123456789abcdef";
    config.pbkeylen_ = 16;
    vector<string> opts = expected_options(&config);
    ASSERT_EQ(14, (int)opts.size());

    for (int i = 0; i < (int)opts.size(); i++) {
        string name = opts[i].substr(0, opts[i].find('='));

        vector<string> calls;
        MockSrtOptionsForSrtAcceptor options(&calls);
        options.fail_option_ = name;
        MockAppFactoryForSrtAcceptor factory(new MockSrtListenerForSrtAcceptor(&calls));

        SrsSrtAcceptor acceptor(NULL);
        inject_mocks(&acceptor, &config, &factory, &options);

        srs_error_t err = acceptor.listen(kAcceptorIp, kAcceptorPort);
        EXPECT_EQ(ERROR_SOCKET_LISTEN, srs_error_code(err)) << "fail at " << name;
        srs_freep(err);

        vector<string> expected;
        expected.push_back("create_socket");
        expected.insert(expected.end(), opts.begin(), opts.begin() + i + 1);
        EXPECT_EQ(expected, calls) << "fail at " << name;

        release_mocks(&acceptor);
    }
}

// When the listener cannot listen, the error is returned after every option was set.
VOID TEST(SrtAcceptorTest, ListenFailsWhenListenerListenFails)
{
    vector<string> calls;
    MockAppConfigForSrtAcceptor config;
    MockSrtOptionsForSrtAcceptor options(&calls);
    MockSrtListenerForSrtAcceptor *listener = new MockSrtListenerForSrtAcceptor(&calls);
    listener->listen_error_ = srs_error_new(ERROR_SOCKET_BIND, "mock listen");
    MockAppFactoryForSrtAcceptor factory(listener);

    SrsSrtAcceptor acceptor(NULL);
    inject_mocks(&acceptor, &config, &factory, &options);

    srs_error_t err = acceptor.listen(kAcceptorIp, kAcceptorPort);
    EXPECT_EQ(ERROR_SOCKET_BIND, srs_error_code(err));
    srs_freep(err);

    vector<string> expected;
    expected.push_back("create_socket");
    vector<string> opts = expected_options(&config);
    expected.insert(expected.end(), opts.begin(), opts.end());
    expected.push_back("listen");
    EXPECT_EQ(expected, calls);

    release_mocks(&acceptor);
}
