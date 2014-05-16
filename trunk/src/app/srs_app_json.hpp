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

#ifndef SRS_APP_JSON_HPP
#define SRS_APP_JSON_HPP

/*
#include <srs_app_json.hpp>
*/
#include <srs_core.hpp>

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
/* json encode
    cout<< JOBJECT_START
            << JFIELD_STR("name", "srs") << JFIELD_CONT
            << JFIELD_ORG("version", 100) << JFIELD_CONT
            << JFIELD_NAME("features") << JOBJECT_START
                << JFIELD_STR("rtmp", "released") << JFIELD_CONT
                << JFIELD_STR("hls", "released") << JFIELD_CONT
                << JFIELD_STR("dash", "plan")
            << JOBJECT_END << JFIELD_CONT
            << JFIELD_STR("author", "srs team")
        << JOBJECT_END
it's:
    cont<< "{"
            << "name:" << "srs" << ","
            << "version:" << 100 << ","
            << "features:" << "{"
                << "rtmp:" << "released" << ","
                << "hls:" << "released" << ","
                << "dash:" << "plan"
            << "}" << ","
            << "author:" << "srs team"
        << "}"
that is:
    """
        {
            "name": "srs",
            "version": 100,
            "features": {
                "rtmp": "released",
                "hls": "released",
                "dash": "plan"
            },
            "author": "srs team"
        }
    """
*/
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
#define JOBJECT_START "{"
#define JFIELD_NAME(k) "\"" << k << "\":"
#define JFIELD_STR(k, v) "\"" << k << "\":\"" << v << "\""
#define JFIELD_ORG(k, v) "\"" << k << "\":" << std::dec << v
#define JFIELD_ERROR(ret) "\"" << "code" << "\":" << ret
#define JFIELD_CONT ","
#define JOBJECT_END "}"
#define JARRAY_START "["
#define JARRAY_END "]"

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// json decode
// 1. SrsJsonAny: read any from stream
//        SrsJsonAny* pany = NULL;
//        if ((ret = srs_json_read_any(stream, &pany)) != ERROR_SUCCESS) {
//            return ret;
//         }
//        srs_assert(pany); // if success, always valid object.
// 2. SrsJsonAny: convert to specifid type, for instance, string
//        SrsJsonAny* pany = ...
//        if (pany->is_string()) {
//            string v = pany->to_str();
//        }
//
// for detail usage, see interfaces of each object.
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////
// @see: https://bitbucket.org/yarosla/nxjson
// @see: https://github.com/udp/json-parser

#endif