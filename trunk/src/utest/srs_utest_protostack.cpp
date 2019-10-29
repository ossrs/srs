/*
The MIT License (MIT)

Copyright (c) 2013-2019 Winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#include <srs_utest_protostack.hpp>

#include <srs_utest_protocol.hpp>

#include <srs_kernel_error.hpp>
#include <srs_core_autofree.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_rtmp_msg_array.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_st.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_rtmp_stack.hpp>
#include <srs_service_http_conn.hpp>
#include <srs_kernel_buffer.hpp>

using namespace std;

class MockErrorPacket : public SrsPacket
{
protected:
    virtual int get_size() {
        return 1024;
    }
};

VOID TEST(ProtoStackTest, PacketEncode)
{
    srs_error_t err;

    int size;
    char* payload;

    if (true) {
        MockErrorPacket pkt;
        HELPER_EXPECT_FAILED(pkt.encode(size, payload));
    }

    if (true) {
        MockErrorPacket pkt;
        SrsBuffer b;
        HELPER_EXPECT_FAILED(pkt.decode(&b));
    }

    if (true) {
        SrsPacket pkt;
        EXPECT_EQ(0, pkt.get_prefer_cid());
        EXPECT_EQ(0, pkt.get_message_type());
        EXPECT_EQ(0, pkt.get_size());
    }

    if (true) {
        MockErrorPacket pkt;
        EXPECT_EQ(1024, pkt.get_size());
    }
}

VOID TEST(ProtoStackTest, ManualFlush)
{
    srs_error_t err;

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        // Always response ACK message.
        HELPER_EXPECT_SUCCESS(p.set_in_window_ack_size(1));

        // Default is auto response.
        HELPER_EXPECT_SUCCESS(p.response_acknowledgement_message());
        EXPECT_EQ(12+4, io.out_buffer.length());
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        // Always response ACK message.
        HELPER_EXPECT_SUCCESS(p.set_in_window_ack_size(1));

        p.set_auto_response(true);
        HELPER_EXPECT_SUCCESS(p.response_acknowledgement_message());
        EXPECT_EQ(12+4, io.out_buffer.length());
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        // Always response ACK message.
        HELPER_EXPECT_SUCCESS(p.set_in_window_ack_size(1));

        // When not auto response, need to flush it manually.
        p.set_auto_response(false);
        HELPER_EXPECT_SUCCESS(p.response_acknowledgement_message());
        EXPECT_EQ(0, io.out_buffer.length());

        HELPER_EXPECT_SUCCESS(p.manual_response_flush());
        EXPECT_EQ(12+4, io.out_buffer.length());
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        // Always response ACK message.
        HELPER_EXPECT_SUCCESS(p.set_in_window_ack_size(1));

        HELPER_EXPECT_SUCCESS(p.response_ping_message(1024));
        EXPECT_EQ(12+6, io.out_buffer.length());
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        // Always response ACK message.
        HELPER_EXPECT_SUCCESS(p.set_in_window_ack_size(1));

        // When not auto response, need to flush it manually.
        p.set_auto_response(false);
        HELPER_EXPECT_SUCCESS(p.response_ping_message(1024));
        EXPECT_EQ(0, io.out_buffer.length());

        HELPER_EXPECT_SUCCESS(p.manual_response_flush());
        EXPECT_EQ(12+6, io.out_buffer.length());
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        // Always response ACK message.
        HELPER_EXPECT_SUCCESS(p.set_in_window_ack_size(1));

        // When not auto response, need to flush it manually.
        p.set_auto_response(false);
        HELPER_EXPECT_SUCCESS(p.response_ping_message(1024));
        EXPECT_EQ(0, io.out_buffer.length());

        // If not flushed, the packets will be destroyed.
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        p.set_recv_buffer(0);
        p.set_recv_buffer(131072 * 10);

        p.set_merge_read(true, NULL);
        p.set_merge_read(false, NULL);
    }
}

VOID TEST(ProtoStackTest, SendZeroMessages)
{
    srs_error_t err;
    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);
        HELPER_EXPECT_SUCCESS(p.send_and_free_message(NULL, 0));
    }
}

