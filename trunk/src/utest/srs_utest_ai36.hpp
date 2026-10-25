//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI36_HPP
#define SRS_UTEST_AI36_HPP

/*
#include <srs_utest_ai36.hpp>
*/
#include <srs_utest.hpp>

#include <srs_utest_ai15.hpp>

// Mock config for testing how SrsServer takes a client whose peer address is unknown.
class MockAppConfigForEmptyIp : public MockAppConfig
{
public:
    bool empty_ip_ok_;

public:
    MockAppConfigForEmptyIp();
    virtual ~MockAppConfigForEmptyIp();

public:
    virtual bool empty_ip_ok();
};

// Mock connection manager for testing SrsServer::listen() and gracefully_dispose().
class MockConnectionManagerForServerQuit : public MockConnectionManagerForConnectionLimit
{
public:
    // How many more calls of empty() answer that connections remain.
    int nn_not_empty_;
    int empty_count_;
    // The error start() returns, owned by the caller once returned.
    srs_error_t start_error_;

public:
    MockConnectionManagerForServerQuit();
    virtual ~MockConnectionManagerForServerQuit();

public:
    virtual srs_error_t start();
    virtual bool empty();
};

// Mock live source for testing how SrsRtmpConn::process_publish_message() feeds the source.
class MockLiveSourceForPublishMessage : public MockLiveSource
{
public:
    int on_aggregate_count_;
    int on_meta_data_count_;
    int on_edge_proxy_publish_count_;

public:
    MockLiveSourceForPublishMessage();
    virtual ~MockLiveSourceForPublishMessage();

public:
    virtual srs_error_t on_aggregate(SrsRtmpCommonMessage *msg);
    virtual srs_error_t on_meta_data(SrsRtmpCommonMessage *msg, SrsOnMetaDataPacket *metadata);
    virtual srs_error_t on_edge_proxy_publish(SrsRtmpCommonMessage *msg);
};

// Mock config for testing the origin cluster redirect and the socket options of SrsRtmpConn.
class MockAppConfigForRtmpConnCluster : public MockAppConfigForRtmpConn
{
public:
    std::vector<std::string> coworkers_;
    bool tcp_nodelay_;

public:
    MockAppConfigForRtmpConnCluster();
    virtual ~MockAppConfigForRtmpConnCluster();

public:
    virtual std::vector<std::string> get_vhost_coworkers(std::string vhost);
    virtual bool get_tcp_nodelay(std::string vhost);
};

// Mock hooks whose discovery of each coworker answers in turn with a host and port, or an error.
class MockHttpHooksForCoworkers : public MockHttpHooks
{
public:
    std::vector<std::string> hosts_;
    std::vector<int> ports_;
    // The error of each discovery, owned by the mock.
    std::vector<srs_error_t> errors_;
    std::vector<std::string> urls_;

public:
    MockHttpHooksForCoworkers();
    virtual ~MockHttpHooksForCoworkers();

public:
    virtual srs_error_t discover_co_workers(std::string url, std::string &host, int &port);
};

// Mock RTMP server that records the redirects and may refuse them.
class MockRtmpServerForRedirect : public MockRtmpServer
{
public:
    std::vector<std::string> redirect_urls_;
    // The error of each redirect, owned by the mock.
    srs_error_t redirect_error_;

public:
    MockRtmpServerForRedirect();
    virtual ~MockRtmpServerForRedirect();

public:
    virtual srs_error_t redirect(ISrsRequest *r, std::string url, bool &accepted);
};

// Mock RTMP transport whose handshake fails as asked, and which records the TCP_NODELAY it is given.
class MockRtmpTransportForCycle : public MockRtmpTransportForDoCycle
{
public:
    // The error of each handshake, owned by the mock.
    srs_error_t handshake_error_;
    int set_tcp_nodelay_count_;
    bool tcp_nodelay_;
    srs_error_t set_tcp_nodelay_error_;

public:
    MockRtmpTransportForCycle();
    virtual ~MockRtmpTransportForCycle();

public:
    virtual srs_error_t handshake();
    virtual srs_error_t set_tcp_nodelay(bool v);
};

#endif
