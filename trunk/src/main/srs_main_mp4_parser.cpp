/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2017 OSSRS(winlin)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <srs_core.hpp>

#include <srs_kernel_error.hpp>
#include <srs_service_log.hpp>

#include <stdio.h>
#include <stdlib.h>
#include <string>
using namespace std;

// @global log and context.
ISrsLog* _srs_log = new SrsConsoleLog(SrsLogLevelTrace, false);
ISrsThreadContext* _srs_context = new SrsThreadContext();

void help(char** argv)
{
    printf("Usage: %s <mp4_file>\n"
           "        mp4_file The MP4 file path to parse.\n"
           "For example:\n"
           "        %s doc/source.200kbps.768x320.mp4\n",
           argv[0], argv[0]);
    
    exit(0);
}

int main(int argc, char** argv)
{
    printf("SRS MP4 parser/%d.%d.%d, show the mp4 boxes structure.\n",
           VERSION_MAJOR, VERSION_MINOR, VERSION_REVISION);
    
    if (argc < 2) {
        help(argv);
    }
    string mp4_file = argv[1];
    srs_trace("Parse MP4 file %s", mp4_file.c_str());
    
    return 0;
}

