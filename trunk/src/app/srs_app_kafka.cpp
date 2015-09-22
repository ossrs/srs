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

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>
#include <srs_app_async_call.hpp>

#ifdef SRS_AUTO_KAFKA

SrsKafkaProducer::SrsKafkaProducer()
{
    worker = new SrsAsyncCallWorker();
}

SrsKafkaProducer::~SrsKafkaProducer()
{
    srs_freep(worker);
}

int SrsKafkaProducer::initialize()
{
    int ret = ERROR_SUCCESS;
    
    srs_trace("initialize kafka producer ok.");
    
    return ret;
}

int SrsKafkaProducer::start()
{
    int ret = ERROR_SUCCESS;
    
    if ((ret = worker->start()) != ERROR_SUCCESS) {
        srs_error("start kafka failed. ret=%d", ret);
        return ret;
    }
    
    srs_trace("start kafka async worker ok.");
    
    return ret;
}

void SrsKafkaProducer::stop()
{
    worker->stop();
}

#endif

