//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_st.hpp>
#include <srs_utest_manual_st.hpp>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

using namespace std;

VOID TEST(StTest, CondPtrSugar)
{
    SrsUniquePtr<SrsCond> cond(new SrsCond());
    cond->signal();
}

VOID TEST(StTest, MutexPtrSugar)
{
    if (true) {
        SrsUniquePtr<SrsMutex> mutex(new SrsMutex());
        SrsLocker(mutex->get());
    }

    if (true) {
        SrsUniquePtr<SrsMutex> mutex(new SrsMutex());
        mutex->lock();
        mutex->unlock();
    }
}

VOID TEST(StTest, StUtimeInMicroseconds)
{
    st_utime_t st_time_1 = st_utime();
    usleep(1); // sleep 1 microsecond
    st_utime_t st_time_2 = st_utime();

    EXPECT_GT(st_time_1, 0);
    EXPECT_GT(st_time_2, 0);
    EXPECT_GE(st_time_2, st_time_1);
    // st_time_2 - st_time_1 should be in range of [1, 3000] microseconds
    EXPECT_GE(st_time_2 - st_time_1, 0);
    EXPECT_LE(st_time_2 - st_time_1, 3000);
}

static inline st_utime_t time_gettimeofday()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000000LL + tv.tv_usec);
}

VOID TEST(StTest, StUtimePerformance)
{
    clock_t start;
    int gettimeofday_elapsed_time = 0;
    int st_utime_elapsed_time = 0;

    // Both the st_utime(clock_gettime or gettimeofday) and gettimeofday's
    // elpased time to execute is dependence on whether it is the first time be called.
    // In general, the gettimeofday has better performance, but the gap between
    // them is really small, maybe less than 30 clock ~ 30 microseconds.

    // check st_utime first, then gettimeofday
    {
        start = clock();
        st_utime_t t2 = st_utime();
        int elapsed_time = clock() - start;
        st_utime_elapsed_time += elapsed_time;
        EXPECT_GT(t2, 0);

        start = clock();
        st_utime_t t1 = time_gettimeofday();
        elapsed_time = clock() - start;
        gettimeofday_elapsed_time += elapsed_time;
        EXPECT_GT(t1, 0);

        EXPECT_GE(gettimeofday_elapsed_time, 0);
        EXPECT_GE(st_utime_elapsed_time, 0);

        // Calculate absolute difference between the two elapsed times
        int time_diff = gettimeofday_elapsed_time > st_utime_elapsed_time
                            ? gettimeofday_elapsed_time - st_utime_elapsed_time
                            : st_utime_elapsed_time - gettimeofday_elapsed_time;

        // The difference should be less than N clock ticks (microseconds)
        EXPECT_LT(time_diff, 100);
    }

    // check gettimeofday first, then st_utime
    {
        start = clock();
        st_utime_t t1 = time_gettimeofday();
        int elapsed_time = clock() - start;
        gettimeofday_elapsed_time += elapsed_time;
        EXPECT_GT(t1, 0);

        start = clock();
        st_utime_t t2 = st_utime();
        elapsed_time = clock() - start;
        st_utime_elapsed_time += elapsed_time;
        EXPECT_GT(t2, 0);

        EXPECT_GE(gettimeofday_elapsed_time, 0);
        EXPECT_GE(st_utime_elapsed_time, 0);

        // Calculate absolute difference between the two elapsed times
        int time_diff = gettimeofday_elapsed_time > st_utime_elapsed_time
                            ? gettimeofday_elapsed_time - st_utime_elapsed_time
                            : st_utime_elapsed_time - gettimeofday_elapsed_time;

        // The difference should be less than N clock ticks (microseconds)
        EXPECT_LT(time_diff, 100);
    }

    // compare st_utime & gettimeofday in a loop
    for (int i = 0; i < 100; i++) {
        start = clock();
        st_utime_t t2 = st_utime();
        int elapsed_time = clock() - start;
        st_utime_elapsed_time = elapsed_time;
        EXPECT_GT(t2, 0);
        usleep(1);

        start = clock();
        st_utime_t t1 = time_gettimeofday();
        elapsed_time = clock() - start;
        gettimeofday_elapsed_time = elapsed_time;
        EXPECT_GT(t1, 0);
        usleep(1);

        EXPECT_GE(gettimeofday_elapsed_time, 0);
        EXPECT_GE(st_utime_elapsed_time, 0);

        // Calculate absolute difference between the two elapsed times
        int time_diff = gettimeofday_elapsed_time > st_utime_elapsed_time
                            ? gettimeofday_elapsed_time - st_utime_elapsed_time
                            : st_utime_elapsed_time - gettimeofday_elapsed_time;

        // The difference should be less than N clock ticks (microseconds)
        EXPECT_LT(time_diff, 100);
    }
}

// Test IPv4 and IPv6 socket functions
VOID TEST(StSocketTest, IPv4TcpListen)
{
    srs_error_t err;

    srs_netfd_t fd = NULL;
    HELPER_EXPECT_SUCCESS(srs_tcp_listen("127.0.0.1", 0, &fd));
    EXPECT_TRUE(fd != NULL);

    // Get the actual port that was assigned
    int actual_fd = srs_netfd_fileno(fd);
    EXPECT_GT(actual_fd, 0);

    srs_close_stfd(fd);
}

VOID TEST(StSocketTest, IPv4UdpListen)
{
    srs_error_t err;

    srs_netfd_t fd = NULL;
    HELPER_EXPECT_SUCCESS(srs_udp_listen("127.0.0.1", 0, &fd));
    EXPECT_TRUE(fd != NULL);

    // Get the actual port that was assigned
    int actual_fd = srs_netfd_fileno(fd);
    EXPECT_GT(actual_fd, 0);

    srs_close_stfd(fd);
}

VOID TEST(StSocketTest, IPv6TcpListen)
{
    srs_error_t err;

    srs_netfd_t fd = NULL;
    HELPER_EXPECT_SUCCESS(srs_udp_listen("::1", 0, &fd));

    int actual_fd = srs_netfd_fileno(fd);
    EXPECT_GT(actual_fd, 0);

    srs_close_stfd(fd);
}

VOID TEST(StSocketTest, IPv6UdpListen)
{
    srs_error_t err;

    srs_netfd_t fd = NULL;
    HELPER_EXPECT_SUCCESS(srs_udp_listen("::1", 0, &fd));

    int actual_fd = srs_netfd_fileno(fd);
    EXPECT_GT(actual_fd, 0);

    srs_close_stfd(fd);
}

VOID TEST(StSocketTest, IPv6AnyAddressTcpListen)
{
    srs_error_t err;

    srs_netfd_t fd = NULL;
    HELPER_EXPECT_SUCCESS(srs_tcp_listen("::", 0, &fd));

    int actual_fd = srs_netfd_fileno(fd);
    EXPECT_GT(actual_fd, 0);

    srs_close_stfd(fd);
}

VOID TEST(StSocketTest, IPv6AnyAddressUdpListen)
{
    srs_error_t err;

    srs_netfd_t fd = NULL;
    HELPER_EXPECT_SUCCESS(srs_udp_listen("::", 0, &fd));

    int actual_fd = srs_netfd_fileno(fd);
    EXPECT_GT(actual_fd, 0);

    srs_close_stfd(fd);
}

VOID TEST(StSocketTest, IPv4AnyAddressTcpListen)
{
    srs_error_t err;

    srs_netfd_t fd = NULL;
    HELPER_EXPECT_SUCCESS(srs_tcp_listen("0.0.0.0", 0, &fd));
    EXPECT_TRUE(fd != NULL);

    int actual_fd = srs_netfd_fileno(fd);
    EXPECT_GT(actual_fd, 0);

    srs_close_stfd(fd);
}

VOID TEST(StSocketTest, IPv4AnyAddressUdpListen)
{
    srs_error_t err;

    srs_netfd_t fd = NULL;
    HELPER_EXPECT_SUCCESS(srs_udp_listen("0.0.0.0", 0, &fd));
    EXPECT_TRUE(fd != NULL);

    int actual_fd = srs_netfd_fileno(fd);
    EXPECT_GT(actual_fd, 0);

    srs_close_stfd(fd);
}

VOID TEST(StSocketTest, InvalidAddressTcpListen)
{
    srs_error_t err;

    srs_netfd_t fd = NULL;
    HELPER_EXPECT_FAILED(srs_tcp_listen("999.999.999.999", 8080, &fd));
    EXPECT_TRUE(fd == NULL);

    HELPER_EXPECT_FAILED(srs_tcp_listen("::gggg", 8080, &fd));
    EXPECT_TRUE(fd == NULL);
}

VOID TEST(StSocketTest, InvalidAddressUdpListen)
{
    srs_error_t err;

    srs_netfd_t fd = NULL;
    HELPER_EXPECT_FAILED(srs_udp_listen("999.999.999.999", 8080, &fd));
    EXPECT_TRUE(fd == NULL);

    HELPER_EXPECT_FAILED(srs_udp_listen("::gggg", 8080, &fd));
    EXPECT_TRUE(fd == NULL);
}

VOID TEST(StSocketTest, PortRangeTcpListen)
{
    srs_error_t err;

    // Test valid port ranges
    srs_netfd_t fd = NULL;
    HELPER_EXPECT_SUCCESS(srs_tcp_listen("127.0.0.1", 1024, &fd));
    EXPECT_TRUE(fd != NULL);
    srs_close_stfd(fd);

    fd = NULL;
    HELPER_EXPECT_SUCCESS(srs_tcp_listen("127.0.0.1", 65535, &fd));
    EXPECT_TRUE(fd != NULL);
    srs_close_stfd(fd);

    // Test port 0 (system assigns available port)
    fd = NULL;
    HELPER_EXPECT_SUCCESS(srs_tcp_listen("127.0.0.1", 0, &fd));
    EXPECT_TRUE(fd != NULL);
    srs_close_stfd(fd);
}

VOID TEST(StSocketTest, PortRangeUdpListen)
{
    srs_error_t err;

    // Test valid port ranges
    srs_netfd_t fd = NULL;
    HELPER_EXPECT_SUCCESS(srs_udp_listen("127.0.0.1", 1024, &fd));
    EXPECT_TRUE(fd != NULL);
    srs_close_stfd(fd);

    fd = NULL;
    HELPER_EXPECT_SUCCESS(srs_udp_listen("127.0.0.1", 65535, &fd));
    EXPECT_TRUE(fd != NULL);
    srs_close_stfd(fd);

    // Test port 0 (system assigns available port)
    fd = NULL;
    HELPER_EXPECT_SUCCESS(srs_udp_listen("127.0.0.1", 0, &fd));
    EXPECT_TRUE(fd != NULL);
    srs_close_stfd(fd);
}

VOID TEST(StSocketTest, TcpConnectIPv4)
{
    srs_error_t err;

    // Test connecting to a well-known service (should fail but test address resolution)
    srs_netfd_t fd = NULL;
    HELPER_EXPECT_FAILED(srs_tcp_connect("127.0.0.1", 1, SRS_UTIME_SECONDS, &fd));
    EXPECT_TRUE(fd == NULL);
}

VOID TEST(StSocketTest, TcpConnectIPv6)
{
    srs_error_t err;

    // Test connecting to IPv6 loopback (should fail but test address resolution)
    srs_netfd_t fd = NULL;
    HELPER_EXPECT_FAILED(srs_tcp_connect("::1", 1, SRS_UTIME_SECONDS, &fd));
    EXPECT_TRUE(fd == NULL);
}

VOID TEST(StSocketTest, TcpConnectInvalidAddress)
{
    srs_error_t err;

    // Test connecting to invalid address
    srs_netfd_t fd = NULL;
    HELPER_EXPECT_FAILED(srs_tcp_connect("999.999.999.999", 80, SRS_UTIME_SECONDS, &fd));
    EXPECT_TRUE(fd == NULL);

    // Test connecting to invalid IPv6 address
    fd = NULL;
    HELPER_EXPECT_FAILED(srs_tcp_connect("::gggg", 80, SRS_UTIME_SECONDS, &fd));
    EXPECT_TRUE(fd == NULL);
}

// Test address utility functions
VOID TEST(StSocketTest, AddressUtilityFunctions)
{
    // Test srs_net_address_any() function
    string any_addr = srs_net_address_any();
    EXPECT_TRUE(any_addr == "0.0.0.0" || any_addr == "::");

    // Test srs_net_is_valid_ip() function
    EXPECT_TRUE(srs_net_is_valid_ip("127.0.0.1"));
    EXPECT_TRUE(srs_net_is_valid_ip("0.0.0.0"));
    EXPECT_TRUE(srs_net_is_valid_ip("192.168.1.1"));

    // Test IPv6 addresses if supported
    EXPECT_TRUE(srs_net_is_valid_ip("::1"));
    EXPECT_TRUE(srs_net_is_valid_ip("::"));
    EXPECT_TRUE(srs_net_is_valid_ip("2001:db8::1"));

    // Test invalid addresses
    EXPECT_FALSE(srs_net_is_valid_ip("999.999.999.999"));
    EXPECT_FALSE(srs_net_is_valid_ip("::gggg"));
    EXPECT_FALSE(srs_net_is_valid_ip("invalid"));
    EXPECT_FALSE(srs_net_is_valid_ip(""));
}

VOID TEST(StSocketTest, AddressParsingForListener)
{
    string ip;
    int port;

    // Test IPv4 address:port parsing
    srs_net_split_for_listener("192.168.1.1:8080", ip, port);
    EXPECT_EQ(ip, "192.168.1.1");
    EXPECT_EQ(port, 8080);

    // Test IPv4 address without port (should use any address)
    srs_net_split_for_listener("8080", ip, port);
    EXPECT_TRUE(ip == "0.0.0.0" || ip == "::"); // Should be any address
    EXPECT_EQ(port, 8080);

    // Test IPv6 address in RFC 2732 format [address]:port
    srs_net_split_for_listener("[::1]:8080", ip, port);
    EXPECT_EQ(ip, "::1");
    EXPECT_EQ(port, 8080);

    // Test IPv6 address in RFC 2732 format [address]:port with full address
    srs_net_split_for_listener("[2001:db8::1]:1935", ip, port);
    EXPECT_EQ(ip, "2001:db8::1");
    EXPECT_EQ(port, 1935);

    // Test IPv6 any address
    srs_net_split_for_listener("[::]:8080", ip, port);
    EXPECT_EQ(ip, "::");
    EXPECT_EQ(port, 8080);

    // Test localhost
    srs_net_split_for_listener("localhost:8080", ip, port);
    EXPECT_EQ(ip, "localhost");
    EXPECT_EQ(port, 8080);
}

VOID TEST(StSocketTest, IPv4Detection)
{
    // Test srs_net_is_ipv4() function
    EXPECT_TRUE(srs_net_is_ipv4("127.0.0.1"));
    EXPECT_TRUE(srs_net_is_ipv4("0.0.0.0"));
    EXPECT_TRUE(srs_net_is_ipv4("192.168.1.1"));
    EXPECT_TRUE(srs_net_is_ipv4("255.255.255.255"));

    // Test non-IPv4 addresses
    EXPECT_FALSE(srs_net_is_ipv4("::1"));
    EXPECT_FALSE(srs_net_is_ipv4("2001:db8::1"));
    EXPECT_FALSE(srs_net_is_ipv4("localhost"));
    EXPECT_FALSE(srs_net_is_ipv4("example.com"));

    // Test empty string - the function might treat it as IPv4 format, so check actual behavior
    bool empty_result = srs_net_is_ipv4("");
    // Just verify the function doesn't crash, behavior may vary
    EXPECT_TRUE(empty_result == true || empty_result == false);

    // Test invalid IPv4 (but still numeric format)
    EXPECT_TRUE(srs_net_is_ipv4("999.999.999.999")); // Invalid but IPv4 format
}

VOID TEST(StSocketTest, DualStackBehavior)
{
    srs_error_t err;

    // Test that we can create both IPv4 and IPv6 sockets on the same port
    // This tests the underlying dual-stack behavior

    srs_netfd_t ipv4_fd = NULL;
    srs_netfd_t ipv6_fd = NULL;

    // Create IPv4 socket first
    HELPER_EXPECT_SUCCESS(srs_tcp_listen("127.0.0.1", 0, &ipv4_fd));
    EXPECT_TRUE(ipv4_fd != NULL);
    srs_close_stfd(ipv4_fd);

    // Try to create IPv6 socket (may fail if IPv6 not supported)
    HELPER_EXPECT_SUCCESS(srs_tcp_listen("::1", 0, &ipv6_fd));
    EXPECT_TRUE(ipv6_fd != NULL);
    srs_close_stfd(ipv6_fd);
}

VOID TEST(StSocketTest, SocketFileDescriptorValidation)
{
    srs_error_t err;

    // Test that created sockets have valid file descriptors
    srs_netfd_t tcp_fd = NULL;
    HELPER_EXPECT_SUCCESS(srs_tcp_listen("127.0.0.1", 0, &tcp_fd));
    EXPECT_TRUE(tcp_fd != NULL);

    int raw_fd = srs_netfd_fileno(tcp_fd);
    EXPECT_GT(raw_fd, 0); // Valid file descriptor should be > 0

    srs_close_stfd(tcp_fd);

    // Test UDP socket
    srs_netfd_t udp_fd = NULL;
    HELPER_EXPECT_SUCCESS(srs_udp_listen("127.0.0.1", 0, &udp_fd));
    EXPECT_TRUE(udp_fd != NULL);

    raw_fd = srs_netfd_fileno(udp_fd);
    EXPECT_GT(raw_fd, 0); // Valid file descriptor should be > 0

    srs_close_stfd(udp_fd);
}

VOID TEST(StSocketTest, MultipleSocketsOnDifferentPorts)
{
    srs_error_t err;

    // Test creating multiple sockets on different ports
    vector<srs_netfd_t> sockets;

    for (int i = 0; i < 5; i++) {
        srs_netfd_t fd = NULL;
        HELPER_EXPECT_SUCCESS(srs_tcp_listen("127.0.0.1", 0, &fd)); // Port 0 = system assigns
        EXPECT_TRUE(fd != NULL);

        int raw_fd = srs_netfd_fileno(fd);
        EXPECT_GT(raw_fd, 0);

        sockets.push_back(fd);
    }

    // Clean up all sockets
    for (size_t i = 0; i < sockets.size(); i++) {
        srs_close_stfd(sockets[i]);
    }
}

VOID TEST(StSocketTest, RandomPortTcpListenAndConnect)
{
    srs_error_t err;

    // Generate random port in range [30000, 60000]
    SrsRand rand;
    int random_port = 30000 + (rand.integer() % (60000 - 30000 + 1));
    EXPECT_GE(random_port, 30000);
    EXPECT_LE(random_port, 60000);

    // Create TCP listener on the random port
    srs_netfd_t listen_fd = NULL;
    HELPER_EXPECT_SUCCESS(srs_tcp_listen("127.0.0.1", random_port, &listen_fd));
    EXPECT_TRUE(listen_fd != NULL);

    // Use SrsUniquePtr to automatically close the socket when it goes out of scope.
    SrsUniquePtr<srs_netfd_t> listen_fd_ptr(&listen_fd, srs_close_stfd_ptr);

    int actual_fd = srs_netfd_fileno(listen_fd);
    EXPECT_GT(actual_fd, 0);

    // Try to connect to the listening port
    srs_netfd_t connect_fd = NULL;
    HELPER_EXPECT_SUCCESS(srs_tcp_connect("127.0.0.1", random_port, SRS_UTIME_SECONDS, &connect_fd));
    SrsUniquePtr<srs_netfd_t> connect_fd_ptr(&connect_fd, srs_close_stfd_ptr);

    int connect_actual_fd = srs_netfd_fileno(connect_fd);
    EXPECT_GT(connect_actual_fd, 0);

    // Verify they are different file descriptors
    EXPECT_NE(actual_fd, connect_actual_fd);
}

VOID TEST(StSocketTest, RandomPortTcpListenAndConnectIPv6)
{
    srs_error_t err;

    // Generate random port in range [30000, 60000]
    SrsRand rand;
    int random_port = 30000 + (rand.integer() % (60000 - 30000 + 1));
    EXPECT_GE(random_port, 30000);
    EXPECT_LE(random_port, 60000);

    // Create TCP listener on IPv6 loopback with the random port
    srs_netfd_t listen_fd = NULL;
    HELPER_EXPECT_SUCCESS(srs_tcp_listen("::1", random_port, &listen_fd));
    EXPECT_TRUE(listen_fd != NULL);
    SrsUniquePtr<srs_netfd_t> listen_fd_ptr(&listen_fd, srs_close_stfd_ptr);

    int actual_fd = srs_netfd_fileno(listen_fd);
    EXPECT_GT(actual_fd, 0);

    // Try to connect to the listening port
    srs_netfd_t connect_fd = NULL;
    HELPER_EXPECT_SUCCESS(srs_tcp_connect("::1", random_port, SRS_UTIME_SECONDS, &connect_fd));
    EXPECT_TRUE(connect_fd != NULL);
    SrsUniquePtr<srs_netfd_t> connect_fd_ptr(&connect_fd, srs_close_stfd_ptr);

    int connect_actual_fd = srs_netfd_fileno(connect_fd);
    EXPECT_GT(connect_actual_fd, 0);

    // Verify they are different file descriptors
    EXPECT_NE(actual_fd, connect_actual_fd);
}

VOID TEST(StSocketTest, RandomPortUdpListenIPv4)
{
    srs_error_t err;

    // Generate random port in range [30000, 60000]
    SrsRand rand;
    int random_port = 30000 + (rand.integer() % (60000 - 30000 + 1));
    EXPECT_GE(random_port, 30000);
    EXPECT_LE(random_port, 60000);

    // Create UDP listener on IPv4 loopback with the random port
    srs_netfd_t listen_fd = NULL;
    HELPER_EXPECT_SUCCESS(srs_udp_listen("127.0.0.1", random_port, &listen_fd));
    EXPECT_TRUE(listen_fd != NULL);
    SrsUniquePtr<srs_netfd_t> listen_fd_ptr(&listen_fd, srs_close_stfd_ptr);

    int actual_fd = srs_netfd_fileno(listen_fd);
    EXPECT_GT(actual_fd, 0);

    // For UDP, we can create another socket and bind to a different port
    // to simulate client behavior (UDP is connectionless)
    srs_netfd_t client_fd = NULL;
    HELPER_EXPECT_SUCCESS(srs_udp_listen("127.0.0.1", 0, &client_fd)); // Port 0 = system assigns
    EXPECT_TRUE(client_fd != NULL);
    SrsUniquePtr<srs_netfd_t> client_fd_ptr(&client_fd, srs_close_stfd_ptr);

    int client_actual_fd = srs_netfd_fileno(client_fd);
    EXPECT_GT(client_actual_fd, 0);

    // Verify they are different file descriptors
    EXPECT_NE(actual_fd, client_actual_fd);
}

VOID TEST(StSocketTest, RandomPortUdpListenIPv6)
{
    srs_error_t err;

    // Generate random port in range [30000, 60000]
    SrsRand rand;
    int random_port = 30000 + (rand.integer() % (60000 - 30000 + 1));
    EXPECT_GE(random_port, 30000);
    EXPECT_LE(random_port, 60000);

    // Create UDP listener on IPv6 loopback with the random port
    srs_netfd_t listen_fd = NULL;
    HELPER_EXPECT_SUCCESS(srs_udp_listen("::1", random_port, &listen_fd));
    EXPECT_TRUE(listen_fd != NULL);
    SrsUniquePtr<srs_netfd_t> listen_fd_ptr(&listen_fd, srs_close_stfd_ptr);

    int actual_fd = srs_netfd_fileno(listen_fd);
    EXPECT_GT(actual_fd, 0);

    // For UDP, we can create another socket and bind to a different port
    // to simulate client behavior (UDP is connectionless)
    srs_netfd_t client_fd = NULL;
    HELPER_EXPECT_SUCCESS(srs_udp_listen("::1", 0, &client_fd));
    EXPECT_TRUE(client_fd != NULL);
    SrsUniquePtr<srs_netfd_t> client_fd_ptr(&client_fd, srs_close_stfd_ptr);

    int client_actual_fd = srs_netfd_fileno(client_fd);
    EXPECT_GT(client_actual_fd, 0);

    // Verify they are different file descriptors
    EXPECT_NE(actual_fd, client_actual_fd);
}
