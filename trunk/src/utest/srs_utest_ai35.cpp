//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai35.hpp>

#include <srs_app_config.hpp>
#include <srs_app_srt_conn.hpp>
#include <srs_app_statistic.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_utest_ai18.hpp>
#include <srs_utest_manual_coworkers.hpp>
#include <srs_utest_manual_mock.hpp>
#include <srs_utest_manual_service.hpp>

#include <srt/srt.h>

#include <string.h>

using namespace std;

// The libsrt stats inside SrsSrtStat, for the test to set.
static SRT_TRACEBSTATS *srt_stats_of(SrsSrtStat &stat)
{
    return (SRT_TRACEBSTATS *)stat.stat_;
}

MockStatisticSrtForClient::MockStatisticSrtForClient()
{
    dumps_count_ = 0;
    rtt_ms_ = 0;
    dumps_error_ = srs_success;
}

MockStatisticSrtForClient::~MockStatisticSrtForClient()
{
    srs_freep(dumps_error_);
}

srs_error_t MockStatisticSrtForClient::dumps_srt(SrsJsonObject *obj)
{
    dumps_count_++;

    if (dumps_error_ != srs_success) {
        return srs_error_copy(dumps_error_);
    }

    obj->set("rtt_ms", SrsJsonAny::number(rtt_ms_));
    return srs_success;
}

MockSrtConnectionForStats::MockSrtConnectionForStats()
{
    clear_count_ = 0;
    peek_count_ = 0;
}

MockSrtConnectionForStats::~MockSrtConnectionForStats()
{
}

srs_error_t MockSrtConnectionForStats::get_stats(SrsSrtStat &stat, bool clear)
{
    *srt_stats_of(stat) = *srt_stats_of(stats_);

    if (!clear) {
        peek_count_++;
        return srs_success;
    }

    // Like libsrt, clear resets the interval counters only.
    clear_count_++;
    SRT_TRACEBSTATS *p = srt_stats_of(stats_);
    p->pktRecv = 0;
    p->pktRcvLoss = 0;
    p->pktRcvRetrans = 0;
    p->pktRcvDrop = 0;
    p->pktSent = 0;
    p->pktSndLoss = 0;
    p->pktRetrans = 0;
    p->pktSndDrop = 0;

    return srs_success;
}

// Find the client of id in the dumped clients, or NULL.
static SrsJsonObject *find_dumped_client(SrsJsonArray *clients, string id)
{
    for (int i = 0; i < clients->count(); i++) {
        SrsJsonObject *client = clients->at(i)->to_object();
        SrsJsonAny *prop = client->get_property("id");
        if (prop && prop->to_str() == id) {
            return client;
        }
    }
    return NULL;
}

// A client of another protocol, such as RTMP, has no SRT stats, so its srt is null, like the video of a
// stream without video.
VOID TEST(StatisticSrtTest, NonSrtClientDumpsNullSrt)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsStatistic> stat(new SrsStatistic());
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "stream1"));
    MockExpire conn;
    HELPER_EXPECT_SUCCESS(stat->on_client("rtmp-1", req.get(), &conn, SrsRtmpConnFMLEPublish));

    SrsUniquePtr<SrsJsonArray> clients(SrsJsonAny::array());
    HELPER_EXPECT_SUCCESS(stat->dumps_clients(clients.get(), 0, 10));

    SrsJsonObject *client = find_dumped_client(clients.get(), "rtmp-1");
    ASSERT_TRUE(client != NULL);
    SrsJsonAny *srt = client->get_property("srt");
    ASSERT_TRUE(srt != NULL);
    EXPECT_TRUE(srt->is_null());
}

// An SRT client dumps the SRT stats of its connection, both in the list and by id.
VOID TEST(StatisticSrtTest, SrtClientDumpsSrtObject)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsStatistic> stat(new SrsStatistic());
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "stream1"));
    MockExpire conn;
    HELPER_EXPECT_SUCCESS(stat->on_client("srt-1", req.get(), &conn, SrsSrtConnPublish));

    MockStatisticSrtForClient srt;
    srt.rtt_ms_ = 23.5;
    stat->on_srt_stats("srt-1", &srt);

    SrsUniquePtr<SrsJsonArray> clients(SrsJsonAny::array());
    HELPER_EXPECT_SUCCESS(stat->dumps_clients(clients.get(), 0, 10));

    SrsJsonObject *obj = find_dumped_client(clients.get(), "srt-1");
    ASSERT_TRUE(obj != NULL);
    SrsJsonAny *prop = obj->get_property("srt");
    ASSERT_TRUE(prop != NULL);
    ASSERT_TRUE(prop->is_object());
    SrsJsonAny *rtt = prop->to_object()->get_property("rtt_ms");
    ASSERT_TRUE(rtt != NULL);
    EXPECT_DOUBLE_EQ(23.5, rtt->to_number());

    // The API by id dumps the same client.
    SrsStatisticClient *client = stat->find_client("srt-1");
    ASSERT_TRUE(client != NULL);
    SrsUniquePtr<SrsJsonObject> one(SrsJsonAny::object());
    HELPER_EXPECT_SUCCESS(client->dumps(one.get()));
    prop = one->get_property("srt");
    ASSERT_TRUE(prop != NULL);
    EXPECT_TRUE(prop->is_object());
    EXPECT_EQ(2, srt.dumps_count_);
}

// The SRT stats are extra: when they fail, for example the socket is closing, the client is still listed,
// with a null srt, and the API does not fail.
VOID TEST(StatisticSrtTest, SrtStatsErrorDumpsNullSrt)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsStatistic> stat(new SrsStatistic());
    SrsUniquePtr<MockSrsRequest> req(new MockSrsRequest("test.vhost", "live", "stream1"));
    MockExpire conn;
    HELPER_EXPECT_SUCCESS(stat->on_client("srt-1", req.get(), &conn, SrsSrtConnPublish));

    MockStatisticSrtForClient srt;
    srt.dumps_error_ = srs_error_new(ERROR_SRT_STATS, "mock srt_bstats failed");
    stat->on_srt_stats("srt-1", &srt);

    SrsUniquePtr<SrsJsonArray> clients(SrsJsonAny::array());
    HELPER_EXPECT_SUCCESS(stat->dumps_clients(clients.get(), 0, 10));

    SrsJsonObject *obj = find_dumped_client(clients.get(), "srt-1");
    ASSERT_TRUE(obj != NULL);
    SrsJsonAny *prop = obj->get_property("srt");
    ASSERT_TRUE(prop != NULL);
    EXPECT_TRUE(prop->is_null());
    EXPECT_EQ(1, srt.dumps_count_);
}

// The SRT stats are for a client already on, so an unknown client is ignored, and creates no client.
VOID TEST(StatisticSrtTest, OnSrtStatsIgnoresUnknownClient)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsStatistic> stat(new SrsStatistic());
    MockStatisticSrtForClient srt;
    stat->on_srt_stats("srt-unknown", &srt);
    EXPECT_TRUE(stat->find_client("srt-unknown") == NULL);

    SrsUniquePtr<SrsJsonArray> clients(SrsJsonAny::array());
    HELPER_EXPECT_SUCCESS(stat->dumps_clients(clients.get(), 0, 10));
    EXPECT_EQ(0, clients->count());
    EXPECT_EQ(0, srt.dumps_count_);
}

// The SRT connection dumps the totals since it connected and the current values, without clearing
// the interval counters that the pithy print logs.
VOID TEST(SrtConnStatsTest, DumpsTotalsAndCurrentValues)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsMpegtsSrtConn> conn(new SrsMpegtsSrtConn(NULL, 1, "192.168.1.100", 9000));
    MockSrtConnectionForStats *srt_conn = new MockSrtConnectionForStats();
    srs_freep(conn->srt_conn_);
    conn->srt_conn_ = srt_conn;

    SRT_TRACEBSTATS *p = srt_stats_of(srt_conn->stats_);
    p->msRTT = 23.5;
    p->mbpsBandwidth = 48.25;
    p->msRcvBuf = 118;
    p->msSndBuf = 7;
    p->msRcvTsbPdDelay = 120;
    p->msSndTsbPdDelay = 125;
    p->pktRecvTotal = 100000;
    p->pktRcvLossTotal = 312;
    p->pktRcvRetrans = 298;
    p->pktRcvDropTotal = 4;
    p->pktRcvUndecryptTotal = 1;
    p->pktSentTotal = 2000;
    p->pktSndLossTotal = 11;
    p->pktRetransTotal = 13;
    p->pktSndDropTotal = 2;

    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
    HELPER_EXPECT_SUCCESS(conn->dumps_srt(obj.get()));

    // Dumping peeks, it never clears the interval counters of the pithy print.
    EXPECT_EQ(1, srt_conn->peek_count_);
    EXPECT_EQ(0, srt_conn->clear_count_);

    SrsJsonAny *prop = NULL;
    ASSERT_TRUE((prop = obj->get_property("rtt_ms")) != NULL);
    EXPECT_DOUBLE_EQ(23.5, prop->to_number());
    ASSERT_TRUE((prop = obj->get_property("bandwidth_mbps")) != NULL);
    EXPECT_DOUBLE_EQ(48.25, prop->to_number());
    ASSERT_TRUE((prop = obj->get_property("recv_buf_ms")) != NULL);
    EXPECT_EQ(118, prop->to_integer());
    ASSERT_TRUE((prop = obj->get_property("send_buf_ms")) != NULL);
    EXPECT_EQ(7, prop->to_integer());
    ASSERT_TRUE((prop = obj->get_property("recv_latency_ms")) != NULL);
    EXPECT_EQ(120, prop->to_integer());
    ASSERT_TRUE((prop = obj->get_property("send_latency_ms")) != NULL);
    EXPECT_EQ(125, prop->to_integer());
    ASSERT_TRUE((prop = obj->get_property("recv_packets")) != NULL);
    EXPECT_EQ(100000, prop->to_integer());
    ASSERT_TRUE((prop = obj->get_property("recv_loss")) != NULL);
    EXPECT_EQ(312, prop->to_integer());
    ASSERT_TRUE((prop = obj->get_property("recv_retrans")) != NULL);
    EXPECT_EQ(298, prop->to_integer());
    ASSERT_TRUE((prop = obj->get_property("recv_drop")) != NULL);
    EXPECT_EQ(4, prop->to_integer());
    ASSERT_TRUE((prop = obj->get_property("recv_undecrypt")) != NULL);
    EXPECT_EQ(1, prop->to_integer());
    ASSERT_TRUE((prop = obj->get_property("send_packets")) != NULL);
    EXPECT_EQ(2000, prop->to_integer());
    ASSERT_TRUE((prop = obj->get_property("send_loss")) != NULL);
    EXPECT_EQ(11, prop->to_integer());
    ASSERT_TRUE((prop = obj->get_property("send_retrans")) != NULL);
    EXPECT_EQ(13, prop->to_integer());
    ASSERT_TRUE((prop = obj->get_property("send_drop")) != NULL);
    EXPECT_EQ(2, prop->to_integer());
}

// libsrt has no total of the received retransmits, and the pithy print clears its interval counter, so
// the connection adds up what each print clears. The prints still log the retransmits of their interval.
VOID TEST(SrtConnStatsTest, RecvRetransSurvivesPithyPrintClear)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsMpegtsSrtConn> conn(new SrsMpegtsSrtConn(NULL, 1, "192.168.1.100", 9000));
    MockSrtConnectionForStats *srt_conn = new MockSrtConnectionForStats();
    srs_freep(conn->srt_conn_);
    conn->srt_conn_ = srt_conn;
    SRT_TRACEBSTATS *p = srt_stats_of(srt_conn->stats_);

    // The first pithy print logs and clears 5 retransmits.
    p->pktRcvRetrans = 5;
    if (true) {
        SrsSrtStat s;
        HELPER_EXPECT_SUCCESS(conn->fetch_srt_stats(s));
        EXPECT_EQ(5, s.pktRcvRetrans());
    }

    // The second logs and clears 7.
    p->pktRcvRetrans = 7;
    if (true) {
        SrsSrtStat s;
        HELPER_EXPECT_SUCCESS(conn->fetch_srt_stats(s));
        EXPECT_EQ(7, s.pktRcvRetrans());
    }
    EXPECT_EQ(2, srt_conn->clear_count_);

    // 3 more since the last print, so 15 in total.
    p->pktRcvRetrans = 3;
    SrsUniquePtr<SrsJsonObject> obj(SrsJsonAny::object());
    HELPER_EXPECT_SUCCESS(conn->dumps_srt(obj.get()));

    SrsJsonAny *prop = obj->get_property("recv_retrans");
    ASSERT_TRUE(prop != NULL);
    EXPECT_EQ(15, prop->to_integer());

    // Dumping does not clear, so dumping again gives the same total.
    SrsUniquePtr<SrsJsonObject> again(SrsJsonAny::object());
    HELPER_EXPECT_SUCCESS(conn->dumps_srt(again.get()));
    prop = again->get_property("recv_retrans");
    ASSERT_TRUE(prop != NULL);
    EXPECT_EQ(15, prop->to_integer());
}

// Run an SRT connection with the stream id against a real statistic, check the HTTP API dumps its SRT
// stats while it is connected, and that the client is removed after it quits.
static void run_srt_conn_with_statistic(string streamid, SrsRtmpConnType type)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsStatistic> stat(new SrsStatistic());
    SrsUniquePtr<MockAppConfig> mock_config(new MockAppConfig());
    SrsUniquePtr<MockConnectionManager> mock_manager(new MockConnectionManager());
    SrsUniquePtr<MockLiveSourceManager> mock_sources(new MockLiveSourceManager());
    SrsUniquePtr<MockStreamPublishTokenManager> mock_tokens(new MockStreamPublishTokenManager());
    SrsUniquePtr<MockHttpHooks> mock_hooks(new MockHttpHooks());
    SrsUniquePtr<MockRtcSourceManager> mock_rtc_sources(new MockRtcSourceManager());
    SrsUniquePtr<MockSrtSourceManager> mock_srt_sources(new MockSrtSourceManager());
    MockSrtConnectionForStats *mock_srt_conn = new MockSrtConnectionForStats();
    MockSecurity *mock_security = new MockSecurity();

    mock_config->default_vhost_ = new SrsConfDirective();
    mock_config->default_vhost_->name_ = "vhost";
    mock_config->default_vhost_->args_.push_back("__defaultVhost__");
    mock_config->srt_enabled_ = true;
    mock_config->srt_to_rtmp_ = false;
    mock_config->rtc_from_rtmp_ = false;

    mock_srt_conn->streamid_ = streamid;
    mock_srt_conn->srt_fd_ = 100;
    srt_stats_of(mock_srt_conn->stats_)->msRTT = 31.5;

    // A player needs a publisher.
    if (type == SrsSrtConnPlay) {
        mock_srt_sources->set_can_publish(false);
    }

    SrsUniquePtr<SrsMpegtsSrtConn> conn(new SrsMpegtsSrtConn(mock_manager.get(), 100, "192.168.1.100", 9000));
    conn->config_ = mock_config.get();
    conn->stat_ = stat.get();
    conn->stream_publish_tokens_ = mock_tokens.get();
    conn->srt_sources_ = mock_srt_sources.get();
    conn->live_sources_ = mock_sources.get();
    conn->rtc_sources_ = mock_rtc_sources.get();
    conn->hooks_ = mock_hooks.get();
    srs_freep(conn->srt_conn_);
    conn->srt_conn_ = mock_srt_conn;
    srs_freep(conn->security_);
    conn->security_ = mock_security;
    conn->assemble();

    HELPER_EXPECT_SUCCESS(conn->start());
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);

    string id = conn->get_id().c_str();
    SrsStatisticClient *client = stat->find_client(id);
    ASSERT_TRUE(client != NULL);
    EXPECT_EQ(type, client->type_);
    EXPECT_TRUE(client->srt_ == conn.get());

    if (true) {
        SrsUniquePtr<SrsJsonArray> clients(SrsJsonAny::array());
        HELPER_EXPECT_SUCCESS(stat->dumps_clients(clients.get(), 0, 10));

        SrsJsonObject *obj = find_dumped_client(clients.get(), id);
        ASSERT_TRUE(obj != NULL);
        SrsJsonAny *prop = obj->get_property("srt");
        ASSERT_TRUE(prop != NULL);
        ASSERT_TRUE(prop->is_object());
        SrsJsonAny *rtt = prop->to_object()->get_property("rtt_ms");
        ASSERT_TRUE(rtt != NULL);
        EXPECT_DOUBLE_EQ(31.5, rtt->to_number());
    }

    // The connection ends, so the statistic removes the client before the connection is freed. A player
    // only checks the quit of its client when a packet arrives or it times out, so stop it.
    mock_srt_conn->read_error_ = srs_error_new(ERROR_SOCKET_READ, "mock client quit");
    mock_srt_conn->cond_->signal();
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    conn->stop();
    EXPECT_TRUE(stat->find_client(id) == NULL);

    conn->stat_ = NULL;
}

VOID TEST(SrtConnStatsTest, PublisherDumpsSrtStatsInApi)
{
    run_srt_conn_with_statistic("#!::h=127.0.0.1,r=live/livestream,m=publish", SrsSrtConnPublish);
}

VOID TEST(SrtConnStatsTest, PlayerDumpsSrtStatsInApi)
{
    run_srt_conn_with_statistic("#!::h=127.0.0.1,r=live/livestream,m=request", SrsSrtConnPlay);
}
