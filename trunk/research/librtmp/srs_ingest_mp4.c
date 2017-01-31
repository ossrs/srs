/*
 The MIT License (MIT)
 
 Copyright (c) 2013-2017 SRS(ossrs)
 
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

#include <stdio.h>
#include <stdlib.h>

#include "../../objs/include/srs_librtmp.h"

int main(int argc, char** argv)
{
    int ret, opt;
    
    printf("Ingest mp4 file and publish to RTMP server like FFMPEG.\n");
    printf("SRS(OSSRS) client librtmp library.\n");
    printf("Version: %d.%d.%d\n", srs_version_major(), srs_version_minor(), srs_version_revision());
    
    if (argc <= 2) {
        printf("Ingest mp4 file and publish to RTMP server\n"
               "Usage: %s <-i in_mp4_file> <-y out_rtmp_url>\n"
               "   in_mp4_file     input mp4 file, ingest from this file.\n"
               "   out_rtmp_url    output rtmp url, publish to this url.\n"
               "@remark Only support non-seek mp4, see https://github.com/ossrs/srs/issues/738#issuecomment-276343669\n"
               "For example:\n"
               "   %s -i avatar.mp4 -y rtmp://127.0.0.1/live/livestream\n",
               argv[0], argv[0]);
        exit(-1);
    }
    
    for (opt = 0; opt < argc; opt++) {
        srs_human_trace("argv[%d]=%s", opt, argv[opt]);
    }
    
    return ret;
}

