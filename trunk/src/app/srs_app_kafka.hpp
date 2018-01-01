/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
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

#ifndef SRS_APP_KAFKA_HPP
#define SRS_APP_KAFKA_HPP

#include <srs_core.hpp>

#include <map>
#include <vector>

class SrsLbRoundRobin;
class SrsAsyncCallWorker;
class SrsTcpClient;
class SrsKafkaClient;
class SrsJsonObject;
class SrsKafkaProducer;

#include <srs_app_thread.hpp>
#include <srs_app_server.hpp>
#include <srs_app_async_call.hpp>

#ifdef SRS_AUTO_KAFKA

/**
 * the partition messages cache.
 */
typedef std::vector<SrsJsonObject*> SrsKafkaPartitionCache;

/**
 * the kafka partition info.
 */
struct SrsKafkaPartition
{
private:
    std::string ep;
    // Not NULL when connected.
    SrsTcpClient* transport;
    SrsKafkaClient* kafka;
public:
    int id;
    std::string topic;
    // leader.
    int broker;
    std::string host;
    int port;
public:
    SrsKafkaPartition();
    virtual ~SrsKafkaPartition();
public:
    virtual std::string hostport();
    virtual srs_error_t connect();
    virtual srs_error_t flush(SrsKafkaPartitionCache* pc);
private:
    virtual void disconnect();
};

/**
 * the following is all types of kafka messages.
 */
class SrsKafkaMessage : public ISrsAsyncCallTask
{
private:
    SrsKafkaProducer* producer;
    int key;
    SrsJsonObject* obj;
public:
    SrsKafkaMessage(SrsKafkaProducer* p, int k, SrsJsonObject* j);
    virtual ~SrsKafkaMessage();
// interface ISrsAsyncCallTask
public:
    virtual srs_error_t call();
    virtual std::string to_string();
};

/**
 * a message cache for kafka.
 */
class SrsKafkaCache
{
public:
    // the total partitions,
    // for the key to map to the parition by key%nb_partitions.
    int nb_partitions;
private:
    // total messages for all partitions.
    int count;
    // key is the partition id, value is the message set to write to this partition.
    // @remark, when refresh metadata, the partition will increase,
    //      so maybe some message will dispatch to new partition.
    std::map< int32_t, SrsKafkaPartitionCache*> cache;
public:
    SrsKafkaCache();
    virtual ~SrsKafkaCache();
public:
    virtual void append(int key, SrsJsonObject* obj);
    virtual int size();
    /**
     * fetch out a available partition cache.
     * @return true when got a key and pc; otherwise, false.
     */
    virtual bool fetch(int* pkey, SrsKafkaPartitionCache** ppc);
    /**
     * flush the specified partition cache.
     */
    virtual srs_error_t flush(SrsKafkaPartition* partition, int key, SrsKafkaPartitionCache* pc);
};

/**
 * the kafka cluster interface.
 */
class ISrsKafkaCluster
{
public:
    ISrsKafkaCluster();
    virtual ~ISrsKafkaCluster();
public:
    /**
     * when got any client connect to SRS, notify kafka.
     * @param key the partition map key, the client id or hash(ip).
     * @param type the type of client.
     * @param ip the peer ip of client.
     */
    virtual srs_error_t on_client(int key, SrsListenerType type, std::string ip) = 0;
    /**
     * when client close or disconnect for error.
     * @param key the partition map key, the client id or hash(ip).
     */
    virtual srs_error_t on_close(int key) = 0;
};

// @global kafka event producer.
extern ISrsKafkaCluster* _srs_kafka;
// kafka initialize and disposer for global object.
extern srs_error_t srs_initialize_kafka();
extern void srs_dispose_kafka();

/**
 * the kafka producer used to save log to kafka cluster.
 */
class SrsKafkaProducer : virtual public ISrsCoroutineHandler, virtual public ISrsKafkaCluster
{
private:
    // TODO: FIXME: support reload.
    bool enabled;
    srs_mutex_t lock;
    SrsCoroutine* trd;
private:
    bool metadata_ok;
    srs_cond_t metadata_expired;
public:
    std::vector<SrsKafkaPartition*> partitions;
    SrsKafkaCache* cache;
private:
    SrsLbRoundRobin* lb;
    SrsAsyncCallWorker* worker;
public:
    SrsKafkaProducer();
    virtual ~SrsKafkaProducer();
public:
    virtual srs_error_t initialize();
    virtual srs_error_t start();
    virtual void stop();
// internal: for worker to call task to send object.
public:
    /**
     * send json object to kafka cluster.
     * the producer will aggregate message and send in kafka message set.
     * @param key the key to map to the partition, user can use cid or hash.
     * @param obj the json object; user must never free it again.
     */
    virtual srs_error_t send(int key, SrsJsonObject* obj);
// interface ISrsKafkaCluster
public:
    virtual srs_error_t on_client(int key, SrsListenerType type, std::string ip);
    virtual srs_error_t on_close(int key);
// interface ISrsReusableThreadHandler
public:
    virtual srs_error_t cycle();
private:
    virtual void clear_metadata();
    virtual srs_error_t do_cycle();
    virtual srs_error_t request_metadata();
    // set the metadata to invalid and refresh it.
    virtual void refresh_metadata();
    virtual srs_error_t flush();
};

#endif

#endif

