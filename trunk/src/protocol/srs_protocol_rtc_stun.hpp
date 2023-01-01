//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_PROTOCOL_RTC_STUN_HPP
#define SRS_PROTOCOL_RTC_STUN_HPP

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
public:
    bool is_binding_request() const;
    bool is_binding_response() const;
    uint16_t get_message_type() const;
    std::string get_username() const;
    std::string get_local_ufrag() const;
    std::string get_remote_ufrag() const;
    std::string get_transcation_id() const;
    uint32_t get_mapped_address() const;
    uint16_t get_mapped_port() const;
    bool get_ice_controlled() const;
    bool get_ice_controlling() const;
    bool get_use_candidate() const;
    void set_message_type(const uint16_t& m);
    void set_local_ufrag(const std::string& u);
    void set_remote_ufrag(const std::string& u);
    void set_transcation_id(const std::string& t);
    void set_mapped_address(const uint32_t& addr);
    void set_mapped_port(const uint32_t& port);
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
