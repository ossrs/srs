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

#ifndef SRS_APP_KAFKA_HPP
#define SRS_APP_KAFKA_HPP

/*
#include <srs_app_kafka.hpp>
*/
#include <srs_core.hpp>

class SrsLbRoundRobin;
class SrsAsyncCallWorker;
class SrsTcpClient;
class SrsKafkaClient;

#ifdef SRS_AUTO_KAFKA

/**
 * the kafka producer used to save log to kafka cluster.
 */
class SrsKafkaProducer
{
private:
    SrsLbRoundRobin* lb;
    SrsAsyncCallWorker* worker;
    SrsTcpClient* transport;
    SrsKafkaClient* kafka;
public:
    SrsKafkaProducer();
    virtual ~SrsKafkaProducer();
public:
    virtual int initialize();
    virtual int start();
    virtual void stop();
private:
    virtual int request_metadata();
};

#endif

#endif

