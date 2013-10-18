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

#include <st.h>

#include <srs_core_log.hpp>
#include <srs_core_error.hpp>

#include <srs_core_server.hpp>

SrsServer::SrsServer()
{
}

SrsServer::~SrsServer()
{
}

int SrsServer::initialize()
{
	int ret = ERROR_SUCCESS;
    
    // use linux epoll.
    if (st_set_eventsys(ST_EVENTSYS_ALT) == -1) {
        ret = ERROR_ST_SET_EPOLL;
        SrsError("st_set_eventsys use linux epoll failed. ret=%d", ret);
        return ret;
    }
    SrsInfo("st_set_eventsys use linux epoll success");
    
    if(st_init() != 0){
        ret = ERROR_ST_INITIALIZE;
        SrsError("st_init failed. ret=%d", ret);
        return ret;
    }
    SrsTrace("st_init success");
	
	// set current log id.
	log_context->SetId();
	SrsInfo("log set id success");
	
	return ret;
}
