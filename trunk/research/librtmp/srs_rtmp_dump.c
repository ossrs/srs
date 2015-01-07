/*
The MIT License (MIT)

Copyright (c) 2013-2015 winlin

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

#include "../../objs/include/srs_librtmp.h"

void parse_amf0_object(char* p, srs_amf0_t args)
{
    char opvt = 0; // object property value type.
    const char* opnp = NULL; // object property name ptr.
    const char* opvp = NULL; // object property value ptr.
    
    while (*p) {
        switch (*p++) {
            case 'O':
                while (*p && *p++ != ':') {
                }
                if (*p++ == '1') {
                    printf("amf0 object start\n");
                } else {
                    printf("amf0 object end\n");
                }
                break;
            case 'N':
                opvt = *p++;
                if (*p++ != ':') {
                    printf("object property must split by :.\n");
                    exit(-1);
                }
                opnp = p++;
                while (*p && *p++ != ':') {
                }
                p[-1] = 0;
                opvp = p;
                printf("amf0 %c property[%s]=%s\n", opvt, opnp, opvp);
                switch(opvt) {
                    case 'S':
                        srs_amf0_object_property_set(args, opnp, srs_amf0_create_string(opvp));
                        break;
                    default:
                        printf("unsupported object property.\n");
                        exit(-1);
                }
                *p=0;
                break;
            default:
                printf("only supports an object arg.\n");
                exit(-1);
        }
    }
}

int main(int argc, char** argv)
{
    printf("dump rtmp stream to flv file\n");
    printf("srs(simple-rtmp-server) client librtmp library.\n");
    printf("version: %d.%d.%d\n", srs_version_major(), srs_version_minor(), srs_version_revision());
    printf("@refer to http://rtmpdump.mplayerhq.hu/rtmpdump.1.html\n");
    
    struct option long_options[] = {
        {"rtmp", required_argument, 0, 'r'},
        {"flv", required_argument, 0, 'o'},
        {"swfUrl", required_argument, 0, 's'},
        {"tcUrl", required_argument, 0, 't'},
        {"pageUrl", required_argument, 0, 'p'},
        {"conn", required_argument, 0, 'C'},
        {"complex", no_argument, 0, 'x'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    int show_help = 0;
    int complex_handshake = 0;
    const char* rtmp_url = NULL;
    const char* output_flv = NULL;
    const char* swfUrl = NULL;
    const char* tcUrl = NULL;
    const char* pageUrl = NULL;
    srs_amf0_t args = NULL;
    
    int opt = 0;
    int option_index = 0;
    while((opt = getopt_long(argc, argv, "hxr:o:s:t:p:C:", long_options, &option_index)) != -1){
        switch(opt){
            case 'r':
                rtmp_url = optarg;
                break;
            case 'o':
                output_flv = optarg;
                break;
            case 's':
                swfUrl = optarg;
                break;
            case 't':
                tcUrl = optarg;
                break;
            case 'p':
                pageUrl = optarg;
                break;
            case 'C':
                if (!args) {
                    args = srs_amf0_create_object();
                }
                char* p = (char*)optarg;
                parse_amf0_object(p, args);
                break;
            case 'x':
                complex_handshake = 1;
                break;
            case 'h':
                show_help = 1;
                break;
            default:
                printf("unsupported opt.\n");
                exit(-1);
        }
    }
    
    if (!rtmp_url || show_help) {
        printf("Usage: %s -r url [-o output] [-s swfUrl] [-t tcUrl] [-p pageUrl] [-C conndata] [--complex] [-h]\n"
            "Options:\n"
            "   --rtmp -r url\n"
            "       URL of the server and media content.\n"
            "   --flv -o output\n"
            "       Specify the output file name. If the name is − or is omitted, the stream is written to stdout.\n"
            "   --complex\n"
            "       Whether use complex handshake(srs-librtmp with ssl required).\n"
            "   --swfUrl -s url\n"
            "       URL of the SWF player for the media. By default no value will be sent.\n"
            "   --tcUrl -t url\n"
            "       URL of the target stream. Defaults to rtmp[e]://host[:port]/app/playpath.\n"
            "   --pageUrl -p url\n"
            "       URL of the web page in which the media was embedded. By default no value will be sent.\n"
            "   −−conn −C type:data\n"
            "       Append arbitrary AMF data to the Connect message. The type must be B for Boolean, N for number, S for string, O for object, or Z for null. For Booleans the data must be either 0 or 1 for FALSE or TRUE, respectively. Likewise for Objects the data must be 0 or 1 to end or begin an object, respectively. Data items in subobjects may be named, by prefixing the type with 'N' and specifying the name before the value, e.g. NB:myFlag:1. This option may be used multiple times to construct arbitrary AMF sequences. E.g.\n"
            "       −C B:1 −C S:authMe −C O:1 −C NN:code:1.23 −C NS:flag:ok −C O:0\n"
            "       -C O:1 -C NS:CONN:\" -C B:4Rg9vr0\" -C O:0\n"
            "       @remark, support a object args only.\n"
            "   --help -h\n"
            "       Print a summary of command options.\n"
            "For example:\n"
            "   %s -r rtmp://127.0.0.1:1935/live/livestream -o output.flv\n"
            "   %s -h\n",
            argv[0], argv[0], argv[0]);
        exit(-1);
    }
    
    srs_human_trace("rtmp url: %s", rtmp_url);
    srs_human_trace("handshake: %s", (complex_handshake? "complex" : "simple"));
    srs_human_trace("swfUrl: %s", swfUrl);
    srs_human_trace("pageUrl: %s", pageUrl);
    srs_human_trace("tcUrl: %s", tcUrl);
    if (output_flv) {
        srs_human_trace("flv output path: %s", output_flv);
    } else {
        srs_human_trace("output to console");
    }
    
    srs_rtmp_t rtmp = srs_rtmp_create(rtmp_url);
    
    if (__srs_rtmp_dns_resolve(rtmp) != 0) {
        srs_human_trace("dns resolve failed.");
        goto rtmp_destroy;
    }
    
    if (__srs_rtmp_connect_server(rtmp) != 0) {
        srs_human_trace("connect to server failed.");
        goto rtmp_destroy;
    }
    
    if (complex_handshake) {
        if (__srs_rtmp_do_complex_handshake(rtmp) != 0) {
            srs_human_trace("complex handshake failed.");
            goto rtmp_destroy;
        }
        srs_human_trace("do complex handshake success");
    } else {
        if (__srs_rtmp_do_simple_handshake(rtmp) != 0) {
            srs_human_trace("simple handshake failed.");
            goto rtmp_destroy;
        }
        srs_human_trace("do simple handshake success");
    }
    
    if (srs_rtmp_set_connect_args(rtmp, tcUrl, swfUrl, pageUrl, args) != 0) {
        srs_human_trace("set connect args failed.");
        goto rtmp_destroy;
    }
    
    if (srs_rtmp_connect_app(rtmp) != 0) {
        srs_human_trace("connect vhost/app failed.");
        goto rtmp_destroy;
    }
    srs_human_trace("connect vhost/app success");
    
    if (srs_rtmp_play_stream(rtmp) != 0) {
        srs_human_trace("play stream failed.");
        goto rtmp_destroy;
    }
    srs_human_trace("play stream success");
    
    srs_flv_t flv = NULL;
    if (output_flv) {
        flv = srs_flv_open_write(output_flv);
    }
    
    if (flv) {
        // flv header
        char header[9];
        // 3bytes, signature, "FLV",
        header[0] = 'F';
        header[1] = 'L';
        header[2] = 'V';
        // 1bytes, version, 0x01,
        header[3] = 0x01;
        // 1bytes, flags, UB[5] 0, UB[1] audio present, UB[1] 0, UB[1] video present.
        header[4] = 0x03; // audio + video.
        // 4bytes, dataoffset
        header[5] = 0x00;
        header[6] = 0x00;
        header[7] = 0x00;
        header[8] = 0x09;
        if (srs_flv_write_header(flv, header) != 0) {
            srs_human_trace("write flv header failed.");
            goto rtmp_destroy;
        }
    }
    
    for (;;) {
        int size;
        char type;
        char* data;
        u_int32_t timestamp;
        
        if (srs_rtmp_read_packet(rtmp, &type, &timestamp, &data, &size) != 0) {
            srs_human_trace("read rtmp packet failed.");
            goto rtmp_destroy;
        }
        
        if (srs_human_print_rtmp_packet(type, timestamp, data, size) != 0) {
            srs_human_trace("print rtmp packet failed.");
            goto rtmp_destroy;
        }
        
        if (flv) {
            if (srs_flv_write_tag(flv, type, timestamp, data, size) != 0) {
                srs_human_trace("dump rtmp packet failed.");
                goto rtmp_destroy;
            }
        }
        
        free(data);
    }
    
rtmp_destroy:
    srs_rtmp_destroy(rtmp);
    if (flv) {
        srs_flv_close(flv);
    }
    srs_human_trace("completed");
    
    return 0;
}
