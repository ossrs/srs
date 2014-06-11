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

#ifndef SRS_RTMP_PROTOCOL_CONSTS_HPP
#define SRS_RTMP_PROTOCOL_CONSTS_HPP

/*
#include <srs_protocol_utility.hpp>
*/
#include <srs_core.hpp>

#include <string>

// default vhost for rtmp
#define RTMP_VHOST_DEFAULT "__defaultVhost__"

#define RTMP_DEFAULT_PORT "1935"

// the default chunk size for system.
#define SRS_CONF_DEFAULT_CHUNK_SIZE 60000

// resolve the vhost in query string
// @param app, may contains the vhost in query string format:
//         app?vhost=request_vhost
//        app...vhost...request_vhost
extern void srs_vhost_resolve(std::string& vhost, std::string& app);

extern void srs_random_generate(char* bytes, int size);

#endif
