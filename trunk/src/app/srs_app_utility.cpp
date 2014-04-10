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

#include <srs_app_utility.hpp>

#include <srs_kernel_log.hpp>
#include <srs_app_config.hpp>

int srs_get_log_level(std::string level)
{
    if ("verbose" == _srs_config->get_srs_log_level()) {
        return SrsLogLevel::Verbose;
    } else if ("info" == _srs_config->get_srs_log_level()) {
        return SrsLogLevel::Info;
    } else if ("trace" == _srs_config->get_srs_log_level()) {
        return SrsLogLevel::Trace;
    } else if ("warn" == _srs_config->get_srs_log_level()) {
        return SrsLogLevel::Warn;
    } else if ("error" == _srs_config->get_srs_log_level()) {
        return SrsLogLevel::Error;
    } else {
        return SrsLogLevel::Trace;
    }
}
