/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2019 Winlin
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

srs_error_t SrsKafkaString::encode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if (!buf->require(2)) {
        return srs_error_new(ERROR_KAFKA_CODEC_STRING, "requires 2 only %d bytes", buf->left());
    }
    buf->write_2bytes(_size);
    
    if (_size <= 0) {
        return err;
    }
    
    if (!buf->require(_size)) {
        return srs_error_new(ERROR_KAFKA_CODEC_STRING, "requires %d only %d bytes", _size, buf->left());
    }
    buf->write_bytes(data, _size);
    
    return err;
}

srs_error_t SrsKafkaString::decode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if (!buf->require(2)) {
        return srs_error_new(ERROR_KAFKA_CODEC_STRING, "requires 2 only %d bytes", buf->left());
    }
    _size = buf->read_2bytes();
    
    if (_size != -1 && _size < 0) {
        return srs_error_new(ERROR_KAFKA_CODEC_STRING, "invalid size=%d", _size);
    }
    
    if (_size <= 0) {
        return err;
    }
    
    if (!buf->require(_size)) {
        return srs_error_new(ERROR_KAFKA_CODEC_STRING, "requires %d only %d bytes", _size, buf->left());
    }
    
    srs_freepa(data);
    data = new char[_size];
    
    buf->read_bytes(data, _size);
    
    return err;
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

uint32_t SrsKafkaBytes::crc32(uint32_t previous)
{
    char bsize[4];
    SrsBuffer(bsize, 4).write_4bytes(_size);
    
    if (_size <= 0) {
        return srs_crc32_ieee(bsize, 4, previous);
    }
    
    uint32_t crc = srs_crc32_ieee(bsize, 4, previous);
    crc = srs_crc32_ieee(_data, _size, crc);
    
    return crc;
}

int SrsKafkaBytes::nb_bytes()
{
    return 4 + (_size == -1? 0 : _size);
}

srs_error_t SrsKafkaBytes::encode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if (!buf->require(4)) {
        return srs_error_new(ERROR_KAFKA_CODEC_BYTES, "requires 4 only %d bytes", buf->left());
    }
    buf->write_4bytes(_size);
    
    if (_size <= 0) {
        return err;
    }
    
    if (!buf->require(_size)) {
        return srs_error_new(ERROR_KAFKA_CODEC_BYTES, "requires %d only %d bytes", _size, buf->left());
    }
    buf->write_bytes(_data, _size);
    
    return err;
}

srs_error_t SrsKafkaBytes::decode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if (!buf->require(4)) {
        return srs_error_new(ERROR_KAFKA_CODEC_BYTES, "requires 4 only %d bytes", buf->left());
    }
    _size = buf->read_4bytes();
    
    if (_size != -1 && _size < 0) {
        return srs_error_new(ERROR_KAFKA_CODEC_BYTES, "invalid size=%d", _size);
    }
    
    if (!buf->require(_size)) {
        return srs_error_new(ERROR_KAFKA_CODEC_BYTES, "requires %d only %d bytes", _size, buf->left());
    }
    
    srs_freepa(_data);
    _data = new char[_size];
    buf->read_bytes(_data, _size);
    
    return err;
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

srs_error_t SrsKafkaRequestHeader::encode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if (!buf->require(4 + _size)) {
        return srs_error_new(ERROR_KAFKA_CODEC_REQUEST, "requires %d only %d bytes", 4 + _size, buf->left());
    }
    
    buf->write_4bytes(_size);
    buf->write_2bytes(_api_key);
    buf->write_2bytes(api_version);
    buf->write_4bytes(_correlation_id);
    
    if ((err = client_id->encode(buf)) != srs_success) {
        return srs_error_wrap(err, "encode client_id");
    }
    
    return err;
}

srs_error_t SrsKafkaRequestHeader::decode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if (!buf->require(4)) {
        return srs_error_new(ERROR_KAFKA_CODEC_REQUEST, "requires %d only %d bytes", 4, buf->left());
    }
    _size = buf->read_4bytes();
    
    if (_size <= 0) {
        srs_warn("kafka got empty request");
        return err;
    }
    
    if (!buf->require(_size)) {
        return srs_error_new(ERROR_KAFKA_CODEC_REQUEST, "requires %d only %d bytes", _size, buf->left());
    }
    _api_key = buf->read_2bytes();
    api_version = buf->read_2bytes();
    _correlation_id = buf->read_4bytes();
    
    if ((err = client_id->decode(buf)) != srs_success) {
        return srs_error_wrap(err, "decode client_id");
    }
    
    return err;
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

srs_error_t SrsKafkaResponseHeader::encode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if (!buf->require(4 + _size)) {
        return srs_error_new(ERROR_KAFKA_CODEC_RESPONSE, "requires %d only %d bytes", 4 + _size, buf->left());
    }
    
    buf->write_4bytes(_size);
    buf->write_4bytes(_correlation_id);
    
    return err;
}

srs_error_t SrsKafkaResponseHeader::decode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if (!buf->require(4)) {
        return srs_error_new(ERROR_KAFKA_CODEC_RESPONSE, "requires %d only %d bytes", 4, buf->left());
    }
    _size = buf->read_4bytes();
    
    if (_size <= 0) {
        srs_warn("kafka got empty response");
        return err;
    }
    
    if (!buf->require(_size)) {
        return srs_error_new(ERROR_KAFKA_CODEC_RESPONSE, "requires %d only %d bytes", _size, buf->left());
    }
    _correlation_id = buf->read_4bytes();
    
    return err;
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

srs_error_t SrsKafkaRawMessage::create(SrsJsonObject* obj)
{
    srs_error_t err = srs_success;
    
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
    
    return err;
}

int SrsKafkaRawMessage::raw_message_size()
{
    return 4 + 1 + 1 + key->nb_bytes() + value->nb_bytes();
}

int SrsKafkaRawMessage::nb_bytes()
{
    return 8 + 4 + 4 + 1 + 1 + key->nb_bytes() + value->nb_bytes();
}

srs_error_t SrsKafkaRawMessage::encode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    int nb_required = 8 + 4 + 4 + 1 + 1;
    if (!buf->require(nb_required)) {
        return srs_error_new(ERROR_KAFKA_CODEC_MESSAGE, "requires %d only %d bytes", nb_required, buf->left());
    }
    buf->write_8bytes(offset);
    buf->write_4bytes(message_size);
    buf->write_4bytes(crc);
    buf->write_1bytes(magic_byte);
    buf->write_1bytes(attributes);
    
    if ((err = key->encode(buf)) != srs_success) {
        return srs_error_wrap(err, "encode key");
    }
    
    if ((err = value->encode(buf)) != srs_success) {
        return srs_error_wrap(err, "encode value");
    }
    
    return err;
}

srs_error_t SrsKafkaRawMessage::decode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    int nb_required = 8 + 4 + 4 + 1 + 1;
    if (!buf->require(nb_required)) {
        return srs_error_new(ERROR_KAFKA_CODEC_MESSAGE, "requires %d only %d bytes", nb_required, buf->left());
    }
    offset = buf->read_8bytes();
    message_size = buf->read_4bytes();
    crc = buf->read_4bytes();
    magic_byte = buf->read_1bytes();
    attributes = buf->read_1bytes();
    
    if ((err = key->decode(buf)) != srs_success) {
        return srs_error_wrap(err, "decode key");
    }
    
    if ((err = value->decode(buf)) != srs_success) {
        return srs_error_wrap(err, "decode value");
    }
    
    return err;
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

srs_error_t SrsKafkaRawMessageSet::encode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    vector<SrsKafkaRawMessage*>::iterator it;
    for (it = messages.begin(); it != messages.end(); ++it) {
        SrsKafkaRawMessage* message = *it;
        if ((err = message->encode(buf)) != srs_success) {
            return srs_error_wrap(err, "encode message");
        }
    }
    
    return err;
}

srs_error_t SrsKafkaRawMessageSet::decode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    while (!buf->empty()) {
        SrsKafkaRawMessage* message = new SrsKafkaRawMessage();
        
        if ((err = message->decode(buf)) != srs_success) {
            srs_freep(message);
            return srs_error_wrap(err, "decode message");
        }
        
        messages.push_back(message);
    }
    
    return err;
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

srs_error_t SrsKafkaRequest::encode(SrsBuffer* buf)
{
    return header.encode(buf);
}

srs_error_t SrsKafkaRequest::decode(SrsBuffer* buf)
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

srs_error_t SrsKafkaResponse::encode(SrsBuffer* buf)
{
    return header.encode(buf);
}

srs_error_t SrsKafkaResponse::decode(SrsBuffer* buf)
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

srs_error_t SrsKafkaTopicMetadataRequest::encode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsKafkaRequest::encode(buf)) != srs_success) {
        return srs_error_wrap(err, "encode request");
    }
    
    if ((err = topics.encode(buf)) != srs_success) {
        return srs_error_wrap(err, "encode topics");
    }
    
    return err;
}

srs_error_t SrsKafkaTopicMetadataRequest::decode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsKafkaRequest::decode(buf)) != srs_success) {
        return srs_error_wrap(err, "decode request");
    }
    
    if ((err = topics.decode(buf)) != srs_success) {
        return srs_error_wrap(err, "decode topics");
    }
    
    return err;
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

srs_error_t SrsKafkaBroker::encode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if (!buf->require(4)) {
        return srs_error_new(ERROR_KAFKA_CODEC_METADATA, "requires %d only %d bytes", 4, buf->left());
    }
    buf->write_4bytes(node_id);
    
    if ((err = host.encode(buf)) != srs_success) {
        return srs_error_wrap(err, "host");
    }
    
    if (!buf->require(4)) {
        return srs_error_new(ERROR_KAFKA_CODEC_METADATA, "requires %d only %d bytes", 4, buf->left());
    }
    buf->write_4bytes(port);
    
    return err;
}

srs_error_t SrsKafkaBroker::decode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if (!buf->require(4)) {
        return srs_error_new(ERROR_KAFKA_CODEC_METADATA, "requires %d only %d bytes", 4, buf->left());
    }
    node_id = buf->read_4bytes();
    
    if ((err = host.decode(buf)) != srs_success) {
        return srs_error_wrap(err, "host");
    }
    
    if (!buf->require(4)) {
        return srs_error_new(ERROR_KAFKA_CODEC_METADATA, "requires %d only %d bytes", 4, buf->left());
    }
    port = buf->read_4bytes();
    
    return err;
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

srs_error_t SrsKafkaPartitionMetadata::encode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    int nb_required = 2 + 4 + 4;
    if (!buf->require(nb_required)) {
        return srs_error_new(ERROR_KAFKA_CODEC_METADATA, "requires %d only %d bytes", nb_required, buf->left());
    }
    buf->write_2bytes(error_code);
    buf->write_4bytes(partition_id);
    buf->write_4bytes(leader);
    
    if ((err = replicas.encode(buf)) != srs_success) {
        return srs_error_wrap(err, "replicas");
    }
    if ((err = isr.encode(buf)) != srs_success) {
        return srs_error_wrap(err, "isr");
    }
    
    return err;
}

srs_error_t SrsKafkaPartitionMetadata::decode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    int nb_required = 2 + 4 + 4;
    if (!buf->require(nb_required)) {
        return srs_error_new(ERROR_KAFKA_CODEC_METADATA, "requires %d only %d bytes", nb_required, buf->left());
    }
    error_code = buf->read_2bytes();
    partition_id = buf->read_4bytes();
    leader = buf->read_4bytes();
    
    if ((err = replicas.decode(buf)) != srs_success) {
        return srs_error_wrap(err, "replicas");
    }
    if ((err = isr.decode(buf)) != srs_success) {
        return srs_error_wrap(err, "isr");
    }
    
    return err;
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

srs_error_t SrsKafkaTopicMetadata::encode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if (!buf->require(2)) {
        return srs_error_new(ERROR_KAFKA_CODEC_METADATA, "requires %d only %d bytes", 2, buf->left());
    }
    buf->write_2bytes(error_code);
    
    if ((err = name.encode(buf)) != srs_success) {
        return srs_error_wrap(err, "name");
    }
    
    if ((err = metadatas.encode(buf)) != srs_success) {
        return srs_error_wrap(err, "metadatas");
    }
    
    return err;
}

srs_error_t SrsKafkaTopicMetadata::decode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if (!buf->require(2)) {
        return srs_error_new(ERROR_KAFKA_CODEC_METADATA, "requires %d only %d bytes", 2, buf->left());
    }
    error_code = buf->read_2bytes();
    
    if ((err = name.decode(buf)) != srs_success) {
        return srs_error_wrap(err, "name");
    }
    
    if ((err = metadatas.decode(buf)) != srs_success) {
        return srs_error_wrap(err, "metadatas");
    }
    
    return err;
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

srs_error_t SrsKafkaTopicMetadataResponse::encode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsKafkaResponse::encode(buf)) != srs_success) {
        return srs_error_wrap(err, "encode response");
    }
    
    if ((err = brokers.encode(buf)) != srs_success) {
        return srs_error_wrap(err, "brokers");
    }
    
    if ((err = metadatas.encode(buf)) != srs_success) {
        return srs_error_wrap(err, "metadatas");
    }
    
    return err;
}

srs_error_t SrsKafkaTopicMetadataResponse::decode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsKafkaResponse::decode(buf)) != srs_success) {
        return srs_error_wrap(err, "decode response");
    }
    
    if ((err = brokers.decode(buf)) != srs_success) {
        return srs_error_wrap(err, "brokers");
    }
    
    if ((err = metadatas.decode(buf)) != srs_success) {
        return srs_error_wrap(err, "metadatas");
    }
    
    return err;
}

int SrsKafkaProducerPartitionMessages::nb_bytes()
{
    return 4 + 4 + messages.nb_bytes();
}

srs_error_t SrsKafkaProducerPartitionMessages::encode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    int nb_required = 4 + 4;
    if (!buf->require(nb_required)) {
        return srs_error_new(ERROR_KAFKA_CODEC_PRODUCER, "requires %d only %d bytes", nb_required, buf->left());
    }
    buf->write_4bytes(partition);
    buf->write_4bytes(message_set_size);
    
    if ((err = messages.encode(buf)) != srs_success) {
        return srs_error_wrap(err, "messages");
    }
    
    return err;
}

srs_error_t SrsKafkaProducerPartitionMessages::decode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    int nb_required = 4 + 4;
    if (!buf->require(nb_required)) {
        return srs_error_new(ERROR_KAFKA_CODEC_PRODUCER, "requires %d only %d bytes", nb_required, buf->left());
    }
    partition = buf->read_4bytes();
    message_set_size = buf->read_4bytes();
    
    // for the message set decode util empty, we must create a new buffer when
    // there exists other objects after message set.
    if (buf->left() != message_set_size) {
        SrsBuffer* tbuf = new SrsBuffer(buf->data() + buf->pos(), message_set_size);
        SrsAutoFree(SrsBuffer, tbuf);
        
        if ((err = messages.decode(buf)) != srs_success) {
            return srs_error_wrap(err, "messages");
        }
    } else {
        if ((err = messages.decode(buf)) != srs_success) {
            return srs_error_wrap(err, "messages");
        }
    }
    
    return err;
}

int SrsKafkaProducerTopicMessages::nb_bytes()
{
    return topic_name.nb_bytes() + partitions.nb_bytes();
}

srs_error_t SrsKafkaProducerTopicMessages::encode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = topic_name.encode(buf)) != srs_success) {
        return srs_error_wrap(err, "topic_name");
    }
    
    if ((err = partitions.encode(buf)) != srs_success) {
        return srs_error_wrap(err, "partitions");
    }
    
    return err;
}

srs_error_t SrsKafkaProducerTopicMessages::decode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = topic_name.decode(buf)) != srs_success) {
        return srs_error_wrap(err, "topic_name");
    }
    
    if ((err = partitions.decode(buf)) != srs_success) {
        return srs_error_wrap(err, "partitions");
    }
    
    return err;
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

srs_error_t SrsKafkaProducerRequest::encode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsKafkaRequest::encode(buf)) != srs_success) {
        return srs_error_wrap(err, "encode request");
    }
    
    int nb_required = 2 + 4;
    if (!buf->require(nb_required)) {
        return srs_error_new(ERROR_KAFKA_CODEC_PRODUCER, "requires %d only %d bytes", nb_required, buf->left());
    }
    buf->write_2bytes(required_acks);
    buf->write_4bytes(timeout);
    
    if ((err = topics.encode(buf)) != srs_success) {
        return srs_error_wrap(err, "encode topics");
    }
    
    return err;
}

srs_error_t SrsKafkaProducerRequest::decode(SrsBuffer* buf)
{
    srs_error_t err = srs_success;
    
    if ((err = SrsKafkaRequest::decode(buf)) != srs_success) {
        return srs_error_wrap(err, "decode request");
    }
    
    int nb_required = 2 + 4;
    if (!buf->require(nb_required)) {
        return srs_error_new(ERROR_KAFKA_CODEC_PRODUCER, "requires %d only %d bytes", nb_required, buf->left());
    }
    required_acks = buf->read_2bytes();
    timeout = buf->read_4bytes();
    
    if ((err = topics.decode(buf)) != srs_success) {
        return srs_error_wrap(err, "decode topics");
    }
    
    return err;
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

SrsKafkaProtocol::SrsKafkaProtocol(ISrsProtocolReadWriter* io)
{
    skt = io;
    reader = new SrsFastStream();
}

SrsKafkaProtocol::~SrsKafkaProtocol()
{
    srs_freep(reader);
}

srs_error_t SrsKafkaProtocol::send_and_free_message(SrsKafkaRequest* msg)
{
    srs_error_t err = srs_success;
    
    // TODO: FIXME: refine for performance issue.
    SrsAutoFree(SrsKafkaRequest, msg);
    
    int size = msg->nb_bytes();
    if (size <= 0) {
        return err;
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
    SrsBuffer* buf = new SrsBuffer(bytes, size);
    SrsAutoFree(SrsBuffer, buf);
    
    if ((err = msg->encode(buf)) != srs_success) {
        return srs_error_wrap(err, "encode msg");
    }
    
    if ((err = skt->write(bytes, size, NULL)) != srs_success) {
        return srs_error_wrap(err, "write msg");
    }
    
    return err;
}

srs_error_t SrsKafkaProtocol::recv_message(SrsKafkaResponse** pmsg)
{
    *pmsg = NULL;
    
    srs_error_t err = srs_success;
    
    while (true) {
        SrsKafkaResponseHeader header;
        
        // ensure enough bytes for response header.
        if ((err = reader->grow(skt, header.nb_bytes())) != srs_success) {
            return srs_error_wrap(err, "grow buffer");
        }
        
        // decode response header.
        SrsBuffer* buf = new SrsBuffer(reader->bytes(), reader->size());
        SrsAutoFree(SrsBuffer, buf);
        
        if ((err = header.decode(buf)) != srs_success) {
            return srs_error_wrap(err, "decode header");
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
        if ((err = reader->grow(skt, header.total_size())) != srs_success) {
            srs_freep(res);
            return srs_error_wrap(err, "grow buffer");
        }
        
        // dropped message, fetch next.
        if (!res) {
            reader->skip(header.total_size());
            srs_warn("kafka ignore unknown message, size=%d.", header.total_size());
            continue;
        }
        
        // parse the whole message.
        if ((err = res->decode(buf)) != srs_success) {
            srs_freep(res);
            return srs_error_wrap(err, "decode response");
        }
        
        *pmsg = res;
        break;
    }
    
    return err;
}

SrsKafkaClient::SrsKafkaClient(ISrsProtocolReadWriter* io)
{
    protocol = new SrsKafkaProtocol(io);
}

SrsKafkaClient::~SrsKafkaClient()
{
    srs_freep(protocol);
}

srs_error_t SrsKafkaClient::fetch_metadata(string topic, SrsKafkaTopicMetadataResponse** pmsg)
{
    *pmsg = NULL;
    
    srs_error_t err = srs_success;
    
    SrsKafkaTopicMetadataRequest* req = new SrsKafkaTopicMetadataRequest();
    
    req->add_topic(topic);
    
    if ((err = protocol->send_and_free_message(req)) != srs_success) {
        return srs_error_wrap(err, "send request");
    }
    
    if ((err = protocol->expect_message(pmsg)) != srs_success) {
        return srs_error_wrap(err, "expect message");
    }
    
    return err;
}

srs_error_t SrsKafkaClient::write_messages(std::string topic, int32_t partition, vector<SrsJsonObject*>& msgs)
{
    srs_error_t err = srs_success;
    
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
        
        if ((err = msg->create(obj)) != srs_success) {
            srs_freep(msg);
            srs_freep(req);
            return srs_error_wrap(err, "create message");
        }
        
        partitions->messages.append(msg);
    }
    
    partitions->message_set_size = partitions->messages.nb_bytes();
    
    // write to kafka cluster.
    if ((err = protocol->send_and_free_message(req)) != srs_success) {
        return srs_error_wrap(err, "send request");
    }
    
    return err;
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

