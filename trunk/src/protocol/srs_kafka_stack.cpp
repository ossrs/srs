/*
 The MIT License (MIT)
 
 Copyright (c) 2013-2015 SRS(simple-rtmp-server)
 
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

#include <srs_kafka_stack.hpp>

#include <string>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_log.hpp>
#include <srs_protocol_io.hpp>

#ifdef SRS_AUTO_KAFKA

SrsKafkaString::SrsKafkaString()
{
    _size = -1;
    data = NULL;
}

SrsKafkaString::SrsKafkaString(string v)
{
    _size = (int16_t)v.length();
    
    srs_assert(_size > 0);
    data = new char[_size];
    memcpy(data, v.data(), _size);
}

SrsKafkaString::~SrsKafkaString()
{
    srs_freep(data);
}

bool SrsKafkaString::null()
{
    return _size == -1;
}

bool SrsKafkaString::empty()
{
    return _size <= 0;
}

int SrsKafkaString::size()
{
    return _size == -1? 2 : 2 + _size;
}

int SrsKafkaString::encode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if (!buf->require(2)) {
        ret = ERROR_KAFKA_CODEC_STRING;
        srs_error("kafka encode string failed. ret=%d", ret);
        return ret;
    }
    buf->write_2bytes(_size);
    
    if (_size <= 0) {
        return ret;
    }
    
    if (!buf->require(_size)) {
        ret = ERROR_KAFKA_CODEC_STRING;
        srs_error("kafka encode string data failed. ret=%d", ret);
        return ret;
    }
    buf->write_bytes(data, _size);
    
    return ret;
}

int SrsKafkaString::decode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if (!buf->require(2)) {
        ret = ERROR_KAFKA_CODEC_STRING;
        srs_error("kafka decode string failed. ret=%d", ret);
        return ret;
    }
    _size = buf->read_2bytes();
    
    if (_size != -1 && _size < 0) {
        ret = ERROR_KAFKA_CODEC_STRING;
        srs_error("kafka string must be -1 or >=0, actual is %d. ret=%d", _size, ret);
        return ret;
    }
    
    if (_size <= 0) {
        return ret;
    }
    
    if (!buf->require(_size)) {
        ret = ERROR_KAFKA_CODEC_STRING;
        srs_error("kafka decode string data failed. ret=%d", ret);
        return ret;
    }
    
    srs_freep(data);
    data = new char[_size];
    
    buf->read_bytes(data, _size);
    
    return ret;
}

SrsKafkaBytes::SrsKafkaBytes()
{
    _size = -1;
    data = NULL;
}

SrsKafkaBytes::SrsKafkaBytes(const char* v, int nb_v)
{
    _size = (int16_t)nb_v;
    
    srs_assert(_size > 0);
    data = new char[_size];
    memcpy(data, v, _size);
}

SrsKafkaBytes::~SrsKafkaBytes()
{
    srs_freep(data);
}

bool SrsKafkaBytes::null()
{
    return _size == -1;
}

bool SrsKafkaBytes::empty()
{
    return _size <= 0;
}

int SrsKafkaBytes::size()
{
    return 4 + (_size == -1? 0 : _size);
}

int SrsKafkaBytes::encode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if (!buf->require(4)) {
        ret = ERROR_KAFKA_CODEC_BYTES;
        srs_error("kafka encode bytes failed. ret=%d", ret);
        return ret;
    }
    buf->write_4bytes(_size);
    
    if (_size <= 0) {
        return ret;
    }
    
    if (!buf->require(_size)) {
        ret = ERROR_KAFKA_CODEC_BYTES;
        srs_error("kafka encode bytes data failed. ret=%d", ret);
        return ret;
    }
    buf->write_bytes(data, _size);
    
    return ret;
}

int SrsKafkaBytes::decode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if (!buf->require(4)) {
        ret = ERROR_KAFKA_CODEC_BYTES;
        srs_error("kafka decode bytes failed. ret=%d", ret);
        return ret;
    }
    _size = buf->read_4bytes();
    
    if (_size != -1 && _size < 0) {
        ret = ERROR_KAFKA_CODEC_BYTES;
        srs_error("kafka bytes must be -1 or >=0, actual is %d. ret=%d", _size, ret);
        return ret;
    }
    
    if (!buf->require(_size)) {
        ret = ERROR_KAFKA_CODEC_BYTES;
        srs_error("kafka decode bytes data failed. ret=%d", ret);
        return ret;
    }
    
    srs_freep(data);
    data = new char[_size];
    buf->read_bytes(data, _size);
    
    return ret;
}

SrsKafkaRequestHeader::SrsKafkaRequestHeader()
{
    _size = 0;
    api_key = api_version = 0;
    correlation_id = 0;
    client_id = new SrsKafkaString();
}

SrsKafkaRequestHeader::~SrsKafkaRequestHeader()
{
    srs_freep(client_id);
}

int SrsKafkaRequestHeader::header_size()
{
    return 2 + 2 + 4 + client_id->size();
}

int SrsKafkaRequestHeader::message_size()
{
    return _size - header_size();
}

int SrsKafkaRequestHeader::total_size()
{
    return 4 + _size;
}

bool SrsKafkaRequestHeader::is_producer_request()
{
    return api_key == SrsKafkaApiKeyProduceRequest;
}

bool SrsKafkaRequestHeader::is_fetch_request()
{
    return api_key == SrsKafkaApiKeyFetchRequest;
}

bool SrsKafkaRequestHeader::is_offset_request()
{
    return api_key == SrsKafkaApiKeyOffsetRequest;
}

bool SrsKafkaRequestHeader::is_metadata_request()
{
    return api_key == SrsKafkaApiKeyMetadataRequest;
}

bool SrsKafkaRequestHeader::is_offset_commit_request()
{
    return api_key == SrsKafkaApiKeyOffsetCommitRequest;
}

bool SrsKafkaRequestHeader::is_offset_fetch_request()
{
    return api_key == SrsKafkaApiKeyOffsetFetchRequest;
}

bool SrsKafkaRequestHeader::is_consumer_metadata_request()
{
    return api_key == SrsKafkaApiKeyConsumerMetadataRequest;
}

void SrsKafkaRequestHeader::set_api_key(SrsKafkaApiKey key)
{
    api_key = (int16_t)key;
}

int SrsKafkaRequestHeader::size()
{
    return 4 + _size;
}

int SrsKafkaRequestHeader::encode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if (!buf->require(4 + _size)) {
        ret = ERROR_KAFKA_CODEC_REQUEST;
        srs_error("kafka encode request failed. ret=%d", ret);
        return ret;
    }
    
    buf->write_4bytes(_size);
    buf->write_2bytes(api_key);
    buf->write_2bytes(api_version);
    buf->write_4bytes(correlation_id);
    
    if ((ret = client_id->encode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka encode request client_id failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsKafkaRequestHeader::decode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if (!buf->require(4)) {
        ret = ERROR_KAFKA_CODEC_REQUEST;
        srs_error("kafka decode request size failed. ret=%d", ret);
        return ret;
    }
    _size = buf->read_4bytes();
    
    if (_size <= 0) {
        srs_warn("kafka got empty request");
        return ret;
    }
    
    if (!buf->require(_size)) {
        ret = ERROR_KAFKA_CODEC_REQUEST;
        srs_error("kafka decode request message failed. ret=%d", ret);
        return ret;
    }
    api_key = buf->read_2bytes();
    api_version = buf->read_2bytes();
    correlation_id = buf->read_4bytes();
    
    if ((ret = client_id->decode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka decode request client_id failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

SrsKafkaResponseHeader::SrsKafkaResponseHeader()
{
    _size = 0;
    correlation_id = 0;
}

SrsKafkaResponseHeader::~SrsKafkaResponseHeader()
{
}

int SrsKafkaResponseHeader::header_size()
{
    return 4;
}

int SrsKafkaResponseHeader::message_size()
{
    return _size - header_size();
}

int SrsKafkaResponseHeader::total_size()
{
    return 4 + _size;
}

int SrsKafkaResponseHeader::size()
{
    return 4 + _size;
}

int SrsKafkaResponseHeader::encode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if (!buf->require(4 + _size)) {
        ret = ERROR_KAFKA_CODEC_RESPONSE;
        srs_error("kafka encode response failed. ret=%d", ret);
        return ret;
    }
    
    buf->write_4bytes(_size);
    buf->write_4bytes(correlation_id);
    
    return ret;
}

int SrsKafkaResponseHeader::decode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if (!buf->require(4)) {
        ret = ERROR_KAFKA_CODEC_RESPONSE;
        srs_error("kafka decode response size failed. ret=%d", ret);
        return ret;
    }
    _size = buf->read_4bytes();
    
    if (_size <= 0) {
        srs_warn("kafka got empty response");
        return ret;
    }
    
    if (!buf->require(_size)) {
        ret = ERROR_KAFKA_CODEC_RESPONSE;
        srs_error("kafka decode response message failed. ret=%d", ret);
        return ret;
    }
    correlation_id = buf->read_4bytes();
    
    return ret;
}

SrsKafkaRawMessage::SrsKafkaRawMessage()
{
    offset = 0;
    message_size = 0;
    
    crc = 0;
    magic_byte = attributes = 0;
    key = new SrsKafkaBytes();
    value = new SrsKafkaBytes();
}

SrsKafkaRawMessage::~SrsKafkaRawMessage()
{
    srs_freep(key);
    srs_freep(value);
}

SrsKafkaMessageSet::SrsKafkaMessageSet()
{
}

SrsKafkaMessageSet::~SrsKafkaMessageSet()
{
    vector<SrsKafkaRawMessage*>::iterator it;
    for (it = messages.begin(); it != messages.end(); ++it) {
        SrsKafkaRawMessage* message = *it;
        srs_freep(message);
    }
    messages.clear();
}

SrsKafkaRequest::SrsKafkaRequest()
{
}

SrsKafkaRequest::~SrsKafkaRequest()
{
}

int SrsKafkaRequest::size()
{
    return header.size();
}

int SrsKafkaRequest::encode(SrsBuffer* buf)
{
    return header.encode(buf);
}

int SrsKafkaRequest::decode(SrsBuffer* buf)
{
    return header.decode(buf);
}

SrsKafkaResponse::SrsKafkaResponse()
{
}

SrsKafkaResponse::~SrsKafkaResponse()
{
}

int SrsKafkaResponse::size()
{
    return header.size();
}

int SrsKafkaResponse::encode(SrsBuffer* buf)
{
    return header.encode(buf);
}

int SrsKafkaResponse::decode(SrsBuffer* buf)
{
    return header.decode(buf);
}

SrsKafkaTopicMetadataRequest::SrsKafkaTopicMetadataRequest()
{
    header.set_api_key(SrsKafkaApiKeyMetadataRequest);
}

SrsKafkaTopicMetadataRequest::~SrsKafkaTopicMetadataRequest()
{
}

void SrsKafkaTopicMetadataRequest::add_topic(string topic)
{
    topics.append(new SrsKafkaString(topic));
}

int SrsKafkaTopicMetadataRequest::size()
{
    return SrsKafkaRequest::size() + topics.size();
}

int SrsKafkaTopicMetadataRequest::encode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsKafkaRequest::encode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka encode metadata request failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = topics.encode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka encode metadata topics failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsKafkaTopicMetadataRequest::decode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsKafkaRequest::decode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka decode metadata request failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = topics.decode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka decode metadata topics failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

SrsKafkaTopicMetadataResponse::SrsKafkaTopicMetadataResponse()
{
}

SrsKafkaTopicMetadataResponse::~SrsKafkaTopicMetadataResponse()
{
}

int SrsKafkaTopicMetadataResponse::size()
{
    // TODO: FIXME: implements it.
    return SrsKafkaResponse::size();
}

int SrsKafkaTopicMetadataResponse::encode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsKafkaResponse::encode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka encode metadata response failed. ret=%d", ret);
        return ret;
    }
    
    // TODO: FIXME: implements it.
    return ret;
}

int SrsKafkaTopicMetadataResponse::decode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsKafkaResponse::decode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka decode metadata response failed. ret=%d", ret);
        return ret;
    }
    
    // TODO: FIXME: implements it.
    return ret;
}

SrsKafkaProtocol::SrsKafkaProtocol(ISrsProtocolReaderWriter* io)
{
    skt = io;
}

SrsKafkaProtocol::~SrsKafkaProtocol()
{
}

int SrsKafkaProtocol::send_and_free_message(SrsKafkaRequest* msg)
{
    int ret = ERROR_SUCCESS;
    
    // TODO: FIXME: refine for performance issue.
    SrsAutoFree(SrsKafkaRequest, msg);
    
    int size = msg->size();
    if (size <= 0) {
        return ret;
    }
    
    // TODO: FIXME: refine for performance issue.
    char* bytes = new char[size];
    SrsAutoFree(char, bytes);
    
    // TODO: FIXME: refine for performance issue.
    SrsBuffer* buf = new SrsBuffer();
    SrsAutoFree(SrsBuffer, buf);
    
    if ((ret = buf->initialize(bytes, size)) != ERROR_SUCCESS) {
        srs_error("kafka create buffer failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = msg->encode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka encode message failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = skt->write(bytes, size, NULL)) != ERROR_SUCCESS) {
        srs_error("kafka send message failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

SrsKafkaClient::SrsKafkaClient(ISrsProtocolReaderWriter* io)
{
    protocol = new SrsKafkaProtocol(io);
}

SrsKafkaClient::~SrsKafkaClient()
{
    srs_freep(protocol);
}

int SrsKafkaClient::fetch_metadata(string topic)
{
    int ret = ERROR_SUCCESS;
    
    SrsKafkaTopicMetadataRequest* req = new SrsKafkaTopicMetadataRequest();
    
    req->add_topic(topic);
    
    if ((ret = protocol->send_and_free_message(req)) != ERROR_SUCCESS) {
        srs_error("kafka send message failed. ret=%d", ret);
        return ret;
    }
    
    // TODO: FIXME: implements it.
    
    return ret;
}

#endif

