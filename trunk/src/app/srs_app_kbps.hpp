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

#ifndef SRS_APP_KBPS_HPP
#define SRS_APP_KBPS_HPP

/*
#include <srs_app_kbps.hpp>
*/

#include <srs_core.hpp>

class ISrsProtocolReader;
class ISrsProtocolWriter;

/**
* a slice of kbps statistic, for input or output.
*/
class SrsKbpsSlice
{
private:
    union slice_io {
        ISrsProtocolReader* in;
        ISrsProtocolWriter* out;
    };
public:
    slice_io io;
    int64_t bytes;
    int64_t starttime;
    // startup bytes number for io when set it,
    // the base offset of bytes for io.
    int64_t io_bytes_base;
    // last updated bytes number,
    // cache for io maybe freed.
    int64_t last_bytes;
public:
    SrsKbpsSlice();
    virtual ~SrsKbpsSlice();
};

/**
* to statistic the kbps of io.
*/
class SrsKbps
{
private:
    SrsKbpsSlice is;
    SrsKbpsSlice os;
public:
    SrsKbps();
    virtual ~SrsKbps();
public:
    /**
    * set the underlayer reader/writer,
    * if the io destroied, for instance, the forwarder reconnect,
    * user must set the io of SrsKbps to NULL to continue to use the kbps object.
    * @param in the input stream statistic. can be NULL.
    * @param out the output stream statistic. can be NULL.
    * @remark if in/out is NULL, use the cached data for kbps.
    */
    virtual void set_io(ISrsProtocolReader* in, ISrsProtocolWriter* out);
public:
    /**
    * get total kbps, duration is from the startup of io.
    */
    virtual int get_send_kbps();
    virtual int get_recv_kbps();
public:
    /**
    * get the total send/recv bytes, from the startup of the oldest io.
    */
    virtual int64_t get_send_bytes();
    virtual int64_t get_recv_bytes();
};

#endif