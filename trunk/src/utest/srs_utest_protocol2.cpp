//
// Copyright (c) 2013-2022 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//
#include <srs_utest_protocol2.hpp>

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_core_autofree.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_protocol_rtmp_msg_array.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_st.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_http_conn.hpp>
#include <srs_protocol_protobuf.hpp>
#include <srs_kernel_buffer.hpp>

VOID TEST(ProtocolKbpsTest, Connections)
{
    if (true) {
        MockWallClock* clock = new MockWallClock();
        SrsAutoFree(MockWallClock, clock);
        MockStatistic* io = new MockStatistic();
        SrsAutoFree(MockStatistic, io);

        SrsKbps* kbps = new SrsKbps(clock->set_clock(0));
        SrsAutoFree(SrsKbps, kbps);

        SrsNetworkDelta* delta = new SrsNetworkDelta();
        SrsAutoFree(SrsNetworkDelta, delta);
        delta->set_io(io, io);

        // No data, 0kbps.
        kbps->add_delta(delta);
        kbps->sample();

        EXPECT_EQ(0, kbps->get_recv_kbps());
        EXPECT_EQ(0, kbps->get_recv_kbps_30s());
        EXPECT_EQ(0, kbps->get_recv_kbps_5m());

        EXPECT_EQ(0, kbps->get_send_kbps());
        EXPECT_EQ(0, kbps->get_send_kbps_30s());
        EXPECT_EQ(0, kbps->get_send_kbps_5m());

        // 800kbps in 30s.
        clock->set_clock(30 * 1000 * SRS_UTIME_MILLISECONDS);
        io->set_in(30 * 100 * 1000)->set_out(30 * 100 * 1000);
        kbps->add_delta(delta);
        kbps->sample();

        EXPECT_EQ(800, kbps->get_recv_kbps());
        EXPECT_EQ(800, kbps->get_recv_kbps_30s());
        EXPECT_EQ(0, kbps->get_recv_kbps_5m());

        EXPECT_EQ(800, kbps->get_send_kbps());
        EXPECT_EQ(800, kbps->get_send_kbps_30s());
        EXPECT_EQ(0, kbps->get_send_kbps_5m());

        // 800kbps in 300s.
        clock->set_clock(330 * 1000 * SRS_UTIME_MILLISECONDS);
        io->set_in(330 * 100 * 1000)->set_out(330 * 100 * 1000);
        kbps->add_delta(delta);
        kbps->sample();

        EXPECT_EQ(800, kbps->get_recv_kbps());
        EXPECT_EQ(800, kbps->get_recv_kbps_30s());
        EXPECT_EQ(800, kbps->get_recv_kbps_5m());

        EXPECT_EQ(800, kbps->get_send_kbps());
        EXPECT_EQ(800, kbps->get_send_kbps_30s());
        EXPECT_EQ(800, kbps->get_send_kbps_5m());
    }

    if (true) {
        MockWallClock* clock = new MockWallClock();
        SrsAutoFree(MockWallClock, clock);
        MockStatistic* io = new MockStatistic();
        SrsAutoFree(MockStatistic, io);

        SrsKbps* kbps = new SrsKbps(clock->set_clock(0));
        SrsAutoFree(SrsKbps, kbps);

        SrsNetworkDelta* delta = new SrsNetworkDelta();
        SrsAutoFree(SrsNetworkDelta, delta);
        delta->set_io(io, io);

        // No data, 0kbps.
        kbps->add_delta(delta);
        kbps->sample();

        EXPECT_EQ(0, kbps->get_recv_kbps());
        EXPECT_EQ(0, kbps->get_recv_kbps_30s());
        EXPECT_EQ(0, kbps->get_recv_kbps_5m());

        EXPECT_EQ(0, kbps->get_send_kbps());
        EXPECT_EQ(0, kbps->get_send_kbps_30s());
        EXPECT_EQ(0, kbps->get_send_kbps_5m());

        // 800kbps in 30s.
        clock->set_clock(30 * 1000 * SRS_UTIME_MILLISECONDS);
        io->set_in(30 * 100 * 1000);
        kbps->add_delta(delta);
        kbps->sample();

        EXPECT_EQ(800, kbps->get_recv_kbps());
        EXPECT_EQ(800, kbps->get_recv_kbps_30s());
        EXPECT_EQ(0, kbps->get_recv_kbps_5m());

        EXPECT_EQ(0, kbps->get_send_kbps());
        EXPECT_EQ(0, kbps->get_send_kbps_30s());
        EXPECT_EQ(0, kbps->get_send_kbps_5m());

        // 800kbps in 300s.
        clock->set_clock(330 * 1000 * SRS_UTIME_MILLISECONDS);
        io->set_in(330 * 100 * 1000);
        kbps->add_delta(delta);
        kbps->sample();

        EXPECT_EQ(800, kbps->get_recv_kbps());
        EXPECT_EQ(800, kbps->get_recv_kbps_30s());
        EXPECT_EQ(800, kbps->get_recv_kbps_5m());

        EXPECT_EQ(0, kbps->get_send_kbps());
        EXPECT_EQ(0, kbps->get_send_kbps_30s());
        EXPECT_EQ(0, kbps->get_send_kbps_5m());
    }

    if (true) {
        MockWallClock* clock = new MockWallClock();
        SrsAutoFree(MockWallClock, clock);
        MockStatistic* io = new MockStatistic();
        SrsAutoFree(MockStatistic, io);

        SrsKbps* kbps = new SrsKbps(clock->set_clock(0));
        SrsAutoFree(SrsKbps, kbps);

        SrsNetworkDelta* delta = new SrsNetworkDelta();
        SrsAutoFree(SrsNetworkDelta, delta);
        delta->set_io(io, io);

        // No data, 0kbps.
        kbps->add_delta(delta);
        kbps->sample();

        EXPECT_EQ(0, kbps->get_recv_kbps());
        EXPECT_EQ(0, kbps->get_recv_kbps_30s());
        EXPECT_EQ(0, kbps->get_recv_kbps_5m());

        EXPECT_EQ(0, kbps->get_send_kbps());
        EXPECT_EQ(0, kbps->get_send_kbps_30s());
        EXPECT_EQ(0, kbps->get_send_kbps_5m());

        // 800kbps in 30s.
        clock->set_clock(30 * 1000 * SRS_UTIME_MILLISECONDS);
        io->set_out(30 * 100 * 1000);
        kbps->add_delta(delta);
        kbps->sample();

        EXPECT_EQ(0, kbps->get_recv_kbps());
        EXPECT_EQ(0, kbps->get_recv_kbps_30s());
        EXPECT_EQ(0, kbps->get_recv_kbps_5m());

        EXPECT_EQ(800, kbps->get_send_kbps());
        EXPECT_EQ(800, kbps->get_send_kbps_30s());
        EXPECT_EQ(0, kbps->get_send_kbps_5m());

        // 800kbps in 300s.
        clock->set_clock(330 * 1000 * SRS_UTIME_MILLISECONDS);
        io->set_out(330 * 100 * 1000);
        kbps->add_delta(delta);
        kbps->sample();

        EXPECT_EQ(0, kbps->get_recv_kbps());
        EXPECT_EQ(0, kbps->get_recv_kbps_30s());
        EXPECT_EQ(0, kbps->get_recv_kbps_5m());

        EXPECT_EQ(800, kbps->get_send_kbps());
        EXPECT_EQ(800, kbps->get_send_kbps_30s());
        EXPECT_EQ(800, kbps->get_send_kbps_5m());
    }
}

VOID TEST(ProtocolKbpsTest, Delta)
{
    if (true) {
        MockWallClock* clock = new MockWallClock();
        SrsAutoFree(MockWallClock, clock);
        MockStatistic* io = new MockStatistic();
        SrsAutoFree(MockStatistic, io);

        SrsNetworkDelta* delta = new SrsNetworkDelta();
        SrsAutoFree(SrsNetworkDelta, delta);
        delta->set_io(io, io);

        // No data.
        int64_t in, out;
        delta->remark(&in, &out);
        EXPECT_EQ(0, in);
        EXPECT_EQ(0, out);

        // 800kb.
        io->set_in(100 * 1000)->set_out(100 * 1000);
        delta->remark(&in, &out);
        EXPECT_EQ(100 * 1000, in);
        EXPECT_EQ(100 * 1000, out);

        // No data.
        delta->remark(&in, &out);
        EXPECT_EQ(0, in);
        EXPECT_EQ(0, out);
    }

    if (true) {
        MockWallClock* clock = new MockWallClock();
        SrsAutoFree(MockWallClock, clock);
        MockStatistic* io = new MockStatistic();
        SrsAutoFree(MockStatistic, io);

        SrsNetworkDelta* delta = new SrsNetworkDelta();
        SrsAutoFree(SrsNetworkDelta, delta);
        delta->set_io(io, io);

        // No data.
        int64_t in, out;
        delta->remark(&in, &out);
        EXPECT_EQ(0, in);
        EXPECT_EQ(0, out);

        // 800kb.
        io->set_in(100 * 1000)->set_out(100 * 1000);
        delta->remark(&in, &out);
        EXPECT_EQ(100 * 1000, in);
        EXPECT_EQ(100 * 1000, out);

        // Kbps without io, gather delta.
        SrsKbps* kbps = new SrsKbps(clock->set_clock(0));
        SrsAutoFree(SrsKbps, kbps);

        // No data, 0kbps.
        kbps->sample();

        EXPECT_EQ(0, kbps->get_recv_kbps());
        EXPECT_EQ(0, kbps->get_recv_kbps_30s());
        EXPECT_EQ(0, kbps->get_recv_kbps_5m());

        EXPECT_EQ(0, kbps->get_send_kbps());
        EXPECT_EQ(0, kbps->get_send_kbps_30s());
        EXPECT_EQ(0, kbps->get_send_kbps_5m());

        // 800kbps in 30s.
        clock->set_clock(30 * 1000 * SRS_UTIME_MILLISECONDS);
        kbps->add_delta(30 * in, 30 * out);
        kbps->sample();

        EXPECT_EQ(800, kbps->get_recv_kbps());
        EXPECT_EQ(800, kbps->get_recv_kbps_30s());
        EXPECT_EQ(0, kbps->get_recv_kbps_5m());

        EXPECT_EQ(800, kbps->get_send_kbps());
        EXPECT_EQ(800, kbps->get_send_kbps_30s());
        EXPECT_EQ(0, kbps->get_send_kbps_5m());
    }
}

VOID TEST(ProtocolKbpsTest, RAWStatistic)
{
    if (true) {
        MockWallClock* clock = new MockWallClock();
        SrsAutoFree(MockWallClock, clock);
        MockStatistic* io = new MockStatistic();
        SrsAutoFree(MockStatistic, io);

        SrsNetworkDelta* delta = new SrsNetworkDelta();
        SrsAutoFree(SrsNetworkDelta, delta);
        delta->set_io(io, io);

        SrsKbps* kbps = new SrsKbps(clock->set_clock(0));
        SrsAutoFree(SrsKbps, kbps);

        // No data, 0kbps.
        kbps->add_delta(delta);
        kbps->sample();

        EXPECT_EQ(0, kbps->get_recv_kbps());
        EXPECT_EQ(0, kbps->get_recv_kbps_30s());
        EXPECT_EQ(0, kbps->get_recv_kbps_5m());

        EXPECT_EQ(0, kbps->get_send_kbps());
        EXPECT_EQ(0, kbps->get_send_kbps_30s());
        EXPECT_EQ(0, kbps->get_send_kbps_5m());

        // 800kbps in 30s.
        clock->set_clock(30 * 1000 * SRS_UTIME_MILLISECONDS);
        io->set_out(30 * 100 * 1000);
        kbps->add_delta(delta);
        kbps->sample();

        EXPECT_EQ(0, kbps->get_recv_kbps());
        EXPECT_EQ(0, kbps->get_recv_kbps_30s());
        EXPECT_EQ(0, kbps->get_recv_kbps_5m());

        EXPECT_EQ(800, kbps->get_send_kbps());
        EXPECT_EQ(800, kbps->get_send_kbps_30s());
        EXPECT_EQ(0, kbps->get_send_kbps_5m());
    }

    if (true) {
        MockWallClock* clock = new MockWallClock();
        SrsAutoFree(MockWallClock, clock);

        SrsKbps* kbps = new SrsKbps(clock->set_clock(0));
        SrsAutoFree(SrsKbps, kbps);

        // No io, no data.
        EXPECT_EQ(0, kbps->get_recv_bytes());
        EXPECT_EQ(0, kbps->get_send_bytes());

        // With io, zero data.
        MockStatistic* io = new MockStatistic();
        SrsAutoFree(MockStatistic, io);

        SrsNetworkDelta* delta = new SrsNetworkDelta();
        SrsAutoFree(SrsNetworkDelta, delta);
        delta->set_io(io, io);

        kbps->add_delta(delta);
        kbps->sample();
        EXPECT_EQ(0, kbps->get_recv_bytes());
        EXPECT_EQ(0, kbps->get_send_bytes());

        // With io with data.
        io->set_in(100 * 1000)->set_out(100 * 1000);
        kbps->add_delta(delta);
        kbps->sample();
        EXPECT_EQ(100 * 1000, kbps->get_recv_bytes());
        EXPECT_EQ(100 * 1000, kbps->get_send_bytes());

        // No io, cached data.
        delta->set_io(NULL, NULL);
        kbps->add_delta(delta);
        kbps->sample();
        EXPECT_EQ(100 * 1000, kbps->get_recv_bytes());
        EXPECT_EQ(100 * 1000, kbps->get_send_bytes());

        // Use the same IO, but as a fresh io.
        delta->set_io(io, io);
        kbps->add_delta(delta);
        kbps->sample();
        EXPECT_EQ(200 * 1000, kbps->get_recv_bytes());
        EXPECT_EQ(200 * 1000, kbps->get_send_bytes());

        io->set_in(150 * 1000)->set_out(150 * 1000);
        kbps->add_delta(delta);
        kbps->sample();
        EXPECT_EQ(250 * 1000, kbps->get_recv_bytes());
        EXPECT_EQ(250 * 1000, kbps->get_send_bytes());

        // No io, cached data.
        delta->set_io(NULL, NULL);
        kbps->add_delta(delta);
        kbps->sample();
        EXPECT_EQ(250 * 1000, kbps->get_recv_bytes());
        EXPECT_EQ(250 * 1000, kbps->get_send_bytes());
    }
}

VOID TEST(ProtocolKbpsTest, WriteLargeIOVs)
{
    srs_error_t err;

    if (true) {
        iovec iovs[1];
        iovs[0].iov_base = (char*)"Hello";
        iovs[0].iov_len = 5;

        MockBufferIO io;
        ssize_t nn = 0;
        HELPER_EXPECT_SUCCESS(srs_write_large_iovs(&io, iovs, 1, &nn));
        EXPECT_EQ(5, nn);
        EXPECT_EQ(5, io.sbytes);
    }

    if (true) {
        iovec iovs[1024];
        int nn_iovs = (int)(sizeof(iovs)/sizeof(iovec));
        for (int i = 0; i < nn_iovs; i++) {
            iovs[i].iov_base = (char*)"Hello";
            iovs[i].iov_len = 5;
        }

        MockBufferIO io;
        ssize_t nn = 0;
        HELPER_EXPECT_SUCCESS(srs_write_large_iovs(&io, iovs, nn_iovs, &nn));
        EXPECT_EQ(5 * nn_iovs, nn);
        EXPECT_EQ(5 * nn_iovs, io.sbytes);
    }

    if (true) {
        iovec iovs[1025];
        int nn_iovs = (int)(sizeof(iovs)/sizeof(iovec));
        for (int i = 0; i < nn_iovs; i++) {
            iovs[i].iov_base = (char*)"Hello";
            iovs[i].iov_len = 5;
        }

        MockBufferIO io;
        ssize_t nn = 0;
        HELPER_EXPECT_SUCCESS(srs_write_large_iovs(&io, iovs, nn_iovs, &nn));
        EXPECT_EQ(5 * nn_iovs, nn);
        EXPECT_EQ(5 * nn_iovs, io.sbytes);
    }

    if (true) {
        iovec iovs[4096];
        int nn_iovs = (int)(sizeof(iovs)/sizeof(iovec));
        for (int i = 0; i < nn_iovs; i++) {
            iovs[i].iov_base = (char*)"Hello";
            iovs[i].iov_len = 5;
        }

        MockBufferIO io;
        ssize_t nn = 0;
        HELPER_EXPECT_SUCCESS(srs_write_large_iovs(&io, iovs, nn_iovs, &nn));
        EXPECT_EQ(5 * nn_iovs, nn);
        EXPECT_EQ(5 * nn_iovs, io.sbytes);
    }
}

VOID TEST(ProtocolKbpsTest, ConnectionsSugar)
{
    if (true) {
        MockWallClock* clock = new MockWallClock();
        SrsAutoFree(MockWallClock, clock);
        MockStatistic* io = new MockStatistic();
        SrsAutoFree(MockStatistic, io);

        SrsNetworkKbps* kbps = new SrsNetworkKbps(clock->set_clock(0));
        SrsAutoFree(SrsNetworkKbps, kbps);
        kbps->set_io(io, io);

        // No data, 0kbps.
        kbps->sample();

        EXPECT_EQ(0, kbps->get_recv_kbps());
        EXPECT_EQ(0, kbps->get_recv_kbps_30s());
        EXPECT_EQ(0, kbps->get_recv_kbps_5m());

        EXPECT_EQ(0, kbps->get_send_kbps());
        EXPECT_EQ(0, kbps->get_send_kbps_30s());
        EXPECT_EQ(0, kbps->get_send_kbps_5m());

        // 800kbps in 30s.
        clock->set_clock(30 * 1000 * SRS_UTIME_MILLISECONDS);
        io->set_in(30 * 100 * 1000)->set_out(30 * 100 * 1000);
        kbps->sample();

        EXPECT_EQ(800, kbps->get_recv_kbps());
        EXPECT_EQ(800, kbps->get_recv_kbps_30s());
        EXPECT_EQ(0, kbps->get_recv_kbps_5m());

        EXPECT_EQ(800, kbps->get_send_kbps());
        EXPECT_EQ(800, kbps->get_send_kbps_30s());
        EXPECT_EQ(0, kbps->get_send_kbps_5m());

        // 800kbps in 300s.
        clock->set_clock(330 * 1000 * SRS_UTIME_MILLISECONDS);
        io->set_in(330 * 100 * 1000)->set_out(330 * 100 * 1000);
        kbps->sample();

        EXPECT_EQ(800, kbps->get_recv_kbps());
        EXPECT_EQ(800, kbps->get_recv_kbps_30s());
        EXPECT_EQ(800, kbps->get_recv_kbps_5m());

        EXPECT_EQ(800, kbps->get_send_kbps());
        EXPECT_EQ(800, kbps->get_send_kbps_30s());
        EXPECT_EQ(800, kbps->get_send_kbps_5m());
    }

    if (true) {
        MockWallClock* clock = new MockWallClock();
        SrsAutoFree(MockWallClock, clock);
        MockStatistic* io = new MockStatistic();
        SrsAutoFree(MockStatistic, io);

        SrsNetworkKbps* kbps = new SrsNetworkKbps(clock->set_clock(0));
        SrsAutoFree(SrsNetworkKbps, kbps);
        kbps->set_io(io, io);

        // No data, 0kbps.
        kbps->sample();

        EXPECT_EQ(0, kbps->get_recv_kbps());
        EXPECT_EQ(0, kbps->get_recv_kbps_30s());
        EXPECT_EQ(0, kbps->get_recv_kbps_5m());

        EXPECT_EQ(0, kbps->get_send_kbps());
        EXPECT_EQ(0, kbps->get_send_kbps_30s());
        EXPECT_EQ(0, kbps->get_send_kbps_5m());

        // 800kbps in 30s.
        clock->set_clock(30 * 1000 * SRS_UTIME_MILLISECONDS);
        io->set_in(30 * 100 * 1000);
        kbps->sample();

        EXPECT_EQ(800, kbps->get_recv_kbps());
        EXPECT_EQ(800, kbps->get_recv_kbps_30s());
        EXPECT_EQ(0, kbps->get_recv_kbps_5m());

        EXPECT_EQ(0, kbps->get_send_kbps());
        EXPECT_EQ(0, kbps->get_send_kbps_30s());
        EXPECT_EQ(0, kbps->get_send_kbps_5m());

        // 800kbps in 300s.
        clock->set_clock(330 * 1000 * SRS_UTIME_MILLISECONDS);
        io->set_in(330 * 100 * 1000);
        kbps->sample();

        EXPECT_EQ(800, kbps->get_recv_kbps());
        EXPECT_EQ(800, kbps->get_recv_kbps_30s());
        EXPECT_EQ(800, kbps->get_recv_kbps_5m());

        EXPECT_EQ(0, kbps->get_send_kbps());
        EXPECT_EQ(0, kbps->get_send_kbps_30s());
        EXPECT_EQ(0, kbps->get_send_kbps_5m());
    }

    if (true) {
        MockWallClock* clock = new MockWallClock();
        SrsAutoFree(MockWallClock, clock);
        MockStatistic* io = new MockStatistic();
        SrsAutoFree(MockStatistic, io);

        SrsNetworkKbps* kbps = new SrsNetworkKbps(clock->set_clock(0));
        SrsAutoFree(SrsNetworkKbps, kbps);
        kbps->set_io(io, io);

        // No data, 0kbps.
        kbps->sample();

        EXPECT_EQ(0, kbps->get_recv_kbps());
        EXPECT_EQ(0, kbps->get_recv_kbps_30s());
        EXPECT_EQ(0, kbps->get_recv_kbps_5m());

        EXPECT_EQ(0, kbps->get_send_kbps());
        EXPECT_EQ(0, kbps->get_send_kbps_30s());
        EXPECT_EQ(0, kbps->get_send_kbps_5m());

        // 800kbps in 30s.
        clock->set_clock(30 * 1000 * SRS_UTIME_MILLISECONDS);
        io->set_out(30 * 100 * 1000);
        kbps->sample();

        EXPECT_EQ(0, kbps->get_recv_kbps());
        EXPECT_EQ(0, kbps->get_recv_kbps_30s());
        EXPECT_EQ(0, kbps->get_recv_kbps_5m());

        EXPECT_EQ(800, kbps->get_send_kbps());
        EXPECT_EQ(800, kbps->get_send_kbps_30s());
        EXPECT_EQ(0, kbps->get_send_kbps_5m());

        // 800kbps in 300s.
        clock->set_clock(330 * 1000 * SRS_UTIME_MILLISECONDS);
        io->set_out(330 * 100 * 1000);
        kbps->sample();

        EXPECT_EQ(0, kbps->get_recv_kbps());
        EXPECT_EQ(0, kbps->get_recv_kbps_30s());
        EXPECT_EQ(0, kbps->get_recv_kbps_5m());

        EXPECT_EQ(800, kbps->get_send_kbps());
        EXPECT_EQ(800, kbps->get_send_kbps_30s());
        EXPECT_EQ(800, kbps->get_send_kbps_5m());
    }
}

VOID TEST(ProtocolKbpsTest, DeltaSugar)
{
    if (true) {
        MockWallClock* clock = new MockWallClock();
        SrsAutoFree(MockWallClock, clock);
        MockStatistic* io = new MockStatistic();
        SrsAutoFree(MockStatistic, io);

        // Kbps without io, gather delta.
        SrsNetworkKbps* kbps = new SrsNetworkKbps(clock->set_clock(0));
        SrsAutoFree(SrsNetworkKbps, kbps);
        kbps->set_io(io, io);

        // No data, 0kbps.
        kbps->sample();

        EXPECT_EQ(0, kbps->get_recv_kbps());
        EXPECT_EQ(0, kbps->get_recv_kbps_30s());
        EXPECT_EQ(0, kbps->get_recv_kbps_5m());

        EXPECT_EQ(0, kbps->get_send_kbps());
        EXPECT_EQ(0, kbps->get_send_kbps_30s());
        EXPECT_EQ(0, kbps->get_send_kbps_5m());

        // 800kbps in 30s.
        clock->set_clock(30 * 1000 * SRS_UTIME_MILLISECONDS);
        io->set_in(30 * 100 * 1000)->set_out(30 * 100 * 1000);
        kbps->sample();

        EXPECT_EQ(800, kbps->get_recv_kbps());
        EXPECT_EQ(800, kbps->get_recv_kbps_30s());
        EXPECT_EQ(0, kbps->get_recv_kbps_5m());

        EXPECT_EQ(800, kbps->get_send_kbps());
        EXPECT_EQ(800, kbps->get_send_kbps_30s());
        EXPECT_EQ(0, kbps->get_send_kbps_5m());
    }
}

VOID TEST(ProtocolKbpsTest, RAWStatisticSugar)
{
    if (true) {
        MockWallClock* clock = new MockWallClock();
        SrsAutoFree(MockWallClock, clock);
        MockStatistic* io = new MockStatistic();
        SrsAutoFree(MockStatistic, io);

        SrsNetworkKbps* kbps = new SrsNetworkKbps(clock->set_clock(0));
        SrsAutoFree(SrsNetworkKbps, kbps);
        kbps->set_io(io, io);

        // No data, 0kbps.
        kbps->sample();

        EXPECT_EQ(0, kbps->get_recv_kbps());
        EXPECT_EQ(0, kbps->get_recv_kbps_30s());
        EXPECT_EQ(0, kbps->get_recv_kbps_5m());

        EXPECT_EQ(0, kbps->get_send_kbps());
        EXPECT_EQ(0, kbps->get_send_kbps_30s());
        EXPECT_EQ(0, kbps->get_send_kbps_5m());

        // 800kbps in 30s.
        clock->set_clock(30 * 1000 * SRS_UTIME_MILLISECONDS);
        io->set_out(30 * 100 * 1000);
        kbps->sample();

        EXPECT_EQ(0, kbps->get_recv_kbps());
        EXPECT_EQ(0, kbps->get_recv_kbps_30s());
        EXPECT_EQ(0, kbps->get_recv_kbps_5m());

        EXPECT_EQ(800, kbps->get_send_kbps());
        EXPECT_EQ(800, kbps->get_send_kbps_30s());
        EXPECT_EQ(0, kbps->get_send_kbps_5m());
    }

    if (true) {
        MockWallClock* clock = new MockWallClock();
        SrsAutoFree(MockWallClock, clock);

        SrsNetworkKbps* kbps = new SrsNetworkKbps(clock->set_clock(0));
        SrsAutoFree(SrsNetworkKbps, kbps);

        // No io, no data.
        EXPECT_EQ(0, kbps->get_recv_bytes());
        EXPECT_EQ(0, kbps->get_send_bytes());

        // With io, zero data.
        MockStatistic* io = new MockStatistic();
        SrsAutoFree(MockStatistic, io);
        kbps->set_io(io, io);

        kbps->sample();
        EXPECT_EQ(0, kbps->get_recv_bytes());
        EXPECT_EQ(0, kbps->get_send_bytes());

        // With io with data.
        io->set_in(100 * 1000)->set_out(100 * 1000);
        kbps->sample();
        EXPECT_EQ(100 * 1000, kbps->get_recv_bytes());
        EXPECT_EQ(100 * 1000, kbps->get_send_bytes());

        // No io, cached data.
        kbps->set_io(NULL, NULL);
        kbps->sample();
        EXPECT_EQ(100 * 1000, kbps->get_recv_bytes());
        EXPECT_EQ(100 * 1000, kbps->get_send_bytes());

        // Use the same IO, but as a fresh io.
        kbps->set_io(io, io);
        kbps->sample();
        EXPECT_EQ(200 * 1000, kbps->get_recv_bytes());
        EXPECT_EQ(200 * 1000, kbps->get_send_bytes());

        io->set_in(150 * 1000)->set_out(150 * 1000);
        kbps->sample();
        EXPECT_EQ(250 * 1000, kbps->get_recv_bytes());
        EXPECT_EQ(250 * 1000, kbps->get_send_bytes());

        // No io, cached data.
        kbps->set_io(NULL, NULL);
        kbps->sample();
        EXPECT_EQ(250 * 1000, kbps->get_recv_bytes());
        EXPECT_EQ(250 * 1000, kbps->get_send_bytes());
    }
}

