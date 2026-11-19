//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai48.hpp>

#include <srs_app_config.hpp>
#include <srs_app_rtc_api.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_rtc_stun.hpp>
#include <srs_protocol_sdp.hpp>
#include <srs_utest_ai06.hpp>
#include <srs_utest_ai12.hpp>
#include <srs_utest_ai17.hpp>
#include <srs_utest_manual_config.hpp>
#include <srs_utest_manual_http.hpp>
#include <srs_utest_manual_mock.hpp>
#include <srs_utest_workflow_rtc_conn.hpp>

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <sstream>
#include <stdlib.h>
#include <string>
#include <vector>

using namespace std;

static string sped_hex_to_bytes(const char *hex)
{
    string bytes;
    for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) {
        char pair[3] = {hex[i], hex[i + 1], 0};
        bytes.push_back((char)strtol(pair, NULL, 16));
    }
    return bytes;
}

static void sped_put_u16(string &b, uint16_t v)
{
    b.push_back((char)((v >> 8) & 0xff));
    b.push_back((char)(v & 0xff));
}

static void sped_put_u32(string &b, uint32_t v)
{
    sped_put_u16(b, (uint16_t)(v >> 16));
    sped_put_u16(b, (uint16_t)(v & 0xffff));
}

static void sped_put_attr(string &b, uint16_t type, const string &value)
{
    sped_put_u16(b, type);
    sped_put_u16(b, (uint16_t)value.size());
    b.append(value);
    if (value.size() % 4) {
        b.append(4 - value.size() % 4, '\0');
    }
}

// Set the length in the header to the bytes after it plus the attribute about to be appended.
static void sped_set_length(string &b, int next_attr_size)
{
    int len = (int)b.size() - 20 + next_attr_size;
    b[2] = (char)((len >> 8) & 0xff);
    b[3] = (char)(len & 0xff);
}

// The ICE password of an answer, 32 random characters as the server makes it.
static string sped_ice_pwd()
{
    SrsRand rand;
    return rand.gen_str(32);
}

// Append MESSAGE-INTEGRITY signed with the ICE password and FINGERPRINT, as a browser ends every check.
static void sped_sign(string &b, const string &pwd)
{
    sped_set_length(b, 24);
    unsigned char mac[20];
    unsigned int mac_len = 0;
    HMAC(EVP_sha1(), pwd.data(), (int)pwd.size(), (const unsigned char *)b.data(), b.size(), mac, &mac_len);
    sped_put_attr(b, MessageIntegrity, string((const char *)mac, mac_len));

    sped_set_length(b, 8);
    string crc;
    sped_put_u32(crc, srs_crc32_ieee(b.data(), (int)b.size()) ^ 0x5354554E);
    sped_put_attr(b, Fingerprint, crc);
}

// The first fragment of a DTLS 1.2 ClientHello, 18 bytes: the record header (handshake, DTLS 1.2, epoch 0, sequence
// 0, length 5), then 5 bytes of the handshake header.
static const char *kSpedClientHelloHex = "16fefd000000000000000000050100000100";

// The SPED attributes use Chrome's codepoints, the only ones deployed while the draft's are unassigned.
VOID TEST(StunSpedAttributeTest, UsesChromeCodepoints)
{
    EXPECT_EQ(0xC070, (int)DtlsInStunData);
    EXPECT_EQ(0xC071, (int)DtlsInStunAck);
    EXPECT_EQ(4, kStunDtlsInStunMaxAcks);
}

// Chrome 158 with the SPED field trial starts its first check with an empty DTLS-IN-STUN-ACK and no DATA: the empty
// attribute is legal on the wire and means an empty list.
VOID TEST(StunSpedAttributeTest, EmptyAckIsAnEmptyList)
{
    srs_error_t err;

    string b = sped_hex_to_bytes("00010004"
                                 "2112a442000102030405060708090a0b"
                                 "c0710000");

    SrsStunPacket p;
    HELPER_ASSERT_SUCCESS(p.decode(b.data(), (int)b.size()));
    EXPECT_TRUE(p.is_binding_request());
    EXPECT_TRUE(p.has_dtls_in_stun_ack());
    EXPECT_TRUE(p.get_dtls_in_stun_acks().empty());
    EXPECT_FALSE(p.has_dtls_in_stun_data());
}

// A peer ACKs every DTLS datagram it got by its CRC-32, oldest first.
VOID TEST(StunSpedAttributeTest, AckListsCrcsInOrder)
{
    srs_error_t err;

    string b = sped_hex_to_bytes("00010010"
                                 "2112a442000102030405060708090a0b"
                                 "c071000c"
                                 "11223344"
                                 "55667788"
                                 "99aabbcc");

    SrsStunPacket p;
    HELPER_ASSERT_SUCCESS(p.decode(b.data(), (int)b.size()));
    ASSERT_TRUE(p.has_dtls_in_stun_ack());
    vector<uint32_t> acks = p.get_dtls_in_stun_acks();
    ASSERT_EQ(3u, acks.size());
    EXPECT_EQ(0x11223344u, acks[0]);
    EXPECT_EQ(0x55667788u, acks[1]);
    EXPECT_EQ(0x99aabbccu, acks[2]);
}

// Four values is the most a peer keeps to ACK.
VOID TEST(StunSpedAttributeTest, AckWithFourCrcsIsAccepted)
{
    srs_error_t err;

    string b = sped_hex_to_bytes("00010014"
                                 "2112a442000102030405060708090a0b"
                                 "c0710010"
                                 "00000001000000020000000300000004");

    SrsStunPacket p;
    HELPER_ASSERT_SUCCESS(p.decode(b.data(), (int)b.size()));
    vector<uint32_t> acks = p.get_dtls_in_stun_acks();
    ASSERT_EQ(4u, acks.size());
    EXPECT_EQ(1u, acks[0]);
    EXPECT_EQ(4u, acks[3]);
}

// An ACK with more than four values is malformed.
VOID TEST(StunSpedAttributeTest, AckWithFiveCrcsIsRefused)
{
    srs_error_t err;

    string b = sped_hex_to_bytes("00010018"
                                 "2112a442000102030405060708090a0b"
                                 "c0710014"
                                 "0000000100000002000000030000000400000005");

    SrsStunPacket p;
    HELPER_EXPECT_FAILED(p.decode(b.data(), (int)b.size()));
}

// An ACK whose length is not a whole number of CRC-32 values is malformed, even with valid padding after it.
VOID TEST(StunSpedAttributeTest, AckWithPartialCrcIsRefused)
{
    srs_error_t err;

    string b = sped_hex_to_bytes("0001000c"
                                 "2112a442000102030405060708090a0b"
                                 "c0710006"
                                 "112233445566"
                                 "0000");

    SrsStunPacket p;
    HELPER_EXPECT_FAILED(p.decode(b.data(), (int)b.size()));
}

// The DATA attribute carries one DTLS datagram, padded to 4 bytes on the wire: the datagram is kept without the
// padding, and the attribute after it is still read.
VOID TEST(StunSpedAttributeTest, DataKeepsDatagramWithoutPadding)
{
    srs_error_t err;

    string b = sped_hex_to_bytes("0001001c"
                                 "2112a442000102030405060708090a0b"
                                 "c0700012"
                                 "16fefd000000000000000000050100000100"
                                 "0000"
                                 "00250000");

    SrsStunPacket p;
    HELPER_ASSERT_SUCCESS(p.decode(b.data(), (int)b.size()));
    ASSERT_TRUE(p.has_dtls_in_stun_data());
    EXPECT_EQ(sped_hex_to_bytes(kSpedClientHelloHex), p.get_dtls_in_stun_data());
    EXPECT_TRUE(p.get_use_candidate());
    EXPECT_FALSE(p.has_dtls_in_stun_ack());
}

// A datagram whose size is a multiple of 4 has no padding after it.
VOID TEST(StunSpedAttributeTest, DataAlignedHasNoPadding)
{
    srs_error_t err;

    string b = sped_hex_to_bytes("0001001c"
                                 "2112a442000102030405060708090a0b"
                                 "c0700014"
                                 "16fefd00000000000000000007010000030000ff"
                                 "00250000");

    SrsStunPacket p;
    HELPER_ASSERT_SUCCESS(p.decode(b.data(), (int)b.size()));
    ASSERT_TRUE(p.has_dtls_in_stun_data());
    EXPECT_EQ(sped_hex_to_bytes("16fefd00000000000000000007010000030000ff"), p.get_dtls_in_stun_data());
    EXPECT_TRUE(p.get_use_candidate());
}

// The codec keeps a DATA value whose first byte is not a DTLS content type, here an RTP header; dropping it is the
// receiver's job, since the codec does not know what the session expects.
VOID TEST(StunSpedAttributeTest, DataNotDtlsIsKeptForTheCaller)
{
    srs_error_t err;

    string b = sped_hex_to_bytes("00010008"
                                 "2112a442000102030405060708090a0b"
                                 "c0700004"
                                 "80600001");

    SrsStunPacket p;
    HELPER_ASSERT_SUCCESS(p.decode(b.data(), (int)b.size()));
    ASSERT_TRUE(p.has_dtls_in_stun_data());
    EXPECT_EQ(sped_hex_to_bytes("80600001"), p.get_dtls_in_stun_data());
}

// A check as Chrome sends it once SPED is on: USERNAME, ICE-CONTROLLING, PRIORITY, an ACK of the server's datagram,
// the next fragment of the ClientHello, then MESSAGE-INTEGRITY and FINGERPRINT. The integrity covers the embedded
// datagram and the ACK, so changing either fails the check.
VOID TEST(StunSpedAttributeTest, ChromeCheckWithAckAndData)
{
    srs_error_t err;

    const string pwd = sped_ice_pwd();
    const string datagram = sped_hex_to_bytes(kSpedClientHelloHex);

    string b;
    sped_put_u16(b, BindingRequest);
    sped_put_u16(b, 0);
    sped_put_u32(b, kStunMagicCookie);
    b.append("chrome-tid-1", 12);
    sped_put_attr(b, Username, "srvufrag:cliufrag");
    sped_put_attr(b, IceControlling, string("\x5a\x13\x7c\x02\x9e\x41\xd8\x66", 8));
    string priority;
    sped_put_u32(priority, 0x6e7f1eff);
    sped_put_attr(b, Priority, priority);
    string ack;
    sped_put_u32(ack, 0xdeadbeef);
    sped_put_attr(b, DtlsInStunAck, ack);
    size_t data_pos = b.size() + 4;
    sped_put_attr(b, DtlsInStunData, datagram);
    sped_sign(b, pwd);

    SrsStunPacket p;
    HELPER_ASSERT_SUCCESS(p.decode(b.data(), (int)b.size()));
    EXPECT_TRUE(p.is_binding_request());
    EXPECT_STREQ("srvufrag", p.get_local_ufrag().c_str());
    EXPECT_STREQ("cliufrag", p.get_remote_ufrag().c_str());
    EXPECT_TRUE(p.get_ice_controlling());
    ASSERT_TRUE(p.has_dtls_in_stun_data());
    EXPECT_EQ(datagram, p.get_dtls_in_stun_data());
    ASSERT_TRUE(p.has_dtls_in_stun_ack());
    ASSERT_EQ(1u, p.get_dtls_in_stun_acks().size());
    EXPECT_EQ(0xdeadbeefu, p.get_dtls_in_stun_acks()[0]);
    HELPER_EXPECT_SUCCESS(p.check_message_integrity(pwd));

    // A changed byte of the embedded datagram breaks the integrity.
    string tampered = b;
    tampered[data_pos + 13] ^= 0x01;
    SrsStunPacket t;
    HELPER_ASSERT_SUCCESS(t.decode(tampered.data(), (int)tampered.size()));
    HELPER_EXPECT_FAILED(t.check_message_integrity(pwd));
}

// Attributes after MESSAGE-INTEGRITY are not signed, so a DATA or ACK placed there by anyone on the path is ignored.
VOID TEST(StunSpedAttributeTest, AttributesAfterIntegrityAreIgnored)
{
    srs_error_t err;

    const string pwd = sped_ice_pwd();

    string b;
    sped_put_u16(b, BindingRequest);
    sped_put_u16(b, 0);
    sped_put_u32(b, kStunMagicCookie);
    b.append("chrome-tid-2", 12);
    sped_put_attr(b, Username, "srvufrag:cliufrag");
    sped_sign(b, pwd);

    string ack;
    sped_put_u32(ack, 0xdeadbeef);
    sped_put_attr(b, DtlsInStunAck, ack);
    sped_put_attr(b, DtlsInStunData, sped_hex_to_bytes(kSpedClientHelloHex));
    sped_set_length(b, 0);

    SrsStunPacket p;
    HELPER_ASSERT_SUCCESS(p.decode(b.data(), (int)b.size()));
    HELPER_EXPECT_SUCCESS(p.check_message_integrity(pwd));
    EXPECT_FALSE(p.has_dtls_in_stun_data());
    EXPECT_FALSE(p.has_dtls_in_stun_ack());
    EXPECT_TRUE(p.get_dtls_in_stun_acks().empty());
}

// A check from a browser without SPED carries neither attribute.
VOID TEST(StunSpedAttributeTest, CheckWithoutSpedHasNoSpedAttributes)
{
    srs_error_t err;

    const string pwd = sped_ice_pwd();

    string b;
    sped_put_u16(b, BindingRequest);
    sped_put_u16(b, 0);
    sped_put_u32(b, kStunMagicCookie);
    b.append("chrome-tid-3", 12);
    sped_put_attr(b, Username, "srvufrag:cliufrag");
    sped_put_attr(b, UseCandidate, "");
    sped_sign(b, pwd);

    SrsStunPacket p;
    HELPER_ASSERT_SUCCESS(p.decode(b.data(), (int)b.size()));
    HELPER_EXPECT_SUCCESS(p.check_message_integrity(pwd));
    EXPECT_TRUE(p.get_use_candidate());
    EXPECT_FALSE(p.has_dtls_in_stun_data());
    EXPECT_FALSE(p.has_dtls_in_stun_ack());
}

// The Binding success response SRS sends for a check from cliufrag to srvufrag, seen at 192.168.3.203:18000.
static void sped_response(SrsStunPacket &p)
{
    p.set_message_type(BindingResponse);
    p.set_local_ufrag("cliufrag");
    p.set_remote_ufrag("srvufrag");
    p.set_transcation_id("srs-sped-tid");
    p.set_mapped_address(0xc0a803cb);
    p.set_mapped_port(18000);
}

static string sped_encode(SrsStunPacket &p, const string &pwd, srs_error_t *perr)
{
    char buf[1500];
    SrsBuffer stream(buf, sizeof(buf));
    *perr = p.encode(pwd, &stream);
    return string(stream.data(), stream.pos());
}

// The ICE password of the pion/stun vectors below. It is short so the vectors stay readable.
static const char *kSpedVectorIcePwd = "sped-vector-pass";

// A 21-byte server datagram, a DTLS 1.2 handshake record, so its DATA attribute needs 3 bytes of padding.
static const char *kSpedServerDatagramHex = "16fefd000000000000000000080200000400000000";

// The CRC-32 of the datagram kSpedClientHelloHex.
static const uint32_t kSpedClientHelloCrc = 0x26bf472b;

// A response without SPED attributes is the same as pion/stun v4 builds it. This passes from the start: it locks in
// today's encoding, which the SPED attributes must not change.
VOID TEST(StunSpedAttributeTest, EncodeResponseWithoutSpedMatchesPion)
{
    srs_error_t err;

    SrsStunPacket p;
    sped_response(p);
    string b = sped_encode(p, kSpedVectorIcePwd, &err);
    HELPER_ASSERT_SUCCESS(err);

    // pion/stun v4.0.0: BindingSuccess, USERNAME, XOR-MAPPED-ADDRESS, MESSAGE-INTEGRITY, FINGERPRINT.
    EXPECT_EQ(sped_hex_to_bytes("010100442112a4427372732d737065642d746964"
                                "0006001173727675667261673a636c697566726167000000"
                                "0020000800016742e1baa789"
                                "0008001432562f07239af57894e3a8802f263b0e480be1a9"
                                "80280004240c7bc9"),
              b);
}

// A response with an empty ACK list still carries the attribute, with length 0, before MESSAGE-INTEGRITY.
VOID TEST(StunSpedAttributeTest, EncodeResponseWithEmptyAckMatchesPion)
{
    srs_error_t err;

    SrsStunPacket p;
    sped_response(p);
    p.set_dtls_in_stun_acks(vector<uint32_t>());
    string b = sped_encode(p, kSpedVectorIcePwd, &err);
    HELPER_ASSERT_SUCCESS(err);

    // pion/stun v4.0.0: as above, with RawAttribute{AttrDtlsInStunAck, []byte{}} before the integrity.
    EXPECT_EQ(sped_hex_to_bytes("010100482112a4427372732d737065642d746964"
                                "0006001173727675667261673a636c697566726167000000"
                                "0020000800016742e1baa789"
                                "c0710000"
                                "0008001463ed415a722f41fac787e4f86f2ddfbbc4795d64"
                                "8028000441ecc2d1"),
              b);
}

// The response to a check that embedded the ClientHello ACKs it and embeds the server's datagram: ACK, then DATA with
// its padding, then MESSAGE-INTEGRITY and FINGERPRINT, which cover both.
VOID TEST(StunSpedAttributeTest, EncodeResponseWithAckAndDataMatchesPion)
{
    srs_error_t err;

    SrsStunPacket p;
    sped_response(p);
    vector<uint32_t> acks;
    acks.push_back(kSpedClientHelloCrc);
    acks.push_back(0xdeadbeef);
    p.set_dtls_in_stun_acks(acks);
    p.set_dtls_in_stun_data(sped_hex_to_bytes(kSpedServerDatagramHex));
    string b = sped_encode(p, kSpedVectorIcePwd, &err);
    HELPER_ASSERT_SUCCESS(err);

    // pion/stun v4.0.0: RawAttribute{AttrDtlsInStunAck, the two CRCs}, then RawAttribute{AttrDtlsInStun, datagram}.
    EXPECT_EQ(sped_hex_to_bytes("0101006c2112a4427372732d737065642d746964"
                                "0006001173727675667261673a636c697566726167000000"
                                "0020000800016742e1baa789"
                                "c071000826bf472bdeadbeef"
                                "c070001516fefd000000000000000000080200000400000000000000"
                                "0008001466a0788fdb57f539ce9b95e0e010e7c8794e22bc"
                                "8028000413424306"),
              b);
    EXPECT_EQ(kSpedClientHelloCrc, srs_crc32_ieee(sped_hex_to_bytes(kSpedClientHelloHex).data(), 18));
}

// The browser reads back what SRS embedded: the datagram, the ACKs, and a valid integrity with the ICE password.
VOID TEST(StunSpedAttributeTest, EncodedResponseDecodesBack)
{
    srs_error_t err;

    const string pwd = sped_ice_pwd();
    const string datagram = sped_hex_to_bytes(kSpedServerDatagramHex);

    SrsStunPacket p;
    sped_response(p);
    vector<uint32_t> acks;
    acks.push_back(kSpedClientHelloCrc);
    p.set_dtls_in_stun_acks(acks);
    p.set_dtls_in_stun_data(datagram);
    string b = sped_encode(p, pwd, &err);
    HELPER_ASSERT_SUCCESS(err);

    SrsStunPacket d;
    HELPER_ASSERT_SUCCESS(d.decode(b.data(), (int)b.size()));
    EXPECT_TRUE(d.is_binding_response());
    HELPER_EXPECT_SUCCESS(d.check_message_integrity(pwd));
    ASSERT_TRUE(d.has_dtls_in_stun_data());
    EXPECT_EQ(datagram, d.get_dtls_in_stun_data());
    ASSERT_TRUE(d.has_dtls_in_stun_ack());
    ASSERT_EQ(1u, d.get_dtls_in_stun_acks().size());
    EXPECT_EQ(kSpedClientHelloCrc, d.get_dtls_in_stun_acks()[0]);
}

// SRS's DTLS 1.2 server flight is one 707-byte datagram, which fits. With USERNAME (24 bytes), XOR-MAPPED-ADDRESS (12)
// and one ACK (8), the DATA attribute's header and padded datagram may take the other 1124 bytes of the body.
VOID TEST(StunSpedAttributeTest, DataFitsUpToBodyLimit)
{
    srs_error_t err;

    SrsStunPacket p;
    sped_response(p);
    vector<uint32_t> acks;
    acks.push_back(kSpedClientHelloCrc);
    p.set_dtls_in_stun_acks(acks);

    EXPECT_TRUE(p.dtls_in_stun_data_fits(707));
    EXPECT_TRUE(p.dtls_in_stun_data_fits(1120));
    // One byte more pads to 1124, four over the limit.
    EXPECT_FALSE(p.dtls_in_stun_data_fits(1121));
    // A post-quantum ClientHello-sized datagram does not fit one message.
    EXPECT_FALSE(p.dtls_in_stun_data_fits(1440));

    p.set_dtls_in_stun_data(string(1120, '\x16'));
    string b = sped_encode(p, sped_ice_pwd(), &err);
    HELPER_ASSERT_SUCCESS(err);
    EXPECT_EQ(20 + kStunSpedMaxBodySize + 24 + 8, (int)b.size());
}

// Without an ACK attribute the datagram has those 8 bytes too.
VOID TEST(StunSpedAttributeTest, DataFitsCountsTheAck)
{
    SrsStunPacket p;
    sped_response(p);

    EXPECT_TRUE(p.dtls_in_stun_data_fits(1128));
    EXPECT_FALSE(p.dtls_in_stun_data_fits(1129));

    p.set_dtls_in_stun_acks(vector<uint32_t>());
    EXPECT_TRUE(p.dtls_in_stun_data_fits(1124));
    EXPECT_FALSE(p.dtls_in_stun_data_fits(1125));
}

// Encoding a datagram that does not fit is refused, so a caller that skipped the check never sends an oversized check
// response.
VOID TEST(StunSpedAttributeTest, EncodeRefusesDataOverBodyLimit)
{
    srs_error_t err;

    SrsStunPacket p;
    sped_response(p);
    p.set_dtls_in_stun_data(string(1129, '\x16'));
    sped_encode(p, sped_ice_pwd(), &err);
    HELPER_EXPECT_FAILED(err);
}

// A peer refuses an ACK with more than four values, so SRS never writes one.
VOID TEST(StunSpedAttributeTest, EncodeRefusesTooManyAcks)
{
    srs_error_t err;

    SrsStunPacket p;
    sped_response(p);
    vector<uint32_t> acks;
    for (int i = 0; i < kStunDtlsInStunMaxAcks + 1; i++) {
        acks.push_back(i + 1);
    }
    p.set_dtls_in_stun_acks(acks);
    sped_encode(p, sped_ice_pwd(), &err);
    HELPER_EXPECT_FAILED(err);
}

VOID TEST(RtcSpedConfigTest, DefaultIsOff)
{
    srs_error_t err;

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF));
        EXPECT_FALSE(conf.get_rtc_sped("__defaultVhost__"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on;}}"));
        EXPECT_FALSE(conf.get_rtc_sped("test.com"));
        EXPECT_FALSE(conf.get_rtc_sped("other.com"));
    }
}

VOID TEST(RtcSpedConfigTest, ConfigOnAndOff)
{
    srs_error_t err;

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on; sped on;}}"));
        EXPECT_TRUE(conf.get_rtc_sped("test.com"));
        EXPECT_FALSE(conf.get_rtc_sped("other.com"));
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{enabled on; sped off;}}"));
        EXPECT_FALSE(conf.get_rtc_sped("test.com"));
    }
}

VOID TEST(RtcSpedConfigTest, EnvOverridesEveryVhost)
{
    srs_error_t err;

    if (true) {
        MockSrsConfig conf;
        SrsSetEnvConfig(conf, sped, "SRS_VHOST_RTC_SPED", "on");
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{sped off;}}"));
        EXPECT_TRUE(conf.get_rtc_sped("test.com"));
        EXPECT_TRUE(conf.get_rtc_sped("__defaultVhost__"));
    }

    if (true) {
        MockSrsConfig conf;
        SrsSetEnvConfig(conf, sped, "SRS_VHOST_RTC_SPED", "off");
        HELPER_ASSERT_SUCCESS(conf.mock_parse(_MIN_OK_CONF "vhost test.com{rtc{sped on;}}"));
        EXPECT_FALSE(conf.get_rtc_sped("test.com"));
    }
}

VOID TEST(RtcSpedConfigTest, BadValueIsRefused)
{
    const char *bad[] = {"yes", "true", "1", "ON", "of"};
    for (int i = 0; i < (int)(sizeof(bad) / sizeof(bad[0])); i++) {
        string value = bad[i];

        if (true) {
            string c = string(_MIN_OK_CONF) + "vhost test.com{rtc{sped " + value + ";}}";
            MockSrsConfig conf;
            srs_error_t err = conf.mock_parse(c);
            EXPECT_TRUE(err != srs_success) << c;
            EXPECT_NE(string::npos, srs_error_desc(err).find("sped=" + value)) << c;
            srs_freep(err);
        }

        if (true) {
            MockSrsConfig conf;
            SrsSetEnvConfig(conf, sped, "SRS_VHOST_RTC_SPED", value);
            srs_error_t err = conf.mock_parse(_MIN_OK_CONF);
            EXPECT_TRUE(err != srs_success) << value;
            EXPECT_NE(string::npos, srs_error_desc(err).find("SRS_VHOST_RTC_SPED=" + value)) << value;
            srs_freep(err);
        }
    }
}

// A BUNDLE offer as Chrome makes it, with its ice-options on every media.
static string sped_offer(const string &session_ice_options, const string &media_ice_options)
{
    string sdp = "v=0\r\n"
                 "o=- 123456 2 IN IP4 127.0.0.1\r\n"
                 "s=-\r\n"
                 "t=0 0\r\n"
                 "a=group:BUNDLE 0 1\r\n"
                 "a=msid-semantic: WMS stream\r\n";
    if (!session_ice_options.empty()) {
        sdp += "a=ice-options:" + session_ice_options + "\r\n";
    }

    const char *medias[] = {"m=audio 9 UDP/TLS/RTP/SAVPF 111\r\n", "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"};
    const char *rtpmaps[] = {"a=rtpmap:111 opus/48000/2\r\n", "a=rtpmap:96 H264/90000\r\n"};
    for (int i = 0; i < 2; i++) {
        sdp += medias[i];
        sdp += "c=IN IP4 0.0.0.0\r\n"
               "a=ice-ufrag:chromeufrag\r\n"
               "a=ice-pwd:chromepassword\r\n";
        if (!media_ice_options.empty()) {
            sdp += "a=ice-options:" + media_ice_options + "\r\n";
        }
        sdp += "a=setup:actpass\r\n";
        sdp += string("a=mid:") + (i ? "1" : "0") + "\r\n";
        sdp += "a=sendrecv\r\n"
               "a=rtcp-mux\r\n";
        sdp += rtpmaps[i];
    }
    return sdp;
}

// Chrome 158 with its SPED field trial offers "trickle sped googspedv1" on every media.
VOID TEST(RtcSpedSdpTest, ChromeOfferHasSped)
{
    srs_error_t err;

    SrsSdp offer;
    HELPER_ASSERT_SUCCESS(offer.parse(sped_offer("", "trickle sped googspedv1")));
    EXPECT_TRUE(offer.has_ice_option("sped"));
    EXPECT_TRUE(offer.has_ice_option("googspedv1"));
    EXPECT_TRUE(offer.has_ice_option("trickle"));
    EXPECT_FALSE(offer.has_ice_option("spe"));
    EXPECT_FALSE(offer.has_ice_option(""));
}

// Chrome without the trial offers only trickle.
VOID TEST(RtcSpedSdpTest, ChromeOfferWithoutTrialHasNoSped)
{
    srs_error_t err;

    SrsSdp offer;
    HELPER_ASSERT_SUCCESS(offer.parse(sped_offer("", "trickle")));
    EXPECT_FALSE(offer.has_ice_option("sped"));
    EXPECT_TRUE(offer.has_ice_option("trickle"));

    SrsSdp none;
    HELPER_ASSERT_SUCCESS(none.parse(sped_offer("", "")));
    EXPECT_FALSE(none.has_ice_option("sped"));
}

// googspedv1 is Chrome's private token: it contains "sped" but is not the draft's option.
VOID TEST(RtcSpedSdpTest, GoogSpedAloneIsNotSped)
{
    srs_error_t err;

    SrsSdp offer;
    HELPER_ASSERT_SUCCESS(offer.parse(sped_offer("", "trickle googspedv1")));
    EXPECT_FALSE(offer.has_ice_option("sped"));
    EXPECT_TRUE(offer.has_ice_option("googspedv1"));
}

// RFC 8839 allows ice-options at the session level too, as other clients such as OBS put their ICE
// attributes there.
VOID TEST(RtcSpedSdpTest, SessionLevelOptionIsRead)
{
    srs_error_t err;

    SrsSdp offer;
    HELPER_ASSERT_SUCCESS(offer.parse(sped_offer("sped", "")));
    EXPECT_TRUE(offer.has_ice_option("sped"));

    SrsSdp both;
    HELPER_ASSERT_SUCCESS(both.parse(sped_offer("trickle", "sped")));
    EXPECT_TRUE(both.has_ice_option("sped"));
    EXPECT_TRUE(both.has_ice_option("trickle"));
}

// Build the answer as the API and the session do: the API copies the vhost's sped into the local
// SDP's session config, and the session negotiates it against the offer. Returns the encoded answer.
static srs_error_t sped_generate_answer(bool config_sped, const string &offer_ice_options, SrsSdp &local_sdp, string &answer)
{
    srs_error_t err = srs_success;

    MockRtcAsyncTaskExecutor mock_exec;
    SrsContextId cid;
    SrsUniquePtr<SrsRtcConnection> conn(new SrsRtcConnection(&mock_exec, cid));

    MockConnectionManagerForExpire conn_manager;
    MockAppConfig config;
    MockDtlsCertificate certificate;
    MockAppFactoryForRtcConn app_factory;
    MockRandForHandshake rand;
    certificate.fingerprint_ = "test-fingerprint";
    app_factory.mock_protocol_utility_ = new MockProtocolUtility("192.168.1.100");

    conn->conn_manager_ = &conn_manager;
    conn->config_ = &config;
    conn->dtls_certificate_ = &certificate;
    conn->app_factory_ = &app_factory;
    srs_freep(conn->rand_);
    conn->rand_ = &rand;

    SrsRtcUserConfig ruc;
    srs_freep(ruc.req_);
    ruc.req_ = new MockRtcAsyncCallRequest("test.vhost", "live", "stream1");
    if ((err = ruc.remote_sdp_.parse(sped_offer("", offer_ice_options))) == srs_success) {
        local_sdp.session_config_.dtls_role_ = "passive";
        local_sdp.session_config_.dtls_version_ = "auto";
        local_sdp.session_config_.sped_ = config_sped;
        local_sdp.media_descs_.push_back(SrsMediaDesc("audio"));
        local_sdp.media_descs_.back().mid_ = "0";

        std::string username;
        err = conn->generate_local_sdp(&ruc, local_sdp, username);
    }

    conn->conn_manager_ = _srs_conn_manager;
    conn->config_ = _srs_config;
    conn->dtls_certificate_ = _srs_rtc_dtls_certificate;
    conn->app_factory_ = _srs_app_factory;
    conn->rand_ = NULL;

    if (err != srs_success) {
        return err;
    }

    std::ostringstream os;
    if ((err = local_sdp.encode(os)) != srs_success) {
        return err;
    }
    answer = os.str();
    return err;
}

static int sped_count(const string &s, const string &what)
{
    int n = 0;
    for (size_t pos = s.find(what); pos != string::npos; pos = s.find(what, pos + what.size())) {
        n++;
    }
    return n;
}

// SPED is used only when the vhost enables it and the offer has the draft's token. Chrome reads
// the answer's session-level ice-options for every BUNDLE transport.
VOID TEST(RtcSpedSdpTest, AnswerMatrix)
{
    srs_error_t err;

    struct Case {
        bool config_sped;
        const char *offer_ice_options;
        bool negotiated;
    };
    Case cases[] = {
        {true, "trickle sped googspedv1", true},
        {true, "sped", true},
        {true, "trickle", false},
        {true, "", false},
        {true, "trickle googspedv1", false},
        {false, "trickle sped googspedv1", false},
        {false, "trickle", false},
        {false, "", false},
    };

    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        Case &c = cases[i];

        SrsSdp local_sdp;
        string answer;
        HELPER_ASSERT_SUCCESS(sped_generate_answer(c.config_sped, c.offer_ice_options, local_sdp, answer));

        EXPECT_EQ(c.negotiated, local_sdp.session_negotiate_.sped_) << i;
        EXPECT_EQ(c.config_sped, local_sdp.session_config_.sped_) << i;
        EXPECT_EQ(c.negotiated ? 1 : 0, sped_count(answer, "a=ice-options:")) << i << answer;

        size_t option = answer.find("a=ice-options:sped\r\n");
        size_t media = answer.find("m=");
        if (c.negotiated) {
            ASSERT_NE(string::npos, option) << i << answer;
            ASSERT_NE(string::npos, media) << i << answer;
            EXPECT_LT(option, media) << i << answer;
        }

        // The rest of the negotiation is unchanged.
        EXPECT_EQ("passive", local_sdp.session_negotiate_.dtls_role_) << i;
        EXPECT_EQ("auto", local_sdp.session_negotiate_.dtls_version_) << i;
    }
}

// The play API copies the vhost's sped into the session config it hands the server.
VOID TEST(RtcSpedApiTest, PlayReadsConfig)
{
    srs_error_t err;

    bool values[] = {true, false};
    for (int i = 0; i < 2; i++) {
        SrsUniquePtr<MockRtcApiServerForPlay> mock_server(new MockRtcApiServerForPlay());
        SrsUniquePtr<SrsGoApiRtcPlay> api(new SrsGoApiRtcPlay(mock_server.get()));

        SrsUniquePtr<MockAppConfigForRtcPlay> mock_config(new MockAppConfigForRtcPlay());
        mock_config->sped_ = values[i];
        SrsUniquePtr<MockRtcSourceManager> mock_rtc_sources(new MockRtcSourceManager());
        SrsUniquePtr<MockLiveSourceManager> mock_live_sources(new MockLiveSourceManager());
        SrsUniquePtr<MockHttpHooksForRtcPlay> mock_hooks(new MockHttpHooksForRtcPlay());
        SrsUniquePtr<MockSecurity> mock_security(new MockSecurity());

        api->config_ = mock_config.get();
        api->rtc_sources_ = mock_rtc_sources.get();
        api->live_sources_ = mock_live_sources.get();
        api->hooks_ = mock_hooks.get();
        srs_freep(api->security_);
        api->security_ = mock_security.get();

        SrsUniquePtr<SrsRtcUserConfig> ruc(new SrsRtcUserConfig());
        ruc->req_->vhost_ = "__defaultVhost__";
        ruc->req_->app_ = "live";
        ruc->req_->stream_ = "livestream";
        ruc->req_->ip_ = "192.168.1.100";
        ruc->remote_sdp_str_ = sped_offer("", "trickle sped googspedv1");
        HELPER_EXPECT_SUCCESS(ruc->remote_sdp_.parse(ruc->remote_sdp_str_));

        SrsUniquePtr<MockResponseWriter> mock_writer(new MockResponseWriter());
        SrsUniquePtr<MockHttpMessageForRtcApi> mock_request(new MockHttpMessageForRtcApi());
        HELPER_EXPECT_SUCCESS(api->serve_http(mock_writer.get(), mock_request.get(), ruc.get()));

        EXPECT_TRUE(mock_server->create_session_called_);
        EXPECT_EQ(values[i], mock_server->session_config_.sped_) << i;
        srs_freep(mock_server->session_);

        api->config_ = NULL;
        api->rtc_sources_ = NULL;
        api->live_sources_ = NULL;
        api->hooks_ = NULL;
        api->security_ = NULL;
    }
}

// The publish API copies the vhost's sped into the session config it hands the server.
VOID TEST(RtcSpedApiTest, PublishReadsConfig)
{
    srs_error_t err;

    bool values[] = {true, false};
    for (int i = 0; i < 2; i++) {
        SrsUniquePtr<MockRtcApiServerForPlay> mock_server(new MockRtcApiServerForPlay());
        SrsUniquePtr<SrsGoApiRtcPublish> api(new SrsGoApiRtcPublish(mock_server.get()));

        SrsUniquePtr<MockAppConfigForRtcPlay> mock_config(new MockAppConfigForRtcPlay());
        mock_config->sped_ = values[i];
        SrsUniquePtr<MockHttpHooksForRtcPlay> mock_hooks(new MockHttpHooksForRtcPlay());
        SrsUniquePtr<MockSecurity> mock_security(new MockSecurity());
        SrsUniquePtr<MockStatisticForRtcApi> mock_stat(new MockStatisticForRtcApi());

        api->config_ = mock_config.get();
        api->hooks_ = mock_hooks.get();
        srs_freep(api->security_);
        api->security_ = mock_security.get();
        api->stat_ = mock_stat.get();

        SrsUniquePtr<SrsRtcUserConfig> ruc(new SrsRtcUserConfig());
        ruc->req_->vhost_ = "__defaultVhost__";
        ruc->req_->app_ = "live";
        ruc->req_->stream_ = "livestream";
        ruc->req_->ip_ = "127.0.0.1";
        ruc->publish_ = true;
        ruc->remote_sdp_str_ = sped_offer("", "trickle sped googspedv1");
        HELPER_EXPECT_SUCCESS(ruc->remote_sdp_.parse(ruc->remote_sdp_str_));

        SrsUniquePtr<MockResponseWriter> mock_writer(new MockResponseWriter());
        SrsUniquePtr<MockHttpMessageForRtcApi> mock_message(new MockHttpMessageForRtcApi());
        HELPER_EXPECT_SUCCESS(api->serve_http(mock_writer.get(), mock_message.get(), ruc.get()));

        EXPECT_TRUE(mock_server->create_session_called_);
        EXPECT_EQ(values[i], mock_server->session_config_.sped_) << i;
        srs_freep(mock_server->session_);

        api->config_ = NULL;
        api->hooks_ = NULL;
        api->security_ = NULL;
        api->stat_ = NULL;
    }
}
