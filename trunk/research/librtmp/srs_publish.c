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
/**
gcc srs_publish.c ../../objs/lib/srs_librtmp.a -g -O0 -lstdc++ -o srs_publish
*/

#include <stdio.h>
#include "../../objs/include/srs_librtmp.h"

int main(int argc, char** argv)
{
	srs_rtmp_t rtmp;
	
    printf("publish rtmp stream to server like FMLE/FFMPEG/Encoder\n");
    printf("srs(simple-rtmp-server) client librtmp library.\n");
    printf("version: %d.%d.%d\n", srs_version_major(), srs_version_minor(), srs_version_revision());
    
    rtmp = srs_rtmp_create("rtmp://127.0.0.1/live/livestream");
    
	if (srs_simple_handshake(rtmp) != 0) {
		printf("simple handshake failed.\n");
		return -1;
	}
	printf("simple handshake success\n");
    
	if (srs_connect_app(rtmp) != 0) {
		printf("connect vhost/app failed.\n");
		return -1;
	}
	printf("connect vhost/app success\n");
    
	if (srs_publish_stream(rtmp) != 0) {
		printf("publish stream failed.\n");
		return -1;
	}
	printf("publish stream success\n");
	
    srs_rtmp_destroy(rtmp);
    
    return 0;
}
