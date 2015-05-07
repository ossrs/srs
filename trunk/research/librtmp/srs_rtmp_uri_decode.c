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
/**
gcc srs_rtmp_dump.c ../../objs/lib/srs_librtmp.a -g -O0 -lstdc++ -o srs_rtmp_dump
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <assert.h>

#include "../../src/protocol/srs_rtmp_utility.hpp"


int main(int argc, char** argv)
{
    
    if ( argc > 1) {
        printf("%s\n",srs_UriDecode(argv[1]).c_str());
    } else {
        printf("Usage: %s after_encode_uri \n"
            "For example:\n"
            "   %s %s \n"
            ,argv[0],argv[0]
            ,"rtmp%3A%2F%2F127%2E0%2E0%2E1%3A1935%2Flive%2Flivestream%3Ftime%3DXXXXXX%26token%3DXXXXX"
            );
        exit(-1);
    }
    
    return 0;
}
