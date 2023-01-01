//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_kernel_rtc_rtp.hpp>

#include <arpa/inet.h>
#include <fcntl.h>
#include <sstream>
using namespace std;

#include <srs_kernel_log.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_kernel_flv.hpp>

#include <srs_kernel_kbps.hpp>

SrsPps* _srs_pps_objs_rtps = NULL;
SrsPps* _srs_pps_objs_rraw = NULL;
SrsPps* _srs_pps_objs_rfua = NULL;
SrsPps* _srs_pps_objs_rbuf = NULL;
SrsPps* _srs_pps_objs_rothers = NULL;

/* @see https://tools.ietf.org/html/rfc1889#section-5.1
  0                   1                   2                   3
  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |V=2|P|X|  CC   |M|     PT      |       sequence number         |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |                           timestamp                           |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 |           synchronization source (SSRC) identifier            |
 +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
 |            contributing source (CSRC) identifiers             |
 |                             ....                              |
 +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
uint32_t srs_rtp_fast_parse_ssrc(char* buf, int size)
{
    if (size < 12) {
        return 0;
    }

    uint32_t value = 0;
    char* pp = (char*)&value;

    char* p = buf + 8;
    pp[3] = *p++;
    pp[2] = *p++;
    pp[1] = *p++;
    pp[0] = *p++;
    return value;
}
uint8_t srs_rtp_fast_parse_pt(char* buf, int size)
{
    if (size < 12) {
        return 0;
    }
    return buf[1] & 0x7f;
}
srs_error_t srs_rtp_fast_parse_twcc(char* buf, int size, uint8_t twcc_id, uint16_t& twcc_sn)
{
    srs_error_t err = srs_success;

    int need_size = 12 /*rtp head fix len*/ + 4 /* extension header len*/ + 3 /* twcc extension len*/;
    if(size < (need_size)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "required %d bytes, actual %d", need_size, size);
    }

    uint8_t first = buf[0];
    bool extension = (first & 0x10);
    uint8_t cc = (first & 0x0F);

    if(!extension) {
        return srs_error_new(ERROR_RTC_RTP, "no extension in rtp");
    }

    need_size += cc * 4; // csrc size
    if(size < (need_size)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "required %d bytes, actual %d", need_size, size);
    }
    buf += 12 + 4*cc;

    uint16_t value = *((uint16_t*)buf);
    value = ntohs(value);
    if(0xBEDE != value) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "no support this type(0x%02x) extension", value);
    }
    buf += 2;
    
    uint16_t extension_length = ntohs(*((uint16_t*)buf));
    buf += 2;
    extension_length *= 4;
    need_size += extension_length; // entension size
    if(size < (need_size)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "required %d bytes, actual %d", need_size, size);
    }

    while(extension_length > 0) {
        uint8_t v = buf[0];
        buf++;
        extension_length--;
        if(0 == v) {
            continue;
        }

        uint8_t id = (v & 0xF0) >>4;
        uint8_t len = (v & 0x0F) + 1;

        if(id == twcc_id) {
            twcc_sn = ntohs(*((uint16_t*)buf));
            return err;
        } else {
            buf += len;
            extension_length -= len;
        }
    }


    return err;
}

// If value is newer than pre_value，return true; otherwise false
bool srs_seq_is_newer(uint16_t value, uint16_t pre_value)
{
    return srs_rtp_seq_distance(pre_value, value) > 0;
}

bool srs_seq_is_rollback(uint16_t value, uint16_t pre_value)
{
    if(srs_seq_is_newer(value, pre_value)) {
        return pre_value > value;
    }
    return false;
}

// If value is newer then pre_value, return positive, otherwise negative.
int32_t srs_seq_distance(uint16_t value, uint16_t pre_value)
{
    return srs_rtp_seq_distance(pre_value, value);
}

SrsRtpExtensionTypes::SrsRtpExtensionTypes()
{
    memset(ids_, kRtpExtensionNone, sizeof(ids_));
}

SrsRtpExtensionTypes::~SrsRtpExtensionTypes()
{
}

bool SrsRtpExtensionTypes::register_by_uri(int id, std::string uri)
{
    for (int i = 0; i < (int)(sizeof(kExtensions)/sizeof(kExtensions[0])); ++i) {
        if (kExtensions[i].uri == uri) {
            return register_id(id, kExtensions[i].type, kExtensions[i].uri);
        }
    }
    return false;
}

bool SrsRtpExtensionTypes::register_id(int id, SrsRtpExtensionType type, std::string uri)
{
    if (id < 1 || id > 255) {
        return false;
    }

    ids_[type] = static_cast<uint8_t>(id);
    return true;
}

SrsRtpExtensionType SrsRtpExtensionTypes::get_type(int id) const
{
    for (int type = kRtpExtensionNone + 1; type < kRtpExtensionNumberOfExtensions; ++type) {
        if (ids_[type] == id) {
            return static_cast<SrsRtpExtensionType>(type);
        }
    }
    return kInvalidType;
}

SrsRtpExtensionTwcc::SrsRtpExtensionTwcc()
{
    has_twcc_ = false;
    id_ = 0;
    sn_ = 0;
}

SrsRtpExtensionTwcc::~SrsRtpExtensionTwcc()
{
}

srs_error_t SrsRtpExtensionTwcc::decode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;

    //   0                   1                   2
    //   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3
    //  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    //  |  ID   | L=1   |transport wide sequence number |
    //  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    if (!buf->require(1)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", 1);
    }
    uint8_t v = buf->read_1bytes();

    id_ = (v & 0xF0) >> 4;
    uint8_t len = (v & 0x0F);
    if(!id_ || len != 1) {
        return srs_error_new(ERROR_RTC_RTP, "invalid twcc id=%d, len=%d", id_, len);
    }

    if (!buf->require(2)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", 2);
    }
    sn_ = buf->read_2bytes();

    has_twcc_ = true;
    return err;
}

uint64_t SrsRtpExtensionTwcc::nb_bytes()
{
    return 3;
}

srs_error_t SrsRtpExtensionTwcc::encode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;

    if(!buf->require(3)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", 3);
    }
 
    uint8_t id_len = (id_ & 0x0F)<< 4| 0x01;
    buf->write_1bytes(id_len);
    buf->write_2bytes(sn_);
    
    return err;
}


uint8_t SrsRtpExtensionTwcc::get_id()
{
    return id_;
}

void SrsRtpExtensionTwcc::set_id(uint8_t id)
{
    id_ = id;
    has_twcc_ = true;
}

uint16_t SrsRtpExtensionTwcc::get_sn()
{
    return sn_;
}

void SrsRtpExtensionTwcc::set_sn(uint16_t sn)
{
    sn_ = sn;
    has_twcc_ = true;
}

SrsRtpExtensionOneByte::SrsRtpExtensionOneByte()
{
    has_ext_ = false;
    id_ = 0;
    value_ = 0;
}

SrsRtpExtensionOneByte::~SrsRtpExtensionOneByte()
{
}

void SrsRtpExtensionOneByte::set_id(int id)
{
    id_ = id;
    has_ext_ = true;
}

void SrsRtpExtensionOneByte::set_value(uint8_t value)
{
    value_ = value;
    has_ext_ = true;
}

srs_error_t SrsRtpExtensionOneByte::decode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;

    if (!buf->require(2)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", 2);
    }
    uint8_t v = buf->read_1bytes();

    id_ = (v & 0xF0) >> 4;
    uint8_t len = (v & 0x0F);
    if(!id_ || len != 0) {
        return srs_error_new(ERROR_RTC_RTP, "invalid rtp extension id=%d, len=%d", id_, len);
    }

    value_ = buf->read_1bytes();

    has_ext_ = true;
    return err;
}

srs_error_t SrsRtpExtensionOneByte::encode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;

    if (!buf->require(2)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", 2);
    }

    uint8_t id_len = (id_ & 0x0F)<< 4 | 0x00;
    buf->write_1bytes(id_len);
    buf->write_1bytes(value_);

    return err;
}

SrsRtpExtensions::SrsRtpExtensions()
{
    types_ = NULL;
    has_ext_ = false;
    decode_twcc_extension_ = false;
}

SrsRtpExtensions::~SrsRtpExtensions()
{
}

srs_error_t SrsRtpExtensions::decode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;

    /* @see https://tools.ietf.org/html/rfc3550#section-5.3.1
        0                   1                   2                   3
        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |      defined by profile       |           length              |
        +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
        |                        header extension                       |
        |                             ....                              |
    */
    if (!buf->require(4)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires 4 bytes");
    }
    uint16_t profile_id = buf->read_2bytes();
    uint16_t extension_length = buf->read_2bytes();

    // @see: https://tools.ietf.org/html/rfc5285#section-4.2
    if (profile_id == 0xBEDE) {
        SrsBuffer xbuf(buf->head(), extension_length * 4);
        buf->skip(extension_length * 4);
        return decode_0xbede(&xbuf);
    }  else if (profile_id == 0x1000) {
        buf->skip(extension_length * 4);
    } else {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "fail to parse extension");
    }
    return err;
}

srs_error_t SrsRtpExtensions::decode_0xbede(SrsBuffer* buf)
{
    srs_error_t err = srs_success;

    while (!buf->empty()) {
        // The first byte maybe padding or id+len.
        if (!buf->require(1)) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", 1);
        }
        uint8_t v = *((uint8_t*)buf->head());

        // Padding, ignore
        if(v == 0) {
            buf->skip(1);
            continue;
        }

        //  0
        //  0 1 2 3 4 5 6 7
        // +-+-+-+-+-+-+-+-+
        // |  ID   |  len  |
        // +-+-+-+-+-+-+-+-+
        // Note that 'len' is the header extension element length, which is the
        // number of bytes - 1.
        uint8_t id = (v & 0xF0) >> 4;
        uint8_t len = (v & 0x0F) + 1;

        SrsRtpExtensionType xtype = types_? types_->get_type(id) : kRtpExtensionNone;
        if (xtype == kRtpExtensionTransportSequenceNumber) {
            if (decode_twcc_extension_) {
                if ((err = twcc_.decode(buf)) != srs_success) {
                    return srs_error_wrap(err, "decode twcc extension");
                }
                has_ext_ = true;
            } else {
                if (!buf->require(len+1)) {
                    return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", len+1);
                }
                buf->skip(len + 1);
            }
        } else if (xtype == kRtpExtensionAudioLevel) {
            if((err = audio_level_.decode(buf)) != srs_success) {
                return srs_error_wrap(err, "decode audio level extension");
            }
            has_ext_ = true;
        } else {
            buf->skip(1 + len);
        }
    }

    return err;
}

uint64_t SrsRtpExtensions::nb_bytes()
{
    int size =  4 + (twcc_.exists() ? twcc_.nb_bytes() : 0);
    size += (audio_level_.exists() ? audio_level_.nb_bytes() : 0);
    // add padding
    size += (size % 4 == 0) ? 0 : (4 - size % 4);
    return size;
}

srs_error_t SrsRtpExtensions::encode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;

    buf->write_2bytes(0xBEDE);

    // Write length.
    int len = 0;

    if (twcc_.exists()) {
        len += twcc_.nb_bytes();
    }

    if (audio_level_.exists()) {
        len += audio_level_.nb_bytes();
    }

    int padding_count = (len % 4 == 0) ? 0 : (4 - len % 4);
    len += padding_count;

    if (!buf->require(len)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", len);
    }

    buf->write_2bytes(len / 4);

    // Write extensions.
    if (twcc_.exists()) {
        if ((err = twcc_.encode(buf)) != srs_success) {
            return srs_error_wrap(err, "encode twcc extension");
        }
    }

    if (audio_level_.exists()) {
        if (srs_success != (err = audio_level_.encode(buf))) {
            return srs_error_wrap(err, "encode audio level extension");
        }
    }

    // add padding
    if (padding_count) {
        memset(buf->head(), 0, padding_count);
        buf->skip(padding_count);
    }

    return err;
}

void SrsRtpExtensions::set_types_(SrsRtpExtensionTypes* types)
{
    types_ = types;
}

srs_error_t SrsRtpExtensions::get_twcc_sequence_number(uint16_t& twcc_sn)
{
    if (twcc_.exists()) {
        twcc_sn = twcc_.get_sn();
        return srs_success;
    }
    return srs_error_new(ERROR_RTC_RTP_MUXER, "not find twcc sequence number");
}
    
srs_error_t SrsRtpExtensions::set_twcc_sequence_number(uint8_t id, uint16_t sn)
{
    has_ext_ = true;
    twcc_.set_id(id);
    twcc_.set_sn(sn);
    return srs_success;
}

srs_error_t SrsRtpExtensions::get_audio_level(uint8_t& level)
{
    if(audio_level_.exists()) {
        level = audio_level_.get_value();
        return srs_success;
    }
    return srs_error_new(ERROR_RTC_RTP_MUXER, "not find rtp extension audio level");
}

srs_error_t SrsRtpExtensions::set_audio_level(int id, uint8_t level)
{
    has_ext_ = true;
    audio_level_.set_id(id);
    audio_level_.set_value(level);
    return srs_success;
}

SrsRtpHeader::SrsRtpHeader()
{
    cc               = 0;
    marker           = false;
    payload_type     = 0;
    sequence         = 0;
    timestamp        = 0;
    ssrc             = 0;
    padding_length   = 0;
    ignore_padding_  = false;
    memset(csrc, 0, sizeof(csrc));
}

SrsRtpHeader::~SrsRtpHeader()
{
}

srs_error_t SrsRtpHeader::decode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;

    if (!buf->require(kRtpHeaderFixedSize)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d+ bytes", kRtpHeaderFixedSize);
    }

    /* @see https://tools.ietf.org/html/rfc1889#section-5.1
      0                   1                   2                   3
      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |V=2|P|X|  CC   |M|     PT      |       sequence number         |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |                           timestamp                           |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
     |           synchronization source (SSRC) identifier            |
     +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
     |            contributing source (CSRC) identifiers             |
     |                             ....                              |
     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    */

    uint8_t first = buf->read_1bytes();
    bool padding = (first & 0x20);
    bool extension = (first & 0x10);
    cc = (first & 0x0F);

    uint8_t second = buf->read_1bytes();
    marker = (second & 0x80);
    payload_type = (second & 0x7F);

    sequence = buf->read_2bytes();
    timestamp = buf->read_4bytes();
    ssrc = buf->read_4bytes();

    int ext_bytes = nb_bytes() - kRtpHeaderFixedSize;
    if (!buf->require(ext_bytes)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d+ bytes", ext_bytes);
    }

    for (uint8_t i = 0; i < cc; ++i) {
        csrc[i] = buf->read_4bytes();
    }    

    if (extension) {
        if ((err = parse_extensions(buf)) != srs_success) {
            return srs_error_wrap(err, "fail to parse extension");
        }
    }

    if (padding && !ignore_padding_ && !buf->empty()) {
        padding_length = *(reinterpret_cast<uint8_t*>(buf->data() + buf->size() - 1));
        if (!buf->require(padding_length)) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "padding requires %d bytes", padding_length);
        }
    }

    return err;
}

srs_error_t SrsRtpHeader::parse_extensions(SrsBuffer* buf) {
    srs_error_t err = srs_success;

    if(srs_success != (err = extensions_.decode(buf))) {
        return srs_error_wrap(err, "decode rtp extension");
    }

    return err;
}

srs_error_t SrsRtpHeader::encode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;

    // Encode the RTP fix header, 12bytes.
    // @see https://tools.ietf.org/html/rfc1889#section-5.1
    // The version, padding, extension and cc, total 1 byte.
    uint8_t v = 0x80 | cc;
    if (padding_length > 0) {
        v |= 0x20;
    }
    if (extensions_.exists()) {
        v |= 0x10;
    }
    buf->write_1bytes(v);

    // The marker and payload type, total 1 byte.
    v = payload_type;
    if (marker) {
        v |= kRtpMarker;
    }
    buf->write_1bytes(v);

    // The sequence number, 2 bytes.
    buf->write_2bytes(sequence);

    // The timestamp, 4 bytes.
    buf->write_4bytes(timestamp);

    // The SSRC, 4 bytes.
    buf->write_4bytes(ssrc);

    // The CSRC list: 0 to 15 items, each is 4 bytes.
    for (size_t i = 0; i < cc; ++i) {
        buf->write_4bytes(csrc[i]);
    }

    if (extensions_.exists()) {
        if(srs_success != (err = extensions_.encode(buf))) {
            return srs_error_wrap(err, "encode rtp extension");
        }
    }

    return err;
}
void SrsRtpHeader::set_extensions(SrsRtpExtensionTypes* extmap)
{
    if (extmap) {
        extensions_.set_types_(extmap);
    }
}

void SrsRtpHeader::ignore_padding(bool v)
{
    ignore_padding_ = v;
}

srs_error_t SrsRtpHeader::get_twcc_sequence_number(uint16_t& twcc_sn)
{
    if (extensions_.exists()) {
        return extensions_.get_twcc_sequence_number(twcc_sn);
    }
    return srs_error_new(ERROR_RTC_RTP_MUXER, "no rtp extension");
}

srs_error_t SrsRtpHeader::set_twcc_sequence_number(uint8_t id, uint16_t sn)
{
    return extensions_.set_twcc_sequence_number(id, sn);
}

uint64_t SrsRtpHeader::nb_bytes()
{
    return kRtpHeaderFixedSize + cc * 4 + (extensions_.exists() ? extensions_.nb_bytes() : 0);
}

void SrsRtpHeader::set_marker(bool v)
{
    marker = v;
}

bool SrsRtpHeader::get_marker() const
{
    return marker;
}

void SrsRtpHeader::set_payload_type(uint8_t v)
{
    payload_type = v;
}

uint8_t SrsRtpHeader::get_payload_type() const
{
    return payload_type;
}

void SrsRtpHeader::set_sequence(uint16_t v)
{
    sequence = v;
}

uint16_t SrsRtpHeader::get_sequence() const
{
    return sequence;
}

void SrsRtpHeader::set_timestamp(uint32_t v)
{
    timestamp = v;
}

uint32_t SrsRtpHeader::get_timestamp() const
{
    return timestamp;
}

void SrsRtpHeader::set_ssrc(uint32_t v)
{
    ssrc = v;
}

void SrsRtpHeader::set_padding(uint8_t v)
{
    padding_length = v;
}

uint8_t SrsRtpHeader::get_padding() const
{
    return padding_length;
}

ISrsRtpPayloader::ISrsRtpPayloader()
{
}

ISrsRtpPayloader::~ISrsRtpPayloader()
{
}

ISrsRtspPacketDecodeHandler::ISrsRtspPacketDecodeHandler()
{
}

ISrsRtspPacketDecodeHandler::~ISrsRtspPacketDecodeHandler()
{
}

SrsRtpPacket::SrsRtpPacket()
{
    payload_ = NULL;
    payload_type_ = SrsRtspPacketPayloadTypeUnknown;
    shared_buffer_ = NULL;
    actual_buffer_size_ = 0;

    nalu_type = SrsAvcNaluTypeReserved;
    frame_type = SrsFrameTypeReserved;
    cached_payload_size = 0;
    decode_handler = NULL;
    avsync_time_ = -1;

    ++_srs_pps_objs_rtps->sugar;
}

SrsRtpPacket::~SrsRtpPacket()
{
    srs_freep(payload_);
    srs_freep(shared_buffer_);
}

char* SrsRtpPacket::wrap(int size)
{
    // The buffer size is larger or equals to the size of packet.
    actual_buffer_size_ = size;

    // If the buffer is large enough, reuse it.
    if (shared_buffer_ && shared_buffer_->size >= size) {
        return shared_buffer_->payload;
    }

    // Create a large enough message, with under-layer buffer.
    srs_freep(shared_buffer_);
    shared_buffer_ = new SrsSharedPtrMessage();

    // Create under-layer buffer for new message
    // For RTC, we use larger under-layer buffer for each packet.
    int nb_buffer = srs_max(size, kRtpPacketSize);
    char* buf = new char[nb_buffer];
    shared_buffer_->wrap(buf, nb_buffer);

    ++_srs_pps_objs_rbuf->sugar;

    return shared_buffer_->payload;
}

char* SrsRtpPacket::wrap(char* data, int size)
{
    char* buf = wrap(size);
    memcpy(buf, data, size);
    return buf;
}

char* SrsRtpPacket::wrap(SrsSharedPtrMessage* msg)
{
    // Generally, the wrap(msg) is used for RTMP to RTC, where the msg
    // is not generated by RTC.
    srs_freep(shared_buffer_);

    // Copy from the new message.
    shared_buffer_ = msg->copy();
    // If we wrap a message, the size of packet equals to the message size.
    actual_buffer_size_ = shared_buffer_->size;

    return msg->payload;
}

SrsRtpPacket* SrsRtpPacket::copy()
{
    SrsRtpPacket* cp = new SrsRtpPacket();

    cp->header = header;
    cp->payload_ = payload_? payload_->copy():NULL;
    cp->payload_type_ = payload_type_;

    cp->nalu_type = nalu_type;
    cp->shared_buffer_ = shared_buffer_? shared_buffer_->copy2() : NULL;
    cp->actual_buffer_size_ = actual_buffer_size_;
    cp->frame_type = frame_type;

    cp->cached_payload_size = cached_payload_size;
    // For performance issue, do not copy the unused field.
    cp->decode_handler = decode_handler;

    cp->avsync_time_ = avsync_time_;

    return cp;
}

void SrsRtpPacket::set_padding(int size)
{
    header.set_padding(size);
    if (cached_payload_size) {
        cached_payload_size += size - header.get_padding();
    }
}

void SrsRtpPacket::add_padding(int size)
{
    header.set_padding(header.get_padding() + size);
    if (cached_payload_size) {
        cached_payload_size += size;
    }
}

void SrsRtpPacket::set_decode_handler(ISrsRtspPacketDecodeHandler* h)
{
    decode_handler = h;
}

bool SrsRtpPacket::is_audio()
{
    return frame_type == SrsFrameTypeAudio;
}

void SrsRtpPacket::set_extension_types(SrsRtpExtensionTypes* v)
{
    return header.set_extensions(v);
}

uint64_t SrsRtpPacket::nb_bytes()
{
    if (!cached_payload_size) {
        int nn_payload = (payload_? payload_->nb_bytes():0);
        cached_payload_size = header.nb_bytes() + nn_payload + header.get_padding();
    }
    return cached_payload_size;
}

srs_error_t SrsRtpPacket::encode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;

    if ((err = header.encode(buf)) != srs_success) {
        return srs_error_wrap(err, "rtp header");
    }

    if (payload_ && (err = payload_->encode(buf)) != srs_success) {
        return srs_error_wrap(err, "rtp payload");
    }

    if (header.get_padding() > 0) {
        uint8_t padding = header.get_padding();
        if (!buf->require(padding)) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", padding);
        }
        memset(buf->head(), padding, padding);
        buf->skip(padding);
    }

    return err;
}

srs_error_t SrsRtpPacket::decode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;

    if ((err = header.decode(buf)) != srs_success) {
        return srs_error_wrap(err, "rtp header");
    }

    // We must skip the padding bytes before parsing payload.
    uint8_t padding = header.get_padding();
    if (!buf->require(padding)) {
        return srs_error_wrap(err, "requires padding %d bytes", padding);
    }
    buf->set_size(buf->size() - padding);

    // TODO: FIXME: We should keep payload to NULL and return if buffer is empty.
    // If user set the decode handler, call it to set the payload.
    if (decode_handler) {
        decode_handler->on_before_decode_payload(this, buf, &payload_, &payload_type_);
    }

    // By default, we always use the RAW payload.
    if (!payload_) {
        payload_ = new SrsRtpRawPayload();
        payload_type_ = SrsRtspPacketPayloadTypeRaw;
    }

    if ((err = payload_->decode(buf)) != srs_success) {
        return srs_error_wrap(err, "rtp payload");
    }

    return err;
}

bool SrsRtpPacket::is_keyframe()
{
    // False if audio packet
    if(SrsFrameTypeAudio == frame_type) {
        return false;
    }

    // It's normal H264 video rtp packet
    if (nalu_type == kStapA) {
        SrsRtpSTAPPayload* stap_payload = dynamic_cast<SrsRtpSTAPPayload*>(payload_);
        if(NULL != stap_payload->get_sps() || NULL != stap_payload->get_pps()) {
            return true;
        }
    } else if (nalu_type == kFuA) {
        SrsRtpFUAPayload2* fua_payload = dynamic_cast<SrsRtpFUAPayload2*>(payload_);
        if(SrsAvcNaluTypeIDR == fua_payload->nalu_type) {
            return true;
        }
    } else {
        if((SrsAvcNaluTypeIDR == nalu_type) || (SrsAvcNaluTypeSPS == nalu_type) || (SrsAvcNaluTypePPS == nalu_type)) {
            return true;
        }
    }

    return false;
}

SrsRtpRawPayload::SrsRtpRawPayload()
{
    payload = NULL;
    nn_payload = 0;

    ++_srs_pps_objs_rraw->sugar;
}

SrsRtpRawPayload::~SrsRtpRawPayload()
{
}

uint64_t SrsRtpRawPayload::nb_bytes()
{
    return nn_payload;
}

srs_error_t SrsRtpRawPayload::encode(SrsBuffer* buf)
{
    if (nn_payload <= 0) {
        return srs_success;
    }

    if (!buf->require(nn_payload)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", nn_payload);
    }

    buf->write_bytes(payload, nn_payload);

    return srs_success;
}

srs_error_t SrsRtpRawPayload::decode(SrsBuffer* buf)
{
    if (buf->empty()) {
        return srs_success;
    }

    payload = buf->head();
    nn_payload = buf->left();

    return srs_success;
}

ISrsRtpPayloader* SrsRtpRawPayload::copy()
{
    SrsRtpRawPayload* cp = new SrsRtpRawPayload();

    cp->payload = payload;
    cp->nn_payload = nn_payload;

    return cp;
}

SrsRtpRawNALUs::SrsRtpRawNALUs()
{
    cursor = 0;
    nn_bytes = 0;

    ++_srs_pps_objs_rothers->sugar;
}

SrsRtpRawNALUs::~SrsRtpRawNALUs()
{
    int nn_nalus = (int)nalus.size();
    for (int i = 0; i < nn_nalus; i++) {
        SrsSample* p = nalus[i];
        srs_freep(p);
    }
}

void SrsRtpRawNALUs::push_back(SrsSample* sample)
{
    if (sample->size <= 0) {
        return;
    }

    if (!nalus.empty()) {
        SrsSample* p = new SrsSample();
        p->bytes = (char*)"\0\0\1";
        p->size = 3;
        nn_bytes += 3;
        nalus.push_back(p);
    }

    nn_bytes += sample->size;
    nalus.push_back(sample);
}

uint8_t SrsRtpRawNALUs::skip_first_byte()
{
    srs_assert (cursor >= 0 && nn_bytes > 0 && cursor < nn_bytes);
    cursor++;
    return uint8_t(nalus[0]->bytes[0]);
}

srs_error_t SrsRtpRawNALUs::read_samples(vector<SrsSample*>& samples, int packet_size)
{
    if (cursor + packet_size < 0 || cursor + packet_size > nn_bytes) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "cursor=%d, max=%d, size=%d", cursor, nn_bytes, packet_size);
    }

    int pos = cursor;
    cursor += packet_size;
    int left = packet_size;

    int nn_nalus = (int)nalus.size();
    for (int i = 0; left > 0 && i < nn_nalus; i++) {
        SrsSample* p = nalus[i];

        // Ignore previous consumed samples.
        if (pos && pos - p->size >= 0) {
            pos -= p->size;
            continue;
        }

        // Now, we are working at the sample.
        int nn = srs_min(left, p->size - pos);
        srs_assert(nn > 0);

        SrsSample* sample = new SrsSample();
        samples.push_back(sample);

        sample->bytes = p->bytes + pos;
        sample->size = nn;

        left -= nn;
        pos = 0;
    }

    return srs_success;
}

uint64_t SrsRtpRawNALUs::nb_bytes()
{
    int size = 0;

    int nn_nalus = (int)nalus.size();
    for (int i = 0; i < nn_nalus; i++) {
        SrsSample* p = nalus[i];
        size += p->size;
    }

    return size;
}

srs_error_t SrsRtpRawNALUs::encode(SrsBuffer* buf)
{
    int nn_nalus = (int)nalus.size();
    for (int i = 0; i < nn_nalus; i++) {
        SrsSample* p = nalus[i];

        if (!buf->require(p->size)) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", p->size);
        }

        buf->write_bytes(p->bytes, p->size);
    }

    return srs_success;
}

srs_error_t SrsRtpRawNALUs::decode(SrsBuffer* buf)
{
    if (buf->empty()) {
        return srs_success;
    }

    SrsSample* sample = new SrsSample();
    sample->bytes = buf->head();
    sample->size = buf->left();
    buf->skip(sample->size);

    nalus.push_back(sample);

    return srs_success;
}

ISrsRtpPayloader* SrsRtpRawNALUs::copy()
{
    SrsRtpRawNALUs* cp = new SrsRtpRawNALUs();

    cp->nn_bytes = nn_bytes;
    cp->cursor = cursor;

    int nn_nalus = (int)nalus.size();
    for (int i = 0; i < nn_nalus; i++) {
        SrsSample* p = nalus[i];
        cp->nalus.push_back(p->copy());
    }

    return cp;
}

SrsRtpSTAPPayload::SrsRtpSTAPPayload()
{
    nri = (SrsAvcNaluType)0;

    ++_srs_pps_objs_rothers->sugar;
}

SrsRtpSTAPPayload::~SrsRtpSTAPPayload()
{
    int nn_nalus = (int)nalus.size();
    for (int i = 0; i < nn_nalus; i++) {
        SrsSample* p = nalus[i];
        srs_freep(p);
    }
}

SrsSample* SrsRtpSTAPPayload::get_sps()
{
    int nn_nalus = (int)nalus.size();
    for (int i = 0; i < nn_nalus; i++) {
        SrsSample* p = nalus[i];
        if (!p || !p->size) {
            continue;
        }

        SrsAvcNaluType nalu_type = (SrsAvcNaluType)(p->bytes[0] & kNalTypeMask);
        if (nalu_type == SrsAvcNaluTypeSPS) {
            return p;
        }
    }

    return NULL;
}

SrsSample* SrsRtpSTAPPayload::get_pps()
{
    int nn_nalus = (int)nalus.size();
    for (int i = 0; i < nn_nalus; i++) {
        SrsSample* p = nalus[i];
        if (!p || !p->size) {
            continue;
        }

        SrsAvcNaluType nalu_type = (SrsAvcNaluType)(p->bytes[0] & kNalTypeMask);
        if (nalu_type == SrsAvcNaluTypePPS) {
            return p;
        }
    }

    return NULL;
}

uint64_t SrsRtpSTAPPayload::nb_bytes()
{
    int size = 1;

    int nn_nalus = (int)nalus.size();
    for (int i = 0; i < nn_nalus; i++) {
        SrsSample* p = nalus[i];
        size += 2 + p->size;
    }

    return size;
}

srs_error_t SrsRtpSTAPPayload::encode(SrsBuffer* buf)
{
    if (!buf->require(1)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", 1);
    }

    // STAP header, RTP payload format for aggregation packets
    // @see https://tools.ietf.org/html/rfc6184#section-5.7
    uint8_t v = kStapA;
    v |= (nri & (~kNalTypeMask));
    buf->write_1bytes(v);

    // NALUs.
    int nn_nalus = (int)nalus.size();
    for (int i = 0; i < nn_nalus; i++) {
        SrsSample* p = nalus[i];

        if (!buf->require(2 + p->size)) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", 2 + p->size);
        }

        buf->write_2bytes(p->size);
        buf->write_bytes(p->bytes, p->size);
    }

    return srs_success;
}

srs_error_t SrsRtpSTAPPayload::decode(SrsBuffer* buf)
{
    if (!buf->require(1)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", 1);
    }

    // STAP header, RTP payload format for aggregation packets
    // @see https://tools.ietf.org/html/rfc6184#section-5.7
    uint8_t v = buf->read_1bytes();

    // forbidden_zero_bit shoul be zero.
    // @see https://tools.ietf.org/html/rfc6184#section-5.3
    uint8_t f = (v & 0x80);
    if (f == 0x80) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "forbidden_zero_bit should be zero");
    }

    nri = SrsAvcNaluType(v & (~kNalTypeMask));

    // NALUs.
    while (!buf->empty()) {
        if (!buf->require(2)) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", 2);
        }

        int size = buf->read_2bytes();
        if (!buf->require(size)) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", size);
        }

        SrsSample* sample = new SrsSample();
        sample->bytes = buf->head();
        sample->size = size;
        buf->skip(size);

        nalus.push_back(sample);
    }

    return srs_success;
}

ISrsRtpPayloader* SrsRtpSTAPPayload::copy()
{
    SrsRtpSTAPPayload* cp = new SrsRtpSTAPPayload();

    cp->nri = nri;

    int nn_nalus = (int)nalus.size();
    for (int i = 0; i < nn_nalus; i++) {
        SrsSample* p = nalus[i];
        cp->nalus.push_back(p->copy());
    }

    return cp;
}

SrsRtpFUAPayload::SrsRtpFUAPayload()
{
    start = end = false;
    nri = nalu_type = (SrsAvcNaluType)0;

    ++_srs_pps_objs_rothers->sugar;
}

SrsRtpFUAPayload::~SrsRtpFUAPayload()
{
    int nn_nalus = (int)nalus.size();
    for (int i = 0; i < nn_nalus; i++) {
        SrsSample* p = nalus[i];
        srs_freep(p);
    }
}

uint64_t SrsRtpFUAPayload::nb_bytes()
{
    int size = 2;

    int nn_nalus = (int)nalus.size();
    for (int i = 0; i < nn_nalus; i++) {
        SrsSample* p = nalus[i];
        size += p->size;
    }

    return size;
}

srs_error_t SrsRtpFUAPayload::encode(SrsBuffer* buf)
{
    if (!buf->require(2)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", 1);
    }

    // FU indicator, @see https://tools.ietf.org/html/rfc6184#section-5.8
    uint8_t fu_indicate = kFuA;
    fu_indicate |= (nri & (~kNalTypeMask));
    buf->write_1bytes(fu_indicate);

    // FU header, @see https://tools.ietf.org/html/rfc6184#section-5.8
    uint8_t fu_header = nalu_type;
    if (start) {
        fu_header |= kStart;
    }
    if (end) {
        fu_header |= kEnd;
    }
    buf->write_1bytes(fu_header);

    // FU payload, @see https://tools.ietf.org/html/rfc6184#section-5.8
    int nn_nalus = (int)nalus.size();
    for (int i = 0; i < nn_nalus; i++) {
        SrsSample* p = nalus[i];

        if (!buf->require(p->size)) {
            return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", p->size);
        }

        buf->write_bytes(p->bytes, p->size);
    }

    return srs_success;
}

srs_error_t SrsRtpFUAPayload::decode(SrsBuffer* buf)
{
    if (!buf->require(2)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", 2);
    }

    // FU indicator, @see https://tools.ietf.org/html/rfc6184#section-5.8
    uint8_t v = buf->read_1bytes();
    nri = SrsAvcNaluType(v & (~kNalTypeMask));

    // FU header, @see https://tools.ietf.org/html/rfc6184#section-5.8
    v = buf->read_1bytes();
    start = v & kStart;
    end = v & kEnd;
    nalu_type = SrsAvcNaluType(v & kNalTypeMask);

    if (!buf->require(1)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", 1);
    }

    SrsSample* sample = new SrsSample();
    sample->bytes = buf->head();
    sample->size = buf->left();
    buf->skip(sample->size);

    nalus.push_back(sample);

    return srs_success;
}

ISrsRtpPayloader* SrsRtpFUAPayload::copy()
{
    SrsRtpFUAPayload* cp = new SrsRtpFUAPayload();

    cp->nri = nri;
    cp->start = start;
    cp->end = end;
    cp->nalu_type = nalu_type;

    int nn_nalus = (int)nalus.size();
    for (int i = 0; i < nn_nalus; i++) {
        SrsSample* p = nalus[i];
        cp->nalus.push_back(p->copy());
    }

    return cp;
}

SrsRtpFUAPayload2::SrsRtpFUAPayload2()
{
    start = end = false;
    nri = nalu_type = (SrsAvcNaluType)0;

    payload = NULL;
    size = 0;

    ++_srs_pps_objs_rfua->sugar;
}

SrsRtpFUAPayload2::~SrsRtpFUAPayload2()
{
}

uint64_t SrsRtpFUAPayload2::nb_bytes()
{
    return 2 + size;
}

srs_error_t SrsRtpFUAPayload2::encode(SrsBuffer* buf)
{
    if (!buf->require(2 + size)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", 1);
    }

    // Fast encoding.
    char* p = buf->head();

    // FU indicator, @see https://tools.ietf.org/html/rfc6184#section-5.8
    uint8_t fu_indicate = kFuA;
    fu_indicate |= (nri & (~kNalTypeMask));
    *p++ = fu_indicate;

    // FU header, @see https://tools.ietf.org/html/rfc6184#section-5.8
    uint8_t fu_header = nalu_type;
    if (start) {
        fu_header |= kStart;
    }
    if (end) {
        fu_header |= kEnd;
    }
    *p++ = fu_header;

    // FU payload, @see https://tools.ietf.org/html/rfc6184#section-5.8
    memcpy(p, payload, size);

    // Consume bytes.
    buf->skip(2 + size);

    return srs_success;
}

srs_error_t SrsRtpFUAPayload2::decode(SrsBuffer* buf)
{
    if (!buf->require(2)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", 2);
    }

    // FU indicator, @see https://tools.ietf.org/html/rfc6184#section-5.8
    uint8_t v = buf->read_1bytes();
    nri = SrsAvcNaluType(v & (~kNalTypeMask));

    // FU header, @see https://tools.ietf.org/html/rfc6184#section-5.8
    v = buf->read_1bytes();
    start = v & kStart;
    end = v & kEnd;
    nalu_type = SrsAvcNaluType(v & kNalTypeMask);

    if (!buf->require(1)) {
        return srs_error_new(ERROR_RTC_RTP_MUXER, "requires %d bytes", 1);
    }

    payload = buf->head();
    size = buf->left();
    buf->skip(size);

    return srs_success;
}

ISrsRtpPayloader* SrsRtpFUAPayload2::copy()
{
    SrsRtpFUAPayload2* cp = new SrsRtpFUAPayload2();

    cp->nri = nri;
    cp->start = start;
    cp->end = end;
    cp->nalu_type = nalu_type;
    cp->payload = payload;
    cp->size = size;

    return cp;
}
