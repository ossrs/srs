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

#include <srs_app_kafka.hpp>

#include <vector>
using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_app_async_call.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_kernel_balance.hpp>
#include <srs_kafka_stack.hpp>
#include <srs_core_autofree.hpp>
#include <srs_protocol_json.hpp>

#ifdef SRS_AUTO_KAFKA

#define SRS_KAKFA_CYCLE_INTERVAL_MS 3000
#define SRS_KAFKA_PRODUCER_AGGREGATE_SIZE 10

std::string srs_kafka_metadata_summary(SrsKafkaTopicMetadataResponse* metadata)
{
    vector<string> bs;
    for (int i = 0; i < metadata->brokers.size(); i++) {
        SrsKafkaBroker* broker = metadata->brokers.at(i);
        
        string hostport = srs_int2str(broker->node_id) + "/" + broker->host.to_str();
        if (broker->port > 0) {
            hostport += ":" + srs_int2str(broker->port);
        }
        
        bs.push_back(hostport);
    }
    
    vector<string> ps;
    for (int i = 0; i < metadata->metadatas.size(); i++) {
        SrsKafkaTopicMetadata* topic = metadata->metadatas.at(i);
        
        for (int j = 0; j < topic->metadatas.size(); j++) {
            string desc = "topic=" + topic->name.to_str();
            
            SrsKafkaPartitionMetadata* partition = topic->metadatas.at(j);
            
            desc += "?partition=" + srs_int2str(partition->partition_id);
            desc += "&leader=" + srs_int2str(partition->leader);
            
            vector<string> replicas = srs_kafka_array2vector(&partition->replicas);
            desc += "&replicas=" + srs_join_vector_string(replicas, ",");
            
            ps.push_back(desc);
        }
    }
    
    std::stringstream ss;
    ss << "brokers=" << srs_join_vector_string(bs, ",");
    ss << ", " << srs_join_vector_string(ps, ", ");
    
    return ss.str();
}

std::string srs_kafka_summary_partitions(const vector<SrsKafkaPartition*>& partitions)
{
    vector<string> ret;
    
    vector<SrsKafkaPartition*>::const_iterator it;
    for (it = partitions.begin(); it != partitions.end(); ++it) {
        SrsKafkaPartition* partition = *it;
        
        string desc = "tcp://";
        desc += partition->host + ":" + srs_int2str(partition->port);
        desc += "?broker=" + srs_int2str(partition->broker);
        desc += "&partition=" + srs_int2str(partition->id);
        ret.push_back(desc);
    }
    
    return srs_join_vector_string(ret, ", ");
}

void srs_kafka_metadata2connector(SrsKafkaTopicMetadataResponse* metadata, vector<SrsKafkaPartition*>& partitions)
{
    for (int i = 0; i < metadata->metadatas.size(); i++) {
        SrsKafkaTopicMetadata* topic = metadata->metadatas.at(i);
        
        for (int j = 0; j < topic->metadatas.size(); j++) {
            SrsKafkaPartitionMetadata* partition = topic->metadatas.at(j);
            
            SrsKafkaPartition* p = new SrsKafkaPartition();
            p->id = partition->partition_id;
            p->broker = partition->leader;
            
            for (int i = 0; i < metadata->brokers.size(); i++) {
                SrsKafkaBroker* broker = metadata->brokers.at(i);
                if (broker->node_id == p->broker) {
                    p->host = broker->host.to_str();
                    p->port = broker->port;
                    break;
                }
            }
            
            partitions.push_back(p);
        }
    }
}

SrsKafkaPartition::SrsKafkaPartition()
{
    id = broker = 0;
    port = SRS_CONSTS_KAFKA_DEFAULT_PORT;
}

SrsKafkaPartition::~SrsKafkaPartition()
{
}

string SrsKafkaPartition::hostport()
{
    if (ep.empty()) {
        ep = host + ":" + srs_int2str(port);
    }
    
    return ep;
}

SrsKafkaMessageOnClient::SrsKafkaMessageOnClient(SrsKafkaProducer* p, SrsListenerType t, string i)
{
    producer = p;
    type = t;
    ip = i;
}

SrsKafkaMessageOnClient::~SrsKafkaMessageOnClient()
{
}

int SrsKafkaMessageOnClient::call()
{
    SrsJsonObject* obj = SrsJsonAny::object();
    
    obj->set("msg", SrsJsonAny::str("accept"));
    obj->set("type", SrsJsonAny::integer(type));
    obj->set("ip", SrsJsonAny::str(ip.c_str()));
    
    return producer->send(obj);
}

string SrsKafkaMessageOnClient::to_string()
{
    return ip;
}

SrsKafkaProducer::SrsKafkaProducer()
{
    metadata_ok = false;
    metadata_expired = st_cond_new();
    
    lock = st_mutex_new();
    pthread = new SrsReusableThread("kafka", this, SRS_KAKFA_CYCLE_INTERVAL_MS * 1000);
    worker = new SrsAsyncCallWorker();
    
    lb = new SrsLbRoundRobin();
    transport = new SrsTcpClient();
    kafka = new SrsKafkaClient(transport);
}

SrsKafkaProducer::~SrsKafkaProducer()
{
    vector<SrsKafkaPartition*>::iterator it;
    for (it = partitions.begin(); it != partitions.end(); ++it) {
        SrsKafkaPartition* partition = *it;
        srs_freep(partition);
    }
    partitions.clear();
    
    srs_freep(lb);
    srs_freep(kafka);
    srs_freep(transport);
    
    srs_freep(worker);
    srs_freep(pthread);
    
    st_mutex_destroy(lock);
    st_cond_destroy(metadata_expired);
}

int SrsKafkaProducer::initialize()
{
    int ret = ERROR_SUCCESS;
    
    srs_info("initialize kafka producer ok.");
    
    return ret;
}

int SrsKafkaProducer::start()
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = worker->start()) != ERROR_SUCCESS) {
        srs_error("start kafka worker failed. ret=%d", ret);
        return ret;
    }
    
    if ((ret = pthread->start()) != ERROR_SUCCESS) {
        srs_error("start kafka thread failed. ret=%d", ret);
    }
    
    refresh_metadata();
    
    return ret;
}

void SrsKafkaProducer::stop()
{
    pthread->stop();
    worker->stop();
}

int SrsKafkaProducer::on_client(SrsListenerType type, st_netfd_t stfd)
{
    return worker->execute(new SrsKafkaMessageOnClient(this, type, srs_get_peer_ip(st_netfd_fileno(stfd))));
}

int SrsKafkaProducer::send(SrsJsonObject* obj)
{
    int ret = ERROR_SUCCESS;
    
    // cache the json object.
    objects.push_back(obj);
    
    // too few messages, ignore.
    if (objects.size() < SRS_KAFKA_PRODUCER_AGGREGATE_SIZE) {
        return ret;
    }
    
    // too many messages, warn user.
    if (objects.size() > SRS_KAFKA_PRODUCER_AGGREGATE_SIZE * 10) {
        srs_warn("kafka cache too many messages: %d", objects.size());
    }
    
    // sync with backgound metadata worker.
    st_mutex_lock(lock);
    
    // flush message when metadata is ok.
    if (metadata_ok) {
        ret = flush();
    }
    
    st_mutex_unlock(lock);
    
    return ret;
}

int SrsKafkaProducer::cycle()
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = do_cycle()) != ERROR_SUCCESS) {
        srs_warn("ignore kafka error. ret=%d", ret);
    }
    
    return ret;
}

int SrsKafkaProducer::on_before_cycle()
{
    // wait for the metadata expired.
    // when metadata is ok, wait for it expired.
    if (metadata_ok) {
        st_cond_wait(metadata_expired);
    }
    
    // request to lock to acquire the socket.
    st_mutex_lock(lock);
    
    return ERROR_SUCCESS;
}

int SrsKafkaProducer::on_end_cycle()
{
    st_mutex_unlock(lock);
 
    return ERROR_SUCCESS;
}

int SrsKafkaProducer::do_cycle()
{
    int ret = ERROR_SUCCESS;
    
    // ignore when disabled.
    bool enabled = _srs_config->get_kafka_enabled();
    if (!enabled) {
        return ret;
    }
    
    // when kafka enabled, request metadata when startup.
    if ((ret = request_metadata()) != ERROR_SUCCESS) {
        srs_error("request kafka metadata failed. ret=%d", ret);
        return ret;
    }
    
    return ret;
}

int SrsKafkaProducer::request_metadata()
{
    int ret = ERROR_SUCCESS;
    
    // ignore when disabled.
    bool enabled = _srs_config->get_kafka_enabled();
    if (!enabled) {
        return ret;
    }
    
    // select one broker to connect to.
    SrsConfDirective* brokers = _srs_config->get_kafka_brokers();
    if (!brokers) {
        srs_warn("ignore for empty brokers.");
        return ret;
    }
    
    std::string server;
    int port = SRS_CONSTS_KAFKA_DEFAULT_PORT;
    if (true) {
        srs_assert(!brokers->args.empty());
        std::string broker = lb->select(brokers->args);
        srs_parse_endpoint(broker, server, port);
    }
    
    std::string topic = _srs_config->get_kafka_topic();
    if (true) {
        std::string senabled = srs_bool2switch(enabled);
        std::string sbrokers = srs_join_vector_string(brokers->args, ",");
        srs_trace("kafka request enabled:%s, brokers:%s, current:[%d]%s:%d, topic:%s",
                  senabled.c_str(), sbrokers.c_str(), lb->current(), server.c_str(), port, topic.c_str());
    }
    
    // connect to kafka server.
    if ((ret = transport->connect(server, port, SRS_CONSTS_KAFKA_TIMEOUT_US)) != ERROR_SUCCESS) {
        srs_error("kafka connect %s:%d failed. ret=%d", server.c_str(), port, ret);
        return ret;
    }
    
    // do fetch medata from broker.
    SrsKafkaTopicMetadataResponse* metadata = NULL;
    if ((ret = kafka->fetch_metadata(topic, &metadata)) != ERROR_SUCCESS) {
        srs_error("kafka fetch metadata failed. ret=%d", ret);
        return ret;
    }
    SrsAutoFree(SrsKafkaTopicMetadataResponse, metadata);
    
    // show kafka metadata.
    string summary = srs_kafka_metadata_summary(metadata);
    srs_trace("kafka metadata: %s", summary.c_str());
    
    // generate the partition info.
    srs_kafka_metadata2connector(metadata, partitions);
    srs_trace("kafka connector: %s", srs_kafka_summary_partitions(partitions).c_str());
    
    metadata_ok = true;
    
    return ret;
}

void SrsKafkaProducer::refresh_metadata()
{
    metadata_ok = false;
    st_cond_signal(metadata_expired);
    srs_trace("kafka async refresh metadata in background");
}

int SrsKafkaProducer::flush()
{
    int ret = ERROR_SUCCESS;
    // TODO: FIXME: implements it.
    return ret;
}

#endif

