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

class MockPacket : public SrsPacket
{
public:
    int size;
public:
    MockPacket() {
        size = 0;
    }
    virtual ~MockPacket() {
    }
protected:
    virtual int get_size() {
        return size;
    }
};

class MockPacket2 : public MockPacket
{
public:
    char* payload;
public:
    MockPacket2() {
        payload = NULL;
    }
    virtual ~MockPacket2() {
        srs_freep(payload);
    }
    virtual srs_error_t encode(int& size, char*& payload) {
        size = this->size;
        payload = this->payload;
        this->payload = NULL;
        return srs_success;
    }
};

VOID TEST(ProtoStackTest, PacketEncode)
{
    srs_error_t err;

    int size;
    char* payload;

    if (true) {
        MockPacket pkt;
        pkt.size = 1024;

        HELPER_EXPECT_FAILED(pkt.encode(size, payload));
    }

    if (true) {
        MockPacket pkt;
        pkt.size = 1024;

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
        MockPacket pkt;
        pkt.size = 1024;

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

VOID TEST(ProtoStackTest, SendPacketsError)
{
    srs_error_t err;

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        MockPacket* pkt = new MockPacket();
        pkt->size = 1024;
        HELPER_EXPECT_FAILED(p.send_and_free_packet(pkt, 1));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        SrsPacket* pkt = new SrsPacket();
        HELPER_EXPECT_SUCCESS(p.send_and_free_packet(pkt, 1));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        MockPacket2* pkt = new MockPacket2();
        pkt->size = 1024;
        HELPER_EXPECT_SUCCESS(p.send_and_free_packet(pkt, 1));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        SrsCommonMessage pkt;
        pkt.header.initialize_audio(200, 1000, 1);
        pkt.create_payload(256);
        pkt.size = 256;

        SrsSharedPtrMessage* msg = new SrsSharedPtrMessage();
        msg->create(&pkt);
        SrsAutoFree(SrsSharedPtrMessage, msg);

        SrsSharedPtrMessage* msgs[10240];
        for (int i = 0; i < 10240; i++) {
            msgs[i] = msg->copy();
        }

        io.out_err = srs_error_new(1, "fail");
        HELPER_EXPECT_FAILED(p.send_and_free_messages(msgs, 10240, 1));
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

        io.out_err = srs_error_new(1, "fail");
        HELPER_EXPECT_FAILED(p.manual_response_flush());
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        MockPacket2* pkt = new MockPacket2();
        pkt->size = 16;
        pkt->payload = new char[16];

        io.out_err = srs_error_new(1, "fail");
        HELPER_EXPECT_FAILED(p.send_and_free_packet(pkt, 1));
    }
}

VOID TEST(ProtoStackTest, SendHugePacket)
{
    srs_error_t err;

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        MockPacket2* pkt = new MockPacket2();
        pkt->size = 1024;
        pkt->payload = new char[1024];
        HELPER_EXPECT_SUCCESS(p.send_and_free_packet(pkt, 1));
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

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);
        SrsSharedPtrMessage* msg = new SrsSharedPtrMessage();
        HELPER_EXPECT_SUCCESS(p.send_and_free_message(msg, 1));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);
        SrsSharedPtrMessage* msgs[1024];
        for (int i = 0; i < 1024; i++) {
            msgs[i] = new SrsSharedPtrMessage();
        }
        HELPER_EXPECT_SUCCESS(p.send_and_free_messages(msgs, 1024, 0));
    }
}

VOID TEST(ProtoStackTest, HugeMessages)
{
    srs_error_t err;
    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        SrsCommonMessage pkt;
        pkt.header.initialize_audio(200, 1000, 1);
        pkt.create_payload(256);
        pkt.size = 256;

        SrsSharedPtrMessage* msg = new SrsSharedPtrMessage();
        msg->create(&pkt);

        HELPER_EXPECT_SUCCESS(p.send_and_free_message(msg, 1));
        EXPECT_EQ(269, io.out_buffer.length());
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        SrsCommonMessage pkt;
        pkt.header.initialize_audio(200, 1000, 1);
        pkt.create_payload(256);
        pkt.size = 256;

        SrsSharedPtrMessage* msg = new SrsSharedPtrMessage();
        msg->create(&pkt);
        SrsAutoFree(SrsSharedPtrMessage, msg);

        SrsSharedPtrMessage* msgs[1024];
        for (int i = 0; i < 1024; i++) {
            msgs[i] = msg->copy();
        }

        HELPER_EXPECT_SUCCESS(p.send_and_free_messages(msgs, 1024, 1));
        EXPECT_EQ(269*1024, io.out_buffer.length());
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        SrsCommonMessage pkt;
        pkt.header.initialize_audio(200, 1000, 1);
        pkt.create_payload(256);
        pkt.size = 256;

        SrsSharedPtrMessage* msg = new SrsSharedPtrMessage();
        msg->create(&pkt);
        SrsAutoFree(SrsSharedPtrMessage, msg);

        SrsSharedPtrMessage* msgs[10240];
        for (int i = 0; i < 10240; i++) {
            msgs[i] = msg->copy();
        }

        HELPER_EXPECT_SUCCESS(p.send_and_free_messages(msgs, 10240, 1));
        EXPECT_EQ(269*10240, io.out_buffer.length());
    }
}

VOID TEST(ProtoStackTest, DecodeMessages)
{
    srs_error_t err;

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        // AMF0 message with 1B should fail.
        SrsCommonMessage msg;
        msg.header.initialize_amf0_script(1, 1);
        msg.create_payload(1);
        msg.size = 1;

        SrsPacket* pkt;
        HELPER_EXPECT_FAILED(p.decode_message(&msg, &pkt));
    }
}

