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
#include <srs_kernel_balance.hpp>
#include <srs_kafka_stack.hpp>

#ifdef SRS_AUTO_KAFKA

#define SRS_KAKFA_CYCLE_INTERVAL_MS 3000

SrsKafkaProducer::SrsKafkaProducer()
{
    meatadata_ok = false;
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
    
    meatadata_ok = false;
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
    
    meatadata_ok = false;
    st_cond_signal(metadata_expired);
    srs_trace("kafka work in background");
    
    return ret;
}

void SrsKafkaProducer::stop()
{
    pthread->stop();
    worker->stop();
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
    if (meatadata_ok) {
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
    
    // connect to kafka server.
    if ((ret = transport->connect(server, port, SRS_CONSTS_KAFKA_TIMEOUT_US)) != ERROR_SUCCESS) {
        srs_error("kafka connect %s:%d failed. ret=%d", server.c_str(), port, ret);
        return ret;
    }
    
    // do fetch medata from broker.
    std::string topic = _srs_config->get_kafka_topic();
    if ((ret = kafka->fetch_metadata(topic)) != ERROR_SUCCESS) {
        srs_error("kafka fetch metadata failed. ret=%d", ret);
        return ret;
    }
    
    // log when completed.
    if (true) {
        std::string senabled = srs_bool2switch(enabled);
        std::string sbrokers = srs_join_vector_string(brokers->args, ",");
        srs_trace("kafka ok, enabled:%s, brokers:%s, current:[%d]%s:%d, topic:%s",
                  senabled.c_str(), sbrokers.c_str(), lb->current(), server.c_str(), port, topic.c_str());
    }
    
    meatadata_ok = true;
    
    return ret;
}

#endif

