/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2020 John
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRS_PROTOCOL_STUN_HPP
#define SRS_PROTOCOL_STUN_HPP

#include <srs_core.hpp>

#include <string>

#include <srs_kernel_error.hpp>

class SrsBuffer;

// @see: https://tools.ietf.org/html/rfc5389
// The magic cookie field MUST contain the fixed value 0x2112A442 in network byte order
const uint32_t kStunMagicCookie = 0x2112A442;

enum SrsStunMessageType
{
	// see @ https://tools.ietf.org/html/rfc3489#section-11.1	
    BindingRequest            = 0x0001,
    BindingResponse           = 0x0101,
    BindingErrorResponse      = 0x0111,
    SharedSecretRequest       = 0x0002,
    SharedSecretResponse      = 0x0102,
    SharedSecretErrorResponse = 0x0112,
};

enum SrsStunMessageAttribute
{
    // see @ https://tools.ietf.org/html/rfc3489#section-11.2
	MappedAddress     = 0x0001,
   	ResponseAddress   = 0x0002,
   	ChangeRequest     = 0x0003,
   	SourceAddress     = 0x0004,
   	ChangedAddress    = 0x0005,
   	Username          = 0x0006,
   	Password          = 0x0007,
   	MessageIntegrity  = 0x0008,
   	ErrorCode         = 0x0009,
   	UnknownAttributes = 0x000A,
   	ReflectedFrom     = 0x000B,

    // see @ https://tools.ietf.org/html/rfc5389#section-18.2
    Realm             = 0x0014,
    Nonce             = 0x0015,
    XorMappedAddress  = 0x0020,
    Software          = 0x8022,
    AlternateServer   = 0x8023,
    Fingerprint       = 0x8028,

    Priority          = 0x0024,
    UseCandidate      = 0x0025,
    IceControlled     = 0x8029,
    IceControlling    = 0x802A,
};

class SrsStunPacket 
{
private:
    uint16_t message_type;
    std::string username;
    std::string local_ufrag;
    std::string remote_ufrag;
    std::string transcation_id;
    uint32_t mapped_address;
    uint16_t mapped_port;
    bool use_candidate;
    bool ice_controlled;
    bool ice_controlling;
public:
    SrsStunPacket();
    virtual ~SrsStunPacket();

    bool is_binding_request() const { return message_type == BindingRequest; }
    bool is_binding_response() const { return message_type == BindingResponse; }

    uint16_t get_message_type() const { return message_type; }
    std::string get_username() const { return username; }
    std::string get_local_ufrag() const { return local_ufrag; }
    std::string get_remote_ufrag() const { return remote_ufrag; }
    std::string get_transcation_id() const { return transcation_id; }
    uint32_t get_mapped_address() const { return mapped_address; }
    uint16_t get_mapped_port() const { return mapped_port; }
    bool get_ice_controlled() const { return ice_controlled; }
    bool get_ice_controlling() const { return ice_controlling; }
    bool get_use_candidate() const { return use_candidate; }

    void set_message_type(const uint16_t& m) { message_type = m; }
    void set_local_ufrag(const std::string& u) { local_ufrag = u; }
    void set_remote_ufrag(const std::string& u) { remote_ufrag = u; }
    void set_transcation_id(const std::string& t) { transcation_id = t; }
    void set_mapped_address(const uint32_t& addr) { mapped_address = addr; }
    void set_mapped_port(const uint32_t& port) { mapped_port = port; }

    srs_error_t decode(const char* buf, const int nb_buf);
    srs_error_t encode(const std::string& pwd, SrsBuffer* stream);
private:
    srs_error_t encode_binding_response(const std::string& pwd, SrsBuffer* stream);
    std::string encode_username();
    std::string encode_mapped_address();
    std::string encode_hmac(char* hamc_buf, const int hmac_buf_len);
    std::string encode_fingerprint(uint32_t crc32);
};

#endif
