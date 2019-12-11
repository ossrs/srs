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

VOID TEST(ProtoStackTest, OnDecodeMessages)
{
    srs_error_t err;

    SrsSimpleStream bytes;

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        SrsSetChunkSizePacket* pkt = new SrsSetChunkSizePacket();
        pkt->chunk_size = 0;

        HELPER_EXPECT_SUCCESS(p.send_and_free_packet(pkt, 1));
        bytes.append(&io.out_buffer);
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        // Always response ACK message.
        HELPER_EXPECT_SUCCESS(p.set_in_window_ack_size(1));

        SrsCommonMessage* msg;
        io.in_buffer.append(&bytes);
        HELPER_EXPECT_FAILED(p.recv_message(&msg));
        srs_freep(msg);
    }
}

SrsCommonMessage* _create_amf0(char* bytes, int size, int stream_id)
{
    SrsCommonMessage* msg = new SrsCommonMessage();
    msg->header.initialize_amf0_script(size, stream_id);
    msg->create_payload(size);
    memcpy(msg->payload, bytes, size);
    msg->size = size;
    return msg;
}

VOID TEST(ProtoStackTest, OnDecodeMessages2)
{
    srs_error_t err;

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x17, 0x02, 0x00, 0x01, 's', 0x00, 0,0,0,0,0,0,0,0, 0x03,0,0,9};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);
        msg->header.message_type = RTMP_MSG_AMF3CommandMessage;

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);
        HELPER_EXPECT_SUCCESS(p.decode_message(msg, &pkt));

        SrsCallPacket* call = (SrsCallPacket*)pkt;
        EXPECT_STREQ("s", call->command_name.c_str());
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x17, 0x02, 0x00, 0x01, 's'};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);
        msg->header.message_type = RTMP_MSG_AMF3CommandMessage;

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x00};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);
        msg->header.message_type = 0xff;

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        HELPER_EXPECT_SUCCESS(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x02, 0x00, 0x01, 's'};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);
        msg->header.message_type = RTMP_MSG_AMF0DataMessage;

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        HELPER_EXPECT_SUCCESS(p.decode_message(msg, &pkt));
    }
}

VOID TEST(ProtoStackTest, OnDecodeMessages3)
{
    srs_error_t err;

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x02, 0x00, 0x07, '_','r','e','s','u','l','t'};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);
        msg->header.message_type = RTMP_MSG_AMF0DataMessage;

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Decode the response failed, no transaction ID was set by request.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x17, 0x02, 0x00, 0x07, '_','r','e','s','u','l','t'};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);
        msg->header.message_type = RTMP_MSG_AMF3DataMessage;

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Decode the response failed, no transaction ID was set by request.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x17, 0x02, 0x00, 0x07, '_','r','e','s','u','l','t', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);
        msg->header.message_type = RTMP_MSG_AMF3CommandMessage;

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Decode the response failed, no transaction ID was set by request.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        SrsConnectAppPacket* request = new SrsConnectAppPacket();
        request->transaction_id = 0.0;
        HELPER_EXPECT_SUCCESS(p.send_and_free_packet(request, 1));

        uint8_t bytes[] = {0x02, 0x00, 0x07, '_','r','e','s','u','l','t', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the response packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        SrsCreateStreamPacket* request = new SrsCreateStreamPacket();
        request->transaction_id = 0.0;
        HELPER_EXPECT_SUCCESS(p.send_and_free_packet(request, 1));

        uint8_t bytes[] = {0x02, 0x00, 0x07, '_','r','e','s','u','l','t', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the response packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        SrsFMLEStartPacket* request = SrsFMLEStartPacket::create_FC_publish("livestream");
        request->transaction_id = 0.0;
        HELPER_EXPECT_SUCCESS(p.send_and_free_packet(request, 1));

        uint8_t bytes[] = {0x02, 0x00, 0x07, '_','r','e','s','u','l','t', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the response packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        SrsFMLEStartPacket* request = SrsFMLEStartPacket::create_release_stream("livestream");
        request->transaction_id = 0.0;
        HELPER_EXPECT_SUCCESS(p.send_and_free_packet(request, 1));

        uint8_t bytes[] = {0x02, 0x00, 0x07, '_','r','e','s','u','l','t', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the response packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        SrsFMLEStartPacket* request = SrsFMLEStartPacket::create_release_stream("livestream");
        request->command_name = RTMP_AMF0_COMMAND_UNPUBLISH;
        request->transaction_id = 0.0;
        HELPER_EXPECT_SUCCESS(p.send_and_free_packet(request, 1));

        uint8_t bytes[] = {0x02, 0x00, 0x07, '_','r','e','s','u','l','t', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the response packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        SrsFMLEStartPacket* request = new SrsFMLEStartPacket();
        request->command_name = "srs";
        request->transaction_id = 0.0;
        HELPER_EXPECT_SUCCESS(p.send_and_free_packet(request, 1));

        uint8_t bytes[] = {0x02, 0x00, 0x07, '_','r','e','s','u','l','t', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the response packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }
}

VOID TEST(ProtoStackTest, OnDecodeMessages4)
{
    srs_error_t err;

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x02, 0x00, 0x07, 'c','o','n','n','e','c','t', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the request packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x02, 0x00, 12, 'c','r','e','a','t','e','S','t','r','e','a','m', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the request packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x02, 0x00, 4, 'p','l','a','y', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the request packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x02, 0x00, 5, 'p','a','u','s','e', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the request packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x02, 0x00, 13, 'r','e','l','e','a','s','e','S','t','r','e','a','m', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the request packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x02, 0x00, 9, 'F','C','P','u','b','l','i','s','h', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the request packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x02, 0x00, 7, 'p','u','b','l','i','s','h', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the request packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x02, 0x00, 11, 'F','C','U','n','p','u','b','l','i','s','h', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the request packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x02, 0x00, 13, '@','s','e','t','D','a','t','a','F','r','a','m','e', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the request packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x02, 0x00, 10, 'o','n','M','e','t','a','D','a','t','a', 03,0,0,9};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        HELPER_EXPECT_SUCCESS(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x02, 0x00, 22, 'o','n','S','r','s','B','a','n','d','C','h','e','c','k','F','i','n','i','s','h','e','d', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the request packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x02, 0x00, 21, 'o','n','S','r','s','B','a','n','d','C','h','e','c','k','P','l','a','y','i','n','g', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the request packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x02, 0x00, 24, 'o','n','S','r','s','B','a','n','d','C','h','e','c','k','P','u','b','l','i','s','h','i','n','g', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the request packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x02, 0x00, 31, 'o','n','S','r','s','B','a','n','d','C','h','e','c','k','S','t','a','r','t','i','n','g','P','l','a','y','B','y','t','e','s', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the request packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x02, 0x00, 34, 'o','n','S','r','s','B','a','n','d','C','h','e','c','k','S','t','a','r','t','i','n','g','P','u','b','l','i','s','h','B','y','t','e','s', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the request packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x02, 0x00, 28, 'o','n','S','r','s','B','a','n','d','C','h','e','c','k','S','t','a','r','t','P','l','a','y','B','y','t','e','s', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the request packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x02, 0x00, 31, 'o','n','S','r','s','B','a','n','d','C','h','e','c','k','S','t','a','r','t','P','u','b','l','i','s','h','B','y','t','e','s', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the request packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x02, 0x00, 30, 'o','n','S','r','s','B','a','n','d','C','h','e','c','k','S','t','o','p','p','e','d','P','l','a','y','B','y','t','e','s', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the request packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x02, 0x00, 27, 'o','n','S','r','s','B','a','n','d','C','h','e','c','k','S','t','o','p','P','l','a','y','B','y','t','e','s', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the request packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x02, 0x00, 30, 'o','n','S','r','s','B','a','n','d','C','h','e','c','k','S','t','o','p','P','u','b','l','i','s','h','B','y','t','e','s', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the request packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x02, 0x00, 33, 'o','n','S','r','s','B','a','n','d','C','h','e','c','k','S','t','o','p','p','e','d','P','u','b','l','i','s','h','B','y','t','e','s', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the request packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x02, 0x00, 17, 'f','i','n','a','l','C','l','i','e','n','t','P','a','c','k','e','t', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the request packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x02, 0x00, 11, 'c','l','o','s','e','S','t','r','e','a','m', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the request packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x02, 0x00, 3, 's','r','s', 0x00,0,0,0,0,0,0,0,0};
        SrsCommonMessage* msg = _create_amf0((char*)bytes, sizeof(bytes), 1);
        msg->header.message_type = RTMP_MSG_AMF0CommandMessage;
        SrsAutoFree(SrsCommonMessage, msg);

        SrsPacket* pkt;
        SrsAutoFree(SrsPacket, pkt);

        // Without enough data, it fail when decoding the request packet.
        HELPER_EXPECT_FAILED(p.decode_message(msg, &pkt));
    }
}

VOID TEST(ProtoStackTest, RecvMessage)
{
    srs_error_t err;

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x01, 0x00, 0x00};
        io.in_buffer.append((char*)bytes, sizeof(bytes));

        SrsCommonMessage* msg;
        SrsAutoFree(SrsCommonMessage, msg);
        HELPER_EXPECT_FAILED(p.recv_message(&msg));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x00, 0x00};
        io.in_buffer.append((char*)bytes, sizeof(bytes));

        SrsCommonMessage* msg;
        SrsAutoFree(SrsCommonMessage, msg);
        HELPER_EXPECT_FAILED(p.recv_message(&msg));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x00};
        io.in_buffer.append((char*)bytes, sizeof(bytes));

        SrsCommonMessage* msg;
        SrsAutoFree(SrsCommonMessage, msg);
        HELPER_EXPECT_FAILED(p.recv_message(&msg));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        SrsCommonMessage* msg;
        SrsAutoFree(SrsCommonMessage, msg);
        HELPER_EXPECT_FAILED(p.recv_message(&msg));
    }
}

VOID TEST(ProtoStackTest, RecvMessage2)
{
    srs_error_t err;

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x03, 0,0,0, 0,0,4, 0, 0,0,0,0, 1,2,3};
        io.in_buffer.append((char*)bytes, sizeof(bytes));

        SrsCommonMessage* msg;
        SrsAutoFree(SrsCommonMessage, msg);
        HELPER_EXPECT_FAILED(p.recv_message(&msg));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        p.in_chunk_size = 3;

        uint8_t bytes[] = {0x03, 0,0,0, 0,0,4, 0, 0,0,0,0, 1,2,3};
        io.in_buffer.append((char*)bytes, sizeof(bytes));

        SrsCommonMessage* msg;
        SrsAutoFree(SrsCommonMessage, msg);
        HELPER_EXPECT_FAILED(p.recv_message(&msg));

        uint8_t bytes2[] = {0x43, 0,0,0, 0,0,5, 0, 0,0,0,0, 1,2,3};
        io.in_buffer.append((char*)bytes2, sizeof(bytes2));
        HELPER_EXPECT_FAILED(p.recv_message(&msg));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x03};
        io.in_buffer.append((char*)bytes, sizeof(bytes));

        SrsCommonMessage* msg;
        SrsAutoFree(SrsCommonMessage, msg);
        HELPER_EXPECT_FAILED(p.recv_message(&msg));
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        uint8_t bytes[] = {0x43, 0,0,0, 0,0,0, 0};
        io.in_buffer.append((char*)bytes, sizeof(bytes));

        SrsCommonMessage* msg;
        SrsAutoFree(SrsCommonMessage, msg);
        HELPER_EXPECT_FAILED(p.recv_message(&msg));
    }
}

VOID TEST(ProtoStackTest, RecvMessage3)
{
    if (true) {
        SrsRequest req;
        req.ip = "10.11.12.13";

        SrsRequest* cp = req.copy();
        EXPECT_STREQ("10.11.12.13", cp->ip.c_str());
        srs_freep(cp);
    }

    if (true) {
        SrsRequest req;
        req.ip = "10.11.12.13";

        SrsAmf0Object* obj = SrsAmf0Any::object();
        obj->set("id", SrsAmf0Any::str("srs"));
        req.args = obj;

        SrsRequest* cp = req.copy();
        EXPECT_STREQ("10.11.12.13", cp->ip.c_str());

        SrsAmf0Object* cpa = dynamic_cast<SrsAmf0Object*>(cp->args);
        SrsAmf0Any* cps = cpa->ensure_property_string("id");
        EXPECT_STREQ("srs", cps->to_str().c_str());
        srs_freep(cp);
    }

    if (true) {
        SrsRequest req;
        EXPECT_STREQ("//", req.get_stream_url().c_str());
    }

    if (true) {
        SrsRequest req;
        EXPECT_STREQ("", req.schema.c_str());

        req.as_http();
        EXPECT_STREQ("http", req.schema.c_str());
    }

    if (true) {
        SrsResponse res;
        EXPECT_EQ(1, res.stream_id);
    }

    if (true) {
        EXPECT_STREQ("Play", srs_client_type_string(SrsRtmpConnPlay).c_str());
        EXPECT_STREQ("flash-publish", srs_client_type_string(SrsRtmpConnFlashPublish).c_str());
        EXPECT_STREQ("fmle-publish", srs_client_type_string(SrsRtmpConnFMLEPublish).c_str());
        EXPECT_STREQ("haivision-publish", srs_client_type_string(SrsRtmpConnHaivisionPublish).c_str());
        EXPECT_STREQ("Unknown", srs_client_type_string(SrsRtmpConnType(0x0f)).c_str());

        EXPECT_TRUE(srs_client_type_is_publish(SrsRtmpConnFlashPublish));
        EXPECT_TRUE(srs_client_type_is_publish(SrsRtmpConnFMLEPublish));
        EXPECT_TRUE(srs_client_type_is_publish(SrsRtmpConnHaivisionPublish));
        EXPECT_FALSE(srs_client_type_is_publish(SrsRtmpConnPlay));
    }
}

VOID TEST(ProtoStackTest, RecvMessage4)
{
    srs_error_t err;

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        SrsSetChunkSizePacket* pkt = new SrsSetChunkSizePacket();
        pkt->chunk_size = 256;
        HELPER_EXPECT_SUCCESS(p.send_and_free_packet(pkt, 0));

        io.in_buffer.append(&io.out_buffer);

        SrsCommonMessage* msg;
        SrsAutoFree(SrsCommonMessage, msg);
        HELPER_EXPECT_SUCCESS(p.recv_message(&msg));

        EXPECT_EQ(256, p.out_chunk_size);
    }

    if (true) {
        MockBufferIO io;
        SrsProtocol p(&io);

        SrsUserControlPacket* pkt = new SrsUserControlPacket();
        pkt->event_type = SrcPCUCSetBufferLength;
        pkt->extra_data = 256;
        HELPER_EXPECT_SUCCESS(p.send_and_free_packet(pkt, 0));

        io.in_buffer.append(&io.out_buffer);

        SrsCommonMessage* msg;
        SrsAutoFree(SrsCommonMessage, msg);
        HELPER_EXPECT_SUCCESS(p.recv_message(&msg));

        EXPECT_EQ(256, p.in_buffer_length);
    }
}

VOID TEST(ProtoStackTest, HandshakeC0C1)
{
    srs_error_t err;

    // Fail for empty io.
    if (true) {
        MockBufferIO io;
        SrsHandshakeBytes hs;
        HELPER_EXPECT_FAILED(hs.read_c0c1(&io));
    }

    // It's normal c0c1, so it should be ok.
    if (true) {
        uint8_t buf[1537];
        HELPER_ARRAY_INIT(buf, 1537, 0x00);

        MockBufferIO io;
        io.append(buf, sizeof(buf));

        SrsHandshakeBytes hs;
        HELPER_EXPECT_SUCCESS(hs.read_c0c1(&io));
    }

    // It's extended c0c1 prefixed with ip, which should be ok.
    if (true) {
        uint8_t buf[1537 + 7] = {
            0xF3, 0x00, 0x04,
            0x01, 0x02, 0x03, 0x04,
        };
        HELPER_ARRAY_INIT(buf+7, 1537, 0x00);

        MockBufferIO io;
        io.append(buf, sizeof(buf));

        SrsHandshakeBytes hs;
        HELPER_EXPECT_SUCCESS(hs.read_c0c1(&io));
        EXPECT_EQ(0x01020304, hs.proxy_real_ip);
    }

    // It seems a normal c0c1, but it's extended, so it fail.
    if (true) {
        uint8_t buf[1537] = {
            0xF3, 0x04, 0x01,
            0x01, 0x02, 0x03, 0x04,
        };

        MockBufferIO io;
        io.append(buf, sizeof(buf));

        SrsHandshakeBytes hs;
        HELPER_EXPECT_FAILED(hs.read_c0c1(&io));
    }

    // For extended c0c1, it fail for not enough bytes.
    if (true) {
        uint8_t buf[7 + 1537 - 1] = {
            0xF3, 0x00, 0x04,
            0x01, 0x02, 0x03, 0x04,
        };

        MockBufferIO io;
        io.append(buf, sizeof(buf));

        SrsHandshakeBytes hs;
        HELPER_EXPECT_FAILED(hs.read_c0c1(&io));
    }

    // Ignore when c0c1 exists.
    if (true) {
        uint8_t buf[1537];
        HELPER_ARRAY_INIT(buf, 1537, 0x00);

        MockBufferIO io;
        io.append(buf, sizeof(buf));

        SrsHandshakeBytes hs;
        HELPER_EXPECT_SUCCESS(hs.read_c0c1(&io));

        io.append(buf, sizeof(buf));
        HELPER_EXPECT_SUCCESS(hs.read_c0c1(&io));
        EXPECT_EQ(1537, io.length());
    }
}

VOID TEST(ProtoStackTest, HandshakeS0S1S2)
{
    srs_error_t err;

    // It should be ok for normal s0s1s2.
    if (true) {
        uint8_t buf[3073];
        HELPER_ARRAY_INIT(buf, 3073, 0x00);

        MockBufferIO io;
        io.append(buf, sizeof(buf));

        SrsHandshakeBytes hs;
        HELPER_EXPECT_SUCCESS(hs.read_s0s1s2(&io));
    }

    // Fail for not enough data.
    if (true) {
        uint8_t buf[3073-1];
        HELPER_ARRAY_INIT(buf, 3073-1, 0x00);

        MockBufferIO io;
        io.append(buf, sizeof(buf));

        SrsHandshakeBytes hs;
        HELPER_EXPECT_FAILED(hs.read_s0s1s2(&io));
    }

    // Ignore for s0s1s2 exists.
    if (true) {
        uint8_t buf[3073];
        HELPER_ARRAY_INIT(buf, 3073, 0x00);

        MockBufferIO io;
        io.append(buf, sizeof(buf));

        SrsHandshakeBytes hs;
        HELPER_EXPECT_SUCCESS(hs.read_s0s1s2(&io));

        io.append(buf, sizeof(buf));
        HELPER_EXPECT_SUCCESS(hs.read_s0s1s2(&io));
        EXPECT_EQ(3073, io.length());
    }
}

VOID TEST(ProtoStackTest, HandshakeC2)
{
    srs_error_t err;

    // It should be ok for normal c2.
    if (true) {
        uint8_t buf[1536];
        HELPER_ARRAY_INIT(buf, 1536, 0x00);

        MockBufferIO io;
        io.append(buf, sizeof(buf));

        SrsHandshakeBytes hs;
        HELPER_EXPECT_SUCCESS(hs.read_c2(&io));
    }

    // Fail for not enough bytes.
    if (true) {
        uint8_t buf[1536-1];
        HELPER_ARRAY_INIT(buf, 1536-1, 0x00);

        MockBufferIO io;
        io.append(buf, sizeof(buf));

        SrsHandshakeBytes hs;
        HELPER_EXPECT_FAILED(hs.read_c2(&io));
    }

    // Ignore when c2 exists.
    if (true) {
        uint8_t buf[1536];
        HELPER_ARRAY_INIT(buf, 1536, 0x00);

        MockBufferIO io;
        io.append(buf, sizeof(buf));

        SrsHandshakeBytes hs;
        HELPER_EXPECT_SUCCESS(hs.read_c2(&io));

        io.append(buf, sizeof(buf));
        HELPER_EXPECT_SUCCESS(hs.read_c2(&io));
        EXPECT_EQ(1536, io.length());
    }
}

VOID TEST(ProtoStackTest, ServerInfo)
{
    SrsServerInfo si;
    EXPECT_EQ(0, si.pid);
    EXPECT_EQ(0, si.cid);
    EXPECT_EQ(0, si.major);
    EXPECT_EQ(0, si.minor);
    EXPECT_EQ(0, si.revision);
    EXPECT_EQ(0, si.build);
}

VOID TEST(ProtoStackTest, ClientCommandMessage)
{
    srs_error_t err;

    // ConnectApp.
    if (true) {
        MockBufferIO io;

        if (true) {
            SrsConnectAppResPacket* res = new SrsConnectAppResPacket();

            SrsAmf0EcmaArray* data = SrsAmf0Any::ecma_array();
            res->info->set("data", data);

            data->set("srs_server_ip", SrsAmf0Any::str("1.2.3.4"));
            data->set("srs_server", SrsAmf0Any::str("srs"));
            data->set("srs_id", SrsAmf0Any::number(100));
            data->set("srs_pid", SrsAmf0Any::number(200));
            data->set("srs_version", SrsAmf0Any::str("3.4.5.678"));

            MockBufferIO tio;
            SrsProtocol p(&tio);
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(res, 0));

            io.in_buffer.append(&tio.out_buffer);
        }

        SrsRequest req;
        SrsRtmpClient r(&io);

        SrsServerInfo si;
        HELPER_EXPECT_SUCCESS(r.connect_app("live", "rtmp://127.0.0.1/live", &req, true, &si));
        EXPECT_STREQ("1.2.3.4", si.ip.c_str());
        EXPECT_STREQ("srs", si.sig.c_str());
        EXPECT_EQ(100, si.cid);
        EXPECT_EQ(200, si.pid);
        EXPECT_EQ(3, si.major);
        EXPECT_EQ(4, si.minor);
        EXPECT_EQ(5, si.revision);
        EXPECT_EQ(678, si.build);
    }

    // CreateStream.
    if (true) {
        MockBufferIO io;

        if (true) {
            SrsCreateStreamResPacket* res = new SrsCreateStreamResPacket(2.0, 3.0);

            MockBufferIO tio;
            SrsProtocol p(&tio);
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(res, 0));

            io.in_buffer.append(&tio.out_buffer);
        }

        SrsRtmpClient r(&io);

        int stream_id = 0;
        HELPER_EXPECT_SUCCESS(r.create_stream(stream_id));
        EXPECT_EQ(3, stream_id);
    }

    // Play.
    if (true) {
        MockBufferIO io;
        SrsRtmpClient r(&io);
        HELPER_EXPECT_SUCCESS(r.play("livestream", 1, 128));
        EXPECT_TRUE(io.out_length() > 0);
    }

    // Publish.
    if (true) {
        MockBufferIO io;
        SrsRtmpClient r(&io);
        HELPER_EXPECT_SUCCESS(r.publish("livestream", 1, 128));
        EXPECT_TRUE(io.out_length() > 0);
    }

    // FMLE publish.
    if (true) {
        MockBufferIO io;

        if (true) {
            SrsCreateStreamResPacket* res = new SrsCreateStreamResPacket(4.0, 3.0);

            MockBufferIO tio;
            SrsProtocol p(&tio);
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(res, 0));

            io.in_buffer.append(&tio.out_buffer);
        }

        SrsRtmpClient r(&io);

        int stream_id = 0;
        HELPER_EXPECT_SUCCESS(r.fmle_publish("livestream", stream_id));
        EXPECT_EQ(3, stream_id);
    }
}

VOID TEST(ProtoStackTest, ServerCommandMessage)
{
    srs_error_t err;

    // ConnectApp.
    if (true) {
        MockBufferIO io;

        if (true) {
            SrsConnectAppPacket* res = new SrsConnectAppPacket();
            res->command_object->set("tcUrl", SrsAmf0Any::str("rtmp://127.0.0.1/live"));

            MockBufferIO tio;
            SrsProtocol p(&tio);
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(res, 0));

            io.in_buffer.append(&tio.out_buffer);
        }

        SrsRtmpServer r(&io);

        SrsRequest req;
        HELPER_EXPECT_SUCCESS(r.connect_app(&req));
        EXPECT_STREQ("rtmp", req.schema.c_str());
        EXPECT_STREQ("127.0.0.1", req.host.c_str());
        EXPECT_STREQ("127.0.0.1", req.vhost.c_str());
        EXPECT_STREQ("live", req.app.c_str());
    }

    // Window ACK size.
    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);
        HELPER_EXPECT_SUCCESS(r.set_window_ack_size(1024));

        if (true) {
            MockBufferIO tio;
            tio.in_buffer.append(&io.out_buffer);

            SrsProtocol p(&tio);

            SrsCommonMessage* msg = NULL;
            SrsSetWindowAckSizePacket* pkt = NULL;
            HELPER_ASSERT_SUCCESS(p.expect_message(&msg, &pkt));
            EXPECT_EQ(1024, pkt->ackowledgement_window_size);

            srs_freep(msg);
            srs_freep(pkt);
        }
    }

    // Response ConnectApp.
    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);

        SrsRequest req;
        req.objectEncoding = 3.0;

        const char* ip = "1.2.3.4";
        HELPER_EXPECT_SUCCESS(r.response_connect_app(&req, ip));

        if (true) {
            MockBufferIO tio;
            tio.in_buffer.append(&io.out_buffer);

            SrsProtocol p(&tio);

            // In order to receive and decode the response,
            // we have to send out a ConnectApp request.
            if (true) {
                SrsConnectAppPacket* pkt = new SrsConnectAppPacket();
                HELPER_EXPECT_SUCCESS(p.send_and_free_packet(pkt, 0));
            }

            SrsCommonMessage* msg = NULL;
            SrsConnectAppResPacket* pkt = NULL;
            HELPER_ASSERT_SUCCESS(p.expect_message(&msg, &pkt));

            SrsAmf0Any* prop = pkt->info->get_property("objectEncoding");
            ASSERT_TRUE(prop && prop->is_number());
            EXPECT_EQ(3.0, prop->to_number());

            prop = pkt->info->get_property("data");
            ASSERT_TRUE(prop && prop->is_ecma_array());

            SrsAmf0EcmaArray* arr = prop->to_ecma_array();
            prop = arr->get_property("srs_server_ip");
            ASSERT_TRUE(prop && prop->is_string());
            EXPECT_STREQ("1.2.3.4", prop->to_str().c_str());

            srs_freep(msg);
            srs_freep(pkt);
        }
    }

    // Response ConnectApp rejected.
    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);

        const char* desc = "Rejected";
        r.response_connect_reject(NULL, desc);

        if (true) {
            MockBufferIO tio;
            tio.in_buffer.append(&io.out_buffer);

            SrsProtocol p(&tio);

            SrsCommonMessage* msg = NULL;
            SrsCallPacket* pkt = NULL;
            HELPER_ASSERT_SUCCESS(p.expect_message(&msg, &pkt));

            SrsAmf0Any* prop = pkt->arguments;
            ASSERT_TRUE(prop && prop->is_object());

            prop = prop->to_object()->get_property(StatusDescription);
            ASSERT_TRUE(prop && prop->is_string());
            EXPECT_STREQ(desc, prop->to_str().c_str());

            srs_freep(msg);
            srs_freep(pkt);
        }
    }

    // Response OnBWDone
    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);
        HELPER_EXPECT_SUCCESS(r.on_bw_done());
        EXPECT_TRUE(io.out_length() > 0);
    }

    // Set peer chunk size.
    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);
        HELPER_EXPECT_SUCCESS(r.set_chunk_size(1024));

        if (true) {
            MockBufferIO tio;
            tio.in_buffer.append(&io.out_buffer);

            SrsProtocol p(&tio);

            SrsCommonMessage* msg = NULL;
            HELPER_EXPECT_SUCCESS(p.recv_message(&msg));
            srs_freep(msg);

            EXPECT_EQ(1024, p.in_chunk_size);
        }
    }
}

VOID TEST(ProtoStackTest, ServerRedirect)
{
    srs_error_t err;

    // Redirect without response.
    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);

        SrsRequest req;
        req.app = "live";
        req.stream = "livestream";

        string host = "target.net";
        int port = 8888;
        bool accepted = false;
        HELPER_EXPECT_SUCCESS(r.redirect(&req, host, port, accepted));

        if (true) {
            MockBufferIO tio;
            tio.in_buffer.append(&io.out_buffer);

            SrsProtocol p(&tio);

            SrsCommonMessage* msg = NULL;
            SrsCallPacket* pkt = NULL;
            HELPER_ASSERT_SUCCESS(p.expect_message(&msg, &pkt));

            SrsAmf0Any* prop = pkt->arguments;
            ASSERT_TRUE(prop && prop->is_object());

            prop = prop->to_object()->get_property("ex");
            ASSERT_TRUE(prop && prop->is_object());
            SrsAmf0Object* ex = prop->to_object();

            prop = ex->get_property("code");
            ASSERT_TRUE(prop && prop->is_number());
            EXPECT_EQ(302, prop->to_number());

            prop = ex->get_property("redirect");
            ASSERT_TRUE(prop && prop->is_string());
            EXPECT_STREQ("rtmp://target.net:8888/live/livestream", prop->to_str().c_str());

            srs_freep(msg);
            srs_freep(pkt);
        }
    }

    // Redirect with response.
    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);

        if (true) {
            MockBufferIO tio;
            SrsProtocol p(&tio);

            SrsCallPacket* call = new SrsCallPacket();
            call->command_name = "redirected";
            call->command_object = SrsAmf0Any::object();
            call->arguments = SrsAmf0Any::str("OK");
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(call, 0));

            io.in_buffer.append(&tio.out_buffer);
        }

        SrsRequest req;
        req.app = "live";
        req.stream = "livestream";

        string host = "target.net";
        int port = 8888;
        bool accepted = false;
        HELPER_EXPECT_SUCCESS(r.redirect(&req, host, port, accepted));
        EXPECT_TRUE(accepted);

        if (true) {
            MockBufferIO tio;
            tio.in_buffer.append(&io.out_buffer);

            SrsProtocol p(&tio);

            SrsCommonMessage* msg = NULL;
            SrsCallPacket* pkt = NULL;
            HELPER_ASSERT_SUCCESS(p.expect_message(&msg, &pkt));

            SrsAmf0Any* prop = pkt->arguments;
            ASSERT_TRUE(prop && prop->is_object());

            prop = prop->to_object()->get_property("ex");
            ASSERT_TRUE(prop && prop->is_object());
            SrsAmf0Object* ex = prop->to_object();

            prop = ex->get_property("code");
            ASSERT_TRUE(prop && prop->is_number());
            EXPECT_EQ(302, prop->to_number());

            prop = ex->get_property("redirect");
            ASSERT_TRUE(prop && prop->is_string());
            EXPECT_STREQ("rtmp://target.net:8888/live/livestream", prop->to_str().c_str());

            srs_freep(msg);
            srs_freep(pkt);
        }
    }
}

VOID TEST(ProtoStackTest, ServerIdentify)
{
    srs_error_t err;

    // Identify failed.
    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);

        string stream_name;
        SrsRtmpConnType tp;
        srs_utime_t duration = 0;
        HELPER_EXPECT_FAILED(r.identify_client(1, tp, stream_name, duration));
    }

    // Identify by CreateStream, Play.
    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);

        if (true) {
            MockBufferIO tio;
            SrsProtocol p(&tio);

            SrsCreateStreamPacket* call = new SrsCreateStreamPacket();
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(call, 0));

            SrsPlayPacket* play = new SrsPlayPacket();
            play->stream_name = "livestream";
            play->duration = 100;
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(play, 0));

            io.in_buffer.append(&tio.out_buffer);
        }

        string stream_name;
        SrsRtmpConnType tp;
        srs_utime_t duration = 0;
        HELPER_EXPECT_SUCCESS(r.identify_client(1, tp, stream_name, duration));
        EXPECT_EQ(SrsRtmpConnPlay, tp);
        EXPECT_STREQ("livestream", stream_name.c_str());
        EXPECT_EQ(100000, duration);
    }

    // Identify by CreateStream, CreateStream, CreateStream, Play.
    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);

        if (true) {
            MockBufferIO tio;
            SrsProtocol p(&tio);

            for (int i = 0; i < 3; i++) {
                SrsCreateStreamPacket* call = new SrsCreateStreamPacket();
                HELPER_EXPECT_SUCCESS(p.send_and_free_packet(call, 0));
            }

            SrsPlayPacket* play = new SrsPlayPacket();
            play->stream_name = "livestream";
            play->duration = 100;
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(play, 0));

            io.in_buffer.append(&tio.out_buffer);
        }

        string stream_name;
        SrsRtmpConnType tp;
        srs_utime_t duration = 0;
        HELPER_EXPECT_SUCCESS(r.identify_client(1, tp, stream_name, duration));
        EXPECT_EQ(SrsRtmpConnPlay, tp);
        EXPECT_STREQ("livestream", stream_name.c_str());
        EXPECT_EQ(100000, duration);
    }

    // Identify by CreateStream, Publish.
    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);

        if (true) {
            MockBufferIO tio;
            SrsProtocol p(&tio);

            SrsCreateStreamPacket* call = new SrsCreateStreamPacket();
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(call, 0));

            SrsPublishPacket* publish = new SrsPublishPacket();
            publish->stream_name = "livestream";
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(publish, 0));

            io.in_buffer.append(&tio.out_buffer);
        }

        string stream_name;
        SrsRtmpConnType tp;
        srs_utime_t duration = 0;
        HELPER_EXPECT_SUCCESS(r.identify_client(1, tp, stream_name, duration));
        EXPECT_EQ(SrsRtmpConnFlashPublish, tp);
        EXPECT_STREQ("livestream", stream_name.c_str());
    }

    // Identify by CreateStream, FMLEStart.
    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);

        if (true) {
            MockBufferIO tio;
            SrsProtocol p(&tio);

            SrsCreateStreamPacket* call = new SrsCreateStreamPacket();
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(call, 0));

            SrsFMLEStartPacket* fmle = new SrsFMLEStartPacket();
            fmle->stream_name = "livestream";
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(fmle, 0));

            io.in_buffer.append(&tio.out_buffer);
        }

        string stream_name;
        SrsRtmpConnType tp;
        srs_utime_t duration = 0;
        HELPER_EXPECT_SUCCESS(r.identify_client(1, tp, stream_name, duration));
        EXPECT_EQ(SrsRtmpConnHaivisionPublish, tp);
        EXPECT_STREQ("livestream", stream_name.c_str());
    }

    // Identify by Play.
    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);

        if (true) {
            MockBufferIO tio;
            SrsProtocol p(&tio);

            SrsPlayPacket* play = new SrsPlayPacket();
            play->stream_name = "livestream";
            play->duration = 100;
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(play, 0));

            io.in_buffer.append(&tio.out_buffer);
        }

        string stream_name;
        SrsRtmpConnType tp;
        srs_utime_t duration = 0;
        HELPER_EXPECT_SUCCESS(r.identify_client(1, tp, stream_name, duration));
        EXPECT_EQ(SrsRtmpConnPlay, tp);
        EXPECT_STREQ("livestream", stream_name.c_str());
        EXPECT_EQ(100000, duration);
    }

    // Identify by FMLEStart.
    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);

        if (true) {
            MockBufferIO tio;
            SrsProtocol p(&tio);

            SrsFMLEStartPacket* fmle = new SrsFMLEStartPacket();
            fmle->stream_name = "livestream";
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(fmle, 0));

            io.in_buffer.append(&tio.out_buffer);
        }

        string stream_name;
        SrsRtmpConnType tp;
        srs_utime_t duration = 0;
        HELPER_EXPECT_SUCCESS(r.identify_client(1, tp, stream_name, duration));
        EXPECT_EQ(SrsRtmpConnFMLEPublish, tp);
        EXPECT_STREQ("livestream", stream_name.c_str());
    }
}

VOID TEST(ProtoStackTest, ServerRecursiveDepth)
{
    srs_error_t err;

    // For N*CreateStream and N>3, it should fail.
    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);

        if (true) {
            MockBufferIO tio;
            SrsProtocol p(&tio);

            for (int i = 0; i < 4; i++) {
                SrsCreateStreamPacket* call = new SrsCreateStreamPacket();
                HELPER_EXPECT_SUCCESS(p.send_and_free_packet(call, 0));
            }

            io.in_buffer.append(&tio.out_buffer);
        }

        string stream_name;
        SrsRtmpConnType tp;
        srs_utime_t duration = 0;
        err = r.identify_client(1, tp, stream_name, duration);
        EXPECT_EQ(ERROR_RTMP_CREATE_STREAM_DEPTH, srs_error_code(err));
        srs_freep(err);
    }

    // If CreateStream N times and N<=3, it should be ok.
    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);

        if (true) {
            MockBufferIO tio;
            SrsProtocol p(&tio);

            for (int i = 0; i < 3; i++) {
                SrsCreateStreamPacket* call = new SrsCreateStreamPacket();
                HELPER_EXPECT_SUCCESS(p.send_and_free_packet(call, 0));
            }

            io.in_buffer.append(&tio.out_buffer);
        }

        string stream_name;
        SrsRtmpConnType tp;
        srs_utime_t duration = 0;
        err = r.identify_client(1, tp, stream_name, duration);
        EXPECT_NE(ERROR_RTMP_CREATE_STREAM_DEPTH, srs_error_code(err));
        srs_freep(err);
    }
}

VOID TEST(ProtoStackTest, ServerResponseCommands)
{
    srs_error_t err;

    // Start play.
    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);
        HELPER_EXPECT_SUCCESS(r.start_play(1));

        if (true) {
            MockBufferIO tio;
            tio.in_buffer.append(&io.out_buffer);

            SrsProtocol p(&tio);

            SrsCommonMessage* msg = NULL;
            SrsCallPacket* pkt = NULL;

            // onStatus(NetStream.Play.Reset)
            HELPER_ASSERT_SUCCESS(p.expect_message(&msg, &pkt));
            srs_freep(msg);
            srs_freep(pkt);

            // onStatus(NetStream.Play.Start)
            HELPER_ASSERT_SUCCESS(p.expect_message(&msg, &pkt));
            srs_freep(msg);
            srs_freep(pkt);

            // onStatus(NetStream.Data.Start)
            SrsPacket* bpkt = NULL;
            HELPER_ASSERT_SUCCESS(p.expect_message(&msg, &bpkt));
            srs_freep(msg);
            srs_freep(bpkt);
        }
    }

    // Pause true.
    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);
        HELPER_EXPECT_SUCCESS(r.on_play_client_pause(1, true));

        if (true) {
            MockBufferIO tio;
            tio.in_buffer.append(&io.out_buffer);

            SrsProtocol p(&tio);

            SrsCommonMessage* msg = NULL;
            SrsCallPacket* pkt = NULL;

            // onStatus(NetStream.Pause.Notify)
            HELPER_ASSERT_SUCCESS(p.expect_message(&msg, &pkt));
            srs_freep(msg);
            srs_freep(pkt);
        }
    }

    // Pause false.
    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);
        HELPER_EXPECT_SUCCESS(r.on_play_client_pause(1, false));

        if (true) {
            MockBufferIO tio;
            tio.in_buffer.append(&io.out_buffer);

            SrsProtocol p(&tio);

            SrsCommonMessage* msg = NULL;
            SrsCallPacket* pkt = NULL;

            // onStatus(NetStream.Pause.Notify)
            HELPER_ASSERT_SUCCESS(p.expect_message(&msg, &pkt));
            srs_freep(msg);
            srs_freep(pkt);
        }
    }

    // Identify by Call,Data,ChunkSize Play.
    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);

        if (true) {
            MockBufferIO tio;
            SrsProtocol p(&tio);

            SrsCallPacket* call = new SrsCallPacket();
            call->command_name = "_checkbw";
            call->transaction_id = 5.0;
            call->command_object = SrsAmf0Any::object();
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(call, 0));

            SrsOnStatusDataPacket* data = new SrsOnStatusDataPacket();
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(data, 0));

            SrsSetChunkSizePacket* scs = new SrsSetChunkSizePacket();
            scs->chunk_size = 1024;
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(scs, 0));

            SrsPlayPacket* play = new SrsPlayPacket();
            play->stream_name = "livestream";
            play->duration = 100;
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(play, 0));

            io.in_buffer.append(&tio.out_buffer);
        }

        string stream_name;
        SrsRtmpConnType tp;
        srs_utime_t duration = 0;
        HELPER_EXPECT_SUCCESS(r.identify_client(1, tp, stream_name, duration));
        EXPECT_EQ(SrsRtmpConnPlay, tp);
        EXPECT_STREQ("livestream", stream_name.c_str());
        EXPECT_EQ(100000, duration);
    }

    // Identify by invalid call.
    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);

        if (true) {
            MockBufferIO tio;
            SrsProtocol p(&tio);

            SrsCallPacket* call = new SrsCallPacket();
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(call, 0));

            io.in_buffer.append(&tio.out_buffer);
        }

        string stream_name;
        SrsRtmpConnType tp;
        srs_utime_t duration = 0;
        HELPER_EXPECT_FAILED(r.identify_client(1, tp, stream_name, duration));
    }
}

