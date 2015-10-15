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

#ifdef SRS_AUTO_KAFKA

SrsKafkaString::SrsKafkaString()
{
    size = -1;
    data = NULL;
}

SrsKafkaString::~SrsKafkaString()
{
    srs_freep(data);
}

bool SrsKafkaString::null()
{
    return size == -1;
}

bool SrsKafkaString::empty()
{
    return size <= 0;
}

int SrsKafkaString::total_size()
{
    return 2 + (size == -1? 0 : size);
}

SrsKafkaBytes::SrsKafkaBytes()
{
    size = -1;
    data = NULL;
}

SrsKafkaBytes::~SrsKafkaBytes()
{
    srs_freep(data);
}

bool SrsKafkaBytes::null()
{
    return size == -1;
}

bool SrsKafkaBytes::empty()
{
    return size <= 0;
}

int SrsKafkaBytes::total_size()
{
    return 4 + (size == -1? 0 : size);
}

SrsKafkaRequestHeader::SrsKafkaRequestHeader()
{
    size = 0;
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
    return 2 + 2 + 4 + client_id->total_size();
}

int SrsKafkaRequestHeader::message_size()
{
    return size - header_size();
}

int SrsKafkaRequestHeader::total_size()
{
    return 4 + size;
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

SrsKafkaResponseHeader::SrsKafkaResponseHeader()
{
    size = 0;
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
    return size - header_size();
}

int SrsKafkaResponseHeader::total_size()
{
    return 4 + size;
}

SrsKafkaMessage::SrsKafkaMessage()
{
    offset = 0;
    message_size = 0;
    
    crc = 0;
    magic_byte = attributes = 0;
    key = new SrsKafkaBytes();
    value = new SrsKafkaBytes();
}

SrsKafkaMessage::~SrsKafkaMessage()
{
    srs_freep(key);
    srs_freep(value);
}

SrsKafkaMessageSet::SrsKafkaMessageSet()
{
}

SrsKafkaMessageSet::~SrsKafkaMessageSet()
{
    vector<SrsKafkaMessage*>::iterator it;
    for (it = messages.begin(); it != messages.end(); ++it) {
        SrsKafkaMessage* message = *it;
        srs_freep(message);
    }
    messages.clear();
}

SrsKafkaTopicMetadataRequest::SrsKafkaTopicMetadataRequest()
{
    header.set_api_key(SrsKafkaApiKeyMetadataRequest);
}

SrsKafkaTopicMetadataRequest::~SrsKafkaTopicMetadataRequest()
{
}

SrsKafkaProtocol::SrsKafkaProtocol(ISrsProtocolReaderWriter* io)
{
    skt = io;
}

SrsKafkaProtocol::~SrsKafkaProtocol()
{
}

int SrsKafkaProtocol::send_and_free_message(SrsKafkaMessage* msg)
{
    int ret = ERROR_SUCCESS;
    
    // TODO: FIXME: implements it.
    
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
    
    // TODO: FIXME: implements it.
    
    return ret;
}

#endif

