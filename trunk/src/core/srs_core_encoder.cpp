/*
The MIT License (MIT)

Copyright (c) 2013 winlin

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

#include <srs_core_encoder.hpp>

#include <srs_core_error.hpp>
#include <srs_core_log.hpp>
#include <srs_core_config.hpp>

#define SRS_ENCODER_SLEEP_MS 2000

SrsEncoder::SrsEncoder()
{
	tid = NULL;
	loop = false;
}

SrsEncoder::~SrsEncoder()
{
	on_unpublish();
}

int SrsEncoder::on_publish(std::string _vhost, std::string _app, std::string _stream)
{
	int ret = ERROR_SUCCESS;

	vhost = _vhost;
	app = _app;
	stream = _stream;
    
    srs_assert(!tid);
    if((tid = st_thread_create(encoder_thread, this, 1, 0)) == NULL){
		ret = ERROR_ST_CREATE_FORWARD_THREAD;
        srs_error("st_thread_create failed. ret=%d", ret);
        return ret;
    }
    
	return ret;
}

void SrsEncoder::on_unpublish()
{
	if (tid) {
		loop = false;
		st_thread_interrupt(tid);
		st_thread_join(tid, NULL);
		tid = NULL;
	}
}

int SrsEncoder::cycle()
{
	int ret = ERROR_SUCCESS;
	return ret;
}

void SrsEncoder::encoder_cycle()
{
	int ret = ERROR_SUCCESS;
	
	log_context->generate_id();
	srs_trace("encoder cycle start");
	
	while (loop) {
		if ((ret = cycle()) != ERROR_SUCCESS) {
			srs_warn("encoder cycle failed, ignored and retry, ret=%d", ret);
		} else {
			srs_info("encoder cycle success, retry");
		}
		
		if (!loop) {
			break;
		}
		
		st_usleep(SRS_ENCODER_SLEEP_MS * 1000);
	}
	
	// TODO: kill ffmpeg when finished and it alive
	
	srs_trace("encoder cycle finished");
}

void* SrsEncoder::encoder_thread(void* arg)
{
	SrsEncoder* obj = (SrsEncoder*)arg;
	srs_assert(obj != NULL);
	
	obj->loop = true;
	obj->encoder_cycle();
	
	return NULL;
}

