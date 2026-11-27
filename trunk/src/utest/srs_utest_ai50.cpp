//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai50.hpp>

#include <srs_app_config.hpp>
#include <srs_app_server.hpp>
#include <srs_app_srt_conn.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_resource.hpp>
#include <srs_protocol_srt.hpp>
#include <srs_utest_ai28.hpp>
#include <srs_utest_ai46.hpp>
#include <srs_utest_ai48.hpp>
#include <srs_utest_manual_config.hpp>
#include <srs_utest_manual_mock.hpp>

#include <srt/srt.h>

#include <string>
#include <vector>
using namespace std;

// The libsrt SRS builds has bonding, so an SRS that turns bonding on can make a listener accept socket groups.
// srt.h declares the group API in every build; without bonding the calls are stubs that fail, so the build is
// checked by calling them.
VOID TEST(SrtBondingBuildTest, LibsrtCreatesBroadcastAndBackupGroups)
{
    SRT_GROUP_TYPE types[] = {SRT_GTYPE_BROADCAST, SRT_GTYPE_BACKUP};
    for (int i = 0; i < (int)(sizeof(types) / sizeof(types[0])); i++) {
        SRTSOCKET group = srt_create_group(types[i]);
        ASSERT_NE(SRT_INVALID_SOCK, group) << "type " << types[i] << ": " << srt_getlasterror_str();

        // A group ID has the group bit, so libsrt and SRS tell it from a socket ID.
        EXPECT_NE(0, group & SRTGROUP_MASK);
        EXPECT_EQ(0, srt_close(group));
    }
}

VOID TEST(SrtBondingBuildTest, ListenerSocketTakesGroupConnect)
{
    srs_error_t err;

    // A socket made like SRS's listener takes the option that lets it accept group callers.
    srs_srt_t fd = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(srs_srt_socket_with_default_option(&fd));

    int on = 1;
    EXPECT_EQ(0, srt_setsockflag(fd, SRTO_GROUPCONNECT, &on, sizeof(on))) << srt_getlasterror_str();

    int value = 0;
    int size = sizeof(value);
    EXPECT_EQ(0, srt_getsockflag(fd, SRTO_GROUPCONNECT, &value, &size)) << srt_getlasterror_str();
    EXPECT_EQ(1, value);

    HELPER_EXPECT_SUCCESS(srs_srt_close(fd));
}

// Bonding is off unless the operator turns it on, so a server that upgrades keeps refusing group callers.
VOID TEST(SrtBondingConfigTest, OffByDefault)
{
    srs_error_t err;

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_FALSE(conf.get_srt_bonding());
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled on;}"));
        EXPECT_FALSE(conf.get_srt_bonding());
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled on; bonding;}"));
        EXPECT_FALSE(conf.get_srt_bonding());
    }

    // The mock config the SRT tests share keeps bonding off too.
    if (true) {
        MockAppConfig conf;
        EXPECT_FALSE(conf.get_srt_bonding());
    }
}

// The config file turns bonding on; the config check accepts the directive.
VOID TEST(SrtBondingConfigTest, FileTurnsItOn)
{
    srs_error_t err;

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled on; bonding on;}"));
        EXPECT_TRUE(conf.get_srt_bonding());
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled on; bonding off;}"));
        EXPECT_FALSE(conf.get_srt_bonding());
    }
}

// SRS_SRT_SERVER_BONDING wins over the config file both ways, and works with no config file entry.
VOID TEST(SrtBondingConfigTest, EnvOverridesFile)
{
    srs_error_t err;

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled on; bonding on;}"));
        SrsSetEnvConfig(conf, srt_bonding, "SRS_SRT_SERVER_BONDING", "off");
        EXPECT_FALSE(conf.get_srt_bonding());
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "srt_server{enabled on; bonding off;}"));
        SrsSetEnvConfig(conf, srt_bonding, "SRS_SRT_SERVER_BONDING", "on");
        EXPECT_TRUE(conf.get_srt_bonding());
    }

    if (true) {
        MockSrsConfig conf;
        SrsSetEnvConfig(conf, srt_bonding, "SRS_SRT_SERVER_BONDING", "on");
        EXPECT_TRUE(conf.get_srt_bonding());
    }
}

// An acceptor of mocks: the options and the listener record their calls into one log, so a test sees the order.
class SrtBondingUtestAcceptor
{
public:
    vector<string> calls_;
    MockAppConfigForSrtAcceptor config_;
    MockSrtOptionsForSrtAcceptor options_;
    MockAppFactoryForSrtAcceptor factory_;
    SrsSrtAcceptor acceptor_;

public:
    SrtBondingUtestAcceptor() : options_(&calls_), factory_(new MockSrtListenerForSrtAcceptor(&calls_)), acceptor_(NULL)
    {
        acceptor_.config_ = &config_;
        acceptor_.app_factory_ = &factory_;
        srs_freep(acceptor_.srt_options_);
        acceptor_.srt_options_ = &options_;
    }
    virtual ~SrtBondingUtestAcceptor()
    {
        acceptor_.config_ = NULL;
        acceptor_.app_factory_ = NULL;
        acceptor_.srt_options_ = NULL;
    }

public:
    int count(string call)
    {
        int n = 0;
        for (int i = 0; i < (int)calls_.size(); i++) {
            n += (calls_[i] == call) ? 1 : 0;
        }
        return n;
    }
};

// With bonding on, the listening socket takes group callers: libsrt reads the option only before the socket
// listens, so it is set on the listener's fd before listen.
VOID TEST(SrtBondingListenerTest, BondingOn_GroupConnectSetBeforeListen)
{
    srs_error_t err;

    SrtBondingUtestAcceptor a;
    a.config_.set_srt_bonding(true);
    HELPER_ASSERT_SUCCESS(a.acceptor_.listen("192.0.2.1", 10080));

    ASSERT_EQ(1, a.count("group_connect=1"));
    ASSERT_GE((int)a.calls_.size(), 2);
    EXPECT_STREQ("group_connect=1", a.calls_[a.calls_.size() - 2].c_str());
    EXPECT_STREQ("listen", a.calls_.back().c_str());
    EXPECT_EQ(a.factory_.listener_->fd(), a.options_.fds_.back());
}

// With bonding off, the option is never set, so a libsrt without bonding, which refuses it, keeps working.
VOID TEST(SrtBondingListenerTest, BondingOff_GroupConnectNotSet)
{
    srs_error_t err;

    SrtBondingUtestAcceptor a;
    HELPER_ASSERT_SUCCESS(a.acceptor_.listen("192.0.2.1", 10080));

    for (int i = 0; i < (int)a.calls_.size(); i++) {
        EXPECT_EQ(string::npos, a.calls_[i].find("group_connect")) << a.calls_[i];
    }
    EXPECT_STREQ("listen", a.calls_.back().c_str());
}

// A libsrt built without bonding, such as a system libsrt with --sys-srt, refuses the option. With bonding on
// the server then fails to start, saying why, instead of running with group callers refused.
VOID TEST(SrtBondingListenerTest, GroupConnectRefused_ListenFailsNamingBonding)
{
    SrtBondingUtestAcceptor a;
    a.config_.set_srt_bonding(true);
    a.options_.fail_option_ = "group_connect";

    srs_error_t err = a.acceptor_.listen("192.0.2.1", 10080);
    EXPECT_EQ(ERROR_SOCKET_LISTEN, srs_error_code(err));
    string desc = srs_error_desc(err);
    EXPECT_NE(string::npos, desc.find("srt_server.bonding")) << desc;
    EXPECT_NE(string::npos, desc.find("libsrt built with bonding")) << desc;
    srs_freep(err);

    EXPECT_EQ(0, a.count("listen"));
}

// The option set by SRS's own call on a listener socket is the one libsrt reads back.
VOID TEST(SrtBondingListenerTest, SetGroupConnect_LibsrtReadsItBack)
{
    srs_error_t err;

    srs_srt_t fd = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(srs_srt_socket_with_default_option(&fd));

    int value = -1;
    int size = sizeof(value);
    HELPER_EXPECT_SUCCESS(srs_srt_set_group_connect(fd, true));
    EXPECT_EQ(0, srt_getsockflag(fd, SRTO_GROUPCONNECT, &value, &size)) << srt_getlasterror_str();
    EXPECT_EQ(1, value);

    HELPER_EXPECT_SUCCESS(srs_srt_set_group_connect(fd, false));
    EXPECT_EQ(0, srt_getsockflag(fd, SRTO_GROUPCONNECT, &value, &size)) << srt_getlasterror_str();
    EXPECT_EQ(0, value);

    HELPER_EXPECT_SUCCESS(srs_srt_close(fd));
}

// An encoder that bonds two links to a server with bonding off is refused in the handshake, so no link connects
// and the server is handed nothing; a plain encoder to the same port is still handed to the server.
VOID TEST(SrtBondingListenerTest, BondingOff_GroupCallerRefusedPlainCallerHanded)
{
    srs_error_t err;

    SrtListenerUtestServer s;
    HELPER_ASSERT_SUCCESS(s.listen("127.0.0.1"));

    SrtUtestBondCaller bond(SRT_GTYPE_BROADCAST);
    HELPER_ASSERT_SUCCESS(bond.add_link("127.0.0.1", s.port_));
    HELPER_ASSERT_SUCCESS(bond.add_link("127.0.0.1", s.port_));
    err = bond.connect("#!::r=live/bond,m=publish", 120 * SRS_UTIME_MILLISECONDS);
    EXPECT_EQ(ERROR_SRT_CONN, srs_error_code(err));
    srs_freep(err);
    EXPECT_EQ(0, bond.connected_links());

    SrtListenerUtestCaller encoder;
    HELPER_ASSERT_SUCCESS(encoder.connect(s.port_, "#!::r=live/plain,m=publish"));
    s.wait_for_callers(1);
    ASSERT_EQ(1, (int)s.server_.fds_.size());
    EXPECT_EQ(0, s.server_.fds_[0] & SRTGROUP_MASK);
    EXPECT_STREQ("#!::r=live/plain,m=publish", s.streamid_of(0).c_str());
}

// With bonding on, an encoder that bonds two links is handed to the server once, as one group with the stream
// ID of its links, not as two connections; a plain encoder and a plain player to the same port are still each
// handed as a socket of their own.
VOID TEST(SrtBondingListenerTest, BondingOn_GroupHandedOncePlainCallersToo)
{
    srs_error_t err;

    SrtListenerUtestServer s;
    HELPER_ASSERT_SUCCESS(s.listen("127.0.0.1", "bonding on;"));

    SrtUtestBondCaller bond(SRT_GTYPE_BROADCAST);
    HELPER_ASSERT_SUCCESS(bond.add_link("127.0.0.1", s.port_));
    HELPER_ASSERT_SUCCESS(bond.add_link("127.0.0.1", s.port_));
    HELPER_ASSERT_SUCCESS(bond.connect("#!::r=live/bond,m=publish", 120 * SRS_UTIME_MILLISECONDS));
    EXPECT_EQ(2, bond.connected_links());

    s.wait_for_callers(1);
    ASSERT_EQ(1, (int)s.server_.fds_.size());
    EXPECT_NE(0, s.server_.fds_[0] & SRTGROUP_MASK);
    EXPECT_STREQ("#!::r=live/bond,m=publish", s.streamid_of(0).c_str());

    SrtListenerUtestCaller encoder;
    HELPER_ASSERT_SUCCESS(encoder.connect(s.port_, "#!::r=live/plain,m=publish"));
    SrtListenerUtestCaller player;
    HELPER_ASSERT_SUCCESS(player.connect(s.port_, "#!::r=live/plain,m=request"));
    s.wait_for_callers(3);

    // The group's second link joined the group inside libsrt, so the server was never handed it as a caller.
    ASSERT_EQ(3, (int)s.server_.fds_.size());
    EXPECT_EQ(0, s.server_.fds_[1] & SRTGROUP_MASK);
    EXPECT_EQ(0, s.server_.fds_[2] & SRTGROUP_MASK);
    EXPECT_STREQ("#!::r=live/plain,m=publish", s.streamid_of(1).c_str());
    EXPECT_STREQ("#!::r=live/plain,m=request", s.streamid_of(2).c_str());
}

// The port of an IPv4 address that libsrt reports.
static int srt_bonding_utest_port(const sockaddr_storage &addr)
{
    return ntohs(((const sockaddr_in *)&addr)->sin_port);
}

// Wait until the listener's side of the group has n links, as long as a handshake may take on loopback.
static vector<SRT_SOCKGROUPDATA> srt_bonding_utest_wait_links(srs_srt_t group, int n)
{
    vector<SRT_SOCKGROUPDATA> members = srt_utest_group_members(group);
    for (int i = 0; i < 300 && (int)members.size() < n; i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
        members = srt_utest_group_members(group);
    }
    return members;
}

// A plain caller is handed to the server with its own address, with bonding off or on, as before bonding: the
// server logs the client by it, checks it against the security rules and lists it in the HTTP API.
VOID TEST(SrtBondingAcceptTest, PlainCaller_HandedWithItsAddress)
{
    srs_error_t err;

    const char *directives[] = {"", "bonding on;"};
    for (int i = 0; i < 2; i++) {
        SrtListenerUtestServer s;
        HELPER_ASSERT_SUCCESS(s.listen("127.0.0.1", directives[i]));

        SrtListenerUtestCaller encoder;
        HELPER_ASSERT_SUCCESS(encoder.connect(s.port_, "#!::r=live/plain,m=publish"));
        s.wait_for_callers(1);
        ASSERT_EQ(1, (int)s.server_.fds_.size()) << directives[i];

        string ip;
        int port = 0;
        HELPER_ASSERT_SUCCESS(srs_srt_get_local_ip_port(encoder.skt_->fd(), ip, port));
        EXPECT_STREQ("127.0.0.1", s.server_.ips_[0].c_str()) << directives[i];
        EXPECT_EQ(port, s.server_.ports_[0]) << directives[i];
    }
}

// A group has no peer of its own, so the server is handed it with the address of the link that brought it up. A
// link that joins later changes nothing: the client IP of the logs, the security rules and the HTTP API stays the
// first link's.
VOID TEST(SrtBondingAcceptTest, Group_HandedWithFirstLinksAddress)
{
    srs_error_t err;

    SrtListenerUtestServer s;
    HELPER_ASSERT_SUCCESS(s.listen("127.0.0.1", "bonding on;"));

    SrtUtestBondCaller bond(SRT_GTYPE_BROADCAST);
    HELPER_ASSERT_SUCCESS(bond.add_link("127.0.0.1", s.port_));
    HELPER_ASSERT_SUCCESS(bond.connect("#!::r=live/bond,m=publish", 120 * SRS_UTIME_MILLISECONDS));
    s.wait_for_callers(1);
    ASSERT_EQ(1, (int)s.server_.fds_.size());
    srs_srt_t group = s.server_.fds_[0];
    ASSERT_NE(0, group & SRTGROUP_MASK);

    // libsrt has no peer address of a group, which is why the address comes with the accept.
    string peer_ip;
    int peer_port = 0;
    err = srs_srt_get_remote_ip_port(group, peer_ip, peer_port);
    EXPECT_TRUE(err != srs_success);
    srs_freep(err);

    // The listener's side of the group has the first link only, from the first link's relay.
    vector<SRT_SOCKGROUPDATA> members = srt_bonding_utest_wait_links(group, 1);
    ASSERT_EQ(1, (int)members.size());
    int first_port = srt_bonding_utest_port(members[0].peeraddr);
    EXPECT_STREQ("127.0.0.1", s.server_.ips_[0].c_str());
    EXPECT_EQ(first_port, s.server_.ports_[0]);

    // The encoder brings up a second link, from another port.
    HELPER_ASSERT_SUCCESS(bond.join("127.0.0.1", s.port_));
    EXPECT_EQ(2, bond.connected_links());
    members = srt_bonding_utest_wait_links(group, 2);
    ASSERT_EQ(2, (int)members.size());
    int nn_first = 0;
    for (int i = 0; i < (int)members.size(); i++) {
        nn_first += (srt_bonding_utest_port(members[i].peeraddr) == first_port) ? 1 : 0;
    }
    EXPECT_EQ(1, nn_first);

    // The group was handed once, with the first link's address.
    srs_usleep(20 * SRS_UTIME_MILLISECONDS);
    ASSERT_EQ(1, (int)s.server_.fds_.size());
    EXPECT_EQ(first_port, s.server_.ports_[0]);
}

// The server serves a group with the address it was handed, where it read the peer of the socket before and
// closed every group, which has none. The connection's IP and port, which its logs, security rules, hooks and
// HTTP API use, are the first link's.
VOID TEST(SrtBondingAcceptTest, ServerServesGroupWithFirstLinksAddress)
{
    srs_error_t err;

    SrtListenerUtestServer s;
    HELPER_ASSERT_SUCCESS(s.listen("127.0.0.1", "bonding on;"));

    SrtUtestBondCaller bond(SRT_GTYPE_BROADCAST);
    HELPER_ASSERT_SUCCESS(bond.add_link("127.0.0.1", s.port_));
    HELPER_ASSERT_SUCCESS(bond.add_link("127.0.0.1", s.port_));
    HELPER_ASSERT_SUCCESS(bond.connect("#!::r=live/bond,m=publish", 120 * SRS_UTIME_MILLISECONDS));
    s.wait_for_callers(1);
    ASSERT_EQ(1, (int)s.server_.fds_.size());

    // The server owns the group from here, so the test server does not close it.
    srs_srt_t group = s.server_.fds_[0];
    string ip = s.server_.ips_[0];
    int port = s.server_.ports_[0];
    s.server_.fds_.clear();

    SrsResourceManager manager("utest-ai50-srt");
    manager.assemble();
    HELPER_ASSERT_SUCCESS(manager.start());

    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->assemble();
    server->conn_manager_ = &manager;

    HELPER_EXPECT_SUCCESS(server->accept_srt_client(group, ip, port));
    ASSERT_EQ(1, (int)manager.size());
    SrsMpegtsSrtConn *conn = dynamic_cast<SrsMpegtsSrtConn *>(manager.at(0));
    ASSERT_TRUE(conn != NULL);
    EXPECT_EQ(group, conn->srt_conn_->srtfd());
    EXPECT_STREQ("127.0.0.1", conn->ip_.c_str());
    EXPECT_EQ(port, conn->port_);
    EXPECT_STREQ("127.0.0.1", conn->req_->ip_.c_str());

    // SRT is off in the global config, so the connection refuses the group and the manager frees it.
    for (int i = 0; i < 300 && !manager.empty(); i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
    EXPECT_TRUE(manager.empty());

    server->conn_manager_ = NULL;
}

// A connection tells a socket group from a socket by its fd, to serve a group as a publisher only: a group ID is
// one of either type, a socket ID or the invalid fd is not.
VOID TEST(SrtBondingPublishOnlyTest, IsGroup_GroupIdsOnly)
{
    srs_error_t err;

    SRT_GROUP_TYPE types[] = {SRT_GTYPE_BROADCAST, SRT_GTYPE_BACKUP};
    for (int i = 0; i < (int)(sizeof(types) / sizeof(types[0])); i++) {
        SRTSOCKET group = srt_create_group(types[i]);
        ASSERT_NE(SRT_INVALID_SOCK, group) << srt_getlasterror_str();
        EXPECT_TRUE(srs_srt_is_group(group)) << "type " << types[i];
        EXPECT_EQ(0, srt_close(group));
    }

    srs_srt_t fd = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(srs_srt_socket_with_default_option(&fd));
    EXPECT_FALSE(srs_srt_is_group(fd));
    HELPER_EXPECT_SUCCESS(srs_srt_close(fd));

    EXPECT_FALSE(srs_srt_is_group(srs_srt_socket_invalid()));
}

// The accepted group of a bonded player is a group to the server's connection, as its publisher's is.
VOID TEST(SrtBondingPublishOnlyTest, AcceptedPlayerGroup_IsGroup)
{
    srs_error_t err;

    SrtListenerUtestServer s;
    HELPER_ASSERT_SUCCESS(s.listen("127.0.0.1", "bonding on;"));

    SrtUtestBondCaller bond(SRT_GTYPE_BACKUP);
    HELPER_ASSERT_SUCCESS(bond.add_link("127.0.0.1", s.port_));
    HELPER_ASSERT_SUCCESS(bond.add_link("127.0.0.1", s.port_));
    HELPER_ASSERT_SUCCESS(bond.connect("#!::r=live/bond,m=request", 120 * SRS_UTIME_MILLISECONDS));
    s.wait_for_callers(1);
    ASSERT_EQ(1, (int)s.server_.fds_.size());
    EXPECT_TRUE(srs_srt_is_group(s.server_.fds_[0]));
}

// Take the messages the listener's side of a group has, each one's first byte, for up to the time.
static vector<int> srt_bonding_utest_recv(srs_srt_t group, int n, srs_utime_t timeout)
{
    vector<int> got;
    char buf[1500];
    srs_utime_t deadline = srs_time_now_realtime() + timeout;
    while ((int)got.size() < n && srs_time_now_realtime() < deadline) {
        int nb = srt_recvmsg(group, buf, sizeof(buf));
        if (nb > 0) {
            got.push_back((uint8_t)buf[0]);
            continue;
        }
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }
    return got;
}

// SRS listens for IPv6 on a socket of its own beside the IPv4 one, both on the SRT port, as `listen 10080` with
// `[::]:10080` does. A bonded encoder with an IPv4 network and an IPv6 one, which dials a link over each, reaches
// both listeners: the listener of the first link hands the group, and the other link joins that group inside
// libsrt, which finds it by the encoder's group ID, so the other listener hands nothing. The group carries the
// stream over either family: with the first link down, every message comes over the other.
VOID TEST(SrtBondingAcceptTest, IPv4AndIPv6Links_OneGroupOverBothListeners)
{
    srs_error_t err;

    string ips[] = {"127.0.0.1", "::1"};
    for (int first = 0; first < 2; first++) {
        int second = 1 - first;

        SrtListenerUtestServer s4;
        HELPER_ASSERT_SUCCESS(s4.listen("0.0.0.0", "bonding on;"));
        SrtListenerUtestServer s6;
        HELPER_ASSERT_SUCCESS(s6.listen_at("::", s4.port_, "bonding on;"));
        SrtListenerUtestServer *servers[] = {&s4, &s6};

        // The link of the first family brings the group up, then the other family's link joins.
        SrtUtestBondCaller bond(SRT_GTYPE_BROADCAST);
        HELPER_ASSERT_SUCCESS(bond.add_link(ips[first], s4.port_));
        HELPER_ASSERT_SUCCESS(bond.connect("#!::r=live/bond,m=publish", 120 * SRS_UTIME_MILLISECONDS));
        servers[first]->wait_for_callers(1);
        ASSERT_EQ(1, (int)servers[first]->server_.fds_.size()) << ips[first];
        srs_srt_t group = servers[first]->server_.fds_[0];
        ASSERT_TRUE(srs_srt_is_group(group));
        EXPECT_STREQ(ips[first].c_str(), servers[first]->server_.ips_[0].c_str());

        HELPER_ASSERT_SUCCESS(bond.join(ips[second], s4.port_));
        EXPECT_EQ(2, bond.connected_links()) << ips[first];

        // The server's side of the group has a link of each family, and the other listener handed nothing.
        vector<SRT_SOCKGROUPDATA> members = srt_bonding_utest_wait_links(group, 2);
        ASSERT_EQ(2, (int)members.size()) << ips[first];
        int families = 0;
        for (int i = 0; i < (int)members.size(); i++) {
            SRT_SOCKGROUPDATA &member = members[i];
            EXPECT_EQ(SRTS_CONNECTED, member.sockstate);
            families |= (member.peeraddr.ss_family == AF_INET) ? 1 : (member.peeraddr.ss_family == AF_INET6 ? 2 : 4);
        }
        EXPECT_EQ(3, families) << ips[first];
        srs_usleep(20 * SRS_UTIME_MILLISECONDS);
        EXPECT_EQ(0, (int)servers[second]->server_.fds_.size()) << ips[first];
        EXPECT_EQ(1, (int)servers[first]->server_.fds_.size()) << ips[first];

        // The first family's network goes down: every message comes over the other's, once and in order.
        bond.relay(0)->blackhole();
        const int nb_msgs = 20;
        for (int i = 0; i < nb_msgs; i++) {
            char msg[188];
            memset(msg, i, sizeof(msg));
            HELPER_ASSERT_SUCCESS(bond.send(msg, sizeof(msg)));
            srs_usleep(2 * SRS_UTIME_MILLISECONDS);
        }
        vector<int> got = srt_bonding_utest_recv(group, nb_msgs, 1 * SRS_UTIME_SECONDS);
        ASSERT_EQ(nb_msgs, (int)got.size()) << ips[first];
        for (int i = 0; i < nb_msgs; i++) {
            EXPECT_EQ(i, got[i]) << ips[first];
        }
        EXPECT_GE(bond.relay(0)->blackholed(SrtUtestUdpRelay::Up), nb_msgs);
        EXPECT_GE(bond.relay(1)->forwarded(SrtUtestUdpRelay::Up), nb_msgs);
    }
}

// Bring a group up over one link, then join more links one after another, up to links in all.
static srs_error_t srt_bonding_utest_join_links(SrtUtestBondCaller &bond, int port, int links)
{
    srs_error_t err = srs_success;

    if ((err = bond.add_link("127.0.0.1", port)) != srs_success) {
        return srs_error_wrap(err, "add link");
    }
    if ((err = bond.connect("#!::r=live/bond,m=publish", 120 * SRS_UTIME_MILLISECONDS)) != srs_success) {
        return srs_error_wrap(err, "connect");
    }
    for (int i = 1; i < links; i++) {
        if ((err = bond.join("127.0.0.1", port)) != srs_success) {
            return srs_error_wrap(err, "join link %d", i);
        }
    }

    return err;
}

// Wait until the group has n links, or for as long as a close takes to reach the other side on loopback.
static int srt_bonding_utest_wait_exact(srs_srt_t group, int n)
{
    int size = (int)srt_utest_group_members(group).size();
    for (int i = 0; i < 300 && size != n; i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
        size = (int)srt_utest_group_members(group).size();
    }
    return size;
}

// The links of the group that are connected. A caller sees a link the other side closed only about 1s later, when
// libsrt's collector really closes the socket, so the tests count on the server's side.
static int srt_bonding_utest_connected(srs_srt_t group)
{
    int connected = 0;
    vector<SRT_SOCKGROUPDATA> members = srt_utest_group_members(group);
    for (int i = 0; i < (int)members.size(); i++) {
        connected += (members[i].sockstate == SRTS_CONNECTED) ? 1 : 0;
    }
    return connected;
}

// libsrt itself has no cap on the links of a group: a listener takes every link an encoder brings, 16 here, each a
// libsrt socket with its own buffers. A link that joins is never handed as a caller; the listener tells the server
// it joined, so the server is the one that can bound it.
VOID TEST(SrtBondingLinksTest, LibsrtTakesSixteenLinks_ServerToldOfJoins)
{
    srs_error_t err;

    SrtListenerUtestServer s;
    HELPER_ASSERT_SUCCESS(s.listen("127.0.0.1", "bonding on;"));

    SrtUtestBondCaller bond(SRT_GTYPE_BROADCAST);
    HELPER_ASSERT_SUCCESS(srt_bonding_utest_join_links(bond, s.port_, 16));
    EXPECT_EQ(16, bond.connected_links());

    s.wait_for_callers(1);
    ASSERT_EQ(1, (int)s.server_.fds_.size());
    EXPECT_EQ(16, srt_bonding_utest_wait_exact(s.server_.fds_[0], 16));
    EXPECT_GE(s.server_.links_joined_, 1);
}

// A server keeps at most 8 links of a group: an encoder that brings more has each one after the 8th closed by the
// server once it joins, while the 8 carry the stream on. When a link goes, a new one may take its place.
VOID TEST(SrtBondingLinksTest, GroupKeepsEightLinks_LaterOnesClosed)
{
    srs_error_t err;

    SrsSrtGroups groups;
    SrtListenerUtestServer s;
    s.server_.groups_ = &groups;
    HELPER_ASSERT_SUCCESS(s.listen("127.0.0.1", "bonding on;"));

    SrtUtestBondCaller bond(SRT_GTYPE_BROADCAST);
    HELPER_ASSERT_SUCCESS(srt_bonding_utest_join_links(bond, s.port_, 16));

    s.wait_for_callers(1);
    ASSERT_EQ(1, (int)s.server_.fds_.size());
    srs_srt_t group = s.server_.fds_[0];
    EXPECT_EQ(1, groups.size());
    EXPECT_EQ(8, srt_bonding_utest_wait_exact(group, 8));
    EXPECT_EQ(8, srt_bonding_utest_connected(group));

    // The 8 links carry every message once, in order.
    const int nb_msgs = 20;
    for (int i = 0; i < nb_msgs; i++) {
        char msg[188];
        memset(msg, i, sizeof(msg));
        HELPER_ASSERT_SUCCESS(bond.send(msg, sizeof(msg)));
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }
    vector<int> got = srt_bonding_utest_recv(group, nb_msgs, 1 * SRS_UTIME_SECONDS);
    ASSERT_EQ(nb_msgs, (int)got.size());
    for (int i = 0; i < nb_msgs; i++) {
        EXPECT_EQ(i, got[i]);
    }

    // A link goes, and the link the encoder brings next is kept. The link is closed on the server's side, as libsrt
    // tells a close on the caller's side only at its collector's next pass, about 1s later.
    vector<SRT_SOCKGROUPDATA> members = srt_utest_group_members(group);
    ASSERT_FALSE(members.empty());
    srt_close(members[0].id);
    EXPECT_EQ(7, srt_bonding_utest_wait_exact(group, 7));
    HELPER_ASSERT_SUCCESS(bond.join("127.0.0.1", s.port_));
    EXPECT_EQ(8, srt_bonding_utest_wait_exact(group, 8));
    srs_usleep(20 * SRS_UTIME_MILLISECONDS);
    EXPECT_EQ(8, srt_bonding_utest_connected(group));
    EXPECT_EQ(1, (int)s.server_.fds_.size());
}

// The links of a group may join through the listener of either IP family, which tells only its own server, so the
// bound holds for the links of a group across the listeners of a port.
VOID TEST(SrtBondingLinksTest, LinksOverBothFamilies_EightInAll)
{
    srs_error_t err;

    SrsSrtGroups groups;
    SrtListenerUtestServer s4;
    s4.server_.groups_ = &groups;
    HELPER_ASSERT_SUCCESS(s4.listen("0.0.0.0", "bonding on;"));
    SrtListenerUtestServer s6;
    s6.server_.groups_ = &groups;
    HELPER_ASSERT_SUCCESS(s6.listen_at("::", s4.port_, "bonding on;"));

    SrtUtestBondCaller bond(SRT_GTYPE_BROADCAST);
    HELPER_ASSERT_SUCCESS(srt_bonding_utest_join_links(bond, s4.port_, 8));
    s4.wait_for_callers(1);
    ASSERT_EQ(1, (int)s4.server_.fds_.size());
    srs_srt_t group = s4.server_.fds_[0];
    EXPECT_EQ(8, srt_bonding_utest_wait_exact(group, 8));

    for (int i = 0; i < 2; i++) {
        HELPER_ASSERT_SUCCESS(bond.join("::1", s4.port_));
    }
    EXPECT_GE(s6.server_.links_joined_, 1);
    EXPECT_EQ(8, srt_bonding_utest_wait_exact(group, 8));
    EXPECT_EQ(8, srt_bonding_utest_connected(group));
    EXPECT_EQ(0, (int)s6.server_.fds_.size());
}

// The server bounds the links of each group it serves, for every listener, whose acceptors tell it of the links
// that joined.
VOID TEST(SrtBondingLinksTest, ServerBoundsGroupItServes)
{
    srs_error_t err;

    // A listener whose server does not bound links, so the group has 9.
    SrtListenerUtestServer s;
    HELPER_ASSERT_SUCCESS(s.listen("127.0.0.1", "bonding on;"));
    SrtUtestBondCaller bond(SRT_GTYPE_BROADCAST);
    HELPER_ASSERT_SUCCESS(srt_bonding_utest_join_links(bond, s.port_, 9));
    s.wait_for_callers(1);
    ASSERT_EQ(1, (int)s.server_.fds_.size());
    srs_srt_t group = s.server_.fds_[0];
    ASSERT_EQ(9, srt_bonding_utest_wait_exact(group, 9));
    string ip = s.server_.ips_[0];
    int port = s.server_.ports_[0];
    s.server_.fds_.clear();

    SrsResourceManager manager("utest-ai50-srt-links");
    manager.assemble();
    HELPER_ASSERT_SUCCESS(manager.start());

    SrsUniquePtr<SrsServer> server(new SrsServer());
    server->assemble();
    server->conn_manager_ = &manager;

    // The server serves the group, and is told a link joined before its connection runs.
    HELPER_EXPECT_SUCCESS(server->accept_srt_client(group, ip, port));
    EXPECT_EQ(1, server->srt_groups_->size());
    server->on_srt_links_joined();
    EXPECT_EQ(8, (int)srt_utest_group_members(group).size());

    // SRT is off in the global config, so the connection refuses the group and the manager frees it.
    for (int i = 0; i < 300 && !manager.empty(); i++) {
        srs_usleep(10 * SRS_UTIME_MILLISECONDS);
    }
    EXPECT_TRUE(manager.empty());

    server->conn_manager_ = NULL;
}

// The groups a server was handed are kept only while libsrt has them, so a group that ended takes no place.
VOID TEST(SrtBondingLinksTest, GroupsKeptWhileOpen)
{
    srs_error_t err;

    SrsSrtGroups groups;
    SrtListenerUtestServer s;
    s.server_.groups_ = &groups;
    HELPER_ASSERT_SUCCESS(s.listen("127.0.0.1", "bonding on;"));

    SrtUtestBondCaller bond(SRT_GTYPE_BROADCAST);
    HELPER_ASSERT_SUCCESS(srt_bonding_utest_join_links(bond, s.port_, 1));
    s.wait_for_callers(1);
    ASSERT_EQ(1, (int)s.server_.fds_.size());
    EXPECT_EQ(1, groups.size());

    // The connection of the group ends and closes it; the next link that joins any group lets the server forget it.
    HELPER_EXPECT_SUCCESS(srs_srt_close(s.server_.fds_[0]));
    s.server_.fds_.clear();
    groups.limit_links();
    EXPECT_EQ(0, groups.size());

    // A plain socket is never a group to bound.
    srs_srt_t fd = srs_srt_socket_invalid();
    HELPER_ASSERT_SUCCESS(srs_srt_socket_with_default_option(&fd));
    groups.add(fd);
    EXPECT_EQ(0, groups.size());
    HELPER_EXPECT_SUCCESS(srs_srt_close(fd));
}

// A listener finds the group of a link by the group ID the caller sends, with no other check, so a caller that
// guessed the ID of a live group can claim a link of it. With a passphrase, the server refuses such a link from a
// caller that does not have it, and the group goes on over its own links. libsrt checks the key of every link, so
// the test passes from the start.
VOID TEST(SrtBondingForgedLinkTest, WrongPassphrase_RefusedGroupGoesOn)
{
    srs_error_t err;

    SrtListenerUtestServer s;
    HELPER_ASSERT_SUCCESS(s.listen("127.0.0.1", "bonding on; passphrase 0123456789abcdef;"));

    SrtUtestBondCaller bond(SRT_GTYPE_BROADCAST);
    bond.set_passphrase("0123456789abcdef");
    HELPER_ASSERT_SUCCESS(bond.add_link("127.0.0.1", s.port_));
    HELPER_ASSERT_SUCCESS(bond.add_link("127.0.0.1", s.port_));
    HELPER_ASSERT_SUCCESS(bond.connect("#!::r=live/bond,m=publish", 120 * SRS_UTIME_MILLISECONDS));
    s.wait_for_callers(1);
    ASSERT_EQ(1, (int)s.server_.fds_.size());
    srs_srt_t group = s.server_.fds_[0];
    EXPECT_EQ(2, srt_bonding_utest_wait_exact(group, 2));

    SrtUtestBondCaller forger(SRT_GTYPE_BROADCAST);
    forger.set_passphrase("fedcba9876543210");
    HELPER_ASSERT_SUCCESS(forger.add_link("127.0.0.1", s.port_));
    forger.relay(0)->forge_group(bond.group());
    err = forger.connect("#!::r=live/bond,m=publish", 120 * SRS_UTIME_MILLISECONDS);
    srs_freep(err);
    EXPECT_EQ(0, forger.connected_links());

    srs_usleep(20 * SRS_UTIME_MILLISECONDS);
    EXPECT_EQ(2, (int)srt_utest_group_members(group).size());
    EXPECT_EQ(1, (int)s.server_.fds_.size());

    const int nb_msgs = 20;
    for (int i = 0; i < nb_msgs; i++) {
        char msg[188];
        memset(msg, i, sizeof(msg));
        HELPER_ASSERT_SUCCESS(bond.send(msg, sizeof(msg)));
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }
    vector<int> got = srt_bonding_utest_recv(group, nb_msgs, 1 * SRS_UTIME_SECONDS);
    ASSERT_EQ(nb_msgs, (int)got.size());
    for (int i = 0; i < nb_msgs; i++) {
        EXPECT_EQ(i, got[i]);
    }
}

// Without a passphrase, a caller that sends the ID of a live group joins it, even with another stream ID: libsrt
// checks nothing else, which is why a server with bonding on wants a passphrase. The stream stays the one the group
// was handed with, as the group's stream ID is its first link's, and the server is handed nothing new. This is
// libsrt's behaviour, so the test passes from the start.
VOID TEST(SrtBondingForgedLinkTest, OtherStreamId_PublishTargetKept)
{
    srs_error_t err;

    SrtListenerUtestServer s;
    HELPER_ASSERT_SUCCESS(s.listen("127.0.0.1", "bonding on;"));

    SrtUtestBondCaller bond(SRT_GTYPE_BROADCAST);
    HELPER_ASSERT_SUCCESS(bond.add_link("127.0.0.1", s.port_));
    HELPER_ASSERT_SUCCESS(bond.add_link("127.0.0.1", s.port_));
    HELPER_ASSERT_SUCCESS(bond.connect("#!::r=live/bond,m=publish", 120 * SRS_UTIME_MILLISECONDS));
    s.wait_for_callers(1);
    ASSERT_EQ(1, (int)s.server_.fds_.size());
    srs_srt_t group = s.server_.fds_[0];
    EXPECT_EQ(2, srt_bonding_utest_wait_exact(group, 2));

    SrtUtestBondCaller forger(SRT_GTYPE_BROADCAST);
    HELPER_ASSERT_SUCCESS(forger.add_link("127.0.0.1", s.port_));
    forger.relay(0)->forge_group(bond.group());
    HELPER_ASSERT_SUCCESS(forger.connect("#!::r=live/other,m=publish", 120 * SRS_UTIME_MILLISECONDS));
    EXPECT_EQ(1, forger.connected_links());

    EXPECT_EQ(3, srt_bonding_utest_wait_exact(group, 3));
    srs_usleep(20 * SRS_UTIME_MILLISECONDS);
    EXPECT_EQ(1, (int)s.server_.fds_.size());
    EXPECT_STREQ("#!::r=live/bond,m=publish", s.streamid_of(0).c_str());
}
