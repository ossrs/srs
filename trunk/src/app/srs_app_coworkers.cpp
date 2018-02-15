/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2018 Winlin
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

#include <srs_app_coworkers.hpp>

using namespace std;

#include <srs_protocol_json.hpp>
#include <srs_kernel_error.hpp>

SrsCoWorkers* SrsCoWorkers::_instance = NULL;

SrsCoWorkers::SrsCoWorkers()
{
}

SrsCoWorkers::~SrsCoWorkers()
{
}

SrsCoWorkers* SrsCoWorkers::instance()
{
    if (!_instance) {
        _instance = new SrsCoWorkers();
    }
    return _instance;
}

SrsJsonAny* SrsCoWorkers::dumps(string vhost, string app, string stream)
{
    return SrsJsonAny::null();
}

srs_error_t SrsCoWorkers::on_publish(SrsSource* s, SrsRequest* r)
{
    srs_error_t err = srs_success;
    return err;
}

void SrsCoWorkers::on_unpublish(SrsSource* s, SrsRequest* r)
{
}

