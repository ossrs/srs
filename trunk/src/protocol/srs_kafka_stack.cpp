/*
 The MIT License (MIT)
 
 Copyright (c) 2013-2017 SRS(ossrs)
 
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

#include <sstream>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_log.hpp>
#include <srs_protocol_io.hpp>
#include <srs_protocol_stream.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_protocol_json.hpp>

#ifdef SRS_AUTO_KAFKA

#define SRS_KAFKA_PRODUCER_MESSAGE_TIMEOUT_MS 300000

SrsKafkaString::SrsKafkaString()
{
    _size = -1;
    data = NULL;
}

SrsKafkaString::SrsKafkaString(string v)
{
    _size = -1;
    data = NULL;
    
    set_value(v);
}

SrsKafkaString::~SrsKafkaString()
{
    srs_freepa(data);
}

bool SrsKafkaString::null()
{
    return _size == -1;
}

bool SrsKafkaString::empty()
{
    return _size <= 0;
}

string SrsKafkaString::to_str()
{
    string ret;
    if (_size > 0) {
        ret.append(data, _size);
    }
    return ret;
}

void SrsKafkaString::set_value(string v)
{
    // free previous data.
    srs_freepa(data);
    
    // copy new value to data.
    _size = (int16_t)v.length();
    
    srs_assert(_size > 0);
    data = new char[_size];
    memcpy(data, v.data(), _size);
}

int SrsKafkaString::nb_bytes()
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
    
    srs_freepa(data);
    data = new char[_size];
    
    buf->read_bytes(data, _size);
    
    return ret;
}

SrsKafkaBytes::SrsKafkaBytes()
{
    _size = -1;
    _data = NULL;
}

SrsKafkaBytes::SrsKafkaBytes(const char* v, int nb_v)
{
    _size = -1;
    _data = NULL;
    
    set_value(v, nb_v);
}

SrsKafkaBytes::~SrsKafkaBytes()
{
    srs_freepa(_data);
}

char* SrsKafkaBytes::data()
{
    return _data;
}

int SrsKafkaBytes::size()
{
    return _size;
}

bool SrsKafkaBytes::null()
{
    return _size == -1;
}

bool SrsKafkaBytes::empty()
{
    return _size <= 0;
}

void SrsKafkaBytes::set_value(string v)
{
    set_value(v.data(), (int)v.length());
}

void SrsKafkaBytes::set_value(const char* v, int nb_v)
{
    // free previous data.
    srs_freepa(_data);
    
    // copy new value to data.
    _size = (int16_t)nb_v;
    
    srs_assert(_size > 0);
    _data = new char[_size];
    memcpy(_data, v, _size);
}

u_int32_t SrsKafkaBytes::crc32(u_int32_t previous)
{
    char bsize[4];
    SrsBuffer(bsize, 4).write_4bytes(_size);
    
    if (_size <= 0) {
        return srs_crc32_ieee(bsize, 4, previous);
    }
    
    u_int32_t crc = srs_crc32_ieee(bsize, 4, previous);
    crc = srs_crc32_ieee(_data, _size, crc);
    
    return crc;
}

int SrsKafkaBytes::nb_bytes()
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
    buf->write_bytes(_data, _size);
    
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
    
    srs_freepa(_data);
    _data = new char[_size];
    buf->read_bytes(_data, _size);
    
    return ret;
}

SrsKafkaRequestHeader::SrsKafkaRequestHeader()
{
    _size = 0;
    _api_key = api_version = 0;
    _correlation_id = 0;
    client_id = new SrsKafkaString();
}

SrsKafkaRequestHeader::~SrsKafkaRequestHeader()
{
    srs_freep(client_id);
}

int SrsKafkaRequestHeader::header_size()
{
    return 2 + 2 + 4 + client_id->nb_bytes();
}

int SrsKafkaRequestHeader::message_size()
{
    return _size - header_size();
}

int SrsKafkaRequestHeader::total_size()
{
    return 4 + _size;
}

void SrsKafkaRequestHeader::set_total_size(int s)
{
    _size = s - 4;
}

int32_t SrsKafkaRequestHeader::correlation_id()
{
    return _correlation_id;
}

void SrsKafkaRequestHeader::set_correlation_id(int32_t cid)
{
    _correlation_id = cid;
}

SrsKafkaApiKey SrsKafkaRequestHeader::api_key()
{
    return (SrsKafkaApiKey)_api_key;
}

void SrsKafkaRequestHeader::set_api_key(SrsKafkaApiKey key)
{
    _api_key = (int16_t)key;
}

bool SrsKafkaRequestHeader::is_producer_request()
{
    return _api_key == SrsKafkaApiKeyProduceRequest;
}

bool SrsKafkaRequestHeader::is_fetch_request()
{
    return _api_key == SrsKafkaApiKeyFetchRequest;
}

bool SrsKafkaRequestHeader::is_offset_request()
{
    return _api_key == SrsKafkaApiKeyOffsetRequest;
}

bool SrsKafkaRequestHeader::is_metadata_request()
{
    return _api_key == SrsKafkaApiKeyMetadataRequest;
}

bool SrsKafkaRequestHeader::is_offset_commit_request()
{
    return _api_key == SrsKafkaApiKeyOffsetCommitRequest;
}

bool SrsKafkaRequestHeader::is_offset_fetch_request()
{
    return _api_key == SrsKafkaApiKeyOffsetFetchRequest;
}

bool SrsKafkaRequestHeader::is_consumer_metadata_request()
{
    return _api_key == SrsKafkaApiKeyConsumerMetadataRequest;
}

int SrsKafkaRequestHeader::nb_bytes()
{
    return 4 + header_size();
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
    buf->write_2bytes(_api_key);
    buf->write_2bytes(api_version);
    buf->write_4bytes(_correlation_id);
    
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
    _api_key = buf->read_2bytes();
    api_version = buf->read_2bytes();
    _correlation_id = buf->read_4bytes();
    
    if ((ret = client_id->decode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka decode request client_id failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

SrsKafkaResponseHeader::SrsKafkaResponseHeader()
{
    _size = 0;
    _correlation_id = 0;
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

void SrsKafkaResponseHeader::set_total_size(int s)
{
    _size = s - 4;
}

int32_t SrsKafkaResponseHeader::correlation_id()
{
    return _correlation_id;
}

int SrsKafkaResponseHeader::nb_bytes()
{
    return 4 + header_size();
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
    buf->write_4bytes(_correlation_id);
    
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
    _correlation_id = buf->read_4bytes();
    
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

int SrsKafkaRawMessage::create(SrsJsonObject* obj)
{
    int ret = ERROR_SUCCESS;
    
    // current must be 0.
    magic_byte = 0;
    
    // no compression codec.
    attributes = 0;
    
    // dumps the json to string.
    value->set_value(obj->dumps());
    
    // crc32 message.
    crc = srs_crc32_ieee(&magic_byte, 1);
    crc = srs_crc32_ieee(&attributes, 1, crc);
    crc = key->crc32(crc);
    crc = value->crc32(crc);
    
    srs_info("crc32 message is %#x", crc);
    
    message_size = raw_message_size();
    
    return ret;
}

int SrsKafkaRawMessage::raw_message_size()
{
    return 4 + 1 + 1 + key->nb_bytes() + value->nb_bytes();
}

int SrsKafkaRawMessage::nb_bytes()
{
    return 8 + 4 + 4 + 1 + 1 + key->nb_bytes() + value->nb_bytes();
}

int SrsKafkaRawMessage::encode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if (!buf->require(8 + 4 + 4 + 1 + 1)) {
        ret = ERROR_KAFKA_CODEC_MESSAGE;
        srs_error("kafka encode message failed. ret=%d", ret);
        return ret;
    }
    buf->write_8bytes(offset);
    buf->write_4bytes(message_size);
    buf->write_4bytes(crc);
    buf->write_1bytes(magic_byte);
    buf->write_1bytes(attributes);
    
    if ((ret = key->encode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka encode message key failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = value->encode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka encode message value failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsKafkaRawMessage::decode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if (!buf->require(8 + 4 + 4 + 1 + 1)) {
        ret = ERROR_KAFKA_CODEC_MESSAGE;
        srs_error("kafka decode message failed. ret=%d", ret);
        return ret;
    }
    offset = buf->read_8bytes();
    message_size = buf->read_4bytes();
    crc = buf->read_4bytes();
    magic_byte = buf->read_1bytes();
    attributes = buf->read_1bytes();
    
    if ((ret = key->decode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka decode message key failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = value->decode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka decode message value failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

SrsKafkaRawMessageSet::SrsKafkaRawMessageSet()
{
}

SrsKafkaRawMessageSet::~SrsKafkaRawMessageSet()
{
    vector<SrsKafkaRawMessage*>::iterator it;
    for (it = messages.begin(); it != messages.end(); ++it) {
        SrsKafkaRawMessage* message = *it;
        srs_freep(message);
    }
    messages.clear();
}

void SrsKafkaRawMessageSet::append(SrsKafkaRawMessage* msg)
{
    messages.push_back(msg);
}

int SrsKafkaRawMessageSet::nb_bytes()
{
    int s = 0;
    
    vector<SrsKafkaRawMessage*>::iterator it;
    for (it = messages.begin(); it != messages.end(); ++it) {
        SrsKafkaRawMessage* message = *it;
        s += message->nb_bytes();
    }
    
    return s;
}

int SrsKafkaRawMessageSet::encode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    vector<SrsKafkaRawMessage*>::iterator it;
    for (it = messages.begin(); it != messages.end(); ++it) {
        SrsKafkaRawMessage* message = *it;
        if ((ret = message->encode(buf)) != ERROR_SUCCESS) {
            srs_error("kafka encode message set failed. ret=%d", ret);
            return ret;
        }
    }
    
    return ret;
}

int SrsKafkaRawMessageSet::decode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    while (!buf->empty()) {
        SrsKafkaRawMessage* message = new SrsKafkaRawMessage();
        
        if ((ret = message->decode(buf)) != ERROR_SUCCESS) {
            srs_freep(message);
            srs_error("kafka decode message set elem failed. ret=%d", ret);
            return ret;
        }
        
        messages.push_back(message);
    }
    
    return ret;
}

SrsKafkaRequest::SrsKafkaRequest()
{
    header.set_correlation_id(SrsKafkaCorrelationPool::instance()->generate_correlation_id());
}

SrsKafkaRequest::~SrsKafkaRequest()
{
}

void SrsKafkaRequest::update_header(int s)
{
    header.set_total_size(s);
}

int32_t SrsKafkaRequest::correlation_id()
{
    return header.correlation_id();
}

SrsKafkaApiKey SrsKafkaRequest::api_key()
{
    return header.api_key();
}

int SrsKafkaRequest::nb_bytes()
{
    return header.nb_bytes();
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

void SrsKafkaResponse::update_header(int s)
{
    header.set_total_size(s);
}

int SrsKafkaResponse::nb_bytes()
{
    return header.nb_bytes();
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

int SrsKafkaTopicMetadataRequest::nb_bytes()
{
    return SrsKafkaRequest::nb_bytes() + topics.nb_bytes();
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

SrsKafkaBroker::SrsKafkaBroker()
{
    node_id = port = 0;
}

SrsKafkaBroker::~SrsKafkaBroker()
{
}

int SrsKafkaBroker::nb_bytes()
{
    return 4 + host.nb_bytes() + 4;
}

int SrsKafkaBroker::encode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if (!buf->require(4)) {
        ret = ERROR_KAFKA_CODEC_METADATA;
        srs_error("kafka encode broker node_id failed. ret=%d", ret);
        return ret;
    }
    buf->write_4bytes(node_id);
    
    if ((ret = host.encode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka encode broker host failed. ret=%d", ret);
        return ret;
    }
    
    if (!buf->require(4)) {
        ret = ERROR_KAFKA_CODEC_METADATA;
        srs_error("kafka encode broker port failed. ret=%d", ret);
        return ret;
    }
    buf->write_4bytes(port);

    return ret;
}

int SrsKafkaBroker::decode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if (!buf->require(4)) {
        ret = ERROR_KAFKA_CODEC_METADATA;
        srs_error("kafka decode broker node_id failed. ret=%d", ret);
        return ret;
    }
    node_id = buf->read_4bytes();
    
    if ((ret = host.decode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka decode broker host failed. ret=%d", ret);
        return ret;
    }
    
    if (!buf->require(4)) {
        ret = ERROR_KAFKA_CODEC_METADATA;
        srs_error("kafka decode broker port failed. ret=%d", ret);
        return ret;
    }
    port = buf->read_4bytes();
    
    return ret;
}

SrsKafkaPartitionMetadata::SrsKafkaPartitionMetadata()
{
    error_code = 0;
    partition_id = 0;
    leader = 0;
}

SrsKafkaPartitionMetadata::~SrsKafkaPartitionMetadata()
{
}

int SrsKafkaPartitionMetadata::nb_bytes()
{
    return 2 + 4 + 4 + replicas.nb_bytes() + isr.nb_bytes();
}

int SrsKafkaPartitionMetadata::encode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if (!buf->require(2 + 4 + 4)) {
        ret = ERROR_KAFKA_CODEC_METADATA;
        srs_error("kafka encode partition metadata failed. ret=%d", ret);
        return ret;
    }
    buf->write_2bytes(error_code);
    buf->write_4bytes(partition_id);
    buf->write_4bytes(leader);
    
    if ((ret = replicas.encode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka encode partition metadata replicas failed. ret=%d", ret);
        return ret;
    }
    if ((ret = isr.encode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka encode partition metadata isr failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsKafkaPartitionMetadata::decode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if (!buf->require(2 + 4 + 4)) {
        ret = ERROR_KAFKA_CODEC_METADATA;
        srs_error("kafka decode partition metadata failed. ret=%d", ret);
        return ret;
    }
    error_code = buf->read_2bytes();
    partition_id = buf->read_4bytes();
    leader = buf->read_4bytes();
    
    if ((ret = replicas.decode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka decode partition metadata replicas failed. ret=%d", ret);
        return ret;
    }
    if ((ret = isr.decode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka decode partition metadata isr failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

SrsKafkaTopicMetadata::SrsKafkaTopicMetadata()
{
    error_code = 0;
}

SrsKafkaTopicMetadata::~SrsKafkaTopicMetadata()
{
}

int SrsKafkaTopicMetadata::nb_bytes()
{
    return 2 + name.nb_bytes() + metadatas.nb_bytes();
}

int SrsKafkaTopicMetadata::encode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if (!buf->require(2)) {
        ret = ERROR_KAFKA_CODEC_METADATA;
        srs_error("kafka encode topic metadata failed. ret=%d", ret);
        return ret;
    }
    buf->write_2bytes(error_code);
    
    if ((ret = name.encode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka encode topic name failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = metadatas.encode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka encode topic metadatas failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsKafkaTopicMetadata::decode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if (!buf->require(2)) {
        ret = ERROR_KAFKA_CODEC_METADATA;
        srs_error("kafka decode topic metadata failed. ret=%d", ret);
        return ret;
    }
    error_code = buf->read_2bytes();
    
    if ((ret = name.decode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka decode topic name failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = metadatas.decode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka decode topic metadatas failed. ret=%d", ret);
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

int SrsKafkaTopicMetadataResponse::nb_bytes()
{
    return SrsKafkaResponse::nb_bytes() + brokers.nb_bytes() + metadatas.nb_bytes();
}

int SrsKafkaTopicMetadataResponse::encode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsKafkaResponse::encode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka encode metadata response failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = brokers.encode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka encode metadata brokers failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = metadatas.encode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka encode metadatas failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsKafkaTopicMetadataResponse::decode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsKafkaResponse::decode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka decode metadata response failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = brokers.decode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka decode metadata brokers failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = metadatas.decode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka decode metadatas failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsKafkaProducerPartitionMessages::nb_bytes()
{
    return 4 + 4 + messages.nb_bytes();
}

int SrsKafkaProducerPartitionMessages::encode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if (!buf->require(4 + 4)) {
        ret = ERROR_KAFKA_CODEC_PRODUCER;
        srs_error("kafka encode producer failed. ret=%d", ret);
        return ret;
    }
    buf->write_4bytes(partition);
    buf->write_4bytes(message_set_size);
    
    if ((ret = messages.encode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka encode producer messages failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsKafkaProducerPartitionMessages::decode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if (!buf->require(4 + 4)) {
        ret = ERROR_KAFKA_CODEC_PRODUCER;
        srs_error("kafka decode producer failed. ret=%d", ret);
        return ret;
    }
    partition = buf->read_4bytes();
    message_set_size = buf->read_4bytes();
    
    // for the message set decode util empty, we must create a new buffer when
    // there exists other objects after message set.
    if (buf->size() - buf->pos() != message_set_size) {
        SrsBuffer* tbuf = new SrsBuffer();
        SrsAutoFree(SrsBuffer, tbuf);
        
        if ((ret = tbuf->initialize(buf->data() + buf->pos(), message_set_size)) != ERROR_SUCCESS) {
            return ret;
        }
        
        if ((ret = messages.decode(tbuf)) != ERROR_SUCCESS) {
            srs_error("kafka decode procuder messages failed. ret=%d", ret);
            return ret;
        }
    } else {
        if ((ret = messages.decode(buf)) != ERROR_SUCCESS) {
            srs_error("kafka decode procuder messages failed. ret=%d", ret);
            return ret;
        }
    }
    
    return ret;
}

int SrsKafkaProducerTopicMessages::nb_bytes()
{
    return topic_name.nb_bytes() + partitions.nb_bytes();
}

int SrsKafkaProducerTopicMessages::encode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = topic_name.encode(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = partitions.encode(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

int SrsKafkaProducerTopicMessages::decode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = topic_name.decode(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if ((ret = partitions.decode(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    return ret;
}

SrsKafkaProducerRequest::SrsKafkaProducerRequest()
{
    required_acks = 0;
    timeout = 0;
}

SrsKafkaProducerRequest::~SrsKafkaProducerRequest()
{
}

int SrsKafkaProducerRequest::nb_bytes()
{
    return SrsKafkaRequest::nb_bytes() + 2 + 4 + topics.nb_bytes();
}

int SrsKafkaProducerRequest::encode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsKafkaRequest::encode(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if (!buf->require(2 + 4)) {
        ret = ERROR_KAFKA_CODEC_PRODUCER;
        srs_error("kafka encode producer failed. ret=%d", ret);
        return ret;
    }
    buf->write_2bytes(required_acks);
    buf->write_4bytes(timeout);
    
    if ((ret = topics.encode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka encode producer topics failed. ret=%d", ret);
    }
    
    return ret;
}

int SrsKafkaProducerRequest::decode(SrsBuffer* buf)
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = SrsKafkaRequest::decode(buf)) != ERROR_SUCCESS) {
        return ret;
    }
    
    if (!buf->require(2 + 4)) {
        ret = ERROR_KAFKA_CODEC_PRODUCER;
        srs_error("kafka decode producer failed. ret=%d", ret);
        return ret;
    }
    required_acks = buf->read_2bytes();
    timeout = buf->read_4bytes();
    
    if ((ret = topics.decode(buf)) != ERROR_SUCCESS) {
        srs_error("kafka decode producer topics failed. ret=%d", ret);
    }
    
    return ret;
}

SrsKafkaCorrelationPool* SrsKafkaCorrelationPool::_instance = new SrsKafkaCorrelationPool();

SrsKafkaCorrelationPool* SrsKafkaCorrelationPool::instance()
{
    return _instance;
}

SrsKafkaCorrelationPool::SrsKafkaCorrelationPool()
{
}

SrsKafkaCorrelationPool::~SrsKafkaCorrelationPool()
{
    correlation_ids.clear();
}

int32_t SrsKafkaCorrelationPool::generate_correlation_id()
{
    static int32_t cid = 1;
    return cid++;
}

SrsKafkaApiKey SrsKafkaCorrelationPool::set(int32_t correlation_id, SrsKafkaApiKey request)
{
    SrsKafkaApiKey previous = SrsKafkaApiKeyUnknown;
    
    std::map<int32_t, SrsKafkaApiKey>::iterator it = correlation_ids.find(correlation_id);
    if (it != correlation_ids.end()) {
        previous = it->second;
    }
    
    correlation_ids[correlation_id] = request;
    
    return previous;
}

SrsKafkaApiKey SrsKafkaCorrelationPool::unset(int32_t correlation_id)
{
    std::map<int32_t, SrsKafkaApiKey>::iterator it = correlation_ids.find(correlation_id);
    
    if (it != correlation_ids.end()) {
        SrsKafkaApiKey key = it->second;
        correlation_ids.erase(it);
        return key;
    }
    
    return SrsKafkaApiKeyUnknown;
}

SrsKafkaApiKey SrsKafkaCorrelationPool::get(int32_t correlation_id)
{
    if (correlation_ids.find(correlation_id) == correlation_ids.end()) {
        return SrsKafkaApiKeyUnknown;
    }
    
    return correlation_ids[correlation_id];
}

SrsKafkaProtocol::SrsKafkaProtocol(ISrsProtocolReaderWriter* io)
{
    skt = io;
    reader = new SrsFastStream();
}

SrsKafkaProtocol::~SrsKafkaProtocol()
{
    srs_freep(reader);
}

int SrsKafkaProtocol::send_and_free_message(SrsKafkaRequest* msg)
{
    int ret = ERROR_SUCCESS;
    
    // TODO: FIXME: refine for performance issue.
    SrsAutoFree(SrsKafkaRequest, msg);
    
    int size = msg->nb_bytes();
    if (size <= 0) {
        return ret;
    }
    
    // update the header of message.
    msg->update_header(size);
    
    // cache the request correlation id to discovery response message.
    SrsKafkaCorrelationPool* pool = SrsKafkaCorrelationPool::instance();
    pool->set(msg->correlation_id(), msg->api_key());
    
    // TODO: FIXME: refine for performance issue.
    char* bytes = new char[size];
    SrsAutoFreeA(char, bytes);
    
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

int SrsKafkaProtocol::recv_message(SrsKafkaResponse** pmsg)
{
    *pmsg = NULL;
    
    int ret = ERROR_SUCCESS;
    
    while (true) {
        SrsKafkaResponseHeader header;
        
        // ensure enough bytes for response header.
        if ((ret = reader->grow(skt, header.nb_bytes())) != ERROR_SUCCESS) {
            srs_error("kafka recv message failed. ret=%d", ret);
            return ret;
        }
        
        // decode response header.
        SrsBuffer buffer;
        if ((ret = buffer.initialize(reader->bytes(), reader->size())) != ERROR_SUCCESS) {
            return ret;
        }
        
        SrsBuffer* buf = &buffer;
        if ((ret = header.decode(buf)) != ERROR_SUCCESS) {
            srs_error("kafka decode response header failed. ret=%d", ret);
            return ret;
        }
        
        // skip the used buffer for header.
        buf->skip(-1 * buf->pos());
        
        // fetch cached api key.
        SrsKafkaCorrelationPool* pool = SrsKafkaCorrelationPool::instance();
        SrsKafkaApiKey key = pool->unset(header.correlation_id());
        srs_info("kafka got %d bytes response, key=%d", header.total_size(), header.correlation_id());
        
        // create message by cached api key.
        SrsKafkaResponse* res = NULL;
        switch (key) {
            case SrsKafkaApiKeyMetadataRequest:
                srs_info("kafka got metadata response");
                res = new SrsKafkaTopicMetadataResponse();
                break;
            case SrsKafkaApiKeyUnknown:
            default:
                break;
        }
        
        // ensure enough bytes to decode message.
        if ((ret = reader->grow(skt, header.total_size())) != ERROR_SUCCESS) {
            srs_freep(res);
            srs_error("kafka recv message body failed. ret=%d", ret);
            return ret;
        }
        
        // dropped message, fetch next.
        if (!res) {
            reader->skip(header.total_size());
            srs_warn("kafka ignore unknown message, size=%d.", header.total_size());
            continue;
        }
        
        // parse the whole message.
        if ((ret = res->decode(buf)) != ERROR_SUCCESS) {
            srs_freep(res);
            srs_error("kafka decode message failed. ret=%d", ret);
            return ret;
        }
        
        *pmsg = res;
        break;
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

int SrsKafkaClient::fetch_metadata(string topic, SrsKafkaTopicMetadataResponse** pmsg)
{
    *pmsg = NULL;
    
    int ret = ERROR_SUCCESS;
    
    SrsKafkaTopicMetadataRequest* req = new SrsKafkaTopicMetadataRequest();
    
    req->add_topic(topic);
    
    if ((ret = protocol->send_and_free_message(req)) != ERROR_SUCCESS) {
        srs_error("kafka send message failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = protocol->expect_message(pmsg)) != ERROR_SUCCESS) {
        srs_error("kafka recv response failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsKafkaClient::write_messages(std::string topic, int32_t partition, vector<SrsJsonObject*>& msgs)
{
    int ret = ERROR_SUCCESS;
    
    SrsKafkaProducerRequest* req = new SrsKafkaProducerRequest();
    
    // 0 the server will not send any response.
    req->required_acks = 0;
    // timeout of producer message.
    req->timeout = SRS_KAFKA_PRODUCER_MESSAGE_TIMEOUT_MS;
    
    // create the topic and partition to write message to.
    SrsKafkaProducerTopicMessages* topics = new SrsKafkaProducerTopicMessages();
    SrsKafkaProducerPartitionMessages* partitions = new SrsKafkaProducerPartitionMessages();
    
    topics->partitions.append(partitions);
    req->topics.append(topics);
    
    topics->topic_name.set_value(topic);
    partitions->partition = partition;
    
    // convert json objects to kafka raw messages.
    vector<SrsJsonObject*>::iterator it;
    for (it = msgs.begin(); it != msgs.end(); ++it) {
        SrsJsonObject* obj = *it;
        SrsKafkaRawMessage* msg = new SrsKafkaRawMessage();
        
        if ((ret = msg->create(obj)) != ERROR_SUCCESS) {
            srs_freep(msg);
            srs_freep(req);
            srs_error("kafka write messages failed. ret=%d", ret);
            return ret;
        }
        
        partitions->messages.append(msg);
    }
    
    partitions->message_set_size = partitions->messages.nb_bytes();
    
    // write to kafka cluster.
    if ((ret = protocol->send_and_free_message(req)) != ERROR_SUCCESS) {
        srs_error("kafka write producer message failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

vector<string> srs_kafka_array2vector(SrsKafkaArray<SrsKafkaString>* arr)
{
    vector<string> strs;
    
    for (int i = 0; i < arr->size(); i++) {
        SrsKafkaString* elem = arr->at(i);
        strs.push_back(elem->to_str());
    }
    
    return strs;
}

vector<string> srs_kafka_array2vector(SrsKafkaArray<int32_t>* arr)
{
    vector<string> strs;
    
    for (int i = 0; i < arr->size(); i++) {
        int32_t elem = arr->at(i);
        strs.push_back(srs_int2str(elem));
    }
    
    return strs;
}

#endif

