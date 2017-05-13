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
#include <srs_kernel_mp4.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_core_autofree.hpp>

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sstream>
using namespace std;

// @global log and context.
ISrsLog* _srs_log = new SrsConsoleLog(SrsLogLevelTrace, false);
ISrsThreadContext* _srs_context = new SrsThreadContext();

int parse(std::string mp4_file)
{
    int ret = ERROR_SUCCESS;
    
    SrsFileReader fr;
    if ((ret = fr.open(mp4_file)) != ERROR_SUCCESS) {
        srs_error("Open MP4 file failed, ret=%d", ret);
        return ret;
    }
    srs_trace("MP4 file open success");
    
    SrsMp4BoxReader br;
    if ((ret = br.initialize(&fr)) != ERROR_SUCCESS) {
        srs_error("Open MP4 box reader failed, ret=%d", ret);
        return ret;
    }
    srs_trace("MP4 box reader open success");
    
    SrsSimpleStream stream;
    while (true) {
        SrsMp4Box* box = NULL;
        SrsAutoFree(SrsMp4Box, box);
        
        if ((ret = br.read(&stream, &box)) != ERROR_SUCCESS) {
            if (ret != ERROR_SYSTEM_FILE_EOF) {
                srs_error("Read MP4 box failed, ret=%d", ret);
            }
            return ret;
        }
        
        if ((ret = br.skip(box, &stream)) != ERROR_SUCCESS) {
            srs_error("Skip MP4 box failed, ret=%d", ret);
            return ret;
        }
        
        stringstream ss;
        ss << "type=" << char(box->type>>24) << char(box->type>>16) << char(box->type>>8) << char(box->type)
            << ", size=" << box->sz();
        srs_trace("MP4 box %s", ss.str().c_str());
    }
    
    return ret;
}

int main(int argc, char** argv)
{
    int ret = ERROR_SUCCESS;
    
    printf("SRS MP4 parser/%d.%d.%d, parse and show the mp4 boxes structure.\n",
           VERSION_MAJOR, VERSION_MINOR, VERSION_REVISION);
    
    if (argc < 2) {
        printf("Usage: %s <mp4_file>\n"
               "        mp4_file The MP4 file path to parse.\n"
               "For example:\n"
               "        %s doc/source.200kbps.768x320.mp4\n",
               argv[0], argv[0]);
        
        exit(-1);
    }
    string mp4_file = argv[1];
    srs_trace("Parse MP4 file %s", mp4_file.c_str());
    
    ret = parse(mp4_file);
    
    if (ret == ERROR_SYSTEM_FILE_EOF) {
        srs_trace("Parse complete");
        return 0;
    }
    
    return ret;
}

