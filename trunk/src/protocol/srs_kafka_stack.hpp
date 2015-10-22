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
#include <string>
#include <map>

#include <srs_kernel_buffer.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>

class SrsFastStream;
class ISrsProtocolReaderWriter;

#ifdef SRS_AUTO_KAFKA

/**
 * the api key used to identify the request type.
 * @see https://cwiki.apache.org/confluence/display/KAFKA/A+Guide+To+The+Kafka+Protocol#AGuideToTheKafkaProtocol-ApiKeys
 */
enum SrsKafkaApiKey
{
    SrsKafkaApiKeyUnknown = -1,
    
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
class SrsKafkaString : public ISrsCodec
{
private:
    int16_t _size;
    char* data;
public:
    SrsKafkaString();
    SrsKafkaString(std::string v);
    virtual ~SrsKafkaString();
public:
    virtual bool null();
    virtual bool empty();
// interface ISrsCodec
public:
    virtual int size();
    virtual int encode(SrsBuffer* buf);
    virtual int decode(SrsBuffer* buf);
};

/**
 * These types consist of a signed integer giving a length N followed by N bytes of content.
 * A length of -1 indicates null. string uses an int16 for its size, and bytes uses an int32.
 * @see https://cwiki.apache.org/confluence/display/KAFKA/A+Guide+To+The+Kafka+Protocol#AGuideToTheKafkaProtocol-ProtocolPrimitiveTypes
 */
class SrsKafkaBytes : public ISrsCodec
{
private:
    int32_t _size;
    char* data;
public:
    SrsKafkaBytes();
    SrsKafkaBytes(const char* v, int nb_v);
    virtual ~SrsKafkaBytes();
public:
    virtual bool null();
    virtual bool empty();
// interface ISrsCodec
public:
    virtual int size();
    virtual int encode(SrsBuffer* buf);
    virtual int decode(SrsBuffer* buf);
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
 * @remark array elem is the T*, which must be ISrsCodec*
 *
 * @see https://cwiki.apache.org/confluence/display/KAFKA/A+Guide+To+The+Kafka+Protocol#AGuideToTheKafkaProtocol-Requests
 */
template<typename T>
class SrsKafkaArray : public ISrsCodec
{
private:
    int32_t length;
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
// interface ISrsCodec
public:
    virtual int size()
    {
        int s = 4;
        
        for (SrsIterator it = elems.begin(); it != elems.end(); ++it) {
            T* elem = *it;
            s += elem->size();
        }
        
        return s;
    }
    virtual int encode(SrsBuffer* buf)
    {
        int ret = ERROR_SUCCESS;
        
        if (!buf->require(4)) {
            ret = ERROR_KAFKA_CODEC_ARRAY;
            srs_error("kafka encode array failed. ret=%d", ret);
            return ret;
        }
        buf->write_4bytes(length);
        
        for (SrsIterator it = elems.begin(); it != elems.end(); ++it) {
            T* elem = *it;
            if ((ret = elem->encode(buf)) != ERROR_SUCCESS) {
                srs_error("kafka encode array elem failed. ret=%d", ret);
                return ret;
            }
        }
        
        return ret;
    }
    virtual int decode(SrsBuffer* buf)
    {
        int ret = ERROR_SUCCESS;
        
        if (!buf->require(4)) {
            ret = ERROR_KAFKA_CODEC_ARRAY;
            srs_error("kafka decode array failed. ret=%d", ret);
            return ret;
        }
        length = buf->read_4bytes();
        
        for (int i = 0; i < length; i++) {
            T* elem = new T();
            if ((ret = elem->decode(buf)) != ERROR_SUCCESS) {
                srs_error("kafka decode array elem failed. ret=%d", ret);
                srs_freep(elem);
                return ret;
            }
            
            elems.push_back(elem);
        }
        
        return ret;
    }
};
template<>
class SrsKafkaArray<int32_t> : public ISrsCodec
{
private:
    int32_t length;
    std::vector<int32_t> elems;
    typedef std::vector<int32_t>::iterator SrsIterator;
public:
    SrsKafkaArray()
    {
        length = 0;
    }
    virtual ~SrsKafkaArray()
    {
        elems.clear();
    }
public:
    virtual void append(int32_t elem)
    {
        length++;
        elems.push_back(elem);
    }
    // interface ISrsCodec
public:
    virtual int size()
    {
        return 4 + sizeof(int32_t) * (int)elems.size();
    }
    virtual int encode(SrsBuffer* buf)
    {
        int ret = ERROR_SUCCESS;
        
        if (!buf->require(4 + sizeof(int32_t) * (int)elems.size())) {
            ret = ERROR_KAFKA_CODEC_ARRAY;
            srs_error("kafka encode array failed. ret=%d", ret);
            return ret;
        }
        buf->write_4bytes(length);
        
        for (SrsIterator it = elems.begin(); it != elems.end(); ++it) {
            int32_t elem = *it;
            buf->write_4bytes(elem);
        }
        
        return ret;
    }
    virtual int decode(SrsBuffer* buf)
    {
        int ret = ERROR_SUCCESS;
        
        if (!buf->require(4)) {
            ret = ERROR_KAFKA_CODEC_ARRAY;
            srs_error("kafka decode array failed. ret=%d", ret);
            return ret;
        }
        length = buf->read_4bytes();
        
        for (int i = 0; i < length; i++) {
            if (!buf->require(sizeof(int32_t))) {
                ret = ERROR_KAFKA_CODEC_ARRAY;
                srs_error("kafka decode array elem failed. ret=%d", ret);
                return ret;
                
            }
            
            int32_t elem = buf->read_4bytes();
            elems.push_back(elem);
        }
        
        return ret;
    }
};

/**
 * the header of request, includes the size of request.
 * @see https://cwiki.apache.org/confluence/display/KAFKA/A+Guide+To+The+Kafka+Protocol#AGuideToTheKafkaProtocol-Requests
 */
class SrsKafkaRequestHeader : public ISrsCodec
{
private:
    /**
     * The MessageSize field gives the size of the subsequent request or response
     * message in bytes. The client can read requests by first reading this 4 byte
     * size as an integer N, and then reading and parsing the subsequent N bytes
     * of the request.
     */
    int32_t _size;
private:
    /**
     * This is a numeric id for the API being invoked (i.e. is it 
     * a metadata request, a produce request, a fetch request, etc).
     * @remark MetadataRequest | ProduceRequest | FetchRequest | OffsetRequest | OffsetCommitRequest | OffsetFetchRequest
     */
    int16_t _api_key;
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
    int32_t _correlation_id;
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
private:
    /**
     * the layout of request:
     *      +-----------+----------------------------------+
     *      |  4B _size |      [_size] bytes               |
     *      +-----------+------------+---------------------+
     *      |  4B _size |   header   |    message          |
     *      +-----------+------------+---------------------+
     *      |  total size = 4 + header + message           |
     *      +----------------------------------------------+
     * where the header is specifies this request header without the start 4B size.
     * @remark size = 4 + header + message.
     */
    virtual int header_size();
    /**
     * the size of message, the bytes left after the header.
     */
    virtual int message_size();
    /**
     * the total size of the request, includes the 4B size.
     */
    virtual int total_size();
public:
    /**
     * when got the whole message size, update the header.
     * @param s the whole message, including the 4 bytes size size.
     */
    virtual void set_total_size(int s);
    /**
     * get the correlation id for message.
     */
    virtual int32_t correlation_id();
    /**
     * set the correlation id for message.
     */
    virtual void set_correlation_id(int32_t cid);
    /**
     * get the api key of header for message.
     */
    virtual SrsKafkaApiKey api_key();
    /**
     * set the api key of header for message.
     */
    virtual void set_api_key(SrsKafkaApiKey key);
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
// interface ISrsCodec
public:
    virtual int size();
    virtual int encode(SrsBuffer* buf);
    virtual int decode(SrsBuffer* buf);
};

/**
 * the header of response, include the size of response.
 * The response will always match the paired request (e.g. we will
 * send a MetadataResponse in return to a MetadataRequest).
 * @see https://cwiki.apache.org/confluence/display/KAFKA/A+Guide+To+The+Kafka+Protocol#AGuideToTheKafkaProtocol-Responses
 */
class SrsKafkaResponseHeader : public ISrsCodec
{
private:
    /**
     * The MessageSize field gives the size of the subsequent request or response
     * message in bytes. The client can read requests by first reading this 4 byte
     * size as an integer N, and then reading and parsing the subsequent N bytes
     * of the request.
     */
    int32_t _size;
private:
    /**
     * This is a user-supplied integer. It will be passed back in
     * the response by the server, unmodified. It is useful for matching
     * request and response between the client and server.
     */
    int32_t _correlation_id;
public:
    SrsKafkaResponseHeader();
    virtual ~SrsKafkaResponseHeader();
private:
    /**
     * the layout of response:
     *      +-----------+----------------------------------+
     *      |  4B _size |      [_size] bytes               |
     *      +-----------+------------+---------------------+
     *      |  4B _size |  4B header |    message          |
     *      +-----------+------------+---------------------+
     *      |  total size = 4 + 4 + message                |
     *      +----------------------------------------------+
     * where the header is specifies this request header without the start 4B size.
     * @remark size = 4 + 4 + message.
     */
    virtual int header_size();
    /**
     * the size of message, the bytes left after the header.
     */
    virtual int message_size();
public:
    /**
     * the total size of the request, includes the 4B size and message body.
     */
    virtual int total_size();
public:
    /**
     * when got the whole message size, update the header.
     * @param s the whole message, including the 4 bytes size size.
     */
    virtual void set_total_size(int s);
    /**
     * get the correlation id of response message.
     */
    virtual int32_t correlation_id();
// interface ISrsCodec
public:
    virtual int size();
    virtual int encode(SrsBuffer* buf);
    virtual int decode(SrsBuffer* buf);
};

/**
 * the kafka message in message set.
 * @see https://cwiki.apache.org/confluence/display/KAFKA/A+Guide+To+The+Kafka+Protocol#AGuideToTheKafkaProtocol-Messagesets
 */
struct SrsKafkaRawMessage
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
    SrsKafkaRawMessage();
    virtual ~SrsKafkaRawMessage();
};

/**
 * a set of kafka message.
 * @see https://cwiki.apache.org/confluence/display/KAFKA/A+Guide+To+The+Kafka+Protocol#AGuideToTheKafkaProtocol-Messagesets
 */
class SrsKafkaRawMessageSet
{
private:
    std::vector<SrsKafkaRawMessage*> messages;
public:
    SrsKafkaRawMessageSet();
    virtual ~SrsKafkaRawMessageSet();
};

/**
 * the kafka request message, for protocol to send.
 */
class SrsKafkaRequest : public ISrsCodec
{
protected:
    SrsKafkaRequestHeader header;
public:
    SrsKafkaRequest();
    virtual ~SrsKafkaRequest();
public:
    /**
     * update the size in header.
     * @param s an int value specifies the size of message in header.
     */
    virtual void update_header(int s);
    /**
     * get the correlation id of header for message. 
     */
    virtual int32_t correlation_id();
    /**
     * get the api key of request.
     */
    virtual SrsKafkaApiKey api_key();
// interface ISrsCodec
public:
    virtual int size();
    virtual int encode(SrsBuffer* buf);
    virtual int decode(SrsBuffer* buf);
};

/**
 * the kafka response message, for protocol to recv.
 */
class SrsKafkaResponse : public ISrsCodec
{
protected:
    SrsKafkaResponseHeader header;
public:
    SrsKafkaResponse();
    virtual ~SrsKafkaResponse();
public:
    /**
     * update the size in header.
     * @param s an int value specifies the size of message in header.
     */
    virtual void update_header(int s);
// interface ISrsCodec
public:
    virtual int size();
    virtual int encode(SrsBuffer* buf);
    virtual int decode(SrsBuffer* buf);
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
class SrsKafkaTopicMetadataRequest : public SrsKafkaRequest
{
private:
    SrsKafkaArray<SrsKafkaString> topics;
public:
    SrsKafkaTopicMetadataRequest();
    virtual ~SrsKafkaTopicMetadataRequest();
public:
    virtual void add_topic(std::string topic);
// interface ISrsCodec
public:
    virtual int size();
    virtual int encode(SrsBuffer* buf);
    virtual int decode(SrsBuffer* buf);
};

/**
 * the metadata response data.
 * @see https://cwiki.apache.org/confluence/display/KAFKA/A+Guide+To+The+Kafka+Protocol#AGuideToTheKafkaProtocol-MetadataResponse
 */
struct SrsKafkaBroker : public ISrsCodec
{
public:
    int32_t node_id;
    SrsKafkaString host;
    int32_t port;
public:
    SrsKafkaBroker();
    virtual ~SrsKafkaBroker();
// interface ISrsCodec
public:
    virtual int size();
    virtual int encode(SrsBuffer* buf);
    virtual int decode(SrsBuffer* buf);
};
struct SrsKafkaPartitionMetadata : public ISrsCodec
{
public:
    int16_t error_code;
    int32_t partition_id;
    int32_t leader;
    SrsKafkaArray<int32_t> replicas;
    SrsKafkaArray<int32_t> isr;
public:
    SrsKafkaPartitionMetadata();
    virtual ~SrsKafkaPartitionMetadata();
// interface ISrsCodec
public:
    virtual int size();
    virtual int encode(SrsBuffer* buf);
    virtual int decode(SrsBuffer* buf);
};
struct SrsKafkaTopicMetadata : public ISrsCodec
{
public:
    int16_t error_code;
    SrsKafkaString name;
    SrsKafkaArray<SrsKafkaPartitionMetadata> metadatas;
public:
    SrsKafkaTopicMetadata();
    virtual ~SrsKafkaTopicMetadata();
// interface ISrsCodec
public:
    virtual int size();
    virtual int encode(SrsBuffer* buf);
    virtual int decode(SrsBuffer* buf);
};

/**
 * response for the metadata request from broker.
 * The response contains metadata for each partition, 
 * with partitions grouped together by topic. This 
 * metadata refers to brokers by their broker id. 
 * The brokers each have a host and port.
 * @see https://cwiki.apache.org/confluence/display/KAFKA/A+Guide+To+The+Kafka+Protocol#AGuideToTheKafkaProtocol-MetadataResponse
 */
class SrsKafkaTopicMetadataResponse : public SrsKafkaResponse
{
private:
    SrsKafkaArray<SrsKafkaBroker> brokers;
    SrsKafkaArray<SrsKafkaTopicMetadata> metadatas;
public:
    SrsKafkaTopicMetadataResponse();
    virtual ~SrsKafkaTopicMetadataResponse();
// interface ISrsCodec
public:
    virtual int size();
    virtual int encode(SrsBuffer* buf);
    virtual int decode(SrsBuffer* buf);
};

/**
 * the poll to discovery reponse.
 * @param CorrelationId This is a user-supplied integer. It will be passed back 
 *          in the response by the server, unmodified. It is useful for matching 
 *          request and response between the client and server.
 * @see https://cwiki.apache.org/confluence/display/KAFKA/A+Guide+To+The+Kafka+Protocol#AGuideToTheKafkaProtocol-Requests
 */
class SrsKafkaCorrelationPool
{
private:
    static SrsKafkaCorrelationPool* _instance;
public:
    static SrsKafkaCorrelationPool* instance();
private:
    std::map<int32_t, SrsKafkaApiKey> correlation_ids;
private:
    SrsKafkaCorrelationPool();
public:
    virtual ~SrsKafkaCorrelationPool();
public:
    /**
     * generate a global correlation id.
     */
    virtual int32_t generate_correlation_id();
    /**
     * set the correlation id to specified request key.
     */
    virtual SrsKafkaApiKey set(int32_t correlation_id, SrsKafkaApiKey request);
    /**
     * unset the correlation id.
     * @return the previous api key; unknown if not set.
     */
    virtual SrsKafkaApiKey unset(int32_t correlation_id);
    /**
     * get the key by specified correlation id.
     * @return the specified api key; unknown if no correlation id.
     */
    virtual SrsKafkaApiKey get(int32_t correlation_id);
};

/**
 * the kafka protocol stack, use to send and recv kakfa messages.
 */
class SrsKafkaProtocol
{
private:
    ISrsProtocolReaderWriter* skt;
    SrsFastStream* reader;
public:
    SrsKafkaProtocol(ISrsProtocolReaderWriter* io);
    virtual ~SrsKafkaProtocol();
public:
    /**
     * write the message to kafka server.
     * @param msg the msg to send. user must not free it again.
     */
    virtual int send_and_free_message(SrsKafkaRequest* msg);
    /**
     * read the message from kafka server.
     * @param pmsg output the received message. user must free it.
     */
    virtual int recv_message(SrsKafkaResponse** pmsg);
};

/**
 * the kafka client, for producer or consumer.
 */
class SrsKafkaClient
{
private:
    SrsKafkaProtocol* protocol;
public:
    SrsKafkaClient(ISrsProtocolReaderWriter* io);
    virtual ~SrsKafkaClient();
public:
    /**
     * fetch the metadata from broker for topic.
     */
    virtual int fetch_metadata(std::string topic);
};

#endif

#endif

