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
gcc srs_ingest_rtmp.c ../../objs/lib/srs_librtmp.a -g -O0 -lstdc++ -o srs_ingest_rtmp
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../../objs/include/srs_librtmp.h"

int main(int argc, char** argv)
{
    if (argc <= 2) {
        printf("ingest RTMP and publish to RTMP server\n"
            "Usage: %s <in_rtmp_url> <out_rtmp_url>\n"
            "   in_rtmp_url     input rtmp url, ingest from this url.\n"
            "   out_rtmp_url    output rtmp url, publish to this url.\n",
            argv[0]);
        exit(1);
    }
    
    printf("ingest RTMP and publish to RTMP server like edge.\n");
    printf("srs(simple-rtmp-server) client librtmp library.\n");
    printf("version: %d.%d.%d\n", srs_version_major(), srs_version_minor(), srs_version_revision());
    
    return 0;
}
