//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI38_HPP
#define SRS_UTEST_AI38_HPP

/*
#include <srs_utest_ai38.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_edge.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_process.hpp>
#include <srs_protocol_http_client.hpp>
#include <srs_protocol_http_stack.hpp>
#include <srs_utest_ai18.hpp>
#include <srs_utest_ai19.hpp>
#include <srs_utest_manual_mock.hpp>

#include <string>
#include <vector>

class MockAppFactoryForEdgeIngester;

// Mock ISrsAppConfig for the edge: the protocol to pull from the origin, and the origins.
class MockAppConfigForEdgeIngester : public MockAppConfig
{
public:
    std::string edge_protocol_;
    bool follow_client_;
    SrsConfDirective *origin_;

public:
    MockAppConfigForEdgeIngester();
    virtual ~MockAppConfigForEdgeIngester();

public:
    virtual std::string get_vhost_edge_protocol(std::string vhost);
    virtual bool get_vhost_edge_follow_client(std::string vhost);
    virtual SrsConfDirective *get_vhost_edge_origin(std::string vhost);
};

// Mock ISrsPlayEdge for SrsEdgeIngester::do_cycle, which tells the edge once the upstream connects.
class MockPlayEdgeForIngesterCycle : public ISrsPlayEdge
{
public:
    int on_ingest_play_count_;
    srs_error_t on_ingest_play_error_;

public:
    MockPlayEdgeForIngesterCycle();
    virtual ~MockPlayEdgeForIngesterCycle();

public:
    virtual srs_error_t on_ingest_play();
};

// Mock ISrsEdgeUpstream for SrsEdgeIngester::do_cycle and ingest: recv_message() hands out msgs_ in
// order, then fails with recv_error_; decode_message() hands out packets_ in order. The ingester owns
// the upstream, so the upstream records its calls in the factory that made it.
class MockEdgeUpstreamForIngesterCycle : public ISrsEdgeUpstream
{
public:
    MockAppFactoryForEdgeIngester *factory_;
    srs_error_t connect_error_;
    srs_error_t recv_error_;
    std::vector<SrsRtmpCommonMessage *> msgs_;
    std::vector<SrsRtmpCommand *> packets_;

public:
    MockEdgeUpstreamForIngesterCycle();
    virtual ~MockEdgeUpstreamForIngesterCycle();

public:
    virtual srs_error_t connect(ISrsRequest *r, ISrsLbRoundRobin *lb);
    virtual srs_error_t recv_message(SrsRtmpCommonMessage **pmsg);
    virtual srs_error_t decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket);
    virtual void close();
    virtual void selected(std::string &server, int &port);
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual void kbps_sample(const char *label, srs_utime_t age);
};

// Mock ISrsHttpMessage for an HTTP-FLV origin that answers with a status and a Location header.
class MockHttpMessageForEdgeRedirect : public MockHttpMessageForHeartbeat
{
public:
    SrsHttpHeader header_;

public:
    MockHttpMessageForEdgeRedirect(int status, std::string location);
    virtual ~MockHttpMessageForEdgeRedirect();

public:
    virtual SrsHttpHeader *header();
};

// Mock ISrsHttpClient for an HTTP-FLV origin that redirects every request to location_. The upstream
// owns the client, so the client records its calls in the factory that made it.
class MockHttpClientForEdgeRedirect : public ISrsHttpClient
{
public:
    MockAppFactoryForEdgeIngester *factory_;

public:
    MockHttpClientForEdgeRedirect(MockAppFactoryForEdgeIngester *factory);
    virtual ~MockHttpClientForEdgeRedirect();

public:
    virtual srs_error_t initialize(std::string schema, std::string h, int p, srs_utime_t tm = SRS_HTTP_CLIENT_TIMEOUT);
    virtual srs_error_t get(std::string path, std::string req, ISrsHttpMessage **ppmsg);
    virtual srs_error_t post(std::string path, std::string req, ISrsHttpMessage **ppmsg);
    virtual void set_recv_timeout(srs_utime_t tm);
    virtual void kbps_sample(const char *label, srs_utime_t age);
};

// Mock ISrsAppFactory for SrsEdgeIngester and SrsEdgeFlvUpstream: hands out the next upstream of
// upstreams_, or a new one when none is left, and the coroutine_, and records every object made.
class MockAppFactoryForEdgeIngester : public SrsAppFactory
{
public:
    // Handed out in order; the ingester owns each one it gets.
    std::vector<MockEdgeUpstreamForIngesterCycle *> upstreams_;
    std::vector<std::string> flv_schemas_;
    std::vector<std::string> rtmp_redirects_;
    // The coroutine for the next create_coroutine(), owned by the caller once handed out.
    ISrsCoroutine *coroutine_;
    std::string coroutine_name_;
    SrsContextId coroutine_cid_;
    int create_coroutine_count_;
    // What the upstreams were asked.
    int connect_count_;
    int close_count_;
    srs_utime_t recv_timeout_;
    // What the HTTP clients were asked, and where the origin redirects to.
    std::string location_;
    std::vector<std::string> http_schemas_;
    std::vector<std::string> http_hosts_;
    std::vector<int> http_ports_;
    std::vector<std::string> http_paths_;

public:
    MockAppFactoryForEdgeIngester();
    virtual ~MockAppFactoryForEdgeIngester();

public:
    virtual ISrsCoroutine *create_coroutine(const std::string &name, ISrsCoroutineHandler *handler, SrsContextId cid);
    virtual ISrsEdgeUpstream *create_edge_flv_upstream(std::string schema);
    virtual ISrsEdgeUpstream *create_edge_rtmp_upstream(std::string redirect);
    virtual ISrsHttpClient *create_http_client();
    MockEdgeUpstreamForIngesterCycle *next_upstream();
};

// Mock ISrsProcess for SrsFFMPEG: records the binary and argv it is started with, and never forks.
class MockProcessForFFMPEG : public ISrsProcess
{
public:
    bool started_;
    std::string binary_;
    std::vector<std::string> argv_;
    int initialize_count_;
    int start_count_;
    int cycle_count_;
    int stop_count_;
    int fast_stop_count_;
    int fast_kill_count_;
    // The errors the calls return a copy of, owned by the mock.
    srs_error_t initialize_error_;
    srs_error_t start_error_;
    srs_error_t cycle_error_;

public:
    MockProcessForFFMPEG();
    virtual ~MockProcessForFFMPEG();

public:
    virtual int get_pid();
    virtual bool started();
    virtual srs_error_t initialize(std::string binary, std::vector<std::string> argv);
    virtual srs_error_t start();
    virtual srs_error_t cycle();
    virtual void stop();
    virtual void fast_stop();
    virtual void fast_kill();
};

// Mock ISrsAppConfig for SrsIngester: the output of an ingest without engines, and the FFmpeg log.
class MockAppConfigForIngesterLog : public MockAppConfigForIngester
{
public:
    std::string default_output_;
    bool ff_log_enabled_;
    std::string ff_log_dir_;

public:
    MockAppConfigForIngesterLog();
    virtual ~MockAppConfigForIngesterLog();

public:
    virtual std::string get_engine_output(SrsConfDirective *conf);
    virtual bool get_ff_log_enabled();
    virtual std::string get_ff_log_dir();
};

// Mock ISrsAppFactory for SrsIngester, whose FFmpegs record how they are set up.
class MockAppFactoryForIngesterSetup : public MockAppFactoryForIngester
{
public:
    // Owned by the ingesters they are handed to.
    std::vector<MockFFMPEGForEncoder *> ffmpegs_;
    std::vector<std::string> ffmpeg_bins_;

public:
    MockAppFactoryForIngesterSetup();
    virtual ~MockAppFactoryForIngesterSetup();

public:
    virtual ISrsFFMPEG *create_ffmpeg(std::string ffmpeg_bin);
};

// Mock ISrsEdgeForwarder for the publish edge of SrsLiveSource.
class MockEdgeForwarderForLiveSource : public ISrsEdgeForwarder
{
public:
    int start_count_;
    int stop_count_;
    int proxy_count_;
    srs_error_t start_error_;

public:
    MockEdgeForwarderForLiveSource();
    virtual ~MockEdgeForwarderForLiveSource();

public:
    virtual void set_queue_size(srs_utime_t queue_size);
    virtual srs_error_t initialize(SrsSharedPtr<SrsLiveSource> s, ISrsPublishEdge *e, ISrsRequest *r);
    virtual srs_error_t start();
    virtual void stop();
    virtual srs_error_t proxy(SrsRtmpCommonMessage *msg);
};

#endif
