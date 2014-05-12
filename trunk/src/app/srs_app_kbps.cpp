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

#include <srs_app_kbps.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_protocol_io.hpp>
#include <srs_kernel_utility.hpp>

SrsKbpsSlice::SrsKbpsSlice()
{
    io.in = NULL;
    io.out = NULL;
    last_bytes = io_bytes_base = starttime = bytes = 0;
}

SrsKbpsSlice::~SrsKbpsSlice()
{
}

SrsKbps::SrsKbps()
{
}

SrsKbps::~SrsKbps()
{
}

void SrsKbps::set_io(ISrsProtocolReader* in, ISrsProtocolWriter* out)
{
    // set input stream
    // now, set start time.
    if (is.starttime == 0) {
        is.starttime = srs_get_system_time_ms();
    }
    // save the old in bytes.
    if (is.io.in) {
        is.bytes += is.last_bytes - is.io_bytes_base;
    }
    // use new io.
    is.io.in = in;
    is.last_bytes = is.io_bytes_base = 0;
    if (in) {
        is.last_bytes = is.io_bytes_base = in->get_recv_bytes();
    }
    
    // set output stream
    // now, set start time.
    if (os.starttime == 0) {
        os.starttime = srs_get_system_time_ms();
    }
    // save the old in bytes.
    if (os.io.out) {
        os.bytes += os.last_bytes - os.io_bytes_base;
    }
    // use new io.
    os.io.out = out;
    os.last_bytes = os.io_bytes_base = 0;
    if (out) {
        os.last_bytes = os.io_bytes_base = out->get_send_bytes();
    }
}

int SrsKbps::get_send_kbps()
{
    int64_t duration = srs_get_system_time_ms() - is.starttime;
    int64_t bytes = get_send_bytes();
    if (duration <= 0) {
        return 0;
    }
    return bytes * 8 / duration;
}

int SrsKbps::get_recv_kbps()
{
    int64_t duration = srs_get_system_time_ms() - os.starttime;
    int64_t bytes = get_recv_bytes();
    if (duration <= 0) {
        return 0;
    }
    return bytes * 8 / duration;
}

int64_t SrsKbps::get_send_bytes()
{
    if (os.io.out) {
        os.last_bytes = os.io.out->get_send_bytes();
    }
    return os.bytes + os.last_bytes - os.io_bytes_base;
}

int64_t SrsKbps::get_recv_bytes()
{
    if (is.io.in) {
        is.last_bytes = is.io.in->get_recv_bytes();
    }
    return is.bytes + is.last_bytes - is.io_bytes_base;
}

