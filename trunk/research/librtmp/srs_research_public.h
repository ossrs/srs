/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

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

#ifndef SRS_RESEARH_PUBLIC_HPP
#define SRS_RESEARH_PUBLIC_HPP

/*
#include "srs_research_public.h"
*/

#include <string.h>
#include <time.h>

char* format_time()
{
    struct timeval tv;
    static char buf[23];
    
    memset(buf, 0, sizeof(buf));
    
    // clock time
    if (gettimeofday(&tv, NULL) == -1) {
        return buf;
    }
    
    // to calendar time
    struct tm* tm;
    if ((tm = localtime(&tv.tv_sec)) == NULL) {
        return buf;
    }
    
    snprintf(buf, sizeof(buf), 
        "%d-%02d-%02d %02d:%02d:%02d.%03d", 
        1900 + tm->tm_year, 1 + tm->tm_mon, tm->tm_mday, 
        tm->tm_hour, tm->tm_min, tm->tm_sec, 
        (int)(tv.tv_usec / 1000));
    
    return buf;
}
#define trace(msg, ...) printf("[%s]", format_time());printf(msg, ##__VA_ARGS__);printf("\n")
#define verbose(msg, ...) printf("[%s]", format_time());printf(msg, ##__VA_ARGS__);printf("\n")
#if 1
#undef verbose
#define verbose(msg, ...) (void)0
#endif

#endif
