//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#include <srs_core.hpp>

#include <srs_kernel_error.hpp>
#include <srs_protocol_log.hpp>
#include <srs_kernel_mp4.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_config.hpp>

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sstream>
using namespace std;

// @global log and context.
ISrsLog* _srs_log = new SrsConsoleLog(SrsLogLevelTrace, false);
ISrsContext* _srs_context = new SrsThreadContext();

// @global config object for app module.
SrsConfig* _srs_config = new SrsConfig();

// @global Other variables.
bool _srs_in_docker = false;

// The binary name of SRS.
const char* _srs_binary = NULL;

srs_error_t parse(std::string mp4_file, bool verbose)
{
    srs_error_t err = srs_success;
    
    SrsFileReader fr;
    if ((err = fr.open(mp4_file)) != srs_success) {
        return srs_error_wrap(err, "open mp4 file %s", mp4_file.c_str());
    }
    srs_trace("MP4 file open success");
    
    SrsMp4BoxReader br;
    if ((err = br.initialize(&fr)) != srs_success) {
        return srs_error_wrap(err, "open box reader");
    }
    srs_trace("MP4 box reader open success");
    
    SrsSimpleStream* stream = new SrsSimpleStream();
    SrsAutoFree(SrsSimpleStream, stream);
    
    fprintf(stderr, "\n%s\n", mp4_file.c_str());
    while (true) {
        SrsMp4Box* box = NULL;
        SrsAutoFree(SrsMp4Box, box);
        
        if ((err = br.read(stream, &box)) != srs_success) {
            if (srs_error_code(err) == ERROR_SYSTEM_FILE_EOF) {
                fprintf(stderr, "\n");
            }
            return srs_error_wrap(err, "read box");
        }
        
        SrsBuffer* buffer = new SrsBuffer(stream->bytes(), stream->length());
        SrsAutoFree(SrsBuffer, buffer);
        
        if ((err = box->decode(buffer)) != srs_success) {
            return srs_error_wrap(err, "decode box");
        }
        
        if ((err = br.skip(box, stream)) != srs_success) {
            return srs_error_wrap(err, "skip box");
        }
        
        SrsMp4DumpContext ctx;
        ctx.level = 1;
        ctx.summary = !verbose;
        
        stringstream ss;
        fprintf(stderr, "%s", box->dumps(ss, ctx).str().c_str());
    }
    
    return err;
}

int main(int argc, char** argv)
{
    printf("SRS MP4 parser/%d.%d.%d, parse and show the mp4 boxes structure.\n",
           VERSION_MAJOR, VERSION_MINOR, VERSION_REVISION);

    _srs_binary = argv[0];
    
    if (argc < 2) {
        printf("Usage: %s <mp4_file> [verbose]\n"
               "        mp4_file The MP4 file path to parse.\n"
               "        verbose Whether print verbose of box.\n"
               "For example:\n"
               "        %s doc/source.200kbps.768x320.mp4\n"
               "        %s doc/source.200kbps.768x320.mp4 verbose\n",
               argv[0], argv[0], argv[0]);
        
        exit(-1);
    }
    string mp4_file = argv[1];
    bool verbose = false;
    if (argc > 2) {
        verbose = true;
    }
    srs_trace("Parse MP4 file %s, verbose=%d", mp4_file.c_str(), verbose);
    
    srs_error_t err = parse(mp4_file, verbose);
    int code = srs_error_code(err);

    if (code == ERROR_SYSTEM_FILE_EOF) {
        srs_trace("Parse complete");
    } else {
        srs_error("Parse error %s", srs_error_desc(err).c_str());
    }

    srs_freep(err);
    return code;
}

