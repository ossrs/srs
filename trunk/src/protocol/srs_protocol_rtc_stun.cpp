//
// Copyright (c) 2013-2024 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_protocol_rtc_stun.hpp>

using namespace std;

#include <openssl/dh.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/ssl.h>

#include <srs_core_autofree.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_rtmp_stack.hpp>

static srs_error_t hmac_encode(const std::string& algo, const char* key, const int& key_length,  
        const char* input, const int input_length, char* output, unsigned int& output_length)
{
    srs_error_t err = srs_success;

    const EVP_MD* engine = NULL;
    if (algo == "sha512") {   
        engine = EVP_sha512();
    } else if(algo == "sha256") { 
        engine = EVP_sha256();
    } else if(algo == "sha1") { 
        engine = EVP_sha1();
    } else if(algo == "md5") { 
        engine = EVP_md5();
    } else if(algo == "sha224") { 
        engine = EVP_sha224();
    } else if(algo == "sha384") { 
        engine = EVP_sha384();
    } else { 
        return srs_error_new(ERROR_RTC_STUN, "unknown algo=%s", algo.c_str());
    } 

    HMAC_CTX* ctx = HMAC_CTX_new();
    if (ctx == NULL) {
        return srs_error_new(ERROR_RTC_STUN, "hmac init faied");
    }

    if (HMAC_Init_ex(ctx, key, key_length, engine, NULL) < 0) {
        HMAC_CTX_free(ctx);
        return srs_error_new(ERROR_RTC_STUN, "hmac init faied");
    }

    if (HMAC_Update(ctx, (const unsigned char*)input, input_length) < 0) {
        HMAC_CTX_free(ctx);
        return srs_error_new(ERROR_RTC_STUN, "hmac update faied");
    }

    if (HMAC_Final(ctx, (unsigned char*)output, &output_length) < 0) {
        HMAC_CTX_free(ctx);
        return srs_error_new(ERROR_RTC_STUN, "hmac final faied");
    }

    HMAC_CTX_free(ctx);
	
    return err;
}


SrsStunPacket::SrsStunPacket()
{
    message_type = 0;
    local_ufrag = "";
    remote_ufrag = "";
    use_candidate = false;
    ice_controlled = false;
    ice_controlling = false;
    mapped_port = 0;
    mapped_address = 0;
}

SrsStunPacket::~SrsStunPacket()
{
}

bool SrsStunPacket::is_binding_request() const
{
    return message_type == BindingRequest;
}

bool SrsStunPacket::is_binding_response() const
{
    return message_type == BindingResponse;
}

uint16_t SrsStunPacket::get_message_type() const
{
    return message_type;
}

std::string SrsStunPacket::get_username() const
{
    return username;
}

std::string SrsStunPacket::get_local_ufrag() const
{
    return local_ufrag;
}

std::string SrsStunPacket::get_remote_ufrag() const
{
    return remote_ufrag;
}

std::string SrsStunPacket::get_transcation_id() const
{
    return transcation_id;
}

uint32_t SrsStunPacket::get_mapped_address() const
{
    return mapped_address;
}

uint16_t SrsStunPacket::get_mapped_port() const
{
    return mapped_port;
}

bool SrsStunPacket::get_ice_controlled() const
{
    return ice_controlled;
}

bool SrsStunPacket::get_ice_controlling() const
{
    return ice_controlling;
}

bool SrsStunPacket::get_use_candidate() const
{
    return use_candidate;
}

void SrsStunPacket::set_message_type(const uint16_t& m)
{
    message_type = m;
}

void SrsStunPacket::set_local_ufrag(const std::string& u)
{
    local_ufrag = u;
}

void SrsStunPacket::set_remote_ufrag(const std::string& u)
{
    remote_ufrag = u;
}

void SrsStunPacket::set_transcation_id(const std::string& t)
{
    transcation_id = t;
}

void SrsStunPacket::set_mapped_address(const uint32_t& addr)
{
    mapped_address = addr;
}

void SrsStunPacket::set_mapped_port(const uint32_t& port)
{
    mapped_port = port;
}

srs_error_t SrsStunPacket::decode(const char* buf, const int nb_buf)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsBuffer> stream(new SrsBuffer(const_cast<char*>(buf), nb_buf));

    if (stream->left() < 20) {
        return srs_error_new(ERROR_RTC_STUN, "invalid stun packet, size=%d", stream->size());
    }

    message_type = stream->read_2bytes();
    uint16_t message_len = stream->read_2bytes();
    string magic_cookie = stream->read_string(4);
    transcation_id = stream->read_string(12);

    if (nb_buf != 20 + message_len) {
        return srs_error_new(ERROR_RTC_STUN, "invalid stun packet, message_len=%d, nb_buf=%d", message_len, nb_buf);
    }

    while (stream->left() >= 4) {
        uint16_t type = stream->read_2bytes();
        uint16_t len = stream->read_2bytes();

        if (stream->left() < len) {
            return srs_error_new(ERROR_RTC_STUN, "invalid stun packet");
        }

        string val = stream->read_string(len);
        // padding
        if (len % 4 != 0) {
            stream->read_string(4 - (len % 4));
        }

        switch (type) {
            case Username: {
                username = val;
                size_t p = val.find(":");
                if (p != string::npos) {
                    local_ufrag = val.substr(0, p);
                    remote_ufrag = val.substr(p + 1);
                    srs_verbose("stun packet local_ufrag=%s, remote_ufrag=%s", local_ufrag.c_str(), remote_ufrag.c_str());
                }
                break;
            }

			case UseCandidate: {
                use_candidate = true;
                srs_verbose("stun use-candidate");
                break;
            }

            // @see: https://tools.ietf.org/html/draft-ietf-ice-rfc5245bis-00#section-5.1.2
			// One agent full, one lite:  The full agent MUST take the controlling
            // role, and the lite agent MUST take the controlled role.  The full
            // agent will form check lists, run the ICE state machines, and
            // generate connectivity checks.
			case IceControlled: {
                ice_controlled = true;
                srs_verbose("stun ice-controlled");
                break;
            }

			case IceControlling: {
                ice_controlling = true;
                srs_verbose("stun ice-controlling");
                break;
            }
            
            default: {
                srs_verbose("stun type=%u, no process", type);
                break;
            }
        }
    }

    return err;
}

srs_error_t SrsStunPacket::encode(const string& pwd, SrsBuffer* stream)
{
    if (is_binding_response()) {
        return encode_binding_response(pwd, stream);
    }

    return srs_error_new(ERROR_RTC_STUN, "unknown stun type=%d", get_message_type());
}

// FIXME: make this function easy to read
srs_error_t SrsStunPacket::encode_binding_response(const string& pwd, SrsBuffer* stream)
{
    srs_error_t err = srs_success;

    string property_username = encode_username();
    string mapped_address = encode_mapped_address();

    stream->write_2bytes(BindingResponse);
    stream->write_2bytes(property_username.size() + mapped_address.size());
    stream->write_4bytes(kStunMagicCookie);
    stream->write_string(transcation_id);
    stream->write_string(property_username);
    stream->write_string(mapped_address);

    stream->data()[2] = ((stream->pos() - 20 + 20 + 4) & 0x0000FF00) >> 8;
    stream->data()[3] = ((stream->pos() - 20 + 20 + 4) & 0x000000FF);

    char hmac_buf[20] = {0};
    unsigned int hmac_buf_len = 0;
    if ((err = hmac_encode("sha1", pwd.c_str(), pwd.size(), stream->data(), stream->pos(), hmac_buf, hmac_buf_len)) != srs_success) {
        return srs_error_wrap(err, "hmac encode failed");
    }

    string hmac = encode_hmac(hmac_buf, hmac_buf_len);

    stream->write_string(hmac);
    stream->data()[2] = ((stream->pos() - 20 + 8) & 0x0000FF00) >> 8;
    stream->data()[3] = ((stream->pos() - 20 + 8) & 0x000000FF);

    uint32_t crc32 = srs_crc32_ieee(stream->data(), stream->pos(), 0) ^ 0x5354554E;

    string fingerprint = encode_fingerprint(crc32);

    stream->write_string(fingerprint);

    stream->data()[2] = ((stream->pos() - 20) & 0x0000FF00) >> 8;
    stream->data()[3] = ((stream->pos() - 20) & 0x000000FF);

    return err;
}

string SrsStunPacket::encode_username()
{
    char buf[1460];
    SrsUniquePtr<SrsBuffer> stream(new SrsBuffer(buf, sizeof(buf)));

    string username = remote_ufrag + ":" + local_ufrag;

    stream->write_2bytes(Username);
    stream->write_2bytes(username.size());
    stream->write_string(username);

    if (stream->pos() % 4 != 0) {
        static char padding[4] = {0};
        stream->write_bytes(padding, 4 - (stream->pos() % 4));
    }

    return string(stream->data(), stream->pos());
}

string SrsStunPacket::encode_mapped_address()
{
    char buf[1460];
    SrsUniquePtr<SrsBuffer> stream(new SrsBuffer(buf, sizeof(buf)));

    stream->write_2bytes(XorMappedAddress);
    stream->write_2bytes(8);
    stream->write_1bytes(0); // ignore this bytes
    stream->write_1bytes(1); // ipv4 family
    stream->write_2bytes(mapped_port ^ (kStunMagicCookie >> 16));
    stream->write_4bytes(mapped_address ^ kStunMagicCookie);

    return string(stream->data(), stream->pos());
}

string SrsStunPacket::encode_hmac(char* hmac_buf, const int hmac_buf_len)
{
    char buf[1460];
    SrsUniquePtr<SrsBuffer> stream(new SrsBuffer(buf, sizeof(buf)));

    stream->write_2bytes(MessageIntegrity);
    stream->write_2bytes(hmac_buf_len);
    stream->write_bytes(hmac_buf, hmac_buf_len);

    return string(stream->data(), stream->pos());
}

string SrsStunPacket::encode_fingerprint(uint32_t crc32)
{
    char buf[1460];
    SrsUniquePtr<SrsBuffer> stream(new SrsBuffer(buf, sizeof(buf)));

    stream->write_2bytes(Fingerprint);
    stream->write_2bytes(4);
    stream->write_4bytes(crc32);

    return string(stream->data(), stream->pos());
}
