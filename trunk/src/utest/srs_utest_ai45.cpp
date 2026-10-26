//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai45.hpp>

#include <srs_app_rtc_server.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_resource.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_utest_ai05.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai18.hpp>
#include <srs_utest_manual_config.hpp>
#include <srs_utest_manual_mock.hpp>

#include <set>
#include <string>
using namespace std;

// An RTC session in the server's connection manager: it subscribes to the manager as SrsRtcConnection::assemble does,
// and marks itself disposing when the manager is about to free it, as SrsRtcConnection::on_before_dispose does.
class RtcServerUtestSession : public MockRtcConnectionForUpdateSessions
{
public:
    ISrsResourceManager *manager_;
    int switches_;

public:
    RtcServerUtestSession(ISrsResourceManager *manager, string username, bool alive)
    {
        manager_ = manager;
        switches_ = 0;
        username_ = username;
        is_alive_ = alive;
        manager_->subscribe(this);
    }
    virtual ~RtcServerUtestSession()
    {
        manager_->unsubscribe(this);
    }

public:
    virtual void on_before_dispose(ISrsResource *c)
    {
        if (c == this) {
            is_disposing_ = true;
        }
    }
    virtual void switch_to_context()
    {
        switches_++;
    }
};

// The server's connection manager, not started, so a removed session waits in the zombies as it does until the
// manager's coroutine runs, with the session manager's 5s check on it, and the server's log kept.
class RtcServerUtestSessions
{
public:
    SrsResourceManager manager_;
    SrsRtcSessionManager sessions_;
    MockLogForSignal log_;
    ISrsLog *saved_log_;

public:
    RtcServerUtestSessions() : manager_("RTC", false)
    {
        manager_.assemble();
        sessions_.conn_manager_ = &manager_;

        saved_log_ = _srs_log;
        _srs_log = &log_;
    }
    ~RtcServerUtestSessions()
    {
        _srs_log = saved_log_;
        sessions_.conn_manager_ = NULL;
    }

public:
    int traces(string text)
    {
        int n = 0;
        for (int i = 0; i < (int)log_.traces_.size(); i++) {
            if (log_.traces_[i].find(text) != string::npos) {
                n++;
            }
        }
        return n;
    }
    string last_trace(string text)
    {
        string found;
        for (int i = 0; i < (int)log_.traces_.size(); i++) {
            if (log_.traces_[i].find(text) != string::npos) {
                found = log_.traces_[i];
            }
        }
        return found;
    }
    bool is_zombie(ISrsResource *c)
    {
        return std::find(manager_.zombies_.begin(), manager_.zombies_.end(), c) != manager_.zombies_.end();
    }
};

// The 5s check of RTC sessions on a server that also serves an RTMP client: the connection manager holds every
// client, so the check skips the RTMP client. A session that timed out is removed once, and at the next check, before
// the manager's coroutine frees it, it is disposing and not removed again. When the last session times out, the
// server has no RTC session, so no stats line is printed.
VOID TEST(AppRtcServerTest, SessionCheck_SkipsRtmpClientAndRemovedSession)
{
    RtcServerUtestSessions s;

    MockSrsResource *rtmp = new MockSrsResource();
    s.manager_.add(rtmp);
    RtcServerUtestSession *alive = new RtcServerUtestSession(&s.manager_, "alive", true);
    s.manager_.add_with_name("alive", alive);
    RtcServerUtestSession *gone = new RtcServerUtestSession(&s.manager_, "gone", false);
    s.manager_.add_with_name("gone", gone);

    s.sessions_.srs_update_rtc_sessions();
    EXPECT_EQ(1, gone->switches_);
    EXPECT_TRUE(gone->is_disposing_);
    EXPECT_TRUE(s.is_zombie(gone));
    EXPECT_EQ(1, (int)s.manager_.zombies_.size());
    EXPECT_EQ(0, alive->switches_);
    EXPECT_EQ(1, s.traces("session destroy by timeout, username=gone"));
    EXPECT_EQ(1, s.traces("RTC: Server conns=1"));

    // The manager has not freed the session yet, so it is still listed.
    EXPECT_EQ(3, (int)s.manager_.size());
    s.sessions_.srs_update_rtc_sessions();
    EXPECT_EQ(1, gone->switches_);
    EXPECT_EQ(1, (int)s.manager_.zombies_.size());
    EXPECT_EQ(1, s.traces("session destroy by timeout"));
    EXPECT_EQ(2, s.traces("RTC: Server conns=1"));

    alive->is_alive_ = false;
    s.sessions_.srs_update_rtc_sessions();
    EXPECT_EQ(1, alive->switches_);
    EXPECT_TRUE(s.is_zombie(alive));
    EXPECT_EQ(2, (int)s.manager_.zombies_.size());
    EXPECT_FALSE(s.is_zombie(rtmp));
    EXPECT_EQ(2, s.traces("RTC: Server conns="));
}

// The server's UDP buffers overflowed since the last check, as /proc/net/snmp counts in RcvbufErrors and SndbufErrors:
// the 5s stats line of the RTC sessions reports the datagrams the kernel dropped, and no loss once none are dropped.
VOID TEST(AppRtcServerTest, SessionCheck_StatsLineReportsUdpBufferLoss)
{
    SrsSnmpUdpStat *udp = srs_get_udp_snmp_stat();
    SrsSnmpUdpStat saved = *udp;

    RtcServerUtestSessions s;
    RtcServerUtestSession *session = new RtcServerUtestSession(&s.manager_, "player", true);
    s.manager_.add_with_name("player", session);

    udp->rcv_buf_errors_delta_ = 3;
    udp->snd_buf_errors_delta_ = 1;
    s.sessions_.srs_update_rtc_sessions();
    string line = s.last_trace("RTC: Server conns=1");
    EXPECT_NE(string::npos, line.find(", loss=(r:3,s:1)")) << line;

    udp->rcv_buf_errors_delta_ = 0;
    udp->snd_buf_errors_delta_ = 0;
    s.sessions_.srs_update_rtc_sessions();
    EXPECT_EQ(2, s.traces("RTC: Server conns=1"));
    line = s.last_trace("RTC: Server conns=1");
    EXPECT_EQ(string::npos, line.find("loss=")) << line;

    *udp = saved;
}

// A host with a loopback and an Ethernet interface, with candidate * and auto detection: the loopback address is never
// a candidate, as a browser on another host cannot reach it, so the answer has the Ethernet address only.
VOID TEST(AppRtcServerTest, Candidates_LoopbackInterfaceSkipped)
{
    srs_error_t err;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on; listen 8000; candidate *; use_auto_detect_network_ip on;}"));

    MockProtocolUtility utility;
    utility.add_ip("127.0.0.1", "lo", true, true, false);
    utility.add_ip("192.168.1.10", "eth0", true, false, false);

    SrsRtcUserConfig ruc;
    set<string> candidates = discover_candidates(&utility, &conf, &ruc);
    ASSERT_EQ(1, (int)candidates.size());
    EXPECT_STREQ("192.168.1.10", candidates.begin()->c_str());
}

// A host whose interfaces cannot be listed (getifaddrs fails, so SRS has no local address): with candidate *, the
// answer has only what the client gave, its ?eip=, and no candidate at all without one.
VOID TEST(AppRtcServerTest, Candidates_NoLocalAddress_OnlyClientEip)
{
    srs_error_t err;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on; listen 8000; candidate *;}"));

    MockProtocolUtility utility;

    if (true) {
        SrsRtcUserConfig ruc;
        ruc.eip_ = "203.0.113.5";
        set<string> candidates = discover_candidates(&utility, &conf, &ruc);
        ASSERT_EQ(1, (int)candidates.size());
        EXPECT_STREQ("203.0.113.5", candidates.begin()->c_str());
    }

    if (true) {
        SrsRtcUserConfig ruc;
        set<string> candidates = discover_candidates(&utility, &conf, &ruc);
        EXPECT_TRUE(candidates.empty());
    }
}

// A WHEP player reaches the API by a domain name DNS does not resolve, such as a name only in the client's hosts file,
// with api_as_candidates, keep_api_domain and resolve_api_domain on: the domain is still a candidate, no resolved
// address is added, and the host's own address is a candidate too.
VOID TEST(AppRtcServerTest, Candidates_ApiDomainNotResolved_DomainKept)
{
    srs_error_t err;

    MockSrsConfig conf;
    HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "rtc_server{enabled on; listen 8000; candidate *; "
                                                       "api_as_candidates on; keep_api_domain on; resolve_api_domain on;}"));

    MockProtocolUtility utility;
    utility.add_ip("192.168.1.10", "eth0", true, false, false);

    SrsRtcUserConfig ruc;
    ruc.req_->host_ = "srs-utest.invalid";
    set<string> candidates = discover_candidates(&utility, &conf, &ruc);
    ASSERT_EQ(2, (int)candidates.size());
    EXPECT_TRUE(candidates.find("srs-utest.invalid") != candidates.end());
    EXPECT_TRUE(candidates.find("192.168.1.10") != candidates.end());
}
