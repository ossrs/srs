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

#include <srs_core.hpp>

#include <sys/time.h>

static int64_t _srs_system_time_us_cache = 0;

int64_t srs_get_system_time_ms()
{
	return _srs_system_time_us_cache / 1000;
}

void srs_update_system_time_ms()
{
    timeval now;
    
    gettimeofday(&now, NULL);

    // we must convert the tv_sec/tv_usec to int64_t.
    _srs_system_time_us_cache = now.tv_sec * 1000 * 1000 + now.tv_usec;
    
    _srs_system_time_us_cache = srs_max(0, _srs_system_time_us_cache);
}
