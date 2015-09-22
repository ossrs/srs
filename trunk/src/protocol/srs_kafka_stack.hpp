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

#ifndef SRS_PROTOCOL_KAFKA_HPP
#define SRS_PROTOCOL_KAFKA_HPP

/*
#include <srs_kafka_stack.hpp>
*/
#include <srs_core.hpp>

#include <vector>

// https://cwiki.apache.org/confluence/display/KAFKA/A+Guide+To+The+Kafka+Protocol#AGuideToTheKafkaProtocol-ApiKeys
enum SrsKafkaApiKey
{
    SrsKafkaApiKeyProduceRequest = 0,
    SrsKafkaApiKeyFetchRequest = 1,
    SrsKafkaApiKeyOffsetRequest = 2,
    SrsKafkaApiKeyMetadataRequest = 3,
    /* Non-user facing control APIs 4-7 */
    SrsKafkaApiKeyOffsetCommitRequest = 8,
    SrsKafkaApiKeyOffsetFetchRequest = 9,
    SrsKafkaApiKeyConsumerMetadataRequest = 10,
};

/**
 * These types consist of a signed integer giving a length N followed by N bytes of content. 
 * A length of -1 indicates null. string uses an int16 for its size, and bytes uses an int32.
 * @see https://cwiki.apache.org/confluence/display/KAFKA/A+Guide+To+The+Kafka+Protocol#AGuideToTheKafkaProtocol-ProtocolPrimitiveTypes
 */
class SrsKafkaString
{
private:
    int16_t size;
    char* data;
public:
    SrsKafkaString();
    virtual ~SrsKafkaString();
public:
    virtual bool null();
    virtual bool empty();
    virtual int total_size();
};

/**
 * These types consist of a signed integer giving a length N followed by N bytes of content.
 * A length of -1 indicates null. string uses an int16 for its size, and bytes uses an int32.
 * @see https://cwiki.apache.org/confluence/display/KAFKA/A+Guide+To+The+Kafka+Protocol#AGuideToTheKafkaProtocol-ProtocolPrimitiveTypes
 */
class SrsKafkaBytes
{
private:
    int32_t size;
    char* data;
public:
    SrsKafkaBytes();
    virtual ~SrsKafkaBytes();
public:
    virtual bool null();
    virtual bool empty();
    virtual int total_size();
};

/**
 * This is a notation for handling repeated structures. These will always be encoded as an 
 * int32 size containing the length N followed by N repetitions of the structure which can 
 * itself be made up of other primitive types. In the BNF grammars below we will show an 
 * array of a structure foo as [foo].
 * 
 * Usage:
 *      SrsKafkaArray<SrsKafkaBytes> body;
 *      body.append(new SrsKafkaBytes());
 *
 * @see https://cwiki.apache.org/confluence/display/KAFKA/A+Guide+To+The+Kafka+Protocol#AGuideToTheKafkaProtocol-Requests
 */
template<typename T>
class SrsKafkaArray
{
private:
    int length;
    std::vector<T*> elems;
    typedef typename std::vector<T*>::iterator SrsIterator;
public:
    SrsKafkaArray()
    {
        length = 0;
    }
    virtual ~SrsKafkaArray()
    {
        for (SrsIterator it = elems.begin(); it != elems.end(); ++it) {
            T* elem = *it;
            srs_freep(elem);
        }
        elems.clear();
    }
public:
    virtual void append(T* elem)
    {
        length++;
        elems.push_back(elem);
    }
};

/**
 * the header of request, includes the size of request.
 * @see https://cwiki.apache.org/confluence/display/KAFKA/A+Guide+To+The+Kafka+Protocol#AGuideToTheKafkaProtocol-Requests
 */
class SrsKafkaRequestHeader
{
private:
    /**
     * The MessageSize field gives the size of the subsequent request or response
     * message in bytes. The client can read requests by first reading this 4 byte
     * size as an integer N, and then reading and parsing the subsequent N bytes
     * of the request.
     */
    int32_t size;
private:
    /**
     * This is a numeric id for the API being invoked (i.e. is it 
     * a metadata request, a produce request, a fetch request, etc).
     * @remark MetadataRequest | ProduceRequest | FetchRequest | OffsetRequest | OffsetCommitRequest | OffsetFetchRequest
     */
    int16_t api_key;
    /**
     * This is a numeric version number for this api. We version each API and 
     * this version number allows the server to properly interpret the request 
     * as the protocol evolves. Responses will always be in the format corresponding 
     * to the request version. Currently the supported version for all APIs is 0.
     */
    int16_t api_version;
    /**
     * This is a user-supplied integer. It will be passed back in
     * the response by the server, unmodified. It is useful for matching
     * request and response between the client and server.
     */
    int32_t correlation_id;
    /**
     * This is a user supplied identifier for the client application. 
     * The user can use any identifier they like and it will be used
     * when logging errors, monitoring aggregates, etc. For example,
     * one might want to monitor not just the requests per second overall,
     * but the number coming from each client application (each of
     * which could reside on multiple servers). This id acts as a
     * logical grouping across all requests from a particular client.
     */
    SrsKafkaString* client_id;
public:
    SrsKafkaRequestHeader();
    virtual ~SrsKafkaRequestHeader();
public:
    /**
     * the size of header, exclude the 4bytes size.
     * @remark total_size = 4 + header_size + message_size.
     */
    virtual int header_size();
    /**
     * the size of message, the left bytes left after the header.
     * @remark total_size = 4 + header_size + message_size.
     */
    virtual int message_size();
    /**
     * the total size of the request, 4bytes + size of header and message.
     * @remark total_size = 4 + header_size + message_size.
     */
    virtual int total_size();
public:
    /**
     * the api key enumeration.
     * @see https://cwiki.apache.org/confluence/display/KAFKA/A+Guide+To+The+Kafka+Protocol#AGuideToTheKafkaProtocol-ApiKeys
     */
    virtual bool is_producer_request();
    virtual bool is_fetch_request();
    virtual bool is_offset_request();
    virtual bool is_metadata_request();
    virtual bool is_offset_commit_request();
    virtual bool is_offset_fetch_request();
    virtual bool is_consumer_metadata_request();
    // set the api key.
    virtual void set_api_key(SrsKafkaApiKey key);
};

/**
 * the common kafka response.
 * The response will always match the paired request (e.g. we will 
 * send a MetadataResponse in return to a MetadataRequest).
 * @see https://cwiki.apache.org/confluence/display/KAFKA/A+Guide+To+The+Kafka+Protocol#AGuideToTheKafkaProtocol-Responses
 */
class SrsKafkaResponse
{
protected:
    /**
     * The server passes back whatever integer the client supplied as the correlation in the request.
     */
    int32_t correlation_id;
public:
    SrsKafkaResponse();
    virtual ~SrsKafkaResponse();
};

/**
 * the kafka message in message set.
 * @see https://cwiki.apache.org/confluence/display/KAFKA/A+Guide+To+The+Kafka+Protocol#AGuideToTheKafkaProtocol-Messagesets
 */
struct SrsKafkaMessage
{
// metadata.
public:
    /**
     * This is the offset used in kafka as the log sequence number. When the
     * producer is sending messages it doesn't actually know the offset and
     * can fill in any value here it likes.
     */
    int64_t offset;
    /**
     * the size of this message.
     */
    int32_t message_size;
// message.
public:
    /**
     * The CRC is the CRC32 of the remainder of the message bytes. 
     * This is used to check the integrity of the message on the broker and consumer.
     */
    int32_t crc;
    /**
     * This is a version id used to allow backwards compatible evolution 
     * of the message binary format. The current value is 0.
     */
    int8_t magic_byte;
    /**
     * This byte holds metadata attributes about the message.
     * The lowest 2 bits contain the compression codec used
     * for the message. The other bits should be set to 0.
     */
    int8_t attributes;
    /**
     * The key is an optional message key that was used for 
     * partition assignment. The key can be null.
     */
    SrsKafkaBytes* key;
    /**
     * The value is the actual message contents as an opaque byte array.
     * Kafka supports recursive messages in which case this may itself
     * contain a message set. The message can be null.
     */
    SrsKafkaBytes* value;
public:
    SrsKafkaMessage();
    virtual ~SrsKafkaMessage();
};

/**
 * a set of kafka message.
 * @see https://cwiki.apache.org/confluence/display/KAFKA/A+Guide+To+The+Kafka+Protocol#AGuideToTheKafkaProtocol-Messagesets
 */
class SrsKafkaMessageSet
{
private:
    std::vector<SrsKafkaMessage*> messages;
public:
    SrsKafkaMessageSet();
    virtual ~SrsKafkaMessageSet();
};

/**
 * request the metadata from broker.
 * This API answers the following questions:
 *      What topics exist?
 *      How many partitions does each topic have?
 *      Which broker is currently the leader for each partition?
 *      What is the host and port for each of these brokers?
 * This is the only request that can be addressed to any broker in the cluster.
 *
 * Since there may be many topics the client can give an optional list of topic 
 * names in order to only return metadata for a subset of topics.
 *
 * @see https://cwiki.apache.org/confluence/display/KAFKA/A+Guide+To+The+Kafka+Protocol#AGuideToTheKafkaProtocol-MetadataAPI
 */
class SrsKafkaTopicMetadataRequest
{
private:
    SrsKafkaRequestHeader header;
    SrsKafkaArray<SrsKafkaString> request;
public:
    SrsKafkaTopicMetadataRequest();
    virtual ~SrsKafkaTopicMetadataRequest();
};

#endif

