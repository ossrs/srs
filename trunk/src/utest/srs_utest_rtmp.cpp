/*
The MIT License (MIT)

Copyright (c) 2013-2020 Winlin

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
#include <srs_utest_rtmp.hpp>

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
#include <srs_kernel_codec.hpp>

#define SRS_DEFAULT_RECV_BUFFER_SIZE 131072

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

VOID TEST(ProtocolRTMPTest, PacketEncode)
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

VOID TEST(ProtocolRTMPTest, ManualFlush)
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
        SrsRtmpServer p(&io);

        // Always response ACK message.
        HELPER_EXPECT_SUCCESS(p.set_in_window_ack_size(1));

        p.set_auto_response(true);
        HELPER_EXPECT_SUCCESS(p.protocol->response_acknowledgement_message());
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

VOID TEST(ProtocolRTMPTest, SendPacketsError)
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

VOID TEST(ProtocolRTMPTest, SendHugePacket)
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

VOID TEST(ProtocolRTMPTest, SendZeroMessages)
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

VOID TEST(ProtocolRTMPTest, HugeMessages)
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

VOID TEST(ProtocolRTMPTest, DecodeMessages)
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

VOID TEST(ProtocolRTMPTest, OnDecodeMessages)
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

VOID TEST(ProtocolRTMPTest, OnDecodeMessages2)
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

VOID TEST(ProtocolRTMPTest, OnDecodeMessages3)
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

VOID TEST(ProtocolRTMPTest, OnDecodeMessages4)
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

VOID TEST(ProtocolRTMPTest, RecvMessage)
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

VOID TEST(ProtocolRTMPTest, RecvMessage2)
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

VOID TEST(ProtocolRTMPTest, RecvMessage3)
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

VOID TEST(ProtocolRTMPTest, RecvMessage4)
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

VOID TEST(ProtocolRTMPTest, HandshakeC0C1)
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
        EXPECT_EQ((uint32_t)0x01020304, (uint32_t)hs.proxy_real_ip);
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

        SrsRtmpServer r(&io);
        HELPER_EXPECT_SUCCESS(r.hs_bytes->read_c0c1(&io));
        EXPECT_EQ((uint32_t)0x01020304, (uint32_t)r.proxy_real_ip());
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

VOID TEST(ProtocolRTMPTest, HandshakeS0S1S2)
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

VOID TEST(ProtocolRTMPTest, HandshakeC2)
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

VOID TEST(ProtocolRTMPTest, ServerInfo)
{
    SrsServerInfo si;
    EXPECT_EQ(0, si.pid);
    EXPECT_EQ(0, si.cid);
    EXPECT_EQ(0, si.major);
    EXPECT_EQ(0, si.minor);
    EXPECT_EQ(0, si.revision);
    EXPECT_EQ(0, si.build);
}

VOID TEST(ProtocolRTMPTest, ClientCommandMessage)
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

VOID TEST(ProtocolRTMPTest, ServerCommandMessage)
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

VOID TEST(ProtocolRTMPTest, ServerRedirect)
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
        string rurl = srs_generate_rtmp_url(host, port, req.host, req.vhost, req.app, req.stream, req.param);
        HELPER_EXPECT_SUCCESS(r.redirect(&req, rurl, accepted));

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
            // The recirect is tcUrl, not RTMP URL.
            // https://github.com/ossrs/srs/issues/1575#issuecomment-574995475
            EXPECT_STREQ("rtmp://target.net:8888/live", prop->to_str().c_str());

            prop = ex->get_property("redirect2");
            ASSERT_TRUE(prop && prop->is_string());
            // The recirect2 is RTMP URL.
            // https://github.com/ossrs/srs/issues/1575#issuecomment-574999798
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
        string rurl = srs_generate_rtmp_url(host, port, req.host, req.vhost, req.app, req.stream, req.param);
        HELPER_EXPECT_SUCCESS(r.redirect(&req, rurl, accepted));
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
            // The recirect is tcUrl, not RTMP URL.
            // https://github.com/ossrs/srs/issues/1575#issuecomment-574995475
            EXPECT_STREQ("rtmp://target.net:8888/live", prop->to_str().c_str());

            prop = ex->get_property("redirect2");
            ASSERT_TRUE(prop && prop->is_string());
            // The recirect2 is RTMP URL.
            // https://github.com/ossrs/srs/issues/1575#issuecomment-574999798
            EXPECT_STREQ("rtmp://target.net:8888/live/livestream", prop->to_str().c_str());

            srs_freep(msg);
            srs_freep(pkt);
        }
    }
}

VOID TEST(ProtocolRTMPTest, ServerIdentify)
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

VOID TEST(ProtocolRTMPTest, ServerFMLEStart)
{
    srs_error_t err;

    // FMLE start.
    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);

        MockBufferIO tio;
        SrsProtocol p(&tio);
        if (true) {
            SrsFMLEStartPacket* fmle = new SrsFMLEStartPacket();
            fmle->stream_name = "livestream";
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(fmle, 0));

            SrsCreateStreamPacket* cs = new SrsCreateStreamPacket();
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(cs, 0));

            SrsPublishPacket* publish = new SrsPublishPacket();
            publish->stream_name = "livestream";
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(publish, 0));

            io.in_buffer.append(&tio.out_buffer);
        }

        HELPER_EXPECT_SUCCESS(r.start_fmle_publish(1));

        if (true) {
            tio.in_buffer.append(&io.out_buffer);

            // FCPublish response
            if (true) {
                SrsCommonMessage* msg = NULL;
                SrsFMLEStartResPacket* pkt = NULL;
                HELPER_ASSERT_SUCCESS(p.expect_message(&msg, &pkt));
                srs_freep(msg);
                srs_freep(pkt);
            }

            // createStream response
            if (true) {
                SrsCommonMessage* msg = NULL;
                SrsCreateStreamResPacket* pkt = NULL;
                HELPER_ASSERT_SUCCESS(p.expect_message(&msg, &pkt));
                EXPECT_EQ(1, pkt->stream_id);
                srs_freep(msg);
                srs_freep(pkt);
            }

            // publish response onFCPublish(NetStream.Publish.Start)
            SrsCommonMessage* msg = NULL;
            SrsCallPacket* pkt = NULL;
            HELPER_ASSERT_SUCCESS(p.expect_message(&msg, &pkt));
            srs_freep(msg);
            srs_freep(pkt);

            // publish response onStatus(NetStream.Publish.Start)
            HELPER_ASSERT_SUCCESS(p.expect_message(&msg, &pkt));
            srs_freep(msg);
            srs_freep(pkt);
        }
    }
}

VOID TEST(ProtocolRTMPTest, ServerHaivisionPublish)
{
    srs_error_t err;

    // FMLE start.
    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);

        MockBufferIO tio;
        SrsProtocol p(&tio);
        if (true) {
            SrsPublishPacket* publish = new SrsPublishPacket();
            publish->stream_name = "livestream";
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(publish, 0));

            io.in_buffer.append(&tio.out_buffer);
        }

        HELPER_EXPECT_SUCCESS(r.start_haivision_publish(1));

        if (true) {
            tio.in_buffer.append(&io.out_buffer);

            // publish response onFCPublish(NetStream.Publish.Start)
            SrsCommonMessage* msg = NULL;
            SrsCallPacket* pkt = NULL;
            HELPER_ASSERT_SUCCESS(p.expect_message(&msg, &pkt));
            srs_freep(msg);
            srs_freep(pkt);

            // publish response onStatus(NetStream.Publish.Start)
            HELPER_ASSERT_SUCCESS(p.expect_message(&msg, &pkt));
            srs_freep(msg);
            srs_freep(pkt);
        }
    }
}

VOID TEST(ProtocolRTMPTest, ServerFMLEUnpublish)
{
    srs_error_t err;

    // FMLE start.
    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);

        MockBufferIO tio;
        SrsProtocol p(&tio);
        if (true) {
            SrsFMLEStartPacket* fmle = new SrsFMLEStartPacket();
            fmle->transaction_id = 3.0;
            fmle->stream_name = "livestream";
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(fmle, 0));

            io.in_buffer.append(&tio.out_buffer);
        }

        HELPER_EXPECT_SUCCESS(r.fmle_unpublish(1, 3.0));

        if (true) {
            tio.in_buffer.append(&io.out_buffer);

            // publish response onFCUnpublish(NetStream.unpublish.Success)
            if (true) {
                SrsCommonMessage* msg = NULL;
                SrsCallPacket* pkt = NULL;
                HELPER_ASSERT_SUCCESS(p.expect_message(&msg, &pkt));
                srs_freep(msg);
                srs_freep(pkt);
            }

            // FCUnpublish response
            if (true) {
                SrsCommonMessage* msg = NULL;
                SrsFMLEStartResPacket* pkt = NULL;
                HELPER_ASSERT_SUCCESS(p.expect_message(&msg, &pkt));
                srs_freep(msg);
                srs_freep(pkt);
            }

            // publish response onStatus(NetStream.Unpublish.Success)
            SrsCommonMessage* msg = NULL;
            SrsCallPacket* pkt = NULL;
            HELPER_ASSERT_SUCCESS(p.expect_message(&msg, &pkt));
            srs_freep(msg);
            srs_freep(pkt);
        }
    }
}

VOID TEST(ProtocolRTMPTest, ServerFlashPublish)
{
    srs_error_t err;

    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);

        HELPER_EXPECT_SUCCESS(r.start_flash_publish(1));

        if (true) {
            MockBufferIO tio;
            SrsProtocol p(&tio);
            tio.in_buffer.append(&io.out_buffer);

            // publish response onStatus(NetStream.Publish.Start)
            SrsCommonMessage* msg = NULL;
            SrsCallPacket* pkt = NULL;
            HELPER_ASSERT_SUCCESS(p.expect_message(&msg, &pkt));
            srs_freep(msg);
            srs_freep(pkt);
        }
    }
}

VOID TEST(ProtocolRTMPTest, ServerRecursiveDepth)
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

VOID TEST(ProtocolRTMPTest, ServerResponseCommands)
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

VOID TEST(ProtocolRTMPTest, CoverAll)
{
    srs_error_t err;

    if (true) {
        MockBufferIO io;
        SrsRtmpClient r(&io);
        r.set_recv_timeout(100 * SRS_UTIME_MILLISECONDS);
        r.set_send_timeout(100 * SRS_UTIME_MILLISECONDS);
        EXPECT_EQ(0, r.get_recv_bytes());
        EXPECT_EQ(0, r.get_send_bytes());

        SrsCommonMessage* msg = NULL;
        HELPER_EXPECT_FAILED(r.recv_message(&msg));

        SrsCallPacket* pkt = new SrsCallPacket();
        HELPER_EXPECT_SUCCESS(r.send_and_free_packet(pkt, 0));
        EXPECT_TRUE(r.get_send_bytes() > 0);
    }

    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);
        r.set_recv_timeout(100 * SRS_UTIME_MILLISECONDS);
        EXPECT_EQ(100 * SRS_UTIME_MILLISECONDS, r.get_recv_timeout());

        r.set_send_timeout(100 * SRS_UTIME_MILLISECONDS);
        EXPECT_EQ(100 * SRS_UTIME_MILLISECONDS, r.get_send_timeout());

        r.set_recv_buffer(SRS_DEFAULT_RECV_BUFFER_SIZE + 10);
        EXPECT_EQ(SRS_DEFAULT_RECV_BUFFER_SIZE + 10, r.protocol->in_buffer->nb_buffer);

        EXPECT_EQ(0, r.get_recv_bytes());
        EXPECT_EQ(0, r.get_send_bytes());

        SrsCommonMessage* msg = NULL;
        HELPER_EXPECT_FAILED(r.recv_message(&msg));

        SrsCallPacket* pkt = new SrsCallPacket();
        HELPER_EXPECT_SUCCESS(r.send_and_free_packet(pkt, 0));
        EXPECT_TRUE(r.get_send_bytes() > 0);
    }

    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);
        HELPER_ASSERT_SUCCESS(r.set_peer_bandwidth(0, 0));
        EXPECT_TRUE(r.get_send_bytes() > 0);
    }

    if (true) {
        MockBufferIO io;
        SrsRtmpClient r(&io);
        HELPER_EXPECT_SUCCESS(r.play("livestream", 1, 1024));
        EXPECT_TRUE(io.out_length() > 0);
    }

    if (true) {
        MockBufferIO io;
        SrsRtmpClient r(&io);
        HELPER_EXPECT_SUCCESS(r.publish("livestream", 1, 1024));
        EXPECT_TRUE(io.out_length() > 0);
    }

    if (true) {
        MockBufferIO io;
        SrsRtmpClient r(&io);

        SrsAcknowledgementPacket* ack = new SrsAcknowledgementPacket();
        ack->sequence_number = 1024;
        HELPER_ASSERT_SUCCESS(r.send_and_free_packet(ack, 0));

        io.in_buffer.append(&io.out_buffer);
        SrsCommonMessage* msg = NULL;
        SrsAcknowledgementPacket* pkt = NULL;
        HELPER_ASSERT_SUCCESS(r.expect_message(&msg, &pkt));
        EXPECT_EQ(1024, (int)pkt->sequence_number);
        srs_freep(msg); srs_freep(pkt);
    }
}

VOID TEST(ProtocolRTMPTest, CoverBandwidth)
{
    if (true) {
        SrsBandwidthPacket p;

        p.set_command("onSrsBandCheckStartPlayBytes");
        EXPECT_TRUE(p.is_start_play());

        p.command_name = "onSrsBandCheckStartPlayBytes";
        EXPECT_TRUE(p.is_start_play());
    }

    if (true) {
        SrsBandwidthPacket* p = SrsBandwidthPacket::create_start_play();
        EXPECT_TRUE(p->is_start_play());
        srs_freep(p);
    }
    if (true) {
        SrsBandwidthPacket* p = SrsBandwidthPacket::create_starting_play();
        EXPECT_TRUE(p->is_starting_play());
        srs_freep(p);
    }
    if (true) {
        SrsBandwidthPacket* p = SrsBandwidthPacket::create_playing();
        srs_freep(p);
    }
    if (true) {
        SrsBandwidthPacket* p = SrsBandwidthPacket::create_stop_play();
        EXPECT_TRUE(p->is_stop_play());
        srs_freep(p);
    }
    if (true) {
        SrsBandwidthPacket* p = SrsBandwidthPacket::create_stopped_play();
        EXPECT_TRUE(p->is_stopped_play());
        srs_freep(p);
    }
    if (true) {
        SrsBandwidthPacket* p = SrsBandwidthPacket::create_start_publish();
        EXPECT_TRUE(p->is_start_publish());
        srs_freep(p);
    }
    if (true) {
        SrsBandwidthPacket* p = SrsBandwidthPacket::create_starting_publish();
        EXPECT_TRUE(p->is_starting_publish());
        srs_freep(p);
    }
    if (true) {
        SrsBandwidthPacket* p = SrsBandwidthPacket::create_publishing();
        srs_freep(p);
    }
    if (true) {
        SrsBandwidthPacket* p = SrsBandwidthPacket::create_stop_publish();
        EXPECT_TRUE(p->is_stop_publish());
        srs_freep(p);
    }
    if (true) {
        SrsBandwidthPacket* p = SrsBandwidthPacket::create_stopped_publish();
        EXPECT_TRUE(p->is_stopped_publish());
        srs_freep(p);
    }
    if (true) {
        SrsBandwidthPacket* p = SrsBandwidthPacket::create_finish();
        EXPECT_TRUE(p->is_finish());
        srs_freep(p);
    }
    if (true) {
        SrsBandwidthPacket* p = SrsBandwidthPacket::create_final();
        EXPECT_TRUE(p->is_final());
        srs_freep(p);
    }
}

VOID TEST(ProtocolRTMPTest, CoverAllUnmarshal)
{
    srs_error_t err;

    if (true) {
        SrsAmf0Any* name = SrsAmf0Any::str("call");

        SrsAmf0EcmaArray* arr = SrsAmf0Any::ecma_array();
        arr->set("license", SrsAmf0Any::str("MIT"));

        int nn = name->total_size() + arr->total_size();
        char* b = new char[nn];
        SrsAutoFreeA(char, b);

        SrsBuffer buf(b, nn);
        HELPER_ASSERT_SUCCESS(name->write(&buf));
        HELPER_ASSERT_SUCCESS(arr->write(&buf));
        srs_freep(name); srs_freep(arr);

        SrsOnMetaDataPacket* p = new SrsOnMetaDataPacket();
        SrsAutoFree(SrsOnMetaDataPacket, p);

        buf.skip(-1 * buf.pos());
        HELPER_ASSERT_SUCCESS(p->decode(&buf));

        SrsAmf0Any* prop = p->metadata->get_property("license");
        ASSERT_TRUE(prop && prop->is_string());
        EXPECT_STREQ("MIT", prop->to_str().c_str());
    }

    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);

        if (true) {
            SrsConnectAppPacket* pkt = new SrsConnectAppPacket();
            pkt->command_object->set("tcUrl", SrsAmf0Any::str("rtmp://127.0.0.1/live"));
            pkt->command_object->set("pageUrl", SrsAmf0Any::str("http://ossrs.net"));
            pkt->command_object->set("swfUrl", SrsAmf0Any::str("http://ossrs.net/index.swf"));
            pkt->command_object->set("objectEncoding", SrsAmf0Any::number(5.0));

            pkt->args = SrsAmf0Any::object();
            pkt->args->set("license", SrsAmf0Any::str("MIT"));

            HELPER_EXPECT_SUCCESS(r.send_and_free_packet(pkt, 0));
            io.in_buffer.append(&io.out_buffer);
        }

        SrsRequest req;
        HELPER_EXPECT_SUCCESS(r.connect_app(&req));

        EXPECT_STREQ("rtmp", req.schema.c_str());
        EXPECT_STREQ("127.0.0.1", req.host.c_str());
        EXPECT_STREQ("127.0.0.1", req.vhost.c_str());
        EXPECT_STREQ("live", req.app.c_str());
        EXPECT_STREQ("http://ossrs.net", req.pageUrl.c_str());
        EXPECT_STREQ("http://ossrs.net/index.swf", req.swfUrl.c_str());
        EXPECT_EQ(5.0, req.objectEncoding);

        ASSERT_TRUE(req.args && req.args->is_object());
        SrsAmf0Any* prop = req.args->get_property("license");
        ASSERT_TRUE(prop && prop->is_string());
        EXPECT_STREQ("MIT", prop->to_str().c_str());
    }

    if (true) {
        MockBufferIO io;
        SrsRtmpServer r(&io);

        if (true) {
            SrsConnectAppPacket* pkt = new SrsConnectAppPacket();
            pkt->command_object->set("tcUrl", SrsAmf0Any::number(3.0));
            HELPER_EXPECT_SUCCESS(r.send_and_free_packet(pkt, 0));
            io.in_buffer.append(&io.out_buffer);
        }

        SrsRequest req;
        HELPER_EXPECT_FAILED(r.connect_app(&req));
    }

    if (true) {
        SrsAmf0Any* name = SrsAmf0Any::str("pause");
        SrsAmf0Any* tid = SrsAmf0Any::number(3.0);
        SrsAmf0Any* null = SrsAmf0Any::null();
        SrsAmf0Any* is_pause = SrsAmf0Any::boolean(true);
        SrsAmf0Any* ts = SrsAmf0Any::number(30.0);

        int nn = name->total_size() + tid->total_size() + null->total_size() + is_pause->total_size() + ts->total_size();
        char* b = new char[nn];
        SrsAutoFreeA(char, b);

        SrsBuffer buf(b, nn);
        HELPER_ASSERT_SUCCESS(name->write(&buf));
        HELPER_ASSERT_SUCCESS(tid->write(&buf));
        HELPER_ASSERT_SUCCESS(null->write(&buf));
        HELPER_ASSERT_SUCCESS(is_pause->write(&buf));
        HELPER_ASSERT_SUCCESS(ts->write(&buf));
        srs_freep(name); srs_freep(tid); srs_freep(null); srs_freep(is_pause); srs_freep(ts);

        SrsPausePacket* p = new SrsPausePacket();
        SrsAutoFree(SrsPausePacket, p);

        buf.skip(-1 * buf.pos());
        HELPER_ASSERT_SUCCESS(p->decode(&buf));
        EXPECT_TRUE(p->is_pause);
        EXPECT_EQ(30.0, p->time_ms);
    }

    if (true) {
        SrsAmf0Any* name = SrsAmf0Any::str("play");
        SrsAmf0Any* tid = SrsAmf0Any::number(3.0);
        SrsAmf0Any* null = SrsAmf0Any::null();
        SrsAmf0Any* stream_name = SrsAmf0Any::str("livestream");
        SrsAmf0Any* start = SrsAmf0Any::number(20.0);
        SrsAmf0Any* duration = SrsAmf0Any::number(30.0);
        SrsAmf0Any* reset = SrsAmf0Any::number(1.0);

        int nn = name->total_size() + tid->total_size() + null->total_size() + stream_name->total_size() + start->total_size() + duration->total_size() + reset->total_size();
        char* b = new char[nn];
        SrsAutoFreeA(char, b);

        SrsBuffer buf(b, nn);
        HELPER_ASSERT_SUCCESS(name->write(&buf));
        HELPER_ASSERT_SUCCESS(tid->write(&buf));
        HELPER_ASSERT_SUCCESS(null->write(&buf));
        HELPER_ASSERT_SUCCESS(stream_name->write(&buf));
        HELPER_ASSERT_SUCCESS(start->write(&buf));
        HELPER_ASSERT_SUCCESS(duration->write(&buf));
        HELPER_ASSERT_SUCCESS(reset->write(&buf));
        srs_freep(name); srs_freep(tid); srs_freep(null); srs_freep(stream_name); srs_freep(start); srs_freep(duration); srs_freep(reset);

        SrsPlayPacket* p = new SrsPlayPacket();
        SrsAutoFree(SrsPlayPacket, p);

        buf.skip(-1 * buf.pos());
        HELPER_ASSERT_SUCCESS(p->decode(&buf));
        EXPECT_STREQ("livestream", p->stream_name.c_str());
        EXPECT_EQ(20.0, p->start);
        EXPECT_EQ(30.0, p->duration);
        EXPECT_TRUE(p->reset);
    }

    if (true) {
        SrsAmf0Any* name = SrsAmf0Any::str("play");
        SrsAmf0Any* tid = SrsAmf0Any::number(3.0);
        SrsAmf0Any* null = SrsAmf0Any::null();
        SrsAmf0Any* stream_name = SrsAmf0Any::str("livestream");
        SrsAmf0Any* start = SrsAmf0Any::number(20.0);
        SrsAmf0Any* duration = SrsAmf0Any::number(30.0);
        SrsAmf0Any* reset = SrsAmf0Any::boolean(true);

        int nn = name->total_size() + tid->total_size() + null->total_size() + stream_name->total_size() + start->total_size() + duration->total_size() + reset->total_size();
        char* b = new char[nn];
        SrsAutoFreeA(char, b);

        SrsBuffer buf(b, nn);
        HELPER_ASSERT_SUCCESS(name->write(&buf));
        HELPER_ASSERT_SUCCESS(tid->write(&buf));
        HELPER_ASSERT_SUCCESS(null->write(&buf));
        HELPER_ASSERT_SUCCESS(stream_name->write(&buf));
        HELPER_ASSERT_SUCCESS(start->write(&buf));
        HELPER_ASSERT_SUCCESS(duration->write(&buf));
        HELPER_ASSERT_SUCCESS(reset->write(&buf));
        srs_freep(name); srs_freep(tid); srs_freep(null); srs_freep(stream_name); srs_freep(start); srs_freep(duration); srs_freep(reset);

        SrsPlayPacket* p = new SrsPlayPacket();
        SrsAutoFree(SrsPlayPacket, p);

        buf.skip(-1 * buf.pos());
        HELPER_ASSERT_SUCCESS(p->decode(&buf));
        EXPECT_STREQ("livestream", p->stream_name.c_str());
        EXPECT_EQ(20.0, p->start);
        EXPECT_EQ(30.0, p->duration);
        EXPECT_TRUE(p->reset);
    }

    if (true) {
        SrsAmf0Any* name = SrsAmf0Any::str("play");
        SrsAmf0Any* tid = SrsAmf0Any::number(3.0);
        SrsAmf0Any* null = SrsAmf0Any::null();
        SrsAmf0Any* stream_name = SrsAmf0Any::str("livestream");
        SrsAmf0Any* start = SrsAmf0Any::number(20.0);
        SrsAmf0Any* duration = SrsAmf0Any::number(30.0);
        SrsAmf0Any* reset = SrsAmf0Any::str("true");

        int nn = name->total_size() + tid->total_size() + null->total_size() + stream_name->total_size() + start->total_size() + duration->total_size() + reset->total_size();
        char* b = new char[nn];
        SrsAutoFreeA(char, b);

        SrsBuffer buf(b, nn);
        HELPER_ASSERT_SUCCESS(name->write(&buf));
        HELPER_ASSERT_SUCCESS(tid->write(&buf));
        HELPER_ASSERT_SUCCESS(null->write(&buf));
        HELPER_ASSERT_SUCCESS(stream_name->write(&buf));
        HELPER_ASSERT_SUCCESS(start->write(&buf));
        HELPER_ASSERT_SUCCESS(duration->write(&buf));
        HELPER_ASSERT_SUCCESS(reset->write(&buf));
        srs_freep(name); srs_freep(tid); srs_freep(null); srs_freep(stream_name); srs_freep(start); srs_freep(duration); srs_freep(reset);

        SrsPlayPacket* p = new SrsPlayPacket();
        SrsAutoFree(SrsPlayPacket, p);

        buf.skip(-1 * buf.pos());
        HELPER_EXPECT_FAILED(p->decode(&buf));
    }
}

VOID TEST(ProtocolRTMPTest, ComplexToSimpleHandshake)
{
    srs_error_t err;

    uint8_t c0c1[] = {
        0x03, 0x01, 0x14, 0xf7, 0x4e, 0x80, 0x00, 0x07, 0x02, 0xff, 0x14, 0x98, 0x57, 0x0a, 0x07, 0x58, 0x44, 0x96, 0x47, 0xb5, 0x9a, 0x73, 0xf6, 0x07, 0x0f, 0x49, 0x0d, 0x72, 0xb8, 0x16, 0xbb, 0xb2, 0xb7, 0x61, 0x17, 0x79, 0xa0, 0xe9, 0x98, 0xca, 0xb2, 0x86, 0x64, 0x5f, 0x65, 0x3e, 0xfc, 0x4d, 0xc0, 0x0e, 0x4c, 0xfa, 0x91, 0xc7, 0x0f, 0x2e, 0x57, 0x31, 0x4b, 0x96, 0xef, 0xc9, 0x81, 0x02, 0x00, 0x54, 0x25, 0x2b, 0xb2, 0x0d, 0x7c, 0xee, 0xba, 0xdb, 0xe4, 0x06, 0x78, 0xcd, 0x70, 0x2c, 0x54, 0x5a, 0x3a, 0x03, 0x13, 0x2e, 0xe7, 0x4b, 0x87, 0x40, 0x77, 0x0b, 0x9f, 0xd2, 0xab, 0x32, 0x07, 0x6f, 0x1e, 0x75, 0x74, 0xe9, 0xc7, 0x44, 0xd9, 0x76, 0x53, 0xba, 0xe2, 0x52, 0xfa, 0xcc, 0xef, 0x34, 0xd5, 0x14, 0x61, 0xac, 0xcc, 0x63, 0xfd, 0x2b, 0x2d, 0xb3, 0xb8, 0xdd, 0x8a, 0x51, 0x9a, 0x2d, 0x0e, 0xfa, 0x84, 0x25, 0x55, 0xb2, 0xb7, 0x94, 0x54, 0x68, 0xfb, 0x94, 0xdf, 0xd8, 0xeb, 0x43, 0xd0, 0x11, 0x70, 0x8f, 0xf5, 0x48, 0xfc, 0x69, 0x4d, 0x5b, 0xc6, 0x53, 0x8a, 0x22, 0xea, 0x62, 0x84, 0x89, 0x6b, 0xfe, 0x4e, 0xab, 0x51, 0x98, 0xf4, 0x4f, 0xae, 0xf8, 0xdf, 0xac, 0x43, 0xed, 0x5a, 0x04, 0x97, 0xc4, 0xbe, 0x44, 0x5b, 0x99, 0x20, 0x68, 0x67, 0x0f, 0xe3, 0xfa, 0x4c, 0x9d, 0xe7, 0x0b, 0x3f, 0x80, 0x7c, 0x4c, 0x35, 0xf6, 0xdd, 0x20, 0x05, 0xfd, 0x0f, 0x39, 0xb7, 0x36, 0x45, 0x4c, 0xb7, 0x62, 0x92, 0x35, 0x2a, 0xcd, 0xb9, 0x49, 0xea, 0x12, 0x0b, 0x5f, 0x39, 0xae, 0x3b, 0x49, 0x29, 0xe6, 0x30, 0xc7, 0x7c, 0x77, 0xaf, 0x00, 0x43, 0x4d, 0x06, 0x45, 0x72, 0x73, 0x25, 0x71, 0x5e, 0x35, 0x04, 0xbd, 0xe9, 0x48, 0x23, 0x64, 0x4d, 0x15, 0x0b, 0xc5, 0x3f, 0x6e, 0x3a, 0xd5, 0xd5, 0xa6, 0xae, 0x3b, 0x4c, 0x66, 0x6a, 0x70, 0x8b, 0xf3, 0x6a, 0x43, 0xc4, 0xb9, 0xbd, 0xa0, 0x09, 0x72, 0xbc, 0xce, 0x7a, 0xea, 0x49, 0xf2, 0x86, 0xa7, 0xd8, 0x4a, 0x87, 0x28, 0xca, 0x2c, 0x53, 0xee, 0x96, 0x0b, 0xbe, 0x15, 0x14, 0xa8, 0x00, 0xca, 0x76, 0x08, 0x4d, 0x0f, 0xef, 0x78, 0x4b, 0xf6, 0x47, 0x60, 0xfc, 0x16, 0x00, 0x7c, 0x6b, 0x49, 0x39, 0x64, 0x36, 0xee, 0x45, 0x3a, 0x9a, 0xa5, 0xbf, 0xfb, 0x7b, 0xe7, 0xcf, 0x42, 0x82, 0x48, 0x1b, 0x30, 0xfe, 0x0d, 0xba, 0x10, 0xb8, 0xe1, 0x40, 0xcc, 0x6f, 0x36, 0x1c, 0x94, 0x5d, 0x50, 0x9e, 0x21, 0x08, 0xc9, 0xd5, 0xb0, 0x32, 0x51, 0x6a, 0x8f, 0xfa, 0x57, 0x8d, 0x45, 0xd7, 0xd2, 0xd0, 0xd6, 0x6c, 0x78, 0x95, 0xe9, 0xe1, 0x20, 0x97, 0x1a, 0x43, 0x40, 0xa3, 0xb5, 0xcc, 0x4b, 0x12, 0x84, 0x1e, 0x0e, 0xd3, 0x32, 0xca, 0x99, 0xc3, 0x2b, 0x78, 0x17, 0x24, 0x6b, 0xc7, 0xbc, 0x9d, 0x05, 0xc6, 0xaf, 0x8f, 0x19, 0x75, 0x3c, 0x08, 0xa6, 0x08, 0x26, 0x5b, 0xf4, 0x10, 0x40, 0xaa, 0x6a, 0x7e, 0xb9, 0xde, 0x0b, 0x23, 0x3f, 0x53, 0x5a, 0x20, 0x13, 0x62, 0xec, 0x53, 0x86, 0x81, 0x1f, 0xf6, 0x8e, 0xe3, 0xd1, 0xaa, 0xb5, 0x41, 0x87, 0x62, 0xd2, 0xb7, 0x09, 0x12, 0x71, 0x01, 0x2c, 0xac, 0x6d, 0x9d, 0x37, 0x46, 0x5b, 0xdc, 0x76, 0x2c, 0x96, 0x61, 0x88, 0x55, 0x5a, 0x20, 0xc2, 0x84, 0x95, 0xbd, 0x72, 0xc4, 0xb7, 0x22, 0xae, 0xeb, 0x49, 0x0e, 0x16, 0xf1, 0xf1, 0xbf, 0xc5, 0xc7, 0xa8, 0x8d, 0xfb, 0xe1, 0x08, 0x6c, 0xc4, 0x79, 0x81, 0x13, 0xe8, 0x39, 0xbf, 0x6e, 0x5c, 0xa1, 0x62, 0xfb, 0x32, 0x2a, 0x62, 0xf0, 0x12, 0x07, 0x31, 0x93, 0x40, 0xf3, 0xc0, 0xea, 0x1d, 0xd8, 0x65, 0xba, 0x12, 0xb3, 0x9b, 0xf5, 0x59, 0x9c, 0x4e, 0xf6, 0xb9, 0xf7, 0x85, 0xa1, 0xd9, 0x2f, 0x7c, 0x8b, 0xd0, 0xfc, 0x53, 0x3b, 0xed, 0x85, 0xa4, 0xd2, 0x5e, 0x69, 0x61, 0x02, 0x53, 0xb6, 0x19, 0xc7, 0x82, 0xea, 0x8a, 0x45, 0x01, 0x5d, 0x4b, 0xb3, 0x06, 0x86, 0x7f, 0x4b, 0x2f, 0xe7, 0xa8, 0xd0, 0x28, 0x62, 0x02, 0xe8, 0xf3, 0x9e, 0x1e, 0x72, 0x82, 0x07, 0x9f, 0xdd, 0xd2, 0x83, 0x7d, 0x89, 0x73, 0x1b, 0x6f, 0x35, 0x20, 0xb7, 0x88, 0x15, 0x92, 0xa7, 0x11, 0xfe, 0x81, 0x68, 0xed, 0x14, 0x07, 0xdf, 0x4a, 0x06, 0x9c, 0x5e, 0x7e, 0x34, 0x3a, 0x2a, 0x8a, 0xd3, 0xe8, 0xf8, 0xd4, 0xdb, 0xe3, 0xe9, 0x73, 0xbf, 0xa7, 0xe9, 0x73, 0x62, 0xf2, 0x9d, 0xc1, 0xf7, 0x51, 0xeb, 0xff, 0xb7, 0xe6, 0xd9, 0xac, 0x46, 0x06, 0x74, 0xe2, 0x25, 0x3f, 0x46, 0x43, 0xce, 0x49, 0x52, 0x25, 0x1b, 0xf9, 0x24, 0x5c, 0xda, 0xfd, 0x7f, 0xf6, 0xef, 0xb3, 0xd5, 0xe9, 0x6e, 0x35, 0xb8, 0xd1, 0x0e, 0x2c, 0xc1, 0x48, 0x5a, 0x27, 0x0a, 0x81, 0x01, 0x0f, 0xe4, 0x51, 0xcf, 0x89, 0x36, 0xd3, 0xe8, 0x5e, 0x05, 0xb9, 0x83, 0x42, 0xf3, 0xa5, 0x94, 0x67, 0x6d, 0x6a, 0x6e, 0xad, 0xf8, 0x90, 0xb1, 0x1d, 0x63, 0x18, 0x52, 0xc1, 0xbf, 0xbc, 0xad, 0xf4, 0xd2, 0xc5, 0xef, 0xca, 0x4c, 0xfe, 0xa1, 0xda, 0x15, 0x92, 0x4c, 0x42, 0x3d, 0xfc, 0x80, 0x7e, 0x49, 0x13, 0x4e, 0xf6, 0xe1, 0xee, 0x70, 0xca, 0xd9, 0x0a, 0xde, 0x9b, 0xea, 0xcd, 0xf9, 0x90, 0xfd, 0xae, 0x09, 0xce, 0xb6, 0xa0, 0xf7, 0xd1, 0xe6, 0x0c, 0x55, 0x1e, 0x3f, 0xbb, 0x1e, 0xff, 0x3d, 0xdb, 0xdd, 0x27, 0x80, 0x06, 0x53, 0x7e, 0x0b, 0x2a, 0x80, 0x24, 0x51, 0x5c, 0x6a, 0xab, 0x32, 0x5d, 0x37, 0x8a, 0xf4, 0xb7, 0x11, 0xa7, 0xc1, 0x9e, 0x05, 0x2c, 0x16, 0xc2, 0x08, 0xe2, 0xac, 0x1a, 0xeb, 0x60, 0xf8, 0xd2, 0xea, 0x39, 0x01, 0x1c, 0x64, 0xbd, 0x22, 0x80, 0x19, 0x20, 0xc9, 0x6f, 0xdd, 0x5c, 0x73, 0x8c, 0xa1, 0x53, 0x48, 0x2e, 0x99, 0x1d, 0xc0, 0x8f, 0x28, 0xf1, 0xe3, 0xc5, 0xc5, 0x65, 0x53, 0xf2, 0x44, 0x44, 0x24, 0xb9, 0xe2, 0x73, 0xe4, 0x76, 0x14, 0x56, 0xb8, 0x82, 0xe3, 0xb4, 0xfd, 0x68, 0x31, 0xed, 0x40, 0x10, 0x99, 0xd3, 0x3d, 0xe5, 0x6b, 0x14, 0x61, 0x66, 0x9a, 0xf6, 0x33, 0x98, 0xc5, 0x4d, 0x11, 0xbb, 0xf8, 0x56, 0xf8, 0x8f, 0xd7, 0xb9, 0xda, 0xa3, 0x56, 0x1a, 0xe0, 0x9e, 0xbe, 0x5f, 0x56, 0xe5, 0xb9, 0xd8, 0xf3, 0xbc, 0x19, 0xf5, 0xe9, 0x1f, 0xd2, 0xea, 0xf4, 0x5a, 0xde, 0xed, 0xd4, 0x9e, 0xc8, 0xf5, 0x54,
        0x83, 0x8b, 0x8c, 0x2d, 0x24, 0x0e, 0x30, 0xb1, 0x84, 0xa2, 0xbe, 0x2c, 0x86, 0xe6, 0x42, 0x82, 0xaa, 0x37, 0x64, 0x55, 0x51, 0xbc, 0xde, 0xc0, 0x63, 0x88, 0xf6, 0x31, 0x71, 0x52, 0xd5, 0x34, 0x0f, 0x8e, 0xcb, 0x28, 0x65, 0x93, 0x1a, 0x66, 0x3b, 0x21, 0x00, 0xaa, 0x7a, 0xda, 0x2d, 0xf6, 0x7e, 0xb5, 0x27, 0x79, 0xf4, 0x50, 0x3b, 0x10, 0x6b, 0x3c, 0xd7, 0x99, 0x9d, 0xf6, 0xc5, 0x01, 0x91, 0xa0, 0xd5, 0x4f, 0xd3, 0x76, 0x54, 0xa8, 0x5c, 0x35, 0x1d, 0xe2, 0x35, 0x6a, 0x68, 0x67, 0x03, 0xc4, 0x1f, 0xe9, 0x60, 0xb8, 0x49, 0xb1, 0x9a, 0x40, 0xd9, 0x3c, 0x4c, 0x73, 0xaa, 0x88, 0x63, 0xaf, 0xfe, 0xe8, 0xa8, 0x0c, 0x96, 0xbe, 0xb4, 0x65, 0x7c, 0x27, 0xfb, 0xc1, 0x27, 0x24, 0x58, 0xab, 0x4b, 0xa0, 0x5a, 0x7d, 0xc7, 0xca, 0x2d, 0xa5, 0x22, 0xa7, 0xed, 0x26, 0x87, 0xd5, 0x44, 0x1a, 0xc7, 0xdd, 0xfb, 0x60, 0xfc, 0xe5, 0x50, 0xd9, 0x8d, 0xa7, 0xdb, 0x78, 0xb6, 0x9d, 0x80, 0x0f, 0xb9, 0x5f, 0xa7, 0x53, 0x92, 0x5d, 0x18, 0xce, 0x89, 0xc2, 0x69, 0xee, 0xcf, 0xb6, 0x66, 0xe5, 0x66, 0xd2, 0xe3, 0x35, 0x74, 0x0b, 0x83, 0xb6, 0xde, 0xf1, 0xfb, 0xb4, 0x1d, 0x4b, 0x94, 0x95, 0x06, 0x82, 0xe7, 0x1c, 0xf8, 0xc5, 0xe6, 0xd0, 0xf2, 0x17, 0x37, 0x44, 0xfe, 0x99, 0x43, 0x82, 0xbb, 0x88, 0xe4, 0x43, 0x67, 0xcc, 0x4d, 0x5f, 0xa6, 0x26, 0xd7, 0x53, 0xd6, 0x45, 0x96, 0x2b, 0x63, 0xd1, 0x2a, 0xa1, 0x2c, 0x41, 0x59, 0x8b, 0xb8, 0xc1, 0x89, 0x03, 0x3a, 0x61, 0x13, 0xc4, 0x2c, 0x37, 0xa5, 0xbf, 0xd7, 0xdb, 0xd8, 0x53, 0x5f, 0xa1, 0xdb, 0xdb, 0xa5, 0x73, 0xb6, 0xf7, 0x74, 0xa0, 0xf8, 0x93, 0xf5, 0x61, 0xee, 0x3c, 0xe7, 0x00, 0x01, 0x98, 0xe0, 0xa1, 0x22, 0xb6, 0x9a, 0x83, 0x44, 0xa1, 0xe6, 0x70, 0x56, 0x65, 0x92, 0x1e, 0xf0, 0xbc, 0x73, 0xa5, 0x7a, 0xc1, 0x1a, 0x02, 0xf9, 0xd4, 0xc4, 0x7c, 0x81, 0xda, 0x15, 0xc0, 0xd4, 0x25, 0xdc, 0x17, 0xa6, 0x0d, 0x90, 0x55, 0xf2, 0x10, 0xf8, 0xa7, 0x71, 0x9b, 0xed, 0xdf, 0xdf, 0xa1, 0xe4, 0xb9, 0x12, 0x6b, 0x05, 0x3e, 0x83, 0x99, 0x49, 0xbf, 0x66, 0xbb, 0xf6, 0x76, 0xd3, 0xa9, 0x24, 0x61, 0x8c, 0x25, 0x49, 0xd0, 0xf7, 0x83, 0x44, 0xfb, 0x27, 0xe2, 0x7d, 0x69, 0x6d, 0x34, 0x67, 0xed, 0x39, 0x89, 0x02, 0xcb, 0x2f, 0x33, 0x3c, 0xcd, 0x12, 0x42, 0x8f, 0x86, 0x7d, 0xda, 0x3f, 0xd7, 0x26, 0x62, 0x9c, 0x1f, 0x2e, 0xa8, 0xc3, 0x85, 0xf1, 0x73, 0xe5, 0x2c, 0x11, 0xde, 0x98, 0xc8, 0xb0, 0x10, 0x17, 0x55, 0xf5, 0x32, 0x52, 0x67, 0xca, 0x64, 0x50, 0x28, 0x9a, 0x24, 0x92, 0xa1, 0x97, 0x57, 0x81, 0xaf, 0xca, 0x1e, 0xc0, 0xa4, 0x71, 0x2d, 0x2a, 0xec, 0xc9, 0x23, 0x6a, 0x0c, 0x1d, 0x54, 0x15, 0x2a, 0x56, 0x42, 0x0a, 0x83, 0xff, 0x28, 0xba, 0xe7, 0x68, 0x38, 0xf5, 0x32, 0xa9, 0xb7, 0xe7, 0x70, 0x32, 0xa8, 0x79, 0x5e, 0x46, 0x1d, 0xec, 0x29, 0x8a, 0xde, 0x41, 0x94, 0x94, 0x26, 0x79, 0xc2, 0x52, 0x23, 0xe0, 0xa1, 0x1d, 0x65, 0x0c, 0xbe, 0x1b, 0x87, 0x2a, 0x21, 0x53, 0x2f, 0x35, 0x56, 0xe8, 0xd1, 0x7b, 0xb8, 0x23, 0x75, 0x56, 0xc7, 0x08, 0x9d, 0x13, 0xf0, 0x8f, 0x80, 0x38, 0xe9, 0x92, 0xf7, 0x16, 0xc2, 0xf3, 0x74, 0xa7, 0x92, 0xf5, 0x49, 0x7d, 0x09, 0x41, 0xbc, 0x07, 0x61, 0x1f, 0xe6, 0xa0, 0xd8, 0xa6, 0xe3, 0x72, 0xa4, 0x59, 0x4a, 0xd9, 0x33, 0x40, 0x80, 0x3a, 0x3a, 0xb3, 0xa0, 0x96, 0xca, 0x56, 0x98, 0xbd, 0x1f, 0x80, 0x86, 0x6c, 0xe1, 0x09, 0x64, 0x1b, 0x1a, 0xc9, 0x52, 0xaa, 0xd1, 0x39, 0xea, 0x4b, 0x6a, 0x3e, 0x4e, 0xa4, 0xea, 0x00, 0xde, 0x07, 0x0b, 0x23, 0xbc, 0x40, 0xc4, 0xd2, 0xd9, 0xf6, 0xda, 0x8e, 0x22, 0x36, 0xbe, 0x5e, 0x65, 0x6e, 0xbe, 0xc8, 0xb0, 0x07, 0xa2, 0x2d, 0xe9, 0x4b, 0x73, 0x54, 0xe6, 0x0a, 0xf2, 0xd3, 0x83, 0x8b, 0x27, 0x4c, 0xcc, 0x0c, 0x8a, 0xd4, 0x2b, 0xb8, 0x95, 0x2e, 0x42, 0x64, 0x29, 0xc1, 0xe0, 0x6b, 0x92, 0xab, 0xfe, 0x53, 0x06, 0x96, 0x4a, 0x8c, 0x5d, 0x7c, 0x51, 0x74, 0xd0, 0x1e, 0x37, 0x35, 0x9c, 0x1e, 0x69, 0x8f, 0x68, 0x18, 0xd9, 0xbe, 0xaf, 0x81, 0x9b, 0x7e, 0xd8, 0x71, 0x9d, 0xb6, 0x50, 0x43, 0x78, 0x85, 0x7d, 0x65, 0x93, 0x45, 0xb4, 0x02, 0xd0, 0x5c, 0x36, 0xe2, 0x62, 0x3f, 0x40, 0x33, 0xee, 0x91, 0xe5, 0x3f, 0x67, 0x39, 0x2f, 0x1b, 0x89, 0x9f, 0x04, 0x9d, 0x46, 0x3e, 0x70, 0x92, 0x9e, 0x8c, 0xf5
    };
    uint8_t c2[] = {
        0x5b, 0x52, 0xf1, 0x2d, 0x94, 0xcb, 0xb0, 0x86, 0xd8, 0xd3, 0xe3, 0x20, 0x88, 0x47, 0xcf, 0x5a, 0x49, 0xd2, 0x11, 0x30, 0x92, 0x17, 0x8d, 0xf4, 0x99, 0xf7, 0x6c, 0x8a, 0xbc, 0xe7, 0x5c, 0x58, 0x6a, 0x65, 0xed, 0x81, 0xdc, 0xdd, 0xcf, 0x83, 0xcd, 0xa4, 0xed, 0xa2, 0x5e, 0x63, 0xd9, 0x98, 0xf6, 0x2e, 0x15, 0x76, 0x9a, 0xc8, 0x8c, 0x42, 0x54, 0x44, 0xf4, 0x47, 0xf5, 0x96, 0xc9, 0x6e, 0x23, 0x09, 0x1a, 0x0d, 0xe3, 0x04, 0xe6, 0xed, 0x48, 0x49, 0x62, 0x31, 0xe8, 0x36, 0x04, 0xed, 0xb9, 0xe7, 0xa6, 0x35, 0x4d, 0xcd, 0xe3, 0xfa, 0xa0, 0xc8, 0x34, 0xbd, 0x62, 0x7b, 0xbc, 0xbe, 0x1c, 0x5b, 0x69, 0x1f, 0x9c, 0x30, 0x20, 0x48, 0x52, 0xd1, 0xb6, 0x5e, 0xa2, 0x6e, 0x06, 0x94, 0x72, 0x10, 0x56, 0x7c, 0x94, 0xa5, 0xc0, 0xaa, 0xea, 0x48, 0x61, 0x03, 0x14, 0x94, 0x09, 0x77, 0xd9, 0xa7, 0xfe, 0x78, 0x17, 0x95, 0x4f, 0x7e, 0xb0, 0x32, 0x63, 0x02, 0x17, 0x47, 0x1e, 0x7d, 0xb2, 0x7d, 0xb5, 0xcb, 0x9f, 0x61, 0x65, 0xed, 0x03, 0xd2, 0xdb, 0xd1, 0xb3, 0xd6, 0x1a, 0xf5, 0x67, 0x0b, 0x8b, 0x6b, 0x44, 0xf2, 0x62, 0x42, 0xc2, 0x4d, 0xe1, 0x5c, 0xfe, 0xc6, 0x19, 0x2b, 0xfb, 0x03, 0x0f, 0x1b, 0x89, 0x08, 0x86, 0x40, 0xca, 0x45, 0x15, 0xda, 0x65, 0xcc, 0x73, 0x00, 0x49, 0x4e, 0x48, 0x21, 0x25, 0xc6, 0xde, 0x26, 0x21, 0x1d, 0xea, 0x3c, 0x11, 0xac, 0xef, 0x34, 0x4c, 0x96, 0xcc, 0x5e, 0x26, 0xf3, 0xcd, 0x70, 0x0d, 0x62, 0xea, 0x09, 0x35, 0x2b, 0x1e, 0x60, 0xe4, 0x76, 0xd3, 0x65, 0x01, 0x8c, 0xab, 0xd4, 0x89, 0xad, 0x81, 0x9d, 0x04, 0x01, 0xd5, 0x55, 0x3c, 0xcb, 0x32, 0xe1, 0xb5, 0xd4, 0xda, 0xb4, 0xa9, 0x01, 0xb2, 0x10, 0xc7, 0xb1, 0xa9, 0x54, 0x66, 0x1d, 0xcc, 0xff, 0x54, 0x0b, 0x84, 0x37, 0xe0, 0x3a, 0xa5, 0x68, 0x80, 0x87, 0xbc, 0x3c, 0x0f, 0xda, 0x7e, 0x3c, 0x23, 0xfc, 0xd8, 0xc5, 0x52, 0xf7, 0x22, 0x12, 0x05, 0x9c, 0x68, 0x39, 0xb1, 0xed, 0x26, 0x24, 0x2b, 0x7e, 0x0b, 0xaf, 0x9e, 0x97, 0x45, 0x7b, 0xa9, 0xbc, 0x48, 0x0e, 0x66, 0x93, 0x32, 0x0d, 0x6b, 0xd6, 0xf0, 0x4f, 0x54, 0x18, 0xcd, 0xc9, 0x8c, 0xce, 0xc4, 0xa2, 0xff, 0x1e, 0x69, 0x17, 0x7e, 0xf4, 0x99, 0x09, 0x68, 0xa1, 0x9e, 0x1f, 0xbf, 0x90, 0xdc, 0x77, 0x5d, 0x50, 0x2b, 0x0e, 0xff, 0x96, 0xdc, 0x21, 0x2e, 0x74, 0x22, 0x28, 0x88, 0xa0, 0x00, 0x32, 0x15, 0xb0, 0xfd, 0xb1, 0xc9, 0x75, 0xb3, 0x3c, 0xbd, 0x89, 0xc5, 0xa4, 0x48, 0x17, 0xa9, 0xc9, 0x50, 0x61, 0x0c, 0x35, 0x31, 0x55, 0x11, 0xe3, 0x23, 0xe9, 0x3e, 0x78, 0x25, 0xdc, 0x50, 0xe8, 0x23, 0x5f, 0xb7, 0x3f, 0xc7, 0xae, 0xf0, 0x82, 0x35, 0x46, 0x34, 0x63, 0xcc, 0x5d, 0x96, 0xb8, 0x6a, 0x7a, 0x7f, 0x54, 0x27, 0x1a, 0xa4, 0x63, 0xdd, 0xb0, 0xb6, 0x17, 0x08, 0xa1, 0x2e, 0x95, 0x9e, 0xd4, 0x9b, 0x71, 0x83, 0x81, 0x6c, 0xea, 0xab, 0x00, 0x2e, 0xca, 0x60, 0xc1, 0x4b, 0x83, 0xa7, 0xab, 0x47, 0xe8, 0x1b, 0x5a, 0x78, 0x4f, 0xec, 0xbd, 0x62, 0x94, 0x25, 0x75, 0x2e, 0x64, 0xe7, 0x70, 0x13, 0xac, 0xe9, 0x89, 0x4f, 0x1e, 0x79, 0xbc, 0x15, 0x0c, 0x8d, 0x40, 0xe8, 0x16, 0x31, 0x7c, 0xb8, 0xa5, 0xd7, 0x21, 0x39, 0x93, 0x9b, 0xe6, 0x05, 0x81, 0xb6, 0x20, 0xa8, 0x5d, 0x73, 0x58, 0x8b, 0x66, 0x92, 0xac, 0x23, 0xa0, 0xf4, 0x8c, 0xab, 0x58, 0xae, 0xb6, 0x9c, 0x3c, 0x4d, 0x77, 0x5f, 0xae, 0xe2, 0x57, 0x89, 0x8f, 0xe4, 0x68, 0x81, 0x24, 0x7d, 0x3b, 0x99, 0x46, 0x9f, 0x7b, 0x9d, 0xa6, 0xdd, 0x99, 0xcf, 0xc1, 0x79, 0x04, 0x95, 0xce, 0x96, 0x7a, 0xd9, 0xb5, 0x6e, 0xcf, 0xd1, 0x72, 0x18, 0x97, 0x76, 0xe2, 0xb7, 0x38, 0x1e, 0x24, 0x0b, 0x09, 0x00, 0x8b, 0x28, 0x5d, 0xf8, 0xd0, 0x50, 0x7f, 0xeb, 0x3b, 0x37, 0x61, 0x0b, 0xd3, 0xff, 0x65, 0x7d, 0x88, 0x1e, 0x1d, 0xbb, 0x6c, 0xf5, 0xf8, 0xf3, 0x2b, 0x51, 0xd9, 0x6d, 0xc9, 0xbe, 0xbe, 0xd1, 0x94, 0x0e, 0x58, 0x2a, 0x0a, 0xe4, 0xf8, 0x28, 0x26, 0xc3, 0x74, 0x87, 0xd3, 0x81, 0x48, 0x6e, 0x9b, 0xd5, 0xa1, 0x60, 0x87, 0xfc, 0x1b, 0x06, 0x33, 0x0d, 0x87, 0xfa, 0x9b, 0xf9, 0x73, 0x6b, 0x0c, 0xdf, 0xea, 0xee, 0x32, 0x78, 0xe0, 0xf8, 0x18, 0x3f, 0xc3, 0x3b, 0x12, 0x88, 0x0b, 0xb2, 0x4a, 0x52, 0x64, 0x4e, 0x58, 0x54, 0x82, 0x52, 0x61, 0x54, 0x28, 0x1b, 0xf7, 0x99, 0x06, 0xa2, 0xad, 0x04, 0x19, 0x9f, 0x2e, 0x34, 0xe6, 0xf0, 0xee, 0xeb, 0x93, 0x9a, 0x9c, 0x73, 0x86, 0x23, 0x6d, 0x5d, 0xae, 0x64, 0xec, 0x6f, 0xf9, 0x7c, 0xc7, 0x46, 0x96, 0xdb, 0x44, 0xf4, 0xab, 0xc9, 0x67, 0x61, 0xb8, 0xec, 0xf0, 0x99, 0xe0, 0x4d, 0x45, 0xed, 0xa3, 0x1c, 0xe9, 0x68, 0x31, 0x85, 0xa5, 0xa1, 0xba, 0x08, 0xdb, 0x3f, 0x84, 0x75, 0x70, 0x24, 0xcd, 0x49, 0xd4, 0x07, 0xa8, 0xaa, 0x52, 0xd9, 0x55, 0x68, 0x8f, 0x78, 0xd2, 0x5d, 0x46, 0x23, 0x60, 0x76, 0xe1, 0x22, 0xdc, 0x2a, 0xeb, 0xac, 0xbc, 0xeb, 0xd6, 0x4c, 0x0f, 0xb5, 0xcb, 0x47, 0xce, 0x43, 0x59, 0x1d, 0x3e, 0xfc, 0x7f, 0x7c, 0x93, 0x9e, 0xef, 0xcd, 0x79, 0x5c, 0x08, 0x8e, 0xeb, 0xa8, 0x98, 0x3e, 0x95, 0xd1, 0x36, 0x42, 0x57, 0xfd, 0x6d, 0xdc, 0xe0, 0xa3, 0x3f, 0x46, 0x32, 0xb7, 0xff, 0x00, 0x4f, 0x7b, 0x23, 0x4d, 0xd0, 0xe5, 0xdd, 0x40, 0xab, 0xb2, 0xcb, 0x45, 0x92, 0x76, 0x7c, 0x5b, 0x98, 0xc7, 0xc0, 0x54, 0x34, 0x94, 0x8e, 0xbb, 0x28, 0xcf, 0xba, 0xd9, 0xa0, 0xe6, 0xf3, 0x65, 0x61, 0xd7, 0x10, 0xd3, 0xeb, 0xce, 0x21, 0x6a, 0xca, 0x61, 0xe7, 0x81, 0x15, 0x18, 0x4e, 0x71, 0xb0, 0x99, 0x62, 0xd9, 0xeb, 0xd0, 0x8b, 0xe9, 0xdf, 0x6a, 0x6d, 0x59, 0x0b, 0x45, 0x93, 0x38, 0xfe, 0xe6, 0x6a, 0xd1, 0x5f, 0xb6, 0xe9, 0x86, 0x01, 0x38, 0xab, 0x59, 0x5c, 0xd7, 0xb7, 0xfa, 0x81, 0x8a, 0xbe, 0xdc, 0xeb, 0x50, 0x7d, 0x81, 0xfa, 0x1b, 0x8f, 0xce, 0x53, 0x38, 0xe4, 0x8a, 0x82, 0xbe, 0x7d, 0xdc, 0xd8, 0x57, 0x5a, 0x48, 0xa3, 0x38, 0x74, 0x8a, 0xac, 0xf2, 0xfd, 0xbf, 0xcc, 0xd8, 0x08, 0x4d, 0x3e, 0xae, 0xa9, 0x00, 0x66, 0x06, 0xcb, 0xf3,
        0x50, 0xcc, 0x52, 0xc7, 0x4b, 0x16, 0x33, 0xa5, 0xde, 0x20, 0xed, 0x6a, 0xa7, 0x58, 0x5e, 0x4e, 0x7e, 0x29, 0xab, 0xb9, 0x65, 0x9d, 0x17, 0xe0, 0x1e, 0x79, 0x77, 0xf6, 0x1e, 0xa0, 0xcb, 0x0c, 0xf7, 0xc0, 0xe4, 0xf6, 0x3b, 0x60, 0x81, 0xfe, 0xed, 0xd9, 0x42, 0xa9, 0x61, 0x9d, 0xa8, 0xd7, 0xe8, 0xaa, 0x97, 0xad, 0xbb, 0xba, 0x13, 0x6e, 0x05, 0xa5, 0xce, 0x7a, 0x65, 0x6f, 0x55, 0xe3, 0xcf, 0xbc, 0x67, 0x14, 0x64, 0x57, 0x9c, 0x46, 0x14, 0xd6, 0x1d, 0x39, 0x1c, 0xd3, 0xe8, 0x98, 0x20, 0x5a, 0x1a, 0x05, 0x3b, 0x27, 0xd5, 0x84, 0xca, 0xd4, 0x0b, 0xc4, 0x1e, 0xd8, 0x46, 0x29, 0x48, 0x95, 0xdb, 0xe5, 0x58, 0x8a, 0x51, 0xc7, 0x74, 0x7f, 0x53, 0xa8, 0xbb, 0x58, 0xc0, 0x5b, 0xe1, 0xa7, 0x27, 0x36, 0x6c, 0xa6, 0x70, 0xec, 0x88, 0xcd, 0x9a, 0x70, 0xe1, 0xa0, 0xc7, 0xdd, 0x60, 0x71, 0xf4, 0x2a, 0x51, 0x98, 0x8e, 0xab, 0xb8, 0x13, 0x03, 0x48, 0x5f, 0x44, 0xf8, 0x88, 0xd9, 0x7d, 0xd3, 0xf1, 0x5f, 0xc4, 0x2b, 0x44, 0x15, 0x57, 0x31, 0xa4, 0xa1, 0xdb, 0x6d, 0x2a, 0x5a, 0x5a, 0xf7, 0xde, 0xd5, 0x23, 0x38, 0x00, 0xe5, 0x5c, 0x55, 0xe7, 0x37, 0x9c, 0xcb, 0x8b, 0xc0, 0x33, 0x42, 0x68, 0x23, 0x84, 0x7d, 0x89, 0x9d, 0xae, 0x59, 0x18, 0xae, 0xea, 0x46, 0x3f, 0xac, 0x57, 0x0d, 0x5d, 0x49, 0x14, 0x50, 0xe5, 0x70, 0x17, 0x73, 0x09, 0x11, 0x93, 0x6b, 0x02, 0x22, 0xb7, 0x63, 0xc9, 0xe6, 0xa4, 0xe3, 0xb1, 0xf7, 0xa6, 0x58, 0x8d, 0x14, 0xa1, 0xda, 0x6a, 0xb9, 0x38, 0xf9, 0x20, 0x45, 0x8c, 0xe6, 0x32, 0x23, 0x9d, 0x5f, 0xba, 0xcb, 0xb4, 0x95, 0xf9, 0xa9, 0x5c, 0x60, 0x03, 0x5a, 0x8c, 0xa7, 0xb9, 0x65, 0xa8, 0x84, 0x38, 0xc0, 0x25, 0xe6, 0xa7, 0xc0, 0x3b, 0xbc, 0x11, 0xed, 0x0e, 0x9a, 0x6f, 0xfe, 0x61, 0x79, 0x86, 0x92, 0x3a, 0xce, 0xe0, 0xb7, 0x70, 0xad, 0xe0, 0xcc, 0x88, 0x47, 0xd9, 0x2a, 0x3d, 0x41, 0x06, 0x77, 0x41, 0xbe, 0x3f, 0x55, 0x31, 0x54, 0x10, 0x14, 0x5b, 0xdf, 0x88, 0xb2, 0x9f, 0xff, 0x11, 0xb8, 0x11, 0xdc, 0x5e, 0x64, 0xf9, 0x97, 0x8a, 0x26, 0x6a, 0x44, 0xb4, 0x83, 0x83, 0x9b, 0x81, 0xaa, 0xfd, 0xb5, 0x8b, 0x16, 0x18, 0x2e, 0x5c, 0xe4, 0x5b, 0x8f, 0xdd, 0x7c, 0x1f, 0x33, 0x2f, 0xef, 0x57, 0x8c, 0x6a, 0x3f, 0x3c, 0x19, 0x5e, 0x73, 0x64, 0xc5, 0xaf, 0x1d, 0xa1, 0xb4, 0x11, 0xee, 0x6b, 0x7e, 0x66, 0xfb, 0xaa, 0x03, 0x17, 0xe4, 0xc9, 0x90, 0x4b, 0xf2, 0x50, 0x55, 0x71, 0xad, 0x31, 0x71, 0x49, 0xd7, 0x80, 0xd1, 0xa5, 0x9f, 0x6d, 0x71, 0x28, 0x2b, 0x65, 0xcf, 0x8d, 0xb1, 0x2a, 0x33, 0xdc, 0x93, 0xff, 0x86, 0xd7, 0xa6, 0xd0, 0x46, 0x66, 0x32, 0x3d, 0x18, 0x8c, 0xd3, 0xda, 0xf6, 0x1b, 0xa0, 0x2d, 0x29, 0xfd, 0x8d, 0x57, 0x2c, 0x82, 0xed, 0x38, 0x4a, 0x6f, 0xc4, 0x3c, 0x9a, 0x61, 0xcb, 0xe5, 0xcf, 0xd3, 0x83, 0xa1, 0x91, 0x93, 0x0d, 0x75, 0xfd, 0x4e, 0x2c, 0x83, 0xa0, 0x85, 0x27, 0x13, 0x5a, 0x24, 0xbd, 0x08, 0x1e, 0xe9, 0xab, 0x92, 0x41, 0xc2, 0x3a, 0xa0, 0xe1, 0xfd, 0x00, 0xb9, 0xf8, 0xca, 0x0b, 0x1a, 0x8e, 0xf6, 0x27, 0x9f, 0x5a, 0xf0, 0x23, 0x07, 0xc8, 0xbf, 0xf6, 0x74, 0xe7, 0xf8, 0x67, 0xfc, 0x28, 0x4e, 0x6a, 0x6c, 0xc6, 0x83, 0xe3, 0xf0, 0x01, 0xe0, 0x0f, 0x2d, 0xdf, 0x9e, 0x4b, 0x8b, 0x06, 0x15, 0x4c, 0x9f, 0xdf, 0x55, 0x14, 0x44, 0xde, 0x34, 0x35, 0x5a, 0xcb, 0xe5, 0xa7, 0xb5, 0x7e, 0x00, 0x31, 0x98, 0x5f, 0x51, 0x11, 0x37, 0xe1, 0xd2, 0x99, 0x8f, 0x70, 0x13, 0x40, 0xa0, 0xbe, 0xf8, 0xde, 0xac, 0x37, 0x06, 0xb6, 0x26, 0xf3, 0xb1, 0x97, 0x0b, 0x85, 0x68, 0x09, 0xa4, 0xc8, 0x34, 0x0a, 0x41, 0x6e, 0xac, 0x1a, 0x5b, 0xe0, 0x91, 0x6f, 0xa3, 0x0a, 0xf6, 0x05, 0x37, 0x32, 0xe1, 0x8e, 0xd8, 0xed, 0x55, 0xa3, 0x54, 0x3f, 0x62, 0x95, 0x82, 0xcf, 0x0a, 0x19, 0xb4, 0x9f, 0x04, 0xcc, 0x86, 0x7e, 0xf1, 0xe5, 0x8b, 0x67, 0x73, 0xa2, 0x46, 0x4e, 0xf2, 0x98, 0x94, 0xb5, 0xeb, 0xa5, 0xbd, 0xcb, 0x66, 0x82, 0xe9, 0x87, 0xe9, 0xe3, 0x50, 0x55, 0x4b, 0xd6, 0x67, 0x30, 0xe1, 0x7c, 0x15, 0x77, 0x29, 0xfd, 0x85, 0x67, 0x5a, 0xc4, 0xd5, 0x69, 0xfa, 0xc7, 0x66, 0x66, 0x49, 0xf7, 0x5a, 0xcd, 0xd1, 0x81, 0x5c, 0x74, 0x8d, 0xbf, 0xc5, 0xc2, 0xff, 0x4d, 0x90, 0xe8, 0x8e, 0x05, 0x00, 0xff, 0x7a, 0xd7, 0xb2, 0x7a, 0xad, 0x8b, 0xd6, 0x4b, 0x52, 0x09, 0x50, 0x4b
    };

    if (true) {
        MockBufferIO io;
        io.append(c0c1, 1537);
        io.append(c2, 1536);

        SrsRtmpServer r(&io);
        HELPER_EXPECT_SUCCESS(r.handshake());
    }
}

VOID TEST(ProtocolRTMPTest, ConnectAppWithArgs)
{
    srs_error_t err;

    // ConnectApp.
    if (true) {
        MockBufferIO io;

        MockBufferIO tio;
        SrsProtocol p(&tio);
        if (true) {
            SrsConnectAppResPacket* res = new SrsConnectAppResPacket();

            SrsAmf0EcmaArray* data = SrsAmf0Any::ecma_array();
            res->info->set("data", data);

            data->set("srs_server_ip", SrsAmf0Any::str("1.2.3.4"));
            data->set("srs_server", SrsAmf0Any::str("srs"));
            data->set("srs_id", SrsAmf0Any::number(100));
            data->set("srs_pid", SrsAmf0Any::number(200));
            data->set("srs_version", SrsAmf0Any::str("3.4.5.678"));

            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(res, 0));

            io.in_buffer.append(&tio.out_buffer);
        }

        SrsRequest req;
        req.args = SrsAmf0Any::object();
        req.args->set("license", SrsAmf0Any::str("MIT"));

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

        if (true) {
            tio.in_buffer.append(&io.out_buffer);

            SrsCommonMessage* msg = NULL;
            SrsConnectAppPacket* pkt = NULL;
            HELPER_ASSERT_SUCCESS(p.expect_message(&msg, &pkt));

            SrsAmf0Any* prop = pkt->command_object->get_property("tcUrl");
            ASSERT_TRUE(prop && prop->is_string());
            EXPECT_STREQ("rtmp://127.0.0.1/live", prop->to_str().c_str());

            ASSERT_TRUE(pkt->args);
            prop = pkt->args->get_property("license");
            ASSERT_TRUE(prop && prop->is_string());
            EXPECT_STREQ("MIT", prop->to_str().c_str());
        }
    }
}

VOID TEST(ProtocolRTMPTest, AgentMessageCodec)
{
    srs_error_t err;

    if (true) {
        MockBufferIO io;
        SrsRtmpClient p(&io);

        if (true) {
            SrsConnectAppPacket* res = new SrsConnectAppPacket();
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(res, 0));
            io.in_buffer.append(&io.out_buffer);
        }

        if (true) {
            SrsCommonMessage* msg = NULL;
            HELPER_EXPECT_SUCCESS(p.recv_message(&msg));
            srs_freep(msg);
        }
    }

    if (true) {
        MockBufferIO io;
        SrsRtmpClient p(&io);

        if (true) {
            SrsConnectAppPacket* res = new SrsConnectAppPacket();
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(res, 0));
            io.in_buffer.append(&io.out_buffer);
        }

        if (true) {
            SrsCommonMessage* msg = NULL;
            HELPER_ASSERT_SUCCESS(p.recv_message(&msg));

            SrsPacket* pkt = NULL;
            HELPER_EXPECT_SUCCESS(p.decode_message(msg, &pkt));

            srs_freep(msg);
            srs_freep(pkt);
        }
    }

    if (true) {
        MockBufferIO io;
        SrsRtmpServer p(&io);

        if (true) {
            SrsConnectAppPacket* res = new SrsConnectAppPacket();
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(res, 0));
            io.in_buffer.append(&io.out_buffer);
        }

        if (true) {
            SrsCommonMessage* msg = NULL;
            HELPER_EXPECT_SUCCESS(p.recv_message(&msg));
            srs_freep(msg);
        }
    }

    if (true) {
        MockBufferIO io;
        SrsRtmpServer p(&io);

        if (true) {
            SrsConnectAppPacket* res = new SrsConnectAppPacket();
            HELPER_EXPECT_SUCCESS(p.send_and_free_packet(res, 0));
            io.in_buffer.append(&io.out_buffer);
        }

        if (true) {
            SrsCommonMessage* msg = NULL;
            HELPER_ASSERT_SUCCESS(p.recv_message(&msg));

            SrsPacket* pkt = NULL;
            HELPER_EXPECT_SUCCESS(p.decode_message(msg, &pkt));

            srs_freep(msg);
            srs_freep(pkt);
        }
    }
}

srs_error_t _mock_packet_to_shared_msg(SrsPacket* packet, int stream_id, SrsSharedPtrMessage* shared_msg)
{
    srs_error_t err = srs_success;

    SrsCommonMessage* msg = new SrsCommonMessage();
    SrsAutoFree(SrsCommonMessage, msg);

    if ((err = packet->to_msg(msg, stream_id)) != srs_success) {
        srs_freep(msg);
        return err;
    }

    if ((err = shared_msg->create(msg)) != srs_success) {
        return err;
    }

    return err;
}

VOID TEST(ProtocolRTMPTest, CheckStreamID)
{
    srs_error_t err;

    if (true) {
        MockBufferIO io;
        SrsRtmpClient p(&io);

        if (true) {
            SrsSharedPtrMessage* shared_msgs[2];
            SrsConnectAppPacket* res = new SrsConnectAppPacket();
            SrsAutoFree(SrsConnectAppPacket, res);

            if (true) {
                SrsSharedPtrMessage* shared_msg = new SrsSharedPtrMessage();
                HELPER_ASSERT_SUCCESS(_mock_packet_to_shared_msg(res, 1, shared_msg));
                shared_msgs[0] = shared_msg;
            }

            if (true) {
                SrsSharedPtrMessage* shared_msg = new SrsSharedPtrMessage();
                HELPER_ASSERT_SUCCESS(_mock_packet_to_shared_msg(res, 2, shared_msg));
                shared_msgs[1] = shared_msg;
            }

            HELPER_EXPECT_SUCCESS(p.send_and_free_messages(shared_msgs, 2, 1));
            io.in_buffer.append(&io.out_buffer);
        }

        if (true) {
            SrsCommonMessage* msg = NULL;
            HELPER_EXPECT_SUCCESS(p.recv_message(&msg));
            EXPECT_EQ(1, msg->header.stream_id);
            srs_freep(msg);
        }

        if (true) {
            SrsCommonMessage* msg = NULL;
            HELPER_EXPECT_SUCCESS(p.recv_message(&msg));
            EXPECT_EQ(2, msg->header.stream_id);
            srs_freep(msg);
        }
    }
}

VOID TEST(ProtocolRTMPTest, AgentMessageTransform)
{
    srs_error_t err;

    if (true) {
        MockBufferIO io;
        SrsRtmpClient p(&io);

        if (true) {
            SrsSharedPtrMessage* shared_msg = new SrsSharedPtrMessage();
            SrsConnectAppPacket* res = new SrsConnectAppPacket();
            HELPER_ASSERT_SUCCESS(_mock_packet_to_shared_msg(res, 1, shared_msg));
            srs_freep(res);

            HELPER_EXPECT_SUCCESS(p.send_and_free_message(shared_msg, 0));
            io.in_buffer.append(&io.out_buffer);
        }

        if (true) {
            SrsCommonMessage* msg = NULL;
            HELPER_EXPECT_SUCCESS(p.recv_message(&msg));
            srs_freep(msg);
        }
    }

    if (true) {
        MockBufferIO io;
        SrsRtmpClient p(&io);

        if (true) {
            SrsSharedPtrMessage* shared_msg = new SrsSharedPtrMessage();
            SrsConnectAppPacket* res = new SrsConnectAppPacket();
            HELPER_ASSERT_SUCCESS(_mock_packet_to_shared_msg(res, 1, shared_msg));
            srs_freep(res);

            HELPER_EXPECT_SUCCESS(p.send_and_free_messages(&shared_msg, 1, 0));
            io.in_buffer.append(&io.out_buffer);
        }

        if (true) {
            SrsCommonMessage* msg = NULL;
            HELPER_EXPECT_SUCCESS(p.recv_message(&msg));
            srs_freep(msg);
        }
    }

    if (true) {
        MockBufferIO io;
        SrsRtmpServer p(&io);

        if (true) {
            SrsSharedPtrMessage* shared_msg = new SrsSharedPtrMessage();
            SrsConnectAppPacket* res = new SrsConnectAppPacket();
            HELPER_ASSERT_SUCCESS(_mock_packet_to_shared_msg(res, 1, shared_msg));
            srs_freep(res);

            HELPER_EXPECT_SUCCESS(p.send_and_free_message(shared_msg, 0));
            io.in_buffer.append(&io.out_buffer);
        }

        if (true) {
            SrsCommonMessage* msg = NULL;
            HELPER_EXPECT_SUCCESS(p.recv_message(&msg));
            srs_freep(msg);
        }
    }

    if (true) {
        MockBufferIO io;
        SrsRtmpServer p(&io);

        if (true) {
            SrsSharedPtrMessage* shared_msg = new SrsSharedPtrMessage();
            SrsConnectAppPacket* res = new SrsConnectAppPacket();
            HELPER_ASSERT_SUCCESS(_mock_packet_to_shared_msg(res, 1, shared_msg));
            srs_freep(res);

            HELPER_EXPECT_SUCCESS(p.send_and_free_messages(&shared_msg, 1, 0));
            io.in_buffer.append(&io.out_buffer);
        }

        if (true) {
            SrsCommonMessage* msg = NULL;
            HELPER_EXPECT_SUCCESS(p.recv_message(&msg));
            srs_freep(msg);
        }
    }
}

class MockMRHandler : public IMergeReadHandler
{
public:
    ssize_t nn;
    MockMRHandler() : nn(0) {
    }
    virtual void on_read(ssize_t nread) {
        nn += nread;
    }
};

VOID TEST(ProtocolRTMPTest, MergeReadHandler)
{
    srs_error_t err;

    MockBufferIO io;
    SrsRtmpServer r(&io);

    if (true) {
        SrsConnectAppPacket* res = new SrsConnectAppPacket();
        HELPER_EXPECT_SUCCESS(r.send_and_free_packet(res, 0));
        io.in_buffer.append(&io.out_buffer);
    }

    MockMRHandler h;
    r.set_merge_read(true, &h);

    if (true) {
        SrsCommonMessage* msg = NULL;
        HELPER_EXPECT_SUCCESS(r.recv_message(&msg));
        srs_freep(msg);
    }

    EXPECT_TRUE(h.nn > 0);
}

char* _strcpy(const char* src)
{
    return strcpy(new char[strlen(src)], src);
}

VOID TEST(ProtocolRTMPTest, CreateRTMPMessage)
{
    srs_error_t err;

    // Invalid message type.
    if (true) {
        SrsSharedPtrMessage* msg = NULL;
        HELPER_EXPECT_FAILED(srs_rtmp_create_msg(SrsFrameTypeForbidden, 0, _strcpy("Hello"), 5, 0, &msg));
        EXPECT_TRUE(NULL == msg);
    }
    if (true) {
        SrsCommonMessage* msg = NULL;
        HELPER_EXPECT_FAILED(srs_rtmp_create_msg(SrsFrameTypeForbidden, 0, _strcpy("Hello"), 5, 0, &msg));
        EXPECT_TRUE(NULL == msg);
    }

    // Normal script message.
    if (true) {
        SrsSharedPtrMessage* msg = NULL;
        HELPER_EXPECT_SUCCESS(srs_rtmp_create_msg(SrsFrameTypeScript, 0, _strcpy("Hello"), 5, 0, &msg));
        EXPECT_STREQ("Hello", msg->payload);
        srs_freep(msg);
    }

    // Normal video message.
    if (true) {
        SrsSharedPtrMessage* msg = NULL;
        HELPER_EXPECT_SUCCESS(srs_rtmp_create_msg(SrsFrameTypeVideo, 0, _strcpy("Hello"), 5, 0, &msg));
        EXPECT_STREQ("Hello", msg->payload);
        srs_freep(msg);
    }

    // Normal audio message.
    if (true) {
        SrsSharedPtrMessage* msg = NULL;
        HELPER_EXPECT_SUCCESS(srs_rtmp_create_msg(SrsFrameTypeAudio, 0, _strcpy("Hello"), 5, 0, &msg));
        EXPECT_STREQ("Hello", msg->payload);
        srs_freep(msg);
    }
    if (true) {
        SrsCommonMessage* msg = NULL;
        HELPER_EXPECT_SUCCESS(srs_rtmp_create_msg(SrsFrameTypeAudio, 0, _strcpy("Hello"), 5, 0, &msg));
        EXPECT_STREQ("Hello", msg->payload);
        srs_freep(msg);
    }
}

VOID TEST(ProtocolRTMPTest, OthersAll)
{
    if (true) {
        vector<string> vs;
        vs.push_back("Hello");
        vs.push_back("world!");
        string v = srs_join_vector_string(vs, ", ");
        EXPECT_STREQ("Hello, world!", v.c_str());
    }

    if (true) {
        EXPECT_TRUE(srs_is_ipv4("1.2.3.4"));
        EXPECT_TRUE(srs_is_ipv4("255.2.3.4"));
        EXPECT_TRUE(srs_is_ipv4("1255.2.3.4"));
    }

    if (true) {
        EXPECT_FALSE(srs_is_ipv4("ossrs.2.3.4"));
        EXPECT_FALSE(srs_is_ipv4("2.3.4.ossrs"));
    }

    if (true) {
        SrsMessageArray h(10);
        h.msgs[0] = new SrsSharedPtrMessage();
        h.msgs[1] = new SrsSharedPtrMessage();
        EXPECT_TRUE(NULL != h.msgs[0]);
        EXPECT_TRUE(NULL != h.msgs[1]);

        h.free(1);
        EXPECT_TRUE(NULL == h.msgs[0]);
        EXPECT_TRUE(NULL != h.msgs[1]);

        h.free(2);
        EXPECT_TRUE(NULL == h.msgs[0]);
        EXPECT_TRUE(NULL == h.msgs[1]);
    }
}

VOID TEST(ProtocolRTMPTest, ParseRTMPURL)
{
    if (true) {
        string url("rtmp://ossrs.net/live/show/livestream?token=abc"), tcUrl, stream;
        srs_parse_rtmp_url(url, tcUrl, stream);
        EXPECT_STREQ("rtmp://ossrs.net/live/show", tcUrl.c_str());
        EXPECT_STREQ("livestream?token=abc", stream.c_str());
    }

    if (true) {
        string url("rtmp://ossrs.net/live/show/livestream"), tcUrl, stream;
        srs_parse_rtmp_url(url, tcUrl, stream);
        EXPECT_STREQ("rtmp://ossrs.net/live/show", tcUrl.c_str());
        EXPECT_STREQ("livestream", stream.c_str());
    }

    if (true) {
        string url("rtmp://ossrs.net/live/livestream"), tcUrl, stream;
        srs_parse_rtmp_url(url, tcUrl, stream);
        EXPECT_STREQ("rtmp://ossrs.net/live", tcUrl.c_str());
        EXPECT_STREQ("livestream", stream.c_str());
    }
}

VOID TEST(ProtocolRTMPTest, GenerateURL)
{
    if (true) {
        string host("184.23.22.14"), vhost("ossrs.net"), stream("stream"), param("token=abc");
        string url = srs_generate_stream_with_query(host, vhost, stream, param);
        EXPECT_STREQ("stream?token=abc&vhost=ossrs.net", url.c_str());
    }

    if (true) {
        string host("184.23.22.14"), vhost("__defaultVhost__"), stream("stream"), param("vhost=ossrs.net");
        string url = srs_generate_stream_with_query(host, vhost, stream, param);
        EXPECT_STREQ("stream?vhost=ossrs.net", url.c_str());
    }

    if (true) {
        string host("184.23.22.14"), vhost("__defaultVhost__"), stream("stream"), param;
        string url = srs_generate_stream_with_query(host, vhost, stream, param);
        EXPECT_STREQ("stream", url.c_str());
    }

    if (true) {
        string host("184.23.22.14"), vhost("ossrs.net"), stream("stream"), param;
        string url = srs_generate_stream_with_query(host, vhost, stream, param);
        EXPECT_STREQ("stream?vhost=ossrs.net", url.c_str());
    }

    if (true) {
        string host("ossrs.net"), vhost("__defaultVhost__"), stream("stream"), param;
        string url = srs_generate_stream_with_query(host, vhost, stream, param);
        EXPECT_STREQ("stream?vhost=ossrs.net", url.c_str());
    }
}

/**
* discovery tcUrl to schema/vhost/host/port/app
*/
VOID TEST(ProtocolRTMPTest, DiscoveryTcUrl)
{
    // vhost and params
    if (true) {
        int port; std::string tcUrl, schema, ip, vhost, app, stream, param;

        tcUrl = "rtmp://127.0.0.1:19351/live?vhost=demo&token=abc"; stream= "show";
        srs_discovery_tc_url(tcUrl, schema, ip, vhost, app, stream, port, param);
        EXPECT_STREQ("rtmp", schema.c_str());
        EXPECT_STREQ("127.0.0.1", ip.c_str());
        EXPECT_STREQ("demo", vhost.c_str());
        EXPECT_STREQ("live", app.c_str());
        EXPECT_STREQ("show", stream.c_str());
        EXPECT_EQ(19351, port);
    }

    // vhost and params
    if (true) {
        int port; std::string tcUrl, schema, ip, vhost, app, stream, param;

        tcUrl = "rtmp://127.0.0.1:19351/live"; stream= "show?vhost=demo&token=abc";
        srs_discovery_tc_url(tcUrl, schema, ip, vhost, app, stream, port, param);
        EXPECT_STREQ("rtmp", schema.c_str());
        EXPECT_STREQ("127.0.0.1", ip.c_str());
        EXPECT_STREQ("demo", vhost.c_str());
        EXPECT_STREQ("live", app.c_str());
        EXPECT_STREQ("show", stream.c_str());
        EXPECT_EQ(19351, port);
    }

    // default vhost in param
    if (true) {
        int port; std::string tcUrl, schema, ip, vhost, app, stream, param;

        tcUrl = "rtmp://winlin.cn/live"; stream= "show";
        param = "?vhost=__defaultVhost__";
        srs_discovery_tc_url(tcUrl, schema, ip, vhost, app, stream, port, param);
        EXPECT_STREQ("rtmp", schema.c_str());
        EXPECT_STREQ("winlin.cn", ip.c_str());
        EXPECT_STREQ("winlin.cn", vhost.c_str());
        EXPECT_STREQ("live", app.c_str());
        EXPECT_STREQ("show", stream.c_str());
        EXPECT_EQ(1935, port);
    }

    // default app
    if (true) {
        int port; std::string tcUrl, schema, ip, vhost, app, stream, param;

        tcUrl = "rtmp://winlin.cn/"; stream= "show";
        srs_discovery_tc_url(tcUrl, schema, ip, vhost, app, stream, port, param);
        EXPECT_STREQ("rtmp", schema.c_str());
        EXPECT_STREQ("winlin.cn", ip.c_str());
        EXPECT_STREQ("winlin.cn", vhost.c_str());
        EXPECT_STREQ("__defaultApp__", app.c_str());
        EXPECT_STREQ("show", stream.c_str());
        EXPECT_EQ(1935, port);
    }

    // general url
    if (true) {
        int port; std::string tcUrl, schema, ip, vhost, app, stream, param;

        tcUrl = "rtmp://winlin.cn/live"; stream= "show";
        srs_discovery_tc_url(tcUrl, schema, ip, vhost, app, stream, port, param);
        EXPECT_STREQ("rtmp", schema.c_str());
        EXPECT_STREQ("winlin.cn", ip.c_str());
        EXPECT_STREQ("winlin.cn", vhost.c_str());
        EXPECT_STREQ("live", app.c_str());
        EXPECT_STREQ("show", stream.c_str());
        EXPECT_EQ(1935, port);
    }

    if (true) {
        int port; std::string tcUrl, schema, ip, vhost, app, stream, param;

        tcUrl = "rtmp://winlin.cn:19351/live"; stream= "show";
        srs_discovery_tc_url(tcUrl, schema, ip, vhost, app, stream, port, param);
        EXPECT_STREQ("rtmp", schema.c_str());
        EXPECT_STREQ("winlin.cn", ip.c_str());
        EXPECT_STREQ("winlin.cn", vhost.c_str());
        EXPECT_STREQ("live", app.c_str());
        EXPECT_STREQ("show", stream.c_str());
        EXPECT_EQ(19351, port);
    }

    if (true) {
        int port; std::string tcUrl, schema, ip, vhost, app, stream, param;

        tcUrl = "rtmp://winlin.cn/live"; stream= "show?key=abc";
        srs_discovery_tc_url(tcUrl, schema, ip, vhost, app, stream, port, param);
        EXPECT_STREQ("rtmp", schema.c_str());
        EXPECT_STREQ("winlin.cn", ip.c_str());
        EXPECT_STREQ("winlin.cn", vhost.c_str());
        EXPECT_STREQ("live", app.c_str());
        EXPECT_STREQ("show", stream.c_str());
        EXPECT_EQ(1935, port);
        EXPECT_STREQ("?key=abc", param.c_str());
    }

    if (true) {
        int port; std::string tcUrl, schema, ip, vhost, app, stream, param;

        tcUrl = "rtmp://winlin.cn/live"; stream= "show?key=abc&&vhost=demo.com";
        srs_discovery_tc_url(tcUrl, schema, ip, vhost, app, stream, port, param);
        EXPECT_STREQ("rtmp", schema.c_str());
        EXPECT_STREQ("winlin.cn", ip.c_str());
        EXPECT_STREQ("demo.com", vhost.c_str());
        EXPECT_STREQ("live", app.c_str());
        EXPECT_STREQ("show", stream.c_str());
        EXPECT_EQ(1935, port);
        EXPECT_STREQ("?key=abc&&vhost=demo.com", param.c_str());
    }

    // vhost in app
    if (true) {
        int port; std::string tcUrl, schema, ip, vhost, app, stream, param;

        tcUrl = "rtmp://winlin.cn/live?key=abc"; stream= "show";
        srs_discovery_tc_url(tcUrl, schema, ip, vhost, app, stream, port, param);
        EXPECT_STREQ("rtmp", schema.c_str());
        EXPECT_STREQ("winlin.cn", ip.c_str());
        EXPECT_STREQ("winlin.cn", vhost.c_str());
        EXPECT_STREQ("live", app.c_str());
        EXPECT_STREQ("show", stream.c_str());
        EXPECT_EQ(1935, port);
        EXPECT_STREQ("?key=abc", param.c_str());
    }

    if (true) {
        int port; std::string tcUrl, schema, ip, vhost, app, stream, param;

        tcUrl = "rtmp://winlin.cn/live?key=abc&&vhost=demo.com"; stream= "show";
        srs_discovery_tc_url(tcUrl, schema, ip, vhost, app, stream, port, param);
        EXPECT_STREQ("rtmp", schema.c_str());
        EXPECT_STREQ("winlin.cn", ip.c_str());
        EXPECT_STREQ("demo.com", vhost.c_str());
        EXPECT_STREQ("live", app.c_str());
        EXPECT_STREQ("show", stream.c_str());
        EXPECT_EQ(1935, port);
        EXPECT_STREQ("?key=abc&&vhost=demo.com", param.c_str());
    }

    // without stream
    if (true) {
        int port; std::string tcUrl, schema, ip, vhost, app, stream, param;

        tcUrl = "rtmp://winlin.cn/live"; stream="";
        srs_discovery_tc_url(tcUrl, schema, ip, vhost, app, stream, port, param);
        EXPECT_STREQ("rtmp", schema.c_str());
        EXPECT_STREQ("winlin.cn", ip.c_str());
        EXPECT_STREQ("winlin.cn", vhost.c_str());
        EXPECT_STREQ("live", app.c_str());
        EXPECT_STREQ("", stream.c_str());
        EXPECT_EQ(1935, port);
    }

    if (true) {
        int port; std::string tcUrl, schema, ip, vhost, app, stream, param;

        tcUrl = "rtmp://127.0.0.1:1935/live"; stream="";
        srs_discovery_tc_url(tcUrl, schema, ip, vhost, app, stream, port, param);
        EXPECT_STREQ("rtmp", schema.c_str());
        EXPECT_STREQ("127.0.0.1", ip.c_str());
        EXPECT_STREQ("127.0.0.1", vhost.c_str());
        EXPECT_STREQ("live", app.c_str());
        EXPECT_STREQ("", stream.c_str());
        EXPECT_EQ(1935, port);
    }

    if (true) {
        int port; std::string tcUrl, schema, ip, vhost, app, stream, param;

        tcUrl = "rtmp://127.0.0.1:19351/live"; stream="";
        srs_discovery_tc_url(tcUrl, schema, ip, vhost, app, stream, port, param);
        EXPECT_STREQ("rtmp", schema.c_str());
        EXPECT_STREQ("127.0.0.1", ip.c_str());
        EXPECT_STREQ("127.0.0.1", vhost.c_str());
        EXPECT_STREQ("live", app.c_str());
        EXPECT_STREQ("", stream.c_str());
        EXPECT_EQ(19351, port);
    }

    if (true) {
        int port; std::string tcUrl, schema, ip, vhost, app, stream, param;

        tcUrl = "rtmp://127.0.0.1:19351/live?vhost=demo"; stream="";
        srs_discovery_tc_url(tcUrl, schema, ip, vhost, app, stream, port, param);
        EXPECT_STREQ("rtmp", schema.c_str());
        EXPECT_STREQ("127.0.0.1", ip.c_str());
        EXPECT_STREQ("demo", vhost.c_str());
        EXPECT_STREQ("live", app.c_str());
        EXPECT_STREQ("", stream.c_str());
        EXPECT_EQ(19351, port);
    }

    // no vhost
    if (true) {
        int port; std::string tcUrl, schema, ip, vhost, app, stream, param;

        tcUrl = "rtmp://127.0.0.1:19351/live"; stream= "show";
        srs_discovery_tc_url(tcUrl, schema, ip, vhost, app, stream, port, param);
        EXPECT_STREQ("rtmp", schema.c_str());
        EXPECT_STREQ("127.0.0.1", ip.c_str());
        EXPECT_STREQ("127.0.0.1", vhost.c_str());
        EXPECT_STREQ("live", app.c_str());
        EXPECT_STREQ("show", stream.c_str());
        EXPECT_EQ(19351, port);
    }

    // ip and vhost
    if (true) {
        int port; std::string tcUrl, schema, ip, vhost, app, stream, param;

        tcUrl = "rtmp://127.0.0.1:19351/live"; stream= "show?vhost=demo";
        srs_discovery_tc_url(tcUrl, schema, ip, vhost, app, stream, port, param);
        EXPECT_STREQ("rtmp", schema.c_str());
        EXPECT_STREQ("127.0.0.1", ip.c_str());
        EXPECT_STREQ("demo", vhost.c_str());
        EXPECT_STREQ("live", app.c_str());
        EXPECT_STREQ("show", stream.c_str());
        EXPECT_EQ(19351, port);
    }

    // _definst_ at the end of app
    if (true) {
        int port; std::string tcUrl, schema, ip, vhost, app, stream, param;

        tcUrl = "rtmp://winlin.cn/live/_definst_"; stream= "show";
        srs_discovery_tc_url(tcUrl, schema, ip, vhost, app, stream, port, param);
        EXPECT_STREQ("rtmp", schema.c_str());
        EXPECT_STREQ("winlin.cn", ip.c_str());
        EXPECT_STREQ("winlin.cn", vhost.c_str());
        EXPECT_STREQ("live", app.c_str());
        EXPECT_STREQ("show", stream.c_str());
        EXPECT_EQ(1935, port);
    }
}

